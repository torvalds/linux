/*
 * Copyright (c) 2007-2011 Grégoire Henry, Juliusz Chroboczek
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the project nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE PROJECT AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE PROJECT OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

/* \summary: Babel Routing Protocol printer */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <netdissect-stdinc.h>

#include <stdio.h>
#include <string.h>

#include "netdissect.h"
#include "addrtoname.h"
#include "extract.h"

static const char tstr[] = "[|babel]";

static void babel_print_v2(netdissect_options *, const u_char *cp, u_int length);

void
babel_print(netdissect_options *ndo,
            const u_char *cp, u_int length)
{
    ND_PRINT((ndo, "babel"));

    ND_TCHECK2(*cp, 4);

    if(cp[0] != 42) {
        ND_PRINT((ndo, " invalid header"));
        return;
    } else {
        ND_PRINT((ndo, " %d", cp[1]));
    }

    switch(cp[1]) {
    case 2:
        babel_print_v2(ndo, cp, length);
        break;
    default:
        ND_PRINT((ndo, " unknown version"));
        break;
    }

    return;

 trunc:
    ND_PRINT((ndo, " %s", tstr));
    return;
}

/* TLVs */
#define MESSAGE_PAD1 0
#define MESSAGE_PADN 1
#define MESSAGE_ACK_REQ 2
#define MESSAGE_ACK 3
#define MESSAGE_HELLO 4
#define MESSAGE_IHU 5
#define MESSAGE_ROUTER_ID 6
#define MESSAGE_NH 7
#define MESSAGE_UPDATE 8
#define MESSAGE_REQUEST 9
#define MESSAGE_MH_REQUEST 10
#define MESSAGE_TSPC 11
#define MESSAGE_HMAC 12
#define MESSAGE_UPDATE_SRC_SPECIFIC 13
#define MESSAGE_REQUEST_SRC_SPECIFIC 14
#define MESSAGE_MH_REQUEST_SRC_SPECIFIC 15

/* sub-TLVs */
#define MESSAGE_SUB_PAD1 0
#define MESSAGE_SUB_PADN 1
#define MESSAGE_SUB_DIVERSITY 2
#define MESSAGE_SUB_TIMESTAMP 3

/* Diversity sub-TLV channel codes */
static const struct tok diversity_str[] = {
    { 0,   "reserved" },
    { 255, "all"      },
    { 0, NULL }
};

static const char *
format_id(const u_char *id)
{
    static char buf[25];
    snprintf(buf, 25, "%02x:%02x:%02x:%02x:%02x:%02x:%02x:%02x",
             id[0], id[1], id[2], id[3], id[4], id[5], id[6], id[7]);
    buf[24] = '\0';
    return buf;
}

static const unsigned char v4prefix[16] =
    {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0xFF, 0xFF, 0, 0, 0, 0 };

static const char *
format_prefix(netdissect_options *ndo, const u_char *prefix, unsigned char plen)
{
    static char buf[50];
    if(plen >= 96 && memcmp(prefix, v4prefix, 12) == 0)
        snprintf(buf, 50, "%s/%u", ipaddr_string(ndo, prefix + 12), plen - 96);
    else
        snprintf(buf, 50, "%s/%u", ip6addr_string(ndo, prefix), plen);
    buf[49] = '\0';
    return buf;
}

static const char *
format_address(netdissect_options *ndo, const u_char *prefix)
{
    if(memcmp(prefix, v4prefix, 12) == 0)
        return ipaddr_string(ndo, prefix + 12);
    else
        return ip6addr_string(ndo, prefix);
}

static const char *
format_interval(const uint16_t i)
{
    static char buf[sizeof("000.00s")];

    if (i == 0)
        return "0.0s (bogus)";
    snprintf(buf, sizeof(buf), "%u.%02us", i / 100, i % 100);
    return buf;
}

static const char *
format_interval_update(const uint16_t i)
{
    return i == 0xFFFF ? "infinity" : format_interval(i);
}

