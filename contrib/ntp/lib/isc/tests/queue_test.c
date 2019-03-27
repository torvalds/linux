/*
 * Copyright (C) 2011, 2012  Internet Systems Consortium, Inc. ("ISC")
 *
 * Permission to use, copy, modify, and/or distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND ISC DISCLAIMS ALL WARRANTIES WITH
 * REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF MERCHANTABILITY
 * AND FITNESS.  IN NO EVENT SHALL ISC BE LIABLE FOR ANY SPECIAL, DIRECT,
 * INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES WHATSOEVER RESULTING FROM
 * LOSS OF USE, DATA OR PROFITS, WHETHER IN AN ACTION OF CONTRACT, NEGLIGENCE
 * OR OTHER TORTIOUS ACTION, ARISING OUT OF OR IN CONNECTION WITH THE USE OR
 * PERFORMANCE OF THIS SOFTWARE.
 */

/* $Id$ */

/*! \file */

#include <config.h>

#include <atf-c.h>

#include <unistd.h>
#include <time.h>

#include <isc/queue.h>

#include "isctest.h"

typedef struct item {
	int 			value;
	ISC_QLINK(item_t)	qlink;
} item_t;

typedef ISC_QUEUE(item_t) item_queue_t;

static void
item_init(item_t *item, int value) {
	item->value = value;
	ISC_QLINK_INIT(item, qlink);
}

/*
 * Individual unit tests
 */

/* Test UDP sendto/recv (IPv4) */
ATF_TC(queue_valid);
ATF_TC_HEAD(queue_valid, tc) {
	atf_tc_set_md_var(tc, "descr", "Check queue validity");
}
ATF_TC_BODY(queue_valid, tc) {
	isc_result_t result;
	item_queue_t queue;
	item_t one, two, three, four, five;
	item_t *p;

	UNUSED(tc);

	ISC_QUEUE_INIT(queue, qlink);

	item_init(&one, 1);
	item_init(&two, 2);
	item_init(&three, 3);
	item_init(&four, 4);
	item_init(&five, 5);

	result = isc_test_begin(NULL, ISC_TRUE);
	ATF_REQUIRE_EQ(result, ISC_R_SUCCESS);

	ATF_CHECK(ISC_QUEUE_EMPTY(queue));

	ISC_QUEUE_POP(queue, qlink, p);
	ATF_CHECK(p == NULL);

	ATF_CHECK(! ISC_QLINK_LINKED(&one, qlink));
	ISC_QUEUE_PUSH(queue, &one, qlink);
	ATF_CHECK(ISC_QLINK_LINKED(&one, qlink));

	ATF_CHECK(! ISC_QUEUE_EMPTY(queue));

	ISC_QUEUE_POP(queue, qlink, p);
	ATF_REQUIRE(p != NULL);
	ATF_CHECK_EQ(p->value, 1);
	ATF_CHECK(ISC_QUEUE_EMPTY(queue));
	ATF_CHECK(! ISC_QLINK_LINKED(p, qlink));

	ISC_QUEUE_PUSH(queue, p, qlink);
	ATF_CHECK(! ISC_QUEUE_EMPTY(queue));
	ATF_CHECK(ISC_QLINK_LINKED(p, qlink));

	ATF_CHECK(! ISC_QLINK_LINKED(&two, qlink));
	ISC_QUEUE_PUSH(queue, &two, qlink);
	ATF_CHECK(ISC_QLINK_LINKED(&two, qlink));

	ATF_CHECK(! ISC_QLINK_LINKED(&three, qlink));
	ISC_QUEUE_PUSH(queue, &three, qlink);
	ATF_CHECK(ISC_QLINK_LINKED(&three, qlink));

	ATF_CHECK(! ISC_QLINK_LINKED(&four, qlink));
	ISC_QUEUE_PUSH(queue, &four, qlink);
	ATF_CHECK(ISC_QLINK_LINKED(&four, qlink));

	ATF_CHECK(! ISC_QLINK_LINKED(&five, qlink));
	ISC_QUEUE_PUSH(queue, &five, qlink);
	ATF_CHECK(ISC_QLINK_LINKED(&five, qlink));

	ISC_QUEUE_POP(queue, qlink, p);
	ATF_REQUIRE(p != NULL);
	ATF_CHECK_EQ(p->value, 1);

	ISC_QUEUE_POP(queue, qlink, p);
	ATF_REQUIRE(p != NULL);
	ATF_CHECK_EQ(p->value, 2);

	ISC_QUEUE_POP(queue, qlink, p);
	ATF_REQUIRE(p != NULL);
	ATF_CHECK_EQ(p->value, 3);

	ISC_QUEUE_POP(queue, qlink, p);
	ATF_REQUIRE(p != NULL);
	ATF_CHECK_EQ(p->value, 4);

	ISC_QUEUE_POP(queue, qlink, p);
	ATF_REQUIRE(p != NULL);
	ATF_CHECK_EQ(p->value, 5);

	ATF_CHECK(ISC_QUEUE_EMPTY(queue));

	ISC_QUEUE_DESTROY(queue);
	isc_test_end();
}

/*
 * Main
 */
ATF_TP_ADD_TCS(tp) {
	ATF_TP_ADD_TC(tp, queue_valid);

	return (atf_no_error());
}

