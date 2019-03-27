/*
 * Copyright (c) 2000 Lennert Buytenhek
 *
 * This software may be distributed either under the terms of the
 * BSD-style license that accompanies tcpdump or the GNU General
 * Public License
 *
 * Contributed by Lennert Buytenhek <buytenh@gnu.org>
 */

/* \summary: IEEE 802.1d Spanning Tree Protocol (STP) printer */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <netdissect-stdinc.h>

#include <stdio.h>

#include "netdissect.h"
#include "extract.h"

#define	RSTP_EXTRACT_PORT_ROLE(x) (((x)&0x0C)>>2)
/* STP timers are expressed in multiples of 1/256th second */
#define STP_TIME_BASE 256
#define STP_BPDU_MSTP_MIN_LEN 102

struct stp_bpdu_ {
    uint8_t protocol_id[2];
    uint8_t protocol_version;
    uint8_t bpdu_type;
    uint8_t flags;
    uint8_t root_id[8];
    uint8_t root_path_cost[4];
    uint8_t bridge_id[8];
    uint8_t port_id[2];
    uint8_t message_age[2];
    uint8_t max_age[2];
    uint8_t hello_time[2];
    uint8_t forward_delay[2];
    uint8_t v1_length;
};

#define STP_PROTO_REGULAR 0x00
#define STP_PROTO_RAPID   0x02
#define STP_PROTO_MSTP    0x03
#define STP_PROTO_SPB     0x04

static const struct tok stp_proto_values[] = {
    { STP_PROTO_REGULAR, "802.1d" },
    { STP_PROTO_RAPID, "802.1w" },
    { STP_PROTO_MSTP, "802.1s" },
    { STP_PROTO_SPB, "802.1aq" },
    { 0, NULL}
};

#define STP_BPDU_TYPE_CONFIG      0x00
#define STP_BPDU_TYPE_RSTP        0x02
#define STP_BPDU_TYPE_TOPO_CHANGE 0x80

static const struct tok stp_bpdu_flag_values[] = {
    { 0x01, "Topology change" },
    { 0x02, "Proposal" },
    { 0x10, "Learn" },
    { 0x20, "Forward" },
    { 0x40, "Agreement" },
    { 0x80, "Topology change ACK" },
    { 0, NULL}
};

static const struct tok stp_bpdu_type_values[] = {
    { STP_BPDU_TYPE_CONFIG, "Config" },
    { STP_BPDU_TYPE_RSTP, "Rapid STP" },
    { STP_BPDU_TYPE_TOPO_CHANGE, "Topology Change" },
    { 0, NULL}
};

static const struct tok rstp_obj_port_role_values[] = {
    { 0x00, "Unknown" },
    { 0x01, "Alternate" },
    { 0x02, "Root" },
    { 0x03, "Designated" },
    { 0, NULL}
};

#define ND_TCHECK_BRIDGE_ID(p) ND_TCHECK2(*(p), 8)

static char *
stp_print_bridge_id(const u_char *p)
{
    static char bridge_id_str[sizeof("pppp.aa:bb:cc:dd:ee:ff")];

    snprintf(bridge_id_str, sizeof(bridge_id_str),
             "%.2x%.2x.%.2x:%.2x:%.2x:%.2x:%.2x:%.2x",
             p[0], p[1], p[2], p[3], p[4], p[5], p[6], p[7]);

    return bridge_id_str;
}

