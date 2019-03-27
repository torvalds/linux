/*	$NetBSD: print-tcp.c,v 1.9 2007/07/26 18:15:12 plunky Exp $	*/

/*
 * Copyright (c) 1988, 1989, 1990, 1991, 1992, 1993, 1994, 1995, 1996, 1997
 *	The Regents of the University of California.  All rights reserved.
 *
 * Copyright (c) 1999-2004 The tcpdump.org project
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

/* \summary: TCP printer */

#ifndef lint
#else
__RCSID("$NetBSD: print-tcp.c,v 1.8 2007/07/24 11:53:48 drochner Exp $");
#endif

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <netdissect-stdinc.h>

#include <stdlib.h>
#include <string.h>

#include "netdissect.h"
#include "addrtoname.h"
#include "extract.h"

#include "tcp.h"

#include "ip.h"
#include "ip6.h"
#include "ipproto.h"
#include "rpc_auth.h"
#include "rpc_msg.h"

#ifdef HAVE_LIBCRYPTO
#include <openssl/md5.h>
#include "signature.h"

static int tcp_verify_signature(netdissect_options *ndo,
                                const struct ip *ip, const struct tcphdr *tp,
                                const u_char *data, int length, const u_char *rcvsig);
#endif

static void print_tcp_rst_data(netdissect_options *, register const u_char *sp, u_int length);
static void print_tcp_fastopen_option(netdissect_options *ndo, register const u_char *cp,
                                      u_int datalen, int exp);

#define MAX_RST_DATA_LEN	30


struct tha {
        struct in_addr src;
        struct in_addr dst;
        u_int port;
};

struct tcp_seq_hash {
        struct tcp_seq_hash *nxt;
        struct tha addr;
        tcp_seq seq;
        tcp_seq ack;
};

struct tha6 {
        struct in6_addr src;
        struct in6_addr dst;
        u_int port;
};

struct tcp_seq_hash6 {
        struct tcp_seq_hash6 *nxt;
        struct tha6 addr;
        tcp_seq seq;
        tcp_seq ack;
};

#define TSEQ_HASHSIZE 919

/* These tcp optinos do not have the size octet */
#define ZEROLENOPT(o) ((o) == TCPOPT_EOL || (o) == TCPOPT_NOP)

static struct tcp_seq_hash tcp_seq_hash4[TSEQ_HASHSIZE];
static struct tcp_seq_hash6 tcp_seq_hash6[TSEQ_HASHSIZE];

static const struct tok tcp_flag_values[] = {
        { TH_FIN, "F" },
        { TH_SYN, "S" },
        { TH_RST, "R" },
        { TH_PUSH, "P" },
        { TH_ACK, "." },
        { TH_URG, "U" },
        { TH_ECNECHO, "E" },
        { TH_CWR, "W" },
        { 0, NULL }
};

static const struct tok tcp_option_values[] = {
        { TCPOPT_EOL, "eol" },
        { TCPOPT_NOP, "nop" },
        { TCPOPT_MAXSEG, "mss" },
        { TCPOPT_WSCALE, "wscale" },
        { TCPOPT_SACKOK, "sackOK" },
        { TCPOPT_SACK, "sack" },
        { TCPOPT_ECHO, "echo" },
        { TCPOPT_ECHOREPLY, "echoreply" },
        { TCPOPT_TIMESTAMP, "TS" },
        { TCPOPT_CC, "cc" },
        { TCPOPT_CCNEW, "ccnew" },
        { TCPOPT_CCECHO, "" },
        { TCPOPT_SIGNATURE, "md5" },
        { TCPOPT_SCPS, "scps" },
        { TCPOPT_UTO, "uto" },
        { TCPOPT_TCPAO, "tcp-ao" },
        { TCPOPT_MPTCP, "mptcp" },
        { TCPOPT_FASTOPEN, "tfo" },
        { TCPOPT_EXPERIMENT2, "exp" },
        { 0, NULL }
};

static int
tcp_cksum(netdissect_options *ndo,
          register const struct ip *ip,
          register const struct tcphdr *tp,
          register u_int len)
{
	return nextproto4_cksum(ndo, ip, (const uint8_t *)tp, len, len,
				IPPROTO_TCP);
}

