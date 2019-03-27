/*#define CHASE_CHAIN*/
/*
 * Copyright (c) 1990, 1991, 1992, 1993, 1994, 1995, 1996, 1997, 1998
 *	The Regents of the University of California.  All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that: (1) source code distributions
 * retain the above copyright notice and this paragraph in its entirety, (2)
 * distributions including binary code include the above copyright notice and
 * this paragraph in its entirety in the documentation or other materials
 * provided with the distribution, and (3) all advertising materials mentioning
 * features or use of this software display the following acknowledgement:
 * ``This product includes software developed by the University of California,
 * Lawrence Berkeley Laboratory and its contributors.'' Neither the name of
 * the University nor the names of its contributors may be used to endorse
 * or promote products derived from this software without specific prior
 * written permission.
 * THIS SOFTWARE IS PROVIDED ``AS IS'' AND WITHOUT ANY EXPRESS OR IMPLIED
 * WARRANTIES, INCLUDING, WITHOUT LIMITATION, THE IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE.
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <pcap-types.h>
#ifdef _WIN32
  #include <ws2tcpip.h>
#else
  #include <sys/socket.h>

  #ifdef __NetBSD__
    #include <sys/param.h>
  #endif

  #include <netinet/in.h>
  #include <arpa/inet.h>
#endif /* _WIN32 */

#include <stdlib.h>
#include <string.h>
#include <memory.h>
#include <setjmp.h>
#include <stdarg.h>

#ifdef MSDOS
#include "pcap-dos.h"
#endif

#include "pcap-int.h"

#include "ethertype.h"
#include "nlpid.h"
#include "llc.h"
#include "gencode.h"
#include "ieee80211.h"
#include "atmuni31.h"
#include "sunatmpos.h"
#include "ppp.h"
#include "pcap/sll.h"
#include "pcap/ipnet.h"
#include "arcnet.h"

#include "grammar.h"
#include "scanner.h"

#if defined(linux) && defined(PF_PACKET) && defined(SO_ATTACH_FILTER)
#include <linux/types.h>
#include <linux/if_packet.h>
#include <linux/filter.h>
#endif

#ifdef HAVE_NET_PFVAR_H
#include <sys/socket.h>
#include <net/if.h>
#include <net/pfvar.h>
#include <net/if_pflog.h>
#endif

#ifndef offsetof
#define offsetof(s, e) ((size_t)&((s *)0)->e)
#endif

#ifdef _WIN32
  #ifdef INET6
    #if defined(__MINGW32__) && defined(DEFINE_ADDITIONAL_IPV6_STUFF)
/* IPv6 address */
struct in6_addr
  {
    union
      {
	uint8_t		u6_addr8[16];
	uint16_t	u6_addr16[8];
	uint32_t	u6_addr32[4];
      } in6_u;
#define s6_addr			in6_u.u6_addr8
#define s6_addr16		in6_u.u6_addr16
#define s6_addr32		in6_u.u6_addr32
#define s6_addr64		in6_u.u6_addr64
  };

typedef unsigned short	sa_family_t;

#define	__SOCKADDR_COMMON(sa_prefix) \
  sa_family_t sa_prefix##family

/* Ditto, for IPv6.  */
struct sockaddr_in6
  {
    __SOCKADDR_COMMON (sin6_);
    uint16_t sin6_port;		/* Transport layer port # */
    uint32_t sin6_flowinfo;	/* IPv6 flow information */
    struct in6_addr sin6_addr;	/* IPv6 address */
  };

      #ifndef EAI_ADDRFAMILY
struct addrinfo {
	int	ai_flags;	/* AI_PASSIVE, AI_CANONNAME */
	int	ai_family;	/* PF_xxx */
	int	ai_socktype;	/* SOCK_xxx */
	int	ai_protocol;	/* 0 or IPPROTO_xxx for IPv4 and IPv6 */
	size_t	ai_addrlen;	/* length of ai_addr */
	char	*ai_canonname;	/* canonical name for hostname */
	struct sockaddr *ai_addr;	/* binary address */
	struct addrinfo *ai_next;	/* next structure in linked list */
};
      #endif /* EAI_ADDRFAMILY */
    #endif /* defined(__MINGW32__) && defined(DEFINE_ADDITIONAL_IPV6_STUFF) */
  #endif /* INET6 */
#else /* _WIN32 */
  #include <netdb.h>	/* for "struct addrinfo" */
#endif /* _WIN32 */
#include <pcap/namedb.h>

#include "nametoaddr.h"

#define ETHERMTU	1500

#ifndef ETHERTYPE_TEB
#define ETHERTYPE_TEB 0x6558
#endif

#ifndef IPPROTO_HOPOPTS
#define IPPROTO_HOPOPTS 0
#endif
#ifndef IPPROTO_ROUTING
#define IPPROTO_ROUTING 43
#endif
#ifndef IPPROTO_FRAGMENT
#define IPPROTO_FRAGMENT 44
#endif
#ifndef IPPROTO_DSTOPTS
#define IPPROTO_DSTOPTS 60
#endif
#ifndef IPPROTO_SCTP
#define IPPROTO_SCTP 132
#endif

#define GENEVE_PORT 6081

#ifdef HAVE_OS_PROTO_H
#include "os-proto.h"
#endif

#define JMP(c) ((c)|BPF_JMP|BPF_K)

/*
 * "Push" the current value of the link-layer header type and link-layer
 * header offset onto a "stack", and set a new value.  (It's not a
 * full-blown stack; we keep only the top two items.)
 */
#define PUSH_LINKHDR(cs, new_linktype, new_is_variable, new_constant_part, new_reg) \
{ \
	(cs)->prevlinktype = (cs)->linktype; \
	(cs)->off_prevlinkhdr = (cs)->off_linkhdr; \
	(cs)->linktype = (new_linktype); \
	(cs)->off_linkhdr.is_variable = (new_is_variable); \
	(cs)->off_linkhdr.constant_part = (new_constant_part); \
	(cs)->off_linkhdr.reg = (new_reg); \
	(cs)->is_geneve = 0; \
}

/*
 * Offset "not set" value.
 */
#define OFFSET_NOT_SET	0xffffffffU

/*
 * Absolute offsets, which are offsets from the beginning of the raw
 * packet data, are, in the general case, the sum of a variable value
 * and a constant value; the variable value may be absent, in which
 * case the offset is only the constant value, and the constant value
 * may be zero, in which case the offset is only the variable value.
 *
 * bpf_abs_offset is a structure containing all that information:
 *
 *   is_variable is 1 if there's a variable part.
 *
 *   constant_part is the constant part of the value, possibly zero;
 *
 *   if is_variable is 1, reg is the register number for a register
 *   containing the variable value if the register has been assigned,
 *   and -1 otherwise.
 */
typedef struct {
	int	is_variable;
	u_int	constant_part;
	int	reg;
} bpf_abs_offset;

/*
 * Value passed to gen_load_a() to indicate what the offset argument
 * is relative to the beginning of.
 */
enum e_offrel {
	OR_PACKET,		/* full packet data */
	OR_LINKHDR,		/* link-layer header */
	OR_PREVLINKHDR,		/* previous link-layer header */
	OR_LLC,			/* 802.2 LLC header */
	OR_PREVMPLSHDR,		/* previous MPLS header */
	OR_LINKTYPE,		/* link-layer type */
	OR_LINKPL,		/* link-layer payload */
	OR_LINKPL_NOSNAP,	/* link-layer payload, with no SNAP header at the link layer */
	OR_TRAN_IPV4,		/* transport-layer header, with IPv4 network layer */
	OR_TRAN_IPV6		/* transport-layer header, with IPv6 network layer */
};

/*
 * We divy out chunks of memory rather than call malloc each time so
 * we don't have to worry about leaking memory.  It's probably
 * not a big deal if all this memory was wasted but if this ever
 * goes into a library that would probably not be a good idea.
 *
 * XXX - this *is* in a library....
 */
#define NCHUNKS 16
#define CHUNK0SIZE 1024
struct chunk {
	size_t n_left;
	void *m;
};

/* Code generator state */

struct _compiler_state {
	jmp_buf top_ctx;
	pcap_t *bpf_pcap;

	struct icode ic;

	int snaplen;

	int linktype;
	int prevlinktype;
	int outermostlinktype;

	bpf_u_int32 netmask;
	int no_optimize;

	/* Hack for handling VLAN and MPLS stacks. */
	u_int label_stack_depth;
	u_int vlan_stack_depth;

	/* XXX */
	u_int pcap_fddipad;

	/*
	 * As errors are handled by a longjmp, anything allocated must
	 * be freed in the longjmp handler, so it must be reachable
	 * from that handler.
	 *
	 * One thing that's allocated is the result of pcap_nametoaddrinfo();
	 * it must be freed with freeaddrinfo().  This variable points to
	 * any addrinfo structure that would need to be freed.
	 */
	struct addrinfo *ai;

	/*
	 * Various code constructs need to know the layout of the packet.
	 * These values give the necessary offsets from the beginning
	 * of the packet data.
	 */

	/*
	 * Absolute offset of the beginning of the link-layer header.
	 */
	bpf_abs_offset off_linkhdr;

	/*
	 * If we're checking a link-layer header for a packet encapsulated
	 * in another protocol layer, this is the equivalent information
	 * for the previous layers' link-layer header from the beginning
	 * of the raw packet data.
	 */
	bpf_abs_offset off_prevlinkhdr;

	/*
	 * This is the equivalent information for the outermost layers'
	 * link-layer header.
	 */
	bpf_abs_offset off_outermostlinkhdr;

	/*
	 * Absolute offset of the beginning of the link-layer payload.
	 */
	bpf_abs_offset off_linkpl;

	/*
	 * "off_linktype" is the offset to information in the link-layer
	 * header giving the packet type. This is an absolute offset
	 * from the beginning of the packet.
	 *
	 * For Ethernet, it's the offset of the Ethernet type field; this
	 * means that it must have a value that skips VLAN tags.
	 *
	 * For link-layer types that always use 802.2 headers, it's the
	 * offset of the LLC header; this means that it must have a value
	 * that skips VLAN tags.
	 *
	 * For PPP, it's the offset of the PPP type field.
	 *
	 * For Cisco HDLC, it's the offset of the CHDLC type field.
	 *
	 * For BSD loopback, it's the offset of the AF_ value.
	 *
	 * For Linux cooked sockets, it's the offset of the type field.
	 *
	 * off_linktype.constant_part is set to OFFSET_NOT_SET for no
	 * encapsulation, in which case, IP is assumed.
	 */
	bpf_abs_offset off_linktype;

	/*
	 * TRUE if the link layer includes an ATM pseudo-header.
	 */
	int is_atm;

	/*
	 * TRUE if "geneve" appeared in the filter; it causes us to
	 * generate code that checks for a Geneve header and assume
	 * that later filters apply to the encapsulated payload.
	 */
	int is_geneve;

	/*
	 * TRUE if we need variable length part of VLAN offset
	 */
	int is_vlan_vloffset;

	/*
	 * These are offsets for the ATM pseudo-header.
	 */
	u_int off_vpi;
	u_int off_vci;
	u_int off_proto;

	/*
	 * These are offsets for the MTP2 fields.
	 */
	u_int off_li;
	u_int off_li_hsl;

	/*
	 * These are offsets for the MTP3 fields.
	 */
	u_int off_sio;
	u_int off_opc;
	u_int off_dpc;
	u_int off_sls;

	/*
	 * This is the offset of the first byte after the ATM pseudo_header,
	 * or -1 if there is no ATM pseudo-header.
	 */
	u_int off_payload;

	/*
	 * These are offsets to the beginning of the network-layer header.
	 * They are relative to the beginning of the link-layer payload
	 * (i.e., they don't include off_linkhdr.constant_part or
	 * off_linkpl.constant_part).
	 *
	 * If the link layer never uses 802.2 LLC:
	 *
	 *	"off_nl" and "off_nl_nosnap" are the same.
	 *
	 * If the link layer always uses 802.2 LLC:
	 *
	 *	"off_nl" is the offset if there's a SNAP header following
	 *	the 802.2 header;
	 *
	 *	"off_nl_nosnap" is the offset if there's no SNAP header.
	 *
	 * If the link layer is Ethernet:
	 *
	 *	"off_nl" is the offset if the packet is an Ethernet II packet
	 *	(we assume no 802.3+802.2+SNAP);
	 *
	 *	"off_nl_nosnap" is the offset if the packet is an 802.3 packet
	 *	with an 802.2 header following it.
	 */
	u_int off_nl;
	u_int off_nl_nosnap;

	/*
	 * Here we handle simple allocation of the scratch registers.
	 * If too many registers are alloc'd, the allocator punts.
	 */
	int regused[BPF_MEMWORDS];
	int curreg;

	/*
	 * Memory chunks.
	 */
	struct chunk chunks[NCHUNKS];
	int cur_chunk;
};

void PCAP_NORETURN
bpf_syntax_error(compiler_state_t *cstate, const char *msg)
{
	bpf_error(cstate, "syntax error in filter expression: %s", msg);
	/* NOTREACHED */
}

/* VARARGS */
void PCAP_NORETURN
bpf_error(compiler_state_t *cstate, const char *fmt, ...)
{
	va_list ap;

	va_start(ap, fmt);
	if (cstate->bpf_pcap != NULL)
		(void)pcap_vsnprintf(pcap_geterr(cstate->bpf_pcap),
		    PCAP_ERRBUF_SIZE, fmt, ap);
	va_end(ap);
	longjmp(cstate->top_ctx, 1);
	/* NOTREACHED */
}

static void init_linktype(compiler_state_t *, pcap_t *);

static void init_regs(compiler_state_t *);
static int alloc_reg(compiler_state_t *);
static void free_reg(compiler_state_t *, int);

static void initchunks(compiler_state_t *cstate);
static void *newchunk(compiler_state_t *cstate, size_t);
static void freechunks(compiler_state_t *cstate);
static inline struct block *new_block(compiler_state_t *cstate, int);
static inline struct slist *new_stmt(compiler_state_t *cstate, int);
static struct block *gen_retblk(compiler_state_t *cstate, int);
static inline void syntax(compiler_state_t *cstate);

static void backpatch(struct block *, struct block *);
static void merge(struct block *, struct block *);
static struct block *gen_cmp(compiler_state_t *, enum e_offrel, u_int,
    u_int, bpf_int32);
static struct block *gen_cmp_gt(compiler_state_t *, enum e_offrel, u_int,
    u_int, bpf_int32);
static struct block *gen_cmp_ge(compiler_state_t *, enum e_offrel, u_int,
    u_int, bpf_int32);
static struct block *gen_cmp_lt(compiler_state_t *, enum e_offrel, u_int,
    u_int, bpf_int32);
static struct block *gen_cmp_le(compiler_state_t *, enum e_offrel, u_int,
    u_int, bpf_int32);
static struct block *gen_mcmp(compiler_state_t *, enum e_offrel, u_int,
    u_int, bpf_int32, bpf_u_int32);
static struct block *gen_bcmp(compiler_state_t *, enum e_offrel, u_int,
    u_int, const u_char *);
static struct block *gen_ncmp(compiler_state_t *, enum e_offrel, bpf_u_int32,
    bpf_u_int32, bpf_u_int32, bpf_u_int32, int, bpf_int32);
static struct slist *gen_load_absoffsetrel(compiler_state_t *, bpf_abs_offset *,
    u_int, u_int);
static struct slist *gen_load_a(compiler_state_t *, enum e_offrel, u_int,
    u_int);
static struct slist *gen_loadx_iphdrlen(compiler_state_t *);
static struct block *gen_uncond(compiler_state_t *, int);
static inline struct block *gen_true(compiler_state_t *);
static inline struct block *gen_false(compiler_state_t *);
static struct block *gen_ether_linktype(compiler_state_t *, int);
static struct block *gen_ipnet_linktype(compiler_state_t *, int);
static struct block *gen_linux_sll_linktype(compiler_state_t *, int);
static struct slist *gen_load_prism_llprefixlen(compiler_state_t *);
static struct slist *gen_load_avs_llprefixlen(compiler_state_t *);
static struct slist *gen_load_radiotap_llprefixlen(compiler_state_t *);
static struct slist *gen_load_ppi_llprefixlen(compiler_state_t *);
static void insert_compute_vloffsets(compiler_state_t *, struct block *);
static struct slist *gen_abs_offset_varpart(compiler_state_t *,
    bpf_abs_offset *);
static int ethertype_to_ppptype(int);
static struct block *gen_linktype(compiler_state_t *, int);
static struct block *gen_snap(compiler_state_t *, bpf_u_int32, bpf_u_int32);
static struct block *gen_llc_linktype(compiler_state_t *, int);
static struct block *gen_hostop(compiler_state_t *, bpf_u_int32, bpf_u_int32,
    int, int, u_int, u_int);
#ifdef INET6
static struct block *gen_hostop6(compiler_state_t *, struct in6_addr *,
    struct in6_addr *, int, int, u_int, u_int);
#endif
static struct block *gen_ahostop(compiler_state_t *, const u_char *, int);
static struct block *gen_ehostop(compiler_state_t *, const u_char *, int);
static struct block *gen_fhostop(compiler_state_t *, const u_char *, int);
static struct block *gen_thostop(compiler_state_t *, const u_char *, int);
static struct block *gen_wlanhostop(compiler_state_t *, const u_char *, int);
static struct block *gen_ipfchostop(compiler_state_t *, const u_char *, int);
static struct block *gen_dnhostop(compiler_state_t *, bpf_u_int32, int);
static struct block *gen_mpls_linktype(compiler_state_t *, int);
static struct block *gen_host(compiler_state_t *, bpf_u_int32, bpf_u_int32,
    int, int, int);
#ifdef INET6
static struct block *gen_host6(compiler_state_t *, struct in6_addr *,
    struct in6_addr *, int, int, int);
#endif
#ifndef INET6
static struct block *gen_gateway(compiler_state_t *, const u_char *,
    struct addrinfo *, int, int);
#endif
static struct block *gen_ipfrag(compiler_state_t *);
static struct block *gen_portatom(compiler_state_t *, int, bpf_int32);
static struct block *gen_portrangeatom(compiler_state_t *, int, bpf_int32,
    bpf_int32);
static struct block *gen_portatom6(compiler_state_t *, int, bpf_int32);
static struct block *gen_portrangeatom6(compiler_state_t *, int, bpf_int32,
    bpf_int32);
struct block *gen_portop(compiler_state_t *, int, int, int);
static struct block *gen_port(compiler_state_t *, int, int, int);
struct block *gen_portrangeop(compiler_state_t *, int, int, int, int);
static struct block *gen_portrange(compiler_state_t *, int, int, int, int);
struct block *gen_portop6(compiler_state_t *, int, int, int);
static struct block *gen_port6(compiler_state_t *, int, int, int);
struct block *gen_portrangeop6(compiler_state_t *, int, int, int, int);
static struct block *gen_portrange6(compiler_state_t *, int, int, int, int);
static int lookup_proto(compiler_state_t *, const char *, int);
static struct block *gen_protochain(compiler_state_t *, int, int, int);
static struct block *gen_proto(compiler_state_t *, int, int, int);
static struct slist *xfer_to_x(compiler_state_t *, struct arth *);
static struct slist *xfer_to_a(compiler_state_t *, struct arth *);
static struct block *gen_mac_multicast(compiler_state_t *, int);
static struct block *gen_len(compiler_state_t *, int, int);
static struct block *gen_check_802_11_data_frame(compiler_state_t *);
static struct block *gen_geneve_ll_check(compiler_state_t *cstate);

static struct block *gen_ppi_dlt_check(compiler_state_t *);
static struct block *gen_msg_abbrev(compiler_state_t *, int type);

static void
initchunks(compiler_state_t *cstate)
{
	int i;

	for (i = 0; i < NCHUNKS; i++) {
		cstate->chunks[i].n_left = 0;
		cstate->chunks[i].m = NULL;
	}
	cstate->cur_chunk = 0;
}

static void *
newchunk(compiler_state_t *cstate, size_t n)
{
	struct chunk *cp;
	int k;
	size_t size;

#ifndef __NetBSD__
	/* XXX Round up to nearest long. */
	n = (n + sizeof(long) - 1) & ~(sizeof(long) - 1);
#else
	/* XXX Round up to structure boundary. */
	n = ALIGN(n);
#endif

	cp = &cstate->chunks[cstate->cur_chunk];
	if (n > cp->n_left) {
		++cp;
		k = ++cstate->cur_chunk;
		if (k >= NCHUNKS)
			bpf_error(cstate, "out of memory");
		size = CHUNK0SIZE << k;
		cp->m = (void *)malloc(size);
		if (cp->m == NULL)
			bpf_error(cstate, "out of memory");
		memset((char *)cp->m, 0, size);
		cp->n_left = size;
		if (n > size)
			bpf_error(cstate, "out of memory");
	}
	cp->n_left -= n;
	return (void *)((char *)cp->m + cp->n_left);
}

static void
freechunks(compiler_state_t *cstate)
{
	int i;

	for (i = 0; i < NCHUNKS; ++i)
		if (cstate->chunks[i].m != NULL)
			free(cstate->chunks[i].m);
}

/*
 * A strdup whose allocations are freed after code generation is over.
 */
char *
sdup(compiler_state_t *cstate, const char *s)
{
	size_t n = strlen(s) + 1;
	char *cp = newchunk(cstate, n);

	strlcpy(cp, s, n);
	return (cp);
}

static inline struct block *
new_block(compiler_state_t *cstate, int code)
{
	struct block *p;

	p = (struct block *)newchunk(cstate, sizeof(*p));
	p->s.code = code;
	p->head = p;

	return p;
}

static inline struct slist *
new_stmt(compiler_state_t *cstate, int code)
{
	struct slist *p;

	p = (struct slist *)newchunk(cstate, sizeof(*p));
	p->s.code = code;

	return p;
}

static struct block *
gen_retblk(compiler_state_t *cstate, int v)
{
	struct block *b = new_block(cstate, BPF_RET|BPF_K);

	b->s.k = v;
	return b;
}

static inline PCAP_NORETURN_DEF void
syntax(compiler_state_t *cstate)
{
	bpf_error(cstate, "syntax error in filter expression");
}

int
pcap_compile(pcap_t *p, struct bpf_program *program,
	     const char *buf, int optimize, bpf_u_int32 mask)
{
#ifdef _WIN32
	static int done = 0;
#endif
	compiler_state_t cstate;
	const char * volatile xbuf = buf;
	yyscan_t scanner = NULL;
	YY_BUFFER_STATE in_buffer = NULL;
	u_int len;
	int  rc;

	/*
	 * If this pcap_t hasn't been activated, it doesn't have a
	 * link-layer type, so we can't use it.
	 */
	if (!p->activated) {
		pcap_snprintf(p->errbuf, PCAP_ERRBUF_SIZE,
		    "not-yet-activated pcap_t passed to pcap_compile");
		return (-1);
	}

#ifdef _WIN32
	if (!done)
		pcap_wsockinit();
	done = 1;
#endif

#ifdef ENABLE_REMOTE
	/*
	 * If the device on which we're capturing need to be notified
	 * that a new filter is being compiled, do so.
	 *
	 * This allows them to save a copy of it, in case, for example,
	 * they're implementing a form of remote packet capture, and
	 * want the remote machine to filter out the packets in which
	 * it's sending the packets it's captured.
	 *
	 * XXX - the fact that we happen to be compiling a filter
	 * doesn't necessarily mean we'll be installing it as the
	 * filter for this pcap_t; we might be running it from userland
	 * on captured packets to do packet classification.  We really
	 * need a better way of handling this, but this is all that
	 * the WinPcap code did.
	 */
	if (p->save_current_filter_op != NULL)
		(p->save_current_filter_op)(p, buf);
#endif

	initchunks(&cstate);
	cstate.no_optimize = 0;
#ifdef INET6
	cstate.ai = NULL;
#endif
	cstate.ic.root = NULL;
	cstate.ic.cur_mark = 0;
	cstate.bpf_pcap = p;
	init_regs(&cstate);

	if (setjmp(cstate.top_ctx)) {
#ifdef INET6
		if (cstate.ai != NULL)
			freeaddrinfo(cstate.ai);
#endif
		rc = -1;
		goto quit;
	}

	cstate.netmask = mask;

	cstate.snaplen = pcap_snapshot(p);
	if (cstate.snaplen == 0) {
		pcap_snprintf(p->errbuf, PCAP_ERRBUF_SIZE,
			 "snaplen of 0 rejects all packets");
		rc = -1;
		goto quit;
	}

	if (pcap_lex_init(&scanner) != 0)
		pcap_fmt_errmsg_for_errno(p->errbuf, PCAP_ERRBUF_SIZE,
		    errno, "can't initialize scanner");
	in_buffer = pcap__scan_string(xbuf ? xbuf : "", scanner);

	/*
	 * Associate the compiler state with the lexical analyzer
	 * state.
	 */
	pcap_set_extra(&cstate, scanner);

	init_linktype(&cstate, p);
	(void)pcap_parse(scanner, &cstate);

	if (cstate.ic.root == NULL)
		cstate.ic.root = gen_retblk(&cstate, cstate.snaplen);

	if (optimize && !cstate.no_optimize) {
		bpf_optimize(&cstate, &cstate.ic);
		if (cstate.ic.root == NULL ||
		    (cstate.ic.root->s.code == (BPF_RET|BPF_K) && cstate.ic.root->s.k == 0))
			bpf_error(&cstate, "expression rejects all packets");
	}
	program->bf_insns = icode_to_fcode(&cstate, &cstate.ic, cstate.ic.root, &len);
	program->bf_len = len;

	rc = 0;  /* We're all okay */

quit:
	/*
	 * Clean up everything for the lexical analyzer.
	 */
	if (in_buffer != NULL)
		pcap__delete_buffer(in_buffer, scanner);
	if (scanner != NULL)
		pcap_lex_destroy(scanner);

	/*
	 * Clean up our own allocated memory.
	 */
	freechunks(&cstate);

	return (rc);
}

/*
 * entry point for using the compiler with no pcap open
 * pass in all the stuff that is needed explicitly instead.
 */
int
pcap_compile_nopcap(int snaplen_arg, int linktype_arg,
		    struct bpf_program *program,
	     const char *buf, int optimize, bpf_u_int32 mask)
{
	pcap_t *p;
	int ret;

	p = pcap_open_dead(linktype_arg, snaplen_arg);
	if (p == NULL)
		return (-1);
	ret = pcap_compile(p, program, buf, optimize, mask);
	pcap_close(p);
	return (ret);
}

/*
 * Clean up a "struct bpf_program" by freeing all the memory allocated
 * in it.
 */
void
pcap_freecode(struct bpf_program *program)
{
	program->bf_len = 0;
	if (program->bf_insns != NULL) {
		free((char *)program->bf_insns);
		program->bf_insns = NULL;
	}
}

/*
 * Backpatch the blocks in 'list' to 'target'.  The 'sense' field indicates
 * which of the jt and jf fields has been resolved and which is a pointer
 * back to another unresolved block (or nil).  At least one of the fields
 * in each block is already resolved.
 */
static void
backpatch(struct block *list, struct block *target)
{
	struct block *next;

	while (list) {
		if (!list->sense) {
			next = JT(list);
			JT(list) = target;
		} else {
			next = JF(list);
			JF(list) = target;
		}
		list = next;
	}
}

/*
 * Merge the lists in b0 and b1, using the 'sense' field to indicate
 * which of jt and jf is the link.
 */
static void
merge(struct block *b0, struct block *b1)
{
	register struct block **p = &b0;

	/* Find end of list. */
	while (*p)
		p = !((*p)->sense) ? &JT(*p) : &JF(*p);

	/* Concatenate the lists. */
	*p = b1;
}

void
finish_parse(compiler_state_t *cstate, struct block *p)
{
	struct block *ppi_dlt_check;

	/*
	 * Insert before the statements of the first (root) block any
	 * statements needed to load the lengths of any variable-length
	 * headers into registers.
	 *
	 * XXX - a fancier strategy would be to insert those before the
	 * statements of all blocks that use those lengths and that
	 * have no predecessors that use them, so that we only compute
	 * the lengths if we need them.  There might be even better
	 * approaches than that.
	 *
	 * However, those strategies would be more complicated, and
	 * as we don't generate code to compute a length if the
	 * program has no tests that use the length, and as most
	 * tests will probably use those lengths, we would just
	 * postpone computing the lengths so that it's not done
	 * for tests that fail early, and it's not clear that's
	 * worth the effort.
	 */
	insert_compute_vloffsets(cstate, p->head);

	/*
	 * For DLT_PPI captures, generate a check of the per-packet
	 * DLT value to make sure it's DLT_IEEE802_11.
	 *
	 * XXX - TurboCap cards use DLT_PPI for Ethernet.
	 * Can we just define some DLT_ETHERNET_WITH_PHDR pseudo-header
	 * with appropriate Ethernet information and use that rather
	 * than using something such as DLT_PPI where you don't know
	 * the link-layer header type until runtime, which, in the
	 * general case, would force us to generate both Ethernet *and*
	 * 802.11 code (*and* anything else for which PPI is used)
	 * and choose between them early in the BPF program?
	 */
	ppi_dlt_check = gen_ppi_dlt_check(cstate);
	if (ppi_dlt_check != NULL)
		gen_and(ppi_dlt_check, p);

	backpatch(p, gen_retblk(cstate, cstate->snaplen));
	p->sense = !p->sense;
	backpatch(p, gen_retblk(cstate, 0));
	cstate->ic.root = p->head;
}

void
gen_and(struct block *b0, struct block *b1)
{
	backpatch(b0, b1->head);
	b0->sense = !b0->sense;
	b1->sense = !b1->sense;
	merge(b1, b0);
	b1->sense = !b1->sense;
	b1->head = b0->head;
}

void
gen_or(struct block *b0, struct block *b1)
{
	b0->sense = !b0->sense;
	backpatch(b0, b1->head);
	b0->sense = !b0->sense;
	merge(b1, b0);
	b1->head = b0->head;
}

void
gen_not(struct block *b)
{
	b->sense = !b->sense;
}

static struct block *
gen_cmp(compiler_state_t *cstate, enum e_offrel offrel, u_int offset,
    u_int size, bpf_int32 v)
{
	return gen_ncmp(cstate, offrel, offset, size, 0xffffffff, BPF_JEQ, 0, v);
}

static struct block *
gen_cmp_gt(compiler_state_t *cstate, enum e_offrel offrel, u_int offset,
    u_int size, bpf_int32 v)
{
	return gen_ncmp(cstate, offrel, offset, size, 0xffffffff, BPF_JGT, 0, v);
}

static struct block *
gen_cmp_ge(compiler_state_t *cstate, enum e_offrel offrel, u_int offset,
    u_int size, bpf_int32 v)
{
	return gen_ncmp(cstate, offrel, offset, size, 0xffffffff, BPF_JGE, 0, v);
}

static struct block *
gen_cmp_lt(compiler_state_t *cstate, enum e_offrel offrel, u_int offset,
    u_int size, bpf_int32 v)
{
	return gen_ncmp(cstate, offrel, offset, size, 0xffffffff, BPF_JGE, 1, v);
}

static struct block *
gen_cmp_le(compiler_state_t *cstate, enum e_offrel offrel, u_int offset,
    u_int size, bpf_int32 v)
{
	return gen_ncmp(cstate, offrel, offset, size, 0xffffffff, BPF_JGT, 1, v);
}

static struct block *
gen_mcmp(compiler_state_t *cstate, enum e_offrel offrel, u_int offset,
    u_int size, bpf_int32 v, bpf_u_int32 mask)
{
	return gen_ncmp(cstate, offrel, offset, size, mask, BPF_JEQ, 0, v);
}

static struct block *
gen_bcmp(compiler_state_t *cstate, enum e_offrel offrel, u_int offset,
    u_int size, const u_char *v)
{
	register struct block *b, *tmp;

	b = NULL;
	while (size >= 4) {
		register const u_char *p = &v[size - 4];
		bpf_int32 w = ((bpf_int32)p[0] << 24) |
		    ((bpf_int32)p[1] << 16) | ((bpf_int32)p[2] << 8) | p[3];

		tmp = gen_cmp(cstate, offrel, offset + size - 4, BPF_W, w);
		if (b != NULL)
			gen_and(b, tmp);
		b = tmp;
		size -= 4;
	}
	while (size >= 2) {
		register const u_char *p = &v[size - 2];
		bpf_int32 w = ((bpf_int32)p[0] << 8) | p[1];

		tmp = gen_cmp(cstate, offrel, offset + size - 2, BPF_H, w);
		if (b != NULL)
			gen_and(b, tmp);
		b = tmp;
		size -= 2;
	}
	if (size > 0) {
		tmp = gen_cmp(cstate, offrel, offset, BPF_B, (bpf_int32)v[0]);
		if (b != NULL)
			gen_and(b, tmp);
		b = tmp;
	}
	return b;
}

/*
 * AND the field of size "size" at offset "offset" relative to the header
 * specified by "offrel" with "mask", and compare it with the value "v"
 * with the test specified by "jtype"; if "reverse" is true, the test
 * should test the opposite of "jtype".
 */
static struct block *
gen_ncmp(compiler_state_t *cstate, enum e_offrel offrel, bpf_u_int32 offset,
    bpf_u_int32 size, bpf_u_int32 mask, bpf_u_int32 jtype, int reverse,
    bpf_int32 v)
{
	struct slist *s, *s2;
	struct block *b;

	s = gen_load_a(cstate, offrel, offset, size);

	if (mask != 0xffffffff) {
		s2 = new_stmt(cstate, BPF_ALU|BPF_AND|BPF_K);
		s2->s.k = mask;
		sappend(s, s2);
	}

	b = new_block(cstate, JMP(jtype));
	b->stmts = s;
	b->s.k = v;
	if (reverse && (jtype == BPF_JGT || jtype == BPF_JGE))
		gen_not(b);
	return b;
}

