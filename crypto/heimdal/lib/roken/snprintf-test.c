/*
 * Copyright (c) 2000 - 2001 Kungliga Tekniska Högskolan
 * (Royal Institute of Technology, Stockholm, Sweden).
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 *
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * 3. Neither the name of KTH nor the names of its contributors may be
 *    used to endorse or promote products derived from this software without
 *    specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY KTH AND ITS CONTRIBUTORS ``AS IS'' AND ANY
 * EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL KTH OR ITS CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR
 * BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
 * WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR
 * OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF
 * ADVISED OF THE POSSIBILITY OF SUCH DAMAGE. */

#include <config.h>
#include "roken.h"
#include <limits.h>

static int
try (const char *format, ...)
{
    int ret;
    va_list ap;
    char buf1[256], buf2[256];

    va_start (ap, format);
    ret = rk_vsnprintf (buf1, sizeof(buf1), format, ap);
    if (ret >= sizeof(buf1))
	errx (1, "increase buf and try again");
    va_end (ap);
    va_start (ap, format);
    vsprintf (buf2, format, ap);
    ret = strcmp (buf1, buf2);
    if (ret)
	printf ("failed: format = \"%s\", \"%s\" != \"%s\"\n",
		format, buf1, buf2);
    va_end (ap);
    return ret;
}

static int
cmp_with_sprintf_int (void)
{
    int tot = 0;
    int int_values[] = {INT_MIN, -17, -1, 0, 1, 17, 4711, 65535, INT_MAX};
    int i;

    for (i = 0; i < sizeof(int_values) / sizeof(int_values[0]); ++i) {
	tot += try ("%d", int_values[i]);
	tot += try ("%x", int_values[i]);
	tot += try ("%X", int_values[i]);
	tot += try ("%o", int_values[i]);
	tot += try ("%#x", int_values[i]);
	tot += try ("%#X", int_values[i]);
	tot += try ("%#o", int_values[i]);
	tot += try ("%10d", int_values[i]);
	tot += try ("%10x", int_values[i]);
	tot += try ("%10X", int_values[i]);
	tot += try ("%10o", int_values[i]);
	tot += try ("%#10x", int_values[i]);
	tot += try ("%#10X", int_values[i]);
	tot += try ("%#10o", int_values[i]);
	tot += try ("%-10d", int_values[i]);
	tot += try ("%-10x", int_values[i]);
	tot += try ("%-10X", int_values[i]);
	tot += try ("%-10o", int_values[i]);
	tot += try ("%-#10x", int_values[i]);
	tot += try ("%-#10X", int_values[i]);
	tot += try ("%-#10o", int_values[i]);
    }
    return tot;
}

static int
cmp_with_sprintf_long (void)
{
    int tot = 0;
    long long_values[] = {LONG_MIN, -17, -1, 0, 1, 17, 4711, 65535, LONG_MAX};
    int i;

    for (i = 0; i < sizeof(long_values) / sizeof(long_values[0]); ++i) {
	tot += try ("%ld", long_values[i]);
	tot += try ("%lx", long_values[i]);
	tot += try ("%lX", long_values[i]);
	tot += try ("%lo", long_values[i]);
	tot += try ("%#lx", long_values[i]);
	tot += try ("%#lX", long_values[i]);
	tot += try ("%#lo", long_values[i]);
	tot += try ("%10ld", long_values[i]);
	tot += try ("%10lx", long_values[i]);
	tot += try ("%10lX", long_values[i]);
	tot += try ("%10lo", long_values[i]);
	tot += try ("%#10lx", long_values[i]);
	tot += try ("%#10lX", long_values[i]);
	tot += try ("%#10lo", long_values[i]);
	tot += try ("%-10ld", long_values[i]);
	tot += try ("%-10lx", long_values[i]);
	tot += try ("%-10lX", long_values[i]);
	tot += try ("%-10lo", long_values[i]);
	tot += try ("%-#10lx", long_values[i]);
	tot += try ("%-#10lX", long_values[i]);
	tot += try ("%-#10lo", long_values[i]);
    }
    return tot;
}

#ifdef HAVE_LONG_LONG

/* XXX doesn't work as expected on lp64 platforms with sizeof(long
 * long) == sizeof(long) */

