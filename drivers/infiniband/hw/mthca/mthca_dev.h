/*
 * Copyright (c) 2004, 2005 Topspin Communications.  All rights reserved.
 * Copyright (c) 2005 Sun Microsystems, Inc. All rights reserved.
 * Copyright (c) 2005, 2006 Cisco Systems.  All rights reserved.
 * Copyright (c) 2005 Mellanox Technologies. All rights reserved.
 * Copyright (c) 2004 Voltaire, Inc. All rights reserved.
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

#ifndef MTHCA_DEV_H
#define MTHCA_DEV_H

#include <linux/spinlock.h>
#include <linux/kernel.h>
#include <linux/pci.h>
#include <linux/dma-mapping.h>
#include <linux/timer.h>
#include <linux/mutex.h>
#include <linux/list.h>
#include <linux/semaphore.h>

#include "mthca_provider.h"
#include "mthca_doorbell.h"

#define DRV_NAME	"ib_mthca"
#define PFX		DRV_NAME ": "
#define DRV_VERSION	"1.0"
#define DRV_RELDATE	"April 4, 2008"

enum {
	MTHCA_FLAG_DDR_HIDDEN = 1 << 1,
	MTHCA_FLAG_SRQ        = 1 << 2,
	MTHCA_FLAG_MSI_X      = 1 << 3,
	MTHCA_FLAG_NO_LAM     = 1 << 4,
	MTHCA_FLAG_FMR        = 1 << 5,
	MTHCA_FLAG_MEMFREE    = 1 << 6,
	MTHCA_FLAG_PCIE       = 1 << 7,
	MTHCA_FLAG_SINAI_OPT  = 1 << 8
};

enum {
	MTHCA_MAX_PORTS = 2
};

enum {
	MTHCA_BOARD_ID_LEN = 64
};

enum {
	MTHCA_EQ_CONTEXT_SIZE =  0x40,
	MTHCA_CQ_CONTEXT_SIZE =  0x40,
	MTHCA_QP_CONTEXT_SIZE = 0x200,
	MTHCA_RDB_ENTRY_SIZE  =  0x20,
	MTHCA_AV_SIZE         =  0x20,
	MTHCA_MGM_ENTRY_SIZE  = 0x100,

	/* Arbel FW gives us these, but we need them for Tavor */
	MTHCA_MPT_ENTRY_SIZE  =  0x40,
	MTHCA_MTT_SEG_SIZE    =  0x40,

	MTHCA_QP_PER_MGM      = 4 * (MTHCA_MGM_ENTRY_SIZE / 16 - 2)
};

enum {
	MTHCA_EQ_CMD,
	MTHCA_EQ_ASYNC,
	MTHCA_EQ_COMP,
	MTHCA_NUM_EQ
};

enum {
	MTHCA_OPCODE_NOP            = 0x00,
	MTHCA_OPCODE_RDMA_WRITE     = 0x08,
	MTHCA_OPCODE_RDMA_WRITE_IMM = 0x09,
	MTHCA_OPCODE_SEND           = 0x0a,
	MTHCA_OPCODE_SEND_IMM       = 0x0b,
	MTHCA_OPCODE_RDMA_READ      = 0x10,
	MTHCA_OPCODE_ATOMIC_CS      = 0x11,
	MTHCA_OPCODE_ATOMIC_FA      = 0x12,
	MTHCA_OPCODE_BIND_MW        = 0x18,
};

enum {
	MTHCA_CMD_USE_EVENTS         = 1 << 0,
	MTHCA_CMD_POST_DOORBELLS     = 1 << 1
};

enum {
	MTHCA_CMD_NUM_DBELL_DWORDS = 8
};

struct mthca_cmd {
	struct dma_pool          *pool;
	struct mutex              hcr_mutex;
	struct semaphore 	  poll_sem;
	struct semaphore 	  event_sem;
	int              	  max_cmds;
	spinlock_t                context_lock;
	int                       free_head;
	struct mthca_cmd_context *context;
	u16                       token_mask;
	u32                       flags;
	void __iomem             *dbell_map;
	u16                       dbell_offsets[MTHCA_CMD_NUM_DBELL_DWORDS];
};

