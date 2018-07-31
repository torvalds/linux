/* SPDX-License-Identifier: (GPL-2.0+ OR BSD-3-Clause) */
/* Copyright 2013-2015 Freescale Semiconductor Inc.
 */
#ifndef __FSL_NET_H
#define __FSL_NET_H

#define LAST_HDR_INDEX 0xFFFFFFFF

/*****************************************************************************/
/*                Protocol fields                                            */
/*****************************************************************************/

/*************************  Ethernet fields  *********************************/
#define NH_FLD_ETH_DA                         (1)
#define NH_FLD_ETH_SA                         (NH_FLD_ETH_DA << 1)
#define NH_FLD_ETH_LENGTH                     (NH_FLD_ETH_DA << 2)
#define NH_FLD_ETH_TYPE                       (NH_FLD_ETH_DA << 3)
#define NH_FLD_ETH_FINAL_CKSUM                (NH_FLD_ETH_DA << 4)
#define NH_FLD_ETH_PADDING                    (NH_FLD_ETH_DA << 5)
#define NH_FLD_ETH_ALL_FIELDS                 ((NH_FLD_ETH_DA << 6) - 1)

#define NH_FLD_ETH_ADDR_SIZE                 6

/***************************  VLAN fields  ***********************************/
#define NH_FLD_VLAN_VPRI                      (1)
#define NH_FLD_VLAN_CFI                       (NH_FLD_VLAN_VPRI << 1)
#define NH_FLD_VLAN_VID                       (NH_FLD_VLAN_VPRI << 2)
#define NH_FLD_VLAN_LENGTH                    (NH_FLD_VLAN_VPRI << 3)
#define NH_FLD_VLAN_TYPE                      (NH_FLD_VLAN_VPRI << 4)
#define NH_FLD_VLAN_ALL_FIELDS                ((NH_FLD_VLAN_VPRI << 5) - 1)

#define NH_FLD_VLAN_TCI                       (NH_FLD_VLAN_VPRI | \
					       NH_FLD_VLAN_CFI | \
					       NH_FLD_VLAN_VID)

/************************  IP (generic) fields  ******************************/
#define NH_FLD_IP_VER                         (1)
#define NH_FLD_IP_DSCP                        (NH_FLD_IP_VER << 2)
#define NH_FLD_IP_ECN                         (NH_FLD_IP_VER << 3)
#define NH_FLD_IP_PROTO                       (NH_FLD_IP_VER << 4)
#define NH_FLD_IP_SRC                         (NH_FLD_IP_VER << 5)
#define NH_FLD_IP_DST                         (NH_FLD_IP_VER << 6)
#define NH_FLD_IP_TOS_TC                      (NH_FLD_IP_VER << 7)
#define NH_FLD_IP_ID                          (NH_FLD_IP_VER << 8)
#define NH_FLD_IP_ALL_FIELDS                  ((NH_FLD_IP_VER << 9) - 1)

#define NH_FLD_IP_PROTO_SIZE                  1

/*****************************  IPV4 fields  *********************************/
#define NH_FLD_IPV4_VER                       (1)
#define NH_FLD_IPV4_HDR_LEN                   (NH_FLD_IPV4_VER << 1)
#define NH_FLD_IPV4_TOS                       (NH_FLD_IPV4_VER << 2)
#define NH_FLD_IPV4_TOTAL_LEN                 (NH_FLD_IPV4_VER << 3)
#define NH_FLD_IPV4_ID                        (NH_FLD_IPV4_VER << 4)
#define NH_FLD_IPV4_FLAG_D                    (NH_FLD_IPV4_VER << 5)
#define NH_FLD_IPV4_FLAG_M                    (NH_FLD_IPV4_VER << 6)
#define NH_FLD_IPV4_OFFSET                    (NH_FLD_IPV4_VER << 7)
#define NH_FLD_IPV4_TTL                       (NH_FLD_IPV4_VER << 8)
#define NH_FLD_IPV4_PROTO                     (NH_FLD_IPV4_VER << 9)
#define NH_FLD_IPV4_CKSUM                     (NH_FLD_IPV4_VER << 10)
#define NH_FLD_IPV4_SRC_IP                    (NH_FLD_IPV4_VER << 11)
#define NH_FLD_IPV4_DST_IP                    (NH_FLD_IPV4_VER << 12)
#define NH_FLD_IPV4_OPTS                      (NH_FLD_IPV4_VER << 13)
#define NH_FLD_IPV4_OPTS_COUNT                (NH_FLD_IPV4_VER << 14)
#define NH_FLD_IPV4_ALL_FIELDS                ((NH_FLD_IPV4_VER << 15) - 1)

