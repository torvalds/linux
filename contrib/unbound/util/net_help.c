/*
 * util/net_help.c - implementation of the network helper code
 *
 * Copyright (c) 2007, NLnet Labs. All rights reserved.
 *
 * This software is open source.
 * 
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 
 * Redistributions of source code must retain the above copyright notice,
 * this list of conditions and the following disclaimer.
 * 
 * Redistributions in binary form must reproduce the above copyright notice,
 * this list of conditions and the following disclaimer in the documentation
 * and/or other materials provided with the distribution.
 * 
 * Neither the name of the NLNET LABS nor the names of its contributors may
 * be used to endorse or promote products derived from this software without
 * specific prior written permission.
 * 
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED
 * TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
 * PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
 * LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
 * NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */
/**
 * \file
 * Implementation of net_help.h.
 */

#include "config.h"
#include "util/net_help.h"
#include "util/log.h"
#include "util/data/dname.h"
#include "util/module.h"
#include "util/regional.h"
#include "sldns/parseutil.h"
#include "sldns/wire2str.h"
#include <fcntl.h>
#ifdef HAVE_OPENSSL_SSL_H
#include <openssl/ssl.h>
#endif
#ifdef HAVE_OPENSSL_ERR_H
#include <openssl/err.h>
#endif
#ifdef USE_WINSOCK
#include <wincrypt.h>
#endif

/** max length of an IP address (the address portion) that we allow */
#define MAX_ADDR_STRLEN 128 /* characters */
/** default value for EDNS ADVERTISED size */
uint16_t EDNS_ADVERTISED_SIZE = 4096;

/** minimal responses when positive answer: default is no */
int MINIMAL_RESPONSES = 0;

/** rrset order roundrobin: default is no */
int RRSET_ROUNDROBIN = 0;

/* returns true is string addr is an ip6 specced address */
int
str_is_ip6(const char* str)
{
	if(strchr(str, ':'))
		return 1;
	else    return 0;
}

int 
fd_set_nonblock(int s) 
{
#ifdef HAVE_FCNTL
	int flag;
	if((flag = fcntl(s, F_GETFL)) == -1) {
		log_err("can't fcntl F_GETFL: %s", strerror(errno));
		flag = 0;
	}
	flag |= O_NONBLOCK;
	if(fcntl(s, F_SETFL, flag) == -1) {
		log_err("can't fcntl F_SETFL: %s", strerror(errno));
		return 0;
	}
#elif defined(HAVE_IOCTLSOCKET)
	unsigned long on = 1;
	if(ioctlsocket(s, FIONBIO, &on) != 0) {
		log_err("can't ioctlsocket FIONBIO on: %s", 
			wsa_strerror(WSAGetLastError()));
	}
#endif
	return 1;
}

int 
fd_set_block(int s) 
{
#ifdef HAVE_FCNTL
	int flag;
	if((flag = fcntl(s, F_GETFL)) == -1) {
		log_err("cannot fcntl F_GETFL: %s", strerror(errno));
		flag = 0;
	}
	flag &= ~O_NONBLOCK;
	if(fcntl(s, F_SETFL, flag) == -1) {
		log_err("cannot fcntl F_SETFL: %s", strerror(errno));
		return 0;
	}
#elif defined(HAVE_IOCTLSOCKET)
	unsigned long off = 0;
	if(ioctlsocket(s, FIONBIO, &off) != 0) {
		if(WSAGetLastError() != WSAEINVAL || verbosity >= 4)
			log_err("can't ioctlsocket FIONBIO off: %s", 
				wsa_strerror(WSAGetLastError()));
	}
#endif	
	return 1;
}

int 
is_pow2(size_t num)
{
	if(num == 0) return 1;
	return (num & (num-1)) == 0;
}

void* 
memdup(void* data, size_t len)
{
	void* d;
	if(!data) return NULL;
	if(len == 0) return NULL;
	d = malloc(len);
	if(!d) return NULL;
	memcpy(d, data, len);
	return d;
}

void
log_addr(enum verbosity_value v, const char* str, 
	struct sockaddr_storage* addr, socklen_t addrlen)
{
	uint16_t port;
	const char* family = "unknown";
	char dest[100];
	int af = (int)((struct sockaddr_in*)addr)->sin_family;
	void* sinaddr = &((struct sockaddr_in*)addr)->sin_addr;
	if(verbosity < v)
		return;
	switch(af) {
		case AF_INET: family="ip4"; break;
		case AF_INET6: family="ip6";
			sinaddr = &((struct sockaddr_in6*)addr)->sin6_addr;
			break;
		case AF_LOCAL:
			dest[0]=0;
			(void)inet_ntop(af, sinaddr, dest,
				(socklen_t)sizeof(dest));
			verbose(v, "%s local %s", str, dest);
			return; /* do not continue and try to get port */
		default: break;
	}
	if(inet_ntop(af, sinaddr, dest, (socklen_t)sizeof(dest)) == 0) {
		(void)strlcpy(dest, "(inet_ntop error)", sizeof(dest));
	}
	dest[sizeof(dest)-1] = 0;
	port = ntohs(((struct sockaddr_in*)addr)->sin_port);
	if(verbosity >= 4)
		verbose(v, "%s %s %s port %d (len %d)", str, family, dest, 
			(int)port, (int)addrlen);
	else	verbose(v, "%s %s port %d", str, dest, (int)port);
}