struct mthca_limits {
	int      num_ports;
	int      vl_cap;
	int      mtu_cap;
	int      gid_table_len;
	int      pkey_table_len;
	int      local_ca_ack_delay;
	int      num_uars;
	int      max_sg;
	int      num_qps;
	int      max_wqes;
	int	 max_desc_sz;
	int	 max_qp_init_rdma;
	int      reserved_qps;
	int      num_srqs;
	int      max_srq_wqes;
	int      max_srq_sge;
	int      reserved_srqs;
	int      num_eecs;
	int      reserved_eecs;
	int      num_cqs;
	int      max_cqes;
	int      reserved_cqs;
	int      num_eqs;
	int      reserved_eqs;
	int      num_mpts;
	int      num_mtt_segs;
	int	 mtt_seg_size;
	int      fmr_reserved_mtts;
	int      reserved_mtts;
	int      reserved_mrws;
	int      reserved_uars;
	int      num_mgms;
	int      num_amgms;
	int      reserved_mcgs;
	int      num_pds;
	int      reserved_pds;
	u32      page_size_cap;
	u32      flags;
	u16      stat_rate_support;
	u8       port_width_cap;
};

struct mthca_alloc {
	u32            last;
	u32            top;
	u32            max;
	u32            mask;
	spinlock_t     lock;
	unsigned long *table;
};

struct mthca_array {
	struct {
		void    **page;
		int       used;
	} *page_list;
};

struct mthca_uar_table {
	struct mthca_alloc alloc;
	u64                uarc_base;
	int                uarc_size;
};

struct mthca_pd_table {
	struct mthca_alloc alloc;
};

struct mthca_buddy {
	unsigned long **bits;
	int	       *num_free;
	int             max_order;
	spinlock_t      lock;
};

struct mthca_mr_table {
	struct mthca_alloc      mpt_alloc;
	struct mthca_buddy      mtt_buddy;
	struct mthca_buddy     *fmr_mtt_buddy;
	u64                     mtt_base;
	u64                     mpt_base;
	struct mthca_icm_table *mtt_table;
	struct mthca_icm_table *mpt_table;
	struct {
		void __iomem   *mpt_base;
		void __iomem   *mtt_base;
		struct mthca_buddy mtt_buddy;
	} tavor_fmr;
};

struct mthca_eq_table {
	struct mthca_alloc alloc;
	void __iomem      *clr_int;
	u32                clr_mask;
	u32                arm_mask;
	struct mthca_eq    eq[MTHCA_NUM_EQ];
	u64                icm_virt;
	struct page       *icm_page;
	dma_addr_t         icm_dma;
	int                have_irq;
	u8                 inta_pin;
};

struct mthca_cq_table {
	struct mthca_alloc 	alloc;
	spinlock_t         	lock;
	struct mthca_array      cq;
	struct mthca_icm_table *table;
};

struct mthca_srq_table {
	struct mthca_alloc 	alloc;
	spinlock_t         	lock;
	struct mthca_array      srq;
	struct mthca_icm_table *table;
};

struct mthca_qp_table {
	struct mthca_alloc     	alloc;
	u32                    	rdb_base;
	int                    	rdb_shift;
	int                    	sqp_start;
	spinlock_t             	lock;
	struct mthca_array     	qp;
	struct mthca_icm_table *qp_table;
	struct mthca_icm_table *eqp_table;
	struct mthca_icm_table *rdb_table;
};

struct mthca_av_table {
	struct dma_pool   *pool;
	int                num_ddr_avs;
	u64                ddr_av_base;
	void __iomem      *av_map;
	struct mthca_alloc alloc;
};

struct mthca_mcg_table {
	struct mutex		mutex;
	struct mthca_alloc 	alloc;
	struct mthca_icm_table *table;
};

