/* 57xx_iscsi_hsi.h: Broadcom NetXtreme II iSCSI HSI.
 *
 * Copyright (c) 2006 - 2010 Broadcom Corporation
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation.
 *
 * Written by: Anil Veerabhadrappa (anilgv@broadcom.com)
 * Maintained by: Eddie Wai (eddie.wai@broadcom.com)
 */
#ifndef __57XX_ISCSI_HSI_LINUX_LE__
#define __57XX_ISCSI_HSI_LINUX_LE__

/*
 * iSCSI Async CQE
 */
struct bnx2i_async_msg {
#if defined(__BIG_ENDIAN)
	u8 op_code;
	u8 reserved1;
	u16 reserved0;
#elif defined(__LITTLE_ENDIAN)
	u16 reserved0;
	u8 reserved1;
	u8 op_code;
#endif
	u32 reserved2;
	u32 exp_cmd_sn;
	u32 max_cmd_sn;
	u32 reserved3[2];
#if defined(__BIG_ENDIAN)
	u16 reserved5;
	u8 err_code;
	u8 reserved4;
#elif defined(__LITTLE_ENDIAN)
	u8 reserved4;
	u8 err_code;
	u16 reserved5;
#endif
	u32 reserved6;
	u32 lun[2];
#if defined(__BIG_ENDIAN)
	u8 async_event;
	u8 async_vcode;
	u16 param1;
#elif defined(__LITTLE_ENDIAN)
	u16 param1;
	u8 async_vcode;
	u8 async_event;
#endif
#if defined(__BIG_ENDIAN)
	u16 param2;
	u16 param3;
#elif defined(__LITTLE_ENDIAN)
	u16 param3;
	u16 param2;
#endif
	u32 reserved7[3];
	u32 cq_req_sn;
};


/*
 * iSCSI Buffer Descriptor (BD)
 */
struct iscsi_bd {
	u32 buffer_addr_hi;
	u32 buffer_addr_lo;
#if defined(__BIG_ENDIAN)
	u16 reserved0;
	u16 buffer_length;
#elif defined(__LITTLE_ENDIAN)
	u16 buffer_length;
	u16 reserved0;
#endif
#if defined(__BIG_ENDIAN)
	u16 reserved3;
	u16 flags;
#define ISCSI_BD_RESERVED1 (0x3F<<0)
#define ISCSI_BD_RESERVED1_SHIFT 0
#define ISCSI_BD_LAST_IN_BD_CHAIN (0x1<<6)
#define ISCSI_BD_LAST_IN_BD_CHAIN_SHIFT 6
#define ISCSI_BD_FIRST_IN_BD_CHAIN (0x1<<7)
#define ISCSI_BD_FIRST_IN_BD_CHAIN_SHIFT 7
#define ISCSI_BD_RESERVED2 (0xFF<<8)
#define ISCSI_BD_RESERVED2_SHIFT 8
#elif defined(__LITTLE_ENDIAN)
	u16 flags;
#define ISCSI_BD_RESERVED1 (0x3F<<0)
#define ISCSI_BD_RESERVED1_SHIFT 0
#define ISCSI_BD_LAST_IN_BD_CHAIN (0x1<<6)
#define ISCSI_BD_LAST_IN_BD_CHAIN_SHIFT 6
#define ISCSI_BD_FIRST_IN_BD_CHAIN (0x1<<7)
#define ISCSI_BD_FIRST_IN_BD_CHAIN_SHIFT 7
#define ISCSI_BD_RESERVED2 (0xFF<<8)
#define ISCSI_BD_RESERVED2_SHIFT 8
	u16 reserved3;
#endif
};


/*
 * iSCSI Cleanup SQ WQE
 */
struct bnx2i_cleanup_request {
#if defined(__BIG_ENDIAN)
	u8 op_code;
	u8 reserved1;
	u16 reserved0;
#elif defined(__LITTLE_ENDIAN)
	u16 reserved0;
	u8 reserved1;
	u8 op_code;
#endif
	u32 reserved2[3];
#if defined(__BIG_ENDIAN)
	u16 reserved3;
	u16 itt;
#define ISCSI_CLEANUP_REQUEST_INDEX (0x3FFF<<0)
#define ISCSI_CLEANUP_REQUEST_INDEX_SHIFT 0
#define ISCSI_CLEANUP_REQUEST_TYPE (0x3<<14)
#define ISCSI_CLEANUP_REQUEST_TYPE_SHIFT 14
#elif defined(__LITTLE_ENDIAN)
	u16 itt;
#define ISCSI_CLEANUP_REQUEST_INDEX (0x3FFF<<0)
#define ISCSI_CLEANUP_REQUEST_INDEX_SHIFT 0
#define ISCSI_CLEANUP_REQUEST_TYPE (0x3<<14)
#define ISCSI_CLEANUP_REQUEST_TYPE_SHIFT 14
	u16 reserved3;
#endif
	u32 reserved4[10];
#if defined(__BIG_ENDIAN)
	u8 cq_index;
	u8 reserved6;
	u16 reserved5;
#elif defined(__LITTLE_ENDIAN)
	u16 reserved5;
	u8 reserved6;
	u8 cq_index;
#endif
};


/*
 * iSCSI Cleanup CQE
 */
struct bnx2i_cleanup_response {
#if defined(__BIG_ENDIAN)
	u8 op_code;
	u8 status;
	u16 reserved0;
#elif defined(__LITTLE_ENDIAN)
	u16 reserved0;
	u8 status;
	u8 op_code;
#endif
	u32 reserved1[3];
	u32 reserved2[2];
#if defined(__BIG_ENDIAN)
	u16 reserved4;
	u8 err_code;
	u8 reserved3;
#elif defined(__LITTLE_ENDIAN)
	u8 reserved3;
	u8 err_code;
	u16 reserved4;
#endif
	u32 reserved5[7];
#if defined(__BIG_ENDIAN)
	u16 reserved6;
	u16 itt;
#define ISCSI_CLEANUP_RESPONSE_INDEX (0x3FFF<<0)
#define ISCSI_CLEANUP_RESPONSE_INDEX_SHIFT 0
#define ISCSI_CLEANUP_RESPONSE_TYPE (0x3<<14)
#define ISCSI_CLEANUP_RESPONSE_TYPE_SHIFT 14
#elif defined(__LITTLE_ENDIAN)
	u16 itt;
#define ISCSI_CLEANUP_RESPONSE_INDEX (0x3FFF<<0)
#define ISCSI_CLEANUP_RESPONSE_INDEX_SHIFT 0
#define ISCSI_CLEANUP_RESPONSE_TYPE (0x3<<14)
#define ISCSI_CLEANUP_RESPONSE_TYPE_SHIFT 14
	u16 reserved6;
#endif
	u32 cq_req_sn;
};


/*
 * SCSI read/write SQ WQE
 */
