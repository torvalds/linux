/*
 * Copyright (c) 2004 - 2005 Kungliga Tekniska Högskolan
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

#include "hdb_locl.h"
#include <der.h>

krb5_error_code
hdb_entry_check_mandatory(krb5_context context, const hdb_entry *ent)
{
    size_t i;

    if (ent->extensions == NULL)
	return 0;

    /*
     * check for unknown extensions and if they where tagged mandatory
     */

    for (i = 0; i < ent->extensions->len; i++) {
	if (ent->extensions->val[i].data.element !=
	    choice_HDB_extension_data_asn1_ellipsis)
	    continue;
	if (ent->extensions->val[i].mandatory) {
	    krb5_set_error_message(context, HDB_ERR_MANDATORY_OPTION,
				   "Principal have unknown "
				   "mandatory extension");
	    return HDB_ERR_MANDATORY_OPTION;
	}
    }
    return 0;
}

HDB_extension *
hdb_find_extension(const hdb_entry *entry, int type)
{
    size_t i;

    if (entry->extensions == NULL)
	return NULL;

    for (i = 0; i < entry->extensions->len; i++)
	if (entry->extensions->val[i].data.element == (unsigned)type)
	    return &entry->extensions->val[i];
    return NULL;
}

/*
 * Replace the extension `ext' in `entry'. Make a copy of the
 * extension, so the caller must still free `ext' on both success and
 * failure. Returns 0 or error code.
 */

krb5_error_code
hdb_replace_extension(krb5_context context,
		      hdb_entry *entry,
		      const HDB_extension *ext)
{
    HDB_extension *ext2;
    HDB_extension *es;
    int ret;

    ext2 = NULL;

    if (entry->extensions == NULL) {
	entry->extensions = calloc(1, sizeof(*entry->extensions));
	if (entry->extensions == NULL) {
	    krb5_set_error_message(context, ENOMEM, "malloc: out of memory");
	    return ENOMEM;
	}
    } else if (ext->data.element != choice_HDB_extension_data_asn1_ellipsis) {
	ext2 = hdb_find_extension(entry, ext->data.element);
    } else {
	/*
	 * This is an unknown extention, and we are asked to replace a
	 * possible entry in `entry' that is of the same type. This
	 * might seem impossible, but ASN.1 CHOICE comes to our
	 * rescue. The first tag in each branch in the CHOICE is
	 * unique, so just find the element in the list that have the
	 * same tag was we are putting into the list.
	 */
	Der_class replace_class, list_class;
	Der_type replace_type, list_type;
	unsigned int replace_tag, list_tag;
	size_t size;
	size_t i;

	ret = der_get_tag(ext->data.u.asn1_ellipsis.data,
			  ext->data.u.asn1_ellipsis.length,
			  &replace_class, &replace_type, &replace_tag,
			  &size);
	if (ret) {
	    krb5_set_error_message(context, ret, "hdb: failed to decode "
				   "replacement hdb extention");
	    return ret;
	}

	for (i = 0; i < entry->extensions->len; i++) {
	    HDB_extension *ext3 = &entry->extensions->val[i];

	    if (ext3->data.element != choice_HDB_extension_data_asn1_ellipsis)
		continue;

	    ret = der_get_tag(ext3->data.u.asn1_ellipsis.data,
			      ext3->data.u.asn1_ellipsis.length,
			      &list_class, &list_type, &list_tag,
			      &size);
	    if (ret) {
		krb5_set_error_message(context, ret, "hdb: failed to decode "
				       "present hdb extention");
		return ret;
	    }

	    if (MAKE_TAG(replace_class,replace_type,replace_type) ==
		MAKE_TAG(list_class,list_type,list_type)) {
		ext2 = ext3;
		break;
	    }
	}
    }

    if (ext2) {
	free_HDB_extension(ext2);
	ret = copy_HDB_extension(ext, ext2);
	if (ret)
	    krb5_set_error_message(context, ret, "hdb: failed to copy replacement "
				   "hdb extention");
	return ret;
    }

    es = realloc(entry->extensions->val,
		 (entry->extensions->len+1)*sizeof(entry->extensions->val[0]));
    if (es == NULL) {
	krb5_set_error_message(context, ENOMEM, "malloc: out of memory");
	return ENOMEM;
    }
    entry->extensions->val = es;

    ret = copy_HDB_extension(ext,
			     &entry->extensions->val[entry->extensions->len]);
    if (ret == 0)
	entry->extensions->len++;
    else
	krb5_set_error_message(context, ret, "hdb: failed to copy new extension");

    return ret;
}

