/*
 * Copyright (c) 1997-2008 Kungliga Tekniska Högskolan
 * (Royal Institute of Technology, Stockholm, Sweden).
 * All rights reserved.
 *
 * Portions Copyright (c) 2009 Apple Inc. All rights reserved.
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
#include "rtbl.h"
#include "parse_units.h"
#include "kcc-commands.h"

static char*
printable_time_internal(time_t t, int x)
{
    static char s[128];
    char *p;

    if ((p = ctime(&t)) == NULL)
	strlcpy(s, "?", sizeof(s));
    else
	strlcpy(s, p + 4, sizeof(s));
    s[x] = 0;
    return s;
}

static char*
printable_time(time_t t)
{
    return printable_time_internal(t, 20);
}

static char*
printable_time_long(time_t t)
{
    return printable_time_internal(t, 20);
}

#define COL_ISSUED		NP_("  Issued","")
#define COL_EXPIRES		NP_("  Expires", "")
#define COL_FLAGS		NP_("Flags", "")
#define COL_NAME		NP_("  Name", "")
#define COL_PRINCIPAL		NP_("  Principal", "in klist output")
#define COL_PRINCIPAL_KVNO	NP_("  Principal (kvno)", "in klist output")
#define COL_CACHENAME		NP_("  Cache name", "name in klist output")
#define COL_DEFCACHE		NP_("", "")

static void
print_cred(krb5_context context, krb5_creds *cred, rtbl_t ct, int do_flags)
{
    char *str;
    krb5_error_code ret;
    krb5_timestamp sec;

    krb5_timeofday (context, &sec);


    if(cred->times.starttime)
	rtbl_add_column_entry(ct, COL_ISSUED,
			      printable_time(cred->times.starttime));
    else
	rtbl_add_column_entry(ct, COL_ISSUED,
			      printable_time(cred->times.authtime));

    if(cred->times.endtime > sec)
	rtbl_add_column_entry(ct, COL_EXPIRES,
			      printable_time(cred->times.endtime));
    else
	rtbl_add_column_entry(ct, COL_EXPIRES, N_(">>>Expired<<<", ""));
    ret = krb5_unparse_name (context, cred->server, &str);
    if (ret)
	krb5_err(context, 1, ret, "krb5_unparse_name");
    rtbl_add_column_entry(ct, COL_PRINCIPAL, str);
    if(do_flags) {
	char s[16], *sp = s;
	if(cred->flags.b.forwardable)
	    *sp++ = 'F';
	if(cred->flags.b.forwarded)
	    *sp++ = 'f';
	if(cred->flags.b.proxiable)
	    *sp++ = 'P';
	if(cred->flags.b.proxy)
	    *sp++ = 'p';
	if(cred->flags.b.may_postdate)
	    *sp++ = 'D';
	if(cred->flags.b.postdated)
	    *sp++ = 'd';
	if(cred->flags.b.renewable)
	    *sp++ = 'R';
	if(cred->flags.b.initial)
	    *sp++ = 'I';
	if(cred->flags.b.invalid)
	    *sp++ = 'i';
	if(cred->flags.b.pre_authent)
	    *sp++ = 'A';
	if(cred->flags.b.hw_authent)
	    *sp++ = 'H';
	*sp = '\0';
	rtbl_add_column_entry(ct, COL_FLAGS, s);
    }
    free(str);
}

static void
print_cred_verbose(krb5_context context, krb5_creds *cred)
{
    size_t j;
    char *str;
    krb5_error_code ret;
    krb5_timestamp sec;

    krb5_timeofday (context, &sec);

    ret = krb5_unparse_name(context, cred->server, &str);
    if(ret)
	exit(1);
    printf(N_("Server: %s\n", ""), str);
    free (str);

    ret = krb5_unparse_name(context, cred->client, &str);
    if(ret)
	exit(1);
    printf(N_("Client: %s\n", ""), str);
    free (str);

    {
	Ticket t;
	size_t len;
	char *s;

	decode_Ticket(cred->ticket.data, cred->ticket.length, &t, &len);
	ret = krb5_enctype_to_string(context, t.enc_part.etype, &s);
	printf(N_("Ticket etype: ", ""));
	if (ret == 0) {
	    printf("%s", s);
	    free(s);
	} else {
	    printf(N_("unknown-enctype(%d)", ""), t.enc_part.etype);
	}
	if(t.enc_part.kvno)
	    printf(N_(", kvno %d", ""), *t.enc_part.kvno);
	printf("\n");
	if(cred->session.keytype != t.enc_part.etype) {
	    ret = krb5_enctype_to_string(context, cred->session.keytype, &str);
	    if(ret)
		krb5_warn(context, ret, "session keytype");
	    else {
		printf(N_("Session key: %s\n", "enctype"), str);
		free(str);
	    }
	}
	free_Ticket(&t);
	printf(N_("Ticket length: %lu\n", ""),
	       (unsigned long)cred->ticket.length);
    }
    printf(N_("Auth time:  %s\n", ""),
	   printable_time_long(cred->times.authtime));
    if(cred->times.authtime != cred->times.starttime)
	printf(N_("Start time: %s\n", ""),
	       printable_time_long(cred->times.starttime));
    printf(N_("End time:   %s", ""),
	   printable_time_long(cred->times.endtime));
    if(sec > cred->times.endtime)
	printf(N_(" (expired)", ""));
    printf("\n");
    if(cred->flags.b.renewable)
	printf(N_("Renew till: %s\n", ""),
	       printable_time_long(cred->times.renew_till));
    {
	char flags[1024];
	unparse_flags(TicketFlags2int(cred->flags.b),
		      asn1_TicketFlags_units(),
		      flags, sizeof(flags));
	printf(N_("Ticket flags: %s\n", ""), flags);
    }
    printf(N_("Addresses: ", ""));
    if (cred->addresses.len != 0) {
	for(j = 0; j < cred->addresses.len; j++){
	    char buf[128];
	    size_t len;
	    if(j) printf(", ");
	    ret = krb5_print_address(&cred->addresses.val[j],
				     buf, sizeof(buf), &len);

	    if(ret == 0)
		printf("%s", buf);
	}
    } else {
	printf(N_("addressless", ""));
    }
    printf("\n\n");
}

/*
 * Print all tickets in `ccache' on stdout, verbosily iff do_verbose.
 */

