/*     NetBSD: print-juniper.c,v 1.2 2007/07/24 11:53:45 drochner Exp        */

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

/* \summary: DLT_JUNIPER_* printers */

#ifndef lint
#else
__RCSID("NetBSD: print-juniper.c,v 1.3 2007/07/25 06:31:32 dogcow Exp ");
#endif

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <netdissect-stdinc.h>

#include <string.h>

#include "netdissect.h"
#include "addrtoname.h"
#include "extract.h"
#include "ppp.h"
#include "llc.h"
#include "nlpid.h"
#include "ethertype.h"
#include "atm.h"

#define JUNIPER_BPF_OUT           0       /* Outgoing packet */
#define JUNIPER_BPF_IN            1       /* Incoming packet */
#define JUNIPER_BPF_PKT_IN        0x1     /* Incoming packet */
#define JUNIPER_BPF_NO_L2         0x2     /* L2 header stripped */
#define JUNIPER_BPF_IIF           0x4     /* IIF is valid */
#define JUNIPER_BPF_FILTER        0x40    /* BPF filtering is supported */
#define JUNIPER_BPF_EXT           0x80    /* extensions present */
#define JUNIPER_MGC_NUMBER        0x4d4743 /* = "MGC" */

#define JUNIPER_LSQ_COOKIE_RE         (1 << 3)
#define JUNIPER_LSQ_COOKIE_DIR        (1 << 2)
#define JUNIPER_LSQ_L3_PROTO_SHIFT     4
#define JUNIPER_LSQ_L3_PROTO_MASK     (0x17 << JUNIPER_LSQ_L3_PROTO_SHIFT)
#define JUNIPER_LSQ_L3_PROTO_IPV4     (0 << JUNIPER_LSQ_L3_PROTO_SHIFT)
#define JUNIPER_LSQ_L3_PROTO_IPV6     (1 << JUNIPER_LSQ_L3_PROTO_SHIFT)
#define JUNIPER_LSQ_L3_PROTO_MPLS     (2 << JUNIPER_LSQ_L3_PROTO_SHIFT)
#define JUNIPER_LSQ_L3_PROTO_ISO      (3 << JUNIPER_LSQ_L3_PROTO_SHIFT)
#define AS_PIC_COOKIE_LEN 8

#define JUNIPER_IPSEC_O_ESP_ENCRYPT_ESP_AUTHEN_TYPE 1
#define JUNIPER_IPSEC_O_ESP_ENCRYPT_AH_AUTHEN_TYPE 2
#define JUNIPER_IPSEC_O_ESP_AUTHENTICATION_TYPE 3
#define JUNIPER_IPSEC_O_AH_AUTHENTICATION_TYPE 4
#define JUNIPER_IPSEC_O_ESP_ENCRYPTION_TYPE 5

static const struct tok juniper_ipsec_type_values[] = {
    { JUNIPER_IPSEC_O_ESP_ENCRYPT_ESP_AUTHEN_TYPE, "ESP ENCR-AUTH" },
    { JUNIPER_IPSEC_O_ESP_ENCRYPT_AH_AUTHEN_TYPE, "ESP ENCR-AH AUTH" },
    { JUNIPER_IPSEC_O_ESP_AUTHENTICATION_TYPE, "ESP AUTH" },
    { JUNIPER_IPSEC_O_AH_AUTHENTICATION_TYPE, "AH AUTH" },
    { JUNIPER_IPSEC_O_ESP_ENCRYPTION_TYPE, "ESP ENCR" },
    { 0, NULL}
};

static const struct tok juniper_direction_values[] = {
    { JUNIPER_BPF_IN,  "In"},
    { JUNIPER_BPF_OUT, "Out"},
    { 0, NULL}
};

/* codepoints for encoding extensions to a .pcap file */
enum {
    JUNIPER_EXT_TLV_IFD_IDX = 1,
    JUNIPER_EXT_TLV_IFD_NAME = 2,
    JUNIPER_EXT_TLV_IFD_MEDIATYPE = 3,
    JUNIPER_EXT_TLV_IFL_IDX = 4,
    JUNIPER_EXT_TLV_IFL_UNIT = 5,
    JUNIPER_EXT_TLV_IFL_ENCAPS = 6,
    JUNIPER_EXT_TLV_TTP_IFD_MEDIATYPE = 7,
    JUNIPER_EXT_TLV_TTP_IFL_ENCAPS = 8
};

/* 1 byte type and 1-byte length */
#define JUNIPER_EXT_TLV_OVERHEAD 2U

static const struct tok jnx_ext_tlv_values[] = {
    { JUNIPER_EXT_TLV_IFD_IDX, "Device Interface Index" },
    { JUNIPER_EXT_TLV_IFD_NAME,"Device Interface Name" },
    { JUNIPER_EXT_TLV_IFD_MEDIATYPE, "Device Media Type" },
    { JUNIPER_EXT_TLV_IFL_IDX, "Logical Interface Index" },
    { JUNIPER_EXT_TLV_IFL_UNIT,"Logical Unit Number" },
    { JUNIPER_EXT_TLV_IFL_ENCAPS, "Logical Interface Encapsulation" },
    { JUNIPER_EXT_TLV_TTP_IFD_MEDIATYPE, "TTP derived Device Media Type" },
    { JUNIPER_EXT_TLV_TTP_IFL_ENCAPS, "TTP derived Logical Interface Encapsulation" },
    { 0, NULL }
};

static const struct tok jnx_flag_values[] = {
    { JUNIPER_BPF_EXT, "Ext" },
    { JUNIPER_BPF_FILTER, "Filter" },
    { JUNIPER_BPF_IIF, "IIF" },
    { JUNIPER_BPF_NO_L2, "no-L2" },
    { JUNIPER_BPF_PKT_IN, "In" },
    { 0, NULL }
};

#define JUNIPER_IFML_ETHER              1
#define JUNIPER_IFML_FDDI               2
#define JUNIPER_IFML_TOKENRING          3
#define JUNIPER_IFML_PPP                4
#define JUNIPER_IFML_FRAMERELAY         5
#define JUNIPER_IFML_CISCOHDLC          6
#define JUNIPER_IFML_SMDSDXI            7
#define JUNIPER_IFML_ATMPVC             8
#define JUNIPER_IFML_PPP_CCC            9
#define JUNIPER_IFML_FRAMERELAY_CCC     10
#define JUNIPER_IFML_IPIP               11
#define JUNIPER_IFML_GRE                12
#define JUNIPER_IFML_PIM                13
#define JUNIPER_IFML_PIMD               14
#define JUNIPER_IFML_CISCOHDLC_CCC      15
#define JUNIPER_IFML_VLAN_CCC           16
#define JUNIPER_IFML_MLPPP              17
#define JUNIPER_IFML_MLFR               18
#define JUNIPER_IFML_ML                 19
#define JUNIPER_IFML_LSI                20
#define JUNIPER_IFML_DFE                21
#define JUNIPER_IFML_ATM_CELLRELAY_CCC  22
#define JUNIPER_IFML_CRYPTO             23
#define JUNIPER_IFML_GGSN               24
#define JUNIPER_IFML_LSI_PPP            25
#define JUNIPER_IFML_LSI_CISCOHDLC      26
#define JUNIPER_IFML_PPP_TCC            27
#define JUNIPER_IFML_FRAMERELAY_TCC     28
#define JUNIPER_IFML_CISCOHDLC_TCC      29
#define JUNIPER_IFML_ETHERNET_CCC       30
#define JUNIPER_IFML_VT                 31
#define JUNIPER_IFML_EXTENDED_VLAN_CCC  32
#define JUNIPER_IFML_ETHER_OVER_ATM     33
#define JUNIPER_IFML_MONITOR            34
#define JUNIPER_IFML_ETHERNET_TCC       35
#define JUNIPER_IFML_VLAN_TCC           36
#define JUNIPER_IFML_EXTENDED_VLAN_TCC  37
#define JUNIPER_IFML_CONTROLLER         38
#define JUNIPER_IFML_MFR                39
#define JUNIPER_IFML_LS                 40
#define JUNIPER_IFML_ETHERNET_VPLS      41
#define JUNIPER_IFML_ETHERNET_VLAN_VPLS 42
#define JUNIPER_IFML_ETHERNET_EXTENDED_VLAN_VPLS 43
#define JUNIPER_IFML_LT                 44
#define JUNIPER_IFML_SERVICES           45
#define JUNIPER_IFML_ETHER_VPLS_OVER_ATM 46
#define JUNIPER_IFML_FR_PORT_CCC        47
#define JUNIPER_IFML_FRAMERELAY_EXT_CCC 48
#define JUNIPER_IFML_FRAMERELAY_EXT_TCC 49
#define JUNIPER_IFML_FRAMERELAY_FLEX    50
#define JUNIPER_IFML_GGSNI              51
#define JUNIPER_IFML_ETHERNET_FLEX      52
#define JUNIPER_IFML_COLLECTOR          53
#define JUNIPER_IFML_AGGREGATOR         54
#define JUNIPER_IFML_LAPD               55
#define JUNIPER_IFML_PPPOE              56
#define JUNIPER_IFML_PPP_SUBORDINATE    57
#define JUNIPER_IFML_CISCOHDLC_SUBORDINATE  58
#define JUNIPER_IFML_DFC                59
#define JUNIPER_IFML_PICPEER            60

