#include <linux/acpi.h>
#include <linux/delay.h>
#include <linux/i2c.h>
#include <linux/iopoll.h>
#include <linux/module.h>
#include <linux/pm_runtime.h>
#include <media/v4l2-ctrls.h>
#include <media/v4l2-device.h>

#define DW9800_MAX_FOCUS_POS	1023
/*
 * This sets the minimum granularity for the focus positions.
 * A value of 1 gives maximum accuracy for a desired focus position.
 */
#define DW9800_FOCUS_STEPS	1

/*
 * Ring control and Power control register
 * Bit[1] RING_EN
 * 0: Direct mode
 * 1: SAC mode (ringing control mode)
 * Bit[0] PD
 * 0: Normal operation mode
 * 1: Power down mode
 */
#define DW9800_RING_PD_CONTROL_REG		0x02
#define DW9800_PD_MODE_OFF	0x00
#define DW9800_PD_MODE_EN	BIT(0)
#define DW9800_SAC_MODE_EN	BIT(1)

/*
 * DW9800 sepeates two registers to control VCM position.
 * One for MSB value, another is LSB value.
 */
#define DW9800_MSB_REG		0x03
#define DW9800_LSB_REG		0x04

/*
 * Mode control & prescale register
 * Bit[7:5] Namely AC[2:0], decide the VCM mode and operation time.
 * 001 SAC2
 * 010 SAC3
 * 011 SAC4
 * 101 SAC5
 * Bit[2:0] Namely PRESC[2:0], set the internal clock dividing rate as follow.
 * 000 2
 * 001 1
 * 010 1/2
 * 011 1/4
 * 100 8
 * 101 4
 */
#define DW9800_SAC_PRESC_REG	0x06

/*
 * VCM period of vibration register
 * Bit[5:0] Defined as VCM rising periodic time (Tvib) together with PRESC[2:0]
 * Tvib = (6.3ms + SACT[5:0] * 0.1ms) * Dividing Rate
 * Dividing Rate is the internal clock dividing rate that is defined at
 * PRESCALE register (ADD: 0x06)
 */
#define DW9800_SAC_TIME_REG	0x07

#define DW9800_T_OPR_US	1000
#define DW9800_SAC_MODE_DEFAULT	1
#define DW9800_SAC_TIME_DEFAULT	0x25
#define DW9800_CLOCK_PRE_SCALE_DEFAULT	1

/*
 * This acts as the minimum granularity of lens movement.
 * Keep this value power of 2, so the control steps can be
 * uniformly adjusted for gradual lens movement, with desired
 * number of control steps.
 */
#define DW9800_MOVE_STEPS	16

static const char * const dw9800_supply_names[] = {
	"vin",	/* Digital I/O power */
	"vdd",	/* Digital core power */
};

struct dw9800 {
	struct regulator_bulk_data supplies[ARRAY_SIZE(dw9800_supply_names)];
	struct v4l2_ctrl_handler ctrls;
	struct v4l2_ctrl *focus;
	struct v4l2_subdev sd;

	u32 sac_mode;
	u32 sac_timing;
	u32 clock_presc;
	u32 move_delay_us;
};

struct dw9800_clk_presc_dividing_rate {
	u32 clk_presc_enum;
	u32 dividing_rate_base100;
};

static const struct dw9800_clk_presc_dividing_rate presc_dividing_rate[] = {
	{0, 200},
	{1, 100},
	{2,  50},
	{3,  25},
	{4, 800},
	{5, 400},
};

static u32 dw9800_find_dividing_rate(u32 presc_param)
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

static inline u32 dw9800_calc_move_delay(u32 mode, u32 presc, u32 timing)
{
	return (63 + timing) * dw9800_find_dividing_rate(presc);
}

static inline struct dw9800 *sd_to_dw9800(
					struct v4l2_subdev *subdev)
{
	return container_of(subdev, struct dw9800, sd);
}

static int dw9800_set_dac(struct i2c_client *client, u16 val)
{
	return i2c_smbus_write_word_swapped(client, DW9800_MSB_REG, val);
}

