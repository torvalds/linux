/* SPDX-License-Identifier: GPL-2.0 OR Linux-OpenIB */
/* Copyright (c) 2024 NVIDIA Corporation & Affiliates */

#ifndef HWS_DEFINER_H_
#define HWS_DEFINER_H_

/* Max available selecotrs */
#define DW_SELECTORS 9
#define BYTE_SELECTORS 8

/* Selectors based on match TAG */
#define DW_SELECTORS_MATCH 6
#define DW_SELECTORS_LIMITED 3

/* Selectors based on range TAG */
#define DW_SELECTORS_RANGE 2
#define BYTE_SELECTORS_RANGE 8

#define HWS_NUM_OF_FLEX_PARSERS 8

enum mlx5hws_definer_fname {
	MLX5HWS_DEFINER_FNAME_ETH_SMAC_47_16_O,
	MLX5HWS_DEFINER_FNAME_ETH_SMAC_47_16_I,
	MLX5HWS_DEFINER_FNAME_ETH_SMAC_15_0_O,
	MLX5HWS_DEFINER_FNAME_ETH_SMAC_15_0_I,
	MLX5HWS_DEFINER_FNAME_ETH_DMAC_47_16_O,
	MLX5HWS_DEFINER_FNAME_ETH_DMAC_47_16_I,
	MLX5HWS_DEFINER_FNAME_ETH_DMAC_15_0_O,
	MLX5HWS_DEFINER_FNAME_ETH_DMAC_15_0_I,
	MLX5HWS_DEFINER_FNAME_ETH_TYPE_O,
	MLX5HWS_DEFINER_FNAME_ETH_TYPE_I,
	MLX5HWS_DEFINER_FNAME_ETH_L3_TYPE_O,
	MLX5HWS_DEFINER_FNAME_ETH_L3_TYPE_I,
	MLX5HWS_DEFINER_FNAME_VLAN_TYPE_O,
	MLX5HWS_DEFINER_FNAME_VLAN_TYPE_I,
	MLX5HWS_DEFINER_FNAME_VLAN_FIRST_PRIO_O,
	MLX5HWS_DEFINER_FNAME_VLAN_FIRST_PRIO_I,
	MLX5HWS_DEFINER_FNAME_VLAN_CFI_O,
	MLX5HWS_DEFINER_FNAME_VLAN_CFI_I,
	MLX5HWS_DEFINER_FNAME_VLAN_ID_O,
	MLX5HWS_DEFINER_FNAME_VLAN_ID_I,
	MLX5HWS_DEFINER_FNAME_VLAN_SECOND_TYPE_O,
	MLX5HWS_DEFINER_FNAME_VLAN_SECOND_TYPE_I,
	MLX5HWS_DEFINER_FNAME_VLAN_SECOND_PRIO_O,
	MLX5HWS_DEFINER_FNAME_VLAN_SECOND_PRIO_I,
	MLX5HWS_DEFINER_FNAME_VLAN_SECOND_CFI_O,
	MLX5HWS_DEFINER_FNAME_VLAN_SECOND_CFI_I,
	MLX5HWS_DEFINER_FNAME_VLAN_SECOND_ID_O,
	MLX5HWS_DEFINER_FNAME_VLAN_SECOND_ID_I,
	MLX5HWS_DEFINER_FNAME_IPV4_IHL_O,
	MLX5HWS_DEFINER_FNAME_IPV4_IHL_I,
	MLX5HWS_DEFINER_FNAME_IP_DSCP_O,
	MLX5HWS_DEFINER_FNAME_IP_DSCP_I,
	MLX5HWS_DEFINER_FNAME_IP_ECN_O,
	MLX5HWS_DEFINER_FNAME_IP_ECN_I,
	MLX5HWS_DEFINER_FNAME_IP_TTL_O,
	MLX5HWS_DEFINER_FNAME_IP_TTL_I,
	MLX5HWS_DEFINER_FNAME_IPV4_DST_O,
	MLX5HWS_DEFINER_FNAME_IPV4_DST_I,
	MLX5HWS_DEFINER_FNAME_IPV4_SRC_O,
	MLX5HWS_DEFINER_FNAME_IPV4_SRC_I,
	MLX5HWS_DEFINER_FNAME_IP_VERSION_O,
	MLX5HWS_DEFINER_FNAME_IP_VERSION_I,
	MLX5HWS_DEFINER_FNAME_IP_FRAG_O,
	MLX5HWS_DEFINER_FNAME_IP_FRAG_I,
	MLX5HWS_DEFINER_FNAME_IP_LEN_O,
	MLX5HWS_DEFINER_FNAME_IP_LEN_I,
	MLX5HWS_DEFINER_FNAME_IP_TOS_O,
	MLX5HWS_DEFINER_FNAME_IP_TOS_I,
	MLX5HWS_DEFINER_FNAME_IPV6_FLOW_LABEL_O,
	MLX5HWS_DEFINER_FNAME_IPV6_FLOW_LABEL_I,
	MLX5HWS_DEFINER_FNAME_IPV6_DST_127_96_O,
	MLX5HWS_DEFINER_FNAME_IPV6_DST_95_64_O,
	MLX5HWS_DEFINER_FNAME_IPV6_DST_63_32_O,
	MLX5HWS_DEFINER_FNAME_IPV6_DST_31_0_O,
	MLX5HWS_DEFINER_FNAME_IPV6_DST_127_96_I,
	MLX5HWS_DEFINER_FNAME_IPV6_DST_95_64_I,
	MLX5HWS_DEFINER_FNAME_IPV6_DST_63_32_I,
	MLX5HWS_DEFINER_FNAME_IPV6_DST_31_0_I,
	MLX5HWS_DEFINER_FNAME_IPV6_SRC_127_96_O,
	MLX5HWS_DEFINER_FNAME_IPV6_SRC_95_64_O,
	MLX5HWS_DEFINER_FNAME_IPV6_SRC_63_32_O,
	MLX5HWS_DEFINER_FNAME_IPV6_SRC_31_0_O,
	MLX5HWS_DEFINER_FNAME_IPV6_SRC_127_96_I,
	MLX5HWS_DEFINER_FNAME_IPV6_SRC_95_64_I,
	MLX5HWS_DEFINER_FNAME_IPV6_SRC_63_32_I,
	MLX5HWS_DEFINER_FNAME_IPV6_SRC_31_0_I,
	MLX5HWS_DEFINER_FNAME_IP_PROTOCOL_O,
	MLX5HWS_DEFINER_FNAME_IP_PROTOCOL_I,
	MLX5HWS_DEFINER_FNAME_L4_SPORT_O,
	MLX5HWS_DEFINER_FNAME_L4_SPORT_I,
	MLX5HWS_DEFINER_FNAME_L4_DPORT_O,
	MLX5HWS_DEFINER_FNAME_L4_DPORT_I,
	MLX5HWS_DEFINER_FNAME_TCP_FLAGS_I,
	MLX5HWS_DEFINER_FNAME_TCP_FLAGS_O,
	MLX5HWS_DEFINER_FNAME_TCP_SEQ_NUM,
	MLX5HWS_DEFINER_FNAME_TCP_ACK_NUM,
	MLX5HWS_DEFINER_FNAME_GTP_TEID,
	MLX5HWS_DEFINER_FNAME_GTP_MSG_TYPE,
	MLX5HWS_DEFINER_FNAME_GTP_EXT_FLAG,
	MLX5HWS_DEFINER_FNAME_GTP_NEXT_EXT_HDR,
	MLX5HWS_DEFINER_FNAME_GTP_EXT_HDR_PDU,
	MLX5HWS_DEFINER_FNAME_GTP_EXT_HDR_QFI,
	MLX5HWS_DEFINER_FNAME_GTPU_DW0,
	MLX5HWS_DEFINER_FNAME_GTPU_FIRST_EXT_DW0,
	MLX5HWS_DEFINER_FNAME_GTPU_DW2,
	MLX5HWS_DEFINER_FNAME_FLEX_PARSER_0,
	MLX5HWS_DEFINER_FNAME_FLEX_PARSER_1,
	MLX5HWS_DEFINER_FNAME_FLEX_PARSER_2,
	MLX5HWS_DEFINER_FNAME_FLEX_PARSER_3,
	MLX5HWS_DEFINER_FNAME_FLEX_PARSER_4,
	MLX5HWS_DEFINER_FNAME_FLEX_PARSER_5,
	MLX5HWS_DEFINER_FNAME_FLEX_PARSER_6,
	MLX5HWS_DEFINER_FNAME_FLEX_PARSER_7,
	MLX5HWS_DEFINER_FNAME_VPORT_REG_C_0,
	MLX5HWS_DEFINER_FNAME_VXLAN_FLAGS,
	MLX5HWS_DEFINER_FNAME_VXLAN_VNI,
	MLX5HWS_DEFINER_FNAME_VXLAN_GPE_FLAGS,
	MLX5HWS_DEFINER_FNAME_VXLAN_GPE_RSVD0,
	MLX5HWS_DEFINER_FNAME_VXLAN_GPE_PROTO,
	MLX5HWS_DEFINER_FNAME_VXLAN_GPE_VNI,
	MLX5HWS_DEFINER_FNAME_VXLAN_GPE_RSVD1,
	MLX5HWS_DEFINER_FNAME_GENEVE_OPT_LEN,
	MLX5HWS_DEFINER_FNAME_GENEVE_OAM,
	MLX5HWS_DEFINER_FNAME_GENEVE_PROTO,
	MLX5HWS_DEFINER_FNAME_GENEVE_VNI,
	MLX5HWS_DEFINER_FNAME_SOURCE_QP,
	MLX5HWS_DEFINER_FNAME_SOURCE_GVMI,
	MLX5HWS_DEFINER_FNAME_REG_0,
	MLX5HWS_DEFINER_FNAME_REG_1,
	MLX5HWS_DEFINER_FNAME_REG_2,
	MLX5HWS_DEFINER_FNAME_REG_3,
	MLX5HWS_DEFINER_FNAME_REG_4,
	MLX5HWS_DEFINER_FNAME_REG_5,
	MLX5HWS_DEFINER_FNAME_REG_6,
	MLX5HWS_DEFINER_FNAME_REG_7,
	MLX5HWS_DEFINER_FNAME_REG_8,
	MLX5HWS_DEFINER_FNAME_REG_9,
	MLX5HWS_DEFINER_FNAME_REG_10,
	MLX5HWS_DEFINER_FNAME_REG_11,
	MLX5HWS_DEFINER_FNAME_REG_A,
	MLX5HWS_DEFINER_FNAME_REG_B,
	MLX5HWS_DEFINER_FNAME_GRE_KEY_PRESENT,
	MLX5HWS_DEFINER_FNAME_GRE_C,
	MLX5HWS_DEFINER_FNAME_GRE_K,
	MLX5HWS_DEFINER_FNAME_GRE_S,
	MLX5HWS_DEFINER_FNAME_GRE_PROTOCOL,
	MLX5HWS_DEFINER_FNAME_GRE_OPT_KEY,
	MLX5HWS_DEFINER_FNAME_GRE_OPT_SEQ,
	MLX5HWS_DEFINER_FNAME_GRE_OPT_CHECKSUM,
	MLX5HWS_DEFINER_FNAME_INTEGRITY_O,
	MLX5HWS_DEFINER_FNAME_INTEGRITY_I,
	MLX5HWS_DEFINER_FNAME_ICMP_DW1,
	MLX5HWS_DEFINER_FNAME_ICMP_DW2,
	MLX5HWS_DEFINER_FNAME_ICMP_DW3,
	MLX5HWS_DEFINER_FNAME_IPSEC_SPI,
	MLX5HWS_DEFINER_FNAME_IPSEC_SEQUENCE_NUMBER,
	MLX5HWS_DEFINER_FNAME_IPSEC_SYNDROME,
	MLX5HWS_DEFINER_FNAME_MPLS0_O,
	MLX5HWS_DEFINER_FNAME_MPLS1_O,
	MLX5HWS_DEFINER_FNAME_MPLS2_O,
	MLX5HWS_DEFINER_FNAME_MPLS3_O,
	MLX5HWS_DEFINER_FNAME_MPLS4_O,
	MLX5HWS_DEFINER_FNAME_MPLS0_I,
	MLX5HWS_DEFINER_FNAME_MPLS1_I,
	MLX5HWS_DEFINER_FNAME_MPLS2_I,
	MLX5HWS_DEFINER_FNAME_MPLS3_I,
	MLX5HWS_DEFINER_FNAME_MPLS4_I,
	MLX5HWS_DEFINER_FNAME_FLEX_PARSER0_OK,
	MLX5HWS_DEFINER_FNAME_FLEX_PARSER1_OK,
	MLX5HWS_DEFINER_FNAME_FLEX_PARSER2_OK,
	MLX5HWS_DEFINER_FNAME_FLEX_PARSER3_OK,
	MLX5HWS_DEFINER_FNAME_FLEX_PARSER4_OK,
	MLX5HWS_DEFINER_FNAME_FLEX_PARSER5_OK,
	MLX5HWS_DEFINER_FNAME_FLEX_PARSER6_OK,
	MLX5HWS_DEFINER_FNAME_FLEX_PARSER7_OK,
	MLX5HWS_DEFINER_FNAME_OKS2_MPLS0_O,
	MLX5HWS_DEFINER_FNAME_OKS2_MPLS1_O,
	MLX5HWS_DEFINER_FNAME_OKS2_MPLS2_O,
	MLX5HWS_DEFINER_FNAME_OKS2_MPLS3_O,
	MLX5HWS_DEFINER_FNAME_OKS2_MPLS4_O,
	MLX5HWS_DEFINER_FNAME_OKS2_MPLS0_I,
	MLX5HWS_DEFINER_FNAME_OKS2_MPLS1_I,
	MLX5HWS_DEFINER_FNAME_OKS2_MPLS2_I,
	MLX5HWS_DEFINER_FNAME_OKS2_MPLS3_I,
	MLX5HWS_DEFINER_FNAME_OKS2_MPLS4_I,
	MLX5HWS_DEFINER_FNAME_GENEVE_OPT_OK_0,
	MLX5HWS_DEFINER_FNAME_GENEVE_OPT_OK_1,
	MLX5HWS_DEFINER_FNAME_GENEVE_OPT_OK_2,
	MLX5HWS_DEFINER_FNAME_GENEVE_OPT_OK_3,
	MLX5HWS_DEFINER_FNAME_GENEVE_OPT_OK_4,
	MLX5HWS_DEFINER_FNAME_GENEVE_OPT_OK_5,
	MLX5HWS_DEFINER_FNAME_GENEVE_OPT_OK_6,
	MLX5HWS_DEFINER_FNAME_GENEVE_OPT_OK_7,
	MLX5HWS_DEFINER_FNAME_GENEVE_OPT_DW_0,
	MLX5HWS_DEFINER_FNAME_GENEVE_OPT_DW_1,
	MLX5HWS_DEFINER_FNAME_GENEVE_OPT_DW_2,
	MLX5HWS_DEFINER_FNAME_GENEVE_OPT_DW_3,
	MLX5HWS_DEFINER_FNAME_GENEVE_OPT_DW_4,
	MLX5HWS_DEFINER_FNAME_GENEVE_OPT_DW_5,
	MLX5HWS_DEFINER_FNAME_GENEVE_OPT_DW_6,
	MLX5HWS_DEFINER_FNAME_GENEVE_OPT_DW_7,
	MLX5HWS_DEFINER_FNAME_IB_L4_OPCODE,
	MLX5HWS_DEFINER_FNAME_IB_L4_QPN,
	MLX5HWS_DEFINER_FNAME_IB_L4_A,
	MLX5HWS_DEFINER_FNAME_RANDOM_NUM,
	MLX5HWS_DEFINER_FNAME_PTYPE_L2_O,
	MLX5HWS_DEFINER_FNAME_PTYPE_L2_I,
	MLX5HWS_DEFINER_FNAME_PTYPE_L3_O,
	MLX5HWS_DEFINER_FNAME_PTYPE_L3_I,
	MLX5HWS_DEFINER_FNAME_PTYPE_L4_O,
	MLX5HWS_DEFINER_FNAME_PTYPE_L4_I,
	MLX5HWS_DEFINER_FNAME_PTYPE_L4_EXT_O,
	MLX5HWS_DEFINER_FNAME_PTYPE_L4_EXT_I,
	MLX5HWS_DEFINER_FNAME_PTYPE_FRAG_O,
	MLX5HWS_DEFINER_FNAME_PTYPE_FRAG_I,
	MLX5HWS_DEFINER_FNAME_TNL_HDR_0,
	MLX5HWS_DEFINER_FNAME_TNL_HDR_1,
	MLX5HWS_DEFINER_FNAME_TNL_HDR_2,
	MLX5HWS_DEFINER_FNAME_TNL_HDR_3,
	MLX5HWS_DEFINER_FNAME_MAX,
};

