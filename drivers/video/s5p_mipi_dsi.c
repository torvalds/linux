/* linux/drivers/video/s5p_mipi_dsi.c
 *
 * Samsung SoC MIPI-DSIM driver.
 *
 * Copyright (c) 2011 Samsung Electronics
 *
 * InKi Dae, <inki.dae@samsung.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
*/

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/clk.h>
#include <linux/mutex.h>
#include <linux/wait.h>
#include <linux/fs.h>
#include <linux/mm.h>
#include <linux/fb.h>
#include <linux/ctype.h>
#include <linux/platform_device.h>
#include <linux/io.h>
#include <linux/irq.h>
#include <linux/memory.h>
#include <linux/delay.h>
#include <linux/interrupt.h>
#include <linux/kthread.h>
#include <linux/regulator/consumer.h>
#include <linux/notifier.h>
#include <linux/pm_runtime.h>

#include <linux/gpio.h>

#include <video/mipi_display.h>

#include <plat/fb.h>
#include <plat/regs-mipidsim.h>
#include <plat/dsim.h>

#include <mach/map.h>

#include "s5p_mipi_dsi_lowlevel.h"
#include "s5p_mipi_dsi.h"

#ifdef CONFIG_HAS_EARLYSUSPEND
#include <linux/earlysuspend.h>
#endif

static unsigned int dpll_table[15] = {
	100, 120, 170, 220, 270,
	320, 390, 450, 510, 560,
	640, 690, 770, 870, 950 };

#ifdef CONFIG_LCD_MIPI_TC358764
int s5p_mipi_dsi_wait_int_status(struct mipi_dsim_device *dsim, unsigned int intSrc)
{
	while (1) {
		if ((s5p_mipi_dsi_get_int_status(dsim) & intSrc)) {
			s5p_mipi_dsi_clear_int_status(dsim, intSrc);
			return 1;
		}else if ((s5p_mipi_dsi_get_FIFOCTRL_status(dsim) & 0xf00000) == 0)
			return 0;
	}
}

static int s5p_mipi_dsi_fb_notifier_callback(struct notifier_block *self,
		unsigned long event, void *data)
{
	struct mipi_dsim_device *dsim;

	if (event != FB_EVENT_RESUME)
		return 0;

	dsim = container_of(self, struct mipi_dsim_device, fb_notif);
	s5p_mipi_dsi_func_reset(dsim);

	return 0;
}

static int s5p_mipi_dsi_register_fb(struct mipi_dsim_device *dsim)
{
	memset(&dsim->fb_notif, 0, sizeof(dsim->fb_notif));
	dsim->fb_notif.notifier_call = s5p_mipi_dsi_fb_notifier_callback;

	return fb_register_client(&dsim->fb_notif);
}
#endif

static void s5p_mipi_dsi_long_data_wr(struct mipi_dsim_device *dsim, unsigned int data0, unsigned int data1)
{
	unsigned int data_cnt = 0, payload = 0;

	/* in case that data count is more then 4 */
	for (data_cnt = 0; data_cnt < data1; data_cnt += 4) {
		/*
		 * after sending 4bytes per one time,
		 * send remainder data less then 4.
		 */
		if ((data1 - data_cnt) < 4) {
			if ((data1 - data_cnt) == 3) {
				payload = *(u8 *)(data0 + data_cnt) |
				    (*(u8 *)(data0 + (data_cnt + 1))) << 8 |
					(*(u8 *)(data0 + (data_cnt + 2))) << 16;
			dev_dbg(dsim->dev, "count = 3 payload = %x, %x %x %x\n",
				payload, *(u8 *)(data0 + data_cnt),
				*(u8 *)(data0 + (data_cnt + 1)),
				*(u8 *)(data0 + (data_cnt + 2)));
			} else if ((data1 - data_cnt) == 2) {
				payload = *(u8 *)(data0 + data_cnt) |
					(*(u8 *)(data0 + (data_cnt + 1))) << 8;
			dev_dbg(dsim->dev,
				"count = 2 payload = %x, %x %x\n", payload,
				*(u8 *)(data0 + data_cnt),
				*(u8 *)(data0 + (data_cnt + 1)));
			} else if ((data1 - data_cnt) == 1) {
				payload = *(u8 *)(data0 + data_cnt);
			}

			s5p_mipi_dsi_wr_tx_data(dsim, payload);
		/* send 4bytes per one time. */
		} else {
			payload = *(u8 *)(data0 + data_cnt) |
				(*(u8 *)(data0 + (data_cnt + 1))) << 8 |
				(*(u8 *)(data0 + (data_cnt + 2))) << 16 |
				(*(u8 *)(data0 + (data_cnt + 3))) << 24;

			dev_dbg(dsim->dev,
				"count = 4 payload = %x, %x %x %x %x\n",
				payload, *(u8 *)(data0 + data_cnt),
				*(u8 *)(data0 + (data_cnt + 1)),
				*(u8 *)(data0 + (data_cnt + 2)),
				*(u8 *)(data0 + (data_cnt + 3)));

			s5p_mipi_dsi_wr_tx_data(dsim, payload);
		}
	}
}

