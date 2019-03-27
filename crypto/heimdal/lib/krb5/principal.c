/*
 * Copyright (c) 1997-2007 Kungliga Tekniska Högskolan
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

/**
 * @page krb5_principal_intro The principal handing functions.
 *
 * A Kerberos principal is a email address looking string that
 * contains to parts separeted by a @.  The later part is the kerbero
 * realm the principal belongs to and the former is a list of 0 or
 * more components. For example
 * @verbatim
lha@SU.SE
host/hummel.it.su.se@SU.SE
host/admin@H5L.ORG
@endverbatim
 *
 * See the library functions here: @ref krb5_principal
 */

#include "krb5_locl.h"
#ifdef HAVE_RES_SEARCH
#define USE_RESOLVER
#endif
#ifdef HAVE_ARPA_NAMESER_H
#include <arpa/nameser.h>
#endif
#include <fnmatch.h>
#include "resolve.h"

#define princ_num_comp(P) ((P)->name.name_string.len)
#define princ_type(P) ((P)->name.name_type)
#define princ_comp(P) ((P)->name.name_string.val)
#define princ_ncomp(P, N) ((P)->name.name_string.val[(N)])
#define princ_realm(P) ((P)->realm)

/**
 * Frees a Kerberos principal allocated by the library with
 * krb5_parse_name(), krb5_make_principal() or any other related
 * principal functions.
 *
 * @param context A Kerberos context.
 * @param p a principal to free.
 *
 * @return An krb5 error code, see krb5_get_error_message().
 *
 * @ingroup krb5_principal
 */

KRB5_LIB_FUNCTION void KRB5_LIB_CALL
krb5_free_principal(krb5_context context,
		    krb5_principal p)
{
    if(p){
	free_Principal(p);
	free(p);
    }
}

/**
 * Set the type of the principal
 *
 * @param context A Kerberos context.
 * @param principal principal to set the type for
 * @param type the new type
 *
 * @return An krb5 error code, see krb5_get_error_message().
 *
 * @ingroup krb5_principal
 */

KRB5_LIB_FUNCTION void KRB5_LIB_CALL
krb5_principal_set_type(krb5_context context,
			krb5_principal principal,
			int type)
{
    princ_type(principal) = type;
}

/**
 * Get the type of the principal
 *
 * @param context A Kerberos context.
 * @param principal principal to get the type for
 *
 * @return the type of principal
 *
 * @ingroup krb5_principal
 */

KRB5_LIB_FUNCTION int KRB5_LIB_CALL
krb5_principal_get_type(krb5_context context,
			krb5_const_principal principal)
{
    return princ_type(principal);
}

/**
 * Get the realm of the principal
 *
 * @param context A Kerberos context.
 * @param principal principal to get the realm for
 *
 * @return realm of the principal, don't free or use after krb5_principal is freed
 *
 * @ingroup krb5_principal
 */

KRB5_LIB_FUNCTION const char* KRB5_LIB_CALL
krb5_principal_get_realm(krb5_context context,
			 krb5_const_principal principal)
{
    return princ_realm(principal);
}

KRB5_LIB_FUNCTION const char* KRB5_LIB_CALL
krb5_principal_get_comp_string(krb5_context context,
			       krb5_const_principal principal,
			       unsigned int component)
{
    if(component >= princ_num_comp(principal))
       return NULL;
    return princ_ncomp(principal, component);
}

/**
 * Get number of component is principal.
 *
 * @param context Kerberos 5 context
 * @param principal principal to query
 *
 * @return number of components in string
 *
 * @ingroup krb5_principal
 */

KRB5_LIB_FUNCTION unsigned int KRB5_LIB_CALL
krb5_principal_get_num_comp(krb5_context context,
			    krb5_const_principal principal)
{
    return princ_num_comp(principal);
}

/**
 * Parse a name into a krb5_principal structure, flags controls the behavior.
 *
 * @param context Kerberos 5 context
 * @param name name to parse into a Kerberos principal
 * @param flags flags to control the behavior
 * @param principal returned principal, free with krb5_free_principal().
 *
 * @return An krb5 error code, see krb5_get_error_message().
 *
 * @ingroup krb5_principal
 */

