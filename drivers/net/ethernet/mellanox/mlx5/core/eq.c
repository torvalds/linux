// SPDX-License-Identifier: GPL-2.0 OR Linux-OpenIB
/*
 * Copyright (c) 2013-2021, Mellanox Technologies inc.  All rights reserved.
 */

#include <linux/interrupt.h>
#include <linux/notifier.h>
#include <linux/mlx5/driver.h>
#include <linux/mlx5/vport.h>
#include <linux/mlx5/eq.h>
#ifdef CONFIG_RFS_ACCEL
#include <linux/cpu_rmap.h>
#endif
#include "mlx5_core.h"
#include "lib/eq.h"
#include "fpga/core.h"
#include "eswitch.h"
#include "lib/clock.h"
#include "diag/fw_tracer.h"
#include "mlx5_irq.h"
#include "devlink.h"

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

/* budget must be smaller than MLX5_NUM_SPARE_EQE to guarantee that we update
 * the ci before we polled all the entries in the EQ. MLX5_NUM_SPARE_EQE is
 * used to set the EQ size, budget must be smaller than the EQ size.
 */
enum {
	MLX5_EQ_POLLING_BUDGET	= 128,
};

static_assert(MLX5_EQ_POLLING_BUDGET <= MLX5_NUM_SPARE_EQE);

struct mlx5_eq_table {
	struct list_head        comp_eqs_list;
	struct mlx5_eq_async    pages_eq;
	struct mlx5_eq_async    cmd_eq;
	struct mlx5_eq_async    async_eq;

	struct atomic_notifier_head nh[MLX5_EVENT_TYPE_MAX];

	/* Since CQ DB is stored in async_eq */
	struct mlx5_nb          cq_err_nb;

	struct mutex            lock; /* sync async eqs creations */
	int			num_comp_eqs;
	struct mlx5_irq_table	*irq_table;
	struct mlx5_irq         **comp_irqs;
	struct mlx5_irq         *ctrl_irq;
#ifdef CONFIG_RFS_ACCEL
	struct cpu_rmap		*rmap;
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
	u32 in[MLX5_ST_SZ_DW(destroy_eq_in)] = {};

	MLX5_SET(destroy_eq_in, in, opcode, MLX5_CMD_OP_DESTROY_EQ);
	MLX5_SET(destroy_eq_in, in, eq_number, eqn);
	return mlx5_cmd_exec_in(dev, destroy_eq, in);
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

static int mlx5_eq_comp_int(struct notifier_block *nb,
			    __always_unused unsigned long action,
			    __always_unused void *data)
{
	struct mlx5_eq_comp *eq_comp =
		container_of(nb, struct mlx5_eq_comp, irq_nb);
	struct mlx5_eq *eq = &eq_comp->core;
	struct mlx5_eqe *eqe;
	int num_eqes = 0;
	u32 cqn = -1;

	eqe = next_eqe_sw(eq);
	if (!eqe)
		goto out;

	do {
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
			cq->comp(cq, eqe);
			mlx5_cq_put(cq);
		} else {
			dev_dbg_ratelimited(eq->dev->device,
					    "Completion event for bogus CQ 0x%x\n", cqn);
		}

		++eq->cons_index;

	} while ((++num_eqes < MLX5_EQ_POLLING_BUDGET) && (eqe = next_eqe_sw(eq)));

out:
	eq_update_ci(eq, 1);

	if (cqn != -1)
		tasklet_schedule(&eq_comp->tasklet_ctx.task);

	return 0;
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
	mlx5_eq_comp_int(&eq->irq_nb, 0, NULL);
	count_eqe = eq->core.cons_index - count_eqe;
	enable_irq(eq->core.irqn);

	return count_eqe;
}

static void mlx5_eq_async_int_lock(struct mlx5_eq_async *eq, bool recovery,
				   unsigned long *flags)
	__acquires(&eq->lock)
{
	if (!recovery)
		spin_lock(&eq->lock);
	else
		spin_lock_irqsave(&eq->lock, *flags);
}

static void mlx5_eq_async_int_unlock(struct mlx5_eq_async *eq, bool recovery,
				     unsigned long *flags)
	__releases(&eq->lock)
{
	if (!recovery)
		spin_unlock(&eq->lock);
	else
		spin_unlock_irqrestore(&eq->lock, *flags);
}