int s5p_mipi_dsi_wr_data(struct mipi_dsim_device *dsim, unsigned int data_id,
	unsigned int data0, unsigned int data1)
{
	unsigned long delay_val, udelay;
	unsigned int check_rx_ack = 0;

	if (dsim->state == DSIM_STATE_ULPS) {
		dev_err(dsim->dev, "state is ULPS.\n");

		return -EINVAL;
	}

	delay_val = MHZ / dsim->dsim_config->esc_clk;
	udelay = 10 * delay_val;

	mdelay(udelay);

	switch (data_id) {
	/* short packet types of packet types for command. */
	case MIPI_DSI_GENERIC_SHORT_WRITE_0_PARAM:
	case MIPI_DSI_GENERIC_SHORT_WRITE_1_PARAM:
	case MIPI_DSI_GENERIC_SHORT_WRITE_2_PARAM:
	case MIPI_DSI_DCS_SHORT_WRITE:
	case MIPI_DSI_DCS_SHORT_WRITE_PARAM:
	case MIPI_DSI_SET_MAXIMUM_RETURN_PACKET_SIZE:
		s5p_mipi_dsi_wr_tx_header(dsim, data_id, data0, data1);
		if (check_rx_ack)
			/* process response func should be implemented */
			return 0;
		else
			return -EINVAL;

	/* general command */
	case MIPI_DSI_COLOR_MODE_OFF:
	case MIPI_DSI_COLOR_MODE_ON:
	case MIPI_DSI_SHUTDOWN_PERIPHERAL:
	case MIPI_DSI_TURN_ON_PERIPHERAL:
		s5p_mipi_dsi_wr_tx_header(dsim, data_id, data0, data1);
		if (check_rx_ack)
			/* process response func should be implemented. */
			return 0;
		else
			return -EINVAL;

	/* packet types for video data */
	case MIPI_DSI_V_SYNC_START:
	case MIPI_DSI_V_SYNC_END:
	case MIPI_DSI_H_SYNC_START:
	case MIPI_DSI_H_SYNC_END:
	case MIPI_DSI_END_OF_TRANSMISSION:
		return 0;

	/* short and response packet types for command */
	case MIPI_DSI_GENERIC_READ_REQUEST_0_PARAM:
	case MIPI_DSI_GENERIC_READ_REQUEST_1_PARAM:
	case MIPI_DSI_GENERIC_READ_REQUEST_2_PARAM:
	case MIPI_DSI_DCS_READ:
		s5p_mipi_dsi_clear_all_interrupt(dsim);
		s5p_mipi_dsi_wr_tx_header(dsim, data_id, data0, data1);
		/* process response func should be implemented. */
		return 0;

	/* long packet type and null packet */
	case MIPI_DSI_NULL_PACKET:
	case MIPI_DSI_BLANKING_PACKET:
		return 0;
	case MIPI_DSI_GENERIC_LONG_WRITE:
	case MIPI_DSI_DCS_LONG_WRITE:
	{
		unsigned int size, data_cnt = 0, payload = 0;

		size = data1 * 4;

		/* if data count is less then 4, then send 3bytes data.  */
		if (data1 < 4) {
			payload = *(u8 *)(data0) |
				*(u8 *)(data0 + 1) << 8 |
				*(u8 *)(data0 + 2) << 16;

			s5p_mipi_dsi_wr_tx_data(dsim, payload);

			dev_dbg(dsim->dev, "count = %d payload = %x,%x %x %x\n",
				data1, payload,
				*(u8 *)(data0 + data_cnt),
				*(u8 *)(data0 + (data_cnt + 1)),
				*(u8 *)(data0 + (data_cnt + 2)));
		/* in case that data count is more then 4 */
		} else
			s5p_mipi_dsi_long_data_wr(dsim, data0, data1);

		/* put data into header fifo */
		s5p_mipi_dsi_wr_tx_header(dsim, data_id, data1 & 0xff,
			(data1 & 0xff00) >> 8);
	}
#ifdef CONFIG_LCD_MIPI_TC358764
	if (s5p_mipi_dsi_wait_int_status(dsim, INTSRC_SFR_FIFO_EMPTY) == 0)
		return -1;
#endif

	if (check_rx_ack)
		/* process response func should be implemented. */
		return 0;
	else
		return -EINVAL;

	/* packet typo for video data */
	case MIPI_DSI_PACKED_PIXEL_STREAM_16:
	case MIPI_DSI_PACKED_PIXEL_STREAM_18:
	case MIPI_DSI_PIXEL_STREAM_3BYTE_18:
	case MIPI_DSI_PACKED_PIXEL_STREAM_24:
		if (check_rx_ack)
			/* process response func should be implemented. */
			return 0;
		else
			return -EINVAL;
	default:
		dev_warn(dsim->dev,
			"data id %x is not supported current DSI spec.\n",
			data_id);

		return -EINVAL;
	}

	return 0;
}

