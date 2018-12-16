/*
 * Copyright (c) 2013-2015, Mellanox Technologies. All rights reserved.
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

#include <linux/interrupt.h>
#include <linux/notifier.h>
#include <linux/module.h>
#include <linux/mlx5/driver.h>
#include <linux/mlx5/vport.h>
#include <linux/mlx5/eq.h>
#include <linux/mlx5/cmd.h>
#ifdef CONFIG_RFS_ACCEL
#include <linux/cpu_rmap.h>
#endif
#include "mlx5_core.h"
#include "lib/eq.h"
#include "fpga/core.h"
#include "eswitch.h"
#include "lib/clock.h"
#include "diag/fw_tracer.h"

enum {
	MLX5_EQE_OWNER_INIT_VAL	= 0x1,
};

enum {
	MLX5_EQ_STATE_ARMED		= 0x9,
	MLX5_EQ_STATE_FIRED		= 0xa,
	MLX5_EQ_STATE_ALWAYS_ARMED	= 0xb,
};

enum {
	MLX5_EQ_DOORBEL_OFFSET	= 0x40,
};

struct mlx5_irq_info {
	cpumask_var_t mask;
	char name[MLX5_MAX_IRQ_NAME];
	void *context; /* dev_id provided to request_irq */
};

struct mlx5_eq_table {
	struct list_head        comp_eqs_list;
	struct mlx5_eq          pages_eq;
	struct mlx5_eq	        cmd_eq;
	struct mlx5_eq          async_eq;

	struct atomic_notifier_head nh[MLX5_EVENT_TYPE_MAX];

	/* Since CQ DB is stored in async_eq */
	struct mlx5_nb          cq_err_nb;

	struct mutex            lock; /* sync async eqs creations */
	int			num_comp_vectors;
	struct mlx5_irq_info	*irq_info;
#ifdef CONFIG_RFS_ACCEL
	struct cpu_rmap         *rmap;
#endif
};

#define MLX5_ASYNC_EVENT_MASK ((1ull << MLX5_EVENT_TYPE_PATH_MIG)	    | \
			       (1ull << MLX5_EVENT_TYPE_COMM_EST)	    | \
			       (1ull << MLX5_EVENT_TYPE_SQ_DRAINED)	    | \
			       (1ull << MLX5_EVENT_TYPE_CQ_ERROR)	    | \
			       (1ull << MLX5_EVENT_TYPE_WQ_CATAS_ERROR)	    | \
			       (1ull << MLX5_EVENT_TYPE_PATH_MIG_FAILED)    | \
			       (1ull << MLX5_EVENT_TYPE_WQ_INVAL_REQ_ERROR) | \
			       (1ull << MLX5_EVENT_TYPE_WQ_ACCESS_ERROR)    | \
			       (1ull << MLX5_EVENT_TYPE_PORT_CHANGE)	    | \
			       (1ull << MLX5_EVENT_TYPE_SRQ_CATAS_ERROR)    | \
			       (1ull << MLX5_EVENT_TYPE_SRQ_LAST_WQE)	    | \
			       (1ull << MLX5_EVENT_TYPE_SRQ_RQ_LIMIT))

static int mlx5_cmd_destroy_eq(struct mlx5_core_dev *dev, u8 eqn)
{
	u32 out[MLX5_ST_SZ_DW(destroy_eq_out)] = {0};
	u32 in[MLX5_ST_SZ_DW(destroy_eq_in)]   = {0};

	MLX5_SET(destroy_eq_in, in, opcode, MLX5_CMD_OP_DESTROY_EQ);
	MLX5_SET(destroy_eq_in, in, eq_number, eqn);
	return mlx5_cmd_exec(dev, in, sizeof(in), out, sizeof(out));
}

/* caller must eventually call mlx5_cq_put on the returned cq */
static struct mlx5_core_cq *mlx5_eq_cq_get(struct mlx5_eq *eq, u32 cqn)
{
	struct mlx5_cq_table *table = &eq->cq_table;
	struct mlx5_core_cq *cq = NULL;

	rcu_read_lock();
	cq = radix_tree_lookup(&table->tree, cqn);
	if (likely(cq))
		mlx5_cq_hold(cq);
	rcu_read_unlock();

	return cq;
}