KRB5_LIB_FUNCTION krb5_error_code KRB5_LIB_CALL
krb5_parse_name_flags(krb5_context context,
		      const char *name,
		      int flags,
		      krb5_principal *principal)
{
    krb5_error_code ret;
    heim_general_string *comp;
    heim_general_string realm = NULL;
    int ncomp;

    const char *p;
    char *q;
    char *s;
    char *start;

    int n;
    char c;
    int got_realm = 0;
    int first_at = 1;
    int enterprise = (flags & KRB5_PRINCIPAL_PARSE_ENTERPRISE);

    *principal = NULL;

#define RFLAGS (KRB5_PRINCIPAL_PARSE_NO_REALM|KRB5_PRINCIPAL_PARSE_REQUIRE_REALM)

    if ((flags & RFLAGS) == RFLAGS) {
	krb5_set_error_message(context, KRB5_ERR_NO_SERVICE,
			       N_("Can't require both realm and "
				  "no realm at the same time", ""));
	return KRB5_ERR_NO_SERVICE;
    }
#undef RFLAGS

    /* count number of component,
     * enterprise names only have one component
     */
    ncomp = 1;
    if (!enterprise) {
	for(p = name; *p; p++){
	    if(*p=='\\'){
		if(!p[1]) {
		    krb5_set_error_message(context, KRB5_PARSE_MALFORMED,
					   N_("trailing \\ in principal name", ""));
		    return KRB5_PARSE_MALFORMED;
		}
		p++;
	    } else if(*p == '/')
		ncomp++;
	    else if(*p == '@')
		break;
	}
    }
    comp = calloc(ncomp, sizeof(*comp));
    if (comp == NULL) {
	krb5_set_error_message(context, ENOMEM,
			       N_("malloc: out of memory", ""));
	return ENOMEM;
    }

    n = 0;
    p = start = q = s = strdup(name);
    if (start == NULL) {
	free (comp);
	krb5_set_error_message(context, ENOMEM,
			       N_("malloc: out of memory", ""));
	return ENOMEM;
    }
    while(*p){
	c = *p++;
	if(c == '\\'){
	    c = *p++;
	    if(c == 'n')
		c = '\n';
	    else if(c == 't')
		c = '\t';
	    else if(c == 'b')
		c = '\b';
	    else if(c == '0')
		c = '\0';
	    else if(c == '\0') {
		ret = KRB5_PARSE_MALFORMED;
		krb5_set_error_message(context, ret,
				       N_("trailing \\ in principal name", ""));
		goto exit;
	    }
	}else if(enterprise && first_at) {
	    if (c == '@')
		first_at = 0;
	}else if((c == '/' && !enterprise) || c == '@'){
	    if(got_realm){
		ret = KRB5_PARSE_MALFORMED;
		krb5_set_error_message(context, ret,
				       N_("part after realm in principal name", ""));
		goto exit;
	    }else{
		comp[n] = malloc(q - start + 1);
		if (comp[n] == NULL) {
		    ret = ENOMEM;
		    krb5_set_error_message(context, ret,
					   N_("malloc: out of memory", ""));
		    goto exit;
		}
		memcpy(comp[n], start, q - start);
		comp[n][q - start] = 0;
		n++;
	    }
	    if(c == '@')
		got_realm = 1;
	    start = q;
	    continue;
	}
	if(got_realm && (c == '/' || c == '\0')) {
	    ret = KRB5_PARSE_MALFORMED;
	    krb5_set_error_message(context, ret,
				   N_("part after realm in principal name", ""));
	    goto exit;
	}
	*q++ = c;
    }
    if(got_realm){
	if (flags & KRB5_PRINCIPAL_PARSE_NO_REALM) {
	    ret = KRB5_PARSE_MALFORMED;
	    krb5_set_error_message(context, ret,
				   N_("realm found in 'short' principal "
				      "expected to be without one", ""));
	    goto exit;
	}
	realm = malloc(q - start + 1);
	if (realm == NULL) {
	    ret = ENOMEM;
	    krb5_set_error_message(context, ret,
				   N_("malloc: out of memory", ""));
	    goto exit;
	}
	memcpy(realm, start, q - start);
	realm[q - start] = 0;
    }else{
	if (flags & KRB5_PRINCIPAL_PARSE_REQUIRE_REALM) {
	    ret = KRB5_PARSE_MALFORMED;
	    krb5_set_error_message(context, ret,
				   N_("realm NOT found in principal "
				      "expected to be with one", ""));
	    goto exit;
	} else if (flags & KRB5_PRINCIPAL_PARSE_NO_REALM) {
	    realm = NULL;
	} else {
	    ret = krb5_get_default_realm (context, &realm);
	    if (ret)
		goto exit;
	}

	comp[n] = malloc(q - start + 1);
	if (comp[n] == NULL) {
	    ret = ENOMEM;
	    krb5_set_error_message(context, ret,
				   N_("malloc: out of memory", ""));
	    goto exit;
	}
	memcpy(comp[n], start, q - start);
	comp[n][q - start] = 0;
	n++;
    }
    *principal = malloc(sizeof(**principal));
    if (*principal == NULL) {
	ret = ENOMEM;
	krb5_set_error_message(context, ret,
			       N_("malloc: out of memory", ""));
	goto exit;
    }
    if (enterprise)
	(*principal)->name.name_type = KRB5_NT_ENTERPRISE_PRINCIPAL;
    else
	(*principal)->name.name_type = KRB5_NT_PRINCIPAL;
    (*principal)->name.name_string.val = comp;
    princ_num_comp(*principal) = n;
    (*principal)->realm = realm;
    free(s);
    return 0;
exit:
    while(n>0){
	free(comp[--n]);
    }
    free(comp);
    free(realm);
    free(s);
    return ret;
}

