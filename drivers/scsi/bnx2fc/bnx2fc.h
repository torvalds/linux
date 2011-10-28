#ifndef _BNX2FC_H_
#define _BNX2FC_H_
/* bnx2fc.h: Broadcom NetXtreme II Linux FCoE offload driver.
 *
 * Copyright (c) 2008 - 2010 Broadcom Corporation
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation.
 *
 * Written by: Bhanu Prakash Gollapudi (bprakash@broadcom.com)
 */

#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/kernel.h>
#include <linux/skbuff.h>
#include <linux/netdevice.h>
#include <linux/etherdevice.h>
#include <linux/if_ether.h>
#include <linux/if_vlan.h>
#include <linux/kthread.h>
#include <linux/crc32.h>
#include <linux/cpu.h>
#include <linux/types.h>
#include <linux/list.h>
#include <linux/delay.h>
#include <linux/timer.h>
#include <linux/errno.h>
#include <linux/pci.h>
#include <linux/init.h>
#include <linux/dma-mapping.h>
#include <linux/workqueue.h>
#include <linux/mutex.h>
#include <linux/spinlock.h>
#include <linux/bitops.h>
#include <linux/log2.h>
#include <linux/interrupt.h>
#include <linux/sched.h>
#include <linux/io.h>

#include <scsi/scsi.h>
#include <scsi/scsi_host.h>
#include <scsi/scsi_device.h>
#include <scsi/scsi_cmnd.h>
#include <scsi/scsi_eh.h>
#include <scsi/scsi_tcq.h>
#include <scsi/libfc.h>
#include <scsi/libfcoe.h>
#include <scsi/fc_encode.h>
#include <scsi/scsi_transport.h>
#include <scsi/scsi_transport_fc.h>
#include <scsi/fc/fc_fip.h>
#include <scsi/fc/fc_fc2.h>
#include <scsi/fc_frame.h>
#include <scsi/fc/fc_fcoe.h>
#include <scsi/fc/fc_fcp.h>

#include "57xx_hsi_bnx2fc.h"
#include "bnx2fc_debug.h"
#include "../../net/cnic_if.h"
#include "bnx2fc_constants.h"

#define BNX2FC_NAME		"bnx2fc"
#define BNX2FC_VERSION		"1.0.1"

#define PFX			"bnx2fc: "

#define BNX2X_DOORBELL_PCI_BAR		2

#define BNX2FC_MAX_BD_LEN		0xffff
#define BNX2FC_BD_SPLIT_SZ		0x8000
#define BNX2FC_MAX_BDS_PER_CMD		256

#define BNX2FC_SQ_WQES_MAX	256

#define BNX2FC_SCSI_MAX_SQES	((3 * BNX2FC_SQ_WQES_MAX) / 8)
#define BNX2FC_TM_MAX_SQES	((BNX2FC_SQ_WQES_MAX) / 2)
#define BNX2FC_ELS_MAX_SQES	(BNX2FC_TM_MAX_SQES - 1)

#define BNX2FC_RQ_WQES_MAX	16
#define BNX2FC_CQ_WQES_MAX	(BNX2FC_SQ_WQES_MAX + BNX2FC_RQ_WQES_MAX)

#define BNX2FC_NUM_MAX_SESS	128
#define BNX2FC_NUM_MAX_SESS_LOG	(ilog2(BNX2FC_NUM_MAX_SESS))

#define BNX2FC_MAX_OUTSTANDING_CMNDS	2048
#define BNX2FC_CAN_QUEUE		BNX2FC_MAX_OUTSTANDING_CMNDS
#define BNX2FC_ELSTM_XIDS		BNX2FC_CAN_QUEUE
#define BNX2FC_MIN_PAYLOAD		256
#define BNX2FC_MAX_PAYLOAD		2048
#define BNX2FC_MFS			\
			(BNX2FC_MAX_PAYLOAD + sizeof(struct fc_frame_header))
#define BNX2FC_MINI_JUMBO_MTU		2500


#define BNX2FC_RQ_BUF_SZ		256
#define BNX2FC_RQ_BUF_LOG_SZ		(ilog2(BNX2FC_RQ_BUF_SZ))

#define BNX2FC_SQ_WQE_SIZE		(sizeof(struct fcoe_sqe))
#define BNX2FC_CQ_WQE_SIZE		(sizeof(struct fcoe_cqe))
#define BNX2FC_RQ_WQE_SIZE		(BNX2FC_RQ_BUF_SZ)
#define BNX2FC_XFERQ_WQE_SIZE		(sizeof(struct fcoe_xfrqe))
#define BNX2FC_CONFQ_WQE_SIZE		(sizeof(struct fcoe_confqe))
#define BNX2FC_5771X_DB_PAGE_SIZE	128

