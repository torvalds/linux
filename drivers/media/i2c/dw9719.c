// SPDX-License-Identifier: GPL-2.0
// Copyright (c) 2012 Intel Corporation

/*
 * Based on linux/modules/camera/drivers/media/i2c/imx/dw9719.c from:
 * https://github.com/ZenfoneArea/android_kernel_asus_zenfone5 and
 * latte-l-oss/drivers/external_drivers/camera/drivers/media/i2c/micam/dw9761.c
 * from: https://github.com/MiCode/Xiaomi_Kernel_OpenSource/
 */

#include <linux/delay.h>
#include <linux/i2c.h>
#include <linux/pm_runtime.h>
#include <linux/regulator/consumer.h>
#include <linux/types.h>

#include <media/v4l2-cci.h>
#include <media/v4l2-common.h>
#include <media/v4l2-ctrls.h>
#include <media/v4l2-subdev.h>

#define DW9719_MAX_FOCUS_POS	1023
#define DW9719_CTRL_STEPS	16
#define DW9719_CTRL_DELAY_US	1000

#define DW9719_INFO			CCI_REG8(0)
#define DW9719_ID			0xF1
#define DW9761_ID			0xF4

#define DW9719_CONTROL			CCI_REG8(2)
#define DW9719_STANDBY			0x00
#define DW9719_SHUTDOWN			0x01
#define DW9719_ENABLE_RINGING		0x02

#define DW9719_VCM_CURRENT		CCI_REG16(3)

#define DW9719_STATUS			CCI_REG16(5)
#define DW9719_STATUS_BUSY		BIT(0)

#define DW9719_MODE			CCI_REG8(6)
#define DW9719_MODE_SAC_SHIFT		4
#define DW9719_DEFAULT_SAC		4
#define DW9761_DEFAULT_SAC		6

#define DW9719_VCM_FREQ			CCI_REG8(7)
#define DW9719_DEFAULT_VCM_FREQ		0x60
#define DW9761_DEFAULT_VCM_FREQ		0x3E

#define DW9761_VCM_PRELOAD		CCI_REG8(8)
#define DW9761_DEFAULT_VCM_PRELOAD	0x73


#define to_dw9719_device(x) container_of(x, struct dw9719_device, sd)

enum dw9719_model {
	DW9719,
	DW9761,
};

struct dw9719_device {
	struct v4l2_subdev sd;
	struct device *dev;
	struct regmap *regmap;
	struct regulator *regulator;
	enum dw9719_model model;
	u32 mode_low_bits;
	u32 sac_mode;
	u32 vcm_freq;

	struct dw9719_v4l2_ctrls {
		struct v4l2_ctrl_handler handler;
		struct v4l2_ctrl *focus;
	} ctrls;
};

static int dw9719_power_down(struct dw9719_device *dw9719)
{
	return regulator_disable(dw9719->regulator);
}

static int dw9719_power_up(struct dw9719_device *dw9719, bool detect)
{
	u64 val;
	int ret;

	ret = regulator_enable(dw9719->regulator);
	if (ret)
		return ret;

	/* Jiggle SCL pin to wake up device */
	cci_write(dw9719->regmap, DW9719_CONTROL, DW9719_SHUTDOWN, &ret);
	fsleep(100);
	cci_write(dw9719->regmap, DW9719_CONTROL, DW9719_STANDBY, &ret);
	/* Need 100us to transit from SHUTDOWN to STANDBY */
	fsleep(100);

	if (detect) {
		ret = cci_read(dw9719->regmap, DW9719_INFO, &val, NULL);
		if (ret < 0)
			return ret;

		switch (val) {
		case DW9719_ID:
			dw9719->model = DW9719;
			dw9719->mode_low_bits = 0x00;
			dw9719->sac_mode = DW9719_DEFAULT_SAC;
			dw9719->vcm_freq = DW9719_DEFAULT_VCM_FREQ;
			break;
		case DW9761_ID:
			dw9719->model = DW9761;
			dw9719->mode_low_bits = 0x01;
			dw9719->sac_mode = DW9761_DEFAULT_SAC;
			dw9719->vcm_freq = DW9761_DEFAULT_VCM_FREQ;
			break;
		default:
			dev_err(dw9719->dev,
				"Error unknown device id 0x%02llx\n", val);
			return -ENXIO;
		}

		/* Optional indication of SAC mode select */
		device_property_read_u32(dw9719->dev, "dongwoon,sac-mode",
					 &dw9719->sac_mode);

		/* Optional indication of VCM frequency */
		device_property_read_u32(dw9719->dev, "dongwoon,vcm-freq",
					 &dw9719->vcm_freq);
	}

	cci_write(dw9719->regmap, DW9719_CONTROL, DW9719_ENABLE_RINGING, &ret);
	cci_write(dw9719->regmap, DW9719_MODE, dw9719->mode_low_bits |
			  (dw9719->sac_mode << DW9719_MODE_SAC_SHIFT), &ret);
	cci_write(dw9719->regmap, DW9719_VCM_FREQ, dw9719->vcm_freq, &ret);

	if (dw9719->model == DW9761)
		cci_write(dw9719->regmap, DW9761_VCM_PRELOAD,
			  DW9761_DEFAULT_VCM_PRELOAD, &ret);

	if (ret)
		dw9719_power_down(dw9719);

	return ret;
}

