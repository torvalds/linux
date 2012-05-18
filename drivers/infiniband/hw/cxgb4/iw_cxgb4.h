/*
 * Copyright (c) 2009-2010 Chelsio, Inc. All rights reserved.
 *
 * This software is available to you under a choice of one of two
 * licenses.  You may choose to be licensed under the terms of the GNU
 * General Public License (GPL) Version 2, available from the file
 * COPYING in the main directory of this source tree, or the
 * OpenIB.org BSD license below:
 *
 *     Redistribution and use in source and binary forms, with or
 *     without modification, are permitted provided that the following
 *     conditions are met:
 *
 *      - Redistributions of source code must retain the above
 *	  copyright notice, this list of conditions and the following
 *	  disclaimer.
 *      - Redistributions in binary form must reproduce the above
 *	  copyright notice, this list of conditions and the following
 *	  disclaimer in the documentation and/or other materials
 *	  provided with the distribution.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
 * BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
 * ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */
#ifndef __IW_CXGB4_H__
#define __IW_CXGB4_H__

#include <linux/mutex.h>
#include <linux/list.h>
#include <linux/spinlock.h>
#include <linux/idr.h>
#include <linux/completion.h>
#include <linux/netdevice.h>
#include <linux/sched.h>
#include <linux/pci.h>
#include <linux/dma-mapping.h>
#include <linux/inet.h>
#include <linux/wait.h>
#include <linux/kref.h>
#include <linux/timer.h>
#include <linux/io.h>
#include <linux/kfifo.h>

#include <asm/byteorder.h>

#include <net/net_namespace.h>

#include <rdma/ib_verbs.h>
#include <rdma/iw_cm.h>

#include "cxgb4.h"
#include "cxgb4_uld.h"
#include "l2t.h"
#include "user.h"

#define DRV_NAME "iw_cxgb4"
#define MOD DRV_NAME ":"

