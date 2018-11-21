/*
 * QLogic iSCSI Offload Driver
 * Copyright (c) 2016 Cavium Inc.
 *
 * This software is available under the terms of the GNU General Public License
 * (GPL) Version 2, available from the file COPYING in the main directory of
 * this source tree.
 */

#ifndef _QEDI_H_
#define _QEDI_H_

#define __PREVENT_QED_HSI__

#include <scsi/scsi_transport_iscsi.h>
#include <scsi/libiscsi.h>
#include <scsi/scsi_host.h>
#include <linux/uio_driver.h>

#include "qedi_hsi.h"
#include <linux/qed/qed_if.h>
#include "qedi_dbg.h"
#include <linux/qed/qed_iscsi_if.h>
#include <linux/qed/qed_ll2_if.h>
#include "qedi_version.h"
#include "qedi_nvm_iscsi_cfg.h"

#define QEDI_MODULE_NAME		"qedi"

struct qedi_endpoint;

#ifndef GET_FIELD2
#define GET_FIELD2(value, name) \
	(((value) & (name ## _MASK)) >> (name ## _OFFSET))
#endif

/*
 * PCI function probe defines
 */
#define QEDI_MODE_NORMAL	0
#define QEDI_MODE_RECOVERY	1

#define ISCSI_WQE_SET_PTU_INVALIDATE	1
#define QEDI_MAX_ISCSI_TASK		4096
#define QEDI_MAX_TASK_NUM		0x0FFF
#define QEDI_MAX_ISCSI_CONNS_PER_HBA	1024
#define QEDI_ISCSI_MAX_BDS_PER_CMD	255	/* Firmware max BDs is 255 */
#define MAX_OUTSTANDING_TASKS_PER_CON	1024

#define QEDI_MAX_BD_LEN		0xffff
#define QEDI_BD_SPLIT_SZ	0x1000
#define QEDI_PAGE_SIZE		4096
#define QEDI_FAST_SGE_COUNT	4
/* MAX Length for cached SGL */
#define MAX_SGLEN_FOR_CACHESGL	((1U << 16) - 1)

#define MIN_NUM_CPUS_MSIX(x)	min_t(u32, x->dev_info.num_cqs, \
					num_online_cpus())

#define QEDI_LOCAL_PORT_MIN     60000
#define QEDI_LOCAL_PORT_MAX     61024
#define QEDI_LOCAL_PORT_RANGE   (QEDI_LOCAL_PORT_MAX - QEDI_LOCAL_PORT_MIN)
#define QEDI_LOCAL_PORT_INVALID	0xffff
#define TX_RX_RING		16
#define RX_RING			(TX_RX_RING - 1)
#define LL2_SINGLE_BUF_SIZE	0x400
#define QEDI_PAGE_ALIGN(addr)	ALIGN(addr, QEDI_PAGE_SIZE)
#define QEDI_PAGE_MASK		(~((QEDI_PAGE_SIZE) - 1))

#define QEDI_HW_DMA_BOUNDARY	0xfff
#define QEDI_PATH_HANDLE	0xFE0000000UL

enum qedi_nvm_tgts {
	QEDI_NVM_TGT_PRI,
	QEDI_NVM_TGT_SEC,
};

struct qedi_nvm_iscsi_image {
	struct nvm_iscsi_cfg iscsi_cfg;
	u32 crc;
};

struct qedi_uio_ctrl {
	/* meta data */
	u32 uio_hsi_version;

	/* user writes */
	u32 host_tx_prod;
	u32 host_rx_cons;
	u32 host_rx_bd_cons;
	u32 host_tx_pkt_len;
	u32 host_rx_cons_cnt;

	/* driver writes */
	u32 hw_tx_cons;
	u32 hw_rx_prod;
	u32 hw_rx_bd_prod;
	u32 hw_rx_prod_cnt;

	/* other */
	u8 mac_addr[6];
	u8 reserve[2];
};

struct qedi_rx_bd {
	u32 rx_pkt_index;
	u32 rx_pkt_len;
	u16 vlan_id;
};

#define QEDI_RX_DESC_CNT	(QEDI_PAGE_SIZE / sizeof(struct qedi_rx_bd))
#define QEDI_MAX_RX_DESC_CNT	(QEDI_RX_DESC_CNT - 1)
#define QEDI_NUM_RX_BD		(QEDI_RX_DESC_CNT * 1)
#define QEDI_MAX_RX_BD		(QEDI_NUM_RX_BD - 1)

#define QEDI_NEXT_RX_IDX(x)	((((x) & (QEDI_MAX_RX_DESC_CNT)) ==	\
				  (QEDI_MAX_RX_DESC_CNT - 1)) ?		\
				 (x) + 2 : (x) + 1)

