/*
 * hdmi.c
 *
 * HDMI interface DSS driver setting for TI's OMAP4 family of processor.
 * Copyright (C) 2010-2011 Texas Instruments Incorporated - http://www.ti.com/
 * Authors: Yong Zhi
 *	Mythri pk <mythripk@ti.com>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 as published by
 * the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#define DSS_SUBSYS_NAME "HDMI"

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/err.h>
#include <linux/io.h>
#include <linux/interrupt.h>
#include <linux/mutex.h>
#include <linux/delay.h>
#include <linux/string.h>
#include <linux/platform_device.h>
#include <linux/pm_runtime.h>
#include <linux/clk.h>
#include <video/omapdss.h>
#if defined(CONFIG_SND_OMAP_SOC_OMAP4_HDMI) || \
	defined(CONFIG_SND_OMAP_SOC_OMAP4_HDMI_MODULE)
#include <sound/soc.h>
#include <sound/pcm_params.h>
#endif

#include "dss.h"
#include "hdmi.h"
#include "dss_features.h"

static struct {
	struct mutex lock;
	struct omap_display_platform_data *pdata;
	struct platform_device *pdev;
	void __iomem *base_wp;	/* HDMI wrapper */
	int code;
	int mode;
	u8 edid[HDMI_EDID_MAX_LENGTH];
	u8 edid_set;
	bool custom_set;
	struct hdmi_config cfg;

	struct clk *sys_clk;
	struct clk *hdmi_clk;
} hdmi;

/*
 * Logic for the below structure :
 * user enters the CEA or VESA timings by specifying the HDMI/DVI code.
 * There is a correspondence between CEA/VESA timing and code, please
 * refer to section 6.3 in HDMI 1.3 specification for timing code.
 *
 * In the below structure, cea_vesa_timings corresponds to all OMAP4
 * supported CEA and VESA timing values.code_cea corresponds to the CEA
 * code, It is used to get the timing from cea_vesa_timing array.Similarly
 * with code_vesa. Code_index is used for back mapping, that is once EDID
 * is read from the TV, EDID is parsed to find the timing values and then
 * map it to corresponding CEA or VESA index.
 */

static const struct hdmi_timings cea_vesa_timings[OMAP_HDMI_TIMINGS_NB] = {
	{ {640, 480, 25200, 96, 16, 48, 2, 10, 33} , 0 , 0},
	{ {1280, 720, 74250, 40, 440, 220, 5, 5, 20}, 1, 1},
	{ {1280, 720, 74250, 40, 110, 220, 5, 5, 20}, 1, 1},
	{ {720, 480, 27027, 62, 16, 60, 6, 9, 30}, 0, 0},
	{ {2880, 576, 108000, 256, 48, 272, 5, 5, 39}, 0, 0},
	{ {1440, 240, 27027, 124, 38, 114, 3, 4, 15}, 0, 0},
	{ {1440, 288, 27000, 126, 24, 138, 3, 2, 19}, 0, 0},
	{ {1920, 540, 74250, 44, 528, 148, 5, 2, 15}, 1, 1},
	{ {1920, 540, 74250, 44, 88, 148, 5, 2, 15}, 1, 1},
	{ {1920, 1080, 148500, 44, 88, 148, 5, 4, 36}, 1, 1},
	{ {720, 576, 27000, 64, 12, 68, 5, 5, 39}, 0, 0},
	{ {1440, 576, 54000, 128, 24, 136, 5, 5, 39}, 0, 0},
	{ {1920, 1080, 148500, 44, 528, 148, 5, 4, 36}, 1, 1},
	{ {2880, 480, 108108, 248, 64, 240, 6, 9, 30}, 0, 0},
	{ {1920, 1080, 74250, 44, 638, 148, 5, 4, 36}, 1, 1},
	/* VESA From Here */
	{ {640, 480, 25175, 96, 16, 48, 2 , 11, 31}, 0, 0},
	{ {800, 600, 40000, 128, 40, 88, 4 , 1, 23}, 1, 1},
	{ {848, 480, 33750, 112, 16, 112, 8 , 6, 23}, 1, 1},
	{ {1280, 768, 79500, 128, 64, 192, 7 , 3, 20}, 1, 0},
	{ {1280, 800, 83500, 128, 72, 200, 6 , 3, 22}, 1, 0},
	{ {1360, 768, 85500, 112, 64, 256, 6 , 3, 18}, 1, 1},
	{ {1280, 960, 108000, 112, 96, 312, 3 , 1, 36}, 1, 1},
	{ {1280, 1024, 108000, 112, 48, 248, 3 , 1, 38}, 1, 1},
	{ {1024, 768, 65000, 136, 24, 160, 6, 3, 29}, 0, 0},
	{ {1400, 1050, 121750, 144, 88, 232, 4, 3, 32}, 1, 0},
	{ {1440, 900, 106500, 152, 80, 232, 6, 3, 25}, 1, 0},
	{ {1680, 1050, 146250, 176 , 104, 280, 6, 3, 30}, 1, 0},
	{ {1366, 768, 85500, 143, 70, 213, 3, 3, 24}, 1, 1},
	{ {1920, 1080, 148500, 44, 148, 80, 5, 4, 36}, 1, 1},
	{ {1280, 768, 68250, 32, 48, 80, 7, 3, 12}, 0, 1},
	{ {1400, 1050, 101000, 32, 48, 80, 4, 3, 23}, 0, 1},
	{ {1680, 1050, 119000, 32, 48, 80, 6, 3, 21}, 0, 1},
	{ {1280, 800, 79500, 32, 48, 80, 6, 3, 14}, 0, 1},
	{ {1280, 720, 74250, 40, 110, 220, 5, 5, 20}, 1, 1}
};

/*
 * This is a static mapping array which maps the timing values
 * with corresponding CEA / VESA code
 */
static const int code_index[OMAP_HDMI_TIMINGS_NB] = {
	1, 19, 4, 2, 37, 6, 21, 20, 5, 16, 17, 29, 31, 35, 32,
	/* <--15 CEA 17--> vesa*/
	4, 9, 0xE, 0x17, 0x1C, 0x27, 0x20, 0x23, 0x10, 0x2A,
	0X2F, 0x3A, 0X51, 0X52, 0x16, 0x29, 0x39, 0x1B
};

/*
 * This is reverse static mapping which maps the CEA / VESA code
 * to the corresponding timing values
 */
static const int code_cea[39] = {
	-1,  0,  3,  3,  2,  8,  5,  5, -1, -1,
	-1, -1, -1, -1, -1, -1,  9, 10, 10,  1,
	7,   6,  6, -1, -1, -1, -1, -1, -1, 11,
	11, 12, 14, -1, -1, 13, 13,  4,  4
};

static const int code_vesa[85] = {
	-1, -1, -1, -1, 15, -1, -1, -1, -1, 16,
	-1, -1, -1, -1, 17, -1, 23, -1, -1, -1,
	-1, -1, 29, 18, -1, -1, -1, 32, 19, -1,
	-1, -1, 21, -1, -1, 22, -1, -1, -1, 20,
	-1, 30, 24, -1, -1, -1, -1, 25, -1, -1,
	-1, -1, -1, -1, -1, -1, -1, 31, 26, -1,
	-1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
	-1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
	-1, 27, 28, -1, 33};

static const u8 edid_header[8] = {0x0, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0x0};

static inline void hdmi_write_reg(const struct hdmi_reg idx, u32 val)
{
	__raw_writel(val, hdmi.base_wp + idx.idx);
}

static inline u32 hdmi_read_reg(const struct hdmi_reg idx)
{
	return __raw_readl(hdmi.base_wp + idx.idx);
}

static inline int hdmi_wait_for_bit_change(const struct hdmi_reg idx,
				int b2, int b1, u32 val)
{
	u32 t = 0;
	while (val != REG_GET(idx, b2, b1)) {
		udelay(1);
		if (t++ > 10000)
			return !val;
	}
	return val;
}

static int hdmi_runtime_get(void)
{
	int r;

	DSSDBG("hdmi_runtime_get\n");

	r = pm_runtime_get_sync(&hdmi.pdev->dev);
	WARN_ON(r < 0);
	return r < 0 ? r : 0;
}

static void hdmi_runtime_put(void)
{
	int r;

	DSSDBG("hdmi_runtime_put\n");

	r = pm_runtime_put(&hdmi.pdev->dev);
	WARN_ON(r < 0);
}

int hdmi_init_display(struct omap_dss_device *dssdev)
{
	DSSDBG("init_display\n");

	return 0;
}

static int hdmi_pll_init(enum hdmi_clk_refsel refsel, int dcofreq,
		struct hdmi_pll_info *fmt, u16 sd)
{
	u32 r;

	/* PLL start always use manual mode */
	REG_FLD_MOD(PLLCTRL_PLL_CONTROL, 0x0, 0, 0);

	r = hdmi_read_reg(PLLCTRL_CFG1);
	r = FLD_MOD(r, fmt->regm, 20, 9); /* CFG1_PLL_REGM */
	r = FLD_MOD(r, fmt->regn, 8, 1);  /* CFG1_PLL_REGN */

	hdmi_write_reg(PLLCTRL_CFG1, r);

	r = hdmi_read_reg(PLLCTRL_CFG2);

