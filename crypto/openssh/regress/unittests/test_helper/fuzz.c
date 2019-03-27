/*	$OpenBSD: fuzz.c,v 1.8 2015/03/03 20:42:49 djm Exp $	*/
/*
 * Copyright (c) 2011 Damien Miller <djm@mindrot.org>
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

/* Utility functions/framework for fuzz tests */

#include "includes.h"

#include <sys/types.h>
#include <sys/uio.h>

#include <assert.h>
#include <ctype.h>
#include <stdio.h>
#ifdef HAVE_STDINT_H
# include <stdint.h>
#endif
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <unistd.h>

#include "test_helper.h"
#include "atomicio.h"

/* #define FUZZ_DEBUG */

#ifdef FUZZ_DEBUG
# define FUZZ_DBG(x) do { \
		printf("%s:%d %s: ", __FILE__, __LINE__, __func__); \
		printf x; \
		printf("\n"); \
		fflush(stdout); \
	} while (0)
#else
# define FUZZ_DBG(x)
#endif

/* For brevity later */
typedef unsigned long long fuzz_ullong;

/* For base-64 fuzzing */
static const char fuzz_b64chars[] =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

struct fuzz {
	/* Fuzz method currently in use */
	int strategy;

	/* Fuzz methods remaining */
	int strategies;

	/* Original seed data blob */
	void *seed;
	size_t slen;

	/* Current working copy of seed with fuzz mutations applied */
	u_char *fuzzed;

	/* Used by fuzz methods */
	size_t o1, o2;
};

static const char *
fuzz_ntop(u_int n)
{
	switch (n) {
	case 0:
		return "NONE";
	case FUZZ_1_BIT_FLIP:
		return "FUZZ_1_BIT_FLIP";
	case FUZZ_2_BIT_FLIP:
		return "FUZZ_2_BIT_FLIP";
	case FUZZ_1_BYTE_FLIP:
		return "FUZZ_1_BYTE_FLIP";
	case FUZZ_2_BYTE_FLIP:
		return "FUZZ_2_BYTE_FLIP";
	case FUZZ_TRUNCATE_START:
		return "FUZZ_TRUNCATE_START";
	case FUZZ_TRUNCATE_END:
		return "FUZZ_TRUNCATE_END";
	case FUZZ_BASE64:
		return "FUZZ_BASE64";
	default:
		abort();
	}
}

static int
fuzz_fmt(struct fuzz *fuzz, char *s, size_t n)
{
	if (fuzz == NULL)
		return -1;

	switch (fuzz->strategy) {
	case FUZZ_1_BIT_FLIP:
		snprintf(s, n, "%s case %zu of %zu (bit: %zu)\n",
		    fuzz_ntop(fuzz->strategy),
		    fuzz->o1, fuzz->slen * 8, fuzz->o1);
		return 0;
	case FUZZ_2_BIT_FLIP:
		snprintf(s, n, "%s case %llu of %llu (bits: %zu, %zu)\n",
		    fuzz_ntop(fuzz->strategy),
		    (((fuzz_ullong)fuzz->o2) * fuzz->slen * 8) + fuzz->o1,
		    ((fuzz_ullong)fuzz->slen * 8) * fuzz->slen * 8,
		    fuzz->o1, fuzz->o2);
		return 0;
	case FUZZ_1_BYTE_FLIP:
		snprintf(s, n, "%s case %zu of %zu (byte: %zu)\n",
		    fuzz_ntop(fuzz->strategy),
		    fuzz->o1, fuzz->slen, fuzz->o1);
		return 0;
	case FUZZ_2_BYTE_FLIP:
		snprintf(s, n, "%s case %llu of %llu (bytes: %zu, %zu)\n",
		    fuzz_ntop(fuzz->strategy),
		    (((fuzz_ullong)fuzz->o2) * fuzz->slen) + fuzz->o1,
		    ((fuzz_ullong)fuzz->slen) * fuzz->slen,
		    fuzz->o1, fuzz->o2);
		return 0;
	case FUZZ_TRUNCATE_START:
		snprintf(s, n, "%s case %zu of %zu (offset: %zu)\n",
		    fuzz_ntop(fuzz->strategy),
		    fuzz->o1, fuzz->slen, fuzz->o1);
		return 0;
	case FUZZ_TRUNCATE_END:
		snprintf(s, n, "%s case %zu of %zu (offset: %zu)\n",
		    fuzz_ntop(fuzz->strategy),
		    fuzz->o1, fuzz->slen, fuzz->o1);
		return 0;
	case FUZZ_BASE64:
		assert(fuzz->o2 < sizeof(fuzz_b64chars) - 1);
		snprintf(s, n, "%s case %llu of %llu (offset: %zu char: %c)\n",
		    fuzz_ntop(fuzz->strategy),
		    (fuzz->o1 * (fuzz_ullong)64) + fuzz->o2,
		    fuzz->slen * (fuzz_ullong)64, fuzz->o1,
		    fuzz_b64chars[fuzz->o2]);
		return 0;
	default:
		return -1;
		abort();
	}
}

