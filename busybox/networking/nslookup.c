/* vi: set sw=4 ts=4: */

//config:config NSLOOKUP
//config:	bool "nslookup (4.5 kb)"
//config:	default y
//config:	help
//config:	nslookup is a tool to query Internet name servers.
//config:
//config:config FEATURE_NSLOOKUP_BIG
//config:	bool "Use internal resolver code instead of libc"
//config:	depends on NSLOOKUP
//config:	default y
//config:
//config:config FEATURE_NSLOOKUP_LONG_OPTIONS
//config:	bool "Enable long options"
//config:	default y
//config:	depends on FEATURE_NSLOOKUP_BIG && LONG_OPTS

//applet:IF_NSLOOKUP(APPLET(nslookup, BB_DIR_USR_BIN, BB_SUID_DROP))

//kbuild:lib-$(CONFIG_NSLOOKUP) += nslookup.o

//usage:#define nslookup_trivial_usage
//usage:       IF_FEATURE_NSLOOKUP_BIG("[-type=QUERY_TYPE] [-debug] ") "HOST [DNS_SERVER]"
//usage:#define nslookup_full_usage "\n\n"
//usage:       "Query DNS about HOST"
//usage:       IF_FEATURE_NSLOOKUP_BIG("\n")
//usage:       IF_FEATURE_NSLOOKUP_BIG("\nQUERY_TYPE: soa,ns,a,"IF_FEATURE_IPV6("aaaa,")"cname,mx,txt,ptr,any")
//usage:#define nslookup_example_usage
//usage:       "$ nslookup localhost\n"
//usage:       "Server:     default\n"
//usage:       "Address:    default\n"
//usage:       "\n"
//usage:       "Name:       debian\n"
//usage:       "Address:    127.0.0.1\n"

#include <resolv.h>
#include <net/if.h>	/* for IFNAMSIZ */
//#include <arpa/inet.h>
//#include <netdb.h>
#include "libbb.h"
#include "common_bufsiz.h"


#if !ENABLE_FEATURE_NSLOOKUP_BIG

/*
 * Mini nslookup implementation for busybox
 *
 * Copyright (C) 1999,2000 by Lineo, inc. and John Beppu
 * Copyright (C) 1999,2000,2001 by John Beppu <beppu@codepoet.org>
 *
 * Correct default name server display and explicit name server option
 * added by Ben Zeckel <bzeckel@hmc.edu> June 2001
 *
 * Licensed under GPLv2 or later, see file LICENSE in this source tree.
 */

/*
 * I'm only implementing non-interactive mode;
 * I totally forgot nslookup even had an interactive mode.
 *
 * This applet is the only user of res_init(). Without it,
 * you may avoid pulling in _res global from libc.
 */

/* Examples of 'standard' nslookup output
 * $ nslookup yahoo.com
 * Server:         128.193.0.10
 * Address:        128.193.0.10#53
 *
 * Non-authoritative answer:
 * Name:   yahoo.com
 * Address: 216.109.112.135
 * Name:   yahoo.com
 * Address: 66.94.234.13
 *
 * $ nslookup 204.152.191.37
 * Server:         128.193.4.20
 * Address:        128.193.4.20#53
 *
 * Non-authoritative answer:
 * 37.191.152.204.in-addr.arpa     canonical name = 37.32-27.191.152.204.in-addr.arpa.
 * 37.32-27.191.152.204.in-addr.arpa       name = zeus-pub2.kernel.org.
 *
 * Authoritative answers can be found from:
 * 32-27.191.152.204.in-addr.arpa  nameserver = ns1.kernel.org.
 * 32-27.191.152.204.in-addr.arpa  nameserver = ns2.kernel.org.
 * 32-27.191.152.204.in-addr.arpa  nameserver = ns3.kernel.org.
 * ns1.kernel.org  internet address = 140.211.167.34
 * ns2.kernel.org  internet address = 204.152.191.4
 * ns3.kernel.org  internet address = 204.152.191.36
 */

