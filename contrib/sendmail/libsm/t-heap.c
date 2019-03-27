/*
 * Copyright (c) 2000-2001 Proofpoint, Inc. and its suppliers.
 *	All rights reserved.
 *
 * By using this file, you agree to the terms and conditions set
 * forth in the LICENSE file which can be found at the top level of
 * the sendmail distribution.
 */

#include <sm/gen.h>
SM_IDSTR(id, "@(#)$Id: t-heap.c,v 1.11 2013-11-22 20:51:43 ca Exp $")

#include <sm/debug.h>
#include <sm/heap.h>
#include <sm/io.h>
#include <sm/test.h>
#include <sm/xtrap.h>

#if SM_HEAP_CHECK
extern SM_DEBUG_T SmHeapCheck;
# define HEAP_CHECK sm_debug_active(&SmHeapCheck, 1)
#else /* SM_HEAP_CHECK */
# define HEAP_CHECK 0
#endif /* SM_HEAP_CHECK */

int
main(argc, argv)
	int argc;
	char **argv;
{
	void *p;

	sm_test_begin(argc, argv, "test heap handling");
	if (argc > 1)
		sm_debug_addsettings_x(argv[1]);

	p = sm_malloc(10);
	SM_TEST(p != NULL);
	p = sm_realloc_x(p, 20);
	SM_TEST(p != NULL);
	p = sm_realloc(p, 30);
	SM_TEST(p != NULL);
	if (HEAP_CHECK)
	{
		sm_dprintf("heap with 1 30-byte block allocated:\n");
		sm_heap_report(smioout, 3);
	}

	if (HEAP_CHECK)
	{
		sm_free(p);
		sm_dprintf("heap with 0 blocks allocated:\n");
		sm_heap_report(smioout, 3);
		sm_dprintf("xtrap count = %d\n", SmXtrapCount);
	}

#if DEBUG
	/* this will cause a core dump */
	sm_dprintf("about to free %p for the second time\n", p);
	sm_free(p);
#endif /* DEBUG */

	return sm_test_end();
}