	r = FLD_MOD(r, 0x0, 12, 12); /* PLL_HIGHFREQ divide by 2 */
	r = FLD_MOD(r, 0x1, 13, 13); /* PLL_REFEN */
	r = FLD_MOD(r, 0x0, 14, 14); /* PHY_CLKINEN de-assert during locking */

	if (dcofreq) {
		/* divider programming for frequency beyond 1000Mhz */
		REG_FLD_MOD(PLLCTRL_CFG3, sd, 17, 10);
		r = FLD_MOD(r, 0x4, 3, 1); /* 1000MHz and 2000MHz */
	} else {
		r = FLD_MOD(r, 0x2, 3, 1); /* 500MHz and 1000MHz */
	}

	hdmi_write_reg(PLLCTRL_CFG2, r);

	r = hdmi_read_reg(PLLCTRL_CFG4);
	r = FLD_MOD(r, fmt->regm2, 24, 18);
	r = FLD_MOD(r, fmt->regmf, 17, 0);

	hdmi_write_reg(PLLCTRL_CFG4, r);

	/* go now */
	REG_FLD_MOD(PLLCTRL_PLL_GO, 0x1, 0, 0);

	/* wait for bit change */
	if (hdmi_wait_for_bit_change(PLLCTRL_PLL_GO, 0, 0, 1) != 1) {
		DSSERR("PLL GO bit not set\n");
		return -ETIMEDOUT;
	}

	/* Wait till the lock bit is set in PLL status */
	if (hdmi_wait_for_bit_change(PLLCTRL_PLL_STATUS, 1, 1, 1) != 1) {
		DSSWARN("cannot lock PLL\n");
		DSSWARN("CFG1 0x%x\n",
			hdmi_read_reg(PLLCTRL_CFG1));
		DSSWARN("CFG2 0x%x\n",
			hdmi_read_reg(PLLCTRL_CFG2));
		DSSWARN("CFG4 0x%x\n",
			hdmi_read_reg(PLLCTRL_CFG4));
		return -ETIMEDOUT;
	}

	DSSDBG("PLL locked!\n");

	return 0;
}

/* PHY_PWR_CMD */
static int hdmi_set_phy_pwr(enum hdmi_phy_pwr val)
{
	/* Command for power control of HDMI PHY */
	REG_FLD_MOD(HDMI_WP_PWR_CTRL, val, 7, 6);

	/* Status of the power control of HDMI PHY */
	if (hdmi_wait_for_bit_change(HDMI_WP_PWR_CTRL, 5, 4, val) != val) {
		DSSERR("Failed to set PHY power mode to %d\n", val);
		return -ETIMEDOUT;
	}

	return 0;
}

/* PLL_PWR_CMD */
static int hdmi_set_pll_pwr(enum hdmi_pll_pwr val)
{
	/* Command for power control of HDMI PLL */
	REG_FLD_MOD(HDMI_WP_PWR_CTRL, val, 3, 2);

	/* wait till PHY_PWR_STATUS is set */
	if (hdmi_wait_for_bit_change(HDMI_WP_PWR_CTRL, 1, 0, val) != val) {
		DSSERR("Failed to set PHY_PWR_STATUS\n");
		return -ETIMEDOUT;
	}

	return 0;
}

static int hdmi_pll_reset(void)
{
	/* SYSRESET  controlled by power FSM */
	REG_FLD_MOD(PLLCTRL_PLL_CONTROL, 0x0, 3, 3);

	/* READ 0x0 reset is in progress */
	if (hdmi_wait_for_bit_change(PLLCTRL_PLL_STATUS, 0, 0, 1) != 1) {
		DSSERR("Failed to sysreset PLL\n");
		return -ETIMEDOUT;
	}

	return 0;
}

static int hdmi_phy_init(void)
{
	u16 r = 0;

	r = hdmi_set_phy_pwr(HDMI_PHYPWRCMD_LDOON);
	if (r)
		return r;

	r = hdmi_set_phy_pwr(HDMI_PHYPWRCMD_TXON);
	if (r)
		return r;

	/*
	 * Read address 0 in order to get the SCP reset done completed
	 * Dummy access performed to make sure reset is done
	 */
	hdmi_read_reg(HDMI_TXPHY_TX_CTRL);

	/*
	 * Write to phy address 0 to configure the clock
	 * use HFBITCLK write HDMI_TXPHY_TX_CONTROL_FREQOUT field
	 */
	REG_FLD_MOD(HDMI_TXPHY_TX_CTRL, 0x1, 31, 30);

	/* Write to phy address 1 to start HDMI line (TXVALID and TMDSCLKEN) */
	hdmi_write_reg(HDMI_TXPHY_DIGITAL_CTRL, 0xF0000000);

	/* Setup max LDO voltage */
	REG_FLD_MOD(HDMI_TXPHY_POWER_CTRL, 0xB, 3, 0);

	/* Write to phy address 3 to change the polarity control */
	REG_FLD_MOD(HDMI_TXPHY_PAD_CFG_CTRL, 0x1, 27, 27);

	return 0;
}

static int hdmi_pll_program(struct hdmi_pll_info *fmt)
{
	u16 r = 0;
	enum hdmi_clk_refsel refsel;

	r = hdmi_set_pll_pwr(HDMI_PLLPWRCMD_ALLOFF);
	if (r)
		return r;

	r = hdmi_set_pll_pwr(HDMI_PLLPWRCMD_BOTHON_ALLCLKS);
	if (r)
		return r;

	r = hdmi_pll_reset();
	if (r)
		return r;

	refsel = HDMI_REFSEL_SYSCLK;

	r = hdmi_pll_init(refsel, fmt->dcofreq, fmt, fmt->regsd);
	if (r)
		return r;

	return 0;
}

static void hdmi_phy_off(void)
{
	hdmi_set_phy_pwr(HDMI_PHYPWRCMD_OFF);
}

static int hdmi_core_ddc_edid(u8 *pedid, int ext)
{
	u32 i, j;
	char checksum = 0;
	u32 offset = 0;

	/* Turn on CLK for DDC */
	REG_FLD_MOD(HDMI_CORE_AV_DPD, 0x7, 2, 0);

	/*
	 * SW HACK : Without the Delay DDC(i2c bus) reads 0 values /
	 * right shifted values( The behavior is not consistent and seen only
	 * with some TV's)
	 */
	usleep_range(800, 1000);

	if (!ext) {
		/* Clk SCL Devices */
		REG_FLD_MOD(HDMI_CORE_DDC_CMD, 0xA, 3, 0);

		/* HDMI_CORE_DDC_STATUS_IN_PROG */
		if (hdmi_wait_for_bit_change(HDMI_CORE_DDC_STATUS,
						4, 4, 0) != 0) {
			DSSERR("Failed to program DDC\n");
			return -ETIMEDOUT;
		}

		/* Clear FIFO */
		REG_FLD_MOD(HDMI_CORE_DDC_CMD, 0x9, 3, 0);

		/* HDMI_CORE_DDC_STATUS_IN_PROG */
		if (hdmi_wait_for_bit_change(HDMI_CORE_DDC_STATUS,
						4, 4, 0) != 0) {
			DSSERR("Failed to program DDC\n");
			return -ETIMEDOUT;
		}

	} else {
		if (ext % 2 != 0)
			offset = 0x80;
	}

	/* Load Segment Address Register */
	REG_FLD_MOD(HDMI_CORE_DDC_SEGM, ext/2, 7, 0);

	/* Load Slave Address Register */
	REG_FLD_MOD(HDMI_CORE_DDC_ADDR, 0xA0 >> 1, 7, 1);

	/* Load Offset Address Register */
	REG_FLD_MOD(HDMI_CORE_DDC_OFFSET, offset, 7, 0);

	/* Load Byte Count */
	REG_FLD_MOD(HDMI_CORE_DDC_COUNT1, 0x80, 7, 0);
	REG_FLD_MOD(HDMI_CORE_DDC_COUNT2, 0x0, 1, 0);

	/* Set DDC_CMD */
	if (ext)
		REG_FLD_MOD(HDMI_CORE_DDC_CMD, 0x4, 3, 0);
	else
		REG_FLD_MOD(HDMI_CORE_DDC_CMD, 0x2, 3, 0);

	/* HDMI_CORE_DDC_STATUS_BUS_LOW */
	if (REG_GET(HDMI_CORE_DDC_STATUS, 6, 6) == 1) {
		DSSWARN("I2C Bus Low?\n");
		return -EIO;
	}
	/* HDMI_CORE_DDC_STATUS_NO_ACK */
	if (REG_GET(HDMI_CORE_DDC_STATUS, 5, 5) == 1) {
		DSSWARN("I2C No Ack\n");
		return -EIO;
	}

	i = ext * 128;
	j = 0;
	while (((REG_GET(HDMI_CORE_DDC_STATUS, 4, 4) == 1) ||
			(REG_GET(HDMI_CORE_DDC_STATUS, 2, 2) == 0)) &&
			j < 128) {

		if (REG_GET(HDMI_CORE_DDC_STATUS, 2, 2) == 0) {
			/* FIFO not empty */
			pedid[i++] = REG_GET(HDMI_CORE_DDC_DATA, 7, 0);
			j++;
		}
	}

	for (j = 0; j < 128; j++)
		checksum += pedid[j];

	if (checksum != 0) {
		DSSERR("E-EDID checksum failed!!\n");
		return -EIO;
	}

	return 0;
}

