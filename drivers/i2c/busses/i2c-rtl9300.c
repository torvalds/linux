// SPDX-License-Identifier: GPL-2.0-only

#include <linux/bits.h>
#include <linux/clk.h>
#include <linux/i2c.h>
#include <linux/i2c-mux.h>
#include <linux/mod_devicetable.h>
#include <linux/mfd/syscon.h>
#include <linux/mutex.h>
#include <linux/platform_device.h>
#include <linux/regmap.h>
#include <linux/unaligned.h>

enum rtl9300_bus_freq {
	RTL9300_I2C_STD_FREQ,			// 100kHz
	RTL9300_I2C_FAST_FREQ,			// 400kHz
	RTL9300_I2C_SUPER_FAST_FREQ,		// 2.5MHz
	RTL9300_I2C_SLOW_FREQ,			// 50kHz
};

#define RTL9300_I2C_MAX_SUPER_FAST_FREQ		2500000
#define RTL9300_I2C_MAX_SLOW_FREQ		50000


struct rtl9300_i2c;

struct rtl9300_i2c_chan {
	struct i2c_adapter adap;
	struct rtl9300_i2c *i2c;
	enum rtl9300_bus_freq bus_freq;
	u8 sda_num;
	u32 clk_div;
};

enum rtl9300_i2c_reg_scope {
	REG_SCOPE_GLOBAL,
	REG_SCOPE_MASTER,
};

struct rtl9300_i2c_reg_field {
	struct reg_field field;
	enum rtl9300_i2c_reg_scope scope;
};

enum rtl9300_i2c_reg_fields {
	F_DATA_WIDTH = 0,
	F_DEV_ADDR,
	F_I2C_FAIL,
	F_I2C_TRIG,
	F_MEM_ADDR,
	F_MEM_ADDR_WIDTH,
	F_RD_MODE,
	F_RWOP,
	F_SCL_FREQ,
	F_SCL_SEL,
	F_SDA_OUT_SEL,
	F_SDA_SEL,
	F_BUSY,
	F_CLK_DIV,
	F_EXT_SCK_5MS,

	/* keep last */
	F_NUM_FIELDS
};

struct rtl9300_i2c_drv_data {
	struct rtl9300_i2c_reg_field field_desc[F_NUM_FIELDS];
	int (*select_scl)(struct rtl9300_i2c *i2c, u8 scl);
	int (*config_chan)(struct rtl9300_i2c *i2c, struct rtl9300_i2c_chan *chan);
	void (*config_clock)(u32 clock_freq, struct rtl9300_i2c_chan *chan);
	int (*misc_init)(struct rtl9300_i2c *i2c);
	u32 rd_reg;
	u32 wd_reg;
	u8 max_nchan;
	u8 max_data_len;
	u8 reg_addr_8bit_len;
};

#define RTL9300_I2C_MUX_NCHAN	8
#define RTL9310_I2C_MUX_NCHAN	12
#define RTL9607_I2C_MUX_NCHAN	1

#define RTL9300_I2C_MAX_DATA_LEN	16
#define RTL9607_I2C_MAX_DATA_LEN	4

struct rtl9300_i2c {
	struct regmap *regmap;
	struct device *dev;
	struct rtl9300_i2c_chan chans[RTL9310_I2C_MUX_NCHAN];
	struct regmap_field *fields[F_NUM_FIELDS];
	u32 reg_base;
	u32 rd_reg;
	u32 wd_reg;
	u8 scl_num;
	u8 sda_num;
	struct mutex lock;
	struct clk *clk;
};

DEFINE_GUARD(rtl9300_i2c, struct rtl9300_i2c *, mutex_lock(&_T->lock), mutex_unlock(&_T->lock))

enum rtl9300_i2c_xfer_type {
	RTL9300_I2C_XFER_BYTE,
	RTL9300_I2C_XFER_WORD,
	RTL9300_I2C_XFER_BLOCK,
};

