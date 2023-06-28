// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2022-2023 Qualcomm Innovation Center, Inc. All rights reserved.
 */
#define pr_fmt(fmt) "%s: " fmt, __func__

#include <linux/spinlock.h>
#include <linux/slab.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/interrupt.h>
#include <linux/virtio.h>
#include <linux/virtio_regulator.h>
#include <linux/scatterlist.h>
#include <linux/delay.h>
#include <linux/completion.h>
#include <linux/mutex.h>
#include <linux/of.h>
#include <linux/regulator/driver.h>
#include <linux/regulator/machine.h>
#include <linux/regulator/of_regulator.h>
#include <linux/regulator/debug-regulator.h>

#define VIRTIO_REGULATOR_MAX_NAME		20
#define VIRTIO_REGULATOR_VOLTAGE_UNKNOWN	1

struct reg_virtio;

struct virtio_regulator {
	struct virtio_device	*vdev;
	struct virtqueue	*vq;
	struct completion	rsp_avail;
	struct mutex		lock;
	struct reg_virtio	*regs;
	int			regs_count;
};

struct reg_virtio {
	struct device_node		*of_node;
	struct regulator_desc		rdesc;
	struct regulator_dev		*rdev;
	struct virtio_regulator		*vreg;
	char				name[VIRTIO_REGULATOR_MAX_NAME];
	bool				enabled;
};

static int virtio_regulator_enable(struct regulator_dev *rdev)
{
	struct reg_virtio *reg = rdev_get_drvdata(rdev);
	struct virtio_regulator *vreg = reg->vreg;
	struct virtio_regulator_msg *req, *rsp;
	struct scatterlist sg[1];
	unsigned int len;
	int ret = 0;

	req = kzalloc(sizeof(struct virtio_regulator_msg), GFP_KERNEL);
	if (!req)
		return -ENOMEM;

	strscpy(req->name, reg->rdesc.name, sizeof(req->name));
	req->type = cpu_to_virtio32(vreg->vdev, VIRTIO_REGULATOR_T_ENABLE);
	sg_init_one(sg, req, sizeof(*req));

	mutex_lock(&vreg->lock);

	ret = virtqueue_add_outbuf(vreg->vq, sg, 1, req, GFP_KERNEL);
	if (ret) {
		pr_err("%s: fail to add output buffer\n", reg->rdesc.name);
		goto out;
	}

	virtqueue_kick(vreg->vq);

	wait_for_completion(&vreg->rsp_avail);

	rsp = virtqueue_get_buf(vreg->vq, &len);
	if (!rsp) {
		pr_err("%s: fail to get virtqueue buffer\n", reg->rdesc.name);
		ret = -EIO;
		goto out;
	}

	ret = virtio32_to_cpu(vreg->vdev, rsp->result);

	if (!ret)
		reg->enabled = true;
out:
	mutex_unlock(&vreg->lock);
	kfree(req);

	pr_debug("%s return %d\n", reg->rdesc.name, ret);

	return ret;
}

static int virtio_regulator_disable(struct regulator_dev *rdev)
{
	struct reg_virtio *reg = rdev_get_drvdata(rdev);
	struct virtio_regulator *vreg = reg->vreg;
	struct virtio_regulator_msg *req, *rsp;
	struct scatterlist sg[1];
	unsigned int len;
	int ret = 0;

	req = kzalloc(sizeof(struct virtio_regulator_msg), GFP_KERNEL);
	if (!req)
		return -ENOMEM;

	strscpy(req->name, reg->rdesc.name, sizeof(req->name));
	req->type = cpu_to_virtio32(vreg->vdev, VIRTIO_REGULATOR_T_DISABLE);
	sg_init_one(sg, req, sizeof(*req));

	mutex_lock(&vreg->lock);

	ret = virtqueue_add_outbuf(vreg->vq, sg, 1, req, GFP_KERNEL);
	if (ret) {
		pr_err("%s: fail to add output buffer\n", reg->rdesc.name);
		goto out;
	}

	virtqueue_kick(vreg->vq);

	wait_for_completion(&vreg->rsp_avail);

	rsp = virtqueue_get_buf(vreg->vq, &len);
	if (!rsp) {
		pr_err("%s: fail to get virtqueue buffer\n", reg->rdesc.name);
		ret = -EIO;
		goto out;
	}

	ret = virtio32_to_cpu(vreg->vdev, rsp->result);

	if (!ret)
		reg->enabled = false;
out:
	mutex_unlock(&vreg->lock);
	kfree(req);

	pr_debug("%s return %d\n", reg->rdesc.name, ret);

	return ret;
}

