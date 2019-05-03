/*
 * Copyright 2015 - 2016 Amazon.com, Inc. or its affiliates.
 *
 * This software is available to you under a choice of one of two
 * licenses.  You may choose to be licensed under the terms of the GNU
 * General Public License (GPL) Version 2, available from the file
 * COPYING in the main directory of this source tree, or the
 * BSD license below:
 *
 *     Redistribution and use in source and binary forms, with or
 *     without modification, are permitted provided that the following
 *     conditions are met:
 *
 *      - Redistributions of source code must retain the above
 *        copyright notice, this list of conditions and the following
 *        disclaimer.
 *
 *      - Redistributions in binary form must reproduce the above
 *        copyright notice, this list of conditions and the following
 *        disclaimer in the documentation and/or other materials
 *        provided with the distribution.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
 * BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
 * ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */
#ifndef _ENA_ETH_IO_H_
#define _ENA_ETH_IO_H_

enum ena_eth_io_l3_proto_index {
	ENA_ETH_IO_L3_PROTO_UNKNOWN                 = 0,
	ENA_ETH_IO_L3_PROTO_IPV4                    = 8,
	ENA_ETH_IO_L3_PROTO_IPV6                    = 11,
	ENA_ETH_IO_L3_PROTO_FCOE                    = 21,
	ENA_ETH_IO_L3_PROTO_ROCE                    = 22,
};

enum ena_eth_io_l4_proto_index {
	ENA_ETH_IO_L4_PROTO_UNKNOWN                 = 0,
	ENA_ETH_IO_L4_PROTO_TCP                     = 12,
	ENA_ETH_IO_L4_PROTO_UDP                     = 13,
	ENA_ETH_IO_L4_PROTO_ROUTEABLE_ROCE          = 23,
};

struct ena_eth_io_tx_desc {
	/* 15:0 : length - Buffer length in bytes, must
	 *    include any packet trailers that the ENA supposed
	 *    to update like End-to-End CRC, Authentication GMAC
	 *    etc. This length must not include the
	 *    'Push_Buffer' length. This length must not include
	 *    the 4-byte added in the end for 802.3 Ethernet FCS
	 * 21:16 : req_id_hi - Request ID[15:10]
	 * 22 : reserved22 - MBZ
	 * 23 : meta_desc - MBZ
	 * 24 : phase
	 * 25 : reserved1 - MBZ
	 * 26 : first - Indicates first descriptor in
	 *    transaction
	 * 27 : last - Indicates last descriptor in
	 *    transaction
	 * 28 : comp_req - Indicates whether completion
	 *    should be posted, after packet is transmitted.
	 *    Valid only for first descriptor
	 * 30:29 : reserved29 - MBZ
	 * 31 : reserved31 - MBZ
	 */
	u32 len_ctrl;

	/* 3:0 : l3_proto_idx - L3 protocol. This field
	 *    required when l3_csum_en,l3_csum or tso_en are set.
	 * 4 : DF - IPv4 DF, must be 0 if packet is IPv4 and
	 *    DF flags of the IPv4 header is 0. Otherwise must
	 *    be set to 1
	 * 6:5 : reserved5
	 * 7 : tso_en - Enable TSO, For TCP only.
	 * 12:8 : l4_proto_idx - L4 protocol. This field need
	 *    to be set when l4_csum_en or tso_en are set.
	 * 13 : l3_csum_en - enable IPv4 header checksum.
	 * 14 : l4_csum_en - enable TCP/UDP checksum.
	 * 15 : ethernet_fcs_dis - when set, the controller
	 *    will not append the 802.3 Ethernet Frame Check
	 *    Sequence to the packet
	 * 16 : reserved16
	 * 17 : l4_csum_partial - L4 partial checksum. when
	 *    set to 0, the ENA calculates the L4 checksum,
	 *    where the Destination Address required for the
	 *    TCP/UDP pseudo-header is taken from the actual
	 *    packet L3 header. when set to 1, the ENA doesn't
	 *    calculate the sum of the pseudo-header, instead,
	 *    the checksum field of the L4 is used instead. When
	 *    TSO enabled, the checksum of the pseudo-header
	 *    must not include the tcp length field. L4 partial
	 *    checksum should be used for IPv6 packet that
	 *    contains Routing Headers.
	 * 20:18 : reserved18 - MBZ
	 * 21 : reserved21 - MBZ
	 * 31:22 : req_id_lo - Request ID[9:0]
	 */
	u32 meta_ctrl;

