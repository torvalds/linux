/*
 *  QLogic FCoE Offload Driver
 *  Copyright (c) 2016-2018 Cavium Inc.
 *
 *  This software is available under the terms of the GNU General Public License
 *  (GPL) Version 2, available from the file COPYING in the main directory of
 *  this source tree.
 */
#ifndef _QEDFC_H_
#define _QEDFC_H_

#include <scsi/libfcoe.h>
#include <scsi/libfc.h>
#include <scsi/fc/fc_fip.h>
#include <scsi/fc/fc_fc2.h>
#include <scsi/scsi_tcq.h>
#include <scsi/fc_encode.h>
#include <linux/version.h>


/* qedf_hsi.h needs to before included any qed includes */
#include "qedf_hsi.h"

#include <linux/qed/qed_if.h>
#include <linux/qed/qed_fcoe_if.h>
#include <linux/qed/qed_ll2_if.h>
#include "qedf_version.h"
#include "qedf_dbg.h"
#include "drv_fcoe_fw_funcs.h"

/* Helpers to extract upper and lower 32-bits of pointer */
#define U64_HI(val) ((u32)(((u64)(val)) >> 32))
#define U64_LO(val) ((u32)(((u64)(val)) & 0xffffffff))

#define QEDF_DESCR "QLogic FCoE Offload Driver"
#define QEDF_MODULE_NAME "qedf"

#define QEDF_FLOGI_RETRY_CNT	3
#define QEDF_RPORT_RETRY_CNT	255
#define QEDF_MAX_SESSIONS	1024
#define QEDF_MAX_PAYLOAD	2048
#define QEDF_MAX_BDS_PER_CMD	256
#define QEDF_MAX_BD_LEN		0xffff
#define QEDF_BD_SPLIT_SZ	0x1000
#define QEDF_PAGE_SIZE		4096
#define QED_HW_DMA_BOUNDARY     0xfff
#define QEDF_MAX_SGLEN_FOR_CACHESGL		((1U << 16) - 1)
#define QEDF_MFS		(QEDF_MAX_PAYLOAD + \
	sizeof(struct fc_frame_header))
#define QEDF_MAX_NPIV		64
#define QEDF_TM_TIMEOUT		10
#define QEDF_ABORT_TIMEOUT	(10 * 1000)
#define QEDF_CLEANUP_TIMEOUT	1
#define QEDF_MAX_CDB_LEN	16

#define UPSTREAM_REMOVE		1
#define UPSTREAM_KEEP		1

struct qedf_mp_req {
	uint32_t req_len;
	void *req_buf;
	dma_addr_t req_buf_dma;
	struct scsi_sge *mp_req_bd;
	dma_addr_t mp_req_bd_dma;
	struct fc_frame_header req_fc_hdr;

	uint32_t resp_len;
	void *resp_buf;
	dma_addr_t resp_buf_dma;
	struct scsi_sge *mp_resp_bd;
	dma_addr_t mp_resp_bd_dma;
	struct fc_frame_header resp_fc_hdr;
};

struct qedf_els_cb_arg {
	struct qedf_ioreq *aborted_io_req;
	struct qedf_ioreq *io_req;
	u8 op; /* Used to keep track of ELS op */
	uint16_t l2_oxid;
	u32 offset; /* Used for sequence cleanup */
	u8 r_ctl; /* Used for sequence cleanup */
};

enum qedf_ioreq_event {
	QEDF_IOREQ_EV_NONE,
	QEDF_IOREQ_EV_ABORT_SUCCESS,
	QEDF_IOREQ_EV_ABORT_FAILED,
	QEDF_IOREQ_EV_SEND_RRQ,
	QEDF_IOREQ_EV_ELS_TMO,
	QEDF_IOREQ_EV_ELS_ERR_DETECT,
	QEDF_IOREQ_EV_ELS_FLUSH,
	QEDF_IOREQ_EV_CLEANUP_SUCCESS,
	QEDF_IOREQ_EV_CLEANUP_FAILED,
};

