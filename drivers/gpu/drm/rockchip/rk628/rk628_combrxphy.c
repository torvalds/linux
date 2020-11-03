// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2020 Rockchip Electronics Co. Ltd.
 *
 * Author: Algea Cao <algea.cao@rock-chips.com>
 */

#include <linux/kernel.h>
#include <linux/clk.h>
#include <linux/delay.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/of_device.h>
#include <linux/platform_device.h>
#include <linux/reset.h>
#include <linux/phy/phy.h>
#include <linux/mfd/rk628.h>

#define REG(x)					((x) + 0x10000)
#define COMBRXPHY_MAX_REGISTER			REG(0x6780)

struct rk628_combrxphy {
	struct device *dev;
	struct rk628 *parent;
	struct regmap *regmap;
	struct clk *pclk;
	struct reset_control *rstc;
	enum phy_mode mode;
};

#define MAX_ROUND	12
#define MAX_DATA_NUM	16
#define MAX_CHANNEL	3

static void rk628_combrxphy_set_data_of_round(struct rk628_combrxphy
					      *combrxphy, u32 *data,
					      u32 *data_in)
{
	data_in[0] = data[0];
	data_in[1] = data[7];
	data_in[2] = data[13];
	data_in[3] = data[14];
	data_in[4] = data[15];
	data_in[5] = data[1];
	data_in[6] = data[2];
	data_in[7] = data[3];
	data_in[8] = data[4];
	data_in[9] = data[5];
	data_in[10] = data[6];
	data_in[11] = data[8];
	data_in[12] = data[9];
	data_in[13] = data[10];
	data_in[14] = data[11];
	data_in[15] = data[12];
}

static void
rk628_combrxphy_max_zero_of_round(struct rk628_combrxphy *combrxphy,
				  u32 *data_in, u32 *max_zero, u32 *max_val,
				  int n, int ch)
{
	int i;
	int cnt = 0;
	int max_cnt = 0;
	u32 max_v = 0;

	dev_dbg(combrxphy->dev, "%s channel:%d, round:%d ==============\n",
		 __func__, ch, n);
	for (i = 0; i < MAX_DATA_NUM; i++) {
		dev_dbg(combrxphy->dev, "0x%02x ", data_in[i]);
		if ((i + 1) % MAX_ROUND == 0)
			dev_dbg(combrxphy->dev, "\n");
	}

	for (i = 0; i < MAX_DATA_NUM; i++) {
		if (max_v < data_in[i])
			max_v = data_in[i];
	}

	for (i = 0; i < MAX_DATA_NUM; i++) {
		if (data_in[i] == 0)
			cnt = cnt + 200;
		else if (data_in[i] > 0 && data_in[i] < 100)
			cnt = cnt + 100 - data_in[i];
	}
	max_cnt = cnt >= 3200 ? 0 : cnt;

	max_zero[n] = max_cnt;
	max_val[n] = max_v;
	dev_dbg(combrxphy->dev, "channel:%d,round:%d,max_zero_cnt:%d,max_val:%#x",
		ch, n, max_zero[n], max_val[n]);
}

static int
rk628_combrxphy_chose_round_for_ch(struct rk628_combrxphy *combrxphy,
				   u32 *rd_max_zero,
				   u32 *rd_max_val, int ch,
				   int min_round, int max_round)
{
	int i, rd = 0;
	u32 max = 0;
	u32 max_v = 0;

	dev_dbg(combrxphy->dev, "%s channel:%d=============\n", __func__, ch);
	for (i = min_round; i < max_round; i++) {
		dev_dbg(combrxphy->dev, "0x%02x ", rd_max_zero[i]);
		if ((i + 1) % max_round == 0)
			dev_dbg(combrxphy->dev, "\n");
	}
	dev_dbg(combrxphy->dev, "\n");
	for (i = min_round; i < max_round; i++) {
		dev_dbg(combrxphy->dev, "0x%02x ", rd_max_val[i]);
		if ((i + 1) % max_round == 0)
			dev_dbg(combrxphy->dev, "\n");
	}

	for (i = min_round; i < max_round; i++) {
		if (rd_max_zero[i] > max) {
			max = rd_max_zero[i];
			max_v = rd_max_val[i];
			rd = i;
		} else if (rd_max_zero[i] == max && rd_max_val[i] > max_v) {
			max = rd_max_zero[i];
			max_v = rd_max_val[i];
			rd = i;
		}
	}

	dev_dbg(combrxphy->dev, "%s channel:%d, rd:%d\n", __func__, ch, rd);
	return rd;
}

