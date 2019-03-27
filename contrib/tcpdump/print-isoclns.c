/*
 * Copyright (c) 1992, 1993, 1994, 1995, 1996
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
 *
 * Original code by Matt Thomas, Digital Equipment Corporation
 *
 * Extensively modified by Hannes Gredler (hannes@gredler.at) for more
 * complete IS-IS & CLNP support.
 */

/* \summary: ISO CLNS, ESIS, and ISIS printer */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <netdissect-stdinc.h>

#include <string.h>

#include "netdissect.h"
#include "addrtoname.h"
#include "ether.h"
#include "nlpid.h"
#include "extract.h"
#include "gmpls.h"
#include "oui.h"
#include "signature.h"

static const char tstr[] = " [|isis]";

/*
 * IS-IS is defined in ISO 10589.  Look there for protocol definitions.
 */

#define SYSTEM_ID_LEN	ETHER_ADDR_LEN
#define NODE_ID_LEN     SYSTEM_ID_LEN+1
#define LSP_ID_LEN      SYSTEM_ID_LEN+2

#define ISIS_VERSION	1
#define ESIS_VERSION	1
#define CLNP_VERSION	1

#define ISIS_PDU_TYPE_MASK      0x1F
#define ESIS_PDU_TYPE_MASK      0x1F
#define CLNP_PDU_TYPE_MASK      0x1F
#define CLNP_FLAG_MASK          0xE0
#define ISIS_LAN_PRIORITY_MASK  0x7F

#define ISIS_PDU_L1_LAN_IIH	15
#define ISIS_PDU_L2_LAN_IIH	16
#define ISIS_PDU_PTP_IIH	17
#define ISIS_PDU_L1_LSP       	18
#define ISIS_PDU_L2_LSP       	20
#define ISIS_PDU_L1_CSNP  	24
#define ISIS_PDU_L2_CSNP  	25
#define ISIS_PDU_L1_PSNP        26
#define ISIS_PDU_L2_PSNP        27

static const struct tok isis_pdu_values[] = {
    { ISIS_PDU_L1_LAN_IIH,       "L1 Lan IIH"},
    { ISIS_PDU_L2_LAN_IIH,       "L2 Lan IIH"},
    { ISIS_PDU_PTP_IIH,          "p2p IIH"},
    { ISIS_PDU_L1_LSP,           "L1 LSP"},
    { ISIS_PDU_L2_LSP,           "L2 LSP"},
    { ISIS_PDU_L1_CSNP,          "L1 CSNP"},
    { ISIS_PDU_L2_CSNP,          "L2 CSNP"},
    { ISIS_PDU_L1_PSNP,          "L1 PSNP"},
    { ISIS_PDU_L2_PSNP,          "L2 PSNP"},
    { 0, NULL}
};

/*
 * A TLV is a tuple of a type, length and a value and is normally used for
 * encoding information in all sorts of places.  This is an enumeration of
 * the well known types.
 *
 * list taken from rfc3359 plus some memory from veterans ;-)
 */

#define ISIS_TLV_AREA_ADDR           1   /* iso10589 */
#define ISIS_TLV_IS_REACH            2   /* iso10589 */
#define ISIS_TLV_ESNEIGH             3   /* iso10589 */
#define ISIS_TLV_PART_DIS            4   /* iso10589 */
#define ISIS_TLV_PREFIX_NEIGH        5   /* iso10589 */
#define ISIS_TLV_ISNEIGH             6   /* iso10589 */
#define ISIS_TLV_ISNEIGH_VARLEN      7   /* iso10589 */
#define ISIS_TLV_PADDING             8   /* iso10589 */
#define ISIS_TLV_LSP                 9   /* iso10589 */
#define ISIS_TLV_AUTH                10  /* iso10589, rfc3567 */
#define ISIS_TLV_CHECKSUM            12  /* rfc3358 */
#define ISIS_TLV_CHECKSUM_MINLEN 2
#define ISIS_TLV_POI                 13  /* rfc6232 */
#define ISIS_TLV_LSP_BUFFERSIZE      14  /* iso10589 rev2 */
#define ISIS_TLV_LSP_BUFFERSIZE_MINLEN 2
#define ISIS_TLV_EXT_IS_REACH        22  /* draft-ietf-isis-traffic-05 */
#define ISIS_TLV_IS_ALIAS_ID         24  /* draft-ietf-isis-ext-lsp-frags-02 */
#define ISIS_TLV_DECNET_PHASE4       42
#define ISIS_TLV_LUCENT_PRIVATE      66
#define ISIS_TLV_INT_IP_REACH        128 /* rfc1195, rfc2966 */
#define ISIS_TLV_PROTOCOLS           129 /* rfc1195 */
#define ISIS_TLV_EXT_IP_REACH        130 /* rfc1195, rfc2966 */
#define ISIS_TLV_IDRP_INFO           131 /* rfc1195 */
#define ISIS_TLV_IDRP_INFO_MINLEN      1
#define ISIS_TLV_IPADDR              132 /* rfc1195 */
#define ISIS_TLV_IPAUTH              133 /* rfc1195 */
#define ISIS_TLV_TE_ROUTER_ID        134 /* draft-ietf-isis-traffic-05 */
#define ISIS_TLV_EXTD_IP_REACH       135 /* draft-ietf-isis-traffic-05 */
#define ISIS_TLV_HOSTNAME            137 /* rfc2763 */
#define ISIS_TLV_SHARED_RISK_GROUP   138 /* draft-ietf-isis-gmpls-extensions */
#define ISIS_TLV_MT_PORT_CAP         143 /* rfc6165 */
#define ISIS_TLV_MT_CAPABILITY       144 /* rfc6329 */
#define ISIS_TLV_NORTEL_PRIVATE1     176
#define ISIS_TLV_NORTEL_PRIVATE2     177
#define ISIS_TLV_RESTART_SIGNALING   211 /* rfc3847 */
#define ISIS_TLV_RESTART_SIGNALING_FLAGLEN 1
#define ISIS_TLV_RESTART_SIGNALING_HOLDTIMELEN 2
#define ISIS_TLV_MT_IS_REACH         222 /* draft-ietf-isis-wg-multi-topology-05 */
#define ISIS_TLV_MT_SUPPORTED        229 /* draft-ietf-isis-wg-multi-topology-05 */
#define ISIS_TLV_MT_SUPPORTED_MINLEN 2
#define ISIS_TLV_IP6ADDR             232 /* draft-ietf-isis-ipv6-02 */
#define ISIS_TLV_MT_IP_REACH         235 /* draft-ietf-isis-wg-multi-topology-05 */
#define ISIS_TLV_IP6_REACH           236 /* draft-ietf-isis-ipv6-02 */
#define ISIS_TLV_MT_IP6_REACH        237 /* draft-ietf-isis-wg-multi-topology-05 */
#define ISIS_TLV_PTP_ADJ             240 /* rfc3373 */
#define ISIS_TLV_IIH_SEQNR           241 /* draft-shen-isis-iih-sequence-00 */
#define ISIS_TLV_IIH_SEQNR_MINLEN 4
#define ISIS_TLV_VENDOR_PRIVATE      250 /* draft-ietf-isis-experimental-tlv-01 */
#define ISIS_TLV_VENDOR_PRIVATE_MINLEN 3

static const struct tok isis_tlv_values[] = {
    { ISIS_TLV_AREA_ADDR,	   "Area address(es)"},
    { ISIS_TLV_IS_REACH,           "IS Reachability"},
    { ISIS_TLV_ESNEIGH,            "ES Neighbor(s)"},
    { ISIS_TLV_PART_DIS,           "Partition DIS"},
    { ISIS_TLV_PREFIX_NEIGH,       "Prefix Neighbors"},
    { ISIS_TLV_ISNEIGH,            "IS Neighbor(s)"},
    { ISIS_TLV_ISNEIGH_VARLEN,     "IS Neighbor(s) (variable length)"},
    { ISIS_TLV_PADDING,            "Padding"},
    { ISIS_TLV_LSP,                "LSP entries"},
    { ISIS_TLV_AUTH,               "Authentication"},
    { ISIS_TLV_CHECKSUM,           "Checksum"},
    { ISIS_TLV_POI,                "Purge Originator Identifier"},
    { ISIS_TLV_LSP_BUFFERSIZE,     "LSP Buffersize"},
    { ISIS_TLV_EXT_IS_REACH,       "Extended IS Reachability"},
    { ISIS_TLV_IS_ALIAS_ID,        "IS Alias ID"},
    { ISIS_TLV_DECNET_PHASE4,      "DECnet Phase IV"},
    { ISIS_TLV_LUCENT_PRIVATE,     "Lucent Proprietary"},
    { ISIS_TLV_INT_IP_REACH,       "IPv4 Internal Reachability"},
    { ISIS_TLV_PROTOCOLS,          "Protocols supported"},
    { ISIS_TLV_EXT_IP_REACH,       "IPv4 External Reachability"},
    { ISIS_TLV_IDRP_INFO,          "Inter-Domain Information Type"},
    { ISIS_TLV_IPADDR,             "IPv4 Interface address(es)"},
    { ISIS_TLV_IPAUTH,             "IPv4 authentication (deprecated)"},
    { ISIS_TLV_TE_ROUTER_ID,       "Traffic Engineering Router ID"},
    { ISIS_TLV_EXTD_IP_REACH,      "Extended IPv4 Reachability"},
    { ISIS_TLV_SHARED_RISK_GROUP,  "Shared Risk Link Group"},
    { ISIS_TLV_MT_PORT_CAP,        "Multi-Topology-Aware Port Capability"},
    { ISIS_TLV_MT_CAPABILITY,      "Multi-Topology Capability"},
    { ISIS_TLV_NORTEL_PRIVATE1,    "Nortel Proprietary"},
    { ISIS_TLV_NORTEL_PRIVATE2,    "Nortel Proprietary"},
    { ISIS_TLV_HOSTNAME,           "Hostname"},
    { ISIS_TLV_RESTART_SIGNALING,  "Restart Signaling"},
    { ISIS_TLV_MT_IS_REACH,        "Multi Topology IS Reachability"},
    { ISIS_TLV_MT_SUPPORTED,       "Multi Topology"},
    { ISIS_TLV_IP6ADDR,            "IPv6 Interface address(es)"},
    { ISIS_TLV_MT_IP_REACH,        "Multi-Topology IPv4 Reachability"},
    { ISIS_TLV_IP6_REACH,          "IPv6 reachability"},
    { ISIS_TLV_MT_IP6_REACH,       "Multi-Topology IP6 Reachability"},
    { ISIS_TLV_PTP_ADJ,            "Point-to-point Adjacency State"},
    { ISIS_TLV_IIH_SEQNR,          "Hello PDU Sequence Number"},
    { ISIS_TLV_VENDOR_PRIVATE,     "Vendor Private"},
    { 0, NULL }
};

#define ESIS_OPTION_PROTOCOLS        129
#define ESIS_OPTION_QOS_MAINTENANCE  195 /* iso9542 */
#define ESIS_OPTION_SECURITY         197 /* iso9542 */
#define ESIS_OPTION_ES_CONF_TIME     198 /* iso9542 */
#define ESIS_OPTION_PRIORITY         205 /* iso9542 */
#define ESIS_OPTION_ADDRESS_MASK     225 /* iso9542 */
#define ESIS_OPTION_SNPA_MASK        226 /* iso9542 */

static const struct tok esis_option_values[] = {
    { ESIS_OPTION_PROTOCOLS,       "Protocols supported"},
    { ESIS_OPTION_QOS_MAINTENANCE, "QoS Maintenance" },
    { ESIS_OPTION_SECURITY,        "Security" },
    { ESIS_OPTION_ES_CONF_TIME,    "ES Configuration Time" },
    { ESIS_OPTION_PRIORITY,        "Priority" },
    { ESIS_OPTION_ADDRESS_MASK,    "Addressk Mask" },
    { ESIS_OPTION_SNPA_MASK,       "SNPA Mask" },
    { 0, NULL }
};

#define CLNP_OPTION_DISCARD_REASON   193
#define CLNP_OPTION_QOS_MAINTENANCE  195 /* iso8473 */
#define CLNP_OPTION_SECURITY         197 /* iso8473 */
#define CLNP_OPTION_SOURCE_ROUTING   200 /* iso8473 */
#define CLNP_OPTION_ROUTE_RECORDING  203 /* iso8473 */
#define CLNP_OPTION_PADDING          204 /* iso8473 */
#define CLNP_OPTION_PRIORITY         205 /* iso8473 */

static const struct tok clnp_option_values[] = {
    { CLNP_OPTION_DISCARD_REASON,  "Discard Reason"},
    { CLNP_OPTION_PRIORITY,        "Priority"},
    { CLNP_OPTION_QOS_MAINTENANCE, "QoS Maintenance"},
    { CLNP_OPTION_SECURITY, "Security"},
    { CLNP_OPTION_SOURCE_ROUTING, "Source Routing"},
    { CLNP_OPTION_ROUTE_RECORDING, "Route Recording"},
    { CLNP_OPTION_PADDING, "Padding"},
    { 0, NULL }
};

static const struct tok clnp_option_rfd_class_values[] = {
    { 0x0, "General"},
    { 0x8, "Address"},
    { 0x9, "Source Routeing"},
    { 0xa, "Lifetime"},
    { 0xb, "PDU Discarded"},
    { 0xc, "Reassembly"},
    { 0, NULL }
};

static const struct tok clnp_option_rfd_general_values[] = {
    { 0x0, "Reason not specified"},
    { 0x1, "Protocol procedure error"},
    { 0x2, "Incorrect checksum"},
    { 0x3, "PDU discarded due to congestion"},
    { 0x4, "Header syntax error (cannot be parsed)"},
    { 0x5, "Segmentation needed but not permitted"},
    { 0x6, "Incomplete PDU received"},
    { 0x7, "Duplicate option"},
    { 0, NULL }
};

static const struct tok clnp_option_rfd_address_values[] = {
    { 0x0, "Destination address unreachable"},
    { 0x1, "Destination address unknown"},
    { 0, NULL }
};

static const struct tok clnp_option_rfd_source_routeing_values[] = {
    { 0x0, "Unspecified source routeing error"},
    { 0x1, "Syntax error in source routeing field"},
    { 0x2, "Unknown address in source routeing field"},
    { 0x3, "Path not acceptable"},
    { 0, NULL }
};

static const struct tok clnp_option_rfd_lifetime_values[] = {
    { 0x0, "Lifetime expired while data unit in transit"},
    { 0x1, "Lifetime expired during reassembly"},
    { 0, NULL }
};

static const struct tok clnp_option_rfd_pdu_discard_values[] = {
    { 0x0, "Unsupported option not specified"},
    { 0x1, "Unsupported protocol version"},
    { 0x2, "Unsupported security option"},
    { 0x3, "Unsupported source routeing option"},
    { 0x4, "Unsupported recording of route option"},
    { 0, NULL }
};

static const struct tok clnp_option_rfd_reassembly_values[] = {
    { 0x0, "Reassembly interference"},
    { 0, NULL }
};

/* array of 16 error-classes */
static const struct tok *clnp_option_rfd_error_class[] = {
    clnp_option_rfd_general_values,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    clnp_option_rfd_address_values,
    clnp_option_rfd_source_routeing_values,
    clnp_option_rfd_lifetime_values,
    clnp_option_rfd_pdu_discard_values,
    clnp_option_rfd_reassembly_values,
    NULL,
    NULL,
    NULL
};

#define CLNP_OPTION_OPTION_QOS_MASK 0x3f
#define CLNP_OPTION_SCOPE_MASK      0xc0
#define CLNP_OPTION_SCOPE_SA_SPEC   0x40
#define CLNP_OPTION_SCOPE_DA_SPEC   0x80
#define CLNP_OPTION_SCOPE_GLOBAL    0xc0

static const struct tok clnp_option_scope_values[] = {
    { CLNP_OPTION_SCOPE_SA_SPEC, "Source Address Specific"},
    { CLNP_OPTION_SCOPE_DA_SPEC, "Destination Address Specific"},
    { CLNP_OPTION_SCOPE_GLOBAL, "Globally unique"},
    { 0, NULL }
};

static const struct tok clnp_option_sr_rr_values[] = {
    { 0x0, "partial"},
    { 0x1, "complete"},
    { 0, NULL }
};

static const struct tok clnp_option_sr_rr_string_values[] = {
    { CLNP_OPTION_SOURCE_ROUTING, "source routing"},
    { CLNP_OPTION_ROUTE_RECORDING, "recording of route in progress"},
    { 0, NULL }
};

static const struct tok clnp_option_qos_global_values[] = {
    { 0x20, "reserved"},
    { 0x10, "sequencing vs. delay"},
    { 0x08, "congested"},
    { 0x04, "delay vs. cost"},
    { 0x02, "error vs. delay"},
    { 0x01, "error vs. cost"},
    { 0, NULL }
};