krb5_error_code
hdb_clear_extension(krb5_context context,
		    hdb_entry *entry,
		    int type)
{
    size_t i;

    if (entry->extensions == NULL)
	return 0;

    for (i = 0; i < entry->extensions->len; i++) {
	if (entry->extensions->val[i].data.element == (unsigned)type) {
	    free_HDB_extension(&entry->extensions->val[i]);
	    memmove(&entry->extensions->val[i],
		    &entry->extensions->val[i + 1],
		    sizeof(entry->extensions->val[i]) * (entry->extensions->len - i - 1));
	    entry->extensions->len--;
	}
    }
    if (entry->extensions->len == 0) {
	free(entry->extensions->val);
	free(entry->extensions);
	entry->extensions = NULL;
    }

    return 0;
}


krb5_error_code
hdb_entry_get_pkinit_acl(const hdb_entry *entry, const HDB_Ext_PKINIT_acl **a)
{
    const HDB_extension *ext;

    ext = hdb_find_extension(entry, choice_HDB_extension_data_pkinit_acl);
    if (ext)
	*a = &ext->data.u.pkinit_acl;
    else
	*a = NULL;

    return 0;
}

krb5_error_code
hdb_entry_get_pkinit_hash(const hdb_entry *entry, const HDB_Ext_PKINIT_hash **a)
{
    const HDB_extension *ext;

    ext = hdb_find_extension(entry, choice_HDB_extension_data_pkinit_cert_hash);
    if (ext)
	*a = &ext->data.u.pkinit_cert_hash;
    else
	*a = NULL;

    return 0;
}

krb5_error_code
hdb_entry_get_pkinit_cert(const hdb_entry *entry, const HDB_Ext_PKINIT_cert **a)
{
    const HDB_extension *ext;

    ext = hdb_find_extension(entry, choice_HDB_extension_data_pkinit_cert);
    if (ext)
	*a = &ext->data.u.pkinit_cert;
    else
	*a = NULL;

    return 0;
}

krb5_error_code
hdb_entry_get_pw_change_time(const hdb_entry *entry, time_t *t)
{
    const HDB_extension *ext;

    ext = hdb_find_extension(entry, choice_HDB_extension_data_last_pw_change);
    if (ext)
	*t = ext->data.u.last_pw_change;
    else
	*t = 0;

    return 0;
}

krb5_error_code
hdb_entry_set_pw_change_time(krb5_context context,
			     hdb_entry *entry,
			     time_t t)
{
    HDB_extension ext;

    ext.mandatory = FALSE;
    ext.data.element = choice_HDB_extension_data_last_pw_change;
    if (t == 0)
	t = time(NULL);
    ext.data.u.last_pw_change = t;

    return hdb_replace_extension(context, entry, &ext);
}