extern int c4iw_debug;
#define PDBG(fmt, args...) \
do { \
	if (c4iw_debug) \
		printk(MOD fmt, ## args); \
} while (0)

#include "t4.h"

#define PBL_OFF(rdev_p, a) ((a) - (rdev_p)->lldi.vr->pbl.start)
#define RQT_OFF(rdev_p, a) ((a) - (rdev_p)->lldi.vr->rq.start)

static inline void *cplhdr(struct sk_buff *skb)
{
	return skb->data;
}

struct c4iw_resource {
	struct kfifo tpt_fifo;
	spinlock_t tpt_fifo_lock;
	struct kfifo qid_fifo;
	spinlock_t qid_fifo_lock;
	struct kfifo pdid_fifo;
	spinlock_t pdid_fifo_lock;
};

struct c4iw_qid_list {
	struct list_head entry;
	u32 qid;
};

struct c4iw_dev_ucontext {
	struct list_head qpids;
	struct list_head cqids;
	struct mutex lock;
};

enum c4iw_rdev_flags {
	T4_FATAL_ERROR = (1<<0),
};

struct c4iw_stat {
	u64 total;
	u64 cur;
	u64 max;
};

struct c4iw_stats {
	struct mutex lock;
	struct c4iw_stat qid;
	struct c4iw_stat pd;
	struct c4iw_stat stag;
	struct c4iw_stat pbl;
	struct c4iw_stat rqt;
	struct c4iw_stat ocqp;
	u64  db_full;
	u64  db_empty;
	u64  db_drop;
	u64  db_state_transitions;
};

struct c4iw_rdev {
	struct c4iw_resource resource;
	unsigned long qpshift;
	u32 qpmask;
	unsigned long cqshift;
	u32 cqmask;
	struct c4iw_dev_ucontext uctx;
	struct gen_pool *pbl_pool;
	struct gen_pool *rqt_pool;
	struct gen_pool *ocqp_pool;
	u32 flags;
	struct cxgb4_lld_info lldi;
	unsigned long oc_mw_pa;
	void __iomem *oc_mw_kva;
	struct c4iw_stats stats;
};

static inline int c4iw_fatal_error(struct c4iw_rdev *rdev)
{
	return rdev->flags & T4_FATAL_ERROR;
}

static inline int c4iw_num_stags(struct c4iw_rdev *rdev)
{
	return min((int)T4_MAX_NUM_STAG, (int)(rdev->lldi.vr->stag.size >> 5));
}

#define C4IW_WR_TO (10*HZ)

struct c4iw_wr_wait {
	struct completion completion;
	int ret;
};

static inline void c4iw_init_wr_wait(struct c4iw_wr_wait *wr_waitp)
{
	wr_waitp->ret = 0;
	init_completion(&wr_waitp->completion);
}

static inline void c4iw_wake_up(struct c4iw_wr_wait *wr_waitp, int ret)
{
	wr_waitp->ret = ret;
	complete(&wr_waitp->completion);
}

static inline int c4iw_wait_for_reply(struct c4iw_rdev *rdev,
				 struct c4iw_wr_wait *wr_waitp,
				 u32 hwtid, u32 qpid,
				 const char *func)
{
	unsigned to = C4IW_WR_TO;
	int ret;

	do {
		ret = wait_for_completion_timeout(&wr_waitp->completion, to);
		if (!ret) {
			printk(KERN_ERR MOD "%s - Device %s not responding - "
			       "tid %u qpid %u\n", func,
			       pci_name(rdev->lldi.pdev), hwtid, qpid);
			if (c4iw_fatal_error(rdev)) {
				wr_waitp->ret = -EIO;
				break;
			}
			to = to << 2;
		}
	} while (!ret);
	if (wr_waitp->ret)
		PDBG("%s: FW reply %d tid %u qpid %u\n",
		     pci_name(rdev->lldi.pdev), wr_waitp->ret, hwtid, qpid);
	return wr_waitp->ret;
}

enum db_state {
	NORMAL = 0,
	FLOW_CONTROL = 1,
	RECOVERY = 2
};

struct c4iw_dev {
	struct ib_device ibdev;
	struct c4iw_rdev rdev;
	u32 device_cap_flags;
	struct idr cqidr;
	struct idr qpidr;
	struct idr mmidr;
	spinlock_t lock;
	struct mutex db_mutex;
	struct dentry *debugfs_root;
	enum db_state db_state;
	int qpcnt;
};

static inline struct c4iw_dev *to_c4iw_dev(struct ib_device *ibdev)
{
	return container_of(ibdev, struct c4iw_dev, ibdev);
}

static inline struct c4iw_dev *rdev_to_c4iw_dev(struct c4iw_rdev *rdev)
{
	return container_of(rdev, struct c4iw_dev, rdev);
}

static inline struct c4iw_cq *get_chp(struct c4iw_dev *rhp, u32 cqid)
{
	return idr_find(&rhp->cqidr, cqid);
}

static inline struct c4iw_qp *get_qhp(struct c4iw_dev *rhp, u32 qpid)
{
	return idr_find(&rhp->qpidr, qpid);
}

static inline struct c4iw_mr *get_mhp(struct c4iw_dev *rhp, u32 mmid)
{
	return idr_find(&rhp->mmidr, mmid);
}

static inline int _insert_handle(struct c4iw_dev *rhp, struct idr *idr,
				 void *handle, u32 id, int lock)
{
	int ret;
	int newid;

	do {
		if (!idr_pre_get(idr, lock ? GFP_KERNEL : GFP_ATOMIC))
			return -ENOMEM;
		if (lock)
			spin_lock_irq(&rhp->lock);
		ret = idr_get_new_above(idr, handle, id, &newid);
		BUG_ON(newid != id);
		if (lock)
			spin_unlock_irq(&rhp->lock);
	} while (ret == -EAGAIN);

	return ret;
}

static inline int insert_handle(struct c4iw_dev *rhp, struct idr *idr,
				void *handle, u32 id)
{
	return _insert_handle(rhp, idr, handle, id, 1);
}

static inline int insert_handle_nolock(struct c4iw_dev *rhp, struct idr *idr,
				       void *handle, u32 id)
{
	return _insert_handle(rhp, idr, handle, id, 0);
}

static inline void _remove_handle(struct c4iw_dev *rhp, struct idr *idr,
				   u32 id, int lock)
{
	if (lock)
		spin_lock_irq(&rhp->lock);
	idr_remove(idr, id);
	if (lock)
		spin_unlock_irq(&rhp->lock);
}

static inline void remove_handle(struct c4iw_dev *rhp, struct idr *idr, u32 id)
{
	_remove_handle(rhp, idr, id, 1);
}

static inline void remove_handle_nolock(struct c4iw_dev *rhp,
					 struct idr *idr, u32 id)
{
	_remove_handle(rhp, idr, id, 0);
}

struct c4iw_pd {
	struct ib_pd ibpd;
	u32 pdid;
	struct c4iw_dev *rhp;
};

static inline struct c4iw_pd *to_c4iw_pd(struct ib_pd *ibpd)
{
	return container_of(ibpd, struct c4iw_pd, ibpd);
}

struct tpt_attributes {
	u64 len;
	u64 va_fbo;
	enum fw_ri_mem_perms perms;
	u32 stag;
	u32 pdid;
	u32 qpid;
	u32 pbl_addr;
	u32 pbl_size;
	u32 state:1;
	u32 type:2;
	u32 rsvd:1;
	u32 remote_invaliate_disable:1;
	u32 zbva:1;
	u32 mw_bind_enable:1;
	u32 page_size:5;
};

struct c4iw_mr {
	struct ib_mr ibmr;
	struct ib_umem *umem;
	struct c4iw_dev *rhp;
	u64 kva;
	struct tpt_attributes attr;
};

static inline struct c4iw_mr *to_c4iw_mr(struct ib_mr *ibmr)
{
	return container_of(ibmr, struct c4iw_mr, ibmr);
}

struct c4iw_mw {
	struct ib_mw ibmw;
	struct c4iw_dev *rhp;
	u64 kva;
	struct tpt_attributes attr;
};

static inline struct c4iw_mw *to_c4iw_mw(struct ib_mw *ibmw)
{
	return container_of(ibmw, struct c4iw_mw, ibmw);
}

struct c4iw_fr_page_list {
	struct ib_fast_reg_page_list ibpl;
	DEFINE_DMA_UNMAP_ADDR(mapping);
	dma_addr_t dma_addr;
	struct c4iw_dev *dev;
	int size;
};

static inline struct c4iw_fr_page_list *to_c4iw_fr_page_list(
					struct ib_fast_reg_page_list *ibpl)
{
	return container_of(ibpl, struct c4iw_fr_page_list, ibpl);
}

struct c4iw_cq {
	struct ib_cq ibcq;
	struct c4iw_dev *rhp;
	struct t4_cq cq;
	spinlock_t lock;
	spinlock_t comp_handler_lock;
	atomic_t refcnt;
	wait_queue_head_t wait;
};

static inline struct c4iw_cq *to_c4iw_cq(struct ib_cq *ibcq)
{
	return container_of(ibcq, struct c4iw_cq, ibcq);
}

struct c4iw_mpa_attributes {
	u8 initiator;
	u8 recv_marker_enabled;
	u8 xmit_marker_enabled;
	u8 crc_enabled;
	u8 enhanced_rdma_conn;
	u8 version;
	u8 p2p_type;
};

struct c4iw_qp_attributes {
	u32 scq;
	u32 rcq;
	u32 sq_num_entries;
	u32 rq_num_entries;
	u32 sq_max_sges;
	u32 sq_max_sges_rdma_write;
	u32 rq_max_sges;
	u32 state;
	u8 enable_rdma_read;
	u8 enable_rdma_write;
	u8 enable_bind;
	u8 enable_mmid0_fastreg;
	u32 max_ord;
	u32 max_ird;
	u32 pd;
	u32 next_state;
	char terminate_buffer[52];
	u32 terminate_msg_len;
	u8 is_terminate_local;
	struct c4iw_mpa_attributes mpa_attr;
	struct c4iw_ep *llp_stream_handle;
	u8 layer_etype;
	u8 ecode;
	u16 sq_db_inc;
	u16 rq_db_inc;
};

struct c4iw_qp {
	struct ib_qp ibqp;
	struct c4iw_dev *rhp;
	struct c4iw_ep *ep;
	struct c4iw_qp_attributes attr;
	struct t4_wq wq;
	spinlock_t lock;
	struct mutex mutex;
	atomic_t refcnt;
	wait_queue_head_t wait;
	struct timer_list timer;
};

static inline struct c4iw_qp *to_c4iw_qp(struct ib_qp *ibqp)
{
	return container_of(ibqp, struct c4iw_qp, ibqp);
}

struct c4iw_ucontext {
	struct ib_ucontext ibucontext;
	struct c4iw_dev_ucontext uctx;
	u32 key;
	spinlock_t mmap_lock;
	struct list_head mmaps;
};

static inline struct c4iw_ucontext *to_c4iw_ucontext(struct ib_ucontext *c)
{
	return container_of(c, struct c4iw_ucontext, ibucontext);
}

struct c4iw_mm_entry {
	struct list_head entry;
	u64 addr;
	u32 key;
	unsigned len;
};

static inline struct c4iw_mm_entry *remove_mmap(struct c4iw_ucontext *ucontext,
						u32 key, unsigned len)
{
	struct list_head *pos, *nxt;
	struct c4iw_mm_entry *mm;

	spin_lock(&ucontext->mmap_lock);
	list_for_each_safe(pos, nxt, &ucontext->mmaps) {

		mm = list_entry(pos, struct c4iw_mm_entry, entry);
		if (mm->key == key && mm->len == len) {
			list_del_init(&mm->entry);
			spin_unlock(&ucontext->mmap_lock);
			PDBG("%s key 0x%x addr 0x%llx len %d\n", __func__,
			     key, (unsigned long long) mm->addr, mm->len);
			return mm;
		}
	}
	spin_unlock(&ucontext->mmap_lock);
	return NULL;
}

static inline void insert_mmap(struct c4iw_ucontext *ucontext,
			       struct c4iw_mm_entry *mm)
{
	spin_lock(&ucontext->mmap_lock);
	PDBG("%s key 0x%x addr 0x%llx len %d\n", __func__,
	     mm->key, (unsigned long long) mm->addr, mm->len);
	list_add_tail(&mm->entry, &ucontext->mmaps);
	spin_unlock(&ucontext->mmap_lock);
}

enum c4iw_qp_attr_mask {
	C4IW_QP_ATTR_NEXT_STATE = 1 << 0,
	C4IW_QP_ATTR_SQ_DB = 1<<1,
	C4IW_QP_ATTR_RQ_DB = 1<<2,
	C4IW_QP_ATTR_ENABLE_RDMA_READ = 1 << 7,
	C4IW_QP_ATTR_ENABLE_RDMA_WRITE = 1 << 8,
	C4IW_QP_ATTR_ENABLE_RDMA_BIND = 1 << 9,
	C4IW_QP_ATTR_MAX_ORD = 1 << 11,
	C4IW_QP_ATTR_MAX_IRD = 1 << 12,
	C4IW_QP_ATTR_LLP_STREAM_HANDLE = 1 << 22,
	C4IW_QP_ATTR_STREAM_MSG_BUFFER = 1 << 23,
	C4IW_QP_ATTR_MPA_ATTR = 1 << 24,
	C4IW_QP_ATTR_QP_CONTEXT_ACTIVATE = 1 << 25,
	C4IW_QP_ATTR_VALID_MODIFY = (C4IW_QP_ATTR_ENABLE_RDMA_READ |
				     C4IW_QP_ATTR_ENABLE_RDMA_WRITE |
				     C4IW_QP_ATTR_MAX_ORD |
				     C4IW_QP_ATTR_MAX_IRD |
				     C4IW_QP_ATTR_LLP_STREAM_HANDLE |
				     C4IW_QP_ATTR_STREAM_MSG_BUFFER |
				     C4IW_QP_ATTR_MPA_ATTR |
				     C4IW_QP_ATTR_QP_CONTEXT_ACTIVATE)
};

int c4iw_modify_qp(struct c4iw_dev *rhp,
				struct c4iw_qp *qhp,
				enum c4iw_qp_attr_mask mask,
				struct c4iw_qp_attributes *attrs,
				int internal);

enum c4iw_qp_state {
	C4IW_QP_STATE_IDLE,
	C4IW_QP_STATE_RTS,
	C4IW_QP_STATE_ERROR,
	C4IW_QP_STATE_TERMINATE,
	C4IW_QP_STATE_CLOSING,
	C4IW_QP_STATE_TOT
};

static inline int c4iw_convert_state(enum ib_qp_state ib_state)
{
	switch (ib_state) {
	case IB_QPS_RESET:
	case IB_QPS_INIT:
		return C4IW_QP_STATE_IDLE;
	case IB_QPS_RTS:
		return C4IW_QP_STATE_RTS;
	case IB_QPS_SQD:
		return C4IW_QP_STATE_CLOSING;
	case IB_QPS_SQE:
		return C4IW_QP_STATE_TERMINATE;
	case IB_QPS_ERR:
		return C4IW_QP_STATE_ERROR;
	default:
		return -1;
	}
}

static inline u32 c4iw_ib_to_tpt_access(int a)
{
	return (a & IB_ACCESS_REMOTE_WRITE ? FW_RI_MEM_ACCESS_REM_WRITE : 0) |
	       (a & IB_ACCESS_REMOTE_READ ? FW_RI_MEM_ACCESS_REM_READ : 0) |
	       (a & IB_ACCESS_LOCAL_WRITE ? FW_RI_MEM_ACCESS_LOCAL_WRITE : 0) |
	       FW_RI_MEM_ACCESS_LOCAL_READ;
}

static inline u32 c4iw_ib_to_tpt_bind_access(int acc)
{
	return (acc & IB_ACCESS_REMOTE_WRITE ? FW_RI_MEM_ACCESS_REM_WRITE : 0) |
	       (acc & IB_ACCESS_REMOTE_READ ? FW_RI_MEM_ACCESS_REM_READ : 0);
}

enum c4iw_mmid_state {
	C4IW_STAG_STATE_VALID,
	C4IW_STAG_STATE_INVALID
};

#define C4IW_NODE_DESC "cxgb4 Chelsio Communications"

#define MPA_KEY_REQ "MPA ID Req Frame"
#define MPA_KEY_REP "MPA ID Rep Frame"

#define MPA_MAX_PRIVATE_DATA	256
#define MPA_ENHANCED_RDMA_CONN	0x10
#define MPA_REJECT		0x20
#define MPA_CRC			0x40
#define MPA_MARKERS		0x80
#define MPA_FLAGS_MASK		0xE0

#define MPA_V2_PEER2PEER_MODEL          0x8000
#define MPA_V2_ZERO_LEN_FPDU_RTR        0x4000
#define MPA_V2_RDMA_WRITE_RTR           0x8000
#define MPA_V2_RDMA_READ_RTR            0x4000
#define MPA_V2_IRD_ORD_MASK             0x3FFF

#define c4iw_put_ep(ep) { \
	PDBG("put_ep (via %s:%u) ep %p refcnt %d\n", __func__, __LINE__,  \
	     ep, atomic_read(&((ep)->kref.refcount))); \
	WARN_ON(atomic_read(&((ep)->kref.refcount)) < 1); \
	kref_put(&((ep)->kref), _c4iw_free_ep); \
}

