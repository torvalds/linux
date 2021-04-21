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

struct rk628_combrxphy {
	struct device *dev;
	struct rk628 *parent;
	struct regmap *regmap;
	struct clk *pclk;
	struct reset_control *rstc;
	bool is_cable_mode;
};

#define REG(x)			((x) + 0x10000)
#define COMBRXPHY_MAX_REGISTER	REG(0x6790)

#define MAX_ROUND		6
#define MAX_DATA_NUM		16
#define MAX_CHANNEL		3
#define CLK_DET_TRY_TIMES	10
#define CLK_STABLE_LOOP_CNT	10
#define CLK_STABLE_THRESHOLD	6

static int debug;
module_param(debug, int, 0644);
MODULE_PARM_DESC(debug, "debug level (0-1)");

static void rk628_combrxphy_set_data_of_round(u32 *data, u32 *data_in)
{
	if ((data != NULL) && (data_in != NULL)) {
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
}

static void
rk628_combrxphy_max_zero_of_round(struct rk628_combrxphy *combrxphy,
				  u32 *data_in, u32 *max_zero, u32 *max_val,
				  int n, int ch)
{
	u32 i;
	u32 cnt = 0;
	u32 max_cnt = 0;
	u32 max_v = 0;

	if (debug > 0) {
		dev_info(combrxphy->dev,
			"%s channel:%d, round:%d ====\n",  __func__, ch, n);
		print_hex_dump(KERN_WARNING, "", DUMP_PREFIX_NONE, 32, 4,
				data_in, MAX_DATA_NUM * sizeof(u32), false);
	}

	for (i = 0; i < MAX_DATA_NUM; i++) {
		if (max_v < data_in[i])
			max_v = data_in[i];
	}

	for (i = 0; i < MAX_DATA_NUM; i++) {
		if (data_in[i] == 0)
			cnt = cnt + 200;
		else if ((data_in[i] > 0) && (data_in[i] < 100))
			cnt = cnt + 100 - data_in[i];
	}
	max_cnt = (cnt >= 3200) ? 0 : cnt;

	max_zero[n] = max_cnt;
	max_val[n] = max_v;
	dev_dbg(combrxphy->dev,
		"channel:%d, round:%d, max_zero_cnt:%d, max_val:%#x",
		ch, n, max_zero[n], max_val[n]);
}

static int
rk628_combrxphy_chose_round_for_ch(struct rk628_combrxphy *combrxphy,
				   u32 *rd_max_zero,
				   u32 *rd_max_val, int ch)
{
	int i, rd = 0;
	u32 max = 0;
	u32 max_v = 0;

	if (debug > 0) {
		dev_info(combrxphy->dev,
			"%s max cnt of channel:%d ====\n", __func__, ch);
		print_hex_dump(KERN_WARNING, "", DUMP_PREFIX_NONE, 32, 4,
				rd_max_zero, MAX_ROUND * sizeof(u32), false);

		dev_info(combrxphy->dev,
			"%s max value of channel:%d ====\n", __func__, ch);
		print_hex_dump(KERN_WARNING, "", DUMP_PREFIX_NONE, 32, 4,
				rd_max_val, MAX_ROUND * sizeof(u32), false);
	}

	for (i = 0; i < MAX_ROUND; i++) {
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

	dev_dbg(combrxphy->dev, "channel dc gain ch0:%d, ch1:%d, ch2:%d\n",
			x, y, z);

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
	u32 equ_gain_ch0, equ_gain_ch1, equ_gain_ch2;

	dev_dbg(combrxphy->dev, "channel equ gain ch0:%d, ch1:%d, ch2:%d\n",
			x, y, z);

	equ_gain_ch0 = (x & 0xf);
	equ_gain_ch1 = (y & 0xf);
	equ_gain_ch2 = (z & 0xf);
	regmap_read(combrxphy->regmap, REG(0x6618), &val);
	val = (val & 0xff00f0ff) | (equ_gain_ch1 << 20) |
		(equ_gain_ch0 << 16) | (equ_gain_ch2 << 8);
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

	regmap_read(combrxphy->regmap, REG(0x6634), &val);
	val = val & (~(0xf << ((ch + 1) * 4)));
	regmap_write(combrxphy->regmap, REG(0x6634), val);
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

static void rk628_combrxphy_sample_edge_procedure_for_cable(
		struct rk628_combrxphy *combrxphy, u32 cdr_mode)
{
	u32 n, ch;
	u32 data[MAX_DATA_NUM];
	u32 data_in[MAX_DATA_NUM];
	u32 round_max_zero[MAX_CHANNEL][MAX_ROUND];
	u32 round_max_value[MAX_CHANNEL][MAX_ROUND];
	u32 ch_round[MAX_CHANNEL];
	u32 edge, dc_gain;
	u32 rd_offset;

	/* Step1: set sample edge mode for channel 0~2 */
	for (ch = 0; ch < MAX_CHANNEL; ch++)
		rk628_combrxphy_set_sample_edge_mode(combrxphy, ch);

	/* step2: once per round */
	for (ch = 0; ch < MAX_CHANNEL; ch++) {
		rk628_combrxphy_select_channel(combrxphy, ch);
		rk628_combrxphy_cfg_6730(combrxphy);
	}

	/* step3: config sample edge until the end of one frame
	 * (for example 1080p:2200*1125=32’h25c3f8)
	 */
	if (cdr_mode < 16) {
		dc_gain = 0;
		rd_offset = 0;
	} else if (cdr_mode < 18) {
		dc_gain = 1;
		rd_offset = 0;
	} else {
		dc_gain = 3;
		rd_offset = 2;
	}

	/* When the pix clk is the same, the low frame rate resolution is used
	 * to calculate the sampling window (the frame rate is not less than
	 * 30). The sampling delay time is configured as 40ms.
	 */
	if (cdr_mode <= 1) { /* 27M vic17 720x576P50 */
		edge = 864 * 625;
	} else if (cdr_mode <= 4) { /* 59.4M vic81 1680x720P30 */
		edge = 2640 * 750;
	} else if (cdr_mode <= 7) { /* 74.25M vic34 1920x1080P30 */
		edge = 2200 * 1125;
	} else if (cdr_mode <= 14) { /* 119M vic88 2560x1180P30 */
		edge = 3520 * 1125;
	} else if (cdr_mode <= 16) { /* 148.5M vic31 1920x1080P50 */
		edge = 2640 * 1125;
	} else if (cdr_mode <= 17) { /* 162M vic89 2560x1080P50 */
		edge = 3300 * 1125;
	} else if (cdr_mode <= 18) { /* 297M vic95 3840x2160P30 */
		edge = 4400 * 2250;
	} else {		     /* unkonw vic16 1920x1080P60 */
		edge = 2200 * 1125;
	}

	dev_info(combrxphy->dev,
		"cdr_mode:%d, dc_gain:%d, rd_offset:%d, edge:%#x\n",
		cdr_mode, dc_gain, rd_offset, edge);
	for (ch = 0; ch < MAX_CHANNEL; ch++) {
		rk628_combrxphy_select_channel(combrxphy, ch);
		regmap_write(combrxphy->regmap, REG(0x6708), edge);
	}

	rk628_combrxphy_set_dc_gain(combrxphy, dc_gain, dc_gain, dc_gain);
	for (n = rd_offset; n < (rd_offset + MAX_ROUND); n++) {
		/* step4:set sample edge round value n,n=0(n=0~31) */
		rk628_combrxphy_set_sample_edge_round(combrxphy, n, n, n);
		/* step5:start sample edge */
		rk628_combrxphy_start_sample_edge(combrxphy);
		/* step6:waiting more than one frame time */
		usleep_range(40*1000, 41*1000);
		for (ch = 0; ch < MAX_CHANNEL; ch++) {
			/* step7: get data of round n */
			rk628_combrxphy_select_channel(combrxphy, ch);
			rk628_combrxphy_get_data_of_round(combrxphy, data);
			rk628_combrxphy_set_data_of_round(data, data_in);
			/* step8: get the max constant value of round n */
			rk628_combrxphy_max_zero_of_round(combrxphy, data_in,
				round_max_zero[ch], round_max_value[ch],
				n - rd_offset, ch);
		}
	}

	/* step9: after finish round, get the max constant value and
	 * corresponding value n.
	 */
	for (ch = 0; ch < MAX_CHANNEL; ch++) {
		ch_round[ch] = rk628_combrxphy_chose_round_for_ch(combrxphy,
				round_max_zero[ch], round_max_value[ch], ch)
				+ rd_offset;
	}
	dev_info(combrxphy->dev, "last equ gain ch0:%d, ch1:%d, ch2:%d\n",
		ch_round[0], ch_round[1], ch_round[2]);

	 /* step10: write result to sample edge round value  */
	rk628_combrxphy_set_sample_edge_round(combrxphy, ch_round[0],
		ch_round[1], ch_round[2]);

	/* do step5, step6 again */
	/* step5:start sample edge */
	rk628_combrxphy_start_sample_edge(combrxphy);
	/* step6:waiting more than one frame time */
	usleep_range(40*1000, 41*1000);
}

static void
rk628_combrxphy_sample_edge_procedure(struct rk628_combrxphy *combrxphy,
				      int f, u32 rd_offset)
{
	u32 n, ch;
	u32 data[MAX_DATA_NUM];
	u32 data_in[MAX_DATA_NUM];
	u32 round_max_zero[MAX_CHANNEL][MAX_ROUND];
	u32 round_max_value[MAX_CHANNEL][MAX_ROUND];
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

	for (n = rd_offset; n < (rd_offset + MAX_ROUND); n++) {
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
			rk628_combrxphy_set_data_of_round(data, data_in);
			dev_dbg(combrxphy->dev, "step7 set data ok!");
			rk628_combrxphy_max_zero_of_round(combrxphy, data_in,
							  round_max_zero[ch],
							  round_max_value[ch],
							  n - rd_offset, ch);
		}
	}
	for (ch = 0; ch < MAX_CHANNEL; ch++)
		ch_round[ch] =
			rk628_combrxphy_chose_round_for_ch(combrxphy,
							   round_max_zero[ch],
							   round_max_value[ch],
							   ch) + rd_offset;

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

static int rk628_combrxphy_try_clk_detect(struct rk628_combrxphy *combrxphy)
{
	u32 val, i;
	int ret;

	ret = -1;
	reset_control_assert(combrxphy->rstc);
	usleep_range(10, 20);
	reset_control_deassert(combrxphy->rstc);
	usleep_range(10, 20);

	/* step1: set pin_rst_n to 1’b0.wait 1 period(1us).release reset */
	/* step2: select pll clock src and enable auto check */
	regmap_read(combrxphy->regmap, REG(0x6630), &val);
	/* clear bit0 and bit3 */
	val = val & 0xfffffff6;
	regmap_write(combrxphy->regmap, REG(0x6630), val);
	/* step3: select hdmi mode and enable chip, read reg6654,
	 * make sure auto setup done.
	 */
	/* auto fsm reset related */
	regmap_read(combrxphy->regmap, REG(0x6630), &val);
	val = val | BIT(24);
	regmap_write(combrxphy->regmap, REG(0x6630), val);
	/* pull down ana rstn */
	regmap_read(combrxphy->regmap, REG(0x66f0), &val);
	val = val & 0xfffffeff;
	regmap_write(combrxphy->regmap, REG(0x66f0), val);
	/* pull down dig rstn */
	regmap_read(combrxphy->regmap, REG(0x66f4), &val);
	val = val & 0xfffffffe;
	regmap_write(combrxphy->regmap, REG(0x66f4), val);
	/* pull up ana rstn */
	regmap_read(combrxphy->regmap, REG(0x66f0), &val);
	val = val | 0x100;
	regmap_write(combrxphy->regmap, REG(0x66f0), val);
	/* pull up dig rstn */
	regmap_read(combrxphy->regmap, REG(0x66f4), &val);
	val = val  | 0x1;
	regmap_write(combrxphy->regmap, REG(0x66f4), val);

	regmap_read(combrxphy->regmap, REG(0x66f0), &val);
	/* set bit0 and bit2 to 1*/
	val = (val & 0xfffffff8) | 0x5;
	regmap_write(combrxphy->regmap, REG(0x66f0), val);

	/* auto fsm en = 0 */
	regmap_read(combrxphy->regmap, REG(0x66f0), &val);
	/* set bit0 and bit2 to 1*/
	val = (val & 0xfffffff8) | 0x4;
	regmap_write(combrxphy->regmap, REG(0x66f0), val);

	for (i = 0; i < 10; i++) {
		usleep_range(500, 510);
		regmap_read(combrxphy->regmap, REG(0x6654), &val);
		if ((val & 0xf0000000) == 0x80000000) {
			ret = 0;
			dev_info(combrxphy->dev, "clock detected!");
			break;
		}
	}

	return ret;
}

static int
rk628_combrxphy_set_hdmi_mode_for_cable(struct rk628_combrxphy *combrxphy,
					  int f)
{
	u32 val, val_a, val_b, data_a, data_b;
	u32 i, j, count, ret;
	u32 cdr_mode, cdr_data, pll_man;
	u32 tmds_bitrate_per_lane;
	u32 cdr_data_min, cdr_data_max;

	/*
	 * use the mode of automatic clock detection, only supports fixed TMDS
	 * frequency.Refer to register 0x6654[21:16]:
	 * 5'd31:Error mode
	 * 5'd30:manual mode detected
	 * 5'd18:rx3p clock = 297MHz
	 * 5'd17:rx3p clock = 162MHz
	 * 5'd16:rx3p clock = 148.5MHz
	 * 5'd15:rx3p clock = 135MHz
	 * 5'd14:rx3p clock = 119MHz
	 * 5'd13:rx3p clock = 108MHz
	 * 5'd12:rx3p clock = 101MHz
	 * 5'd11:rx3p clock = 92.8125MHz
	 * 5'd10:rx3p clock = 88.75MHz
	 * 5'd9:rx3p clock  = 85.5MHz
	 * 5'd8:rx3p clock  = 83.5MHz
	 * 5'd7:rx3p clock  = 74.25MHz
	 * 5'd6:rx3p clock  = 68.25MHz
	 * 5'd5:rx3p clock  = 65MHz
	 * 5'd4:rx3p clock  = 59.4MHz
	 * 5'd3:rx3p clock  = 40MHz
	 * 5'd2:rx3p clock  = 33.75MHz
	 * 5'd1:rx3p clock  = 27MHz
	 * 5'd0:rx3p clock  = 25.17MHz
	 */

	const u32 cdr_mode_to_khz[] = {
		25170,   27000,  33750,  40000,  59400,  65000,  68250,
		74250,   83500,  85500,  88750,  92812, 101000, 108000,
		119000, 135000, 148500, 162000, 297000,
	};

	for (i = 0; i < CLK_DET_TRY_TIMES; i++) {
		if (rk628_combrxphy_try_clk_detect(combrxphy) >= 0)
			break;
		usleep_range(100*1000, 100*1000);
	}
	regmap_read(combrxphy->regmap, REG(0x6654), &val);
	dev_info(combrxphy->dev, "clk det over cnt:%d, reg_0x6654:%#x", i, val);

	regmap_read(combrxphy->regmap, REG(0x6620), &val);
	if ((i == CLK_DET_TRY_TIMES) ||
		((val & 0x7f000000) == 0) ||
		((val & 0x007f0000) == 0) ||
		((val & 0x00007f00) == 0) ||
		((val & 0x0000007f) == 0)) {
		dev_info(combrxphy->dev,
			"clock detected failed, cfg resistance manual!");
		regmap_write(combrxphy->regmap, REG(0x6620), 0x66666666);
		regmap_update_bits(combrxphy->regmap, REG(0x6604), BIT(31),
				BIT(31));
		usleep_range(1000, 1100);
	}

	/* step4: get cdr_mode and cdr_data */
	for (j = 0; j < CLK_STABLE_LOOP_CNT ; j++) {
		cdr_data_min = 0xffffffff;
		cdr_data_max = 0;

		for (i = 0; i < CLK_DET_TRY_TIMES; i++) {
			regmap_read(combrxphy->regmap, REG(0x6654), &val);
			cdr_data = val & 0xffff;
			if (cdr_data <= cdr_data_min)
				cdr_data_min = cdr_data;
			if (cdr_data >= cdr_data_max)
				cdr_data_max = cdr_data;
			udelay(50);
		}

		if (((cdr_data_max - cdr_data_min) <= CLK_STABLE_THRESHOLD) &&
				(cdr_data_min >= 60)) {
			dev_info(combrxphy->dev, "clock stable!");
			break;
		}
	}

	if (j == CLK_STABLE_LOOP_CNT) {
		regmap_read(combrxphy->regmap, REG(0x6630), &val_a);
		regmap_read(combrxphy->regmap, REG(0x6608), &val_b);
		dev_err(combrxphy->dev,
			"err, clk not stable, reg_0x6630:%#x, reg_0x6608:%#x",
			val_a, val_b);

		return -EINVAL;
	}

	regmap_read(combrxphy->regmap, REG(0x6654), &val);
	if ((val & 0x1f0000) == 0x1f0000) {
		regmap_read(combrxphy->regmap, REG(0x6630), &val_a);
		regmap_read(combrxphy->regmap, REG(0x6608), &val_b);
		dev_err(combrxphy->dev,
			"clock error: 0x1f, reg_0x6630:%#x, reg_0x6608:%#x",
			val_a, val_b);

		return -EINVAL;
	}

	cdr_mode = (val >> 16) & 0x1f;
	cdr_data =  val & 0xffff;
	dev_info(combrxphy->dev, "cdr_mode:%d, cdr_data:%d\n", cdr_mode,
			cdr_data);

	/* step5: manually configure PLL
	 * cfg reg 66a8 tmds clock div2 for rgb/yuv444 as default
	 * reg 662c[16:8] pll_pre_div
	 */
	if (f <= 340000) {
		regmap_write(combrxphy->regmap, REG(0x662c), 0x01000500);
		regmap_write(combrxphy->regmap, REG(0x66a8), 0x0000c600);
	} else {
		regmap_write(combrxphy->regmap, REG(0x662c), 0x01001400);
		regmap_write(combrxphy->regmap, REG(0x66a8), 0x0000c600);
	}

	/* when tmds bitrate/lane <= 340M, bitrate/lane = pix_clk * 10 */
	tmds_bitrate_per_lane = cdr_mode_to_khz[cdr_mode] * 10;
	if (tmds_bitrate_per_lane < 400000)
		pll_man = 0x7960c;
	else if (tmds_bitrate_per_lane < 600000)
		pll_man = 0x7750c;
	else if (tmds_bitrate_per_lane < 800000)
		pll_man = 0x7964c;
	else if (tmds_bitrate_per_lane < 1000000)
		pll_man = 0x7754c;
	else if (tmds_bitrate_per_lane < 1600000)
		pll_man = 0x7a108;
	else if (tmds_bitrate_per_lane < 2400000)
		pll_man = 0x73588;
	else if (tmds_bitrate_per_lane < 3400000)
		pll_man = 0x7a108;
	else
		pll_man = 0x7f0c8;

	dev_info(combrxphy->dev, "cdr_mode:%d, pll_man:%#x\n", cdr_mode,
			pll_man);
	regmap_write(combrxphy->regmap, REG(0x6630), pll_man);

	/* step6: EQ and SAMPLE cfg */
	rk628_combrxphy_sample_edge_procedure_for_cable(combrxphy, cdr_mode);

	/* step7: Deassert fifo reset,enable fifo write and read */
	/* reset rx_infifo */
	regmap_write(combrxphy->regmap, REG(0x66a0), 0x00000003);
	/* rx_infofo wr/rd disable */
	regmap_write(combrxphy->regmap, REG(0x66b0), 0x00080060);
	/* deassert rx_infifo reset */
	regmap_write(combrxphy->regmap, REG(0x66a0), 0x00000083);
	/* enable rx_infofo wr/rd en */
	regmap_write(combrxphy->regmap, REG(0x66b0), 0x00380060);
	/* cfg 0x2260 high_8b to 0x66ac high_8b, low_8b to 0x66b0 low_8b */
	regmap_update_bits(combrxphy->regmap, REG(0x66ac), GENMASK(31, 24),
			UPDATE(0x22, 31, 24));
	usleep_range(5*1000, 6*1000);

	/* step8: check all 3 data channels alignment */
	count = 0;
	for (i = 0; i < 100; i++) {
		usleep_range(100, 110);
		regmap_read(combrxphy->regmap, REG(0x66b4), &data_a);
		regmap_read(combrxphy->regmap, REG(0x66b8), &data_b);
		/* ch0 ch1 ch2 lock */
		if (((data_a & 0x00ff00ff) == 0x00ff00ff) &&
			((data_b & 0xff) == 0xff)) {
			count++;
		}
	}

	if (count >= 100) {
		dev_info(combrxphy->dev, "channel alignment done");
		dev_info(combrxphy->dev, "rx initial done");
		ret = 0;
	} else if (count > 0) {
		dev_err(combrxphy->dev, "link not stable, count:%d of 100",
				count);
		ret = 0;
	} else {
		dev_err(combrxphy->dev, "channel alignment failed!");
		ret = -EINVAL;
	}

	return ret;
}

static int rk628_combrxphy_set_hdmi_mode(struct rk628_combrxphy *combrxphy,
					 int bus_width)
{
	u32 val, data_a, data_b, f, val2 = 0;
	int i, ret, count;
	u32 pll_man, rd_offset;
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
		rd_offset = 0;
		pll_man = 0x7964c;
		break;
	case 148500:
		pll_man = 0x7a1c8;
		rd_offset = 0;
		break;
	case 297000:
		pll_man = 0x7a108;
		rd_offset = 2;
		break;
	case 594000:
		pll_man = 0x7f0c8;
		rd_offset = 4;
		break;
	default:
		pll_man = 0x7964c;
		rd_offset = 1;
		break;
	}

	pll_man  |= BIT(23);
	regmap_write(combrxphy->regmap, REG(0x6630), pll_man);

	/* EQ and SAMPLE cfg */
	rk628_combrxphy_sample_edge_procedure(combrxphy, f, rd_offset);

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
	int ret;

	/* Bit31 is used to distinguish HDMI cable mode and direct
	 * connection mode.
	 * Bit31: 0 -direct connection mode;
	 *        1 -cable mode;
	 */
	combrxphy->is_cable_mode = (f & BIT(31)) ? true : false;
	dev_dbg(combrxphy->dev, "%s\n", __func__);
	clk_prepare_enable(combrxphy->pclk);
	reset_control_assert(combrxphy->rstc);
	udelay(10);
	reset_control_deassert(combrxphy->rstc);
	udelay(10);

	if (combrxphy->is_cable_mode) {
		f = f & 0x7fffffff;
		ret = rk628_combrxphy_set_hdmi_mode_for_cable(combrxphy, f);
	} else {
		ret = rk628_combrxphy_set_hdmi_mode(combrxphy, f);
	}

	return ret;
}

static int rk628_combrxphy_power_off(struct phy *phy)
{
	struct rk628_combrxphy *combrxphy = phy_get_drvdata(phy);

	dev_dbg(combrxphy->dev, "%s\n", __func__);
	reset_control_assert(combrxphy->rstc);
	udelay(10);
	clk_disable_unprepare(combrxphy->pclk);

	return 0;
}

static const struct phy_ops rk628_combrxphy_ops = {
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
