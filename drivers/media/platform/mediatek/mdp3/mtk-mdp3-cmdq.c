// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2022 MediaTek Inc.
 * Author: Ping-Hsun Wu <ping-hsun.wu@mediatek.com>
 */

#include <linux/mailbox_controller.h>
#include <linux/platform_device.h>
#include "mtk-mdp3-cmdq.h"
#include "mtk-mdp3-comp.h"
#include "mtk-mdp3-core.h"
#include "mtk-mdp3-m2m.h"

#define MDP_PATH_MAX_COMPS	IMG_MAX_COMPONENTS

struct mdp_path {
	struct mdp_dev		*mdp_dev;
	struct mdp_comp_ctx	comps[MDP_PATH_MAX_COMPS];
	u32			num_comps;
	const struct img_config	*config;
	const struct img_ipi_frameparam *param;
	const struct v4l2_rect	*composes[IMG_MAX_HW_OUTPUTS];
	struct v4l2_rect	bounds[IMG_MAX_HW_OUTPUTS];
};

#define has_op(ctx, op) \
	((ctx)->comp->ops && (ctx)->comp->ops->op)
 #define call_op(ctx, op, ...) \
	(has_op(ctx, op) ? (ctx)->comp->ops->op(ctx, ##__VA_ARGS__) : 0)

static bool is_output_disabled(const struct img_compparam *param, u32 count)
{
	return (count < param->num_subfrms) ?
		(param->frame.output_disable ||
		param->subfrms[count].tile_disable) :
		true;
}

static int mdp_path_subfrm_require(const struct mdp_path *path,
				   struct mdp_cmdq_cmd *cmd,
				   s32 *mutex_id, u32 count)
{
	const struct img_config *config = path->config;
	const struct mdp_comp_ctx *ctx;
	const struct mtk_mdp_driver_data *data = path->mdp_dev->mdp_data;
	struct device *dev = &path->mdp_dev->pdev->dev;
	struct mtk_mutex **mutex = path->mdp_dev->mdp_mutex;
	int id, index;

	/* Decide which mutex to use based on the current pipeline */
	switch (path->comps[0].comp->id) {
	case MDP_COMP_RDMA0:
		*mutex_id = MDP_PIPE_RDMA0;
		break;
	case MDP_COMP_ISP_IMGI:
		*mutex_id = MDP_PIPE_IMGI;
		break;
	case MDP_COMP_WPEI:
		*mutex_id = MDP_PIPE_WPEI;
		break;
	case MDP_COMP_WPEI2:
		*mutex_id = MDP_PIPE_WPEI2;
		break;
	default:
		dev_err(dev, "Unknown pipeline and no mutex is assigned");
		return -EINVAL;
	}

	/* Set mutex mod */
	for (index = 0; index < config->num_components; index++) {
		ctx = &path->comps[index];
		if (is_output_disabled(ctx->param, count))
			continue;
		id = ctx->comp->id;
		mtk_mutex_write_mod(mutex[*mutex_id],
				    data->mdp_mutex_table_idx[id], false);
	}

	mtk_mutex_write_sof(mutex[*mutex_id],
			    MUTEX_SOF_IDX_SINGLE_MODE);

	return 0;
}

static int mdp_path_subfrm_run(const struct mdp_path *path,
			       struct mdp_cmdq_cmd *cmd,
			       s32 *mutex_id, u32 count)
{
	const struct img_config *config = path->config;
	const struct mdp_comp_ctx *ctx;
	struct device *dev = &path->mdp_dev->pdev->dev;
	struct mtk_mutex **mutex = path->mdp_dev->mdp_mutex;
	int index;
	s32 event;

	if (-1 == *mutex_id) {
		dev_err(dev, "Incorrect mutex id");
		return -EINVAL;
	}

	/* Wait WROT SRAM shared to DISP RDMA */
	/* Clear SOF event for each engine */
	for (index = 0; index < config->num_components; index++) {
		ctx = &path->comps[index];
		if (is_output_disabled(ctx->param, count))
			continue;
		event = ctx->comp->gce_event[MDP_GCE_EVENT_SOF];
		if (event != MDP_GCE_NO_EVENT)
			MM_REG_CLEAR(cmd, event);
	}

	/* Enable the mutex */
	mtk_mutex_enable_by_cmdq(mutex[*mutex_id], (void *)&cmd->pkt);

	/* Wait SOF events and clear mutex modules (optional) */
	for (index = 0; index < config->num_components; index++) {
		ctx = &path->comps[index];
		if (is_output_disabled(ctx->param, count))
			continue;
		event = ctx->comp->gce_event[MDP_GCE_EVENT_SOF];
		if (event != MDP_GCE_NO_EVENT)
			MM_REG_WAIT(cmd, event);
	}

	return 0;
}

static int mdp_path_ctx_init(struct mdp_dev *mdp, struct mdp_path *path)
{
	const struct img_config *config = path->config;
	int index, ret;

	if (config->num_components < 1)
		return -EINVAL;

	for (index = 0; index < config->num_components; index++) {
		ret = mdp_comp_ctx_config(mdp, &path->comps[index],
					  &config->components[index],
					  path->param);
		if (ret)
			return ret;
	}

	return 0;
}

static int mdp_path_config_subfrm(struct mdp_cmdq_cmd *cmd,
				  struct mdp_path *path, u32 count)
{
	const struct img_config *config = path->config;
	const struct img_mmsys_ctrl *ctrl = &config->ctrls[count];
	const struct img_mux *set;
	struct mdp_comp_ctx *ctx;
	s32 mutex_id;
	int index, ret;

	/* Acquire components */
	ret = mdp_path_subfrm_require(path, cmd, &mutex_id, count);
	if (ret)
		return ret;
	/* Enable mux settings */
	for (index = 0; index < ctrl->num_sets; index++) {
		set = &ctrl->sets[index];
		cmdq_pkt_write_mask(&cmd->pkt, set->subsys_id, set->reg,
				    set->value, 0xFFFFFFFF);
	}
	/* Config sub-frame information */
	for (index = (config->num_components - 1); index >= 0; index--) {
		ctx = &path->comps[index];
		if (is_output_disabled(ctx->param, count))
			continue;
		ret = call_op(ctx, config_subfrm, cmd, count);
		if (ret)
			return ret;
	}
	/* Run components */
	ret = mdp_path_subfrm_run(path, cmd, &mutex_id, count);
	if (ret)
		return ret;
	/* Wait components done */
	for (index = 0; index < config->num_components; index++) {
		ctx = &path->comps[index];
		if (is_output_disabled(ctx->param, count))
			continue;
		ret = call_op(ctx, wait_comp_event, cmd);
		if (ret)
			return ret;
	}
	/* Advance to the next sub-frame */
	for (index = 0; index < config->num_components; index++) {
		ctx = &path->comps[index];
		ret = call_op(ctx, advance_subfrm, cmd, count);
		if (ret)
			return ret;
	}
	/* Disable mux settings */
	for (index = 0; index < ctrl->num_sets; index++) {
		set = &ctrl->sets[index];
		cmdq_pkt_write_mask(&cmd->pkt, set->subsys_id, set->reg,
				    0, 0xFFFFFFFF);
	}

	return 0;
}

static int mdp_path_config(struct mdp_dev *mdp, struct mdp_cmdq_cmd *cmd,
			   struct mdp_path *path)
{
	const struct img_config *config = path->config;
	struct mdp_comp_ctx *ctx;
	int index, count, ret;

	/* Config path frame */
	/* Reset components */
	for (index = 0; index < config->num_components; index++) {
		ctx = &path->comps[index];
		ret = call_op(ctx, init_comp, cmd);
		if (ret)
			return ret;
	}
	/* Config frame mode */
	for (index = 0; index < config->num_components; index++) {
		const struct v4l2_rect *compose =
			path->composes[ctx->param->outputs[0]];

		ctx = &path->comps[index];
		ret = call_op(ctx, config_frame, cmd, compose);
		if (ret)
			return ret;
	}

	/* Config path sub-frames */
	for (count = 0; count < config->num_subfrms; count++) {
		ret = mdp_path_config_subfrm(cmd, path, count);
		if (ret)
			return ret;
	}
	/* Post processing information */
	for (index = 0; index < config->num_components; index++) {
		ctx = &path->comps[index];
		ret = call_op(ctx, post_process, cmd);
		if (ret)
			return ret;
	}
	return 0;
}

static int mdp_cmdq_pkt_create(struct cmdq_client *client, struct cmdq_pkt *pkt,
			       size_t size)
{
	struct device *dev;
	dma_addr_t dma_addr;

	pkt->va_base = kzalloc(size, GFP_KERNEL);
	if (!pkt->va_base) {
		kfree(pkt);
		return -ENOMEM;
	}
	pkt->buf_size = size;
	pkt->cl = (void *)client;

	dev = client->chan->mbox->dev;
	dma_addr = dma_map_single(dev, pkt->va_base, pkt->buf_size,
				  DMA_TO_DEVICE);
	if (dma_mapping_error(dev, dma_addr)) {
		dev_err(dev, "dma map failed, size=%u\n", (u32)(u64)size);
		kfree(pkt->va_base);
		return -ENOMEM;
	}

	pkt->pa_base = dma_addr;

	return 0;
}

static void mdp_cmdq_pkt_destroy(struct cmdq_pkt *pkt)
{
	struct cmdq_client *client = (struct cmdq_client *)pkt->cl;

	dma_unmap_single(client->chan->mbox->dev, pkt->pa_base, pkt->buf_size,
			 DMA_TO_DEVICE);
	kfree(pkt->va_base);
	pkt->va_base = NULL;
}

static void mdp_auto_release_work(struct work_struct *work)
{
	struct mdp_cmdq_cmd *cmd;
	struct mdp_dev *mdp;

	cmd = container_of(work, struct mdp_cmdq_cmd, auto_release_work);
	mdp = cmd->mdp;

	mtk_mutex_unprepare(mdp->mdp_mutex[MDP_PIPE_RDMA0]);
	mdp_comp_clocks_off(&mdp->pdev->dev, cmd->comps,
			    cmd->num_comps);

	atomic_dec(&mdp->job_count);
	wake_up(&mdp->callback_wq);

	mdp_cmdq_pkt_destroy(&cmd->pkt);
	kfree(cmd->comps);
	cmd->comps = NULL;
	kfree(cmd);
	cmd = NULL;
}

static void mdp_handle_cmdq_callback(struct mbox_client *cl, void *mssg)
{
	struct mdp_cmdq_cmd *cmd;
	struct cmdq_cb_data *data;
	struct mdp_dev *mdp;
	struct device *dev;

	if (!mssg) {
		pr_info("%s:no callback data\n", __func__);
		return;
	}

	data = (struct cmdq_cb_data *)mssg;
	cmd = container_of(data->pkt, struct mdp_cmdq_cmd, pkt);
	mdp = cmd->mdp;
	dev = &mdp->pdev->dev;

	if (cmd->mdp_ctx)
		mdp_m2m_job_finish(cmd->mdp_ctx);

	if (cmd->user_cmdq_cb) {
		struct cmdq_cb_data user_cb_data;

		user_cb_data.sta = data->sta;
		user_cb_data.pkt = data->pkt;
		cmd->user_cmdq_cb(user_cb_data);
	}

	INIT_WORK(&cmd->auto_release_work, mdp_auto_release_work);
	if (!queue_work(mdp->clock_wq, &cmd->auto_release_work)) {
		dev_err(dev, "%s:queue_work fail!\n", __func__);
		mtk_mutex_unprepare(mdp->mdp_mutex[MDP_PIPE_RDMA0]);
		mdp_comp_clocks_off(&mdp->pdev->dev, cmd->comps,
				    cmd->num_comps);

		atomic_dec(&mdp->job_count);
		wake_up(&mdp->callback_wq);

		mdp_cmdq_pkt_destroy(&cmd->pkt);
		kfree(cmd->comps);
		cmd->comps = NULL;
		kfree(cmd);
		cmd = NULL;
	}
}

int mdp_cmdq_send(struct mdp_dev *mdp, struct mdp_cmdq_param *param)
{
	struct mdp_path *path = NULL;
	struct mdp_cmdq_cmd *cmd = NULL;
	struct mdp_comp *comps = NULL;
	struct device *dev = &mdp->pdev->dev;
	int i, ret;

	atomic_inc(&mdp->job_count);
	if (atomic_read(&mdp->suspended)) {
		atomic_dec(&mdp->job_count);
		return -ECANCELED;
	}

	cmd = kzalloc(sizeof(*cmd), GFP_KERNEL);
	if (!cmd) {
		ret = -ENOMEM;
		goto err_cmdq_data;
	}

	if (mdp_cmdq_pkt_create(mdp->cmdq_clt, &cmd->pkt, SZ_16K)) {
		ret = -ENOMEM;
		goto err_cmdq_data;
	}

	comps = kcalloc(param->config->num_components, sizeof(*comps),
			GFP_KERNEL);
	if (!comps) {
		ret = -ENOMEM;
		goto err_cmdq_data;
	}

	path = kzalloc(sizeof(*path), GFP_KERNEL);
	if (!path) {
		ret = -ENOMEM;
		goto err_cmdq_data;
	}

	path->mdp_dev = mdp;
	path->config = param->config;
	path->param = param->param;
	for (i = 0; i < param->param->num_outputs; i++) {
		path->bounds[i].left = 0;
		path->bounds[i].top = 0;
		path->bounds[i].width =
			param->param->outputs[i].buffer.format.width;
		path->bounds[i].height =
			param->param->outputs[i].buffer.format.height;
		path->composes[i] = param->composes[i] ?
			param->composes[i] : &path->bounds[i];
	}

	ret = mdp_path_ctx_init(mdp, path);
	if (ret) {
		dev_err(dev, "mdp_path_ctx_init error\n");
		goto err_cmdq_data;
	}

	mtk_mutex_prepare(mdp->mdp_mutex[MDP_PIPE_RDMA0]);

	ret = mdp_path_config(mdp, cmd, path);
	if (ret) {
		dev_err(dev, "mdp_path_config error\n");
		goto err_cmdq_data;
	}
	cmdq_pkt_finalize(&cmd->pkt);

	for (i = 0; i < param->config->num_components; i++)
		memcpy(&comps[i], path->comps[i].comp,
		       sizeof(struct mdp_comp));

	mdp->cmdq_clt->client.rx_callback = mdp_handle_cmdq_callback;
	cmd->mdp = mdp;
	cmd->user_cmdq_cb = param->cmdq_cb;
	cmd->user_cb_data = param->cb_data;
	cmd->comps = comps;
	cmd->num_comps = param->config->num_components;
	cmd->mdp_ctx = param->mdp_ctx;

	ret = mdp_comp_clocks_on(&mdp->pdev->dev, cmd->comps, cmd->num_comps);
	if (ret) {
		dev_err(dev, "comp %d failed to enable clock!\n", ret);
		goto err_clock_off;
	}

	dma_sync_single_for_device(mdp->cmdq_clt->chan->mbox->dev,
				   cmd->pkt.pa_base, cmd->pkt.cmd_buf_size,
				   DMA_TO_DEVICE);
	ret = mbox_send_message(mdp->cmdq_clt->chan, &cmd->pkt);
	if (ret < 0) {
		dev_err(dev, "mbox send message fail %d!\n", ret);
		goto err_clock_off;
	}
	mbox_client_txdone(mdp->cmdq_clt->chan, 0);

	kfree(path);
	return 0;

err_clock_off:
	mtk_mutex_unprepare(mdp->mdp_mutex[MDP_PIPE_RDMA0]);
	mdp_comp_clocks_off(&mdp->pdev->dev, cmd->comps,
			    cmd->num_comps);
err_cmdq_data:
	kfree(path);
	atomic_dec(&mdp->job_count);
	wake_up(&mdp->callback_wq);
	if (cmd->pkt.buf_size > 0)
		mdp_cmdq_pkt_destroy(&cmd->pkt);
	kfree(comps);
	kfree(cmd);
	return ret;
}
EXPORT_SYMBOL_GPL(mdp_cmdq_send);