enum async_eq_nb_action {
	ASYNC_EQ_IRQ_HANDLER = 0,
	ASYNC_EQ_RECOVER = 1,
};

static int mlx5_eq_async_int(struct notifier_block *nb,
			     unsigned long action, void *data)
{
	struct mlx5_eq_async *eq_async =
		container_of(nb, struct mlx5_eq_async, irq_nb);
	struct mlx5_eq *eq = &eq_async->core;
	struct mlx5_eq_table *eqt;
	struct mlx5_core_dev *dev;
	struct mlx5_eqe *eqe;
	unsigned long flags;
	int num_eqes = 0;
	bool recovery;

	dev = eq->dev;
	eqt = dev->priv.eq_table;

	recovery = action == ASYNC_EQ_RECOVER;
	mlx5_eq_async_int_lock(eq_async, recovery, &flags);

	eqe = next_eqe_sw(eq);
	if (!eqe)
		goto out;

	do {
		/*
		 * Make sure we read EQ entry contents after we've
		 * checked the ownership bit.
		 */
		dma_rmb();

		atomic_notifier_call_chain(&eqt->nh[eqe->type], eqe->type, eqe);
		atomic_notifier_call_chain(&eqt->nh[MLX5_EVENT_TYPE_NOTIFY_ANY], eqe->type, eqe);

		++eq->cons_index;

	} while ((++num_eqes < MLX5_EQ_POLLING_BUDGET) && (eqe = next_eqe_sw(eq)));

out:
	eq_update_ci(eq, 1);
	mlx5_eq_async_int_unlock(eq_async, recovery, &flags);

	return unlikely(recovery) ? num_eqes : 0;
}

void mlx5_cmd_eq_recover(struct mlx5_core_dev *dev)
{
	struct mlx5_eq_async *eq = &dev->priv.eq_table->cmd_eq;
	int eqes;

	eqes = mlx5_eq_async_int(&eq->irq_nb, ASYNC_EQ_RECOVER, NULL);
	if (eqes)
		mlx5_core_warn(dev, "Recovered %d EQEs on cmd_eq\n", eqes);
}

static void init_eq_buf(struct mlx5_eq *eq)
{
	struct mlx5_eqe *eqe;
	int i;

	for (i = 0; i < eq_get_size(eq); i++) {
		eqe = get_eqe(eq, i);
		eqe->owner = MLX5_EQE_OWNER_INIT_VAL;
	}
}

static int
create_map_eq(struct mlx5_core_dev *dev, struct mlx5_eq *eq,
	      struct mlx5_eq_param *param)
{
	u8 log_eq_size = order_base_2(param->nent + MLX5_NUM_SPARE_EQE);
	struct mlx5_cq_table *cq_table = &eq->cq_table;
	u32 out[MLX5_ST_SZ_DW(create_eq_out)] = {0};
	u8 log_eq_stride = ilog2(MLX5_EQE_SIZE);
	struct mlx5_priv *priv = &dev->priv;
	__be64 *pas;
	u16 vecidx;
	void *eqc;
	int inlen;
	u32 *in;
	int err;
	int i;

	/* Init CQ table */
	memset(cq_table, 0, sizeof(*cq_table));
	spin_lock_init(&cq_table->lock);
	INIT_RADIX_TREE(&cq_table->tree, GFP_ATOMIC);

	eq->cons_index = 0;

	err = mlx5_frag_buf_alloc_node(dev, wq_get_byte_sz(log_eq_size, log_eq_stride),
				       &eq->frag_buf, dev->priv.numa_node);
	if (err)
		return err;

	mlx5_init_fbc(eq->frag_buf.frags, log_eq_stride, log_eq_size, &eq->fbc);
	init_eq_buf(eq);

	eq->irq = param->irq;
	vecidx = mlx5_irq_get_index(eq->irq);

	inlen = MLX5_ST_SZ_BYTES(create_eq_in) +
		MLX5_FLD_SZ_BYTES(create_eq_in, pas[0]) * eq->frag_buf.npages;

	in = kvzalloc(inlen, GFP_KERNEL);
	if (!in) {
		err = -ENOMEM;
		goto err_buf;
	}

	pas = (__be64 *)MLX5_ADDR_OF(create_eq_in, in, pas);
	mlx5_fill_page_frag_array(&eq->frag_buf, pas);

