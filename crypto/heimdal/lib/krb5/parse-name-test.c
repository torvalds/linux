/*
 * Copyright (c) 2002 Kungliga Tekniska Högskolan
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

enum { MAX_COMPONENTS = 3 };

static struct testcase {
    const char *input_string;
    const char *output_string;
    krb5_realm realm;
    unsigned ncomponents;
    char *comp_val[MAX_COMPONENTS];
    int realmp;
} tests[] = {
    {"", "@", "", 1, {""}, FALSE},
    {"a", "a@", "", 1, {"a"}, FALSE},
    {"\\n", "\\n@", "", 1, {"\n"}, FALSE},
    {"\\ ", "\\ @", "", 1, {" "}, FALSE},
    {"\\t", "\\t@", "", 1, {"\t"}, FALSE},
    {"\\b", "\\b@", "", 1, {"\b"}, FALSE},
    {"\\\\", "\\\\@", "", 1, {"\\"}, FALSE},
    {"\\/", "\\/@", "", 1, {"/"}, FALSE},
    {"\\@", "\\@@", "", 1, {"@"}, FALSE},
    {"@", "@", "", 1, {""}, TRUE},
    {"a/b", "a/b@", "", 2, {"a", "b"}, FALSE},
    {"a/", "a/@", "", 2, {"a", ""}, FALSE},
    {"a\\//\\/", "a\\//\\/@", "", 2, {"a/", "/"}, FALSE},
    {"/a", "/a@", "", 2, {"", "a"}, FALSE},
    {"\\@@\\@", "\\@@\\@", "@", 1, {"@"}, TRUE},
    {"a/b/c", "a/b/c@", "", 3, {"a", "b", "c"}, FALSE},
    {NULL, NULL, "", 0, { NULL }, FALSE}};

int
main(int argc, char **argv)
{
    struct testcase *t;
    krb5_context context;
    krb5_error_code ret;
    int val = 0;

    ret = krb5_init_context (&context);
    if (ret)
	errx (1, "krb5_init_context failed: %d", ret);

    /* to enable realm-less principal name above */

    krb5_set_default_realm(context, "");

    for (t = tests; t->input_string; ++t) {
	krb5_principal princ;
	int i, j;
	char name_buf[1024];
	char *s;

	ret = krb5_parse_name(context, t->input_string, &princ);
	if (ret)
	    krb5_err (context, 1, ret, "krb5_parse_name %s",
		      t->input_string);
	if (strcmp (t->realm, princ->realm) != 0) {
	    printf ("wrong realm (\"%s\" should be \"%s\")"
		    " for \"%s\"\n",
		    princ->realm, t->realm,
		    t->input_string);
	    val = 1;
	}

	if (t->ncomponents != princ->name.name_string.len) {
	    printf ("wrong number of components (%u should be %u)"
		    " for \"%s\"\n",
		    princ->name.name_string.len, t->ncomponents,
		    t->input_string);
	    val = 1;
	} else {
	    for (i = 0; i < t->ncomponents; ++i) {
		if (strcmp(t->comp_val[i],
			   princ->name.name_string.val[i]) != 0) {
		    printf ("bad component %d (\"%s\" should be \"%s\")"
			    " for \"%s\"\n",
			    i,
			    princ->name.name_string.val[i],
			    t->comp_val[i],
			    t->input_string);
		    val = 1;
		}
	    }
	}
	for (j = 0; j < strlen(t->output_string); ++j) {
	    ret = krb5_unparse_name_fixed(context, princ,
					  name_buf, j);
	    if (ret != ERANGE) {
		printf ("unparse_name %s with length %d should have failed\n",
			t->input_string, j);
		val = 1;
		break;
	    }
	}
	ret = krb5_unparse_name_fixed(context, princ,
				      name_buf, sizeof(name_buf));
	if (ret)
	    krb5_err (context, 1, ret, "krb5_unparse_name_fixed");

	if (strcmp (t->output_string, name_buf) != 0) {
	    printf ("failed comparing the re-parsed"
		    " (\"%s\" should be \"%s\")\n",
		    name_buf, t->output_string);
	    val = 1;
	}

	ret = krb5_unparse_name(context, princ, &s);
	if (ret)
	    krb5_err (context, 1, ret, "krb5_unparse_name");

	if (strcmp (t->output_string, s) != 0) {
	    printf ("failed comparing the re-parsed"
		    " (\"%s\" should be \"%s\"\n",
		    s, t->output_string);
	    val = 1;
	}
	free(s);

	if (!t->realmp) {
	    for (j = 0; j < strlen(t->input_string); ++j) {
		ret = krb5_unparse_name_fixed_short(context, princ,
						    name_buf, j);
		if (ret != ERANGE) {
		    printf ("unparse_name_short %s with length %d"
			    " should have failed\n",
			    t->input_string, j);
		    val = 1;
		    break;
		}
	    }
	    ret = krb5_unparse_name_fixed_short(context, princ,
						name_buf, sizeof(name_buf));
	    if (ret)
		krb5_err (context, 1, ret, "krb5_unparse_name_fixed");

	    if (strcmp (t->input_string, name_buf) != 0) {
		printf ("failed comparing the re-parsed"
			" (\"%s\" should be \"%s\")\n",
			name_buf, t->input_string);
		val = 1;
	    }

	    ret = krb5_unparse_name_short(context, princ, &s);
	    if (ret)
		krb5_err (context, 1, ret, "krb5_unparse_name_short");

	    if (strcmp (t->input_string, s) != 0) {
		printf ("failed comparing the re-parsed"
			" (\"%s\" should be \"%s\"\n",
			s, t->input_string);
		val = 1;
	    }
	    free(s);
	}
	krb5_free_principal (context, princ);
    }
    krb5_free_context(context);
    return val;
}
