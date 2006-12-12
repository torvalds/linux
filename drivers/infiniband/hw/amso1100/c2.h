/*
 * Copyright (c) 2005 Ammasso, Inc. All rights reserved.
 * Copyright (c) 2005 Open Grid Computing, Inc. All rights reserved.
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
 *        copyright notice, this list of conditions and the following
 *        disclaimer.
 *
 *      - Redistributions in binary form must reproduce the above
 *        copyright notice, this list of conditions and the following
 *        disclaimer in the documentation and/or other materials
 *        provided with the distribution.
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

#ifndef __C2_H
#define __C2_H

#include <linux/netdevice.h>
#include <linux/spinlock.h>
#include <linux/kernel.h>
#include <linux/pci.h>
#include <linux/dma-mapping.h>
#include <linux/idr.h>
#include <asm/semaphore.h>

#include "c2_provider.h"
#include "c2_mq.h"
#include "c2_status.h"

#define DRV_NAME     "c2"
#define DRV_VERSION  "1.1"
#define PFX          DRV_NAME ": "

#define BAR_0                0
#define BAR_2                2
#define BAR_4                4

#define RX_BUF_SIZE         (1536 + 8)
#define ETH_JUMBO_MTU        9000
#define C2_MAGIC            "CEPHEUS"
#define C2_VERSION           4
#define C2_IVN              (18 & 0x7fffffff)

#define C2_REG0_SIZE        (16 * 1024)
#define C2_REG2_SIZE        (2 * 1024 * 1024)
#define C2_REG4_SIZE        (256 * 1024 * 1024)
#define C2_NUM_TX_DESC       341
#define C2_NUM_RX_DESC       256
#define C2_PCI_REGS_OFFSET  (0x10000)
#define C2_RXP_HRXDQ_OFFSET (((C2_REG4_SIZE)/2))
#define C2_RXP_HRXDQ_SIZE   (4096)
#define C2_TXP_HTXDQ_OFFSET (((C2_REG4_SIZE)/2) + C2_RXP_HRXDQ_SIZE)
#define C2_TXP_HTXDQ_SIZE   (4096)
#define C2_TX_TIMEOUT	    (6*HZ)

/* CEPHEUS */
static const u8 c2_magic[] = {
	0x43, 0x45, 0x50, 0x48, 0x45, 0x55, 0x53
};

enum adapter_pci_regs {
	C2_REGS_MAGIC = 0x0000,
	C2_REGS_VERS = 0x0008,
	C2_REGS_IVN = 0x000C,
	C2_REGS_PCI_WINSIZE = 0x0010,
	C2_REGS_Q0_QSIZE = 0x0014,
	C2_REGS_Q0_MSGSIZE = 0x0018,
	C2_REGS_Q0_POOLSTART = 0x001C,
	C2_REGS_Q0_SHARED = 0x0020,
	C2_REGS_Q1_QSIZE = 0x0024,
	C2_REGS_Q1_MSGSIZE = 0x0028,
	C2_REGS_Q1_SHARED = 0x0030,
	C2_REGS_Q2_QSIZE = 0x0034,
	C2_REGS_Q2_MSGSIZE = 0x0038,
	C2_REGS_Q2_SHARED = 0x0040,
	C2_REGS_ENADDR = 0x004C,
	C2_REGS_RDMA_ENADDR = 0x0054,
	C2_REGS_HRX_CUR = 0x006C,
};

struct c2_adapter_pci_regs {
	char reg_magic[8];
	u32 version;
	u32 ivn;
	u32 pci_window_size;
	u32 q0_q_size;
	u32 q0_msg_size;
	u32 q0_pool_start;
	u32 q0_shared;
	u32 q1_q_size;
	u32 q1_msg_size;
	u32 q1_pool_start;
	u32 q1_shared;
	u32 q2_q_size;
	u32 q2_msg_size;
	u32 q2_pool_start;
	u32 q2_shared;
	u32 log_start;
	u32 log_size;
	u8 host_enaddr[8];
	u8 rdma_enaddr[8];
	u32 crash_entry;
	u32 crash_ready[2];
	u32 fw_txd_cur;
	u32 fw_hrxd_cur;
	u32 fw_rxd_cur;
};

enum pci_regs {
	C2_HISR = 0x0000,
	C2_DISR = 0x0004,
	C2_HIMR = 0x0008,
	C2_DIMR = 0x000C,
	C2_NISR0 = 0x0010,
	C2_NISR1 = 0x0014,
	C2_NIMR0 = 0x0018,
	C2_NIMR1 = 0x001C,
	C2_IDIS = 0x0020,
};

enum {
	C2_PCI_HRX_INT = 1 << 8,
	C2_PCI_HTX_INT = 1 << 17,
	C2_PCI_HRX_QUI = 1 << 31,
};

