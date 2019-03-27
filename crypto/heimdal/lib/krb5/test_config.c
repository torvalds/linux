/*
 * Copyright (c) 2003 Kungliga Tekniska Högskolan
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
 * ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include "krb5_locl.h"
#include <err.h>

static int
check_config_file(krb5_context context, char *filelist, char **res, int def)
{
    krb5_error_code ret;
    char **pp;
    int i;

    pp = NULL;

    if (def)
	ret = krb5_prepend_config_files_default(filelist, &pp);
    else
	ret = krb5_prepend_config_files(filelist, NULL, &pp);

    if (ret)
	krb5_err(context, 1, ret, "prepend_config_files");

    for (i = 0; res[i] && pp[i]; i++)
	if (strcmp(pp[i], res[i]) != 0)
	    krb5_errx(context, 1, "'%s' != '%s'", pp[i], res[i]);

    if (res[i] != NULL)
	krb5_errx(context, 1, "pp ended before res list");

    if (def) {
	char **deflist;
	int j;

	ret = krb5_get_default_config_files(&deflist);
	if (ret)
	    krb5_err(context, 1, ret, "get_default_config_files");

	for (j = 0 ; pp[i] && deflist[j]; i++, j++)
	    if (strcmp(pp[i], deflist[j]) != 0)
		krb5_errx(context, 1, "'%s' != '%s'", pp[i], deflist[j]);

	if (deflist[j] != NULL)
	    krb5_errx(context, 1, "pp ended before def list");
	krb5_free_config_files(deflist);
    }

    if (pp[i] != NULL)
	krb5_errx(context, 1, "pp ended after res (and def) list");

    krb5_free_config_files(pp);

    return 0;
}

char *list0[] =  { "/tmp/foo", NULL };
char *list1[] =  { "/tmp/foo", "/tmp/foo/bar", NULL };
char *list2[] =  { "", NULL };

struct {
    char *fl;
    char **res;
} test[] = {
    { "/tmp/foo", NULL },
    { "/tmp/foo" PATH_SEP "/tmp/foo/bar", NULL },
    { "", NULL }
};

static void
check_config_files(void)
{
    krb5_context context;
    krb5_error_code ret;
    int i;

    ret = krb5_init_context(&context);
    if (ret)
	errx(1, "krb5_init_context %d", ret);

    test[0].res = list0;
    test[1].res = list1;
    test[2].res = list2;

    for (i = 0; i < sizeof(test)/sizeof(*test); i++) {
	check_config_file(context, test[i].fl, test[i].res, 0);
	check_config_file(context, test[i].fl, test[i].res, 1);
    }

    krb5_free_context(context);
}

const char *config_string_result0[] = {
    "A", "B", "C", "D", NULL
};

const char *config_string_result1[] = {
    "A", "B", "C D", NULL
};

const char *config_string_result2[] = {
    "A", "B", "", NULL
};

const char *config_string_result3[] = {
    "A B;C: D", NULL
};

const char *config_string_result4[] = {
    "\"\"", "", "\"\"", NULL
};

const char *config_string_result5[] = {
    "A\"BQd", NULL
};

const char *config_string_result6[] = {
    "efgh\"", "ABC", NULL
};

const char *config_string_result7[] = {
    "SnapeKills\\", "Dumbledore", NULL
};

const char *config_string_result8[] = {
    "\"TownOf Sandwich: Massachusetts\"Oldest", "Town", "In", "Cape Cod", NULL
};

const char *config_string_result9[] = {
    "\"Begins and\"ends", "In", "One", "String", NULL
};

const char *config_string_result10[] = {
    "Redistribution and use in source and binary forms, with or without modification, are permitted provided that the following conditions are met:",
    "1. Redistributions of source code must retain the above copyright notice, this list of conditions and the following disclaimer.",
    "2. Redistributions in binary form must reproduce the above copyright notice, this list of conditions and the following disclaimer in the documentation and/or other materials provided with the distribution.",
    "3. Neither the name of the Institute nor the names of its contributors may be used to endorse or promote products derived from this software without specific prior written permission.",
    "THIS SOFTWARE IS PROVIDED BY THE INSTITUTE AND CONTRIBUTORS ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE INSTITUTE OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.",
    "Why do we test with such long strings? Because some people have config files",
    "That", "look", "Like this.", NULL
};

const struct {
    const char * name;
    const char ** expected;
} config_strings_tests[] = {
    { "foo", config_string_result0 },
    { "bar", config_string_result1 },
    { "baz", config_string_result2 },
    { "quux", config_string_result3 },
    { "questionable", config_string_result4 },
    { "mismatch1", config_string_result5 },
    { "mismatch2", config_string_result6 },
    { "internal1", config_string_result7 },
    { "internal2", config_string_result8 },
    { "internal3", config_string_result9 },
    { "longer_strings", config_string_result10 }
};

static void
check_escaped_strings(void)
{
    krb5_context context;
    krb5_config_section *c = NULL;
    krb5_error_code ret;
    int i;

    ret = krb5_init_context(&context);
    if (ret)
        errx(1, "krb5_init_context %d", ret);

    ret = krb5_config_parse_file(context, "test_config_strings.out", &c);
    if (ret)
        krb5_errx(context, 1, "krb5_config_parse_file()");

    for (i=0; i < sizeof(config_strings_tests)/sizeof(config_strings_tests[0]); i++) {
        char **ps;
        const char **s;
        const char **e;

        ps = krb5_config_get_strings(context, c, "escapes", config_strings_tests[i].name,
                                     NULL);
        if (ps == NULL)
            errx(1, "Failed to read string value %s", config_strings_tests[i].name);

        e = config_strings_tests[i].expected;

        for (s = (const char **)ps; *s && *e; s++, e++) {
            if (strcmp(*s, *e))
                errx(1,
                     "Unexpected configuration string at value [%s].\n"
                     "Actual=[%s]\n"
                     "Expected=[%s]\n",
                     config_strings_tests[i].name, *s, *e);
        }

        if (*s || *e)
            errx(1, "Configuation string list for value [%s] has incorrect length.",
		 config_strings_tests[i].name);

        krb5_config_free_strings(ps);
    }

    ret = krb5_config_file_free(context, c);
    if (ret)
        krb5_errx(context, 1, "krb5_config_file_free()");

    krb5_free_context(context);
}

int
main(int argc, char **argv)
{
    check_config_files();
    check_escaped_strings();
    return 0;
}
