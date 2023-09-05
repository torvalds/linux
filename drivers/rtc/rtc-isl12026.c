// SPDX-License-Identifier: GPL-2.0+
/*
 * An I2C driver for the Intersil ISL 12026
 *
 * Copyright (c) 2018 Cavium, Inc.
 */
#include <linux/bcd.h>
#include <linux/delay.h>
#include <linux/i2c.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/nvmem-provider.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/rtc.h>
#include <linux/slab.h>

/* register offsets */
#define ISL12026_REG_PWR	0x14
# define ISL12026_REG_PWR_BSW	BIT(6)
# define ISL12026_REG_PWR_SBIB	BIT(7)
#define ISL12026_REG_SC		0x30
#define ISL12026_REG_HR		0x32
# define ISL12026_REG_HR_MIL	BIT(7)	/* military or 24 hour time */
#define ISL12026_REG_SR		0x3f
# define ISL12026_REG_SR_RTCF	BIT(0)
# define ISL12026_REG_SR_WEL	BIT(1)
# define ISL12026_REG_SR_RWEL	BIT(2)
# define ISL12026_REG_SR_MBZ	BIT(3)
# define ISL12026_REG_SR_OSCF	BIT(4)

/* The EEPROM array responds at i2c address 0x57 */
#define ISL12026_EEPROM_ADDR	0x57

#define ISL12026_PAGESIZE 16
#define ISL12026_NVMEM_WRITE_TIME 20

struct isl12026 {
	struct rtc_device *rtc;
	struct i2c_client *nvm_client;
};

static int isl12026_read_reg(struct i2c_client *client, int reg)
{
	u8 addr[] = {0, reg};
	u8 val;
	int ret;

	struct i2c_msg msgs[] = {
		{
			.addr	= client->addr,
			.flags	= 0,
			.len	= sizeof(addr),
			.buf	= addr
		}, {
			.addr	= client->addr,
			.flags	= I2C_M_RD,
			.len	= 1,
			.buf	= &val
		}
	};

	ret = i2c_transfer(client->adapter, msgs, ARRAY_SIZE(msgs));
	if (ret != ARRAY_SIZE(msgs)) {
		dev_err(&client->dev, "read reg error, ret=%d\n", ret);
		ret = ret < 0 ? ret : -EIO;
	} else {
		ret = val;
	}

	return ret;
}

static int isl12026_arm_write(struct i2c_client *client)
{
	int ret;
	u8 op[3];
	struct i2c_msg msg = {
		.addr	= client->addr,
		.flags	= 0,
		.len	= 1,
		.buf	= op
	};

	/* Set SR.WEL */
	op[0] = 0;
	op[1] = ISL12026_REG_SR;
	op[2] = ISL12026_REG_SR_WEL;
	msg.len = 3;
	ret = i2c_transfer(client->adapter, &msg, 1);
	if (ret != 1) {
		dev_err(&client->dev, "write error SR.WEL, ret=%d\n", ret);
		ret = ret < 0 ? ret : -EIO;
		goto out;
	}

	/* Set SR.WEL and SR.RWEL */
	op[2] = ISL12026_REG_SR_WEL | ISL12026_REG_SR_RWEL;
	msg.len = 3;
	ret = i2c_transfer(client->adapter, &msg, 1);
	if (ret != 1) {
		dev_err(&client->dev,
			"write error SR.WEL|SR.RWEL, ret=%d\n", ret);
		ret = ret < 0 ? ret : -EIO;
		goto out;
	} else {
		ret = 0;
	}
out:
	return ret;
}

static int isl12026_disarm_write(struct i2c_client *client)
{
	int ret;
	u8 op[3] = {0, ISL12026_REG_SR, 0};
	struct i2c_msg msg = {
		.addr	= client->addr,
		.flags	= 0,
		.len	= sizeof(op),
		.buf	= op
	};

	ret = i2c_transfer(client->adapter, &msg, 1);
	if (ret != 1) {
		dev_err(&client->dev,
			"write error SR, ret=%d\n", ret);
		ret = ret < 0 ? ret : -EIO;
	} else {
		ret = 0;
	}

	return ret;
}

