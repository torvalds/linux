#ifndef __BNX2FC_CONSTANTS_H_
#define __BNX2FC_CONSTANTS_H_

/**
 * This file defines HSI constants for the FCoE flows
 */

/* KWQ/KCQ FCoE layer code */
#define FCOE_KWQE_LAYER_CODE   (7)

/* KWQ (kernel work queue) request op codes */
#define FCOE_KWQE_OPCODE_INIT1			(0)
#define FCOE_KWQE_OPCODE_INIT2			(1)
#define FCOE_KWQE_OPCODE_INIT3			(2)
#define FCOE_KWQE_OPCODE_OFFLOAD_CONN1	(3)
#define FCOE_KWQE_OPCODE_OFFLOAD_CONN2	(4)
#define FCOE_KWQE_OPCODE_OFFLOAD_CONN3	(5)
#define FCOE_KWQE_OPCODE_OFFLOAD_CONN4	(6)
#define FCOE_KWQE_OPCODE_ENABLE_CONN	(7)
#define FCOE_KWQE_OPCODE_DISABLE_CONN	(8)
#define FCOE_KWQE_OPCODE_DESTROY_CONN	(9)
#define FCOE_KWQE_OPCODE_DESTROY		(10)
#define FCOE_KWQE_OPCODE_STAT			(11)

/* KCQ (kernel completion queue) response op codes */
#define FCOE_KCQE_OPCODE_INIT_FUNC				(0x10)
#define FCOE_KCQE_OPCODE_DESTROY_FUNC			(0x11)
#define FCOE_KCQE_OPCODE_STAT_FUNC				(0x12)
#define FCOE_KCQE_OPCODE_OFFLOAD_CONN			(0x15)
#define FCOE_KCQE_OPCODE_ENABLE_CONN			(0x16)
#define FCOE_KCQE_OPCODE_DISABLE_CONN			(0x17)
#define FCOE_KCQE_OPCODE_DESTROY_CONN			(0x18)
#define FCOE_KCQE_OPCODE_CQ_EVENT_NOTIFICATION  (0x20)
#define FCOE_KCQE_OPCODE_FCOE_ERROR				(0x21)

/* KCQ (kernel completion queue) completion status */
#define FCOE_KCQE_COMPLETION_STATUS_SUCCESS				(0x0)
#define FCOE_KCQE_COMPLETION_STATUS_ERROR				(0x1)
#define FCOE_KCQE_COMPLETION_STATUS_INVALID_OPCODE		(0x2)
#define FCOE_KCQE_COMPLETION_STATUS_CTX_ALLOC_FAILURE	(0x3)
#define FCOE_KCQE_COMPLETION_STATUS_CTX_FREE_FAILURE	(0x4)
#define FCOE_KCQE_COMPLETION_STATUS_NIC_ERROR			(0x5)

/* Unsolicited CQE type */
#define FCOE_UNSOLICITED_FRAME_CQE_TYPE			0
#define FCOE_ERROR_DETECTION_CQE_TYPE			1
#define FCOE_WARNING_DETECTION_CQE_TYPE			2

/* Task context constants */
/* After driver has initialize the task in case timer services required */
#define	FCOE_TASK_TX_STATE_INIT					0
/* In case timer services are required then shall be updated by Xstorm after
 * start processing the task. In case no timer facilities are required then the
 * driver would initialize the state to this value */
#define	FCOE_TASK_TX_STATE_NORMAL				1
/* Task is under abort procedure. Updated in order to stop processing of
 * pending WQEs on this task */
#define	FCOE_TASK_TX_STATE_ABORT				2
/* For E_D_T_TOV timer expiration in Xstorm (Class 2 only) */
#define	FCOE_TASK_TX_STATE_ERROR				3
/* For REC_TOV timer expiration indication received from Xstorm */
#define	FCOE_TASK_TX_STATE_WARNING				4
/* For completed unsolicited task */
#define	FCOE_TASK_TX_STATE_UNSOLICITED_COMPLETED		5
/* For exchange cleanup request task */
#define	FCOE_TASK_TX_STATE_EXCHANGE_CLEANUP			6
/* For sequence cleanup request task */
#define	FCOE_TASK_TX_STATE_SEQUENCE_CLEANUP			7
/* Mark task as aborted and indicate that ABTS was not transmitted */
#define	FCOE_TASK_TX_STATE_BEFORE_ABTS_TX			8
/* Mark task as aborted and indicate that ABTS was transmitted */
#define	FCOE_TASK_TX_STATE_AFTER_ABTS_TX			9
/* For completion the ABTS task. */
#define	FCOE_TASK_TX_STATE_ABTS_TX_COMPLETED			10
/* Mark task as aborted and indicate that Exchange cleanup was not transmitted
 */
