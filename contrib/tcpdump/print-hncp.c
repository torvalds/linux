/*
 * Copyright (c) 2016 Antonin Décimo, Jean-Raphaël Gaglione
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

/* \summary: Home Networking Control Protocol (HNCP) printer */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <netdissect-stdinc.h>

#include <stdlib.h>
#include <string.h>

#include "netdissect.h"
#include "addrtoname.h"
#include "extract.h"

static void
hncp_print_rec(netdissect_options *ndo,
               const u_char *cp, u_int length, int indent);

void
hncp_print(netdissect_options *ndo,
           const u_char *cp, u_int length)
{
    ND_PRINT((ndo, "hncp (%d)", length));
    hncp_print_rec(ndo, cp, length, 1);
}

/* RFC7787 */
#define DNCP_REQUEST_NETWORK_STATE  1
#define DNCP_REQUEST_NODE_STATE     2
#define DNCP_NODE_ENDPOINT          3
#define DNCP_NETWORK_STATE          4
#define DNCP_NODE_STATE             5
#define DNCP_PEER                   8
#define DNCP_KEEP_ALIVE_INTERVAL    9
#define DNCP_TRUST_VERDICT         10

/* RFC7788 */
#define HNCP_HNCP_VERSION          32
#define HNCP_EXTERNAL_CONNECTION   33
#define HNCP_DELEGATED_PREFIX      34
#define HNCP_PREFIX_POLICY         43
#define HNCP_DHCPV4_DATA           37
#define HNCP_DHCPV6_DATA           38
#define HNCP_ASSIGNED_PREFIX       35
#define HNCP_NODE_ADDRESS          36
#define HNCP_DNS_DELEGATED_ZONE    39
#define HNCP_DOMAIN_NAME           40
#define HNCP_NODE_NAME             41
#define HNCP_MANAGED_PSK           42

/* See type_mask in hncp_print_rec below */
#define RANGE_DNCP_RESERVED    0x10000
#define RANGE_HNCP_UNASSIGNED  0x10001
#define RANGE_DNCP_PRIVATE_USE 0x10002
#define RANGE_DNCP_FUTURE_USE  0x10003

static const struct tok type_values[] = {
    { DNCP_REQUEST_NETWORK_STATE, "Request network state" },
    { DNCP_REQUEST_NODE_STATE,    "Request node state" },
    { DNCP_NODE_ENDPOINT,         "Node endpoint" },
    { DNCP_NETWORK_STATE,         "Network state" },
    { DNCP_NODE_STATE,            "Node state" },
    { DNCP_PEER,                  "Peer" },
    { DNCP_KEEP_ALIVE_INTERVAL,   "Keep-alive interval" },
    { DNCP_TRUST_VERDICT,         "Trust-Verdict" },

    { HNCP_HNCP_VERSION,        "HNCP-Version" },
    { HNCP_EXTERNAL_CONNECTION, "External-Connection" },
    { HNCP_DELEGATED_PREFIX,    "Delegated-Prefix" },
    { HNCP_PREFIX_POLICY,       "Prefix-Policy" },
    { HNCP_DHCPV4_DATA,         "DHCPv4-Data" },
    { HNCP_DHCPV6_DATA,         "DHCPv6-Data" },
    { HNCP_ASSIGNED_PREFIX,     "Assigned-Prefix" },
    { HNCP_NODE_ADDRESS,        "Node-Address" },
    { HNCP_DNS_DELEGATED_ZONE,  "DNS-Delegated-Zone" },
    { HNCP_DOMAIN_NAME,         "Domain-Name" },
    { HNCP_NODE_NAME,           "Node-Name" },
    { HNCP_MANAGED_PSK,         "Managed-PSK" },

    { RANGE_DNCP_RESERVED,    "Reserved" },
    { RANGE_HNCP_UNASSIGNED,  "Unassigned" },
    { RANGE_DNCP_PRIVATE_USE, "Private use" },
    { RANGE_DNCP_FUTURE_USE,  "Future use" },

    { 0, NULL}
};