#define c4iw_get_ep(ep) { \
	PDBG("get_ep (via %s:%u) ep %p, refcnt %d\n", __func__, __LINE__, \
	     ep, atomic_read(&((ep)->kref.refcount))); \
	kref_get(&((ep)->kref));  \
}
void _c4iw_free_ep(struct kref *kref);

struct mpa_message {
	u8 key[16];
	u8 flags;
	u8 revision;
	__be16 private_data_size;
	u8 private_data[0];
};

struct mpa_v2_conn_params {
	__be16 ird;
	__be16 ord;
};

struct terminate_message {
	u8 layer_etype;
	u8 ecode;
	__be16 hdrct_rsvd;
	u8 len_hdrs[0];
};

#define TERM_MAX_LENGTH (sizeof(struct terminate_message) + 2 + 18 + 28)

enum c4iw_layers_types {
	LAYER_RDMAP		= 0x00,
	LAYER_DDP		= 0x10,
	LAYER_MPA		= 0x20,
	RDMAP_LOCAL_CATA	= 0x00,
	RDMAP_REMOTE_PROT	= 0x01,
	RDMAP_REMOTE_OP		= 0x02,
	DDP_LOCAL_CATA		= 0x00,
	DDP_TAGGED_ERR		= 0x01,
	DDP_UNTAGGED_ERR	= 0x02,
	DDP_LLP			= 0x03
};