#define NH_FLD_IPV4_ADDR_SIZE                 4
#define NH_FLD_IPV4_PROTO_SIZE                1

/*****************************  IPV6 fields  *********************************/
#define NH_FLD_IPV6_VER                       (1)
#define NH_FLD_IPV6_TC                        (NH_FLD_IPV6_VER << 1)
#define NH_FLD_IPV6_SRC_IP                    (NH_FLD_IPV6_VER << 2)
#define NH_FLD_IPV6_DST_IP                    (NH_FLD_IPV6_VER << 3)
#define NH_FLD_IPV6_NEXT_HDR                  (NH_FLD_IPV6_VER << 4)
#define NH_FLD_IPV6_FL                        (NH_FLD_IPV6_VER << 5)
#define NH_FLD_IPV6_HOP_LIMIT                 (NH_FLD_IPV6_VER << 6)
#define NH_FLD_IPV6_ID			      (NH_FLD_IPV6_VER << 7)
#define NH_FLD_IPV6_ALL_FIELDS                ((NH_FLD_IPV6_VER << 8) - 1)

#define NH_FLD_IPV6_ADDR_SIZE                 16
#define NH_FLD_IPV6_NEXT_HDR_SIZE             1

/*****************************  ICMP fields  *********************************/
#define NH_FLD_ICMP_TYPE                      (1)
#define NH_FLD_ICMP_CODE                      (NH_FLD_ICMP_TYPE << 1)
#define NH_FLD_ICMP_CKSUM                     (NH_FLD_ICMP_TYPE << 2)
#define NH_FLD_ICMP_ID                        (NH_FLD_ICMP_TYPE << 3)
#define NH_FLD_ICMP_SQ_NUM                    (NH_FLD_ICMP_TYPE << 4)
#define NH_FLD_ICMP_ALL_FIELDS                ((NH_FLD_ICMP_TYPE << 5) - 1)

#define NH_FLD_ICMP_CODE_SIZE                 1
#define NH_FLD_ICMP_TYPE_SIZE                 1

/*****************************  IGMP fields  *********************************/
#define NH_FLD_IGMP_VERSION                   (1)
#define NH_FLD_IGMP_TYPE                      (NH_FLD_IGMP_VERSION << 1)
#define NH_FLD_IGMP_CKSUM                     (NH_FLD_IGMP_VERSION << 2)
#define NH_FLD_IGMP_DATA                      (NH_FLD_IGMP_VERSION << 3)
#define NH_FLD_IGMP_ALL_FIELDS                ((NH_FLD_IGMP_VERSION << 4) - 1)

/*****************************  TCP fields  **********************************/
#define NH_FLD_TCP_PORT_SRC                   (1)
#define NH_FLD_TCP_PORT_DST                   (NH_FLD_TCP_PORT_SRC << 1)
#define NH_FLD_TCP_SEQ                        (NH_FLD_TCP_PORT_SRC << 2)
#define NH_FLD_TCP_ACK                        (NH_FLD_TCP_PORT_SRC << 3)
#define NH_FLD_TCP_OFFSET                     (NH_FLD_TCP_PORT_SRC << 4)
#define NH_FLD_TCP_FLAGS                      (NH_FLD_TCP_PORT_SRC << 5)
#define NH_FLD_TCP_WINDOW                     (NH_FLD_TCP_PORT_SRC << 6)
#define NH_FLD_TCP_CKSUM                      (NH_FLD_TCP_PORT_SRC << 7)
#define NH_FLD_TCP_URGPTR                     (NH_FLD_TCP_PORT_SRC << 8)
#define NH_FLD_TCP_OPTS                       (NH_FLD_TCP_PORT_SRC << 9)
#define NH_FLD_TCP_OPTS_COUNT                 (NH_FLD_TCP_PORT_SRC << 10)
#define NH_FLD_TCP_ALL_FIELDS                 ((NH_FLD_TCP_PORT_SRC << 11) - 1)