static irqreturn_t mlx5_eq_comp_int(int irq, void *eq_ptr)
{
	struct mlx5_eq_comp *eq_comp = eq_ptr;
	struct mlx5_eq *eq = eq_ptr;
	struct mlx5_eqe *eqe;
	int set_ci = 0;
	u32 cqn = -1;

	while ((eqe = next_eqe_sw(eq))) {
		struct mlx5_core_cq *cq;
		/* Make sure we read EQ entry contents after we've
		 * checked the ownership bit.
		 */
		dma_rmb();
		/* Assume (eqe->type) is always MLX5_EVENT_TYPE_COMP */
		cqn = be32_to_cpu(eqe->data.comp.cqn) & 0xffffff;

		cq = mlx5_eq_cq_get(eq, cqn);
		if (likely(cq)) {
			++cq->arm_sn;
			cq->comp(cq);
			mlx5_cq_put(cq);
		} else {
			mlx5_core_warn(eq->dev, "Completion event for bogus CQ 0x%x\n", cqn);
		}

		++eq->cons_index;
		++set_ci;

		/* The HCA will think the queue has overflowed if we
		 * don't tell it we've been processing events.  We
		 * create our EQs with MLX5_NUM_SPARE_EQE extra
		 * entries, so we must update our consumer index at
		 * least that often.
		 */
		if (unlikely(set_ci >= MLX5_NUM_SPARE_EQE)) {
			eq_update_ci(eq, 0);
			set_ci = 0;
		}
	}

	eq_update_ci(eq, 1);

	if (cqn != -1)
		tasklet_schedule(&eq_comp->tasklet_ctx.task);

	return IRQ_HANDLED;
}

/* Some architectures don't latch interrupts when they are disabled, so using
 * mlx5_eq_poll_irq_disabled could end up losing interrupts while trying to
 * avoid losing them.  It is not recommended to use it, unless this is the last
 * resort.
 */
u32 mlx5_eq_poll_irq_disabled(struct mlx5_eq_comp *eq)
{
	u32 count_eqe;

	disable_irq(eq->core.irqn);
	count_eqe = eq->core.cons_index;
	mlx5_eq_comp_int(eq->core.irqn, eq);
	count_eqe = eq->core.cons_index - count_eqe;
	enable_irq(eq->core.irqn);

	return count_eqe;
}

static irqreturn_t mlx5_eq_async_int(int irq, void *eq_ptr)
{
	struct mlx5_eq *eq = eq_ptr;
	struct mlx5_eq_table *eqt;
	struct mlx5_core_dev *dev;
	struct mlx5_eqe *eqe;
	int set_ci = 0;

	dev = eq->dev;
	eqt = dev->priv.eq_table;

	while ((eqe = next_eqe_sw(eq))) {
		/*
		 * Make sure we read EQ entry contents after we've
		 * checked the ownership bit.
		 */
		dma_rmb();

		if (likely(eqe->type < MLX5_EVENT_TYPE_MAX))
			atomic_notifier_call_chain(&eqt->nh[eqe->type], eqe->type, eqe);
		else
			mlx5_core_warn_once(dev, "notifier_call_chain is not setup for eqe: %d\n", eqe->type);

		atomic_notifier_call_chain(&eqt->nh[MLX5_EVENT_TYPE_NOTIFY_ANY], eqe->type, eqe);

		++eq->cons_index;
		++set_ci;

		/* The HCA will think the queue has overflowed if we
		 * don't tell it we've been processing events.  We
		 * create our EQs with MLX5_NUM_SPARE_EQE extra
		 * entries, so we must update our consumer index at
		 * least that often.
		 */
		if (unlikely(set_ci >= MLX5_NUM_SPARE_EQE)) {
			eq_update_ci(eq, 0);
			set_ci = 0;
		}
	}

	eq_update_ci(eq, 1);

	return IRQ_HANDLED;
}

static void init_eq_buf(struct mlx5_eq *eq)
{
	struct mlx5_eqe *eqe;
	int i;

	for (i = 0; i < eq->nent; i++) {
		eqe = get_eqe(eq, i);
		eqe->owner = MLX5_EQE_OWNER_INIT_VAL;
	}
}

static int
create_map_eq(struct mlx5_core_dev *dev, struct mlx5_eq *eq, const char *name,
	      struct mlx5_eq_param *param)
{
	struct mlx5_eq_table *eq_table = dev->priv.eq_table;
	struct mlx5_cq_table *cq_table = &eq->cq_table;
	u32 out[MLX5_ST_SZ_DW(create_eq_out)] = {0};
	struct mlx5_priv *priv = &dev->priv;
	u8 vecidx = param->index;
	__be64 *pas;
	void *eqc;
	int inlen;
	u32 *in;
	int err;

	if (eq_table->irq_info[vecidx].context)
		return -EEXIST;

	/* Init CQ table */
	memset(cq_table, 0, sizeof(*cq_table));
	spin_lock_init(&cq_table->lock);
	INIT_RADIX_TREE(&cq_table->tree, GFP_ATOMIC);

