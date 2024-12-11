/* SPDX-License-Identifier: GPL-2.0 OR Linux-OpenIB */
/* Copyright (c) 2024 NVIDIA Corporation & Affiliates */

#ifndef MLX5_PRM_H_
#define MLX5_PRM_H_

#define MLX5_MAX_ACTIONS_DATA_IN_HEADER_MODIFY 512

/* Action type of header modification. */
enum {
	MLX5_MODIFICATION_TYPE_SET = 0x1,
	MLX5_MODIFICATION_TYPE_ADD = 0x2,
	MLX5_MODIFICATION_TYPE_COPY = 0x3,
	MLX5_MODIFICATION_TYPE_INSERT = 0x4,
	MLX5_MODIFICATION_TYPE_REMOVE = 0x5,
	MLX5_MODIFICATION_TYPE_NOP = 0x6,
	MLX5_MODIFICATION_TYPE_REMOVE_WORDS = 0x7,
	MLX5_MODIFICATION_TYPE_ADD_FIELD = 0x8,
	MLX5_MODIFICATION_TYPE_MAX,
};

/* The field of packet to be modified. */
enum mlx5_modification_field {
	MLX5_MODI_OUT_NONE = -1,
	MLX5_MODI_OUT_SMAC_47_16 = 1,
	MLX5_MODI_OUT_SMAC_15_0,
	MLX5_MODI_OUT_ETHERTYPE,
	MLX5_MODI_OUT_DMAC_47_16,
	MLX5_MODI_OUT_DMAC_15_0,
	MLX5_MODI_OUT_IP_DSCP,
	MLX5_MODI_OUT_TCP_FLAGS,
	MLX5_MODI_OUT_TCP_SPORT,
	MLX5_MODI_OUT_TCP_DPORT,
	MLX5_MODI_OUT_IPV4_TTL,
	MLX5_MODI_OUT_UDP_SPORT,
	MLX5_MODI_OUT_UDP_DPORT,
	MLX5_MODI_OUT_SIPV6_127_96,
	MLX5_MODI_OUT_SIPV6_95_64,
	MLX5_MODI_OUT_SIPV6_63_32,
	MLX5_MODI_OUT_SIPV6_31_0,
	MLX5_MODI_OUT_DIPV6_127_96,
	MLX5_MODI_OUT_DIPV6_95_64,
	MLX5_MODI_OUT_DIPV6_63_32,
	MLX5_MODI_OUT_DIPV6_31_0,
	MLX5_MODI_OUT_SIPV4,
	MLX5_MODI_OUT_DIPV4,
	MLX5_MODI_OUT_FIRST_VID,
	MLX5_MODI_IN_SMAC_47_16 = 0x31,
	MLX5_MODI_IN_SMAC_15_0,
	MLX5_MODI_IN_ETHERTYPE,
	MLX5_MODI_IN_DMAC_47_16,
	MLX5_MODI_IN_DMAC_15_0,
	MLX5_MODI_IN_IP_DSCP,
	MLX5_MODI_IN_TCP_FLAGS,
	MLX5_MODI_IN_TCP_SPORT,
	MLX5_MODI_IN_TCP_DPORT,
	MLX5_MODI_IN_IPV4_TTL,
	MLX5_MODI_IN_UDP_SPORT,
	MLX5_MODI_IN_UDP_DPORT,
	MLX5_MODI_IN_SIPV6_127_96,
	MLX5_MODI_IN_SIPV6_95_64,
	MLX5_MODI_IN_SIPV6_63_32,
	MLX5_MODI_IN_SIPV6_31_0,
	MLX5_MODI_IN_DIPV6_127_96,
	MLX5_MODI_IN_DIPV6_95_64,
	MLX5_MODI_IN_DIPV6_63_32,
	MLX5_MODI_IN_DIPV6_31_0,
	MLX5_MODI_IN_SIPV4,
	MLX5_MODI_IN_DIPV4,
	MLX5_MODI_OUT_IPV6_HOPLIMIT,
	MLX5_MODI_IN_IPV6_HOPLIMIT,
	MLX5_MODI_META_DATA_REG_A,
	MLX5_MODI_META_DATA_REG_B = 0x50,
	MLX5_MODI_META_REG_C_0,
	MLX5_MODI_META_REG_C_1,
	MLX5_MODI_META_REG_C_2,
	MLX5_MODI_META_REG_C_3,
	MLX5_MODI_META_REG_C_4,
	MLX5_MODI_META_REG_C_5,
	MLX5_MODI_META_REG_C_6,
	MLX5_MODI_META_REG_C_7,
	MLX5_MODI_OUT_TCP_SEQ_NUM,
	MLX5_MODI_IN_TCP_SEQ_NUM,
	MLX5_MODI_OUT_TCP_ACK_NUM,
	MLX5_MODI_IN_TCP_ACK_NUM = 0x5C,
	MLX5_MODI_GTP_TEID = 0x6E,
	MLX5_MODI_OUT_IP_ECN = 0x73,
	MLX5_MODI_TUNNEL_HDR_DW_1 = 0x75,
	MLX5_MODI_GTPU_FIRST_EXT_DW_0 = 0x76,
	MLX5_MODI_HASH_RESULT = 0x81,
	MLX5_MODI_IN_MPLS_LABEL_0 = 0x8a,
	MLX5_MODI_IN_MPLS_LABEL_1,
	MLX5_MODI_IN_MPLS_LABEL_2,
	MLX5_MODI_IN_MPLS_LABEL_3,
	MLX5_MODI_IN_MPLS_LABEL_4,
	MLX5_MODI_OUT_IP_PROTOCOL = 0x4A,
	MLX5_MODI_OUT_IPV6_NEXT_HDR = 0x4A,
	MLX5_MODI_META_REG_C_8 = 0x8F,
	MLX5_MODI_META_REG_C_9 = 0x90,
	MLX5_MODI_META_REG_C_10 = 0x91,
	MLX5_MODI_META_REG_C_11 = 0x92,
	MLX5_MODI_META_REG_C_12 = 0x93,
	MLX5_MODI_META_REG_C_13 = 0x94,
	MLX5_MODI_META_REG_C_14 = 0x95,
	MLX5_MODI_META_REG_C_15 = 0x96,
	MLX5_MODI_OUT_IPV4_TOTAL_LEN = 0x11D,
	MLX5_MODI_OUT_IPV6_PAYLOAD_LEN = 0x11E,
	MLX5_MODI_OUT_IPV4_IHL = 0x11F,
	MLX5_MODI_OUT_TCP_DATA_OFFSET = 0x120,
	MLX5_MODI_OUT_ESP_SPI = 0x5E,
	MLX5_MODI_OUT_ESP_SEQ_NUM = 0x82,
	MLX5_MODI_OUT_IPSEC_NEXT_HDR = 0x126,
	MLX5_MODI_INVALID = INT_MAX,
};

