/*	$NetBSD: t_refuse_opt.c,v 1.8 2017/01/13 21:30:41 christos Exp $ */

/*-
 * Copyright (c) 2016 The NetBSD Foundation, Inc.
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
 * PURPOSE ARE DISCLAIMED.	IN NO EVENT SHALL THE FOUNDATION OR CONTRIBUTORS
 * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */
#include <sys/cdefs.h>
__RCSID("$NetBSD: t_refuse_opt.c,v 1.8 2017/01/13 21:30:41 christos Exp $");

#define _KERNTYPES
#include <sys/types.h>

#include <atf-c.h>

#include <fuse.h>

#include "h_macros.h"

ATF_TC(t_fuse_opt_add_arg);
ATF_TC_HEAD(t_fuse_opt_add_arg, tc)
{
	atf_tc_set_md_var(tc, "descr", "Check that fuse_opt_add_arg(3) works");
}

ATF_TC_BODY(t_fuse_opt_add_arg, tc)
{
	struct fuse_args args = FUSE_ARGS_INIT(0, NULL);

	RZ(fuse_opt_add_arg(&args, "foo"));
	RZ(fuse_opt_add_arg(&args, "bar"));

	ATF_REQUIRE_EQ(args.argc, 2);
	ATF_CHECK_STREQ(args.argv[0], "foo");
	ATF_CHECK_STREQ(args.argv[1], "bar");
	ATF_CHECK(args.allocated != 0);
}

ATF_TC(t_fuse_opt_insert_arg);
ATF_TC_HEAD(t_fuse_opt_insert_arg, tc)
{
	atf_tc_set_md_var(tc, "descr", "Check that fuse_opt_insert_arg(3) works");
}

ATF_TC_BODY(t_fuse_opt_insert_arg, tc)
{
	struct fuse_args args = FUSE_ARGS_INIT(0, NULL);

	RZ(fuse_opt_insert_arg(&args, 0, "foo"));
	RZ(fuse_opt_insert_arg(&args, 0, "bar"));

	ATF_REQUIRE_EQ(args.argc, 2);
	ATF_CHECK_STREQ(args.argv[0], "bar");
	ATF_CHECK_STREQ(args.argv[1], "foo");
	ATF_CHECK(args.allocated != 0);
}

ATF_TC(t_fuse_opt_add_opt);
ATF_TC_HEAD(t_fuse_opt_add_opt, tc)
{
	atf_tc_set_md_var(tc, "descr", "Check that fuse_opt_add_opt(3) works");
}

ATF_TC_BODY(t_fuse_opt_add_opt, tc)
{
	char* opt = NULL;

	RZ(fuse_opt_add_opt(&opt, "fo\\o"));
	ATF_CHECK_STREQ(opt, "fo\\o");

	RZ(fuse_opt_add_opt(&opt, "ba,r"));
	ATF_CHECK_STREQ(opt, "fo\\o,ba,r");
}

ATF_TC(t_fuse_opt_add_opt_escaped);
ATF_TC_HEAD(t_fuse_opt_add_opt_escaped, tc)
{
	atf_tc_set_md_var(tc, "descr", "Check that fuse_opt_add_opt_escaped(3) works");
}

ATF_TC_BODY(t_fuse_opt_add_opt_escaped, tc)
{
	char* opt = NULL;

	RZ(fuse_opt_add_opt_escaped(&opt, "fo\\o"));
	ATF_CHECK_STREQ(opt, "fo\\\\o");

	RZ(fuse_opt_add_opt_escaped(&opt, "ba,r"));
	ATF_CHECK_STREQ(opt, "fo\\\\o,ba\\,r");
}

ATF_TC(t_fuse_opt_match);
ATF_TC_HEAD(t_fuse_opt_match, tc)
{
	atf_tc_set_md_var(tc, "descr", "Check that fuse_opt_match(3) works"
					  " for every form of templates");
}