	u32 buff_addr_lo;

	/* address high and header size
	 * 15:0 : addr_hi - Buffer Pointer[47:32]
	 * 23:16 : reserved16_w2
	 * 31:24 : header_length - Header length. For Low
	 *    Latency Queues, this fields indicates the number
	 *    of bytes written to the headers' memory. For
	 *    normal queues, if packet is TCP or UDP, and longer
	 *    than max_header_size, then this field should be
	 *    set to the sum of L4 header offset and L4 header
	 *    size(without options), otherwise, this field
	 *    should be set to 0. For both modes, this field
	 *    must not exceed the max_header_size.
	 *    max_header_size value is reported by the Max
	 *    Queues Feature descriptor
	 */
	u32 buff_addr_hi_hdr_sz;
};

struct ena_eth_io_tx_meta_desc {
	/* 9:0 : req_id_lo - Request ID[9:0]
	 * 11:10 : reserved10 - MBZ
	 * 12 : reserved12 - MBZ
	 * 13 : reserved13 - MBZ
	 * 14 : ext_valid - if set, offset fields in Word2
	 *    are valid Also MSS High in Word 0 and bits [31:24]
	 *    in Word 3
	 * 15 : reserved15
	 * 19:16 : mss_hi
	 * 20 : eth_meta_type - 0: Tx Metadata Descriptor, 1:
	 *    Extended Metadata Descriptor
	 * 21 : meta_store - Store extended metadata in queue
	 *    cache
	 * 22 : reserved22 - MBZ
	 * 23 : meta_desc - MBO
	 * 24 : phase
	 * 25 : reserved25 - MBZ
	 * 26 : first - Indicates first descriptor in
	 *    transaction
	 * 27 : last - Indicates last descriptor in
	 *    transaction
	 * 28 : comp_req - Indicates whether completion
	 *    should be posted, after packet is transmitted.
	 *    Valid only for first descriptor
	 * 30:29 : reserved29 - MBZ
	 * 31 : reserved31 - MBZ
	 */
	u32 len_ctrl;

	/* 5:0 : req_id_hi
	 * 31:6 : reserved6 - MBZ
	 */
	u32 word1;

	/* 7:0 : l3_hdr_len
	 * 15:8 : l3_hdr_off
	 * 21:16 : l4_hdr_len_in_words - counts the L4 header
	 *    length in words. there is an explicit assumption
	 *    that L4 header appears right after L3 header and
	 *    L4 offset is based on l3_hdr_off+l3_hdr_len
	 * 31:22 : mss_lo
	 */
	u32 word2;

	u32 reserved;
};

struct ena_eth_io_tx_cdesc {
	/* Request ID[15:0] */
	u16 req_id;

	u8 status;

	/* flags
	 * 0 : phase
	 * 7:1 : reserved1
	 */
	u8 flags;

	u16 sub_qid;

	u16 sq_head_idx;
};

struct ena_eth_io_rx_desc {
	/* In bytes. 0 means 64KB */
	u16 length;

	/* MBZ */
	u8 reserved2;

	/* 0 : phase
	 * 1 : reserved1 - MBZ
	 * 2 : first - Indicates first descriptor in
	 *    transaction
	 * 3 : last - Indicates last descriptor in transaction
	 * 4 : comp_req
	 * 5 : reserved5 - MBO
	 * 7:6 : reserved6 - MBZ
	 */
	u8 ctrl;

	u16 req_id;

	/* MBZ */
	u16 reserved6;

	u32 buff_addr_lo;

	u16 buff_addr_hi;

	/* MBZ */
	u16 reserved16_w3;
};

/* 4-word format Note: all ethernet parsing information are valid only when
 * last=1
 */