int 
extstrtoaddr(const char* str, struct sockaddr_storage* addr,
	socklen_t* addrlen)
{
	char* s;
	int port = UNBOUND_DNS_PORT;
	if((s=strchr(str, '@'))) {
		char buf[MAX_ADDR_STRLEN];
		if(s-str >= MAX_ADDR_STRLEN) {
			return 0;
		}
		(void)strlcpy(buf, str, sizeof(buf));
		buf[s-str] = 0;
		port = atoi(s+1);
		if(port == 0 && strcmp(s+1,"0")!=0) {
			return 0;
		}
		return ipstrtoaddr(buf, port, addr, addrlen);
	}
	return ipstrtoaddr(str, port, addr, addrlen);
}


int 
ipstrtoaddr(const char* ip, int port, struct sockaddr_storage* addr,
	socklen_t* addrlen)
{
	uint16_t p;
	if(!ip) return 0;
	p = (uint16_t) port;
	if(str_is_ip6(ip)) {
		char buf[MAX_ADDR_STRLEN];
		char* s;
		struct sockaddr_in6* sa = (struct sockaddr_in6*)addr;
		*addrlen = (socklen_t)sizeof(struct sockaddr_in6);
		memset(sa, 0, *addrlen);
		sa->sin6_family = AF_INET6;
		sa->sin6_port = (in_port_t)htons(p);
		if((s=strchr(ip, '%'))) { /* ip6%interface, rfc 4007 */
			if(s-ip >= MAX_ADDR_STRLEN)
				return 0;
			(void)strlcpy(buf, ip, sizeof(buf));
			buf[s-ip]=0;
			sa->sin6_scope_id = (uint32_t)atoi(s+1);
			ip = buf;
		}
		if(inet_pton((int)sa->sin6_family, ip, &sa->sin6_addr) <= 0) {
			return 0;
		}
	} else { /* ip4 */
		struct sockaddr_in* sa = (struct sockaddr_in*)addr;
		*addrlen = (socklen_t)sizeof(struct sockaddr_in);
		memset(sa, 0, *addrlen);
		sa->sin_family = AF_INET;
		sa->sin_port = (in_port_t)htons(p);
		if(inet_pton((int)sa->sin_family, ip, &sa->sin_addr) <= 0) {
			return 0;
		}
	}
	return 1;
}

int netblockstrtoaddr(const char* str, int port, struct sockaddr_storage* addr,
        socklen_t* addrlen, int* net)
{
	char buf[64];
	char* s;
	*net = (str_is_ip6(str)?128:32);
	if((s=strchr(str, '/'))) {
		if(atoi(s+1) > *net) {
			log_err("netblock too large: %s", str);
			return 0;
		}
		*net = atoi(s+1);
		if(*net == 0 && strcmp(s+1, "0") != 0) {
			log_err("cannot parse netblock: '%s'", str);
			return 0;
		}
		strlcpy(buf, str, sizeof(buf));
		s = strchr(buf, '/');
		if(s) *s = 0;
		s = buf;
	}
	if(!ipstrtoaddr(s?s:str, port, addr, addrlen)) {
		log_err("cannot parse ip address: '%s'", str);
		return 0;
	}
	if(s) {
		addr_mask(addr, *addrlen, *net);
	}
	return 1;
}

int authextstrtoaddr(char* str, struct sockaddr_storage* addr, 
	socklen_t* addrlen, char** auth_name)
{
	char* s;
	int port = UNBOUND_DNS_PORT;
	if((s=strchr(str, '@'))) {
		char buf[MAX_ADDR_STRLEN];
		size_t len = (size_t)(s-str);
		char* hash = strchr(s+1, '#');
		if(hash) {
			*auth_name = hash+1;
		} else {
			*auth_name = NULL;
		}
		if(len >= MAX_ADDR_STRLEN) {
			return 0;
		}
		(void)strlcpy(buf, str, sizeof(buf));
		buf[len] = 0;
		port = atoi(s+1);
		if(port == 0) {
			if(!hash && strcmp(s+1,"0")!=0)
				return 0;
			if(hash && strncmp(s+1,"0#",2)!=0)
				return 0;
		}
		return ipstrtoaddr(buf, port, addr, addrlen);
	}
	if((s=strchr(str, '#'))) {
		char buf[MAX_ADDR_STRLEN];
		size_t len = (size_t)(s-str);
		if(len >= MAX_ADDR_STRLEN) {
			return 0;
		}
		(void)strlcpy(buf, str, sizeof(buf));
		buf[len] = 0;
		port = UNBOUND_DNS_OVER_TLS_PORT;
		*auth_name = s+1;
		return ipstrtoaddr(buf, port, addr, addrlen);
	}
	*auth_name = NULL;
	return ipstrtoaddr(str, port, addr, addrlen);
}

