/*
 * Copyright 2005-2015 Freescale Semiconductor, Inc. All Rights Reserved.
 */

/*
 * The code contained herein is licensed under the GNU General Public
 * License. You may obtain a copy of the GNU General Public License
 * Version 2 or later at the following locations:
 *
 * http://www.opensource.org/licenses/gpl-license.html
 * http://www.gnu.org/copyleft/gpl.html
 */

/*!
 * @file ipu_disp.c
 *
 * @brief IPU display submodule API functions
 *
 * @ingroup IPU
 */

#include <linux/clk.h>
#include <linux/clk-provider.h>
#include <linux/delay.h>
#include <linux/err.h>
#include <linux/errno.h>
#include <linux/io.h>
#include <linux/ipu-v3.h>
#include <linux/module.h>
#include <linux/spinlock.h>
#include <linux/types.h>

#include <asm/atomic.h>

#include "ipu_param_mem.h"
#include "ipu_regs.h"

struct dp_csc_param_t {
	int mode;
	void *coeff;
};

#define SYNC_WAVE 0
#define NULL_WAVE (-1)
#define ASYNC_SER_WAVE 6

/* DC display ID assignments */
#define DC_DISP_ID_SYNC(di)	(di)
#define DC_DISP_ID_SERIAL	2
#define DC_DISP_ID_ASYNC	3

int dmfc_type_setup;

void _ipu_dmfc_init(struct ipu_soc *ipu, int dmfc_type, int first)
{
	u32 dmfc_wr_chan, dmfc_dp_chan;

	if (first) {
		if (dmfc_type_setup > dmfc_type)
			dmfc_type = dmfc_type_setup;
		else
			dmfc_type_setup = dmfc_type;

		/* disable DMFC-IC channel*/
		ipu_dmfc_write(ipu, 0x2, DMFC_IC_CTRL);
	} else if (dmfc_type_setup >= DMFC_HIGH_RESOLUTION_DC) {
		dev_dbg(ipu->dev, "DMFC high resolution has set, will not change\n");
		return;
	} else
		dmfc_type_setup = dmfc_type;

	if (dmfc_type == DMFC_HIGH_RESOLUTION_DC) {
		/* 1 - segment 0~3;
		 * 5B - segement 4, 5;
		 * 5F - segement 6, 7;
		 * 1C, 2C and 6B, 6F unused;
		 */
		dev_info(ipu->dev, "IPU DMFC DC HIGH RESOLUTION: 1(0~3), 5B(4,5), 5F(6,7)\n");
		dmfc_wr_chan = 0x00000088;
		dmfc_dp_chan = 0x00009694;
		ipu->dmfc_size_28 = 256*4;
		ipu->dmfc_size_29 = 0;
		ipu->dmfc_size_24 = 0;
		ipu->dmfc_size_27 = 128*4;
		ipu->dmfc_size_23 = 128*4;
	} else if (dmfc_type == DMFC_HIGH_RESOLUTION_DP) {
		/* 1 - segment 0, 1;
		 * 5B - segement 2~5;
		 * 5F - segement 6,7;
		 * 1C, 2C and 6B, 6F unused;
		 */
		dev_info(ipu->dev, "IPU DMFC DP HIGH RESOLUTION: 1(0,1), 5B(2~5), 5F(6,7)\n");
		dmfc_wr_chan = 0x00000090;
		dmfc_dp_chan = 0x0000968a;
		ipu->dmfc_size_28 = 128*4;
		ipu->dmfc_size_29 = 0;
		ipu->dmfc_size_24 = 0;
		ipu->dmfc_size_27 = 128*4;
		ipu->dmfc_size_23 = 256*4;
	} else if (dmfc_type == DMFC_HIGH_RESOLUTION_ONLY_DP) {
		/* 5B - segement 0~3;
		 * 5F - segement 4~7;
		 * 1, 1C, 2C and 6B, 6F unused;
		 */
		dev_info(ipu->dev, "IPU DMFC ONLY-DP HIGH RESOLUTION: 5B(0~3), 5F(4~7)\n");
		dmfc_wr_chan = 0x00000000;
		dmfc_dp_chan = 0x00008c88;
		ipu->dmfc_size_28 = 0;
		ipu->dmfc_size_29 = 0;
		ipu->dmfc_size_24 = 0;
		ipu->dmfc_size_27 = 256*4;
		ipu->dmfc_size_23 = 256*4;
	} else {
		/* 1 - segment 0, 1;
		 * 5B - segement 4, 5;
		 * 5F - segement 6, 7;
		 * 1C, 2C and 6B, 6F unused;
		 */
		dev_info(ipu->dev, "IPU DMFC NORMAL mode: 1(0~1), 5B(4,5), 5F(6,7)\n");
		dmfc_wr_chan = 0x00000090;
		dmfc_dp_chan = 0x00009694;
		ipu->dmfc_size_28 = 128*4;
		ipu->dmfc_size_29 = 0;
		ipu->dmfc_size_24 = 0;
		ipu->dmfc_size_27 = 128*4;
		ipu->dmfc_size_23 = 128*4;
	}
	ipu_dmfc_write(ipu, dmfc_wr_chan, DMFC_WR_CHAN);
	ipu_dmfc_write(ipu, 0x202020F6, DMFC_WR_CHAN_DEF);
	ipu_dmfc_write(ipu, dmfc_dp_chan, DMFC_DP_CHAN);
	/* Enable chan 5 watermark set at 5 bursts and clear at 7 bursts */
	ipu_dmfc_write(ipu, 0x2020F6F6, DMFC_DP_CHAN_DEF);
}

static int __init dmfc_setup(char *options)
{
	get_option(&options, &dmfc_type_setup);
	if (dmfc_type_setup > DMFC_HIGH_RESOLUTION_ONLY_DP)
		dmfc_type_setup = DMFC_HIGH_RESOLUTION_ONLY_DP;
	return 1;
}
__setup("dmfc=", dmfc_setup);

void _ipu_dmfc_set_wait4eot(struct ipu_soc *ipu, int dma_chan, int width)
{
	u32 dmfc_gen1 = ipu_dmfc_read(ipu, DMFC_GENERAL1);

	if (width >= HIGH_RESOLUTION_WIDTH) {
		if (dma_chan == 23)
			_ipu_dmfc_init(ipu, DMFC_HIGH_RESOLUTION_DP, 0);
		else if (dma_chan == 28)
			_ipu_dmfc_init(ipu, DMFC_HIGH_RESOLUTION_DC, 0);
	}

	if (dma_chan == 23) { /*5B*/
		if (ipu->dmfc_size_23/width > 3)
			dmfc_gen1 |= 1UL << 20;
		else
			dmfc_gen1 &= ~(1UL << 20);
	} else if (dma_chan == 24) { /*6B*/
		if (ipu->dmfc_size_24/width > 1)
			dmfc_gen1 |= 1UL << 22;
		else
			dmfc_gen1 &= ~(1UL << 22);
	} else if (dma_chan == 27) { /*5F*/
		if (ipu->dmfc_size_27/width > 2)
			dmfc_gen1 |= 1UL << 21;
		else
			dmfc_gen1 &= ~(1UL << 21);
	} else if (dma_chan == 28) { /*1*/
		if (ipu->dmfc_size_28/width > 2)
			dmfc_gen1 |= 1UL << 16;
		else
			dmfc_gen1 &= ~(1UL << 16);
	} else if (dma_chan == 29) { /*6F*/
		if (ipu->dmfc_size_29/width > 1)
			dmfc_gen1 |= 1UL << 23;
		else
			dmfc_gen1 &= ~(1UL << 23);
	}

	ipu_dmfc_write(ipu, dmfc_gen1, DMFC_GENERAL1);
}

void _ipu_dmfc_set_burst_size(struct ipu_soc *ipu, int dma_chan, int burst_size)
{
	u32 dmfc_wr_chan = ipu_dmfc_read(ipu, DMFC_WR_CHAN);
	u32 dmfc_dp_chan = ipu_dmfc_read(ipu, DMFC_DP_CHAN);
	int dmfc_bs = 0;

	switch (burst_size) {
	case 64:
		dmfc_bs = 0x40;
		break;
	case 32:
	case 20:
		dmfc_bs = 0x80;
		break;
	case 16:
		dmfc_bs = 0xc0;
		break;
	default:
		dev_err(ipu->dev, "Unsupported burst size %d\n",
			burst_size);
		return;
	}

	if (dma_chan == 23) { /*5B*/
		dmfc_dp_chan &= ~(0xc0);
		dmfc_dp_chan |= dmfc_bs;
	} else if (dma_chan == 27) { /*5F*/
		dmfc_dp_chan &= ~(0xc000);
		dmfc_dp_chan |= (dmfc_bs << 8);
	} else if (dma_chan == 28) { /*1*/
		dmfc_wr_chan &= ~(0xc0);
		dmfc_wr_chan |= dmfc_bs;
	}

	ipu_dmfc_write(ipu, dmfc_wr_chan, DMFC_WR_CHAN);
	ipu_dmfc_write(ipu, dmfc_dp_chan, DMFC_DP_CHAN);
}

static void _ipu_di_data_wave_config(struct ipu_soc *ipu,
				int di, int wave_gen,
				int access_size, int component_size)
{
	u32 reg;
	reg = (access_size << DI_DW_GEN_ACCESS_SIZE_OFFSET) |
	    (component_size << DI_DW_GEN_COMPONENT_SIZE_OFFSET);
	ipu_di_write(ipu, di, reg, DI_DW_GEN(wave_gen));
}

static void _ipu_di_data_pin_config(struct ipu_soc *ipu,
			int di, int wave_gen, int di_pin, int set,
			int up, int down)
{
	u32 reg;

	reg = ipu_di_read(ipu, di, DI_DW_GEN(wave_gen));
	reg &= ~(0x3 << (di_pin * 2));
	reg |= set << (di_pin * 2);
	ipu_di_write(ipu, di, reg, DI_DW_GEN(wave_gen));

	ipu_di_write(ipu, di, (down << 16) | up, DI_DW_SET(wave_gen, set));
}

static void _ipu_di_sync_config(struct ipu_soc *ipu,
				int di, int wave_gen,
				int run_count, int run_src,
				int offset_count, int offset_src,
				int repeat_count, int cnt_clr_src,
				int cnt_polarity_gen_en,
				int cnt_polarity_clr_src,
				int cnt_polarity_trigger_src,
				int cnt_up, int cnt_down)
{
	u32 reg;

	if ((run_count >= 0x1000) || (offset_count >= 0x1000) || (repeat_count >= 0x1000) ||
		(cnt_up >= 0x400) || (cnt_down >= 0x400)) {
		dev_err(ipu->dev, "DI%d counters out of range.\n", di);
		return;
	}

	reg = (run_count << 19) | (++run_src << 16) |
	    (offset_count << 3) | ++offset_src;
	ipu_di_write(ipu, di, reg, DI_SW_GEN0(wave_gen));
	reg = (cnt_polarity_gen_en << 29) | (++cnt_clr_src << 25) |
	    (++cnt_polarity_trigger_src << 12) | (++cnt_polarity_clr_src << 9);
	reg |= (cnt_down << 16) | cnt_up;
	if (repeat_count == 0) {
		/* Enable auto reload */
		reg |= 0x10000000;
	}
	ipu_di_write(ipu, di, reg, DI_SW_GEN1(wave_gen));
	reg = ipu_di_read(ipu, di, DI_STP_REP(wave_gen));
	reg &= ~(0xFFFF << (16 * ((wave_gen - 1) & 0x1)));
	reg |= repeat_count << (16 * ((wave_gen - 1) & 0x1));
	ipu_di_write(ipu, di, reg, DI_STP_REP(wave_gen));
}