static const struct tok juniper_ifmt_values[] = {
    { JUNIPER_IFML_ETHER, "Ethernet" },
    { JUNIPER_IFML_FDDI, "FDDI" },
    { JUNIPER_IFML_TOKENRING, "Token-Ring" },
    { JUNIPER_IFML_PPP, "PPP" },
    { JUNIPER_IFML_PPP_SUBORDINATE, "PPP-Subordinate" },
    { JUNIPER_IFML_FRAMERELAY, "Frame-Relay" },
    { JUNIPER_IFML_CISCOHDLC, "Cisco-HDLC" },
    { JUNIPER_IFML_SMDSDXI, "SMDS-DXI" },
    { JUNIPER_IFML_ATMPVC, "ATM-PVC" },
    { JUNIPER_IFML_PPP_CCC, "PPP-CCC" },
    { JUNIPER_IFML_FRAMERELAY_CCC, "Frame-Relay-CCC" },
    { JUNIPER_IFML_FRAMERELAY_EXT_CCC, "Extended FR-CCC" },
    { JUNIPER_IFML_IPIP, "IP-over-IP" },
    { JUNIPER_IFML_GRE, "GRE" },
    { JUNIPER_IFML_PIM, "PIM-Encapsulator" },
    { JUNIPER_IFML_PIMD, "PIM-Decapsulator" },
    { JUNIPER_IFML_CISCOHDLC_CCC, "Cisco-HDLC-CCC" },
    { JUNIPER_IFML_VLAN_CCC, "VLAN-CCC" },
    { JUNIPER_IFML_EXTENDED_VLAN_CCC, "Extended-VLAN-CCC" },
    { JUNIPER_IFML_MLPPP, "Multilink-PPP" },
    { JUNIPER_IFML_MLFR, "Multilink-FR" },
    { JUNIPER_IFML_MFR, "Multilink-FR-UNI-NNI" },
    { JUNIPER_IFML_ML, "Multilink" },
    { JUNIPER_IFML_LS, "LinkService" },
    { JUNIPER_IFML_LSI, "LSI" },
    { JUNIPER_IFML_ATM_CELLRELAY_CCC, "ATM-CCC-Cell-Relay" },
    { JUNIPER_IFML_CRYPTO, "IPSEC-over-IP" },
    { JUNIPER_IFML_GGSN, "GGSN" },
    { JUNIPER_IFML_PPP_TCC, "PPP-TCC" },
    { JUNIPER_IFML_FRAMERELAY_TCC, "Frame-Relay-TCC" },
    { JUNIPER_IFML_FRAMERELAY_EXT_TCC, "Extended FR-TCC" },
    { JUNIPER_IFML_CISCOHDLC_TCC, "Cisco-HDLC-TCC" },
    { JUNIPER_IFML_ETHERNET_CCC, "Ethernet-CCC" },
    { JUNIPER_IFML_VT, "VPN-Loopback-tunnel" },
    { JUNIPER_IFML_ETHER_OVER_ATM, "Ethernet-over-ATM" },
    { JUNIPER_IFML_ETHER_VPLS_OVER_ATM, "Ethernet-VPLS-over-ATM" },
    { JUNIPER_IFML_MONITOR, "Monitor" },
    { JUNIPER_IFML_ETHERNET_TCC, "Ethernet-TCC" },
    { JUNIPER_IFML_VLAN_TCC, "VLAN-TCC" },
    { JUNIPER_IFML_EXTENDED_VLAN_TCC, "Extended-VLAN-TCC" },
    { JUNIPER_IFML_CONTROLLER, "Controller" },
    { JUNIPER_IFML_ETHERNET_VPLS, "VPLS" },
    { JUNIPER_IFML_ETHERNET_VLAN_VPLS, "VLAN-VPLS" },
    { JUNIPER_IFML_ETHERNET_EXTENDED_VLAN_VPLS, "Extended-VLAN-VPLS" },
    { JUNIPER_IFML_LT, "Logical-tunnel" },
    { JUNIPER_IFML_SERVICES, "General-Services" },
    { JUNIPER_IFML_PPPOE, "PPPoE" },
    { JUNIPER_IFML_ETHERNET_FLEX, "Flexible-Ethernet-Services" },
    { JUNIPER_IFML_FRAMERELAY_FLEX, "Flexible-FrameRelay" },
    { JUNIPER_IFML_COLLECTOR, "Flow-collection" },
    { JUNIPER_IFML_PICPEER, "PIC Peer" },
    { JUNIPER_IFML_DFC, "Dynamic-Flow-Capture" },
    {0,                    NULL}
};

