// SPDX-License-Identifier: GPL-2.0
// Copyright (c) 2020 MediaTek Inc.

#include <linux/delay.h>
#include <linux/i2c.h>
#include <linux/module.h>
#include <linux/pm_runtime.h>
#include <linux/regulator/consumer.h>
#include <media/v4l2-async.h>
#include <media/v4l2-ctrls.h>
#include <media/v4l2-device.h>
#include <media/v4l2-fwnode.h>
#include <media/v4l2-subdev.h>

#define DW9768_NAME				"dw9768"
#define DW9768_MAX_FOCUS_POS			(1024 - 1)
/*
 * This sets the minimum granularity for the focus positions.
 * A value of 1 gives maximum accuracy for a desired focus position
 */
#define DW9768_FOCUS_STEPS			1

/*
 * Ring control and Power control register
 * Bit[1] RING_EN
 * 0: Direct mode
 * 1: AAC mode (ringing control mode)
 * Bit[0] PD
 * 0: Normal operation mode
 * 1: Power down mode
 * DW9768 requires waiting time of Topr after PD reset takes place.
 */
#define DW9768_RING_PD_CONTROL_REG		0x02
#define DW9768_PD_MODE_OFF			0x00
#define DW9768_PD_MODE_EN			BIT(0)
#define DW9768_AAC_MODE_EN			BIT(1)

/*
 * DW9768 separates two registers to control the VCM position.
 * One for MSB value, another is LSB value.
 * DAC_MSB: D[9:8] (ADD: 0x03)
 * DAC_LSB: D[7:0] (ADD: 0x04)
 * D[9:0] DAC data input: positive output current = D[9:0] / 1023 * 100[mA]
 */
#define DW9768_MSB_ADDR				0x03
#define DW9768_LSB_ADDR				0x04
#define DW9768_STATUS_ADDR			0x05

/*
 * AAC mode control & prescale register
 * Bit[7:5] Namely AC[2:0], decide the VCM mode and operation time.
 * 001 AAC2 0.48 x Tvib
 * 010 AAC3 0.70 x Tvib
 * 011 AAC4 0.75 x Tvib
 * 101 AAC8 1.13 x Tvib
 * Bit[2:0] Namely PRESC[2:0], set the internal clock dividing rate as follow.
 * 000 2
 * 001 1
 * 010 1/2
 * 011 1/4
 * 100 8
 * 101 4
 */
#define DW9768_AAC_PRESC_REG			0x06
#define DW9768_AAC_MODE_SEL_MASK		GENMASK(7, 5)
#define DW9768_CLOCK_PRE_SCALE_SEL_MASK		GENMASK(2, 0)

/*
 * VCM period of vibration register
 * Bit[5:0] Defined as VCM rising periodic time (Tvib) together with PRESC[2:0]
 * Tvib = (6.3ms + AACT[5:0] * 0.1ms) * Dividing Rate
 * Dividing Rate is the internal clock dividing rate that is defined at
 * PRESCALE register (ADD: 0x06)
 */
#define DW9768_AAC_TIME_REG			0x07

/*
 * DW9768 requires waiting time (delay time) of t_OPR after power-up,
 * or in the case of PD reset taking place.
 */
#define DW9768_T_OPR_US				1000
#define DW9768_TVIB_MS_BASE10			(64 - 1)
#define DW9768_AAC_MODE_DEFAULT			2
#define DW9768_AAC_TIME_DEFAULT			0x20
#define DW9768_CLOCK_PRE_SCALE_DEFAULT		1

/*
 * This acts as the minimum granularity of lens movement.
 * Keep this value power of 2, so the control steps can be
 * uniformly adjusted for gradual lens movement, with desired
 * number of control steps.
 */
#define DW9768_MOVE_STEPS			16

static const char * const dw9768_supply_names[] = {
	"vin",	/* Digital I/O power */
	"vdd",	/* Digital core power */
};