static void
init_linktype(compiler_state_t *cstate, pcap_t *p)
{
	cstate->pcap_fddipad = p->fddipad;

	/*
	 * We start out with only one link-layer header.
	 */
	cstate->outermostlinktype = pcap_datalink(p);
	cstate->off_outermostlinkhdr.constant_part = 0;
	cstate->off_outermostlinkhdr.is_variable = 0;
	cstate->off_outermostlinkhdr.reg = -1;

	cstate->prevlinktype = cstate->outermostlinktype;
	cstate->off_prevlinkhdr.constant_part = 0;
	cstate->off_prevlinkhdr.is_variable = 0;
	cstate->off_prevlinkhdr.reg = -1;

	cstate->linktype = cstate->outermostlinktype;
	cstate->off_linkhdr.constant_part = 0;
	cstate->off_linkhdr.is_variable = 0;
	cstate->off_linkhdr.reg = -1;

	/*
	 * XXX
	 */
	cstate->off_linkpl.constant_part = 0;
	cstate->off_linkpl.is_variable = 0;
	cstate->off_linkpl.reg = -1;

	cstate->off_linktype.constant_part = 0;
	cstate->off_linktype.is_variable = 0;
	cstate->off_linktype.reg = -1;

	/*
	 * Assume it's not raw ATM with a pseudo-header, for now.
	 */
	cstate->is_atm = 0;
	cstate->off_vpi = OFFSET_NOT_SET;
	cstate->off_vci = OFFSET_NOT_SET;
	cstate->off_proto = OFFSET_NOT_SET;
	cstate->off_payload = OFFSET_NOT_SET;

	/*
	 * And not Geneve.
	 */
	cstate->is_geneve = 0;

	/*
	 * No variable length VLAN offset by default
	 */
	cstate->is_vlan_vloffset = 0;

	/*
	 * And assume we're not doing SS7.
	 */
	cstate->off_li = OFFSET_NOT_SET;
	cstate->off_li_hsl = OFFSET_NOT_SET;
	cstate->off_sio = OFFSET_NOT_SET;
	cstate->off_opc = OFFSET_NOT_SET;
	cstate->off_dpc = OFFSET_NOT_SET;
	cstate->off_sls = OFFSET_NOT_SET;

	cstate->label_stack_depth = 0;
	cstate->vlan_stack_depth = 0;

	switch (cstate->linktype) {

	case DLT_ARCNET:
		cstate->off_linktype.constant_part = 2;
		cstate->off_linkpl.constant_part = 6;
		cstate->off_nl = 0;		/* XXX in reality, variable! */
		cstate->off_nl_nosnap = 0;	/* no 802.2 LLC */
		break;

	case DLT_ARCNET_LINUX:
		cstate->off_linktype.constant_part = 4;
		cstate->off_linkpl.constant_part = 8;
		cstate->off_nl = 0;		/* XXX in reality, variable! */
		cstate->off_nl_nosnap = 0;	/* no 802.2 LLC */
		break;

	case DLT_EN10MB:
		cstate->off_linktype.constant_part = 12;
		cstate->off_linkpl.constant_part = 14;	/* Ethernet header length */
		cstate->off_nl = 0;		/* Ethernet II */
		cstate->off_nl_nosnap = 3;	/* 802.3+802.2 */
		break;

	case DLT_SLIP:
		/*
		 * SLIP doesn't have a link level type.  The 16 byte
		 * header is hacked into our SLIP driver.
		 */
		cstate->off_linktype.constant_part = OFFSET_NOT_SET;
		cstate->off_linkpl.constant_part = 16;
		cstate->off_nl = 0;
		cstate->off_nl_nosnap = 0;	/* no 802.2 LLC */
		break;

	case DLT_SLIP_BSDOS:
		/* XXX this may be the same as the DLT_PPP_BSDOS case */
		cstate->off_linktype.constant_part = OFFSET_NOT_SET;
		/* XXX end */
		cstate->off_linkpl.constant_part = 24;
		cstate->off_nl = 0;
		cstate->off_nl_nosnap = 0;	/* no 802.2 LLC */
		break;

	case DLT_NULL:
	case DLT_LOOP:
		cstate->off_linktype.constant_part = 0;
		cstate->off_linkpl.constant_part = 4;
		cstate->off_nl = 0;
		cstate->off_nl_nosnap = 0;	/* no 802.2 LLC */
		break;

	case DLT_ENC:
		cstate->off_linktype.constant_part = 0;
		cstate->off_linkpl.constant_part = 12;
		cstate->off_nl = 0;
		cstate->off_nl_nosnap = 0;	/* no 802.2 LLC */
		break;

	case DLT_PPP:
	case DLT_PPP_PPPD:
	case DLT_C_HDLC:		/* BSD/OS Cisco HDLC */
	case DLT_PPP_SERIAL:		/* NetBSD sync/async serial PPP */
		cstate->off_linktype.constant_part = 2;	/* skip HDLC-like framing */
		cstate->off_linkpl.constant_part = 4;	/* skip HDLC-like framing and protocol field */
		cstate->off_nl = 0;
		cstate->off_nl_nosnap = 0;	/* no 802.2 LLC */
		break;

	case DLT_PPP_ETHER:
		/*
		 * This does no include the Ethernet header, and
		 * only covers session state.
		 */
		cstate->off_linktype.constant_part = 6;
		cstate->off_linkpl.constant_part = 8;
		cstate->off_nl = 0;
		cstate->off_nl_nosnap = 0;	/* no 802.2 LLC */
		break;

	case DLT_PPP_BSDOS:
		cstate->off_linktype.constant_part = 5;
		cstate->off_linkpl.constant_part = 24;
		cstate->off_nl = 0;
		cstate->off_nl_nosnap = 0;	/* no 802.2 LLC */
		break;

	case DLT_FDDI:
		/*
		 * FDDI doesn't really have a link-level type field.
		 * We set "off_linktype" to the offset of the LLC header.
		 *
		 * To check for Ethernet types, we assume that SSAP = SNAP
		 * is being used and pick out the encapsulated Ethernet type.
		 * XXX - should we generate code to check for SNAP?
		 */
		cstate->off_linktype.constant_part = 13;
		cstate->off_linktype.constant_part += cstate->pcap_fddipad;
		cstate->off_linkpl.constant_part = 13;	/* FDDI MAC header length */
		cstate->off_linkpl.constant_part += cstate->pcap_fddipad;
		cstate->off_nl = 8;		/* 802.2+SNAP */
		cstate->off_nl_nosnap = 3;	/* 802.2 */
		break;

	case DLT_IEEE802:
		/*
		 * Token Ring doesn't really have a link-level type field.
		 * We set "off_linktype" to the offset of the LLC header.
		 *
		 * To check for Ethernet types, we assume that SSAP = SNAP
		 * is being used and pick out the encapsulated Ethernet type.
		 * XXX - should we generate code to check for SNAP?
		 *
		 * XXX - the header is actually variable-length.
		 * Some various Linux patched versions gave 38
		 * as "off_linktype" and 40 as "off_nl"; however,
		 * if a token ring packet has *no* routing
		 * information, i.e. is not source-routed, the correct
		 * values are 20 and 22, as they are in the vanilla code.
		 *
		 * A packet is source-routed iff the uppermost bit
		 * of the first byte of the source address, at an
		 * offset of 8, has the uppermost bit set.  If the
		 * packet is source-routed, the total number of bytes
		 * of routing information is 2 plus bits 0x1F00 of
		 * the 16-bit value at an offset of 14 (shifted right
		 * 8 - figure out which byte that is).
		 */
		cstate->off_linktype.constant_part = 14;
		cstate->off_linkpl.constant_part = 14;	/* Token Ring MAC header length */
		cstate->off_nl = 8;		/* 802.2+SNAP */
		cstate->off_nl_nosnap = 3;	/* 802.2 */
		break;

	case DLT_PRISM_HEADER:
	case DLT_IEEE802_11_RADIO_AVS:
	case DLT_IEEE802_11_RADIO:
		cstate->off_linkhdr.is_variable = 1;
		/* Fall through, 802.11 doesn't have a variable link
		 * prefix but is otherwise the same. */

	case DLT_IEEE802_11:
		/*
		 * 802.11 doesn't really have a link-level type field.
		 * We set "off_linktype.constant_part" to the offset of
		 * the LLC header.
		 *
		 * To check for Ethernet types, we assume that SSAP = SNAP
		 * is being used and pick out the encapsulated Ethernet type.
		 * XXX - should we generate code to check for SNAP?
		 *
		 * We also handle variable-length radio headers here.
		 * The Prism header is in theory variable-length, but in
		 * practice it's always 144 bytes long.  However, some
		 * drivers on Linux use ARPHRD_IEEE80211_PRISM, but
		 * sometimes or always supply an AVS header, so we
		 * have to check whether the radio header is a Prism
		 * header or an AVS header, so, in practice, it's
		 * variable-length.
		 */
		cstate->off_linktype.constant_part = 24;
		cstate->off_linkpl.constant_part = 0;	/* link-layer header is variable-length */
		cstate->off_linkpl.is_variable = 1;
		cstate->off_nl = 8;		/* 802.2+SNAP */
		cstate->off_nl_nosnap = 3;	/* 802.2 */
		break;

	case DLT_PPI:
		/*
		 * At the moment we treat PPI the same way that we treat
		 * normal Radiotap encoded packets. The difference is in
		 * the function that generates the code at the beginning
		 * to compute the header length.  Since this code generator
		 * of PPI supports bare 802.11 encapsulation only (i.e.
		 * the encapsulated DLT should be DLT_IEEE802_11) we
		 * generate code to check for this too.
		 */
		cstate->off_linktype.constant_part = 24;
		cstate->off_linkpl.constant_part = 0;	/* link-layer header is variable-length */
		cstate->off_linkpl.is_variable = 1;
		cstate->off_linkhdr.is_variable = 1;
		cstate->off_nl = 8;		/* 802.2+SNAP */
		cstate->off_nl_nosnap = 3;	/* 802.2 */
		break;

	case DLT_ATM_RFC1483:
	case DLT_ATM_CLIP:	/* Linux ATM defines this */
		/*
		 * assume routed, non-ISO PDUs
		 * (i.e., LLC = 0xAA-AA-03, OUT = 0x00-00-00)
		 *
		 * XXX - what about ISO PDUs, e.g. CLNP, ISIS, ESIS,
		 * or PPP with the PPP NLPID (e.g., PPPoA)?  The
		 * latter would presumably be treated the way PPPoE
		 * should be, so you can do "pppoe and udp port 2049"
		 * or "pppoa and tcp port 80" and have it check for
		 * PPPo{A,E} and a PPP protocol of IP and....
		 */
		cstate->off_linktype.constant_part = 0;
		cstate->off_linkpl.constant_part = 0;	/* packet begins with LLC header */
		cstate->off_nl = 8;		/* 802.2+SNAP */
		cstate->off_nl_nosnap = 3;	/* 802.2 */
		break;

	case DLT_SUNATM:
		/*
		 * Full Frontal ATM; you get AALn PDUs with an ATM
		 * pseudo-header.
		 */
		cstate->is_atm = 1;
		cstate->off_vpi = SUNATM_VPI_POS;
		cstate->off_vci = SUNATM_VCI_POS;
		cstate->off_proto = PROTO_POS;
		cstate->off_payload = SUNATM_PKT_BEGIN_POS;
		cstate->off_linktype.constant_part = cstate->off_payload;
		cstate->off_linkpl.constant_part = cstate->off_payload;	/* if LLC-encapsulated */
		cstate->off_nl = 8;		/* 802.2+SNAP */
		cstate->off_nl_nosnap = 3;	/* 802.2 */
		break;

	case DLT_RAW:
	case DLT_IPV4:
	case DLT_IPV6:
		cstate->off_linktype.constant_part = OFFSET_NOT_SET;
		cstate->off_linkpl.constant_part = 0;
		cstate->off_nl = 0;
		cstate->off_nl_nosnap = 0;	/* no 802.2 LLC */
		break;

	case DLT_LINUX_SLL:	/* fake header for Linux cooked socket */
		cstate->off_linktype.constant_part = 14;
		cstate->off_linkpl.constant_part = 16;
		cstate->off_nl = 0;
		cstate->off_nl_nosnap = 0;	/* no 802.2 LLC */
		break;

	case DLT_LTALK:
		/*
		 * LocalTalk does have a 1-byte type field in the LLAP header,
		 * but really it just indicates whether there is a "short" or
		 * "long" DDP packet following.
		 */
		cstate->off_linktype.constant_part = OFFSET_NOT_SET;
		cstate->off_linkpl.constant_part = 0;
		cstate->off_nl = 0;
		cstate->off_nl_nosnap = 0;	/* no 802.2 LLC */
		break;

	case DLT_IP_OVER_FC:
		/*
		 * RFC 2625 IP-over-Fibre-Channel doesn't really have a
		 * link-level type field.  We set "off_linktype" to the
		 * offset of the LLC header.
		 *
		 * To check for Ethernet types, we assume that SSAP = SNAP
		 * is being used and pick out the encapsulated Ethernet type.
		 * XXX - should we generate code to check for SNAP? RFC
		 * 2625 says SNAP should be used.
		 */
		cstate->off_linktype.constant_part = 16;
		cstate->off_linkpl.constant_part = 16;
		cstate->off_nl = 8;		/* 802.2+SNAP */
		cstate->off_nl_nosnap = 3;	/* 802.2 */
		break;

	case DLT_FRELAY:
		/*
		 * XXX - we should set this to handle SNAP-encapsulated
		 * frames (NLPID of 0x80).
		 */
		cstate->off_linktype.constant_part = OFFSET_NOT_SET;
		cstate->off_linkpl.constant_part = 0;
		cstate->off_nl = 0;
		cstate->off_nl_nosnap = 0;	/* no 802.2 LLC */
		break;

                /*
                 * the only BPF-interesting FRF.16 frames are non-control frames;
                 * Frame Relay has a variable length link-layer
                 * so lets start with offset 4 for now and increments later on (FIXME);
                 */
	case DLT_MFR:
		cstate->off_linktype.constant_part = OFFSET_NOT_SET;
		cstate->off_linkpl.constant_part = 0;
		cstate->off_nl = 4;
		cstate->off_nl_nosnap = 0;	/* XXX - for now -> no 802.2 LLC */
		break;

	case DLT_APPLE_IP_OVER_IEEE1394:
		cstate->off_linktype.constant_part = 16;
		cstate->off_linkpl.constant_part = 18;
		cstate->off_nl = 0;
		cstate->off_nl_nosnap = 0;	/* no 802.2 LLC */
		break;

	case DLT_SYMANTEC_FIREWALL:
		cstate->off_linktype.constant_part = 6;
		cstate->off_linkpl.constant_part = 44;
		cstate->off_nl = 0;		/* Ethernet II */
		cstate->off_nl_nosnap = 0;	/* XXX - what does it do with 802.3 packets? */
		break;

#ifdef HAVE_NET_PFVAR_H
	case DLT_PFLOG:
		cstate->off_linktype.constant_part = 0;
		cstate->off_linkpl.constant_part = PFLOG_HDRLEN;
		cstate->off_nl = 0;
		cstate->off_nl_nosnap = 0;	/* no 802.2 LLC */
		break;
#endif

        case DLT_JUNIPER_MFR:
        case DLT_JUNIPER_MLFR:
        case DLT_JUNIPER_MLPPP:
        case DLT_JUNIPER_PPP:
        case DLT_JUNIPER_CHDLC:
        case DLT_JUNIPER_FRELAY:
		cstate->off_linktype.constant_part = 4;
		cstate->off_linkpl.constant_part = 4;
		cstate->off_nl = 0;
		cstate->off_nl_nosnap = OFFSET_NOT_SET;	/* no 802.2 LLC */
                break;

	case DLT_JUNIPER_ATM1:
		cstate->off_linktype.constant_part = 4;		/* in reality variable between 4-8 */
		cstate->off_linkpl.constant_part = 4;	/* in reality variable between 4-8 */
		cstate->off_nl = 0;
		cstate->off_nl_nosnap = 10;
		break;

	case DLT_JUNIPER_ATM2:
		cstate->off_linktype.constant_part = 8;		/* in reality variable between 8-12 */
		cstate->off_linkpl.constant_part = 8;	/* in reality variable between 8-12 */
		cstate->off_nl = 0;
		cstate->off_nl_nosnap = 10;
		break;

		/* frames captured on a Juniper PPPoE service PIC
		 * contain raw ethernet frames */
	case DLT_JUNIPER_PPPOE:
        case DLT_JUNIPER_ETHER:
		cstate->off_linkpl.constant_part = 14;
		cstate->off_linktype.constant_part = 16;
		cstate->off_nl = 18;		/* Ethernet II */
		cstate->off_nl_nosnap = 21;	/* 802.3+802.2 */
		break;

	case DLT_JUNIPER_PPPOE_ATM:
		cstate->off_linktype.constant_part = 4;
		cstate->off_linkpl.constant_part = 6;
		cstate->off_nl = 0;
		cstate->off_nl_nosnap = OFFSET_NOT_SET;	/* no 802.2 LLC */
		break;

	case DLT_JUNIPER_GGSN:
		cstate->off_linktype.constant_part = 6;
		cstate->off_linkpl.constant_part = 12;
		cstate->off_nl = 0;
		cstate->off_nl_nosnap = OFFSET_NOT_SET;	/* no 802.2 LLC */
		break;

	case DLT_JUNIPER_ES:
		cstate->off_linktype.constant_part = 6;
		cstate->off_linkpl.constant_part = OFFSET_NOT_SET;	/* not really a network layer but raw IP addresses */
		cstate->off_nl = OFFSET_NOT_SET;	/* not really a network layer but raw IP addresses */
		cstate->off_nl_nosnap = OFFSET_NOT_SET;	/* no 802.2 LLC */
		break;

	case DLT_JUNIPER_MONITOR:
		cstate->off_linktype.constant_part = 12;
		cstate->off_linkpl.constant_part = 12;
		cstate->off_nl = 0;			/* raw IP/IP6 header */
		cstate->off_nl_nosnap = OFFSET_NOT_SET;	/* no 802.2 LLC */
		break;

	case DLT_BACNET_MS_TP:
		cstate->off_linktype.constant_part = OFFSET_NOT_SET;
		cstate->off_linkpl.constant_part = OFFSET_NOT_SET;
		cstate->off_nl = OFFSET_NOT_SET;
		cstate->off_nl_nosnap = OFFSET_NOT_SET;
		break;

	case DLT_JUNIPER_SERVICES:
		cstate->off_linktype.constant_part = 12;
		cstate->off_linkpl.constant_part = OFFSET_NOT_SET;	/* L3 proto location dep. on cookie type */
		cstate->off_nl = OFFSET_NOT_SET;	/* L3 proto location dep. on cookie type */
		cstate->off_nl_nosnap = OFFSET_NOT_SET;	/* no 802.2 LLC */
		break;

	case DLT_JUNIPER_VP:
		cstate->off_linktype.constant_part = 18;
		cstate->off_linkpl.constant_part = OFFSET_NOT_SET;
		cstate->off_nl = OFFSET_NOT_SET;
		cstate->off_nl_nosnap = OFFSET_NOT_SET;
		break;

	case DLT_JUNIPER_ST:
		cstate->off_linktype.constant_part = 18;
		cstate->off_linkpl.constant_part = OFFSET_NOT_SET;
		cstate->off_nl = OFFSET_NOT_SET;
		cstate->off_nl_nosnap = OFFSET_NOT_SET;
		break;

	case DLT_JUNIPER_ISM:
		cstate->off_linktype.constant_part = 8;
		cstate->off_linkpl.constant_part = OFFSET_NOT_SET;
		cstate->off_nl = OFFSET_NOT_SET;
		cstate->off_nl_nosnap = OFFSET_NOT_SET;
		break;

	case DLT_JUNIPER_VS:
	case DLT_JUNIPER_SRX_E2E:
	case DLT_JUNIPER_FIBRECHANNEL:
	case DLT_JUNIPER_ATM_CEMIC:
		cstate->off_linktype.constant_part = 8;
		cstate->off_linkpl.constant_part = OFFSET_NOT_SET;
		cstate->off_nl = OFFSET_NOT_SET;
		cstate->off_nl_nosnap = OFFSET_NOT_SET;
		break;

	case DLT_MTP2:
		cstate->off_li = 2;
		cstate->off_li_hsl = 4;
		cstate->off_sio = 3;
		cstate->off_opc = 4;
		cstate->off_dpc = 4;
		cstate->off_sls = 7;
		cstate->off_linktype.constant_part = OFFSET_NOT_SET;
		cstate->off_linkpl.constant_part = OFFSET_NOT_SET;
		cstate->off_nl = OFFSET_NOT_SET;
		cstate->off_nl_nosnap = OFFSET_NOT_SET;
		break;

	case DLT_MTP2_WITH_PHDR:
		cstate->off_li = 6;
		cstate->off_li_hsl = 8;
		cstate->off_sio = 7;
		cstate->off_opc = 8;
		cstate->off_dpc = 8;
		cstate->off_sls = 11;
		cstate->off_linktype.constant_part = OFFSET_NOT_SET;
		cstate->off_linkpl.constant_part = OFFSET_NOT_SET;
		cstate->off_nl = OFFSET_NOT_SET;
		cstate->off_nl_nosnap = OFFSET_NOT_SET;
		break;

	case DLT_ERF:
		cstate->off_li = 22;
		cstate->off_li_hsl = 24;
		cstate->off_sio = 23;
		cstate->off_opc = 24;
		cstate->off_dpc = 24;
		cstate->off_sls = 27;
		cstate->off_linktype.constant_part = OFFSET_NOT_SET;
		cstate->off_linkpl.constant_part = OFFSET_NOT_SET;
		cstate->off_nl = OFFSET_NOT_SET;
		cstate->off_nl_nosnap = OFFSET_NOT_SET;
		break;

	case DLT_PFSYNC:
		cstate->off_linktype.constant_part = OFFSET_NOT_SET;
		cstate->off_linkpl.constant_part = 4;
		cstate->off_nl = 0;
		cstate->off_nl_nosnap = 0;
		break;

	case DLT_AX25_KISS:
		/*
		 * Currently, only raw "link[N:M]" filtering is supported.
		 */
		cstate->off_linktype.constant_part = OFFSET_NOT_SET;	/* variable, min 15, max 71 steps of 7 */
		cstate->off_linkpl.constant_part = OFFSET_NOT_SET;
		cstate->off_nl = OFFSET_NOT_SET;	/* variable, min 16, max 71 steps of 7 */
		cstate->off_nl_nosnap = OFFSET_NOT_SET;	/* no 802.2 LLC */
		break;

	case DLT_IPNET:
		cstate->off_linktype.constant_part = 1;
		cstate->off_linkpl.constant_part = 24;	/* ipnet header length */
		cstate->off_nl = 0;
		cstate->off_nl_nosnap = OFFSET_NOT_SET;
		break;

	case DLT_NETANALYZER:
		cstate->off_linkhdr.constant_part = 4;	/* Ethernet header is past 4-byte pseudo-header */
		cstate->off_linktype.constant_part = cstate->off_linkhdr.constant_part + 12;
		cstate->off_linkpl.constant_part = cstate->off_linkhdr.constant_part + 14;	/* pseudo-header+Ethernet header length */
		cstate->off_nl = 0;		/* Ethernet II */
		cstate->off_nl_nosnap = 3;	/* 802.3+802.2 */
		break;

	case DLT_NETANALYZER_TRANSPARENT:
		cstate->off_linkhdr.constant_part = 12;	/* MAC header is past 4-byte pseudo-header, preamble, and SFD */
		cstate->off_linktype.constant_part = cstate->off_linkhdr.constant_part + 12;
		cstate->off_linkpl.constant_part = cstate->off_linkhdr.constant_part + 14;	/* pseudo-header+preamble+SFD+Ethernet header length */
		cstate->off_nl = 0;		/* Ethernet II */
		cstate->off_nl_nosnap = 3;	/* 802.3+802.2 */
		break;

	default:
		/*
		 * For values in the range in which we've assigned new
		 * DLT_ values, only raw "link[N:M]" filtering is supported.
		 */
		if (cstate->linktype >= DLT_MATCHING_MIN &&
		    cstate->linktype <= DLT_MATCHING_MAX) {
			cstate->off_linktype.constant_part = OFFSET_NOT_SET;
			cstate->off_linkpl.constant_part = OFFSET_NOT_SET;
			cstate->off_nl = OFFSET_NOT_SET;
			cstate->off_nl_nosnap = OFFSET_NOT_SET;
		} else {
			bpf_error(cstate, "unknown data link type %d", cstate->linktype);
		}
		break;
	}

	cstate->off_outermostlinkhdr = cstate->off_prevlinkhdr = cstate->off_linkhdr;
}

/*
 * Load a value relative to the specified absolute offset.
 */
static struct slist *
gen_load_absoffsetrel(compiler_state_t *cstate, bpf_abs_offset *abs_offset,
    u_int offset, u_int size)
{
	struct slist *s, *s2;

	s = gen_abs_offset_varpart(cstate, abs_offset);

	/*
	 * If "s" is non-null, it has code to arrange that the X register
	 * contains the variable part of the absolute offset, so we
	 * generate a load relative to that, with an offset of
	 * abs_offset->constant_part + offset.
	 *
	 * Otherwise, we can do an absolute load with an offset of
	 * abs_offset->constant_part + offset.
	 */
	if (s != NULL) {
		/*
		 * "s" points to a list of statements that puts the
		 * variable part of the absolute offset into the X register.
		 * Do an indirect load, to use the X register as an offset.
		 */
		s2 = new_stmt(cstate, BPF_LD|BPF_IND|size);
		s2->s.k = abs_offset->constant_part + offset;
		sappend(s, s2);
	} else {
		/*
		 * There is no variable part of the absolute offset, so
		 * just do an absolute load.
		 */
		s = new_stmt(cstate, BPF_LD|BPF_ABS|size);
		s->s.k = abs_offset->constant_part + offset;
	}
	return s;
}

/*
 * Load a value relative to the beginning of the specified header.
 */
static struct slist *
gen_load_a(compiler_state_t *cstate, enum e_offrel offrel, u_int offset,
    u_int size)
{
	struct slist *s, *s2;

	switch (offrel) {

	case OR_PACKET:
                s = new_stmt(cstate, BPF_LD|BPF_ABS|size);
                s->s.k = offset;
		break;

	case OR_LINKHDR:
		s = gen_load_absoffsetrel(cstate, &cstate->off_linkhdr, offset, size);
		break;

	case OR_PREVLINKHDR:
		s = gen_load_absoffsetrel(cstate, &cstate->off_prevlinkhdr, offset, size);
		break;

	case OR_LLC:
		s = gen_load_absoffsetrel(cstate, &cstate->off_linkpl, offset, size);
		break;

	case OR_PREVMPLSHDR:
		s = gen_load_absoffsetrel(cstate, &cstate->off_linkpl, cstate->off_nl - 4 + offset, size);
		break;

	case OR_LINKPL:
		s = gen_load_absoffsetrel(cstate, &cstate->off_linkpl, cstate->off_nl + offset, size);
		break;

	case OR_LINKPL_NOSNAP:
		s = gen_load_absoffsetrel(cstate, &cstate->off_linkpl, cstate->off_nl_nosnap + offset, size);
		break;

	case OR_LINKTYPE:
		s = gen_load_absoffsetrel(cstate, &cstate->off_linktype, offset, size);
		break;

	case OR_TRAN_IPV4:
		/*
		 * Load the X register with the length of the IPv4 header
		 * (plus the offset of the link-layer header, if it's
		 * preceded by a variable-length header such as a radio
		 * header), in bytes.
		 */
		s = gen_loadx_iphdrlen(cstate);

		/*
		 * Load the item at {offset of the link-layer payload} +
		 * {offset, relative to the start of the link-layer
		 * paylod, of the IPv4 header} + {length of the IPv4 header} +
		 * {specified offset}.
		 *
		 * If the offset of the link-layer payload is variable,
		 * the variable part of that offset is included in the
		 * value in the X register, and we include the constant
		 * part in the offset of the load.
		 */
		s2 = new_stmt(cstate, BPF_LD|BPF_IND|size);
		s2->s.k = cstate->off_linkpl.constant_part + cstate->off_nl + offset;
		sappend(s, s2);
		break;

	case OR_TRAN_IPV6:
		s = gen_load_absoffsetrel(cstate, &cstate->off_linkpl, cstate->off_nl + 40 + offset, size);
		break;

	default:
		abort();
		/* NOTREACHED */
	}
	return s;
}

/*
 * Generate code to load into the X register the sum of the length of
 * the IPv4 header and the variable part of the offset of the link-layer
 * payload.
 */
static struct slist *
gen_loadx_iphdrlen(compiler_state_t *cstate)
{
	struct slist *s, *s2;

	s = gen_abs_offset_varpart(cstate, &cstate->off_linkpl);
	if (s != NULL) {
		/*
		 * The offset of the link-layer payload has a variable
		 * part.  "s" points to a list of statements that put
		 * the variable part of that offset into the X register.
		 *
		 * The 4*([k]&0xf) addressing mode can't be used, as we
		 * don't have a constant offset, so we have to load the
		 * value in question into the A register and add to it
		 * the value from the X register.
		 */
		s2 = new_stmt(cstate, BPF_LD|BPF_IND|BPF_B);
		s2->s.k = cstate->off_linkpl.constant_part + cstate->off_nl;
		sappend(s, s2);
		s2 = new_stmt(cstate, BPF_ALU|BPF_AND|BPF_K);
		s2->s.k = 0xf;
		sappend(s, s2);
		s2 = new_stmt(cstate, BPF_ALU|BPF_LSH|BPF_K);
		s2->s.k = 2;
		sappend(s, s2);

		/*
		 * The A register now contains the length of the IP header.
		 * We need to add to it the variable part of the offset of
		 * the link-layer payload, which is still in the X
		 * register, and move the result into the X register.
		 */
		sappend(s, new_stmt(cstate, BPF_ALU|BPF_ADD|BPF_X));
		sappend(s, new_stmt(cstate, BPF_MISC|BPF_TAX));
	} else {
		/*
		 * The offset of the link-layer payload is a constant,
		 * so no code was generated to load the (non-existent)
		 * variable part of that offset.
		 *
		 * This means we can use the 4*([k]&0xf) addressing
		 * mode.  Load the length of the IPv4 header, which
		 * is at an offset of cstate->off_nl from the beginning of
		 * the link-layer payload, and thus at an offset of
		 * cstate->off_linkpl.constant_part + cstate->off_nl from the beginning
		 * of the raw packet data, using that addressing mode.
		 */
		s = new_stmt(cstate, BPF_LDX|BPF_MSH|BPF_B);
		s->s.k = cstate->off_linkpl.constant_part + cstate->off_nl;
	}
	return s;
}


static struct block *
gen_uncond(compiler_state_t *cstate, int rsense)
{
	struct block *b;
	struct slist *s;

	s = new_stmt(cstate, BPF_LD|BPF_IMM);
	s->s.k = !rsense;
	b = new_block(cstate, JMP(BPF_JEQ));
	b->stmts = s;

	return b;
}

static inline struct block *
gen_true(compiler_state_t *cstate)
{
	return gen_uncond(cstate, 1);
}

static inline struct block *
gen_false(compiler_state_t *cstate)
{
	return gen_uncond(cstate, 0);
}

/*
 * Byte-swap a 32-bit number.
 * ("htonl()" or "ntohl()" won't work - we want to byte-swap even on
 * big-endian platforms.)
 */
#define	SWAPLONG(y) \
((((y)&0xff)<<24) | (((y)&0xff00)<<8) | (((y)&0xff0000)>>8) | (((y)>>24)&0xff))

/*
 * Generate code to match a particular packet type.
 *
 * "proto" is an Ethernet type value, if > ETHERMTU, or an LLC SAP
 * value, if <= ETHERMTU.  We use that to determine whether to
 * match the type/length field or to check the type/length field for
 * a value <= ETHERMTU to see whether it's a type field and then do
 * the appropriate test.
 */
static struct block *
gen_ether_linktype(compiler_state_t *cstate, int proto)
{
	struct block *b0, *b1;

	switch (proto) {

	case LLCSAP_ISONS:
	case LLCSAP_IP:
	case LLCSAP_NETBEUI:
		/*
		 * OSI protocols and NetBEUI always use 802.2 encapsulation,
		 * so we check the DSAP and SSAP.
		 *
		 * LLCSAP_IP checks for IP-over-802.2, rather
		 * than IP-over-Ethernet or IP-over-SNAP.
		 *
		 * XXX - should we check both the DSAP and the
		 * SSAP, like this, or should we check just the
		 * DSAP, as we do for other types <= ETHERMTU
		 * (i.e., other SAP values)?
		 */
		b0 = gen_cmp_gt(cstate, OR_LINKTYPE, 0, BPF_H, ETHERMTU);
		gen_not(b0);
		b1 = gen_cmp(cstate, OR_LLC, 0, BPF_H, (bpf_int32)
			     ((proto << 8) | proto));
		gen_and(b0, b1);
		return b1;

	case LLCSAP_IPX:
		/*
		 * Check for;
		 *
		 *	Ethernet_II frames, which are Ethernet
		 *	frames with a frame type of ETHERTYPE_IPX;
		 *
		 *	Ethernet_802.3 frames, which are 802.3
		 *	frames (i.e., the type/length field is
		 *	a length field, <= ETHERMTU, rather than
		 *	a type field) with the first two bytes
		 *	after the Ethernet/802.3 header being
		 *	0xFFFF;
		 *
		 *	Ethernet_802.2 frames, which are 802.3
		 *	frames with an 802.2 LLC header and
		 *	with the IPX LSAP as the DSAP in the LLC
		 *	header;
		 *
		 *	Ethernet_SNAP frames, which are 802.3
		 *	frames with an LLC header and a SNAP
		 *	header and with an OUI of 0x000000
		 *	(encapsulated Ethernet) and a protocol
		 *	ID of ETHERTYPE_IPX in the SNAP header.
		 *
		 * XXX - should we generate the same code both
		 * for tests for LLCSAP_IPX and for ETHERTYPE_IPX?
		 */

		/*
		 * This generates code to check both for the
		 * IPX LSAP (Ethernet_802.2) and for Ethernet_802.3.
		 */
		b0 = gen_cmp(cstate, OR_LLC, 0, BPF_B, (bpf_int32)LLCSAP_IPX);
		b1 = gen_cmp(cstate, OR_LLC, 0, BPF_H, (bpf_int32)0xFFFF);
		gen_or(b0, b1);

		/*
		 * Now we add code to check for SNAP frames with
		 * ETHERTYPE_IPX, i.e. Ethernet_SNAP.
		 */
		b0 = gen_snap(cstate, 0x000000, ETHERTYPE_IPX);
		gen_or(b0, b1);

		/*
		 * Now we generate code to check for 802.3
		 * frames in general.
		 */
		b0 = gen_cmp_gt(cstate, OR_LINKTYPE, 0, BPF_H, ETHERMTU);
		gen_not(b0);

		/*
		 * Now add the check for 802.3 frames before the
		 * check for Ethernet_802.2 and Ethernet_802.3,
		 * as those checks should only be done on 802.3
		 * frames, not on Ethernet frames.
		 */
		gen_and(b0, b1);

		/*
		 * Now add the check for Ethernet_II frames, and
		 * do that before checking for the other frame
		 * types.
		 */
		b0 = gen_cmp(cstate, OR_LINKTYPE, 0, BPF_H, (bpf_int32)ETHERTYPE_IPX);
		gen_or(b0, b1);
		return b1;

	case ETHERTYPE_ATALK:
	case ETHERTYPE_AARP:
		/*
		 * EtherTalk (AppleTalk protocols on Ethernet link
		 * layer) may use 802.2 encapsulation.
		 */

		/*
		 * Check for 802.2 encapsulation (EtherTalk phase 2?);
		 * we check for an Ethernet type field less than
		 * 1500, which means it's an 802.3 length field.
		 */
		b0 = gen_cmp_gt(cstate, OR_LINKTYPE, 0, BPF_H, ETHERMTU);
		gen_not(b0);

		/*
		 * 802.2-encapsulated ETHERTYPE_ATALK packets are
		 * SNAP packets with an organization code of
		 * 0x080007 (Apple, for Appletalk) and a protocol
		 * type of ETHERTYPE_ATALK (Appletalk).
		 *
		 * 802.2-encapsulated ETHERTYPE_AARP packets are
		 * SNAP packets with an organization code of
		 * 0x000000 (encapsulated Ethernet) and a protocol
		 * type of ETHERTYPE_AARP (Appletalk ARP).
		 */
		if (proto == ETHERTYPE_ATALK)
			b1 = gen_snap(cstate, 0x080007, ETHERTYPE_ATALK);
		else	/* proto == ETHERTYPE_AARP */
			b1 = gen_snap(cstate, 0x000000, ETHERTYPE_AARP);
		gen_and(b0, b1);

		/*
		 * Check for Ethernet encapsulation (Ethertalk
		 * phase 1?); we just check for the Ethernet
		 * protocol type.
		 */
		b0 = gen_cmp(cstate, OR_LINKTYPE, 0, BPF_H, (bpf_int32)proto);

		gen_or(b0, b1);
		return b1;

	default:
		if (proto <= ETHERMTU) {
			/*
			 * This is an LLC SAP value, so the frames
			 * that match would be 802.2 frames.
			 * Check that the frame is an 802.2 frame
			 * (i.e., that the length/type field is
			 * a length field, <= ETHERMTU) and
			 * then check the DSAP.
			 */
			b0 = gen_cmp_gt(cstate, OR_LINKTYPE, 0, BPF_H, ETHERMTU);
			gen_not(b0);
			b1 = gen_cmp(cstate, OR_LINKTYPE, 2, BPF_B, (bpf_int32)proto);
			gen_and(b0, b1);
			return b1;
		} else {
			/*
			 * This is an Ethernet type, so compare
			 * the length/type field with it (if
			 * the frame is an 802.2 frame, the length
			 * field will be <= ETHERMTU, and, as
			 * "proto" is > ETHERMTU, this test
			 * will fail and the frame won't match,
			 * which is what we want).
			 */
			return gen_cmp(cstate, OR_LINKTYPE, 0, BPF_H,
			    (bpf_int32)proto);
		}
	}
}

static struct block *
gen_loopback_linktype(compiler_state_t *cstate, int proto)
{
	/*
	 * For DLT_NULL, the link-layer header is a 32-bit word
	 * containing an AF_ value in *host* byte order, and for
	 * DLT_ENC, the link-layer header begins with a 32-bit
	 * word containing an AF_ value in host byte order.
	 *
	 * In addition, if we're reading a saved capture file,
	 * the host byte order in the capture may not be the
	 * same as the host byte order on this machine.
	 *
	 * For DLT_LOOP, the link-layer header is a 32-bit
	 * word containing an AF_ value in *network* byte order.
	 */
	if (cstate->linktype == DLT_NULL || cstate->linktype == DLT_ENC) {
		/*
		 * The AF_ value is in host byte order, but the BPF
		 * interpreter will convert it to network byte order.
		 *
		 * If this is a save file, and it's from a machine
		 * with the opposite byte order to ours, we byte-swap
		 * the AF_ value.
		 *
		 * Then we run it through "htonl()", and generate
		 * code to compare against the result.
		 */
		if (cstate->bpf_pcap->rfile != NULL && cstate->bpf_pcap->swapped)
			proto = SWAPLONG(proto);
		proto = htonl(proto);
	}
	return (gen_cmp(cstate, OR_LINKHDR, 0, BPF_W, (bpf_int32)proto));
}

/*
 * "proto" is an Ethernet type value and for IPNET, if it is not IPv4
 * or IPv6 then we have an error.
 */
static struct block *
gen_ipnet_linktype(compiler_state_t *cstate, int proto)
{
	switch (proto) {

	case ETHERTYPE_IP:
		return gen_cmp(cstate, OR_LINKTYPE, 0, BPF_B, (bpf_int32)IPH_AF_INET);
		/* NOTREACHED */

	case ETHERTYPE_IPV6:
		return gen_cmp(cstate, OR_LINKTYPE, 0, BPF_B,
		    (bpf_int32)IPH_AF_INET6);
		/* NOTREACHED */

	default:
		break;
	}

	return gen_false(cstate);
}

/*
 * Generate code to match a particular packet type.
 *
 * "proto" is an Ethernet type value, if > ETHERMTU, or an LLC SAP
 * value, if <= ETHERMTU.  We use that to determine whether to
 * match the type field or to check the type field for the special
 * LINUX_SLL_P_802_2 value and then do the appropriate test.
 */
static struct block *
gen_linux_sll_linktype(compiler_state_t *cstate, int proto)
{
	struct block *b0, *b1;

	switch (proto) {

	case LLCSAP_ISONS:
	case LLCSAP_IP:
	case LLCSAP_NETBEUI:
		/*
		 * OSI protocols and NetBEUI always use 802.2 encapsulation,
		 * so we check the DSAP and SSAP.
		 *
		 * LLCSAP_IP checks for IP-over-802.2, rather
		 * than IP-over-Ethernet or IP-over-SNAP.
		 *
		 * XXX - should we check both the DSAP and the
		 * SSAP, like this, or should we check just the
		 * DSAP, as we do for other types <= ETHERMTU
		 * (i.e., other SAP values)?
		 */
		b0 = gen_cmp(cstate, OR_LINKTYPE, 0, BPF_H, LINUX_SLL_P_802_2);
		b1 = gen_cmp(cstate, OR_LLC, 0, BPF_H, (bpf_int32)
			     ((proto << 8) | proto));
		gen_and(b0, b1);
		return b1;

	case LLCSAP_IPX:
		/*
		 *	Ethernet_II frames, which are Ethernet
		 *	frames with a frame type of ETHERTYPE_IPX;
		 *
		 *	Ethernet_802.3 frames, which have a frame
		 *	type of LINUX_SLL_P_802_3;
		 *
		 *	Ethernet_802.2 frames, which are 802.3
		 *	frames with an 802.2 LLC header (i.e, have
		 *	a frame type of LINUX_SLL_P_802_2) and
		 *	with the IPX LSAP as the DSAP in the LLC
		 *	header;
		 *
		 *	Ethernet_SNAP frames, which are 802.3
		 *	frames with an LLC header and a SNAP
		 *	header and with an OUI of 0x000000
		 *	(encapsulated Ethernet) and a protocol
		 *	ID of ETHERTYPE_IPX in the SNAP header.
		 *
		 * First, do the checks on LINUX_SLL_P_802_2
		 * frames; generate the check for either
		 * Ethernet_802.2 or Ethernet_SNAP frames, and
		 * then put a check for LINUX_SLL_P_802_2 frames
		 * before it.
		 */
		b0 = gen_cmp(cstate, OR_LLC, 0, BPF_B, (bpf_int32)LLCSAP_IPX);
		b1 = gen_snap(cstate, 0x000000, ETHERTYPE_IPX);
		gen_or(b0, b1);
		b0 = gen_cmp(cstate, OR_LINKTYPE, 0, BPF_H, LINUX_SLL_P_802_2);
		gen_and(b0, b1);

		/*
		 * Now check for 802.3 frames and OR that with
		 * the previous test.
		 */
		b0 = gen_cmp(cstate, OR_LINKTYPE, 0, BPF_H, LINUX_SLL_P_802_3);
		gen_or(b0, b1);

		/*
		 * Now add the check for Ethernet_II frames, and
		 * do that before checking for the other frame
		 * types.
		 */
		b0 = gen_cmp(cstate, OR_LINKTYPE, 0, BPF_H, (bpf_int32)ETHERTYPE_IPX);
		gen_or(b0, b1);
		return b1;

	case ETHERTYPE_ATALK:
	case ETHERTYPE_AARP:
		/*
		 * EtherTalk (AppleTalk protocols on Ethernet link
		 * layer) may use 802.2 encapsulation.
		 */

		/*
		 * Check for 802.2 encapsulation (EtherTalk phase 2?);
		 * we check for the 802.2 protocol type in the
		 * "Ethernet type" field.
		 */
		b0 = gen_cmp(cstate, OR_LINKTYPE, 0, BPF_H, LINUX_SLL_P_802_2);

		/*
		 * 802.2-encapsulated ETHERTYPE_ATALK packets are
		 * SNAP packets with an organization code of
		 * 0x080007 (Apple, for Appletalk) and a protocol
		 * type of ETHERTYPE_ATALK (Appletalk).
		 *
		 * 802.2-encapsulated ETHERTYPE_AARP packets are
		 * SNAP packets with an organization code of
		 * 0x000000 (encapsulated Ethernet) and a protocol
		 * type of ETHERTYPE_AARP (Appletalk ARP).
		 */
		if (proto == ETHERTYPE_ATALK)
			b1 = gen_snap(cstate, 0x080007, ETHERTYPE_ATALK);
		else	/* proto == ETHERTYPE_AARP */
			b1 = gen_snap(cstate, 0x000000, ETHERTYPE_AARP);
		gen_and(b0, b1);

		/*
		 * Check for Ethernet encapsulation (Ethertalk
		 * phase 1?); we just check for the Ethernet
		 * protocol type.
		 */
		b0 = gen_cmp(cstate, OR_LINKTYPE, 0, BPF_H, (bpf_int32)proto);

		gen_or(b0, b1);
		return b1;

	default:
		if (proto <= ETHERMTU) {
			/*
			 * This is an LLC SAP value, so the frames
			 * that match would be 802.2 frames.
			 * Check for the 802.2 protocol type
			 * in the "Ethernet type" field, and
			 * then check the DSAP.
			 */
			b0 = gen_cmp(cstate, OR_LINKTYPE, 0, BPF_H, LINUX_SLL_P_802_2);
			b1 = gen_cmp(cstate, OR_LINKHDR, cstate->off_linkpl.constant_part, BPF_B,
			     (bpf_int32)proto);
			gen_and(b0, b1);
			return b1;
		} else {
			/*
			 * This is an Ethernet type, so compare
			 * the length/type field with it (if
			 * the frame is an 802.2 frame, the length
			 * field will be <= ETHERMTU, and, as
			 * "proto" is > ETHERMTU, this test
			 * will fail and the frame won't match,
			 * which is what we want).
			 */
			return gen_cmp(cstate, OR_LINKTYPE, 0, BPF_H, (bpf_int32)proto);
		}
	}
}