static int
stp_print_config_bpdu(netdissect_options *ndo, const struct stp_bpdu_ *stp_bpdu,
                      u_int length)
{
    ND_TCHECK(stp_bpdu->flags);
    ND_PRINT((ndo, ", Flags [%s]",
           bittok2str(stp_bpdu_flag_values, "none", stp_bpdu->flags)));

    ND_TCHECK(stp_bpdu->port_id);
    ND_PRINT((ndo, ", bridge-id %s.%04x, length %u",
           stp_print_bridge_id((const u_char *)&stp_bpdu->bridge_id),
           EXTRACT_16BITS(&stp_bpdu->port_id), length));

    /* in non-verbose mode just print the bridge-id */
    if (!ndo->ndo_vflag) {
        return 1;
    }

    ND_TCHECK(stp_bpdu->forward_delay);
    ND_PRINT((ndo, "\n\tmessage-age %.2fs, max-age %.2fs"
           ", hello-time %.2fs, forwarding-delay %.2fs",
           (float)EXTRACT_16BITS(&stp_bpdu->message_age) / STP_TIME_BASE,
           (float)EXTRACT_16BITS(&stp_bpdu->max_age) / STP_TIME_BASE,
           (float)EXTRACT_16BITS(&stp_bpdu->hello_time) / STP_TIME_BASE,
           (float)EXTRACT_16BITS(&stp_bpdu->forward_delay) / STP_TIME_BASE));

    ND_PRINT((ndo, "\n\troot-id %s, root-pathcost %u",
           stp_print_bridge_id((const u_char *)&stp_bpdu->root_id),
           EXTRACT_32BITS(&stp_bpdu->root_path_cost)));

    /* Port role is only valid for 802.1w */
    if (stp_bpdu->protocol_version == STP_PROTO_RAPID) {
        ND_PRINT((ndo, ", port-role %s",
               tok2str(rstp_obj_port_role_values, "Unknown",
                       RSTP_EXTRACT_PORT_ROLE(stp_bpdu->flags))));
    }
    return 1;

trunc:
    return 0;
}

/*
 * MSTP packet format
 * Ref. IEEE 802.1Q 2003 Ed. Section 14
 *
 * MSTP BPDU
 *
 * 2 -  bytes Protocol Id
 * 1 -  byte  Protocol Ver.
 * 1 -  byte  BPDU tye
 * 1 -  byte  Flags
 * 8 -  bytes CIST Root Identifier
 * 4 -  bytes CIST External Path Cost
 * 8 -  bytes CIST Regional Root Identifier
 * 2 -  bytes CIST Port Identifier
 * 2 -  bytes Message Age
 * 2 -  bytes Max age
 * 2 -  bytes Hello Time
 * 2 -  bytes Forward delay
 * 1 -  byte  Version 1 length. Must be 0
 * 2 -  bytes Version 3 length
 * 1 -  byte  Config Identifier
 * 32 - bytes Config Name
 * 2 -  bytes Revision level
 * 16 - bytes Config Digest [MD5]
 * 4 -  bytes CIST Internal Root Path Cost
 * 8 -  bytes CIST Bridge Identifier
 * 1 -  byte  CIST Remaining Hops
 * 16 - bytes MSTI information [Max 64 MSTI, each 16 bytes]
 *
 *
 * SPB BPDU
 * Ref. IEEE 802.1aq. Section 14
 *
 * 2 -  bytes Version 4 length
 * 1 -  byte  Aux Config Identifier
 * 32 - bytes Aux Config Name
 * 2 -  bytes Aux Revision level
 * 16 - bytes Aux Config Digest [MD5]
 * 1 -  byte  (1 - 2) Agreement Number
 *            (3 - 4) Discarded Agreement Number
 *            (5) Agreement Valid Flag
 *            (6) Restricted Role Flag
 *            (7 - 8) Unused sent zero
 * 1 -  byte Unused
 * 1 -  byte (1 - 4) Agreement Digest Format Identifier
 *           (5 - 8) Agreement Digest Format Capabilities
 * 1 -  byte (1 - 4) Agreement Digest Convention Identifier
 *           (5 - 8) Agreement Digest Convention Capabilities
 * 2 -  bytes Agreement Digest Edge Count
 * 8 -  byte Reserved Set
 * 20 - bytes Computed Topology Digest
 *
 *
 * MSTI Payload
 *
 * 1 - byte  MSTI flag
 * 8 - bytes MSTI Regional Root Identifier
 * 4 - bytes MSTI Regional Path Cost
 * 1 - byte  MSTI Bridge Priority
 * 1 - byte  MSTI Port Priority
 * 1 - byte  MSTI Remaining Hops
 *
 */

#define MST_BPDU_MSTI_LENGTH		    16
#define MST_BPDU_CONFIG_INFO_LENGTH	    64