int
hdb_entry_get_password(krb5_context context, HDB *db,
		       const hdb_entry *entry, char **p)
{
    HDB_extension *ext;
    char *str;
    int ret;

    ext = hdb_find_extension(entry, choice_HDB_extension_data_password);
    if (ext) {
	heim_utf8_string xstr;
	heim_octet_string pw;

	if (db->hdb_master_key_set && ext->data.u.password.mkvno) {
	    hdb_master_key key;

	    key = _hdb_find_master_key(ext->data.u.password.mkvno,
				       db->hdb_master_key);

	    if (key == NULL) {
		krb5_set_error_message(context, HDB_ERR_NO_MKEY,
				       "master key %d missing",
				       *ext->data.u.password.mkvno);
		return HDB_ERR_NO_MKEY;
	    }

	    ret = _hdb_mkey_decrypt(context, key, HDB_KU_MKEY,
				    ext->data.u.password.password.data,
				    ext->data.u.password.password.length,
				    &pw);
	} else {
	    ret = der_copy_octet_string(&ext->data.u.password.password, &pw);
	}
	if (ret) {
	    krb5_clear_error_message(context);
	    return ret;
	}

	xstr = pw.data;
	if (xstr[pw.length - 1] != '\0') {
	    krb5_set_error_message(context, EINVAL, "malformed password");
	    return EINVAL;
	}

	*p = strdup(xstr);

	der_free_octet_string(&pw);
	if (*p == NULL) {
	    krb5_set_error_message(context, ENOMEM, "malloc: out of memory");
	    return ENOMEM;
	}
	return 0;
    }

    ret = krb5_unparse_name(context, entry->principal, &str);
    if (ret == 0) {
	krb5_set_error_message(context, ENOENT,
			       "no password attribute for %s", str);
	free(str);
    } else
	krb5_clear_error_message(context);

    return ENOENT;
}

int
hdb_entry_set_password(krb5_context context, HDB *db,
		       hdb_entry *entry, const char *p)
{
    HDB_extension ext;
    hdb_master_key key;
    int ret;

    ext.mandatory = FALSE;
    ext.data.element = choice_HDB_extension_data_password;

    if (db->hdb_master_key_set) {

	key = _hdb_find_master_key(NULL, db->hdb_master_key);
	if (key == NULL) {
	    krb5_set_error_message(context, HDB_ERR_NO_MKEY,
				   "hdb_entry_set_password: "
				   "failed to find masterkey");
	    return HDB_ERR_NO_MKEY;
	}

	ret = _hdb_mkey_encrypt(context, key, HDB_KU_MKEY,
				p, strlen(p) + 1,
				&ext.data.u.password.password);
	if (ret)
	    return ret;

	ext.data.u.password.mkvno =
	    malloc(sizeof(*ext.data.u.password.mkvno));
	if (ext.data.u.password.mkvno == NULL) {
	    free_HDB_extension(&ext);
	    krb5_set_error_message(context, ENOMEM, "malloc: out of memory");
	    return ENOMEM;
	}
	*ext.data.u.password.mkvno = _hdb_mkey_version(key);

    } else {
	ext.data.u.password.mkvno = NULL;

	ret = krb5_data_copy(&ext.data.u.password.password,
			     p, strlen(p) + 1);
	if (ret) {
	    krb5_set_error_message(context, ret, "malloc: out of memory");
	    free_HDB_extension(&ext);
	    return ret;
	}
    }

    ret = hdb_replace_extension(context, entry, &ext);

    free_HDB_extension(&ext);

    return ret;
}

int
hdb_entry_clear_password(krb5_context context, hdb_entry *entry)
{
    return hdb_clear_extension(context, entry,
			       choice_HDB_extension_data_password);
}

krb5_error_code
hdb_entry_get_ConstrainedDelegACL(const hdb_entry *entry,
				  const HDB_Ext_Constrained_delegation_acl **a)
{
    const HDB_extension *ext;

    ext = hdb_find_extension(entry,
			     choice_HDB_extension_data_allowed_to_delegate_to);
    if (ext)
	*a = &ext->data.u.allowed_to_delegate_to;
    else
	*a = NULL;

    return 0;
}

krb5_error_code
hdb_entry_get_aliases(const hdb_entry *entry, const HDB_Ext_Aliases **a)
{
    const HDB_extension *ext;

    ext = hdb_find_extension(entry, choice_HDB_extension_data_aliases);
    if (ext)
	*a = &ext->data.u.aliases;
    else
	*a = NULL;

    return 0;
}