/** store port number into sockaddr structure */
void
sockaddr_store_port(struct sockaddr_storage* addr, socklen_t addrlen, int port)
{
	if(addr_is_ip6(addr, addrlen)) {
		struct sockaddr_in6* sa = (struct sockaddr_in6*)addr;
		sa->sin6_port = (in_port_t)htons((uint16_t)port);
	} else {
		struct sockaddr_in* sa = (struct sockaddr_in*)addr;
		sa->sin_port = (in_port_t)htons((uint16_t)port);
	}
}

void
log_nametypeclass(enum verbosity_value v, const char* str, uint8_t* name, 
	uint16_t type, uint16_t dclass)
{
	char buf[LDNS_MAX_DOMAINLEN+1];
	char t[12], c[12];
	const char *ts, *cs; 
	if(verbosity < v)
		return;
	dname_str(name, buf);
	if(type == LDNS_RR_TYPE_TSIG) ts = "TSIG";
	else if(type == LDNS_RR_TYPE_IXFR) ts = "IXFR";
	else if(type == LDNS_RR_TYPE_AXFR) ts = "AXFR";
	else if(type == LDNS_RR_TYPE_MAILB) ts = "MAILB";
	else if(type == LDNS_RR_TYPE_MAILA) ts = "MAILA";
	else if(type == LDNS_RR_TYPE_ANY) ts = "ANY";
	else if(sldns_rr_descript(type) && sldns_rr_descript(type)->_name)
		ts = sldns_rr_descript(type)->_name;
	else {
		snprintf(t, sizeof(t), "TYPE%d", (int)type);
		ts = t;
	}
	if(sldns_lookup_by_id(sldns_rr_classes, (int)dclass) &&
		sldns_lookup_by_id(sldns_rr_classes, (int)dclass)->name)
		cs = sldns_lookup_by_id(sldns_rr_classes, (int)dclass)->name;
	else {
		snprintf(c, sizeof(c), "CLASS%d", (int)dclass);
		cs = c;
	}
	log_info("%s %s %s %s", str, buf, ts, cs);
}

void log_name_addr(enum verbosity_value v, const char* str, uint8_t* zone, 
	struct sockaddr_storage* addr, socklen_t addrlen)
{
	uint16_t port;
	const char* family = "unknown_family ";
	char namebuf[LDNS_MAX_DOMAINLEN+1];
	char dest[100];
	int af = (int)((struct sockaddr_in*)addr)->sin_family;
	void* sinaddr = &((struct sockaddr_in*)addr)->sin_addr;
	if(verbosity < v)
		return;
	switch(af) {
		case AF_INET: family=""; break;
		case AF_INET6: family="";
			sinaddr = &((struct sockaddr_in6*)addr)->sin6_addr;
			break;
		case AF_LOCAL: family="local "; break;
		default: break;
	}
	if(inet_ntop(af, sinaddr, dest, (socklen_t)sizeof(dest)) == 0) {
		(void)strlcpy(dest, "(inet_ntop error)", sizeof(dest));
	}
	dest[sizeof(dest)-1] = 0;
	port = ntohs(((struct sockaddr_in*)addr)->sin_port);
	dname_str(zone, namebuf);
	if(af != AF_INET && af != AF_INET6)
		verbose(v, "%s <%s> %s%s#%d (addrlen %d)",
			str, namebuf, family, dest, (int)port, (int)addrlen);
	else	verbose(v, "%s <%s> %s%s#%d",
			str, namebuf, family, dest, (int)port);
}

void log_err_addr(const char* str, const char* err,
	struct sockaddr_storage* addr, socklen_t addrlen)
{
	uint16_t port;
	char dest[100];
	int af = (int)((struct sockaddr_in*)addr)->sin_family;
	void* sinaddr = &((struct sockaddr_in*)addr)->sin_addr;
	if(af == AF_INET6)
		sinaddr = &((struct sockaddr_in6*)addr)->sin6_addr;
	if(inet_ntop(af, sinaddr, dest, (socklen_t)sizeof(dest)) == 0) {
		(void)strlcpy(dest, "(inet_ntop error)", sizeof(dest));
	}
	dest[sizeof(dest)-1] = 0;
	port = ntohs(((struct sockaddr_in*)addr)->sin_port);
	if(verbosity >= 4)
		log_err("%s: %s for %s port %d (len %d)", str, err, dest,
			(int)port, (int)addrlen);
	else	log_err("%s: %s for %s port %d", str, err, dest, (int)port);
}

