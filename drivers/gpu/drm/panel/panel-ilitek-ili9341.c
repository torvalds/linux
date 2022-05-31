// SPDX-License-Identifier: GPL-2.0-only
/*
 * Ilitek ILI9341 TFT LCD drm_panel driver.
 *
 * This panel can be configured to support:
 * - 16-bit parallel RGB interface
 * - 18-bit parallel RGB interface
 * - 4-line serial spi interface
 *
 * Copyright (C) 2021 Dillon Min <dillon.minfei@gmail.com>
 *
 * For dbi+dpi part:
 * Derived from drivers/drm/gpu/panel/panel-ilitek-ili9322.c
 * the reuse of DBI abstraction part referred from Linus's patch
 * "drm/panel: s6e63m0: Switch to DBI abstraction for SPI"
 *
 * For only-dbi part, copy from David's code (drm/tiny/ili9341.c)
 * Copyright 2018 David Lechner <david@lechnology.com>
 */

#include <linux/bitops.h>
#include <linux/delay.h>
#include <linux/gpio/consumer.h>
#include <linux/module.h>
#include <linux/of_device.h>
#include <linux/regulator/consumer.h>
#include <linux/spi/spi.h>

#include <video/mipi_display.h>

#include <drm/drm_atomic_helper.h>
#include <drm/drm_drv.h>
#include <drm/drm_fb_helper.h>
#include <drm/drm_gem_atomic_helper.h>
#include <drm/drm_gem_cma_helper.h>
#include <drm/drm_gem_framebuffer_helper.h>
#include <drm/drm_mipi_dbi.h>
#include <drm/drm_modes.h>
#include <drm/drm_panel.h>
#include <drm/drm_print.h>

#define ILI9341_RGB_INTERFACE  0xb0   /* RGB Interface Signal Control */
#define ILI9341_FRC            0xb1   /* Frame Rate Control register */
#define ILI9341_DFC            0xb6   /* Display Function Control register */
#define ILI9341_POWER1         0xc0   /* Power Control 1 register */
#define ILI9341_POWER2         0xc1   /* Power Control 2 register */
#define ILI9341_VCOM1          0xc5   /* VCOM Control 1 register */
#define ILI9341_VCOM2          0xc7   /* VCOM Control 2 register */
#define ILI9341_POWERA         0xcb   /* Power control A register */
#define ILI9341_POWERB         0xcf   /* Power control B register */
#define ILI9341_PGAMMA         0xe0   /* Positive Gamma Correction register */
#define ILI9341_NGAMMA         0xe1   /* Negative Gamma Correction register */
#define ILI9341_DTCA           0xe8   /* Driver timing control A */
#define ILI9341_DTCB           0xea   /* Driver timing control B */
#define ILI9341_POWER_SEQ      0xed   /* Power on sequence register */
#define ILI9341_3GAMMA_EN      0xf2   /* 3 Gamma enable register */
#define ILI9341_INTERFACE      0xf6   /* Interface control register */
#define ILI9341_PRC            0xf7   /* Pump ratio control register */
#define ILI9341_ETMOD	       0xb7   /* Entry mode set */

#define ILI9341_MADCTL_BGR	BIT(3)
#define ILI9341_MADCTL_MV	BIT(5)
#define ILI9341_MADCTL_MX	BIT(6)
#define ILI9341_MADCTL_MY	BIT(7)

#define ILI9341_POWER_B_LEN	3
#define ILI9341_POWER_SEQ_LEN	4
#define ILI9341_DTCA_LEN	3
#define ILI9341_DTCB_LEN	2
#define ILI9341_POWER_A_LEN	5
#define ILI9341_DFC_1_LEN	2
#define ILI9341_FRC_LEN		2
#define ILI9341_VCOM_1_LEN	2
#define ILI9341_DFC_2_LEN	4
#define ILI9341_COLUMN_ADDR_LEN	4
#define ILI9341_PAGE_ADDR_LEN	4
#define ILI9341_INTERFACE_LEN	3
#define ILI9341_PGAMMA_LEN	15
#define ILI9341_NGAMMA_LEN	15
#define ILI9341_CA_LEN		3