/* Offsets of fields from the begginning for the packet */
#define MST_BPDU_VER3_LEN_OFFSET	    36
#define MST_BPDU_CONFIG_NAME_OFFSET	    39
#define MST_BPDU_CONFIG_DIGEST_OFFSET	    73
#define MST_BPDU_CIST_INT_PATH_COST_OFFSET  89
#define MST_BPDU_CIST_BRIDGE_ID_OFFSET	    93
#define MST_BPDU_CIST_REMAIN_HOPS_OFFSET    101
#define MST_BPDU_MSTI_OFFSET		    102
/* Offsets within  an MSTI */
#define MST_BPDU_MSTI_ROOT_PRIO_OFFSET	    1
#define MST_BPDU_MSTI_ROOT_PATH_COST_OFFSET 9
#define MST_BPDU_MSTI_BRIDGE_PRIO_OFFSET    13
#define MST_BPDU_MSTI_PORT_PRIO_OFFSET	    14
#define MST_BPDU_MSTI_REMAIN_HOPS_OFFSET    15

#define SPB_BPDU_MIN_LEN                  87
#define SPB_BPDU_CONFIG_NAME_OFFSET       3
#define SPB_BPDU_CONFIG_REV_OFFSET        SPB_BPDU_CONFIG_NAME_OFFSET + 32
#define SPB_BPDU_CONFIG_DIGEST_OFFSET     SPB_BPDU_CONFIG_REV_OFFSET + 2
#define SPB_BPDU_AGREEMENT_OFFSET         SPB_BPDU_CONFIG_DIGEST_OFFSET + 16
#define SPB_BPDU_AGREEMENT_UNUSED_OFFSET  SPB_BPDU_AGREEMENT_OFFSET + 1
#define SPB_BPDU_AGREEMENT_FORMAT_OFFSET  SPB_BPDU_AGREEMENT_UNUSED_OFFSET + 1
#define SPB_BPDU_AGREEMENT_CON_OFFSET     SPB_BPDU_AGREEMENT_FORMAT_OFFSET + 1
#define SPB_BPDU_AGREEMENT_EDGE_OFFSET    SPB_BPDU_AGREEMENT_CON_OFFSET + 1
#define SPB_BPDU_AGREEMENT_RES1_OFFSET    SPB_BPDU_AGREEMENT_EDGE_OFFSET + 2
#define SPB_BPDU_AGREEMENT_RES2_OFFSET    SPB_BPDU_AGREEMENT_RES1_OFFSET + 4
#define SPB_BPDU_AGREEMENT_DIGEST_OFFSET  SPB_BPDU_AGREEMENT_RES2_OFFSET + 4