static int read_edid(u8 *pedid, u16 max_length)
{
	int r = 0, n = 0, i = 0;
	int max_ext_blocks = (max_length / 128) - 1;

	r = hdmi_core_ddc_edid(pedid, 0);
	if (r) {
		return r;
	} else {
		n = pedid[0x7e];

		/*
		 * README: need to comply with max_length set by the caller.
		 * Better implementation should be to allocate necessary
		 * memory to store EDID according to nb_block field found
		 * in first block
		 */
		if (n > max_ext_blocks)
			n = max_ext_blocks;

		for (i = 1; i <= n; i++) {
			r = hdmi_core_ddc_edid(pedid, i);
			if (r)
				return r;
		}
	}
	return 0;
}

static int get_timings_index(void)
{
	int code;

	if (hdmi.mode == 0)
		code = code_vesa[hdmi.code];
	else
		code = code_cea[hdmi.code];

	if (code == -1)	{
		/* HDMI code 4 corresponds to 640 * 480 VGA */
		hdmi.code = 4;
		/* DVI mode 1 corresponds to HDMI 0 to DVI */
		hdmi.mode = HDMI_DVI;

		code = code_vesa[hdmi.code];
	}
	return code;
}

static struct hdmi_cm hdmi_get_code(struct omap_video_timings *timing)
{
	int i = 0, code = -1, temp_vsync = 0, temp_hsync = 0;
	int timing_vsync = 0, timing_hsync = 0;
	struct omap_video_timings temp;
	struct hdmi_cm cm = {-1};
	DSSDBG("hdmi_get_code\n");

	for (i = 0; i < OMAP_HDMI_TIMINGS_NB; i++) {
		temp = cea_vesa_timings[i].timings;
		if ((temp.pixel_clock == timing->pixel_clock) &&
			(temp.x_res == timing->x_res) &&
			(temp.y_res == timing->y_res)) {

			temp_hsync = temp.hfp + temp.hsw + temp.hbp;
			timing_hsync = timing->hfp + timing->hsw + timing->hbp;
			temp_vsync = temp.vfp + temp.vsw + temp.vbp;
			timing_vsync = timing->vfp + timing->vsw + timing->vbp;

			DSSDBG("temp_hsync = %d , temp_vsync = %d"
				"timing_hsync = %d, timing_vsync = %d\n",
				temp_hsync, temp_hsync,
				timing_hsync, timing_vsync);

			if ((temp_hsync == timing_hsync) &&
					(temp_vsync == timing_vsync)) {
				code = i;
				cm.code = code_index[i];
				if (code < 14)
					cm.mode = HDMI_HDMI;
				else
					cm.mode = HDMI_DVI;
				DSSDBG("Hdmi_code = %d mode = %d\n",
					 cm.code, cm.mode);
				break;
			 }
		}
	}

	return cm;
}

static void get_horz_vert_timing_info(int current_descriptor_addrs, u8 *edid ,
		struct omap_video_timings *timings)
{
	/* X and Y resolution */
	timings->x_res = (((edid[current_descriptor_addrs + 4] & 0xF0) << 4) |
			 edid[current_descriptor_addrs + 2]);
	timings->y_res = (((edid[current_descriptor_addrs + 7] & 0xF0) << 4) |
			 edid[current_descriptor_addrs + 5]);

	timings->pixel_clock = ((edid[current_descriptor_addrs + 1] << 8) |
				edid[current_descriptor_addrs]);

	timings->pixel_clock = 10 * timings->pixel_clock;

	/* HORIZONTAL FRONT PORCH */
	timings->hfp = edid[current_descriptor_addrs + 8] |
			((edid[current_descriptor_addrs + 11] & 0xc0) << 2);
	/* HORIZONTAL SYNC WIDTH */
	timings->hsw = edid[current_descriptor_addrs + 9] |
			((edid[current_descriptor_addrs + 11] & 0x30) << 4);
	/* HORIZONTAL BACK PORCH */
	timings->hbp = (((edid[current_descriptor_addrs + 4] & 0x0F) << 8) |
			edid[current_descriptor_addrs + 3]) -
			(timings->hfp + timings->hsw);
	/* VERTICAL FRONT PORCH */
	timings->vfp = ((edid[current_descriptor_addrs + 10] & 0xF0) >> 4) |
			((edid[current_descriptor_addrs + 11] & 0x0f) << 2);
	/* VERTICAL SYNC WIDTH */
	timings->vsw = (edid[current_descriptor_addrs + 10] & 0x0F) |
			((edid[current_descriptor_addrs + 11] & 0x03) << 4);
	/* VERTICAL BACK PORCH */
	timings->vbp = (((edid[current_descriptor_addrs + 7] & 0x0F) << 8) |
			edid[current_descriptor_addrs + 6]) -
			(timings->vfp + timings->vsw);

}

/* Description : This function gets the resolution information from EDID */
static void get_edid_timing_data(u8 *edid)
{
	u8 count;
	u16 current_descriptor_addrs;
	struct hdmi_cm cm;
	struct omap_video_timings edid_timings;

	/* search block 0, there are 4 DTDs arranged in priority order */
	for (count = 0; count < EDID_SIZE_BLOCK0_TIMING_DESCRIPTOR; count++) {
		current_descriptor_addrs =
			EDID_DESCRIPTOR_BLOCK0_ADDRESS +
			count * EDID_TIMING_DESCRIPTOR_SIZE;
		get_horz_vert_timing_info(current_descriptor_addrs,
				edid, &edid_timings);
		cm = hdmi_get_code(&edid_timings);
		DSSDBG("Block0[%d] value matches code = %d , mode = %d\n",
			count, cm.code, cm.mode);
		if (cm.code == -1) {
			continue;
		} else {
			hdmi.code = cm.code;
			hdmi.mode = cm.mode;
			DSSDBG("code = %d , mode = %d\n",
				hdmi.code, hdmi.mode);
			return;
		}
	}
	if (edid[0x7e] != 0x00) {
		for (count = 0; count < EDID_SIZE_BLOCK1_TIMING_DESCRIPTOR;
			count++) {
			current_descriptor_addrs =
			EDID_DESCRIPTOR_BLOCK1_ADDRESS +
			count * EDID_TIMING_DESCRIPTOR_SIZE;
			get_horz_vert_timing_info(current_descriptor_addrs,
						edid, &edid_timings);
			cm = hdmi_get_code(&edid_timings);
			DSSDBG("Block1[%d] value matches code = %d, mode = %d",
				count, cm.code, cm.mode);
			if (cm.code == -1) {
				continue;
			} else {
				hdmi.code = cm.code;
				hdmi.mode = cm.mode;
				DSSDBG("code = %d , mode = %d\n",
					hdmi.code, hdmi.mode);
				return;
			}
		}
	}

	DSSINFO("no valid timing found , falling back to VGA\n");
	hdmi.code = 4; /* setting default value of 640 480 VGA */
	hdmi.mode = HDMI_DVI;
}

static void hdmi_read_edid(struct omap_video_timings *dp)
{
	int ret = 0, code;

	memset(hdmi.edid, 0, HDMI_EDID_MAX_LENGTH);

	if (!hdmi.edid_set)
		ret = read_edid(hdmi.edid, HDMI_EDID_MAX_LENGTH);

	if (!ret) {
		if (!memcmp(hdmi.edid, edid_header, sizeof(edid_header))) {
			/* search for timings of default resolution */
			get_edid_timing_data(hdmi.edid);
			hdmi.edid_set = true;
		}
	} else {
		DSSWARN("failed to read E-EDID\n");
	}

	if (!hdmi.edid_set) {
		DSSINFO("fallback to VGA\n");
		hdmi.code = 4; /* setting default value of 640 480 VGA */
		hdmi.mode = HDMI_DVI;
	}

	code = get_timings_index();

	*dp = cea_vesa_timings[code].timings;
}

static void hdmi_core_init(struct hdmi_core_video_config *video_cfg,
			struct hdmi_core_infoframe_avi *avi_cfg,
			struct hdmi_core_packet_enable_repeat *repeat_cfg)
{
	DSSDBG("Enter hdmi_core_init\n");

	/* video core */
	video_cfg->ip_bus_width = HDMI_INPUT_8BIT;
	video_cfg->op_dither_truc = HDMI_OUTPUTTRUNCATION_8BIT;
	video_cfg->deep_color_pkt = HDMI_DEEPCOLORPACKECTDISABLE;
	video_cfg->pkt_mode = HDMI_PACKETMODERESERVEDVALUE;
	video_cfg->hdmi_dvi = HDMI_DVI;
	video_cfg->tclk_sel_clkmult = HDMI_FPLL10IDCK;

	/* info frame */
	avi_cfg->db1_format = 0;
	avi_cfg->db1_active_info = 0;
	avi_cfg->db1_bar_info_dv = 0;
	avi_cfg->db1_scan_info = 0;
	avi_cfg->db2_colorimetry = 0;
	avi_cfg->db2_aspect_ratio = 0;
	avi_cfg->db2_active_fmt_ar = 0;
	avi_cfg->db3_itc = 0;
	avi_cfg->db3_ec = 0;
	avi_cfg->db3_q_range = 0;
	avi_cfg->db3_nup_scaling = 0;
	avi_cfg->db4_videocode = 0;
	avi_cfg->db5_pixel_repeat = 0;
	avi_cfg->db6_7_line_eoftop = 0 ;
	avi_cfg->db8_9_line_sofbottom = 0;
	avi_cfg->db10_11_pixel_eofleft = 0;
	avi_cfg->db12_13_pixel_sofright = 0;