	MLX5_SET(create_eq_in, in, opcode, MLX5_CMD_OP_CREATE_EQ);
	if (!param->mask[0] && MLX5_CAP_GEN(dev, log_max_uctx))
		MLX5_SET(create_eq_in, in, uid, MLX5_SHARED_RESOURCE_UID);

	for (i = 0; i < 4; i++)
		MLX5_ARRAY_SET64(create_eq_in, in, event_bitmask, i,
				 param->mask[i]);

	eqc = MLX5_ADDR_OF(create_eq_in, in, eq_context_entry);
	MLX5_SET(eqc, eqc, log_eq_size, eq->fbc.log_sz);
	MLX5_SET(eqc, eqc, uar_page, priv->uar->index);
	MLX5_SET(eqc, eqc, intr, vecidx);
	MLX5_SET(eqc, eqc, log_page_size,
		 eq->frag_buf.page_shift - MLX5_ADAPTER_PAGE_SHIFT);

	err = mlx5_cmd_exec(dev, in, inlen, out, sizeof(out));
	if (err)
		goto err_in;

	eq->vecidx = vecidx;
	eq->eqn = MLX5_GET(create_eq_out, out, eq_number);
	eq->irqn = pci_irq_vector(dev->pdev, vecidx);
	eq->dev = dev;
	eq->doorbell = priv->uar->map + MLX5_EQ_DOORBEL_OFFSET;

	err = mlx5_debug_eq_add(dev, eq);
	if (err)
		goto err_eq;

	kvfree(in);
	return 0;

err_eq:
	mlx5_cmd_destroy_eq(dev, eq->eqn);

err_in:
	kvfree(in);

err_buf:
	mlx5_frag_buf_free(dev, &eq->frag_buf);
	return err;
}

/**
 * mlx5_eq_enable - Enable EQ for receiving EQEs
 * @dev : Device which owns the eq
 * @eq  : EQ to enable
 * @nb  : Notifier call block
 *
 * Must be called after EQ is created in device.
 *
 * @return: 0 if no error
 */
int mlx5_eq_enable(struct mlx5_core_dev *dev, struct mlx5_eq *eq,
		   struct notifier_block *nb)
{
	int err;

	err = mlx5_irq_attach_nb(eq->irq, nb);
	if (!err)
		eq_update_ci(eq, 1);

	return err;
}
EXPORT_SYMBOL(mlx5_eq_enable);

/**
 * mlx5_eq_disable - Disable EQ for receiving EQEs
 * @dev : Device which owns the eq
 * @eq  : EQ to disable
 * @nb  : Notifier call block
 *
 * Must be called before EQ is destroyed.
 */
void mlx5_eq_disable(struct mlx5_core_dev *dev, struct mlx5_eq *eq,
		     struct notifier_block *nb)
{
	mlx5_irq_detach_nb(eq->irq, nb);
}
EXPORT_SYMBOL(mlx5_eq_disable);

