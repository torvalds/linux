/*
 * Copyright (c) 1997 - 2006 Kungliga Tekniska Högskolan
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

#include "kadmin_locl.h"
#include <parse_units.h>

/*
 * util.c - functions for parsing, unparsing, and editing different
 * types of data used in kadmin.
 */

static int
get_response(const char *prompt, const char *def, char *buf, size_t len);

/*
 * attributes
 */

struct units kdb_attrs[] = {
    { "allow-digest",		KRB5_KDB_ALLOW_DIGEST },
    { "allow-kerberos4",	KRB5_KDB_ALLOW_KERBEROS4 },
    { "trusted-for-delegation",	KRB5_KDB_TRUSTED_FOR_DELEGATION },
    { "ok-as-delegate",		KRB5_KDB_OK_AS_DELEGATE },
    { "new-princ",		KRB5_KDB_NEW_PRINC },
    { "support-desmd5",		KRB5_KDB_SUPPORT_DESMD5 },
    { "pwchange-service",	KRB5_KDB_PWCHANGE_SERVICE },
    { "disallow-svr",		KRB5_KDB_DISALLOW_SVR },
    { "requires-pw-change",	KRB5_KDB_REQUIRES_PWCHANGE },
    { "requires-hw-auth",	KRB5_KDB_REQUIRES_HW_AUTH },
    { "requires-pre-auth",	KRB5_KDB_REQUIRES_PRE_AUTH },
    { "disallow-all-tix",	KRB5_KDB_DISALLOW_ALL_TIX },
    { "disallow-dup-skey",	KRB5_KDB_DISALLOW_DUP_SKEY },
    { "disallow-proxiable",	KRB5_KDB_DISALLOW_PROXIABLE },
    { "disallow-renewable",	KRB5_KDB_DISALLOW_RENEWABLE },
    { "disallow-tgt-based",	KRB5_KDB_DISALLOW_TGT_BASED },
    { "disallow-forwardable",	KRB5_KDB_DISALLOW_FORWARDABLE },
    { "disallow-postdated",	KRB5_KDB_DISALLOW_POSTDATED },
    { NULL, 0 }
};

/*
 * convert the attributes in `attributes' into a printable string
 * in `str, len'
 */

void
attributes2str(krb5_flags attributes, char *str, size_t len)
{
    unparse_flags (attributes, kdb_attrs, str, len);
}

/*
 * convert the string in `str' into attributes in `flags'
 * return 0 if parsed ok, else -1.
 */

int
str2attributes(const char *str, krb5_flags *flags)
{
    int res;

    res = parse_flags (str, kdb_attrs, *flags);
    if (res < 0)
	return res;
    else {
	*flags = res;
	return 0;
    }
}

/*
 * try to parse the string `resp' into attributes in `attr', also
 * setting the `bit' in `mask' if attributes are given and valid.
 */

int
parse_attributes (const char *resp, krb5_flags *attr, int *mask, int bit)
{
    krb5_flags tmp = *attr;

    if (str2attributes(resp, &tmp) == 0) {
	*attr = tmp;
	if (mask)
	    *mask |= bit;
	return 0;
    } else if(*resp == '?') {
	print_flags_table (kdb_attrs, stderr);
    } else {
	fprintf (stderr, "Unable to parse \"%s\"\n", resp);
    }
    return -1;
}

/*
 * allow the user to edit the attributes in `attr', prompting with `prompt'
 */

int
edit_attributes (const char *prompt, krb5_flags *attr, int *mask, int bit)
{
    char buf[1024], resp[1024];

    if (mask && (*mask & bit))
	return 0;

    attributes2str(*attr, buf, sizeof(buf));
    for (;;) {
	if(get_response("Attributes", buf, resp, sizeof(resp)) != 0)
	    return 1;
	if (resp[0] == '\0')
	    break;
	if (parse_attributes (resp, attr, mask, bit) == 0)
	    break;
    }
    return 0;
}

/*
 * time_t
 * the special value 0 means ``never''
 */

/*
 * Convert the time `t' to a string representation in `str' (of max
 * size `len').  If include_time also include time, otherwise just
 * date.
 */

void
time_t2str(time_t t, char *str, size_t len, int include_time)
{
    if(t) {
	if(include_time)
	    strftime(str, len, "%Y-%m-%d %H:%M:%S UTC", gmtime(&t));
	else
	    strftime(str, len, "%Y-%m-%d", gmtime(&t));
    } else
	snprintf(str, len, "never");
}

/*
 * Convert the time representation in `str' to a time in `time'.
 * Return 0 if succesful, else -1.
 */