/*
 * Cepheus registers in BAR0.
 */
struct c2_pci_regs {
	u32 hostisr;
	u32 dmaisr;
	u32 hostimr;
	u32 dmaimr;
	u32 netisr0;
	u32 netisr1;
	u32 netimr0;
	u32 netimr1;
	u32 int_disable;
};

/* TXP flags */
enum c2_txp_flags {
	TXP_HTXD_DONE = 0,
	TXP_HTXD_READY = 1 << 0,
	TXP_HTXD_UNINIT = 1 << 1,
};

/* RXP flags */
enum c2_rxp_flags {
	RXP_HRXD_UNINIT = 0,
	RXP_HRXD_READY = 1 << 0,
	RXP_HRXD_DONE = 1 << 1,
};

/* RXP status */
enum c2_rxp_status {
	RXP_HRXD_ZERO = 0,
	RXP_HRXD_OK = 1 << 0,
	RXP_HRXD_BUF_OV = 1 << 1,
};

/* TXP descriptor fields */
enum txp_desc {
	C2_TXP_FLAGS = 0x0000,
	C2_TXP_LEN = 0x0002,
	C2_TXP_ADDR = 0x0004,
};

/* RXP descriptor fields */
enum rxp_desc {
	C2_RXP_FLAGS = 0x0000,
	C2_RXP_STATUS = 0x0002,
	C2_RXP_COUNT = 0x0004,
	C2_RXP_LEN = 0x0006,
	C2_RXP_ADDR = 0x0008,
};

struct c2_txp_desc {
	u16 flags;
	u16 len;
	u64 addr;
} __attribute__ ((packed));

struct c2_rxp_desc {
	u16 flags;
	u16 status;
	u16 count;
	u16 len;
	u64 addr;
} __attribute__ ((packed));

struct c2_rxp_hdr {
	u16 flags;
	u16 status;
	u16 len;
	u16 rsvd;
} __attribute__ ((packed));

struct c2_tx_desc {
	u32 len;
	u32 status;
	dma_addr_t next_offset;
};

struct c2_rx_desc {
	u32 len;
	u32 status;
	dma_addr_t next_offset;
};

struct c2_alloc {
	u32 last;
	u32 max;
	spinlock_t lock;
	unsigned long *table;
};

struct c2_array {
	struct {
		void **page;
		int used;
	} *page_list;
};

/*
 * The MQ shared pointer pool is organized as a linked list of
 * chunks. Each chunk contains a linked list of free shared pointers
 * that can be allocated to a given user mode client.
 *
 */
struct sp_chunk {
	struct sp_chunk *next;
	dma_addr_t dma_addr;
	DECLARE_PCI_UNMAP_ADDR(mapping);
	u16 head;
	u16 shared_ptr[0];
};

struct c2_pd_table {
	u32 last;
	u32 max;
	spinlock_t lock;
	unsigned long *table;
};

struct c2_qp_table {
	struct idr idr;
	spinlock_t lock;
	int last;
};

struct c2_element {
	struct c2_element *next;
	void *ht_desc;		/* host     descriptor */
	void __iomem *hw_desc;	/* hardware descriptor */
	struct sk_buff *skb;
	dma_addr_t mapaddr;
	u32 maplen;
};

struct c2_ring {
	struct c2_element *to_clean;
	struct c2_element *to_use;
	struct c2_element *start;
	unsigned long count;
};

struct c2_dev {
	struct ib_device ibdev;
	void __iomem *regs;
	void __iomem *mmio_txp_ring; /* remapped adapter memory for hw rings */
	void __iomem *mmio_rxp_ring;
	spinlock_t lock;
	struct pci_dev *pcidev;
	struct net_device *netdev;
	struct net_device *pseudo_netdev;
	unsigned int cur_tx;
	unsigned int cur_rx;
	u32 adapter_handle;
	int device_cap_flags;
	void __iomem *kva;	/* KVA device memory */
	unsigned long pa;	/* PA device memory */
	void **qptr_array;

	struct kmem_cache *host_msg_cache;

	struct list_head cca_link;		/* adapter list */
	struct list_head eh_wakeup_list;	/* event wakeup list */
	wait_queue_head_t req_vq_wo;

	/* Cached RNIC properties */
	struct ib_device_attr props;

	struct c2_pd_table pd_table;
	struct c2_qp_table qp_table;
	int ports;		/* num of GigE ports */
	int devnum;
	spinlock_t vqlock;	/* sync vbs req MQ */

	/* Verbs Queues */
	struct c2_mq req_vq;	/* Verbs Request MQ */
	struct c2_mq rep_vq;	/* Verbs Reply MQ */
	struct c2_mq aeq;	/* Async Events MQ */

