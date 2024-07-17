// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2022 MediaTek Inc.
 * Author: Ping-Hsun Wu <ping-hsun.wu@mediatek.com>
 */

#include <linux/mailbox_controller.h>
#include <linux/platform_device.h>
#include "mtk-mdp3-cfg.h"
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
	} else if (CFG_CHECK(MT8195, p_id)) {
		num = CFG_COMP(MT8195, param, num_subfrms);
		dis_output = CFG_COMP(MT8195, param, frame.output_disable);
		dis_tile = CFG_COMP(MT8195, param, frame.output_disable);
	}

	return (count < num) ? (dis_output || dis_tile) : true;
}

static struct mtk_mutex *__get_mutex(const struct mdp_dev *mdp_dev,
				     const struct mdp_pipe_info *p)
{
	return mdp_dev->mm_subsys[p->sub_id].mdp_mutex[p->mutex_id];
}

static u8 __get_pp_num(enum mdp_stream_type type)
{
	switch (type) {
	case MDP_STREAM_TYPE_DUAL_BITBLT:
		return MDP_PP_USED_2;
	default:
		return MDP_PP_USED_1;
	}
}

static enum mdp_pipe_id __get_pipe(const struct mdp_dev *mdp_dev,
				   enum mtk_mdp_comp_id id)
{
	enum mdp_pipe_id pipe_id;

	switch (id) {
	case MDP_COMP_RDMA0:
		pipe_id = MDP_PIPE_RDMA0;
		break;
	case MDP_COMP_ISP_IMGI:
		pipe_id = MDP_PIPE_IMGI;
		break;
	case MDP_COMP_WPEI:
		pipe_id = MDP_PIPE_WPEI;
		break;
	case MDP_COMP_WPEI2:
		pipe_id = MDP_PIPE_WPEI2;
		break;
	case MDP_COMP_RDMA1:
		pipe_id = MDP_PIPE_RDMA1;
		break;
	case MDP_COMP_RDMA2:
		pipe_id = MDP_PIPE_RDMA2;
		break;
	case MDP_COMP_RDMA3:
		pipe_id = MDP_PIPE_RDMA3;
		break;
	default:
		/* Avoid exceptions when operating MUTEX */
		pipe_id = MDP_PIPE_RDMA0;
		dev_err(&mdp_dev->pdev->dev, "Unknown pipeline id %d", id);
		break;
	}

	return pipe_id;
}

static struct img_config *__get_config_offset(struct mdp_dev *mdp,
					      struct mdp_cmdq_param *param,
					      u8 pp_idx)
{
	const int p_id = mdp->mdp_data->mdp_plat_id;
	struct device *dev = &mdp->pdev->dev;
	void *cfg_c, *cfg_n;
	long bound = mdp->vpu.config_size;

	if (pp_idx >= mdp->mdp_data->pp_used)
		goto err_param;

	if (CFG_CHECK(MT8183, p_id))
		cfg_c = CFG_OFST(MT8183, param->config, pp_idx);
	else if (CFG_CHECK(MT8195, p_id))
		cfg_c = CFG_OFST(MT8195, param->config, pp_idx);
	else
		goto err_param;

	if (CFG_CHECK(MT8183, p_id))
		cfg_n = CFG_OFST(MT8183, param->config, pp_idx + 1);
	else if (CFG_CHECK(MT8195, p_id))
		cfg_n = CFG_OFST(MT8195, param->config, pp_idx + 1);
	else
		goto err_param;

	if ((long)cfg_n - (long)mdp->vpu.config > bound) {
		dev_err(dev, "config offset %ld OOB %ld\n", (long)cfg_n, bound);
		cfg_c = ERR_PTR(-EFAULT);
	}

	return (struct img_config *)cfg_c;

err_param:
	cfg_c = ERR_PTR(-EINVAL);
	return (struct img_config *)cfg_c;
}

static int mdp_path_subfrm_require(const struct mdp_path *path,
				   struct mdp_cmdq_cmd *cmd,
				   struct mdp_pipe_info *p, u32 count)
{
	const int p_id = path->mdp_dev->mdp_data->mdp_plat_id;
	const struct mdp_comp_ctx *ctx;
	const struct mtk_mdp_driver_data *data = path->mdp_dev->mdp_data;
	struct mtk_mutex *mutex;
	int id, index;
	u32 num_comp = 0;

