/*
 * Copyright (c) 1997 - 2005 Kungliga Tekniska Högskolan
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

#include "krb5_locl.h"

/**
 * @page krb5_keytab_intro The keytab handing functions
 * @section section_krb5_keytab Kerberos Keytabs
 *
 * See the library functions here: @ref krb5_keytab
 *
 * Keytabs are long term key storage for servers, their equvalment of
 * password files.
 *
 * Normally the only function that useful for server are to specify
 * what keytab to use to other core functions like krb5_rd_req()
 * krb5_kt_resolve(), and krb5_kt_close().
 *
 * @subsection krb5_keytab_names Keytab names
 *
 * A keytab name is on the form type:residual. The residual part is
 * specific to each keytab-type.
 *
 * When a keytab-name is resolved, the type is matched with an internal
 * list of keytab types. If there is no matching keytab type,
 * the default keytab is used. The current default type is FILE.
 *
 * The default value can be changed in the configuration file
 * /etc/krb5.conf by setting the variable
 * [defaults]default_keytab_name.
 *
 * The keytab types that are implemented in Heimdal are:
 * - file
 *   store the keytab in a file, the type's name is FILE .  The
 *   residual part is a filename. For compatibility with other
 *   Kerberos implemtation WRFILE and JAVA14 is also accepted.  WRFILE
 *   has the same format as FILE. JAVA14 have a format that is
 *   compatible with older versions of MIT kerberos and SUN's Java
 *   based installation.  They store a truncted kvno, so when the knvo
 *   excess 255, they are truncted in this format.
 *
 * - keytab
 *   store the keytab in a AFS keyfile (usually /usr/afs/etc/KeyFile ),
 *   the type's name is AFSKEYFILE. The residual part is a filename.
 *
 * - memory
 *   The keytab is stored in a memory segment. This allows sensitive
 *   and/or temporary data not to be stored on disk. The type's name
 *   is MEMORY. Each MEMORY keytab is referenced counted by and
 *   opened by the residual name, so two handles can point to the
 *   same memory area.  When the last user closes using krb5_kt_close()
 *   the keytab, the keys in they keytab is memset() to zero and freed
 *   and can no longer be looked up by name.
 *
 *
 * @subsection krb5_keytab_example Keytab example
 *
 *  This is a minimalistic version of ktutil.
 *
 * @code
int
main (int argc, char **argv)
{
    krb5_context context;
    krb5_keytab keytab;
    krb5_kt_cursor cursor;
    krb5_keytab_entry entry;
    krb5_error_code ret;
    char *principal;

    if (krb5_init_context (&context) != 0)
	errx(1, "krb5_context");

    ret = krb5_kt_default (context, &keytab);
    if (ret)
	krb5_err(context, 1, ret, "krb5_kt_default");

    ret = krb5_kt_start_seq_get(context, keytab, &cursor);
    if (ret)
	krb5_err(context, 1, ret, "krb5_kt_start_seq_get");
    while((ret = krb5_kt_next_entry(context, keytab, &entry, &cursor)) == 0){
	krb5_unparse_name(context, entry.principal, &principal);
	printf("principal: %s\n", principal);
	free(principal);
	krb5_kt_free_entry(context, &entry);
    }
    ret = krb5_kt_end_seq_get(context, keytab, &cursor);
    if (ret)
	krb5_err(context, 1, ret, "krb5_kt_end_seq_get");
    ret = krb5_kt_close(context, keytab);
    if (ret)
	krb5_err(context, 1, ret, "krb5_kt_close");
    krb5_free_context(context);
    return 0;
}
 * @endcode
 *
 */


/**
 * Register a new keytab backend.
 *
 * @param context a Keberos context.
 * @param ops a backend to register.
 *
 * @return Return an error code or 0, see krb5_get_error_message().
 *
 * @ingroup krb5_keytab
 */