static int
tcp6_cksum(netdissect_options *ndo,
           register const struct ip6_hdr *ip6,
           register const struct tcphdr *tp,
           register u_int len)
{
	return nextproto6_cksum(ndo, ip6, (const uint8_t *)tp, len, len,
				IPPROTO_TCP);
}

void
tcp_print(netdissect_options *ndo,
          register const u_char *bp, register u_int length,
          register const u_char *bp2, int fragmented)
{
        register const struct tcphdr *tp;
        register const struct ip *ip;
        register u_char flags;
        register u_int hlen;
        register char ch;
        uint16_t sport, dport, win, urp;
        uint32_t seq, ack, thseq, thack;
        u_int utoval;
        uint16_t magic;
        register int rev;
        register const struct ip6_hdr *ip6;

        tp = (const struct tcphdr *)bp;
        ip = (const struct ip *)bp2;
        if (IP_V(ip) == 6)
                ip6 = (const struct ip6_hdr *)bp2;
        else
                ip6 = NULL;
        ch = '\0';
        if (!ND_TTEST(tp->th_dport)) {
                ND_PRINT((ndo, "%s > %s: [|tcp]",
                             ipaddr_string(ndo, &ip->ip_src),
                             ipaddr_string(ndo, &ip->ip_dst)));
                return;
        }

        sport = EXTRACT_16BITS(&tp->th_sport);
        dport = EXTRACT_16BITS(&tp->th_dport);

        if (ip6) {
                if (ip6->ip6_nxt == IPPROTO_TCP) {
                        ND_PRINT((ndo, "%s.%s > %s.%s: ",
                                     ip6addr_string(ndo, &ip6->ip6_src),
                                     tcpport_string(ndo, sport),
                                     ip6addr_string(ndo, &ip6->ip6_dst),
                                     tcpport_string(ndo, dport)));
                } else {
                        ND_PRINT((ndo, "%s > %s: ",
                                     tcpport_string(ndo, sport), tcpport_string(ndo, dport)));
                }
        } else {
                if (ip->ip_p == IPPROTO_TCP) {
                        ND_PRINT((ndo, "%s.%s > %s.%s: ",
                                     ipaddr_string(ndo, &ip->ip_src),
                                     tcpport_string(ndo, sport),
                                     ipaddr_string(ndo, &ip->ip_dst),
                                     tcpport_string(ndo, dport)));
                } else {
                        ND_PRINT((ndo, "%s > %s: ",
                                     tcpport_string(ndo, sport), tcpport_string(ndo, dport)));
                }
        }

        ND_TCHECK(*tp);

        hlen = TH_OFF(tp) * 4;

        if (hlen < sizeof(*tp)) {
                ND_PRINT((ndo, " tcp %d [bad hdr length %u - too short, < %lu]",
                             length - hlen, hlen, (unsigned long)sizeof(*tp)));
                return;
        }

        seq = EXTRACT_32BITS(&tp->th_seq);
        ack = EXTRACT_32BITS(&tp->th_ack);
        win = EXTRACT_16BITS(&tp->th_win);
        urp = EXTRACT_16BITS(&tp->th_urp);

        if (ndo->ndo_qflag) {
                ND_PRINT((ndo, "tcp %d", length - hlen));
                if (hlen > length) {
                        ND_PRINT((ndo, " [bad hdr length %u - too long, > %u]",
                                     hlen, length));
                }
                return;
        }

        flags = tp->th_flags;
        ND_PRINT((ndo, "Flags [%s]", bittok2str_nosep(tcp_flag_values, "none", flags)));

        if (!ndo->ndo_Sflag && (flags & TH_ACK)) {
                /*
                 * Find (or record) the initial sequence numbers for
                 * this conversation.  (we pick an arbitrary
                 * collating order so there's only one entry for
                 * both directions).
                 */
                rev = 0;
                if (ip6) {
                        register struct tcp_seq_hash6 *th;
                        struct tcp_seq_hash6 *tcp_seq_hash;
                        const struct in6_addr *src, *dst;
                        struct tha6 tha;

                        tcp_seq_hash = tcp_seq_hash6;
                        src = &ip6->ip6_src;
                        dst = &ip6->ip6_dst;
                        if (sport > dport)
                                rev = 1;
                        else if (sport == dport) {
                                if (UNALIGNED_MEMCMP(src, dst, sizeof ip6->ip6_dst) > 0)
                                        rev = 1;
                        }
                        if (rev) {
                                UNALIGNED_MEMCPY(&tha.src, dst, sizeof ip6->ip6_dst);
                                UNALIGNED_MEMCPY(&tha.dst, src, sizeof ip6->ip6_src);
                                tha.port = dport << 16 | sport;
                        } else {
                                UNALIGNED_MEMCPY(&tha.dst, dst, sizeof ip6->ip6_dst);
                                UNALIGNED_MEMCPY(&tha.src, src, sizeof ip6->ip6_src);
                                tha.port = sport << 16 | dport;
                        }

                        for (th = &tcp_seq_hash[tha.port % TSEQ_HASHSIZE];
                             th->nxt; th = th->nxt)
                                if (memcmp((char *)&tha, (char *)&th->addr,
                                           sizeof(th->addr)) == 0)
                                        break;

                        if (!th->nxt || (flags & TH_SYN)) {
                                /* didn't find it or new conversation */
                                if (th->nxt == NULL) {
                                        th->nxt = (struct tcp_seq_hash6 *)
                                                calloc(1, sizeof(*th));
                                        if (th->nxt == NULL)
                                                (*ndo->ndo_error)(ndo,
								  "tcp_print: calloc");
                                }
                                th->addr = tha;
                                if (rev)
                                        th->ack = seq, th->seq = ack - 1;
                                else
                                        th->seq = seq, th->ack = ack - 1;
                        } else {
                                if (rev)
                                        seq -= th->ack, ack -= th->seq;
                                else
                                        seq -= th->seq, ack -= th->ack;
                        }

                        thseq = th->seq;
                        thack = th->ack;
                } else {
                        register struct tcp_seq_hash *th;
                        struct tcp_seq_hash *tcp_seq_hash;
                        struct tha tha;

                        tcp_seq_hash = tcp_seq_hash4;
                        if (sport > dport)
                                rev = 1;
                        else if (sport == dport) {
                                if (UNALIGNED_MEMCMP(&ip->ip_src, &ip->ip_dst, sizeof ip->ip_dst) > 0)
                                        rev = 1;
                        }
                        if (rev) {
                                UNALIGNED_MEMCPY(&tha.src, &ip->ip_dst, sizeof ip->ip_dst);
                                UNALIGNED_MEMCPY(&tha.dst, &ip->ip_src, sizeof ip->ip_src);
                                tha.port = dport << 16 | sport;
                        } else {
                                UNALIGNED_MEMCPY(&tha.dst, &ip->ip_dst, sizeof ip->ip_dst);
                                UNALIGNED_MEMCPY(&tha.src, &ip->ip_src, sizeof ip->ip_src);
                                tha.port = sport << 16 | dport;
                        }

                        for (th = &tcp_seq_hash[tha.port % TSEQ_HASHSIZE];
                             th->nxt; th = th->nxt)
                                if (memcmp((char *)&tha, (char *)&th->addr,
                                           sizeof(th->addr)) == 0)
                                        break;

                        if (!th->nxt || (flags & TH_SYN)) {
                                /* didn't find it or new conversation */
                                if (th->nxt == NULL) {
                                        th->nxt = (struct tcp_seq_hash *)
                                                calloc(1, sizeof(*th));
                                        if (th->nxt == NULL)
                                                (*ndo->ndo_error)(ndo,
								  "tcp_print: calloc");
                                }
                                th->addr = tha;
                                if (rev)
                                        th->ack = seq, th->seq = ack - 1;
                                else
                                        th->seq = seq, th->ack = ack - 1;
                        } else {
                                if (rev)
                                        seq -= th->ack, ack -= th->seq;
                                else
                                        seq -= th->seq, ack -= th->ack;
                        }

                        thseq = th->seq;
                        thack = th->ack;
                }
        } else {
                /*fool gcc*/
                thseq = thack = rev = 0;
        }
        if (hlen > length) {
                ND_PRINT((ndo, " [bad hdr length %u - too long, > %u]",
                             hlen, length));
                return;
        }

        if (ndo->ndo_vflag && !ndo->ndo_Kflag && !fragmented) {
                /* Check the checksum, if possible. */
                uint16_t sum, tcp_sum;

                if (IP_V(ip) == 4) {
                        if (ND_TTEST2(tp->th_sport, length)) {
                                sum = tcp_cksum(ndo, ip, tp, length);
                                tcp_sum = EXTRACT_16BITS(&tp->th_sum);

                                ND_PRINT((ndo, ", cksum 0x%04x", tcp_sum));
                                if (sum != 0)
                                        ND_PRINT((ndo, " (incorrect -> 0x%04x)",
                                            in_cksum_shouldbe(tcp_sum, sum)));
                                else
                                        ND_PRINT((ndo, " (correct)"));
                        }
                } else if (IP_V(ip) == 6 && ip6->ip6_plen) {
                        if (ND_TTEST2(tp->th_sport, length)) {
                                sum = tcp6_cksum(ndo, ip6, tp, length);
                                tcp_sum = EXTRACT_16BITS(&tp->th_sum);

                                ND_PRINT((ndo, ", cksum 0x%04x", tcp_sum));
                                if (sum != 0)
                                        ND_PRINT((ndo, " (incorrect -> 0x%04x)",
                                            in_cksum_shouldbe(tcp_sum, sum)));
                                else
                                        ND_PRINT((ndo, " (correct)"));

                        }
                }
        }

        length -= hlen;
        if (ndo->ndo_vflag > 1 || length > 0 || flags & (TH_SYN | TH_FIN | TH_RST)) {
                ND_PRINT((ndo, ", seq %u", seq));

                if (length > 0) {
                        ND_PRINT((ndo, ":%u", seq + length));
                }
        }

        if (flags & TH_ACK) {
                ND_PRINT((ndo, ", ack %u", ack));
        }

        ND_PRINT((ndo, ", win %d", win));

        if (flags & TH_URG)
                ND_PRINT((ndo, ", urg %d", urp));
        /*
         * Handle any options.
         */
        if (hlen > sizeof(*tp)) {
                register const u_char *cp;
                register u_int i, opt, datalen;
                register u_int len;

                hlen -= sizeof(*tp);
                cp = (const u_char *)tp + sizeof(*tp);
                ND_PRINT((ndo, ", options ["));
                while (hlen > 0) {
                        if (ch != '\0')
                                ND_PRINT((ndo, "%c", ch));
                        ND_TCHECK(*cp);
                        opt = *cp++;
                        if (ZEROLENOPT(opt))
                                len = 1;
                        else {
                                ND_TCHECK(*cp);
                                len = *cp++;	/* total including type, len */
                                if (len < 2 || len > hlen)
                                        goto bad;
                                --hlen;		/* account for length byte */
                        }
                        --hlen;			/* account for type byte */
                        datalen = 0;

/* Bail if "l" bytes of data are not left or were not captured  */
#define LENCHECK(l) { if ((l) > hlen) goto bad; ND_TCHECK2(*cp, l); }


                        ND_PRINT((ndo, "%s", tok2str(tcp_option_values, "unknown-%u", opt)));

                        switch (opt) {

                        case TCPOPT_MAXSEG:
                                datalen = 2;
                                LENCHECK(datalen);
                                ND_PRINT((ndo, " %u", EXTRACT_16BITS(cp)));
                                break;

                        case TCPOPT_WSCALE:
                                datalen = 1;
                                LENCHECK(datalen);
                                ND_PRINT((ndo, " %u", *cp));
                                break;

                        case TCPOPT_SACK:
                                datalen = len - 2;
                                if (datalen % 8 != 0) {
                                        ND_PRINT((ndo, " invalid sack"));
                                } else {
                                        uint32_t s, e;

                                        ND_PRINT((ndo, " %d ", datalen / 8));
                                        for (i = 0; i < datalen; i += 8) {
                                                LENCHECK(i + 4);
                                                s = EXTRACT_32BITS(cp + i);
                                                LENCHECK(i + 8);
                                                e = EXTRACT_32BITS(cp + i + 4);
                                                if (rev) {
                                                        s -= thseq;
                                                        e -= thseq;
                                                } else {
                                                        s -= thack;
                                                        e -= thack;
                                                }
                                                ND_PRINT((ndo, "{%u:%u}", s, e));
                                        }
                                }
                                break;

                        case TCPOPT_CC:
                        case TCPOPT_CCNEW:
                        case TCPOPT_CCECHO:
                        case TCPOPT_ECHO:
                        case TCPOPT_ECHOREPLY:

                                /*
                                 * those options share their semantics.
                                 * fall through
                                 */
                                datalen = 4;
                                LENCHECK(datalen);
                                ND_PRINT((ndo, " %u", EXTRACT_32BITS(cp)));
                                break;

                        case TCPOPT_TIMESTAMP:
                                datalen = 8;
                                LENCHECK(datalen);
                                ND_PRINT((ndo, " val %u ecr %u",
                                             EXTRACT_32BITS(cp),
                                             EXTRACT_32BITS(cp + 4)));
                                break;

                        case TCPOPT_SIGNATURE:
                                datalen = TCP_SIGLEN;
                                LENCHECK(datalen);
                                ND_PRINT((ndo, " "));
#ifdef HAVE_LIBCRYPTO
                                switch (tcp_verify_signature(ndo, ip, tp,
                                                             bp + TH_OFF(tp) * 4, length, cp)) {

                                case SIGNATURE_VALID:
                                        ND_PRINT((ndo, "valid"));
                                        break;

                                case SIGNATURE_INVALID:
                                        ND_PRINT((ndo, "invalid"));
                                        break;

                                case CANT_CHECK_SIGNATURE:
                                        ND_PRINT((ndo, "can't check - "));
                                        for (i = 0; i < TCP_SIGLEN; ++i)
                                                ND_PRINT((ndo, "%02x", cp[i]));
                                        break;
                                }
#else
                                for (i = 0; i < TCP_SIGLEN; ++i)
                                        ND_PRINT((ndo, "%02x", cp[i]));
#endif
                                break;

                        case TCPOPT_SCPS:
                                datalen = 2;
                                LENCHECK(datalen);
                                ND_PRINT((ndo, " cap %02x id %u", cp[0], cp[1]));
                                break;

                        case TCPOPT_TCPAO:
                                datalen = len - 2;
                                /* RFC 5925 Section 2.2:
                                 * "The Length value MUST be greater than or equal to 4."
                                 * (This includes the Kind and Length fields already processed
                                 * at this point.)
                                 */
                                if (datalen < 2) {
                                        ND_PRINT((ndo, " invalid"));
                                } else {
                                        LENCHECK(1);
                                        ND_PRINT((ndo, " keyid %u", cp[0]));
                                        LENCHECK(2);
                                        ND_PRINT((ndo, " rnextkeyid %u", cp[1]));
                                        if (datalen > 2) {
                                                ND_PRINT((ndo, " mac 0x"));
                                                for (i = 2; i < datalen; i++) {
                                                        LENCHECK(i + 1);
                                                        ND_PRINT((ndo, "%02x", cp[i]));
                                                }
                                        }
                                }
                                break;

                        case TCPOPT_EOL:
                        case TCPOPT_NOP:
                        case TCPOPT_SACKOK:
                                /*
                                 * Nothing interesting.
                                 * fall through
                                 */
                                break;

                        case TCPOPT_UTO:
                                datalen = 2;
                                LENCHECK(datalen);
                                utoval = EXTRACT_16BITS(cp);
                                ND_PRINT((ndo, " 0x%x", utoval));
                                if (utoval & 0x0001)
                                        utoval = (utoval >> 1) * 60;
                                else
                                        utoval >>= 1;
                                ND_PRINT((ndo, " %u", utoval));
                                break;

                        case TCPOPT_MPTCP:
                                datalen = len - 2;
                                LENCHECK(datalen);
                                if (!mptcp_print(ndo, cp-2, len, flags))
                                        goto bad;
                                break;

                        case TCPOPT_FASTOPEN:
                                datalen = len - 2;
                                LENCHECK(datalen);
                                ND_PRINT((ndo, " "));
                                print_tcp_fastopen_option(ndo, cp, datalen, FALSE);
                                break;

                        case TCPOPT_EXPERIMENT2:
                                datalen = len - 2;
                                LENCHECK(datalen);
                                if (datalen < 2)
                                        goto bad;
                                /* RFC6994 */
                                magic = EXTRACT_16BITS(cp);
                                ND_PRINT((ndo, "-"));

                                switch(magic) {

                                case 0xf989: /* TCP Fast Open RFC 7413 */
                                        print_tcp_fastopen_option(ndo, cp + 2, datalen - 2, TRUE);
                                        break;

                                default:
                                        /* Unknown magic number */
                                        ND_PRINT((ndo, "%04x", magic));
                                        break;
                                }
                                break;

                        default:
                                datalen = len - 2;
                                if (datalen)
                                        ND_PRINT((ndo, " 0x"));
                                for (i = 0; i < datalen; ++i) {
                                        LENCHECK(i + 1);
                                        ND_PRINT((ndo, "%02x", cp[i]));
                                }
                                break;
                        }

                        /* Account for data printed */
                        cp += datalen;
                        hlen -= datalen;

                        /* Check specification against observed length */
                        ++datalen;			/* option octet */
                        if (!ZEROLENOPT(opt))
                                ++datalen;		/* size octet */
                        if (datalen != len)
                                ND_PRINT((ndo, "[len %d]", len));
                        ch = ',';
                        if (opt == TCPOPT_EOL)
                                break;
                }
                ND_PRINT((ndo, "]"));
        }

        /*
         * Print length field before crawling down the stack.
         */
        ND_PRINT((ndo, ", length %u", length));

        if (length <= 0)
                return;

        /*
         * Decode payload if necessary.
         */
        bp += TH_OFF(tp) * 4;
        if ((flags & TH_RST) && ndo->ndo_vflag) {
                print_tcp_rst_data(ndo, bp, length);
                return;
        }

        if (ndo->ndo_packettype) {
                switch (ndo->ndo_packettype) {
                case PT_ZMTP1:
                        zmtp1_print(ndo, bp, length);
                        break;
                case PT_RESP:
                        resp_print(ndo, bp, length);
                        break;
                }
                return;
        }

        if (IS_SRC_OR_DST_PORT(TELNET_PORT)) {
                telnet_print(ndo, bp, length);
        } else if (IS_SRC_OR_DST_PORT(SMTP_PORT)) {
                ND_PRINT((ndo, ": "));
                smtp_print(ndo, bp, length);
        } else if (IS_SRC_OR_DST_PORT(BGP_PORT))
                bgp_print(ndo, bp, length);
        else if (IS_SRC_OR_DST_PORT(PPTP_PORT))
                pptp_print(ndo, bp);
        else if (IS_SRC_OR_DST_PORT(REDIS_PORT))
                resp_print(ndo, bp, length);
#ifdef ENABLE_SMB
        else if (IS_SRC_OR_DST_PORT(NETBIOS_SSN_PORT))
                nbt_tcp_print(ndo, bp, length);
	else if (IS_SRC_OR_DST_PORT(SMB_PORT))
		smb_tcp_print(ndo, bp, length);
#endif
        else if (IS_SRC_OR_DST_PORT(BEEP_PORT))
                beep_print(ndo, bp, length);
        else if (IS_SRC_OR_DST_PORT(OPENFLOW_PORT_OLD) || IS_SRC_OR_DST_PORT(OPENFLOW_PORT_IANA))
                openflow_print(ndo, bp, length);
        else if (IS_SRC_OR_DST_PORT(FTP_PORT)) {
                ND_PRINT((ndo, ": "));
                ftp_print(ndo, bp, length);
        } else if (IS_SRC_OR_DST_PORT(HTTP_PORT) || IS_SRC_OR_DST_PORT(HTTP_PORT_ALT)) {
                ND_PRINT((ndo, ": "));
                http_print(ndo, bp, length);
        } else if (IS_SRC_OR_DST_PORT(RTSP_PORT) || IS_SRC_OR_DST_PORT(RTSP_PORT_ALT)) {
                ND_PRINT((ndo, ": "));
                rtsp_print(ndo, bp, length);
        } else if (length > 2 &&
                 (IS_SRC_OR_DST_PORT(NAMESERVER_PORT))) {
                /*
                 * TCP DNS query has 2byte length at the head.
                 * XXX packet could be unaligned, it can go strange
                 */
                ns_print(ndo, bp + 2, length - 2, 0);
        } else if (IS_SRC_OR_DST_PORT(MSDP_PORT)) {
                msdp_print(ndo, bp, length);
        } else if (IS_SRC_OR_DST_PORT(RPKI_RTR_PORT)) {
                rpki_rtr_print(ndo, bp, length);
        }
        else if (length > 0 && (IS_SRC_OR_DST_PORT(LDP_PORT))) {
                ldp_print(ndo, bp, length);
        }
        else if ((IS_SRC_OR_DST_PORT(NFS_PORT)) &&
                 length >= 4 && ND_TTEST2(*bp, 4)) {
                /*
                 * If data present, header length valid, and NFS port used,
                 * assume NFS.
                 * Pass offset of data plus 4 bytes for RPC TCP msg length
                 * to NFS print routines.
                 */
                uint32_t fraglen;
                register const struct sunrpc_msg *rp;
                enum sunrpc_msg_type direction;

                fraglen = EXTRACT_32BITS(bp) & 0x7FFFFFFF;
                if (fraglen > (length) - 4)
                        fraglen = (length) - 4;
                rp = (const struct sunrpc_msg *)(bp + 4);
                if (ND_TTEST(rp->rm_direction)) {
                        direction = (enum sunrpc_msg_type)EXTRACT_32BITS(&rp->rm_direction);
                        if (dport == NFS_PORT && direction == SUNRPC_CALL) {
                                ND_PRINT((ndo, ": NFS request xid %u ", EXTRACT_32BITS(&rp->rm_xid)));
                                nfsreq_print_noaddr(ndo, (const u_char *)rp, fraglen, (const u_char *)ip);
                                return;
                        }
                        if (sport == NFS_PORT && direction == SUNRPC_REPLY) {
                                ND_PRINT((ndo, ": NFS reply xid %u ", EXTRACT_32BITS(&rp->rm_xid)));
                                nfsreply_print_noaddr(ndo, (const u_char *)rp, fraglen, (const u_char *)ip);
                                return;
                        }
                }
        }

        return;
 bad:
        ND_PRINT((ndo, "[bad opt]"));
        if (ch != '\0')
                ND_PRINT((ndo, ">"));
        return;
 trunc:
        ND_PRINT((ndo, "[|tcp]"));
        if (ch != '\0')
                ND_PRINT((ndo, ">"));
}