int
sockaddr_cmp(struct sockaddr_storage* addr1, socklen_t len1, 
	struct sockaddr_storage* addr2, socklen_t len2)
{
	struct sockaddr_in* p1_in = (struct sockaddr_in*)addr1;
	struct sockaddr_in* p2_in = (struct sockaddr_in*)addr2;
	struct sockaddr_in6* p1_in6 = (struct sockaddr_in6*)addr1;
	struct sockaddr_in6* p2_in6 = (struct sockaddr_in6*)addr2;
	if(len1 < len2)
		return -1;
	if(len1 > len2)
		return 1;
	log_assert(len1 == len2);
	if( p1_in->sin_family < p2_in->sin_family)
		return -1;
	if( p1_in->sin_family > p2_in->sin_family)
		return 1;
	log_assert( p1_in->sin_family == p2_in->sin_family );
	/* compare ip4 */
	if( p1_in->sin_family == AF_INET ) {
		/* just order it, ntohs not required */
		if(p1_in->sin_port < p2_in->sin_port)
			return -1;
		if(p1_in->sin_port > p2_in->sin_port)
			return 1;
		log_assert(p1_in->sin_port == p2_in->sin_port);
		return memcmp(&p1_in->sin_addr, &p2_in->sin_addr, INET_SIZE);
	} else if (p1_in6->sin6_family == AF_INET6) {
		/* just order it, ntohs not required */
		if(p1_in6->sin6_port < p2_in6->sin6_port)
			return -1;
		if(p1_in6->sin6_port > p2_in6->sin6_port)
			return 1;
		log_assert(p1_in6->sin6_port == p2_in6->sin6_port);
		return memcmp(&p1_in6->sin6_addr, &p2_in6->sin6_addr, 
			INET6_SIZE);
	} else {
		/* eek unknown type, perform this comparison for sanity. */
		return memcmp(addr1, addr2, len1);
	}
}

int
sockaddr_cmp_addr(struct sockaddr_storage* addr1, socklen_t len1, 
	struct sockaddr_storage* addr2, socklen_t len2)
{
	struct sockaddr_in* p1_in = (struct sockaddr_in*)addr1;
	struct sockaddr_in* p2_in = (struct sockaddr_in*)addr2;
	struct sockaddr_in6* p1_in6 = (struct sockaddr_in6*)addr1;
	struct sockaddr_in6* p2_in6 = (struct sockaddr_in6*)addr2;
	if(len1 < len2)
		return -1;
	if(len1 > len2)
		return 1;
	log_assert(len1 == len2);
	if( p1_in->sin_family < p2_in->sin_family)
		return -1;
	if( p1_in->sin_family > p2_in->sin_family)
		return 1;
	log_assert( p1_in->sin_family == p2_in->sin_family );
	/* compare ip4 */
	if( p1_in->sin_family == AF_INET ) {
		return memcmp(&p1_in->sin_addr, &p2_in->sin_addr, INET_SIZE);
	} else if (p1_in6->sin6_family == AF_INET6) {
		return memcmp(&p1_in6->sin6_addr, &p2_in6->sin6_addr, 
			INET6_SIZE);
	} else {
		/* eek unknown type, perform this comparison for sanity. */
		return memcmp(addr1, addr2, len1);
	}
}

int
addr_is_ip6(struct sockaddr_storage* addr, socklen_t len)
{
	if(len == (socklen_t)sizeof(struct sockaddr_in6) &&
		((struct sockaddr_in6*)addr)->sin6_family == AF_INET6)
		return 1;
	else    return 0;
}

void
addr_mask(struct sockaddr_storage* addr, socklen_t len, int net)
{
	uint8_t mask[8] = {0x0, 0x80, 0xc0, 0xe0, 0xf0, 0xf8, 0xfc, 0xfe};
	int i, max;
	uint8_t* s;
	if(addr_is_ip6(addr, len)) {
		s = (uint8_t*)&((struct sockaddr_in6*)addr)->sin6_addr;
		max = 128;
	} else {
		s = (uint8_t*)&((struct sockaddr_in*)addr)->sin_addr;
		max = 32;
	}
	if(net >= max)
		return;
	for(i=net/8+1; i<max/8; i++) {
		s[i] = 0;
	}
	s[net/8] &= mask[net&0x7];
}

int
addr_in_common(struct sockaddr_storage* addr1, int net1,
	struct sockaddr_storage* addr2, int net2, socklen_t addrlen)
{
	int min = (net1<net2)?net1:net2;
	int i, to;
	int match = 0;
	uint8_t* s1, *s2;
	if(addr_is_ip6(addr1, addrlen)) {
		s1 = (uint8_t*)&((struct sockaddr_in6*)addr1)->sin6_addr;
		s2 = (uint8_t*)&((struct sockaddr_in6*)addr2)->sin6_addr;
		to = 16;
	} else {
		s1 = (uint8_t*)&((struct sockaddr_in*)addr1)->sin_addr;
		s2 = (uint8_t*)&((struct sockaddr_in*)addr2)->sin_addr;
		to = 4;
	}
	/* match = bits_in_common(s1, s2, to); */
	for(i=0; i<to; i++) {
		if(s1[i] == s2[i]) {
			match += 8;
		} else {
			uint8_t z = s1[i]^s2[i];
			log_assert(z);
			while(!(z&0x80)) {
				match++;
				z<<=1;
			}
			break;
		}
	}
	if(match > min) match = min;
	return match;
}