#define NH_FLD_TCP_PORT_SIZE                  2

/*****************************  UDP fields  **********************************/
#define NH_FLD_UDP_PORT_SRC                   (1)
#define NH_FLD_UDP_PORT_DST                   (NH_FLD_UDP_PORT_SRC << 1)
#define NH_FLD_UDP_LEN                        (NH_FLD_UDP_PORT_SRC << 2)
#define NH_FLD_UDP_CKSUM                      (NH_FLD_UDP_PORT_SRC << 3)
#define NH_FLD_UDP_ALL_FIELDS                 ((NH_FLD_UDP_PORT_SRC << 4) - 1)

#define NH_FLD_UDP_PORT_SIZE                  2

/***************************  UDP-lite fields  *******************************/
#define NH_FLD_UDP_LITE_PORT_SRC              (1)
#define NH_FLD_UDP_LITE_PORT_DST              (NH_FLD_UDP_LITE_PORT_SRC << 1)
#define NH_FLD_UDP_LITE_ALL_FIELDS \
	((NH_FLD_UDP_LITE_PORT_SRC << 2) - 1)

#define NH_FLD_UDP_LITE_PORT_SIZE             2

/***************************  UDP-encap-ESP fields  **************************/
#define NH_FLD_UDP_ENC_ESP_PORT_SRC         (1)
#define NH_FLD_UDP_ENC_ESP_PORT_DST         (NH_FLD_UDP_ENC_ESP_PORT_SRC << 1)
#define NH_FLD_UDP_ENC_ESP_LEN              (NH_FLD_UDP_ENC_ESP_PORT_SRC << 2)
#define NH_FLD_UDP_ENC_ESP_CKSUM            (NH_FLD_UDP_ENC_ESP_PORT_SRC << 3)
#define NH_FLD_UDP_ENC_ESP_SPI              (NH_FLD_UDP_ENC_ESP_PORT_SRC << 4)
#define NH_FLD_UDP_ENC_ESP_SEQUENCE_NUM     (NH_FLD_UDP_ENC_ESP_PORT_SRC << 5)
#define NH_FLD_UDP_ENC_ESP_ALL_FIELDS \
	((NH_FLD_UDP_ENC_ESP_PORT_SRC << 6) - 1)

#define NH_FLD_UDP_ENC_ESP_PORT_SIZE        2
#define NH_FLD_UDP_ENC_ESP_SPI_SIZE         4

/*****************************  SCTP fields  *********************************/
#define NH_FLD_SCTP_PORT_SRC                  (1)
#define NH_FLD_SCTP_PORT_DST                  (NH_FLD_SCTP_PORT_SRC << 1)
#define NH_FLD_SCTP_VER_TAG                   (NH_FLD_SCTP_PORT_SRC << 2)
#define NH_FLD_SCTP_CKSUM                     (NH_FLD_SCTP_PORT_SRC << 3)
#define NH_FLD_SCTP_ALL_FIELDS                ((NH_FLD_SCTP_PORT_SRC << 4) - 1)

#define NH_FLD_SCTP_PORT_SIZE                 2

/*****************************  DCCP fields  *********************************/
#define NH_FLD_DCCP_PORT_SRC                  (1)
#define NH_FLD_DCCP_PORT_DST                  (NH_FLD_DCCP_PORT_SRC << 1)
#define NH_FLD_DCCP_ALL_FIELDS                ((NH_FLD_DCCP_PORT_SRC << 2) - 1)

#define NH_FLD_DCCP_PORT_SIZE                 2

