// SPDX-License-Identifier: GPL-2.0
// Copyright (c) 2015--2017 Intel Corporation.

#include <linux/delay.h>
#include <linux/i2c.h>
#include <linux/module.h>
#include <linux/pm_runtime.h>
#include <linux/regulator/consumer.h>
#include <media/v4l2-ctrls.h>
#include <media/v4l2-device.h>
#include <media/v4l2-event.h>

#define DW9714_NAME		"dw9714"
#define DW9714_MAX_FOCUS_POS	1023
/*
 * This sets the minimum granularity for the focus positions.
 * A value of 1 gives maximum accuracy for a desired focus position
 */
#define DW9714_FOCUS_STEPS	1
/*
 * This acts as the minimum granularity of lens movement.
 * Keep this value power of 2, so the control steps can be
 * uniformly adjusted for gradual lens movement, with desired
 * number of control steps.
 */
#define DW9714_CTRL_STEPS	16
#define DW9714_CTRL_DELAY_US	1000
/*
 * S[3:2] = 0x00, codes per step for "Linear Slope Control"
 * S[1:0] = 0x00, step period
 */
#define DW9714_DEFAULT_S 0x0
#define DW9714_VAL(data, s) ((data) << 4 | (s))

/* dw9714 device structure */
struct dw9714_device {
	struct v4l2_ctrl_handler ctrls_vcm;
	struct v4l2_subdev sd;
	u16 current_val;
	struct regulator *vcc;
};

static inline struct dw9714_device *to_dw9714_vcm(struct v4l2_ctrl *ctrl)
{
	return container_of(ctrl->handler, struct dw9714_device, ctrls_vcm);
}

static inline struct dw9714_device *sd_to_dw9714_vcm(struct v4l2_subdev *subdev)
{
	return container_of(subdev, struct dw9714_device, sd);
}

static int dw9714_i2c_write(struct i2c_client *client, u16 data)
{
	int ret;
	__be16 val = cpu_to_be16(data);

	ret = i2c_master_send(client, (const char *)&val, sizeof(val));
	if (ret != sizeof(val)) {
		dev_err(&client->dev, "I2C write fail\n");
		return -EIO;
	}
	return 0;
}

static int dw9714_t_focus_vcm(struct dw9714_device *dw9714_dev, u16 val)
{
	struct i2c_client *client = v4l2_get_subdevdata(&dw9714_dev->sd);

	dw9714_dev->current_val = val;

	return dw9714_i2c_write(client, DW9714_VAL(val, DW9714_DEFAULT_S));
}

static int dw9714_set_ctrl(struct v4l2_ctrl *ctrl)
{
	struct dw9714_device *dev_vcm = to_dw9714_vcm(ctrl);

	if (ctrl->id == V4L2_CID_FOCUS_ABSOLUTE)
		return dw9714_t_focus_vcm(dev_vcm, ctrl->val);

	return -EINVAL;
}

static const struct v4l2_ctrl_ops dw9714_vcm_ctrl_ops = {
	.s_ctrl = dw9714_set_ctrl,
};

static int dw9714_open(struct v4l2_subdev *sd, struct v4l2_subdev_fh *fh)
{
	return pm_runtime_resume_and_get(sd->dev);
}

static int dw9714_close(struct v4l2_subdev *sd, struct v4l2_subdev_fh *fh)
{
	pm_runtime_put(sd->dev);

	return 0;
}

static const struct v4l2_subdev_internal_ops dw9714_int_ops = {
	.open = dw9714_open,
	.close = dw9714_close,
};

static const struct v4l2_subdev_core_ops dw9714_core_ops = {
	.log_status = v4l2_ctrl_subdev_log_status,
	.subscribe_event = v4l2_ctrl_subdev_subscribe_event,
	.unsubscribe_event = v4l2_event_subdev_unsubscribe,
};

static const struct v4l2_subdev_ops dw9714_ops = {
	.core = &dw9714_core_ops,
};