static void
dump(u_char *p, size_t len)
{
	size_t i, j;

	for (i = 0; i < len; i += 16) {
		fprintf(stderr, "%.4zd: ", i);
		for (j = i; j < i + 16; j++) {
			if (j < len)
				fprintf(stderr, "%02x ", p[j]);
			else
				fprintf(stderr, "   ");
		}
		fprintf(stderr, " ");
		for (j = i; j < i + 16; j++) {
			if (j < len) {
				if  (isascii(p[j]) && isprint(p[j]))
					fprintf(stderr, "%c", p[j]);
				else
					fprintf(stderr, ".");
			}
		}
		fprintf(stderr, "\n");
	}
}

void
fuzz_dump(struct fuzz *fuzz)
{
	char buf[256];

	if (fuzz_fmt(fuzz, buf, sizeof(buf)) != 0) {
		fprintf(stderr, "%s: fuzz invalid\n", __func__);
		abort();
	}
	fputs(buf, stderr);
	fprintf(stderr, "fuzz original %p len = %zu\n", fuzz->seed, fuzz->slen);
	dump(fuzz->seed, fuzz->slen);
	fprintf(stderr, "fuzz context %p len = %zu\n", fuzz, fuzz_len(fuzz));
	dump(fuzz_ptr(fuzz), fuzz_len(fuzz));
}

#ifdef SIGINFO
static struct fuzz *last_fuzz;

static void
siginfo(int unused __attribute__((__unused__)))
{
	char buf[256];

	test_info(buf, sizeof(buf));
	atomicio(vwrite, STDERR_FILENO, buf, strlen(buf));
	if (last_fuzz != NULL) {
		fuzz_fmt(last_fuzz, buf, sizeof(buf));
		atomicio(vwrite, STDERR_FILENO, buf, strlen(buf));
	}
}
#endif

struct fuzz *
fuzz_begin(u_int strategies, const void *p, size_t l)
{
	struct fuzz *ret = calloc(sizeof(*ret), 1);

	assert(p != NULL);
	assert(ret != NULL);
	ret->seed = malloc(l);
	assert(ret->seed != NULL);
	memcpy(ret->seed, p, l);
	ret->slen = l;
	ret->strategies = strategies;

	assert(ret->slen < SIZE_MAX / 8);
	assert(ret->strategies <= (FUZZ_MAX|(FUZZ_MAX-1)));

	FUZZ_DBG(("begin, ret = %p", ret));

	fuzz_next(ret);

#ifdef SIGINFO
	last_fuzz = ret;
	signal(SIGINFO, siginfo);
#endif

	return ret;
}