#define FC_GOOD		0
#define FCOE_FCP_RSP_FLAGS_FCP_RESID_OVER	(0x1<<2)
#define FCOE_FCP_RSP_FLAGS_FCP_RESID_UNDER	(0x1<<3)
#define CMD_SCSI_STATUS(Cmnd)			((Cmnd)->SCp.Status)
#define FCOE_FCP_RSP_FLAGS_FCP_RSP_LEN_VALID	(0x1<<0)
#define FCOE_FCP_RSP_FLAGS_FCP_SNS_LEN_VALID	(0x1<<1)
struct qedf_ioreq {
	struct list_head link;
	uint16_t xid;
	struct scsi_cmnd *sc_cmd;
#define QEDF_SCSI_CMD		1
#define QEDF_TASK_MGMT_CMD	2
#define QEDF_ABTS		3
#define QEDF_ELS		4
#define QEDF_CLEANUP		5
#define QEDF_SEQ_CLEANUP	6
	u8 cmd_type;
#define QEDF_CMD_OUTSTANDING		0x0
#define QEDF_CMD_IN_ABORT		0x1
#define QEDF_CMD_IN_CLEANUP		0x2
#define QEDF_CMD_SRR_SENT		0x3
#define QEDF_CMD_DIRTY			0x4
#define QEDF_CMD_ERR_SCSI_DONE		0x5
	u8 io_req_flags;
	uint8_t tm_flags;
	struct qedf_rport *fcport;
#define	QEDF_CMD_ST_INACTIVE		0
#define	QEDFC_CMD_ST_IO_ACTIVE		1
#define	QEDFC_CMD_ST_ABORT_ACTIVE	2
#define	QEDFC_CMD_ST_ABORT_ACTIVE_EH	3
#define	QEDFC_CMD_ST_CLEANUP_ACTIVE	4
#define	QEDFC_CMD_ST_CLEANUP_ACTIVE_EH	5
#define	QEDFC_CMD_ST_RRQ_ACTIVE		6
#define	QEDFC_CMD_ST_RRQ_WAIT		7
#define	QEDFC_CMD_ST_OXID_RETIRE_WAIT	8
#define	QEDFC_CMD_ST_TMF_ACTIVE		9
#define	QEDFC_CMD_ST_DRAIN_ACTIVE	10
#define	QEDFC_CMD_ST_CLEANED		11
#define	QEDFC_CMD_ST_ELS_ACTIVE		12
	atomic_t state;
	unsigned long flags;
	enum qedf_ioreq_event event;
	size_t data_xfer_len;
	/* ID: 001: Alloc cmd (qedf_alloc_cmd) */
	/* ID: 002: Initiate ABTS (qedf_initiate_abts) */
	/* ID: 003: For RRQ (qedf_process_abts_compl) */
	struct kref refcount;
	struct qedf_cmd_mgr *cmd_mgr;
	struct io_bdt *bd_tbl;
	struct delayed_work timeout_work;
	struct completion tm_done;
	struct completion abts_done;
	struct completion cleanup_done;
	struct e4_fcoe_task_context *task;
	struct fcoe_task_params *task_params;
	struct scsi_sgl_task_params *sgl_task_params;
	int idx;
	int lun;
/*
 * Need to allocate enough room for both sense data and FCP response data
 * which has a max length of 8 bytes according to spec.
 */
#define QEDF_SCSI_SENSE_BUFFERSIZE	(SCSI_SENSE_BUFFERSIZE + 8)
	uint8_t *sense_buffer;
	dma_addr_t sense_buffer_dma;
	u32 fcp_resid;
	u32 fcp_rsp_len;
	u32 fcp_sns_len;
	u8 cdb_status;
	u8 fcp_status;
	u8 fcp_rsp_code;
	u8 scsi_comp_flags;
#define QEDF_MAX_REUSE		0xfff
	u16 reuse_count;
	struct qedf_mp_req mp_req;
	void (*cb_func)(struct qedf_els_cb_arg *cb_arg);
	struct qedf_els_cb_arg *cb_arg;
	int fp_idx;
	unsigned int cpu;
	unsigned int int_cpu;
#define QEDF_IOREQ_UNKNOWN_SGE		1
#define QEDF_IOREQ_SLOW_SGE		2
#define QEDF_IOREQ_FAST_SGE		3
	u8 sge_type;
	struct delayed_work rrq_work;

	/* Used for sequence level recovery; i.e. REC/SRR */
	uint32_t rx_buf_off;
	uint32_t tx_buf_off;
	uint32_t rx_id;
	uint32_t task_retry_identifier;