struct bnx2i_cmd_request {
#if defined(__BIG_ENDIAN)
	u8 op_code;
	u8 op_attr;
#define ISCSI_CMD_REQUEST_TASK_ATTR (0x7<<0)
#define ISCSI_CMD_REQUEST_TASK_ATTR_SHIFT 0
#define ISCSI_CMD_REQUEST_RESERVED1 (0x3<<3)
#define ISCSI_CMD_REQUEST_RESERVED1_SHIFT 3
#define ISCSI_CMD_REQUEST_WRITE (0x1<<5)
#define ISCSI_CMD_REQUEST_WRITE_SHIFT 5
#define ISCSI_CMD_REQUEST_READ (0x1<<6)
#define ISCSI_CMD_REQUEST_READ_SHIFT 6
#define ISCSI_CMD_REQUEST_FINAL (0x1<<7)
#define ISCSI_CMD_REQUEST_FINAL_SHIFT 7
	u16 reserved0;
#elif defined(__LITTLE_ENDIAN)
	u16 reserved0;
	u8 op_attr;
#define ISCSI_CMD_REQUEST_TASK_ATTR (0x7<<0)
#define ISCSI_CMD_REQUEST_TASK_ATTR_SHIFT 0
#define ISCSI_CMD_REQUEST_RESERVED1 (0x3<<3)
#define ISCSI_CMD_REQUEST_RESERVED1_SHIFT 3
#define ISCSI_CMD_REQUEST_WRITE (0x1<<5)
#define ISCSI_CMD_REQUEST_WRITE_SHIFT 5
#define ISCSI_CMD_REQUEST_READ (0x1<<6)
#define ISCSI_CMD_REQUEST_READ_SHIFT 6
#define ISCSI_CMD_REQUEST_FINAL (0x1<<7)
#define ISCSI_CMD_REQUEST_FINAL_SHIFT 7
	u8 op_code;
#endif
#if defined(__BIG_ENDIAN)
	u16 ud_buffer_offset;
	u16 sd_buffer_offset;
#elif defined(__LITTLE_ENDIAN)
	u16 sd_buffer_offset;
	u16 ud_buffer_offset;
#endif
	u32 lun[2];
#if defined(__BIG_ENDIAN)
	u16 reserved2;
	u16 itt;
#define ISCSI_CMD_REQUEST_INDEX (0x3FFF<<0)
#define ISCSI_CMD_REQUEST_INDEX_SHIFT 0
#define ISCSI_CMD_REQUEST_TYPE (0x3<<14)
#define ISCSI_CMD_REQUEST_TYPE_SHIFT 14
#elif defined(__LITTLE_ENDIAN)
	u16 itt;
#define ISCSI_CMD_REQUEST_INDEX (0x3FFF<<0)
#define ISCSI_CMD_REQUEST_INDEX_SHIFT 0
#define ISCSI_CMD_REQUEST_TYPE (0x3<<14)
#define ISCSI_CMD_REQUEST_TYPE_SHIFT 14
	u16 reserved2;
#endif
	u32 total_data_transfer_length;
	u32 cmd_sn;
	u32 reserved3;
	u32 cdb[4];
	u32 zero_fill;
	u32 bd_list_addr_lo;
	u32 bd_list_addr_hi;
#if defined(__BIG_ENDIAN)
	u8 cq_index;
	u8 sd_start_bd_index;
	u8 ud_start_bd_index;
	u8 num_bds;
#elif defined(__LITTLE_ENDIAN)
	u8 num_bds;
	u8 ud_start_bd_index;
	u8 sd_start_bd_index;
	u8 cq_index;
#endif
};


/*
 * task statistics for write response
 */
struct bnx2i_write_resp_task_stat {
	u32 num_data_ins;
};

/*
 * task statistics for read response
 */
struct bnx2i_read_resp_task_stat {
#if defined(__BIG_ENDIAN)
	u16 num_data_outs;
	u16 num_r2ts;
#elif defined(__LITTLE_ENDIAN)
	u16 num_r2ts;
	u16 num_data_outs;
#endif
};

/*
 * task statistics for iSCSI cmd response
 */
union bnx2i_cmd_resp_task_stat {
	struct bnx2i_write_resp_task_stat write_stat;
	struct bnx2i_read_resp_task_stat read_stat;
};

/*
 * SCSI Command CQE
 */
struct bnx2i_cmd_response {
#if defined(__BIG_ENDIAN)
	u8 op_code;
	u8 response_flags;
#define ISCSI_CMD_RESPONSE_RESERVED0 (0x1<<0)
#define ISCSI_CMD_RESPONSE_RESERVED0_SHIFT 0
#define ISCSI_CMD_RESPONSE_RESIDUAL_UNDERFLOW (0x1<<1)
#define ISCSI_CMD_RESPONSE_RESIDUAL_UNDERFLOW_SHIFT 1
#define ISCSI_CMD_RESPONSE_RESIDUAL_OVERFLOW (0x1<<2)
#define ISCSI_CMD_RESPONSE_RESIDUAL_OVERFLOW_SHIFT 2
#define ISCSI_CMD_RESPONSE_BR_RESIDUAL_UNDERFLOW (0x1<<3)
#define ISCSI_CMD_RESPONSE_BR_RESIDUAL_UNDERFLOW_SHIFT 3
#define ISCSI_CMD_RESPONSE_BR_RESIDUAL_OVERFLOW (0x1<<4)
#define ISCSI_CMD_RESPONSE_BR_RESIDUAL_OVERFLOW_SHIFT 4
#define ISCSI_CMD_RESPONSE_RESERVED1 (0x7<<5)
#define ISCSI_CMD_RESPONSE_RESERVED1_SHIFT 5
	u8 response;
	u8 status;
#elif defined(__LITTLE_ENDIAN)
	u8 status;
	u8 response;
	u8 response_flags;
#define ISCSI_CMD_RESPONSE_RESERVED0 (0x1<<0)
#define ISCSI_CMD_RESPONSE_RESERVED0_SHIFT 0
#define ISCSI_CMD_RESPONSE_RESIDUAL_UNDERFLOW (0x1<<1)
#define ISCSI_CMD_RESPONSE_RESIDUAL_UNDERFLOW_SHIFT 1
#define ISCSI_CMD_RESPONSE_RESIDUAL_OVERFLOW (0x1<<2)
#define ISCSI_CMD_RESPONSE_RESIDUAL_OVERFLOW_SHIFT 2
#define ISCSI_CMD_RESPONSE_BR_RESIDUAL_UNDERFLOW (0x1<<3)
#define ISCSI_CMD_RESPONSE_BR_RESIDUAL_UNDERFLOW_SHIFT 3
#define ISCSI_CMD_RESPONSE_BR_RESIDUAL_OVERFLOW (0x1<<4)
#define ISCSI_CMD_RESPONSE_BR_RESIDUAL_OVERFLOW_SHIFT 4
#define ISCSI_CMD_RESPONSE_RESERVED1 (0x7<<5)
#define ISCSI_CMD_RESPONSE_RESERVED1_SHIFT 5
	u8 op_code;
#endif
	u32 data_length;
	u32 exp_cmd_sn;
	u32 max_cmd_sn;
	u32 reserved2;
	u32 residual_count;
#if defined(__BIG_ENDIAN)
	u16 reserved4;
	u8 err_code;
	u8 reserved3;
#elif defined(__LITTLE_ENDIAN)
	u8 reserved3;
	u8 err_code;
	u16 reserved4;
#endif
	u32 reserved5[5];
	union bnx2i_cmd_resp_task_stat task_stat;
	u32 reserved6;
#if defined(__BIG_ENDIAN)
	u16 reserved7;
	u16 itt;
#define ISCSI_CMD_RESPONSE_INDEX (0x3FFF<<0)
#define ISCSI_CMD_RESPONSE_INDEX_SHIFT 0
#define ISCSI_CMD_RESPONSE_TYPE (0x3<<14)
#define ISCSI_CMD_RESPONSE_TYPE_SHIFT 14
#elif defined(__LITTLE_ENDIAN)
	u16 itt;
#define ISCSI_CMD_RESPONSE_INDEX (0x3FFF<<0)
#define ISCSI_CMD_RESPONSE_INDEX_SHIFT 0
#define ISCSI_CMD_RESPONSE_TYPE (0x3<<14)
#define ISCSI_CMD_RESPONSE_TYPE_SHIFT 14
	u16 reserved7;
#endif
	u32 cq_req_sn;
};



/*
 * firmware middle-path request SQ WQE
 */
