/*
 * Copyright (C) 2015 Freescale Semiconductor, Inc. All Rights Reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.

 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.

 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */

#include <linux/types.h>
#include <linux/init.h>
#include <linux/platform_device.h>
#include <linux/err.h>
#include <linux/clk.h>
#include <linux/console.h>
#include <linux/io.h>
#include <linux/interrupt.h>
#include <linux/irq.h>
#include <linux/bitops.h>
#include <linux/mipi_dsi_samsung.h>
#include <linux/module.h>
#include <linux/mxcfb.h>
#include <linux/pm_runtime.h>
#include <linux/busfreq-imx.h>
#include <linux/backlight.h>
#include <linux/of_device.h>
#include <linux/of_address.h>
#include <linux/regulator/consumer.h>
#include <linux/reset.h>
#include <linux/spinlock.h>
#include <linux/delay.h>
#include <video/mipi_display.h>
#include <linux/mfd/syscon.h>

#include "mipi_dsi.h"

#define DISPDRV_MIPI			"mipi_dsi_samsung"
#define ROUND_UP(x)			((x)+1)
#define NS2PS_RATIO			(1000)
#define	MIPI_LCD_SLEEP_MODE_DELAY	(120)
#define MIPI_FIFO_TIMEOUT		msecs_to_jiffies(250)

static struct mipi_dsi_match_lcd mipi_dsi_lcd_db[] = {
#ifdef CONFIG_FB_MXC_TRULY_WVGA_SYNC_PANEL
	{
	 "TRULY-WVGA",
	 {mipid_hx8369_get_lcd_videomode, mipid_hx8369_lcd_setup}
	},
#endif
#ifdef CONFIG_FB_MXC_TRULY_PANEL_TFT3P5079E
	{
	 "TRULY-WVGA-TFT3P5079E",
	 {mipid_otm8018b_get_lcd_videomode, mipid_otm8018b_lcd_setup}
	},
#endif
#ifdef CONFIG_FB_MXC_TRULY_PANEL_TFT3P5581E
	{
	 "TRULY-WVGA-TFT3P5581E",
	 {mipid_hx8363_get_lcd_videomode, mipid_hx8363_lcd_setup}
	},
#endif
	{
	"", {NULL, NULL}
	}
};

enum mipi_dsi_mode {
	DSI_COMMAND_MODE,
	DSI_VIDEO_MODE
};

enum mipi_dsi_trans_mode {
	DSI_LP_MODE,
	DSI_HS_MODE
};

static struct regulator *mipi_phy_reg;
static DECLARE_COMPLETION(dsi_rx_done);
static DECLARE_COMPLETION(dsi_tx_done);

static void mipi_dsi_dphy_power_down(void);
static void mipi_dsi_set_mode(struct mipi_dsi_info *mipi_dsi,
			      enum mipi_dsi_trans_mode mode);

static int mipi_dsi_lcd_init(struct mipi_dsi_info *mipi_dsi,
			     struct mxc_dispdrv_setting *setting)
{
	int i, size, err;
	struct fb_videomode *mipi_lcd_modedb;
	struct fb_videomode mode;
	struct device *dev = &mipi_dsi->pdev->dev;

	for (i = 0; i < ARRAY_SIZE(mipi_dsi_lcd_db); i++) {
		if (!strcmp(mipi_dsi->lcd_panel,
			mipi_dsi_lcd_db[i].lcd_panel)) {
			mipi_dsi->lcd_callback =
				&mipi_dsi_lcd_db[i].lcd_callback;
			break;
		}
	}
	if (i == ARRAY_SIZE(mipi_dsi_lcd_db)) {
		dev_err(dev, "failed to find supported lcd panel.\n");
		return -EINVAL;
	}

	/* set default bpp to 32 if not set*/
	if (!setting->default_bpp)
		setting->default_bpp = 32;

	mipi_dsi->lcd_callback->get_mipi_lcd_videomode(&mipi_lcd_modedb, &size,
					&mipi_dsi->lcd_config);

	err = fb_find_mode(&setting->fbi->var, setting->fbi,
				setting->dft_mode_str,
				mipi_lcd_modedb, size, NULL,
				setting->default_bpp);
	if (err != 1)
		fb_videomode_to_var(&setting->fbi->var, mipi_lcd_modedb);

	INIT_LIST_HEAD(&setting->fbi->modelist);
	for (i = 0; i < size; i++) {
		fb_var_to_videomode(&mode, &setting->fbi->var);
		if (fb_mode_is_equal(&mode, mipi_lcd_modedb + i)) {
			err = fb_add_videomode(mipi_lcd_modedb + i,
					&setting->fbi->modelist);
			mipi_dsi->mode = mipi_lcd_modedb + i;
			break;
		}
	}

