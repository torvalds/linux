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
#include "pci_irq.h"
#include "devlink.h"
#include "en_accel/ipsec.h"

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
	struct xarray           comp_eqs;
	struct mlx5_eq_async    pages_eq;
	struct mlx5_eq_async    cmd_eq;
	struct mlx5_eq_async    async_eq;

	struct atomic_notifier_head nh[MLX5_EVENT_TYPE_MAX];

	/* Since CQ DB is stored in async_eq */
	struct mlx5_nb          cq_err_nb;

	struct mutex            lock; /* sync async eqs creations */
	struct mutex            comp_lock; /* sync comp eqs creations */
	int			curr_comp_eqs;
	int			max_comp_eqs;
	struct mlx5_irq_table	*irq_table;
	struct xarray           comp_irqs;
	struct mlx5_irq         *ctrl_irq;
	struct cpu_rmap		*rmap;
	struct cpumask          used_cpus;
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
	cpumask_clear(&eq_table->used_cpus);
	xa_init(&eq_table->comp_eqs);
	xa_init(&eq_table->comp_irqs);
	mutex_init(&eq_table->comp_lock);
	eq_table->curr_comp_eqs = 0;
	return 0;
}

void mlx5_eq_table_cleanup(struct mlx5_core_dev *dev)
{
	struct mlx5_eq_table *table = dev->priv.eq_table;

	mlx5_eq_debugfs_cleanup(dev);
	xa_destroy(&table->comp_irqs);
	xa_destroy(&table->comp_eqs);
	kvfree(table);
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

	if (mlx5_ipsec_device_caps(dev) & MLX5_IPSEC_CAP_PACKET_OFFLOAD)
		async_event_mask |=
			(1ull << MLX5_EVENT_TYPE_OBJECT_CHANGE);

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

	err = devl_param_driverinit_value_get(devlink,
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

	/* Skip page eq creation when the device does not request for page requests */
	if (MLX5_CAP_GEN(dev, page_request_disable)) {
		mlx5_core_dbg(dev, "Skip page EQ creation\n");
		return 0;
	}

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

	if (!MLX5_CAP_GEN(dev, page_request_disable))
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

static void comp_irq_release_pci(struct mlx5_core_dev *dev, u16 vecidx)
{
	struct mlx5_eq_table *table = dev->priv.eq_table;
	struct mlx5_irq *irq;

	irq = xa_load(&table->comp_irqs, vecidx);
	if (!irq)
		return;

	xa_erase(&table->comp_irqs, vecidx);
	mlx5_irq_release_vector(irq);
}

static int mlx5_cpumask_default_spread(int numa_node, int index)
{
	const struct cpumask *prev = cpu_none_mask;
	const struct cpumask *mask;
	int found_cpu = 0;
	int i = 0;
	int cpu;

	rcu_read_lock();
	for_each_numa_hop_mask(mask, numa_node) {
		for_each_cpu_andnot(cpu, mask, prev) {
			if (i++ == index) {
				found_cpu = cpu;
				goto spread_done;
			}
		}
		prev = mask;
	}

spread_done:
	rcu_read_unlock();
	return found_cpu;
}

static struct cpu_rmap *mlx5_eq_table_get_pci_rmap(struct mlx5_core_dev *dev)
{
#ifdef CONFIG_RFS_ACCEL
#ifdef CONFIG_MLX5_SF
	if (mlx5_core_is_sf(dev))
		return dev->priv.parent_mdev->priv.eq_table->rmap;
#endif
	return dev->priv.eq_table->rmap;
#else
	return NULL;
#endif
}

static int comp_irq_request_pci(struct mlx5_core_dev *dev, u16 vecidx)
{
	struct mlx5_eq_table *table = dev->priv.eq_table;
	struct cpu_rmap *rmap;
	struct mlx5_irq *irq;
	int cpu;

	rmap = mlx5_eq_table_get_pci_rmap(dev);
	cpu = mlx5_cpumask_default_spread(dev->priv.numa_node, vecidx);
	irq = mlx5_irq_request_vector(dev, cpu, vecidx, &rmap);
	if (IS_ERR(irq))
		return PTR_ERR(irq);

	return xa_err(xa_store(&table->comp_irqs, vecidx, irq, GFP_KERNEL));
}

static void comp_irq_release_sf(struct mlx5_core_dev *dev, u16 vecidx)
{
	struct mlx5_eq_table *table = dev->priv.eq_table;
	struct mlx5_irq *irq;
	int cpu;

	irq = xa_load(&table->comp_irqs, vecidx);
	if (!irq)
		return;

	cpu = cpumask_first(mlx5_irq_get_affinity_mask(irq));
	cpumask_clear_cpu(cpu, &table->used_cpus);
	xa_erase(&table->comp_irqs, vecidx);
	mlx5_irq_affinity_irq_release(dev, irq);
}

static int comp_irq_request_sf(struct mlx5_core_dev *dev, u16 vecidx)
{
	struct mlx5_eq_table *table = dev->priv.eq_table;
	struct mlx5_irq_pool *pool = mlx5_irq_pool_get(dev);
	struct irq_affinity_desc af_desc = {};
	struct mlx5_irq *irq;

	/* In case SF irq pool does not exist, fallback to the PF irqs*/
	if (!mlx5_irq_pool_is_sf_pool(pool))
		return comp_irq_request_pci(dev, vecidx);

	af_desc.is_managed = 1;
	cpumask_copy(&af_desc.mask, cpu_online_mask);
	cpumask_andnot(&af_desc.mask, &af_desc.mask, &table->used_cpus);
	irq = mlx5_irq_affinity_request(pool, &af_desc);
	if (IS_ERR(irq))
		return PTR_ERR(irq);

	cpumask_or(&table->used_cpus, &table->used_cpus, mlx5_irq_get_affinity_mask(irq));
	mlx5_core_dbg(pool->dev, "IRQ %u mapped to cpu %*pbl, %u EQs on this irq\n",
		      pci_irq_vector(dev->pdev, mlx5_irq_get_index(irq)),
		      cpumask_pr_args(mlx5_irq_get_affinity_mask(irq)),
		      mlx5_irq_read_locked(irq) / MLX5_EQ_REFS_PER_IRQ);

	return xa_err(xa_store(&table->comp_irqs, vecidx, irq, GFP_KERNEL));
}

static void comp_irq_release(struct mlx5_core_dev *dev, u16 vecidx)
{
	mlx5_core_is_sf(dev) ? comp_irq_release_sf(dev, vecidx) :
			       comp_irq_release_pci(dev, vecidx);
}

static int comp_irq_request(struct mlx5_core_dev *dev, u16 vecidx)
{
	return mlx5_core_is_sf(dev) ? comp_irq_request_sf(dev, vecidx) :
				      comp_irq_request_pci(dev, vecidx);
}

#ifdef CONFIG_RFS_ACCEL
static int alloc_rmap(struct mlx5_core_dev *mdev)
{
	struct mlx5_eq_table *eq_table = mdev->priv.eq_table;

	/* rmap is a mapping between irq number and queue number.
	 * Each irq can be assigned only to a single rmap.
	 * Since SFs share IRQs, rmap mapping cannot function correctly
	 * for irqs that are shared between different core/netdev RX rings.
	 * Hence we don't allow netdev rmap for SFs.
	 */
	if (mlx5_core_is_sf(mdev))
		return 0;

	eq_table->rmap = alloc_irq_cpu_rmap(eq_table->max_comp_eqs);
	if (!eq_table->rmap)
		return -ENOMEM;
	return 0;
}

static void free_rmap(struct mlx5_core_dev *mdev)
{
	struct mlx5_eq_table *eq_table = mdev->priv.eq_table;

	if (eq_table->rmap) {
		free_irq_cpu_rmap(eq_table->rmap);
		eq_table->rmap = NULL;
	}
}
#else
static int alloc_rmap(struct mlx5_core_dev *mdev) { return 0; }
static void free_rmap(struct mlx5_core_dev *mdev) {}
#endif

static void destroy_comp_eq(struct mlx5_core_dev *dev, struct mlx5_eq_comp *eq, u16 vecidx)
{
	struct mlx5_eq_table *table = dev->priv.eq_table;

	xa_erase(&table->comp_eqs, vecidx);
	mlx5_eq_disable(dev, &eq->core, &eq->irq_nb);
	if (destroy_unmap_eq(dev, &eq->core))
		mlx5_core_warn(dev, "failed to destroy comp EQ 0x%x\n",
			       eq->core.eqn);
	tasklet_disable(&eq->tasklet_ctx.task);
	kfree(eq);
	comp_irq_release(dev, vecidx);
	table->curr_comp_eqs--;
}

static u16 comp_eq_depth_devlink_param_get(struct mlx5_core_dev *dev)
{
	struct devlink *devlink = priv_to_devlink(dev);
	union devlink_param_value val;
	int err;

	err = devl_param_driverinit_value_get(devlink,
					      DEVLINK_PARAM_GENERIC_ID_IO_EQ_SIZE,
					      &val);
	if (!err)
		return val.vu32;
	mlx5_core_dbg(dev, "Failed to get param. using default. err = %d\n", err);
	return MLX5_COMP_EQ_SIZE;
}

/* Must be called with EQ table comp_lock held */
static int create_comp_eq(struct mlx5_core_dev *dev, u16 vecidx)
{
	struct mlx5_eq_table *table = dev->priv.eq_table;
	struct mlx5_eq_param param = {};
	struct mlx5_eq_comp *eq;
	struct mlx5_irq *irq;
	int nent;
	int err;

	lockdep_assert_held(&table->comp_lock);
	if (table->curr_comp_eqs == table->max_comp_eqs) {
		mlx5_core_err(dev, "maximum number of vectors is allocated, %d\n",
			      table->max_comp_eqs);
		return -ENOMEM;
	}

	err = comp_irq_request(dev, vecidx);
	if (err)
		return err;

	nent = comp_eq_depth_devlink_param_get(dev);

	eq = kzalloc_node(sizeof(*eq), GFP_KERNEL, dev->priv.numa_node);
	if (!eq) {
		err = -ENOMEM;
		goto clean_irq;
	}

	INIT_LIST_HEAD(&eq->tasklet_ctx.list);
	INIT_LIST_HEAD(&eq->tasklet_ctx.process_list);
	spin_lock_init(&eq->tasklet_ctx.lock);
	tasklet_setup(&eq->tasklet_ctx.task, mlx5_cq_tasklet_cb);

	irq = xa_load(&table->comp_irqs, vecidx);
	eq->irq_nb.notifier_call = mlx5_eq_comp_int;
	param = (struct mlx5_eq_param) {
		.irq = irq,
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
	err = xa_err(xa_store(&table->comp_eqs, vecidx, eq, GFP_KERNEL));
	if (err)
		goto disable_eq;

	table->curr_comp_eqs++;
	return eq->core.eqn;

disable_eq:
	mlx5_eq_disable(dev, &eq->core, &eq->irq_nb);
clean_eq:
	kfree(eq);
clean_irq:
	comp_irq_release(dev, vecidx);
	return err;
}

int mlx5_comp_eqn_get(struct mlx5_core_dev *dev, u16 vecidx, int *eqn)
{
	struct mlx5_eq_table *table = dev->priv.eq_table;
	struct mlx5_eq_comp *eq;
	int ret = 0;

	mutex_lock(&table->comp_lock);
	eq = xa_load(&table->comp_eqs, vecidx);
	if (eq) {
		*eqn = eq->core.eqn;
		goto out;
	}

	ret = create_comp_eq(dev, vecidx);
	if (ret < 0) {
		mutex_unlock(&table->comp_lock);
		return ret;
	}

	*eqn = ret;
out:
	mutex_unlock(&table->comp_lock);
	return 0;
}
EXPORT_SYMBOL(mlx5_comp_eqn_get);

int mlx5_comp_irqn_get(struct mlx5_core_dev *dev, int vector, unsigned int *irqn)
{
	struct mlx5_eq_table *table = dev->priv.eq_table;
	struct mlx5_eq_comp *eq;
	int eqn;
	int err;

	/* Allocate the EQ if not allocated yet */
	err = mlx5_comp_eqn_get(dev, vector, &eqn);
	if (err)
		return err;

	eq = xa_load(&table->comp_eqs, vector);
	*irqn = eq->core.irqn;
	return 0;
}

unsigned int mlx5_comp_vectors_max(struct mlx5_core_dev *dev)
{
	return dev->priv.eq_table->max_comp_eqs;
}
EXPORT_SYMBOL(mlx5_comp_vectors_max);

static struct cpumask *
mlx5_comp_irq_get_affinity_mask(struct mlx5_core_dev *dev, int vector)
{
	struct mlx5_eq_table *table = dev->priv.eq_table;
	struct mlx5_eq_comp *eq;

	eq = xa_load(&table->comp_eqs, vector);
	if (eq)
		return mlx5_irq_get_affinity_mask(eq->core.irq);

	return NULL;
}

int mlx5_comp_vector_get_cpu(struct mlx5_core_dev *dev, int vector)
{
	struct cpumask *mask;
	int cpu;

	mask = mlx5_comp_irq_get_affinity_mask(dev, vector);
	if (mask)
		cpu = cpumask_first(mask);
	else
		cpu = mlx5_cpumask_default_spread(dev->priv.numa_node, vector);

	return cpu;
}
EXPORT_SYMBOL(mlx5_comp_vector_get_cpu);

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
	unsigned long index;

	xa_for_each(&table->comp_eqs, index, eq)
		if (eq->core.eqn == eqn)
			return eq;

	return ERR_PTR(-ENOENT);
}

/* This function should only be called after mlx5_cmd_force_teardown_hca */
void mlx5_core_eq_free_irqs(struct mlx5_core_dev *dev)
{
	mlx5_irq_table_free_irqs(dev);
}

#ifdef CONFIG_INFINIBAND_ON_DEMAND_PAGING
#define MLX5_MAX_ASYNC_EQS 4
#else
#define MLX5_MAX_ASYNC_EQS 3
#endif

static int get_num_eqs(struct mlx5_core_dev *dev)
{
	struct mlx5_eq_table *eq_table = dev->priv.eq_table;
	int max_dev_eqs;
	int max_eqs_sf;
	int num_eqs;

	/* If ethernet is disabled we use just a single completion vector to
	 * have the other vectors available for other drivers using mlx5_core. For
	 * example, mlx5_vdpa
	 */
	if (!mlx5_core_is_eth_enabled(dev) && mlx5_eth_supported(dev))
		return 1;

	max_dev_eqs = MLX5_CAP_GEN(dev, max_num_eqs) ?
		      MLX5_CAP_GEN(dev, max_num_eqs) :
		      1 << MLX5_CAP_GEN(dev, log_max_eq);

	num_eqs = min_t(int, mlx5_irq_table_get_num_comp(eq_table->irq_table),
			max_dev_eqs - MLX5_MAX_ASYNC_EQS);
	if (mlx5_core_is_sf(dev)) {
		max_eqs_sf = min_t(int, MLX5_COMP_EQS_PER_SF,
				   mlx5_irq_table_get_sfs_vec(eq_table->irq_table));
		num_eqs = min_t(int, num_eqs, max_eqs_sf);
	}

	return num_eqs;
}

int mlx5_eq_table_create(struct mlx5_core_dev *dev)
{
	struct mlx5_eq_table *eq_table = dev->priv.eq_table;
	int err;

	eq_table->max_comp_eqs = get_num_eqs(dev);
	err = create_async_eqs(dev);
	if (err) {
		mlx5_core_err(dev, "Failed to create async EQs\n");
		goto err_async_eqs;
	}

	err = alloc_rmap(dev);
	if (err) {
		mlx5_core_err(dev, "Failed to allocate rmap\n");
		goto err_rmap;
	}

	return 0;

err_rmap:
	destroy_async_eqs(dev);
err_async_eqs:
	return err;
}

void mlx5_eq_table_destroy(struct mlx5_core_dev *dev)
{
	struct mlx5_eq_table *table = dev->priv.eq_table;
	struct mlx5_eq_comp *eq;
	unsigned long index;

	xa_for_each(&table->comp_eqs, index, eq)
		destroy_comp_eq(dev, eq, index);

	free_rmap(dev);
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
