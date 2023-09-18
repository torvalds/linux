// SPDX-License-Identifier: GPL-2.0
/*
 * Driver for panels based on Sitronix ST7703 controller, souch as:
 *
 * - Rocktech jh057n00900 5.5" MIPI-DSI panel
 *
 * Copyright (C) Purism SPC 2019
 */

#include <linux/debugfs.h>
#include <linux/delay.h>
#include <linux/gpio/consumer.h>
#include <linux/media-bus-format.h>
#include <linux/mod_devicetable.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/regulator/consumer.h>

#include <video/display_timing.h>
#include <video/mipi_display.h>

#include <drm/drm_mipi_dsi.h>
#include <drm/drm_modes.h>
#include <drm/drm_panel.h>

#define DRV_NAME "panel-sitronix-st7703"

/* Manufacturer specific Commands send via DSI */
#define ST7703_CMD_ALL_PIXEL_OFF 0x22
#define ST7703_CMD_ALL_PIXEL_ON	 0x23
#define ST7703_CMD_SETAPID	 0xB1
#define ST7703_CMD_SETDISP	 0xB2
#define ST7703_CMD_SETRGBIF	 0xB3
#define ST7703_CMD_SETCYC	 0xB4
#define ST7703_CMD_SETBGP	 0xB5
#define ST7703_CMD_SETVCOM	 0xB6
#define ST7703_CMD_SETOTP	 0xB7
#define ST7703_CMD_SETPOWER_EXT	 0xB8
#define ST7703_CMD_SETEXTC	 0xB9
#define ST7703_CMD_SETMIPI	 0xBA
#define ST7703_CMD_SETVDC	 0xBC
#define ST7703_CMD_UNKNOWN_BF	 0xBF
#define ST7703_CMD_SETSCR	 0xC0
#define ST7703_CMD_SETPOWER	 0xC1
#define ST7703_CMD_SETECO	 0xC6
#define ST7703_CMD_SETIO	 0xC7
#define ST7703_CMD_SETCABC	 0xC8
#define ST7703_CMD_SETPANEL	 0xCC
#define ST7703_CMD_SETGAMMA	 0xE0
#define ST7703_CMD_SETEQ	 0xE3
#define ST7703_CMD_SETGIP1	 0xE9
#define ST7703_CMD_SETGIP2	 0xEA
#define ST7703_CMD_UNKNOWN_EF	 0xEF

struct st7703 {
	struct device *dev;
	struct drm_panel panel;
	struct gpio_desc *reset_gpio;
	struct regulator *vcc;
	struct regulator *iovcc;
	bool prepared;

	struct dentry *debugfs;
	const struct st7703_panel_desc *desc;
};

struct st7703_panel_desc {
	const struct drm_display_mode *mode;
	unsigned int lanes;
	unsigned long mode_flags;
	enum mipi_dsi_pixel_format format;
	int (*init_sequence)(struct st7703 *ctx);
};

static inline struct st7703 *panel_to_st7703(struct drm_panel *panel)
{
	return container_of(panel, struct st7703, panel);
}