static const char *
format_timestamp(const uint32_t i)
{
    static char buf[sizeof("0000.000000s")];
    snprintf(buf, sizeof(buf), "%u.%06us", i / 1000000, i % 1000000);
    return buf;
}

/* Return number of octets consumed from the input buffer (not the prefix length
 * in bytes), or -1 for encoding error. */
static int
network_prefix(int ae, int plen, unsigned int omitted,
               const unsigned char *p, const unsigned char *dp,
               unsigned int len, unsigned char *p_r)
{
    unsigned pb;
    unsigned char prefix[16];
    int consumed = 0;

    if(plen >= 0)
        pb = (plen + 7) / 8;
    else if(ae == 1)
        pb = 4;
    else
        pb = 16;

    if(pb > 16)
        return -1;

    memset(prefix, 0, 16);

    switch(ae) {
    case 0: break;
    case 1:
        if(omitted > 4 || pb > 4 || (pb > omitted && len < pb - omitted))
            return -1;
        memcpy(prefix, v4prefix, 12);
        if(omitted) {
            if (dp == NULL) return -1;
            memcpy(prefix, dp, 12 + omitted);
        }
        if(pb > omitted) {
            memcpy(prefix + 12 + omitted, p, pb - omitted);
            consumed = pb - omitted;
        }
        break;
    case 2:
        if(omitted > 16 || (pb > omitted && len < pb - omitted))
            return -1;
        if(omitted) {
            if (dp == NULL) return -1;
            memcpy(prefix, dp, omitted);
        }
        if(pb > omitted) {
            memcpy(prefix + omitted, p, pb - omitted);
            consumed = pb - omitted;
        }
        break;
    case 3:
        if(pb > 8 && len < pb - 8) return -1;
        prefix[0] = 0xfe;
        prefix[1] = 0x80;
        if(pb > 8) {
            memcpy(prefix + 8, p, pb - 8);
            consumed = pb - 8;
        }
        break;
    default:
        return -1;
    }

    memcpy(p_r, prefix, 16);
    return consumed;
}

static int
network_address(int ae, const unsigned char *a, unsigned int len,
                unsigned char *a_r)
{
    return network_prefix(ae, -1, 0, a, NULL, len, a_r);
}

/*
 * Sub-TLVs consume the "extra data" of Babel TLVs (see Section 4.3 of RFC6126),
 * their encoding is similar to the encoding of TLVs, but the type namespace is
 * different:
 *
 * o Type 0 stands for Pad1 sub-TLV with the same encoding as the Pad1 TLV.
 * o Type 1 stands for PadN sub-TLV with the same encoding as the PadN TLV.
 * o Type 2 stands for Diversity sub-TLV, which propagates diversity routing
 *   data. Its body is a variable-length sequence of 8-bit unsigned integers,
 *   each representing per-hop number of interferring radio channel for the
 *   prefix. Channel 0 is invalid and must not be used in the sub-TLV, channel
 *   255 interferes with any other channel.
 * o Type 3 stands for Timestamp sub-TLV, used to compute RTT between
 *   neighbours. In the case of a Hello TLV, the body stores a 32-bits
 *   timestamp, while in the case of a IHU TLV, two 32-bits timestamps are
 *   stored.
 *
 * Sub-TLV types 0 and 1 are valid for any TLV type, whether sub-TLV type 2 is
 * only valid for TLV type 8 (Update). Note that within an Update TLV a missing
 * Diversity sub-TLV is not the same as a Diversity sub-TLV with an empty body.
 * The former would mean a lack of any claims about the interference, and the
 * latter would state that interference is definitely absent.
 * A type 3 sub-TLV is valid both for Hello and IHU TLVs, though the exact
 * semantic of the sub-TLV is different in each case.
 */
