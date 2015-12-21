/* 57xx_hsi_bnx2fc.h: QLogic Linux FCoE offload driver.
 * Handles operations such as session offload/upload etc, and manages
 * session resources such as connection id and qp resources.
 *
 * Copyright (c) 2008-2013 Broadcom Corporation
 * Copyright (c) 2014-2015 QLogic Corporation
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation.
 *
 */

#ifndef __57XX_FCOE_HSI_LINUX_LE__
#define __57XX_FCOE_HSI_LINUX_LE__

/*
 * common data for all protocols
 */
struct b577xx_doorbell_hdr {
	u8 header;
#define B577XX_DOORBELL_HDR_RX (0x1<<0)
#define B577XX_DOORBELL_HDR_RX_SHIFT 0
#define B577XX_DOORBELL_HDR_DB_TYPE (0x1<<1)
#define B577XX_DOORBELL_HDR_DB_TYPE_SHIFT 1
#define B577XX_DOORBELL_HDR_DPM_SIZE (0x3<<2)
#define B577XX_DOORBELL_HDR_DPM_SIZE_SHIFT 2
#define B577XX_DOORBELL_HDR_CONN_TYPE (0xF<<4)
#define B577XX_DOORBELL_HDR_CONN_TYPE_SHIFT 4
};

/*
 * doorbell message sent to the chip
 */
struct b577xx_doorbell {
#if defined(__BIG_ENDIAN)
	u16 zero_fill2;
	u8 zero_fill1;
	struct b577xx_doorbell_hdr header;
#elif defined(__LITTLE_ENDIAN)
	struct b577xx_doorbell_hdr header;
	u8 zero_fill1;
	u16 zero_fill2;
#endif
};



/*
 * doorbell message sent to the chip
 */
struct b577xx_doorbell_set_prod {
#if defined(__BIG_ENDIAN)
	u16 prod;
	u8 zero_fill1;
	struct b577xx_doorbell_hdr header;
#elif defined(__LITTLE_ENDIAN)
	struct b577xx_doorbell_hdr header;
	u8 zero_fill1;
	u16 prod;
#endif
};


struct regpair {
	__le32 lo;
	__le32 hi;
};


/*
 * ABTS info $$KEEP_ENDIANNESS$$
 */
struct fcoe_abts_info {
	__le16 aborted_task_id;
	__le16 reserved0;
	__le32 reserved1;
};


/*
 * Fixed size structure in order to plant it in Union structure
 * $$KEEP_ENDIANNESS$$
 */
struct fcoe_abts_rsp_union {
	u8 r_ctl;
	u8 rsrv[3];
	__le32 abts_rsp_payload[7];
};


/*
 * 4 regs size $$KEEP_ENDIANNESS$$
 */
struct fcoe_bd_ctx {
	__le32 buf_addr_hi;
	__le32 buf_addr_lo;
	__le16 buf_len;
	__le16 rsrv0;
	__le16 flags;
	__le16 rsrv1;
};


/*
 * FCoE cached sges context $$KEEP_ENDIANNESS$$
 */
struct fcoe_cached_sge_ctx {
	struct regpair cur_buf_addr;
	__le16 cur_buf_rem;
	__le16 second_buf_rem;
	struct regpair second_buf_addr;
};


/*
 * Cleanup info $$KEEP_ENDIANNESS$$
 */
struct fcoe_cleanup_info {
	__le16 cleaned_task_id;
	__le16 rolled_tx_seq_cnt;
	__le32 rolled_tx_data_offset;
};


/*
 * Fcp RSP flags $$KEEP_ENDIANNESS$$
 */
struct fcoe_fcp_rsp_flags {
	u8 flags;
#define FCOE_FCP_RSP_FLAGS_FCP_RSP_LEN_VALID (0x1<<0)
#define FCOE_FCP_RSP_FLAGS_FCP_RSP_LEN_VALID_SHIFT 0
#define FCOE_FCP_RSP_FLAGS_FCP_SNS_LEN_VALID (0x1<<1)
#define FCOE_FCP_RSP_FLAGS_FCP_SNS_LEN_VALID_SHIFT 1
#define FCOE_FCP_RSP_FLAGS_FCP_RESID_OVER (0x1<<2)
#define FCOE_FCP_RSP_FLAGS_FCP_RESID_OVER_SHIFT 2
#define FCOE_FCP_RSP_FLAGS_FCP_RESID_UNDER (0x1<<3)
#define FCOE_FCP_RSP_FLAGS_FCP_RESID_UNDER_SHIFT 3
#define FCOE_FCP_RSP_FLAGS_FCP_CONF_REQ (0x1<<4)
#define FCOE_FCP_RSP_FLAGS_FCP_CONF_REQ_SHIFT 4
#define FCOE_FCP_RSP_FLAGS_FCP_BIDI_FLAGS (0x7<<5)
#define FCOE_FCP_RSP_FLAGS_FCP_BIDI_FLAGS_SHIFT 5
};

