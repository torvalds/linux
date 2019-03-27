/**
 * Copyright (c) 2012
 *
 * Gregory Detal <gregory.detal@uclouvain.be>
 * Christoph Paasch <christoph.paasch@uclouvain.be>
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
 * 3. Neither the name of the University nor of the Laboratory may be used
 *    to endorse or promote products derived from this software without
 *    specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE REGENTS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

/* \summary: Multipath TCP (MPTCP) printer */

/* specification: RFC 6824 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <netdissect-stdinc.h>

#include "netdissect.h"
#include "extract.h"
#include "addrtoname.h"

#include "tcp.h"

#define MPTCP_SUB_CAPABLE       0x0
#define MPTCP_SUB_JOIN          0x1
#define MPTCP_SUB_DSS           0x2
#define MPTCP_SUB_ADD_ADDR      0x3
#define MPTCP_SUB_REMOVE_ADDR   0x4
#define MPTCP_SUB_PRIO          0x5
#define MPTCP_SUB_FAIL          0x6
#define MPTCP_SUB_FCLOSE        0x7

struct mptcp_option {
        uint8_t        kind;
        uint8_t        len;
        uint8_t        sub_etc;        /* subtype upper 4 bits, other stuff lower 4 bits */
};

#define MPTCP_OPT_SUBTYPE(sub_etc)      (((sub_etc) >> 4) & 0xF)

struct mp_capable {
        uint8_t        kind;
        uint8_t        len;
        uint8_t        sub_ver;
        uint8_t        flags;
        uint8_t        sender_key[8];
        uint8_t        receiver_key[8];
};

#define MP_CAPABLE_OPT_VERSION(sub_ver) (((sub_ver) >> 0) & 0xF)
#define MP_CAPABLE_C                    0x80
#define MP_CAPABLE_S                    0x01

struct mp_join {
        uint8_t        kind;
        uint8_t        len;
        uint8_t        sub_b;
        uint8_t        addr_id;
        union {
                struct {
                        uint8_t         token[4];
                        uint8_t         nonce[4];
                } syn;
                struct {
                        uint8_t         mac[8];
                        uint8_t         nonce[4];
                } synack;
                struct {
                        uint8_t        mac[20];
                } ack;
        } u;
};

#define MP_JOIN_B                       0x01

struct mp_dss {
        uint8_t        kind;
        uint8_t        len;
        uint8_t        sub;
        uint8_t        flags;
};

#define MP_DSS_F                        0x10
#define MP_DSS_m                        0x08
#define MP_DSS_M                        0x04
#define MP_DSS_a                        0x02
#define MP_DSS_A                        0x01

struct mp_add_addr {
        uint8_t        kind;
        uint8_t        len;
        uint8_t        sub_ipver;
        uint8_t        addr_id;
        union {
                struct {
                        uint8_t         addr[4];
                        uint8_t         port[2];
                } v4;
                struct {
                        uint8_t         addr[16];
                        uint8_t         port[2];
                } v6;
        } u;
};

#define MP_ADD_ADDR_IPVER(sub_ipver)    (((sub_ipver) >> 0) & 0xF)

struct mp_remove_addr {
        uint8_t        kind;
        uint8_t        len;
        uint8_t        sub;
        /* list of addr_id */
        uint8_t        addrs_id;
};

struct mp_fail {
        uint8_t        kind;
        uint8_t        len;
        uint8_t        sub;
        uint8_t        resv;
        uint8_t        data_seq[8];
};

struct mp_close {
        uint8_t        kind;
        uint8_t        len;
        uint8_t        sub;
        uint8_t        rsv;
        uint8_t        key[8];
};

struct mp_prio {
        uint8_t        kind;
        uint8_t        len;
        uint8_t        sub_b;
        uint8_t        addr_id;
};

#define MP_PRIO_B                       0x01

static int
dummy_print(netdissect_options *ndo _U_,
            const u_char *opt _U_, u_int opt_len _U_, u_char flags _U_)
{
        return 1;
}

