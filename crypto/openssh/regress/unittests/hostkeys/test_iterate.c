/* 	$OpenBSD: test_iterate.c,v 1.6 2018/07/16 03:09:59 djm Exp $ */
/*
 * Regress test for hostfile.h hostkeys_foreach()
 *
 * Placed in the public domain
 */

#include "includes.h"

#include <sys/types.h>
#include <sys/param.h>
#include <stdio.h>
#ifdef HAVE_STDINT_H
#include <stdint.h>
#endif
#include <stdlib.h>
#include <string.h>

#include "../test_helper/test_helper.h"

#include "sshkey.h"
#include "authfile.h"
#include "hostfile.h"

struct expected {
	const char *key_file;		/* Path for key, NULL for none */
	int no_parse_status;		/* Expected status w/o key parsing */
	int no_parse_keytype;		/* Expected keytype w/o key parsing */
	int match_host_p;		/* Match 'prometheus.example.com' */
	int match_host_s;		/* Match 'sisyphus.example.com' */
	int match_ipv4;			/* Match '192.0.2.1' */
	int match_ipv6;			/* Match '2001:db8::1' */
	int match_flags;		/* Expected flags from match */
	struct hostkey_foreach_line l;	/* Expected line contents */
};

struct cbctx {
	const struct expected *expected;
	size_t nexpected;
	size_t i;
	int flags;
	int match_host_p;
	int match_host_s;
	int match_ipv4;
	int match_ipv6;
};

/*
 * hostkeys_foreach() iterator callback that verifies the line passed
 * against an array of expected entries.
 */
static int
check(struct hostkey_foreach_line *l, void *_ctx)
{
	struct cbctx *ctx = (struct cbctx *)_ctx;
	const struct expected *expected;
	int parse_key = (ctx->flags & HKF_WANT_PARSE_KEY) != 0;
	const int matching = (ctx->flags & HKF_WANT_MATCH) != 0;
	u_int expected_status, expected_match;
	int expected_keytype;

	test_subtest_info("entry %zu/%zu, file line %ld",
	    ctx->i + 1, ctx->nexpected, l->linenum);

	for (;;) {
		ASSERT_SIZE_T_LT(ctx->i, ctx->nexpected);
		expected = ctx->expected + ctx->i++;
		/* If we are matching host/IP then skip entries that don't */
		if (!matching)
			break;
		if (ctx->match_host_p && expected->match_host_p)
			break;
		if (ctx->match_host_s && expected->match_host_s)
			break;
		if (ctx->match_ipv4 && expected->match_ipv4)
			break;
		if (ctx->match_ipv6 && expected->match_ipv6)
			break;
	}
	expected_status = (parse_key || expected->no_parse_status < 0) ?
	    expected->l.status : (u_int)expected->no_parse_status;
	expected_match = expected->l.match;
#define UPDATE_MATCH_STATUS(x) do { \
		if (ctx->x && expected->x) { \
			expected_match |= expected->x; \
			if (expected_status == HKF_STATUS_OK) \
				expected_status = HKF_STATUS_MATCHED; \
		} \
	} while (0)
	expected_keytype = (parse_key || expected->no_parse_keytype < 0) ?
	    expected->l.keytype : expected->no_parse_keytype;

#ifndef OPENSSL_HAS_ECC
	if (expected->l.keytype == KEY_ECDSA ||
	    expected->no_parse_keytype == KEY_ECDSA) {
		expected_status = HKF_STATUS_INVALID;
		expected_keytype = KEY_UNSPEC;
		parse_key = 0;
	}
#endif

	UPDATE_MATCH_STATUS(match_host_p);
	UPDATE_MATCH_STATUS(match_host_s);
	UPDATE_MATCH_STATUS(match_ipv4);
	UPDATE_MATCH_STATUS(match_ipv6);

	ASSERT_PTR_NE(l->path, NULL); /* Don't care about path */
	ASSERT_LONG_LONG_EQ(l->linenum, expected->l.linenum);
	ASSERT_U_INT_EQ(l->status, expected_status);
	ASSERT_U_INT_EQ(l->match, expected_match);
	/* Not all test entries contain fulltext */
	if (expected->l.line != NULL)
		ASSERT_STRING_EQ(l->line, expected->l.line);
	ASSERT_INT_EQ(l->marker, expected->l.marker);
	/* XXX we skip hashed hostnames for now; implement checking */
	if (expected->l.hosts != NULL)
		ASSERT_STRING_EQ(l->hosts, expected->l.hosts);
	/* Not all test entries contain raw keys */
	if (expected->l.rawkey != NULL)
		ASSERT_STRING_EQ(l->rawkey, expected->l.rawkey);
	/* XXX synthesise raw key for cases lacking and compare */
	ASSERT_INT_EQ(l->keytype, expected_keytype);
	if (parse_key) {
		if (expected->l.key == NULL)
			ASSERT_PTR_EQ(l->key, NULL);
		if (expected->l.key != NULL) {
			ASSERT_PTR_NE(l->key, NULL);
			ASSERT_INT_EQ(sshkey_equal(l->key, expected->l.key), 1);
		}
	}
	if (parse_key && !(l->comment == NULL && expected->l.comment == NULL))
		ASSERT_STRING_EQ(l->comment, expected->l.comment);
	return 0;
}

