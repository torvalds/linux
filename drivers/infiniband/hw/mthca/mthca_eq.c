/*
 * Copyright (c) 2004, 2005 Topspin Communications.  All rights reserved.
 * Copyright (c) 2005 Mellanox Technologies. All rights reserved.
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
 *
 * $Id: mthca_eq.c 1382 2004-12-24 02:21:02Z roland $
 */

#include <linux/init.h>
#include <linux/errno.h>
#include <linux/interrupt.h>
#include <linux/pci.h>

#include "mthca_dev.h"
#include "mthca_cmd.h"
#include "mthca_config_reg.h"

enum {
	MTHCA_NUM_ASYNC_EQE = 0x80,
	MTHCA_NUM_CMD_EQE   = 0x80,
	MTHCA_EQ_ENTRY_SIZE = 0x20
};

/*
 * Must be packed because start is 64 bits but only aligned to 32 bits.
 */
struct mthca_eq_context {
	__be32 flags;
	__be64 start;
	__be32 logsize_usrpage;
	__be32 tavor_pd;	/* reserved for Arbel */
	u8     reserved1[3];
	u8     intr;
	__be32 arbel_pd;	/* lost_count for Tavor */
	__be32 lkey;
	u32    reserved2[2];
	__be32 consumer_index;
	__be32 producer_index;
	u32    reserved3[4];
} __attribute__((packed));

#define MTHCA_EQ_STATUS_OK          ( 0 << 28)
#define MTHCA_EQ_STATUS_OVERFLOW    ( 9 << 28)
#define MTHCA_EQ_STATUS_WRITE_FAIL  (10 << 28)
#define MTHCA_EQ_OWNER_SW           ( 0 << 24)
#define MTHCA_EQ_OWNER_HW           ( 1 << 24)
#define MTHCA_EQ_FLAG_TR            ( 1 << 18)
#define MTHCA_EQ_FLAG_OI            ( 1 << 17)
#define MTHCA_EQ_STATE_ARMED        ( 1 <<  8)
#define MTHCA_EQ_STATE_FIRED        ( 2 <<  8)
#define MTHCA_EQ_STATE_ALWAYS_ARMED ( 3 <<  8)
#define MTHCA_EQ_STATE_ARBEL        ( 8 <<  8)

enum {
	MTHCA_EVENT_TYPE_COMP       	    = 0x00,
	MTHCA_EVENT_TYPE_PATH_MIG   	    = 0x01,
	MTHCA_EVENT_TYPE_COMM_EST   	    = 0x02,
	MTHCA_EVENT_TYPE_SQ_DRAINED 	    = 0x03,
	MTHCA_EVENT_TYPE_SRQ_LAST_WQE       = 0x13,
	MTHCA_EVENT_TYPE_CQ_ERROR   	    = 0x04,
	MTHCA_EVENT_TYPE_WQ_CATAS_ERROR     = 0x05,
	MTHCA_EVENT_TYPE_EEC_CATAS_ERROR    = 0x06,
	MTHCA_EVENT_TYPE_PATH_MIG_FAILED    = 0x07,
	MTHCA_EVENT_TYPE_WQ_INVAL_REQ_ERROR = 0x10,
	MTHCA_EVENT_TYPE_WQ_ACCESS_ERROR    = 0x11,
	MTHCA_EVENT_TYPE_SRQ_CATAS_ERROR    = 0x12,
	MTHCA_EVENT_TYPE_LOCAL_CATAS_ERROR  = 0x08,
	MTHCA_EVENT_TYPE_PORT_CHANGE        = 0x09,
	MTHCA_EVENT_TYPE_EQ_OVERFLOW        = 0x0f,
	MTHCA_EVENT_TYPE_ECC_DETECT         = 0x0e,
	MTHCA_EVENT_TYPE_CMD                = 0x0a
};

#define MTHCA_ASYNC_EVENT_MASK ((1ULL << MTHCA_EVENT_TYPE_PATH_MIG)           | \
				(1ULL << MTHCA_EVENT_TYPE_COMM_EST)           | \
				(1ULL << MTHCA_EVENT_TYPE_SQ_DRAINED)         | \
				(1ULL << MTHCA_EVENT_TYPE_CQ_ERROR)           | \
				(1ULL << MTHCA_EVENT_TYPE_WQ_CATAS_ERROR)     | \
				(1ULL << MTHCA_EVENT_TYPE_EEC_CATAS_ERROR)    | \
				(1ULL << MTHCA_EVENT_TYPE_PATH_MIG_FAILED)    | \
				(1ULL << MTHCA_EVENT_TYPE_WQ_INVAL_REQ_ERROR) | \
				(1ULL << MTHCA_EVENT_TYPE_WQ_ACCESS_ERROR)    | \
				(1ULL << MTHCA_EVENT_TYPE_LOCAL_CATAS_ERROR)  | \
				(1ULL << MTHCA_EVENT_TYPE_PORT_CHANGE)        | \
				(1ULL << MTHCA_EVENT_TYPE_ECC_DETECT))