/*
 * Fcp RSP payload $$KEEP_ENDIANNESS$$
 */
struct fcoe_fcp_rsp_payload {
	struct regpair reserved0;
	__le32 fcp_resid;
	u8 scsi_status_code;
	struct fcoe_fcp_rsp_flags fcp_flags;
	__le16 retry_delay_timer;
	__le32 fcp_rsp_len;
	__le32 fcp_sns_len;
};

/*
 * Fixed size structure in order to plant it in Union structure
 * $$KEEP_ENDIANNESS$$
 */
struct fcoe_fcp_rsp_union {
	struct fcoe_fcp_rsp_payload payload;
	struct regpair reserved0;
};

/*
 * FC header $$KEEP_ENDIANNESS$$
 */
struct fcoe_fc_hdr {
	u8 s_id[3];
	u8 cs_ctl;
	u8 d_id[3];
	u8 r_ctl;
	__le16 seq_cnt;
	u8 df_ctl;
	u8 seq_id;
	u8 f_ctl[3];
	u8 type;
	__le32 parameters;
	__le16 rx_id;
	__le16 ox_id;
};

/*
 * FC header union $$KEEP_ENDIANNESS$$
 */
struct fcoe_mp_rsp_union {
	struct fcoe_fc_hdr fc_hdr;
	__le32 mp_payload_len;
	__le32 rsrv;
};

/*
 * Completion information $$KEEP_ENDIANNESS$$
 */
union fcoe_comp_flow_info {
	struct fcoe_fcp_rsp_union fcp_rsp;
	struct fcoe_abts_rsp_union abts_rsp;
	struct fcoe_mp_rsp_union mp_rsp;
	__le32 opaque[8];
};


/*
 * External ABTS info $$KEEP_ENDIANNESS$$
 */
struct fcoe_ext_abts_info {
	__le32 rsrv0[6];
	struct fcoe_abts_info ctx;
};


/*
 * External cleanup info $$KEEP_ENDIANNESS$$
 */
struct fcoe_ext_cleanup_info {
	__le32 rsrv0[6];
	struct fcoe_cleanup_info ctx;
};


/*
 * Fcoe FW Tx sequence context $$KEEP_ENDIANNESS$$
 */
struct fcoe_fw_tx_seq_ctx {
	__le32 data_offset;
	__le16 seq_cnt;
	__le16 rsrv0;
};

/*
 * Fcoe external FW Tx sequence context $$KEEP_ENDIANNESS$$
 */
struct fcoe_ext_fw_tx_seq_ctx {
	__le32 rsrv0[6];
	struct fcoe_fw_tx_seq_ctx ctx;
};


/*
 * FCoE multiple sges context $$KEEP_ENDIANNESS$$
 */
struct fcoe_mul_sges_ctx {
	struct regpair cur_sge_addr;
	__le16 cur_sge_off;
	u8 cur_sge_idx;
	u8 sgl_size;
};

/*
 * FCoE external multiple sges context $$KEEP_ENDIANNESS$$
 */
struct fcoe_ext_mul_sges_ctx {
	struct fcoe_mul_sges_ctx mul_sgl;
	struct regpair rsrv0;
};


/*
 * FCP CMD payload $$KEEP_ENDIANNESS$$
 */
struct fcoe_fcp_cmd_payload {
	__le32 opaque[8];
};





/*
 * Fcp xfr rdy payload $$KEEP_ENDIANNESS$$
 */
struct fcoe_fcp_xfr_rdy_payload {
	__le32 burst_len;
	__le32 data_ro;
};


/*
 * FC frame $$KEEP_ENDIANNESS$$
 */
struct fcoe_fc_frame {
	struct fcoe_fc_hdr fc_hdr;
	__le32 reserved0[2];
};




/*
 * FCoE KCQ CQE parameters $$KEEP_ENDIANNESS$$
 */
union fcoe_kcqe_params {
	__le32 reserved0[4];
};

/*
 * FCoE KCQ CQE $$KEEP_ENDIANNESS$$
 */
struct fcoe_kcqe {
	__le32 fcoe_conn_id;
	__le32 completion_status;
	__le32 fcoe_conn_context_id;
	union fcoe_kcqe_params params;
	__le16 qe_self_seq;
	u8 op_code;
	u8 flags;
#define FCOE_KCQE_RESERVED0 (0x7<<0)
#define FCOE_KCQE_RESERVED0_SHIFT 0
#define FCOE_KCQE_RAMROD_COMPLETION (0x1<<3)
#define FCOE_KCQE_RAMROD_COMPLETION_SHIFT 3
#define FCOE_KCQE_LAYER_CODE (0x7<<4)
#define FCOE_KCQE_LAYER_CODE_SHIFT 4
#define FCOE_KCQE_LINKED_WITH_NEXT (0x1<<7)
#define FCOE_KCQE_LINKED_WITH_NEXT_SHIFT 7
};



