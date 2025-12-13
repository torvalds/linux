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

static inline struct mdp_dev *vpu_to_mdp(struct mdp_vpu_dev *vpu)
{
	return container_of(vpu, struct mdp_dev, vpu);
}

static int mdp_vpu_shared_mem_alloc(struct mdp_vpu_dev *vpu)
{
	struct device *dev;

	if (IS_ERR_OR_NULL(vpu))
		goto err_return;

	dev = scp_get_device(vpu->scp);

	if (!vpu->param) {
		vpu->param = dma_alloc_wc(dev, vpu->param_size,
					  &vpu->param_addr, GFP_KERNEL);
		if (!vpu->param)
			goto err_return;
	}

	if (!vpu->work) {
		vpu->work = dma_alloc_wc(dev, vpu->work_size,
					 &vpu->work_addr, GFP_KERNEL);
		if (!vpu->work)
			goto err_free_param;
	}

	if (!vpu->config) {
		vpu->config = dma_alloc_wc(dev, vpu->config_size,
					   &vpu->config_addr, GFP_KERNEL);
		if (!vpu->config)
			goto err_free_work;
	}

	return 0;

err_free_work:
	dma_free_wc(dev, vpu->work_size, vpu->work, vpu->work_addr);
	vpu->work = NULL;
err_free_param:
	dma_free_wc(dev, vpu->param_size, vpu->param, vpu->param_addr);
	vpu->param = NULL;
err_return:
	return -ENOMEM;
}

void mdp_vpu_shared_mem_free(struct mdp_vpu_dev *vpu)
{
	struct device *dev;

	if (IS_ERR_OR_NULL(vpu))
		return;

	dev = scp_get_device(vpu->scp);

	if (vpu->param && vpu->param_addr)
		dma_free_wc(dev, vpu->param_size, vpu->param, vpu->param_addr);

	if (vpu->work && vpu->work_addr)
		dma_free_wc(dev, vpu->work_size, vpu->work, vpu->work_addr);

	if (vpu->config && vpu->config_addr)
		dma_free_wc(dev, vpu->config_size, vpu->config, vpu->config_addr);
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
	struct mdp_vpu_dev *vpu =
		(struct mdp_vpu_dev *)(unsigned long)param->drv_data;

	if (param->state) {
		struct mdp_dev *mdp = vpu_to_mdp(vpu);

		dev_err(&mdp->pdev->dev, "VPU MDP failure:%d\n", param->state);
	}
	vpu->status = param->state;
	complete(&vpu->ipi_acked);
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
	struct mdp_dev *mdp = vpu_to_mdp(vpu);
	int err;
	u8 pp_num = mdp->mdp_data->pp_used;

	init_completion(&vpu->ipi_acked);
	vpu->scp = scp;
	vpu->lock = lock;
	vpu->work_size = 0;
	err = mdp_vpu_sendmsg(vpu, SCP_IPI_MDP_INIT, &msg, sizeof(msg));
	if (err)
		goto err_work_size;
	/* vpu work_size was set in mdp_vpu_ipi_handle_init_ack */

	mutex_lock(vpu->lock);
	vpu->work_size = ALIGN(vpu->work_size, 64);
	vpu->param_size = ALIGN(sizeof(struct img_ipi_frameparam), 64);
	vpu->config_size = ALIGN(sizeof(struct img_config) * pp_num, 64);
	err = mdp_vpu_shared_mem_alloc(vpu);
	mutex_unlock(vpu->lock);
	if (err) {
		dev_err(&mdp->pdev->dev, "VPU memory alloc fail!");
		goto err_mem_alloc;
	}

	dev_dbg(&mdp->pdev->dev,
		"VPU param:%p pa:%pad sz:%zx, work:%p pa:%pad sz:%zx, config:%p pa:%pad sz:%zx",
		vpu->param, &vpu->param_addr, vpu->param_size,
		vpu->work, &vpu->work_addr, vpu->work_size,
		vpu->config, &vpu->config_addr, vpu->config_size);

	msg.work_addr = vpu->work_addr;
	msg.work_size = vpu->work_size;
	err = mdp_vpu_sendmsg(vpu, SCP_IPI_MDP_INIT, &msg, sizeof(msg));
	if (err)
		goto err_work_size;

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

int mdp_vpu_process(struct mdp_vpu_dev *vpu, struct img_ipi_frameparam *param)
{
	struct mdp_dev *mdp = vpu_to_mdp(vpu);
	struct img_sw_addr addr;

	mutex_lock(vpu->lock);
	if (mdp_vpu_shared_mem_alloc(vpu)) {
		dev_err(&mdp->pdev->dev, "VPU memory alloc fail!");
		mutex_unlock(vpu->lock);
		return -ENOMEM;
	}

	memset(vpu->param, 0, vpu->param_size);
	memset(vpu->work, 0, vpu->work_size);
	memset(vpu->config, 0, vpu->config_size);

	param->self_data.va = (unsigned long)vpu->work;
	param->self_data.pa = vpu->work_addr;
	param->config_data.va = (unsigned long)vpu->config;
	param->config_data.pa = vpu->config_addr;
	param->drv_data = (unsigned long)vpu;
	memcpy(vpu->param, param, sizeof(*param));

	addr.pa = vpu->param_addr;
	addr.va = (unsigned long)vpu->param;
	mutex_unlock(vpu->lock);
	return mdp_vpu_sendmsg(vpu, SCP_IPI_MDP_FRAME, &addr, sizeof(addr));
}