#define DH4OPT_DNS_SERVERS 6     /* RFC2132 */
#define DH4OPT_NTP_SERVERS 42    /* RFC2132 */
#define DH4OPT_DOMAIN_SEARCH 119 /* RFC3397 */

static const struct tok dh4opt_str[] = {
    { DH4OPT_DNS_SERVERS, "DNS-server" },
    { DH4OPT_NTP_SERVERS, "NTP-server"},
    { DH4OPT_DOMAIN_SEARCH, "DNS-search" },
    { 0, NULL }
};

#define DH6OPT_DNS_SERVERS 23   /* RFC3646 */
#define DH6OPT_DOMAIN_LIST 24   /* RFC3646 */
#define DH6OPT_SNTP_SERVERS 31  /* RFC4075 */

static const struct tok dh6opt_str[] = {
    { DH6OPT_DNS_SERVERS,  "DNS-server" },
    { DH6OPT_DOMAIN_LIST,  "DNS-search-list" },
    { DH6OPT_SNTP_SERVERS, "SNTP-servers" },
    { 0, NULL }
};

/*
 * For IPv4-mapped IPv6 addresses, length of the prefix that precedes
 * the 4 bytes of IPv4 address at the end of the IPv6 address.
 */
#define IPV4_MAPPED_HEADING_LEN    12

/*
 * Is an IPv6 address an IPv4-mapped address?
 */
static inline int
is_ipv4_mapped_address(const u_char *addr)
{
    /* The value of the prefix */
    static const u_char ipv4_mapped_heading[IPV4_MAPPED_HEADING_LEN] =
        { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0xFF, 0xFF };

    return memcmp(addr, ipv4_mapped_heading, IPV4_MAPPED_HEADING_LEN) == 0;
}

static const char *
format_nid(const u_char *data)
{
    static char buf[4][11+5];
    static int i = 0;
    i = (i + 1) % 4;
    snprintf(buf[i], 16, "%02x:%02x:%02x:%02x",
             data[0], data[1], data[2], data[3]);
    return buf[i];
}

static const char *
format_256(const u_char *data)
{
    static char buf[4][64+5];
    static int i = 0;
    i = (i + 1) % 4;
    snprintf(buf[i], 28, "%016" PRIx64 "%016" PRIx64 "%016" PRIx64 "%016" PRIx64,
         EXTRACT_64BITS(data),
         EXTRACT_64BITS(data + 8),
         EXTRACT_64BITS(data + 16),
         EXTRACT_64BITS(data + 24)
    );
    return buf[i];
}

static const char *
format_interval(const uint32_t n)
{
    static char buf[4][sizeof("0000000.000s")];
    static int i = 0;
    i = (i + 1) % 4;
    snprintf(buf[i], sizeof(buf[i]), "%u.%03us", n / 1000, n % 1000);
    return buf[i];
}

static const char *
format_ip6addr(netdissect_options *ndo, const u_char *cp)
{
    if (is_ipv4_mapped_address(cp))
        return ipaddr_string(ndo, cp + IPV4_MAPPED_HEADING_LEN);
    else
        return ip6addr_string(ndo, cp);
}

static int
print_prefix(netdissect_options *ndo, const u_char *prefix, u_int max_length)
{
    int plenbytes;
    char buf[sizeof("xxxx:xxxx:xxxx:xxxx:xxxx:xxxx:xxxx:xxxx::/128")];

    if (prefix[0] >= 96 && max_length >= IPV4_MAPPED_HEADING_LEN + 1 &&
        is_ipv4_mapped_address(&prefix[1])) {
        struct in_addr addr;
        u_int plen;

        plen = prefix[0]-96;
        if (32 < plen)
            return -1;
        max_length -= 1;

        memset(&addr, 0, sizeof(addr));
        plenbytes = (plen + 7) / 8;
        if (max_length < (u_int)plenbytes + IPV4_MAPPED_HEADING_LEN)
            return -3;
        memcpy(&addr, &prefix[1 + IPV4_MAPPED_HEADING_LEN], plenbytes);
        if (plen % 8) {
		((u_char *)&addr)[plenbytes - 1] &=
			((0xff00 >> (plen % 8)) & 0xff);
	}
	snprintf(buf, sizeof(buf), "%s/%d", ipaddr_string(ndo, &addr), plen);
        plenbytes += 1 + IPV4_MAPPED_HEADING_LEN;
    } else {
        plenbytes = decode_prefix6(ndo, prefix, max_length, buf, sizeof(buf));
    }

    ND_PRINT((ndo, "%s", buf));
    return plenbytes;
}