#define ISIS_SUBTLV_EXT_IS_REACH_ADMIN_GROUP           3 /* draft-ietf-isis-traffic-05 */
#define ISIS_SUBTLV_EXT_IS_REACH_LINK_LOCAL_REMOTE_ID  4 /* rfc4205 */
#define ISIS_SUBTLV_EXT_IS_REACH_LINK_REMOTE_ID        5 /* draft-ietf-isis-traffic-05 */
#define ISIS_SUBTLV_EXT_IS_REACH_IPV4_INTF_ADDR        6 /* draft-ietf-isis-traffic-05 */
#define ISIS_SUBTLV_EXT_IS_REACH_IPV4_NEIGHBOR_ADDR    8 /* draft-ietf-isis-traffic-05 */
#define ISIS_SUBTLV_EXT_IS_REACH_MAX_LINK_BW           9 /* draft-ietf-isis-traffic-05 */
#define ISIS_SUBTLV_EXT_IS_REACH_RESERVABLE_BW        10 /* draft-ietf-isis-traffic-05 */
#define ISIS_SUBTLV_EXT_IS_REACH_UNRESERVED_BW        11 /* rfc4124 */
#define ISIS_SUBTLV_EXT_IS_REACH_BW_CONSTRAINTS_OLD   12 /* draft-ietf-tewg-diff-te-proto-06 */
#define ISIS_SUBTLV_EXT_IS_REACH_TE_METRIC            18 /* draft-ietf-isis-traffic-05 */
#define ISIS_SUBTLV_EXT_IS_REACH_LINK_ATTRIBUTE       19 /* draft-ietf-isis-link-attr-01 */
#define ISIS_SUBTLV_EXT_IS_REACH_LINK_PROTECTION_TYPE 20 /* rfc4205 */
#define ISIS_SUBTLV_EXT_IS_REACH_INTF_SW_CAP_DESCR    21 /* rfc4205 */
#define ISIS_SUBTLV_EXT_IS_REACH_BW_CONSTRAINTS       22 /* rfc4124 */

#define ISIS_SUBTLV_SPB_METRIC                        29 /* rfc6329 */

static const struct tok isis_ext_is_reach_subtlv_values[] = {
    { ISIS_SUBTLV_EXT_IS_REACH_ADMIN_GROUP,            "Administrative groups" },
    { ISIS_SUBTLV_EXT_IS_REACH_LINK_LOCAL_REMOTE_ID,   "Link Local/Remote Identifier" },
    { ISIS_SUBTLV_EXT_IS_REACH_LINK_REMOTE_ID,         "Link Remote Identifier" },
    { ISIS_SUBTLV_EXT_IS_REACH_IPV4_INTF_ADDR,         "IPv4 interface address" },
    { ISIS_SUBTLV_EXT_IS_REACH_IPV4_NEIGHBOR_ADDR,     "IPv4 neighbor address" },
    { ISIS_SUBTLV_EXT_IS_REACH_MAX_LINK_BW,            "Maximum link bandwidth" },
    { ISIS_SUBTLV_EXT_IS_REACH_RESERVABLE_BW,          "Reservable link bandwidth" },
    { ISIS_SUBTLV_EXT_IS_REACH_UNRESERVED_BW,          "Unreserved bandwidth" },
    { ISIS_SUBTLV_EXT_IS_REACH_TE_METRIC,              "Traffic Engineering Metric" },
    { ISIS_SUBTLV_EXT_IS_REACH_LINK_ATTRIBUTE,         "Link Attribute" },
    { ISIS_SUBTLV_EXT_IS_REACH_LINK_PROTECTION_TYPE,   "Link Protection Type" },
    { ISIS_SUBTLV_EXT_IS_REACH_INTF_SW_CAP_DESCR,      "Interface Switching Capability" },
    { ISIS_SUBTLV_EXT_IS_REACH_BW_CONSTRAINTS_OLD,     "Bandwidth Constraints (old)" },
    { ISIS_SUBTLV_EXT_IS_REACH_BW_CONSTRAINTS,         "Bandwidth Constraints" },
    { ISIS_SUBTLV_SPB_METRIC,                          "SPB Metric" },
    { 250,                                             "Reserved for cisco specific extensions" },
    { 251,                                             "Reserved for cisco specific extensions" },
    { 252,                                             "Reserved for cisco specific extensions" },
    { 253,                                             "Reserved for cisco specific extensions" },
    { 254,                                             "Reserved for cisco specific extensions" },
    { 255,                                             "Reserved for future expansion" },
    { 0, NULL }
};

#define ISIS_SUBTLV_EXTD_IP_REACH_ADMIN_TAG32          1 /* draft-ietf-isis-admin-tags-01 */
#define ISIS_SUBTLV_EXTD_IP_REACH_ADMIN_TAG64          2 /* draft-ietf-isis-admin-tags-01 */
#define ISIS_SUBTLV_EXTD_IP_REACH_MGMT_PREFIX_COLOR  117 /* draft-ietf-isis-wg-multi-topology-05 */

static const struct tok isis_ext_ip_reach_subtlv_values[] = {
    { ISIS_SUBTLV_EXTD_IP_REACH_ADMIN_TAG32,           "32-Bit Administrative tag" },
    { ISIS_SUBTLV_EXTD_IP_REACH_ADMIN_TAG64,           "64-Bit Administrative tag" },
    { ISIS_SUBTLV_EXTD_IP_REACH_MGMT_PREFIX_COLOR,     "Management Prefix Color" },
    { 0, NULL }
};

static const struct tok isis_subtlv_link_attribute_values[] = {
    { 0x01, "Local Protection Available" },
    { 0x02, "Link excluded from local protection path" },
    { 0x04, "Local maintenance required"},
    { 0, NULL }
};

#define ISIS_SUBTLV_AUTH_SIMPLE        1
#define ISIS_SUBTLV_AUTH_GENERIC       3 /* rfc 5310 */
#define ISIS_SUBTLV_AUTH_MD5          54
#define ISIS_SUBTLV_AUTH_MD5_LEN      16
#define ISIS_SUBTLV_AUTH_PRIVATE     255

static const struct tok isis_subtlv_auth_values[] = {
    { ISIS_SUBTLV_AUTH_SIMPLE,	"simple text password"},
    { ISIS_SUBTLV_AUTH_GENERIC, "Generic Crypto key-id"},
    { ISIS_SUBTLV_AUTH_MD5,	"HMAC-MD5 password"},
    { ISIS_SUBTLV_AUTH_PRIVATE,	"Routing Domain private password"},
    { 0, NULL }
};

#define ISIS_SUBTLV_IDRP_RES           0
#define ISIS_SUBTLV_IDRP_LOCAL         1
#define ISIS_SUBTLV_IDRP_ASN           2

static const struct tok isis_subtlv_idrp_values[] = {
    { ISIS_SUBTLV_IDRP_RES,         "Reserved"},
    { ISIS_SUBTLV_IDRP_LOCAL,       "Routing-Domain Specific"},
    { ISIS_SUBTLV_IDRP_ASN,         "AS Number Tag"},
    { 0, NULL}
};

#define ISIS_SUBTLV_SPB_MCID          4
#define ISIS_SUBTLV_SPB_DIGEST        5
#define ISIS_SUBTLV_SPB_BVID          6

#define ISIS_SUBTLV_SPB_INSTANCE      1
#define ISIS_SUBTLV_SPBM_SI           3

#define ISIS_SPB_MCID_LEN                         51
#define ISIS_SUBTLV_SPB_MCID_MIN_LEN              102
#define ISIS_SUBTLV_SPB_DIGEST_MIN_LEN            33
#define ISIS_SUBTLV_SPB_BVID_MIN_LEN              6
#define ISIS_SUBTLV_SPB_INSTANCE_MIN_LEN          19
#define ISIS_SUBTLV_SPB_INSTANCE_VLAN_TUPLE_LEN   8

static const struct tok isis_mt_port_cap_subtlv_values[] = {
    { ISIS_SUBTLV_SPB_MCID,           "SPB MCID" },
    { ISIS_SUBTLV_SPB_DIGEST,         "SPB Digest" },
    { ISIS_SUBTLV_SPB_BVID,           "SPB BVID" },
    { 0, NULL }
};

static const struct tok isis_mt_capability_subtlv_values[] = {
    { ISIS_SUBTLV_SPB_INSTANCE,      "SPB Instance" },
    { ISIS_SUBTLV_SPBM_SI,      "SPBM Service Identifier and Unicast Address" },
    { 0, NULL }
};

struct isis_spb_mcid {
  uint8_t  format_id;
  uint8_t  name[32];
  uint8_t  revision_lvl[2];
  uint8_t  digest[16];
};

struct isis_subtlv_spb_mcid {
  struct isis_spb_mcid mcid;
  struct isis_spb_mcid aux_mcid;
};

struct isis_subtlv_spb_instance {
  uint8_t cist_root_id[8];
  uint8_t cist_external_root_path_cost[4];
  uint8_t bridge_priority[2];
  uint8_t spsourceid[4];
  uint8_t no_of_trees;
};

#define CLNP_SEGMENT_PART  0x80
#define CLNP_MORE_SEGMENTS 0x40
#define CLNP_REQUEST_ER    0x20

static const struct tok clnp_flag_values[] = {
    { CLNP_SEGMENT_PART, "Segmentation permitted"},
    { CLNP_MORE_SEGMENTS, "more Segments"},
    { CLNP_REQUEST_ER, "request Error Report"},
    { 0, NULL}
};

#define ISIS_MASK_LSP_OL_BIT(x)            ((x)&0x4)
#define ISIS_MASK_LSP_ISTYPE_BITS(x)       ((x)&0x3)
#define ISIS_MASK_LSP_PARTITION_BIT(x)     ((x)&0x80)
#define ISIS_MASK_LSP_ATT_BITS(x)          ((x)&0x78)
#define ISIS_MASK_LSP_ATT_ERROR_BIT(x)     ((x)&0x40)
#define ISIS_MASK_LSP_ATT_EXPENSE_BIT(x)   ((x)&0x20)
#define ISIS_MASK_LSP_ATT_DELAY_BIT(x)     ((x)&0x10)
#define ISIS_MASK_LSP_ATT_DEFAULT_BIT(x)   ((x)&0x8)

#define ISIS_MASK_MTID(x)                  ((x)&0x0fff)
#define ISIS_MASK_MTFLAGS(x)               ((x)&0xf000)

static const struct tok isis_mt_flag_values[] = {
    { 0x4000,                  "ATT bit set"},
    { 0x8000,                  "Overload bit set"},
    { 0, NULL}
};

#define ISIS_MASK_TLV_EXTD_IP_UPDOWN(x)     ((x)&0x80)
#define ISIS_MASK_TLV_EXTD_IP_SUBTLV(x)     ((x)&0x40)

#define ISIS_MASK_TLV_EXTD_IP6_IE(x)        ((x)&0x40)
#define ISIS_MASK_TLV_EXTD_IP6_SUBTLV(x)    ((x)&0x20)

#define ISIS_LSP_TLV_METRIC_SUPPORTED(x)   ((x)&0x80)
#define ISIS_LSP_TLV_METRIC_IE(x)          ((x)&0x40)
#define ISIS_LSP_TLV_METRIC_UPDOWN(x)      ((x)&0x80)
#define ISIS_LSP_TLV_METRIC_VALUE(x)	   ((x)&0x3f)

#define ISIS_MASK_TLV_SHARED_RISK_GROUP(x) ((x)&0x1)

static const struct tok isis_mt_values[] = {
    { 0,    "IPv4 unicast"},
    { 1,    "In-Band Management"},
    { 2,    "IPv6 unicast"},
    { 3,    "Multicast"},
    { 4095, "Development, Experimental or Proprietary"},
    { 0, NULL }
};

static const struct tok isis_iih_circuit_type_values[] = {
    { 1,    "Level 1 only"},
    { 2,    "Level 2 only"},
    { 3,    "Level 1, Level 2"},
    { 0, NULL}
};

#define ISIS_LSP_TYPE_UNUSED0   0
#define ISIS_LSP_TYPE_LEVEL_1   1
#define ISIS_LSP_TYPE_UNUSED2   2
#define ISIS_LSP_TYPE_LEVEL_2   3

static const struct tok isis_lsp_istype_values[] = {
    { ISIS_LSP_TYPE_UNUSED0,	"Unused 0x0 (invalid)"},
    { ISIS_LSP_TYPE_LEVEL_1,	"L1 IS"},
    { ISIS_LSP_TYPE_UNUSED2,	"Unused 0x2 (invalid)"},
    { ISIS_LSP_TYPE_LEVEL_2,	"L2 IS"},
    { 0, NULL }
};

/*
 * Katz's point to point adjacency TLV uses codes to tell us the state of
 * the remote adjacency.  Enumerate them.
 */

#define ISIS_PTP_ADJ_UP   0
#define ISIS_PTP_ADJ_INIT 1
#define ISIS_PTP_ADJ_DOWN 2

static const struct tok isis_ptp_adjancey_values[] = {
    { ISIS_PTP_ADJ_UP,    "Up" },
    { ISIS_PTP_ADJ_INIT,  "Initializing" },
    { ISIS_PTP_ADJ_DOWN,  "Down" },
    { 0, NULL}
};

struct isis_tlv_ptp_adj {
    uint8_t adjacency_state;
    uint8_t extd_local_circuit_id[4];
    uint8_t neighbor_sysid[SYSTEM_ID_LEN];
    uint8_t neighbor_extd_local_circuit_id[4];
};

static void osi_print_cksum(netdissect_options *, const uint8_t *pptr,
			    uint16_t checksum, int checksum_offset, u_int length);
static int clnp_print(netdissect_options *, const uint8_t *, u_int);
static void esis_print(netdissect_options *, const uint8_t *, u_int);
static int isis_print(netdissect_options *, const uint8_t *, u_int);

struct isis_metric_block {
    uint8_t metric_default;
    uint8_t metric_delay;
    uint8_t metric_expense;
    uint8_t metric_error;
};

struct isis_tlv_is_reach {
    struct isis_metric_block isis_metric_block;
    uint8_t neighbor_nodeid[NODE_ID_LEN];
};

struct isis_tlv_es_reach {
    struct isis_metric_block isis_metric_block;
    uint8_t neighbor_sysid[SYSTEM_ID_LEN];
};

struct isis_tlv_ip_reach {
    struct isis_metric_block isis_metric_block;
    uint8_t prefix[4];
    uint8_t mask[4];
};

static const struct tok isis_is_reach_virtual_values[] = {
    { 0,    "IsNotVirtual"},
    { 1,    "IsVirtual"},
    { 0, NULL }
};

static const struct tok isis_restart_flag_values[] = {
    { 0x1,  "Restart Request"},
    { 0x2,  "Restart Acknowledgement"},
    { 0x4,  "Suppress adjacency advertisement"},
    { 0, NULL }
};

struct isis_common_header {
    uint8_t nlpid;
    uint8_t fixed_len;
    uint8_t version;			/* Protocol version */
    uint8_t id_length;
    uint8_t pdu_type;		        /* 3 MSbits are reserved */
    uint8_t pdu_version;		/* Packet format version */
    uint8_t reserved;
    uint8_t max_area;
};

struct isis_iih_lan_header {
    uint8_t circuit_type;
    uint8_t source_id[SYSTEM_ID_LEN];
    uint8_t holding_time[2];
    uint8_t pdu_len[2];
    uint8_t priority;
    uint8_t lan_id[NODE_ID_LEN];
};

struct isis_iih_ptp_header {
    uint8_t circuit_type;
    uint8_t source_id[SYSTEM_ID_LEN];
    uint8_t holding_time[2];
    uint8_t pdu_len[2];
    uint8_t circuit_id;
};

struct isis_lsp_header {
    uint8_t pdu_len[2];
    uint8_t remaining_lifetime[2];
    uint8_t lsp_id[LSP_ID_LEN];
    uint8_t sequence_number[4];
    uint8_t checksum[2];
    uint8_t typeblock;
};

struct isis_csnp_header {
    uint8_t pdu_len[2];
    uint8_t source_id[NODE_ID_LEN];
    uint8_t start_lsp_id[LSP_ID_LEN];
    uint8_t end_lsp_id[LSP_ID_LEN];
};

struct isis_psnp_header {
    uint8_t pdu_len[2];
    uint8_t source_id[NODE_ID_LEN];
};

struct isis_tlv_lsp {
    uint8_t remaining_lifetime[2];
    uint8_t lsp_id[LSP_ID_LEN];
    uint8_t sequence_number[4];
    uint8_t checksum[2];
};

#define ISIS_COMMON_HEADER_SIZE (sizeof(struct isis_common_header))
#define ISIS_IIH_LAN_HEADER_SIZE (sizeof(struct isis_iih_lan_header))
#define ISIS_IIH_PTP_HEADER_SIZE (sizeof(struct isis_iih_ptp_header))
#define ISIS_LSP_HEADER_SIZE (sizeof(struct isis_lsp_header))
#define ISIS_CSNP_HEADER_SIZE (sizeof(struct isis_csnp_header))
#define ISIS_PSNP_HEADER_SIZE (sizeof(struct isis_psnp_header))