#define MTHCA_SRQ_EVENT_MASK    (1ULL << MTHCA_EVENT_TYPE_SRQ_CATAS_ERROR)    | \
				(1ULL << MTHCA_EVENT_TYPE_SRQ_LAST_WQE)
#define MTHCA_CMD_EVENT_MASK    (1ULL << MTHCA_EVENT_TYPE_CMD)

#define MTHCA_EQ_DB_INC_CI     (1 << 24)
#define MTHCA_EQ_DB_REQ_NOT    (2 << 24)
#define MTHCA_EQ_DB_DISARM_CQ  (3 << 24)
#define MTHCA_EQ_DB_SET_CI     (4 << 24)
#define MTHCA_EQ_DB_ALWAYS_ARM (5 << 24)

struct mthca_eqe {
	u8 reserved1;
	u8 type;
	u8 reserved2;
	u8 subtype;
	union {
		u32 raw[6];
		struct {
			__be32 cqn;
		} __attribute__((packed)) comp;
		struct {
			u16    reserved1;
			__be16 token;
			u32    reserved2;
			u8     reserved3[3];
			u8     status;
			__be64 out_param;
		} __attribute__((packed)) cmd;
		struct {
			__be32 qpn;
		} __attribute__((packed)) qp;
		struct {
			__be32 cqn;
			u32    reserved1;
			u8     reserved2[3];
			u8     syndrome;
		} __attribute__((packed)) cq_err;
		struct {
			u32    reserved1[2];
			__be32 port;
		} __attribute__((packed)) port_change;
	} event;
	u8 reserved3[3];
	u8 owner;
} __attribute__((packed));

#define  MTHCA_EQ_ENTRY_OWNER_SW      (0 << 7)
#define  MTHCA_EQ_ENTRY_OWNER_HW      (1 << 7)

static inline u64 async_mask(struct mthca_dev *dev)
{
	return dev->mthca_flags & MTHCA_FLAG_SRQ ?
		MTHCA_ASYNC_EVENT_MASK | MTHCA_SRQ_EVENT_MASK :
		MTHCA_ASYNC_EVENT_MASK;
}

static inline void tavor_set_eq_ci(struct mthca_dev *dev, struct mthca_eq *eq, u32 ci)
{
	__be32 doorbell[2];

	doorbell[0] = cpu_to_be32(MTHCA_EQ_DB_SET_CI | eq->eqn);
	doorbell[1] = cpu_to_be32(ci & (eq->nent - 1));

	/*
	 * This barrier makes sure that all updates to ownership bits
	 * done by set_eqe_hw() hit memory before the consumer index
	 * is updated.  set_eq_ci() allows the HCA to possibly write
	 * more EQ entries, and we want to avoid the exceedingly
	 * unlikely possibility of the HCA writing an entry and then
	 * having set_eqe_hw() overwrite the owner field.
	 */
	wmb();
	mthca_write64(doorbell,
		      dev->kar + MTHCA_EQ_DOORBELL,
		      MTHCA_GET_DOORBELL_LOCK(&dev->doorbell_lock));
}

static inline void arbel_set_eq_ci(struct mthca_dev *dev, struct mthca_eq *eq, u32 ci)
{
	/* See comment in tavor_set_eq_ci() above. */
	wmb();
	__raw_writel((__force u32) cpu_to_be32(ci),
		     dev->eq_regs.arbel.eq_set_ci_base + eq->eqn * 8);
	/* We still want ordering, just not swabbing, so add a barrier */
	mb();
}

static inline void set_eq_ci(struct mthca_dev *dev, struct mthca_eq *eq, u32 ci)
{
	if (mthca_is_memfree(dev))
		arbel_set_eq_ci(dev, eq, ci);
	else
		tavor_set_eq_ci(dev, eq, ci);
}

static inline void tavor_eq_req_not(struct mthca_dev *dev, int eqn)
{
	__be32 doorbell[2];

	doorbell[0] = cpu_to_be32(MTHCA_EQ_DB_REQ_NOT | eqn);
	doorbell[1] = 0;

	mthca_write64(doorbell,
		      dev->kar + MTHCA_EQ_DOORBELL,
		      MTHCA_GET_DOORBELL_LOCK(&dev->doorbell_lock));
}

static inline void arbel_eq_req_not(struct mthca_dev *dev, u32 eqn_mask)
{
	writel(eqn_mask, dev->eq_regs.arbel.eq_arm);
}

