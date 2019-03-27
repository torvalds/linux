/* 	$OpenBSD: tests.c,v 1.1 2018/03/03 03:16:17 djm Exp $ */

/*
 * Regress test for keys options functions.
 *
 * Placed in the public domain
 */

#include <sys/types.h>
#include <sys/param.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "test_helper.h"

#include "sshkey.h"
#include "authfile.h"
#include "auth-options.h"
#include "misc.h"
#include "log.h"

static struct sshkey *
load_key(const char *name)
{
	struct sshkey *ret;
	int r;

	r = sshkey_load_public(test_data_file(name), &ret, NULL);
	ASSERT_INT_EQ(r, 0);
	ASSERT_PTR_NE(ret, NULL);
	return ret;
}

static struct sshauthopt *
default_authkey_opts(void)
{
	struct sshauthopt *ret = sshauthopt_new();

	ASSERT_PTR_NE(ret, NULL);
	ret->permit_port_forwarding_flag = 1;
	ret->permit_agent_forwarding_flag = 1;
	ret->permit_x11_forwarding_flag = 1;
	ret->permit_pty_flag = 1;
	ret->permit_user_rc = 1;
	return ret;
}

static struct sshauthopt *
default_authkey_restrict_opts(void)
{
	struct sshauthopt *ret = sshauthopt_new();

	ASSERT_PTR_NE(ret, NULL);
	ret->permit_port_forwarding_flag = 0;
	ret->permit_agent_forwarding_flag = 0;
	ret->permit_x11_forwarding_flag = 0;
	ret->permit_pty_flag = 0;
	ret->permit_user_rc = 0;
	ret->restricted = 1;
	return ret;
}

static char **
commasplit(const char *s, size_t *np)
{
	char *ocp, *cp, *cp2, **ret = NULL;
	size_t n;

	ocp = cp = strdup(s);
	ASSERT_PTR_NE(cp, NULL);
	for (n = 0; (cp2 = strsep(&cp, ",")) != NULL;) {
		ret = recallocarray(ret, n, n + 1, sizeof(*ret));
		ASSERT_PTR_NE(ret, NULL);
		cp2 = strdup(cp2);
		ASSERT_PTR_NE(cp2, NULL);
		ret[n++] = cp2;
	}
	free(ocp);
	*np = n;
	return ret;
}

static void
compare_opts(const struct sshauthopt *opts,
    const struct sshauthopt *expected)
{
	size_t i;

	ASSERT_PTR_NE(opts, NULL);
	ASSERT_PTR_NE(expected, NULL);
	ASSERT_PTR_NE(expected, opts); /* bozo :) */

#define FLAG_EQ(x) ASSERT_INT_EQ(opts->x, expected->x)
	FLAG_EQ(permit_port_forwarding_flag);
	FLAG_EQ(permit_agent_forwarding_flag);
	FLAG_EQ(permit_x11_forwarding_flag);
	FLAG_EQ(permit_pty_flag);
	FLAG_EQ(permit_user_rc);
	FLAG_EQ(restricted);
	FLAG_EQ(cert_authority);
#undef FLAG_EQ

#define STR_EQ(x) \
	do { \
		if (expected->x == NULL) \
			ASSERT_PTR_EQ(opts->x, expected->x); \
		else \
			ASSERT_STRING_EQ(opts->x, expected->x); \
	} while (0)
	STR_EQ(cert_principals);
	STR_EQ(force_command);
	STR_EQ(required_from_host_cert);
	STR_EQ(required_from_host_keys);
#undef STR_EQ

#define ARRAY_EQ(nx, x) \
	do { \
		ASSERT_SIZE_T_EQ(opts->nx, expected->nx); \
		if (expected->nx == 0) \
			break; \
		for (i = 0; i < expected->nx; i++) \
			ASSERT_STRING_EQ(opts->x[i], expected->x[i]); \
	} while (0)
	ARRAY_EQ(nenv, env);
	ARRAY_EQ(npermitopen, permitopen);
#undef ARRAY_EQ
}