	if ((err < 0) || (size == i)) {
		dev_err(dev, "failed to add videomode.\n");
		return err;
	}

	return 0;
}

static void mipi_dsi_wr_tx_header(struct mipi_dsi_info *mipi_dsi,
			u8 di, u8 data0, u8 data1)
{
	unsigned int reg;

	reg = (data1 << 16) | (data0 << 8) | ((di & 0x3f) << 0);

	writel(reg, mipi_dsi->mmio_base + MIPI_DSI_PKTHDR);
}

static void mipi_dsi_wr_tx_data(struct mipi_dsi_info *mipi_dsi,
				unsigned int tx_data)
{
	writel(tx_data, mipi_dsi->mmio_base + MIPI_DSI_PAYLOAD);
}

static void mipi_dsi_long_data_wr(struct mipi_dsi_info *mipi_dsi,
			const unsigned char *data0, unsigned int data_size)
{
	unsigned int data_cnt = 0, payload = 0;

        /* in case that data count is more then 4 */
        for (data_cnt = 0; data_cnt < data_size; data_cnt += 4) {
                /*
                 * after sending 4bytes per one time,
                 * send remainder data less then 4.
                 */
                if ((data_size - data_cnt) < 4) {
                        if ((data_size - data_cnt) == 3) {
                                payload = data0[data_cnt] |
                                    data0[data_cnt + 1] << 8 |
                                        data0[data_cnt + 2] << 16;
                        dev_dbg(&mipi_dsi->pdev->dev, "count = 3 payload = %x, %x %x %x\n",
                                payload, data0[data_cnt],
                                data0[data_cnt + 1],
                                data0[data_cnt + 2]);
                        } else if ((data_size - data_cnt) == 2) {
                                payload = data0[data_cnt] |
                                        data0[data_cnt + 1] << 8;
                        dev_dbg(&mipi_dsi->pdev->dev,
                                "count = 2 payload = %x, %x %x\n", payload,
                                data0[data_cnt],
                                data0[data_cnt + 1]);
                        } else if ((data_size - data_cnt) == 1) {
                                payload = data0[data_cnt];
                        }

                        mipi_dsi_wr_tx_data(mipi_dsi, payload);
                /* send 4bytes per one time. */
                } else {
                        payload = data0[data_cnt] |
                                data0[data_cnt + 1] << 8 |
                                data0[data_cnt + 2] << 16 |
                                data0[data_cnt + 3] << 24;

                        dev_dbg(&mipi_dsi->pdev->dev,
                                "count = 4 payload = %x, %x %x %x %x\n",
                                payload, *(u8 *)(data0 + data_cnt),
                                data0[data_cnt + 1],
                                data0[data_cnt + 2],
                                data0[data_cnt + 3]);

			mipi_dsi_wr_tx_data(mipi_dsi, payload);
                }
        }
}

static int mipi_dsi_pkt_write(struct mipi_dsi_info *mipi_dsi,
		       u8 data_type, const u32 *buf, int len)
{
	int ret = 0;
	struct platform_device *pdev = mipi_dsi->pdev;
	const unsigned char *data = (const unsigned char*)buf;

	if (len == 0)
		/* handle generic short write command */
		mipi_dsi_wr_tx_header(mipi_dsi, data_type, data[0], data[1]);
	else {
		reinit_completion(&dsi_tx_done);

		/* handle generic long write command */
		mipi_dsi_long_data_wr(mipi_dsi, data, len);
		mipi_dsi_wr_tx_header(mipi_dsi, data_type, len & 0xff, (len & 0xff00) >> 8);

		ret = wait_for_completion_timeout(&dsi_tx_done, MIPI_FIFO_TIMEOUT);
		if (!ret) {
			dev_err(&pdev->dev, "wait tx done timeout!\n");
			return -ETIMEDOUT;
		}
	}
	mdelay(10);

	return 0;
}

static void mipi_dsi_rd_tx_header(struct mipi_dsi_info *mipi_dsi,
				  u8 data_type, u8 data0)
{
	unsigned int reg = (data_type << 0) | (data0 << 8);

	writel(reg, mipi_dsi->mmio_base + MIPI_DSI_PKTHDR);
}

static unsigned int mipi_dsi_rd_rx_fifo(struct mipi_dsi_info *mipi_dsi)
{
	return readl(mipi_dsi->mmio_base + MIPI_DSI_RXFIFO);
}