/**
 * Parse a name into a krb5_principal structure
 *
 * @param context Kerberos 5 context
 * @param name name to parse into a Kerberos principal
 * @param principal returned principal, free with krb5_free_principal().
 *
 * @return An krb5 error code, see krb5_get_error_message().
 *
 * @ingroup krb5_principal
 */

KRB5_LIB_FUNCTION krb5_error_code KRB5_LIB_CALL
krb5_parse_name(krb5_context context,
		const char *name,
		krb5_principal *principal)
{
    return krb5_parse_name_flags(context, name, 0, principal);
}

static const char quotable_chars[] = " \n\t\b\\/@";
static const char replace_chars[] = " ntb\\/@";
static const char nq_chars[] = "    \\/@";

#define add_char(BASE, INDEX, LEN, C) do { if((INDEX) < (LEN)) (BASE)[(INDEX)++] = (C); }while(0);

static size_t
quote_string(const char *s, char *out, size_t idx, size_t len, int display)
{
    const char *p, *q;
    for(p = s; *p && idx < len; p++){
	q = strchr(quotable_chars, *p);
	if (q && display) {
	    add_char(out, idx, len, replace_chars[q - quotable_chars]);
	} else if (q) {
	    add_char(out, idx, len, '\\');
	    add_char(out, idx, len, replace_chars[q - quotable_chars]);
	}else
	    add_char(out, idx, len, *p);
    }
    if(idx < len)
	out[idx] = '\0';
    return idx;
}


static krb5_error_code
unparse_name_fixed(krb5_context context,
		   krb5_const_principal principal,
		   char *name,
		   size_t len,
		   int flags)
{
    size_t idx = 0;
    size_t i;
    int short_form = (flags & KRB5_PRINCIPAL_UNPARSE_SHORT) != 0;
    int no_realm = (flags & KRB5_PRINCIPAL_UNPARSE_NO_REALM) != 0;
    int display = (flags & KRB5_PRINCIPAL_UNPARSE_DISPLAY) != 0;

    if (!no_realm && princ_realm(principal) == NULL) {
	krb5_set_error_message(context, ERANGE,
			       N_("Realm missing from principal, "
				  "can't unparse", ""));
	return ERANGE;
    }

    for(i = 0; i < princ_num_comp(principal); i++){
	if(i)
	    add_char(name, idx, len, '/');
	idx = quote_string(princ_ncomp(principal, i), name, idx, len, display);
	if(idx == len) {
	    krb5_set_error_message(context, ERANGE,
				   N_("Out of space printing principal", ""));
	    return ERANGE;
	}
    }
    /* add realm if different from default realm */
    if(short_form && !no_realm) {
	krb5_realm r;
	krb5_error_code ret;
	ret = krb5_get_default_realm(context, &r);
	if(ret)
	    return ret;
	if(strcmp(princ_realm(principal), r) != 0)
	    short_form = 0;
	free(r);
    }
    if(!short_form && !no_realm) {
	add_char(name, idx, len, '@');
	idx = quote_string(princ_realm(principal), name, idx, len, display);
	if(idx == len) {
	    krb5_set_error_message(context, ERANGE,
				   N_("Out of space printing "
				      "realm of principal", ""));
	    return ERANGE;
	}
    }
    return 0;
}