struct mthca_catas_err {
	u64			addr;
	u32 __iomem	       *map;
	u32			size;
	struct timer_list	timer;
	struct list_head	list;
};

extern struct mutex mthca_device_mutex;

struct mthca_dev {
	struct ib_device  ib_dev;
	struct pci_dev   *pdev;

	int          	 hca_type;
	unsigned long	 mthca_flags;
	unsigned long    device_cap_flags;

	u32              rev_id;
	char             board_id[MTHCA_BOARD_ID_LEN];

	/* firmware info */
	u64              fw_ver;
	union {
		struct {
			u64 fw_start;
			u64 fw_end;
		}        tavor;
		struct {
			u64 clr_int_base;
			u64 eq_arm_base;
			u64 eq_set_ci_base;
			struct mthca_icm *fw_icm;
			struct mthca_icm *aux_icm;
			u16 fw_pages;
		}        arbel;
	}                fw;

	u64              ddr_start;
	u64              ddr_end;

	MTHCA_DECLARE_DOORBELL_LOCK(doorbell_lock)
	struct mutex cap_mask_mutex;

	void __iomem    *hcr;
	void __iomem    *kar;
	void __iomem    *clr_base;
	union {
		struct {
			void __iomem *ecr_base;
		} tavor;
		struct {
			void __iomem *eq_arm;
			void __iomem *eq_set_ci_base;
		} arbel;
	} eq_regs;

	struct mthca_cmd    cmd;
	struct mthca_limits limits;

	struct mthca_uar_table uar_table;
	struct mthca_pd_table  pd_table;
	struct mthca_mr_table  mr_table;
	struct mthca_eq_table  eq_table;
	struct mthca_cq_table  cq_table;
	struct mthca_srq_table srq_table;
	struct mthca_qp_table  qp_table;
	struct mthca_av_table  av_table;
	struct mthca_mcg_table mcg_table;

	struct mthca_catas_err catas_err;

	struct mthca_uar       driver_uar;
	struct mthca_db_table *db_tab;
	struct mthca_pd        driver_pd;
	struct mthca_mr        driver_mr;

	struct ib_mad_agent  *send_agent[MTHCA_MAX_PORTS][2];
	struct ib_ah         *sm_ah[MTHCA_MAX_PORTS];
	spinlock_t            sm_lock;
	u8                    rate[MTHCA_MAX_PORTS];
	bool		      active;
};

#ifdef CONFIG_INFINIBAND_MTHCA_DEBUG
extern int mthca_debug_level;

