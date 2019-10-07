/* SPDX-License-Identifier: GPL-2.0 OR Linux-OpenIB */
/* Copyright (c) 2019, Mellanox Technologies */

#ifndef MLX5_IFC_DR_H
#define MLX5_IFC_DR_H

enum {
	MLX5DR_ACTION_MDFY_HW_FLD_L2_0		= 0,
	MLX5DR_ACTION_MDFY_HW_FLD_L2_1		= 1,
	MLX5DR_ACTION_MDFY_HW_FLD_L2_2		= 2,
	MLX5DR_ACTION_MDFY_HW_FLD_L3_0		= 3,
	MLX5DR_ACTION_MDFY_HW_FLD_L3_1		= 4,
	MLX5DR_ACTION_MDFY_HW_FLD_L3_2		= 5,
	MLX5DR_ACTION_MDFY_HW_FLD_L3_3		= 6,
	MLX5DR_ACTION_MDFY_HW_FLD_L3_4		= 7,
	MLX5DR_ACTION_MDFY_HW_FLD_L4_0		= 8,
	MLX5DR_ACTION_MDFY_HW_FLD_L4_1		= 9,
	MLX5DR_ACTION_MDFY_HW_FLD_MPLS		= 10,
	MLX5DR_ACTION_MDFY_HW_FLD_L2_TNL_0	= 11,
	MLX5DR_ACTION_MDFY_HW_FLD_REG_0		= 12,
	MLX5DR_ACTION_MDFY_HW_FLD_REG_1		= 13,
	MLX5DR_ACTION_MDFY_HW_FLD_REG_2		= 14,
	MLX5DR_ACTION_MDFY_HW_FLD_REG_3		= 15,
	MLX5DR_ACTION_MDFY_HW_FLD_L4_2		= 16,
	MLX5DR_ACTION_MDFY_HW_FLD_FLEX_0	= 17,
	MLX5DR_ACTION_MDFY_HW_FLD_FLEX_1	= 18,
	MLX5DR_ACTION_MDFY_HW_FLD_FLEX_2	= 19,
	MLX5DR_ACTION_MDFY_HW_FLD_FLEX_3	= 20,
	MLX5DR_ACTION_MDFY_HW_FLD_L2_TNL_1	= 21,
	MLX5DR_ACTION_MDFY_HW_FLD_METADATA	= 22,
	MLX5DR_ACTION_MDFY_HW_FLD_RESERVED	= 23,
};

enum {
	MLX5DR_ACTION_MDFY_HW_OP_SET		= 0x2,
	MLX5DR_ACTION_MDFY_HW_OP_ADD		= 0x3,
};

enum {
	MLX5DR_ACTION_MDFY_HW_HDR_L3_NONE	= 0x0,
	MLX5DR_ACTION_MDFY_HW_HDR_L3_IPV4	= 0x1,
	MLX5DR_ACTION_MDFY_HW_HDR_L3_IPV6	= 0x2,
};

enum {
	MLX5DR_ACTION_MDFY_HW_HDR_L4_NONE	= 0x0,
	MLX5DR_ACTION_MDFY_HW_HDR_L4_TCP	= 0x1,
	MLX5DR_ACTION_MDFY_HW_HDR_L4_UDP	= 0x2,
};