void
fuzz_cleanup(struct fuzz *fuzz)
{
	FUZZ_DBG(("cleanup, fuzz = %p", fuzz));
#ifdef SIGINFO
	last_fuzz = NULL;
	signal(SIGINFO, SIG_DFL);
#endif
	assert(fuzz != NULL);
	assert(fuzz->seed != NULL);
	assert(fuzz->fuzzed != NULL);
	free(fuzz->seed);
	free(fuzz->fuzzed);
	free(fuzz);
}

static int
fuzz_strategy_done(struct fuzz *fuzz)
{
	FUZZ_DBG(("fuzz = %p, strategy = %s, o1 = %zu, o2 = %zu, slen = %zu",
	    fuzz, fuzz_ntop(fuzz->strategy), fuzz->o1, fuzz->o2, fuzz->slen));

	switch (fuzz->strategy) {
	case FUZZ_1_BIT_FLIP:
		return fuzz->o1 >= fuzz->slen * 8;
	case FUZZ_2_BIT_FLIP:
		return fuzz->o2 >= fuzz->slen * 8;
	case FUZZ_2_BYTE_FLIP:
		return fuzz->o2 >= fuzz->slen;
	case FUZZ_1_BYTE_FLIP:
	case FUZZ_TRUNCATE_START:
	case FUZZ_TRUNCATE_END:
	case FUZZ_BASE64:
		return fuzz->o1 >= fuzz->slen;
	default:
		abort();
	}
}

void
fuzz_next(struct fuzz *fuzz)
{
	u_int i;

	FUZZ_DBG(("start, fuzz = %p, strategy = %s, strategies = 0x%lx, "
	    "o1 = %zu, o2 = %zu, slen = %zu", fuzz, fuzz_ntop(fuzz->strategy),
	    (u_long)fuzz->strategies, fuzz->o1, fuzz->o2, fuzz->slen));

	if (fuzz->strategy == 0 || fuzz_strategy_done(fuzz)) {
		/* If we are just starting out, we need to allocate too */
		if (fuzz->fuzzed == NULL) {
			FUZZ_DBG(("alloc"));
			fuzz->fuzzed = calloc(fuzz->slen, 1);
		}
		/* Pick next strategy */
		FUZZ_DBG(("advance"));
		for (i = 1; i <= FUZZ_MAX; i <<= 1) {
			if ((fuzz->strategies & i) != 0) {
				fuzz->strategy = i;
				break;
			}
		}
		FUZZ_DBG(("selected = %u", fuzz->strategy));
		if (fuzz->strategy == 0) {
			FUZZ_DBG(("done, no more strategies"));
			return;
		}
		fuzz->strategies &= ~(fuzz->strategy);
		fuzz->o1 = fuzz->o2 = 0;
	}

	assert(fuzz->fuzzed != NULL);

	switch (fuzz->strategy) {
	case FUZZ_1_BIT_FLIP:
		assert(fuzz->o1 / 8 < fuzz->slen);
		memcpy(fuzz->fuzzed, fuzz->seed, fuzz->slen);
		fuzz->fuzzed[fuzz->o1 / 8] ^= 1 << (fuzz->o1 % 8);
		fuzz->o1++;
		break;
	case FUZZ_2_BIT_FLIP:
		assert(fuzz->o1 / 8 < fuzz->slen);
		assert(fuzz->o2 / 8 < fuzz->slen);
		memcpy(fuzz->fuzzed, fuzz->seed, fuzz->slen);
		fuzz->fuzzed[fuzz->o1 / 8] ^= 1 << (fuzz->o1 % 8);
		fuzz->fuzzed[fuzz->o2 / 8] ^= 1 << (fuzz->o2 % 8);
		fuzz->o1++;
		if (fuzz->o1 >= fuzz->slen * 8) {
			fuzz->o1 = 0;
			fuzz->o2++;
		}
		break;
	case FUZZ_1_BYTE_FLIP:
		assert(fuzz->o1 < fuzz->slen);
		memcpy(fuzz->fuzzed, fuzz->seed, fuzz->slen);
		fuzz->fuzzed[fuzz->o1] ^= 0xff;
		fuzz->o1++;
		break;
	case FUZZ_2_BYTE_FLIP:
		assert(fuzz->o1 < fuzz->slen);
		assert(fuzz->o2 < fuzz->slen);
		memcpy(fuzz->fuzzed, fuzz->seed, fuzz->slen);
		fuzz->fuzzed[fuzz->o1] ^= 0xff;
		fuzz->fuzzed[fuzz->o2] ^= 0xff;
		fuzz->o1++;
		if (fuzz->o1 >= fuzz->slen) {
			fuzz->o1 = 0;
			fuzz->o2++;
		}
		break;
	case FUZZ_TRUNCATE_START:
	case FUZZ_TRUNCATE_END:
		assert(fuzz->o1 < fuzz->slen);
		memcpy(fuzz->fuzzed, fuzz->seed, fuzz->slen);
		fuzz->o1++;
		break;
	case FUZZ_BASE64:
		assert(fuzz->o1 < fuzz->slen);
		assert(fuzz->o2 < sizeof(fuzz_b64chars) - 1);
		memcpy(fuzz->fuzzed, fuzz->seed, fuzz->slen);
		fuzz->fuzzed[fuzz->o1] = fuzz_b64chars[fuzz->o2];
		fuzz->o2++;
		if (fuzz->o2 >= sizeof(fuzz_b64chars) - 1) {
			fuzz->o2 = 0;
			fuzz->o1++;
		}
		break;
	default:
		abort();
	}

	FUZZ_DBG(("done, fuzz = %p, strategy = %s, strategies = 0x%lx, "
	    "o1 = %zu, o2 = %zu, slen = %zu", fuzz, fuzz_ntop(fuzz->strategy),
	    (u_long)fuzz->strategies, fuzz->o1, fuzz->o2, fuzz->slen));
}