static int mipi_dsi_pkt_read(struct mipi_dsi_info *mipi_dsi,
				u8 data_type, u32 *buf, int len)
{
	int ret;
	struct platform_device *pdev = mipi_dsi->pdev;

	if (len <= 4) {
		reinit_completion(&dsi_rx_done);

		mipi_dsi_rd_tx_header(mipi_dsi, data_type, buf[0]);

		ret = wait_for_completion_timeout(&dsi_rx_done, MIPI_FIFO_TIMEOUT);
		if (!ret) {
			dev_err(&pdev->dev, "wait rx done timeout!\n");
			return -ETIMEDOUT;
		}

		buf[0] = mipi_dsi_rd_rx_fifo(mipi_dsi);
		buf[0] = buf[0] >> 8;
	}
	else {
		/* TODO: add support later */
	}

	return 0;
}

int mipi_dsi_dcs_cmd(struct mipi_dsi_info *mipi_dsi,
				u8 cmd, const u32 *param, int num)
{
	int err = 0;
	u32 buf[DSI_CMD_BUF_MAXSIZE];

	switch (cmd) {
	case MIPI_DCS_EXIT_SLEEP_MODE:
	case MIPI_DCS_ENTER_SLEEP_MODE:
	case MIPI_DCS_SET_DISPLAY_ON:
	case MIPI_DCS_SET_DISPLAY_OFF:
		buf[0] = cmd;
		err = mipi_dsi_pkt_write(mipi_dsi,
				MIPI_DSI_DCS_SHORT_WRITE, buf, 0);
		break;

	default:
		dev_err(&mipi_dsi->pdev->dev,
			"MIPI DSI DCS Command:0x%x Not supported!\n", cmd);
		break;
	}

	return err;
}

static void mipi_dsi_set_main_standby(struct mipi_dsi_info *mipi_dsi,
                               unsigned int enable)
{
        unsigned int reg;

        reg = readl(mipi_dsi->mmio_base + MIPI_DSI_MDRESOL);

        reg &= ~MIPI_DSI_MAIN_STANDBY(1);

        if (enable)
                reg |= MIPI_DSI_MAIN_STANDBY(1);

        writel(reg, mipi_dsi->mmio_base + MIPI_DSI_MDRESOL);
}

static void mipi_dsi_power_off(struct mxc_dispdrv_handle *disp)
{
	int err;
	struct mipi_dsi_info *mipi_dsi = mxc_dispdrv_getdata(disp);

	err = mipi_dsi_dcs_cmd(mipi_dsi, MIPI_DCS_ENTER_SLEEP_MODE,
			NULL, 0);
	if (err) {
		dev_err(&mipi_dsi->pdev->dev,
			"MIPI DSI DCS Command display on error!\n");
	}
	msleep(MIPI_LCD_SLEEP_MODE_DELAY);

	mipi_dsi_set_main_standby(mipi_dsi, 0);

	clk_disable_unprepare(mipi_dsi->dphy_clk);
	clk_disable_unprepare(mipi_dsi->cfg_clk);
}

static void mipi_dsi_dphy_power_on(struct platform_device *pdev)
{
	int ret;

	regulator_set_voltage(mipi_phy_reg, 1000000, 1000000);

	ret = regulator_enable(mipi_phy_reg);
	if (ret){
		dev_err(&pdev->dev, "failed to enable mipi phy regulatore\n");
		BUG_ON(1);
	}
}

static void mipi_dsi_dphy_power_down(void)
{
	regulator_disable(mipi_phy_reg);
}

static int mipi_dsi_lane_stop_state(struct mipi_dsi_info *mipi_dsi)
{
	unsigned int reg;

	reg = readl(mipi_dsi->mmio_base + MIPI_DSI_STATUS);

	if (((reg & MIPI_DSI_STOP_STATE_DAT(0x3)) == 0x3) &&
	    ((reg & MIPI_DSI_STOP_STATE_CLK(0x1)) ||
	     (reg & MIPI_DSI_TX_READY_HS_CLK(0x1))))
		return 1;

	return 0;
}

static void mipi_dsi_init_interrupt(struct mipi_dsi_info *mipi_dsi)
{
	unsigned int intsrc, intmsk;

	intsrc = (INTSRC_SFR_PL_FIFO_EMPTY | INTSRC_RX_DATA_DONE);
	writel(intsrc, mipi_dsi->mmio_base + MIPI_DSI_INTSRC);

	intmsk = ~(INTMSK_SFR_PL_FIFO_EMPTY | INTMSK_RX_DATA_DONE);
	writel(intmsk, mipi_dsi->mmio_base + MIPI_DSI_INTMSK);
}