static struct slist *
gen_load_prism_llprefixlen(compiler_state_t *cstate)
{
	struct slist *s1, *s2;
	struct slist *sjeq_avs_cookie;
	struct slist *sjcommon;

	/*
	 * This code is not compatible with the optimizer, as
	 * we are generating jmp instructions within a normal
	 * slist of instructions
	 */
	cstate->no_optimize = 1;

	/*
	 * Generate code to load the length of the radio header into
	 * the register assigned to hold that length, if one has been
	 * assigned.  (If one hasn't been assigned, no code we've
	 * generated uses that prefix, so we don't need to generate any
	 * code to load it.)
	 *
	 * Some Linux drivers use ARPHRD_IEEE80211_PRISM but sometimes
	 * or always use the AVS header rather than the Prism header.
	 * We load a 4-byte big-endian value at the beginning of the
	 * raw packet data, and see whether, when masked with 0xFFFFF000,
	 * it's equal to 0x80211000.  If so, that indicates that it's
	 * an AVS header (the masked-out bits are the version number).
	 * Otherwise, it's a Prism header.
	 *
	 * XXX - the Prism header is also, in theory, variable-length,
	 * but no known software generates headers that aren't 144
	 * bytes long.
	 */
	if (cstate->off_linkhdr.reg != -1) {
		/*
		 * Load the cookie.
		 */
		s1 = new_stmt(cstate, BPF_LD|BPF_W|BPF_ABS);
		s1->s.k = 0;

		/*
		 * AND it with 0xFFFFF000.
		 */
		s2 = new_stmt(cstate, BPF_ALU|BPF_AND|BPF_K);
		s2->s.k = 0xFFFFF000;
		sappend(s1, s2);

		/*
		 * Compare with 0x80211000.
		 */
		sjeq_avs_cookie = new_stmt(cstate, JMP(BPF_JEQ));
		sjeq_avs_cookie->s.k = 0x80211000;
		sappend(s1, sjeq_avs_cookie);

		/*
		 * If it's AVS:
		 *
		 * The 4 bytes at an offset of 4 from the beginning of
		 * the AVS header are the length of the AVS header.
		 * That field is big-endian.
		 */
		s2 = new_stmt(cstate, BPF_LD|BPF_W|BPF_ABS);
		s2->s.k = 4;
		sappend(s1, s2);
		sjeq_avs_cookie->s.jt = s2;

		/*
		 * Now jump to the code to allocate a register
		 * into which to save the header length and
		 * store the length there.  (The "jump always"
		 * instruction needs to have the k field set;
		 * it's added to the PC, so, as we're jumping
		 * over a single instruction, it should be 1.)
		 */
		sjcommon = new_stmt(cstate, JMP(BPF_JA));
		sjcommon->s.k = 1;
		sappend(s1, sjcommon);

		/*
		 * Now for the code that handles the Prism header.
		 * Just load the length of the Prism header (144)
		 * into the A register.  Have the test for an AVS
		 * header branch here if we don't have an AVS header.
		 */
		s2 = new_stmt(cstate, BPF_LD|BPF_W|BPF_IMM);
		s2->s.k = 144;
		sappend(s1, s2);
		sjeq_avs_cookie->s.jf = s2;

		/*
		 * Now allocate a register to hold that value and store
		 * it.  The code for the AVS header will jump here after
		 * loading the length of the AVS header.
		 */
		s2 = new_stmt(cstate, BPF_ST);
		s2->s.k = cstate->off_linkhdr.reg;
		sappend(s1, s2);
		sjcommon->s.jf = s2;

		/*
		 * Now move it into the X register.
		 */
		s2 = new_stmt(cstate, BPF_MISC|BPF_TAX);
		sappend(s1, s2);

		return (s1);
	} else
		return (NULL);
}

static struct slist *
gen_load_avs_llprefixlen(compiler_state_t *cstate)
{
	struct slist *s1, *s2;

	/*
	 * Generate code to load the length of the AVS header into
	 * the register assigned to hold that length, if one has been
	 * assigned.  (If one hasn't been assigned, no code we've
	 * generated uses that prefix, so we don't need to generate any
	 * code to load it.)
	 */
	if (cstate->off_linkhdr.reg != -1) {
		/*
		 * The 4 bytes at an offset of 4 from the beginning of
		 * the AVS header are the length of the AVS header.
		 * That field is big-endian.
		 */
		s1 = new_stmt(cstate, BPF_LD|BPF_W|BPF_ABS);
		s1->s.k = 4;

		/*
		 * Now allocate a register to hold that value and store
		 * it.
		 */
		s2 = new_stmt(cstate, BPF_ST);
		s2->s.k = cstate->off_linkhdr.reg;
		sappend(s1, s2);

		/*
		 * Now move it into the X register.
		 */
		s2 = new_stmt(cstate, BPF_MISC|BPF_TAX);
		sappend(s1, s2);

		return (s1);
	} else
		return (NULL);
}

static struct slist *
gen_load_radiotap_llprefixlen(compiler_state_t *cstate)
{
	struct slist *s1, *s2;

	/*
	 * Generate code to load the length of the radiotap header into
	 * the register assigned to hold that length, if one has been
	 * assigned.  (If one hasn't been assigned, no code we've
	 * generated uses that prefix, so we don't need to generate any
	 * code to load it.)
	 */
	if (cstate->off_linkhdr.reg != -1) {
		/*
		 * The 2 bytes at offsets of 2 and 3 from the beginning
		 * of the radiotap header are the length of the radiotap
		 * header; unfortunately, it's little-endian, so we have
		 * to load it a byte at a time and construct the value.
		 */

		/*
		 * Load the high-order byte, at an offset of 3, shift it
		 * left a byte, and put the result in the X register.
		 */
		s1 = new_stmt(cstate, BPF_LD|BPF_B|BPF_ABS);
		s1->s.k = 3;
		s2 = new_stmt(cstate, BPF_ALU|BPF_LSH|BPF_K);
		sappend(s1, s2);
		s2->s.k = 8;
		s2 = new_stmt(cstate, BPF_MISC|BPF_TAX);
		sappend(s1, s2);

		/*
		 * Load the next byte, at an offset of 2, and OR the
		 * value from the X register into it.
		 */
		s2 = new_stmt(cstate, BPF_LD|BPF_B|BPF_ABS);
		sappend(s1, s2);
		s2->s.k = 2;
		s2 = new_stmt(cstate, BPF_ALU|BPF_OR|BPF_X);
		sappend(s1, s2);

		/*
		 * Now allocate a register to hold that value and store
		 * it.
		 */
		s2 = new_stmt(cstate, BPF_ST);
		s2->s.k = cstate->off_linkhdr.reg;
		sappend(s1, s2);

		/*
		 * Now move it into the X register.
		 */
		s2 = new_stmt(cstate, BPF_MISC|BPF_TAX);
		sappend(s1, s2);

		return (s1);
	} else
		return (NULL);
}

/*
 * At the moment we treat PPI as normal Radiotap encoded
 * packets. The difference is in the function that generates
 * the code at the beginning to compute the header length.
 * Since this code generator of PPI supports bare 802.11
 * encapsulation only (i.e. the encapsulated DLT should be
 * DLT_IEEE802_11) we generate code to check for this too;
 * that's done in finish_parse().
 */
static struct slist *
gen_load_ppi_llprefixlen(compiler_state_t *cstate)
{
	struct slist *s1, *s2;

	/*
	 * Generate code to load the length of the radiotap header
	 * into the register assigned to hold that length, if one has
	 * been assigned.
	 */
	if (cstate->off_linkhdr.reg != -1) {
		/*
		 * The 2 bytes at offsets of 2 and 3 from the beginning
		 * of the radiotap header are the length of the radiotap
		 * header; unfortunately, it's little-endian, so we have
		 * to load it a byte at a time and construct the value.
		 */

		/*
		 * Load the high-order byte, at an offset of 3, shift it
		 * left a byte, and put the result in the X register.
		 */
		s1 = new_stmt(cstate, BPF_LD|BPF_B|BPF_ABS);
		s1->s.k = 3;
		s2 = new_stmt(cstate, BPF_ALU|BPF_LSH|BPF_K);
		sappend(s1, s2);
		s2->s.k = 8;
		s2 = new_stmt(cstate, BPF_MISC|BPF_TAX);
		sappend(s1, s2);

		/*
		 * Load the next byte, at an offset of 2, and OR the
		 * value from the X register into it.
		 */
		s2 = new_stmt(cstate, BPF_LD|BPF_B|BPF_ABS);
		sappend(s1, s2);
		s2->s.k = 2;
		s2 = new_stmt(cstate, BPF_ALU|BPF_OR|BPF_X);
		sappend(s1, s2);

		/*
		 * Now allocate a register to hold that value and store
		 * it.
		 */
		s2 = new_stmt(cstate, BPF_ST);
		s2->s.k = cstate->off_linkhdr.reg;
		sappend(s1, s2);

		/*
		 * Now move it into the X register.
		 */
		s2 = new_stmt(cstate, BPF_MISC|BPF_TAX);
		sappend(s1, s2);

		return (s1);
	} else
		return (NULL);
}

/*
 * Load a value relative to the beginning of the link-layer header after the 802.11
 * header, i.e. LLC_SNAP.
 * The link-layer header doesn't necessarily begin at the beginning
 * of the packet data; there might be a variable-length prefix containing
 * radio information.
 */
static struct slist *
gen_load_802_11_header_len(compiler_state_t *cstate, struct slist *s, struct slist *snext)
{
	struct slist *s2;
	struct slist *sjset_data_frame_1;
	struct slist *sjset_data_frame_2;
	struct slist *sjset_qos;
	struct slist *sjset_radiotap_flags_present;
	struct slist *sjset_radiotap_ext_present;
	struct slist *sjset_radiotap_tsft_present;
	struct slist *sjset_tsft_datapad, *sjset_notsft_datapad;
	struct slist *s_roundup;

	if (cstate->off_linkpl.reg == -1) {
		/*
		 * No register has been assigned to the offset of
		 * the link-layer payload, which means nobody needs
		 * it; don't bother computing it - just return
		 * what we already have.
		 */
		return (s);
	}

	/*
	 * This code is not compatible with the optimizer, as
	 * we are generating jmp instructions within a normal
	 * slist of instructions
	 */
	cstate->no_optimize = 1;

	/*
	 * If "s" is non-null, it has code to arrange that the X register
	 * contains the length of the prefix preceding the link-layer
	 * header.
	 *
	 * Otherwise, the length of the prefix preceding the link-layer
	 * header is "off_outermostlinkhdr.constant_part".
	 */
	if (s == NULL) {
		/*
		 * There is no variable-length header preceding the
		 * link-layer header.
		 *
		 * Load the length of the fixed-length prefix preceding
		 * the link-layer header (if any) into the X register,
		 * and store it in the cstate->off_linkpl.reg register.
		 * That length is off_outermostlinkhdr.constant_part.
		 */
		s = new_stmt(cstate, BPF_LDX|BPF_IMM);
		s->s.k = cstate->off_outermostlinkhdr.constant_part;
	}

	/*
	 * The X register contains the offset of the beginning of the
	 * link-layer header; add 24, which is the minimum length
	 * of the MAC header for a data frame, to that, and store it
	 * in cstate->off_linkpl.reg, and then load the Frame Control field,
	 * which is at the offset in the X register, with an indexed load.
	 */
	s2 = new_stmt(cstate, BPF_MISC|BPF_TXA);
	sappend(s, s2);
	s2 = new_stmt(cstate, BPF_ALU|BPF_ADD|BPF_K);
	s2->s.k = 24;
	sappend(s, s2);
	s2 = new_stmt(cstate, BPF_ST);
	s2->s.k = cstate->off_linkpl.reg;
	sappend(s, s2);

	s2 = new_stmt(cstate, BPF_LD|BPF_IND|BPF_B);
	s2->s.k = 0;
	sappend(s, s2);

	/*
	 * Check the Frame Control field to see if this is a data frame;
	 * a data frame has the 0x08 bit (b3) in that field set and the
	 * 0x04 bit (b2) clear.
	 */
	sjset_data_frame_1 = new_stmt(cstate, JMP(BPF_JSET));
	sjset_data_frame_1->s.k = 0x08;
	sappend(s, sjset_data_frame_1);

	/*
	 * If b3 is set, test b2, otherwise go to the first statement of
	 * the rest of the program.
	 */
	sjset_data_frame_1->s.jt = sjset_data_frame_2 = new_stmt(cstate, JMP(BPF_JSET));
	sjset_data_frame_2->s.k = 0x04;
	sappend(s, sjset_data_frame_2);
	sjset_data_frame_1->s.jf = snext;

	/*
	 * If b2 is not set, this is a data frame; test the QoS bit.
	 * Otherwise, go to the first statement of the rest of the
	 * program.
	 */
	sjset_data_frame_2->s.jt = snext;
	sjset_data_frame_2->s.jf = sjset_qos = new_stmt(cstate, JMP(BPF_JSET));
	sjset_qos->s.k = 0x80;	/* QoS bit */
	sappend(s, sjset_qos);

	/*
	 * If it's set, add 2 to cstate->off_linkpl.reg, to skip the QoS
	 * field.
	 * Otherwise, go to the first statement of the rest of the
	 * program.
	 */
	sjset_qos->s.jt = s2 = new_stmt(cstate, BPF_LD|BPF_MEM);
	s2->s.k = cstate->off_linkpl.reg;
	sappend(s, s2);
	s2 = new_stmt(cstate, BPF_ALU|BPF_ADD|BPF_IMM);
	s2->s.k = 2;
	sappend(s, s2);
	s2 = new_stmt(cstate, BPF_ST);
	s2->s.k = cstate->off_linkpl.reg;
	sappend(s, s2);

	/*
	 * If we have a radiotap header, look at it to see whether
	 * there's Atheros padding between the MAC-layer header
	 * and the payload.
	 *
	 * Note: all of the fields in the radiotap header are
	 * little-endian, so we byte-swap all of the values
	 * we test against, as they will be loaded as big-endian
	 * values.
	 *
	 * XXX - in the general case, we would have to scan through
	 * *all* the presence bits, if there's more than one word of
	 * presence bits.  That would require a loop, meaning that
	 * we wouldn't be able to run the filter in the kernel.
	 *
	 * We assume here that the Atheros adapters that insert the
	 * annoying padding don't have multiple antennae and therefore
	 * do not generate radiotap headers with multiple presence words.
	 */
	if (cstate->linktype == DLT_IEEE802_11_RADIO) {
		/*
		 * Is the IEEE80211_RADIOTAP_FLAGS bit (0x0000002) set
		 * in the first presence flag word?
		 */
		sjset_qos->s.jf = s2 = new_stmt(cstate, BPF_LD|BPF_ABS|BPF_W);
		s2->s.k = 4;
		sappend(s, s2);

		sjset_radiotap_flags_present = new_stmt(cstate, JMP(BPF_JSET));
		sjset_radiotap_flags_present->s.k = SWAPLONG(0x00000002);
		sappend(s, sjset_radiotap_flags_present);

		/*
		 * If not, skip all of this.
		 */
		sjset_radiotap_flags_present->s.jf = snext;

		/*
		 * Otherwise, is the "extension" bit set in that word?
		 */
		sjset_radiotap_ext_present = new_stmt(cstate, JMP(BPF_JSET));
		sjset_radiotap_ext_present->s.k = SWAPLONG(0x80000000);
		sappend(s, sjset_radiotap_ext_present);
		sjset_radiotap_flags_present->s.jt = sjset_radiotap_ext_present;

		/*
		 * If so, skip all of this.
		 */
		sjset_radiotap_ext_present->s.jt = snext;

		/*
		 * Otherwise, is the IEEE80211_RADIOTAP_TSFT bit set?
		 */
		sjset_radiotap_tsft_present = new_stmt(cstate, JMP(BPF_JSET));
		sjset_radiotap_tsft_present->s.k = SWAPLONG(0x00000001);
		sappend(s, sjset_radiotap_tsft_present);
		sjset_radiotap_ext_present->s.jf = sjset_radiotap_tsft_present;

		/*
		 * If IEEE80211_RADIOTAP_TSFT is set, the flags field is
		 * at an offset of 16 from the beginning of the raw packet
		 * data (8 bytes for the radiotap header and 8 bytes for
		 * the TSFT field).
		 *
		 * Test whether the IEEE80211_RADIOTAP_F_DATAPAD bit (0x20)
		 * is set.
		 */
		s2 = new_stmt(cstate, BPF_LD|BPF_ABS|BPF_B);
		s2->s.k = 16;
		sappend(s, s2);
		sjset_radiotap_tsft_present->s.jt = s2;

		sjset_tsft_datapad = new_stmt(cstate, JMP(BPF_JSET));
		sjset_tsft_datapad->s.k = 0x20;
		sappend(s, sjset_tsft_datapad);

		/*
		 * If IEEE80211_RADIOTAP_TSFT is not set, the flags field is
		 * at an offset of 8 from the beginning of the raw packet
		 * data (8 bytes for the radiotap header).
		 *
		 * Test whether the IEEE80211_RADIOTAP_F_DATAPAD bit (0x20)
		 * is set.
		 */
		s2 = new_stmt(cstate, BPF_LD|BPF_ABS|BPF_B);
		s2->s.k = 8;
		sappend(s, s2);
		sjset_radiotap_tsft_present->s.jf = s2;

		sjset_notsft_datapad = new_stmt(cstate, JMP(BPF_JSET));
		sjset_notsft_datapad->s.k = 0x20;
		sappend(s, sjset_notsft_datapad);

		/*
		 * In either case, if IEEE80211_RADIOTAP_F_DATAPAD is
		 * set, round the length of the 802.11 header to
		 * a multiple of 4.  Do that by adding 3 and then
		 * dividing by and multiplying by 4, which we do by
		 * ANDing with ~3.
		 */
		s_roundup = new_stmt(cstate, BPF_LD|BPF_MEM);
		s_roundup->s.k = cstate->off_linkpl.reg;
		sappend(s, s_roundup);
		s2 = new_stmt(cstate, BPF_ALU|BPF_ADD|BPF_IMM);
		s2->s.k = 3;
		sappend(s, s2);
		s2 = new_stmt(cstate, BPF_ALU|BPF_AND|BPF_IMM);
		s2->s.k = ~3;
		sappend(s, s2);
		s2 = new_stmt(cstate, BPF_ST);
		s2->s.k = cstate->off_linkpl.reg;
		sappend(s, s2);

		sjset_tsft_datapad->s.jt = s_roundup;
		sjset_tsft_datapad->s.jf = snext;
		sjset_notsft_datapad->s.jt = s_roundup;
		sjset_notsft_datapad->s.jf = snext;
	} else
		sjset_qos->s.jf = snext;

	return s;
}

static void
insert_compute_vloffsets(compiler_state_t *cstate, struct block *b)
{
	struct slist *s;

	/* There is an implicit dependency between the link
	 * payload and link header since the payload computation
	 * includes the variable part of the header. Therefore,
	 * if nobody else has allocated a register for the link
	 * header and we need it, do it now. */
	if (cstate->off_linkpl.reg != -1 && cstate->off_linkhdr.is_variable &&
	    cstate->off_linkhdr.reg == -1)
		cstate->off_linkhdr.reg = alloc_reg(cstate);

	/*
	 * For link-layer types that have a variable-length header
	 * preceding the link-layer header, generate code to load
	 * the offset of the link-layer header into the register
	 * assigned to that offset, if any.
	 *
	 * XXX - this, and the next switch statement, won't handle
	 * encapsulation of 802.11 or 802.11+radio information in
	 * some other protocol stack.  That's significantly more
	 * complicated.
	 */
	switch (cstate->outermostlinktype) {

	case DLT_PRISM_HEADER:
		s = gen_load_prism_llprefixlen(cstate);
		break;

	case DLT_IEEE802_11_RADIO_AVS:
		s = gen_load_avs_llprefixlen(cstate);
		break;

	case DLT_IEEE802_11_RADIO:
		s = gen_load_radiotap_llprefixlen(cstate);
		break;

	case DLT_PPI:
		s = gen_load_ppi_llprefixlen(cstate);
		break;

	default:
		s = NULL;
		break;
	}

	/*
	 * For link-layer types that have a variable-length link-layer
	 * header, generate code to load the offset of the link-layer
	 * payload into the register assigned to that offset, if any.
	 */
	switch (cstate->outermostlinktype) {

	case DLT_IEEE802_11:
	case DLT_PRISM_HEADER:
	case DLT_IEEE802_11_RADIO_AVS:
	case DLT_IEEE802_11_RADIO:
	case DLT_PPI:
		s = gen_load_802_11_header_len(cstate, s, b->stmts);
		break;
	}

	/*
	 * If there there is no initialization yet and we need variable
	 * length offsets for VLAN, initialize them to zero
	 */
	if (s == NULL && cstate->is_vlan_vloffset) {
		struct slist *s2;

		if (cstate->off_linkpl.reg == -1)
			cstate->off_linkpl.reg = alloc_reg(cstate);
		if (cstate->off_linktype.reg == -1)
			cstate->off_linktype.reg = alloc_reg(cstate);

		s = new_stmt(cstate, BPF_LD|BPF_W|BPF_IMM);
		s->s.k = 0;
		s2 = new_stmt(cstate, BPF_ST);
		s2->s.k = cstate->off_linkpl.reg;
		sappend(s, s2);
		s2 = new_stmt(cstate, BPF_ST);
		s2->s.k = cstate->off_linktype.reg;
		sappend(s, s2);
	}

	/*
	 * If we have any offset-loading code, append all the
	 * existing statements in the block to those statements,
	 * and make the resulting list the list of statements
	 * for the block.
	 */
	if (s != NULL) {
		sappend(s, b->stmts);
		b->stmts = s;
	}
}

static struct block *
gen_ppi_dlt_check(compiler_state_t *cstate)
{
	struct slist *s_load_dlt;
	struct block *b;

	if (cstate->linktype == DLT_PPI)
	{
		/* Create the statements that check for the DLT
		 */
		s_load_dlt = new_stmt(cstate, BPF_LD|BPF_W|BPF_ABS);
		s_load_dlt->s.k = 4;

		b = new_block(cstate, JMP(BPF_JEQ));

		b->stmts = s_load_dlt;
		b->s.k = SWAPLONG(DLT_IEEE802_11);
	}
	else
	{
		b = NULL;
	}

	return b;
}

/*
 * Take an absolute offset, and:
 *
 *    if it has no variable part, return NULL;
 *
 *    if it has a variable part, generate code to load the register
 *    containing that variable part into the X register, returning
 *    a pointer to that code - if no register for that offset has
 *    been allocated, allocate it first.
 *
 * (The code to set that register will be generated later, but will
 * be placed earlier in the code sequence.)
 */
static struct slist *
gen_abs_offset_varpart(compiler_state_t *cstate, bpf_abs_offset *off)
{
	struct slist *s;

	if (off->is_variable) {
		if (off->reg == -1) {
			/*
			 * We haven't yet assigned a register for the
			 * variable part of the offset of the link-layer
			 * header; allocate one.
			 */
			off->reg = alloc_reg(cstate);
		}

		/*
		 * Load the register containing the variable part of the
		 * offset of the link-layer header into the X register.
		 */
		s = new_stmt(cstate, BPF_LDX|BPF_MEM);
		s->s.k = off->reg;
		return s;
	} else {
		/*
		 * That offset isn't variable, there's no variable part,
		 * so we don't need to generate any code.
		 */
		return NULL;
	}
}

/*
 * Map an Ethernet type to the equivalent PPP type.
 */
static int
ethertype_to_ppptype(int proto)
{
	switch (proto) {

	case ETHERTYPE_IP:
		proto = PPP_IP;
		break;

	case ETHERTYPE_IPV6:
		proto = PPP_IPV6;
		break;

	case ETHERTYPE_DN:
		proto = PPP_DECNET;
		break;

	case ETHERTYPE_ATALK:
		proto = PPP_APPLE;
		break;

	case ETHERTYPE_NS:
		proto = PPP_NS;
		break;

	case LLCSAP_ISONS:
		proto = PPP_OSI;
		break;

	case LLCSAP_8021D:
		/*
		 * I'm assuming the "Bridging PDU"s that go
		 * over PPP are Spanning Tree Protocol
		 * Bridging PDUs.
		 */
		proto = PPP_BRPDU;
		break;

	case LLCSAP_IPX:
		proto = PPP_IPX;
		break;
	}
	return (proto);
}

/*
 * Generate any tests that, for encapsulation of a link-layer packet
 * inside another protocol stack, need to be done to check for those
 * link-layer packets (and that haven't already been done by a check
 * for that encapsulation).
 */
static struct block *
gen_prevlinkhdr_check(compiler_state_t *cstate)
{
	struct block *b0;

	if (cstate->is_geneve)
		return gen_geneve_ll_check(cstate);

	switch (cstate->prevlinktype) {

	case DLT_SUNATM:
		/*
		 * This is LANE-encapsulated Ethernet; check that the LANE
		 * packet doesn't begin with an LE Control marker, i.e.
		 * that it's data, not a control message.
		 *
		 * (We've already generated a test for LANE.)
		 */
		b0 = gen_cmp(cstate, OR_PREVLINKHDR, SUNATM_PKT_BEGIN_POS, BPF_H, 0xFF00);
		gen_not(b0);
		return b0;

	default:
		/*
		 * No such tests are necessary.
		 */
		return NULL;
	}
	/*NOTREACHED*/
}

/*
 * The three different values we should check for when checking for an
 * IPv6 packet with DLT_NULL.
 */
#define BSD_AFNUM_INET6_BSD	24	/* NetBSD, OpenBSD, BSD/OS, Npcap */
#define BSD_AFNUM_INET6_FREEBSD	28	/* FreeBSD */
#define BSD_AFNUM_INET6_DARWIN	30	/* macOS, iOS, other Darwin-based OSes */

/*
 * Generate code to match a particular packet type by matching the
 * link-layer type field or fields in the 802.2 LLC header.
 *
 * "proto" is an Ethernet type value, if > ETHERMTU, or an LLC SAP
 * value, if <= ETHERMTU.
 */
static struct block *
gen_linktype(compiler_state_t *cstate, int proto)
{
	struct block *b0, *b1, *b2;
	const char *description;

	/* are we checking MPLS-encapsulated packets? */
	if (cstate->label_stack_depth > 0) {
		switch (proto) {
		case ETHERTYPE_IP:
		case PPP_IP:
			/* FIXME add other L3 proto IDs */
			return gen_mpls_linktype(cstate, Q_IP);

		case ETHERTYPE_IPV6:
		case PPP_IPV6:
			/* FIXME add other L3 proto IDs */
			return gen_mpls_linktype(cstate, Q_IPV6);

		default:
			bpf_error(cstate, "unsupported protocol over mpls");
			/* NOTREACHED */
		}
	}

	switch (cstate->linktype) {

	case DLT_EN10MB:
	case DLT_NETANALYZER:
	case DLT_NETANALYZER_TRANSPARENT:
		/* Geneve has an EtherType regardless of whether there is an
		 * L2 header. */
		if (!cstate->is_geneve)
			b0 = gen_prevlinkhdr_check(cstate);
		else
			b0 = NULL;

		b1 = gen_ether_linktype(cstate, proto);
		if (b0 != NULL)
			gen_and(b0, b1);
		return b1;
		/*NOTREACHED*/
		break;

	case DLT_C_HDLC:
		switch (proto) {

		case LLCSAP_ISONS:
			proto = (proto << 8 | LLCSAP_ISONS);
			/* fall through */

		default:
			return gen_cmp(cstate, OR_LINKTYPE, 0, BPF_H, (bpf_int32)proto);
			/*NOTREACHED*/
			break;
		}
		break;

	case DLT_IEEE802_11:
	case DLT_PRISM_HEADER:
	case DLT_IEEE802_11_RADIO_AVS:
	case DLT_IEEE802_11_RADIO:
	case DLT_PPI:
		/*
		 * Check that we have a data frame.
		 */
		b0 = gen_check_802_11_data_frame(cstate);

		/*
		 * Now check for the specified link-layer type.
		 */
		b1 = gen_llc_linktype(cstate, proto);
		gen_and(b0, b1);
		return b1;
		/*NOTREACHED*/
		break;

	case DLT_FDDI:
		/*
		 * XXX - check for LLC frames.
		 */
		return gen_llc_linktype(cstate, proto);
		/*NOTREACHED*/
		break;

	case DLT_IEEE802:
		/*
		 * XXX - check for LLC PDUs, as per IEEE 802.5.
		 */
		return gen_llc_linktype(cstate, proto);
		/*NOTREACHED*/
		break;

	case DLT_ATM_RFC1483:
	case DLT_ATM_CLIP:
	case DLT_IP_OVER_FC:
		return gen_llc_linktype(cstate, proto);
		/*NOTREACHED*/
		break;

	case DLT_SUNATM:
		/*
		 * Check for an LLC-encapsulated version of this protocol;
		 * if we were checking for LANE, linktype would no longer
		 * be DLT_SUNATM.
		 *
		 * Check for LLC encapsulation and then check the protocol.
		 */
		b0 = gen_atmfield_code(cstate, A_PROTOTYPE, PT_LLC, BPF_JEQ, 0);
		b1 = gen_llc_linktype(cstate, proto);
		gen_and(b0, b1);
		return b1;
		/*NOTREACHED*/
		break;

	case DLT_LINUX_SLL:
		return gen_linux_sll_linktype(cstate, proto);
		/*NOTREACHED*/
		break;

	case DLT_SLIP:
	case DLT_SLIP_BSDOS:
	case DLT_RAW:
		/*
		 * These types don't provide any type field; packets
		 * are always IPv4 or IPv6.
		 *
		 * XXX - for IPv4, check for a version number of 4, and,
		 * for IPv6, check for a version number of 6?
		 */
		switch (proto) {

		case ETHERTYPE_IP:
			/* Check for a version number of 4. */
			return gen_mcmp(cstate, OR_LINKHDR, 0, BPF_B, 0x40, 0xF0);

		case ETHERTYPE_IPV6:
			/* Check for a version number of 6. */
			return gen_mcmp(cstate, OR_LINKHDR, 0, BPF_B, 0x60, 0xF0);

		default:
			return gen_false(cstate);	/* always false */
		}
		/*NOTREACHED*/
		break;

	case DLT_IPV4:
		/*
		 * Raw IPv4, so no type field.
		 */
		if (proto == ETHERTYPE_IP)
			return gen_true(cstate);	/* always true */

		/* Checking for something other than IPv4; always false */
		return gen_false(cstate);
		/*NOTREACHED*/
		break;

	case DLT_IPV6:
		/*
		 * Raw IPv6, so no type field.
		 */
		if (proto == ETHERTYPE_IPV6)
			return gen_true(cstate);	/* always true */

		/* Checking for something other than IPv6; always false */
		return gen_false(cstate);
		/*NOTREACHED*/
		break;

	case DLT_PPP:
	case DLT_PPP_PPPD:
	case DLT_PPP_SERIAL:
	case DLT_PPP_ETHER:
		/*
		 * We use Ethernet protocol types inside libpcap;
		 * map them to the corresponding PPP protocol types.
		 */
		proto = ethertype_to_ppptype(proto);
		return gen_cmp(cstate, OR_LINKTYPE, 0, BPF_H, (bpf_int32)proto);
		/*NOTREACHED*/
		break;

	case DLT_PPP_BSDOS:
		/*
		 * We use Ethernet protocol types inside libpcap;
		 * map them to the corresponding PPP protocol types.
		 */
		switch (proto) {

		case ETHERTYPE_IP:
			/*
			 * Also check for Van Jacobson-compressed IP.
			 * XXX - do this for other forms of PPP?
			 */
			b0 = gen_cmp(cstate, OR_LINKTYPE, 0, BPF_H, PPP_IP);
			b1 = gen_cmp(cstate, OR_LINKTYPE, 0, BPF_H, PPP_VJC);
			gen_or(b0, b1);
			b0 = gen_cmp(cstate, OR_LINKTYPE, 0, BPF_H, PPP_VJNC);
			gen_or(b1, b0);
			return b0;

		default:
			proto = ethertype_to_ppptype(proto);
			return gen_cmp(cstate, OR_LINKTYPE, 0, BPF_H,
				(bpf_int32)proto);
		}
		/*NOTREACHED*/
		break;

	case DLT_NULL:
	case DLT_LOOP:
	case DLT_ENC:
		switch (proto) {

		case ETHERTYPE_IP:
			return (gen_loopback_linktype(cstate, AF_INET));

		case ETHERTYPE_IPV6:
			/*
			 * AF_ values may, unfortunately, be platform-
			 * dependent; AF_INET isn't, because everybody
			 * used 4.2BSD's value, but AF_INET6 is, because
			 * 4.2BSD didn't have a value for it (given that
			 * IPv6 didn't exist back in the early 1980's),
			 * and they all picked their own values.
			 *
			 * This means that, if we're reading from a
			 * savefile, we need to check for all the
			 * possible values.
			 *
			 * If we're doing a live capture, we only need
			 * to check for this platform's value; however,
			 * Npcap uses 24, which isn't Windows's AF_INET6
			 * value.  (Given the multiple different values,
			 * programs that read pcap files shouldn't be
			 * checking for their platform's AF_INET6 value
			 * anyway, they should check for all of the
			 * possible values. and they might as well do
			 * that even for live captures.)
			 */
			if (cstate->bpf_pcap->rfile != NULL) {
				/*
				 * Savefile - check for all three
				 * possible IPv6 values.
				 */
				b0 = gen_loopback_linktype(cstate, BSD_AFNUM_INET6_BSD);
				b1 = gen_loopback_linktype(cstate, BSD_AFNUM_INET6_FREEBSD);
				gen_or(b0, b1);
				b0 = gen_loopback_linktype(cstate, BSD_AFNUM_INET6_DARWIN);
				gen_or(b0, b1);
				return (b1);
			} else {
				/*
				 * Live capture, so we only need to
				 * check for the value used on this
				 * platform.
				 */
#ifdef _WIN32
				/*
				 * Npcap doesn't use Windows's AF_INET6,
				 * as that collides with AF_IPX on
				 * some BSDs (both have the value 23).
				 * Instead, it uses 24.
				 */
				return (gen_loopback_linktype(cstate, 24));
#else /* _WIN32 */
#ifdef AF_INET6
				return (gen_loopback_linktype(cstate, AF_INET6));
#else /* AF_INET6 */
				/*
				 * I guess this platform doesn't support
				 * IPv6, so we just reject all packets.
				 */
				return gen_false(cstate);
#endif /* AF_INET6 */
#endif /* _WIN32 */
			}

		default:
			/*
			 * Not a type on which we support filtering.
			 * XXX - support those that have AF_ values
			 * #defined on this platform, at least?
			 */
			return gen_false(cstate);
		}

#ifdef HAVE_NET_PFVAR_H
	case DLT_PFLOG:
		/*
		 * af field is host byte order in contrast to the rest of
		 * the packet.
		 */
		if (proto == ETHERTYPE_IP)
			return (gen_cmp(cstate, OR_LINKHDR, offsetof(struct pfloghdr, af),
			    BPF_B, (bpf_int32)AF_INET));
		else if (proto == ETHERTYPE_IPV6)
			return (gen_cmp(cstate, OR_LINKHDR, offsetof(struct pfloghdr, af),
			    BPF_B, (bpf_int32)AF_INET6));
		else
			return gen_false(cstate);
		/*NOTREACHED*/
		break;
#endif /* HAVE_NET_PFVAR_H */

	case DLT_ARCNET:
	case DLT_ARCNET_LINUX:
		/*
		 * XXX should we check for first fragment if the protocol
		 * uses PHDS?
		 */
		switch (proto) {

		default:
			return gen_false(cstate);

		case ETHERTYPE_IPV6:
			return (gen_cmp(cstate, OR_LINKTYPE, 0, BPF_B,
				(bpf_int32)ARCTYPE_INET6));

		case ETHERTYPE_IP:
			b0 = gen_cmp(cstate, OR_LINKTYPE, 0, BPF_B,
				     (bpf_int32)ARCTYPE_IP);
			b1 = gen_cmp(cstate, OR_LINKTYPE, 0, BPF_B,
				     (bpf_int32)ARCTYPE_IP_OLD);
			gen_or(b0, b1);
			return (b1);

		case ETHERTYPE_ARP:
			b0 = gen_cmp(cstate, OR_LINKTYPE, 0, BPF_B,
				     (bpf_int32)ARCTYPE_ARP);
			b1 = gen_cmp(cstate, OR_LINKTYPE, 0, BPF_B,
				     (bpf_int32)ARCTYPE_ARP_OLD);
			gen_or(b0, b1);
			return (b1);

		case ETHERTYPE_REVARP:
			return (gen_cmp(cstate, OR_LINKTYPE, 0, BPF_B,
					(bpf_int32)ARCTYPE_REVARP));

		case ETHERTYPE_ATALK:
			return (gen_cmp(cstate, OR_LINKTYPE, 0, BPF_B,
					(bpf_int32)ARCTYPE_ATALK));
		}
		/*NOTREACHED*/
		break;

	case DLT_LTALK:
		switch (proto) {
		case ETHERTYPE_ATALK:
			return gen_true(cstate);
		default:
			return gen_false(cstate);
		}
		/*NOTREACHED*/
		break;

	case DLT_FRELAY:
		/*
		 * XXX - assumes a 2-byte Frame Relay header with
		 * DLCI and flags.  What if the address is longer?
		 */
		switch (proto) {

		case ETHERTYPE_IP:
			/*
			 * Check for the special NLPID for IP.
			 */
			return gen_cmp(cstate, OR_LINKHDR, 2, BPF_H, (0x03<<8) | 0xcc);

		case ETHERTYPE_IPV6:
			/*
			 * Check for the special NLPID for IPv6.
			 */
			return gen_cmp(cstate, OR_LINKHDR, 2, BPF_H, (0x03<<8) | 0x8e);

		case LLCSAP_ISONS:
			/*
			 * Check for several OSI protocols.
			 *
			 * Frame Relay packets typically have an OSI
			 * NLPID at the beginning; we check for each
			 * of them.
			 *
			 * What we check for is the NLPID and a frame
			 * control field of UI, i.e. 0x03 followed
			 * by the NLPID.
			 */
			b0 = gen_cmp(cstate, OR_LINKHDR, 2, BPF_H, (0x03<<8) | ISO8473_CLNP);
			b1 = gen_cmp(cstate, OR_LINKHDR, 2, BPF_H, (0x03<<8) | ISO9542_ESIS);
			b2 = gen_cmp(cstate, OR_LINKHDR, 2, BPF_H, (0x03<<8) | ISO10589_ISIS);
			gen_or(b1, b2);
			gen_or(b0, b2);
			return b2;

		default:
			return gen_false(cstate);
		}
		/*NOTREACHED*/
		break;

	case DLT_MFR:
		bpf_error(cstate, "Multi-link Frame Relay link-layer type filtering not implemented");

        case DLT_JUNIPER_MFR:
        case DLT_JUNIPER_MLFR:
        case DLT_JUNIPER_MLPPP:
	case DLT_JUNIPER_ATM1:
	case DLT_JUNIPER_ATM2:
	case DLT_JUNIPER_PPPOE:
	case DLT_JUNIPER_PPPOE_ATM:
        case DLT_JUNIPER_GGSN:
        case DLT_JUNIPER_ES:
        case DLT_JUNIPER_MONITOR:
        case DLT_JUNIPER_SERVICES:
        case DLT_JUNIPER_ETHER:
        case DLT_JUNIPER_PPP:
        case DLT_JUNIPER_FRELAY:
        case DLT_JUNIPER_CHDLC:
        case DLT_JUNIPER_VP:
        case DLT_JUNIPER_ST:
        case DLT_JUNIPER_ISM:
        case DLT_JUNIPER_VS:
        case DLT_JUNIPER_SRX_E2E:
        case DLT_JUNIPER_FIBRECHANNEL:
	case DLT_JUNIPER_ATM_CEMIC:

		/* just lets verify the magic number for now -
		 * on ATM we may have up to 6 different encapsulations on the wire
		 * and need a lot of heuristics to figure out that the payload
		 * might be;
		 *
		 * FIXME encapsulation specific BPF_ filters
		 */
		return gen_mcmp(cstate, OR_LINKHDR, 0, BPF_W, 0x4d474300, 0xffffff00); /* compare the magic number */

	case DLT_BACNET_MS_TP:
		return gen_mcmp(cstate, OR_LINKHDR, 0, BPF_W, 0x55FF0000, 0xffff0000);

	case DLT_IPNET:
		return gen_ipnet_linktype(cstate, proto);

	case DLT_LINUX_IRDA:
		bpf_error(cstate, "IrDA link-layer type filtering not implemented");

	case DLT_DOCSIS:
		bpf_error(cstate, "DOCSIS link-layer type filtering not implemented");

	case DLT_MTP2:
	case DLT_MTP2_WITH_PHDR:
		bpf_error(cstate, "MTP2 link-layer type filtering not implemented");

	case DLT_ERF:
		bpf_error(cstate, "ERF link-layer type filtering not implemented");

	case DLT_PFSYNC:
		bpf_error(cstate, "PFSYNC link-layer type filtering not implemented");

	case DLT_LINUX_LAPD:
		bpf_error(cstate, "LAPD link-layer type filtering not implemented");

	case DLT_USB_FREEBSD:
	case DLT_USB_LINUX:
	case DLT_USB_LINUX_MMAPPED:
	case DLT_USBPCAP:
		bpf_error(cstate, "USB link-layer type filtering not implemented");

	case DLT_BLUETOOTH_HCI_H4:
	case DLT_BLUETOOTH_HCI_H4_WITH_PHDR:
		bpf_error(cstate, "Bluetooth link-layer type filtering not implemented");

	case DLT_CAN20B:
	case DLT_CAN_SOCKETCAN:
		bpf_error(cstate, "CAN link-layer type filtering not implemented");

	case DLT_IEEE802_15_4:
	case DLT_IEEE802_15_4_LINUX:
	case DLT_IEEE802_15_4_NONASK_PHY:
	case DLT_IEEE802_15_4_NOFCS:
		bpf_error(cstate, "IEEE 802.15.4 link-layer type filtering not implemented");

	case DLT_IEEE802_16_MAC_CPS_RADIO:
		bpf_error(cstate, "IEEE 802.16 link-layer type filtering not implemented");

	case DLT_SITA:
		bpf_error(cstate, "SITA link-layer type filtering not implemented");

	case DLT_RAIF1:
		bpf_error(cstate, "RAIF1 link-layer type filtering not implemented");

	case DLT_IPMB:
		bpf_error(cstate, "IPMB link-layer type filtering not implemented");

	case DLT_AX25_KISS:
		bpf_error(cstate, "AX.25 link-layer type filtering not implemented");

	case DLT_NFLOG:
		/* Using the fixed-size NFLOG header it is possible to tell only
		 * the address family of the packet, other meaningful data is
		 * either missing or behind TLVs.
		 */
		bpf_error(cstate, "NFLOG link-layer type filtering not implemented");

	default:
		/*
		 * Does this link-layer header type have a field
		 * indicating the type of the next protocol?  If
		 * so, off_linktype.constant_part will be the offset of that
		 * field in the packet; if not, it will be OFFSET_NOT_SET.
		 */
		if (cstate->off_linktype.constant_part != OFFSET_NOT_SET) {
			/*
			 * Yes; assume it's an Ethernet type.  (If
			 * it's not, it needs to be handled specially
			 * above.)
			 */
			return gen_cmp(cstate, OR_LINKTYPE, 0, BPF_H, (bpf_int32)proto);
		} else {
			/*
			 * No; report an error.
			 */
			description = pcap_datalink_val_to_description(cstate->linktype);
			if (description != NULL) {
				bpf_error(cstate, "%s link-layer type filtering not implemented",
				    description);
			} else {
				bpf_error(cstate, "DLT %u link-layer type filtering not implemented",
				    cstate->linktype);
			}
		}
		break;
	}
}

