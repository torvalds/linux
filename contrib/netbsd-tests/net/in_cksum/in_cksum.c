/*	$NetBSD: in_cksum.c,v 1.5 2015/10/18 18:27:25 christos Exp $	*/
/*-
 * Copyright (c) 2008 Joerg Sonnenberger <joerg@NetBSD.org>.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in
 *    the documentation and/or other materials provided with the
 *    distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
 * FOR A PARTICULAR PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE
 * COPYRIGHT HOLDERS OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY OR CONSEQUENTIAL DAMAGES (INCLUDING,
 * BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED
 * AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
 * OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT
 * OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#include <sys/cdefs.h>
__KERNEL_RCSID(0, "$NetBSD: in_cksum.c,v 1.5 2015/10/18 18:27:25 christos Exp $");

#include <sys/param.h>
#include <sys/mbuf.h>
#include <sys/resource.h>
#include <err.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdarg.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>

#define cpu_in_cksum portable_cpu_in_cksum 
#include "cpu_in_cksum.c"

#ifdef HAVE_CPU_IN_CKSUM
#undef cpu_in_cksum
int	cpu_in_cksum(struct mbuf*, int, int, uint32_t);
#endif

static bool	random_aligned;

void panic(const char *, ...) __printflike(1, 2);
void
panic(const char *fmt, ...)
{
	va_list ap;
	va_start(ap, fmt);
	verrx(1, fmt, ap);
	va_end(ap);
}

static void
free_mbuf_chain(struct mbuf *m)
{
	struct mbuf *next;

	if (m == NULL)
		return;

	next = m->m_next;
	free(m);
	free_mbuf_chain(next);
}

static struct mbuf *
allocate_mbuf_chain(char **lens)
{
	int len, off;
	struct mbuf *m;

	if (*lens == NULL)
		return NULL;

	len = atoi(*lens);
	off = random_aligned ? rand() % 64 : 0;

	m = malloc(sizeof(struct m_hdr) + len + off);
	if (m == NULL)
		err(EXIT_FAILURE, "malloc failed");

	m->m_data = (char *)m + sizeof(struct m_hdr) + off;
	m->m_len = len;

	m->m_next = allocate_mbuf_chain(lens + 1);

	return m;
}

#ifdef MBUFDUMP
static void
dump_mbuf(const struct mbuf *m, int len, int off)
{
	int x = 0;
	if (len <= 0)
		return;

	printf("Starting len=%d off=%d:\n", len, off);
	if (off > 0) {
		for (; m; m = m->m_next)
			if (off > m->m_len)
				off -= m->m_len;
			else
				break;
		if (m == NULL || off > m->m_len)
			errx(1, "out of data");
	}

	unsigned char *ptr = mtod(m, unsigned char *) + off;
	unsigned char *eptr = ptr + m->m_len;
	printf("[");
	for (;;) {
		if (ptr == eptr) {
			m = m->m_next;
			if (m == NULL)
				errx(1, "out of data");
			ptr = mtod(m, unsigned char *);
			eptr = ptr + m->m_len;
			printf("]\n[");
			x = 0;
		}
		printf("%.2x ", *ptr++);
		if (++x % 16 == 0)
			printf("\n");
		if (--len == 0)
			break;
	}
	printf("]\n");
	fflush(stdout);
}
#endif

static void
randomise_mbuf_chain(struct mbuf *m)
{
	int i, data, len;

	for (i = 0; i < m->m_len; i += sizeof(int)) {
		data = rand();
		if (i + sizeof(int) < (size_t)m->m_len)
			len = sizeof(int);
		else
			len = m->m_len - i;
		memcpy(m->m_data + i, &data, len);
	}
	if (m->m_next)
		randomise_mbuf_chain(m->m_next);
}

static int
mbuf_len(struct mbuf *m)
{
	return m == NULL ? 0 : m->m_len + mbuf_len(m->m_next);
}

int	in_cksum_portable(struct mbuf *, int);
int	in_cksum(struct mbuf *, int);

int
main(int argc, char **argv)
{
	struct rusage res;
	struct timeval tv, old_tv;
	int loops, old_sum, off, len;
#ifdef HAVE_CPU_IN_CKSUM
	int new_sum;
#endif
	long i, iterations;
	uint32_t init_sum;
	struct mbuf *m;
	bool verbose;
	int c;

	loops = 16;
	verbose = false;
	random_aligned = 0;
	iterations = 100000;

	while ((c = getopt(argc, argv, "i:l:u:v")) != -1) {
		switch (c) {
		case 'i':
			iterations = atoi(optarg);
			break;
		case 'l':
			loops = atoi(optarg);
			break;
		case 'u':
			random_aligned = atoi(optarg);
			break;
		case 'v':
			verbose = true;
			break;
		default:
			errx(1, "%s [-l <loops>] [-u <unalign> [-i <iterations> "
			    "[<mbuf-size> ...]", getprogname());
		}
	}

	for (; loops; --loops) {
		if ((m = allocate_mbuf_chain(argv + 4)) == NULL)
			continue;
		randomise_mbuf_chain(m);
		init_sum = rand();
		len = mbuf_len(m);

		/* force one loop over all data */
		if (loops == 1)
			off = 0;
		else
			off = len ? rand() % len : 0;

		len -= off;
		old_sum = portable_cpu_in_cksum(m, len, off, init_sum);
#ifdef HAVE_CPU_IN_CKSUM
#ifdef MBUFDUMP
		printf("m->m_len=%d len=%d off=%d\n", m->m_len, len, off);
		dump_mbuf(m, len, off);
#endif
		new_sum = cpu_in_cksum(m, len, off, init_sum);
		if (old_sum != new_sum)
			errx(1, "comparison failed: %x %x", old_sum, new_sum);
#else
		__USE(old_sum);
#endif

		if (iterations == 0)
			continue;

		getrusage(RUSAGE_SELF, &res);
		tv = res.ru_utime;
		for (i = iterations; i; --i)
			(void)portable_cpu_in_cksum(m, len, off, init_sum);
		getrusage(RUSAGE_SELF, &res);
		timersub(&res.ru_utime, &tv, &old_tv);
		if (verbose)
			printf("portable version: %jd.%06jd\n",
			    (intmax_t)old_tv.tv_sec, (intmax_t)old_tv.tv_usec);

#ifdef HAVE_CPU_IN_CKSUM
		getrusage(RUSAGE_SELF, &res);
		tv = res.ru_utime;
		for (i = iterations; i; --i)
			(void)cpu_in_cksum(m, len, off, init_sum);
		getrusage(RUSAGE_SELF, &res);
		timersub(&res.ru_utime, &tv, &tv);
		if (verbose) {
			printf("test version:     %jd.%06jd\n",
			    (intmax_t)tv.tv_sec, (intmax_t)tv.tv_usec);
			printf("relative time:    %3.g%%\n",
			    100 * ((double)tv.tv_sec * 1e6 + tv.tv_usec) /
			    ((double)old_tv.tv_sec * 1e6 + old_tv.tv_usec + 1));
		}
#endif
		free_mbuf_chain(m);
	}

	return 0;
}
