/*
 * Copyright (c) 2005, 2006, 2007, 2008 Mellanox Technologies. All rights reserved.
 * Copyright (c) 2005, 2006, 2007 Cisco Systems, Inc. All rights reserved.
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
 *	- Redistributions of source code must retain the above
 *	  copyright notice, this list of conditions and the following
 *	  disclaimer.
 *
 *	- Redistributions in binary form must reproduce the above
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

#include <linux/interrupt.h>
#include <linux/slab.h>
#include <linux/export.h>
#include <linux/mm.h>
#include <linux/dma-mapping.h>

#include <linux/mlx4/cmd.h>

#include "mlx4.h"
#include "fw.h"

enum {
	MLX4_IRQNAME_SIZE	= 32
};

enum {
	MLX4_NUM_ASYNC_EQE	= 0x100,
	MLX4_NUM_SPARE_EQE	= 0x80,
	MLX4_EQ_ENTRY_SIZE	= 0x20
};

/*
 * Must be packed because start is 64 bits but only aligned to 32 bits.
 */
struct mlx4_eq_context {
	__be32			flags;
	u16			reserved1[3];
	__be16			page_offset;
	u8			log_eq_size;
	u8			reserved2[4];
	u8			eq_period;
	u8			reserved3;
	u8			eq_max_count;
	u8			reserved4[3];
	u8			intr;
	u8			log_page_size;
	u8			reserved5[2];
	u8			mtt_base_addr_h;
	__be32			mtt_base_addr_l;
	u32			reserved6[2];
	__be32			consumer_index;
	__be32			producer_index;
	u32			reserved7[4];
};

#define MLX4_EQ_STATUS_OK	   ( 0 << 28)
#define MLX4_EQ_STATUS_WRITE_FAIL  (10 << 28)
#define MLX4_EQ_OWNER_SW	   ( 0 << 24)
#define MLX4_EQ_OWNER_HW	   ( 1 << 24)
#define MLX4_EQ_FLAG_EC		   ( 1 << 18)
#define MLX4_EQ_FLAG_OI		   ( 1 << 17)
#define MLX4_EQ_STATE_ARMED	   ( 9 <<  8)
#define MLX4_EQ_STATE_FIRED	   (10 <<  8)
#define MLX4_EQ_STATE_ALWAYS_ARMED (11 <<  8)

#define MLX4_ASYNC_EVENT_MASK ((1ull << MLX4_EVENT_TYPE_PATH_MIG)	    | \
			       (1ull << MLX4_EVENT_TYPE_COMM_EST)	    | \
			       (1ull << MLX4_EVENT_TYPE_SQ_DRAINED)	    | \
			       (1ull << MLX4_EVENT_TYPE_CQ_ERROR)	    | \
			       (1ull << MLX4_EVENT_TYPE_WQ_CATAS_ERROR)	    | \
			       (1ull << MLX4_EVENT_TYPE_EEC_CATAS_ERROR)    | \
			       (1ull << MLX4_EVENT_TYPE_PATH_MIG_FAILED)    | \
			       (1ull << MLX4_EVENT_TYPE_WQ_INVAL_REQ_ERROR) | \
			       (1ull << MLX4_EVENT_TYPE_WQ_ACCESS_ERROR)    | \
			       (1ull << MLX4_EVENT_TYPE_PORT_CHANGE)	    | \
			       (1ull << MLX4_EVENT_TYPE_ECC_DETECT)	    | \
			       (1ull << MLX4_EVENT_TYPE_SRQ_CATAS_ERROR)    | \
			       (1ull << MLX4_EVENT_TYPE_SRQ_QP_LAST_WQE)    | \
			       (1ull << MLX4_EVENT_TYPE_SRQ_LIMIT)	    | \
			       (1ull << MLX4_EVENT_TYPE_CMD))

