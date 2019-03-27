/*
 * Copyright (c) 2001 - 2003 Kungliga Tekniska Högskolan
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
#include "locate_plugin.h"

static int
string_to_proto(const char *string)
{
    if(strcasecmp(string, "udp") == 0)
	return KRB5_KRBHST_UDP;
    else if(strcasecmp(string, "tcp") == 0)
	return KRB5_KRBHST_TCP;
    else if(strcasecmp(string, "http") == 0)
	return KRB5_KRBHST_HTTP;
    return -1;
}

/*
 * set `res' and `count' to the result of looking up SRV RR in DNS for
 * `proto', `proto', `realm' using `dns_type'.
 * if `port' != 0, force that port number
 */

static krb5_error_code
srv_find_realm(krb5_context context, krb5_krbhst_info ***res, int *count,
	       const char *realm, const char *dns_type,
	       const char *proto, const char *service, int port)
{
    char domain[1024];
    struct rk_dns_reply *r;
    struct rk_resource_record *rr;
    int num_srv;
    int proto_num;
    int def_port;

    *res = NULL;
    *count = 0;

    proto_num = string_to_proto(proto);
    if(proto_num < 0) {
	krb5_set_error_message(context, EINVAL,
			       N_("unknown protocol `%s' to lookup", ""),
			       proto);
	return EINVAL;
    }

    if(proto_num == KRB5_KRBHST_HTTP)
	def_port = ntohs(krb5_getportbyname (context, "http", "tcp", 80));
    else if(port == 0)
	def_port = ntohs(krb5_getportbyname (context, service, proto, 88));
    else
	def_port = port;

    snprintf(domain, sizeof(domain), "_%s._%s.%s.", service, proto, realm);

    r = rk_dns_lookup(domain, dns_type);
    if(r == NULL) {
	_krb5_debug(context, 0,
		    "DNS lookup failed domain: %s", domain);
	return KRB5_KDC_UNREACH;
    }

    for(num_srv = 0, rr = r->head; rr; rr = rr->next)
	if(rr->type == rk_ns_t_srv)
	    num_srv++;

    *res = malloc(num_srv * sizeof(**res));
    if(*res == NULL) {
	rk_dns_free_data(r);
	krb5_set_error_message(context, ENOMEM,
			       N_("malloc: out of memory", ""));
	return ENOMEM;
    }

    rk_dns_srv_order(r);

    for(num_srv = 0, rr = r->head; rr; rr = rr->next)
	if(rr->type == rk_ns_t_srv) {
	    krb5_krbhst_info *hi;
	    size_t len = strlen(rr->u.srv->target);

	    hi = calloc(1, sizeof(*hi) + len);
	    if(hi == NULL) {
		rk_dns_free_data(r);
		while(--num_srv >= 0)
		    free((*res)[num_srv]);
		free(*res);
		*res = NULL;
		return ENOMEM;
	    }
	    (*res)[num_srv++] = hi;

	    hi->proto = proto_num;

	    hi->def_port = def_port;
	    if (port != 0)
		hi->port = port;
	    else
		hi->port = rr->u.srv->port;

	    strlcpy(hi->hostname, rr->u.srv->target, len + 1);
	}

    *count = num_srv;

    rk_dns_free_data(r);
    return 0;
}


struct krb5_krbhst_data {
    char *realm;
    unsigned int flags;
    int def_port;
    int port;			/* hardwired port number if != 0 */
#define KD_CONFIG		 1
#define KD_SRV_UDP		 2
#define KD_SRV_TCP		 4
#define KD_SRV_HTTP		 8
#define KD_FALLBACK		16
#define KD_CONFIG_EXISTS	32
#define KD_LARGE_MSG		64
#define KD_PLUGIN	       128
    krb5_error_code (*get_next)(krb5_context, struct krb5_krbhst_data *,
				krb5_krbhst_info**);

    unsigned int fallback_count;

    struct krb5_krbhst_info *hosts, **index, **end;
};

static krb5_boolean
krbhst_empty(const struct krb5_krbhst_data *kd)
{
    return kd->index == &kd->hosts;
}

/*
 * Return the default protocol for the `kd' (either TCP or UDP)
 */

