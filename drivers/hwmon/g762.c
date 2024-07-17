// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * g762 - Driver for the Global Mixed-mode Technology Inc. fan speed
 *        PWM controller chips from G762 family, i.e. G762 and G763
 *
 * Copyright (C) 2013, Arnaud EBALARD <arno@natisbad.org>
 *
 * This work is based on a basic version for 2.6.31 kernel developed
 * by Olivier Mouchet for LaCie. Updates and correction have been
 * performed to run on recent kernels. Additional features, like the
 * ability to configure various characteristics via .dts file or
 * board init file have been added. Detailed datasheet on which this
 * development is based is available here:
 *
 *  http://natisbad.org/NAS/refs/GMT_EDS-762_763-080710-0.2.pdf
 *
 * Headers from previous developments have been kept below:
 *
 * Copyright (c) 2009 LaCie
 *
 * Author: Olivier Mouchet <olivier.mouchet@gmail.com>
 *
 * based on g760a code written by Herbert Valerio Riedel <hvr@gnu.org>
 * Copyright (C) 2007  Herbert Valerio Riedel <hvr@gnu.org>
 *
 * g762: minimal datasheet available at:
 *       http://www.gmt.com.tw/product/datasheet/EDS-762_3.pdf
 */

#include <linux/device.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/jiffies.h>
#include <linux/i2c.h>
#include <linux/hwmon.h>
#include <linux/hwmon-sysfs.h>
#include <linux/err.h>
#include <linux/mutex.h>
#include <linux/kernel.h>
#include <linux/clk.h>
#include <linux/of.h>
#include <linux/platform_data/g762.h>

#define DRVNAME "g762"

static const struct i2c_device_id g762_id[] = {
	{ "g761" },
	{ "g762" },
	{ "g763" },
	{ }
};
MODULE_DEVICE_TABLE(i2c, g762_id);

enum g762_regs {
	G762_REG_SET_CNT  = 0x00,
	G762_REG_ACT_CNT  = 0x01,
	G762_REG_FAN_STA  = 0x02,
	G762_REG_SET_OUT  = 0x03,
	G762_REG_FAN_CMD1 = 0x04,
	G762_REG_FAN_CMD2 = 0x05,
};

/* Config register bits */
#define G762_REG_FAN_CMD1_DET_FAN_FAIL  0x80 /* enable fan_fail signal */
#define G762_REG_FAN_CMD1_DET_FAN_OOC   0x40 /* enable fan_out_of_control */
#define G762_REG_FAN_CMD1_OUT_MODE      0x20 /* out mode: PWM or DC */
#define G762_REG_FAN_CMD1_FAN_MODE      0x10 /* fan mode: closed/open-loop */
#define G762_REG_FAN_CMD1_CLK_DIV_ID1   0x08 /* clock divisor value */
#define G762_REG_FAN_CMD1_CLK_DIV_ID0   0x04
#define G762_REG_FAN_CMD1_PWM_POLARITY  0x02 /* PWM polarity */
#define G762_REG_FAN_CMD1_PULSE_PER_REV 0x01 /* pulse per fan revolution */

#define G761_REG_FAN_CMD2_FAN_CLOCK     0x20 /* choose internal clock*/
#define G762_REG_FAN_CMD2_GEAR_MODE_1   0x08 /* fan gear mode */
#define G762_REG_FAN_CMD2_GEAR_MODE_0   0x04
#define G762_REG_FAN_CMD2_FAN_STARTV_1  0x02 /* fan startup voltage */
#define G762_REG_FAN_CMD2_FAN_STARTV_0  0x01

#define G762_REG_FAN_STA_FAIL           0x02 /* fan fail */
#define G762_REG_FAN_STA_OOC            0x01 /* fan out of control */

/* Config register values */
#define G762_OUT_MODE_PWM            1
#define G762_OUT_MODE_DC             0

#define G762_FAN_MODE_CLOSED_LOOP    2
#define G762_FAN_MODE_OPEN_LOOP      1

#define G762_PWM_POLARITY_NEGATIVE   1
#define G762_PWM_POLARITY_POSITIVE   0

/* Register data is read (and cached) at most once per second. */
#define G762_UPDATE_INTERVAL    HZ

/*
 * Extract pulse count per fan revolution value (2 or 4) from given
 * FAN_CMD1 register value.
 */
#define G762_PULSE_FROM_REG(reg) \
	((((reg) & G762_REG_FAN_CMD1_PULSE_PER_REV) + 1) << 1)

/*
 * Extract fan clock divisor (1, 2, 4 or 8) from given FAN_CMD1
 * register value.
 */