	/* packet enable and repeat */
	repeat_cfg->audio_pkt = 0;
	repeat_cfg->audio_pkt_repeat = 0;
	repeat_cfg->avi_infoframe = 0;
	repeat_cfg->avi_infoframe_repeat = 0;
	repeat_cfg->gen_cntrl_pkt = 0;
	repeat_cfg->gen_cntrl_pkt_repeat = 0;
	repeat_cfg->generic_pkt = 0;
	repeat_cfg->generic_pkt_repeat = 0;
}

static void hdmi_core_powerdown_disable(void)
{
	DSSDBG("Enter hdmi_core_powerdown_disable\n");
	REG_FLD_MOD(HDMI_CORE_CTRL1, 0x0, 0, 0);
}

static void hdmi_core_swreset_release(void)
{
	DSSDBG("Enter hdmi_core_swreset_release\n");
	REG_FLD_MOD(HDMI_CORE_SYS_SRST, 0x0, 0, 0);
}

static void hdmi_core_swreset_assert(void)
{
	DSSDBG("Enter hdmi_core_swreset_assert\n");
	REG_FLD_MOD(HDMI_CORE_SYS_SRST, 0x1, 0, 0);
}

/* DSS_HDMI_CORE_VIDEO_CONFIG */
static void hdmi_core_video_config(struct hdmi_core_video_config *cfg)
{
	u32 r = 0;

	/* sys_ctrl1 default configuration not tunable */
	r = hdmi_read_reg(HDMI_CORE_CTRL1);
	r = FLD_MOD(r, HDMI_CORE_CTRL1_VEN_FOLLOWVSYNC, 5, 5);
	r = FLD_MOD(r, HDMI_CORE_CTRL1_HEN_FOLLOWHSYNC, 4, 4);
	r = FLD_MOD(r, HDMI_CORE_CTRL1_BSEL_24BITBUS, 2, 2);
	r = FLD_MOD(r, HDMI_CORE_CTRL1_EDGE_RISINGEDGE, 1, 1);
	hdmi_write_reg(HDMI_CORE_CTRL1, r);

	REG_FLD_MOD(HDMI_CORE_SYS_VID_ACEN, cfg->ip_bus_width, 7, 6);

	/* Vid_Mode */
	r = hdmi_read_reg(HDMI_CORE_SYS_VID_MODE);

	/* dither truncation configuration */
	if (cfg->op_dither_truc > HDMI_OUTPUTTRUNCATION_12BIT) {
		r = FLD_MOD(r, cfg->op_dither_truc - 3, 7, 6);
		r = FLD_MOD(r, 1, 5, 5);
	} else {
		r = FLD_MOD(r, cfg->op_dither_truc, 7, 6);
		r = FLD_MOD(r, 0, 5, 5);
	}
	hdmi_write_reg(HDMI_CORE_SYS_VID_MODE, r);

	/* HDMI_Ctrl */
	r = hdmi_read_reg(HDMI_CORE_AV_HDMI_CTRL);
	r = FLD_MOD(r, cfg->deep_color_pkt, 6, 6);
	r = FLD_MOD(r, cfg->pkt_mode, 5, 3);
	r = FLD_MOD(r, cfg->hdmi_dvi, 0, 0);
	hdmi_write_reg(HDMI_CORE_AV_HDMI_CTRL, r);

	/* TMDS_CTRL */
	REG_FLD_MOD(HDMI_CORE_SYS_TMDS_CTRL,
		cfg->tclk_sel_clkmult, 6, 5);
}

static void hdmi_core_aux_infoframe_avi_config(
		struct hdmi_core_infoframe_avi info_avi)
{
	u32 val;
	char sum = 0, checksum = 0;

	sum += 0x82 + 0x002 + 0x00D;
	hdmi_write_reg(HDMI_CORE_AV_AVI_TYPE, 0x082);
	hdmi_write_reg(HDMI_CORE_AV_AVI_VERS, 0x002);
	hdmi_write_reg(HDMI_CORE_AV_AVI_LEN, 0x00D);

	val = (info_avi.db1_format << 5) |
		(info_avi.db1_active_info << 4) |
		(info_avi.db1_bar_info_dv << 2) |
		(info_avi.db1_scan_info);
	hdmi_write_reg(HDMI_CORE_AV_AVI_DBYTE(0), val);
	sum += val;

	val = (info_avi.db2_colorimetry << 6) |
		(info_avi.db2_aspect_ratio << 4) |
		(info_avi.db2_active_fmt_ar);
	hdmi_write_reg(HDMI_CORE_AV_AVI_DBYTE(1), val);
	sum += val;

	val = (info_avi.db3_itc << 7) |
		(info_avi.db3_ec << 4) |
		(info_avi.db3_q_range << 2) |
		(info_avi.db3_nup_scaling);
	hdmi_write_reg(HDMI_CORE_AV_AVI_DBYTE(2), val);
	sum += val;

	hdmi_write_reg(HDMI_CORE_AV_AVI_DBYTE(3), info_avi.db4_videocode);
	sum += info_avi.db4_videocode;

	val = info_avi.db5_pixel_repeat;
	hdmi_write_reg(HDMI_CORE_AV_AVI_DBYTE(4), val);
	sum += val;

	val = info_avi.db6_7_line_eoftop & 0x00FF;
	hdmi_write_reg(HDMI_CORE_AV_AVI_DBYTE(5), val);
	sum += val;

	val = ((info_avi.db6_7_line_eoftop >> 8) & 0x00FF);
	hdmi_write_reg(HDMI_CORE_AV_AVI_DBYTE(6), val);
	sum += val;

	val = info_avi.db8_9_line_sofbottom & 0x00FF;
	hdmi_write_reg(HDMI_CORE_AV_AVI_DBYTE(7), val);
	sum += val;

	val = ((info_avi.db8_9_line_sofbottom >> 8) & 0x00FF);
	hdmi_write_reg(HDMI_CORE_AV_AVI_DBYTE(8), val);
	sum += val;

	val = info_avi.db10_11_pixel_eofleft & 0x00FF;
	hdmi_write_reg(HDMI_CORE_AV_AVI_DBYTE(9), val);
	sum += val;

	val = ((info_avi.db10_11_pixel_eofleft >> 8) & 0x00FF);
	hdmi_write_reg(HDMI_CORE_AV_AVI_DBYTE(10), val);
	sum += val;

	val = info_avi.db12_13_pixel_sofright & 0x00FF;
	hdmi_write_reg(HDMI_CORE_AV_AVI_DBYTE(11), val);
	sum += val;

	val = ((info_avi.db12_13_pixel_sofright >> 8) & 0x00FF);
	hdmi_write_reg(HDMI_CORE_AV_AVI_DBYTE(12), val);
	sum += val;

	checksum = 0x100 - sum;
	hdmi_write_reg(HDMI_CORE_AV_AVI_CHSUM, checksum);
}

static void hdmi_core_av_packet_config(
		struct hdmi_core_packet_enable_repeat repeat_cfg)
{
	/* enable/repeat the infoframe */
	hdmi_write_reg(HDMI_CORE_AV_PB_CTRL1,
		(repeat_cfg.audio_pkt << 5) |
		(repeat_cfg.audio_pkt_repeat << 4) |
		(repeat_cfg.avi_infoframe << 1) |
		(repeat_cfg.avi_infoframe_repeat));

	/* enable/repeat the packet */
	hdmi_write_reg(HDMI_CORE_AV_PB_CTRL2,
		(repeat_cfg.gen_cntrl_pkt << 3) |
		(repeat_cfg.gen_cntrl_pkt_repeat << 2) |
		(repeat_cfg.generic_pkt << 1) |
		(repeat_cfg.generic_pkt_repeat));
}

static void hdmi_wp_init(struct omap_video_timings *timings,
			struct hdmi_video_format *video_fmt,
			struct hdmi_video_interface *video_int)
{
	DSSDBG("Enter hdmi_wp_init\n");

	timings->hbp = 0;
	timings->hfp = 0;
	timings->hsw = 0;
	timings->vbp = 0;
	timings->vfp = 0;
	timings->vsw = 0;

	video_fmt->packing_mode = HDMI_PACK_10b_RGB_YUV444;
	video_fmt->y_res = 0;
	video_fmt->x_res = 0;

	video_int->vsp = 0;
	video_int->hsp = 0;

	video_int->interlacing = 0;
	video_int->tm = 0; /* HDMI_TIMING_SLAVE */

}

static void hdmi_wp_video_start(bool start)
{
	REG_FLD_MOD(HDMI_WP_VIDEO_CFG, start, 31, 31);
}

static void hdmi_wp_video_init_format(struct hdmi_video_format *video_fmt,
	struct omap_video_timings *timings, struct hdmi_config *param)
{
	DSSDBG("Enter hdmi_wp_video_init_format\n");

	video_fmt->y_res = param->timings.timings.y_res;
	video_fmt->x_res = param->timings.timings.x_res;

	timings->hbp = param->timings.timings.hbp;
	timings->hfp = param->timings.timings.hfp;
	timings->hsw = param->timings.timings.hsw;
	timings->vbp = param->timings.timings.vbp;
	timings->vfp = param->timings.timings.vfp;
	timings->vsw = param->timings.timings.vsw;
}