static void rk628_combrxphy_get_data_of_round(struct rk628_combrxphy
					      *combrxphy, u32 *data)
{
	u32 i;

	for (i = 0; i < MAX_DATA_NUM; i++)
		regmap_read(combrxphy->regmap, REG(0x6740 + i * 4), &data[i]);
}

static void
rk628_combrxphy_set_dc_gain(struct rk628_combrxphy *combrxphy,
			    u32 x, u32 y, u32 z)
{
	u32 val;
	u32 dc_gain_ch0, dc_gain_ch1, dc_gain_ch2;

	dev_dbg(combrxphy->dev, "channel dc gain x:%d, y:%d, z:%d", x, y, z);

	dc_gain_ch0 = x & 0xf;
	dc_gain_ch1 = y & 0xf;
	dc_gain_ch2 = z & 0xf;
	regmap_read(combrxphy->regmap, REG(0x661c), &val);

	val = (val & 0xff0f0f0f) | (dc_gain_ch0 << 20) | (dc_gain_ch1 << 12) |
		(dc_gain_ch2 << 4);
	regmap_write(combrxphy->regmap, REG(0x661c), val);
}

static void rk628_combrxphy_set_sample_edge_round(struct rk628_combrxphy
		*combrxphy, u32 x, u32 y, u32 z)
{
	u32 val;
	u32 lsb_ch0;
	u32 lsb_ch1;
	u32 lsb_ch2;

	dev_dbg(combrxphy->dev, "channel sample edge x:%d, y:%d, z:%d",
		x, y, z);

	lsb_ch0 = (x & 0xf);
	lsb_ch1 = (y & 0xf);
	lsb_ch2 = (z & 0xf);

	regmap_read(combrxphy->regmap, REG(0x6618), &val);

	val = (val & 0xff00f0ff) | (lsb_ch1 << 20) | (lsb_ch0 << 16) |
		(lsb_ch2 << 8);
	regmap_write(combrxphy->regmap, REG(0x6618), val);
}

static void rk628_combrxphy_start_sample_edge(struct rk628_combrxphy *combrxphy)
{
	u32 val;

	regmap_read(combrxphy->regmap, REG(0x66f0), &val);
	val &= 0xfffff1ff;
	regmap_write(combrxphy->regmap, REG(0x66f0), val);
	regmap_read(combrxphy->regmap, REG(0x66f0), &val);
	val = (val & 0xfffff1ff) | (0x7 << 9);
	regmap_write(combrxphy->regmap, REG(0x66f0), val);
}

static void
rk628_combrxphy_set_sample_edge_mode(struct rk628_combrxphy *combrxphy,
				     int ch)
{
	u32 val;

	dev_dbg(combrxphy->dev, "%s in!", __func__);
	regmap_read(combrxphy->regmap, REG(0x6634), &val);
	dev_dbg(combrxphy->dev, "%s read val:%#x!", __func__, val);
	val = val & (~(0xf << ((ch + 1) * 4)));
	dev_dbg(combrxphy->dev, "%s write val:%#x!", __func__, val);
	regmap_write(combrxphy->regmap, REG(0x6634), val);
	dev_dbg(combrxphy->dev, "%s out!", __func__);
}

static void rk628_combrxphy_select_channel(struct rk628_combrxphy *combrxphy,
					  int ch)
{
	u32 val;

	regmap_read(combrxphy->regmap, REG(0x6700), &val);
	val = (val & 0xfffffffc) | (ch & 0x3);
	regmap_write(combrxphy->regmap, REG(0x6700), val);
}