/*
 * FCoE KWQE header $$KEEP_ENDIANNESS$$
 */
struct fcoe_kwqe_header {
	u8 op_code;
	u8 flags;
#define FCOE_KWQE_HEADER_RESERVED0 (0xF<<0)
#define FCOE_KWQE_HEADER_RESERVED0_SHIFT 0
#define FCOE_KWQE_HEADER_LAYER_CODE (0x7<<4)
#define FCOE_KWQE_HEADER_LAYER_CODE_SHIFT 4
#define FCOE_KWQE_HEADER_RESERVED1 (0x1<<7)
#define FCOE_KWQE_HEADER_RESERVED1_SHIFT 7
};

/*
 * FCoE firmware init request 1 $$KEEP_ENDIANNESS$$
 */
struct fcoe_kwqe_init1 {
	__le16 num_tasks;
	struct fcoe_kwqe_header hdr;
	__le32 task_list_pbl_addr_lo;
	__le32 task_list_pbl_addr_hi;
	__le32 dummy_buffer_addr_lo;
	__le32 dummy_buffer_addr_hi;
	__le16 sq_num_wqes;
	__le16 rq_num_wqes;
	__le16 rq_buffer_log_size;
	__le16 cq_num_wqes;
	__le16 mtu;
	u8 num_sessions_log;
	u8 flags;
#define FCOE_KWQE_INIT1_LOG_PAGE_SIZE (0xF<<0)
#define FCOE_KWQE_INIT1_LOG_PAGE_SIZE_SHIFT 0
#define FCOE_KWQE_INIT1_LOG_CACHED_PBES_PER_FUNC (0x7<<4)
#define FCOE_KWQE_INIT1_LOG_CACHED_PBES_PER_FUNC_SHIFT 4
#define FCOE_KWQE_INIT1_RESERVED1 (0x1<<7)
#define FCOE_KWQE_INIT1_RESERVED1_SHIFT 7
};

/*
 * FCoE firmware init request 2 $$KEEP_ENDIANNESS$$
 */
struct fcoe_kwqe_init2 {
	u8 hsi_major_version;
	u8 hsi_minor_version;
	struct fcoe_kwqe_header hdr;
	__le32 hash_tbl_pbl_addr_lo;
	__le32 hash_tbl_pbl_addr_hi;
	__le32 t2_hash_tbl_addr_lo;
	__le32 t2_hash_tbl_addr_hi;
	__le32 t2_ptr_hash_tbl_addr_lo;
	__le32 t2_ptr_hash_tbl_addr_hi;
	__le32 free_list_count;
};

/*
 * FCoE firmware init request 3 $$KEEP_ENDIANNESS$$
 */
struct fcoe_kwqe_init3 {
	__le16 reserved0;
	struct fcoe_kwqe_header hdr;
	__le32 error_bit_map_lo;
	__le32 error_bit_map_hi;
	u8 perf_config;
	u8 reserved21[3];
	__le32 reserved2[4];
};

/*
 * FCoE connection offload request 1 $$KEEP_ENDIANNESS$$
 */
struct fcoe_kwqe_conn_offload1 {
	__le16 fcoe_conn_id;
	struct fcoe_kwqe_header hdr;
	__le32 sq_addr_lo;
	__le32 sq_addr_hi;
	__le32 rq_pbl_addr_lo;
	__le32 rq_pbl_addr_hi;
	__le32 rq_first_pbe_addr_lo;
	__le32 rq_first_pbe_addr_hi;
	__le16 rq_prod;
	__le16 reserved0;
};

/*
 * FCoE connection offload request 2 $$KEEP_ENDIANNESS$$
 */
struct fcoe_kwqe_conn_offload2 {
	__le16 tx_max_fc_pay_len;
	struct fcoe_kwqe_header hdr;
	__le32 cq_addr_lo;
	__le32 cq_addr_hi;
	__le32 xferq_addr_lo;
	__le32 xferq_addr_hi;
	__le32 conn_db_addr_lo;
	__le32 conn_db_addr_hi;
	__le32 reserved1;
};

/*
 * FCoE connection offload request 3 $$KEEP_ENDIANNESS$$
 */
