// SPDX-License-Identifier: GPL-2.0-only

#include <linux/bits.h>
#include <linux/i2c.h>
#include <linux/i2c-mux.h>
#include <linux/mod_devicetable.h>
#include <linux/mfd/syscon.h>
#include <linux/mutex.h>
#include <linux/platform_device.h>
#include <linux/regmap.h>

enum rtl9300_bus_freq {
	RTL9300_I2C_STD_FREQ,
	RTL9300_I2C_FAST_FREQ,
};

struct rtl9300_i2c;

struct rtl9300_i2c_chan {
	struct i2c_adapter adap;
	struct rtl9300_i2c *i2c;
	enum rtl9300_bus_freq bus_freq;
	u8 sda_pin;
};

#define RTL9300_I2C_MUX_NCHAN	8

struct rtl9300_i2c {
	struct regmap *regmap;
	struct device *dev;
	struct rtl9300_i2c_chan chans[RTL9300_I2C_MUX_NCHAN];
	u32 reg_base;
	u8 sda_pin;
	struct mutex lock;
};

#define RTL9300_I2C_MST_CTRL1				0x0
#define  RTL9300_I2C_MST_CTRL1_MEM_ADDR_OFS		8
#define  RTL9300_I2C_MST_CTRL1_MEM_ADDR_MASK		GENMASK(31, 8)
#define  RTL9300_I2C_MST_CTRL1_SDA_OUT_SEL_OFS		4
#define  RTL9300_I2C_MST_CTRL1_SDA_OUT_SEL_MASK		GENMASK(6, 4)
#define  RTL9300_I2C_MST_CTRL1_GPIO_SCL_SEL		BIT(3)
#define  RTL9300_I2C_MST_CTRL1_RWOP			BIT(2)
#define  RTL9300_I2C_MST_CTRL1_I2C_FAIL			BIT(1)
#define  RTL9300_I2C_MST_CTRL1_I2C_TRIG			BIT(0)
#define RTL9300_I2C_MST_CTRL2				0x4
#define  RTL9300_I2C_MST_CTRL2_RD_MODE			BIT(15)
#define  RTL9300_I2C_MST_CTRL2_DEV_ADDR_OFS		8
#define  RTL9300_I2C_MST_CTRL2_DEV_ADDR_MASK		GENMASK(14, 8)
#define  RTL9300_I2C_MST_CTRL2_DATA_WIDTH_OFS		4
#define  RTL9300_I2C_MST_CTRL2_DATA_WIDTH_MASK		GENMASK(7, 4)
#define  RTL9300_I2C_MST_CTRL2_MEM_ADDR_WIDTH_OFS	2
#define  RTL9300_I2C_MST_CTRL2_MEM_ADDR_WIDTH_MASK	GENMASK(3, 2)
#define  RTL9300_I2C_MST_CTRL2_SCL_FREQ_OFS		0
#define  RTL9300_I2C_MST_CTRL2_SCL_FREQ_MASK		GENMASK(1, 0)
#define RTL9300_I2C_MST_DATA_WORD0			0x8
#define RTL9300_I2C_MST_DATA_WORD1			0xc
#define RTL9300_I2C_MST_DATA_WORD2			0x10
#define RTL9300_I2C_MST_DATA_WORD3			0x14

#define RTL9300_I2C_MST_GLB_CTRL			0x384

static int rtl9300_i2c_reg_addr_set(struct rtl9300_i2c *i2c, u32 reg, u16 len)
{
	u32 val, mask;
	int ret;

	val = len << RTL9300_I2C_MST_CTRL2_MEM_ADDR_WIDTH_OFS;
	mask = RTL9300_I2C_MST_CTRL2_MEM_ADDR_WIDTH_MASK;

	ret = regmap_update_bits(i2c->regmap, i2c->reg_base + RTL9300_I2C_MST_CTRL2, mask, val);
	if (ret)
		return ret;

	val = reg << RTL9300_I2C_MST_CTRL1_MEM_ADDR_OFS;
	mask = RTL9300_I2C_MST_CTRL1_MEM_ADDR_MASK;

	return regmap_update_bits(i2c->regmap, i2c->reg_base + RTL9300_I2C_MST_CTRL1, mask, val);
}