static int
mp_capable_print(netdissect_options *ndo,
                 const u_char *opt, u_int opt_len, u_char flags)
{
        const struct mp_capable *mpc = (const struct mp_capable *) opt;

        if (!(opt_len == 12 && (flags & TH_SYN)) &&
            !(opt_len == 20 && (flags & (TH_SYN | TH_ACK)) == TH_ACK))
                return 0;

        if (MP_CAPABLE_OPT_VERSION(mpc->sub_ver) != 0) {
                ND_PRINT((ndo, " Unknown Version (%d)", MP_CAPABLE_OPT_VERSION(mpc->sub_ver)));
                return 1;
        }

        if (mpc->flags & MP_CAPABLE_C)
                ND_PRINT((ndo, " csum"));
        ND_PRINT((ndo, " {0x%" PRIx64, EXTRACT_64BITS(mpc->sender_key)));
        if (opt_len == 20) /* ACK */
                ND_PRINT((ndo, ",0x%" PRIx64, EXTRACT_64BITS(mpc->receiver_key)));
        ND_PRINT((ndo, "}"));
        return 1;
}

static int
mp_join_print(netdissect_options *ndo,
              const u_char *opt, u_int opt_len, u_char flags)
{
        const struct mp_join *mpj = (const struct mp_join *) opt;

        if (!(opt_len == 12 && (flags & TH_SYN)) &&
            !(opt_len == 16 && (flags & (TH_SYN | TH_ACK)) == (TH_SYN | TH_ACK)) &&
            !(opt_len == 24 && (flags & TH_ACK)))
                return 0;

        if (opt_len != 24) {
                if (mpj->sub_b & MP_JOIN_B)
                        ND_PRINT((ndo, " backup"));
                ND_PRINT((ndo, " id %u", mpj->addr_id));
        }

        switch (opt_len) {
        case 12: /* SYN */
                ND_PRINT((ndo, " token 0x%x" " nonce 0x%x",
                        EXTRACT_32BITS(mpj->u.syn.token),
                        EXTRACT_32BITS(mpj->u.syn.nonce)));
                break;
        case 16: /* SYN/ACK */
                ND_PRINT((ndo, " hmac 0x%" PRIx64 " nonce 0x%x",
                        EXTRACT_64BITS(mpj->u.synack.mac),
                        EXTRACT_32BITS(mpj->u.synack.nonce)));
                break;
        case 24: {/* ACK */
                size_t i;
                ND_PRINT((ndo, " hmac 0x"));
                for (i = 0; i < sizeof(mpj->u.ack.mac); ++i)
                        ND_PRINT((ndo, "%02x", mpj->u.ack.mac[i]));
        }
        default:
                break;
        }
        return 1;
}

static int
mp_dss_print(netdissect_options *ndo,
             const u_char *opt, u_int opt_len, u_char flags)
{
        const struct mp_dss *mdss = (const struct mp_dss *) opt;

        /* We need the flags, at a minimum. */
        if (opt_len < 4)
                return 0;

        if (flags & TH_SYN)
                return 0;

        if (mdss->flags & MP_DSS_F)
                ND_PRINT((ndo, " fin"));

        opt += 4;
        opt_len -= 4;
        if (mdss->flags & MP_DSS_A) {
                /* Ack present */
                ND_PRINT((ndo, " ack "));
                /*
                 * If the a flag is set, we have an 8-byte ack; if it's
                 * clear, we have a 4-byte ack.
                 */
                if (mdss->flags & MP_DSS_a) {
                        if (opt_len < 8)
                                return 0;
                        ND_PRINT((ndo, "%" PRIu64, EXTRACT_64BITS(opt)));
                        opt += 8;
                        opt_len -= 8;
                } else {
                        if (opt_len < 4)
                                return 0;
                        ND_PRINT((ndo, "%u", EXTRACT_32BITS(opt)));
                        opt += 4;
                        opt_len -= 4;
                }
        }

        if (mdss->flags & MP_DSS_M) {
                /*
                 * Data Sequence Number (DSN), Subflow Sequence Number (SSN),
                 * Data-Level Length present, and Checksum possibly present.
                 */
                ND_PRINT((ndo, " seq "));
		/*
                 * If the m flag is set, we have an 8-byte NDS; if it's clear,
                 * we have a 4-byte DSN.
                 */
                if (mdss->flags & MP_DSS_m) {
                        if (opt_len < 8)
                                return 0;
                        ND_PRINT((ndo, "%" PRIu64, EXTRACT_64BITS(opt)));
                        opt += 8;
                        opt_len -= 8;
                } else {
                        if (opt_len < 4)
                                return 0;
                        ND_PRINT((ndo, "%u", EXTRACT_32BITS(opt)));
                        opt += 4;
                        opt_len -= 4;
                }
                if (opt_len < 4)
                        return 0;
                ND_PRINT((ndo, " subseq %u", EXTRACT_32BITS(opt)));
                opt += 4;
                opt_len -= 4;
                if (opt_len < 2)
                        return 0;
                ND_PRINT((ndo, " len %u", EXTRACT_16BITS(opt)));
                opt += 2;
                opt_len -= 2;

                /*
                 * The Checksum is present only if negotiated.
                 * If there are at least 2 bytes left, process the next 2
                 * bytes as the Checksum.
                 */
                if (opt_len >= 2) {
                        ND_PRINT((ndo, " csum 0x%x", EXTRACT_16BITS(opt)));
                        opt_len -= 2;
                }
        }
        if (opt_len != 0)
                return 0;
        return 1;
}

