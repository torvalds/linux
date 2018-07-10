/* vi: set sw=4 ts=4: */
/*
 * Mini DNS server implementation for busybox
 *
 * Copyright (C) 2005 Roberto A. Foglietta (me@roberto.foglietta.name)
 * Copyright (C) 2005 Odd Arild Olsen (oao at fibula dot no)
 * Copyright (C) 2003 Paul Sheer
 *
 * Licensed under GPLv2 or later, see file LICENSE in this source tree.
 *
 * Odd Arild Olsen started out with the sheerdns [1] of Paul Sheer and rewrote
 * it into a shape which I believe is both easier to understand and maintain.
 * I also reused the input buffer for output and removed services he did not
 * need.  [1] http://threading.2038bug.com/sheerdns/
 *
 * Some bugfix and minor changes was applied by Roberto A. Foglietta who made
 * the first porting of oao' scdns to busybox also.
 */
//config:config DNSD
//config:	bool "dnsd (9.8 kb)"
//config:	default y
//config:	help
//config:	Small and static DNS server daemon.

//applet:IF_DNSD(APPLET(dnsd, BB_DIR_USR_SBIN, BB_SUID_DROP))

//kbuild:lib-$(CONFIG_DNSD) += dnsd.o

//usage:#define dnsd_trivial_usage
//usage:       "[-dvs] [-c CONFFILE] [-t TTL_SEC] [-p PORT] [-i ADDR]"
//usage:#define dnsd_full_usage "\n\n"
//usage:       "Small static DNS server daemon\n"
//usage:     "\n	-c FILE	Config file"
//usage:     "\n	-t SEC	TTL"
//usage:     "\n	-p PORT	Listen on PORT"
//usage:     "\n	-i ADDR	Listen on ADDR"
//usage:     "\n	-d	Daemonize"
//usage:     "\n	-v	Verbose"
//usage:     "\n	-s	Send successful replies only. Use this if you want"
//usage:     "\n		to use /etc/resolv.conf with two nameserver lines:"
//usage:     "\n			nameserver DNSD_SERVER"
//usage:     "\n			nameserver NORMAL_DNS_SERVER"

#include "libbb.h"
#include <syslog.h>

//#define DEBUG 1
#define DEBUG 0

enum {
	/* can tweak this */
	DEFAULT_TTL = 120,

	/* cannot get bigger packets than 512 per RFC1035. */
	MAX_PACK_LEN = 512,
	IP_STRING_LEN = sizeof(".xxx.xxx.xxx.xxx"),
	MAX_NAME_LEN = IP_STRING_LEN - 1 + sizeof(".in-addr.arpa"),
	REQ_A = 1,
	REQ_PTR = 12,
};

/* the message from client and first part of response msg */
struct dns_head {
	uint16_t id;
	uint16_t flags;
	uint16_t nquer;
	uint16_t nansw;
	uint16_t nauth;
	uint16_t nadd;
};
/* Structure used to access type and class fields.
 * They are totally unaligned, but gcc 4.3.4 thinks that pointer of type uint16_t*
 * is 16-bit aligned and replaces 16-bit memcpy (in move_from_unaligned16 macro)
 * with aligned halfword access on arm920t!
 * Oh well. Slapping PACKED everywhere seems to help: */
struct type_and_class {
	uint16_t type PACKED;
	uint16_t class PACKED;
} PACKED;
/* element of known name, ip address and reversed ip address */
struct dns_entry {
	struct dns_entry *next;
	uint32_t ip;
	char rip[IP_STRING_LEN]; /* length decimal reversed IP */
	char name[1];
};

#define OPT_verbose (option_mask32 & 1)
#define OPT_silent  (option_mask32 & 2)


/*
 * Insert length of substrings instead of dots
 */
static void undot(char *rip)
{
	int i = 0;
	int s = 0;

	while (rip[i])
		i++;
	for (--i; i >= 0; i--) {
		if (rip[i] == '.') {
			rip[i] = s;
			s = 0;
		} else {
			s++;
		}
	}
}

/*
 * Read hostname/IP records from file
 */
