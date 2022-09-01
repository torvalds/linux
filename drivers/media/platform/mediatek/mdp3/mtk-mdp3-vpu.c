// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2022 MediaTek Inc.
 * Author: Ping-Hsun Wu <ping-hsun.wu@mediatek.com>
 */

#include <linux/remoteproc.h>
#include <linux/remoteproc/mtk_scp.h>
#include "mtk-mdp3-vpu.h"
#include "mtk-mdp3-core.h"

#define MDP_VPU_MESSAGE_TIMEOUT 500U
#define vpu_alloc_size		0x600000

static inline struct mdp_dev *vpu_to_mdp(struct mdp_vpu_dev *vpu)
{
	return container_of(vpu, struct mdp_dev, vpu);
}

static int mdp_vpu_shared_mem_alloc(struct mdp_vpu_dev *vpu)
{
	if (vpu->work && vpu->work_addr)
		return 0;

	vpu->work = dma_alloc_coherent(scp_get_device(vpu->scp), vpu_alloc_size,
				       &vpu->work_addr, GFP_KERNEL);

	if (!vpu->work)
		return -ENOMEM;
	else
		return 0;
}

void mdp_vpu_shared_mem_free(struct mdp_vpu_dev *vpu)
{
	if (vpu->work && vpu->work_addr)
		dma_free_coherent(scp_get_device(vpu->scp), vpu_alloc_size,
				  vpu->work, vpu->work_addr);
}

static void mdp_vpu_ipi_handle_init_ack(void *data, unsigned int len,
					void *priv)
{
	struct mdp_ipi_init_msg *msg = (struct mdp_ipi_init_msg *)data;
	struct mdp_vpu_dev *vpu =
		(struct mdp_vpu_dev *)(unsigned long)msg->drv_data;

	if (!vpu->work_size)
		vpu->work_size = msg->work_size;

	vpu->status = msg->status;
	complete(&vpu->ipi_acked);
}

static void mdp_vpu_ipi_handle_deinit_ack(void *data, unsigned int len,
					  void *priv)
{
	struct mdp_ipi_deinit_msg *msg = (struct mdp_ipi_deinit_msg *)data;
	struct mdp_vpu_dev *vpu =
		(struct mdp_vpu_dev *)(unsigned long)msg->drv_data;

	vpu->status = msg->status;
	complete(&vpu->ipi_acked);
}

static void mdp_vpu_ipi_handle_frame_ack(void *data, unsigned int len,
					 void *priv)
{
	struct img_sw_addr *addr = (struct img_sw_addr *)data;
	struct img_ipi_frameparam *param =
		(struct img_ipi_frameparam *)(unsigned long)addr->va;
	struct mdp_vpu_ctx *ctx =
		(struct mdp_vpu_ctx *)(unsigned long)param->drv_data;

	if (param->state) {
		struct mdp_dev *mdp = vpu_to_mdp(ctx->vpu_dev);

		dev_err(&mdp->pdev->dev, "VPU MDP failure:%d\n", param->state);
	}
	ctx->vpu_dev->status = param->state;
	complete(&ctx->vpu_dev->ipi_acked);
}

int mdp_vpu_register(struct mdp_dev *mdp)
{
	int err;
	struct mtk_scp *scp = mdp->scp;
	struct device *dev = &mdp->pdev->dev;

	err = scp_ipi_register(scp, SCP_IPI_MDP_INIT,
			       mdp_vpu_ipi_handle_init_ack, NULL);
	if (err) {
		dev_err(dev, "scp_ipi_register failed %d\n", err);
		goto err_ipi_init;
	}
	err = scp_ipi_register(scp, SCP_IPI_MDP_DEINIT,
			       mdp_vpu_ipi_handle_deinit_ack, NULL);
	if (err) {
		dev_err(dev, "scp_ipi_register failed %d\n", err);
		goto err_ipi_deinit;
	}
	err = scp_ipi_register(scp, SCP_IPI_MDP_FRAME,
			       mdp_vpu_ipi_handle_frame_ack, NULL);
	if (err) {
		dev_err(dev, "scp_ipi_register failed %d\n", err);
		goto err_ipi_frame;
	}
	return 0;

err_ipi_frame:
	scp_ipi_unregister(scp, SCP_IPI_MDP_DEINIT);
err_ipi_deinit:
	scp_ipi_unregister(scp, SCP_IPI_MDP_INIT);
err_ipi_init:

	return err;
}