#define ILI9341_PIXEL_DPI_16_BITS	(BIT(6) | BIT(4))
#define ILI9341_PIXEL_DPI_18_BITS	(BIT(6) | BIT(5))
#define ILI9341_GAMMA_CURVE_1		BIT(0)
#define ILI9341_IF_WE_MODE		BIT(0)
#define ILI9341_IF_BIG_ENDIAN		0x00
#define ILI9341_IF_DM_RGB		BIT(2)
#define ILI9341_IF_DM_INTERNAL		0x00
#define ILI9341_IF_DM_VSYNC		BIT(3)
#define ILI9341_IF_RM_RGB		BIT(1)
#define ILI9341_IF_RIM_RGB		0x00

#define ILI9341_COLUMN_ADDR		0x00ef
#define ILI9341_PAGE_ADDR		0x013f

#define ILI9341_RGB_EPL			BIT(0)
#define ILI9341_RGB_DPL			BIT(1)
#define ILI9341_RGB_HSPL		BIT(2)
#define ILI9341_RGB_VSPL		BIT(3)
#define ILI9341_RGB_DE_MODE		BIT(6)
#define ILI9341_RGB_DISP_PATH_MEM	BIT(7)

#define ILI9341_DBI_VCOMH_4P6V		0x23
#define ILI9341_DBI_PWR_2_DEFAULT	0x10
#define ILI9341_DBI_PRC_NORMAL		0x20
#define ILI9341_DBI_VCOM_1_VMH_4P25V	0x3e
#define ILI9341_DBI_VCOM_1_VML_1P5V	0x28
#define ILI9341_DBI_VCOM_2_DEC_58	0x86
#define ILI9341_DBI_FRC_DIVA		0x00
#define ILI9341_DBI_FRC_RTNA		0x1b
#define ILI9341_DBI_EMS_GAS		BIT(0)
#define ILI9341_DBI_EMS_DTS		BIT(1)
#define ILI9341_DBI_EMS_GON		BIT(2)

/* struct ili9341_config - the system specific ILI9341 configuration */
struct ili9341_config {
	u32 max_spi_speed;
	/* mode: the drm display mode */
	const struct drm_display_mode mode;
	/* ca: TODO: need comments for this register */
	u8 ca[ILI9341_CA_LEN];
	/* power_b: TODO: need comments for this register */
	u8 power_b[ILI9341_POWER_B_LEN];
	/* power_seq: TODO: need comments for this register */
	u8 power_seq[ILI9341_POWER_SEQ_LEN];
	/* dtca: TODO: need comments for this register */
	u8 dtca[ILI9341_DTCA_LEN];
	/* dtcb: TODO: need comments for this register */
	u8 dtcb[ILI9341_DTCB_LEN];
	/* power_a: TODO: need comments for this register */
	u8 power_a[ILI9341_POWER_A_LEN];
	/* frc: Frame Rate Control (In Normal Mode/Full Colors) (B1h) */
	u8 frc[ILI9341_FRC_LEN];
	/* prc: TODO: need comments for this register */
	u8 prc;
	/* dfc_1: B6h DISCTRL (Display Function Control) */
	u8 dfc_1[ILI9341_DFC_1_LEN];
	/* power_1: Power Control 1 (C0h) */
	u8 power_1;
	/* power_2: Power Control 2 (C1h) */
	u8 power_2;
	/* vcom_1: VCOM Control 1(C5h) */
	u8 vcom_1[ILI9341_VCOM_1_LEN];
	/* vcom_2: VCOM Control 2(C7h) */
	u8 vcom_2;
	/* address_mode: Memory Access Control (36h) */
	u8 address_mode;
	/* g3amma_en: TODO: need comments for this register */
	u8 g3amma_en;
	/* rgb_interface: RGB Interface Signal Control (B0h) */
	u8 rgb_interface;
	/* dfc_2: refer to dfc_1 */
	u8 dfc_2[ILI9341_DFC_2_LEN];
	/* column_addr: Column Address Set (2Ah) */
	u8 column_addr[ILI9341_COLUMN_ADDR_LEN];
	/* page_addr: Page Address Set (2Bh) */
	u8 page_addr[ILI9341_PAGE_ADDR_LEN];
	/* interface: Interface Control (F6h) */
	u8 interface[ILI9341_INTERFACE_LEN];
	/*
	 * pixel_format: This command sets the pixel format for the RGB
	 * image data used by
	 */
	u8 pixel_format;
	/*
	 * gamma_curve: This command is used to select the desired Gamma
	 * curve for the
	 */
	u8 gamma_curve;
	/* pgamma: Positive Gamma Correction (E0h) */
	u8 pgamma[ILI9341_PGAMMA_LEN];
	/* ngamma: Negative Gamma Correction (E1h) */
	u8 ngamma[ILI9341_NGAMMA_LEN];
};

