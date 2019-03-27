/*
 * Copyright (c) 1997-2006 Kungliga Tekniska Högskolan
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
#include "kadmin-commands.h"

/*
 * fetch the default principal corresponding to `princ'
 */

static krb5_error_code
get_default (kadm5_server_context *contextp,
	     krb5_principal princ,
	     kadm5_principal_ent_t default_ent)
{
    krb5_error_code ret;
    krb5_principal def_principal;
    krb5_const_realm realm = krb5_principal_get_realm(contextp->context, princ);

    ret = krb5_make_principal (contextp->context, &def_principal,
			       realm, "default", NULL);
    if (ret)
	return ret;
    ret = kadm5_get_principal (contextp, def_principal, default_ent,
			       KADM5_PRINCIPAL_NORMAL_MASK);
    krb5_free_principal (contextp->context, def_principal);
    return ret;
}

/*
 * Add the principal `name' to the database.
 * Prompt for all data not given by the input parameters.
 */

static krb5_error_code
add_one_principal (const char *name,
		   int rand_key,
		   int rand_password,
		   int use_defaults,
		   char *password,
		   krb5_key_data *key_data,
		   const char *max_ticket_life,
		   const char *max_renewable_life,
		   const char *attributes,
		   const char *expiration,
		   const char *pw_expiration)
{
    krb5_error_code ret;
    kadm5_principal_ent_rec princ, defrec;
    kadm5_principal_ent_rec *default_ent = NULL;
    krb5_principal princ_ent = NULL;
    int mask = 0;
    int default_mask = 0;
    char pwbuf[1024];

    memset(&princ, 0, sizeof(princ));
    ret = krb5_parse_name(context, name, &princ_ent);
    if (ret) {
	krb5_warn(context, ret, "krb5_parse_name");
	return ret;
    }
    princ.principal = princ_ent;
    mask |= KADM5_PRINCIPAL;

    ret = set_entry(context, &princ, &mask,
		    max_ticket_life, max_renewable_life,
		    expiration, pw_expiration, attributes);
    if (ret)
	goto out;

    default_ent = &defrec;
    ret = get_default (kadm_handle, princ_ent, default_ent);
    if (ret) {
	default_ent  = NULL;
	default_mask = 0;
    } else {
	default_mask = KADM5_ATTRIBUTES | KADM5_MAX_LIFE | KADM5_MAX_RLIFE |
	    KADM5_PRINC_EXPIRE_TIME | KADM5_PW_EXPIRATION;
    }

    if(use_defaults)
	set_defaults(&princ, &mask, default_ent, default_mask);
    else
	if(edit_entry(&princ, &mask, default_ent, default_mask))
	    goto out;
    if(rand_key || key_data) {
	princ.attributes |= KRB5_KDB_DISALLOW_ALL_TIX;
	mask |= KADM5_ATTRIBUTES;
	random_password (pwbuf, sizeof(pwbuf));
	password = pwbuf;
    } else if (rand_password) {
	random_password (pwbuf, sizeof(pwbuf));
	password = pwbuf;
    } else if(password == NULL) {
	char *princ_name;
	char *prompt;

	krb5_unparse_name(context, princ_ent, &princ_name);
	asprintf (&prompt, "%s's Password: ", princ_name);
	free (princ_name);
	ret = UI_UTIL_read_pw_string (pwbuf, sizeof(pwbuf), prompt, 1);
	free (prompt);
	if (ret) {
	    ret = KRB5_LIBOS_BADPWDMATCH;
	    krb5_set_error_message(context, ret, "failed to verify password");
	    goto out;
	}
	password = pwbuf;
    }

    ret = kadm5_create_principal(kadm_handle, &princ, mask, password);
    if(ret) {
	krb5_warn(context, ret, "kadm5_create_principal");
	goto out;
    }
    if(rand_key) {
	krb5_keyblock *new_keys;
	int n_keys, i;
	ret = kadm5_randkey_principal(kadm_handle, princ_ent,
				      &new_keys, &n_keys);
	if(ret){
	    krb5_warn(context, ret, "kadm5_randkey_principal");
	    n_keys = 0;
	}
	for(i = 0; i < n_keys; i++)
	    krb5_free_keyblock_contents(context, &new_keys[i]);
	if (n_keys > 0)
	    free(new_keys);
	kadm5_get_principal(kadm_handle, princ_ent, &princ,
			    KADM5_PRINCIPAL | KADM5_KVNO | KADM5_ATTRIBUTES);
	princ.attributes &= (~KRB5_KDB_DISALLOW_ALL_TIX);
	princ.kvno = 1;
	kadm5_modify_principal(kadm_handle, &princ,
			       KADM5_ATTRIBUTES | KADM5_KVNO);
	kadm5_free_principal_ent(kadm_handle, &princ);
    } else if (key_data) {
	ret = kadm5_chpass_principal_with_key (kadm_handle, princ_ent,
					       3, key_data);
	if (ret) {
	    krb5_warn(context, ret, "kadm5_chpass_principal_with_key");
	}
	kadm5_get_principal(kadm_handle, princ_ent, &princ,
			    KADM5_PRINCIPAL | KADM5_ATTRIBUTES);
	princ.attributes &= (~KRB5_KDB_DISALLOW_ALL_TIX);
	kadm5_modify_principal(kadm_handle, &princ, KADM5_ATTRIBUTES);
	kadm5_free_principal_ent(kadm_handle, &princ);
    } else if (rand_password) {
	char *princ_name;

	krb5_unparse_name(context, princ_ent, &princ_name);
	printf ("added %s with password \"%s\"\n", princ_name, password);
	free (princ_name);
    }
out:
    if (princ_ent)
	krb5_free_principal (context, princ_ent);
    if(default_ent)
	kadm5_free_principal_ent (kadm_handle, default_ent);
    if (password != NULL)
	memset (password, 0, strlen(password));
    return ret;
}

/*
 * parse the string `key_string' into `key', returning 0 iff succesful.
 */

/*
 * the ank command
 */

/*
 * Parse arguments and add all the principals.
 */

int
add_new_key(struct add_options *opt, int argc, char **argv)
{
    krb5_error_code ret = 0;
    int i;
    int num;
    krb5_key_data key_data[3];
    krb5_key_data *kdp = NULL;

    num = 0;
    if (opt->random_key_flag)
	++num;
    if (opt->random_password_flag)
	++num;
    if (opt->password_string)
	++num;
    if (opt->key_string)
	++num;

    if (num > 1) {
	fprintf (stderr, "give only one of "
		"--random-key, --random-password, --password, --key\n");
	return 1;
    }

    if (opt->key_string) {
	const char *error;

	if (parse_des_key (opt->key_string, key_data, &error)) {
	    fprintf (stderr, "failed parsing key \"%s\": %s\n",
		     opt->key_string, error);
	    return 1;
	}
	kdp = key_data;
    }

    for(i = 0; i < argc; i++) {
	ret = add_one_principal (argv[i],
				 opt->random_key_flag,
				 opt->random_password_flag,
				 opt->use_defaults_flag,
				 opt->password_string,
				 kdp,
				 opt->max_ticket_life_string,
				 opt->max_renewable_life_string,
				 opt->attributes_string,
				 opt->expiration_time_string,
				 opt->pw_expiration_time_string);
	if (ret) {
	    krb5_warn (context, ret, "adding %s", argv[i]);
	    break;
	}
    }
    if (kdp) {
	int16_t dummy = 3;
	kadm5_free_key_data (kadm_handle, &dummy, key_data);
    }
    return ret != 0;
}