static int
cmp_with_sprintf_long_long (void)
{
    int tot = 0;
    long long long_long_values[] = {
	((long long)LONG_MIN) -1, LONG_MIN, -17, -1,
	0,
	1, 17, 4711, 65535, LONG_MAX, ((long long)LONG_MAX) + 1};
    int i;

    for (i = 0; i < sizeof(long_long_values) / sizeof(long_long_values[0]); ++i) {
	tot += try ("%lld", long_long_values[i]);
	tot += try ("%llx", long_long_values[i]);
	tot += try ("%llX", long_long_values[i]);
	tot += try ("%llo", long_long_values[i]);
	tot += try ("%#llx", long_long_values[i]);
	tot += try ("%#llX", long_long_values[i]);
	tot += try ("%#llo", long_long_values[i]);
	tot += try ("%10lld", long_long_values[i]);
	tot += try ("%10llx", long_long_values[i]);
	tot += try ("%10llX", long_long_values[i]);
	tot += try ("%10llo", long_long_values[i]);
	tot += try ("%#10llx", long_long_values[i]);
	tot += try ("%#10llX", long_long_values[i]);
	tot += try ("%#10llo", long_long_values[i]);
	tot += try ("%-10lld", long_long_values[i]);
	tot += try ("%-10llx", long_long_values[i]);
	tot += try ("%-10llX", long_long_values[i]);
	tot += try ("%-10llo", long_long_values[i]);
	tot += try ("%-#10llx", long_long_values[i]);
	tot += try ("%-#10llX", long_long_values[i]);
	tot += try ("%-#10llo", long_long_values[i]);
    }
    return tot;
}

#endif

#if 0
static int
cmp_with_sprintf_float (void)
{
    int tot = 0;
    double double_values[] = {-99999, -999, -17.4, -4.3, -3.0, -1.5, -1,
			      0, 0.1, 0.2342374852, 0.2340007,
			      3.1415926, 14.7845, 34.24758, 9999, 9999999};
    int i;

    for (i = 0; i < sizeof(double_values) / sizeof(double_values[0]); ++i) {
	tot += try ("%f", double_values[i]);
	tot += try ("%10f", double_values[i]);
	tot += try ("%.2f", double_values[i]);
	tot += try ("%7.0f", double_values[i]);
	tot += try ("%5.2f", double_values[i]);
	tot += try ("%0f", double_values[i]);
	tot += try ("%#f", double_values[i]);
	tot += try ("%e", double_values[i]);
	tot += try ("%10e", double_values[i]);
	tot += try ("%.2e", double_values[i]);
	tot += try ("%7.0e", double_values[i]);
	tot += try ("%5.2e", double_values[i]);
	tot += try ("%0e", double_values[i]);
	tot += try ("%#e", double_values[i]);
	tot += try ("%E", double_values[i]);
	tot += try ("%10E", double_values[i]);
	tot += try ("%.2E", double_values[i]);
	tot += try ("%7.0E", double_values[i]);
	tot += try ("%5.2E", double_values[i]);
	tot += try ("%0E", double_values[i]);
	tot += try ("%#E", double_values[i]);
	tot += try ("%g", double_values[i]);
	tot += try ("%10g", double_values[i]);
	tot += try ("%.2g", double_values[i]);
	tot += try ("%7.0g", double_values[i]);
	tot += try ("%5.2g", double_values[i]);
	tot += try ("%0g", double_values[i]);
	tot += try ("%#g", double_values[i]);
	tot += try ("%G", double_values[i]);
	tot += try ("%10G", double_values[i]);
	tot += try ("%.2G", double_values[i]);
	tot += try ("%7.0G", double_values[i]);
	tot += try ("%5.2G", double_values[i]);
	tot += try ("%0G", double_values[i]);
	tot += try ("%#G", double_values[i]);
    }
    return tot;
}
#endif

static int
test_null (void)
{
    return rk_snprintf (NULL, 0, "foo") != 3;
}

static int
test_sizet (void)
{
    int tot = 0;
    size_t sizet_values[] = { 0, 1, 2, 200, 4294967295u }; /* SIZE_MAX */
    char *result[] = { "0", "1", "2", "200", "4294967295" };
    int i;

    for (i = 0; i < sizeof(sizet_values) / sizeof(sizet_values[0]); ++i) {
#if 0
	tot += try("%zu", sizet_values[i]);
	tot += try("%zx", sizet_values[i]);
	tot += try("%zX", sizet_values[i]);
#else
	char buf[256];
	rk_snprintf(buf, sizeof(buf), "%zu", sizet_values[i]);
	if (strcmp(buf, result[i]) != 0) {
	    printf("%s != %s", buf, result[i]);
	    tot++;
	}
#endif
    }
    return tot;
}


int
main (int argc, char **argv)
{
    int ret = 0;

    ret += cmp_with_sprintf_int ();
    ret += cmp_with_sprintf_long ();
#ifdef HAVE_LONG_LONG
    ret += cmp_with_sprintf_long_long ();
#endif
    ret += test_null ();
    ret += test_sizet ();
    return ret;
}