static int mipi_dsi_master_init(struct mipi_dsi_info *mipi_dsi,
				bool init)
{
	unsigned int time_out = 100;
	unsigned int reg, byte_clk, esc_div;
	struct fb_videomode *mode = mipi_dsi->mode;
	struct device *dev = &mipi_dsi->pdev->dev;

	/* configure DPHY PLL clock */
	writel(MIPI_DSI_TX_REQUEST_HSCLK(0) |
	       MIPI_DSI_DPHY_SEL(0) |
	       MIPI_DSI_PLL_BYPASS(0) |
	       MIPI_DSI_BYTE_CLK_SRC(0),
	       mipi_dsi->mmio_base + MIPI_DSI_CLKCTRL);
	if (!strcmp(mipi_dsi->lcd_panel, "TRULY-WVGA-TFT3P5581E"))
		writel(MIPI_DSI_PLL_EN(1) | MIPI_DSI_PMS(0x3141),
		       mipi_dsi->mmio_base + MIPI_DSI_PLLCTRL);
	else
		writel(MIPI_DSI_PLL_EN(1) | MIPI_DSI_PMS(0x4190),
		       mipi_dsi->mmio_base + MIPI_DSI_PLLCTRL);

	/* set PLLTMR: stable time */
	writel(33024, mipi_dsi->mmio_base + MIPI_DSI_PLLTMR);
	udelay(300);

	/* configure byte clock */
	reg = readl(mipi_dsi->mmio_base + MIPI_DSI_CLKCTRL);
	reg |= MIPI_DSI_BYTE_CLK_EN(1);
	byte_clk = 1500000000 / 8;
	esc_div  = DIV_ROUND_UP(byte_clk, 20 * 1000000);
	reg |= (esc_div & 0xffff);
	/* enable escape clock for clock lane and data lane0 and lane1 */
	reg |= MIPI_DSI_LANE_ESC_CLK_EN(0x7);
	reg |= MIPI_DSI_ESC_CLK_EN(1);
	writel(reg, mipi_dsi->mmio_base + MIPI_DSI_CLKCTRL);

	/* set main display resolution */
	writel(MIPI_DSI_MAIN_HRESOL(mode->xres) |
	       MIPI_DSI_MAIN_VRESOL(mode->yres) |
	       MIPI_DSI_MAIN_STANDBY(0),
	       mipi_dsi->mmio_base + MIPI_DSI_MDRESOL);

	/* set config register */
	writel(MIPI_DSI_MFLUSH_VS(1) |
	       MIPI_DSI_SYNC_IN_FORM(0) |
	       MIPI_DSI_BURST_MODE(1) |
	       MIPI_DSI_VIDEO_MODE(1) |
	       MIPI_DSI_AUTO_MODE(0)  |
	       MIPI_DSI_HSE_DISABLE_MODE(0) |
	       MIPI_DSI_HFP_DISABLE_MODE(0) |
	       MIPI_DSI_HBP_DISABLE_MODE(0) |
	       MIPI_DSI_HSA_DISABLE_MODE(0) |
	       MIPI_DSI_MAIN_VC(0) |
	       MIPI_DSI_SUB_VC(1)  |
	       MIPI_DSI_MAIN_PIX_FORMAT(0x7) |
	       MIPI_DSI_SUB_PIX_FORMAT(0x7) |
	       MIPI_DSI_NUM_OF_DATALANE(0x1) |
	       MIPI_DSI_LANE_EN(0x7), /* enable data lane 0 and 1 */
	       mipi_dsi->mmio_base + MIPI_DSI_CONFIG);

	/* set main display vporch */
	writel(MIPI_DSI_CMDALLOW(0xf) |
	       MIPI_DSI_STABLE_VFP(mode->lower_margin) |
	       MIPI_DSI_MAIN_VBP(mode->upper_margin),
	       mipi_dsi->mmio_base + MIPI_DSI_MVPORCH);
	/* set main display hporch */
	writel(MIPI_DSI_MAIN_HFP(mode->right_margin) |
	       MIPI_DSI_MAIN_HBP(mode->left_margin),
	       mipi_dsi->mmio_base + MIPI_DSI_MHPORCH);
	/* set main display sync */
	writel(MIPI_DSI_MAIN_VSA(mode->vsync_len) |
	       MIPI_DSI_MAIN_HSA(mode->hsync_len),
	       mipi_dsi->mmio_base + MIPI_DSI_MSYNC);

	/* configure d-phy timings */
	if (!strcmp(mipi_dsi->lcd_panel, "TRULY-WVGA-TFT3P5581E")) {
		writel(MIPI_DSI_M_TLPXCTL(2) | MIPI_DSI_M_THSEXITCTL(4),
			mipi_dsi->mmio_base + MIPI_DSI_PHYTIMING);
		writel(MIPI_DSI_M_TCLKPRPRCTL(5) |
			MIPI_DSI_M_TCLKZEROCTL(14) |
			MIPI_DSI_M_TCLKPOSTCTL(8) |
			MIPI_DSI_M_TCLKTRAILCTL(3),
			mipi_dsi->mmio_base + MIPI_DSI_PHYTIMING1);
		writel(MIPI_DSI_M_THSPRPRCTL(3) |
			MIPI_DSI_M_THSZEROCTL(3) |
			MIPI_DSI_M_THSTRAILCTL(3),
			mipi_dsi->mmio_base + MIPI_DSI_PHYTIMING2);
	} else {
		writel(MIPI_DSI_M_TLPXCTL(11) | MIPI_DSI_M_THSEXITCTL(18),
			mipi_dsi->mmio_base + MIPI_DSI_PHYTIMING);
		writel(MIPI_DSI_M_TCLKPRPRCTL(13) |
			MIPI_DSI_M_TCLKZEROCTL(65) |
			MIPI_DSI_M_TCLKPOSTCTL(17) |
			MIPI_DSI_M_TCLKTRAILCTL(13),
			mipi_dsi->mmio_base + MIPI_DSI_PHYTIMING1);
		writel(MIPI_DSI_M_THSPRPRCTL(16) |
			MIPI_DSI_M_THSZEROCTL(24) |
			MIPI_DSI_M_THSTRAILCTL(16),
			mipi_dsi->mmio_base + MIPI_DSI_PHYTIMING2);
	}

	writel(0xf000f, mipi_dsi->mmio_base + MIPI_DSI_TIMEOUT);

	/* Init FIFO */
	writel(0x0, mipi_dsi->mmio_base + MIPI_DSI_FIFOCTRL);
	udelay(300);
	writel(0x1f, mipi_dsi->mmio_base + MIPI_DSI_FIFOCTRL);

	/* check clock and data lanes are in stop state
	 * which means dphy is in low power mode
	 */
	while (!mipi_dsi_lane_stop_state(mipi_dsi)) {
		time_out--;
		if (time_out == 0) {
			dev_err(dev, "MIPI DSI is not stop state.\n");
			return -EINVAL;
		}
	}

	/* transfer commands always in lp mode */
	writel(MIPI_DSI_CMD_LPDT, mipi_dsi->mmio_base + MIPI_DSI_ESCMODE);

	mipi_dsi_init_interrupt(mipi_dsi);

	return 0;
}