static int rtl9300_i2c_config_io(struct rtl9300_i2c *i2c, u8 sda_pin)
{
	int ret;
	u32 val, mask;

	ret = regmap_update_bits(i2c->regmap, RTL9300_I2C_MST_GLB_CTRL, BIT(sda_pin), BIT(sda_pin));
	if (ret)
		return ret;

	val = (sda_pin << RTL9300_I2C_MST_CTRL1_SDA_OUT_SEL_OFS) |
		RTL9300_I2C_MST_CTRL1_GPIO_SCL_SEL;
	mask = RTL9300_I2C_MST_CTRL1_SDA_OUT_SEL_MASK | RTL9300_I2C_MST_CTRL1_GPIO_SCL_SEL;

	return regmap_update_bits(i2c->regmap, i2c->reg_base + RTL9300_I2C_MST_CTRL1, mask, val);
}

static int rtl9300_i2c_config_xfer(struct rtl9300_i2c *i2c, struct rtl9300_i2c_chan *chan,
				   u16 addr, u16 len)
{
	u32 val, mask;

	val = chan->bus_freq << RTL9300_I2C_MST_CTRL2_SCL_FREQ_OFS;
	mask = RTL9300_I2C_MST_CTRL2_SCL_FREQ_MASK;

	val |= addr << RTL9300_I2C_MST_CTRL2_DEV_ADDR_OFS;
	mask |= RTL9300_I2C_MST_CTRL2_DEV_ADDR_MASK;

	val |= ((len - 1) & 0xf) << RTL9300_I2C_MST_CTRL2_DATA_WIDTH_OFS;
	mask |= RTL9300_I2C_MST_CTRL2_DATA_WIDTH_MASK;

	mask |= RTL9300_I2C_MST_CTRL2_RD_MODE;

	return regmap_update_bits(i2c->regmap, i2c->reg_base + RTL9300_I2C_MST_CTRL2, mask, val);
}

static int rtl9300_i2c_read(struct rtl9300_i2c *i2c, u8 *buf, int len)
{
	u32 vals[4] = {};
	int i, ret;

	if (len > 16)
		return -EIO;

	ret = regmap_bulk_read(i2c->regmap, i2c->reg_base + RTL9300_I2C_MST_DATA_WORD0,
			       vals, ARRAY_SIZE(vals));
	if (ret)
		return ret;

	for (i = 0; i < len; i++) {
		buf[i] = vals[i/4] & 0xff;
		vals[i/4] >>= 8;
	}

	return 0;
}

static int rtl9300_i2c_write(struct rtl9300_i2c *i2c, u8 *buf, int len)
{
	u32 vals[4] = {};
	int i;

	if (len > 16)
		return -EIO;

	for (i = 0; i < len; i++) {
		unsigned int shift = (i % 4) * 8;
		unsigned int reg = i / 4;

		vals[reg] |= buf[i] << shift;
	}

	return regmap_bulk_write(i2c->regmap, i2c->reg_base + RTL9300_I2C_MST_DATA_WORD0,
				vals, ARRAY_SIZE(vals));
}

static int rtl9300_i2c_writel(struct rtl9300_i2c *i2c, u32 data)
{
	return regmap_write(i2c->regmap, i2c->reg_base + RTL9300_I2C_MST_DATA_WORD0, data);
}

static int rtl9300_i2c_execute_xfer(struct rtl9300_i2c *i2c, char read_write,
				    int size, union i2c_smbus_data *data, int len)
{
	u32 val, mask;
	int ret;

	val = read_write == I2C_SMBUS_WRITE ? RTL9300_I2C_MST_CTRL1_RWOP : 0;
	mask = RTL9300_I2C_MST_CTRL1_RWOP;

	val |= RTL9300_I2C_MST_CTRL1_I2C_TRIG;
	mask |= RTL9300_I2C_MST_CTRL1_I2C_TRIG;

	ret = regmap_update_bits(i2c->regmap, i2c->reg_base + RTL9300_I2C_MST_CTRL1, mask, val);
	if (ret)
		return ret;

	ret = regmap_read_poll_timeout(i2c->regmap, i2c->reg_base + RTL9300_I2C_MST_CTRL1,
				       val, !(val & RTL9300_I2C_MST_CTRL1_I2C_TRIG), 100, 100000);
	if (ret)
		return ret;

	if (val & RTL9300_I2C_MST_CTRL1_I2C_FAIL)
		return -EIO;

	if (read_write == I2C_SMBUS_READ) {
		if (size == I2C_SMBUS_BYTE || size == I2C_SMBUS_BYTE_DATA) {
			ret = regmap_read(i2c->regmap,
					  i2c->reg_base + RTL9300_I2C_MST_DATA_WORD0, &val);
			if (ret)
				return ret;
			data->byte = val & 0xff;
		} else if (size == I2C_SMBUS_WORD_DATA) {
			ret = regmap_read(i2c->regmap,
					  i2c->reg_base + RTL9300_I2C_MST_DATA_WORD0, &val);
			if (ret)
				return ret;
			data->word = val & 0xffff;
		} else {
			ret = rtl9300_i2c_read(i2c, &data->block[0], len);
			if (ret)
				return ret;
		}
	}

	return 0;
}