void 
addr_to_str(struct sockaddr_storage* addr, socklen_t addrlen, 
	char* buf, size_t len)
{
	int af = (int)((struct sockaddr_in*)addr)->sin_family;
	void* sinaddr = &((struct sockaddr_in*)addr)->sin_addr;
	if(addr_is_ip6(addr, addrlen))
		sinaddr = &((struct sockaddr_in6*)addr)->sin6_addr;
	if(inet_ntop(af, sinaddr, buf, (socklen_t)len) == 0) {
		snprintf(buf, len, "(inet_ntop_error)");
	}
}

int 
addr_is_ip4mapped(struct sockaddr_storage* addr, socklen_t addrlen)
{
	/* prefix for ipv4 into ipv6 mapping is ::ffff:x.x.x.x */
	const uint8_t map_prefix[16] = 
		{0,0,0,0,  0,0,0,0, 0,0,0xff,0xff, 0,0,0,0};
	uint8_t* s;
	if(!addr_is_ip6(addr, addrlen))
		return 0;
	/* s is 16 octet ipv6 address string */
	s = (uint8_t*)&((struct sockaddr_in6*)addr)->sin6_addr;
	return (memcmp(s, map_prefix, 12) == 0);
}

int addr_is_broadcast(struct sockaddr_storage* addr, socklen_t addrlen)
{
	int af = (int)((struct sockaddr_in*)addr)->sin_family;
	void* sinaddr = &((struct sockaddr_in*)addr)->sin_addr;
	return af == AF_INET && addrlen>=(socklen_t)sizeof(struct sockaddr_in)
		&& memcmp(sinaddr, "\377\377\377\377", 4) == 0;
}

int addr_is_any(struct sockaddr_storage* addr, socklen_t addrlen)
{
	int af = (int)((struct sockaddr_in*)addr)->sin_family;
	void* sinaddr = &((struct sockaddr_in*)addr)->sin_addr;
	void* sin6addr = &((struct sockaddr_in6*)addr)->sin6_addr;
	if(af == AF_INET && addrlen>=(socklen_t)sizeof(struct sockaddr_in)
		&& memcmp(sinaddr, "\000\000\000\000", 4) == 0)
		return 1;
	else if(af==AF_INET6 && addrlen>=(socklen_t)sizeof(struct sockaddr_in6)
		&& memcmp(sin6addr, "\000\000\000\000\000\000\000\000"
		"\000\000\000\000\000\000\000\000", 16) == 0)
		return 1;
	return 0;
}

void sock_list_insert(struct sock_list** list, struct sockaddr_storage* addr,
	socklen_t len, struct regional* region)
{
	struct sock_list* add = (struct sock_list*)regional_alloc(region,
		sizeof(*add) - sizeof(add->addr) + (size_t)len);
	if(!add) {
		log_err("out of memory in socketlist insert");
		return;
	}
	log_assert(list);
	add->next = *list;
	add->len = len;
	*list = add;
	if(len) memmove(&add->addr, addr, len);
}

void sock_list_prepend(struct sock_list** list, struct sock_list* add)
{
	struct sock_list* last = add;
	if(!last) 
		return;
	while(last->next)
		last = last->next;
	last->next = *list;
	*list = add;
}

int sock_list_find(struct sock_list* list, struct sockaddr_storage* addr,
        socklen_t len)
{
	while(list) {
		if(len == list->len) {
			if(len == 0 || sockaddr_cmp_addr(addr, len, 
				&list->addr, list->len) == 0)
				return 1;
		}
		list = list->next;
	}
	return 0;
}

void sock_list_merge(struct sock_list** list, struct regional* region,
	struct sock_list* add)
{
	struct sock_list* p;
	for(p=add; p; p=p->next) {
		if(!sock_list_find(*list, &p->addr, p->len))
			sock_list_insert(list, &p->addr, p->len, region);
	}
}

void
log_crypto_err(const char* str)
{
#ifdef HAVE_SSL
	/* error:[error code]:[library name]:[function name]:[reason string] */
	char buf[128];
	unsigned long e;
	ERR_error_string_n(ERR_get_error(), buf, sizeof(buf));
	log_err("%s crypto %s", str, buf);
	while( (e=ERR_get_error()) ) {
		ERR_error_string_n(e, buf, sizeof(buf));
		log_err("and additionally crypto %s", buf);
	}
#else
	(void)str;
#endif /* HAVE_SSL */
}