ATF_TC_BODY(t_fuse_opt_match, tc)
{
	struct fuse_opt o1[] = { FUSE_OPT_KEY("-x"    , 0), FUSE_OPT_END };
	struct fuse_opt o2[] = { FUSE_OPT_KEY("foo"   , 0), FUSE_OPT_END };
	struct fuse_opt o3[] = { FUSE_OPT_KEY("foo="  , 0), FUSE_OPT_END };
	struct fuse_opt o4[] = { FUSE_OPT_KEY("foo=%s", 0), FUSE_OPT_END };
	struct fuse_opt o5[] = { FUSE_OPT_KEY("-x "   , 0), FUSE_OPT_END };
	struct fuse_opt o6[] = { FUSE_OPT_KEY("-x %s" , 0), FUSE_OPT_END };

	ATF_CHECK(fuse_opt_match(o1, "-x") == 1);
	ATF_CHECK(fuse_opt_match(o1,  "x") == 0);

	ATF_CHECK(fuse_opt_match(o2,  "foo") == 1);
	ATF_CHECK(fuse_opt_match(o2, "-foo") == 0);

	ATF_CHECK(fuse_opt_match(o3, "foo=bar") == 1);
	ATF_CHECK(fuse_opt_match(o3, "foo"    ) == 0);

	ATF_CHECK(fuse_opt_match(o4, "foo=bar") == 1);
	ATF_CHECK(fuse_opt_match(o4, "foo"    ) == 0);

	ATF_CHECK(fuse_opt_match(o5, "-xbar" ) == 1);
	ATF_CHECK(fuse_opt_match(o5, "-x"    ) == 1);
	ATF_CHECK(fuse_opt_match(o5, "-x=bar") == 1);
	ATF_CHECK(fuse_opt_match(o5, "bar"   ) == 0);

	ATF_CHECK(fuse_opt_match(o6, "-xbar" ) == 1);
	ATF_CHECK(fuse_opt_match(o6, "-x"    ) == 1);
	ATF_CHECK(fuse_opt_match(o6, "-x=bar") == 1);
	ATF_CHECK(fuse_opt_match(o6, "bar"   ) == 0);
}

struct foofs_config {
	int number;
	char *string;
	char* nonopt;
};

#define FOOFS_OPT(t, p, v) { t, offsetof(struct foofs_config, p), v }

static struct fuse_opt foofs_opts[] = {
	FOOFS_OPT("number=%i"     , number, 0),
	FOOFS_OPT("-n %i"         , number, 0),
	FOOFS_OPT("string=%s"     , string, 0),
	FOOFS_OPT("number1"       , number, 1),
	FOOFS_OPT("number2"       , number, 2),
	FOOFS_OPT("--number=three", number, 3),
	FOOFS_OPT("--number=four" , number, 4),
	FUSE_OPT_END
};

static int foo_opt_proc(void *data, const char *arg, int key, struct fuse_args *outargs) {
	struct foofs_config *config = data;

	if (key == FUSE_OPT_KEY_NONOPT && config->nonopt == NULL) {
		config->nonopt = strdup(arg);
		return 0;
	}
	else {
		return 1;
	}
}

ATF_TC(t_fuse_opt_parse_null_args);
ATF_TC_HEAD(t_fuse_opt_parse_null_args, tc)
{
	atf_tc_set_md_var(tc, "descr", "NULL args means an empty arguments vector");
}

ATF_TC_BODY(t_fuse_opt_parse_null_args, tc)
{
	struct foofs_config config;

	memset(&config, 0, sizeof(config));
	ATF_CHECK(fuse_opt_parse(NULL, &config, NULL, NULL) == 0);
	ATF_CHECK_EQ(config.number, 0);
	ATF_CHECK_EQ(config.string, NULL);
	ATF_CHECK_EQ(config.nonopt, NULL);
}