static int
krbhst_get_default_proto(struct krb5_krbhst_data *kd)
{
    if (kd->flags & KD_LARGE_MSG)
	return KRB5_KRBHST_TCP;
    return KRB5_KRBHST_UDP;
}

/*
 *
 */

const char *
_krb5_krbhst_get_realm(krb5_krbhst_handle handle)
{
    return handle->realm;
}

/*
 * parse `spec' into a krb5_krbhst_info, defaulting the port to `def_port'
 * and forcing it to `port' if port != 0
 */

static struct krb5_krbhst_info*
parse_hostspec(krb5_context context, struct krb5_krbhst_data *kd,
	       const char *spec, int def_port, int port)
{
    const char *p = spec, *q;
    struct krb5_krbhst_info *hi;

    hi = calloc(1, sizeof(*hi) + strlen(spec));
    if(hi == NULL)
	return NULL;

    hi->proto = krbhst_get_default_proto(kd);

    if(strncmp(p, "http://", 7) == 0){
	hi->proto = KRB5_KRBHST_HTTP;
	p += 7;
    } else if(strncmp(p, "http/", 5) == 0) {
	hi->proto = KRB5_KRBHST_HTTP;
	p += 5;
	def_port = ntohs(krb5_getportbyname (context, "http", "tcp", 80));
    }else if(strncmp(p, "tcp/", 4) == 0){
	hi->proto = KRB5_KRBHST_TCP;
	p += 4;
    } else if(strncmp(p, "udp/", 4) == 0) {
	p += 4;
    }

    if (p[0] == '[' && (q = strchr(p, ']')) != NULL) {
	/* if address looks like [foo:bar] or [foo:bar]: its a ipv6
	   adress, strip of [] */
	memcpy(hi->hostname, &p[1], q - p - 1);
	hi->hostname[q - p - 1] = '\0';
	p = q + 1;
	/* get trailing : */
	if (p[0] == ':')
	    p++;
    } else if(strsep_copy(&p, ":", hi->hostname, strlen(spec) + 1) < 0) {
	/* copy everything before : */
	free(hi);
	return NULL;
    }
    /* get rid of trailing /, and convert to lower case */
    hi->hostname[strcspn(hi->hostname, "/")] = '\0';
    strlwr(hi->hostname);

    hi->port = hi->def_port = def_port;
    if(p != NULL && p[0]) {
	char *end;
	hi->port = strtol(p, &end, 0);
	if(end == p) {
	    free(hi);
	    return NULL;
	}
    }
    if (port)
	hi->port = port;
    return hi;
}

void
_krb5_free_krbhst_info(krb5_krbhst_info *hi)
{
    if (hi->ai != NULL)
	freeaddrinfo(hi->ai);
    free(hi);
}

krb5_error_code
_krb5_krbhost_info_move(krb5_context context,
			krb5_krbhst_info *from,
			krb5_krbhst_info **to)
{
    size_t hostnamelen = strlen(from->hostname);
    /* trailing NUL is included in structure */
    *to = calloc(1, sizeof(**to) + hostnamelen);
    if(*to == NULL) {
	krb5_set_error_message(context, ENOMEM,
			       N_("malloc: out of memory", ""));
	return ENOMEM;
    }

    (*to)->proto = from->proto;
    (*to)->port = from->port;
    (*to)->def_port = from->def_port;
    (*to)->ai = from->ai;
    from->ai = NULL;
    (*to)->next = NULL;
    memcpy((*to)->hostname, from->hostname, hostnamelen + 1);
    return 0;
}


static void
append_host_hostinfo(struct krb5_krbhst_data *kd, struct krb5_krbhst_info *host)
{
    struct krb5_krbhst_info *h;

    for(h = kd->hosts; h; h = h->next)
	if(h->proto == host->proto &&
	   h->port == host->port &&
	   strcmp(h->hostname, host->hostname) == 0) {
	    _krb5_free_krbhst_info(host);
	    return;
	}
    *kd->end = host;
    kd->end = &host->next;
}

static krb5_error_code
append_host_string(krb5_context context, struct krb5_krbhst_data *kd,
		   const char *host, int def_port, int port)
{
    struct krb5_krbhst_info *hi;

    hi = parse_hostspec(context, kd, host, def_port, port);
    if(hi == NULL)
	return ENOMEM;

    append_host_hostinfo(kd, hi);
    return 0;
}