/*
 * Check for an LLC SNAP packet with a given organization code and
 * protocol type; we check the entire contents of the 802.2 LLC and
 * snap headers, checking for DSAP and SSAP of SNAP and a control
 * field of 0x03 in the LLC header, and for the specified organization
 * code and protocol type in the SNAP header.
 */
static struct block *
gen_snap(compiler_state_t *cstate, bpf_u_int32 orgcode, bpf_u_int32 ptype)
{
	u_char snapblock[8];

	snapblock[0] = LLCSAP_SNAP;		/* DSAP = SNAP */
	snapblock[1] = LLCSAP_SNAP;		/* SSAP = SNAP */
	snapblock[2] = 0x03;			/* control = UI */
	snapblock[3] = (u_char)(orgcode >> 16);	/* upper 8 bits of organization code */
	snapblock[4] = (u_char)(orgcode >> 8);	/* middle 8 bits of organization code */
	snapblock[5] = (u_char)(orgcode >> 0);	/* lower 8 bits of organization code */
	snapblock[6] = (u_char)(ptype >> 8);	/* upper 8 bits of protocol type */
	snapblock[7] = (u_char)(ptype >> 0);	/* lower 8 bits of protocol type */
	return gen_bcmp(cstate, OR_LLC, 0, 8, snapblock);
}

/*
 * Generate code to match frames with an LLC header.
 */
struct block *
gen_llc(compiler_state_t *cstate)
{
	struct block *b0, *b1;

	switch (cstate->linktype) {

	case DLT_EN10MB:
		/*
		 * We check for an Ethernet type field less than
		 * 1500, which means it's an 802.3 length field.
		 */
		b0 = gen_cmp_gt(cstate, OR_LINKTYPE, 0, BPF_H, ETHERMTU);
		gen_not(b0);

		/*
		 * Now check for the purported DSAP and SSAP not being
		 * 0xFF, to rule out NetWare-over-802.3.
		 */
		b1 = gen_cmp(cstate, OR_LLC, 0, BPF_H, (bpf_int32)0xFFFF);
		gen_not(b1);
		gen_and(b0, b1);
		return b1;

	case DLT_SUNATM:
		/*
		 * We check for LLC traffic.
		 */
		b0 = gen_atmtype_abbrev(cstate, A_LLC);
		return b0;

	case DLT_IEEE802:	/* Token Ring */
		/*
		 * XXX - check for LLC frames.
		 */
		return gen_true(cstate);

	case DLT_FDDI:
		/*
		 * XXX - check for LLC frames.
		 */
		return gen_true(cstate);

	case DLT_ATM_RFC1483:
		/*
		 * For LLC encapsulation, these are defined to have an
		 * 802.2 LLC header.
		 *
		 * For VC encapsulation, they don't, but there's no
		 * way to check for that; the protocol used on the VC
		 * is negotiated out of band.
		 */
		return gen_true(cstate);

	case DLT_IEEE802_11:
	case DLT_PRISM_HEADER:
	case DLT_IEEE802_11_RADIO:
	case DLT_IEEE802_11_RADIO_AVS:
	case DLT_PPI:
		/*
		 * Check that we have a data frame.
		 */
		b0 = gen_check_802_11_data_frame(cstate);
		return b0;

	default:
		bpf_error(cstate, "'llc' not supported for linktype %d", cstate->linktype);
		/* NOTREACHED */
	}
}

struct block *
gen_llc_i(compiler_state_t *cstate)
{
	struct block *b0, *b1;
	struct slist *s;

	/*
	 * Check whether this is an LLC frame.
	 */
	b0 = gen_llc(cstate);

	/*
	 * Load the control byte and test the low-order bit; it must
	 * be clear for I frames.
	 */
	s = gen_load_a(cstate, OR_LLC, 2, BPF_B);
	b1 = new_block(cstate, JMP(BPF_JSET));
	b1->s.k = 0x01;
	b1->stmts = s;
	gen_not(b1);
	gen_and(b0, b1);
	return b1;
}

struct block *
gen_llc_s(compiler_state_t *cstate)
{
	struct block *b0, *b1;

	/*
	 * Check whether this is an LLC frame.
	 */
	b0 = gen_llc(cstate);

	/*
	 * Now compare the low-order 2 bit of the control byte against
	 * the appropriate value for S frames.
	 */
	b1 = gen_mcmp(cstate, OR_LLC, 2, BPF_B, LLC_S_FMT, 0x03);
	gen_and(b0, b1);
	return b1;
}

struct block *
gen_llc_u(compiler_state_t *cstate)
{
	struct block *b0, *b1;

	/*
	 * Check whether this is an LLC frame.
	 */
	b0 = gen_llc(cstate);

	/*
	 * Now compare the low-order 2 bit of the control byte against
	 * the appropriate value for U frames.
	 */
	b1 = gen_mcmp(cstate, OR_LLC, 2, BPF_B, LLC_U_FMT, 0x03);
	gen_and(b0, b1);
	return b1;
}

struct block *
gen_llc_s_subtype(compiler_state_t *cstate, bpf_u_int32 subtype)
{
	struct block *b0, *b1;

	/*
	 * Check whether this is an LLC frame.
	 */
	b0 = gen_llc(cstate);

	/*
	 * Now check for an S frame with the appropriate type.
	 */
	b1 = gen_mcmp(cstate, OR_LLC, 2, BPF_B, subtype, LLC_S_CMD_MASK);
	gen_and(b0, b1);
	return b1;
}

struct block *
gen_llc_u_subtype(compiler_state_t *cstate, bpf_u_int32 subtype)
{
	struct block *b0, *b1;

	/*
	 * Check whether this is an LLC frame.
	 */
	b0 = gen_llc(cstate);

	/*
	 * Now check for a U frame with the appropriate type.
	 */
	b1 = gen_mcmp(cstate, OR_LLC, 2, BPF_B, subtype, LLC_U_CMD_MASK);
	gen_and(b0, b1);
	return b1;
}

/*
 * Generate code to match a particular packet type, for link-layer types
 * using 802.2 LLC headers.
 *
 * This is *NOT* used for Ethernet; "gen_ether_linktype()" is used
 * for that - it handles the D/I/X Ethernet vs. 802.3+802.2 issues.
 *
 * "proto" is an Ethernet type value, if > ETHERMTU, or an LLC SAP
 * value, if <= ETHERMTU.  We use that to determine whether to
 * match the DSAP or both DSAP and LSAP or to check the OUI and
 * protocol ID in a SNAP header.
 */
static struct block *
gen_llc_linktype(compiler_state_t *cstate, int proto)
{
	/*
	 * XXX - handle token-ring variable-length header.
	 */
	switch (proto) {

	case LLCSAP_IP:
	case LLCSAP_ISONS:
	case LLCSAP_NETBEUI:
		/*
		 * XXX - should we check both the DSAP and the
		 * SSAP, like this, or should we check just the
		 * DSAP, as we do for other SAP values?
		 */
		return gen_cmp(cstate, OR_LLC, 0, BPF_H, (bpf_u_int32)
			     ((proto << 8) | proto));

	case LLCSAP_IPX:
		/*
		 * XXX - are there ever SNAP frames for IPX on
		 * non-Ethernet 802.x networks?
		 */
		return gen_cmp(cstate, OR_LLC, 0, BPF_B,
		    (bpf_int32)LLCSAP_IPX);

	case ETHERTYPE_ATALK:
		/*
		 * 802.2-encapsulated ETHERTYPE_ATALK packets are
		 * SNAP packets with an organization code of
		 * 0x080007 (Apple, for Appletalk) and a protocol
		 * type of ETHERTYPE_ATALK (Appletalk).
		 *
		 * XXX - check for an organization code of
		 * encapsulated Ethernet as well?
		 */
		return gen_snap(cstate, 0x080007, ETHERTYPE_ATALK);

	default:
		/*
		 * XXX - we don't have to check for IPX 802.3
		 * here, but should we check for the IPX Ethertype?
		 */
		if (proto <= ETHERMTU) {
			/*
			 * This is an LLC SAP value, so check
			 * the DSAP.
			 */
			return gen_cmp(cstate, OR_LLC, 0, BPF_B, (bpf_int32)proto);
		} else {
			/*
			 * This is an Ethernet type; we assume that it's
			 * unlikely that it'll appear in the right place
			 * at random, and therefore check only the
			 * location that would hold the Ethernet type
			 * in a SNAP frame with an organization code of
			 * 0x000000 (encapsulated Ethernet).
			 *
			 * XXX - if we were to check for the SNAP DSAP and
			 * LSAP, as per XXX, and were also to check for an
			 * organization code of 0x000000 (encapsulated
			 * Ethernet), we'd do
			 *
			 *	return gen_snap(cstate, 0x000000, proto);
			 *
			 * here; for now, we don't, as per the above.
			 * I don't know whether it's worth the extra CPU
			 * time to do the right check or not.
			 */
			return gen_cmp(cstate, OR_LLC, 6, BPF_H, (bpf_int32)proto);
		}
	}
}

static struct block *
gen_hostop(compiler_state_t *cstate, bpf_u_int32 addr, bpf_u_int32 mask,
    int dir, int proto, u_int src_off, u_int dst_off)
{
	struct block *b0, *b1;
	u_int offset;

	switch (dir) {

	case Q_SRC:
		offset = src_off;
		break;

	case Q_DST:
		offset = dst_off;
		break;

	case Q_AND:
		b0 = gen_hostop(cstate, addr, mask, Q_SRC, proto, src_off, dst_off);
		b1 = gen_hostop(cstate, addr, mask, Q_DST, proto, src_off, dst_off);
		gen_and(b0, b1);
		return b1;

	case Q_OR:
	case Q_DEFAULT:
		b0 = gen_hostop(cstate, addr, mask, Q_SRC, proto, src_off, dst_off);
		b1 = gen_hostop(cstate, addr, mask, Q_DST, proto, src_off, dst_off);
		gen_or(b0, b1);
		return b1;

	case Q_ADDR1:
		bpf_error(cstate, "'addr1' and 'address1' are not valid qualifiers for addresses other than 802.11 MAC addresses");
		break;

	case Q_ADDR2:
		bpf_error(cstate, "'addr2' and 'address2' are not valid qualifiers for addresses other than 802.11 MAC addresses");
		break;

	case Q_ADDR3:
		bpf_error(cstate, "'addr3' and 'address3' are not valid qualifiers for addresses other than 802.11 MAC addresses");
		break;

	case Q_ADDR4:
		bpf_error(cstate, "'addr4' and 'address4' are not valid qualifiers for addresses other than 802.11 MAC addresses");
		break;

	case Q_RA:
		bpf_error(cstate, "'ra' is not a valid qualifier for addresses other than 802.11 MAC addresses");
		break;

	case Q_TA:
		bpf_error(cstate, "'ta' is not a valid qualifier for addresses other than 802.11 MAC addresses");
		break;

	default:
		abort();
	}
	b0 = gen_linktype(cstate, proto);
	b1 = gen_mcmp(cstate, OR_LINKPL, offset, BPF_W, (bpf_int32)addr, mask);
	gen_and(b0, b1);
	return b1;
}

#ifdef INET6
static struct block *
gen_hostop6(compiler_state_t *cstate, struct in6_addr *addr,
    struct in6_addr *mask, int dir, int proto, u_int src_off, u_int dst_off)
{
	struct block *b0, *b1;
	u_int offset;
	uint32_t *a, *m;

	switch (dir) {

	case Q_SRC:
		offset = src_off;
		break;

	case Q_DST:
		offset = dst_off;
		break;

	case Q_AND:
		b0 = gen_hostop6(cstate, addr, mask, Q_SRC, proto, src_off, dst_off);
		b1 = gen_hostop6(cstate, addr, mask, Q_DST, proto, src_off, dst_off);
		gen_and(b0, b1);
		return b1;

	case Q_OR:
	case Q_DEFAULT:
		b0 = gen_hostop6(cstate, addr, mask, Q_SRC, proto, src_off, dst_off);
		b1 = gen_hostop6(cstate, addr, mask, Q_DST, proto, src_off, dst_off);
		gen_or(b0, b1);
		return b1;

	case Q_ADDR1:
		bpf_error(cstate, "'addr1' and 'address1' are not valid qualifiers for addresses other than 802.11 MAC addresses");
		break;

	case Q_ADDR2:
		bpf_error(cstate, "'addr2' and 'address2' are not valid qualifiers for addresses other than 802.11 MAC addresses");
		break;

	case Q_ADDR3:
		bpf_error(cstate, "'addr3' and 'address3' are not valid qualifiers for addresses other than 802.11 MAC addresses");
		break;

	case Q_ADDR4:
		bpf_error(cstate, "'addr4' and 'address4' are not valid qualifiers for addresses other than 802.11 MAC addresses");
		break;

	case Q_RA:
		bpf_error(cstate, "'ra' is not a valid qualifier for addresses other than 802.11 MAC addresses");
		break;

	case Q_TA:
		bpf_error(cstate, "'ta' is not a valid qualifier for addresses other than 802.11 MAC addresses");
		break;

	default:
		abort();
	}
	/* this order is important */
	a = (uint32_t *)addr;
	m = (uint32_t *)mask;
	b1 = gen_mcmp(cstate, OR_LINKPL, offset + 12, BPF_W, ntohl(a[3]), ntohl(m[3]));
	b0 = gen_mcmp(cstate, OR_LINKPL, offset + 8, BPF_W, ntohl(a[2]), ntohl(m[2]));
	gen_and(b0, b1);
	b0 = gen_mcmp(cstate, OR_LINKPL, offset + 4, BPF_W, ntohl(a[1]), ntohl(m[1]));
	gen_and(b0, b1);
	b0 = gen_mcmp(cstate, OR_LINKPL, offset + 0, BPF_W, ntohl(a[0]), ntohl(m[0]));
	gen_and(b0, b1);
	b0 = gen_linktype(cstate, proto);
	gen_and(b0, b1);
	return b1;
}
#endif

static struct block *
gen_ehostop(compiler_state_t *cstate, const u_char *eaddr, int dir)
{
	register struct block *b0, *b1;

	switch (dir) {
	case Q_SRC:
		return gen_bcmp(cstate, OR_LINKHDR, 6, 6, eaddr);

	case Q_DST:
		return gen_bcmp(cstate, OR_LINKHDR, 0, 6, eaddr);

	case Q_AND:
		b0 = gen_ehostop(cstate, eaddr, Q_SRC);
		b1 = gen_ehostop(cstate, eaddr, Q_DST);
		gen_and(b0, b1);
		return b1;

	case Q_DEFAULT:
	case Q_OR:
		b0 = gen_ehostop(cstate, eaddr, Q_SRC);
		b1 = gen_ehostop(cstate, eaddr, Q_DST);
		gen_or(b0, b1);
		return b1;

	case Q_ADDR1:
		bpf_error(cstate, "'addr1' and 'address1' are only supported on 802.11 with 802.11 headers");
		break;

	case Q_ADDR2:
		bpf_error(cstate, "'addr2' and 'address2' are only supported on 802.11 with 802.11 headers");
		break;

	case Q_ADDR3:
		bpf_error(cstate, "'addr3' and 'address3' are only supported on 802.11 with 802.11 headers");
		break;

	case Q_ADDR4:
		bpf_error(cstate, "'addr4' and 'address4' are only supported on 802.11 with 802.11 headers");
		break;

	case Q_RA:
		bpf_error(cstate, "'ra' is only supported on 802.11 with 802.11 headers");
		break;

	case Q_TA:
		bpf_error(cstate, "'ta' is only supported on 802.11 with 802.11 headers");
		break;
	}
	abort();
	/* NOTREACHED */
}

/*
 * Like gen_ehostop, but for DLT_FDDI
 */
static struct block *
gen_fhostop(compiler_state_t *cstate, const u_char *eaddr, int dir)
{
	struct block *b0, *b1;

	switch (dir) {
	case Q_SRC:
		return gen_bcmp(cstate, OR_LINKHDR, 6 + 1 + cstate->pcap_fddipad, 6, eaddr);

	case Q_DST:
		return gen_bcmp(cstate, OR_LINKHDR, 0 + 1 + cstate->pcap_fddipad, 6, eaddr);

	case Q_AND:
		b0 = gen_fhostop(cstate, eaddr, Q_SRC);
		b1 = gen_fhostop(cstate, eaddr, Q_DST);
		gen_and(b0, b1);
		return b1;

	case Q_DEFAULT:
	case Q_OR:
		b0 = gen_fhostop(cstate, eaddr, Q_SRC);
		b1 = gen_fhostop(cstate, eaddr, Q_DST);
		gen_or(b0, b1);
		return b1;

	case Q_ADDR1:
		bpf_error(cstate, "'addr1' and 'address1' are only supported on 802.11");
		break;

	case Q_ADDR2:
		bpf_error(cstate, "'addr2' and 'address2' are only supported on 802.11");
		break;

	case Q_ADDR3:
		bpf_error(cstate, "'addr3' and 'address3' are only supported on 802.11");
		break;

	case Q_ADDR4:
		bpf_error(cstate, "'addr4' and 'address4' are only supported on 802.11");
		break;

	case Q_RA:
		bpf_error(cstate, "'ra' is only supported on 802.11");
		break;

	case Q_TA:
		bpf_error(cstate, "'ta' is only supported on 802.11");
		break;
	}
	abort();
	/* NOTREACHED */
}

/*
 * Like gen_ehostop, but for DLT_IEEE802 (Token Ring)
 */
static struct block *
gen_thostop(compiler_state_t *cstate, const u_char *eaddr, int dir)
{
	register struct block *b0, *b1;

	switch (dir) {
	case Q_SRC:
		return gen_bcmp(cstate, OR_LINKHDR, 8, 6, eaddr);

	case Q_DST:
		return gen_bcmp(cstate, OR_LINKHDR, 2, 6, eaddr);

	case Q_AND:
		b0 = gen_thostop(cstate, eaddr, Q_SRC);
		b1 = gen_thostop(cstate, eaddr, Q_DST);
		gen_and(b0, b1);
		return b1;

	case Q_DEFAULT:
	case Q_OR:
		b0 = gen_thostop(cstate, eaddr, Q_SRC);
		b1 = gen_thostop(cstate, eaddr, Q_DST);
		gen_or(b0, b1);
		return b1;

	case Q_ADDR1:
		bpf_error(cstate, "'addr1' and 'address1' are only supported on 802.11");
		break;

	case Q_ADDR2:
		bpf_error(cstate, "'addr2' and 'address2' are only supported on 802.11");
		break;

	case Q_ADDR3:
		bpf_error(cstate, "'addr3' and 'address3' are only supported on 802.11");
		break;

	case Q_ADDR4:
		bpf_error(cstate, "'addr4' and 'address4' are only supported on 802.11");
		break;

	case Q_RA:
		bpf_error(cstate, "'ra' is only supported on 802.11");
		break;

	case Q_TA:
		bpf_error(cstate, "'ta' is only supported on 802.11");
		break;
	}
	abort();
	/* NOTREACHED */
}

/*
 * Like gen_ehostop, but for DLT_IEEE802_11 (802.11 wireless LAN) and
 * various 802.11 + radio headers.
 */
static struct block *
gen_wlanhostop(compiler_state_t *cstate, const u_char *eaddr, int dir)
{
	register struct block *b0, *b1, *b2;
	register struct slist *s;

#ifdef ENABLE_WLAN_FILTERING_PATCH
	/*
	 * TODO GV 20070613
	 * We need to disable the optimizer because the optimizer is buggy
	 * and wipes out some LD instructions generated by the below
	 * code to validate the Frame Control bits
	 */
	cstate->no_optimize = 1;
#endif /* ENABLE_WLAN_FILTERING_PATCH */

	switch (dir) {
	case Q_SRC:
		/*
		 * Oh, yuk.
		 *
		 *	For control frames, there is no SA.
		 *
		 *	For management frames, SA is at an
		 *	offset of 10 from the beginning of
		 *	the packet.
		 *
		 *	For data frames, SA is at an offset
		 *	of 10 from the beginning of the packet
		 *	if From DS is clear, at an offset of
		 *	16 from the beginning of the packet
		 *	if From DS is set and To DS is clear,
		 *	and an offset of 24 from the beginning
		 *	of the packet if From DS is set and To DS
		 *	is set.
		 */

		/*
		 * Generate the tests to be done for data frames
		 * with From DS set.
		 *
		 * First, check for To DS set, i.e. check "link[1] & 0x01".
		 */
		s = gen_load_a(cstate, OR_LINKHDR, 1, BPF_B);
		b1 = new_block(cstate, JMP(BPF_JSET));
		b1->s.k = 0x01;	/* To DS */
		b1->stmts = s;

		/*
		 * If To DS is set, the SA is at 24.
		 */
		b0 = gen_bcmp(cstate, OR_LINKHDR, 24, 6, eaddr);
		gen_and(b1, b0);

		/*
		 * Now, check for To DS not set, i.e. check
		 * "!(link[1] & 0x01)".
		 */
		s = gen_load_a(cstate, OR_LINKHDR, 1, BPF_B);
		b2 = new_block(cstate, JMP(BPF_JSET));
		b2->s.k = 0x01;	/* To DS */
		b2->stmts = s;
		gen_not(b2);

		/*
		 * If To DS is not set, the SA is at 16.
		 */
		b1 = gen_bcmp(cstate, OR_LINKHDR, 16, 6, eaddr);
		gen_and(b2, b1);

		/*
		 * Now OR together the last two checks.  That gives
		 * the complete set of checks for data frames with
		 * From DS set.
		 */
		gen_or(b1, b0);

		/*
		 * Now check for From DS being set, and AND that with
		 * the ORed-together checks.
		 */
		s = gen_load_a(cstate, OR_LINKHDR, 1, BPF_B);
		b1 = new_block(cstate, JMP(BPF_JSET));
		b1->s.k = 0x02;	/* From DS */
		b1->stmts = s;
		gen_and(b1, b0);

		/*
		 * Now check for data frames with From DS not set.
		 */
		s = gen_load_a(cstate, OR_LINKHDR, 1, BPF_B);
		b2 = new_block(cstate, JMP(BPF_JSET));
		b2->s.k = 0x02;	/* From DS */
		b2->stmts = s;
		gen_not(b2);

		/*
		 * If From DS isn't set, the SA is at 10.
		 */
		b1 = gen_bcmp(cstate, OR_LINKHDR, 10, 6, eaddr);
		gen_and(b2, b1);

		/*
		 * Now OR together the checks for data frames with
		 * From DS not set and for data frames with From DS
		 * set; that gives the checks done for data frames.
		 */
		gen_or(b1, b0);

		/*
		 * Now check for a data frame.
		 * I.e, check "link[0] & 0x08".
		 */
		s = gen_load_a(cstate, OR_LINKHDR, 0, BPF_B);
		b1 = new_block(cstate, JMP(BPF_JSET));
		b1->s.k = 0x08;
		b1->stmts = s;

		/*
		 * AND that with the checks done for data frames.
		 */
		gen_and(b1, b0);

		/*
		 * If the high-order bit of the type value is 0, this
		 * is a management frame.
		 * I.e, check "!(link[0] & 0x08)".
		 */
		s = gen_load_a(cstate, OR_LINKHDR, 0, BPF_B);
		b2 = new_block(cstate, JMP(BPF_JSET));
		b2->s.k = 0x08;
		b2->stmts = s;
		gen_not(b2);

		/*
		 * For management frames, the SA is at 10.
		 */
		b1 = gen_bcmp(cstate, OR_LINKHDR, 10, 6, eaddr);
		gen_and(b2, b1);

		/*
		 * OR that with the checks done for data frames.
		 * That gives the checks done for management and
		 * data frames.
		 */
		gen_or(b1, b0);

		/*
		 * If the low-order bit of the type value is 1,
		 * this is either a control frame or a frame
		 * with a reserved type, and thus not a
		 * frame with an SA.
		 *
		 * I.e., check "!(link[0] & 0x04)".
		 */
		s = gen_load_a(cstate, OR_LINKHDR, 0, BPF_B);
		b1 = new_block(cstate, JMP(BPF_JSET));
		b1->s.k = 0x04;
		b1->stmts = s;
		gen_not(b1);

		/*
		 * AND that with the checks for data and management
		 * frames.
		 */
		gen_and(b1, b0);
		return b0;

	case Q_DST:
		/*
		 * Oh, yuk.
		 *
		 *	For control frames, there is no DA.
		 *
		 *	For management frames, DA is at an
		 *	offset of 4 from the beginning of
		 *	the packet.
		 *
		 *	For data frames, DA is at an offset
		 *	of 4 from the beginning of the packet
		 *	if To DS is clear and at an offset of
		 *	16 from the beginning of the packet
		 *	if To DS is set.
		 */

		/*
		 * Generate the tests to be done for data frames.
		 *
		 * First, check for To DS set, i.e. "link[1] & 0x01".
		 */
		s = gen_load_a(cstate, OR_LINKHDR, 1, BPF_B);
		b1 = new_block(cstate, JMP(BPF_JSET));
		b1->s.k = 0x01;	/* To DS */
		b1->stmts = s;

		/*
		 * If To DS is set, the DA is at 16.
		 */
		b0 = gen_bcmp(cstate, OR_LINKHDR, 16, 6, eaddr);
		gen_and(b1, b0);

		/*
		 * Now, check for To DS not set, i.e. check
		 * "!(link[1] & 0x01)".
		 */
		s = gen_load_a(cstate, OR_LINKHDR, 1, BPF_B);
		b2 = new_block(cstate, JMP(BPF_JSET));
		b2->s.k = 0x01;	/* To DS */
		b2->stmts = s;
		gen_not(b2);

		/*
		 * If To DS is not set, the DA is at 4.
		 */
		b1 = gen_bcmp(cstate, OR_LINKHDR, 4, 6, eaddr);
		gen_and(b2, b1);

		/*
		 * Now OR together the last two checks.  That gives
		 * the complete set of checks for data frames.
		 */
		gen_or(b1, b0);

		/*
		 * Now check for a data frame.
		 * I.e, check "link[0] & 0x08".
		 */
		s = gen_load_a(cstate, OR_LINKHDR, 0, BPF_B);
		b1 = new_block(cstate, JMP(BPF_JSET));
		b1->s.k = 0x08;
		b1->stmts = s;

		/*
		 * AND that with the checks done for data frames.
		 */
		gen_and(b1, b0);

		/*
		 * If the high-order bit of the type value is 0, this
		 * is a management frame.
		 * I.e, check "!(link[0] & 0x08)".
		 */
		s = gen_load_a(cstate, OR_LINKHDR, 0, BPF_B);
		b2 = new_block(cstate, JMP(BPF_JSET));
		b2->s.k = 0x08;
		b2->stmts = s;
		gen_not(b2);

		/*
		 * For management frames, the DA is at 4.
		 */
		b1 = gen_bcmp(cstate, OR_LINKHDR, 4, 6, eaddr);
		gen_and(b2, b1);

		/*
		 * OR that with the checks done for data frames.
		 * That gives the checks done for management and
		 * data frames.
		 */
		gen_or(b1, b0);

		/*
		 * If the low-order bit of the type value is 1,
		 * this is either a control frame or a frame
		 * with a reserved type, and thus not a
		 * frame with an SA.
		 *
		 * I.e., check "!(link[0] & 0x04)".
		 */
		s = gen_load_a(cstate, OR_LINKHDR, 0, BPF_B);
		b1 = new_block(cstate, JMP(BPF_JSET));
		b1->s.k = 0x04;
		b1->stmts = s;
		gen_not(b1);

		/*
		 * AND that with the checks for data and management
		 * frames.
		 */
		gen_and(b1, b0);
		return b0;

	case Q_RA:
		/*
		 * Not present in management frames; addr1 in other
		 * frames.
		 */

		/*
		 * If the high-order bit of the type value is 0, this
		 * is a management frame.
		 * I.e, check "(link[0] & 0x08)".
		 */
		s = gen_load_a(cstate, OR_LINKHDR, 0, BPF_B);
		b1 = new_block(cstate, JMP(BPF_JSET));
		b1->s.k = 0x08;
		b1->stmts = s;

		/*
		 * Check addr1.
		 */
		b0 = gen_bcmp(cstate, OR_LINKHDR, 4, 6, eaddr);

		/*
		 * AND that with the check of addr1.
		 */
		gen_and(b1, b0);
		return (b0);

	case Q_TA:
		/*
		 * Not present in management frames; addr2, if present,
		 * in other frames.
		 */

		/*
		 * Not present in CTS or ACK control frames.
		 */
		b0 = gen_mcmp(cstate, OR_LINKHDR, 0, BPF_B, IEEE80211_FC0_TYPE_CTL,
			IEEE80211_FC0_TYPE_MASK);
		gen_not(b0);
		b1 = gen_mcmp(cstate, OR_LINKHDR, 0, BPF_B, IEEE80211_FC0_SUBTYPE_CTS,
			IEEE80211_FC0_SUBTYPE_MASK);
		gen_not(b1);
		b2 = gen_mcmp(cstate, OR_LINKHDR, 0, BPF_B, IEEE80211_FC0_SUBTYPE_ACK,
			IEEE80211_FC0_SUBTYPE_MASK);
		gen_not(b2);
		gen_and(b1, b2);
		gen_or(b0, b2);

		/*
		 * If the high-order bit of the type value is 0, this
		 * is a management frame.
		 * I.e, check "(link[0] & 0x08)".
		 */
		s = gen_load_a(cstate, OR_LINKHDR, 0, BPF_B);
		b1 = new_block(cstate, JMP(BPF_JSET));
		b1->s.k = 0x08;
		b1->stmts = s;

		/*
		 * AND that with the check for frames other than
		 * CTS and ACK frames.
		 */
		gen_and(b1, b2);

		/*
		 * Check addr2.
		 */
		b1 = gen_bcmp(cstate, OR_LINKHDR, 10, 6, eaddr);
		gen_and(b2, b1);
		return b1;

	/*
	 * XXX - add BSSID keyword?
	 */
	case Q_ADDR1:
		return (gen_bcmp(cstate, OR_LINKHDR, 4, 6, eaddr));

	case Q_ADDR2:
		/*
		 * Not present in CTS or ACK control frames.
		 */
		b0 = gen_mcmp(cstate, OR_LINKHDR, 0, BPF_B, IEEE80211_FC0_TYPE_CTL,
			IEEE80211_FC0_TYPE_MASK);
		gen_not(b0);
		b1 = gen_mcmp(cstate, OR_LINKHDR, 0, BPF_B, IEEE80211_FC0_SUBTYPE_CTS,
			IEEE80211_FC0_SUBTYPE_MASK);
		gen_not(b1);
		b2 = gen_mcmp(cstate, OR_LINKHDR, 0, BPF_B, IEEE80211_FC0_SUBTYPE_ACK,
			IEEE80211_FC0_SUBTYPE_MASK);
		gen_not(b2);
		gen_and(b1, b2);
		gen_or(b0, b2);
		b1 = gen_bcmp(cstate, OR_LINKHDR, 10, 6, eaddr);
		gen_and(b2, b1);
		return b1;

	case Q_ADDR3:
		/*
		 * Not present in control frames.
		 */
		b0 = gen_mcmp(cstate, OR_LINKHDR, 0, BPF_B, IEEE80211_FC0_TYPE_CTL,
			IEEE80211_FC0_TYPE_MASK);
		gen_not(b0);
		b1 = gen_bcmp(cstate, OR_LINKHDR, 16, 6, eaddr);
		gen_and(b0, b1);
		return b1;

	case Q_ADDR4:
		/*
		 * Present only if the direction mask has both "From DS"
		 * and "To DS" set.  Neither control frames nor management
		 * frames should have both of those set, so we don't
		 * check the frame type.
		 */
		b0 = gen_mcmp(cstate, OR_LINKHDR, 1, BPF_B,
			IEEE80211_FC1_DIR_DSTODS, IEEE80211_FC1_DIR_MASK);
		b1 = gen_bcmp(cstate, OR_LINKHDR, 24, 6, eaddr);
		gen_and(b0, b1);
		return b1;

	case Q_AND:
		b0 = gen_wlanhostop(cstate, eaddr, Q_SRC);
		b1 = gen_wlanhostop(cstate, eaddr, Q_DST);
		gen_and(b0, b1);
		return b1;

	case Q_DEFAULT:
	case Q_OR:
		b0 = gen_wlanhostop(cstate, eaddr, Q_SRC);
		b1 = gen_wlanhostop(cstate, eaddr, Q_DST);
		gen_or(b0, b1);
		return b1;
	}
	abort();
	/* NOTREACHED */
}

/*
 * Like gen_ehostop, but for RFC 2625 IP-over-Fibre-Channel.
 * (We assume that the addresses are IEEE 48-bit MAC addresses,
 * as the RFC states.)
 */
static struct block *
gen_ipfchostop(compiler_state_t *cstate, const u_char *eaddr, int dir)
{
	register struct block *b0, *b1;

	switch (dir) {
	case Q_SRC:
		return gen_bcmp(cstate, OR_LINKHDR, 10, 6, eaddr);

	case Q_DST:
		return gen_bcmp(cstate, OR_LINKHDR, 2, 6, eaddr);

	case Q_AND:
		b0 = gen_ipfchostop(cstate, eaddr, Q_SRC);
		b1 = gen_ipfchostop(cstate, eaddr, Q_DST);
		gen_and(b0, b1);
		return b1;

	case Q_DEFAULT:
	case Q_OR:
		b0 = gen_ipfchostop(cstate, eaddr, Q_SRC);
		b1 = gen_ipfchostop(cstate, eaddr, Q_DST);
		gen_or(b0, b1);
		return b1;

	case Q_ADDR1:
		bpf_error(cstate, "'addr1' and 'address1' are only supported on 802.11");
		break;

	case Q_ADDR2:
		bpf_error(cstate, "'addr2' and 'address2' are only supported on 802.11");
		break;

	case Q_ADDR3:
		bpf_error(cstate, "'addr3' and 'address3' are only supported on 802.11");
		break;

	case Q_ADDR4:
		bpf_error(cstate, "'addr4' and 'address4' are only supported on 802.11");
		break;

	case Q_RA:
		bpf_error(cstate, "'ra' is only supported on 802.11");
		break;

	case Q_TA:
		bpf_error(cstate, "'ta' is only supported on 802.11");
		break;
	}
	abort();
	/* NOTREACHED */
}

