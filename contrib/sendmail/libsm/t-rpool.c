/*
 * Copyright (c) 2000-2001 Proofpoint, Inc. and its suppliers.
 *	All rights reserved.
 *
 * By using this file, you agree to the terms and conditions set
 * forth in the LICENSE file which can be found at the top level of
 * the sendmail distribution.
 */

#include <sm/gen.h>
SM_IDSTR(id, "@(#)$Id: t-rpool.c,v 1.19 2013-11-22 20:51:43 ca Exp $")

#include <sm/debug.h>
#include <sm/heap.h>
#include <sm/rpool.h>
#include <sm/io.h>
#include <sm/test.h>

static void
rfree __P((
	void *cx));

static void
rfree(cx)
	void *cx;
{
	(void) sm_io_fprintf(smioout, SM_TIME_DEFAULT, "rfree freeing `%s'\n",
			     (char *) cx);
}

int
main(argc, argv)
	int argc;
	char *argv[];
{
	SM_RPOOL_T *rpool;
	char *a[26];
	int i, j;
	SM_RPOOL_ATTACH_T att;

	sm_test_begin(argc, argv, "test rpool");
	sm_debug_addsetting_x("sm_check_heap", 1);
	rpool = sm_rpool_new_x(NULL);
	SM_TEST(rpool != NULL);
	att = sm_rpool_attach_x(rpool, rfree, "attachment #1");
	SM_TEST(att != NULL);
	for (i = 0; i < 26; ++i)
	{
		size_t sz = i * i * i;

		a[i] = sm_rpool_malloc_x(rpool, sz);
		for (j = 0; j < sz; ++j)
			a[i][j] = 'a' + i;
	}
	att = sm_rpool_attach_x(rpool, rfree, "attachment #2");
	(void) sm_rpool_attach_x(rpool, rfree, "attachment #3");
	sm_rpool_detach(att);

	/* XXX more tests? */
#if DEBUG
	sm_dprintf("heap after filling up rpool:\n");
	sm_heap_report(smioout, 3);
	sm_dprintf("freeing rpool:\n");
	sm_rpool_free(rpool);
	sm_dprintf("heap after freeing rpool:\n");
	sm_heap_report(smioout, 3);
#endif /* DEBUG */
	return sm_test_end();
}
