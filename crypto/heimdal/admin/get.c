/*
 * Copyright (c) 1997-2004 Kungliga Tekniska Högskolan
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

#include "ktutil_locl.h"

RCSID("$Id$");

static void*
open_kadmin_connection(char *principal,
		       const char *realm,
		       char *admin_server,
		       int server_port)
{
    static kadm5_config_params conf;
    krb5_error_code ret;
    void *kadm_handle;
    memset(&conf, 0, sizeof(conf));

    if(realm) {
	conf.realm = strdup(realm);
	if (conf.realm == NULL) {
	    krb5_set_error_message(context, 0, "malloc: out of memory");
	    return NULL;
	}
	conf.mask |= KADM5_CONFIG_REALM;
    }

    if (admin_server) {
	conf.admin_server = admin_server;
	conf.mask |= KADM5_CONFIG_ADMIN_SERVER;
    }

    if (server_port) {
	conf.kadmind_port = htons(server_port);
	conf.mask |= KADM5_CONFIG_KADMIND_PORT;
    }

    /* should get realm from each principal, instead of doing
       everything with the same (local) realm */

    ret = kadm5_init_with_password_ctx(context,
				       principal,
				       NULL,
				       KADM5_ADMIN_SERVICE,
				       &conf, 0, 0,
				       &kadm_handle);
    free(conf.realm);
    if(ret) {
	krb5_warn(context, ret, "kadm5_init_with_password");
	return NULL;
    }
    return kadm_handle;
}

int
kt_get(struct get_options *opt, int argc, char **argv)
{
    krb5_error_code ret = 0;
    krb5_keytab keytab;
    void *kadm_handle = NULL;
    krb5_enctype *etypes = NULL;
    size_t netypes = 0;
    size_t i;
    int a, j;
    unsigned int failed = 0;

    if((keytab = ktutil_open_keytab()) == NULL)
	return 1;

    if(opt->realm_string)
	krb5_set_default_realm(context, opt->realm_string);

    if (opt->enctypes_strings.num_strings != 0) {

	etypes = malloc (opt->enctypes_strings.num_strings * sizeof(*etypes));
	if (etypes == NULL) {
	    krb5_warnx(context, "malloc failed");
	    goto out;
	}
	netypes = opt->enctypes_strings.num_strings;
	for(i = 0; i < netypes; i++) {
	    ret = krb5_string_to_enctype(context,
					 opt->enctypes_strings.strings[i],
					 &etypes[i]);
	    if(ret) {
		krb5_warnx(context, "unrecognized enctype: %s",
			   opt->enctypes_strings.strings[i]);
		goto out;
	    }
	}
    }


    for(a = 0; a < argc; a++){
	krb5_principal princ_ent;
	kadm5_principal_ent_rec princ;
	int mask = 0;
	krb5_keyblock *keys;
	int n_keys;
	int created = 0;
	krb5_keytab_entry entry;

	ret = krb5_parse_name(context, argv[a], &princ_ent);
	if (ret) {
	    krb5_warn(context, ret, "can't parse principal %s", argv[a]);
	    failed++;
	    continue;
	}
	memset(&princ, 0, sizeof(princ));
	princ.principal = princ_ent;
	mask |= KADM5_PRINCIPAL;
	princ.attributes |= KRB5_KDB_DISALLOW_ALL_TIX;
	mask |= KADM5_ATTRIBUTES;
	princ.princ_expire_time = 0;
	mask |= KADM5_PRINC_EXPIRE_TIME;

	if(kadm_handle == NULL) {
	    const char *r;
	    if(opt->realm_string != NULL)
		r = opt->realm_string;
	    else
		r = krb5_principal_get_realm(context, princ_ent);
	    kadm_handle = open_kadmin_connection(opt->principal_string,
						 r,
						 opt->admin_server_string,
						 opt->server_port_integer);
	    if(kadm_handle == NULL)
		break;
	}

	ret = kadm5_create_principal(kadm_handle, &princ, mask, "x");
	if(ret == 0)
	    created = 1;
	else if(ret != KADM5_DUP) {
	    krb5_warn(context, ret, "kadm5_create_principal(%s)", argv[a]);
	    krb5_free_principal(context, princ_ent);
	    failed++;
	    continue;
	}
	ret = kadm5_randkey_principal(kadm_handle, princ_ent, &keys, &n_keys);
	if (ret) {
	    krb5_warn(context, ret, "kadm5_randkey_principal(%s)", argv[a]);
	    krb5_free_principal(context, princ_ent);
	    failed++;
	    continue;
	}

	ret = kadm5_get_principal(kadm_handle, princ_ent, &princ,
			      KADM5_PRINCIPAL | KADM5_KVNO | KADM5_ATTRIBUTES);
	if (ret) {
	    krb5_warn(context, ret, "kadm5_get_principal(%s)", argv[a]);
	    for (j = 0; j < n_keys; j++)
		krb5_free_keyblock_contents(context, &keys[j]);
	    krb5_free_principal(context, princ_ent);
	    failed++;
	    continue;
	}
	if(!created && (princ.attributes & KRB5_KDB_DISALLOW_ALL_TIX))
	    krb5_warnx(context, "%s: disallow-all-tix flag set - clearing", argv[a]);
	princ.attributes &= (~KRB5_KDB_DISALLOW_ALL_TIX);
	mask = KADM5_ATTRIBUTES;
	if(created) {
	    princ.kvno = 1;
	    mask |= KADM5_KVNO;
	}
	ret = kadm5_modify_principal(kadm_handle, &princ, mask);
	if (ret) {
	    krb5_warn(context, ret, "kadm5_modify_principal(%s)", argv[a]);
	    for (j = 0; j < n_keys; j++)
		krb5_free_keyblock_contents(context, &keys[j]);
	    krb5_free_principal(context, princ_ent);
	    failed++;
	    continue;
	}
	for(j = 0; j < n_keys; j++) {
	    int do_add = TRUE;

	    if (netypes) {
		size_t k;

		do_add = FALSE;
		for (k = 0; k < netypes; ++k)
		    if (keys[j].keytype == etypes[k]) {
			do_add = TRUE;
			break;
		    }
	    }
	    if (do_add) {
		entry.principal = princ_ent;
		entry.vno = princ.kvno;
		entry.keyblock = keys[j];
		entry.timestamp = time (NULL);
		ret = krb5_kt_add_entry(context, keytab, &entry);
		if (ret)
		    krb5_warn(context, ret, "krb5_kt_add_entry");
	    }
	    krb5_free_keyblock_contents(context, &keys[j]);
	}

	kadm5_free_principal_ent(kadm_handle, &princ);
	krb5_free_principal(context, princ_ent);
    }
 out:
    free(etypes);
    if (kadm_handle)
	kadm5_destroy(kadm_handle);
    krb5_kt_close(context, keytab);
    return ret != 0 || failed > 0;
}