static int print_host(const char *hostname, const char *header)
{
	/* We can't use xhost2sockaddr() - we want to get ALL addresses,
	 * not just one */
	struct addrinfo *result = NULL;
	int rc;
	struct addrinfo hint;

	memset(&hint, 0 , sizeof(hint));
	/* hint.ai_family = AF_UNSPEC; - zero anyway */
	/* Needed. Or else we will get each address thrice (or more)
	 * for each possible socket type (tcp,udp,raw...): */
	hint.ai_socktype = SOCK_STREAM;
	// hint.ai_flags = AI_CANONNAME;
	rc = getaddrinfo(hostname, NULL /*service*/, &hint, &result);

	if (rc == 0) {
		struct addrinfo *cur = result;
		unsigned cnt = 0;

		printf("%-10s %s\n", header, hostname);
		// puts(cur->ai_canonname); ?
		while (cur) {
			char *dotted, *revhost;
			dotted = xmalloc_sockaddr2dotted_noport(cur->ai_addr);
			revhost = xmalloc_sockaddr2hostonly_noport(cur->ai_addr);

			printf("Address %u: %s%c", ++cnt, dotted, revhost ? ' ' : '\n');
			if (revhost) {
				puts(revhost);
				if (ENABLE_FEATURE_CLEAN_UP)
					free(revhost);
			}
			if (ENABLE_FEATURE_CLEAN_UP)
				free(dotted);
			cur = cur->ai_next;
		}
	} else {
#if ENABLE_VERBOSE_RESOLUTION_ERRORS
		bb_error_msg("can't resolve '%s': %s", hostname, gai_strerror(rc));
#else
		bb_error_msg("can't resolve '%s'", hostname);
#endif
	}
	if (ENABLE_FEATURE_CLEAN_UP && result)
		freeaddrinfo(result);
	return (rc != 0);
}

/* lookup the default nameserver and display it */
static void server_print(void)
{
	char *server;
	struct sockaddr *sa;

#if ENABLE_FEATURE_IPV6
	sa = (struct sockaddr*)_res._u._ext.nsaddrs[0];
	if (!sa)
#endif
		sa = (struct sockaddr*)&_res.nsaddr_list[0];
	server = xmalloc_sockaddr2dotted_noport(sa);

	print_host(server, "Server:");
	if (ENABLE_FEATURE_CLEAN_UP)
		free(server);
	bb_putchar('\n');
}

/* alter the global _res nameserver structure to use
   an explicit dns server instead of what is in /etc/resolv.conf */
static void set_default_dns(const char *server)
{
	len_and_sockaddr *lsa;

	if (!server)
		return;

	/* NB: this works even with, say, "[::1]:5353"! :) */
	lsa = xhost2sockaddr(server, 53);

	if (lsa->u.sa.sa_family == AF_INET) {
		_res.nscount = 1;
		/* struct copy */
		_res.nsaddr_list[0] = lsa->u.sin;
	}
#if ENABLE_FEATURE_IPV6
	/* Hoped libc can cope with IPv4 address there too.
	 * No such luck, glibc 2.4 segfaults even with IPv6,
	 * maybe I misunderstand how to make glibc use IPv6 addr?
	 * (uclibc 0.9.31+ should work) */
	if (lsa->u.sa.sa_family == AF_INET6) {
		// glibc neither SEGVs nor sends any dgrams with this
		// (strace shows no socket ops):
		//_res.nscount = 0;
		_res._u._ext.nscount = 1;
		/* store a pointer to part of malloc'ed lsa */
		_res._u._ext.nsaddrs[0] = &lsa->u.sin6;
		/* must not free(lsa)! */
	}
#endif
}

int nslookup_main(int argc, char **argv) MAIN_EXTERNALLY_VISIBLE;
int nslookup_main(int argc, char **argv)
{
	/* We allow 1 or 2 arguments.
	 * The first is the name to be looked up and the second is an
	 * optional DNS server with which to do the lookup.
	 * More than 3 arguments is an error to follow the pattern of the
	 * standard nslookup */
	if (!argv[1] || argv[1][0] == '-' || argc > 3)
		bb_show_usage();

	/* initialize DNS structure _res used in printing the default
	 * name server and in the explicit name server option feature. */
	res_init();
	/* rfc2133 says this enables IPv6 lookups */
	/* (but it also says "may be enabled in /etc/resolv.conf") */
	/*_res.options |= RES_USE_INET6;*/

	set_default_dns(argv[2]);

	server_print();

	/* getaddrinfo and friends are free to request a resolver
	 * reinitialization. Just in case, set_default_dns() again
	 * after getaddrinfo (in server_print). This reportedly helps
	 * with bug 675 "nslookup does not properly use second argument"
	 * at least on Debian Wheezy and Openwrt AA (eglibc based).
	 */
	set_default_dns(argv[2]);

	return print_host(argv[1], "Name:");
}