static void dw9714_subdev_cleanup(struct dw9714_device *dw9714_dev)
{
	v4l2_async_unregister_subdev(&dw9714_dev->sd);
	v4l2_ctrl_handler_free(&dw9714_dev->ctrls_vcm);
	media_entity_cleanup(&dw9714_dev->sd.entity);
}

static int dw9714_init_controls(struct dw9714_device *dev_vcm)
{
	struct v4l2_ctrl_handler *hdl = &dev_vcm->ctrls_vcm;
	const struct v4l2_ctrl_ops *ops = &dw9714_vcm_ctrl_ops;

	v4l2_ctrl_handler_init(hdl, 1);

	v4l2_ctrl_new_std(hdl, ops, V4L2_CID_FOCUS_ABSOLUTE,
			  0, DW9714_MAX_FOCUS_POS, DW9714_FOCUS_STEPS, 0);

	if (hdl->error)
		dev_err(dev_vcm->sd.dev, "%s fail error: 0x%x\n",
			__func__, hdl->error);
	dev_vcm->sd.ctrl_handler = hdl;
	return hdl->error;
}

static int dw9714_probe(struct i2c_client *client)
{
	struct dw9714_device *dw9714_dev;
	int rval;

	dw9714_dev = devm_kzalloc(&client->dev, sizeof(*dw9714_dev),
				  GFP_KERNEL);
	if (dw9714_dev == NULL)
		return -ENOMEM;

	dw9714_dev->vcc = devm_regulator_get(&client->dev, "vcc");
	if (IS_ERR(dw9714_dev->vcc))
		return PTR_ERR(dw9714_dev->vcc);

	rval = regulator_enable(dw9714_dev->vcc);
	if (rval < 0) {
		dev_err(&client->dev, "failed to enable vcc: %d\n", rval);
		return rval;
	}

	usleep_range(1000, 2000);

	v4l2_i2c_subdev_init(&dw9714_dev->sd, client, &dw9714_ops);
	dw9714_dev->sd.flags |= V4L2_SUBDEV_FL_HAS_DEVNODE |
				V4L2_SUBDEV_FL_HAS_EVENTS;
	dw9714_dev->sd.internal_ops = &dw9714_int_ops;

	rval = dw9714_init_controls(dw9714_dev);
	if (rval)
		goto err_cleanup;

	rval = media_entity_pads_init(&dw9714_dev->sd.entity, 0, NULL);
	if (rval < 0)
		goto err_cleanup;

	dw9714_dev->sd.entity.function = MEDIA_ENT_F_LENS;

	rval = v4l2_async_register_subdev(&dw9714_dev->sd);
	if (rval < 0)
		goto err_cleanup;

	pm_runtime_set_active(&client->dev);
	pm_runtime_enable(&client->dev);
	pm_runtime_idle(&client->dev);

	return 0;

err_cleanup:
	regulator_disable(dw9714_dev->vcc);
	v4l2_ctrl_handler_free(&dw9714_dev->ctrls_vcm);
	media_entity_cleanup(&dw9714_dev->sd.entity);

	return rval;
}

static void dw9714_remove(struct i2c_client *client)
{
	struct v4l2_subdev *sd = i2c_get_clientdata(client);
	struct dw9714_device *dw9714_dev = sd_to_dw9714_vcm(sd);
	int ret;

	pm_runtime_disable(&client->dev);
	if (!pm_runtime_status_suspended(&client->dev)) {
		ret = regulator_disable(dw9714_dev->vcc);
		if (ret) {
			dev_err(&client->dev,
				"Failed to disable vcc: %d\n", ret);
		}
	}
	pm_runtime_set_suspended(&client->dev);
	dw9714_subdev_cleanup(dw9714_dev);
}

/*
 * This function sets the vcm position, so it consumes least current
 * The lens position is gradually moved in units of DW9714_CTRL_STEPS,
 * to make the movements smoothly.
 */