static void
print_tickets (krb5_context context,
	       krb5_ccache ccache,
	       krb5_principal principal,
	       int do_verbose,
	       int do_flags,
	       int do_hidden)
{
    krb5_error_code ret;
    char *str, *name;
    krb5_cc_cursor cursor;
    krb5_creds creds;
    krb5_deltat sec;

    rtbl_t ct = NULL;

    ret = krb5_unparse_name (context, principal, &str);
    if (ret)
	krb5_err (context, 1, ret, "krb5_unparse_name");

    printf ("%17s: %s:%s\n",
	    N_("Credentials cache", ""),
	    krb5_cc_get_type(context, ccache),
	    krb5_cc_get_name(context, ccache));
    printf ("%17s: %s\n", N_("Principal", ""), str);

    ret = krb5_cc_get_friendly_name(context, ccache, &name);
    if (ret == 0) {
	if (strcmp(name, str) != 0)
	    printf ("%17s: %s\n", N_("Friendly name", ""), name);
	free(name);
    }
    free (str);

    if(do_verbose) {
	printf ("%17s: %d\n", N_("Cache version", ""),
		krb5_cc_get_version(context, ccache));
    } else {
        krb5_cc_set_flags(context, ccache, KRB5_TC_NOTICKET);
    }

    ret = krb5_cc_get_kdc_offset(context, ccache, &sec);

    if (ret == 0 && do_verbose && sec != 0) {
	char buf[BUFSIZ];
	int val;
	int sig;

	val = sec;
	sig = 1;
	if (val < 0) {
	    sig = -1;
	    val = -val;
	}

	unparse_time (val, buf, sizeof(buf));

	printf ("%17s: %s%s\n", N_("KDC time offset", ""),
		sig == -1 ? "-" : "", buf);
    }

    printf("\n");

    ret = krb5_cc_start_seq_get (context, ccache, &cursor);
    if (ret)
	krb5_err(context, 1, ret, "krb5_cc_start_seq_get");

    if(!do_verbose) {
	ct = rtbl_create();
	rtbl_add_column(ct, COL_ISSUED, 0);
	rtbl_add_column(ct, COL_EXPIRES, 0);
	if(do_flags)
	    rtbl_add_column(ct, COL_FLAGS, 0);
	rtbl_add_column(ct, COL_PRINCIPAL, 0);
	rtbl_set_separator(ct, "  ");
    }
    while ((ret = krb5_cc_next_cred (context,
				     ccache,
				     &cursor,
				     &creds)) == 0) {
	if (!do_hidden && krb5_is_config_principal(context, creds.server)) {
	    ;
	}else if(do_verbose){
	    print_cred_verbose(context, &creds);
	}else{
	    print_cred(context, &creds, ct, do_flags);
	}
	krb5_free_cred_contents (context, &creds);
    }
    if(ret != KRB5_CC_END)
	krb5_err(context, 1, ret, "krb5_cc_get_next");
    ret = krb5_cc_end_seq_get (context, ccache, &cursor);
    if (ret)
	krb5_err (context, 1, ret, "krb5_cc_end_seq_get");
    if(!do_verbose) {
	rtbl_format(ct, stdout);
	rtbl_destroy(ct);
    }
}

