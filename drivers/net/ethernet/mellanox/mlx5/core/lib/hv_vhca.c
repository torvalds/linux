// SPDX-License-Identifier: GPL-2.0 OR Linux-OpenIB
// Copyright (c) 2018 Mellanox Technologies

#include <linux/hyperv.h>
#include "mlx5_core.h"
#include "lib/hv.h"
#include "lib/hv_vhca.h"

struct mlx5_hv_vhca {
	struct mlx5_core_dev       *dev;
	struct workqueue_struct    *work_queue;
	struct mlx5_hv_vhca_agent  *agents[MLX5_HV_VHCA_AGENT_MAX];
	struct mutex                agents_lock; /* Protect agents array */
};

struct mlx5_hv_vhca_work {
	struct work_struct     invalidate_work;
	struct mlx5_hv_vhca   *hv_vhca;
	u64                    block_mask;
};

struct mlx5_hv_vhca_data_block {
	u16     sequence;
	u16     offset;
	u8      reserved[4];
	u64     data[15];
};

struct mlx5_hv_vhca_agent {
	enum mlx5_hv_vhca_agent_type	 type;
	struct mlx5_hv_vhca		*hv_vhca;
	void				*priv;
	u16                              seq;
	void (*control)(struct mlx5_hv_vhca_agent *agent,
			struct mlx5_hv_vhca_control_block *block);
	void (*invalidate)(struct mlx5_hv_vhca_agent *agent,
			   u64 block_mask);
	void (*cleanup)(struct mlx5_hv_vhca_agent *agent);
};

struct mlx5_hv_vhca *mlx5_hv_vhca_create(struct mlx5_core_dev *dev)
{
	struct mlx5_hv_vhca *hv_vhca;

	hv_vhca = kzalloc(sizeof(*hv_vhca), GFP_KERNEL);
	if (!hv_vhca)
		return ERR_PTR(-ENOMEM);

	hv_vhca->work_queue = create_singlethread_workqueue("mlx5_hv_vhca");
	if (!hv_vhca->work_queue) {
		kfree(hv_vhca);
		return ERR_PTR(-ENOMEM);
	}

	hv_vhca->dev = dev;
	mutex_init(&hv_vhca->agents_lock);

	return hv_vhca;
}

void mlx5_hv_vhca_destroy(struct mlx5_hv_vhca *hv_vhca)
{
	if (IS_ERR_OR_NULL(hv_vhca))
		return;

	destroy_workqueue(hv_vhca->work_queue);
	kfree(hv_vhca);
}

static void mlx5_hv_vhca_invalidate_work(struct work_struct *work)
{
	struct mlx5_hv_vhca_work *hwork;
	struct mlx5_hv_vhca *hv_vhca;
	int i;

	hwork = container_of(work, struct mlx5_hv_vhca_work, invalidate_work);
	hv_vhca = hwork->hv_vhca;

	mutex_lock(&hv_vhca->agents_lock);
	for (i = 0; i < MLX5_HV_VHCA_AGENT_MAX; i++) {
		struct mlx5_hv_vhca_agent *agent = hv_vhca->agents[i];

		if (!agent || !agent->invalidate)
			continue;

		if (!(BIT(agent->type) & hwork->block_mask))
			continue;

		agent->invalidate(agent, hwork->block_mask);
	}
	mutex_unlock(&hv_vhca->agents_lock);

	kfree(hwork);
}

void mlx5_hv_vhca_invalidate(void *context, u64 block_mask)
{
	struct mlx5_hv_vhca *hv_vhca = (struct mlx5_hv_vhca *)context;
	struct mlx5_hv_vhca_work *work;

	work = kzalloc(sizeof(*work), GFP_ATOMIC);
	if (!work)
		return;

	INIT_WORK(&work->invalidate_work, mlx5_hv_vhca_invalidate_work);
	work->hv_vhca    = hv_vhca;
	work->block_mask = block_mask;

	queue_work(hv_vhca->work_queue, &work->invalidate_work);
}

#define AGENT_MASK(type) (type ? BIT(type - 1) : 0 /* control */)

static void mlx5_hv_vhca_agents_control(struct mlx5_hv_vhca *hv_vhca,
					struct mlx5_hv_vhca_control_block *block)
{
	int i;

