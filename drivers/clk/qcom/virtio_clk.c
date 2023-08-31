// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2022-2023, Qualcomm Innovation Center, Inc. All rights reserved.
 */

#define pr_fmt(fmt) "%s: " fmt, __func__

#include <linux/spinlock.h>
#include <linux/slab.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/interrupt.h>
#include <linux/virtio.h>
#include <linux/virtio_clk.h>
#include <linux/scatterlist.h>
#include <linux/clk-provider.h>
#include <linux/reset-controller.h>
#include <linux/delay.h>
#include <linux/completion.h>
#include <linux/mutex.h>
#include "virtio_clk_common.h"

struct virtio_clk {
	struct virtio_device	*vdev;
	struct virtqueue	*vq;
	struct completion	rsp_avail;
	struct mutex		lock;
	struct reset_controller_dev rcdev;
	const struct clk_virtio_desc *desc;
	struct clk_virtio *clks;
	size_t num_clks;
	size_t num_resets;
};

#define to_clk_virtio(_hw) container_of(_hw, struct clk_virtio, hw)

struct clk_virtio {
	int clk_id;
	struct clk_hw hw;
	struct virtio_clk *vclk;
};

struct virtio_cc_map {
	char cc_name[20];
	const struct clk_virtio_desc *desc;
};

static int virtio_clk_prepare(struct clk_hw *hw)
{
	struct clk_virtio *v = to_clk_virtio(hw);
	struct virtio_clk *vclk = v->vclk;
	struct virtio_clk_msg *req, *rsp;
	struct scatterlist sg[1];
	unsigned int len;
	int ret = 0;

	pr_debug("%s\n", clk_hw_get_name(hw));

	req = kzalloc(sizeof(struct virtio_clk_msg), GFP_KERNEL);
	if (!req)
		return -ENOMEM;

	strscpy(req->name, clk_hw_get_name(hw), sizeof(req->name));
	req->id = cpu_to_virtio32(vclk->vdev, v->clk_id);
	req->type = cpu_to_virtio32(vclk->vdev, VIRTIO_CLK_T_ENABLE);
	sg_init_one(sg, req, sizeof(*req));

	mutex_lock(&vclk->lock);

	ret = virtqueue_add_outbuf(vclk->vq, sg, 1, req, GFP_KERNEL);
	if (ret) {
		pr_err("%s: fail to add output buffer (%d)\n",
				clk_hw_get_name(hw), ret);
		goto out;
	}

	virtqueue_kick(vclk->vq);

	wait_for_completion(&vclk->rsp_avail);

	rsp = virtqueue_get_buf(vclk->vq, &len);
	if (!rsp) {
		pr_err("%s: fail to get virtqueue buffer\n",
				clk_hw_get_name(hw));
		ret = -EIO;
		goto out;
	}

	ret = virtio32_to_cpu(vclk->vdev, rsp->result);
out:
	mutex_unlock(&vclk->lock);
	kfree(req);

	return ret;
}

static void virtio_clk_unprepare(struct clk_hw *hw)
{
	struct clk_virtio *v = to_clk_virtio(hw);
	struct virtio_clk *vclk = v->vclk;
	struct virtio_clk_msg *req, *rsp;
	struct scatterlist sg[1];
	unsigned int len;
	int ret = 0;

	pr_debug("%s\n", clk_hw_get_name(hw));

	req = kzalloc(sizeof(struct virtio_clk_msg), GFP_KERNEL);
	if (!req)
		return;

	strscpy(req->name, clk_hw_get_name(hw), sizeof(req->name));
	req->id = cpu_to_virtio32(vclk->vdev, v->clk_id);
	req->type = cpu_to_virtio32(vclk->vdev, VIRTIO_CLK_T_DISABLE);
	sg_init_one(sg, req, sizeof(*req));

	mutex_lock(&vclk->lock);

	ret = virtqueue_add_outbuf(vclk->vq, sg, 1, req, GFP_KERNEL);
	if (ret) {
		pr_err("%s: fail to add output buffer (%d)\n",
				clk_hw_get_name(hw), ret);
		goto out;
	}

	virtqueue_kick(vclk->vq);

	wait_for_completion(&vclk->rsp_avail);

	rsp = virtqueue_get_buf(vclk->vq, &len);
	if (!rsp) {
		pr_err("%s: fail to get virtqueue buffer\n",
				clk_hw_get_name(hw));
		goto out;
	}

	if (rsp->result)
		pr_err("%s: error response (%d)\n", clk_hw_get_name(hw),
				rsp->result);

out:
	mutex_unlock(&vclk->lock);
	kfree(req);
}