struct rtl9300_i2c_xfer {
	enum rtl9300_i2c_xfer_type type;
	u16 dev_addr;
	u8 reg_addr;
	u8 reg_addr_len;
	u8 *data;
	u8 data_len;
	bool write;
};

#define RTL9300_I2C_MST_CTRL1				0x0
#define RTL9300_I2C_MST_CTRL2				0x4
#define RTL9300_I2C_MST_DATA_WORD0			0x8
#define RTL9300_I2C_MST_DATA_WORD1			0xc
#define RTL9300_I2C_MST_DATA_WORD2			0x10
#define RTL9300_I2C_MST_DATA_WORD3			0x14
#define RTL9300_I2C_MST_GLB_CTRL			0x384
#define RTL9300_REG_ADDR_8BIT_LEN			1

#define RTL9310_I2C_MST_IF_CTRL				0x1004
#define RTL9310_I2C_MST_IF_SEL				0x1008
#define RTL9310_I2C_MST_CTRL				0x0
#define RTL9310_I2C_MST_MEMADDR_CTRL			0x4
#define RTL9310_I2C_MST_DATA_CTRL			0x8

#define RTL9607_I2C_CONFIG				0x22f50
#define RTL9607_IO_MODE_EN				0x23014
#define RTL9607_I2C_IND_WD				0x0
#define RTL9607_I2C_IND_ADR				0x8
#define RTL9607_I2C_IND_CMD				0x10
#define RTL9607_I2C_IND_RD				0x18
#define RTL9607_REG_ADDR_8BIT_LEN			0

static int rtl9300_i2c_reg_addr_set(struct rtl9300_i2c *i2c, u32 reg, u16 len)
{
	int ret;

	ret = regmap_field_write(i2c->fields[F_MEM_ADDR_WIDTH], len);
	if (ret)
		return ret;

	return regmap_field_write(i2c->fields[F_MEM_ADDR], reg);
}

static int rtl9300_i2c_select_scl(struct rtl9300_i2c *i2c, u8 scl)
{
	return regmap_field_write(i2c->fields[F_SCL_SEL], 1);
}

static int rtl9310_i2c_select_scl(struct rtl9300_i2c *i2c, u8 scl)
{
	return regmap_field_update_bits(i2c->fields[F_SCL_SEL], BIT(scl), BIT(scl));
}

static int rtl9300_i2c_config_chan(struct rtl9300_i2c *i2c, struct rtl9300_i2c_chan *chan)
{
	const struct rtl9300_i2c_drv_data *drv_data;
	int ret;

	if (i2c->sda_num == chan->sda_num)
		return 0;

	ret = regmap_field_write(i2c->fields[F_SCL_FREQ], chan->bus_freq);
	if (ret)
		return ret;

	drv_data = device_get_match_data(i2c->dev);
	ret = drv_data->select_scl(i2c, i2c->scl_num);
	if (ret)
		return ret;

	ret = regmap_field_update_bits(i2c->fields[F_SDA_SEL], BIT(chan->sda_num),
				       BIT(chan->sda_num));
	if (ret)
		return ret;

	ret = regmap_field_write(i2c->fields[F_SDA_OUT_SEL], chan->sda_num);
	if (ret)
		return ret;

	i2c->sda_num = chan->sda_num;
	return 0;
}

static int rtl9607_i2c_config_chan(struct rtl9300_i2c *i2c, struct rtl9300_i2c_chan *chan)
{
	const struct rtl9300_i2c_drv_data *drv_data;
	int ret;

	if (i2c->sda_num == chan->sda_num)
		return 0;

	ret = regmap_field_write(i2c->fields[F_CLK_DIV], chan->clk_div);
	if (ret)
		return ret;

	drv_data = device_get_match_data(i2c->dev);
	ret = drv_data->select_scl(i2c, i2c->scl_num);
	if (ret)
		return ret;

	i2c->sda_num = chan->sda_num;
	return 0;
}