static int isl12026_write_reg(struct i2c_client *client, int reg, u8 val)
{
	int ret;
	u8 op[3] = {0, reg, val};
	struct i2c_msg msg = {
		.addr	= client->addr,
		.flags	= 0,
		.len	= sizeof(op),
		.buf	= op
	};

	ret = isl12026_arm_write(client);
	if (ret)
		return ret;

	ret = i2c_transfer(client->adapter, &msg, 1);
	if (ret != 1) {
		dev_err(&client->dev, "write error CCR, ret=%d\n", ret);
		ret = ret < 0 ? ret : -EIO;
		goto out;
	}

	msleep(ISL12026_NVMEM_WRITE_TIME);

	ret = isl12026_disarm_write(client);
out:
	return ret;
}

static int isl12026_rtc_set_time(struct device *dev, struct rtc_time *tm)
{
	struct i2c_client *client = to_i2c_client(dev);
	int ret;
	u8 op[10];
	struct i2c_msg msg = {
		.addr	= client->addr,
		.flags	= 0,
		.len	= sizeof(op),
		.buf	= op
	};

	ret = isl12026_arm_write(client);
	if (ret)
		return ret;

	/* Set the CCR registers */
	op[0] = 0;
	op[1] = ISL12026_REG_SC;
	op[2] = bin2bcd(tm->tm_sec); /* SC */
	op[3] = bin2bcd(tm->tm_min); /* MN */
	op[4] = bin2bcd(tm->tm_hour) | ISL12026_REG_HR_MIL; /* HR */
	op[5] = bin2bcd(tm->tm_mday); /* DT */
	op[6] = bin2bcd(tm->tm_mon + 1); /* MO */
	op[7] = bin2bcd(tm->tm_year % 100); /* YR */
	op[8] = bin2bcd(tm->tm_wday & 7); /* DW */
	op[9] = bin2bcd(tm->tm_year >= 100 ? 20 : 19); /* Y2K */
	ret = i2c_transfer(client->adapter, &msg, 1);
	if (ret != 1) {
		dev_err(&client->dev, "write error CCR, ret=%d\n", ret);
		ret = ret < 0 ? ret : -EIO;
		goto out;
	}

	ret = isl12026_disarm_write(client);
out:
	return ret;
}

static int isl12026_rtc_read_time(struct device *dev, struct rtc_time *tm)
{
	struct i2c_client *client = to_i2c_client(dev);
	u8 ccr[8];
	u8 addr[2];
	u8 sr;
	int ret;
	struct i2c_msg msgs[] = {
		{
			.addr	= client->addr,
			.flags	= 0,
			.len	= sizeof(addr),
			.buf	= addr
		}, {
			.addr	= client->addr,
			.flags	= I2C_M_RD,
		}
	};

	/* First, read SR */
	addr[0] = 0;
	addr[1] = ISL12026_REG_SR;
	msgs[1].len = 1;
	msgs[1].buf = &sr;

	ret = i2c_transfer(client->adapter, msgs, ARRAY_SIZE(msgs));
	if (ret != ARRAY_SIZE(msgs)) {
		dev_err(&client->dev, "read error, ret=%d\n", ret);
		ret = ret < 0 ? ret : -EIO;
		goto out;
	}

	if (sr & ISL12026_REG_SR_RTCF)
		dev_warn(&client->dev, "Real-Time Clock Failure on read\n");
	if (sr & ISL12026_REG_SR_OSCF)
		dev_warn(&client->dev, "Oscillator Failure on read\n");

	/* Second, CCR regs */
	addr[0] = 0;
	addr[1] = ISL12026_REG_SC;
	msgs[1].len = sizeof(ccr);
	msgs[1].buf = ccr;

	ret = i2c_transfer(client->adapter, msgs, ARRAY_SIZE(msgs));
	if (ret != ARRAY_SIZE(msgs)) {
		dev_err(&client->dev, "read error, ret=%d\n", ret);
		ret = ret < 0 ? ret : -EIO;
		goto out;
	}

	tm->tm_sec = bcd2bin(ccr[0] & 0x7F);
	tm->tm_min = bcd2bin(ccr[1] & 0x7F);
	if (ccr[2] & ISL12026_REG_HR_MIL)
		tm->tm_hour = bcd2bin(ccr[2] & 0x3F);
	else
		tm->tm_hour = bcd2bin(ccr[2] & 0x1F) +
			((ccr[2] & 0x20) ? 12 : 0);
	tm->tm_mday = bcd2bin(ccr[3] & 0x3F);
	tm->tm_mon = bcd2bin(ccr[4] & 0x1F) - 1;
	tm->tm_year = bcd2bin(ccr[5]);
	if (bcd2bin(ccr[7]) == 20)
		tm->tm_year += 100;
	tm->tm_wday = ccr[6] & 0x07;

	ret = 0;
out:
	return ret;
}