static void _ipu_dc_map_link(struct ipu_soc *ipu,
		int current_map,
		int base_map_0, int buf_num_0,
		int base_map_1, int buf_num_1,
		int base_map_2, int buf_num_2)
{
	int ptr_0 = base_map_0 * 3 + buf_num_0;
	int ptr_1 = base_map_1 * 3 + buf_num_1;
	int ptr_2 = base_map_2 * 3 + buf_num_2;
	int ptr;
	u32 reg;
	ptr = (ptr_2 << 10) +  (ptr_1 << 5) + ptr_0;

	reg = ipu_dc_read(ipu, DC_MAP_CONF_PTR(current_map));
	reg &= ~(0x1F << ((16 * (current_map & 0x1))));
	reg |= ptr << ((16 * (current_map & 0x1)));
	ipu_dc_write(ipu, reg, DC_MAP_CONF_PTR(current_map));
}

static void _ipu_dc_map_config(struct ipu_soc *ipu,
		int map, int byte_num, int offset, int mask)
{
	int ptr = map * 3 + byte_num;
	u32 reg;

	reg = ipu_dc_read(ipu, DC_MAP_CONF_VAL(ptr));
	reg &= ~(0xFFFF << (16 * (ptr & 0x1)));
	reg |= ((offset << 8) | mask) << (16 * (ptr & 0x1));
	ipu_dc_write(ipu, reg, DC_MAP_CONF_VAL(ptr));

	reg = ipu_dc_read(ipu, DC_MAP_CONF_PTR(map));
	reg &= ~(0x1F << ((16 * (map & 0x1)) + (5 * byte_num)));
	reg |= ptr << ((16 * (map & 0x1)) + (5 * byte_num));
	ipu_dc_write(ipu, reg, DC_MAP_CONF_PTR(map));
}

static void _ipu_dc_map_clear(struct ipu_soc *ipu, int map)
{
	u32 reg = ipu_dc_read(ipu, DC_MAP_CONF_PTR(map));
	ipu_dc_write(ipu, reg & ~(0xFFFF << (16 * (map & 0x1))),
		     DC_MAP_CONF_PTR(map));
}

static void _ipu_dc_write_tmpl(struct ipu_soc *ipu,
			int word, u32 opcode, u32 operand, int map,
			int wave, int glue, int sync, int stop)
{
	u32 reg;

	if (opcode == WRG) {
		reg = sync;
		reg |= (glue << 4);
		reg |= (++wave << 11);
		reg |= ((operand & 0x1FFFF) << 15);
		ipu_dc_tmpl_write(ipu, reg, word * 8);

		reg = (operand >> 17);
		reg |= opcode << 7;
		reg |= (stop << 9);
		ipu_dc_tmpl_write(ipu, reg, word * 8 + 4);
	} else {
		reg = sync;
		reg |= (glue << 4);
		reg |= (++wave << 11);
		reg |= (++map << 15);
		reg |= (operand << 20) & 0xFFF00000;
		ipu_dc_tmpl_write(ipu, reg, word * 8);

		reg = (operand >> 12);
		reg |= opcode << 4;
		reg |= (stop << 9);
		ipu_dc_tmpl_write(ipu, reg, word * 8 + 4);
	}
}

static void _ipu_dc_link_event(struct ipu_soc *ipu,
		int chan, int event, int addr, int priority)
{
	u32 reg;
	u32 address_shift;
	if (event < DC_EVEN_UGDE0) {
		reg = ipu_dc_read(ipu, DC_RL_CH(chan, event));
		reg &= ~(0xFFFF << (16 * (event & 0x1)));
		reg |= ((addr << 8) | priority) << (16 * (event & 0x1));
		ipu_dc_write(ipu, reg, DC_RL_CH(chan, event));
	} else {
		reg = ipu_dc_read(ipu, DC_UGDE_0((event - DC_EVEN_UGDE0) / 2));
		if ((event - DC_EVEN_UGDE0) & 0x1) {
			reg &= ~(0x2FF << 16);
			reg |= (addr << 16);
			reg |= priority ? (2 << 24) : 0x0;
		} else {
			reg &= ~0xFC00FFFF;
			if (priority)
				chan = (chan >> 1) +
					((((chan & 0x1) + ((chan & 0x2) >> 1))) | (chan >> 3));
			else
				chan = 0x7;
			address_shift = ((event - DC_EVEN_UGDE0) >> 1) ? 7 : 8;
			reg |= (addr << address_shift) | (priority << 3) | chan;
		}
		ipu_dc_write(ipu, reg, DC_UGDE_0((event - DC_EVEN_UGDE0) / 2));
	}
}

/*     Y = R *  1.200 + G *  2.343 + B *  .453 + 0.250;
       U = R * -.672 + G * -1.328 + B *  2.000 + 512.250.;
       V = R *  2.000 + G * -1.672 + B * -.328 + 512.250.;*/
static const int rgb2ycbcr_coeff[5][3] = {
	{0x4D, 0x96, 0x1D},
	{-0x2B, -0x55, 0x80},
	{0x80, -0x6B, -0x15},
	{0x0000, 0x0200, 0x0200},	/* B0, B1, B2 */
	{0x2, 0x2, 0x2},	/* S0, S1, S2 */
};

/*     R = (1.164 * (Y - 16)) + (1.596 * (Cr - 128));
       G = (1.164 * (Y - 16)) - (0.392 * (Cb - 128)) - (0.813 * (Cr - 128));
       B = (1.164 * (Y - 16)) + (2.017 * (Cb - 128); */
static const int ycbcr2rgb_coeff[5][3] = {
	{0x095, 0x000, 0x0CC},
	{0x095, 0x3CE, 0x398},
	{0x095, 0x0FF, 0x000},
	{0x3E42, 0x010A, 0x3DD6},	/*B0,B1,B2 */
	{0x1, 0x1, 0x1},	/*S0,S1,S2 */
};

#define mask_a(a) ((u32)(a) & 0x3FF)
#define mask_b(b) ((u32)(b) & 0x3FFF)

/* Pls keep S0, S1 and S2 as 0x2 by using this convertion */
static int _rgb_to_yuv(int n, int red, int green, int blue)
{
	int c;
	c = red * rgb2ycbcr_coeff[n][0];
	c += green * rgb2ycbcr_coeff[n][1];
	c += blue * rgb2ycbcr_coeff[n][2];
	c /= 16;
	c += rgb2ycbcr_coeff[3][n] * 4;
	c += 8;
	c /= 16;
	if (c < 0)
		c = 0;
	if (c > 255)
		c = 255;
	return c;
}

/*
 * Row is for BG: 	RGB2YUV YUV2RGB RGB2RGB YUV2YUV CSC_NONE
 * Column is for FG:	RGB2YUV YUV2RGB RGB2RGB YUV2YUV CSC_NONE
 */
static struct dp_csc_param_t dp_csc_array[CSC_NUM][CSC_NUM] = {
{
	{DP_COM_CONF_CSC_DEF_BOTH, (void *)&rgb2ycbcr_coeff},
	{0, 0}, {0, 0},
	{DP_COM_CONF_CSC_DEF_BG, (void *)&rgb2ycbcr_coeff},
	{DP_COM_CONF_CSC_DEF_BG, (void *)&rgb2ycbcr_coeff}
},
{
	{0, 0},
	{DP_COM_CONF_CSC_DEF_BOTH, (void *)&ycbcr2rgb_coeff},
	{DP_COM_CONF_CSC_DEF_BG, (void *)&ycbcr2rgb_coeff},
	{0, 0},
	{DP_COM_CONF_CSC_DEF_BG, (void *)&ycbcr2rgb_coeff}
},
{
	{0, 0},
	{DP_COM_CONF_CSC_DEF_FG, (void *)&ycbcr2rgb_coeff},
	{0, 0}, {0, 0}, {0, 0}
},
{
	{DP_COM_CONF_CSC_DEF_FG, (void *)&rgb2ycbcr_coeff},
	{0, 0}, {0, 0}, {0, 0}, {0, 0}
},
{
	{DP_COM_CONF_CSC_DEF_FG, (void *)&rgb2ycbcr_coeff},
	{DP_COM_CONF_CSC_DEF_FG, (void *)&ycbcr2rgb_coeff},
	{0, 0}, {0, 0}, {0, 0}
}
};

void __ipu_dp_csc_setup(struct ipu_soc *ipu,
		int dp, struct dp_csc_param_t dp_csc_param,
		bool srm_mode_update)
{
	u32 reg;
	const int (*coeff)[5][3];

	if (dp_csc_param.mode >= 0) {
		reg = ipu_dp_read(ipu, DP_COM_CONF(dp));
		reg &= ~DP_COM_CONF_CSC_DEF_MASK;
		reg |= dp_csc_param.mode;
		ipu_dp_write(ipu, reg, DP_COM_CONF(dp));
	}

	coeff = dp_csc_param.coeff;

	if (coeff) {
		ipu_dp_write(ipu, mask_a((*coeff)[0][0]) |
				(mask_a((*coeff)[0][1]) << 16), DP_CSC_A_0(dp));
		ipu_dp_write(ipu, mask_a((*coeff)[0][2]) |
				(mask_a((*coeff)[1][0]) << 16), DP_CSC_A_1(dp));
		ipu_dp_write(ipu, mask_a((*coeff)[1][1]) |
				(mask_a((*coeff)[1][2]) << 16), DP_CSC_A_2(dp));
		ipu_dp_write(ipu, mask_a((*coeff)[2][0]) |
				(mask_a((*coeff)[2][1]) << 16), DP_CSC_A_3(dp));
		ipu_dp_write(ipu, mask_a((*coeff)[2][2]) |
				(mask_b((*coeff)[3][0]) << 16) |
				((*coeff)[4][0] << 30), DP_CSC_0(dp));
		ipu_dp_write(ipu, mask_b((*coeff)[3][1]) | ((*coeff)[4][1] << 14) |
				(mask_b((*coeff)[3][2]) << 16) |
				((*coeff)[4][2] << 30), DP_CSC_1(dp));
	}

	if (srm_mode_update) {
		reg = ipu_cm_read(ipu, IPU_SRM_PRI2) | 0x8;
		ipu_cm_write(ipu, reg, IPU_SRM_PRI2);
	}
}

