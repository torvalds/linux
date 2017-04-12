/*
 *  QLogic FCoE Offload Driver
 *  Copyright (c) 2016 Cavium Inc.
 *
 *  This software is available under the terms of the GNU General Public License
 *  (GPL) Version 2, available from the file COPYING in the main directory of
 *  this source tree.
 */
#ifndef __QEDF_HSI__
#define __QEDF_HSI__
/*
 * Add include to common target
 */
#include <linux/qed/common_hsi.h>

/*
 * Add include to common storage target
 */
#include <linux/qed/storage_common.h>

/*
 * Add include to common fcoe target for both eCore and protocol driver
 */
#include <linux/qed/fcoe_common.h>


/*
 * FCoE CQ element ABTS information
 */
struct fcoe_abts_info {
	u8 r_ctl /* R_CTL in the ABTS response frame */;
	u8 reserved0;
	__le16 rx_id;
	__le32 reserved2[2];
	__le32 fc_payload[3] /* ABTS FC payload response frame */;
};


/*
 * FCoE class type
 */
enum fcoe_class_type {
	FCOE_TASK_CLASS_TYPE_3,
	FCOE_TASK_CLASS_TYPE_2,
	MAX_FCOE_CLASS_TYPE
};


/*
 * FCoE CMDQ element control information
 */
struct fcoe_cmdqe_control {
	__le16 conn_id;
	u8 num_additional_cmdqes;
	u8 cmdType;
	/* true for ABTS request cmdqe. used in Target mode */
#define FCOE_CMDQE_CONTROL_ABTSREQCMD_MASK  0x1
#define FCOE_CMDQE_CONTROL_ABTSREQCMD_SHIFT 0
#define FCOE_CMDQE_CONTROL_RESERVED1_MASK   0x7F
#define FCOE_CMDQE_CONTROL_RESERVED1_SHIFT  1
	u8 reserved2[4];
};

/*
 * FCoE control + payload CMDQ element
 */
struct fcoe_cmdqe {
	struct fcoe_cmdqe_control hdr;
	u8 fc_header[24];
	__le32 fcp_cmd_payload[8];
};



/*
 * FCP RSP flags
 */
struct fcoe_fcp_rsp_flags {
	u8 flags;
#define FCOE_FCP_RSP_FLAGS_FCP_RSP_LEN_VALID_MASK  0x1
#define FCOE_FCP_RSP_FLAGS_FCP_RSP_LEN_VALID_SHIFT 0
#define FCOE_FCP_RSP_FLAGS_FCP_SNS_LEN_VALID_MASK  0x1
#define FCOE_FCP_RSP_FLAGS_FCP_SNS_LEN_VALID_SHIFT 1
#define FCOE_FCP_RSP_FLAGS_FCP_RESID_OVER_MASK     0x1
#define FCOE_FCP_RSP_FLAGS_FCP_RESID_OVER_SHIFT    2
#define FCOE_FCP_RSP_FLAGS_FCP_RESID_UNDER_MASK    0x1
#define FCOE_FCP_RSP_FLAGS_FCP_RESID_UNDER_SHIFT   3
#define FCOE_FCP_RSP_FLAGS_FCP_CONF_REQ_MASK       0x1
#define FCOE_FCP_RSP_FLAGS_FCP_CONF_REQ_SHIFT      4
#define FCOE_FCP_RSP_FLAGS_FCP_BIDI_FLAGS_MASK     0x7
#define FCOE_FCP_RSP_FLAGS_FCP_BIDI_FLAGS_SHIFT    5
};

/*
 * FCoE CQ element response information
 */