static void hdmi_wp_video_config_format(
		struct hdmi_video_format *video_fmt)
{
	u32 l = 0;

	REG_FLD_MOD(HDMI_WP_VIDEO_CFG, video_fmt->packing_mode, 10, 8);

	l |= FLD_VAL(video_fmt->y_res, 31, 16);
	l |= FLD_VAL(video_fmt->x_res, 15, 0);
	hdmi_write_reg(HDMI_WP_VIDEO_SIZE, l);
}

static void hdmi_wp_video_config_interface(
		struct hdmi_video_interface *video_int)
{
	u32 r;
	DSSDBG("Enter hdmi_wp_video_config_interface\n");

	r = hdmi_read_reg(HDMI_WP_VIDEO_CFG);
	r = FLD_MOD(r, video_int->vsp, 7, 7);
	r = FLD_MOD(r, video_int->hsp, 6, 6);
	r = FLD_MOD(r, video_int->interlacing, 3, 3);
	r = FLD_MOD(r, video_int->tm, 1, 0);
	hdmi_write_reg(HDMI_WP_VIDEO_CFG, r);
}

static void hdmi_wp_video_config_timing(
		struct omap_video_timings *timings)
{
	u32 timing_h = 0;
	u32 timing_v = 0;

	DSSDBG("Enter hdmi_wp_video_config_timing\n");

	timing_h |= FLD_VAL(timings->hbp, 31, 20);
	timing_h |= FLD_VAL(timings->hfp, 19, 8);
	timing_h |= FLD_VAL(timings->hsw, 7, 0);
	hdmi_write_reg(HDMI_WP_VIDEO_TIMING_H, timing_h);

	timing_v |= FLD_VAL(timings->vbp, 31, 20);
	timing_v |= FLD_VAL(timings->vfp, 19, 8);
	timing_v |= FLD_VAL(timings->vsw, 7, 0);
	hdmi_write_reg(HDMI_WP_VIDEO_TIMING_V, timing_v);
}

static void hdmi_basic_configure(struct hdmi_config *cfg)
{
	/* HDMI */
	struct omap_video_timings video_timing;
	struct hdmi_video_format video_format;
	struct hdmi_video_interface video_interface;
	/* HDMI core */
	struct hdmi_core_infoframe_avi avi_cfg;
	struct hdmi_core_video_config v_core_cfg;
	struct hdmi_core_packet_enable_repeat repeat_cfg;

	hdmi_wp_init(&video_timing, &video_format,
		&video_interface);

	hdmi_core_init(&v_core_cfg,
		&avi_cfg,
		&repeat_cfg);

	hdmi_wp_video_init_format(&video_format,
			&video_timing, cfg);

	hdmi_wp_video_config_timing(&video_timing);

	/* video config */
	video_format.packing_mode = HDMI_PACK_24b_RGB_YUV444_YUV422;

	hdmi_wp_video_config_format(&video_format);

	video_interface.vsp = cfg->timings.vsync_pol;
	video_interface.hsp = cfg->timings.hsync_pol;
	video_interface.interlacing = cfg->interlace;
	video_interface.tm = 1 ; /* HDMI_TIMING_MASTER_24BIT */

	hdmi_wp_video_config_interface(&video_interface);

	/*
	 * configure core video part
	 * set software reset in the core
	 */
	hdmi_core_swreset_assert();

	/* power down off */
	hdmi_core_powerdown_disable();

	v_core_cfg.pkt_mode = HDMI_PACKETMODE24BITPERPIXEL;
	v_core_cfg.hdmi_dvi = cfg->cm.mode;

	hdmi_core_video_config(&v_core_cfg);

	/* release software reset in the core */
	hdmi_core_swreset_release();

	/*
	 * configure packet
	 * info frame video see doc CEA861-D page 65
	 */
	avi_cfg.db1_format = HDMI_INFOFRAME_AVI_DB1Y_RGB;
	avi_cfg.db1_active_info =
		HDMI_INFOFRAME_AVI_DB1A_ACTIVE_FORMAT_OFF;
	avi_cfg.db1_bar_info_dv = HDMI_INFOFRAME_AVI_DB1B_NO;
	avi_cfg.db1_scan_info = HDMI_INFOFRAME_AVI_DB1S_0;
	avi_cfg.db2_colorimetry = HDMI_INFOFRAME_AVI_DB2C_NO;
	avi_cfg.db2_aspect_ratio = HDMI_INFOFRAME_AVI_DB2M_NO;
	avi_cfg.db2_active_fmt_ar = HDMI_INFOFRAME_AVI_DB2R_SAME;
	avi_cfg.db3_itc = HDMI_INFOFRAME_AVI_DB3ITC_NO;
	avi_cfg.db3_ec = HDMI_INFOFRAME_AVI_DB3EC_XVYUV601;
	avi_cfg.db3_q_range = HDMI_INFOFRAME_AVI_DB3Q_DEFAULT;
	avi_cfg.db3_nup_scaling = HDMI_INFOFRAME_AVI_DB3SC_NO;
	avi_cfg.db4_videocode = cfg->cm.code;
	avi_cfg.db5_pixel_repeat = HDMI_INFOFRAME_AVI_DB5PR_NO;
	avi_cfg.db6_7_line_eoftop = 0;
	avi_cfg.db8_9_line_sofbottom = 0;
	avi_cfg.db10_11_pixel_eofleft = 0;
	avi_cfg.db12_13_pixel_sofright = 0;

	hdmi_core_aux_infoframe_avi_config(avi_cfg);

	/* enable/repeat the infoframe */
	repeat_cfg.avi_infoframe = HDMI_PACKETENABLE;
	repeat_cfg.avi_infoframe_repeat = HDMI_PACKETREPEATON;
	/* wakeup */
	repeat_cfg.audio_pkt = HDMI_PACKETENABLE;
	repeat_cfg.audio_pkt_repeat = HDMI_PACKETREPEATON;
	hdmi_core_av_packet_config(repeat_cfg);
}

static void update_hdmi_timings(struct hdmi_config *cfg,
		struct omap_video_timings *timings, int code)
{
	cfg->timings.timings.x_res = timings->x_res;
	cfg->timings.timings.y_res = timings->y_res;
	cfg->timings.timings.hbp = timings->hbp;
	cfg->timings.timings.hfp = timings->hfp;
	cfg->timings.timings.hsw = timings->hsw;
	cfg->timings.timings.vbp = timings->vbp;
	cfg->timings.timings.vfp = timings->vfp;
	cfg->timings.timings.vsw = timings->vsw;
	cfg->timings.timings.pixel_clock = timings->pixel_clock;
	cfg->timings.vsync_pol = cea_vesa_timings[code].vsync_pol;
	cfg->timings.hsync_pol = cea_vesa_timings[code].hsync_pol;
}

static void hdmi_compute_pll(struct omap_dss_device *dssdev, int phy,
		struct hdmi_pll_info *pi)
{
	unsigned long clkin, refclk;
	u32 mf;

	clkin = clk_get_rate(hdmi.sys_clk) / 10000;
	/*
	 * Input clock is predivided by N + 1
	 * out put of which is reference clk
	 */
	pi->regn = dssdev->clocks.hdmi.regn;
	refclk = clkin / (pi->regn + 1);

	/*
	 * multiplier is pixel_clk/ref_clk
	 * Multiplying by 100 to avoid fractional part removal
	 */
	pi->regm = (phy * 100 / (refclk)) / 100;
	pi->regm2 = dssdev->clocks.hdmi.regm2;

	/*
	 * fractional multiplier is remainder of the difference between
	 * multiplier and actual phy(required pixel clock thus should be
	 * multiplied by 2^18(262144) divided by the reference clock
	 */
	mf = (phy - pi->regm * refclk) * 262144;
	pi->regmf = mf / (refclk);

	/*
	 * Dcofreq should be set to 1 if required pixel clock
	 * is greater than 1000MHz
	 */
	pi->dcofreq = phy > 1000 * 100;
	pi->regsd = ((pi->regm * clkin / 10) / ((pi->regn + 1) * 250) + 5) / 10;

	DSSDBG("M = %d Mf = %d\n", pi->regm, pi->regmf);
	DSSDBG("range = %d sd = %d\n", pi->dcofreq, pi->regsd);
}

