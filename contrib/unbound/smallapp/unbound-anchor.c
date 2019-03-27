/*
 * unbound-anchor.c - update the root anchor if necessary.
 *
 * Copyright (c) 2010, NLnet Labs. All rights reserved.
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
 *
 * This file checks to see that the current 5011 keys work to prime the
 * current root anchor.  If not a certificate is used to update the anchor,
 * with RFC7958 https xml fetch.
 *
 * This is a concept solution for distribution of the DNSSEC root
 * trust anchor.  It is a small tool, called "unbound-anchor", that
 * runs before the main validator starts.  I.e. in the init script:
 * unbound-anchor; unbound.  Thus it is meant to run at system boot time.
 *
 * Management-Abstract:
 *    * first run: fill root.key file with hardcoded DS record.
 *    * mostly: use RFC5011 tracking, quick . DNSKEY UDP query.
 *    * failover: use RFC7958 builtin certificate, do https and update.
 * Special considerations:
 *    * 30-days RFC5011 timer saves a lot of https traffic.
 *    * DNSKEY probe must be NOERROR, saves a lot of https traffic.
 *    * fail if clock before sign date of the root, if cert expired.
 *    * if the root goes back to unsigned, deals with it.
 *
 * It has hardcoded the root DS anchors and the ICANN CA root certificate.
 * It allows with options to override those.  It also takes root-hints (it
 * has to do a DNS resolve), and also has hardcoded defaults for those.
 *
 * Once it starts, just before the validator starts, it quickly checks if
 * the root anchor file needs to be updated.  First it tries to use
 * RFC5011-tracking of the root key.  If that fails (and for 30-days since
 * last successful probe), then it attempts to update using the
 * certificate.  So most of the time, the RFC5011 tracking will work fine,
 * and within a couple milliseconds, the main daemon can start.  It will
 * have only probed the . DNSKEY, not done expensive https transfers on the
 * root infrastructure.
 *
 * If there is no root key in the root.key file, it bootstraps the
 * RFC5011-tracking with its builtin DS anchors; if that fails it
 * bootstraps the RFC5011-tracking using the certificate.  (again to avoid
 * https, and it is also faster).
 * 
 * It uses the XML file by converting it to DS records and writing that to the
 * key file.  Unbound can detect that the 'special comments' are gone, and
 * the file contains a list of normal DNSKEY/DS records, and uses that to
 * bootstrap 5011 (the KSK is made VALID).
 *
 * The certificate RFC7958 update is done by fetching root-anchors.xml and
 * root-anchors.p7s via SSL.  The HTTPS certificate can be logged but is
 * not validated (https for channel security; the security comes from the
 * certificate).  The 'data.iana.org' domain name A and AAAA are resolved
 * without DNSSEC.  It tries a random IP until the transfer succeeds.  It
 * then checks the p7s signature.
 *
 * On any failure, it leaves the root key file untouched.  The main
 * validator has to cope with it, it cannot fix things (So a failure does
 * not go 'without DNSSEC', no downgrade).  If it used its builtin stuff or
 * did the https, it exits with an exit code, so that this can trigger the
 * init script to log the event and potentially alert the operator that can
 * do a manual check.
 *
 * The date is also checked.  Before 2010-07-15 is a failure (root not
 * signed yet; avoids attacks on system clock).  The
 * last-successful-RFC5011-probe (if available) has to be more than 30 days
 * in the past (otherwise, RFC5011 should have worked).  This keeps
 * unnecessary https traffic down.  If the main certificate is expired, it
 * fails.
 *
 * The dates on the keys in the xml are checked (uses the libexpat xml
 * parser), only the valid ones are used to re-enstate RFC5011 tracking.
 * If 0 keys are valid, the zone has gone to insecure (a special marker is
 * written in the keyfile that tells the main validator daemon the zone is
 * insecure).
 *
 * Only the root ICANN CA is shipped, not the intermediate ones.  The
 * intermediate CAs are included in the p7s file that was downloaded.  (the
 * root cert is valid to 2028 and the intermediate to 2014, today).
 *
 * Obviously, the tool also has options so the operator can provide a new
 * keyfile, a new certificate and new URLs, and fresh root hints.  By
 * default it logs nothing on failure and success; it 'just works'.
 *
 */

#include "config.h"
#include "libunbound/unbound.h"
#include "sldns/rrdef.h"
#include "sldns/parseutil.h"
#include <expat.h>
#ifndef HAVE_EXPAT_H
#error "need libexpat to parse root-anchors.xml file."
#endif
#ifdef HAVE_GETOPT_H
#include <getopt.h>
#endif
#ifdef HAVE_OPENSSL_SSL_H
#include <openssl/ssl.h>
#endif
#ifdef HAVE_OPENSSL_ERR_H
#include <openssl/err.h>
#endif
#ifdef HAVE_OPENSSL_RAND_H
#include <openssl/rand.h>
#endif
#include <openssl/x509.h>
#include <openssl/x509v3.h>
#include <openssl/pem.h>

/** name of server in URL to fetch HTTPS from */
#define URLNAME "data.iana.org"
/** path on HTTPS server to xml file */
#define XMLNAME "root-anchors/root-anchors.xml"
/** path on HTTPS server to p7s file */
#define P7SNAME "root-anchors/root-anchors.p7s"
/** name of the signer of the certificate */
#define P7SIGNER "dnssec@iana.org"
/** port number for https access */
#define HTTPS_PORT 443

#ifdef USE_WINSOCK
/* sneakily reuse the the wsa_strerror function, on windows */
char* wsa_strerror(int err);
#endif

/** verbosity for this application */
static int verb = 0;

/** list of IP addresses */
struct ip_list {
	/** next in list */
	struct ip_list* next;
	/** length of addr */
	socklen_t len;
	/** address ready to connect to */
	struct sockaddr_storage addr;
	/** has the address been used */
	int used;
};

/** Give unbound-anchor usage, and exit (1). */
static void
usage(void)
{
	printf("Usage:	local-unbound-anchor [opts]\n");
	printf("	Setup or update root anchor. "
		"Most options have defaults.\n");
	printf("	Run this program before you start the validator.\n");
	printf("\n");
	printf("	The anchor and cert have default builtin content\n");
	printf("	if the file does not exist or is empty.\n");
	printf("\n");
	printf("-a file		root key file, default %s\n", ROOT_ANCHOR_FILE);
	printf("		The key is input and output for this tool.\n");
	printf("-c file		cert file, default %s\n", ROOT_CERT_FILE);
	printf("-l		list builtin key and cert on stdout\n");
	printf("-u name		server in https url, default %s\n", URLNAME);
	printf("-x path		pathname to xml in url, default %s\n", XMLNAME);
	printf("-s path		pathname to p7s in url, default %s\n", P7SNAME);
	printf("-n name		signer's subject emailAddress, default %s\n", P7SIGNER);
	printf("-4		work using IPv4 only\n");
	printf("-6		work using IPv6 only\n");
	printf("-f resolv.conf	use given resolv.conf\n");
	printf("-r root.hints	use given root.hints\n"
		"		builtin root hints are used by default\n");
	printf("-R		fallback from -f to root query on error\n");
	printf("-v		more verbose\n");
	printf("-C conf		debug, read config\n");
	printf("-P port		use port for https connect, default 443\n");
	printf("-F 		debug, force update with cert\n");
	printf("-h		show this usage help\n");
	printf("Version %s\n", PACKAGE_VERSION);
	printf("BSD licensed, see LICENSE in source package for details.\n");
	printf("Report bugs to %s\n", PACKAGE_BUGREPORT);
	exit(1);
}

/** return the built in root update certificate */
static const char*
get_builtin_cert(void)
{
	return
/* The ICANN CA fetched at 24 Sep 2010.  Valid to 2028 */
"-----BEGIN CERTIFICATE-----\n"
"MIIDdzCCAl+gAwIBAgIBATANBgkqhkiG9w0BAQsFADBdMQ4wDAYDVQQKEwVJQ0FO\n"
"TjEmMCQGA1UECxMdSUNBTk4gQ2VydGlmaWNhdGlvbiBBdXRob3JpdHkxFjAUBgNV\n"
"BAMTDUlDQU5OIFJvb3QgQ0ExCzAJBgNVBAYTAlVTMB4XDTA5MTIyMzA0MTkxMloX\n"
"DTI5MTIxODA0MTkxMlowXTEOMAwGA1UEChMFSUNBTk4xJjAkBgNVBAsTHUlDQU5O\n"
"IENlcnRpZmljYXRpb24gQXV0aG9yaXR5MRYwFAYDVQQDEw1JQ0FOTiBSb290IENB\n"
"MQswCQYDVQQGEwJVUzCCASIwDQYJKoZIhvcNAQEBBQADggEPADCCAQoCggEBAKDb\n"
"cLhPNNqc1NB+u+oVvOnJESofYS9qub0/PXagmgr37pNublVThIzyLPGCJ8gPms9S\n"
"G1TaKNIsMI7d+5IgMy3WyPEOECGIcfqEIktdR1YWfJufXcMReZwU4v/AdKzdOdfg\n"
"ONiwc6r70duEr1IiqPbVm5T05l1e6D+HkAvHGnf1LtOPGs4CHQdpIUcy2kauAEy2\n"
"paKcOcHASvbTHK7TbbvHGPB+7faAztABLoneErruEcumetcNfPMIjXKdv1V1E3C7\n"
"MSJKy+jAqqQJqjZoQGB0necZgUMiUv7JK1IPQRM2CXJllcyJrm9WFxY0c1KjBO29\n"
"iIKK69fcglKcBuFShUECAwEAAaNCMEAwDwYDVR0TAQH/BAUwAwEB/zAOBgNVHQ8B\n"
"Af8EBAMCAf4wHQYDVR0OBBYEFLpS6UmDJIZSL8eZzfyNa2kITcBQMA0GCSqGSIb3\n"
"DQEBCwUAA4IBAQAP8emCogqHny2UYFqywEuhLys7R9UKmYY4suzGO4nkbgfPFMfH\n"
"6M+Zj6owwxlwueZt1j/IaCayoKU3QsrYYoDRolpILh+FPwx7wseUEV8ZKpWsoDoD\n"
"2JFbLg2cfB8u/OlE4RYmcxxFSmXBg0yQ8/IoQt/bxOcEEhhiQ168H2yE5rxJMt9h\n"
"15nu5JBSewrCkYqYYmaxyOC3WrVGfHZxVI7MpIFcGdvSb2a1uyuua8l0BKgk3ujF\n"
"0/wsHNeP22qNyVO+XVBzrM8fk8BSUFuiT/6tZTYXRtEt5aKQZgXbKU5dUF3jT9qg\n"
"j/Br5BZw3X/zd325TvnswzMC1+ljLzHnQGGk\n"
"-----END CERTIFICATE-----\n"
		;
}