/**
 * Unparse the principal name to a fixed buffer
 *
 * @param context A Kerberos context.
 * @param principal principal to unparse
 * @param name buffer to write name to
 * @param len length of buffer
 *
 * @return An krb5 error code, see krb5_get_error_message().
 *
 * @ingroup krb5_principal
 */

KRB5_LIB_FUNCTION krb5_error_code KRB5_LIB_CALL
krb5_unparse_name_fixed(krb5_context context,
			krb5_const_principal principal,
			char *name,
			size_t len)
{
    return unparse_name_fixed(context, principal, name, len, 0);
}

/**
 * Unparse the principal name to a fixed buffer. The realm is skipped
 * if its a default realm.
 *
 * @param context A Kerberos context.
 * @param principal principal to unparse
 * @param name buffer to write name to
 * @param len length of buffer
 *
 * @return An krb5 error code, see krb5_get_error_message().
 *
 * @ingroup krb5_principal
 */

KRB5_LIB_FUNCTION krb5_error_code KRB5_LIB_CALL
krb5_unparse_name_fixed_short(krb5_context context,
			      krb5_const_principal principal,
			      char *name,
			      size_t len)
{
    return unparse_name_fixed(context, principal, name, len,
			      KRB5_PRINCIPAL_UNPARSE_SHORT);
}

/**
 * Unparse the principal name with unparse flags to a fixed buffer.
 *
 * @param context A Kerberos context.
 * @param principal principal to unparse
 * @param flags unparse flags
 * @param name buffer to write name to
 * @param len length of buffer
 *
 * @return An krb5 error code, see krb5_get_error_message().
 *
 * @ingroup krb5_principal
 */

KRB5_LIB_FUNCTION krb5_error_code KRB5_LIB_CALL
krb5_unparse_name_fixed_flags(krb5_context context,
			      krb5_const_principal principal,
			      int flags,
			      char *name,
			      size_t len)
{
    return unparse_name_fixed(context, principal, name, len, flags);
}

static krb5_error_code
unparse_name(krb5_context context,
	     krb5_const_principal principal,
	     char **name,
	     int flags)
{
    size_t len = 0, plen;
    size_t i;
    krb5_error_code ret;
    /* count length */
    if (princ_realm(principal)) {
	plen = strlen(princ_realm(principal));

	if(strcspn(princ_realm(principal), quotable_chars) == plen)
	    len += plen;
	else
	    len += 2*plen;
	len++; /* '@' */
    }
    for(i = 0; i < princ_num_comp(principal); i++){
	plen = strlen(princ_ncomp(principal, i));
	if(strcspn(princ_ncomp(principal, i), quotable_chars) == plen)
	    len += plen;
	else
	    len += 2*plen;
	len++;
    }
    len++; /* '\0' */
    *name = malloc(len);
    if(*name == NULL) {
	krb5_set_error_message(context, ENOMEM,
			       N_("malloc: out of memory", ""));
	return ENOMEM;
    }
    ret = unparse_name_fixed(context, principal, *name, len, flags);
    if(ret) {
	free(*name);
	*name = NULL;
    }
    return ret;
}

/**
 * Unparse the Kerberos name into a string
 *
 * @param context Kerberos 5 context
 * @param principal principal to query
 * @param name resulting string, free with krb5_xfree()
 *
 * @return An krb5 error code, see krb5_get_error_message().
 *
 * @ingroup krb5_principal
 */

