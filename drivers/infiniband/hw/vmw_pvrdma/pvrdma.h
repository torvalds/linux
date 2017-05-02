/*
 * Copyright (c) 2012-2016 VMware, Inc.  All rights reserved.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of EITHER the GNU General Public License
 * version 2 as published by the Free Software Foundation or the BSD
 * 2-Clause License. This program is distributed in the hope that it
 * will be useful, but WITHOUT ANY WARRANTY; WITHOUT EVEN THE IMPLIED
 * WARRANTY OF MERCHANTABILITY OR FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License version 2 for more details at
 * http://www.gnu.org/licenses/old-licenses/gpl-2.0.en.html.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program available in the file COPYING in the main
 * directory of this source tree.
 *
 * The BSD 2-Clause License
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
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
 * FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE
 * COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT,
 * INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
 * STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED
 * OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#ifndef __PVRDMA_H__
#define __PVRDMA_H__

#include <linux/compiler.h>
#include <linux/interrupt.h>
#include <linux/list.h>
#include <linux/mutex.h>
#include <linux/pci.h>
#include <linux/semaphore.h>
#include <linux/workqueue.h>
#include <rdma/ib_umem.h>
#include <rdma/ib_verbs.h>
#include <rdma/vmw_pvrdma-abi.h>

#include "pvrdma_ring.h"
#include "pvrdma_dev_api.h"
#include "pvrdma_verbs.h"

/* NOT the same as BIT_MASK(). */
#define PVRDMA_MASK(n) ((n << 1) - 1)

/*
 * VMware PVRDMA PCI device id.
 */
#define PCI_DEVICE_ID_VMWARE_PVRDMA	0x0820

#define PVRDMA_NUM_RING_PAGES		4
#define PVRDMA_QP_NUM_HEADER_PAGES	1

struct pvrdma_dev;

struct pvrdma_page_dir {
	dma_addr_t dir_dma;
	u64 *dir;
	int ntables;
	u64 **tables;
	u64 npages;
	void **pages;
};

struct pvrdma_cq {
	struct ib_cq ibcq;
	int offset;
	spinlock_t cq_lock; /* Poll lock. */
	struct pvrdma_uar_map *uar;
	struct ib_umem *umem;
	struct pvrdma_ring_state *ring_state;
	struct pvrdma_page_dir pdir;
	u32 cq_handle;
	bool is_kernel;
	atomic_t refcnt;
	wait_queue_head_t wait;
};

struct pvrdma_id_table {
	u32 last;
	u32 top;
	u32 max;
	u32 mask;
	spinlock_t lock; /* Table lock. */
	unsigned long *table;
};

struct pvrdma_uar_map {
	unsigned long pfn;
	void __iomem *map;
	int index;
};

struct pvrdma_uar_table {
	struct pvrdma_id_table tbl;
	int size;
};

struct pvrdma_ucontext {
	struct ib_ucontext ibucontext;
	struct pvrdma_dev *dev;
	struct pvrdma_uar_map uar;
	u64 ctx_handle;
};

struct pvrdma_pd {
	struct ib_pd ibpd;
	u32 pdn;
	u32 pd_handle;
	int privileged;
};

struct pvrdma_mr {
	u32 mr_handle;
	u64 iova;
	u64 size;
};

struct pvrdma_user_mr {
	struct ib_mr ibmr;
	struct ib_umem *umem;
	struct pvrdma_mr mmr;
	struct pvrdma_page_dir pdir;
	u64 *pages;
	u32 npages;
	u32 max_pages;
	u32 page_shift;
};

struct pvrdma_wq {
	struct pvrdma_ring *ring;
	spinlock_t lock; /* Work queue lock. */
	int wqe_cnt;
	int wqe_size;
	int max_sg;
	int offset;
};

struct pvrdma_ah {
	struct ib_ah ibah;
	struct pvrdma_av av;
};

struct pvrdma_qp {
	struct ib_qp ibqp;
	u32 qp_handle;
	u32 qkey;
	struct pvrdma_wq sq;
	struct pvrdma_wq rq;
	struct ib_umem *rumem;
	struct ib_umem *sumem;
	struct pvrdma_page_dir pdir;
	int npages;
	int npages_send;
	int npages_recv;
	u32 flags;
	u8 port;
	u8 state;
	bool is_kernel;
	struct mutex mutex; /* QP state mutex. */
	atomic_t refcnt;
	wait_queue_head_t wait;
};

struct pvrdma_dev {
	/* PCI device-related information. */
	struct ib_device ib_dev;
	struct pci_dev *pdev;
	void __iomem *regs;
	struct pvrdma_device_shared_region *dsr; /* Shared region pointer */
	dma_addr_t dsrbase; /* Shared region base address */
	void *cmd_slot;
	void *resp_slot;
	unsigned long flags;
	struct list_head device_link;

