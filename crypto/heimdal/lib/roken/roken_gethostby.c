/*
 * Copyright (c) 1998 Kungliga Tekniska Högskolan
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

#include <config.h>

#include "roken.h"

#undef roken_gethostbyname
#undef roken_gethostbyaddr

static struct sockaddr_in dns_addr;
static char *dns_req;

static int
make_address(const char *address, struct in_addr *ip)
{
    if(inet_aton(address, ip) == 0){
	/* try to resolve as hostname, it might work if the address we
           are trying to lookup is local, for instance a web proxy */
	struct hostent *he = gethostbyname(address);
	if(he) {
	    unsigned char *p = (unsigned char*)he->h_addr;
	    ip->s_addr = (p[0] << 24) | (p[1] << 16) | (p[2] << 8) | p[3];
	} else {
	    return -1;
	}
    }
    return 0;
}

static int
setup_int(const char *proxy_host, short proxy_port,
	  const char *dns_host, short dns_port,
	  const char *dns_path)
{
    memset(&dns_addr, 0, sizeof(dns_addr));
    if(dns_req)
	free(dns_req);
    dns_req = NULL;
    if(proxy_host) {
	if(make_address(proxy_host, &dns_addr.sin_addr) != 0)
	    return -1;
	dns_addr.sin_port = htons(proxy_port);
	if (asprintf(&dns_req, "http://%s:%d%s", dns_host, dns_port, dns_path) < 0)
	    return -1;
    } else {
	if(make_address(dns_host, &dns_addr.sin_addr) != 0)
	    return -1;
	dns_addr.sin_port = htons(dns_port);
	asprintf(&dns_req, "%s", dns_path);
    }
    dns_addr.sin_family = AF_INET;
    return 0;
}

static void
split_spec(const char *spec, char **host, int *port, char **path, int def_port)
{
    char *p;
    *host = strdup(spec);
    p = strchr(*host, ':');
    if(p) {
	*p++ = '\0';
	if(sscanf(p, "%d", port) != 1)
	    *port = def_port;
    } else
	*port = def_port;
    p = strchr(p ? p : *host, '/');
    if(p) {
	if(path)
	    *path = strdup(p);
	*p = '\0';
    }else
	if(path)
	    *path = NULL;
}


ROKEN_LIB_FUNCTION int ROKEN_LIB_CALL
roken_gethostby_setup(const char *proxy_spec, const char *dns_spec)
{
    char *proxy_host = NULL;
    int proxy_port = 0;
    char *dns_host, *dns_path;
    int dns_port;

    int ret = -1;

    split_spec(dns_spec, &dns_host, &dns_port, &dns_path, 80);
    if(dns_path == NULL)
	goto out;
    if(proxy_spec)
	split_spec(proxy_spec, &proxy_host, &proxy_port, NULL, 80);
    ret = setup_int(proxy_host, proxy_port, dns_host, dns_port, dns_path);
out:
    free(proxy_host);
    free(dns_host);
    free(dns_path);
    return ret;
}


/* Try to lookup a name or an ip-address using http as transport
   mechanism. See the end of this file for an example program. */
static struct hostent*
roken_gethostby(const char *hostname)
{
    int s;
    struct sockaddr_in addr;
    char *request = NULL;
    char buf[1024];
    int offset = 0;
    int n;
    char *p, *foo;
    size_t len;

    if(dns_addr.sin_family == 0)
	return NULL; /* no configured host */
    addr = dns_addr;
    if (asprintf(&request, "GET %s?%s HTTP/1.0\r\n\r\n", dns_req, hostname) < 0)
	return NULL;
    if(request == NULL)
	return NULL;
    s  = socket(AF_INET, SOCK_STREAM, 0);
    if(s < 0) {
	free(request);
	return NULL;
    }
    if(connect(s, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
	close(s);
	free(request);
	return NULL;
    }

    len = strlen(request);
    if(write(s, request, len) != (ssize_t)len) {
	close(s);
	free(request);
	return NULL;
    }
    free(request);
    while(1) {
	n = read(s, buf + offset, sizeof(buf) - offset);
	if(n <= 0)
	    break;
	offset += n;
    }
    buf[offset] = '\0';
    close(s);
    p = strstr(buf, "\r\n\r\n"); /* find end of header */
    if(p) p += 4;
    else return NULL;
    foo = NULL;
    p = strtok_r(p, " \t\r\n", &foo);
    if(p == NULL)
	return NULL;
    {
	/* make a hostent to return */
#define MAX_ADDRS 16
	static struct hostent he;
	static char addrs[4 * MAX_ADDRS];
	static char *addr_list[MAX_ADDRS + 1];
	int num_addrs = 0;

	he.h_name = p;
	he.h_aliases = NULL;
	he.h_addrtype = AF_INET;
	he.h_length = 4;

	while((p = strtok_r(NULL, " \t\r\n", &foo)) && num_addrs < MAX_ADDRS) {
	    struct in_addr ip;
	    inet_aton(p, &ip);
	    ip.s_addr = ntohl(ip.s_addr);
	    addr_list[num_addrs] = &addrs[num_addrs * 4];
	    addrs[num_addrs * 4 + 0] = (ip.s_addr >> 24) & 0xff;
	    addrs[num_addrs * 4 + 1] = (ip.s_addr >> 16) & 0xff;
	    addrs[num_addrs * 4 + 2] = (ip.s_addr >> 8) & 0xff;
	    addrs[num_addrs * 4 + 3] = (ip.s_addr >> 0) & 0xff;
	    addr_list[++num_addrs] = NULL;
	}
	he.h_addr_list = addr_list;
	return &he;
    }
}

ROKEN_LIB_FUNCTION struct hostent* ROKEN_LIB_CALL
roken_gethostbyname(const char *hostname)
{
    struct hostent *he;
    he = gethostbyname(hostname);
    if(he)
	return he;
    return roken_gethostby(hostname);
}

ROKEN_LIB_FUNCTION struct hostent* ROKEN_LIB_CALL
roken_gethostbyaddr(const void *addr, size_t len, int type)
{
    struct in_addr a;
    const char *p;
    struct hostent *he;
    he = gethostbyaddr(addr, len, type);
    if(he)
	return he;
    if(type != AF_INET || len != 4)
	return NULL;
    p = addr;
    a.s_addr = htonl((p[0] << 24) | (p[1] << 16) | (p[2] << 8) | p[3]);
    return roken_gethostby(inet_ntoa(a));
}

#if 0

/* this program can be used as a cgi `script' to lookup names and
   ip-addresses */

#include <stdio.h>
#include <stdlib.h>
#include <netdb.h>
#include <sys/param.h>

int
main(int argc, char **argv)
{
    char *query = getenv("QUERY_STRING");
    char host[MAXHOSTNAMELEN];
    int i;
    struct hostent *he;

    printf("Content-type: text/plain\n\n");
    if(query == NULL)
	exit(0);
    he = gethostbyname(query);
    strncpy(host, he->h_name, sizeof(host));
    host[sizeof(host) - 1] = '\0';
    he = gethostbyaddr(he->h_addr, he->h_length, AF_INET);
    printf("%s\n", he->h_name);
    for(i = 0; he->h_addr_list[i]; i++) {
	struct in_addr ip;
	unsigned char *p = (unsigned char*)he->h_addr_list[i];
	ip.s_addr = htonl((p[0] << 24) | (p[1] << 16) | (p[2] << 8) | p[3]);
	printf("%s\n", inet_ntoa(ip));
    }
    exit(0);
}

#endif