struct bnx2i_fw_mp_request {
#if defined(__BIG_ENDIAN)
	u8 op_code;
	u8 op_attr;
	u16 hdr_opaque1;
#elif defined(__LITTLE_ENDIAN)
	u16 hdr_opaque1;
	u8 op_attr;
	u8 op_code;
#endif
	u32 data_length;
	u32 hdr_opaque2[2];
#if defined(__BIG_ENDIAN)
	u16 reserved0;
	u16 itt;
#define ISCSI_FW_MP_REQUEST_INDEX (0x3FFF<<0)
#define ISCSI_FW_MP_REQUEST_INDEX_SHIFT 0
#define ISCSI_FW_MP_REQUEST_TYPE (0x3<<14)
#define ISCSI_FW_MP_REQUEST_TYPE_SHIFT 14
#elif defined(__LITTLE_ENDIAN)
	u16 itt;
#define ISCSI_FW_MP_REQUEST_INDEX (0x3FFF<<0)
#define ISCSI_FW_MP_REQUEST_INDEX_SHIFT 0
#define ISCSI_FW_MP_REQUEST_TYPE (0x3<<14)
#define ISCSI_FW_MP_REQUEST_TYPE_SHIFT 14
	u16 reserved0;
#endif
	u32 hdr_opaque3[4];
	u32 resp_bd_list_addr_lo;
	u32 resp_bd_list_addr_hi;
	u32 resp_buffer;
#define ISCSI_FW_MP_REQUEST_RESP_BUFFER_LENGTH (0xFFFFFF<<0)
#define ISCSI_FW_MP_REQUEST_RESP_BUFFER_LENGTH_SHIFT 0
#define ISCSI_FW_MP_REQUEST_NUM_RESP_BDS (0xFF<<24)
#define ISCSI_FW_MP_REQUEST_NUM_RESP_BDS_SHIFT 24
#if defined(__BIG_ENDIAN)
	u16 reserved4;
	u8 reserved3;
	u8 flags;
#define ISCSI_FW_MP_REQUEST_RESERVED1 (0x1<<0)
#define ISCSI_FW_MP_REQUEST_RESERVED1_SHIFT 0
#define ISCSI_FW_MP_REQUEST_LOCAL_COMPLETION (0x1<<1)
#define ISCSI_FW_MP_REQUEST_LOCAL_COMPLETION_SHIFT 1
#define ISCSI_FW_MP_REQUEST_UPDATE_EXP_STAT_SN (0x1<<2)
#define ISCSI_FW_MP_REQUEST_UPDATE_EXP_STAT_SN_SHIFT 2
#define ISCSI_FW_MP_REQUEST_RESERVED2 (0x1F<<3)
#define ISCSI_FW_MP_REQUEST_RESERVED2_SHIFT 3
#elif defined(__LITTLE_ENDIAN)
	u8 flags;
#define ISCSI_FW_MP_REQUEST_RESERVED1 (0x1<<0)
#define ISCSI_FW_MP_REQUEST_RESERVED1_SHIFT 0
#define ISCSI_FW_MP_REQUEST_LOCAL_COMPLETION (0x1<<1)
#define ISCSI_FW_MP_REQUEST_LOCAL_COMPLETION_SHIFT 1
#define ISCSI_FW_MP_REQUEST_UPDATE_EXP_STAT_SN (0x1<<2)
#define ISCSI_FW_MP_REQUEST_UPDATE_EXP_STAT_SN_SHIFT 2
#define ISCSI_FW_MP_REQUEST_RESERVED2 (0x1F<<3)
#define ISCSI_FW_MP_REQUEST_RESERVED2_SHIFT 3
	u8 reserved3;
	u16 reserved4;
#endif
	u32 bd_list_addr_lo;
	u32 bd_list_addr_hi;
#if defined(__BIG_ENDIAN)
	u8 cq_index;
	u8 reserved6;
	u8 reserved5;
	u8 num_bds;
#elif defined(__LITTLE_ENDIAN)
	u8 num_bds;
	u8 reserved5;
	u8 reserved6;
	u8 cq_index;
#endif
};


/*
 * firmware response - CQE: used only by firmware
 */
struct bnx2i_fw_response {
	u32 hdr_dword1[2];
	u32 hdr_exp_cmd_sn;
	u32 hdr_max_cmd_sn;
	u32 hdr_ttt;
	u32 hdr_res_cnt;
	u32 cqe_flags;
#define ISCSI_FW_RESPONSE_RESERVED2 (0xFF<<0)
#define ISCSI_FW_RESPONSE_RESERVED2_SHIFT 0
#define ISCSI_FW_RESPONSE_ERR_CODE (0xFF<<8)
#define ISCSI_FW_RESPONSE_ERR_CODE_SHIFT 8
#define ISCSI_FW_RESPONSE_RESERVED3 (0xFFFF<<16)
#define ISCSI_FW_RESPONSE_RESERVED3_SHIFT 16
	u32 stat_sn;
	u32 hdr_dword2[2];
	u32 hdr_dword3[2];
	u32 task_stat;
	u32 reserved0;
	u32 hdr_itt;
	u32 cq_req_sn;
};


/*
 * iSCSI KCQ CQE parameters
 */
union iscsi_kcqe_params {
	u32 reserved0[4];
};

/*
 * iSCSI KCQ CQE
 */
struct iscsi_kcqe {
	u32 iscsi_conn_id;
	u32 completion_status;
	u32 iscsi_conn_context_id;
	union iscsi_kcqe_params params;
#if defined(__BIG_ENDIAN)
	u8 flags;
#define ISCSI_KCQE_RESERVED0 (0xF<<0)
#define ISCSI_KCQE_RESERVED0_SHIFT 0
#define ISCSI_KCQE_LAYER_CODE (0x7<<4)
#define ISCSI_KCQE_LAYER_CODE_SHIFT 4
#define ISCSI_KCQE_RESERVED1 (0x1<<7)
#define ISCSI_KCQE_RESERVED1_SHIFT 7
	u8 op_code;
	u16 qe_self_seq;
#elif defined(__LITTLE_ENDIAN)
	u16 qe_self_seq;
	u8 op_code;
	u8 flags;
#define ISCSI_KCQE_RESERVED0 (0xF<<0)
#define ISCSI_KCQE_RESERVED0_SHIFT 0
#define ISCSI_KCQE_LAYER_CODE (0x7<<4)
#define ISCSI_KCQE_LAYER_CODE_SHIFT 4
#define ISCSI_KCQE_RESERVED1 (0x1<<7)
#define ISCSI_KCQE_RESERVED1_SHIFT 7
#endif
};



/*
 * iSCSI KWQE header
 */
struct iscsi_kwqe_header {
#if defined(__BIG_ENDIAN)
	u8 flags;
#define ISCSI_KWQE_HEADER_RESERVED0 (0xF<<0)
#define ISCSI_KWQE_HEADER_RESERVED0_SHIFT 0
#define ISCSI_KWQE_HEADER_LAYER_CODE (0x7<<4)
#define ISCSI_KWQE_HEADER_LAYER_CODE_SHIFT 4
#define ISCSI_KWQE_HEADER_RESERVED1 (0x1<<7)
#define ISCSI_KWQE_HEADER_RESERVED1_SHIFT 7
	u8 op_code;
#elif defined(__LITTLE_ENDIAN)
	u8 op_code;
	u8 flags;
#define ISCSI_KWQE_HEADER_RESERVED0 (0xF<<0)
#define ISCSI_KWQE_HEADER_RESERVED0_SHIFT 0
#define ISCSI_KWQE_HEADER_LAYER_CODE (0x7<<4)
#define ISCSI_KWQE_HEADER_LAYER_CODE_SHIFT 4
#define ISCSI_KWQE_HEADER_RESERVED1 (0x1<<7)
#define ISCSI_KWQE_HEADER_RESERVED1_SHIFT 7
#endif
};

/*
 * iSCSI firmware init request 1
 */