#else /****** A version from LEDE / OpenWRT ******/

/*
 * musl compatible nslookup
 *
 * Copyright (C) 2017 Jo-Philipp Wich <jo@mein.io>
 *
 * Permission to use, copy, modify, and/or distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#if 0
# define dbg(...) fprintf(stderr, __VA_ARGS__)
#else
# define dbg(...) ((void)0)
#endif

struct ns {
	const char *name;
	len_and_sockaddr *lsa;
	int failures;
	int replies;
};

struct query {
	const char *name;
	unsigned qlen;
//	unsigned latency;
//	uint8_t rcode;
	unsigned char query[512];
//	unsigned char reply[512];
};

static const struct {
	unsigned char type;
	char name[7];
} qtypes[] = {
	{ ns_t_soa,   "SOA"   },
	{ ns_t_ns,    "NS"    },
	{ ns_t_a,     "A"     },
#if ENABLE_FEATURE_IPV6
	{ ns_t_aaaa,  "AAAA"  },
#endif
	{ ns_t_cname, "CNAME" },
	{ ns_t_mx,    "MX"    },
	{ ns_t_txt,   "TXT"   },
	{ ns_t_ptr,   "PTR"   },
	{ ns_t_any,   "ANY"   },
};

static const char *const rcodes[] = {
	"NOERROR",    // 0
	"FORMERR",    // 1
	"SERVFAIL",   // 2
	"NXDOMAIN",   // 3
	"NOTIMP",     // 4
	"REFUSED",    // 5
	"YXDOMAIN",   // 6
	"YXRRSET",    // 7
	"NXRRSET",    // 8
	"NOTAUTH",    // 9
	"NOTZONE",    // 10
	"11",         // 11 not assigned
	"12",         // 12 not assigned
	"13",         // 13 not assigned
	"14",         // 14 not assigned
	"15",         // 15 not assigned
};

#if ENABLE_FEATURE_IPV6
static const char v4_mapped[12] = { 0,0,0,0, 0,0,0,0, 0,0,0xff,0xff };
#endif

struct globals {
	unsigned default_port;
	unsigned default_retry;
	unsigned default_timeout;
	unsigned query_count;
	unsigned serv_count;
	struct ns *server;
	struct query *query;
} FIX_ALIASING;
#define G (*(struct globals*)bb_common_bufsiz1)
#define INIT_G() do { \
	setup_common_bufsiz(); \
	G.default_port = 53; \
	G.default_retry = 2; \
	G.default_timeout = 5; \
} while (0)

enum {
	OPT_debug = (1 << 0),
};

static int parse_reply(const unsigned char *msg, size_t len)
{
	HEADER *header;

	ns_msg handle;
	ns_rr rr;
	int i, n, rdlen;
	const char *format = NULL;
	char astr[INET6_ADDRSTRLEN], dname[MAXDNAME];
	const unsigned char *cp;

	header = (HEADER *)msg;
	if (!header->aa)
		printf("Non-authoritative answer:\n");

	if (ns_initparse(msg, len, &handle) != 0) {
		//printf("Unable to parse reply: %s\n", strerror(errno));
		return -1;
	}

	for (i = 0; i < ns_msg_count(handle, ns_s_an); i++) {
		if (ns_parserr(&handle, ns_s_an, i, &rr) != 0) {
			//printf("Unable to parse resource record: %s\n", strerror(errno));
			return -1;
		}

		rdlen = ns_rr_rdlen(rr);

		switch (ns_rr_type(rr))
		{
		case ns_t_a:
			if (rdlen != 4) {
				dbg("unexpected A record length %d\n", rdlen);
				return -1;
			}
			inet_ntop(AF_INET, ns_rr_rdata(rr), astr, sizeof(astr));
			printf("Name:\t%s\nAddress: %s\n", ns_rr_name(rr), astr);
			break;

#if ENABLE_FEATURE_IPV6
		case ns_t_aaaa:
			if (rdlen != 16) {
				dbg("unexpected AAAA record length %d\n", rdlen);
				return -1;
			}
			inet_ntop(AF_INET6, ns_rr_rdata(rr), astr, sizeof(astr));
			/* bind-utils-9.11.3 uses the same format for A and AAAA answers */
			printf("Name:\t%s\nAddress: %s\n", ns_rr_name(rr), astr);
			break;