/* dw9768 device structure */
struct dw9768 {
	struct regulator_bulk_data supplies[ARRAY_SIZE(dw9768_supply_names)];
	struct v4l2_ctrl_handler ctrls;
	struct v4l2_ctrl *focus;
	struct v4l2_subdev sd;

	u32 aac_mode;
	u32 aac_timing;
	u32 clock_presc;
	u32 move_delay_us;
};

static inline struct dw9768 *sd_to_dw9768(struct v4l2_subdev *subdev)
{
	return container_of(subdev, struct dw9768, sd);
}

struct dw9768_aac_mode_ot_multi {
	u32 aac_mode_enum;
	u32 ot_multi_base100;
};

struct dw9768_clk_presc_dividing_rate {
	u32 clk_presc_enum;
	u32 dividing_rate_base100;
};

static const struct dw9768_aac_mode_ot_multi aac_mode_ot_multi[] = {
	{1,  48},
	{2,  70},
	{3,  75},
	{5, 113},
};

static const struct dw9768_clk_presc_dividing_rate presc_dividing_rate[] = {
	{0, 200},
	{1, 100},
	{2,  50},
	{3,  25},
	{4, 800},
	{5, 400},
};

static u32 dw9768_find_ot_multi(u32 aac_mode_param)
{
	u32 cur_ot_multi_base100 = 70;
	unsigned int i;

	for (i = 0; i < ARRAY_SIZE(aac_mode_ot_multi); i++) {
		if (aac_mode_ot_multi[i].aac_mode_enum == aac_mode_param) {
			cur_ot_multi_base100 =
				aac_mode_ot_multi[i].ot_multi_base100;
		}
	}

	return cur_ot_multi_base100;
}

static u32 dw9768_find_dividing_rate(u32 presc_param)
{
	u32 cur_clk_dividing_rate_base100 = 100;
	unsigned int i;

	for (i = 0; i < ARRAY_SIZE(presc_dividing_rate); i++) {
		if (presc_dividing_rate[i].clk_presc_enum == presc_param) {
			cur_clk_dividing_rate_base100 =
				presc_dividing_rate[i].dividing_rate_base100;
		}
	}

	return cur_clk_dividing_rate_base100;
}

/*
 * DW9768_AAC_PRESC_REG & DW9768_AAC_TIME_REG determine VCM operation time.
 * For current VCM mode: AAC3, Operation Time would be 0.70 x Tvib.
 * Tvib = (6.3ms + AACT[5:0] * 0.1MS) * Dividing Rate.
 * Below is calculation of the operation delay for each step.
 */
static inline u32 dw9768_cal_move_delay(u32 aac_mode_param, u32 presc_param,
					u32 aac_timing_param)
{
	u32 Tvib_us;
	u32 ot_multi_base100;
	u32 clk_dividing_rate_base100;

	ot_multi_base100 = dw9768_find_ot_multi(aac_mode_param);

	clk_dividing_rate_base100 = dw9768_find_dividing_rate(presc_param);

	Tvib_us = (DW9768_TVIB_MS_BASE10 + aac_timing_param) *
		  clk_dividing_rate_base100;

	return Tvib_us * ot_multi_base100 / 100;
}

static int dw9768_mod_reg(struct dw9768 *dw9768, u8 reg, u8 mask, u8 val)
{
	struct i2c_client *client = v4l2_get_subdevdata(&dw9768->sd);
	int ret;

	ret = i2c_smbus_read_byte_data(client, reg);
	if (ret < 0)
		return ret;

	val = ((unsigned char)ret & ~mask) | (val & mask);

	return i2c_smbus_write_byte_data(client, reg, val);
}

static int dw9768_set_dac(struct dw9768 *dw9768, u16 val)
{
	struct i2c_client *client = v4l2_get_subdevdata(&dw9768->sd);

	/* Write VCM position to registers */
	return i2c_smbus_write_word_swapped(client, DW9768_MSB_ADDR, val);
}

