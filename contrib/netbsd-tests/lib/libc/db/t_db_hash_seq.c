/*	$NetBSD: t_db_hash_seq.c,v 1.2 2015/06/22 22:35:51 christos Exp $	*/

/*-
 * Copyright (c) 2015 The NetBSD Foundation, Inc.
 * All rights reserved.
 *
 * This code is derived from software contributed to The NetBSD Foundation
 * by Christos Zoulas.
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
__RCSID("$NetBSD: t_db_hash_seq.c,v 1.2 2015/06/22 22:35:51 christos Exp $");

#include <sys/types.h>
#include <sys/socket.h>
#include <db.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <stdlib.h>
#include <fcntl.h>
#include <syslog.h>
#include <netinet/in.h>

#define ATF

struct conf {
	struct sockaddr_storage	c_ss;
	int			c_lmask;
	int			c_port;
	int			c_proto;
	int			c_family;
	int			c_uid;
	int			c_nfail;
	char			c_name[128];
	int			c_rmask;
	int			c_duration;
};

struct dbinfo {
	int count;
	time_t last;
	char id[64];
};

#ifdef ATF
#include <atf-c.h>

#define DO_ERR(msg, ...)	ATF_REQUIRE_MSG(0, msg, __VA_ARGS__)
#define DO_WARNX(msg, ...)	ATF_REQUIRE_MSG(0, msg, __VA_ARGS__)
#else
#include <err.h>

#define DO_ERR(fmt, ...)	err(EXIT_FAILURE, fmt, __VA_ARGS__)
#define DO_WARNX(fmt, ...)	warnx(fmt, __VA_ARGS__)
#endif

#define DO_DEBUG(fmt, ...)	fprintf(stderr, fmt, __VA_ARGS__)

static HASHINFO openinfo = {
	4096,		/* bsize */
	32,		/* ffactor */
	256,		/* nelem */
	8 * 1024 * 1024,/* cachesize */
	NULL,		/* hash() */
	0		/* lorder */
};

static int debug = 0;

static int
state_close(DB *db)
{
	if (db == NULL)
		return -1;
	if ((*db->close)(db) == -1)
		DO_ERR("%s: can't close db", __func__);
	return 0;
}

static DB *
state_open(const char *dbname, int flags, mode_t perm)
{
	DB *db;

	db = dbopen(dbname, flags, perm, DB_HASH, &openinfo);
	if (db == NULL) {
		if (errno == ENOENT && (flags & O_CREAT) == 0)
			return NULL;
		DO_ERR("%s: can't open `%s'", __func__, dbname);
	}
	return db;
}

static int
state_sizecheck(const DBT *t)
{
	if (sizeof(struct conf) == t->size)
		return 0;
	DO_WARNX("Key size mismatch %zu != %zu", sizeof(struct conf), t->size);
	return 0;
}

static int
state_del(DB *db, const struct conf *c)
{
	int rv;
	DBT k;

	if (db == NULL)
		return -1;

	k.data = __UNCONST(c);
	k.size = sizeof(*c);

	switch (rv = (*db->del)(db, &k, 1)) {
	case 0:
	case 1:
		if (debug > 1) {
			DO_DEBUG("%s: returns %d", __func__, rv);
			(*db->sync)(db, 0);
		}
		return 0;
	default:
		DO_ERR("%s: failed", __func__);
		return -1;
	}
}

#if 0
static int
state_get(DB *db, const struct conf *c, struct dbinfo *dbi)
{
	int rv;
	DBT k, v;

	if (db == NULL)
		return -1;

	k.data = __UNCONST(c);
	k.size = sizeof(*c);

	switch (rv = (*db->get)(db, &k, &v, 0)) {
	case 0:
	case 1:
		if (rv)
			memset(dbi, 0, sizeof(*dbi));
		else
			memcpy(dbi, v.data, sizeof(*dbi));
		if (debug > 1)
			DO_DEBUG("%s: returns %d", __func__, rv);
		return 0;
	default:
		DO_ERR("%s: failed", __func__);
		return -1;
	}
}
#endif