static int virtio_regulator_is_enabled(struct regulator_dev *rdev)
{
	struct reg_virtio *reg = rdev_get_drvdata(rdev);

	pr_debug("%s return %d\n", reg->rdesc.name, reg->enabled);

	return reg->enabled;
}

static int virtio_regulator_set_voltage(struct regulator_dev *rdev, int min_uV,
				  int max_uV, unsigned int *selector)
{
	struct reg_virtio *reg = rdev_get_drvdata(rdev);
	struct virtio_regulator *vreg = reg->vreg;
	struct virtio_regulator_msg *req, *rsp;
	struct scatterlist sg[1];
	unsigned int len;
	int ret = 0;

	req = kzalloc(sizeof(struct virtio_regulator_msg), GFP_KERNEL);
	if (!req)
		return -ENOMEM;

	strscpy(req->name, reg->rdesc.name, sizeof(req->name));
	req->type = cpu_to_virtio32(vreg->vdev, VIRTIO_REGULATOR_T_SET_VOLTAGE);
	req->data[0] = cpu_to_virtio32(vreg->vdev, DIV_ROUND_UP(min_uV, 1000));
	req->data[1] = cpu_to_virtio32(vreg->vdev, max_uV / 1000);
	sg_init_one(sg, req, sizeof(*req));

	mutex_lock(&vreg->lock);

	ret = virtqueue_add_outbuf(vreg->vq, sg, 1, req, GFP_KERNEL);
	if (ret) {
		pr_err("%s: fail to add output buffer\n", reg->rdesc.name);
		goto out;
	}

	virtqueue_kick(vreg->vq);

	wait_for_completion(&vreg->rsp_avail);

	rsp = virtqueue_get_buf(vreg->vq, &len);
	if (!rsp) {
		pr_err("%s: fail to get virtqueue buffer\n", reg->rdesc.name);
		ret = -EIO;
		goto out;
	}

	ret = virtio32_to_cpu(vreg->vdev, rsp->result);

out:
	mutex_unlock(&vreg->lock);
	kfree(req);

	pr_debug("%s return %d\n", reg->rdesc.name, ret);

	return ret;
}

static int virtio_regulator_get_voltage(struct regulator_dev *rdev)
{
	struct reg_virtio *reg = rdev_get_drvdata(rdev);
	struct virtio_regulator *vreg = reg->vreg;
	struct virtio_regulator_msg *req, *rsp;
	struct scatterlist sg[1];
	unsigned int len;
	int ret = 0;

	req = kzalloc(sizeof(struct virtio_regulator_msg), GFP_KERNEL);
	if (!req)
		return -ENOMEM;

	strscpy(req->name, reg->rdesc.name, sizeof(req->name));
	req->type = cpu_to_virtio32(vreg->vdev, VIRTIO_REGULATOR_T_GET_VOLTAGE);
	sg_init_one(sg, req, sizeof(*req));

	mutex_lock(&vreg->lock);

	ret = virtqueue_add_outbuf(vreg->vq, sg, 1, req, GFP_KERNEL);
	if (ret) {
		pr_err("%s: fail to add output buffer\n", reg->rdesc.name);
		goto out;
	}

	virtqueue_kick(vreg->vq);

	wait_for_completion(&vreg->rsp_avail);

	rsp = virtqueue_get_buf(vreg->vq, &len);
	if (!rsp) {
		pr_err("%s: fail to get virtqueue buffer\n", reg->rdesc.name);
		ret = -EIO;
		goto out;
	}

	if (rsp->result) {
		pr_debug("%s: error response (%d)\n", reg->rdesc.name,
				virtio32_to_cpu(vreg->vdev, rsp->result));
		ret = VIRTIO_REGULATOR_VOLTAGE_UNKNOWN;
	} else
		ret = virtio32_to_cpu(vreg->vdev, rsp->data[0]) * 1000;

out:
	mutex_unlock(&vreg->lock);
	kfree(req);

	pr_debug("%s return %d\n", reg->rdesc.name, ret);

	return ret;
}