static int virtio_clk_set_rate(struct clk_hw *hw,
			    unsigned long rate, unsigned long parent_rate)
{
	struct clk_virtio *v = to_clk_virtio(hw);
	struct virtio_clk *vclk = v->vclk;
	struct virtio_clk_msg *req, *rsp;
	struct scatterlist sg[1];
	unsigned int len;
	int ret = 0;

	pr_debug("%s, rate: %lu, parent_rate: %lu\n", clk_hw_get_name(hw),
			rate, parent_rate);

	req = kzalloc(sizeof(struct virtio_clk_msg), GFP_KERNEL);
	if (!req)
		return -ENOMEM;

	strscpy(req->name, clk_hw_get_name(hw), sizeof(req->name));
	req->id = cpu_to_virtio32(vclk->vdev, v->clk_id);
	req->type = cpu_to_virtio32(vclk->vdev, VIRTIO_CLK_T_SET_RATE);
	req->data[0] = cpu_to_virtio32(vclk->vdev, rate);
	sg_init_one(sg, req, sizeof(*req));

	mutex_lock(&vclk->lock);

	ret = virtqueue_add_outbuf(vclk->vq, sg, 1, req, GFP_KERNEL);
	if (ret) {
		pr_err("%s: fail to add output buffer (%d)\n",
				clk_hw_get_name(hw), ret);
		goto out;
	}

	virtqueue_kick(vclk->vq);

	wait_for_completion(&vclk->rsp_avail);

	rsp = virtqueue_get_buf(vclk->vq, &len);
	if (!rsp) {
		pr_err("%s: fail to get virtqueue buffer\n",
				clk_hw_get_name(hw));
		ret = -EIO;
		goto out;
	}

	ret = virtio32_to_cpu(vclk->vdev, rsp->result);
out:
	mutex_unlock(&vclk->lock);
	kfree(req);

	return ret;
}

static long virtio_clk_round_rate(struct clk_hw *hw, unsigned long rate,
		unsigned long *parent_rate)
{
	struct clk_virtio *v = to_clk_virtio(hw);
	struct virtio_clk *vclk = v->vclk;
	struct virtio_clk_msg *req, *rsp;
	struct scatterlist sg[1];
	unsigned int len;
	int ret = 0;

	pr_debug("%s, rate: %lu\n", clk_hw_get_name(hw), rate);

	req = kzalloc(sizeof(struct virtio_clk_msg), GFP_KERNEL);
	if (!req)
		return 0;

	strscpy(req->name, clk_hw_get_name(hw), sizeof(req->name));
	req->id = cpu_to_virtio32(vclk->vdev, v->clk_id);
	req->type = cpu_to_virtio32(vclk->vdev, VIRTIO_CLK_T_ROUND_RATE);
	req->data[0] = cpu_to_virtio32(vclk->vdev, rate);
	sg_init_one(sg, req, sizeof(*req));

	mutex_lock(&vclk->lock);

	ret = virtqueue_add_outbuf(vclk->vq, sg, 1, req, GFP_KERNEL);
	if (ret) {
		pr_err("%s: fail to add output buffer (%d)\n",
				clk_hw_get_name(hw), ret);
		goto out;
	}

	virtqueue_kick(vclk->vq);

	wait_for_completion(&vclk->rsp_avail);

	rsp = virtqueue_get_buf(vclk->vq, &len);
	if (!rsp) {
		pr_err("%s: fail to get virtqueue buffer\n",
				clk_hw_get_name(hw));
		ret = 0;
		goto out;
	}

	if (rsp->result) {
		pr_err("%s: error response (%d)\n", clk_hw_get_name(hw),
				rsp->result);
		ret = 0;
	} else
		ret = virtio32_to_cpu(vclk->vdev, rsp->data[0]);

out:
	mutex_unlock(&vclk->lock);
	kfree(req);

	return ret;
}