static int
stp_print_mstp_bpdu(netdissect_options *ndo, const struct stp_bpdu_ *stp_bpdu,
                    u_int length)
{
    const u_char *ptr;
    uint16_t	    v3len;
    uint16_t	    len;
    uint16_t	    msti;
    u_int	    offset;

    ptr = (const u_char *)stp_bpdu;
    ND_PRINT((ndo, ", CIST Flags [%s], length %u",
           bittok2str(stp_bpdu_flag_values, "none", stp_bpdu->flags), length));

    /*
     * in non-verbose mode just print the flags.
     */
    if (!ndo->ndo_vflag) {
        return 1;
    }

    ND_TCHECK(stp_bpdu->flags);
    ND_PRINT((ndo, "\n\tport-role %s, ",
           tok2str(rstp_obj_port_role_values, "Unknown",
                   RSTP_EXTRACT_PORT_ROLE(stp_bpdu->flags))));

    ND_TCHECK(stp_bpdu->root_path_cost);
    ND_PRINT((ndo, "CIST root-id %s, CIST ext-pathcost %u",
           stp_print_bridge_id((const u_char *)&stp_bpdu->root_id),
           EXTRACT_32BITS(&stp_bpdu->root_path_cost)));

    ND_TCHECK(stp_bpdu->bridge_id);
    ND_PRINT((ndo, "\n\tCIST regional-root-id %s, ",
           stp_print_bridge_id((const u_char *)&stp_bpdu->bridge_id)));

    ND_TCHECK(stp_bpdu->port_id);
    ND_PRINT((ndo, "CIST port-id %04x,", EXTRACT_16BITS(&stp_bpdu->port_id)));

    ND_TCHECK(stp_bpdu->forward_delay);
    ND_PRINT((ndo, "\n\tmessage-age %.2fs, max-age %.2fs"
           ", hello-time %.2fs, forwarding-delay %.2fs",
           (float)EXTRACT_16BITS(&stp_bpdu->message_age) / STP_TIME_BASE,
           (float)EXTRACT_16BITS(&stp_bpdu->max_age) / STP_TIME_BASE,
           (float)EXTRACT_16BITS(&stp_bpdu->hello_time) / STP_TIME_BASE,
           (float)EXTRACT_16BITS(&stp_bpdu->forward_delay) / STP_TIME_BASE));

    ND_TCHECK_16BITS(ptr + MST_BPDU_VER3_LEN_OFFSET);
    ND_PRINT((ndo, "\n\tv3len %d, ", EXTRACT_16BITS(ptr + MST_BPDU_VER3_LEN_OFFSET)));
    ND_TCHECK_32BITS(ptr + MST_BPDU_CONFIG_DIGEST_OFFSET + 12);
    ND_PRINT((ndo, "MCID Name "));
    if (fn_printzp(ndo, ptr + MST_BPDU_CONFIG_NAME_OFFSET, 32, ndo->ndo_snapend))
	goto trunc;
    ND_PRINT((ndo, ", rev %u,"
            "\n\t\tdigest %08x%08x%08x%08x, ",
	          EXTRACT_16BITS(ptr + MST_BPDU_CONFIG_NAME_OFFSET + 32),
	          EXTRACT_32BITS(ptr + MST_BPDU_CONFIG_DIGEST_OFFSET),
	          EXTRACT_32BITS(ptr + MST_BPDU_CONFIG_DIGEST_OFFSET + 4),
	          EXTRACT_32BITS(ptr + MST_BPDU_CONFIG_DIGEST_OFFSET + 8),
	          EXTRACT_32BITS(ptr + MST_BPDU_CONFIG_DIGEST_OFFSET + 12)));

    ND_TCHECK_32BITS(ptr + MST_BPDU_CIST_INT_PATH_COST_OFFSET);
    ND_PRINT((ndo, "CIST int-root-pathcost %u,",
            EXTRACT_32BITS(ptr + MST_BPDU_CIST_INT_PATH_COST_OFFSET)));

    ND_TCHECK_BRIDGE_ID(ptr + MST_BPDU_CIST_BRIDGE_ID_OFFSET);
    ND_PRINT((ndo, "\n\tCIST bridge-id %s, ",
           stp_print_bridge_id(ptr + MST_BPDU_CIST_BRIDGE_ID_OFFSET)));

    ND_TCHECK(ptr[MST_BPDU_CIST_REMAIN_HOPS_OFFSET]);
    ND_PRINT((ndo, "CIST remaining-hops %d", ptr[MST_BPDU_CIST_REMAIN_HOPS_OFFSET]));

    /* Dump all MSTI's */
    ND_TCHECK_16BITS(ptr + MST_BPDU_VER3_LEN_OFFSET);
    v3len = EXTRACT_16BITS(ptr + MST_BPDU_VER3_LEN_OFFSET);
    if (v3len > MST_BPDU_CONFIG_INFO_LENGTH) {
        len = v3len - MST_BPDU_CONFIG_INFO_LENGTH;
        offset = MST_BPDU_MSTI_OFFSET;
        while (len >= MST_BPDU_MSTI_LENGTH) {
            ND_TCHECK2(*(ptr + offset), MST_BPDU_MSTI_LENGTH);

            msti = EXTRACT_16BITS(ptr + offset +
                                  MST_BPDU_MSTI_ROOT_PRIO_OFFSET);
            msti = msti & 0x0FFF;

            ND_PRINT((ndo, "\n\tMSTI %d, Flags [%s], port-role %s",
                   msti, bittok2str(stp_bpdu_flag_values, "none", ptr[offset]),
                   tok2str(rstp_obj_port_role_values, "Unknown",
                           RSTP_EXTRACT_PORT_ROLE(ptr[offset]))));
            ND_PRINT((ndo, "\n\t\tMSTI regional-root-id %s, pathcost %u",
                   stp_print_bridge_id(ptr + offset +
                                       MST_BPDU_MSTI_ROOT_PRIO_OFFSET),
                   EXTRACT_32BITS(ptr + offset +
                                  MST_BPDU_MSTI_ROOT_PATH_COST_OFFSET)));
            ND_PRINT((ndo, "\n\t\tMSTI bridge-prio %d, port-prio %d, hops %d",
                   ptr[offset + MST_BPDU_MSTI_BRIDGE_PRIO_OFFSET] >> 4,
                   ptr[offset + MST_BPDU_MSTI_PORT_PRIO_OFFSET] >> 4,
                   ptr[offset + MST_BPDU_MSTI_REMAIN_HOPS_OFFSET]));

            len -= MST_BPDU_MSTI_LENGTH;
            offset += MST_BPDU_MSTI_LENGTH;
        }
    }
    return 1;

trunc:
    return 0;
}