static inline void disarm_cq(struct mthca_dev *dev, int eqn, int cqn)
{
	if (!mthca_is_memfree(dev)) {
		__be32 doorbell[2];

		doorbell[0] = cpu_to_be32(MTHCA_EQ_DB_DISARM_CQ | eqn);
		doorbell[1] = cpu_to_be32(cqn);

		mthca_write64(doorbell,
			      dev->kar + MTHCA_EQ_DOORBELL,
			      MTHCA_GET_DOORBELL_LOCK(&dev->doorbell_lock));
	}
}

static inline struct mthca_eqe *get_eqe(struct mthca_eq *eq, u32 entry)
{
	unsigned long off = (entry & (eq->nent - 1)) * MTHCA_EQ_ENTRY_SIZE;
	return eq->page_list[off / PAGE_SIZE].buf + off % PAGE_SIZE;
}

static inline struct mthca_eqe* next_eqe_sw(struct mthca_eq *eq)
{
	struct mthca_eqe* eqe;
	eqe = get_eqe(eq, eq->cons_index);
	return (MTHCA_EQ_ENTRY_OWNER_HW & eqe->owner) ? NULL : eqe;
}

static inline void set_eqe_hw(struct mthca_eqe *eqe)
{
	eqe->owner =  MTHCA_EQ_ENTRY_OWNER_HW;
}

static void port_change(struct mthca_dev *dev, int port, int active)
{
	struct ib_event record;

	mthca_dbg(dev, "Port change to %s for port %d\n",
		  active ? "active" : "down", port);

	record.device = &dev->ib_dev;
	record.event  = active ? IB_EVENT_PORT_ACTIVE : IB_EVENT_PORT_ERR;
	record.element.port_num = port;

	ib_dispatch_event(&record);
}

static int mthca_eq_int(struct mthca_dev *dev, struct mthca_eq *eq)
{
	struct mthca_eqe *eqe;
	int disarm_cqn;
	int  eqes_found = 0;

	while ((eqe = next_eqe_sw(eq))) {
		int set_ci = 0;

		/*
		 * Make sure we read EQ entry contents after we've
		 * checked the ownership bit.
		 */
		rmb();

		switch (eqe->type) {
		case MTHCA_EVENT_TYPE_COMP:
			disarm_cqn = be32_to_cpu(eqe->event.comp.cqn) & 0xffffff;
			disarm_cq(dev, eq->eqn, disarm_cqn);
			mthca_cq_event(dev, disarm_cqn);
			break;

		case MTHCA_EVENT_TYPE_PATH_MIG:
			mthca_qp_event(dev, be32_to_cpu(eqe->event.qp.qpn) & 0xffffff,
				       IB_EVENT_PATH_MIG);
			break;

		case MTHCA_EVENT_TYPE_COMM_EST:
			mthca_qp_event(dev, be32_to_cpu(eqe->event.qp.qpn) & 0xffffff,
				       IB_EVENT_COMM_EST);
			break;

		case MTHCA_EVENT_TYPE_SQ_DRAINED:
			mthca_qp_event(dev, be32_to_cpu(eqe->event.qp.qpn) & 0xffffff,
				       IB_EVENT_SQ_DRAINED);
			break;

		case MTHCA_EVENT_TYPE_WQ_CATAS_ERROR:
			mthca_qp_event(dev, be32_to_cpu(eqe->event.qp.qpn) & 0xffffff,
				       IB_EVENT_QP_FATAL);
			break;

		case MTHCA_EVENT_TYPE_PATH_MIG_FAILED:
			mthca_qp_event(dev, be32_to_cpu(eqe->event.qp.qpn) & 0xffffff,
				       IB_EVENT_PATH_MIG_ERR);
			break;

		case MTHCA_EVENT_TYPE_WQ_INVAL_REQ_ERROR:
			mthca_qp_event(dev, be32_to_cpu(eqe->event.qp.qpn) & 0xffffff,
				       IB_EVENT_QP_REQ_ERR);
			break;

		case MTHCA_EVENT_TYPE_WQ_ACCESS_ERROR:
			mthca_qp_event(dev, be32_to_cpu(eqe->event.qp.qpn) & 0xffffff,
				       IB_EVENT_QP_ACCESS_ERR);
			break;

		case MTHCA_EVENT_TYPE_CMD:
			mthca_cmd_event(dev,
					be16_to_cpu(eqe->event.cmd.token),
					eqe->event.cmd.status,
					be64_to_cpu(eqe->event.cmd.out_param));
			/*
			 * cmd_event() may add more commands.
			 * The card will think the queue has overflowed if
			 * we don't tell it we've been processing events.
			 */
			set_ci = 1;
			break;

		case MTHCA_EVENT_TYPE_PORT_CHANGE:
			port_change(dev,
				    (be32_to_cpu(eqe->event.port_change.port) >> 28) & 3,
				    eqe->subtype == 0x4);
			break;

		case MTHCA_EVENT_TYPE_CQ_ERROR:
			mthca_warn(dev, "CQ %s on CQN %06x\n",
				   eqe->event.cq_err.syndrome == 1 ?
				   "overrun" : "access violation",
				   be32_to_cpu(eqe->event.cq_err.cqn) & 0xffffff);
			break;

		case MTHCA_EVENT_TYPE_EQ_OVERFLOW:
			mthca_warn(dev, "EQ overrun on EQN %d\n", eq->eqn);
			break;

		case MTHCA_EVENT_TYPE_EEC_CATAS_ERROR:
		case MTHCA_EVENT_TYPE_SRQ_CATAS_ERROR:
		case MTHCA_EVENT_TYPE_LOCAL_CATAS_ERROR:
		case MTHCA_EVENT_TYPE_ECC_DETECT:
		default:
			mthca_warn(dev, "Unhandled event %02x(%02x) on EQ %d\n",
				   eqe->type, eqe->subtype, eq->eqn);
			break;
		};

		set_eqe_hw(eqe);
		++eq->cons_index;
		eqes_found = 1;

		if (unlikely(set_ci)) {
			/*
			 * Conditional on hca_type is OK here because
			 * this is a rare case, not the fast path.
			 */
			set_eq_ci(dev, eq, eq->cons_index);
			set_ci = 0;
		}
	}

	/*
	 * Rely on caller to set consumer index so that we don't have
	 * to test hca_type in our interrupt handling fast path.
	 */
	return eqes_found;
}