struct ena_eth_io_rx_cdesc_base {
	/* 4:0 : l3_proto_idx
	 * 6:5 : src_vlan_cnt
	 * 7 : reserved7 - MBZ
	 * 12:8 : l4_proto_idx
	 * 13 : l3_csum_err - when set, either the L3
	 *    checksum error detected, or, the controller didn't
	 *    validate the checksum. This bit is valid only when
	 *    l3_proto_idx indicates IPv4 packet
	 * 14 : l4_csum_err - when set, either the L4
	 *    checksum error detected, or, the controller didn't
	 *    validate the checksum. This bit is valid only when
	 *    l4_proto_idx indicates TCP/UDP packet, and,
	 *    ipv4_frag is not set. This bit is valid only when
	 *    l4_csum_checked below is set.
	 * 15 : ipv4_frag - Indicates IPv4 fragmented packet
	 * 16 : l4_csum_checked - L4 checksum was verified
	 *    (could be OK or error), when cleared the status of
	 *    checksum is unknown
	 * 23:17 : reserved17 - MBZ
	 * 24 : phase
	 * 25 : l3_csum2 - second checksum engine result
	 * 26 : first - Indicates first descriptor in
	 *    transaction
	 * 27 : last - Indicates last descriptor in
	 *    transaction
	 * 29:28 : reserved28
	 * 30 : buffer - 0: Metadata descriptor. 1: Buffer
	 *    Descriptor was used
	 * 31 : reserved31
	 */
	u32 status;

	u16 length;

	u16 req_id;

	/* 32-bit hash result */
	u32 hash;

	u16 sub_qid;

	u16 reserved;
};

/* 8-word format */
struct ena_eth_io_rx_cdesc_ext {
	struct ena_eth_io_rx_cdesc_base base;

	u32 buff_addr_lo;

	u16 buff_addr_hi;

	u16 reserved16;

	u32 reserved_w6;

	u32 reserved_w7;
};

struct ena_eth_io_intr_reg {
	/* 14:0 : rx_intr_delay
	 * 29:15 : tx_intr_delay
	 * 30 : intr_unmask
	 * 31 : reserved
	 */
	u32 intr_control;
};

struct ena_eth_io_numa_node_cfg_reg {
	/* 7:0 : numa
	 * 30:8 : reserved
	 * 31 : enabled
	 */
	u32 numa_cfg;
};

/* tx_desc */
#define ENA_ETH_IO_TX_DESC_LENGTH_MASK                      GENMASK(15, 0)
#define ENA_ETH_IO_TX_DESC_REQ_ID_HI_SHIFT                  16
#define ENA_ETH_IO_TX_DESC_REQ_ID_HI_MASK                   GENMASK(21, 16)
#define ENA_ETH_IO_TX_DESC_META_DESC_SHIFT                  23
#define ENA_ETH_IO_TX_DESC_META_DESC_MASK                   BIT(23)
#define ENA_ETH_IO_TX_DESC_PHASE_SHIFT                      24
#define ENA_ETH_IO_TX_DESC_PHASE_MASK                       BIT(24)
#define ENA_ETH_IO_TX_DESC_FIRST_SHIFT                      26
#define ENA_ETH_IO_TX_DESC_FIRST_MASK                       BIT(26)
#define ENA_ETH_IO_TX_DESC_LAST_SHIFT                       27
#define ENA_ETH_IO_TX_DESC_LAST_MASK                        BIT(27)
#define ENA_ETH_IO_TX_DESC_COMP_REQ_SHIFT                   28
#define ENA_ETH_IO_TX_DESC_COMP_REQ_MASK                    BIT(28)
#define ENA_ETH_IO_TX_DESC_L3_PROTO_IDX_MASK                GENMASK(3, 0)
#define ENA_ETH_IO_TX_DESC_DF_SHIFT                         4
#define ENA_ETH_IO_TX_DESC_DF_MASK                          BIT(4)
#define ENA_ETH_IO_TX_DESC_TSO_EN_SHIFT                     7
#define ENA_ETH_IO_TX_DESC_TSO_EN_MASK                      BIT(7)
#define ENA_ETH_IO_TX_DESC_L4_PROTO_IDX_SHIFT               8
#define ENA_ETH_IO_TX_DESC_L4_PROTO_IDX_MASK                GENMASK(12, 8)
#define ENA_ETH_IO_TX_DESC_L3_CSUM_EN_SHIFT                 13
#define ENA_ETH_IO_TX_DESC_L3_CSUM_EN_MASK                  BIT(13)
#define ENA_ETH_IO_TX_DESC_L4_CSUM_EN_SHIFT                 14
#define ENA_ETH_IO_TX_DESC_L4_CSUM_EN_MASK                  BIT(14)
#define ENA_ETH_IO_TX_DESC_ETHERNET_FCS_DIS_SHIFT           15
#define ENA_ETH_IO_TX_DESC_ETHERNET_FCS_DIS_MASK            BIT(15)
#define ENA_ETH_IO_TX_DESC_L4_CSUM_PARTIAL_SHIFT            17
#define ENA_ETH_IO_TX_DESC_L4_CSUM_PARTIAL_MASK             BIT(17)
#define ENA_ETH_IO_TX_DESC_REQ_ID_LO_SHIFT                  22
#define ENA_ETH_IO_TX_DESC_REQ_ID_LO_MASK                   GENMASK(31, 22)
#define ENA_ETH_IO_TX_DESC_ADDR_HI_MASK                     GENMASK(15, 0)
#define ENA_ETH_IO_TX_DESC_HEADER_LENGTH_SHIFT              24
#define ENA_ETH_IO_TX_DESC_HEADER_LENGTH_MASK               GENMASK(31, 24)