#define	FCOE_TASK_TX_STATE_BEFORE_EXCHANGE_CLEANUP_TX		11
/* Mark task as aborted and indicate that Exchange cleanup was transmitted */
#define	FCOE_TASK_TX_STATE_AFTER_EXCHANGE_CLEANUP_TX		12

#define	FCOE_TASK_RX_STATE_NORMAL				0
#define	FCOE_TASK_RX_STATE_COMPLETED				1
/* Obsolete: Intermediate completion (middle path with local completion) */
#define	FCOE_TASK_RX_STATE_INTER_COMP				2
/* For REC_TOV timer expiration indication received from Xstorm */
#define	FCOE_TASK_RX_STATE_WARNING				3
/* For E_D_T_TOV timer expiration in Ustorm */
#define	FCOE_TASK_RX_STATE_ERROR				4
/* ABTS ACC arrived wait for local completion to finally complete the task. */
#define	FCOE_TASK_RX_STATE_ABTS_ACC_ARRIVED			5
/* local completion arrived wait for ABTS ACC to finally complete the task. */
#define	FCOE_TASK_RX_STATE_ABTS_LOCAL_COMP_ARRIVED		6
/* Special completion indication in case of task was aborted. */
#define FCOE_TASK_RX_STATE_ABTS_COMPLETED			7
/* Special completion indication in case of task was cleaned. */
#define FCOE_TASK_RX_STATE_EXCHANGE_CLEANUP_COMPLETED		8
/* Special completion indication (in task requested the exchange cleanup) in
 * case cleaned task is in non-valid. */
#define FCOE_TASK_RX_STATE_ABORT_CLEANUP_COMPLETED		9
/* Special completion indication (in task requested the sequence cleanup) in
 * case cleaned task was already returned to normal. */
#define FCOE_TASK_RX_STATE_IGNORED_SEQUENCE_CLEANUP		10
/* Exchange cleanup arrived wait until xfer will be handled to finally
 * complete the task. */
#define	FCOE_TASK_RX_STATE_EXCHANGE_CLEANUP_ARRIVED		11
/* Xfer handled, wait for exchange cleanup to finally complete the task. */
#define	FCOE_TASK_RX_STATE_EXCHANGE_CLEANUP_HANDLED_XFER	12

#define	FCOE_TASK_TYPE_WRITE			0
#define	FCOE_TASK_TYPE_READ				1
#define	FCOE_TASK_TYPE_MIDPATH			2
#define	FCOE_TASK_TYPE_UNSOLICITED		3
#define	FCOE_TASK_TYPE_ABTS				4
#define	FCOE_TASK_TYPE_EXCHANGE_CLEANUP	5
#define	FCOE_TASK_TYPE_SEQUENCE_CLEANUP	6

#define FCOE_TASK_DEV_TYPE_DISK			0
#define FCOE_TASK_DEV_TYPE_TAPE			1

#define FCOE_TASK_CLASS_TYPE_3			0
#define FCOE_TASK_CLASS_TYPE_2			1

/* Everest FCoE connection type */
#define B577XX_FCOE_CONNECTION_TYPE		4

/* Error codes for Error Reporting in fast path flows */
/* XFER error codes */
#define FCOE_ERROR_CODE_XFER_OOO_RO					0
#define FCOE_ERROR_CODE_XFER_RO_NOT_ALIGNED				1
#define FCOE_ERROR_CODE_XFER_NULL_BURST_LEN				2
#define FCOE_ERROR_CODE_XFER_RO_GREATER_THAN_DATA2TRNS			3
#define FCOE_ERROR_CODE_XFER_INVALID_PAYLOAD_SIZE			4
#define FCOE_ERROR_CODE_XFER_TASK_TYPE_NOT_WRITE			5
#define FCOE_ERROR_CODE_XFER_PEND_XFER_SET				6
#define FCOE_ERROR_CODE_XFER_OPENED_SEQ					7
#define FCOE_ERROR_CODE_XFER_FCTL					8

/* FCP RSP error codes */
#define FCOE_ERROR_CODE_FCP_RSP_BIDI_FLAGS_SET				9
#define FCOE_ERROR_CODE_FCP_RSP_UNDERFLOW				10
#define FCOE_ERROR_CODE_FCP_RSP_OVERFLOW				11
#define FCOE_ERROR_CODE_FCP_RSP_INVALID_LENGTH_FIELD			12
#define FCOE_ERROR_CODE_FCP_RSP_INVALID_SNS_FIELD			13
#define FCOE_ERROR_CODE_FCP_RSP_INVALID_PAYLOAD_SIZE			14
#define FCOE_ERROR_CODE_FCP_RSP_PEND_XFER_SET				15
#define FCOE_ERROR_CODE_FCP_RSP_OPENED_SEQ				16
#define FCOE_ERROR_CODE_FCP_RSP_FCTL					17
#define FCOE_ERROR_CODE_FCP_RSP_LAST_SEQ_RESET				18
#define FCOE_ERROR_CODE_FCP_RSP_CONF_REQ_NOT_SUPPORTED_YET		19