/*
 * This is quite tricky because there may be pad bytes in front of the
 * DECNET header, and then there are two possible data packet formats that
 * carry both src and dst addresses, plus 5 packet types in a format that
 * carries only the src node, plus 2 types that use a different format and
 * also carry just the src node.
 *
 * Yuck.
 *
 * Instead of doing those all right, we just look for data packets with
 * 0 or 1 bytes of padding.  If you want to look at other packets, that
 * will require a lot more hacking.
 *
 * To add support for filtering on DECNET "areas" (network numbers)
 * one would want to add a "mask" argument to this routine.  That would
 * make the filter even more inefficient, although one could be clever
 * and not generate masking instructions if the mask is 0xFFFF.
 */
static struct block *
gen_dnhostop(compiler_state_t *cstate, bpf_u_int32 addr, int dir)
{
	struct block *b0, *b1, *b2, *tmp;
	u_int offset_lh;	/* offset if long header is received */
	u_int offset_sh;	/* offset if short header is received */

	switch (dir) {

	case Q_DST:
		offset_sh = 1;	/* follows flags */
		offset_lh = 7;	/* flgs,darea,dsubarea,HIORD */
		break;

	case Q_SRC:
		offset_sh = 3;	/* follows flags, dstnode */
		offset_lh = 15;	/* flgs,darea,dsubarea,did,sarea,ssub,HIORD */
		break;

	case Q_AND:
		/* Inefficient because we do our Calvinball dance twice */
		b0 = gen_dnhostop(cstate, addr, Q_SRC);
		b1 = gen_dnhostop(cstate, addr, Q_DST);
		gen_and(b0, b1);
		return b1;

	case Q_OR:
	case Q_DEFAULT:
		/* Inefficient because we do our Calvinball dance twice */
		b0 = gen_dnhostop(cstate, addr, Q_SRC);
		b1 = gen_dnhostop(cstate, addr, Q_DST);
		gen_or(b0, b1);
		return b1;

	case Q_ISO:
		bpf_error(cstate, "ISO host filtering not implemented");

	default:
		abort();
	}
	b0 = gen_linktype(cstate, ETHERTYPE_DN);
	/* Check for pad = 1, long header case */
	tmp = gen_mcmp(cstate, OR_LINKPL, 2, BPF_H,
	    (bpf_int32)ntohs(0x0681), (bpf_int32)ntohs(0x07FF));
	b1 = gen_cmp(cstate, OR_LINKPL, 2 + 1 + offset_lh,
	    BPF_H, (bpf_int32)ntohs((u_short)addr));
	gen_and(tmp, b1);
	/* Check for pad = 0, long header case */
	tmp = gen_mcmp(cstate, OR_LINKPL, 2, BPF_B, (bpf_int32)0x06, (bpf_int32)0x7);
	b2 = gen_cmp(cstate, OR_LINKPL, 2 + offset_lh, BPF_H, (bpf_int32)ntohs((u_short)addr));
	gen_and(tmp, b2);
	gen_or(b2, b1);
	/* Check for pad = 1, short header case */
	tmp = gen_mcmp(cstate, OR_LINKPL, 2, BPF_H,
	    (bpf_int32)ntohs(0x0281), (bpf_int32)ntohs(0x07FF));
	b2 = gen_cmp(cstate, OR_LINKPL, 2 + 1 + offset_sh, BPF_H, (bpf_int32)ntohs((u_short)addr));
	gen_and(tmp, b2);
	gen_or(b2, b1);
	/* Check for pad = 0, short header case */
	tmp = gen_mcmp(cstate, OR_LINKPL, 2, BPF_B, (bpf_int32)0x02, (bpf_int32)0x7);
	b2 = gen_cmp(cstate, OR_LINKPL, 2 + offset_sh, BPF_H, (bpf_int32)ntohs((u_short)addr));
	gen_and(tmp, b2);
	gen_or(b2, b1);

	/* Combine with test for cstate->linktype */
	gen_and(b0, b1);
	return b1;
}

/*
 * Generate a check for IPv4 or IPv6 for MPLS-encapsulated packets;
 * test the bottom-of-stack bit, and then check the version number
 * field in the IP header.
 */
static struct block *
gen_mpls_linktype(compiler_state_t *cstate, int proto)
{
	struct block *b0, *b1;

        switch (proto) {

        case Q_IP:
                /* match the bottom-of-stack bit */
                b0 = gen_mcmp(cstate, OR_LINKPL, (u_int)-2, BPF_B, 0x01, 0x01);
                /* match the IPv4 version number */
                b1 = gen_mcmp(cstate, OR_LINKPL, 0, BPF_B, 0x40, 0xf0);
                gen_and(b0, b1);
                return b1;

       case Q_IPV6:
                /* match the bottom-of-stack bit */
                b0 = gen_mcmp(cstate, OR_LINKPL, (u_int)-2, BPF_B, 0x01, 0x01);
                /* match the IPv4 version number */
                b1 = gen_mcmp(cstate, OR_LINKPL, 0, BPF_B, 0x60, 0xf0);
                gen_and(b0, b1);
                return b1;

       default:
                abort();
        }
}

static struct block *
gen_host(compiler_state_t *cstate, bpf_u_int32 addr, bpf_u_int32 mask,
    int proto, int dir, int type)
{
	struct block *b0, *b1;
	const char *typestr;

	if (type == Q_NET)
		typestr = "net";
	else
		typestr = "host";

	switch (proto) {

	case Q_DEFAULT:
		b0 = gen_host(cstate, addr, mask, Q_IP, dir, type);
		/*
		 * Only check for non-IPv4 addresses if we're not
		 * checking MPLS-encapsulated packets.
		 */
		if (cstate->label_stack_depth == 0) {
			b1 = gen_host(cstate, addr, mask, Q_ARP, dir, type);
			gen_or(b0, b1);
			b0 = gen_host(cstate, addr, mask, Q_RARP, dir, type);
			gen_or(b1, b0);
		}
		return b0;

	case Q_IP:
		return gen_hostop(cstate, addr, mask, dir, ETHERTYPE_IP, 12, 16);

	case Q_RARP:
		return gen_hostop(cstate, addr, mask, dir, ETHERTYPE_REVARP, 14, 24);

	case Q_ARP:
		return gen_hostop(cstate, addr, mask, dir, ETHERTYPE_ARP, 14, 24);

	case Q_TCP:
		bpf_error(cstate, "'tcp' modifier applied to %s", typestr);

	case Q_SCTP:
		bpf_error(cstate, "'sctp' modifier applied to %s", typestr);

	case Q_UDP:
		bpf_error(cstate, "'udp' modifier applied to %s", typestr);

	case Q_ICMP:
		bpf_error(cstate, "'icmp' modifier applied to %s", typestr);

	case Q_IGMP:
		bpf_error(cstate, "'igmp' modifier applied to %s", typestr);

	case Q_IGRP:
		bpf_error(cstate, "'igrp' modifier applied to %s", typestr);

	case Q_PIM:
		bpf_error(cstate, "'pim' modifier applied to %s", typestr);

	case Q_VRRP:
		bpf_error(cstate, "'vrrp' modifier applied to %s", typestr);

	case Q_CARP:
		bpf_error(cstate, "'carp' modifier applied to %s", typestr);

	case Q_ATALK:
		bpf_error(cstate, "ATALK host filtering not implemented");

	case Q_AARP:
		bpf_error(cstate, "AARP host filtering not implemented");

	case Q_DECNET:
		return gen_dnhostop(cstate, addr, dir);

	case Q_SCA:
		bpf_error(cstate, "SCA host filtering not implemented");

	case Q_LAT:
		bpf_error(cstate, "LAT host filtering not implemented");

	case Q_MOPDL:
		bpf_error(cstate, "MOPDL host filtering not implemented");

	case Q_MOPRC:
		bpf_error(cstate, "MOPRC host filtering not implemented");

	case Q_IPV6:
		bpf_error(cstate, "'ip6' modifier applied to ip host");

	case Q_ICMPV6:
		bpf_error(cstate, "'icmp6' modifier applied to %s", typestr);

	case Q_AH:
		bpf_error(cstate, "'ah' modifier applied to %s", typestr);

	case Q_ESP:
		bpf_error(cstate, "'esp' modifier applied to %s", typestr);

	case Q_ISO:
		bpf_error(cstate, "ISO host filtering not implemented");

	case Q_ESIS:
		bpf_error(cstate, "'esis' modifier applied to %s", typestr);

	case Q_ISIS:
		bpf_error(cstate, "'isis' modifier applied to %s", typestr);

	case Q_CLNP:
		bpf_error(cstate, "'clnp' modifier applied to %s", typestr);

	case Q_STP:
		bpf_error(cstate, "'stp' modifier applied to %s", typestr);

	case Q_IPX:
		bpf_error(cstate, "IPX host filtering not implemented");

	case Q_NETBEUI:
		bpf_error(cstate, "'netbeui' modifier applied to %s", typestr);

	case Q_RADIO:
		bpf_error(cstate, "'radio' modifier applied to %s", typestr);

	default:
		abort();
	}
	/* NOTREACHED */
}

#ifdef INET6
static struct block *
gen_host6(compiler_state_t *cstate, struct in6_addr *addr,
    struct in6_addr *mask, int proto, int dir, int type)
{
	const char *typestr;

	if (type == Q_NET)
		typestr = "net";
	else
		typestr = "host";

	switch (proto) {

	case Q_DEFAULT:
		return gen_host6(cstate, addr, mask, Q_IPV6, dir, type);

	case Q_LINK:
		bpf_error(cstate, "link-layer modifier applied to ip6 %s", typestr);

	case Q_IP:
		bpf_error(cstate, "'ip' modifier applied to ip6 %s", typestr);

	case Q_RARP:
		bpf_error(cstate, "'rarp' modifier applied to ip6 %s", typestr);

	case Q_ARP:
		bpf_error(cstate, "'arp' modifier applied to ip6 %s", typestr);

	case Q_SCTP:
		bpf_error(cstate, "'sctp' modifier applied to %s", typestr);

	case Q_TCP:
		bpf_error(cstate, "'tcp' modifier applied to %s", typestr);

	case Q_UDP:
		bpf_error(cstate, "'udp' modifier applied to %s", typestr);

	case Q_ICMP:
		bpf_error(cstate, "'icmp' modifier applied to %s", typestr);

	case Q_IGMP:
		bpf_error(cstate, "'igmp' modifier applied to %s", typestr);

	case Q_IGRP:
		bpf_error(cstate, "'igrp' modifier applied to %s", typestr);

	case Q_PIM:
		bpf_error(cstate, "'pim' modifier applied to %s", typestr);

	case Q_VRRP:
		bpf_error(cstate, "'vrrp' modifier applied to %s", typestr);

	case Q_CARP:
		bpf_error(cstate, "'carp' modifier applied to %s", typestr);

	case Q_ATALK:
		bpf_error(cstate, "ATALK host filtering not implemented");

	case Q_AARP:
		bpf_error(cstate, "AARP host filtering not implemented");

	case Q_DECNET:
		bpf_error(cstate, "'decnet' modifier applied to ip6 %s", typestr);

	case Q_SCA:
		bpf_error(cstate, "SCA host filtering not implemented");

	case Q_LAT:
		bpf_error(cstate, "LAT host filtering not implemented");

	case Q_MOPDL:
		bpf_error(cstate, "MOPDL host filtering not implemented");

	case Q_MOPRC:
		bpf_error(cstate, "MOPRC host filtering not implemented");

	case Q_IPV6:
		return gen_hostop6(cstate, addr, mask, dir, ETHERTYPE_IPV6, 8, 24);

	case Q_ICMPV6:
		bpf_error(cstate, "'icmp6' modifier applied to %s", typestr);

	case Q_AH:
		bpf_error(cstate, "'ah' modifier applied to %s", typestr);

	case Q_ESP:
		bpf_error(cstate, "'esp' modifier applied to %s", typestr);

	case Q_ISO:
		bpf_error(cstate, "ISO host filtering not implemented");

	case Q_ESIS:
		bpf_error(cstate, "'esis' modifier applied to %s", typestr);

	case Q_ISIS:
		bpf_error(cstate, "'isis' modifier applied to %s", typestr);

	case Q_CLNP:
		bpf_error(cstate, "'clnp' modifier applied to %s", typestr);

	case Q_STP:
		bpf_error(cstate, "'stp' modifier applied to %s", typestr);

	case Q_IPX:
		bpf_error(cstate, "IPX host filtering not implemented");

	case Q_NETBEUI:
		bpf_error(cstate, "'netbeui' modifier applied to %s", typestr);

	case Q_RADIO:
		bpf_error(cstate, "'radio' modifier applied to %s", typestr);

	default:
		abort();
	}
	/* NOTREACHED */
}
#endif

#ifndef INET6
static struct block *
gen_gateway(compiler_state_t *cstate, const u_char *eaddr,
    struct addrinfo *alist, int proto, int dir)
{
	struct block *b0, *b1, *tmp;
	struct addrinfo *ai;
	struct sockaddr_in *sin;

	if (dir != 0)
		bpf_error(cstate, "direction applied to 'gateway'");

	switch (proto) {
	case Q_DEFAULT:
	case Q_IP:
	case Q_ARP:
	case Q_RARP:
		switch (cstate->linktype) {
		case DLT_EN10MB:
		case DLT_NETANALYZER:
		case DLT_NETANALYZER_TRANSPARENT:
			b1 = gen_prevlinkhdr_check(cstate);
			b0 = gen_ehostop(cstate, eaddr, Q_OR);
			if (b1 != NULL)
				gen_and(b1, b0);
			break;
		case DLT_FDDI:
			b0 = gen_fhostop(cstate, eaddr, Q_OR);
			break;
		case DLT_IEEE802:
			b0 = gen_thostop(cstate, eaddr, Q_OR);
			break;
		case DLT_IEEE802_11:
		case DLT_PRISM_HEADER:
		case DLT_IEEE802_11_RADIO_AVS:
		case DLT_IEEE802_11_RADIO:
		case DLT_PPI:
			b0 = gen_wlanhostop(cstate, eaddr, Q_OR);
			break;
		case DLT_SUNATM:
			/*
			 * This is LLC-multiplexed traffic; if it were
			 * LANE, cstate->linktype would have been set to
			 * DLT_EN10MB.
			 */
			bpf_error(cstate,
			    "'gateway' supported only on ethernet/FDDI/token ring/802.11/ATM LANE/Fibre Channel");
			break;
		case DLT_IP_OVER_FC:
			b0 = gen_ipfchostop(cstate, eaddr, Q_OR);
			break;
		default:
			bpf_error(cstate,
			    "'gateway' supported only on ethernet/FDDI/token ring/802.11/ATM LANE/Fibre Channel");
		}
		b1 = NULL;
		for (ai = alist; ai != NULL; ai = ai->ai_next) {
			/*
			 * Does it have an address?
			 */
			if (ai->ai_addr != NULL) {
				/*
				 * Yes.  Is it an IPv4 address?
				 */
				if (ai->ai_addr->sa_family == AF_INET) {
					/*
					 * Generate an entry for it.
					 */
					sin = (struct sockaddr_in *)ai->ai_addr;
					tmp = gen_host(cstate,
					    ntohl(sin->sin_addr.s_addr),
					    0xffffffff, proto, Q_OR, Q_HOST);
					/*
					 * Is it the *first* IPv4 address?
					 */
					if (b1 == NULL) {
						/*
						 * Yes, so start with it.
						 */
						b1 = tmp;
					} else {
						/*
						 * No, so OR it into the
						 * existing set of
						 * addresses.
						 */
						gen_or(b1, tmp);
						b1 = tmp;
					}
				}
			}
		}
		if (b1 == NULL) {
			/*
			 * No IPv4 addresses found.
			 */
			return (NULL);
		}
		gen_not(b1);
		gen_and(b0, b1);
		return b1;
	}
	bpf_error(cstate, "illegal modifier of 'gateway'");
	/* NOTREACHED */
}
#endif

struct block *
gen_proto_abbrev(compiler_state_t *cstate, int proto)
{
	struct block *b0;
	struct block *b1;

	switch (proto) {

	case Q_SCTP:
		b1 = gen_proto(cstate, IPPROTO_SCTP, Q_IP, Q_DEFAULT);
		b0 = gen_proto(cstate, IPPROTO_SCTP, Q_IPV6, Q_DEFAULT);
		gen_or(b0, b1);
		break;

	case Q_TCP:
		b1 = gen_proto(cstate, IPPROTO_TCP, Q_IP, Q_DEFAULT);
		b0 = gen_proto(cstate, IPPROTO_TCP, Q_IPV6, Q_DEFAULT);
		gen_or(b0, b1);
		break;

	case Q_UDP:
		b1 = gen_proto(cstate, IPPROTO_UDP, Q_IP, Q_DEFAULT);
		b0 = gen_proto(cstate, IPPROTO_UDP, Q_IPV6, Q_DEFAULT);
		gen_or(b0, b1);
		break;

	case Q_ICMP:
		b1 = gen_proto(cstate, IPPROTO_ICMP, Q_IP, Q_DEFAULT);
		break;

#ifndef	IPPROTO_IGMP
#define	IPPROTO_IGMP	2
#endif

	case Q_IGMP:
		b1 = gen_proto(cstate, IPPROTO_IGMP, Q_IP, Q_DEFAULT);
		break;

#ifndef	IPPROTO_IGRP
#define	IPPROTO_IGRP	9
#endif
	case Q_IGRP:
		b1 = gen_proto(cstate, IPPROTO_IGRP, Q_IP, Q_DEFAULT);
		break;

#ifndef IPPROTO_PIM
#define IPPROTO_PIM	103
#endif

	case Q_PIM:
		b1 = gen_proto(cstate, IPPROTO_PIM, Q_IP, Q_DEFAULT);
		b0 = gen_proto(cstate, IPPROTO_PIM, Q_IPV6, Q_DEFAULT);
		gen_or(b0, b1);
		break;

#ifndef IPPROTO_VRRP
#define IPPROTO_VRRP	112
#endif

	case Q_VRRP:
		b1 = gen_proto(cstate, IPPROTO_VRRP, Q_IP, Q_DEFAULT);
		break;

#ifndef IPPROTO_CARP
#define IPPROTO_CARP	112
#endif

	case Q_CARP:
		b1 = gen_proto(cstate, IPPROTO_CARP, Q_IP, Q_DEFAULT);
		break;

	case Q_IP:
		b1 = gen_linktype(cstate, ETHERTYPE_IP);
		break;

	case Q_ARP:
		b1 = gen_linktype(cstate, ETHERTYPE_ARP);
		break;

	case Q_RARP:
		b1 = gen_linktype(cstate, ETHERTYPE_REVARP);
		break;

	case Q_LINK:
		bpf_error(cstate, "link layer applied in wrong context");

	case Q_ATALK:
		b1 = gen_linktype(cstate, ETHERTYPE_ATALK);
		break;

	case Q_AARP:
		b1 = gen_linktype(cstate, ETHERTYPE_AARP);
		break;

	case Q_DECNET:
		b1 = gen_linktype(cstate, ETHERTYPE_DN);
		break;

	case Q_SCA:
		b1 = gen_linktype(cstate, ETHERTYPE_SCA);
		break;

	case Q_LAT:
		b1 = gen_linktype(cstate, ETHERTYPE_LAT);
		break;

	case Q_MOPDL:
		b1 = gen_linktype(cstate, ETHERTYPE_MOPDL);
		break;

	case Q_MOPRC:
		b1 = gen_linktype(cstate, ETHERTYPE_MOPRC);
		break;

	case Q_IPV6:
		b1 = gen_linktype(cstate, ETHERTYPE_IPV6);
		break;

#ifndef IPPROTO_ICMPV6
#define IPPROTO_ICMPV6	58
#endif
	case Q_ICMPV6:
		b1 = gen_proto(cstate, IPPROTO_ICMPV6, Q_IPV6, Q_DEFAULT);
		break;

#ifndef IPPROTO_AH
#define IPPROTO_AH	51
#endif
	case Q_AH:
		b1 = gen_proto(cstate, IPPROTO_AH, Q_IP, Q_DEFAULT);
		b0 = gen_proto(cstate, IPPROTO_AH, Q_IPV6, Q_DEFAULT);
		gen_or(b0, b1);
		break;

#ifndef IPPROTO_ESP
#define IPPROTO_ESP	50
#endif
	case Q_ESP:
		b1 = gen_proto(cstate, IPPROTO_ESP, Q_IP, Q_DEFAULT);
		b0 = gen_proto(cstate, IPPROTO_ESP, Q_IPV6, Q_DEFAULT);
		gen_or(b0, b1);
		break;

	case Q_ISO:
		b1 = gen_linktype(cstate, LLCSAP_ISONS);
		break;

	case Q_ESIS:
		b1 = gen_proto(cstate, ISO9542_ESIS, Q_ISO, Q_DEFAULT);
		break;

	case Q_ISIS:
		b1 = gen_proto(cstate, ISO10589_ISIS, Q_ISO, Q_DEFAULT);
		break;

	case Q_ISIS_L1: /* all IS-IS Level1 PDU-Types */
		b0 = gen_proto(cstate, ISIS_L1_LAN_IIH, Q_ISIS, Q_DEFAULT);
		b1 = gen_proto(cstate, ISIS_PTP_IIH, Q_ISIS, Q_DEFAULT); /* FIXME extract the circuit-type bits */
		gen_or(b0, b1);
		b0 = gen_proto(cstate, ISIS_L1_LSP, Q_ISIS, Q_DEFAULT);
		gen_or(b0, b1);
		b0 = gen_proto(cstate, ISIS_L1_CSNP, Q_ISIS, Q_DEFAULT);
		gen_or(b0, b1);
		b0 = gen_proto(cstate, ISIS_L1_PSNP, Q_ISIS, Q_DEFAULT);
		gen_or(b0, b1);
		break;

	case Q_ISIS_L2: /* all IS-IS Level2 PDU-Types */
		b0 = gen_proto(cstate, ISIS_L2_LAN_IIH, Q_ISIS, Q_DEFAULT);
		b1 = gen_proto(cstate, ISIS_PTP_IIH, Q_ISIS, Q_DEFAULT); /* FIXME extract the circuit-type bits */
		gen_or(b0, b1);
		b0 = gen_proto(cstate, ISIS_L2_LSP, Q_ISIS, Q_DEFAULT);
		gen_or(b0, b1);
		b0 = gen_proto(cstate, ISIS_L2_CSNP, Q_ISIS, Q_DEFAULT);
		gen_or(b0, b1);
		b0 = gen_proto(cstate, ISIS_L2_PSNP, Q_ISIS, Q_DEFAULT);
		gen_or(b0, b1);
		break;

	case Q_ISIS_IIH: /* all IS-IS Hello PDU-Types */
		b0 = gen_proto(cstate, ISIS_L1_LAN_IIH, Q_ISIS, Q_DEFAULT);
		b1 = gen_proto(cstate, ISIS_L2_LAN_IIH, Q_ISIS, Q_DEFAULT);
		gen_or(b0, b1);
		b0 = gen_proto(cstate, ISIS_PTP_IIH, Q_ISIS, Q_DEFAULT);
		gen_or(b0, b1);
		break;

	case Q_ISIS_LSP:
		b0 = gen_proto(cstate, ISIS_L1_LSP, Q_ISIS, Q_DEFAULT);
		b1 = gen_proto(cstate, ISIS_L2_LSP, Q_ISIS, Q_DEFAULT);
		gen_or(b0, b1);
		break;

	case Q_ISIS_SNP:
		b0 = gen_proto(cstate, ISIS_L1_CSNP, Q_ISIS, Q_DEFAULT);
		b1 = gen_proto(cstate, ISIS_L2_CSNP, Q_ISIS, Q_DEFAULT);
		gen_or(b0, b1);
		b0 = gen_proto(cstate, ISIS_L1_PSNP, Q_ISIS, Q_DEFAULT);
		gen_or(b0, b1);
		b0 = gen_proto(cstate, ISIS_L2_PSNP, Q_ISIS, Q_DEFAULT);
		gen_or(b0, b1);
		break;

	case Q_ISIS_CSNP:
		b0 = gen_proto(cstate, ISIS_L1_CSNP, Q_ISIS, Q_DEFAULT);
		b1 = gen_proto(cstate, ISIS_L2_CSNP, Q_ISIS, Q_DEFAULT);
		gen_or(b0, b1);
		break;

	case Q_ISIS_PSNP:
		b0 = gen_proto(cstate, ISIS_L1_PSNP, Q_ISIS, Q_DEFAULT);
		b1 = gen_proto(cstate, ISIS_L2_PSNP, Q_ISIS, Q_DEFAULT);
		gen_or(b0, b1);
		break;

	case Q_CLNP:
		b1 = gen_proto(cstate, ISO8473_CLNP, Q_ISO, Q_DEFAULT);
		break;

	case Q_STP:
		b1 = gen_linktype(cstate, LLCSAP_8021D);
		break;

	case Q_IPX:
		b1 = gen_linktype(cstate, LLCSAP_IPX);
		break;

	case Q_NETBEUI:
		b1 = gen_linktype(cstate, LLCSAP_NETBEUI);
		break;

	case Q_RADIO:
		bpf_error(cstate, "'radio' is not a valid protocol type");

	default:
		abort();
	}
	return b1;
}

static struct block *
gen_ipfrag(compiler_state_t *cstate)
{
	struct slist *s;
	struct block *b;

	/* not IPv4 frag other than the first frag */
	s = gen_load_a(cstate, OR_LINKPL, 6, BPF_H);
	b = new_block(cstate, JMP(BPF_JSET));
	b->s.k = 0x1fff;
	b->stmts = s;
	gen_not(b);

	return b;
}

/*
 * Generate a comparison to a port value in the transport-layer header
 * at the specified offset from the beginning of that header.
 *
 * XXX - this handles a variable-length prefix preceding the link-layer
 * header, such as the radiotap or AVS radio prefix, but doesn't handle
 * variable-length link-layer headers (such as Token Ring or 802.11
 * headers).
 */
static struct block *
gen_portatom(compiler_state_t *cstate, int off, bpf_int32 v)
{
	return gen_cmp(cstate, OR_TRAN_IPV4, off, BPF_H, v);
}

static struct block *
gen_portatom6(compiler_state_t *cstate, int off, bpf_int32 v)
{
	return gen_cmp(cstate, OR_TRAN_IPV6, off, BPF_H, v);
}

struct block *
gen_portop(compiler_state_t *cstate, int port, int proto, int dir)
{
	struct block *b0, *b1, *tmp;

	/* ip proto 'proto' and not a fragment other than the first fragment */
	tmp = gen_cmp(cstate, OR_LINKPL, 9, BPF_B, (bpf_int32)proto);
	b0 = gen_ipfrag(cstate);
	gen_and(tmp, b0);

	switch (dir) {
	case Q_SRC:
		b1 = gen_portatom(cstate, 0, (bpf_int32)port);
		break;

	case Q_DST:
		b1 = gen_portatom(cstate, 2, (bpf_int32)port);
		break;

	case Q_OR:
	case Q_DEFAULT:
		tmp = gen_portatom(cstate, 0, (bpf_int32)port);
		b1 = gen_portatom(cstate, 2, (bpf_int32)port);
		gen_or(tmp, b1);
		break;

	case Q_AND:
		tmp = gen_portatom(cstate, 0, (bpf_int32)port);
		b1 = gen_portatom(cstate, 2, (bpf_int32)port);
		gen_and(tmp, b1);
		break;

	default:
		abort();
	}
	gen_and(b0, b1);

	return b1;
}

static struct block *
gen_port(compiler_state_t *cstate, int port, int ip_proto, int dir)
{
	struct block *b0, *b1, *tmp;

	/*
	 * ether proto ip
	 *
	 * For FDDI, RFC 1188 says that SNAP encapsulation is used,
	 * not LLC encapsulation with LLCSAP_IP.
	 *
	 * For IEEE 802 networks - which includes 802.5 token ring
	 * (which is what DLT_IEEE802 means) and 802.11 - RFC 1042
	 * says that SNAP encapsulation is used, not LLC encapsulation
	 * with LLCSAP_IP.
	 *
	 * For LLC-encapsulated ATM/"Classical IP", RFC 1483 and
	 * RFC 2225 say that SNAP encapsulation is used, not LLC
	 * encapsulation with LLCSAP_IP.
	 *
	 * So we always check for ETHERTYPE_IP.
	 */
	b0 = gen_linktype(cstate, ETHERTYPE_IP);

	switch (ip_proto) {
	case IPPROTO_UDP:
	case IPPROTO_TCP:
	case IPPROTO_SCTP:
		b1 = gen_portop(cstate, port, ip_proto, dir);
		break;

	case PROTO_UNDEF:
		tmp = gen_portop(cstate, port, IPPROTO_TCP, dir);
		b1 = gen_portop(cstate, port, IPPROTO_UDP, dir);
		gen_or(tmp, b1);
		tmp = gen_portop(cstate, port, IPPROTO_SCTP, dir);
		gen_or(tmp, b1);
		break;

	default:
		abort();
	}
	gen_and(b0, b1);
	return b1;
}

struct block *
gen_portop6(compiler_state_t *cstate, int port, int proto, int dir)
{
	struct block *b0, *b1, *tmp;

	/* ip6 proto 'proto' */
	/* XXX - catch the first fragment of a fragmented packet? */
	b0 = gen_cmp(cstate, OR_LINKPL, 6, BPF_B, (bpf_int32)proto);

	switch (dir) {
	case Q_SRC:
		b1 = gen_portatom6(cstate, 0, (bpf_int32)port);
		break;

	case Q_DST:
		b1 = gen_portatom6(cstate, 2, (bpf_int32)port);
		break;

	case Q_OR:
	case Q_DEFAULT:
		tmp = gen_portatom6(cstate, 0, (bpf_int32)port);
		b1 = gen_portatom6(cstate, 2, (bpf_int32)port);
		gen_or(tmp, b1);
		break;

	case Q_AND:
		tmp = gen_portatom6(cstate, 0, (bpf_int32)port);
		b1 = gen_portatom6(cstate, 2, (bpf_int32)port);
		gen_and(tmp, b1);
		break;

	default:
		abort();
	}
	gen_and(b0, b1);

	return b1;
}

static struct block *
gen_port6(compiler_state_t *cstate, int port, int ip_proto, int dir)
{
	struct block *b0, *b1, *tmp;

	/* link proto ip6 */
	b0 = gen_linktype(cstate, ETHERTYPE_IPV6);

	switch (ip_proto) {
	case IPPROTO_UDP:
	case IPPROTO_TCP:
	case IPPROTO_SCTP:
		b1 = gen_portop6(cstate, port, ip_proto, dir);
		break;

	case PROTO_UNDEF:
		tmp = gen_portop6(cstate, port, IPPROTO_TCP, dir);
		b1 = gen_portop6(cstate, port, IPPROTO_UDP, dir);
		gen_or(tmp, b1);
		tmp = gen_portop6(cstate, port, IPPROTO_SCTP, dir);
		gen_or(tmp, b1);
		break;

	default:
		abort();
	}
	gen_and(b0, b1);
	return b1;
}

/* gen_portrange code */
static struct block *
gen_portrangeatom(compiler_state_t *cstate, int off, bpf_int32 v1,
    bpf_int32 v2)
{
	struct block *b1, *b2;

	if (v1 > v2) {
		/*
		 * Reverse the order of the ports, so v1 is the lower one.
		 */
		bpf_int32 vtemp;

		vtemp = v1;
		v1 = v2;
		v2 = vtemp;
	}

	b1 = gen_cmp_ge(cstate, OR_TRAN_IPV4, off, BPF_H, v1);
	b2 = gen_cmp_le(cstate, OR_TRAN_IPV4, off, BPF_H, v2);

	gen_and(b1, b2);

	return b2;
}

struct block *
gen_portrangeop(compiler_state_t *cstate, int port1, int port2, int proto,
    int dir)
{
	struct block *b0, *b1, *tmp;

	/* ip proto 'proto' and not a fragment other than the first fragment */
	tmp = gen_cmp(cstate, OR_LINKPL, 9, BPF_B, (bpf_int32)proto);
	b0 = gen_ipfrag(cstate);
	gen_and(tmp, b0);

	switch (dir) {
	case Q_SRC:
		b1 = gen_portrangeatom(cstate, 0, (bpf_int32)port1, (bpf_int32)port2);
		break;

	case Q_DST:
		b1 = gen_portrangeatom(cstate, 2, (bpf_int32)port1, (bpf_int32)port2);
		break;

	case Q_OR:
	case Q_DEFAULT:
		tmp = gen_portrangeatom(cstate, 0, (bpf_int32)port1, (bpf_int32)port2);
		b1 = gen_portrangeatom(cstate, 2, (bpf_int32)port1, (bpf_int32)port2);
		gen_or(tmp, b1);
		break;

	case Q_AND:
		tmp = gen_portrangeatom(cstate, 0, (bpf_int32)port1, (bpf_int32)port2);
		b1 = gen_portrangeatom(cstate, 2, (bpf_int32)port1, (bpf_int32)port2);
		gen_and(tmp, b1);
		break;

	default:
		abort();
	}
	gen_and(b0, b1);

	return b1;
}

static struct block *
gen_portrange(compiler_state_t *cstate, int port1, int port2, int ip_proto,
    int dir)
{
	struct block *b0, *b1, *tmp;

	/* link proto ip */
	b0 = gen_linktype(cstate, ETHERTYPE_IP);

	switch (ip_proto) {
	case IPPROTO_UDP:
	case IPPROTO_TCP:
	case IPPROTO_SCTP:
		b1 = gen_portrangeop(cstate, port1, port2, ip_proto, dir);
		break;

	case PROTO_UNDEF:
		tmp = gen_portrangeop(cstate, port1, port2, IPPROTO_TCP, dir);
		b1 = gen_portrangeop(cstate, port1, port2, IPPROTO_UDP, dir);
		gen_or(tmp, b1);
		tmp = gen_portrangeop(cstate, port1, port2, IPPROTO_SCTP, dir);
		gen_or(tmp, b1);
		break;

	default:
		abort();
	}
	gen_and(b0, b1);
	return b1;
}

static struct block *
gen_portrangeatom6(compiler_state_t *cstate, int off, bpf_int32 v1,
    bpf_int32 v2)
{
	struct block *b1, *b2;

	if (v1 > v2) {
		/*
		 * Reverse the order of the ports, so v1 is the lower one.
		 */
		bpf_int32 vtemp;

		vtemp = v1;
		v1 = v2;
		v2 = vtemp;
	}

	b1 = gen_cmp_ge(cstate, OR_TRAN_IPV6, off, BPF_H, v1);
	b2 = gen_cmp_le(cstate, OR_TRAN_IPV6, off, BPF_H, v2);

	gen_and(b1, b2);

	return b2;
}

struct block *
gen_portrangeop6(compiler_state_t *cstate, int port1, int port2, int proto,
    int dir)
{
	struct block *b0, *b1, *tmp;

	/* ip6 proto 'proto' */
	/* XXX - catch the first fragment of a fragmented packet? */
	b0 = gen_cmp(cstate, OR_LINKPL, 6, BPF_B, (bpf_int32)proto);

	switch (dir) {
	case Q_SRC:
		b1 = gen_portrangeatom6(cstate, 0, (bpf_int32)port1, (bpf_int32)port2);
		break;

	case Q_DST:
		b1 = gen_portrangeatom6(cstate, 2, (bpf_int32)port1, (bpf_int32)port2);
		break;

	case Q_OR:
	case Q_DEFAULT:
		tmp = gen_portrangeatom6(cstate, 0, (bpf_int32)port1, (bpf_int32)port2);
		b1 = gen_portrangeatom6(cstate, 2, (bpf_int32)port1, (bpf_int32)port2);
		gen_or(tmp, b1);
		break;

	case Q_AND:
		tmp = gen_portrangeatom6(cstate, 0, (bpf_int32)port1, (bpf_int32)port2);
		b1 = gen_portrangeatom6(cstate, 2, (bpf_int32)port1, (bpf_int32)port2);
		gen_and(tmp, b1);
		break;

	default:
		abort();
	}
	gen_and(b0, b1);

	return b1;
}

static struct block *
gen_portrange6(compiler_state_t *cstate, int port1, int port2, int ip_proto,
    int dir)
{
	struct block *b0, *b1, *tmp;

	/* link proto ip6 */
	b0 = gen_linktype(cstate, ETHERTYPE_IPV6);

	switch (ip_proto) {
	case IPPROTO_UDP:
	case IPPROTO_TCP:
	case IPPROTO_SCTP:
		b1 = gen_portrangeop6(cstate, port1, port2, ip_proto, dir);
		break;

	case PROTO_UNDEF:
		tmp = gen_portrangeop6(cstate, port1, port2, IPPROTO_TCP, dir);
		b1 = gen_portrangeop6(cstate, port1, port2, IPPROTO_UDP, dir);
		gen_or(tmp, b1);
		tmp = gen_portrangeop6(cstate, port1, port2, IPPROTO_SCTP, dir);
		gen_or(tmp, b1);
		break;

	default:
		abort();
	}
	gen_and(b0, b1);
	return b1;
}

static int
lookup_proto(compiler_state_t *cstate, const char *name, int proto)
{
	register int v;

	switch (proto) {

	case Q_DEFAULT:
	case Q_IP:
	case Q_IPV6:
		v = pcap_nametoproto(name);
		if (v == PROTO_UNDEF)
			bpf_error(cstate, "unknown ip proto '%s'", name);
		break;

	case Q_LINK:
		/* XXX should look up h/w protocol type based on cstate->linktype */
		v = pcap_nametoeproto(name);
		if (v == PROTO_UNDEF) {
			v = pcap_nametollc(name);
			if (v == PROTO_UNDEF)
				bpf_error(cstate, "unknown ether proto '%s'", name);
		}
		break;

	case Q_ISO:
		if (strcmp(name, "esis") == 0)
			v = ISO9542_ESIS;
		else if (strcmp(name, "isis") == 0)
			v = ISO10589_ISIS;
		else if (strcmp(name, "clnp") == 0)
			v = ISO8473_CLNP;
		else
			bpf_error(cstate, "unknown osi proto '%s'", name);
		break;

	default:
		v = PROTO_UNDEF;
		break;
	}
	return v;
}

#if 0
struct stmt *
gen_joinsp(struct stmt **s, int n)
{
	return NULL;
}
#endif

