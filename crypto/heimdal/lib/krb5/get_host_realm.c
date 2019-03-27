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
#include <resolve.h>

/* To automagically find the correct realm of a host (without
 * [domain_realm] in krb5.conf) add a text record for your domain with
 * the name of your realm, like this:
 *
 * _kerberos	IN	TXT	"FOO.SE"
 *
 * The search is recursive, so you can add entries for specific
 * hosts. To find the realm of host a.b.c, it first tries
 * _kerberos.a.b.c, then _kerberos.b.c and so on.
 *
 * This method is described in draft-ietf-cat-krb-dns-locate-03.txt.
 *
 */

static int
copy_txt_to_realms (struct rk_resource_record *head,
		    krb5_realm **realms)
{
    struct rk_resource_record *rr;
    unsigned int n, i;

    for(n = 0, rr = head; rr; rr = rr->next)
	if (rr->type == rk_ns_t_txt)
	    ++n;

    if (n == 0)
	return -1;

    *realms = malloc ((n + 1) * sizeof(krb5_realm));
    if (*realms == NULL)
	return -1;

    for (i = 0; i < n + 1; ++i)
	(*realms)[i] = NULL;

    for (i = 0, rr = head; rr; rr = rr->next) {
	if (rr->type == rk_ns_t_txt) {
	    char *tmp;

	    tmp = strdup(rr->u.txt);
	    if (tmp == NULL) {
		for (i = 0; i < n; ++i)
		    free ((*realms)[i]);
		free (*realms);
		return -1;
	    }
	    (*realms)[i] = tmp;
	    ++i;
	}
    }
    return 0;
}

static int
dns_find_realm(krb5_context context,
	       const char *domain,
	       krb5_realm **realms)
{
    static const char *default_labels[] = { "_kerberos", NULL };
    char dom[MAXHOSTNAMELEN];
    struct rk_dns_reply *r;
    const char **labels;
    char **config_labels;
    int i, ret;

    config_labels = krb5_config_get_strings(context, NULL, "libdefaults",
					    "dns_lookup_realm_labels", NULL);
    if(config_labels != NULL)
	labels = (const char **)config_labels;
    else
	labels = default_labels;
    if(*domain == '.')
	domain++;
    for (i = 0; labels[i] != NULL; i++) {
	ret = snprintf(dom, sizeof(dom), "%s.%s.", labels[i], domain);
	if(ret < 0 || (size_t)ret >= sizeof(dom)) {
	    if (config_labels)
		krb5_config_free_strings(config_labels);
	    return -1;
	}
    	r = rk_dns_lookup(dom, "TXT");
    	if(r != NULL) {
	    ret = copy_txt_to_realms (r->head, realms);
	    rk_dns_free_data(r);
	    if(ret == 0) {
		if (config_labels)
		    krb5_config_free_strings(config_labels);
		return 0;
	    }
	}
    }
    if (config_labels)
	krb5_config_free_strings(config_labels);
    return -1;
}

/*
 * Try to figure out what realms host in `domain' belong to from the
 * configuration file.
 */

static int
config_find_realm(krb5_context context,
		  const char *domain,
		  krb5_realm **realms)
{
    char **tmp = krb5_config_get_strings (context, NULL,
					  "domain_realm",
					  domain,
					  NULL);

    if (tmp == NULL)
	return -1;
    *realms = tmp;
    return 0;
}

/*
 * This function assumes that `host' is a FQDN (and doesn't handle the
 * special case of host == NULL either).
 * Try to find mapping in the config file or DNS and it that fails,
 * fall back to guessing
 */

KRB5_LIB_FUNCTION krb5_error_code KRB5_LIB_CALL
_krb5_get_host_realm_int (krb5_context context,
			  const char *host,
			  krb5_boolean use_dns,
			  krb5_realm **realms)
{
    const char *p, *q;
    krb5_boolean dns_locate_enable;

    dns_locate_enable = krb5_config_get_bool_default(context, NULL, TRUE,
	"libdefaults", "dns_lookup_realm", NULL);
    for (p = host; p != NULL; p = strchr (p + 1, '.')) {
	if(config_find_realm(context, p, realms) == 0) {
	    if(strcasecmp(*realms[0], "dns_locate") == 0) {
		if(use_dns)
		    for (q = host; q != NULL; q = strchr(q + 1, '.'))
			if(dns_find_realm(context, q, realms) == 0)
			    return 0;
		continue;
	    } else
	    	return 0;
	}
	else if(use_dns && dns_locate_enable) {
	    if(dns_find_realm(context, p, realms) == 0)
		return 0;
	}
    }
    p = strchr(host, '.');
    if(p != NULL) {
	p++;
	*realms = malloc(2 * sizeof(krb5_realm));
	if (*realms == NULL) {
	    krb5_set_error_message(context, ENOMEM, N_("malloc: out of memory", ""));
	    return ENOMEM;
	}

	(*realms)[0] = strdup(p);
	if((*realms)[0] == NULL) {
	    free(*realms);
	    krb5_set_error_message(context, ENOMEM, N_("malloc: out of memory", ""));
	    return ENOMEM;
	}
	strupr((*realms)[0]);
	(*realms)[1] = NULL;
	return 0;
    }
    krb5_set_error_message(context, KRB5_ERR_HOST_REALM_UNKNOWN,
			   N_("unable to find realm of host %s", ""),
			   host);
    return KRB5_ERR_HOST_REALM_UNKNOWN;
}

/*
 * Return the realm(s) of `host' as a NULL-terminated list in
 * `realms'. Free `realms' with krb5_free_host_realm().
 */

KRB5_LIB_FUNCTION krb5_error_code KRB5_LIB_CALL
krb5_get_host_realm(krb5_context context,
		    const char *targethost,
		    krb5_realm **realms)
{
    const char *host = targethost;
    char hostname[MAXHOSTNAMELEN];
    krb5_error_code ret;
    int use_dns;

    if (host == NULL) {
	if (gethostname (hostname, sizeof(hostname))) {
	    *realms = NULL;
	    return errno;
	}
	host = hostname;
    }

    /*
     * If our local hostname is without components, don't even try to dns.
     */

    use_dns = (strchr(host, '.') != NULL);

    ret = _krb5_get_host_realm_int (context, host, use_dns, realms);
    if (ret && targethost != NULL) {
	/*
	 * If there was no realm mapping for the host (and we wasn't
	 * looking for ourself), guess at the local realm, maybe our
	 * KDC knows better then we do and we get a referral back.
	 */
	ret = krb5_get_default_realms(context, realms);
	if (ret) {
	    krb5_set_error_message(context, KRB5_ERR_HOST_REALM_UNKNOWN,
				   N_("Unable to find realm of host %s", ""),
				   host);
	    return KRB5_ERR_HOST_REALM_UNKNOWN;
	}
    }
    return ret;
}
