/* Copyright (c) 2013, The TCPDUMP project
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright notice, this
 *    list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright notice,
 *    this list of conditions and the following disclaimer in the documentation
 *    and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE FOR
 * ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
 * ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

/* \summary: Message Transfer Part 3 (MTP3) User Adaptation Layer (M3UA) printer */

/* RFC 4666 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <netdissect-stdinc.h>

#include "netdissect.h"
#include "extract.h"

static const char tstr[] = " [|m3ua]";

#define M3UA_REL_1_0 1

struct m3ua_common_header {
  uint8_t  v;
  uint8_t  reserved;
  uint8_t  msg_class;
  uint8_t  msg_type;
  uint32_t len;
};

struct m3ua_param_header {
  uint16_t tag;
  uint16_t len;
};

/* message classes */
#define M3UA_MSGC_MGMT 0
#define M3UA_MSGC_TRANSFER 1
#define M3UA_MSGC_SSNM 2
#define M3UA_MSGC_ASPSM 3
#define M3UA_MSGC_ASPTM 4
/* reserved values */
#define M3UA_MSGC_RKM 9

static const struct tok MessageClasses[] = {
	{ M3UA_MSGC_MGMT,     "Management"            },
	{ M3UA_MSGC_TRANSFER, "Transfer"              },
	{ M3UA_MSGC_SSNM,     "SS7"                   },
	{ M3UA_MSGC_ASPSM,    "ASP"                   },
	{ M3UA_MSGC_ASPTM,    "ASP"                   },
	{ M3UA_MSGC_RKM,      "Routing Key Management"},
	{ 0, NULL }
};

/* management messages */
#define M3UA_MGMT_ERROR 0
#define M3UA_MGMT_NOTIFY 1

static const struct tok MgmtMessages[] = {
  { M3UA_MGMT_ERROR, "Error" },
  { M3UA_MGMT_NOTIFY, "Notify" },
  { 0, NULL }
};

/* transfer messages */
#define M3UA_TRANSFER_DATA 1

static const struct tok TransferMessages[] = {
  { M3UA_TRANSFER_DATA, "Data" },
  { 0, NULL }
};

/* SS7 Signaling Network Management messages */
#define M3UA_SSNM_DUNA 1
#define M3UA_SSNM_DAVA 2
#define M3UA_SSNM_DAUD 3
#define M3UA_SSNM_SCON 4
#define M3UA_SSNM_DUPU 5
#define M3UA_SSNM_DRST 6

static const struct tok SS7Messages[] = {
  { M3UA_SSNM_DUNA, "Destination Unavailable" },
  { M3UA_SSNM_DAVA, "Destination Available" },
  { M3UA_SSNM_DAUD, "Destination State Audit" },
  { M3UA_SSNM_SCON, "Signalling Congestion" },
  { M3UA_SSNM_DUPU, "Destination User Part Unavailable" },
  { M3UA_SSNM_DRST, "Destination Restricted" },
  { 0, NULL }
};

/* ASP State Maintenance messages */
#define M3UA_ASP_UP 1
#define M3UA_ASP_DN 2
#define M3UA_ASP_BEAT 3
#define M3UA_ASP_UP_ACK 4
#define M3UA_ASP_DN_ACK 5
#define M3UA_ASP_BEAT_ACK 6

static const struct tok ASPStateMessages[] = {
  { M3UA_ASP_UP, "Up" },
  { M3UA_ASP_DN, "Down" },
  { M3UA_ASP_BEAT, "Heartbeat" },
  { M3UA_ASP_UP_ACK, "Up Acknowledgement" },
  { M3UA_ASP_DN_ACK, "Down Acknowledgement" },
  { M3UA_ASP_BEAT_ACK, "Heartbeat Acknowledgement" },
  { 0, NULL }
};

/* ASP Traffic Maintenance messages */
#define M3UA_ASP_AC 1
#define M3UA_ASP_IA 2
#define M3UA_ASP_AC_ACK 3
#define M3UA_ASP_IA_ACK 4

static const struct tok ASPTrafficMessages[] = {
  { M3UA_ASP_AC, "Active" },
  { M3UA_ASP_IA, "Inactive" },
  { M3UA_ASP_AC_ACK, "Active Acknowledgement" },
  { M3UA_ASP_IA_ACK, "Inactive Acknowledgement" },
  { 0, NULL }
};