	if (CFG_CHECK(MT8183, p_id))
		num_comp = CFG_GET(MT8183, path->config, num_components);
	else if (CFG_CHECK(MT8195, p_id))
		num_comp = CFG_GET(MT8195, path->config, num_components);

	/* Decide which mutex to use based on the current pipeline */
	index = __get_pipe(path->mdp_dev, path->comps[0].comp->public_id);
	memcpy(p, &data->pipe_info[index], sizeof(struct mdp_pipe_info));
	mutex = __get_mutex(path->mdp_dev, p);

	/* Set mutex mod */
	for (index = 0; index < num_comp; index++) {
		s32 inner_id = MDP_COMP_NONE;
		const u32 *mutex_idx;
		const struct mdp_comp_blend *b;

		if (CFG_CHECK(MT8183, p_id))
			inner_id = CFG_GET(MT8183, path->config, components[index].type);
		else if (CFG_CHECK(MT8195, p_id))
			inner_id = CFG_GET(MT8195, path->config, components[index].type);

		if (mdp_cfg_comp_is_dummy(path->mdp_dev, inner_id))
			continue;

		ctx = &path->comps[index];
		if (is_output_disabled(p_id, ctx->param, count))
			continue;

		mutex_idx = data->mdp_mutex_table_idx;
		id = ctx->comp->public_id;
		mtk_mutex_write_mod(mutex, mutex_idx[id], false);

		b = &data->comp_data[id].blend;
		if (b && b->aid_mod)
			mtk_mutex_write_mod(mutex, mutex_idx[b->b_id], false);
	}

	mtk_mutex_write_sof(mutex, MUTEX_SOF_IDX_SINGLE_MODE);

	return 0;
}

static int mdp_path_subfrm_run(const struct mdp_path *path,
			       struct mdp_cmdq_cmd *cmd,
			       struct mdp_pipe_info *p, u32 count)
{
	const int p_id = path->mdp_dev->mdp_data->mdp_plat_id;
	const struct mdp_comp_ctx *ctx;
	struct device *dev = &path->mdp_dev->pdev->dev;
	struct mtk_mutex *mutex;
	int index;
	u32 num_comp = 0;
	s32 event;
	s32 inner_id = MDP_COMP_NONE;

	if (-1 == p->mutex_id) {
		dev_err(dev, "Incorrect mutex id");
		return -EINVAL;
	}

	if (CFG_CHECK(MT8183, p_id))
		num_comp = CFG_GET(MT8183, path->config, num_components);
	else if (CFG_CHECK(MT8195, p_id))
		num_comp = CFG_GET(MT8195, path->config, num_components);

	/* Wait WROT SRAM shared to DISP RDMA */
	/* Clear SOF event for each engine */
	for (index = 0; index < num_comp; index++) {
		if (CFG_CHECK(MT8183, p_id))
			inner_id = CFG_GET(MT8183, path->config, components[index].type);
		else if (CFG_CHECK(MT8195, p_id))
			inner_id = CFG_GET(MT8195, path->config, components[index].type);

		if (mdp_cfg_comp_is_dummy(path->mdp_dev, inner_id))
			continue;
		ctx = &path->comps[index];
		if (is_output_disabled(p_id, ctx->param, count))
			continue;
		event = ctx->comp->gce_event[MDP_GCE_EVENT_SOF];
		if (event != MDP_GCE_NO_EVENT)
			MM_REG_CLEAR(cmd, event);
	}

	/* Enable the mutex */
	mutex = __get_mutex(path->mdp_dev, p);
	mtk_mutex_enable_by_cmdq(mutex, (void *)&cmd->pkt);

