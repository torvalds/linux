/* SPDX-License-Identifier: GPL-2.0 OR Linux-OpenIB */
/* Copyright (c) 2024 NVIDIA Corporation & Affiliates */

#ifndef HWS_CMD_H_
#define HWS_CMD_H_

#define WIRE_PORT 0xFFFF

#define ACCESS_KEY_LEN	32

enum mlx5hws_cmd_ext_dest_flags {
	MLX5HWS_CMD_EXT_DEST_REFORMAT = 1 << 0,
	MLX5HWS_CMD_EXT_DEST_ESW_OWNER_VHCA_ID = 1 << 1,
};

struct mlx5hws_cmd_set_fte_dest {
	u8 destination_type;
	u32 destination_id;
	enum mlx5hws_cmd_ext_dest_flags ext_flags;
	u32 ext_reformat_id;
	u16 esw_owner_vhca_id;
};

struct mlx5hws_cmd_set_fte_attr {
	u32 action_flags;
	bool ignore_flow_level;
	u8 flow_source;
	u8 extended_dest;
	u8 encrypt_decrypt_type;
	u32 encrypt_decrypt_obj_id;
	u32 packet_reformat_id;
	u32 dests_num;
	struct mlx5hws_cmd_set_fte_dest *dests;
};

struct mlx5hws_cmd_ft_create_attr {
	u8 type;
	u8 level;
	u16 uid;
	bool rtc_valid;
	bool decap_en;
	bool reformat_en;
};

struct mlx5hws_cmd_ft_modify_attr {
	u8 type;
	u32 rtc_id_0;
	u32 rtc_id_1;
	u32 table_miss_id;
	u8 table_miss_action;
	u64 modify_fs;
};

struct mlx5hws_cmd_ft_query_attr {
	u8 type;
};

struct mlx5hws_cmd_fg_attr {
	u32 table_id;
	u32 table_type;
};

struct mlx5hws_cmd_forward_tbl {
	u8 type;
	u32 ft_id;
	u32 fg_id;
	u32 refcount; /* protected by context ctrl lock */
};

struct mlx5hws_cmd_rtc_create_attr {
	u32 pd;
	u32 stc_base;
	u32 ste_base;
	u32 miss_ft_id;
	bool fw_gen_wqe;
	u8 update_index_mode;
	u8 access_index_mode;
	u8 num_hash_definer;
	u8 log_depth;
	u8 log_size;
	u8 table_type;
	u8 match_definer_0;
	u8 match_definer_1;
	u8 reparse_mode;
	bool is_frst_jumbo;
	bool is_scnd_range;
};

struct mlx5hws_cmd_alias_obj_create_attr {
	u32 obj_id;
	u16 vhca_id;
	u16 obj_type;
	u8 access_key[ACCESS_KEY_LEN];
};

struct mlx5hws_cmd_stc_create_attr {
	u8 log_obj_range;
	u8 table_type;
};

struct mlx5hws_cmd_stc_modify_attr {
	u32 stc_offset;
	u8 action_offset;
	u8 reparse_mode;
	enum mlx5_ifc_stc_action_type action_type;
	union {
		u32 id; /* TIRN, TAG, FT ID, STE ID, CRYPTO */
		struct {
			u8 decap;
			u16 start_anchor;
			u16 end_anchor;
		} remove_header;
		struct {
			u32 arg_id;
			u32 pattern_id;
		} modify_header;
		struct {
			__be64 data;
		} modify_action;
		struct {
			u32 arg_id;
			u32 header_size;
			u8 is_inline;
			u8 encap;
			u16 insert_anchor;
			u16 insert_offset;
		} insert_header;
		struct {
			u8 aso_type;
			u32 devx_obj_id;
			u8 return_reg_id;
		} aso;
		struct {
			u16 vport_num;
			u16 esw_owner_vhca_id;
			u8 eswitch_owner_vhca_id_valid;
		} vport;
		struct {
			struct mlx5hws_pool_chunk ste;
			struct mlx5hws_pool *ste_pool;
			u32 ste_obj_id; /* Internal */
			u32 match_definer_id;
			u8 log_hash_size;
			bool ignore_tx;
		} ste_table;
		struct {
			u16 start_anchor;
			u16 num_of_words;
		} remove_words;
		struct {
			u8 type;
			u8 op;
			u8 size;
		} reformat_trailer;