static int destroy_unmap_eq(struct mlx5_core_dev *dev, struct mlx5_eq *eq)
{
	int err;

	mlx5_debug_eq_remove(dev, eq);

	err = mlx5_cmd_destroy_eq(dev, eq->eqn);
	if (err)
		mlx5_core_warn(dev, "failed to destroy a previously created eq: eqn %d\n",
			       eq->eqn);

	mlx5_frag_buf_free(dev, &eq->frag_buf);
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

void mlx5_eq_del_cq(struct mlx5_eq *eq, struct mlx5_core_cq *cq)
{
	struct mlx5_cq_table *table = &eq->cq_table;
	struct mlx5_core_cq *tmp;

	spin_lock(&table->lock);
	tmp = radix_tree_delete(&table->tree, cq->cqn);
	spin_unlock(&table->lock);

	if (!tmp) {
		mlx5_core_dbg(eq->dev, "cq 0x%x not found in eq 0x%x tree\n",
			      eq->eqn, cq->cqn);
		return;
	}

	if (tmp != cq)
		mlx5_core_dbg(eq->dev, "corruption on cqn 0x%x in eq 0x%x\n",
			      eq->eqn, cq->cqn);
}

int mlx5_eq_table_init(struct mlx5_core_dev *dev)
{
	struct mlx5_eq_table *eq_table;
	int i;

	eq_table = kvzalloc_node(sizeof(*eq_table), GFP_KERNEL,
				 dev->priv.numa_node);
	if (!eq_table)
		return -ENOMEM;

	dev->priv.eq_table = eq_table;

	mlx5_eq_debugfs_init(dev);

	mutex_init(&eq_table->lock);
	for (i = 0; i < MLX5_EVENT_TYPE_MAX; i++)
		ATOMIC_INIT_NOTIFIER_HEAD(&eq_table->nh[i]);

	eq_table->irq_table = mlx5_irq_table_get(dev);
	return 0;
}

void mlx5_eq_table_cleanup(struct mlx5_core_dev *dev)
{
	mlx5_eq_debugfs_cleanup(dev);
	kvfree(dev->priv.eq_table);
}

/* Async EQs */

static int create_async_eq(struct mlx5_core_dev *dev,
			   struct mlx5_eq *eq, struct mlx5_eq_param *param)
{
	struct mlx5_eq_table *eq_table = dev->priv.eq_table;
	int err;

	mutex_lock(&eq_table->lock);
	err = create_map_eq(dev, eq, param);
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
	eq  = &eqt->async_eq.core;
	eqe = data;

	cqn = be32_to_cpu(eqe->data.cq_err.cqn) & 0xffffff;
	mlx5_core_warn(eq->dev, "CQ error on CQN 0x%x, syndrome 0x%x\n",
		       cqn, eqe->data.cq_err.syndrome);

	cq = mlx5_eq_cq_get(eq, cqn);
	if (unlikely(!cq)) {
		mlx5_core_warn(eq->dev, "Async event for bogus CQ 0x%x\n", cqn);
		return NOTIFY_OK;
	}

	if (cq->event)
		cq->event(cq, type);

	mlx5_cq_put(cq);

	return NOTIFY_OK;
}

static void gather_user_async_events(struct mlx5_core_dev *dev, u64 mask[4])
{
	__be64 *user_unaffiliated_events;
	__be64 *user_affiliated_events;
	int i;

	user_affiliated_events =
		MLX5_CAP_DEV_EVENT(dev, user_affiliated_events);
	user_unaffiliated_events =
		MLX5_CAP_DEV_EVENT(dev, user_unaffiliated_events);

	for (i = 0; i < 4; i++)
		mask[i] |= be64_to_cpu(user_affiliated_events[i] |
				       user_unaffiliated_events[i]);
}

static void gather_async_events_mask(struct mlx5_core_dev *dev, u64 mask[4])
{
	u64 async_event_mask = MLX5_ASYNC_EVENT_MASK;

	if (MLX5_VPORT_MANAGER(dev))
		async_event_mask |= (1ull << MLX5_EVENT_TYPE_NIC_VPORT_CHANGE);

	if (MLX5_CAP_GEN(dev, general_notification_event))
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

	if (mlx5_eswitch_is_funcs_handler(dev))
		async_event_mask |=
			(1ull << MLX5_EVENT_TYPE_ESW_FUNCTIONS_CHANGED);

	if (MLX5_CAP_GEN_MAX(dev, vhca_state))
		async_event_mask |= (1ull << MLX5_EVENT_TYPE_VHCA_STATE_CHANGE);

	if (MLX5_CAP_MACSEC(dev, log_max_macsec_offload))
		async_event_mask |= (1ull << MLX5_EVENT_TYPE_OBJECT_CHANGE);

	mask[0] = async_event_mask;

	if (MLX5_CAP_GEN(dev, event_cap))
		gather_user_async_events(dev, mask);
}

static int
setup_async_eq(struct mlx5_core_dev *dev, struct mlx5_eq_async *eq,
	       struct mlx5_eq_param *param, const char *name)
{
	int err;

	eq->irq_nb.notifier_call = mlx5_eq_async_int;
	spin_lock_init(&eq->lock);

	err = create_async_eq(dev, &eq->core, param);
	if (err) {
		mlx5_core_warn(dev, "failed to create %s EQ %d\n", name, err);
		return err;
	}
	err = mlx5_eq_enable(dev, &eq->core, &eq->irq_nb);
	if (err) {
		mlx5_core_warn(dev, "failed to enable %s EQ %d\n", name, err);
		destroy_async_eq(dev, &eq->core);
	}
	return err;
}

static void cleanup_async_eq(struct mlx5_core_dev *dev,
			     struct mlx5_eq_async *eq, const char *name)
{
	int err;

	mlx5_eq_disable(dev, &eq->core, &eq->irq_nb);
	err = destroy_async_eq(dev, &eq->core);
	if (err)
		mlx5_core_err(dev, "failed to destroy %s eq, err(%d)\n",
			      name, err);
}

static u16 async_eq_depth_devlink_param_get(struct mlx5_core_dev *dev)
{
	struct devlink *devlink = priv_to_devlink(dev);
	union devlink_param_value val;
	int err;

	err = devlink_param_driverinit_value_get(devlink,
						 DEVLINK_PARAM_GENERIC_ID_EVENT_EQ_SIZE,
						 &val);
	if (!err)
		return val.vu32;
	mlx5_core_dbg(dev, "Failed to get param. using default. err = %d\n", err);
	return MLX5_NUM_ASYNC_EQE;
}
static int create_async_eqs(struct mlx5_core_dev *dev)
{
	struct mlx5_eq_table *table = dev->priv.eq_table;
	struct mlx5_eq_param param = {};
	int err;

	/* All the async_eqs are using single IRQ, request one IRQ and share its
	 * index among all the async_eqs of this device.
	 */
	table->ctrl_irq = mlx5_ctrl_irq_request(dev);
	if (IS_ERR(table->ctrl_irq))
		return PTR_ERR(table->ctrl_irq);

	MLX5_NB_INIT(&table->cq_err_nb, cq_err_event_notifier, CQ_ERROR);
	mlx5_eq_notifier_register(dev, &table->cq_err_nb);

	param = (struct mlx5_eq_param) {
		.irq = table->ctrl_irq,
		.nent = MLX5_NUM_CMD_EQE,
		.mask[0] = 1ull << MLX5_EVENT_TYPE_CMD,
	};
	mlx5_cmd_allowed_opcode(dev, MLX5_CMD_OP_CREATE_EQ);
	err = setup_async_eq(dev, &table->cmd_eq, &param, "cmd");
	if (err)
		goto err1;

	mlx5_cmd_use_events(dev);
	mlx5_cmd_allowed_opcode(dev, CMD_ALLOWED_OPCODE_ALL);

	param = (struct mlx5_eq_param) {
		.irq = table->ctrl_irq,
		.nent = async_eq_depth_devlink_param_get(dev),
	};

	gather_async_events_mask(dev, param.mask);
	err = setup_async_eq(dev, &table->async_eq, &param, "async");
	if (err)
		goto err2;

	param = (struct mlx5_eq_param) {
		.irq = table->ctrl_irq,
		.nent = /* TODO: sriov max_vf + */ 1,
		.mask[0] = 1ull << MLX5_EVENT_TYPE_PAGE_REQUEST,
	};

	err = setup_async_eq(dev, &table->pages_eq, &param, "pages");
	if (err)
		goto err3;

	return 0;

err3:
	cleanup_async_eq(dev, &table->async_eq, "async");
err2:
	mlx5_cmd_use_polling(dev);
	cleanup_async_eq(dev, &table->cmd_eq, "cmd");
err1:
	mlx5_cmd_allowed_opcode(dev, CMD_ALLOWED_OPCODE_ALL);
	mlx5_eq_notifier_unregister(dev, &table->cq_err_nb);
	mlx5_ctrl_irq_release(table->ctrl_irq);
	return err;
}

static void destroy_async_eqs(struct mlx5_core_dev *dev)
{
	struct mlx5_eq_table *table = dev->priv.eq_table;

	cleanup_async_eq(dev, &table->pages_eq, "pages");
	cleanup_async_eq(dev, &table->async_eq, "async");
	mlx5_cmd_allowed_opcode(dev, MLX5_CMD_OP_DESTROY_EQ);
	mlx5_cmd_use_polling(dev);
	cleanup_async_eq(dev, &table->cmd_eq, "cmd");
	mlx5_cmd_allowed_opcode(dev, CMD_ALLOWED_OPCODE_ALL);
	mlx5_eq_notifier_unregister(dev, &table->cq_err_nb);
	mlx5_ctrl_irq_release(table->ctrl_irq);
}

struct mlx5_eq *mlx5_get_async_eq(struct mlx5_core_dev *dev)
{
	return &dev->priv.eq_table->async_eq.core;
}

void mlx5_eq_synchronize_async_irq(struct mlx5_core_dev *dev)
{
	synchronize_irq(dev->priv.eq_table->async_eq.core.irqn);
}

void mlx5_eq_synchronize_cmd_irq(struct mlx5_core_dev *dev)
{
	synchronize_irq(dev->priv.eq_table->cmd_eq.core.irqn);
}

/* Generic EQ API for mlx5_core consumers
 * Needed For RDMA ODP EQ for now
 */
struct mlx5_eq *
mlx5_eq_create_generic(struct mlx5_core_dev *dev,
		       struct mlx5_eq_param *param)
{
	struct mlx5_eq *eq = kvzalloc_node(sizeof(*eq), GFP_KERNEL,
					   dev->priv.numa_node);
	int err;

	if (!eq)
		return ERR_PTR(-ENOMEM);

	param->irq = dev->priv.eq_table->ctrl_irq;
	err = create_async_eq(dev, eq, param);
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
	u32 nent = eq_get_size(eq);
	struct mlx5_eqe *eqe;

	eqe = get_eqe(eq, ci & (nent - 1));
	eqe = ((eqe->owner & 1) ^ !!(ci & nent)) ? NULL : eqe;
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

static void comp_irqs_release(struct mlx5_core_dev *dev)
{
	struct mlx5_eq_table *table = dev->priv.eq_table;

	if (mlx5_core_is_sf(dev))
		mlx5_irq_affinity_irqs_release(dev, table->comp_irqs, table->num_comp_eqs);
	else
		mlx5_irqs_release_vectors(table->comp_irqs, table->num_comp_eqs);
	kfree(table->comp_irqs);
}

static int comp_irqs_request(struct mlx5_core_dev *dev)
{
	struct mlx5_eq_table *table = dev->priv.eq_table;
	int ncomp_eqs = table->num_comp_eqs;
	u16 *cpus;
	int ret;
	int i;

	ncomp_eqs = table->num_comp_eqs;
	table->comp_irqs = kcalloc(ncomp_eqs, sizeof(*table->comp_irqs), GFP_KERNEL);
	if (!table->comp_irqs)
		return -ENOMEM;
	if (mlx5_core_is_sf(dev)) {
		ret = mlx5_irq_affinity_irqs_request_auto(dev, ncomp_eqs, table->comp_irqs);
		if (ret < 0)
			goto free_irqs;
		return ret;
	}

	cpus = kcalloc(ncomp_eqs, sizeof(*cpus), GFP_KERNEL);
	if (!cpus) {
		ret = -ENOMEM;
		goto free_irqs;
	}
	for (i = 0; i < ncomp_eqs; i++)
		cpus[i] = cpumask_local_spread(i, dev->priv.numa_node);
	ret = mlx5_irqs_request_vectors(dev, cpus, ncomp_eqs, table->comp_irqs);
	kfree(cpus);
	if (ret < 0)
		goto free_irqs;
	return ret;

free_irqs:
	kfree(table->comp_irqs);
	return ret;
}

static void destroy_comp_eqs(struct mlx5_core_dev *dev)
{
	struct mlx5_eq_table *table = dev->priv.eq_table;
	struct mlx5_eq_comp *eq, *n;

	list_for_each_entry_safe(eq, n, &table->comp_eqs_list, list) {
		list_del(&eq->list);
		mlx5_eq_disable(dev, &eq->core, &eq->irq_nb);
		if (destroy_unmap_eq(dev, &eq->core))
			mlx5_core_warn(dev, "failed to destroy comp EQ 0x%x\n",
				       eq->core.eqn);
		tasklet_disable(&eq->tasklet_ctx.task);
		kfree(eq);
	}
	comp_irqs_release(dev);
}

static u16 comp_eq_depth_devlink_param_get(struct mlx5_core_dev *dev)
{
	struct devlink *devlink = priv_to_devlink(dev);
	union devlink_param_value val;
	int err;

	err = devlink_param_driverinit_value_get(devlink,
						 DEVLINK_PARAM_GENERIC_ID_IO_EQ_SIZE,
						 &val);
	if (!err)
		return val.vu32;
	mlx5_core_dbg(dev, "Failed to get param. using default. err = %d\n", err);
	return MLX5_COMP_EQ_SIZE;
}

static int create_comp_eqs(struct mlx5_core_dev *dev)
{
	struct mlx5_eq_table *table = dev->priv.eq_table;
	struct mlx5_eq_comp *eq;
	int ncomp_eqs;
	int nent;
	int err;
	int i;

	ncomp_eqs = comp_irqs_request(dev);
	if (ncomp_eqs < 0)
		return ncomp_eqs;
	INIT_LIST_HEAD(&table->comp_eqs_list);
	nent = comp_eq_depth_devlink_param_get(dev);

	for (i = 0; i < ncomp_eqs; i++) {
		struct mlx5_eq_param param = {};

		eq = kzalloc_node(sizeof(*eq), GFP_KERNEL, dev->priv.numa_node);
		if (!eq) {
			err = -ENOMEM;
			goto clean;
		}

		INIT_LIST_HEAD(&eq->tasklet_ctx.list);
		INIT_LIST_HEAD(&eq->tasklet_ctx.process_list);
		spin_lock_init(&eq->tasklet_ctx.lock);
		tasklet_setup(&eq->tasklet_ctx.task, mlx5_cq_tasklet_cb);

		eq->irq_nb.notifier_call = mlx5_eq_comp_int;
		param = (struct mlx5_eq_param) {
			.irq = table->comp_irqs[i],
			.nent = nent,
		};

		err = create_map_eq(dev, &eq->core, &param);
		if (err)
			goto clean_eq;
		err = mlx5_eq_enable(dev, &eq->core, &eq->irq_nb);
		if (err) {
			destroy_unmap_eq(dev, &eq->core);
			goto clean_eq;
		}

		mlx5_core_dbg(dev, "allocated completion EQN %d\n", eq->core.eqn);
		/* add tail, to keep the list ordered, for mlx5_vector2eqn to work */
		list_add_tail(&eq->list, &table->comp_eqs_list);
	}

	table->num_comp_eqs = ncomp_eqs;
	return 0;

clean_eq:
	kfree(eq);
clean:
	destroy_comp_eqs(dev);
	return err;
}

static int vector2eqnirqn(struct mlx5_core_dev *dev, int vector, int *eqn,
			  unsigned int *irqn)
{
	struct mlx5_eq_table *table = dev->priv.eq_table;
	struct mlx5_eq_comp *eq, *n;
	int err = -ENOENT;
	int i = 0;

	list_for_each_entry_safe(eq, n, &table->comp_eqs_list, list) {
		if (i++ == vector) {
			if (irqn)
				*irqn = eq->core.irqn;
			if (eqn)
				*eqn = eq->core.eqn;
			err = 0;
			break;
		}
	}

	return err;
}

int mlx5_vector2eqn(struct mlx5_core_dev *dev, int vector, int *eqn)
{
	return vector2eqnirqn(dev, vector, eqn, NULL);
}
EXPORT_SYMBOL(mlx5_vector2eqn);

int mlx5_vector2irqn(struct mlx5_core_dev *dev, int vector, unsigned int *irqn)
{
	return vector2eqnirqn(dev, vector, NULL, irqn);
}

unsigned int mlx5_comp_vectors_count(struct mlx5_core_dev *dev)
{
	return dev->priv.eq_table->num_comp_eqs;
}
EXPORT_SYMBOL(mlx5_comp_vectors_count);

struct cpumask *
mlx5_comp_irq_get_affinity_mask(struct mlx5_core_dev *dev, int vector)
{
	struct mlx5_eq_table *table = dev->priv.eq_table;
	struct mlx5_eq_comp *eq, *n;
	int i = 0;

	list_for_each_entry_safe(eq, n, &table->comp_eqs_list, list) {
		if (i++ == vector)
			break;
	}

	return mlx5_irq_get_affinity_mask(eq->core.irq);
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

static void clear_rmap(struct mlx5_core_dev *dev)
{
#ifdef CONFIG_RFS_ACCEL
	struct mlx5_eq_table *eq_table = dev->priv.eq_table;

	free_irq_cpu_rmap(eq_table->rmap);
#endif
}

static int set_rmap(struct mlx5_core_dev *mdev)
{
	int err = 0;
#ifdef CONFIG_RFS_ACCEL
	struct mlx5_eq_table *eq_table = mdev->priv.eq_table;
	int vecidx;

	eq_table->rmap = alloc_irq_cpu_rmap(eq_table->num_comp_eqs);
	if (!eq_table->rmap) {
		err = -ENOMEM;
		mlx5_core_err(mdev, "Failed to allocate cpu_rmap. err %d", err);
		goto err_out;
	}

	for (vecidx = 0; vecidx < eq_table->num_comp_eqs; vecidx++) {
		err = irq_cpu_rmap_add(eq_table->rmap,
				       pci_irq_vector(mdev->pdev, vecidx));
		if (err) {
			mlx5_core_err(mdev, "irq_cpu_rmap_add failed. err %d",
				      err);
			goto err_irq_cpu_rmap_add;
		}
	}
	return 0;

err_irq_cpu_rmap_add:
	clear_rmap(mdev);
err_out:
#endif
	return err;
}

/* This function should only be called after mlx5_cmd_force_teardown_hca */
void mlx5_core_eq_free_irqs(struct mlx5_core_dev *dev)
{
	struct mlx5_eq_table *table = dev->priv.eq_table;

	mutex_lock(&table->lock); /* sync with create/destroy_async_eq */
	if (!mlx5_core_is_sf(dev))
		clear_rmap(dev);
	mlx5_irq_table_free_irqs(dev);
	mutex_unlock(&table->lock);
}

#ifdef CONFIG_INFINIBAND_ON_DEMAND_PAGING
#define MLX5_MAX_ASYNC_EQS 4
#else
#define MLX5_MAX_ASYNC_EQS 3
#endif

int mlx5_eq_table_create(struct mlx5_core_dev *dev)
{
	struct mlx5_eq_table *eq_table = dev->priv.eq_table;
	int num_eqs = MLX5_CAP_GEN(dev, max_num_eqs) ?
		      MLX5_CAP_GEN(dev, max_num_eqs) :
		      1 << MLX5_CAP_GEN(dev, log_max_eq);
	int max_eqs_sf;
	int err;

	eq_table->num_comp_eqs =
		min_t(int,
		      mlx5_irq_table_get_num_comp(eq_table->irq_table),
		      num_eqs - MLX5_MAX_ASYNC_EQS);
	if (mlx5_core_is_sf(dev)) {
		max_eqs_sf = min_t(int, MLX5_COMP_EQS_PER_SF,
				   mlx5_irq_table_get_sfs_vec(eq_table->irq_table));
		eq_table->num_comp_eqs = min_t(int, eq_table->num_comp_eqs,
					       max_eqs_sf);
	}

	err = create_async_eqs(dev);
	if (err) {
		mlx5_core_err(dev, "Failed to create async EQs\n");
		goto err_async_eqs;
	}

	if (!mlx5_core_is_sf(dev)) {
		/* rmap is a mapping between irq number and queue number.
		 * each irq can be assign only to a single rmap.
		 * since SFs share IRQs, rmap mapping cannot function correctly
		 * for irqs that are shared for different core/netdev RX rings.
		 * Hence we don't allow netdev rmap for SFs
		 */
		err = set_rmap(dev);
		if (err)
			goto err_rmap;
	}

	err = create_comp_eqs(dev);
	if (err) {
		mlx5_core_err(dev, "Failed to create completion EQs\n");
		goto err_comp_eqs;
	}

	return 0;
err_comp_eqs:
	if (!mlx5_core_is_sf(dev))
		clear_rmap(dev);
err_rmap:
	destroy_async_eqs(dev);
err_async_eqs:
	return err;
}

void mlx5_eq_table_destroy(struct mlx5_core_dev *dev)
{
	if (!mlx5_core_is_sf(dev))
		clear_rmap(dev);
	destroy_comp_eqs(dev);
	destroy_async_eqs(dev);
}

int mlx5_eq_notifier_register(struct mlx5_core_dev *dev, struct mlx5_nb *nb)
{
	struct mlx5_eq_table *eqt = dev->priv.eq_table;

	return atomic_notifier_chain_register(&eqt->nh[nb->event_type], &nb->nb);
}
EXPORT_SYMBOL(mlx5_eq_notifier_register);

int mlx5_eq_notifier_unregister(struct mlx5_core_dev *dev, struct mlx5_nb *nb)
{
	struct mlx5_eq_table *eqt = dev->priv.eq_table;

	return atomic_notifier_chain_unregister(&eqt->nh[nb->event_type], &nb->nb);
}
EXPORT_SYMBOL(mlx5_eq_notifier_unregister);