struct mlx4_eqe {
	u8			reserved1;
	u8			type;
	u8			reserved2;
	u8			subtype;
	union {
		u32		raw[6];
		struct {
			__be32	cqn;
		} __packed comp;
		struct {
			u16	reserved1;
			__be16	token;
			u32	reserved2;
			u8	reserved3[3];
			u8	status;
			__be64	out_param;
		} __packed cmd;
		struct {
			__be32	qpn;
		} __packed qp;
		struct {
			__be32	srqn;
		} __packed srq;
		struct {
			__be32	cqn;
			u32	reserved1;
			u8	reserved2[3];
			u8	syndrome;
		} __packed cq_err;
		struct {
			u32	reserved1[2];
			__be32	port;
		} __packed port_change;
	}			event;
	u8			reserved3[3];
	u8			owner;
} __packed;

static void eq_set_ci(struct mlx4_eq *eq, int req_not)
{
	__raw_writel((__force u32) cpu_to_be32((eq->cons_index & 0xffffff) |
					       req_not << 31),
		     eq->doorbell);
	/* We still want ordering, just not swabbing, so add a barrier */
	mb();
}

static struct mlx4_eqe *get_eqe(struct mlx4_eq *eq, u32 entry)
{
	unsigned long off = (entry & (eq->nent - 1)) * MLX4_EQ_ENTRY_SIZE;
	return eq->page_list[off / PAGE_SIZE].buf + off % PAGE_SIZE;
}

static struct mlx4_eqe *next_eqe_sw(struct mlx4_eq *eq)
{
	struct mlx4_eqe *eqe = get_eqe(eq, eq->cons_index);
	return !!(eqe->owner & 0x80) ^ !!(eq->cons_index & eq->nent) ? NULL : eqe;
}

static int mlx4_eq_int(struct mlx4_dev *dev, struct mlx4_eq *eq)
{
	struct mlx4_eqe *eqe;
	int cqn;
	int eqes_found = 0;
	int set_ci = 0;
	int port;

	while ((eqe = next_eqe_sw(eq))) {
		/*
		 * Make sure we read EQ entry contents after we've
		 * checked the ownership bit.
		 */
		rmb();

		switch (eqe->type) {
		case MLX4_EVENT_TYPE_COMP:
			cqn = be32_to_cpu(eqe->event.comp.cqn) & 0xffffff;
			mlx4_cq_completion(dev, cqn);
			break;

		case MLX4_EVENT_TYPE_PATH_MIG:
		case MLX4_EVENT_TYPE_COMM_EST:
		case MLX4_EVENT_TYPE_SQ_DRAINED:
		case MLX4_EVENT_TYPE_SRQ_QP_LAST_WQE:
		case MLX4_EVENT_TYPE_WQ_CATAS_ERROR:
		case MLX4_EVENT_TYPE_PATH_MIG_FAILED:
		case MLX4_EVENT_TYPE_WQ_INVAL_REQ_ERROR:
		case MLX4_EVENT_TYPE_WQ_ACCESS_ERROR:
			mlx4_qp_event(dev, be32_to_cpu(eqe->event.qp.qpn) & 0xffffff,
				      eqe->type);
			break;

		case MLX4_EVENT_TYPE_SRQ_LIMIT:
		case MLX4_EVENT_TYPE_SRQ_CATAS_ERROR:
			mlx4_srq_event(dev, be32_to_cpu(eqe->event.srq.srqn) & 0xffffff,
				      eqe->type);
			break;

		case MLX4_EVENT_TYPE_CMD:
			mlx4_cmd_event(dev,
				       be16_to_cpu(eqe->event.cmd.token),
				       eqe->event.cmd.status,
				       be64_to_cpu(eqe->event.cmd.out_param));
			break;

		case MLX4_EVENT_TYPE_PORT_CHANGE:
			port = be32_to_cpu(eqe->event.port_change.port) >> 28;
			if (eqe->subtype == MLX4_PORT_CHANGE_SUBTYPE_DOWN) {
				mlx4_dispatch_event(dev, MLX4_DEV_EVENT_PORT_DOWN,
						    port);
				mlx4_priv(dev)->sense.do_sense_port[port] = 1;
			} else {
				mlx4_dispatch_event(dev, MLX4_DEV_EVENT_PORT_UP,
						    port);
				mlx4_priv(dev)->sense.do_sense_port[port] = 0;
			}
			break;

		case MLX4_EVENT_TYPE_CQ_ERROR:
			mlx4_warn(dev, "CQ %s on CQN %06x\n",
				  eqe->event.cq_err.syndrome == 1 ?
				  "overrun" : "access violation",
				  be32_to_cpu(eqe->event.cq_err.cqn) & 0xffffff);
			mlx4_cq_event(dev, be32_to_cpu(eqe->event.cq_err.cqn),
				      eqe->type);
			break;

		case MLX4_EVENT_TYPE_EQ_OVERFLOW:
			mlx4_warn(dev, "EQ overrun on EQN %d\n", eq->eqn);
			break;

		case MLX4_EVENT_TYPE_EEC_CATAS_ERROR:
		case MLX4_EVENT_TYPE_ECC_DETECT:
		default:
			mlx4_warn(dev, "Unhandled event %02x(%02x) on EQ %d at index %u\n",
				  eqe->type, eqe->subtype, eq->eqn, eq->cons_index);
			break;
		}

		++eq->cons_index;
		eqes_found = 1;
		++set_ci;

		/*
		 * The HCA will think the queue has overflowed if we
		 * don't tell it we've been processing events.  We
		 * create our EQs with MLX4_NUM_SPARE_EQE extra
		 * entries, so we must update our consumer index at
		 * least that often.
		 */
		if (unlikely(set_ci >= MLX4_NUM_SPARE_EQE)) {
			eq_set_ci(eq, 0);
			set_ci = 0;
		}
	}

	eq_set_ci(eq, 1);

	return eqes_found;
}