static int jh057n_init_sequence(struct st7703 *ctx)
{
	struct mipi_dsi_device *dsi = to_mipi_dsi_device(ctx->dev);

	/*
	 * Init sequence was supplied by the panel vendor. Most of the commands
	 * resemble the ST7703 but the number of parameters often don't match
	 * so it's likely a clone.
	 */
	mipi_dsi_generic_write_seq(dsi, ST7703_CMD_SETEXTC,
				   0xF1, 0x12, 0x83);
	mipi_dsi_generic_write_seq(dsi, ST7703_CMD_SETRGBIF,
				   0x10, 0x10, 0x05, 0x05, 0x03, 0xFF, 0x00, 0x00,
				   0x00, 0x00);
	mipi_dsi_generic_write_seq(dsi, ST7703_CMD_SETSCR,
				   0x73, 0x73, 0x50, 0x50, 0x00, 0x00, 0x08, 0x70,
				   0x00);
	mipi_dsi_generic_write_seq(dsi, ST7703_CMD_SETVDC, 0x4E);
	mipi_dsi_generic_write_seq(dsi, ST7703_CMD_SETPANEL, 0x0B);
	mipi_dsi_generic_write_seq(dsi, ST7703_CMD_SETCYC, 0x80);
	mipi_dsi_generic_write_seq(dsi, ST7703_CMD_SETDISP, 0xF0, 0x12, 0x30);
	mipi_dsi_generic_write_seq(dsi, ST7703_CMD_SETEQ,
				   0x07, 0x07, 0x0B, 0x0B, 0x03, 0x0B, 0x00, 0x00,
				   0x00, 0x00, 0xFF, 0x00, 0xC0, 0x10);
	mipi_dsi_generic_write_seq(dsi, ST7703_CMD_SETBGP, 0x08, 0x08);
	msleep(20);

	mipi_dsi_generic_write_seq(dsi, ST7703_CMD_SETVCOM, 0x3F, 0x3F);
	mipi_dsi_generic_write_seq(dsi, ST7703_CMD_UNKNOWN_BF, 0x02, 0x11, 0x00);
	mipi_dsi_generic_write_seq(dsi, ST7703_CMD_SETGIP1,
				   0x82, 0x10, 0x06, 0x05, 0x9E, 0x0A, 0xA5, 0x12,
				   0x31, 0x23, 0x37, 0x83, 0x04, 0xBC, 0x27, 0x38,
				   0x0C, 0x00, 0x03, 0x00, 0x00, 0x00, 0x0C, 0x00,
				   0x03, 0x00, 0x00, 0x00, 0x75, 0x75, 0x31, 0x88,
				   0x88, 0x88, 0x88, 0x88, 0x88, 0x13, 0x88, 0x64,
				   0x64, 0x20, 0x88, 0x88, 0x88, 0x88, 0x88, 0x88,
				   0x02, 0x88, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
				   0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00);
	mipi_dsi_generic_write_seq(dsi, ST7703_CMD_SETGIP2,
				   0x02, 0x21, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
				   0x00, 0x00, 0x00, 0x00, 0x02, 0x46, 0x02, 0x88,
				   0x88, 0x88, 0x88, 0x88, 0x88, 0x64, 0x88, 0x13,
				   0x57, 0x13, 0x88, 0x88, 0x88, 0x88, 0x88, 0x88,
				   0x75, 0x88, 0x23, 0x14, 0x00, 0x00, 0x02, 0x00,
				   0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
				   0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x30, 0x0A,
				   0xA5, 0x00, 0x00, 0x00, 0x00);
	mipi_dsi_generic_write_seq(dsi, ST7703_CMD_SETGAMMA,
				   0x00, 0x09, 0x0E, 0x29, 0x2D, 0x3C, 0x41, 0x37,
				   0x07, 0x0B, 0x0D, 0x10, 0x11, 0x0F, 0x10, 0x11,
				   0x18, 0x00, 0x09, 0x0E, 0x29, 0x2D, 0x3C, 0x41,
				   0x37, 0x07, 0x0B, 0x0D, 0x10, 0x11, 0x0F, 0x10,
				   0x11, 0x18);

	return 0;
}

static const struct drm_display_mode jh057n00900_mode = {
	.hdisplay    = 720,
	.hsync_start = 720 + 90,
	.hsync_end   = 720 + 90 + 20,
	.htotal	     = 720 + 90 + 20 + 20,
	.vdisplay    = 1440,
	.vsync_start = 1440 + 20,
	.vsync_end   = 1440 + 20 + 4,
	.vtotal	     = 1440 + 20 + 4 + 12,
	.clock	     = 75276,
	.flags	     = DRM_MODE_FLAG_NHSYNC | DRM_MODE_FLAG_NVSYNC,
	.width_mm    = 65,
	.height_mm   = 130,
};

static const struct st7703_panel_desc jh057n00900_panel_desc = {
	.mode = &jh057n00900_mode,
	.lanes = 4,
	.mode_flags = MIPI_DSI_MODE_VIDEO |
		MIPI_DSI_MODE_VIDEO_BURST | MIPI_DSI_MODE_VIDEO_SYNC_PULSE,
	.format = MIPI_DSI_FMT_RGB888,
	.init_sequence = jh057n_init_sequence,
};