static int hdmi_power_on(struct omap_dss_device *dssdev)
{
	int r, code = 0;
	struct hdmi_pll_info pll_data;
	struct omap_video_timings *p;
	unsigned long phy;

	r = hdmi_runtime_get();
	if (r)
		return r;

	dispc_enable_channel(OMAP_DSS_CHANNEL_DIGIT, 0);

	p = &dssdev->panel.timings;

	DSSDBG("hdmi_power_on x_res= %d y_res = %d\n",
		dssdev->panel.timings.x_res,
		dssdev->panel.timings.y_res);

	if (!hdmi.custom_set) {
		DSSDBG("Read EDID as no EDID is not set on poweron\n");
		hdmi_read_edid(p);
	}
	code = get_timings_index();
	dssdev->panel.timings = cea_vesa_timings[code].timings;
	update_hdmi_timings(&hdmi.cfg, p, code);

	phy = p->pixel_clock;

	hdmi_compute_pll(dssdev, phy, &pll_data);

	hdmi_wp_video_start(0);

	/* config the PLL and PHY first */
	r = hdmi_pll_program(&pll_data);
	if (r) {
		DSSDBG("Failed to lock PLL\n");
		goto err;
	}

	r = hdmi_phy_init();
	if (r) {
		DSSDBG("Failed to start PHY\n");
		goto err;
	}

	hdmi.cfg.cm.mode = hdmi.mode;
	hdmi.cfg.cm.code = hdmi.code;
	hdmi_basic_configure(&hdmi.cfg);

	/* Make selection of HDMI in DSS */
	dss_select_hdmi_venc_clk_source(DSS_HDMI_M_PCLK);

	/* Select the dispc clock source as PRCM clock, to ensure that it is not
	 * DSI PLL source as the clock selected by DSI PLL might not be
	 * sufficient for the resolution selected / that can be changed
	 * dynamically by user. This can be moved to single location , say
	 * Boardfile.
	 */
	dss_select_dispc_clk_source(dssdev->clocks.dispc.dispc_fclk_src);

	/* bypass TV gamma table */
	dispc_enable_gamma_table(0);

	/* tv size */
	dispc_set_digit_size(dssdev->panel.timings.x_res,
			dssdev->panel.timings.y_res);

	dispc_enable_channel(OMAP_DSS_CHANNEL_DIGIT, 1);

	hdmi_wp_video_start(1);

	return 0;
err:
	hdmi_runtime_put();
	return -EIO;
}

static void hdmi_power_off(struct omap_dss_device *dssdev)
{
	dispc_enable_channel(OMAP_DSS_CHANNEL_DIGIT, 0);

	hdmi_wp_video_start(0);
	hdmi_phy_off();
	hdmi_set_pll_pwr(HDMI_PLLPWRCMD_ALLOFF);
	hdmi_runtime_put();

	hdmi.edid_set = 0;
}

int omapdss_hdmi_display_check_timing(struct omap_dss_device *dssdev,
					struct omap_video_timings *timings)
{
	struct hdmi_cm cm;

	cm = hdmi_get_code(timings);
	if (cm.code == -1) {
		DSSERR("Invalid timing entered\n");
		return -EINVAL;
	}

	return 0;

}

void omapdss_hdmi_display_set_timing(struct omap_dss_device *dssdev)
{
	struct hdmi_cm cm;

	hdmi.custom_set = 1;
	cm = hdmi_get_code(&dssdev->panel.timings);
	hdmi.code = cm.code;
	hdmi.mode = cm.mode;
	omapdss_hdmi_display_enable(dssdev);
	hdmi.custom_set = 0;
}

int omapdss_hdmi_display_enable(struct omap_dss_device *dssdev)
{
	int r = 0;

	DSSDBG("ENTER hdmi_display_enable\n");

	mutex_lock(&hdmi.lock);

	r = omap_dss_start_device(dssdev);
	if (r) {
		DSSERR("failed to start device\n");
		goto err0;
	}

	if (dssdev->platform_enable) {
		r = dssdev->platform_enable(dssdev);
		if (r) {
			DSSERR("failed to enable GPIO's\n");
			goto err1;
		}
	}

	r = hdmi_power_on(dssdev);
	if (r) {
		DSSERR("failed to power on device\n");
		goto err2;
	}

	mutex_unlock(&hdmi.lock);
	return 0;

err2:
	if (dssdev->platform_disable)
		dssdev->platform_disable(dssdev);
err1:
	omap_dss_stop_device(dssdev);
err0:
	mutex_unlock(&hdmi.lock);
	return r;
}

void omapdss_hdmi_display_disable(struct omap_dss_device *dssdev)
{
	DSSDBG("Enter hdmi_display_disable\n");

	mutex_lock(&hdmi.lock);

	hdmi_power_off(dssdev);

	if (dssdev->platform_disable)
		dssdev->platform_disable(dssdev);

	omap_dss_stop_device(dssdev);

	mutex_unlock(&hdmi.lock);
}

#if defined(CONFIG_SND_OMAP_SOC_OMAP4_HDMI) || \
	defined(CONFIG_SND_OMAP_SOC_OMAP4_HDMI_MODULE)
static void hdmi_wp_audio_config_format(
		struct hdmi_audio_format *aud_fmt)
{
	u32 r;

	DSSDBG("Enter hdmi_wp_audio_config_format\n");

	r = hdmi_read_reg(HDMI_WP_AUDIO_CFG);
	r = FLD_MOD(r, aud_fmt->stereo_channels, 26, 24);
	r = FLD_MOD(r, aud_fmt->active_chnnls_msk, 23, 16);
	r = FLD_MOD(r, aud_fmt->en_sig_blk_strt_end, 5, 5);
	r = FLD_MOD(r, aud_fmt->type, 4, 4);
	r = FLD_MOD(r, aud_fmt->justification, 3, 3);
	r = FLD_MOD(r, aud_fmt->sample_order, 2, 2);
	r = FLD_MOD(r, aud_fmt->samples_per_word, 1, 1);
	r = FLD_MOD(r, aud_fmt->sample_size, 0, 0);
	hdmi_write_reg(HDMI_WP_AUDIO_CFG, r);
}

static void hdmi_wp_audio_config_dma(struct hdmi_audio_dma *aud_dma)
{
	u32 r;

	DSSDBG("Enter hdmi_wp_audio_config_dma\n");

	r = hdmi_read_reg(HDMI_WP_AUDIO_CFG2);
	r = FLD_MOD(r, aud_dma->transfer_size, 15, 8);
	r = FLD_MOD(r, aud_dma->block_size, 7, 0);
	hdmi_write_reg(HDMI_WP_AUDIO_CFG2, r);

	r = hdmi_read_reg(HDMI_WP_AUDIO_CTRL);
	r = FLD_MOD(r, aud_dma->mode, 9, 9);
	r = FLD_MOD(r, aud_dma->fifo_threshold, 8, 0);
	hdmi_write_reg(HDMI_WP_AUDIO_CTRL, r);
}

static void hdmi_core_audio_config(struct hdmi_core_audio_config *cfg)
{
	u32 r;

	/* audio clock recovery parameters */
	r = hdmi_read_reg(HDMI_CORE_AV_ACR_CTRL);
	r = FLD_MOD(r, cfg->use_mclk, 2, 2);
	r = FLD_MOD(r, cfg->en_acr_pkt, 1, 1);
	r = FLD_MOD(r, cfg->cts_mode, 0, 0);
	hdmi_write_reg(HDMI_CORE_AV_ACR_CTRL, r);

	REG_FLD_MOD(HDMI_CORE_AV_N_SVAL1, cfg->n, 7, 0);
	REG_FLD_MOD(HDMI_CORE_AV_N_SVAL2, cfg->n >> 8, 7, 0);
	REG_FLD_MOD(HDMI_CORE_AV_N_SVAL3, cfg->n >> 16, 7, 0);

	if (cfg->cts_mode == HDMI_AUDIO_CTS_MODE_SW) {
		REG_FLD_MOD(HDMI_CORE_AV_CTS_SVAL1, cfg->cts, 7, 0);
		REG_FLD_MOD(HDMI_CORE_AV_CTS_SVAL2, cfg->cts >> 8, 7, 0);
		REG_FLD_MOD(HDMI_CORE_AV_CTS_SVAL3, cfg->cts >> 16, 7, 0);
	} else {
		/*
		 * HDMI IP uses this configuration to divide the MCLK to
		 * update CTS value.
		 */
		REG_FLD_MOD(HDMI_CORE_AV_FREQ_SVAL, cfg->mclk_mode, 2, 0);

		/* Configure clock for audio packets */
		REG_FLD_MOD(HDMI_CORE_AV_AUD_PAR_BUSCLK_1,
			cfg->aud_par_busclk, 7, 0);
		REG_FLD_MOD(HDMI_CORE_AV_AUD_PAR_BUSCLK_2,
			(cfg->aud_par_busclk >> 8), 7, 0);
		REG_FLD_MOD(HDMI_CORE_AV_AUD_PAR_BUSCLK_3,
			(cfg->aud_par_busclk >> 16), 7, 0);
	}

	/* Override of SPDIF sample frequency with value in I2S_CHST4 */
	REG_FLD_MOD(HDMI_CORE_AV_SPDIF_CTRL, cfg->fs_override, 1, 1);

	/* I2S parameters */
	REG_FLD_MOD(HDMI_CORE_AV_I2S_CHST4, cfg->freq_sample, 3, 0);

	r = hdmi_read_reg(HDMI_CORE_AV_I2S_IN_CTRL);
	r = FLD_MOD(r, cfg->i2s_cfg.en_high_bitrate_aud, 7, 7);
	r = FLD_MOD(r, cfg->i2s_cfg.sck_edge_mode, 6, 6);
	r = FLD_MOD(r, cfg->i2s_cfg.cbit_order, 5, 5);
	r = FLD_MOD(r, cfg->i2s_cfg.vbit, 4, 4);
	r = FLD_MOD(r, cfg->i2s_cfg.ws_polarity, 3, 3);
	r = FLD_MOD(r, cfg->i2s_cfg.justification, 2, 2);
	r = FLD_MOD(r, cfg->i2s_cfg.direction, 1, 1);
	r = FLD_MOD(r, cfg->i2s_cfg.shift, 0, 0);
	hdmi_write_reg(HDMI_CORE_AV_I2S_IN_CTRL, r);

	r = hdmi_read_reg(HDMI_CORE_AV_I2S_CHST5);
	r = FLD_MOD(r, cfg->freq_sample, 7, 4);
	r = FLD_MOD(r, cfg->i2s_cfg.word_length, 3, 1);
	r = FLD_MOD(r, cfg->i2s_cfg.word_max_length, 0, 0);
	hdmi_write_reg(HDMI_CORE_AV_I2S_CHST5, r);

	REG_FLD_MOD(HDMI_CORE_AV_I2S_IN_LEN, cfg->i2s_cfg.in_length_bits, 3, 0);

	/* Audio channels and mode parameters */
	REG_FLD_MOD(HDMI_CORE_AV_HDMI_CTRL, cfg->layout, 2, 1);
	r = hdmi_read_reg(HDMI_CORE_AV_AUD_MODE);
	r = FLD_MOD(r, cfg->i2s_cfg.active_sds, 7, 4);
	r = FLD_MOD(r, cfg->en_dsd_audio, 3, 3);
	r = FLD_MOD(r, cfg->en_parallel_aud_input, 2, 2);
	r = FLD_MOD(r, cfg->en_spdif, 1, 1);
	hdmi_write_reg(HDMI_CORE_AV_AUD_MODE, r);
}

