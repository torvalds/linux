/*
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
 *
 * Original code by Hannes Gredler (hannes@gredler.at)
 */

/* \summary: Bidirectional Forwarding Detection (BFD) printer */

/* specification: RFC 5880 (for version 1) and RFC 5881 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <netdissect-stdinc.h>

#include "netdissect.h"
#include "extract.h"

#include "udp.h"

/*
 * Control packet, BFDv0, draft-katz-ward-bfd-01.txt
 *
 *     0                   1                   2                   3
 *     0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1
 *    +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 *    |Vers |  Diag   |H|D|P|F| Rsvd  |  Detect Mult  |    Length     |
 *    +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 *    |                       My Discriminator                        |
 *    +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 *    |                      Your Discriminator                       |
 *    +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 *    |                    Desired Min TX Interval                    |
 *    +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 *    |                   Required Min RX Interval                    |
 *    +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 *    |                 Required Min Echo RX Interval                 |
 *    +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 */

/*
 *  Control packet, BFDv1, RFC 5880
 *
 *     0                   1                   2                   3
 *     0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1
 *    +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 *    |Vers |  Diag   |Sta|P|F|C|A|D|M|  Detect Mult  |    Length     |
 *    +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 *    |                       My Discriminator                        |
 *    +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 *    |                      Your Discriminator                       |
 *    +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 *    |                    Desired Min TX Interval                    |
 *    +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 *    |                   Required Min RX Interval                    |
 *    +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 *    |                 Required Min Echo RX Interval                 |
 *    +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 */

struct bfd_header_t {
    uint8_t version_diag;
    uint8_t flags;
    uint8_t detect_time_multiplier;
    uint8_t length;
    uint8_t my_discriminator[4];
    uint8_t your_discriminator[4];
    uint8_t desired_min_tx_interval[4];
    uint8_t required_min_rx_interval[4];
    uint8_t required_min_echo_interval[4];
};

/*
 *    An optional Authentication Header may be present
 *
 *     0                   1                   2                   3
 *     0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1
 *    +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 *    |   Auth Type   |   Auth Len    |    Authentication Data...     |
 *    +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 */

struct bfd_auth_header_t {
    uint8_t auth_type;
    uint8_t auth_len;
    uint8_t auth_data;
    uint8_t dummy; /* minimun 4 bytes */
};

enum auth_type {
    AUTH_PASSWORD = 1,
    AUTH_MD5      = 2,
    AUTH_MET_MD5  = 3,
    AUTH_SHA1     = 4,
    AUTH_MET_SHA1 = 5
};

static const struct tok bfd_v1_authentication_values[] = {
    { AUTH_PASSWORD, "Simple Password" },
    { AUTH_MD5,      "Keyed MD5" },
    { AUTH_MET_MD5,  "Meticulous Keyed MD5" },
    { AUTH_SHA1,     "Keyed SHA1" },
    { AUTH_MET_SHA1, "Meticulous Keyed SHA1" },
    { 0, NULL }
};

enum auth_length {
    AUTH_PASSWORD_FIELD_MIN_LEN = 4,  /* header + password min: 3 + 1 */
    AUTH_PASSWORD_FIELD_MAX_LEN = 19, /* header + password max: 3 + 16 */
    AUTH_MD5_FIELD_LEN  = 24,
    AUTH_MD5_HASH_LEN   = 16,
    AUTH_SHA1_FIELD_LEN = 28,
    AUTH_SHA1_HASH_LEN  = 20
};

#define BFD_EXTRACT_VERSION(x) (((x)&0xe0)>>5)
#define BFD_EXTRACT_DIAG(x)     ((x)&0x1f)

static const struct tok bfd_port_values[] = {
    { BFD_CONTROL_PORT, "Control" },
    { BFD_ECHO_PORT,    "Echo" },
    { 0, NULL }
};