static void rtl9300_i2c_config_clock(u32 clock_freq, struct rtl9300_i2c_chan *chan)
{
	struct rtl9300_i2c *i2c = chan->i2c;

	switch (clock_freq) {
	case I2C_MAX_STANDARD_MODE_FREQ:
		chan->bus_freq = RTL9300_I2C_STD_FREQ;
		break;
	case I2C_MAX_FAST_MODE_FREQ:
		chan->bus_freq = RTL9300_I2C_FAST_FREQ;
		break;
	case RTL9300_I2C_MAX_SUPER_FAST_FREQ:
		chan->bus_freq = RTL9300_I2C_SUPER_FAST_FREQ;
		break;
	case RTL9300_I2C_MAX_SLOW_FREQ:
		chan->bus_freq = RTL9300_I2C_SLOW_FREQ;
		break;
	default:
		dev_warn(i2c->dev, "SDA%d clock-frequency %d not supported using default\n",
			 chan->sda_num, clock_freq);
		break;
	}
}

static void rtl9607_i2c_config_clock(u32 clock_freq, struct rtl9300_i2c_chan *chan)
{
	struct rtl9300_i2c *i2c = chan->i2c;

	chan->clk_div = clk_get_rate(i2c->clk) / clock_freq - 1;
}

static int rtl9300_i2c_read(struct rtl9300_i2c *i2c, u8 *buf, u8 len)
{
	u32 vals[4] = {};
	int i, ret;

	if (len > 16)
		return -EIO;

	ret = regmap_bulk_read(i2c->regmap, i2c->rd_reg, vals, ARRAY_SIZE(vals));
	if (ret)
		return ret;

	for (i = 0; i < len; i++) {
		buf[i] = vals[i/4] & 0xff;
		vals[i/4] >>= 8;
	}

	return 0;
}

static int rtl9300_i2c_write(struct rtl9300_i2c *i2c, u8 *buf, u8 len)
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

	return regmap_bulk_write(i2c->regmap, i2c->wd_reg, vals, ARRAY_SIZE(vals));
}

static int rtl9300_i2c_writel(struct rtl9300_i2c *i2c, u32 data)
{
	return regmap_write(i2c->regmap, i2c->wd_reg, data);
}

static int rtl9300_i2c_prepare_xfer(struct rtl9300_i2c *i2c, struct rtl9300_i2c_xfer *xfer)
{
	const struct rtl9300_i2c_drv_data *drv_data;
	int ret;

	drv_data = device_get_match_data(i2c->dev);
	if (xfer->data_len < 1 || xfer->data_len > drv_data->max_data_len)
		return -EINVAL;

	ret = regmap_field_write(i2c->fields[F_DEV_ADDR], xfer->dev_addr);
	if (ret)
		return ret;

	ret = rtl9300_i2c_reg_addr_set(i2c, xfer->reg_addr, xfer->reg_addr_len);
	if (ret)
		return ret;

	ret = regmap_field_write(i2c->fields[F_RWOP], xfer->write);
	if (ret)
		return ret;

	ret = regmap_field_write(i2c->fields[F_DATA_WIDTH], (xfer->data_len - 1) & 0xf);
	if (ret)
		return ret;

	if (xfer->write) {
		switch (xfer->type) {
		case RTL9300_I2C_XFER_BYTE:
			ret = rtl9300_i2c_writel(i2c, *xfer->data);
			break;
		case RTL9300_I2C_XFER_WORD:
			ret = rtl9300_i2c_writel(i2c, get_unaligned((const u16 *)xfer->data));
			break;
		default:
			ret = rtl9300_i2c_write(i2c, xfer->data, xfer->data_len);
			break;
		}
	}

	return ret;
}