/** return the built in root DS trust anchor */
static const char*
get_builtin_ds(void)
{
	return
/* The anchors must start on a new line with ". IN DS and end with \n"[;]
 * because the makedist script greps on the source here */
/* anchor 19036 is from 2010 */
/* anchor 20326 is from 2017 */
". IN DS 19036 8 2 49AAC11D7B6F6446702E54A1607371607A1A41855200FD2CE1CDDE32F24E8FB5\n"
". IN DS 20326 8 2 E06D44B80B8F1D39A95C0B0D7C65D08458E880409BBC683457104237C7F8EC8D\n";
}

/** print hex data */
static void
print_data(const char* msg, const char* data, int len)
{
	int i;
	printf("%s: ", msg);
	for(i=0; i<len; i++) {
		printf(" %2.2x", (unsigned char)data[i]);
	}
	printf("\n");
}

/** print ub context creation error and exit */
static void
ub_ctx_error_exit(struct ub_ctx* ctx, const char* str, const char* str2)
{
	ub_ctx_delete(ctx);
	if(str && str2 && verb) printf("%s: %s\n", str, str2);
	if(verb) printf("error: could not create unbound resolver context\n");
	exit(0);
}

/**
 * Create a new unbound context with the commandline settings applied
 */
static struct ub_ctx* 
create_unbound_context(const char* res_conf, const char* root_hints,
	const char* debugconf, int ip4only, int ip6only)
{
	int r;
	struct ub_ctx* ctx = ub_ctx_create();
	if(!ctx) {
		if(verb) printf("out of memory\n");
		exit(0);
	}
	/* do not waste time and network traffic to fetch extra nameservers */
	r = ub_ctx_set_option(ctx, "target-fetch-policy:", "0 0 0 0 0");
	if(r && verb) printf("ctx targetfetchpolicy: %s\n", ub_strerror(r));
	/* read config file first, so its settings can be overridden */
	if(debugconf) {
		r = ub_ctx_config(ctx, debugconf);
		if(r) ub_ctx_error_exit(ctx, debugconf, ub_strerror(r));
	}
	if(res_conf) {
		r = ub_ctx_resolvconf(ctx, res_conf);
		if(r) ub_ctx_error_exit(ctx, res_conf, ub_strerror(r));
	}
	if(root_hints) {
		r = ub_ctx_set_option(ctx, "root-hints:", root_hints);
		if(r) ub_ctx_error_exit(ctx, root_hints, ub_strerror(r));
	}
	if(ip4only) {
		r = ub_ctx_set_option(ctx, "do-ip6:", "no");
		if(r) ub_ctx_error_exit(ctx, "ip4only", ub_strerror(r));
	}
	if(ip6only) {
		r = ub_ctx_set_option(ctx, "do-ip4:", "no");
		if(r) ub_ctx_error_exit(ctx, "ip6only", ub_strerror(r));
	}
	return ctx;
}

/** printout certificate in detail */
static void
verb_cert(const char* msg, X509* x)
{
	if(verb == 0 || verb == 1) return;
	if(verb == 2) {
		if(msg) printf("%s\n", msg);
		X509_print_ex_fp(stdout, x, 0, (unsigned long)-1
			^(X509_FLAG_NO_SUBJECT
			|X509_FLAG_NO_ISSUER|X509_FLAG_NO_VALIDITY));
		return;
	}
	if(msg) printf("%s\n", msg);
	X509_print_fp(stdout, x);
}

/** printout certificates in detail */
static void
verb_certs(const char* msg, STACK_OF(X509)* sk)
{
	int i, num = sk_X509_num(sk);
	if(verb == 0 || verb == 1) return;
	for(i=0; i<num; i++) {
		printf("%s (%d/%d)\n", msg, i, num);
		verb_cert(NULL, sk_X509_value(sk, i));
	}
}

/** read certificates from a PEM bio */
static STACK_OF(X509)*
read_cert_bio(BIO* bio)
{
	STACK_OF(X509) *sk = sk_X509_new_null();
	if(!sk) {
		if(verb) printf("out of memory\n");
		exit(0);
	}
	while(!BIO_eof(bio)) {
		X509* x = PEM_read_bio_X509(bio, NULL, 0, NULL);
		if(x == NULL) {
			if(verb) {
				printf("failed to read X509\n");
			 	ERR_print_errors_fp(stdout);
			}
			continue;
		}
		if(!sk_X509_push(sk, x)) {
			if(verb) printf("out of memory\n");
			exit(0);
		}
	}
	return sk;
}

/* read the certificate file */
static STACK_OF(X509)*
read_cert_file(const char* file)
{
	STACK_OF(X509)* sk;
	FILE* in;
	int content = 0;
	char buf[128];
	if(file == NULL || strcmp(file, "") == 0) {
		return NULL;
	}
	sk = sk_X509_new_null();
	if(!sk) {
		if(verb) printf("out of memory\n");
		exit(0);
	}
	in = fopen(file, "r");
	if(!in) {
		if(verb) printf("%s: %s\n", file, strerror(errno));
#ifndef S_SPLINT_S
		sk_X509_pop_free(sk, X509_free);
#endif
		return NULL;
	}
	while(!feof(in)) {
		X509* x = PEM_read_X509(in, NULL, 0, NULL);
		if(x == NULL) {
			if(verb) {
				printf("failed to read X509 file\n");
			 	ERR_print_errors_fp(stdout);
			}
			continue;
		}
		if(!sk_X509_push(sk, x)) {
			if(verb) printf("out of memory\n");
			fclose(in);
			exit(0);
		}
		content = 1;
		/* read away newline after --END CERT-- */
		if(!fgets(buf, (int)sizeof(buf), in))
			break;
	}
	fclose(in);
	if(!content) {
		if(verb) printf("%s is empty\n", file);
#ifndef S_SPLINT_S
		sk_X509_pop_free(sk, X509_free);
#endif
		return NULL;
	}
	return sk;
}

/** read certificates from the builtin certificate */
static STACK_OF(X509)*
read_builtin_cert(void)
{
	const char* builtin_cert = get_builtin_cert();
	STACK_OF(X509)* sk;
	BIO *bio = BIO_new_mem_buf(builtin_cert,
		(int)strlen(builtin_cert));
	if(!bio) {
		if(verb) printf("out of memory\n");
		exit(0);
	}
	sk = read_cert_bio(bio);
	if(!sk) {
		if(verb) printf("internal error, out of memory\n");
		exit(0);
	}
	BIO_free(bio);
	return sk;
}

/** read update cert file or use builtin */
static STACK_OF(X509)*
read_cert_or_builtin(const char* file)
{
	STACK_OF(X509) *sk = read_cert_file(file);
	if(!sk) {
		if(verb) printf("using builtin certificate\n");
		sk = read_builtin_cert();
	}
	if(verb) printf("have %d trusted certificates\n", sk_X509_num(sk));
	verb_certs("trusted certificates", sk);
	return sk;
}

static void
do_list_builtin(void)
{
	const char* builtin_cert = get_builtin_cert();
	const char* builtin_ds = get_builtin_ds();
	printf("%s\n", builtin_ds);
	printf("%s\n", builtin_cert);
	exit(0);
}

/** printout IP address with message */
static void
verb_addr(const char* msg, struct ip_list* ip)
{
	if(verb) {
		char out[100];
		void* a = &((struct sockaddr_in*)&ip->addr)->sin_addr;
		if(ip->len != (socklen_t)sizeof(struct sockaddr_in))
			a = &((struct sockaddr_in6*)&ip->addr)->sin6_addr;

		if(inet_ntop((int)((struct sockaddr_in*)&ip->addr)->sin_family,
			a, out, (socklen_t)sizeof(out))==0)
			printf("%s (inet_ntop error)\n", msg);
		else printf("%s %s\n", msg, out);
	}
}

/** free ip_list */
static void
ip_list_free(struct ip_list* p)
{
	struct ip_list* np;
	while(p) {
		np = p->next;
		free(p);
		p = np;
	}
}