static void rk628_combrxphy_cfg_6730(struct rk628_combrxphy *combrxphy)
{
	u32 val;

	regmap_read(combrxphy->regmap, REG(0x6730), &val);
	val = (val & 0xffff0000) | 0x1;
	regmap_write(combrxphy->regmap, REG(0x6730), val);
}

static void
rk628_combrxphy_sample_edge_procedure(struct rk628_combrxphy *combrxphy,
				      int f, int min_round,
				      int max_round)
{
	int n, ch;
	u32 data[MAX_DATA_NUM];
	u32 data_in[MAX_DATA_NUM];
	u32 round_max_zero[MAX_CHANNEL][max_round];
	u32 round_max_value[MAX_CHANNEL][max_round];
	u32 ch_round[MAX_CHANNEL];
	u32 edge, dc_gain;

	dev_dbg(combrxphy->dev, "%s in!", __func__);
	/* Step1: set sample edge mode for channel 0~2 */
	for (ch = 0; ch < MAX_CHANNEL; ch++)
		rk628_combrxphy_set_sample_edge_mode(combrxphy, ch);

	dev_dbg(combrxphy->dev, "step1 set sample edge mode ok!");

	/* step2: once per round */
	for (ch = 0; ch < MAX_CHANNEL; ch++) {
		rk628_combrxphy_select_channel(combrxphy, ch);
		rk628_combrxphy_cfg_6730(combrxphy);
	}
	dev_dbg(combrxphy->dev, "step2 once per round ok!");

	/*
	 * step3:config sample edge until the end of one frame
	 * (for example 1080p:2200*1125=32’h25c3f8)
	 */
	switch (f) {
	case 27000:
		edge = 858 * 525;
		dc_gain = 0;
		break;
	case 64000:
		edge = 1317 * 810;
		dc_gain = 0;
		break;
	case 74250:
		edge = 1650 * 750;
		dc_gain = 0;
		break;
	case 148500:
		edge = 2200 * 1125;
		dc_gain = 1;
		break;
	case 297000:
		dc_gain = 3;
		edge = 4400 * 2250;
		break;
	case 594000:
		dc_gain = 0xf;
		edge = 4400 * 2250;
		break;
	default:
		edge = 2200 * 1125;
		dc_gain = 1;
		break;
	}
	dev_dbg(combrxphy->dev, "===>>> f:%d, edge:%#x", f, edge);
	for (ch = 0; ch < MAX_CHANNEL; ch++) {
		rk628_combrxphy_select_channel(combrxphy, ch);
		regmap_write(combrxphy->regmap, REG(0x6708), edge);
	}
	dev_dbg(combrxphy->dev, "step3 cfg sample edge ok!");

	rk628_combrxphy_set_dc_gain(combrxphy, dc_gain, dc_gain, dc_gain);

	for (n = min_round; n < max_round; n++) {
		/* step4:set sample edge round value n,n=0(n=0~31) */
		rk628_combrxphy_set_sample_edge_round(combrxphy, n, n, n);
		dev_dbg(combrxphy->dev, "step4 ok!");
		/* step5:start sample edge */
		rk628_combrxphy_start_sample_edge(combrxphy);
		dev_dbg(combrxphy->dev, "step5 ok!");
		/* step6:waiting more than one frame time */
		usleep_range(40*1000, 41*1000);
		for (ch = 0; ch < MAX_CHANNEL; ch++) {
			/* step7:get data of round n */
			rk628_combrxphy_select_channel(combrxphy, ch);
			dev_dbg(combrxphy->dev, "step7 set ch ok!");
			rk628_combrxphy_get_data_of_round(combrxphy, data);
			dev_dbg(combrxphy->dev, "step7 get data ok!");
			rk628_combrxphy_set_data_of_round(combrxphy, data,
							  data_in);
			dev_dbg(combrxphy->dev, "step7 set data ok!");
			rk628_combrxphy_max_zero_of_round(combrxphy, data_in,
							  round_max_zero[ch],
							  round_max_value[ch],
							  n, ch);
		}
	}
	for (ch = 0; ch < MAX_CHANNEL; ch++)
		ch_round[ch] =
			rk628_combrxphy_chose_round_for_ch(combrxphy,
							   round_max_zero[ch],
							   round_max_value[ch],
							   ch, min_round,
							   max_round);

	/*
	 * step8:after finish round 31, get the max constant value and
	 * corresponding value n.
	 * write result to sample edge round value.
	 */
	rk628_combrxphy_set_sample_edge_round(combrxphy, ch_round[0],
					      ch_round[1], ch_round[2]);

	/* do step5, step6 again */
	dev_dbg(combrxphy->dev, "do step5 step6 again!");
	rk628_combrxphy_start_sample_edge(combrxphy);
	usleep_range(40*1000, 41*1000);
}