static irqreturn_t mlx4_interrupt(int irq, void *dev_ptr)
{
	struct mlx4_dev *dev = dev_ptr;
	struct mlx4_priv *priv = mlx4_priv(dev);
	int work = 0;
	int i;

	writel(priv->eq_table.clr_mask, priv->eq_table.clr_int);

	for (i = 0; i < dev->caps.num_comp_vectors + 1; ++i)
		work |= mlx4_eq_int(dev, &priv->eq_table.eq[i]);

	return IRQ_RETVAL(work);
}

static irqreturn_t mlx4_msi_x_interrupt(int irq, void *eq_ptr)
{
	struct mlx4_eq  *eq  = eq_ptr;
	struct mlx4_dev *dev = eq->dev;

	mlx4_eq_int(dev, eq);

	/* MSI-X vectors always belong to us */
	return IRQ_HANDLED;
}

static int mlx4_MAP_EQ(struct mlx4_dev *dev, u64 event_mask, int unmap,
			int eq_num)
{
	return mlx4_cmd(dev, event_mask, (unmap << 31) | eq_num,
			0, MLX4_CMD_MAP_EQ, MLX4_CMD_TIME_CLASS_B);
}

static int mlx4_SW2HW_EQ(struct mlx4_dev *dev, struct mlx4_cmd_mailbox *mailbox,
			 int eq_num)
{
	return mlx4_cmd(dev, mailbox->dma, eq_num, 0, MLX4_CMD_SW2HW_EQ,
			MLX4_CMD_TIME_CLASS_A);
}

static int mlx4_HW2SW_EQ(struct mlx4_dev *dev, struct mlx4_cmd_mailbox *mailbox,
			 int eq_num)
{
	return mlx4_cmd_box(dev, 0, mailbox->dma, eq_num, 0, MLX4_CMD_HW2SW_EQ,
			    MLX4_CMD_TIME_CLASS_A);
}