static int
print_dns_label(netdissect_options *ndo,
                const u_char *cp, u_int max_length, int print)
{
    u_int length = 0;
    while (length < max_length) {
        u_int lab_length = cp[length++];
        if (lab_length == 0)
            return (int)length;
        if (length > 1 && print)
            safeputchar(ndo, '.');
        if (length+lab_length > max_length) {
            if (print)
                safeputs(ndo, cp+length, max_length-length);
            break;
        }
        if (print)
            safeputs(ndo, cp+length, lab_length);
        length += lab_length;
    }
    if (print)
        ND_PRINT((ndo, "[|DNS]"));
    return -1;
}

static int
dhcpv4_print(netdissect_options *ndo,
             const u_char *cp, u_int length, int indent)
{
    u_int i, t;
    const u_char *tlv, *value;
    uint8_t type, optlen;

    i = 0;
    while (i < length) {
        if (i + 2 > length)
            return -1;
        tlv = cp + i;
        type = (uint8_t)tlv[0];
        optlen = (uint8_t)tlv[1];
        value = tlv + 2;

        ND_PRINT((ndo, "\n"));
        for (t = indent; t > 0; t--)
            ND_PRINT((ndo, "\t"));

        ND_PRINT((ndo, "%s", tok2str(dh4opt_str, "Unknown", type)));
        ND_PRINT((ndo," (%u)", optlen + 2 ));
        if (i + 2 + optlen > length)
            return -1;

        switch (type) {
        case DH4OPT_DNS_SERVERS:
        case DH4OPT_NTP_SERVERS: {
            if (optlen < 4 || optlen % 4 != 0) {
                return -1;
            }
            for (t = 0; t < optlen; t += 4)
                ND_PRINT((ndo, " %s", ipaddr_string(ndo, value + t)));
        }
            break;
        case DH4OPT_DOMAIN_SEARCH: {
            const u_char *tp = value;
            while (tp < value + optlen) {
                ND_PRINT((ndo, " "));
                if ((tp = ns_nprint(ndo, tp, value + optlen)) == NULL)
                    return -1;
            }
        }
            break;
        }

        i += 2 + optlen;
    }
    return 0;
}

static int
dhcpv6_print(netdissect_options *ndo,
             const u_char *cp, u_int length, int indent)
{
    u_int i, t;
    const u_char *tlv, *value;
    uint16_t type, optlen;

    i = 0;
    while (i < length) {
        if (i + 4 > length)
            return -1;
        tlv = cp + i;
        type = EXTRACT_16BITS(tlv);
        optlen = EXTRACT_16BITS(tlv + 2);
        value = tlv + 4;

        ND_PRINT((ndo, "\n"));
        for (t = indent; t > 0; t--)
            ND_PRINT((ndo, "\t"));

        ND_PRINT((ndo, "%s", tok2str(dh6opt_str, "Unknown", type)));
        ND_PRINT((ndo," (%u)", optlen + 4 ));
        if (i + 4 + optlen > length)
            return -1;

        switch (type) {
            case DH6OPT_DNS_SERVERS:
            case DH6OPT_SNTP_SERVERS: {
                if (optlen % 16 != 0) {
                    ND_PRINT((ndo, " %s", istr));
                    return -1;
                }
                for (t = 0; t < optlen; t += 16)
                    ND_PRINT((ndo, " %s", ip6addr_string(ndo, value + t)));
            }
                break;
            case DH6OPT_DOMAIN_LIST: {
                const u_char *tp = value;
                while (tp < value + optlen) {
                    ND_PRINT((ndo, " "));
                    if ((tp = ns_nprint(ndo, tp, value + optlen)) == NULL)
                        return -1;
                }
            }
                break;
        }

        i += 4 + optlen;
    }
    return 0;
}

