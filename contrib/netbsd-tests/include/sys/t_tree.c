/* $NetBSD: t_tree.c,v 1.1 2011/05/05 13:36:05 jruoho Exp $ */

/*-
 * Copyright (c) 2010, 2011 The NetBSD Foundation, Inc.
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
#include <sys/tree.h>

#include <atf-c.h>
#include <stdlib.h>
#include <stdio.h>

struct mist {
	RB_ENTRY(mist) rbentry;
	int key;
};
RB_HEAD(head, mist) tt;

static int
mistcmp(struct mist *a, struct mist *b)
{
#if 0
	return (b->key - a->key); /* wrong, can overflow */
#else
	if (b->key > a->key)
		return 1;
	else if (b->key < a->key)
		return (-1);
	else
		return 0;
#endif
}

RB_PROTOTYPE(head, mist, rbentry, mistcmp)
RB_GENERATE(head, mist, rbentry, mistcmp)

static struct mist *
addmist(int key)
{
	struct mist *m;

	m = malloc(sizeof(struct mist));
	m->key = key;
	RB_INSERT(head, &tt, m);
	return m;
}

static int
findmist(struct mist *m)
{

	return (!!RB_FIND(head, &tt, m));
}

#define N 1000
static int
test(void)
{
	struct mist *m[N];
	int fail, i, j;

	RB_INIT(&tt);
	fail = 0;
	for (i = 0; i < N; i++) {
		m[i] = addmist(random() << 1); /* use all 32 bits */
		for (j = 0; j <= i; j++)
			if (!findmist(m[j]))
				fail++;
	}
	return fail;
}

ATF_TC(tree_rbstress);
ATF_TC_HEAD(tree_rbstress, tc)
{

	atf_tc_set_md_var(tc, "descr", "rb-tree stress test");
}

ATF_TC_BODY(tree_rbstress, tc)
{
	int i, fail, f;

	srandom(4711);
	fail = 0;
	for (i = 0; i < 10; i++) {
		f = test();
		if (f) {
			atf_tc_fail_nonfatal("loop %d: %d errors\n", i, f);
			fail += f;
		}
	}
}

ATF_TP_ADD_TCS(tp)
{

	ATF_TP_ADD_TC(tp, tree_rbstress);

	return atf_no_error();
}