/*
 * return a readable representation of `host' in `hostname, hostlen'
 */

KRB5_LIB_FUNCTION krb5_error_code KRB5_LIB_CALL
krb5_krbhst_format_string(krb5_context context, const krb5_krbhst_info *host,
			  char *hostname, size_t hostlen)
{
    const char *proto = "";
    char portstr[7] = "";
    if(host->proto == KRB5_KRBHST_TCP)
	proto = "tcp/";
    else if(host->proto == KRB5_KRBHST_HTTP)
	proto = "http://";
    if(host->port != host->def_port)
	snprintf(portstr, sizeof(portstr), ":%d", host->port);
    snprintf(hostname, hostlen, "%s%s%s", proto, host->hostname, portstr);
    return 0;
}

/*
 * create a getaddrinfo `hints' based on `proto'
 */

static void
make_hints(struct addrinfo *hints, int proto)
{
    memset(hints, 0, sizeof(*hints));
    hints->ai_family = AF_UNSPEC;
    switch(proto) {
    case KRB5_KRBHST_UDP :
	hints->ai_socktype = SOCK_DGRAM;
	break;
    case KRB5_KRBHST_HTTP :
    case KRB5_KRBHST_TCP :
	hints->ai_socktype = SOCK_STREAM;
	break;
    }
}

/**
 * Return an `struct addrinfo *' for a KDC host.
 *
 * Returns an the struct addrinfo in in that corresponds to the
 * information in `host'.  free:ing is handled by krb5_krbhst_free, so
 * the returned ai must not be released.
 *
 * @ingroup krb5
 */

KRB5_LIB_FUNCTION krb5_error_code KRB5_LIB_CALL
krb5_krbhst_get_addrinfo(krb5_context context, krb5_krbhst_info *host,
			 struct addrinfo **ai)
{
    int ret = 0;

    if (host->ai == NULL) {
	struct addrinfo hints;
	char portstr[NI_MAXSERV];
	char *hostname = host->hostname;

	snprintf (portstr, sizeof(portstr), "%d", host->port);
	make_hints(&hints, host->proto);

	/**
	 * First try this as an IP address, this allows us to add a
	 * dot at the end to stop using the search domains.
	 */

	hints.ai_flags |= AI_NUMERICHOST | AI_NUMERICSERV;

	ret = getaddrinfo(host->hostname, portstr, &hints, &host->ai);
	if (ret == 0)
	    goto out;

	/**
	 * If the hostname contains a dot, assumes it's a FQDN and
	 * don't use search domains since that might be painfully slow
	 * when machine is disconnected from that network.
	 */

	hints.ai_flags &= ~(AI_NUMERICHOST);

	if (strchr(hostname, '.') && hostname[strlen(hostname) - 1] != '.') {
	    ret = asprintf(&hostname, "%s.", host->hostname);
	    if (ret < 0 || hostname == NULL)
		return ENOMEM;
	}

	ret = getaddrinfo(hostname, portstr, &hints, &host->ai);
	if (hostname != host->hostname)
	    free(hostname);
	if (ret) {
	    ret = krb5_eai_to_heim_errno(ret, errno);
	    goto out;
	}
    }
 out:
    *ai = host->ai;
    return ret;
}

static krb5_boolean
get_next(struct krb5_krbhst_data *kd, krb5_krbhst_info **host)
{
    struct krb5_krbhst_info *hi = *kd->index;
    if(hi != NULL) {
	*host = hi;
	kd->index = &(*kd->index)->next;
	return TRUE;
    }
    return FALSE;
}

static void
srv_get_hosts(krb5_context context, struct krb5_krbhst_data *kd,
	      const char *proto, const char *service)
{
    krb5_error_code ret;
    krb5_krbhst_info **res;
    int count, i;

    ret = srv_find_realm(context, &res, &count, kd->realm, "SRV", proto, service,
			 kd->port);
    _krb5_debug(context, 2, "searching DNS for realm %s %s.%s -> %d",
		kd->realm, proto, service, ret);
    if (ret)
	return;
    for(i = 0; i < count; i++)
	append_host_hostinfo(kd, res[i]);
    free(res);
}