static irqreturn_t mthca_tavor_interrupt(int irq, void *dev_ptr, struct pt_regs *regs)
{
	struct mthca_dev *dev = dev_ptr;
	u32 ecr;
	int i;

	if (dev->eq_table.clr_mask)
		writel(dev->eq_table.clr_mask, dev->eq_table.clr_int);

	ecr = readl(dev->eq_regs.tavor.ecr_base + 4);
	if (ecr) {
		writel(ecr, dev->eq_regs.tavor.ecr_base +
		       MTHCA_ECR_CLR_BASE - MTHCA_ECR_BASE + 4);

		for (i = 0; i < MTHCA_NUM_EQ; ++i)
			if (ecr & dev->eq_table.eq[i].eqn_mask &&
			    mthca_eq_int(dev, &dev->eq_table.eq[i])) {
				tavor_set_eq_ci(dev, &dev->eq_table.eq[i],
						dev->eq_table.eq[i].cons_index);
				tavor_eq_req_not(dev, dev->eq_table.eq[i].eqn);
			}
	}

	return IRQ_RETVAL(ecr);
}

static irqreturn_t mthca_tavor_msi_x_interrupt(int irq, void *eq_ptr,
					 struct pt_regs *regs)
{
	struct mthca_eq  *eq  = eq_ptr;
	struct mthca_dev *dev = eq->dev;

	mthca_eq_int(dev, eq);
	tavor_set_eq_ci(dev, eq, eq->cons_index);
	tavor_eq_req_not(dev, eq->eqn);

	/* MSI-X vectors always belong to us */
	return IRQ_HANDLED;
}

static irqreturn_t mthca_arbel_interrupt(int irq, void *dev_ptr, struct pt_regs *regs)
{
	struct mthca_dev *dev = dev_ptr;
	int work = 0;
	int i;

	if (dev->eq_table.clr_mask)
		writel(dev->eq_table.clr_mask, dev->eq_table.clr_int);

	for (i = 0; i < MTHCA_NUM_EQ; ++i)
		if (mthca_eq_int(dev, &dev->eq_table.eq[i])) {
			work = 1;
			arbel_set_eq_ci(dev, &dev->eq_table.eq[i],
					dev->eq_table.eq[i].cons_index);
		}

	arbel_eq_req_not(dev, dev->eq_table.arm_mask);

	return IRQ_RETVAL(work);
}

static irqreturn_t mthca_arbel_msi_x_interrupt(int irq, void *eq_ptr,
					       struct pt_regs *regs)
{
	struct mthca_eq  *eq  = eq_ptr;
	struct mthca_dev *dev = eq->dev;

	mthca_eq_int(dev, eq);
	arbel_set_eq_ci(dev, eq, eq->cons_index);
	arbel_eq_req_not(dev, eq->eqn_mask);

	/* MSI-X vectors always belong to us */
	return IRQ_HANDLED;
}

static int __devinit mthca_create_eq(struct mthca_dev *dev,
				     int nent,
				     u8 intr,
				     struct mthca_eq *eq)
{
	int npages = (nent * MTHCA_EQ_ENTRY_SIZE + PAGE_SIZE - 1) /
		PAGE_SIZE;
	u64 *dma_list = NULL;
	dma_addr_t t;
	struct mthca_mailbox *mailbox;
	struct mthca_eq_context *eq_context;
	int err = -ENOMEM;
	int i;
	u8 status;

	eq->dev  = dev;
	eq->nent = roundup_pow_of_two(max(nent, 2));