/** create ip_list entry for a RR record */
static struct ip_list*
RR_to_ip(int tp, char* data, int len, int port)
{
	struct ip_list* ip = (struct ip_list*)calloc(1, sizeof(*ip));
	uint16_t p = (uint16_t)port;
	if(tp == LDNS_RR_TYPE_A) {
		struct sockaddr_in* sa = (struct sockaddr_in*)&ip->addr;
		ip->len = (socklen_t)sizeof(*sa);
		sa->sin_family = AF_INET;
		sa->sin_port = (in_port_t)htons(p);
		if(len != (int)sizeof(sa->sin_addr)) {
			if(verb) printf("skipped badly formatted A\n");
			free(ip);
			return NULL;
		}
		memmove(&sa->sin_addr, data, sizeof(sa->sin_addr));

	} else if(tp == LDNS_RR_TYPE_AAAA) {
		struct sockaddr_in6* sa = (struct sockaddr_in6*)&ip->addr;
		ip->len = (socklen_t)sizeof(*sa);
		sa->sin6_family = AF_INET6;
		sa->sin6_port = (in_port_t)htons(p);
		if(len != (int)sizeof(sa->sin6_addr)) {
			if(verb) printf("skipped badly formatted AAAA\n");
			free(ip);
			return NULL;
		}
		memmove(&sa->sin6_addr, data, sizeof(sa->sin6_addr));
	} else {
		if(verb) printf("internal error: bad type in RRtoip\n");
		free(ip);
		return NULL;
	}
	verb_addr("resolved server address", ip);
	return ip;
}

/** Resolve name, type, class and add addresses to iplist */
static void
resolve_host_ip(struct ub_ctx* ctx, const char* host, int port, int tp, int cl,
	struct ip_list** head)
{
	struct ub_result* res = NULL;
	int r;
	int i;

	r = ub_resolve(ctx, host, tp, cl, &res);
	if(r) {
		if(verb) printf("error: resolve %s %s: %s\n", host,
			(tp==LDNS_RR_TYPE_A)?"A":"AAAA", ub_strerror(r));
		return;
	}
	if(!res) {
		if(verb) printf("out of memory\n");
		ub_ctx_delete(ctx);
		exit(0);
	}
	if(!res->havedata || res->rcode || !res->data) {
		if(verb) printf("resolve %s %s: no result\n", host,
			(tp==LDNS_RR_TYPE_A)?"A":"AAAA");
		return;
	}
	for(i = 0; res->data[i]; i++) {
		struct ip_list* ip = RR_to_ip(tp, res->data[i], res->len[i],
			port);
		if(!ip) continue;
		ip->next = *head;
		*head = ip;
	}
	ub_resolve_free(res);
}

/** parse a text IP address into a sockaddr */
static struct ip_list*
parse_ip_addr(const char* str, int port)
{
	socklen_t len = 0;
	union {
		struct sockaddr_in6 a6;
		struct sockaddr_in a;
	} addr;
	struct ip_list* ip;
	uint16_t p = (uint16_t)port;
	memset(&addr, 0, sizeof(addr));

	if(inet_pton(AF_INET6, str, &addr.a6.sin6_addr) > 0) {
		/* it is an IPv6 */
		addr.a6.sin6_family = AF_INET6;
		addr.a6.sin6_port = (in_port_t)htons(p);
		len = (socklen_t)sizeof(addr.a6);
	}
	if(inet_pton(AF_INET, str, &addr.a.sin_addr) > 0) {
		/* it is an IPv4 */
		addr.a.sin_family = AF_INET;
		addr.a.sin_port = (in_port_t)htons(p);
		len = (socklen_t)sizeof(struct sockaddr_in);
	}
	if(!len) return NULL;
	ip = (struct ip_list*)calloc(1, sizeof(*ip));
	if(!ip) {
		if(verb) printf("out of memory\n");
		exit(0);
	}
	ip->len = len;
	memmove(&ip->addr, &addr, len);
	if(verb) printf("server address is %s\n", str);
	return ip;
}

/**
 * Resolve a domain name (even though the resolver is down and there is
 * no trust anchor).  Without DNSSEC validation.
 * @param host: the name to resolve.
 * 	If this name is an IP4 or IP6 address this address is returned.
 * @param port: the port number used for the returned IP structs.
 * @param res_conf: resolv.conf (if any).
 * @param root_hints: root hints (if any).
 * @param debugconf: unbound.conf for debugging options.
 * @param ip4only: use only ip4 for resolve and only lookup A
 * @param ip6only: use only ip6 for resolve and only lookup AAAA
 * 	default is to lookup A and AAAA using ip4 and ip6.
 * @return list of IP addresses.
 */
static struct ip_list*
resolve_name(const char* host, int port, const char* res_conf,
	const char* root_hints, const char* debugconf, int ip4only, int ip6only)
{
	struct ub_ctx* ctx;
	struct ip_list* list = NULL;
	/* first see if name is an IP address itself */
	if( (list=parse_ip_addr(host, port)) ) {
		return list;
	}
	
	/* create resolver context */
	ctx = create_unbound_context(res_conf, root_hints, debugconf,
        	ip4only, ip6only);

	/* try resolution of A */
	if(!ip6only) {
		resolve_host_ip(ctx, host, port, LDNS_RR_TYPE_A,
			LDNS_RR_CLASS_IN, &list);
	}

	/* try resolution of AAAA */
	if(!ip4only) {
		resolve_host_ip(ctx, host, port, LDNS_RR_TYPE_AAAA,
			LDNS_RR_CLASS_IN, &list);
	}

	ub_ctx_delete(ctx);
	if(!list) {
		if(verb) printf("%s has no IP addresses I can use\n", host);
		exit(0);
	}
	return list;
}

/** clear used flags */
static void
wipe_ip_usage(struct ip_list* p)
{
	while(p) {
		p->used = 0;
		p = p->next;
	}
}

/** count unused IPs */
static int
count_unused(struct ip_list* p)
{
	int num = 0;
	while(p) {
		if(!p->used) num++;
		p = p->next;
	}
	return num;
}

/** pick random unused element from IP list */
static struct ip_list*
pick_random_ip(struct ip_list* list)
{
	struct ip_list* p = list;
	int num = count_unused(list);
	int sel;
	if(num == 0) return NULL;
	/* not perfect, but random enough */
	sel = (int)arc4random_uniform((uint32_t)num);
	/* skip over unused elements that we did not select */
	while(sel > 0 && p) {
		if(!p->used) sel--;
		p = p->next;
	}
	/* find the next unused element */
	while(p && p->used)
		p = p->next;
	if(!p) return NULL; /* robustness */
	return p;
}

/** close the fd */
static void
fd_close(int fd)
{
#ifndef USE_WINSOCK
	close(fd);
#else
	closesocket(fd);
#endif
}

/** printout socket errno */
static void
print_sock_err(const char* msg)
{
#ifndef USE_WINSOCK
	if(verb) printf("%s: %s\n", msg, strerror(errno));
#else
	if(verb) printf("%s: %s\n", msg, wsa_strerror(WSAGetLastError()));
#endif
}

/** connect to IP address */
static int
connect_to_ip(struct ip_list* ip)
{
	int fd;
	verb_addr("connect to", ip);
	fd = socket(ip->len==(socklen_t)sizeof(struct sockaddr_in)?
		AF_INET:AF_INET6, SOCK_STREAM, 0);
	if(fd == -1) {
		print_sock_err("socket");
		return -1;
	}
	if(connect(fd, (struct sockaddr*)&ip->addr, ip->len) < 0) {
		print_sock_err("connect");
		fd_close(fd);
		return -1;
	}
	return fd;
}

/** create SSL context */
static SSL_CTX*
setup_sslctx(void)
{
	SSL_CTX* sslctx = SSL_CTX_new(SSLv23_client_method());
	if(!sslctx) {
		if(verb) printf("SSL_CTX_new error\n");
		return NULL;
	}
	return sslctx;
}

/** initiate TLS on a connection */
static SSL*
TLS_initiate(SSL_CTX* sslctx, int fd)
{
	X509* x;
	int r;
	SSL* ssl = SSL_new(sslctx);
	if(!ssl) {
		if(verb) printf("SSL_new error\n");
		return NULL;
	}
	SSL_set_connect_state(ssl);
	(void)SSL_set_mode(ssl, SSL_MODE_AUTO_RETRY);
	if(!SSL_set_fd(ssl, fd)) {
		if(verb) printf("SSL_set_fd error\n");
		SSL_free(ssl);
		return NULL;
	}
	while(1) {
		ERR_clear_error();
		if( (r=SSL_do_handshake(ssl)) == 1)
			break;
		r = SSL_get_error(ssl, r);
		if(r != SSL_ERROR_WANT_READ && r != SSL_ERROR_WANT_WRITE) {
			if(verb) printf("SSL handshake failed\n");
			SSL_free(ssl);
			return NULL;
		}
		/* wants to be called again */
	}
	x = SSL_get_peer_certificate(ssl);
	if(!x) {
		if(verb) printf("Server presented no peer certificate\n");
		SSL_free(ssl);
		return NULL;
	}
	verb_cert("server SSL certificate", x);
	X509_free(x);
	return ssl;
}

/** perform neat TLS shutdown */
static void
TLS_shutdown(int fd, SSL* ssl, SSL_CTX* sslctx)
{
	/* shutdown the SSL connection nicely */
	if(SSL_shutdown(ssl) == 0) {
		SSL_shutdown(ssl);
	}
	SSL_free(ssl);
	SSL_CTX_free(sslctx);
	fd_close(fd);
}

/** write a line over SSL */
static int
write_ssl_line(SSL* ssl, const char* str, const char* sec)
{
	char buf[1024];
	size_t l;
	if(sec) {
		snprintf(buf, sizeof(buf), str, sec);
	} else {
		snprintf(buf, sizeof(buf), "%s", str);
	}
	l = strlen(buf);
	if(l+2 >= sizeof(buf)) {
		if(verb) printf("line too long\n");
		return 0;
	}
	if(verb >= 2) printf("SSL_write: %s\n", buf);
	buf[l] = '\r';
	buf[l+1] = '\n';
	buf[l+2] = 0;
	/* add \r\n */
	if(SSL_write(ssl, buf, (int)strlen(buf)) <= 0) {
		if(verb) printf("could not SSL_write %s", str);
		return 0;
	}
	return 1;
}