static int
state_put(DB *db, const struct conf *c, const struct dbinfo *dbi)
{
	int rv;
	DBT k, v;

	if (db == NULL)
		return -1;

	k.data = __UNCONST(c);
	k.size = sizeof(*c);
	v.data = __UNCONST(dbi);
	v.size = sizeof(*dbi);

	switch (rv = (*db->put)(db, &k, &v, 0)) {
	case 0:
		if (debug > 1) {
			DO_DEBUG("%s: returns %d", __func__, rv);
			(*db->sync)(db, 0);
		}
		return 0;
	case 1:
		errno = EEXIST;
		/*FALLTHROUGH*/
	default:
		DO_ERR("%s: failed", __func__);
	}
}

static int
state_iterate(DB *db, struct conf *c, struct dbinfo *dbi, unsigned int first)
{
	int rv;
	DBT k, v;

	if (db == NULL)
		return -1;

	first = first ? R_FIRST : R_NEXT;

	switch (rv = (*db->seq)(db, &k, &v, first)) {
	case 0:
		if (state_sizecheck(&k) == -1)
			return -1;
		memcpy(c, k.data, sizeof(*c));
		memcpy(dbi, v.data, sizeof(*dbi));
		if (debug > 1)
			DO_DEBUG("%s: returns %d", __func__, rv);
		return 1;
	case 1:
		if (debug > 1)
			DO_DEBUG("%s: returns %d", __func__, rv);
		return 0;
	default:
		DO_ERR("%s: failed", __func__);
		return -1;
	}
}

#define MAXB 100

static int
testdb(int skip)
{
	size_t i;
	int f;
	char flag[MAXB];
	DB *db;
	struct conf c;
	struct dbinfo d;

	db = state_open(NULL, O_RDWR|O_CREAT|O_TRUNC, 0600);
	if (db == NULL)
		DO_ERR("%s: cannot open `%s'", __func__, "foo");

	memset(&c, 0, sizeof(c));
	memset(&d, 0, sizeof(d));
	memset(flag, 0, sizeof(flag));

	for (i = 0; i < __arraycount(flag); i++) {
		c.c_port = i;
		state_put(db, &c, &d);
	}

	for (f = 1, i = 0; state_iterate(db, &c, &d, f) == 1; f = 0, i++) {
		if (debug > 1)
			DO_DEBUG("%zu %d\n", i, c.c_port);
		if (flag[c.c_port])
			DO_WARNX("Already visited %d", c.c_port);
		flag[c.c_port] = 1;
		if (skip == 0 || c.c_port % skip != 0)
			continue;
		state_del(db, &c);
	}
	state_close(db);
	for (i = 0; i < __arraycount(flag); i++) {
		if (flag[i] == 0)
			DO_WARNX("Not visited %zu", i);
	}
	return 0;
}

#ifndef ATF
int
main(int argc, char *argv[])
{
	return testdb(6);
}
#else

ATF_TC(test_hash_del_none);
ATF_TC_HEAD(test_hash_del_none, tc)
{
	atf_tc_set_md_var(tc, "descr", "Check sequential scan of hash tables deleting none");
}

ATF_TC_BODY(test_hash_del_none, tc)
{
	testdb(0);
}

ATF_TC(test_hash_del_all);
ATF_TC_HEAD(test_hash_del_all, tc)
{
	atf_tc_set_md_var(tc, "descr", "Check sequential scan of hash tables deleting all");
}

ATF_TC_BODY(test_hash_del_all, tc)
{
	testdb(1);
}

ATF_TC(test_hash_del_alt);
ATF_TC_HEAD(test_hash_del_alt, tc)
{
	atf_tc_set_md_var(tc, "descr", "Check sequential scan of hash tables alternating deletets");
}

ATF_TC_BODY(test_hash_del_alt, tc)
{
	testdb(2);
}

ATF_TC(test_hash_del_every_7);
ATF_TC_HEAD(test_hash_del_every_7, tc)
{
	atf_tc_set_md_var(tc, "descr", "Check sequential scan of hash tables deleting every 7 elements");
}

ATF_TC_BODY(test_hash_del_every_7, tc)
{
	testdb(7);
}

ATF_TP_ADD_TCS(tp)
{
	ATF_TP_ADD_TC(tp, test_hash_del_none);
	ATF_TP_ADD_TC(tp, test_hash_del_all);
	ATF_TP_ADD_TC(tp, test_hash_del_alt);
	ATF_TP_ADD_TC(tp, test_hash_del_every_7);

	return 0;
}
#endif