	/*
	 * Used to tell if we need to return a SCSI command
	 * during some form of error processing.
	 */
	bool return_scsi_cmd_on_abts;

	unsigned int alloc;
};

extern struct workqueue_struct *qedf_io_wq;

struct qedf_rport {
	spinlock_t rport_lock;
#define QEDF_RPORT_SESSION_READY 1
#define QEDF_RPORT_UPLOADING_CONNECTION	2
#define QEDF_RPORT_IN_RESET 3
#define QEDF_RPORT_IN_LUN_RESET 4
#define QEDF_RPORT_IN_TARGET_RESET 5
	unsigned long flags;
	int lun_reset_lun;
	unsigned long retry_delay_timestamp;
	struct fc_rport *rport;
	struct fc_rport_priv *rdata;
	struct qedf_ctx *qedf;
	u32 handle; /* Handle from qed */
	u32 fw_cid; /* fw_cid from qed */
	void __iomem *p_doorbell;
	/* Send queue management */
	atomic_t free_sqes;
	atomic_t ios_to_queue;
	atomic_t num_active_ios;
	struct fcoe_wqe *sq;
	dma_addr_t sq_dma;
	u16 sq_prod_idx;
	u16 fw_sq_prod_idx;
	u16 sq_con_idx;
	u32 sq_mem_size;
	void *sq_pbl;
	dma_addr_t sq_pbl_dma;
	u32 sq_pbl_size;
	u32 sid;
#define	QEDF_RPORT_TYPE_DISK		0
#define	QEDF_RPORT_TYPE_TAPE		1
	uint dev_type; /* Disk or tape */
	struct list_head peers;
};

/* Used to contain LL2 skb's in ll2_skb_list */
struct qedf_skb_work {
	struct work_struct work;
	struct sk_buff *skb;
	struct qedf_ctx *qedf;
};

struct qedf_fastpath {
#define	QEDF_SB_ID_NULL		0xffff
	u16		sb_id;
	struct qed_sb_info	*sb_info;
	struct qedf_ctx *qedf;
	/* Keep track of number of completions on this fastpath */
	unsigned long completions;
	uint32_t cq_num_entries;
};

/* Used to pass fastpath information needed to process CQEs */
struct qedf_io_work {
	struct work_struct work;
	struct fcoe_cqe cqe;
	struct qedf_ctx *qedf;
	struct fc_frame *fp;
};

struct qedf_glbl_q_params {
	u64	hw_p_cq;	/* Completion queue PBL */
	u64	hw_p_rq;	/* Request queue PBL */
	u64	hw_p_cmdq;	/* Command queue PBL */
};

struct global_queue {
	struct fcoe_cqe *cq;
	dma_addr_t cq_dma;
	u32 cq_mem_size;
	u32 cq_cons_idx; /* Completion queue consumer index */
	u32 cq_prod_idx;

	void *cq_pbl;
	dma_addr_t cq_pbl_dma;
	u32 cq_pbl_size;
};

/* I/O tracing entry */
#define QEDF_IO_TRACE_SIZE		2048
struct qedf_io_log {
#define QEDF_IO_TRACE_REQ		0
#define QEDF_IO_TRACE_RSP		1
	uint8_t direction;
	uint16_t task_id;
	uint32_t port_id; /* Remote port fabric ID */
	int lun;
	unsigned char op; /* SCSI CDB */
	uint8_t lba[4];
	unsigned int bufflen; /* SCSI buffer length */
	unsigned int sg_count; /* Number of SG elements */
	int result; /* Result passed back to mid-layer */
	unsigned long jiffies; /* Time stamp when I/O logged */
	int refcount; /* Reference count for task id */
	unsigned int req_cpu; /* CPU that the task is queued on */
	unsigned int int_cpu; /* Interrupt CPU that the task is received on */
	unsigned int rsp_cpu; /* CPU that task is returned on */
	u8 sge_type; /* Did we take the slow, single or fast SGE path */
};

/* Number of entries in BDQ */
#define QEDF_BDQ_SIZE			256
#define QEDF_BDQ_BUF_SIZE		2072

/* DMA coherent buffers for BDQ */
struct qedf_bdq_buf {
	void *buf_addr;
	dma_addr_t buf_dma;
};