KRB5_LIB_FUNCTION krb5_error_code KRB5_LIB_CALL
krb5_unparse_name(krb5_context context,
		  krb5_const_principal principal,
		  char **name)
{
    return unparse_name(context, principal, name, 0);
}

/**
 * Unparse the Kerberos name into a string
 *
 * @param context Kerberos 5 context
 * @param principal principal to query
 * @param flags flag to determine the behavior
 * @param name resulting string, free with krb5_xfree()
 *
 * @return An krb5 error code, see krb5_get_error_message().
 *
 * @ingroup krb5_principal
 */

KRB5_LIB_FUNCTION krb5_error_code KRB5_LIB_CALL
krb5_unparse_name_flags(krb5_context context,
			krb5_const_principal principal,
			int flags,
			char **name)
{
    return unparse_name(context, principal, name, flags);
}

/**
 * Unparse the principal name to a allocated buffer. The realm is
 * skipped if its a default realm.
 *
 * @param context A Kerberos context.
 * @param principal principal to unparse
 * @param name returned buffer, free with krb5_xfree()
 *
 * @return An krb5 error code, see krb5_get_error_message().
 *
 * @ingroup krb5_principal
 */

KRB5_LIB_FUNCTION krb5_error_code KRB5_LIB_CALL
krb5_unparse_name_short(krb5_context context,
			krb5_const_principal principal,
			char **name)
{
    return unparse_name(context, principal, name, KRB5_PRINCIPAL_UNPARSE_SHORT);
}

/**
 * Set a new realm for a principal, and as a side-effect free the
 * previous realm.
 *
 * @param context A Kerberos context.
 * @param principal principal set the realm for
 * @param realm the new realm to set
 *
 * @return An krb5 error code, see krb5_get_error_message().
 *
 * @ingroup krb5_principal
 */

KRB5_LIB_FUNCTION krb5_error_code KRB5_LIB_CALL
krb5_principal_set_realm(krb5_context context,
			 krb5_principal principal,
			 krb5_const_realm realm)
{
    if (princ_realm(principal))
	free(princ_realm(principal));

    princ_realm(principal) = strdup(realm);
    if (princ_realm(principal) == NULL) {
	krb5_set_error_message(context, ENOMEM,
			       N_("malloc: out of memory", ""));
	return ENOMEM;
    }
    return 0;
}

#ifndef HEIMDAL_SMALLER
/**
 * Build a principal using vararg style building
 *
 * @param context A Kerberos context.
 * @param principal returned principal
 * @param rlen length of realm
 * @param realm realm name
 * @param ... a list of components ended with NULL.
 *
 * @return An krb5 error code, see krb5_get_error_message().
 *
 * @ingroup krb5_principal
 */

KRB5_LIB_FUNCTION krb5_error_code KRB5_LIB_CALL
krb5_build_principal(krb5_context context,
		     krb5_principal *principal,
		     int rlen,
		     krb5_const_realm realm,
		     ...)
{
    krb5_error_code ret;
    va_list ap;
    va_start(ap, realm);
    ret = krb5_build_principal_va(context, principal, rlen, realm, ap);
    va_end(ap);
    return ret;
}
#endif

/**
 * Build a principal using vararg style building
 *
 * @param context A Kerberos context.
 * @param principal returned principal
 * @param realm realm name
 * @param ... a list of components ended with NULL.
 *
 * @return An krb5 error code, see krb5_get_error_message().
 *
 * @ingroup krb5_principal
 */

KRB5_LIB_FUNCTION krb5_error_code KRB5_LIB_CALL
krb5_make_principal(krb5_context context,
		    krb5_principal *principal,
		    krb5_const_realm realm,
		    ...)
{
    krb5_error_code ret;
    krb5_realm r = NULL;
    va_list ap;
    if(realm == NULL) {
	ret = krb5_get_default_realm(context, &r);
	if(ret)
	    return ret;
	realm = r;
    }
    va_start(ap, realm);
    ret = krb5_build_principal_va(context, principal, strlen(realm), realm, ap);
    va_end(ap);
    if(r)
	free(r);
    return ret;
}