/*****************************  IPHC fields  *********************************/
#define NH_FLD_IPHC_CID                       (1)
#define NH_FLD_IPHC_CID_TYPE                  (NH_FLD_IPHC_CID << 1)
#define NH_FLD_IPHC_HCINDEX                   (NH_FLD_IPHC_CID << 2)
#define NH_FLD_IPHC_GEN                       (NH_FLD_IPHC_CID << 3)
#define NH_FLD_IPHC_D_BIT                     (NH_FLD_IPHC_CID << 4)
#define NH_FLD_IPHC_ALL_FIELDS                ((NH_FLD_IPHC_CID << 5) - 1)

/*****************************  SCTP fields  *********************************/
#define NH_FLD_SCTP_CHUNK_DATA_TYPE           (1)
#define NH_FLD_SCTP_CHUNK_DATA_FLAGS          (NH_FLD_SCTP_CHUNK_DATA_TYPE << 1)
#define NH_FLD_SCTP_CHUNK_DATA_LENGTH         (NH_FLD_SCTP_CHUNK_DATA_TYPE << 2)
#define NH_FLD_SCTP_CHUNK_DATA_TSN            (NH_FLD_SCTP_CHUNK_DATA_TYPE << 3)
#define NH_FLD_SCTP_CHUNK_DATA_STREAM_ID      (NH_FLD_SCTP_CHUNK_DATA_TYPE << 4)
#define NH_FLD_SCTP_CHUNK_DATA_STREAM_SQN     (NH_FLD_SCTP_CHUNK_DATA_TYPE << 5)
#define NH_FLD_SCTP_CHUNK_DATA_PAYLOAD_PID    (NH_FLD_SCTP_CHUNK_DATA_TYPE << 6)
#define NH_FLD_SCTP_CHUNK_DATA_UNORDERED      (NH_FLD_SCTP_CHUNK_DATA_TYPE << 7)
#define NH_FLD_SCTP_CHUNK_DATA_BEGGINING      (NH_FLD_SCTP_CHUNK_DATA_TYPE << 8)
#define NH_FLD_SCTP_CHUNK_DATA_END            (NH_FLD_SCTP_CHUNK_DATA_TYPE << 9)
#define NH_FLD_SCTP_CHUNK_DATA_ALL_FIELDS \
	((NH_FLD_SCTP_CHUNK_DATA_TYPE << 10) - 1)

/***************************  L2TPV2 fields  *********************************/
#define NH_FLD_L2TPV2_TYPE_BIT                (1)
#define NH_FLD_L2TPV2_LENGTH_BIT              (NH_FLD_L2TPV2_TYPE_BIT << 1)
#define NH_FLD_L2TPV2_SEQUENCE_BIT            (NH_FLD_L2TPV2_TYPE_BIT << 2)
#define NH_FLD_L2TPV2_OFFSET_BIT              (NH_FLD_L2TPV2_TYPE_BIT << 3)
#define NH_FLD_L2TPV2_PRIORITY_BIT            (NH_FLD_L2TPV2_TYPE_BIT << 4)
#define NH_FLD_L2TPV2_VERSION                 (NH_FLD_L2TPV2_TYPE_BIT << 5)
#define NH_FLD_L2TPV2_LEN                     (NH_FLD_L2TPV2_TYPE_BIT << 6)
#define NH_FLD_L2TPV2_TUNNEL_ID               (NH_FLD_L2TPV2_TYPE_BIT << 7)
#define NH_FLD_L2TPV2_SESSION_ID              (NH_FLD_L2TPV2_TYPE_BIT << 8)
#define NH_FLD_L2TPV2_NS                      (NH_FLD_L2TPV2_TYPE_BIT << 9)
#define NH_FLD_L2TPV2_NR                      (NH_FLD_L2TPV2_TYPE_BIT << 10)
#define NH_FLD_L2TPV2_OFFSET_SIZE             (NH_FLD_L2TPV2_TYPE_BIT << 11)
#define NH_FLD_L2TPV2_FIRST_BYTE              (NH_FLD_L2TPV2_TYPE_BIT << 12)
#define NH_FLD_L2TPV2_ALL_FIELDS \
	((NH_FLD_L2TPV2_TYPE_BIT << 13) - 1)