static int rtl9300_i2c_do_xfer(struct rtl9300_i2c *i2c, struct rtl9300_i2c_xfer *xfer)
{
	u32 val;
	int ret;

	ret = regmap_field_write(i2c->fields[F_I2C_TRIG], 1);
	if (ret)
		return ret;

	ret = regmap_field_read_poll_timeout(i2c->fields[F_BUSY], val, !val, 100, 100000);
	if (ret)
		return ret;

	ret = regmap_field_read(i2c->fields[F_I2C_FAIL], &val);
	if (ret)
		return ret;
	if (val)
		return -EIO;

	if (!xfer->write) {
		switch (xfer->type) {
		case RTL9300_I2C_XFER_BYTE:
			ret = regmap_read(i2c->regmap, i2c->rd_reg, &val);
			if (ret)
				return ret;

			*xfer->data = val & 0xff;
			break;
		case RTL9300_I2C_XFER_WORD:
			ret = regmap_read(i2c->regmap, i2c->rd_reg, &val);
			if (ret)
				return ret;

			put_unaligned(val & 0xffff, (u16*)xfer->data);
			break;
		default:
			ret = rtl9300_i2c_read(i2c, xfer->data, xfer->data_len);
			if (ret)
				return ret;
			break;
		}
	}

	return 0;
}

static int rtl9300_i2c_smbus_xfer(struct i2c_adapter *adap, u16 addr, unsigned short flags,
				  char read_write, u8 command, int size,
				  union i2c_smbus_data *data)
{
	struct rtl9300_i2c_chan *chan = i2c_get_adapdata(adap);
	const struct rtl9300_i2c_drv_data *drv_data;
	struct rtl9300_i2c *i2c = chan->i2c;
	struct rtl9300_i2c_xfer xfer = {0};
	int ret;

	if (addr > 0x7f)
		return -EINVAL;

	guard(rtl9300_i2c)(i2c);

	drv_data = device_get_match_data(i2c->dev);
	ret = drv_data->config_chan(i2c, chan);
	if (ret)
		return ret;

	xfer.dev_addr = addr & 0x7f;
	xfer.write = (read_write == I2C_SMBUS_WRITE);
	xfer.reg_addr = command;
	xfer.reg_addr_len = drv_data->reg_addr_8bit_len;

	switch (size) {
	case I2C_SMBUS_BYTE:
		xfer.data = (read_write == I2C_SMBUS_READ) ? &data->byte : &command;
		xfer.data_len = 1;
		xfer.reg_addr = 0;
		xfer.reg_addr_len = 0;
		xfer.type = RTL9300_I2C_XFER_BYTE;
		break;
	case I2C_SMBUS_BYTE_DATA:
		xfer.data = &data->byte;
		xfer.data_len = 1;
		xfer.type = RTL9300_I2C_XFER_BYTE;
		break;
	case I2C_SMBUS_WORD_DATA:
		xfer.data = (u8 *)&data->word;
		xfer.data_len = 2;
		xfer.type = RTL9300_I2C_XFER_WORD;
		break;
	case I2C_SMBUS_BLOCK_DATA:
		xfer.data = &data->block[0];
		xfer.data_len = data->block[0] + 1;
		xfer.type = RTL9300_I2C_XFER_BLOCK;
		break;
	case I2C_SMBUS_I2C_BLOCK_DATA:
		xfer.data = &data->block[1];
		xfer.data_len = data->block[0];
		xfer.type = RTL9300_I2C_XFER_BLOCK;
		break;
	default:
		dev_err(&adap->dev, "Unsupported transaction %d\n", size);
		return -EOPNOTSUPP;
	}

	ret = rtl9300_i2c_prepare_xfer(i2c, &xfer);
	if (ret)
		return ret;

	return rtl9300_i2c_do_xfer(i2c, &xfer);
}

static u32 rtl9300_i2c_func(struct i2c_adapter *a)
{
	return I2C_FUNC_SMBUS_BYTE | I2C_FUNC_SMBUS_BYTE_DATA |
	       I2C_FUNC_SMBUS_WORD_DATA | I2C_FUNC_SMBUS_BLOCK_DATA |
	       I2C_FUNC_SMBUS_I2C_BLOCK;
}

static const struct i2c_algorithm rtl9300_i2c_algo = {
	.smbus_xfer	= rtl9300_i2c_smbus_xfer,
	.functionality	= rtl9300_i2c_func,
};

