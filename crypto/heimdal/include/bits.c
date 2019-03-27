/*
 * Copyright (c) 1997-2002 Kungliga Tekniska Högskolan
 * (Royal Institute of Technology, Stockholm, Sweden).
 * All rights reserved.
 *
 * Portions Copyright (c) 2010 Apple Inc. All rights reserved.
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
 * 3. Neither the name of the Institute nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE INSTITUTE AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE INSTITUTE OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
RCSID("$Id$");
#endif
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <ctype.h>
#ifdef WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#endif

#define BITSIZE(TYPE)						\
{								\
    int b = 0; TYPE x = 1, zero = 0; const char *pre = "u";	\
    char tmp[128], tmp2[128];					\
    while(x){ x <<= 1; b++; if(x < zero) pre=""; }		\
    if(b >= len){						\
        size_t tabs;						\
	sprintf(tmp, "%sint%d_t" , pre, len);			\
	sprintf(tmp2, "typedef %s %s;", #TYPE, tmp);		\
	tabs = 5 - strlen(tmp2) / 8;				\
        fprintf(f, "%s", tmp2);					\
	while(tabs-- > 0) fprintf(f, "\t");			\
	fprintf(f, "/* %2d bits */\n", b);			\
        return;                                                 \
    }								\
}

#ifndef HAVE___ATTRIBUTE__
#define __attribute__(x)
#endif

static void
try_signed(FILE *f, int len)  __attribute__ ((unused));

static void
try_unsigned(FILE *f, int len) __attribute__ ((unused));

static int
print_bt(FILE *f, int flag) __attribute__ ((unused));

static void
try_signed(FILE *f, int len)
{
    BITSIZE(signed char);
    BITSIZE(short);
    BITSIZE(int);
    BITSIZE(long);
#ifdef HAVE_LONG_LONG
    BITSIZE(long long);
#endif
    fprintf(f, "/* There is no %d bit type */\n", len);
}

static void
try_unsigned(FILE *f, int len)
{
    BITSIZE(unsigned char);
    BITSIZE(unsigned short);
    BITSIZE(unsigned int);
    BITSIZE(unsigned long);
#ifdef HAVE_LONG_LONG
    BITSIZE(unsigned long long);
#endif
    fprintf(f, "/* There is no %d bit type */\n", len);
}

static int
print_bt(FILE *f, int flag)
{
    if(flag == 0){
	fprintf(f, "/* For compatibility with various type definitions */\n");
	fprintf(f, "#ifndef __BIT_TYPES_DEFINED__\n");
	fprintf(f, "#define __BIT_TYPES_DEFINED__\n");
	fprintf(f, "\n");
    }
    return 1;
}