struct ili9341 {
	struct device *dev;
	const struct ili9341_config *conf;
	struct drm_panel panel;
	struct gpio_desc *reset_gpio;
	struct gpio_desc *dc_gpio;
	struct mipi_dbi *dbi;
	u32 max_spi_speed;
	struct regulator_bulk_data supplies[3];
};

/*
 * The Stm32f429-disco board has a panel ili9341 connected to ltdc controller
 */
static const struct ili9341_config ili9341_stm32f429_disco_data = {
	.max_spi_speed = 10000000,
	.mode = {
		.clock = 6100,
		.hdisplay = 240,
		.hsync_start = 240 + 10,/* hfp 10 */
		.hsync_end = 240 + 10 + 10,/* hsync 10 */
		.htotal = 240 + 10 + 10 + 20,/* hbp 20 */
		.vdisplay = 320,
		.vsync_start = 320 + 4,/* vfp 4 */
		.vsync_end = 320 + 4 + 2,/* vsync 2 */
		.vtotal = 320 + 4 + 2 + 2,/* vbp 2 */
		.flags = 0,
		.width_mm = 65,
		.height_mm = 50,
		.type = DRM_MODE_TYPE_DRIVER | DRM_MODE_TYPE_PREFERRED,
	},
	.ca = {0xc3, 0x08, 0x50},
	.power_b = {0x00, 0xc1, 0x30},
	.power_seq = {0x64, 0x03, 0x12, 0x81},
	.dtca = {0x85, 0x00, 0x78},
	.power_a = {0x39, 0x2c, 0x00, 0x34, 0x02},
	.prc = 0x20,
	.dtcb = {0x00, 0x00},
	/* 0x00 fosc, 0x1b 70hz */
	.frc = {0x00, 0x1b},
	/*
	 * 0x0a Interval scan, AGND AGND AGND AGND
	 * 0xa2 Normally white, G1 -> G320, S720 -> S1,
	 *	Scan Cycle 5 frames,85ms
	 */
	.dfc_1 = {0x0a, 0xa2},
	/* 0x10 3.65v */
	.power_1 = 0x10,
	/* 0x10 AVDD=vci*2, VGH=vci*7, VGL=-vci*4 */
	.power_2 = 0x10,
	/* 0x45 VCOMH 4.425v, 0x15 VCOML -1.975*/
	.vcom_1 = {0x45, 0x15},
	/* 0x90 offset voltage, VMH-48, VML-48 */
	.vcom_2 = 0x90,
	/*
	 * 0xc8 Row Address Order, Column Address Order
	 * BGR 1
	 */
	.address_mode = 0xc8,
	.g3amma_en = 0x00,
	/*
	 * 0xc2
	 * Display Data Path: Memory
	 * RGB: DE mode
	 * DOTCLK polarity set (data fetched at the falling time)
	 */
	.rgb_interface = ILI9341_RGB_DISP_PATH_MEM |
			ILI9341_RGB_DE_MODE |
			ILI9341_RGB_DPL,
	/*
	 * 0x0a
	 * Gate outputs in non-display area: Interval scan
	 * Determine source/VCOM output in a non-display area in the partial
	 * display mode: AGND AGND AGND AGND
	 *
	 * 0xa7
	 * Scan Cycle: 15 frames
	 * fFLM = 60Hz: 255ms
	 * Liquid crystal type: Normally white
	 * Gate Output Scan Direction: G1 -> G320
	 * Source Output Scan Direction: S720 -> S1
	 *
	 * 0x27
	 * LCD Driver Line: 320 lines
	 *
	 * 0x04
	 * PCDIV: 4
	 */
	.dfc_2 = {0x0a, 0xa7, 0x27, 0x04},
	/* column address: 240 */
	.column_addr = {0x00, 0x00, (ILI9341_COLUMN_ADDR >> 4) & 0xff,
				ILI9341_COLUMN_ADDR & 0xff},
	/* page address: 320 */
	.page_addr = {0x00, 0x00, (ILI9341_PAGE_ADDR >> 4) & 0xff,
				ILI9341_PAGE_ADDR & 0xff},
	/*
	 * Memory write control: When the transfer number of data exceeds
	 * (EC-SC+1)*(EP-SP+1), the column and page number will be
	 * reset, and the exceeding data will be written into the following
	 * column and page.
	 * Display Operation Mode: RGB Interface Mode
	 * Interface for RAM Access: RGB interface
	 * 16- bit RGB interface (1 transfer/pixel)
	 */
	.interface = {ILI9341_IF_WE_MODE, 0x00,
			ILI9341_IF_DM_RGB | ILI9341_IF_RM_RGB},
	/* DPI: 16 bits / pixel */
	.pixel_format = ILI9341_PIXEL_DPI_16_BITS,
	/* Curve Selected: Gamma curve 1 (G2.2) */
	.gamma_curve = ILI9341_GAMMA_CURVE_1,
	.pgamma = {0x0f, 0x29, 0x24, 0x0c, 0x0e,
			0x09, 0x4e, 0x78, 0x3c, 0x09,
			0x13, 0x05, 0x17, 0x11, 0x00},
	.ngamma = {0x00, 0x16, 0x1b, 0x04, 0x11,
			0x07, 0x31, 0x33, 0x42, 0x05,
			0x0c, 0x0a, 0x28, 0x2f, 0x0f},
};