static int rk628_combrxphy_set_hdmi_mode(struct rk628_combrxphy *combrxphy,
					 int bus_width)
{
	u32 val, data_a, data_b, f, val2 = 0;
	int i, ret, count;
	int pll_man, max_round, min_round;
	bool is_yuv420;

	is_yuv420 = bus_width & BIT(30);

	if (is_yuv420)
		f = (bus_width & 0xffffff) / 2;
	else
		f = bus_width & 0xffffff;

	dev_dbg(combrxphy->dev, "f:%d\n", f);

	regmap_read(combrxphy->regmap, REG(0x6630), &val);
	val &= ~BIT(23);
	val |= 0x18;
	regmap_write(combrxphy->regmap, REG(0x6630), val);

	/* enable cal */
	regmap_read(combrxphy->regmap, REG(0x6610), &val);
	val |= 0x18000000;
	regmap_write(combrxphy->regmap, REG(0x6610), val);

	usleep_range(10*1000, 11*1000);
	/* disable cal */
	val &= ~BIT(28);
	val |= BIT(27);
	regmap_write(combrxphy->regmap, REG(0x6610), val);

	/* save cal val */
	regmap_read(combrxphy->regmap, REG(0x6614), &val);
	if (!(val & 0x3f00)) {
		dev_err(combrxphy->dev, "resistor error\n");
		return -EINVAL;
	}

	val &= 0x3f00;
	val = val >> 8;
	val2 |= 0x40404040;
	val2 |= val << 24 | val << 16 | val << 8 | val;

	/* rtm inc */
	regmap_read(combrxphy->regmap, REG(0x6604), &val);
	val |= BIT(31);
	regmap_write(combrxphy->regmap, REG(0x6604), val);

	regmap_write(combrxphy->regmap, REG(0x6620), val2);

	/* rtm en bypass */
	regmap_read(combrxphy->regmap, REG(0x6600), &val);
	val |= BIT(7);
	regmap_write(combrxphy->regmap, REG(0x6600), val);

	/* rtm prot en bypass */
	regmap_read(combrxphy->regmap, REG(0x6610), &val);
	val |= 0x80f000;
	regmap_write(combrxphy->regmap, REG(0x6610), val);

	regmap_read(combrxphy->regmap, REG(0x661c), &val);
	val |= 0x81000000;
	regmap_write(combrxphy->regmap, REG(0x661c), val);

	/* enable pll */
	regmap_read(combrxphy->regmap, REG(0x6630), &val);
	val &= ~BIT(4);
	val |= BIT(3);
	regmap_write(combrxphy->regmap, REG(0x6630), val);

	/* equ en */
	regmap_read(combrxphy->regmap, REG(0x6618), &val);
	val |= BIT(4);
	regmap_write(combrxphy->regmap, REG(0x6618), val);

	regmap_read(combrxphy->regmap, REG(0x6614), &val);
	val |= 0x10900000;
	regmap_write(combrxphy->regmap, REG(0x6614), val);

	regmap_read(combrxphy->regmap, REG(0x6610), &val);
	val |= 0xf00;
	regmap_write(combrxphy->regmap, REG(0x6610), val);

	regmap_read(combrxphy->regmap, REG(0x6630), &val);
	val |= 0x870000;
	regmap_write(combrxphy->regmap, REG(0x6630), val);

	udelay(10);

	/* get cdr_mode,make sure cdr_mode != 5’h1f */
	regmap_read(combrxphy->regmap, REG(0x6654), &val);
	if ((val & 0x1f0000) == 0x1f0000)
		dev_err(combrxphy->dev, "error,clock error!");

	/* manually configure PLL */
	if (f <= 340000) {
		regmap_write(combrxphy->regmap, REG(0x662c), 0x01000500);
		if (is_yuv420)
			regmap_write(combrxphy->regmap, REG(0x66a8),
				     0x0000c000);
		else
			regmap_write(combrxphy->regmap, REG(0x66a8),
				     0x0000c600);
	} else {
		regmap_write(combrxphy->regmap, REG(0x662c), 0x01001400);
		regmap_write(combrxphy->regmap, REG(0x66a8), 0x0000c600);
	}

	switch (f) {
	case 27000:
	case 64000:
	case 74250:
		max_round = 6;
		min_round = 0;
		pll_man = 0x7964c;
		break;
	case 148500:
		pll_man = 0x7a1c8;
		max_round = 6;
		min_round = 0;
		break;
	case 297000:
		pll_man = 0x7a108;
		max_round = 8;
		min_round = 2;
		break;
	case 594000:
		pll_man = 0x7f0c8;
		max_round = 4;
		min_round = 10;
		break;
	default:
		pll_man = 0x7964c;
		max_round = 1;
		min_round = 12;
		break;
	}

	pll_man  |= BIT(23);
	regmap_write(combrxphy->regmap, REG(0x6630), pll_man);

	/* EQ and SAMPLE cfg */
	rk628_combrxphy_sample_edge_procedure(combrxphy, f, min_round,
					      max_round);

	/* Deassert fifo reset,enable fifo write and read */
	regmap_write(combrxphy->regmap, REG(0x66a0), 0x00000003);
	regmap_write(combrxphy->regmap, REG(0x66b0), 0x00080060);
	regmap_write(combrxphy->regmap, REG(0x66a0), 0x00000083);
	regmap_write(combrxphy->regmap, REG(0x66b0), 0x00380060);
	regmap_update_bits(combrxphy->regmap, REG(0x66ac), GENMASK(31, 24),
			   UPDATE(0x22, 31, 24));
	usleep_range(10*1000, 11*1000);

	/* check all 3 data channels alignment */
	count = 0;
	for (i = 0; i < 100; i++) {
		udelay(100);
		regmap_read(combrxphy->regmap, REG(0x66b4), &data_a);
		regmap_read(combrxphy->regmap, REG(0x66b8), &data_b);
		/* ch0 ch1 ch2 lock */
		if (((data_a & 0x00ff00ff) == 0x00ff00ff) &&
		    ((data_b & 0xff) == 0xff))
			count++;
	}

	if (count >= 100) {
		dev_info(combrxphy->dev, "channel alignment done");
		ret = 0;
	} else if (count > 0) {
		dev_err(combrxphy->dev, "not stable, count:%d of 100", count);
		ret = -EINVAL;
	} else {
		dev_err(combrxphy->dev, "channel alignment failed!");
		ret = -EINVAL;
	}

	return ret;
}