enum {
	MLX5DR_STE_LU_TYPE_NOP				= 0x00,
	MLX5DR_STE_LU_TYPE_SRC_GVMI_AND_QP		= 0x05,
	MLX5DR_STE_LU_TYPE_ETHL2_TUNNELING_I		= 0x0a,
	MLX5DR_STE_LU_TYPE_ETHL2_DST_O			= 0x06,
	MLX5DR_STE_LU_TYPE_ETHL2_DST_I			= 0x07,
	MLX5DR_STE_LU_TYPE_ETHL2_DST_D			= 0x1b,
	MLX5DR_STE_LU_TYPE_ETHL2_SRC_O			= 0x08,
	MLX5DR_STE_LU_TYPE_ETHL2_SRC_I			= 0x09,
	MLX5DR_STE_LU_TYPE_ETHL2_SRC_D			= 0x1c,
	MLX5DR_STE_LU_TYPE_ETHL2_SRC_DST_O		= 0x36,
	MLX5DR_STE_LU_TYPE_ETHL2_SRC_DST_I		= 0x37,
	MLX5DR_STE_LU_TYPE_ETHL2_SRC_DST_D		= 0x38,
	MLX5DR_STE_LU_TYPE_ETHL3_IPV6_DST_O		= 0x0d,
	MLX5DR_STE_LU_TYPE_ETHL3_IPV6_DST_I		= 0x0e,
	MLX5DR_STE_LU_TYPE_ETHL3_IPV6_DST_D		= 0x1e,
	MLX5DR_STE_LU_TYPE_ETHL3_IPV6_SRC_O		= 0x0f,
	MLX5DR_STE_LU_TYPE_ETHL3_IPV6_SRC_I		= 0x10,
	MLX5DR_STE_LU_TYPE_ETHL3_IPV6_SRC_D		= 0x1f,
	MLX5DR_STE_LU_TYPE_ETHL3_IPV4_5_TUPLE_O		= 0x11,
	MLX5DR_STE_LU_TYPE_ETHL3_IPV4_5_TUPLE_I		= 0x12,
	MLX5DR_STE_LU_TYPE_ETHL3_IPV4_5_TUPLE_D		= 0x20,
	MLX5DR_STE_LU_TYPE_ETHL3_IPV4_MISC_O		= 0x29,
	MLX5DR_STE_LU_TYPE_ETHL3_IPV4_MISC_I		= 0x2a,
	MLX5DR_STE_LU_TYPE_ETHL3_IPV4_MISC_D		= 0x2b,
	MLX5DR_STE_LU_TYPE_ETHL4_O			= 0x13,
	MLX5DR_STE_LU_TYPE_ETHL4_I			= 0x14,
	MLX5DR_STE_LU_TYPE_ETHL4_D			= 0x21,
	MLX5DR_STE_LU_TYPE_ETHL4_MISC_O			= 0x2c,
	MLX5DR_STE_LU_TYPE_ETHL4_MISC_I			= 0x2d,
	MLX5DR_STE_LU_TYPE_ETHL4_MISC_D			= 0x2e,
	MLX5DR_STE_LU_TYPE_MPLS_FIRST_O			= 0x15,
	MLX5DR_STE_LU_TYPE_MPLS_FIRST_I			= 0x24,
	MLX5DR_STE_LU_TYPE_MPLS_FIRST_D			= 0x25,
	MLX5DR_STE_LU_TYPE_GRE				= 0x16,
	MLX5DR_STE_LU_TYPE_FLEX_PARSER_0		= 0x22,
	MLX5DR_STE_LU_TYPE_FLEX_PARSER_1		= 0x23,
	MLX5DR_STE_LU_TYPE_FLEX_PARSER_TNL_HEADER	= 0x19,
	MLX5DR_STE_LU_TYPE_GENERAL_PURPOSE		= 0x18,
	MLX5DR_STE_LU_TYPE_STEERING_REGISTERS_0		= 0x2f,
	MLX5DR_STE_LU_TYPE_STEERING_REGISTERS_1		= 0x30,
	MLX5DR_STE_LU_TYPE_DONT_CARE			= 0x0f,
};

enum mlx5dr_ste_entry_type {
	MLX5DR_STE_TYPE_TX		= 1,
	MLX5DR_STE_TYPE_RX		= 2,
	MLX5DR_STE_TYPE_MODIFY_PKT	= 6,
};

struct mlx5_ifc_ste_general_bits {
	u8         entry_type[0x4];
	u8         reserved_at_4[0x4];
	u8         entry_sub_type[0x8];
	u8         byte_mask[0x10];