struct iscsi_kwqe_init1 {
#if defined(__BIG_ENDIAN)
	struct iscsi_kwqe_header hdr;
	u8 reserved0;
	u8 num_cqs;
#elif defined(__LITTLE_ENDIAN)
	u8 num_cqs;
	u8 reserved0;
	struct iscsi_kwqe_header hdr;
#endif
	u32 dummy_buffer_addr_lo;
	u32 dummy_buffer_addr_hi;
#if defined(__BIG_ENDIAN)
	u16 num_ccells_per_conn;
	u16 num_tasks_per_conn;
#elif defined(__LITTLE_ENDIAN)
	u16 num_tasks_per_conn;
	u16 num_ccells_per_conn;
#endif
#if defined(__BIG_ENDIAN)
	u16 sq_wqes_per_page;
	u16 sq_num_wqes;
#elif defined(__LITTLE_ENDIAN)
	u16 sq_num_wqes;
	u16 sq_wqes_per_page;
#endif
#if defined(__BIG_ENDIAN)
	u8 cq_log_wqes_per_page;
	u8 flags;
#define ISCSI_KWQE_INIT1_PAGE_SIZE (0xF<<0)
#define ISCSI_KWQE_INIT1_PAGE_SIZE_SHIFT 0
#define ISCSI_KWQE_INIT1_DELAYED_ACK_ENABLE (0x1<<4)
#define ISCSI_KWQE_INIT1_DELAYED_ACK_ENABLE_SHIFT 4
#define ISCSI_KWQE_INIT1_KEEP_ALIVE_ENABLE (0x1<<5)
#define ISCSI_KWQE_INIT1_KEEP_ALIVE_ENABLE_SHIFT 5
#define ISCSI_KWQE_INIT1_RESERVED1 (0x3<<6)
#define ISCSI_KWQE_INIT1_RESERVED1_SHIFT 6
	u16 cq_num_wqes;
#elif defined(__LITTLE_ENDIAN)
	u16 cq_num_wqes;
	u8 flags;
#define ISCSI_KWQE_INIT1_PAGE_SIZE (0xF<<0)
#define ISCSI_KWQE_INIT1_PAGE_SIZE_SHIFT 0
#define ISCSI_KWQE_INIT1_DELAYED_ACK_ENABLE (0x1<<4)
#define ISCSI_KWQE_INIT1_DELAYED_ACK_ENABLE_SHIFT 4
#define ISCSI_KWQE_INIT1_KEEP_ALIVE_ENABLE (0x1<<5)
#define ISCSI_KWQE_INIT1_KEEP_ALIVE_ENABLE_SHIFT 5
#define ISCSI_KWQE_INIT1_RESERVED1 (0x3<<6)
#define ISCSI_KWQE_INIT1_RESERVED1_SHIFT 6
	u8 cq_log_wqes_per_page;
#endif
#if defined(__BIG_ENDIAN)
	u16 cq_num_pages;
	u16 sq_num_pages;
#elif defined(__LITTLE_ENDIAN)
	u16 sq_num_pages;
	u16 cq_num_pages;
#endif
#if defined(__BIG_ENDIAN)
	u16 rq_buffer_size;
	u16 rq_num_wqes;
#elif defined(__LITTLE_ENDIAN)
	u16 rq_num_wqes;
	u16 rq_buffer_size;
#endif
};

/*
 * iSCSI firmware init request 2
 */
struct iscsi_kwqe_init2 {
#if defined(__BIG_ENDIAN)
	struct iscsi_kwqe_header hdr;
	u16 max_cq_sqn;
#elif defined(__LITTLE_ENDIAN)
	u16 max_cq_sqn;
	struct iscsi_kwqe_header hdr;
#endif
	u32 error_bit_map[2];
	u32 reserved1[5];
};

/*
 * Initial iSCSI connection offload request 1
 */
struct iscsi_kwqe_conn_offload1 {
#if defined(__BIG_ENDIAN)
	struct iscsi_kwqe_header hdr;
	u16 iscsi_conn_id;
#elif defined(__LITTLE_ENDIAN)
	u16 iscsi_conn_id;
	struct iscsi_kwqe_header hdr;
#endif
	u32 sq_page_table_addr_lo;
	u32 sq_page_table_addr_hi;
	u32 cq_page_table_addr_lo;
	u32 cq_page_table_addr_hi;
	u32 reserved0[3];
};

/*
 * iSCSI Page Table Entry (PTE)
 */
struct iscsi_pte {
	u32 hi;
	u32 lo;
};

/*
 * Initial iSCSI connection offload request 2
 */
struct iscsi_kwqe_conn_offload2 {
#if defined(__BIG_ENDIAN)
	struct iscsi_kwqe_header hdr;
	u16 reserved0;
#elif defined(__LITTLE_ENDIAN)
	u16 reserved0;
	struct iscsi_kwqe_header hdr;
#endif
	u32 rq_page_table_addr_lo;
	u32 rq_page_table_addr_hi;
	struct iscsi_pte sq_first_pte;
	struct iscsi_pte cq_first_pte;
	u32 num_additional_wqes;
};


/*
 * Initial iSCSI connection offload request 3
 */
struct iscsi_kwqe_conn_offload3 {
#if defined(__BIG_ENDIAN)
	struct iscsi_kwqe_header hdr;
	u16 reserved0;
#elif defined(__LITTLE_ENDIAN)
	u16 reserved0;
	struct iscsi_kwqe_header hdr;
#endif
	u32 reserved1;
	struct iscsi_pte qp_first_pte[3];
};


/*
 * iSCSI connection update request
 */
struct iscsi_kwqe_conn_update {
#if defined(__BIG_ENDIAN)
	struct iscsi_kwqe_header hdr;
	u16 reserved0;
#elif defined(__LITTLE_ENDIAN)
	u16 reserved0;
	struct iscsi_kwqe_header hdr;
#endif
#if defined(__BIG_ENDIAN)
	u8 session_error_recovery_level;
	u8 max_outstanding_r2ts;
	u8 reserved2;
	u8 conn_flags;
#define ISCSI_KWQE_CONN_UPDATE_HEADER_DIGEST (0x1<<0)
#define ISCSI_KWQE_CONN_UPDATE_HEADER_DIGEST_SHIFT 0
#define ISCSI_KWQE_CONN_UPDATE_DATA_DIGEST (0x1<<1)
#define ISCSI_KWQE_CONN_UPDATE_DATA_DIGEST_SHIFT 1
#define ISCSI_KWQE_CONN_UPDATE_INITIAL_R2T (0x1<<2)
#define ISCSI_KWQE_CONN_UPDATE_INITIAL_R2T_SHIFT 2
#define ISCSI_KWQE_CONN_UPDATE_IMMEDIATE_DATA (0x1<<3)
#define ISCSI_KWQE_CONN_UPDATE_IMMEDIATE_DATA_SHIFT 3
#define ISCSI_KWQE_CONN_UPDATE_RESERVED1 (0xF<<4)
#define ISCSI_KWQE_CONN_UPDATE_RESERVED1_SHIFT 4
#elif defined(__LITTLE_ENDIAN)
	u8 conn_flags;
#define ISCSI_KWQE_CONN_UPDATE_HEADER_DIGEST (0x1<<0)
#define ISCSI_KWQE_CONN_UPDATE_HEADER_DIGEST_SHIFT 0
#define ISCSI_KWQE_CONN_UPDATE_DATA_DIGEST (0x1<<1)
#define ISCSI_KWQE_CONN_UPDATE_DATA_DIGEST_SHIFT 1
#define ISCSI_KWQE_CONN_UPDATE_INITIAL_R2T (0x1<<2)
#define ISCSI_KWQE_CONN_UPDATE_INITIAL_R2T_SHIFT 2
#define ISCSI_KWQE_CONN_UPDATE_IMMEDIATE_DATA (0x1<<3)
#define ISCSI_KWQE_CONN_UPDATE_IMMEDIATE_DATA_SHIFT 3
#define ISCSI_KWQE_CONN_UPDATE_RESERVED1 (0xF<<4)
#define ISCSI_KWQE_CONN_UPDATE_RESERVED1_SHIFT 4
	u8 reserved2;
	u8 max_outstanding_r2ts;
	u8 session_error_recovery_level;
#endif
	u32 context_id;
	u32 max_send_pdu_length;
	u32 max_recv_pdu_length;
	u32 first_burst_length;
	u32 max_burst_length;
	u32 exp_stat_sn;
};

/*
 * iSCSI destroy connection request
 */
struct iscsi_kwqe_conn_destroy {
#if defined(__BIG_ENDIAN)
	struct iscsi_kwqe_header hdr;
	u16 reserved0;
#elif defined(__LITTLE_ENDIAN)
	u16 reserved0;
	struct iscsi_kwqe_header hdr;
#endif
	u32 context_id;
	u32 reserved1[6];
};

/*
 * iSCSI KWQ WQE
 */
union iscsi_kwqe {
	struct iscsi_kwqe_init1 init1;
	struct iscsi_kwqe_init2 init2;
	struct iscsi_kwqe_conn_offload1 conn_offload1;
	struct iscsi_kwqe_conn_offload2 conn_offload2;
	struct iscsi_kwqe_conn_update conn_update;
	struct iscsi_kwqe_conn_destroy conn_destroy;
};

/*
 * iSCSI Login SQ WQE
 */
