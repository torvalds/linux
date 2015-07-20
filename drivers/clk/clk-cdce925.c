/*
 * Driver for TI Dual PLL CDCE925 clock synthesizer
 *
 * This driver always connects the Y1 to the input clock, Y2/Y3 to PLL1
 * and Y4/Y5 to PLL2. PLL frequency is set on a first-come-first-serve
 * basis. Clients can directly request any frequency that the chip can
 * deliver using the standard clk framework. In addition, the device can
 * be configured and activated via the devicetree.
 *
 * Copyright (C) 2014, Topic Embedded Products
 * Licenced under GPL
 */
#include <linux/clk-provider.h>
#include <linux/delay.h>
#include <linux/module.h>
#include <linux/i2c.h>
#include <linux/regmap.h>
#include <linux/slab.h>
#include <linux/gcd.h>

/* The chip has 2 PLLs which can be routed through dividers to 5 outputs.
 * Model this as 2 PLL clocks which are parents to the outputs.
 */
#define NUMBER_OF_PLLS	2
#define NUMBER_OF_OUTPUTS	5

#define CDCE925_REG_GLOBAL1	0x01
#define CDCE925_REG_Y1SPIPDIVH	0x02
#define CDCE925_REG_PDIVL	0x03
#define CDCE925_REG_XCSEL	0x05
/* PLL parameters start at 0x10, steps of 0x10 */
#define CDCE925_OFFSET_PLL	0x10
/* Add CDCE925_OFFSET_PLL * (pll) to these registers before sending */
#define CDCE925_PLL_MUX_OUTPUTS	0x14
#define CDCE925_PLL_MULDIV	0x18

#define CDCE925_PLL_FREQUENCY_MIN	 80000000ul
#define CDCE925_PLL_FREQUENCY_MAX	230000000ul
struct clk_cdce925_chip;

struct clk_cdce925_output {
	struct clk_hw hw;
	struct clk_cdce925_chip *chip;
	u8 index;
	u16 pdiv; /* 1..127 for Y2-Y5; 1..1023 for Y1 */
};
#define to_clk_cdce925_output(_hw) \
	container_of(_hw, struct clk_cdce925_output, hw)

struct clk_cdce925_pll {
	struct clk_hw hw;
	struct clk_cdce925_chip *chip;
	u8 index;
	u16 m;   /* 1..511 */
	u16 n;   /* 1..4095 */
};
#define to_clk_cdce925_pll(_hw)	container_of(_hw, struct clk_cdce925_pll, hw)

struct clk_cdce925_chip {
	struct regmap *regmap;
	struct i2c_client *i2c_client;
	struct clk_cdce925_pll pll[NUMBER_OF_PLLS];
	struct clk_cdce925_output clk[NUMBER_OF_OUTPUTS];
	struct clk *dt_clk[NUMBER_OF_OUTPUTS];
	struct clk_onecell_data onecell;
};

/* ** ** ** ** ** ** ** ** ** ** ** ** ** ** ** ** ** ** ** ** ** ** */

static unsigned long cdce925_pll_calculate_rate(unsigned long parent_rate,
	u16 n, u16 m)
{
	if ((!m || !n) || (m == n))
		return parent_rate; /* In bypass mode runs at same frequency */
	return mult_frac(parent_rate, (unsigned long)n, (unsigned long)m);
}

static unsigned long cdce925_pll_recalc_rate(struct clk_hw *hw,
		unsigned long parent_rate)
{
	/* Output frequency of PLL is Fout = (Fin/Pdiv)*(N/M) */
	struct clk_cdce925_pll *data = to_clk_cdce925_pll(hw);

	return cdce925_pll_calculate_rate(parent_rate, data->n, data->m);
}

static void cdce925_pll_find_rate(unsigned long rate,
		unsigned long parent_rate, u16 *n, u16 *m)
{
	unsigned long un;
	unsigned long um;
	unsigned long g;

	if (rate <= parent_rate) {
		/* Can always deliver parent_rate in bypass mode */
		rate = parent_rate;
		*n = 0;
		*m = 0;
	} else {
		/* In PLL mode, need to apply min/max range */
		if (rate < CDCE925_PLL_FREQUENCY_MIN)
			rate = CDCE925_PLL_FREQUENCY_MIN;
		else if (rate > CDCE925_PLL_FREQUENCY_MAX)
			rate = CDCE925_PLL_FREQUENCY_MAX;

		g = gcd(rate, parent_rate);
		um = parent_rate / g;
		un = rate / g;
		/* When outside hw range, reduce to fit (rounding errors) */
		while ((un > 4095) || (um > 511)) {
			un >>= 1;
			um >>= 1;
		}
		if (un == 0)
			un = 1;
		if (um == 0)
			um = 1;

		*n = un;
		*m = um;
	}
}