static struct block *
gen_protochain(compiler_state_t *cstate, int v, int proto, int dir)
{
#ifdef NO_PROTOCHAIN
	return gen_proto(cstate, v, proto, dir);
#else
	struct block *b0, *b;
	struct slist *s[100];
	int fix2, fix3, fix4, fix5;
	int ahcheck, again, end;
	int i, max;
	int reg2 = alloc_reg(cstate);

	memset(s, 0, sizeof(s));
	fix3 = fix4 = fix5 = 0;

	switch (proto) {
	case Q_IP:
	case Q_IPV6:
		break;
	case Q_DEFAULT:
		b0 = gen_protochain(cstate, v, Q_IP, dir);
		b = gen_protochain(cstate, v, Q_IPV6, dir);
		gen_or(b0, b);
		return b;
	default:
		bpf_error(cstate, "bad protocol applied for 'protochain'");
		/*NOTREACHED*/
	}

	/*
	 * We don't handle variable-length prefixes before the link-layer
	 * header, or variable-length link-layer headers, here yet.
	 * We might want to add BPF instructions to do the protochain
	 * work, to simplify that and, on platforms that have a BPF
	 * interpreter with the new instructions, let the filtering
	 * be done in the kernel.  (We already require a modified BPF
	 * engine to do the protochain stuff, to support backward
	 * branches, and backward branch support is unlikely to appear
	 * in kernel BPF engines.)
	 */
	if (cstate->off_linkpl.is_variable)
		bpf_error(cstate, "'protochain' not supported with variable length headers");

	cstate->no_optimize = 1; /* this code is not compatible with optimizer yet */

	/*
	 * s[0] is a dummy entry to protect other BPF insn from damage
	 * by s[fix] = foo with uninitialized variable "fix".  It is somewhat
	 * hard to find interdependency made by jump table fixup.
	 */
	i = 0;
	s[i] = new_stmt(cstate, 0);	/*dummy*/
	i++;

	switch (proto) {
	case Q_IP:
		b0 = gen_linktype(cstate, ETHERTYPE_IP);

		/* A = ip->ip_p */
		s[i] = new_stmt(cstate, BPF_LD|BPF_ABS|BPF_B);
		s[i]->s.k = cstate->off_linkpl.constant_part + cstate->off_nl + 9;
		i++;
		/* X = ip->ip_hl << 2 */
		s[i] = new_stmt(cstate, BPF_LDX|BPF_MSH|BPF_B);
		s[i]->s.k = cstate->off_linkpl.constant_part + cstate->off_nl;
		i++;
		break;

	case Q_IPV6:
		b0 = gen_linktype(cstate, ETHERTYPE_IPV6);

		/* A = ip6->ip_nxt */
		s[i] = new_stmt(cstate, BPF_LD|BPF_ABS|BPF_B);
		s[i]->s.k = cstate->off_linkpl.constant_part + cstate->off_nl + 6;
		i++;
		/* X = sizeof(struct ip6_hdr) */
		s[i] = new_stmt(cstate, BPF_LDX|BPF_IMM);
		s[i]->s.k = 40;
		i++;
		break;

	default:
		bpf_error(cstate, "unsupported proto to gen_protochain");
		/*NOTREACHED*/
	}

	/* again: if (A == v) goto end; else fall through; */
	again = i;
	s[i] = new_stmt(cstate, BPF_JMP|BPF_JEQ|BPF_K);
	s[i]->s.k = v;
	s[i]->s.jt = NULL;		/*later*/
	s[i]->s.jf = NULL;		/*update in next stmt*/
	fix5 = i;
	i++;

#ifndef IPPROTO_NONE
#define IPPROTO_NONE	59
#endif
	/* if (A == IPPROTO_NONE) goto end */
	s[i] = new_stmt(cstate, BPF_JMP|BPF_JEQ|BPF_K);
	s[i]->s.jt = NULL;	/*later*/
	s[i]->s.jf = NULL;	/*update in next stmt*/
	s[i]->s.k = IPPROTO_NONE;
	s[fix5]->s.jf = s[i];
	fix2 = i;
	i++;

	if (proto == Q_IPV6) {
		int v6start, v6end, v6advance, j;

		v6start = i;
		/* if (A == IPPROTO_HOPOPTS) goto v6advance */
		s[i] = new_stmt(cstate, BPF_JMP|BPF_JEQ|BPF_K);
		s[i]->s.jt = NULL;	/*later*/
		s[i]->s.jf = NULL;	/*update in next stmt*/
		s[i]->s.k = IPPROTO_HOPOPTS;
		s[fix2]->s.jf = s[i];
		i++;
		/* if (A == IPPROTO_DSTOPTS) goto v6advance */
		s[i - 1]->s.jf = s[i] = new_stmt(cstate, BPF_JMP|BPF_JEQ|BPF_K);
		s[i]->s.jt = NULL;	/*later*/
		s[i]->s.jf = NULL;	/*update in next stmt*/
		s[i]->s.k = IPPROTO_DSTOPTS;
		i++;
		/* if (A == IPPROTO_ROUTING) goto v6advance */
		s[i - 1]->s.jf = s[i] = new_stmt(cstate, BPF_JMP|BPF_JEQ|BPF_K);
		s[i]->s.jt = NULL;	/*later*/
		s[i]->s.jf = NULL;	/*update in next stmt*/
		s[i]->s.k = IPPROTO_ROUTING;
		i++;
		/* if (A == IPPROTO_FRAGMENT) goto v6advance; else goto ahcheck; */
		s[i - 1]->s.jf = s[i] = new_stmt(cstate, BPF_JMP|BPF_JEQ|BPF_K);
		s[i]->s.jt = NULL;	/*later*/
		s[i]->s.jf = NULL;	/*later*/
		s[i]->s.k = IPPROTO_FRAGMENT;
		fix3 = i;
		v6end = i;
		i++;

		/* v6advance: */
		v6advance = i;

		/*
		 * in short,
		 * A = P[X + packet head];
		 * X = X + (P[X + packet head + 1] + 1) * 8;
		 */
		/* A = P[X + packet head] */
		s[i] = new_stmt(cstate, BPF_LD|BPF_IND|BPF_B);
		s[i]->s.k = cstate->off_linkpl.constant_part + cstate->off_nl;
		i++;
		/* MEM[reg2] = A */
		s[i] = new_stmt(cstate, BPF_ST);
		s[i]->s.k = reg2;
		i++;
		/* A = P[X + packet head + 1]; */
		s[i] = new_stmt(cstate, BPF_LD|BPF_IND|BPF_B);
		s[i]->s.k = cstate->off_linkpl.constant_part + cstate->off_nl + 1;
		i++;
		/* A += 1 */
		s[i] = new_stmt(cstate, BPF_ALU|BPF_ADD|BPF_K);
		s[i]->s.k = 1;
		i++;
		/* A *= 8 */
		s[i] = new_stmt(cstate, BPF_ALU|BPF_MUL|BPF_K);
		s[i]->s.k = 8;
		i++;
		/* A += X */
		s[i] = new_stmt(cstate, BPF_ALU|BPF_ADD|BPF_X);
		s[i]->s.k = 0;
		i++;
		/* X = A; */
		s[i] = new_stmt(cstate, BPF_MISC|BPF_TAX);
		i++;
		/* A = MEM[reg2] */
		s[i] = new_stmt(cstate, BPF_LD|BPF_MEM);
		s[i]->s.k = reg2;
		i++;

		/* goto again; (must use BPF_JA for backward jump) */
		s[i] = new_stmt(cstate, BPF_JMP|BPF_JA);
		s[i]->s.k = again - i - 1;
		s[i - 1]->s.jf = s[i];
		i++;

		/* fixup */
		for (j = v6start; j <= v6end; j++)
			s[j]->s.jt = s[v6advance];
	} else {
		/* nop */
		s[i] = new_stmt(cstate, BPF_ALU|BPF_ADD|BPF_K);
		s[i]->s.k = 0;
		s[fix2]->s.jf = s[i];
		i++;
	}

	/* ahcheck: */
	ahcheck = i;
	/* if (A == IPPROTO_AH) then fall through; else goto end; */
	s[i] = new_stmt(cstate, BPF_JMP|BPF_JEQ|BPF_K);
	s[i]->s.jt = NULL;	/*later*/
	s[i]->s.jf = NULL;	/*later*/
	s[i]->s.k = IPPROTO_AH;
	if (fix3)
		s[fix3]->s.jf = s[ahcheck];
	fix4 = i;
	i++;

	/*
	 * in short,
	 * A = P[X];
	 * X = X + (P[X + 1] + 2) * 4;
	 */
	/* A = X */
	s[i - 1]->s.jt = s[i] = new_stmt(cstate, BPF_MISC|BPF_TXA);
	i++;
	/* A = P[X + packet head]; */
	s[i] = new_stmt(cstate, BPF_LD|BPF_IND|BPF_B);
	s[i]->s.k = cstate->off_linkpl.constant_part + cstate->off_nl;
	i++;
	/* MEM[reg2] = A */
	s[i] = new_stmt(cstate, BPF_ST);
	s[i]->s.k = reg2;
	i++;
	/* A = X */
	s[i - 1]->s.jt = s[i] = new_stmt(cstate, BPF_MISC|BPF_TXA);
	i++;
	/* A += 1 */
	s[i] = new_stmt(cstate, BPF_ALU|BPF_ADD|BPF_K);
	s[i]->s.k = 1;
	i++;
	/* X = A */
	s[i] = new_stmt(cstate, BPF_MISC|BPF_TAX);
	i++;
	/* A = P[X + packet head] */
	s[i] = new_stmt(cstate, BPF_LD|BPF_IND|BPF_B);
	s[i]->s.k = cstate->off_linkpl.constant_part + cstate->off_nl;
	i++;
	/* A += 2 */
	s[i] = new_stmt(cstate, BPF_ALU|BPF_ADD|BPF_K);
	s[i]->s.k = 2;
	i++;
	/* A *= 4 */
	s[i] = new_stmt(cstate, BPF_ALU|BPF_MUL|BPF_K);
	s[i]->s.k = 4;
	i++;
	/* X = A; */
	s[i] = new_stmt(cstate, BPF_MISC|BPF_TAX);
	i++;
	/* A = MEM[reg2] */
	s[i] = new_stmt(cstate, BPF_LD|BPF_MEM);
	s[i]->s.k = reg2;
	i++;

	/* goto again; (must use BPF_JA for backward jump) */
	s[i] = new_stmt(cstate, BPF_JMP|BPF_JA);
	s[i]->s.k = again - i - 1;
	i++;

	/* end: nop */
	end = i;
	s[i] = new_stmt(cstate, BPF_ALU|BPF_ADD|BPF_K);
	s[i]->s.k = 0;
	s[fix2]->s.jt = s[end];
	s[fix4]->s.jf = s[end];
	s[fix5]->s.jt = s[end];
	i++;

	/*
	 * make slist chain
	 */
	max = i;
	for (i = 0; i < max - 1; i++)
		s[i]->next = s[i + 1];
	s[max - 1]->next = NULL;

	/*
	 * emit final check
	 */
	b = new_block(cstate, JMP(BPF_JEQ));
	b->stmts = s[1];	/*remember, s[0] is dummy*/
	b->s.k = v;

	free_reg(cstate, reg2);

	gen_and(b0, b);
	return b;
#endif
}

static struct block *
gen_check_802_11_data_frame(compiler_state_t *cstate)
{
	struct slist *s;
	struct block *b0, *b1;

	/*
	 * A data frame has the 0x08 bit (b3) in the frame control field set
	 * and the 0x04 bit (b2) clear.
	 */
	s = gen_load_a(cstate, OR_LINKHDR, 0, BPF_B);
	b0 = new_block(cstate, JMP(BPF_JSET));
	b0->s.k = 0x08;
	b0->stmts = s;

	s = gen_load_a(cstate, OR_LINKHDR, 0, BPF_B);
	b1 = new_block(cstate, JMP(BPF_JSET));
	b1->s.k = 0x04;
	b1->stmts = s;
	gen_not(b1);

	gen_and(b1, b0);

	return b0;
}

/*
 * Generate code that checks whether the packet is a packet for protocol
 * <proto> and whether the type field in that protocol's header has
 * the value <v>, e.g. if <proto> is Q_IP, it checks whether it's an
 * IP packet and checks the protocol number in the IP header against <v>.
 *
 * If <proto> is Q_DEFAULT, i.e. just "proto" was specified, it checks
 * against Q_IP and Q_IPV6.
 */
static struct block *
gen_proto(compiler_state_t *cstate, int v, int proto, int dir)
{
	struct block *b0, *b1;
#ifndef CHASE_CHAIN
	struct block *b2;
#endif

	if (dir != Q_DEFAULT)
		bpf_error(cstate, "direction applied to 'proto'");

	switch (proto) {
	case Q_DEFAULT:
		b0 = gen_proto(cstate, v, Q_IP, dir);
		b1 = gen_proto(cstate, v, Q_IPV6, dir);
		gen_or(b0, b1);
		return b1;

	case Q_IP:
		/*
		 * For FDDI, RFC 1188 says that SNAP encapsulation is used,
		 * not LLC encapsulation with LLCSAP_IP.
		 *
		 * For IEEE 802 networks - which includes 802.5 token ring
		 * (which is what DLT_IEEE802 means) and 802.11 - RFC 1042
		 * says that SNAP encapsulation is used, not LLC encapsulation
		 * with LLCSAP_IP.
		 *
		 * For LLC-encapsulated ATM/"Classical IP", RFC 1483 and
		 * RFC 2225 say that SNAP encapsulation is used, not LLC
		 * encapsulation with LLCSAP_IP.
		 *
		 * So we always check for ETHERTYPE_IP.
		 */
		b0 = gen_linktype(cstate, ETHERTYPE_IP);
#ifndef CHASE_CHAIN
		b1 = gen_cmp(cstate, OR_LINKPL, 9, BPF_B, (bpf_int32)v);
#else
		b1 = gen_protochain(cstate, v, Q_IP);
#endif
		gen_and(b0, b1);
		return b1;

	case Q_ISO:
		switch (cstate->linktype) {

		case DLT_FRELAY:
			/*
			 * Frame Relay packets typically have an OSI
			 * NLPID at the beginning; "gen_linktype(cstate, LLCSAP_ISONS)"
			 * generates code to check for all the OSI
			 * NLPIDs, so calling it and then adding a check
			 * for the particular NLPID for which we're
			 * looking is bogus, as we can just check for
			 * the NLPID.
			 *
			 * What we check for is the NLPID and a frame
			 * control field value of UI, i.e. 0x03 followed
			 * by the NLPID.
			 *
			 * XXX - assumes a 2-byte Frame Relay header with
			 * DLCI and flags.  What if the address is longer?
			 *
			 * XXX - what about SNAP-encapsulated frames?
			 */
			return gen_cmp(cstate, OR_LINKHDR, 2, BPF_H, (0x03<<8) | v);
			/*NOTREACHED*/
			break;

		case DLT_C_HDLC:
			/*
			 * Cisco uses an Ethertype lookalike - for OSI,
			 * it's 0xfefe.
			 */
			b0 = gen_linktype(cstate, LLCSAP_ISONS<<8 | LLCSAP_ISONS);
			/* OSI in C-HDLC is stuffed with a fudge byte */
			b1 = gen_cmp(cstate, OR_LINKPL_NOSNAP, 1, BPF_B, (long)v);
			gen_and(b0, b1);
			return b1;

		default:
			b0 = gen_linktype(cstate, LLCSAP_ISONS);
			b1 = gen_cmp(cstate, OR_LINKPL_NOSNAP, 0, BPF_B, (long)v);
			gen_and(b0, b1);
			return b1;
		}

	case Q_ISIS:
		b0 = gen_proto(cstate, ISO10589_ISIS, Q_ISO, Q_DEFAULT);
		/*
		 * 4 is the offset of the PDU type relative to the IS-IS
		 * header.
		 */
		b1 = gen_cmp(cstate, OR_LINKPL_NOSNAP, 4, BPF_B, (long)v);
		gen_and(b0, b1);
		return b1;

	case Q_ARP:
		bpf_error(cstate, "arp does not encapsulate another protocol");
		/* NOTREACHED */

	case Q_RARP:
		bpf_error(cstate, "rarp does not encapsulate another protocol");
		/* NOTREACHED */

	case Q_ATALK:
		bpf_error(cstate, "atalk encapsulation is not specifiable");
		/* NOTREACHED */

	case Q_DECNET:
		bpf_error(cstate, "decnet encapsulation is not specifiable");
		/* NOTREACHED */

	case Q_SCA:
		bpf_error(cstate, "sca does not encapsulate another protocol");
		/* NOTREACHED */

	case Q_LAT:
		bpf_error(cstate, "lat does not encapsulate another protocol");
		/* NOTREACHED */

	case Q_MOPRC:
		bpf_error(cstate, "moprc does not encapsulate another protocol");
		/* NOTREACHED */

	case Q_MOPDL:
		bpf_error(cstate, "mopdl does not encapsulate another protocol");
		/* NOTREACHED */

	case Q_LINK:
		return gen_linktype(cstate, v);

	case Q_UDP:
		bpf_error(cstate, "'udp proto' is bogus");
		/* NOTREACHED */

	case Q_TCP:
		bpf_error(cstate, "'tcp proto' is bogus");
		/* NOTREACHED */

	case Q_SCTP:
		bpf_error(cstate, "'sctp proto' is bogus");
		/* NOTREACHED */

	case Q_ICMP:
		bpf_error(cstate, "'icmp proto' is bogus");
		/* NOTREACHED */

	case Q_IGMP:
		bpf_error(cstate, "'igmp proto' is bogus");
		/* NOTREACHED */

	case Q_IGRP:
		bpf_error(cstate, "'igrp proto' is bogus");
		/* NOTREACHED */

	case Q_PIM:
		bpf_error(cstate, "'pim proto' is bogus");
		/* NOTREACHED */

	case Q_VRRP:
		bpf_error(cstate, "'vrrp proto' is bogus");
		/* NOTREACHED */

	case Q_CARP:
		bpf_error(cstate, "'carp proto' is bogus");
		/* NOTREACHED */

	case Q_IPV6:
		b0 = gen_linktype(cstate, ETHERTYPE_IPV6);
#ifndef CHASE_CHAIN
		/*
		 * Also check for a fragment header before the final
		 * header.
		 */
		b2 = gen_cmp(cstate, OR_LINKPL, 6, BPF_B, IPPROTO_FRAGMENT);
		b1 = gen_cmp(cstate, OR_LINKPL, 40, BPF_B, (bpf_int32)v);
		gen_and(b2, b1);
		b2 = gen_cmp(cstate, OR_LINKPL, 6, BPF_B, (bpf_int32)v);
		gen_or(b2, b1);
#else
		b1 = gen_protochain(cstate, v, Q_IPV6);
#endif
		gen_and(b0, b1);
		return b1;

	case Q_ICMPV6:
		bpf_error(cstate, "'icmp6 proto' is bogus");

	case Q_AH:
		bpf_error(cstate, "'ah proto' is bogus");

	case Q_ESP:
		bpf_error(cstate, "'ah proto' is bogus");

	case Q_STP:
		bpf_error(cstate, "'stp proto' is bogus");

	case Q_IPX:
		bpf_error(cstate, "'ipx proto' is bogus");

	case Q_NETBEUI:
		bpf_error(cstate, "'netbeui proto' is bogus");

	case Q_RADIO:
		bpf_error(cstate, "'radio proto' is bogus");

	default:
		abort();
		/* NOTREACHED */
	}
	/* NOTREACHED */
}

struct block *
gen_scode(compiler_state_t *cstate, const char *name, struct qual q)
{
	int proto = q.proto;
	int dir = q.dir;
	int tproto;
	u_char *eaddr;
	bpf_u_int32 mask, addr;
	struct addrinfo *res, *res0;
	struct sockaddr_in *sin4;
#ifdef INET6
	int tproto6;
	struct sockaddr_in6 *sin6;
	struct in6_addr mask128;
#endif /*INET6*/
	struct block *b, *tmp;
	int port, real_proto;
	int port1, port2;

	switch (q.addr) {

	case Q_NET:
		addr = pcap_nametonetaddr(name);
		if (addr == 0)
			bpf_error(cstate, "unknown network '%s'", name);
		/* Left justify network addr and calculate its network mask */
		mask = 0xffffffff;
		while (addr && (addr & 0xff000000) == 0) {
			addr <<= 8;
			mask <<= 8;
		}
		return gen_host(cstate, addr, mask, proto, dir, q.addr);

	case Q_DEFAULT:
	case Q_HOST:
		if (proto == Q_LINK) {
			switch (cstate->linktype) {

			case DLT_EN10MB:
			case DLT_NETANALYZER:
			case DLT_NETANALYZER_TRANSPARENT:
				eaddr = pcap_ether_hostton(name);
				if (eaddr == NULL)
					bpf_error(cstate,
					    "unknown ether host '%s'", name);
				tmp = gen_prevlinkhdr_check(cstate);
				b = gen_ehostop(cstate, eaddr, dir);
				if (tmp != NULL)
					gen_and(tmp, b);
				free(eaddr);
				return b;

			case DLT_FDDI:
				eaddr = pcap_ether_hostton(name);
				if (eaddr == NULL)
					bpf_error(cstate,
					    "unknown FDDI host '%s'", name);
				b = gen_fhostop(cstate, eaddr, dir);
				free(eaddr);
				return b;

			case DLT_IEEE802:
				eaddr = pcap_ether_hostton(name);
				if (eaddr == NULL)
					bpf_error(cstate,
					    "unknown token ring host '%s'", name);
				b = gen_thostop(cstate, eaddr, dir);
				free(eaddr);
				return b;

			case DLT_IEEE802_11:
			case DLT_PRISM_HEADER:
			case DLT_IEEE802_11_RADIO_AVS:
			case DLT_IEEE802_11_RADIO:
			case DLT_PPI:
				eaddr = pcap_ether_hostton(name);
				if (eaddr == NULL)
					bpf_error(cstate,
					    "unknown 802.11 host '%s'", name);
				b = gen_wlanhostop(cstate, eaddr, dir);
				free(eaddr);
				return b;

			case DLT_IP_OVER_FC:
				eaddr = pcap_ether_hostton(name);
				if (eaddr == NULL)
					bpf_error(cstate,
					    "unknown Fibre Channel host '%s'", name);
				b = gen_ipfchostop(cstate, eaddr, dir);
				free(eaddr);
				return b;
			}

			bpf_error(cstate, "only ethernet/FDDI/token ring/802.11/ATM LANE/Fibre Channel supports link-level host name");
		} else if (proto == Q_DECNET) {
			unsigned short dn_addr;

			if (!__pcap_nametodnaddr(name, &dn_addr)) {
#ifdef	DECNETLIB
				bpf_error(cstate, "unknown decnet host name '%s'\n", name);
#else
				bpf_error(cstate, "decnet name support not included, '%s' cannot be translated\n",
					name);
#endif
			}
			/*
			 * I don't think DECNET hosts can be multihomed, so
			 * there is no need to build up a list of addresses
			 */
			return (gen_host(cstate, dn_addr, 0, proto, dir, q.addr));
		} else {
#ifdef INET6
			memset(&mask128, 0xff, sizeof(mask128));
#endif
			res0 = res = pcap_nametoaddrinfo(name);
			if (res == NULL)
				bpf_error(cstate, "unknown host '%s'", name);
			cstate->ai = res;
			b = tmp = NULL;
			tproto = proto;
#ifdef INET6
			tproto6 = proto;
#endif
			if (cstate->off_linktype.constant_part == OFFSET_NOT_SET &&
			    tproto == Q_DEFAULT) {
				tproto = Q_IP;
#ifdef INET6
				tproto6 = Q_IPV6;
#endif
			}
			for (res = res0; res; res = res->ai_next) {
				switch (res->ai_family) {
				case AF_INET:
#ifdef INET6
					if (tproto == Q_IPV6)
						continue;
#endif

					sin4 = (struct sockaddr_in *)
						res->ai_addr;
					tmp = gen_host(cstate, ntohl(sin4->sin_addr.s_addr),
						0xffffffff, tproto, dir, q.addr);
					break;
#ifdef INET6
				case AF_INET6:
					if (tproto6 == Q_IP)
						continue;

					sin6 = (struct sockaddr_in6 *)
						res->ai_addr;
					tmp = gen_host6(cstate, &sin6->sin6_addr,
						&mask128, tproto6, dir, q.addr);
					break;
#endif
				default:
					continue;
				}
				if (b)
					gen_or(b, tmp);
				b = tmp;
			}
			cstate->ai = NULL;
			freeaddrinfo(res0);
			if (b == NULL) {
				bpf_error(cstate, "unknown host '%s'%s", name,
				    (proto == Q_DEFAULT)
					? ""
					: " for specified address family");
			}
			return b;
		}

	case Q_PORT:
		if (proto != Q_DEFAULT &&
		    proto != Q_UDP && proto != Q_TCP && proto != Q_SCTP)
			bpf_error(cstate, "illegal qualifier of 'port'");
		if (pcap_nametoport(name, &port, &real_proto) == 0)
			bpf_error(cstate, "unknown port '%s'", name);
		if (proto == Q_UDP) {
			if (real_proto == IPPROTO_TCP)
				bpf_error(cstate, "port '%s' is tcp", name);
			else if (real_proto == IPPROTO_SCTP)
				bpf_error(cstate, "port '%s' is sctp", name);
			else
				/* override PROTO_UNDEF */
				real_proto = IPPROTO_UDP;
		}
		if (proto == Q_TCP) {
			if (real_proto == IPPROTO_UDP)
				bpf_error(cstate, "port '%s' is udp", name);

			else if (real_proto == IPPROTO_SCTP)
				bpf_error(cstate, "port '%s' is sctp", name);
			else
				/* override PROTO_UNDEF */
				real_proto = IPPROTO_TCP;
		}
		if (proto == Q_SCTP) {
			if (real_proto == IPPROTO_UDP)
				bpf_error(cstate, "port '%s' is udp", name);

			else if (real_proto == IPPROTO_TCP)
				bpf_error(cstate, "port '%s' is tcp", name);
			else
				/* override PROTO_UNDEF */
				real_proto = IPPROTO_SCTP;
		}
		if (port < 0)
			bpf_error(cstate, "illegal port number %d < 0", port);
		if (port > 65535)
			bpf_error(cstate, "illegal port number %d > 65535", port);
		b = gen_port(cstate, port, real_proto, dir);
		gen_or(gen_port6(cstate, port, real_proto, dir), b);
		return b;

	case Q_PORTRANGE:
		if (proto != Q_DEFAULT &&
		    proto != Q_UDP && proto != Q_TCP && proto != Q_SCTP)
			bpf_error(cstate, "illegal qualifier of 'portrange'");
		if (pcap_nametoportrange(name, &port1, &port2, &real_proto) == 0)
			bpf_error(cstate, "unknown port in range '%s'", name);
		if (proto == Q_UDP) {
			if (real_proto == IPPROTO_TCP)
				bpf_error(cstate, "port in range '%s' is tcp", name);
			else if (real_proto == IPPROTO_SCTP)
				bpf_error(cstate, "port in range '%s' is sctp", name);
			else
				/* override PROTO_UNDEF */
				real_proto = IPPROTO_UDP;
		}
		if (proto == Q_TCP) {
			if (real_proto == IPPROTO_UDP)
				bpf_error(cstate, "port in range '%s' is udp", name);
			else if (real_proto == IPPROTO_SCTP)
				bpf_error(cstate, "port in range '%s' is sctp", name);
			else
				/* override PROTO_UNDEF */
				real_proto = IPPROTO_TCP;
		}
		if (proto == Q_SCTP) {
			if (real_proto == IPPROTO_UDP)
				bpf_error(cstate, "port in range '%s' is udp", name);
			else if (real_proto == IPPROTO_TCP)
				bpf_error(cstate, "port in range '%s' is tcp", name);
			else
				/* override PROTO_UNDEF */
				real_proto = IPPROTO_SCTP;
		}
		if (port1 < 0)
			bpf_error(cstate, "illegal port number %d < 0", port1);
		if (port1 > 65535)
			bpf_error(cstate, "illegal port number %d > 65535", port1);
		if (port2 < 0)
			bpf_error(cstate, "illegal port number %d < 0", port2);
		if (port2 > 65535)
			bpf_error(cstate, "illegal port number %d > 65535", port2);

		b = gen_portrange(cstate, port1, port2, real_proto, dir);
		gen_or(gen_portrange6(cstate, port1, port2, real_proto, dir), b);
		return b;

	case Q_GATEWAY:
#ifndef INET6
		eaddr = pcap_ether_hostton(name);
		if (eaddr == NULL)
			bpf_error(cstate, "unknown ether host: %s", name);

		res = pcap_nametoaddrinfo(name);
		cstate->ai = res;
		if (res == NULL)
			bpf_error(cstate, "unknown host '%s'", name);
		b = gen_gateway(cstate, eaddr, res, proto, dir);
		cstate->ai = NULL;
		freeaddrinfo(res);
		if (b == NULL)
			bpf_error(cstate, "unknown host '%s'", name);
		return b;
#else
		bpf_error(cstate, "'gateway' not supported in this configuration");
#endif /*INET6*/

	case Q_PROTO:
		real_proto = lookup_proto(cstate, name, proto);
		if (real_proto >= 0)
			return gen_proto(cstate, real_proto, proto, dir);
		else
			bpf_error(cstate, "unknown protocol: %s", name);

	case Q_PROTOCHAIN:
		real_proto = lookup_proto(cstate, name, proto);
		if (real_proto >= 0)
			return gen_protochain(cstate, real_proto, proto, dir);
		else
			bpf_error(cstate, "unknown protocol: %s", name);

	case Q_UNDEF:
		syntax(cstate);
		/* NOTREACHED */
	}
	abort();
	/* NOTREACHED */
}

struct block *
gen_mcode(compiler_state_t *cstate, const char *s1, const char *s2,
    unsigned int masklen, struct qual q)
{
	register int nlen, mlen;
	bpf_u_int32 n, m;

	nlen = __pcap_atoin(s1, &n);
	/* Promote short ipaddr */
	n <<= 32 - nlen;

	if (s2 != NULL) {
		mlen = __pcap_atoin(s2, &m);
		/* Promote short ipaddr */
		m <<= 32 - mlen;
		if ((n & ~m) != 0)
			bpf_error(cstate, "non-network bits set in \"%s mask %s\"",
			    s1, s2);
	} else {
		/* Convert mask len to mask */
		if (masklen > 32)
			bpf_error(cstate, "mask length must be <= 32");
		if (masklen == 0) {
			/*
			 * X << 32 is not guaranteed by C to be 0; it's
			 * undefined.
			 */
			m = 0;
		} else
			m = 0xffffffff << (32 - masklen);
		if ((n & ~m) != 0)
			bpf_error(cstate, "non-network bits set in \"%s/%d\"",
			    s1, masklen);
	}

	switch (q.addr) {

	case Q_NET:
		return gen_host(cstate, n, m, q.proto, q.dir, q.addr);

	default:
		bpf_error(cstate, "Mask syntax for networks only");
		/* NOTREACHED */
	}
	/* NOTREACHED */
}

struct block *
gen_ncode(compiler_state_t *cstate, const char *s, bpf_u_int32 v, struct qual q)
{
	bpf_u_int32 mask;
	int proto = q.proto;
	int dir = q.dir;
	register int vlen;

	if (s == NULL)
		vlen = 32;
	else if (q.proto == Q_DECNET) {
		vlen = __pcap_atodn(s, &v);
		if (vlen == 0)
			bpf_error(cstate, "malformed decnet address '%s'", s);
	} else
		vlen = __pcap_atoin(s, &v);

	switch (q.addr) {

	case Q_DEFAULT:
	case Q_HOST:
	case Q_NET:
		if (proto == Q_DECNET)
			return gen_host(cstate, v, 0, proto, dir, q.addr);
		else if (proto == Q_LINK) {
			bpf_error(cstate, "illegal link layer address");
		} else {
			mask = 0xffffffff;
			if (s == NULL && q.addr == Q_NET) {
				/* Promote short net number */
				while (v && (v & 0xff000000) == 0) {
					v <<= 8;
					mask <<= 8;
				}
			} else {
				/* Promote short ipaddr */
				v <<= 32 - vlen;
				mask <<= 32 - vlen ;
			}
			return gen_host(cstate, v, mask, proto, dir, q.addr);
		}

	case Q_PORT:
		if (proto == Q_UDP)
			proto = IPPROTO_UDP;
		else if (proto == Q_TCP)
			proto = IPPROTO_TCP;
		else if (proto == Q_SCTP)
			proto = IPPROTO_SCTP;
		else if (proto == Q_DEFAULT)
			proto = PROTO_UNDEF;
		else
			bpf_error(cstate, "illegal qualifier of 'port'");

		if (v > 65535)
			bpf_error(cstate, "illegal port number %u > 65535", v);

	    {
		struct block *b;
		b = gen_port(cstate, (int)v, proto, dir);
		gen_or(gen_port6(cstate, (int)v, proto, dir), b);
		return b;
	    }

	case Q_PORTRANGE:
		if (proto == Q_UDP)
			proto = IPPROTO_UDP;
		else if (proto == Q_TCP)
			proto = IPPROTO_TCP;
		else if (proto == Q_SCTP)
			proto = IPPROTO_SCTP;
		else if (proto == Q_DEFAULT)
			proto = PROTO_UNDEF;
		else
			bpf_error(cstate, "illegal qualifier of 'portrange'");

		if (v > 65535)
			bpf_error(cstate, "illegal port number %u > 65535", v);

	    {
		struct block *b;
		b = gen_portrange(cstate, (int)v, (int)v, proto, dir);
		gen_or(gen_portrange6(cstate, (int)v, (int)v, proto, dir), b);
		return b;
	    }

	case Q_GATEWAY:
		bpf_error(cstate, "'gateway' requires a name");
		/* NOTREACHED */

	case Q_PROTO:
		return gen_proto(cstate, (int)v, proto, dir);

	case Q_PROTOCHAIN:
		return gen_protochain(cstate, (int)v, proto, dir);

	case Q_UNDEF:
		syntax(cstate);
		/* NOTREACHED */

	default:
		abort();
		/* NOTREACHED */
	}
	/* NOTREACHED */
}

#ifdef INET6
struct block *
gen_mcode6(compiler_state_t *cstate, const char *s1, const char *s2,
    unsigned int masklen, struct qual q)
{
	struct addrinfo *res;
	struct in6_addr *addr;
	struct in6_addr mask;
	struct block *b;
	uint32_t *a, *m;

	if (s2)
		bpf_error(cstate, "no mask %s supported", s2);

	res = pcap_nametoaddrinfo(s1);
	if (!res)
		bpf_error(cstate, "invalid ip6 address %s", s1);
	cstate->ai = res;
	if (res->ai_next)
		bpf_error(cstate, "%s resolved to multiple address", s1);
	addr = &((struct sockaddr_in6 *)res->ai_addr)->sin6_addr;

	if (sizeof(mask) * 8 < masklen)
		bpf_error(cstate, "mask length must be <= %u", (unsigned int)(sizeof(mask) * 8));
	memset(&mask, 0, sizeof(mask));
	memset(&mask, 0xff, masklen / 8);
	if (masklen % 8) {
		mask.s6_addr[masklen / 8] =
			(0xff << (8 - masklen % 8)) & 0xff;
	}

	a = (uint32_t *)addr;
	m = (uint32_t *)&mask;
	if ((a[0] & ~m[0]) || (a[1] & ~m[1])
	 || (a[2] & ~m[2]) || (a[3] & ~m[3])) {
		bpf_error(cstate, "non-network bits set in \"%s/%d\"", s1, masklen);
	}

	switch (q.addr) {

	case Q_DEFAULT:
	case Q_HOST:
		if (masklen != 128)
			bpf_error(cstate, "Mask syntax for networks only");
		/* FALLTHROUGH */

	case Q_NET:
		b = gen_host6(cstate, addr, &mask, q.proto, q.dir, q.addr);
		cstate->ai = NULL;
		freeaddrinfo(res);
		return b;

	default:
		bpf_error(cstate, "invalid qualifier against IPv6 address");
		/* NOTREACHED */
	}
}
#endif /*INET6*/

struct block *
gen_ecode(compiler_state_t *cstate, const u_char *eaddr, struct qual q)
{
	struct block *b, *tmp;

	if ((q.addr == Q_HOST || q.addr == Q_DEFAULT) && q.proto == Q_LINK) {
		switch (cstate->linktype) {
		case DLT_EN10MB:
		case DLT_NETANALYZER:
		case DLT_NETANALYZER_TRANSPARENT:
			tmp = gen_prevlinkhdr_check(cstate);
			b = gen_ehostop(cstate, eaddr, (int)q.dir);
			if (tmp != NULL)
				gen_and(tmp, b);
			return b;
		case DLT_FDDI:
			return gen_fhostop(cstate, eaddr, (int)q.dir);
		case DLT_IEEE802:
			return gen_thostop(cstate, eaddr, (int)q.dir);
		case DLT_IEEE802_11:
		case DLT_PRISM_HEADER:
		case DLT_IEEE802_11_RADIO_AVS:
		case DLT_IEEE802_11_RADIO:
		case DLT_PPI:
			return gen_wlanhostop(cstate, eaddr, (int)q.dir);
		case DLT_IP_OVER_FC:
			return gen_ipfchostop(cstate, eaddr, (int)q.dir);
		default:
			bpf_error(cstate, "ethernet addresses supported only on ethernet/FDDI/token ring/802.11/ATM LANE/Fibre Channel");
			break;
		}
	}
	bpf_error(cstate, "ethernet address used in non-ether expression");
	/* NOTREACHED */
}

void
sappend(struct slist *s0, struct slist *s1)
{
	/*
	 * This is definitely not the best way to do this, but the
	 * lists will rarely get long.
	 */
	while (s0->next)
		s0 = s0->next;
	s0->next = s1;
}

static struct slist *
xfer_to_x(compiler_state_t *cstate, struct arth *a)
{
	struct slist *s;

	s = new_stmt(cstate, BPF_LDX|BPF_MEM);
	s->s.k = a->regno;
	return s;
}

static struct slist *
xfer_to_a(compiler_state_t *cstate, struct arth *a)
{
	struct slist *s;

	s = new_stmt(cstate, BPF_LD|BPF_MEM);
	s->s.k = a->regno;
	return s;
}

/*
 * Modify "index" to use the value stored into its register as an
 * offset relative to the beginning of the header for the protocol
 * "proto", and allocate a register and put an item "size" bytes long
 * (1, 2, or 4) at that offset into that register, making it the register
 * for "index".
 */