struct qedi_uio_dev {
	struct uio_info		qedi_uinfo;
	u32			uio_dev;
	struct list_head	list;

	u32			ll2_ring_size;
	void			*ll2_ring;

	u32			ll2_buf_size;
	void			*ll2_buf;

	void			*rx_pkt;
	void			*tx_pkt;

	struct qedi_ctx		*qedi;
	struct pci_dev		*pdev;
	void			*uctrl;
};

/* List to maintain the skb pointers */
struct skb_work_list {
	struct list_head list;
	struct sk_buff *skb;
	u16 vlan_id;
};

/* Queue sizes in number of elements */
#define QEDI_SQ_SIZE		MAX_OUTSTANDING_TASKS_PER_CON
#define QEDI_CQ_SIZE		2048
#define QEDI_CMDQ_SIZE		QEDI_MAX_ISCSI_TASK
#define QEDI_PROTO_CQ_PROD_IDX	0

struct qedi_glbl_q_params {
	u64 hw_p_cq;	/* Completion queue PBL */
	u64 hw_p_rq;	/* Request queue PBL */
	u64 hw_p_cmdq;	/* Command queue PBL */
};

struct global_queue {
	union iscsi_cqe *cq;
	dma_addr_t cq_dma;
	u32 cq_mem_size;
	u32 cq_cons_idx; /* Completion queue consumer index */

	void *cq_pbl;
	dma_addr_t cq_pbl_dma;
	u32 cq_pbl_size;

};

struct qedi_fastpath {
	struct qed_sb_info	*sb_info;
	u16			sb_id;
#define QEDI_NAME_SIZE		16
	char			name[QEDI_NAME_SIZE];
	struct qedi_ctx         *qedi;
};

/* Used to pass fastpath information needed to process CQEs */
struct qedi_io_work {
	struct list_head list;
	struct iscsi_cqe_solicited cqe;
	u16	que_idx;
};

/**
 * struct iscsi_cid_queue - Per adapter iscsi cid queue
 *
 * @cid_que_base:           queue base memory
 * @cid_que:                queue memory pointer
 * @cid_q_prod_idx:         produce index
 * @cid_q_cons_idx:         consumer index
 * @cid_q_max_idx:          max index. used to detect wrap around condition
 * @cid_free_cnt:           queue size
 * @conn_cid_tbl:           iscsi cid to conn structure mapping table
 *
 * Per adapter iSCSI CID Queue
 */
struct iscsi_cid_queue {
	void *cid_que_base;
	u32 *cid_que;
	u32 cid_q_prod_idx;
	u32 cid_q_cons_idx;
	u32 cid_q_max_idx;
	u32 cid_free_cnt;
	struct qedi_conn **conn_cid_tbl;
};

struct qedi_portid_tbl {
	spinlock_t      lock;	/* Port id lock */
	u16             start;
	u16             max;
	u16             next;
	unsigned long   *table;
};

struct qedi_itt_map {
	__le32	itt;
	struct qedi_cmd *p_cmd;
};

/* I/O tracing entry */
#define QEDI_IO_TRACE_SIZE             2048
struct qedi_io_log {
#define QEDI_IO_TRACE_REQ              0
#define QEDI_IO_TRACE_RSP              1
	u8 direction;
	u16 task_id;
	u32 cid;
	u32 port_id;	/* Remote port fabric ID */
	int lun;
	u8 op;		/* SCSI CDB */
	u8 lba[4];
	unsigned int bufflen;	/* SCSI buffer length */
	unsigned int sg_count;	/* Number of SG elements */
	u8 fast_sgs;		/* number of fast sgls */
	u8 slow_sgs;		/* number of slow sgls */
	u8 cached_sgs;		/* number of cached sgls */
	int result;		/* Result passed back to mid-layer */
	unsigned long jiffies;	/* Time stamp when I/O logged */
	int refcount;		/* Reference count for task id */
	unsigned int blk_req_cpu; /* CPU that the task is queued on by
				   * blk layer
				   */
	unsigned int req_cpu;	/* CPU that the task is queued on */
	unsigned int intr_cpu;	/* Interrupt CPU that the task is received on */
	unsigned int blk_rsp_cpu;/* CPU that task is actually processed and
				  * returned to blk layer
				  */
	bool cached_sge;
	bool slow_sge;
	bool fast_sge;
};

/* Number of entries in BDQ */
#define QEDI_BDQ_NUM		256
#define QEDI_BDQ_BUF_SIZE	256

/* DMA coherent buffers for BDQ */
struct qedi_bdq_buf {
	void *buf_addr;
	dma_addr_t buf_dma;
};