	for (i = 0; i < MLX5_HV_VHCA_AGENT_MAX; i++) {
		struct mlx5_hv_vhca_agent *agent = hv_vhca->agents[i];

		if (!agent || !agent->control)
			continue;

		if (!(AGENT_MASK(agent->type) & block->control))
			continue;

		agent->control(agent, block);
	}
}

static void mlx5_hv_vhca_capabilities(struct mlx5_hv_vhca *hv_vhca,
				      u32 *capabilities)
{
	int i;

	for (i = 0; i < MLX5_HV_VHCA_AGENT_MAX; i++) {
		struct mlx5_hv_vhca_agent *agent = hv_vhca->agents[i];

		if (agent)
			*capabilities |= AGENT_MASK(agent->type);
	}
}

static void
mlx5_hv_vhca_control_agent_invalidate(struct mlx5_hv_vhca_agent *agent,
				      u64 block_mask)
{
	struct mlx5_hv_vhca *hv_vhca = agent->hv_vhca;
	struct mlx5_core_dev *dev = hv_vhca->dev;
	struct mlx5_hv_vhca_control_block *block;
	u32 capabilities = 0;
	int err;

	block = kzalloc(sizeof(*block), GFP_KERNEL);
	if (!block)
		return;

	err = mlx5_hv_read_config(dev, block, sizeof(*block), 0);
	if (err)
		goto free_block;

	mlx5_hv_vhca_capabilities(hv_vhca, &capabilities);

	/* In case no capabilities, send empty block in return */
	if (!capabilities) {
		memset(block, 0, sizeof(*block));
		goto write;
	}

	if (block->capabilities != capabilities)
		block->capabilities = capabilities;

	if (block->control & ~capabilities)
		goto free_block;

	mlx5_hv_vhca_agents_control(hv_vhca, block);
	block->command_ack = block->command;

write:
	mlx5_hv_write_config(dev, block, sizeof(*block), 0);

free_block:
	kfree(block);
}

static struct mlx5_hv_vhca_agent *
mlx5_hv_vhca_control_agent_create(struct mlx5_hv_vhca *hv_vhca)
{
	return mlx5_hv_vhca_agent_create(hv_vhca, MLX5_HV_VHCA_AGENT_CONTROL,
					 NULL,
					 mlx5_hv_vhca_control_agent_invalidate,
					 NULL, NULL);
}

static void mlx5_hv_vhca_control_agent_destroy(struct mlx5_hv_vhca_agent *agent)
{
	mlx5_hv_vhca_agent_destroy(agent);
}

int mlx5_hv_vhca_init(struct mlx5_hv_vhca *hv_vhca)
{
	struct mlx5_hv_vhca_agent *agent;
	int err;

	if (IS_ERR_OR_NULL(hv_vhca))
		return IS_ERR_OR_NULL(hv_vhca);

	err = mlx5_hv_register_invalidate(hv_vhca->dev, hv_vhca,
					  mlx5_hv_vhca_invalidate);
	if (err)
		return err;

	agent = mlx5_hv_vhca_control_agent_create(hv_vhca);
	if (IS_ERR_OR_NULL(agent)) {
		mlx5_hv_unregister_invalidate(hv_vhca->dev);
		return IS_ERR_OR_NULL(agent);
	}

	hv_vhca->agents[MLX5_HV_VHCA_AGENT_CONTROL] = agent;

	return 0;
}

void mlx5_hv_vhca_cleanup(struct mlx5_hv_vhca *hv_vhca)
{
	struct mlx5_hv_vhca_agent *agent;
	int i;

	if (IS_ERR_OR_NULL(hv_vhca))
		return;

	agent = hv_vhca->agents[MLX5_HV_VHCA_AGENT_CONTROL];
	if (agent)
		mlx5_hv_vhca_control_agent_destroy(agent);

	mutex_lock(&hv_vhca->agents_lock);
	for (i = 0; i < MLX5_HV_VHCA_AGENT_MAX; i++)
		WARN_ON(hv_vhca->agents[i]);

	mutex_unlock(&hv_vhca->agents_lock);

	mlx5_hv_unregister_invalidate(hv_vhca->dev);
}