#define BNX2FC_MAX_TASKS		\
			     (BNX2FC_MAX_OUTSTANDING_CMNDS + BNX2FC_ELSTM_XIDS)
#define BNX2FC_TASK_SIZE		128
#define	BNX2FC_TASKS_PER_PAGE		(PAGE_SIZE/BNX2FC_TASK_SIZE)
#define BNX2FC_TASK_CTX_ARR_SZ		(BNX2FC_MAX_TASKS/BNX2FC_TASKS_PER_PAGE)

#define BNX2FC_MAX_ROWS_IN_HASH_TBL	8
#define BNX2FC_HASH_TBL_CHUNK_SIZE	(16 * 1024)

#define BNX2FC_MAX_SEQS			255

#define BNX2FC_READ			(1 << 1)
#define BNX2FC_WRITE			(1 << 0)

#define BNX2FC_MIN_XID			0
#define BNX2FC_MAX_XID			\
			(BNX2FC_MAX_OUTSTANDING_CMNDS + BNX2FC_ELSTM_XIDS - 1)
#define FCOE_MIN_XID			(BNX2FC_MAX_XID + 1)
#define FCOE_MAX_XID			(FCOE_MIN_XID + 4095)
#define BNX2FC_MAX_LUN			0xFFFF
#define BNX2FC_MAX_FCP_TGT		256
#define BNX2FC_MAX_CMD_LEN		16

#define BNX2FC_TM_TIMEOUT		60	/* secs */
#define BNX2FC_IO_TIMEOUT		20000UL	/* msecs */

#define BNX2FC_WAIT_CNT			1200
#define BNX2FC_FW_TIMEOUT		(3 * HZ)
#define PORT_MAX			2

#define CMD_SCSI_STATUS(Cmnd)		((Cmnd)->SCp.Status)

/* FC FCP Status */
#define	FC_GOOD				0

#define BNX2FC_RNID_HBA			0x7

/* bnx2fc driver uses only one instance of fcoe_percpu_s */
extern struct fcoe_percpu_s bnx2fc_global;

extern struct workqueue_struct *bnx2fc_wq;

struct bnx2fc_percpu_s {
	struct task_struct *iothread;
	struct list_head work_list;
	spinlock_t fp_work_lock;
};


struct bnx2fc_hba {
	struct list_head link;
	struct cnic_dev *cnic;
	struct pci_dev *pcidev;
	struct net_device *netdev;
	struct net_device *phys_dev;
	unsigned long reg_with_cnic;
		#define BNX2FC_CNIC_REGISTERED           1
	struct packet_type fcoe_packet_type;
	struct packet_type fip_packet_type;
	struct bnx2fc_cmd_mgr *cmd_mgr;
	struct workqueue_struct *timer_work_queue;
	struct kref kref;
	spinlock_t hba_lock;
	struct mutex hba_mutex;
	unsigned long adapter_state;
		#define ADAPTER_STATE_UP		0
		#define ADAPTER_STATE_GOING_DOWN	1
		#define ADAPTER_STATE_LINK_DOWN		2
		#define ADAPTER_STATE_READY		3
	u32 flags;
	unsigned long init_done;
		#define BNX2FC_FW_INIT_DONE		0
		#define BNX2FC_CTLR_INIT_DONE		1
		#define BNX2FC_CREATE_DONE		2
	struct fcoe_ctlr ctlr;
	u8 vlan_enabled;
	int vlan_id;
	u32 next_conn_id;
	struct fcoe_task_ctx_entry **task_ctx;
	dma_addr_t *task_ctx_dma;
	struct regpair *task_ctx_bd_tbl;
	dma_addr_t task_ctx_bd_dma;

	int hash_tbl_segment_count;
	void **hash_tbl_segments;
	void *hash_tbl_pbl;
	dma_addr_t hash_tbl_pbl_dma;
	struct fcoe_t2_hash_table_entry *t2_hash_tbl;
	dma_addr_t t2_hash_tbl_dma;
	char *t2_hash_tbl_ptr;
	dma_addr_t t2_hash_tbl_ptr_dma;

	char *dummy_buffer;
	dma_addr_t dummy_buf_dma;

	struct fcoe_statistics_params *stats_buffer;
	dma_addr_t stats_buf_dma;