static void hdmi_core_audio_infoframe_config(
		struct hdmi_core_infoframe_audio *info_aud)
{
	u8 val;
	u8 sum = 0, checksum = 0;

	/*
	 * Set audio info frame type, version and length as
	 * described in HDMI 1.4a Section 8.2.2 specification.
	 * Checksum calculation is defined in Section 5.3.5.
	 */
	hdmi_write_reg(HDMI_CORE_AV_AUDIO_TYPE, 0x84);
	hdmi_write_reg(HDMI_CORE_AV_AUDIO_VERS, 0x01);
	hdmi_write_reg(HDMI_CORE_AV_AUDIO_LEN, 0x0a);
	sum += 0x84 + 0x001 + 0x00a;

	val = (info_aud->db1_coding_type << 4)
			| (info_aud->db1_channel_count - 1);
	hdmi_write_reg(HDMI_CORE_AV_AUD_DBYTE(0), val);
	sum += val;

	val = (info_aud->db2_sample_freq << 2) | info_aud->db2_sample_size;
	hdmi_write_reg(HDMI_CORE_AV_AUD_DBYTE(1), val);
	sum += val;

	hdmi_write_reg(HDMI_CORE_AV_AUD_DBYTE(2), 0x00);

	val = info_aud->db4_channel_alloc;
	hdmi_write_reg(HDMI_CORE_AV_AUD_DBYTE(3), val);
	sum += val;

	val = (info_aud->db5_downmix_inh << 7) | (info_aud->db5_lsv << 3);
	hdmi_write_reg(HDMI_CORE_AV_AUD_DBYTE(4), val);
	sum += val;

	hdmi_write_reg(HDMI_CORE_AV_AUD_DBYTE(5), 0x00);
	hdmi_write_reg(HDMI_CORE_AV_AUD_DBYTE(6), 0x00);
	hdmi_write_reg(HDMI_CORE_AV_AUD_DBYTE(7), 0x00);
	hdmi_write_reg(HDMI_CORE_AV_AUD_DBYTE(8), 0x00);
	hdmi_write_reg(HDMI_CORE_AV_AUD_DBYTE(9), 0x00);

	checksum = 0x100 - sum;
	hdmi_write_reg(HDMI_CORE_AV_AUDIO_CHSUM, checksum);

	/*
	 * TODO: Add MPEG and SPD enable and repeat cfg when EDID parsing
	 * is available.
	 */
}

static int hdmi_config_audio_acr(u32 sample_freq, u32 *n, u32 *cts)
{
	u32 r;
	u32 deep_color = 0;
	u32 pclk = hdmi.cfg.timings.timings.pixel_clock;

	if (n == NULL || cts == NULL)
		return -EINVAL;
	/*
	 * Obtain current deep color configuration. This needed
	 * to calculate the TMDS clock based on the pixel clock.
	 */
	r = REG_GET(HDMI_WP_VIDEO_CFG, 1, 0);
	switch (r) {
	case 1: /* No deep color selected */
		deep_color = 100;
		break;
	case 2: /* 10-bit deep color selected */
		deep_color = 125;
		break;
	case 3: /* 12-bit deep color selected */
		deep_color = 150;
		break;
	default:
		return -EINVAL;
	}

	switch (sample_freq) {
	case 32000:
		if ((deep_color == 125) && ((pclk == 54054)
				|| (pclk == 74250)))
			*n = 8192;
		else
			*n = 4096;
		break;
	case 44100:
		*n = 6272;
		break;
	case 48000:
		if ((deep_color == 125) && ((pclk == 54054)
				|| (pclk == 74250)))
			*n = 8192;
		else
			*n = 6144;
		break;
	default:
		*n = 0;
		return -EINVAL;
	}

	/* Calculate CTS. See HDMI 1.3a or 1.4a specifications */
	*cts = pclk * (*n / 128) * deep_color / (sample_freq / 10);

	return 0;
}

static int hdmi_audio_hw_params(struct snd_pcm_substream *substream,
				    struct snd_pcm_hw_params *params,
				    struct snd_soc_dai *dai)
{
	struct hdmi_audio_format audio_format;
	struct hdmi_audio_dma audio_dma;
	struct hdmi_core_audio_config core_cfg;
	struct hdmi_core_infoframe_audio aud_if_cfg;
	int err, n, cts;
	enum hdmi_core_audio_sample_freq sample_freq;

	switch (params_format(params)) {
	case SNDRV_PCM_FORMAT_S16_LE:
		core_cfg.i2s_cfg.word_max_length =
			HDMI_AUDIO_I2S_MAX_WORD_20BITS;
		core_cfg.i2s_cfg.word_length = HDMI_AUDIO_I2S_CHST_WORD_16_BITS;
		core_cfg.i2s_cfg.in_length_bits =
			HDMI_AUDIO_I2S_INPUT_LENGTH_16;
		core_cfg.i2s_cfg.justification = HDMI_AUDIO_JUSTIFY_LEFT;
		audio_format.samples_per_word = HDMI_AUDIO_ONEWORD_TWOSAMPLES;
		audio_format.sample_size = HDMI_AUDIO_SAMPLE_16BITS;
		audio_format.justification = HDMI_AUDIO_JUSTIFY_LEFT;
		audio_dma.transfer_size = 0x10;
		break;
	case SNDRV_PCM_FORMAT_S24_LE:
		core_cfg.i2s_cfg.word_max_length =
			HDMI_AUDIO_I2S_MAX_WORD_24BITS;
		core_cfg.i2s_cfg.word_length = HDMI_AUDIO_I2S_CHST_WORD_24_BITS;
		core_cfg.i2s_cfg.in_length_bits =
			HDMI_AUDIO_I2S_INPUT_LENGTH_24;
		audio_format.samples_per_word = HDMI_AUDIO_ONEWORD_ONESAMPLE;
		audio_format.sample_size = HDMI_AUDIO_SAMPLE_24BITS;
		audio_format.justification = HDMI_AUDIO_JUSTIFY_RIGHT;
		core_cfg.i2s_cfg.justification = HDMI_AUDIO_JUSTIFY_RIGHT;
		audio_dma.transfer_size = 0x20;
		break;
	default:
		return -EINVAL;
	}

	switch (params_rate(params)) {
	case 32000:
		sample_freq = HDMI_AUDIO_FS_32000;
		break;
	case 44100:
		sample_freq = HDMI_AUDIO_FS_44100;
		break;
	case 48000:
		sample_freq = HDMI_AUDIO_FS_48000;
		break;
	default:
		return -EINVAL;
	}

	err = hdmi_config_audio_acr(params_rate(params), &n, &cts);
	if (err < 0)
		return err;

	/* Audio wrapper config */
	audio_format.stereo_channels = HDMI_AUDIO_STEREO_ONECHANNEL;
	audio_format.active_chnnls_msk = 0x03;
	audio_format.type = HDMI_AUDIO_TYPE_LPCM;
	audio_format.sample_order = HDMI_AUDIO_SAMPLE_LEFT_FIRST;
	/* Disable start/stop signals of IEC 60958 blocks */
	audio_format.en_sig_blk_strt_end = HDMI_AUDIO_BLOCK_SIG_STARTEND_OFF;

	audio_dma.block_size = 0xC0;
	audio_dma.mode = HDMI_AUDIO_TRANSF_DMA;
	audio_dma.fifo_threshold = 0x20; /* in number of samples */

	hdmi_wp_audio_config_dma(&audio_dma);
	hdmi_wp_audio_config_format(&audio_format);