void
isoclns_print(netdissect_options *ndo, const uint8_t *p, u_int length)
{
	if (!ND_TTEST(*p)) { /* enough bytes on the wire ? */
		ND_PRINT((ndo, "|OSI"));
		return;
	}

	if (ndo->ndo_eflag)
		ND_PRINT((ndo, "OSI NLPID %s (0x%02x): ", tok2str(nlpid_values, "Unknown", *p), *p));

	switch (*p) {

	case NLPID_CLNP:
		if (!clnp_print(ndo, p, length))
			print_unknown_data(ndo, p, "\n\t", length);
		break;

	case NLPID_ESIS:
		esis_print(ndo, p, length);
		return;

	case NLPID_ISIS:
		if (!isis_print(ndo, p, length))
			print_unknown_data(ndo, p, "\n\t", length);
		break;

	case NLPID_NULLNS:
		ND_PRINT((ndo, "%slength: %u", ndo->ndo_eflag ? "" : ", ", length));
		break;

	case NLPID_Q933:
		q933_print(ndo, p + 1, length - 1);
		break;

	case NLPID_IP:
		ip_print(ndo, p + 1, length - 1);
		break;

	case NLPID_IP6:
		ip6_print(ndo, p + 1, length - 1);
		break;

	case NLPID_PPP:
		ppp_print(ndo, p + 1, length - 1);
		break;

	default:
		if (!ndo->ndo_eflag)
			ND_PRINT((ndo, "OSI NLPID 0x%02x unknown", *p));
		ND_PRINT((ndo, "%slength: %u", ndo->ndo_eflag ? "" : ", ", length));
		if (length > 1)
			print_unknown_data(ndo, p, "\n\t", length);
		break;
	}
}

#define	CLNP_PDU_ER	 1
#define	CLNP_PDU_DT	28
#define	CLNP_PDU_MD	29
#define	CLNP_PDU_ERQ	30
#define	CLNP_PDU_ERP	31

static const struct tok clnp_pdu_values[] = {
    { CLNP_PDU_ER,  "Error Report"},
    { CLNP_PDU_MD,  "MD"},
    { CLNP_PDU_DT,  "Data"},
    { CLNP_PDU_ERQ, "Echo Request"},
    { CLNP_PDU_ERP, "Echo Response"},
    { 0, NULL }
};

struct clnp_header_t {
    uint8_t nlpid;
    uint8_t length_indicator;
    uint8_t version;
    uint8_t lifetime; /* units of 500ms */
    uint8_t type;
    uint8_t segment_length[2];
    uint8_t cksum[2];
};

struct clnp_segment_header_t {
    uint8_t data_unit_id[2];
    uint8_t segment_offset[2];
    uint8_t total_length[2];
};

/*
 * clnp_print
 * Decode CLNP packets.  Return 0 on error.
 */

static int
clnp_print(netdissect_options *ndo,
           const uint8_t *pptr, u_int length)
{
	const uint8_t *optr,*source_address,*dest_address;
        u_int li,tlen,nsap_offset,source_address_length,dest_address_length, clnp_pdu_type, clnp_flags;
	const struct clnp_header_t *clnp_header;
	const struct clnp_segment_header_t *clnp_segment_header;
        uint8_t rfd_error_major,rfd_error_minor;

	clnp_header = (const struct clnp_header_t *) pptr;
        ND_TCHECK(*clnp_header);

        li = clnp_header->length_indicator;
        optr = pptr;

        if (!ndo->ndo_eflag)
            ND_PRINT((ndo, "CLNP"));

        /*
         * Sanity checking of the header.
         */

        if (clnp_header->version != CLNP_VERSION) {
            ND_PRINT((ndo, "version %d packet not supported", clnp_header->version));
            return (0);
        }

	if (li > length) {
            ND_PRINT((ndo, " length indicator(%u) > PDU size (%u)!", li, length));
            return (0);
	}

        if (li < sizeof(struct clnp_header_t)) {
            ND_PRINT((ndo, " length indicator %u < min PDU size:", li));
            while (pptr < ndo->ndo_snapend)
                ND_PRINT((ndo, "%02X", *pptr++));
            return (0);
        }

        /* FIXME further header sanity checking */

        clnp_pdu_type = clnp_header->type & CLNP_PDU_TYPE_MASK;
        clnp_flags = clnp_header->type & CLNP_FLAG_MASK;

        pptr += sizeof(struct clnp_header_t);
        li -= sizeof(struct clnp_header_t);

        if (li < 1) {
            ND_PRINT((ndo, "li < size of fixed part of CLNP header and addresses"));
            return (0);
        }
	ND_TCHECK(*pptr);
        dest_address_length = *pptr;
        pptr += 1;
        li -= 1;
        if (li < dest_address_length) {
            ND_PRINT((ndo, "li < size of fixed part of CLNP header and addresses"));
            return (0);
        }
        ND_TCHECK2(*pptr, dest_address_length);
        dest_address = pptr;
        pptr += dest_address_length;
        li -= dest_address_length;

        if (li < 1) {
            ND_PRINT((ndo, "li < size of fixed part of CLNP header and addresses"));
            return (0);
        }
	ND_TCHECK(*pptr);
        source_address_length = *pptr;
        pptr += 1;
        li -= 1;
        if (li < source_address_length) {
            ND_PRINT((ndo, "li < size of fixed part of CLNP header and addresses"));
            return (0);
        }
        ND_TCHECK2(*pptr, source_address_length);
        source_address = pptr;
        pptr += source_address_length;
        li -= source_address_length;

        if (ndo->ndo_vflag < 1) {
            ND_PRINT((ndo, "%s%s > %s, %s, length %u",
                   ndo->ndo_eflag ? "" : ", ",
                   isonsap_string(ndo, source_address, source_address_length),
                   isonsap_string(ndo, dest_address, dest_address_length),
                   tok2str(clnp_pdu_values,"unknown (%u)",clnp_pdu_type),
                   length));
            return (1);
        }
        ND_PRINT((ndo, "%slength %u", ndo->ndo_eflag ? "" : ", ", length));

        ND_PRINT((ndo, "\n\t%s PDU, hlen: %u, v: %u, lifetime: %u.%us, Segment PDU length: %u, checksum: 0x%04x",
               tok2str(clnp_pdu_values, "unknown (%u)",clnp_pdu_type),
               clnp_header->length_indicator,
               clnp_header->version,
               clnp_header->lifetime/2,
               (clnp_header->lifetime%2)*5,
               EXTRACT_16BITS(clnp_header->segment_length),
               EXTRACT_16BITS(clnp_header->cksum)));

        osi_print_cksum(ndo, optr, EXTRACT_16BITS(clnp_header->cksum), 7,
                        clnp_header->length_indicator);

        ND_PRINT((ndo, "\n\tFlags [%s]",
               bittok2str(clnp_flag_values, "none", clnp_flags)));

        ND_PRINT((ndo, "\n\tsource address (length %u): %s\n\tdest   address (length %u): %s",
               source_address_length,
               isonsap_string(ndo, source_address, source_address_length),
               dest_address_length,
               isonsap_string(ndo, dest_address, dest_address_length)));

        if (clnp_flags & CLNP_SEGMENT_PART) {
                if (li < sizeof(const struct clnp_segment_header_t)) {
                    ND_PRINT((ndo, "li < size of fixed part of CLNP header, addresses, and segment part"));
                    return (0);
                }
            	clnp_segment_header = (const struct clnp_segment_header_t *) pptr;
                ND_TCHECK(*clnp_segment_header);
                ND_PRINT((ndo, "\n\tData Unit ID: 0x%04x, Segment Offset: %u, Total PDU Length: %u",
                       EXTRACT_16BITS(clnp_segment_header->data_unit_id),
                       EXTRACT_16BITS(clnp_segment_header->segment_offset),
                       EXTRACT_16BITS(clnp_segment_header->total_length)));
                pptr+=sizeof(const struct clnp_segment_header_t);
                li-=sizeof(const struct clnp_segment_header_t);
        }

        /* now walk the options */
        while (li >= 2) {
            u_int op, opli;
            const uint8_t *tptr;

            if (li < 2) {
                ND_PRINT((ndo, ", bad opts/li"));
                return (0);
            }
            ND_TCHECK2(*pptr, 2);
            op = *pptr++;
            opli = *pptr++;
            li -= 2;
            if (opli > li) {
                ND_PRINT((ndo, ", opt (%d) too long", op));
                return (0);
            }
            ND_TCHECK2(*pptr, opli);
            li -= opli;
            tptr = pptr;
            tlen = opli;

            ND_PRINT((ndo, "\n\t  %s Option #%u, length %u, value: ",
                   tok2str(clnp_option_values,"Unknown",op),
                   op,
                   opli));

            /*
             * We've already checked that the entire option is present
             * in the captured packet with the ND_TCHECK2() call.
             * Therefore, we don't need to do ND_TCHECK()/ND_TCHECK2()
             * checks.
             * We do, however, need to check tlen, to make sure we
             * don't run past the end of the option.
	     */
            switch (op) {


            case CLNP_OPTION_ROUTE_RECORDING: /* those two options share the format */
            case CLNP_OPTION_SOURCE_ROUTING:
                    if (tlen < 2) {
                            ND_PRINT((ndo, ", bad opt len"));
                            return (0);
                    }
                    ND_PRINT((ndo, "%s %s",
                           tok2str(clnp_option_sr_rr_values,"Unknown",*tptr),
                           tok2str(clnp_option_sr_rr_string_values, "Unknown Option %u", op)));
                    nsap_offset=*(tptr+1);
                    if (nsap_offset == 0) {
                            ND_PRINT((ndo, " Bad NSAP offset (0)"));
                            break;
                    }
                    nsap_offset-=1; /* offset to nsap list */
                    if (nsap_offset > tlen) {
                            ND_PRINT((ndo, " Bad NSAP offset (past end of option)"));
                            break;
                    }
                    tptr+=nsap_offset;
                    tlen-=nsap_offset;
                    while (tlen > 0) {
                            source_address_length=*tptr;
                            if (tlen < source_address_length+1) {
                                    ND_PRINT((ndo, "\n\t    NSAP address goes past end of option"));
                                    break;
                            }
                            if (source_address_length > 0) {
                                    source_address=(tptr+1);
                                    ND_TCHECK2(*source_address, source_address_length);
                                    ND_PRINT((ndo, "\n\t    NSAP address (length %u): %s",
                                           source_address_length,
                                           isonsap_string(ndo, source_address, source_address_length)));
                            }
                            tlen-=source_address_length+1;
                    }
                    break;

            case CLNP_OPTION_PRIORITY:
                    if (tlen < 1) {
                            ND_PRINT((ndo, ", bad opt len"));
                            return (0);
                    }
                    ND_PRINT((ndo, "0x%1x", *tptr&0x0f));
                    break;

            case CLNP_OPTION_QOS_MAINTENANCE:
                    if (tlen < 1) {
                            ND_PRINT((ndo, ", bad opt len"));
                            return (0);
                    }
                    ND_PRINT((ndo, "\n\t    Format Code: %s",
                           tok2str(clnp_option_scope_values, "Reserved", *tptr&CLNP_OPTION_SCOPE_MASK)));

                    if ((*tptr&CLNP_OPTION_SCOPE_MASK) == CLNP_OPTION_SCOPE_GLOBAL)
                            ND_PRINT((ndo, "\n\t    QoS Flags [%s]",
                                   bittok2str(clnp_option_qos_global_values,
                                              "none",
                                              *tptr&CLNP_OPTION_OPTION_QOS_MASK)));
                    break;

            case CLNP_OPTION_SECURITY:
                    if (tlen < 2) {
                            ND_PRINT((ndo, ", bad opt len"));
                            return (0);
                    }
                    ND_PRINT((ndo, "\n\t    Format Code: %s, Security-Level %u",
                           tok2str(clnp_option_scope_values,"Reserved",*tptr&CLNP_OPTION_SCOPE_MASK),
                           *(tptr+1)));
                    break;

            case CLNP_OPTION_DISCARD_REASON:
                if (tlen < 1) {
                        ND_PRINT((ndo, ", bad opt len"));
                        return (0);
                }
                rfd_error_major = (*tptr&0xf0) >> 4;
                rfd_error_minor = *tptr&0x0f;
                ND_PRINT((ndo, "\n\t    Class: %s Error (0x%01x), %s (0x%01x)",
                       tok2str(clnp_option_rfd_class_values,"Unknown",rfd_error_major),
                       rfd_error_major,
                       tok2str(clnp_option_rfd_error_class[rfd_error_major],"Unknown",rfd_error_minor),
                       rfd_error_minor));
                break;

            case CLNP_OPTION_PADDING:
                    ND_PRINT((ndo, "padding data"));
                break;

                /*
                 * FIXME those are the defined Options that lack a decoder
                 * you are welcome to contribute code ;-)
                 */

            default:
                print_unknown_data(ndo, tptr, "\n\t  ", opli);
                break;
            }
            if (ndo->ndo_vflag > 1)
                print_unknown_data(ndo, pptr, "\n\t  ", opli);
            pptr += opli;
        }

        switch (clnp_pdu_type) {

        case    CLNP_PDU_ER: /* fall through */
        case 	CLNP_PDU_ERP:
            ND_TCHECK(*pptr);
            if (*(pptr) == NLPID_CLNP) {
                ND_PRINT((ndo, "\n\t-----original packet-----\n\t"));
                /* FIXME recursion protection */
                clnp_print(ndo, pptr, length - clnp_header->length_indicator);
                break;
            }

        case 	CLNP_PDU_DT:
        case 	CLNP_PDU_MD:
        case 	CLNP_PDU_ERQ:

        default:
            /* dump the PDU specific data */
            if (length-(pptr-optr) > 0) {
                ND_PRINT((ndo, "\n\t  undecoded non-header data, length %u", length-clnp_header->length_indicator));
                print_unknown_data(ndo, pptr, "\n\t  ", length - (pptr - optr));
            }
        }

        return (1);

 trunc:
    ND_PRINT((ndo, "[|clnp]"));
    return (1);

}


#define	ESIS_PDU_REDIRECT	6
#define	ESIS_PDU_ESH	        2
#define	ESIS_PDU_ISH	        4

static const struct tok esis_pdu_values[] = {
    { ESIS_PDU_REDIRECT, "redirect"},
    { ESIS_PDU_ESH,      "ESH"},
    { ESIS_PDU_ISH,      "ISH"},
    { 0, NULL }
};

struct esis_header_t {
	uint8_t nlpid;
	uint8_t length_indicator;
	uint8_t version;
	uint8_t reserved;
	uint8_t type;
	uint8_t holdtime[2];
	uint8_t cksum[2];
};