static int xbd599_init_sequence(struct st7703 *ctx)
{
	struct mipi_dsi_device *dsi = to_mipi_dsi_device(ctx->dev);

	/*
	 * Init sequence was supplied by the panel vendor.
	 */

	/* Magic sequence to unlock user commands below. */
	mipi_dsi_dcs_write_seq(dsi, ST7703_CMD_SETEXTC, 0xF1, 0x12, 0x83);

	mipi_dsi_dcs_write_seq(dsi, ST7703_CMD_SETMIPI,
			       0x33, /* VC_main = 0, Lane_Number = 3 (4 lanes) */
			       0x81, /* DSI_LDO_SEL = 1.7V, RTERM = 90 Ohm */
			       0x05, /* IHSRX = x6 (Low High Speed driving ability) */
			       0xF9, /* TX_CLK_SEL = fDSICLK/16 */
			       0x0E, /* HFP_OSC (min. HFP number in DSI mode) */
			       0x0E, /* HBP_OSC (min. HBP number in DSI mode) */
			       /* The rest is undocumented in ST7703 datasheet */
			       0x20, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
			       0x44, 0x25, 0x00, 0x91, 0x0a, 0x00, 0x00, 0x02,
			       0x4F, 0x11, 0x00, 0x00, 0x37);

	mipi_dsi_dcs_write_seq(dsi, ST7703_CMD_SETPOWER_EXT,
			       0x25, /* PCCS = 2, ECP_DC_DIV = 1/4 HSYNC */
			       0x22, /* DT = 15ms XDK_ECP = x2 */
			       0x20, /* PFM_DC_DIV = /1 */
			       0x03  /* ECP_SYNC_EN = 1, VGX_SYNC_EN = 1 */);

	/* RGB I/F porch timing */
	mipi_dsi_dcs_write_seq(dsi, ST7703_CMD_SETRGBIF,
			       0x10, /* VBP_RGB_GEN */
			       0x10, /* VFP_RGB_GEN */
			       0x05, /* DE_BP_RGB_GEN */
			       0x05, /* DE_FP_RGB_GEN */
			       /* The rest is undocumented in ST7703 datasheet */
			       0x03, 0xFF,
			       0x00, 0x00,
			       0x00, 0x00);

	/* Source driving settings. */
	mipi_dsi_dcs_write_seq(dsi, ST7703_CMD_SETSCR,
			       0x73, /* N_POPON */
			       0x73, /* N_NOPON */
			       0x50, /* I_POPON */
			       0x50, /* I_NOPON */
			       0x00, /* SCR[31,24] */
			       0xC0, /* SCR[23,16] */
			       0x08, /* SCR[15,8] */
			       0x70, /* SCR[7,0] */
			       0x00  /* Undocumented */);

	/* NVDDD_SEL = -1.8V, VDDD_SEL = out of range (possibly 1.9V?) */
	mipi_dsi_dcs_write_seq(dsi, ST7703_CMD_SETVDC, 0x4E);

	/*
	 * SS_PANEL = 1 (reverse scan), GS_PANEL = 0 (normal scan)
	 * REV_PANEL = 1 (normally black panel), BGR_PANEL = 1 (BGR)
	 */
	mipi_dsi_dcs_write_seq(dsi, ST7703_CMD_SETPANEL, 0x0B);

	/* Zig-Zag Type C column inversion. */
	mipi_dsi_dcs_write_seq(dsi, ST7703_CMD_SETCYC, 0x80);

	/* Set display resolution. */
	mipi_dsi_dcs_write_seq(dsi, ST7703_CMD_SETDISP,
			       0xF0, /* NL = 240 */
			       0x12, /* RES_V_LSB = 0, BLK_CON = VSSD,
				      * RESO_SEL = 720RGB
				      */
			       0xF0  /* WHITE_GND_EN = 1 (GND),
				      * WHITE_FRAME_SEL = 7 frames,
				      * ISC = 0 frames
				      */);

	mipi_dsi_dcs_write_seq(dsi, ST7703_CMD_SETEQ,
			       0x00, /* PNOEQ */
			       0x00, /* NNOEQ */
			       0x0B, /* PEQGND */
			       0x0B, /* NEQGND */
			       0x10, /* PEQVCI */
			       0x10, /* NEQVCI */
			       0x00, /* PEQVCI1 */
			       0x00, /* NEQVCI1 */
			       0x00, /* reserved */
			       0x00, /* reserved */
			       0xFF, /* reserved */
			       0x00, /* reserved */
			       0xC0, /* ESD_DET_DATA_WHITE = 1, ESD_WHITE_EN = 1 */
			       0x10  /* SLPIN_OPTION = 1 (no need vsync after sleep-in)
				      * VEDIO_NO_CHECK_EN = 0
				      * ESD_WHITE_GND_EN = 0
				      * ESD_DET_TIME_SEL = 0 frames
				      */);

	mipi_dsi_dcs_write_seq(dsi, ST7703_CMD_SETECO, 0x01, 0x00, 0xFF, 0xFF, 0x00);

	mipi_dsi_dcs_write_seq(dsi, ST7703_CMD_SETPOWER,
			       0x74, /* VBTHS, VBTLS: VGH = 17V, VBL = -11V */
			       0x00, /* FBOFF_VGH = 0, FBOFF_VGL = 0 */
			       0x32, /* VRP  */
			       0x32, /* VRN */
			       0x77, /* reserved */
			       0xF1, /* APS = 1 (small),
				      * VGL_DET_EN = 1, VGH_DET_EN = 1,
				      * VGL_TURBO = 1, VGH_TURBO = 1
				      */
			       0xFF, /* VGH1_L_DIV, VGL1_L_DIV (1.5MHz) */
			       0xFF, /* VGH1_R_DIV, VGL1_R_DIV (1.5MHz) */
			       0xCC, /* VGH2_L_DIV, VGL2_L_DIV (2.6MHz) */
			       0xCC, /* VGH2_R_DIV, VGL2_R_DIV (2.6MHz) */
			       0x77, /* VGH3_L_DIV, VGL3_L_DIV (4.5MHz) */
			       0x77  /* VGH3_R_DIV, VGL3_R_DIV (4.5MHz) */);

	/* Reference voltage. */
	mipi_dsi_dcs_write_seq(dsi, ST7703_CMD_SETBGP,
			       0x07, /* VREF_SEL = 4.2V */
			       0x07  /* NVREF_SEL = 4.2V */);
	msleep(20);

	mipi_dsi_dcs_write_seq(dsi, ST7703_CMD_SETVCOM,
			       0x2C, /* VCOMDC_F = -0.67V */
			       0x2C  /* VCOMDC_B = -0.67V */);

	/* Undocumented command. */
	mipi_dsi_dcs_write_seq(dsi, ST7703_CMD_UNKNOWN_BF, 0x02, 0x11, 0x00);

	/* This command is to set forward GIP timing. */
	mipi_dsi_dcs_write_seq(dsi, ST7703_CMD_SETGIP1,
			       0x82, 0x10, 0x06, 0x05, 0xA2, 0x0A, 0xA5, 0x12,
			       0x31, 0x23, 0x37, 0x83, 0x04, 0xBC, 0x27, 0x38,
			       0x0C, 0x00, 0x03, 0x00, 0x00, 0x00, 0x0C, 0x00,
			       0x03, 0x00, 0x00, 0x00, 0x75, 0x75, 0x31, 0x88,
			       0x88, 0x88, 0x88, 0x88, 0x88, 0x13, 0x88, 0x64,
			       0x64, 0x20, 0x88, 0x88, 0x88, 0x88, 0x88, 0x88,
			       0x02, 0x88, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
			       0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00);

	/* This command is to set backward GIP timing. */
	mipi_dsi_dcs_write_seq(dsi, ST7703_CMD_SETGIP2,
			       0x02, 0x21, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
			       0x00, 0x00, 0x00, 0x00, 0x02, 0x46, 0x02, 0x88,
			       0x88, 0x88, 0x88, 0x88, 0x88, 0x64, 0x88, 0x13,
			       0x57, 0x13, 0x88, 0x88, 0x88, 0x88, 0x88, 0x88,
			       0x75, 0x88, 0x23, 0x14, 0x00, 0x00, 0x02, 0x00,
			       0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
			       0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x03, 0x0A,
			       0xA5, 0x00, 0x00, 0x00, 0x00);

	/* Adjust the gamma characteristics of the panel. */
	mipi_dsi_dcs_write_seq(dsi, ST7703_CMD_SETGAMMA,
			       0x00, 0x09, 0x0D, 0x23, 0x27, 0x3C, 0x41, 0x35,
			       0x07, 0x0D, 0x0E, 0x12, 0x13, 0x10, 0x12, 0x12,
			       0x18, 0x00, 0x09, 0x0D, 0x23, 0x27, 0x3C, 0x41,
			       0x35, 0x07, 0x0D, 0x0E, 0x12, 0x13, 0x10, 0x12,
			       0x12, 0x18);

	return 0;
}