static struct dns_entry *parse_conf_file(const char *fileconf)
{
	char *token[2];
	parser_t *parser;
	struct dns_entry *m, *conf_data;
	struct dns_entry **nextp;

	conf_data = NULL;
	nextp = &conf_data;

	parser = config_open(fileconf);
	while (config_read(parser, token, 2, 2, "# \t", PARSE_NORMAL)) {
		struct in_addr ip;
		uint32_t v32;

		if (inet_aton(token[1], &ip) == 0) {
			bb_error_msg("error at line %u, skipping", parser->lineno);
			continue;
		}

		if (OPT_verbose)
			bb_error_msg("name:%s, ip:%s", token[0], token[1]);

		/* sizeof(*m) includes 1 byte for m->name[0] */
		m = xzalloc(sizeof(*m) + strlen(token[0]) + 1);
		/*m->next = NULL;*/
		*nextp = m;
		nextp = &m->next;

		m->name[0] = '.';
		strcpy(m->name + 1, token[0]);
		undot(m->name);
		m->ip = ip.s_addr; /* in network order */
		v32 = ntohl(m->ip);
		/* inverted order */
		sprintf(m->rip, ".%u.%u.%u.%u",
			(uint8_t)(v32),
			(uint8_t)(v32 >> 8),
			(uint8_t)(v32 >> 16),
			(v32 >> 24)
		);
		undot(m->rip);
	}
	config_close(parser);
	return conf_data;
}

/*
 * Look query up in dns records and return answer if found.
 */
static char *table_lookup(struct dns_entry *d,
		uint16_t type,
		char* query_string)
{
	while (d) {
		unsigned len = d->name[0];
		/* d->name[len] is the last (non NUL) char */
#if DEBUG
		char *p, *q;
		q = query_string + 1;
		p = d->name + 1;
		fprintf(stderr, "%d/%d p:%s q:%s %d\n",
			(int)strlen(p), len,
			p, q, (int)strlen(q)
		);
#endif
		if (type == htons(REQ_A)) {
			/* search by host name */
			if (len != 1 || d->name[1] != '*') {
/* we are lax, hope no name component is ever >64 so that length
 * (which will be represented as 'A','B'...) matches a lowercase letter.
 * Actually, I think false matches are hard to construct.
 * Example.
 * [31] len is represented as '1', [65] as 'A', [65+32] as 'a'.
 * [65]   <65 same chars>[31]<31 same chars>NUL
 * [65+32]<65 same chars>1   <31 same chars>NUL
 * This example seems to be the minimal case when false match occurs.
 */
				if (strcasecmp(d->name, query_string) != 0)
					goto next;
			}
			return (char *)&d->ip;
#if DEBUG
			fprintf(stderr, "Found IP:%x\n", (int)d->ip);
#endif
			return 0;
		}
		/* search by IP-address */
		if ((len != 1 || d->name[1] != '*')
		/* we assume (do not check) that query_string
		 * ends in ".in-addr.arpa" */
		 && is_prefixed_with(query_string, d->rip)
		) {
#if DEBUG
			fprintf(stderr, "Found name:%s\n", d->name);
#endif
			return d->name;
		}
 next:
		d = d->next;
	}

	return NULL;
}

/*
 * Decode message and generate answer
 */