	/* Locking and interrupt information. */
	spinlock_t cmd_lock; /* Command lock. */
	struct semaphore cmd_sema;
	struct completion cmd_done;
	unsigned int nr_vectors;

	/* RDMA-related device information. */
	union ib_gid *sgid_tbl;
	struct pvrdma_ring_state *async_ring_state;
	struct pvrdma_page_dir async_pdir;
	struct pvrdma_ring_state *cq_ring_state;
	struct pvrdma_page_dir cq_pdir;
	struct pvrdma_cq **cq_tbl;
	spinlock_t cq_tbl_lock;
	struct pvrdma_qp **qp_tbl;
	spinlock_t qp_tbl_lock;
	struct pvrdma_uar_table uar_table;
	struct pvrdma_uar_map driver_uar;
	__be64 sys_image_guid;
	spinlock_t desc_lock; /* Device modification lock. */
	u32 port_cap_mask;
	struct mutex port_mutex; /* Port modification mutex. */
	bool ib_active;
	atomic_t num_qps;
	atomic_t num_cqs;
	atomic_t num_pds;
	atomic_t num_ahs;

	/* Network device information. */
	struct net_device *netdev;
	struct notifier_block nb_netdev;
};

struct pvrdma_netdevice_work {
	struct work_struct work;
	struct net_device *event_netdev;
	unsigned long event;
};

static inline struct pvrdma_dev *to_vdev(struct ib_device *ibdev)
{
	return container_of(ibdev, struct pvrdma_dev, ib_dev);
}

static inline struct
pvrdma_ucontext *to_vucontext(struct ib_ucontext *ibucontext)
{
	return container_of(ibucontext, struct pvrdma_ucontext, ibucontext);
}

static inline struct pvrdma_pd *to_vpd(struct ib_pd *ibpd)
{
	return container_of(ibpd, struct pvrdma_pd, ibpd);
}

static inline struct pvrdma_cq *to_vcq(struct ib_cq *ibcq)
{
	return container_of(ibcq, struct pvrdma_cq, ibcq);
}

static inline struct pvrdma_user_mr *to_vmr(struct ib_mr *ibmr)
{
	return container_of(ibmr, struct pvrdma_user_mr, ibmr);
}

static inline struct pvrdma_qp *to_vqp(struct ib_qp *ibqp)
{
	return container_of(ibqp, struct pvrdma_qp, ibqp);
}

static inline struct pvrdma_ah *to_vah(struct ib_ah *ibah)
{
	return container_of(ibah, struct pvrdma_ah, ibah);
}

static inline void pvrdma_write_reg(struct pvrdma_dev *dev, u32 reg, u32 val)
{
	writel(cpu_to_le32(val), dev->regs + reg);
}

static inline u32 pvrdma_read_reg(struct pvrdma_dev *dev, u32 reg)
{
	return le32_to_cpu(readl(dev->regs + reg));
}

static inline void pvrdma_write_uar_cq(struct pvrdma_dev *dev, u32 val)
{
	writel(cpu_to_le32(val), dev->driver_uar.map + PVRDMA_UAR_CQ_OFFSET);
}

static inline void pvrdma_write_uar_qp(struct pvrdma_dev *dev, u32 val)
{
	writel(cpu_to_le32(val), dev->driver_uar.map + PVRDMA_UAR_QP_OFFSET);
}

static inline void *pvrdma_page_dir_get_ptr(struct pvrdma_page_dir *pdir,
					    u64 offset)
{
	return pdir->pages[offset / PAGE_SIZE] + (offset % PAGE_SIZE);
}

static inline enum pvrdma_mtu ib_mtu_to_pvrdma(enum ib_mtu mtu)
{
	return (enum pvrdma_mtu)mtu;
}

static inline enum ib_mtu pvrdma_mtu_to_ib(enum pvrdma_mtu mtu)
{
	return (enum ib_mtu)mtu;
}

static inline enum pvrdma_port_state ib_port_state_to_pvrdma(
					enum ib_port_state state)
{
	return (enum pvrdma_port_state)state;
}

static inline enum ib_port_state pvrdma_port_state_to_ib(
					enum pvrdma_port_state state)
{
	return (enum ib_port_state)state;
}

static inline int ib_port_cap_flags_to_pvrdma(int flags)
{
	return flags & PVRDMA_MASK(PVRDMA_PORT_CAP_FLAGS_MAX);
}

static inline int pvrdma_port_cap_flags_to_ib(int flags)
{
	return flags;
}

static inline enum pvrdma_port_width ib_port_width_to_pvrdma(
					enum ib_port_width width)
{
	return (enum pvrdma_port_width)width;
}

static inline enum ib_port_width pvrdma_port_width_to_ib(
					enum pvrdma_port_width width)
{
	return (enum ib_port_width)width;
}

static inline enum pvrdma_port_speed ib_port_speed_to_pvrdma(
					enum ib_port_speed speed)
{
	return (enum pvrdma_port_speed)speed;
}