static long cdce925_pll_round_rate(struct clk_hw *hw, unsigned long rate,
		unsigned long *parent_rate)
{
	u16 n, m;

	cdce925_pll_find_rate(rate, *parent_rate, &n, &m);
	return (long)cdce925_pll_calculate_rate(*parent_rate, n, m);
}

static int cdce925_pll_set_rate(struct clk_hw *hw, unsigned long rate,
		unsigned long parent_rate)
{
	struct clk_cdce925_pll *data = to_clk_cdce925_pll(hw);

	if (!rate || (rate == parent_rate)) {
		data->m = 0; /* Bypass mode */
		data->n = 0;
		return 0;
	}

	if ((rate < CDCE925_PLL_FREQUENCY_MIN) ||
		(rate > CDCE925_PLL_FREQUENCY_MAX)) {
		pr_debug("%s: rate %lu outside PLL range.\n", __func__, rate);
		return -EINVAL;
	}

	if (rate < parent_rate) {
		pr_debug("%s: rate %lu less than parent rate %lu.\n", __func__,
			rate, parent_rate);
		return -EINVAL;
	}

	cdce925_pll_find_rate(rate, parent_rate, &data->n, &data->m);
	return 0;
}


/* calculate p = max(0, 4 - int(log2 (n/m))) */
static u8 cdce925_pll_calc_p(u16 n, u16 m)
{
	u8 p;
	u16 r = n / m;

	if (r >= 16)
		return 0;
	p = 4;
	while (r > 1) {
		r >>= 1;
		--p;
	}
	return p;
}

/* Returns VCO range bits for VCO1_0_RANGE */
static u8 cdce925_pll_calc_range_bits(struct clk_hw *hw, u16 n, u16 m)
{
	struct clk *parent = clk_get_parent(hw->clk);
	unsigned long rate = clk_get_rate(parent);

	rate = mult_frac(rate, (unsigned long)n, (unsigned long)m);
	if (rate >= 175000000)
		return 0x3;
	if (rate >= 150000000)
		return 0x02;
	if (rate >= 125000000)
		return 0x01;
	return 0x00;
}

/* I2C clock, hence everything must happen in (un)prepare because this
 * may sleep */
static int cdce925_pll_prepare(struct clk_hw *hw)
{
	struct clk_cdce925_pll *data = to_clk_cdce925_pll(hw);
	u16 n = data->n;
	u16 m = data->m;
	u16 r;
	u8 q;
	u8 p;
	u16 nn;
	u8 pll[4]; /* Bits are spread out over 4 byte registers */
	u8 reg_ofs = data->index * CDCE925_OFFSET_PLL;
	unsigned i;

	if ((!m || !n) || (m == n)) {
		/* Set PLL mux to bypass mode, leave the rest as is */
		regmap_update_bits(data->chip->regmap,
			reg_ofs + CDCE925_PLL_MUX_OUTPUTS, 0x80, 0x80);
	} else {
		/* According to data sheet: */
		/* p = max(0, 4 - int(log2 (n/m))) */
		p = cdce925_pll_calc_p(n, m);
		/* nn = n * 2^p */
		nn = n * BIT(p);
		/* q = int(nn/m) */
		q = nn / m;
		if ((q < 16) || (1 > 64)) {
			pr_debug("%s invalid q=%d\n", __func__, q);
			return -EINVAL;
		}
		r = nn - (m*q);
		if (r > 511) {
			pr_debug("%s invalid r=%d\n", __func__, r);
			return -EINVAL;
		}
		pr_debug("%s n=%d m=%d p=%d q=%d r=%d\n", __func__,
			n, m, p, q, r);
		/* encode into register bits */
		pll[0] = n >> 4;
		pll[1] = ((n & 0x0F) << 4) | ((r >> 5) & 0x0F);
		pll[2] = ((r & 0x1F) << 3) | ((q >> 3) & 0x07);
		pll[3] = ((q & 0x07) << 5) | (p << 2) |
				cdce925_pll_calc_range_bits(hw, n, m);
		/* Write to registers */
		for (i = 0; i < ARRAY_SIZE(pll); ++i)
			regmap_write(data->chip->regmap,
				reg_ofs + CDCE925_PLL_MULDIV + i, pll[i]);
		/* Enable PLL */
		regmap_update_bits(data->chip->regmap,
			reg_ofs + CDCE925_PLL_MUX_OUTPUTS, 0x80, 0x00);
	}

	return 0;
}