	eq->page_list = kmalloc(npages * sizeof *eq->page_list,
				GFP_KERNEL);
	if (!eq->page_list)
		goto err_out;

	for (i = 0; i < npages; ++i)
		eq->page_list[i].buf = NULL;

	dma_list = kmalloc(npages * sizeof *dma_list, GFP_KERNEL);
	if (!dma_list)
		goto err_out_free;

	mailbox = mthca_alloc_mailbox(dev, GFP_KERNEL);
	if (IS_ERR(mailbox))
		goto err_out_free;
	eq_context = mailbox->buf;

	for (i = 0; i < npages; ++i) {
		eq->page_list[i].buf = dma_alloc_coherent(&dev->pdev->dev,
							  PAGE_SIZE, &t, GFP_KERNEL);
		if (!eq->page_list[i].buf)
			goto err_out_free_pages;

		dma_list[i] = t;
		pci_unmap_addr_set(&eq->page_list[i], mapping, t);

		memset(eq->page_list[i].buf, 0, PAGE_SIZE);
	}

	for (i = 0; i < eq->nent; ++i)
		set_eqe_hw(get_eqe(eq, i));

	eq->eqn = mthca_alloc(&dev->eq_table.alloc);
	if (eq->eqn == -1)
		goto err_out_free_pages;

	err = mthca_mr_alloc_phys(dev, dev->driver_pd.pd_num,
				  dma_list, PAGE_SHIFT, npages,
				  0, npages * PAGE_SIZE,
				  MTHCA_MPT_FLAG_LOCAL_WRITE |
				  MTHCA_MPT_FLAG_LOCAL_READ,
				  &eq->mr);
	if (err)
		goto err_out_free_eq;

	memset(eq_context, 0, sizeof *eq_context);
	eq_context->flags           = cpu_to_be32(MTHCA_EQ_STATUS_OK   |
						  MTHCA_EQ_OWNER_HW    |
						  MTHCA_EQ_STATE_ARMED |
						  MTHCA_EQ_FLAG_TR);
	if (mthca_is_memfree(dev))
		eq_context->flags  |= cpu_to_be32(MTHCA_EQ_STATE_ARBEL);

	eq_context->logsize_usrpage = cpu_to_be32((ffs(eq->nent) - 1) << 24);
	if (mthca_is_memfree(dev)) {
		eq_context->arbel_pd = cpu_to_be32(dev->driver_pd.pd_num);
	} else {
		eq_context->logsize_usrpage |= cpu_to_be32(dev->driver_uar.index);
		eq_context->tavor_pd         = cpu_to_be32(dev->driver_pd.pd_num);
	}
	eq_context->intr            = intr;
	eq_context->lkey            = cpu_to_be32(eq->mr.ibmr.lkey);

	err = mthca_SW2HW_EQ(dev, mailbox, eq->eqn, &status);
	if (err) {
		mthca_warn(dev, "SW2HW_EQ failed (%d)\n", err);
		goto err_out_free_mr;
	}
	if (status) {
		mthca_warn(dev, "SW2HW_EQ returned status 0x%02x\n",
			   status);
		err = -EINVAL;
		goto err_out_free_mr;
	}

	kfree(dma_list);
	mthca_free_mailbox(dev, mailbox);

	eq->eqn_mask   = swab32(1 << eq->eqn);
	eq->cons_index = 0;

	dev->eq_table.arm_mask |= eq->eqn_mask;

	mthca_dbg(dev, "Allocated EQ %d with %d entries\n",
		  eq->eqn, eq->nent);

	return err;

 err_out_free_mr:
	mthca_free_mr(dev, &eq->mr);

 err_out_free_eq:
	mthca_free(&dev->eq_table.alloc, eq->eqn);

 err_out_free_pages:
	for (i = 0; i < npages; ++i)
		if (eq->page_list[i].buf)
			dma_free_coherent(&dev->pdev->dev, PAGE_SIZE,
					  eq->page_list[i].buf,
					  pci_unmap_addr(&eq->page_list[i],
							 mapping));

	mthca_free_mailbox(dev, mailbox);

 err_out_free:
	kfree(eq->page_list);
	kfree(dma_list);

 err_out:
	return err;
}

static void mthca_free_eq(struct mthca_dev *dev,
			  struct mthca_eq *eq)
{
	struct mthca_mailbox *mailbox;
	int err;
	u8 status;
	int npages = (eq->nent * MTHCA_EQ_ENTRY_SIZE + PAGE_SIZE - 1) /
		PAGE_SIZE;
	int i;

	mailbox = mthca_alloc_mailbox(dev, GFP_KERNEL);
	if (IS_ERR(mailbox))
		return;

	err = mthca_HW2SW_EQ(dev, mailbox, eq->eqn, &status);
	if (err)
		mthca_warn(dev, "HW2SW_EQ failed (%d)\n", err);
	if (status)
		mthca_warn(dev, "HW2SW_EQ returned status 0x%02x\n", status);