KRB5_LIB_FUNCTION krb5_error_code KRB5_LIB_CALL
krb5_kt_register(krb5_context context,
		 const krb5_kt_ops *ops)
{
    struct krb5_keytab_data *tmp;

    if (strlen(ops->prefix) > KRB5_KT_PREFIX_MAX_LEN - 1) {
	krb5_set_error_message(context, KRB5_KT_BADNAME,
			       N_("can't register cache type, prefix too long", ""));
	return KRB5_KT_BADNAME;
    }

    tmp = realloc(context->kt_types,
		  (context->num_kt_types + 1) * sizeof(*context->kt_types));
    if(tmp == NULL) {
	krb5_set_error_message(context, ENOMEM,
			       N_("malloc: out of memory", ""));
	return ENOMEM;
    }
    memcpy(&tmp[context->num_kt_types], ops,
	   sizeof(tmp[context->num_kt_types]));
    context->kt_types = tmp;
    context->num_kt_types++;
    return 0;
}

static const char *
keytab_name(const char *name, const char **type, size_t *type_len)
{
    const char *residual;

    residual = strchr(name, ':');

    if (residual == NULL ||
	name[0] == '/'
#ifdef _WIN32
        /* Avoid treating <drive>:<path> as a keytab type
         * specification */
        || name + 1 == residual
#endif
        ) {

        *type = "FILE";
        *type_len = strlen(*type);
        residual = name;
    } else {
        *type = name;
        *type_len = residual - name;
        residual++;
    }

    return residual;
}

/**
 * Resolve the keytab name (of the form `type:residual') in `name'
 * into a keytab in `id'.
 *
 * @param context a Keberos context.
 * @param name name to resolve
 * @param id resulting keytab, free with krb5_kt_close().
 *
 * @return Return an error code or 0, see krb5_get_error_message().
 *
 * @ingroup krb5_keytab
 */


KRB5_LIB_FUNCTION krb5_error_code KRB5_LIB_CALL
krb5_kt_resolve(krb5_context context,
		const char *name,
		krb5_keytab *id)
{
    krb5_keytab k;
    int i;
    const char *type, *residual;
    size_t type_len;
    krb5_error_code ret;

    residual = keytab_name(name, &type, &type_len);

    for(i = 0; i < context->num_kt_types; i++) {
	if(strncasecmp(type, context->kt_types[i].prefix, type_len) == 0)
	    break;
    }
    if(i == context->num_kt_types) {
	krb5_set_error_message(context, KRB5_KT_UNKNOWN_TYPE,
			       N_("unknown keytab type %.*s", "type"),
			       (int)type_len, type);
	return KRB5_KT_UNKNOWN_TYPE;
    }

    k = malloc (sizeof(*k));
    if (k == NULL) {
	krb5_set_error_message(context, ENOMEM, N_("malloc: out of memory", ""));
	return ENOMEM;
    }
    memcpy(k, &context->kt_types[i], sizeof(*k));
    k->data = NULL;
    ret = (*k->resolve)(context, residual, k);
    if(ret) {
	free(k);
	k = NULL;
    }
    *id = k;
    return ret;
}

/**
 * copy the name of the default keytab into `name'.
 *
 * @param context a Keberos context.
 * @param name buffer where the name will be written
 * @param namesize length of name
 *
 * @return Return an error code or 0, see krb5_get_error_message().
 *
 * @ingroup krb5_keytab
 */

KRB5_LIB_FUNCTION krb5_error_code KRB5_LIB_CALL
krb5_kt_default_name(krb5_context context, char *name, size_t namesize)
{
    if (strlcpy (name, context->default_keytab, namesize) >= namesize) {
	krb5_clear_error_message (context);
	return KRB5_CONFIG_NOTENUFSPACE;
    }
    return 0;
}

/**
 * Copy the name of the default modify keytab into `name'.
 *
 * @param context a Keberos context.
 * @param name buffer where the name will be written
 * @param namesize length of name
 *
 * @return Return an error code or 0, see krb5_get_error_message().
 *
 * @ingroup krb5_keytab
 */