ATF_TC(t_fuse_opt_parse_null_opts);
ATF_TC_HEAD(t_fuse_opt_parse_null_opts, tc)
{
	atf_tc_set_md_var(tc, "descr", "NULL opts means an opts array which only has FUSE_OPT_END");
}

ATF_TC_BODY(t_fuse_opt_parse_null_opts, tc)
{
	struct fuse_args args = FUSE_ARGS_INIT(0, NULL);
	struct foofs_config config;

	RZ(fuse_opt_add_arg(&args, "foofs"));
	RZ(fuse_opt_add_arg(&args, "-o"));
	RZ(fuse_opt_add_arg(&args, "number=1,string=foo"));
	RZ(fuse_opt_add_arg(&args, "bar"));

	memset(&config, 0, sizeof(config));
	ATF_CHECK(fuse_opt_parse(&args, &config, NULL, NULL) == 0);
	ATF_CHECK_EQ(config.number, 0);
	ATF_CHECK_EQ(config.string, NULL);
	ATF_CHECK_EQ(config.nonopt, NULL);
	ATF_CHECK_EQ(args.argc, 4);
	ATF_CHECK_STREQ(args.argv[0], "foofs");
	ATF_CHECK_STREQ(args.argv[1], "-o");
	ATF_CHECK_STREQ(args.argv[2], "number=1,string=foo");
	ATF_CHECK_STREQ(args.argv[3], "bar");
}

ATF_TC(t_fuse_opt_parse_null_proc);
ATF_TC_HEAD(t_fuse_opt_parse_null_proc, tc)
{
	atf_tc_set_md_var(tc, "descr", "NULL proc means a processor function always returning 1,"
					  " i.e. keep the argument");
}

ATF_TC_BODY(t_fuse_opt_parse_null_proc, tc)
{
	struct fuse_args args = FUSE_ARGS_INIT(0, NULL);
	struct foofs_config config;

	RZ(fuse_opt_add_arg(&args, "foofs"));
	RZ(fuse_opt_add_arg(&args, "-o"));
	RZ(fuse_opt_add_arg(&args, "number=1,string=foo"));
	RZ(fuse_opt_add_arg(&args, "bar"));

	memset(&config, 0, sizeof(config));
	ATF_CHECK(fuse_opt_parse(&args, &config, foofs_opts, NULL) == 0);
	ATF_CHECK_EQ(config.number, 1);
	ATF_CHECK_STREQ(config.string, "foo");
	ATF_CHECK_EQ(config.nonopt, NULL);
	ATF_CHECK_EQ(args.argc, 2);
	ATF_CHECK_STREQ(args.argv[0], "foofs");
	ATF_CHECK_STREQ(args.argv[1], "bar");
}

ATF_TC(t_fuse_opt_parse);
ATF_TC_HEAD(t_fuse_opt_parse, tc)
{
	atf_tc_set_md_var(tc, "descr", "Check that fuse_opt_parse(3) fully works");
}