	eq->nent = roundup_pow_of_two(param->nent + MLX5_NUM_SPARE_EQE);
	eq->cons_index = 0;
	err = mlx5_buf_alloc(dev, eq->nent * MLX5_EQE_SIZE, &eq->buf);
	if (err)
		return err;

	init_eq_buf(eq);

	inlen = MLX5_ST_SZ_BYTES(create_eq_in) +
		MLX5_FLD_SZ_BYTES(create_eq_in, pas[0]) * eq->buf.npages;

	in = kvzalloc(inlen, GFP_KERNEL);
	if (!in) {
		err = -ENOMEM;
		goto err_buf;
	}

	pas = (__be64 *)MLX5_ADDR_OF(create_eq_in, in, pas);
	mlx5_fill_page_array(&eq->buf, pas);

	MLX5_SET(create_eq_in, in, opcode, MLX5_CMD_OP_CREATE_EQ);
	MLX5_SET64(create_eq_in, in, event_bitmask, param->mask);

	eqc = MLX5_ADDR_OF(create_eq_in, in, eq_context_entry);
	MLX5_SET(eqc, eqc, log_eq_size, ilog2(eq->nent));
	MLX5_SET(eqc, eqc, uar_page, priv->uar->index);
	MLX5_SET(eqc, eqc, intr, vecidx);
	MLX5_SET(eqc, eqc, log_page_size,
		 eq->buf.page_shift - MLX5_ADAPTER_PAGE_SHIFT);

	err = mlx5_cmd_exec(dev, in, inlen, out, sizeof(out));
	if (err)
		goto err_in;

	snprintf(eq_table->irq_info[vecidx].name, MLX5_MAX_IRQ_NAME, "%s@pci:%s",
		 name, pci_name(dev->pdev));
	eq_table->irq_info[vecidx].context = param->context;

	eq->vecidx = vecidx;
	eq->eqn = MLX5_GET(create_eq_out, out, eq_number);
	eq->irqn = pci_irq_vector(dev->pdev, vecidx);
	eq->dev = dev;
	eq->doorbell = priv->uar->map + MLX5_EQ_DOORBEL_OFFSET;
	err = request_irq(eq->irqn, param->handler, 0,
			  eq_table->irq_info[vecidx].name, param->context);
	if (err)
		goto err_eq;

	err = mlx5_debug_eq_add(dev, eq);
	if (err)
		goto err_irq;

	/* EQs are created in ARMED state
	 */
	eq_update_ci(eq, 1);

	kvfree(in);
	return 0;

err_irq:
	free_irq(eq->irqn, eq);

err_eq:
	mlx5_cmd_destroy_eq(dev, eq->eqn);

err_in:
	kvfree(in);

err_buf:
	mlx5_buf_free(dev, &eq->buf);
	return err;
}

static int destroy_unmap_eq(struct mlx5_core_dev *dev, struct mlx5_eq *eq)
{
	struct mlx5_eq_table *eq_table = dev->priv.eq_table;
	struct mlx5_irq_info *irq_info;
	int err;

	irq_info = &eq_table->irq_info[eq->vecidx];

	mlx5_debug_eq_remove(dev, eq);

	free_irq(eq->irqn, irq_info->context);
	irq_info->context = NULL;

	err = mlx5_cmd_destroy_eq(dev, eq->eqn);
	if (err)
		mlx5_core_warn(dev, "failed to destroy a previously created eq: eqn %d\n",
			       eq->eqn);
	synchronize_irq(eq->irqn);

	mlx5_buf_free(dev, &eq->buf);

	return err;
}

int mlx5_eq_add_cq(struct mlx5_eq *eq, struct mlx5_core_cq *cq)
{
	struct mlx5_cq_table *table = &eq->cq_table;
	int err;

	spin_lock(&table->lock);
	err = radix_tree_insert(&table->tree, cq->cqn, cq);
	spin_unlock(&table->lock);

	return err;
}

int mlx5_eq_del_cq(struct mlx5_eq *eq, struct mlx5_core_cq *cq)
{
	struct mlx5_cq_table *table = &eq->cq_table;
	struct mlx5_core_cq *tmp;

	spin_lock(&table->lock);
	tmp = radix_tree_delete(&table->tree, cq->cqn);
	spin_unlock(&table->lock);

	if (!tmp) {
		mlx5_core_warn(eq->dev, "cq 0x%x not found in eq 0x%x tree\n", eq->eqn, cq->cqn);
		return -ENOENT;
	}

	if (tmp != cq) {
		mlx5_core_warn(eq->dev, "corruption on cqn 0x%x in eq 0x%x\n", eq->eqn, cq->cqn);
		return -EINVAL;
	}

	return 0;
}