static inline struct ili9341 *panel_to_ili9341(struct drm_panel *panel)
{
	return container_of(panel, struct ili9341, panel);
}

static void ili9341_dpi_init(struct ili9341 *ili)
{
	struct device *dev = (&ili->panel)->dev;
	struct mipi_dbi *dbi = ili->dbi;
	struct ili9341_config *cfg = (struct ili9341_config *)ili->conf;

	/* Power Control */
	mipi_dbi_command_stackbuf(dbi, 0xca, cfg->ca, ILI9341_CA_LEN);
	mipi_dbi_command_stackbuf(dbi, ILI9341_POWERB, cfg->power_b,
				  ILI9341_POWER_B_LEN);
	mipi_dbi_command_stackbuf(dbi, ILI9341_POWER_SEQ, cfg->power_seq,
				  ILI9341_POWER_SEQ_LEN);
	mipi_dbi_command_stackbuf(dbi, ILI9341_DTCA, cfg->dtca,
				  ILI9341_DTCA_LEN);
	mipi_dbi_command_stackbuf(dbi, ILI9341_POWERA, cfg->power_a,
				  ILI9341_POWER_A_LEN);
	mipi_dbi_command(ili->dbi, ILI9341_PRC, cfg->prc);
	mipi_dbi_command_stackbuf(dbi, ILI9341_DTCB, cfg->dtcb,
				  ILI9341_DTCB_LEN);
	mipi_dbi_command_stackbuf(dbi, ILI9341_FRC, cfg->frc, ILI9341_FRC_LEN);
	mipi_dbi_command_stackbuf(dbi, ILI9341_DFC, cfg->dfc_1,
				  ILI9341_DFC_1_LEN);
	mipi_dbi_command(dbi, ILI9341_POWER1, cfg->power_1);
	mipi_dbi_command(dbi, ILI9341_POWER2, cfg->power_2);

	/* VCOM */
	mipi_dbi_command_stackbuf(dbi, ILI9341_VCOM1, cfg->vcom_1,
				  ILI9341_VCOM_1_LEN);
	mipi_dbi_command(dbi, ILI9341_VCOM2, cfg->vcom_2);
	mipi_dbi_command(dbi, MIPI_DCS_SET_ADDRESS_MODE, cfg->address_mode);

	/* Gamma */
	mipi_dbi_command(dbi, ILI9341_3GAMMA_EN, cfg->g3amma_en);
	mipi_dbi_command(dbi, ILI9341_RGB_INTERFACE, cfg->rgb_interface);
	mipi_dbi_command_stackbuf(dbi, ILI9341_DFC, cfg->dfc_2,
				  ILI9341_DFC_2_LEN);

	/* Colomn address set */
	mipi_dbi_command_stackbuf(dbi, MIPI_DCS_SET_COLUMN_ADDRESS,
				  cfg->column_addr, ILI9341_COLUMN_ADDR_LEN);

	/* Page address set */
	mipi_dbi_command_stackbuf(dbi, MIPI_DCS_SET_PAGE_ADDRESS,
				  cfg->page_addr, ILI9341_PAGE_ADDR_LEN);
	mipi_dbi_command_stackbuf(dbi, ILI9341_INTERFACE, cfg->interface,
				  ILI9341_INTERFACE_LEN);

	/* Format */
	mipi_dbi_command(dbi, MIPI_DCS_SET_PIXEL_FORMAT, cfg->pixel_format);
	mipi_dbi_command(dbi, MIPI_DCS_WRITE_MEMORY_START);
	msleep(200);
	mipi_dbi_command(dbi, MIPI_DCS_SET_GAMMA_CURVE, cfg->gamma_curve);
	mipi_dbi_command_stackbuf(dbi, ILI9341_PGAMMA, cfg->pgamma,
				  ILI9341_PGAMMA_LEN);
	mipi_dbi_command_stackbuf(dbi, ILI9341_NGAMMA, cfg->ngamma,
				  ILI9341_NGAMMA_LEN);
	mipi_dbi_command(dbi, MIPI_DCS_EXIT_SLEEP_MODE);
	msleep(200);
	mipi_dbi_command(dbi, MIPI_DCS_SET_DISPLAY_ON);
	mipi_dbi_command(dbi, MIPI_DCS_WRITE_MEMORY_START);

	dev_info(dev, "Initialized display rgb interface\n");
}