static const struct tok bfd_diag_values[] = {
    { 0, "No Diagnostic" },
    { 1, "Control Detection Time Expired" },
    { 2, "Echo Function Failed" },
    { 3, "Neighbor Signaled Session Down" },
    { 4, "Forwarding Plane Reset" },
    { 5, "Path Down" },
    { 6, "Concatenated Path Down" },
    { 7, "Administratively Down" },
    { 8, "Reverse Concatenated Path Down" },
    { 0, NULL }
};

static const struct tok bfd_v0_flag_values[] = {
    { 0x80, "I Hear You" },
    { 0x40, "Demand" },
    { 0x20, "Poll" },
    { 0x10, "Final" },
    { 0x08, "Reserved" },
    { 0x04, "Reserved" },
    { 0x02, "Reserved" },
    { 0x01, "Reserved" },
    { 0, NULL }
};

#define BFD_FLAG_AUTH 0x04

static const struct tok bfd_v1_flag_values[] = {
    { 0x20, "Poll" },
    { 0x10, "Final" },
    { 0x08, "Control Plane Independent" },
    { BFD_FLAG_AUTH, "Authentication Present" },
    { 0x02, "Demand" },
    { 0x01, "Multipoint" },
    { 0, NULL }
};

static const struct tok bfd_v1_state_values[] = {
    { 0, "AdminDown" },
    { 1, "Down" },
    { 2, "Init" },
    { 3, "Up" },
    { 0, NULL }
};

static int
auth_print(netdissect_options *ndo, register const u_char *pptr)
{
        const struct bfd_auth_header_t *bfd_auth_header;
        int i;

        pptr += sizeof (const struct bfd_header_t);
        bfd_auth_header = (const struct bfd_auth_header_t *)pptr;
        ND_TCHECK(*bfd_auth_header);
        ND_PRINT((ndo, "\n\tAuthentication: %s (%u), length: %u",
                 tok2str(bfd_v1_authentication_values,"Unknown",bfd_auth_header->auth_type),
                 bfd_auth_header->auth_type,
                 bfd_auth_header->auth_len));
                pptr += 2;
                ND_PRINT((ndo, "\n\t  Auth Key ID: %d", *pptr));

        switch(bfd_auth_header->auth_type) {
            case AUTH_PASSWORD:
/*
 *    Simple Password Authentication Section Format
 *
 *     0                   1                   2                   3
 *     0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1
 *    +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 *    |   Auth Type   |   Auth Len    |  Auth Key ID  |  Password...  |
 *    +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 *    |                              ...                              |
 *    +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 */
                if (bfd_auth_header->auth_len < AUTH_PASSWORD_FIELD_MIN_LEN ||
                    bfd_auth_header->auth_len > AUTH_PASSWORD_FIELD_MAX_LEN) {
                    ND_PRINT((ndo, "[invalid length %d]",
                             bfd_auth_header->auth_len));
                    break;
                }
                pptr++;
                ND_PRINT((ndo, ", Password: "));
                /* the length is equal to the password length plus three */
                if (fn_printn(ndo, pptr, bfd_auth_header->auth_len - 3,
                              ndo->ndo_snapend))
                    goto trunc;
                break;
            case AUTH_MD5:
            case AUTH_MET_MD5:
/*
 *    Keyed MD5 and Meticulous Keyed MD5 Authentication Section Format
 *
 *     0                   1                   2                   3
 *     0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1
 *    +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 *    |   Auth Type   |   Auth Len    |  Auth Key ID  |   Reserved    |
 *    +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 *    |                        Sequence Number                        |
 *    +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 *    |                      Auth Key/Digest...                       |
 *    +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 *    |                              ...                              |
 *    +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 */
                if (bfd_auth_header->auth_len != AUTH_MD5_FIELD_LEN) {
                    ND_PRINT((ndo, "[invalid length %d]",
                             bfd_auth_header->auth_len));
                    break;
                }
                pptr += 2;
                ND_TCHECK2(*pptr, 4);
                ND_PRINT((ndo, ", Sequence Number: 0x%08x", EXTRACT_32BITS(pptr)));
                pptr += 4;
                ND_TCHECK2(*pptr, AUTH_MD5_HASH_LEN);
                ND_PRINT((ndo, "\n\t  Digest: "));
                for(i = 0; i < AUTH_MD5_HASH_LEN; i++)
                    ND_PRINT((ndo, "%02x", pptr[i]));
                break;
            case AUTH_SHA1:
            case AUTH_MET_SHA1:
/*
 *    Keyed SHA1 and Meticulous Keyed SHA1 Authentication Section Format
 *
 *     0                   1                   2                   3
 *     0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1
 *    +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 *    |   Auth Type   |   Auth Len    |  Auth Key ID  |   Reserved    |
 *    +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 *    |                        Sequence Number                        |
 *    +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 *    |                       Auth Key/Hash...                        |
 *    +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 *    |                              ...                              |
 *    +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 */
                if (bfd_auth_header->auth_len != AUTH_SHA1_FIELD_LEN) {
                    ND_PRINT((ndo, "[invalid length %d]",
                             bfd_auth_header->auth_len));
                    break;
                }
                pptr += 2;
                ND_TCHECK2(*pptr, 4);
                ND_PRINT((ndo, ", Sequence Number: 0x%08x", EXTRACT_32BITS(pptr)));
                pptr += 4;
                ND_TCHECK2(*pptr, AUTH_SHA1_HASH_LEN);
                ND_PRINT((ndo, "\n\t  Hash: "));
                for(i = 0; i < AUTH_SHA1_HASH_LEN; i++)
                    ND_PRINT((ndo, "%02x", pptr[i]));
                break;
        }
        return 0;

trunc:
        return 1;
}

