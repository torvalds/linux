/*-
 * Copyright (c) 2014-2017 Dag-Erling Smørgrav
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. The name of the author may not be used to endorse or promote
 *    products derived from this software without specific prior written
 *    permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * $OpenPAM: t_openpam_ctype.c 938 2017-04-30 21:34:42Z des $
 */

#ifdef HAVE_CONFIG_H
# include "config.h"
#endif

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include <cryb/test.h>

#include "openpam_ctype.h"

#define OC_DIGIT	"0123456789"
#define OC_XDIGIT	OC_DIGIT "ABCDEFabcdef"
#define OC_UPPER	"ABCDEFGHIJKLMNOPQRSTUVWXYZ"
#define OC_LOWER	"abcdefghijklmnopqrstuvwxyz"
#define OC_LETTER	OC_UPPER OC_LOWER
#define OC_LWS		" \t\f\r"
#define OC_WS		OC_LWS "\n"
#define OC_P		"!\"#$%&'()*+,-./" OC_DIGIT ":;<=>?@" OC_UPPER "[\\]^_`" OC_LOWER "{|}~"
#define OC_PFCS		OC_DIGIT OC_LETTER "._-"

static const char oc_digit[] = OC_DIGIT;
static const char oc_xdigit[] = OC_XDIGIT;
static const char oc_upper[] = OC_UPPER;
static const char oc_lower[] = OC_LOWER;
static const char oc_letter[] = OC_LETTER;
static const char oc_lws[] = OC_LWS;
static const char oc_ws[] = OC_WS;
static const char oc_p[] = OC_P;
static const char oc_pfcs[] = OC_PFCS;

#define T_OC(set)							\
	static int							\
	t_oc_##set(char **desc, void *arg)				\
	{								\
		char crib[256];						\
		unsigned int i, ret;					\
									\
		(void)desc;						\
		(void)arg;						\
		memset(crib, 0, sizeof crib);				\
		for (i = 0; oc_##set[i]; ++i)				\
			crib[(int)oc_##set[i]] = 1;			\
		for (i = ret = 0; i < sizeof crib; ++i) {		\
			if (is_##set(i) != crib[i]) {			\
				t_printv("is_%s() incorrect "		\
				    "for %#02x\n", #set, i);		\
				++ret;					\
			}						\
		}							\
		return (ret == 0);					\
	}
#define T_OC_ADD(set) t_add_test(&t_oc_##set, NULL, "is_"#set)

T_OC(digit)
T_OC(xdigit)
T_OC(upper)
T_OC(lower)
T_OC(letter)
T_OC(lws)
T_OC(ws)
T_OC(p)
T_OC(pfcs)


/***************************************************************************
 * Boilerplate
 */

static int
t_prepare(int argc, char *argv[])
{

	(void)argc;
	(void)argv;
	T_OC_ADD(digit);
	T_OC_ADD(xdigit);
	T_OC_ADD(upper);
	T_OC_ADD(lower);
	T_OC_ADD(letter);
	T_OC_ADD(lws);
	T_OC_ADD(ws);
	T_OC_ADD(p);
	T_OC_ADD(pfcs);
	return (0);
}

int
main(int argc, char *argv[])
{

	t_main(t_prepare, NULL, argc, argv);
}