static int mlx4_num_eq_uar(struct mlx4_dev *dev)
{
	/*
	 * Each UAR holds 4 EQ doorbells.  To figure out how many UARs
	 * we need to map, take the difference of highest index and
	 * the lowest index we'll use and add 1.
	 */
	return (dev->caps.num_comp_vectors + 1 + dev->caps.reserved_eqs +
		 dev->caps.comp_pool)/4 - dev->caps.reserved_eqs/4 + 1;
}

static void __iomem *mlx4_get_eq_uar(struct mlx4_dev *dev, struct mlx4_eq *eq)
{
	struct mlx4_priv *priv = mlx4_priv(dev);
	int index;

	index = eq->eqn / 4 - dev->caps.reserved_eqs / 4;

	if (!priv->eq_table.uar_map[index]) {
		priv->eq_table.uar_map[index] =
			ioremap(pci_resource_start(dev->pdev, 2) +
				((eq->eqn / 4) << PAGE_SHIFT),
				PAGE_SIZE);
		if (!priv->eq_table.uar_map[index]) {
			mlx4_err(dev, "Couldn't map EQ doorbell for EQN 0x%06x\n",
				 eq->eqn);
			return NULL;
		}
	}

	return priv->eq_table.uar_map[index] + 0x800 + 8 * (eq->eqn % 4);
}

static int mlx4_create_eq(struct mlx4_dev *dev, int nent,
			  u8 intr, struct mlx4_eq *eq)
{
	struct mlx4_priv *priv = mlx4_priv(dev);
	struct mlx4_cmd_mailbox *mailbox;
	struct mlx4_eq_context *eq_context;
	int npages;
	u64 *dma_list = NULL;
	dma_addr_t t;
	u64 mtt_addr;
	int err = -ENOMEM;
	int i;

	eq->dev   = dev;
	eq->nent  = roundup_pow_of_two(max(nent, 2));
	npages = PAGE_ALIGN(eq->nent * MLX4_EQ_ENTRY_SIZE) / PAGE_SIZE;

	eq->page_list = kmalloc(npages * sizeof *eq->page_list,
				GFP_KERNEL);
	if (!eq->page_list)
		goto err_out;

	for (i = 0; i < npages; ++i)
		eq->page_list[i].buf = NULL;

	dma_list = kmalloc(npages * sizeof *dma_list, GFP_KERNEL);
	if (!dma_list)
		goto err_out_free;

	mailbox = mlx4_alloc_cmd_mailbox(dev);
	if (IS_ERR(mailbox))
		goto err_out_free;
	eq_context = mailbox->buf;

	for (i = 0; i < npages; ++i) {
		eq->page_list[i].buf = dma_alloc_coherent(&dev->pdev->dev,
							  PAGE_SIZE, &t, GFP_KERNEL);
		if (!eq->page_list[i].buf)
			goto err_out_free_pages;

		dma_list[i] = t;
		eq->page_list[i].map = t;

		memset(eq->page_list[i].buf, 0, PAGE_SIZE);
	}

	eq->eqn = mlx4_bitmap_alloc(&priv->eq_table.bitmap);
	if (eq->eqn == -1)
		goto err_out_free_pages;

	eq->doorbell = mlx4_get_eq_uar(dev, eq);
	if (!eq->doorbell) {
		err = -ENOMEM;
		goto err_out_free_eq;
	}

	err = mlx4_mtt_init(dev, npages, PAGE_SHIFT, &eq->mtt);
	if (err)
		goto err_out_free_eq;

	err = mlx4_write_mtt(dev, &eq->mtt, 0, npages, dma_list);
	if (err)
		goto err_out_free_mtt;

	memset(eq_context, 0, sizeof *eq_context);
	eq_context->flags	  = cpu_to_be32(MLX4_EQ_STATUS_OK   |
						MLX4_EQ_STATE_ARMED);
	eq_context->log_eq_size	  = ilog2(eq->nent);
	eq_context->intr	  = intr;
	eq_context->log_page_size = PAGE_SHIFT - MLX4_ICM_PAGE_SHIFT;

	mtt_addr = mlx4_mtt_addr(dev, &eq->mtt);
	eq_context->mtt_base_addr_h = mtt_addr >> 32;
	eq_context->mtt_base_addr_l = cpu_to_be32(mtt_addr & 0xffffffff);

	err = mlx4_SW2HW_EQ(dev, mailbox, eq->eqn);
	if (err) {
		mlx4_warn(dev, "SW2HW_EQ failed (%d)\n", err);
		goto err_out_free_mtt;
	}

	kfree(dma_list);
	mlx4_free_cmd_mailbox(dev, mailbox);

	eq->cons_index = 0;

	return err;

err_out_free_mtt:
	mlx4_mtt_cleanup(dev, &eq->mtt);

err_out_free_eq:
	mlx4_bitmap_free(&priv->eq_table.bitmap, eq->eqn);

err_out_free_pages:
	for (i = 0; i < npages; ++i)
		if (eq->page_list[i].buf)
			dma_free_coherent(&dev->pdev->dev, PAGE_SIZE,
					  eq->page_list[i].buf,
					  eq->page_list[i].map);

	mlx4_free_cmd_mailbox(dev, mailbox);

err_out_free:
	kfree(eq->page_list);
	kfree(dma_list);

err_out:
	return err;
}

