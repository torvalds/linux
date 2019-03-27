/*
 * Copyright (c) 1999 - 2006 Kungliga Tekniska Högskolan
 * (Royal Institute of Technology, Stockholm, Sweden).
 * All rights reserved.
 *
 * Portions Copyright (c) 2009 Apple Inc. All rights reserved.
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
#endif
#ifdef HAVE_SYS_MMAN_H
#include <sys/mman.h>
#endif
#include <stdio.h>
#include <string.h>
#include <err.h>
#include <roken.h>

#include "asn1-common.h"
#include "check-common.h"

RCSID("$Id$");

struct map_page {
    void *start;
    size_t size;
    void *data_start;
    size_t data_size;
    enum map_type type;
};

/* #undef HAVE_MMAP */

void *
map_alloc(enum map_type type, const void *buf,
	  size_t size, struct map_page **map)
{
#ifndef HAVE_MMAP
    unsigned char *p;
    size_t len = size + sizeof(long) * 2;
    int i;

    *map = ecalloc(1, sizeof(**map));

    p = emalloc(len);
    (*map)->type = type;
    (*map)->start = p;
    (*map)->size = len;
    (*map)->data_start = p + sizeof(long);
    for (i = sizeof(long); i > 0; i--)
	p[sizeof(long) - i] = 0xff - i;
    for (i = sizeof(long); i > 0; i--)
	p[len - i] = 0xff - i;
#else
    unsigned char *p;
    int flags, ret, fd;
    size_t pagesize = getpagesize();

    *map = ecalloc(1, sizeof(**map));

    (*map)->type = type;

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

    (*map)->size = size + pagesize - (size % pagesize) + pagesize * 2;

    p = (unsigned char *)mmap(0, (*map)->size, PROT_READ | PROT_WRITE,
			      flags, fd, 0);
    if (p == (unsigned char *)MAP_FAILED)
	err (1, "mmap");

    (*map)->start = p;

    ret = mprotect (p, pagesize, 0);
    if (ret < 0)
	err (1, "mprotect");

    ret = mprotect (p + (*map)->size - pagesize, pagesize, 0);
    if (ret < 0)
	err (1, "mprotect");

    switch (type) {
    case OVERRUN:
	(*map)->data_start = p + (*map)->size - pagesize - size;
	break;
    case UNDERRUN:
	(*map)->data_start = p + pagesize;
	break;
   default:
	abort();
    }
#endif
    (*map)->data_size = size;
    if (buf)
	memcpy((*map)->data_start, buf, size);
    return (*map)->data_start;
}

void
map_free(struct map_page *map, const char *test_name, const char *map_name)
{
#ifndef HAVE_MMAP
    unsigned char *p = map->start;
    int i;

    for (i = sizeof(long); i > 0; i--)
	if (p[sizeof(long) - i] != 0xff - i)
	    errx(1, "%s: %s underrun %d\n", test_name, map_name, i);
    for (i = sizeof(long); i > 0; i--)
	if (p[map->size - i] != 0xff - i)
	    errx(1, "%s: %s overrun %lu\n", test_name, map_name,
		 (unsigned long)map->size - i);
    free(map->start);
#else
    int ret;

    ret = munmap (map->start, map->size);
    if (ret < 0)
	err (1, "munmap");
#endif
    free(map);
}

static void
print_bytes (unsigned const char *buf, size_t len)
{
    int i;

    for (i = 0; i < len; ++i)
	printf ("%02x ", buf[i]);
}

#ifndef MAP_FAILED
#define MAP_FAILED (-1)
#endif

static char *current_test = "<uninit>";
static char *current_state = "<uninit>";

static RETSIGTYPE
segv_handler(int sig)
{
    int fd;
    char msg[] = "SIGSEGV i current test: ";

    fd = open("/dev/stdout", O_WRONLY, 0600);
    if (fd >= 0) {
	write(fd, msg, sizeof(msg));
	write(fd, current_test, strlen(current_test));
	write(fd, " ", 1);
	write(fd, current_state, strlen(current_state));
	write(fd, "\n", 1);
	close(fd);
    }
    _exit(1);
}

