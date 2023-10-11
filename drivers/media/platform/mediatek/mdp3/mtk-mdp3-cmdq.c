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
#include "mtk-img-ipi.h"

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

static bool is_output_disabled(int p_id, const struct img_compparam *param, u32 count)
{
	u32 num = 0;
	bool dis_output = false;
	bool dis_tile = false;

	if (CFG_CHECK(MT8183, p_id)) {
		num = CFG_COMP(MT8183, param, num_subfrms);
		dis_output = CFG_COMP(MT8183, param, frame.output_disable);
		dis_tile = CFG_COMP(MT8183, param, frame.output_disable);
	}

	return (count < num) ? (dis_output || dis_tile) : true;
}

static int mdp_path_subfrm_require(const struct mdp_path *path,
				   struct mdp_cmdq_cmd *cmd,
				   s32 *mutex_id, u32 count)
{
	const int p_id = path->mdp_dev->mdp_data->mdp_plat_id;
	const struct mdp_comp_ctx *ctx;
	const struct mtk_mdp_driver_data *data = path->mdp_dev->mdp_data;
	struct device *dev = &path->mdp_dev->pdev->dev;
	struct mtk_mutex **mutex = path->mdp_dev->mdp_mutex;
	int id, index;
	u32 num_comp = 0;

	if (CFG_CHECK(MT8183, p_id))
		num_comp = CFG_GET(MT8183, path->config, num_components);

	/* Decide which mutex to use based on the current pipeline */
	switch (path->comps[0].comp->public_id) {
	case MDP_COMP_RDMA0:
		index = MDP_PIPE_RDMA0;
		break;
	case MDP_COMP_ISP_IMGI:
		index = MDP_PIPE_IMGI;
		break;
	case MDP_COMP_WPEI:
		index = MDP_PIPE_WPEI;
		break;
	case MDP_COMP_WPEI2:
		index = MDP_PIPE_WPEI2;
		break;
	default:
		dev_err(dev, "Unknown pipeline and no mutex is assigned");
		return -EINVAL;
	}
	*mutex_id = data->pipe_info[index].mutex_id;

	/* Set mutex mod */
	for (index = 0; index < num_comp; index++) {
		ctx = &path->comps[index];
		if (is_output_disabled(p_id, ctx->param, count))
			continue;
		id = ctx->comp->public_id;
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
	const int p_id = path->mdp_dev->mdp_data->mdp_plat_id;
	const struct mdp_comp_ctx *ctx;
	struct device *dev = &path->mdp_dev->pdev->dev;
	struct mtk_mutex **mutex = path->mdp_dev->mdp_mutex;
	int index;
	u32 num_comp = 0;
	s32 event;

	if (-1 == *mutex_id) {
		dev_err(dev, "Incorrect mutex id");
		return -EINVAL;
	}

	if (CFG_CHECK(MT8183, p_id))
		num_comp = CFG_GET(MT8183, path->config, num_components);

	/* Wait WROT SRAM shared to DISP RDMA */
	/* Clear SOF event for each engine */
	for (index = 0; index < num_comp; index++) {
		ctx = &path->comps[index];
		if (is_output_disabled(p_id, ctx->param, count))
			continue;
		event = ctx->comp->gce_event[MDP_GCE_EVENT_SOF];
		if (event != MDP_GCE_NO_EVENT)
			MM_REG_CLEAR(cmd, event);
	}

	/* Enable the mutex */
	mtk_mutex_enable_by_cmdq(mutex[*mutex_id], (void *)&cmd->pkt);

	/* Wait SOF events and clear mutex modules (optional) */
	for (index = 0; index < num_comp; index++) {
		ctx = &path->comps[index];
		if (is_output_disabled(p_id, ctx->param, count))
			continue;
		event = ctx->comp->gce_event[MDP_GCE_EVENT_SOF];
		if (event != MDP_GCE_NO_EVENT)
			MM_REG_WAIT(cmd, event);
	}

	return 0;
}

static int mdp_path_ctx_init(struct mdp_dev *mdp, struct mdp_path *path)
{
	const int p_id = mdp->mdp_data->mdp_plat_id;
	void *param = NULL;
	int index, ret;
	u32 num_comp = 0;

	if (CFG_CHECK(MT8183, p_id))
		num_comp = CFG_GET(MT8183, path->config, num_components);

	if (num_comp < 1)
		return -EINVAL;

	for (index = 0; index < num_comp; index++) {
		if (CFG_CHECK(MT8183, p_id))
			param = (void *)CFG_ADDR(MT8183, path->config, components[index]);
		ret = mdp_comp_ctx_config(mdp, &path->comps[index],
					  param, path->param);
		if (ret)
			return ret;
	}

	return 0;
}

static int mdp_path_config_subfrm(struct mdp_cmdq_cmd *cmd,
				  struct mdp_path *path, u32 count)
{
	const int p_id = path->mdp_dev->mdp_data->mdp_plat_id;
	const struct img_mmsys_ctrl *ctrl = NULL;
	const struct img_mux *set;
	struct mdp_comp_ctx *ctx;
	s32 mutex_id;
	int index, ret;
	u32 num_comp = 0;

	if (CFG_CHECK(MT8183, p_id))
		num_comp = CFG_GET(MT8183, path->config, num_components);

	if (CFG_CHECK(MT8183, p_id))
		ctrl = CFG_ADDR(MT8183, path->config, ctrls[count]);

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
	for (index = (num_comp - 1); index >= 0; index--) {
		ctx = &path->comps[index];
		if (is_output_disabled(p_id, ctx->param, count))
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
	for (index = 0; index < num_comp; index++) {
		ctx = &path->comps[index];
		if (is_output_disabled(p_id, ctx->param, count))
			continue;
		ret = call_op(ctx, wait_comp_event, cmd);
		if (ret)
			return ret;
	}
	/* Advance to the next sub-frame */
	for (index = 0; index < num_comp; index++) {
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
	const int p_id = mdp->mdp_data->mdp_plat_id;
	struct mdp_comp_ctx *ctx;
	int index, count, ret;
	u32 num_comp = 0;
	u32 num_sub = 0;

	if (CFG_CHECK(MT8183, p_id))
		num_comp = CFG_GET(MT8183, path->config, num_components);

	if (CFG_CHECK(MT8183, p_id))
		num_sub = CFG_GET(MT8183, path->config, num_subfrms);

	/* Config path frame */
	/* Reset components */
	for (index = 0; index < num_comp; index++) {
		ctx = &path->comps[index];
		ret = call_op(ctx, init_comp, cmd);
		if (ret)
			return ret;
	}
	/* Config frame mode */
	for (index = 0; index < num_comp; index++) {
		const struct v4l2_rect *compose;
		u32 out = 0;

		ctx = &path->comps[index];
		if (CFG_CHECK(MT8183, p_id))
			out = CFG_COMP(MT8183, ctx->param, outputs[0]);

		compose = path->composes[out];
		ret = call_op(ctx, config_frame, cmd, compose);
		if (ret)
			return ret;
	}

	/* Config path sub-frames */
	for (count = 0; count < num_sub; count++) {
		ret = mdp_path_config_subfrm(cmd, path, count);
		if (ret)
			return ret;
	}
	/* Post processing information */
	for (index = 0; index < num_comp; index++) {
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
	if (!pkt->va_base)
		return -ENOMEM;

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
	int id;

	cmd = container_of(work, struct mdp_cmdq_cmd, auto_release_work);
	mdp = cmd->mdp;

	id = mdp->mdp_data->pipe_info[MDP_PIPE_RDMA0].mutex_id;
	mtk_mutex_unprepare(mdp->mdp_mutex[id]);
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
	int id;

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
		id = mdp->mdp_data->pipe_info[MDP_PIPE_RDMA0].mutex_id;
		mtk_mutex_unprepare(mdp->mdp_mutex[id]);
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
	const int p_id = mdp->mdp_data->mdp_plat_id;
	int i, ret;
	u32 num_comp = 0;

	atomic_inc(&mdp->job_count);
	if (atomic_read(&mdp->suspended)) {
		atomic_dec(&mdp->job_count);
		return -ECANCELED;
	}

	cmd = kzalloc(sizeof(*cmd), GFP_KERNEL);
	if (!cmd) {
		ret = -ENOMEM;
		goto err_cancel_job;
	}

	ret = mdp_cmdq_pkt_create(mdp->cmdq_clt, &cmd->pkt, SZ_16K);
	if (ret)
		goto err_free_cmd;

	if (CFG_CHECK(MT8183, p_id)) {
		num_comp = CFG_GET(MT8183, param->config, num_components);
	} else {
		ret = -EINVAL;
		goto err_destroy_pkt;
	}
	comps = kcalloc(num_comp, sizeof(*comps), GFP_KERNEL);
	if (!comps) {
		ret = -ENOMEM;
		goto err_destroy_pkt;
	}

	path = kzalloc(sizeof(*path), GFP_KERNEL);
	if (!path) {
		ret = -ENOMEM;
		goto err_free_comps;
	}

	i = mdp->mdp_data->pipe_info[MDP_PIPE_RDMA0].mutex_id;
	ret = mtk_mutex_prepare(mdp->mdp_mutex[i]);
	if (ret) {
		dev_err(dev, "Fail to enable mutex clk\n");
		goto err_free_path;
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
		goto err_free_path;
	}

	ret = mdp_path_config(mdp, cmd, path);
	if (ret) {
		dev_err(dev, "mdp_path_config error\n");
		goto err_free_path;
	}
	cmdq_pkt_finalize(&cmd->pkt);

	for (i = 0; i < num_comp; i++)
		memcpy(&comps[i], path->comps[i].comp,
		       sizeof(struct mdp_comp));

	mdp->cmdq_clt->client.rx_callback = mdp_handle_cmdq_callback;
	cmd->mdp = mdp;
	cmd->user_cmdq_cb = param->cmdq_cb;
	cmd->user_cb_data = param->cb_data;
	cmd->comps = comps;
	cmd->num_comps = num_comp;
	cmd->mdp_ctx = param->mdp_ctx;

	ret = mdp_comp_clocks_on(&mdp->pdev->dev, cmd->comps, cmd->num_comps);
	if (ret)
		goto err_free_path;

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
	mdp_comp_clocks_off(&mdp->pdev->dev, cmd->comps,
			    cmd->num_comps);
err_free_path:
	i = mdp->mdp_data->pipe_info[MDP_PIPE_RDMA0].mutex_id;
	mtk_mutex_unprepare(mdp->mdp_mutex[i]);
	kfree(path);
err_free_comps:
	kfree(comps);
err_destroy_pkt:
	mdp_cmdq_pkt_destroy(&cmd->pkt);
err_free_cmd:
	kfree(cmd);
err_cancel_job:
	atomic_dec(&mdp->job_count);

	return ret;
}
EXPORT_SYMBOL_GPL(mdp_cmdq_send);