static int ili9341_dpi_power_on(struct ili9341 *ili)
{
	struct device *dev = (&ili->panel)->dev;
	int ret = 0;

	/* Assert RESET */
	gpiod_set_value(ili->reset_gpio, 1);

	/* Enable power */
	ret = regulator_bulk_enable(ARRAY_SIZE(ili->supplies),
				    ili->supplies);
	if (ret < 0) {
		dev_err(dev, "unable to enable vcc\n");
		return ret;
	}
	msleep(20);

	/* De-assert RESET */
	gpiod_set_value(ili->reset_gpio, 0);
	msleep(20);

	return 0;
}

static int ili9341_dpi_power_off(struct ili9341 *ili)
{
	/* Assert RESET */
	gpiod_set_value(ili->reset_gpio, 1);

	/* Disable power */
	return regulator_bulk_disable(ARRAY_SIZE(ili->supplies),
				      ili->supplies);
}

static int ili9341_dpi_disable(struct drm_panel *panel)
{
	struct ili9341 *ili = panel_to_ili9341(panel);

	mipi_dbi_command(ili->dbi, MIPI_DCS_SET_DISPLAY_OFF);
	return 0;
}

static int ili9341_dpi_unprepare(struct drm_panel *panel)
{
	struct ili9341 *ili = panel_to_ili9341(panel);

	return ili9341_dpi_power_off(ili);
}

static int ili9341_dpi_prepare(struct drm_panel *panel)
{
	struct ili9341 *ili = panel_to_ili9341(panel);
	int ret;

	ret = ili9341_dpi_power_on(ili);
	if (ret < 0)
		return ret;

	ili9341_dpi_init(ili);

	return ret;
}

static int ili9341_dpi_enable(struct drm_panel *panel)
{
	struct ili9341 *ili = panel_to_ili9341(panel);

	mipi_dbi_command(ili->dbi, MIPI_DCS_SET_DISPLAY_ON);
	return 0;
}

static int ili9341_dpi_get_modes(struct drm_panel *panel,
				 struct drm_connector *connector)
{
	struct ili9341 *ili = panel_to_ili9341(panel);
	struct drm_device *drm = connector->dev;
	struct drm_display_mode *mode;
	struct drm_display_info *info;

	info = &connector->display_info;
	info->width_mm = ili->conf->mode.width_mm;
	info->height_mm = ili->conf->mode.height_mm;

	if (ili->conf->rgb_interface & ILI9341_RGB_DPL)
		info->bus_flags |= DRM_BUS_FLAG_PIXDATA_DRIVE_POSEDGE;
	else
		info->bus_flags |= DRM_BUS_FLAG_PIXDATA_DRIVE_NEGEDGE;

	if (ili->conf->rgb_interface & ILI9341_RGB_EPL)
		info->bus_flags |= DRM_BUS_FLAG_DE_LOW;
	else
		info->bus_flags |= DRM_BUS_FLAG_DE_HIGH;

	mode = drm_mode_duplicate(drm, &ili->conf->mode);
	if (!mode) {
		drm_err(drm, "bad mode or failed to add mode\n");
		return -EINVAL;
	}
	drm_mode_set_name(mode);

