/*
 * Copyright (c) 1990, 1991, 1992, 1993, 1994, 1995, 1996
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

#include "pcap/funcattrs.h"

/*
 * ATM support:
 *
 * Copyright (c) 1997 Yen Yen Lim and North Dakota State University
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. All advertising materials mentioning features or use of this software
 *    must display the following acknowledgement:
 *      This product includes software developed by Yen Yen Lim and
 *      North Dakota State University
 * 4. The name of the author may not be used to endorse or promote products
 *    derived from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT,
 * INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
 * STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN
 * ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

/* Address qualifiers. */

#define Q_HOST		1
#define Q_NET		2
#define Q_PORT		3
#define Q_GATEWAY	4
#define Q_PROTO		5
#define Q_PROTOCHAIN	6
#define Q_PORTRANGE	7

/* Protocol qualifiers. */

#define Q_LINK		1
#define Q_IP		2
#define Q_ARP		3
#define Q_RARP		4
#define Q_SCTP		5
#define Q_TCP		6
#define Q_UDP		7
#define Q_ICMP		8
#define Q_IGMP		9
#define Q_IGRP		10


#define	Q_ATALK		11
#define	Q_DECNET	12
#define	Q_LAT		13
#define Q_SCA		14
#define	Q_MOPRC		15
#define	Q_MOPDL		16


#define Q_IPV6		17
#define Q_ICMPV6	18
#define Q_AH		19
#define Q_ESP		20

#define Q_PIM		21
#define Q_VRRP		22

#define Q_AARP		23

#define Q_ISO		24
#define Q_ESIS		25
#define Q_ISIS		26
#define Q_CLNP		27

#define Q_STP		28

#define Q_IPX		29

#define Q_NETBEUI	30

/* IS-IS Levels */
#define Q_ISIS_L1       31
#define Q_ISIS_L2       32
/* PDU types */
#define Q_ISIS_IIH      33
#define Q_ISIS_LAN_IIH  34
#define Q_ISIS_PTP_IIH  35
#define Q_ISIS_SNP      36
#define Q_ISIS_CSNP     37
#define Q_ISIS_PSNP     38
#define Q_ISIS_LSP      39

#define Q_RADIO		40

#define Q_CARP		41

/* Directional qualifiers. */

#define Q_SRC		1
#define Q_DST		2
#define Q_OR		3
#define Q_AND		4
#define Q_ADDR1		5
#define Q_ADDR2		6
#define Q_ADDR3		7
#define Q_ADDR4		8
#define Q_RA		9
#define Q_TA		10

#define Q_DEFAULT	0
#define Q_UNDEF		255

/* ATM types */
#define A_METAC		22	/* Meta signalling Circuit */
#define A_BCC		23	/* Broadcast Circuit */
#define A_OAMF4SC	24	/* Segment OAM F4 Circuit */
#define A_OAMF4EC	25	/* End-to-End OAM F4 Circuit */
#define A_SC		26	/* Signalling Circuit*/
#define A_ILMIC		27	/* ILMI Circuit */
#define A_OAM		28	/* OAM cells : F4 only */
#define A_OAMF4		29	/* OAM F4 cells: Segment + End-to-end */
#define A_LANE		30	/* LANE traffic */
#define A_LLC		31	/* LLC-encapsulated traffic */

/* Based on Q.2931 signalling protocol */
#define A_SETUP		41	/* Setup message */
#define A_CALLPROCEED	42	/* Call proceeding message */
#define A_CONNECT	43	/* Connect message */
#define A_CONNECTACK	44	/* Connect Ack message */
#define A_RELEASE	45	/* Release message */
#define A_RELEASE_DONE	46	/* Release message */

/* ATM field types */
#define A_VPI		51
#define A_VCI		52
#define A_PROTOTYPE	53
#define A_MSGTYPE	54
#define A_CALLREFTYPE	55

#define A_CONNECTMSG	70	/* returns Q.2931 signalling messages for
				   establishing and destroying switched
				   virtual connection */