#define G762_CLKDIV_FROM_REG(reg) \
	(1 << (((reg) & (G762_REG_FAN_CMD1_CLK_DIV_ID0 |	\
			 G762_REG_FAN_CMD1_CLK_DIV_ID1)) >> 2))

/*
 * Extract fan gear mode multiplier value (0, 2 or 4) from given
 * FAN_CMD2 register value.
 */
#define G762_GEARMULT_FROM_REG(reg) \
	(1 << (((reg) & (G762_REG_FAN_CMD2_GEAR_MODE_0 |	\
			 G762_REG_FAN_CMD2_GEAR_MODE_1)) >> 2))

struct g762_data {
	struct i2c_client *client;
	bool internal_clock;
	struct clk *clk;

	/* update mutex */
	struct mutex update_lock;

	/* board specific parameters. */
	u32 clk_freq;

	/* g762 register cache */
	bool valid;
	unsigned long last_updated; /* in jiffies */

	u8 set_cnt;  /* controls fan rotation speed in closed-loop mode */
	u8 act_cnt;  /* provides access to current fan RPM value */
	u8 fan_sta;  /* bit 0: set when actual fan speed is more than
		      *        25% outside requested fan speed
		      * bit 1: set when no transition occurs on fan
		      *        pin for 0.7s
		      */
	u8 set_out;  /* controls fan rotation speed in open-loop mode */
	u8 fan_cmd1; /*   0: FG_PLS_ID0 FG pulses count per revolution
		      *      0: 2 counts per revolution
		      *      1: 4 counts per revolution
		      *   1: PWM_POLARITY 1: negative_duty
		      *                   0: positive_duty
		      * 2,3: [FG_CLOCK_ID0, FG_CLK_ID1]
		      *         00: Divide fan clock by 1
		      *         01: Divide fan clock by 2
		      *         10: Divide fan clock by 4
		      *         11: Divide fan clock by 8
		      *   4: FAN_MODE 1:closed-loop, 0:open-loop
		      *   5: OUT_MODE 1:PWM, 0:DC
		      *   6: DET_FAN_OOC enable "fan ooc" status
		      *   7: DET_FAN_FAIL enable "fan fail" status
		      */
	u8 fan_cmd2; /* 0,1: FAN_STARTV 0,1,2,3 -> 0,32,64,96 dac_code
		      * 2,3: FG_GEAR_MODE
		      *         00: multiplier = 1
		      *         01: multiplier = 2
		      *         10: multiplier = 4
		      *   4: Mask ALERT# (g763 only)
		      */
};

/*
 * Convert count value from fan controller register (FAN_SET_CNT) into fan
 * speed RPM value. Note that the datasheet documents a basic formula;
 * influence of additional parameters (fan clock divisor, fan gear mode)
 * have been infered from examples in the datasheet and tests.
 */
static inline unsigned int rpm_from_cnt(u8 cnt, u32 clk_freq, u16 p,
					u8 clk_div, u8 gear_mult)
{
	if (cnt == 0xff)  /* setting cnt to 255 stops the fan */
		return 0;

	return (clk_freq * 30 * gear_mult) / ((cnt ? cnt : 1) * p * clk_div);
}

/*
 * Convert fan RPM value from sysfs into count value for fan controller
 * register (FAN_SET_CNT).
 */
static inline unsigned char cnt_from_rpm(unsigned long rpm, u32 clk_freq, u16 p,
					 u8 clk_div, u8 gear_mult)
{
	unsigned long f1 = clk_freq * 30 * gear_mult;
	unsigned long f2 = p * clk_div;

	if (!rpm)	/* to stop the fan, set cnt to 255 */
		return 0xff;

	rpm = clamp_val(rpm, f1 / (255 * f2), ULONG_MAX / f2);
	return DIV_ROUND_CLOSEST(f1, rpm * f2);
}

/* helper to grab and cache data, at most one time per second */
static struct g762_data *g762_update_client(struct device *dev)
{
	struct g762_data *data = dev_get_drvdata(dev);
	struct i2c_client *client = data->client;
	int ret = 0;

	mutex_lock(&data->update_lock);
	if (time_before(jiffies, data->last_updated + G762_UPDATE_INTERVAL) &&
	    likely(data->valid))
		goto out;

	ret = i2c_smbus_read_byte_data(client, G762_REG_SET_CNT);
	if (ret < 0)
		goto out;
	data->set_cnt = ret;

	ret = i2c_smbus_read_byte_data(client, G762_REG_ACT_CNT);
	if (ret < 0)
		goto out;
	data->act_cnt = ret;

	ret = i2c_smbus_read_byte_data(client, G762_REG_FAN_STA);
	if (ret < 0)
		goto out;
	data->fan_sta = ret;