int mlx5_eq_table_init(struct mlx5_core_dev *dev)
{
	struct mlx5_eq_table *eq_table;
	int i, err;

	eq_table = kvzalloc(sizeof(*eq_table), GFP_KERNEL);
	if (!eq_table)
		return -ENOMEM;

	dev->priv.eq_table = eq_table;

	err = mlx5_eq_debugfs_init(dev);
	if (err)
		goto kvfree_eq_table;

	mutex_init(&eq_table->lock);
	for (i = 0; i < MLX5_EVENT_TYPE_MAX; i++)
		ATOMIC_INIT_NOTIFIER_HEAD(&eq_table->nh[i]);

	return 0;

kvfree_eq_table:
	kvfree(eq_table);
	dev->priv.eq_table = NULL;
	return err;
}

void mlx5_eq_table_cleanup(struct mlx5_core_dev *dev)
{
	mlx5_eq_debugfs_cleanup(dev);
	kvfree(dev->priv.eq_table);
}

/* Async EQs */

static int create_async_eq(struct mlx5_core_dev *dev, const char *name,
			   struct mlx5_eq *eq, struct mlx5_eq_param *param)
{
	struct mlx5_eq_table *eq_table = dev->priv.eq_table;
	int err;

	mutex_lock(&eq_table->lock);
	if (param->index >= MLX5_EQ_MAX_ASYNC_EQS) {
		err = -ENOSPC;
		goto unlock;
	}

	err = create_map_eq(dev, eq, name, param);
unlock:
	mutex_unlock(&eq_table->lock);
	return err;
}

static int destroy_async_eq(struct mlx5_core_dev *dev, struct mlx5_eq *eq)
{
	struct mlx5_eq_table *eq_table = dev->priv.eq_table;
	int err;

	mutex_lock(&eq_table->lock);
	err = destroy_unmap_eq(dev, eq);
	mutex_unlock(&eq_table->lock);
	return err;
}

static int cq_err_event_notifier(struct notifier_block *nb,
				 unsigned long type, void *data)
{
	struct mlx5_eq_table *eqt;
	struct mlx5_core_cq *cq;
	struct mlx5_eqe *eqe;
	struct mlx5_eq *eq;
	u32 cqn;

	/* type == MLX5_EVENT_TYPE_CQ_ERROR */

	eqt = mlx5_nb_cof(nb, struct mlx5_eq_table, cq_err_nb);
	eq  = &eqt->async_eq;
	eqe = data;

	cqn = be32_to_cpu(eqe->data.cq_err.cqn) & 0xffffff;
	mlx5_core_warn(eq->dev, "CQ error on CQN 0x%x, syndrome 0x%x\n",
		       cqn, eqe->data.cq_err.syndrome);

	cq = mlx5_eq_cq_get(eq, cqn);
	if (unlikely(!cq)) {
		mlx5_core_warn(eq->dev, "Async event for bogus CQ 0x%x\n", cqn);
		return NOTIFY_OK;
	}

	cq->event(cq, type);

	mlx5_cq_put(cq);

	return NOTIFY_OK;
}

static u64 gather_async_events_mask(struct mlx5_core_dev *dev)
{
	u64 async_event_mask = MLX5_ASYNC_EVENT_MASK;

	if (MLX5_VPORT_MANAGER(dev))
		async_event_mask |= (1ull << MLX5_EVENT_TYPE_NIC_VPORT_CHANGE);

	if (MLX5_CAP_GEN(dev, port_type) == MLX5_CAP_PORT_TYPE_ETH &&
	    MLX5_CAP_GEN(dev, general_notification_event))
		async_event_mask |= (1ull << MLX5_EVENT_TYPE_GENERAL_EVENT);

	if (MLX5_CAP_GEN(dev, port_module_event))
		async_event_mask |= (1ull << MLX5_EVENT_TYPE_PORT_MODULE_EVENT);
	else
		mlx5_core_dbg(dev, "port_module_event is not set\n");

	if (MLX5_PPS_CAP(dev))
		async_event_mask |= (1ull << MLX5_EVENT_TYPE_PPS_EVENT);

	if (MLX5_CAP_GEN(dev, fpga))
		async_event_mask |= (1ull << MLX5_EVENT_TYPE_FPGA_ERROR) |
				    (1ull << MLX5_EVENT_TYPE_FPGA_QP_ERROR);
	if (MLX5_CAP_GEN_MAX(dev, dct))
		async_event_mask |= (1ull << MLX5_EVENT_TYPE_DCT_DRAINED);

	if (MLX5_CAP_GEN(dev, temp_warn_event))
		async_event_mask |= (1ull << MLX5_EVENT_TYPE_TEMP_WARN_EVENT);

	if (MLX5_CAP_MCAM_REG(dev, tracer_registers))
		async_event_mask |= (1ull << MLX5_EVENT_TYPE_DEVICE_TRACER);

	if (MLX5_CAP_GEN(dev, max_num_of_monitor_counters))
		async_event_mask |= (1ull << MLX5_EVENT_TYPE_MONITOR_COUNTER);

	if (mlx5_core_is_ecpf_esw_manager(dev))
		async_event_mask |= (1ull << MLX5_EVENT_TYPE_HOST_PARAMS_CHANGE);

	return async_event_mask;
}