struct fcoe_kwqe_conn_offload3 {
	__le16 vlan_tag;
#define FCOE_KWQE_CONN_OFFLOAD3_VLAN_ID (0xFFF<<0)
#define FCOE_KWQE_CONN_OFFLOAD3_VLAN_ID_SHIFT 0
#define FCOE_KWQE_CONN_OFFLOAD3_CFI (0x1<<12)
#define FCOE_KWQE_CONN_OFFLOAD3_CFI_SHIFT 12
#define FCOE_KWQE_CONN_OFFLOAD3_PRIORITY (0x7<<13)
#define FCOE_KWQE_CONN_OFFLOAD3_PRIORITY_SHIFT 13
	struct fcoe_kwqe_header hdr;
	u8 s_id[3];
	u8 tx_max_conc_seqs_c3;
	u8 d_id[3];
	u8 flags;
#define FCOE_KWQE_CONN_OFFLOAD3_B_MUL_N_PORT_IDS (0x1<<0)
#define FCOE_KWQE_CONN_OFFLOAD3_B_MUL_N_PORT_IDS_SHIFT 0
#define FCOE_KWQE_CONN_OFFLOAD3_B_E_D_TOV_RES (0x1<<1)
#define FCOE_KWQE_CONN_OFFLOAD3_B_E_D_TOV_RES_SHIFT 1
#define FCOE_KWQE_CONN_OFFLOAD3_B_CONT_INCR_SEQ_CNT (0x1<<2)
#define FCOE_KWQE_CONN_OFFLOAD3_B_CONT_INCR_SEQ_CNT_SHIFT 2
#define FCOE_KWQE_CONN_OFFLOAD3_B_CONF_REQ (0x1<<3)
#define FCOE_KWQE_CONN_OFFLOAD3_B_CONF_REQ_SHIFT 3
#define FCOE_KWQE_CONN_OFFLOAD3_B_REC_VALID (0x1<<4)
#define FCOE_KWQE_CONN_OFFLOAD3_B_REC_VALID_SHIFT 4
#define FCOE_KWQE_CONN_OFFLOAD3_B_C2_VALID (0x1<<5)
#define FCOE_KWQE_CONN_OFFLOAD3_B_C2_VALID_SHIFT 5
#define FCOE_KWQE_CONN_OFFLOAD3_B_ACK_0 (0x1<<6)
#define FCOE_KWQE_CONN_OFFLOAD3_B_ACK_0_SHIFT 6
#define FCOE_KWQE_CONN_OFFLOAD3_B_VLAN_FLAG (0x1<<7)
#define FCOE_KWQE_CONN_OFFLOAD3_B_VLAN_FLAG_SHIFT 7
	__le32 reserved;
	__le32 confq_first_pbe_addr_lo;
	__le32 confq_first_pbe_addr_hi;
	__le16 tx_total_conc_seqs;
	__le16 rx_max_fc_pay_len;
	__le16 rx_total_conc_seqs;
	u8 rx_max_conc_seqs_c3;
	u8 rx_open_seqs_exch_c3;
};

/*
 * FCoE connection offload request 4 $$KEEP_ENDIANNESS$$
 */
struct fcoe_kwqe_conn_offload4 {
	u8 e_d_tov_timer_val;
	u8 reserved2;
	struct fcoe_kwqe_header hdr;
	u8 src_mac_addr_lo[2];
	u8 src_mac_addr_mid[2];
	u8 src_mac_addr_hi[2];
	u8 dst_mac_addr_hi[2];
	u8 dst_mac_addr_lo[2];
	u8 dst_mac_addr_mid[2];
	__le32 lcq_addr_lo;
	__le32 lcq_addr_hi;
	__le32 confq_pbl_base_addr_lo;
	__le32 confq_pbl_base_addr_hi;
};

/*
 * FCoE connection enable request $$KEEP_ENDIANNESS$$
 */
struct fcoe_kwqe_conn_enable_disable {
	__le16 reserved0;
	struct fcoe_kwqe_header hdr;
	u8 src_mac_addr_lo[2];
	u8 src_mac_addr_mid[2];
	u8 src_mac_addr_hi[2];
	u16 vlan_tag;
#define FCOE_KWQE_CONN_ENABLE_DISABLE_VLAN_ID (0xFFF<<0)
#define FCOE_KWQE_CONN_ENABLE_DISABLE_VLAN_ID_SHIFT 0
#define FCOE_KWQE_CONN_ENABLE_DISABLE_CFI (0x1<<12)
#define FCOE_KWQE_CONN_ENABLE_DISABLE_CFI_SHIFT 12
#define FCOE_KWQE_CONN_ENABLE_DISABLE_PRIORITY (0x7<<13)
#define FCOE_KWQE_CONN_ENABLE_DISABLE_PRIORITY_SHIFT 13
	u8 dst_mac_addr_lo[2];
	u8 dst_mac_addr_mid[2];
	u8 dst_mac_addr_hi[2];
	__le16 reserved1;
	u8 s_id[3];
	u8 vlan_flag;
	u8 d_id[3];
	u8 reserved3;
	__le32 context_id;
	__le32 conn_id;
	__le32 reserved4;
};

/*
 * FCoE connection destroy request $$KEEP_ENDIANNESS$$
 */
struct fcoe_kwqe_conn_destroy {
	__le16 reserved0;
	struct fcoe_kwqe_header hdr;
	__le32 context_id;
	__le32 conn_id;
	__le32 reserved1[5];
};

/*
 * FCoe destroy request $$KEEP_ENDIANNESS$$
 */
struct fcoe_kwqe_destroy {
	__le16 reserved0;
	struct fcoe_kwqe_header hdr;
	__le32 reserved1[7];
};

/*
 * FCoe statistics request $$KEEP_ENDIANNESS$$
 */
