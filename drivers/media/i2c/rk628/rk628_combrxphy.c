// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2020 Rockchip Electronics Co. Ltd.
 *
 * Author: Shunqing Chen <csq@rock-chips.com>
 */

#include <linux/kernel.h>
#include <linux/clk.h>
#include <linux/delay.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/of_device.h>
#include <linux/platform_device.h>
#include <linux/reset.h>
#include "rk628.h"
#include "rk628_combrxphy.h"
#include "rk628_cru.h"

#define COMBRXPHY_MAX_REGISTER	COMBRX_REG(0x6790)

#define MAX_ROUND		6
#define MAX_DATA_NUM		16
#define MAX_CHANNEL		3
#define CLK_DET_TRY_TIMES	10
#define CHECK_CNT		100
#define CLK_STABLE_LOOP_CNT	10
#define CLK_STABLE_THRESHOLD	6

static int rk628_combrxphy_try_clk_detect(struct rk628 *rk628)
{
	u32 val, i;
	int ret;

	ret = -1;
	rk628_control_assert(rk628, RGU_RXPHY);
	usleep_range(10, 20);
	rk628_control_deassert(rk628, RGU_RXPHY);
	usleep_range(10, 20);

	/* step1: set pin_rst_n to 1’b0.wait 1 period(1us).release reset */
	/* step2: select pll clock src and enable auto check */
	rk628_i2c_read(rk628, COMBRX_REG(0x6630), &val);
	/* clear bit0 and bit3 */
	val = val & 0xfffffff6;
	rk628_i2c_write(rk628, COMBRX_REG(0x6630), val);
	/* step3: select hdmi mode and enable chip, read reg6654,
	 * make sure auto setup done.
	 */
	/* auto fsm reset related */
	rk628_i2c_read(rk628, COMBRX_REG(0x6630), &val);
	val = val | BIT(24);
	rk628_i2c_write(rk628, COMBRX_REG(0x6630), val);
	/* pull down ana rstn */
	rk628_i2c_read(rk628, COMBRX_REG(0x66f0), &val);
	val = val & 0xfffffeff;
	rk628_i2c_write(rk628, COMBRX_REG(0x66f0), val);
	/* pull down dig rstn */
	rk628_i2c_read(rk628, COMBRX_REG(0x66f4), &val);
	val = val & 0xfffffffe;
	rk628_i2c_write(rk628, COMBRX_REG(0x66f4), val);
	/* pull up ana rstn */
	rk628_i2c_read(rk628, COMBRX_REG(0x66f0), &val);
	val = val | 0x100;
	rk628_i2c_write(rk628, COMBRX_REG(0x66f0), val);
	/* pull up dig rstn */
	rk628_i2c_read(rk628, COMBRX_REG(0x66f4), &val);
	val = val  | 0x1;
	rk628_i2c_write(rk628, COMBRX_REG(0x66f4), val);

	rk628_i2c_read(rk628, COMBRX_REG(0x66f0), &val);
	/* set bit0 and bit2 to 1*/
	val = (val & 0xfffffff8) | 0x5;
	rk628_i2c_write(rk628, COMBRX_REG(0x66f0), val);

	/* auto fsm en = 0 */
	rk628_i2c_read(rk628, COMBRX_REG(0x66f0), &val);
	/* set bit0 and bit2 to 1*/
	val = (val & 0xfffffff8) | 0x4;
	rk628_i2c_write(rk628, COMBRX_REG(0x66f0), val);

	for (i = 0; i < 10; i++) {
		mdelay(1);
		rk628_i2c_read(rk628, COMBRX_REG(0x6654), &val);
		if ((val & 0xf0000000) == 0x80000000) {
			ret = 0;
			dev_info(rk628->dev, "clock detected!\n");
			break;
		}
	}

	return ret;
}

static void rk628_combrxphy_get_data_of_round(struct rk628 *rk628,
					      u32 *data)
{
	u32 i;

	for (i = 0; i < MAX_DATA_NUM; i++)
		rk628_i2c_read(rk628, COMBRX_REG(0x6740 + i * 4), &data[i]);
}

static void rk628_combrxphy_set_dc_gain(struct rk628 *rk628,
					u32 x, u32 y, u32 z)
{
	u32 val;
	u32 dc_gain_ch0, dc_gain_ch1, dc_gain_ch2;

	dc_gain_ch0 = x & 0xf;
	dc_gain_ch1 = y & 0xf;
	dc_gain_ch2 = z & 0xf;
	rk628_i2c_read(rk628, COMBRX_REG(0x661c), &val);

	val = (val & 0xff0f0f0f) | (dc_gain_ch0 << 20) | (dc_gain_ch1 << 12) |
		(dc_gain_ch2 << 4);
	rk628_i2c_write(rk628, COMBRX_REG(0x661c), val);
}

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
rk628_combrxphy_max_zero_of_round(struct rk628 *rk628,
				  u32 *data_in, u32 *max_zero,
				  u32 *max_val, int n, int ch)
{
	u32 i;
	u32 cnt = 0;
	u32 max_cnt = 0;
	u32 max_v = 0;

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
	dev_info(rk628->dev, "channel:%d, round:%d, max_zero_cnt:%d, max_val:%#x\n",
		 ch, n, max_zero[n], max_val[n]);
}