/*
 * read the configuration for `conf_string', defaulting to kd->def_port and
 * forcing it to `kd->port' if kd->port != 0
 */

static void
config_get_hosts(krb5_context context, struct krb5_krbhst_data *kd,
		 const char *conf_string)
{
    int i;
    char **hostlist;
    hostlist = krb5_config_get_strings(context, NULL,
				       "realms", kd->realm, conf_string, NULL);

    _krb5_debug(context, 2, "configuration file for realm %s%s found",
		kd->realm, hostlist ? "" : " not");

    if(hostlist == NULL)
	return;
    kd->flags |= KD_CONFIG_EXISTS;
    for(i = 0; hostlist && hostlist[i] != NULL; i++)
	append_host_string(context, kd, hostlist[i], kd->def_port, kd->port);

    krb5_config_free_strings(hostlist);
}

/*
 * as a fallback, look for `serv_string.kd->realm' (typically
 * kerberos.REALM, kerberos-1.REALM, ...
 * `port' is the default port for the service, and `proto' the
 * protocol
 */

static krb5_error_code
fallback_get_hosts(krb5_context context, struct krb5_krbhst_data *kd,
		   const char *serv_string, int port, int proto)
{
    char *host = NULL;
    int ret;
    struct addrinfo *ai;
    struct addrinfo hints;
    char portstr[NI_MAXSERV];

    _krb5_debug(context, 2, "fallback lookup %d for realm %s (service %s)",
		kd->fallback_count, kd->realm, serv_string);

    /*
     * Don't try forever in case the DNS server keep returning us
     * entries (like wildcard entries or the .nu TLD)
     */
    if(kd->fallback_count >= 5) {
	kd->flags |= KD_FALLBACK;
	return 0;
    }

    if(kd->fallback_count == 0)
	ret = asprintf(&host, "%s.%s.", serv_string, kd->realm);
    else
	ret = asprintf(&host, "%s-%d.%s.",
		       serv_string, kd->fallback_count, kd->realm);

    if (ret < 0 || host == NULL)
	return ENOMEM;

    make_hints(&hints, proto);
    snprintf(portstr, sizeof(portstr), "%d", port);
    ret = getaddrinfo(host, portstr, &hints, &ai);
    if (ret) {
	/* no more hosts, so we're done here */
	free(host);
	kd->flags |= KD_FALLBACK;
    } else {
	struct krb5_krbhst_info *hi;
	size_t hostlen = strlen(host);

	hi = calloc(1, sizeof(*hi) + hostlen);
	if(hi == NULL) {
	    free(host);
	    return ENOMEM;
	}

	hi->proto = proto;
	hi->port  = hi->def_port = port;
	hi->ai    = ai;
	memmove(hi->hostname, host, hostlen);
	hi->hostname[hostlen] = '\0';
	free(host);
	append_host_hostinfo(kd, hi);
	kd->fallback_count++;
    }
    return 0;
}

/*
 * Fetch hosts from plugin
 */

static krb5_error_code
add_locate(void *ctx, int type, struct sockaddr *addr)
{
    struct krb5_krbhst_info *hi;
    struct krb5_krbhst_data *kd = ctx;
    char host[NI_MAXHOST], port[NI_MAXSERV];
    struct addrinfo hints, *ai;
    socklen_t socklen;
    size_t hostlen;
    int ret;

    socklen = socket_sockaddr_size(addr);

    ret = getnameinfo(addr, socklen, host, sizeof(host), port, sizeof(port),
		      NI_NUMERICHOST|NI_NUMERICSERV);
    if (ret != 0)
	return 0;

    make_hints(&hints, krbhst_get_default_proto(kd));
    ret = getaddrinfo(host, port, &hints, &ai);
    if (ret)
	return 0;

    hostlen = strlen(host);

    hi = calloc(1, sizeof(*hi) + hostlen);
    if(hi == NULL)
	return ENOMEM;

    hi->proto = krbhst_get_default_proto(kd);
    hi->port  = hi->def_port = socket_get_port(addr);
    hi->ai    = ai;
    memmove(hi->hostname, host, hostlen);
    hi->hostname[hostlen] = '\0';
    append_host_hostinfo(kd, hi);

    return 0;
}