static struct i2c_adapter_quirks rtl9300_i2c_quirks = {
	.flags		= I2C_AQ_NO_CLK_STRETCH | I2C_AQ_NO_ZERO_LEN,
	.max_read_len	= 16,
	.max_write_len	= 16,
};

static int rtl9300_i2c_init(struct rtl9300_i2c *i2c)
{
	/* only use standard read format */
	return regmap_field_write(i2c->fields[F_RD_MODE], 0);
}

static int rtl9607_i2c_init(struct rtl9300_i2c *i2c)
{
	return regmap_field_write(i2c->fields[F_EXT_SCK_5MS], 1);
}

static int rtl9300_i2c_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct rtl9300_i2c *i2c;
	const struct rtl9300_i2c_drv_data *drv_data;
	struct reg_field fields[F_NUM_FIELDS];
	u32 clock_freq, scl_num, sda_num;
	int ret, i = 0;

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

	ret = device_property_read_u32(dev, "realtek,scl", &scl_num);
	if (ret || scl_num != 1)
		scl_num = 0;
	i2c->scl_num = (u8)scl_num;

	platform_set_drvdata(pdev, i2c);

	drv_data = device_get_match_data(i2c->dev);
	if (device_get_child_node_count(dev) > drv_data->max_nchan)
		return dev_err_probe(dev, -EINVAL, "Too many channels\n");

	i2c->rd_reg = i2c->reg_base + drv_data->rd_reg;
	i2c->wd_reg = i2c->reg_base + drv_data->wd_reg;
	for (i = 0; i < F_NUM_FIELDS; i++) {
		fields[i] = drv_data->field_desc[i].field;
		if (drv_data->field_desc[i].scope == REG_SCOPE_MASTER)
			fields[i].reg += i2c->reg_base;
	}
	ret = devm_regmap_field_bulk_alloc(dev, i2c->regmap, i2c->fields,
					   fields, F_NUM_FIELDS);
	if (ret)
		return ret;

	i2c->clk = devm_clk_get_optional_enabled(dev, NULL);
	if (IS_ERR(i2c->clk))
		return dev_err_probe(dev, PTR_ERR(i2c->clk), "Failed to enable i2c clock\n");

	i = 0;
	for_each_child_of_node_scoped(dev->of_node, child) {
		struct rtl9300_i2c_chan *chan = &i2c->chans[i];
		struct i2c_adapter *adap = &chan->adap;

		ret = of_property_read_u32(child, "reg", &sda_num);
		if (ret)
			return ret;

		ret = of_property_read_u32(child, "clock-frequency", &clock_freq);
		if (ret)
			clock_freq = I2C_MAX_STANDARD_MODE_FREQ;

		chan->sda_num = sda_num;
		chan->i2c = i2c;

		drv_data->config_clock(clock_freq, chan);

		adap = &i2c->chans[i].adap;
		adap->owner = THIS_MODULE;
		adap->algo = &rtl9300_i2c_algo;
		adap->quirks = &rtl9300_i2c_quirks;
		adap->retries = 3;
		adap->dev.parent = dev;
		i2c_set_adapdata(adap, chan);
		adap->dev.of_node = child;
		snprintf(adap->name, sizeof(adap->name), "%s SDA%d\n", dev_name(dev), sda_num);
		i++;

		ret = devm_i2c_add_adapter(dev, adap);
		if (ret)
			return ret;
	}
	i2c->sda_num = 0xff;

	ret = drv_data->misc_init(i2c);
	if (ret)
		return ret;

	return 0;
}

#define GLB_REG_FIELD(reg, msb, lsb)    \
	{ .field = REG_FIELD(reg, msb, lsb), .scope = REG_SCOPE_GLOBAL }
#define MST_REG_FIELD(reg, msb, lsb)    \
	{ .field = REG_FIELD(reg, msb, lsb), .scope = REG_SCOPE_MASTER }