/** process header line, check rcode and keeping track of size */
static int
process_one_header(char* buf, size_t* clen, int* chunked)
{
	if(verb>=2) printf("header: '%s'\n", buf);
	if(strncasecmp(buf, "HTTP/1.1 ", 9) == 0) {
		/* check returncode */
		if(buf[9] != '2') {
			if(verb) printf("bad status %s\n", buf+9);
			return 0;
		}
	} else if(strncasecmp(buf, "Content-Length: ", 16) == 0) {
		if(!*chunked)
			*clen = (size_t)atoi(buf+16);
	} else if(strncasecmp(buf, "Transfer-Encoding: chunked", 19+7) == 0) {
		*clen = 0;
		*chunked = 1;
	}
	return 1;
}

/** 
 * Read one line from SSL
 * zero terminates.
 * skips "\r\n" (but not copied to buf).
 * @param ssl: the SSL connection to read from (blocking).
 * @param buf: buffer to return line in.
 * @param len: size of the buffer.
 * @return 0 on error, 1 on success.
 */
static int
read_ssl_line(SSL* ssl, char* buf, size_t len)
{
	size_t n = 0;
	int r;
	int endnl = 0;
	while(1) {
		if(n >= len) {
			if(verb) printf("line too long\n");
			return 0;
		}
		if((r = SSL_read(ssl, buf+n, 1)) <= 0) {
			if(SSL_get_error(ssl, r) == SSL_ERROR_ZERO_RETURN) {
				/* EOF */
				break;
			}
			if(verb) printf("could not SSL_read\n");
			return 0;
		}
		if(endnl && buf[n] == '\n') {
			break;
		} else if(endnl) {
			/* bad data */
			if(verb) printf("error: stray linefeeds\n");
			return 0;
		} else if(buf[n] == '\r') {
			/* skip \r, and also \n on the wire */
			endnl = 1;
			continue;
		} else if(buf[n] == '\n') {
			/* skip the \n, we are done */
			break;
		} else n++;
	}
	buf[n] = 0;
	return 1;
}

/** read http headers and process them */
static size_t
read_http_headers(SSL* ssl, size_t* clen)
{
	char buf[1024];
	int chunked = 0;
	*clen = 0;
	while(read_ssl_line(ssl, buf, sizeof(buf))) {
		if(buf[0] == 0)
			return 1;
		if(!process_one_header(buf, clen, &chunked))
			return 0;
	}
	return 0;
}

/** read a data chunk */
static char*
read_data_chunk(SSL* ssl, size_t len)
{
	size_t got = 0;
	int r;
	char* data;
	if(len >= 0xfffffff0)
		return NULL; /* to protect against integer overflow in malloc*/
	data = malloc(len+1);
	if(!data) {
		if(verb) printf("out of memory\n");
		return NULL;
	}
	while(got < len) {
		if((r = SSL_read(ssl, data+got, (int)(len-got))) <= 0) {
			if(SSL_get_error(ssl, r) == SSL_ERROR_ZERO_RETURN) {
				/* EOF */
				if(verb) printf("could not SSL_read: unexpected EOF\n");
				free(data);
				return NULL;
			}
			if(verb) printf("could not SSL_read\n");
			free(data);
			return NULL;
		}
		if(verb >= 2) printf("at %d/%d\n", (int)got, (int)len);
		got += r;
	}
	if(verb>=2) printf("read %d data\n", (int)len);
	data[len] = 0;
	return data;
}

/** parse chunk header */
static int
parse_chunk_header(char* buf, size_t* result)
{
	char* e = NULL;
	size_t v = (size_t)strtol(buf, &e, 16);
	if(e == buf)
		return 0;
	*result = v;
	return 1;
}

/** read chunked data from connection */
static BIO*
do_chunked_read(SSL* ssl)
{
	char buf[1024];
	size_t len;
	char* body;
	BIO* mem = BIO_new(BIO_s_mem());
	if(verb>=3) printf("do_chunked_read\n");
	if(!mem) {
		if(verb) printf("out of memory\n");
		return NULL;
	}
	while(read_ssl_line(ssl, buf, sizeof(buf))) {
		/* read the chunked start line */
		if(verb>=2) printf("chunk header: %s\n", buf);
		if(!parse_chunk_header(buf, &len)) {
			BIO_free(mem);
			if(verb>=3) printf("could not parse chunk header\n");
			return NULL;
		}
		if(verb>=2) printf("chunk len: %d\n", (int)len);
		/* are we done? */
		if(len == 0) {
			char z = 0;
			/* skip end-of-chunk-trailer lines,
			 * until the empty line after that */
			do {
				if(!read_ssl_line(ssl, buf, sizeof(buf))) {
					BIO_free(mem);
					return NULL;
				}
			} while (strlen(buf) > 0);
			/* end of chunks, zero terminate it */
			if(BIO_write(mem, &z, 1) <= 0) {
				if(verb) printf("out of memory\n");
				BIO_free(mem);
				return NULL;
			}
			return mem;
		}
		/* read the chunked body */
		body = read_data_chunk(ssl, len);
		if(!body) {
			BIO_free(mem);
			return NULL;
		}
		if(BIO_write(mem, body, (int)len) <= 0) {
			if(verb) printf("out of memory\n");
			free(body);
			BIO_free(mem);
			return NULL;
		}
		free(body);
		/* skip empty line after data chunk */
		if(!read_ssl_line(ssl, buf, sizeof(buf))) {
			BIO_free(mem);
			return NULL;
		}
	}
	BIO_free(mem);
	return NULL;
}

/** start HTTP1.1 transaction on SSL */
static int
write_http_get(SSL* ssl, const char* pathname, const char* urlname)
{
	if(write_ssl_line(ssl, "GET /%s HTTP/1.1", pathname) &&
	   write_ssl_line(ssl, "Host: %s", urlname) &&
	   write_ssl_line(ssl, "User-Agent: unbound-anchor/%s",
	   	PACKAGE_VERSION) &&
	   /* We do not really do multiple queries per connection,
	    * but this header setting is also not needed.
	    * write_ssl_line(ssl, "Connection: close", NULL) &&*/
	   write_ssl_line(ssl, "", NULL)) {
		return 1;
	}
	return 0;
}

/** read chunked data and zero terminate; len is without zero */
static char*
read_chunked_zero_terminate(SSL* ssl, size_t* len)
{
	/* do the chunked version */
	BIO* tmp = do_chunked_read(ssl);
	char* data, *d = NULL;
	size_t l;
	if(!tmp) {
		if(verb) printf("could not read from https\n");
		return NULL;
	}
	l = (size_t)BIO_get_mem_data(tmp, &d);
	if(verb>=2) printf("chunked data is %d\n", (int)l);
	if(l == 0 || d == NULL) {
		if(verb) printf("out of memory\n");
		return NULL;
	}
	*len = l-1;
	data = (char*)malloc(l);
	if(data == NULL) {
		if(verb) printf("out of memory\n");
		return NULL;
	}
	memcpy(data, d, l);
	BIO_free(tmp);
	return data;
}

/** read HTTP result from SSL */
static BIO*
read_http_result(SSL* ssl)
{
	size_t len = 0;
	char* data;
	BIO* m;
	if(!read_http_headers(ssl, &len)) {
		return NULL;
	}
	if(len == 0) {
		data = read_chunked_zero_terminate(ssl, &len);
	} else {
		data = read_data_chunk(ssl, len);
	}
	if(!data) return NULL;
	if(verb >= 4) print_data("read data", data, (int)len);
	m = BIO_new(BIO_s_mem());
	if(!m) {
		if(verb) printf("out of memory\n");
		free(data);
		exit(0);
	}
	BIO_write(m, data, (int)len);
	free(data);
	return m;
}

/** https to an IP addr, return BIO with pathname or NULL */
static BIO*
https_to_ip(struct ip_list* ip, const char* pathname, const char* urlname)
{
	int fd;
	SSL* ssl;
	BIO* bio;
	SSL_CTX* sslctx = setup_sslctx();
	if(!sslctx) {
		return NULL;
	}
	fd = connect_to_ip(ip);
	if(fd == -1) {
		SSL_CTX_free(sslctx);
		return NULL;
	}
	ssl = TLS_initiate(sslctx, fd);
	if(!ssl) {
		SSL_CTX_free(sslctx);
		fd_close(fd);
		return NULL;
	}
	if(!write_http_get(ssl, pathname, urlname)) {
		if(verb) printf("could not write to server\n");
		SSL_free(ssl);
		SSL_CTX_free(sslctx);
		fd_close(fd);
		return NULL;
	}
	bio = read_http_result(ssl);
	TLS_shutdown(fd, ssl, sslctx);
	return bio;
}

/**
 * Do a HTTPS, HTTP1.1 over TLS, to fetch a file
 * @param ip_list: list of IP addresses to use to fetch from.
 * @param pathname: pathname of file on server to GET.
 * @param urlname: name to pass as the virtual host for this request.
 * @return a memory BIO with the file in it.
 */
static BIO*
https(struct ip_list* ip_list, const char* pathname, const char* urlname)
{
	struct ip_list* ip;
	BIO* bio = NULL;
	/* try random address first, and work through the list */
	wipe_ip_usage(ip_list);
	while( (ip = pick_random_ip(ip_list)) ) {
		ip->used = 1;
		bio = https_to_ip(ip, pathname, urlname);
		if(bio) break;
	}
	if(!bio) {
		if(verb) printf("could not fetch %s\n", pathname);
		exit(0);
	} else {
		if(verb) printf("fetched %s (%d bytes)\n",
			pathname, (int)BIO_ctrl_pending(bio));
	}
	return bio;
}