/* RFC 1035
...
Whenever an octet represents a numeric quantity, the left most bit
in the diagram is the high order or most significant bit.
That is, the bit labeled 0 is the most significant bit.
...

4.1.1. Header section format
      0  1  2  3  4  5  6  7  8  9 10 11 12 13 14 15
    +--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+
    |                      ID                       |
    +--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+
    |QR|   OPCODE  |AA|TC|RD|RA| 0  0  0|   RCODE   |
    +--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+
    |                    QDCOUNT                    |
    +--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+
    |                    ANCOUNT                    |
    +--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+
    |                    NSCOUNT                    |
    +--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+
    |                    ARCOUNT                    |
    +--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+
ID      16 bit random identifier assigned by querying peer.
        Used to match query/response.
QR      message is a query (0), or a response (1).
OPCODE  0   standard query (QUERY)
        1   inverse query (IQUERY)
        2   server status request (STATUS)
AA      Authoritative Answer - this bit is valid in responses.
        Responding name server is an authority for the domain name
        in question section. Answer section may have multiple owner names
        because of aliases.  The AA bit corresponds to the name which matches
        the query name, or the first owner name in the answer section.
TC      TrunCation - this message was truncated.
RD      Recursion Desired - this bit may be set in a query and
        is copied into the response.  If RD is set, it directs
        the name server to pursue the query recursively.
        Recursive query support is optional.
RA      Recursion Available - this be is set or cleared in a
        response, and denotes whether recursive query support is
        available in the name server.
RCODE   Response code.
        0   No error condition
        1   Format error
        2   Server failure - server was unable to process the query
            due to a problem with the name server.
        3   Name Error - meaningful only for responses from
            an authoritative name server. The referenced domain name
            does not exist.
        4   Not Implemented.
        5   Refused.
QDCOUNT number of entries in the question section.
ANCOUNT number of records in the answer section.
NSCOUNT number of records in the authority records section.
ARCOUNT number of records in the additional records section.

4.1.2. Question section format

The section contains QDCOUNT (usually 1) entries, each of this format:
      0  1  2  3  4  5  6  7  8  9 10 11 12 13 14 15
    +--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+
    /                     QNAME                     /
    /                                               /
    +--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+
    |                     QTYPE                     |
    +--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+
    |                     QCLASS                    |
    +--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+
QNAME   a domain name represented as a sequence of labels, where
        each label consists of a length octet followed by that
        number of octets. The domain name terminates with the
        zero length octet for the null label of the root. Note
        that this field may be an odd number of octets; no
        padding is used.
QTYPE   a two octet type of the query.
          1 a host address [REQ_A const]
          2 an authoritative name server
          3 a mail destination (Obsolete - use MX)
          4 a mail forwarder (Obsolete - use MX)
          5 the canonical name for an alias
          6 marks the start of a zone of authority
          7 a mailbox domain name (EXPERIMENTAL)
          8 a mail group member (EXPERIMENTAL)
          9 a mail rename domain name (EXPERIMENTAL)
         10 a null RR (EXPERIMENTAL)
         11 a well known service description
         12 a domain name pointer [REQ_PTR const]
         13 host information
         14 mailbox or mail list information
         15 mail exchange
         16 text strings
       0x1c IPv6?
        252 a request for a transfer of an entire zone
        253 a request for mailbox-related records (MB, MG or MR)
        254 a request for mail agent RRs (Obsolete - see MX)
        255 a request for all records
QCLASS  a two octet code that specifies the class of the query.
          1 the Internet
        (others are historic only)
        255 any class

4.1.3. Resource Record format

The answer, authority, and additional sections all share the same format:
a variable number of resource records, where the number of records
is specified in the corresponding count field in the header.
Each resource record has this format:
      0  1  2  3  4  5  6  7  8  9 10 11 12 13 14 15
    +--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+
    /                                               /
    /                      NAME                     /
    +--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+
    |                      TYPE                     |
    +--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+
    |                     CLASS                     |
    +--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+
    |                      TTL                      |
    |                                               |
    +--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+
    |                   RDLENGTH                    |
    +--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--|
    /                     RDATA                     /
    /                                               /
    +--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+
NAME    a domain name to which this resource record pertains.
TYPE    two octets containing one of the RR type codes.  This
        field specifies the meaning of the data in the RDATA field.
CLASS   two octets which specify the class of the data in the RDATA field.
TTL     a 32 bit unsigned integer that specifies the time interval
        (in seconds) that the record may be cached.
RDLENGTH a 16 bit integer, length in octets of the RDATA field.
RDATA   a variable length string of octets that describes the resource.
        The format of this information varies according to the TYPE
        and CLASS of the resource record.
        If the TYPE is A and the CLASS is IN, it's a 4 octet IP address.

4.1.4. Message compression

In order to reduce the size of messages, domain names coan be compressed.
An entire domain name or a list of labels at the end of a domain name
is replaced with a pointer to a prior occurrence of the same name.

The pointer takes the form of a two octet sequence:
    +--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+
    | 1  1|                OFFSET                   |
    +--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+
The first two bits are ones.  This allows a pointer to be distinguished
from a label, since the label must begin with two zero bits because
labels are restricted to 63 octets or less.  The OFFSET field specifies
an offset from the start of the message (i.e., the first octet
of the ID field in the domain header).
A zero offset specifies the first byte of the ID field, etc.
Domain name in a message can be represented as either:
   - a sequence of labels ending in a zero octet
   - a pointer
   - a sequence of labels ending with a pointer
 */