static void mlx5_hv_vhca_agents_update(struct mlx5_hv_vhca *hv_vhca)
{
	mlx5_hv_vhca_invalidate(hv_vhca, BIT(MLX5_HV_VHCA_AGENT_CONTROL));
}

struct mlx5_hv_vhca_agent *
mlx5_hv_vhca_agent_create(struct mlx5_hv_vhca *hv_vhca,
			  enum mlx5_hv_vhca_agent_type type,
			  void (*control)(struct mlx5_hv_vhca_agent*,
					  struct mlx5_hv_vhca_control_block *block),
			  void (*invalidate)(struct mlx5_hv_vhca_agent*,
					     u64 block_mask),
			  void (*cleaup)(struct mlx5_hv_vhca_agent *agent),
			  void *priv)
{
	struct mlx5_hv_vhca_agent *agent;

	if (IS_ERR_OR_NULL(hv_vhca))
		return ERR_PTR(-ENOMEM);

	if (type >= MLX5_HV_VHCA_AGENT_MAX)
		return ERR_PTR(-EINVAL);

	mutex_lock(&hv_vhca->agents_lock);
	if (hv_vhca->agents[type]) {
		mutex_unlock(&hv_vhca->agents_lock);
		return ERR_PTR(-EINVAL);
	}
	mutex_unlock(&hv_vhca->agents_lock);

	agent = kzalloc(sizeof(*agent), GFP_KERNEL);
	if (!agent)
		return ERR_PTR(-ENOMEM);

	agent->type      = type;
	agent->hv_vhca   = hv_vhca;
	agent->priv      = priv;
	agent->control   = control;
	agent->invalidate = invalidate;
	agent->cleanup   = cleaup;

	mutex_lock(&hv_vhca->agents_lock);
	hv_vhca->agents[type] = agent;
	mutex_unlock(&hv_vhca->agents_lock);

	mlx5_hv_vhca_agents_update(hv_vhca);

	return agent;
}

void mlx5_hv_vhca_agent_destroy(struct mlx5_hv_vhca_agent *agent)
{
	struct mlx5_hv_vhca *hv_vhca = agent->hv_vhca;

	mutex_lock(&hv_vhca->agents_lock);

	if (WARN_ON(agent != hv_vhca->agents[agent->type])) {
		mutex_unlock(&hv_vhca->agents_lock);
		return;
	}

	hv_vhca->agents[agent->type] = NULL;
	mutex_unlock(&hv_vhca->agents_lock);

	if (agent->cleanup)
		agent->cleanup(agent);

	kfree(agent);

	mlx5_hv_vhca_agents_update(hv_vhca);
}

static int mlx5_hv_vhca_data_block_prepare(struct mlx5_hv_vhca_agent *agent,
					   struct mlx5_hv_vhca_data_block *data_block,
					   void *src, int len, int *offset)
{
	int bytes = min_t(int, (int)sizeof(data_block->data), len);

	data_block->sequence = agent->seq;
	data_block->offset   = (*offset)++;
	memcpy(data_block->data, src, bytes);

	return bytes;
}

static void mlx5_hv_vhca_agent_seq_update(struct mlx5_hv_vhca_agent *agent)
{
	agent->seq++;
}

int mlx5_hv_vhca_agent_write(struct mlx5_hv_vhca_agent *agent,
			     void *buf, int len)
{
	int offset = agent->type * HV_CONFIG_BLOCK_SIZE_MAX;
	int block_offset = 0;
	int total = 0;
	int err;

	while (len) {
		struct mlx5_hv_vhca_data_block data_block = {0};
		int bytes;

		bytes = mlx5_hv_vhca_data_block_prepare(agent, &data_block,
							buf + total,
							len, &block_offset);
		if (!bytes)
			return -ENOMEM;

		err = mlx5_hv_write_config(agent->hv_vhca->dev, &data_block,
					   sizeof(data_block), offset);
		if (err)
			return err;

		total += bytes;
		len   -= bytes;
	}

	mlx5_hv_vhca_agent_seq_update(agent);

	return 0;
}

void *mlx5_hv_vhca_agent_priv(struct mlx5_hv_vhca_agent *agent)
{
	return agent->priv;
}