/* Main adapter struct */
struct qedf_ctx {
	struct qedf_dbg_ctx dbg_ctx;
	struct fcoe_ctlr ctlr;
	struct fc_lport *lport;
	u8 data_src_addr[ETH_ALEN];
#define QEDF_LINK_DOWN		0
#define QEDF_LINK_UP		1
	atomic_t link_state;
#define QEDF_DCBX_PENDING	0
#define QEDF_DCBX_DONE		1
	atomic_t dcbx;
#define QEDF_NULL_VLAN_ID	-1
#define QEDF_FALLBACK_VLAN	1002
#define QEDF_DEFAULT_PRIO	3
	int vlan_id;
	u8 prio;
	struct qed_dev *cdev;
	struct qed_dev_fcoe_info dev_info;
	struct qed_int_info int_info;
	uint16_t last_command;
	spinlock_t hba_lock;
	struct pci_dev *pdev;
	u64 wwnn;
	u64 wwpn;
	u8 __aligned(16) mac[ETH_ALEN];
	struct list_head fcports;
	atomic_t num_offloads;
	unsigned int curr_conn_id;
	struct workqueue_struct *ll2_recv_wq;
	struct workqueue_struct *link_update_wq;
	struct delayed_work link_update;
	struct delayed_work link_recovery;
	struct completion flogi_compl;
	struct completion fipvlan_compl;

	/*
	 * Used to tell if we're in the window where we are waiting for
	 * the link to come back up before informting fcoe that the link is
	 * done.
	 */
	atomic_t link_down_tmo_valid;
#define QEDF_TIMER_INTERVAL		(1 * HZ)
	struct timer_list timer; /* One second book keeping timer */
#define QEDF_DRAIN_ACTIVE		1
#define QEDF_LL2_STARTED		2
#define QEDF_UNLOADING			3
#define QEDF_GRCDUMP_CAPTURE		4
#define QEDF_IN_RECOVERY		5
#define QEDF_DBG_STOP_IO		6
	unsigned long flags; /* Miscellaneous state flags */
	int fipvlan_retries;
	u8 num_queues;
	struct global_queue **global_queues;
	/* Pointer to array of queue structures */
	struct qedf_glbl_q_params *p_cpuq;
	/* Physical address of array of queue structures */
	dma_addr_t hw_p_cpuq;

	struct qedf_bdq_buf bdq[QEDF_BDQ_SIZE];
	void *bdq_pbl;
	dma_addr_t bdq_pbl_dma;
	size_t bdq_pbl_mem_size;
	void *bdq_pbl_list;
	dma_addr_t bdq_pbl_list_dma;
	u8 bdq_pbl_list_num_entries;
	void __iomem *bdq_primary_prod;
	void __iomem *bdq_secondary_prod;
	uint16_t bdq_prod_idx;

	/* Structure for holding all the fastpath for this qedf_ctx */
	struct qedf_fastpath *fp_array;
	struct qed_fcoe_tid tasks;
	struct qedf_cmd_mgr *cmd_mgr;
	/* Holds the PF parameters we pass to qed to start he FCoE function */
	struct qed_pf_params pf_params;
	/* Used to time middle path ELS and TM commands */
	struct workqueue_struct *timer_work_queue;

#define QEDF_IO_WORK_MIN		64
	mempool_t *io_mempool;
	struct workqueue_struct *dpc_wq;
	struct delayed_work grcdump_work;

	u32 slow_sge_ios;
	u32 fast_sge_ios;

	uint8_t	*grcdump;
	uint32_t grcdump_size;

	struct qedf_io_log io_trace_buf[QEDF_IO_TRACE_SIZE];
	spinlock_t io_trace_lock;
	uint16_t io_trace_idx;

	bool stop_io_on_error;

	u32 flogi_cnt;
	u32 flogi_failed;

	/* Used for fc statistics */
	struct mutex stats_mutex;
	u64 input_requests;
	u64 output_requests;
	u64 control_requests;
	u64 packet_aborts;
	u64 alloc_failures;
	u8 lun_resets;
	u8 target_resets;
	u8 task_set_fulls;
	u8 busy;
	/* Used for flush routine */
	struct mutex flush_mutex;
};

struct io_bdt {
	struct qedf_ioreq *io_req;
	struct scsi_sge *bd_tbl;
	dma_addr_t bd_tbl_dma;
	u16 bd_valid;
};