static const struct rtc_class_ops isl12026_rtc_ops = {
	.read_time	= isl12026_rtc_read_time,
	.set_time	= isl12026_rtc_set_time,
};

static int isl12026_nvm_read(void *p, unsigned int offset,
			     void *val, size_t bytes)
{
	struct isl12026 *priv = p;
	int ret;
	u8 addr[2];
	struct i2c_msg msgs[] = {
		{
			.addr	= priv->nvm_client->addr,
			.flags	= 0,
			.len	= sizeof(addr),
			.buf	= addr
		}, {
			.addr	= priv->nvm_client->addr,
			.flags	= I2C_M_RD,
			.buf	= val
		}
	};

	/*
	 * offset and bytes checked and limited by nvmem core, so
	 * proceed without further checks.
	 */
	ret = mutex_lock_interruptible(&priv->rtc->ops_lock);
	if (ret)
		return ret;

	/* 2 bytes of address, most significant first */
	addr[0] = offset >> 8;
	addr[1] = offset;
	msgs[1].len = bytes;
	ret = i2c_transfer(priv->nvm_client->adapter, msgs, ARRAY_SIZE(msgs));

	mutex_unlock(&priv->rtc->ops_lock);

	if (ret != ARRAY_SIZE(msgs)) {
		dev_err(&priv->nvm_client->dev,
			"nvmem read error, ret=%d\n", ret);
		return ret < 0 ? ret : -EIO;
	}

	return 0;
}

static int isl12026_nvm_write(void *p, unsigned int offset,
			      void *val, size_t bytes)
{
	struct isl12026 *priv = p;
	int ret;
	u8 *v = val;
	size_t chunk_size, num_written;
	u8 payload[ISL12026_PAGESIZE + 2]; /* page + 2 address bytes */
	struct i2c_msg msgs[] = {
		{
			.addr	= priv->nvm_client->addr,
			.flags	= 0,
			.buf	= payload
		}
	};

	/*
	 * offset and bytes checked and limited by nvmem core, so
	 * proceed without further checks.
	 */
	ret = mutex_lock_interruptible(&priv->rtc->ops_lock);
	if (ret)
		return ret;

	num_written = 0;
	while (bytes) {
		chunk_size = round_down(offset, ISL12026_PAGESIZE) +
			ISL12026_PAGESIZE - offset;
		chunk_size = min(bytes, chunk_size);
		/*
		 * 2 bytes of address, most significant first, followed
		 * by page data bytes
		 */
		memcpy(payload + 2, v + num_written, chunk_size);
		payload[0] = offset >> 8;
		payload[1] = offset;
		msgs[0].len = chunk_size + 2;
		ret = i2c_transfer(priv->nvm_client->adapter,
				   msgs, ARRAY_SIZE(msgs));
		if (ret != ARRAY_SIZE(msgs)) {
			dev_err(&priv->nvm_client->dev,
				"nvmem write error, ret=%d\n", ret);
			ret = ret < 0 ? ret : -EIO;
			break;
		}
		ret = 0;
		bytes -= chunk_size;
		offset += chunk_size;
		num_written += chunk_size;
		msleep(ISL12026_NVMEM_WRITE_TIME);
	}

	mutex_unlock(&priv->rtc->ops_lock);

	return ret;
}

