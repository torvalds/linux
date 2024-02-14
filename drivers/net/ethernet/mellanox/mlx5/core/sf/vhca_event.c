// SPDX-License-Identifier: GPL-2.0 OR Linux-OpenIB
/* Copyright (c) 2020 Mellanox Technologies Ltd */

#include <linux/mlx5/driver.h>
#include "mlx5_ifc_vhca_event.h"
#include "mlx5_core.h"
#include "vhca_event.h"
#include "ecpf.h"
#define CREATE_TRACE_POINTS
#include "diag/vhca_tracepoint.h"

struct mlx5_vhca_state_notifier {
	struct mlx5_core_dev *dev;
	struct mlx5_nb nb;
	struct blocking_notifier_head n_head;
};

struct mlx5_vhca_event_work {
	struct work_struct work;
	struct mlx5_vhca_state_notifier *notifier;
	struct mlx5_vhca_state_event event;
};

struct mlx5_vhca_event_handler {
	struct workqueue_struct *wq;
};

struct mlx5_vhca_events {
	struct mlx5_core_dev *dev;
	struct mlx5_vhca_event_handler handler[MLX5_DEV_MAX_WQS];
};

int mlx5_cmd_query_vhca_state(struct mlx5_core_dev *dev, u16 function_id, u32 *out, u32 outlen)
{
	u32 in[MLX5_ST_SZ_DW(query_vhca_state_in)] = {};

	MLX5_SET(query_vhca_state_in, in, opcode, MLX5_CMD_OP_QUERY_VHCA_STATE);
	MLX5_SET(query_vhca_state_in, in, function_id, function_id);
	MLX5_SET(query_vhca_state_in, in, embedded_cpu_function, 0);

	return mlx5_cmd_exec(dev, in, sizeof(in), out, outlen);
}

static int mlx5_cmd_modify_vhca_state(struct mlx5_core_dev *dev, u16 function_id,
				      u32 *in, u32 inlen)
{
	u32 out[MLX5_ST_SZ_DW(modify_vhca_state_out)] = {};

	MLX5_SET(modify_vhca_state_in, in, opcode, MLX5_CMD_OP_MODIFY_VHCA_STATE);
	MLX5_SET(modify_vhca_state_in, in, function_id, function_id);
	MLX5_SET(modify_vhca_state_in, in, embedded_cpu_function, 0);

	return mlx5_cmd_exec(dev, in, inlen, out, sizeof(out));
}

int mlx5_modify_vhca_sw_id(struct mlx5_core_dev *dev, u16 function_id, u32 sw_fn_id)
{
	u32 out[MLX5_ST_SZ_DW(modify_vhca_state_out)] = {};
	u32 in[MLX5_ST_SZ_DW(modify_vhca_state_in)] = {};

	MLX5_SET(modify_vhca_state_in, in, opcode, MLX5_CMD_OP_MODIFY_VHCA_STATE);
	MLX5_SET(modify_vhca_state_in, in, function_id, function_id);
	MLX5_SET(modify_vhca_state_in, in, embedded_cpu_function, 0);
	MLX5_SET(modify_vhca_state_in, in, vhca_state_field_select.sw_function_id, 1);
	MLX5_SET(modify_vhca_state_in, in, vhca_state_context.sw_function_id, sw_fn_id);

	return mlx5_cmd_exec_inout(dev, modify_vhca_state, in, out);
}

int mlx5_vhca_event_arm(struct mlx5_core_dev *dev, u16 function_id)
{
	u32 in[MLX5_ST_SZ_DW(modify_vhca_state_in)] = {};

	MLX5_SET(modify_vhca_state_in, in, vhca_state_context.arm_change_event, 1);
	MLX5_SET(modify_vhca_state_in, in, vhca_state_field_select.arm_change_event, 1);

	return mlx5_cmd_modify_vhca_state(dev, function_id, in, sizeof(in));
}

static void
mlx5_vhca_event_notify(struct mlx5_core_dev *dev, struct mlx5_vhca_state_event *event)
{
	u32 out[MLX5_ST_SZ_DW(query_vhca_state_out)] = {};
	int err;

	err = mlx5_cmd_query_vhca_state(dev, event->function_id, out, sizeof(out));
	if (err)
		return;

	event->sw_function_id = MLX5_GET(query_vhca_state_out, out,
					 vhca_state_context.sw_function_id);
	event->new_vhca_state = MLX5_GET(query_vhca_state_out, out,
					 vhca_state_context.vhca_state);

	mlx5_vhca_event_arm(dev, event->function_id);
	trace_mlx5_sf_vhca_event(dev, event);

	blocking_notifier_call_chain(&dev->priv.vhca_state_notifier->n_head, 0, event);
}

static void mlx5_vhca_state_work_handler(struct work_struct *_work)
{
	struct mlx5_vhca_event_work *work = container_of(_work, struct mlx5_vhca_event_work, work);
	struct mlx5_vhca_state_notifier *notifier = work->notifier;
	struct mlx5_core_dev *dev = notifier->dev;

	mlx5_vhca_event_notify(dev, &work->event);
	kfree(work);
}

void mlx5_vhca_events_work_enqueue(struct mlx5_core_dev *dev, int idx, struct work_struct *work)
{
	queue_work(dev->priv.vhca_events->handler[idx].wq, work);
}

