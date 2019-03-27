/*
 * Copyright (c) 2004 Kungliga Tekniska Högskolan
 * (Royal Institute of Technology, Stockholm, Sweden).
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 *
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * 3. Neither the name of KTH nor the names of its contributors may be
 *    used to endorse or promote products derived from this software without
 *    specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY KTH AND ITS CONTRIBUTORS ``AS IS'' AND ANY
 * EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL KTH OR ITS CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR
 * BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
 * WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR
 * OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF
 * ADVISED OF THE POSSIBILITY OF SUCH DAMAGE. */

#include "krb5_locl.h"
#include <err.h>

#define RETVAL(c, r, e, s) \
	do { if (r != e) krb5_errx(c, 1, "%s", s); } while (0)
#define STRINGMATCH(c, s, _s1, _s2) \
	do {							\
		if (_s1 == NULL || _s2 == NULL) 		\
			krb5_errx(c, 1, "s1 or s2 is NULL");	\
		if (strcmp(_s1,_s2) != 0) 			\
			krb5_errx(c, 1, "%s", s);		\
	} while (0)

static void
test_match_string(krb5_context context)
{
    krb5_error_code ret;
    char *s1, *s2;

    ret = krb5_acl_match_string(context, "foo", "s", "foo");
    RETVAL(context, ret, 0, "single s");
    ret = krb5_acl_match_string(context, "foo foo", "s", "foo");
    RETVAL(context, ret, EACCES, "too many strings");
    ret = krb5_acl_match_string(context, "foo bar", "ss", "foo", "bar");
    RETVAL(context, ret, 0, "two strings");
    ret = krb5_acl_match_string(context, "foo  bar", "ss", "foo", "bar");
    RETVAL(context, ret, 0, "two strings double space");
    ret = krb5_acl_match_string(context, "foo \tbar", "ss", "foo", "bar");
    RETVAL(context, ret, 0, "two strings space + tab");
    ret = krb5_acl_match_string(context, "foo", "ss", "foo", "bar");
    RETVAL(context, ret, EACCES, "one string, two format strings");
    ret = krb5_acl_match_string(context, "foo", "ss", "foo", "foo");
    RETVAL(context, ret, EACCES, "one string, two format strings (same)");
    ret = krb5_acl_match_string(context, "foo  \t", "s", "foo");
    RETVAL(context, ret, 0, "ending space");

    ret = krb5_acl_match_string(context, "foo/bar", "f", "foo/bar");
    RETVAL(context, ret, 0, "liternal fnmatch");
    ret = krb5_acl_match_string(context, "foo/bar", "f", "foo/*");
    RETVAL(context, ret, 0, "foo/*");
    ret = krb5_acl_match_string(context, "foo/bar.example.org", "f",
				"foo/*.example.org");
    RETVAL(context, ret, 0, "foo/*.example.org");
    ret = krb5_acl_match_string(context, "foo/bar.example.com", "f",
				"foo/*.example.org");
    RETVAL(context, ret, EACCES, "foo/*.example.com");

    ret = krb5_acl_match_string(context, "foo/bar/baz", "f", "foo/*/baz");
    RETVAL(context, ret, 0, "foo/*/baz");

    ret = krb5_acl_match_string(context, "foo", "r", &s1);
    RETVAL(context, ret, 0, "ret 1");
    STRINGMATCH(context, "ret 1 match", s1, "foo"); free(s1);

    ret = krb5_acl_match_string(context, "foo bar", "rr", &s1, &s2);
    RETVAL(context, ret, 0, "ret 2");
    STRINGMATCH(context, "ret 2 match 1", s1, "foo"); free(s1);
    STRINGMATCH(context, "ret 2 match 2", s2, "bar"); free(s2);

    ret = krb5_acl_match_string(context, "foo bar", "sr", "bar", &s1);
    RETVAL(context, ret, EACCES, "ret mismatch");
    if (s1 != NULL) krb5_errx(context, 1, "s1 not NULL");

    ret = krb5_acl_match_string(context, "foo", "l", "foo");
    RETVAL(context, ret, EINVAL, "unknown letter");
}


int
main(int argc, char **argv)
{
    krb5_context context;
    krb5_error_code ret;

    setprogname(argv[0]);

    ret = krb5_init_context(&context);
    if (ret)
	errx (1, "krb5_init_context failed: %d", ret);

    test_match_string(context);

    krb5_free_context(context);

    return 0;
}