	ret = i2c_smbus_read_byte_data(client, G762_REG_SET_OUT);
	if (ret < 0)
		goto out;
	data->set_out = ret;

	ret = i2c_smbus_read_byte_data(client, G762_REG_FAN_CMD1);
	if (ret < 0)
		goto out;
	data->fan_cmd1 = ret;

	ret = i2c_smbus_read_byte_data(client, G762_REG_FAN_CMD2);
	if (ret < 0)
		goto out;
	data->fan_cmd2 = ret;

	data->last_updated = jiffies;
	data->valid = true;
 out:
	mutex_unlock(&data->update_lock);

	if (ret < 0) /* upon error, encode it in return value */
		data = ERR_PTR(ret);

	return data;
}

/* helpers for writing hardware parameters */

/*
 * Set input clock frequency received on CLK pin of the chip. Accepted values
 * are between 0 and 0xffffff. If zero is given, then default frequency
 * (32,768Hz) is used. Note that clock frequency is a characteristic of the
 * system but an internal parameter, i.e. value is not passed to the device.
 */
static int do_set_clk_freq(struct device *dev, unsigned long val)
{
	struct g762_data *data = dev_get_drvdata(dev);

	if (val > 0xffffff)
		return -EINVAL;
	if (!val)
		val = 32768;

	data->clk_freq = val;

	return 0;
}

/* Set pwm mode. Accepts either 0 (PWM mode) or 1 (DC mode) */
static int do_set_pwm_mode(struct device *dev, unsigned long val)
{
	struct g762_data *data = g762_update_client(dev);
	int ret;

	if (IS_ERR(data))
		return PTR_ERR(data);

	mutex_lock(&data->update_lock);
	switch (val) {
	case G762_OUT_MODE_PWM:
		data->fan_cmd1 |=  G762_REG_FAN_CMD1_OUT_MODE;
		break;
	case G762_OUT_MODE_DC:
		data->fan_cmd1 &= ~G762_REG_FAN_CMD1_OUT_MODE;
		break;
	default:
		ret = -EINVAL;
		goto out;
	}
	ret = i2c_smbus_write_byte_data(data->client, G762_REG_FAN_CMD1,
					data->fan_cmd1);
	data->valid = false;
 out:
	mutex_unlock(&data->update_lock);

	return ret;
}

/* Set fan clock divisor. Accepts either 1, 2, 4 or 8. */
static int do_set_fan_div(struct device *dev, unsigned long val)
{
	struct g762_data *data = g762_update_client(dev);
	int ret;

	if (IS_ERR(data))
		return PTR_ERR(data);

	mutex_lock(&data->update_lock);
	switch (val) {
	case 1:
		data->fan_cmd1 &= ~G762_REG_FAN_CMD1_CLK_DIV_ID0;
		data->fan_cmd1 &= ~G762_REG_FAN_CMD1_CLK_DIV_ID1;
		break;
	case 2:
		data->fan_cmd1 |=  G762_REG_FAN_CMD1_CLK_DIV_ID0;
		data->fan_cmd1 &= ~G762_REG_FAN_CMD1_CLK_DIV_ID1;
		break;
	case 4:
		data->fan_cmd1 &= ~G762_REG_FAN_CMD1_CLK_DIV_ID0;
		data->fan_cmd1 |=  G762_REG_FAN_CMD1_CLK_DIV_ID1;
		break;
	case 8:
		data->fan_cmd1 |=  G762_REG_FAN_CMD1_CLK_DIV_ID0;
		data->fan_cmd1 |=  G762_REG_FAN_CMD1_CLK_DIV_ID1;
		break;
	default:
		ret = -EINVAL;
		goto out;
	}
	ret = i2c_smbus_write_byte_data(data->client, G762_REG_FAN_CMD1,
					data->fan_cmd1);
	data->valid = false;
 out:
	mutex_unlock(&data->update_lock);

	return ret;
}

/* Set fan gear mode. Accepts either 0, 1 or 2. */
static int do_set_fan_gear_mode(struct device *dev, unsigned long val)
{
	struct g762_data *data = g762_update_client(dev);
	int ret;

	if (IS_ERR(data))
		return PTR_ERR(data);

	mutex_lock(&data->update_lock);
	switch (val) {
	case 0:
		data->fan_cmd2 &= ~G762_REG_FAN_CMD2_GEAR_MODE_0;
		data->fan_cmd2 &= ~G762_REG_FAN_CMD2_GEAR_MODE_1;
		break;
	case 1:
		data->fan_cmd2 |=  G762_REG_FAN_CMD2_GEAR_MODE_0;
		data->fan_cmd2 &= ~G762_REG_FAN_CMD2_GEAR_MODE_1;
		break;
	case 2:
		data->fan_cmd2 &= ~G762_REG_FAN_CMD2_GEAR_MODE_0;
		data->fan_cmd2 |=  G762_REG_FAN_CMD2_GEAR_MODE_1;
		break;
	default:
		ret = -EINVAL;
		goto out;
	}
	ret = i2c_smbus_write_byte_data(data->client, G762_REG_FAN_CMD2,
					data->fan_cmd2);
	data->valid = false;
 out:
	mutex_unlock(&data->update_lock);

	return ret;
}

