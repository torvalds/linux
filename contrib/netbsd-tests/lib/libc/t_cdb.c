/*	$NetBSD: t_cdb.c,v 1.2 2017/01/10 22:24:29 christos Exp $	*/
/*-
 * Copyright (c) 2012 The NetBSD Foundation, Inc.
 * All rights reserved.
 *
 * This code is derived from software contributed to The NetBSD Foundation
 * by Joerg Sonnenberger.
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
__RCSID("$NetBSD: t_cdb.c,v 1.2 2017/01/10 22:24:29 christos Exp $");

#include <atf-c.h>

#include <sys/stat.h>

#include <assert.h>
#include <cdbr.h>
#include <cdbw.h>
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define	MAXKEYS	16384

static const char database_name[] = "test.cdb";

uint32_t keys[MAXKEYS];

static int
cmp_keys(const void *a_, const void *b_)
{
	uint32_t a = *(const uint32_t *)a_;
	uint32_t b = *(const uint32_t *)b_;

	return a > b ? 1 : (a < b ? 1 : 0);
}

static void
init_keys(size_t len)
{
	uint32_t sorted_keys[MAXKEYS];
	size_t i;

	assert(len <= MAXKEYS);

	if (len == 0)
		return;

	do {
		for (i = 0; i < len; ++i)
			sorted_keys[i] = keys[i] = arc4random();

		qsort(sorted_keys, len, sizeof(*sorted_keys), cmp_keys);
		for (i = 1; i < len; ++i) {
			if (sorted_keys[i - 1] == sorted_keys[i])
				break;
		}
	} while (i != len);
}

static void
write_database(size_t len)
{
	struct cdbw *db;
	int fd;
	size_t i;
	uint32_t buf[2];

	ATF_REQUIRE((db = cdbw_open()) != NULL);
	ATF_REQUIRE((fd = creat(database_name, S_IRUSR|S_IWUSR)) != -1);
	for (i = 0; i < len; ++i) {
		buf[0] = i;
		buf[1] = keys[i];
		ATF_REQUIRE(cdbw_put(db, &keys[i], sizeof(keys[i]),
		    buf, sizeof(buf)) == 0);
	}
	ATF_REQUIRE(cdbw_output(db, fd, "test database", arc4random) == 0);
	cdbw_close(db);
	ATF_REQUIRE(close(fd) == 0);
}

static void
check_database(size_t len)
{
	struct cdbr *db;
	size_t i, data_len;
	const void *data;
	uint32_t buf[2];

	ATF_REQUIRE((db = cdbr_open(database_name, CDBR_DEFAULT)) != NULL);
	ATF_REQUIRE_EQ(cdbr_entries(db), len);
	for (i = 0; i < len; ++i) {
		ATF_REQUIRE(cdbr_find(db, &keys[i], sizeof(keys[i]),
		    &data, &data_len) != -1);
		ATF_REQUIRE_EQ(data_len, sizeof(buf));
		memcpy(buf, data, sizeof(buf));
		ATF_REQUIRE_EQ(buf[0], i);
		ATF_REQUIRE_EQ(buf[1], keys[i]);
	}
	cdbr_close(db);
}

ATF_TC_WITH_CLEANUP(cdb);

ATF_TC_HEAD(cdb, tc)
{

	atf_tc_set_md_var(tc, "descr", "Test cdb(5) reading and writing");
}

ATF_TC_BODY(cdb, tc)
{
	size_t i, sizes[] = { 0, 16, 64, 1024, 2048 };
	for (i = 0; i < __arraycount(sizes); ++i) {
		init_keys(sizes[i]);
		write_database(sizes[i]);
		check_database(sizes[i]);
		unlink(database_name);
	}
}

ATF_TC_CLEANUP(cdb, tc)
{

	unlink(database_name);
}

ATF_TP_ADD_TCS(tp)
{

	ATF_TP_ADD_TC(tp, cdb);

	return atf_no_error();
}