static int virtio_regulator_set_mode(struct regulator_dev *rdev,
				   unsigned int mode)
{
	struct reg_virtio *reg = rdev_get_drvdata(rdev);
	struct virtio_regulator *vreg = reg->vreg;
	struct virtio_regulator_msg *req, *rsp;
	struct scatterlist sg[1];
	unsigned int len;
	int ret = 0;

	req = kzalloc(sizeof(struct virtio_regulator_msg), GFP_KERNEL);
	if (!req)
		return -ENOMEM;

	strscpy(req->name, reg->rdesc.name, sizeof(req->name));
	req->type = cpu_to_virtio32(vreg->vdev, VIRTIO_REGULATOR_T_SET_MODE);
	req->data[0] = cpu_to_virtio32(vreg->vdev, mode);
	sg_init_one(sg, req, sizeof(*req));

	mutex_lock(&vreg->lock);

	ret = virtqueue_add_outbuf(vreg->vq, sg, 1, req, GFP_KERNEL);
	if (ret) {
		pr_err("%s: fail to add output buffer\n", reg->rdesc.name);
		goto out;
	}

	virtqueue_kick(vreg->vq);

	wait_for_completion(&vreg->rsp_avail);

	rsp = virtqueue_get_buf(vreg->vq, &len);
	if (!rsp) {
		pr_err("%s: fail to get virtqueue buffer\n", reg->rdesc.name);
		ret = -EIO;
		goto out;
	}

	ret = virtio32_to_cpu(vreg->vdev, rsp->result);

out:
	mutex_unlock(&vreg->lock);
	kfree(req);

	pr_debug("%s return %d\n", reg->rdesc.name, ret);

	return ret;
}

static unsigned int virtio_regulator_get_mode(struct regulator_dev *rdev)
{
	struct reg_virtio *reg = rdev_get_drvdata(rdev);
	struct virtio_regulator *vreg = reg->vreg;
	struct virtio_regulator_msg *req, *rsp;
	struct scatterlist sg[1];
	unsigned int len;
	int ret = 0;

	req = kzalloc(sizeof(struct virtio_regulator_msg), GFP_KERNEL);
	if (!req)
		return -ENOMEM;

	strscpy(req->name, reg->rdesc.name, sizeof(req->name));
	req->type = cpu_to_virtio32(vreg->vdev, VIRTIO_REGULATOR_T_GET_MODE);
	sg_init_one(sg, req, sizeof(*req));

	mutex_lock(&vreg->lock);

	ret = virtqueue_add_outbuf(vreg->vq, sg, 1, req, GFP_KERNEL);
	if (ret) {
		pr_err("%s: fail to add output buffer\n", reg->rdesc.name);
		goto out;
	}

	virtqueue_kick(vreg->vq);

	wait_for_completion(&vreg->rsp_avail);

	rsp = virtqueue_get_buf(vreg->vq, &len);
	if (!rsp) {
		pr_err("%s: fail to get virtqueue buffer\n", reg->rdesc.name);
		ret = -EIO;
		goto out;
	}

	if (rsp->result) {
		pr_err("%s: error response (%d)\n", reg->rdesc.name,
				virtio32_to_cpu(vreg->vdev, rsp->result));
		ret = 0;
	} else
		ret = virtio32_to_cpu(vreg->vdev, rsp->data[0]);

out:
	mutex_unlock(&vreg->lock);
	kfree(req);

	pr_debug("%s return %d\n", reg->rdesc.name, ret);

	return ret;
}

static int virtio_regulator_set_load(struct regulator_dev *rdev, int load_ua)
{
	struct reg_virtio *reg = rdev_get_drvdata(rdev);
	struct virtio_regulator *vreg = reg->vreg;
	struct virtio_regulator_msg *req, *rsp;
	struct scatterlist sg[1];
	unsigned int len;
	int ret = 0;

	req = kzalloc(sizeof(struct virtio_regulator_msg), GFP_KERNEL);
	if (!req)
		return -ENOMEM;

	strscpy(req->name, reg->rdesc.name, sizeof(req->name));
	req->type = cpu_to_virtio32(vreg->vdev, VIRTIO_REGULATOR_T_SET_LOAD);
	req->data[0] = cpu_to_virtio32(vreg->vdev, load_ua);
	sg_init_one(sg, req, sizeof(*req));

	mutex_lock(&vreg->lock);

	ret = virtqueue_add_outbuf(vreg->vq, sg, 1, req, GFP_KERNEL);
	if (ret) {
		pr_err("%s: fail to add output buffer\n", reg->rdesc.name);
		goto out;
	}

	virtqueue_kick(vreg->vq);

	wait_for_completion(&vreg->rsp_avail);

	rsp = virtqueue_get_buf(vreg->vq, &len);
	if (!rsp) {
		pr_err("%s: fail to get virtqueue buffer\n", reg->rdesc.name);
		ret = -EIO;
		goto out;
	}

	ret = virtio32_to_cpu(vreg->vdev, rsp->result);

out:
	mutex_unlock(&vreg->lock);
	kfree(req);

	pr_debug("%s return %d\n", reg->rdesc.name, ret);

	return ret;
}