static int dw9768_init(struct dw9768 *dw9768)
{
	struct i2c_client *client = v4l2_get_subdevdata(&dw9768->sd);
	int ret, val;

	/* Reset DW9768_RING_PD_CONTROL_REG to default status 0x00 */
	ret = i2c_smbus_write_byte_data(client, DW9768_RING_PD_CONTROL_REG,
					DW9768_PD_MODE_OFF);
	if (ret < 0)
		return ret;

	/*
	 * DW9769 requires waiting delay time of t_OPR
	 * after PD reset takes place.
	 */
	usleep_range(DW9768_T_OPR_US, DW9768_T_OPR_US + 100);

	/* Set DW9768_RING_PD_CONTROL_REG to DW9768_AAC_MODE_EN(0x01) */
	ret = i2c_smbus_write_byte_data(client, DW9768_RING_PD_CONTROL_REG,
					DW9768_AAC_MODE_EN);
	if (ret < 0)
		return ret;

	/* Set AAC mode */
	ret = dw9768_mod_reg(dw9768, DW9768_AAC_PRESC_REG,
			     DW9768_AAC_MODE_SEL_MASK,
			     dw9768->aac_mode << 5);
	if (ret < 0)
		return ret;

	/* Set clock presc */
	if (dw9768->clock_presc != DW9768_CLOCK_PRE_SCALE_DEFAULT) {
		ret = dw9768_mod_reg(dw9768, DW9768_AAC_PRESC_REG,
				     DW9768_CLOCK_PRE_SCALE_SEL_MASK,
				     dw9768->clock_presc);
		if (ret < 0)
			return ret;
	}

	/* Set AAC Timing */
	if (dw9768->aac_timing != DW9768_AAC_TIME_DEFAULT) {
		ret = i2c_smbus_write_byte_data(client, DW9768_AAC_TIME_REG,
						dw9768->aac_timing);
		if (ret < 0)
			return ret;
	}

	for (val = dw9768->focus->val % DW9768_MOVE_STEPS;
	     val <= dw9768->focus->val;
	     val += DW9768_MOVE_STEPS) {
		ret = dw9768_set_dac(dw9768, val);
		if (ret) {
			dev_err(&client->dev, "I2C failure: %d", ret);
			return ret;
		}
		usleep_range(dw9768->move_delay_us,
			     dw9768->move_delay_us + 1000);
	}

	return 0;
}

static int dw9768_release(struct dw9768 *dw9768)
{
	struct i2c_client *client = v4l2_get_subdevdata(&dw9768->sd);
	int ret, val;

	val = round_down(dw9768->focus->val, DW9768_MOVE_STEPS);
	for ( ; val >= 0; val -= DW9768_MOVE_STEPS) {
		ret = dw9768_set_dac(dw9768, val);
		if (ret) {
			dev_err(&client->dev, "I2C write fail: %d", ret);
			return ret;
		}
		usleep_range(dw9768->move_delay_us,
			     dw9768->move_delay_us + 1000);
	}

	ret = i2c_smbus_write_byte_data(client, DW9768_RING_PD_CONTROL_REG,
					DW9768_PD_MODE_EN);
	if (ret < 0)
		return ret;

	/*
	 * DW9769 requires waiting delay time of t_OPR
	 * after PD reset takes place.
	 */
	usleep_range(DW9768_T_OPR_US, DW9768_T_OPR_US + 100);

	return 0;
}

static int dw9768_runtime_suspend(struct device *dev)
{
	struct v4l2_subdev *sd = dev_get_drvdata(dev);
	struct dw9768 *dw9768 = sd_to_dw9768(sd);

	dw9768_release(dw9768);
	regulator_bulk_disable(ARRAY_SIZE(dw9768_supply_names),
			       dw9768->supplies);

	return 0;
}