int
listen_sslctx_setup(void* ctxt)
{
#ifdef HAVE_SSL
	SSL_CTX* ctx = (SSL_CTX*)ctxt;
	/* no SSLv2, SSLv3 because has defects */
	if((SSL_CTX_set_options(ctx, SSL_OP_NO_SSLv2) & SSL_OP_NO_SSLv2)
		!= SSL_OP_NO_SSLv2){
		log_crypto_err("could not set SSL_OP_NO_SSLv2");
		return 0;
	}
	if((SSL_CTX_set_options(ctx, SSL_OP_NO_SSLv3) & SSL_OP_NO_SSLv3)
		!= SSL_OP_NO_SSLv3){
		log_crypto_err("could not set SSL_OP_NO_SSLv3");
		return 0;
	}
#if defined(SSL_OP_NO_TLSv1) && defined(SSL_OP_NO_TLSv1_1)
	/* if we have tls 1.1 disable 1.0 */
	if((SSL_CTX_set_options(ctx, SSL_OP_NO_TLSv1) & SSL_OP_NO_TLSv1)
		!= SSL_OP_NO_TLSv1){
		log_crypto_err("could not set SSL_OP_NO_TLSv1");
		return 0;
	}
#endif
#if defined(SSL_OP_NO_TLSv1_1) && defined(SSL_OP_NO_TLSv1_2)
	/* if we have tls 1.2 disable 1.1 */
	if((SSL_CTX_set_options(ctx, SSL_OP_NO_TLSv1_1) & SSL_OP_NO_TLSv1_1)
		!= SSL_OP_NO_TLSv1_1){
		log_crypto_err("could not set SSL_OP_NO_TLSv1_1");
		return 0;
	}
#endif
#if defined(SHA256_DIGEST_LENGTH) && defined(USE_ECDSA)
	/* if we have sha256, set the cipher list to have no known vulns */
	if(!SSL_CTX_set_cipher_list(ctx, "TLS13-CHACHA20-POLY1305-SHA256:TLS13-AES-256-GCM-SHA384:TLS13-AES-128-GCM-SHA256:ECDHE-ECDSA-CHACHA20-POLY1305:ECDHE-RSA-CHACHA20-POLY1305:ECDHE-ECDSA-AES256-GCM-SHA384:ECDHE-RSA-AES256-GCM-SHA384:ECDHE-ECDSA-AES128-GCM-SHA256:ECDHE-RSA-AES128-GCM-SHA256"))
		log_crypto_err("could not set cipher list with SSL_CTX_set_cipher_list");
#endif

	if((SSL_CTX_set_options(ctx, SSL_OP_CIPHER_SERVER_PREFERENCE) &
		SSL_OP_CIPHER_SERVER_PREFERENCE) !=
		SSL_OP_CIPHER_SERVER_PREFERENCE) {
		log_crypto_err("could not set SSL_OP_CIPHER_SERVER_PREFERENCE");
		return 0;
	}

#ifdef HAVE_SSL_CTX_SET_SECURITY_LEVEL
	SSL_CTX_set_security_level(ctx, 0);
#endif
#else
	(void)ctxt;
#endif /* HAVE_SSL */
	return 1;
}

void
listen_sslctx_setup_2(void* ctxt)
{
#ifdef HAVE_SSL
	SSL_CTX* ctx = (SSL_CTX*)ctxt;
	(void)ctx;
#if HAVE_DECL_SSL_CTX_SET_ECDH_AUTO
	if(!SSL_CTX_set_ecdh_auto(ctx,1)) {
		log_crypto_err("Error in SSL_CTX_ecdh_auto, not enabling ECDHE");
	}
#elif defined(USE_ECDSA)
	if(1) {
		EC_KEY *ecdh = EC_KEY_new_by_curve_name (NID_X9_62_prime256v1);
		if (!ecdh) {
			log_crypto_err("could not find p256, not enabling ECDHE");
		} else {
			if (1 != SSL_CTX_set_tmp_ecdh (ctx, ecdh)) {
				log_crypto_err("Error in SSL_CTX_set_tmp_ecdh, not enabling ECDHE");
			}
			EC_KEY_free (ecdh);
		}
	}
#endif
#else
	(void)ctxt;
#endif /* HAVE_SSL */
}

void* listen_sslctx_create(char* key, char* pem, char* verifypem)
{
#ifdef HAVE_SSL
	SSL_CTX* ctx = SSL_CTX_new(SSLv23_server_method());
	if(!ctx) {
		log_crypto_err("could not SSL_CTX_new");
		return NULL;
	}
	if(!listen_sslctx_setup(ctx)) {
		SSL_CTX_free(ctx);
		return NULL;
	}
	if(!SSL_CTX_use_certificate_chain_file(ctx, pem)) {
		log_err("error for cert file: %s", pem);
		log_crypto_err("error in SSL_CTX use_certificate_chain_file");
		SSL_CTX_free(ctx);
		return NULL;
	}
	if(!SSL_CTX_use_PrivateKey_file(ctx, key, SSL_FILETYPE_PEM)) {
		log_err("error for private key file: %s", key);
		log_crypto_err("Error in SSL_CTX use_PrivateKey_file");
		SSL_CTX_free(ctx);
		return NULL;
	}
	if(!SSL_CTX_check_private_key(ctx)) {
		log_err("error for key file: %s", key);
		log_crypto_err("Error in SSL_CTX check_private_key");
		SSL_CTX_free(ctx);
		return NULL;
	}
	listen_sslctx_setup_2(ctx);
	if(verifypem && verifypem[0]) {
		if(!SSL_CTX_load_verify_locations(ctx, verifypem, NULL)) {
			log_crypto_err("Error in SSL_CTX verify locations");
			SSL_CTX_free(ctx);
			return NULL;
		}
		SSL_CTX_set_client_CA_list(ctx, SSL_load_client_CA_file(
			verifypem));
		SSL_CTX_set_verify(ctx, SSL_VERIFY_PEER, NULL);
	}
	return ctx;
#else
	(void)key; (void)pem; (void)verifypem;
	return NULL;
#endif
}