struct arth *
gen_load(compiler_state_t *cstate, int proto, struct arth *inst, int size)
{
	struct slist *s, *tmp;
	struct block *b;
	int regno = alloc_reg(cstate);

	free_reg(cstate, inst->regno);
	switch (size) {

	default:
		bpf_error(cstate, "data size must be 1, 2, or 4");

	case 1:
		size = BPF_B;
		break;

	case 2:
		size = BPF_H;
		break;

	case 4:
		size = BPF_W;
		break;
	}
	switch (proto) {
	default:
		bpf_error(cstate, "unsupported index operation");

	case Q_RADIO:
		/*
		 * The offset is relative to the beginning of the packet
		 * data, if we have a radio header.  (If we don't, this
		 * is an error.)
		 */
		if (cstate->linktype != DLT_IEEE802_11_RADIO_AVS &&
		    cstate->linktype != DLT_IEEE802_11_RADIO &&
		    cstate->linktype != DLT_PRISM_HEADER)
			bpf_error(cstate, "radio information not present in capture");

		/*
		 * Load into the X register the offset computed into the
		 * register specified by "index".
		 */
		s = xfer_to_x(cstate, inst);

		/*
		 * Load the item at that offset.
		 */
		tmp = new_stmt(cstate, BPF_LD|BPF_IND|size);
		sappend(s, tmp);
		sappend(inst->s, s);
		break;

	case Q_LINK:
		/*
		 * The offset is relative to the beginning of
		 * the link-layer header.
		 *
		 * XXX - what about ATM LANE?  Should the index be
		 * relative to the beginning of the AAL5 frame, so
		 * that 0 refers to the beginning of the LE Control
		 * field, or relative to the beginning of the LAN
		 * frame, so that 0 refers, for Ethernet LANE, to
		 * the beginning of the destination address?
		 */
		s = gen_abs_offset_varpart(cstate, &cstate->off_linkhdr);

		/*
		 * If "s" is non-null, it has code to arrange that the
		 * X register contains the length of the prefix preceding
		 * the link-layer header.  Add to it the offset computed
		 * into the register specified by "index", and move that
		 * into the X register.  Otherwise, just load into the X
		 * register the offset computed into the register specified
		 * by "index".
		 */
		if (s != NULL) {
			sappend(s, xfer_to_a(cstate, inst));
			sappend(s, new_stmt(cstate, BPF_ALU|BPF_ADD|BPF_X));
			sappend(s, new_stmt(cstate, BPF_MISC|BPF_TAX));
		} else
			s = xfer_to_x(cstate, inst);

		/*
		 * Load the item at the sum of the offset we've put in the
		 * X register and the offset of the start of the link
		 * layer header (which is 0 if the radio header is
		 * variable-length; that header length is what we put
		 * into the X register and then added to the index).
		 */
		tmp = new_stmt(cstate, BPF_LD|BPF_IND|size);
		tmp->s.k = cstate->off_linkhdr.constant_part;
		sappend(s, tmp);
		sappend(inst->s, s);
		break;

	case Q_IP:
	case Q_ARP:
	case Q_RARP:
	case Q_ATALK:
	case Q_DECNET:
	case Q_SCA:
	case Q_LAT:
	case Q_MOPRC:
	case Q_MOPDL:
	case Q_IPV6:
		/*
		 * The offset is relative to the beginning of
		 * the network-layer header.
		 * XXX - are there any cases where we want
		 * cstate->off_nl_nosnap?
		 */
		s = gen_abs_offset_varpart(cstate, &cstate->off_linkpl);

		/*
		 * If "s" is non-null, it has code to arrange that the
		 * X register contains the variable part of the offset
		 * of the link-layer payload.  Add to it the offset
		 * computed into the register specified by "index",
		 * and move that into the X register.  Otherwise, just
		 * load into the X register the offset computed into
		 * the register specified by "index".
		 */
		if (s != NULL) {
			sappend(s, xfer_to_a(cstate, inst));
			sappend(s, new_stmt(cstate, BPF_ALU|BPF_ADD|BPF_X));
			sappend(s, new_stmt(cstate, BPF_MISC|BPF_TAX));
		} else
			s = xfer_to_x(cstate, inst);

		/*
		 * Load the item at the sum of the offset we've put in the
		 * X register, the offset of the start of the network
		 * layer header from the beginning of the link-layer
		 * payload, and the constant part of the offset of the
		 * start of the link-layer payload.
		 */
		tmp = new_stmt(cstate, BPF_LD|BPF_IND|size);
		tmp->s.k = cstate->off_linkpl.constant_part + cstate->off_nl;
		sappend(s, tmp);
		sappend(inst->s, s);

		/*
		 * Do the computation only if the packet contains
		 * the protocol in question.
		 */
		b = gen_proto_abbrev(cstate, proto);
		if (inst->b)
			gen_and(inst->b, b);
		inst->b = b;
		break;

	case Q_SCTP:
	case Q_TCP:
	case Q_UDP:
	case Q_ICMP:
	case Q_IGMP:
	case Q_IGRP:
	case Q_PIM:
	case Q_VRRP:
	case Q_CARP:
		/*
		 * The offset is relative to the beginning of
		 * the transport-layer header.
		 *
		 * Load the X register with the length of the IPv4 header
		 * (plus the offset of the link-layer header, if it's
		 * a variable-length header), in bytes.
		 *
		 * XXX - are there any cases where we want
		 * cstate->off_nl_nosnap?
		 * XXX - we should, if we're built with
		 * IPv6 support, generate code to load either
		 * IPv4, IPv6, or both, as appropriate.
		 */
		s = gen_loadx_iphdrlen(cstate);

		/*
		 * The X register now contains the sum of the variable
		 * part of the offset of the link-layer payload and the
		 * length of the network-layer header.
		 *
		 * Load into the A register the offset relative to
		 * the beginning of the transport layer header,
		 * add the X register to that, move that to the
		 * X register, and load with an offset from the
		 * X register equal to the sum of the constant part of
		 * the offset of the link-layer payload and the offset,
		 * relative to the beginning of the link-layer payload,
		 * of the network-layer header.
		 */
		sappend(s, xfer_to_a(cstate, inst));
		sappend(s, new_stmt(cstate, BPF_ALU|BPF_ADD|BPF_X));
		sappend(s, new_stmt(cstate, BPF_MISC|BPF_TAX));
		sappend(s, tmp = new_stmt(cstate, BPF_LD|BPF_IND|size));
		tmp->s.k = cstate->off_linkpl.constant_part + cstate->off_nl;
		sappend(inst->s, s);

		/*
		 * Do the computation only if the packet contains
		 * the protocol in question - which is true only
		 * if this is an IP datagram and is the first or
		 * only fragment of that datagram.
		 */
		gen_and(gen_proto_abbrev(cstate, proto), b = gen_ipfrag(cstate));
		if (inst->b)
			gen_and(inst->b, b);
		gen_and(gen_proto_abbrev(cstate, Q_IP), b);
		inst->b = b;
		break;
	case Q_ICMPV6:
        /*
        * Do the computation only if the packet contains
        * the protocol in question.
        */
        b = gen_proto_abbrev(cstate, Q_IPV6);
        if (inst->b) {
            gen_and(inst->b, b);
        }
        inst->b = b;

        /*
        * Check if we have an icmp6 next header
        */
        b = gen_cmp(cstate, OR_LINKPL, 6, BPF_B, 58);
        if (inst->b) {
            gen_and(inst->b, b);
        }
        inst->b = b;


        s = gen_abs_offset_varpart(cstate, &cstate->off_linkpl);
        /*
        * If "s" is non-null, it has code to arrange that the
        * X register contains the variable part of the offset
        * of the link-layer payload.  Add to it the offset
        * computed into the register specified by "index",
        * and move that into the X register.  Otherwise, just
        * load into the X register the offset computed into
        * the register specified by "index".
        */
        if (s != NULL) {
            sappend(s, xfer_to_a(cstate, inst));
            sappend(s, new_stmt(cstate, BPF_ALU|BPF_ADD|BPF_X));
            sappend(s, new_stmt(cstate, BPF_MISC|BPF_TAX));
        } else {
            s = xfer_to_x(cstate, inst);
        }

        /*
        * Load the item at the sum of the offset we've put in the
        * X register, the offset of the start of the network
        * layer header from the beginning of the link-layer
        * payload, and the constant part of the offset of the
        * start of the link-layer payload.
        */
        tmp = new_stmt(cstate, BPF_LD|BPF_IND|size);
        tmp->s.k = cstate->off_linkpl.constant_part + cstate->off_nl + 40;

        sappend(s, tmp);
        sappend(inst->s, s);

        break;
	}
	inst->regno = regno;
	s = new_stmt(cstate, BPF_ST);
	s->s.k = regno;
	sappend(inst->s, s);

	return inst;
}

struct block *
gen_relation(compiler_state_t *cstate, int code, struct arth *a0,
    struct arth *a1, int reversed)
{
	struct slist *s0, *s1, *s2;
	struct block *b, *tmp;

	s0 = xfer_to_x(cstate, a1);
	s1 = xfer_to_a(cstate, a0);
	if (code == BPF_JEQ) {
		s2 = new_stmt(cstate, BPF_ALU|BPF_SUB|BPF_X);
		b = new_block(cstate, JMP(code));
		sappend(s1, s2);
	}
	else
		b = new_block(cstate, BPF_JMP|code|BPF_X);
	if (reversed)
		gen_not(b);

	sappend(s0, s1);
	sappend(a1->s, s0);
	sappend(a0->s, a1->s);

	b->stmts = a0->s;

	free_reg(cstate, a0->regno);
	free_reg(cstate, a1->regno);

	/* 'and' together protocol checks */
	if (a0->b) {
		if (a1->b) {
			gen_and(a0->b, tmp = a1->b);
		}
		else
			tmp = a0->b;
	} else
		tmp = a1->b;

	if (tmp)
		gen_and(tmp, b);

	return b;
}

struct arth *
gen_loadlen(compiler_state_t *cstate)
{
	int regno = alloc_reg(cstate);
	struct arth *a = (struct arth *)newchunk(cstate, sizeof(*a));
	struct slist *s;

	s = new_stmt(cstate, BPF_LD|BPF_LEN);
	s->next = new_stmt(cstate, BPF_ST);
	s->next->s.k = regno;
	a->s = s;
	a->regno = regno;

	return a;
}

struct arth *
gen_loadi(compiler_state_t *cstate, int val)
{
	struct arth *a;
	struct slist *s;
	int reg;

	a = (struct arth *)newchunk(cstate, sizeof(*a));

	reg = alloc_reg(cstate);

	s = new_stmt(cstate, BPF_LD|BPF_IMM);
	s->s.k = val;
	s->next = new_stmt(cstate, BPF_ST);
	s->next->s.k = reg;
	a->s = s;
	a->regno = reg;

	return a;
}

struct arth *
gen_neg(compiler_state_t *cstate, struct arth *a)
{
	struct slist *s;

	s = xfer_to_a(cstate, a);
	sappend(a->s, s);
	s = new_stmt(cstate, BPF_ALU|BPF_NEG);
	s->s.k = 0;
	sappend(a->s, s);
	s = new_stmt(cstate, BPF_ST);
	s->s.k = a->regno;
	sappend(a->s, s);

	return a;
}

struct arth *
gen_arth(compiler_state_t *cstate, int code, struct arth *a0,
    struct arth *a1)
{
	struct slist *s0, *s1, *s2;

	/*
	 * Disallow division by, or modulus by, zero; we do this here
	 * so that it gets done even if the optimizer is disabled.
	 */
	if (code == BPF_DIV) {
		if (a1->s->s.code == (BPF_LD|BPF_IMM) && a1->s->s.k == 0)
			bpf_error(cstate, "division by zero");
	} else if (code == BPF_MOD) {
		if (a1->s->s.code == (BPF_LD|BPF_IMM) && a1->s->s.k == 0)
			bpf_error(cstate, "modulus by zero");
	}
	s0 = xfer_to_x(cstate, a1);
	s1 = xfer_to_a(cstate, a0);
	s2 = new_stmt(cstate, BPF_ALU|BPF_X|code);

	sappend(s1, s2);
	sappend(s0, s1);
	sappend(a1->s, s0);
	sappend(a0->s, a1->s);

	free_reg(cstate, a0->regno);
	free_reg(cstate, a1->regno);

	s0 = new_stmt(cstate, BPF_ST);
	a0->regno = s0->s.k = alloc_reg(cstate);
	sappend(a0->s, s0);

	return a0;
}

/*
 * Initialize the table of used registers and the current register.
 */
static void
init_regs(compiler_state_t *cstate)
{
	cstate->curreg = 0;
	memset(cstate->regused, 0, sizeof cstate->regused);
}

/*
 * Return the next free register.
 */
static int
alloc_reg(compiler_state_t *cstate)
{
	int n = BPF_MEMWORDS;

	while (--n >= 0) {
		if (cstate->regused[cstate->curreg])
			cstate->curreg = (cstate->curreg + 1) % BPF_MEMWORDS;
		else {
			cstate->regused[cstate->curreg] = 1;
			return cstate->curreg;
		}
	}
	bpf_error(cstate, "too many registers needed to evaluate expression");
	/* NOTREACHED */
}

/*
 * Return a register to the table so it can
 * be used later.
 */
static void
free_reg(compiler_state_t *cstate, int n)
{
	cstate->regused[n] = 0;
}

static struct block *
gen_len(compiler_state_t *cstate, int jmp, int n)
{
	struct slist *s;
	struct block *b;

	s = new_stmt(cstate, BPF_LD|BPF_LEN);
	b = new_block(cstate, JMP(jmp));
	b->stmts = s;
	b->s.k = n;

	return b;
}

struct block *
gen_greater(compiler_state_t *cstate, int n)
{
	return gen_len(cstate, BPF_JGE, n);
}

/*
 * Actually, this is less than or equal.
 */
struct block *
gen_less(compiler_state_t *cstate, int n)
{
	struct block *b;

	b = gen_len(cstate, BPF_JGT, n);
	gen_not(b);

	return b;
}

/*
 * This is for "byte {idx} {op} {val}"; "idx" is treated as relative to
 * the beginning of the link-layer header.
 * XXX - that means you can't test values in the radiotap header, but
 * as that header is difficult if not impossible to parse generally
 * without a loop, that might not be a severe problem.  A new keyword
 * "radio" could be added for that, although what you'd really want
 * would be a way of testing particular radio header values, which
 * would generate code appropriate to the radio header in question.
 */
struct block *
gen_byteop(compiler_state_t *cstate, int op, int idx, int val)
{
	struct block *b;
	struct slist *s;

	switch (op) {
	default:
		abort();

	case '=':
		return gen_cmp(cstate, OR_LINKHDR, (u_int)idx, BPF_B, (bpf_int32)val);

	case '<':
		b = gen_cmp_lt(cstate, OR_LINKHDR, (u_int)idx, BPF_B, (bpf_int32)val);
		return b;

	case '>':
		b = gen_cmp_gt(cstate, OR_LINKHDR, (u_int)idx, BPF_B, (bpf_int32)val);
		return b;

	case '|':
		s = new_stmt(cstate, BPF_ALU|BPF_OR|BPF_K);
		break;

	case '&':
		s = new_stmt(cstate, BPF_ALU|BPF_AND|BPF_K);
		break;
	}
	s->s.k = val;
	b = new_block(cstate, JMP(BPF_JEQ));
	b->stmts = s;
	gen_not(b);

	return b;
}

static const u_char abroadcast[] = { 0x0 };

struct block *
gen_broadcast(compiler_state_t *cstate, int proto)
{
	bpf_u_int32 hostmask;
	struct block *b0, *b1, *b2;
	static const u_char ebroadcast[] = { 0xff, 0xff, 0xff, 0xff, 0xff, 0xff };

	switch (proto) {

	case Q_DEFAULT:
	case Q_LINK:
		switch (cstate->linktype) {
		case DLT_ARCNET:
		case DLT_ARCNET_LINUX:
			return gen_ahostop(cstate, abroadcast, Q_DST);
		case DLT_EN10MB:
		case DLT_NETANALYZER:
		case DLT_NETANALYZER_TRANSPARENT:
			b1 = gen_prevlinkhdr_check(cstate);
			b0 = gen_ehostop(cstate, ebroadcast, Q_DST);
			if (b1 != NULL)
				gen_and(b1, b0);
			return b0;
		case DLT_FDDI:
			return gen_fhostop(cstate, ebroadcast, Q_DST);
		case DLT_IEEE802:
			return gen_thostop(cstate, ebroadcast, Q_DST);
		case DLT_IEEE802_11:
		case DLT_PRISM_HEADER:
		case DLT_IEEE802_11_RADIO_AVS:
		case DLT_IEEE802_11_RADIO:
		case DLT_PPI:
			return gen_wlanhostop(cstate, ebroadcast, Q_DST);
		case DLT_IP_OVER_FC:
			return gen_ipfchostop(cstate, ebroadcast, Q_DST);
		default:
			bpf_error(cstate, "not a broadcast link");
		}
		break;

	case Q_IP:
		/*
		 * We treat a netmask of PCAP_NETMASK_UNKNOWN (0xffffffff)
		 * as an indication that we don't know the netmask, and fail
		 * in that case.
		 */
		if (cstate->netmask == PCAP_NETMASK_UNKNOWN)
			bpf_error(cstate, "netmask not known, so 'ip broadcast' not supported");
		b0 = gen_linktype(cstate, ETHERTYPE_IP);
		hostmask = ~cstate->netmask;
		b1 = gen_mcmp(cstate, OR_LINKPL, 16, BPF_W, (bpf_int32)0, hostmask);
		b2 = gen_mcmp(cstate, OR_LINKPL, 16, BPF_W,
			      (bpf_int32)(~0 & hostmask), hostmask);
		gen_or(b1, b2);
		gen_and(b0, b2);
		return b2;
	}
	bpf_error(cstate, "only link-layer/IP broadcast filters supported");
	/* NOTREACHED */
}

/*
 * Generate code to test the low-order bit of a MAC address (that's
 * the bottom bit of the *first* byte).
 */
static struct block *
gen_mac_multicast(compiler_state_t *cstate, int offset)
{
	register struct block *b0;
	register struct slist *s;

	/* link[offset] & 1 != 0 */
	s = gen_load_a(cstate, OR_LINKHDR, offset, BPF_B);
	b0 = new_block(cstate, JMP(BPF_JSET));
	b0->s.k = 1;
	b0->stmts = s;
	return b0;
}

struct block *
gen_multicast(compiler_state_t *cstate, int proto)
{
	register struct block *b0, *b1, *b2;
	register struct slist *s;

	switch (proto) {

	case Q_DEFAULT:
	case Q_LINK:
		switch (cstate->linktype) {
		case DLT_ARCNET:
		case DLT_ARCNET_LINUX:
			/* all ARCnet multicasts use the same address */
			return gen_ahostop(cstate, abroadcast, Q_DST);
		case DLT_EN10MB:
		case DLT_NETANALYZER:
		case DLT_NETANALYZER_TRANSPARENT:
			b1 = gen_prevlinkhdr_check(cstate);
			/* ether[0] & 1 != 0 */
			b0 = gen_mac_multicast(cstate, 0);
			if (b1 != NULL)
				gen_and(b1, b0);
			return b0;
		case DLT_FDDI:
			/*
			 * XXX TEST THIS: MIGHT NOT PORT PROPERLY XXX
			 *
			 * XXX - was that referring to bit-order issues?
			 */
			/* fddi[1] & 1 != 0 */
			return gen_mac_multicast(cstate, 1);
		case DLT_IEEE802:
			/* tr[2] & 1 != 0 */
			return gen_mac_multicast(cstate, 2);
		case DLT_IEEE802_11:
		case DLT_PRISM_HEADER:
		case DLT_IEEE802_11_RADIO_AVS:
		case DLT_IEEE802_11_RADIO:
		case DLT_PPI:
			/*
			 * Oh, yuk.
			 *
			 *	For control frames, there is no DA.
			 *
			 *	For management frames, DA is at an
			 *	offset of 4 from the beginning of
			 *	the packet.
			 *
			 *	For data frames, DA is at an offset
			 *	of 4 from the beginning of the packet
			 *	if To DS is clear and at an offset of
			 *	16 from the beginning of the packet
			 *	if To DS is set.
			 */

			/*
			 * Generate the tests to be done for data frames.
			 *
			 * First, check for To DS set, i.e. "link[1] & 0x01".
			 */
			s = gen_load_a(cstate, OR_LINKHDR, 1, BPF_B);
			b1 = new_block(cstate, JMP(BPF_JSET));
			b1->s.k = 0x01;	/* To DS */
			b1->stmts = s;

			/*
			 * If To DS is set, the DA is at 16.
			 */
			b0 = gen_mac_multicast(cstate, 16);
			gen_and(b1, b0);

			/*
			 * Now, check for To DS not set, i.e. check
			 * "!(link[1] & 0x01)".
			 */
			s = gen_load_a(cstate, OR_LINKHDR, 1, BPF_B);
			b2 = new_block(cstate, JMP(BPF_JSET));
			b2->s.k = 0x01;	/* To DS */
			b2->stmts = s;
			gen_not(b2);

			/*
			 * If To DS is not set, the DA is at 4.
			 */
			b1 = gen_mac_multicast(cstate, 4);
			gen_and(b2, b1);

			/*
			 * Now OR together the last two checks.  That gives
			 * the complete set of checks for data frames.
			 */
			gen_or(b1, b0);

			/*
			 * Now check for a data frame.
			 * I.e, check "link[0] & 0x08".
			 */
			s = gen_load_a(cstate, OR_LINKHDR, 0, BPF_B);
			b1 = new_block(cstate, JMP(BPF_JSET));
			b1->s.k = 0x08;
			b1->stmts = s;

			/*
			 * AND that with the checks done for data frames.
			 */
			gen_and(b1, b0);

			/*
			 * If the high-order bit of the type value is 0, this
			 * is a management frame.
			 * I.e, check "!(link[0] & 0x08)".
			 */
			s = gen_load_a(cstate, OR_LINKHDR, 0, BPF_B);
			b2 = new_block(cstate, JMP(BPF_JSET));
			b2->s.k = 0x08;
			b2->stmts = s;
			gen_not(b2);

			/*
			 * For management frames, the DA is at 4.
			 */
			b1 = gen_mac_multicast(cstate, 4);
			gen_and(b2, b1);

			/*
			 * OR that with the checks done for data frames.
			 * That gives the checks done for management and
			 * data frames.
			 */
			gen_or(b1, b0);

			/*
			 * If the low-order bit of the type value is 1,
			 * this is either a control frame or a frame
			 * with a reserved type, and thus not a
			 * frame with an SA.
			 *
			 * I.e., check "!(link[0] & 0x04)".
			 */
			s = gen_load_a(cstate, OR_LINKHDR, 0, BPF_B);
			b1 = new_block(cstate, JMP(BPF_JSET));
			b1->s.k = 0x04;
			b1->stmts = s;
			gen_not(b1);

			/*
			 * AND that with the checks for data and management
			 * frames.
			 */
			gen_and(b1, b0);
			return b0;
		case DLT_IP_OVER_FC:
			b0 = gen_mac_multicast(cstate, 2);
			return b0;
		default:
			break;
		}
		/* Link not known to support multicasts */
		break;

	case Q_IP:
		b0 = gen_linktype(cstate, ETHERTYPE_IP);
		b1 = gen_cmp_ge(cstate, OR_LINKPL, 16, BPF_B, (bpf_int32)224);
		gen_and(b0, b1);
		return b1;

	case Q_IPV6:
		b0 = gen_linktype(cstate, ETHERTYPE_IPV6);
		b1 = gen_cmp(cstate, OR_LINKPL, 24, BPF_B, (bpf_int32)255);
		gen_and(b0, b1);
		return b1;
	}
	bpf_error(cstate, "link-layer multicast filters supported only on ethernet/FDDI/token ring/ARCNET/802.11/ATM LANE/Fibre Channel");
	/* NOTREACHED */
}

/*
 * Filter on inbound (dir == 0) or outbound (dir == 1) traffic.
 * Outbound traffic is sent by this machine, while inbound traffic is
 * sent by a remote machine (and may include packets destined for a
 * unicast or multicast link-layer address we are not subscribing to).
 * These are the same definitions implemented by pcap_setdirection().
 * Capturing only unicast traffic destined for this host is probably
 * better accomplished using a higher-layer filter.
 */
struct block *
gen_inbound(compiler_state_t *cstate, int dir)
{
	register struct block *b0;

	/*
	 * Only some data link types support inbound/outbound qualifiers.
	 */
	switch (cstate->linktype) {
	case DLT_SLIP:
		b0 = gen_relation(cstate, BPF_JEQ,
			  gen_load(cstate, Q_LINK, gen_loadi(cstate, 0), 1),
			  gen_loadi(cstate, 0),
			  dir);
		break;

	case DLT_IPNET:
		if (dir) {
			/* match outgoing packets */
			b0 = gen_cmp(cstate, OR_LINKHDR, 2, BPF_H, IPNET_OUTBOUND);
		} else {
			/* match incoming packets */
			b0 = gen_cmp(cstate, OR_LINKHDR, 2, BPF_H, IPNET_INBOUND);
		}
		break;

	case DLT_LINUX_SLL:
		/* match outgoing packets */
		b0 = gen_cmp(cstate, OR_LINKHDR, 0, BPF_H, LINUX_SLL_OUTGOING);
		if (!dir) {
			/* to filter on inbound traffic, invert the match */
			gen_not(b0);
		}
		break;

#ifdef HAVE_NET_PFVAR_H
	case DLT_PFLOG:
		b0 = gen_cmp(cstate, OR_LINKHDR, offsetof(struct pfloghdr, dir), BPF_B,
		    (bpf_int32)((dir == 0) ? PF_IN : PF_OUT));
		break;
#endif

	case DLT_PPP_PPPD:
		if (dir) {
			/* match outgoing packets */
			b0 = gen_cmp(cstate, OR_LINKHDR, 0, BPF_B, PPP_PPPD_OUT);
		} else {
			/* match incoming packets */
			b0 = gen_cmp(cstate, OR_LINKHDR, 0, BPF_B, PPP_PPPD_IN);
		}
		break;

        case DLT_JUNIPER_MFR:
        case DLT_JUNIPER_MLFR:
        case DLT_JUNIPER_MLPPP:
	case DLT_JUNIPER_ATM1:
	case DLT_JUNIPER_ATM2:
	case DLT_JUNIPER_PPPOE:
	case DLT_JUNIPER_PPPOE_ATM:
        case DLT_JUNIPER_GGSN:
        case DLT_JUNIPER_ES:
        case DLT_JUNIPER_MONITOR:
        case DLT_JUNIPER_SERVICES:
        case DLT_JUNIPER_ETHER:
        case DLT_JUNIPER_PPP:
        case DLT_JUNIPER_FRELAY:
        case DLT_JUNIPER_CHDLC:
        case DLT_JUNIPER_VP:
        case DLT_JUNIPER_ST:
        case DLT_JUNIPER_ISM:
        case DLT_JUNIPER_VS:
        case DLT_JUNIPER_SRX_E2E:
        case DLT_JUNIPER_FIBRECHANNEL:
	case DLT_JUNIPER_ATM_CEMIC:

		/* juniper flags (including direction) are stored
		 * the byte after the 3-byte magic number */
		if (dir) {
			/* match outgoing packets */
			b0 = gen_mcmp(cstate, OR_LINKHDR, 3, BPF_B, 0, 0x01);
		} else {
			/* match incoming packets */
			b0 = gen_mcmp(cstate, OR_LINKHDR, 3, BPF_B, 1, 0x01);
		}
		break;

	default:
		/*
		 * If we have packet meta-data indicating a direction,
		 * and that metadata can be checked by BPF code, check
		 * it.  Otherwise, give up, as this link-layer type has
		 * nothing in the packet data.
		 *
		 * Currently, the only platform where a BPF filter can
		 * check that metadata is Linux with the in-kernel
		 * BPF interpreter.  If other packet capture mechanisms
		 * and BPF filters also supported this, it would be
		 * nice.  It would be even better if they made that
		 * metadata available so that we could provide it
		 * with newer capture APIs, allowing it to be saved
		 * in pcapng files.
		 */
#if defined(linux) && defined(PF_PACKET) && defined(SO_ATTACH_FILTER)
		/*
		 * This is Linux with PF_PACKET support.
		 * If this is a *live* capture, we can look at
		 * special meta-data in the filter expression;
		 * if it's a savefile, we can't.
		 */
		if (cstate->bpf_pcap->rfile != NULL) {
			/* We have a FILE *, so this is a savefile */
			bpf_error(cstate, "inbound/outbound not supported on linktype %d when reading savefiles",
			    cstate->linktype);
			b0 = NULL;
			/* NOTREACHED */
		}
		/* match outgoing packets */
		b0 = gen_cmp(cstate, OR_LINKHDR, SKF_AD_OFF + SKF_AD_PKTTYPE, BPF_H,
		             PACKET_OUTGOING);
		if (!dir) {
			/* to filter on inbound traffic, invert the match */
			gen_not(b0);
		}
#else /* defined(linux) && defined(PF_PACKET) && defined(SO_ATTACH_FILTER) */
		bpf_error(cstate, "inbound/outbound not supported on linktype %d",
		    cstate->linktype);
		/* NOTREACHED */
#endif /* defined(linux) && defined(PF_PACKET) && defined(SO_ATTACH_FILTER) */
	}
	return (b0);
}

#ifdef HAVE_NET_PFVAR_H
/* PF firewall log matched interface */
struct block *
gen_pf_ifname(compiler_state_t *cstate, const char *ifname)
{
	struct block *b0;
	u_int len, off;

	if (cstate->linktype != DLT_PFLOG) {
		bpf_error(cstate, "ifname supported only on PF linktype");
		/* NOTREACHED */
	}
	len = sizeof(((struct pfloghdr *)0)->ifname);
	off = offsetof(struct pfloghdr, ifname);
	if (strlen(ifname) >= len) {
		bpf_error(cstate, "ifname interface names can only be %d characters",
		    len-1);
		/* NOTREACHED */
	}
	b0 = gen_bcmp(cstate, OR_LINKHDR, off, strlen(ifname), (const u_char *)ifname);
	return (b0);
}

/* PF firewall log ruleset name */
struct block *
gen_pf_ruleset(compiler_state_t *cstate, char *ruleset)
{
	struct block *b0;

	if (cstate->linktype != DLT_PFLOG) {
		bpf_error(cstate, "ruleset supported only on PF linktype");
		/* NOTREACHED */
	}

	if (strlen(ruleset) >= sizeof(((struct pfloghdr *)0)->ruleset)) {
		bpf_error(cstate, "ruleset names can only be %ld characters",
		    (long)(sizeof(((struct pfloghdr *)0)->ruleset) - 1));
		/* NOTREACHED */
	}

	b0 = gen_bcmp(cstate, OR_LINKHDR, offsetof(struct pfloghdr, ruleset),
	    strlen(ruleset), (const u_char *)ruleset);
	return (b0);
}

/* PF firewall log rule number */
struct block *
gen_pf_rnr(compiler_state_t *cstate, int rnr)
{
	struct block *b0;

	if (cstate->linktype != DLT_PFLOG) {
		bpf_error(cstate, "rnr supported only on PF linktype");
		/* NOTREACHED */
	}

	b0 = gen_cmp(cstate, OR_LINKHDR, offsetof(struct pfloghdr, rulenr), BPF_W,
		 (bpf_int32)rnr);
	return (b0);
}

/* PF firewall log sub-rule number */
struct block *
gen_pf_srnr(compiler_state_t *cstate, int srnr)
{
	struct block *b0;

	if (cstate->linktype != DLT_PFLOG) {
		bpf_error(cstate, "srnr supported only on PF linktype");
		/* NOTREACHED */
	}

	b0 = gen_cmp(cstate, OR_LINKHDR, offsetof(struct pfloghdr, subrulenr), BPF_W,
	    (bpf_int32)srnr);
	return (b0);
}

/* PF firewall log reason code */
struct block *
gen_pf_reason(compiler_state_t *cstate, int reason)
{
	struct block *b0;

	if (cstate->linktype != DLT_PFLOG) {
		bpf_error(cstate, "reason supported only on PF linktype");
		/* NOTREACHED */
	}

	b0 = gen_cmp(cstate, OR_LINKHDR, offsetof(struct pfloghdr, reason), BPF_B,
	    (bpf_int32)reason);
	return (b0);
}

/* PF firewall log action */
struct block *
gen_pf_action(compiler_state_t *cstate, int action)
{
	struct block *b0;

	if (cstate->linktype != DLT_PFLOG) {
		bpf_error(cstate, "action supported only on PF linktype");
		/* NOTREACHED */
	}

	b0 = gen_cmp(cstate, OR_LINKHDR, offsetof(struct pfloghdr, action), BPF_B,
	    (bpf_int32)action);
	return (b0);
}
#else /* !HAVE_NET_PFVAR_H */
struct block *
gen_pf_ifname(compiler_state_t *cstate, const char *ifname _U_)
{
	bpf_error(cstate, "libpcap was compiled without pf support");
	/* NOTREACHED */
}

struct block *
gen_pf_ruleset(compiler_state_t *cstate, char *ruleset _U_)
{
	bpf_error(cstate, "libpcap was compiled on a machine without pf support");
	/* NOTREACHED */
}

struct block *
gen_pf_rnr(compiler_state_t *cstate, int rnr _U_)
{
	bpf_error(cstate, "libpcap was compiled on a machine without pf support");
	/* NOTREACHED */
}

struct block *
gen_pf_srnr(compiler_state_t *cstate, int srnr _U_)
{
	bpf_error(cstate, "libpcap was compiled on a machine without pf support");
	/* NOTREACHED */
}

struct block *
gen_pf_reason(compiler_state_t *cstate, int reason _U_)
{
	bpf_error(cstate, "libpcap was compiled on a machine without pf support");
	/* NOTREACHED */
}

struct block *
gen_pf_action(compiler_state_t *cstate, int action _U_)
{
	bpf_error(cstate, "libpcap was compiled on a machine without pf support");
	/* NOTREACHED */
}
#endif /* HAVE_NET_PFVAR_H */

/* IEEE 802.11 wireless header */
struct block *
gen_p80211_type(compiler_state_t *cstate, int type, int mask)
{
	struct block *b0;

	switch (cstate->linktype) {

	case DLT_IEEE802_11:
	case DLT_PRISM_HEADER:
	case DLT_IEEE802_11_RADIO_AVS:
	case DLT_IEEE802_11_RADIO:
		b0 = gen_mcmp(cstate, OR_LINKHDR, 0, BPF_B, (bpf_int32)type,
		    (bpf_int32)mask);
		break;

	default:
		bpf_error(cstate, "802.11 link-layer types supported only on 802.11");
		/* NOTREACHED */
	}

	return (b0);
}

struct block *
gen_p80211_fcdir(compiler_state_t *cstate, int fcdir)
{
	struct block *b0;

	switch (cstate->linktype) {

	case DLT_IEEE802_11:
	case DLT_PRISM_HEADER:
	case DLT_IEEE802_11_RADIO_AVS:
	case DLT_IEEE802_11_RADIO:
		break;

	default:
		bpf_error(cstate, "frame direction supported only with 802.11 headers");
		/* NOTREACHED */
	}

	b0 = gen_mcmp(cstate, OR_LINKHDR, 1, BPF_B, (bpf_int32)fcdir,
		(bpf_u_int32)IEEE80211_FC1_DIR_MASK);

	return (b0);
}

struct block *
gen_acode(compiler_state_t *cstate, const u_char *eaddr, struct qual q)
{
	switch (cstate->linktype) {

	case DLT_ARCNET:
	case DLT_ARCNET_LINUX:
		if ((q.addr == Q_HOST || q.addr == Q_DEFAULT) &&
		    q.proto == Q_LINK)
			return (gen_ahostop(cstate, eaddr, (int)q.dir));
		else {
			bpf_error(cstate, "ARCnet address used in non-arc expression");
			/* NOTREACHED */
		}
		break;

	default:
		bpf_error(cstate, "aid supported only on ARCnet");
		/* NOTREACHED */
	}
}

static struct block *
gen_ahostop(compiler_state_t *cstate, const u_char *eaddr, int dir)
{
	register struct block *b0, *b1;

	switch (dir) {
	/* src comes first, different from Ethernet */
	case Q_SRC:
		return gen_bcmp(cstate, OR_LINKHDR, 0, 1, eaddr);

	case Q_DST:
		return gen_bcmp(cstate, OR_LINKHDR, 1, 1, eaddr);

	case Q_AND:
		b0 = gen_ahostop(cstate, eaddr, Q_SRC);
		b1 = gen_ahostop(cstate, eaddr, Q_DST);
		gen_and(b0, b1);
		return b1;

	case Q_DEFAULT:
	case Q_OR:
		b0 = gen_ahostop(cstate, eaddr, Q_SRC);
		b1 = gen_ahostop(cstate, eaddr, Q_DST);
		gen_or(b0, b1);
		return b1;

	case Q_ADDR1:
		bpf_error(cstate, "'addr1' and 'address1' are only supported on 802.11");
		break;

	case Q_ADDR2:
		bpf_error(cstate, "'addr2' and 'address2' are only supported on 802.11");
		break;

	case Q_ADDR3:
		bpf_error(cstate, "'addr3' and 'address3' are only supported on 802.11");
		break;

	case Q_ADDR4:
		bpf_error(cstate, "'addr4' and 'address4' are only supported on 802.11");
		break;

	case Q_RA:
		bpf_error(cstate, "'ra' is only supported on 802.11");
		break;

	case Q_TA:
		bpf_error(cstate, "'ta' is only supported on 802.11");
		break;
	}
	abort();
	/* NOTREACHED */
}

static struct block *
gen_vlan_tpid_test(compiler_state_t *cstate)
{
	struct block *b0, *b1;

	/* check for VLAN, including QinQ */
	b0 = gen_linktype(cstate, ETHERTYPE_8021Q);
	b1 = gen_linktype(cstate, ETHERTYPE_8021AD);
	gen_or(b0,b1);
	b0 = b1;
	b1 = gen_linktype(cstate, ETHERTYPE_8021QINQ);
	gen_or(b0,b1);

	return b1;
}

static struct block *
gen_vlan_vid_test(compiler_state_t *cstate, int vlan_num)
{
	return gen_mcmp(cstate, OR_LINKPL, 0, BPF_H, (bpf_int32)vlan_num, 0x0fff);
}

static struct block *
gen_vlan_no_bpf_extensions(compiler_state_t *cstate, int vlan_num)
{
	struct block *b0, *b1;

	b0 = gen_vlan_tpid_test(cstate);

	if (vlan_num >= 0) {
		b1 = gen_vlan_vid_test(cstate, vlan_num);
		gen_and(b0, b1);
		b0 = b1;
	}

	/*
	 * Both payload and link header type follow the VLAN tags so that
	 * both need to be updated.
	 */
	cstate->off_linkpl.constant_part += 4;
	cstate->off_linktype.constant_part += 4;

	return b0;
}

#if defined(SKF_AD_VLAN_TAG_PRESENT)
/* add v to variable part of off */
static void
gen_vlan_vloffset_add(compiler_state_t *cstate, bpf_abs_offset *off, int v, struct slist *s)
{
	struct slist *s2;

	if (!off->is_variable)
		off->is_variable = 1;
	if (off->reg == -1)
		off->reg = alloc_reg(cstate);

	s2 = new_stmt(cstate, BPF_LD|BPF_MEM);
	s2->s.k = off->reg;
	sappend(s, s2);
	s2 = new_stmt(cstate, BPF_ALU|BPF_ADD|BPF_IMM);
	s2->s.k = v;
	sappend(s, s2);
	s2 = new_stmt(cstate, BPF_ST);
	s2->s.k = off->reg;
	sappend(s, s2);
}

/*
 * patch block b_tpid (VLAN TPID test) to update variable parts of link payload
 * and link type offsets first
 */
static void
gen_vlan_patch_tpid_test(compiler_state_t *cstate, struct block *b_tpid)
{
	struct slist s;

	/* offset determined at run time, shift variable part */
	s.next = NULL;
	cstate->is_vlan_vloffset = 1;
	gen_vlan_vloffset_add(cstate, &cstate->off_linkpl, 4, &s);
	gen_vlan_vloffset_add(cstate, &cstate->off_linktype, 4, &s);

	/* we get a pointer to a chain of or-ed blocks, patch first of them */
	sappend(s.next, b_tpid->head->stmts);
	b_tpid->head->stmts = s.next;
}

/*
 * patch block b_vid (VLAN id test) to load VID value either from packet
 * metadata (using BPF extensions) if SKF_AD_VLAN_TAG_PRESENT is true
 */
