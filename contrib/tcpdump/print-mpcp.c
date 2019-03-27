/*
 * Copyright (c) 1998-2006 The TCPDUMP project
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
 *
 * Original code by Hannes Gredler (hannes@gredler.at)
 */

/* \summary: IEEE 802.3ah Multi-Point Control Protocol (MPCP) printer */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <netdissect-stdinc.h>

#include "netdissect.h"
#include "extract.h"

#define MPCP_TIMESTAMP_LEN        4
#define MPCP_TIMESTAMP_DURATION_LEN 2

struct mpcp_common_header_t {
    uint8_t opcode[2];
    uint8_t timestamp[MPCP_TIMESTAMP_LEN];
};

#define	MPCP_OPCODE_PAUSE   0x0001
#define	MPCP_OPCODE_GATE    0x0002
#define	MPCP_OPCODE_REPORT  0x0003
#define	MPCP_OPCODE_REG_REQ 0x0004
#define	MPCP_OPCODE_REG     0x0005
#define	MPCP_OPCODE_REG_ACK 0x0006

static const struct tok mpcp_opcode_values[] = {
    { MPCP_OPCODE_PAUSE, "Pause" },
    { MPCP_OPCODE_GATE, "Gate" },
    { MPCP_OPCODE_REPORT, "Report" },
    { MPCP_OPCODE_REG_REQ, "Register Request" },
    { MPCP_OPCODE_REG, "Register" },
    { MPCP_OPCODE_REG_ACK, "Register ACK" },
    { 0, NULL}
};

#define MPCP_GRANT_NUMBER_LEN 1
#define	MPCP_GRANT_NUMBER_MASK 0x7
static const struct tok mpcp_grant_flag_values[] = {
    { 0x08, "Discovery" },
    { 0x10, "Force Grant #1" },
    { 0x20, "Force Grant #2" },
    { 0x40, "Force Grant #3" },
    { 0x80, "Force Grant #4" },
    { 0, NULL}
};

struct mpcp_grant_t {
    uint8_t starttime[MPCP_TIMESTAMP_LEN];
    uint8_t duration[MPCP_TIMESTAMP_DURATION_LEN];
};

struct mpcp_reg_req_t {
    uint8_t flags;
    uint8_t pending_grants;
};


static const struct tok mpcp_reg_req_flag_values[] = {
    { 1, "Register" },
    { 3, "De-Register" },
    { 0, NULL}
};

struct mpcp_reg_t {
    uint8_t assigned_port[2];
    uint8_t flags;
    uint8_t sync_time[MPCP_TIMESTAMP_DURATION_LEN];
    uint8_t echoed_pending_grants;
};

static const struct tok mpcp_reg_flag_values[] = {
    { 1, "Re-Register" },
    { 2, "De-Register" },
    { 3, "ACK" },
    { 4, "NACK" },
    { 0, NULL}
};

#define MPCP_REPORT_QUEUESETS_LEN    1
#define MPCP_REPORT_REPORTBITMAP_LEN 1
static const struct tok mpcp_report_bitmap_values[] = {
    { 0x01, "Q0" },
    { 0x02, "Q1" },
    { 0x04, "Q2" },
    { 0x08, "Q3" },
    { 0x10, "Q4" },
    { 0x20, "Q5" },
    { 0x40, "Q6" },
    { 0x80, "Q7" },
    { 0, NULL}
};

struct mpcp_reg_ack_t {
    uint8_t flags;
    uint8_t echoed_assigned_port[2];
    uint8_t echoed_sync_time[MPCP_TIMESTAMP_DURATION_LEN];
};

static const struct tok mpcp_reg_ack_flag_values[] = {
    { 0, "NACK" },
    { 1, "ACK" },
    { 0, NULL}
};