static int rk628_combrxphy_chose_round_for_ch(struct rk628 *rk628,
					      u32 *rd_max_zero,
					      u32 *rd_max_val, int ch)
{
	int i, rd = 0;
	u32 max = 0;
	u32 max_v = 0;

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
	dev_info(rk628->dev, "%s channel:%d, rd:%d\n", __func__, ch, rd);

	return rd;
}

static void rk628_combrxphy_set_sample_edge_round(struct rk628 *rk628, u32 x, u32 y, u32 z)
{
	u32 val;
	u32 equ_gain_ch0, equ_gain_ch1, equ_gain_ch2;

	equ_gain_ch0 = (x & 0xf);
	equ_gain_ch1 = (y & 0xf);
	equ_gain_ch2 = (z & 0xf);
	rk628_i2c_read(rk628, COMBRX_REG(0x6618), &val);
	val = (val & 0xff00f0ff) | (equ_gain_ch1 << 20) |
		(equ_gain_ch0 << 16) | (equ_gain_ch2 << 8);
	rk628_i2c_write(rk628, COMBRX_REG(0x6618), val);
}

static void rk628_combrxphy_start_sample_edge(struct rk628 *rk628)
{
	u32 val;

	rk628_i2c_read(rk628, COMBRX_REG(0x66f0), &val);
	val &= 0xfffff1ff;
	rk628_i2c_write(rk628, COMBRX_REG(0x66f0), val);
	rk628_i2c_read(rk628, COMBRX_REG(0x66f0), &val);
	val = (val & 0xfffff1ff) | (0x7 << 9);
	rk628_i2c_write(rk628, COMBRX_REG(0x66f0), val);
}

static void rk628_combrxphy_set_sample_edge_mode(struct rk628 *rk628, int ch)
{
	u32 val;

	rk628_i2c_read(rk628, COMBRX_REG(0x6634), &val);
	val = val & (~(0xf << ((ch + 1) * 4)));
	rk628_i2c_write(rk628, COMBRX_REG(0x6634), val);
}

static void rk628_combrxphy_select_channel(struct rk628 *rk628, int ch)
{
	u32 val;

	rk628_i2c_read(rk628, COMBRX_REG(0x6700), &val);
	val = (val & 0xfffffffc) | (ch & 0x3);
	rk628_i2c_write(rk628, COMBRX_REG(0x6700), val);
}

static void rk628_combrxphy_cfg_6730(struct rk628 *rk628)
{
	u32 val;

	rk628_i2c_read(rk628, COMBRX_REG(0x6730), &val);
	val = (val & 0xffff0000) | 0x1;
	rk628_i2c_write(rk628, COMBRX_REG(0x6730), val);
}

static void rk628_combrxphy_sample_edge_procedure_for_cable(struct rk628 *rk628, u32 cdr_mode)
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
		rk628_combrxphy_set_sample_edge_mode(rk628, ch);

	/* step2: once per round */
	for (ch = 0; ch < MAX_CHANNEL; ch++) {
		rk628_combrxphy_select_channel(rk628, ch);
		rk628_combrxphy_cfg_6730(rk628);
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
	} else {         /* unkonw vic16 1920x1080P60 */
		edge = 2200 * 1125;
	}

	dev_info(rk628->dev, "cdr_mode:%d, dc_gain:%d, rd_offset:%d, edge:%#x\n",
		 cdr_mode, dc_gain, rd_offset, edge);
	for (ch = 0; ch < MAX_CHANNEL; ch++) {
		rk628_combrxphy_select_channel(rk628, ch);
		rk628_i2c_write(rk628, COMBRX_REG(0x6708), edge);
	}

	rk628_combrxphy_set_dc_gain(rk628, dc_gain, dc_gain, dc_gain);
	for (n = rd_offset; n < (rd_offset + MAX_ROUND); n++) {
		/* step4:set sample edge round value n,n=0(n=0~31) */
		rk628_combrxphy_set_sample_edge_round(rk628, n, n, n);
		/* step5:start sample edge */
		rk628_combrxphy_start_sample_edge(rk628);
		/* step6:waiting more than one frame time */
		mdelay(41);
		for (ch = 0; ch < MAX_CHANNEL; ch++) {
			/* step7: get data of round n */
			rk628_combrxphy_select_channel(rk628, ch);
			rk628_combrxphy_get_data_of_round(rk628, data);
			rk628_combrxphy_set_data_of_round(data, data_in);
			/* step8: get the max constant value of round n */
			rk628_combrxphy_max_zero_of_round(rk628, data_in,
			round_max_zero[ch], round_max_value[ch],
				n - rd_offset, ch);
		}
	}

	/* step9: after finish round, get the max constant value and
	 * corresponding value n.
	 */
	for (ch = 0; ch < MAX_CHANNEL; ch++) {
		ch_round[ch] =
			rk628_combrxphy_chose_round_for_ch(rk628, round_max_zero[ch],
							   round_max_value[ch], ch) + rd_offset;
	}
	dev_info(rk628->dev, "last equ gain ch0:%d, ch1:%d, ch2:%d\n",
		 ch_round[0], ch_round[1], ch_round[2]);

	/* step10: write result to sample edge round value  */
	rk628_combrxphy_set_sample_edge_round(rk628, ch_round[0], ch_round[1], ch_round[2]);

	/* do step5, step6 again */
	/* step5:start sample edge */
	rk628_combrxphy_start_sample_edge(rk628);
	/* step6:waiting more than one frame time */
	mdelay(41);
}