static void
test_authkeys_parse(void)
{
	struct sshauthopt *opts, *expected;
	const char *errstr;

#define FAIL_TEST(label, keywords) \
	do { \
		TEST_START("sshauthopt_parse invalid " label); \
		opts = sshauthopt_parse(keywords, &errstr); \
		ASSERT_PTR_EQ(opts, NULL); \
		ASSERT_PTR_NE(errstr, NULL); \
		TEST_DONE(); \
	} while (0) 
#define CHECK_SUCCESS_AND_CLEANUP() \
	do { \
		if (errstr != NULL) \
			ASSERT_STRING_EQ(errstr, ""); \
		compare_opts(opts, expected); \
		sshauthopt_free(expected); \
		sshauthopt_free(opts); \
	} while (0)

	/* Basic tests */
	TEST_START("sshauthopt_parse empty");
	expected = default_authkey_opts();
	opts = sshauthopt_parse("", &errstr);
	CHECK_SUCCESS_AND_CLEANUP();
	TEST_DONE();

	TEST_START("sshauthopt_parse trailing whitespace");
	expected = default_authkey_opts();
	opts = sshauthopt_parse(" ", &errstr);
	CHECK_SUCCESS_AND_CLEANUP();
	TEST_DONE();

	TEST_START("sshauthopt_parse restrict");
	expected = default_authkey_restrict_opts();
	opts = sshauthopt_parse("restrict", &errstr);
	CHECK_SUCCESS_AND_CLEANUP();
	TEST_DONE();

	/* Invalid syntax */
	FAIL_TEST("trailing comma", "restrict,");
	FAIL_TEST("bare comma", ",");
	FAIL_TEST("unknown option", "BLAH");
	FAIL_TEST("unknown option with trailing comma", "BLAH,");
	FAIL_TEST("unknown option with trailing whitespace", "BLAH ");

	/* force_tun_device */
	TEST_START("sshauthopt_parse tunnel explicit");
	expected = default_authkey_opts();
	expected->force_tun_device = 1;
	opts = sshauthopt_parse("tunnel=\"1\"", &errstr);
	CHECK_SUCCESS_AND_CLEANUP();
	TEST_DONE();

	TEST_START("sshauthopt_parse tunnel any");
	expected = default_authkey_opts();
	expected->force_tun_device = SSH_TUNID_ANY;
	opts = sshauthopt_parse("tunnel=\"any\"", &errstr);
	CHECK_SUCCESS_AND_CLEANUP();
	TEST_DONE();

	FAIL_TEST("tunnel", "tunnel=\"blah\"");

	/* Flag options */
#define FLAG_TEST(keyword, var, val) \
	do { \
		TEST_START("sshauthopt_parse " keyword); \
		expected = default_authkey_opts(); \
		expected->var = val; \
		opts = sshauthopt_parse(keyword, &errstr); \
		CHECK_SUCCESS_AND_CLEANUP(); \
		expected = default_authkey_restrict_opts(); \
		expected->var = val; \
		opts = sshauthopt_parse("restrict,"keyword, &errstr); \
		CHECK_SUCCESS_AND_CLEANUP(); \
		TEST_DONE(); \
	} while (0)
	/* Positive flags */
	FLAG_TEST("cert-authority", cert_authority, 1);
	FLAG_TEST("port-forwarding", permit_port_forwarding_flag, 1);
	FLAG_TEST("agent-forwarding", permit_agent_forwarding_flag, 1);
	FLAG_TEST("x11-forwarding", permit_x11_forwarding_flag, 1);
	FLAG_TEST("pty", permit_pty_flag, 1);
	FLAG_TEST("user-rc", permit_user_rc, 1);
	/* Negative flags */
	FLAG_TEST("no-port-forwarding", permit_port_forwarding_flag, 0);
	FLAG_TEST("no-agent-forwarding", permit_agent_forwarding_flag, 0);
	FLAG_TEST("no-x11-forwarding", permit_x11_forwarding_flag, 0);
	FLAG_TEST("no-pty", permit_pty_flag, 0);
	FLAG_TEST("no-user-rc", permit_user_rc, 0);
#undef FLAG_TEST
	FAIL_TEST("no-cert-authority", "no-cert-authority");

	/* String options */
#define STRING_TEST(keyword, var, val) \
	do { \
		TEST_START("sshauthopt_parse " keyword); \
		expected = default_authkey_opts(); \
		expected->var = strdup(val); \
		ASSERT_PTR_NE(expected->var, NULL); \
		opts = sshauthopt_parse(keyword "=" #val, &errstr); \
		CHECK_SUCCESS_AND_CLEANUP(); \
		expected = default_authkey_restrict_opts(); \
		expected->var = strdup(val); \
		ASSERT_PTR_NE(expected->var, NULL); \
		opts = sshauthopt_parse( \
		    "restrict," keyword "=" #val ",restrict", &errstr); \
		CHECK_SUCCESS_AND_CLEANUP(); \
		TEST_DONE(); \
	} while (0)
	STRING_TEST("command", force_command, "/bin/true");
	STRING_TEST("principals", cert_principals, "gregor,josef,K");
	STRING_TEST("from", required_from_host_keys, "127.0.0.0/8");
#undef STRING_TEST
	FAIL_TEST("unquoted command", "command=oops");
	FAIL_TEST("unquoted principals", "principals=estragon");
	FAIL_TEST("unquoted from", "from=127.0.0.1");

	/* String array option tests */
#define ARRAY_TEST(label, keywords, var, nvar, val) \
	do { \
		TEST_START("sshauthopt_parse " label); \
		expected = default_authkey_opts(); \
		expected->var = commasplit(val, &expected->nvar); \
		ASSERT_PTR_NE(expected->var, NULL); \
		opts = sshauthopt_parse(keywords, &errstr); \
		CHECK_SUCCESS_AND_CLEANUP(); \
		expected = default_authkey_restrict_opts(); \
		expected->var = commasplit(val, &expected->nvar); \
		ASSERT_PTR_NE(expected->var, NULL); \
		opts = sshauthopt_parse( \
		    "restrict," keywords ",restrict", &errstr); \
		CHECK_SUCCESS_AND_CLEANUP(); \
		TEST_DONE(); \
	} while (0)
	ARRAY_TEST("environment", "environment=\"foo=1\",environment=\"bar=2\"",
	    env, nenv, "foo=1,bar=2");
	ARRAY_TEST("permitopen", "permitopen=\"foo:123\",permitopen=\"bar:*\"",
	    permitopen, npermitopen, "foo:123,bar:*");
#undef ARRAY_TEST
	FAIL_TEST("environment", "environment=\",=bah\"");
	FAIL_TEST("permitopen port", "foo:bar");
	FAIL_TEST("permitopen missing port", "foo:");
	FAIL_TEST("permitopen missing port specification", "foo");
	FAIL_TEST("permitopen invalid host", "[:");

#undef CHECK_SUCCESS_AND_CLEANUP
#undef FAIL_TEST
}

static void
test_cert_parse(void)
{
	struct sshkey *cert;
	struct sshauthopt *opts, *expected;

#define CHECK_SUCCESS_AND_CLEANUP() \
	do { \
		compare_opts(opts, expected); \
		sshauthopt_free(expected); \
		sshauthopt_free(opts); \
		sshkey_free(cert); \
	} while (0)
#define FLAG_TEST(keybase, var) \
	do { \
		TEST_START("sshauthopt_from_cert no_" keybase); \
		cert = load_key("no_" keybase ".cert"); \
		expected = default_authkey_opts(); \
		expected->var = 0; \
		opts = sshauthopt_from_cert(cert); \
		CHECK_SUCCESS_AND_CLEANUP(); \
		TEST_DONE(); \
		TEST_START("sshauthopt_from_cert only_" keybase); \
		cert = load_key("only_" keybase ".cert"); \
		expected = sshauthopt_new(); \
		ASSERT_PTR_NE(expected, NULL); \
		expected->var = 1; \
		opts = sshauthopt_from_cert(cert); \
		CHECK_SUCCESS_AND_CLEANUP(); \
		TEST_DONE(); \
	} while (0)
	FLAG_TEST("agentfwd", permit_agent_forwarding_flag);
	FLAG_TEST("portfwd", permit_port_forwarding_flag);
	FLAG_TEST("pty", permit_pty_flag);
	FLAG_TEST("user_rc", permit_user_rc);
	FLAG_TEST("x11fwd", permit_x11_forwarding_flag);
#undef FLAG_TEST

	TEST_START("sshauthopt_from_cert all permitted");
	cert = load_key("all_permit.cert");
	expected = default_authkey_opts();
	opts = sshauthopt_from_cert(cert);
	CHECK_SUCCESS_AND_CLEANUP();
	TEST_DONE();

	TEST_START("sshauthopt_from_cert nothing permitted");
	cert = load_key("no_permit.cert");
	expected = sshauthopt_new();
	ASSERT_PTR_NE(expected, NULL);
	opts = sshauthopt_from_cert(cert);
	CHECK_SUCCESS_AND_CLEANUP();
	TEST_DONE();

	TEST_START("sshauthopt_from_cert force-command");
	cert = load_key("force_command.cert");
	expected = default_authkey_opts();
	expected->force_command = strdup("foo");
	ASSERT_PTR_NE(expected->force_command, NULL);
	opts = sshauthopt_from_cert(cert);
	CHECK_SUCCESS_AND_CLEANUP();
	TEST_DONE();

	TEST_START("sshauthopt_from_cert source-address");
	cert = load_key("sourceaddr.cert");
	expected = default_authkey_opts();
	expected->required_from_host_cert = strdup("127.0.0.1/32,::1/128");
	ASSERT_PTR_NE(expected->required_from_host_cert, NULL);
	opts = sshauthopt_from_cert(cert);
	CHECK_SUCCESS_AND_CLEANUP();
	TEST_DONE();
#undef CHECK_SUCCESS_AND_CLEANUP

#define FAIL_TEST(keybase) \
	do { \
		TEST_START("sshauthopt_from_cert " keybase); \
		cert = load_key(keybase ".cert"); \
		opts = sshauthopt_from_cert(cert); \
		ASSERT_PTR_EQ(opts, NULL); \
		sshkey_free(cert); \
		TEST_DONE(); \
	} while (0)
	FAIL_TEST("host");
	FAIL_TEST("bad_sourceaddr");
	FAIL_TEST("unknown_critical");
#undef FAIL_TEST
}

static void
test_merge(void)
{
	struct sshkey *cert;
	struct sshauthopt *key_opts, *cert_opts, *merge_opts, *expected;
	const char *errstr;

	/*
	 * Prepare for a test by making some key and cert options and
	 * attempting to merge them.
	 */
#define PREPARE(label, keyname, keywords) \
	do { \
		expected = NULL; \
		TEST_START("sshauthopt_merge " label); \
		cert = load_key(keyname ".cert"); \
		cert_opts = sshauthopt_from_cert(cert); \
		ASSERT_PTR_NE(cert_opts, NULL); \
		key_opts = sshauthopt_parse(keywords, &errstr); \
		if (errstr != NULL) \
			ASSERT_STRING_EQ(errstr, ""); \
		ASSERT_PTR_NE(key_opts, NULL); \
		merge_opts = sshauthopt_merge(key_opts, \
		    cert_opts, &errstr); \
	} while (0)

	/* Cleanup stuff allocated by PREPARE() */
#define CLEANUP() \
	do { \
		sshauthopt_free(expected); \
		sshauthopt_free(merge_opts); \
		sshauthopt_free(key_opts); \
		sshauthopt_free(cert_opts); \
		sshkey_free(cert); \
	} while (0)

	/* Check the results of PREPARE() against expectation; calls CLEANUP */
#define CHECK_SUCCESS_AND_CLEANUP() \
	do { \
		if (errstr != NULL) \
			ASSERT_STRING_EQ(errstr, ""); \
		compare_opts(merge_opts, expected); \
		CLEANUP(); \
	} while (0)

	/* Check a single case of merging of flag options */
#define FLAG_CASE(keybase, label, keyname, keywords, mostly_off, var, val) \
	do { \
		PREPARE(keybase " " label, keyname, keywords); \
		expected = mostly_off ? \
		    sshauthopt_new() : default_authkey_opts(); \
		expected->var = val; \
		ASSERT_PTR_NE(expected, NULL); \
		CHECK_SUCCESS_AND_CLEANUP(); \
		TEST_DONE(); \
	} while (0)

	/*
	 * Fairly exhaustive exercise of a flag option. Tests
	 * option both set and clear in certificate, set and clear in
	 * authorized_keys and set and cleared via restrict keyword.
	 */
#define FLAG_TEST(keybase, keyword, var) \
	do { \
		FLAG_CASE(keybase, "keys:default,yes cert:default,no", \
		    "no_" keybase, keyword, 0, var, 0); \
		FLAG_CASE(keybase,"keys:-*,yes cert:default,no", \
		    "no_" keybase, "restrict," keyword, 1, var, 0); \
		FLAG_CASE(keybase, "keys:default,no cert:default,no", \
		    "no_" keybase, "no-" keyword, 0, var, 0); \
		FLAG_CASE(keybase, "keys:-*,no cert:default,no", \
		    "no_" keybase, "restrict,no-" keyword, 1, var, 0); \
		\
		FLAG_CASE(keybase, "keys:default,yes cert:-*,yes", \
		    "only_" keybase, keyword, 1, var, 1); \
		FLAG_CASE(keybase,"keys:-*,yes cert:-*,yes", \
		    "only_" keybase, "restrict," keyword, 1, var, 1); \
		FLAG_CASE(keybase, "keys:default,no cert:-*,yes", \
		    "only_" keybase, "no-" keyword, 1, var, 0); \
		FLAG_CASE(keybase, "keys:-*,no cert:-*,yes", \
		    "only_" keybase, "restrict,no-" keyword, 1, var, 0); \
		\
		FLAG_CASE(keybase, "keys:default,yes cert:-*", \
		    "no_permit", keyword, 1, var, 0); \
		FLAG_CASE(keybase,"keys:-*,yes cert:-*", \
		    "no_permit", "restrict," keyword, 1, var, 0); \
		FLAG_CASE(keybase, "keys:default,no cert:-*", \
		    "no_permit", "no-" keyword, 1, var, 0); \
		FLAG_CASE(keybase, "keys:-*,no cert:-*", \
		    "no_permit", "restrict,no-" keyword, 1, var, 0); \
		\
		FLAG_CASE(keybase, "keys:default,yes cert:*", \
		    "all_permit", keyword, 0, var, 1); \
		FLAG_CASE(keybase,"keys:-*,yes cert:*", \
		    "all_permit", "restrict," keyword, 1, var, 1); \
		FLAG_CASE(keybase, "keys:default,no cert:*", \
		    "all_permit", "no-" keyword, 0, var, 0); \
		FLAG_CASE(keybase, "keys:-*,no cert:*", \
		    "all_permit", "restrict,no-" keyword, 1, var, 0); \
		\
	} while (0)
	FLAG_TEST("portfwd", "port-forwarding", permit_port_forwarding_flag);
	FLAG_TEST("agentfwd", "agent-forwarding", permit_agent_forwarding_flag);
	FLAG_TEST("pty", "pty", permit_pty_flag);
	FLAG_TEST("user_rc", "user-rc", permit_user_rc);
	FLAG_TEST("x11fwd", "x11-forwarding", permit_x11_forwarding_flag);
#undef FLAG_TEST

	PREPARE("source-address both", "sourceaddr", "from=\"127.0.0.1\"");
	expected = default_authkey_opts();
	expected->required_from_host_cert = strdup("127.0.0.1/32,::1/128");
	ASSERT_PTR_NE(expected->required_from_host_cert, NULL);
	expected->required_from_host_keys = strdup("127.0.0.1");
	ASSERT_PTR_NE(expected->required_from_host_keys, NULL);
	CHECK_SUCCESS_AND_CLEANUP();
	TEST_DONE();

	PREPARE("source-address none", "all_permit", "");
	expected = default_authkey_opts();
	CHECK_SUCCESS_AND_CLEANUP();
	TEST_DONE();

	PREPARE("source-address keys", "all_permit", "from=\"127.0.0.1\"");
	expected = default_authkey_opts();
	expected->required_from_host_keys = strdup("127.0.0.1");
	ASSERT_PTR_NE(expected->required_from_host_keys, NULL);
	CHECK_SUCCESS_AND_CLEANUP();
	TEST_DONE();

	PREPARE("source-address cert", "sourceaddr", "");
	expected = default_authkey_opts();
	expected->required_from_host_cert = strdup("127.0.0.1/32,::1/128");
	ASSERT_PTR_NE(expected->required_from_host_cert, NULL);
	CHECK_SUCCESS_AND_CLEANUP();
	TEST_DONE();

	PREPARE("force-command both", "force_command", "command=\"foo\"");
	expected = default_authkey_opts();
	expected->force_command = strdup("foo");
	ASSERT_PTR_NE(expected->force_command, NULL);
	CHECK_SUCCESS_AND_CLEANUP();
	TEST_DONE();

	PREPARE("force-command none", "all_permit", "");
	expected = default_authkey_opts();
	CHECK_SUCCESS_AND_CLEANUP();
	TEST_DONE();

	PREPARE("force-command keys", "all_permit", "command=\"bar\"");
	expected = default_authkey_opts();
	expected->force_command = strdup("bar");
	ASSERT_PTR_NE(expected->force_command, NULL);
	CHECK_SUCCESS_AND_CLEANUP();
	TEST_DONE();

	PREPARE("force-command cert", "force_command", "");
	expected = default_authkey_opts();
	expected->force_command = strdup("foo");
	ASSERT_PTR_NE(expected->force_command, NULL);
	CHECK_SUCCESS_AND_CLEANUP();
	TEST_DONE();

	PREPARE("force-command mismatch", "force_command", "command=\"bar\"");
	ASSERT_PTR_EQ(merge_opts, NULL);
	CLEANUP();
	TEST_DONE();

	PREPARE("tunnel", "all_permit", "tunnel=\"6\"");
	expected = default_authkey_opts();
	expected->force_tun_device = 6;
	CHECK_SUCCESS_AND_CLEANUP();
	TEST_DONE();

	PREPARE("permitopen", "all_permit",
	    "permitopen=\"127.0.0.1:*\",permitopen=\"127.0.0.1:123\"");
	expected = default_authkey_opts();
	expected->permitopen = commasplit("127.0.0.1:*,127.0.0.1:123",
	    &expected->npermitopen);
	CHECK_SUCCESS_AND_CLEANUP();
	TEST_DONE();

	PREPARE("environment", "all_permit",
	    "environment=\"foo=a\",environment=\"bar=b\"");
	expected = default_authkey_opts();
	expected->env = commasplit("foo=a,bar=b", &expected->nenv);
	CHECK_SUCCESS_AND_CLEANUP();
	TEST_DONE();
}

void
tests(void)
{
	extern char *__progname;
	LogLevel ll = test_is_verbose() ?
	    SYSLOG_LEVEL_DEBUG3 : SYSLOG_LEVEL_QUIET;

	/* test_cert_parse() are a bit spammy to error() by default... */
	log_init(__progname, ll, SYSLOG_FACILITY_USER, 1);

	test_authkeys_parse();
	test_cert_parse();
	test_merge();
}