static krb5_error_code
append_component(krb5_context context, krb5_principal p,
		 const char *comp,
		 size_t comp_len)
{
    heim_general_string *tmp;
    size_t len = princ_num_comp(p);

    tmp = realloc(princ_comp(p), (len + 1) * sizeof(*tmp));
    if(tmp == NULL) {
	krb5_set_error_message(context, ENOMEM,
			       N_("malloc: out of memory", ""));
	return ENOMEM;
    }
    princ_comp(p) = tmp;
    princ_ncomp(p, len) = malloc(comp_len + 1);
    if (princ_ncomp(p, len) == NULL) {
	krb5_set_error_message(context, ENOMEM,
			       N_("malloc: out of memory", ""));
	return ENOMEM;
    }
    memcpy (princ_ncomp(p, len), comp, comp_len);
    princ_ncomp(p, len)[comp_len] = '\0';
    princ_num_comp(p)++;
    return 0;
}

static void
va_ext_princ(krb5_context context, krb5_principal p, va_list ap)
{
    while(1){
	const char *s;
	int len;
	len = va_arg(ap, int);
	if(len == 0)
	    break;
	s = va_arg(ap, const char*);
	append_component(context, p, s, len);
    }
}

static void
va_princ(krb5_context context, krb5_principal p, va_list ap)
{
    while(1){
	const char *s;
	s = va_arg(ap, const char*);
	if(s == NULL)
	    break;
	append_component(context, p, s, strlen(s));
    }
}

static krb5_error_code
build_principal(krb5_context context,
		krb5_principal *principal,
		int rlen,
		krb5_const_realm realm,
		void (*func)(krb5_context, krb5_principal, va_list),
		va_list ap)
{
    krb5_principal p;

    p = calloc(1, sizeof(*p));
    if (p == NULL) {
	krb5_set_error_message(context, ENOMEM,
			       N_("malloc: out of memory", ""));
	return ENOMEM;
    }
    princ_type(p) = KRB5_NT_PRINCIPAL;

    princ_realm(p) = strdup(realm);
    if(p->realm == NULL){
	free(p);
	krb5_set_error_message(context, ENOMEM,
			       N_("malloc: out of memory", ""));
	return ENOMEM;
    }

    (*func)(context, p, ap);
    *principal = p;
    return 0;
}

KRB5_LIB_FUNCTION krb5_error_code KRB5_LIB_CALL
krb5_build_principal_va(krb5_context context,
			krb5_principal *principal,
			int rlen,
			krb5_const_realm realm,
			va_list ap)
{
    return build_principal(context, principal, rlen, realm, va_princ, ap);
}

KRB5_LIB_FUNCTION krb5_error_code KRB5_LIB_CALL
krb5_build_principal_va_ext(krb5_context context,
			    krb5_principal *principal,
			    int rlen,
			    krb5_const_realm realm,
			    va_list ap)
{
    return build_principal(context, principal, rlen, realm, va_ext_princ, ap);
}


KRB5_LIB_FUNCTION krb5_error_code KRB5_LIB_CALL
krb5_build_principal_ext(krb5_context context,
			 krb5_principal *principal,
			 int rlen,
			 krb5_const_realm realm,
			 ...)
{
    krb5_error_code ret;
    va_list ap;
    va_start(ap, realm);
    ret = krb5_build_principal_va_ext(context, principal, rlen, realm, ap);
    va_end(ap);
    return ret;
}

/**
 * Copy a principal
 *
 * @param context A Kerberos context.
 * @param inprinc principal to copy
 * @param outprinc copied principal, free with krb5_free_principal()
 *
 * @return An krb5 error code, see krb5_get_error_message().
 *
 * @ingroup krb5_principal
 */


KRB5_LIB_FUNCTION krb5_error_code KRB5_LIB_CALL
krb5_copy_principal(krb5_context context,
		    krb5_const_principal inprinc,
		    krb5_principal *outprinc)
{
    krb5_principal p = malloc(sizeof(*p));
    if (p == NULL) {
	krb5_set_error_message(context, ENOMEM,
			       N_("malloc: out of memory", ""));
	return ENOMEM;
    }
    if(copy_Principal(inprinc, p)) {
	free(p);
	krb5_set_error_message(context, ENOMEM,
			       N_("malloc: out of memory", ""));
	return ENOMEM;
    }
    *outprinc = p;
    return 0;
}