static void
esis_print(netdissect_options *ndo,
           const uint8_t *pptr, u_int length)
{
	const uint8_t *optr;
	u_int li,esis_pdu_type,source_address_length, source_address_number;
	const struct esis_header_t *esis_header;

	if (!ndo->ndo_eflag)
		ND_PRINT((ndo, "ES-IS"));

	if (length <= 2) {
		ND_PRINT((ndo, ndo->ndo_qflag ? "bad pkt!" : "no header at all!"));
		return;
	}

	esis_header = (const struct esis_header_t *) pptr;
        ND_TCHECK(*esis_header);
        li = esis_header->length_indicator;
        optr = pptr;

        /*
         * Sanity checking of the header.
         */

        if (esis_header->nlpid != NLPID_ESIS) {
            ND_PRINT((ndo, " nlpid 0x%02x packet not supported", esis_header->nlpid));
            return;
        }

        if (esis_header->version != ESIS_VERSION) {
            ND_PRINT((ndo, " version %d packet not supported", esis_header->version));
            return;
        }

	if (li > length) {
            ND_PRINT((ndo, " length indicator(%u) > PDU size (%u)!", li, length));
            return;
	}

	if (li < sizeof(struct esis_header_t) + 2) {
            ND_PRINT((ndo, " length indicator %u < min PDU size:", li));
            while (pptr < ndo->ndo_snapend)
                ND_PRINT((ndo, "%02X", *pptr++));
            return;
	}

        esis_pdu_type = esis_header->type & ESIS_PDU_TYPE_MASK;

        if (ndo->ndo_vflag < 1) {
            ND_PRINT((ndo, "%s%s, length %u",
                   ndo->ndo_eflag ? "" : ", ",
                   tok2str(esis_pdu_values,"unknown type (%u)",esis_pdu_type),
                   length));
            return;
        } else
            ND_PRINT((ndo, "%slength %u\n\t%s (%u)",
                   ndo->ndo_eflag ? "" : ", ",
                   length,
                   tok2str(esis_pdu_values,"unknown type: %u", esis_pdu_type),
                   esis_pdu_type));

        ND_PRINT((ndo, ", v: %u%s", esis_header->version, esis_header->version == ESIS_VERSION ? "" : "unsupported" ));
        ND_PRINT((ndo, ", checksum: 0x%04x", EXTRACT_16BITS(esis_header->cksum)));

        osi_print_cksum(ndo, pptr, EXTRACT_16BITS(esis_header->cksum), 7, li);

        ND_PRINT((ndo, ", holding time: %us, length indicator: %u",
                  EXTRACT_16BITS(esis_header->holdtime), li));

        if (ndo->ndo_vflag > 1)
            print_unknown_data(ndo, optr, "\n\t", sizeof(struct esis_header_t));

	pptr += sizeof(struct esis_header_t);
	li -= sizeof(struct esis_header_t);

	switch (esis_pdu_type) {
	case ESIS_PDU_REDIRECT: {
		const uint8_t *dst, *snpa, *neta;
		u_int dstl, snpal, netal;

		ND_TCHECK(*pptr);
		if (li < 1) {
			ND_PRINT((ndo, ", bad redirect/li"));
			return;
		}
		dstl = *pptr;
		pptr++;
		li--;
		ND_TCHECK2(*pptr, dstl);
		if (li < dstl) {
			ND_PRINT((ndo, ", bad redirect/li"));
			return;
		}
		dst = pptr;
		pptr += dstl;
                li -= dstl;
		ND_PRINT((ndo, "\n\t  %s", isonsap_string(ndo, dst, dstl)));

		ND_TCHECK(*pptr);
		if (li < 1) {
			ND_PRINT((ndo, ", bad redirect/li"));
			return;
		}
		snpal = *pptr;
		pptr++;
		li--;
		ND_TCHECK2(*pptr, snpal);
		if (li < snpal) {
			ND_PRINT((ndo, ", bad redirect/li"));
			return;
		}
		snpa = pptr;
		pptr += snpal;
                li -= snpal;
		ND_TCHECK(*pptr);
		if (li < 1) {
			ND_PRINT((ndo, ", bad redirect/li"));
			return;
		}
		netal = *pptr;
		pptr++;
		ND_TCHECK2(*pptr, netal);
		if (li < netal) {
			ND_PRINT((ndo, ", bad redirect/li"));
			return;
		}
		neta = pptr;
		pptr += netal;
                li -= netal;

		if (snpal == 6)
			ND_PRINT((ndo, "\n\t  SNPA (length: %u): %s",
			       snpal,
			       etheraddr_string(ndo, snpa)));
		else
			ND_PRINT((ndo, "\n\t  SNPA (length: %u): %s",
			       snpal,
			       linkaddr_string(ndo, snpa, LINKADDR_OTHER, snpal)));
		if (netal != 0)
			ND_PRINT((ndo, "\n\t  NET (length: %u) %s",
			       netal,
			       isonsap_string(ndo, neta, netal)));
		break;
	}

	case ESIS_PDU_ESH:
            ND_TCHECK(*pptr);
            if (li < 1) {
                ND_PRINT((ndo, ", bad esh/li"));
                return;
            }
            source_address_number = *pptr;
            pptr++;
            li--;

            ND_PRINT((ndo, "\n\t  Number of Source Addresses: %u", source_address_number));

            while (source_address_number > 0) {
                ND_TCHECK(*pptr);
            	if (li < 1) {
                    ND_PRINT((ndo, ", bad esh/li"));
            	    return;
            	}
                source_address_length = *pptr;
                pptr++;
            	li--;

                ND_TCHECK2(*pptr, source_address_length);
            	if (li < source_address_length) {
                    ND_PRINT((ndo, ", bad esh/li"));
            	    return;
            	}
                ND_PRINT((ndo, "\n\t  NET (length: %u): %s",
                       source_address_length,
                       isonsap_string(ndo, pptr, source_address_length)));
                pptr += source_address_length;
                li -= source_address_length;
                source_address_number--;
            }

            break;

	case ESIS_PDU_ISH: {
            ND_TCHECK(*pptr);
            if (li < 1) {
                ND_PRINT((ndo, ", bad ish/li"));
                return;
            }
            source_address_length = *pptr;
            pptr++;
            li--;
            ND_TCHECK2(*pptr, source_address_length);
            if (li < source_address_length) {
                ND_PRINT((ndo, ", bad ish/li"));
                return;
            }
            ND_PRINT((ndo, "\n\t  NET (length: %u): %s", source_address_length, isonsap_string(ndo, pptr, source_address_length)));
            pptr += source_address_length;
            li -= source_address_length;
            break;
	}

	default:
		if (ndo->ndo_vflag <= 1) {
			if (pptr < ndo->ndo_snapend)
				print_unknown_data(ndo, pptr, "\n\t  ", ndo->ndo_snapend - pptr);
		}
		return;
	}

        /* now walk the options */
        while (li != 0) {
            u_int op, opli;
            const uint8_t *tptr;

            if (li < 2) {
                ND_PRINT((ndo, ", bad opts/li"));
                return;
            }
            ND_TCHECK2(*pptr, 2);
            op = *pptr++;
            opli = *pptr++;
            li -= 2;
            if (opli > li) {
                ND_PRINT((ndo, ", opt (%d) too long", op));
                return;
            }
            li -= opli;
            tptr = pptr;

            ND_PRINT((ndo, "\n\t  %s Option #%u, length %u, value: ",
                   tok2str(esis_option_values,"Unknown",op),
                   op,
                   opli));

            switch (op) {

            case ESIS_OPTION_ES_CONF_TIME:
                if (opli == 2) {
                    ND_TCHECK2(*pptr, 2);
                    ND_PRINT((ndo, "%us", EXTRACT_16BITS(tptr)));
                } else
                    ND_PRINT((ndo, "(bad length)"));
                break;

            case ESIS_OPTION_PROTOCOLS:
                while (opli>0) {
                    ND_TCHECK(*tptr);
                    ND_PRINT((ndo, "%s (0x%02x)",
                           tok2str(nlpid_values,
                                   "unknown",
                                   *tptr),
                           *tptr));
                    if (opli>1) /* further NPLIDs ? - put comma */
                        ND_PRINT((ndo, ", "));
                    tptr++;
                    opli--;
                }
                break;

                /*
                 * FIXME those are the defined Options that lack a decoder
                 * you are welcome to contribute code ;-)
                 */

            case ESIS_OPTION_QOS_MAINTENANCE:
            case ESIS_OPTION_SECURITY:
            case ESIS_OPTION_PRIORITY:
            case ESIS_OPTION_ADDRESS_MASK:
            case ESIS_OPTION_SNPA_MASK:

            default:
                print_unknown_data(ndo, tptr, "\n\t  ", opli);
                break;
            }
            if (ndo->ndo_vflag > 1)
                print_unknown_data(ndo, pptr, "\n\t  ", opli);
            pptr += opli;
        }
trunc:
        ND_PRINT((ndo, "[|esis]"));
}

static void
isis_print_mcid(netdissect_options *ndo,
                const struct isis_spb_mcid *mcid)
{
  int i;

  ND_TCHECK(*mcid);
  ND_PRINT((ndo,  "ID: %d, Name: ", mcid->format_id));

  if (fn_printzp(ndo, mcid->name, 32, ndo->ndo_snapend))
    goto trunc;

  ND_PRINT((ndo, "\n\t              Lvl: %d", EXTRACT_16BITS(mcid->revision_lvl)));

  ND_PRINT((ndo,  ", Digest: "));

  for(i=0;i<16;i++)
    ND_PRINT((ndo, "%.2x ", mcid->digest[i]));

trunc:
  ND_PRINT((ndo, "%s", tstr));
}

static int
isis_print_mt_port_cap_subtlv(netdissect_options *ndo,
                              const uint8_t *tptr, int len)
{
  int stlv_type, stlv_len;
  const struct isis_subtlv_spb_mcid *subtlv_spb_mcid;
  int i;

  while (len > 2)
  {
    ND_TCHECK2(*tptr, 2);
    stlv_type = *(tptr++);
    stlv_len  = *(tptr++);

    /* first lets see if we know the subTLVs name*/
    ND_PRINT((ndo, "\n\t       %s subTLV #%u, length: %u",
               tok2str(isis_mt_port_cap_subtlv_values, "unknown", stlv_type),
               stlv_type,
               stlv_len));

    /*len -= TLV_TYPE_LEN_OFFSET;*/
    len = len -2;

    /* Make sure the subTLV fits within the space left */
    if (len < stlv_len)
      goto trunc;
    /* Make sure the entire subTLV is in the captured data */
    ND_TCHECK2(*(tptr), stlv_len);

    switch (stlv_type)
    {
      case ISIS_SUBTLV_SPB_MCID:
      {
      	if (stlv_len < ISIS_SUBTLV_SPB_MCID_MIN_LEN)
      	  goto trunc;

        subtlv_spb_mcid = (const struct isis_subtlv_spb_mcid *)tptr;

        ND_PRINT((ndo,  "\n\t         MCID: "));
        isis_print_mcid(ndo, &(subtlv_spb_mcid->mcid));

          /*tptr += SPB_MCID_MIN_LEN;
            len -= SPB_MCID_MIN_LEN; */

        ND_PRINT((ndo,  "\n\t         AUX-MCID: "));
        isis_print_mcid(ndo, &(subtlv_spb_mcid->aux_mcid));

          /*tptr += SPB_MCID_MIN_LEN;
            len -= SPB_MCID_MIN_LEN; */
        tptr = tptr + ISIS_SUBTLV_SPB_MCID_MIN_LEN;
        len = len - ISIS_SUBTLV_SPB_MCID_MIN_LEN;
        stlv_len = stlv_len - ISIS_SUBTLV_SPB_MCID_MIN_LEN;

        break;
      }

      case ISIS_SUBTLV_SPB_DIGEST:
      {
        if (stlv_len < ISIS_SUBTLV_SPB_DIGEST_MIN_LEN)
          goto trunc;

        ND_PRINT((ndo, "\n\t        RES: %d V: %d A: %d D: %d",
                        (*(tptr) >> 5), (((*tptr)>> 4) & 0x01),
                        ((*(tptr) >> 2) & 0x03), ((*tptr) & 0x03)));

        tptr++;

        ND_PRINT((ndo,  "\n\t         Digest: "));

        for(i=1;i<=8; i++)
        {
            ND_PRINT((ndo, "%08x ", EXTRACT_32BITS(tptr)));
            if (i%4 == 0 && i != 8)
              ND_PRINT((ndo, "\n\t                 "));
            tptr = tptr + 4;
        }

        len = len - ISIS_SUBTLV_SPB_DIGEST_MIN_LEN;
        stlv_len = stlv_len - ISIS_SUBTLV_SPB_DIGEST_MIN_LEN;

        break;
      }

      case ISIS_SUBTLV_SPB_BVID:
      {
        while (stlv_len >= ISIS_SUBTLV_SPB_BVID_MIN_LEN)
        {
          ND_PRINT((ndo, "\n\t           ECT: %08x",
                      EXTRACT_32BITS(tptr)));

          tptr = tptr+4;

          ND_PRINT((ndo, " BVID: %d, U:%01x M:%01x ",
                     (EXTRACT_16BITS (tptr) >> 4) ,
                     (EXTRACT_16BITS (tptr) >> 3) & 0x01,
                     (EXTRACT_16BITS (tptr) >> 2) & 0x01));

          tptr = tptr + 2;
          len = len - ISIS_SUBTLV_SPB_BVID_MIN_LEN;
          stlv_len = stlv_len - ISIS_SUBTLV_SPB_BVID_MIN_LEN;
        }

        break;
      }

      default:
        break;
    }
    tptr += stlv_len;
    len -= stlv_len;
  }

  return 0;

  trunc:
    ND_PRINT((ndo, "\n\t\t"));
    ND_PRINT((ndo, "%s", tstr));
    return(1);
}

static int
isis_print_mt_capability_subtlv(netdissect_options *ndo,
                                const uint8_t *tptr, int len)
{
  int stlv_type, stlv_len, tmp;

  while (len > 2)
  {
    ND_TCHECK2(*tptr, 2);
    stlv_type = *(tptr++);
    stlv_len  = *(tptr++);

    /* first lets see if we know the subTLVs name*/
    ND_PRINT((ndo, "\n\t      %s subTLV #%u, length: %u",
               tok2str(isis_mt_capability_subtlv_values, "unknown", stlv_type),
               stlv_type,
               stlv_len));

    len = len - 2;

    /* Make sure the subTLV fits within the space left */
    if (len < stlv_len)
      goto trunc;
    /* Make sure the entire subTLV is in the captured data */
    ND_TCHECK2(*(tptr), stlv_len);

    switch (stlv_type)
    {
      case ISIS_SUBTLV_SPB_INSTANCE:
          if (stlv_len < ISIS_SUBTLV_SPB_INSTANCE_MIN_LEN)
            goto trunc;

          ND_PRINT((ndo, "\n\t        CIST Root-ID: %08x", EXTRACT_32BITS(tptr)));
          tptr = tptr+4;
          ND_PRINT((ndo, " %08x", EXTRACT_32BITS(tptr)));
          tptr = tptr+4;
          ND_PRINT((ndo, ", Path Cost: %08x", EXTRACT_32BITS(tptr)));
          tptr = tptr+4;
          ND_PRINT((ndo, ", Prio: %d", EXTRACT_16BITS(tptr)));
          tptr = tptr + 2;
          ND_PRINT((ndo, "\n\t        RES: %d",
                    EXTRACT_16BITS(tptr) >> 5));
          ND_PRINT((ndo, ", V: %d",
                    (EXTRACT_16BITS(tptr) >> 4) & 0x0001));
          ND_PRINT((ndo, ", SPSource-ID: %d",
                    (EXTRACT_32BITS(tptr) & 0x000fffff)));
          tptr = tptr+4;
          ND_PRINT((ndo, ", No of Trees: %x", *(tptr)));

          tmp = *(tptr++);

          len = len - ISIS_SUBTLV_SPB_INSTANCE_MIN_LEN;
          stlv_len = stlv_len - ISIS_SUBTLV_SPB_INSTANCE_MIN_LEN;

          while (tmp)
          {
            if (stlv_len < ISIS_SUBTLV_SPB_INSTANCE_VLAN_TUPLE_LEN)
              goto trunc;

            ND_PRINT((ndo, "\n\t         U:%d, M:%d, A:%d, RES:%d",
                      *(tptr) >> 7, (*(tptr) >> 6) & 0x01,
                      (*(tptr) >> 5) & 0x01, (*(tptr) & 0x1f)));

            tptr++;

            ND_PRINT((ndo, ", ECT: %08x", EXTRACT_32BITS(tptr)));

            tptr = tptr + 4;

            ND_PRINT((ndo, ", BVID: %d, SPVID: %d",
                      (EXTRACT_24BITS(tptr) >> 12) & 0x000fff,
                      EXTRACT_24BITS(tptr) & 0x000fff));

            tptr = tptr + 3;
            len = len - ISIS_SUBTLV_SPB_INSTANCE_VLAN_TUPLE_LEN;
            stlv_len = stlv_len - ISIS_SUBTLV_SPB_INSTANCE_VLAN_TUPLE_LEN;
            tmp--;
          }

          break;

      case ISIS_SUBTLV_SPBM_SI:
          if (stlv_len < 8)
            goto trunc;

          ND_PRINT((ndo, "\n\t        BMAC: %08x", EXTRACT_32BITS(tptr)));
          tptr = tptr+4;
          ND_PRINT((ndo, "%04x", EXTRACT_16BITS(tptr)));
          tptr = tptr+2;

          ND_PRINT((ndo, ", RES: %d, VID: %d", EXTRACT_16BITS(tptr) >> 12,
                    (EXTRACT_16BITS(tptr)) & 0x0fff));

          tptr = tptr+2;
          len = len - 8;
          stlv_len = stlv_len - 8;

          while (stlv_len >= 4) {
            ND_TCHECK2(*tptr, 4);
            ND_PRINT((ndo, "\n\t        T: %d, R: %d, RES: %d, ISID: %d",
                    (EXTRACT_32BITS(tptr) >> 31),
                    (EXTRACT_32BITS(tptr) >> 30) & 0x01,
                    (EXTRACT_32BITS(tptr) >> 24) & 0x03f,
                    (EXTRACT_32BITS(tptr)) & 0x0ffffff));

            tptr = tptr + 4;
            len = len - 4;
            stlv_len = stlv_len - 4;
          }

        break;

      default:
        break;
    }
    tptr += stlv_len;
    len -= stlv_len;
  }
  return 0;

  trunc:
    ND_PRINT((ndo, "\n\t\t"));
    ND_PRINT((ndo, "%s", tstr));
    return(1);
}