static int __maybe_unused dw9714_vcm_suspend(struct device *dev)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct v4l2_subdev *sd = i2c_get_clientdata(client);
	struct dw9714_device *dw9714_dev = sd_to_dw9714_vcm(sd);
	int ret, val;

	if (pm_runtime_suspended(&client->dev))
		return 0;

	for (val = dw9714_dev->current_val & ~(DW9714_CTRL_STEPS - 1);
	     val >= 0; val -= DW9714_CTRL_STEPS) {
		ret = dw9714_i2c_write(client,
				       DW9714_VAL(val, DW9714_DEFAULT_S));
		if (ret)
			dev_err_once(dev, "%s I2C failure: %d", __func__, ret);
		usleep_range(DW9714_CTRL_DELAY_US, DW9714_CTRL_DELAY_US + 10);
	}

	ret = regulator_disable(dw9714_dev->vcc);
	if (ret)
		dev_err(dev, "Failed to disable vcc: %d\n", ret);

	return ret;
}

/*
 * This function sets the vcm position to the value set by the user
 * through v4l2_ctrl_ops s_ctrl handler
 * The lens position is gradually moved in units of DW9714_CTRL_STEPS,
 * to make the movements smoothly.
 */
static int  __maybe_unused dw9714_vcm_resume(struct device *dev)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct v4l2_subdev *sd = i2c_get_clientdata(client);
	struct dw9714_device *dw9714_dev = sd_to_dw9714_vcm(sd);
	int ret, val;

	if (pm_runtime_suspended(&client->dev))
		return 0;

	ret = regulator_enable(dw9714_dev->vcc);
	if (ret) {
		dev_err(dev, "Failed to enable vcc: %d\n", ret);
		return ret;
	}
	usleep_range(1000, 2000);

	for (val = dw9714_dev->current_val % DW9714_CTRL_STEPS;
	     val < dw9714_dev->current_val + DW9714_CTRL_STEPS - 1;
	     val += DW9714_CTRL_STEPS) {
		ret = dw9714_i2c_write(client,
				       DW9714_VAL(val, DW9714_DEFAULT_S));
		if (ret)
			dev_err_ratelimited(dev, "%s I2C failure: %d",
						__func__, ret);
		usleep_range(DW9714_CTRL_DELAY_US, DW9714_CTRL_DELAY_US + 10);
	}

	return 0;
}

static const struct i2c_device_id dw9714_id_table[] = {
	{ DW9714_NAME },
	{ }
};
MODULE_DEVICE_TABLE(i2c, dw9714_id_table);

static const struct of_device_id dw9714_of_table[] = {
	{ .compatible = "dongwoon,dw9714" },
	{ { 0 } }
};
MODULE_DEVICE_TABLE(of, dw9714_of_table);

static const struct dev_pm_ops dw9714_pm_ops = {
	SET_SYSTEM_SLEEP_PM_OPS(dw9714_vcm_suspend, dw9714_vcm_resume)
	SET_RUNTIME_PM_OPS(dw9714_vcm_suspend, dw9714_vcm_resume, NULL)
};

static struct i2c_driver dw9714_i2c_driver = {
	.driver = {
		.name = DW9714_NAME,
		.pm = &dw9714_pm_ops,
		.of_match_table = dw9714_of_table,
	},
	.probe = dw9714_probe,
	.remove = dw9714_remove,
	.id_table = dw9714_id_table,
};

module_i2c_driver(dw9714_i2c_driver);

MODULE_AUTHOR("Tianshu Qiu <tian.shu.qiu@intel.com>");
MODULE_AUTHOR("Jian Xu Zheng");
MODULE_AUTHOR("Yuning Pu");
MODULE_AUTHOR("Jouni Ukkonen");
MODULE_AUTHOR("Tommi Franttila");
MODULE_DESCRIPTION("DW9714 VCM driver");
MODULE_LICENSE("GPL v2");