/***************************  L2TPV3 fields  *********************************/
#define NH_FLD_L2TPV3_CTRL_TYPE_BIT           (1)
#define NH_FLD_L2TPV3_CTRL_LENGTH_BIT         (NH_FLD_L2TPV3_CTRL_TYPE_BIT << 1)
#define NH_FLD_L2TPV3_CTRL_SEQUENCE_BIT       (NH_FLD_L2TPV3_CTRL_TYPE_BIT << 2)
#define NH_FLD_L2TPV3_CTRL_VERSION            (NH_FLD_L2TPV3_CTRL_TYPE_BIT << 3)
#define NH_FLD_L2TPV3_CTRL_LENGTH             (NH_FLD_L2TPV3_CTRL_TYPE_BIT << 4)
#define NH_FLD_L2TPV3_CTRL_CONTROL            (NH_FLD_L2TPV3_CTRL_TYPE_BIT << 5)
#define NH_FLD_L2TPV3_CTRL_SENT               (NH_FLD_L2TPV3_CTRL_TYPE_BIT << 6)
#define NH_FLD_L2TPV3_CTRL_RECV               (NH_FLD_L2TPV3_CTRL_TYPE_BIT << 7)
#define NH_FLD_L2TPV3_CTRL_FIRST_BYTE         (NH_FLD_L2TPV3_CTRL_TYPE_BIT << 8)
#define NH_FLD_L2TPV3_CTRL_ALL_FIELDS \
	((NH_FLD_L2TPV3_CTRL_TYPE_BIT << 9) - 1)

#define NH_FLD_L2TPV3_SESS_TYPE_BIT           (1)
#define NH_FLD_L2TPV3_SESS_VERSION            (NH_FLD_L2TPV3_SESS_TYPE_BIT << 1)
#define NH_FLD_L2TPV3_SESS_ID                 (NH_FLD_L2TPV3_SESS_TYPE_BIT << 2)
#define NH_FLD_L2TPV3_SESS_COOKIE             (NH_FLD_L2TPV3_SESS_TYPE_BIT << 3)
#define NH_FLD_L2TPV3_SESS_ALL_FIELDS \
	((NH_FLD_L2TPV3_SESS_TYPE_BIT << 4) - 1)

/****************************  PPP fields  ***********************************/
#define NH_FLD_PPP_PID                        (1)
#define NH_FLD_PPP_COMPRESSED                 (NH_FLD_PPP_PID << 1)
#define NH_FLD_PPP_ALL_FIELDS                 ((NH_FLD_PPP_PID << 2) - 1)

/**************************  PPPoE fields  ***********************************/
#define NH_FLD_PPPOE_VER                      (1)
#define NH_FLD_PPPOE_TYPE                     (NH_FLD_PPPOE_VER << 1)
#define NH_FLD_PPPOE_CODE                     (NH_FLD_PPPOE_VER << 2)
#define NH_FLD_PPPOE_SID                      (NH_FLD_PPPOE_VER << 3)
#define NH_FLD_PPPOE_LEN                      (NH_FLD_PPPOE_VER << 4)
#define NH_FLD_PPPOE_SESSION                  (NH_FLD_PPPOE_VER << 5)
#define NH_FLD_PPPOE_PID                      (NH_FLD_PPPOE_VER << 6)
#define NH_FLD_PPPOE_ALL_FIELDS               ((NH_FLD_PPPOE_VER << 7) - 1)

/*************************  PPP-Mux fields  **********************************/
#define NH_FLD_PPPMUX_PID                     (1)
#define NH_FLD_PPPMUX_CKSUM                   (NH_FLD_PPPMUX_PID << 1)
#define NH_FLD_PPPMUX_COMPRESSED              (NH_FLD_PPPMUX_PID << 2)
#define NH_FLD_PPPMUX_ALL_FIELDS              ((NH_FLD_PPPMUX_PID << 3) - 1)