	/*
	 * I2S config
	 */
	core_cfg.i2s_cfg.en_high_bitrate_aud = false;
	/* Only used with high bitrate audio */
	core_cfg.i2s_cfg.cbit_order = false;
	/* Serial data and word select should change on sck rising edge */
	core_cfg.i2s_cfg.sck_edge_mode = HDMI_AUDIO_I2S_SCK_EDGE_RISING;
	core_cfg.i2s_cfg.vbit = HDMI_AUDIO_I2S_VBIT_FOR_PCM;
	/* Set I2S word select polarity */
	core_cfg.i2s_cfg.ws_polarity = HDMI_AUDIO_I2S_WS_POLARITY_LOW_IS_LEFT;
	core_cfg.i2s_cfg.direction = HDMI_AUDIO_I2S_MSB_SHIFTED_FIRST;
	/* Set serial data to word select shift. See Phillips spec. */
	core_cfg.i2s_cfg.shift = HDMI_AUDIO_I2S_FIRST_BIT_SHIFT;
	/* Enable one of the four available serial data channels */
	core_cfg.i2s_cfg.active_sds = HDMI_AUDIO_I2S_SD0_EN;

	/* Core audio config */
	core_cfg.freq_sample = sample_freq;
	core_cfg.n = n;
	core_cfg.cts = cts;
	if (dss_has_feature(FEAT_HDMI_CTS_SWMODE)) {
		core_cfg.aud_par_busclk = 0;
		core_cfg.cts_mode = HDMI_AUDIO_CTS_MODE_SW;
		core_cfg.use_mclk = false;
	} else {
		core_cfg.aud_par_busclk = (((128 * 31) - 1) << 8);
		core_cfg.cts_mode = HDMI_AUDIO_CTS_MODE_HW;
		core_cfg.use_mclk = true;
		core_cfg.mclk_mode = HDMI_AUDIO_MCLK_128FS;
	}
	core_cfg.layout = HDMI_AUDIO_LAYOUT_2CH;
	core_cfg.en_spdif = false;
	/* Use sample frequency from channel status word */
	core_cfg.fs_override = true;
	/* Enable ACR packets */
	core_cfg.en_acr_pkt = true;
	/* Disable direct streaming digital audio */
	core_cfg.en_dsd_audio = false;
	/* Use parallel audio interface */
	core_cfg.en_parallel_aud_input = true;

	hdmi_core_audio_config(&core_cfg);

	/*
	 * Configure packet
	 * info frame audio see doc CEA861-D page 74
	 */
	aud_if_cfg.db1_coding_type = HDMI_INFOFRAME_AUDIO_DB1CT_FROM_STREAM;
	aud_if_cfg.db1_channel_count = 2;
	aud_if_cfg.db2_sample_freq = HDMI_INFOFRAME_AUDIO_DB2SF_FROM_STREAM;
	aud_if_cfg.db2_sample_size = HDMI_INFOFRAME_AUDIO_DB2SS_FROM_STREAM;
	aud_if_cfg.db4_channel_alloc = 0x00;
	aud_if_cfg.db5_downmix_inh = false;
	aud_if_cfg.db5_lsv = 0;

	hdmi_core_audio_infoframe_config(&aud_if_cfg);
	return 0;
}

static int hdmi_audio_trigger(struct snd_pcm_substream *substream, int cmd,
				  struct snd_soc_dai *dai)
{
	int err = 0;
	switch (cmd) {
	case SNDRV_PCM_TRIGGER_START:
	case SNDRV_PCM_TRIGGER_RESUME:
	case SNDRV_PCM_TRIGGER_PAUSE_RELEASE:
		REG_FLD_MOD(HDMI_CORE_AV_AUD_MODE, 1, 0, 0);
		REG_FLD_MOD(HDMI_WP_AUDIO_CTRL, 1, 31, 31);
		REG_FLD_MOD(HDMI_WP_AUDIO_CTRL, 1, 30, 30);
		break;

	case SNDRV_PCM_TRIGGER_STOP:
	case SNDRV_PCM_TRIGGER_SUSPEND:
	case SNDRV_PCM_TRIGGER_PAUSE_PUSH:
		REG_FLD_MOD(HDMI_CORE_AV_AUD_MODE, 0, 0, 0);
		REG_FLD_MOD(HDMI_WP_AUDIO_CTRL, 0, 30, 30);
		REG_FLD_MOD(HDMI_WP_AUDIO_CTRL, 0, 31, 31);
		break;
	default:
		err = -EINVAL;
	}
	return err;
}

static int hdmi_audio_startup(struct snd_pcm_substream *substream,
				  struct snd_soc_dai *dai)
{
	if (!hdmi.mode) {
		pr_err("Current video settings do not support audio.\n");
		return -EIO;
	}
	return 0;
}

static struct snd_soc_codec_driver hdmi_audio_codec_drv = {
};

static struct snd_soc_dai_ops hdmi_audio_codec_ops = {
	.hw_params = hdmi_audio_hw_params,
	.trigger = hdmi_audio_trigger,
	.startup = hdmi_audio_startup,
};

static struct snd_soc_dai_driver hdmi_codec_dai_drv = {
		.name = "hdmi-audio-codec",
		.playback = {
			.channels_min = 2,
			.channels_max = 2,
			.rates = SNDRV_PCM_RATE_32000 |
				SNDRV_PCM_RATE_44100 | SNDRV_PCM_RATE_48000,
			.formats = SNDRV_PCM_FMTBIT_S16_LE |
				SNDRV_PCM_FMTBIT_S24_LE,
		},
		.ops = &hdmi_audio_codec_ops,
};
#endif

static int hdmi_get_clocks(struct platform_device *pdev)
{
	struct clk *clk;

	clk = clk_get(&pdev->dev, "sys_clk");
	if (IS_ERR(clk)) {
		DSSERR("can't get sys_clk\n");
		return PTR_ERR(clk);
	}

	hdmi.sys_clk = clk;

	clk = clk_get(&pdev->dev, "dss_48mhz_clk");
	if (IS_ERR(clk)) {
		DSSERR("can't get hdmi_clk\n");
		clk_put(hdmi.sys_clk);
		return PTR_ERR(clk);
	}

	hdmi.hdmi_clk = clk;

	return 0;
}

static void hdmi_put_clocks(void)
{
	if (hdmi.sys_clk)
		clk_put(hdmi.sys_clk);
	if (hdmi.hdmi_clk)
		clk_put(hdmi.hdmi_clk);
}

/* HDMI HW IP initialisation */
static int omapdss_hdmihw_probe(struct platform_device *pdev)
{
	struct resource *hdmi_mem;
	int r;

	hdmi.pdata = pdev->dev.platform_data;
	hdmi.pdev = pdev;

	mutex_init(&hdmi.lock);

	hdmi_mem = platform_get_resource(hdmi.pdev, IORESOURCE_MEM, 0);
	if (!hdmi_mem) {
		DSSERR("can't get IORESOURCE_MEM HDMI\n");
		return -EINVAL;
	}

	/* Base address taken from platform */
	hdmi.base_wp = ioremap(hdmi_mem->start, resource_size(hdmi_mem));
	if (!hdmi.base_wp) {
		DSSERR("can't ioremap WP\n");
		return -ENOMEM;
	}

	r = hdmi_get_clocks(pdev);
	if (r) {
		iounmap(hdmi.base_wp);
		return r;
	}

	pm_runtime_enable(&pdev->dev);

	hdmi_panel_init();

#if defined(CONFIG_SND_OMAP_SOC_OMAP4_HDMI) || \
	defined(CONFIG_SND_OMAP_SOC_OMAP4_HDMI_MODULE)

	/* Register ASoC codec DAI */
	r = snd_soc_register_codec(&pdev->dev, &hdmi_audio_codec_drv,
					&hdmi_codec_dai_drv, 1);
	if (r) {
		DSSERR("can't register ASoC HDMI audio codec\n");
		return r;
	}
#endif
	return 0;
}

static int omapdss_hdmihw_remove(struct platform_device *pdev)
{
	hdmi_panel_exit();

#if defined(CONFIG_SND_OMAP_SOC_OMAP4_HDMI) || \
	defined(CONFIG_SND_OMAP_SOC_OMAP4_HDMI_MODULE)
	snd_soc_unregister_codec(&pdev->dev);
#endif

	pm_runtime_disable(&pdev->dev);

	hdmi_put_clocks();

	iounmap(hdmi.base_wp);

	return 0;
}

static int hdmi_runtime_suspend(struct device *dev)
{
	clk_disable(hdmi.hdmi_clk);
	clk_disable(hdmi.sys_clk);

	dispc_runtime_put();
	dss_runtime_put();

	return 0;
}

static int hdmi_runtime_resume(struct device *dev)
{
	int r;

	r = dss_runtime_get();
	if (r < 0)
		goto err_get_dss;

	r = dispc_runtime_get();
	if (r < 0)
		goto err_get_dispc;


	clk_enable(hdmi.sys_clk);
	clk_enable(hdmi.hdmi_clk);

	return 0;

err_get_dispc:
	dss_runtime_put();
err_get_dss:
	return r;
}

static const struct dev_pm_ops hdmi_pm_ops = {
	.runtime_suspend = hdmi_runtime_suspend,
	.runtime_resume = hdmi_runtime_resume,
};

static struct platform_driver omapdss_hdmihw_driver = {
	.probe          = omapdss_hdmihw_probe,
	.remove         = omapdss_hdmihw_remove,
	.driver         = {
		.name   = "omapdss_hdmi",
		.owner  = THIS_MODULE,
		.pm	= &hdmi_pm_ops,
	},
};

int hdmi_init_platform_driver(void)
{
	return platform_driver_register(&omapdss_hdmihw_driver);
}

void hdmi_uninit_platform_driver(void)
{
	return platform_driver_unregister(&omapdss_hdmihw_driver);
}