struct bnx2i_login_request {
#if defined(__BIG_ENDIAN)
	u8 op_code;
	u8 op_attr;
#define ISCSI_LOGIN_REQUEST_NEXT_STAGE (0x3<<0)
#define ISCSI_LOGIN_REQUEST_NEXT_STAGE_SHIFT 0
#define ISCSI_LOGIN_REQUEST_CURRENT_STAGE (0x3<<2)
#define ISCSI_LOGIN_REQUEST_CURRENT_STAGE_SHIFT 2
#define ISCSI_LOGIN_REQUEST_RESERVED0 (0x3<<4)
#define ISCSI_LOGIN_REQUEST_RESERVED0_SHIFT 4
#define ISCSI_LOGIN_REQUEST_CONT (0x1<<6)
#define ISCSI_LOGIN_REQUEST_CONT_SHIFT 6
#define ISCSI_LOGIN_REQUEST_TRANSIT (0x1<<7)
#define ISCSI_LOGIN_REQUEST_TRANSIT_SHIFT 7
	u8 version_max;
	u8 version_min;
#elif defined(__LITTLE_ENDIAN)
	u8 version_min;
	u8 version_max;
	u8 op_attr;
#define ISCSI_LOGIN_REQUEST_NEXT_STAGE (0x3<<0)
#define ISCSI_LOGIN_REQUEST_NEXT_STAGE_SHIFT 0
#define ISCSI_LOGIN_REQUEST_CURRENT_STAGE (0x3<<2)
#define ISCSI_LOGIN_REQUEST_CURRENT_STAGE_SHIFT 2
#define ISCSI_LOGIN_REQUEST_RESERVED0 (0x3<<4)
#define ISCSI_LOGIN_REQUEST_RESERVED0_SHIFT 4
#define ISCSI_LOGIN_REQUEST_CONT (0x1<<6)
#define ISCSI_LOGIN_REQUEST_CONT_SHIFT 6
#define ISCSI_LOGIN_REQUEST_TRANSIT (0x1<<7)
#define ISCSI_LOGIN_REQUEST_TRANSIT_SHIFT 7
	u8 op_code;
#endif
	u32 data_length;
	u32 isid_lo;
#if defined(__BIG_ENDIAN)
	u16 isid_hi;
	u16 tsih;
#elif defined(__LITTLE_ENDIAN)
	u16 tsih;
	u16 isid_hi;
#endif
#if defined(__BIG_ENDIAN)
	u16 reserved2;
	u16 itt;
#define ISCSI_LOGIN_REQUEST_INDEX (0x3FFF<<0)
#define ISCSI_LOGIN_REQUEST_INDEX_SHIFT 0
#define ISCSI_LOGIN_REQUEST_TYPE (0x3<<14)
#define ISCSI_LOGIN_REQUEST_TYPE_SHIFT 14
#elif defined(__LITTLE_ENDIAN)
	u16 itt;
#define ISCSI_LOGIN_REQUEST_INDEX (0x3FFF<<0)
#define ISCSI_LOGIN_REQUEST_INDEX_SHIFT 0
#define ISCSI_LOGIN_REQUEST_TYPE (0x3<<14)
#define ISCSI_LOGIN_REQUEST_TYPE_SHIFT 14
	u16 reserved2;
#endif
#if defined(__BIG_ENDIAN)
	u16 cid;
	u16 reserved3;
#elif defined(__LITTLE_ENDIAN)
	u16 reserved3;
	u16 cid;
#endif
	u32 cmd_sn;
	u32 exp_stat_sn;
	u32 reserved4;
	u32 resp_bd_list_addr_lo;
	u32 resp_bd_list_addr_hi;
	u32 resp_buffer;
#define ISCSI_LOGIN_REQUEST_RESP_BUFFER_LENGTH (0xFFFFFF<<0)
#define ISCSI_LOGIN_REQUEST_RESP_BUFFER_LENGTH_SHIFT 0
#define ISCSI_LOGIN_REQUEST_NUM_RESP_BDS (0xFF<<24)
#define ISCSI_LOGIN_REQUEST_NUM_RESP_BDS_SHIFT 24
#if defined(__BIG_ENDIAN)
	u16 reserved8;
	u8 reserved7;
	u8 flags;
#define ISCSI_LOGIN_REQUEST_RESERVED5 (0x3<<0)
#define ISCSI_LOGIN_REQUEST_RESERVED5_SHIFT 0
#define ISCSI_LOGIN_REQUEST_UPDATE_EXP_STAT_SN (0x1<<2)
#define ISCSI_LOGIN_REQUEST_UPDATE_EXP_STAT_SN_SHIFT 2
#define ISCSI_LOGIN_REQUEST_RESERVED6 (0x1F<<3)
#define ISCSI_LOGIN_REQUEST_RESERVED6_SHIFT 3
#elif defined(__LITTLE_ENDIAN)
	u8 flags;
#define ISCSI_LOGIN_REQUEST_RESERVED5 (0x3<<0)
#define ISCSI_LOGIN_REQUEST_RESERVED5_SHIFT 0
#define ISCSI_LOGIN_REQUEST_UPDATE_EXP_STAT_SN (0x1<<2)
#define ISCSI_LOGIN_REQUEST_UPDATE_EXP_STAT_SN_SHIFT 2
#define ISCSI_LOGIN_REQUEST_RESERVED6 (0x1F<<3)
#define ISCSI_LOGIN_REQUEST_RESERVED6_SHIFT 3
	u8 reserved7;
	u16 reserved8;
#endif
	u32 bd_list_addr_lo;
	u32 bd_list_addr_hi;
#if defined(__BIG_ENDIAN)
	u8 cq_index;
	u8 reserved10;
	u8 reserved9;
	u8 num_bds;
#elif defined(__LITTLE_ENDIAN)
	u8 num_bds;
	u8 reserved9;
	u8 reserved10;
	u8 cq_index;
#endif
};


/*
 * iSCSI Login CQE
 */
struct bnx2i_login_response {
#if defined(__BIG_ENDIAN)
	u8 op_code;
	u8 response_flags;
#define ISCSI_LOGIN_RESPONSE_NEXT_STAGE (0x3<<0)
#define ISCSI_LOGIN_RESPONSE_NEXT_STAGE_SHIFT 0
#define ISCSI_LOGIN_RESPONSE_CURRENT_STAGE (0x3<<2)
#define ISCSI_LOGIN_RESPONSE_CURRENT_STAGE_SHIFT 2
#define ISCSI_LOGIN_RESPONSE_RESERVED0 (0x3<<4)
#define ISCSI_LOGIN_RESPONSE_RESERVED0_SHIFT 4
#define ISCSI_LOGIN_RESPONSE_CONT (0x1<<6)
#define ISCSI_LOGIN_RESPONSE_CONT_SHIFT 6
#define ISCSI_LOGIN_RESPONSE_TRANSIT (0x1<<7)
#define ISCSI_LOGIN_RESPONSE_TRANSIT_SHIFT 7
	u8 version_max;
	u8 version_active;
#elif defined(__LITTLE_ENDIAN)
	u8 version_active;
	u8 version_max;
	u8 response_flags;
#define ISCSI_LOGIN_RESPONSE_NEXT_STAGE (0x3<<0)
#define ISCSI_LOGIN_RESPONSE_NEXT_STAGE_SHIFT 0
#define ISCSI_LOGIN_RESPONSE_CURRENT_STAGE (0x3<<2)
#define ISCSI_LOGIN_RESPONSE_CURRENT_STAGE_SHIFT 2
#define ISCSI_LOGIN_RESPONSE_RESERVED0 (0x3<<4)
#define ISCSI_LOGIN_RESPONSE_RESERVED0_SHIFT 4
#define ISCSI_LOGIN_RESPONSE_CONT (0x1<<6)
#define ISCSI_LOGIN_RESPONSE_CONT_SHIFT 6
#define ISCSI_LOGIN_RESPONSE_TRANSIT (0x1<<7)
#define ISCSI_LOGIN_RESPONSE_TRANSIT_SHIFT 7
	u8 op_code;
#endif
	u32 data_length;
	u32 exp_cmd_sn;
	u32 max_cmd_sn;
	u32 reserved1[2];
#if defined(__BIG_ENDIAN)
	u16 reserved3;
	u8 err_code;
	u8 reserved2;
#elif defined(__LITTLE_ENDIAN)
	u8 reserved2;
	u8 err_code;
	u16 reserved3;
#endif
	u32 stat_sn;
	u32 isid_lo;
#if defined(__BIG_ENDIAN)
	u16 isid_hi;
	u16 tsih;
#elif defined(__LITTLE_ENDIAN)
	u16 tsih;
	u16 isid_hi;
#endif
#if defined(__BIG_ENDIAN)
	u8 status_class;
	u8 status_detail;
	u16 reserved4;
#elif defined(__LITTLE_ENDIAN)
	u16 reserved4;
	u8 status_detail;
	u8 status_class;
#endif
	u32 reserved5[3];
#if defined(__BIG_ENDIAN)
	u16 reserved6;
	u16 itt;
#define ISCSI_LOGIN_RESPONSE_INDEX (0x3FFF<<0)
#define ISCSI_LOGIN_RESPONSE_INDEX_SHIFT 0
#define ISCSI_LOGIN_RESPONSE_TYPE (0x3<<14)
#define ISCSI_LOGIN_RESPONSE_TYPE_SHIFT 14
#elif defined(__LITTLE_ENDIAN)
	u16 itt;
#define ISCSI_LOGIN_RESPONSE_INDEX (0x3FFF<<0)
#define ISCSI_LOGIN_RESPONSE_INDEX_SHIFT 0
#define ISCSI_LOGIN_RESPONSE_TYPE (0x3<<14)
#define ISCSI_LOGIN_RESPONSE_TYPE_SHIFT 14
	u16 reserved6;
#endif
	u32 cq_req_sn;
};