/* Routing Key Management messages */
#define M3UA_RKM_REQ 1
#define M3UA_RKM_RSP 2
#define M3UA_RKM_DEREQ 3
#define M3UA_RKM_DERSP 4

static const struct tok RoutingKeyMgmtMessages[] = {
  { M3UA_RKM_REQ, "Registration Request" },
  { M3UA_RKM_RSP, "Registration Response" },
  { M3UA_RKM_DEREQ, "Deregistration Request" },
  { M3UA_RKM_DERSP, "Deregistration Response" },
  { 0, NULL }
};

/* M3UA Parameters */
#define M3UA_PARAM_INFO 0x0004
#define M3UA_PARAM_ROUTING_CTX 0x0006
#define M3UA_PARAM_DIAGNOSTIC 0x0007
#define M3UA_PARAM_HB_DATA 0x0009
#define M3UA_PARAM_TRAFFIC_MODE_TYPE 0x000b
#define M3UA_PARAM_ERROR_CODE 0x000c
#define M3UA_PARAM_STATUS 0x000d
#define M3UA_PARAM_ASP_ID 0x0011
#define M3UA_PARAM_AFFECTED_POINT_CODE 0x0012
#define M3UA_PARAM_CORR_ID 0x0013

#define M3UA_PARAM_NETWORK_APPEARANCE 0x0200
#define M3UA_PARAM_USER 0x0204
#define M3UA_PARAM_CONGESTION_INDICATION 0x0205
#define M3UA_PARAM_CONCERNED_DST 0x0206
#define M3UA_PARAM_ROUTING_KEY 0x0207
#define M3UA_PARAM_REG_RESULT 0x0208
#define M3UA_PARAM_DEREG_RESULT 0x0209
#define M3UA_PARAM_LOCAL_ROUTING_KEY_ID 0x020a
#define M3UA_PARAM_DST_POINT_CODE 0x020b
#define M3UA_PARAM_SI 0x020c
#define M3UA_PARAM_ORIGIN_POINT_CODE_LIST 0x020e
#define M3UA_PARAM_PROTO_DATA 0x0210
#define M3UA_PARAM_REG_STATUS 0x0212
#define M3UA_PARAM_DEREG_STATUS 0x0213

static const struct tok ParamName[] = {
  { M3UA_PARAM_INFO, "INFO String" },
  { M3UA_PARAM_ROUTING_CTX, "Routing Context" },
  { M3UA_PARAM_DIAGNOSTIC, "Diagnostic Info" },
  { M3UA_PARAM_HB_DATA, "Heartbeat Data" },
  { M3UA_PARAM_TRAFFIC_MODE_TYPE, "Traffic Mode Type" },
  { M3UA_PARAM_ERROR_CODE, "Error Code" },
  { M3UA_PARAM_STATUS, "Status" },
  { M3UA_PARAM_ASP_ID, "ASP Identifier" },
  { M3UA_PARAM_AFFECTED_POINT_CODE, "Affected Point Code" },
  { M3UA_PARAM_CORR_ID, "Correlation ID" },
  { M3UA_PARAM_NETWORK_APPEARANCE, "Network Appearance" },
  { M3UA_PARAM_USER, "User/Cause" },
  { M3UA_PARAM_CONGESTION_INDICATION, "Congestion Indications" },
  { M3UA_PARAM_CONCERNED_DST, "Concerned Destination" },
  { M3UA_PARAM_ROUTING_KEY, "Routing Key" },
  { M3UA_PARAM_REG_RESULT, "Registration Result" },
  { M3UA_PARAM_DEREG_RESULT, "Deregistration Result" },
  { M3UA_PARAM_LOCAL_ROUTING_KEY_ID, "Local Routing Key Identifier" },
  { M3UA_PARAM_DST_POINT_CODE, "Destination Point Code" },
  { M3UA_PARAM_SI, "Service Indicators" },
  { M3UA_PARAM_ORIGIN_POINT_CODE_LIST, "Originating Point Code List" },
  { M3UA_PARAM_PROTO_DATA, "Protocol Data" },
  { M3UA_PARAM_REG_STATUS, "Registration Status" },
  { M3UA_PARAM_DEREG_STATUS, "Deregistration Status" },
  { 0, NULL }
};