static const struct drm_display_mode xbd599_mode = {
	.hdisplay    = 720,
	.hsync_start = 720 + 40,
	.hsync_end   = 720 + 40 + 40,
	.htotal	     = 720 + 40 + 40 + 40,
	.vdisplay    = 1440,
	.vsync_start = 1440 + 18,
	.vsync_end   = 1440 + 18 + 10,
	.vtotal	     = 1440 + 18 + 10 + 17,
	.clock	     = 69000,
	.flags	     = DRM_MODE_FLAG_NHSYNC | DRM_MODE_FLAG_NVSYNC,
	.width_mm    = 68,
	.height_mm   = 136,
};

static const struct st7703_panel_desc xbd599_desc = {
	.mode = &xbd599_mode,
	.lanes = 4,
	.mode_flags = MIPI_DSI_MODE_VIDEO | MIPI_DSI_MODE_VIDEO_SYNC_PULSE,
	.format = MIPI_DSI_FMT_RGB888,
	.init_sequence = xbd599_init_sequence,
};

static int rg353v2_init_sequence(struct st7703 *ctx)
{
	struct mipi_dsi_device *dsi = to_mipi_dsi_device(ctx->dev);

	/*
	 * Init sequence was supplied by the panel vendor.
	 */

	mipi_dsi_dcs_write_seq(dsi, ST7703_CMD_SETEXTC, 0xf1, 0x12, 0x83);
	mipi_dsi_dcs_write_seq(dsi, ST7703_CMD_SETAPID, 0x00, 0x00, 0x00,
			       0xda, 0x80);
	mipi_dsi_dcs_write_seq(dsi, ST7703_CMD_SETDISP, 0x00, 0x13, 0x70);
	mipi_dsi_dcs_write_seq(dsi, ST7703_CMD_SETRGBIF, 0x10, 0x10, 0x28,
			       0x28, 0x03, 0xff, 0x00, 0x00, 0x00, 0x00);
	mipi_dsi_dcs_write_seq(dsi, ST7703_CMD_SETCYC, 0x80);
	mipi_dsi_dcs_write_seq(dsi, ST7703_CMD_SETBGP, 0x0a, 0x0a);
	mipi_dsi_dcs_write_seq(dsi, ST7703_CMD_SETVCOM, 0x92, 0x92);
	mipi_dsi_dcs_write_seq(dsi, ST7703_CMD_SETPOWER_EXT, 0x25, 0x22,
			       0xf0, 0x63);
	mipi_dsi_dcs_write_seq(dsi, ST7703_CMD_SETMIPI, 0x33, 0x81, 0x05,
			       0xf9, 0x0e, 0x0e, 0x20, 0x00, 0x00, 0x00, 0x00,
			       0x00, 0x00, 0x00, 0x44, 0x25, 0x00, 0x90, 0x0a,
			       0x00, 0x00, 0x01, 0x4f, 0x01, 0x00, 0x00, 0x37);
	mipi_dsi_dcs_write_seq(dsi, ST7703_CMD_SETVDC, 0x47);
	mipi_dsi_dcs_write_seq(dsi, ST7703_CMD_UNKNOWN_BF, 0x02, 0x11, 0x00);
	mipi_dsi_dcs_write_seq(dsi, ST7703_CMD_SETSCR, 0x73, 0x73, 0x50, 0x50,
			       0x00, 0x00, 0x12, 0x50, 0x00);
	mipi_dsi_dcs_write_seq(dsi, ST7703_CMD_SETPOWER, 0x53, 0xc0, 0x32,
			       0x32, 0x77, 0xe1, 0xdd, 0xdd, 0x77, 0x77, 0x33,
			       0x33);
	mipi_dsi_dcs_write_seq(dsi, ST7703_CMD_SETECO, 0x82, 0x00, 0xbf, 0xff,
			       0x00, 0xff);
	mipi_dsi_dcs_write_seq(dsi, ST7703_CMD_SETIO, 0xb8, 0x00, 0x0a, 0x00,
			       0x00, 0x00);
	mipi_dsi_dcs_write_seq(dsi, ST7703_CMD_SETCABC, 0x10, 0x40, 0x1e,
			       0x02);
	mipi_dsi_dcs_write_seq(dsi, ST7703_CMD_SETPANEL, 0x0b);
	mipi_dsi_dcs_write_seq(dsi, ST7703_CMD_SETGAMMA, 0x00, 0x07, 0x0d,
			       0x37, 0x35, 0x3f, 0x41, 0x44, 0x06, 0x0c, 0x0d,
			       0x0f, 0x11, 0x10, 0x12, 0x14, 0x1a, 0x00, 0x07,
			       0x0d, 0x37, 0x35, 0x3f, 0x41, 0x44, 0x06, 0x0c,
			       0x0d, 0x0f, 0x11, 0x10, 0x12, 0x14, 0x1a);
	mipi_dsi_dcs_write_seq(dsi, ST7703_CMD_SETEQ, 0x07, 0x07, 0x0b, 0x0b,
			       0x0b, 0x0b, 0x00, 0x00, 0x00, 0x00, 0xff, 0x00,
			       0xc0, 0x10);
	mipi_dsi_dcs_write_seq(dsi, ST7703_CMD_SETGIP1, 0xc8, 0x10, 0x02, 0x00,
			       0x00, 0xb0, 0xb1, 0x11, 0x31, 0x23, 0x28, 0x80,
			       0xb0, 0xb1, 0x27, 0x08, 0x00, 0x04, 0x02, 0x00,
			       0x00, 0x00, 0x00, 0x04, 0x02, 0x00, 0x00, 0x00,
			       0x88, 0x88, 0xba, 0x60, 0x24, 0x08, 0x88, 0x88,
			       0x88, 0x88, 0x88, 0x88, 0x88, 0xba, 0x71, 0x35,
			       0x18, 0x88, 0x88, 0x88, 0x88, 0x88, 0x00, 0x00,
			       0x00, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
			       0x00, 0x00, 0x00);
	mipi_dsi_dcs_write_seq(dsi, ST7703_CMD_SETGIP2, 0x97, 0x0a, 0x82, 0x02,
			       0x03, 0x07, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
			       0x81, 0x88, 0xba, 0x17, 0x53, 0x88, 0x88, 0x88,
			       0x88, 0x88, 0x88, 0x80, 0x88, 0xba, 0x06, 0x42,
			       0x88, 0x88, 0x88, 0x88, 0x88, 0x88, 0x23, 0x00,
			       0x00, 0x02, 0x80, 0x00, 0x00, 0x00, 0x00, 0x00,
			       0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
			       0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
			       0x00);
	mipi_dsi_dcs_write_seq(dsi, ST7703_CMD_UNKNOWN_EF, 0xff, 0xff, 0x01);

	return 0;
}

