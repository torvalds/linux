/*
 * Copyright (c) 2002 Kungliga Tekniska Högskolan
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

#include <config.h>

#include <sys/types.h>
#ifdef HAVE_SYS_MMAN_H
#include <sys/mman.h>
#endif
#include <fcntl.h>

#include "roken.h"
#include "resolve.h"

struct dns_reply*
parse_reply(const unsigned char *, size_t);

enum { MAX_BUF = 36};

static struct testcase {
    unsigned char buf[MAX_BUF];
    size_t buf_len;
} tests[] = {
    {{0x12, 0x67, 0x84, 0x00, 0x00, 0x01, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00,
     0x03, 'f', 'o', 'o', 0x00,
     0x00, 0x10, 0x00, 0x01,
     0x03, 'f', 'o', 'o', 0x00,
     0x00, 0x10, 0x00, 0x01,
      0x00, 0x00, 0x12, 0x67, 0xff, 0xff}, 36}
};

#ifndef MAP_FAILED
#define MAP_FAILED (-1)
#endif

static sig_atomic_t val = 0;

static RETSIGTYPE
segv_handler(int sig)
{
    val = 1;
}

int
main(int argc, char **argv)
{
#ifndef HAVE_MMAP
    return 77;			/* signal to automake that this test
                                   cannot be run */
#else /* HAVE_MMAP */
    int ret;
    int i;
    struct sigaction sa;

    sigemptyset (&sa.sa_mask);
    sa.sa_flags = 0;
    sa.sa_handler = segv_handler;
    sigaction (SIGSEGV, &sa, NULL);

    for (i = 0; val == 0 && i < sizeof(tests)/sizeof(tests[0]); ++i) {
	const struct testcase *t = &tests[i];
	unsigned char *p1, *p2;
	int flags;
	int fd;
	size_t pagesize = getpagesize();
	unsigned char *buf;

#ifdef MAP_ANON
	flags = MAP_ANON;
	fd = -1;
#else
	flags = 0;
	fd = open ("/dev/zero", O_RDONLY);
	if(fd < 0)
	    err (1, "open /dev/zero");
#endif
	flags |= MAP_PRIVATE;

	p1 = (unsigned char *)mmap(0, 2 * pagesize, PROT_READ | PROT_WRITE,
		  flags, fd, 0);
	if (p1 == (unsigned char *)MAP_FAILED)
	    err (1, "mmap");
	p2 = p1 + pagesize;
	ret = mprotect ((void *)p2, pagesize, 0);
	if (ret < 0)
	    err (1, "mprotect");
	buf = p2 - t->buf_len;
	memcpy (buf, t->buf, t->buf_len);
	parse_reply (buf, t->buf_len);
	ret = munmap ((void *)p1, 2 * pagesize);
	if (ret < 0)
	    err (1, "munmap");
    }
    return val;
#endif /* HAVE_MMAP */
}