int _ipu_dp_init(struct ipu_soc *ipu,
		ipu_channel_t channel, uint32_t in_pixel_fmt,
		uint32_t out_pixel_fmt)
{
	int in_fmt, out_fmt;
	int dp;
	int partial = false;
	uint32_t reg;

	if (channel == MEM_FG_SYNC) {
		dp = DP_SYNC;
		partial = true;
	} else if (channel == MEM_BG_SYNC) {
		dp = DP_SYNC;
		partial = false;
	} else if (channel == MEM_BG_ASYNC0) {
		dp = DP_ASYNC0;
		partial = false;
	} else {
		return -EINVAL;
	}

	in_fmt = format_to_colorspace(in_pixel_fmt);
	out_fmt = format_to_colorspace(out_pixel_fmt);

	if (partial) {
		if (in_fmt == RGB) {
			if (out_fmt == RGB)
				ipu->fg_csc_type = RGB2RGB;
			else
				ipu->fg_csc_type = RGB2YUV;
		} else {
			if (out_fmt == RGB)
				ipu->fg_csc_type = YUV2RGB;
			else
				ipu->fg_csc_type = YUV2YUV;
		}
	} else {
		if (in_fmt == RGB) {
			if (out_fmt == RGB)
				ipu->bg_csc_type = RGB2RGB;
			else
				ipu->bg_csc_type = RGB2YUV;
		} else {
			if (out_fmt == RGB)
				ipu->bg_csc_type = YUV2RGB;
			else
				ipu->bg_csc_type = YUV2YUV;
		}
	}

	/* Transform color key from rgb to yuv if CSC is enabled */
	reg = ipu_dp_read(ipu, DP_COM_CONF(dp));
	if (ipu->color_key_4rgb && (reg & DP_COM_CONF_GWCKE) &&
			(((ipu->fg_csc_type == RGB2YUV) && (ipu->bg_csc_type == YUV2YUV)) ||
			 ((ipu->fg_csc_type == YUV2YUV) && (ipu->bg_csc_type == RGB2YUV)) ||
			 ((ipu->fg_csc_type == YUV2YUV) && (ipu->bg_csc_type == YUV2YUV)) ||
			 ((ipu->fg_csc_type == YUV2RGB) && (ipu->bg_csc_type == YUV2RGB)))) {
		int red, green, blue;
		int y, u, v;
		uint32_t color_key = ipu_dp_read(ipu, DP_GRAPH_WIND_CTRL(dp)) & 0xFFFFFFL;

		dev_dbg(ipu->dev, "_ipu_dp_init color key 0x%x need change to yuv fmt!\n", color_key);

		red = (color_key >> 16) & 0xFF;
		green = (color_key >> 8) & 0xFF;
		blue = color_key & 0xFF;

		y = _rgb_to_yuv(0, red, green, blue);
		u = _rgb_to_yuv(1, red, green, blue);
		v = _rgb_to_yuv(2, red, green, blue);
		color_key = (y << 16) | (u << 8) | v;

		reg = ipu_dp_read(ipu, DP_GRAPH_WIND_CTRL(dp)) & 0xFF000000L;
		ipu_dp_write(ipu, reg | color_key, DP_GRAPH_WIND_CTRL(dp));
		ipu->color_key_4rgb = false;

		dev_dbg(ipu->dev, "_ipu_dp_init color key change to yuv fmt 0x%x!\n", color_key);
	}

	__ipu_dp_csc_setup(ipu, dp,
			   dp_csc_array[ipu->bg_csc_type][ipu->fg_csc_type],
			   false);

	return 0;
}

void _ipu_dp_uninit(struct ipu_soc *ipu, ipu_channel_t channel)
{
	int dp;
	int partial = false;

	if (channel == MEM_FG_SYNC) {
		dp = DP_SYNC;
		partial = true;
	} else if (channel == MEM_BG_SYNC) {
		dp = DP_SYNC;
		partial = false;
	} else if (channel == MEM_BG_ASYNC0) {
		dp = DP_ASYNC0;
		partial = false;
	} else {
		return;
	}

	if (partial)
		ipu->fg_csc_type = CSC_NONE;
	else
		ipu->bg_csc_type = CSC_NONE;

	__ipu_dp_csc_setup(ipu, dp, dp_csc_array[ipu->bg_csc_type][ipu->fg_csc_type], false);
}

void _ipu_dc_init(struct ipu_soc *ipu, int dc_chan, int di, bool interlaced, uint32_t pixel_fmt)
{
	u32 reg = 0;

	if ((dc_chan == 1) || (dc_chan == 5)) {
		if (interlaced) {
			_ipu_dc_link_event(ipu, dc_chan, DC_EVT_NL, 0, 3);
			_ipu_dc_link_event(ipu, dc_chan, DC_EVT_EOL, 0, 2);
			_ipu_dc_link_event(ipu, dc_chan, DC_EVT_NEW_DATA, 0, 1);
		} else {
			if (di) {
				_ipu_dc_link_event(ipu, dc_chan, DC_EVT_NL, 2, 3);
				_ipu_dc_link_event(ipu, dc_chan, DC_EVT_EOL, 3, 2);
				_ipu_dc_link_event(ipu, dc_chan, DC_EVT_NEW_DATA, 1, 1);
				if ((pixel_fmt == IPU_PIX_FMT_YUYV) ||
				(pixel_fmt == IPU_PIX_FMT_UYVY) ||
				(pixel_fmt == IPU_PIX_FMT_YVYU) ||
				(pixel_fmt == IPU_PIX_FMT_VYUY)) {
					_ipu_dc_link_event(ipu, dc_chan, DC_ODD_UGDE1, 9, 5);
					_ipu_dc_link_event(ipu, dc_chan, DC_EVEN_UGDE1, 8, 5);
				}
			} else {
				_ipu_dc_link_event(ipu, dc_chan, DC_EVT_NL, 5, 3);
				_ipu_dc_link_event(ipu, dc_chan, DC_EVT_EOL, 6, 2);
				_ipu_dc_link_event(ipu, dc_chan, DC_EVT_NEW_DATA, 12, 1);
				if ((pixel_fmt == IPU_PIX_FMT_YUYV) ||
				(pixel_fmt == IPU_PIX_FMT_UYVY) ||
				(pixel_fmt == IPU_PIX_FMT_YVYU) ||
				(pixel_fmt == IPU_PIX_FMT_VYUY)) {
					_ipu_dc_link_event(ipu, dc_chan, DC_ODD_UGDE0, 10, 5);
					_ipu_dc_link_event(ipu, dc_chan, DC_EVEN_UGDE0, 11, 5);
				}
			}
		}
		_ipu_dc_link_event(ipu, dc_chan, DC_EVT_NF, 0, 0);
		_ipu_dc_link_event(ipu, dc_chan, DC_EVT_NFIELD, 0, 0);
		_ipu_dc_link_event(ipu, dc_chan, DC_EVT_EOF, 0, 0);
		_ipu_dc_link_event(ipu, dc_chan, DC_EVT_EOFIELD, 0, 0);
		_ipu_dc_link_event(ipu, dc_chan, DC_EVT_NEW_CHAN, 0, 0);
		_ipu_dc_link_event(ipu, dc_chan, DC_EVT_NEW_ADDR, 0, 0);

		reg = 0x2;
		reg |= DC_DISP_ID_SYNC(di) << DC_WR_CH_CONF_PROG_DISP_ID_OFFSET;
		reg |= di << 2;
		if (interlaced)
			reg |= DC_WR_CH_CONF_FIELD_MODE;
	} else if ((dc_chan == 8) || (dc_chan == 9)) {
		/* async channels */
		_ipu_dc_link_event(ipu, dc_chan, DC_EVT_NEW_DATA_W_0, 0x64, 1);
		_ipu_dc_link_event(ipu, dc_chan, DC_EVT_NEW_DATA_W_1, 0x64, 1);

		reg = 0x3;
		reg |= DC_DISP_ID_SERIAL << DC_WR_CH_CONF_PROG_DISP_ID_OFFSET;
	}
	ipu_dc_write(ipu, reg, DC_WR_CH_CONF(dc_chan));

	ipu_dc_write(ipu, 0x00000000, DC_WR_CH_ADDR(dc_chan));

	ipu_dc_write(ipu, 0x00000084, DC_GEN);
}

void _ipu_dc_uninit(struct ipu_soc *ipu, int dc_chan)
{
	if ((dc_chan == 1) || (dc_chan == 5)) {
		_ipu_dc_link_event(ipu, dc_chan, DC_EVT_NL, 0, 0);
		_ipu_dc_link_event(ipu, dc_chan, DC_EVT_EOL, 0, 0);
		_ipu_dc_link_event(ipu, dc_chan, DC_EVT_NEW_DATA, 0, 0);
		_ipu_dc_link_event(ipu, dc_chan, DC_EVT_NF, 0, 0);
		_ipu_dc_link_event(ipu, dc_chan, DC_EVT_NFIELD, 0, 0);
		_ipu_dc_link_event(ipu, dc_chan, DC_EVT_EOF, 0, 0);
		_ipu_dc_link_event(ipu, dc_chan, DC_EVT_EOFIELD, 0, 0);
		_ipu_dc_link_event(ipu, dc_chan, DC_EVT_NEW_CHAN, 0, 0);
		_ipu_dc_link_event(ipu, dc_chan, DC_EVT_NEW_ADDR, 0, 0);
		_ipu_dc_link_event(ipu, dc_chan, DC_ODD_UGDE0, 0, 0);
		_ipu_dc_link_event(ipu, dc_chan, DC_EVEN_UGDE0, 0, 0);
		_ipu_dc_link_event(ipu, dc_chan, DC_ODD_UGDE1, 0, 0);
		_ipu_dc_link_event(ipu, dc_chan, DC_EVEN_UGDE1, 0, 0);
	} else if ((dc_chan == 8) || (dc_chan == 9)) {
		_ipu_dc_link_event(ipu, dc_chan, DC_EVT_NEW_ADDR_W_0, 0, 0);
		_ipu_dc_link_event(ipu, dc_chan, DC_EVT_NEW_ADDR_W_1, 0, 0);
		_ipu_dc_link_event(ipu, dc_chan, DC_EVT_NEW_CHAN_W_0, 0, 0);
		_ipu_dc_link_event(ipu, dc_chan, DC_EVT_NEW_CHAN_W_1, 0, 0);
		_ipu_dc_link_event(ipu, dc_chan, DC_EVT_NEW_DATA_W_0, 0, 0);
		_ipu_dc_link_event(ipu, dc_chan, DC_EVT_NEW_DATA_W_1, 0, 0);
		_ipu_dc_link_event(ipu, dc_chan, DC_EVT_NEW_ADDR_R_0, 0, 0);
		_ipu_dc_link_event(ipu, dc_chan, DC_EVT_NEW_ADDR_R_1, 0, 0);
		_ipu_dc_link_event(ipu, dc_chan, DC_EVT_NEW_CHAN_R_0, 0, 0);
		_ipu_dc_link_event(ipu, dc_chan, DC_EVT_NEW_CHAN_R_1, 0, 0);
		_ipu_dc_link_event(ipu, dc_chan, DC_EVT_NEW_DATA_R_0, 0, 0);
		_ipu_dc_link_event(ipu, dc_chan, DC_EVT_NEW_DATA_R_1, 0, 0);
	}
}

int _ipu_disp_chan_is_interlaced(struct ipu_soc *ipu, ipu_channel_t channel)
{
	if (channel == MEM_DC_SYNC)
		return !!(ipu_dc_read(ipu, DC_WR_CH_CONF_1) &
			  DC_WR_CH_CONF_FIELD_MODE);
	else if ((channel == MEM_BG_SYNC) || (channel == MEM_FG_SYNC))
		return !!(ipu_dc_read(ipu, DC_WR_CH_CONF_5) &
			  DC_WR_CH_CONF_FIELD_MODE);
	return 0;
}