#ifdef USE_WINSOCK
/* For windows, the CA trust store is not read by openssl.
   Add code to open the trust store using wincrypt API and add
   the root certs into openssl trust store */
static int
add_WIN_cacerts_to_openssl_store(SSL_CTX* tls_ctx)
{
	HCERTSTORE      hSystemStore;
	PCCERT_CONTEXT  pTargetCert = NULL;
	X509_STORE*	store;

	verbose(VERB_ALGO, "Adding Windows certificates from system root store to CA store");

	/* load just once per context lifetime for this version
	   TODO: dynamically update CA trust changes as they are available */
	if (!tls_ctx)
		return 0;

	/* Call wincrypt's CertOpenStore to open the CA root store. */

	if ((hSystemStore = CertOpenStore(
		CERT_STORE_PROV_SYSTEM,
		0,
		0,
		/* NOTE: mingw does not have this const: replace with 1 << 16 from code 
		   CERT_SYSTEM_STORE_CURRENT_USER, */
		1 << 16,
		L"root")) == 0)
	{
		return 0;
	}

	store = SSL_CTX_get_cert_store(tls_ctx);
	if (!store)
		return 0;

	/* failure if the CA store is empty or the call fails */
	if ((pTargetCert = CertEnumCertificatesInStore(
		hSystemStore, pTargetCert)) == 0) {
		verbose(VERB_ALGO, "CA certificate store for Windows is empty.");
		return 0;
	}
	/* iterate over the windows cert store and add to openssl store */
	do
	{
		X509 *cert1 = d2i_X509(NULL,
			(const unsigned char **)&pTargetCert->pbCertEncoded,
			pTargetCert->cbCertEncoded);
		if (!cert1) {
			/* return error if a cert fails */
			verbose(VERB_ALGO, "%s %d:%s",
				"Unable to parse certificate in memory",
				(int)ERR_get_error(), ERR_error_string(ERR_get_error(), NULL));
			return 0;
		}
		else {
			/* return error if a cert add to store fails */
			if (X509_STORE_add_cert(store, cert1) == 0) {
				unsigned long error = ERR_peek_last_error();

				/* Ignore error X509_R_CERT_ALREADY_IN_HASH_TABLE which means the
				* certificate is already in the store.  */
				if(ERR_GET_LIB(error) != ERR_LIB_X509 ||
				   ERR_GET_REASON(error) != X509_R_CERT_ALREADY_IN_HASH_TABLE) {
					verbose(VERB_ALGO, "%s %d:%s\n",
					    "Error adding certificate", (int)ERR_get_error(),
					     ERR_error_string(ERR_get_error(), NULL));
					X509_free(cert1);
					return 0;
				}
			}
			X509_free(cert1);
		}
	} while ((pTargetCert = CertEnumCertificatesInStore(
		hSystemStore, pTargetCert)) != 0);

	/* Clean up memory and quit. */
	if (pTargetCert)
		CertFreeCertificateContext(pTargetCert);
	if (hSystemStore)
	{
		if (!CertCloseStore(
			hSystemStore, 0))
			return 0;
	}
	verbose(VERB_ALGO, "Completed adding Windows certificates to CA store successfully");
	return 1;
}
#endif /* USE_WINSOCK */

void* connect_sslctx_create(char* key, char* pem, char* verifypem, int wincert)
{
#ifdef HAVE_SSL
	SSL_CTX* ctx = SSL_CTX_new(SSLv23_client_method());
	if(!ctx) {
		log_crypto_err("could not allocate SSL_CTX pointer");
		return NULL;
	}
	if((SSL_CTX_set_options(ctx, SSL_OP_NO_SSLv2) & SSL_OP_NO_SSLv2)
		!= SSL_OP_NO_SSLv2) {
		log_crypto_err("could not set SSL_OP_NO_SSLv2");
		SSL_CTX_free(ctx);
		return NULL;
	}
	if((SSL_CTX_set_options(ctx, SSL_OP_NO_SSLv3) & SSL_OP_NO_SSLv3)
		!= SSL_OP_NO_SSLv3) {
		log_crypto_err("could not set SSL_OP_NO_SSLv3");
		SSL_CTX_free(ctx);
		return NULL;
	}
	if(key && key[0]) {
		if(!SSL_CTX_use_certificate_chain_file(ctx, pem)) {
			log_err("error in client certificate %s", pem);
			log_crypto_err("error in certificate file");
			SSL_CTX_free(ctx);
			return NULL;
		}
		if(!SSL_CTX_use_PrivateKey_file(ctx, key, SSL_FILETYPE_PEM)) {
			log_err("error in client private key %s", key);
			log_crypto_err("error in key file");
			SSL_CTX_free(ctx);
			return NULL;
		}
		if(!SSL_CTX_check_private_key(ctx)) {
			log_err("error in client key %s", key);
			log_crypto_err("error in SSL_CTX_check_private_key");
			SSL_CTX_free(ctx);
			return NULL;
		}
	}
	if((verifypem && verifypem[0]) || wincert) {
		if(verifypem && verifypem[0]) {
			if(!SSL_CTX_load_verify_locations(ctx, verifypem, NULL)) {
				log_crypto_err("error in SSL_CTX verify");
				SSL_CTX_free(ctx);
				return NULL;
			}
		}
#ifdef USE_WINSOCK
		if(wincert) {
			if(!add_WIN_cacerts_to_openssl_store(ctx)) {
				log_crypto_err("error in add_WIN_cacerts_to_openssl_store");
				SSL_CTX_free(ctx);
				return NULL;
			}
		}
#else
		(void)wincert;
#endif
		SSL_CTX_set_verify(ctx, SSL_VERIFY_PEER, NULL);
	}
	return ctx;
#else
	(void)key; (void)pem; (void)verifypem; (void)wincert;
	return NULL;
#endif
}