static int process_packet(struct dns_entry *conf_data,
		uint32_t conf_ttl,
		uint8_t *buf)
{
	struct dns_head *head;
	struct type_and_class *unaligned_type_class;
	const char *err_msg;
	char *query_string;
	char *answstr;
	uint8_t *answb;
	uint16_t outr_rlen;
	uint16_t outr_flags;
	uint16_t type;
	uint16_t class;
	int query_len;

	head = (struct dns_head *)buf;
	if (head->nquer == 0) {
		bb_error_msg("packet has 0 queries, ignored");
		return 0; /* don't reply */
	}
	if (head->flags & htons(0x8000)) { /* QR bit */
		bb_error_msg("response packet, ignored");
		return 0; /* don't reply */
	}
	/* QR = 1 "response", RCODE = 4 "Not Implemented" */
	outr_flags = htons(0x8000 | 4);
	err_msg = NULL;

	/* start of query string */
	query_string = (void *)(head + 1);
	/* caller guarantees strlen is <= MAX_PACK_LEN */
	query_len = strlen(query_string) + 1;
	/* may be unaligned! */
	unaligned_type_class = (void *)(query_string + query_len);
	query_len += sizeof(*unaligned_type_class);
	/* where to append answer block */
	answb = (void *)(unaligned_type_class + 1);

	/* OPCODE != 0 "standard query"? */
	if ((head->flags & htons(0x7800)) != 0) {
		err_msg = "opcode != 0";
		goto empty_packet;
	}
	move_from_unaligned16(class, &unaligned_type_class->class);
	if (class != htons(1)) { /* not class INET? */
		err_msg = "class != 1";
		goto empty_packet;
	}
	move_from_unaligned16(type, &unaligned_type_class->type);
	if (type != htons(REQ_A) && type != htons(REQ_PTR)) {
		/* we can't handle this query type */
//TODO: happens all the time with REQ_AAAA (0x1c) requests - implement those?
		err_msg = "type is !REQ_A and !REQ_PTR";
		goto empty_packet;
	}

	/* look up the name */
	answstr = table_lookup(conf_data, type, query_string);
#if DEBUG
	/* Shows lengths instead of dots, unusable for !DEBUG */
	bb_error_msg("'%s'->'%s'", query_string, answstr);
#endif
	outr_rlen = 4;
	if (answstr && type == htons(REQ_PTR)) {
		/* returning a host name */
		outr_rlen = strlen(answstr) + 1;
	}
	if (!answstr
	 || (unsigned)(answb - buf) + query_len + 4 + 2 + outr_rlen > MAX_PACK_LEN
	) {
		/* QR = 1 "response"
		 * AA = 1 "Authoritative Answer"
		 * RCODE = 3 "Name Error" */
		err_msg = "name is not found";
		outr_flags = htons(0x8000 | 0x0400 | 3);
		goto empty_packet;
	}

	/* Append answer Resource Record */
	memcpy(answb, query_string, query_len); /* name, type, class */
	answb += query_len;
	move_to_unaligned32((uint32_t *)answb, htonl(conf_ttl));
	answb += 4;
	move_to_unaligned16((uint16_t *)answb, htons(outr_rlen));
	answb += 2;
	memcpy(answb, answstr, outr_rlen);
	answb += outr_rlen;

	/* QR = 1 "response",
	 * AA = 1 "Authoritative Answer",
	 * TODO: need to set RA bit 0x80? One user says nslookup complains
	 * "Got recursion not available from SERVER, trying next server"
	 * "** server can't find HOSTNAME"
	 * RCODE = 0 "success"
	 */
	if (OPT_verbose)
		bb_error_msg("returning positive reply");
	outr_flags = htons(0x8000 | 0x0400 | 0);
	/* we have one answer */
	head->nansw = htons(1);

 empty_packet:
	if ((outr_flags & htons(0xf)) != 0) { /* not a positive response */
		if (OPT_verbose) {
			bb_error_msg("%s, %s",
				err_msg,
				OPT_silent ? "dropping query" : "sending error reply"
			);
		}
		if (OPT_silent)
			return 0;
	}
	head->flags |= outr_flags;
	head->nauth = head->nadd = 0;
	head->nquer = htons(1); // why???

	return answb - buf;
}