/*
 * iSCSI Logout SQ WQE
 */
struct bnx2i_logout_request {
#if defined(__BIG_ENDIAN)
	u8 op_code;
	u8 op_attr;
#define ISCSI_LOGOUT_REQUEST_REASON (0x7F<<0)
#define ISCSI_LOGOUT_REQUEST_REASON_SHIFT 0
#define ISCSI_LOGOUT_REQUEST_ALWAYS_ONE (0x1<<7)
#define ISCSI_LOGOUT_REQUEST_ALWAYS_ONE_SHIFT 7
	u16 reserved0;
#elif defined(__LITTLE_ENDIAN)
	u16 reserved0;
	u8 op_attr;
#define ISCSI_LOGOUT_REQUEST_REASON (0x7F<<0)
#define ISCSI_LOGOUT_REQUEST_REASON_SHIFT 0
#define ISCSI_LOGOUT_REQUEST_ALWAYS_ONE (0x1<<7)
#define ISCSI_LOGOUT_REQUEST_ALWAYS_ONE_SHIFT 7
	u8 op_code;
#endif
	u32 data_length;
	u32 reserved1[2];
#if defined(__BIG_ENDIAN)
	u16 reserved2;
	u16 itt;
#define ISCSI_LOGOUT_REQUEST_INDEX (0x3FFF<<0)
#define ISCSI_LOGOUT_REQUEST_INDEX_SHIFT 0
#define ISCSI_LOGOUT_REQUEST_TYPE (0x3<<14)
#define ISCSI_LOGOUT_REQUEST_TYPE_SHIFT 14
#elif defined(__LITTLE_ENDIAN)
	u16 itt;
#define ISCSI_LOGOUT_REQUEST_INDEX (0x3FFF<<0)
#define ISCSI_LOGOUT_REQUEST_INDEX_SHIFT 0
#define ISCSI_LOGOUT_REQUEST_TYPE (0x3<<14)
#define ISCSI_LOGOUT_REQUEST_TYPE_SHIFT 14
	u16 reserved2;
#endif
#if defined(__BIG_ENDIAN)
	u16 cid;
	u16 reserved3;
#elif defined(__LITTLE_ENDIAN)
	u16 reserved3;
	u16 cid;
#endif
	u32 cmd_sn;
	u32 reserved4[5];
	u32 zero_fill;
	u32 bd_list_addr_lo;
	u32 bd_list_addr_hi;
#if defined(__BIG_ENDIAN)
	u8 cq_index;
	u8 reserved6;
	u8 reserved5;
	u8 num_bds;
#elif defined(__LITTLE_ENDIAN)
	u8 num_bds;
	u8 reserved5;
	u8 reserved6;
	u8 cq_index;
#endif
};


/*
 * iSCSI Logout CQE
 */
struct bnx2i_logout_response {
#if defined(__BIG_ENDIAN)
	u8 op_code;
	u8 reserved1;
	u8 response;
	u8 reserved0;
#elif defined(__LITTLE_ENDIAN)
	u8 reserved0;
	u8 response;
	u8 reserved1;
	u8 op_code;
#endif
	u32 reserved2;
	u32 exp_cmd_sn;
	u32 max_cmd_sn;
	u32 reserved3[2];
#if defined(__BIG_ENDIAN)
	u16 reserved5;
	u8 err_code;
	u8 reserved4;
#elif defined(__LITTLE_ENDIAN)
	u8 reserved4;
	u8 err_code;
	u16 reserved5;
#endif
	u32 reserved6[3];
#if defined(__BIG_ENDIAN)
	u16 time_to_wait;
	u16 time_to_retain;
#elif defined(__LITTLE_ENDIAN)
	u16 time_to_retain;
	u16 time_to_wait;
#endif
	u32 reserved7[3];
#if defined(__BIG_ENDIAN)
	u16 reserved8;
	u16 itt;
#define ISCSI_LOGOUT_RESPONSE_INDEX (0x3FFF<<0)
#define ISCSI_LOGOUT_RESPONSE_INDEX_SHIFT 0
#define ISCSI_LOGOUT_RESPONSE_TYPE (0x3<<14)
#define ISCSI_LOGOUT_RESPONSE_TYPE_SHIFT 14
#elif defined(__LITTLE_ENDIAN)
	u16 itt;
#define ISCSI_LOGOUT_RESPONSE_INDEX (0x3FFF<<0)
#define ISCSI_LOGOUT_RESPONSE_INDEX_SHIFT 0
#define ISCSI_LOGOUT_RESPONSE_TYPE (0x3<<14)
#define ISCSI_LOGOUT_RESPONSE_TYPE_SHIFT 14
	u16 reserved8;
#endif
	u32 cq_req_sn;
};


/*
 * iSCSI Nop-In CQE
 */
struct bnx2i_nop_in_msg {
#if defined(__BIG_ENDIAN)
	u8 op_code;
	u8 reserved1;
	u16 reserved0;
#elif defined(__LITTLE_ENDIAN)
	u16 reserved0;
	u8 reserved1;
	u8 op_code;
#endif
	u32 data_length;
	u32 exp_cmd_sn;
	u32 max_cmd_sn;
	u32 ttt;
	u32 reserved2;
#if defined(__BIG_ENDIAN)
	u16 reserved4;
	u8 err_code;
	u8 reserved3;
#elif defined(__LITTLE_ENDIAN)
	u8 reserved3;
	u8 err_code;
	u16 reserved4;
#endif
	u32 reserved5;
	u32 lun[2];
	u32 reserved6[4];
#if defined(__BIG_ENDIAN)
	u16 reserved7;
	u16 itt;
#define ISCSI_NOP_IN_MSG_INDEX (0x3FFF<<0)
#define ISCSI_NOP_IN_MSG_INDEX_SHIFT 0
#define ISCSI_NOP_IN_MSG_TYPE (0x3<<14)
#define ISCSI_NOP_IN_MSG_TYPE_SHIFT 14
#elif defined(__LITTLE_ENDIAN)
	u16 itt;
#define ISCSI_NOP_IN_MSG_INDEX (0x3FFF<<0)
#define ISCSI_NOP_IN_MSG_INDEX_SHIFT 0
#define ISCSI_NOP_IN_MSG_TYPE (0x3<<14)
#define ISCSI_NOP_IN_MSG_TYPE_SHIFT 14
	u16 reserved7;
#endif
	u32 cq_req_sn;
};


/*
 * iSCSI NOP-OUT SQ WQE
 */