/*
 * RFC1122 says the following on data in RST segments:
 *
 *         4.2.2.12  RST Segment: RFC-793 Section 3.4
 *
 *            A TCP SHOULD allow a received RST segment to include data.
 *
 *            DISCUSSION
 *                 It has been suggested that a RST segment could contain
 *                 ASCII text that encoded and explained the cause of the
 *                 RST.  No standard has yet been established for such
 *                 data.
 *
 */

static void
print_tcp_rst_data(netdissect_options *ndo,
                   register const u_char *sp, u_int length)
{
        int c;

        ND_PRINT((ndo, ND_TTEST2(*sp, length) ? " [RST" : " [!RST"));
        if (length > MAX_RST_DATA_LEN) {
                length = MAX_RST_DATA_LEN;	/* can use -X for longer */
                ND_PRINT((ndo, "+"));			/* indicate we truncate */
        }
        ND_PRINT((ndo, " "));
        while (length-- && sp < ndo->ndo_snapend) {
                c = *sp++;
                safeputchar(ndo, c);
        }
        ND_PRINT((ndo, "]"));
}

static void
print_tcp_fastopen_option(netdissect_options *ndo, register const u_char *cp,
                          u_int datalen, int exp)
{
        u_int i;

        if (exp)
                ND_PRINT((ndo, "tfo"));

        if (datalen == 0) {
                /* Fast Open Cookie Request */
                ND_PRINT((ndo, " cookiereq"));
        } else {
                /* Fast Open Cookie */
                if (datalen % 2 != 0 || datalen < 4 || datalen > 16) {
                        ND_PRINT((ndo, " invalid"));
                } else {
                        ND_PRINT((ndo, " cookie "));
                        for (i = 0; i < datalen; ++i)
                                ND_PRINT((ndo, "%02x", cp[i]));
                }
        }
}