/* Determine in-line mode */
static int
is_in_line(netdissect_options *ndo, int indent)
{
    return indent - 1 >= ndo->ndo_vflag && ndo->ndo_vflag < 3;
}

static void
print_type_in_line(netdissect_options *ndo,
                   uint32_t type, int count, int indent, int *first_one)
{
    if (count > 0) {
        if (*first_one) {
            *first_one = 0;
            if (indent > 1) {
                u_int t;
                ND_PRINT((ndo, "\n"));
                for (t = indent; t > 0; t--)
                    ND_PRINT((ndo, "\t"));
            } else {
                ND_PRINT((ndo, " "));
            }
        } else {
            ND_PRINT((ndo, ", "));
        }
        ND_PRINT((ndo, "%s", tok2str(type_values, "Easter Egg", type)));
        if (count > 1)
            ND_PRINT((ndo, " (x%d)", count));
    }
}

void
hncp_print_rec(netdissect_options *ndo,
               const u_char *cp, u_int length, int indent)
{
    const int in_line = is_in_line(ndo, indent);
    int first_one = 1;

    u_int i, t;

    uint32_t last_type_mask = 0xffffffffU;
    int last_type_count = -1;

    const u_char *tlv, *value;
    uint16_t type, bodylen;
    uint32_t type_mask;

    i = 0;
    while (i < length) {
        tlv = cp + i;

        if (!in_line) {
            ND_PRINT((ndo, "\n"));
            for (t = indent; t > 0; t--)
                ND_PRINT((ndo, "\t"));
        }

        ND_TCHECK2(*tlv, 4);
        if (i + 4 > length)
            goto invalid;

        type = EXTRACT_16BITS(tlv);
        bodylen = EXTRACT_16BITS(tlv + 2);
        value = tlv + 4;
        ND_TCHECK2(*value, bodylen);
        if (i + bodylen + 4 > length)
            goto invalid;

        type_mask =
            (type == 0)                   ? RANGE_DNCP_RESERVED:
            (44 <= type && type <= 511)   ? RANGE_HNCP_UNASSIGNED:
            (768 <= type && type <= 1023) ? RANGE_DNCP_PRIVATE_USE:
                                            RANGE_DNCP_FUTURE_USE;
        if (type == 6 || type == 7)
            type_mask = RANGE_DNCP_FUTURE_USE;

        /* defined types */
        {
            t = 0;
            while (1) {
                u_int key = type_values[t++].v;
                if (key > 0xffff)
                    break;
                if (key == type) {
                    type_mask = type;
                    break;
                }
            }
        }

        if (in_line) {
            if (last_type_mask == type_mask) {
                last_type_count++;
            } else {
                print_type_in_line(ndo, last_type_mask, last_type_count, indent, &first_one);
                last_type_mask = type_mask;
                last_type_count = 1;
            }

            goto skip_multiline;
        }

        ND_PRINT((ndo,"%s", tok2str(type_values, "Easter Egg (42)", type_mask) ));
        if (type_mask > 0xffff)
            ND_PRINT((ndo,": type=%u", type ));
        ND_PRINT((ndo," (%u)", bodylen + 4 ));

        switch (type_mask) {

        case DNCP_REQUEST_NETWORK_STATE: {
            if (bodylen != 0)
                ND_PRINT((ndo, " %s", istr));
        }
            break;

        case DNCP_REQUEST_NODE_STATE: {
            const char *node_identifier;
            if (bodylen != 4) {
                ND_PRINT((ndo, " %s", istr));
                break;
            }
            node_identifier = format_nid(value);
            ND_PRINT((ndo, " NID: %s", node_identifier));
        }
            break;

        case DNCP_NODE_ENDPOINT: {
            const char *node_identifier;
            uint32_t endpoint_identifier;
            if (bodylen != 8) {
                ND_PRINT((ndo, " %s", istr));
                break;
            }
            node_identifier = format_nid(value);
            endpoint_identifier = EXTRACT_32BITS(value + 4);
            ND_PRINT((ndo, " NID: %s EPID: %08x",
                node_identifier,
                endpoint_identifier
            ));
        }
            break;

        case DNCP_NETWORK_STATE: {
            uint64_t hash;
            if (bodylen != 8) {
                ND_PRINT((ndo, " %s", istr));
                break;
            }
            hash = EXTRACT_64BITS(value);
            ND_PRINT((ndo, " hash: %016" PRIx64, hash));
        }
            break;

        case DNCP_NODE_STATE: {
            const char *node_identifier, *interval;
            uint32_t sequence_number;
            uint64_t hash;
            if (bodylen < 20) {
                ND_PRINT((ndo, " %s", istr));
                break;
            }
            node_identifier = format_nid(value);
            sequence_number = EXTRACT_32BITS(value + 4);
            interval = format_interval(EXTRACT_32BITS(value + 8));
            hash = EXTRACT_64BITS(value + 12);
            ND_PRINT((ndo, " NID: %s seqno: %u %s hash: %016" PRIx64,
                node_identifier,
                sequence_number,
                interval,
                hash
            ));
            hncp_print_rec(ndo, value+20, bodylen-20, indent+1);
        }
            break;

        case DNCP_PEER: {
            const char *peer_node_identifier;
            uint32_t peer_endpoint_identifier, endpoint_identifier;
            if (bodylen != 12) {
                ND_PRINT((ndo, " %s", istr));
                break;
            }
            peer_node_identifier = format_nid(value);
            peer_endpoint_identifier = EXTRACT_32BITS(value + 4);
            endpoint_identifier = EXTRACT_32BITS(value + 8);
            ND_PRINT((ndo, " Peer-NID: %s Peer-EPID: %08x Local-EPID: %08x",
                peer_node_identifier,
                peer_endpoint_identifier,
                endpoint_identifier
            ));
        }
            break;

        case DNCP_KEEP_ALIVE_INTERVAL: {
            uint32_t endpoint_identifier;
            const char *interval;
            if (bodylen < 8) {
                ND_PRINT((ndo, " %s", istr));
                break;
            }
            endpoint_identifier = EXTRACT_32BITS(value);
            interval = format_interval(EXTRACT_32BITS(value + 4));
            ND_PRINT((ndo, " EPID: %08x Interval: %s",
                endpoint_identifier,
                interval
            ));
        }
            break;

        case DNCP_TRUST_VERDICT: {
            if (bodylen <= 36) {
                ND_PRINT((ndo, " %s", istr));
                break;
            }
            ND_PRINT((ndo, " Verdict: %u Fingerprint: %s Common Name: ",
                *value,
                format_256(value + 4)));
            safeputs(ndo, value + 36, bodylen - 36);
        }
            break;

        case HNCP_HNCP_VERSION: {
            uint16_t capabilities;
            uint8_t M, P, H, L;
            if (bodylen < 5) {
                ND_PRINT((ndo, " %s", istr));
                break;
            }
            capabilities = EXTRACT_16BITS(value + 2);
            M = (uint8_t)((capabilities >> 12) & 0xf);
            P = (uint8_t)((capabilities >> 8) & 0xf);
            H = (uint8_t)((capabilities >> 4) & 0xf);
            L = (uint8_t)(capabilities & 0xf);
            ND_PRINT((ndo, " M: %u P: %u H: %u L: %u User-agent: ",
                M, P, H, L
            ));
            safeputs(ndo, value + 4, bodylen - 4);
        }
            break;

        case HNCP_EXTERNAL_CONNECTION: {
            /* Container TLV */
            hncp_print_rec(ndo, value, bodylen, indent+1);
        }
            break;

        case HNCP_DELEGATED_PREFIX: {
            int l;
            if (bodylen < 9 || bodylen < 9 + (value[8] + 7) / 8) {
                ND_PRINT((ndo, " %s", istr));
                break;
            }
            ND_PRINT((ndo, " VLSO: %s PLSO: %s Prefix: ",
                format_interval(EXTRACT_32BITS(value)),
                format_interval(EXTRACT_32BITS(value + 4))
            ));
            l = print_prefix(ndo, value + 8, bodylen - 8);
            if (l == -1) {
                ND_PRINT((ndo, "(length is invalid)"));
                break;
            }
            if (l < 0) {
                /*
                 * We've already checked that we've captured the
                 * entire TLV, based on its length, so this will
                 * either be -1, meaning "the prefix length is
                 * greater than the longest possible address of
                 * that type" (i.e., > 32 for IPv4 or > 128 for
                 * IPv6", or -3, meaning "the prefix runs past
                 * the end of the TLV".
                 */
                ND_PRINT((ndo, " %s", istr));
                break;
            }
            l += 8 + (-l & 3);

            if (bodylen >= l)
                hncp_print_rec(ndo, value + l, bodylen - l, indent+1);
        }
            break;

        case HNCP_PREFIX_POLICY: {
            uint8_t policy;
            int l;
            if (bodylen < 1) {
                ND_PRINT((ndo, " %s", istr));
                break;
            }
            policy = value[0];
            ND_PRINT((ndo, " type: "));
            if (policy == 0) {
                if (bodylen != 1) {
                    ND_PRINT((ndo, " %s", istr));
                    break;
                }
                ND_PRINT((ndo, "Internet connectivity"));
            } else if (policy >= 1 && policy <= 128) {
                ND_PRINT((ndo, "Dest-Prefix: "));
                l = print_prefix(ndo, value, bodylen);
                if (l == -1) {
                    ND_PRINT((ndo, "(length is invalid)"));
                    break;
                }
                if (l < 0) {
                    /*
                     * We've already checked that we've captured the
                     * entire TLV, based on its length, so this will
                     * either be -1, meaning "the prefix length is
                     * greater than the longest possible address of
                     * that type" (i.e., > 32 for IPv4 or > 128 for
                     * IPv6", or -3, meaning "the prefix runs past
                     * the end of the TLV".
                     */
                    ND_PRINT((ndo, " %s", istr));
                    break;
                }
            } else if (policy == 129) {
                ND_PRINT((ndo, "DNS domain: "));
                print_dns_label(ndo, value+1, bodylen-1, 1);
            } else if (policy == 130) {
                ND_PRINT((ndo, "Opaque UTF-8: "));
                safeputs(ndo, value + 1, bodylen - 1);
            } else if (policy == 131) {
                if (bodylen != 1) {
                    ND_PRINT((ndo, " %s", istr));
                    break;
                }
                ND_PRINT((ndo, "Restrictive assignment"));
            } else if (policy >= 132) {
                ND_PRINT((ndo, "Unknown (%u)", policy)); /* Reserved for future additions */
            }
        }
            break;

        case HNCP_DHCPV4_DATA: {
            if (bodylen == 0) {
                ND_PRINT((ndo, " %s", istr));
                break;
            }
            if (dhcpv4_print(ndo, value, bodylen, indent+1) != 0)
                goto invalid;
        }
            break;

        case HNCP_DHCPV6_DATA: {
            if (bodylen == 0) {
                ND_PRINT((ndo, " %s", istr));
                break;
            }
            if (dhcpv6_print(ndo, value, bodylen, indent+1) != 0) {
                ND_PRINT((ndo, " %s", istr));
                break;
            }
        }
            break;

        case HNCP_ASSIGNED_PREFIX: {
            uint8_t prty;
            int l;
            if (bodylen < 6 || bodylen < 6 + (value[5] + 7) / 8) {
                ND_PRINT((ndo, " %s", istr));
                break;
            }
            prty = (uint8_t)(value[4] & 0xf);
            ND_PRINT((ndo, " EPID: %08x Prty: %u",
                EXTRACT_32BITS(value),
                prty
            ));
            ND_PRINT((ndo, " Prefix: "));
            if ((l = print_prefix(ndo, value + 5, bodylen - 5)) < 0) {
                ND_PRINT((ndo, " %s", istr));
                break;
            }
            l += 5;
            l += -l & 3;

            if (bodylen >= l)
                hncp_print_rec(ndo, value + l, bodylen - l, indent+1);
        }
            break;

        case HNCP_NODE_ADDRESS: {
            uint32_t endpoint_identifier;
            const char *ip_address;
            if (bodylen < 20) {
                ND_PRINT((ndo, " %s", istr));
                break;
            }
            endpoint_identifier = EXTRACT_32BITS(value);
            ip_address = format_ip6addr(ndo, value + 4);
            ND_PRINT((ndo, " EPID: %08x IP Address: %s",
                endpoint_identifier,
                ip_address
            ));

            hncp_print_rec(ndo, value + 20, bodylen - 20, indent+1);
        }
            break;

        case HNCP_DNS_DELEGATED_ZONE: {
            const char *ip_address;
            int len;
            if (bodylen < 17) {
                ND_PRINT((ndo, " %s", istr));
                break;
            }
            ip_address = format_ip6addr(ndo, value);
            ND_PRINT((ndo, " IP-Address: %s %c%c%c ",
                ip_address,
                (value[16] & 4) ? 'l' : '-',
                (value[16] & 2) ? 'b' : '-',
                (value[16] & 1) ? 's' : '-'
            ));
            len = print_dns_label(ndo, value+17, bodylen-17, 1);
            if (len < 0) {
                ND_PRINT((ndo, " %s", istr));
                break;
            }
            len += 17;
            len += -len & 3;
            if (bodylen >= len)
                hncp_print_rec(ndo, value+len, bodylen-len, indent+1);
        }
            break;

        case HNCP_DOMAIN_NAME: {
            if (bodylen == 0) {
                ND_PRINT((ndo, " %s", istr));
                break;
            }
            ND_PRINT((ndo, " Domain: "));
            print_dns_label(ndo, value, bodylen, 1);
        }
            break;

        case HNCP_NODE_NAME: {
            u_int l;
            if (bodylen < 17) {
                ND_PRINT((ndo, " %s", istr));
                break;
            }
            l = value[16];
            if (bodylen < 17 + l) {
                ND_PRINT((ndo, " %s", istr));
                break;
            }
            ND_PRINT((ndo, " IP-Address: %s Name: ",
                format_ip6addr(ndo, value)
            ));
            if (l < 64) {
                safeputchar(ndo, '"');
                safeputs(ndo, value + 17, l);
                safeputchar(ndo, '"');
            } else {
                ND_PRINT((ndo, "%s", istr));
            }
            l += 17;
            l += -l & 3;
            if (bodylen >= l)
                hncp_print_rec(ndo, value + l, bodylen - l, indent+1);
        }
            break;

        case HNCP_MANAGED_PSK: {
            if (bodylen < 32) {
                ND_PRINT((ndo, " %s", istr));
                break;
            }
            ND_PRINT((ndo, " PSK: %s", format_256(value)));
            hncp_print_rec(ndo, value + 32, bodylen - 32, indent+1);
        }
            break;

        case RANGE_DNCP_RESERVED:
        case RANGE_HNCP_UNASSIGNED:
        case RANGE_DNCP_PRIVATE_USE:
        case RANGE_DNCP_FUTURE_USE:
            break;

        }
    skip_multiline:

        i += 4 + bodylen + (-bodylen & 3);
    }
    print_type_in_line(ndo, last_type_mask, last_type_count, indent, &first_one);

    return;

 trunc:
    ND_PRINT((ndo, "%s", "[|hncp]"));
    return;

 invalid:
    ND_PRINT((ndo, "%s", istr));
    return;
}