static void
subtlvs_print(netdissect_options *ndo,
              const u_char *cp, const u_char *ep, const uint8_t tlv_type)
{
    uint8_t subtype, sublen;
    const char *sep;
    uint32_t t1, t2;

    while (cp < ep) {
        subtype = *cp++;
        if(subtype == MESSAGE_SUB_PAD1) {
            ND_PRINT((ndo, " sub-pad1"));
            continue;
        }
        if(cp == ep)
            goto invalid;
        sublen = *cp++;
        if(cp + sublen > ep)
            goto invalid;

        switch(subtype) {
        case MESSAGE_SUB_PADN:
            ND_PRINT((ndo, " sub-padn"));
            cp += sublen;
            break;
        case MESSAGE_SUB_DIVERSITY:
            ND_PRINT((ndo, " sub-diversity"));
            if (sublen == 0) {
                ND_PRINT((ndo, " empty"));
                break;
            }
            sep = " ";
            while(sublen--) {
                ND_PRINT((ndo, "%s%s", sep, tok2str(diversity_str, "%u", *cp++)));
                sep = "-";
            }
            if(tlv_type != MESSAGE_UPDATE &&
               tlv_type != MESSAGE_UPDATE_SRC_SPECIFIC)
                ND_PRINT((ndo, " (bogus)"));
            break;
        case MESSAGE_SUB_TIMESTAMP:
            ND_PRINT((ndo, " sub-timestamp"));
            if(tlv_type == MESSAGE_HELLO) {
                if(sublen < 4)
                    goto invalid;
                t1 = EXTRACT_32BITS(cp);
                ND_PRINT((ndo, " %s", format_timestamp(t1)));
            } else if(tlv_type == MESSAGE_IHU) {
                if(sublen < 8)
                    goto invalid;
                t1 = EXTRACT_32BITS(cp);
                ND_PRINT((ndo, " %s", format_timestamp(t1)));
                t2 = EXTRACT_32BITS(cp + 4);
                ND_PRINT((ndo, "|%s", format_timestamp(t2)));
            } else
                ND_PRINT((ndo, " (bogus)"));
            cp += sublen;
            break;
        default:
            ND_PRINT((ndo, " sub-unknown-0x%02x", subtype));
            cp += sublen;
        } /* switch */
    } /* while */
    return;

 invalid:
    ND_PRINT((ndo, "%s", istr));
}

#define ICHECK(i, l) \
	if ((i) + (l) > bodylen || (i) + (l) > length) goto invalid;