/* tx_meta_desc */
#define ENA_ETH_IO_TX_META_DESC_REQ_ID_LO_MASK              GENMASK(9, 0)
#define ENA_ETH_IO_TX_META_DESC_EXT_VALID_SHIFT             14
#define ENA_ETH_IO_TX_META_DESC_EXT_VALID_MASK              BIT(14)
#define ENA_ETH_IO_TX_META_DESC_MSS_HI_SHIFT                16
#define ENA_ETH_IO_TX_META_DESC_MSS_HI_MASK                 GENMASK(19, 16)
#define ENA_ETH_IO_TX_META_DESC_ETH_META_TYPE_SHIFT         20
#define ENA_ETH_IO_TX_META_DESC_ETH_META_TYPE_MASK          BIT(20)
#define ENA_ETH_IO_TX_META_DESC_META_STORE_SHIFT            21
#define ENA_ETH_IO_TX_META_DESC_META_STORE_MASK             BIT(21)
#define ENA_ETH_IO_TX_META_DESC_META_DESC_SHIFT             23
#define ENA_ETH_IO_TX_META_DESC_META_DESC_MASK              BIT(23)
#define ENA_ETH_IO_TX_META_DESC_PHASE_SHIFT                 24
#define ENA_ETH_IO_TX_META_DESC_PHASE_MASK                  BIT(24)
#define ENA_ETH_IO_TX_META_DESC_FIRST_SHIFT                 26
#define ENA_ETH_IO_TX_META_DESC_FIRST_MASK                  BIT(26)
#define ENA_ETH_IO_TX_META_DESC_LAST_SHIFT                  27
#define ENA_ETH_IO_TX_META_DESC_LAST_MASK                   BIT(27)
#define ENA_ETH_IO_TX_META_DESC_COMP_REQ_SHIFT              28
#define ENA_ETH_IO_TX_META_DESC_COMP_REQ_MASK               BIT(28)
#define ENA_ETH_IO_TX_META_DESC_REQ_ID_HI_MASK              GENMASK(5, 0)
#define ENA_ETH_IO_TX_META_DESC_L3_HDR_LEN_MASK             GENMASK(7, 0)
#define ENA_ETH_IO_TX_META_DESC_L3_HDR_OFF_SHIFT            8
#define ENA_ETH_IO_TX_META_DESC_L3_HDR_OFF_MASK             GENMASK(15, 8)
#define ENA_ETH_IO_TX_META_DESC_L4_HDR_LEN_IN_WORDS_SHIFT   16
#define ENA_ETH_IO_TX_META_DESC_L4_HDR_LEN_IN_WORDS_MASK    GENMASK(21, 16)
#define ENA_ETH_IO_TX_META_DESC_MSS_LO_SHIFT                22
#define ENA_ETH_IO_TX_META_DESC_MSS_LO_MASK                 GENMASK(31, 22)

/* tx_cdesc */
#define ENA_ETH_IO_TX_CDESC_PHASE_MASK                      BIT(0)