static void
plugin_get_hosts(krb5_context context,
		 struct krb5_krbhst_data *kd,
		 enum locate_service_type type)
{
    struct krb5_plugin *list = NULL, *e;
    krb5_error_code ret;

    ret = _krb5_plugin_find(context, PLUGIN_TYPE_DATA,
			    KRB5_PLUGIN_LOCATE, &list);
    if(ret != 0 || list == NULL)
	return;

    for (e = list; e != NULL; e = _krb5_plugin_get_next(e)) {
	krb5plugin_service_locate_ftable *service;
	void *ctx;

	service = _krb5_plugin_get_symbol(e);
	if (service->minor_version != 0)
	    continue;

	(*service->init)(context, &ctx);
	ret = (*service->lookup)(ctx, type, kd->realm, 0, 0, add_locate, kd);
	(*service->fini)(ctx);
	if (ret && ret != KRB5_PLUGIN_NO_HANDLE) {
	    krb5_set_error_message(context, ret,
				   N_("Locate plugin failed to lookup realm %s: %d", ""),
				   kd->realm, ret);
	    break;
	} else if (ret == 0) {
	    _krb5_debug(context, 2, "plugin found result for realm %s", kd->realm);
	    kd->flags |= KD_CONFIG_EXISTS;
	}

    }
    _krb5_plugin_free(list);
}

/*
 *
 */

static krb5_error_code
kdc_get_next(krb5_context context,
	     struct krb5_krbhst_data *kd,
	     krb5_krbhst_info **host)
{
    krb5_error_code ret;

    if ((kd->flags & KD_PLUGIN) == 0) {
	plugin_get_hosts(context, kd, locate_service_kdc);
	kd->flags |= KD_PLUGIN;
	if(get_next(kd, host))
	    return 0;
    }

    if((kd->flags & KD_CONFIG) == 0) {
	config_get_hosts(context, kd, "kdc");
	kd->flags |= KD_CONFIG;
	if(get_next(kd, host))
	    return 0;
    }

    if (kd->flags & KD_CONFIG_EXISTS) {
	_krb5_debug(context, 1,
		    "Configuration exists for realm %s, wont go to DNS",
		    kd->realm);
	return KRB5_KDC_UNREACH;
    }

    if(context->srv_lookup) {
	if((kd->flags & KD_SRV_UDP) == 0 && (kd->flags & KD_LARGE_MSG) == 0) {
	    srv_get_hosts(context, kd, "udp", "kerberos");
	    kd->flags |= KD_SRV_UDP;
	    if(get_next(kd, host))
		return 0;
	}

	if((kd->flags & KD_SRV_TCP) == 0) {
	    srv_get_hosts(context, kd, "tcp", "kerberos");
	    kd->flags |= KD_SRV_TCP;
	    if(get_next(kd, host))
		return 0;
	}
	if((kd->flags & KD_SRV_HTTP) == 0) {
	    srv_get_hosts(context, kd, "http", "kerberos");
	    kd->flags |= KD_SRV_HTTP;
	    if(get_next(kd, host))
		return 0;
	}
    }

    while((kd->flags & KD_FALLBACK) == 0) {
	ret = fallback_get_hosts(context, kd, "kerberos",
				 kd->def_port,
				 krbhst_get_default_proto(kd));
	if(ret)
	    return ret;
	if(get_next(kd, host))
	    return 0;
    }

    _krb5_debug(context, 0, "No KDC entries found for %s", kd->realm);

    return KRB5_KDC_UNREACH; /* XXX */
}