static int create_async_eqs(struct mlx5_core_dev *dev)
{
	struct mlx5_eq_table *table = dev->priv.eq_table;
	struct mlx5_eq_param param = {};
	int err;

	MLX5_NB_INIT(&table->cq_err_nb, cq_err_event_notifier, CQ_ERROR);
	mlx5_eq_notifier_register(dev, &table->cq_err_nb);

	param = (struct mlx5_eq_param) {
		.index = MLX5_EQ_CMD_IDX,
		.mask = 1ull << MLX5_EVENT_TYPE_CMD,
		.nent = MLX5_NUM_CMD_EQE,
		.context = &table->cmd_eq,
		.handler = mlx5_eq_async_int,
	};
	err = create_async_eq(dev, "mlx5_cmd_eq", &table->cmd_eq, &param);
	if (err) {
		mlx5_core_warn(dev, "failed to create cmd EQ %d\n", err);
		goto err0;
	}

	mlx5_cmd_use_events(dev);

	param = (struct mlx5_eq_param) {
		.index = MLX5_EQ_ASYNC_IDX,
		.mask = gather_async_events_mask(dev),
		.nent = MLX5_NUM_ASYNC_EQE,
		.context = &table->async_eq,
		.handler = mlx5_eq_async_int,
	};
	err = create_async_eq(dev, "mlx5_async_eq", &table->async_eq, &param);
	if (err) {
		mlx5_core_warn(dev, "failed to create async EQ %d\n", err);
		goto err1;
	}

	param = (struct mlx5_eq_param) {
		.index = MLX5_EQ_PAGEREQ_IDX,
		.mask =  1 << MLX5_EVENT_TYPE_PAGE_REQUEST,
		.nent = /* TODO: sriov max_vf + */ 1,
		.context = &table->pages_eq,
		.handler = mlx5_eq_async_int,
	};
	err = create_async_eq(dev, "mlx5_pages_eq", &table->pages_eq, &param);
	if (err) {
		mlx5_core_warn(dev, "failed to create pages EQ %d\n", err);
		goto err2;
	}

	return err;

err2:
	destroy_async_eq(dev, &table->async_eq);

err1:
	mlx5_cmd_use_polling(dev);
	destroy_async_eq(dev, &table->cmd_eq);
err0:
	mlx5_eq_notifier_unregister(dev, &table->cq_err_nb);
	return err;
}

static void destroy_async_eqs(struct mlx5_core_dev *dev)
{
	struct mlx5_eq_table *table = dev->priv.eq_table;
	int err;

	err = destroy_async_eq(dev, &table->pages_eq);
	if (err)
		mlx5_core_err(dev, "failed to destroy pages eq, err(%d)\n",
			      err);

	err = destroy_async_eq(dev, &table->async_eq);
	if (err)
		mlx5_core_err(dev, "failed to destroy async eq, err(%d)\n",
			      err);

	mlx5_cmd_use_polling(dev);

	err = destroy_async_eq(dev, &table->cmd_eq);
	if (err)
		mlx5_core_err(dev, "failed to destroy command eq, err(%d)\n",
			      err);

	mlx5_eq_notifier_unregister(dev, &table->cq_err_nb);
}

struct mlx5_eq *mlx5_get_async_eq(struct mlx5_core_dev *dev)
{
	return &dev->priv.eq_table->async_eq;
}

void mlx5_eq_synchronize_async_irq(struct mlx5_core_dev *dev)
{
	synchronize_irq(dev->priv.eq_table->async_eq.irqn);
}

void mlx5_eq_synchronize_cmd_irq(struct mlx5_core_dev *dev)
{
	synchronize_irq(dev->priv.eq_table->cmd_eq.irqn);
}

/* Generic EQ API for mlx5_core consumers
 * Needed For RDMA ODP EQ for now
 */