struct fcoe_cqe_rsp_info {
	struct fcoe_fcp_rsp_flags rsp_flags;
	u8 scsi_status_code;
	__le16 retry_delay_timer;
	__le32 fcp_resid;
	__le32 fcp_sns_len;
	__le32 fcp_rsp_len;
	__le16 rx_id;
	u8 fw_error_flags;
#define FCOE_CQE_RSP_INFO_FW_UNDERRUN_MASK  0x1 /* FW detected underrun */
#define FCOE_CQE_RSP_INFO_FW_UNDERRUN_SHIFT 0
#define FCOE_CQE_RSP_INFO_RESREVED_MASK     0x7F
#define FCOE_CQE_RSP_INFO_RESREVED_SHIFT    1
	u8 reserved;
	__le32 fw_residual /* Residual bytes calculated by FW */;
};

/*
 * FCoE CQ element Target completion information
 */
struct fcoe_cqe_target_info {
	__le16 rx_id;
	__le16 reserved0;
	__le32 reserved1[5];
};

/*
 * FCoE error/warning reporting entry
 */
struct fcoe_err_report_entry {
	__le32 err_warn_bitmap_lo /* Error bitmap lower 32 bits */;
	__le32 err_warn_bitmap_hi /* Error bitmap higher 32 bits */;
	/* Buffer offset the beginning of the Sequence last transmitted */
	__le32 tx_buf_off;
	/* Buffer offset from the beginning of the Sequence last received */
	__le32 rx_buf_off;
	__le16 rx_id /* RX_ID of the associated task */;
	__le16 reserved1;
	__le32 reserved2;
};

/*
 * FCoE CQ element middle path information
 */
struct fcoe_cqe_midpath_info {
	__le32 data_placement_size;
	__le16 rx_id;
	__le16 reserved0;
	__le32 reserved1[4];
};

/*
 * FCoE CQ element unsolicited information
 */
struct fcoe_unsolic_info {
	/* BD information: Physical address and opaque data */
	struct scsi_bd bd_info;
	__le16 conn_id /* Connection ID the frame is associated to */;
	__le16 pkt_len /* Packet length */;
	u8 reserved1[4];
};

/*
 * FCoE warning reporting entry
 */
struct fcoe_warning_report_entry {
	/* BD information: Physical address and opaque data */
	struct scsi_bd bd_info;
	/* Buffer offset the beginning of the Sequence last transmitted */
	__le32 buf_off;
	__le16 rx_id /* RX_ID of the associated task */;
	__le16 reserved1;
};

/*
 * FCoE CQ element information
 */
union fcoe_cqe_info {
	struct fcoe_cqe_rsp_info rsp_info /* Response completion information */;
	/* Target completion information */
	struct fcoe_cqe_target_info target_info;
	/* Error completion information */
	struct fcoe_err_report_entry err_info;
	struct fcoe_abts_info abts_info /* ABTS completion information */;
	/* Middle path completion information */
	struct fcoe_cqe_midpath_info midpath_info;
	/* Unsolicited packet completion information */
	struct fcoe_unsolic_info unsolic_info;
	/* Warning completion information (Rec Tov expiration) */
	struct fcoe_warning_report_entry warn_info;
};

/*
 * FCoE CQ element
 */
struct fcoe_cqe {
	__le32 cqe_data;
	/* The task identifier (OX_ID) to be completed */
#define FCOE_CQE_TASK_ID_MASK    0xFFFF
#define FCOE_CQE_TASK_ID_SHIFT   0
	/*
	 * The CQE type: 0x0 Indicating on a pending work request completion.
	 * 0x1 - Indicating on an unsolicited event notification. use enum
	 * fcoe_cqe_type  (use enum fcoe_cqe_type)
	 */
#define FCOE_CQE_CQE_TYPE_MASK   0xF
#define FCOE_CQE_CQE_TYPE_SHIFT  16
#define FCOE_CQE_RESERVED0_MASK  0xFFF
#define FCOE_CQE_RESERVED0_SHIFT 20
	__le16 reserved1;
	__le16 fw_cq_prod;
	union fcoe_cqe_info cqe_info;
};

/*
 * FCoE CQE type
 */