#define A_METACONNECT	71	/* returns Q.2931 signalling messages for
				   establishing and destroying predefined
				   virtual circuits, such as broadcast
				   circuit, oamf4 segment circuit, oamf4
				   end-to-end circuits, ILMI circuits or
				   connection signalling circuit. */

/* MTP2 types */
#define M_FISU		22	/* FISU */
#define M_LSSU		23	/* LSSU */
#define M_MSU		24	/* MSU */

/* MTP2 HSL types */
#define MH_FISU		25	/* FISU for HSL */
#define MH_LSSU		26	/* LSSU */
#define MH_MSU		27	/* MSU */

/* MTP3 field types */
#define M_SIO		1
#define M_OPC		2
#define M_DPC		3
#define M_SLS		4

/* MTP3 field types in case of MTP2 HSL */
#define MH_SIO		5
#define MH_OPC		6
#define MH_DPC		7
#define MH_SLS		8


struct slist;

struct stmt {
	int code;
	struct slist *jt;	/*only for relative jump in block*/
	struct slist *jf;	/*only for relative jump in block*/
	bpf_int32 k;
};

struct slist {
	struct stmt s;
	struct slist *next;
};

/*
 * A bit vector to represent definition sets.  We assume TOT_REGISTERS
 * is smaller than 8*sizeof(atomset).
 */
typedef bpf_u_int32 atomset;
#define ATOMMASK(n) (1 << (n))
#define ATOMELEM(d, n) (d & ATOMMASK(n))

/*
 * An unbounded set.
 */
typedef bpf_u_int32 *uset;

/*
 * Total number of atomic entities, including accumulator (A) and index (X).
 * We treat all these guys similarly during flow analysis.
 */
#define N_ATOMS (BPF_MEMWORDS+2)

struct edge {
	int id;
	int code;
	uset edom;
	struct block *succ;
	struct block *pred;
	struct edge *next;	/* link list of incoming edges for a node */
};

struct block {
	int id;
	struct slist *stmts;	/* side effect stmts */
	struct stmt s;		/* branch stmt */
	int mark;
	u_int longjt;		/* jt branch requires long jump */
	u_int longjf;		/* jf branch requires long jump */
	int level;
	int offset;
	int sense;
	struct edge et;
	struct edge ef;
	struct block *head;
	struct block *link;	/* link field used by optimizer */
	uset dom;
	uset closure;
	struct edge *in_edges;
	atomset def, kill;
	atomset in_use;
	atomset out_use;
	int oval;
	int val[N_ATOMS];
};

/*
 * A value of 0 for val[i] means the value is unknown.
 */
#define VAL_UNKNOWN	0

struct arth {
	struct block *b;	/* protocol checks */
	struct slist *s;	/* stmt list */
	int regno;		/* virtual register number of result */
};

struct qual {
	unsigned char addr;
	unsigned char proto;
	unsigned char dir;
	unsigned char pad;
};

struct _compiler_state;

typedef struct _compiler_state compiler_state_t;

struct arth *gen_loadi(compiler_state_t *, int);
struct arth *gen_load(compiler_state_t *, int, struct arth *, int);
struct arth *gen_loadlen(compiler_state_t *);
struct arth *gen_neg(compiler_state_t *, struct arth *);
struct arth *gen_arth(compiler_state_t *, int, struct arth *, struct arth *);

void gen_and(struct block *, struct block *);
void gen_or(struct block *, struct block *);
void gen_not(struct block *);

struct block *gen_scode(compiler_state_t *, const char *, struct qual);
struct block *gen_ecode(compiler_state_t *, const u_char *, struct qual);
struct block *gen_acode(compiler_state_t *, const u_char *, struct qual);
struct block *gen_mcode(compiler_state_t *, const char *, const char *,
    unsigned int, struct qual);
#ifdef INET6
struct block *gen_mcode6(compiler_state_t *, const char *, const char *,
    unsigned int, struct qual);
#endif
struct block *gen_ncode(compiler_state_t *, const char *, bpf_u_int32,
    struct qual);
