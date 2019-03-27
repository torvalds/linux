/*
 * Copyright (c) 1999 - 2001 Kungliga Tekniska Högskolan
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

static krb5_error_code
copy_hostname(krb5_context context,
	      const char *orig_hostname,
	      char **new_hostname)
{
    *new_hostname = strdup (orig_hostname);
    if (*new_hostname == NULL) {
	krb5_set_error_message(context, ENOMEM,
			       N_("malloc: out of memory", ""));
	return ENOMEM;
    }
    strlwr (*new_hostname);
    return 0;
}

/**
 * krb5_expand_hostname() tries to make orig_hostname into a more
 * canonical one in the newly allocated space returned in
 * new_hostname.

 * @param context a Keberos context
 * @param orig_hostname hostname to canonicalise.
 * @param new_hostname output hostname, caller must free hostname with
 *        krb5_xfree().
 *
 * @return Return an error code or 0, see krb5_get_error_message().
 *
 * @ingroup krb5_support
 */

KRB5_LIB_FUNCTION krb5_error_code KRB5_LIB_CALL
krb5_expand_hostname (krb5_context context,
		      const char *orig_hostname,
		      char **new_hostname)
{
    struct addrinfo *ai, *a, hints;
    int error;

    if ((context->flags & KRB5_CTX_F_DNS_CANONICALIZE_HOSTNAME) == 0)
	return copy_hostname (context, orig_hostname, new_hostname);

    memset (&hints, 0, sizeof(hints));
    hints.ai_flags = AI_CANONNAME;

    error = getaddrinfo (orig_hostname, NULL, &hints, &ai);
    if (error)
	return copy_hostname (context, orig_hostname, new_hostname);
    for (a = ai; a != NULL; a = a->ai_next) {
	if (a->ai_canonname != NULL) {
	    *new_hostname = strdup (a->ai_canonname);
	    freeaddrinfo (ai);
	    if (*new_hostname == NULL) {
		krb5_set_error_message(context, ENOMEM,
				       N_("malloc: out of memory", ""));
		return ENOMEM;
	    } else {
		return 0;
	    }
	}
    }
    freeaddrinfo (ai);
    return copy_hostname (context, orig_hostname, new_hostname);
}

/*
 * handle the case of the hostname being unresolvable and thus identical
 */

static krb5_error_code
vanilla_hostname (krb5_context context,
		  const char *orig_hostname,
		  char **new_hostname,
		  char ***realms)
{
    krb5_error_code ret;

    ret = copy_hostname (context, orig_hostname, new_hostname);
    if (ret)
	return ret;
    strlwr (*new_hostname);

    ret = krb5_get_host_realm (context, *new_hostname, realms);
    if (ret) {
	free (*new_hostname);
	return ret;
    }
    return 0;
}

/**
 * krb5_expand_hostname_realms() expands orig_hostname to a name we
 * believe to be a hostname in newly allocated space in new_hostname
 * and return the realms new_hostname is believed to belong to in
 * realms.
 *
 * @param context a Keberos context
 * @param orig_hostname hostname to canonicalise.
 * @param new_hostname output hostname, caller must free hostname with
 *        krb5_xfree().
 * @param realms output possible realms, is an array that is terminated
 *        with NULL. Caller must free with krb5_free_host_realm().
 *
 * @return Return an error code or 0, see krb5_get_error_message().
 *
 * @ingroup krb5_support
 */

KRB5_LIB_FUNCTION krb5_error_code KRB5_LIB_CALL
krb5_expand_hostname_realms (krb5_context context,
			     const char *orig_hostname,
			     char **new_hostname,
			     char ***realms)
{
    struct addrinfo *ai, *a, hints;
    int error;
    krb5_error_code ret = 0;

    if ((context->flags & KRB5_CTX_F_DNS_CANONICALIZE_HOSTNAME) == 0)
	return vanilla_hostname (context, orig_hostname, new_hostname,
				 realms);

    memset (&hints, 0, sizeof(hints));
    hints.ai_flags = AI_CANONNAME;

    error = getaddrinfo (orig_hostname, NULL, &hints, &ai);
    if (error)
	return vanilla_hostname (context, orig_hostname, new_hostname,
				 realms);

    for (a = ai; a != NULL; a = a->ai_next) {
	if (a->ai_canonname != NULL) {
	    ret = copy_hostname (context, a->ai_canonname, new_hostname);
	    if (ret) {
		freeaddrinfo (ai);
		return ret;
	    }
	    strlwr (*new_hostname);
	    ret = krb5_get_host_realm (context, *new_hostname, realms);
	    if (ret == 0) {
		freeaddrinfo (ai);
		return 0;
	    }
	    free (*new_hostname);
	}
    }
    freeaddrinfo(ai);
    return vanilla_hostname (context, orig_hostname, new_hostname, realms);
}