enum fcoe_cqe_type {
	/* solicited response on a R/W or middle-path SQE */
	FCOE_GOOD_COMPLETION_CQE_TYPE,
	FCOE_UNSOLIC_CQE_TYPE /* unsolicited packet, RQ consumed */,
	FCOE_ERROR_DETECTION_CQE_TYPE /* timer expiration, validation error */,
	FCOE_WARNING_CQE_TYPE /* rec_tov or rr_tov timer expiration */,
	FCOE_EXCH_CLEANUP_CQE_TYPE /* task cleanup completed */,
	FCOE_ABTS_CQE_TYPE /* ABTS received and task cleaned */,
	FCOE_DUMMY_CQE_TYPE /* just increment SQ CONS */,
	/* Task was completed wight after sending a pkt to the target */
	FCOE_LOCAL_COMP_CQE_TYPE,
	MAX_FCOE_CQE_TYPE
};


/*
 * FCoE device type
 */
enum fcoe_device_type {
	FCOE_TASK_DEV_TYPE_DISK,
	FCOE_TASK_DEV_TYPE_TAPE,
	MAX_FCOE_DEVICE_TYPE
};




/*
 * FCoE fast path error codes
 */
enum fcoe_fp_error_warning_code {
	FCOE_ERROR_CODE_XFER_OOO_RO /* XFER error codes */,
	FCOE_ERROR_CODE_XFER_RO_NOT_ALIGNED,
	FCOE_ERROR_CODE_XFER_NULL_BURST_LEN,
	FCOE_ERROR_CODE_XFER_RO_GREATER_THAN_DATA2TRNS,
	FCOE_ERROR_CODE_XFER_INVALID_PAYLOAD_SIZE,
	FCOE_ERROR_CODE_XFER_TASK_TYPE_NOT_WRITE,
	FCOE_ERROR_CODE_XFER_PEND_XFER_SET,
	FCOE_ERROR_CODE_XFER_OPENED_SEQ,
	FCOE_ERROR_CODE_XFER_FCTL,
	FCOE_ERROR_CODE_FCP_RSP_BIDI_FLAGS_SET /* FCP RSP error codes */,
	FCOE_ERROR_CODE_FCP_RSP_INVALID_LENGTH_FIELD,
	FCOE_ERROR_CODE_FCP_RSP_INVALID_SNS_FIELD,
	FCOE_ERROR_CODE_FCP_RSP_INVALID_PAYLOAD_SIZE,
	FCOE_ERROR_CODE_FCP_RSP_PEND_XFER_SET,
	FCOE_ERROR_CODE_FCP_RSP_OPENED_SEQ,
	FCOE_ERROR_CODE_FCP_RSP_FCTL,
	FCOE_ERROR_CODE_FCP_RSP_LAST_SEQ_RESET,
	FCOE_ERROR_CODE_FCP_RSP_CONF_REQ_NOT_SUPPORTED_YET,
	FCOE_ERROR_CODE_DATA_OOO_RO /* FCP DATA error codes */,
	FCOE_ERROR_CODE_DATA_EXCEEDS_DEFINED_MAX_FRAME_SIZE,
	FCOE_ERROR_CODE_DATA_EXCEEDS_DATA2TRNS,
	FCOE_ERROR_CODE_DATA_SOFI3_SEQ_ACTIVE_SET,
	FCOE_ERROR_CODE_DATA_SOFN_SEQ_ACTIVE_RESET,
	FCOE_ERROR_CODE_DATA_EOFN_END_SEQ_SET,
	FCOE_ERROR_CODE_DATA_EOFT_END_SEQ_RESET,
	FCOE_ERROR_CODE_DATA_TASK_TYPE_NOT_READ,
	FCOE_ERROR_CODE_DATA_FCTL_INITIATIR,
	FCOE_ERROR_CODE_MIDPATH_INVALID_TYPE /* Middle path error codes */,
	FCOE_ERROR_CODE_MIDPATH_SOFI3_SEQ_ACTIVE_SET,
	FCOE_ERROR_CODE_MIDPATH_SOFN_SEQ_ACTIVE_RESET,
	FCOE_ERROR_CODE_MIDPATH_EOFN_END_SEQ_SET,
	FCOE_ERROR_CODE_MIDPATH_EOFT_END_SEQ_RESET,
	FCOE_ERROR_CODE_MIDPATH_REPLY_FCTL,
	FCOE_ERROR_CODE_MIDPATH_INVALID_REPLY,
	FCOE_ERROR_CODE_MIDPATH_ELS_REPLY_RCTL,
	FCOE_ERROR_CODE_COMMON_MIDDLE_FRAME_WITH_PAD /* Common error codes */,
	FCOE_ERROR_CODE_COMMON_SEQ_INIT_IN_TCE,
	FCOE_ERROR_CODE_COMMON_FC_HDR_RX_ID_MISMATCH,
	FCOE_ERROR_CODE_COMMON_INCORRECT_SEQ_CNT,
	FCOE_ERROR_CODE_COMMON_DATA_FC_HDR_FCP_TYPE_MISMATCH,
	FCOE_ERROR_CODE_COMMON_DATA_NO_MORE_SGES,
	FCOE_ERROR_CODE_COMMON_OPTIONAL_FC_HDR,
	FCOE_ERROR_CODE_COMMON_READ_TCE_OX_ID_TOO_BIG,
	FCOE_ERROR_CODE_COMMON_DATA_WAS_NOT_TRANSMITTED,
	FCOE_ERROR_CODE_COMMON_TASK_DDF_RCTL_INFO_FIELD,
	FCOE_ERROR_CODE_COMMON_TASK_INVALID_RCTL,
	FCOE_ERROR_CODE_COMMON_TASK_RCTL_GENERAL_MISMATCH,
	FCOE_ERROR_CODE_E_D_TOV_TIMER_EXPIRATION /* Timer error codes */,
	FCOE_WARNING_CODE_REC_TOV_TIMER_EXPIRATION /* Timer error codes */,
	FCOE_ERROR_CODE_RR_TOV_TIMER_EXPIRATION /* Timer error codes */,
	/* ABTSrsp pckt arrived unexpected */
	FCOE_ERROR_CODE_ABTS_REPLY_UNEXPECTED,
	FCOE_ERROR_CODE_TARGET_MODE_FCP_RSP,
	FCOE_ERROR_CODE_TARGET_MODE_FCP_XFER,
	FCOE_ERROR_CODE_TARGET_MODE_DATA_TASK_TYPE_NOT_WRITE,
	FCOE_ERROR_CODE_DATA_FCTL_TARGET,
	FCOE_ERROR_CODE_TARGET_DATA_SIZE_NO_MATCH_XFER,
	FCOE_ERROR_CODE_TARGET_DIF_CRC_CHECKSUM_ERROR,
	FCOE_ERROR_CODE_TARGET_DIF_REF_TAG_ERROR,
	FCOE_ERROR_CODE_TARGET_DIF_APP_TAG_ERROR,
	MAX_FCOE_FP_ERROR_WARNING_CODE
};