/**
 * Return TRUE iff princ1 == princ2 (without considering the realm)
 *
 * @param context Kerberos 5 context
 * @param princ1 first principal to compare
 * @param princ2 second principal to compare
 *
 * @return non zero if equal, 0 if not
 *
 * @ingroup krb5_principal
 * @see krb5_principal_compare()
 * @see krb5_realm_compare()
 */

KRB5_LIB_FUNCTION krb5_boolean KRB5_LIB_CALL
krb5_principal_compare_any_realm(krb5_context context,
				 krb5_const_principal princ1,
				 krb5_const_principal princ2)
{
    size_t i;
    if(princ_num_comp(princ1) != princ_num_comp(princ2))
	return FALSE;
    for(i = 0; i < princ_num_comp(princ1); i++){
	if(strcmp(princ_ncomp(princ1, i), princ_ncomp(princ2, i)) != 0)
	    return FALSE;
    }
    return TRUE;
}

KRB5_LIB_FUNCTION krb5_boolean KRB5_LIB_CALL
_krb5_principal_compare_PrincipalName(krb5_context context,
				      krb5_const_principal princ1,
				      PrincipalName *princ2)
{
    size_t i;
    if (princ_num_comp(princ1) != princ2->name_string.len)
	return FALSE;
    for(i = 0; i < princ_num_comp(princ1); i++){
	if(strcmp(princ_ncomp(princ1, i), princ2->name_string.val[i]) != 0)
	    return FALSE;
    }
    return TRUE;
}


/**
 * Compares the two principals, including realm of the principals and returns
 * TRUE if they are the same and FALSE if not.
 *
 * @param context Kerberos 5 context
 * @param princ1 first principal to compare
 * @param princ2 second principal to compare
 *
 * @ingroup krb5_principal
 * @see krb5_principal_compare_any_realm()
 * @see krb5_realm_compare()
 */

/*
 * return TRUE iff princ1 == princ2
 */

KRB5_LIB_FUNCTION krb5_boolean KRB5_LIB_CALL
krb5_principal_compare(krb5_context context,
		       krb5_const_principal princ1,
		       krb5_const_principal princ2)
{
    if(!krb5_realm_compare(context, princ1, princ2))
	return FALSE;
    return krb5_principal_compare_any_realm(context, princ1, princ2);
}

/**
 * return TRUE iff realm(princ1) == realm(princ2)
 *
 * @param context Kerberos 5 context
 * @param princ1 first principal to compare
 * @param princ2 second principal to compare
 *
 * @ingroup krb5_principal
 * @see krb5_principal_compare_any_realm()
 * @see krb5_principal_compare()
 */

KRB5_LIB_FUNCTION krb5_boolean KRB5_LIB_CALL
krb5_realm_compare(krb5_context context,
		   krb5_const_principal princ1,
		   krb5_const_principal princ2)
{
    return strcmp(princ_realm(princ1), princ_realm(princ2)) == 0;
}

/**
 * return TRUE iff princ matches pattern
 *
 * @ingroup krb5_principal
 */

KRB5_LIB_FUNCTION krb5_boolean KRB5_LIB_CALL
krb5_principal_match(krb5_context context,
		     krb5_const_principal princ,
		     krb5_const_principal pattern)
{
    size_t i;
    if(princ_num_comp(princ) != princ_num_comp(pattern))
	return FALSE;
    if(fnmatch(princ_realm(pattern), princ_realm(princ), 0) != 0)
	return FALSE;
    for(i = 0; i < princ_num_comp(princ); i++){
	if(fnmatch(princ_ncomp(pattern, i), princ_ncomp(princ, i), 0) != 0)
	    return FALSE;
    }
    return TRUE;
}

/**
 * Create a principal for the service running on hostname. If
 * KRB5_NT_SRV_HST is used, the hostname is canonization using DNS (or
 * some other service), this is potentially insecure.
 *
 * @param context A Kerberos context.
 * @param hostname hostname to use
 * @param sname Service name to use
 * @param type name type of pricipal, use KRB5_NT_SRV_HST or KRB5_NT_UNKNOWN.
 * @param ret_princ return principal, free with krb5_free_principal().
 *
 * @return An krb5 error code, see krb5_get_error_message().
 *
 * @ingroup krb5_principal
 */