struct mlx5_eq *
mlx5_eq_create_generic(struct mlx5_core_dev *dev, const char *name,
		       struct mlx5_eq_param *param)
{
	struct mlx5_eq *eq = kvzalloc(sizeof(*eq), GFP_KERNEL);
	int err;

	if (!eq)
		return ERR_PTR(-ENOMEM);

	err = create_async_eq(dev, name, eq, param);
	if (err) {
		kvfree(eq);
		eq = ERR_PTR(err);
	}

	return eq;
}
EXPORT_SYMBOL(mlx5_eq_create_generic);

int mlx5_eq_destroy_generic(struct mlx5_core_dev *dev, struct mlx5_eq *eq)
{
	int err;

	if (IS_ERR(eq))
		return -EINVAL;

	err = destroy_async_eq(dev, eq);
	if (err)
		goto out;

	kvfree(eq);
out:
	return err;
}
EXPORT_SYMBOL(mlx5_eq_destroy_generic);

struct mlx5_eqe *mlx5_eq_get_eqe(struct mlx5_eq *eq, u32 cc)
{
	u32 ci = eq->cons_index + cc;
	struct mlx5_eqe *eqe;

	eqe = get_eqe(eq, ci & (eq->nent - 1));
	eqe = ((eqe->owner & 1) ^ !!(ci & eq->nent)) ? NULL : eqe;
	/* Make sure we read EQ entry contents after we've
	 * checked the ownership bit.
	 */
	if (eqe)
		dma_rmb();

	return eqe;
}
EXPORT_SYMBOL(mlx5_eq_get_eqe);

void mlx5_eq_update_ci(struct mlx5_eq *eq, u32 cc, bool arm)
{
	__be32 __iomem *addr = eq->doorbell + (arm ? 0 : 2);
	u32 val;

	eq->cons_index += cc;
	val = (eq->cons_index & 0xffffff) | (eq->eqn << 24);

	__raw_writel((__force u32)cpu_to_be32(val), addr);
	/* We still want ordering, just not swabbing, so add a barrier */
	wmb();
}
EXPORT_SYMBOL(mlx5_eq_update_ci);

/* Completion EQs */

static int set_comp_irq_affinity_hint(struct mlx5_core_dev *mdev, int i)
{
	struct mlx5_priv *priv  = &mdev->priv;
	int vecidx = MLX5_EQ_VEC_COMP_BASE + i;
	int irq = pci_irq_vector(mdev->pdev, vecidx);
	struct mlx5_irq_info *irq_info = &priv->eq_table->irq_info[vecidx];

	if (!zalloc_cpumask_var(&irq_info->mask, GFP_KERNEL)) {
		mlx5_core_warn(mdev, "zalloc_cpumask_var failed");
		return -ENOMEM;
	}

	cpumask_set_cpu(cpumask_local_spread(i, priv->numa_node),
			irq_info->mask);

	if (IS_ENABLED(CONFIG_SMP) &&
	    irq_set_affinity_hint(irq, irq_info->mask))
		mlx5_core_warn(mdev, "irq_set_affinity_hint failed, irq 0x%.4x", irq);

	return 0;
}

static void clear_comp_irq_affinity_hint(struct mlx5_core_dev *mdev, int i)
{
	int vecidx = MLX5_EQ_VEC_COMP_BASE + i;
	struct mlx5_priv *priv  = &mdev->priv;
	int irq = pci_irq_vector(mdev->pdev, vecidx);
	struct mlx5_irq_info *irq_info = &priv->eq_table->irq_info[vecidx];

	irq_set_affinity_hint(irq, NULL);
	free_cpumask_var(irq_info->mask);
}

static int set_comp_irq_affinity_hints(struct mlx5_core_dev *mdev)
{
	int err;
	int i;

	for (i = 0; i < mdev->priv.eq_table->num_comp_vectors; i++) {
		err = set_comp_irq_affinity_hint(mdev, i);
		if (err)
			goto err_out;
	}

	return 0;

err_out:
	for (i--; i >= 0; i--)
		clear_comp_irq_affinity_hint(mdev, i);

	return err;
}

static void clear_comp_irqs_affinity_hints(struct mlx5_core_dev *mdev)
{
	int i;

	for (i = 0; i < mdev->priv.eq_table->num_comp_vectors; i++)
		clear_comp_irq_affinity_hint(mdev, i);
}