static int dw9768_runtime_resume(struct device *dev)
{
	struct v4l2_subdev *sd = dev_get_drvdata(dev);
	struct dw9768 *dw9768 = sd_to_dw9768(sd);
	int ret;

	ret = regulator_bulk_enable(ARRAY_SIZE(dw9768_supply_names),
				    dw9768->supplies);
	if (ret < 0) {
		dev_err(dev, "failed to enable regulators\n");
		return ret;
	}

	/*
	 * The datasheet refers to t_OPR that needs to be waited before sending
	 * I2C commands after power-up.
	 */
	usleep_range(DW9768_T_OPR_US, DW9768_T_OPR_US + 100);

	ret = dw9768_init(dw9768);
	if (ret < 0)
		goto disable_regulator;

	return 0;

disable_regulator:
	regulator_bulk_disable(ARRAY_SIZE(dw9768_supply_names),
			       dw9768->supplies);

	return ret;
}

static int dw9768_set_ctrl(struct v4l2_ctrl *ctrl)
{
	struct dw9768 *dw9768 = container_of(ctrl->handler,
					     struct dw9768, ctrls);

	if (ctrl->id == V4L2_CID_FOCUS_ABSOLUTE)
		return dw9768_set_dac(dw9768, ctrl->val);

	return 0;
}

static const struct v4l2_ctrl_ops dw9768_ctrl_ops = {
	.s_ctrl = dw9768_set_ctrl,
};

static int dw9768_open(struct v4l2_subdev *sd, struct v4l2_subdev_fh *fh)
{
	return pm_runtime_resume_and_get(sd->dev);
}

static int dw9768_close(struct v4l2_subdev *sd, struct v4l2_subdev_fh *fh)
{
	pm_runtime_put(sd->dev);

	return 0;
}

static const struct v4l2_subdev_internal_ops dw9768_int_ops = {
	.open = dw9768_open,
	.close = dw9768_close,
};

static const struct v4l2_subdev_ops dw9768_ops = { };

static int dw9768_init_controls(struct dw9768 *dw9768)
{
	struct v4l2_ctrl_handler *hdl = &dw9768->ctrls;
	const struct v4l2_ctrl_ops *ops = &dw9768_ctrl_ops;

	v4l2_ctrl_handler_init(hdl, 1);

	dw9768->focus = v4l2_ctrl_new_std(hdl, ops, V4L2_CID_FOCUS_ABSOLUTE, 0,
					  DW9768_MAX_FOCUS_POS,
					  DW9768_FOCUS_STEPS, 0);

	if (hdl->error)
		return hdl->error;

	dw9768->sd.ctrl_handler = hdl;

	return 0;
}