static void isl12026_force_power_modes(struct i2c_client *client)
{
	int ret;
	int pwr, requested_pwr;
	u32 bsw_val, sbib_val;
	bool set_bsw, set_sbib;

	/*
	 * If we can read the of_property, set the specified value.
	 * If there is an error reading the of_property (likely
	 * because it does not exist), keep the current value.
	 */
	ret = of_property_read_u32(client->dev.of_node,
				   "isil,pwr-bsw", &bsw_val);
	set_bsw = (ret == 0);

	ret = of_property_read_u32(client->dev.of_node,
				   "isil,pwr-sbib", &sbib_val);
	set_sbib = (ret == 0);

	/* Check if PWR.BSW and/or PWR.SBIB need specified values */
	if (!set_bsw && !set_sbib)
		return;

	pwr = isl12026_read_reg(client, ISL12026_REG_PWR);
	if (pwr < 0) {
		dev_warn(&client->dev, "Error: Failed to read PWR %d\n", pwr);
		return;
	}

	requested_pwr = pwr;

	if (set_bsw) {
		if (bsw_val)
			requested_pwr |= ISL12026_REG_PWR_BSW;
		else
			requested_pwr &= ~ISL12026_REG_PWR_BSW;
	} /* else keep current BSW */

	if (set_sbib) {
		if (sbib_val)
			requested_pwr |= ISL12026_REG_PWR_SBIB;
		else
			requested_pwr &= ~ISL12026_REG_PWR_SBIB;
	} /* else keep current SBIB */

	if (pwr >= 0 && pwr != requested_pwr) {
		dev_dbg(&client->dev, "PWR: %02x\n", pwr);
		dev_dbg(&client->dev, "Updating PWR to: %02x\n", requested_pwr);
		isl12026_write_reg(client, ISL12026_REG_PWR, requested_pwr);
	}
}

static int isl12026_probe_new(struct i2c_client *client)
{
	struct isl12026 *priv;
	int ret;
	struct nvmem_config nvm_cfg = {
		.name = "isl12026-",
		.base_dev = &client->dev,
		.stride = 1,
		.word_size = 1,
		.size = 512,
		.reg_read = isl12026_nvm_read,
		.reg_write = isl12026_nvm_write,
	};

	if (!i2c_check_functionality(client->adapter, I2C_FUNC_I2C))
		return -ENODEV;

	priv = devm_kzalloc(&client->dev, sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	i2c_set_clientdata(client, priv);

	isl12026_force_power_modes(client);

	priv->nvm_client = i2c_new_dummy_device(client->adapter, ISL12026_EEPROM_ADDR);
	if (IS_ERR(priv->nvm_client))
		return PTR_ERR(priv->nvm_client);

	priv->rtc = devm_rtc_allocate_device(&client->dev);
	ret = PTR_ERR_OR_ZERO(priv->rtc);
	if (ret)
		return ret;

	priv->rtc->ops = &isl12026_rtc_ops;
	nvm_cfg.priv = priv;
	ret = devm_rtc_nvmem_register(priv->rtc, &nvm_cfg);
	if (ret)
		return ret;

	return devm_rtc_register_device(priv->rtc);
}

static void isl12026_remove(struct i2c_client *client)
{
	struct isl12026 *priv = i2c_get_clientdata(client);

	i2c_unregister_device(priv->nvm_client);
}

static const struct of_device_id isl12026_dt_match[] = {
	{ .compatible = "isil,isl12026" },
	{ }
};
MODULE_DEVICE_TABLE(of, isl12026_dt_match);

static struct i2c_driver isl12026_driver = {
	.driver		= {
		.name	= "rtc-isl12026",
		.of_match_table = isl12026_dt_match,
	},
	.probe		= isl12026_probe_new,
	.remove		= isl12026_remove,
};

module_i2c_driver(isl12026_driver);

MODULE_DESCRIPTION("ISL 12026 RTC driver");
MODULE_LICENSE("GPL");