static int dw9800_init(struct dw9800 *dw9800_dev)
{
	struct i2c_client *client = v4l2_get_subdevdata(&dw9800_dev->sd);
	u32 mode;
	u32 time;
	int ret, val;

	// Set DW9800_RING_PD_CONTROL_ADDR to 0x0
	ret = i2c_smbus_write_byte_data(client, DW9800_RING_PD_CONTROL_REG,
					DW9800_PD_MODE_OFF);
	if (ret < 0)
		return ret;

	usleep_range(DW9800_T_OPR_US, DW9800_T_OPR_US + 100);

	// Set DW9800_RING_PD_CONTROL_ADDR to DW9800_SAC_MODE_EN
	ret = i2c_smbus_write_byte_data(client, DW9800_RING_PD_CONTROL_REG,
					DW9800_SAC_MODE_EN);
	if (ret < 0)
		return ret;

	// Set SAC mode and clock prescale
	mode = dw9800_dev->sac_mode << 6;
	mode |= (dw9800_dev->clock_presc >> 2) & 0x01;
	ret = i2c_smbus_write_byte_data(client, DW9800_SAC_PRESC_REG, mode);
	if (ret < 0)
		return ret;

	// Setup SAC timings
	time = dw9800_dev->clock_presc >> 6 | dw9800_dev->sac_timing;
	ret = i2c_smbus_write_byte_data(client, DW9800_SAC_TIME_REG,
				time);
	if (ret < 0)
		return ret;

	// Hacky but let it be for now
	val = round_down(i2c_smbus_read_word_swapped(client, DW9800_MSB_REG), DW9800_MOVE_STEPS);
	for (; val > 0; val -= DW9800_MOVE_STEPS) {
		ret = dw9800_set_dac(client, val);
		if (ret) {
			dev_err(&client->dev, "I2C write fail: %d", ret);
			return ret;
		}
		usleep_range(dw9800_dev->move_delay_us,
			     dw9800_dev->move_delay_us + 1000);
	}

	for (val = dw9800_dev->focus->val % DW9800_MOVE_STEPS;
	     val <= dw9800_dev->focus->val;
	     val += DW9800_MOVE_STEPS) {
		ret = dw9800_set_dac(client, val);
		if (ret) {
			dev_err(&client->dev, "I2C failure: %d", ret);
			return ret;
		}
		usleep_range(dw9800_dev->move_delay_us,
			     dw9800_dev->move_delay_us + 1000);
	}

	return 0;
}

static int dw9800_set_ctrl(struct v4l2_ctrl *ctrl)
{
	struct dw9800 *dw9800_dev = container_of(ctrl->handler,
					     struct dw9800, ctrls);

	if (ctrl->id == V4L2_CID_FOCUS_ABSOLUTE) {
		struct i2c_client *client = v4l2_get_subdevdata(&dw9800_dev->sd);

		dw9800_dev->focus->val = ctrl->val;
		return dw9800_set_dac(client, ctrl->val);
	}

	return -EINVAL;
}

static const struct v4l2_ctrl_ops dw9800_ctrl_ops = {
	.s_ctrl = dw9800_set_ctrl,
};

static int dw9800_open(struct v4l2_subdev *sd, struct v4l2_subdev_fh *fh)
{
	return pm_runtime_resume_and_get(sd->dev);
}

static int dw9800_close(struct v4l2_subdev *sd, struct v4l2_subdev_fh *fh)
{
	pm_runtime_put(sd->dev);

	return 0;
}

static const struct v4l2_subdev_internal_ops dw9800_int_ops = {
	.open = dw9800_open,
	.close = dw9800_close,
};

static const struct v4l2_subdev_ops dw9800_ops = { };

static void dw9800_subdev_cleanup(struct dw9800 *dw9800_dev)
{
	v4l2_async_unregister_subdev(&dw9800_dev->sd);
	v4l2_ctrl_handler_free(&dw9800_dev->ctrls);
	media_entity_cleanup(&dw9800_dev->sd.entity);
}