/* Set number of fan pulses per revolution. Accepts either 2 or 4. */
static int do_set_fan_pulses(struct device *dev, unsigned long val)
{
	struct g762_data *data = g762_update_client(dev);
	int ret;

	if (IS_ERR(data))
		return PTR_ERR(data);

	mutex_lock(&data->update_lock);
	switch (val) {
	case 2:
		data->fan_cmd1 &= ~G762_REG_FAN_CMD1_PULSE_PER_REV;
		break;
	case 4:
		data->fan_cmd1 |=  G762_REG_FAN_CMD1_PULSE_PER_REV;
		break;
	default:
		ret = -EINVAL;
		goto out;
	}
	ret = i2c_smbus_write_byte_data(data->client, G762_REG_FAN_CMD1,
					data->fan_cmd1);
	data->valid = false;
 out:
	mutex_unlock(&data->update_lock);

	return ret;
}

/* Set fan mode. Accepts either 1 (open-loop) or 2 (closed-loop). */
static int do_set_pwm_enable(struct device *dev, unsigned long val)
{
	struct g762_data *data = g762_update_client(dev);
	int ret;

	if (IS_ERR(data))
		return PTR_ERR(data);

	mutex_lock(&data->update_lock);
	switch (val) {
	case G762_FAN_MODE_CLOSED_LOOP:
		data->fan_cmd1 |=  G762_REG_FAN_CMD1_FAN_MODE;
		break;
	case G762_FAN_MODE_OPEN_LOOP:
		data->fan_cmd1 &= ~G762_REG_FAN_CMD1_FAN_MODE;
		/*
		 * BUG FIX: if SET_CNT register value is 255 then, for some
		 * unknown reason, fan will not rotate as expected, no matter
		 * the value of SET_OUT (to be specific, this seems to happen
		 * only in PWM mode). To workaround this bug, we give SET_CNT
		 * value of 254 if it is 255 when switching to open-loop.
		 */
		if (data->set_cnt == 0xff)
			i2c_smbus_write_byte_data(data->client,
						  G762_REG_SET_CNT, 254);
		break;
	default:
		ret = -EINVAL;
		goto out;
	}

	ret = i2c_smbus_write_byte_data(data->client, G762_REG_FAN_CMD1,
					data->fan_cmd1);
	data->valid = false;
 out:
	mutex_unlock(&data->update_lock);

	return ret;
}

/* Set PWM polarity. Accepts either 0 (positive duty) or 1 (negative duty) */
static int do_set_pwm_polarity(struct device *dev, unsigned long val)
{
	struct g762_data *data = g762_update_client(dev);
	int ret;

	if (IS_ERR(data))
		return PTR_ERR(data);

	mutex_lock(&data->update_lock);
	switch (val) {
	case G762_PWM_POLARITY_POSITIVE:
		data->fan_cmd1 &= ~G762_REG_FAN_CMD1_PWM_POLARITY;
		break;
	case G762_PWM_POLARITY_NEGATIVE:
		data->fan_cmd1 |=  G762_REG_FAN_CMD1_PWM_POLARITY;
		break;
	default:
		ret = -EINVAL;
		goto out;
	}
	ret = i2c_smbus_write_byte_data(data->client, G762_REG_FAN_CMD1,
					data->fan_cmd1);
	data->valid = false;
 out:
	mutex_unlock(&data->update_lock);

	return ret;
}

/*
 * Set pwm value. Accepts values between 0 (stops the fan) and
 * 255 (full speed). This only makes sense in open-loop mode.
 */
static int do_set_pwm(struct device *dev, unsigned long val)
{
	struct g762_data *data = dev_get_drvdata(dev);
	struct i2c_client *client = data->client;
	int ret;

	if (val > 255)
		return -EINVAL;

	mutex_lock(&data->update_lock);
	ret = i2c_smbus_write_byte_data(client, G762_REG_SET_OUT, val);
	data->valid = false;
	mutex_unlock(&data->update_lock);

	return ret;
}

/*
 * Set fan RPM value. Can be called both in closed and open-loop mode
 * but effect will only be seen after closed-loop mode is configured.
 */
