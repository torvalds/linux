/* From iscsi_iser.h */

/* Constant PDU lengths calculations */
#define ISER_HEADERS_LEN  (sizeof(struct iser_ctrl) + sizeof(struct iscsi_hdr))

#define ISER_RECV_DATA_SEG_LEN  8192
#define ISER_RX_PAYLOAD_SIZE    (ISER_HEADERS_LEN + ISER_RECV_DATA_SEG_LEN)
#define ISER_RX_LOGIN_SIZE      (ISER_HEADERS_LEN + ISCSI_DEF_MAX_RECV_SEG_LEN)

/* QP settings */
/* Maximal bounds on received asynchronous PDUs */
#define ISERT_MAX_TX_MISC_PDUS	4 /* NOOP_IN(2) , ASYNC_EVENT(2)   */

#define ISERT_MAX_RX_MISC_PDUS	6 /* NOOP_OUT(2), TEXT(1),         *
				   * SCSI_TMFUNC(2), LOGOUT(1) */

#define ISCSI_DEF_XMIT_CMDS_MAX 128 /* from libiscsi.h, must be power of 2 */

#define ISERT_QP_MAX_RECV_DTOS	(ISCSI_DEF_XMIT_CMDS_MAX)

#define ISERT_MIN_POSTED_RX	(ISCSI_DEF_XMIT_CMDS_MAX >> 2)

#define ISERT_INFLIGHT_DATAOUTS	8

#define ISERT_QP_MAX_REQ_DTOS	(ISCSI_DEF_XMIT_CMDS_MAX *    \
				(1 + ISERT_INFLIGHT_DATAOUTS) + \
				ISERT_MAX_TX_MISC_PDUS	+ \
				ISERT_MAX_RX_MISC_PDUS)

#define ISER_RX_PAD_SIZE	(ISER_RECV_DATA_SEG_LEN + 4096 - \
		(ISER_RX_PAYLOAD_SIZE + sizeof(u64) + sizeof(struct ib_sge)))