/** XML parse private data during the parse */
struct xml_data {
	/** the parser, reference */
	XML_Parser parser;
	/** the current tag; malloced; or NULL outside of tags */
	char* tag;
	/** current date to use during the parse */
	time_t date;
	/** number of keys usefully read in */
	int num_keys;
	/** the compiled anchors as DS records */
	BIO* ds;

	/** do we want to use this anchor? */
	int use_key;
	/** the current anchor: Zone */
	BIO* czone;
	/** the current anchor: KeyTag */
	BIO* ctag;
	/** the current anchor: Algorithm */
	BIO* calgo;
	/** the current anchor: DigestType */
	BIO* cdigtype;
	/** the current anchor: Digest*/
	BIO* cdigest;
};

/** The BIO for the tag */
static BIO*
xml_selectbio(struct xml_data* data, const char* tag)
{
	BIO* b = NULL;
	if(strcasecmp(tag, "KeyTag") == 0)
		b = data->ctag;
	else if(strcasecmp(tag, "Algorithm") == 0)
		b = data->calgo;
	else if(strcasecmp(tag, "DigestType") == 0)
		b = data->cdigtype;
	else if(strcasecmp(tag, "Digest") == 0)
		b = data->cdigest;
	return b;
}

/**
 * XML handle character data, the data inside an element.
 * @param userData: xml_data structure
 * @param s: the character data.  May not all be in one callback.
 * 	NOT zero terminated.
 * @param len: length of this part of the data.
 */
static void
xml_charhandle(void *userData, const XML_Char *s, int len)
{
	struct xml_data* data = (struct xml_data*)userData;
	BIO* b = NULL;
	/* skip characters outside of elements */
	if(!data->tag)
		return;
	if(verb>=4) {
		int i;
		printf("%s%s charhandle: '",
			data->use_key?"use ":"",
			data->tag?data->tag:"none");
		for(i=0; i<len; i++)
			printf("%c", s[i]);
		printf("'\n");
	}
	if(strcasecmp(data->tag, "Zone") == 0) {
		if(BIO_write(data->czone, s, len) < 0) {
			if(verb) printf("out of memory in BIO_write\n");
			exit(0);
		}
		return;
	}
	/* only store if key is used */
	if(!data->use_key)
		return;
	b = xml_selectbio(data, data->tag);
	if(b) {
		if(BIO_write(b, s, len) < 0) {
			if(verb) printf("out of memory in BIO_write\n");
			exit(0);
		}
	}
}

/**
 * XML fetch value of particular attribute(by name) or NULL if not present.
 * @param atts: attribute array (from xml_startelem).
 * @param name: name of attribute to look for.
 * @return the value or NULL. (ptr into atts).
 */
static const XML_Char*
find_att(const XML_Char **atts, const XML_Char* name)
{
	int i;
	for(i=0; atts[i]; i+=2) {
		if(strcasecmp(atts[i], name) == 0)
			return atts[i+1];
	}
	return NULL;
}

/**
 * XML convert DateTime element to time_t.
 * [-]CCYY-MM-DDThh:mm:ss[Z|(+|-)hh:mm]
 * (with optional .ssssss fractional seconds)
 * @param str: the string
 * @return a time_t representation or 0 on failure.
 */
static time_t
xml_convertdate(const char* str)
{
	time_t t = 0;
	struct tm tm;
	const char* s;
	/* for this application, ignore minus in front;
	 * only positive dates are expected */
	s = str;
	if(s[0] == '-') s++;
	memset(&tm, 0, sizeof(tm));
	/* parse initial content of the string (lots of whitespace allowed) */
	s = strptime(s, "%t%Y%t-%t%m%t-%t%d%tT%t%H%t:%t%M%t:%t%S%t", &tm);
	if(!s) {
		if(verb) printf("xml_convertdate parse failure %s\n", str);
		return 0;
	}
	/* parse remainder of date string */
	if(*s == '.') {
		/* optional '.' and fractional seconds */
		int frac = 0, n = 0;
		if(sscanf(s+1, "%d%n", &frac, &n) < 1) {
			if(verb) printf("xml_convertdate f failure %s\n", str);
			return 0;
		}
		/* fraction is not used, time_t has second accuracy */
		s++;
		s+=n;
	}
	if(*s == 'Z' || *s == 'z') {
		/* nothing to do for this */
		s++;
	} else if(*s == '+' || *s == '-') {
		/* optional timezone spec: Z or +hh:mm or -hh:mm */
		int hr = 0, mn = 0, n = 0;
		if(sscanf(s+1, "%d:%d%n", &hr, &mn, &n) < 2) {
			if(verb) printf("xml_convertdate tz failure %s\n", str);
			return 0;
		}
		if(*s == '+') {
			tm.tm_hour += hr;
			tm.tm_min += mn;
		} else {
			tm.tm_hour -= hr;
			tm.tm_min -= mn;
		}
		s++;
		s += n;
	}
	if(*s != 0) {
		/* not ended properly */
		/* but ignore, (lenient) */
	}

	t = sldns_mktime_from_utc(&tm);
	if(t == (time_t)-1) {
		if(verb) printf("xml_convertdate mktime failure\n");
		return 0;
	}
	return t;
}

/**
 * XML handle the KeyDigest start tag, check validity periods.
 */
static void
handle_keydigest(struct xml_data* data, const XML_Char **atts)
{
	data->use_key = 0;
	if(find_att(atts, "validFrom")) {
		time_t from = xml_convertdate(find_att(atts, "validFrom"));
		if(from == 0) {
			if(verb) printf("error: xml cannot be parsed\n");
			exit(0);
		}
		if(data->date < from)
			return;
	}
	if(find_att(atts, "validUntil")) {
		time_t until = xml_convertdate(find_att(atts, "validUntil"));
		if(until == 0) {
			if(verb) printf("error: xml cannot be parsed\n");
			exit(0);
		}
		if(data->date > until)
			return;
	}
	/* yes we want to use this key */
	data->use_key = 1;
	(void)BIO_reset(data->ctag);
	(void)BIO_reset(data->calgo);
	(void)BIO_reset(data->cdigtype);
	(void)BIO_reset(data->cdigest);
}

/** See if XML element equals the zone name */
static int
xml_is_zone_name(BIO* zone, const char* name)
{
	char buf[1024];
	char* z = NULL;
	long zlen;
	(void)BIO_seek(zone, 0);
	zlen = BIO_get_mem_data(zone, &z);
	if(!zlen || !z) return 0;
	/* zero terminate */
	if(zlen >= (long)sizeof(buf)) return 0;
	memmove(buf, z, (size_t)zlen);
	buf[zlen] = 0;
	/* compare */
	return (strncasecmp(buf, name, strlen(name)) == 0);
}

/** 
 * XML start of element. This callback is called whenever an XML tag starts.
 * XML_Char is UTF8.
 * @param userData: the xml_data structure.
 * @param name: the tag that starts.
 * @param atts: array of strings, pairs of attr = value, ends with NULL.
 * 	i.e. att[0]="att[1]" att[2]="att[3]" att[4]isNull
 */
static void
xml_startelem(void *userData, const XML_Char *name, const XML_Char **atts)
{
	struct xml_data* data = (struct xml_data*)userData;
	BIO* b;
	if(verb>=4) printf("xml tag start '%s'\n", name);
	free(data->tag);
	data->tag = strdup(name);
	if(!data->tag) {
		if(verb) printf("out of memory\n");
		exit(0);
	}
	if(verb>=4) {
		int i;
		for(i=0; atts[i]; i+=2) {
			printf("  %s='%s'\n", atts[i], atts[i+1]);
		}
	}
	/* handle attributes to particular types */
	if(strcasecmp(name, "KeyDigest") == 0) {
		handle_keydigest(data, atts);
		return;
	} else if(strcasecmp(name, "Zone") == 0) {
		(void)BIO_reset(data->czone);
		return;
	}

	/* for other types we prepare to pick up the data */
	if(!data->use_key)
		return;
	b = xml_selectbio(data, data->tag);
	if(b) {
		/* empty it */
		(void)BIO_reset(b);
	}
}

/** Append str to bio */
static void
xml_append_str(BIO* b, const char* s)
{
	if(BIO_write(b, s, (int)strlen(s)) < 0) {
		if(verb) printf("out of memory in BIO_write\n");
		exit(0);
	}
}

/** Append bio to bio */
static void
xml_append_bio(BIO* b, BIO* a)
{
	char* z = NULL;
	long i, len;
	(void)BIO_seek(a, 0);
	len = BIO_get_mem_data(a, &z);
	if(!len || !z) {
		if(verb) printf("out of memory in BIO_write\n");
		exit(0);
	}
	/* remove newlines in the data here */
	for(i=0; i<len; i++) {
		if(z[i] == '\r' || z[i] == '\n')
			z[i] = ' ';
	}
	/* write to BIO */
	if(BIO_write(b, z, len) < 0) {
		if(verb) printf("out of memory in BIO_write\n");
		exit(0);
	}
}

/** write the parsed xml-DS to the DS list */
static void
xml_append_ds(struct xml_data* data)
{
	/* write DS to accumulated DS */
	xml_append_str(data->ds, ". IN DS ");
	xml_append_bio(data->ds, data->ctag);
	xml_append_str(data->ds, " ");
	xml_append_bio(data->ds, data->calgo);
	xml_append_str(data->ds, " ");
	xml_append_bio(data->ds, data->cdigtype);
	xml_append_str(data->ds, " ");
	xml_append_bio(data->ds, data->cdigest);
	xml_append_str(data->ds, "\n");
	data->num_keys++;
}