void _ipu_dp_dc_enable(struct ipu_soc *ipu, ipu_channel_t channel)
{
	int di;
	uint32_t reg;
	uint32_t dc_chan;
	int irq = 0;

	if (channel == MEM_FG_SYNC)
		irq = IPU_IRQ_DP_SF_END;
	else if (channel == MEM_DC_SYNC)
		dc_chan = 1;
	else if (channel == MEM_BG_SYNC)
		dc_chan = 5;
	else
		return;

	if (channel == MEM_FG_SYNC) {
		/* Enable FG channel */
		reg = ipu_dp_read(ipu, DP_COM_CONF(DP_SYNC));
		ipu_dp_write(ipu, reg | DP_COM_CONF_FG_EN, DP_COM_CONF(DP_SYNC));

		reg = ipu_cm_read(ipu, IPU_SRM_PRI2) | 0x8;
		ipu_cm_write(ipu, reg, IPU_SRM_PRI2);
		return;
	} else if (channel == MEM_BG_SYNC) {
		reg = ipu_cm_read(ipu, IPU_SRM_PRI2) | 0x8;
		ipu_cm_write(ipu, reg, IPU_SRM_PRI2);
	}

	di = ipu->dc_di_assignment[dc_chan];

	/* Make sure other DC sync channel is not assigned same DI */
	reg = ipu_dc_read(ipu, DC_WR_CH_CONF(6 - dc_chan));
	if ((di << 2) == (reg & DC_WR_CH_CONF_PROG_DI_ID)) {
		reg &= ~DC_WR_CH_CONF_PROG_DI_ID;
		reg |= di ? 0 : DC_WR_CH_CONF_PROG_DI_ID;
		ipu_dc_write(ipu, reg, DC_WR_CH_CONF(6 - dc_chan));
	}

	reg = ipu_dc_read(ipu, DC_WR_CH_CONF(dc_chan));
	reg |= 4 << DC_WR_CH_CONF_PROG_TYPE_OFFSET;
	ipu_dc_write(ipu, reg, DC_WR_CH_CONF(dc_chan));

	clk_prepare_enable(ipu->pixel_clk[di]);
	ipu->pixel_clk_en[ipu->dc_di_assignment[dc_chan]] = true;
}

static irqreturn_t dc_irq_handler(int irq, void *dev_id)
{
	struct ipu_soc *ipu = dev_id;
	struct completion *comp = &ipu->dc_comp;
	uint32_t reg;
	uint32_t dc_chan;

	if (irq == IPU_IRQ_DC_FC_1)
		dc_chan = 1;
	else
		dc_chan = 5;

	if (!ipu->dc_swap) {
		reg = ipu_dc_read(ipu, DC_WR_CH_CONF(dc_chan));
		reg &= ~DC_WR_CH_CONF_PROG_TYPE_MASK;
		ipu_dc_write(ipu, reg, DC_WR_CH_CONF(dc_chan));

		reg = ipu_cm_read(ipu, IPU_DISP_GEN);
		if (ipu->dc_di_assignment[dc_chan])
			reg &= ~DI1_COUNTER_RELEASE;
		else
			reg &= ~DI0_COUNTER_RELEASE;
		ipu_cm_write(ipu, reg, IPU_DISP_GEN);
	}

	complete(comp);
	return IRQ_HANDLED;
}

void _ipu_dp_dc_disable(struct ipu_soc *ipu, ipu_channel_t channel, bool swap)
{
	int ret;
	uint32_t reg;
	uint32_t csc;
	uint32_t dc_chan;
	int irq = 0;
	int timeout = 50;

	ipu->dc_swap = swap;

	if (channel == MEM_DC_SYNC) {
		dc_chan = 1;
		irq = IPU_IRQ_DC_FC_1;
	} else if (channel == MEM_BG_SYNC) {
		dc_chan = 5;
		irq = IPU_IRQ_DP_SF_END;
	} else if (channel == MEM_FG_SYNC) {
		/* Disable FG channel */
		dc_chan = 5;

		reg = ipu_dp_read(ipu, DP_COM_CONF(DP_SYNC));
		csc = reg & DP_COM_CONF_CSC_DEF_MASK;
		if (csc == DP_COM_CONF_CSC_DEF_FG)
			reg &= ~DP_COM_CONF_CSC_DEF_MASK;

		reg &= ~DP_COM_CONF_FG_EN;
		ipu_dp_write(ipu, reg, DP_COM_CONF(DP_SYNC));

		reg = ipu_cm_read(ipu, IPU_SRM_PRI2) | 0x8;
		ipu_cm_write(ipu, reg, IPU_SRM_PRI2);

		if (ipu_is_channel_busy(ipu, MEM_BG_SYNC)) {
			ipu_cm_write(ipu, IPUIRQ_2_MASK(IPU_IRQ_DP_SF_END),
				IPUIRQ_2_STATREG(ipu->devtype,
							IPU_IRQ_DP_SF_END));
			while ((ipu_cm_read(ipu,
				IPUIRQ_2_STATREG(ipu->devtype,
							IPU_IRQ_DP_SF_END)) &
				IPUIRQ_2_MASK(IPU_IRQ_DP_SF_END)) == 0) {
				msleep(2);
				timeout -= 2;
				if (timeout <= 0)
					break;
			}
		}
		return;
	} else {
		return;
	}

	init_completion(&ipu->dc_comp);
	ret = ipu_request_irq(ipu, irq, dc_irq_handler, 0, NULL, ipu);
	if (ret < 0) {
		dev_err(ipu->dev, "DC irq %d in use\n", irq);
		return;
	}
	ret = wait_for_completion_timeout(&ipu->dc_comp, msecs_to_jiffies(50));
	ipu_free_irq(ipu, irq, ipu);
	dev_dbg(ipu->dev, "DC stop timeout - %d * 10ms\n", 5 - ret);

	if (ipu->dc_swap) {
		/* Swap DC channel 1 and 5 settings, and disable old dc chan */
		reg = ipu_dc_read(ipu, DC_WR_CH_CONF(dc_chan));
		ipu_dc_write(ipu, reg, DC_WR_CH_CONF(6 - dc_chan));
		reg &= ~DC_WR_CH_CONF_PROG_TYPE_MASK;
		reg ^= DC_WR_CH_CONF_PROG_DI_ID;
		ipu_dc_write(ipu, reg, DC_WR_CH_CONF(dc_chan));
	}
}

void _ipu_init_dc_mappings(struct ipu_soc *ipu)
{
	/* IPU_PIX_FMT_RGB24 */
	_ipu_dc_map_clear(ipu, 0);
	_ipu_dc_map_config(ipu, 0, 0, 7, 0xFF);
	_ipu_dc_map_config(ipu, 0, 1, 15, 0xFF);
	_ipu_dc_map_config(ipu, 0, 2, 23, 0xFF);

	/* IPU_PIX_FMT_RGB666 */
	_ipu_dc_map_clear(ipu, 1);
	_ipu_dc_map_config(ipu, 1, 0, 5, 0xFC);
	_ipu_dc_map_config(ipu, 1, 1, 11, 0xFC);
	_ipu_dc_map_config(ipu, 1, 2, 17, 0xFC);

	/* IPU_PIX_FMT_YUV444 */
	_ipu_dc_map_clear(ipu, 2);
	_ipu_dc_map_config(ipu, 2, 0, 15, 0xFF);
	_ipu_dc_map_config(ipu, 2, 1, 23, 0xFF);
	_ipu_dc_map_config(ipu, 2, 2, 7, 0xFF);

	/* IPU_PIX_FMT_RGB565 */
	_ipu_dc_map_clear(ipu, 3);
	_ipu_dc_map_config(ipu, 3, 0, 4, 0xF8);
	_ipu_dc_map_config(ipu, 3, 1, 10, 0xFC);
	_ipu_dc_map_config(ipu, 3, 2, 15, 0xF8);

	/* IPU_PIX_FMT_LVDS666 */
	_ipu_dc_map_clear(ipu, 4);
	_ipu_dc_map_config(ipu, 4, 0, 5, 0xFC);
	_ipu_dc_map_config(ipu, 4, 1, 13, 0xFC);
	_ipu_dc_map_config(ipu, 4, 2, 21, 0xFC);

	/* IPU_PIX_FMT_VYUY 16bit width */
	_ipu_dc_map_clear(ipu, 5);
	_ipu_dc_map_config(ipu, 5, 0, 7, 0xFF);
	_ipu_dc_map_config(ipu, 5, 1, 0, 0x0);
	_ipu_dc_map_config(ipu, 5, 2, 15, 0xFF);
	_ipu_dc_map_clear(ipu, 6);
	_ipu_dc_map_config(ipu, 6, 0, 0, 0x0);
	_ipu_dc_map_config(ipu, 6, 1, 7, 0xFF);
	_ipu_dc_map_config(ipu, 6, 2, 15, 0xFF);

	/* IPU_PIX_FMT_UYUV 16bit width */
	_ipu_dc_map_clear(ipu, 7);
	_ipu_dc_map_link(ipu, 7, 6, 0, 6, 1, 6, 2);
	_ipu_dc_map_clear(ipu, 8);
	_ipu_dc_map_link(ipu, 8, 5, 0, 5, 1, 5, 2);

	/* IPU_PIX_FMT_YUYV 16bit width */
	_ipu_dc_map_clear(ipu, 9);
	_ipu_dc_map_link(ipu, 9, 5, 2, 5, 1, 5, 0);
	_ipu_dc_map_clear(ipu, 10);
	_ipu_dc_map_link(ipu, 10, 5, 1, 5, 2, 5, 0);

	/* IPU_PIX_FMT_YVYU 16bit width */
	_ipu_dc_map_clear(ipu, 11);
	_ipu_dc_map_link(ipu, 11, 5, 1, 5, 2, 5, 0);
	_ipu_dc_map_clear(ipu, 12);
	_ipu_dc_map_link(ipu, 12, 5, 2, 5, 1, 5, 0);

	/* IPU_PIX_FMT_GBR24 */
	/* IPU_PIX_FMT_VYU444 */
	_ipu_dc_map_clear(ipu, 13);
	_ipu_dc_map_link(ipu, 13, 0, 2, 0, 0, 0, 1);

	/* IPU_PIX_FMT_BGR24 */
	_ipu_dc_map_clear(ipu, 14);
	_ipu_dc_map_link(ipu, 14, 0, 2, 0, 1, 0, 0);
}

int _ipu_pixfmt_to_map(uint32_t fmt)
{
	switch (fmt) {
	case IPU_PIX_FMT_GENERIC:
	case IPU_PIX_FMT_RGB24:
		return 0;
	case IPU_PIX_FMT_RGB666:
		return 1;
	case IPU_PIX_FMT_YUV444:
		return 2;
	case IPU_PIX_FMT_RGB565:
		return 3;
	case IPU_PIX_FMT_LVDS666:
		return 4;
	case IPU_PIX_FMT_VYUY:
		return 6;
	case IPU_PIX_FMT_UYVY:
		return 8;
	case IPU_PIX_FMT_YUYV:
		return 10;
	case IPU_PIX_FMT_YVYU:
		return 12;
	case IPU_PIX_FMT_GBR24:
	case IPU_PIX_FMT_VYU444:
		return 13;
	case IPU_PIX_FMT_BGR24:
		return 14;
	}

	return -1;
}