#ifdef HAVE_LIBCRYPTO
USES_APPLE_DEPRECATED_API
static int
tcp_verify_signature(netdissect_options *ndo,
                     const struct ip *ip, const struct tcphdr *tp,
                     const u_char *data, int length, const u_char *rcvsig)
{
        struct tcphdr tp1;
        u_char sig[TCP_SIGLEN];
        char zero_proto = 0;
        MD5_CTX ctx;
        uint16_t savecsum, tlen;
        const struct ip6_hdr *ip6;
        uint32_t len32;
        uint8_t nxt;

	if (data + length > ndo->ndo_snapend) {
		ND_PRINT((ndo, "snaplen too short, "));
		return (CANT_CHECK_SIGNATURE);
	}

        tp1 = *tp;

        if (ndo->ndo_sigsecret == NULL) {
		ND_PRINT((ndo, "shared secret not supplied with -M, "));
                return (CANT_CHECK_SIGNATURE);
        }

        MD5_Init(&ctx);
        /*
         * Step 1: Update MD5 hash with IP pseudo-header.
         */
        if (IP_V(ip) == 4) {
                MD5_Update(&ctx, (const char *)&ip->ip_src, sizeof(ip->ip_src));
                MD5_Update(&ctx, (const char *)&ip->ip_dst, sizeof(ip->ip_dst));
                MD5_Update(&ctx, (const char *)&zero_proto, sizeof(zero_proto));
                MD5_Update(&ctx, (const char *)&ip->ip_p, sizeof(ip->ip_p));
                tlen = EXTRACT_16BITS(&ip->ip_len) - IP_HL(ip) * 4;
                tlen = htons(tlen);
                MD5_Update(&ctx, (const char *)&tlen, sizeof(tlen));
        } else if (IP_V(ip) == 6) {
                ip6 = (const struct ip6_hdr *)ip;
                MD5_Update(&ctx, (const char *)&ip6->ip6_src, sizeof(ip6->ip6_src));
                MD5_Update(&ctx, (const char *)&ip6->ip6_dst, sizeof(ip6->ip6_dst));
                len32 = htonl(EXTRACT_16BITS(&ip6->ip6_plen));
                MD5_Update(&ctx, (const char *)&len32, sizeof(len32));
                nxt = 0;
                MD5_Update(&ctx, (const char *)&nxt, sizeof(nxt));
                MD5_Update(&ctx, (const char *)&nxt, sizeof(nxt));
                MD5_Update(&ctx, (const char *)&nxt, sizeof(nxt));
                nxt = IPPROTO_TCP;
                MD5_Update(&ctx, (const char *)&nxt, sizeof(nxt));
        } else {
		ND_PRINT((ndo, "IP version not 4 or 6, "));
                return (CANT_CHECK_SIGNATURE);
        }

        /*
         * Step 2: Update MD5 hash with TCP header, excluding options.
         * The TCP checksum must be set to zero.
         */
        savecsum = tp1.th_sum;
        tp1.th_sum = 0;
        MD5_Update(&ctx, (const char *)&tp1, sizeof(struct tcphdr));
        tp1.th_sum = savecsum;
        /*
         * Step 3: Update MD5 hash with TCP segment data, if present.
         */
        if (length > 0)
                MD5_Update(&ctx, data, length);
        /*
         * Step 4: Update MD5 hash with shared secret.
         */
        MD5_Update(&ctx, ndo->ndo_sigsecret, strlen(ndo->ndo_sigsecret));
        MD5_Final(sig, &ctx);

        if (memcmp(rcvsig, sig, TCP_SIGLEN) == 0)
                return (SIGNATURE_VALID);
        else
                return (SIGNATURE_INVALID);
}
USES_APPLE_RST
#endif /* HAVE_LIBCRYPTO */

/*
 * Local Variables:
 * c-style: whitesmith
 * c-basic-offset: 8
 * End:
 */