KRB5_LIB_FUNCTION krb5_error_code KRB5_LIB_CALL
krb5_kt_default_modify_name(krb5_context context, char *name, size_t namesize)
{
    const char *kt = NULL;
    if(context->default_keytab_modify == NULL) {
	if(strncasecmp(context->default_keytab, "ANY:", 4) != 0)
	    kt = context->default_keytab;
	else {
	    size_t len = strcspn(context->default_keytab + 4, ",");
	    if(len >= namesize) {
		krb5_clear_error_message(context);
		return KRB5_CONFIG_NOTENUFSPACE;
	    }
	    strlcpy(name, context->default_keytab + 4, namesize);
	    name[len] = '\0';
	    return 0;
	}
    } else
	kt = context->default_keytab_modify;
    if (strlcpy (name, kt, namesize) >= namesize) {
	krb5_clear_error_message (context);
	return KRB5_CONFIG_NOTENUFSPACE;
    }
    return 0;
}

/**
 * Set `id' to the default keytab.
 *
 * @param context a Keberos context.
 * @param id the new default keytab.
 *
 * @return Return an error code or 0, see krb5_get_error_message().
 *
 * @ingroup krb5_keytab
 */

KRB5_LIB_FUNCTION krb5_error_code KRB5_LIB_CALL
krb5_kt_default(krb5_context context, krb5_keytab *id)
{
    return krb5_kt_resolve (context, context->default_keytab, id);
}

/**
 * Read the key identified by `(principal, vno, enctype)' from the
 * keytab in `keyprocarg' (the default if == NULL) into `*key'.
 *
 * @param context a Keberos context.
 * @param keyprocarg
 * @param principal
 * @param vno
 * @param enctype
 * @param key
 *
 * @return Return an error code or 0, see krb5_get_error_message().
 *
 * @ingroup krb5_keytab
 */

KRB5_LIB_FUNCTION krb5_error_code KRB5_LIB_CALL
krb5_kt_read_service_key(krb5_context context,
			 krb5_pointer keyprocarg,
			 krb5_principal principal,
			 krb5_kvno vno,
			 krb5_enctype enctype,
			 krb5_keyblock **key)
{
    krb5_keytab keytab;
    krb5_keytab_entry entry;
    krb5_error_code ret;

    if (keyprocarg)
	ret = krb5_kt_resolve (context, keyprocarg, &keytab);
    else
	ret = krb5_kt_default (context, &keytab);

    if (ret)
	return ret;

    ret = krb5_kt_get_entry (context, keytab, principal, vno, enctype, &entry);
    krb5_kt_close (context, keytab);
    if (ret)
	return ret;
    ret = krb5_copy_keyblock (context, &entry.keyblock, key);
    krb5_kt_free_entry(context, &entry);
    return ret;
}

/**
 * Return the type of the `keytab' in the string `prefix of length
 * `prefixsize'.
 *
 * @param context a Keberos context.
 * @param keytab the keytab to get the prefix for
 * @param prefix prefix buffer
 * @param prefixsize length of prefix buffer
 *
 * @return Return an error code or 0, see krb5_get_error_message().
 *
 * @ingroup krb5_keytab
 */

KRB5_LIB_FUNCTION krb5_error_code KRB5_LIB_CALL
krb5_kt_get_type(krb5_context context,
		 krb5_keytab keytab,
		 char *prefix,
		 size_t prefixsize)
{
    strlcpy(prefix, keytab->prefix, prefixsize);
    return 0;
}

/**
 * Retrieve the name of the keytab `keytab' into `name', `namesize'
 *
 * @param context a Keberos context.
 * @param keytab the keytab to get the name for.
 * @param name name buffer.
 * @param namesize size of name buffer.
 *
 * @return Return an error code or 0, see krb5_get_error_message().
 *
 * @ingroup krb5_keytab
 */