#endif

		case ns_t_ns:
			if (!format)
				format = "%s\tnameserver = %s\n";
			/* fall through */

		case ns_t_cname:
			if (!format)
				format = "%s\tcanonical name = %s\n";
			/* fall through */

		case ns_t_ptr:
			if (!format)
				format = "%s\tname = %s\n";
			if (ns_name_uncompress(ns_msg_base(handle), ns_msg_end(handle),
					ns_rr_rdata(rr), dname, sizeof(dname)) < 0
			) {
				//printf("Unable to uncompress domain: %s\n", strerror(errno));
				return -1;
			}
			printf(format, ns_rr_name(rr), dname);
			break;

		case ns_t_mx:
			if (rdlen < 2) {
				printf("MX record too short\n");
				return -1;
			}
			n = ns_get16(ns_rr_rdata(rr));
			if (ns_name_uncompress(ns_msg_base(handle), ns_msg_end(handle),
					ns_rr_rdata(rr) + 2, dname, sizeof(dname)) < 0
			) {
				//printf("Cannot uncompress MX domain: %s\n", strerror(errno));
				return -1;
			}
			printf("%s\tmail exchanger = %d %s\n", ns_rr_name(rr), n, dname);
			break;

		case ns_t_txt:
			if (rdlen < 1) {
				//printf("TXT record too short\n");
				return -1;
			}
			n = *(unsigned char *)ns_rr_rdata(rr);
			if (n > 0) {
				memset(dname, 0, sizeof(dname));
				memcpy(dname, ns_rr_rdata(rr) + 1, n);
				printf("%s\ttext = \"%s\"\n", ns_rr_name(rr), dname);
			}
			break;

		case ns_t_soa:
			if (rdlen < 20) {
				dbg("SOA record too short:%d\n", rdlen);
				return -1;
			}

			printf("%s\n", ns_rr_name(rr));

			cp = ns_rr_rdata(rr);
			n = ns_name_uncompress(ns_msg_base(handle), ns_msg_end(handle),
			                       cp, dname, sizeof(dname));
			if (n < 0) {
				//printf("Unable to uncompress domain: %s\n", strerror(errno));
				return -1;
			}

			printf("\torigin = %s\n", dname);
			cp += n;

			n = ns_name_uncompress(ns_msg_base(handle), ns_msg_end(handle),
			                       cp, dname, sizeof(dname));
			if (n < 0) {
				//printf("Unable to uncompress domain: %s\n", strerror(errno));
				return -1;
			}

			printf("\tmail addr = %s\n", dname);
			cp += n;

			printf("\tserial = %lu\n", ns_get32(cp));
			cp += 4;

			printf("\trefresh = %lu\n", ns_get32(cp));
			cp += 4;

			printf("\tretry = %lu\n", ns_get32(cp));
			cp += 4;

			printf("\texpire = %lu\n", ns_get32(cp));
			cp += 4;

			printf("\tminimum = %lu\n", ns_get32(cp));
			break;

		default:
			break;
		}
	}

	return i;
}

/*
 * Function logic borrowed & modified from musl libc, res_msend.c
 * G.query_count is always > 0.
 */