static void
tag_value_print(netdissect_options *ndo,
                const u_char *buf, const uint16_t tag, const uint16_t size)
{
  switch (tag) {
  case M3UA_PARAM_NETWORK_APPEARANCE:
  case M3UA_PARAM_ROUTING_CTX:
  case M3UA_PARAM_CORR_ID:
    /* buf and size don't include the header */
    if (size < 4)
      goto invalid;
    ND_TCHECK2(*buf, size);
    ND_PRINT((ndo, "0x%08x", EXTRACT_32BITS(buf)));
    break;
  /* ... */
  default:
    ND_PRINT((ndo, "(length %u)", size + (u_int)sizeof(struct m3ua_param_header)));
    ND_TCHECK2(*buf, size);
  }
  return;

invalid:
  ND_PRINT((ndo, "%s", istr));
  ND_TCHECK2(*buf, size);
  return;
trunc:
  ND_PRINT((ndo, "%s", tstr));
}

/*
 *     0                   1                   2                   3
 *     0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1
 *    +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 *    |          Parameter Tag        |       Parameter Length        |
 *    +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 *    \                                                               \
 *    /                       Parameter Value                         /
 *    \                                                               \
 *    +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 */
static void
m3ua_tags_print(netdissect_options *ndo,
                const u_char *buf, const u_int size)
{
  const u_char *p = buf;
  int align;
  uint16_t hdr_tag;
  uint16_t hdr_len;

  while (p < buf + size) {
    if (p + sizeof(struct m3ua_param_header) > buf + size)
      goto invalid;
    ND_TCHECK2(*p, sizeof(struct m3ua_param_header));
    /* Parameter Tag */
    hdr_tag = EXTRACT_16BITS(p);
    ND_PRINT((ndo, "\n\t\t\t%s: ", tok2str(ParamName, "Unknown Parameter (0x%04x)", hdr_tag)));
    /* Parameter Length */
    hdr_len = EXTRACT_16BITS(p + 2);
    if (hdr_len < sizeof(struct m3ua_param_header))
      goto invalid;
    /* Parameter Value */
    align = (p + hdr_len - buf) % 4;
    align = align ? 4 - align : 0;
    ND_TCHECK2(*p, hdr_len + align);
    tag_value_print(ndo, p, hdr_tag, hdr_len - sizeof(struct m3ua_param_header));
    p += hdr_len + align;
  }
  return;

invalid:
  ND_PRINT((ndo, "%s", istr));
  ND_TCHECK2(*buf, size);
  return;
trunc:
  ND_PRINT((ndo, "%s", tstr));
}

/*
 *     0                   1                   2                   3
 *     0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1
 *    +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 *    |    Version    |   Reserved    | Message Class | Message Type  |
 *    +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 *    |                        Message Length                         |
 *    +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 *    \                                                               \
 *    /                                                               /
 */
void
m3ua_print(netdissect_options *ndo,
           const u_char *buf, const u_int size)
{
  const struct m3ua_common_header *hdr = (const struct m3ua_common_header *) buf;
  const struct tok *dict;

  /* size includes the header */
  if (size < sizeof(struct m3ua_common_header))
    goto invalid;
  ND_TCHECK(*hdr);
  if (hdr->v != M3UA_REL_1_0)
    return;

  dict =
    hdr->msg_class == M3UA_MSGC_MGMT     ? MgmtMessages :
    hdr->msg_class == M3UA_MSGC_TRANSFER ? TransferMessages :
    hdr->msg_class == M3UA_MSGC_SSNM     ? SS7Messages :
    hdr->msg_class == M3UA_MSGC_ASPSM    ? ASPStateMessages :
    hdr->msg_class == M3UA_MSGC_ASPTM    ? ASPTrafficMessages :
    hdr->msg_class == M3UA_MSGC_RKM      ? RoutingKeyMgmtMessages :
    NULL;

  ND_PRINT((ndo, "\n\t\t%s", tok2str(MessageClasses, "Unknown message class %i", hdr->msg_class)));
  if (dict != NULL)
    ND_PRINT((ndo, " %s Message", tok2str(dict, "Unknown (0x%02x)", hdr->msg_type)));

  if (size != EXTRACT_32BITS(&hdr->len))
    ND_PRINT((ndo, "\n\t\t\t@@@@@@ Corrupted length %u of message @@@@@@", EXTRACT_32BITS(&hdr->len)));
  else
    m3ua_tags_print(ndo, buf + sizeof(struct m3ua_common_header), EXTRACT_32BITS(&hdr->len) - sizeof(struct m3ua_common_header));
  return;

invalid:
  ND_PRINT((ndo, "%s", istr));
  ND_TCHECK2(*buf, size);
  return;
trunc:
  ND_PRINT((ndo, "%s", tstr));
}