	dev->eq_table.arm_mask &= ~eq->eqn_mask;

	if (0) {
		mthca_dbg(dev, "Dumping EQ context %02x:\n", eq->eqn);
		for (i = 0; i < sizeof (struct mthca_eq_context) / 4; ++i) {
			if (i % 4 == 0)
				printk("[%02x] ", i * 4);
			printk(" %08x", be32_to_cpup(mailbox->buf + i * 4));
			if ((i + 1) % 4 == 0)
				printk("\n");
		}
	}

	mthca_free_mr(dev, &eq->mr);
	for (i = 0; i < npages; ++i)
		pci_free_consistent(dev->pdev, PAGE_SIZE,
				    eq->page_list[i].buf,
				    pci_unmap_addr(&eq->page_list[i], mapping));

	kfree(eq->page_list);
	mthca_free_mailbox(dev, mailbox);
}

static void mthca_free_irqs(struct mthca_dev *dev)
{
	int i;

	if (dev->eq_table.have_irq)
		free_irq(dev->pdev->irq, dev);
	for (i = 0; i < MTHCA_NUM_EQ; ++i)
		if (dev->eq_table.eq[i].have_irq)
			free_irq(dev->eq_table.eq[i].msi_x_vector,
				 dev->eq_table.eq + i);
}

static int __devinit mthca_map_reg(struct mthca_dev *dev,
				   unsigned long offset, unsigned long size,
				   void __iomem **map)
{
	unsigned long base = pci_resource_start(dev->pdev, 0);

	if (!request_mem_region(base + offset, size, DRV_NAME))
		return -EBUSY;

	*map = ioremap(base + offset, size);
	if (!*map) {
		release_mem_region(base + offset, size);
		return -ENOMEM;
	}

	return 0;
}

static void mthca_unmap_reg(struct mthca_dev *dev, unsigned long offset,
			    unsigned long size, void __iomem *map)
{
	unsigned long base = pci_resource_start(dev->pdev, 0);

	release_mem_region(base + offset, size);
	iounmap(map);
}

static int __devinit mthca_map_eq_regs(struct mthca_dev *dev)
{
	unsigned long mthca_base;

	mthca_base = pci_resource_start(dev->pdev, 0);

	if (mthca_is_memfree(dev)) {
		/*
		 * We assume that the EQ arm and EQ set CI registers
		 * fall within the first BAR.  We can't trust the
		 * values firmware gives us, since those addresses are
		 * valid on the HCA's side of the PCI bus but not
		 * necessarily the host side.
		 */
		if (mthca_map_reg(dev, (pci_resource_len(dev->pdev, 0) - 1) &
				  dev->fw.arbel.clr_int_base, MTHCA_CLR_INT_SIZE,
				  &dev->clr_base)) {
			mthca_err(dev, "Couldn't map interrupt clear register, "
				  "aborting.\n");
			return -ENOMEM;
		}

		/*
		 * Add 4 because we limit ourselves to EQs 0 ... 31,
		 * so we only need the low word of the register.
		 */
		if (mthca_map_reg(dev, ((pci_resource_len(dev->pdev, 0) - 1) &
					dev->fw.arbel.eq_arm_base) + 4, 4,
				  &dev->eq_regs.arbel.eq_arm)) {
			mthca_err(dev, "Couldn't map EQ arm register, aborting.\n");
			mthca_unmap_reg(dev, (pci_resource_len(dev->pdev, 0) - 1) &
					dev->fw.arbel.clr_int_base, MTHCA_CLR_INT_SIZE,
					dev->clr_base);
			return -ENOMEM;
		}

		if (mthca_map_reg(dev, (pci_resource_len(dev->pdev, 0) - 1) &
				  dev->fw.arbel.eq_set_ci_base,
				  MTHCA_EQ_SET_CI_SIZE,
				  &dev->eq_regs.arbel.eq_set_ci_base)) {
			mthca_err(dev, "Couldn't map EQ CI register, aborting.\n");
			mthca_unmap_reg(dev, ((pci_resource_len(dev->pdev, 0) - 1) &
					      dev->fw.arbel.eq_arm_base) + 4, 4,
					dev->eq_regs.arbel.eq_arm);
			mthca_unmap_reg(dev, (pci_resource_len(dev->pdev, 0) - 1) &
					dev->fw.arbel.clr_int_base, MTHCA_CLR_INT_SIZE,
					dev->clr_base);
			return -ENOMEM;
		}
	} else {
		if (mthca_map_reg(dev, MTHCA_CLR_INT_BASE, MTHCA_CLR_INT_SIZE,
				  &dev->clr_base)) {
			mthca_err(dev, "Couldn't map interrupt clear register, "
				  "aborting.\n");
			return -ENOMEM;
		}

		if (mthca_map_reg(dev, MTHCA_ECR_BASE,
				  MTHCA_ECR_SIZE + MTHCA_ECR_CLR_SIZE,
				  &dev->eq_regs.tavor.ecr_base)) {
			mthca_err(dev, "Couldn't map ecr register, "
				  "aborting.\n");
			mthca_unmap_reg(dev, MTHCA_CLR_INT_BASE, MTHCA_CLR_INT_SIZE,
					dev->clr_base);
			return -ENOMEM;
		}
	}

	return 0;

}

