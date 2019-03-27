/*
 * Copyright (c) 1997 - 2001 Kungliga Tekniska Högskolan
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

#include "kdc_locl.h"

struct timeval _kdc_now;

krb5_error_code
_kdc_db_fetch(krb5_context context,
	      krb5_kdc_configuration *config,
	      krb5_const_principal principal,
	      unsigned flags,
	      krb5uint32 *kvno_ptr,
	      HDB **db,
	      hdb_entry_ex **h)
{
    hdb_entry_ex *ent;
    krb5_error_code ret = HDB_ERR_NOENTRY;
    int i;
    unsigned kvno = 0;

    if (kvno_ptr) {
	    kvno = *kvno_ptr;
	    flags |= HDB_F_KVNO_SPECIFIED;
    }

    ent = calloc (1, sizeof (*ent));
    if (ent == NULL) {
	krb5_set_error_message(context, ENOMEM, "malloc: out of memory");
	return ENOMEM;
    }

    for(i = 0; i < config->num_db; i++) {
	krb5_principal enterprise_principal = NULL;
	if (!(config->db[i]->hdb_capability_flags & HDB_CAP_F_HANDLE_ENTERPRISE_PRINCIPAL)
	    && principal->name.name_type == KRB5_NT_ENTERPRISE_PRINCIPAL) {
	    if (principal->name.name_string.len != 1) {
		ret = KRB5_PARSE_MALFORMED;
		krb5_set_error_message(context, ret,
				       "malformed request: "
				       "enterprise name with %d name components",
				       principal->name.name_string.len);
		free(ent);
		return ret;
	    }
	    ret = krb5_parse_name(context, principal->name.name_string.val[0],
				  &enterprise_principal);
	    if (ret) {
		free(ent);
		return ret;
	    }

	    principal = enterprise_principal;
	}

	ret = config->db[i]->hdb_open(context, config->db[i], O_RDONLY, 0);
	if (ret) {
	    const char *msg = krb5_get_error_message(context, ret);
	    kdc_log(context, config, 0, "Failed to open database: %s", msg);
	    krb5_free_error_message(context, msg);
	    continue;
	}

	ret = config->db[i]->hdb_fetch_kvno(context,
					    config->db[i],
					    principal,
					    flags | HDB_F_DECRYPT,
					    kvno,
					    ent);

	krb5_free_principal(context, enterprise_principal);

	config->db[i]->hdb_close(context, config->db[i]);
	if(ret == 0) {
	    if (db)
		*db = config->db[i];
	    *h = ent;
	    return 0;
	}
    }
    free(ent);
    krb5_set_error_message(context, ret,
			   "no such entry found in hdb");
    return ret;
}

void
_kdc_free_ent(krb5_context context, hdb_entry_ex *ent)
{
    hdb_free_entry (context, ent);
    free (ent);
}

/*
 * Use the order list of preferred encryption types and sort the
 * available keys and return the most preferred key.
 */

krb5_error_code
_kdc_get_preferred_key(krb5_context context,
		       krb5_kdc_configuration *config,
		       hdb_entry_ex *h,
		       const char *name,
		       krb5_enctype *enctype,
		       Key **key)
{
    krb5_error_code ret;
    int i;

    if (config->use_strongest_server_key) {
	const krb5_enctype *p = krb5_kerberos_enctypes(context);

	for (i = 0; p[i] != ETYPE_NULL; i++) {
	    if (krb5_enctype_valid(context, p[i]) != 0)
		continue;
	    ret = hdb_enctype2key(context, &h->entry, p[i], key);
	    if (ret != 0)
		continue;
	    if (enctype != NULL)
		*enctype = p[i];
	    return 0;
	}
    } else {
	*key = NULL;

	for (i = 0; i < h->entry.keys.len; i++) {
	    if (krb5_enctype_valid(context, h->entry.keys.val[i].key.keytype)
		!= 0)
		continue;
	    ret = hdb_enctype2key(context, &h->entry,
		h->entry.keys.val[i].key.keytype, key);
	    if (ret != 0)
		continue;
	    if (enctype != NULL)
		*enctype = (*key)->key.keytype;
	    return 0;
	}
    }

    krb5_set_error_message(context, EINVAL,
			   "No valid kerberos key found for %s", name);
    return EINVAL; /* XXX */
}