enum mlx5hws_definer_match_criteria {
	MLX5HWS_DEFINER_MATCH_CRITERIA_EMPTY = 0,
	MLX5HWS_DEFINER_MATCH_CRITERIA_OUTER = 1 << 0,
	MLX5HWS_DEFINER_MATCH_CRITERIA_MISC = 1 << 1,
	MLX5HWS_DEFINER_MATCH_CRITERIA_INNER = 1 << 2,
	MLX5HWS_DEFINER_MATCH_CRITERIA_MISC2 = 1 << 3,
	MLX5HWS_DEFINER_MATCH_CRITERIA_MISC3 = 1 << 4,
	MLX5HWS_DEFINER_MATCH_CRITERIA_MISC4 = 1 << 5,
	MLX5HWS_DEFINER_MATCH_CRITERIA_MISC5 = 1 << 6,
	MLX5HWS_DEFINER_MATCH_CRITERIA_MISC6 = 1 << 7,
};

enum mlx5hws_definer_type {
	MLX5HWS_DEFINER_TYPE_MATCH,
	MLX5HWS_DEFINER_TYPE_JUMBO,
};

enum mlx5hws_definer_match_flag {
	MLX5HWS_DEFINER_MATCH_FLAG_TNL_VXLAN_GPE = 1 << 0,
	MLX5HWS_DEFINER_MATCH_FLAG_TNL_GENEVE = 1 << 1,
	MLX5HWS_DEFINER_MATCH_FLAG_TNL_GTPU = 1 << 2,
	MLX5HWS_DEFINER_MATCH_FLAG_TNL_GRE = 1 << 3,
	MLX5HWS_DEFINER_MATCH_FLAG_TNL_VXLAN = 1 << 4,
	MLX5HWS_DEFINER_MATCH_FLAG_TNL_HEADER_0_1 = 1 << 5,