/**
 * XML end of element. This callback is called whenever an XML tag ends.
 * XML_Char is UTF8.
 * @param userData: the xml_data structure
 * @param name: the tag that ends.
 */
static void
xml_endelem(void *userData, const XML_Char *name)
{
	struct xml_data* data = (struct xml_data*)userData;
	if(verb>=4) printf("xml tag end   '%s'\n", name);
	free(data->tag);
	data->tag = NULL;
	if(strcasecmp(name, "KeyDigest") == 0) {
		if(data->use_key)
			xml_append_ds(data);
		data->use_key = 0;
	} else if(strcasecmp(name, "Zone") == 0) {
		if(!xml_is_zone_name(data->czone, ".")) {
			if(verb) printf("xml not for the right zone\n");
			exit(0);
		}
	}
}

/* Stop the parser when an entity declaration is encountered. For safety. */
static void
xml_entitydeclhandler(void *userData,
	const XML_Char *ATTR_UNUSED(entityName),
	int ATTR_UNUSED(is_parameter_entity),
	const XML_Char *ATTR_UNUSED(value), int ATTR_UNUSED(value_length),
	const XML_Char *ATTR_UNUSED(base),
	const XML_Char *ATTR_UNUSED(systemId),
	const XML_Char *ATTR_UNUSED(publicId),
	const XML_Char *ATTR_UNUSED(notationName))
{
#if HAVE_DECL_XML_STOPPARSER
	(void)XML_StopParser((XML_Parser)userData, XML_FALSE);
#else
	(void)userData;
#endif
}

/**
 * XML parser setup of the callbacks for the tags
 */
static void
xml_parse_setup(XML_Parser parser, struct xml_data* data, time_t now)
{
	char buf[1024];
	memset(data, 0, sizeof(*data));
	XML_SetUserData(parser, data);
	data->parser = parser;
	data->date = now;
	data->ds = BIO_new(BIO_s_mem());
	data->ctag = BIO_new(BIO_s_mem());
	data->czone = BIO_new(BIO_s_mem());
	data->calgo = BIO_new(BIO_s_mem());
	data->cdigtype = BIO_new(BIO_s_mem());
	data->cdigest = BIO_new(BIO_s_mem());
	if(!data->ds || !data->ctag || !data->calgo || !data->czone ||
		!data->cdigtype || !data->cdigest) {
		if(verb) printf("out of memory\n");
		exit(0);
	}
	snprintf(buf, sizeof(buf), "; created by unbound-anchor on %s",
		ctime(&now));
	if(BIO_write(data->ds, buf, (int)strlen(buf)) < 0) {
		if(verb) printf("out of memory\n");
		exit(0);
	}
	XML_SetEntityDeclHandler(parser, xml_entitydeclhandler);
	XML_SetElementHandler(parser, xml_startelem, xml_endelem);
	XML_SetCharacterDataHandler(parser, xml_charhandle);
}

/**
 * Perform XML parsing of the root-anchors file
 * Its format description can be read here
 * https://data.iana.org/root-anchors/draft-icann-dnssec-trust-anchor.txt
 * It uses libexpat.
 * @param xml: BIO with xml data.
 * @param now: the current time for checking DS validity periods.
 * @return memoryBIO with the DS data in zone format.
 * 	or NULL if the zone is insecure.
 * 	(It exit()s on error)
 */
static BIO*
xml_parse(BIO* xml, time_t now)
{
	char* pp;
	int len;
	XML_Parser parser;
	struct xml_data data;

	parser = XML_ParserCreate(NULL);
	if(!parser) {
		if(verb) printf("could not XML_ParserCreate\n");
		exit(0);
	}

	/* setup callbacks */
	xml_parse_setup(parser, &data, now);

	/* parse it */
	(void)BIO_seek(xml, 0);
	len = (int)BIO_get_mem_data(xml, &pp);
	if(!len || !pp) {
		if(verb) printf("out of memory\n");
		exit(0);
	}
	if(!XML_Parse(parser, pp, len, 1 /*isfinal*/ )) {
		const char *e = XML_ErrorString(XML_GetErrorCode(parser));
		if(verb) printf("XML_Parse failure %s\n", e?e:"");
		exit(0);
	}

	/* parsed */
	if(verb) printf("XML was parsed successfully, %d keys\n",
			data.num_keys);
	free(data.tag);
	XML_ParserFree(parser);

	if(verb >= 4) {
		(void)BIO_seek(data.ds, 0);
		len = BIO_get_mem_data(data.ds, &pp);
		printf("got DS bio %d: '", len);
		if(!fwrite(pp, (size_t)len, 1, stdout))
			/* compilers do not allow us to ignore fwrite .. */
			fprintf(stderr, "error writing to stdout\n");
		printf("'\n");
	}
	BIO_free(data.czone);
	BIO_free(data.ctag);
	BIO_free(data.calgo);
	BIO_free(data.cdigtype);
	BIO_free(data.cdigest);

	if(data.num_keys == 0) {
		/* the root zone seems to have gone insecure */
		BIO_free(data.ds);
		return NULL;
	} else {
		return data.ds;
	}
}

/* get key usage out of its extension, returns 0 if no key_usage extension */
static unsigned long
get_usage_of_ex(X509* cert)
{
	unsigned long val = 0;
	ASN1_BIT_STRING* s;
	if((s=X509_get_ext_d2i(cert, NID_key_usage, NULL, NULL))) {
		if(s->length > 0) {
			val = s->data[0];
			if(s->length > 1)
				val |= s->data[1] << 8;
		}
		ASN1_BIT_STRING_free(s);
	}
	return val;
}

/** get valid signers from the list of signers in the signature */
static STACK_OF(X509)*
get_valid_signers(PKCS7* p7, const char* p7signer)
{
	int i;
	STACK_OF(X509)* validsigners = sk_X509_new_null();
	STACK_OF(X509)* signers = PKCS7_get0_signers(p7, NULL, 0);
	unsigned long usage = 0;
	if(!validsigners) {
		if(verb) printf("out of memory\n");
		sk_X509_free(signers);
		return NULL;
	}
	if(!signers) {
		if(verb) printf("no signers in pkcs7 signature\n");
		sk_X509_free(validsigners);
		return NULL;
	}
	for(i=0; i<sk_X509_num(signers); i++) {
		X509_NAME* nm = X509_get_subject_name(
			sk_X509_value(signers, i));
		char buf[1024];
		if(!nm) {
			if(verb) printf("signer %d: cert has no subject name\n", i);
			continue;
		}
		if(verb && nm) {
			char* nmline = X509_NAME_oneline(nm, buf,
				(int)sizeof(buf));
			printf("signer %d: Subject: %s\n", i,
				nmline?nmline:"no subject");
			if(verb >= 3 && X509_NAME_get_text_by_NID(nm,
				NID_commonName, buf, (int)sizeof(buf)))
				printf("commonName: %s\n", buf);
			if(verb >= 3 && X509_NAME_get_text_by_NID(nm,
				NID_pkcs9_emailAddress, buf, (int)sizeof(buf)))
				printf("emailAddress: %s\n", buf);
		}
		if(verb) {
			int ku_loc = X509_get_ext_by_NID(
				sk_X509_value(signers, i), NID_key_usage, -1);
			if(verb >= 3 && ku_loc >= 0) {
				X509_EXTENSION *ex = X509_get_ext(
					sk_X509_value(signers, i), ku_loc);
				if(ex) {
					printf("keyUsage: ");
					X509V3_EXT_print_fp(stdout, ex, 0, 0);
					printf("\n");
				}
			}
		}
		if(!p7signer || strcmp(p7signer, "")==0) {
			/* there is no name to check, return all records */
			if(verb) printf("did not check commonName of signer\n");
		} else {
			if(!X509_NAME_get_text_by_NID(nm,
				NID_pkcs9_emailAddress,
				buf, (int)sizeof(buf))) {
				if(verb) printf("removed cert with no name\n");
				continue; /* no name, no use */
			}
			if(strcmp(buf, p7signer) != 0) {
				if(verb) printf("removed cert with wrong name\n");
				continue; /* wrong name, skip it */
			}
		}

		/* check that the key usage allows digital signatures
		 * (the p7s) */
		usage = get_usage_of_ex(sk_X509_value(signers, i));
		if(!(usage & KU_DIGITAL_SIGNATURE)) {
			if(verb) printf("removed cert with no key usage Digital Signature allowed\n");
			continue;
		}

		/* we like this cert, add it to our list of valid
		 * signers certificates */
		sk_X509_push(validsigners, sk_X509_value(signers, i));
	}
	sk_X509_free(signers);
	return validsigners;
}