struct fcoe_kwqe_stat {
	__le16 reserved0;
	struct fcoe_kwqe_header hdr;
	__le32 stat_params_addr_lo;
	__le32 stat_params_addr_hi;
	__le32 reserved1[5];
};

/*
 * FCoE KWQ WQE $$KEEP_ENDIANNESS$$
 */
union fcoe_kwqe {
	struct fcoe_kwqe_init1 init1;
	struct fcoe_kwqe_init2 init2;
	struct fcoe_kwqe_init3 init3;
	struct fcoe_kwqe_conn_offload1 conn_offload1;
	struct fcoe_kwqe_conn_offload2 conn_offload2;
	struct fcoe_kwqe_conn_offload3 conn_offload3;
	struct fcoe_kwqe_conn_offload4 conn_offload4;
	struct fcoe_kwqe_conn_enable_disable conn_enable_disable;
	struct fcoe_kwqe_conn_destroy conn_destroy;
	struct fcoe_kwqe_destroy destroy;
	struct fcoe_kwqe_stat statistics;
};
















/*
 * TX SGL context $$KEEP_ENDIANNESS$$
 */
union fcoe_sgl_union_ctx {
	struct fcoe_cached_sge_ctx cached_sge;
	struct fcoe_ext_mul_sges_ctx sgl;
	__le32 opaque[5];
};

/*
 * Data-In/ELS/BLS information $$KEEP_ENDIANNESS$$
 */
struct fcoe_read_flow_info {
	union fcoe_sgl_union_ctx sgl_ctx;
	__le32 rsrv0[3];
};


/*
 * Fcoe stat context $$KEEP_ENDIANNESS$$
 */
struct fcoe_s_stat_ctx {
	u8 flags;
#define FCOE_S_STAT_CTX_ACTIVE (0x1<<0)
#define FCOE_S_STAT_CTX_ACTIVE_SHIFT 0
#define FCOE_S_STAT_CTX_ACK_ABORT_SEQ_COND (0x1<<1)
#define FCOE_S_STAT_CTX_ACK_ABORT_SEQ_COND_SHIFT 1
#define FCOE_S_STAT_CTX_ABTS_PERFORMED (0x1<<2)
#define FCOE_S_STAT_CTX_ABTS_PERFORMED_SHIFT 2
#define FCOE_S_STAT_CTX_SEQ_TIMEOUT (0x1<<3)
#define FCOE_S_STAT_CTX_SEQ_TIMEOUT_SHIFT 3
#define FCOE_S_STAT_CTX_P_RJT (0x1<<4)
#define FCOE_S_STAT_CTX_P_RJT_SHIFT 4
#define FCOE_S_STAT_CTX_ACK_EOFT (0x1<<5)
#define FCOE_S_STAT_CTX_ACK_EOFT_SHIFT 5
#define FCOE_S_STAT_CTX_RSRV1 (0x3<<6)
#define FCOE_S_STAT_CTX_RSRV1_SHIFT 6
};

/*
 * Fcoe rx seq context $$KEEP_ENDIANNESS$$
 */
struct fcoe_rx_seq_ctx {
	u8 seq_id;
	struct fcoe_s_stat_ctx s_stat;
	__le16 seq_cnt;
	__le32 low_exp_ro;
	__le32 high_exp_ro;
};


/*
 * Fcoe rx_wr union context $$KEEP_ENDIANNESS$$
 */
union fcoe_rx_wr_union_ctx {
	struct fcoe_read_flow_info read_info;
	union fcoe_comp_flow_info comp_info;
	__le32 opaque[8];
};



/*
 * FCoE SQ element $$KEEP_ENDIANNESS$$
 */
struct fcoe_sqe {
	__le16 wqe;
#define FCOE_SQE_TASK_ID (0x7FFF<<0)
#define FCOE_SQE_TASK_ID_SHIFT 0
#define FCOE_SQE_TOGGLE_BIT (0x1<<15)
#define FCOE_SQE_TOGGLE_BIT_SHIFT 15
};



/*
 * 14 regs $$KEEP_ENDIANNESS$$
 */
struct fcoe_tce_tx_only {
	union fcoe_sgl_union_ctx sgl_ctx;
	__le32 rsrv0;
};

/*
 * 32 bytes (8 regs) used for TX only purposes $$KEEP_ENDIANNESS$$
 */
union fcoe_tx_wr_rx_rd_union_ctx {
	struct fcoe_fc_frame tx_frame;
	struct fcoe_fcp_cmd_payload fcp_cmd;
	struct fcoe_ext_cleanup_info cleanup;
	struct fcoe_ext_abts_info abts;
	struct fcoe_ext_fw_tx_seq_ctx tx_seq;
	__le32 opaque[8];
};

/*
 * tce_tx_wr_rx_rd_const $$KEEP_ENDIANNESS$$
 */