static void mlx4_free_eq(struct mlx4_dev *dev,
			 struct mlx4_eq *eq)
{
	struct mlx4_priv *priv = mlx4_priv(dev);
	struct mlx4_cmd_mailbox *mailbox;
	int err;
	int npages = PAGE_ALIGN(MLX4_EQ_ENTRY_SIZE * eq->nent) / PAGE_SIZE;
	int i;

	mailbox = mlx4_alloc_cmd_mailbox(dev);
	if (IS_ERR(mailbox))
		return;

	err = mlx4_HW2SW_EQ(dev, mailbox, eq->eqn);
	if (err)
		mlx4_warn(dev, "HW2SW_EQ failed (%d)\n", err);

	if (0) {
		mlx4_dbg(dev, "Dumping EQ context %02x:\n", eq->eqn);
		for (i = 0; i < sizeof (struct mlx4_eq_context) / 4; ++i) {
			if (i % 4 == 0)
				pr_cont("[%02x] ", i * 4);
			pr_cont(" %08x", be32_to_cpup(mailbox->buf + i * 4));
			if ((i + 1) % 4 == 0)
				pr_cont("\n");
		}
	}

	mlx4_mtt_cleanup(dev, &eq->mtt);
	for (i = 0; i < npages; ++i)
		dma_free_coherent(&dev->pdev->dev, PAGE_SIZE,
				    eq->page_list[i].buf,
				    eq->page_list[i].map);

	kfree(eq->page_list);
	mlx4_bitmap_free(&priv->eq_table.bitmap, eq->eqn);
	mlx4_free_cmd_mailbox(dev, mailbox);
}

static void mlx4_free_irqs(struct mlx4_dev *dev)
{
	struct mlx4_eq_table *eq_table = &mlx4_priv(dev)->eq_table;
	struct mlx4_priv *priv = mlx4_priv(dev);
	int	i, vec;

	if (eq_table->have_irq)
		free_irq(dev->pdev->irq, dev);

	for (i = 0; i < dev->caps.num_comp_vectors + 1; ++i)
		if (eq_table->eq[i].have_irq) {
			free_irq(eq_table->eq[i].irq, eq_table->eq + i);
			eq_table->eq[i].have_irq = 0;
		}

	for (i = 0; i < dev->caps.comp_pool; i++) {
		/*
		 * Freeing the assigned irq's
		 * all bits should be 0, but we need to validate
		 */
		if (priv->msix_ctl.pool_bm & 1ULL << i) {
			/* NO need protecting*/
			vec = dev->caps.num_comp_vectors + 1 + i;
			free_irq(priv->eq_table.eq[vec].irq,
				 &priv->eq_table.eq[vec]);
		}
	}


	kfree(eq_table->irq_names);
}