int s5p_mipi_dsi_pll_on(struct mipi_dsim_device *dsim, unsigned int enable)
{
	int sw_timeout;

	if (enable) {
		sw_timeout = 1000;

		s5p_mipi_dsi_clear_interrupt(dsim, INTSRC_PLL_STABLE);
		s5p_mipi_dsi_enable_pll(dsim, 1);
		while (1) {
			sw_timeout--;
			if (s5p_mipi_dsi_is_pll_stable(dsim))
				return 0;
			if (sw_timeout == 0)
				return -EINVAL;
		}
	} else
		s5p_mipi_dsi_enable_pll(dsim, 0);

	return 0;
}

unsigned long s5p_mipi_dsi_change_pll(struct mipi_dsim_device *dsim,
	unsigned int pre_divider, unsigned int main_divider,
	unsigned int scaler)
{
	unsigned long dfin_pll, dfvco, dpll_out;
	unsigned int i, freq_band = 0xf;

	dfin_pll = (FIN_HZ / pre_divider);

	if (dfin_pll < DFIN_PLL_MIN_HZ || dfin_pll > DFIN_PLL_MAX_HZ) {
		dev_warn(dsim->dev, "fin_pll range should be 6MHz ~ 12MHz\n");
		s5p_mipi_dsi_enable_afc(dsim, 0, 0);
	} else {
		if (dfin_pll < 7 * MHZ)
			s5p_mipi_dsi_enable_afc(dsim, 1, 0x1);
		else if (dfin_pll < 8 * MHZ)
			s5p_mipi_dsi_enable_afc(dsim, 1, 0x0);
		else if (dfin_pll < 9 * MHZ)
			s5p_mipi_dsi_enable_afc(dsim, 1, 0x3);
		else if (dfin_pll < 10 * MHZ)
			s5p_mipi_dsi_enable_afc(dsim, 1, 0x2);
		else if (dfin_pll < 11 * MHZ)
			s5p_mipi_dsi_enable_afc(dsim, 1, 0x5);
		else
			s5p_mipi_dsi_enable_afc(dsim, 1, 0x4);
	}

	dfvco = dfin_pll * main_divider;
	dev_dbg(dsim->dev, "dfvco = %lu, dfin_pll = %lu, main_divider = %d\n",
				dfvco, dfin_pll, main_divider);
	if (dfvco < DFVCO_MIN_HZ || dfvco > DFVCO_MAX_HZ)
		dev_warn(dsim->dev, "fvco range should be 500MHz ~ 1000MHz\n");

	dpll_out = dfvco / (1 << scaler);
	dev_dbg(dsim->dev, "dpll_out = %lu, dfvco = %lu, scaler = %d\n",
		dpll_out, dfvco, scaler);

	for (i = 0; i < ARRAY_SIZE(dpll_table); i++) {
		if (dpll_out < dpll_table[i] * MHZ) {
			freq_band = i;
			break;
		}
	}

	dev_dbg(dsim->dev, "freq_band = %d\n", freq_band);

	s5p_mipi_dsi_pll_freq(dsim, pre_divider, main_divider, scaler);

	s5p_mipi_dsi_hs_zero_ctrl(dsim, 0);
	s5p_mipi_dsi_prep_ctrl(dsim, 0);

	/* Freq Band */
	s5p_mipi_dsi_pll_freq_band(dsim, freq_band);

	/* Stable time */
	s5p_mipi_dsi_pll_stable_time(dsim, dsim->dsim_config->pll_stable_time);

	/* Enable PLL */
	dev_dbg(dsim->dev, "FOUT of mipi dphy pll is %luMHz\n",
		(dpll_out / MHZ));

	return dpll_out;
}