	MLX5HWS_DEFINER_MATCH_FLAG_TNL_GRE_OPT_KEY = 1 << 6,
	MLX5HWS_DEFINER_MATCH_FLAG_TNL_HEADER_2 = 1 << 7,

	MLX5HWS_DEFINER_MATCH_FLAG_TNL_MPLS_OVER_GRE = 1 << 8,
	MLX5HWS_DEFINER_MATCH_FLAG_TNL_MPLS_OVER_UDP = 1 << 9,

	MLX5HWS_DEFINER_MATCH_FLAG_ICMPV4 = 1 << 10,
	MLX5HWS_DEFINER_MATCH_FLAG_ICMPV6 = 1 << 11,
	MLX5HWS_DEFINER_MATCH_FLAG_TCP_O = 1 << 12,
	MLX5HWS_DEFINER_MATCH_FLAG_TCP_I = 1 << 13,
};

struct mlx5hws_definer_fc {
	struct mlx5hws_context *ctx;
	/* Source */
	u32 s_byte_off;
	int s_bit_off;
	u32 s_bit_mask;
	/* Destination */
	u32 byte_off;
	int bit_off;
	u32 bit_mask;
	enum mlx5hws_definer_fname fname;
	void (*tag_set)(struct mlx5hws_definer_fc *fc,
			void *mach_param,
			u8 *tag);
	void (*tag_mask_set)(struct mlx5hws_definer_fc *fc,
			     void *mach_param,
			     u8 *tag);
};