static krb5_error_code
admin_get_next(krb5_context context,
	       struct krb5_krbhst_data *kd,
	       krb5_krbhst_info **host)
{
    krb5_error_code ret;

    if ((kd->flags & KD_PLUGIN) == 0) {
	plugin_get_hosts(context, kd, locate_service_kadmin);
	kd->flags |= KD_PLUGIN;
	if(get_next(kd, host))
	    return 0;
    }

    if((kd->flags & KD_CONFIG) == 0) {
	config_get_hosts(context, kd, "admin_server");
	kd->flags |= KD_CONFIG;
	if(get_next(kd, host))
	    return 0;
    }

    if (kd->flags & KD_CONFIG_EXISTS) {
	_krb5_debug(context, 1,
		    "Configuration exists for realm %s, wont go to DNS",
		    kd->realm);
	return KRB5_KDC_UNREACH;
    }

    if(context->srv_lookup) {
	if((kd->flags & KD_SRV_TCP) == 0) {
	    srv_get_hosts(context, kd, "tcp", "kerberos-adm");
	    kd->flags |= KD_SRV_TCP;
	    if(get_next(kd, host))
		return 0;
	}
    }

    if (krbhst_empty(kd)
	&& (kd->flags & KD_FALLBACK) == 0) {
	ret = fallback_get_hosts(context, kd, "kerberos",
				 kd->def_port,
				 krbhst_get_default_proto(kd));
	if(ret)
	    return ret;
	kd->flags |= KD_FALLBACK;
	if(get_next(kd, host))
	    return 0;
    }

    _krb5_debug(context, 0, "No admin entries found for realm %s", kd->realm);

    return KRB5_KDC_UNREACH;	/* XXX */
}

static krb5_error_code
kpasswd_get_next(krb5_context context,
		 struct krb5_krbhst_data *kd,
		 krb5_krbhst_info **host)
{
    krb5_error_code ret;

    if ((kd->flags & KD_PLUGIN) == 0) {
	plugin_get_hosts(context, kd, locate_service_kpasswd);
	kd->flags |= KD_PLUGIN;
	if(get_next(kd, host))
	    return 0;
    }

    if((kd->flags & KD_CONFIG) == 0) {
	config_get_hosts(context, kd, "kpasswd_server");
	kd->flags |= KD_CONFIG;
	if(get_next(kd, host))
	    return 0;
    }

    if (kd->flags & KD_CONFIG_EXISTS) {
	_krb5_debug(context, 1,
		    "Configuration exists for realm %s, wont go to DNS",
		    kd->realm);
	return KRB5_KDC_UNREACH;
    }

    if(context->srv_lookup) {
	if((kd->flags & KD_SRV_UDP) == 0) {
	    srv_get_hosts(context, kd, "udp", "kpasswd");
	    kd->flags |= KD_SRV_UDP;
	    if(get_next(kd, host))
		return 0;
	}
	if((kd->flags & KD_SRV_TCP) == 0) {
	    srv_get_hosts(context, kd, "tcp", "kpasswd");
	    kd->flags |= KD_SRV_TCP;
	    if(get_next(kd, host))
		return 0;
	}
    }

    /* no matches -> try admin */

    if (krbhst_empty(kd)) {
	kd->flags = 0;
	kd->port  = kd->def_port;
	kd->get_next = admin_get_next;
	ret = (*kd->get_next)(context, kd, host);
	if (ret == 0)
	    (*host)->proto = krbhst_get_default_proto(kd);
	return ret;
    }

    _krb5_debug(context, 0, "No kpasswd entries found for realm %s", kd->realm);

    return KRB5_KDC_UNREACH;
}

static krb5_error_code
krb524_get_next(krb5_context context,
		struct krb5_krbhst_data *kd,
		krb5_krbhst_info **host)
{
    if ((kd->flags & KD_PLUGIN) == 0) {
	plugin_get_hosts(context, kd, locate_service_krb524);
	kd->flags |= KD_PLUGIN;
	if(get_next(kd, host))
	    return 0;
    }

    if((kd->flags & KD_CONFIG) == 0) {
	config_get_hosts(context, kd, "krb524_server");
	if(get_next(kd, host))
	    return 0;
	kd->flags |= KD_CONFIG;
    }

    if (kd->flags & KD_CONFIG_EXISTS) {
	_krb5_debug(context, 1,
		    "Configuration exists for realm %s, wont go to DNS",
		    kd->realm);
	return KRB5_KDC_UNREACH;
    }

    if(context->srv_lookup) {
	if((kd->flags & KD_SRV_UDP) == 0) {
	    srv_get_hosts(context, kd, "udp", "krb524");
	    kd->flags |= KD_SRV_UDP;
	    if(get_next(kd, host))
		return 0;
	}

	if((kd->flags & KD_SRV_TCP) == 0) {
	    srv_get_hosts(context, kd, "tcp", "krb524");
	    kd->flags |= KD_SRV_TCP;
	    if(get_next(kd, host))
		return 0;
	}
    }

    /* no matches -> try kdc */

    if (krbhst_empty(kd)) {
	kd->flags = 0;
	kd->port  = kd->def_port;
	kd->get_next = kdc_get_next;
	return (*kd->get_next)(context, kd, host);
    }

    _krb5_debug(context, 0, "No kpasswd entries found for realm %s", kd->realm);

    return KRB5_KDC_UNREACH;
}