/* Main port level struct */
struct qedi_ctx {
	struct qedi_dbg_ctx dbg_ctx;
	struct Scsi_Host *shost;
	struct pci_dev *pdev;
	struct qed_dev *cdev;
	struct qed_dev_iscsi_info dev_info;
	struct qed_int_info int_info;
	struct qedi_glbl_q_params *p_cpuq;
	struct global_queue **global_queues;
	/* uio declaration */
	struct qedi_uio_dev *udev;
	struct list_head ll2_skb_list;
	spinlock_t ll2_lock;	/* Light L2 lock */
	spinlock_t hba_lock;	/* per port lock */
	struct task_struct *ll2_recv_thread;
	unsigned long flags;
#define UIO_DEV_OPENED		1
#define QEDI_IOTHREAD_WAKE	2
#define QEDI_IN_RECOVERY	5
#define QEDI_IN_OFFLINE		6

	u8 mac[ETH_ALEN];
	u32 src_ip[4];
	u8 ip_type;

	/* Physical address of above array */
	dma_addr_t hw_p_cpuq;

	struct qedi_bdq_buf bdq[QEDI_BDQ_NUM];
	void *bdq_pbl;
	dma_addr_t bdq_pbl_dma;
	size_t bdq_pbl_mem_size;
	void *bdq_pbl_list;
	dma_addr_t bdq_pbl_list_dma;
	u8 bdq_pbl_list_num_entries;
	struct qedi_nvm_iscsi_image *iscsi_image;
	dma_addr_t nvm_buf_dma;
	void __iomem *bdq_primary_prod;
	void __iomem *bdq_secondary_prod;
	u16 bdq_prod_idx;
	u16 rq_num_entries;

	u32 max_sqes;
	u8 num_queues;
	u32 max_active_conns;

	struct iscsi_cid_queue cid_que;
	struct qedi_endpoint **ep_tbl;
	struct qedi_portid_tbl lcl_port_tbl;

	/* Rx fast path intr context */
	struct qed_sb_info	*sb_array;
	struct qedi_fastpath	*fp_array;
	struct qed_iscsi_tid	tasks;

#define QEDI_LINK_DOWN		0
#define QEDI_LINK_UP		1
	atomic_t link_state;

#define QEDI_RESERVE_TASK_ID	0
#define MAX_ISCSI_TASK_ENTRIES	4096
#define QEDI_INVALID_TASK_ID	(MAX_ISCSI_TASK_ENTRIES + 1)
	unsigned long task_idx_map[MAX_ISCSI_TASK_ENTRIES / BITS_PER_LONG];
	struct qedi_itt_map *itt_map;
	u16 tid_reuse_count[QEDI_MAX_ISCSI_TASK];
	struct qed_pf_params pf_params;

	struct workqueue_struct *tmf_thread;
	struct workqueue_struct *offload_thread;

	u16 ll2_mtu;

	struct workqueue_struct *dpc_wq;

	spinlock_t task_idx_lock;	/* To protect gbl context */
	s32 last_tidx_alloc;
	s32 last_tidx_clear;

	struct qedi_io_log io_trace_buf[QEDI_IO_TRACE_SIZE];
	spinlock_t io_trace_lock;	/* prtect trace Log buf */
	u16 io_trace_idx;
	unsigned int intr_cpu;
	u32 cached_sgls;
	bool use_cached_sge;
	u32 slow_sgls;
	bool use_slow_sge;
	u32 fast_sgls;
	bool use_fast_sge;

	atomic_t num_offloads;
#define SYSFS_FLAG_FW_SEL_BOOT 2
#define IPV6_LEN	41
#define IPV4_LEN	17
	struct iscsi_boot_kset *boot_kset;

	/* Used for iscsi statistics */
	struct mutex stats_lock;
};

struct qedi_work {
	struct list_head list;
	struct qedi_ctx *qedi;
	union iscsi_cqe cqe;
	u16     que_idx;
	bool is_solicited;
};

struct qedi_percpu_s {
	struct task_struct *iothread;
	struct list_head work_list;
	spinlock_t p_work_lock;		/* Per cpu worker lock */
};

static inline void *qedi_get_task_mem(struct qed_iscsi_tid *info, u32 tid)
{
	return (info->blocks[tid / info->num_tids_per_block] +
		(tid % info->num_tids_per_block) * info->size);
}

#define QEDI_U64_HI(val) ((u32)(((u64)(val)) >> 32))
#define QEDI_U64_LO(val) ((u32)(((u64)(val)) & 0xffffffff))

#endif /* _QEDI_H_ */
