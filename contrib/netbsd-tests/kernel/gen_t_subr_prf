#!/bin/sh

cat << _EOF > $2
#include <sys/types.h>
#include <sys/time.h>
#include <stdio.h>
#include <stdarg.h>
#include <stdint.h>
#include <string.h>
#include <sha2.h>

#include <atf-c.h>

/* Avoid SSP re-definitions */
#undef snprintf
#undef vsnprintf
#undef sprintf
#undef vsprintf

#define KPRINTF_BUFSIZE 1024
#undef putchar
#define putchar xputchar

static int putchar(char c, int foo, void *b)
{
	return fputc(c, stderr);
}

#define TOBUFONLY 1
static const char HEXDIGITS[] = "0123456789ABCDEF";
static const char hexdigits[] = "0123456789abcdef";

typedef int device_t;

#if 0
static SHA512_CTX kprnd_sha;
#endif

#define timespec timeval
#define nanotime(ts) gettimeofday(ts, NULL)

#define device_xname(a) ""
int kprintf(const char *, int, void *, char *, va_list) __printflike(1, 0);
void device_printf(device_t, const char *, ...) __printflike(2, 3);

static void
empty(void)
{
}

static void (*v_flush)(void) = empty;

ATF_TC(snprintf_print);
ATF_TC_HEAD(snprintf_print, tc)
{
        atf_tc_set_md_var(tc, "descr", "checks snprintf print");
}
 
ATF_TC_BODY(snprintf_print, tc)
{
	char buf[10];
	int i;

	memset(buf, 'x', sizeof(buf));
	i = snprintf(buf, sizeof(buf), "number %d", 10);
	ATF_CHECK_EQ(i, 9);
	ATF_CHECK_STREQ(buf, "number 10");
}

ATF_TC(snprintf_print_overflow);
ATF_TC_HEAD(snprintf_print_overflow, tc)
{
        atf_tc_set_md_var(tc, "descr", "checks snprintf print with overflow");
}
 
ATF_TC_BODY(snprintf_print_overflow, tc)
{
	char buf[10];
	int i;

	memset(buf, 'x', sizeof(buf));
	i = snprintf(buf, sizeof(buf), "fjsdfsdjfsdf %d\n", 10);
	ATF_CHECK_EQ(i, 16);
	ATF_CHECK_STREQ(buf, "fjsdfsdjf");
}

ATF_TC(snprintf_count);
ATF_TC_HEAD(snprintf_count, tc)
{
        atf_tc_set_md_var(tc, "descr", "checks snprintf count");
}
 
ATF_TC_BODY(snprintf_count, tc)
{
	int i;
	
	i = snprintf(NULL, 20, "number %d", 10);
	ATF_CHECK_EQ(i, 9);
}

ATF_TC(snprintf_count_overflow);
ATF_TC_HEAD(snprintf_count_overflow, tc)
{
        atf_tc_set_md_var(tc, "descr", "checks snprintf count with overflow");
}
 
ATF_TC_BODY(snprintf_count_overflow, tc)
{
	int i;

	i = snprintf(NULL, 10, "fjsdfsdjfsdf %d\n", 10);
	ATF_CHECK_EQ(i, 16);
}

ATF_TP_ADD_TCS(tp)
{
        ATF_TP_ADD_TC(tp, snprintf_print);
        ATF_TP_ADD_TC(tp, snprintf_print_overflow);
        ATF_TP_ADD_TC(tp, snprintf_count);
        ATF_TP_ADD_TC(tp, snprintf_count_overflow);

        return atf_no_error();
}
_EOF

awk '
/^snprintf\(/ {
	print prevline
	out = 1
}
{
	if (out) print
	else prevline = $0
}' $1 >>$2