static int mlx4_map_clr_int(struct mlx4_dev *dev)
{
	struct mlx4_priv *priv = mlx4_priv(dev);

	priv->clr_base = ioremap(pci_resource_start(dev->pdev, priv->fw.clr_int_bar) +
				 priv->fw.clr_int_base, MLX4_CLR_INT_SIZE);
	if (!priv->clr_base) {
		mlx4_err(dev, "Couldn't map interrupt clear register, aborting.\n");
		return -ENOMEM;
	}

	return 0;
}

static void mlx4_unmap_clr_int(struct mlx4_dev *dev)
{
	struct mlx4_priv *priv = mlx4_priv(dev);

	iounmap(priv->clr_base);
}

int mlx4_alloc_eq_table(struct mlx4_dev *dev)
{
	struct mlx4_priv *priv = mlx4_priv(dev);

	priv->eq_table.eq = kcalloc(dev->caps.num_eqs - dev->caps.reserved_eqs,
				    sizeof *priv->eq_table.eq, GFP_KERNEL);
	if (!priv->eq_table.eq)
		return -ENOMEM;

	return 0;
}

void mlx4_free_eq_table(struct mlx4_dev *dev)
{
	kfree(mlx4_priv(dev)->eq_table.eq);
}

int mlx4_init_eq_table(struct mlx4_dev *dev)
{
	struct mlx4_priv *priv = mlx4_priv(dev);
	int err;
	int i;

	priv->eq_table.uar_map = kcalloc(sizeof *priv->eq_table.uar_map,
					 mlx4_num_eq_uar(dev), GFP_KERNEL);
	if (!priv->eq_table.uar_map) {
		err = -ENOMEM;
		goto err_out_free;
	}

	err = mlx4_bitmap_init(&priv->eq_table.bitmap, dev->caps.num_eqs,
			       dev->caps.num_eqs - 1, dev->caps.reserved_eqs, 0);
	if (err)
		goto err_out_free;

	for (i = 0; i < mlx4_num_eq_uar(dev); ++i)
		priv->eq_table.uar_map[i] = NULL;

	err = mlx4_map_clr_int(dev);
	if (err)
		goto err_out_bitmap;

	priv->eq_table.clr_mask =
		swab32(1 << (priv->eq_table.inta_pin & 31));
	priv->eq_table.clr_int  = priv->clr_base +
		(priv->eq_table.inta_pin < 32 ? 4 : 0);

	priv->eq_table.irq_names =
		kmalloc(MLX4_IRQNAME_SIZE * (dev->caps.num_comp_vectors + 1 +
					     dev->caps.comp_pool),
			GFP_KERNEL);
	if (!priv->eq_table.irq_names) {
		err = -ENOMEM;
		goto err_out_bitmap;
	}

	for (i = 0; i < dev->caps.num_comp_vectors; ++i) {
		err = mlx4_create_eq(dev, dev->caps.num_cqs -
					  dev->caps.reserved_cqs +
					  MLX4_NUM_SPARE_EQE,
				     (dev->flags & MLX4_FLAG_MSI_X) ? i : 0,
				     &priv->eq_table.eq[i]);
		if (err) {
			--i;
			goto err_out_unmap;
		}
	}

	err = mlx4_create_eq(dev, MLX4_NUM_ASYNC_EQE + MLX4_NUM_SPARE_EQE,
			     (dev->flags & MLX4_FLAG_MSI_X) ? dev->caps.num_comp_vectors : 0,
			     &priv->eq_table.eq[dev->caps.num_comp_vectors]);
	if (err)
		goto err_out_comp;

	/*if additional completion vectors poolsize is 0 this loop will not run*/
	for (i = dev->caps.num_comp_vectors + 1;
	      i < dev->caps.num_comp_vectors + dev->caps.comp_pool + 1; ++i) {

		err = mlx4_create_eq(dev, dev->caps.num_cqs -
					  dev->caps.reserved_cqs +
					  MLX4_NUM_SPARE_EQE,
				     (dev->flags & MLX4_FLAG_MSI_X) ? i : 0,
				     &priv->eq_table.eq[i]);
		if (err) {
			--i;
			goto err_out_unmap;
		}
	}


	if (dev->flags & MLX4_FLAG_MSI_X) {
		const char *eq_name;

		for (i = 0; i < dev->caps.num_comp_vectors + 1; ++i) {
			if (i < dev->caps.num_comp_vectors) {
				snprintf(priv->eq_table.irq_names +
					 i * MLX4_IRQNAME_SIZE,
					 MLX4_IRQNAME_SIZE,
					 "mlx4-comp-%d@pci:%s", i,
					 pci_name(dev->pdev));
			} else {
				snprintf(priv->eq_table.irq_names +
					 i * MLX4_IRQNAME_SIZE,
					 MLX4_IRQNAME_SIZE,
					 "mlx4-async@pci:%s",
					 pci_name(dev->pdev));
			}

			eq_name = priv->eq_table.irq_names +
				  i * MLX4_IRQNAME_SIZE;
			err = request_irq(priv->eq_table.eq[i].irq,
					  mlx4_msi_x_interrupt, 0, eq_name,
					  priv->eq_table.eq + i);
			if (err)
				goto err_out_async;

			priv->eq_table.eq[i].have_irq = 1;
		}
	} else {
		snprintf(priv->eq_table.irq_names,
			 MLX4_IRQNAME_SIZE,
			 DRV_NAME "@pci:%s",
			 pci_name(dev->pdev));
		err = request_irq(dev->pdev->irq, mlx4_interrupt,
				  IRQF_SHARED, priv->eq_table.irq_names, dev);
		if (err)
			goto err_out_async;

		priv->eq_table.have_irq = 1;
	}

	err = mlx4_MAP_EQ(dev, MLX4_ASYNC_EVENT_MASK, 0,
			  priv->eq_table.eq[dev->caps.num_comp_vectors].eqn);
	if (err)
		mlx4_warn(dev, "MAP_EQ for async EQ %d failed (%d)\n",
			   priv->eq_table.eq[dev->caps.num_comp_vectors].eqn, err);

	for (i = 0; i < dev->caps.num_comp_vectors + 1; ++i)
		eq_set_ci(&priv->eq_table.eq[i], 1);

	return 0;

err_out_async:
	mlx4_free_eq(dev, &priv->eq_table.eq[dev->caps.num_comp_vectors]);

err_out_comp:
	i = dev->caps.num_comp_vectors - 1;

err_out_unmap:
	while (i >= 0) {
		mlx4_free_eq(dev, &priv->eq_table.eq[i]);
		--i;
	}
	mlx4_unmap_clr_int(dev);
	mlx4_free_irqs(dev);

err_out_bitmap:
	mlx4_bitmap_cleanup(&priv->eq_table.bitmap);

err_out_free:
	kfree(priv->eq_table.uar_map);

	return err;
}