struct bnx2i_nop_out_request {
#if defined(__BIG_ENDIAN)
	u8 op_code;
	u8 op_attr;
#define ISCSI_NOP_OUT_REQUEST_RESERVED1 (0x7F<<0)
#define ISCSI_NOP_OUT_REQUEST_RESERVED1_SHIFT 0
#define ISCSI_NOP_OUT_REQUEST_ALWAYS_ONE (0x1<<7)
#define ISCSI_NOP_OUT_REQUEST_ALWAYS_ONE_SHIFT 7
	u16 reserved0;
#elif defined(__LITTLE_ENDIAN)
	u16 reserved0;
	u8 op_attr;
#define ISCSI_NOP_OUT_REQUEST_RESERVED1 (0x7F<<0)
#define ISCSI_NOP_OUT_REQUEST_RESERVED1_SHIFT 0
#define ISCSI_NOP_OUT_REQUEST_ALWAYS_ONE (0x1<<7)
#define ISCSI_NOP_OUT_REQUEST_ALWAYS_ONE_SHIFT 7
	u8 op_code;
#endif
	u32 data_length;
	u32 lun[2];
#if defined(__BIG_ENDIAN)
	u16 reserved2;
	u16 itt;
#define ISCSI_NOP_OUT_REQUEST_INDEX (0x3FFF<<0)
#define ISCSI_NOP_OUT_REQUEST_INDEX_SHIFT 0
#define ISCSI_NOP_OUT_REQUEST_TYPE (0x3<<14)
#define ISCSI_NOP_OUT_REQUEST_TYPE_SHIFT 14
#elif defined(__LITTLE_ENDIAN)
	u16 itt;
#define ISCSI_NOP_OUT_REQUEST_INDEX (0x3FFF<<0)
#define ISCSI_NOP_OUT_REQUEST_INDEX_SHIFT 0
#define ISCSI_NOP_OUT_REQUEST_TYPE (0x3<<14)
#define ISCSI_NOP_OUT_REQUEST_TYPE_SHIFT 14
	u16 reserved2;
#endif
	u32 ttt;
	u32 cmd_sn;
	u32 reserved3[2];
	u32 resp_bd_list_addr_lo;
	u32 resp_bd_list_addr_hi;
	u32 resp_buffer;
#define ISCSI_NOP_OUT_REQUEST_RESP_BUFFER_LENGTH (0xFFFFFF<<0)
#define ISCSI_NOP_OUT_REQUEST_RESP_BUFFER_LENGTH_SHIFT 0
#define ISCSI_NOP_OUT_REQUEST_NUM_RESP_BDS (0xFF<<24)
#define ISCSI_NOP_OUT_REQUEST_NUM_RESP_BDS_SHIFT 24
#if defined(__BIG_ENDIAN)
	u16 reserved7;
	u8 reserved6;
	u8 flags;
#define ISCSI_NOP_OUT_REQUEST_RESERVED4 (0x1<<0)
#define ISCSI_NOP_OUT_REQUEST_RESERVED4_SHIFT 0
#define ISCSI_NOP_OUT_REQUEST_LOCAL_COMPLETION (0x1<<1)
#define ISCSI_NOP_OUT_REQUEST_LOCAL_COMPLETION_SHIFT 1
#define ISCSI_NOP_OUT_REQUEST_ZERO_FILL (0x3F<<2)
#define ISCSI_NOP_OUT_REQUEST_ZERO_FILL_SHIFT 2
#elif defined(__LITTLE_ENDIAN)
	u8 flags;
#define ISCSI_NOP_OUT_REQUEST_RESERVED4 (0x1<<0)
#define ISCSI_NOP_OUT_REQUEST_RESERVED4_SHIFT 0
#define ISCSI_NOP_OUT_REQUEST_LOCAL_COMPLETION (0x1<<1)
#define ISCSI_NOP_OUT_REQUEST_LOCAL_COMPLETION_SHIFT 1
#define ISCSI_NOP_OUT_REQUEST_ZERO_FILL (0x3F<<2)
#define ISCSI_NOP_OUT_REQUEST_ZERO_FILL_SHIFT 2
	u8 reserved6;
	u16 reserved7;
#endif
	u32 bd_list_addr_lo;
	u32 bd_list_addr_hi;
#if defined(__BIG_ENDIAN)
	u8 cq_index;
	u8 reserved9;
	u8 reserved8;
	u8 num_bds;
#elif defined(__LITTLE_ENDIAN)
	u8 num_bds;
	u8 reserved8;
	u8 reserved9;
	u8 cq_index;
#endif
};

/*
 * iSCSI Reject CQE
 */
struct bnx2i_reject_msg {
#if defined(__BIG_ENDIAN)
	u8 op_code;
	u8 reserved1;
	u8 reason;
	u8 reserved0;
#elif defined(__LITTLE_ENDIAN)
	u8 reserved0;
	u8 reason;
	u8 reserved1;
	u8 op_code;
#endif
	u32 data_length;
	u32 exp_cmd_sn;
	u32 max_cmd_sn;
	u32 reserved2[2];
#if defined(__BIG_ENDIAN)
	u16 reserved4;
	u8 err_code;
	u8 reserved3;
#elif defined(__LITTLE_ENDIAN)
	u8 reserved3;
	u8 err_code;
	u16 reserved4;
#endif
	u32 reserved5[8];
	u32 cq_req_sn;
};

/*
 * bnx2i iSCSI TMF SQ WQE
 */
struct bnx2i_tmf_request {
#if defined(__BIG_ENDIAN)
	u8 op_code;
	u8 op_attr;
#define ISCSI_TMF_REQUEST_FUNCTION (0x7F<<0)
#define ISCSI_TMF_REQUEST_FUNCTION_SHIFT 0
#define ISCSI_TMF_REQUEST_ALWAYS_ONE (0x1<<7)
#define ISCSI_TMF_REQUEST_ALWAYS_ONE_SHIFT 7
	u16 reserved0;
#elif defined(__LITTLE_ENDIAN)
	u16 reserved0;
	u8 op_attr;
#define ISCSI_TMF_REQUEST_FUNCTION (0x7F<<0)
#define ISCSI_TMF_REQUEST_FUNCTION_SHIFT 0
#define ISCSI_TMF_REQUEST_ALWAYS_ONE (0x1<<7)
#define ISCSI_TMF_REQUEST_ALWAYS_ONE_SHIFT 7
	u8 op_code;
#endif
	u32 data_length;
	u32 lun[2];
#if defined(__BIG_ENDIAN)
	u16 reserved1;
	u16 itt;
#define ISCSI_TMF_REQUEST_INDEX (0x3FFF<<0)
#define ISCSI_TMF_REQUEST_INDEX_SHIFT 0
#define ISCSI_TMF_REQUEST_TYPE (0x3<<14)
#define ISCSI_TMF_REQUEST_TYPE_SHIFT 14
#elif defined(__LITTLE_ENDIAN)
	u16 itt;
#define ISCSI_TMF_REQUEST_INDEX (0x3FFF<<0)
#define ISCSI_TMF_REQUEST_INDEX_SHIFT 0
#define ISCSI_TMF_REQUEST_TYPE (0x3<<14)
#define ISCSI_TMF_REQUEST_TYPE_SHIFT 14
	u16 reserved1;
#endif
	u32 ref_itt;
	u32 cmd_sn;
	u32 reserved2;
	u32 ref_cmd_sn;
	u32 reserved3[3];
	u32 zero_fill;
	u32 bd_list_addr_lo;
	u32 bd_list_addr_hi;
#if defined(__BIG_ENDIAN)
	u8 cq_index;
	u8 reserved5;
	u8 reserved4;
	u8 num_bds;
#elif defined(__LITTLE_ENDIAN)
	u8 num_bds;
	u8 reserved4;
	u8 reserved5;
	u8 cq_index;
#endif
};

/*
 * iSCSI Text SQ WQE
 */