	u8         next_table_base_63_48[0x10];
	u8         next_lu_type[0x8];
	u8         next_table_base_39_32_size[0x8];

	u8         next_table_base_31_5_size[0x1b];
	u8         linear_hash_enable[0x1];
	u8         reserved_at_5c[0x2];
	u8         next_table_rank[0x2];

	u8         reserved_at_60[0xa0];
	u8         tag_value[0x60];
	u8         bit_mask[0x60];
};

struct mlx5_ifc_ste_sx_transmit_bits {
	u8         entry_type[0x4];
	u8         reserved_at_4[0x4];
	u8         entry_sub_type[0x8];
	u8         byte_mask[0x10];

	u8         next_table_base_63_48[0x10];
	u8         next_lu_type[0x8];
	u8         next_table_base_39_32_size[0x8];

	u8         next_table_base_31_5_size[0x1b];
	u8         linear_hash_enable[0x1];
	u8         reserved_at_5c[0x2];
	u8         next_table_rank[0x2];

	u8         sx_wire[0x1];
	u8         sx_func_lb[0x1];
	u8         sx_sniffer[0x1];
	u8         sx_wire_enable[0x1];
	u8         sx_func_lb_enable[0x1];
	u8         sx_sniffer_enable[0x1];
	u8         action_type[0x3];
	u8         reserved_at_69[0x1];
	u8         action_description[0x6];
	u8         gvmi[0x10];

	u8         encap_pointer_vlan_data[0x20];

	u8         loopback_syndome_en[0x8];
	u8         loopback_syndome[0x8];
	u8         counter_trigger[0x10];

	u8         miss_address_63_48[0x10];
	u8         counter_trigger_23_16[0x8];
	u8         miss_address_39_32[0x8];

	u8         miss_address_31_6[0x1a];
	u8         learning_point[0x1];
	u8         go_back[0x1];
	u8         match_polarity[0x1];
	u8         mask_mode[0x1];
	u8         miss_rank[0x2];
};

struct mlx5_ifc_ste_rx_steering_mult_bits {
	u8         entry_type[0x4];
	u8         reserved_at_4[0x4];
	u8         entry_sub_type[0x8];
	u8         byte_mask[0x10];

	u8         next_table_base_63_48[0x10];
	u8         next_lu_type[0x8];
	u8         next_table_base_39_32_size[0x8];

	u8         next_table_base_31_5_size[0x1b];
	u8         linear_hash_enable[0x1];
	u8         reserved_at_[0x2];
	u8         next_table_rank[0x2];

	u8         member_count[0x10];
	u8         gvmi[0x10];

	u8         qp_list_pointer[0x20];

	u8         reserved_at_a0[0x1];
	u8         tunneling_action[0x3];
	u8         action_description[0x4];
	u8         reserved_at_a8[0x8];
	u8         counter_trigger_15_0[0x10];

	u8         miss_address_63_48[0x10];
	u8         counter_trigger_23_16[0x08];
	u8         miss_address_39_32[0x8];

	u8         miss_address_31_6[0x1a];
	u8         learning_point[0x1];
	u8         fail_on_error[0x1];
	u8         match_polarity[0x1];
	u8         mask_mode[0x1];
	u8         miss_rank[0x2];
};

struct mlx5_ifc_ste_modify_packet_bits {
	u8         entry_type[0x4];
	u8         reserved_at_4[0x4];
	u8         entry_sub_type[0x8];
	u8         byte_mask[0x10];

	u8         next_table_base_63_48[0x10];
	u8         next_lu_type[0x8];
	u8         next_table_base_39_32_size[0x8];

	u8         next_table_base_31_5_size[0x1b];
	u8         linear_hash_enable[0x1];
	u8         reserved_at_[0x2];
	u8         next_table_rank[0x2];

	u8         number_of_re_write_actions[0x10];
	u8         gvmi[0x10];

	u8         header_re_write_actions_pointer[0x20];