struct mlx5_ifc_definer_hl_eth_l2_bits {
	u8 dmac_47_16[0x20];
	u8 dmac_15_0[0x10];
	u8 l3_ethertype[0x10];
	u8 reserved_at_40[0x1];
	u8 sx_sniffer[0x1];
	u8 functional_lb[0x1];
	u8 ip_fragmented[0x1];
	u8 qp_type[0x2];
	u8 encap_type[0x2];
	u8 port_number[0x2];
	u8 l3_type[0x2];
	u8 l4_type_bwc[0x2];
	u8 first_vlan_qualifier[0x2];
	u8 first_priority[0x3];
	u8 first_cfi[0x1];
	u8 first_vlan_id[0xc];
	u8 l4_type[0x4];
	u8 reserved_at_64[0x2];
	u8 ipsec_layer[0x2];
	u8 l2_type[0x2];
	u8 force_lb[0x1];
	u8 l2_ok[0x1];
	u8 l3_ok[0x1];
	u8 l4_ok[0x1];
	u8 second_vlan_qualifier[0x2];
	u8 second_priority[0x3];
	u8 second_cfi[0x1];
	u8 second_vlan_id[0xc];
};

struct mlx5_ifc_definer_hl_eth_l2_src_bits {
	u8 smac_47_16[0x20];
	u8 smac_15_0[0x10];
	u8 loopback_syndrome[0x8];
	u8 l3_type[0x2];
	u8 l4_type_bwc[0x2];
	u8 first_vlan_qualifier[0x2];
	u8 ip_fragmented[0x1];
	u8 functional_lb[0x1];
};