static struct krb5_krbhst_data*
common_init(krb5_context context,
	    const char *service,
	    const char *realm,
	    int flags)
{
    struct krb5_krbhst_data *kd;

    if((kd = calloc(1, sizeof(*kd))) == NULL)
	return NULL;

    if((kd->realm = strdup(realm)) == NULL) {
	free(kd);
	return NULL;
    }

    _krb5_debug(context, 2, "Trying to find service %s for realm %s flags %x",
		service, realm, flags);

    /* For 'realms' without a . do not even think of going to DNS */
    if (!strchr(realm, '.'))
	kd->flags |= KD_CONFIG_EXISTS;

    if (flags & KRB5_KRBHST_FLAGS_LARGE_MSG)
	kd->flags |= KD_LARGE_MSG;
    kd->end = kd->index = &kd->hosts;
    return kd;
}

/*
 * initialize `handle' to look for hosts of type `type' in realm `realm'
 */

KRB5_LIB_FUNCTION krb5_error_code KRB5_LIB_CALL
krb5_krbhst_init(krb5_context context,
		 const char *realm,
		 unsigned int type,
		 krb5_krbhst_handle *handle)
{
    return krb5_krbhst_init_flags(context, realm, type, 0, handle);
}

KRB5_LIB_FUNCTION krb5_error_code KRB5_LIB_CALL
krb5_krbhst_init_flags(krb5_context context,
		       const char *realm,
		       unsigned int type,
		       int flags,
		       krb5_krbhst_handle *handle)
{
    struct krb5_krbhst_data *kd;
    krb5_error_code (*next)(krb5_context, struct krb5_krbhst_data *,
			    krb5_krbhst_info **);
    int def_port;
    const char *service;

    switch(type) {
    case KRB5_KRBHST_KDC:
	next = kdc_get_next;
	def_port = ntohs(krb5_getportbyname (context, "kerberos", "udp", 88));
	service = "kdc";
	break;
    case KRB5_KRBHST_ADMIN:
	next = admin_get_next;
	def_port = ntohs(krb5_getportbyname (context, "kerberos-adm",
					     "tcp", 749));
	service = "admin";
	break;
    case KRB5_KRBHST_CHANGEPW:
	next = kpasswd_get_next;
	def_port = ntohs(krb5_getportbyname (context, "kpasswd", "udp",
					     KPASSWD_PORT));
	service = "change_password";
	break;
    case KRB5_KRBHST_KRB524:
	next = krb524_get_next;
	def_port = ntohs(krb5_getportbyname (context, "krb524", "udp", 4444));
	service = "524";
	break;
    default:
	krb5_set_error_message(context, ENOTTY,
			       N_("unknown krbhst type (%u)", ""), type);
	return ENOTTY;
    }
    if((kd = common_init(context, service, realm, flags)) == NULL)
	return ENOMEM;
    kd->get_next = next;
    kd->def_port = def_port;
    *handle = kd;
    return 0;
}

/*
 * return the next host information from `handle' in `host'
 */

KRB5_LIB_FUNCTION krb5_error_code KRB5_LIB_CALL
krb5_krbhst_next(krb5_context context,
		 krb5_krbhst_handle handle,
		 krb5_krbhst_info **host)
{
    if(get_next(handle, host))
	return 0;

    return (*handle->get_next)(context, handle, host);
}

/*
 * return the next host information from `handle' as a host name
 * in `hostname' (or length `hostlen)
 */

KRB5_LIB_FUNCTION krb5_error_code KRB5_LIB_CALL
krb5_krbhst_next_as_string(krb5_context context,
			   krb5_krbhst_handle handle,
			   char *hostname,
			   size_t hostlen)
{
    krb5_error_code ret;
    krb5_krbhst_info *host;
    ret = krb5_krbhst_next(context, handle, &host);
    if(ret)
	return ret;
    return krb5_krbhst_format_string(context, host, hostname, hostlen);
}