static const struct drm_display_mode rg353v2_mode = {
	.hdisplay	= 640,
	.hsync_start	= 640 + 40,
	.hsync_end	= 640 + 40 + 2,
	.htotal		= 640 + 40 + 2 + 80,
	.vdisplay	= 480,
	.vsync_start	= 480 + 18,
	.vsync_end	= 480 + 18 + 2,
	.vtotal		= 480 + 18 + 2 + 28,
	.clock		= 24150,
	.flags		= DRM_MODE_FLAG_NHSYNC | DRM_MODE_FLAG_NVSYNC,
	.width_mm	= 70,
	.height_mm	= 57,
};

static const struct st7703_panel_desc rg353v2_desc = {
	.mode = &rg353v2_mode,
	.lanes = 4,
	.mode_flags = MIPI_DSI_MODE_VIDEO | MIPI_DSI_MODE_VIDEO_BURST |
		      MIPI_DSI_MODE_NO_EOT_PACKET | MIPI_DSI_MODE_LPM,
	.format = MIPI_DSI_FMT_RGB888,
	.init_sequence = rg353v2_init_sequence,
};

static int st7703_enable(struct drm_panel *panel)
{
	struct st7703 *ctx = panel_to_st7703(panel);
	struct mipi_dsi_device *dsi = to_mipi_dsi_device(ctx->dev);
	int ret;

	ret = ctx->desc->init_sequence(ctx);
	if (ret < 0) {
		dev_err(ctx->dev, "Panel init sequence failed: %d\n", ret);
		return ret;
	}

	msleep(20);

	ret = mipi_dsi_dcs_exit_sleep_mode(dsi);
	if (ret < 0) {
		dev_err(ctx->dev, "Failed to exit sleep mode: %d\n", ret);
		return ret;
	}

	/* Panel is operational 120 msec after reset */
	msleep(60);

	ret = mipi_dsi_dcs_set_display_on(dsi);
	if (ret)
		return ret;

	dev_dbg(ctx->dev, "Panel init sequence done\n");

	return 0;
}