	u8         reserved_at_a0[0x1];
	u8         tunneling_action[0x3];
	u8         action_description[0x4];
	u8         reserved_at_a8[0x8];
	u8         counter_trigger_15_0[0x10];

	u8         miss_address_63_48[0x10];
	u8         counter_trigger_23_16[0x08];
	u8         miss_address_39_32[0x8];

	u8         miss_address_31_6[0x1a];
	u8         learning_point[0x1];
	u8         fail_on_error[0x1];
	u8         match_polarity[0x1];
	u8         mask_mode[0x1];
	u8         miss_rank[0x2];
};

struct mlx5_ifc_ste_eth_l2_src_bits {
	u8         smac_47_16[0x20];

	u8         smac_15_0[0x10];
	u8         l3_ethertype[0x10];

	u8         qp_type[0x2];
	u8         ethertype_filter[0x1];
	u8         reserved_at_43[0x1];
	u8         sx_sniffer[0x1];
	u8         force_lb[0x1];
	u8         functional_lb[0x1];
	u8         port[0x1];
	u8         reserved_at_48[0x4];
	u8         first_priority[0x3];
	u8         first_cfi[0x1];
	u8         first_vlan_qualifier[0x2];
	u8         reserved_at_52[0x2];
	u8         first_vlan_id[0xc];

	u8         ip_fragmented[0x1];
	u8         tcp_syn[0x1];
	u8         encp_type[0x2];
	u8         l3_type[0x2];
	u8         l4_type[0x2];
	u8         reserved_at_68[0x4];
	u8         second_priority[0x3];
	u8         second_cfi[0x1];
	u8         second_vlan_qualifier[0x2];
	u8         reserved_at_72[0x2];
	u8         second_vlan_id[0xc];
};

struct mlx5_ifc_ste_eth_l2_dst_bits {
	u8         dmac_47_16[0x20];

	u8         dmac_15_0[0x10];
	u8         l3_ethertype[0x10];

	u8         qp_type[0x2];
	u8         ethertype_filter[0x1];
	u8         reserved_at_43[0x1];
	u8         sx_sniffer[0x1];
	u8         force_lb[0x1];
	u8         functional_lb[0x1];
	u8         port[0x1];
	u8         reserved_at_48[0x4];
	u8         first_priority[0x3];
	u8         first_cfi[0x1];
	u8         first_vlan_qualifier[0x2];
	u8         reserved_at_52[0x2];
	u8         first_vlan_id[0xc];

	u8         ip_fragmented[0x1];
	u8         tcp_syn[0x1];
	u8         encp_type[0x2];
	u8         l3_type[0x2];
	u8         l4_type[0x2];
	u8         reserved_at_68[0x4];
	u8         second_priority[0x3];
	u8         second_cfi[0x1];
	u8         second_vlan_qualifier[0x2];
	u8         reserved_at_72[0x2];
	u8         second_vlan_id[0xc];
};

struct mlx5_ifc_ste_eth_l2_src_dst_bits {
	u8         dmac_47_16[0x20];

	u8         dmac_15_0[0x10];
	u8         smac_47_32[0x10];

	u8         smac_31_0[0x20];

	u8         sx_sniffer[0x1];
	u8         force_lb[0x1];
	u8         functional_lb[0x1];
	u8         port[0x1];
	u8         l3_type[0x2];
	u8         reserved_at_66[0x6];
	u8         first_priority[0x3];
	u8         first_cfi[0x1];
	u8         first_vlan_qualifier[0x2];
	u8         reserved_at_72[0x2];
	u8         first_vlan_id[0xc];
};

struct mlx5_ifc_ste_eth_l3_ipv4_5_tuple_bits {
	u8         destination_address[0x20];

	u8         source_address[0x20];

	u8         source_port[0x10];
	u8         destination_port[0x10];