static int do_set_fan_target(struct device *dev, unsigned long val)
{
	struct g762_data *data = g762_update_client(dev);
	int ret;

	if (IS_ERR(data))
		return PTR_ERR(data);

	mutex_lock(&data->update_lock);
	data->set_cnt = cnt_from_rpm(val, data->clk_freq,
				     G762_PULSE_FROM_REG(data->fan_cmd1),
				     G762_CLKDIV_FROM_REG(data->fan_cmd1),
				     G762_GEARMULT_FROM_REG(data->fan_cmd2));
	ret = i2c_smbus_write_byte_data(data->client, G762_REG_SET_CNT,
					data->set_cnt);
	data->valid = false;
	mutex_unlock(&data->update_lock);

	return ret;
}

/* Set fan startup voltage. Accepted values are either 0, 1, 2 or 3. */
static int do_set_fan_startv(struct device *dev, unsigned long val)
{
	struct g762_data *data = g762_update_client(dev);
	int ret;

	if (IS_ERR(data))
		return PTR_ERR(data);

	mutex_lock(&data->update_lock);
	switch (val) {
	case 0:
		data->fan_cmd2 &= ~G762_REG_FAN_CMD2_FAN_STARTV_0;
		data->fan_cmd2 &= ~G762_REG_FAN_CMD2_FAN_STARTV_1;
		break;
	case 1:
		data->fan_cmd2 |=  G762_REG_FAN_CMD2_FAN_STARTV_0;
		data->fan_cmd2 &= ~G762_REG_FAN_CMD2_FAN_STARTV_1;
		break;
	case 2:
		data->fan_cmd2 &= ~G762_REG_FAN_CMD2_FAN_STARTV_0;
		data->fan_cmd2 |=  G762_REG_FAN_CMD2_FAN_STARTV_1;
		break;
	case 3:
		data->fan_cmd2 |=  G762_REG_FAN_CMD2_FAN_STARTV_0;
		data->fan_cmd2 |=  G762_REG_FAN_CMD2_FAN_STARTV_1;
		break;
	default:
		ret = -EINVAL;
		goto out;
	}
	ret = i2c_smbus_write_byte_data(data->client, G762_REG_FAN_CMD2,
					data->fan_cmd2);
	data->valid = false;
 out:
	mutex_unlock(&data->update_lock);

	return ret;
}

/*
 * Helper to import hardware characteristics from .dts file and push
 * those to the chip.
 */

#ifdef CONFIG_OF
static const struct of_device_id g762_dt_match[] = {
	{ .compatible = "gmt,g761" },
	{ .compatible = "gmt,g762" },
	{ .compatible = "gmt,g763" },
	{ },
};
MODULE_DEVICE_TABLE(of, g762_dt_match);

/*
 * Grab clock (a required property), enable it, get (fixed) clock frequency
 * and store it. Note: upon success, clock has been prepared and enabled; it
 * must later be unprepared and disabled (e.g. during module unloading) by a
 * call to g762_of_clock_disable(). Note that a reference to clock is kept
 * in our private data structure to be used in this function.
 */
static void g762_of_clock_disable(void *data)
{
	struct g762_data *g762 = data;

	clk_disable_unprepare(g762->clk);
	clk_put(g762->clk);
}

static int g762_of_clock_enable(struct i2c_client *client)
{
	struct g762_data *data;
	unsigned long clk_freq;
	struct clk *clk;
	int ret;

	if (!client->dev.of_node)
		return 0;

	data = i2c_get_clientdata(client);

	/*
	 * Skip CLK detection and handling if we use internal clock.
	 * This is only valid for g761.
	 */
	data->internal_clock = of_device_is_compatible(client->dev.of_node,
						       "gmt,g761") &&
			       !of_property_present(client->dev.of_node,
						    "clocks");
	if (data->internal_clock) {
		do_set_clk_freq(&client->dev, 32768);
		return 0;
	}

	clk = of_clk_get(client->dev.of_node, 0);
	if (IS_ERR(clk)) {
		dev_err(&client->dev, "failed to get clock\n");
		return PTR_ERR(clk);
	}

	ret = clk_prepare_enable(clk);
	if (ret) {
		dev_err(&client->dev, "failed to enable clock\n");
		goto clk_put;
	}

	clk_freq = clk_get_rate(clk);
	ret = do_set_clk_freq(&client->dev, clk_freq);
	if (ret) {
		dev_err(&client->dev, "invalid clock freq %lu\n", clk_freq);
		goto clk_unprep;
	}

	data->clk = clk;

	ret = devm_add_action(&client->dev, g762_of_clock_disable, data);
	if (ret) {
		dev_err(&client->dev, "failed to add disable clock action\n");
		goto clk_unprep;
	}

	return 0;

 clk_unprep:
	clk_disable_unprepare(clk);

 clk_put:
	clk_put(clk);

	return ret;
}