static int send_queries(struct ns *ns)
{
	unsigned char reply[512];
	uint8_t rcode;
	len_and_sockaddr *local_lsa;
	struct pollfd pfd;
	int servfail_retry = 0;
	int n_replies = 0;
//	int save_idx = 0;
	unsigned retry_interval;
	unsigned timeout = G.default_timeout * 1000;
	unsigned tstart, tsent, tcur;

	pfd.events = POLLIN;
	pfd.fd = xsocket_type(&local_lsa, ns->lsa->u.sa.sa_family, SOCK_DGRAM);
	/*
	 * local_lsa has "null" address and port 0 now.
	 * bind() ensures we have a *particular port* selected by kernel
	 * and remembered in fd, thus later recv(fd)
	 * receives only packets sent to this port.
	 */
	xbind(pfd.fd, &local_lsa->u.sa, local_lsa->len);
	free(local_lsa);
	/* Make read/writes know the destination */
	xconnect(pfd.fd, &ns->lsa->u.sa, ns->lsa->len);
	ndelay_on(pfd.fd);

	retry_interval = timeout / G.default_retry;
	tstart = tcur = monotonic_ms();
	goto send;

	while (tcur - tstart < timeout) {
		int qn;
		int recvlen;

		if (tcur - tsent >= retry_interval) {
 send:
			for (qn = 0; qn < G.query_count; qn++) {
				if (G.query[qn].qlen == 0)
					continue; /* this one was replied already */

				if (write(pfd.fd, G.query[qn].query, G.query[qn].qlen) < 0) {
					bb_perror_msg("write to '%s'", ns->name);
					n_replies = -1; /* "no go, try next server" */
					goto ret;
				}
				dbg("query %u sent\n", qn);
			}
			tsent = tcur;
			servfail_retry = 2 * G.query_count;
		}

		/* Wait for a response, or until time to retry */
		if (poll(&pfd, 1, retry_interval - (tcur - tsent)) <= 0)
			goto next;

		recvlen = read(pfd.fd, reply, sizeof(reply));
		if (recvlen < 0) {
			bb_perror_msg("read");
 next:
			tcur = monotonic_ms();
			continue;
		}

		if (ns->replies++ == 0) {
			printf("Server:\t\t%s\n", ns->name);
			printf("Address:\t%s\n\n",
				auto_string(xmalloc_sockaddr2dotted(&ns->lsa->u.sa))
			);
			/* In "Address", bind-utils-9.11.3 show port after a hash: "1.2.3.4#53" */
			/* Should we do the same? */
		}

		/* Non-identifiable packet */
		if (recvlen < 4) {
			dbg("read is too short:%d\n", recvlen);
			goto next;
		}

		/* Find which query this answer goes with, if any */
//		qn = save_idx;
		qn = 0;
		for (;;) {
			if (memcmp(reply, G.query[qn].query, 2) == 0) {
				dbg("response matches query %u\n", qn);
				break;
			}
			if (++qn >= G.query_count) {
				dbg("response does not match any query\n");
				goto next;
			}
		}

		if (G.query[qn].qlen == 0) {
			dbg("dropped duplicate response to query %u\n", qn);
			goto next;
		}

		rcode = reply[3] & 0x0f;
		dbg("query %u rcode:%s\n", qn, rcodes[rcode]);

		/* Retry immediately on SERVFAIL */
		if (rcode == 2) {
			ns->failures++;
			if (servfail_retry) {
				servfail_retry--;
				write(pfd.fd, G.query[qn].query, G.query[qn].qlen);
				dbg("query %u resent\n", qn);
				goto next;
			}
		}

		/* Process reply */
		G.query[qn].qlen = 0; /* flag: "reply received" */
		tcur = monotonic_ms();
#if 1
		if (option_mask32 & OPT_debug) {
			printf("Query #%d completed in %ums:\n", qn, tcur - tstart);
		}
		if (rcode != 0) {
			printf("** server can't find %s: %s\n",
					G.query[qn].name, rcodes[rcode]);
		} else {
			if (parse_reply(reply, recvlen) < 0)
				printf("*** Can't find %s: Parse error\n", G.query[qn].name);
		}
		bb_putchar('\n');
		n_replies++;
		if (n_replies >= G.query_count)
			goto ret;
#else
//used to store replies and process them later
		G.query[qn].latency = tcur - tstart;
		n_replies++;
		if (qn != save_idx) {
			/* "wrong" receive buffer, move to correct one */
			memcpy(G.query[qn].reply, G.query[save_idx].reply, recvlen);
			continue;
		}
		/* G.query[0..save_idx] have replies, move to next one, if exists */
		for (;;) {
			save_idx++;
			if (save_idx >= G.query_count)
				goto ret; /* all are full: we have all results */
			if (!G.query[save_idx].rlen)
				break; /* this one is empty */
		}
#endif
	} /* while() */

 ret:
	close(pfd.fd);

	return n_replies;
}