#define JUNIPER_IFLE_ATM_SNAP           2
#define JUNIPER_IFLE_ATM_NLPID          3
#define JUNIPER_IFLE_ATM_VCMUX          4
#define JUNIPER_IFLE_ATM_LLC            5
#define JUNIPER_IFLE_ATM_PPP_VCMUX      6
#define JUNIPER_IFLE_ATM_PPP_LLC        7
#define JUNIPER_IFLE_ATM_PPP_FUNI       8
#define JUNIPER_IFLE_ATM_CCC            9
#define JUNIPER_IFLE_FR_NLPID           10
#define JUNIPER_IFLE_FR_SNAP            11
#define JUNIPER_IFLE_FR_PPP             12
#define JUNIPER_IFLE_FR_CCC             13
#define JUNIPER_IFLE_ENET2              14
#define JUNIPER_IFLE_IEEE8023_SNAP      15
#define JUNIPER_IFLE_IEEE8023_LLC       16
#define JUNIPER_IFLE_PPP                17
#define JUNIPER_IFLE_CISCOHDLC          18
#define JUNIPER_IFLE_PPP_CCC            19
#define JUNIPER_IFLE_IPIP_NULL          20
#define JUNIPER_IFLE_PIM_NULL           21
#define JUNIPER_IFLE_GRE_NULL           22
#define JUNIPER_IFLE_GRE_PPP            23
#define JUNIPER_IFLE_PIMD_DECAPS        24
#define JUNIPER_IFLE_CISCOHDLC_CCC      25
#define JUNIPER_IFLE_ATM_CISCO_NLPID    26
#define JUNIPER_IFLE_VLAN_CCC           27
#define JUNIPER_IFLE_MLPPP              28
#define JUNIPER_IFLE_MLFR               29
#define JUNIPER_IFLE_LSI_NULL           30
#define JUNIPER_IFLE_AGGREGATE_UNUSED   31
#define JUNIPER_IFLE_ATM_CELLRELAY_CCC  32
#define JUNIPER_IFLE_CRYPTO             33
#define JUNIPER_IFLE_GGSN               34
#define JUNIPER_IFLE_ATM_TCC            35
#define JUNIPER_IFLE_FR_TCC             36
#define JUNIPER_IFLE_PPP_TCC            37
#define JUNIPER_IFLE_CISCOHDLC_TCC      38
#define JUNIPER_IFLE_ETHERNET_CCC       39
#define JUNIPER_IFLE_VT                 40
#define JUNIPER_IFLE_ATM_EOA_LLC        41
#define JUNIPER_IFLE_EXTENDED_VLAN_CCC          42
#define JUNIPER_IFLE_ATM_SNAP_TCC       43
#define JUNIPER_IFLE_MONITOR            44
#define JUNIPER_IFLE_ETHERNET_TCC       45
#define JUNIPER_IFLE_VLAN_TCC           46
#define JUNIPER_IFLE_EXTENDED_VLAN_TCC  47
#define JUNIPER_IFLE_MFR                48
#define JUNIPER_IFLE_ETHERNET_VPLS      49
#define JUNIPER_IFLE_ETHERNET_VLAN_VPLS 50
#define JUNIPER_IFLE_ETHERNET_EXTENDED_VLAN_VPLS 51
#define JUNIPER_IFLE_SERVICES           52
#define JUNIPER_IFLE_ATM_ETHER_VPLS_ATM_LLC                53
#define JUNIPER_IFLE_FR_PORT_CCC        54
#define JUNIPER_IFLE_ATM_MLPPP_LLC      55
#define JUNIPER_IFLE_ATM_EOA_CCC        56
#define JUNIPER_IFLE_LT_VLAN            57
#define JUNIPER_IFLE_COLLECTOR          58
#define JUNIPER_IFLE_AGGREGATOR         59
#define JUNIPER_IFLE_LAPD               60
#define JUNIPER_IFLE_ATM_PPPOE_LLC          61
#define JUNIPER_IFLE_ETHERNET_PPPOE         62
#define JUNIPER_IFLE_PPPOE                  63
#define JUNIPER_IFLE_PPP_SUBORDINATE        64
#define JUNIPER_IFLE_CISCOHDLC_SUBORDINATE  65
#define JUNIPER_IFLE_DFC                    66
#define JUNIPER_IFLE_PICPEER                67

static const struct tok juniper_ifle_values[] = {
    { JUNIPER_IFLE_AGGREGATOR, "Aggregator" },
    { JUNIPER_IFLE_ATM_CCC, "CCC over ATM" },
    { JUNIPER_IFLE_ATM_CELLRELAY_CCC, "ATM CCC Cell Relay" },
    { JUNIPER_IFLE_ATM_CISCO_NLPID, "CISCO compatible NLPID" },
    { JUNIPER_IFLE_ATM_EOA_CCC, "Ethernet over ATM CCC" },
    { JUNIPER_IFLE_ATM_EOA_LLC, "Ethernet over ATM LLC" },
    { JUNIPER_IFLE_ATM_ETHER_VPLS_ATM_LLC, "Ethernet VPLS over ATM LLC" },
    { JUNIPER_IFLE_ATM_LLC, "ATM LLC" },
    { JUNIPER_IFLE_ATM_MLPPP_LLC, "MLPPP over ATM LLC" },
    { JUNIPER_IFLE_ATM_NLPID, "ATM NLPID" },
    { JUNIPER_IFLE_ATM_PPPOE_LLC, "PPPoE over ATM LLC" },
    { JUNIPER_IFLE_ATM_PPP_FUNI, "PPP over FUNI" },
    { JUNIPER_IFLE_ATM_PPP_LLC, "PPP over ATM LLC" },
    { JUNIPER_IFLE_ATM_PPP_VCMUX, "PPP over ATM VCMUX" },
    { JUNIPER_IFLE_ATM_SNAP, "ATM SNAP" },
    { JUNIPER_IFLE_ATM_SNAP_TCC, "ATM SNAP TCC" },
    { JUNIPER_IFLE_ATM_TCC, "ATM VCMUX TCC" },
    { JUNIPER_IFLE_ATM_VCMUX, "ATM VCMUX" },
    { JUNIPER_IFLE_CISCOHDLC, "C-HDLC" },
    { JUNIPER_IFLE_CISCOHDLC_CCC, "C-HDLC CCC" },
    { JUNIPER_IFLE_CISCOHDLC_SUBORDINATE, "C-HDLC via dialer" },
    { JUNIPER_IFLE_CISCOHDLC_TCC, "C-HDLC TCC" },
    { JUNIPER_IFLE_COLLECTOR, "Collector" },
    { JUNIPER_IFLE_CRYPTO, "Crypto" },
    { JUNIPER_IFLE_ENET2, "Ethernet" },
    { JUNIPER_IFLE_ETHERNET_CCC, "Ethernet CCC" },
    { JUNIPER_IFLE_ETHERNET_EXTENDED_VLAN_VPLS, "Extended VLAN VPLS" },
    { JUNIPER_IFLE_ETHERNET_PPPOE, "PPPoE over Ethernet" },
    { JUNIPER_IFLE_ETHERNET_TCC, "Ethernet TCC" },
    { JUNIPER_IFLE_ETHERNET_VLAN_VPLS, "VLAN VPLS" },
    { JUNIPER_IFLE_ETHERNET_VPLS, "VPLS" },
    { JUNIPER_IFLE_EXTENDED_VLAN_CCC, "Extended VLAN CCC" },
    { JUNIPER_IFLE_EXTENDED_VLAN_TCC, "Extended VLAN TCC" },
    { JUNIPER_IFLE_FR_CCC, "FR CCC" },
    { JUNIPER_IFLE_FR_NLPID, "FR NLPID" },
    { JUNIPER_IFLE_FR_PORT_CCC, "FR CCC" },
    { JUNIPER_IFLE_FR_PPP, "FR PPP" },
    { JUNIPER_IFLE_FR_SNAP, "FR SNAP" },
    { JUNIPER_IFLE_FR_TCC, "FR TCC" },
    { JUNIPER_IFLE_GGSN, "GGSN" },
    { JUNIPER_IFLE_GRE_NULL, "GRE NULL" },
    { JUNIPER_IFLE_GRE_PPP, "PPP over GRE" },
    { JUNIPER_IFLE_IPIP_NULL, "IPIP" },
    { JUNIPER_IFLE_LAPD, "LAPD" },
    { JUNIPER_IFLE_LSI_NULL, "LSI Null" },
    { JUNIPER_IFLE_LT_VLAN, "LT VLAN" },
    { JUNIPER_IFLE_MFR, "MFR" },
    { JUNIPER_IFLE_MLFR, "MLFR" },
    { JUNIPER_IFLE_MLPPP, "MLPPP" },
    { JUNIPER_IFLE_MONITOR, "Monitor" },
    { JUNIPER_IFLE_PIMD_DECAPS, "PIMd" },
    { JUNIPER_IFLE_PIM_NULL, "PIM Null" },
    { JUNIPER_IFLE_PPP, "PPP" },
    { JUNIPER_IFLE_PPPOE, "PPPoE" },
    { JUNIPER_IFLE_PPP_CCC, "PPP CCC" },
    { JUNIPER_IFLE_PPP_SUBORDINATE, "" },
    { JUNIPER_IFLE_PPP_TCC, "PPP TCC" },
    { JUNIPER_IFLE_SERVICES, "General Services" },
    { JUNIPER_IFLE_VLAN_CCC, "VLAN CCC" },
    { JUNIPER_IFLE_VLAN_TCC, "VLAN TCC" },
    { JUNIPER_IFLE_VT, "VT" },
    {0,                    NULL}
};

struct juniper_cookie_table_t {
    uint32_t pictype;		/* pic type */
    uint8_t  cookie_len;       /* cookie len */
    const char *s;		/* pic name */
};