	/*
	 * PCI related info.
	 */
	u16 pci_did;
	u16 pci_vid;
	u16 pci_sdid;
	u16 pci_svid;
	u16 pci_func;
	u16 pci_devno;

	struct task_struct *l2_thread;

	/* linkdown handling */
	wait_queue_head_t shutdown_wait;
	int wait_for_link_down;

	/*destroy handling */
	struct timer_list destroy_timer;
	wait_queue_head_t destroy_wait;

	/* Active list of offloaded sessions */
	struct bnx2fc_rport *tgt_ofld_list[BNX2FC_NUM_MAX_SESS];
	int num_ofld_sess;

	/* statistics */
	struct completion stat_req_done;
};

#define bnx2fc_from_ctlr(fip) container_of(fip, struct bnx2fc_hba, ctlr)

struct bnx2fc_cmd_mgr {
	struct bnx2fc_hba *hba;
	u16 next_idx;
	struct list_head *free_list;
	spinlock_t *free_list_lock;
	struct io_bdt **io_bdt_pool;
	struct bnx2fc_cmd **cmds;
};

struct bnx2fc_rport {
	struct fcoe_port *port;
	struct fc_rport *rport;
	struct fc_rport_priv *rdata;
	void __iomem *ctx_base;
#define DPM_TRIGER_TYPE		0x40
	u32 fcoe_conn_id;
	u32 context_id;
	u32 sid;

	unsigned long flags;
#define BNX2FC_FLAG_SESSION_READY	0x1
#define BNX2FC_FLAG_OFFLOADED		0x2
#define BNX2FC_FLAG_DISABLED		0x3
#define BNX2FC_FLAG_DESTROYED		0x4
#define BNX2FC_FLAG_OFLD_REQ_CMPL	0x5
#define BNX2FC_FLAG_DESTROY_CMPL	0x6
#define BNX2FC_FLAG_CTX_ALLOC_FAILURE	0x7
#define BNX2FC_FLAG_UPLD_REQ_COMPL	0x8
#define BNX2FC_FLAG_EXPL_LOGO		0x9

	u32 max_sqes;
	u32 max_rqes;
	u32 max_cqes;

	struct fcoe_sqe *sq;
	dma_addr_t sq_dma;
	u16 sq_prod_idx;
	u8 sq_curr_toggle_bit;
	u32 sq_mem_size;

	struct fcoe_cqe *cq;
	dma_addr_t cq_dma;
	u32 cq_cons_idx;
	u8 cq_curr_toggle_bit;
	u32 cq_mem_size;

	void *rq;
	dma_addr_t rq_dma;
	u32 rq_prod_idx;
	u32 rq_cons_idx;
	u32 rq_mem_size;

	void *rq_pbl;
	dma_addr_t rq_pbl_dma;
	u32 rq_pbl_size;

	struct fcoe_xfrqe *xferq;
	dma_addr_t xferq_dma;
	u32 xferq_mem_size;

	struct fcoe_confqe *confq;
	dma_addr_t confq_dma;
	u32 confq_mem_size;

	void *confq_pbl;
	dma_addr_t confq_pbl_dma;
	u32 confq_pbl_size;

	struct fcoe_conn_db *conn_db;
	dma_addr_t conn_db_dma;
	u32 conn_db_mem_size;

	struct fcoe_sqe *lcq;
	dma_addr_t lcq_dma;
	u32 lcq_mem_size;

	void *ofld_req[4];
	dma_addr_t ofld_req_dma[4];
	void *enbl_req;
	dma_addr_t enbl_req_dma;

	spinlock_t tgt_lock;
	spinlock_t cq_lock;
	atomic_t num_active_ios;
	u32 flush_in_prog;
	unsigned long work_time_slice;
	unsigned long timestamp;
	struct list_head free_task_list;
	struct bnx2fc_cmd *pending_queue[BNX2FC_SQ_WQES_MAX+1];
	atomic_t pi;
	atomic_t ci;
	struct list_head active_cmd_queue;
	struct list_head els_queue;
	struct list_head io_retire_queue;
	struct list_head active_tm_queue;

	struct timer_list ofld_timer;
	wait_queue_head_t ofld_wait;

	struct timer_list upld_timer;
	wait_queue_head_t upld_wait;
};

struct bnx2fc_mp_req {
	u8 tm_flags;

	u32 req_len;
	void *req_buf;
	dma_addr_t req_buf_dma;
	struct fcoe_bd_ctx *mp_req_bd;
	dma_addr_t mp_req_bd_dma;
	struct fc_frame_header req_fc_hdr;