static unsigned long virtio_clk_get_rate(struct clk_hw *hw,
		unsigned long parent_rate)
{
	struct clk_virtio *v = to_clk_virtio(hw);
	struct virtio_clk *vclk = v->vclk;
	struct virtio_clk_msg *req, *rsp;
	struct scatterlist sg[1];
	unsigned int len;
	int ret = 0;

	req = kzalloc(sizeof(struct virtio_clk_msg), GFP_KERNEL);
	if (!req)
		return 0;

	strscpy(req->name, clk_hw_get_name(hw), sizeof(req->name));
	req->id = cpu_to_virtio32(vclk->vdev, v->clk_id);
	req->type = cpu_to_virtio32(vclk->vdev, VIRTIO_CLK_T_GET_RATE);
	sg_init_one(sg, req, sizeof(*req));

	mutex_lock(&vclk->lock);

	ret = virtqueue_add_outbuf(vclk->vq, sg, 1, req, GFP_KERNEL);
	if (ret) {
		pr_err("%s: fail to add output buffer (%d)\n",
				clk_hw_get_name(hw), ret);
		goto out;
	}

	virtqueue_kick(vclk->vq);

	wait_for_completion(&vclk->rsp_avail);

	rsp = virtqueue_get_buf(vclk->vq, &len);
	if (!rsp) {
		pr_err("%s: fail to get virtqueue buffer\n",
				clk_hw_get_name(hw));
		ret = 0;
		goto out;
	}

	if (rsp->result) {
		/*
		 * Some clocks do not support getting rate.
		 * If getting clock rate is failing, return 0.
		 */
		pr_debug("%s: error response (%d)\n", clk_hw_get_name(hw),
				rsp->result);
		ret = 0;
	} else
		ret = virtio32_to_cpu(vclk->vdev, rsp->data[0]);

out:
	mutex_unlock(&vclk->lock);
	kfree(req);

	return ret;
}

static int virtio_clk_set_parent(struct clk_hw *hw, u8 index)
{
	struct clk_virtio *v = to_clk_virtio(hw);
	struct virtio_clk *vclk = v->vclk;
	struct virtio_clk_msg *req, *rsp;
	struct scatterlist sg[1];
	unsigned int len;
	int ret = 0;

	pr_debug("%s, parent index: %d\n", clk_hw_get_name(hw), index);

	req = kzalloc(sizeof(struct virtio_clk_msg), GFP_KERNEL);
	if (!req)
		return 0;

	strscpy(req->name, clk_hw_get_name(hw), sizeof(req->name));
	req->id = cpu_to_virtio32(vclk->vdev, v->clk_id);
	req->type = cpu_to_virtio32(vclk->vdev, VIRTIO_CLK_T_SET_PARENT);
	req->data[0] = cpu_to_virtio32(vclk->vdev, index);
	sg_init_one(sg, req, sizeof(*req));

	mutex_lock(&vclk->lock);

	ret = virtqueue_add_outbuf(vclk->vq, sg, 1, req, GFP_KERNEL);
	if (ret) {
		pr_err("%s: fail to add output buffer (%d)\n",
				clk_hw_get_name(hw), ret);
		goto out;
	}

	virtqueue_kick(vclk->vq);

	wait_for_completion(&vclk->rsp_avail);

	rsp = virtqueue_get_buf(vclk->vq, &len);
	if (!rsp) {
		pr_err("%s: fail to get virtqueue buffer\n",
				clk_hw_get_name(hw));
		ret = 0;
		goto out;
	}

	ret = virtio32_to_cpu(vclk->vdev, rsp->result);

out:
	mutex_unlock(&vclk->lock);
	kfree(req);

	return ret;
}