enum {
	MLX5_GET_HCA_CAP_OP_MOD_NIC_FLOW_TABLE = 0x7 << 1,
	MLX5_GET_HCA_CAP_OP_MOD_ESW_FLOW_TABLE = 0x8 << 1,
	MLX5_SET_HCA_CAP_OP_MOD_ESW = 0x9 << 1,
	MLX5_GET_HCA_CAP_OP_MOD_WQE_BASED_FLOW_TABLE = 0x1B << 1,
	MLX5_GET_HCA_CAP_OP_MOD_GENERAL_DEVICE_2 = 0x20 << 1,
};

enum mlx5_ifc_rtc_update_mode {
	MLX5_IFC_RTC_STE_UPDATE_MODE_BY_HASH = 0x0,
	MLX5_IFC_RTC_STE_UPDATE_MODE_BY_OFFSET = 0x1,
};

enum mlx5_ifc_rtc_access_mode {
	MLX5_IFC_RTC_STE_ACCESS_MODE_BY_HASH = 0x0,
	MLX5_IFC_RTC_STE_ACCESS_MODE_LINEAR = 0x1,
};

enum mlx5_ifc_rtc_ste_format {
	MLX5_IFC_RTC_STE_FORMAT_8DW = 0x4,
	MLX5_IFC_RTC_STE_FORMAT_11DW = 0x5,
	MLX5_IFC_RTC_STE_FORMAT_RANGE = 0x7,
};