static int
stp_print_spb_bpdu(netdissect_options *ndo, const struct stp_bpdu_ *stp_bpdu,
                   u_int offset)
{
    const u_char *ptr;

    /*
     * in non-verbose mode don't print anything.
     */
    if (!ndo->ndo_vflag) {
        return 1;
    }

    ptr = (const u_char *)stp_bpdu;
    ND_TCHECK_32BITS(ptr + offset + SPB_BPDU_AGREEMENT_DIGEST_OFFSET + 16);

    ND_PRINT((ndo, "\n\tv4len %d, ", EXTRACT_16BITS (ptr + offset)));
    ND_PRINT((ndo, "AUXMCID Name "));
    if (fn_printzp(ndo, ptr + offset + SPB_BPDU_CONFIG_NAME_OFFSET, 32,
		   ndo->ndo_snapend))
	goto trunc;
    ND_PRINT((ndo, ", Rev %u,\n\t\tdigest %08x%08x%08x%08x",
            EXTRACT_16BITS(ptr + offset + SPB_BPDU_CONFIG_REV_OFFSET),
            EXTRACT_32BITS(ptr + offset + SPB_BPDU_CONFIG_DIGEST_OFFSET),
            EXTRACT_32BITS(ptr + offset + SPB_BPDU_CONFIG_DIGEST_OFFSET + 4),
            EXTRACT_32BITS(ptr + offset + SPB_BPDU_CONFIG_DIGEST_OFFSET + 8),
            EXTRACT_32BITS(ptr + offset + SPB_BPDU_CONFIG_DIGEST_OFFSET + 12)));

    ND_PRINT((ndo, "\n\tAgreement num %d, Discarded Agreement num %d, Agreement valid-"
            "flag %d,\n\tRestricted role-flag: %d, Format id %d cap %d, "
            "Convention id %d cap %d,\n\tEdge count %d, "
            "Agreement digest %08x%08x%08x%08x%08x\n",
            ptr[offset + SPB_BPDU_AGREEMENT_OFFSET]>>6,
            ptr[offset + SPB_BPDU_AGREEMENT_OFFSET]>>4 & 0x3,
            ptr[offset + SPB_BPDU_AGREEMENT_OFFSET]>>3 & 0x1,
            ptr[offset + SPB_BPDU_AGREEMENT_OFFSET]>>2 & 0x1,
            ptr[offset + SPB_BPDU_AGREEMENT_FORMAT_OFFSET]>>4,
            ptr[offset + SPB_BPDU_AGREEMENT_FORMAT_OFFSET]&0x00ff,
            ptr[offset + SPB_BPDU_AGREEMENT_CON_OFFSET]>>4,
            ptr[offset + SPB_BPDU_AGREEMENT_CON_OFFSET]&0x00ff,
            EXTRACT_16BITS(ptr + offset + SPB_BPDU_AGREEMENT_EDGE_OFFSET),
            EXTRACT_32BITS(ptr + offset + SPB_BPDU_AGREEMENT_DIGEST_OFFSET),
            EXTRACT_32BITS(ptr + offset + SPB_BPDU_AGREEMENT_DIGEST_OFFSET+4),
            EXTRACT_32BITS(ptr + offset + SPB_BPDU_AGREEMENT_DIGEST_OFFSET+8),
            EXTRACT_32BITS(ptr + offset + SPB_BPDU_AGREEMENT_DIGEST_OFFSET+12),
            EXTRACT_32BITS(ptr + offset + SPB_BPDU_AGREEMENT_DIGEST_OFFSET+16)));
    return 1;

trunc:
    return 0;
}