KRB5_LIB_FUNCTION krb5_error_code KRB5_LIB_CALL
krb5_kt_get_name(krb5_context context,
		 krb5_keytab keytab,
		 char *name,
		 size_t namesize)
{
    return (*keytab->get_name)(context, keytab, name, namesize);
}

/**
 * Retrieve the full name of the keytab `keytab' and store the name in
 * `str'.
 *
 * @param context a Keberos context.
 * @param keytab keytab to get name for.
 * @param str the name of the keytab name, usee krb5_xfree() to free
 *        the string.  On error, *str is set to NULL.
 *
 * @return Return an error code or 0, see krb5_get_error_message().
 *
 * @ingroup krb5_keytab
 */

KRB5_LIB_FUNCTION krb5_error_code KRB5_LIB_CALL
krb5_kt_get_full_name(krb5_context context,
		      krb5_keytab keytab,
		      char **str)
{
    char type[KRB5_KT_PREFIX_MAX_LEN];
    char name[MAXPATHLEN];
    krb5_error_code ret;

    *str = NULL;

    ret = krb5_kt_get_type(context, keytab, type, sizeof(type));
    if (ret)
	return ret;

    ret = krb5_kt_get_name(context, keytab, name, sizeof(name));
    if (ret)
	return ret;

    if (asprintf(str, "%s:%s", type, name) == -1) {
	krb5_set_error_message(context, ENOMEM, N_("malloc: out of memory", ""));
	*str = NULL;
	return ENOMEM;
    }

    return 0;
}

/**
 * Finish using the keytab in `id'.  All resources will be released,
 * even on errors.
 *
 * @param context a Keberos context.
 * @param id keytab to close.
 *
 * @return Return an error code or 0, see krb5_get_error_message().
 *
 * @ingroup krb5_keytab
 */

KRB5_LIB_FUNCTION krb5_error_code KRB5_LIB_CALL
krb5_kt_close(krb5_context context,
	      krb5_keytab id)
{
    krb5_error_code ret;

    ret = (*id->close)(context, id);
    memset(id, 0, sizeof(*id));
    free(id);
    return ret;
}

/**
 * Destroy (remove) the keytab in `id'.  All resources will be released,
 * even on errors, does the equvalment of krb5_kt_close() on the resources.
 *
 * @param context a Keberos context.
 * @param id keytab to destroy.
 *
 * @return Return an error code or 0, see krb5_get_error_message().
 *
 * @ingroup krb5_keytab
 */

KRB5_LIB_FUNCTION krb5_error_code KRB5_LIB_CALL
krb5_kt_destroy(krb5_context context,
		krb5_keytab id)
{
    krb5_error_code ret;

    ret = (*id->destroy)(context, id);
    krb5_kt_close(context, id);
    return ret;
}

/*
 * Match any aliases in keytab `entry' with `principal'.
 */

static krb5_boolean
compare_aliseses(krb5_context context,
		 krb5_keytab_entry *entry,
		 krb5_const_principal principal)
{
    unsigned int i;
    if (entry->aliases == NULL)
	return FALSE;
    for (i = 0; i < entry->aliases->len; i++)
	if (krb5_principal_compare(context, &entry->aliases->val[i], principal))
	    return TRUE;
    return FALSE;
}

/**
 * Compare `entry' against `principal, vno, enctype'.
 * Any of `principal, vno, enctype' might be 0 which acts as a wildcard.
 * Return TRUE if they compare the same, FALSE otherwise.
 *
 * @param context a Keberos context.
 * @param entry an entry to match with.
 * @param principal principal to match, NULL matches all principals.
 * @param vno key version to match, 0 matches all key version numbers.
 * @param enctype encryption type to match, 0 matches all encryption types.
 *
 * @return Return TRUE or match, FALSE if not matched.
 *
 * @ingroup krb5_keytab
 */