/** verify a PKCS7 signature, false on failure */
static int
verify_p7sig(BIO* data, BIO* p7s, STACK_OF(X509)* trust, const char* p7signer)
{
	PKCS7* p7;
	X509_STORE *store = X509_STORE_new();
	STACK_OF(X509)* validsigners;
	int secure = 0;
	int i;
#ifdef X509_V_FLAG_CHECK_SS_SIGNATURE
	X509_VERIFY_PARAM* param = X509_VERIFY_PARAM_new();
	if(!param) {
		if(verb) printf("out of memory\n");
		X509_STORE_free(store);
		return 0;
	}
	/* do the selfcheck on the root certificate; it checks that the
	 * input is valid */
	X509_VERIFY_PARAM_set_flags(param, X509_V_FLAG_CHECK_SS_SIGNATURE);
	if(store) X509_STORE_set1_param(store, param);
#endif
	if(!store) {
		if(verb) printf("out of memory\n");
#ifdef X509_V_FLAG_CHECK_SS_SIGNATURE
		X509_VERIFY_PARAM_free(param);
#endif
		return 0;
	}
#ifdef X509_V_FLAG_CHECK_SS_SIGNATURE
	X509_VERIFY_PARAM_free(param);
#endif

	(void)BIO_seek(p7s, 0);
	(void)BIO_seek(data, 0);

	/* convert p7s to p7 (the signature) */
	p7 = d2i_PKCS7_bio(p7s, NULL);
	if(!p7) {
		if(verb) printf("could not parse p7s signature file\n");
		X509_STORE_free(store);
		return 0;
	}
	if(verb >= 2) printf("parsed the PKCS7 signature\n");

	/* convert trust to trusted certificate store */
	for(i=0; i<sk_X509_num(trust); i++) {
		if(!X509_STORE_add_cert(store, sk_X509_value(trust, i))) {
			if(verb) printf("failed X509_STORE_add_cert\n");
			X509_STORE_free(store);
			PKCS7_free(p7);
			return 0;
		}
	}
	if(verb >= 2) printf("setup the X509_STORE\n");

	/* check what is in the Subject name of the certificates,
	 * and build a stack that contains only the right certificates */
	validsigners = get_valid_signers(p7, p7signer);
	if(!validsigners) {
			X509_STORE_free(store);
			PKCS7_free(p7);
			return 0;
	}
	if(PKCS7_verify(p7, validsigners, store, data, NULL, PKCS7_NOINTERN) == 1) {
		secure = 1;
		if(verb) printf("the PKCS7 signature verified\n");
	} else {
		if(verb) {
			ERR_print_errors_fp(stdout);
		}
	}

	sk_X509_free(validsigners);
	X509_STORE_free(store);
	PKCS7_free(p7);
	return secure;
}

/** write unsigned root anchor file, a 5011 revoked tp */
static void
write_unsigned_root(const char* root_anchor_file)
{
	FILE* out;
	time_t now = time(NULL);
	out = fopen(root_anchor_file, "w");
	if(!out) {
		if(verb) printf("%s: %s\n", root_anchor_file, strerror(errno));
		return;
	}
	if(fprintf(out, "; autotrust trust anchor file\n"
		";;REVOKED\n"
		";;id: . 1\n"
		"; This file was written by unbound-anchor on %s"
		"; It indicates that the root does not use DNSSEC\n"
		"; to restart DNSSEC overwrite this file with a\n"
		"; valid trustanchor or (empty-it and run unbound-anchor)\n"
		, ctime(&now)) < 0) {
		if(verb) printf("failed to write 'unsigned' to %s\n",
			root_anchor_file);
		if(verb && errno != 0) printf("%s\n", strerror(errno));
	}
	fflush(out);
#ifdef HAVE_FSYNC
	fsync(fileno(out));
#else
	FlushFileBuffers((HANDLE)_get_osfhandle(_fileno(out)));
#endif
	fclose(out);
}

/** write root anchor file */
static void
write_root_anchor(const char* root_anchor_file, BIO* ds)
{
	char* pp = NULL;
	int len;
	FILE* out;
	(void)BIO_seek(ds, 0);
	len = BIO_get_mem_data(ds, &pp);
	if(!len || !pp) {
		if(verb) printf("out of memory\n");
		return;
	}
	out = fopen(root_anchor_file, "w");
	if(!out) {
		if(verb) printf("%s: %s\n", root_anchor_file, strerror(errno));
		return;
	}
	if(fwrite(pp, (size_t)len, 1, out) != 1) {
		if(verb) printf("failed to write all data to %s\n",
			root_anchor_file);
		if(verb && errno != 0) printf("%s\n", strerror(errno));
	}
	fflush(out);
#ifdef HAVE_FSYNC
	fsync(fileno(out));
#else
	FlushFileBuffers((HANDLE)_get_osfhandle(_fileno(out)));
#endif
	fclose(out);
}

/** Perform the verification and update of the trustanchor file */
static void
verify_and_update_anchor(const char* root_anchor_file, BIO* xml, BIO* p7s,
	STACK_OF(X509)* cert, const char* p7signer)
{
	BIO* ds;

	/* verify xml file */
	if(!verify_p7sig(xml, p7s, cert, p7signer)) {
		printf("the PKCS7 signature failed\n");
		exit(0);
	}

	/* parse the xml file into DS records */
	ds = xml_parse(xml, time(NULL));
	if(!ds) {
		/* the root zone is unsigned now */
		write_unsigned_root(root_anchor_file);
	} else {
		/* reinstate 5011 tracking */
		write_root_anchor(root_anchor_file, ds);
	}
	BIO_free(ds);
}

#ifdef USE_WINSOCK
static void do_wsa_cleanup(void) { WSACleanup(); }
#endif

/** perform actual certupdate work */
static int
do_certupdate(const char* root_anchor_file, const char* root_cert_file,
	const char* urlname, const char* xmlname, const char* p7sname,
	const char* p7signer, const char* res_conf, const char* root_hints,
	const char* debugconf, int ip4only, int ip6only, int port)
{
	STACK_OF(X509)* cert;
	BIO *xml, *p7s;
	struct ip_list* ip_list = NULL;

	/* read pem file or provide builtin */
	cert = read_cert_or_builtin(root_cert_file);

	/* lookup A, AAAA for the urlname (or parse urlname if IP address) */
	ip_list = resolve_name(urlname, port, res_conf, root_hints, debugconf,
		ip4only, ip6only);

#ifdef USE_WINSOCK
	if(1) { /* libunbound finished, startup WSA for the https connection */
		WSADATA wsa_data;
		int r;
		if((r = WSAStartup(MAKEWORD(2,2), &wsa_data)) != 0) {
			if(verb) printf("WSAStartup failed: %s\n",
				wsa_strerror(r));
			exit(0);
		}
		atexit(&do_wsa_cleanup);
	}
#endif

	/* fetch the necessary files over HTTPS */
	xml = https(ip_list, xmlname, urlname);
	p7s = https(ip_list, p7sname, urlname);

	/* verify and update the root anchor */
	verify_and_update_anchor(root_anchor_file, xml, p7s, cert, p7signer);
	if(verb) printf("success: the anchor has been updated "
			"using the cert\n");

	BIO_free(xml);
	BIO_free(p7s);
#ifndef S_SPLINT_S
	sk_X509_pop_free(cert, X509_free);
#endif
	ip_list_free(ip_list);
	return 1;
}

/**
 * Try to read the root RFC5011 autotrust anchor file,
 * @param file: filename.
 * @return:
 * 	0 if does not exist or empty
 * 	1 if trust-point-revoked-5011
 * 	2 if it is OK.
 */
static int
try_read_anchor(const char* file)
{
	int empty = 1;
	char line[10240];
	char* p;
	FILE* in = fopen(file, "r");
	if(!in) {
		/* only if the file does not exist, can we fix it */
		if(errno != ENOENT) {
			if(verb) printf("%s: %s\n", file, strerror(errno));
			if(verb) printf("error: cannot access the file\n");
			exit(0);
		}
		if(verb) printf("%s does not exist\n", file);
		return 0;
	}
	while(fgets(line, (int)sizeof(line), in)) {
		line[sizeof(line)-1] = 0;
		if(strncmp(line, ";;REVOKED", 9) == 0) {
			fclose(in);
			if(verb) printf("%s : the trust point is revoked\n"
				"and the zone is considered unsigned.\n"
				"if you wish to re-enable, delete the file\n",
				file);
			return 1;
		}
		p=line;
		while(*p == ' ' || *p == '\t')
			p++;
		if(p[0]==0 || p[0]=='\n' || p[0]==';') continue;
		/* this line is a line of content */
		empty = 0;
	}
	fclose(in);
	if(empty) {
		if(verb) printf("%s is empty\n", file);
		return 0;
	}
	if(verb) printf("%s has content\n", file);
	return 2;
}

/** Write the builtin root anchor to a file */
static void
write_builtin_anchor(const char* file)
{
	const char* builtin_root_anchor = get_builtin_ds();
	FILE* out = fopen(file, "w");
	if(!out) {
		if(verb) printf("%s: %s\n", file, strerror(errno));
		if(verb) printf("  could not write builtin anchor\n");
		return;
	}
	if(!fwrite(builtin_root_anchor, strlen(builtin_root_anchor), 1, out)) {
		if(verb) printf("%s: %s\n", file, strerror(errno));
		if(verb) printf("  could not complete write builtin anchor\n");
	}
	fclose(out);
}

/** 
 * Check the root anchor file.
 * If does not exist, provide builtin and write file.
 * If empty, provide builtin and write file.
 * If trust-point-revoked-5011 file: make the program exit.
 * @param root_anchor_file: filename of the root anchor.
 * @param used_builtin: set to 1 if the builtin is written.
 * @return 0 if trustpoint is insecure, 1 on success.  Exit on failure.
 */
static int
provide_builtin(const char* root_anchor_file, int* used_builtin)
{
	/* try to read it */
	switch(try_read_anchor(root_anchor_file))
	{
		case 0: /* no exist or empty */
			write_builtin_anchor(root_anchor_file);
			*used_builtin = 1;
			break;
		case 1: /* revoked tp */
			return 0;	
		case 2: /* it is fine */
		default:
			break;
	}
	return 1;
}

/**
 * add an autotrust anchor for the root to the context
 */
static void
add_5011_probe_root(struct ub_ctx* ctx, const char* root_anchor_file)
{
	int r;
	r = ub_ctx_set_option(ctx, "auto-trust-anchor-file:", root_anchor_file);
	if(r) {
		if(verb) printf("add 5011 probe to ctx: %s\n", ub_strerror(r));
		ub_ctx_delete(ctx);
		exit(0);
	}
}

/**
 * Prime the root key and return the result.  Exit on error.
 * @param ctx: the unbound context to perform the priming with.
 * @return: the result of the prime, on error it exit()s.
 */