static int g762_of_prop_import_one(struct i2c_client *client,
				   const char *pname,
				   int (*psetter)(struct device *dev,
						  unsigned long val))
{
	int ret;
	u32 pval;

	if (of_property_read_u32(client->dev.of_node, pname, &pval))
		return 0;

	dev_dbg(&client->dev, "found %s (%d)\n", pname, pval);
	ret = (*psetter)(&client->dev, pval);
	if (ret)
		dev_err(&client->dev, "unable to set %s (%d)\n", pname, pval);

	return ret;
}

static int g762_of_prop_import(struct i2c_client *client)
{
	int ret;

	if (!client->dev.of_node)
		return 0;

	ret = g762_of_prop_import_one(client, "fan_gear_mode",
				      do_set_fan_gear_mode);
	if (ret)
		return ret;

	ret = g762_of_prop_import_one(client, "pwm_polarity",
				      do_set_pwm_polarity);
	if (ret)
		return ret;

	return g762_of_prop_import_one(client, "fan_startv",
				       do_set_fan_startv);
}

#else
static int g762_of_prop_import(struct i2c_client *client)
{
	return 0;
}

static int g762_of_clock_enable(struct i2c_client *client)
{
	return 0;
}
#endif

/*
 * Helper to import hardware characteristics from .dts file and push
 * those to the chip.
 */

static int g762_pdata_prop_import(struct i2c_client *client)
{
	struct g762_platform_data *pdata = dev_get_platdata(&client->dev);
	int ret;

	if (!pdata)
		return 0;

	ret = do_set_fan_gear_mode(&client->dev, pdata->fan_gear_mode);
	if (ret)
		return ret;

	ret = do_set_pwm_polarity(&client->dev, pdata->pwm_polarity);
	if (ret)
		return ret;

	ret = do_set_fan_startv(&client->dev, pdata->fan_startv);
	if (ret)
		return ret;

	return do_set_clk_freq(&client->dev, pdata->clk_freq);
}

/*
 * sysfs attributes
 */

/*
 * Read function for fan1_input sysfs file. Return current fan RPM value, or
 * 0 if fan is out of control.
 */
static ssize_t fan1_input_show(struct device *dev,
			       struct device_attribute *da, char *buf)
{
	struct g762_data *data = g762_update_client(dev);
	unsigned int rpm = 0;

	if (IS_ERR(data))
		return PTR_ERR(data);

	mutex_lock(&data->update_lock);
	/* reverse logic: fan out of control reporting is enabled low */
	if (data->fan_sta & G762_REG_FAN_STA_OOC) {
		rpm = rpm_from_cnt(data->act_cnt, data->clk_freq,
				   G762_PULSE_FROM_REG(data->fan_cmd1),
				   G762_CLKDIV_FROM_REG(data->fan_cmd1),
				   G762_GEARMULT_FROM_REG(data->fan_cmd2));
	}
	mutex_unlock(&data->update_lock);

	return sprintf(buf, "%u\n", rpm);
}

/*
 * Read and write functions for pwm1_mode sysfs file. Get and set fan speed
 * control mode i.e. PWM (1) or DC (0).
 */
static ssize_t pwm1_mode_show(struct device *dev, struct device_attribute *da,
			      char *buf)
{
	struct g762_data *data = g762_update_client(dev);

	if (IS_ERR(data))
		return PTR_ERR(data);

	return sprintf(buf, "%d\n",
		       !!(data->fan_cmd1 & G762_REG_FAN_CMD1_OUT_MODE));
}

static ssize_t pwm1_mode_store(struct device *dev,
			       struct device_attribute *da, const char *buf,
			       size_t count)
{
	unsigned long val;
	int ret;

	if (kstrtoul(buf, 10, &val))
		return -EINVAL;

	ret = do_set_pwm_mode(dev, val);
	if (ret < 0)
		return ret;

	return count;
}

/*
 * Read and write functions for fan1_div sysfs file. Get and set fan
 * controller prescaler value
 */
static ssize_t fan1_div_show(struct device *dev, struct device_attribute *da,
			     char *buf)
{
	struct g762_data *data = g762_update_client(dev);

	if (IS_ERR(data))
		return PTR_ERR(data);

	return sprintf(buf, "%d\n", G762_CLKDIV_FROM_REG(data->fan_cmd1));
}

