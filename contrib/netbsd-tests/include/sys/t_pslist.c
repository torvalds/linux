/*	$NetBSD: t_pslist.c,v 1.1 2016/04/09 04:39:47 riastradh Exp $	*/

/*-
 * Copyright (c) 2016 The NetBSD Foundation, Inc.
 * All rights reserved.
 *
 * This code is derived from software contributed to The NetBSD Foundation
 * by Taylor R. Campbell.
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

#include <sys/pslist.h>

#include <atf-c.h>

/*
 * XXX This is a limited test to make sure the operations behave as
 * described on a sequential machine.  It does nothing to test the
 * pserialize-safety of any operations.
 */

ATF_TC(misc);
ATF_TC_HEAD(misc, tc)
{
	atf_tc_set_md_var(tc, "descr", "pserialize-safe list tests");
}
ATF_TC_BODY(misc, tc)
{
	struct pslist_head h = PSLIST_INITIALIZER;
	struct element {
		unsigned i;
		struct pslist_entry entry;
	} elements[] = {
		{ .i = 0, .entry = PSLIST_ENTRY_INITIALIZER },
		{ .i = 1 },
		{ .i = 2 },
		{ .i = 3 },
		{ .i = 4 },
		{ .i = 5 },
		{ .i = 6 },
		{ .i = 7 },
	};
	struct element *element;
	unsigned i;

	/* Check PSLIST_INITIALIZER is destroyable.  */
	PSLIST_DESTROY(&h);
	PSLIST_INIT(&h);

	/* Check PSLIST_ENTRY_INITIALIZER is destroyable.  */
	PSLIST_ENTRY_DESTROY(&elements[0], entry);

	for (i = 0; i < __arraycount(elements); i++)
		PSLIST_ENTRY_INIT(&elements[i], entry);

	PSLIST_WRITER_INSERT_HEAD(&h, &elements[4], entry);
	PSLIST_WRITER_INSERT_BEFORE(&elements[4], &elements[2], entry);
	PSLIST_WRITER_INSERT_BEFORE(&elements[4], &elements[3], entry);
	PSLIST_WRITER_INSERT_BEFORE(&elements[2], &elements[1], entry);
	PSLIST_WRITER_INSERT_HEAD(&h, &elements[0], entry);
	PSLIST_WRITER_INSERT_AFTER(&elements[4], &elements[5], entry);
	PSLIST_WRITER_INSERT_AFTER(&elements[5], &elements[7], entry);
	PSLIST_WRITER_INSERT_AFTER(&elements[5], &elements[6], entry);

	PSLIST_WRITER_REMOVE(&elements[0], entry);
	ATF_CHECK(elements[0].entry.ple_next != NULL);
	PSLIST_ENTRY_DESTROY(&elements[0], entry);

	PSLIST_WRITER_REMOVE(&elements[4], entry);
	ATF_CHECK(elements[4].entry.ple_next != NULL);
	PSLIST_ENTRY_DESTROY(&elements[4], entry);

	PSLIST_ENTRY_INIT(&elements[0], entry);
	PSLIST_WRITER_INSERT_HEAD(&h, &elements[0], entry);

	PSLIST_ENTRY_INIT(&elements[4], entry);
	PSLIST_WRITER_INSERT_AFTER(&elements[3], &elements[4], entry);

	i = 0;
	PSLIST_WRITER_FOREACH(element, &h, struct element, entry) {
		ATF_CHECK_EQ(i, element->i);
		i++;
	}
	i = 0;
	PSLIST_READER_FOREACH(element, &h, struct element, entry) {
		ATF_CHECK_EQ(i, element->i);
		i++;
	}

	while ((element = PSLIST_WRITER_FIRST(&h, struct element, entry))
	    != NULL) {
		PSLIST_WRITER_REMOVE(element, entry);
		PSLIST_ENTRY_DESTROY(element, entry);
	}

	PSLIST_DESTROY(&h);
}

ATF_TP_ADD_TCS(tp)
{

	ATF_TP_ADD_TC(tp, misc);

	return atf_no_error();
}