struct block *gen_proto_abbrev(compiler_state_t *, int);
struct block *gen_relation(compiler_state_t *, int, struct arth *,
    struct arth *, int);
struct block *gen_less(compiler_state_t *, int);
struct block *gen_greater(compiler_state_t *, int);
struct block *gen_byteop(compiler_state_t *, int, int, int);
struct block *gen_broadcast(compiler_state_t *, int);
struct block *gen_multicast(compiler_state_t *, int);
struct block *gen_inbound(compiler_state_t *, int);

struct block *gen_llc(compiler_state_t *);
struct block *gen_llc_i(compiler_state_t *);
struct block *gen_llc_s(compiler_state_t *);
struct block *gen_llc_u(compiler_state_t *);
struct block *gen_llc_s_subtype(compiler_state_t *, bpf_u_int32);
struct block *gen_llc_u_subtype(compiler_state_t *, bpf_u_int32);

struct block *gen_vlan(compiler_state_t *, int);
struct block *gen_mpls(compiler_state_t *, int);

struct block *gen_pppoed(compiler_state_t *);
struct block *gen_pppoes(compiler_state_t *, int);

struct block *gen_geneve(compiler_state_t *, int);

struct block *gen_atmfield_code(compiler_state_t *, int, bpf_int32,
    bpf_u_int32, int);
struct block *gen_atmtype_abbrev(compiler_state_t *, int type);
struct block *gen_atmmulti_abbrev(compiler_state_t *, int type);

struct block *gen_mtp2type_abbrev(compiler_state_t *, int type);
struct block *gen_mtp3field_code(compiler_state_t *, int, bpf_u_int32,
    bpf_u_int32, int);

#ifndef HAVE_NET_PFVAR_H
PCAP_NORETURN
#endif
struct block *gen_pf_ifname(compiler_state_t *, const char *);
#ifndef HAVE_NET_PFVAR_H
PCAP_NORETURN
#endif
struct block *gen_pf_rnr(compiler_state_t *, int);
#ifndef HAVE_NET_PFVAR_H
PCAP_NORETURN
#endif
struct block *gen_pf_srnr(compiler_state_t *, int);
#ifndef HAVE_NET_PFVAR_H
PCAP_NORETURN
#endif
struct block *gen_pf_ruleset(compiler_state_t *, char *);
#ifndef HAVE_NET_PFVAR_H
PCAP_NORETURN
#endif
struct block *gen_pf_reason(compiler_state_t *, int);
#ifndef HAVE_NET_PFVAR_H
PCAP_NORETURN
#endif
struct block *gen_pf_action(compiler_state_t *, int);

struct block *gen_p80211_type(compiler_state_t *, int, int);
struct block *gen_p80211_fcdir(compiler_state_t *, int);

/*
 * Representation of a program as a tree of blocks, plus current mark.
 * A block is marked if only if its mark equals the current mark.
 * Rather than traverse the code array, marking each item, 'cur_mark'
 * is incremented.  This automatically makes each element unmarked.
 */
#define isMarked(icp, p) ((p)->mark == (icp)->cur_mark)
#define unMarkAll(icp) (icp)->cur_mark += 1
#define Mark(icp, p) ((p)->mark = (icp)->cur_mark)

struct icode {
	struct block *root;
	int cur_mark;
};

void bpf_optimize(compiler_state_t *, struct icode *ic);
void PCAP_NORETURN bpf_syntax_error(compiler_state_t *, const char *);
void PCAP_NORETURN bpf_error(compiler_state_t *, const char *, ...)
    PCAP_PRINTFLIKE(2, 3);

void finish_parse(compiler_state_t *, struct block *);
char *sdup(compiler_state_t *, const char *);

struct bpf_insn *icode_to_fcode(compiler_state_t *, struct icode *,
    struct block *, u_int *);
void sappend(struct slist *, struct slist *);

/*
 * Older versions of Bison don't put this declaration in
 * grammar.h.
 */
int pcap_parse(void *, compiler_state_t *);

/* XXX */
#define JT(b)  ((b)->et.succ)
#define JF(b)  ((b)->ef.succ)