enum mlx5_ifc_rtc_reparse_mode {
	MLX5_IFC_RTC_REPARSE_NEVER = 0x0,
	MLX5_IFC_RTC_REPARSE_ALWAYS = 0x1,
	MLX5_IFC_RTC_REPARSE_BY_STC = 0x2,
};

#define MLX5_IFC_RTC_LINEAR_LOOKUP_TBL_LOG_MAX 16

struct mlx5_ifc_rtc_bits {
	u8 modify_field_select[0x40];
	u8 reserved_at_40[0x40];
	u8 update_index_mode[0x2];
	u8 reparse_mode[0x2];
	u8 num_match_ste[0x4];
	u8 pd[0x18];
	u8 reserved_at_a0[0x9];
	u8 access_index_mode[0x3];
	u8 num_hash_definer[0x4];
	u8 update_method[0x1];
	u8 reserved_at_b1[0x2];
	u8 log_depth[0x5];
	u8 log_hash_size[0x8];
	u8 ste_format_0[0x8];
	u8 table_type[0x8];
	u8 ste_format_1[0x8];
	u8 reserved_at_d8[0x8];
	u8 match_definer_0[0x20];
	u8 stc_id[0x20];
	u8 ste_table_base_id[0x20];
	u8 ste_table_offset[0x20];
	u8 reserved_at_160[0x8];
	u8 miss_flow_table_id[0x18];
	u8 match_definer_1[0x20];
	u8 reserved_at_1a0[0x260];
};

enum mlx5_ifc_stc_action_type {
	MLX5_IFC_STC_ACTION_TYPE_NOP = 0x00,
	MLX5_IFC_STC_ACTION_TYPE_COPY = 0x05,
	MLX5_IFC_STC_ACTION_TYPE_SET = 0x06,
	MLX5_IFC_STC_ACTION_TYPE_ADD = 0x07,
	MLX5_IFC_STC_ACTION_TYPE_REMOVE_WORDS = 0x08,
	MLX5_IFC_STC_ACTION_TYPE_HEADER_REMOVE = 0x09,
	MLX5_IFC_STC_ACTION_TYPE_HEADER_INSERT = 0x0b,
	MLX5_IFC_STC_ACTION_TYPE_TAG = 0x0c,
	MLX5_IFC_STC_ACTION_TYPE_ACC_MODIFY_LIST = 0x0e,
	MLX5_IFC_STC_ACTION_TYPE_CRYPTO_IPSEC_ENCRYPTION = 0x10,
	MLX5_IFC_STC_ACTION_TYPE_CRYPTO_IPSEC_DECRYPTION = 0x11,
	MLX5_IFC_STC_ACTION_TYPE_ASO = 0x12,
	MLX5_IFC_STC_ACTION_TYPE_TRAILER = 0x13,
	MLX5_IFC_STC_ACTION_TYPE_COUNTER = 0x14,
	MLX5_IFC_STC_ACTION_TYPE_ADD_FIELD = 0x1b,
	MLX5_IFC_STC_ACTION_TYPE_JUMP_TO_STE_TABLE = 0x80,
	MLX5_IFC_STC_ACTION_TYPE_JUMP_TO_TIR = 0x81,
	MLX5_IFC_STC_ACTION_TYPE_JUMP_TO_FT = 0x82,
	MLX5_IFC_STC_ACTION_TYPE_DROP = 0x83,
	MLX5_IFC_STC_ACTION_TYPE_ALLOW = 0x84,
	MLX5_IFC_STC_ACTION_TYPE_JUMP_TO_VPORT = 0x85,
	MLX5_IFC_STC_ACTION_TYPE_JUMP_TO_UPLINK = 0x86,
};

enum mlx5_ifc_stc_reparse_mode {
	MLX5_IFC_STC_REPARSE_IGNORE = 0x0,
	MLX5_IFC_STC_REPARSE_NEVER = 0x1,
	MLX5_IFC_STC_REPARSE_ALWAYS = 0x2,
};