/***********************  PPP-Mux sub-frame fields  **************************/
#define NH_FLD_PPPMUX_SUBFRM_PFF            (1)
#define NH_FLD_PPPMUX_SUBFRM_LXT            (NH_FLD_PPPMUX_SUBFRM_PFF << 1)
#define NH_FLD_PPPMUX_SUBFRM_LEN            (NH_FLD_PPPMUX_SUBFRM_PFF << 2)
#define NH_FLD_PPPMUX_SUBFRM_PID            (NH_FLD_PPPMUX_SUBFRM_PFF << 3)
#define NH_FLD_PPPMUX_SUBFRM_USE_PID        (NH_FLD_PPPMUX_SUBFRM_PFF << 4)
#define NH_FLD_PPPMUX_SUBFRM_ALL_FIELDS \
	((NH_FLD_PPPMUX_SUBFRM_PFF << 5) - 1)

/***************************  LLC fields  ************************************/
#define NH_FLD_LLC_DSAP                       (1)
#define NH_FLD_LLC_SSAP                       (NH_FLD_LLC_DSAP << 1)
#define NH_FLD_LLC_CTRL                       (NH_FLD_LLC_DSAP << 2)
#define NH_FLD_LLC_ALL_FIELDS                 ((NH_FLD_LLC_DSAP << 3) - 1)

/***************************  NLPID fields  **********************************/
#define NH_FLD_NLPID_NLPID                    (1)
#define NH_FLD_NLPID_ALL_FIELDS               ((NH_FLD_NLPID_NLPID << 1) - 1)

/***************************  SNAP fields  ***********************************/
#define NH_FLD_SNAP_OUI                       (1)
#define NH_FLD_SNAP_PID                       (NH_FLD_SNAP_OUI << 1)
#define NH_FLD_SNAP_ALL_FIELDS                ((NH_FLD_SNAP_OUI << 2) - 1)

/***************************  LLC SNAP fields  *******************************/
#define NH_FLD_LLC_SNAP_TYPE                  (1)
#define NH_FLD_LLC_SNAP_ALL_FIELDS            ((NH_FLD_LLC_SNAP_TYPE << 1) - 1)

#define NH_FLD_ARP_HTYPE                      (1)
#define NH_FLD_ARP_PTYPE                      (NH_FLD_ARP_HTYPE << 1)
#define NH_FLD_ARP_HLEN                       (NH_FLD_ARP_HTYPE << 2)
#define NH_FLD_ARP_PLEN                       (NH_FLD_ARP_HTYPE << 3)
#define NH_FLD_ARP_OPER                       (NH_FLD_ARP_HTYPE << 4)
#define NH_FLD_ARP_SHA                        (NH_FLD_ARP_HTYPE << 5)
#define NH_FLD_ARP_SPA                        (NH_FLD_ARP_HTYPE << 6)
#define NH_FLD_ARP_THA                        (NH_FLD_ARP_HTYPE << 7)
#define NH_FLD_ARP_TPA                        (NH_FLD_ARP_HTYPE << 8)
#define NH_FLD_ARP_ALL_FIELDS                 ((NH_FLD_ARP_HTYPE << 9) - 1)

/***************************  RFC2684 fields  ********************************/
#define NH_FLD_RFC2684_LLC                    (1)
#define NH_FLD_RFC2684_NLPID                  (NH_FLD_RFC2684_LLC << 1)
#define NH_FLD_RFC2684_OUI                    (NH_FLD_RFC2684_LLC << 2)
#define NH_FLD_RFC2684_PID                    (NH_FLD_RFC2684_LLC << 3)
#define NH_FLD_RFC2684_VPN_OUI                (NH_FLD_RFC2684_LLC << 4)
#define NH_FLD_RFC2684_VPN_IDX                (NH_FLD_RFC2684_LLC << 5)
#define NH_FLD_RFC2684_ALL_FIELDS             ((NH_FLD_RFC2684_LLC << 6) - 1)