ATF_TC_BODY(t_fuse_opt_parse, tc)
{
	struct fuse_args args = FUSE_ARGS_INIT(0, NULL);
	struct foofs_config config;

    /* Standard form */
	fuse_opt_free_args(&args);
	RZ(fuse_opt_add_arg(&args, "foofs"));
	RZ(fuse_opt_add_arg(&args, "-o"));
	RZ(fuse_opt_add_arg(&args, "number=1,string=foo"));
	RZ(fuse_opt_add_arg(&args, "bar"));

	memset(&config, 0, sizeof(config));
	ATF_CHECK(fuse_opt_parse(&args, &config, foofs_opts, foo_opt_proc) == 0);
	ATF_CHECK_EQ(config.number, 1);
	ATF_CHECK_STREQ(config.string, "foo");
	ATF_CHECK_STREQ(config.nonopt, "bar");
	ATF_CHECK_EQ(args.argc, 1);
	ATF_CHECK_STREQ(args.argv[0], "foofs");

    /* Concatenated -o */
	fuse_opt_free_args(&args);
	RZ(fuse_opt_add_arg(&args, "foofs"));
	RZ(fuse_opt_add_arg(&args, "-onumber=1,unknown,string=foo"));
	RZ(fuse_opt_add_arg(&args, "bar"));

	memset(&config, 0, sizeof(config));
	ATF_CHECK(fuse_opt_parse(&args, &config, foofs_opts, foo_opt_proc) == 0);
	ATF_CHECK_EQ(config.number, 1);
	ATF_CHECK_STREQ(config.string, "foo");
	ATF_CHECK_STREQ(config.nonopt, "bar");
	ATF_CHECK_EQ(args.argc, 3);
	ATF_CHECK_STREQ(args.argv[0], "foofs");
	ATF_CHECK_STREQ(args.argv[1], "-o");
	ATF_CHECK_STREQ(args.argv[2], "unknown");

	/* Sparse -o */
	fuse_opt_free_args(&args);
	RZ(fuse_opt_add_arg(&args, "foofs"));
	RZ(fuse_opt_add_arg(&args, "bar"));
	RZ(fuse_opt_add_arg(&args, "baz"));
	RZ(fuse_opt_add_arg(&args, "-o"));
	RZ(fuse_opt_add_arg(&args, "number=1"));
	RZ(fuse_opt_add_arg(&args, "-o"));
	RZ(fuse_opt_add_arg(&args, "unknown"));
	RZ(fuse_opt_add_arg(&args, "-o"));
	RZ(fuse_opt_add_arg(&args, "string=foo"));

	memset(&config, 0, sizeof(config));
	ATF_CHECK(fuse_opt_parse(&args, &config, foofs_opts, foo_opt_proc) == 0);
	ATF_CHECK_EQ(config.number, 1);
	ATF_CHECK_STREQ(config.string, "foo");
	ATF_CHECK_STREQ(config.nonopt, "bar");
	ATF_CHECK_EQ(args.argc, 4);
	ATF_CHECK_STREQ(args.argv[0], "foofs");
	ATF_CHECK_STREQ(args.argv[1], "-o");
	ATF_CHECK_STREQ(args.argv[2], "unknown");
	ATF_CHECK_STREQ(args.argv[3], "baz");

	/* Separate -n %i */
	fuse_opt_free_args(&args);
	RZ(fuse_opt_add_arg(&args, "foofs"));
	RZ(fuse_opt_add_arg(&args, "-n"));
	RZ(fuse_opt_add_arg(&args, "3"));

	memset(&config, 0, sizeof(config));
	ATF_CHECK(fuse_opt_parse(&args, &config, foofs_opts, foo_opt_proc) == 0);
	ATF_CHECK_EQ(config.number, 3);
	ATF_CHECK_EQ(config.string, NULL);
	ATF_CHECK_EQ(config.nonopt, NULL);
	ATF_CHECK_EQ(args.argc, 1);
	ATF_CHECK_STREQ(args.argv[0], "foofs");

	/* Concatenated -n %i */
	fuse_opt_free_args(&args);
	RZ(fuse_opt_add_arg(&args, "foofs"));
	RZ(fuse_opt_add_arg(&args, "-n3"));

	memset(&config, 0, sizeof(config));
	ATF_CHECK(fuse_opt_parse(&args, &config, foofs_opts, foo_opt_proc) == 0);
	ATF_CHECK_EQ(config.number, 3);
	ATF_CHECK_EQ(config.string, NULL);
	ATF_CHECK_EQ(config.nonopt, NULL);
	ATF_CHECK_EQ(args.argc, 1);
	ATF_CHECK_STREQ(args.argv[0], "foofs");

	/* -o constant */
	fuse_opt_free_args(&args);
	RZ(fuse_opt_add_arg(&args, "foofs"));
	RZ(fuse_opt_add_arg(&args, "-o"));
	RZ(fuse_opt_add_arg(&args, "number2"));

	memset(&config, 0, sizeof(config));
	ATF_CHECK(fuse_opt_parse(&args, &config, foofs_opts, foo_opt_proc) == 0);
	ATF_CHECK_EQ(config.number, 2);
	ATF_CHECK_EQ(config.string, NULL);
	ATF_CHECK_EQ(config.nonopt, NULL);
	ATF_CHECK_EQ(args.argc, 1);
	ATF_CHECK_STREQ(args.argv[0], "foofs");

	/* -x constant */
	fuse_opt_free_args(&args);
	RZ(fuse_opt_add_arg(&args, "foofs"));
	RZ(fuse_opt_add_arg(&args, "--number=four"));

	memset(&config, 0, sizeof(config));
	ATF_CHECK(fuse_opt_parse(&args, &config, foofs_opts, foo_opt_proc) == 0);
	ATF_CHECK_EQ(config.number, 4);
	ATF_CHECK_EQ(config.string, NULL);
	ATF_CHECK_EQ(config.nonopt, NULL);
	ATF_CHECK_EQ(args.argc, 1);
	ATF_CHECK_STREQ(args.argv[0], "foofs");

	/* end-of-options "--" marker */
	fuse_opt_free_args(&args);
	RZ(fuse_opt_add_arg(&args, "foofs"));
    RZ(fuse_opt_add_arg(&args, "--"));
	RZ(fuse_opt_add_arg(&args, "-onumber=1"));
	RZ(fuse_opt_add_arg(&args, "-ostring=foo"));

	memset(&config, 0, sizeof(config));
	ATF_CHECK(fuse_opt_parse(&args, &config, foofs_opts, foo_opt_proc) == 0);
	ATF_CHECK_EQ(config.number, 0);
	ATF_CHECK_EQ(config.string, NULL);
	ATF_CHECK_STREQ(config.nonopt, "-onumber=1");
	ATF_CHECK_EQ(args.argc, 3);
	ATF_CHECK_STREQ(args.argv[0], "foofs");
	ATF_CHECK_STREQ(args.argv[1], "--");
	ATF_CHECK_STREQ(args.argv[2], "-ostring=foo");

	/* The "--" marker at the last of outargs should be removed */
	fuse_opt_free_args(&args);
	RZ(fuse_opt_add_arg(&args, "foofs"));
    RZ(fuse_opt_add_arg(&args, "--"));
	RZ(fuse_opt_add_arg(&args, "-onumber=1"));

	memset(&config, 0, sizeof(config));
	ATF_CHECK(fuse_opt_parse(&args, &config, foofs_opts, foo_opt_proc) == 0);
	ATF_CHECK_EQ(config.number, 0);
	ATF_CHECK_EQ(config.string, NULL);
	ATF_CHECK_STREQ(config.nonopt, "-onumber=1");
	ATF_CHECK_EQ(args.argc, 1);
	ATF_CHECK_STREQ(args.argv[0], "foofs");
}

ATF_TP_ADD_TCS(tp)
{
	ATF_TP_ADD_TC(tp, t_fuse_opt_add_arg);
	ATF_TP_ADD_TC(tp, t_fuse_opt_insert_arg);
	ATF_TP_ADD_TC(tp, t_fuse_opt_add_opt);
	ATF_TP_ADD_TC(tp, t_fuse_opt_add_opt_escaped);
	ATF_TP_ADD_TC(tp, t_fuse_opt_match);
	ATF_TP_ADD_TC(tp, t_fuse_opt_parse_null_args);
	ATF_TP_ADD_TC(tp, t_fuse_opt_parse_null_opts);
	ATF_TP_ADD_TC(tp, t_fuse_opt_parse_null_proc);
	ATF_TP_ADD_TC(tp, t_fuse_opt_parse);

	return atf_no_error();
}