enum c4iw_rdma_ecodes {
	RDMAP_INV_STAG		= 0x00,
	RDMAP_BASE_BOUNDS	= 0x01,
	RDMAP_ACC_VIOL		= 0x02,
	RDMAP_STAG_NOT_ASSOC	= 0x03,
	RDMAP_TO_WRAP		= 0x04,
	RDMAP_INV_VERS		= 0x05,
	RDMAP_INV_OPCODE	= 0x06,
	RDMAP_STREAM_CATA	= 0x07,
	RDMAP_GLOBAL_CATA	= 0x08,
	RDMAP_CANT_INV_STAG	= 0x09,
	RDMAP_UNSPECIFIED	= 0xff
};

enum c4iw_ddp_ecodes {
	DDPT_INV_STAG		= 0x00,
	DDPT_BASE_BOUNDS	= 0x01,
	DDPT_STAG_NOT_ASSOC	= 0x02,
	DDPT_TO_WRAP		= 0x03,
	DDPT_INV_VERS		= 0x04,
	DDPU_INV_QN		= 0x01,
	DDPU_INV_MSN_NOBUF	= 0x02,
	DDPU_INV_MSN_RANGE	= 0x03,
	DDPU_INV_MO		= 0x04,
	DDPU_MSG_TOOBIG		= 0x05,
	DDPU_INV_VERS		= 0x06
};