	u8         fragmented[0x1];
	u8         first_fragment[0x1];
	u8         reserved_at_62[0x2];
	u8         reserved_at_64[0x1];
	u8         ecn[0x2];
	u8         tcp_ns[0x1];
	u8         tcp_cwr[0x1];
	u8         tcp_ece[0x1];
	u8         tcp_urg[0x1];
	u8         tcp_ack[0x1];
	u8         tcp_psh[0x1];
	u8         tcp_rst[0x1];
	u8         tcp_syn[0x1];
	u8         tcp_fin[0x1];
	u8         dscp[0x6];
	u8         reserved_at_76[0x2];
	u8         protocol[0x8];
};

struct mlx5_ifc_ste_eth_l3_ipv6_dst_bits {
	u8         dst_ip_127_96[0x20];

	u8         dst_ip_95_64[0x20];

	u8         dst_ip_63_32[0x20];

	u8         dst_ip_31_0[0x20];
};

struct mlx5_ifc_ste_eth_l2_tnl_bits {
	u8         dmac_47_16[0x20];

	u8         dmac_15_0[0x10];
	u8         l3_ethertype[0x10];

	u8         l2_tunneling_network_id[0x20];

	u8         ip_fragmented[0x1];
	u8         tcp_syn[0x1];
	u8         encp_type[0x2];
	u8         l3_type[0x2];
	u8         l4_type[0x2];
	u8         first_priority[0x3];
	u8         first_cfi[0x1];
	u8         reserved_at_6c[0x3];
	u8         gre_key_flag[0x1];
	u8         first_vlan_qualifier[0x2];
	u8         reserved_at_72[0x2];
	u8         first_vlan_id[0xc];
};

struct mlx5_ifc_ste_eth_l3_ipv6_src_bits {
	u8         src_ip_127_96[0x20];

	u8         src_ip_95_64[0x20];

	u8         src_ip_63_32[0x20];

	u8         src_ip_31_0[0x20];
};

struct mlx5_ifc_ste_eth_l3_ipv4_misc_bits {
	u8         version[0x4];
	u8         ihl[0x4];
	u8         reserved_at_8[0x8];
	u8         total_length[0x10];

	u8         identification[0x10];
	u8         flags[0x3];
	u8         fragment_offset[0xd];

	u8         time_to_live[0x8];
	u8         reserved_at_48[0x8];
	u8         checksum[0x10];

	u8         reserved_at_60[0x20];
};

struct mlx5_ifc_ste_eth_l4_bits {
	u8         fragmented[0x1];
	u8         first_fragment[0x1];
	u8         reserved_at_2[0x6];
	u8         protocol[0x8];
	u8         dst_port[0x10];

	u8         ipv6_version[0x4];
	u8         reserved_at_24[0x1];
	u8         ecn[0x2];
	u8         tcp_ns[0x1];
	u8         tcp_cwr[0x1];
	u8         tcp_ece[0x1];
	u8         tcp_urg[0x1];
	u8         tcp_ack[0x1];
	u8         tcp_psh[0x1];
	u8         tcp_rst[0x1];
	u8         tcp_syn[0x1];
	u8         tcp_fin[0x1];
	u8         src_port[0x10];

	u8         ipv6_payload_length[0x10];
	u8         ipv6_hop_limit[0x8];
	u8         dscp[0x6];
	u8         reserved_at_5e[0x2];

	u8         tcp_data_offset[0x4];
	u8         reserved_at_64[0x8];
	u8         flow_label[0x14];
};

struct mlx5_ifc_ste_eth_l4_misc_bits {
	u8         checksum[0x10];
	u8         length[0x10];

	u8         seq_num[0x20];

	u8         ack_num[0x20];

	u8         urgent_pointer[0x10];
	u8         window_size[0x10];
};

struct mlx5_ifc_ste_mpls_bits {
	u8         mpls0_label[0x14];
	u8         mpls0_exp[0x3];
	u8         mpls0_s_bos[0x1];
	u8         mpls0_ttl[0x8];

	u8         mpls1_label[0x20];

	u8         mpls2_label[0x20];