struct mlx5_ifc_stc_ste_param_ste_table_bits {
	u8 ste_obj_id[0x20];
	u8 match_definer_id[0x20];
	u8 reserved_at_40[0x3];
	u8 log_hash_size[0x5];
	u8 reserved_at_48[0x38];
};

struct mlx5_ifc_stc_ste_param_tir_bits {
	u8 reserved_at_0[0x8];
	u8 tirn[0x18];
	u8 reserved_at_20[0x60];
};

struct mlx5_ifc_stc_ste_param_table_bits {
	u8 reserved_at_0[0x8];
	u8 table_id[0x18];
	u8 reserved_at_20[0x60];
};

struct mlx5_ifc_stc_ste_param_flow_counter_bits {
	u8 flow_counter_id[0x20];
};

enum {
	MLX5_ASO_CT_NUM_PER_OBJ = 1,
	MLX5_ASO_METER_NUM_PER_OBJ = 2,
	MLX5_ASO_IPSEC_NUM_PER_OBJ = 1,
	MLX5_ASO_FIRST_HIT_NUM_PER_OBJ = 512,
};

struct mlx5_ifc_stc_ste_param_execute_aso_bits {
	u8 aso_object_id[0x20];
	u8 return_reg_id[0x4];
	u8 aso_type[0x4];
	u8 reserved_at_28[0x18];
};

struct mlx5_ifc_stc_ste_param_ipsec_encrypt_bits {
	u8 ipsec_object_id[0x20];
};

struct mlx5_ifc_stc_ste_param_ipsec_decrypt_bits {
	u8 ipsec_object_id[0x20];
};

struct mlx5_ifc_stc_ste_param_trailer_bits {
	u8 reserved_at_0[0x8];
	u8 command[0x4];
	u8 reserved_at_c[0x2];
	u8 type[0x2];
	u8 reserved_at_10[0xa];
	u8 length[0x6];
};

struct mlx5_ifc_stc_ste_param_header_modify_list_bits {
	u8 header_modify_pattern_id[0x20];
	u8 header_modify_argument_id[0x20];
};

enum mlx5_ifc_header_anchors {
	MLX5_HEADER_ANCHOR_PACKET_START = 0x0,
	MLX5_HEADER_ANCHOR_MAC = 0x1,
	MLX5_HEADER_ANCHOR_FIRST_VLAN_START = 0x2,
	MLX5_HEADER_ANCHOR_IPV6_IPV4 = 0x07,
	MLX5_HEADER_ANCHOR_ESP = 0x08,
	MLX5_HEADER_ANCHOR_TCP_UDP = 0x09,
	MLX5_HEADER_ANCHOR_TUNNEL_HEADER = 0x0a,
	MLX5_HEADER_ANCHOR_INNER_MAC = 0x13,
	MLX5_HEADER_ANCHOR_INNER_IPV6_IPV4 = 0x19,
	MLX5_HEADER_ANCHOR_INNER_TCP_UDP = 0x1a,
	MLX5_HEADER_ANCHOR_L4_PAYLOAD = 0x1b,
	MLX5_HEADER_ANCHOR_INNER_L4_PAYLOAD = 0x1c
};

struct mlx5_ifc_stc_ste_param_remove_bits {
	u8 action_type[0x4];
	u8 decap[0x1];
	u8 reserved_at_5[0x5];
	u8 remove_start_anchor[0x6];
	u8 reserved_at_10[0x2];
	u8 remove_end_anchor[0x6];
	u8 reserved_at_18[0x8];
};

struct mlx5_ifc_stc_ste_param_remove_words_bits {
	u8 action_type[0x4];
	u8 reserved_at_4[0x6];
	u8 remove_start_anchor[0x6];
	u8 reserved_at_10[0x1];
	u8 remove_offset[0x7];
	u8 reserved_at_18[0x2];
	u8 remove_size[0x6];
};