static void cdce925_pll_unprepare(struct clk_hw *hw)
{
	struct clk_cdce925_pll *data = to_clk_cdce925_pll(hw);
	u8 reg_ofs = data->index * CDCE925_OFFSET_PLL;

	regmap_update_bits(data->chip->regmap,
			reg_ofs + CDCE925_PLL_MUX_OUTPUTS, 0x80, 0x80);
}

static const struct clk_ops cdce925_pll_ops = {
	.prepare = cdce925_pll_prepare,
	.unprepare = cdce925_pll_unprepare,
	.recalc_rate = cdce925_pll_recalc_rate,
	.round_rate = cdce925_pll_round_rate,
	.set_rate = cdce925_pll_set_rate,
};


static void cdce925_clk_set_pdiv(struct clk_cdce925_output *data, u16 pdiv)
{
	switch (data->index) {
	case 0:
		regmap_update_bits(data->chip->regmap,
			CDCE925_REG_Y1SPIPDIVH,
			0x03, (pdiv >> 8) & 0x03);
		regmap_write(data->chip->regmap, 0x03, pdiv & 0xFF);
		break;
	case 1:
		regmap_update_bits(data->chip->regmap, 0x16, 0x7F, pdiv);
		break;
	case 2:
		regmap_update_bits(data->chip->regmap, 0x17, 0x7F, pdiv);
		break;
	case 3:
		regmap_update_bits(data->chip->regmap, 0x26, 0x7F, pdiv);
		break;
	case 4:
		regmap_update_bits(data->chip->regmap, 0x27, 0x7F, pdiv);
		break;
	}
}

static void cdce925_clk_activate(struct clk_cdce925_output *data)
{
	switch (data->index) {
	case 0:
		regmap_update_bits(data->chip->regmap,
			CDCE925_REG_Y1SPIPDIVH, 0x0c, 0x0c);
		break;
	case 1:
	case 2:
		regmap_update_bits(data->chip->regmap, 0x14, 0x03, 0x03);
		break;
	case 3:
	case 4:
		regmap_update_bits(data->chip->regmap, 0x24, 0x03, 0x03);
		break;
	}
}

static int cdce925_clk_prepare(struct clk_hw *hw)
{
	struct clk_cdce925_output *data = to_clk_cdce925_output(hw);

	cdce925_clk_set_pdiv(data, data->pdiv);
	cdce925_clk_activate(data);
	return 0;
}

static void cdce925_clk_unprepare(struct clk_hw *hw)
{
	struct clk_cdce925_output *data = to_clk_cdce925_output(hw);

	/* Disable clock by setting divider to "0" */
	cdce925_clk_set_pdiv(data, 0);
}

static unsigned long cdce925_clk_recalc_rate(struct clk_hw *hw,
		unsigned long parent_rate)
{
	struct clk_cdce925_output *data = to_clk_cdce925_output(hw);

	if (data->pdiv)
		return parent_rate / data->pdiv;
	return 0;
}

static u16 cdce925_calc_divider(unsigned long rate,
		unsigned long parent_rate)
{
	unsigned long divider;

	if (!rate)
		return 0;
	if (rate >= parent_rate)
		return 1;

	divider = DIV_ROUND_CLOSEST(parent_rate, rate);
	if (divider > 0x7F)
		divider = 0x7F;

	return (u16)divider;
}

static unsigned long cdce925_clk_best_parent_rate(
	struct clk_hw *hw, unsigned long rate)
{
	struct clk *pll = clk_get_parent(hw->clk);
	struct clk *root = clk_get_parent(pll);
	unsigned long root_rate = clk_get_rate(root);
	unsigned long best_rate_error = rate;
	u16 pdiv_min;
	u16 pdiv_max;
	u16 pdiv_best;
	u16 pdiv_now;

	if (root_rate % rate == 0)
		return root_rate; /* Don't need the PLL, use bypass */

	pdiv_min = (u16)max(1ul, DIV_ROUND_UP(CDCE925_PLL_FREQUENCY_MIN, rate));
	pdiv_max = (u16)min(127ul, CDCE925_PLL_FREQUENCY_MAX / rate);