static int rtl9300_i2c_smbus_xfer(struct i2c_adapter *adap, u16 addr, unsigned short flags,
				  char read_write, u8 command, int size,
				  union i2c_smbus_data *data)
{
	struct rtl9300_i2c_chan *chan = i2c_get_adapdata(adap);
	struct rtl9300_i2c *i2c = chan->i2c;
	int len = 0, ret;

	mutex_lock(&i2c->lock);
	if (chan->sda_pin != i2c->sda_pin) {
		ret = rtl9300_i2c_config_io(i2c, chan->sda_pin);
		if (ret)
			goto out_unlock;
		i2c->sda_pin = chan->sda_pin;
	}

	switch (size) {
	case I2C_SMBUS_QUICK:
		ret = rtl9300_i2c_config_xfer(i2c, chan, addr, 0);
		if (ret)
			goto out_unlock;
		ret = rtl9300_i2c_reg_addr_set(i2c, 0, 0);
		if (ret)
			goto out_unlock;
		break;

	case I2C_SMBUS_BYTE:
		if (read_write == I2C_SMBUS_WRITE) {
			ret = rtl9300_i2c_config_xfer(i2c, chan, addr, 0);
			if (ret)
				goto out_unlock;
			ret = rtl9300_i2c_reg_addr_set(i2c, command, 1);
			if (ret)
				goto out_unlock;
		} else {
			ret = rtl9300_i2c_config_xfer(i2c, chan, addr, 1);
			if (ret)
				goto out_unlock;
			ret = rtl9300_i2c_reg_addr_set(i2c, 0, 0);
			if (ret)
				goto out_unlock;
		}
		break;

	case I2C_SMBUS_BYTE_DATA:
		ret = rtl9300_i2c_reg_addr_set(i2c, command, 1);
		if (ret)
			goto out_unlock;
		ret = rtl9300_i2c_config_xfer(i2c, chan, addr, 1);
		if (ret)
			goto out_unlock;
		if (read_write == I2C_SMBUS_WRITE) {
			ret = rtl9300_i2c_writel(i2c, data->byte);
			if (ret)
				goto out_unlock;
		}
		break;

	case I2C_SMBUS_WORD_DATA:
		ret = rtl9300_i2c_reg_addr_set(i2c, command, 1);
		if (ret)
			goto out_unlock;
		ret = rtl9300_i2c_config_xfer(i2c, chan, addr, 2);
		if (ret)
			goto out_unlock;
		if (read_write == I2C_SMBUS_WRITE) {
			ret = rtl9300_i2c_writel(i2c, data->word);
			if (ret)
				goto out_unlock;
		}
		break;

	case I2C_SMBUS_BLOCK_DATA:
		ret = rtl9300_i2c_reg_addr_set(i2c, command, 1);
		if (ret)
			goto out_unlock;
		if (data->block[0] < 1 || data->block[0] > I2C_SMBUS_BLOCK_MAX) {
			ret = -EINVAL;
			goto out_unlock;
		}
		ret = rtl9300_i2c_config_xfer(i2c, chan, addr, data->block[0] + 1);
		if (ret)
			goto out_unlock;
		if (read_write == I2C_SMBUS_WRITE) {
			ret = rtl9300_i2c_write(i2c, &data->block[0], data->block[0] + 1);
			if (ret)
				goto out_unlock;
		}
		len = data->block[0] + 1;
		break;

	default:
		dev_err(&adap->dev, "Unsupported transaction %d\n", size);
		ret = -EOPNOTSUPP;
		goto out_unlock;
	}