enum c4iw_mpa_ecodes {
	MPA_CRC_ERR		= 0x02,
	MPA_MARKER_ERR          = 0x03,
	MPA_LOCAL_CATA          = 0x05,
	MPA_INSUFF_IRD          = 0x06,
	MPA_NOMATCH_RTR         = 0x07,
};

enum c4iw_ep_state {
	IDLE = 0,
	LISTEN,
	CONNECTING,
	MPA_REQ_WAIT,
	MPA_REQ_SENT,
	MPA_REQ_RCVD,
	MPA_REP_SENT,
	FPDU_MODE,
	ABORTING,
	CLOSING,
	MORIBUND,
	DEAD,
};

enum c4iw_ep_flags {
	PEER_ABORT_IN_PROGRESS	= 0,
	ABORT_REQ_IN_PROGRESS	= 1,
	RELEASE_RESOURCES	= 2,
	CLOSE_SENT		= 3,
};

struct c4iw_ep_common {
	struct iw_cm_id *cm_id;
	struct c4iw_qp *qp;
	struct c4iw_dev *dev;
	enum c4iw_ep_state state;
	struct kref kref;
	struct mutex mutex;
	struct sockaddr_in local_addr;
	struct sockaddr_in remote_addr;
	struct c4iw_wr_wait wr_wait;
	unsigned long flags;
};

