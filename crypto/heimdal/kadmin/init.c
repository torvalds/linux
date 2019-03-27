/*
 * Copyright (c) 1997-2004 Kungliga Tekniska Högskolan
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

#include "kadmin_locl.h"
#include "kadmin-commands.h"
#include <kadm5/private.h>

static kadm5_ret_t
create_random_entry(krb5_principal princ,
		    unsigned max_life,
		    unsigned max_rlife,
		    uint32_t attributes)
{
    kadm5_principal_ent_rec ent;
    kadm5_ret_t ret;
    int mask = 0;
    krb5_keyblock *keys;
    int n_keys, i;
    char *name;
    const char *password;
    char pwbuf[512];

    random_password(pwbuf, sizeof(pwbuf));
    password = pwbuf;

    ret = krb5_unparse_name(context, princ, &name);
    if (ret) {
	krb5_warn(context, ret, "failed to unparse principal name");
	return ret;
    }

    memset(&ent, 0, sizeof(ent));
    ent.principal = princ;
    mask |= KADM5_PRINCIPAL;
    if (max_life) {
	ent.max_life = max_life;
	mask |= KADM5_MAX_LIFE;
    }
    if (max_rlife) {
	ent.max_renewable_life = max_rlife;
	mask |= KADM5_MAX_RLIFE;
    }
    ent.attributes |= attributes | KRB5_KDB_DISALLOW_ALL_TIX;
    mask |= KADM5_ATTRIBUTES;

    /* Create the entry with a random password */
    ret = kadm5_create_principal(kadm_handle, &ent, mask, password);
    if(ret) {
	krb5_warn(context, ret, "create_random_entry(%s): randkey failed",
		  name);
	goto out;
    }

    /* Replace the string2key based keys with real random bytes */
    ret = kadm5_randkey_principal(kadm_handle, princ, &keys, &n_keys);
    if(ret) {
	krb5_warn(context, ret, "create_random_entry(%s): randkey failed",
		  name);
	goto out;
    }
    for(i = 0; i < n_keys; i++)
	krb5_free_keyblock_contents(context, &keys[i]);
    free(keys);
    ret = kadm5_get_principal(kadm_handle, princ, &ent,
			      KADM5_PRINCIPAL | KADM5_ATTRIBUTES);
    if(ret) {
	krb5_warn(context, ret, "create_random_entry(%s): "
		  "unable to get principal", name);
	goto out;
    }
    ent.attributes &= (~KRB5_KDB_DISALLOW_ALL_TIX);
    ent.kvno = 1;
    ret = kadm5_modify_principal(kadm_handle, &ent,
				 KADM5_ATTRIBUTES|KADM5_KVNO);
    kadm5_free_principal_ent (kadm_handle, &ent);
    if(ret) {
	krb5_warn(context, ret, "create_random_entry(%s): "
		  "unable to modify principal", name);
	goto out;
    }
 out:
    free(name);
    return ret;
}

extern int local_flag;