/***************************  User defined fields  ***************************/
#define NH_FLD_USER_DEFINED_SRCPORT           (1)
#define NH_FLD_USER_DEFINED_PCDID             (NH_FLD_USER_DEFINED_SRCPORT << 1)
#define NH_FLD_USER_DEFINED_ALL_FIELDS \
	((NH_FLD_USER_DEFINED_SRCPORT << 2) - 1)

/***************************  Payload fields  ********************************/
#define NH_FLD_PAYLOAD_BUFFER                 (1)
#define NH_FLD_PAYLOAD_SIZE                   (NH_FLD_PAYLOAD_BUFFER << 1)
#define NH_FLD_MAX_FRM_SIZE                   (NH_FLD_PAYLOAD_BUFFER << 2)
#define NH_FLD_MIN_FRM_SIZE                   (NH_FLD_PAYLOAD_BUFFER << 3)
#define NH_FLD_PAYLOAD_TYPE                   (NH_FLD_PAYLOAD_BUFFER << 4)
#define NH_FLD_FRAME_SIZE                     (NH_FLD_PAYLOAD_BUFFER << 5)
#define NH_FLD_PAYLOAD_ALL_FIELDS             ((NH_FLD_PAYLOAD_BUFFER << 6) - 1)

/***************************  GRE fields  ************************************/
#define NH_FLD_GRE_TYPE                       (1)
#define NH_FLD_GRE_ALL_FIELDS                 ((NH_FLD_GRE_TYPE << 1) - 1)

/***************************  MINENCAP fields  *******************************/
#define NH_FLD_MINENCAP_SRC_IP                (1)
#define NH_FLD_MINENCAP_DST_IP                (NH_FLD_MINENCAP_SRC_IP << 1)
#define NH_FLD_MINENCAP_TYPE                  (NH_FLD_MINENCAP_SRC_IP << 2)
#define NH_FLD_MINENCAP_ALL_FIELDS \
	((NH_FLD_MINENCAP_SRC_IP << 3) - 1)

/***************************  IPSEC AH fields  *******************************/
#define NH_FLD_IPSEC_AH_SPI                   (1)
#define NH_FLD_IPSEC_AH_NH                    (NH_FLD_IPSEC_AH_SPI << 1)
#define NH_FLD_IPSEC_AH_ALL_FIELDS            ((NH_FLD_IPSEC_AH_SPI << 2) - 1)

/***************************  IPSEC ESP fields  ******************************/
#define NH_FLD_IPSEC_ESP_SPI                  (1)
#define NH_FLD_IPSEC_ESP_SEQUENCE_NUM         (NH_FLD_IPSEC_ESP_SPI << 1)
#define NH_FLD_IPSEC_ESP_ALL_FIELDS           ((NH_FLD_IPSEC_ESP_SPI << 2) - 1)

#define NH_FLD_IPSEC_ESP_SPI_SIZE             4

/***************************  MPLS fields  ***********************************/
#define NH_FLD_MPLS_LABEL_STACK               (1)
#define NH_FLD_MPLS_LABEL_STACK_ALL_FIELDS \
	((NH_FLD_MPLS_LABEL_STACK << 1) - 1)

/***************************  MACSEC fields  *********************************/
#define NH_FLD_MACSEC_SECTAG                  (1)
#define NH_FLD_MACSEC_ALL_FIELDS              ((NH_FLD_MACSEC_SECTAG << 1) - 1)

/***************************  GTP fields  ************************************/
#define NH_FLD_GTP_TEID                       (1)

/* Protocol options */

/* Ethernet options */
#define	NH_OPT_ETH_BROADCAST			1
#define	NH_OPT_ETH_MULTICAST			2
#define	NH_OPT_ETH_UNICAST			3
#define	NH_OPT_ETH_BPDU				4

#define NH_ETH_IS_MULTICAST_ADDR(addr) (addr[0] & 0x01)
/* also applicable for broadcast */

/* VLAN options */
#define	NH_OPT_VLAN_CFI				1