int
str2time_t (const char *str, time_t *t)
{
    const char *p;
    struct tm tm, tm2;

    memset (&tm, 0, sizeof (tm));
    memset (&tm2, 0, sizeof (tm2));

    while(isspace((unsigned char)*str))
	str++;

    if (str[0] == '+') {
	str++;
	*t = parse_time(str, "month");
	if (*t < 0)
	    return -1;
	*t += time(NULL);
	return 0;
    }

    if(strcasecmp(str, "never") == 0) {
	*t = 0;
	return 0;
    }

    if(strcasecmp(str, "now") == 0) {
	*t = time(NULL);
	return 0;
    }

    p = strptime (str, "%Y-%m-%d", &tm);

    if (p == NULL)
	return -1;

    while(isspace((unsigned char)*p))
	p++;

    /* XXX this is really a bit optimistic, we should really complain
       if there was a problem parsing the time */
    if(p[0] != '\0' && strptime (p, "%H:%M:%S", &tm2) != NULL) {
	tm.tm_hour = tm2.tm_hour;
	tm.tm_min  = tm2.tm_min;
	tm.tm_sec  = tm2.tm_sec;
    } else {
	/* Do it on the end of the day */
	tm.tm_hour = 23;
	tm.tm_min  = 59;
	tm.tm_sec  = 59;
    }

    *t = tm2time (tm, 0);
    return 0;
}

/*
 * try to parse the time in `resp' storing it in `value'
 */

int
parse_timet (const char *resp, krb5_timestamp *value, int *mask, int bit)
{
    time_t tmp;

    if (str2time_t(resp, &tmp) == 0) {
	*value = tmp;
	if(mask)
	    *mask |= bit;
	return 0;
    }
    if(*resp != '?')
	fprintf (stderr, "Unable to parse time \"%s\"\n", resp);
    fprintf (stderr, "Print date on format YYYY-mm-dd [hh:mm:ss]\n");
    return -1;
}

/*
 * allow the user to edit the time in `value'
 */

int
edit_timet (const char *prompt, krb5_timestamp *value, int *mask, int bit)
{
    char buf[1024], resp[1024];

    if (mask && (*mask & bit))
	return 0;

    time_t2str (*value, buf, sizeof (buf), 0);

    for (;;) {
	if(get_response(prompt, buf, resp, sizeof(resp)) != 0)
	    return 1;
	if (parse_timet (resp, value, mask, bit) == 0)
	    break;
    }
    return 0;
}

/*
 * deltat
 * the special value 0 means ``unlimited''
 */

/*
 * convert the delta_t value in `t' into a printable form in `str, len'
 */

void
deltat2str(unsigned t, char *str, size_t len)
{
    if(t == 0 || t == INT_MAX)
	snprintf(str, len, "unlimited");
    else
	unparse_time(t, str, len);
}

/*
 * parse the delta value in `str', storing result in `*delta'
 * return 0 if ok, else -1
 */

int
str2deltat(const char *str, krb5_deltat *delta)
{
    int res;

    if(strcasecmp(str, "unlimited") == 0) {
	*delta = 0;
	return 0;
    }
    res = parse_time(str, "day");
    if (res < 0)
	return res;
    else {
	*delta = res;
	return 0;
    }
}

/*
 * try to parse the string in `resp' into a deltad in `value'
 * `mask' will get the bit `bit' set if a value was given.
 */

int
parse_deltat (const char *resp, krb5_deltat *value, int *mask, int bit)
{
    krb5_deltat tmp;

    if (str2deltat(resp, &tmp) == 0) {
	*value = tmp;
	if (mask)
	    *mask |= bit;
	return 0;
    } else if(*resp == '?') {
	print_time_table (stderr);
    } else {
	fprintf (stderr, "Unable to parse time \"%s\"\n", resp);
    }
    return -1;
}

/*
 * allow the user to edit the deltat in `value'
 */

int
edit_deltat (const char *prompt, krb5_deltat *value, int *mask, int bit)
{
    char buf[1024], resp[1024];

    if (mask && (*mask & bit))
	return 0;

    deltat2str(*value, buf, sizeof(buf));
    for (;;) {
	if(get_response(prompt, buf, resp, sizeof(resp)) != 0)
	    return 1;
	if (parse_deltat (resp, value, mask, bit) == 0)
	    break;
    }
    return 0;
}

/*
 * allow the user to edit `ent'
 */