/* FCP DATA error codes */
#define FCOE_ERROR_CODE_DATA_OOO_RO					20
#define FCOE_ERROR_CODE_DATA_EXCEEDS_DEFINED_MAX_FRAME_SIZE		21
#define FCOE_ERROR_CODE_DATA_EXCEEDS_DATA2TRNS				22
#define FCOE_ERROR_CODE_DATA_SOFI3_SEQ_ACTIVE_SET			23
#define FCOE_ERROR_CODE_DATA_SOFN_SEQ_ACTIVE_RESET			24
#define FCOE_ERROR_CODE_DATA_EOFN_END_SEQ_SET				25
#define FCOE_ERROR_CODE_DATA_EOFT_END_SEQ_RESET			26
#define FCOE_ERROR_CODE_DATA_TASK_TYPE_NOT_READ			27
#define FCOE_ERROR_CODE_DATA_FCTL					28

/* Middle path error codes */
#define FCOE_ERROR_CODE_MIDPATH_TYPE_NOT_ELS				29
#define FCOE_ERROR_CODE_MIDPATH_SOFI3_SEQ_ACTIVE_SET			30
#define FCOE_ERROR_CODE_MIDPATH_SOFN_SEQ_ACTIVE_RESET			31
#define FCOE_ERROR_CODE_MIDPATH_EOFN_END_SEQ_SET			32
#define FCOE_ERROR_CODE_MIDPATH_EOFT_END_SEQ_RESET			33
#define FCOE_ERROR_CODE_MIDPATH_ELS_REPLY_FCTL				34
#define FCOE_ERROR_CODE_MIDPATH_INVALID_REPLY				35
#define FCOE_ERROR_CODE_MIDPATH_ELS_REPLY_RCTL				36

/* ABTS error codes */
#define FCOE_ERROR_CODE_ABTS_REPLY_F_CTL				37
#define FCOE_ERROR_CODE_ABTS_REPLY_DDF_RCTL_FIELD			38
#define FCOE_ERROR_CODE_ABTS_REPLY_INVALID_BLS_RCTL			39
#define FCOE_ERROR_CODE_ABTS_REPLY_INVALID_RCTL			40
#define FCOE_ERROR_CODE_ABTS_REPLY_RCTL_GENERAL_MISMATCH		41

/* Common error codes */
#define FCOE_ERROR_CODE_COMMON_MIDDLE_FRAME_WITH_PAD			42
#define FCOE_ERROR_CODE_COMMON_SEQ_INIT_IN_TCE				43
#define FCOE_ERROR_CODE_COMMON_FC_HDR_RX_ID_MISMATCH			44
#define FCOE_ERROR_CODE_COMMON_INCORRECT_SEQ_CNT			45
#define FCOE_ERROR_CODE_COMMON_DATA_FC_HDR_FCP_TYPE_MISMATCH		46
#define FCOE_ERROR_CODE_COMMON_DATA_NO_MORE_SGES			47
#define FCOE_ERROR_CODE_COMMON_OPTIONAL_FC_HDR				48
#define FCOE_ERROR_CODE_COMMON_READ_TCE_OX_ID_TOO_BIG			49
#define FCOE_ERROR_CODE_COMMON_DATA_WAS_NOT_TRANSMITTED		50

/* Unsolicited Rx error codes */
#define FCOE_ERROR_CODE_UNSOLICITED_TYPE_NOT_ELS			51
#define FCOE_ERROR_CODE_UNSOLICITED_TYPE_NOT_BLS			52
#define FCOE_ERROR_CODE_UNSOLICITED_FCTL_ELS				53
#define FCOE_ERROR_CODE_UNSOLICITED_FCTL_BLS				54
#define FCOE_ERROR_CODE_UNSOLICITED_R_CTL				55

#define FCOE_ERROR_CODE_RW_TASK_DDF_RCTL_INFO_FIELD			56
#define FCOE_ERROR_CODE_RW_TASK_INVALID_RCTL				57
#define FCOE_ERROR_CODE_RW_TASK_RCTL_GENERAL_MISMATCH			58

/* Timer error codes */
#define FCOE_ERROR_CODE_E_D_TOV_TIMER_EXPIRATION			60
#define FCOE_ERROR_CODE_REC_TOV_TIMER_EXPIRATION			61


#endif /* BNX2FC_CONSTANTS_H_ */