struct fcoe_tce_tx_wr_rx_rd_const {
	u8 init_flags;
#define FCOE_TCE_TX_WR_RX_RD_CONST_TASK_TYPE (0x7<<0)
#define FCOE_TCE_TX_WR_RX_RD_CONST_TASK_TYPE_SHIFT 0
#define FCOE_TCE_TX_WR_RX_RD_CONST_DEV_TYPE (0x1<<3)
#define FCOE_TCE_TX_WR_RX_RD_CONST_DEV_TYPE_SHIFT 3
#define FCOE_TCE_TX_WR_RX_RD_CONST_CLASS_TYPE (0x1<<4)
#define FCOE_TCE_TX_WR_RX_RD_CONST_CLASS_TYPE_SHIFT 4
#define FCOE_TCE_TX_WR_RX_RD_CONST_CACHED_SGE (0x3<<5)
#define FCOE_TCE_TX_WR_RX_RD_CONST_CACHED_SGE_SHIFT 5
#define FCOE_TCE_TX_WR_RX_RD_CONST_SUPPORT_REC_TOV (0x1<<7)
#define FCOE_TCE_TX_WR_RX_RD_CONST_SUPPORT_REC_TOV_SHIFT 7
	u8 tx_flags;
#define FCOE_TCE_TX_WR_RX_RD_CONST_TX_VALID (0x1<<0)
#define FCOE_TCE_TX_WR_RX_RD_CONST_TX_VALID_SHIFT 0
#define FCOE_TCE_TX_WR_RX_RD_CONST_TX_STATE (0xF<<1)
#define FCOE_TCE_TX_WR_RX_RD_CONST_TX_STATE_SHIFT 1
#define FCOE_TCE_TX_WR_RX_RD_CONST_RSRV1 (0x1<<5)
#define FCOE_TCE_TX_WR_RX_RD_CONST_RSRV1_SHIFT 5
#define FCOE_TCE_TX_WR_RX_RD_CONST_TX_SEQ_INIT (0x1<<6)
#define FCOE_TCE_TX_WR_RX_RD_CONST_TX_SEQ_INIT_SHIFT 6
#define FCOE_TCE_TX_WR_RX_RD_CONST_RSRV2 (0x1<<7)
#define FCOE_TCE_TX_WR_RX_RD_CONST_RSRV2_SHIFT 7
	__le16 rsrv3;
	__le32 verify_tx_seq;
};

/*
 * tce_tx_wr_rx_rd $$KEEP_ENDIANNESS$$
 */
struct fcoe_tce_tx_wr_rx_rd {
	union fcoe_tx_wr_rx_rd_union_ctx union_ctx;
	struct fcoe_tce_tx_wr_rx_rd_const const_ctx;
};

/*
 * tce_rx_wr_tx_rd_const $$KEEP_ENDIANNESS$$
 */
struct fcoe_tce_rx_wr_tx_rd_const {
	__le32 data_2_trns;
	__le32 init_flags;
#define FCOE_TCE_RX_WR_TX_RD_CONST_CID (0xFFFFFF<<0)
#define FCOE_TCE_RX_WR_TX_RD_CONST_CID_SHIFT 0
#define FCOE_TCE_RX_WR_TX_RD_CONST_RSRV0 (0xFF<<24)
#define FCOE_TCE_RX_WR_TX_RD_CONST_RSRV0_SHIFT 24
};

/*
 * tce_rx_wr_tx_rd_var $$KEEP_ENDIANNESS$$
 */
struct fcoe_tce_rx_wr_tx_rd_var {
	__le16 rx_flags;
#define FCOE_TCE_RX_WR_TX_RD_VAR_RSRV1 (0xF<<0)
#define FCOE_TCE_RX_WR_TX_RD_VAR_RSRV1_SHIFT 0
#define FCOE_TCE_RX_WR_TX_RD_VAR_NUM_RQ_WQE (0x7<<4)
#define FCOE_TCE_RX_WR_TX_RD_VAR_NUM_RQ_WQE_SHIFT 4
#define FCOE_TCE_RX_WR_TX_RD_VAR_CONF_REQ (0x1<<7)
#define FCOE_TCE_RX_WR_TX_RD_VAR_CONF_REQ_SHIFT 7
#define FCOE_TCE_RX_WR_TX_RD_VAR_RX_STATE (0xF<<8)
#define FCOE_TCE_RX_WR_TX_RD_VAR_RX_STATE_SHIFT 8
#define FCOE_TCE_RX_WR_TX_RD_VAR_EXP_FIRST_FRAME (0x1<<12)
#define FCOE_TCE_RX_WR_TX_RD_VAR_EXP_FIRST_FRAME_SHIFT 12
#define FCOE_TCE_RX_WR_TX_RD_VAR_RX_SEQ_INIT (0x1<<13)
#define FCOE_TCE_RX_WR_TX_RD_VAR_RX_SEQ_INIT_SHIFT 13
#define FCOE_TCE_RX_WR_TX_RD_VAR_RSRV2 (0x1<<14)
#define FCOE_TCE_RX_WR_TX_RD_VAR_RSRV2_SHIFT 14
#define FCOE_TCE_RX_WR_TX_RD_VAR_RX_VALID (0x1<<15)
#define FCOE_TCE_RX_WR_TX_RD_VAR_RX_VALID_SHIFT 15
	__le16 rx_id;
	struct fcoe_fcp_xfr_rdy_payload fcp_xfr_rdy;
};