static const struct regulator_ops virtio_regulator_ops = {
	.enable			= virtio_regulator_enable,
	.disable		= virtio_regulator_disable,
	.is_enabled		= virtio_regulator_is_enabled,
	.set_voltage		= virtio_regulator_set_voltage,
	.get_voltage		= virtio_regulator_get_voltage,
	.set_mode		= virtio_regulator_set_mode,
	.get_mode		= virtio_regulator_get_mode,
	.set_load		= virtio_regulator_set_load,
};

static const struct regulator_ops virtio_regulator_switch_ops = {
	.enable			= virtio_regulator_enable,
	.disable		= virtio_regulator_disable,
	.is_enabled		= virtio_regulator_is_enabled,
};

static void virtio_regulator_isr(struct virtqueue *vq)
{
	struct virtio_regulator *vregulator = vq->vdev->priv;

	complete(&vregulator->rsp_avail);
}

static int virtio_regulator_init_vqs(struct virtio_regulator *vreg)
{
	struct virtqueue *vqs[1];
	vq_callback_t *cbs[] = { virtio_regulator_isr };
	static const char * const names[] = { "regulator" };
	int ret;

	ret = virtio_find_vqs(vreg->vdev, 1, vqs, cbs, names, NULL);
	if (ret)
		return ret;

	vreg->vq = vqs[0];

	return 0;
}

static int virtio_regulator_allocate_reg(struct virtio_regulator *vreg)
{
	struct device_node *parent_node, *node, *child_node;
	int i, ret;

	vreg->regs_count = 0;
	parent_node = vreg->vdev->dev.parent->of_node;

	for_each_available_child_of_node(parent_node, node) {
		for_each_available_child_of_node(node, child_node) {
			/* Skip child nodes handled by other drivers. */
			if (of_find_property(child_node, "compatible", NULL))
				continue;
			vreg->regs_count++;
		}
	}

	if (vreg->regs_count == 0) {
		dev_err(&vreg->vdev->dev,
				"could not find any regulator subnodes\n");
		return -ENODEV;
	}

	vreg->regs = devm_kcalloc(&vreg->vdev->dev, vreg->regs_count,
			sizeof(*vreg->regs), GFP_KERNEL);
	if (!vreg->regs)
		return -ENOMEM;

	i = 0;
	for_each_available_child_of_node(parent_node, node) {
		for_each_available_child_of_node(node, child_node) {
			/* Skip child nodes handled by other drivers. */
			if (of_find_property(child_node, "compatible", NULL))
				continue;

			vreg->regs[i].of_node = child_node;
			vreg->regs[i].vreg = vreg;

			ret = of_property_read_string(child_node, "regulator-name",
							&vreg->regs[i].rdesc.name);
			if (ret) {
				dev_err(&vreg->vdev->dev,
						"could not read regulator-name\n");
				return ret;
			}

			i++;
		}
	}

	return 0;
}