/* shared routine for printing system, node and lsp-ids */
static char *
isis_print_id(const uint8_t *cp, int id_len)
{
    int i;
    static char id[sizeof("xxxx.xxxx.xxxx.yy-zz")];
    char *pos = id;
    int sysid_len;

    sysid_len = SYSTEM_ID_LEN;
    if (sysid_len > id_len)
        sysid_len = id_len;
    for (i = 1; i <= sysid_len; i++) {
        snprintf(pos, sizeof(id) - (pos - id), "%02x", *cp++);
	pos += strlen(pos);
	if (i == 2 || i == 4)
	    *pos++ = '.';
	}
    if (id_len >= NODE_ID_LEN) {
        snprintf(pos, sizeof(id) - (pos - id), ".%02x", *cp++);
	pos += strlen(pos);
    }
    if (id_len == LSP_ID_LEN)
        snprintf(pos, sizeof(id) - (pos - id), "-%02x", *cp);
    return (id);
}

/* print the 4-byte metric block which is common found in the old-style TLVs */
static int
isis_print_metric_block(netdissect_options *ndo,
                        const struct isis_metric_block *isis_metric_block)
{
    ND_PRINT((ndo, ", Default Metric: %d, %s",
           ISIS_LSP_TLV_METRIC_VALUE(isis_metric_block->metric_default),
           ISIS_LSP_TLV_METRIC_IE(isis_metric_block->metric_default) ? "External" : "Internal"));
    if (!ISIS_LSP_TLV_METRIC_SUPPORTED(isis_metric_block->metric_delay))
        ND_PRINT((ndo, "\n\t\t  Delay Metric: %d, %s",
               ISIS_LSP_TLV_METRIC_VALUE(isis_metric_block->metric_delay),
               ISIS_LSP_TLV_METRIC_IE(isis_metric_block->metric_delay) ? "External" : "Internal"));
    if (!ISIS_LSP_TLV_METRIC_SUPPORTED(isis_metric_block->metric_expense))
        ND_PRINT((ndo, "\n\t\t  Expense Metric: %d, %s",
               ISIS_LSP_TLV_METRIC_VALUE(isis_metric_block->metric_expense),
               ISIS_LSP_TLV_METRIC_IE(isis_metric_block->metric_expense) ? "External" : "Internal"));
    if (!ISIS_LSP_TLV_METRIC_SUPPORTED(isis_metric_block->metric_error))
        ND_PRINT((ndo, "\n\t\t  Error Metric: %d, %s",
               ISIS_LSP_TLV_METRIC_VALUE(isis_metric_block->metric_error),
               ISIS_LSP_TLV_METRIC_IE(isis_metric_block->metric_error) ? "External" : "Internal"));

    return(1); /* everything is ok */
}

static int
isis_print_tlv_ip_reach(netdissect_options *ndo,
                        const uint8_t *cp, const char *ident, int length)
{
	int prefix_len;
	const struct isis_tlv_ip_reach *tlv_ip_reach;

	tlv_ip_reach = (const struct isis_tlv_ip_reach *)cp;

	while (length > 0) {
		if ((size_t)length < sizeof(*tlv_ip_reach)) {
			ND_PRINT((ndo, "short IPv4 Reachability (%d vs %lu)",
                               length,
                               (unsigned long)sizeof(*tlv_ip_reach)));
			return (0);
		}

		if (!ND_TTEST(*tlv_ip_reach))
		    return (0);

		prefix_len = mask2plen(EXTRACT_32BITS(tlv_ip_reach->mask));

		if (prefix_len == -1)
			ND_PRINT((ndo, "%sIPv4 prefix: %s mask %s",
                               ident,
			       ipaddr_string(ndo, (tlv_ip_reach->prefix)),
			       ipaddr_string(ndo, (tlv_ip_reach->mask))));
		else
			ND_PRINT((ndo, "%sIPv4 prefix: %15s/%u",
                               ident,
			       ipaddr_string(ndo, (tlv_ip_reach->prefix)),
			       prefix_len));

		ND_PRINT((ndo, ", Distribution: %s, Metric: %u, %s",
                       ISIS_LSP_TLV_METRIC_UPDOWN(tlv_ip_reach->isis_metric_block.metric_default) ? "down" : "up",
                       ISIS_LSP_TLV_METRIC_VALUE(tlv_ip_reach->isis_metric_block.metric_default),
                       ISIS_LSP_TLV_METRIC_IE(tlv_ip_reach->isis_metric_block.metric_default) ? "External" : "Internal"));

		if (!ISIS_LSP_TLV_METRIC_SUPPORTED(tlv_ip_reach->isis_metric_block.metric_delay))
                    ND_PRINT((ndo, "%s  Delay Metric: %u, %s",
                           ident,
                           ISIS_LSP_TLV_METRIC_VALUE(tlv_ip_reach->isis_metric_block.metric_delay),
                           ISIS_LSP_TLV_METRIC_IE(tlv_ip_reach->isis_metric_block.metric_delay) ? "External" : "Internal"));

		if (!ISIS_LSP_TLV_METRIC_SUPPORTED(tlv_ip_reach->isis_metric_block.metric_expense))
                    ND_PRINT((ndo, "%s  Expense Metric: %u, %s",
                           ident,
                           ISIS_LSP_TLV_METRIC_VALUE(tlv_ip_reach->isis_metric_block.metric_expense),
                           ISIS_LSP_TLV_METRIC_IE(tlv_ip_reach->isis_metric_block.metric_expense) ? "External" : "Internal"));

		if (!ISIS_LSP_TLV_METRIC_SUPPORTED(tlv_ip_reach->isis_metric_block.metric_error))
                    ND_PRINT((ndo, "%s  Error Metric: %u, %s",
                           ident,
                           ISIS_LSP_TLV_METRIC_VALUE(tlv_ip_reach->isis_metric_block.metric_error),
                           ISIS_LSP_TLV_METRIC_IE(tlv_ip_reach->isis_metric_block.metric_error) ? "External" : "Internal"));

		length -= sizeof(struct isis_tlv_ip_reach);
		tlv_ip_reach++;
	}
	return (1);
}

/*
 * this is the common IP-REACH subTLV decoder it is called
 * from various EXTD-IP REACH TLVs (135,235,236,237)
 */

static int
isis_print_ip_reach_subtlv(netdissect_options *ndo,
                           const uint8_t *tptr, int subt, int subl,
                           const char *ident)
{
    /* first lets see if we know the subTLVs name*/
    ND_PRINT((ndo, "%s%s subTLV #%u, length: %u",
              ident, tok2str(isis_ext_ip_reach_subtlv_values, "unknown", subt),
              subt, subl));

    ND_TCHECK2(*tptr,subl);

    switch(subt) {
    case ISIS_SUBTLV_EXTD_IP_REACH_MGMT_PREFIX_COLOR: /* fall through */
    case ISIS_SUBTLV_EXTD_IP_REACH_ADMIN_TAG32:
        while (subl >= 4) {
	    ND_PRINT((ndo, ", 0x%08x (=%u)",
		   EXTRACT_32BITS(tptr),
		   EXTRACT_32BITS(tptr)));
	    tptr+=4;
	    subl-=4;
	}
	break;
    case ISIS_SUBTLV_EXTD_IP_REACH_ADMIN_TAG64:
        while (subl >= 8) {
	    ND_PRINT((ndo, ", 0x%08x%08x",
		   EXTRACT_32BITS(tptr),
		   EXTRACT_32BITS(tptr+4)));
	    tptr+=8;
	    subl-=8;
	}
	break;
    default:
	if (!print_unknown_data(ndo, tptr, "\n\t\t    ", subl))
	  return(0);
	break;
    }
    return(1);

trunc:
    ND_PRINT((ndo, "%s", ident));
    ND_PRINT((ndo, "%s", tstr));
    return(0);
}

/*
 * this is the common IS-REACH subTLV decoder it is called
 * from isis_print_ext_is_reach()
 */

static int
isis_print_is_reach_subtlv(netdissect_options *ndo,
                           const uint8_t *tptr, u_int subt, u_int subl,
                           const char *ident)
{
        u_int te_class,priority_level,gmpls_switch_cap;
        union { /* int to float conversion buffer for several subTLVs */
            float f;
            uint32_t i;
        } bw;

        /* first lets see if we know the subTLVs name*/
	ND_PRINT((ndo, "%s%s subTLV #%u, length: %u",
	          ident, tok2str(isis_ext_is_reach_subtlv_values, "unknown", subt),
	          subt, subl));

	ND_TCHECK2(*tptr, subl);

        switch(subt) {
        case ISIS_SUBTLV_EXT_IS_REACH_ADMIN_GROUP:
        case ISIS_SUBTLV_EXT_IS_REACH_LINK_LOCAL_REMOTE_ID:
        case ISIS_SUBTLV_EXT_IS_REACH_LINK_REMOTE_ID:
	    if (subl >= 4) {
	      ND_PRINT((ndo, ", 0x%08x", EXTRACT_32BITS(tptr)));
	      if (subl == 8) /* rfc4205 */
	        ND_PRINT((ndo, ", 0x%08x", EXTRACT_32BITS(tptr+4)));
	    }
	    break;
        case ISIS_SUBTLV_EXT_IS_REACH_IPV4_INTF_ADDR:
        case ISIS_SUBTLV_EXT_IS_REACH_IPV4_NEIGHBOR_ADDR:
            if (subl >= sizeof(struct in_addr))
              ND_PRINT((ndo, ", %s", ipaddr_string(ndo, tptr)));
            break;
        case ISIS_SUBTLV_EXT_IS_REACH_MAX_LINK_BW :
	case ISIS_SUBTLV_EXT_IS_REACH_RESERVABLE_BW:
            if (subl >= 4) {
              bw.i = EXTRACT_32BITS(tptr);
              ND_PRINT((ndo, ", %.3f Mbps", bw.f * 8 / 1000000));
            }
            break;
        case ISIS_SUBTLV_EXT_IS_REACH_UNRESERVED_BW :
            if (subl >= 32) {
              for (te_class = 0; te_class < 8; te_class++) {
                bw.i = EXTRACT_32BITS(tptr);
                ND_PRINT((ndo, "%s  TE-Class %u: %.3f Mbps",
                       ident,
                       te_class,
                       bw.f * 8 / 1000000));
		tptr+=4;
	      }
            }
            break;
        case ISIS_SUBTLV_EXT_IS_REACH_BW_CONSTRAINTS: /* fall through */
        case ISIS_SUBTLV_EXT_IS_REACH_BW_CONSTRAINTS_OLD:
            if (subl == 0)
                break;
            ND_PRINT((ndo, "%sBandwidth Constraints Model ID: %s (%u)",
                   ident,
                   tok2str(diffserv_te_bc_values, "unknown", *tptr),
                   *tptr));
            tptr++;
            /* decode BCs until the subTLV ends */
            for (te_class = 0; te_class < (subl-1)/4; te_class++) {
                bw.i = EXTRACT_32BITS(tptr);
                ND_PRINT((ndo, "%s  Bandwidth constraint CT%u: %.3f Mbps",
                       ident,
                       te_class,
                       bw.f * 8 / 1000000));
		tptr+=4;
            }
            break;
        case ISIS_SUBTLV_EXT_IS_REACH_TE_METRIC:
            if (subl >= 3)
              ND_PRINT((ndo, ", %u", EXTRACT_24BITS(tptr)));
            break;
        case ISIS_SUBTLV_EXT_IS_REACH_LINK_ATTRIBUTE:
            if (subl == 2) {
               ND_PRINT((ndo, ", [ %s ] (0x%04x)",
                      bittok2str(isis_subtlv_link_attribute_values,
                                 "Unknown",
                                 EXTRACT_16BITS(tptr)),
                      EXTRACT_16BITS(tptr)));
            }
            break;
        case ISIS_SUBTLV_EXT_IS_REACH_LINK_PROTECTION_TYPE:
            if (subl >= 2) {
              ND_PRINT((ndo, ", %s, Priority %u",
		   bittok2str(gmpls_link_prot_values, "none", *tptr),
                   *(tptr+1)));
            }
            break;
        case ISIS_SUBTLV_SPB_METRIC:
            if (subl >= 6) {
              ND_PRINT((ndo, ", LM: %u", EXTRACT_24BITS(tptr)));
              tptr=tptr+3;
              ND_PRINT((ndo, ", P: %u", *(tptr)));
              tptr++;
              ND_PRINT((ndo, ", P-ID: %u", EXTRACT_16BITS(tptr)));
            }
            break;
        case ISIS_SUBTLV_EXT_IS_REACH_INTF_SW_CAP_DESCR:
            if (subl >= 36) {
              gmpls_switch_cap = *tptr;
              ND_PRINT((ndo, "%s  Interface Switching Capability:%s",
                   ident,
                   tok2str(gmpls_switch_cap_values, "Unknown", gmpls_switch_cap)));
              ND_PRINT((ndo, ", LSP Encoding: %s",
                   tok2str(gmpls_encoding_values, "Unknown", *(tptr + 1))));
	      tptr+=4;
              ND_PRINT((ndo, "%s  Max LSP Bandwidth:", ident));
              for (priority_level = 0; priority_level < 8; priority_level++) {
                bw.i = EXTRACT_32BITS(tptr);
                ND_PRINT((ndo, "%s    priority level %d: %.3f Mbps",
                       ident,
                       priority_level,
                       bw.f * 8 / 1000000));
		tptr+=4;
              }
              subl-=36;
              switch (gmpls_switch_cap) {
              case GMPLS_PSC1:
              case GMPLS_PSC2:
              case GMPLS_PSC3:
              case GMPLS_PSC4:
                if (subl < 6)
                    break;
                bw.i = EXTRACT_32BITS(tptr);
                ND_PRINT((ndo, "%s  Min LSP Bandwidth: %.3f Mbps", ident, bw.f * 8 / 1000000));
                ND_PRINT((ndo, "%s  Interface MTU: %u", ident, EXTRACT_16BITS(tptr + 4)));
                break;
              case GMPLS_TSC:
                if (subl < 8)
                    break;
                bw.i = EXTRACT_32BITS(tptr);
                ND_PRINT((ndo, "%s  Min LSP Bandwidth: %.3f Mbps", ident, bw.f * 8 / 1000000));
                ND_PRINT((ndo, "%s  Indication %s", ident,
                       tok2str(gmpls_switch_cap_tsc_indication_values, "Unknown (%u)", *(tptr + 4))));
                break;
              default:
                /* there is some optional stuff left to decode but this is as of yet
                   not specified so just lets hexdump what is left */
                if(subl>0){
                  if (!print_unknown_data(ndo, tptr, "\n\t\t    ", subl))
                    return(0);
                }
              }
            }
            break;
        default:
            if (!print_unknown_data(ndo, tptr, "\n\t\t    ", subl))
                return(0);
            break;
        }
        return(1);

trunc:
    return(0);
}

/*
 * this is the common IS-REACH decoder it is called
 * from various EXTD-IS REACH style TLVs (22,24,222)
 */

static int
isis_print_ext_is_reach(netdissect_options *ndo,
                        const uint8_t *tptr, const char *ident, int tlv_type)
{
    char ident_buffer[20];
    int subtlv_type,subtlv_len,subtlv_sum_len;
    int proc_bytes = 0; /* how many bytes did we process ? */

    if (!ND_TTEST2(*tptr, NODE_ID_LEN))
        return(0);

    ND_PRINT((ndo, "%sIS Neighbor: %s", ident, isis_print_id(tptr, NODE_ID_LEN)));
    tptr+=(NODE_ID_LEN);

    if (tlv_type != ISIS_TLV_IS_ALIAS_ID) { /* the Alias TLV Metric field is implicit 0 */
        if (!ND_TTEST2(*tptr, 3))    /* and is therefore skipped */
	    return(0);
	ND_PRINT((ndo, ", Metric: %d", EXTRACT_24BITS(tptr)));
	tptr+=3;
    }

    if (!ND_TTEST2(*tptr, 1))
        return(0);
    subtlv_sum_len=*(tptr++); /* read out subTLV length */
    proc_bytes=NODE_ID_LEN+3+1;
    ND_PRINT((ndo, ", %ssub-TLVs present",subtlv_sum_len ? "" : "no "));
    if (subtlv_sum_len) {
        ND_PRINT((ndo, " (%u)", subtlv_sum_len));
        while (subtlv_sum_len>0) {
            if (!ND_TTEST2(*tptr,2))
                return(0);
            subtlv_type=*(tptr++);
            subtlv_len=*(tptr++);
            /* prepend the indent string */
            snprintf(ident_buffer, sizeof(ident_buffer), "%s  ",ident);
            if (!isis_print_is_reach_subtlv(ndo, tptr, subtlv_type, subtlv_len, ident_buffer))
                return(0);
            tptr+=subtlv_len;
            subtlv_sum_len-=(subtlv_len+2);
            proc_bytes+=(subtlv_len+2);
        }
    }
    return(proc_bytes);
}

/*
 * this is the common Multi Topology ID decoder
 * it is called from various MT-TLVs (222,229,235,237)
 */