static int mipi_dsi_disp_init(struct mxc_dispdrv_handle *disp,
	struct mxc_dispdrv_setting *setting)
{
	struct mipi_dsi_info *mipi_dsi = mxc_dispdrv_getdata(disp);
	struct device *dev = &mipi_dsi->pdev->dev;
	int ret = 0;

	ret = mipi_dsi_lcd_init(mipi_dsi, setting);
	if (ret) {
		dev_err(dev, "failed to init mipi dsi lcd\n");
		return ret;
	}

	dev_info(dev, "MIPI DSI dispdrv inited!\n");

	return ret;
}

static void mipi_dsi_disp_deinit(struct mxc_dispdrv_handle *disp)
{
	struct mipi_dsi_info *mipi_dsi;

	mipi_dsi = mxc_dispdrv_getdata(disp);

	mipi_dsi_power_off(mipi_dsi->disp_mipi);
	if (mipi_dsi->bl)
		backlight_device_unregister(mipi_dsi->bl);
}

static void mipi_dsi_set_mode(struct mipi_dsi_info *mipi_dsi,
			      enum mipi_dsi_trans_mode mode)
{
	unsigned int dsi_clkctrl;

	dsi_clkctrl = readl(mipi_dsi->mmio_base + MIPI_DSI_CLKCTRL);

	switch (mode) {
	case DSI_LP_MODE:
		dsi_clkctrl &= ~MIPI_DSI_TX_REQUEST_HSCLK(1);
		break;
	case DSI_HS_MODE:
		dsi_clkctrl |= MIPI_DSI_TX_REQUEST_HSCLK(1);
		break;
	default:
		dev_err(&mipi_dsi->pdev->dev,
			"invalid dsi mode\n");
		return;
	}

	writel(dsi_clkctrl, mipi_dsi->mmio_base + MIPI_DSI_CLKCTRL);
	mdelay(1);
}