static int rk628_combrxphy_power_on(struct phy *phy)
{
	struct rk628_combrxphy *combrxphy = phy_get_drvdata(phy);
	int f = phy_get_bus_width(phy);

	clk_prepare_enable(combrxphy->pclk);
	reset_control_deassert(combrxphy->rstc);

	return rk628_combrxphy_set_hdmi_mode(combrxphy, f);
}

static int rk628_combrxphy_power_off(struct phy *phy)
{
	struct rk628_combrxphy *combrxphy = phy_get_drvdata(phy);

	reset_control_assert(combrxphy->rstc);
	clk_disable_unprepare(combrxphy->pclk);

	return 0;
}

static int rk628_combrxphy_set_mode(struct phy *phy, enum phy_mode mode)
{
	struct rk628_combrxphy *combrxphy = phy_get_drvdata(phy);

	combrxphy->mode = mode;

	return 0;
}

static const struct phy_ops rk628_combrxphy_ops = {
	.set_mode = rk628_combrxphy_set_mode,
	.power_on = rk628_combrxphy_power_on,
	.power_off = rk628_combrxphy_power_off,
	.owner = THIS_MODULE,
};

static const struct regmap_range rk628_combrxphy_readable_ranges[] = {
	regmap_reg_range(REG(0x6600), REG(0x665b)),
	regmap_reg_range(REG(0x66a0), REG(0x66db)),
	regmap_reg_range(REG(0x66f0), REG(0x66ff)),
	regmap_reg_range(REG(0x6700), REG(0x6790)),
};