KRB5_LIB_FUNCTION krb5_boolean KRB5_LIB_CALL
krb5_kt_compare(krb5_context context,
		krb5_keytab_entry *entry,
		krb5_const_principal principal,
		krb5_kvno vno,
		krb5_enctype enctype)
{
    if(principal != NULL &&
       !(krb5_principal_compare(context, entry->principal, principal) ||
	 compare_aliseses(context, entry, principal)))
	return FALSE;
    if(vno && vno != entry->vno)
	return FALSE;
    if(enctype && enctype != entry->keyblock.keytype)
	return FALSE;
    return TRUE;
}

krb5_error_code
_krb5_kt_principal_not_found(krb5_context context,
			     krb5_error_code ret,
			     krb5_keytab id,
			     krb5_const_principal principal,
			     krb5_enctype enctype,
			     int kvno)
{
    char princ[256], kvno_str[25], *kt_name;
    char *enctype_str = NULL;

    krb5_unparse_name_fixed (context, principal, princ, sizeof(princ));
    krb5_kt_get_full_name (context, id, &kt_name);
    krb5_enctype_to_string(context, enctype, &enctype_str);

    if (kvno)
	snprintf(kvno_str, sizeof(kvno_str), "(kvno %d)", kvno);
    else
	kvno_str[0] = '\0';

    krb5_set_error_message (context, ret,
			    N_("Failed to find %s%s in keytab %s (%s)",
			       "principal, kvno, keytab file, enctype"),
			    princ,
			    kvno_str,
			    kt_name ? kt_name : "unknown keytab",
			    enctype_str ? enctype_str : "unknown enctype");
    free(kt_name);
    free(enctype_str);
    return ret;
}


/**
 * Retrieve the keytab entry for `principal, kvno, enctype' into `entry'
 * from the keytab `id'. Matching is done like krb5_kt_compare().
 *
 * @param context a Keberos context.
 * @param id a keytab.
 * @param principal principal to match, NULL matches all principals.
 * @param kvno key version to match, 0 matches all key version numbers.
 * @param enctype encryption type to match, 0 matches all encryption types.
 * @param entry the returned entry, free with krb5_kt_free_entry().
 *
 * @return Return an error code or 0, see krb5_get_error_message().
 *
 * @ingroup krb5_keytab
 */

KRB5_LIB_FUNCTION krb5_error_code KRB5_LIB_CALL
krb5_kt_get_entry(krb5_context context,
		  krb5_keytab id,
		  krb5_const_principal principal,
		  krb5_kvno kvno,
		  krb5_enctype enctype,
		  krb5_keytab_entry *entry)
{
    krb5_keytab_entry tmp;
    krb5_error_code ret;
    krb5_kt_cursor cursor;

    if(id->get)
	return (*id->get)(context, id, principal, kvno, enctype, entry);

    ret = krb5_kt_start_seq_get (context, id, &cursor);
    if (ret) {
	/* This is needed for krb5_verify_init_creds, but keep error
	 * string from previous error for the human. */
	context->error_code = KRB5_KT_NOTFOUND;
	return KRB5_KT_NOTFOUND;
    }

    entry->vno = 0;
    while (krb5_kt_next_entry(context, id, &tmp, &cursor) == 0) {
	if (krb5_kt_compare(context, &tmp, principal, 0, enctype)) {
	    /* the file keytab might only store the lower 8 bits of
	       the kvno, so only compare those bits */
	    if (kvno == tmp.vno
		|| (tmp.vno < 256 && kvno % 256 == tmp.vno)) {
		krb5_kt_copy_entry_contents (context, &tmp, entry);
		krb5_kt_free_entry (context, &tmp);
		krb5_kt_end_seq_get(context, id, &cursor);
		return 0;
	    } else if (kvno == 0 && tmp.vno > entry->vno) {
		if (entry->vno)
		    krb5_kt_free_entry (context, entry);
		krb5_kt_copy_entry_contents (context, &tmp, entry);
	    }
	}
	krb5_kt_free_entry(context, &tmp);
    }
    krb5_kt_end_seq_get (context, id, &cursor);
    if (entry->vno == 0)
	return _krb5_kt_principal_not_found(context, KRB5_KT_NOTFOUND,
					    id, principal, enctype, kvno);
    return 0;
}