static void add_ns(const char *addr)
{
	struct ns *ns;
	unsigned count;

	dbg("%s: addr:'%s'\n", __func__, addr);

	count = G.serv_count++;

	G.server = xrealloc_vector(G.server, /*8=2^3:*/ 3, count);
	ns = &G.server[count];
	ns->name = addr;
	ns->lsa = xhost2sockaddr(addr, G.default_port);
	/*ns->replies = 0; - already is */
	/*ns->failures = 0; - already is */
}

static void parse_resolvconf(void)
{
	FILE *resolv;

	resolv = fopen("/etc/resolv.conf", "r");
	if (resolv) {
		char line[128], *p;

		while (fgets(line, sizeof(line), resolv)) {
			p = strtok(line, " \t\n");

			if (!p || strcmp(p, "nameserver") != 0)
				continue;

			p = strtok(NULL, " \t\n");

			if (!p)
				continue;

			add_ns(xstrdup(p));
		}

		fclose(resolv);
	}
}

static void add_query(int type, const char *dname)
{
	struct query *new_q;
	unsigned count;
	ssize_t qlen;

	count = G.query_count++;

	G.query = xrealloc_vector(G.query, /*2=2^1:*/ 1, count);
	new_q = &G.query[count];

	dbg("new query#%u type %u for '%s'\n", count, type, dname);
	new_q->name = dname;

	qlen = res_mkquery(QUERY, dname, C_IN, type,
			/*data:*/ NULL, /*datalen:*/ 0,
			/*newrr:*/ NULL,
			new_q->query, sizeof(new_q->query)
	);
	new_q->qlen = qlen;
}

static char *make_ptr(const char *addrstr)
{
	unsigned char addr[16];

#if ENABLE_FEATURE_IPV6
	if (inet_pton(AF_INET6, addrstr, addr)) {
		if (memcmp(addr, v4_mapped, 12) != 0) {
			int i;
			char resbuf[80];
			char *ptr = resbuf;
			for (i = 0; i < 16; i++) {
				*ptr++ = 0x20 | bb_hexdigits_upcase[(unsigned char)addr[15 - i] & 0xf];
				*ptr++ = '.';
				*ptr++ = 0x20 | bb_hexdigits_upcase[(unsigned char)addr[15 - i] >> 4];
				*ptr++ = '.';
			}
			strcpy(ptr, "ip6.arpa");
			return xstrdup(resbuf);
		}
		return xasprintf("%u.%u.%u.%u.in-addr.arpa",
				addr[15], addr[14], addr[13], addr[12]);
	}
#endif

	if (inet_pton(AF_INET, addrstr, addr)) {
		return xasprintf("%u.%u.%u.%u.in-addr.arpa",
		        addr[3], addr[2], addr[1], addr[0]);
	}

	return NULL;
}

