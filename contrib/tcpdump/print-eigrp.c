/*
 * Copyright (c) 1998-2004  Hannes Gredler <hannes@gredler.at>
 *      The TCPDUMP project
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that: (1) source code
 * distributions retain the above copyright notice and this paragraph
 * in its entirety, and (2) distributions including binary code include
 * the above copyright notice and this paragraph in its entirety in
 * the documentation or other materials provided with the distribution.
 * THIS SOFTWARE IS PROVIDED ``AS IS'' AND
 * WITHOUT ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, WITHOUT
 * LIMITATION, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
 * FOR A PARTICULAR PURPOSE.
 */

/* \summary: Enhanced Interior Gateway Routing Protocol (EIGRP) printer */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <netdissect-stdinc.h>

#include <string.h>

#include "netdissect.h"
#include "extract.h"
#include "addrtoname.h"

/*
 * packet format documented at
 * http://www.rhyshaden.com/eigrp.htm
 * RFC 7868
 */

struct eigrp_common_header {
    uint8_t version;
    uint8_t opcode;
    uint8_t checksum[2];
    uint8_t flags[4];
    uint8_t seq[4];
    uint8_t ack[4];
    uint8_t asn[4];
};

#define	EIGRP_VERSION                        2

#define	EIGRP_OPCODE_UPDATE                  1
#define	EIGRP_OPCODE_QUERY                   3
#define	EIGRP_OPCODE_REPLY                   4
#define	EIGRP_OPCODE_HELLO                   5
#define	EIGRP_OPCODE_IPXSAP                  6
#define	EIGRP_OPCODE_PROBE                   7

static const struct tok eigrp_opcode_values[] = {
    { EIGRP_OPCODE_UPDATE, "Update" },
    { EIGRP_OPCODE_QUERY, "Query" },
    { EIGRP_OPCODE_REPLY, "Reply" },
    { EIGRP_OPCODE_HELLO, "Hello" },
    { EIGRP_OPCODE_IPXSAP, "IPX SAP" },
    { EIGRP_OPCODE_PROBE, "Probe" },
    { 0, NULL}
};

static const struct tok eigrp_common_header_flag_values[] = {
    { 0x01, "Init" },
    { 0x02, "Conditionally Received" },
    { 0, NULL}
};

struct eigrp_tlv_header {
    uint8_t type[2];
    uint8_t length[2];
};

#define EIGRP_TLV_GENERAL_PARM   0x0001
#define EIGRP_TLV_AUTH           0x0002
#define EIGRP_TLV_SEQ            0x0003
#define EIGRP_TLV_SW_VERSION     0x0004
#define EIGRP_TLV_MCAST_SEQ      0x0005
#define EIGRP_TLV_IP_INT         0x0102
#define EIGRP_TLV_IP_EXT         0x0103
#define EIGRP_TLV_AT_INT         0x0202
#define EIGRP_TLV_AT_EXT         0x0203
#define EIGRP_TLV_AT_CABLE_SETUP 0x0204
#define EIGRP_TLV_IPX_INT        0x0302
#define EIGRP_TLV_IPX_EXT        0x0303

static const struct tok eigrp_tlv_values[] = {
    { EIGRP_TLV_GENERAL_PARM, "General Parameters"},
    { EIGRP_TLV_AUTH, "Authentication"},
    { EIGRP_TLV_SEQ, "Sequence"},
    { EIGRP_TLV_SW_VERSION, "Software Version"},
    { EIGRP_TLV_MCAST_SEQ, "Next Multicast Sequence"},
    { EIGRP_TLV_IP_INT, "IP Internal routes"},
    { EIGRP_TLV_IP_EXT, "IP External routes"},
    { EIGRP_TLV_AT_INT, "AppleTalk Internal routes"},
    { EIGRP_TLV_AT_EXT, "AppleTalk External routes"},
    { EIGRP_TLV_AT_CABLE_SETUP, "AppleTalk Cable setup"},
    { EIGRP_TLV_IPX_INT, "IPX Internal routes"},
    { EIGRP_TLV_IPX_EXT, "IPX External routes"},
    { 0, NULL}
};

struct eigrp_tlv_general_parm_t {
    uint8_t k1;
    uint8_t k2;
    uint8_t k3;
    uint8_t k4;
    uint8_t k5;
    uint8_t res;
    uint8_t holdtime[2];
};