static int mipi_dsi_enable(struct mxc_dispdrv_handle *disp,
			   struct fb_info *fbi)
{
	int ret;
	struct mipi_dsi_info *mipi_dsi = mxc_dispdrv_getdata(disp);

	if (fbi->state == FBINFO_STATE_SUSPENDED) {
		if (mipi_dsi->disp_power_on) {
			ret = regulator_enable(mipi_dsi->disp_power_on);
			if (ret) {
				dev_err(&mipi_dsi->pdev->dev, "failed to enable display "
						"power regulator, err = %d\n", ret);
				return ret;
			}
		}
	}

	if (!mipi_dsi->dsi_power_on)
		pm_runtime_get_sync(&mipi_dsi->pdev->dev);

	ret = clk_prepare_enable(mipi_dsi->dphy_clk);
	ret |= clk_prepare_enable(mipi_dsi->cfg_clk);
	if (ret) {
		dev_err(&mipi_dsi->pdev->dev,
				"clk enable error:%d!\n", ret);
		return -EINVAL;
	}

	if (!mipi_dsi->lcd_inited) {
		ret = mipi_dsi_master_init(mipi_dsi, true);
		if (ret)
			return -EINVAL;

		msleep(20);
		ret = device_reset(&mipi_dsi->pdev->dev);
		if (ret) {
			dev_err(&mipi_dsi->pdev->dev, "failed to reset device: %d\n", ret);
			return -EINVAL;
		}
		msleep(120);

		/* the panel should be config under LP mode */
		ret = mipi_dsi->lcd_callback->mipi_lcd_setup(mipi_dsi);
		if (ret < 0) {
			dev_err(&mipi_dsi->pdev->dev,
					"failed to init mipi lcd.\n");
			return ret ;
		}
		mipi_dsi->lcd_inited = 1;

		/* change to HS mode for panel display */
		mipi_dsi_set_mode(mipi_dsi, DSI_HS_MODE);
	} else {
		ret = mipi_dsi_dcs_cmd(mipi_dsi, MIPI_DCS_EXIT_SLEEP_MODE,
			NULL, 0);
		if (ret) {
			dev_err(&mipi_dsi->pdev->dev,
				"MIPI DSI DCS Command sleep-in error!\n");
		}
		msleep(MIPI_LCD_SLEEP_MODE_DELAY);
	}

	mipi_dsi_set_main_standby(mipi_dsi, 1);

	return 0;
}

static void mipi_dsi_disable(struct mxc_dispdrv_handle *disp,
			     struct fb_info *fbi)
{
	struct mipi_dsi_info *mipi_dsi = mxc_dispdrv_getdata(disp);

	mipi_dsi_power_off(mipi_dsi->disp_mipi);

	if (fbi->state == FBINFO_STATE_SUSPENDED) {
		if (mipi_dsi->dsi_power_on) {
			pm_runtime_put_noidle(&mipi_dsi->pdev->dev);
			pm_runtime_put_sync_suspend(&mipi_dsi->pdev->dev);
			pm_runtime_get_noresume(&mipi_dsi->pdev->dev);
		}

		if (mipi_dsi->disp_power_on)
			regulator_disable(mipi_dsi->disp_power_on);

		mipi_dsi->lcd_inited = 0;
	}
}

static int mipi_dsi_setup(struct mxc_dispdrv_handle *disp,
			  struct fb_info *fbi)
{
	struct mipi_dsi_info *mipi_dsi = mxc_dispdrv_getdata(disp);
	int xres_virtual = fbi->var.xres_virtual;
	int yres_virtual = fbi->var.yres_virtual;
	int xoffset = fbi->var.xoffset;
	int yoffset = fbi->var.yoffset;
	int pixclock = fbi->var.pixclock;

	if (!mipi_dsi->mode)
		return 0;

	/* set the mode back to var in case userspace changes it */
	fb_videomode_to_var(&fbi->var, mipi_dsi->mode);

	/* restore some var entries cached */
	fbi->var.xres_virtual = xres_virtual;
	fbi->var.yres_virtual = yres_virtual;
	fbi->var.xoffset = xoffset;
	fbi->var.yoffset = yoffset;
	fbi->var.pixclock = pixclock;

	return 0;
}

static struct mxc_dispdrv_driver mipi_dsi_drv = {
	.name = DISPDRV_MIPI,
	.init = mipi_dsi_disp_init,
	.deinit = mipi_dsi_disp_deinit,
	.enable = mipi_dsi_enable,
	.disable = mipi_dsi_disable,
	.setup = mipi_dsi_setup,
};

static const struct of_device_id imx_mipi_dsi_dt_ids[] = {
	{ .compatible = "fsl,imx7d-mipi-dsi", .data = NULL, },
	{ }
};
MODULE_DEVICE_TABLE(of, imx_mipi_dsi_dt_ids);