static int dw9800_init_controls(struct dw9800 *dw9800_dev)
{
	struct v4l2_ctrl_handler *hdl = &dw9800_dev->ctrls;
	const struct v4l2_ctrl_ops *ops = &dw9800_ctrl_ops;
	struct i2c_client *client = v4l2_get_subdevdata(&dw9800_dev->sd);

	v4l2_ctrl_handler_init(hdl, 1);

	dw9800_dev->focus = v4l2_ctrl_new_std(hdl, ops, V4L2_CID_FOCUS_ABSOLUTE,
			  0, DW9800_MAX_FOCUS_POS, DW9800_FOCUS_STEPS, 0);

	if (hdl->error) {
		dev_err(&client->dev, "%s fail error: 0x%x\n",
			__func__, hdl->error);
		return hdl->error;
	}

	dw9800_dev->sd.ctrl_handler = hdl;

	return 0;
}

static int dw9800_probe(struct i2c_client *client)
{
	struct dw9800 *dw9800_dev;
	int rval;

	dw9800_dev = devm_kzalloc(&client->dev, sizeof(*dw9800_dev),
				  GFP_KERNEL);
	if (dw9800_dev == NULL)
		return -ENOMEM;

	for (unsigned int i = 0; i < ARRAY_SIZE(dw9800_supply_names); i++)
		dw9800_dev->supplies[i].supply = dw9800_supply_names[i];
	rval = devm_regulator_bulk_get(&client->dev, ARRAY_SIZE(dw9800_supply_names),
				      dw9800_dev->supplies);
	if (rval) {
		dev_err(&client->dev, "failed to get regulators\n");
		return rval;
	}

	rval = regulator_bulk_enable(ARRAY_SIZE(dw9800_supply_names),
				dw9800_dev->supplies);
	if (rval < 0) {
		dev_err(&client->dev, "failed to enable regulators: %d\n", rval);
		return rval;
	}

	dw9800_dev->sac_mode = DW9800_SAC_MODE_DEFAULT;
	dw9800_dev->sac_timing = DW9800_SAC_TIME_DEFAULT;
	dw9800_dev->clock_presc = DW9800_CLOCK_PRE_SCALE_DEFAULT;

	/* Optional indication of SAC mode select */
	fwnode_property_read_u32(dev_fwnode(&client->dev), "dongwoon,sac-mode",
				 &dw9800_dev->sac_mode);

	/* Optional indication of clock pre-scale select */
	fwnode_property_read_u32(dev_fwnode(&client->dev), "dongwoon,clock-presc",
				 &dw9800_dev->clock_presc);

	/* Optional indication of SAC Timing */
	fwnode_property_read_u32(dev_fwnode(&client->dev), "dongwoon,sac-timing",
				 &dw9800_dev->sac_timing);

	dw9800_dev->move_delay_us = dw9800_calc_move_delay(dw9800_dev->sac_mode,
						       dw9800_dev->clock_presc,
						       dw9800_dev->sac_timing);

	v4l2_i2c_subdev_init(&dw9800_dev->sd, client, &dw9800_ops);
	dw9800_dev->sd.flags |= V4L2_SUBDEV_FL_HAS_DEVNODE;
	dw9800_dev->sd.internal_ops = &dw9800_int_ops;

	rval = dw9800_init_controls(dw9800_dev);
	if (rval)
		goto err_cleanup;

	rval = media_entity_pads_init(&dw9800_dev->sd.entity, 0, NULL);
	if (rval < 0)
		goto err_cleanup;

	dw9800_dev->sd.entity.function = MEDIA_ENT_F_LENS;

	rval = v4l2_async_register_subdev(&dw9800_dev->sd);
	if (rval < 0)
		goto err_cleanup;

	pm_runtime_set_active(&client->dev);
	pm_runtime_enable(&client->dev);
	pm_runtime_idle(&client->dev);

	return 0;

err_cleanup:
	regulator_bulk_disable(ARRAY_SIZE(dw9800_supply_names),
			       dw9800_dev->supplies);
	v4l2_ctrl_handler_free(&dw9800_dev->ctrls);
	media_entity_cleanup(&dw9800_dev->sd.entity);

	return rval;
}