static int st7703_disable(struct drm_panel *panel)
{
	struct st7703 *ctx = panel_to_st7703(panel);
	struct mipi_dsi_device *dsi = to_mipi_dsi_device(ctx->dev);
	int ret;

	ret = mipi_dsi_dcs_set_display_off(dsi);
	if (ret < 0)
		dev_err(ctx->dev, "Failed to turn off the display: %d\n", ret);

	ret = mipi_dsi_dcs_enter_sleep_mode(dsi);
	if (ret < 0)
		dev_err(ctx->dev, "Failed to enter sleep mode: %d\n", ret);

	return 0;
}

static int st7703_unprepare(struct drm_panel *panel)
{
	struct st7703 *ctx = panel_to_st7703(panel);

	if (!ctx->prepared)
		return 0;

	gpiod_set_value_cansleep(ctx->reset_gpio, 1);
	regulator_disable(ctx->iovcc);
	regulator_disable(ctx->vcc);
	ctx->prepared = false;

	return 0;
}

static int st7703_prepare(struct drm_panel *panel)
{
	struct st7703 *ctx = panel_to_st7703(panel);
	int ret;

	if (ctx->prepared)
		return 0;

	dev_dbg(ctx->dev, "Resetting the panel\n");
	ret = regulator_enable(ctx->vcc);
	if (ret < 0) {
		dev_err(ctx->dev, "Failed to enable vcc supply: %d\n", ret);
		return ret;
	}
	ret = regulator_enable(ctx->iovcc);
	if (ret < 0) {
		dev_err(ctx->dev, "Failed to enable iovcc supply: %d\n", ret);
		goto disable_vcc;
	}

	gpiod_set_value_cansleep(ctx->reset_gpio, 1);
	usleep_range(20, 40);
	gpiod_set_value_cansleep(ctx->reset_gpio, 0);
	msleep(20);

	ctx->prepared = true;

	return 0;

disable_vcc:
	regulator_disable(ctx->vcc);
	return ret;
}