void
set_defaults(kadm5_principal_ent_t ent, int *mask,
	     kadm5_principal_ent_t default_ent, int default_mask)
{
    if (default_ent
	&& (default_mask & KADM5_MAX_LIFE)
	&& !(*mask & KADM5_MAX_LIFE))
	ent->max_life = default_ent->max_life;

    if (default_ent
	&& (default_mask & KADM5_MAX_RLIFE)
	&& !(*mask & KADM5_MAX_RLIFE))
	ent->max_renewable_life = default_ent->max_renewable_life;

    if (default_ent
	&& (default_mask & KADM5_PRINC_EXPIRE_TIME)
	&& !(*mask & KADM5_PRINC_EXPIRE_TIME))
	ent->princ_expire_time = default_ent->princ_expire_time;

    if (default_ent
	&& (default_mask & KADM5_PW_EXPIRATION)
	&& !(*mask & KADM5_PW_EXPIRATION))
	ent->pw_expiration = default_ent->pw_expiration;

    if (default_ent
	&& (default_mask & KADM5_ATTRIBUTES)
	&& !(*mask & KADM5_ATTRIBUTES))
	ent->attributes = default_ent->attributes & ~KRB5_KDB_DISALLOW_ALL_TIX;
}

int
edit_entry(kadm5_principal_ent_t ent, int *mask,
	   kadm5_principal_ent_t default_ent, int default_mask)
{

    set_defaults(ent, mask, default_ent, default_mask);

    if(edit_deltat ("Max ticket life", &ent->max_life, mask,
		    KADM5_MAX_LIFE) != 0)
	return 1;

    if(edit_deltat ("Max renewable life", &ent->max_renewable_life, mask,
		    KADM5_MAX_RLIFE) != 0)
	return 1;

    if(edit_timet ("Principal expiration time", &ent->princ_expire_time, mask,
		   KADM5_PRINC_EXPIRE_TIME) != 0)
	return 1;

    if(edit_timet ("Password expiration time", &ent->pw_expiration, mask,
		   KADM5_PW_EXPIRATION) != 0)
	return 1;

    if(edit_attributes ("Attributes", &ent->attributes, mask,
			KADM5_ATTRIBUTES) != 0)
	return 1;

    return 0;
}

/*
 * Parse the arguments, set the fields in `ent' and the `mask' for the
 * entries having been set.
 * Return 1 on failure and 0 on success.
 */

int
set_entry(krb5_context contextp,
	  kadm5_principal_ent_t ent,
	  int *mask,
	  const char *max_ticket_life,
	  const char *max_renewable_life,
	  const char *expiration,
	  const char *pw_expiration,
	  const char *attributes)
{
    if (max_ticket_life != NULL) {
	if (parse_deltat (max_ticket_life, &ent->max_life,
			  mask, KADM5_MAX_LIFE)) {
	    krb5_warnx (contextp, "unable to parse `%s'", max_ticket_life);
	    return 1;
	}
    }
    if (max_renewable_life != NULL) {
	if (parse_deltat (max_renewable_life, &ent->max_renewable_life,
			  mask, KADM5_MAX_RLIFE)) {
	    krb5_warnx (contextp, "unable to parse `%s'", max_renewable_life);
	    return 1;
	}
    }

    if (expiration) {
	if (parse_timet (expiration, &ent->princ_expire_time,
			mask, KADM5_PRINC_EXPIRE_TIME)) {
	    krb5_warnx (contextp, "unable to parse `%s'", expiration);
	    return 1;
	}
    }
    if (pw_expiration) {
	if (parse_timet (pw_expiration, &ent->pw_expiration,
			 mask, KADM5_PW_EXPIRATION)) {
	    krb5_warnx (contextp, "unable to parse `%s'", pw_expiration);
	    return 1;
	}
    }
    if (attributes != NULL) {
	if (parse_attributes (attributes, &ent->attributes,
			      mask, KADM5_ATTRIBUTES)) {
	    krb5_warnx (contextp, "unable to parse `%s'", attributes);
	    return 1;
	}
    }
    return 0;
}

/*
 * Does `string' contain any globing characters?
 */

static int
is_expression(const char *string)
{
    const char *p;
    int quote = 0;

    for(p = string; *p; p++) {
	if(quote) {
	    quote = 0;
	    continue;
	}
	if(*p == '\\')
	    quote++;
	else if(strchr("[]*?", *p) != NULL)
	    return 1;
    }
    return 0;
}

/*
 * Loop over all principals matching exp.  If any of calls to `func'
 * failes, the first error is returned when all principals are
 * processed.
 */