struct qedf_cmd_mgr {
	struct qedf_ctx *qedf;
	u16 idx;
	struct io_bdt **io_bdt_pool;
#define FCOE_PARAMS_NUM_TASKS		2048
	struct qedf_ioreq cmds[FCOE_PARAMS_NUM_TASKS];
	spinlock_t lock;
	atomic_t free_list_cnt;
};

/* Stolen from qed_cxt_api.h and adapted for qed_fcoe_info
 * Usage:
 *
 * void *ptr;
 * ptr = qedf_get_task_mem(&qedf->tasks, 128);
 */
static inline void *qedf_get_task_mem(struct qed_fcoe_tid *info, u32 tid)
{
	return (void *)(info->blocks[tid / info->num_tids_per_block] +
			(tid % info->num_tids_per_block) * info->size);
}

static inline void qedf_stop_all_io(struct qedf_ctx *qedf)
{
	set_bit(QEDF_DBG_STOP_IO, &qedf->flags);
}

/*
 * Externs
 */

/*
 * (QEDF_LOG_NPIV | QEDF_LOG_SESS | QEDF_LOG_LPORT | QEDF_LOG_ELS | QEDF_LOG_MQ
 * | QEDF_LOG_IO | QEDF_LOG_UNSOL | QEDF_LOG_SCSI_TM | QEDF_LOG_MP_REQ |
 * QEDF_LOG_EVT | QEDF_LOG_CONN | QEDF_LOG_DISC | QEDF_LOG_INFO)
 */
#define QEDF_DEFAULT_LOG_MASK		0x3CFB6
extern const struct qed_fcoe_ops *qed_ops;
extern uint qedf_dump_frames;
extern uint qedf_io_tracing;
extern uint qedf_stop_io_on_error;
extern uint qedf_link_down_tmo;
#define QEDF_RETRY_DELAY_MAX		20 /* 2 seconds */
extern bool qedf_retry_delay;
extern uint qedf_debug;

extern struct qedf_cmd_mgr *qedf_cmd_mgr_alloc(struct qedf_ctx *qedf);
extern void qedf_cmd_mgr_free(struct qedf_cmd_mgr *cmgr);
extern int qedf_queuecommand(struct Scsi_Host *host,
	struct scsi_cmnd *sc_cmd);
extern void qedf_fip_send(struct fcoe_ctlr *fip, struct sk_buff *skb);
extern u8 *qedf_get_src_mac(struct fc_lport *lport);
extern void qedf_fip_recv(struct qedf_ctx *qedf, struct sk_buff *skb);
extern void qedf_fcoe_send_vlan_req(struct qedf_ctx *qedf);
extern void qedf_scsi_completion(struct qedf_ctx *qedf, struct fcoe_cqe *cqe,
	struct qedf_ioreq *io_req);
extern void qedf_process_warning_compl(struct qedf_ctx *qedf,
	struct fcoe_cqe *cqe, struct qedf_ioreq *io_req);
extern void qedf_process_error_detect(struct qedf_ctx *qedf,
	struct fcoe_cqe *cqe, struct qedf_ioreq *io_req);
extern void qedf_flush_active_ios(struct qedf_rport *fcport, int lun);
extern void qedf_release_cmd(struct kref *ref);
extern int qedf_initiate_abts(struct qedf_ioreq *io_req,
	bool return_scsi_cmd_on_abts);
extern void qedf_process_abts_compl(struct qedf_ctx *qedf, struct fcoe_cqe *cqe,
	struct qedf_ioreq *io_req);
extern struct qedf_ioreq *qedf_alloc_cmd(struct qedf_rport *fcport,
	u8 cmd_type);

extern struct device_attribute *qedf_host_attrs[];
extern void qedf_cmd_timer_set(struct qedf_ctx *qedf, struct qedf_ioreq *io_req,
	unsigned int timer_msec);
extern int qedf_init_mp_req(struct qedf_ioreq *io_req);
extern void qedf_init_mp_task(struct qedf_ioreq *io_req,
	struct e4_fcoe_task_context *task_ctx, struct fcoe_wqe *sqe);
extern u16 qedf_get_sqe_idx(struct qedf_rport *fcport);
extern void qedf_ring_doorbell(struct qedf_rport *fcport);
extern void qedf_process_els_compl(struct qedf_ctx *qedf, struct fcoe_cqe *cqe,
	struct qedf_ioreq *els_req);