	/* Set up the polarity */
	if (ili->conf->rgb_interface & ILI9341_RGB_HSPL)
		mode->flags |= DRM_MODE_FLAG_PHSYNC;
	else
		mode->flags |= DRM_MODE_FLAG_NHSYNC;

	if (ili->conf->rgb_interface & ILI9341_RGB_VSPL)
		mode->flags |= DRM_MODE_FLAG_PVSYNC;
	else
		mode->flags |= DRM_MODE_FLAG_NVSYNC;

	drm_mode_probed_add(connector, mode);

	return 1; /* Number of modes */
}

static const struct drm_panel_funcs ili9341_dpi_funcs = {
	.disable = ili9341_dpi_disable,
	.unprepare = ili9341_dpi_unprepare,
	.prepare = ili9341_dpi_prepare,
	.enable = ili9341_dpi_enable,
	.get_modes = ili9341_dpi_get_modes,
};

static void ili9341_dbi_enable(struct drm_simple_display_pipe *pipe,
			       struct drm_crtc_state *crtc_state,
			       struct drm_plane_state *plane_state)
{
	struct mipi_dbi_dev *dbidev = drm_to_mipi_dbi_dev(pipe->crtc.dev);
	struct mipi_dbi *dbi = &dbidev->dbi;
	u8 addr_mode;
	int ret, idx;

	if (!drm_dev_enter(pipe->crtc.dev, &idx))
		return;

	ret = mipi_dbi_poweron_conditional_reset(dbidev);
	if (ret < 0)
		goto out_exit;
	if (ret == 1)
		goto out_enable;

	mipi_dbi_command(dbi, MIPI_DCS_SET_DISPLAY_OFF);

	mipi_dbi_command(dbi, ILI9341_POWERB, 0x00, 0xc1, 0x30);
	mipi_dbi_command(dbi, ILI9341_POWER_SEQ, 0x64, 0x03, 0x12, 0x81);
	mipi_dbi_command(dbi, ILI9341_DTCA, 0x85, 0x00, 0x78);
	mipi_dbi_command(dbi, ILI9341_POWERA, 0x39, 0x2c, 0x00, 0x34, 0x02);
	mipi_dbi_command(dbi, ILI9341_PRC, ILI9341_DBI_PRC_NORMAL);
	mipi_dbi_command(dbi, ILI9341_DTCB, 0x00, 0x00);

	/* Power Control */
	mipi_dbi_command(dbi, ILI9341_POWER1, ILI9341_DBI_VCOMH_4P6V);
	mipi_dbi_command(dbi, ILI9341_POWER2, ILI9341_DBI_PWR_2_DEFAULT);
	/* VCOM */
	mipi_dbi_command(dbi, ILI9341_VCOM1, ILI9341_DBI_VCOM_1_VMH_4P25V,
			 ILI9341_DBI_VCOM_1_VML_1P5V);
	mipi_dbi_command(dbi, ILI9341_VCOM2, ILI9341_DBI_VCOM_2_DEC_58);

	/* Memory Access Control */
	mipi_dbi_command(dbi, MIPI_DCS_SET_PIXEL_FORMAT,
			 MIPI_DCS_PIXEL_FMT_16BIT);

	/* Frame Rate */
	mipi_dbi_command(dbi, ILI9341_FRC, ILI9341_DBI_FRC_DIVA & 0x03,
			 ILI9341_DBI_FRC_RTNA & 0x1f);

	/* Gamma */
	mipi_dbi_command(dbi, ILI9341_3GAMMA_EN, 0x00);
	mipi_dbi_command(dbi, MIPI_DCS_SET_GAMMA_CURVE, ILI9341_GAMMA_CURVE_1);
	mipi_dbi_command(dbi, ILI9341_PGAMMA,
			 0x0f, 0x31, 0x2b, 0x0c, 0x0e, 0x08, 0x4e, 0xf1,
			 0x37, 0x07, 0x10, 0x03, 0x0e, 0x09, 0x00);
	mipi_dbi_command(dbi, ILI9341_NGAMMA,
			 0x00, 0x0e, 0x14, 0x03, 0x11, 0x07, 0x31, 0xc1,
			 0x48, 0x08, 0x0f, 0x0c, 0x31, 0x36, 0x0f);

	/* DDRAM */
	mipi_dbi_command(dbi, ILI9341_ETMOD, ILI9341_DBI_EMS_GAS |
			 ILI9341_DBI_EMS_DTS |
			 ILI9341_DBI_EMS_GON);