int
foreach_principal(const char *exp_str,
		  int (*func)(krb5_principal, void*),
		  const char *funcname,
		  void *data)
{
    char **princs = NULL;
    int num_princs = 0;
    int i;
    krb5_error_code saved_ret = 0, ret = 0;
    krb5_principal princ_ent;
    int is_expr;

    /* if this isn't an expression, there is no point in wading
       through the whole database looking for matches */
    is_expr = is_expression(exp_str);
    if(is_expr)
	ret = kadm5_get_principals(kadm_handle, exp_str, &princs, &num_princs);
    if(!is_expr || ret == KADM5_AUTH_LIST) {
	/* we might be able to perform the requested opreration even
           if we're not allowed to list principals */
	num_princs = 1;
	princs = malloc(sizeof(*princs));
	if(princs == NULL)
	    return ENOMEM;
	princs[0] = strdup(exp_str);
	if(princs[0] == NULL){
	    free(princs);
	    return ENOMEM;
	}
    } else if(ret) {
	krb5_warn(context, ret, "kadm5_get_principals");
	return ret;
    }
    for(i = 0; i < num_princs; i++) {
	ret = krb5_parse_name(context, princs[i], &princ_ent);
	if(ret){
	    krb5_warn(context, ret, "krb5_parse_name(%s)", princs[i]);
	    continue;
	}
	ret = (*func)(princ_ent, data);
	if(ret) {
	    krb5_clear_error_message(context);
	    krb5_warn(context, ret, "%s %s", funcname, princs[i]);
	    if (saved_ret == 0)
		saved_ret = ret;
	}
	krb5_free_principal(context, princ_ent);
    }
    if (ret == 0 && saved_ret != 0)
	ret = saved_ret;
    kadm5_free_name_list(kadm_handle, princs, &num_princs);
    return ret;
}

/*
 * prompt with `prompt' and default value `def', and store the reply
 * in `buf, len'
 */

#include <setjmp.h>

static jmp_buf jmpbuf;

static void
interrupt(int sig)
{
    longjmp(jmpbuf, 1);
}

static int
get_response(const char *prompt, const char *def, char *buf, size_t len)
{
    char *p;
    void (*osig)(int);

    osig = signal(SIGINT, interrupt);
    if(setjmp(jmpbuf)) {
	signal(SIGINT, osig);
	fprintf(stderr, "\n");
	return 1;
    }

    fprintf(stderr, "%s [%s]:", prompt, def);
    if(fgets(buf, len, stdin) == NULL) {
	int save_errno = errno;
	if(ferror(stdin))
	    krb5_err(context, 1, save_errno, "<stdin>");
	signal(SIGINT, osig);
	return 1;
    }
    p = strchr(buf, '\n');
    if(p)
	*p = '\0';
    if(strcmp(buf, "") == 0)
	strlcpy(buf, def, len);
    signal(SIGINT, osig);
    return 0;
}

/*
 * return [0, 16) or -1
 */

static int
hex2n (char c)
{
    static char hexdigits[] = "0123456789abcdef";
    const char *p;

    p = strchr (hexdigits, tolower((unsigned char)c));
    if (p == NULL)
	return -1;
    else
	return p - hexdigits;
}

/*
 * convert a key in a readable format into a keyblock.
 * return 0 iff succesful, otherwise `err' should point to an error message
 */

int
parse_des_key (const char *key_string, krb5_key_data *key_data,
	       const char **error)
{
    const char *p = key_string;
    unsigned char bits[8];
    int i;

    if (strlen (key_string) != 16) {
	*error = "bad length, should be 16 for DES key";
	return 1;
    }
    for (i = 0; i < 8; ++i) {
	int d1, d2;

	d1 = hex2n(p[2 * i]);
	d2 = hex2n(p[2 * i + 1]);
	if (d1 < 0 || d2 < 0) {
	    *error = "non-hex character";
	    return 1;
	}
	bits[i] = (d1 << 4) | d2;
    }
    for (i = 0; i < 3; ++i) {
	key_data[i].key_data_ver  = 2;
	key_data[i].key_data_kvno = 0;
	/* key */
	key_data[i].key_data_type[0]     = ETYPE_DES_CBC_CRC;
	key_data[i].key_data_length[0]   = 8;
	key_data[i].key_data_contents[0] = malloc(8);
	if (key_data[i].key_data_contents[0] == NULL) {
	    *error = "malloc";
	    return ENOMEM;
	}
	memcpy (key_data[i].key_data_contents[0], bits, 8);
	/* salt */
	key_data[i].key_data_type[1]     = KRB5_PW_SALT;
	key_data[i].key_data_length[1]   = 0;
	key_data[i].key_data_contents[1] = NULL;
    }
    key_data[0].key_data_type[0] = ETYPE_DES_CBC_MD5;
    key_data[1].key_data_type[0] = ETYPE_DES_CBC_MD4;
    return 0;
}