	if (pdiv_min > pdiv_max)
		return 0; /* No can do? */

	pdiv_best = pdiv_min;
	for (pdiv_now = pdiv_min; pdiv_now < pdiv_max; ++pdiv_now) {
		unsigned long target_rate = rate * pdiv_now;
		long pll_rate = clk_round_rate(pll, target_rate);
		unsigned long actual_rate;
		unsigned long rate_error;

		if (pll_rate <= 0)
			continue;
		actual_rate = pll_rate / pdiv_now;
		rate_error = abs((long)actual_rate - (long)rate);
		if (rate_error < best_rate_error) {
			pdiv_best = pdiv_now;
			best_rate_error = rate_error;
		}
		/* TODO: Consider PLL frequency based on smaller n/m values
		 * and pick the better one if the error is equal */
	}

	return rate * pdiv_best;
}

static long cdce925_clk_round_rate(struct clk_hw *hw, unsigned long rate,
		unsigned long *parent_rate)
{
	unsigned long l_parent_rate = *parent_rate;
	u16 divider = cdce925_calc_divider(rate, l_parent_rate);

	if (l_parent_rate / divider != rate) {
		l_parent_rate = cdce925_clk_best_parent_rate(hw, rate);
		divider = cdce925_calc_divider(rate, l_parent_rate);
		*parent_rate = l_parent_rate;
	}

	if (divider)
		return (long)(l_parent_rate / divider);
	return 0;
}

static int cdce925_clk_set_rate(struct clk_hw *hw, unsigned long rate,
		unsigned long parent_rate)
{
	struct clk_cdce925_output *data = to_clk_cdce925_output(hw);

	data->pdiv = cdce925_calc_divider(rate, parent_rate);

	return 0;
}

static const struct clk_ops cdce925_clk_ops = {
	.prepare = cdce925_clk_prepare,
	.unprepare = cdce925_clk_unprepare,
	.recalc_rate = cdce925_clk_recalc_rate,
	.round_rate = cdce925_clk_round_rate,
	.set_rate = cdce925_clk_set_rate,
};


static u16 cdce925_y1_calc_divider(unsigned long rate,
		unsigned long parent_rate)
{
	unsigned long divider;

	if (!rate)
		return 0;
	if (rate >= parent_rate)
		return 1;

	divider = DIV_ROUND_CLOSEST(parent_rate, rate);
	if (divider > 0x3FF) /* Y1 has 10-bit divider */
		divider = 0x3FF;

	return (u16)divider;
}

static long cdce925_clk_y1_round_rate(struct clk_hw *hw, unsigned long rate,
		unsigned long *parent_rate)
{
	unsigned long l_parent_rate = *parent_rate;
	u16 divider = cdce925_y1_calc_divider(rate, l_parent_rate);

	if (divider)
		return (long)(l_parent_rate / divider);
	return 0;
}

static int cdce925_clk_y1_set_rate(struct clk_hw *hw, unsigned long rate,
		unsigned long parent_rate)
{
	struct clk_cdce925_output *data = to_clk_cdce925_output(hw);

	data->pdiv = cdce925_y1_calc_divider(rate, parent_rate);

	return 0;
}

static const struct clk_ops cdce925_clk_y1_ops = {
	.prepare = cdce925_clk_prepare,
	.unprepare = cdce925_clk_unprepare,
	.recalc_rate = cdce925_clk_recalc_rate,
	.round_rate = cdce925_clk_y1_round_rate,
	.set_rate = cdce925_clk_y1_set_rate,
};


static struct regmap_config cdce925_regmap_config = {
	.name = "configuration0",
	.reg_bits = 8,
	.val_bits = 8,
	.cache_type = REGCACHE_RBTREE,
	.max_register = 0x2F,
};

#define CDCE925_I2C_COMMAND_BLOCK_TRANSFER	0x00
#define CDCE925_I2C_COMMAND_BYTE_TRANSFER	0x80

static int cdce925_regmap_i2c_write(
	void *context, const void *data, size_t count)
{
	struct device *dev = context;
	struct i2c_client *i2c = to_i2c_client(dev);
	int ret;
	u8 reg_data[2];

	if (count != 2)
		return -ENOTSUPP;

	/* First byte is command code */
	reg_data[0] = CDCE925_I2C_COMMAND_BYTE_TRANSFER | ((u8 *)data)[0];
	reg_data[1] = ((u8 *)data)[1];

	dev_dbg(&i2c->dev, "%s(%zu) %#x %#x\n", __func__, count,
			reg_data[0], reg_data[1]);

	ret = i2c_master_send(i2c, reg_data, count);
	if (likely(ret == count))
		return 0;
	else if (ret < 0)
		return ret;
	else
		return -EIO;
}