static const struct juniper_cookie_table_t juniper_cookie_table[] = {
#ifdef DLT_JUNIPER_ATM1
    { DLT_JUNIPER_ATM1,  4, "ATM1"},
#endif
#ifdef DLT_JUNIPER_ATM2
    { DLT_JUNIPER_ATM2,  8, "ATM2"},
#endif
#ifdef DLT_JUNIPER_MLPPP
    { DLT_JUNIPER_MLPPP, 2, "MLPPP"},
#endif
#ifdef DLT_JUNIPER_MLFR
    { DLT_JUNIPER_MLFR,  2, "MLFR"},
#endif
#ifdef DLT_JUNIPER_MFR
    { DLT_JUNIPER_MFR,   4, "MFR"},
#endif
#ifdef DLT_JUNIPER_PPPOE
    { DLT_JUNIPER_PPPOE, 0, "PPPoE"},
#endif
#ifdef DLT_JUNIPER_PPPOE_ATM
    { DLT_JUNIPER_PPPOE_ATM, 0, "PPPoE ATM"},
#endif
#ifdef DLT_JUNIPER_GGSN
    { DLT_JUNIPER_GGSN, 8, "GGSN"},
#endif
#ifdef DLT_JUNIPER_MONITOR
    { DLT_JUNIPER_MONITOR, 8, "MONITOR"},
#endif
#ifdef DLT_JUNIPER_SERVICES
    { DLT_JUNIPER_SERVICES, 8, "AS"},
#endif
#ifdef DLT_JUNIPER_ES
    { DLT_JUNIPER_ES, 0, "ES"},
#endif
    { 0, 0, NULL }
};

struct juniper_l2info_t {
    uint32_t length;
    uint32_t caplen;
    uint32_t pictype;
    uint8_t direction;
    uint8_t header_len;
    uint8_t cookie_len;
    uint8_t cookie_type;
    uint8_t cookie[8];
    uint8_t bundle;
    uint16_t proto;
    uint8_t flags;
};

#define LS_COOKIE_ID            0x54
#define AS_COOKIE_ID            0x47
#define LS_MLFR_COOKIE_LEN	4
#define ML_MLFR_COOKIE_LEN	2
#define LS_MFR_COOKIE_LEN	6
#define ATM1_COOKIE_LEN         4
#define ATM2_COOKIE_LEN         8

#define ATM2_PKT_TYPE_MASK  0x70
#define ATM2_GAP_COUNT_MASK 0x3F

#define JUNIPER_PROTO_NULL          1
#define JUNIPER_PROTO_IPV4          2
#define JUNIPER_PROTO_IPV6          6

#define MFR_BE_MASK 0xc0

static const struct tok juniper_protocol_values[] = {
    { JUNIPER_PROTO_NULL, "Null" },
    { JUNIPER_PROTO_IPV4, "IPv4" },
    { JUNIPER_PROTO_IPV6, "IPv6" },
    { 0, NULL}
};

static int ip_heuristic_guess(netdissect_options *, register const u_char *, u_int);
static int juniper_ppp_heuristic_guess(netdissect_options *, register const u_char *, u_int);
static int juniper_parse_header(netdissect_options *, const u_char *, const struct pcap_pkthdr *, struct juniper_l2info_t *);

#ifdef DLT_JUNIPER_GGSN
u_int
juniper_ggsn_print(netdissect_options *ndo,
                   const struct pcap_pkthdr *h, register const u_char *p)
{
        struct juniper_l2info_t l2info;
        struct juniper_ggsn_header {
            uint8_t svc_id;
            uint8_t flags_len;
            uint8_t proto;
            uint8_t flags;
            uint8_t vlan_id[2];
            uint8_t res[2];
        };
        const struct juniper_ggsn_header *gh;

        l2info.pictype = DLT_JUNIPER_GGSN;
        if (juniper_parse_header(ndo, p, h, &l2info) == 0)
            return l2info.header_len;

        p+=l2info.header_len;
        gh = (struct juniper_ggsn_header *)&l2info.cookie;

        ND_TCHECK(*gh);
        if (ndo->ndo_eflag) {
            ND_PRINT((ndo, "proto %s (%u), vlan %u: ",
                   tok2str(juniper_protocol_values,"Unknown",gh->proto),
                   gh->proto,
                   EXTRACT_16BITS(&gh->vlan_id[0])));
        }

        switch (gh->proto) {
        case JUNIPER_PROTO_IPV4:
            ip_print(ndo, p, l2info.length);
            break;
        case JUNIPER_PROTO_IPV6:
            ip6_print(ndo, p, l2info.length);
            break;
        default:
            if (!ndo->ndo_eflag)
                ND_PRINT((ndo, "unknown GGSN proto (%u)", gh->proto));
        }

        return l2info.header_len;

trunc:
	ND_PRINT((ndo, "[|juniper_services]"));
	return l2info.header_len;
}
#endif

#ifdef DLT_JUNIPER_ES
u_int
juniper_es_print(netdissect_options *ndo,
                 const struct pcap_pkthdr *h, register const u_char *p)
{
        struct juniper_l2info_t l2info;
        struct juniper_ipsec_header {
            uint8_t sa_index[2];
            uint8_t ttl;
            uint8_t type;
            uint8_t spi[4];
            uint8_t src_ip[4];
            uint8_t dst_ip[4];
        };
        u_int rewrite_len,es_type_bundle;
        const struct juniper_ipsec_header *ih;

        l2info.pictype = DLT_JUNIPER_ES;
        if (juniper_parse_header(ndo, p, h, &l2info) == 0)
            return l2info.header_len;

        p+=l2info.header_len;
        ih = (const struct juniper_ipsec_header *)p;

        ND_TCHECK(*ih);
        switch (ih->type) {
        case JUNIPER_IPSEC_O_ESP_ENCRYPT_ESP_AUTHEN_TYPE:
        case JUNIPER_IPSEC_O_ESP_ENCRYPT_AH_AUTHEN_TYPE:
            rewrite_len = 0;
            es_type_bundle = 1;
            break;
        case JUNIPER_IPSEC_O_ESP_AUTHENTICATION_TYPE:
        case JUNIPER_IPSEC_O_AH_AUTHENTICATION_TYPE:
        case JUNIPER_IPSEC_O_ESP_ENCRYPTION_TYPE:
            rewrite_len = 16;
            es_type_bundle = 0;
            break;
        default:
            ND_PRINT((ndo, "ES Invalid type %u, length %u",
                   ih->type,
                   l2info.length));
            return l2info.header_len;
        }

        l2info.length-=rewrite_len;
        p+=rewrite_len;

        if (ndo->ndo_eflag) {
            if (!es_type_bundle) {
                ND_PRINT((ndo, "ES SA, index %u, ttl %u type %s (%u), spi %u, Tunnel %s > %s, length %u\n",
                       EXTRACT_16BITS(&ih->sa_index),
                       ih->ttl,
                       tok2str(juniper_ipsec_type_values,"Unknown",ih->type),
                       ih->type,
                       EXTRACT_32BITS(&ih->spi),
                       ipaddr_string(ndo, &ih->src_ip),
                       ipaddr_string(ndo, &ih->dst_ip),
                       l2info.length));
            } else {
                ND_PRINT((ndo, "ES SA, index %u, ttl %u type %s (%u), length %u\n",
                       EXTRACT_16BITS(&ih->sa_index),
                       ih->ttl,
                       tok2str(juniper_ipsec_type_values,"Unknown",ih->type),
                       ih->type,
                       l2info.length));
            }
        }

        ip_print(ndo, p, l2info.length);
        return l2info.header_len;

trunc:
	ND_PRINT((ndo, "[|juniper_services]"));
	return l2info.header_len;
}
#endif