static void dw9800_remove(struct i2c_client *client)
{
	struct v4l2_subdev *sd = i2c_get_clientdata(client);
	struct dw9800 *dw9800_dev = sd_to_dw9800(sd);
	int ret;

	pm_runtime_disable(&client->dev);
	if (!pm_runtime_status_suspended(&client->dev)) {
		ret = regulator_bulk_disable(ARRAY_SIZE(dw9800_supply_names),
			       dw9800_dev->supplies);
		if (ret) {
			dev_err(&client->dev,
				"Failed to disable regulators: %d\n", ret);
		}
	}
	pm_runtime_set_suspended(&client->dev);
	dw9800_subdev_cleanup(dw9800_dev);
}

/*
 * This function sets the vcm position, so it consumes least current
 * The lens position is gradually moved in units of DW9800_MOVE_STEPS,
 * to make the movements smoothly.
 */
static int dw9800_suspend(struct device *dev)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct v4l2_subdev *sd = i2c_get_clientdata(client);
	struct dw9800 *dw9800_dev = sd_to_dw9800(sd);
	int ret, val;

	if (pm_runtime_suspended(&client->dev))
		return 0;

	val = round_down(dw9800_dev->focus->val, DW9800_MOVE_STEPS);
	for (; val >= 0; val -= DW9800_MOVE_STEPS) {
		ret = dw9800_set_dac(client, val);
		if (ret) {
			dev_err(&client->dev, "I2C write fail: %d", ret);
			return ret;
		}
		usleep_range(dw9800_dev->move_delay_us,
			     dw9800_dev->move_delay_us + 1000);
	}

	ret = i2c_smbus_write_byte_data(client, DW9800_RING_PD_CONTROL_REG,
				DW9800_PD_MODE_EN);
	if (ret < 0)
		return ret;

	usleep_range(DW9800_T_OPR_US, DW9800_T_OPR_US + 100);

	ret = regulator_bulk_disable(ARRAY_SIZE(dw9800_supply_names),
			       dw9800_dev->supplies);
	if (ret)
		dev_err(dev, "Failed to disable regulators: %d\n", ret);

	return 0;
}

/*
 * This function sets the vcm position to the value set by the user
 * through v4l2_ctrl_ops s_ctrl handler
 * The lens position is gradually moved in units of DW9800_MOVE_STEPS,
 * to make the movements smoothly.
 */
static int dw9800_resume(struct device *dev)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct v4l2_subdev *sd = i2c_get_clientdata(client);
	struct dw9800 *dw9800_dev = sd_to_dw9800(sd);
	int ret;

	if (pm_runtime_suspended(&client->dev))
		return 0;

	ret = regulator_bulk_enable(ARRAY_SIZE(dw9800_supply_names),
				    dw9800_dev->supplies);
	if (ret) {
		dev_err(dev, "Failed to enable regulators: %d\n", ret);
		return ret;
	}
	usleep_range(DW9800_T_OPR_US, DW9800_T_OPR_US + 100);

	ret = dw9800_init(dw9800_dev);
	if (ret) {
		dev_err(dev, "Failed to init VCM: %d", ret);
		goto disable_regulator;
	}

	return 0;

disable_regulator:
	regulator_bulk_disable(ARRAY_SIZE(dw9800_supply_names),
			       dw9800_dev->supplies);

	return ret;
}

static const struct of_device_id dw9800_of_table[] = {
	{ .compatible = "dongwoon,dw9800" },
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, dw9800_of_table);

static const struct dev_pm_ops dw9800_pm_ops = {
	SET_SYSTEM_SLEEP_PM_OPS(dw9800_suspend, dw9800_resume)
	SET_RUNTIME_PM_OPS(dw9800_suspend, dw9800_resume, NULL)
};

static struct i2c_driver dw9800_i2c_driver = {
	.driver = {
		.name = "dw9800",
		.pm = &dw9800_pm_ops,
		.of_match_table = dw9800_of_table,
	},
	.probe = dw9800_probe,
	.remove = dw9800_remove,
};
module_i2c_driver(dw9800_i2c_driver);

MODULE_AUTHOR("Vitalii Skorkin <nikroksm@mail.ru>");
MODULE_DESCRIPTION("DW9800 VCM driver");
MODULE_LICENSE("GPL");