static int cdce925_regmap_i2c_read(void *context,
	   const void *reg, size_t reg_size, void *val, size_t val_size)
{
	struct device *dev = context;
	struct i2c_client *i2c = to_i2c_client(dev);
	struct i2c_msg xfer[2];
	int ret;
	u8 reg_data[2];

	if (reg_size != 1)
		return -ENOTSUPP;

	xfer[0].addr = i2c->addr;
	xfer[0].flags = 0;
	xfer[0].buf = reg_data;
	if (val_size == 1) {
		reg_data[0] =
			CDCE925_I2C_COMMAND_BYTE_TRANSFER | ((u8 *)reg)[0];
		xfer[0].len = 1;
	} else {
		reg_data[0] =
			CDCE925_I2C_COMMAND_BLOCK_TRANSFER | ((u8 *)reg)[0];
		reg_data[1] = val_size;
		xfer[0].len = 2;
	}

	xfer[1].addr = i2c->addr;
	xfer[1].flags = I2C_M_RD;
	xfer[1].len = val_size;
	xfer[1].buf = val;

	ret = i2c_transfer(i2c->adapter, xfer, 2);
	if (likely(ret == 2)) {
		dev_dbg(&i2c->dev, "%s(%zu, %zu) %#x %#x\n", __func__,
				reg_size, val_size, reg_data[0], *((u8 *)val));
		return 0;
	} else if (ret < 0)
		return ret;
	else
		return -EIO;
}

/* The CDCE925 uses a funky way to read/write registers. Bulk mode is
 * just weird, so just use the single byte mode exclusively. */
static struct regmap_bus regmap_cdce925_bus = {
	.write = cdce925_regmap_i2c_write,
	.read = cdce925_regmap_i2c_read,
};