#ifdef DLT_JUNIPER_MONITOR
u_int
juniper_monitor_print(netdissect_options *ndo,
                      const struct pcap_pkthdr *h, register const u_char *p)
{
        struct juniper_l2info_t l2info;
        struct juniper_monitor_header {
            uint8_t pkt_type;
            uint8_t padding;
            uint8_t iif[2];
            uint8_t service_id[4];
        };
        const struct juniper_monitor_header *mh;

        l2info.pictype = DLT_JUNIPER_MONITOR;
        if (juniper_parse_header(ndo, p, h, &l2info) == 0)
            return l2info.header_len;

        p+=l2info.header_len;
        mh = (const struct juniper_monitor_header *)p;

        ND_TCHECK(*mh);
        if (ndo->ndo_eflag)
            ND_PRINT((ndo, "service-id %u, iif %u, pkt-type %u: ",
                   EXTRACT_32BITS(&mh->service_id),
                   EXTRACT_16BITS(&mh->iif),
                   mh->pkt_type));

        /* no proto field - lets guess by first byte of IP header*/
        ip_heuristic_guess (ndo, p, l2info.length);

        return l2info.header_len;

trunc:
	ND_PRINT((ndo, "[|juniper_services]"));
	return l2info.header_len;
}
#endif

#ifdef DLT_JUNIPER_SERVICES
u_int
juniper_services_print(netdissect_options *ndo,
                       const struct pcap_pkthdr *h, register const u_char *p)
{
        struct juniper_l2info_t l2info;
        struct juniper_services_header {
            uint8_t svc_id;
            uint8_t flags_len;
            uint8_t svc_set_id[2];
            uint8_t dir_iif[4];
        };
        const struct juniper_services_header *sh;

        l2info.pictype = DLT_JUNIPER_SERVICES;
        if (juniper_parse_header(ndo, p, h, &l2info) == 0)
            return l2info.header_len;

        p+=l2info.header_len;
        sh = (const struct juniper_services_header *)p;

        ND_TCHECK(*sh);
        if (ndo->ndo_eflag)
            ND_PRINT((ndo, "service-id %u flags 0x%02x service-set-id 0x%04x iif %u: ",
                   sh->svc_id,
                   sh->flags_len,
                   EXTRACT_16BITS(&sh->svc_set_id),
                   EXTRACT_24BITS(&sh->dir_iif[1])));

        /* no proto field - lets guess by first byte of IP header*/
        ip_heuristic_guess (ndo, p, l2info.length);

        return l2info.header_len;

trunc:
	ND_PRINT((ndo, "[|juniper_services]"));
	return l2info.header_len;
}
#endif

#ifdef DLT_JUNIPER_PPPOE
u_int
juniper_pppoe_print(netdissect_options *ndo,
                    const struct pcap_pkthdr *h, register const u_char *p)
{
        struct juniper_l2info_t l2info;

        l2info.pictype = DLT_JUNIPER_PPPOE;
        if (juniper_parse_header(ndo, p, h, &l2info) == 0)
            return l2info.header_len;

        p+=l2info.header_len;
        /* this DLT contains nothing but raw ethernet frames */
        ether_print(ndo, p, l2info.length, l2info.caplen, NULL, NULL);
        return l2info.header_len;
}
#endif

#ifdef DLT_JUNIPER_ETHER
u_int
juniper_ether_print(netdissect_options *ndo,
                    const struct pcap_pkthdr *h, register const u_char *p)
{
        struct juniper_l2info_t l2info;

        l2info.pictype = DLT_JUNIPER_ETHER;
        if (juniper_parse_header(ndo, p, h, &l2info) == 0)
            return l2info.header_len;

        p+=l2info.header_len;
        /* this DLT contains nothing but raw Ethernet frames */
        ether_print(ndo, p, l2info.length, l2info.caplen, NULL, NULL);
        return l2info.header_len;
}
#endif

#ifdef DLT_JUNIPER_PPP
u_int
juniper_ppp_print(netdissect_options *ndo,
                  const struct pcap_pkthdr *h, register const u_char *p)
{
        struct juniper_l2info_t l2info;

        l2info.pictype = DLT_JUNIPER_PPP;
        if (juniper_parse_header(ndo, p, h, &l2info) == 0)
            return l2info.header_len;

        p+=l2info.header_len;
        /* this DLT contains nothing but raw ppp frames */
        ppp_print(ndo, p, l2info.length);
        return l2info.header_len;
}
#endif

#ifdef DLT_JUNIPER_FRELAY
u_int
juniper_frelay_print(netdissect_options *ndo,
                     const struct pcap_pkthdr *h, register const u_char *p)
{
        struct juniper_l2info_t l2info;

        l2info.pictype = DLT_JUNIPER_FRELAY;
        if (juniper_parse_header(ndo, p, h, &l2info) == 0)
            return l2info.header_len;

        p+=l2info.header_len;
        /* this DLT contains nothing but raw frame-relay frames */
        fr_print(ndo, p, l2info.length);
        return l2info.header_len;
}
#endif

#ifdef DLT_JUNIPER_CHDLC
u_int
juniper_chdlc_print(netdissect_options *ndo,
                    const struct pcap_pkthdr *h, register const u_char *p)
{
        struct juniper_l2info_t l2info;

        l2info.pictype = DLT_JUNIPER_CHDLC;
        if (juniper_parse_header(ndo, p, h, &l2info) == 0)
            return l2info.header_len;

        p+=l2info.header_len;
        /* this DLT contains nothing but raw c-hdlc frames */
        chdlc_print(ndo, p, l2info.length);
        return l2info.header_len;
}
#endif

#ifdef DLT_JUNIPER_PPPOE_ATM
u_int
juniper_pppoe_atm_print(netdissect_options *ndo,
                        const struct pcap_pkthdr *h, register const u_char *p)
{
        struct juniper_l2info_t l2info;
	uint16_t extracted_ethertype;

        l2info.pictype = DLT_JUNIPER_PPPOE_ATM;
        if (juniper_parse_header(ndo, p, h, &l2info) == 0)
            return l2info.header_len;

        p+=l2info.header_len;

        ND_TCHECK2(p[0], 2);
        extracted_ethertype = EXTRACT_16BITS(p);
        /* this DLT contains nothing but raw PPPoE frames,
         * prepended with a type field*/
        if (ethertype_print(ndo, extracted_ethertype,
                              p+ETHERTYPE_LEN,
                              l2info.length-ETHERTYPE_LEN,
                              l2info.caplen-ETHERTYPE_LEN,
                              NULL, NULL) == 0)
            /* ether_type not known, probably it wasn't one */
            ND_PRINT((ndo, "unknown ethertype 0x%04x", extracted_ethertype));

        return l2info.header_len;

trunc:
	ND_PRINT((ndo, "[|juniper_pppoe_atm]"));
	return l2info.header_len;
}
#endif

#ifdef DLT_JUNIPER_MLPPP
u_int
juniper_mlppp_print(netdissect_options *ndo,
                    const struct pcap_pkthdr *h, register const u_char *p)
{
        struct juniper_l2info_t l2info;

        l2info.pictype = DLT_JUNIPER_MLPPP;
        if (juniper_parse_header(ndo, p, h, &l2info) == 0)
            return l2info.header_len;

        /* suppress Bundle-ID if frame was captured on a child-link
         * best indicator if the cookie looks like a proto */
        if (ndo->ndo_eflag &&
            EXTRACT_16BITS(&l2info.cookie) != PPP_OSI &&
            EXTRACT_16BITS(&l2info.cookie) !=  (PPP_ADDRESS << 8 | PPP_CONTROL))
            ND_PRINT((ndo, "Bundle-ID %u: ", l2info.bundle));

        p+=l2info.header_len;

        /* first try the LSQ protos */
        switch(l2info.proto) {
        case JUNIPER_LSQ_L3_PROTO_IPV4:
            /* IP traffic going to the RE would not have a cookie
             * -> this must be incoming IS-IS over PPP
             */
            if (l2info.cookie[4] == (JUNIPER_LSQ_COOKIE_RE|JUNIPER_LSQ_COOKIE_DIR))
                ppp_print(ndo, p, l2info.length);
            else
                ip_print(ndo, p, l2info.length);
            return l2info.header_len;
        case JUNIPER_LSQ_L3_PROTO_IPV6:
            ip6_print(ndo, p,l2info.length);
            return l2info.header_len;
        case JUNIPER_LSQ_L3_PROTO_MPLS:
            mpls_print(ndo, p, l2info.length);
            return l2info.header_len;
        case JUNIPER_LSQ_L3_PROTO_ISO:
            isoclns_print(ndo, p, l2info.length);
            return l2info.header_len;
        default:
            break;
        }

        /* zero length cookie ? */
        switch (EXTRACT_16BITS(&l2info.cookie)) {
        case PPP_OSI:
            ppp_print(ndo, p - 2, l2info.length + 2);
            break;
        case (PPP_ADDRESS << 8 | PPP_CONTROL): /* fall through */
        default:
            ppp_print(ndo, p, l2info.length);
            break;
        }

        return l2info.header_len;
}
#endif