struct eigrp_tlv_sw_version_t {
    uint8_t ios_major;
    uint8_t ios_minor;
    uint8_t eigrp_major;
    uint8_t eigrp_minor;
};

struct eigrp_tlv_ip_int_t {
    uint8_t nexthop[4];
    uint8_t delay[4];
    uint8_t bandwidth[4];
    uint8_t mtu[3];
    uint8_t hopcount;
    uint8_t reliability;
    uint8_t load;
    uint8_t reserved[2];
    uint8_t plen;
    uint8_t destination; /* variable length [1-4] bytes encoding */
};

struct eigrp_tlv_ip_ext_t {
    uint8_t nexthop[4];
    uint8_t origin_router[4];
    uint8_t origin_as[4];
    uint8_t tag[4];
    uint8_t metric[4];
    uint8_t reserved[2];
    uint8_t proto_id;
    uint8_t flags;
    uint8_t delay[4];
    uint8_t bandwidth[4];
    uint8_t mtu[3];
    uint8_t hopcount;
    uint8_t reliability;
    uint8_t load;
    uint8_t reserved2[2];
    uint8_t plen;
    uint8_t destination; /* variable length [1-4] bytes encoding */
};

struct eigrp_tlv_at_cable_setup_t {
    uint8_t cable_start[2];
    uint8_t cable_end[2];
    uint8_t router_id[4];
};

struct eigrp_tlv_at_int_t {
    uint8_t nexthop[4];
    uint8_t delay[4];
    uint8_t bandwidth[4];
    uint8_t mtu[3];
    uint8_t hopcount;
    uint8_t reliability;
    uint8_t load;
    uint8_t reserved[2];
    uint8_t cable_start[2];
    uint8_t cable_end[2];
};

struct eigrp_tlv_at_ext_t {
    uint8_t nexthop[4];
    uint8_t origin_router[4];
    uint8_t origin_as[4];
    uint8_t tag[4];
    uint8_t proto_id;
    uint8_t flags;
    uint8_t metric[2];
    uint8_t delay[4];
    uint8_t bandwidth[4];
    uint8_t mtu[3];
    uint8_t hopcount;
    uint8_t reliability;
    uint8_t load;
    uint8_t reserved2[2];
    uint8_t cable_start[2];
    uint8_t cable_end[2];
};

static const struct tok eigrp_ext_proto_id_values[] = {
    { 0x01, "IGRP" },
    { 0x02, "EIGRP" },
    { 0x03, "Static" },
    { 0x04, "RIP" },
    { 0x05, "Hello" },
    { 0x06, "OSPF" },
    { 0x07, "IS-IS" },
    { 0x08, "EGP" },
    { 0x09, "BGP" },
    { 0x0a, "IDRP" },
    { 0x0b, "Connected" },
    { 0, NULL}
};