void mdp_vpu_unregister(struct mdp_dev *mdp)
{
	scp_ipi_unregister(mdp->scp, SCP_IPI_MDP_INIT);
	scp_ipi_unregister(mdp->scp, SCP_IPI_MDP_DEINIT);
	scp_ipi_unregister(mdp->scp, SCP_IPI_MDP_FRAME);
}

static int mdp_vpu_sendmsg(struct mdp_vpu_dev *vpu, enum scp_ipi_id id,
			   void *buf, unsigned int len)
{
	struct mdp_dev *mdp = vpu_to_mdp(vpu);
	unsigned int t = MDP_VPU_MESSAGE_TIMEOUT;
	int ret;

	if (!vpu->scp) {
		dev_dbg(&mdp->pdev->dev, "vpu scp is NULL");
		return -EINVAL;
	}
	ret = scp_ipi_send(vpu->scp, id, buf, len, 2000);

	if (ret) {
		dev_err(&mdp->pdev->dev, "scp_ipi_send failed %d\n", ret);
		return -EPERM;
	}
	ret = wait_for_completion_timeout(&vpu->ipi_acked,
					  msecs_to_jiffies(t));
	if (!ret)
		ret = -ETIME;
	else if (vpu->status)
		ret = -EINVAL;
	else
		ret = 0;
	return ret;
}

int mdp_vpu_dev_init(struct mdp_vpu_dev *vpu, struct mtk_scp *scp,
		     struct mutex *lock)
{
	struct mdp_ipi_init_msg msg = {
		.drv_data = (unsigned long)vpu,
	};
	size_t mem_size;
	phys_addr_t pool;
	const size_t pool_size = sizeof(struct mdp_config_pool);
	struct mdp_dev *mdp = vpu_to_mdp(vpu);
	int err;

	init_completion(&vpu->ipi_acked);
	vpu->scp = scp;
	vpu->lock = lock;
	vpu->work_size = 0;
	err = mdp_vpu_sendmsg(vpu, SCP_IPI_MDP_INIT, &msg, sizeof(msg));
	if (err)
		goto err_work_size;
	/* vpu work_size was set in mdp_vpu_ipi_handle_init_ack */

	mem_size = vpu_alloc_size;
	err = mdp_vpu_shared_mem_alloc(vpu);
	if (err) {
		dev_err(&mdp->pdev->dev, "VPU memory alloc fail!");
		goto err_mem_alloc;
	}

	pool = ALIGN((uintptr_t)vpu->work + vpu->work_size, 8);
	if (pool + pool_size - (uintptr_t)vpu->work > mem_size) {
		dev_err(&mdp->pdev->dev,
			"VPU memory insufficient: %zx + %zx > %zx",
			vpu->work_size, pool_size, mem_size);
		err = -ENOMEM;
		goto err_mem_size;
	}

	dev_dbg(&mdp->pdev->dev,
		"VPU work:%pK pa:%pad sz:%zx pool:%pa sz:%zx (mem sz:%zx)",
		vpu->work, &vpu->work_addr, vpu->work_size,
		&pool, pool_size, mem_size);
	vpu->pool = (struct mdp_config_pool *)(uintptr_t)pool;
	msg.work_addr = vpu->work_addr;
	msg.work_size = vpu->work_size;
	err = mdp_vpu_sendmsg(vpu, SCP_IPI_MDP_INIT, &msg, sizeof(msg));
	if (err)
		goto err_work_size;