/* IPV4 options */
#define	NH_OPT_IPV4_UNICAST			1
#define	NH_OPT_IPV4_MULTICAST			2
#define	NH_OPT_IPV4_BROADCAST			3
#define	NH_OPT_IPV4_OPTION			4
#define	NH_OPT_IPV4_FRAG			5
#define	NH_OPT_IPV4_INITIAL_FRAG		6

/* IPV6 options */
#define	NH_OPT_IPV6_UNICAST			1
#define	NH_OPT_IPV6_MULTICAST			2
#define	NH_OPT_IPV6_OPTION			3
#define	NH_OPT_IPV6_FRAG			4
#define	NH_OPT_IPV6_INITIAL_FRAG		5

/* General IP options (may be used for any version) */
#define	NH_OPT_IP_FRAG				1
#define	NH_OPT_IP_INITIAL_FRAG			2
#define	NH_OPT_IP_OPTION			3

/* Minenc. options */
#define	NH_OPT_MINENCAP_SRC_ADDR_PRESENT	1

/* GRE. options */
#define	NH_OPT_GRE_ROUTING_PRESENT		1

/* TCP options */
#define	NH_OPT_TCP_OPTIONS			1
#define	NH_OPT_TCP_CONTROL_HIGH_BITS		2
#define	NH_OPT_TCP_CONTROL_LOW_BITS		3

/* CAPWAP options */
#define	NH_OPT_CAPWAP_DTLS			1

enum net_prot {
	NET_PROT_NONE = 0,
	NET_PROT_PAYLOAD,
	NET_PROT_ETH,
	NET_PROT_VLAN,
	NET_PROT_IPV4,
	NET_PROT_IPV6,
	NET_PROT_IP,
	NET_PROT_TCP,
	NET_PROT_UDP,
	NET_PROT_UDP_LITE,
	NET_PROT_IPHC,
	NET_PROT_SCTP,
	NET_PROT_SCTP_CHUNK_DATA,
	NET_PROT_PPPOE,
	NET_PROT_PPP,
	NET_PROT_PPPMUX,
	NET_PROT_PPPMUX_SUBFRM,
	NET_PROT_L2TPV2,
	NET_PROT_L2TPV3_CTRL,
	NET_PROT_L2TPV3_SESS,
	NET_PROT_LLC,
	NET_PROT_LLC_SNAP,
	NET_PROT_NLPID,
	NET_PROT_SNAP,
	NET_PROT_MPLS,
	NET_PROT_IPSEC_AH,
	NET_PROT_IPSEC_ESP,
	NET_PROT_UDP_ENC_ESP, /* RFC 3948 */
	NET_PROT_MACSEC,
	NET_PROT_GRE,
	NET_PROT_MINENCAP,
	NET_PROT_DCCP,
	NET_PROT_ICMP,
	NET_PROT_IGMP,
	NET_PROT_ARP,
	NET_PROT_CAPWAP_DATA,
	NET_PROT_CAPWAP_CTRL,
	NET_PROT_RFC2684,
	NET_PROT_ICMPV6,
	NET_PROT_FCOE,
	NET_PROT_FIP,
	NET_PROT_ISCSI,
	NET_PROT_GTP,
	NET_PROT_USER_DEFINED_L2,
	NET_PROT_USER_DEFINED_L3,
	NET_PROT_USER_DEFINED_L4,
	NET_PROT_USER_DEFINED_L5,
	NET_PROT_USER_DEFINED_SHIM1,
	NET_PROT_USER_DEFINED_SHIM2,

	NET_PROT_DUMMY_LAST
};

/*! IEEE8021.Q */
#define NH_IEEE8021Q_ETYPE  0x8100
#define NH_IEEE8021Q_HDR(etype, pcp, dei, vlan_id)	\
	    ((((u32)((etype) & 0xFFFF)) << 16) |	\
	    (((u32)((pcp) & 0x07)) << 13) |		\
	    (((u32)((dei) & 0x01)) << 12) |		\
	    (((u32)((vlan_id) & 0xFFF))))

#endif /* __FSL_NET_H */