	/* Kernel client MQs */
	struct sp_chunk *kern_mqsp_pool;

	/* Device updates these values when posting messages to a host
	 * target queue */
	u16 req_vq_shared;
	u16 rep_vq_shared;
	u16 aeq_shared;
	u16 irq_claimed;

	/*
	 * Shared host target pages for user-accessible MQs.
	 */
	int hthead;		/* index of first free entry */
	void *htpages;		/* kernel vaddr */
	int htlen;		/* length of htpages memory */
	void *htuva;		/* user mapped vaddr */
	spinlock_t htlock;	/* serialize allocation */

	u64 adapter_hint_uva;	/* access to the activity FIFO */

	//	spinlock_t aeq_lock;
	//	spinlock_t rnic_lock;

	u16 *hint_count;
	dma_addr_t hint_count_dma;
	u16 hints_read;

	int init;		/* TRUE if it's ready */
	char ae_cache_name[16];
	char vq_cache_name[16];
};

struct c2_port {
	u32 msg_enable;
	struct c2_dev *c2dev;
	struct net_device *netdev;

	spinlock_t tx_lock;
	u32 tx_avail;
	struct c2_ring tx_ring;
	struct c2_ring rx_ring;

	void *mem;		/* PCI memory for host rings */
	dma_addr_t dma;
	unsigned long mem_size;

	u32 rx_buf_size;

	struct net_device_stats netstats;
};

/*
 * Activity FIFO registers in BAR0.
 */
#define PCI_BAR0_HOST_HINT	0x100
#define PCI_BAR0_ADAPTER_HINT	0x2000

/*
 * Ammasso PCI vendor id and Cepheus PCI device id.
 */
#define CQ_ARMED 	0x01
#define CQ_WAIT_FOR_DMA	0x80

/*
 * The format of a hint is as follows:
 * Lower 16 bits are the count of hints for the queue.
 * Next 15 bits are the qp_index
 * Upper most bit depends on who reads it:
 *    If read by producer, then it means Full (1) or Not-Full (0)
 *    If read by consumer, then it means Empty (1) or Not-Empty (0)
 */
#define C2_HINT_MAKE(q_index, hint_count) (((q_index) << 16) | hint_count)
#define C2_HINT_GET_INDEX(hint) (((hint) & 0x7FFF0000) >> 16)
#define C2_HINT_GET_COUNT(hint) ((hint) & 0x0000FFFF)


/*
 * The following defines the offset in SDRAM for the c2_adapter_pci_regs_t
 * struct.
 */
#define C2_ADAPTER_PCI_REGS_OFFSET 0x10000

#ifndef readq
static inline u64 readq(const void __iomem * addr)
{
	u64 ret = readl(addr + 4);
	ret <<= 32;
	ret |= readl(addr);

	return ret;
}
#endif

#ifndef writeq
static inline void __raw_writeq(u64 val, void __iomem * addr)
{
	__raw_writel((u32) (val), addr);
	__raw_writel((u32) (val >> 32), (addr + 4));
}
#endif

#define C2_SET_CUR_RX(c2dev, cur_rx) \
	__raw_writel(cpu_to_be32(cur_rx), c2dev->mmio_txp_ring + 4092)

#define C2_GET_CUR_RX(c2dev) \
	be32_to_cpu(readl(c2dev->mmio_txp_ring + 4092))

static inline struct c2_dev *to_c2dev(struct ib_device *ibdev)
{
	return container_of(ibdev, struct c2_dev, ibdev);
}

static inline int c2_errno(void *reply)
{
	switch (c2_wr_get_result(reply)) {
	case C2_OK:
		return 0;
	case CCERR_NO_BUFS:
	case CCERR_INSUFFICIENT_RESOURCES:
	case CCERR_ZERO_RDMA_READ_RESOURCES:
		return -ENOMEM;
	case CCERR_MR_IN_USE:
	case CCERR_QP_IN_USE:
		return -EBUSY;
	case CCERR_ADDR_IN_USE:
		return -EADDRINUSE;
	case CCERR_ADDR_NOT_AVAIL:
		return -EADDRNOTAVAIL;
	case CCERR_CONN_RESET:
		return -ECONNRESET;
	case CCERR_NOT_IMPLEMENTED:
	case CCERR_INVALID_WQE:
		return -ENOSYS;
	case CCERR_QP_NOT_PRIVILEGED:
		return -EPERM;
	case CCERR_STACK_ERROR:
		return -EPROTO;
	case CCERR_ACCESS_VIOLATION:
	case CCERR_BASE_AND_BOUNDS_VIOLATION:
		return -EFAULT;
	case CCERR_STAG_STATE_NOT_INVALID:
	case CCERR_INVALID_ADDRESS:
	case CCERR_INVALID_CQ:
	case CCERR_INVALID_EP:
	case CCERR_INVALID_MODIFIER:
	case CCERR_INVALID_MTU:
	case CCERR_INVALID_PD_ID:
	case CCERR_INVALID_QP:
	case CCERR_INVALID_RNIC:
	case CCERR_INVALID_STAG:
		return -EINVAL;
	default:
		return -EAGAIN;
	}
}