static void destroy_comp_eqs(struct mlx5_core_dev *dev)
{
	struct mlx5_eq_table *table = dev->priv.eq_table;
	struct mlx5_eq_comp *eq, *n;

	clear_comp_irqs_affinity_hints(dev);

#ifdef CONFIG_RFS_ACCEL
	if (table->rmap) {
		free_irq_cpu_rmap(table->rmap);
		table->rmap = NULL;
	}
#endif
	list_for_each_entry_safe(eq, n, &table->comp_eqs_list, list) {
		list_del(&eq->list);
		if (destroy_unmap_eq(dev, &eq->core))
			mlx5_core_warn(dev, "failed to destroy comp EQ 0x%x\n",
				       eq->core.eqn);
		tasklet_disable(&eq->tasklet_ctx.task);
		kfree(eq);
	}
}

static int create_comp_eqs(struct mlx5_core_dev *dev)
{
	struct mlx5_eq_table *table = dev->priv.eq_table;
	char name[MLX5_MAX_IRQ_NAME];
	struct mlx5_eq_comp *eq;
	int ncomp_vec;
	int nent;
	int err;
	int i;

	INIT_LIST_HEAD(&table->comp_eqs_list);
	ncomp_vec = table->num_comp_vectors;
	nent = MLX5_COMP_EQ_SIZE;
#ifdef CONFIG_RFS_ACCEL
	table->rmap = alloc_irq_cpu_rmap(ncomp_vec);
	if (!table->rmap)
		return -ENOMEM;
#endif
	for (i = 0; i < ncomp_vec; i++) {
		int vecidx = i + MLX5_EQ_VEC_COMP_BASE;
		struct mlx5_eq_param param = {};

		eq = kzalloc(sizeof(*eq), GFP_KERNEL);
		if (!eq) {
			err = -ENOMEM;
			goto clean;
		}

		INIT_LIST_HEAD(&eq->tasklet_ctx.list);
		INIT_LIST_HEAD(&eq->tasklet_ctx.process_list);
		spin_lock_init(&eq->tasklet_ctx.lock);
		tasklet_init(&eq->tasklet_ctx.task, mlx5_cq_tasklet_cb,
			     (unsigned long)&eq->tasklet_ctx);

#ifdef CONFIG_RFS_ACCEL
		irq_cpu_rmap_add(table->rmap, pci_irq_vector(dev->pdev, vecidx));
#endif
		snprintf(name, MLX5_MAX_IRQ_NAME, "mlx5_comp%d", i);
		param = (struct mlx5_eq_param) {
			.index = vecidx,
			.mask = 0,
			.nent = nent,
			.context = &eq->core,
			.handler = mlx5_eq_comp_int
		};
		err = create_map_eq(dev, &eq->core, name, &param);
		if (err) {
			kfree(eq);
			goto clean;
		}
		mlx5_core_dbg(dev, "allocated completion EQN %d\n", eq->core.eqn);
		/* add tail, to keep the list ordered, for mlx5_vector2eqn to work */
		list_add_tail(&eq->list, &table->comp_eqs_list);
	}

	err = set_comp_irq_affinity_hints(dev);
	if (err) {
		mlx5_core_err(dev, "Failed to alloc affinity hint cpumask\n");
		goto clean;
	}

	return 0;

clean:
	destroy_comp_eqs(dev);
	return err;
}

int mlx5_vector2eqn(struct mlx5_core_dev *dev, int vector, int *eqn,
		    unsigned int *irqn)
{
	struct mlx5_eq_table *table = dev->priv.eq_table;
	struct mlx5_eq_comp *eq, *n;
	int err = -ENOENT;
	int i = 0;

	list_for_each_entry_safe(eq, n, &table->comp_eqs_list, list) {
		if (i++ == vector) {
			*eqn = eq->core.eqn;
			*irqn = eq->core.irqn;
			err = 0;
			break;
		}
	}

	return err;
}
EXPORT_SYMBOL(mlx5_vector2eqn);

unsigned int mlx5_comp_vectors_count(struct mlx5_core_dev *dev)
{
	return dev->priv.eq_table->num_comp_vectors;
}
EXPORT_SYMBOL(mlx5_comp_vectors_count);

struct cpumask *
mlx5_comp_irq_get_affinity_mask(struct mlx5_core_dev *dev, int vector)
{
	/* TODO: consider irq_get_affinity_mask(irq) */
	return dev->priv.eq_table->irq_info[vector + MLX5_EQ_VEC_COMP_BASE].mask;
}
EXPORT_SYMBOL(mlx5_comp_irq_get_affinity_mask);

#ifdef CONFIG_RFS_ACCEL
struct cpu_rmap *mlx5_eq_table_get_rmap(struct mlx5_core_dev *dev)
{
	return dev->priv.eq_table->rmap;
}
#endif

struct mlx5_eq_comp *mlx5_eqn2comp_eq(struct mlx5_core_dev *dev, int eqn)
{
	struct mlx5_eq_table *table = dev->priv.eq_table;
	struct mlx5_eq_comp *eq;