/* Loads public keys for a set of expected results */
static void
prepare_expected(struct expected *expected, size_t n)
{
	size_t i;

	for (i = 0; i < n; i++) {
		if (expected[i].key_file == NULL)
			continue;
#ifndef OPENSSL_HAS_ECC
		if (expected[i].l.keytype == KEY_ECDSA)
			continue;
#endif
		ASSERT_INT_EQ(sshkey_load_public(
		    test_data_file(expected[i].key_file), &expected[i].l.key,
		    NULL), 0);
	}
}

static void
cleanup_expected(struct expected *expected, size_t n)
{
	size_t i;

	for (i = 0; i < n; i++) {
		sshkey_free(expected[i].l.key);
		expected[i].l.key = NULL;
	}
}

struct expected expected_full[] = {
	{ NULL, -1, -1, 0, 0, 0, 0, -1, {
		NULL,				/* path, don't care */
		1,				/* line number */
		HKF_STATUS_COMMENT,		/* status */
		0,				/* match flags */
		"# Plain host keys, plain host names", /* full line, optional */
		MRK_NONE,			/* marker (CA / revoked) */
		NULL,				/* hosts text */
		NULL,				/* raw key, optional */
		KEY_UNSPEC,			/* key type */
		NULL,				/* deserialised key */
		NULL,				/* comment */
	} },
	{ "dsa_1.pub" , -1, -1, 0, HKF_MATCH_HOST, 0, 0, -1, {
		NULL,
		2,
		HKF_STATUS_OK,
		0,
		NULL,
		MRK_NONE,
		"sisyphus.example.com",
		NULL,
		KEY_DSA,
		NULL,	/* filled at runtime */
		"DSA #1",
	} },
	{ "ecdsa_1.pub" , -1, -1, 0, HKF_MATCH_HOST, 0, 0, -1, {
		NULL,
		3,
		HKF_STATUS_OK,
		0,
		NULL,
		MRK_NONE,
		"sisyphus.example.com",
		NULL,
		KEY_ECDSA,
		NULL,	/* filled at runtime */
		"ECDSA #1",
	} },
	{ "ed25519_1.pub" , -1, -1, 0, HKF_MATCH_HOST, 0, 0, -1, {
		NULL,
		4,
		HKF_STATUS_OK,
		0,
		NULL,
		MRK_NONE,
		"sisyphus.example.com",
		NULL,
		KEY_ED25519,
		NULL,	/* filled at runtime */
		"ED25519 #1",
	} },
	{ "rsa_1.pub" , -1, -1, 0, HKF_MATCH_HOST, 0, 0, -1, {
		NULL,
		5,
		HKF_STATUS_OK,
		0,
		NULL,
		MRK_NONE,
		"sisyphus.example.com",
		NULL,
		KEY_RSA,
		NULL,	/* filled at runtime */
		"RSA #1",
	} },
	{ NULL, -1, -1, 0, 0, 0, 0, -1, {
		NULL,
		6,
		HKF_STATUS_COMMENT,
		0,
		"",
		MRK_NONE,
		NULL,
		NULL,
		KEY_UNSPEC,
		NULL,
		NULL,
	} },
	{ NULL, -1, -1, 0, 0, 0, 0, -1, {
		NULL,
		7,
		HKF_STATUS_COMMENT,
		0,
		"# Plain host keys, hostnames + addresses",
		MRK_NONE,
		NULL,
		NULL,
		KEY_UNSPEC,
		NULL,
		NULL,
	} },
	{ "dsa_2.pub" , -1, -1, HKF_MATCH_HOST, 0, HKF_MATCH_IP, HKF_MATCH_IP, -1, {
		NULL,
		8,
		HKF_STATUS_OK,
		0,
		NULL,
		MRK_NONE,
		"prometheus.example.com,192.0.2.1,2001:db8::1",
		NULL,
		KEY_DSA,
		NULL,	/* filled at runtime */
		"DSA #2",
	} },
	{ "ecdsa_2.pub" , -1, -1, HKF_MATCH_HOST, 0, HKF_MATCH_IP, HKF_MATCH_IP, -1, {
		NULL,
		9,
		HKF_STATUS_OK,
		0,
		NULL,
		MRK_NONE,
		"prometheus.example.com,192.0.2.1,2001:db8::1",
		NULL,
		KEY_ECDSA,
		NULL,	/* filled at runtime */
		"ECDSA #2",
	} },
	{ "ed25519_2.pub" , -1, -1, HKF_MATCH_HOST, 0, HKF_MATCH_IP, HKF_MATCH_IP, -1, {
		NULL,
		10,
		HKF_STATUS_OK,
		0,
		NULL,
		MRK_NONE,
		"prometheus.example.com,192.0.2.1,2001:db8::1",
		NULL,
		KEY_ED25519,
		NULL,	/* filled at runtime */
		"ED25519 #2",
	} },
	{ "rsa_2.pub" , -1, -1, HKF_MATCH_HOST, 0, HKF_MATCH_IP, HKF_MATCH_IP, -1, {
		NULL,
		11,
		HKF_STATUS_OK,
		0,
		NULL,
		MRK_NONE,
		"prometheus.example.com,192.0.2.1,2001:db8::1",
		NULL,
		KEY_RSA,
		NULL,	/* filled at runtime */
		"RSA #2",
	} },
	{ NULL, -1, -1, 0, 0, 0, 0, -1, {
		NULL,
		12,
		HKF_STATUS_COMMENT,
		0,
		"",
		MRK_NONE,
		NULL,
		NULL,
		KEY_UNSPEC,
		NULL,
		NULL,
	} },
	{ NULL, -1, -1, 0, 0, 0, 0, -1, {
		NULL,
		13,
		HKF_STATUS_COMMENT,
		0,
		"# Some hosts with wildcard names / IPs",
		MRK_NONE,
		NULL,
		NULL,
		KEY_UNSPEC,
		NULL,
		NULL,
	} },
	{ "dsa_3.pub" , -1, -1, HKF_MATCH_HOST, HKF_MATCH_HOST, HKF_MATCH_IP, HKF_MATCH_IP, -1, {
		NULL,
		14,
		HKF_STATUS_OK,
		0,
		NULL,
		MRK_NONE,
		"*.example.com,192.0.2.*,2001:*",
		NULL,
		KEY_DSA,
		NULL,	/* filled at runtime */
		"DSA #3",
	} },
	{ "ecdsa_3.pub" , -1, -1, HKF_MATCH_HOST, HKF_MATCH_HOST, HKF_MATCH_IP, HKF_MATCH_IP, -1, {
		NULL,
		15,
		HKF_STATUS_OK,
		0,
		NULL,
		MRK_NONE,
		"*.example.com,192.0.2.*,2001:*",
		NULL,
		KEY_ECDSA,
		NULL,	/* filled at runtime */
		"ECDSA #3",
	} },
	{ "ed25519_3.pub" , -1, -1, HKF_MATCH_HOST, HKF_MATCH_HOST, HKF_MATCH_IP, HKF_MATCH_IP, -1, {
		NULL,
		16,
		HKF_STATUS_OK,
		0,
		NULL,
		MRK_NONE,
		"*.example.com,192.0.2.*,2001:*",
		NULL,
		KEY_ED25519,
		NULL,	/* filled at runtime */
		"ED25519 #3",
	} },
	{ "rsa_3.pub" , -1, -1, HKF_MATCH_HOST, HKF_MATCH_HOST, HKF_MATCH_IP, HKF_MATCH_IP, -1, {
		NULL,
		17,
		HKF_STATUS_OK,
		0,
		NULL,
		MRK_NONE,
		"*.example.com,192.0.2.*,2001:*",
		NULL,
		KEY_RSA,
		NULL,	/* filled at runtime */
		"RSA #3",
	} },
	{ NULL, -1, -1, 0, 0, 0, 0, -1, {
		NULL,
		18,
		HKF_STATUS_COMMENT,
		0,
		"",
		MRK_NONE,
		NULL,
		NULL,
		KEY_UNSPEC,
		NULL,
		NULL,
	} },
	{ NULL, -1, -1, 0, 0, 0, 0, -1, {
		NULL,
		19,
		HKF_STATUS_COMMENT,
		0,
		"# Hashed hostname and address entries",
		MRK_NONE,
		NULL,
		NULL,
		KEY_UNSPEC,
		NULL,
		NULL,
	} },
	{ "dsa_5.pub" , -1, -1, 0, HKF_MATCH_HOST|HKF_MATCH_HOST_HASHED, 0, 0, -1, {
		NULL,
		20,
		HKF_STATUS_OK,
		0,
		NULL,
		MRK_NONE,
		NULL,
		NULL,
		KEY_DSA,
		NULL,	/* filled at runtime */
		"DSA #5",
	} },
	{ "ecdsa_5.pub" , -1, -1, 0, HKF_MATCH_HOST|HKF_MATCH_HOST_HASHED, 0, 0, -1, {
		NULL,
		21,
		HKF_STATUS_OK,
		0,
		NULL,
		MRK_NONE,
		NULL,
		NULL,
		KEY_ECDSA,
		NULL,	/* filled at runtime */
		"ECDSA #5",
	} },
	{ "ed25519_5.pub" , -1, -1, 0, HKF_MATCH_HOST|HKF_MATCH_HOST_HASHED, 0, 0, -1, {
		NULL,
		22,
		HKF_STATUS_OK,
		0,
		NULL,
		MRK_NONE,
		NULL,
		NULL,
		KEY_ED25519,
		NULL,	/* filled at runtime */
		"ED25519 #5",
	} },
	{ "rsa_5.pub" , -1, -1, 0, HKF_MATCH_HOST|HKF_MATCH_HOST_HASHED, 0, 0, -1, {
		NULL,
		23,
		HKF_STATUS_OK,
		0,
		NULL,
		MRK_NONE,
		NULL,
		NULL,
		KEY_RSA,
		NULL,	/* filled at runtime */
		"RSA #5",
	} },
	{ NULL, -1, -1, 0, 0, 0, 0, -1, {
		NULL,
		24,
		HKF_STATUS_COMMENT,
		0,
		"",
		MRK_NONE,
		NULL,
		NULL,
		KEY_UNSPEC,
		NULL,
		NULL,
	} },
	/*
	 * The next series have each key listed multiple times, as the
	 * hostname and addresses in the pre-hashed known_hosts are split
	 * to separate lines.
	 */
	{ "dsa_6.pub" , -1, -1, HKF_MATCH_HOST|HKF_MATCH_HOST_HASHED, 0, 0, 0, -1, {
		NULL,
		25,
		HKF_STATUS_OK,
		0,
		NULL,
		MRK_NONE,
		NULL,
		NULL,
		KEY_DSA,
		NULL,	/* filled at runtime */
		"DSA #6",
	} },
	{ "dsa_6.pub" , -1, -1, 0, 0, HKF_MATCH_IP|HKF_MATCH_IP_HASHED, 0, -1, {
		NULL,
		26,
		HKF_STATUS_OK,
		0,
		NULL,
		MRK_NONE,
		NULL,
		NULL,
		KEY_DSA,
		NULL,	/* filled at runtime */
		"DSA #6",
	} },
	{ "dsa_6.pub" , -1, -1, 0, 0, 0, HKF_MATCH_IP|HKF_MATCH_IP_HASHED, -1, {
		NULL,
		27,
		HKF_STATUS_OK,
		0,
		NULL,
		MRK_NONE,
		NULL,
		NULL,
		KEY_DSA,
		NULL,	/* filled at runtime */
		"DSA #6",
	} },
	{ "ecdsa_6.pub" , -1, -1, HKF_MATCH_HOST|HKF_MATCH_HOST_HASHED, 0, 0, 0, -1, {
		NULL,
		28,
		HKF_STATUS_OK,
		0,
		NULL,
		MRK_NONE,
		NULL,
		NULL,
		KEY_ECDSA,
		NULL,	/* filled at runtime */
		"ECDSA #6",
	} },
	{ "ecdsa_6.pub" , -1, -1, 0, 0, HKF_MATCH_IP|HKF_MATCH_IP_HASHED, 0, -1, {
		NULL,
		29,
		HKF_STATUS_OK,
		0,
		NULL,
		MRK_NONE,
		NULL,
		NULL,
		KEY_ECDSA,
		NULL,	/* filled at runtime */
		"ECDSA #6",
	} },
	{ "ecdsa_6.pub" , -1, -1, 0, 0, 0, HKF_MATCH_IP|HKF_MATCH_IP_HASHED, -1, {
		NULL,
		30,
		HKF_STATUS_OK,
		0,
		NULL,
		MRK_NONE,
		NULL,
		NULL,
		KEY_ECDSA,
		NULL,	/* filled at runtime */
		"ECDSA #6",
	} },
	{ "ed25519_6.pub" , -1, -1, HKF_MATCH_HOST|HKF_MATCH_HOST_HASHED, 0, 0, 0, -1, {
		NULL,
		31,
		HKF_STATUS_OK,
		0,
		NULL,
		MRK_NONE,
		NULL,
		NULL,
		KEY_ED25519,
		NULL,	/* filled at runtime */
		"ED25519 #6",
	} },
	{ "ed25519_6.pub" , -1, -1, 0, 0, HKF_MATCH_IP|HKF_MATCH_IP_HASHED, 0, -1, {
		NULL,
		32,
		HKF_STATUS_OK,
		0,
		NULL,
		MRK_NONE,
		NULL,
		NULL,
		KEY_ED25519,
		NULL,	/* filled at runtime */
		"ED25519 #6",
	} },
	{ "ed25519_6.pub" , -1, -1, 0, 0, 0, HKF_MATCH_IP|HKF_MATCH_IP_HASHED, -1, {
		NULL,
		33,
		HKF_STATUS_OK,
		0,
		NULL,
		MRK_NONE,
		NULL,
		NULL,
		KEY_ED25519,
		NULL,	/* filled at runtime */
		"ED25519 #6",
	} },
	{ "rsa_6.pub" , -1, -1, HKF_MATCH_HOST|HKF_MATCH_HOST_HASHED, 0, 0, 0, -1, {
		NULL,
		34,
		HKF_STATUS_OK,
		0,
		NULL,
		MRK_NONE,
		NULL,
		NULL,
		KEY_RSA,
		NULL,	/* filled at runtime */
		"RSA #6",
	} },
	{ "rsa_6.pub" , -1, -1, 0, 0, HKF_MATCH_IP|HKF_MATCH_IP_HASHED, 0, -1, {
		NULL,
		35,
		HKF_STATUS_OK,
		0,
		NULL,
		MRK_NONE,
		NULL,
		NULL,
		KEY_RSA,
		NULL,	/* filled at runtime */
		"RSA #6",
	} },
	{ "rsa_6.pub" , -1, -1, 0, 0, 0, HKF_MATCH_IP|HKF_MATCH_IP_HASHED, -1, {
		NULL,
		36,
		HKF_STATUS_OK,
		0,
		NULL,
		MRK_NONE,
		NULL,
		NULL,
		KEY_RSA,
		NULL,	/* filled at runtime */
		"RSA #6",
	} },
	{ NULL, -1, -1, 0, 0, 0, 0, -1, {
		NULL,
		37,
		HKF_STATUS_COMMENT,
		0,
		"",
		MRK_NONE,
		NULL,
		NULL,
		KEY_UNSPEC,
		NULL,
		NULL,
	} },
	{ NULL, -1, -1, 0, 0, 0, 0, -1, {
		NULL,
		38,
		HKF_STATUS_COMMENT,
		0,
		"",
		MRK_NONE,
		NULL,
		NULL,
		KEY_UNSPEC,
		NULL,
		NULL,
	} },
	{ NULL, -1, -1, 0, 0, 0, 0, -1, {
		NULL,
		39,
		HKF_STATUS_COMMENT,
		0,
		"# Revoked and CA keys",
		MRK_NONE,
		NULL,
		NULL,
		KEY_UNSPEC,
		NULL,
		NULL,
	} },
	{ "ed25519_4.pub" , -1, -1, 0, HKF_MATCH_HOST, 0, 0, -1, {
		NULL,
		40,
		HKF_STATUS_OK,
		0,
		NULL,
		MRK_REVOKE,
		"sisyphus.example.com",
		NULL,
		KEY_ED25519,
		NULL,	/* filled at runtime */
		"ED25519 #4",
	} },
	{ "ecdsa_4.pub" , -1, -1, HKF_MATCH_HOST, 0, 0, 0, -1, {
		NULL,
		41,
		HKF_STATUS_OK,
		0,
		NULL,
		MRK_CA,
		"prometheus.example.com",
		NULL,
		KEY_ECDSA,
		NULL,	/* filled at runtime */
		"ECDSA #4",
	} },
	{ "dsa_4.pub" , -1, -1, HKF_MATCH_HOST, HKF_MATCH_HOST, 0, 0, -1, {
		NULL,
		42,
		HKF_STATUS_OK,
		0,
		NULL,
		MRK_CA,
		"*.example.com",
		NULL,
		KEY_DSA,
		NULL,	/* filled at runtime */
		"DSA #4",
	} },
	{ NULL, -1, -1, 0, 0, 0, 0, -1, {
		NULL,
		43,
		HKF_STATUS_COMMENT,
		0,
		"",
		MRK_NONE,
		NULL,
		NULL,
		KEY_UNSPEC,
		NULL,
		NULL,
	} },
	{ NULL, -1, -1, 0, 0, 0, 0, -1, {
		NULL,
		44,
		HKF_STATUS_COMMENT,
		0,
		"# Some invalid lines",
		MRK_NONE,
		NULL,
		NULL,
		KEY_UNSPEC,
		NULL,
		NULL,
	} },
	{ NULL, -1, -1, 0, 0, 0, 0, -1, {
		NULL,
		45,
		HKF_STATUS_INVALID,
		0,
		NULL,
		MRK_ERROR,
		NULL,
		NULL,
		KEY_UNSPEC,
		NULL,
		NULL,
	} },
	{ NULL, -1, -1, 0, HKF_MATCH_HOST, 0, 0, -1, {
		NULL,
		46,
		HKF_STATUS_INVALID,
		0,
		NULL,
		MRK_NONE,
		"sisyphus.example.com",
		NULL,
		KEY_UNSPEC,
		NULL,
		NULL,
	} },
	{ NULL, -1, -1, HKF_MATCH_HOST, 0, 0, 0, -1, {
		NULL,
		47,
		HKF_STATUS_INVALID,
		0,
		NULL,
		MRK_NONE,
		"prometheus.example.com",
		NULL,
		KEY_UNSPEC,
		NULL,
		NULL,
	} },
	{ NULL, -1, -1, 0, HKF_MATCH_HOST, 0, 0, -1, {
		NULL,
		48,
		HKF_STATUS_INVALID,	/* Would be ok if key not parsed */
		0,
		NULL,
		MRK_NONE,
		"sisyphus.example.com",
		NULL,
		KEY_UNSPEC,
		NULL,
		NULL,
	} },
	{ NULL, -1, -1, 0, HKF_MATCH_HOST, 0, 0, -1, {
		NULL,
		49,
		HKF_STATUS_INVALID,
		0,
		NULL,
		MRK_NONE,
		"sisyphus.example.com",
		NULL,
		KEY_UNSPEC,
		NULL,	/* filled at runtime */
		NULL,
	} },
	{ NULL, HKF_STATUS_OK, KEY_RSA, HKF_MATCH_HOST, 0, 0, 0, -1, {
		NULL,
		50,
		HKF_STATUS_INVALID,	/* Would be ok if key not parsed */
		0,
		NULL,
		MRK_NONE,
		"prometheus.example.com",
		NULL,
		KEY_UNSPEC,
		NULL,	/* filled at runtime */
		NULL,
	} },
};