static u8 virtio_clk_get_parent(struct clk_hw *hw)
{
	return U8_MAX;
}

static const struct clk_ops clk_virtio_ops = {
	.prepare	= virtio_clk_prepare,
	.unprepare	= virtio_clk_unprepare,
	.set_rate	= virtio_clk_set_rate,
	.round_rate	= virtio_clk_round_rate,
	.recalc_rate	= virtio_clk_get_rate,
	.set_parent	= virtio_clk_set_parent,
	.get_parent	= virtio_clk_get_parent,
};

static int
__virtio_reset(struct reset_controller_dev *rcdev, unsigned long id,
		unsigned int action)
{
	struct virtio_clk *vclk = container_of(rcdev, struct virtio_clk, rcdev);
	struct virtio_clk_msg *req, *rsp;
	struct scatterlist sg[1];
	unsigned int len;
	int ret = 0;

	pr_debug("%s, action: %d\n", vclk->desc->reset_names[id], action);

	req = kzalloc(sizeof(struct virtio_clk_msg), GFP_KERNEL);
	if (!req)
		return -ENOMEM;

	if (vclk->desc && vclk->desc->reset_names[id])
		strscpy(req->name, vclk->desc->reset_names[id],
				sizeof(req->name));
	req->id = cpu_to_virtio32(vclk->vdev, id);
	req->type = cpu_to_virtio32(vclk->vdev, VIRTIO_CLK_T_RESET);
	req->data[0] = cpu_to_virtio32(vclk->vdev, action);
	sg_init_one(sg, req, sizeof(*req));

	mutex_lock(&vclk->lock);

	ret = virtqueue_add_outbuf(vclk->vq, sg, 1, req, GFP_KERNEL);
	if (ret) {
		pr_err("fail to add output buffer (%d)\n", ret);
		goto out;
	}

	virtqueue_kick(vclk->vq);

	wait_for_completion(&vclk->rsp_avail);

	rsp = virtqueue_get_buf(vclk->vq, &len);
	if (!rsp) {
		pr_err("fail to get virtqueue buffer\n");
		ret = -EIO;
		goto out;
	}

	ret = virtio32_to_cpu(vclk->vdev, rsp->result);

out:
	mutex_unlock(&vclk->lock);
	kfree(req);

	return ret;
}

static int
virtio_reset_assert(struct reset_controller_dev *rcdev, unsigned long id)
{
	return __virtio_reset(rcdev, id, 1);
}

static int
virtio_reset_deassert(struct reset_controller_dev *rcdev, unsigned long id)
{
	return __virtio_reset(rcdev, id, 0);
}

static int
virtio_reset(struct reset_controller_dev *rcdev, unsigned long id)
{
	rcdev->ops->assert(rcdev, id);
	udelay(1);
	rcdev->ops->deassert(rcdev, id);
	return 0;
}

static const struct reset_control_ops virtio_reset_ops = {
	.reset = virtio_reset,
	.assert = virtio_reset_assert,
	.deassert = virtio_reset_deassert,
};

static void virtclk_isr(struct virtqueue *vq)
{
	struct virtio_clk *vclk = vq->vdev->priv;

	complete(&vclk->rsp_avail);
}