int main(int argc, char **argv)
{
    FILE *f;
    int flag;
    const char *fn, *hb;

    if (argc > 1 && strcmp(argv[1], "--version") == 0) {
	printf("some version");
	return 0;
    }

    if(argc < 2){
	fn = "bits.h";
	hb = "__BITS_H__";
	f = stdout;
    } else {
	char *p;
	fn = argv[1];
	p = malloc(strlen(fn) + 5);
	sprintf(p, "__%s__", fn);
	hb = p;
	for(; *p; p++){
	    if(!isalnum((unsigned char)*p))
		*p = '_';
	}
	f = fopen(argv[1], "w");
    }
    fprintf(f, "/* %s -- this file was generated for %s by\n", fn, HOST);
    fprintf(f, "   %*s    %s */\n\n", (int)strlen(fn), "",
	    "$Id$");
    fprintf(f, "#ifndef %s\n", hb);
    fprintf(f, "#define %s\n", hb);
    fprintf(f, "\n");
#ifdef HAVE_INTTYPES_H
    fprintf(f, "#include <inttypes.h>\n");
#endif
#ifdef HAVE_SYS_TYPES_H
    fprintf(f, "#include <sys/types.h>\n");
#endif
#ifdef HAVE_SYS_BITYPES_H
    fprintf(f, "#include <sys/bitypes.h>\n");
#endif
#ifdef HAVE_BIND_BITYPES_H
    fprintf(f, "#include <bind/bitypes.h>\n");
#endif
#ifdef HAVE_NETINET_IN6_MACHTYPES_H
    fprintf(f, "#include <netinet/in6_machtypes.h>\n");
#endif
#ifdef HAVE_SOCKLEN_T
#ifndef WIN32
    fprintf(f, "#include <sys/socket.h>\n");
#else
    fprintf(f, "#include <winsock2.h>\n");
    fprintf(f, "#include <ws2tcpip.h>\n");
#endif
#endif
    fprintf(f, "\n");

    flag = 0;
#ifndef HAVE_INT8_T
    flag = print_bt(f, flag);
    try_signed (f, 8);
#endif /* HAVE_INT8_T */
#ifndef HAVE_INT16_T
    flag = print_bt(f, flag);
    try_signed (f, 16);
#endif /* HAVE_INT16_T */
#ifndef HAVE_INT32_T
    flag = print_bt(f, flag);
    try_signed (f, 32);
#endif /* HAVE_INT32_T */
#ifndef HAVE_INT64_T
    flag = print_bt(f, flag);
    try_signed (f, 64);
#endif /* HAVE_INT64_T */

#ifndef HAVE_UINT8_T
    flag = print_bt(f, flag);
    try_unsigned (f, 8);
#endif /* HAVE_UINT8_T */
#ifndef HAVE_UINT16_T
    flag = print_bt(f, flag);
    try_unsigned (f, 16);
#endif /* HAVE_UINT16_T */
#ifndef HAVE_UINT32_T
    flag = print_bt(f, flag);
    try_unsigned (f, 32);
#endif /* HAVE_UINT32_T */
#ifndef HAVE_UINT64_T
    flag = print_bt(f, flag);
    try_unsigned (f, 64);
#endif /* HAVE_UINT64_T */

#define X(S) fprintf(f, "typedef uint" #S "_t u_int" #S "_t;\n")
#ifndef HAVE_U_INT8_T
    flag = print_bt(f, flag);
    X(8);
#endif /* HAVE_U_INT8_T */
#ifndef HAVE_U_INT16_T
    flag = print_bt(f, flag);
    X(16);
#endif /* HAVE_U_INT16_T */
#ifndef HAVE_U_INT32_T
    flag = print_bt(f, flag);
    X(32);
#endif /* HAVE_U_INT32_T */
#ifndef HAVE_U_INT64_T
    flag = print_bt(f, flag);
    X(64);
#endif /* HAVE_U_INT64_T */

    if(flag){
	fprintf(f, "\n");
	fprintf(f, "#endif /* __BIT_TYPES_DEFINED__ */\n\n");
    }
#ifdef KRB5
    fprintf(f, "\n");
#if defined(HAVE_SOCKLEN_T)
    fprintf(f, "typedef socklen_t krb5_socklen_t;\n");
#else
    fprintf(f, "typedef int krb5_socklen_t;\n");
#endif
#if defined(HAVE_SSIZE_T)
#ifdef HAVE_UNISTD_H
    fprintf(f, "#include <unistd.h>\n");
#endif
    fprintf(f, "typedef ssize_t krb5_ssize_t;\n");
#else
    fprintf(f, "typedef int krb5_ssize_t;\n");
#endif
    fprintf(f, "\n");

#if defined(_WIN32)
    fprintf(f, "typedef SOCKET krb5_socket_t;\n");
#else
    fprintf(f, "typedef int krb5_socket_t;\n");
#endif
    fprintf(f, "\n");

#endif /* KRB5 */

    fprintf(f, "#ifndef HEIMDAL_DEPRECATED\n");
    fprintf(f, "#if defined(__GNUC__) && ((__GNUC__ > 3) || ((__GNUC__ == 3) && (__GNUC_MINOR__ >= 1 )))\n");
    fprintf(f, "#define HEIMDAL_DEPRECATED __attribute__((deprecated))\n");
    fprintf(f, "#elif defined(_MSC_VER) && (_MSC_VER>1200)\n");
    fprintf(f, "#define HEIMDAL_DEPRECATED __declspec(deprecated)\n");
    fprintf(f, "#else\n");
    fprintf(f, "#define HEIMDAL_DEPRECATED\n");
    fprintf(f, "#endif\n");
    fprintf(f, "#endif\n");

    fprintf(f, "#ifndef HEIMDAL_PRINTF_ATTRIBUTE\n");
    fprintf(f, "#if defined(__GNUC__) && ((__GNUC__ > 3) || ((__GNUC__ == 3) && (__GNUC_MINOR__ >= 1 )))\n");
    fprintf(f, "#define HEIMDAL_PRINTF_ATTRIBUTE(x) __attribute__((format x))\n");
    fprintf(f, "#else\n");
    fprintf(f, "#define HEIMDAL_PRINTF_ATTRIBUTE(x)\n");
    fprintf(f, "#endif\n");
    fprintf(f, "#endif\n");

    fprintf(f, "#ifndef HEIMDAL_NORETURN_ATTRIBUTE\n");
    fprintf(f, "#if defined(__GNUC__) && ((__GNUC__ > 3) || ((__GNUC__ == 3) && (__GNUC_MINOR__ >= 1 )))\n");
    fprintf(f, "#define HEIMDAL_NORETURN_ATTRIBUTE __attribute__((noreturn))\n");
    fprintf(f, "#else\n");
    fprintf(f, "#define HEIMDAL_NORETURN_ATTRIBUTE\n");
    fprintf(f, "#endif\n");
    fprintf(f, "#endif\n");

    fprintf(f, "#ifndef HEIMDAL_UNUSED_ATTRIBUTE\n");
    fprintf(f, "#if defined(__GNUC__) && ((__GNUC__ > 3) || ((__GNUC__ == 3) && (__GNUC_MINOR__ >= 1 )))\n");
    fprintf(f, "#define HEIMDAL_UNUSED_ATTRIBUTE __attribute__((unused))\n");
    fprintf(f, "#else\n");
    fprintf(f, "#define HEIMDAL_UNUSED_ATTRIBUTE\n");
    fprintf(f, "#endif\n");
    fprintf(f, "#endif\n");

    fprintf(f, "#endif /* %s */\n", hb);

    if (f != stdout)
	fclose(f);
    return 0;
}