static struct ub_result*
prime_root_key(struct ub_ctx* ctx)
{
	struct ub_result* res = NULL;
	int r;
	r = ub_resolve(ctx, ".", LDNS_RR_TYPE_DNSKEY, LDNS_RR_CLASS_IN, &res);
	if(r) {
		if(verb) printf("resolve DNSKEY: %s\n", ub_strerror(r));
		ub_ctx_delete(ctx);
		exit(0);
	}
	if(!res) {
		if(verb) printf("out of memory\n");
		ub_ctx_delete(ctx);
		exit(0);
	}
	return res;
}

/** see if ADDPEND keys exist in autotrust file (if possible) */
static int
read_if_pending_keys(const char* file)
{
	FILE* in = fopen(file, "r");
	char line[8192];
	if(!in) {
		if(verb>=2) printf("%s: %s\n", file, strerror(errno));
		return 0;
	}
	while(fgets(line, (int)sizeof(line), in)) {
		if(line[0]==';') continue;
		if(strstr(line, "[ ADDPEND ]")) {
			fclose(in);
			if(verb) printf("RFC5011-state has ADDPEND keys\n");
			return 1;
		}
	}
	fclose(in);
	return 0;
}

/** read last successful probe time from autotrust file (if possible) */
static int32_t
read_last_success_time(const char* file)
{
	FILE* in = fopen(file, "r");
	char line[1024];
	if(!in) {
		if(verb) printf("%s: %s\n", file, strerror(errno));
		return 0;
	}
	while(fgets(line, (int)sizeof(line), in)) {
		if(strncmp(line, ";;last_success: ", 16) == 0) {
			char* e;
			time_t x = (unsigned int)strtol(line+16, &e, 10);
			fclose(in);
			if(line+16 == e) {
				if(verb) printf("failed to parse "
					"last_success probe time\n");
				return 0;
			}
			if(verb) printf("last successful probe: %s", ctime(&x));
			return (int32_t)x;
		}
	}
	fclose(in);
	if(verb) printf("no last_success probe time in anchor file\n");
	return 0;
}

/**
 * Read autotrust 5011 probe file and see if the date
 * compared to the current date allows a certupdate.
 * If the last successful probe was recent then 5011 cannot be behind,
 * and the failure cannot be solved with a certupdate.
 * The debugconf is to validation-override the date for testing.
 * @param root_anchor_file: filename of root key
 * @return true if certupdate is ok.
 */
static int
probe_date_allows_certupdate(const char* root_anchor_file)
{
	int has_pending_keys = read_if_pending_keys(root_anchor_file);
	int32_t last_success = read_last_success_time(root_anchor_file);
	int32_t now = (int32_t)time(NULL);
	int32_t leeway = 30 * 24 * 3600; /* 30 days leeway */
	/* if the date is before 2010-07-15:00.00.00 then the root has not
	 * been signed yet, and thus we refuse to take action. */
	if(time(NULL) < xml_convertdate("2010-07-15T00:00:00")) {
		if(verb) printf("the date is before the root was first signed,"
			" please correct the clock\n");
		return 0;
	}
	if(last_success == 0)
		return 1; /* no probe time */
	if(has_pending_keys)
		return 1; /* key in ADDPEND state, a previous probe has
		inserted that, and it was present in all recent probes,
		but it has not become active.  The 30 day timer may not have
		expired, but we know(for sure) there is a rollover going on.
		If we only managed to pickup the new key on its last day
		of announcement (for example) this can happen. */
	if(now - last_success < 0) {
		if(verb) printf("the last successful probe is in the future,"
			" clock was modified\n");
		return 0;
	}
	if(now - last_success >= leeway) {
		if(verb) printf("the last successful probe was more than 30 "
			"days ago\n");
		return 1;
	}
	if(verb) printf("the last successful probe is recent\n");
	return 0;
}

static struct ub_result *
fetch_root_key(const char* root_anchor_file, const char* res_conf,
	const char* root_hints, const char* debugconf,
	int ip4only, int ip6only)
{
	struct ub_ctx* ctx;
	struct ub_result* dnskey;

	ctx = create_unbound_context(res_conf, root_hints, debugconf,
		ip4only, ip6only);
	add_5011_probe_root(ctx, root_anchor_file);
	dnskey = prime_root_key(ctx);
	ub_ctx_delete(ctx);
	return dnskey;
}

/** perform the unbound-anchor work */
static int
do_root_update_work(const char* root_anchor_file, const char* root_cert_file,
	const char* urlname, const char* xmlname, const char* p7sname,
	const char* p7signer, const char* res_conf, const char* root_hints,
	const char* debugconf, int ip4only, int ip6only, int force,
	int res_conf_fallback, int port)
{
	struct ub_result* dnskey;
	int used_builtin = 0;
	int rcode;

	/* see if builtin rootanchor needs to be provided, or if
	 * rootanchor is 'revoked-trust-point' */
	if(!provide_builtin(root_anchor_file, &used_builtin))
		return 0;

	/* make unbound context with 5011-probe for root anchor,
	 * and probe . DNSKEY */
	dnskey = fetch_root_key(root_anchor_file, res_conf,
		root_hints, debugconf, ip4only, ip6only);
	rcode = dnskey->rcode;

	if (res_conf_fallback && res_conf && !dnskey->secure) {
		if (verb) printf("%s failed, retrying direct\n", res_conf);
		ub_resolve_free(dnskey);
		/* try direct query without res_conf */
		dnskey = fetch_root_key(root_anchor_file, NULL,
			root_hints, debugconf, ip4only, ip6only);
		if (rcode != 0 && dnskey->rcode == 0) {
			res_conf = NULL;
			rcode = 0;
		}
	}

	/* if secure: exit */
	if(dnskey->secure && !force) {
		if(verb) printf("success: the anchor is ok\n");
		ub_resolve_free(dnskey);
		return used_builtin;
	}
	if(force && verb) printf("debug cert update forced\n");
	ub_resolve_free(dnskey);

	/* if not (and NOERROR): check date and do certupdate */
	if((rcode == 0 &&
		probe_date_allows_certupdate(root_anchor_file)) || force) {
		if(do_certupdate(root_anchor_file, root_cert_file, urlname,
			xmlname, p7sname, p7signer, res_conf, root_hints,
			debugconf, ip4only, ip6only, port))
			return 1;
		return used_builtin;
	}
	if(verb) printf("fail: the anchor is NOT ok and could not be fixed\n");
	return used_builtin;
}

/** getopt global, in case header files fail to declare it. */
extern int optind;
/** getopt global, in case header files fail to declare it. */
extern char* optarg;

/** Main routine for unbound-anchor */
int main(int argc, char* argv[])
{
	int c;
	const char* root_anchor_file = ROOT_ANCHOR_FILE;
	const char* root_cert_file = ROOT_CERT_FILE;
	const char* urlname = URLNAME;
	const char* xmlname = XMLNAME;
	const char* p7sname = P7SNAME;
	const char* p7signer = P7SIGNER;
	const char* res_conf = NULL;
	const char* root_hints = NULL;
	const char* debugconf = NULL;
	int dolist=0, ip4only=0, ip6only=0, force=0, port = HTTPS_PORT;
	int res_conf_fallback = 0;
	/* parse the options */
	while( (c=getopt(argc, argv, "46C:FRP:a:c:f:hln:r:s:u:vx:")) != -1) {
		switch(c) {
		case 'l':
			dolist = 1;
			break;
		case '4':
			ip4only = 1;
			break;
		case '6':
			ip6only = 1;
			break;
		case 'a':
			root_anchor_file = optarg;
			break;
		case 'c':
			root_cert_file = optarg;
			break;
		case 'u':
			urlname = optarg;
			break;
		case 'x':
			xmlname = optarg;
			break;
		case 's':
			p7sname = optarg;
			break;
		case 'n':
			p7signer = optarg;
			break;
		case 'f':
			res_conf = optarg;
			break;
		case 'r':
			root_hints = optarg;
			break;
		case 'R':
			res_conf_fallback = 1;
			break;
		case 'C':
			debugconf = optarg;
			break;
		case 'F':
			force = 1;
			break;
		case 'P':
			port = atoi(optarg);
			break;
		case 'v':
			verb++;
			break;
		case '?':
		case 'h':
		default:
			usage();
		}
	}
	argc -= optind;
	/* argv += optind; not using further arguments */
	if(argc != 0)
		usage();

#ifdef HAVE_ERR_LOAD_CRYPTO_STRINGS
	ERR_load_crypto_strings();
#endif
#if OPENSSL_VERSION_NUMBER < 0x10100000 || !defined(HAVE_OPENSSL_INIT_SSL)
	ERR_load_SSL_strings();
#endif
#if OPENSSL_VERSION_NUMBER < 0x10100000 || !defined(HAVE_OPENSSL_INIT_CRYPTO)
	OpenSSL_add_all_algorithms();
#else
	OPENSSL_init_crypto(OPENSSL_INIT_ADD_ALL_CIPHERS
		| OPENSSL_INIT_ADD_ALL_DIGESTS
		| OPENSSL_INIT_LOAD_CRYPTO_STRINGS, NULL);
#endif
#if OPENSSL_VERSION_NUMBER < 0x10100000 || !defined(HAVE_OPENSSL_INIT_SSL)
	(void)SSL_library_init();
#else
	(void)OPENSSL_init_ssl(OPENSSL_INIT_LOAD_SSL_STRINGS, NULL);
#endif

	if(dolist) do_list_builtin();

	return do_root_update_work(root_anchor_file, root_cert_file, urlname,
		xmlname, p7sname, p7signer, res_conf, root_hints, debugconf,
		ip4only, ip6only, force, res_conf_fallback, port);
}