static int cdce925_probe(struct i2c_client *client,
		const struct i2c_device_id *id)
{
	struct clk_cdce925_chip *data;
	struct device_node *node = client->dev.of_node;
	const char *parent_name;
	const char *pll_clk_name[NUMBER_OF_PLLS] = {NULL,};
	struct clk_init_data init;
	struct clk *clk;
	u32 value;
	int i;
	int err;
	struct device_node *np_output;
	char child_name[6];

	dev_dbg(&client->dev, "%s\n", __func__);
	data = devm_kzalloc(&client->dev, sizeof(*data), GFP_KERNEL);
	if (!data)
		return -ENOMEM;

	data->i2c_client = client;
	data->regmap = devm_regmap_init(&client->dev, &regmap_cdce925_bus,
			&client->dev, &cdce925_regmap_config);
	if (IS_ERR(data->regmap)) {
		dev_err(&client->dev, "failed to allocate register map\n");
		return PTR_ERR(data->regmap);
	}
	i2c_set_clientdata(client, data);

	parent_name = of_clk_get_parent_name(node, 0);
	if (!parent_name) {
		dev_err(&client->dev, "missing parent clock\n");
		return -ENODEV;
	}
	dev_dbg(&client->dev, "parent is: %s\n", parent_name);

	if (of_property_read_u32(node, "xtal-load-pf", &value) == 0)
		regmap_write(data->regmap,
			CDCE925_REG_XCSEL, (value << 3) & 0xF8);
	/* PWDN bit */
	regmap_update_bits(data->regmap, CDCE925_REG_GLOBAL1, BIT(4), 0);

	/* Set input source for Y1 to be the XTAL */
	regmap_update_bits(data->regmap, 0x02, BIT(7), 0);

	init.ops = &cdce925_pll_ops;
	init.flags = 0;
	init.parent_names = &parent_name;
	init.num_parents = parent_name ? 1 : 0;

	/* Register PLL clocks */
	for (i = 0; i < NUMBER_OF_PLLS; ++i) {
		pll_clk_name[i] = kasprintf(GFP_KERNEL, "%s.pll%d",
			client->dev.of_node->name, i);
		init.name = pll_clk_name[i];
		data->pll[i].chip = data;
		data->pll[i].hw.init = &init;
		data->pll[i].index = i;
		clk = devm_clk_register(&client->dev, &data->pll[i].hw);
		if (IS_ERR(clk)) {
			dev_err(&client->dev, "Failed register PLL %d\n", i);
			err = PTR_ERR(clk);
			goto error;
		}
		sprintf(child_name, "PLL%d", i+1);
		np_output = of_get_child_by_name(node, child_name);
		if (!np_output)
			continue;
		if (!of_property_read_u32(np_output,
			"clock-frequency", &value)) {
			err = clk_set_rate(clk, value);
			if (err)
				dev_err(&client->dev,
					"unable to set PLL frequency %ud\n",
					value);
		}
		if (!of_property_read_u32(np_output,
			"spread-spectrum", &value)) {
			u8 flag = of_property_read_bool(np_output,
				"spread-spectrum-center") ? 0x80 : 0x00;
			regmap_update_bits(data->regmap,
				0x16 + (i*CDCE925_OFFSET_PLL),
				0x80, flag);
			regmap_update_bits(data->regmap,
				0x12 + (i*CDCE925_OFFSET_PLL),
				0x07, value & 0x07);
		}
	}

	/* Register output clock Y1 */
	init.ops = &cdce925_clk_y1_ops;
	init.flags = 0;
	init.num_parents = 1;
	init.parent_names = &parent_name; /* Mux Y1 to input */
	init.name = kasprintf(GFP_KERNEL, "%s.Y1", client->dev.of_node->name);
	data->clk[0].chip = data;
	data->clk[0].hw.init = &init;
	data->clk[0].index = 0;
	data->clk[0].pdiv = 1;
	clk = devm_clk_register(&client->dev, &data->clk[0].hw);
	kfree(init.name); /* clock framework made a copy of the name */
	if (IS_ERR(clk)) {
		dev_err(&client->dev, "clock registration Y1 failed\n");
		err = PTR_ERR(clk);
		goto error;
	}
	data->dt_clk[0] = clk;

	/* Register output clocks Y2 .. Y5*/
	init.ops = &cdce925_clk_ops;
	init.flags = CLK_SET_RATE_PARENT;
	init.num_parents = 1;
	for (i = 1; i < NUMBER_OF_OUTPUTS; ++i) {
		init.name = kasprintf(GFP_KERNEL, "%s.Y%d",
			client->dev.of_node->name, i+1);
		data->clk[i].chip = data;
		data->clk[i].hw.init = &init;
		data->clk[i].index = i;
		data->clk[i].pdiv = 1;
		switch (i) {
		case 1:
		case 2:
			/* Mux Y2/3 to PLL1 */
			init.parent_names = &pll_clk_name[0];
			break;
		case 3:
		case 4:
			/* Mux Y4/5 to PLL2 */
			init.parent_names = &pll_clk_name[1];
			break;
		}
		clk = devm_clk_register(&client->dev, &data->clk[i].hw);
		kfree(init.name); /* clock framework made a copy of the name */
		if (IS_ERR(clk)) {
			dev_err(&client->dev, "clock registration failed\n");
			err = PTR_ERR(clk);
			goto error;
		}
		data->dt_clk[i] = clk;
	}

	/* Register the output clocks */
	data->onecell.clk_num = NUMBER_OF_OUTPUTS;
	data->onecell.clks = data->dt_clk;
	err = of_clk_add_provider(client->dev.of_node, of_clk_src_onecell_get,
		&data->onecell);
	if (err)
		dev_err(&client->dev, "unable to add OF clock provider\n");

	err = 0;

error:
	for (i = 0; i < NUMBER_OF_PLLS; ++i)
		/* clock framework made a copy of the name */
		kfree(pll_clk_name[i]);

	return err;
}

static const struct i2c_device_id cdce925_id[] = {
	{ "cdce925", 0 },
	{ }
};
MODULE_DEVICE_TABLE(i2c, cdce925_id);

static const struct of_device_id clk_cdce925_of_match[] = {
	{ .compatible = "ti,cdce925" },
	{ },
};
MODULE_DEVICE_TABLE(of, clk_cdce925_of_match);

static struct i2c_driver cdce925_driver = {
	.driver = {
		.name = "cdce925",
		.of_match_table = of_match_ptr(clk_cdce925_of_match),
	},
	.probe		= cdce925_probe,
	.id_table	= cdce925_id,
};
module_i2c_driver(cdce925_driver);

MODULE_AUTHOR("Mike Looijmans <mike.looijmans@topic.nl>");
MODULE_DESCRIPTION("cdce925 driver");
MODULE_LICENSE("GPL");