int s5p_mipi_dsi_set_clock(struct mipi_dsim_device *dsim,
	unsigned int byte_clk_sel, unsigned int enable)
{
	unsigned int esc_div;
	unsigned long esc_clk_error_rate;

	if (enable) {
		dsim->e_clk_src = byte_clk_sel;

		/* Escape mode clock and byte clock source */
		s5p_mipi_dsi_set_byte_clock_src(dsim, byte_clk_sel);

		/* DPHY, DSIM Link : D-PHY clock out */
		if (byte_clk_sel == DSIM_PLL_OUT_DIV8) {
			dsim->hs_clk = s5p_mipi_dsi_change_pll(dsim,
				dsim->dsim_config->p, dsim->dsim_config->m,
				dsim->dsim_config->s);
			if (dsim->hs_clk == 0) {
				dev_err(dsim->dev,
					"failed to get hs clock.\n");
				return -EINVAL;
			}

			dsim->byte_clk = dsim->hs_clk / 8;
			s5p_mipi_dsi_enable_pll_bypass(dsim, 0);
			s5p_mipi_dsi_pll_on(dsim, 1);
		/* DPHY : D-PHY clock out, DSIM link : external clock out */
		} else if (byte_clk_sel == DSIM_EXT_CLK_DIV8)
			dev_warn(dsim->dev,
				"this project is not support \
				external clock source for MIPI DSIM\n");
		else if (byte_clk_sel == DSIM_EXT_CLK_BYPASS)
			dev_warn(dsim->dev,
				"this project is not support \
				external clock source for MIPI DSIM\n");

		/* escape clock divider */
		esc_div = dsim->byte_clk / (dsim->dsim_config->esc_clk);
		dev_dbg(dsim->dev,
			"esc_div = %d, byte_clk = %lu, esc_clk = %lu\n",
			esc_div, dsim->byte_clk, dsim->dsim_config->esc_clk);
		if ((dsim->byte_clk / esc_div) >= (20 * MHZ) ||
				(dsim->byte_clk / esc_div) >
					dsim->dsim_config->esc_clk)
			esc_div += 1;

		dsim->escape_clk = dsim->byte_clk / esc_div;
		dev_dbg(dsim->dev,
			"escape_clk = %lu, byte_clk = %lu, esc_div = %d\n",
			dsim->escape_clk, dsim->byte_clk, esc_div);

		/* enable escape clock. */
		s5p_mipi_dsi_enable_byte_clock(dsim, DSIM_ESCCLK_ON);

		/* enable byte clk and escape clock */
		s5p_mipi_dsi_set_esc_clk_prs(dsim, 1, esc_div);
		/* escape clock on lane */
		s5p_mipi_dsi_enable_esc_clk_on_lane(dsim,
			(DSIM_LANE_CLOCK | dsim->data_lane), 1);

		dev_dbg(dsim->dev, "byte clock is %luMHz\n",
			(dsim->byte_clk / MHZ));
		dev_dbg(dsim->dev, "escape clock that user's need is %lu\n",
			(dsim->dsim_config->esc_clk / MHZ));
		dev_dbg(dsim->dev, "escape clock divider is %x\n", esc_div);
		dev_dbg(dsim->dev, "escape clock is %luMHz\n",
			((dsim->byte_clk / esc_div) / MHZ));

		if ((dsim->byte_clk / esc_div) > dsim->escape_clk) {
			esc_clk_error_rate = dsim->escape_clk /
				(dsim->byte_clk / esc_div);
			dev_warn(dsim->dev, "error rate is %lu over.\n",
				(esc_clk_error_rate / 100));
		} else if ((dsim->byte_clk / esc_div) < (dsim->escape_clk)) {
			esc_clk_error_rate = (dsim->byte_clk / esc_div) /
				dsim->escape_clk;
			dev_warn(dsim->dev, "error rate is %lu under.\n",
				(esc_clk_error_rate / 100));
		}
	} else {
		s5p_mipi_dsi_enable_esc_clk_on_lane(dsim,
			(DSIM_LANE_CLOCK | dsim->data_lane), 0);
		s5p_mipi_dsi_set_esc_clk_prs(dsim, 0, 0);

		/* disable escape clock. */
		s5p_mipi_dsi_enable_byte_clock(dsim, DSIM_ESCCLK_OFF);

		if (byte_clk_sel == DSIM_PLL_OUT_DIV8)
			s5p_mipi_dsi_pll_on(dsim, 0);
	}

	return 0;
}

void s5p_mipi_dsi_d_phy_onoff(struct mipi_dsim_device *dsim,
	unsigned int enable)
{
	if (dsim->pd->init_d_phy)
		dsim->pd->init_d_phy(dsim, enable);
}

int s5p_mipi_dsi_init_dsim(struct mipi_dsim_device *dsim)
{
	s5p_mipi_dsi_d_phy_onoff(dsim, 1);

	dsim->state = DSIM_STATE_INIT;

	switch (dsim->dsim_config->e_no_data_lane) {
	case DSIM_DATA_LANE_1:
		dsim->data_lane = DSIM_LANE_DATA0;
		break;
	case DSIM_DATA_LANE_2:
		dsim->data_lane = DSIM_LANE_DATA0 | DSIM_LANE_DATA1;
		break;
	case DSIM_DATA_LANE_3:
		dsim->data_lane = DSIM_LANE_DATA0 | DSIM_LANE_DATA1 |
			DSIM_LANE_DATA2;
		break;
	case DSIM_DATA_LANE_4:
		dsim->data_lane = DSIM_LANE_DATA0 | DSIM_LANE_DATA1 |
			DSIM_LANE_DATA2 | DSIM_LANE_DATA3;
		break;
	default:
		dev_info(dsim->dev, "data lane is invalid.\n");
		return -EINVAL;
	};

	s5p_mipi_dsi_sw_reset(dsim);
	s5p_mipi_dsi_dp_dn_swap(dsim, 0);

	return 0;
}