/*!
 * This function sets the colorspace for of dp.
 * modes.
 *
 * @param	ipu		ipu handler
 * @param       channel         Input parameter for the logical channel ID.
 *
 * @param       param         	If it's not NULL, update the csc table
 *                              with this parameter.
 *
 * @return      N/A
 */
void _ipu_dp_set_csc_coefficients(struct ipu_soc *ipu, ipu_channel_t channel, int32_t param[][3])
{
	int dp;
	struct dp_csc_param_t dp_csc_param;

	if (channel == MEM_FG_SYNC)
		dp = DP_SYNC;
	else if (channel == MEM_BG_SYNC)
		dp = DP_SYNC;
	else if (channel == MEM_BG_ASYNC0)
		dp = DP_ASYNC0;
	else
		return;

	dp_csc_param.mode = -1;
	dp_csc_param.coeff = param;
	__ipu_dp_csc_setup(ipu, dp, dp_csc_param, true);
}

void ipu_set_csc_coefficients(struct ipu_soc *ipu, ipu_channel_t channel, int32_t param[][3])
{
	_ipu_dp_set_csc_coefficients(ipu, channel, param);
}
EXPORT_SYMBOL(ipu_set_csc_coefficients);

/*!
 * This function is called to adapt synchronous LCD panel to IPU restriction.
 *
 */
void adapt_panel_to_ipu_restricitions(struct ipu_soc *ipu, uint16_t *v_start_width,
					uint16_t *v_sync_width,
					uint16_t *v_end_width)
{
	if (*v_end_width < 2) {
		uint16_t diff = 2 - *v_end_width;
		if (*v_start_width >= diff) {
			*v_end_width = 2;
			*v_start_width = *v_start_width - diff;
		} else if (*v_sync_width > diff) {
			*v_end_width = 2;
			*v_sync_width = *v_sync_width - diff;
		} else
			dev_err(ipu->dev, "WARNING: try to adapt timming, but failed\n");
		dev_err(ipu->dev, "WARNING: adapt panel end blank lines\n");
	}
}

/*!
 * This function is called to initialize a synchronous LCD panel.
 *
 * @param	ipu		ipu handler
 * @param       disp            The DI the panel is attached to.
 *
 * @param       pixel_clk       Desired pixel clock frequency in Hz.
 *
 * @param       pixel_fmt       Input parameter for pixel format of buffer.
 *                              Pixel format is a FOURCC ASCII code.
 *
 * @param       width           The width of panel in pixels.
 *
 * @param       height          The height of panel in pixels.
 *
 * @param       hStartWidth     The number of pixel clocks between the HSYNC
 *                              signal pulse and the start of valid data.
 *
 * @param       hSyncWidth      The width of the HSYNC signal in units of pixel
 *                              clocks.
 *
 * @param       hEndWidth       The number of pixel clocks between the end of
 *                              valid data and the HSYNC signal for next line.
 *
 * @param       vStartWidth     The number of lines between the VSYNC
 *                              signal pulse and the start of valid data.
 *
 * @param       vSyncWidth      The width of the VSYNC signal in units of lines
 *
 * @param       vEndWidth       The number of lines between the end of valid
 *                              data and the VSYNC signal for next frame.
 *
 * @param       sig             Bitfield of signal polarities for LCD interface.
 *
 * @return      This function returns 0 on success or negative error code on
 *              fail.
 */