struct mlx5_ifc_stc_ste_param_insert_bits {
	u8 action_type[0x4];
	u8 encap[0x1];
	u8 inline_data[0x1];
	u8 reserved_at_6[0x4];
	u8 insert_anchor[0x6];
	u8 reserved_at_10[0x1];
	u8 insert_offset[0x7];
	u8 reserved_at_18[0x1];
	u8 insert_size[0x7];
	u8 insert_argument[0x20];
};

struct mlx5_ifc_stc_ste_param_vport_bits {
	u8 eswitch_owner_vhca_id[0x10];
	u8 vport_number[0x10];
	u8 eswitch_owner_vhca_id_valid[0x1];
	u8 reserved_at_21[0x5f];
};

union mlx5_ifc_stc_param_bits {
	struct mlx5_ifc_stc_ste_param_ste_table_bits ste_table;
	struct mlx5_ifc_stc_ste_param_tir_bits tir;
	struct mlx5_ifc_stc_ste_param_table_bits table;
	struct mlx5_ifc_stc_ste_param_flow_counter_bits counter;
	struct mlx5_ifc_stc_ste_param_header_modify_list_bits modify_header;
	struct mlx5_ifc_stc_ste_param_execute_aso_bits aso;
	struct mlx5_ifc_stc_ste_param_remove_bits remove_header;
	struct mlx5_ifc_stc_ste_param_insert_bits insert_header;
	struct mlx5_ifc_set_action_in_bits add;
	struct mlx5_ifc_set_action_in_bits set;
	struct mlx5_ifc_copy_action_in_bits copy;
	struct mlx5_ifc_stc_ste_param_vport_bits vport;
	struct mlx5_ifc_stc_ste_param_ipsec_encrypt_bits ipsec_encrypt;
	struct mlx5_ifc_stc_ste_param_ipsec_decrypt_bits ipsec_decrypt;
	struct mlx5_ifc_stc_ste_param_trailer_bits trailer;
	u8 reserved_at_0[0x80];
};

enum {
	MLX5_IFC_MODIFY_STC_FIELD_SELECT_NEW_STC = BIT(0),
};

struct mlx5_ifc_stc_bits {
	u8 modify_field_select[0x40];
	u8 reserved_at_40[0x46];
	u8 reparse_mode[0x2];
	u8 table_type[0x8];
	u8 ste_action_offset[0x8];
	u8 action_type[0x8];
	u8 reserved_at_a0[0x60];
	union mlx5_ifc_stc_param_bits stc_param;
	u8 reserved_at_180[0x280];
};

struct mlx5_ifc_ste_bits {
	u8 modify_field_select[0x40];
	u8 reserved_at_40[0x48];
	u8 table_type[0x8];
	u8 reserved_at_90[0x370];
};

struct mlx5_ifc_definer_bits {
	u8 modify_field_select[0x40];
	u8 reserved_at_40[0x50];
	u8 format_id[0x10];
	u8 reserved_at_60[0x60];
	u8 format_select_dw3[0x8];
	u8 format_select_dw2[0x8];
	u8 format_select_dw1[0x8];
	u8 format_select_dw0[0x8];
	u8 format_select_dw7[0x8];
	u8 format_select_dw6[0x8];
	u8 format_select_dw5[0x8];
	u8 format_select_dw4[0x8];
	u8 reserved_at_100[0x18];
	u8 format_select_dw8[0x8];
	u8 reserved_at_120[0x20];
	u8 format_select_byte3[0x8];
	u8 format_select_byte2[0x8];
	u8 format_select_byte1[0x8];
	u8 format_select_byte0[0x8];
	u8 format_select_byte7[0x8];
	u8 format_select_byte6[0x8];
	u8 format_select_byte5[0x8];
	u8 format_select_byte4[0x8];
	u8 reserved_at_180[0x40];
	u8 ctrl[0xa0];
	u8 match_mask[0x160];
};

struct mlx5_ifc_arg_bits {
	u8 rsvd0[0x88];
	u8 access_pd[0x18];
};

struct mlx5_ifc_header_modify_pattern_in_bits {
	u8 modify_field_select[0x40];

	u8 reserved_at_40[0x40];