static ssize_t fan1_div_store(struct device *dev, struct device_attribute *da,
			      const char *buf, size_t count)
{
	unsigned long val;
	int ret;

	if (kstrtoul(buf, 10, &val))
		return -EINVAL;

	ret = do_set_fan_div(dev, val);
	if (ret < 0)
		return ret;

	return count;
}

/*
 * Read and write functions for fan1_pulses sysfs file. Get and set number
 * of tachometer pulses per fan revolution.
 */
static ssize_t fan1_pulses_show(struct device *dev,
				struct device_attribute *da, char *buf)
{
	struct g762_data *data = g762_update_client(dev);

	if (IS_ERR(data))
		return PTR_ERR(data);

	return sprintf(buf, "%d\n", G762_PULSE_FROM_REG(data->fan_cmd1));
}

static ssize_t fan1_pulses_store(struct device *dev,
				 struct device_attribute *da, const char *buf,
				 size_t count)
{
	unsigned long val;
	int ret;

	if (kstrtoul(buf, 10, &val))
		return -EINVAL;

	ret = do_set_fan_pulses(dev, val);
	if (ret < 0)
		return ret;

	return count;
}

/*
 * Read and write functions for pwm1_enable. Get and set fan speed control mode
 * (i.e. closed or open-loop).
 *
 * Following documentation about hwmon's sysfs interface, a pwm1_enable node
 * should accept the following:
 *
 *  0 : no fan speed control (i.e. fan at full speed)
 *  1 : manual fan speed control enabled (use pwm[1-*]) (open-loop)
 *  2+: automatic fan speed control enabled (use fan[1-*]_target) (closed-loop)
 *
 * but we do not accept 0 as this mode is not natively supported by the chip
 * and it is not emulated by g762 driver. -EINVAL is returned in this case.
 */
static ssize_t pwm1_enable_show(struct device *dev,
				struct device_attribute *da, char *buf)
{
	struct g762_data *data = g762_update_client(dev);

	if (IS_ERR(data))
		return PTR_ERR(data);

	return sprintf(buf, "%d\n",
		       (!!(data->fan_cmd1 & G762_REG_FAN_CMD1_FAN_MODE)) + 1);
}

static ssize_t pwm1_enable_store(struct device *dev,
				 struct device_attribute *da, const char *buf,
				 size_t count)
{
	unsigned long val;
	int ret;

	if (kstrtoul(buf, 10, &val))
		return -EINVAL;

	ret = do_set_pwm_enable(dev, val);
	if (ret < 0)
		return ret;

	return count;
}

/*
 * Read and write functions for pwm1 sysfs file. Get and set pwm value
 * (which affects fan speed) in open-loop mode. 0 stops the fan and 255
 * makes it run at full speed.
 */
static ssize_t pwm1_show(struct device *dev, struct device_attribute *da,
			 char *buf)
{
	struct g762_data *data = g762_update_client(dev);

	if (IS_ERR(data))
		return PTR_ERR(data);

	return sprintf(buf, "%d\n", data->set_out);
}

static ssize_t pwm1_store(struct device *dev, struct device_attribute *da,
			  const char *buf, size_t count)
{
	unsigned long val;
	int ret;

	if (kstrtoul(buf, 10, &val))
		return -EINVAL;

	ret = do_set_pwm(dev, val);
	if (ret < 0)
		return ret;

	return count;
}

/*
 * Read and write function for fan1_target sysfs file. Get/set the fan speed in
 * closed-loop mode. Speed is given as a RPM value; then the chip will regulate
 * the fan speed using pulses from fan tachometer.
 *
 * Refer to rpm_from_cnt() implementation above to get info about count number
 * calculation.
 *
 * Also note that due to rounding errors it is possible that you don't read
 * back exactly the value you have set.
 */
static ssize_t fan1_target_show(struct device *dev,
				struct device_attribute *da, char *buf)
{
	struct g762_data *data = g762_update_client(dev);
	unsigned int rpm;

	if (IS_ERR(data))
		return PTR_ERR(data);

	mutex_lock(&data->update_lock);
	rpm = rpm_from_cnt(data->set_cnt, data->clk_freq,
			   G762_PULSE_FROM_REG(data->fan_cmd1),
			   G762_CLKDIV_FROM_REG(data->fan_cmd1),
			   G762_GEARMULT_FROM_REG(data->fan_cmd2));
	mutex_unlock(&data->update_lock);

	return sprintf(buf, "%u\n", rpm);
}

static ssize_t fan1_target_store(struct device *dev,
				 struct device_attribute *da, const char *buf,
				 size_t count)
{
	unsigned long val;
	int ret;

	if (kstrtoul(buf, 10, &val))
		return -EINVAL;

	ret = do_set_fan_target(dev, val);
	if (ret < 0)
		return ret;

	return count;
}