struct bnx2i_text_request {
#if defined(__BIG_ENDIAN)
	u8 op_code;
	u8 op_attr;
#define ISCSI_TEXT_REQUEST_RESERVED1 (0x3F<<0)
#define ISCSI_TEXT_REQUEST_RESERVED1_SHIFT 0
#define ISCSI_TEXT_REQUEST_CONT (0x1<<6)
#define ISCSI_TEXT_REQUEST_CONT_SHIFT 6
#define ISCSI_TEXT_REQUEST_FINAL (0x1<<7)
#define ISCSI_TEXT_REQUEST_FINAL_SHIFT 7
	u16 reserved0;
#elif defined(__LITTLE_ENDIAN)
	u16 reserved0;
	u8 op_attr;
#define ISCSI_TEXT_REQUEST_RESERVED1 (0x3F<<0)
#define ISCSI_TEXT_REQUEST_RESERVED1_SHIFT 0
#define ISCSI_TEXT_REQUEST_CONT (0x1<<6)
#define ISCSI_TEXT_REQUEST_CONT_SHIFT 6
#define ISCSI_TEXT_REQUEST_FINAL (0x1<<7)
#define ISCSI_TEXT_REQUEST_FINAL_SHIFT 7
	u8 op_code;
#endif
	u32 data_length;
	u32 lun[2];
#if defined(__BIG_ENDIAN)
	u16 reserved3;
	u16 itt;
#define ISCSI_TEXT_REQUEST_INDEX (0x3FFF<<0)
#define ISCSI_TEXT_REQUEST_INDEX_SHIFT 0
#define ISCSI_TEXT_REQUEST_TYPE (0x3<<14)
#define ISCSI_TEXT_REQUEST_TYPE_SHIFT 14
#elif defined(__LITTLE_ENDIAN)
	u16 itt;
#define ISCSI_TEXT_REQUEST_INDEX (0x3FFF<<0)
#define ISCSI_TEXT_REQUEST_INDEX_SHIFT 0
#define ISCSI_TEXT_REQUEST_TYPE (0x3<<14)
#define ISCSI_TEXT_REQUEST_TYPE_SHIFT 14
	u16 reserved3;
#endif
	u32 ttt;
	u32 cmd_sn;
	u32 reserved4[2];
	u32 resp_bd_list_addr_lo;
	u32 resp_bd_list_addr_hi;
	u32 resp_buffer;
#define ISCSI_TEXT_REQUEST_RESP_BUFFER_LENGTH (0xFFFFFF<<0)
#define ISCSI_TEXT_REQUEST_RESP_BUFFER_LENGTH_SHIFT 0
#define ISCSI_TEXT_REQUEST_NUM_RESP_BDS (0xFF<<24)
#define ISCSI_TEXT_REQUEST_NUM_RESP_BDS_SHIFT 24
	u32 zero_fill;
	u32 bd_list_addr_lo;
	u32 bd_list_addr_hi;
#if defined(__BIG_ENDIAN)
	u8 cq_index;
	u8 reserved7;
	u8 reserved6;
	u8 num_bds;
#elif defined(__LITTLE_ENDIAN)
	u8 num_bds;
	u8 reserved6;
	u8 reserved7;
	u8 cq_index;
#endif
};

/*
 * iSCSI SQ WQE
 */
union iscsi_request {
	struct bnx2i_cmd_request cmd;
	struct bnx2i_tmf_request tmf;
	struct bnx2i_nop_out_request nop_out;
	struct bnx2i_login_request login_req;
	struct bnx2i_text_request text;
	struct bnx2i_logout_request logout_req;
	struct bnx2i_cleanup_request cleanup;
};


/*
 * iSCSI TMF CQE
 */
struct bnx2i_tmf_response {
#if defined(__BIG_ENDIAN)
	u8 op_code;
	u8 reserved1;
	u8 response;
	u8 reserved0;
#elif defined(__LITTLE_ENDIAN)
	u8 reserved0;
	u8 response;
	u8 reserved1;
	u8 op_code;
#endif
	u32 reserved2;
	u32 exp_cmd_sn;
	u32 max_cmd_sn;
	u32 reserved3[2];
#if defined(__BIG_ENDIAN)
	u16 reserved5;
	u8 err_code;
	u8 reserved4;
#elif defined(__LITTLE_ENDIAN)
	u8 reserved4;
	u8 err_code;
	u16 reserved5;
#endif
	u32 reserved6[7];
#if defined(__BIG_ENDIAN)
	u16 reserved7;
	u16 itt;
#define ISCSI_TMF_RESPONSE_INDEX (0x3FFF<<0)
#define ISCSI_TMF_RESPONSE_INDEX_SHIFT 0
#define ISCSI_TMF_RESPONSE_TYPE (0x3<<14)
#define ISCSI_TMF_RESPONSE_TYPE_SHIFT 14
#elif defined(__LITTLE_ENDIAN)
	u16 itt;
#define ISCSI_TMF_RESPONSE_INDEX (0x3FFF<<0)
#define ISCSI_TMF_RESPONSE_INDEX_SHIFT 0
#define ISCSI_TMF_RESPONSE_TYPE (0x3<<14)
#define ISCSI_TMF_RESPONSE_TYPE_SHIFT 14
	u16 reserved7;
#endif
	u32 cq_req_sn;
};

/*
 * iSCSI Text CQE
 */
struct bnx2i_text_response {
#if defined(__BIG_ENDIAN)
	u8 op_code;
	u8 response_flags;
#define ISCSI_TEXT_RESPONSE_RESERVED1 (0x3F<<0)
#define ISCSI_TEXT_RESPONSE_RESERVED1_SHIFT 0
#define ISCSI_TEXT_RESPONSE_CONT (0x1<<6)
#define ISCSI_TEXT_RESPONSE_CONT_SHIFT 6
#define ISCSI_TEXT_RESPONSE_FINAL (0x1<<7)
#define ISCSI_TEXT_RESPONSE_FINAL_SHIFT 7
	u16 reserved0;
#elif defined(__LITTLE_ENDIAN)
	u16 reserved0;
	u8 response_flags;
#define ISCSI_TEXT_RESPONSE_RESERVED1 (0x3F<<0)
#define ISCSI_TEXT_RESPONSE_RESERVED1_SHIFT 0
#define ISCSI_TEXT_RESPONSE_CONT (0x1<<6)
#define ISCSI_TEXT_RESPONSE_CONT_SHIFT 6
#define ISCSI_TEXT_RESPONSE_FINAL (0x1<<7)
#define ISCSI_TEXT_RESPONSE_FINAL_SHIFT 7
	u8 op_code;
#endif
	u32 data_length;
	u32 exp_cmd_sn;
	u32 max_cmd_sn;
	u32 ttt;
	u32 reserved2;
#if defined(__BIG_ENDIAN)
	u16 reserved4;
	u8 err_code;
	u8 reserved3;
#elif defined(__LITTLE_ENDIAN)
	u8 reserved3;
	u8 err_code;
	u16 reserved4;
#endif
	u32 reserved5;
	u32 lun[2];
	u32 reserved6[4];
#if defined(__BIG_ENDIAN)
	u16 reserved7;
	u16 itt;
#define ISCSI_TEXT_RESPONSE_INDEX (0x3FFF<<0)
#define ISCSI_TEXT_RESPONSE_INDEX_SHIFT 0
#define ISCSI_TEXT_RESPONSE_TYPE (0x3<<14)
#define ISCSI_TEXT_RESPONSE_TYPE_SHIFT 14
#elif defined(__LITTLE_ENDIAN)
	u16 itt;
#define ISCSI_TEXT_RESPONSE_INDEX (0x3FFF<<0)
#define ISCSI_TEXT_RESPONSE_INDEX_SHIFT 0
#define ISCSI_TEXT_RESPONSE_TYPE (0x3<<14)
#define ISCSI_TEXT_RESPONSE_TYPE_SHIFT 14
	u16 reserved7;
#endif
	u32 cq_req_sn;
};

/*
 * iSCSI CQE
 */
union iscsi_response {
	struct bnx2i_cmd_response cmd;
	struct bnx2i_tmf_response tmf;
	struct bnx2i_login_response login_resp;
	struct bnx2i_text_response text;
	struct bnx2i_logout_response logout_resp;
	struct bnx2i_cleanup_response cleanup;
	struct bnx2i_reject_msg reject;
	struct bnx2i_async_msg async;
	struct bnx2i_nop_in_msg nop_in;
};

#endif /* __57XX_ISCSI_HSI_LINUX_LE__ */