		u32 dest_table_id;
		u32 dest_tir_num;
	};
};

struct mlx5hws_cmd_ste_create_attr {
	u8 log_obj_range;
	u8 table_type;
};

struct mlx5hws_cmd_definer_create_attr {
	u8 *dw_selector;
	u8 *byte_selector;
	u8 *match_mask;
};

struct mlx5hws_cmd_allow_other_vhca_access_attr {
	u16 obj_type;
	u32 obj_id;
	u8 access_key[ACCESS_KEY_LEN];
};

struct mlx5hws_cmd_packet_reformat_create_attr {
	u8 type;
	size_t data_sz;
	void *data;
	u8 reformat_param_0;
};

struct mlx5hws_cmd_query_ft_caps {
	u8 max_level;
	u8 reparse;
	u8 ignore_flow_level_rtc_valid;
};

struct mlx5hws_cmd_generate_wqe_attr {
	u8 *wqe_ctrl;
	u8 *gta_ctrl;
	u8 *gta_data_0;
	u8 *gta_data_1;
	u32 pdn;
};

struct mlx5hws_cmd_query_caps {
	u32 flex_protocols;
	u8 wqe_based_update;
	u8 rtc_reparse_mode;
	u16 ste_format;
	u8 rtc_index_mode;
	u8 ste_alloc_log_max;
	u8 ste_alloc_log_gran;
	u8 stc_alloc_log_max;
	u8 stc_alloc_log_gran;
	u8 rtc_log_depth_max;
	u8 format_select_gtpu_dw_0;
	u8 format_select_gtpu_dw_1;
	u8 flow_table_hash_type;
	u8 format_select_gtpu_dw_2;
	u8 format_select_gtpu_ext_dw_0;
	u8 access_index_mode;
	u32 linear_match_definer;
	bool full_dw_jumbo_support;
	bool rtc_hash_split_table;
	bool rtc_linear_lookup_table;
	u32 supp_type_gen_wqe;
	u8 rtc_max_hash_def_gen_wqe;
	u16 supp_ste_format_gen_wqe;
	struct mlx5hws_cmd_query_ft_caps nic_ft;
	struct mlx5hws_cmd_query_ft_caps fdb_ft;
	bool eswitch_manager;
	bool merged_eswitch;
	u32 eswitch_manager_vport_number;
	u8 log_header_modify_argument_granularity;
	u8 log_header_modify_argument_max_alloc;
	u8 sq_ts_format;
	u8 fdb_tir_stc;
	u64 definer_format_sup;
	u32 trivial_match_definer;
	u32 vhca_id;
	u32 shared_vhca_id;
	char fw_ver[64];
	bool ipsec_offload;
	bool is_ecpf;
	u8 flex_parser_ok_bits_supp;
	u8 flex_parser_id_geneve_tlv_option_0;
	u8 flex_parser_id_mpls_over_gre;
	u8 flex_parser_id_mpls_over_udp;
};

int mlx5hws_cmd_flow_table_create(struct mlx5_core_dev *mdev,
				  struct mlx5hws_cmd_ft_create_attr *ft_attr,
				  u32 *table_id);

int mlx5hws_cmd_flow_table_modify(struct mlx5_core_dev *mdev,
				  struct mlx5hws_cmd_ft_modify_attr *ft_attr,
				  u32 table_id);

int mlx5hws_cmd_flow_table_query(struct mlx5_core_dev *mdev,
				 u32 obj_id,
				 struct mlx5hws_cmd_ft_query_attr *ft_attr,
				 u64 *icm_addr_0, u64 *icm_addr_1);

int mlx5hws_cmd_flow_table_destroy(struct mlx5_core_dev *mdev,
				   u8 fw_ft_type, u32 table_id);