static int dw9719_t_focus_abs(struct dw9719_device *dw9719, s32 value)
{
	return cci_write(dw9719->regmap, DW9719_VCM_CURRENT, value, NULL);
}

static int dw9719_set_ctrl(struct v4l2_ctrl *ctrl)
{
	struct dw9719_device *dw9719 = container_of(ctrl->handler,
						    struct dw9719_device,
						    ctrls.handler);
	int ret;

	/* Only apply changes to the controls if the device is powered up */
	if (!pm_runtime_get_if_in_use(dw9719->dev))
		return 0;

	switch (ctrl->id) {
	case V4L2_CID_FOCUS_ABSOLUTE:
		ret = dw9719_t_focus_abs(dw9719, ctrl->val);
		break;
	default:
		ret = -EINVAL;
	}

	pm_runtime_put(dw9719->dev);

	return ret;
}

static const struct v4l2_ctrl_ops dw9719_ctrl_ops = {
	.s_ctrl = dw9719_set_ctrl,
};

static int dw9719_suspend(struct device *dev)
{
	struct v4l2_subdev *sd = dev_get_drvdata(dev);
	struct dw9719_device *dw9719 = to_dw9719_device(sd);
	int ret;
	int val;

	for (val = dw9719->ctrls.focus->val; val >= 0;
	     val -= DW9719_CTRL_STEPS) {
		ret = dw9719_t_focus_abs(dw9719, val);
		if (ret)
			return ret;

		usleep_range(DW9719_CTRL_DELAY_US, DW9719_CTRL_DELAY_US + 10);
	}

	return dw9719_power_down(dw9719);
}

static int dw9719_resume(struct device *dev)
{
	struct v4l2_subdev *sd = dev_get_drvdata(dev);
	struct dw9719_device *dw9719 = to_dw9719_device(sd);
	int current_focus = dw9719->ctrls.focus->val;
	int ret;
	int val;

	ret = dw9719_power_up(dw9719, false);
	if (ret)
		return ret;

	for (val = current_focus % DW9719_CTRL_STEPS; val < current_focus;
	     val += DW9719_CTRL_STEPS) {
		ret = dw9719_t_focus_abs(dw9719, val);
		if (ret)
			goto err_power_down;

		usleep_range(DW9719_CTRL_DELAY_US, DW9719_CTRL_DELAY_US + 10);
	}

	return 0;

err_power_down:
	dw9719_power_down(dw9719);
	return ret;
}

static int dw9719_open(struct v4l2_subdev *sd, struct v4l2_subdev_fh *fh)
{
	return pm_runtime_resume_and_get(sd->dev);
}

static int dw9719_close(struct v4l2_subdev *sd, struct v4l2_subdev_fh *fh)
{
	pm_runtime_put(sd->dev);

	return 0;
}

static const struct v4l2_subdev_internal_ops dw9719_internal_ops = {
	.open = dw9719_open,
	.close = dw9719_close,
};

static int dw9719_init_controls(struct dw9719_device *dw9719)
{
	const struct v4l2_ctrl_ops *ops = &dw9719_ctrl_ops;
	int ret;

	v4l2_ctrl_handler_init(&dw9719->ctrls.handler, 1);

	dw9719->ctrls.focus = v4l2_ctrl_new_std(&dw9719->ctrls.handler, ops,
						V4L2_CID_FOCUS_ABSOLUTE, 0,
						DW9719_MAX_FOCUS_POS, 1, 0);

	if (dw9719->ctrls.handler.error) {
		dev_err(dw9719->dev, "Error initialising v4l2 ctrls\n");
		ret = dw9719->ctrls.handler.error;
		goto err_free_handler;
	}

	dw9719->sd.ctrl_handler = &dw9719->ctrls.handler;
	return 0;

err_free_handler:
	v4l2_ctrl_handler_free(&dw9719->ctrls.handler);
	return ret;
}