static int virtclk_init_vqs(struct virtio_clk *vclk)
{
	struct virtqueue *vqs[1];
	vq_callback_t *cbs[] = { virtclk_isr };
	static const char * const names[] = { "clock" };
	int ret;

	ret = virtio_find_vqs(vclk->vdev, 1, vqs, cbs, names, NULL);
	if (ret)
		return ret;

	vclk->vq = vqs[0];

	return 0;
}

static const struct virtio_cc_map clk_virtio_map_table[] = {
	{ .cc_name = "sm8150-gcc", .desc = &clk_virtio_sm8150_gcc, },
	{ .cc_name = "sm8150-scc", .desc = &clk_virtio_sm8150_scc, },
	{ .cc_name = "sm6150-gcc", .desc = &clk_virtio_sm6150_gcc, },
	{ .cc_name = "sm6150-scc", .desc = &clk_virtio_sm6150_scc, },
	{ .cc_name = "sa8195p-gcc", .desc = &clk_virtio_sa8195p_gcc, },
	{ .cc_name = "direwolf-gcc", .desc = &clk_virtio_direwolf_gcc, },
	{ .cc_name = "lemans-gcc", .desc = &clk_virtio_lemans_gcc, },
	{ .cc_name = "monaco-gcc", .desc = &clk_virtio_monaco_gcc, },
	{ }
};

static const struct clk_virtio_desc *virtclk_find_desc(
		const struct virtio_cc_map *maps,
		const char *name)
{
	if (!maps)
		return NULL;

	for (; maps->cc_name[0]; maps++) {
		if (!strcmp(name, maps->cc_name))
			return maps->desc;
	}

	return NULL;
}

static struct clk_hw *
of_clk_hw_virtio_get(struct of_phandle_args *clkspec, void *data)
{
	struct virtio_clk *vclk = data;
	unsigned int idx = clkspec->args[0];

	if (idx >= vclk->num_clks) {
		pr_err("%s: invalid index %u\n", __func__, idx);
		return ERR_PTR(-EINVAL);
	}

	return &vclk->clks[idx].hw;
}