static irqreturn_t mipi_dsi_irq_handler(int irq, void *data)
{
	unsigned int intsrc, intclr;
	struct mipi_dsi_info *mipi_dsi = data;
	struct platform_device *pdev = mipi_dsi->pdev;

	intclr = 0;
	intsrc = readl(mipi_dsi->mmio_base + MIPI_DSI_INTSRC);

	dev_dbg(&pdev->dev, "intsrc = 0x%x\n", intsrc);

	if (intsrc & INTSRC_SFR_PL_FIFO_EMPTY) {
		dev_dbg(&pdev->dev, "playload tx finished\n");
		intclr |= INTSRC_SFR_PL_FIFO_EMPTY;
		complete(&dsi_tx_done);
	}

	if(intsrc & INTSRC_RX_DATA_DONE) {
		dev_dbg(&pdev->dev, "rx data finished\n");
		intclr |= INTSRC_RX_DATA_DONE;
		complete(&dsi_rx_done);
	}

	/* clear the interrupts */
	if (intclr)
		writel(intclr, mipi_dsi->mmio_base + MIPI_DSI_INTSRC);

	return IRQ_HANDLED;
}

/**
 * This function is called by the driver framework to initialize the MIPI DSI
 * device.
 *
 * @param	pdev	The device structure for the MIPI DSI passed in by the
 *			driver framework.
 *
 * @return      Returns 0 on success or negative error code on error
 */
static int mipi_dsi_probe(struct platform_device *pdev)
{
	struct device_node *np = pdev->dev.of_node;
	struct mipi_dsi_info *mipi_dsi;
	struct resource *res;
	const char *lcd_panel;
	int ret = 0;

	mipi_dsi = devm_kzalloc(&pdev->dev, sizeof(*mipi_dsi), GFP_KERNEL);
	if (!mipi_dsi)
		return -ENOMEM;
	mipi_dsi->pdev = pdev;

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!res) {
		dev_err(&pdev->dev, "failed to get platform resource mem\n");
		return -ENODEV;
	}

	if (!devm_request_mem_region(&pdev->dev, res->start,
				resource_size(res), pdev->name))
		return -EBUSY;

	mipi_dsi->mmio_base = devm_ioremap(&pdev->dev, res->start,
					resource_size(res));
	if (!mipi_dsi->mmio_base)
		return -ENOMEM;

	mipi_dsi->irq = platform_get_irq(pdev, 0);
	if (mipi_dsi->irq < 0) {
		dev_err(&pdev->dev, "failed to get device irq\n");
		return -EINVAL;
	}

	ret = devm_request_irq(&pdev->dev, mipi_dsi->irq,
				mipi_dsi_irq_handler,
				0, "mipi_dsi_samsung", mipi_dsi);
	if (ret) {
		dev_err(&pdev->dev, "failed to request mipi dsi irq\n");
		return ret;
	}

	mipi_dsi->dphy_clk = devm_clk_get(&pdev->dev, "mipi_pllref_clk");
	if (IS_ERR(mipi_dsi->dphy_clk)) {
		dev_err(&pdev->dev, "failed to get dphy pll_ref_clk\n");
		return PTR_ERR(mipi_dsi->dphy_clk);
	}

	mipi_dsi->cfg_clk = devm_clk_get(&pdev->dev, "mipi_cfg_clk");
	if (IS_ERR(mipi_dsi->cfg_clk)) {
		dev_err(&pdev->dev, "failed to get cfg_clk\n");
		return PTR_ERR(mipi_dsi->cfg_clk);
	}

	ret = of_property_read_string(np, "lcd_panel", &lcd_panel);
	if (ret) {
		dev_err(&pdev->dev, "failed to read lcd_panel property\n");
		return ret;
	}

	mipi_phy_reg = devm_regulator_get(&pdev->dev, "mipi-phy");
	if (IS_ERR(mipi_phy_reg)) {
		dev_err(&pdev->dev, "mipi phy power supply not found\n");
		return ret;
	}

	mipi_dsi->disp_power_on = devm_regulator_get(&pdev->dev,
						"disp-power-on");
	if (!IS_ERR(mipi_dsi->disp_power_on)) {
		ret = regulator_enable(mipi_dsi->disp_power_on);
		if (ret) {
			dev_err(&pdev->dev, "failed to enable display "
				"power regulator, err = %d\n", ret);
			return ret;
		}
	}

	mipi_dsi->lcd_panel = kstrdup(lcd_panel, GFP_KERNEL);
	if (!mipi_dsi->lcd_panel) {
		dev_err(&pdev->dev, "failed to allocate lcd panel name\n");
		ret = -ENOMEM;
		goto kstrdup_fail;
	}

	mipi_dsi->disp_mipi = mxc_dispdrv_register(&mipi_dsi_drv);
	if (IS_ERR(mipi_dsi->disp_mipi)) {
		dev_err(&pdev->dev, "mxc_dispdrv_register error\n");
		ret = PTR_ERR(mipi_dsi->disp_mipi);
		goto dispdrv_reg_fail;
	}

        mipi_dsi->mipi_dsi_pkt_read  = mipi_dsi_pkt_read;
        mipi_dsi->mipi_dsi_pkt_write = mipi_dsi_pkt_write;
        mipi_dsi->mipi_dsi_dcs_cmd   = mipi_dsi_dcs_cmd;

	pm_runtime_enable(&pdev->dev);

	mxc_dispdrv_setdata(mipi_dsi->disp_mipi, mipi_dsi);
	dev_set_drvdata(&pdev->dev, mipi_dsi);

	dev_info(&pdev->dev, "i.MX MIPI DSI driver probed\n");
	return ret;