extern int qedf_send_rrq(struct qedf_ioreq *aborted_io_req);
extern int qedf_send_adisc(struct qedf_rport *fcport, struct fc_frame *fp);
extern int qedf_initiate_cleanup(struct qedf_ioreq *io_req,
	bool return_scsi_cmd_on_abts);
extern void qedf_process_cleanup_compl(struct qedf_ctx *qedf,
	struct fcoe_cqe *cqe, struct qedf_ioreq *io_req);
extern int qedf_initiate_tmf(struct scsi_cmnd *sc_cmd, u8 tm_flags);
extern void qedf_process_tmf_compl(struct qedf_ctx *qedf, struct fcoe_cqe *cqe,
	struct qedf_ioreq *io_req);
extern void qedf_process_cqe(struct qedf_ctx *qedf, struct fcoe_cqe *cqe);
extern void qedf_scsi_done(struct qedf_ctx *qedf, struct qedf_ioreq *io_req,
	int result);
extern void qedf_set_vlan_id(struct qedf_ctx *qedf, int vlan_id);
extern void qedf_create_sysfs_ctx_attr(struct qedf_ctx *qedf);
extern void qedf_remove_sysfs_ctx_attr(struct qedf_ctx *qedf);
extern void qedf_capture_grc_dump(struct qedf_ctx *qedf);
bool qedf_wait_for_upload(struct qedf_ctx *qedf);
extern void qedf_process_unsol_compl(struct qedf_ctx *qedf, uint16_t que_idx,
	struct fcoe_cqe *cqe);
extern void qedf_restart_rport(struct qedf_rport *fcport);
extern int qedf_send_rec(struct qedf_ioreq *orig_io_req);
extern int qedf_post_io_req(struct qedf_rport *fcport,
	struct qedf_ioreq *io_req);
extern void qedf_process_seq_cleanup_compl(struct qedf_ctx *qedf,
	struct fcoe_cqe *cqe, struct qedf_ioreq *io_req);
extern int qedf_send_flogi(struct qedf_ctx *qedf);
extern void qedf_get_protocol_tlv_data(void *dev, void *data);
extern void qedf_fp_io_handler(struct work_struct *work);
extern void qedf_get_generic_tlv_data(void *dev, struct qed_generic_tlvs *data);
extern void qedf_wq_grcdump(struct work_struct *work);
void qedf_stag_change_work(struct work_struct *work);
void qedf_ctx_soft_reset(struct fc_lport *lport);

#define FCOE_WORD_TO_BYTE  4
#define QEDF_MAX_TASK_NUM	0xFFFF

struct fip_vlan {
	struct ethhdr eth;
	struct fip_header fip;
	struct {
		struct fip_mac_desc mac;
		struct fip_wwn_desc wwnn;
	} desc;
};

/* SQ/CQ Sizes */
#define GBL_RSVD_TASKS			16
#define NUM_TASKS_PER_CONNECTION	1024
#define NUM_RW_TASKS_PER_CONNECTION	512
#define FCOE_PARAMS_CQ_NUM_ENTRIES	FCOE_PARAMS_NUM_TASKS

#define FCOE_PARAMS_CMDQ_NUM_ENTRIES	FCOE_PARAMS_NUM_TASKS
#define SQ_NUM_ENTRIES			NUM_TASKS_PER_CONNECTION

#define QEDF_FCOE_PARAMS_GL_RQ_PI              0
#define QEDF_FCOE_PARAMS_GL_CMD_PI             1

#define QEDF_READ                     (1 << 1)
#define QEDF_WRITE                    (1 << 0)
#define MAX_FIBRE_LUNS			0xffffffff

#define MIN_NUM_CPUS_MSIX(x)	min_t(u32, x->dev_info.num_cqs, \
					num_online_cpus())

/*
 * PCI function probe defines
 */
/* Probe/remove called during normal PCI probe */
#define	QEDF_MODE_NORMAL		0
/* Probe/remove called from qed error recovery */
#define QEDF_MODE_RECOVERY		1

#define SUPPORTED_25000baseKR_Full    (1<<27)
#define SUPPORTED_50000baseKR2_Full   (1<<28)
#define SUPPORTED_100000baseKR4_Full  (1<<29)
#define SUPPORTED_100000baseCR4_Full  (1<<30)

#endif