static int
mlx5_vhca_state_change_notifier(struct notifier_block *nb, unsigned long type, void *data)
{
	struct mlx5_vhca_state_notifier *notifier =
				mlx5_nb_cof(nb, struct mlx5_vhca_state_notifier, nb);
	struct mlx5_vhca_event_work *work;
	struct mlx5_eqe *eqe = data;
	int wq_idx;

	work = kzalloc(sizeof(*work), GFP_ATOMIC);
	if (!work)
		return NOTIFY_DONE;
	INIT_WORK(&work->work, &mlx5_vhca_state_work_handler);
	work->notifier = notifier;
	work->event.function_id = be16_to_cpu(eqe->data.vhca_state.function_id);
	wq_idx = work->event.function_id % MLX5_DEV_MAX_WQS;
	mlx5_vhca_events_work_enqueue(notifier->dev, wq_idx, &work->work);
	return NOTIFY_OK;
}

void mlx5_vhca_state_cap_handle(struct mlx5_core_dev *dev, void *set_hca_cap)
{
	if (!mlx5_vhca_event_supported(dev))
		return;

	MLX5_SET(cmd_hca_cap, set_hca_cap, vhca_state, 1);
	MLX5_SET(cmd_hca_cap, set_hca_cap, event_on_vhca_state_allocated, 1);
	MLX5_SET(cmd_hca_cap, set_hca_cap, event_on_vhca_state_active, 1);
	MLX5_SET(cmd_hca_cap, set_hca_cap, event_on_vhca_state_in_use, 1);
	MLX5_SET(cmd_hca_cap, set_hca_cap, event_on_vhca_state_teardown_request, 1);
}

int mlx5_vhca_event_init(struct mlx5_core_dev *dev)
{
	struct mlx5_vhca_state_notifier *notifier;
	char wq_name[MLX5_CMD_WQ_MAX_NAME];
	struct mlx5_vhca_events *events;
	int err, i;

	if (!mlx5_vhca_event_supported(dev))
		return 0;

	events = kzalloc(sizeof(*events), GFP_KERNEL);
	if (!events)
		return -ENOMEM;

	events->dev = dev;
	dev->priv.vhca_events = events;
	for (i = 0; i < MLX5_DEV_MAX_WQS; i++) {
		snprintf(wq_name, MLX5_CMD_WQ_MAX_NAME, "mlx5_vhca_event%d", i);
		events->handler[i].wq = create_singlethread_workqueue(wq_name);
		if (!events->handler[i].wq) {
			err = -ENOMEM;
			goto err_create_wq;
		}
	}

	notifier = kzalloc(sizeof(*notifier), GFP_KERNEL);
	if (!notifier) {
		err = -ENOMEM;
		goto err_notifier;
	}

	dev->priv.vhca_state_notifier = notifier;
	notifier->dev = dev;
	BLOCKING_INIT_NOTIFIER_HEAD(&notifier->n_head);
	MLX5_NB_INIT(&notifier->nb, mlx5_vhca_state_change_notifier, VHCA_STATE_CHANGE);
	return 0;

err_notifier:
err_create_wq:
	for (--i; i >= 0; i--)
		destroy_workqueue(events->handler[i].wq);
	kfree(events);
	return err;
}

void mlx5_vhca_event_work_queues_flush(struct mlx5_core_dev *dev)
{
	struct mlx5_vhca_events *vhca_events;
	int i;

	if (!mlx5_vhca_event_supported(dev))
		return;

	vhca_events = dev->priv.vhca_events;
	for (i = 0; i < MLX5_DEV_MAX_WQS; i++)
		flush_workqueue(vhca_events->handler[i].wq);
}

void mlx5_vhca_event_cleanup(struct mlx5_core_dev *dev)
{
	struct mlx5_vhca_events *vhca_events;
	int i;

	if (!mlx5_vhca_event_supported(dev))
		return;

	kfree(dev->priv.vhca_state_notifier);
	dev->priv.vhca_state_notifier = NULL;
	vhca_events = dev->priv.vhca_events;
	for (i = 0; i < MLX5_DEV_MAX_WQS; i++)
		destroy_workqueue(vhca_events->handler[i].wq);
	kvfree(vhca_events);
}

void mlx5_vhca_event_start(struct mlx5_core_dev *dev)
{
	struct mlx5_vhca_state_notifier *notifier;

	if (!dev->priv.vhca_state_notifier)
		return;

	notifier = dev->priv.vhca_state_notifier;
	mlx5_eq_notifier_register(dev, &notifier->nb);
}

void mlx5_vhca_event_stop(struct mlx5_core_dev *dev)
{
	struct mlx5_vhca_state_notifier *notifier;

	if (!dev->priv.vhca_state_notifier)
		return;

	notifier = dev->priv.vhca_state_notifier;
	mlx5_eq_notifier_unregister(dev, &notifier->nb);
}

int mlx5_vhca_event_notifier_register(struct mlx5_core_dev *dev, struct notifier_block *nb)
{
	if (!dev->priv.vhca_state_notifier)
		return -EOPNOTSUPP;
	return blocking_notifier_chain_register(&dev->priv.vhca_state_notifier->n_head, nb);
}

void mlx5_vhca_event_notifier_unregister(struct mlx5_core_dev *dev, struct notifier_block *nb)
{
	blocking_notifier_chain_unregister(&dev->priv.vhca_state_notifier->n_head, nb);
}