/*
 * FCoE RESPQ element
 */
struct fcoe_respqe {
	__le16 ox_id /* OX_ID that is located in the FCP_RSP FC header */;
	__le16 rx_id /* RX_ID that is located in the FCP_RSP FC header */;
	__le32 additional_info;
/* PARAM that is located in the FCP_RSP FC header */
#define FCOE_RESPQE_PARAM_MASK            0xFFFFFF
#define FCOE_RESPQE_PARAM_SHIFT           0
/* Indication whther its Target-auto-rsp mode or not */
#define FCOE_RESPQE_TARGET_AUTO_RSP_MASK  0xFF
#define FCOE_RESPQE_TARGET_AUTO_RSP_SHIFT 24
};


/*
 * FCoE slow path error codes
 */
enum fcoe_sp_error_code {
	/* Error codes for Error Reporting in slow path flows */
	FCOE_ERROR_CODE_SLOW_PATH_TOO_MANY_FUNCS,
	FCOE_ERROR_SLOW_PATH_CODE_NO_LICENSE,
	MAX_FCOE_SP_ERROR_CODE
};


/*
 * FCoE SQE request type
 */
enum fcoe_sqe_request_type {
	SEND_FCOE_CMD,
	SEND_FCOE_MIDPATH,
	SEND_FCOE_ABTS_REQUEST,
	FCOE_EXCHANGE_CLEANUP,
	FCOE_SEQUENCE_RECOVERY,
	SEND_FCOE_XFER_RDY,
	SEND_FCOE_RSP,
	SEND_FCOE_RSP_WITH_SENSE_DATA,
	SEND_FCOE_TARGET_DATA,
	SEND_FCOE_INITIATOR_DATA,
	/*
	 * Xfer Continuation (==1) ready to be sent. Previous XFERs data
	 * received successfully.
	 */
	SEND_FCOE_XFER_CONTINUATION_RDY,
	SEND_FCOE_TARGET_ABTS_RSP,
	MAX_FCOE_SQE_REQUEST_TYPE
};