int32_t ipu_init_sync_panel(struct ipu_soc *ipu, int disp, uint32_t pixel_clk,
			    uint16_t width, uint16_t height,
			    uint32_t pixel_fmt,
			    uint16_t h_start_width, uint16_t h_sync_width,
			    uint16_t h_end_width, uint16_t v_start_width,
			    uint16_t v_sync_width, uint16_t v_end_width,
			    uint32_t v_to_h_sync, ipu_di_signal_cfg_t sig)
{
	uint32_t field0_offset = 0;
	uint32_t field1_offset;
	uint32_t reg;
	uint32_t di_gen, vsync_cnt;
	uint32_t div, rounded_pixel_clk;
	uint32_t h_total, v_total;
	int map;
	int ret;
	struct clk *ldb_di0_clk, *ldb_di1_clk;
	struct clk *di_parent;

	dev_dbg(ipu->dev, "panel size = %d x %d\n", width, height);

	if ((v_sync_width == 0) || (h_sync_width == 0))
		return -EINVAL;

	adapt_panel_to_ipu_restricitions(ipu, &v_start_width, &v_sync_width, &v_end_width);
	h_total = width + h_sync_width + h_start_width + h_end_width;
	v_total = height + v_sync_width + v_start_width + v_end_width;

	/* Init clocking */
	dev_dbg(ipu->dev, "pixel clk = %d\n", pixel_clk);

	di_parent = clk_get_parent(ipu->di_clk_sel[disp]);
	if (!di_parent) {
		dev_err(ipu->dev, "get di clk parent fail\n");
		return -EINVAL;
	}
	ldb_di0_clk = clk_get(ipu->dev, "ldb_di0");
	if (IS_ERR(ldb_di0_clk)) {
		dev_err(ipu->dev, "clk_get di0 failed");
		return PTR_ERR(ldb_di0_clk);
	}
	ldb_di1_clk = clk_get(ipu->dev, "ldb_di1");
	if (IS_ERR(ldb_di1_clk)) {
		dev_err(ipu->dev, "clk_get di1 failed");
		return PTR_ERR(ldb_di1_clk);
	}
	if (!strcmp(__clk_get_name(di_parent), __clk_get_name(ldb_di1_clk)) ||
		!strcmp(__clk_get_name(di_parent), __clk_get_name(ldb_di1_clk))) {
		/* if di clk parent is tve/ldb, then keep it;*/
		dev_dbg(ipu->dev, "use special clk parent\n");
		ret = clk_set_parent(ipu->pixel_clk_sel[disp], ipu->di_clk[disp]);
		if (ret) {
			dev_err(ipu->dev, "set pixel clk error:%d\n", ret);
			return ret;
		}
		clk_put(ldb_di0_clk);
		clk_put(ldb_di1_clk);
	} else {
		/* try ipu clk first*/
		dev_dbg(ipu->dev, "try ipu internal clk\n");
		ret = clk_set_parent(ipu->pixel_clk_sel[disp], ipu->ipu_clk);
		if (ret) {
			dev_err(ipu->dev, "set pixel clk error:%d\n", ret);
			return ret;
		}
		rounded_pixel_clk = clk_round_rate(ipu->pixel_clk[disp], pixel_clk);
		dev_dbg(ipu->dev, "rounded pix clk:%d\n", rounded_pixel_clk);
		/*
		 * we will only use 1/2 fraction for ipu clk,
		 * so if the clk rate is not fit, try ext clk.
		 */
		if (!sig.int_clk &&
			((rounded_pixel_clk >= pixel_clk + pixel_clk/200) ||
			(rounded_pixel_clk <= pixel_clk - pixel_clk/200))) {
			dev_dbg(ipu->dev, "try ipu ext di clk\n");

			rounded_pixel_clk =
				clk_round_rate(ipu->di_clk[disp], pixel_clk);
			ret = clk_set_rate(ipu->di_clk[disp],
						rounded_pixel_clk);
			if (ret) {
				dev_err(ipu->dev,
					"set di clk rate error:%d\n", ret);
				return ret;
			}
			dev_dbg(ipu->dev, "di clk:%d\n", rounded_pixel_clk);
			ret = clk_set_parent(ipu->pixel_clk_sel[disp],
						ipu->di_clk[disp]);
			if (ret) {
				dev_err(ipu->dev,
					"set pixel clk parent error:%d\n", ret);
				return ret;
			}
		}
	}
	rounded_pixel_clk = clk_round_rate(ipu->pixel_clk[disp], pixel_clk);
	dev_dbg(ipu->dev, "round pixel clk:%d\n", rounded_pixel_clk);
	ret = clk_set_rate(ipu->pixel_clk[disp], rounded_pixel_clk);
	if (ret) {
		dev_err(ipu->dev, "set pixel clk rate error:%d\n", ret);
		return ret;
	}
	msleep(5);
	/* Get integer portion of divider */
	div = clk_get_rate(clk_get_parent(ipu->pixel_clk_sel[disp])) / rounded_pixel_clk;
	dev_dbg(ipu->dev, "div:%d\n", div);
	if (!div) {
		dev_err(ipu->dev, "invalid pixel clk div = 0\n");
		return -EINVAL;
	}


	mutex_lock(&ipu->mutex_lock);

	_ipu_di_data_wave_config(ipu, disp, SYNC_WAVE, div - 1, div - 1);
	_ipu_di_data_pin_config(ipu, disp, SYNC_WAVE, DI_PIN15, 3, 0, div * 2);

	map = _ipu_pixfmt_to_map(pixel_fmt);
	if (map < 0) {
		dev_dbg(ipu->dev, "IPU_DISP: No MAP\n");
		mutex_unlock(&ipu->mutex_lock);
		return -EINVAL;
	}

	/*clear DI*/
	di_gen = ipu_di_read(ipu, disp, DI_GENERAL);
	di_gen &= (0x3 << 20);
	ipu_di_write(ipu, disp, di_gen, DI_GENERAL);

	if (sig.interlaced) {
		if (ipu->devtype >= IPUv3EX) {
			/* Setup internal HSYNC waveform */
			_ipu_di_sync_config(ipu,
					disp, 		/* display */
					1, 		/* counter */
					h_total/2 - 1, 	/* run count */
					DI_SYNC_CLK,	/* run_resolution */
					0, 		/* offset */
					DI_SYNC_NONE, 	/* offset resolution */
					0, 		/* repeat count */
					DI_SYNC_NONE, 	/* CNT_CLR_SEL */
					0, 		/* CNT_POLARITY_GEN_EN */
					DI_SYNC_NONE, 	/* CNT_POLARITY_CLR_SEL */
					DI_SYNC_NONE, 	/* CNT_POLARITY_TRIGGER_SEL */
					0, 		/* COUNT UP */
					0		/* COUNT DOWN */
					);

			/* Field 1 VSYNC waveform */
			_ipu_di_sync_config(ipu,
					disp, 		/* display */
					2, 		/* counter */
					h_total - 1, 	/* run count */
					DI_SYNC_CLK,	/* run_resolution */
					0, 		/* offset */
					DI_SYNC_NONE, 	/* offset resolution */
					0, 		/* repeat count */
					DI_SYNC_NONE, 	/* CNT_CLR_SEL */
					0, 		/* CNT_POLARITY_GEN_EN */
					DI_SYNC_NONE, 	/* CNT_POLARITY_CLR_SEL */
					DI_SYNC_NONE, 	/* CNT_POLARITY_TRIGGER_SEL */
					0, 		/* COUNT UP */
					2*div		/* COUNT DOWN */
					);

			/* Setup internal HSYNC waveform */
			_ipu_di_sync_config(ipu,
					disp, 		/* display */
					3, 		/* counter */
					v_total*2 - 1, 	/* run count */
					DI_SYNC_INT_HSYNC,	/* run_resolution */
					1, 			/* offset */
					DI_SYNC_INT_HSYNC, 	/* offset resolution */
					0, 		/* repeat count */
					DI_SYNC_NONE, 	/* CNT_CLR_SEL */
					0, 		/* CNT_POLARITY_GEN_EN */
					DI_SYNC_NONE, 	/* CNT_POLARITY_CLR_SEL */
					DI_SYNC_NONE, 	/* CNT_POLARITY_TRIGGER_SEL */
					0, 		/* COUNT UP */
					2*div		/* COUNT DOWN */
					);

			/* Active Field ? */
			_ipu_di_sync_config(ipu,
					disp, 		/* display */
					4, 		/* counter */
					v_total/2 - 1, 	/* run count */
					DI_SYNC_HSYNC,	/* run_resolution */
					v_start_width, 	/*  offset */
					DI_SYNC_HSYNC, 	/* offset resolution */
					2, 		/* repeat count */
					DI_SYNC_VSYNC, 	/* CNT_CLR_SEL */
					0, 		/* CNT_POLARITY_GEN_EN */
					DI_SYNC_NONE, 	/* CNT_POLARITY_CLR_SEL */
					DI_SYNC_NONE, 	/* CNT_POLARITY_TRIGGER_SEL */
					0, 		/* COUNT UP */
					0		/* COUNT DOWN */
					);

			/* Active Line */
			_ipu_di_sync_config(ipu,
					disp, 		/* display */
					5, 		/* counter */
					0, 		/* run count */
					DI_SYNC_HSYNC,	/* run_resolution */
					0, 		/*  offset */
					DI_SYNC_NONE, 	/* offset resolution */
					height/2, 	/* repeat count */
					4, 		/* CNT_CLR_SEL */
					0, 		/* CNT_POLARITY_GEN_EN */
					DI_SYNC_NONE, 	/* CNT_POLARITY_CLR_SEL */
					DI_SYNC_NONE, 	/* CNT_POLARITY_TRIGGER_SEL */
					0, 		/* COUNT UP */
					0		/* COUNT DOWN */
					);

			/* Field 0 VSYNC waveform */
			_ipu_di_sync_config(ipu,
					disp, 		/* display */
					6, 		/* counter */
					v_total - 1, 	/* run count */
					DI_SYNC_HSYNC,	/* run_resolution */
					0, 		/* offset */
					DI_SYNC_NONE, 	/* offset resolution */
					0, 		/* repeat count */
					DI_SYNC_NONE, 	/* CNT_CLR_SEL  */
					0, 		/* CNT_POLARITY_GEN_EN */
					DI_SYNC_NONE, 	/* CNT_POLARITY_CLR_SEL */
					DI_SYNC_NONE, 	/* CNT_POLARITY_TRIGGER_SEL */
					0, 		/* COUNT UP */
					0		/* COUNT DOWN */
					);

			/* DC VSYNC waveform */
			vsync_cnt = 7;
			_ipu_di_sync_config(ipu,
					disp, 		/* display */
					7, 		/* counter */
					v_total/2 - 1, 	/* run count */
					DI_SYNC_HSYNC,	/* run_resolution  */
					9, 		/* offset  */
					DI_SYNC_HSYNC, 	/* offset resolution */
					2, 		/* repeat count */
					DI_SYNC_VSYNC, 	/* CNT_CLR_SEL */
					0, 		/* CNT_POLARITY_GEN_EN */
					DI_SYNC_NONE, 	/* CNT_POLARITY_CLR_SEL */
					DI_SYNC_NONE, 	/* CNT_POLARITY_TRIGGER_SEL */
					0, 		/* COUNT UP */
					0		/* COUNT DOWN */
					);

			/* active pixel waveform */
			_ipu_di_sync_config(ipu,
					disp, 		/* display */
					8, 		/* counter */
					0, 		/* run count  */
					DI_SYNC_CLK,	/* run_resolution */
					h_start_width, 	/* offset  */
					DI_SYNC_CLK, 	/* offset resolution */
					width, 		/* repeat count  */
					5, 		/* CNT_CLR_SEL  */
					0, 		/* CNT_POLARITY_GEN_EN  */
					DI_SYNC_NONE, 	/* CNT_POLARITY_CLR_SEL */
					DI_SYNC_NONE, 	/* CNT_POLARITY_TRIGGER_SEL  */
					0, 		/* COUNT UP  */
					0		/* COUNT DOWN */
					);

			/* Second VSYNC */
			_ipu_di_sync_config(ipu,
					disp, 		/* display */
					9, 		/* counter */
					v_total - 1, 	/* run count */
					DI_SYNC_INT_HSYNC,	/* run_resolution */
					v_total/2, 		/* offset  */
					DI_SYNC_INT_HSYNC, 	/* offset resolution  */
					0, 		/* repeat count */
					DI_SYNC_HSYNC, 	/* CNT_CLR_SEL */
					0, 		/* CNT_POLARITY_GEN_EN  */
					DI_SYNC_NONE, 	/* CNT_POLARITY_CLR_SEL  */
					DI_SYNC_NONE, 	/* CNT_POLARITY_TRIGGER_SEL */
					0, 		/* COUNT UP */
					2*div		/* COUNT DOWN */
					);

			/* set gentime select and tag sel */
			reg = ipu_di_read(ipu, disp, DI_SW_GEN1(9));
			reg &= 0x1FFFFFFF;
			reg |= (3-1)<<29 | 0x00008000;
			ipu_di_write(ipu, disp, reg, DI_SW_GEN1(9));

			ipu_di_write(ipu, disp, v_total / 2 - 1, DI_SCR_CONF);

			/* set y_sel = 1 */
			di_gen |= 0x10000000;
			di_gen |= DI_GEN_POLARITY_5;
			di_gen |= DI_GEN_POLARITY_8;
		} else {
			/* Setup internal HSYNC waveform */
			_ipu_di_sync_config(ipu, disp, 1, h_total - 1, DI_SYNC_CLK,
					0, DI_SYNC_NONE, 0, DI_SYNC_NONE, 0, DI_SYNC_NONE,
					DI_SYNC_NONE, 0, 0);

			field1_offset = v_sync_width + v_start_width + height / 2 +
				v_end_width;
			if (sig.odd_field_first) {
				field0_offset = field1_offset - 1;
				field1_offset = 0;
			}
			v_total += v_start_width + v_end_width;

			/* Field 1 VSYNC waveform */
			_ipu_di_sync_config(ipu, disp, 2, v_total - 1, 1,
					field0_offset,
					field0_offset ? 1 : DI_SYNC_NONE,
					0, DI_SYNC_NONE, 0,
					DI_SYNC_NONE, DI_SYNC_NONE, 0, 4);

			/* Setup internal HSYNC waveform */
			_ipu_di_sync_config(ipu, disp, 3, h_total - 1, DI_SYNC_CLK,
					0, DI_SYNC_NONE, 0, DI_SYNC_NONE, 0,
					DI_SYNC_NONE, DI_SYNC_NONE, 0, 4);

			/* Active Field ? */
			_ipu_di_sync_config(ipu, disp, 4,
					field0_offset ?
					field0_offset : field1_offset - 2,
					1, v_start_width + v_sync_width, 1, 2, 2,
					0, DI_SYNC_NONE, DI_SYNC_NONE, 0, 0);

			/* Active Line */
			_ipu_di_sync_config(ipu, disp, 5, 0, 1,
					0, DI_SYNC_NONE,
					height / 2, 4, 0, DI_SYNC_NONE,
					DI_SYNC_NONE, 0, 0);

			/* Field 0 VSYNC waveform */
			_ipu_di_sync_config(ipu, disp, 6, v_total - 1, 1,
					0, DI_SYNC_NONE,
					0, DI_SYNC_NONE, 0, DI_SYNC_NONE,
					DI_SYNC_NONE, 0, 0);

			/* DC VSYNC waveform */
			vsync_cnt = 7;
			_ipu_di_sync_config(ipu, disp, 7, 0, 1,
					field1_offset,
					field1_offset ? 1 : DI_SYNC_NONE,
					1, 2, 0, DI_SYNC_NONE, DI_SYNC_NONE, 0, 0);

			/* active pixel waveform */
			_ipu_di_sync_config(ipu, disp, 8, 0, DI_SYNC_CLK,
					h_sync_width + h_start_width, DI_SYNC_CLK,
					width, 5, 0, DI_SYNC_NONE, DI_SYNC_NONE,
					0, 0);

			/* ??? */
			_ipu_di_sync_config(ipu, disp, 9, v_total - 1, 2,
					0, DI_SYNC_NONE,
					0, DI_SYNC_NONE, 6, DI_SYNC_NONE,
					DI_SYNC_NONE, 0, 0);

			reg = ipu_di_read(ipu, disp, DI_SW_GEN1(9));
			reg |= 0x8000;
			ipu_di_write(ipu, disp, reg, DI_SW_GEN1(9));

			ipu_di_write(ipu, disp, v_sync_width + v_start_width +
					v_end_width + height / 2 - 1, DI_SCR_CONF);
		}

		/* Init template microcode */
		_ipu_dc_write_tmpl(ipu, 0, WROD(0), 0, map, SYNC_WAVE, 0, 8, 1);

		if (sig.Hsync_pol)
			di_gen |= DI_GEN_POLARITY_3;
		if (sig.Vsync_pol)
			di_gen |= DI_GEN_POLARITY_2;
	} else {
		/* Setup internal HSYNC waveform */
		_ipu_di_sync_config(ipu, disp, 1, h_total - 1, DI_SYNC_CLK,
					0, DI_SYNC_NONE, 0, DI_SYNC_NONE, 0, DI_SYNC_NONE,
					DI_SYNC_NONE, 0, 0);

		/* Setup external (delayed) HSYNC waveform */
		_ipu_di_sync_config(ipu, disp, DI_SYNC_HSYNC, h_total - 1,
				    DI_SYNC_CLK, div * v_to_h_sync, DI_SYNC_CLK,
				    0, DI_SYNC_NONE, 1, DI_SYNC_NONE,
				    DI_SYNC_CLK, 0, h_sync_width * 2);
		/* Setup VSYNC waveform */
		vsync_cnt = DI_SYNC_VSYNC;
		_ipu_di_sync_config(ipu, disp, DI_SYNC_VSYNC, v_total - 1,
				    DI_SYNC_INT_HSYNC, 0, DI_SYNC_NONE, 0,
				    DI_SYNC_NONE, 1, DI_SYNC_NONE,
				    DI_SYNC_INT_HSYNC, 0, v_sync_width * 2);
		ipu_di_write(ipu, disp, v_total - 1, DI_SCR_CONF);

		/* Setup active data waveform to sync with DC */
		_ipu_di_sync_config(ipu, disp, 4, 0, DI_SYNC_HSYNC,
				    v_sync_width + v_start_width, DI_SYNC_HSYNC, height,
				    DI_SYNC_VSYNC, 0, DI_SYNC_NONE,
				    DI_SYNC_NONE, 0, 0);
		_ipu_di_sync_config(ipu, disp, 5, 0, DI_SYNC_CLK,
				    h_sync_width + h_start_width, DI_SYNC_CLK,
				    width, 4, 0, DI_SYNC_NONE, DI_SYNC_NONE, 0,
				    0);

		/* set VGA delayed hsync/vsync no matter VGA enabled */
		if (disp) {
			/* couter 7 for VGA delay HSYNC */
			_ipu_di_sync_config(ipu, disp, 7,
					h_total - 1, DI_SYNC_CLK,
					18, DI_SYNC_CLK,
					0, DI_SYNC_NONE,
					1, DI_SYNC_NONE, DI_SYNC_CLK,
					0, h_sync_width * 2);

			/* couter 8 for VGA delay VSYNC */
			_ipu_di_sync_config(ipu, disp, 8,
					v_total - 1, DI_SYNC_INT_HSYNC,
					1, DI_SYNC_INT_HSYNC,
					0, DI_SYNC_NONE,
					1, DI_SYNC_NONE, DI_SYNC_INT_HSYNC,
					0, v_sync_width * 2);
		}

		/* reset all unused counters */
		ipu_di_write(ipu, disp, 0, DI_SW_GEN0(6));
		ipu_di_write(ipu, disp, 0, DI_SW_GEN1(6));
		if (!disp) {
			ipu_di_write(ipu, disp, 0, DI_SW_GEN0(7));
			ipu_di_write(ipu, disp, 0, DI_SW_GEN1(7));
			ipu_di_write(ipu, disp, 0, DI_STP_REP(7));
			ipu_di_write(ipu, disp, 0, DI_SW_GEN0(8));
			ipu_di_write(ipu, disp, 0, DI_SW_GEN1(8));
			ipu_di_write(ipu, disp, 0, DI_STP_REP(8));
		}
		ipu_di_write(ipu, disp, 0, DI_SW_GEN0(9));
		ipu_di_write(ipu, disp, 0, DI_SW_GEN1(9));
		ipu_di_write(ipu, disp, 0, DI_STP_REP(9));

		reg = ipu_di_read(ipu, disp, DI_STP_REP(6));
		reg &= 0x0000FFFF;
		ipu_di_write(ipu, disp, reg, DI_STP_REP(6));

		/* Init template microcode */
		if (disp) {
			if ((pixel_fmt == IPU_PIX_FMT_YUYV) ||
				(pixel_fmt == IPU_PIX_FMT_UYVY) ||
				(pixel_fmt == IPU_PIX_FMT_YVYU) ||
				(pixel_fmt == IPU_PIX_FMT_VYUY)) {
				_ipu_dc_write_tmpl(ipu, 8, WROD(0), 0, (map - 1), SYNC_WAVE, 0, 5, 1);
				_ipu_dc_write_tmpl(ipu, 9, WROD(0), 0, map, SYNC_WAVE, 0, 5, 1);
				/* configure user events according to DISP NUM */
				ipu_dc_write(ipu, (width - 1), DC_UGDE_3(disp));
			}
			_ipu_dc_write_tmpl(ipu, 2, WROD(0), 0, map, SYNC_WAVE, 8, 5, 1);
			_ipu_dc_write_tmpl(ipu, 3, WROD(0), 0, map, SYNC_WAVE, 4, 5, 0);
			_ipu_dc_write_tmpl(ipu, 4, WRG, 0, map, NULL_WAVE, 0, 0, 1);
			_ipu_dc_write_tmpl(ipu, 1, WROD(0), 0, map, SYNC_WAVE, 0, 5, 1);

		} else {
			if ((pixel_fmt == IPU_PIX_FMT_YUYV) ||
				(pixel_fmt == IPU_PIX_FMT_UYVY) ||
				(pixel_fmt == IPU_PIX_FMT_YVYU) ||
				(pixel_fmt == IPU_PIX_FMT_VYUY)) {
				_ipu_dc_write_tmpl(ipu, 10, WROD(0), 0, (map - 1), SYNC_WAVE, 0, 5, 1);
				_ipu_dc_write_tmpl(ipu, 11, WROD(0), 0, map, SYNC_WAVE, 0, 5, 1);
				/* configure user events according to DISP NUM */
				ipu_dc_write(ipu, width - 1, DC_UGDE_3(disp));
			}
		   _ipu_dc_write_tmpl(ipu, 5, WROD(0), 0, map, SYNC_WAVE, 8, 5, 1);
		   _ipu_dc_write_tmpl(ipu, 6, WROD(0), 0, map, SYNC_WAVE, 4, 5, 0);
		   _ipu_dc_write_tmpl(ipu, 7, WRG, 0, map, NULL_WAVE, 0, 0, 1);
		   _ipu_dc_write_tmpl(ipu, 12, WROD(0), 0, map, SYNC_WAVE, 0, 5, 1);
		}

		if (sig.Hsync_pol) {
			di_gen |= DI_GEN_POLARITY_2;
			if (disp)
				di_gen |= DI_GEN_POLARITY_7;
		}
		if (sig.Vsync_pol) {
			di_gen |= DI_GEN_POLARITY_3;
			if (disp)
				di_gen |= DI_GEN_POLARITY_8;
		}
	}
	/* changinc DISP_CLK polarity: it can be wrong for some applications */
	if ((pixel_fmt == IPU_PIX_FMT_YUYV) ||
		(pixel_fmt == IPU_PIX_FMT_UYVY) ||
		(pixel_fmt == IPU_PIX_FMT_YVYU) ||
		(pixel_fmt == IPU_PIX_FMT_VYUY))
			di_gen |= 0x00020000;

	if (!sig.clk_pol)
		di_gen |= DI_GEN_POLARITY_DISP_CLK;

	ipu_di_write(ipu, disp, di_gen, DI_GENERAL);

	ipu_di_write(ipu, disp, (--vsync_cnt << DI_VSYNC_SEL_OFFSET) |
			0x00000002, DI_SYNC_AS_GEN);
	reg = ipu_di_read(ipu, disp, DI_POL);
	reg &= ~(DI_POL_DRDY_DATA_POLARITY | DI_POL_DRDY_POLARITY_15);
	if (sig.enable_pol)
		reg |= DI_POL_DRDY_POLARITY_15;
	if (sig.data_pol)
		reg |= DI_POL_DRDY_DATA_POLARITY;
	ipu_di_write(ipu, disp, reg, DI_POL);

	ipu_dc_write(ipu, width, DC_DISP_CONF2(DC_DISP_ID_SYNC(disp)));

	mutex_unlock(&ipu->mutex_lock);

	return 0;
}
EXPORT_SYMBOL(ipu_init_sync_panel);