/*
 * Check if there's a tgt for the realm of `principal' and ccache and
 * if so return 0, else 1
 */

static int
check_for_tgt (krb5_context context,
	       krb5_ccache ccache,
	       krb5_principal principal,
	       time_t *expiration)
{
    krb5_error_code ret;
    krb5_creds pattern;
    krb5_creds creds;
    krb5_const_realm client_realm;
    int expired;

    krb5_cc_clear_mcred(&pattern);

    client_realm = krb5_principal_get_realm(context, principal);

    ret = krb5_make_principal (context, &pattern.server,
			       client_realm, KRB5_TGS_NAME, client_realm, NULL);
    if (ret)
	krb5_err (context, 1, ret, "krb5_make_principal");
    pattern.client = principal;

    ret = krb5_cc_retrieve_cred (context, ccache, 0, &pattern, &creds);
    krb5_free_principal (context, pattern.server);
    if (ret) {
	if (ret == KRB5_CC_END)
	    return 1;
	krb5_err (context, 1, ret, "krb5_cc_retrieve_cred");
    }

    expired = time(NULL) > creds.times.endtime;

    if (expiration)
	*expiration = creds.times.endtime;

    krb5_free_cred_contents (context, &creds);

    return expired;
}

/*
 * Print a list of all AFS tokens
 */

#ifndef NO_AFS

static void
display_tokens(int do_verbose)
{
    uint32_t i;
    unsigned char t[4096];
    struct ViceIoctl parms;

    parms.in = (void *)&i;
    parms.in_size = sizeof(i);
    parms.out = (void *)t;
    parms.out_size = sizeof(t);

    for (i = 0;; i++) {
        int32_t size_secret_tok, size_public_tok;
        unsigned char *cell;
	struct ClearToken ct;
	unsigned char *r = t;
	struct timeval tv;
	char buf1[20], buf2[20];

	if(k_pioctl(NULL, VIOCGETTOK, &parms, 0) < 0) {
	    if(errno == EDOM)
		break;
	    continue;
	}
	if(parms.out_size > sizeof(t))
	    continue;
	if(parms.out_size < sizeof(size_secret_tok))
	    continue;
	t[min(parms.out_size,sizeof(t)-1)] = 0;
	memcpy(&size_secret_tok, r, sizeof(size_secret_tok));
	/* dont bother about the secret token */
	r += size_secret_tok + sizeof(size_secret_tok);
	if (parms.out_size < (r - t) + sizeof(size_public_tok))
	    continue;
	memcpy(&size_public_tok, r, sizeof(size_public_tok));
	r += sizeof(size_public_tok);
	if (parms.out_size < (r - t) + size_public_tok + sizeof(int32_t))
	    continue;
	memcpy(&ct, r, size_public_tok);
	r += size_public_tok;
	/* there is a int32_t with length of cellname, but we dont read it */
	r += sizeof(int32_t);
	cell = r;

	gettimeofday (&tv, NULL);
	strlcpy (buf1, printable_time(ct.BeginTimestamp),
		 sizeof(buf1));
	if (do_verbose || tv.tv_sec < ct.EndTimestamp)
	    strlcpy (buf2, printable_time(ct.EndTimestamp),
		     sizeof(buf2));
	else
	    strlcpy (buf2, N_(">>> Expired <<<", ""), sizeof(buf2));

	printf("%s  %s  ", buf1, buf2);

	if ((ct.EndTimestamp - ct.BeginTimestamp) & 1)
	    printf(N_("User's (AFS ID %d) tokens for %s", ""), ct.ViceId, cell);
	else
	    printf(N_("Tokens for %s", ""), cell);
	if (do_verbose)
	    printf(" (%d)", ct.AuthHandle);
	putchar('\n');
    }
}
#endif

/*
 * display the ccache in `cred_cache'
 */

static int
display_v5_ccache (krb5_context context, krb5_ccache ccache,
		   int do_test, int do_verbose,
		   int do_flags, int do_hidden)
{
    krb5_error_code ret;
    krb5_principal principal;
    int exit_status = 0;


    ret = krb5_cc_get_principal (context, ccache, &principal);
    if (ret) {
	if(ret == ENOENT) {
	    if (!do_test)
		krb5_warnx(context, N_("No ticket file: %s", ""),
			   krb5_cc_get_name(context, ccache));
	    return 1;
	} else
	    krb5_err (context, 1, ret, "krb5_cc_get_principal");
    }
    if (do_test)
	exit_status = check_for_tgt (context, ccache, principal, NULL);
    else
	print_tickets (context, ccache, principal, do_verbose,
		       do_flags, do_hidden);

    ret = krb5_cc_close (context, ccache);
    if (ret)
	krb5_err (context, 1, ret, "krb5_cc_close");

    krb5_free_principal (context, principal);

    return exit_status;
}