/* Device */
extern int c2_register_device(struct c2_dev *c2dev);
extern void c2_unregister_device(struct c2_dev *c2dev);
extern int c2_rnic_init(struct c2_dev *c2dev);
extern void c2_rnic_term(struct c2_dev *c2dev);
extern void c2_rnic_interrupt(struct c2_dev *c2dev);
extern int c2_del_addr(struct c2_dev *c2dev, u32 inaddr, u32 inmask);
extern int c2_add_addr(struct c2_dev *c2dev, u32 inaddr, u32 inmask);

/* QPs */
extern int c2_alloc_qp(struct c2_dev *c2dev, struct c2_pd *pd,
		       struct ib_qp_init_attr *qp_attrs, struct c2_qp *qp);
extern void c2_free_qp(struct c2_dev *c2dev, struct c2_qp *qp);
extern struct ib_qp *c2_get_qp(struct ib_device *device, int qpn);
extern int c2_qp_modify(struct c2_dev *c2dev, struct c2_qp *qp,
			struct ib_qp_attr *attr, int attr_mask);
extern int c2_qp_set_read_limits(struct c2_dev *c2dev, struct c2_qp *qp,
				 int ord, int ird);
extern int c2_post_send(struct ib_qp *ibqp, struct ib_send_wr *ib_wr,
			struct ib_send_wr **bad_wr);
extern int c2_post_receive(struct ib_qp *ibqp, struct ib_recv_wr *ib_wr,
			   struct ib_recv_wr **bad_wr);
extern void __devinit c2_init_qp_table(struct c2_dev *c2dev);
extern void __devexit c2_cleanup_qp_table(struct c2_dev *c2dev);
extern void c2_set_qp_state(struct c2_qp *, int);
extern struct c2_qp *c2_find_qpn(struct c2_dev *c2dev, int qpn);

/* PDs */
extern int c2_pd_alloc(struct c2_dev *c2dev, int privileged, struct c2_pd *pd);
extern void c2_pd_free(struct c2_dev *c2dev, struct c2_pd *pd);
extern int __devinit c2_init_pd_table(struct c2_dev *c2dev);
extern void __devexit c2_cleanup_pd_table(struct c2_dev *c2dev);

/* CQs */
extern int c2_init_cq(struct c2_dev *c2dev, int entries,
		      struct c2_ucontext *ctx, struct c2_cq *cq);
extern void c2_free_cq(struct c2_dev *c2dev, struct c2_cq *cq);
extern void c2_cq_event(struct c2_dev *c2dev, u32 mq_index);
extern void c2_cq_clean(struct c2_dev *c2dev, struct c2_qp *qp, u32 mq_index);
extern int c2_poll_cq(struct ib_cq *ibcq, int num_entries, struct ib_wc *entry);
extern int c2_arm_cq(struct ib_cq *ibcq, enum ib_cq_notify notify);

/* CM */
extern int c2_llp_connect(struct iw_cm_id *cm_id,
			  struct iw_cm_conn_param *iw_param);
extern int c2_llp_accept(struct iw_cm_id *cm_id,
			 struct iw_cm_conn_param *iw_param);
extern int c2_llp_reject(struct iw_cm_id *cm_id, const void *pdata,
			 u8 pdata_len);
extern int c2_llp_service_create(struct iw_cm_id *cm_id, int backlog);
extern int c2_llp_service_destroy(struct iw_cm_id *cm_id);

/* MM */
extern int c2_nsmr_register_phys_kern(struct c2_dev *c2dev, u64 *addr_list,
 				      int page_size, int pbl_depth, u32 length,
 				      u32 off, u64 *va, enum c2_acf acf,
				      struct c2_mr *mr);
extern int c2_stag_dealloc(struct c2_dev *c2dev, u32 stag_index);

/* AE */
extern void c2_ae_event(struct c2_dev *c2dev, u32 mq_index);

/* MQSP Allocator */
extern int c2_init_mqsp_pool(struct c2_dev *c2dev, gfp_t gfp_mask,
			     struct sp_chunk **root);
extern void c2_free_mqsp_pool(struct c2_dev *c2dev, struct sp_chunk *root);
extern u16 *c2_alloc_mqsp(struct c2_dev *c2dev, struct sp_chunk *head,
			  dma_addr_t *dma_addr, gfp_t gfp_mask);
extern void c2_free_mqsp(u16 * mqsp);
#endif
