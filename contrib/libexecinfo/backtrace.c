/*	$NetBSD: backtrace.c,v 1.3 2013/08/29 14:58:56 christos Exp $	*/

/*-
 * Copyright (c) 2012 The NetBSD Foundation, Inc.
 * All rights reserved.
 *
 * This code is derived from software contributed to The NetBSD Foundation
 * by Christos Zoulas.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE NETBSD FOUNDATION, INC. AND CONTRIBUTORS
 * ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED
 * TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE FOUNDATION OR CONTRIBUTORS
 * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */
#include <sys/cdefs.h>
__RCSID("$NetBSD: backtrace.c,v 1.3 2013/08/29 14:58:56 christos Exp $");

#include <sys/param.h>
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdarg.h>
#include <stdint.h>
#include <stddef.h>
#include <unistd.h>
#include <fcntl.h>
#include <dlfcn.h>
#include <elf.h>

#include "execinfo.h"
#include "symtab.h"

#ifdef __linux__
#define SELF	"/proc/self/exe"
#else
#include <sys/sysctl.h>
#define SELF	"/proc/curproc/file"
#endif

static int
open_self(int flags)
{
	const char *pathname = SELF;
#ifdef KERN_PROC_PATHNAME
	static const int name[] = {
		CTL_KERN, KERN_PROC, KERN_PROC_PATHNAME, -1,
	};
	char path[MAXPATHLEN];
	size_t len;

	len = sizeof(path);
	if (sysctl(name, 4, path, &len, NULL, 0) != -1)
		pathname = path;
#endif
	return open(pathname, flags);
}


static int __printflike(4, 5)
rasprintf(char **buf, size_t *bufsiz, size_t offs, const char *fmt, ...)
{
	for (;;) {
		size_t nbufsiz;
		char *nbuf;

		if (*buf && offs < *bufsiz) {
			va_list ap;
			int len;

			va_start(ap, fmt);
			len = vsnprintf(*buf + offs, *bufsiz - offs, fmt, ap);
			va_end(ap);

			if (len < 0 || (size_t)len + 1 < *bufsiz - offs)
				return len;
			nbufsiz = MAX(*bufsiz + 512, (size_t)len + 1);
		} else
			nbufsiz = MAX(offs, *bufsiz) + 512;
			
		nbuf = realloc(*buf, nbufsiz);
		if (nbuf == NULL)
			return -1;
		*buf = nbuf;
		*bufsiz = nbufsiz;
	}
}

/*
 * format specifiers:
 *	%a	= address
 *	%n	= symbol_name
 *	%d	= symbol_address - address
 *	%D	= if symbol_address == address "" else +%d
 *	%f	= filename
 */
static ssize_t
format_string(char **buf, size_t *bufsiz, size_t offs, const char *fmt,
    Dl_info *dli, const void *addr)
{
	ptrdiff_t diff = (const char *)addr - (const char *)dli->dli_saddr;
	size_t o = offs;
	int len;

	for (; *fmt; fmt++) {
		if (*fmt != '%')
			goto printone;
		switch (*++fmt) {
		case 'a':
			len = rasprintf(buf, bufsiz, o, "%p", addr);
			break;
		case 'n':
			len = rasprintf(buf, bufsiz, o, "%s", dli->dli_sname);
			break;
		case 'D':
			if (diff)
				len = rasprintf(buf, bufsiz, o, "+0x%tx", diff);
			else
				len = 0;
			break;
		case 'd':
			len = rasprintf(buf, bufsiz, o, "0x%tx", diff);
			break;
		case 'f':
			len = rasprintf(buf, bufsiz, o, "%s", dli->dli_fname);
			break;
		default:
		printone:
			len = rasprintf(buf, bufsiz, o, "%c", *fmt);
			break;
		}
		if (len == -1)
			return -1;
		o += len;
	}
	return o - offs;
}

static ssize_t
format_address(symtab_t *st, char **buf, size_t *bufsiz, size_t offs,
    const char *fmt, const void *addr)
{
	Dl_info dli;

	memset(&dli, 0, sizeof(dli));
	(void)dladdr(addr, &dli);
	if (st)
		symtab_find(st, addr, &dli);

	if (dli.dli_sname == NULL)
		dli.dli_sname = "???";
	if (dli.dli_fname == NULL)
		dli.dli_fname = "???";
	if (dli.dli_saddr == NULL)
		dli.dli_saddr = (void *)(intptr_t)addr;

	return format_string(buf, bufsiz, offs, fmt, &dli, addr);
}

char **
backtrace_symbols_fmt(void *const *trace, size_t len, const char *fmt)
{

	static const size_t slen = sizeof(char *) + 64;	/* estimate */
	char *ptr;
	symtab_t *st;
	int fd;

	if ((fd = open_self(O_RDONLY)) != -1)
		st = symtab_create(fd, -1, STT_FUNC);
	else
		st = NULL;

	if ((ptr = calloc(len, slen)) == NULL)
		goto out;

	size_t psize = len * slen;
	size_t offs = len * sizeof(char *);

	/* We store only offsets in the first pass because of realloc */
	for (size_t i = 0; i < len; i++) {
		ssize_t x;
		((char **)(void *)ptr)[i] = (void *)offs;
		x = format_address(st, &ptr, &psize, offs, fmt, trace[i]);
		if (x == -1) {
			free(ptr);
			ptr = NULL;
			goto out;
		}
		offs += x;
		ptr[offs++] = '\0';
		assert(offs < psize);
	}

	/* Change offsets to pointers */
	for (size_t j = 0; j < len; j++)
		((char **)(void *)ptr)[j] += (intptr_t)ptr;

out:
	symtab_destroy(st);
	if (fd != -1)
		(void)close(fd);

	return (void *)ptr;
}

int
backtrace_symbols_fd_fmt(void *const *trace, size_t len, int fd,
    const char *fmt)
{
	char **s = backtrace_symbols_fmt(trace, len, fmt);
	if (s == NULL)
		return -1;
	for (size_t i = 0; i < len; i++)
		if (dprintf(fd, "%s\n", s[i]) < 0)
			break;
	free(s);
	return 0;
}

static const char fmt[] = "%a <%n%D> at %f";

char **
backtrace_symbols(void *const *trace, size_t len)
{
	return backtrace_symbols_fmt(trace, len, fmt);
}

int
backtrace_symbols_fd(void *const *trace, size_t len, int fd)
{
	return backtrace_symbols_fd_fmt(trace, len, fd, fmt);
}