static const struct v4l2_subdev_ops dw9719_ops = { };

static int dw9719_probe(struct i2c_client *client)
{
	struct dw9719_device *dw9719;
	int ret;

	dw9719 = devm_kzalloc(&client->dev, sizeof(*dw9719), GFP_KERNEL);
	if (!dw9719)
		return -ENOMEM;

	dw9719->regmap = devm_cci_regmap_init_i2c(client, 8);
	if (IS_ERR(dw9719->regmap))
		return PTR_ERR(dw9719->regmap);

	dw9719->dev = &client->dev;

	dw9719->regulator = devm_regulator_get(&client->dev, "vdd");
	if (IS_ERR(dw9719->regulator))
		return dev_err_probe(&client->dev, PTR_ERR(dw9719->regulator),
				     "getting regulator\n");

	v4l2_i2c_subdev_init(&dw9719->sd, client, &dw9719_ops);
	dw9719->sd.flags |= V4L2_SUBDEV_FL_HAS_DEVNODE;
	dw9719->sd.internal_ops = &dw9719_internal_ops;

	ret = dw9719_init_controls(dw9719);
	if (ret)
		return ret;

	ret = media_entity_pads_init(&dw9719->sd.entity, 0, NULL);
	if (ret < 0)
		goto err_free_ctrl_handler;

	dw9719->sd.entity.function = MEDIA_ENT_F_LENS;

	/*
	 * We need the driver to work in the event that pm runtime is disable in
	 * the kernel, so power up and verify the chip now. In the event that
	 * runtime pm is disabled this will leave the chip on, so that the lens
	 * will work.
	 */

	ret = dw9719_power_up(dw9719, true);
	if (ret)
		goto err_cleanup_media;

	pm_runtime_set_active(&client->dev);
	pm_runtime_get_noresume(&client->dev);
	pm_runtime_enable(&client->dev);

	ret = v4l2_async_register_subdev(&dw9719->sd);
	if (ret < 0)
		goto err_pm_runtime;

	pm_runtime_set_autosuspend_delay(&client->dev, 1000);
	pm_runtime_use_autosuspend(&client->dev);
	pm_runtime_put_autosuspend(&client->dev);

	return ret;

err_pm_runtime:
	pm_runtime_disable(&client->dev);
	pm_runtime_put_noidle(&client->dev);
	dw9719_power_down(dw9719);
err_cleanup_media:
	media_entity_cleanup(&dw9719->sd.entity);
err_free_ctrl_handler:
	v4l2_ctrl_handler_free(&dw9719->ctrls.handler);

	return ret;
}

static void dw9719_remove(struct i2c_client *client)
{
	struct v4l2_subdev *sd = i2c_get_clientdata(client);
	struct dw9719_device *dw9719 =
		container_of(sd, struct dw9719_device, sd);

	v4l2_async_unregister_subdev(sd);
	v4l2_ctrl_handler_free(&dw9719->ctrls.handler);
	media_entity_cleanup(&dw9719->sd.entity);

	pm_runtime_disable(&client->dev);
	if (!pm_runtime_status_suspended(&client->dev))
		dw9719_power_down(dw9719);
	pm_runtime_set_suspended(&client->dev);
}

static const struct i2c_device_id dw9719_id_table[] = {
	{ "dw9719" },
	{ "dw9761" },
	{ }
};
MODULE_DEVICE_TABLE(i2c, dw9719_id_table);

static DEFINE_RUNTIME_DEV_PM_OPS(dw9719_pm_ops, dw9719_suspend, dw9719_resume,
				 NULL);

static struct i2c_driver dw9719_i2c_driver = {
	.driver = {
		.name = "dw9719",
		.pm = pm_sleep_ptr(&dw9719_pm_ops),
	},
	.probe = dw9719_probe,
	.remove = dw9719_remove,
	.id_table = dw9719_id_table,
};
module_i2c_driver(dw9719_i2c_driver);

MODULE_AUTHOR("Daniel Scally <djrscally@gmail.com>");
MODULE_DESCRIPTION("DW9719 VCM Driver");
MODULE_LICENSE("GPL");