int dnsd_main(int argc, char **argv) MAIN_EXTERNALLY_VISIBLE;
int dnsd_main(int argc UNUSED_PARAM, char **argv)
{
	const char *listen_interface = "0.0.0.0";
	const char *fileconf = "/etc/dnsd.conf";
	struct dns_entry *conf_data;
	uint32_t conf_ttl = DEFAULT_TTL;
	char *sttl, *sport;
	len_and_sockaddr *lsa, *from, *to;
	unsigned lsa_size;
	int udps, opts;
	uint16_t port = 53;
	/* Ensure buf is 32bit aligned (we need 16bit, but 32bit can't hurt) */
	uint8_t buf[MAX_PACK_LEN + 1] ALIGN4;

	opts = getopt32(argv, "vsi:c:t:p:d", &listen_interface, &fileconf, &sttl, &sport);
	//if (opts & (1 << 0)) // -v
	//if (opts & (1 << 1)) // -s
	//if (opts & (1 << 2)) // -i
	//if (opts & (1 << 3)) // -c
	if (opts & (1 << 4)) // -t
		conf_ttl = xatou_range(sttl, 1, 0xffffffff);
	if (opts & (1 << 5)) // -p
		port = xatou_range(sport, 1, 0xffff);
	if (opts & (1 << 6)) { // -d
		bb_daemonize_or_rexec(DAEMON_CLOSE_EXTRA_FDS, argv);
		openlog(applet_name, LOG_PID, LOG_DAEMON);
		logmode = LOGMODE_SYSLOG;
	}

	conf_data = parse_conf_file(fileconf);

	lsa = xdotted2sockaddr(listen_interface, port);
	udps = xsocket(lsa->u.sa.sa_family, SOCK_DGRAM, 0);
	xbind(udps, &lsa->u.sa, lsa->len);
	socket_want_pktinfo(udps); /* needed for recv_from_to to work */
	lsa_size = LSA_LEN_SIZE + lsa->len;
	from = xzalloc(lsa_size);
	to = xzalloc(lsa_size);

	{
		char *p = xmalloc_sockaddr2dotted(&lsa->u.sa);
		bb_error_msg("accepting UDP packets on %s", p);
		free(p);
	}

	while (1) {
		int r;
		/* Try to get *DEST* address (to which of our addresses
		 * this query was directed), and reply from the same address.
		 * Or else we can exhibit usual UDP ugliness:
		 * [ip1.multihomed.ip2] <=  query to ip1  <= peer
		 * [ip1.multihomed.ip2] => reply from ip2 => peer (confused) */
		memcpy(to, lsa, lsa_size);
		r = recv_from_to(udps, buf, MAX_PACK_LEN + 1, 0, &from->u.sa, &to->u.sa, lsa->len);
		if (r < 12 || r > MAX_PACK_LEN) {
			bb_error_msg("packet size %d, ignored", r);
			continue;
		}
		if (OPT_verbose)
			bb_error_msg("got UDP packet");
		buf[r] = '\0'; /* paranoia */
		r = process_packet(conf_data, conf_ttl, buf);
		if (r <= 0)
			continue;
		send_to_from(udps, buf, r, 0, &from->u.sa, &to->u.sa, lsa->len);
	}
	return 0;
}