/*
 * FCoE task TX state
 */
enum fcoe_task_tx_state {
	/* Initiate state after driver has initialized the task */
	FCOE_TASK_TX_STATE_NORMAL,
	/* Updated by TX path after complete transmitting unsolicited packet */
	FCOE_TASK_TX_STATE_UNSOLICITED_COMPLETED,
	/*
	 * Updated by TX path after start processing the task requesting the
	 * cleanup/abort operation
	 */
	FCOE_TASK_TX_STATE_CLEAN_REQ,
	FCOE_TASK_TX_STATE_ABTS /* Updated by TX path during abort procedure */,
	/* Updated by TX path during exchange cleanup procedure */
	FCOE_TASK_TX_STATE_EXCLEANUP,
	/*
	 * Updated by TX path during exchange cleanup continuation task
	 * procedure
	 */
	FCOE_TASK_TX_STATE_EXCLEANUP_TARGET_WRITE_CONT,
	/* Updated by TX path during exchange cleanup first xfer procedure */
	FCOE_TASK_TX_STATE_EXCLEANUP_TARGET_WRITE,
	/* Updated by TX path during exchange cleanup read task in Target */
	FCOE_TASK_TX_STATE_EXCLEANUP_TARGET_READ_OR_RSP,
	/* Updated by TX path during target exchange cleanup procedure */
	FCOE_TASK_TX_STATE_EXCLEANUP_TARGET_WRITE_LAST_CYCLE,
	/* Updated by TX path during sequence recovery procedure */
	FCOE_TASK_TX_STATE_SEQRECOVERY,
	MAX_FCOE_TASK_TX_STATE
};


/*
 * FCoE task type
 */
enum fcoe_task_type {
	FCOE_TASK_TYPE_WRITE_INITIATOR,
	FCOE_TASK_TYPE_READ_INITIATOR,
	FCOE_TASK_TYPE_MIDPATH,
	FCOE_TASK_TYPE_UNSOLICITED,
	FCOE_TASK_TYPE_ABTS,
	FCOE_TASK_TYPE_EXCHANGE_CLEANUP,
	FCOE_TASK_TYPE_SEQUENCE_CLEANUP,
	FCOE_TASK_TYPE_WRITE_TARGET,
	FCOE_TASK_TYPE_READ_TARGET,
	FCOE_TASK_TYPE_RSP,
	FCOE_TASK_TYPE_RSP_SENSE_DATA,
	FCOE_TASK_TYPE_ABTS_TARGET,
	FCOE_TASK_TYPE_ENUM_SIZE,
	MAX_FCOE_TASK_TYPE
};

struct scsi_glbl_queue_entry {
	/* Start physical address for the RQ (receive queue) PBL. */
	struct regpair rq_pbl_addr;
	/* Start physical address for the CQ (completion queue) PBL. */
	struct regpair cq_pbl_addr;
	/* Start physical address for the CMDQ (command queue) PBL. */
	struct regpair cmdq_pbl_addr;
};

#endif /* __QEDF_HSI__ */