dispdrv_reg_fail:
	kfree(mipi_dsi->lcd_panel);
kstrdup_fail:
	if (mipi_dsi->disp_power_on)
		regulator_disable(mipi_dsi->disp_power_on);

	return ret;
}

static void mipi_dsi_shutdown(struct platform_device *pdev)
{
	struct mipi_dsi_info *mipi_dsi = dev_get_drvdata(&pdev->dev);

	mipi_dsi_power_off(mipi_dsi->disp_mipi);
	mipi_dsi_dphy_power_down();
}

static int mipi_dsi_remove(struct platform_device *pdev)
{
	struct mipi_dsi_info *mipi_dsi = dev_get_drvdata(&pdev->dev);

	mxc_dispdrv_puthandle(mipi_dsi->disp_mipi);
	mxc_dispdrv_unregister(mipi_dsi->disp_mipi);

	if (mipi_dsi->disp_power_on)
		regulator_disable(mipi_dsi->disp_power_on);

	kfree(mipi_dsi->lcd_panel);
	dev_set_drvdata(&pdev->dev, NULL);

	return 0;
}

static int mipi_dsi_runtime_suspend(struct device *dev)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct mipi_dsi_info *mipi_dsi = dev_get_drvdata(&pdev->dev);

	if (mipi_dsi->dsi_power_on) {
		release_bus_freq(BUS_FREQ_HIGH);
		dev_dbg(dev, "mipi dsi busfreq high release.\n");

		mipi_dsi_dphy_power_down();
		mipi_dsi->dsi_power_on = 0;
	}

	return 0;
}

static int mipi_dsi_runtime_resume(struct device *dev)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct mipi_dsi_info *mipi_dsi = dev_get_drvdata(&pdev->dev);

	if (!mipi_dsi->dsi_power_on) {
		request_bus_freq(BUS_FREQ_HIGH);
		dev_dbg(dev, "mipi dsi busfreq high request.\n");

		mipi_dsi_dphy_power_on(pdev);
		mipi_dsi->dsi_power_on = 1;
	}

	return 0;
}

static const struct dev_pm_ops mipi_dsi_pm_ops = {
	.runtime_suspend = mipi_dsi_runtime_suspend,
	.runtime_resume  = mipi_dsi_runtime_resume,
	.runtime_idle	 = NULL,
};

static struct platform_driver mipi_dsi_driver = {
	.driver = {
		   .of_match_table = imx_mipi_dsi_dt_ids,
		   .name = "mxc_mipi_dsi_samsung",
		   .pm = &mipi_dsi_pm_ops,
	},
	.probe  = mipi_dsi_probe,
	.remove = mipi_dsi_remove,
	.shutdown = mipi_dsi_shutdown,
};

static int __init mipi_dsi_init(void)
{
	int err;

	err = platform_driver_register(&mipi_dsi_driver);
	if (err) {
		pr_err("mipi_dsi_driver register failed\n");
		return err;
	}

	pr_info("MIPI DSI driver module loaded\n");

	return 0;
}

static void __exit mipi_dsi_cleanup(void)
{
	platform_driver_unregister(&mipi_dsi_driver);
}

module_init(mipi_dsi_init);
module_exit(mipi_dsi_cleanup);

MODULE_AUTHOR("Freescale Semiconductor, Inc.");
MODULE_DESCRIPTION("i.MX MIPI DSI driver");
MODULE_LICENSE("GPL");