static const u32 mantix_bus_formats[] = {
	MEDIA_BUS_FMT_RGB888_1X24,
};

static int st7703_get_modes(struct drm_panel *panel,
			    struct drm_connector *connector)
{
	struct st7703 *ctx = panel_to_st7703(panel);
	struct drm_display_mode *mode;

	mode = drm_mode_duplicate(connector->dev, ctx->desc->mode);
	if (!mode) {
		dev_err(ctx->dev, "Failed to add mode %ux%u@%u\n",
			ctx->desc->mode->hdisplay, ctx->desc->mode->vdisplay,
			drm_mode_vrefresh(ctx->desc->mode));
		return -ENOMEM;
	}

	drm_mode_set_name(mode);

	mode->type = DRM_MODE_TYPE_DRIVER | DRM_MODE_TYPE_PREFERRED;
	connector->display_info.width_mm = mode->width_mm;
	connector->display_info.height_mm = mode->height_mm;
	drm_mode_probed_add(connector, mode);

	drm_display_info_set_bus_formats(&connector->display_info,
					 mantix_bus_formats,
					 ARRAY_SIZE(mantix_bus_formats));

	return 1;
}

static const struct drm_panel_funcs st7703_drm_funcs = {
	.disable   = st7703_disable,
	.unprepare = st7703_unprepare,
	.prepare   = st7703_prepare,
	.enable	   = st7703_enable,
	.get_modes = st7703_get_modes,
};

static int allpixelson_set(void *data, u64 val)
{
	struct st7703 *ctx = data;
	struct mipi_dsi_device *dsi = to_mipi_dsi_device(ctx->dev);

	dev_dbg(ctx->dev, "Setting all pixels on\n");
	mipi_dsi_generic_write_seq(dsi, ST7703_CMD_ALL_PIXEL_ON);
	msleep(val * 1000);
	/* Reset the panel to get video back */
	drm_panel_disable(&ctx->panel);
	drm_panel_unprepare(&ctx->panel);
	drm_panel_prepare(&ctx->panel);
	drm_panel_enable(&ctx->panel);

	return 0;
}

DEFINE_SIMPLE_ATTRIBUTE(allpixelson_fops, NULL,
			allpixelson_set, "%llu\n");

static void st7703_debugfs_init(struct st7703 *ctx)
{
	ctx->debugfs = debugfs_create_dir(DRV_NAME, NULL);

	debugfs_create_file("allpixelson", 0600, ctx->debugfs, ctx,
			    &allpixelson_fops);
}