int s5p_mipi_dsi_enable_frame_done_int(struct mipi_dsim_device *dsim,
	unsigned int enable)
{
	/* enable only frame done interrupt */
	s5p_mipi_dsi_set_interrupt_mask(dsim, INTMSK_FRAME_DONE, enable);

	return 0;
}

int s5p_mipi_dsi_set_display_mode(struct mipi_dsim_device *dsim,
	struct mipi_dsim_config *dsim_config)
{
	struct fb_videomode *lcd_video = NULL;
	struct s3c_fb_pd_win *pd;
	unsigned int width = 0, height = 0;
	pd = (struct s3c_fb_pd_win *)dsim->dsim_config->lcd_panel_info;
	lcd_video = (struct fb_videomode *)&pd->win_mode;

	width = dsim->pd->dsim_lcd_config->lcd_size.width;
	height = dsim->pd->dsim_lcd_config->lcd_size.height;

	/* in case of VIDEO MODE (RGB INTERFACE) */
	if (dsim->dsim_config->e_interface == (u32) DSIM_VIDEO) {
			s5p_mipi_dsi_set_main_disp_vporch(dsim,
				DSIM_CMD_LEN,
				dsim->pd->dsim_lcd_config->rgb_timing.left_margin,
				dsim->pd->dsim_lcd_config->rgb_timing.right_margin);
			s5p_mipi_dsi_set_main_disp_hporch(dsim,
				dsim->pd->dsim_lcd_config->rgb_timing.upper_margin,
				dsim->pd->dsim_lcd_config->rgb_timing.lower_margin);
			s5p_mipi_dsi_set_main_disp_sync_area(dsim,
				dsim->pd->dsim_lcd_config->rgb_timing.vsync_len,
				dsim->pd->dsim_lcd_config->rgb_timing.hsync_len);
	}
	s5p_mipi_dsi_set_main_disp_resol(dsim, height, width);
	s5p_mipi_dsi_display_config(dsim);
	return 0;
}

int s5p_mipi_dsi_init_link(struct mipi_dsim_device *dsim)
{
	unsigned int time_out = 100;
	unsigned int id;
	id = dsim->id;
	switch (dsim->state) {
	case DSIM_STATE_INIT:
		s5p_mipi_dsi_sw_reset(dsim);
		s5p_mipi_dsi_init_fifo_pointer(dsim, 0x1f);

		/* dsi configuration */
		s5p_mipi_dsi_init_config(dsim);
		s5p_mipi_dsi_enable_lane(dsim, DSIM_LANE_CLOCK, 1);
		s5p_mipi_dsi_enable_lane(dsim, dsim->data_lane, 1);

		/* set clock configuration */
		s5p_mipi_dsi_set_clock(dsim, dsim->dsim_config->e_byte_clk, 1);

		mdelay(100);

		/* check clock and data lane state are stop state */
		while (!(s5p_mipi_dsi_is_lane_state(dsim))) {
			time_out--;
			if (time_out == 0) {
				dev_err(dsim->dev,
					"DSI Master is not stop state.\n");
				dev_err(dsim->dev,
					"Check initialization process\n");

				return -EINVAL;
			}
		}

		if (time_out != 0) {
			dev_info(dsim->dev,
				"DSI Master driver has been completed.\n");
			dev_info(dsim->dev, "DSI Master state is stop state\n");
		}

		dsim->state = DSIM_STATE_STOP;

		/* BTA sequence counters */
		s5p_mipi_dsi_set_stop_state_counter(dsim,
			dsim->dsim_config->stop_holding_cnt);
		s5p_mipi_dsi_set_bta_timeout(dsim,
			dsim->dsim_config->bta_timeout);
		s5p_mipi_dsi_set_lpdr_timeout(dsim,
			dsim->dsim_config->rx_timeout);

		return 0;
	default:
		dev_info(dsim->dev, "DSI Master is already init.\n");
		return 0;
	}

	return 0;
}

int s5p_mipi_dsi_set_hs_enable(struct mipi_dsim_device *dsim)
{
	if (dsim->state == DSIM_STATE_STOP) {
		if (dsim->e_clk_src != DSIM_EXT_CLK_BYPASS) {
			dsim->state = DSIM_STATE_HSCLKEN;

			 /* set LCDC and CPU transfer mode to HS. */
			s5p_mipi_dsi_set_lcdc_transfer_mode(dsim, 0);
			s5p_mipi_dsi_set_cpu_transfer_mode(dsim, 0);

			s5p_mipi_dsi_enable_hs_clock(dsim, 1);

			return 0;
		} else
			dev_warn(dsim->dev,
				"clock source is external bypass.\n");
	} else
		dev_warn(dsim->dev, "DSIM is not stop state.\n");

	return 0;
}