void* incoming_ssl_fd(void* sslctx, int fd)
{
#ifdef HAVE_SSL
	SSL* ssl = SSL_new((SSL_CTX*)sslctx);
	if(!ssl) {
		log_crypto_err("could not SSL_new");
		return NULL;
	}
	SSL_set_accept_state(ssl);
	(void)SSL_set_mode(ssl, SSL_MODE_AUTO_RETRY);
	if(!SSL_set_fd(ssl, fd)) {
		log_crypto_err("could not SSL_set_fd");
		SSL_free(ssl);
		return NULL;
	}
	return ssl;
#else
	(void)sslctx; (void)fd;
	return NULL;
#endif
}

void* outgoing_ssl_fd(void* sslctx, int fd)
{
#ifdef HAVE_SSL
	SSL* ssl = SSL_new((SSL_CTX*)sslctx);
	if(!ssl) {
		log_crypto_err("could not SSL_new");
		return NULL;
	}
	SSL_set_connect_state(ssl);
	(void)SSL_set_mode(ssl, SSL_MODE_AUTO_RETRY);
	if(!SSL_set_fd(ssl, fd)) {
		log_crypto_err("could not SSL_set_fd");
		SSL_free(ssl);
		return NULL;
	}
	return ssl;
#else
	(void)sslctx; (void)fd;
	return NULL;
#endif
}

#if defined(HAVE_SSL) && defined(OPENSSL_THREADS) && !defined(THREADS_DISABLED) && defined(CRYPTO_LOCK) && OPENSSL_VERSION_NUMBER < 0x10100000L
/** global lock list for openssl locks */
static lock_basic_type *ub_openssl_locks = NULL;

/** callback that gets thread id for openssl */
static unsigned long
ub_crypto_id_cb(void)
{
	return (unsigned long)log_thread_get();
}

static void
ub_crypto_lock_cb(int mode, int type, const char *ATTR_UNUSED(file),
	int ATTR_UNUSED(line))
{
	if((mode&CRYPTO_LOCK)) {
		lock_basic_lock(&ub_openssl_locks[type]);
	} else {
		lock_basic_unlock(&ub_openssl_locks[type]);
	}
}
#endif /* OPENSSL_THREADS */

int ub_openssl_lock_init(void)
{
#if defined(HAVE_SSL) && defined(OPENSSL_THREADS) && !defined(THREADS_DISABLED) && defined(CRYPTO_LOCK) && OPENSSL_VERSION_NUMBER < 0x10100000L
	int i;
	ub_openssl_locks = (lock_basic_type*)reallocarray(
		NULL, (size_t)CRYPTO_num_locks(), sizeof(lock_basic_type));
	if(!ub_openssl_locks)
		return 0;
	for(i=0; i<CRYPTO_num_locks(); i++) {
		lock_basic_init(&ub_openssl_locks[i]);
	}
	CRYPTO_set_id_callback(&ub_crypto_id_cb);
	CRYPTO_set_locking_callback(&ub_crypto_lock_cb);
#endif /* OPENSSL_THREADS */
	return 1;
}

void ub_openssl_lock_delete(void)
{
#if defined(HAVE_SSL) && defined(OPENSSL_THREADS) && !defined(THREADS_DISABLED) && defined(CRYPTO_LOCK) && OPENSSL_VERSION_NUMBER < 0x10100000L
	int i;
	if(!ub_openssl_locks)
		return;
	CRYPTO_set_id_callback(NULL);
	CRYPTO_set_locking_callback(NULL);
	for(i=0; i<CRYPTO_num_locks(); i++) {
		lock_basic_destroy(&ub_openssl_locks[i]);
	}
	free(ub_openssl_locks);
#endif /* OPENSSL_THREADS */
}