void ipu_uninit_sync_panel(struct ipu_soc *ipu, int disp)
{
	uint32_t reg;
	uint32_t di_gen;

	if ((disp != 0) || (disp != 1))
		return;

	mutex_lock(&ipu->mutex_lock);

	di_gen = ipu_di_read(ipu, disp, DI_GENERAL);
	di_gen |= 0x3ff | DI_GEN_POLARITY_DISP_CLK;
	ipu_di_write(ipu, disp, di_gen, DI_GENERAL);

	reg = ipu_di_read(ipu, disp, DI_POL);
	reg |= 0x3ffffff;
	ipu_di_write(ipu, disp, reg, DI_POL);

	mutex_unlock(&ipu->mutex_lock);
}
EXPORT_SYMBOL(ipu_uninit_sync_panel);

int ipu_init_async_panel(struct ipu_soc *ipu, int disp, int type, uint32_t cycle_time,
			 uint32_t pixel_fmt, ipu_adc_sig_cfg_t sig)
{
	int map;
	u32 ser_conf = 0;
	u32 div;
	u32 di_clk = clk_get_rate(ipu->ipu_clk);

	/* round up cycle_time, then calcalate the divider using scaled math */
	cycle_time += (1000000000UL / di_clk) - 1;
	div = (cycle_time * (di_clk / 256UL)) / (1000000000UL / 256UL);

	map = _ipu_pixfmt_to_map(pixel_fmt);
	if (map < 0)
		return -EINVAL;

	mutex_lock(&ipu->mutex_lock);

	if (type == IPU_PANEL_SERIAL) {
		ipu_di_write(ipu, disp, (div << 24) | ((sig.ifc_width - 1) << 4),
			     DI_DW_GEN(ASYNC_SER_WAVE));

		_ipu_di_data_pin_config(ipu, disp, ASYNC_SER_WAVE, DI_PIN_CS,
					0, 0, (div * 2) + 1);
		_ipu_di_data_pin_config(ipu, disp, ASYNC_SER_WAVE, DI_PIN_SER_CLK,
					1, div, div * 2);
		_ipu_di_data_pin_config(ipu, disp, ASYNC_SER_WAVE, DI_PIN_SER_RS,
					2, 0, 0);

		_ipu_dc_write_tmpl(ipu, 0x64, WROD(0), 0, map, ASYNC_SER_WAVE, 0, 0, 1);

		/* Configure DC for serial panel */
		ipu_dc_write(ipu, 0x14, DC_DISP_CONF1(DC_DISP_ID_SERIAL));

		if (sig.clk_pol)
			ser_conf |= DI_SER_CONF_SERIAL_CLK_POL;
		if (sig.data_pol)
			ser_conf |= DI_SER_CONF_SERIAL_DATA_POL;
		if (sig.rs_pol)
			ser_conf |= DI_SER_CONF_SERIAL_RS_POL;
		if (sig.cs_pol)
			ser_conf |= DI_SER_CONF_SERIAL_CS_POL;
		ipu_di_write(ipu, disp, ser_conf, DI_SER_CONF);
	}

	mutex_unlock(&ipu->mutex_lock);
	return 0;
}
EXPORT_SYMBOL(ipu_init_async_panel);

/*!
 * This function sets the foreground and background plane global alpha blending
 * modes. This function also sets the DP graphic plane according to the
 * parameter of IPUv3 DP channel.
 *
 * @param	ipu		ipu handler
 * @param	channel		IPUv3 DP channel
 *
 * @param       enable          Boolean to enable or disable global alpha
 *                              blending. If disabled, local blending is used.
 *
 * @param       alpha           Global alpha value.
 *
 * @return      Returns 0 on success or negative error code on fail
 */
int32_t ipu_disp_set_global_alpha(struct ipu_soc *ipu, ipu_channel_t channel,
				bool enable, uint8_t alpha)
{
	uint32_t reg;
	uint32_t flow;
	bool bg_chan;

	if (channel == MEM_BG_SYNC || channel == MEM_FG_SYNC)
		flow = DP_SYNC;
	else if (channel == MEM_BG_ASYNC0 || channel == MEM_FG_ASYNC0)
		flow = DP_ASYNC0;
	else if (channel == MEM_BG_ASYNC1 || channel == MEM_FG_ASYNC1)
		flow = DP_ASYNC1;
	else
		return -EINVAL;

	if (channel == MEM_BG_SYNC || channel == MEM_BG_ASYNC0 ||
	    channel == MEM_BG_ASYNC1)
		bg_chan = true;
	else
		bg_chan = false;

	_ipu_get(ipu);

	mutex_lock(&ipu->mutex_lock);

	if (bg_chan) {
		reg = ipu_dp_read(ipu, DP_COM_CONF(flow));
		ipu_dp_write(ipu, reg & ~DP_COM_CONF_GWSEL, DP_COM_CONF(flow));
	} else {
		reg = ipu_dp_read(ipu, DP_COM_CONF(flow));
		ipu_dp_write(ipu, reg | DP_COM_CONF_GWSEL, DP_COM_CONF(flow));
	}

	if (enable) {
		reg = ipu_dp_read(ipu, DP_GRAPH_WIND_CTRL(flow)) & 0x00FFFFFFL;
		ipu_dp_write(ipu, reg | ((uint32_t) alpha << 24),
			     DP_GRAPH_WIND_CTRL(flow));

		reg = ipu_dp_read(ipu, DP_COM_CONF(flow));
		ipu_dp_write(ipu, reg | DP_COM_CONF_GWAM, DP_COM_CONF(flow));
	} else {
		reg = ipu_dp_read(ipu, DP_COM_CONF(flow));
		ipu_dp_write(ipu, reg & ~DP_COM_CONF_GWAM, DP_COM_CONF(flow));
	}

	reg = ipu_cm_read(ipu, IPU_SRM_PRI2) | 0x8;
	ipu_cm_write(ipu, reg, IPU_SRM_PRI2);

	mutex_unlock(&ipu->mutex_lock);

	_ipu_put(ipu);

	return 0;
}
EXPORT_SYMBOL(ipu_disp_set_global_alpha);