struct mlx5_ifc_definer_hl_ib_l2_bits {
	u8 sx_sniffer[0x1];
	u8 force_lb[0x1];
	u8 functional_lb[0x1];
	u8 reserved_at_3[0x3];
	u8 port_number[0x2];
	u8 sl[0x4];
	u8 qp_type[0x2];
	u8 lnh[0x2];
	u8 dlid[0x10];
	u8 vl[0x4];
	u8 lrh_packet_length[0xc];
	u8 slid[0x10];
};

struct mlx5_ifc_definer_hl_eth_l3_bits {
	u8 ip_version[0x4];
	u8 ihl[0x4];
	union {
		u8 tos[0x8];
		struct {
			u8 dscp[0x6];
			u8 ecn[0x2];
		};
	};
	u8 time_to_live_hop_limit[0x8];
	u8 protocol_next_header[0x8];
	u8 identification[0x10];
	union {
		u8 ipv4_frag[0x10];
		struct {
			u8 flags[0x3];
			u8 fragment_offset[0xd];
		};
	};
	u8 ipv4_total_length[0x10];
	u8 checksum[0x10];
	u8 reserved_at_60[0xc];
	u8 flow_label[0x14];
	u8 packet_length[0x10];
	u8 ipv6_payload_length[0x10];
};

struct mlx5_ifc_definer_hl_eth_l4_bits {
	u8 source_port[0x10];
	u8 destination_port[0x10];
	u8 data_offset[0x4];
	u8 l4_ok[0x1];
	u8 l3_ok[0x1];
	u8 ip_fragmented[0x1];
	u8 tcp_ns[0x1];
	union {
		u8 tcp_flags[0x8];
		struct {
			u8 tcp_cwr[0x1];
			u8 tcp_ece[0x1];
			u8 tcp_urg[0x1];
			u8 tcp_ack[0x1];
			u8 tcp_psh[0x1];
			u8 tcp_rst[0x1];
			u8 tcp_syn[0x1];
			u8 tcp_fin[0x1];
		};
	};
	u8 first_fragment[0x1];
	u8 reserved_at_31[0xf];
};

struct mlx5_ifc_definer_hl_src_qp_gvmi_bits {
	u8 loopback_syndrome[0x8];
	u8 l3_type[0x2];
	u8 l4_type_bwc[0x2];
	u8 first_vlan_qualifier[0x2];
	u8 reserved_at_e[0x1];
	u8 functional_lb[0x1];
	u8 source_gvmi[0x10];
	u8 force_lb[0x1];
	u8 ip_fragmented[0x1];
	u8 source_is_requestor[0x1];
	u8 reserved_at_23[0x5];
	u8 source_qp[0x18];
};

struct mlx5_ifc_definer_hl_ib_l4_bits {
	u8 opcode[0x8];
	u8 qp[0x18];
	u8 se[0x1];
	u8 migreq[0x1];
	u8 ackreq[0x1];
	u8 fecn[0x1];
	u8 becn[0x1];
	u8 bth[0x1];
	u8 deth[0x1];
	u8 dcceth[0x1];
	u8 reserved_at_28[0x2];
	u8 pad_count[0x2];
	u8 tver[0x4];
	u8 p_key[0x10];
	u8 reserved_at_40[0x8];
	u8 deth_source_qp[0x18];
};