/*
 *
 */

static int
list_caches(krb5_context context)
{
    krb5_cc_cache_cursor cursor;
    const char *cdef_name;
    char *def_name;
    krb5_error_code ret;
    krb5_ccache id;
    rtbl_t ct;

    cdef_name = krb5_cc_default_name(context);
    if (cdef_name == NULL)
	krb5_errx(context, 1, "krb5_cc_default_name");
    def_name = strdup(cdef_name);

    ret = krb5_cc_cache_get_first (context, NULL, &cursor);
    if (ret == KRB5_CC_NOSUPP)
	return 0;
    else if (ret)
	krb5_err (context, 1, ret, "krb5_cc_cache_get_first");

    ct = rtbl_create();
    rtbl_add_column(ct, COL_NAME, 0);
    rtbl_add_column(ct, COL_CACHENAME, 0);
    rtbl_add_column(ct, COL_EXPIRES, 0);
    rtbl_add_column(ct, COL_DEFCACHE, 0);
    rtbl_set_prefix(ct, "   ");
    rtbl_set_column_prefix(ct, COL_NAME, "");

    while (krb5_cc_cache_next (context, cursor, &id) == 0) {
	krb5_principal principal = NULL;
	int expired = 0;
	char *name;
	time_t t;

	ret = krb5_cc_get_principal(context, id, &principal);
	if (ret)
	    continue;

	expired = check_for_tgt (context, id, principal, &t);

	ret = krb5_cc_get_friendly_name(context, id, &name);
	if (ret == 0) {
	    const char *str;
	    char *fname;
	    rtbl_add_column_entry(ct, COL_NAME, name);
	    rtbl_add_column_entry(ct, COL_CACHENAME,
				  krb5_cc_get_name(context, id));
	    if (expired)
		str = N_(">>> Expired <<<", "");
	    else
		str = printable_time(t);
	    rtbl_add_column_entry(ct, COL_EXPIRES, str);
	    free(name);

	    ret = krb5_cc_get_full_name(context, id, &fname);
	    if (ret)
		krb5_err (context, 1, ret, "krb5_cc_get_full_name");

	    if (strcmp(fname, def_name) == 0)
		rtbl_add_column_entry(ct, COL_DEFCACHE, "*");
	    else
		rtbl_add_column_entry(ct, COL_DEFCACHE, "");

	    krb5_xfree(fname);
	}
	krb5_cc_close(context, id);

	krb5_free_principal(context, principal);
    }

    krb5_cc_cache_end_seq_get(context, cursor);

    free(def_name);
    rtbl_format(ct, stdout);
    rtbl_destroy(ct);

    return 0;
}

/*
 *
 */

int
klist(struct klist_options *opt, int argc, char **argv)
{
    krb5_error_code ret;
    int exit_status = 0;

    int do_verbose =
	opt->verbose_flag ||
	opt->a_flag ||
	opt->n_flag;
    int do_test =
	opt->test_flag ||
	opt->s_flag;

    if (opt->list_all_flag) {
	exit_status = list_caches(kcc_context);
	return exit_status;
    }

    if (opt->v5_flag) {
	krb5_ccache id;

	if (opt->all_content_flag) {
	    krb5_cc_cache_cursor cursor;

	    ret = krb5_cc_cache_get_first(kcc_context, NULL, &cursor);
	    if (ret)
		krb5_err(kcc_context, 1, ret, "krb5_cc_cache_get_first");


	    while (krb5_cc_cache_next(kcc_context, cursor, &id) == 0) {
		exit_status |= display_v5_ccache(kcc_context, id, do_test,
						 do_verbose, opt->flags_flag,
						 opt->hidden_flag);
		printf("\n\n");
	    }
	    krb5_cc_cache_end_seq_get(kcc_context, cursor);

	} else {
	    if(opt->cache_string) {
		ret = krb5_cc_resolve(kcc_context, opt->cache_string, &id);
		if (ret)
		    krb5_err(kcc_context, 1, ret, "%s", opt->cache_string);
	    } else {
		ret = krb5_cc_default(kcc_context, &id);
		if (ret)
		    krb5_err(kcc_context, 1, ret, "krb5_cc_resolve");
	    }
	    exit_status = display_v5_ccache(kcc_context, id, do_test,
					    do_verbose, opt->flags_flag,
					    opt->hidden_flag);
	}
    }

    if (!do_test) {
#ifndef NO_AFS
	if (opt->tokens_flag && k_hasafs()) {
	    if (opt->v5_flag)
		printf("\n");
	    display_tokens(opt->verbose_flag);
	}
#endif
    }

    return exit_status;
}