static void __devexit mthca_unmap_eq_regs(struct mthca_dev *dev)
{
	if (mthca_is_memfree(dev)) {
		mthca_unmap_reg(dev, (pci_resource_len(dev->pdev, 0) - 1) &
				dev->fw.arbel.eq_set_ci_base,
				MTHCA_EQ_SET_CI_SIZE,
				dev->eq_regs.arbel.eq_set_ci_base);
		mthca_unmap_reg(dev, ((pci_resource_len(dev->pdev, 0) - 1) &
				      dev->fw.arbel.eq_arm_base) + 4, 4,
				dev->eq_regs.arbel.eq_arm);
		mthca_unmap_reg(dev, (pci_resource_len(dev->pdev, 0) - 1) &
				dev->fw.arbel.clr_int_base, MTHCA_CLR_INT_SIZE,
				dev->clr_base);
	} else {
		mthca_unmap_reg(dev, MTHCA_ECR_BASE,
				MTHCA_ECR_SIZE + MTHCA_ECR_CLR_SIZE,
				dev->eq_regs.tavor.ecr_base);
		mthca_unmap_reg(dev, MTHCA_CLR_INT_BASE, MTHCA_CLR_INT_SIZE,
				dev->clr_base);
	}
}

int __devinit mthca_map_eq_icm(struct mthca_dev *dev, u64 icm_virt)
{
	int ret;
	u8 status;

	/*
	 * We assume that mapping one page is enough for the whole EQ
	 * context table.  This is fine with all current HCAs, because
	 * we only use 32 EQs and each EQ uses 32 bytes of context
	 * memory, or 1 KB total.
	 */
	dev->eq_table.icm_virt = icm_virt;
	dev->eq_table.icm_page = alloc_page(GFP_HIGHUSER);
	if (!dev->eq_table.icm_page)
		return -ENOMEM;
	dev->eq_table.icm_dma  = pci_map_page(dev->pdev, dev->eq_table.icm_page, 0,
					      PAGE_SIZE, PCI_DMA_BIDIRECTIONAL);
	if (pci_dma_mapping_error(dev->eq_table.icm_dma)) {
		__free_page(dev->eq_table.icm_page);
		return -ENOMEM;
	}

	ret = mthca_MAP_ICM_page(dev, dev->eq_table.icm_dma, icm_virt, &status);
	if (!ret && status)
		ret = -EINVAL;
	if (ret) {
		pci_unmap_page(dev->pdev, dev->eq_table.icm_dma, PAGE_SIZE,
			       PCI_DMA_BIDIRECTIONAL);
		__free_page(dev->eq_table.icm_page);
	}

	return ret;
}

void __devexit mthca_unmap_eq_icm(struct mthca_dev *dev)
{
	u8 status;

	mthca_UNMAP_ICM(dev, dev->eq_table.icm_virt, PAGE_SIZE / 4096, &status);
	pci_unmap_page(dev->pdev, dev->eq_table.icm_dma, PAGE_SIZE,
		       PCI_DMA_BIDIRECTIONAL);
	__free_page(dev->eq_table.icm_page);
}