enum mlx5hws_integrity_ok1_bits {
	MLX5HWS_DEFINER_OKS1_FIRST_L4_OK = 24,
	MLX5HWS_DEFINER_OKS1_FIRST_L3_OK = 25,
	MLX5HWS_DEFINER_OKS1_SECOND_L4_OK = 26,
	MLX5HWS_DEFINER_OKS1_SECOND_L3_OK = 27,
	MLX5HWS_DEFINER_OKS1_FIRST_L4_CSUM_OK = 28,
	MLX5HWS_DEFINER_OKS1_FIRST_IPV4_CSUM_OK = 29,
	MLX5HWS_DEFINER_OKS1_SECOND_L4_CSUM_OK = 30,
	MLX5HWS_DEFINER_OKS1_SECOND_IPV4_CSUM_OK = 31,
};

struct mlx5_ifc_definer_hl_oks1_bits {
	union {
		u8 oks1_bits[0x20];
		struct {
			u8 second_ipv4_checksum_ok[0x1];
			u8 second_l4_checksum_ok[0x1];
			u8 first_ipv4_checksum_ok[0x1];
			u8 first_l4_checksum_ok[0x1];
			u8 second_l3_ok[0x1];
			u8 second_l4_ok[0x1];
			u8 first_l3_ok[0x1];
			u8 first_l4_ok[0x1];
			u8 flex_parser7_steering_ok[0x1];
			u8 flex_parser6_steering_ok[0x1];
			u8 flex_parser5_steering_ok[0x1];
			u8 flex_parser4_steering_ok[0x1];
			u8 flex_parser3_steering_ok[0x1];
			u8 flex_parser2_steering_ok[0x1];
			u8 flex_parser1_steering_ok[0x1];
			u8 flex_parser0_steering_ok[0x1];
			u8 second_ipv6_extension_header_vld[0x1];
			u8 first_ipv6_extension_header_vld[0x1];
			u8 l3_tunneling_ok[0x1];
			u8 l2_tunneling_ok[0x1];
			u8 second_tcp_ok[0x1];
			u8 second_udp_ok[0x1];
			u8 second_ipv4_ok[0x1];
			u8 second_ipv6_ok[0x1];
			u8 second_l2_ok[0x1];
			u8 vxlan_ok[0x1];
			u8 gre_ok[0x1];
			u8 first_tcp_ok[0x1];
			u8 first_udp_ok[0x1];
			u8 first_ipv4_ok[0x1];
			u8 first_ipv6_ok[0x1];
			u8 first_l2_ok[0x1];
		};
	};
};

struct mlx5_ifc_definer_hl_oks2_bits {
	u8 reserved_at_0[0xa];
	u8 second_mpls_ok[0x1];
	u8 second_mpls4_s_bit[0x1];
	u8 second_mpls4_qualifier[0x1];
	u8 second_mpls3_s_bit[0x1];
	u8 second_mpls3_qualifier[0x1];
	u8 second_mpls2_s_bit[0x1];
	u8 second_mpls2_qualifier[0x1];
	u8 second_mpls1_s_bit[0x1];
	u8 second_mpls1_qualifier[0x1];
	u8 second_mpls0_s_bit[0x1];
	u8 second_mpls0_qualifier[0x1];
	u8 first_mpls_ok[0x1];
	u8 first_mpls4_s_bit[0x1];
	u8 first_mpls4_qualifier[0x1];
	u8 first_mpls3_s_bit[0x1];
	u8 first_mpls3_qualifier[0x1];
	u8 first_mpls2_s_bit[0x1];
	u8 first_mpls2_qualifier[0x1];
	u8 first_mpls1_s_bit[0x1];
	u8 first_mpls1_qualifier[0x1];
	u8 first_mpls0_s_bit[0x1];
	u8 first_mpls0_qualifier[0x1];
};

struct mlx5_ifc_definer_hl_voq_bits {
	u8 reserved_at_0[0x18];
	u8 ecn_ok[0x1];
	u8 congestion[0x1];
	u8 profile[0x2];
	u8 internal_prio[0x4];
};

struct mlx5_ifc_definer_hl_ipv4_src_dst_bits {
	u8 source_address[0x20];
	u8 destination_address[0x20];
};

struct mlx5_ifc_definer_hl_random_number_bits {
	u8 random_number[0x10];
	u8 reserved[0x10];
};

struct mlx5_ifc_definer_hl_ipv6_addr_bits {
	u8 ipv6_address_127_96[0x20];
	u8 ipv6_address_95_64[0x20];
	u8 ipv6_address_63_32[0x20];
	u8 ipv6_address_31_0[0x20];
};

struct mlx5_ifc_definer_tcp_icmp_header_bits {
	union {
		struct {
			u8 icmp_dw1[0x20];
			u8 icmp_dw2[0x20];
			u8 icmp_dw3[0x20];
		};
		struct {
			u8 tcp_seq[0x20];
			u8 tcp_ack[0x20];
			u8 tcp_win_urg[0x20];
		};
	};
};

struct mlx5_ifc_definer_hl_tunnel_header_bits {
	u8 tunnel_header_0[0x20];
	u8 tunnel_header_1[0x20];
	u8 tunnel_header_2[0x20];
	u8 tunnel_header_3[0x20];
};

struct mlx5_ifc_definer_hl_ipsec_bits {
	u8 spi[0x20];
	u8 sequence_number[0x20];
	u8 reserved[0x10];
	u8 ipsec_syndrome[0x8];
	u8 next_header[0x8];
};