	/* Display */
	mipi_dbi_command(dbi, ILI9341_DFC, 0x08, 0x82, 0x27, 0x00);
	mipi_dbi_command(dbi, MIPI_DCS_EXIT_SLEEP_MODE);
	msleep(100);

	mipi_dbi_command(dbi, MIPI_DCS_SET_DISPLAY_ON);
	msleep(100);

out_enable:
	switch (dbidev->rotation) {
	default:
		addr_mode = ILI9341_MADCTL_MX;
		break;
	case 90:
		addr_mode = ILI9341_MADCTL_MV;
		break;
	case 180:
		addr_mode = ILI9341_MADCTL_MY;
		break;
	case 270:
		addr_mode = ILI9341_MADCTL_MV | ILI9341_MADCTL_MY |
			    ILI9341_MADCTL_MX;
		break;
	}

	addr_mode |= ILI9341_MADCTL_BGR;
	mipi_dbi_command(dbi, MIPI_DCS_SET_ADDRESS_MODE, addr_mode);
	mipi_dbi_enable_flush(dbidev, crtc_state, plane_state);
	drm_info(&dbidev->drm, "Initialized display serial interface\n");
out_exit:
	drm_dev_exit(idx);
}

static const struct drm_simple_display_pipe_funcs ili9341_dbi_funcs = {
	.enable = ili9341_dbi_enable,
	.disable = mipi_dbi_pipe_disable,
	.update = mipi_dbi_pipe_update,
	.prepare_fb = drm_gem_simple_display_pipe_prepare_fb,
};

static const struct drm_display_mode ili9341_dbi_mode = {
	DRM_SIMPLE_MODE(240, 320, 37, 49),
};

DEFINE_DRM_GEM_CMA_FOPS(ili9341_dbi_fops);

static struct drm_driver ili9341_dbi_driver = {
	.driver_features	= DRIVER_GEM | DRIVER_MODESET | DRIVER_ATOMIC,
	.fops			= &ili9341_dbi_fops,
	DRM_GEM_CMA_DRIVER_OPS_VMAP,
	.debugfs_init		= mipi_dbi_debugfs_init,
	.name			= "ili9341",
	.desc			= "Ilitek ILI9341",
	.date			= "20210716",
	.major			= 1,
	.minor			= 0,
};

static int ili9341_dbi_probe(struct spi_device *spi, struct gpio_desc *dc,
			     struct gpio_desc *reset)
{
	struct device *dev = &spi->dev;
	struct mipi_dbi_dev *dbidev;
	struct mipi_dbi *dbi;
	struct drm_device *drm;
	struct regulator *vcc;
	u32 rotation = 0;
	int ret;

	vcc = devm_regulator_get_optional(dev, "vcc");
	if (IS_ERR(vcc)) {
		dev_err(dev, "get optional vcc failed\n");
		vcc = NULL;
	}

	dbidev = devm_drm_dev_alloc(dev, &ili9341_dbi_driver,
				    struct mipi_dbi_dev, drm);
	if (IS_ERR(dbidev))
		return PTR_ERR(dbidev);

	dbi = &dbidev->dbi;
	drm = &dbidev->drm;
	dbi->reset = reset;
	dbidev->regulator = vcc;

	drm_mode_config_init(drm);

	dbidev->backlight = devm_of_find_backlight(dev);
	if (IS_ERR(dbidev->backlight))
		return PTR_ERR(dbidev->backlight);

	device_property_read_u32(dev, "rotation", &rotation);

	ret = mipi_dbi_spi_init(spi, dbi, dc);
	if (ret)
		return ret;

	ret = mipi_dbi_dev_init(dbidev, &ili9341_dbi_funcs,
				&ili9341_dbi_mode, rotation);
	if (ret)
		return ret;

	drm_mode_config_reset(drm);

	ret = drm_dev_register(drm, 0);
	if (ret)
		return ret;

	spi_set_drvdata(spi, drm);

	drm_fbdev_generic_setup(drm, 0);

	return 0;
}

static int ili9341_dpi_probe(struct spi_device *spi, struct gpio_desc *dc,
			     struct gpio_desc *reset)
{
	struct device *dev = &spi->dev;
	struct ili9341 *ili;
	int ret;

	ili = devm_kzalloc(dev, sizeof(struct ili9341), GFP_KERNEL);
	if (!ili)
		return -ENOMEM;