int
generic_test (const struct test_case *tests,
	      unsigned ntests,
	      size_t data_size,
	      int (ASN1CALL *encode)(unsigned char *, size_t, void *, size_t *),
	      int (ASN1CALL *length)(void *),
	      int (ASN1CALL *decode)(unsigned char *, size_t, void *, size_t *),
	      int (ASN1CALL *free_data)(void *),
	      int (*cmp)(void *a, void *b),
	      int (ASN1CALL *copy)(const void *from, void *to))
{
    unsigned char *buf, *buf2;
    int i;
    int failures = 0;
    void *data;
    struct map_page *data_map, *buf_map, *buf2_map;

#ifdef HAVE_SIGACTION
    struct sigaction sa, osa;
#endif

    for (i = 0; i < ntests; ++i) {
	int ret;
	size_t sz, consumed_sz, length_sz, buf_sz;
	void *to = NULL;

	current_test = tests[i].name;

	current_state = "init";

#ifdef HAVE_SIGACTION
	sigemptyset (&sa.sa_mask);
	sa.sa_flags = 0;
#ifdef SA_RESETHAND
	sa.sa_flags |= SA_RESETHAND;
#endif
	sa.sa_handler = segv_handler;
	sigaction (SIGSEGV, &sa, &osa);
#endif

	data = map_alloc(OVERRUN, NULL, data_size, &data_map);

	buf_sz = tests[i].byte_len;
	buf = map_alloc(UNDERRUN, NULL, buf_sz, &buf_map);

	current_state = "encode";
	ret = (*encode) (buf + buf_sz - 1, buf_sz,
			 tests[i].val, &sz);
	if (ret != 0) {
	    printf ("encoding of %s failed %d\n", tests[i].name, ret);
	    ++failures;
	    continue;
	}
	if (sz != tests[i].byte_len) {
 	    printf ("encoding of %s has wrong len (%lu != %lu)\n",
		    tests[i].name,
		    (unsigned long)sz, (unsigned long)tests[i].byte_len);
	    ++failures;
	    continue;
	}

	current_state = "length";
	length_sz = (*length) (tests[i].val);
	if (sz != length_sz) {
	    printf ("length for %s is bad (%lu != %lu)\n",
		    tests[i].name, (unsigned long)length_sz, (unsigned long)sz);
	    ++failures;
	    continue;
	}

	current_state = "memcmp";
	if (memcmp (buf, tests[i].bytes, tests[i].byte_len) != 0) {
	    printf ("encoding of %s has bad bytes:\n"
		    "correct: ", tests[i].name);
	    print_bytes ((unsigned char *)tests[i].bytes, tests[i].byte_len);
	    printf ("\nactual:  ");
	    print_bytes (buf, sz);
	    printf ("\n");
#if 0
	    rk_dumpdata("correct", tests[i].bytes, tests[i].byte_len);
	    rk_dumpdata("actual", buf, sz);
	    exit (1);
#endif
	    ++failures;
	    continue;
	}

	buf2 = map_alloc(OVERRUN, buf, sz, &buf2_map);

	current_state = "decode";
	ret = (*decode) (buf2, sz, data, &consumed_sz);
	if (ret != 0) {
	    printf ("decoding of %s failed %d\n", tests[i].name, ret);
	    ++failures;
	    continue;
	}
	if (sz != consumed_sz) {
	    printf ("different length decoding %s (%ld != %ld)\n",
		    tests[i].name,
		    (unsigned long)sz, (unsigned long)consumed_sz);
	    ++failures;
	    continue;
	}
	current_state = "cmp";
	if ((*cmp)(data, tests[i].val) != 0) {
	    printf ("%s: comparison failed\n", tests[i].name);
	    ++failures;
	    continue;
	}

	current_state = "copy";
	if (copy) {
	    to = emalloc(data_size);
	    ret = (*copy)(data, to);
	    if (ret != 0) {
		printf ("copy of %s failed %d\n", tests[i].name, ret);
		++failures;
		continue;
	    }

	    current_state = "cmp-copy";
	    if ((*cmp)(data, to) != 0) {
		printf ("%s: copy comparison failed\n", tests[i].name);
		++failures;
		continue;
	    }
	}

	current_state = "free";
	if (free_data) {
	    (*free_data)(data);
	    if (to) {
		(*free_data)(to);
		free(to);
	    }
	}

	current_state = "free";
	map_free(buf_map, tests[i].name, "encode");
	map_free(buf2_map, tests[i].name, "decode");
	map_free(data_map, tests[i].name, "data");

#ifdef HAVE_SIGACTION
	sigaction (SIGSEGV, &osa, NULL);
#endif
    }
    current_state = "done";
    return failures;
}

/*
 * check for failures
 *
 * a test size (byte_len) of -1 means that the test tries to trigger a
 * integer overflow (and later a malloc of to little memory), just
 * allocate some memory and hope that is enough for that test.
 */

int
generic_decode_fail (const struct test_case *tests,
		     unsigned ntests,
		     size_t data_size,
		     int (ASN1CALL *decode)(unsigned char *, size_t, void *, size_t *))
{
    unsigned char *buf;
    int i;
    int failures = 0;
    void *data;
    struct map_page *data_map, *buf_map;

#ifdef HAVE_SIGACTION
    struct sigaction sa, osa;
#endif

    for (i = 0; i < ntests; ++i) {
	int ret;
	size_t sz;
	const void *bytes;

	current_test = tests[i].name;

	current_state = "init";

#ifdef HAVE_SIGACTION
	sigemptyset (&sa.sa_mask);
	sa.sa_flags = 0;
#ifdef SA_RESETHAND
	sa.sa_flags |= SA_RESETHAND;
#endif
	sa.sa_handler = segv_handler;
	sigaction (SIGSEGV, &sa, &osa);
#endif

	data = map_alloc(OVERRUN, NULL, data_size, &data_map);

	if (tests[i].byte_len < 0xffffff && tests[i].byte_len >= 0) {
	    sz = tests[i].byte_len;
	    bytes = tests[i].bytes;
	} else {
	    sz = 4096;
	    bytes = NULL;
	}

	buf = map_alloc(OVERRUN, bytes, sz, &buf_map);

	if (tests[i].byte_len == -1)
	    memset(buf, 0, sz);

	current_state = "decode";
	ret = (*decode) (buf, tests[i].byte_len, data, &sz);
	if (ret == 0) {
	    printf ("sucessfully decoded %s\n", tests[i].name);
	    ++failures;
	    continue;
	}

	current_state = "free";
	if (buf)
	    map_free(buf_map, tests[i].name, "encode");
	map_free(data_map, tests[i].name, "data");

#ifdef HAVE_SIGACTION
	sigaction (SIGSEGV, &osa, NULL);
#endif
    }
    current_state = "done";
    return failures;
}
