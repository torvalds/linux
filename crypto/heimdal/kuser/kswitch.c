/*
 * Copyright (c) 2008 - 2010 Kungliga Tekniska Högskolan
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
 * 3. Neither the name of the Institute nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE INSTITUTE AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE INSTITUTE OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#include "kuser_locl.h"
#include "kcc-commands.h"

#ifdef HAVE_READLINE
char *readline(const char *prompt);
#else

static char *
readline(const char *prompt)
{
    char buf[BUFSIZ];
    printf ("%s", prompt);
    fflush (stdout);
    if(fgets(buf, sizeof(buf), stdin) == NULL)
	return NULL;
    buf[strcspn(buf, "\r\n")] = '\0';
    return strdup(buf);
}

#endif

/*
 *
 */

int
kswitch(struct kswitch_options *opt, int argc, char **argv)
{
    krb5_error_code ret;
    krb5_ccache id = NULL;

    if (opt->cache_string && opt->principal_string)
	krb5_errx(kcc_context, 1,
		  N_("Both --cache and --principal given, choose one", ""));

    if (opt->interactive_flag) {
	krb5_cc_cache_cursor cursor;
	krb5_ccache *ids = NULL;
	size_t i, len = 0;
	char *name;
	rtbl_t ct;

	ct = rtbl_create();

	rtbl_add_column_by_id(ct, 0, "#", 0);
	rtbl_add_column_by_id(ct, 1, "Principal", 0);
	rtbl_set_column_affix_by_id(ct, 1, "    ", "");
        rtbl_add_column_by_id(ct, 2, "Type", 0);
        rtbl_set_column_affix_by_id(ct, 2, "  ", "");

	ret = krb5_cc_cache_get_first(kcc_context, NULL, &cursor);
	if (ret)
	    krb5_err(kcc_context, 1, ret, "krb5_cc_cache_get_first");

	while (krb5_cc_cache_next(kcc_context, cursor, &id) == 0) {
	    krb5_principal p;
	    char num[10];

	    ret = krb5_cc_get_principal(kcc_context, id, &p);
	    if (ret)
		continue;

	    ret = krb5_unparse_name(kcc_context, p, &name);
	    krb5_free_principal(kcc_context, p);

	    snprintf(num, sizeof(num), "%d", (int)(len + 1));
	    rtbl_add_column_entry_by_id(ct, 0, num);
	    rtbl_add_column_entry_by_id(ct, 1, name);
            rtbl_add_column_entry_by_id(ct, 2, krb5_cc_get_type(kcc_context, id));
	    free(name);

	    ids = erealloc(ids, (len + 1) * sizeof(ids[0]));
	    ids[len] = id;
	    len++;
	}
	krb5_cc_cache_end_seq_get(kcc_context, cursor);

	rtbl_format(ct, stdout);
	rtbl_destroy(ct);

	name = readline("Select number: ");
	if (name) {
	    i = atoi(name);
	    if (i == 0)
		krb5_errx(kcc_context, 1, "Cache number '%s' is invalid", name);
	    if (i > len)
		krb5_errx(kcc_context, 1, "Cache number '%s' is too large", name);

	    id = ids[i - 1];
	    ids[i - 1] = NULL;
	} else
	    krb5_errx(kcc_context, 1, "No cache selected");
	for (i = 0; i < len; i++)
	    if (ids[i])
		krb5_cc_close(kcc_context, ids[i]);

    } else if (opt->principal_string) {
	krb5_principal p;

	ret = krb5_parse_name(kcc_context, opt->principal_string, &p);
	if (ret)
	    krb5_err(kcc_context, 1, ret, "krb5_parse_name: %s",
		     opt->principal_string);

	ret = krb5_cc_cache_match(kcc_context, p, &id);
	if (ret)
	    krb5_err(kcc_context, 1, ret,
		     N_("Did not find principal: %s", ""),
		     opt->principal_string);

	krb5_free_principal(kcc_context, p);

    } else if (opt->cache_string) {
	const krb5_cc_ops *ops;
	char *str;

	ops = krb5_cc_get_prefix_ops(kcc_context, opt->type_string);
	if (ops == NULL)
	    krb5_err(kcc_context, 1, 0, "krb5_cc_get_prefix_ops");

	asprintf(&str, "%s:%s", ops->prefix, opt->cache_string);
	if (str == NULL)
	    krb5_errx(kcc_context, 1, N_("out of memory", ""));

	ret = krb5_cc_resolve(kcc_context, str, &id);
	if (ret)
	    krb5_err(kcc_context, 1, ret, "krb5_cc_resolve: %s", str);

	free(str);
    } else {
	krb5_errx(kcc_context, 1, "missing option for kswitch");
    }

    ret = krb5_cc_switch(kcc_context, id);
    if (ret)
	krb5_err(kcc_context, 1, ret, "krb5_cc_switch");

    return 0;
}