struct c4iw_listen_ep {
	struct c4iw_ep_common com;
	unsigned int stid;
	int backlog;
};

struct c4iw_ep {
	struct c4iw_ep_common com;
	struct c4iw_ep *parent_ep;
	struct timer_list timer;
	struct list_head entry;
	unsigned int atid;
	u32 hwtid;
	u32 snd_seq;
	u32 rcv_seq;
	struct l2t_entry *l2t;
	struct dst_entry *dst;
	struct sk_buff *mpa_skb;
	struct c4iw_mpa_attributes mpa_attr;
	u8 mpa_pkt[sizeof(struct mpa_message) + MPA_MAX_PRIVATE_DATA];
	unsigned int mpa_pkt_len;
	u32 ird;
	u32 ord;
	u32 smac_idx;
	u32 tx_chan;
	u32 mtu;
	u16 mss;
	u16 emss;
	u16 plen;
	u16 rss_qid;
	u16 txq_idx;
	u16 ctrlq_idx;
	u8 tos;
	u8 retry_with_mpa_v1;
	u8 tried_with_mpa_v1;
};

static inline struct c4iw_ep *to_ep(struct iw_cm_id *cm_id)
{
	return cm_id->provider_data;
}

static inline struct c4iw_listen_ep *to_listen_ep(struct iw_cm_id *cm_id)
{
	return cm_id->provider_data;
}