static int
isis_print_mtid(netdissect_options *ndo,
                const uint8_t *tptr, const char *ident)
{
    if (!ND_TTEST2(*tptr, 2))
        return(0);

    ND_PRINT((ndo, "%s%s",
           ident,
           tok2str(isis_mt_values,
                   "Reserved for IETF Consensus",
                   ISIS_MASK_MTID(EXTRACT_16BITS(tptr)))));

    ND_PRINT((ndo, " Topology (0x%03x), Flags: [%s]",
           ISIS_MASK_MTID(EXTRACT_16BITS(tptr)),
           bittok2str(isis_mt_flag_values, "none",ISIS_MASK_MTFLAGS(EXTRACT_16BITS(tptr)))));

    return(2);
}

/*
 * this is the common extended IP reach decoder
 * it is called from TLVs (135,235,236,237)
 * we process the TLV and optional subTLVs and return
 * the amount of processed bytes
 */

static int
isis_print_extd_ip_reach(netdissect_options *ndo,
                         const uint8_t *tptr, const char *ident, uint16_t afi)
{
    char ident_buffer[20];
    uint8_t prefix[sizeof(struct in6_addr)]; /* shared copy buffer for IPv4 and IPv6 prefixes */
    u_int metric, status_byte, bit_length, byte_length, sublen, processed, subtlvtype, subtlvlen;

    if (!ND_TTEST2(*tptr, 4))
        return (0);
    metric = EXTRACT_32BITS(tptr);
    processed=4;
    tptr+=4;

    if (afi == AF_INET) {
        if (!ND_TTEST2(*tptr, 1)) /* fetch status byte */
            return (0);
        status_byte=*(tptr++);
        bit_length = status_byte&0x3f;
        if (bit_length > 32) {
            ND_PRINT((ndo, "%sIPv4 prefix: bad bit length %u",
                   ident,
                   bit_length));
            return (0);
        }
        processed++;
    } else if (afi == AF_INET6) {
        if (!ND_TTEST2(*tptr, 2)) /* fetch status & prefix_len byte */
            return (0);
        status_byte=*(tptr++);
        bit_length=*(tptr++);
        if (bit_length > 128) {
            ND_PRINT((ndo, "%sIPv6 prefix: bad bit length %u",
                   ident,
                   bit_length));
            return (0);
        }
        processed+=2;
    } else
        return (0); /* somebody is fooling us */

    byte_length = (bit_length + 7) / 8; /* prefix has variable length encoding */

    if (!ND_TTEST2(*tptr, byte_length))
        return (0);
    memset(prefix, 0, sizeof prefix);   /* clear the copy buffer */
    memcpy(prefix,tptr,byte_length);    /* copy as much as is stored in the TLV */
    tptr+=byte_length;
    processed+=byte_length;

    if (afi == AF_INET)
        ND_PRINT((ndo, "%sIPv4 prefix: %15s/%u",
               ident,
               ipaddr_string(ndo, prefix),
               bit_length));
    else if (afi == AF_INET6)
        ND_PRINT((ndo, "%sIPv6 prefix: %s/%u",
               ident,
               ip6addr_string(ndo, prefix),
               bit_length));

    ND_PRINT((ndo, ", Distribution: %s, Metric: %u",
           ISIS_MASK_TLV_EXTD_IP_UPDOWN(status_byte) ? "down" : "up",
           metric));

    if (afi == AF_INET && ISIS_MASK_TLV_EXTD_IP_SUBTLV(status_byte))
        ND_PRINT((ndo, ", sub-TLVs present"));
    else if (afi == AF_INET6)
        ND_PRINT((ndo, ", %s%s",
               ISIS_MASK_TLV_EXTD_IP6_IE(status_byte) ? "External" : "Internal",
               ISIS_MASK_TLV_EXTD_IP6_SUBTLV(status_byte) ? ", sub-TLVs present" : ""));

    if ((afi == AF_INET  && ISIS_MASK_TLV_EXTD_IP_SUBTLV(status_byte))
     || (afi == AF_INET6 && ISIS_MASK_TLV_EXTD_IP6_SUBTLV(status_byte))
	) {
        /* assume that one prefix can hold more
           than one subTLV - therefore the first byte must reflect
           the aggregate bytecount of the subTLVs for this prefix
        */
        if (!ND_TTEST2(*tptr, 1))
            return (0);
        sublen=*(tptr++);
        processed+=sublen+1;
        ND_PRINT((ndo, " (%u)", sublen));   /* print out subTLV length */

        while (sublen>0) {
            if (!ND_TTEST2(*tptr,2))
                return (0);
            subtlvtype=*(tptr++);
            subtlvlen=*(tptr++);
            /* prepend the indent string */
            snprintf(ident_buffer, sizeof(ident_buffer), "%s  ",ident);
            if (!isis_print_ip_reach_subtlv(ndo, tptr, subtlvtype, subtlvlen, ident_buffer))
                return(0);
            tptr+=subtlvlen;
            sublen-=(subtlvlen+2);
        }
    }
    return (processed);
}

/*
 * Clear checksum and lifetime prior to signature verification.
 */
static void
isis_clear_checksum_lifetime(void *header)
{
    struct isis_lsp_header *header_lsp = (struct isis_lsp_header *) header;

    header_lsp->checksum[0] = 0;
    header_lsp->checksum[1] = 0;
    header_lsp->remaining_lifetime[0] = 0;
    header_lsp->remaining_lifetime[1] = 0;
}

/*
 * isis_print
 * Decode IS-IS packets.  Return 0 on error.
 */