static void
babel_print_v2(netdissect_options *ndo,
               const u_char *cp, u_int length)
{
    u_int i;
    u_short bodylen;
    u_char v4_prefix[16] =
        {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0xFF, 0xFF, 0, 0, 0, 0 };
    u_char v6_prefix[16] = {0};

    ND_TCHECK2(*cp, 4);
    if (length < 4)
        goto invalid;
    bodylen = EXTRACT_16BITS(cp + 2);
    ND_PRINT((ndo, " (%u)", bodylen));

    /* Process the TLVs in the body */
    i = 0;
    while(i < bodylen) {
        const u_char *message;
        u_int type, len;

        message = cp + 4 + i;

        ND_TCHECK2(*message, 1);
        if((type = message[0]) == MESSAGE_PAD1) {
            ND_PRINT((ndo, ndo->ndo_vflag ? "\n\tPad 1" : " pad1"));
            i += 1;
            continue;
        }

        ND_TCHECK2(*message, 2);
        ICHECK(i, 2);
        len = message[1];

        ND_TCHECK2(*message, 2 + len);
        ICHECK(i, 2 + len);

        switch(type) {
        case MESSAGE_PADN: {
            if (!ndo->ndo_vflag)
                ND_PRINT((ndo, " padN"));
            else
                ND_PRINT((ndo, "\n\tPad %d", len + 2));
        }
            break;

        case MESSAGE_ACK_REQ: {
            u_short nonce, interval;
            if (!ndo->ndo_vflag)
                ND_PRINT((ndo, " ack-req"));
            else {
                ND_PRINT((ndo, "\n\tAcknowledgment Request "));
                if(len < 6) goto invalid;
                nonce = EXTRACT_16BITS(message + 4);
                interval = EXTRACT_16BITS(message + 6);
                ND_PRINT((ndo, "%04x %s", nonce, format_interval(interval)));
            }
        }
            break;

        case MESSAGE_ACK: {
            u_short nonce;
            if (!ndo->ndo_vflag)
                ND_PRINT((ndo, " ack"));
            else {
                ND_PRINT((ndo, "\n\tAcknowledgment "));
                if(len < 2) goto invalid;
                nonce = EXTRACT_16BITS(message + 2);
                ND_PRINT((ndo, "%04x", nonce));
            }
        }
            break;

        case MESSAGE_HELLO:  {
            u_short seqno, interval;
            if (!ndo->ndo_vflag)
                ND_PRINT((ndo, " hello"));
            else {
                ND_PRINT((ndo, "\n\tHello "));
                if(len < 6) goto invalid;
                seqno = EXTRACT_16BITS(message + 4);
                interval = EXTRACT_16BITS(message + 6);
                ND_PRINT((ndo, "seqno %u interval %s", seqno, format_interval(interval)));
                /* Extra data. */
                if(len > 6)
                    subtlvs_print(ndo, message + 8, message + 2 + len, type);
            }
        }
            break;

        case MESSAGE_IHU: {
            unsigned short txcost, interval;
            if (!ndo->ndo_vflag)
                ND_PRINT((ndo, " ihu"));
            else {
                u_char address[16];
                int rc;
                ND_PRINT((ndo, "\n\tIHU "));
                if(len < 6) goto invalid;
                txcost = EXTRACT_16BITS(message + 4);
                interval = EXTRACT_16BITS(message + 6);
                rc = network_address(message[2], message + 8, len - 6, address);
                if(rc < 0) { ND_PRINT((ndo, "%s", tstr)); break; }
                ND_PRINT((ndo, "%s txcost %u interval %s",
                       format_address(ndo, address), txcost, format_interval(interval)));
                /* Extra data. */
                if((u_int)rc < len - 6)
                    subtlvs_print(ndo, message + 8 + rc, message + 2 + len,
                                  type);
            }
        }
            break;

        case MESSAGE_ROUTER_ID: {
            if (!ndo->ndo_vflag)
                ND_PRINT((ndo, " router-id"));
            else {
                ND_PRINT((ndo, "\n\tRouter Id"));
                if(len < 10) goto invalid;
                ND_PRINT((ndo, " %s", format_id(message + 4)));
            }
        }
            break;

        case MESSAGE_NH: {
            if (!ndo->ndo_vflag)
                ND_PRINT((ndo, " nh"));
            else {
                int rc;
                u_char nh[16];
                ND_PRINT((ndo, "\n\tNext Hop"));
                if(len < 2) goto invalid;
                rc = network_address(message[2], message + 4, len - 2, nh);
                if(rc < 0) goto invalid;
                ND_PRINT((ndo, " %s", format_address(ndo, nh)));
            }
        }
            break;

        case MESSAGE_UPDATE: {
            if (!ndo->ndo_vflag) {
                ND_PRINT((ndo, " update"));
                if(len < 1)
                    ND_PRINT((ndo, "/truncated"));
                else
                    ND_PRINT((ndo, "%s%s%s",
                           (message[3] & 0x80) ? "/prefix": "",
                           (message[3] & 0x40) ? "/id" : "",
                           (message[3] & 0x3f) ? "/unknown" : ""));
            } else {
                u_short interval, seqno, metric;
                u_char plen;
                int rc;
                u_char prefix[16];
                ND_PRINT((ndo, "\n\tUpdate"));
                if(len < 10) goto invalid;
                plen = message[4] + (message[2] == 1 ? 96 : 0);
                rc = network_prefix(message[2], message[4], message[5],
                                    message + 12,
                                    message[2] == 1 ? v4_prefix : v6_prefix,
                                    len - 10, prefix);
                if(rc < 0) goto invalid;
                interval = EXTRACT_16BITS(message + 6);
                seqno = EXTRACT_16BITS(message + 8);
                metric = EXTRACT_16BITS(message + 10);
                ND_PRINT((ndo, "%s%s%s %s metric %u seqno %u interval %s",
                       (message[3] & 0x80) ? "/prefix": "",
                       (message[3] & 0x40) ? "/id" : "",
                       (message[3] & 0x3f) ? "/unknown" : "",
                       format_prefix(ndo, prefix, plen),
                       metric, seqno, format_interval_update(interval)));
                if(message[3] & 0x80) {
                    if(message[2] == 1)
                        memcpy(v4_prefix, prefix, 16);
                    else
                        memcpy(v6_prefix, prefix, 16);
                }
                /* extra data? */
                if((u_int)rc < len - 10)
                    subtlvs_print(ndo, message + 12 + rc, message + 2 + len, type);
            }
        }
            break;

        case MESSAGE_REQUEST: {
            if (!ndo->ndo_vflag)
                ND_PRINT((ndo, " request"));
            else {
                int rc;
                u_char prefix[16], plen;
                ND_PRINT((ndo, "\n\tRequest "));
                if(len < 2) goto invalid;
                plen = message[3] + (message[2] == 1 ? 96 : 0);
                rc = network_prefix(message[2], message[3], 0,
                                    message + 4, NULL, len - 2, prefix);
                if(rc < 0) goto invalid;
                ND_PRINT((ndo, "for %s",
                       message[2] == 0 ? "any" : format_prefix(ndo, prefix, plen)));
            }
        }
            break;

        case MESSAGE_MH_REQUEST : {
            if (!ndo->ndo_vflag)
                ND_PRINT((ndo, " mh-request"));
            else {
                int rc;
                u_short seqno;
                u_char prefix[16], plen;
                ND_PRINT((ndo, "\n\tMH-Request "));
                if(len < 14) goto invalid;
                seqno = EXTRACT_16BITS(message + 4);
                rc = network_prefix(message[2], message[3], 0,
                                    message + 16, NULL, len - 14, prefix);
                if(rc < 0) goto invalid;
                plen = message[3] + (message[2] == 1 ? 96 : 0);
                ND_PRINT((ndo, "(%u hops) for %s seqno %u id %s",
                       message[6], format_prefix(ndo, prefix, plen),
                       seqno, format_id(message + 8)));
            }
        }
            break;
        case MESSAGE_TSPC :
            if (!ndo->ndo_vflag)
                ND_PRINT((ndo, " tspc"));
            else {
                ND_PRINT((ndo, "\n\tTS/PC "));
                if(len < 6) goto invalid;
                ND_PRINT((ndo, "timestamp %u packetcounter %u", EXTRACT_32BITS (message + 4),
                       EXTRACT_16BITS(message + 2)));
            }
            break;
        case MESSAGE_HMAC : {
            if (!ndo->ndo_vflag)
                ND_PRINT((ndo, " hmac"));
            else {
                unsigned j;
                ND_PRINT((ndo, "\n\tHMAC "));
                if(len < 18) goto invalid;
                ND_PRINT((ndo, "key-id %u digest-%u ", EXTRACT_16BITS(message + 2), len - 2));
                for (j = 0; j < len - 2; j++)
                    ND_PRINT((ndo, "%02X", message[4 + j]));
            }
        }
            break;

        case MESSAGE_UPDATE_SRC_SPECIFIC : {
            if(!ndo->ndo_vflag) {
                ND_PRINT((ndo, " ss-update"));
            } else {
                u_char prefix[16], src_prefix[16];
                u_short interval, seqno, metric;
                u_char ae, plen, src_plen, omitted;
                int rc;
                int parsed_len = 10;
                ND_PRINT((ndo, "\n\tSS-Update"));
                if(len < 10) goto invalid;
                ae = message[2];
                src_plen = message[3];
                plen = message[4];
                omitted = message[5];
                interval = EXTRACT_16BITS(message + 6);
                seqno = EXTRACT_16BITS(message + 8);
                metric = EXTRACT_16BITS(message + 10);
                rc = network_prefix(ae, plen, omitted, message + 2 + parsed_len,
                                    ae == 1 ? v4_prefix : v6_prefix,
                                    len - parsed_len, prefix);
                if(rc < 0) goto invalid;
                if(ae == 1)
                    plen += 96;
                parsed_len += rc;
                rc = network_prefix(ae, src_plen, 0, message + 2 + parsed_len,
                                    NULL, len - parsed_len, src_prefix);
                if(rc < 0) goto invalid;
                if(ae == 1)
                    src_plen += 96;
                parsed_len += rc;

                ND_PRINT((ndo, " %s from", format_prefix(ndo, prefix, plen)));
                ND_PRINT((ndo, " %s metric %u seqno %u interval %s",
                          format_prefix(ndo, src_prefix, src_plen),
                          metric, seqno, format_interval_update(interval)));
                /* extra data? */
                if((u_int)parsed_len < len)
                    subtlvs_print(ndo, message + 2 + parsed_len,
                                  message + 2 + len, type);
            }
        }
            break;

        case MESSAGE_REQUEST_SRC_SPECIFIC : {
            if(!ndo->ndo_vflag)
                ND_PRINT((ndo, " ss-request"));
            else {
                int rc, parsed_len = 3;
                u_char ae, plen, src_plen, prefix[16], src_prefix[16];
                ND_PRINT((ndo, "\n\tSS-Request "));
                if(len < 3) goto invalid;
                ae = message[2];
                plen = message[3];
                src_plen = message[4];
                rc = network_prefix(ae, plen, 0, message + 2 + parsed_len,
                                    NULL, len - parsed_len, prefix);
                if(rc < 0) goto invalid;
                if(ae == 1)
                    plen += 96;
                parsed_len += rc;
                rc = network_prefix(ae, src_plen, 0, message + 2 + parsed_len,
                                    NULL, len - parsed_len, src_prefix);
                if(rc < 0) goto invalid;
                if(ae == 1)
                    src_plen += 96;
                parsed_len += rc;
                if(ae == 0) {
                    ND_PRINT((ndo, "for any"));
                } else {
                    ND_PRINT((ndo, "for (%s, ", format_prefix(ndo, prefix, plen)));
                    ND_PRINT((ndo, "%s)", format_prefix(ndo, src_prefix, src_plen)));
                }
            }
        }
            break;

        case MESSAGE_MH_REQUEST_SRC_SPECIFIC : {
            if(!ndo->ndo_vflag)
                ND_PRINT((ndo, " ss-mh-request"));
            else {
                int rc, parsed_len = 14;
                u_short seqno;
                u_char ae, plen, src_plen, prefix[16], src_prefix[16], hopc;
                const u_char *router_id = NULL;
                ND_PRINT((ndo, "\n\tSS-MH-Request "));
                if(len < 14) goto invalid;
                ae = message[2];
                plen = message[3];
                seqno = EXTRACT_16BITS(message + 4);
                hopc = message[6];
                src_plen = message[7];
                router_id = message + 8;
                rc = network_prefix(ae, plen, 0, message + 2 + parsed_len,
                                    NULL, len - parsed_len, prefix);
                if(rc < 0) goto invalid;
                if(ae == 1)
                    plen += 96;
                parsed_len += rc;
                rc = network_prefix(ae, src_plen, 0, message + 2 + parsed_len,
                                    NULL, len - parsed_len, src_prefix);
                if(rc < 0) goto invalid;
                if(ae == 1)
                    src_plen += 96;
                ND_PRINT((ndo, "(%u hops) for (%s, ",
                          hopc, format_prefix(ndo, prefix, plen)));
                ND_PRINT((ndo, "%s) seqno %u id %s",
                          format_prefix(ndo, src_prefix, src_plen),
                          seqno, format_id(router_id)));
            }
        }
            break;

        default:
            if (!ndo->ndo_vflag)
                ND_PRINT((ndo, " unknown"));
            else
                ND_PRINT((ndo, "\n\tUnknown message type %d", type));
        }
        i += len + 2;
    }
    return;

 trunc:
    ND_PRINT((ndo, " %s", tstr));
    return;

 invalid:
    ND_PRINT((ndo, "%s", istr));
    return;
}