void mlx4_cleanup_eq_table(struct mlx4_dev *dev)
{
	struct mlx4_priv *priv = mlx4_priv(dev);
	int i;

	mlx4_MAP_EQ(dev, MLX4_ASYNC_EVENT_MASK, 1,
		    priv->eq_table.eq[dev->caps.num_comp_vectors].eqn);

	mlx4_free_irqs(dev);

	for (i = 0; i < dev->caps.num_comp_vectors + dev->caps.comp_pool + 1; ++i)
		mlx4_free_eq(dev, &priv->eq_table.eq[i]);

	mlx4_unmap_clr_int(dev);

	for (i = 0; i < mlx4_num_eq_uar(dev); ++i)
		if (priv->eq_table.uar_map[i])
			iounmap(priv->eq_table.uar_map[i]);

	mlx4_bitmap_cleanup(&priv->eq_table.bitmap);

	kfree(priv->eq_table.uar_map);
}

/* A test that verifies that we can accept interrupts on all
 * the irq vectors of the device.
 * Interrupts are checked using the NOP command.
 */
int mlx4_test_interrupts(struct mlx4_dev *dev)
{
	struct mlx4_priv *priv = mlx4_priv(dev);
	int i;
	int err;

	err = mlx4_NOP(dev);
	/* When not in MSI_X, there is only one irq to check */
	if (!(dev->flags & MLX4_FLAG_MSI_X))
		return err;

	/* A loop over all completion vectors, for each vector we will check
	 * whether it works by mapping command completions to that vector
	 * and performing a NOP command
	 */
	for(i = 0; !err && (i < dev->caps.num_comp_vectors); ++i) {
		/* Temporary use polling for command completions */
		mlx4_cmd_use_polling(dev);

		/* Map the new eq to handle all asyncronous events */
		err = mlx4_MAP_EQ(dev, MLX4_ASYNC_EVENT_MASK, 0,
				  priv->eq_table.eq[i].eqn);
		if (err) {
			mlx4_warn(dev, "Failed mapping eq for interrupt test\n");
			mlx4_cmd_use_events(dev);
			break;
		}

		/* Go back to using events */
		mlx4_cmd_use_events(dev);
		err = mlx4_NOP(dev);
	}

	/* Return to default */
	mlx4_MAP_EQ(dev, MLX4_ASYNC_EVENT_MASK, 0,
		    priv->eq_table.eq[dev->caps.num_comp_vectors].eqn);
	return err;
}
EXPORT_SYMBOL(mlx4_test_interrupts);