void
eigrp_print(netdissect_options *ndo, register const u_char *pptr, register u_int len)
{
    const struct eigrp_common_header *eigrp_com_header;
    const struct eigrp_tlv_header *eigrp_tlv_header;
    const u_char *tptr,*tlv_tptr;
    u_int tlen,eigrp_tlv_len,eigrp_tlv_type,tlv_tlen, byte_length, bit_length;
    uint8_t prefix[4];

    union {
        const struct eigrp_tlv_general_parm_t *eigrp_tlv_general_parm;
        const struct eigrp_tlv_sw_version_t *eigrp_tlv_sw_version;
        const struct eigrp_tlv_ip_int_t *eigrp_tlv_ip_int;
        const struct eigrp_tlv_ip_ext_t *eigrp_tlv_ip_ext;
        const struct eigrp_tlv_at_cable_setup_t *eigrp_tlv_at_cable_setup;
        const struct eigrp_tlv_at_int_t *eigrp_tlv_at_int;
        const struct eigrp_tlv_at_ext_t *eigrp_tlv_at_ext;
    } tlv_ptr;

    tptr=pptr;
    eigrp_com_header = (const struct eigrp_common_header *)pptr;
    ND_TCHECK(*eigrp_com_header);

    /*
     * Sanity checking of the header.
     */
    if (eigrp_com_header->version != EIGRP_VERSION) {
	ND_PRINT((ndo, "EIGRP version %u packet not supported",eigrp_com_header->version));
	return;
    }

    /* in non-verbose mode just lets print the basic Message Type*/
    if (ndo->ndo_vflag < 1) {
        ND_PRINT((ndo, "EIGRP %s, length: %u",
               tok2str(eigrp_opcode_values, "unknown (%u)",eigrp_com_header->opcode),
               len));
        return;
    }

    /* ok they seem to want to know everything - lets fully decode it */

    if (len < sizeof(struct eigrp_common_header)) {
        ND_PRINT((ndo, "EIGRP %s, length: %u (too short, < %u)",
               tok2str(eigrp_opcode_values, "unknown (%u)",eigrp_com_header->opcode),
               len, (u_int) sizeof(struct eigrp_common_header)));
        return;
    }
    tlen=len-sizeof(struct eigrp_common_header);

    /* FIXME print other header info */
    ND_PRINT((ndo, "\n\tEIGRP v%u, opcode: %s (%u), chksum: 0x%04x, Flags: [%s]\n\tseq: 0x%08x, ack: 0x%08x, AS: %u, length: %u",
           eigrp_com_header->version,
           tok2str(eigrp_opcode_values, "unknown, type: %u",eigrp_com_header->opcode),
           eigrp_com_header->opcode,
           EXTRACT_16BITS(&eigrp_com_header->checksum),
           tok2str(eigrp_common_header_flag_values,
                   "none",
                   EXTRACT_32BITS(&eigrp_com_header->flags)),
           EXTRACT_32BITS(&eigrp_com_header->seq),
           EXTRACT_32BITS(&eigrp_com_header->ack),
           EXTRACT_32BITS(&eigrp_com_header->asn),
           tlen));

    tptr+=sizeof(const struct eigrp_common_header);

    while(tlen>0) {
        /* did we capture enough for fully decoding the object header ? */
        ND_TCHECK2(*tptr, sizeof(struct eigrp_tlv_header));

        eigrp_tlv_header = (const struct eigrp_tlv_header *)tptr;
        eigrp_tlv_len=EXTRACT_16BITS(&eigrp_tlv_header->length);
        eigrp_tlv_type=EXTRACT_16BITS(&eigrp_tlv_header->type);


        if (eigrp_tlv_len < sizeof(struct eigrp_tlv_header) ||
            eigrp_tlv_len > tlen) {
            print_unknown_data(ndo,tptr+sizeof(struct eigrp_tlv_header),"\n\t    ",tlen);
            return;
        }

        ND_PRINT((ndo, "\n\t  %s TLV (0x%04x), length: %u",
               tok2str(eigrp_tlv_values,
                       "Unknown",
                       eigrp_tlv_type),
               eigrp_tlv_type,
               eigrp_tlv_len));

        if (eigrp_tlv_len < sizeof(struct eigrp_tlv_header)) {
                ND_PRINT((ndo, " (too short, < %u)",
                        (u_int) sizeof(struct eigrp_tlv_header)));
                break;
        }
        tlv_tptr=tptr+sizeof(struct eigrp_tlv_header);
        tlv_tlen=eigrp_tlv_len-sizeof(struct eigrp_tlv_header);

        /* did we capture enough for fully decoding the object ? */
        ND_TCHECK2(*tptr, eigrp_tlv_len);

        switch(eigrp_tlv_type) {

        case EIGRP_TLV_GENERAL_PARM:
            tlv_ptr.eigrp_tlv_general_parm = (const struct eigrp_tlv_general_parm_t *)tlv_tptr;
            if (tlv_tlen < sizeof(*tlv_ptr.eigrp_tlv_general_parm)) {
                ND_PRINT((ndo, " (too short, < %u)",
                    (u_int) (sizeof(struct eigrp_tlv_header) + sizeof(*tlv_ptr.eigrp_tlv_general_parm))));
                break;
            }

            ND_PRINT((ndo, "\n\t    holdtime: %us, k1 %u, k2 %u, k3 %u, k4 %u, k5 %u",
                   EXTRACT_16BITS(tlv_ptr.eigrp_tlv_general_parm->holdtime),
                   tlv_ptr.eigrp_tlv_general_parm->k1,
                   tlv_ptr.eigrp_tlv_general_parm->k2,
                   tlv_ptr.eigrp_tlv_general_parm->k3,
                   tlv_ptr.eigrp_tlv_general_parm->k4,
                   tlv_ptr.eigrp_tlv_general_parm->k5));
            break;

        case EIGRP_TLV_SW_VERSION:
            tlv_ptr.eigrp_tlv_sw_version = (const struct eigrp_tlv_sw_version_t *)tlv_tptr;
            if (tlv_tlen < sizeof(*tlv_ptr.eigrp_tlv_sw_version)) {
                ND_PRINT((ndo, " (too short, < %u)",
                    (u_int) (sizeof(struct eigrp_tlv_header) + sizeof(*tlv_ptr.eigrp_tlv_sw_version))));
                break;
            }

            ND_PRINT((ndo, "\n\t    IOS version: %u.%u, EIGRP version %u.%u",
                   tlv_ptr.eigrp_tlv_sw_version->ios_major,
                   tlv_ptr.eigrp_tlv_sw_version->ios_minor,
                   tlv_ptr.eigrp_tlv_sw_version->eigrp_major,
                   tlv_ptr.eigrp_tlv_sw_version->eigrp_minor));
            break;

        case EIGRP_TLV_IP_INT:
            tlv_ptr.eigrp_tlv_ip_int = (const struct eigrp_tlv_ip_int_t *)tlv_tptr;
            if (tlv_tlen < sizeof(*tlv_ptr.eigrp_tlv_ip_int)) {
                ND_PRINT((ndo, " (too short, < %u)",
                    (u_int) (sizeof(struct eigrp_tlv_header) + sizeof(*tlv_ptr.eigrp_tlv_ip_int))));
                break;
            }

            bit_length = tlv_ptr.eigrp_tlv_ip_int->plen;
            if (bit_length > 32) {
                ND_PRINT((ndo, "\n\t    illegal prefix length %u",bit_length));
                break;
            }
            byte_length = (bit_length + 7) / 8; /* variable length encoding */
            memset(prefix, 0, 4);
            memcpy(prefix,&tlv_ptr.eigrp_tlv_ip_int->destination,byte_length);

            ND_PRINT((ndo, "\n\t    IPv4 prefix: %15s/%u, nexthop: ",
                   ipaddr_string(ndo, prefix),
                   bit_length));
            if (EXTRACT_32BITS(&tlv_ptr.eigrp_tlv_ip_int->nexthop) == 0)
                ND_PRINT((ndo, "self"));
            else
                ND_PRINT((ndo, "%s",ipaddr_string(ndo, &tlv_ptr.eigrp_tlv_ip_int->nexthop)));

            ND_PRINT((ndo, "\n\t      delay %u ms, bandwidth %u Kbps, mtu %u, hop %u, reliability %u, load %u",
                   (EXTRACT_32BITS(&tlv_ptr.eigrp_tlv_ip_int->delay)/100),
                   EXTRACT_32BITS(&tlv_ptr.eigrp_tlv_ip_int->bandwidth),
                   EXTRACT_24BITS(&tlv_ptr.eigrp_tlv_ip_int->mtu),
                   tlv_ptr.eigrp_tlv_ip_int->hopcount,
                   tlv_ptr.eigrp_tlv_ip_int->reliability,
                   tlv_ptr.eigrp_tlv_ip_int->load));
            break;

        case EIGRP_TLV_IP_EXT:
            tlv_ptr.eigrp_tlv_ip_ext = (const struct eigrp_tlv_ip_ext_t *)tlv_tptr;
            if (tlv_tlen < sizeof(*tlv_ptr.eigrp_tlv_ip_ext)) {
                ND_PRINT((ndo, " (too short, < %u)",
                    (u_int) (sizeof(struct eigrp_tlv_header) + sizeof(*tlv_ptr.eigrp_tlv_ip_ext))));
                break;
            }

            bit_length = tlv_ptr.eigrp_tlv_ip_ext->plen;
            if (bit_length > 32) {
                ND_PRINT((ndo, "\n\t    illegal prefix length %u",bit_length));
                break;
            }
            byte_length = (bit_length + 7) / 8; /* variable length encoding */
            memset(prefix, 0, 4);
            memcpy(prefix,&tlv_ptr.eigrp_tlv_ip_ext->destination,byte_length);

            ND_PRINT((ndo, "\n\t    IPv4 prefix: %15s/%u, nexthop: ",
                   ipaddr_string(ndo, prefix),
                   bit_length));
            if (EXTRACT_32BITS(&tlv_ptr.eigrp_tlv_ip_ext->nexthop) == 0)
                ND_PRINT((ndo, "self"));
            else
                ND_PRINT((ndo, "%s",ipaddr_string(ndo, &tlv_ptr.eigrp_tlv_ip_ext->nexthop)));

            ND_PRINT((ndo, "\n\t      origin-router %s, origin-as %u, origin-proto %s, flags [0x%02x], tag 0x%08x, metric %u",
                   ipaddr_string(ndo, tlv_ptr.eigrp_tlv_ip_ext->origin_router),
                   EXTRACT_32BITS(tlv_ptr.eigrp_tlv_ip_ext->origin_as),
                   tok2str(eigrp_ext_proto_id_values,"unknown",tlv_ptr.eigrp_tlv_ip_ext->proto_id),
                   tlv_ptr.eigrp_tlv_ip_ext->flags,
                   EXTRACT_32BITS(tlv_ptr.eigrp_tlv_ip_ext->tag),
                   EXTRACT_32BITS(tlv_ptr.eigrp_tlv_ip_ext->metric)));

            ND_PRINT((ndo, "\n\t      delay %u ms, bandwidth %u Kbps, mtu %u, hop %u, reliability %u, load %u",
                   (EXTRACT_32BITS(&tlv_ptr.eigrp_tlv_ip_ext->delay)/100),
                   EXTRACT_32BITS(&tlv_ptr.eigrp_tlv_ip_ext->bandwidth),
                   EXTRACT_24BITS(&tlv_ptr.eigrp_tlv_ip_ext->mtu),
                   tlv_ptr.eigrp_tlv_ip_ext->hopcount,
                   tlv_ptr.eigrp_tlv_ip_ext->reliability,
                   tlv_ptr.eigrp_tlv_ip_ext->load));
            break;

        case EIGRP_TLV_AT_CABLE_SETUP:
            tlv_ptr.eigrp_tlv_at_cable_setup = (const struct eigrp_tlv_at_cable_setup_t *)tlv_tptr;
            if (tlv_tlen < sizeof(*tlv_ptr.eigrp_tlv_at_cable_setup)) {
                ND_PRINT((ndo, " (too short, < %u)",
                    (u_int) (sizeof(struct eigrp_tlv_header) + sizeof(*tlv_ptr.eigrp_tlv_at_cable_setup))));
                break;
            }

            ND_PRINT((ndo, "\n\t    Cable-range: %u-%u, Router-ID %u",
                   EXTRACT_16BITS(&tlv_ptr.eigrp_tlv_at_cable_setup->cable_start),
                   EXTRACT_16BITS(&tlv_ptr.eigrp_tlv_at_cable_setup->cable_end),
                   EXTRACT_32BITS(&tlv_ptr.eigrp_tlv_at_cable_setup->router_id)));
            break;

        case EIGRP_TLV_AT_INT:
            tlv_ptr.eigrp_tlv_at_int = (const struct eigrp_tlv_at_int_t *)tlv_tptr;
            if (tlv_tlen < sizeof(*tlv_ptr.eigrp_tlv_at_int)) {
                ND_PRINT((ndo, " (too short, < %u)",
                    (u_int) (sizeof(struct eigrp_tlv_header) + sizeof(*tlv_ptr.eigrp_tlv_at_int))));
                break;
            }

            ND_PRINT((ndo, "\n\t     Cable-Range: %u-%u, nexthop: ",
                   EXTRACT_16BITS(&tlv_ptr.eigrp_tlv_at_int->cable_start),
                   EXTRACT_16BITS(&tlv_ptr.eigrp_tlv_at_int->cable_end)));

            if (EXTRACT_32BITS(&tlv_ptr.eigrp_tlv_at_int->nexthop) == 0)
                ND_PRINT((ndo, "self"));
            else
                ND_PRINT((ndo, "%u.%u",
                       EXTRACT_16BITS(&tlv_ptr.eigrp_tlv_at_int->nexthop),
                       EXTRACT_16BITS(&tlv_ptr.eigrp_tlv_at_int->nexthop[2])));

            ND_PRINT((ndo, "\n\t      delay %u ms, bandwidth %u Kbps, mtu %u, hop %u, reliability %u, load %u",
                   (EXTRACT_32BITS(&tlv_ptr.eigrp_tlv_at_int->delay)/100),
                   EXTRACT_32BITS(&tlv_ptr.eigrp_tlv_at_int->bandwidth),
                   EXTRACT_24BITS(&tlv_ptr.eigrp_tlv_at_int->mtu),
                   tlv_ptr.eigrp_tlv_at_int->hopcount,
                   tlv_ptr.eigrp_tlv_at_int->reliability,
                   tlv_ptr.eigrp_tlv_at_int->load));
            break;

        case EIGRP_TLV_AT_EXT:
            tlv_ptr.eigrp_tlv_at_ext = (const struct eigrp_tlv_at_ext_t *)tlv_tptr;
            if (tlv_tlen < sizeof(*tlv_ptr.eigrp_tlv_at_ext)) {
                ND_PRINT((ndo, " (too short, < %u)",
                    (u_int) (sizeof(struct eigrp_tlv_header) + sizeof(*tlv_ptr.eigrp_tlv_at_ext))));
                break;
            }

            ND_PRINT((ndo, "\n\t     Cable-Range: %u-%u, nexthop: ",
                   EXTRACT_16BITS(&tlv_ptr.eigrp_tlv_at_ext->cable_start),
                   EXTRACT_16BITS(&tlv_ptr.eigrp_tlv_at_ext->cable_end)));

            if (EXTRACT_32BITS(&tlv_ptr.eigrp_tlv_at_ext->nexthop) == 0)
                ND_PRINT((ndo, "self"));
            else
                ND_PRINT((ndo, "%u.%u",
                       EXTRACT_16BITS(&tlv_ptr.eigrp_tlv_at_ext->nexthop),
                       EXTRACT_16BITS(&tlv_ptr.eigrp_tlv_at_ext->nexthop[2])));

            ND_PRINT((ndo, "\n\t      origin-router %u, origin-as %u, origin-proto %s, flags [0x%02x], tag 0x%08x, metric %u",
                   EXTRACT_32BITS(tlv_ptr.eigrp_tlv_at_ext->origin_router),
                   EXTRACT_32BITS(tlv_ptr.eigrp_tlv_at_ext->origin_as),
                   tok2str(eigrp_ext_proto_id_values,"unknown",tlv_ptr.eigrp_tlv_at_ext->proto_id),
                   tlv_ptr.eigrp_tlv_at_ext->flags,
                   EXTRACT_32BITS(tlv_ptr.eigrp_tlv_at_ext->tag),
                   EXTRACT_16BITS(tlv_ptr.eigrp_tlv_at_ext->metric)));

            ND_PRINT((ndo, "\n\t      delay %u ms, bandwidth %u Kbps, mtu %u, hop %u, reliability %u, load %u",
                   (EXTRACT_32BITS(&tlv_ptr.eigrp_tlv_at_ext->delay)/100),
                   EXTRACT_32BITS(&tlv_ptr.eigrp_tlv_at_ext->bandwidth),
                   EXTRACT_24BITS(&tlv_ptr.eigrp_tlv_at_ext->mtu),
                   tlv_ptr.eigrp_tlv_at_ext->hopcount,
                   tlv_ptr.eigrp_tlv_at_ext->reliability,
                   tlv_ptr.eigrp_tlv_at_ext->load));
            break;

            /*
             * FIXME those are the defined TLVs that lack a decoder
             * you are welcome to contribute code ;-)
             */

        case EIGRP_TLV_AUTH:
        case EIGRP_TLV_SEQ:
        case EIGRP_TLV_MCAST_SEQ:
        case EIGRP_TLV_IPX_INT:
        case EIGRP_TLV_IPX_EXT:

        default:
            if (ndo->ndo_vflag <= 1)
                print_unknown_data(ndo,tlv_tptr,"\n\t    ",tlv_tlen);
            break;
        }
        /* do we want to see an additionally hexdump ? */
        if (ndo->ndo_vflag > 1)
            print_unknown_data(ndo,tptr+sizeof(struct eigrp_tlv_header),"\n\t    ",
                               eigrp_tlv_len-sizeof(struct eigrp_tlv_header));

        tptr+=eigrp_tlv_len;
        tlen-=eigrp_tlv_len;
    }
    return;
trunc:
    ND_PRINT((ndo, "\n\t\t packet exceeded snapshot"));
}