int mlx5hws_cmd_rtc_create(struct mlx5_core_dev *mdev,
			   struct mlx5hws_cmd_rtc_create_attr *rtc_attr,
			   u32 *rtc_id);

void mlx5hws_cmd_rtc_destroy(struct mlx5_core_dev *mdev, u32 rtc_id);

int mlx5hws_cmd_stc_create(struct mlx5_core_dev *mdev,
			   struct mlx5hws_cmd_stc_create_attr *stc_attr,
			   u32 *stc_id);

int mlx5hws_cmd_stc_modify(struct mlx5_core_dev *mdev,
			   u32 stc_id,
			   struct mlx5hws_cmd_stc_modify_attr *stc_attr);

void mlx5hws_cmd_stc_destroy(struct mlx5_core_dev *mdev, u32 stc_id);

int mlx5hws_cmd_generate_wqe(struct mlx5_core_dev *mdev,
			     struct mlx5hws_cmd_generate_wqe_attr *attr,
			     struct mlx5_cqe64 *ret_cqe);

int mlx5hws_cmd_ste_create(struct mlx5_core_dev *mdev,
			   struct mlx5hws_cmd_ste_create_attr *ste_attr,
			   u32 *ste_id);

void mlx5hws_cmd_ste_destroy(struct mlx5_core_dev *mdev, u32 ste_id);

int mlx5hws_cmd_definer_create(struct mlx5_core_dev *mdev,
			       struct mlx5hws_cmd_definer_create_attr *def_attr,
			       u32 *definer_id);

void mlx5hws_cmd_definer_destroy(struct mlx5_core_dev *mdev,
				 u32 definer_id);

int mlx5hws_cmd_arg_create(struct mlx5_core_dev *mdev,
			   u16 log_obj_range,
			   u32 pd,
			   u32 *arg_id);

void mlx5hws_cmd_arg_destroy(struct mlx5_core_dev *mdev,
			     u32 arg_id);

int mlx5hws_cmd_header_modify_pattern_create(struct mlx5_core_dev *mdev,
					     u32 pattern_length,
					     u8 *actions,
					     u32 *ptrn_id);

void mlx5hws_cmd_header_modify_pattern_destroy(struct mlx5_core_dev *mdev,
					       u32 ptrn_id);

int mlx5hws_cmd_packet_reformat_create(struct mlx5_core_dev *mdev,
				       struct mlx5hws_cmd_packet_reformat_create_attr *attr,
				       u32 *reformat_id);

int mlx5hws_cmd_packet_reformat_destroy(struct mlx5_core_dev *mdev,
					u32 reformat_id);

int mlx5hws_cmd_set_fte(struct mlx5_core_dev *mdev,
			u32 table_type,
			u32 table_id,
			u32 group_id,
			struct mlx5hws_cmd_set_fte_attr *fte_attr);

int mlx5hws_cmd_delete_fte(struct mlx5_core_dev *mdev,
			   u32 table_type, u32 table_id);

struct mlx5hws_cmd_forward_tbl *
mlx5hws_cmd_forward_tbl_create(struct mlx5_core_dev *mdev,
			       struct mlx5hws_cmd_ft_create_attr *ft_attr,
			       struct mlx5hws_cmd_set_fte_attr *fte_attr);

void mlx5hws_cmd_forward_tbl_destroy(struct mlx5_core_dev *mdev,
				     struct mlx5hws_cmd_forward_tbl *tbl);

int mlx5hws_cmd_sq_modify_rdy(struct mlx5_core_dev *mdev, u32 sqn);

int mlx5hws_cmd_query_caps(struct mlx5_core_dev *mdev,
			   struct mlx5hws_cmd_query_caps *caps);

void mlx5hws_cmd_set_attr_connect_miss_tbl(struct mlx5hws_context *ctx,
					   u32 fw_ft_type,
					   enum mlx5hws_table_type type,
					   struct mlx5hws_cmd_ft_modify_attr *ft_attr);

int mlx5hws_cmd_query_gvmi(struct mlx5_core_dev *mdev, bool other_function,
			   u16 vport_number, u16 *gvmi);

#endif /* HWS_CMD_H_ */