static const struct rtl9300_i2c_drv_data rtl9300_i2c_drv_data = {
	.field_desc = {
		[F_MEM_ADDR]		= MST_REG_FIELD(RTL9300_I2C_MST_CTRL1, 8, 31),
		[F_SDA_OUT_SEL]		= MST_REG_FIELD(RTL9300_I2C_MST_CTRL1, 4, 6),
		[F_SCL_SEL]		= MST_REG_FIELD(RTL9300_I2C_MST_CTRL1, 3, 3),
		[F_RWOP]		= MST_REG_FIELD(RTL9300_I2C_MST_CTRL1, 2, 2),
		[F_I2C_FAIL]		= MST_REG_FIELD(RTL9300_I2C_MST_CTRL1, 1, 1),
		[F_I2C_TRIG]		= MST_REG_FIELD(RTL9300_I2C_MST_CTRL1, 0, 0),
		[F_RD_MODE]		= MST_REG_FIELD(RTL9300_I2C_MST_CTRL2, 15, 15),
		[F_DEV_ADDR]		= MST_REG_FIELD(RTL9300_I2C_MST_CTRL2, 8, 14),
		[F_DATA_WIDTH]		= MST_REG_FIELD(RTL9300_I2C_MST_CTRL2, 4, 7),
		[F_MEM_ADDR_WIDTH]	= MST_REG_FIELD(RTL9300_I2C_MST_CTRL2, 2, 3),
		[F_SCL_FREQ]		= MST_REG_FIELD(RTL9300_I2C_MST_CTRL2, 0, 1),
		[F_SDA_SEL]		= GLB_REG_FIELD(RTL9300_I2C_MST_GLB_CTRL, 0, 7),
		[F_BUSY]		= MST_REG_FIELD(RTL9300_I2C_MST_CTRL1, 0, 0),
	},
	.select_scl = rtl9300_i2c_select_scl,
	.config_chan = rtl9300_i2c_config_chan,
	.config_clock = rtl9300_i2c_config_clock,
	.misc_init = rtl9300_i2c_init,
	.rd_reg = RTL9300_I2C_MST_DATA_WORD0,
	.wd_reg = RTL9300_I2C_MST_DATA_WORD0,
	.max_nchan = RTL9300_I2C_MUX_NCHAN,
	.max_data_len = RTL9300_I2C_MAX_DATA_LEN,
	.reg_addr_8bit_len = RTL9300_REG_ADDR_8BIT_LEN,
};

static const struct rtl9300_i2c_drv_data rtl9310_i2c_drv_data = {
	.field_desc = {
		[F_SCL_SEL]		= GLB_REG_FIELD(RTL9310_I2C_MST_IF_SEL, 12, 13),
		[F_SDA_SEL]		= GLB_REG_FIELD(RTL9310_I2C_MST_IF_SEL, 0, 11),
		[F_SCL_FREQ]		= MST_REG_FIELD(RTL9310_I2C_MST_CTRL, 30, 31),
		[F_DEV_ADDR]		= MST_REG_FIELD(RTL9310_I2C_MST_CTRL, 11, 17),
		[F_SDA_OUT_SEL]		= MST_REG_FIELD(RTL9310_I2C_MST_CTRL, 18, 21),
		[F_MEM_ADDR_WIDTH]	= MST_REG_FIELD(RTL9310_I2C_MST_CTRL, 9, 10),
		[F_DATA_WIDTH]		= MST_REG_FIELD(RTL9310_I2C_MST_CTRL, 5, 8),
		[F_RD_MODE]		= MST_REG_FIELD(RTL9310_I2C_MST_CTRL, 4, 4),
		[F_RWOP]		= MST_REG_FIELD(RTL9310_I2C_MST_CTRL, 2, 2),
		[F_I2C_FAIL]		= MST_REG_FIELD(RTL9310_I2C_MST_CTRL, 1, 1),
		[F_I2C_TRIG]		= MST_REG_FIELD(RTL9310_I2C_MST_CTRL, 0, 0),
		[F_MEM_ADDR]		= MST_REG_FIELD(RTL9310_I2C_MST_MEMADDR_CTRL, 0, 23),
		[F_BUSY]		= MST_REG_FIELD(RTL9310_I2C_MST_CTRL, 0, 0),
	},
	.select_scl = rtl9310_i2c_select_scl,
	.config_chan = rtl9300_i2c_config_chan,
	.config_clock = rtl9300_i2c_config_clock,
	.misc_init = rtl9300_i2c_init,
	.rd_reg = RTL9310_I2C_MST_DATA_CTRL,
	.wd_reg = RTL9310_I2C_MST_DATA_CTRL,
	.max_nchan = RTL9310_I2C_MUX_NCHAN,
	.max_data_len = RTL9300_I2C_MAX_DATA_LEN,
	.reg_addr_8bit_len = RTL9300_REG_ADDR_8BIT_LEN,
};