/**
 * Copy the contents of `in' into `out'.
 *
 * @param context a Keberos context.
 * @param in the keytab entry to copy.
 * @param out the copy of the keytab entry, free with krb5_kt_free_entry().
 *
 * @return Return an error code or 0, see krb5_get_error_message().
 *
 * @ingroup krb5_keytab
 */

KRB5_LIB_FUNCTION krb5_error_code KRB5_LIB_CALL
krb5_kt_copy_entry_contents(krb5_context context,
			    const krb5_keytab_entry *in,
			    krb5_keytab_entry *out)
{
    krb5_error_code ret;

    memset(out, 0, sizeof(*out));
    out->vno = in->vno;

    ret = krb5_copy_principal (context, in->principal, &out->principal);
    if (ret)
	goto fail;
    ret = krb5_copy_keyblock_contents (context,
				       &in->keyblock,
				       &out->keyblock);
    if (ret)
	goto fail;
    out->timestamp = in->timestamp;
    return 0;
fail:
    krb5_kt_free_entry (context, out);
    return ret;
}

/**
 * Free the contents of `entry'.
 *
 * @param context a Keberos context.
 * @param entry the entry to free
 *
 * @return Return an error code or 0, see krb5_get_error_message().
 *
 * @ingroup krb5_keytab
 */

KRB5_LIB_FUNCTION krb5_error_code KRB5_LIB_CALL
krb5_kt_free_entry(krb5_context context,
		   krb5_keytab_entry *entry)
{
    krb5_free_principal (context, entry->principal);
    krb5_free_keyblock_contents (context, &entry->keyblock);
    memset(entry, 0, sizeof(*entry));
    return 0;
}

/**
 * Set `cursor' to point at the beginning of `id'.
 *
 * @param context a Keberos context.
 * @param id a keytab.
 * @param cursor a newly allocated cursor, free with krb5_kt_end_seq_get().
 *
 * @return Return an error code or 0, see krb5_get_error_message().
 *
 * @ingroup krb5_keytab
 */

KRB5_LIB_FUNCTION krb5_error_code KRB5_LIB_CALL
krb5_kt_start_seq_get(krb5_context context,
		      krb5_keytab id,
		      krb5_kt_cursor *cursor)
{
    if(id->start_seq_get == NULL) {
	krb5_set_error_message(context, HEIM_ERR_OPNOTSUPP,
			       N_("start_seq_get is not supported "
				  "in the %s keytab type", ""),
			       id->prefix);
	return HEIM_ERR_OPNOTSUPP;
    }
    return (*id->start_seq_get)(context, id, cursor);
}

/**
 * Get the next entry from keytab, advance the cursor.  On last entry
 * the function will return KRB5_KT_END.
 *
 * @param context a Keberos context.
 * @param id a keytab.
 * @param entry the returned entry, free with krb5_kt_free_entry().
 * @param cursor the cursor of the iteration.
 *
 * @return Return an error code or 0, see krb5_get_error_message().
 *
 * @ingroup krb5_keytab
 */

KRB5_LIB_FUNCTION krb5_error_code KRB5_LIB_CALL
krb5_kt_next_entry(krb5_context context,
		   krb5_keytab id,
		   krb5_keytab_entry *entry,
		   krb5_kt_cursor *cursor)
{
    if(id->next_entry == NULL) {
	krb5_set_error_message(context, HEIM_ERR_OPNOTSUPP,
			       N_("next_entry is not supported in the %s "
				  " keytab", ""),
			       id->prefix);
	return HEIM_ERR_OPNOTSUPP;
    }
    return (*id->next_entry)(context, id, entry, cursor);
}