static int virtio_clk_probe(struct virtio_device *vdev)
{
	const struct clk_virtio_desc *desc = NULL;
	struct virtio_clk *vclk;
	struct virtio_clk_config config;
	struct clk_virtio *virtio_clks;
	char name[40];
	struct clk_init_data init;
	static int instance;
	unsigned int i;
	int ret;

	if (!virtio_has_feature(vdev, VIRTIO_F_VERSION_1))
		return -ENODEV;

	vclk = devm_kzalloc(&vdev->dev, sizeof(*vclk), GFP_KERNEL);
	if (!vclk)
		return -ENOMEM;

	vdev->priv = vclk;
	vclk->vdev = vdev;
	mutex_init(&vclk->lock);
	init_completion(&vclk->rsp_avail);

	ret = virtclk_init_vqs(vclk);
	if (ret) {
		dev_err(&vdev->dev, "failed to initialized virtqueue\n");
		return ret;
	}

	virtio_device_ready(vdev);

	memset(&config, 0x0, sizeof(config));

	virtio_cread(vdev, struct virtio_clk_config, num_clks,
			&config.num_clks);
	virtio_cread_feature(vdev, VIRTIO_CLK_F_RESET, struct virtio_clk_config,
			num_resets, &config.num_resets);

	if (virtio_has_feature(vdev, VIRTIO_CLK_F_NAME)) {
		virtio_cread_bytes(vdev,
				   offsetof(struct virtio_clk_config, name),
				   &config.name, sizeof(config.name));
		desc = virtclk_find_desc(clk_virtio_map_table, config.name);
		if (!desc) {
			ret = -ENXIO;
			goto err_find_desc;
		}
	}

	dev_dbg(&vdev->dev, "num_clks=%d, num_resets=%d, name=%s\n",
			config.num_clks, config.num_resets, config.name);

	if (desc) {
		vclk->desc = desc;
		vclk->num_clks = desc->num_clks;
		if (desc->num_resets > 0)
			vclk->num_resets = desc->num_resets;
	} else {
		vclk->num_clks = config.num_clks;
		if (config.num_resets > 0)
			vclk->num_resets = config.num_resets;
	}

	virtio_clks = devm_kcalloc(&vdev->dev, vclk->num_clks,
			sizeof(struct clk_virtio), GFP_KERNEL);
	if (!virtio_clks) {
		ret = -ENOMEM;
		goto err_kcalloc;
	}

	vclk->clks = virtio_clks;

	memset(&init, 0x0, sizeof(init));
	init.ops = &clk_virtio_ops;

	if (desc) {
		for (i = 0; i < vclk->num_clks; i++) {
			if (!desc->clks[i].name)
				continue;

			virtio_clks[i].clk_id = i;
			virtio_clks[i].vclk = vclk;
			init.name = desc->clks[i].name;
			init.parent_names = desc->clks[i].parent_names;
			init.num_parents = desc->clks[i].num_parents;
			virtio_clks[i].hw.init = &init;
			ret = devm_clk_hw_register(&vdev->dev,
					&virtio_clks[i].hw);
			if (ret) {
				dev_err(&vdev->dev, "fail to register clock\n");
				goto err_clk_register;
			}
		}
	} else {
		init.name = name;

		for (i = 0; i < config.num_clks; i++) {
			virtio_clks[i].clk_id = i;
			virtio_clks[i].vclk = vclk;
			snprintf(name, sizeof(name), "virtio_%d_%d",
					instance, i);
			virtio_clks[i].hw.init = &init;
			ret = devm_clk_hw_register(&vdev->dev,
					&virtio_clks[i].hw);
			if (ret) {
				dev_err(&vdev->dev, "fail to register clock\n");
				goto err_clk_register;
			}
		}
	}

	ret = devm_of_clk_add_hw_provider(vdev->dev.parent,
			of_clk_hw_virtio_get, vclk);
	if (ret) {
		dev_err(&vdev->dev, "failed to add clock provider\n");
		goto err_clk_register;
	}

	if (vclk->num_resets > 0) {
		vclk->rcdev.of_node = vdev->dev.parent->of_node;
		vclk->rcdev.ops = &virtio_reset_ops;
		vclk->rcdev.owner = vdev->dev.driver->owner;
		vclk->rcdev.nr_resets = vclk->num_resets;
		ret = devm_reset_controller_register(&vdev->dev, &vclk->rcdev);
		if (ret)
			goto err_rst_register;
	}

	instance++;

	dev_info(&vdev->dev, "Registered virtio clocks (%s)\n", config.name);

	return 0;

err_rst_register:
err_clk_register:
err_kcalloc:
err_find_desc:
	vdev->config->del_vqs(vdev);
	return ret;
}

static const struct virtio_device_id id_table[] = {
	{ VIRTIO_ID_CLOCK, VIRTIO_DEV_ANY_ID },
	{ 0 },
};

static unsigned int features[] = {
	VIRTIO_CLK_F_RESET,
	VIRTIO_CLK_F_NAME,
};

static struct virtio_driver virtio_clk_driver = {
	.feature_table			= features,
	.feature_table_size		= ARRAY_SIZE(features),
	.driver.name			= KBUILD_MODNAME,
	.driver.owner			= THIS_MODULE,
	.id_table			= id_table,
	.probe				= virtio_clk_probe,
};

static int __init virtio_clk_init(void)
{
	return register_virtio_driver(&virtio_clk_driver);
}

static void __exit virtio_clk_fini(void)
{
	unregister_virtio_driver(&virtio_clk_driver);
}
subsys_initcall_sync(virtio_clk_init);
module_exit(virtio_clk_fini);

MODULE_DEVICE_TABLE(virtio, id_table);
MODULE_DESCRIPTION("Virtio clock driver");
MODULE_LICENSE("GPL");