	/* Wait SOF events and clear mutex modules (optional) */
	for (index = 0; index < num_comp; index++) {
		if (CFG_CHECK(MT8183, p_id))
			inner_id = CFG_GET(MT8183, path->config, components[index].type);
		else if (CFG_CHECK(MT8195, p_id))
			inner_id = CFG_GET(MT8195, path->config, components[index].type);

		if (mdp_cfg_comp_is_dummy(path->mdp_dev, inner_id))
			continue;
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
	else if (CFG_CHECK(MT8195, p_id))
		num_comp = CFG_GET(MT8195, path->config, num_components);

	if (num_comp < 1)
		return -EINVAL;

	for (index = 0; index < num_comp; index++) {
		s32 inner_id = MDP_COMP_NONE;

		if (CFG_CHECK(MT8183, p_id))
			inner_id = CFG_GET(MT8183, path->config, components[index].type);
		else if (CFG_CHECK(MT8195, p_id))
			inner_id = CFG_GET(MT8195, path->config, components[index].type);

		if (mdp_cfg_comp_is_dummy(path->mdp_dev, inner_id))
			continue;
		if (CFG_CHECK(MT8183, p_id))
			param = (void *)CFG_ADDR(MT8183, path->config, components[index]);
		else if (CFG_CHECK(MT8195, p_id))
			param = (void *)CFG_ADDR(MT8195, path->config, components[index]);
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
	struct mdp_pipe_info pipe;
	int index, ret;
	u32 num_comp = 0;
	s32 inner_id = MDP_COMP_NONE;

	if (CFG_CHECK(MT8183, p_id))
		num_comp = CFG_GET(MT8183, path->config, num_components);
	else if (CFG_CHECK(MT8195, p_id))
		num_comp = CFG_GET(MT8195, path->config, num_components);

	if (CFG_CHECK(MT8183, p_id))
		ctrl = CFG_ADDR(MT8183, path->config, ctrls[count]);
	else if (CFG_CHECK(MT8195, p_id))
		ctrl = CFG_ADDR(MT8195, path->config, ctrls[count]);

	/* Acquire components */
	ret = mdp_path_subfrm_require(path, cmd, &pipe, count);
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
		if (CFG_CHECK(MT8183, p_id))
			inner_id = CFG_GET(MT8183, path->config, components[index].type);
		else if (CFG_CHECK(MT8195, p_id))
			inner_id = CFG_GET(MT8195, path->config, components[index].type);

		if (mdp_cfg_comp_is_dummy(path->mdp_dev, inner_id))
			continue;
		ctx = &path->comps[index];
		if (is_output_disabled(p_id, ctx->param, count))
			continue;
		ret = call_op(ctx, config_subfrm, cmd, count);
		if (ret)
			return ret;
	}
	/* Run components */
	ret = mdp_path_subfrm_run(path, cmd, &pipe, count);
	if (ret)
		return ret;
	/* Wait components done */
	for (index = 0; index < num_comp; index++) {
		if (CFG_CHECK(MT8183, p_id))
			inner_id = CFG_GET(MT8183, path->config, components[index].type);
		else if (CFG_CHECK(MT8195, p_id))
			inner_id = CFG_GET(MT8195, path->config, components[index].type);

		if (mdp_cfg_comp_is_dummy(path->mdp_dev, inner_id))
			continue;
		ctx = &path->comps[index];
		if (is_output_disabled(p_id, ctx->param, count))
			continue;
		ret = call_op(ctx, wait_comp_event, cmd);
		if (ret)
			return ret;
	}
	/* Advance to the next sub-frame */
	for (index = 0; index < num_comp; index++) {
		if (CFG_CHECK(MT8183, p_id))
			inner_id = CFG_GET(MT8183, path->config, components[index].type);
		else if (CFG_CHECK(MT8195, p_id))
			inner_id = CFG_GET(MT8195, path->config, components[index].type);

		if (mdp_cfg_comp_is_dummy(path->mdp_dev, inner_id))
			continue;
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
	s32 inner_id = MDP_COMP_NONE;

	if (CFG_CHECK(MT8183, p_id))
		num_comp = CFG_GET(MT8183, path->config, num_components);
	else if (CFG_CHECK(MT8195, p_id))
		num_comp = CFG_GET(MT8195, path->config, num_components);

	if (CFG_CHECK(MT8183, p_id))
		num_sub = CFG_GET(MT8183, path->config, num_subfrms);
	else if (CFG_CHECK(MT8195, p_id))
		num_sub = CFG_GET(MT8195, path->config, num_subfrms);

	/* Config path frame */
	/* Reset components */
	for (index = 0; index < num_comp; index++) {
		if (CFG_CHECK(MT8183, p_id))
			inner_id = CFG_GET(MT8183, path->config, components[index].type);
		else if (CFG_CHECK(MT8195, p_id))
			inner_id = CFG_GET(MT8195, path->config, components[index].type);

		if (mdp_cfg_comp_is_dummy(path->mdp_dev, inner_id))
			continue;
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
			inner_id = CFG_GET(MT8183, path->config, components[index].type);
		else if (CFG_CHECK(MT8195, p_id))
			inner_id = CFG_GET(MT8195, path->config, components[index].type);

		if (mdp_cfg_comp_is_dummy(path->mdp_dev, inner_id))
			continue;

		if (CFG_CHECK(MT8183, p_id))
			out = CFG_COMP(MT8183, ctx->param, outputs[0]);
		else if (CFG_CHECK(MT8195, p_id))
			out = CFG_COMP(MT8195, ctx->param, outputs[0]);

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
		if (CFG_CHECK(MT8183, p_id))
			inner_id = CFG_GET(MT8183, path->config, components[index].type);
		else if (CFG_CHECK(MT8195, p_id))
			inner_id = CFG_GET(MT8195, path->config, components[index].type);

		if (mdp_cfg_comp_is_dummy(path->mdp_dev, inner_id))
			continue;
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
	struct mtk_mutex *mutex;
	enum mdp_pipe_id pipe_id;

	cmd = container_of(work, struct mdp_cmdq_cmd, auto_release_work);
	mdp = cmd->mdp;

	pipe_id = __get_pipe(mdp, cmd->comps[0].public_id);
	mutex = __get_mutex(mdp, &mdp->mdp_data->pipe_info[pipe_id]);
	mtk_mutex_unprepare(mutex);
	mdp_comp_clocks_off(&mdp->pdev->dev, cmd->comps,
			    cmd->num_comps);

	if (refcount_dec_and_test(&mdp->job_count)) {
		if (cmd->mdp_ctx)
			mdp_m2m_job_finish(cmd->mdp_ctx);

		if (cmd->user_cmdq_cb) {
			struct cmdq_cb_data user_cb_data;

			user_cb_data.sta = cmd->data->sta;
			user_cb_data.pkt = cmd->data->pkt;
			cmd->user_cmdq_cb(user_cb_data);
		}
		wake_up(&mdp->callback_wq);
	}

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
	enum mdp_pipe_id pipe_id;

	if (!mssg) {
		pr_info("%s:no callback data\n", __func__);
		return;
	}

	data = (struct cmdq_cb_data *)mssg;
	cmd = container_of(data->pkt, struct mdp_cmdq_cmd, pkt);
	cmd->data = data;
	mdp = cmd->mdp;
	dev = &mdp->pdev->dev;

	INIT_WORK(&cmd->auto_release_work, mdp_auto_release_work);
	if (!queue_work(mdp->clock_wq, &cmd->auto_release_work)) {
		struct mtk_mutex *mutex;

		dev_err(dev, "%s:queue_work fail!\n", __func__);
		pipe_id = __get_pipe(mdp, cmd->comps[0].public_id);
		mutex = __get_mutex(mdp, &mdp->mdp_data->pipe_info[pipe_id]);
		mtk_mutex_unprepare(mutex);
		mdp_comp_clocks_off(&mdp->pdev->dev, cmd->comps,
				    cmd->num_comps);

		if (refcount_dec_and_test(&mdp->job_count))
			wake_up(&mdp->callback_wq);

		mdp_cmdq_pkt_destroy(&cmd->pkt);
		kfree(cmd->comps);
		cmd->comps = NULL;
		kfree(cmd);
		cmd = NULL;
	}
}

static struct mdp_cmdq_cmd *mdp_cmdq_prepare(struct mdp_dev *mdp,
					     struct mdp_cmdq_param *param,
					     u8 pp_idx)
{
	struct mdp_path *path = NULL;
	struct mdp_cmdq_cmd *cmd = NULL;
	struct mdp_comp *comps = NULL;
	struct device *dev = &mdp->pdev->dev;
	const int p_id = mdp->mdp_data->mdp_plat_id;
	struct img_config *config;
	struct mtk_mutex *mutex = NULL;
	enum mdp_pipe_id pipe_id;
	int i, ret = -ECANCELED;
	u32 num_comp;

	config = __get_config_offset(mdp, param, pp_idx);
	if (IS_ERR(config)) {
		ret = PTR_ERR(config);
		goto err_uninit;
	}

	if (CFG_CHECK(MT8183, p_id))
		num_comp = CFG_GET(MT8183, config, num_components);
	else if (CFG_CHECK(MT8195, p_id))
		num_comp = CFG_GET(MT8195, config, num_components);
	else
		goto err_uninit;

	cmd = kzalloc(sizeof(*cmd), GFP_KERNEL);
	if (!cmd) {
		ret = -ENOMEM;
		goto err_uninit;
	}

	ret = mdp_cmdq_pkt_create(mdp->cmdq_clt[pp_idx], &cmd->pkt, SZ_16K);
	if (ret)
		goto err_free_cmd;

	if (CFG_CHECK(MT8183, p_id)) {
		num_comp = CFG_GET(MT8183, param->config, num_components);
	} else if (CFG_CHECK(MT8195, p_id)) {
		num_comp = CFG_GET(MT8195, param->config, num_components);
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

	path->mdp_dev = mdp;
	path->config = config;
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
		dev_err(dev, "mdp_path_ctx_init error %d\n", pp_idx);
		goto err_free_path;
	}

	pipe_id = __get_pipe(mdp, path->comps[0].comp->public_id);
	mutex = __get_mutex(mdp, &mdp->mdp_data->pipe_info[pipe_id]);
	ret = mtk_mutex_prepare(mutex);
	if (ret) {
		dev_err(dev, "Fail to enable mutex %d clk\n", pp_idx);
		goto err_free_path;
	}

	ret = mdp_path_config(mdp, cmd, path);
	if (ret) {
		dev_err(dev, "mdp_path_config error %d\n", pp_idx);
		goto err_free_path;
	}
	cmdq_pkt_finalize(&cmd->pkt);

	for (i = 0; i < num_comp; i++) {
		s32 inner_id = MDP_COMP_NONE;

		if (CFG_CHECK(MT8183, p_id))
			inner_id = CFG_GET(MT8183, path->config, components[i].type);
		else if (CFG_CHECK(MT8195, p_id))
			inner_id = CFG_GET(MT8195, path->config, components[i].type);

		if (mdp_cfg_comp_is_dummy(mdp, inner_id))
			continue;
		memcpy(&comps[i], path->comps[i].comp,
		       sizeof(struct mdp_comp));
	}

	mdp->cmdq_clt[pp_idx]->client.rx_callback = mdp_handle_cmdq_callback;
	cmd->mdp = mdp;
	cmd->user_cmdq_cb = param->cmdq_cb;
	cmd->user_cb_data = param->cb_data;
	cmd->comps = comps;
	cmd->num_comps = num_comp;
	cmd->mdp_ctx = param->mdp_ctx;

	kfree(path);
	return cmd;

err_free_path:
	if (mutex)
		mtk_mutex_unprepare(mutex);
	kfree(path);
err_free_comps:
	kfree(comps);
err_destroy_pkt:
	mdp_cmdq_pkt_destroy(&cmd->pkt);
err_free_cmd:
	kfree(cmd);
err_uninit:
	return ERR_PTR(ret);
}

int mdp_cmdq_send(struct mdp_dev *mdp, struct mdp_cmdq_param *param)
{
	struct mdp_cmdq_cmd *cmd[MDP_PP_MAX] = {NULL};
	struct device *dev = &mdp->pdev->dev;
	int i, ret;
	u8 pp_used = __get_pp_num(param->param->type);

	refcount_set(&mdp->job_count, pp_used);
	if (atomic_read(&mdp->suspended)) {
		refcount_set(&mdp->job_count, 0);
		return -ECANCELED;
	}

	for (i = 0; i < pp_used; i++) {
		cmd[i] = mdp_cmdq_prepare(mdp, param, i);
		if (IS_ERR_OR_NULL(cmd[i])) {
			ret = PTR_ERR(cmd[i]);
			goto err_cancel_job;
		}
	}

	for (i = 0; i < pp_used; i++) {
		ret = mdp_comp_clocks_on(&mdp->pdev->dev, cmd[i]->comps, cmd[i]->num_comps);
		if (ret)
			goto err_clock_off;
	}

	for (i = 0; i < pp_used; i++) {
		dma_sync_single_for_device(mdp->cmdq_clt[i]->chan->mbox->dev,
					   cmd[i]->pkt.pa_base, cmd[i]->pkt.cmd_buf_size,
					   DMA_TO_DEVICE);

		ret = mbox_send_message(mdp->cmdq_clt[i]->chan, &cmd[i]->pkt);
		if (ret < 0) {
			dev_err(dev, "mbox send message fail %d!\n", ret);
			i = pp_used;
			goto err_clock_off;
		}
		mbox_client_txdone(mdp->cmdq_clt[i]->chan, 0);
	}
	return 0;

err_clock_off:
	while (--i >= 0)
		mdp_comp_clocks_off(&mdp->pdev->dev, cmd[i]->comps,
				    cmd[i]->num_comps);
err_cancel_job:
	refcount_set(&mdp->job_count, 0);

	return ret;
}
EXPORT_SYMBOL_GPL(mdp_cmdq_send);