#ifdef DLT_JUNIPER_MFR
u_int
juniper_mfr_print(netdissect_options *ndo,
                  const struct pcap_pkthdr *h, register const u_char *p)
{
        struct juniper_l2info_t l2info;

        memset(&l2info, 0, sizeof(l2info));
        l2info.pictype = DLT_JUNIPER_MFR;
        if (juniper_parse_header(ndo, p, h, &l2info) == 0)
            return l2info.header_len;

        p+=l2info.header_len;

        /* child-link ? */
        if (l2info.cookie_len == 0) {
            mfr_print(ndo, p, l2info.length);
            return l2info.header_len;
        }

        /* first try the LSQ protos */
        if (l2info.cookie_len == AS_PIC_COOKIE_LEN) {
            switch(l2info.proto) {
            case JUNIPER_LSQ_L3_PROTO_IPV4:
                ip_print(ndo, p, l2info.length);
                return l2info.header_len;
            case JUNIPER_LSQ_L3_PROTO_IPV6:
                ip6_print(ndo, p,l2info.length);
                return l2info.header_len;
            case JUNIPER_LSQ_L3_PROTO_MPLS:
                mpls_print(ndo, p, l2info.length);
                return l2info.header_len;
            case JUNIPER_LSQ_L3_PROTO_ISO:
                isoclns_print(ndo, p, l2info.length);
                return l2info.header_len;
            default:
                break;
            }
            return l2info.header_len;
        }

        /* suppress Bundle-ID if frame was captured on a child-link */
        if (ndo->ndo_eflag && EXTRACT_32BITS(l2info.cookie) != 1)
            ND_PRINT((ndo, "Bundle-ID %u, ", l2info.bundle));
        switch (l2info.proto) {
        case (LLCSAP_ISONS<<8 | LLCSAP_ISONS):
            isoclns_print(ndo, p + 1, l2info.length - 1);
            break;
        case (LLC_UI<<8 | NLPID_Q933):
        case (LLC_UI<<8 | NLPID_IP):
        case (LLC_UI<<8 | NLPID_IP6):
            /* pass IP{4,6} to the OSI layer for proper link-layer printing */
            isoclns_print(ndo, p - 1, l2info.length + 1);
            break;
        default:
            ND_PRINT((ndo, "unknown protocol 0x%04x, length %u", l2info.proto, l2info.length));
        }

        return l2info.header_len;
}
#endif

#ifdef DLT_JUNIPER_MLFR
u_int
juniper_mlfr_print(netdissect_options *ndo,
                   const struct pcap_pkthdr *h, register const u_char *p)
{
        struct juniper_l2info_t l2info;

        l2info.pictype = DLT_JUNIPER_MLFR;
        if (juniper_parse_header(ndo, p, h, &l2info) == 0)
            return l2info.header_len;

        p+=l2info.header_len;

        /* suppress Bundle-ID if frame was captured on a child-link */
        if (ndo->ndo_eflag && EXTRACT_32BITS(l2info.cookie) != 1)
            ND_PRINT((ndo, "Bundle-ID %u, ", l2info.bundle));
        switch (l2info.proto) {
        case (LLC_UI):
        case (LLC_UI<<8):
            isoclns_print(ndo, p, l2info.length);
            break;
        case (LLC_UI<<8 | NLPID_Q933):
        case (LLC_UI<<8 | NLPID_IP):
        case (LLC_UI<<8 | NLPID_IP6):
            /* pass IP{4,6} to the OSI layer for proper link-layer printing */
            isoclns_print(ndo, p - 1, l2info.length + 1);
            break;
        default:
            ND_PRINT((ndo, "unknown protocol 0x%04x, length %u", l2info.proto, l2info.length));
        }

        return l2info.header_len;
}
#endif

/*
 *     ATM1 PIC cookie format
 *
 *     +-----+-------------------------+-------------------------------+
 *     |fmtid|     vc index            |  channel  ID                  |
 *     +-----+-------------------------+-------------------------------+
 */

#ifdef DLT_JUNIPER_ATM1
u_int
juniper_atm1_print(netdissect_options *ndo,
                   const struct pcap_pkthdr *h, register const u_char *p)
{
        int llc_hdrlen;

        struct juniper_l2info_t l2info;

        l2info.pictype = DLT_JUNIPER_ATM1;
        if (juniper_parse_header(ndo, p, h, &l2info) == 0)
            return l2info.header_len;

        p+=l2info.header_len;

        if (l2info.cookie[0] == 0x80) { /* OAM cell ? */
            oam_print(ndo, p, l2info.length, ATM_OAM_NOHEC);
            return l2info.header_len;
        }

        ND_TCHECK2(p[0], 3);
        if (EXTRACT_24BITS(p) == 0xfefe03 || /* NLPID encaps ? */
            EXTRACT_24BITS(p) == 0xaaaa03) { /* SNAP encaps ? */

            llc_hdrlen = llc_print(ndo, p, l2info.length, l2info.caplen, NULL, NULL);
            if (llc_hdrlen > 0)
                return l2info.header_len;
        }

        if (p[0] == 0x03) { /* Cisco style NLPID encaps ? */
            isoclns_print(ndo, p + 1, l2info.length - 1);
            /* FIXME check if frame was recognized */
            return l2info.header_len;
        }

        if (ip_heuristic_guess(ndo, p, l2info.length) != 0) /* last try - vcmux encaps ? */
            return l2info.header_len;

	return l2info.header_len;

trunc:
	ND_PRINT((ndo, "[|juniper_atm1]"));
	return l2info.header_len;
}
#endif

/*
 *     ATM2 PIC cookie format
 *
 *     +-------------------------------+---------+---+-----+-----------+
 *     |     channel ID                |  reserv |AAL| CCRQ| gap cnt   |
 *     +-------------------------------+---------+---+-----+-----------+
 */

#ifdef DLT_JUNIPER_ATM2
u_int
juniper_atm2_print(netdissect_options *ndo,
                   const struct pcap_pkthdr *h, register const u_char *p)
{
        int llc_hdrlen;

        struct juniper_l2info_t l2info;

        l2info.pictype = DLT_JUNIPER_ATM2;
        if (juniper_parse_header(ndo, p, h, &l2info) == 0)
            return l2info.header_len;

        p+=l2info.header_len;

        if (l2info.cookie[7] & ATM2_PKT_TYPE_MASK) { /* OAM cell ? */
            oam_print(ndo, p, l2info.length, ATM_OAM_NOHEC);
            return l2info.header_len;
        }

        ND_TCHECK2(p[0], 3);
        if (EXTRACT_24BITS(p) == 0xfefe03 || /* NLPID encaps ? */
            EXTRACT_24BITS(p) == 0xaaaa03) { /* SNAP encaps ? */

            llc_hdrlen = llc_print(ndo, p, l2info.length, l2info.caplen, NULL, NULL);
            if (llc_hdrlen > 0)
                return l2info.header_len;
        }

        if (l2info.direction != JUNIPER_BPF_PKT_IN && /* ether-over-1483 encaps ? */
            (EXTRACT_32BITS(l2info.cookie) & ATM2_GAP_COUNT_MASK)) {
            ether_print(ndo, p, l2info.length, l2info.caplen, NULL, NULL);
            return l2info.header_len;
        }

        if (p[0] == 0x03) { /* Cisco style NLPID encaps ? */
            isoclns_print(ndo, p + 1, l2info.length - 1);
            /* FIXME check if frame was recognized */
            return l2info.header_len;
        }

        if(juniper_ppp_heuristic_guess(ndo, p, l2info.length) != 0) /* PPPoA vcmux encaps ? */
            return l2info.header_len;

        if (ip_heuristic_guess(ndo, p, l2info.length) != 0) /* last try - vcmux encaps ? */
            return l2info.header_len;

	return l2info.header_len;

trunc:
	ND_PRINT((ndo, "[|juniper_atm2]"));
	return l2info.header_len;
}
#endif