	memset(vpu->pool, 0, sizeof(*vpu->pool));
	return 0;

err_work_size:
	switch (vpu->status) {
	case -MDP_IPI_EBUSY:
		err = -EBUSY;
		break;
	case -MDP_IPI_ENOMEM:
		err = -ENOSPC;	/* -ENOMEM */
		break;
	}
	return err;
err_mem_size:
err_mem_alloc:
	return err;
}

int mdp_vpu_dev_deinit(struct mdp_vpu_dev *vpu)
{
	struct mdp_ipi_deinit_msg msg = {
		.drv_data = (unsigned long)vpu,
		.work_addr = vpu->work_addr,
	};

	return mdp_vpu_sendmsg(vpu, SCP_IPI_MDP_DEINIT, &msg, sizeof(msg));
}

static struct img_config *mdp_config_get(struct mdp_vpu_dev *vpu,
					 enum mdp_config_id id, uint32_t *addr)
{
	struct img_config *config;

	if (id < 0 || id >= MDP_CONFIG_POOL_SIZE)
		return ERR_PTR(-EINVAL);

	mutex_lock(vpu->lock);
	vpu->pool->cfg_count[id]++;
	config = &vpu->pool->configs[id];
	*addr = vpu->work_addr + ((uintptr_t)config - (uintptr_t)vpu->work);
	mutex_unlock(vpu->lock);

	return config;
}

static int mdp_config_put(struct mdp_vpu_dev *vpu,
			  enum mdp_config_id id,
			  const struct img_config *config)
{
	int err = 0;

	if (id < 0 || id >= MDP_CONFIG_POOL_SIZE)
		return -EINVAL;
	if (vpu->lock)
		mutex_lock(vpu->lock);
	if (!vpu->pool->cfg_count[id] || config != &vpu->pool->configs[id])
		err = -EINVAL;
	else
		vpu->pool->cfg_count[id]--;
	if (vpu->lock)
		mutex_unlock(vpu->lock);
	return err;
}

int mdp_vpu_ctx_init(struct mdp_vpu_ctx *ctx, struct mdp_vpu_dev *vpu,
		     enum mdp_config_id id)
{
	ctx->config = mdp_config_get(vpu, id, &ctx->inst_addr);
	if (IS_ERR(ctx->config)) {
		int err = PTR_ERR(ctx->config);

		ctx->config = NULL;
		return err;
	}
	ctx->config_id = id;
	ctx->vpu_dev = vpu;
	return 0;
}

int mdp_vpu_ctx_deinit(struct mdp_vpu_ctx *ctx)
{
	int err = mdp_config_put(ctx->vpu_dev, ctx->config_id, ctx->config);

	ctx->config_id = 0;
	ctx->config = NULL;
	ctx->inst_addr = 0;
	return err;
}

int mdp_vpu_process(struct mdp_vpu_ctx *ctx, struct img_ipi_frameparam *param)
{
	struct mdp_vpu_dev *vpu = ctx->vpu_dev;
	struct mdp_dev *mdp = vpu_to_mdp(vpu);
	struct img_sw_addr addr;

	if (!ctx->vpu_dev->work || !ctx->vpu_dev->work_addr) {
		if (mdp_vpu_shared_mem_alloc(vpu)) {
			dev_err(&mdp->pdev->dev, "VPU memory alloc fail!");
			return -ENOMEM;
		}
	}
	memset((void *)ctx->vpu_dev->work, 0, ctx->vpu_dev->work_size);
	memset(ctx->config, 0, sizeof(*ctx->config));
	param->config_data.va = (unsigned long)ctx->config;
	param->config_data.pa = ctx->inst_addr;
	param->drv_data = (unsigned long)ctx;

	memcpy((void *)ctx->vpu_dev->work, param, sizeof(*param));
	addr.pa = ctx->vpu_dev->work_addr;
	addr.va = (uintptr_t)ctx->vpu_dev->work;
	return mdp_vpu_sendmsg(ctx->vpu_dev, SCP_IPI_MDP_FRAME,
		&addr, sizeof(addr));
}