/*
 * Print 802.1d / 802.1w / 802.1q (mstp) / 802.1aq (spb) packets.
 */
void
stp_print(netdissect_options *ndo, const u_char *p, u_int length)
{
    const struct stp_bpdu_ *stp_bpdu;
    u_int                  mstp_len;
    u_int                  spb_len;

    stp_bpdu = (const struct stp_bpdu_*)p;

    /* Minimum STP Frame size. */
    if (length < 4)
        goto trunc;

    ND_TCHECK(stp_bpdu->protocol_id);
    if (EXTRACT_16BITS(&stp_bpdu->protocol_id)) {
        ND_PRINT((ndo, "unknown STP version, length %u", length));
        return;
    }

    ND_TCHECK(stp_bpdu->protocol_version);
    ND_PRINT((ndo, "STP %s", tok2str(stp_proto_values, "Unknown STP protocol (0x%02x)",
                         stp_bpdu->protocol_version)));

    switch (stp_bpdu->protocol_version) {
    case STP_PROTO_REGULAR:
    case STP_PROTO_RAPID:
    case STP_PROTO_MSTP:
    case STP_PROTO_SPB:
        break;
    default:
        return;
    }

    ND_TCHECK(stp_bpdu->bpdu_type);
    ND_PRINT((ndo, ", %s", tok2str(stp_bpdu_type_values, "Unknown BPDU Type (0x%02x)",
                           stp_bpdu->bpdu_type)));

    switch (stp_bpdu->bpdu_type) {
    case STP_BPDU_TYPE_CONFIG:
        if (length < sizeof(struct stp_bpdu_) - 1) {
            goto trunc;
        }
        if (!stp_print_config_bpdu(ndo, stp_bpdu, length))
            goto trunc;
        break;

    case STP_BPDU_TYPE_RSTP:
        if (stp_bpdu->protocol_version == STP_PROTO_RAPID) {
            if (length < sizeof(struct stp_bpdu_)) {
                goto trunc;
            }
            if (!stp_print_config_bpdu(ndo, stp_bpdu, length))
                goto trunc;
        } else if (stp_bpdu->protocol_version == STP_PROTO_MSTP ||
                   stp_bpdu->protocol_version == STP_PROTO_SPB) {
            if (length < STP_BPDU_MSTP_MIN_LEN) {
                goto trunc;
            }

            ND_TCHECK(stp_bpdu->v1_length);
            if (stp_bpdu->v1_length != 0) {
                /* FIX ME: Emit a message here ? */
                goto trunc;
            }

            /* Validate v3 length */
            ND_TCHECK_16BITS(p + MST_BPDU_VER3_LEN_OFFSET);
            mstp_len = EXTRACT_16BITS(p + MST_BPDU_VER3_LEN_OFFSET);
            mstp_len += 2;  /* length encoding itself is 2 bytes */
            if (length < (sizeof(struct stp_bpdu_) + mstp_len)) {
                goto trunc;
            }
            if (!stp_print_mstp_bpdu(ndo, stp_bpdu, length))
                goto trunc;

            if (stp_bpdu->protocol_version == STP_PROTO_SPB)
            {
              /* Validate v4 length */
              ND_TCHECK_16BITS(p + MST_BPDU_VER3_LEN_OFFSET + mstp_len);
              spb_len = EXTRACT_16BITS (p + MST_BPDU_VER3_LEN_OFFSET + mstp_len);
              spb_len += 2;
              if (length < (sizeof(struct stp_bpdu_) + mstp_len + spb_len) ||
                  spb_len < SPB_BPDU_MIN_LEN) {
                goto trunc;
              }
              if (!stp_print_spb_bpdu(ndo, stp_bpdu, (sizeof(struct stp_bpdu_) + mstp_len)))
                goto trunc;
            }
        }
        break;

    case STP_BPDU_TYPE_TOPO_CHANGE:
        /* always empty message - just break out */
        break;

    default:
        break;
    }

    return;
trunc:
    ND_PRINT((ndo, "[|stp %d]", length));
}

/*
 * Local Variables:
 * c-style: whitesmith
 * c-basic-offset: 4
 * End:
 */