KRB5_LIB_FUNCTION krb5_error_code KRB5_LIB_CALL
krb5_sname_to_principal (krb5_context context,
			 const char *hostname,
			 const char *sname,
			 int32_t type,
			 krb5_principal *ret_princ)
{
    krb5_error_code ret;
    char localhost[MAXHOSTNAMELEN];
    char **realms, *host = NULL;

    if(type != KRB5_NT_SRV_HST && type != KRB5_NT_UNKNOWN) {
	krb5_set_error_message(context, KRB5_SNAME_UNSUPP_NAMETYPE,
			       N_("unsupported name type %d", ""),
			       (int)type);
	return KRB5_SNAME_UNSUPP_NAMETYPE;
    }
    if(hostname == NULL) {
	ret = gethostname(localhost, sizeof(localhost) - 1);
	if (ret != 0) {
	    ret = errno;
	    krb5_set_error_message(context, ret,
				   N_("Failed to get local hostname", ""));
	    return ret;
	}
	localhost[sizeof(localhost) - 1] = '\0';
	hostname = localhost;
    }
    if(sname == NULL)
	sname = "host";
    if(type == KRB5_NT_SRV_HST) {
	ret = krb5_expand_hostname_realms (context, hostname,
					   &host, &realms);
	if (ret)
	    return ret;
	strlwr(host);
	hostname = host;
    } else {
	ret = krb5_get_host_realm(context, hostname, &realms);
	if(ret)
	    return ret;
    }

    ret = krb5_make_principal(context, ret_princ, realms[0], sname,
			      hostname, NULL);
    if(host)
	free(host);
    krb5_free_host_realm(context, realms);
    return ret;
}

static const struct {
    const char *type;
    int32_t value;
} nametypes[] = {
    { "UNKNOWN", KRB5_NT_UNKNOWN },
    { "PRINCIPAL", KRB5_NT_PRINCIPAL },
    { "SRV_INST", KRB5_NT_SRV_INST },
    { "SRV_HST", KRB5_NT_SRV_HST },
    { "SRV_XHST", KRB5_NT_SRV_XHST },
    { "UID", KRB5_NT_UID },
    { "X500_PRINCIPAL", KRB5_NT_X500_PRINCIPAL },
    { "SMTP_NAME", KRB5_NT_SMTP_NAME },
    { "ENTERPRISE_PRINCIPAL", KRB5_NT_ENTERPRISE_PRINCIPAL },
    { "ENT_PRINCIPAL_AND_ID", KRB5_NT_ENT_PRINCIPAL_AND_ID },
    { "MS_PRINCIPAL", KRB5_NT_MS_PRINCIPAL },
    { "MS_PRINCIPAL_AND_ID", KRB5_NT_MS_PRINCIPAL_AND_ID },
    { NULL, 0 }
};

/**
 * Parse nametype string and return a nametype integer
 *
 * @ingroup krb5_principal
 */

KRB5_LIB_FUNCTION krb5_error_code KRB5_LIB_CALL
krb5_parse_nametype(krb5_context context, const char *str, int32_t *nametype)
{
    size_t i;

    for(i = 0; nametypes[i].type; i++) {
	if (strcasecmp(nametypes[i].type, str) == 0) {
	    *nametype = nametypes[i].value;
	    return 0;
	}
    }
    krb5_set_error_message(context, KRB5_PARSE_MALFORMED,
			   N_("Failed to find name type %s", ""), str);
    return KRB5_PARSE_MALFORMED;
}

/**
 * Check if the cname part of the principal is a krbtgt principal
 *
 * @ingroup krb5_principal
 */

KRB5_LIB_FUNCTION krb5_boolean KRB5_LIB_CALL
krb5_principal_is_krbtgt(krb5_context context, krb5_const_principal p)
{
    return p->name.name_string.len == 2 &&
	strcmp(p->name.name_string.val[0], KRB5_TGS_NAME) == 0;

}