int
init(struct init_options *opt, int argc, char **argv)
{
    kadm5_ret_t ret;
    int i;
    HDB *db;
    krb5_deltat max_life = 0, max_rlife = 0;

    if (!local_flag) {
	krb5_warnx(context, "init is only available in local (-l) mode");
	return 0;
    }

    if (opt->realm_max_ticket_life_string) {
	if (str2deltat (opt->realm_max_ticket_life_string, &max_life) != 0) {
	    krb5_warnx (context, "unable to parse \"%s\"",
			opt->realm_max_ticket_life_string);
	    return 0;
	}
    }
    if (opt->realm_max_renewable_life_string) {
	if (str2deltat (opt->realm_max_renewable_life_string, &max_rlife) != 0) {
	    krb5_warnx (context, "unable to parse \"%s\"",
			opt->realm_max_renewable_life_string);
	    return 0;
	}
    }

    db = _kadm5_s_get_db(kadm_handle);

    ret = db->hdb_open(context, db, O_RDWR | O_CREAT, 0600);
    if(ret){
	krb5_warn(context, ret, "hdb_open");
	return 0;
    }
    db->hdb_close(context, db);
    for(i = 0; i < argc; i++){
	krb5_principal princ;
	const char *realm = argv[i];

	if (opt->realm_max_ticket_life_string == NULL) {
	    max_life = 0;
	    if(edit_deltat ("Realm max ticket life", &max_life, NULL, 0)) {
		return 0;
	    }
	}
	if (opt->realm_max_renewable_life_string == NULL) {
	    max_rlife = 0;
	    if(edit_deltat("Realm max renewable ticket life", &max_rlife,
			   NULL, 0)) {
		return 0;
	    }
	}

	/* Create `krbtgt/REALM' */
	ret = krb5_make_principal(context, &princ, realm,
				  KRB5_TGS_NAME, realm, NULL);
	if(ret)
	    return 0;

	create_random_entry(princ, max_life, max_rlife, 0);
	krb5_free_principal(context, princ);

	if (opt->bare_flag)
	    continue;

	/* Create `kadmin/changepw' */
	krb5_make_principal(context, &princ, realm,
			    "kadmin", "changepw", NULL);
	/*
	 * The Windows XP (at least) password changing protocol
	 * request the `kadmin/changepw' ticket with `renewable_ok,
	 * renewable, forwardable' and so fails if we disallow
	 * forwardable here.
	 */
	create_random_entry(princ, 5*60, 5*60,
			    KRB5_KDB_DISALLOW_TGT_BASED|
			    KRB5_KDB_PWCHANGE_SERVICE|
			    KRB5_KDB_DISALLOW_POSTDATED|
			    KRB5_KDB_DISALLOW_RENEWABLE|
			    KRB5_KDB_DISALLOW_PROXIABLE|
			    KRB5_KDB_REQUIRES_PRE_AUTH);
	krb5_free_principal(context, princ);

	/* Create `kadmin/admin' */
	krb5_make_principal(context, &princ, realm,
			    "kadmin", "admin", NULL);
	create_random_entry(princ, 60*60, 60*60, KRB5_KDB_REQUIRES_PRE_AUTH);
	krb5_free_principal(context, princ);

	/* Create `changepw/kerberos' (for v4 compat) */
	krb5_make_principal(context, &princ, realm,
			    "changepw", "kerberos", NULL);
	create_random_entry(princ, 60*60, 60*60,
			    KRB5_KDB_DISALLOW_TGT_BASED|
			    KRB5_KDB_PWCHANGE_SERVICE);

	krb5_free_principal(context, princ);

	/* Create `kadmin/hprop' for database propagation */
	krb5_make_principal(context, &princ, realm,
			    "kadmin", "hprop", NULL);
	create_random_entry(princ, 60*60, 60*60,
			    KRB5_KDB_REQUIRES_PRE_AUTH|
			    KRB5_KDB_DISALLOW_TGT_BASED);
	krb5_free_principal(context, princ);

	/* Create `WELLKNOWN/ANONYMOUS' for anonymous as-req */
	krb5_make_principal(context, &princ, realm,
			    KRB5_WELLKNOWN_NAME, KRB5_ANON_NAME, NULL);
	create_random_entry(princ, 60*60, 60*60,
			    KRB5_KDB_REQUIRES_PRE_AUTH);
	krb5_free_principal(context, princ);


	/* Create `default' */
	{
	    kadm5_principal_ent_rec ent;
	    int mask = 0;

	    memset (&ent, 0, sizeof(ent));
	    mask |= KADM5_PRINCIPAL;
	    krb5_make_principal(context, &ent.principal, realm,
				"default", NULL);
	    mask |= KADM5_MAX_LIFE;
	    ent.max_life = 24 * 60 * 60;
	    mask |= KADM5_MAX_RLIFE;
	    ent.max_renewable_life = 7 * ent.max_life;
	    ent.attributes = KRB5_KDB_DISALLOW_ALL_TIX;
	    mask |= KADM5_ATTRIBUTES;

	    ret = kadm5_create_principal(kadm_handle, &ent, mask, "");
	    if (ret)
		krb5_err (context, 1, ret, "kadm5_create_principal");

	    krb5_free_principal(context, ent.principal);
	}
    }
    return 0;
}