	u32 resp_len;
	void *resp_buf;
	dma_addr_t resp_buf_dma;
	struct fcoe_bd_ctx *mp_resp_bd;
	dma_addr_t mp_resp_bd_dma;
	struct fc_frame_header resp_fc_hdr;
};

struct bnx2fc_els_cb_arg {
	struct bnx2fc_cmd *aborted_io_req;
	struct bnx2fc_cmd *io_req;
	u16 l2_oxid;
};

/* bnx2fc command structure */
struct bnx2fc_cmd {
	struct list_head link;
	u8 on_active_queue;
	u8 on_tmf_queue;
	u8 cmd_type;
#define BNX2FC_SCSI_CMD		1
#define BNX2FC_TASK_MGMT_CMD		2
#define BNX2FC_ABTS			3
#define BNX2FC_ELS			4
#define BNX2FC_CLEANUP			5
	u8 io_req_flags;
	struct kref refcount;
	struct fcoe_port *port;
	struct bnx2fc_rport *tgt;
	struct scsi_cmnd *sc_cmd;
	struct bnx2fc_cmd_mgr *cmd_mgr;
	struct bnx2fc_mp_req mp_req;
	void (*cb_func)(struct bnx2fc_els_cb_arg *cb_arg);
	struct bnx2fc_els_cb_arg *cb_arg;
	struct delayed_work timeout_work; /* timer for ULP timeouts */
	struct completion tm_done;
	int wait_for_comp;
	u16 xid;
	struct fcoe_task_ctx_entry *task;
	struct io_bdt *bd_tbl;
	struct fcp_rsp *rsp;
	size_t data_xfer_len;
	unsigned long req_flags;
#define BNX2FC_FLAG_ISSUE_RRQ		0x1
#define BNX2FC_FLAG_ISSUE_ABTS		0x2
#define BNX2FC_FLAG_ABTS_DONE		0x3
#define BNX2FC_FLAG_TM_COMPL		0x4
#define BNX2FC_FLAG_TM_TIMEOUT		0x5
#define BNX2FC_FLAG_IO_CLEANUP		0x6
#define BNX2FC_FLAG_RETIRE_OXID		0x7
#define	BNX2FC_FLAG_EH_ABORT		0x8
#define BNX2FC_FLAG_IO_COMPL		0x9
#define BNX2FC_FLAG_ELS_DONE		0xa
#define BNX2FC_FLAG_ELS_TIMEOUT		0xb
	u32 fcp_resid;
	u32 fcp_rsp_len;
	u32 fcp_sns_len;
	u8 cdb_status; /* SCSI IO status */
	u8 fcp_status; /* FCP IO status */
	u8 fcp_rsp_code;
	u8 scsi_comp_flags;
};

struct io_bdt {
	struct bnx2fc_cmd *io_req;
	struct fcoe_bd_ctx *bd_tbl;
	dma_addr_t bd_tbl_dma;
	u16 bd_valid;
};

struct bnx2fc_work {
	struct list_head list;
	struct bnx2fc_rport *tgt;
	u16 wqe;
};
struct bnx2fc_unsol_els {
	struct fc_lport *lport;
	struct fc_frame *fp;
	struct work_struct unsol_els_work;
};



struct bnx2fc_cmd *bnx2fc_elstm_alloc(struct bnx2fc_rport *tgt, int type);
void bnx2fc_cmd_release(struct kref *ref);
int bnx2fc_queuecommand(struct Scsi_Host *host, struct scsi_cmnd *sc_cmd);
int bnx2fc_send_fw_fcoe_init_msg(struct bnx2fc_hba *hba);
int bnx2fc_send_fw_fcoe_destroy_msg(struct bnx2fc_hba *hba);
int bnx2fc_send_session_ofld_req(struct fcoe_port *port,
					struct bnx2fc_rport *tgt);
int bnx2fc_send_session_disable_req(struct fcoe_port *port,
				    struct bnx2fc_rport *tgt);
int bnx2fc_send_session_destroy_req(struct bnx2fc_hba *hba,
					struct bnx2fc_rport *tgt);
int bnx2fc_map_doorbell(struct bnx2fc_rport *tgt);
void bnx2fc_indicate_kcqe(void *context, struct kcqe *kcq[],
					u32 num_cqe);
int bnx2fc_setup_task_ctx(struct bnx2fc_hba *hba);
void bnx2fc_free_task_ctx(struct bnx2fc_hba *hba);
int bnx2fc_setup_fw_resc(struct bnx2fc_hba *hba);
void bnx2fc_free_fw_resc(struct bnx2fc_hba *hba);
struct bnx2fc_cmd_mgr *bnx2fc_cmd_mgr_alloc(struct bnx2fc_hba *hba,
						u16 min_xid, u16 max_xid);