static void st7703_debugfs_remove(struct st7703 *ctx)
{
	debugfs_remove_recursive(ctx->debugfs);
	ctx->debugfs = NULL;
}

static int st7703_probe(struct mipi_dsi_device *dsi)
{
	struct device *dev = &dsi->dev;
	struct st7703 *ctx;
	int ret;

	ctx = devm_kzalloc(dev, sizeof(*ctx), GFP_KERNEL);
	if (!ctx)
		return -ENOMEM;

	ctx->reset_gpio = devm_gpiod_get(dev, "reset", GPIOD_OUT_LOW);
	if (IS_ERR(ctx->reset_gpio))
		return dev_err_probe(dev, PTR_ERR(ctx->reset_gpio), "Failed to get reset gpio\n");

	mipi_dsi_set_drvdata(dsi, ctx);

	ctx->dev = dev;
	ctx->desc = of_device_get_match_data(dev);

	dsi->mode_flags = ctx->desc->mode_flags;
	dsi->format = ctx->desc->format;
	dsi->lanes = ctx->desc->lanes;

	ctx->vcc = devm_regulator_get(dev, "vcc");
	if (IS_ERR(ctx->vcc))
		return dev_err_probe(dev, PTR_ERR(ctx->vcc), "Failed to request vcc regulator\n");

	ctx->iovcc = devm_regulator_get(dev, "iovcc");
	if (IS_ERR(ctx->iovcc))
		return dev_err_probe(dev, PTR_ERR(ctx->iovcc),
				     "Failed to request iovcc regulator\n");

	drm_panel_init(&ctx->panel, dev, &st7703_drm_funcs,
		       DRM_MODE_CONNECTOR_DSI);

	ret = drm_panel_of_backlight(&ctx->panel);
	if (ret)
		return ret;

	drm_panel_add(&ctx->panel);

	ret = mipi_dsi_attach(dsi);
	if (ret < 0) {
		dev_err(dev, "mipi_dsi_attach failed (%d). Is host ready?\n", ret);
		drm_panel_remove(&ctx->panel);
		return ret;
	}

	dev_info(dev, "%ux%u@%u %ubpp dsi %udl - ready\n",
		 ctx->desc->mode->hdisplay, ctx->desc->mode->vdisplay,
		 drm_mode_vrefresh(ctx->desc->mode),
		 mipi_dsi_pixel_format_to_bpp(dsi->format), dsi->lanes);

	st7703_debugfs_init(ctx);
	return 0;
}

static void st7703_shutdown(struct mipi_dsi_device *dsi)
{
	struct st7703 *ctx = mipi_dsi_get_drvdata(dsi);
	int ret;

	ret = drm_panel_unprepare(&ctx->panel);
	if (ret < 0)
		dev_err(&dsi->dev, "Failed to unprepare panel: %d\n", ret);

	ret = drm_panel_disable(&ctx->panel);
	if (ret < 0)
		dev_err(&dsi->dev, "Failed to disable panel: %d\n", ret);
}

static void st7703_remove(struct mipi_dsi_device *dsi)
{
	struct st7703 *ctx = mipi_dsi_get_drvdata(dsi);
	int ret;

	st7703_shutdown(dsi);

	ret = mipi_dsi_detach(dsi);
	if (ret < 0)
		dev_err(&dsi->dev, "Failed to detach from DSI host: %d\n", ret);

	drm_panel_remove(&ctx->panel);

	st7703_debugfs_remove(ctx);
}

static const struct of_device_id st7703_of_match[] = {
	{ .compatible = "anbernic,rg353v-panel-v2", .data = &rg353v2_desc },
	{ .compatible = "rocktech,jh057n00900", .data = &jh057n00900_panel_desc },
	{ .compatible = "xingbangda,xbd599", .data = &xbd599_desc },
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, st7703_of_match);

static struct mipi_dsi_driver st7703_driver = {
	.probe	= st7703_probe,
	.remove = st7703_remove,
	.shutdown = st7703_shutdown,
	.driver = {
		.name = DRV_NAME,
		.of_match_table = st7703_of_match,
	},
};
module_mipi_dsi_driver(st7703_driver);

MODULE_AUTHOR("Guido Günther <agx@sigxcpu.org>");
MODULE_DESCRIPTION("DRM driver for Sitronix ST7703 based MIPI DSI panels");
MODULE_LICENSE("GPL v2");