static inline int compute_wscale(int win)
{
	int wscale = 0;

	while (wscale < 14 && (65535<<wscale) < win)
		wscale++;
	return wscale;
}

typedef int (*c4iw_handler_func)(struct c4iw_dev *dev, struct sk_buff *skb);

int c4iw_ep_redirect(void *ctx, struct dst_entry *old, struct dst_entry *new,
		     struct l2t_entry *l2t);
void c4iw_put_qpid(struct c4iw_rdev *rdev, u32 qpid,
		   struct c4iw_dev_ucontext *uctx);
u32 c4iw_get_resource(struct kfifo *fifo, spinlock_t *lock);
void c4iw_put_resource(struct kfifo *fifo, u32 entry, spinlock_t *lock);
int c4iw_init_resource(struct c4iw_rdev *rdev, u32 nr_tpt, u32 nr_pdid);
int c4iw_init_ctrl_qp(struct c4iw_rdev *rdev);
int c4iw_pblpool_create(struct c4iw_rdev *rdev);
int c4iw_rqtpool_create(struct c4iw_rdev *rdev);
int c4iw_ocqp_pool_create(struct c4iw_rdev *rdev);
void c4iw_pblpool_destroy(struct c4iw_rdev *rdev);
void c4iw_rqtpool_destroy(struct c4iw_rdev *rdev);
void c4iw_ocqp_pool_destroy(struct c4iw_rdev *rdev);
void c4iw_destroy_resource(struct c4iw_resource *rscp);
int c4iw_destroy_ctrl_qp(struct c4iw_rdev *rdev);
int c4iw_register_device(struct c4iw_dev *dev);
void c4iw_unregister_device(struct c4iw_dev *dev);
int __init c4iw_cm_init(void);
void __exit c4iw_cm_term(void);
void c4iw_release_dev_ucontext(struct c4iw_rdev *rdev,
			       struct c4iw_dev_ucontext *uctx);
void c4iw_init_dev_ucontext(struct c4iw_rdev *rdev,
			    struct c4iw_dev_ucontext *uctx);
int c4iw_poll_cq(struct ib_cq *ibcq, int num_entries, struct ib_wc *wc);
int c4iw_post_send(struct ib_qp *ibqp, struct ib_send_wr *wr,
		      struct ib_send_wr **bad_wr);
int c4iw_post_receive(struct ib_qp *ibqp, struct ib_recv_wr *wr,
		      struct ib_recv_wr **bad_wr);
int c4iw_bind_mw(struct ib_qp *qp, struct ib_mw *mw,
		 struct ib_mw_bind *mw_bind);
int c4iw_connect(struct iw_cm_id *cm_id, struct iw_cm_conn_param *conn_param);
int c4iw_create_listen(struct iw_cm_id *cm_id, int backlog);
int c4iw_destroy_listen(struct iw_cm_id *cm_id);
int c4iw_accept_cr(struct iw_cm_id *cm_id, struct iw_cm_conn_param *conn_param);
int c4iw_reject_cr(struct iw_cm_id *cm_id, const void *pdata, u8 pdata_len);
void c4iw_qp_add_ref(struct ib_qp *qp);
void c4iw_qp_rem_ref(struct ib_qp *qp);
void c4iw_free_fastreg_pbl(struct ib_fast_reg_page_list *page_list);
struct ib_fast_reg_page_list *c4iw_alloc_fastreg_pbl(
					struct ib_device *device,
					int page_list_len);
struct ib_mr *c4iw_alloc_fast_reg_mr(struct ib_pd *pd, int pbl_depth);
int c4iw_dealloc_mw(struct ib_mw *mw);
struct ib_mw *c4iw_alloc_mw(struct ib_pd *pd);
struct ib_mr *c4iw_reg_user_mr(struct ib_pd *pd, u64 start,
					   u64 length, u64 virt, int acc,
					   struct ib_udata *udata);