#define mthca_dbg(mdev, format, arg...)					\
	do {								\
		if (mthca_debug_level)					\
			dev_printk(KERN_DEBUG, &mdev->pdev->dev, format, ## arg); \
	} while (0)

#else /* CONFIG_INFINIBAND_MTHCA_DEBUG */

#define mthca_dbg(mdev, format, arg...) do { (void) mdev; } while (0)

#endif /* CONFIG_INFINIBAND_MTHCA_DEBUG */

#define mthca_err(mdev, format, arg...) \
	dev_err(&mdev->pdev->dev, format, ## arg)
#define mthca_info(mdev, format, arg...) \
	dev_info(&mdev->pdev->dev, format, ## arg)
#define mthca_warn(mdev, format, arg...) \
	dev_warn(&mdev->pdev->dev, format, ## arg)

extern void __buggy_use_of_MTHCA_GET(void);
extern void __buggy_use_of_MTHCA_PUT(void);

#define MTHCA_GET(dest, source, offset)                               \
	do {                                                          \
		void *__p = (char *) (source) + (offset);             \
		switch (sizeof (dest)) {                              \
		case 1: (dest) = *(u8 *) __p;       break;	      \
		case 2: (dest) = be16_to_cpup(__p); break;	      \
		case 4: (dest) = be32_to_cpup(__p); break;	      \
		case 8: (dest) = be64_to_cpup(__p); break;	      \
		default: __buggy_use_of_MTHCA_GET();		      \
		}                                                     \
	} while (0)

#define MTHCA_PUT(dest, source, offset)                               \
	do {                                                          \
		void *__d = ((char *) (dest) + (offset));	      \
		switch (sizeof(source)) {                             \
		case 1: *(u8 *) __d = (source);                break; \
		case 2:	*(__be16 *) __d = cpu_to_be16(source); break; \
		case 4:	*(__be32 *) __d = cpu_to_be32(source); break; \
		case 8:	*(__be64 *) __d = cpu_to_be64(source); break; \
		default: __buggy_use_of_MTHCA_PUT();		      \
		}                                                     \
	} while (0)

int mthca_reset(struct mthca_dev *mdev);

u32 mthca_alloc(struct mthca_alloc *alloc);
void mthca_free(struct mthca_alloc *alloc, u32 obj);
int mthca_alloc_init(struct mthca_alloc *alloc, u32 num, u32 mask,
		     u32 reserved);
void mthca_alloc_cleanup(struct mthca_alloc *alloc);
void *mthca_array_get(struct mthca_array *array, int index);
int mthca_array_set(struct mthca_array *array, int index, void *value);
void mthca_array_clear(struct mthca_array *array, int index);
int mthca_array_init(struct mthca_array *array, int nent);
void mthca_array_cleanup(struct mthca_array *array, int nent);
int mthca_buf_alloc(struct mthca_dev *dev, int size, int max_direct,
		    union mthca_buf *buf, int *is_direct, struct mthca_pd *pd,
		    int hca_write, struct mthca_mr *mr);
void mthca_buf_free(struct mthca_dev *dev, int size, union mthca_buf *buf,
		    int is_direct, struct mthca_mr *mr);

int mthca_init_uar_table(struct mthca_dev *dev);
int mthca_init_pd_table(struct mthca_dev *dev);
int mthca_init_mr_table(struct mthca_dev *dev);
int mthca_init_eq_table(struct mthca_dev *dev);
int mthca_init_cq_table(struct mthca_dev *dev);
int mthca_init_srq_table(struct mthca_dev *dev);
int mthca_init_qp_table(struct mthca_dev *dev);
int mthca_init_av_table(struct mthca_dev *dev);
int mthca_init_mcg_table(struct mthca_dev *dev);

void mthca_cleanup_uar_table(struct mthca_dev *dev);
void mthca_cleanup_pd_table(struct mthca_dev *dev);
void mthca_cleanup_mr_table(struct mthca_dev *dev);
void mthca_cleanup_eq_table(struct mthca_dev *dev);
void mthca_cleanup_cq_table(struct mthca_dev *dev);
void mthca_cleanup_srq_table(struct mthca_dev *dev);
void mthca_cleanup_qp_table(struct mthca_dev *dev);
void mthca_cleanup_av_table(struct mthca_dev *dev);
void mthca_cleanup_mcg_table(struct mthca_dev *dev);

int mthca_register_device(struct mthca_dev *dev);
void mthca_unregister_device(struct mthca_dev *dev);

void mthca_start_catas_poll(struct mthca_dev *dev);
void mthca_stop_catas_poll(struct mthca_dev *dev);
int __mthca_restart_one(struct pci_dev *pdev);
int mthca_catas_init(void);
void mthca_catas_cleanup(void);

int mthca_uar_alloc(struct mthca_dev *dev, struct mthca_uar *uar);
void mthca_uar_free(struct mthca_dev *dev, struct mthca_uar *uar);

int mthca_pd_alloc(struct mthca_dev *dev, int privileged, struct mthca_pd *pd);
void mthca_pd_free(struct mthca_dev *dev, struct mthca_pd *pd);

int mthca_write_mtt_size(struct mthca_dev *dev);

struct mthca_mtt *mthca_alloc_mtt(struct mthca_dev *dev, int size);
void mthca_free_mtt(struct mthca_dev *dev, struct mthca_mtt *mtt);
int mthca_write_mtt(struct mthca_dev *dev, struct mthca_mtt *mtt,
		    int start_index, u64 *buffer_list, int list_len);
int mthca_mr_alloc(struct mthca_dev *dev, u32 pd, int buffer_size_shift,
		   u64 iova, u64 total_size, u32 access, struct mthca_mr *mr);
int mthca_mr_alloc_notrans(struct mthca_dev *dev, u32 pd,
			   u32 access, struct mthca_mr *mr);
int mthca_mr_alloc_phys(struct mthca_dev *dev, u32 pd,
			u64 *buffer_list, int buffer_size_shift,
			int list_len, u64 iova, u64 total_size,
			u32 access, struct mthca_mr *mr);
void mthca_free_mr(struct mthca_dev *dev,  struct mthca_mr *mr);

int mthca_fmr_alloc(struct mthca_dev *dev, u32 pd,
		    u32 access, struct mthca_fmr *fmr);
int mthca_tavor_map_phys_fmr(struct ib_fmr *ibfmr, u64 *page_list,
			     int list_len, u64 iova);
void mthca_tavor_fmr_unmap(struct mthca_dev *dev, struct mthca_fmr *fmr);
int mthca_arbel_map_phys_fmr(struct ib_fmr *ibfmr, u64 *page_list,
			     int list_len, u64 iova);
void mthca_arbel_fmr_unmap(struct mthca_dev *dev, struct mthca_fmr *fmr);
int mthca_free_fmr(struct mthca_dev *dev,  struct mthca_fmr *fmr);

int mthca_map_eq_icm(struct mthca_dev *dev, u64 icm_virt);
void mthca_unmap_eq_icm(struct mthca_dev *dev);

int mthca_poll_cq(struct ib_cq *ibcq, int num_entries,
		  struct ib_wc *entry);
int mthca_tavor_arm_cq(struct ib_cq *cq, enum ib_cq_notify_flags flags);
int mthca_arbel_arm_cq(struct ib_cq *cq, enum ib_cq_notify_flags flags);
int mthca_init_cq(struct mthca_dev *dev, int nent,
		  struct mthca_ucontext *ctx, u32 pdn,
		  struct mthca_cq *cq);
void mthca_free_cq(struct mthca_dev *dev,
		   struct mthca_cq *cq);
void mthca_cq_completion(struct mthca_dev *dev, u32 cqn);
void mthca_cq_event(struct mthca_dev *dev, u32 cqn,
		    enum ib_event_type event_type);
void mthca_cq_clean(struct mthca_dev *dev, struct mthca_cq *cq, u32 qpn,
		    struct mthca_srq *srq);
void mthca_cq_resize_copy_cqes(struct mthca_cq *cq);
int mthca_alloc_cq_buf(struct mthca_dev *dev, struct mthca_cq_buf *buf, int nent);
void mthca_free_cq_buf(struct mthca_dev *dev, struct mthca_cq_buf *buf, int cqe);

int mthca_alloc_srq(struct mthca_dev *dev, struct mthca_pd *pd,
		    struct ib_srq_attr *attr, struct mthca_srq *srq);
void mthca_free_srq(struct mthca_dev *dev, struct mthca_srq *srq);
int mthca_modify_srq(struct ib_srq *ibsrq, struct ib_srq_attr *attr,
		     enum ib_srq_attr_mask attr_mask, struct ib_udata *udata);
int mthca_query_srq(struct ib_srq *srq, struct ib_srq_attr *srq_attr);
int mthca_max_srq_sge(struct mthca_dev *dev);
void mthca_srq_event(struct mthca_dev *dev, u32 srqn,
		     enum ib_event_type event_type);
void mthca_free_srq_wqe(struct mthca_srq *srq, u32 wqe_addr);
int mthca_tavor_post_srq_recv(struct ib_srq *srq, const struct ib_recv_wr *wr,
			      const struct ib_recv_wr **bad_wr);
int mthca_arbel_post_srq_recv(struct ib_srq *srq, const struct ib_recv_wr *wr,
			      const struct ib_recv_wr **bad_wr);

void mthca_qp_event(struct mthca_dev *dev, u32 qpn,
		    enum ib_event_type event_type);
int mthca_query_qp(struct ib_qp *ibqp, struct ib_qp_attr *qp_attr, int qp_attr_mask,
		   struct ib_qp_init_attr *qp_init_attr);
int mthca_modify_qp(struct ib_qp *ibqp, struct ib_qp_attr *attr, int attr_mask,
		    struct ib_udata *udata);
int mthca_tavor_post_send(struct ib_qp *ibqp, const struct ib_send_wr *wr,
			  const struct ib_send_wr **bad_wr);
int mthca_tavor_post_receive(struct ib_qp *ibqp, const struct ib_recv_wr *wr,
			     const struct ib_recv_wr **bad_wr);
int mthca_arbel_post_send(struct ib_qp *ibqp, const struct ib_send_wr *wr,
			  const struct ib_send_wr **bad_wr);
int mthca_arbel_post_receive(struct ib_qp *ibqp, const struct ib_recv_wr *wr,
			     const struct ib_recv_wr **bad_wr);
void mthca_free_err_wqe(struct mthca_dev *dev, struct mthca_qp *qp, int is_send,
			int index, int *dbd, __be32 *new_wqe);
int mthca_alloc_qp(struct mthca_dev *dev,
		   struct mthca_pd *pd,
		   struct mthca_cq *send_cq,
		   struct mthca_cq *recv_cq,
		   enum ib_qp_type type,
		   enum ib_sig_type send_policy,
		   struct ib_qp_cap *cap,
		   struct mthca_qp *qp);
int mthca_alloc_sqp(struct mthca_dev *dev,
		    struct mthca_pd *pd,
		    struct mthca_cq *send_cq,
		    struct mthca_cq *recv_cq,
		    enum ib_sig_type send_policy,
		    struct ib_qp_cap *cap,
		    int qpn,
		    int port,
		    struct mthca_sqp *sqp);
void mthca_free_qp(struct mthca_dev *dev, struct mthca_qp *qp);
int mthca_create_ah(struct mthca_dev *dev,
		    struct mthca_pd *pd,
		    struct rdma_ah_attr *ah_attr,
		    struct mthca_ah *ah);
int mthca_destroy_ah(struct mthca_dev *dev, struct mthca_ah *ah);
int mthca_read_ah(struct mthca_dev *dev, struct mthca_ah *ah,
		  struct ib_ud_header *header);
int mthca_ah_query(struct ib_ah *ibah, struct rdma_ah_attr *attr);
int mthca_ah_grh_present(struct mthca_ah *ah);
u8 mthca_get_rate(struct mthca_dev *dev, int static_rate, u8 port);
enum ib_rate mthca_rate_to_ib(struct mthca_dev *dev, u8 mthca_rate, u8 port);

int mthca_multicast_attach(struct ib_qp *ibqp, union ib_gid *gid, u16 lid);
int mthca_multicast_detach(struct ib_qp *ibqp, union ib_gid *gid, u16 lid);

int mthca_process_mad(struct ib_device *ibdev,
		      int mad_flags,
		      u8 port_num,
		      const struct ib_wc *in_wc,
		      const struct ib_grh *in_grh,
		      const struct ib_mad_hdr *in, size_t in_mad_size,
		      struct ib_mad_hdr *out, size_t *out_mad_size,
		      u16 *out_mad_pkey_index);
int mthca_create_agents(struct mthca_dev *dev);
void mthca_free_agents(struct mthca_dev *dev);

static inline struct mthca_dev *to_mdev(struct ib_device *ibdev)
{
	return container_of(ibdev, struct mthca_dev, ib_dev);
}

static inline int mthca_is_memfree(struct mthca_dev *dev)
{
	return dev->mthca_flags & MTHCA_FLAG_MEMFREE;
}

#endif /* MTHCA_DEV_H */