static int virtio_regulator_init_reg(struct reg_virtio *reg)
{
	struct device *dev = reg->vreg->vdev->dev.parent;
	struct regulator_config reg_config = {};
	struct regulator_init_data *init_data;
	int ret = 0;

	reg->rdesc.owner	= THIS_MODULE;
	reg->rdesc.type		= REGULATOR_VOLTAGE;
	reg->rdesc.ops		= &virtio_regulator_ops;

	init_data = of_get_regulator_init_data(dev, reg->of_node, &reg->rdesc);
	if (init_data == NULL)
		return -ENOMEM;

	if (init_data->constraints.min_uV == 0 &&
	    init_data->constraints.max_uV == 0)
		reg->rdesc.n_voltages = 0;
	else if (init_data->constraints.min_uV == init_data->constraints.max_uV)
		reg->rdesc.n_voltages = 1;
	else
		reg->rdesc.n_voltages = 2;

	if (reg->rdesc.n_voltages == 0) {
		reg->rdesc.ops = &virtio_regulator_switch_ops;
	} else {
		init_data->constraints.input_uV = init_data->constraints.max_uV;
		init_data->constraints.valid_ops_mask |= REGULATOR_CHANGE_VOLTAGE;
	}

	reg_config.dev			= dev;
	reg_config.init_data		= init_data;
	reg_config.of_node		= reg->of_node;
	reg_config.driver_data		= reg;

	reg->rdev = devm_regulator_register(dev, &reg->rdesc, &reg_config);
	if (IS_ERR(reg->rdev)) {
		ret = PTR_ERR(reg->rdev);
		reg->rdev = NULL;
		dev_err(&reg->vreg->vdev->dev, "fail to register regulator\n");
		return ret;
	}

	return ret;
}

static int virtio_regulator_probe(struct virtio_device *vdev)
{
	struct virtio_regulator *vreg;
	unsigned int i;
	int ret;

	if (!virtio_has_feature(vdev, VIRTIO_F_VERSION_1))
		return -ENODEV;

	vreg = devm_kzalloc(&vdev->dev, sizeof(struct virtio_regulator),
			GFP_KERNEL);
	if (!vreg)
		return -ENOMEM;

	vdev->priv = vreg;
	vreg->vdev = vdev;
	mutex_init(&vreg->lock);
	init_completion(&vreg->rsp_avail);

	ret = virtio_regulator_init_vqs(vreg);
	if (ret) {
		dev_err(&vdev->dev, "fail to initialize virtqueue\n");
		return ret;
	}

	virtio_device_ready(vdev);

	ret = virtio_regulator_allocate_reg(vreg);
	if (ret) {
		dev_err(&vdev->dev, "fail to allocate regulators\n");
		goto err_allocate_reg;
	}

	for (i = 0; i < vreg->regs_count; i++) {
		ret = virtio_regulator_init_reg(&vreg->regs[i]);
		if (ret) {
			dev_err(&vdev->dev, "fail to initialize regulator %s\n",
				vreg->regs[i].rdesc.name);
			goto err_init_reg;
		}

		ret = devm_regulator_debug_register(&vdev->dev, vreg->regs[i].rdev);
		if (ret) {
			dev_err(&vdev->dev, "fail to register regulator %s debugfs\n",
				vreg->regs[i].rdesc.name);
			goto err_register_reg_debug;
		}
	}

	dev_dbg(&vdev->dev, "virtio regulator probe successfully\n");

	return 0;

err_register_reg_debug:
err_init_reg:
err_allocate_reg:
	vdev->config->del_vqs(vdev);
	return ret;
}

static void virtio_regulator_remove(struct virtio_device *vdev)
{
	struct virtio_regulator *vreg = vdev->priv;
	void *buf;

	vdev->config->reset(vdev);
	while ((buf = virtqueue_detach_unused_buf(vreg->vq)) != NULL)
		kfree(buf);
	vdev->config->del_vqs(vdev);
}

static const struct virtio_device_id id_table[] = {
	{ VIRTIO_ID_REGULATOR, VIRTIO_DEV_ANY_ID },
	{ 0 },
};

static unsigned int features[] = {
};

static struct virtio_driver virtio_regulator_driver = {
	.feature_table			= features,
	.feature_table_size		= ARRAY_SIZE(features),
	.driver.name			= KBUILD_MODNAME,
	.driver.owner			= THIS_MODULE,
	.id_table			= id_table,
	.probe				= virtio_regulator_probe,
	.remove				= virtio_regulator_remove,
};

static int __init virtio_regulator_init(void)
{
	return register_virtio_driver(&virtio_regulator_driver);
}

static void __exit virtio_regulator_exit(void)
{
	unregister_virtio_driver(&virtio_regulator_driver);
}
subsys_initcall(virtio_regulator_init);
module_exit(virtio_regulator_exit);

MODULE_DEVICE_TABLE(virtio, id_table);
MODULE_DESCRIPTION("Virtio regulator driver");
MODULE_LICENSE("GPL");