void test_iterate(void);

void
test_iterate(void)
{
	struct cbctx ctx;

	TEST_START("hostkeys_iterate all with key parse");
	memset(&ctx, 0, sizeof(ctx));
	ctx.expected = expected_full;
	ctx.nexpected = sizeof(expected_full)/sizeof(*expected_full);
	ctx.flags = HKF_WANT_PARSE_KEY;
	prepare_expected(expected_full, ctx.nexpected);
	ASSERT_INT_EQ(hostkeys_foreach(test_data_file("known_hosts"),
	    check, &ctx, NULL, NULL, ctx.flags), 0);
	cleanup_expected(expected_full, ctx.nexpected);
	TEST_DONE();

	TEST_START("hostkeys_iterate all without key parse");
	memset(&ctx, 0, sizeof(ctx));
	ctx.expected = expected_full;
	ctx.nexpected = sizeof(expected_full)/sizeof(*expected_full);
	ctx.flags = 0;
	prepare_expected(expected_full, ctx.nexpected);
	ASSERT_INT_EQ(hostkeys_foreach(test_data_file("known_hosts"),
	    check, &ctx, NULL, NULL, ctx.flags), 0);
	cleanup_expected(expected_full, ctx.nexpected);
	TEST_DONE();

	TEST_START("hostkeys_iterate specify host 1");
	memset(&ctx, 0, sizeof(ctx));
	ctx.expected = expected_full;
	ctx.nexpected = sizeof(expected_full)/sizeof(*expected_full);
	ctx.flags = 0;
	ctx.match_host_p = 1;
	prepare_expected(expected_full, ctx.nexpected);
	ASSERT_INT_EQ(hostkeys_foreach(test_data_file("known_hosts"),
	    check, &ctx, "prometheus.example.com", NULL, ctx.flags), 0);
	cleanup_expected(expected_full, ctx.nexpected);
	TEST_DONE();

	TEST_START("hostkeys_iterate specify host 2");
	memset(&ctx, 0, sizeof(ctx));
	ctx.expected = expected_full;
	ctx.nexpected = sizeof(expected_full)/sizeof(*expected_full);
	ctx.flags = 0;
	ctx.match_host_s = 1;
	prepare_expected(expected_full, ctx.nexpected);
	ASSERT_INT_EQ(hostkeys_foreach(test_data_file("known_hosts"),
	    check, &ctx, "sisyphus.example.com", NULL, ctx.flags), 0);
	cleanup_expected(expected_full, ctx.nexpected);
	TEST_DONE();

	TEST_START("hostkeys_iterate match host 1");
	memset(&ctx, 0, sizeof(ctx));
	ctx.expected = expected_full;
	ctx.nexpected = sizeof(expected_full)/sizeof(*expected_full);
	ctx.flags = HKF_WANT_MATCH;
	ctx.match_host_p = 1;
	prepare_expected(expected_full, ctx.nexpected);
	ASSERT_INT_EQ(hostkeys_foreach(test_data_file("known_hosts"),
	    check, &ctx, "prometheus.example.com", NULL, ctx.flags), 0);
	cleanup_expected(expected_full, ctx.nexpected);
	TEST_DONE();

	TEST_START("hostkeys_iterate match host 2");
	memset(&ctx, 0, sizeof(ctx));
	ctx.expected = expected_full;
	ctx.nexpected = sizeof(expected_full)/sizeof(*expected_full);
	ctx.flags = HKF_WANT_MATCH;
	ctx.match_host_s = 1;
	prepare_expected(expected_full, ctx.nexpected);
	ASSERT_INT_EQ(hostkeys_foreach(test_data_file("known_hosts"),
	    check, &ctx, "sisyphus.example.com", NULL, ctx.flags), 0);
	cleanup_expected(expected_full, ctx.nexpected);
	TEST_DONE();

	TEST_START("hostkeys_iterate specify host missing");
	memset(&ctx, 0, sizeof(ctx));
	ctx.expected = expected_full;
	ctx.nexpected = sizeof(expected_full)/sizeof(*expected_full);
	ctx.flags = 0;
	prepare_expected(expected_full, ctx.nexpected);
	ASSERT_INT_EQ(hostkeys_foreach(test_data_file("known_hosts"),
	    check, &ctx, "actaeon.example.org", NULL, ctx.flags), 0);
	cleanup_expected(expected_full, ctx.nexpected);
	TEST_DONE();

	TEST_START("hostkeys_iterate match host missing");
	memset(&ctx, 0, sizeof(ctx));
	ctx.expected = expected_full;
	ctx.nexpected = sizeof(expected_full)/sizeof(*expected_full);
	ctx.flags = HKF_WANT_MATCH;
	prepare_expected(expected_full, ctx.nexpected);
	ASSERT_INT_EQ(hostkeys_foreach(test_data_file("known_hosts"),
	    check, &ctx, "actaeon.example.org", NULL, ctx.flags), 0);
	cleanup_expected(expected_full, ctx.nexpected);
	TEST_DONE();

	TEST_START("hostkeys_iterate specify IPv4");
	memset(&ctx, 0, sizeof(ctx));
	ctx.expected = expected_full;
	ctx.nexpected = sizeof(expected_full)/sizeof(*expected_full);
	ctx.flags = 0;
	ctx.match_ipv4 = 1;
	prepare_expected(expected_full, ctx.nexpected);
	ASSERT_INT_EQ(hostkeys_foreach(test_data_file("known_hosts"),
	    check, &ctx, "tiresias.example.org", "192.0.2.1", ctx.flags), 0);
	cleanup_expected(expected_full, ctx.nexpected);
	TEST_DONE();

	TEST_START("hostkeys_iterate specify IPv6");
	memset(&ctx, 0, sizeof(ctx));
	ctx.expected = expected_full;
	ctx.nexpected = sizeof(expected_full)/sizeof(*expected_full);
	ctx.flags = 0;
	ctx.match_ipv6 = 1;
	prepare_expected(expected_full, ctx.nexpected);
	ASSERT_INT_EQ(hostkeys_foreach(test_data_file("known_hosts"),
	    check, &ctx, "tiresias.example.org", "2001:db8::1", ctx.flags), 0);
	cleanup_expected(expected_full, ctx.nexpected);
	TEST_DONE();

	TEST_START("hostkeys_iterate match IPv4");
	memset(&ctx, 0, sizeof(ctx));
	ctx.expected = expected_full;
	ctx.nexpected = sizeof(expected_full)/sizeof(*expected_full);
	ctx.flags = HKF_WANT_MATCH;
	ctx.match_ipv4 = 1;
	prepare_expected(expected_full, ctx.nexpected);
	ASSERT_INT_EQ(hostkeys_foreach(test_data_file("known_hosts"),
	    check, &ctx, "tiresias.example.org", "192.0.2.1", ctx.flags), 0);
	cleanup_expected(expected_full, ctx.nexpected);
	TEST_DONE();

	TEST_START("hostkeys_iterate match IPv6");
	memset(&ctx, 0, sizeof(ctx));
	ctx.expected = expected_full;
	ctx.nexpected = sizeof(expected_full)/sizeof(*expected_full);
	ctx.flags = HKF_WANT_MATCH;
	ctx.match_ipv6 = 1;
	prepare_expected(expected_full, ctx.nexpected);
	ASSERT_INT_EQ(hostkeys_foreach(test_data_file("known_hosts"),
	    check, &ctx, "tiresias.example.org", "2001:db8::1", ctx.flags), 0);
	cleanup_expected(expected_full, ctx.nexpected);
	TEST_DONE();

	TEST_START("hostkeys_iterate specify addr missing");
	memset(&ctx, 0, sizeof(ctx));
	ctx.expected = expected_full;
	ctx.nexpected = sizeof(expected_full)/sizeof(*expected_full);
	ctx.flags = 0;
	prepare_expected(expected_full, ctx.nexpected);
	ASSERT_INT_EQ(hostkeys_foreach(test_data_file("known_hosts"),
	    check, &ctx, "tiresias.example.org", "192.168.0.1", ctx.flags), 0);
	cleanup_expected(expected_full, ctx.nexpected);
	TEST_DONE();

	TEST_START("hostkeys_iterate match addr missing");
	memset(&ctx, 0, sizeof(ctx));
	ctx.expected = expected_full;
	ctx.nexpected = sizeof(expected_full)/sizeof(*expected_full);
	ctx.flags = HKF_WANT_MATCH;
	prepare_expected(expected_full, ctx.nexpected);
	ASSERT_INT_EQ(hostkeys_foreach(test_data_file("known_hosts"),
	    check, &ctx, "tiresias.example.org", "::1", ctx.flags), 0);
	cleanup_expected(expected_full, ctx.nexpected);
	TEST_DONE();

	TEST_START("hostkeys_iterate specify host 2 and IPv4");
	memset(&ctx, 0, sizeof(ctx));
	ctx.expected = expected_full;
	ctx.nexpected = sizeof(expected_full)/sizeof(*expected_full);
	ctx.flags = 0;
	ctx.match_host_s = 1;
	ctx.match_ipv4 = 1;
	prepare_expected(expected_full, ctx.nexpected);
	ASSERT_INT_EQ(hostkeys_foreach(test_data_file("known_hosts"),
	    check, &ctx, "sisyphus.example.com", "192.0.2.1", ctx.flags), 0);
	cleanup_expected(expected_full, ctx.nexpected);
	TEST_DONE();

	TEST_START("hostkeys_iterate match host 1 and IPv6");
	memset(&ctx, 0, sizeof(ctx));
	ctx.expected = expected_full;
	ctx.nexpected = sizeof(expected_full)/sizeof(*expected_full);
	ctx.flags = HKF_WANT_MATCH;
	ctx.match_host_p = 1;
	ctx.match_ipv6 = 1;
	prepare_expected(expected_full, ctx.nexpected);
	ASSERT_INT_EQ(hostkeys_foreach(test_data_file("known_hosts"),
	    check, &ctx, "prometheus.example.com",
	    "2001:db8::1", ctx.flags), 0);
	cleanup_expected(expected_full, ctx.nexpected);
	TEST_DONE();

	TEST_START("hostkeys_iterate specify host 2 and IPv4 w/ key parse");
	memset(&ctx, 0, sizeof(ctx));
	ctx.expected = expected_full;
	ctx.nexpected = sizeof(expected_full)/sizeof(*expected_full);
	ctx.flags = HKF_WANT_PARSE_KEY;
	ctx.match_host_s = 1;
	ctx.match_ipv4 = 1;
	prepare_expected(expected_full, ctx.nexpected);
	ASSERT_INT_EQ(hostkeys_foreach(test_data_file("known_hosts"),
	    check, &ctx, "sisyphus.example.com", "192.0.2.1", ctx.flags), 0);
	cleanup_expected(expected_full, ctx.nexpected);
	TEST_DONE();

	TEST_START("hostkeys_iterate match host 1 and IPv6 w/ key parse");
	memset(&ctx, 0, sizeof(ctx));
	ctx.expected = expected_full;
	ctx.nexpected = sizeof(expected_full)/sizeof(*expected_full);
	ctx.flags = HKF_WANT_MATCH|HKF_WANT_PARSE_KEY;
	ctx.match_host_p = 1;
	ctx.match_ipv6 = 1;
	prepare_expected(expected_full, ctx.nexpected);
	ASSERT_INT_EQ(hostkeys_foreach(test_data_file("known_hosts"),
	    check, &ctx, "prometheus.example.com",
	    "2001:db8::1", ctx.flags), 0);
	cleanup_expected(expected_full, ctx.nexpected);
	TEST_DONE();
}