	list_for_each_entry(eq, &table->comp_eqs_list, list) {
		if (eq->core.eqn == eqn)
			return eq;
	}

	return ERR_PTR(-ENOENT);
}

/* This function should only be called after mlx5_cmd_force_teardown_hca */
void mlx5_core_eq_free_irqs(struct mlx5_core_dev *dev)
{
	struct mlx5_eq_table *table = dev->priv.eq_table;
	int i, max_eqs;

	clear_comp_irqs_affinity_hints(dev);

#ifdef CONFIG_RFS_ACCEL
	if (table->rmap) {
		free_irq_cpu_rmap(table->rmap);
		table->rmap = NULL;
	}
#endif

	mutex_lock(&table->lock); /* sync with create/destroy_async_eq */
	max_eqs = table->num_comp_vectors + MLX5_EQ_VEC_COMP_BASE;
	for (i = max_eqs - 1; i >= 0; i--) {
		if (!table->irq_info[i].context)
			continue;
		free_irq(pci_irq_vector(dev->pdev, i), table->irq_info[i].context);
		table->irq_info[i].context = NULL;
	}
	mutex_unlock(&table->lock);
	pci_free_irq_vectors(dev->pdev);
}

static int alloc_irq_vectors(struct mlx5_core_dev *dev)
{
	struct mlx5_priv *priv = &dev->priv;
	struct mlx5_eq_table *table = priv->eq_table;
	int num_eqs = MLX5_CAP_GEN(dev, max_num_eqs) ?
		      MLX5_CAP_GEN(dev, max_num_eqs) :
		      1 << MLX5_CAP_GEN(dev, log_max_eq);
	int nvec;
	int err;

	nvec = MLX5_CAP_GEN(dev, num_ports) * num_online_cpus() +
	       MLX5_EQ_VEC_COMP_BASE;
	nvec = min_t(int, nvec, num_eqs);
	if (nvec <= MLX5_EQ_VEC_COMP_BASE)
		return -ENOMEM;

	table->irq_info = kcalloc(nvec, sizeof(*table->irq_info), GFP_KERNEL);
	if (!table->irq_info)
		return -ENOMEM;

	nvec = pci_alloc_irq_vectors(dev->pdev, MLX5_EQ_VEC_COMP_BASE + 1,
				     nvec, PCI_IRQ_MSIX);
	if (nvec < 0) {
		err = nvec;
		goto err_free_irq_info;
	}

	table->num_comp_vectors = nvec - MLX5_EQ_VEC_COMP_BASE;

	return 0;

err_free_irq_info:
	kfree(table->irq_info);
	return err;
}

static void free_irq_vectors(struct mlx5_core_dev *dev)
{
	struct mlx5_priv *priv = &dev->priv;

	pci_free_irq_vectors(dev->pdev);
	kfree(priv->eq_table->irq_info);
}

int mlx5_eq_table_create(struct mlx5_core_dev *dev)
{
	int err;

	err = alloc_irq_vectors(dev);
	if (err) {
		mlx5_core_err(dev, "alloc irq vectors failed\n");
		return err;
	}

	err = create_async_eqs(dev);
	if (err) {
		mlx5_core_err(dev, "Failed to create async EQs\n");
		goto err_async_eqs;
	}

	err = create_comp_eqs(dev);
	if (err) {
		mlx5_core_err(dev, "Failed to create completion EQs\n");
		goto err_comp_eqs;
	}

	return 0;
err_comp_eqs:
	destroy_async_eqs(dev);
err_async_eqs:
	free_irq_vectors(dev);
	return err;
}

void mlx5_eq_table_destroy(struct mlx5_core_dev *dev)
{
	destroy_comp_eqs(dev);
	destroy_async_eqs(dev);
	free_irq_vectors(dev);
}

int mlx5_eq_notifier_register(struct mlx5_core_dev *dev, struct mlx5_nb *nb)
{
	struct mlx5_eq_table *eqt = dev->priv.eq_table;

	if (nb->event_type >= MLX5_EVENT_TYPE_MAX)
		return -EINVAL;

	return atomic_notifier_chain_register(&eqt->nh[nb->event_type], &nb->nb);
}

int mlx5_eq_notifier_unregister(struct mlx5_core_dev *dev, struct mlx5_nb *nb)
{
	struct mlx5_eq_table *eqt = dev->priv.eq_table;

	if (nb->event_type >= MLX5_EVENT_TYPE_MAX)
		return -EINVAL;

	return atomic_notifier_chain_unregister(&eqt->nh[nb->event_type], &nb->nb);
}