int s5p_mipi_dsi_set_data_transfer_mode(struct mipi_dsim_device *dsim,
		unsigned int mode)
{
	if (mode) {
		if (dsim->state != DSIM_STATE_HSCLKEN) {
			dev_err(dsim->dev, "HS Clock lane is not enabled.\n");
			return -EINVAL;
		}

		s5p_mipi_dsi_set_lcdc_transfer_mode(dsim, 0);
	} else {
		if (dsim->state == DSIM_STATE_INIT || dsim->state ==
			DSIM_STATE_ULPS) {
			dev_err(dsim->dev,
				"DSI Master is not STOP or HSDT state.\n");
			return -EINVAL;
		}

		s5p_mipi_dsi_set_cpu_transfer_mode(dsim, 0);
	}
	return 0;
}

int s5p_mipi_dsi_get_frame_done_status(struct mipi_dsim_device *dsim)
{
	return _s5p_mipi_dsi_get_frame_done_status(dsim);
}

int s5p_mipi_dsi_clear_frame_done(struct mipi_dsim_device *dsim)
{
	_s5p_mipi_dsi_clear_frame_done(dsim);

	return 0;
}

static irqreturn_t s5p_mipi_dsi_interrupt_handler(int irq, void *dev_id)
{
	unsigned int int_src;
	struct mipi_dsim_device *dsim = dev_id;

	s5p_mipi_dsi_set_interrupt_mask(dsim, 0xffffffff, 1);

	int_src = readl(dsim->reg_base + S5P_DSIM_INTSRC);
	s5p_mipi_dsi_clear_interrupt(dsim, int_src);

	if (!(int_src && INTSRC_PLL_STABLE))
		printk(KERN_ERR "mipi dsi interrupt source (%x).\n", int_src);

	s5p_mipi_dsi_set_interrupt_mask(dsim, 0xffffffff, 0);
	return IRQ_HANDLED;
}

#ifdef CONFIG_PM
#ifdef CONFIG_HAS_EARLYSUSPEND
static void s5p_mipi_dsi_early_suspend(struct early_suspend *handler)
{
	struct mipi_dsim_device *dsim =
		container_of(handler, struct mipi_dsim_device, early_suspend);
	struct platform_device *pdev = to_platform_device(dsim->dev);

	dsim->dsim_lcd_drv->suspend(dsim);
	s5p_mipi_dsi_d_phy_onoff(dsim, 0);
	clk_disable(dsim->clock);
	pm_runtime_put_sync(&pdev->dev);
}

static void s5p_mipi_dsi_late_resume(struct early_suspend *handler)
{
	struct mipi_dsim_device *dsim =
		container_of(handler, struct mipi_dsim_device, early_suspend);
	struct platform_device *pdev = to_platform_device(dsim->dev);

#if defined(CONFIG_LCD_MIPI_TC358764)
	int again = 1;
	while (again != 0 && again <= 1000) {
		pm_runtime_get_sync(&pdev->dev);
		clk_enable(dsim->clock);
		s5p_mipi_dsi_init_dsim(dsim);
		s5p_mipi_dsi_init_link(dsim);
		s5p_mipi_dsi_enable_hs_clock(dsim, 1);
		s5p_mipi_dsi_set_cpu_transfer_mode(dsim, 1);
		s5p_mipi_dsi_set_display_mode(dsim, dsim->dsim_config);
		s5p_mipi_dsi_clear_int_status(dsim, INTSRC_SFR_FIFO_EMPTY);
		if (dsim->dsim_lcd_drv->displayon(dsim) == 0)
			again++;
		else
			again = 0;
		s5p_mipi_dsi_set_cpu_transfer_mode(dsim, 0);
		if (again != 0)
			s5p_mipi_dsi_sw_reset(dsim);
	}
#else
	pm_runtime_get_sync(&pdev->dev);
	clk_enable(dsim->clock);
	s5p_mipi_dsi_init_dsim(dsim);
	s5p_mipi_dsi_init_link(dsim);
	s5p_mipi_dsi_set_data_transfer_mode(dsim, 0);
	s5p_mipi_dsi_set_display_mode(dsim, dsim->dsim_config);
	dsim->dsim_lcd_drv->displayon(dsim);
	s5p_mipi_dsi_set_hs_enable(dsim);
#endif
}
#else
static int s5p_mipi_dsi_suspend(struct device *dev)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct mipi_dsim_device *dsim = platform_get_drvdata(pdev);

	dsim->dsim_lcd_drv->suspend(dsim);
	s5p_mipi_dsi_d_phy_onoff(dsim, 0);
	clk_disable(dsim->clock);
	pm_runtime_put_sync(dev);
	return 0;
}