/**
 * Release all resources associated with `cursor'.
 *
 * @param context a Keberos context.
 * @param id a keytab.
 * @param cursor the cursor to free.
 *
 * @return Return an error code or 0, see krb5_get_error_message().
 *
 * @ingroup krb5_keytab
 */

KRB5_LIB_FUNCTION krb5_error_code KRB5_LIB_CALL
krb5_kt_end_seq_get(krb5_context context,
		    krb5_keytab id,
		    krb5_kt_cursor *cursor)
{
    if(id->end_seq_get == NULL) {
	krb5_set_error_message(context, HEIM_ERR_OPNOTSUPP,
			       "end_seq_get is not supported in the %s "
			       " keytab", id->prefix);
	return HEIM_ERR_OPNOTSUPP;
    }
    return (*id->end_seq_get)(context, id, cursor);
}

/**
 * Add the entry in `entry' to the keytab `id'.
 *
 * @param context a Keberos context.
 * @param id a keytab.
 * @param entry the entry to add
 *
 * @return Return an error code or 0, see krb5_get_error_message().
 *
 * @ingroup krb5_keytab
 */

KRB5_LIB_FUNCTION krb5_error_code KRB5_LIB_CALL
krb5_kt_add_entry(krb5_context context,
		  krb5_keytab id,
		  krb5_keytab_entry *entry)
{
    if(id->add == NULL) {
	krb5_set_error_message(context, KRB5_KT_NOWRITE,
			       N_("Add is not supported in the %s keytab", ""),
			       id->prefix);
	return KRB5_KT_NOWRITE;
    }
    entry->timestamp = time(NULL);
    return (*id->add)(context, id,entry);
}

/**
 * Remove an entry from the keytab, matching is done using
 * krb5_kt_compare().

 * @param context a Keberos context.
 * @param id a keytab.
 * @param entry the entry to remove
 *
 * @return Return an error code or 0, see krb5_get_error_message().
 *
 * @ingroup krb5_keytab
 */

KRB5_LIB_FUNCTION krb5_error_code KRB5_LIB_CALL
krb5_kt_remove_entry(krb5_context context,
		     krb5_keytab id,
		     krb5_keytab_entry *entry)
{
    if(id->remove == NULL) {
	krb5_set_error_message(context, KRB5_KT_NOWRITE,
			       N_("Remove is not supported in the %s keytab", ""),
			       id->prefix);
	return KRB5_KT_NOWRITE;
    }
    return (*id->remove)(context, id, entry);
}

/**
 * Return true if the keytab exists and have entries
 *
 * @param context a Keberos context.
 * @param id a keytab.
 *
 * @return Return an error code or 0, see krb5_get_error_message().
 *
 * @ingroup krb5_keytab
 */

KRB5_LIB_FUNCTION krb5_boolean KRB5_LIB_CALL
krb5_kt_have_content(krb5_context context,
		     krb5_keytab id)
{
    krb5_keytab_entry entry;
    krb5_kt_cursor cursor;
    krb5_error_code ret;
    char *name;

    ret = krb5_kt_start_seq_get(context, id, &cursor);
    if (ret)
	goto notfound;

    ret = krb5_kt_next_entry(context, id, &entry, &cursor);
    krb5_kt_end_seq_get(context, id, &cursor);
    if (ret)
	goto notfound;

    krb5_kt_free_entry(context, &entry);

    return 0;

 notfound:
    ret = krb5_kt_get_full_name(context, id, &name);
    if (ret == 0) {
	krb5_set_error_message(context, KRB5_KT_NOTFOUND,
			       N_("No entry in keytab: %s", ""), name);
	free(name);
    }
    return KRB5_KT_NOTFOUND;
}