static const struct rtl9300_i2c_drv_data rtl9607_i2c_drv_data = {
	.field_desc = {
		[F_SCL_SEL]		= GLB_REG_FIELD(RTL9607_IO_MODE_EN, 13, 14),
		[F_EXT_SCK_5MS]		= MST_REG_FIELD(RTL9607_I2C_CONFIG, 26, 26),
		[F_DEV_ADDR]		= MST_REG_FIELD(RTL9607_I2C_CONFIG, 14, 20),
		[F_MEM_ADDR_WIDTH]	= MST_REG_FIELD(RTL9607_I2C_CONFIG, 12, 13),
		[F_DATA_WIDTH]		= MST_REG_FIELD(RTL9607_I2C_CONFIG, 10, 11),
		[F_CLK_DIV]		= MST_REG_FIELD(RTL9607_I2C_CONFIG, 0, 9),
		[F_I2C_FAIL]		= MST_REG_FIELD(RTL9607_I2C_IND_CMD, 3, 3),
		[F_BUSY]		= MST_REG_FIELD(RTL9607_I2C_IND_CMD, 2, 2),
		[F_RWOP]		= MST_REG_FIELD(RTL9607_I2C_IND_CMD, 1, 1),
		[F_I2C_TRIG]		= MST_REG_FIELD(RTL9607_I2C_IND_CMD, 0, 0),
		[F_MEM_ADDR]		= MST_REG_FIELD(RTL9607_I2C_IND_ADR, 0, 31),
	},
	.select_scl = rtl9310_i2c_select_scl,
	.config_chan = rtl9607_i2c_config_chan,
	.config_clock = rtl9607_i2c_config_clock,
	.misc_init = rtl9607_i2c_init,
	.rd_reg = RTL9607_I2C_IND_RD,
	.wd_reg = RTL9607_I2C_IND_WD,
	.max_nchan = RTL9607_I2C_MUX_NCHAN,
	.max_data_len = RTL9607_I2C_MAX_DATA_LEN,
	.reg_addr_8bit_len = RTL9607_REG_ADDR_8BIT_LEN,
};

static const struct of_device_id i2c_rtl9300_dt_ids[] = {
	{ .compatible = "realtek,rtl9301-i2c", .data = (void *) &rtl9300_i2c_drv_data },
	{ .compatible = "realtek,rtl9302b-i2c", .data = (void *) &rtl9300_i2c_drv_data },
	{ .compatible = "realtek,rtl9302c-i2c", .data = (void *) &rtl9300_i2c_drv_data },
	{ .compatible = "realtek,rtl9303-i2c", .data = (void *) &rtl9300_i2c_drv_data },
	{ .compatible = "realtek,rtl9310-i2c", .data = (void *) &rtl9310_i2c_drv_data },
	{ .compatible = "realtek,rtl9311-i2c", .data = (void *) &rtl9310_i2c_drv_data },
	{ .compatible = "realtek,rtl9312-i2c", .data = (void *) &rtl9310_i2c_drv_data },
	{ .compatible = "realtek,rtl9313-i2c", .data = (void *) &rtl9310_i2c_drv_data },
	{ .compatible = "realtek,rtl9607-i2c", .data = (void *) &rtl9607_i2c_drv_data },
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