static int s5p_mipi_dsi_resume(struct device *dev)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct mipi_dsim_device *dsim = platform_get_drvdata(pdev);

#if defined(CONFIG_LCD_MIPI_TC358764)
	int again = 1;
	while (again != 0 && again <= 1000) {
		pm_runtime_get_sync(dev);
		clk_enable(dsim->clock);
		s5p_mipi_dsi_init_dsim(dsim);
		s5p_mipi_dsi_init_link(dsim);
		s5p_mipi_dsi_enable_hs_clock(dsim, 1);
		s5p_mipi_dsi_set_cpu_transfer_mode(dsim, 1);
		s5p_mipi_dsi_set_display_mode(dsim, dsim->dsim_config);
		s5p_mipi_dsi_clear_int_status(dsim, INTSRC_SFR_FIFO_EMPTY);
		if (dsim->dsim_lcd_drv->displayon(dsim) == 0)
			again++;
		else
			again = 0;
		s5p_mipi_dsi_set_cpu_transfer_mode(dsim, 0);
		if (again != 0)
			s5p_mipi_dsi_sw_reset(dsim);
	}
#else
	pm_runtime_get_sync(dev);
	clk_enable(dsim->clock);
	s5p_mipi_dsi_init_dsim(dsim);
	s5p_mipi_dsi_init_link(dsim);
	s5p_mipi_dsi_set_data_transfer_mode(dsim, 0);
	s5p_mipi_dsi_set_display_mode(dsim, dsim->dsim_config);
	dsim->dsim_lcd_drv->displayon(dsim);
	s5p_mipi_dsi_set_hs_enable(dsim);
#endif

	return 0;
}
#endif
static int s5p_mipi_dsi_runtime_suspend(struct device *dev)
{
	return 0;
}

static int s5p_mipi_dsi_runtime_resume(struct device *dev)
{
	return 0;
}
#else
#define s5p_mipi_dsi_suspend NULL
#define s5p_mipi_dsi_resume NULL
#define s5p_mipi_dsi_runtime_suspend NULL
#define s5p_mipi_dsi_runtime_resume NULL
#endif


static int s5p_mipi_dsi_probe(struct platform_device *pdev)
{
	struct resource *res;
	struct mipi_dsim_device *dsim = NULL;
	struct mipi_dsim_config *dsim_config;
	struct s5p_platform_mipi_dsim *dsim_pd;
	int ret = -1;

#ifdef CONFIG_LCD_MIPI_TC358764
	int again = 1;
#endif

	if (!dsim)
		dsim = kzalloc(sizeof(struct mipi_dsim_device),
			GFP_KERNEL);
	if (!dsim) {
		dev_err(&pdev->dev, "failed to allocate dsim object.\n");
		return -EFAULT;
	}

	dsim->pd = to_dsim_plat(&pdev->dev);
	dsim->dev = &pdev->dev;
	dsim->id = pdev->id;

#ifdef CONFIG_LCD_MIPI_TC358764
	ret = s5p_mipi_dsi_register_fb(dsim);
	if (ret) {
		dev_err(&pdev->dev, "failed to register fb notifier chain\n");
		return -EFAULT;
	}
#endif

	pm_runtime_enable(&pdev->dev);
	pm_runtime_get_sync(&pdev->dev);

	/* get s5p_platform_mipi_dsim. */
	dsim_pd = (struct s5p_platform_mipi_dsim *)dsim->pd;
	/* get mipi_dsim_config. */
	dsim_config = dsim_pd->dsim_config;
	dsim->dsim_config = dsim_config;

	dsim->clock = clk_get(&pdev->dev, dsim->pd->clk_name);
	if (IS_ERR(dsim->clock)) {
		dev_err(&pdev->dev, "failed to get dsim clock source\n");
		goto err_clock_get;
	}
	clk_enable(dsim->clock);

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!res) {
		dev_err(&pdev->dev, "failed to get io memory region\n");
		ret = -EINVAL;
		goto err_platform_get;
	}
	res = request_mem_region(res->start, resource_size(res),
					dev_name(&pdev->dev));
	if (!res) {
		dev_err(&pdev->dev, "failed to request io memory region\n");
		ret = -EINVAL;
		goto err_mem_region;
	}

	dsim->res = res;
	dsim->reg_base = ioremap(res->start, resource_size(res));
	if (!dsim->reg_base) {
		dev_err(&pdev->dev, "failed to remap io region\n");
		ret = -EINVAL;
		goto err_mem_region;
	}

	/*
	 * it uses frame done interrupt handler
	 * only in case of MIPI Video mode.
	 */
	if (dsim->pd->dsim_config->e_interface == DSIM_VIDEO) {
		dsim->irq = platform_get_irq(pdev, 0);
		if (request_irq(dsim->irq, s5p_mipi_dsi_interrupt_handler,
				IRQF_DISABLED, "mipi-dsi", dsim)) {
			dev_err(&pdev->dev, "request_irq failed.\n");
			goto err_irq;
		}
	}

	dsim->dsim_lcd_drv = dsim->dsim_config->dsim_ddi_pd;

	if (dsim->dsim_config == NULL) {
		dev_err(&pdev->dev, "dsim_config is NULL.\n");
		goto err_dsim_config;
	}