	u8         reserved_at_60[0x16];
	u8         mpls4_s_bit[0x1];
	u8         mpls4_qualifier[0x1];
	u8         mpls3_s_bit[0x1];
	u8         mpls3_qualifier[0x1];
	u8         mpls2_s_bit[0x1];
	u8         mpls2_qualifier[0x1];
	u8         mpls1_s_bit[0x1];
	u8         mpls1_qualifier[0x1];
	u8         mpls0_s_bit[0x1];
	u8         mpls0_qualifier[0x1];
};

struct mlx5_ifc_ste_register_0_bits {
	u8         register_0_h[0x20];

	u8         register_0_l[0x20];

	u8         register_1_h[0x20];

	u8         register_1_l[0x20];
};

struct mlx5_ifc_ste_register_1_bits {
	u8         register_2_h[0x20];

	u8         register_2_l[0x20];

	u8         register_3_h[0x20];

	u8         register_3_l[0x20];
};

struct mlx5_ifc_ste_gre_bits {
	u8         gre_c_present[0x1];
	u8         reserved_at_30[0x1];
	u8         gre_k_present[0x1];
	u8         gre_s_present[0x1];
	u8         strict_src_route[0x1];
	u8         recur[0x3];
	u8         flags[0x5];
	u8         version[0x3];
	u8         gre_protocol[0x10];

	u8         checksum[0x10];
	u8         offset[0x10];

	u8         gre_key_h[0x18];
	u8         gre_key_l[0x8];

	u8         seq_num[0x20];
};

struct mlx5_ifc_ste_flex_parser_0_bits {
	u8         parser_3_label[0x14];
	u8         parser_3_exp[0x3];
	u8         parser_3_s_bos[0x1];
	u8         parser_3_ttl[0x8];

	u8         flex_parser_2[0x20];

	u8         flex_parser_1[0x20];

	u8         flex_parser_0[0x20];
};

struct mlx5_ifc_ste_flex_parser_1_bits {
	u8         flex_parser_7[0x20];

	u8         flex_parser_6[0x20];

	u8         flex_parser_5[0x20];

	u8         flex_parser_4[0x20];
};

struct mlx5_ifc_ste_flex_parser_tnl_bits {
	u8         flex_parser_tunneling_header_63_32[0x20];

	u8         flex_parser_tunneling_header_31_0[0x20];

	u8         reserved_at_40[0x40];
};

struct mlx5_ifc_ste_general_purpose_bits {
	u8         general_purpose_lookup_field[0x20];

	u8         reserved_at_20[0x20];

	u8         reserved_at_40[0x20];

	u8         reserved_at_60[0x20];
};

struct mlx5_ifc_ste_src_gvmi_qp_bits {
	u8         loopback_syndrome[0x8];
	u8         reserved_at_8[0x8];
	u8         source_gvmi[0x10];

	u8         reserved_at_20[0x5];
	u8         force_lb[0x1];
	u8         functional_lb[0x1];
	u8         source_is_requestor[0x1];
	u8         source_qp[0x18];

	u8         reserved_at_40[0x20];

	u8         reserved_at_60[0x20];
};

struct mlx5_ifc_l2_hdr_bits {
	u8         dmac_47_16[0x20];

	u8         dmac_15_0[0x10];
	u8         smac_47_32[0x10];

	u8         smac_31_0[0x20];

	u8         ethertype[0x10];
	u8         vlan_type[0x10];

	u8         vlan[0x10];
	u8         reserved_at_90[0x10];
};

/* Both HW set and HW add share the same HW format with different opcodes */
struct mlx5_ifc_dr_action_hw_set_bits {
	u8         opcode[0x8];
	u8         destination_field_code[0x8];
	u8         reserved_at_10[0x2];
	u8         destination_left_shifter[0x6];
	u8         reserved_at_18[0x3];
	u8         destination_length[0x5];

	u8         inline_data[0x20];
};

#endif /* MLX5_IFC_DR_H */