static int
add_addr_print(netdissect_options *ndo,
               const u_char *opt, u_int opt_len, u_char flags _U_)
{
        const struct mp_add_addr *add_addr = (const struct mp_add_addr *) opt;
        u_int ipver = MP_ADD_ADDR_IPVER(add_addr->sub_ipver);

        if (!((opt_len == 8 || opt_len == 10) && ipver == 4) &&
            !((opt_len == 20 || opt_len == 22) && ipver == 6))
                return 0;

        ND_PRINT((ndo, " id %u", add_addr->addr_id));
        switch (ipver) {
        case 4:
                ND_PRINT((ndo, " %s", ipaddr_string(ndo, add_addr->u.v4.addr)));
                if (opt_len == 10)
                        ND_PRINT((ndo, ":%u", EXTRACT_16BITS(add_addr->u.v4.port)));
                break;
        case 6:
                ND_PRINT((ndo, " %s", ip6addr_string(ndo, add_addr->u.v6.addr)));
                if (opt_len == 22)
                        ND_PRINT((ndo, ":%u", EXTRACT_16BITS(add_addr->u.v6.port)));
                break;
        default:
                return 0;
        }

        return 1;
}

static int
remove_addr_print(netdissect_options *ndo,
                  const u_char *opt, u_int opt_len, u_char flags _U_)
{
        const struct mp_remove_addr *remove_addr = (const struct mp_remove_addr *) opt;
        const uint8_t *addr_id = &remove_addr->addrs_id;

        if (opt_len < 4)
                return 0;

        opt_len -= 3;
        ND_PRINT((ndo, " id"));
        while (opt_len--)
                ND_PRINT((ndo, " %u", *addr_id++));
        return 1;
}

static int
mp_prio_print(netdissect_options *ndo,
              const u_char *opt, u_int opt_len, u_char flags _U_)
{
        const struct mp_prio *mpp = (const struct mp_prio *) opt;

        if (opt_len != 3 && opt_len != 4)
                return 0;

        if (mpp->sub_b & MP_PRIO_B)
                ND_PRINT((ndo, " backup"));
        else
                ND_PRINT((ndo, " non-backup"));
        if (opt_len == 4)
                ND_PRINT((ndo, " id %u", mpp->addr_id));

        return 1;
}

static int
mp_fail_print(netdissect_options *ndo,
              const u_char *opt, u_int opt_len, u_char flags _U_)
{
        if (opt_len != 12)
                return 0;

        ND_PRINT((ndo, " seq %" PRIu64, EXTRACT_64BITS(opt + 4)));
        return 1;
}

static int
mp_fast_close_print(netdissect_options *ndo,
                    const u_char *opt, u_int opt_len, u_char flags _U_)
{
        if (opt_len != 12)
                return 0;

        ND_PRINT((ndo, " key 0x%" PRIx64, EXTRACT_64BITS(opt + 4)));
        return 1;
}

static const struct {
        const char *name;
        int (*print)(netdissect_options *, const u_char *, u_int, u_char);
} mptcp_options[] = {
        { "capable", mp_capable_print},
        { "join",       mp_join_print },
        { "dss",        mp_dss_print },
        { "add-addr",   add_addr_print },
        { "rem-addr",   remove_addr_print },
        { "prio",       mp_prio_print },
        { "fail",       mp_fail_print },
        { "fast-close", mp_fast_close_print },
        { "unknown",    dummy_print },
};

int
mptcp_print(netdissect_options *ndo,
            const u_char *cp, u_int len, u_char flags)
{
        const struct mptcp_option *opt;
        u_int subtype;

        if (len < 3)
                return 0;

        opt = (const struct mptcp_option *) cp;
        subtype = min(MPTCP_OPT_SUBTYPE(opt->sub_etc), MPTCP_SUB_FCLOSE + 1);

        ND_PRINT((ndo, " %s", mptcp_options[subtype].name));
        return mptcp_options[subtype].print(ndo, cp, len, flags);
}