/*
 * tce_rx_wr_tx_rd $$KEEP_ENDIANNESS$$
 */
struct fcoe_tce_rx_wr_tx_rd {
	struct fcoe_tce_rx_wr_tx_rd_const const_ctx;
	struct fcoe_tce_rx_wr_tx_rd_var var_ctx;
};

/*
 * tce_rx_only $$KEEP_ENDIANNESS$$
 */
struct fcoe_tce_rx_only {
	struct fcoe_rx_seq_ctx rx_seq_ctx;
	union fcoe_rx_wr_union_ctx union_ctx;
};

/*
 * task_ctx_entry $$KEEP_ENDIANNESS$$
 */
struct fcoe_task_ctx_entry {
	struct fcoe_tce_tx_only txwr_only;
	struct fcoe_tce_tx_wr_rx_rd txwr_rxrd;
	struct fcoe_tce_rx_wr_tx_rd rxwr_txrd;
	struct fcoe_tce_rx_only rxwr_only;
};










/*
 * FCoE XFRQ element $$KEEP_ENDIANNESS$$
 */
struct fcoe_xfrqe {
	__le16 wqe;
#define FCOE_XFRQE_TASK_ID (0x7FFF<<0)
#define FCOE_XFRQE_TASK_ID_SHIFT 0
#define FCOE_XFRQE_TOGGLE_BIT (0x1<<15)
#define FCOE_XFRQE_TOGGLE_BIT_SHIFT 15
};


/*
 * fcoe rx doorbell message sent to the chip $$KEEP_ENDIANNESS$$
 */
struct b577xx_fcoe_rx_doorbell {
	struct b577xx_doorbell_hdr hdr;
	u8 params;
#define B577XX_FCOE_RX_DOORBELL_NEGATIVE_ARM (0x1F<<0)
#define B577XX_FCOE_RX_DOORBELL_NEGATIVE_ARM_SHIFT 0
#define B577XX_FCOE_RX_DOORBELL_OPCODE (0x7<<5)
#define B577XX_FCOE_RX_DOORBELL_OPCODE_SHIFT 5
	__le16 doorbell_cq_cons;
};


/*
 * FCoE CONFQ element $$KEEP_ENDIANNESS$$
 */
struct fcoe_confqe {
	__le16 ox_id;
	__le16 rx_id;
	__le32 param;
};


/*
 * FCoE conection data base
 */
struct fcoe_conn_db {
#if defined(__BIG_ENDIAN)
	u16 rsrv0;
	u16 rq_prod;
#elif defined(__LITTLE_ENDIAN)
	u16 rq_prod;
	u16 rsrv0;
#endif
	u32 rsrv1;
	struct regpair cq_arm;
};


/*
 * FCoE CQ element $$KEEP_ENDIANNESS$$
 */
struct fcoe_cqe {
	__le16 wqe;
#define FCOE_CQE_CQE_INFO (0x3FFF<<0)
#define FCOE_CQE_CQE_INFO_SHIFT 0
#define FCOE_CQE_CQE_TYPE (0x1<<14)
#define FCOE_CQE_CQE_TYPE_SHIFT 14
#define FCOE_CQE_TOGGLE_BIT (0x1<<15)
#define FCOE_CQE_TOGGLE_BIT_SHIFT 15
};


/*
 * FCoE error/warning reporting entry $$KEEP_ENDIANNESS$$
 */
struct fcoe_partial_err_report_entry {
	__le32 err_warn_bitmap_lo;
	__le32 err_warn_bitmap_hi;
	__le32 tx_buf_off;
	__le32 rx_buf_off;
};

/*
 * FCoE error/warning reporting entry $$KEEP_ENDIANNESS$$
 */
struct fcoe_err_report_entry {
	struct fcoe_partial_err_report_entry data;
	struct fcoe_fc_hdr fc_hdr;
};


/*
 * FCoE hash table entry (32 bytes) $$KEEP_ENDIANNESS$$
 */