	ret = rtl9300_i2c_execute_xfer(i2c, read_write, size, data, len);

out_unlock:
	mutex_unlock(&i2c->lock);

	return ret;
}

static u32 rtl9300_i2c_func(struct i2c_adapter *a)
{
	return I2C_FUNC_SMBUS_QUICK | I2C_FUNC_SMBUS_BYTE |
	       I2C_FUNC_SMBUS_BYTE_DATA | I2C_FUNC_SMBUS_WORD_DATA |
	       I2C_FUNC_SMBUS_BLOCK_DATA;
}

static const struct i2c_algorithm rtl9300_i2c_algo = {
	.smbus_xfer	= rtl9300_i2c_smbus_xfer,
	.functionality	= rtl9300_i2c_func,
};

static struct i2c_adapter_quirks rtl9300_i2c_quirks = {
	.flags		= I2C_AQ_NO_CLK_STRETCH,
	.max_read_len	= 16,
	.max_write_len	= 16,
};

static int rtl9300_i2c_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct rtl9300_i2c *i2c;
	u32 clock_freq, sda_pin;
	int ret, i = 0;
	struct fwnode_handle *child;

	i2c = devm_kzalloc(dev, sizeof(*i2c), GFP_KERNEL);
	if (!i2c)
		return -ENOMEM;

	i2c->regmap = syscon_node_to_regmap(dev->parent->of_node);
	if (IS_ERR(i2c->regmap))
		return PTR_ERR(i2c->regmap);
	i2c->dev = dev;

	mutex_init(&i2c->lock);

	ret = device_property_read_u32(dev, "reg", &i2c->reg_base);
	if (ret)
		return ret;

	platform_set_drvdata(pdev, i2c);

	if (device_get_child_node_count(dev) > RTL9300_I2C_MUX_NCHAN)
		return dev_err_probe(dev, -EINVAL, "Too many channels\n");

	device_for_each_child_node(dev, child) {
		struct rtl9300_i2c_chan *chan = &i2c->chans[i];
		struct i2c_adapter *adap = &chan->adap;

		ret = fwnode_property_read_u32(child, "reg", &sda_pin);
		if (ret)
			return ret;

		ret = fwnode_property_read_u32(child, "clock-frequency", &clock_freq);
		if (ret)
			clock_freq = I2C_MAX_STANDARD_MODE_FREQ;

		switch (clock_freq) {
		case I2C_MAX_STANDARD_MODE_FREQ:
			chan->bus_freq = RTL9300_I2C_STD_FREQ;
			break;

		case I2C_MAX_FAST_MODE_FREQ:
			chan->bus_freq = RTL9300_I2C_FAST_FREQ;
			break;
		default:
			dev_warn(i2c->dev, "SDA%d clock-frequency %d not supported using default\n",
				 sda_pin, clock_freq);
			break;
		}

		chan->sda_pin = sda_pin;
		chan->i2c = i2c;
		adap = &i2c->chans[i].adap;
		adap->owner = THIS_MODULE;
		adap->algo = &rtl9300_i2c_algo;
		adap->quirks = &rtl9300_i2c_quirks;
		adap->retries = 3;
		adap->dev.parent = dev;
		i2c_set_adapdata(adap, chan);
		adap->dev.of_node = to_of_node(child);
		snprintf(adap->name, sizeof(adap->name), "%s SDA%d\n", dev_name(dev), sda_pin);
		i++;

		ret = devm_i2c_add_adapter(dev, adap);
		if (ret)
			return ret;
	}
	i2c->sda_pin = 0xff;

	return 0;
}

static const struct of_device_id i2c_rtl9300_dt_ids[] = {
	{ .compatible = "realtek,rtl9301-i2c" },
	{ .compatible = "realtek,rtl9302b-i2c" },
	{ .compatible = "realtek,rtl9302c-i2c" },
	{ .compatible = "realtek,rtl9303-i2c" },
	{}
};
MODULE_DEVICE_TABLE(of, i2c_rtl9300_dt_ids);

static struct platform_driver rtl9300_i2c_driver = {
	.probe = rtl9300_i2c_probe,
	.driver = {
		.name = "i2c-rtl9300",
		.of_match_table = i2c_rtl9300_dt_ids,
	},
};

module_platform_driver(rtl9300_i2c_driver);

MODULE_DESCRIPTION("RTL9300 I2C controller driver");
MODULE_LICENSE("GPL");