int mlx4_assign_eq(struct mlx4_dev *dev, char* name, int * vector)
{

	struct mlx4_priv *priv = mlx4_priv(dev);
	int vec = 0, err = 0, i;

	spin_lock(&priv->msix_ctl.pool_lock);
	for (i = 0; !vec && i < dev->caps.comp_pool; i++) {
		if (~priv->msix_ctl.pool_bm & 1ULL << i) {
			priv->msix_ctl.pool_bm |= 1ULL << i;
			vec = dev->caps.num_comp_vectors + 1 + i;
			snprintf(priv->eq_table.irq_names +
					vec * MLX4_IRQNAME_SIZE,
					MLX4_IRQNAME_SIZE, "%s", name);
			err = request_irq(priv->eq_table.eq[vec].irq,
					  mlx4_msi_x_interrupt, 0,
					  &priv->eq_table.irq_names[vec<<5],
					  priv->eq_table.eq + vec);
			if (err) {
				/*zero out bit by fliping it*/
				priv->msix_ctl.pool_bm ^= 1 << i;
				vec = 0;
				continue;
				/*we dont want to break here*/
			}
			eq_set_ci(&priv->eq_table.eq[vec], 1);
		}
	}
	spin_unlock(&priv->msix_ctl.pool_lock);

	if (vec) {
		*vector = vec;
	} else {
		*vector = 0;
		err = (i == dev->caps.comp_pool) ? -ENOSPC : err;
	}
	return err;
}
EXPORT_SYMBOL(mlx4_assign_eq);

void mlx4_release_eq(struct mlx4_dev *dev, int vec)
{
	struct mlx4_priv *priv = mlx4_priv(dev);
	/*bm index*/
	int i = vec - dev->caps.num_comp_vectors - 1;

	if (likely(i >= 0)) {
		/*sanity check , making sure were not trying to free irq's
		  Belonging to a legacy EQ*/
		spin_lock(&priv->msix_ctl.pool_lock);
		if (priv->msix_ctl.pool_bm & 1ULL << i) {
			free_irq(priv->eq_table.eq[vec].irq,
				 &priv->eq_table.eq[vec]);
			priv->msix_ctl.pool_bm &= ~(1ULL << i);
		}
		spin_unlock(&priv->msix_ctl.pool_lock);
	}

}
EXPORT_SYMBOL(mlx4_release_eq);