static const struct regmap_access_table rk628_combrxphy_readable_table = {
	.yes_ranges     = rk628_combrxphy_readable_ranges,
	.n_yes_ranges   = ARRAY_SIZE(rk628_combrxphy_readable_ranges),
};

static const struct regmap_config rk628_combrxphy_regmap_cfg = {
	.name = "combrxphy",
	.reg_bits = 32,
	.val_bits = 32,
	.reg_stride = 4,
	.max_register = COMBRXPHY_MAX_REGISTER,
	.reg_format_endian = REGMAP_ENDIAN_LITTLE,
	.val_format_endian = REGMAP_ENDIAN_LITTLE,
	.rd_table = &rk628_combrxphy_readable_table,
};

static int rk628_combrxphy_probe(struct platform_device *pdev)
{
	struct rk628 *rk628 = dev_get_drvdata(pdev->dev.parent);
	struct device *dev = &pdev->dev;
	struct rk628_combrxphy *combrxphy;
	struct phy_provider *phy_provider;
	struct phy *phy;
	int ret;

	if (!of_device_is_available(dev->of_node))
		return -ENODEV;

	combrxphy = devm_kzalloc(dev, sizeof(*combrxphy), GFP_KERNEL);
	if (!combrxphy)
		return -ENOMEM;

	combrxphy->dev = dev;
	combrxphy->parent = rk628;
	platform_set_drvdata(pdev, combrxphy);

	combrxphy->pclk = devm_clk_get(dev, "pclk");
	if (IS_ERR(combrxphy->pclk)) {
		ret = PTR_ERR(combrxphy->pclk);
		dev_err(dev, "failed to get pclk: %d\n", ret);
		return ret;
	}

	combrxphy->rstc = of_reset_control_get(dev->of_node, NULL);
	if (IS_ERR(combrxphy->rstc)) {
		ret = PTR_ERR(combrxphy->rstc);
		dev_err(dev, "failed to get reset control: %d\n", ret);
		return ret;
	}

	combrxphy->regmap = devm_regmap_init_i2c(rk628->client,
						 &rk628_combrxphy_regmap_cfg);
	if (IS_ERR(combrxphy->regmap)) {
		ret = PTR_ERR(combrxphy->regmap);
		dev_err(dev, "failed to allocate host register map: %d\n", ret);
		return ret;
	}

	phy = devm_phy_create(dev, NULL, &rk628_combrxphy_ops);
	if (IS_ERR(phy)) {
		ret = PTR_ERR(phy);
		dev_err(dev, "failed to create phy: %d\n", ret);
		return ret;
	}

	phy_set_drvdata(phy, combrxphy);

	phy_provider = devm_of_phy_provider_register(dev, of_phy_simple_xlate);
	if (IS_ERR(phy_provider)) {
		ret = PTR_ERR(phy_provider);
		dev_err(dev, "failed to register phy provider: %d\n", ret);
		return ret;
	}

	return 0;
}

static const struct of_device_id rk628_combrxphy_of_match[] = {
	{ .compatible = "rockchip,rk628-combrxphy", },
	{}
};
MODULE_DEVICE_TABLE(of, rk628_combrxphy_of_match);

static struct platform_driver rk628_combrxphy_driver = {
	.driver = {
		.name = "rk628-combrxphy",
		.of_match_table	= of_match_ptr(rk628_combrxphy_of_match),
	},
	.probe = rk628_combrxphy_probe,
};
module_platform_driver(rk628_combrxphy_driver);

MODULE_AUTHOR("Algea Cao <algea.cao@rock-chips.com>");
MODULE_DESCRIPTION("Rockchip RK628 HDMI Combo RX PHY driver");
MODULE_LICENSE("GPL v2");