static int dw9768_probe(struct i2c_client *client)
{
	struct device *dev = &client->dev;
	struct dw9768 *dw9768;
	bool full_power;
	unsigned int i;
	int ret;

	dw9768 = devm_kzalloc(dev, sizeof(*dw9768), GFP_KERNEL);
	if (!dw9768)
		return -ENOMEM;

	/* Initialize subdev */
	v4l2_i2c_subdev_init(&dw9768->sd, client, &dw9768_ops);

	dw9768->aac_mode = DW9768_AAC_MODE_DEFAULT;
	dw9768->aac_timing = DW9768_AAC_TIME_DEFAULT;
	dw9768->clock_presc = DW9768_CLOCK_PRE_SCALE_DEFAULT;

	/* Optional indication of AAC mode select */
	fwnode_property_read_u32(dev_fwnode(dev), "dongwoon,aac-mode",
				 &dw9768->aac_mode);

	/* Optional indication of clock pre-scale select */
	fwnode_property_read_u32(dev_fwnode(dev), "dongwoon,clock-presc",
				 &dw9768->clock_presc);

	/* Optional indication of AAC Timing */
	fwnode_property_read_u32(dev_fwnode(dev), "dongwoon,aac-timing",
				 &dw9768->aac_timing);

	dw9768->move_delay_us = dw9768_cal_move_delay(dw9768->aac_mode,
						      dw9768->clock_presc,
						      dw9768->aac_timing);

	for (i = 0; i < ARRAY_SIZE(dw9768_supply_names); i++)
		dw9768->supplies[i].supply = dw9768_supply_names[i];

	ret = devm_regulator_bulk_get(dev, ARRAY_SIZE(dw9768_supply_names),
				      dw9768->supplies);
	if (ret) {
		dev_err(dev, "failed to get regulators\n");
		return ret;
	}

	/* Initialize controls */
	ret = dw9768_init_controls(dw9768);
	if (ret)
		goto err_free_handler;

	/* Initialize subdev */
	dw9768->sd.flags |= V4L2_SUBDEV_FL_HAS_DEVNODE;
	dw9768->sd.internal_ops = &dw9768_int_ops;

	ret = media_entity_pads_init(&dw9768->sd.entity, 0, NULL);
	if (ret < 0)
		goto err_free_handler;

	dw9768->sd.entity.function = MEDIA_ENT_F_LENS;

	/*
	 * Figure out whether we're going to power up the device here. Generally
	 * this is done if CONFIG_PM is disabled in a DT system or the device is
	 * to be powered on in an ACPI system. Similarly for power off in
	 * remove.
	 */
	pm_runtime_enable(dev);
	full_power = (is_acpi_node(dev_fwnode(dev)) &&
		      acpi_dev_state_d0(dev)) ||
		     (is_of_node(dev_fwnode(dev)) && !pm_runtime_enabled(dev));
	if (full_power) {
		ret = dw9768_runtime_resume(dev);
		if (ret < 0) {
			dev_err(dev, "failed to power on: %d\n", ret);
			goto err_clean_entity;
		}
		pm_runtime_set_active(dev);
	}

	ret = v4l2_async_register_subdev(&dw9768->sd);
	if (ret < 0) {
		dev_err(dev, "failed to register V4L2 subdev: %d", ret);
		goto err_power_off;
	}

	pm_runtime_idle(dev);

	return 0;

err_power_off:
	if (full_power) {
		dw9768_runtime_suspend(dev);
		pm_runtime_set_suspended(dev);
	}
err_clean_entity:
	pm_runtime_disable(dev);
	media_entity_cleanup(&dw9768->sd.entity);
err_free_handler:
	v4l2_ctrl_handler_free(&dw9768->ctrls);

	return ret;
}

static void dw9768_remove(struct i2c_client *client)
{
	struct v4l2_subdev *sd = i2c_get_clientdata(client);
	struct dw9768 *dw9768 = sd_to_dw9768(sd);
	struct device *dev = &client->dev;

	v4l2_async_unregister_subdev(&dw9768->sd);
	v4l2_ctrl_handler_free(&dw9768->ctrls);
	media_entity_cleanup(&dw9768->sd.entity);
	if ((is_acpi_node(dev_fwnode(dev)) && acpi_dev_state_d0(dev)) ||
	    (is_of_node(dev_fwnode(dev)) && !pm_runtime_enabled(dev))) {
		dw9768_runtime_suspend(dev);
		pm_runtime_set_suspended(dev);
	}
	pm_runtime_disable(dev);
}

static const struct of_device_id dw9768_of_table[] = {
	{ .compatible = "dongwoon,dw9768" },
	{ .compatible = "giantec,gt9769" },
	{}
};
MODULE_DEVICE_TABLE(of, dw9768_of_table);

static const struct dev_pm_ops dw9768_pm_ops = {
	SET_SYSTEM_SLEEP_PM_OPS(pm_runtime_force_suspend,
				pm_runtime_force_resume)
	SET_RUNTIME_PM_OPS(dw9768_runtime_suspend, dw9768_runtime_resume, NULL)
};

static struct i2c_driver dw9768_i2c_driver = {
	.driver = {
		.name = DW9768_NAME,
		.pm = &dw9768_pm_ops,
		.of_match_table = dw9768_of_table,
	},
	.probe = dw9768_probe,
	.remove = dw9768_remove,
};
module_i2c_driver(dw9768_i2c_driver);

MODULE_AUTHOR("Dongchun Zhu <dongchun.zhu@mediatek.com>");
MODULE_DESCRIPTION("DW9768 VCM driver");
MODULE_LICENSE("GPL v2");