/* try to guess, based on all PPP protos that are supported in
 * a juniper router if the payload data is encapsulated using PPP */
static int
juniper_ppp_heuristic_guess(netdissect_options *ndo,
                            register const u_char *p, u_int length)
{
    switch(EXTRACT_16BITS(p)) {
    case PPP_IP :
    case PPP_OSI :
    case PPP_MPLS_UCAST :
    case PPP_MPLS_MCAST :
    case PPP_IPCP :
    case PPP_OSICP :
    case PPP_MPLSCP :
    case PPP_LCP :
    case PPP_PAP :
    case PPP_CHAP :
    case PPP_ML :
    case PPP_IPV6 :
    case PPP_IPV6CP :
        ppp_print(ndo, p, length);
        break;

    default:
        return 0; /* did not find a ppp header */
        break;
    }
    return 1; /* we printed a ppp packet */
}

static int
ip_heuristic_guess(netdissect_options *ndo,
                   register const u_char *p, u_int length)
{
    switch(p[0]) {
    case 0x45:
    case 0x46:
    case 0x47:
    case 0x48:
    case 0x49:
    case 0x4a:
    case 0x4b:
    case 0x4c:
    case 0x4d:
    case 0x4e:
    case 0x4f:
	    ip_print(ndo, p, length);
	    break;
    case 0x60:
    case 0x61:
    case 0x62:
    case 0x63:
    case 0x64:
    case 0x65:
    case 0x66:
    case 0x67:
    case 0x68:
    case 0x69:
    case 0x6a:
    case 0x6b:
    case 0x6c:
    case 0x6d:
    case 0x6e:
    case 0x6f:
        ip6_print(ndo, p, length);
        break;
    default:
        return 0; /* did not find a ip header */
        break;
    }
    return 1; /* we printed an v4/v6 packet */
}

static int
juniper_read_tlv_value(const u_char *p, u_int tlv_type, u_int tlv_len)
{
   int tlv_value;

   /* TLVs < 128 are little endian encoded */
   if (tlv_type < 128) {
       switch (tlv_len) {
       case 1:
           tlv_value = *p;
           break;
       case 2:
           tlv_value = EXTRACT_LE_16BITS(p);
           break;
       case 3:
           tlv_value = EXTRACT_LE_24BITS(p);
           break;
       case 4:
           tlv_value = EXTRACT_LE_32BITS(p);
           break;
       default:
           tlv_value = -1;
           break;
       }
   } else {
       /* TLVs >= 128 are big endian encoded */
       switch (tlv_len) {
       case 1:
           tlv_value = *p;
           break;
       case 2:
           tlv_value = EXTRACT_16BITS(p);
           break;
       case 3:
           tlv_value = EXTRACT_24BITS(p);
           break;
       case 4:
           tlv_value = EXTRACT_32BITS(p);
           break;
       default:
           tlv_value = -1;
           break;
       }
   }
   return tlv_value;
}