static int
isis_print(netdissect_options *ndo,
           const uint8_t *p, u_int length)
{
    const struct isis_common_header *isis_header;

    const struct isis_iih_lan_header *header_iih_lan;
    const struct isis_iih_ptp_header *header_iih_ptp;
    const struct isis_lsp_header *header_lsp;
    const struct isis_csnp_header *header_csnp;
    const struct isis_psnp_header *header_psnp;

    const struct isis_tlv_lsp *tlv_lsp;
    const struct isis_tlv_ptp_adj *tlv_ptp_adj;
    const struct isis_tlv_is_reach *tlv_is_reach;
    const struct isis_tlv_es_reach *tlv_es_reach;

    uint8_t pdu_type, max_area, id_length, tlv_type, tlv_len, tmp, alen, lan_alen, prefix_len;
    uint8_t ext_is_len, ext_ip_len, mt_len;
    const uint8_t *optr, *pptr, *tptr;
    u_short packet_len,pdu_len, key_id;
    u_int i,vendor_id;
    int sigcheck;

    packet_len=length;
    optr = p; /* initialize the _o_riginal pointer to the packet start -
                 need it for parsing the checksum TLV and authentication
                 TLV verification */
    isis_header = (const struct isis_common_header *)p;
    ND_TCHECK(*isis_header);
    if (length < ISIS_COMMON_HEADER_SIZE)
        goto trunc;
    pptr = p+(ISIS_COMMON_HEADER_SIZE);
    header_iih_lan = (const struct isis_iih_lan_header *)pptr;
    header_iih_ptp = (const struct isis_iih_ptp_header *)pptr;
    header_lsp = (const struct isis_lsp_header *)pptr;
    header_csnp = (const struct isis_csnp_header *)pptr;
    header_psnp = (const struct isis_psnp_header *)pptr;

    if (!ndo->ndo_eflag)
        ND_PRINT((ndo, "IS-IS"));

    /*
     * Sanity checking of the header.
     */

    if (isis_header->version != ISIS_VERSION) {
	ND_PRINT((ndo, "version %d packet not supported", isis_header->version));
	return (0);
    }

    if ((isis_header->id_length != SYSTEM_ID_LEN) && (isis_header->id_length != 0)) {
	ND_PRINT((ndo, "system ID length of %d is not supported",
	       isis_header->id_length));
	return (0);
    }

    if (isis_header->pdu_version != ISIS_VERSION) {
	ND_PRINT((ndo, "version %d packet not supported", isis_header->pdu_version));
	return (0);
    }

    if (length < isis_header->fixed_len) {
	ND_PRINT((ndo, "fixed header length %u > packet length %u", isis_header->fixed_len, length));
	return (0);
    }

    if (isis_header->fixed_len < ISIS_COMMON_HEADER_SIZE) {
	ND_PRINT((ndo, "fixed header length %u < minimum header size %u", isis_header->fixed_len, (u_int)ISIS_COMMON_HEADER_SIZE));
	return (0);
    }

    max_area = isis_header->max_area;
    switch(max_area) {
    case 0:
	max_area = 3;	 /* silly shit */
	break;
    case 255:
	ND_PRINT((ndo, "bad packet -- 255 areas"));
	return (0);
    default:
	break;
    }

    id_length = isis_header->id_length;
    switch(id_length) {
    case 0:
        id_length = 6;	 /* silly shit again */
	break;
    case 1:              /* 1-8 are valid sys-ID lenghts */
    case 2:
    case 3:
    case 4:
    case 5:
    case 6:
    case 7:
    case 8:
        break;
    case 255:
        id_length = 0;   /* entirely useless */
	break;
    default:
        break;
    }

    /* toss any non 6-byte sys-ID len PDUs */
    if (id_length != 6 ) {
	ND_PRINT((ndo, "bad packet -- illegal sys-ID length (%u)", id_length));
	return (0);
    }

    pdu_type=isis_header->pdu_type;

    /* in non-verbose mode print the basic PDU Type plus PDU specific brief information*/
    if (ndo->ndo_vflag == 0) {
        ND_PRINT((ndo, "%s%s",
               ndo->ndo_eflag ? "" : ", ",
               tok2str(isis_pdu_values, "unknown PDU-Type %u", pdu_type)));
    } else {
        /* ok they seem to want to know everything - lets fully decode it */
        ND_PRINT((ndo, "%slength %u", ndo->ndo_eflag ? "" : ", ", length));

        ND_PRINT((ndo, "\n\t%s, hlen: %u, v: %u, pdu-v: %u, sys-id-len: %u (%u), max-area: %u (%u)",
               tok2str(isis_pdu_values,
                       "unknown, type %u",
                       pdu_type),
               isis_header->fixed_len,
               isis_header->version,
               isis_header->pdu_version,
               id_length,
               isis_header->id_length,
               max_area,
               isis_header->max_area));

        if (ndo->ndo_vflag > 1) {
            if (!print_unknown_data(ndo, optr, "\n\t", 8)) /* provide the _o_riginal pointer */
                return (0);                         /* for optionally debugging the common header */
        }
    }

    switch (pdu_type) {

    case ISIS_PDU_L1_LAN_IIH:
    case ISIS_PDU_L2_LAN_IIH:
        if (isis_header->fixed_len != (ISIS_COMMON_HEADER_SIZE+ISIS_IIH_LAN_HEADER_SIZE)) {
            ND_PRINT((ndo, ", bogus fixed header length %u should be %lu",
                     isis_header->fixed_len, (unsigned long)(ISIS_COMMON_HEADER_SIZE+ISIS_IIH_LAN_HEADER_SIZE)));
            return (0);
        }
        ND_TCHECK(*header_iih_lan);
        if (length < ISIS_COMMON_HEADER_SIZE+ISIS_IIH_LAN_HEADER_SIZE)
            goto trunc;
        if (ndo->ndo_vflag == 0) {
            ND_PRINT((ndo, ", src-id %s",
                      isis_print_id(header_iih_lan->source_id, SYSTEM_ID_LEN)));
            ND_PRINT((ndo, ", lan-id %s, prio %u",
                      isis_print_id(header_iih_lan->lan_id,NODE_ID_LEN),
                      header_iih_lan->priority));
            ND_PRINT((ndo, ", length %u", length));
            return (1);
        }
        pdu_len=EXTRACT_16BITS(header_iih_lan->pdu_len);
        if (packet_len>pdu_len) {
           packet_len=pdu_len; /* do TLV decoding as long as it makes sense */
           length=pdu_len;
        }

        ND_PRINT((ndo, "\n\t  source-id: %s,  holding time: %us, Flags: [%s]",
                  isis_print_id(header_iih_lan->source_id,SYSTEM_ID_LEN),
                  EXTRACT_16BITS(header_iih_lan->holding_time),
                  tok2str(isis_iih_circuit_type_values,
                          "unknown circuit type 0x%02x",
                          header_iih_lan->circuit_type)));

        ND_PRINT((ndo, "\n\t  lan-id:    %s, Priority: %u, PDU length: %u",
                  isis_print_id(header_iih_lan->lan_id, NODE_ID_LEN),
                  (header_iih_lan->priority) & ISIS_LAN_PRIORITY_MASK,
                  pdu_len));

        if (ndo->ndo_vflag > 1) {
            if (!print_unknown_data(ndo, pptr, "\n\t  ", ISIS_IIH_LAN_HEADER_SIZE))
                return (0);
        }

        packet_len -= (ISIS_COMMON_HEADER_SIZE+ISIS_IIH_LAN_HEADER_SIZE);
        pptr = p + (ISIS_COMMON_HEADER_SIZE+ISIS_IIH_LAN_HEADER_SIZE);
        break;

    case ISIS_PDU_PTP_IIH:
        if (isis_header->fixed_len != (ISIS_COMMON_HEADER_SIZE+ISIS_IIH_PTP_HEADER_SIZE)) {
            ND_PRINT((ndo, ", bogus fixed header length %u should be %lu",
                      isis_header->fixed_len, (unsigned long)(ISIS_COMMON_HEADER_SIZE+ISIS_IIH_PTP_HEADER_SIZE)));
            return (0);
        }
        ND_TCHECK(*header_iih_ptp);
        if (length < ISIS_COMMON_HEADER_SIZE+ISIS_IIH_PTP_HEADER_SIZE)
            goto trunc;
        if (ndo->ndo_vflag == 0) {
            ND_PRINT((ndo, ", src-id %s", isis_print_id(header_iih_ptp->source_id, SYSTEM_ID_LEN)));
            ND_PRINT((ndo, ", length %u", length));
            return (1);
        }
        pdu_len=EXTRACT_16BITS(header_iih_ptp->pdu_len);
        if (packet_len>pdu_len) {
            packet_len=pdu_len; /* do TLV decoding as long as it makes sense */
            length=pdu_len;
        }

        ND_PRINT((ndo, "\n\t  source-id: %s, holding time: %us, Flags: [%s]",
                  isis_print_id(header_iih_ptp->source_id,SYSTEM_ID_LEN),
                  EXTRACT_16BITS(header_iih_ptp->holding_time),
                  tok2str(isis_iih_circuit_type_values,
                          "unknown circuit type 0x%02x",
                          header_iih_ptp->circuit_type)));

        ND_PRINT((ndo, "\n\t  circuit-id: 0x%02x, PDU length: %u",
                  header_iih_ptp->circuit_id,
                  pdu_len));

        if (ndo->ndo_vflag > 1) {
            if (!print_unknown_data(ndo, pptr, "\n\t  ", ISIS_IIH_PTP_HEADER_SIZE))
                return (0);
        }

        packet_len -= (ISIS_COMMON_HEADER_SIZE+ISIS_IIH_PTP_HEADER_SIZE);
        pptr = p + (ISIS_COMMON_HEADER_SIZE+ISIS_IIH_PTP_HEADER_SIZE);
        break;

    case ISIS_PDU_L1_LSP:
    case ISIS_PDU_L2_LSP:
        if (isis_header->fixed_len != (ISIS_COMMON_HEADER_SIZE+ISIS_LSP_HEADER_SIZE)) {
            ND_PRINT((ndo, ", bogus fixed header length %u should be %lu",
                   isis_header->fixed_len, (unsigned long)ISIS_LSP_HEADER_SIZE));
            return (0);
        }
        ND_TCHECK(*header_lsp);
        if (length < ISIS_COMMON_HEADER_SIZE+ISIS_LSP_HEADER_SIZE)
            goto trunc;
        if (ndo->ndo_vflag == 0) {
            ND_PRINT((ndo, ", lsp-id %s, seq 0x%08x, lifetime %5us",
                      isis_print_id(header_lsp->lsp_id, LSP_ID_LEN),
                      EXTRACT_32BITS(header_lsp->sequence_number),
                      EXTRACT_16BITS(header_lsp->remaining_lifetime)));
            ND_PRINT((ndo, ", length %u", length));
            return (1);
        }
        pdu_len=EXTRACT_16BITS(header_lsp->pdu_len);
        if (packet_len>pdu_len) {
            packet_len=pdu_len; /* do TLV decoding as long as it makes sense */
            length=pdu_len;
        }

        ND_PRINT((ndo, "\n\t  lsp-id: %s, seq: 0x%08x, lifetime: %5us\n\t  chksum: 0x%04x",
               isis_print_id(header_lsp->lsp_id, LSP_ID_LEN),
               EXTRACT_32BITS(header_lsp->sequence_number),
               EXTRACT_16BITS(header_lsp->remaining_lifetime),
               EXTRACT_16BITS(header_lsp->checksum)));

        osi_print_cksum(ndo, (const uint8_t *)header_lsp->lsp_id,
                        EXTRACT_16BITS(header_lsp->checksum),
                        12, length-12);

        ND_PRINT((ndo, ", PDU length: %u, Flags: [ %s",
               pdu_len,
               ISIS_MASK_LSP_OL_BIT(header_lsp->typeblock) ? "Overload bit set, " : ""));

        if (ISIS_MASK_LSP_ATT_BITS(header_lsp->typeblock)) {
            ND_PRINT((ndo, "%s", ISIS_MASK_LSP_ATT_DEFAULT_BIT(header_lsp->typeblock) ? "default " : ""));
            ND_PRINT((ndo, "%s", ISIS_MASK_LSP_ATT_DELAY_BIT(header_lsp->typeblock) ? "delay " : ""));
            ND_PRINT((ndo, "%s", ISIS_MASK_LSP_ATT_EXPENSE_BIT(header_lsp->typeblock) ? "expense " : ""));
            ND_PRINT((ndo, "%s", ISIS_MASK_LSP_ATT_ERROR_BIT(header_lsp->typeblock) ? "error " : ""));
            ND_PRINT((ndo, "ATT bit set, "));
        }
        ND_PRINT((ndo, "%s", ISIS_MASK_LSP_PARTITION_BIT(header_lsp->typeblock) ? "P bit set, " : ""));
        ND_PRINT((ndo, "%s ]", tok2str(isis_lsp_istype_values, "Unknown(0x%x)",
                  ISIS_MASK_LSP_ISTYPE_BITS(header_lsp->typeblock))));

        if (ndo->ndo_vflag > 1) {
            if (!print_unknown_data(ndo, pptr, "\n\t  ", ISIS_LSP_HEADER_SIZE))
                return (0);
        }

        packet_len -= (ISIS_COMMON_HEADER_SIZE+ISIS_LSP_HEADER_SIZE);
        pptr = p + (ISIS_COMMON_HEADER_SIZE+ISIS_LSP_HEADER_SIZE);
        break;

    case ISIS_PDU_L1_CSNP:
    case ISIS_PDU_L2_CSNP:
        if (isis_header->fixed_len != (ISIS_COMMON_HEADER_SIZE+ISIS_CSNP_HEADER_SIZE)) {
            ND_PRINT((ndo, ", bogus fixed header length %u should be %lu",
                      isis_header->fixed_len, (unsigned long)(ISIS_COMMON_HEADER_SIZE+ISIS_CSNP_HEADER_SIZE)));
            return (0);
        }
        ND_TCHECK(*header_csnp);
        if (length < ISIS_COMMON_HEADER_SIZE+ISIS_CSNP_HEADER_SIZE)
            goto trunc;
        if (ndo->ndo_vflag == 0) {
            ND_PRINT((ndo, ", src-id %s", isis_print_id(header_csnp->source_id, NODE_ID_LEN)));
            ND_PRINT((ndo, ", length %u", length));
            return (1);
        }
        pdu_len=EXTRACT_16BITS(header_csnp->pdu_len);
        if (packet_len>pdu_len) {
            packet_len=pdu_len; /* do TLV decoding as long as it makes sense */
            length=pdu_len;
        }

        ND_PRINT((ndo, "\n\t  source-id:    %s, PDU length: %u",
               isis_print_id(header_csnp->source_id, NODE_ID_LEN),
               pdu_len));
        ND_PRINT((ndo, "\n\t  start lsp-id: %s",
               isis_print_id(header_csnp->start_lsp_id, LSP_ID_LEN)));
        ND_PRINT((ndo, "\n\t  end lsp-id:   %s",
               isis_print_id(header_csnp->end_lsp_id, LSP_ID_LEN)));

        if (ndo->ndo_vflag > 1) {
            if (!print_unknown_data(ndo, pptr, "\n\t  ", ISIS_CSNP_HEADER_SIZE))
                return (0);
        }

        packet_len -= (ISIS_COMMON_HEADER_SIZE+ISIS_CSNP_HEADER_SIZE);
        pptr = p + (ISIS_COMMON_HEADER_SIZE+ISIS_CSNP_HEADER_SIZE);
        break;

    case ISIS_PDU_L1_PSNP:
    case ISIS_PDU_L2_PSNP:
        if (isis_header->fixed_len != (ISIS_COMMON_HEADER_SIZE+ISIS_PSNP_HEADER_SIZE)) {
            ND_PRINT((ndo, "- bogus fixed header length %u should be %lu",
                   isis_header->fixed_len, (unsigned long)(ISIS_COMMON_HEADER_SIZE+ISIS_PSNP_HEADER_SIZE)));
            return (0);
        }
        ND_TCHECK(*header_psnp);
        if (length < ISIS_COMMON_HEADER_SIZE+ISIS_PSNP_HEADER_SIZE)
            goto trunc;
        if (ndo->ndo_vflag == 0) {
            ND_PRINT((ndo, ", src-id %s", isis_print_id(header_psnp->source_id, NODE_ID_LEN)));
            ND_PRINT((ndo, ", length %u", length));
            return (1);
        }
        pdu_len=EXTRACT_16BITS(header_psnp->pdu_len);
        if (packet_len>pdu_len) {
            packet_len=pdu_len; /* do TLV decoding as long as it makes sense */
            length=pdu_len;
        }

        ND_PRINT((ndo, "\n\t  source-id:    %s, PDU length: %u",
               isis_print_id(header_psnp->source_id, NODE_ID_LEN),
               pdu_len));

        if (ndo->ndo_vflag > 1) {
            if (!print_unknown_data(ndo, pptr, "\n\t  ", ISIS_PSNP_HEADER_SIZE))
                return (0);
        }

        packet_len -= (ISIS_COMMON_HEADER_SIZE+ISIS_PSNP_HEADER_SIZE);
        pptr = p + (ISIS_COMMON_HEADER_SIZE+ISIS_PSNP_HEADER_SIZE);
        break;

    default:
        if (ndo->ndo_vflag == 0) {
            ND_PRINT((ndo, ", length %u", length));
            return (1);
        }
	(void)print_unknown_data(ndo, pptr, "\n\t  ", length);
	return (0);
    }

    /*
     * Now print the TLV's.
     */

    while (packet_len > 0) {
	ND_TCHECK2(*pptr, 2);
	if (packet_len < 2)
	    goto trunc;
	tlv_type = *pptr++;
	tlv_len = *pptr++;
        tmp =tlv_len; /* copy temporary len & pointer to packet data */
        tptr = pptr;
	packet_len -= 2;

        /* first lets see if we know the TLVs name*/
	ND_PRINT((ndo, "\n\t    %s TLV #%u, length: %u",
               tok2str(isis_tlv_values,
                       "unknown",
                       tlv_type),
               tlv_type,
               tlv_len));

        if (tlv_len == 0) /* something is invalid */
	    continue;

	if (packet_len < tlv_len)
	    goto trunc;

        /* now check if we have a decoder otherwise do a hexdump at the end*/
	switch (tlv_type) {
	case ISIS_TLV_AREA_ADDR:
	    ND_TCHECK2(*tptr, 1);
	    alen = *tptr++;
	    while (tmp && alen < tmp) {
	        ND_TCHECK2(*tptr, alen);
		ND_PRINT((ndo, "\n\t      Area address (length: %u): %s",
                       alen,
                       isonsap_string(ndo, tptr, alen)));
		tptr += alen;
		tmp -= alen + 1;
		if (tmp==0) /* if this is the last area address do not attemt a boundary check */
                    break;
		ND_TCHECK2(*tptr, 1);
		alen = *tptr++;
	    }
	    break;
	case ISIS_TLV_ISNEIGH:
	    while (tmp >= ETHER_ADDR_LEN) {
                ND_TCHECK2(*tptr, ETHER_ADDR_LEN);
                ND_PRINT((ndo, "\n\t      SNPA: %s", isis_print_id(tptr, ETHER_ADDR_LEN)));
                tmp -= ETHER_ADDR_LEN;
                tptr += ETHER_ADDR_LEN;
	    }
	    break;

        case ISIS_TLV_ISNEIGH_VARLEN:
            if (!ND_TTEST2(*tptr, 1) || tmp < 3) /* min. TLV length */
		goto trunctlv;
	    lan_alen = *tptr++; /* LAN address length */
	    if (lan_alen == 0) {
                ND_PRINT((ndo, "\n\t      LAN address length 0 bytes (invalid)"));
                break;
            }
            tmp --;
            ND_PRINT((ndo, "\n\t      LAN address length %u bytes ", lan_alen));
	    while (tmp >= lan_alen) {
                ND_TCHECK2(*tptr, lan_alen);
                ND_PRINT((ndo, "\n\t\tIS Neighbor: %s", isis_print_id(tptr, lan_alen)));
                tmp -= lan_alen;
                tptr +=lan_alen;
            }
            break;

	case ISIS_TLV_PADDING:
	    break;

        case ISIS_TLV_MT_IS_REACH:
            mt_len = isis_print_mtid(ndo, tptr, "\n\t      ");
            if (mt_len == 0) /* did something go wrong ? */
                goto trunctlv;
            tptr+=mt_len;
            tmp-=mt_len;
            while (tmp >= 2+NODE_ID_LEN+3+1) {
                ext_is_len = isis_print_ext_is_reach(ndo, tptr, "\n\t      ", tlv_type);
                if (ext_is_len == 0) /* did something go wrong ? */
                    goto trunctlv;

                tmp-=ext_is_len;
                tptr+=ext_is_len;
            }
            break;

        case ISIS_TLV_IS_ALIAS_ID:
	    while (tmp >= NODE_ID_LEN+1) { /* is it worth attempting a decode ? */
	        ext_is_len = isis_print_ext_is_reach(ndo, tptr, "\n\t      ", tlv_type);
		if (ext_is_len == 0) /* did something go wrong ? */
	            goto trunctlv;
		tmp-=ext_is_len;
		tptr+=ext_is_len;
	    }
	    break;

        case ISIS_TLV_EXT_IS_REACH:
            while (tmp >= NODE_ID_LEN+3+1) { /* is it worth attempting a decode ? */
                ext_is_len = isis_print_ext_is_reach(ndo, tptr, "\n\t      ", tlv_type);
                if (ext_is_len == 0) /* did something go wrong ? */
                    goto trunctlv;
                tmp-=ext_is_len;
                tptr+=ext_is_len;
            }
            break;
        case ISIS_TLV_IS_REACH:
	    ND_TCHECK2(*tptr,1);  /* check if there is one byte left to read out the virtual flag */
            ND_PRINT((ndo, "\n\t      %s",
                   tok2str(isis_is_reach_virtual_values,
                           "bogus virtual flag 0x%02x",
                           *tptr++)));
	    tlv_is_reach = (const struct isis_tlv_is_reach *)tptr;
            while (tmp >= sizeof(struct isis_tlv_is_reach)) {
		ND_TCHECK(*tlv_is_reach);
		ND_PRINT((ndo, "\n\t      IS Neighbor: %s",
		       isis_print_id(tlv_is_reach->neighbor_nodeid, NODE_ID_LEN)));
		isis_print_metric_block(ndo, &tlv_is_reach->isis_metric_block);
		tmp -= sizeof(struct isis_tlv_is_reach);
		tlv_is_reach++;
	    }
            break;

        case ISIS_TLV_ESNEIGH:
	    tlv_es_reach = (const struct isis_tlv_es_reach *)tptr;
            while (tmp >= sizeof(struct isis_tlv_es_reach)) {
		ND_TCHECK(*tlv_es_reach);
		ND_PRINT((ndo, "\n\t      ES Neighbor: %s",
                       isis_print_id(tlv_es_reach->neighbor_sysid, SYSTEM_ID_LEN)));
		isis_print_metric_block(ndo, &tlv_es_reach->isis_metric_block);
		tmp -= sizeof(struct isis_tlv_es_reach);
		tlv_es_reach++;
	    }
            break;

            /* those two TLVs share the same format */
	case ISIS_TLV_INT_IP_REACH:
	case ISIS_TLV_EXT_IP_REACH:
		if (!isis_print_tlv_ip_reach(ndo, pptr, "\n\t      ", tlv_len))
			return (1);
		break;

	case ISIS_TLV_EXTD_IP_REACH:
	    while (tmp>0) {
                ext_ip_len = isis_print_extd_ip_reach(ndo, tptr, "\n\t      ", AF_INET);
                if (ext_ip_len == 0) /* did something go wrong ? */
                    goto trunctlv;
                tptr+=ext_ip_len;
		tmp-=ext_ip_len;
	    }
	    break;

        case ISIS_TLV_MT_IP_REACH:
            mt_len = isis_print_mtid(ndo, tptr, "\n\t      ");
            if (mt_len == 0) { /* did something go wrong ? */
                goto trunctlv;
            }
            tptr+=mt_len;
            tmp-=mt_len;

            while (tmp>0) {
                ext_ip_len = isis_print_extd_ip_reach(ndo, tptr, "\n\t      ", AF_INET);
                if (ext_ip_len == 0) /* did something go wrong ? */
                    goto trunctlv;
                tptr+=ext_ip_len;
		tmp-=ext_ip_len;
	    }
	    break;

	case ISIS_TLV_IP6_REACH:
	    while (tmp>0) {
                ext_ip_len = isis_print_extd_ip_reach(ndo, tptr, "\n\t      ", AF_INET6);
                if (ext_ip_len == 0) /* did something go wrong ? */
                    goto trunctlv;
                tptr+=ext_ip_len;
		tmp-=ext_ip_len;
	    }
	    break;

	case ISIS_TLV_MT_IP6_REACH:
            mt_len = isis_print_mtid(ndo, tptr, "\n\t      ");
            if (mt_len == 0) { /* did something go wrong ? */
                goto trunctlv;
            }
            tptr+=mt_len;
            tmp-=mt_len;

	    while (tmp>0) {
                ext_ip_len = isis_print_extd_ip_reach(ndo, tptr, "\n\t      ", AF_INET6);
                if (ext_ip_len == 0) /* did something go wrong ? */
                    goto trunctlv;
                tptr+=ext_ip_len;
		tmp-=ext_ip_len;
	    }
	    break;

	case ISIS_TLV_IP6ADDR:
	    while (tmp>=sizeof(struct in6_addr)) {
		ND_TCHECK2(*tptr, sizeof(struct in6_addr));

                ND_PRINT((ndo, "\n\t      IPv6 interface address: %s",
		       ip6addr_string(ndo, tptr)));

		tptr += sizeof(struct in6_addr);
		tmp -= sizeof(struct in6_addr);
	    }
	    break;
	case ISIS_TLV_AUTH:
	    ND_TCHECK2(*tptr, 1);

            ND_PRINT((ndo, "\n\t      %s: ",
                   tok2str(isis_subtlv_auth_values,
                           "unknown Authentication type 0x%02x",
                           *tptr)));

	    switch (*tptr) {
	    case ISIS_SUBTLV_AUTH_SIMPLE:
		if (fn_printzp(ndo, tptr + 1, tlv_len - 1, ndo->ndo_snapend))
		    goto trunctlv;
		break;
	    case ISIS_SUBTLV_AUTH_MD5:
		for(i=1;i<tlv_len;i++) {
		    ND_TCHECK2(*(tptr + i), 1);
		    ND_PRINT((ndo, "%02x", *(tptr + i)));
		}
		if (tlv_len != ISIS_SUBTLV_AUTH_MD5_LEN+1)
                    ND_PRINT((ndo, ", (invalid subTLV) "));

                sigcheck = signature_verify(ndo, optr, length, tptr + 1,
                                            isis_clear_checksum_lifetime,
                                            header_lsp);
                ND_PRINT((ndo, " (%s)", tok2str(signature_check_values, "Unknown", sigcheck)));

		break;
            case ISIS_SUBTLV_AUTH_GENERIC:
		ND_TCHECK2(*(tptr + 1), 2);
                key_id = EXTRACT_16BITS((tptr+1));
                ND_PRINT((ndo, "%u, password: ", key_id));
                for(i=1 + sizeof(uint16_t);i<tlv_len;i++) {
                    ND_TCHECK2(*(tptr + i), 1);
                    ND_PRINT((ndo, "%02x", *(tptr + i)));
                }
                break;
	    case ISIS_SUBTLV_AUTH_PRIVATE:
	    default:
		if (!print_unknown_data(ndo, tptr + 1, "\n\t\t  ", tlv_len - 1))
		    return(0);
		break;
	    }
	    break;

	case ISIS_TLV_PTP_ADJ:
	    tlv_ptp_adj = (const struct isis_tlv_ptp_adj *)tptr;
	    if(tmp>=1) {
		ND_TCHECK2(*tptr, 1);
		ND_PRINT((ndo, "\n\t      Adjacency State: %s (%u)",
		       tok2str(isis_ptp_adjancey_values, "unknown", *tptr),
                        *tptr));
		tmp--;
	    }
	    if(tmp>sizeof(tlv_ptp_adj->extd_local_circuit_id)) {
		ND_TCHECK(tlv_ptp_adj->extd_local_circuit_id);
		ND_PRINT((ndo, "\n\t      Extended Local circuit-ID: 0x%08x",
		       EXTRACT_32BITS(tlv_ptp_adj->extd_local_circuit_id)));
		tmp-=sizeof(tlv_ptp_adj->extd_local_circuit_id);
	    }
	    if(tmp>=SYSTEM_ID_LEN) {
		ND_TCHECK2(tlv_ptp_adj->neighbor_sysid, SYSTEM_ID_LEN);
		ND_PRINT((ndo, "\n\t      Neighbor System-ID: %s",
		       isis_print_id(tlv_ptp_adj->neighbor_sysid, SYSTEM_ID_LEN)));
		tmp-=SYSTEM_ID_LEN;
	    }
	    if(tmp>=sizeof(tlv_ptp_adj->neighbor_extd_local_circuit_id)) {
		ND_TCHECK(tlv_ptp_adj->neighbor_extd_local_circuit_id);
		ND_PRINT((ndo, "\n\t      Neighbor Extended Local circuit-ID: 0x%08x",
		       EXTRACT_32BITS(tlv_ptp_adj->neighbor_extd_local_circuit_id)));
	    }
	    break;

	case ISIS_TLV_PROTOCOLS:
	    ND_PRINT((ndo, "\n\t      NLPID(s): "));
	    while (tmp>0) {
		ND_TCHECK2(*(tptr), 1);
		ND_PRINT((ndo, "%s (0x%02x)",
                       tok2str(nlpid_values,
                               "unknown",
                               *tptr),
                       *tptr));
		if (tmp>1) /* further NPLIDs ? - put comma */
		    ND_PRINT((ndo, ", "));
                tptr++;
                tmp--;
	    }
	    break;

    case ISIS_TLV_MT_PORT_CAP:
    {
      ND_TCHECK2(*(tptr), 2);

      ND_PRINT((ndo, "\n\t       RES: %d, MTID(s): %d",
              (EXTRACT_16BITS (tptr) >> 12),
              (EXTRACT_16BITS (tptr) & 0x0fff)));

      tmp = tmp-2;
      tptr = tptr+2;

      if (tmp)
        isis_print_mt_port_cap_subtlv(ndo, tptr, tmp);

      break;
    }

    case ISIS_TLV_MT_CAPABILITY:

      ND_TCHECK2(*(tptr), 2);

      ND_PRINT((ndo, "\n\t      O: %d, RES: %d, MTID(s): %d",
                (EXTRACT_16BITS(tptr) >> 15) & 0x01,
                (EXTRACT_16BITS(tptr) >> 12) & 0x07,
                EXTRACT_16BITS(tptr) & 0x0fff));

      tmp = tmp-2;
      tptr = tptr+2;

      if (tmp)
        isis_print_mt_capability_subtlv(ndo, tptr, tmp);

      break;

	case ISIS_TLV_TE_ROUTER_ID:
	    ND_TCHECK2(*pptr, sizeof(struct in_addr));
	    ND_PRINT((ndo, "\n\t      Traffic Engineering Router ID: %s", ipaddr_string(ndo, pptr)));
	    break;

	case ISIS_TLV_IPADDR:
	    while (tmp>=sizeof(struct in_addr)) {
		ND_TCHECK2(*tptr, sizeof(struct in_addr));
		ND_PRINT((ndo, "\n\t      IPv4 interface address: %s", ipaddr_string(ndo, tptr)));
		tptr += sizeof(struct in_addr);
		tmp -= sizeof(struct in_addr);
	    }
	    break;

	case ISIS_TLV_HOSTNAME:
	    ND_PRINT((ndo, "\n\t      Hostname: "));
	    if (fn_printzp(ndo, tptr, tmp, ndo->ndo_snapend))
		goto trunctlv;
	    break;

	case ISIS_TLV_SHARED_RISK_GROUP:
	    if (tmp < NODE_ID_LEN)
	        break;
	    ND_TCHECK2(*tptr, NODE_ID_LEN);
	    ND_PRINT((ndo, "\n\t      IS Neighbor: %s", isis_print_id(tptr, NODE_ID_LEN)));
	    tptr+=(NODE_ID_LEN);
	    tmp-=(NODE_ID_LEN);

	    if (tmp < 1)
	        break;
	    ND_TCHECK2(*tptr, 1);
	    ND_PRINT((ndo, ", Flags: [%s]", ISIS_MASK_TLV_SHARED_RISK_GROUP(*tptr++) ? "numbered" : "unnumbered"));
	    tmp--;

	    if (tmp < sizeof(struct in_addr))
	        break;
	    ND_TCHECK2(*tptr, sizeof(struct in_addr));
	    ND_PRINT((ndo, "\n\t      IPv4 interface address: %s", ipaddr_string(ndo, tptr)));
	    tptr+=sizeof(struct in_addr);
	    tmp-=sizeof(struct in_addr);

	    if (tmp < sizeof(struct in_addr))
	        break;
	    ND_TCHECK2(*tptr, sizeof(struct in_addr));
	    ND_PRINT((ndo, "\n\t      IPv4 neighbor address: %s", ipaddr_string(ndo, tptr)));
	    tptr+=sizeof(struct in_addr);
	    tmp-=sizeof(struct in_addr);

	    while (tmp>=4) {
                ND_TCHECK2(*tptr, 4);
                ND_PRINT((ndo, "\n\t      Link-ID: 0x%08x", EXTRACT_32BITS(tptr)));
                tptr+=4;
                tmp-=4;
	    }
	    break;

	case ISIS_TLV_LSP:
	    tlv_lsp = (const struct isis_tlv_lsp *)tptr;
	    while(tmp>=sizeof(struct isis_tlv_lsp)) {
		ND_TCHECK((tlv_lsp->lsp_id)[LSP_ID_LEN-1]);
		ND_PRINT((ndo, "\n\t      lsp-id: %s",
                       isis_print_id(tlv_lsp->lsp_id, LSP_ID_LEN)));
		ND_TCHECK2(tlv_lsp->sequence_number, 4);
		ND_PRINT((ndo, ", seq: 0x%08x", EXTRACT_32BITS(tlv_lsp->sequence_number)));
		ND_TCHECK2(tlv_lsp->remaining_lifetime, 2);
		ND_PRINT((ndo, ", lifetime: %5ds", EXTRACT_16BITS(tlv_lsp->remaining_lifetime)));
		ND_TCHECK2(tlv_lsp->checksum, 2);
		ND_PRINT((ndo, ", chksum: 0x%04x", EXTRACT_16BITS(tlv_lsp->checksum)));
		tmp-=sizeof(struct isis_tlv_lsp);
		tlv_lsp++;
	    }
	    break;

	case ISIS_TLV_CHECKSUM:
	    if (tmp < ISIS_TLV_CHECKSUM_MINLEN)
	        break;
	    ND_TCHECK2(*tptr, ISIS_TLV_CHECKSUM_MINLEN);
	    ND_PRINT((ndo, "\n\t      checksum: 0x%04x ", EXTRACT_16BITS(tptr)));
            /* do not attempt to verify the checksum if it is zero
             * most likely a HMAC-MD5 TLV is also present and
             * to avoid conflicts the checksum TLV is zeroed.
             * see rfc3358 for details
             */
            osi_print_cksum(ndo, optr, EXTRACT_16BITS(tptr), tptr-optr,
                length);
	    break;

	case ISIS_TLV_POI:
	    if (tlv_len >= SYSTEM_ID_LEN + 1) {
		ND_TCHECK2(*tptr, SYSTEM_ID_LEN + 1);
		ND_PRINT((ndo, "\n\t      Purge Originator System-ID: %s",
		       isis_print_id(tptr + 1, SYSTEM_ID_LEN)));
	    }

	    if (tlv_len == 2 * SYSTEM_ID_LEN + 1) {
		ND_TCHECK2(*tptr, 2 * SYSTEM_ID_LEN + 1);
		ND_PRINT((ndo, "\n\t      Received from System-ID: %s",
		       isis_print_id(tptr + SYSTEM_ID_LEN + 1, SYSTEM_ID_LEN)));
	    }
	    break;

	case ISIS_TLV_MT_SUPPORTED:
            if (tmp < ISIS_TLV_MT_SUPPORTED_MINLEN)
                break;
	    while (tmp>1) {
		/* length can only be a multiple of 2, otherwise there is
		   something broken -> so decode down until length is 1 */
		if (tmp!=1) {
                    mt_len = isis_print_mtid(ndo, tptr, "\n\t      ");
                    if (mt_len == 0) /* did something go wrong ? */
                        goto trunctlv;
                    tptr+=mt_len;
                    tmp-=mt_len;
		} else {
		    ND_PRINT((ndo, "\n\t      invalid MT-ID"));
		    break;
		}
	    }
	    break;

	case ISIS_TLV_RESTART_SIGNALING:
            /* first attempt to decode the flags */
            if (tmp < ISIS_TLV_RESTART_SIGNALING_FLAGLEN)
                break;
            ND_TCHECK2(*tptr, ISIS_TLV_RESTART_SIGNALING_FLAGLEN);
            ND_PRINT((ndo, "\n\t      Flags [%s]",
                   bittok2str(isis_restart_flag_values, "none", *tptr)));
            tptr+=ISIS_TLV_RESTART_SIGNALING_FLAGLEN;
            tmp-=ISIS_TLV_RESTART_SIGNALING_FLAGLEN;

            /* is there anything other than the flags field? */
            if (tmp == 0)
                break;

            if (tmp < ISIS_TLV_RESTART_SIGNALING_HOLDTIMELEN)
                break;
            ND_TCHECK2(*tptr, ISIS_TLV_RESTART_SIGNALING_HOLDTIMELEN);

            ND_PRINT((ndo, ", Remaining holding time %us", EXTRACT_16BITS(tptr)));
            tptr+=ISIS_TLV_RESTART_SIGNALING_HOLDTIMELEN;
            tmp-=ISIS_TLV_RESTART_SIGNALING_HOLDTIMELEN;

            /* is there an additional sysid field present ?*/
            if (tmp == SYSTEM_ID_LEN) {
                    ND_TCHECK2(*tptr, SYSTEM_ID_LEN);
                    ND_PRINT((ndo, ", for %s", isis_print_id(tptr,SYSTEM_ID_LEN)));
            }
	    break;

        case ISIS_TLV_IDRP_INFO:
	    if (tmp < ISIS_TLV_IDRP_INFO_MINLEN)
	        break;
            ND_TCHECK2(*tptr, ISIS_TLV_IDRP_INFO_MINLEN);
            ND_PRINT((ndo, "\n\t      Inter-Domain Information Type: %s",
                   tok2str(isis_subtlv_idrp_values,
                           "Unknown (0x%02x)",
                           *tptr)));
            switch (*tptr++) {
            case ISIS_SUBTLV_IDRP_ASN:
                ND_TCHECK2(*tptr, 2); /* fetch AS number */
                ND_PRINT((ndo, "AS Number: %u", EXTRACT_16BITS(tptr)));
                break;
            case ISIS_SUBTLV_IDRP_LOCAL:
            case ISIS_SUBTLV_IDRP_RES:
            default:
                if (!print_unknown_data(ndo, tptr, "\n\t      ", tlv_len - 1))
                    return(0);
                break;
            }
            break;

        case ISIS_TLV_LSP_BUFFERSIZE:
	    if (tmp < ISIS_TLV_LSP_BUFFERSIZE_MINLEN)
	        break;
            ND_TCHECK2(*tptr, ISIS_TLV_LSP_BUFFERSIZE_MINLEN);
            ND_PRINT((ndo, "\n\t      LSP Buffersize: %u", EXTRACT_16BITS(tptr)));
            break;

        case ISIS_TLV_PART_DIS:
            while (tmp >= SYSTEM_ID_LEN) {
                ND_TCHECK2(*tptr, SYSTEM_ID_LEN);
                ND_PRINT((ndo, "\n\t      %s", isis_print_id(tptr, SYSTEM_ID_LEN)));
                tptr+=SYSTEM_ID_LEN;
                tmp-=SYSTEM_ID_LEN;
            }
            break;

        case ISIS_TLV_PREFIX_NEIGH:
	    if (tmp < sizeof(struct isis_metric_block))
	        break;
            ND_TCHECK2(*tptr, sizeof(struct isis_metric_block));
            ND_PRINT((ndo, "\n\t      Metric Block"));
            isis_print_metric_block(ndo, (const struct isis_metric_block *)tptr);
            tptr+=sizeof(struct isis_metric_block);
            tmp-=sizeof(struct isis_metric_block);

            while(tmp>0) {
                ND_TCHECK2(*tptr, 1);
                prefix_len=*tptr++; /* read out prefix length in semioctets*/
                if (prefix_len < 2) {
                    ND_PRINT((ndo, "\n\t\tAddress: prefix length %u < 2", prefix_len));
                    break;
                }
                tmp--;
                if (tmp < prefix_len/2)
                    break;
                ND_TCHECK2(*tptr, prefix_len / 2);
                ND_PRINT((ndo, "\n\t\tAddress: %s/%u",
                       isonsap_string(ndo, tptr, prefix_len / 2), prefix_len * 4));
                tptr+=prefix_len/2;
                tmp-=prefix_len/2;
            }
            break;

        case ISIS_TLV_IIH_SEQNR:
	    if (tmp < ISIS_TLV_IIH_SEQNR_MINLEN)
	        break;
            ND_TCHECK2(*tptr, ISIS_TLV_IIH_SEQNR_MINLEN); /* check if four bytes are on the wire */
            ND_PRINT((ndo, "\n\t      Sequence number: %u", EXTRACT_32BITS(tptr)));
            break;

        case ISIS_TLV_VENDOR_PRIVATE:
	    if (tmp < ISIS_TLV_VENDOR_PRIVATE_MINLEN)
	        break;
            ND_TCHECK2(*tptr, ISIS_TLV_VENDOR_PRIVATE_MINLEN); /* check if enough byte for a full oui */
            vendor_id = EXTRACT_24BITS(tptr);
            ND_PRINT((ndo, "\n\t      Vendor: %s (%u)",
                   tok2str(oui_values, "Unknown", vendor_id),
                   vendor_id));
            tptr+=3;
            tmp-=3;
            if (tmp > 0) /* hexdump the rest */
                if (!print_unknown_data(ndo, tptr, "\n\t\t", tmp))
                    return(0);
            break;
            /*
             * FIXME those are the defined TLVs that lack a decoder
             * you are welcome to contribute code ;-)
             */

        case ISIS_TLV_DECNET_PHASE4:
        case ISIS_TLV_LUCENT_PRIVATE:
        case ISIS_TLV_IPAUTH:
        case ISIS_TLV_NORTEL_PRIVATE1:
        case ISIS_TLV_NORTEL_PRIVATE2:

	default:
		if (ndo->ndo_vflag <= 1) {
			if (!print_unknown_data(ndo, pptr, "\n\t\t", tlv_len))
				return(0);
		}
		break;
	}
        /* do we want to see an additionally hexdump ? */
	if (ndo->ndo_vflag> 1) {
		if (!print_unknown_data(ndo, pptr, "\n\t      ", tlv_len))
			return(0);
	}

	pptr += tlv_len;
	packet_len -= tlv_len;
    }

    if (packet_len != 0) {
	ND_PRINT((ndo, "\n\t      %u straggler bytes", packet_len));
    }
    return (1);

 trunc:
    ND_PRINT((ndo, "%s", tstr));
    return (1);

 trunctlv:
    ND_PRINT((ndo, "\n\t\t"));
    ND_PRINT((ndo, "%s", tstr));
    return(1);
}

static void
osi_print_cksum(netdissect_options *ndo, const uint8_t *pptr,
	        uint16_t checksum, int checksum_offset, u_int length)
{
        uint16_t calculated_checksum;

        /* do not attempt to verify the checksum if it is zero,
         * if the offset is nonsense,
         * or the base pointer is not sane
         */
        if (!checksum
            || checksum_offset < 0
            || !ND_TTEST2(*(pptr + checksum_offset), 2)
            || (u_int)checksum_offset > length
            || !ND_TTEST2(*pptr, length)) {
                ND_PRINT((ndo, " (unverified)"));
        } else {
#if 0
                printf("\nosi_print_cksum: %p %u %u\n", pptr, checksum_offset, length);
#endif
                calculated_checksum = create_osi_cksum(pptr, checksum_offset, length);
                if (checksum == calculated_checksum) {
                        ND_PRINT((ndo, " (correct)"));
                } else {
                        ND_PRINT((ndo, " (incorrect should be 0x%04x)", calculated_checksum));
                }
        }
}

/*
 * Local Variables:
 * c-style: whitesmith
 * c-basic-offset: 8
 * End:
 */