static inline enum ib_port_speed pvrdma_port_speed_to_ib(
					enum pvrdma_port_speed speed)
{
	return (enum ib_port_speed)speed;
}

static inline int pvrdma_qp_attr_mask_to_ib(int attr_mask)
{
	return attr_mask;
}

static inline int ib_qp_attr_mask_to_pvrdma(int attr_mask)
{
	return attr_mask & PVRDMA_MASK(PVRDMA_QP_ATTR_MASK_MAX);
}

static inline enum pvrdma_mig_state ib_mig_state_to_pvrdma(
					enum ib_mig_state state)
{
	return (enum pvrdma_mig_state)state;
}

static inline enum ib_mig_state pvrdma_mig_state_to_ib(
					enum pvrdma_mig_state state)
{
	return (enum ib_mig_state)state;
}

static inline int ib_access_flags_to_pvrdma(int flags)
{
	return flags;
}

static inline int pvrdma_access_flags_to_ib(int flags)
{
	return flags & PVRDMA_MASK(PVRDMA_ACCESS_FLAGS_MAX);
}

static inline enum pvrdma_qp_type ib_qp_type_to_pvrdma(enum ib_qp_type type)
{
	return (enum pvrdma_qp_type)type;
}

static inline enum ib_qp_type pvrdma_qp_type_to_ib(enum pvrdma_qp_type type)
{
	return (enum ib_qp_type)type;
}

static inline enum pvrdma_qp_state ib_qp_state_to_pvrdma(enum ib_qp_state state)
{
	return (enum pvrdma_qp_state)state;
}

static inline enum ib_qp_state pvrdma_qp_state_to_ib(enum pvrdma_qp_state state)
{
	return (enum ib_qp_state)state;
}

static inline enum pvrdma_wr_opcode ib_wr_opcode_to_pvrdma(enum ib_wr_opcode op)
{
	return (enum pvrdma_wr_opcode)op;
}

static inline enum ib_wc_status pvrdma_wc_status_to_ib(
					enum pvrdma_wc_status status)
{
	return (enum ib_wc_status)status;
}

static inline int pvrdma_wc_opcode_to_ib(int opcode)
{
	return opcode;
}

static inline int pvrdma_wc_flags_to_ib(int flags)
{
	return flags;
}

static inline int ib_send_flags_to_pvrdma(int flags)
{
	return flags & PVRDMA_MASK(PVRDMA_SEND_FLAGS_MAX);
}

void pvrdma_qp_cap_to_ib(struct ib_qp_cap *dst,
			 const struct pvrdma_qp_cap *src);
void ib_qp_cap_to_pvrdma(struct pvrdma_qp_cap *dst,
			 const struct ib_qp_cap *src);
void pvrdma_gid_to_ib(union ib_gid *dst, const union pvrdma_gid *src);
void ib_gid_to_pvrdma(union pvrdma_gid *dst, const union ib_gid *src);
void pvrdma_global_route_to_ib(struct ib_global_route *dst,
			       const struct pvrdma_global_route *src);
void ib_global_route_to_pvrdma(struct pvrdma_global_route *dst,
			       const struct ib_global_route *src);
void pvrdma_ah_attr_to_ib(struct ib_ah_attr *dst,
			  const struct pvrdma_ah_attr *src);
void ib_ah_attr_to_pvrdma(struct pvrdma_ah_attr *dst,
			  const struct ib_ah_attr *src);

int pvrdma_uar_table_init(struct pvrdma_dev *dev);
void pvrdma_uar_table_cleanup(struct pvrdma_dev *dev);

int pvrdma_uar_alloc(struct pvrdma_dev *dev, struct pvrdma_uar_map *uar);
void pvrdma_uar_free(struct pvrdma_dev *dev, struct pvrdma_uar_map *uar);

void _pvrdma_flush_cqe(struct pvrdma_qp *qp, struct pvrdma_cq *cq);

int pvrdma_page_dir_init(struct pvrdma_dev *dev, struct pvrdma_page_dir *pdir,
			 u64 npages, bool alloc_pages);
void pvrdma_page_dir_cleanup(struct pvrdma_dev *dev,
			     struct pvrdma_page_dir *pdir);
int pvrdma_page_dir_insert_dma(struct pvrdma_page_dir *pdir, u64 idx,
			       dma_addr_t daddr);
int pvrdma_page_dir_insert_umem(struct pvrdma_page_dir *pdir,
				struct ib_umem *umem, u64 offset);
dma_addr_t pvrdma_page_dir_get_dma(struct pvrdma_page_dir *pdir, u64 idx);
int pvrdma_page_dir_insert_page_list(struct pvrdma_page_dir *pdir,
				     u64 *page_list, int num_pages);

int pvrdma_cmd_post(struct pvrdma_dev *dev, union pvrdma_cmd_req *req,
		    union pvrdma_cmd_resp *rsp, unsigned resp_code);

#endif /* __PVRDMA_H__ */