struct mlx5_ifc_definer_hl_metadata_bits {
	u8 metadata_to_cqe[0x20];
	u8 general_purpose[0x20];
	u8 acomulated_hash[0x20];
};

struct mlx5_ifc_definer_hl_flex_parser_bits {
	u8 flex_parser_7[0x20];
	u8 flex_parser_6[0x20];
	u8 flex_parser_5[0x20];
	u8 flex_parser_4[0x20];
	u8 flex_parser_3[0x20];
	u8 flex_parser_2[0x20];
	u8 flex_parser_1[0x20];
	u8 flex_parser_0[0x20];
};

struct mlx5_ifc_definer_hl_registers_bits {
	u8 register_c_10[0x20];
	u8 register_c_11[0x20];
	u8 register_c_8[0x20];
	u8 register_c_9[0x20];
	u8 register_c_6[0x20];
	u8 register_c_7[0x20];
	u8 register_c_4[0x20];
	u8 register_c_5[0x20];
	u8 register_c_2[0x20];
	u8 register_c_3[0x20];
	u8 register_c_0[0x20];
	u8 register_c_1[0x20];
};

struct mlx5_ifc_definer_hl_mpls_bits {
	u8 mpls0_label[0x20];
	u8 mpls1_label[0x20];
	u8 mpls2_label[0x20];
	u8 mpls3_label[0x20];
	u8 mpls4_label[0x20];
};

struct mlx5_ifc_definer_hl_bits {
	struct mlx5_ifc_definer_hl_eth_l2_bits eth_l2_outer;
	struct mlx5_ifc_definer_hl_eth_l2_bits eth_l2_inner;
	struct mlx5_ifc_definer_hl_eth_l2_src_bits eth_l2_src_outer;
	struct mlx5_ifc_definer_hl_eth_l2_src_bits eth_l2_src_inner;
	struct mlx5_ifc_definer_hl_ib_l2_bits ib_l2;
	struct mlx5_ifc_definer_hl_eth_l3_bits eth_l3_outer;
	struct mlx5_ifc_definer_hl_eth_l3_bits eth_l3_inner;
	struct mlx5_ifc_definer_hl_eth_l4_bits eth_l4_outer;
	struct mlx5_ifc_definer_hl_eth_l4_bits eth_l4_inner;
	struct mlx5_ifc_definer_hl_src_qp_gvmi_bits source_qp_gvmi;
	struct mlx5_ifc_definer_hl_ib_l4_bits ib_l4;
	struct mlx5_ifc_definer_hl_oks1_bits oks1;
	struct mlx5_ifc_definer_hl_oks2_bits oks2;
	struct mlx5_ifc_definer_hl_voq_bits voq;
	u8 reserved_at_480[0x380];
	struct mlx5_ifc_definer_hl_ipv4_src_dst_bits ipv4_src_dest_outer;
	struct mlx5_ifc_definer_hl_ipv4_src_dst_bits ipv4_src_dest_inner;
	struct mlx5_ifc_definer_hl_ipv6_addr_bits ipv6_dst_outer;
	struct mlx5_ifc_definer_hl_ipv6_addr_bits ipv6_dst_inner;
	struct mlx5_ifc_definer_hl_ipv6_addr_bits ipv6_src_outer;
	struct mlx5_ifc_definer_hl_ipv6_addr_bits ipv6_src_inner;
	u8 unsupported_dest_ib_l3[0x80];
	u8 unsupported_source_ib_l3[0x80];
	u8 unsupported_udp_misc_outer[0x20];
	u8 unsupported_udp_misc_inner[0x20];
	struct mlx5_ifc_definer_tcp_icmp_header_bits tcp_icmp;
	struct mlx5_ifc_definer_hl_tunnel_header_bits tunnel_header;
	struct mlx5_ifc_definer_hl_mpls_bits mpls_outer;
	struct mlx5_ifc_definer_hl_mpls_bits mpls_inner;
	u8 unsupported_config_headers_outer[0x80];
	u8 unsupported_config_headers_inner[0x80];
	struct mlx5_ifc_definer_hl_random_number_bits random_number;
	struct mlx5_ifc_definer_hl_ipsec_bits ipsec;
	struct mlx5_ifc_definer_hl_metadata_bits metadata;
	u8 unsupported_utc_timestamp[0x40];
	u8 unsupported_free_running_timestamp[0x40];
	struct mlx5_ifc_definer_hl_flex_parser_bits flex_parser;
	struct mlx5_ifc_definer_hl_registers_bits registers;
	/* Reserved in case header layout on future HW */
	u8 unsupported_reserved[0xd40];
};

enum mlx5hws_definer_gtp {
	MLX5HWS_DEFINER_GTP_EXT_HDR_BIT = 0x04,
};

struct mlx5_ifc_header_gtp_bits {
	u8 version[0x3];
	u8 proto_type[0x1];
	u8 reserved1[0x1];
	union {
		u8 msg_flags[0x3];
		struct {
			u8 ext_hdr_flag[0x1];
			u8 seq_num_flag[0x1];
			u8 pdu_flag[0x1];
		};
	};
	u8 msg_type[0x8];
	u8 msg_len[0x8];
	u8 teid[0x20];
};