static int
juniper_parse_header(netdissect_options *ndo,
                     const u_char *p, const struct pcap_pkthdr *h, struct juniper_l2info_t *l2info)
{
    const struct juniper_cookie_table_t *lp = juniper_cookie_table;
    u_int idx, jnx_ext_len, jnx_header_len = 0;
    uint8_t tlv_type,tlv_len;
    uint32_t control_word;
    int tlv_value;
    const u_char *tptr;


    l2info->header_len = 0;
    l2info->cookie_len = 0;
    l2info->proto = 0;


    l2info->length = h->len;
    l2info->caplen = h->caplen;
    ND_TCHECK2(p[0], 4);
    l2info->flags = p[3];
    l2info->direction = p[3]&JUNIPER_BPF_PKT_IN;

    if (EXTRACT_24BITS(p) != JUNIPER_MGC_NUMBER) { /* magic number found ? */
        ND_PRINT((ndo, "no magic-number found!"));
        return 0;
    }

    if (ndo->ndo_eflag) /* print direction */
        ND_PRINT((ndo, "%3s ", tok2str(juniper_direction_values, "---", l2info->direction)));

    /* magic number + flags */
    jnx_header_len = 4;

    if (ndo->ndo_vflag > 1)
        ND_PRINT((ndo, "\n\tJuniper PCAP Flags [%s]",
               bittok2str(jnx_flag_values, "none", l2info->flags)));

    /* extensions present ?  - calculate how much bytes to skip */
    if ((l2info->flags & JUNIPER_BPF_EXT ) == JUNIPER_BPF_EXT ) {

        tptr = p+jnx_header_len;

        /* ok to read extension length ? */
        ND_TCHECK2(tptr[0], 2);
        jnx_ext_len = EXTRACT_16BITS(tptr);
        jnx_header_len += 2;
        tptr +=2;

        /* nail up the total length -
         * just in case something goes wrong
         * with TLV parsing */
        jnx_header_len += jnx_ext_len;

        if (ndo->ndo_vflag > 1)
            ND_PRINT((ndo, ", PCAP Extension(s) total length %u", jnx_ext_len));

        ND_TCHECK2(tptr[0], jnx_ext_len);
        while (jnx_ext_len > JUNIPER_EXT_TLV_OVERHEAD) {
            tlv_type = *(tptr++);
            tlv_len = *(tptr++);
            tlv_value = 0;

            /* sanity checks */
            if (tlv_type == 0 || tlv_len == 0)
                break;
            if (tlv_len+JUNIPER_EXT_TLV_OVERHEAD > jnx_ext_len)
                goto trunc;

            if (ndo->ndo_vflag > 1)
                ND_PRINT((ndo, "\n\t  %s Extension TLV #%u, length %u, value ",
                       tok2str(jnx_ext_tlv_values,"Unknown",tlv_type),
                       tlv_type,
                       tlv_len));

            tlv_value = juniper_read_tlv_value(tptr, tlv_type, tlv_len);
            switch (tlv_type) {
            case JUNIPER_EXT_TLV_IFD_NAME:
                /* FIXME */
                break;
            case JUNIPER_EXT_TLV_IFD_MEDIATYPE:
            case JUNIPER_EXT_TLV_TTP_IFD_MEDIATYPE:
                if (tlv_value != -1) {
                    if (ndo->ndo_vflag > 1)
                        ND_PRINT((ndo, "%s (%u)",
                               tok2str(juniper_ifmt_values, "Unknown", tlv_value),
                               tlv_value));
                }
                break;
            case JUNIPER_EXT_TLV_IFL_ENCAPS:
            case JUNIPER_EXT_TLV_TTP_IFL_ENCAPS:
                if (tlv_value != -1) {
                    if (ndo->ndo_vflag > 1)
                        ND_PRINT((ndo, "%s (%u)",
                               tok2str(juniper_ifle_values, "Unknown", tlv_value),
                               tlv_value));
                }
                break;
            case JUNIPER_EXT_TLV_IFL_IDX: /* fall through */
            case JUNIPER_EXT_TLV_IFL_UNIT:
            case JUNIPER_EXT_TLV_IFD_IDX:
            default:
                if (tlv_value != -1) {
                    if (ndo->ndo_vflag > 1)
                        ND_PRINT((ndo, "%u", tlv_value));
                }
                break;
            }

            tptr+=tlv_len;
            jnx_ext_len -= tlv_len+JUNIPER_EXT_TLV_OVERHEAD;
        }

        if (ndo->ndo_vflag > 1)
            ND_PRINT((ndo, "\n\t-----original packet-----\n\t"));
    }

    if ((l2info->flags & JUNIPER_BPF_NO_L2 ) == JUNIPER_BPF_NO_L2 ) {
        if (ndo->ndo_eflag)
            ND_PRINT((ndo, "no-L2-hdr, "));

        /* there is no link-layer present -
         * perform the v4/v6 heuristics
         * to figure out what it is
         */
        ND_TCHECK2(p[jnx_header_len + 4], 1);
        if (ip_heuristic_guess(ndo, p + jnx_header_len + 4,
                               l2info->length - (jnx_header_len + 4)) == 0)
            ND_PRINT((ndo, "no IP-hdr found!"));

        l2info->header_len=jnx_header_len+4;
        return 0; /* stop parsing the output further */

    }
    l2info->header_len = jnx_header_len;
    p+=l2info->header_len;
    l2info->length -= l2info->header_len;
    l2info->caplen -= l2info->header_len;

    /* search through the cookie table and copy values matching for our PIC type */
    ND_TCHECK(p[0]);
    while (lp->s != NULL) {
        if (lp->pictype == l2info->pictype) {

            l2info->cookie_len += lp->cookie_len;

            switch (p[0]) {
            case LS_COOKIE_ID:
                l2info->cookie_type = LS_COOKIE_ID;
                l2info->cookie_len += 2;
                break;
            case AS_COOKIE_ID:
                l2info->cookie_type = AS_COOKIE_ID;
                l2info->cookie_len = 8;
                break;

            default:
                l2info->bundle = l2info->cookie[0];
                break;
            }


#ifdef DLT_JUNIPER_MFR
            /* MFR child links don't carry cookies */
            if (l2info->pictype == DLT_JUNIPER_MFR &&
                (p[0] & MFR_BE_MASK) == MFR_BE_MASK) {
                l2info->cookie_len = 0;
            }
#endif

            l2info->header_len += l2info->cookie_len;
            l2info->length -= l2info->cookie_len;
            l2info->caplen -= l2info->cookie_len;

            if (ndo->ndo_eflag)
                ND_PRINT((ndo, "%s-PIC, cookie-len %u",
                       lp->s,
                       l2info->cookie_len));

            if (l2info->cookie_len > 0) {
                ND_TCHECK2(p[0], l2info->cookie_len);
                if (ndo->ndo_eflag)
                    ND_PRINT((ndo, ", cookie 0x"));
                for (idx = 0; idx < l2info->cookie_len; idx++) {
                    l2info->cookie[idx] = p[idx]; /* copy cookie data */
                    if (ndo->ndo_eflag) ND_PRINT((ndo, "%02x", p[idx]));
                }
            }

            if (ndo->ndo_eflag) ND_PRINT((ndo, ": ")); /* print demarc b/w L2/L3*/


            ND_TCHECK_16BITS(p+l2info->cookie_len);
            l2info->proto = EXTRACT_16BITS(p+l2info->cookie_len);
            break;
        }
        ++lp;
    }
    p+=l2info->cookie_len;

    /* DLT_ specific parsing */
    switch(l2info->pictype) {
#ifdef DLT_JUNIPER_MLPPP
    case DLT_JUNIPER_MLPPP:
        switch (l2info->cookie_type) {
        case LS_COOKIE_ID:
            l2info->bundle = l2info->cookie[1];
            break;
        case AS_COOKIE_ID:
            l2info->bundle = (EXTRACT_16BITS(&l2info->cookie[6])>>3)&0xfff;
            l2info->proto = (l2info->cookie[5])&JUNIPER_LSQ_L3_PROTO_MASK;
            break;
        default:
            l2info->bundle = l2info->cookie[0];
            break;
        }
        break;
#endif
#ifdef DLT_JUNIPER_MLFR
    case DLT_JUNIPER_MLFR:
        switch (l2info->cookie_type) {
        case LS_COOKIE_ID:
            ND_TCHECK2(p[0], 2);
            l2info->bundle = l2info->cookie[1];
            l2info->proto = EXTRACT_16BITS(p);
            l2info->header_len += 2;
            l2info->length -= 2;
            l2info->caplen -= 2;
            break;
        case AS_COOKIE_ID:
            l2info->bundle = (EXTRACT_16BITS(&l2info->cookie[6])>>3)&0xfff;
            l2info->proto = (l2info->cookie[5])&JUNIPER_LSQ_L3_PROTO_MASK;
            break;
        default:
            l2info->bundle = l2info->cookie[0];
            l2info->header_len += 2;
            l2info->length -= 2;
            l2info->caplen -= 2;
            break;
        }
        break;
#endif
#ifdef DLT_JUNIPER_MFR
    case DLT_JUNIPER_MFR:
        switch (l2info->cookie_type) {
        case LS_COOKIE_ID:
            ND_TCHECK2(p[0], 2);
            l2info->bundle = l2info->cookie[1];
            l2info->proto = EXTRACT_16BITS(p);
            l2info->header_len += 2;
            l2info->length -= 2;
            l2info->caplen -= 2;
            break;
        case AS_COOKIE_ID:
            l2info->bundle = (EXTRACT_16BITS(&l2info->cookie[6])>>3)&0xfff;
            l2info->proto = (l2info->cookie[5])&JUNIPER_LSQ_L3_PROTO_MASK;
            break;
        default:
            l2info->bundle = l2info->cookie[0];
            break;
        }
        break;
#endif
#ifdef DLT_JUNIPER_ATM2
    case DLT_JUNIPER_ATM2:
        ND_TCHECK2(p[0], 4);
        /* ATM cell relay control word present ? */
        if (l2info->cookie[7] & ATM2_PKT_TYPE_MASK) {
            control_word = EXTRACT_32BITS(p);
            /* some control word heuristics */
            switch(control_word) {
            case 0: /* zero control word */
            case 0x08000000: /* < JUNOS 7.4 control-word */
            case 0x08380000: /* cntl word plus cell length (56) >= JUNOS 7.4*/
                l2info->header_len += 4;
                break;
            default:
                break;
            }

            if (ndo->ndo_eflag)
                ND_PRINT((ndo, "control-word 0x%08x ", control_word));
        }
        break;
#endif
#ifdef DLT_JUNIPER_GGSN
    case DLT_JUNIPER_GGSN:
        break;
#endif
#ifdef DLT_JUNIPER_ATM1
    case DLT_JUNIPER_ATM1:
        break;
#endif
#ifdef DLT_JUNIPER_PPP
    case DLT_JUNIPER_PPP:
        break;
#endif
#ifdef DLT_JUNIPER_CHDLC
    case DLT_JUNIPER_CHDLC:
        break;
#endif
#ifdef DLT_JUNIPER_ETHER
    case DLT_JUNIPER_ETHER:
        break;
#endif
#ifdef DLT_JUNIPER_FRELAY
    case DLT_JUNIPER_FRELAY:
        break;
#endif

    default:
        ND_PRINT((ndo, "Unknown Juniper DLT_ type %u: ", l2info->pictype));
        break;
    }

    if (ndo->ndo_eflag > 1)
        ND_PRINT((ndo, "hlen %u, proto 0x%04x, ", l2info->header_len, l2info->proto));

    return 1; /* everything went ok so far. continue parsing */
 trunc:
    ND_PRINT((ndo, "[|juniper_hdr], length %u", h->len));
    return 0;
}


/*
 * Local Variables:
 * c-style: whitesmith
 * c-basic-offset: 4
 * End:
 */