/*!
 * This function sets the transparent color key for SDC graphic plane.
 *
 * @param	ipu		ipu handler
 * @param       channel         Input parameter for the logical channel ID.
 *
 * @param       enable          Boolean to enable or disable color key
 *
 * @param       colorKey        24-bit RGB color for transparent color key.
 *
 * @return      Returns 0 on success or negative error code on fail
 */
int32_t ipu_disp_set_color_key(struct ipu_soc *ipu, ipu_channel_t channel,
				bool enable, uint32_t color_key)
{
	uint32_t reg, flow;
	int y, u, v;
	int red, green, blue;

	if (channel == MEM_BG_SYNC || channel == MEM_FG_SYNC)
		flow = DP_SYNC;
	else if (channel == MEM_BG_ASYNC0 || channel == MEM_FG_ASYNC0)
		flow = DP_ASYNC0;
	else if (channel == MEM_BG_ASYNC1 || channel == MEM_FG_ASYNC1)
		flow = DP_ASYNC1;
	else
		return -EINVAL;

	_ipu_get(ipu);

	mutex_lock(&ipu->mutex_lock);

	ipu->color_key_4rgb = true;
	/* Transform color key from rgb to yuv if CSC is enabled */
	if (((ipu->fg_csc_type == RGB2YUV) && (ipu->bg_csc_type == YUV2YUV)) ||
			((ipu->fg_csc_type == YUV2YUV) && (ipu->bg_csc_type == RGB2YUV)) ||
			((ipu->fg_csc_type == YUV2YUV) && (ipu->bg_csc_type == YUV2YUV)) ||
			((ipu->fg_csc_type == YUV2RGB) && (ipu->bg_csc_type == YUV2RGB))) {

		dev_dbg(ipu->dev, "color key 0x%x need change to yuv fmt\n", color_key);

		red = (color_key >> 16) & 0xFF;
		green = (color_key >> 8) & 0xFF;
		blue = color_key & 0xFF;

		y = _rgb_to_yuv(0, red, green, blue);
		u = _rgb_to_yuv(1, red, green, blue);
		v = _rgb_to_yuv(2, red, green, blue);
		color_key = (y << 16) | (u << 8) | v;

		ipu->color_key_4rgb = false;

		dev_dbg(ipu->dev, "color key change to yuv fmt 0x%x\n", color_key);
	}

	if (enable) {
		reg = ipu_dp_read(ipu, DP_GRAPH_WIND_CTRL(flow)) & 0xFF000000L;
		ipu_dp_write(ipu, reg | color_key, DP_GRAPH_WIND_CTRL(flow));

		reg = ipu_dp_read(ipu, DP_COM_CONF(flow));
		ipu_dp_write(ipu, reg | DP_COM_CONF_GWCKE, DP_COM_CONF(flow));
	} else {
		reg = ipu_dp_read(ipu, DP_COM_CONF(flow));
		ipu_dp_write(ipu, reg & ~DP_COM_CONF_GWCKE, DP_COM_CONF(flow));
	}

	reg = ipu_cm_read(ipu, IPU_SRM_PRI2) | 0x8;
	ipu_cm_write(ipu, reg, IPU_SRM_PRI2);

	mutex_unlock(&ipu->mutex_lock);

	_ipu_put(ipu);

	return 0;
}
EXPORT_SYMBOL(ipu_disp_set_color_key);

/*!
 * This function sets the gamma correction for DP output.
 *
 * @param	ipu		ipu handler
 * @param       channel         Input parameter for the logical channel ID.
 *
 * @param       enable          Boolean to enable or disable gamma correction.
 *
 * @param       constk        	Gamma piecewise linear approximation constk coeff.
 *
 * @param       slopek        	Gamma piecewise linear approximation slopek coeff.
 *
 * @return      Returns 0 on success or negative error code on fail
 */
int32_t ipu_disp_set_gamma_correction(struct ipu_soc *ipu, ipu_channel_t channel, bool enable, int constk[], int slopek[])
{
	uint32_t reg, flow, i;

	if (channel == MEM_BG_SYNC || channel == MEM_FG_SYNC)
		flow = DP_SYNC;
	else if (channel == MEM_BG_ASYNC0 || channel == MEM_FG_ASYNC0)
		flow = DP_ASYNC0;
	else if (channel == MEM_BG_ASYNC1 || channel == MEM_FG_ASYNC1)
		flow = DP_ASYNC1;
	else
		return -EINVAL;

	_ipu_get(ipu);

	mutex_lock(&ipu->mutex_lock);

	for (i = 0; i < 8; i++)
		ipu_dp_write(ipu, (constk[2*i] & 0x1ff) | ((constk[2*i+1] & 0x1ff) << 16), DP_GAMMA_C(flow, i));
	for (i = 0; i < 4; i++)
		ipu_dp_write(ipu, (slopek[4*i] & 0xff) | ((slopek[4*i+1] & 0xff) << 8) |
			((slopek[4*i+2] & 0xff) << 16) | ((slopek[4*i+3] & 0xff) << 24), DP_GAMMA_S(flow, i));

	reg = ipu_dp_read(ipu, DP_COM_CONF(flow));
	if (enable) {
		if ((ipu->bg_csc_type == RGB2YUV) || (ipu->bg_csc_type == YUV2YUV))
			reg |= DP_COM_CONF_GAMMA_YUV_EN;
		else
			reg &= ~DP_COM_CONF_GAMMA_YUV_EN;
		ipu_dp_write(ipu, reg | DP_COM_CONF_GAMMA_EN, DP_COM_CONF(flow));
	} else
		ipu_dp_write(ipu, reg & ~DP_COM_CONF_GAMMA_EN, DP_COM_CONF(flow));

	reg = ipu_cm_read(ipu, IPU_SRM_PRI2) | 0x8;
	ipu_cm_write(ipu, reg, IPU_SRM_PRI2);

	mutex_unlock(&ipu->mutex_lock);

	_ipu_put(ipu);

	return 0;
}
EXPORT_SYMBOL(ipu_disp_set_gamma_correction);

/*!
 * This function sets the window position of the foreground or background plane.
 * modes.
 *
 * @param	ipu		ipu handler
 * @param       channel         Input parameter for the logical channel ID.
 *
 * @param       x_pos           The X coordinate position to place window at.
 *                              The position is relative to the top left corner.
 *
 * @param       y_pos           The Y coordinate position to place window at.
 *                              The position is relative to the top left corner.
 *
 * @return      Returns 0 on success or negative error code on fail
 */
int32_t _ipu_disp_set_window_pos(struct ipu_soc *ipu, ipu_channel_t channel,
				int16_t x_pos, int16_t y_pos)
{
	u32 reg;
	uint32_t flow = 0;
	uint32_t dp_srm_shift;

	if ((channel == MEM_FG_SYNC) || (channel == MEM_BG_SYNC)) {
		flow = DP_SYNC;
		dp_srm_shift = 3;
	} else if (channel == MEM_FG_ASYNC0) {
		flow = DP_ASYNC0;
		dp_srm_shift = 5;
	} else if (channel == MEM_FG_ASYNC1) {
		flow = DP_ASYNC1;
		dp_srm_shift = 7;
	} else
		return -EINVAL;

	ipu_dp_write(ipu, (x_pos << 16) | y_pos, DP_FG_POS(flow));

	if (ipu_is_channel_busy(ipu, channel)) {
		/* controled by FSU if channel enabled */
		reg = ipu_cm_read(ipu, IPU_SRM_PRI2) & (~(0x3 << dp_srm_shift));
		reg |= (0x1 << dp_srm_shift);
		ipu_cm_write(ipu, reg, IPU_SRM_PRI2);
	} else {
		/* disable auto swap, controled by MCU if channel disabled */
		reg = ipu_cm_read(ipu, IPU_SRM_PRI2) & (~(0x3 << dp_srm_shift));
		ipu_cm_write(ipu, reg, IPU_SRM_PRI2);
	}

	return 0;
}

int32_t ipu_disp_set_window_pos(struct ipu_soc *ipu, ipu_channel_t channel,
				int16_t x_pos, int16_t y_pos)
{
	int ret;

	_ipu_get(ipu);
	mutex_lock(&ipu->mutex_lock);
	ret = _ipu_disp_set_window_pos(ipu, channel, x_pos, y_pos);
	mutex_unlock(&ipu->mutex_lock);
	_ipu_put(ipu);
	return ret;
}
EXPORT_SYMBOL(ipu_disp_set_window_pos);

int32_t _ipu_disp_get_window_pos(struct ipu_soc *ipu, ipu_channel_t channel,
				int16_t *x_pos, int16_t *y_pos)
{
	u32 reg;
	uint32_t flow = 0;

	if (channel == MEM_FG_SYNC)
		flow = DP_SYNC;
	else if (channel == MEM_FG_ASYNC0)
		flow = DP_ASYNC0;
	else if (channel == MEM_FG_ASYNC1)
		flow = DP_ASYNC1;
	else
		return -EINVAL;

	reg = ipu_dp_read(ipu, DP_FG_POS(flow));

	*x_pos = (reg >> 16) & 0x7FF;
	*y_pos = reg & 0x7FF;

	return 0;
}
int32_t ipu_disp_get_window_pos(struct ipu_soc *ipu, ipu_channel_t channel,
				int16_t *x_pos, int16_t *y_pos)
{
	int ret;

	_ipu_get(ipu);
	mutex_lock(&ipu->mutex_lock);
	ret = _ipu_disp_get_window_pos(ipu, channel, x_pos, y_pos);
	mutex_unlock(&ipu->mutex_lock);
	_ipu_put(ipu);
	return ret;
}
EXPORT_SYMBOL(ipu_disp_get_window_pos);

void ipu_reset_disp_panel(struct ipu_soc *ipu)
{
	uint32_t tmp;

	tmp = ipu_di_read(ipu, 1, DI_GENERAL);
	ipu_di_write(ipu, 1, tmp | 0x08, DI_GENERAL);
	msleep(10); /* tRES >= 100us */
	tmp = ipu_di_read(ipu, 1, DI_GENERAL);
	ipu_di_write(ipu, 1, tmp & ~0x08, DI_GENERAL);
	msleep(60);

	return;
}
EXPORT_SYMBOL(ipu_reset_disp_panel);

void ipu_disp_init(struct ipu_soc *ipu)
{
	ipu->fg_csc_type = ipu->bg_csc_type = CSC_NONE;
	ipu->color_key_4rgb = true;
	_ipu_init_dc_mappings(ipu);
	_ipu_dmfc_init(ipu, DMFC_NORMAL, 1);
}