int
fuzz_matches_original(struct fuzz *fuzz)
{
	if (fuzz_len(fuzz) != fuzz->slen)
		return 0;
	return memcmp(fuzz_ptr(fuzz), fuzz->seed, fuzz->slen) == 0;
}

int
fuzz_done(struct fuzz *fuzz)
{
	FUZZ_DBG(("fuzz = %p, strategies = 0x%lx", fuzz,
	    (u_long)fuzz->strategies));

	return fuzz_strategy_done(fuzz) && fuzz->strategies == 0;
}

size_t
fuzz_len(struct fuzz *fuzz)
{
	assert(fuzz->fuzzed != NULL);
	switch (fuzz->strategy) {
	case FUZZ_1_BIT_FLIP:
	case FUZZ_2_BIT_FLIP:
	case FUZZ_1_BYTE_FLIP:
	case FUZZ_2_BYTE_FLIP:
	case FUZZ_BASE64:
		return fuzz->slen;
	case FUZZ_TRUNCATE_START:
	case FUZZ_TRUNCATE_END:
		assert(fuzz->o1 <= fuzz->slen);
		return fuzz->slen - fuzz->o1;
	default:
		abort();
	}
}

u_char *
fuzz_ptr(struct fuzz *fuzz)
{
	assert(fuzz->fuzzed != NULL);
	switch (fuzz->strategy) {
	case FUZZ_1_BIT_FLIP:
	case FUZZ_2_BIT_FLIP:
	case FUZZ_1_BYTE_FLIP:
	case FUZZ_2_BYTE_FLIP:
	case FUZZ_BASE64:
		return fuzz->fuzzed;
	case FUZZ_TRUNCATE_START:
		assert(fuzz->o1 <= fuzz->slen);
		return fuzz->fuzzed + fuzz->o1;
	case FUZZ_TRUNCATE_END:
		assert(fuzz->o1 <= fuzz->slen);
		return fuzz->fuzzed;
	default:
		abort();
	}
}