KRB5_LIB_FUNCTION void KRB5_LIB_CALL
krb5_krbhst_reset(krb5_context context, krb5_krbhst_handle handle)
{
    handle->index = &handle->hosts;
}

KRB5_LIB_FUNCTION void KRB5_LIB_CALL
krb5_krbhst_free(krb5_context context, krb5_krbhst_handle handle)
{
    krb5_krbhst_info *h, *next;

    if (handle == NULL)
	return;

    for (h = handle->hosts; h != NULL; h = next) {
	next = h->next;
	_krb5_free_krbhst_info(h);
    }

    free(handle->realm);
    free(handle);
}

/* backwards compatibility ahead */

static krb5_error_code
gethostlist(krb5_context context, const char *realm,
	    unsigned int type, char ***hostlist)
{
    krb5_error_code ret;
    int nhost = 0;
    krb5_krbhst_handle handle;
    char host[MAXHOSTNAMELEN];
    krb5_krbhst_info *hostinfo;

    ret = krb5_krbhst_init(context, realm, type, &handle);
    if (ret)
	return ret;

    while(krb5_krbhst_next(context, handle, &hostinfo) == 0)
	nhost++;
    if(nhost == 0) {
	krb5_set_error_message(context, KRB5_KDC_UNREACH,
			       N_("No KDC found for realm %s", ""), realm);
	return KRB5_KDC_UNREACH;
    }
    *hostlist = calloc(nhost + 1, sizeof(**hostlist));
    if(*hostlist == NULL) {
	krb5_krbhst_free(context, handle);
	return ENOMEM;
    }

    krb5_krbhst_reset(context, handle);
    nhost = 0;
    while(krb5_krbhst_next_as_string(context, handle,
				     host, sizeof(host)) == 0) {
	if(((*hostlist)[nhost++] = strdup(host)) == NULL) {
	    krb5_free_krbhst(context, *hostlist);
	    krb5_krbhst_free(context, handle);
	    return ENOMEM;
	}
    }
    (*hostlist)[nhost] = NULL;
    krb5_krbhst_free(context, handle);
    return 0;
}

/*
 * return an malloced list of kadmin-hosts for `realm' in `hostlist'
 */

KRB5_LIB_FUNCTION krb5_error_code KRB5_LIB_CALL
krb5_get_krb_admin_hst (krb5_context context,
			const krb5_realm *realm,
			char ***hostlist)
{
    return gethostlist(context, *realm, KRB5_KRBHST_ADMIN, hostlist);
}

/*
 * return an malloced list of changepw-hosts for `realm' in `hostlist'
 */

KRB5_LIB_FUNCTION krb5_error_code KRB5_LIB_CALL
krb5_get_krb_changepw_hst (krb5_context context,
			   const krb5_realm *realm,
			   char ***hostlist)
{
    return gethostlist(context, *realm, KRB5_KRBHST_CHANGEPW, hostlist);
}

/*
 * return an malloced list of 524-hosts for `realm' in `hostlist'
 */

KRB5_LIB_FUNCTION krb5_error_code KRB5_LIB_CALL
krb5_get_krb524hst (krb5_context context,
		    const krb5_realm *realm,
		    char ***hostlist)
{
    return gethostlist(context, *realm, KRB5_KRBHST_KRB524, hostlist);
}


/*
 * return an malloced list of KDC's for `realm' in `hostlist'
 */

KRB5_LIB_FUNCTION krb5_error_code KRB5_LIB_CALL
krb5_get_krbhst (krb5_context context,
		 const krb5_realm *realm,
		 char ***hostlist)
{
    return gethostlist(context, *realm, KRB5_KRBHST_KDC, hostlist);
}

/*
 * free all the memory allocated in `hostlist'
 */

KRB5_LIB_FUNCTION krb5_error_code KRB5_LIB_CALL
krb5_free_krbhst (krb5_context context,
		  char **hostlist)
{
    char **p;

    for (p = hostlist; *p; ++p)
	free (*p);
    free (hostlist);
    return 0;
}