void
bfd_print(netdissect_options *ndo, register const u_char *pptr,
          register u_int len, register u_int port)
{
        const struct bfd_header_t *bfd_header;
        uint8_t version = 0;

        bfd_header = (const struct bfd_header_t *)pptr;
        if (port == BFD_CONTROL_PORT) {
            ND_TCHECK(*bfd_header);
            version = BFD_EXTRACT_VERSION(bfd_header->version_diag);
        } else if (port == BFD_ECHO_PORT) {
            /* Echo is BFD v1 only */
            version = 1;
        }
        switch ((port << 8) | version) {

            /* BFDv0 */
        case (BFD_CONTROL_PORT << 8):
            if (ndo->ndo_vflag < 1)
            {
                ND_PRINT((ndo, "BFDv%u, %s, Flags: [%s], length: %u",
                       version,
                       tok2str(bfd_port_values, "unknown (%u)", port),
                       bittok2str(bfd_v0_flag_values, "none", bfd_header->flags),
                       len));
                return;
            }

            ND_PRINT((ndo, "BFDv%u, length: %u\n\t%s, Flags: [%s], Diagnostic: %s (0x%02x)",
                   version,
                   len,
                   tok2str(bfd_port_values, "unknown (%u)", port),
                   bittok2str(bfd_v0_flag_values, "none", bfd_header->flags),
                   tok2str(bfd_diag_values,"unknown",BFD_EXTRACT_DIAG(bfd_header->version_diag)),
                   BFD_EXTRACT_DIAG(bfd_header->version_diag)));

            ND_PRINT((ndo, "\n\tDetection Timer Multiplier: %u (%u ms Detection time), BFD Length: %u",
                   bfd_header->detect_time_multiplier,
                   bfd_header->detect_time_multiplier * EXTRACT_32BITS(bfd_header->desired_min_tx_interval)/1000,
                   bfd_header->length));


            ND_PRINT((ndo, "\n\tMy Discriminator: 0x%08x", EXTRACT_32BITS(bfd_header->my_discriminator)));
            ND_PRINT((ndo, ", Your Discriminator: 0x%08x", EXTRACT_32BITS(bfd_header->your_discriminator)));
            ND_PRINT((ndo, "\n\t  Desired min Tx Interval:    %4u ms", EXTRACT_32BITS(bfd_header->desired_min_tx_interval)/1000));
            ND_PRINT((ndo, "\n\t  Required min Rx Interval:   %4u ms", EXTRACT_32BITS(bfd_header->required_min_rx_interval)/1000));
            ND_PRINT((ndo, "\n\t  Required min Echo Interval: %4u ms", EXTRACT_32BITS(bfd_header->required_min_echo_interval)/1000));
            break;

            /* BFDv1 */
        case (BFD_CONTROL_PORT << 8 | 1):
            if (ndo->ndo_vflag < 1)
            {
                ND_PRINT((ndo, "BFDv%u, %s, State %s, Flags: [%s], length: %u",
                       version,
                       tok2str(bfd_port_values, "unknown (%u)", port),
                       tok2str(bfd_v1_state_values, "unknown (%u)", (bfd_header->flags & 0xc0) >> 6),
                       bittok2str(bfd_v1_flag_values, "none", bfd_header->flags & 0x3f),
                       len));
                return;
            }

            ND_PRINT((ndo, "BFDv%u, length: %u\n\t%s, State %s, Flags: [%s], Diagnostic: %s (0x%02x)",
                   version,
                   len,
                   tok2str(bfd_port_values, "unknown (%u)", port),
                   tok2str(bfd_v1_state_values, "unknown (%u)", (bfd_header->flags & 0xc0) >> 6),
                   bittok2str(bfd_v1_flag_values, "none", bfd_header->flags & 0x3f),
                   tok2str(bfd_diag_values,"unknown",BFD_EXTRACT_DIAG(bfd_header->version_diag)),
                   BFD_EXTRACT_DIAG(bfd_header->version_diag)));

            ND_PRINT((ndo, "\n\tDetection Timer Multiplier: %u (%u ms Detection time), BFD Length: %u",
                   bfd_header->detect_time_multiplier,
                   bfd_header->detect_time_multiplier * EXTRACT_32BITS(bfd_header->desired_min_tx_interval)/1000,
                   bfd_header->length));


            ND_PRINT((ndo, "\n\tMy Discriminator: 0x%08x", EXTRACT_32BITS(bfd_header->my_discriminator)));
            ND_PRINT((ndo, ", Your Discriminator: 0x%08x", EXTRACT_32BITS(bfd_header->your_discriminator)));
            ND_PRINT((ndo, "\n\t  Desired min Tx Interval:    %4u ms", EXTRACT_32BITS(bfd_header->desired_min_tx_interval)/1000));
            ND_PRINT((ndo, "\n\t  Required min Rx Interval:   %4u ms", EXTRACT_32BITS(bfd_header->required_min_rx_interval)/1000));
            ND_PRINT((ndo, "\n\t  Required min Echo Interval: %4u ms", EXTRACT_32BITS(bfd_header->required_min_echo_interval)/1000));

            if (bfd_header->flags & BFD_FLAG_AUTH) {
                if (auth_print(ndo, pptr))
                    goto trunc;
            }
            break;

            /* BFDv0 */
        case (BFD_ECHO_PORT << 8): /* not yet supported - fall through */
            /* BFDv1 */
        case (BFD_ECHO_PORT << 8 | 1):

        default:
            ND_PRINT((ndo, "BFD, %s, length: %u",
                   tok2str(bfd_port_values, "unknown (%u)", port),
                   len));
            if (ndo->ndo_vflag >= 1) {
                    if(!print_unknown_data(ndo, pptr,"\n\t",len))
                            return;
            }
            break;
        }
        return;

trunc:
        ND_PRINT((ndo, "[|BFD]"));
}
/*
 * Local Variables:
 * c-style: whitesmith
 * c-basic-offset: 8
 * End:
 */