int __devinit mthca_init_eq_table(struct mthca_dev *dev)
{
	int err;
	u8 status;
	u8 intr;
	int i;

	err = mthca_alloc_init(&dev->eq_table.alloc,
			       dev->limits.num_eqs,
			       dev->limits.num_eqs - 1,
			       dev->limits.reserved_eqs);
	if (err)
		return err;

	err = mthca_map_eq_regs(dev);
	if (err)
		goto err_out_free;

	if (dev->mthca_flags & MTHCA_FLAG_MSI ||
	    dev->mthca_flags & MTHCA_FLAG_MSI_X) {
		dev->eq_table.clr_mask = 0;
	} else {
		dev->eq_table.clr_mask =
			swab32(1 << (dev->eq_table.inta_pin & 31));
		dev->eq_table.clr_int  = dev->clr_base +
			(dev->eq_table.inta_pin < 32 ? 4 : 0);
	}

	dev->eq_table.arm_mask = 0;

	intr = (dev->mthca_flags & MTHCA_FLAG_MSI) ?
		128 : dev->eq_table.inta_pin;

	err = mthca_create_eq(dev, dev->limits.num_cqs,
			      (dev->mthca_flags & MTHCA_FLAG_MSI_X) ? 128 : intr,
			      &dev->eq_table.eq[MTHCA_EQ_COMP]);
	if (err)
		goto err_out_unmap;

	err = mthca_create_eq(dev, MTHCA_NUM_ASYNC_EQE,
			      (dev->mthca_flags & MTHCA_FLAG_MSI_X) ? 129 : intr,
			      &dev->eq_table.eq[MTHCA_EQ_ASYNC]);
	if (err)
		goto err_out_comp;

	err = mthca_create_eq(dev, MTHCA_NUM_CMD_EQE,
			      (dev->mthca_flags & MTHCA_FLAG_MSI_X) ? 130 : intr,
			      &dev->eq_table.eq[MTHCA_EQ_CMD]);
	if (err)
		goto err_out_async;

	if (dev->mthca_flags & MTHCA_FLAG_MSI_X) {
		static const char *eq_name[] = {
			[MTHCA_EQ_COMP]  = DRV_NAME " (comp)",
			[MTHCA_EQ_ASYNC] = DRV_NAME " (async)",
			[MTHCA_EQ_CMD]   = DRV_NAME " (cmd)"
		};

		for (i = 0; i < MTHCA_NUM_EQ; ++i) {
			err = request_irq(dev->eq_table.eq[i].msi_x_vector,
					  mthca_is_memfree(dev) ?
					  mthca_arbel_msi_x_interrupt :
					  mthca_tavor_msi_x_interrupt,
					  0, eq_name[i], dev->eq_table.eq + i);
			if (err)
				goto err_out_cmd;
			dev->eq_table.eq[i].have_irq = 1;
		}
	} else {
		err = request_irq(dev->pdev->irq,
				  mthca_is_memfree(dev) ?
				  mthca_arbel_interrupt :
				  mthca_tavor_interrupt,
				  SA_SHIRQ, DRV_NAME, dev);
		if (err)
			goto err_out_cmd;
		dev->eq_table.have_irq = 1;
	}

	err = mthca_MAP_EQ(dev, async_mask(dev),
			   0, dev->eq_table.eq[MTHCA_EQ_ASYNC].eqn, &status);
	if (err)
		mthca_warn(dev, "MAP_EQ for async EQ %d failed (%d)\n",
			   dev->eq_table.eq[MTHCA_EQ_ASYNC].eqn, err);
	if (status)
		mthca_warn(dev, "MAP_EQ for async EQ %d returned status 0x%02x\n",
			   dev->eq_table.eq[MTHCA_EQ_ASYNC].eqn, status);

	err = mthca_MAP_EQ(dev, MTHCA_CMD_EVENT_MASK,
			   0, dev->eq_table.eq[MTHCA_EQ_CMD].eqn, &status);
	if (err)
		mthca_warn(dev, "MAP_EQ for cmd EQ %d failed (%d)\n",
			   dev->eq_table.eq[MTHCA_EQ_CMD].eqn, err);
	if (status)
		mthca_warn(dev, "MAP_EQ for cmd EQ %d returned status 0x%02x\n",
			   dev->eq_table.eq[MTHCA_EQ_CMD].eqn, status);

	for (i = 0; i < MTHCA_EQ_CMD; ++i)
		if (mthca_is_memfree(dev))
			arbel_eq_req_not(dev, dev->eq_table.eq[i].eqn_mask);
		else
			tavor_eq_req_not(dev, dev->eq_table.eq[i].eqn);

	return 0;

err_out_cmd:
	mthca_free_irqs(dev);
	mthca_free_eq(dev, &dev->eq_table.eq[MTHCA_EQ_CMD]);

err_out_async:
	mthca_free_eq(dev, &dev->eq_table.eq[MTHCA_EQ_ASYNC]);

err_out_comp:
	mthca_free_eq(dev, &dev->eq_table.eq[MTHCA_EQ_COMP]);

err_out_unmap:
	mthca_unmap_eq_regs(dev);

err_out_free:
	mthca_alloc_cleanup(&dev->eq_table.alloc);
	return err;
}

void __devexit mthca_cleanup_eq_table(struct mthca_dev *dev)
{
	u8 status;
	int i;

	mthca_free_irqs(dev);

	mthca_MAP_EQ(dev, async_mask(dev),
		     1, dev->eq_table.eq[MTHCA_EQ_ASYNC].eqn, &status);
	mthca_MAP_EQ(dev, MTHCA_CMD_EVENT_MASK,
		     1, dev->eq_table.eq[MTHCA_EQ_CMD].eqn, &status);

	for (i = 0; i < MTHCA_NUM_EQ; ++i)
		mthca_free_eq(dev, &dev->eq_table.eq[i]);

	mthca_unmap_eq_regs(dev);

	mthca_alloc_cleanup(&dev->eq_table.alloc);
}