struct mlx5_ifc_header_opt_gtp_bits {
	u8 seq_num[0x10];
	u8 pdu_num[0x8];
	u8 next_ext_hdr_type[0x8];
};

struct mlx5_ifc_header_gtp_psc_bits {
	u8 len[0x8];
	u8 pdu_type[0x4];
	u8 flags[0x4];
	u8 qfi[0x8];
	u8 reserved2[0x8];
};

struct mlx5_ifc_header_ipv6_vtc_bits {
	u8 version[0x4];
	union {
		u8 tos[0x8];
		struct {
			u8 dscp[0x6];
			u8 ecn[0x2];
		};
	};
	u8 flow_label[0x14];
};

struct mlx5_ifc_header_ipv6_routing_ext_bits {
	u8 next_hdr[0x8];
	u8 hdr_len[0x8];
	u8 type[0x8];
	u8 segments_left[0x8];
	union {
		u8 flags[0x20];
		struct {
			u8 last_entry[0x8];
			u8 flag[0x8];
			u8 tag[0x10];
		};
	};
};

struct mlx5_ifc_header_vxlan_bits {
	u8 flags[0x8];
	u8 reserved1[0x18];
	u8 vni[0x18];
	u8 reserved2[0x8];
};

struct mlx5_ifc_header_vxlan_gpe_bits {
	u8 flags[0x8];
	u8 rsvd0[0x10];
	u8 protocol[0x8];
	u8 vni[0x18];
	u8 rsvd1[0x8];
};

struct mlx5_ifc_header_gre_bits {
	union {
		u8 c_rsvd0_ver[0x10];
		struct {
			u8 gre_c_present[0x1];
			u8 reserved_at_1[0x1];
			u8 gre_k_present[0x1];
			u8 gre_s_present[0x1];
			u8 reserved_at_4[0x9];
			u8 version[0x3];
		};
	};
	u8 gre_protocol[0x10];
	u8 checksum[0x10];
	u8 reserved_at_30[0x10];
};

struct mlx5_ifc_header_geneve_bits {
	union {
		u8 ver_opt_len_o_c_rsvd[0x10];
		struct {
			u8 version[0x2];
			u8 opt_len[0x6];
			u8 o_flag[0x1];
			u8 c_flag[0x1];
			u8 reserved_at_a[0x6];
		};
	};
	u8 protocol_type[0x10];
	u8 vni[0x18];
	u8 reserved_at_38[0x8];
};

struct mlx5_ifc_header_geneve_opt_bits {
	u8 class[0x10];
	u8 type[0x8];
	u8 reserved[0x3];
	u8 len[0x5];
};

struct mlx5_ifc_header_icmp_bits {
	union {
		u8 icmp_dw1[0x20];
		struct {
			u8 type[0x8];
			u8 code[0x8];
			u8 cksum[0x10];
		};
	};
	union {
		u8 icmp_dw2[0x20];
		struct {
			u8 ident[0x10];
			u8 seq_nb[0x10];
		};
	};
};

struct mlx5hws_definer {
	enum mlx5hws_definer_type type;
	u8 dw_selector[DW_SELECTORS];
	u8 byte_selector[BYTE_SELECTORS];
	struct mlx5hws_rule_match_tag mask;
	u32 obj_id;
};

struct mlx5hws_definer_cache {
	struct list_head list_head;
};

struct mlx5hws_definer_cache_item {
	struct mlx5hws_definer definer;
	u32 refcount; /* protected by context ctrl lock */
	struct list_head list_node;
};

static inline bool
mlx5hws_definer_is_jumbo(struct mlx5hws_definer *definer)
{
	return (definer->type == MLX5HWS_DEFINER_TYPE_JUMBO);
}

void mlx5hws_definer_create_tag(u32 *match_param,
				struct mlx5hws_definer_fc *fc,
				u32 fc_sz,
				u8 *tag);

int mlx5hws_definer_get_id(struct mlx5hws_definer *definer);

int mlx5hws_definer_mt_init(struct mlx5hws_context *ctx,
			    struct mlx5hws_match_template *mt);

void mlx5hws_definer_mt_uninit(struct mlx5hws_context *ctx,
			       struct mlx5hws_match_template *mt);

int mlx5hws_definer_init_cache(struct mlx5hws_definer_cache **cache);

void mlx5hws_definer_uninit_cache(struct mlx5hws_definer_cache *cache);

int mlx5hws_definer_compare(struct mlx5hws_definer *definer_a,
			    struct mlx5hws_definer *definer_b);

int mlx5hws_definer_get_obj(struct mlx5hws_context *ctx,
			    struct mlx5hws_definer *definer);

void mlx5hws_definer_free(struct mlx5hws_context *ctx,
			  struct mlx5hws_definer *definer);

int mlx5hws_definer_calc_layout(struct mlx5hws_context *ctx,
				struct mlx5hws_match_template *mt,
				struct mlx5hws_definer *match_definer,
				bool allow_jumbo);

const char *mlx5hws_definer_fname_to_str(enum mlx5hws_definer_fname fname);

#endif /* HWS_DEFINER_H_ */