int nslookup_main(int argc, char **argv) MAIN_EXTERNALLY_VISIBLE;
int nslookup_main(int argc UNUSED_PARAM, char **argv)
{
	unsigned types;
	int rc;
	int err;

	INIT_G();

	/* manpage: "Options can also be specified on the command line
	 * if they precede the arguments and are prefixed with a hyphen."
	 */
	types = 0;
	argv++;
	for (;;) {
		const char *options =
// bind-utils-9.11.3 accept these:
// class=   cl=
// type=    ty= querytype= query= qu= q=
// domain=  do=
// port=    po=
// timeout= t=
// retry=   ret=
// ndots=
// recurse
// norecurse
// defname
// nodefname
// vc
// novc
// debug
// nodebug
// d2
// nod2
// search
// nosearch
// sil
// fail
// nofail
// ver (prints version and exits)
			"type\0"      /* 0 */
			"querytype\0" /* 1 */
			"port\0"      /* 2 */
			"retry\0"     /* 3 */
			"debug\0"     /* 4 */
			"t\0" /* disambiguate with "type": else -t=2 fails */
			"timeout\0"   /* 6 */
			"";
		int i;
		char *arg;
		char *val;

		if (!*argv)
			bb_show_usage();
		if (argv[0][0] != '-')
			break;

		/* Separate out "=val" part */
		arg = (*argv++) + 1;
		val = strchrnul(arg, '=');
		if (*val)
			*val++ = '\0';

		i = index_in_substrings(options, arg);
		//bb_error_msg("i:%d arg:'%s' val:'%s'", i, arg, val);
		if (i < 0)
			bb_show_usage();

		if (i <= 1) {
			for (i = 0;; i++) {
				if (i == ARRAY_SIZE(qtypes))
					bb_error_msg_and_die("invalid query type \"%s\"", val);
				if (strcasecmp(qtypes[i].name, val) == 0)
					break;
			}
			types |= (1 << i);
			continue;
		}
		if (i == 2) {
			G.default_port = xatou_range(val, 1, 0xffff);
		}
		if (i == 3) {
			G.default_retry = xatou_range(val, 1, INT_MAX);
		}
		if (i == 4) {
			option_mask32 |= OPT_debug;
		}
		if (i > 4) {
			G.default_timeout = xatou_range(val, 1, INT_MAX / 1000);
		}
	}

	if (types == 0) {
		/* No explicit type given, guess query type.
		 * If we can convert the domain argument into a ptr (means that
		 * inet_pton() could read it) we assume a PTR request, else
		 * we issue A+AAAA queries and switch to an output format
		 * mimicking the one of the traditional nslookup applet.
		 */
		char *ptr;

		ptr = make_ptr(argv[0]);
		if (ptr) {
			add_query(T_PTR, ptr);
		} else {
			add_query(T_A, argv[0]);
#if ENABLE_FEATURE_IPV6
			add_query(T_AAAA, argv[0]);
#endif
		}
	} else {
		int c;
		for (c = 0; c < ARRAY_SIZE(qtypes); c++) {
			if (types & (1 << c))
				add_query(qtypes[c].type, argv[0]);
		}
	}

	/* Use given DNS server if present */
	if (argv[1]) {
		if (argv[2])
			bb_show_usage();
		add_ns(argv[1]);
	} else {
		parse_resolvconf();
		/* Fall back to localhost if we could not find NS in resolv.conf */
		if (G.serv_count == 0)
			add_ns("127.0.0.1");
	}

	for (rc = 0; rc < G.serv_count;) {
		int c;

		c = send_queries(&G.server[rc]);
		if (c > 0) {
			/* more than zero replies received */
#if 0 /* which version does this? */
			if (option_mask32 & OPT_debug) {
				printf("Replies:\t%d\n", G.server[rc].replies);
				printf("Failures:\t%d\n\n", G.server[rc].failures);
			}
#endif
			break;
//FIXME: we "break" even though some queries may still be not answered, and other servers may know them?
		}
		/* c = 0: timed out waiting for replies */
		/* c < 0: error (message already printed) */
		rc++;
		if (rc >= G.serv_count) {
//
// NB: bind-utils-9.11.3 behavior (all to stdout, not stderr):
//
// $ nslookup gmail.com 8.8.8.8
// ;; connection timed out; no servers could be reached
//
// Using TCP mode:
// $ nslookup -vc gmail.com 8.8.8.8; echo EXITCODE:$?
//     <~10 sec>
// ;; Connection to 8.8.8.8#53(8.8.8.8) for gmail.com failed: timed out.
//     <~10 sec>
// ;; Connection to 8.8.8.8#53(8.8.8.8) for gmail.com failed: timed out.
//     <~10 sec>
// ;; connection timed out; no servers could be reached
// ;; Connection to 8.8.8.8#53(8.8.8.8) for gmail.com failed: timed out.
//     <empty line>
// EXITCODE:1
// $ _
			printf(";; connection timed out; no servers could be reached\n\n");
			return EXIT_FAILURE;
		}
	}

	err = 0;
	for (rc = 0; rc < G.query_count; rc++) {
		if (G.query[rc].qlen) {
			printf("*** Can't find %s: No answer\n", G.query[rc].name);
			err = 1;
		}
	}
	if (err) /* should this affect exicode too? */
		bb_putchar('\n');

	if (ENABLE_FEATURE_CLEAN_UP) {
		free(G.server);
		free(G.query);
	}

	return EXIT_SUCCESS;
}

#endif