static int rk628_combrxphy_set_hdmi_mode_for_cable(struct rk628 *rk628, int f)
{
	u32 val, val_a, val_b, data_a, data_b;
	u32 i, j, count, ret;
	u32 cdr_mode, cdr_data, pll_man;
	u32 tmds_bitrate_per_lane;
	u32 cdr_data_min, cdr_data_max;
	u32 temp = 0;
	u32 state, channel_st;

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
		119000,  135000, 148500, 162000, 297000,
	};

	for (i = 0; i < CLK_DET_TRY_TIMES; i++) {
		rk628_i2c_read(rk628, COMBRX_REG(0x6620), &val);
		if (!temp && val) {
			temp = val;
			msleep(200);
		}
		if (rk628_combrxphy_try_clk_detect(rk628) >= 0)
			break;
		mdelay(1);
	}
	rk628_i2c_read(rk628, COMBRX_REG(0x6654), &val);
	dev_info(rk628->dev, "clk det over cnt:%d, reg_0x6654:%#x\n", i, val);
	state = (val >> 28) & 0xf;
	if (state == 5) {
		dev_info(rk628->dev, "Clock detection anomaly\n");
	} else if (state == 4) {
		channel_st = (val >> 21) & 0x7f;
		dev_info(rk628->dev, "%s%s%s%s%s%s%s%s level detection anomaly\n",
			 channel_st & 0x40 ? "|clk_p|" : "",
			 channel_st & 0x20 ? "|clk_n|" : "",
			 channel_st & 0x10 ? "|d0_p|" : "",
			 channel_st & 0x08 ? "|d0_n|" : "",
			 channel_st & 0x04 ? "|d1_p|" : "",
			 channel_st & 0x02 ? "|d1_n|" : "",
			 channel_st & 0x01 ? "|d2_p|" : "",
			 channel_st ? "" : "|d2_n|");
	}

	rk628_i2c_read(rk628, COMBRX_REG(0x6620), &val);
	if ((i == CLK_DET_TRY_TIMES) ||
	    ((val & 0x7f000000) == 0) ||
	    ((val & 0x007f0000) == 0) ||
	    ((val & 0x00007f00) == 0) ||
	    ((val & 0x0000007f) == 0)) {
		dev_info(rk628->dev, "clock detected failed, cfg resistance manual!\n");
		rk628_i2c_write(rk628, COMBRX_REG(0x6620), 0x66666666);
		rk628_i2c_update_bits(rk628, COMBRX_REG(0x6604), BIT(31), BIT(31));
		mdelay(1);
	}

	/* step4: get cdr_mode and cdr_data */
	for (j = 0; j < CLK_STABLE_LOOP_CNT ; j++) {
		cdr_data_min = 0xffffffff;
		cdr_data_max = 0;

		for (i = 0; i < CLK_DET_TRY_TIMES; i++) {
			rk628_i2c_read(rk628, COMBRX_REG(0x6654), &val);
			cdr_data = val & 0xffff;
			if (cdr_data <= cdr_data_min)
				cdr_data_min = cdr_data;
			if (cdr_data >= cdr_data_max)
				cdr_data_max = cdr_data;
			udelay(50);
		}

		if (((cdr_data_max - cdr_data_min) <= CLK_STABLE_THRESHOLD) &&
				(cdr_data_min >= 60)) {
			dev_info(rk628->dev, "clock stable!");
			break;
		}
	}

	if (j == CLK_STABLE_LOOP_CNT) {
		rk628_i2c_read(rk628, COMBRX_REG(0x6630), &val_a);
		rk628_i2c_read(rk628, COMBRX_REG(0x6608), &val_b);
		dev_err(rk628->dev,
			"clk not stable, reg_0x6630:%#x, reg_0x6608:%#x",
			val_a, val_b);
		/* bypass level detection anomaly */
		if (state == 4)
			rk628_i2c_update_bits(rk628, COMBRX_REG(0x6628), BIT(31), BIT(31));
		else
			return -EINVAL;
	}

	rk628_i2c_read(rk628, COMBRX_REG(0x6654), &val);
	if ((val & 0x1f0000) == 0x1f0000) {
		rk628_i2c_read(rk628, COMBRX_REG(0x6630), &val_a);
		rk628_i2c_read(rk628, COMBRX_REG(0x6608), &val_b);
		dev_err(rk628->dev,
			"clock error: 0x1f, reg_0x6630:%#x, reg_0x6608:%#x",
			val_a, val_b);

		return -EINVAL;
	}

	cdr_mode = (val >> 16) & 0x1f;
	cdr_data =  val & 0xffff;
	dev_info(rk628->dev, "cdr_mode:%d, cdr_data:%d\n", cdr_mode, cdr_data);

	/* step5: manually configure PLL
	 * cfg reg 66a8 tmds clock div2 for rgb/yuv444 as default
	 * reg 662c[16:8] pll_pre_div
	 */
	if (f <= 340000) {
		rk628_i2c_write(rk628, COMBRX_REG(0x662c), 0x01000500);
		rk628_i2c_write(rk628, COMBRX_REG(0x66a8), 0x0000c600);
	} else {
		rk628_i2c_write(rk628, COMBRX_REG(0x662c), 0x01001400);
		rk628_i2c_write(rk628, COMBRX_REG(0x66a8), 0x0000c600);
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

	dev_info(rk628->dev, "cdr_mode:%d, pll_man:%#x\n", cdr_mode, pll_man);
	rk628_i2c_write(rk628, COMBRX_REG(0x6630), pll_man);

	/* step6: EQ and SAMPLE cfg */
	rk628_combrxphy_sample_edge_procedure_for_cable(rk628, cdr_mode);

	/* step7: Deassert fifo reset,enable fifo write and read */
	/* reset rx_infifo */
	rk628_i2c_write(rk628, COMBRX_REG(0x66a0), 0x00000003);
	/* rx_infofo wr/rd disable */
	rk628_i2c_write(rk628, COMBRX_REG(0x66b0), 0x00080060);
	/* deassert rx_infifo reset */
	rk628_i2c_write(rk628, COMBRX_REG(0x66a0), 0x00000083);
	/* enable rx_infofo wr/rd en */
	rk628_i2c_write(rk628, COMBRX_REG(0x66b0), 0x00380060);
	/* cfg 0x2260 high_8b to 0x66ac high_8b, low_8b to 0x66b0 low_8b */
	rk628_i2c_update_bits(rk628, COMBRX_REG(0x66ac), GENMASK(31, 24), UPDATE(0x22, 31, 24));
	mdelay(6);

	/* step8: check all 3 data channels alignment */
	count = 0;
	for (i = 0; i < CHECK_CNT; i++) {
		mdelay(1);
		rk628_i2c_read(rk628, COMBRX_REG(0x66b4), &data_a);
		rk628_i2c_read(rk628, COMBRX_REG(0x66b8), &data_b);
		/* ch0 ch1 ch2 lock */
		if (((data_a & 0x00ff00ff) == 0x00ff00ff) &&
			((data_b & 0xff) == 0xff)) {
			count++;
		}
	}

	if (count >= CHECK_CNT) {
		dev_info(rk628->dev, "channel alignment done\n");
		dev_info(rk628->dev, "rx initial done\n");
		ret = 0;
	} else if (count > 0) {
		dev_info(rk628->dev, "link not stable, count:%d of 100\n", count);
		ret = 0;
	} else {
		dev_err(rk628->dev, "channel alignment failed!\n");
		ret = -EINVAL;
	}

	return ret;
}

int rk628_rxphy_power_on(struct rk628 *rk628, int f)
{
	rk628_control_assert(rk628, RGU_RXPHY);
	udelay(10);
	rk628_control_deassert(rk628, RGU_RXPHY);
	udelay(10);

	f = f & 0x7fffffff;
	return rk628_combrxphy_set_hdmi_mode_for_cable(rk628, f);
}
EXPORT_SYMBOL(rk628_rxphy_power_on);

int rk628_rxphy_power_off(struct rk628 *rk628)
{
	rk628_i2c_update_bits(rk628, COMBRX_REG(0x6630), BIT(0), BIT(0));
	rk628_control_assert(rk628, RGU_RXPHY);
	udelay(10);

	return 0;
}
EXPORT_SYMBOL(rk628_rxphy_power_off);