	u8 pattern_length[0x8];
	u8 reserved_at_88[0x18];

	u8 reserved_at_a0[0x60];

	u8 pattern_data[MLX5_MAX_ACTIONS_DATA_IN_HEADER_MODIFY * 8];
};

struct mlx5_ifc_create_rtc_in_bits {
	struct mlx5_ifc_general_obj_in_cmd_hdr_bits hdr;
	struct mlx5_ifc_rtc_bits rtc;
};

struct mlx5_ifc_create_stc_in_bits {
	struct mlx5_ifc_general_obj_in_cmd_hdr_bits hdr;
	struct mlx5_ifc_stc_bits stc;
};

struct mlx5_ifc_create_ste_in_bits {
	struct mlx5_ifc_general_obj_in_cmd_hdr_bits hdr;
	struct mlx5_ifc_ste_bits ste;
};

struct mlx5_ifc_create_definer_in_bits {
	struct mlx5_ifc_general_obj_in_cmd_hdr_bits hdr;
	struct mlx5_ifc_definer_bits definer;
};

struct mlx5_ifc_create_arg_in_bits {
	struct mlx5_ifc_general_obj_in_cmd_hdr_bits hdr;
	struct mlx5_ifc_arg_bits arg;
};

struct mlx5_ifc_create_header_modify_pattern_in_bits {
	struct mlx5_ifc_general_obj_in_cmd_hdr_bits hdr;
	struct mlx5_ifc_header_modify_pattern_in_bits pattern;
};

struct mlx5_ifc_generate_wqe_in_bits {
	u8 opcode[0x10];
	u8 uid[0x10];
	u8 reserved_at_20[0x10];
	u8 op_mode[0x10];
	u8 reserved_at_40[0x40];
	u8 reserved_at_80[0x8];
	u8 pdn[0x18];
	u8 reserved_at_a0[0x160];
	u8 wqe_ctrl[0x80];
	u8 wqe_gta_ctrl[0x180];
	u8 wqe_gta_data_0[0x200];
	u8 wqe_gta_data_1[0x200];
};

struct mlx5_ifc_generate_wqe_out_bits {
	u8 status[0x8];
	u8 reserved_at_8[0x18];
	u8 syndrome[0x20];
	u8 reserved_at_40[0x1c0];
	u8 cqe_data[0x200];
};

enum mlx5_access_aso_opc_mod {
	ASO_OPC_MOD_IPSEC = 0x0,
	ASO_OPC_MOD_CONNECTION_TRACKING = 0x1,
	ASO_OPC_MOD_POLICER = 0x2,
	ASO_OPC_MOD_RACE_AVOIDANCE = 0x3,
	ASO_OPC_MOD_FLOW_HIT = 0x4,
};

enum {
	MLX5_IFC_MODIFY_FLOW_TABLE_MISS_ACTION = BIT(0),
	MLX5_IFC_MODIFY_FLOW_TABLE_RTC_ID = BIT(1),
};

enum {
	MLX5_IFC_MODIFY_FLOW_TABLE_MISS_ACTION_DEFAULT = 0,
	MLX5_IFC_MODIFY_FLOW_TABLE_MISS_ACTION_GOTO_TBL = 1,
};

struct mlx5_ifc_alloc_packet_reformat_out_bits {
	u8 status[0x8];
	u8 reserved_at_8[0x18];

	u8 syndrome[0x20];

	u8 packet_reformat_id[0x20];

	u8 reserved_at_60[0x20];
};

struct mlx5_ifc_dealloc_packet_reformat_in_bits {
	u8 opcode[0x10];
	u8 reserved_at_10[0x10];

	u8 reserved_at_20[0x10];
	u8 op_mod[0x10];

	u8 packet_reformat_id[0x20];

	u8 reserved_at_60[0x20];
};

struct mlx5_ifc_dealloc_packet_reformat_out_bits {
	u8 status[0x8];
	u8 reserved_at_8[0x18];

	u8 syndrome[0x20];

	u8 reserved_at_40[0x40];
};

#endif /* MLX5_PRM_H_ */