void
mpcp_print(netdissect_options *ndo, register const u_char *pptr, register u_int length)
{
    union {
        const struct mpcp_common_header_t *common_header;
        const struct mpcp_grant_t *grant;
        const struct mpcp_reg_req_t *reg_req;
        const struct mpcp_reg_t *reg;
        const struct mpcp_reg_ack_t *reg_ack;
    } mpcp;


    const u_char *tptr;
    uint16_t opcode;
    uint8_t grant_numbers, grant;
    uint8_t queue_sets, queue_set, report_bitmap, report;

    tptr=pptr;
    mpcp.common_header = (const struct mpcp_common_header_t *)pptr;

    ND_TCHECK2(*tptr, sizeof(const struct mpcp_common_header_t));
    opcode = EXTRACT_16BITS(mpcp.common_header->opcode);
    ND_PRINT((ndo, "MPCP, Opcode %s", tok2str(mpcp_opcode_values, "Unknown (%u)", opcode)));
    if (opcode != MPCP_OPCODE_PAUSE) {
        ND_PRINT((ndo, ", Timestamp %u ticks", EXTRACT_32BITS(mpcp.common_header->timestamp)));
    }
    ND_PRINT((ndo, ", length %u", length));

    if (!ndo->ndo_vflag)
        return;

    tptr += sizeof(const struct mpcp_common_header_t);

    switch (opcode) {
    case MPCP_OPCODE_PAUSE:
        break;

    case MPCP_OPCODE_GATE:
        ND_TCHECK2(*tptr, MPCP_GRANT_NUMBER_LEN);
        grant_numbers = *tptr & MPCP_GRANT_NUMBER_MASK;
        ND_PRINT((ndo, "\n\tGrant Numbers %u, Flags [ %s ]",
               grant_numbers,
               bittok2str(mpcp_grant_flag_values,
                          "?",
                          *tptr &~ MPCP_GRANT_NUMBER_MASK)));
        tptr++;

        for (grant = 1; grant <= grant_numbers; grant++) {
            ND_TCHECK2(*tptr, sizeof(const struct mpcp_grant_t));
            mpcp.grant = (const struct mpcp_grant_t *)tptr;
            ND_PRINT((ndo, "\n\tGrant #%u, Start-Time %u ticks, duration %u ticks",
                   grant,
                   EXTRACT_32BITS(mpcp.grant->starttime),
                   EXTRACT_16BITS(mpcp.grant->duration)));
            tptr += sizeof(const struct mpcp_grant_t);
        }

        ND_TCHECK2(*tptr, MPCP_TIMESTAMP_DURATION_LEN);
        ND_PRINT((ndo, "\n\tSync-Time %u ticks", EXTRACT_16BITS(tptr)));
        break;


    case MPCP_OPCODE_REPORT:
        ND_TCHECK2(*tptr, MPCP_REPORT_QUEUESETS_LEN);
        queue_sets = *tptr;
        tptr+=MPCP_REPORT_QUEUESETS_LEN;
        ND_PRINT((ndo, "\n\tTotal Queue-Sets %u", queue_sets));

        for (queue_set = 1; queue_set < queue_sets; queue_set++) {
            ND_TCHECK2(*tptr, MPCP_REPORT_REPORTBITMAP_LEN);
            report_bitmap = *(tptr);
            ND_PRINT((ndo, "\n\t  Queue-Set #%u, Report-Bitmap [ %s ]",
                   queue_sets,
                   bittok2str(mpcp_report_bitmap_values, "Unknown", report_bitmap)));
            tptr++;

            report=1;
            while (report_bitmap != 0) {
                if (report_bitmap & 1) {
                    ND_TCHECK2(*tptr, MPCP_TIMESTAMP_DURATION_LEN);
                    ND_PRINT((ndo, "\n\t    Q%u Report, Duration %u ticks",
                           report,
                           EXTRACT_16BITS(tptr)));
                    tptr+=MPCP_TIMESTAMP_DURATION_LEN;
                }
                report++;
                report_bitmap = report_bitmap >> 1;
            }
        }
        break;

    case MPCP_OPCODE_REG_REQ:
        ND_TCHECK2(*tptr, sizeof(const struct mpcp_reg_req_t));
        mpcp.reg_req = (const struct mpcp_reg_req_t *)tptr;
        ND_PRINT((ndo, "\n\tFlags [ %s ], Pending-Grants %u",
               bittok2str(mpcp_reg_req_flag_values, "Reserved", mpcp.reg_req->flags),
               mpcp.reg_req->pending_grants));
        break;

    case MPCP_OPCODE_REG:
        ND_TCHECK2(*tptr, sizeof(const struct mpcp_reg_t));
        mpcp.reg = (const struct mpcp_reg_t *)tptr;
        ND_PRINT((ndo, "\n\tAssigned-Port %u, Flags [ %s ]" \
               "\n\tSync-Time %u ticks, Echoed-Pending-Grants %u",
               EXTRACT_16BITS(mpcp.reg->assigned_port),
               bittok2str(mpcp_reg_flag_values, "Reserved", mpcp.reg->flags),
               EXTRACT_16BITS(mpcp.reg->sync_time),
               mpcp.reg->echoed_pending_grants));
        break;

    case MPCP_OPCODE_REG_ACK:
        ND_TCHECK2(*tptr, sizeof(const struct mpcp_reg_ack_t));
        mpcp.reg_ack = (const struct mpcp_reg_ack_t *)tptr;
        ND_PRINT((ndo, "\n\tEchoed-Assigned-Port %u, Flags [ %s ]" \
               "\n\tEchoed-Sync-Time %u ticks",
               EXTRACT_16BITS(mpcp.reg_ack->echoed_assigned_port),
               bittok2str(mpcp_reg_ack_flag_values, "Reserved", mpcp.reg_ack->flags),
               EXTRACT_16BITS(mpcp.reg_ack->echoed_sync_time)));
        break;

    default:
        /* unknown opcode - hexdump for now */
        print_unknown_data(ndo,pptr, "\n\t", length);
        break;
    }

    return;

trunc:
    ND_PRINT((ndo, "\n\t[|MPCP]"));
}
/*
 * Local Variables:
 * c-style: whitesmith
 * c-basic-offset: 8
 * End:
 */