#if defined(CONFIG_LCD_MIPI_TC358764)
	while (again != 0 && again <= 1000) {
		s5p_mipi_dsi_init_dsim(dsim);
		s5p_mipi_dsi_init_link(dsim);
		/* initialize mipi-dsi client(lcd panel). */
		s5p_mipi_dsi_enable_hs_clock(dsim, 1);
		s5p_mipi_dsi_set_cpu_transfer_mode(dsim, 1);
		s5p_mipi_dsi_set_display_mode(dsim, dsim->dsim_config);
		s5p_mipi_dsi_clear_int_status(dsim, INTSRC_SFR_FIFO_EMPTY);
		if (dsim->dsim_lcd_drv->displayon(dsim) == 0)
			again++;
		else
			again = 0;
		s5p_mipi_dsi_set_cpu_transfer_mode(dsim, 0);
		if (again != 0)
			s5p_mipi_dsi_sw_reset(dsim);
	}
#else
	s5p_mipi_dsi_init_dsim(dsim);
	s5p_mipi_dsi_init_link(dsim);
	/* initialize mipi-dsi client(lcd panel). */
	s5p_mipi_dsi_set_data_transfer_mode(dsim, 0);
	s5p_mipi_dsi_set_display_mode(dsim, dsim->dsim_config);
	dsim->dsim_lcd_drv->displayon(dsim);
	s5p_mipi_dsi_set_hs_enable(dsim);
#endif
	dev_info(&pdev->dev, "mipi-dsi driver(%s mode) has been probed.\n",
		(dsim_config->e_interface == DSIM_COMMAND) ?
			"CPU" : "RGB");

#ifdef CONFIG_HAS_EARLYSUSPEND
	dsim->early_suspend.suspend = s5p_mipi_dsi_early_suspend;
	dsim->early_suspend.resume = s5p_mipi_dsi_late_resume;
	dsim->early_suspend.level = EARLY_SUSPEND_LEVEL_DISABLE_FB + 1;
	register_early_suspend(&(dsim->early_suspend));
#endif
	platform_set_drvdata(pdev, dsim);

	return 0;

err_dsim_config:
err_irq:
	release_resource(dsim->res);
	kfree(dsim->res);

	iounmap((void __iomem *) dsim->reg_base);

err_mem_region:
err_platform_get:
	clk_disable(dsim->clock);
	clk_put(dsim->clock);

err_clock_get:
	kfree(dsim);
	pm_runtime_put_sync(&pdev->dev);
	return ret;

}

static int __devexit s5p_mipi_dsi_remove(struct platform_device *pdev)
{
	struct mipi_dsim_device *dsim = platform_get_drvdata(pdev);

	if (dsim->dsim_config->e_interface == DSIM_VIDEO)
		free_irq(dsim->irq, dsim);

	iounmap(dsim->reg_base);

	clk_disable(dsim->clock);
	clk_put(dsim->clock);

	release_resource(dsim->res);
	kfree(dsim->res);

	kfree(dsim);

	return 0;
}

static const struct dev_pm_ops mipi_dsi_pm_ops = {
#ifndef CONFIG_HAS_EARLYSUSPEND
	.suspend = s5p_mipi_dsi_suspend,
	.resume = s5p_mipi_dsi_resume,
#endif
	.runtime_suspend	= s5p_mipi_dsi_runtime_suspend,
	.runtime_resume		= s5p_mipi_dsi_runtime_resume,
};

static struct platform_driver s5p_mipi_dsi_driver = {
	.probe = s5p_mipi_dsi_probe,
	.remove = __devexit_p(s5p_mipi_dsi_remove),
	.driver = {
		   .name = "s5p-mipi-dsim",
		   .owner = THIS_MODULE,
		   .pm = &mipi_dsi_pm_ops,
	},
};

static int s5p_mipi_dsi_register(void)
{
	platform_driver_register(&s5p_mipi_dsi_driver);

	return 0;
}

static void s5p_mipi_dsi_unregister(void)
{
	platform_driver_unregister(&s5p_mipi_dsi_driver);
}
module_init(s5p_mipi_dsi_register);
module_exit(s5p_mipi_dsi_unregister);

MODULE_AUTHOR("InKi Dae <inki.dae@samsung.com>");
MODULE_DESCRIPTION("Samusung MIPI-DSI driver");
MODULE_LICENSE("GPL");