struct fcoe_hash_table_entry {
	u8 s_id_0;
	u8 s_id_1;
	u8 s_id_2;
	u8 d_id_0;
	u8 d_id_1;
	u8 d_id_2;
	__le16 dst_mac_addr_hi;
	__le16 dst_mac_addr_mid;
	__le16 dst_mac_addr_lo;
	__le16 src_mac_addr_hi;
	__le16 vlan_id;
	__le16 src_mac_addr_lo;
	__le16 src_mac_addr_mid;
	u8 vlan_flag;
	u8 reserved0;
	__le16 reserved1;
	__le32 reserved2;
	__le32 field_id;
#define FCOE_HASH_TABLE_ENTRY_CID (0xFFFFFF<<0)
#define FCOE_HASH_TABLE_ENTRY_CID_SHIFT 0
#define FCOE_HASH_TABLE_ENTRY_RESERVED3 (0x7F<<24)
#define FCOE_HASH_TABLE_ENTRY_RESERVED3_SHIFT 24
#define FCOE_HASH_TABLE_ENTRY_VALID (0x1<<31)
#define FCOE_HASH_TABLE_ENTRY_VALID_SHIFT 31
};


/*
 * FCoE LCQ element $$KEEP_ENDIANNESS$$
 */
struct fcoe_lcqe {
	__le32 wqe;
#define FCOE_LCQE_TASK_ID (0xFFFF<<0)
#define FCOE_LCQE_TASK_ID_SHIFT 0
#define FCOE_LCQE_LCQE_TYPE (0xFF<<16)
#define FCOE_LCQE_LCQE_TYPE_SHIFT 16
#define FCOE_LCQE_RESERVED (0xFF<<24)
#define FCOE_LCQE_RESERVED_SHIFT 24
};



/*
 * FCoE pending work request CQE $$KEEP_ENDIANNESS$$
 */
struct fcoe_pend_wq_cqe {
	__le16 wqe;
#define FCOE_PEND_WQ_CQE_TASK_ID (0x3FFF<<0)
#define FCOE_PEND_WQ_CQE_TASK_ID_SHIFT 0
#define FCOE_PEND_WQ_CQE_CQE_TYPE (0x1<<14)
#define FCOE_PEND_WQ_CQE_CQE_TYPE_SHIFT 14
#define FCOE_PEND_WQ_CQE_TOGGLE_BIT (0x1<<15)
#define FCOE_PEND_WQ_CQE_TOGGLE_BIT_SHIFT 15
};


/*
 * FCoE RX statistics parameters section#0 $$KEEP_ENDIANNESS$$
 */
struct fcoe_rx_stat_params_section0 {
	__le32 fcoe_rx_pkt_cnt;
	__le32 fcoe_rx_byte_cnt;
};


/*
 * FCoE RX statistics parameters section#1 $$KEEP_ENDIANNESS$$
 */
struct fcoe_rx_stat_params_section1 {
	__le32 fcoe_ver_cnt;
	__le32 fcoe_rx_drop_pkt_cnt;
};


/*
 * FCoE RX statistics parameters section#2 $$KEEP_ENDIANNESS$$
 */
struct fcoe_rx_stat_params_section2 {
	__le32 fc_crc_cnt;
	__le32 eofa_del_cnt;
	__le32 miss_frame_cnt;
	__le32 seq_timeout_cnt;
	__le32 drop_seq_cnt;
	__le32 fcoe_rx_drop_pkt_cnt;
	__le32 fcp_rx_pkt_cnt;
	__le32 reserved0;
};


/*
 * FCoE TX statistics parameters $$KEEP_ENDIANNESS$$
 */
struct fcoe_tx_stat_params {
	__le32 fcoe_tx_pkt_cnt;
	__le32 fcoe_tx_byte_cnt;
	__le32 fcp_tx_pkt_cnt;
	__le32 reserved0;
};

/*
 * FCoE statistics parameters $$KEEP_ENDIANNESS$$
 */
struct fcoe_statistics_params {
	struct fcoe_tx_stat_params tx_stat;
	struct fcoe_rx_stat_params_section0 rx_stat0;
	struct fcoe_rx_stat_params_section1 rx_stat1;
	struct fcoe_rx_stat_params_section2 rx_stat2;
};


/*
 * FCoE t2 hash table entry (64 bytes) $$KEEP_ENDIANNESS$$
 */
struct fcoe_t2_hash_table_entry {
	struct fcoe_hash_table_entry data;
	struct regpair next;
	struct regpair reserved0[3];
};



/*
 * FCoE unsolicited CQE $$KEEP_ENDIANNESS$$
 */
struct fcoe_unsolicited_cqe {
	__le16 wqe;
#define FCOE_UNSOLICITED_CQE_SUBTYPE (0x3<<0)
#define FCOE_UNSOLICITED_CQE_SUBTYPE_SHIFT 0
#define FCOE_UNSOLICITED_CQE_PKT_LEN (0xFFF<<2)
#define FCOE_UNSOLICITED_CQE_PKT_LEN_SHIFT 2
#define FCOE_UNSOLICITED_CQE_CQE_TYPE (0x1<<14)
#define FCOE_UNSOLICITED_CQE_CQE_TYPE_SHIFT 14
#define FCOE_UNSOLICITED_CQE_TOGGLE_BIT (0x1<<15)
#define FCOE_UNSOLICITED_CQE_TOGGLE_BIT_SHIFT 15
};

#endif /* __57XX_FCOE_HSI_LINUX_LE__ */