/* read function for fan1_fault sysfs file. */
static ssize_t fan1_fault_show(struct device *dev, struct device_attribute *da,
			       char *buf)
{
	struct g762_data *data = g762_update_client(dev);

	if (IS_ERR(data))
		return PTR_ERR(data);

	return sprintf(buf, "%u\n", !!(data->fan_sta & G762_REG_FAN_STA_FAIL));
}

/*
 * read function for fan1_alarm sysfs file. Note that OOC condition is
 * enabled low
 */
static ssize_t fan1_alarm_show(struct device *dev,
			       struct device_attribute *da, char *buf)
{
	struct g762_data *data = g762_update_client(dev);

	if (IS_ERR(data))
		return PTR_ERR(data);

	return sprintf(buf, "%u\n", !(data->fan_sta & G762_REG_FAN_STA_OOC));
}

static DEVICE_ATTR_RW(pwm1);
static DEVICE_ATTR_RW(pwm1_mode);
static DEVICE_ATTR_RW(pwm1_enable);
static DEVICE_ATTR_RO(fan1_input);
static DEVICE_ATTR_RO(fan1_alarm);
static DEVICE_ATTR_RO(fan1_fault);
static DEVICE_ATTR_RW(fan1_target);
static DEVICE_ATTR_RW(fan1_div);
static DEVICE_ATTR_RW(fan1_pulses);

/* Driver data */
static struct attribute *g762_attrs[] = {
	&dev_attr_fan1_input.attr,
	&dev_attr_fan1_alarm.attr,
	&dev_attr_fan1_fault.attr,
	&dev_attr_fan1_target.attr,
	&dev_attr_fan1_div.attr,
	&dev_attr_fan1_pulses.attr,
	&dev_attr_pwm1.attr,
	&dev_attr_pwm1_mode.attr,
	&dev_attr_pwm1_enable.attr,
	NULL
};

ATTRIBUTE_GROUPS(g762);

/*
 * Enable both fan failure detection and fan out of control protection. The
 * function does not protect change/access to data structure; it must thus
 * only be called during initialization.
 */
static inline int g762_fan_init(struct device *dev)
{
	struct g762_data *data = g762_update_client(dev);
	int ret;

	if (IS_ERR(data))
		return PTR_ERR(data);

	/* internal_clock can only be set with compatible g761 */
	if (data->internal_clock)
		data->fan_cmd2 |= G761_REG_FAN_CMD2_FAN_CLOCK;

	data->fan_cmd1 |= G762_REG_FAN_CMD1_DET_FAN_FAIL;
	data->fan_cmd1 |= G762_REG_FAN_CMD1_DET_FAN_OOC;
	data->valid = false;

	ret = i2c_smbus_write_byte_data(data->client, G762_REG_FAN_CMD1,
					data->fan_cmd1);
	if (ret)
		return ret;

	return i2c_smbus_write_byte_data(data->client, G762_REG_FAN_CMD2,
					 data->fan_cmd2);
}

static int g762_probe(struct i2c_client *client)
{
	struct device *dev = &client->dev;
	struct device *hwmon_dev;
	struct g762_data *data;
	int ret;

	if (!i2c_check_functionality(client->adapter,
				     I2C_FUNC_SMBUS_BYTE_DATA))
		return -ENODEV;

	data = devm_kzalloc(dev, sizeof(struct g762_data), GFP_KERNEL);
	if (!data)
		return -ENOMEM;

	i2c_set_clientdata(client, data);
	data->client = client;
	mutex_init(&data->update_lock);

	/* Get configuration via DT ... */
	ret = g762_of_clock_enable(client);
	if (ret)
		return ret;

	/* Enable fan failure detection and fan out of control protection */
	ret = g762_fan_init(dev);
	if (ret)
		return ret;

	ret = g762_of_prop_import(client);
	if (ret)
		return ret;
	/* ... or platform_data */
	ret = g762_pdata_prop_import(client);
	if (ret)
		return ret;

	hwmon_dev = devm_hwmon_device_register_with_groups(dev, client->name,
							    data, g762_groups);
	return PTR_ERR_OR_ZERO(hwmon_dev);
}

static struct i2c_driver g762_driver = {
	.driver = {
		.name = DRVNAME,
		.of_match_table = of_match_ptr(g762_dt_match),
	},
	.probe = g762_probe,
	.id_table = g762_id,
};

module_i2c_driver(g762_driver);

MODULE_AUTHOR("Arnaud EBALARD <arno@natisbad.org>");
MODULE_DESCRIPTION("GMT G762/G763 driver");
MODULE_LICENSE("GPL");
