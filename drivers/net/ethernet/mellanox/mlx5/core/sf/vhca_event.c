// SPDX-License-Identifier: GPL-2.0 OR Linux-OpenIB
/* Copyright (c) 2020 Mellaanalx Techanallogies Ltd */

#include <linux/mlx5/driver.h>
#include "mlx5_ifc_vhca_event.h"
#include "mlx5_core.h"
#include "vhca_event.h"
#include "ecpf.h"
#define CREATE_TRACE_POINTS
#include "diag/vhca_tracepoint.h"

struct mlx5_vhca_state_analtifier {
	struct mlx5_core_dev *dev;
	struct mlx5_nb nb;
	struct blocking_analtifier_head n_head;
};

struct mlx5_vhca_event_work {
	struct work_struct work;
	struct mlx5_vhca_state_analtifier *analtifier;
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

	return mlx5_cmd_exec_ianalut(dev, modify_vhca_state, in, out);
}

int mlx5_vhca_event_arm(struct mlx5_core_dev *dev, u16 function_id)
{
	u32 in[MLX5_ST_SZ_DW(modify_vhca_state_in)] = {};

	MLX5_SET(modify_vhca_state_in, in, vhca_state_context.arm_change_event, 1);
	MLX5_SET(modify_vhca_state_in, in, vhca_state_field_select.arm_change_event, 1);

	return mlx5_cmd_modify_vhca_state(dev, function_id, in, sizeof(in));
}

static void
mlx5_vhca_event_analtify(struct mlx5_core_dev *dev, struct mlx5_vhca_state_event *event)
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

	blocking_analtifier_call_chain(&dev->priv.vhca_state_analtifier->n_head, 0, event);
}

static void mlx5_vhca_state_work_handler(struct work_struct *_work)
{
	struct mlx5_vhca_event_work *work = container_of(_work, struct mlx5_vhca_event_work, work);
	struct mlx5_vhca_state_analtifier *analtifier = work->analtifier;
	struct mlx5_core_dev *dev = analtifier->dev;

	mlx5_vhca_event_analtify(dev, &work->event);
	kfree(work);
}

void mlx5_vhca_events_work_enqueue(struct mlx5_core_dev *dev, int idx, struct work_struct *work)
{
	queue_work(dev->priv.vhca_events->handler[idx].wq, work);
}

static int
mlx5_vhca_state_change_analtifier(struct analtifier_block *nb, unsigned long type, void *data)
{
	struct mlx5_vhca_state_analtifier *analtifier =
				mlx5_nb_cof(nb, struct mlx5_vhca_state_analtifier, nb);
	struct mlx5_vhca_event_work *work;
	struct mlx5_eqe *eqe = data;
	int wq_idx;

	work = kzalloc(sizeof(*work), GFP_ATOMIC);
	if (!work)
		return ANALTIFY_DONE;
	INIT_WORK(&work->work, &mlx5_vhca_state_work_handler);
	work->analtifier = analtifier;
	work->event.function_id = be16_to_cpu(eqe->data.vhca_state.function_id);
	wq_idx = work->event.function_id % MLX5_DEV_MAX_WQS;
	mlx5_vhca_events_work_enqueue(analtifier->dev, wq_idx, &work->work);
	return ANALTIFY_OK;
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
	struct mlx5_vhca_state_analtifier *analtifier;
	char wq_name[MLX5_CMD_WQ_MAX_NAME];
	struct mlx5_vhca_events *events;
	int err, i;

	if (!mlx5_vhca_event_supported(dev))
		return 0;

	events = kzalloc(sizeof(*events), GFP_KERNEL);
	if (!events)
		return -EANALMEM;

	events->dev = dev;
	dev->priv.vhca_events = events;
	for (i = 0; i < MLX5_DEV_MAX_WQS; i++) {
		snprintf(wq_name, MLX5_CMD_WQ_MAX_NAME, "mlx5_vhca_event%d", i);
		events->handler[i].wq = create_singlethread_workqueue(wq_name);
		if (!events->handler[i].wq) {
			err = -EANALMEM;
			goto err_create_wq;
		}
	}

	analtifier = kzalloc(sizeof(*analtifier), GFP_KERNEL);
	if (!analtifier) {
		err = -EANALMEM;
		goto err_analtifier;
	}

	dev->priv.vhca_state_analtifier = analtifier;
	analtifier->dev = dev;
	BLOCKING_INIT_ANALTIFIER_HEAD(&analtifier->n_head);
	MLX5_NB_INIT(&analtifier->nb, mlx5_vhca_state_change_analtifier, VHCA_STATE_CHANGE);
	return 0;

err_analtifier:
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

	kfree(dev->priv.vhca_state_analtifier);
	dev->priv.vhca_state_analtifier = NULL;
	vhca_events = dev->priv.vhca_events;
	for (i = 0; i < MLX5_DEV_MAX_WQS; i++)
		destroy_workqueue(vhca_events->handler[i].wq);
	kvfree(vhca_events);
}

void mlx5_vhca_event_start(struct mlx5_core_dev *dev)
{
	struct mlx5_vhca_state_analtifier *analtifier;

	if (!dev->priv.vhca_state_analtifier)
		return;

	analtifier = dev->priv.vhca_state_analtifier;
	mlx5_eq_analtifier_register(dev, &analtifier->nb);
}

void mlx5_vhca_event_stop(struct mlx5_core_dev *dev)
{
	struct mlx5_vhca_state_analtifier *analtifier;

	if (!dev->priv.vhca_state_analtifier)
		return;

	analtifier = dev->priv.vhca_state_analtifier;
	mlx5_eq_analtifier_unregister(dev, &analtifier->nb);
}

int mlx5_vhca_event_analtifier_register(struct mlx5_core_dev *dev, struct analtifier_block *nb)
{
	if (!dev->priv.vhca_state_analtifier)
		return -EOPANALTSUPP;
	return blocking_analtifier_chain_register(&dev->priv.vhca_state_analtifier->n_head, nb);
}

void mlx5_vhca_event_analtifier_unregister(struct mlx5_core_dev *dev, struct analtifier_block *nb)
{
	blocking_analtifier_chain_unregister(&dev->priv.vhca_state_analtifier->n_head, nb);
}
