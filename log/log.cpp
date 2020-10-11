#include <string.h>
#include <time.h>
#include <sys/time.h>
#include <stdarg.h>
#include "log.h"
#include <pthread.h>
using namespace std;

Log::Log()
{
    m_count = 0;
    m_is_async = false;
}

Log::~Log()
{
    if (m_fp != NULL)
    {
        fclose(m_fp);
    }
}
//异步需要设置阻塞队列的长度，同步不需要设置
//同步日志就是当要输出日志时，必须等日志输出语句执行完毕之后再执行下一句。异步日志则是日志输出语句与业务逻辑语句并不是在同一个线程中运行，而是有专门的线程用于进行日志输出操作，处理业务逻辑的主线程不用等待即可执行后续业务逻辑，异步日志需要阻塞队列来保存需要异步输出的日志事件
bool Log::init(const char *file_name, int close_log, int log_buf_size, int split_lines, int max_queue_size){
    //如果设置了max_queue_size,则设置为异步
    if (max_queue_size >= 1)
    {
        m_is_async = true;
        m_log_queue = new block_queue<string>(max_queue_size);
        pthread_t tid;
        //flush_log_thread为回调函数,这里表示创建线程异步写日志
        pthread_create(&tid, NULL, flush_log_thread, NULL);
    }
    
    m_close_log = close_log;//是否关闭日志
    m_log_buf_size = log_buf_size;//日志缓冲区大小
    m_buf = new char[m_log_buf_size];//日志缓冲
    memset(m_buf, '\0', m_log_buf_size);//将某一块内存中的内容全部设置为指定的值
    m_split_lines = split_lines;//日志最大行数

    time_t t = time(NULL);
    struct tm *sys_tm = localtime(&t);
    struct tm my_tm = *sys_tm;

 
    const char *p = strrchr(file_name, '/');//C 库函数 char *strrchr(const char *str, int c) 在参数 str 所指向的字符串中搜索最后一次出现字符 c（一个无符号字符）的位置，最后返回的是c后面的字符串，这里就是返回日志文件名
    char log_full_name[256] = {0};

    if (p == NULL)//如果为空说明没有/，也就是说原本就是一个文件名没有路径，那么就以日期重新命名
    {
        snprintf(log_full_name, 255, "%d_%02d_%02d_%s", my_tm.tm_year + 1900, my_tm.tm_mon + 1, my_tm.tm_mday, file_name);//C 库函数 int snprintf(char *str, size_t size, const char *format, ...) 设将可变参数(...)按照 format 格式化成字符串，并将字符串复制到 str 中，size 为要写入的字符的最大数目，超过 size 会被截断。
    }
    else
    {
        strcpy(log_name, p + 1);//C 库函数 char *strcpy(char *dest, const char *src) 把 src 所指向的字符串复制到 dest。
        strncpy(dir_name, file_name, p - file_name + 1);//C 库函数 char *strncpy(char *dest, const char *src, size_t n) 把 src 所指向的字符串复制到 dest，最多复制 n 个字符。当 src 的长度小于 n 时，dest 的剩余部分将用空字节填充。
        snprintf(log_full_name, 255, "%s%d_%02d_%02d_%s", dir_name, my_tm.tm_year + 1900, my_tm.tm_mon + 1, my_tm.tm_mday, log_name);
    }

    m_today = my_tm.tm_mday;
    
    m_fp = fopen(log_full_name, "a");////打开log的文件指针
    if (m_fp == NULL)
    {
        return false;
    }

    return true;
}

void Log::write_log(int level, const char *format, ...)
{
    struct timeval now = {0, 0};
    gettimeofday(&now, NULL);
    time_t t = now.tv_sec;
    struct tm *sys_tm = localtime(&t);
    struct tm my_tm = *sys_tm;
    char s[16] = {0};
    switch (level)
    {
    case 0:
        strcpy(s, "[debug]:");
        break;
    case 1:
        strcpy(s, "[info]:");
        break;
    case 2:
        strcpy(s, "[warn]:");
        break;
    case 3:
        strcpy(s, "[erro]:");
        break;
    default:
        strcpy(s, "[info]:");
        break;
    }
    //写入一个log，对m_count++, m_split_lines最大行数
    m_mutex.lock();
    m_count++;

    //每个日志都是按天记的，每个日志也都有最大行数，所以如果是日期变了或者行数超了就都要新建日志
    if (m_today != my_tm.tm_mday || m_count % m_split_lines == 0) //everyday log
    {
        
        char new_log[256] = {0};
        fflush(m_fp);//fflush()用于清空文件缓冲区，如果文件是以写的方式打开 的，则把缓冲区内容写入文件。
        fclose(m_fp);//fclose()函数用来关闭当前文件流
        char tail[16] = {0};
       
        snprintf(tail, 16, "%d_%02d_%02d_", my_tm.tm_year + 1900, my_tm.tm_mon + 1, my_tm.tm_mday);
       
        if (m_today != my_tm.tm_mday)
        {
            snprintf(new_log, 255, "%s%s%s", dir_name, tail, log_name);
            m_today = my_tm.tm_mday;
            m_count = 0;
        }
        else
        {
            snprintf(new_log, 255, "%s%s%s.%lld", dir_name, tail, log_name, m_count / m_split_lines);
        }
        m_fp = fopen(new_log, "a");
    }

    m_mutex.unlock();

    va_list valst;
    va_start(valst, format);

    string log_str;
    m_mutex.lock();

    //写入的具体时间内容格式
    int n = snprintf(m_buf, 48, "%d-%02d-%02d %02d:%02d:%02d.%06ld %s ",
                     my_tm.tm_year + 1900, my_tm.tm_mon + 1, my_tm.tm_mday,
                     my_tm.tm_hour, my_tm.tm_min, my_tm.tm_sec, now.tv_usec, s);
    
    int m = vsnprintf(m_buf + n, m_log_buf_size - 1, format, valst);//与snprintf一样，区别就在于最后的输入为valst
    m_buf[n + m] = '\n';
    m_buf[n + m + 1] = '\0';
    log_str = m_buf;

    m_mutex.unlock();

    if (m_is_async && !m_log_queue->full())
    {
        m_log_queue->push(log_str);
    }
    else
    {
        m_mutex.lock();
        fputs(log_str.c_str(), m_fp);
        m_mutex.unlock();
    }

    va_end(valst);
}

void Log::flush(void)
{
    m_mutex.lock();
    //强制刷新写入流缓冲区
    fflush(m_fp);
    m_mutex.unlock();
}