static void
gen_vlan_patch_vid_test(compiler_state_t *cstate, struct block *b_vid)
{
	struct slist *s, *s2, *sjeq;
	unsigned cnt;

	s = new_stmt(cstate, BPF_LD|BPF_B|BPF_ABS);
	s->s.k = SKF_AD_OFF + SKF_AD_VLAN_TAG_PRESENT;

	/* true -> next instructions, false -> beginning of b_vid */
	sjeq = new_stmt(cstate, JMP(BPF_JEQ));
	sjeq->s.k = 1;
	sjeq->s.jf = b_vid->stmts;
	sappend(s, sjeq);

	s2 = new_stmt(cstate, BPF_LD|BPF_B|BPF_ABS);
	s2->s.k = SKF_AD_OFF + SKF_AD_VLAN_TAG;
	sappend(s, s2);
	sjeq->s.jt = s2;

	/* jump to the test in b_vid (bypass loading VID from packet data) */
	cnt = 0;
	for (s2 = b_vid->stmts; s2; s2 = s2->next)
		cnt++;
	s2 = new_stmt(cstate, JMP(BPF_JA));
	s2->s.k = cnt;
	sappend(s, s2);

	/* insert our statements at the beginning of b_vid */
	sappend(s, b_vid->stmts);
	b_vid->stmts = s;
}

/*
 * Generate check for "vlan" or "vlan <id>" on systems with support for BPF
 * extensions.  Even if kernel supports VLAN BPF extensions, (outermost) VLAN
 * tag can be either in metadata or in packet data; therefore if the
 * SKF_AD_VLAN_TAG_PRESENT test is negative, we need to check link
 * header for VLAN tag. As the decision is done at run time, we need
 * update variable part of the offsets
 */
static struct block *
gen_vlan_bpf_extensions(compiler_state_t *cstate, int vlan_num)
{
        struct block *b0, *b_tpid, *b_vid = NULL;
        struct slist *s;

        /* generate new filter code based on extracting packet
         * metadata */
        s = new_stmt(cstate, BPF_LD|BPF_B|BPF_ABS);
        s->s.k = SKF_AD_OFF + SKF_AD_VLAN_TAG_PRESENT;

        b0 = new_block(cstate, JMP(BPF_JEQ));
        b0->stmts = s;
        b0->s.k = 1;

	/*
	 * This is tricky. We need to insert the statements updating variable
	 * parts of offsets before the the traditional TPID and VID tests so
	 * that they are called whenever SKF_AD_VLAN_TAG_PRESENT fails but
	 * we do not want this update to affect those checks. That's why we
	 * generate both test blocks first and insert the statements updating
	 * variable parts of both offsets after that. This wouldn't work if
	 * there already were variable length link header when entering this
	 * function but gen_vlan_bpf_extensions() isn't called in that case.
	 */
	b_tpid = gen_vlan_tpid_test(cstate);
	if (vlan_num >= 0)
		b_vid = gen_vlan_vid_test(cstate, vlan_num);

	gen_vlan_patch_tpid_test(cstate, b_tpid);
	gen_or(b0, b_tpid);
	b0 = b_tpid;

	if (vlan_num >= 0) {
		gen_vlan_patch_vid_test(cstate, b_vid);
		gen_and(b0, b_vid);
		b0 = b_vid;
	}

        return b0;
}
#endif

/*
 * support IEEE 802.1Q VLAN trunk over ethernet
 */
struct block *
gen_vlan(compiler_state_t *cstate, int vlan_num)
{
	struct	block	*b0;

	/* can't check for VLAN-encapsulated packets inside MPLS */
	if (cstate->label_stack_depth > 0)
		bpf_error(cstate, "no VLAN match after MPLS");

	/*
	 * Check for a VLAN packet, and then change the offsets to point
	 * to the type and data fields within the VLAN packet.  Just
	 * increment the offsets, so that we can support a hierarchy, e.g.
	 * "vlan 300 && vlan 200" to capture VLAN 200 encapsulated within
	 * VLAN 100.
	 *
	 * XXX - this is a bit of a kludge.  If we were to split the
	 * compiler into a parser that parses an expression and
	 * generates an expression tree, and a code generator that
	 * takes an expression tree (which could come from our
	 * parser or from some other parser) and generates BPF code,
	 * we could perhaps make the offsets parameters of routines
	 * and, in the handler for an "AND" node, pass to subnodes
	 * other than the VLAN node the adjusted offsets.
	 *
	 * This would mean that "vlan" would, instead of changing the
	 * behavior of *all* tests after it, change only the behavior
	 * of tests ANDed with it.  That would change the documented
	 * semantics of "vlan", which might break some expressions.
	 * However, it would mean that "(vlan and ip) or ip" would check
	 * both for VLAN-encapsulated IP and IP-over-Ethernet, rather than
	 * checking only for VLAN-encapsulated IP, so that could still
	 * be considered worth doing; it wouldn't break expressions
	 * that are of the form "vlan and ..." or "vlan N and ...",
	 * which I suspect are the most common expressions involving
	 * "vlan".  "vlan or ..." doesn't necessarily do what the user
	 * would really want, now, as all the "or ..." tests would
	 * be done assuming a VLAN, even though the "or" could be viewed
	 * as meaning "or, if this isn't a VLAN packet...".
	 */
	switch (cstate->linktype) {

	case DLT_EN10MB:
	case DLT_NETANALYZER:
	case DLT_NETANALYZER_TRANSPARENT:
#if defined(SKF_AD_VLAN_TAG_PRESENT)
		/* Verify that this is the outer part of the packet and
		 * not encapsulated somehow. */
		if (cstate->vlan_stack_depth == 0 && !cstate->off_linkhdr.is_variable &&
		    cstate->off_linkhdr.constant_part ==
		    cstate->off_outermostlinkhdr.constant_part) {
			/*
			 * Do we need special VLAN handling?
			 */
			if (cstate->bpf_pcap->bpf_codegen_flags & BPF_SPECIAL_VLAN_HANDLING)
				b0 = gen_vlan_bpf_extensions(cstate, vlan_num);
			else
				b0 = gen_vlan_no_bpf_extensions(cstate, vlan_num);
		} else
#endif
			b0 = gen_vlan_no_bpf_extensions(cstate, vlan_num);
                break;

	case DLT_IEEE802_11:
	case DLT_PRISM_HEADER:
	case DLT_IEEE802_11_RADIO_AVS:
	case DLT_IEEE802_11_RADIO:
		b0 = gen_vlan_no_bpf_extensions(cstate, vlan_num);
		break;

	default:
		bpf_error(cstate, "no VLAN support for data link type %d",
		      cstate->linktype);
		/*NOTREACHED*/
	}

        cstate->vlan_stack_depth++;

	return (b0);
}

/*
 * support for MPLS
 */
struct block *
gen_mpls(compiler_state_t *cstate, int label_num)
{
	struct	block	*b0, *b1;

        if (cstate->label_stack_depth > 0) {
            /* just match the bottom-of-stack bit clear */
            b0 = gen_mcmp(cstate, OR_PREVMPLSHDR, 2, BPF_B, 0, 0x01);
        } else {
            /*
             * We're not in an MPLS stack yet, so check the link-layer
             * type against MPLS.
             */
            switch (cstate->linktype) {

            case DLT_C_HDLC: /* fall through */
            case DLT_EN10MB:
            case DLT_NETANALYZER:
            case DLT_NETANALYZER_TRANSPARENT:
                    b0 = gen_linktype(cstate, ETHERTYPE_MPLS);
                    break;

            case DLT_PPP:
                    b0 = gen_linktype(cstate, PPP_MPLS_UCAST);
                    break;

                    /* FIXME add other DLT_s ...
                     * for Frame-Relay/and ATM this may get messy due to SNAP headers
                     * leave it for now */

            default:
                    bpf_error(cstate, "no MPLS support for data link type %d",
                          cstate->linktype);
                    /*NOTREACHED*/
                    break;
            }
        }

	/* If a specific MPLS label is requested, check it */
	if (label_num >= 0) {
		label_num = label_num << 12; /* label is shifted 12 bits on the wire */
		b1 = gen_mcmp(cstate, OR_LINKPL, 0, BPF_W, (bpf_int32)label_num,
		    0xfffff000); /* only compare the first 20 bits */
		gen_and(b0, b1);
		b0 = b1;
	}

        /*
         * Change the offsets to point to the type and data fields within
         * the MPLS packet.  Just increment the offsets, so that we
         * can support a hierarchy, e.g. "mpls 100000 && mpls 1024" to
         * capture packets with an outer label of 100000 and an inner
         * label of 1024.
         *
         * Increment the MPLS stack depth as well; this indicates that
         * we're checking MPLS-encapsulated headers, to make sure higher
         * level code generators don't try to match against IP-related
         * protocols such as Q_ARP, Q_RARP etc.
         *
         * XXX - this is a bit of a kludge.  See comments in gen_vlan().
         */
        cstate->off_nl_nosnap += 4;
        cstate->off_nl += 4;
        cstate->label_stack_depth++;
	return (b0);
}

/*
 * Support PPPOE discovery and session.
 */
struct block *
gen_pppoed(compiler_state_t *cstate)
{
	/* check for PPPoE discovery */
	return gen_linktype(cstate, (bpf_int32)ETHERTYPE_PPPOED);
}

struct block *
gen_pppoes(compiler_state_t *cstate, int sess_num)
{
	struct block *b0, *b1;

	/*
	 * Test against the PPPoE session link-layer type.
	 */
	b0 = gen_linktype(cstate, (bpf_int32)ETHERTYPE_PPPOES);

	/* If a specific session is requested, check PPPoE session id */
	if (sess_num >= 0) {
		b1 = gen_mcmp(cstate, OR_LINKPL, 0, BPF_W,
		    (bpf_int32)sess_num, 0x0000ffff);
		gen_and(b0, b1);
		b0 = b1;
	}

	/*
	 * Change the offsets to point to the type and data fields within
	 * the PPP packet, and note that this is PPPoE rather than
	 * raw PPP.
	 *
	 * XXX - this is a bit of a kludge.  If we were to split the
	 * compiler into a parser that parses an expression and
	 * generates an expression tree, and a code generator that
	 * takes an expression tree (which could come from our
	 * parser or from some other parser) and generates BPF code,
	 * we could perhaps make the offsets parameters of routines
	 * and, in the handler for an "AND" node, pass to subnodes
	 * other than the PPPoE node the adjusted offsets.
	 *
	 * This would mean that "pppoes" would, instead of changing the
	 * behavior of *all* tests after it, change only the behavior
	 * of tests ANDed with it.  That would change the documented
	 * semantics of "pppoes", which might break some expressions.
	 * However, it would mean that "(pppoes and ip) or ip" would check
	 * both for VLAN-encapsulated IP and IP-over-Ethernet, rather than
	 * checking only for VLAN-encapsulated IP, so that could still
	 * be considered worth doing; it wouldn't break expressions
	 * that are of the form "pppoes and ..." which I suspect are the
	 * most common expressions involving "pppoes".  "pppoes or ..."
	 * doesn't necessarily do what the user would really want, now,
	 * as all the "or ..." tests would be done assuming PPPoE, even
	 * though the "or" could be viewed as meaning "or, if this isn't
	 * a PPPoE packet...".
	 *
	 * The "network-layer" protocol is PPPoE, which has a 6-byte
	 * PPPoE header, followed by a PPP packet.
	 *
	 * There is no HDLC encapsulation for the PPP packet (it's
	 * encapsulated in PPPoES instead), so the link-layer type
	 * starts at the first byte of the PPP packet.  For PPPoE,
	 * that offset is relative to the beginning of the total
	 * link-layer payload, including any 802.2 LLC header, so
	 * it's 6 bytes past cstate->off_nl.
	 */
	PUSH_LINKHDR(cstate, DLT_PPP, cstate->off_linkpl.is_variable,
	    cstate->off_linkpl.constant_part + cstate->off_nl + 6, /* 6 bytes past the PPPoE header */
	    cstate->off_linkpl.reg);

	cstate->off_linktype = cstate->off_linkhdr;
	cstate->off_linkpl.constant_part = cstate->off_linkhdr.constant_part + 2;

	cstate->off_nl = 0;
	cstate->off_nl_nosnap = 0;	/* no 802.2 LLC */

	return b0;
}

/* Check that this is Geneve and the VNI is correct if
 * specified. Parameterized to handle both IPv4 and IPv6. */
static struct block *
gen_geneve_check(compiler_state_t *cstate,
    struct block *(*gen_portfn)(compiler_state_t *, int, int, int),
    enum e_offrel offrel, int vni)
{
	struct block *b0, *b1;

	b0 = gen_portfn(cstate, GENEVE_PORT, IPPROTO_UDP, Q_DST);

	/* Check that we are operating on version 0. Otherwise, we
	 * can't decode the rest of the fields. The version is 2 bits
	 * in the first byte of the Geneve header. */
	b1 = gen_mcmp(cstate, offrel, 8, BPF_B, (bpf_int32)0, 0xc0);
	gen_and(b0, b1);
	b0 = b1;

	if (vni >= 0) {
		vni <<= 8; /* VNI is in the upper 3 bytes */
		b1 = gen_mcmp(cstate, offrel, 12, BPF_W, (bpf_int32)vni,
			      0xffffff00);
		gen_and(b0, b1);
		b0 = b1;
	}

	return b0;
}

/* The IPv4 and IPv6 Geneve checks need to do two things:
 * - Verify that this actually is Geneve with the right VNI.
 * - Place the IP header length (plus variable link prefix if
 *   needed) into register A to be used later to compute
 *   the inner packet offsets. */
static struct block *
gen_geneve4(compiler_state_t *cstate, int vni)
{
	struct block *b0, *b1;
	struct slist *s, *s1;

	b0 = gen_geneve_check(cstate, gen_port, OR_TRAN_IPV4, vni);

	/* Load the IP header length into A. */
	s = gen_loadx_iphdrlen(cstate);

	s1 = new_stmt(cstate, BPF_MISC|BPF_TXA);
	sappend(s, s1);

	/* Forcibly append these statements to the true condition
	 * of the protocol check by creating a new block that is
	 * always true and ANDing them. */
	b1 = new_block(cstate, BPF_JMP|BPF_JEQ|BPF_X);
	b1->stmts = s;
	b1->s.k = 0;

	gen_and(b0, b1);

	return b1;
}

static struct block *
gen_geneve6(compiler_state_t *cstate, int vni)
{
	struct block *b0, *b1;
	struct slist *s, *s1;

	b0 = gen_geneve_check(cstate, gen_port6, OR_TRAN_IPV6, vni);

	/* Load the IP header length. We need to account for a
	 * variable length link prefix if there is one. */
	s = gen_abs_offset_varpart(cstate, &cstate->off_linkpl);
	if (s) {
		s1 = new_stmt(cstate, BPF_LD|BPF_IMM);
		s1->s.k = 40;
		sappend(s, s1);

		s1 = new_stmt(cstate, BPF_ALU|BPF_ADD|BPF_X);
		s1->s.k = 0;
		sappend(s, s1);
	} else {
		s = new_stmt(cstate, BPF_LD|BPF_IMM);
		s->s.k = 40;
	}

	/* Forcibly append these statements to the true condition
	 * of the protocol check by creating a new block that is
	 * always true and ANDing them. */
	s1 = new_stmt(cstate, BPF_MISC|BPF_TAX);
	sappend(s, s1);

	b1 = new_block(cstate, BPF_JMP|BPF_JEQ|BPF_X);
	b1->stmts = s;
	b1->s.k = 0;

	gen_and(b0, b1);

	return b1;
}

/* We need to store three values based on the Geneve header::
 * - The offset of the linktype.
 * - The offset of the end of the Geneve header.
 * - The offset of the end of the encapsulated MAC header. */
static struct slist *
gen_geneve_offsets(compiler_state_t *cstate)
{
	struct slist *s, *s1, *s_proto;

	/* First we need to calculate the offset of the Geneve header
	 * itself. This is composed of the IP header previously calculated
	 * (include any variable link prefix) and stored in A plus the
	 * fixed sized headers (fixed link prefix, MAC length, and UDP
	 * header). */
	s = new_stmt(cstate, BPF_ALU|BPF_ADD|BPF_K);
	s->s.k = cstate->off_linkpl.constant_part + cstate->off_nl + 8;

	/* Stash this in X since we'll need it later. */
	s1 = new_stmt(cstate, BPF_MISC|BPF_TAX);
	sappend(s, s1);

	/* The EtherType in Geneve is 2 bytes in. Calculate this and
	 * store it. */
	s1 = new_stmt(cstate, BPF_ALU|BPF_ADD|BPF_K);
	s1->s.k = 2;
	sappend(s, s1);

	cstate->off_linktype.reg = alloc_reg(cstate);
	cstate->off_linktype.is_variable = 1;
	cstate->off_linktype.constant_part = 0;

	s1 = new_stmt(cstate, BPF_ST);
	s1->s.k = cstate->off_linktype.reg;
	sappend(s, s1);

	/* Load the Geneve option length and mask and shift to get the
	 * number of bytes. It is stored in the first byte of the Geneve
	 * header. */
	s1 = new_stmt(cstate, BPF_LD|BPF_IND|BPF_B);
	s1->s.k = 0;
	sappend(s, s1);

	s1 = new_stmt(cstate, BPF_ALU|BPF_AND|BPF_K);
	s1->s.k = 0x3f;
	sappend(s, s1);

	s1 = new_stmt(cstate, BPF_ALU|BPF_MUL|BPF_K);
	s1->s.k = 4;
	sappend(s, s1);

	/* Add in the rest of the Geneve base header. */
	s1 = new_stmt(cstate, BPF_ALU|BPF_ADD|BPF_K);
	s1->s.k = 8;
	sappend(s, s1);

	/* Add the Geneve header length to its offset and store. */
	s1 = new_stmt(cstate, BPF_ALU|BPF_ADD|BPF_X);
	s1->s.k = 0;
	sappend(s, s1);

	/* Set the encapsulated type as Ethernet. Even though we may
	 * not actually have Ethernet inside there are two reasons this
	 * is useful:
	 * - The linktype field is always in EtherType format regardless
	 *   of whether it is in Geneve or an inner Ethernet frame.
	 * - The only link layer that we have specific support for is
	 *   Ethernet. We will confirm that the packet actually is
	 *   Ethernet at runtime before executing these checks. */
	PUSH_LINKHDR(cstate, DLT_EN10MB, 1, 0, alloc_reg(cstate));

	s1 = new_stmt(cstate, BPF_ST);
	s1->s.k = cstate->off_linkhdr.reg;
	sappend(s, s1);

	/* Calculate whether we have an Ethernet header or just raw IP/
	 * MPLS/etc. If we have Ethernet, advance the end of the MAC offset
	 * and linktype by 14 bytes so that the network header can be found
	 * seamlessly. Otherwise, keep what we've calculated already. */

	/* We have a bare jmp so we can't use the optimizer. */
	cstate->no_optimize = 1;

	/* Load the EtherType in the Geneve header, 2 bytes in. */
	s1 = new_stmt(cstate, BPF_LD|BPF_IND|BPF_H);
	s1->s.k = 2;
	sappend(s, s1);

	/* Load X with the end of the Geneve header. */
	s1 = new_stmt(cstate, BPF_LDX|BPF_MEM);
	s1->s.k = cstate->off_linkhdr.reg;
	sappend(s, s1);

	/* Check if the EtherType is Transparent Ethernet Bridging. At the
	 * end of this check, we should have the total length in X. In
	 * the non-Ethernet case, it's already there. */
	s_proto = new_stmt(cstate, JMP(BPF_JEQ));
	s_proto->s.k = ETHERTYPE_TEB;
	sappend(s, s_proto);

	s1 = new_stmt(cstate, BPF_MISC|BPF_TXA);
	sappend(s, s1);
	s_proto->s.jt = s1;

	/* Since this is Ethernet, use the EtherType of the payload
	 * directly as the linktype. Overwrite what we already have. */
	s1 = new_stmt(cstate, BPF_ALU|BPF_ADD|BPF_K);
	s1->s.k = 12;
	sappend(s, s1);

	s1 = new_stmt(cstate, BPF_ST);
	s1->s.k = cstate->off_linktype.reg;
	sappend(s, s1);

	/* Advance two bytes further to get the end of the Ethernet
	 * header. */
	s1 = new_stmt(cstate, BPF_ALU|BPF_ADD|BPF_K);
	s1->s.k = 2;
	sappend(s, s1);

	/* Move the result to X. */
	s1 = new_stmt(cstate, BPF_MISC|BPF_TAX);
	sappend(s, s1);

	/* Store the final result of our linkpl calculation. */
	cstate->off_linkpl.reg = alloc_reg(cstate);
	cstate->off_linkpl.is_variable = 1;
	cstate->off_linkpl.constant_part = 0;

	s1 = new_stmt(cstate, BPF_STX);
	s1->s.k = cstate->off_linkpl.reg;
	sappend(s, s1);
	s_proto->s.jf = s1;

	cstate->off_nl = 0;

	return s;
}

/* Check to see if this is a Geneve packet. */
struct block *
gen_geneve(compiler_state_t *cstate, int vni)
{
	struct block *b0, *b1;
	struct slist *s;

	b0 = gen_geneve4(cstate, vni);
	b1 = gen_geneve6(cstate, vni);

	gen_or(b0, b1);
	b0 = b1;

	/* Later filters should act on the payload of the Geneve frame,
	 * update all of the header pointers. Attach this code so that
	 * it gets executed in the event that the Geneve filter matches. */
	s = gen_geneve_offsets(cstate);

	b1 = gen_true(cstate);
	sappend(s, b1->stmts);
	b1->stmts = s;

	gen_and(b0, b1);

	cstate->is_geneve = 1;

	return b1;
}

/* Check that the encapsulated frame has a link layer header
 * for Ethernet filters. */
static struct block *
gen_geneve_ll_check(compiler_state_t *cstate)
{
	struct block *b0;
	struct slist *s, *s1;

	/* The easiest way to see if there is a link layer present
	 * is to check if the link layer header and payload are not
	 * the same. */

	/* Geneve always generates pure variable offsets so we can
	 * compare only the registers. */
	s = new_stmt(cstate, BPF_LD|BPF_MEM);
	s->s.k = cstate->off_linkhdr.reg;

	s1 = new_stmt(cstate, BPF_LDX|BPF_MEM);
	s1->s.k = cstate->off_linkpl.reg;
	sappend(s, s1);

	b0 = new_block(cstate, BPF_JMP|BPF_JEQ|BPF_X);
	b0->stmts = s;
	b0->s.k = 0;
	gen_not(b0);

	return b0;
}

struct block *
gen_atmfield_code(compiler_state_t *cstate, int atmfield, bpf_int32 jvalue,
    bpf_u_int32 jtype, int reverse)
{
	struct block *b0;

	switch (atmfield) {

	case A_VPI:
		if (!cstate->is_atm)
			bpf_error(cstate, "'vpi' supported only on raw ATM");
		if (cstate->off_vpi == OFFSET_NOT_SET)
			abort();
		b0 = gen_ncmp(cstate, OR_LINKHDR, cstate->off_vpi, BPF_B, 0xffffffff, jtype,
		    reverse, jvalue);
		break;

	case A_VCI:
		if (!cstate->is_atm)
			bpf_error(cstate, "'vci' supported only on raw ATM");
		if (cstate->off_vci == OFFSET_NOT_SET)
			abort();
		b0 = gen_ncmp(cstate, OR_LINKHDR, cstate->off_vci, BPF_H, 0xffffffff, jtype,
		    reverse, jvalue);
		break;

	case A_PROTOTYPE:
		if (cstate->off_proto == OFFSET_NOT_SET)
			abort();	/* XXX - this isn't on FreeBSD */
		b0 = gen_ncmp(cstate, OR_LINKHDR, cstate->off_proto, BPF_B, 0x0f, jtype,
		    reverse, jvalue);
		break;

	case A_MSGTYPE:
		if (cstate->off_payload == OFFSET_NOT_SET)
			abort();
		b0 = gen_ncmp(cstate, OR_LINKHDR, cstate->off_payload + MSG_TYPE_POS, BPF_B,
		    0xffffffff, jtype, reverse, jvalue);
		break;

	case A_CALLREFTYPE:
		if (!cstate->is_atm)
			bpf_error(cstate, "'callref' supported only on raw ATM");
		if (cstate->off_proto == OFFSET_NOT_SET)
			abort();
		b0 = gen_ncmp(cstate, OR_LINKHDR, cstate->off_proto, BPF_B, 0xffffffff,
		    jtype, reverse, jvalue);
		break;

	default:
		abort();
	}
	return b0;
}

struct block *
gen_atmtype_abbrev(compiler_state_t *cstate, int type)
{
	struct block *b0, *b1;

	switch (type) {

	case A_METAC:
		/* Get all packets in Meta signalling Circuit */
		if (!cstate->is_atm)
			bpf_error(cstate, "'metac' supported only on raw ATM");
		b0 = gen_atmfield_code(cstate, A_VPI, 0, BPF_JEQ, 0);
		b1 = gen_atmfield_code(cstate, A_VCI, 1, BPF_JEQ, 0);
		gen_and(b0, b1);
		break;

	case A_BCC:
		/* Get all packets in Broadcast Circuit*/
		if (!cstate->is_atm)
			bpf_error(cstate, "'bcc' supported only on raw ATM");
		b0 = gen_atmfield_code(cstate, A_VPI, 0, BPF_JEQ, 0);
		b1 = gen_atmfield_code(cstate, A_VCI, 2, BPF_JEQ, 0);
		gen_and(b0, b1);
		break;

	case A_OAMF4SC:
		/* Get all cells in Segment OAM F4 circuit*/
		if (!cstate->is_atm)
			bpf_error(cstate, "'oam4sc' supported only on raw ATM");
		b0 = gen_atmfield_code(cstate, A_VPI, 0, BPF_JEQ, 0);
		b1 = gen_atmfield_code(cstate, A_VCI, 3, BPF_JEQ, 0);
		gen_and(b0, b1);
		break;

	case A_OAMF4EC:
		/* Get all cells in End-to-End OAM F4 Circuit*/
		if (!cstate->is_atm)
			bpf_error(cstate, "'oam4ec' supported only on raw ATM");
		b0 = gen_atmfield_code(cstate, A_VPI, 0, BPF_JEQ, 0);
		b1 = gen_atmfield_code(cstate, A_VCI, 4, BPF_JEQ, 0);
		gen_and(b0, b1);
		break;

	case A_SC:
		/*  Get all packets in connection Signalling Circuit */
		if (!cstate->is_atm)
			bpf_error(cstate, "'sc' supported only on raw ATM");
		b0 = gen_atmfield_code(cstate, A_VPI, 0, BPF_JEQ, 0);
		b1 = gen_atmfield_code(cstate, A_VCI, 5, BPF_JEQ, 0);
		gen_and(b0, b1);
		break;

	case A_ILMIC:
		/* Get all packets in ILMI Circuit */
		if (!cstate->is_atm)
			bpf_error(cstate, "'ilmic' supported only on raw ATM");
		b0 = gen_atmfield_code(cstate, A_VPI, 0, BPF_JEQ, 0);
		b1 = gen_atmfield_code(cstate, A_VCI, 16, BPF_JEQ, 0);
		gen_and(b0, b1);
		break;

	case A_LANE:
		/* Get all LANE packets */
		if (!cstate->is_atm)
			bpf_error(cstate, "'lane' supported only on raw ATM");
		b1 = gen_atmfield_code(cstate, A_PROTOTYPE, PT_LANE, BPF_JEQ, 0);

		/*
		 * Arrange that all subsequent tests assume LANE
		 * rather than LLC-encapsulated packets, and set
		 * the offsets appropriately for LANE-encapsulated
		 * Ethernet.
		 *
		 * We assume LANE means Ethernet, not Token Ring.
		 */
		PUSH_LINKHDR(cstate, DLT_EN10MB, 0,
		    cstate->off_payload + 2,	/* Ethernet header */
		    -1);
		cstate->off_linktype.constant_part = cstate->off_linkhdr.constant_part + 12;
		cstate->off_linkpl.constant_part = cstate->off_linkhdr.constant_part + 14;	/* Ethernet */
		cstate->off_nl = 0;			/* Ethernet II */
		cstate->off_nl_nosnap = 3;		/* 802.3+802.2 */
		break;

	case A_LLC:
		/* Get all LLC-encapsulated packets */
		if (!cstate->is_atm)
			bpf_error(cstate, "'llc' supported only on raw ATM");
		b1 = gen_atmfield_code(cstate, A_PROTOTYPE, PT_LLC, BPF_JEQ, 0);
		cstate->linktype = cstate->prevlinktype;
		break;

	default:
		abort();
	}
	return b1;
}

/*
 * Filtering for MTP2 messages based on li value
 * FISU, length is null
 * LSSU, length is 1 or 2
 * MSU, length is 3 or more
 * For MTP2_HSL, sequences are on 2 bytes, and length on 9 bits
 */
struct block *
gen_mtp2type_abbrev(compiler_state_t *cstate, int type)
{
	struct block *b0, *b1;

	switch (type) {

	case M_FISU:
		if ( (cstate->linktype != DLT_MTP2) &&
		     (cstate->linktype != DLT_ERF) &&
		     (cstate->linktype != DLT_MTP2_WITH_PHDR) )
			bpf_error(cstate, "'fisu' supported only on MTP2");
		/* gen_ncmp(cstate, offrel, offset, size, mask, jtype, reverse, value) */
		b0 = gen_ncmp(cstate, OR_PACKET, cstate->off_li, BPF_B, 0x3f, BPF_JEQ, 0, 0);
		break;

	case M_LSSU:
		if ( (cstate->linktype != DLT_MTP2) &&
		     (cstate->linktype != DLT_ERF) &&
		     (cstate->linktype != DLT_MTP2_WITH_PHDR) )
			bpf_error(cstate, "'lssu' supported only on MTP2");
		b0 = gen_ncmp(cstate, OR_PACKET, cstate->off_li, BPF_B, 0x3f, BPF_JGT, 1, 2);
		b1 = gen_ncmp(cstate, OR_PACKET, cstate->off_li, BPF_B, 0x3f, BPF_JGT, 0, 0);
		gen_and(b1, b0);
		break;

	case M_MSU:
		if ( (cstate->linktype != DLT_MTP2) &&
		     (cstate->linktype != DLT_ERF) &&
		     (cstate->linktype != DLT_MTP2_WITH_PHDR) )
			bpf_error(cstate, "'msu' supported only on MTP2");
		b0 = gen_ncmp(cstate, OR_PACKET, cstate->off_li, BPF_B, 0x3f, BPF_JGT, 0, 2);
		break;

	case MH_FISU:
		if ( (cstate->linktype != DLT_MTP2) &&
		     (cstate->linktype != DLT_ERF) &&
		     (cstate->linktype != DLT_MTP2_WITH_PHDR) )
			bpf_error(cstate, "'hfisu' supported only on MTP2_HSL");
		/* gen_ncmp(cstate, offrel, offset, size, mask, jtype, reverse, value) */
		b0 = gen_ncmp(cstate, OR_PACKET, cstate->off_li_hsl, BPF_H, 0xff80, BPF_JEQ, 0, 0);
		break;

	case MH_LSSU:
		if ( (cstate->linktype != DLT_MTP2) &&
		     (cstate->linktype != DLT_ERF) &&
		     (cstate->linktype != DLT_MTP2_WITH_PHDR) )
			bpf_error(cstate, "'hlssu' supported only on MTP2_HSL");
		b0 = gen_ncmp(cstate, OR_PACKET, cstate->off_li_hsl, BPF_H, 0xff80, BPF_JGT, 1, 0x0100);
		b1 = gen_ncmp(cstate, OR_PACKET, cstate->off_li_hsl, BPF_H, 0xff80, BPF_JGT, 0, 0);
		gen_and(b1, b0);
		break;

	case MH_MSU:
		if ( (cstate->linktype != DLT_MTP2) &&
		     (cstate->linktype != DLT_ERF) &&
		     (cstate->linktype != DLT_MTP2_WITH_PHDR) )
			bpf_error(cstate, "'hmsu' supported only on MTP2_HSL");
		b0 = gen_ncmp(cstate, OR_PACKET, cstate->off_li_hsl, BPF_H, 0xff80, BPF_JGT, 0, 0x0100);
		break;

	default:
		abort();
	}
	return b0;
}

struct block *
gen_mtp3field_code(compiler_state_t *cstate, int mtp3field, bpf_u_int32 jvalue,
    bpf_u_int32 jtype, int reverse)
{
	struct block *b0;
	bpf_u_int32 val1 , val2 , val3;
	u_int newoff_sio = cstate->off_sio;
	u_int newoff_opc = cstate->off_opc;
	u_int newoff_dpc = cstate->off_dpc;
	u_int newoff_sls = cstate->off_sls;

	switch (mtp3field) {

	case MH_SIO:
		newoff_sio += 3; /* offset for MTP2_HSL */
		/* FALLTHROUGH */

	case M_SIO:
		if (cstate->off_sio == OFFSET_NOT_SET)
			bpf_error(cstate, "'sio' supported only on SS7");
		/* sio coded on 1 byte so max value 255 */
		if(jvalue > 255)
		        bpf_error(cstate, "sio value %u too big; max value = 255",
		            jvalue);
		b0 = gen_ncmp(cstate, OR_PACKET, newoff_sio, BPF_B, 0xffffffff,
		    (u_int)jtype, reverse, (u_int)jvalue);
		break;

	case MH_OPC:
		newoff_opc+=3;
        case M_OPC:
	        if (cstate->off_opc == OFFSET_NOT_SET)
			bpf_error(cstate, "'opc' supported only on SS7");
		/* opc coded on 14 bits so max value 16383 */
		if (jvalue > 16383)
		        bpf_error(cstate, "opc value %u too big; max value = 16383",
		            jvalue);
		/* the following instructions are made to convert jvalue
		 * to the form used to write opc in an ss7 message*/
		val1 = jvalue & 0x00003c00;
		val1 = val1 >>10;
		val2 = jvalue & 0x000003fc;
		val2 = val2 <<6;
		val3 = jvalue & 0x00000003;
		val3 = val3 <<22;
		jvalue = val1 + val2 + val3;
		b0 = gen_ncmp(cstate, OR_PACKET, newoff_opc, BPF_W, 0x00c0ff0f,
		    (u_int)jtype, reverse, (u_int)jvalue);
		break;

	case MH_DPC:
		newoff_dpc += 3;
		/* FALLTHROUGH */

	case M_DPC:
	        if (cstate->off_dpc == OFFSET_NOT_SET)
			bpf_error(cstate, "'dpc' supported only on SS7");
		/* dpc coded on 14 bits so max value 16383 */
		if (jvalue > 16383)
		        bpf_error(cstate, "dpc value %u too big; max value = 16383",
		            jvalue);
		/* the following instructions are made to convert jvalue
		 * to the forme used to write dpc in an ss7 message*/
		val1 = jvalue & 0x000000ff;
		val1 = val1 << 24;
		val2 = jvalue & 0x00003f00;
		val2 = val2 << 8;
		jvalue = val1 + val2;
		b0 = gen_ncmp(cstate, OR_PACKET, newoff_dpc, BPF_W, 0xff3f0000,
		    (u_int)jtype, reverse, (u_int)jvalue);
		break;

	case MH_SLS:
	  newoff_sls+=3;
	case M_SLS:
	        if (cstate->off_sls == OFFSET_NOT_SET)
			bpf_error(cstate, "'sls' supported only on SS7");
		/* sls coded on 4 bits so max value 15 */
		if (jvalue > 15)
		         bpf_error(cstate, "sls value %u too big; max value = 15",
		             jvalue);
		/* the following instruction is made to convert jvalue
		 * to the forme used to write sls in an ss7 message*/
		jvalue = jvalue << 4;
		b0 = gen_ncmp(cstate, OR_PACKET, newoff_sls, BPF_B, 0xf0,
		    (u_int)jtype,reverse, (u_int)jvalue);
		break;

	default:
		abort();
	}
	return b0;
}

static struct block *
gen_msg_abbrev(compiler_state_t *cstate, int type)
{
	struct block *b1;

	/*
	 * Q.2931 signalling protocol messages for handling virtual circuits
	 * establishment and teardown
	 */
	switch (type) {

	case A_SETUP:
		b1 = gen_atmfield_code(cstate, A_MSGTYPE, SETUP, BPF_JEQ, 0);
		break;

	case A_CALLPROCEED:
		b1 = gen_atmfield_code(cstate, A_MSGTYPE, CALL_PROCEED, BPF_JEQ, 0);
		break;

	case A_CONNECT:
		b1 = gen_atmfield_code(cstate, A_MSGTYPE, CONNECT, BPF_JEQ, 0);
		break;

	case A_CONNECTACK:
		b1 = gen_atmfield_code(cstate, A_MSGTYPE, CONNECT_ACK, BPF_JEQ, 0);
		break;

	case A_RELEASE:
		b1 = gen_atmfield_code(cstate, A_MSGTYPE, RELEASE, BPF_JEQ, 0);
		break;

	case A_RELEASE_DONE:
		b1 = gen_atmfield_code(cstate, A_MSGTYPE, RELEASE_DONE, BPF_JEQ, 0);
		break;

	default:
		abort();
	}
	return b1;
}

struct block *
gen_atmmulti_abbrev(compiler_state_t *cstate, int type)
{
	struct block *b0, *b1;

	switch (type) {

	case A_OAM:
		if (!cstate->is_atm)
			bpf_error(cstate, "'oam' supported only on raw ATM");
		b1 = gen_atmmulti_abbrev(cstate, A_OAMF4);
		break;

	case A_OAMF4:
		if (!cstate->is_atm)
			bpf_error(cstate, "'oamf4' supported only on raw ATM");
		/* OAM F4 type */
		b0 = gen_atmfield_code(cstate, A_VCI, 3, BPF_JEQ, 0);
		b1 = gen_atmfield_code(cstate, A_VCI, 4, BPF_JEQ, 0);
		gen_or(b0, b1);
		b0 = gen_atmfield_code(cstate, A_VPI, 0, BPF_JEQ, 0);
		gen_and(b0, b1);
		break;

	case A_CONNECTMSG:
		/*
		 * Get Q.2931 signalling messages for switched
		 * virtual connection
		 */
		if (!cstate->is_atm)
			bpf_error(cstate, "'connectmsg' supported only on raw ATM");
		b0 = gen_msg_abbrev(cstate, A_SETUP);
		b1 = gen_msg_abbrev(cstate, A_CALLPROCEED);
		gen_or(b0, b1);
		b0 = gen_msg_abbrev(cstate, A_CONNECT);
		gen_or(b0, b1);
		b0 = gen_msg_abbrev(cstate, A_CONNECTACK);
		gen_or(b0, b1);
		b0 = gen_msg_abbrev(cstate, A_RELEASE);
		gen_or(b0, b1);
		b0 = gen_msg_abbrev(cstate, A_RELEASE_DONE);
		gen_or(b0, b1);
		b0 = gen_atmtype_abbrev(cstate, A_SC);
		gen_and(b0, b1);
		break;

	case A_METACONNECT:
		if (!cstate->is_atm)
			bpf_error(cstate, "'metaconnect' supported only on raw ATM");
		b0 = gen_msg_abbrev(cstate, A_SETUP);
		b1 = gen_msg_abbrev(cstate, A_CALLPROCEED);
		gen_or(b0, b1);
		b0 = gen_msg_abbrev(cstate, A_CONNECT);
		gen_or(b0, b1);
		b0 = gen_msg_abbrev(cstate, A_RELEASE);
		gen_or(b0, b1);
		b0 = gen_msg_abbrev(cstate, A_RELEASE_DONE);
		gen_or(b0, b1);
		b0 = gen_atmtype_abbrev(cstate, A_METAC);
		gen_and(b0, b1);
		break;

	default:
		abort();
	}
	return b1;
}