/* rx_desc */
#define ENA_ETH_IO_RX_DESC_PHASE_MASK                       BIT(0)
#define ENA_ETH_IO_RX_DESC_FIRST_SHIFT                      2
#define ENA_ETH_IO_RX_DESC_FIRST_MASK                       BIT(2)
#define ENA_ETH_IO_RX_DESC_LAST_SHIFT                       3
#define ENA_ETH_IO_RX_DESC_LAST_MASK                        BIT(3)
#define ENA_ETH_IO_RX_DESC_COMP_REQ_SHIFT                   4
#define ENA_ETH_IO_RX_DESC_COMP_REQ_MASK                    BIT(4)

/* rx_cdesc_base */
#define ENA_ETH_IO_RX_CDESC_BASE_L3_PROTO_IDX_MASK          GENMASK(4, 0)
#define ENA_ETH_IO_RX_CDESC_BASE_SRC_VLAN_CNT_SHIFT         5
#define ENA_ETH_IO_RX_CDESC_BASE_SRC_VLAN_CNT_MASK          GENMASK(6, 5)
#define ENA_ETH_IO_RX_CDESC_BASE_L4_PROTO_IDX_SHIFT         8
#define ENA_ETH_IO_RX_CDESC_BASE_L4_PROTO_IDX_MASK          GENMASK(12, 8)
#define ENA_ETH_IO_RX_CDESC_BASE_L3_CSUM_ERR_SHIFT          13
#define ENA_ETH_IO_RX_CDESC_BASE_L3_CSUM_ERR_MASK           BIT(13)
#define ENA_ETH_IO_RX_CDESC_BASE_L4_CSUM_ERR_SHIFT          14
#define ENA_ETH_IO_RX_CDESC_BASE_L4_CSUM_ERR_MASK           BIT(14)
#define ENA_ETH_IO_RX_CDESC_BASE_IPV4_FRAG_SHIFT            15
#define ENA_ETH_IO_RX_CDESC_BASE_IPV4_FRAG_MASK             BIT(15)
#define ENA_ETH_IO_RX_CDESC_BASE_L4_CSUM_CHECKED_SHIFT      16
#define ENA_ETH_IO_RX_CDESC_BASE_L4_CSUM_CHECKED_MASK       BIT(16)
#define ENA_ETH_IO_RX_CDESC_BASE_PHASE_SHIFT                24
#define ENA_ETH_IO_RX_CDESC_BASE_PHASE_MASK                 BIT(24)
#define ENA_ETH_IO_RX_CDESC_BASE_L3_CSUM2_SHIFT             25
#define ENA_ETH_IO_RX_CDESC_BASE_L3_CSUM2_MASK              BIT(25)
#define ENA_ETH_IO_RX_CDESC_BASE_FIRST_SHIFT                26
#define ENA_ETH_IO_RX_CDESC_BASE_FIRST_MASK                 BIT(26)
#define ENA_ETH_IO_RX_CDESC_BASE_LAST_SHIFT                 27
#define ENA_ETH_IO_RX_CDESC_BASE_LAST_MASK                  BIT(27)
#define ENA_ETH_IO_RX_CDESC_BASE_BUFFER_SHIFT               30
#define ENA_ETH_IO_RX_CDESC_BASE_BUFFER_MASK                BIT(30)

/* intr_reg */
#define ENA_ETH_IO_INTR_REG_RX_INTR_DELAY_MASK              GENMASK(14, 0)
#define ENA_ETH_IO_INTR_REG_TX_INTR_DELAY_SHIFT             15
#define ENA_ETH_IO_INTR_REG_TX_INTR_DELAY_MASK              GENMASK(29, 15)
#define ENA_ETH_IO_INTR_REG_INTR_UNMASK_SHIFT               30
#define ENA_ETH_IO_INTR_REG_INTR_UNMASK_MASK                BIT(30)

/* numa_node_cfg_reg */
#define ENA_ETH_IO_NUMA_NODE_CFG_REG_NUMA_MASK              GENMASK(7, 0)
#define ENA_ETH_IO_NUMA_NODE_CFG_REG_ENABLED_SHIFT          31
#define ENA_ETH_IO_NUMA_NODE_CFG_REG_ENABLED_MASK           BIT(31)

#endif /*_ENA_ETH_IO_H_ */