	ili->dbi = devm_kzalloc(dev, sizeof(struct mipi_dbi),
				GFP_KERNEL);
	if (!ili->dbi)
		return -ENOMEM;

	ili->supplies[0].supply = "vci";
	ili->supplies[1].supply = "vddi";
	ili->supplies[2].supply = "vddi-led";
	ret = devm_regulator_bulk_get(dev, ARRAY_SIZE(ili->supplies),
				      ili->supplies);
	if (ret < 0) {
		dev_err(dev, "failed to get regulators: %d\n", ret);
		return ret;
	}

	ret = mipi_dbi_spi_init(spi, ili->dbi, dc);
	if (ret)
		return ret;

	spi_set_drvdata(spi, ili);
	ili->reset_gpio = reset;
	/*
	 * Every new incarnation of this display must have a unique
	 * data entry for the system in this driver.
	 */
	ili->conf = of_device_get_match_data(dev);
	if (!ili->conf) {
		dev_err(dev, "missing device configuration\n");
		return -ENODEV;
	}

	ili->max_spi_speed = ili->conf->max_spi_speed;
	drm_panel_init(&ili->panel, dev, &ili9341_dpi_funcs,
		       DRM_MODE_CONNECTOR_DPI);
	drm_panel_add(&ili->panel);

	return 0;
}

static int ili9341_probe(struct spi_device *spi)
{
	struct device *dev = &spi->dev;
	struct gpio_desc *dc;
	struct gpio_desc *reset;
	const struct spi_device_id *id = spi_get_device_id(spi);

	reset = devm_gpiod_get_optional(dev, "reset", GPIOD_OUT_HIGH);
	if (IS_ERR(reset))
		dev_err(dev, "Failed to get gpio 'reset'\n");

	dc = devm_gpiod_get_optional(dev, "dc", GPIOD_OUT_LOW);
	if (IS_ERR(dc))
		dev_err(dev, "Failed to get gpio 'dc'\n");

	if (!strcmp(id->name, "sf-tc240t-9370-t"))
		return ili9341_dpi_probe(spi, dc, reset);
	else if (!strcmp(id->name, "yx240qv29"))
		return ili9341_dbi_probe(spi, dc, reset);

	return -1;
}

static void ili9341_remove(struct spi_device *spi)
{
	const struct spi_device_id *id = spi_get_device_id(spi);
	struct ili9341 *ili = spi_get_drvdata(spi);
	struct drm_device *drm = spi_get_drvdata(spi);

	if (!strcmp(id->name, "sf-tc240t-9370-t")) {
		ili9341_dpi_power_off(ili);
		drm_panel_remove(&ili->panel);
	} else if (!strcmp(id->name, "yx240qv29")) {
		drm_dev_unplug(drm);
		drm_atomic_helper_shutdown(drm);
	}
}

static void ili9341_shutdown(struct spi_device *spi)
{
	const struct spi_device_id *id = spi_get_device_id(spi);

	if (!strcmp(id->name, "yx240qv29"))
		drm_atomic_helper_shutdown(spi_get_drvdata(spi));
}

static const struct of_device_id ili9341_of_match[] = {
	{
		.compatible = "st,sf-tc240t-9370-t",
		.data = &ili9341_stm32f429_disco_data,
	},
	{
		/* porting from tiny/ili9341.c
		 * for original mipi dbi compitable
		 */
		.compatible = "adafruit,yx240qv29",
		.data = NULL,
	},
	{ }
};
MODULE_DEVICE_TABLE(of, ili9341_of_match);

static const struct spi_device_id ili9341_id[] = {
	{ "yx240qv29", 0 },
	{ "sf-tc240t-9370-t", 0 },
	{ }
};
MODULE_DEVICE_TABLE(spi, ili9341_id);

static struct spi_driver ili9341_driver = {
	.probe = ili9341_probe,
	.remove = ili9341_remove,
	.shutdown = ili9341_shutdown,
	.id_table = ili9341_id,
	.driver = {
		.name = "panel-ilitek-ili9341",
		.of_match_table = ili9341_of_match,
	},
};
module_spi_driver(ili9341_driver);

MODULE_AUTHOR("Dillon Min <dillon.minfei@gmail.com>");
MODULE_DESCRIPTION("ILI9341 LCD panel driver");
MODULE_LICENSE("GPL v2");