struct ib_mr *c4iw_get_dma_mr(struct ib_pd *pd, int acc);
struct ib_mr *c4iw_register_phys_mem(struct ib_pd *pd,
					struct ib_phys_buf *buffer_list,
					int num_phys_buf,
					int acc,
					u64 *iova_start);
int c4iw_reregister_phys_mem(struct ib_mr *mr,
				     int mr_rereg_mask,
				     struct ib_pd *pd,
				     struct ib_phys_buf *buffer_list,
				     int num_phys_buf,
				     int acc, u64 *iova_start);
int c4iw_dereg_mr(struct ib_mr *ib_mr);
int c4iw_destroy_cq(struct ib_cq *ib_cq);
struct ib_cq *c4iw_create_cq(struct ib_device *ibdev, int entries,
					int vector,
					struct ib_ucontext *ib_context,
					struct ib_udata *udata);
int c4iw_resize_cq(struct ib_cq *cq, int cqe, struct ib_udata *udata);
int c4iw_arm_cq(struct ib_cq *ibcq, enum ib_cq_notify_flags flags);
int c4iw_destroy_qp(struct ib_qp *ib_qp);
struct ib_qp *c4iw_create_qp(struct ib_pd *pd,
			     struct ib_qp_init_attr *attrs,
			     struct ib_udata *udata);
int c4iw_ib_modify_qp(struct ib_qp *ibqp, struct ib_qp_attr *attr,
				 int attr_mask, struct ib_udata *udata);
struct ib_qp *c4iw_get_qp(struct ib_device *dev, int qpn);
u32 c4iw_rqtpool_alloc(struct c4iw_rdev *rdev, int size);
void c4iw_rqtpool_free(struct c4iw_rdev *rdev, u32 addr, int size);
u32 c4iw_pblpool_alloc(struct c4iw_rdev *rdev, int size);
void c4iw_pblpool_free(struct c4iw_rdev *rdev, u32 addr, int size);
u32 c4iw_ocqp_pool_alloc(struct c4iw_rdev *rdev, int size);
void c4iw_ocqp_pool_free(struct c4iw_rdev *rdev, u32 addr, int size);
int c4iw_ofld_send(struct c4iw_rdev *rdev, struct sk_buff *skb);
void c4iw_flush_hw_cq(struct t4_cq *cq);
void c4iw_count_rcqes(struct t4_cq *cq, struct t4_wq *wq, int *count);
void c4iw_count_scqes(struct t4_cq *cq, struct t4_wq *wq, int *count);
int c4iw_ep_disconnect(struct c4iw_ep *ep, int abrupt, gfp_t gfp);
int c4iw_flush_rq(struct t4_wq *wq, struct t4_cq *cq, int count);
int c4iw_flush_sq(struct t4_wq *wq, struct t4_cq *cq, int count);
int c4iw_ev_handler(struct c4iw_dev *rnicp, u32 qid);
u16 c4iw_rqes_posted(struct c4iw_qp *qhp);
int c4iw_post_terminate(struct c4iw_qp *qhp, struct t4_cqe *err_cqe);
u32 c4iw_get_cqid(struct c4iw_rdev *rdev, struct c4iw_dev_ucontext *uctx);
void c4iw_put_cqid(struct c4iw_rdev *rdev, u32 qid,
		struct c4iw_dev_ucontext *uctx);
u32 c4iw_get_qpid(struct c4iw_rdev *rdev, struct c4iw_dev_ucontext *uctx);
void c4iw_put_qpid(struct c4iw_rdev *rdev, u32 qid,
		struct c4iw_dev_ucontext *uctx);
void c4iw_ev_dispatch(struct c4iw_dev *dev, struct t4_cqe *err_cqe);

extern struct cxgb4_client t4c_client;
extern c4iw_handler_func c4iw_handlers[NUM_CPL_CMDS];
extern int c4iw_max_read_depth;
extern int db_fc_threshold;


#endif