void bnx2fc_cmd_mgr_free(struct bnx2fc_cmd_mgr *cmgr);
void bnx2fc_get_link_state(struct bnx2fc_hba *hba);
char *bnx2fc_get_next_rqe(struct bnx2fc_rport *tgt, u8 num_items);
void bnx2fc_return_rqe(struct bnx2fc_rport *tgt, u8 num_items);
int bnx2fc_get_paged_crc_eof(struct sk_buff *skb, int tlen);
int bnx2fc_send_rrq(struct bnx2fc_cmd *aborted_io_req);
int bnx2fc_send_adisc(struct bnx2fc_rport *tgt, struct fc_frame *fp);
int bnx2fc_send_logo(struct bnx2fc_rport *tgt, struct fc_frame *fp);
int bnx2fc_send_rls(struct bnx2fc_rport *tgt, struct fc_frame *fp);
int bnx2fc_initiate_cleanup(struct bnx2fc_cmd *io_req);
int bnx2fc_initiate_abts(struct bnx2fc_cmd *io_req);
void bnx2fc_cmd_timer_set(struct bnx2fc_cmd *io_req,
			  unsigned int timer_msec);
int bnx2fc_init_mp_req(struct bnx2fc_cmd *io_req);
void bnx2fc_init_cleanup_task(struct bnx2fc_cmd *io_req,
			      struct fcoe_task_ctx_entry *task,
			      u16 orig_xid);
void bnx2fc_init_mp_task(struct bnx2fc_cmd *io_req,
			 struct fcoe_task_ctx_entry *task);
void bnx2fc_init_task(struct bnx2fc_cmd *io_req,
			     struct fcoe_task_ctx_entry *task);
void bnx2fc_add_2_sq(struct bnx2fc_rport *tgt, u16 xid);
void bnx2fc_ring_doorbell(struct bnx2fc_rport *tgt);
int bnx2fc_eh_abort(struct scsi_cmnd *sc_cmd);
int bnx2fc_eh_host_reset(struct scsi_cmnd *sc_cmd);
int bnx2fc_eh_target_reset(struct scsi_cmnd *sc_cmd);
int bnx2fc_eh_device_reset(struct scsi_cmnd *sc_cmd);
void bnx2fc_rport_event_handler(struct fc_lport *lport,
				struct fc_rport_priv *rport,
				enum fc_rport_event event);
void bnx2fc_process_scsi_cmd_compl(struct bnx2fc_cmd *io_req,
				   struct fcoe_task_ctx_entry *task,
				   u8 num_rq);
void bnx2fc_process_cleanup_compl(struct bnx2fc_cmd *io_req,
			       struct fcoe_task_ctx_entry *task,
			       u8 num_rq);
void bnx2fc_process_abts_compl(struct bnx2fc_cmd *io_req,
			       struct fcoe_task_ctx_entry *task,
			       u8 num_rq);
void bnx2fc_process_tm_compl(struct bnx2fc_cmd *io_req,
			     struct fcoe_task_ctx_entry *task,
			     u8 num_rq);
void bnx2fc_process_els_compl(struct bnx2fc_cmd *els_req,
			      struct fcoe_task_ctx_entry *task,
			      u8 num_rq);
void bnx2fc_build_fcp_cmnd(struct bnx2fc_cmd *io_req,
			   struct fcp_cmnd *fcp_cmnd);



void bnx2fc_flush_active_ios(struct bnx2fc_rport *tgt);
struct fc_seq *bnx2fc_elsct_send(struct fc_lport *lport, u32 did,
				      struct fc_frame *fp, unsigned int op,
				      void (*resp)(struct fc_seq *,
						   struct fc_frame *,
						   void *),
				      void *arg, u32 timeout);
int bnx2fc_process_new_cqes(struct bnx2fc_rport *tgt);
void bnx2fc_process_cq_compl(struct bnx2fc_rport *tgt, u16 wqe);
struct bnx2fc_rport *bnx2fc_tgt_lookup(struct fcoe_port *port,
					     u32 port_id);
void bnx2fc_process_l2_frame_compl(struct bnx2fc_rport *tgt,
				   unsigned char *buf,
				   u32 frame_len, u16 l2_oxid);
int bnx2fc_send_stat_req(struct bnx2fc_hba *hba);

#endif
