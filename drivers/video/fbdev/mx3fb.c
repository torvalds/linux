// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2008
 * Guennadi Liakhovetski, DENX Software Engineering, <lg@denx.de>
 *
 * Copyright 2004-2007 Freescale Semiconductor, Inc. All Rights Reserved.
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/platform_device.h>
#include <linux/sched.h>
#include <linux/errno.h>
#include <linux/string.h>
#include <linux/interrupt.h>
#include <linux/slab.h>
#include <linux/fb.h>
#include <linux/delay.h>
#include <linux/init.h>
#include <linux/ioport.h>
#include <linux/dma-mapping.h>
#include <linux/dmaengine.h>
#include <linux/console.h>
#include <linux/clk.h>
#include <linux/mutex.h>
#include <linux/dma/ipu-dma.h>
#include <linux/backlight.h>

#include <linux/dma/imx-dma.h>
#include <linux/platform_data/video-mx3fb.h>

#include <asm/io.h>
#include <linux/uaccess.h>

#define MX3FB_NAME		"mx3_sdc_fb"

#define MX3FB_REG_OFFSET	0xB4

/* SDC Registers */
#define SDC_COM_CONF		(0xB4 - MX3FB_REG_OFFSET)
#define SDC_GW_CTRL		(0xB8 - MX3FB_REG_OFFSET)
#define SDC_FG_POS		(0xBC - MX3FB_REG_OFFSET)
#define SDC_BG_POS		(0xC0 - MX3FB_REG_OFFSET)
#define SDC_CUR_POS		(0xC4 - MX3FB_REG_OFFSET)
#define SDC_PWM_CTRL		(0xC8 - MX3FB_REG_OFFSET)
#define SDC_CUR_MAP		(0xCC - MX3FB_REG_OFFSET)
#define SDC_HOR_CONF		(0xD0 - MX3FB_REG_OFFSET)
#define SDC_VER_CONF		(0xD4 - MX3FB_REG_OFFSET)
#define SDC_SHARP_CONF_1	(0xD8 - MX3FB_REG_OFFSET)
#define SDC_SHARP_CONF_2	(0xDC - MX3FB_REG_OFFSET)

/* Register bits */
#define SDC_COM_TFT_COLOR	0x00000001UL
#define SDC_COM_FG_EN		0x00000010UL
#define SDC_COM_GWSEL		0x00000020UL
#define SDC_COM_GLB_A		0x00000040UL
#define SDC_COM_KEY_COLOR_G	0x00000080UL
#define SDC_COM_BG_EN		0x00000200UL
#define SDC_COM_SHARP		0x00001000UL

#define SDC_V_SYNC_WIDTH_L	0x00000001UL

/* Display Interface registers */
#define DI_DISP_IF_CONF		(0x0124 - MX3FB_REG_OFFSET)
#define DI_DISP_SIG_POL		(0x0128 - MX3FB_REG_OFFSET)
#define DI_SER_DISP1_CONF	(0x012C - MX3FB_REG_OFFSET)
#define DI_SER_DISP2_CONF	(0x0130 - MX3FB_REG_OFFSET)
#define DI_HSP_CLK_PER		(0x0134 - MX3FB_REG_OFFSET)
#define DI_DISP0_TIME_CONF_1	(0x0138 - MX3FB_REG_OFFSET)
#define DI_DISP0_TIME_CONF_2	(0x013C - MX3FB_REG_OFFSET)
#define DI_DISP0_TIME_CONF_3	(0x0140 - MX3FB_REG_OFFSET)
#define DI_DISP1_TIME_CONF_1	(0x0144 - MX3FB_REG_OFFSET)
#define DI_DISP1_TIME_CONF_2	(0x0148 - MX3FB_REG_OFFSET)
#define DI_DISP1_TIME_CONF_3	(0x014C - MX3FB_REG_OFFSET)
#define DI_DISP2_TIME_CONF_1	(0x0150 - MX3FB_REG_OFFSET)
#define DI_DISP2_TIME_CONF_2	(0x0154 - MX3FB_REG_OFFSET)
#define DI_DISP2_TIME_CONF_3	(0x0158 - MX3FB_REG_OFFSET)
#define DI_DISP3_TIME_CONF	(0x015C - MX3FB_REG_OFFSET)
#define DI_DISP0_DB0_MAP	(0x0160 - MX3FB_REG_OFFSET)
#define DI_DISP0_DB1_MAP	(0x0164 - MX3FB_REG_OFFSET)
#define DI_DISP0_DB2_MAP	(0x0168 - MX3FB_REG_OFFSET)
#define DI_DISP0_CB0_MAP	(0x016C - MX3FB_REG_OFFSET)
#define DI_DISP0_CB1_MAP	(0x0170 - MX3FB_REG_OFFSET)
#define DI_DISP0_CB2_MAP	(0x0174 - MX3FB_REG_OFFSET)
#define DI_DISP1_DB0_MAP	(0x0178 - MX3FB_REG_OFFSET)
#define DI_DISP1_DB1_MAP	(0x017C - MX3FB_REG_OFFSET)
#define DI_DISP1_DB2_MAP	(0x0180 - MX3FB_REG_OFFSET)
#define DI_DISP1_CB0_MAP	(0x0184 - MX3FB_REG_OFFSET)
#define DI_DISP1_CB1_MAP	(0x0188 - MX3FB_REG_OFFSET)
#define DI_DISP1_CB2_MAP	(0x018C - MX3FB_REG_OFFSET)
#define DI_DISP2_DB0_MAP	(0x0190 - MX3FB_REG_OFFSET)
#define DI_DISP2_DB1_MAP	(0x0194 - MX3FB_REG_OFFSET)
#define DI_DISP2_DB2_MAP	(0x0198 - MX3FB_REG_OFFSET)
#define DI_DISP2_CB0_MAP	(0x019C - MX3FB_REG_OFFSET)
#define DI_DISP2_CB1_MAP	(0x01A0 - MX3FB_REG_OFFSET)
#define DI_DISP2_CB2_MAP	(0x01A4 - MX3FB_REG_OFFSET)
#define DI_DISP3_B0_MAP		(0x01A8 - MX3FB_REG_OFFSET)
#define DI_DISP3_B1_MAP		(0x01AC - MX3FB_REG_OFFSET)
#define DI_DISP3_B2_MAP		(0x01B0 - MX3FB_REG_OFFSET)
#define DI_DISP_ACC_CC		(0x01B4 - MX3FB_REG_OFFSET)
#define DI_DISP_LLA_CONF	(0x01B8 - MX3FB_REG_OFFSET)
#define DI_DISP_LLA_DATA	(0x01BC - MX3FB_REG_OFFSET)

/* DI_DISP_SIG_POL bits */
#define DI_D3_VSYNC_POL_SHIFT		28
#define DI_D3_HSYNC_POL_SHIFT		27
#define DI_D3_DRDY_SHARP_POL_SHIFT	26
#define DI_D3_CLK_POL_SHIFT		25
#define DI_D3_DATA_POL_SHIFT		24

/* DI_DISP_IF_CONF bits */
#define DI_D3_CLK_IDLE_SHIFT		26
#define DI_D3_CLK_SEL_SHIFT		25
#define DI_D3_DATAMSK_SHIFT		24

enum ipu_panel {
	IPU_PANEL_SHARP_TFT,
	IPU_PANEL_TFT,
};

struct ipu_di_signal_cfg {
	unsigned datamask_en:1;
	unsigned clksel_en:1;
	unsigned clkidle_en:1;
	unsigned data_pol:1;	/* true = inverted */
	unsigned clk_pol:1;	/* true = rising edge */
	unsigned enable_pol:1;
	unsigned Hsync_pol:1;	/* true = active high */
	unsigned Vsync_pol:1;
};

static const struct fb_videomode mx3fb_modedb[] = {
	{
		/* 240x320 @ 60 Hz */
		.name		= "Sharp-QVGA",
		.refresh	= 60,
		.xres		= 240,
		.yres		= 320,
		.pixclock	= 185925,
		.left_margin	= 9,
		.right_margin	= 16,
		.upper_margin	= 7,
		.lower_margin	= 9,
		.hsync_len	= 1,
		.vsync_len	= 1,
		.sync		= FB_SYNC_HOR_HIGH_ACT | FB_SYNC_SHARP_MODE |
				  FB_SYNC_CLK_INVERT | FB_SYNC_DATA_INVERT |
				  FB_SYNC_CLK_IDLE_EN,
		.vmode		= FB_VMODE_NONINTERLACED,
		.flag		= 0,
	}, {
		/* 240x33 @ 60 Hz */
		.name		= "Sharp-CLI",
		.refresh	= 60,
		.xres		= 240,
		.yres		= 33,
		.pixclock	= 185925,
		.left_margin	= 9,
		.right_margin	= 16,
		.upper_margin	= 7,
		.lower_margin	= 9 + 287,
		.hsync_len	= 1,
		.vsync_len	= 1,
		.sync		= FB_SYNC_HOR_HIGH_ACT | FB_SYNC_SHARP_MODE |
				  FB_SYNC_CLK_INVERT | FB_SYNC_DATA_INVERT |
				  FB_SYNC_CLK_IDLE_EN,
		.vmode		= FB_VMODE_NONINTERLACED,
		.flag		= 0,
	}, {
		/* 640x480 @ 60 Hz */
		.name		= "NEC-VGA",
		.refresh	= 60,
		.xres		= 640,
		.yres		= 480,
		.pixclock	= 38255,
		.left_margin	= 144,
		.right_margin	= 0,
		.upper_margin	= 34,
		.lower_margin	= 40,
		.hsync_len	= 1,
		.vsync_len	= 1,
		.sync		= FB_SYNC_VERT_HIGH_ACT | FB_SYNC_OE_ACT_HIGH,
		.vmode		= FB_VMODE_NONINTERLACED,
		.flag		= 0,
	}, {
		/* NTSC TV output */
		.name		= "TV-NTSC",
		.refresh	= 60,
		.xres		= 640,
		.yres		= 480,
		.pixclock	= 37538,
		.left_margin	= 38,
		.right_margin	= 858 - 640 - 38 - 3,
		.upper_margin	= 36,
		.lower_margin	= 518 - 480 - 36 - 1,
		.hsync_len	= 3,
		.vsync_len	= 1,
		.sync		= 0,
		.vmode		= FB_VMODE_NONINTERLACED,
		.flag		= 0,
	}, {
		/* PAL TV output */
		.name		= "TV-PAL",
		.refresh	= 50,
		.xres		= 640,
		.yres		= 480,
		.pixclock	= 37538,
		.left_margin	= 38,
		.right_margin	= 960 - 640 - 38 - 32,
		.upper_margin	= 32,
		.lower_margin	= 555 - 480 - 32 - 3,
		.hsync_len	= 32,
		.vsync_len	= 3,
		.sync		= 0,
		.vmode		= FB_VMODE_NONINTERLACED,
		.flag		= 0,
	}, {
		/* TV output VGA mode, 640x480 @ 65 Hz */
		.name		= "TV-VGA",
		.refresh	= 60,
		.xres		= 640,
		.yres		= 480,
		.pixclock	= 40574,
		.left_margin	= 35,
		.right_margin	= 45,
		.upper_margin	= 9,
		.lower_margin	= 1,
		.hsync_len	= 46,
		.vsync_len	= 5,
		.sync		= 0,
		.vmode		= FB_VMODE_NONINTERLACED,
		.flag		= 0,
	},
};

struct mx3fb_data {
	struct fb_info		*fbi;
	int			backlight_level;
	void __iomem		*reg_base;
	spinlock_t		lock;
	struct device		*dev;
	struct backlight_device	*bl;

	uint32_t		h_start_width;
	uint32_t		v_start_width;
	enum disp_data_mapping	disp_data_fmt;
};

struct dma_chan_request {
	struct mx3fb_data	*mx3fb;
	enum ipu_channel	id;
};

/* MX3 specific framebuffer information. */
struct mx3fb_info {
	int				blank;
	enum ipu_channel		ipu_ch;
	uint32_t			cur_ipu_buf;

	u32				pseudo_palette[16];

	struct completion		flip_cmpl;
	struct mutex			mutex;	/* Protects fb-ops */
	struct mx3fb_data		*mx3fb;
	struct idmac_channel		*idmac_channel;
	struct dma_async_tx_descriptor	*txd;
	dma_cookie_t			cookie;
	struct scatterlist		sg[2];

	struct fb_var_screeninfo	cur_var; /* current var info */
};

static void sdc_set_brightness(struct mx3fb_data *mx3fb, uint8_t value);
static u32 sdc_get_brightness(struct mx3fb_data *mx3fb);

static int mx3fb_bl_get_brightness(struct backlight_device *bl)
{
	struct mx3fb_data *fbd = bl_get_data(bl);

	return sdc_get_brightness(fbd);
}

static int mx3fb_bl_update_status(struct backlight_device *bl)
{
	struct mx3fb_data *fbd = bl_get_data(bl);
	int brightness = backlight_get_brightness(bl);

	fbd->backlight_level = (fbd->backlight_level & ~0xFF) | brightness;

	sdc_set_brightness(fbd, fbd->backlight_level);

	return 0;
}

static const struct backlight_ops mx3fb_lcdc_bl_ops = {
	.update_status = mx3fb_bl_update_status,
	.get_brightness = mx3fb_bl_get_brightness,
};

static void mx3fb_init_backlight(struct mx3fb_data *fbd)
{
	struct backlight_properties props;
	struct backlight_device	*bl;

	if (fbd->bl)
		return;

	memset(&props, 0, sizeof(struct backlight_properties));
	props.max_brightness = 0xff;
	props.type = BACKLIGHT_RAW;
	sdc_set_brightness(fbd, fbd->backlight_level);

	bl = backlight_device_register("mx3fb-bl", fbd->dev, fbd,
				       &mx3fb_lcdc_bl_ops, &props);
	if (IS_ERR(bl)) {
		dev_err(fbd->dev, "error %ld on backlight register\n",
				PTR_ERR(bl));
		return;
	}

	fbd->bl = bl;
	bl->props.power = FB_BLANK_UNBLANK;
	bl->props.fb_blank = FB_BLANK_UNBLANK;
	bl->props.brightness = mx3fb_bl_get_brightness(bl);
}

static void mx3fb_exit_backlight(struct mx3fb_data *fbd)
{
	backlight_device_unregister(fbd->bl);
}

static void mx3fb_dma_done(void *);

/* Used fb-mode and bpp. Can be set on kernel command line, therefore file-static. */
static const char *fb_mode;
static unsigned long default_bpp = 16;

static u32 mx3fb_read_reg(struct mx3fb_data *mx3fb, unsigned long reg)
{
	return __raw_readl(mx3fb->reg_base + reg);
}

static void mx3fb_write_reg(struct mx3fb_data *mx3fb, u32 value, unsigned long reg)
{
	__raw_writel(value, mx3fb->reg_base + reg);
}

struct di_mapping {
	uint32_t b0, b1, b2;
};

static const struct di_mapping di_mappings[] = {
	[IPU_DISP_DATA_MAPPING_RGB666] = { 0x0005000f, 0x000b000f, 0x0011000f },
	[IPU_DISP_DATA_MAPPING_RGB565] = { 0x0004003f, 0x000a000f, 0x000f003f },
	[IPU_DISP_DATA_MAPPING_RGB888] = { 0x00070000, 0x000f0000, 0x00170000 },
};

static void sdc_fb_init(struct mx3fb_info *fbi)
{
	struct mx3fb_data *mx3fb = fbi->mx3fb;
	uint32_t reg;

	reg = mx3fb_read_reg(mx3fb, SDC_COM_CONF);

	mx3fb_write_reg(mx3fb, reg | SDC_COM_BG_EN, SDC_COM_CONF);
}

/* Returns enabled flag before uninit */
static uint32_t sdc_fb_uninit(struct mx3fb_info *fbi)
{
	struct mx3fb_data *mx3fb = fbi->mx3fb;
	uint32_t reg;

	reg = mx3fb_read_reg(mx3fb, SDC_COM_CONF);

	mx3fb_write_reg(mx3fb, reg & ~SDC_COM_BG_EN, SDC_COM_CONF);

	return reg & SDC_COM_BG_EN;
}

static void sdc_enable_channel(struct mx3fb_info *mx3_fbi)
{
	struct mx3fb_data *mx3fb = mx3_fbi->mx3fb;
	struct idmac_channel *ichan = mx3_fbi->idmac_channel;
	struct dma_chan *dma_chan = &ichan->dma_chan;
	unsigned long flags;
	dma_cookie_t cookie;

	if (mx3_fbi->txd)
		dev_dbg(mx3fb->dev, "mx3fbi %p, desc %p, sg %p\n", mx3_fbi,
			to_tx_desc(mx3_fbi->txd), to_tx_desc(mx3_fbi->txd)->sg);
	else
		dev_dbg(mx3fb->dev, "mx3fbi %p, txd = NULL\n", mx3_fbi);

	/* This enables the channel */
	if (mx3_fbi->cookie < 0) {
		mx3_fbi->txd = dmaengine_prep_slave_sg(dma_chan,
		      &mx3_fbi->sg[0], 1, DMA_MEM_TO_DEV, DMA_PREP_INTERRUPT);
		if (!mx3_fbi->txd) {
			dev_err(mx3fb->dev, "Cannot allocate descriptor on %d\n",
				dma_chan->chan_id);
			return;
		}

		mx3_fbi->txd->callback_param	= mx3_fbi->txd;
		mx3_fbi->txd->callback		= mx3fb_dma_done;

		cookie = mx3_fbi->txd->tx_submit(mx3_fbi->txd);
		dev_dbg(mx3fb->dev, "%d: Submit %p #%d [%c]\n", __LINE__,
		       mx3_fbi->txd, cookie, list_empty(&ichan->queue) ? '-' : '+');
	} else {
		if (!mx3_fbi->txd || !mx3_fbi->txd->tx_submit) {
			dev_err(mx3fb->dev, "Cannot enable channel %d\n",
				dma_chan->chan_id);
			return;
		}

		/* Just re-activate the same buffer */
		dma_async_issue_pending(dma_chan);
		cookie = mx3_fbi->cookie;
		dev_dbg(mx3fb->dev, "%d: Re-submit %p #%d [%c]\n", __LINE__,
		       mx3_fbi->txd, cookie, list_empty(&ichan->queue) ? '-' : '+');
	}

	if (cookie >= 0) {
		spin_lock_irqsave(&mx3fb->lock, flags);
		sdc_fb_init(mx3_fbi);
		mx3_fbi->cookie = cookie;
		spin_unlock_irqrestore(&mx3fb->lock, flags);
	}

	/*
	 * Attention! Without this msleep the channel keeps generating
	 * interrupts. Next sdc_set_brightness() is going to be called
	 * from mx3fb_blank().
	 */
	msleep(2);
}

static void sdc_disable_channel(struct mx3fb_info *mx3_fbi)
{
	struct mx3fb_data *mx3fb = mx3_fbi->mx3fb;
	unsigned long flags;

	if (mx3_fbi->txd == NULL)
		return;

	spin_lock_irqsave(&mx3fb->lock, flags);

	sdc_fb_uninit(mx3_fbi);

	spin_unlock_irqrestore(&mx3fb->lock, flags);

	dmaengine_terminate_all(mx3_fbi->txd->chan);
	mx3_fbi->txd = NULL;
	mx3_fbi->cookie = -EINVAL;
}

/**
 * sdc_set_window_pos() - set window position of the respective plane.
 * @mx3fb:	mx3fb context.
 * @channel:	IPU DMAC channel ID.
 * @x_pos:	X coordinate relative to the top left corner to place window at.
 * @y_pos:	Y coordinate relative to the top left corner to place window at.
 * @return:	0 on success or negative error code on failure.
 */
static int sdc_set_window_pos(struct mx3fb_data *mx3fb, enum ipu_channel channel,
			      int16_t x_pos, int16_t y_pos)
{
	if (channel != IDMAC_SDC_0)
		return -EINVAL;

	x_pos += mx3fb->h_start_width;
	y_pos += mx3fb->v_start_width;

	mx3fb_write_reg(mx3fb, (x_pos << 16) | y_pos, SDC_BG_POS);
	return 0;
}

/**
 * sdc_init_panel() - initialize a synchronous LCD panel.
 * @mx3fb:		mx3fb context.
 * @panel:		panel type.
 * @pixel_clk:		desired pixel clock frequency in Hz.
 * @width:		width of panel in pixels.
 * @height:		height of panel in pixels.
 * @h_start_width:	number of pixel clocks between the HSYNC signal pulse
 *			and the start of valid data.
 * @h_sync_width:	width of the HSYNC signal in units of pixel clocks.
 * @h_end_width:	number of pixel clocks between the end of valid data
 *			and the HSYNC signal for next line.
 * @v_start_width:	number of lines between the VSYNC signal pulse and the
 *			start of valid data.
 * @v_sync_width:	width of the VSYNC signal in units of lines
 * @v_end_width:	number of lines between the end of valid data and the
 *			VSYNC signal for next frame.
 * @sig:		bitfield of signal polarities for LCD interface.
 * @return:		0 on success or negative error code on failure.
 */
static int sdc_init_panel(struct mx3fb_data *mx3fb, enum ipu_panel panel,
			  uint32_t pixel_clk,
			  uint16_t width, uint16_t height,
			  uint16_t h_start_width, uint16_t h_sync_width,
			  uint16_t h_end_width, uint16_t v_start_width,
			  uint16_t v_sync_width, uint16_t v_end_width,
			  const struct ipu_di_signal_cfg *sig)
{
	unsigned long lock_flags;
	uint32_t reg;
	uint32_t old_conf;
	uint32_t div;
	struct clk *ipu_clk;
	const struct di_mapping *map;

	dev_dbg(mx3fb->dev, "panel size = %d x %d", width, height);

	if (v_sync_width == 0 || h_sync_width == 0)
		return -EINVAL;

	/* Init panel size and blanking periods */
	reg = ((uint32_t) (h_sync_width - 1) << 26) |
		((uint32_t) (width + h_start_width + h_end_width - 1) << 16);
	mx3fb_write_reg(mx3fb, reg, SDC_HOR_CONF);

#ifdef DEBUG
	printk(KERN_CONT " hor_conf %x,", reg);
#endif

	reg = ((uint32_t) (v_sync_width - 1) << 26) | SDC_V_SYNC_WIDTH_L |
	    ((uint32_t) (height + v_start_width + v_end_width - 1) << 16);
	mx3fb_write_reg(mx3fb, reg, SDC_VER_CONF);

#ifdef DEBUG
	printk(KERN_CONT " ver_conf %x\n", reg);
#endif

	mx3fb->h_start_width = h_start_width;
	mx3fb->v_start_width = v_start_width;

	switch (panel) {
	case IPU_PANEL_SHARP_TFT:
		mx3fb_write_reg(mx3fb, 0x00FD0102L, SDC_SHARP_CONF_1);
		mx3fb_write_reg(mx3fb, 0x00F500F4L, SDC_SHARP_CONF_2);
		mx3fb_write_reg(mx3fb, SDC_COM_SHARP | SDC_COM_TFT_COLOR, SDC_COM_CONF);
		break;
	case IPU_PANEL_TFT:
		mx3fb_write_reg(mx3fb, SDC_COM_TFT_COLOR, SDC_COM_CONF);
		break;
	default:
		return -EINVAL;
	}

	/* Init clocking */

	/*
	 * Calculate divider: fractional part is 4 bits so simply multiple by
	 * 2^4 to get fractional part, as long as we stay under ~250MHz and on
	 * i.MX31 it (HSP_CLK) is <= 178MHz. Currently 128.267MHz
	 */
	ipu_clk = clk_get(mx3fb->dev, NULL);
	if (!IS_ERR(ipu_clk)) {
		div = clk_get_rate(ipu_clk) * 16 / pixel_clk;
		clk_put(ipu_clk);
	} else {
		div = 0;
	}

	if (div < 0x40) {	/* Divider less than 4 */
		dev_dbg(mx3fb->dev,
			"InitPanel() - Pixel clock divider less than 4\n");
		div = 0x40;
	}

	dev_dbg(mx3fb->dev, "pixel clk = %u, divider %u.%u\n",
		pixel_clk, div >> 4, (div & 7) * 125);

	spin_lock_irqsave(&mx3fb->lock, lock_flags);

	/*
	 * DISP3_IF_CLK_DOWN_WR is half the divider value and 2 fraction bits
	 * fewer. Subtract 1 extra from DISP3_IF_CLK_DOWN_WR based on timing
	 * debug. DISP3_IF_CLK_UP_WR is 0
	 */
	mx3fb_write_reg(mx3fb, (((div / 8) - 1) << 22) | div, DI_DISP3_TIME_CONF);

	/* DI settings */
	old_conf = mx3fb_read_reg(mx3fb, DI_DISP_IF_CONF) & 0x78FFFFFF;
	old_conf |= sig->datamask_en << DI_D3_DATAMSK_SHIFT |
		sig->clksel_en << DI_D3_CLK_SEL_SHIFT |
		sig->clkidle_en << DI_D3_CLK_IDLE_SHIFT;
	mx3fb_write_reg(mx3fb, old_conf, DI_DISP_IF_CONF);

	old_conf = mx3fb_read_reg(mx3fb, DI_DISP_SIG_POL) & 0xE0FFFFFF;
	old_conf |= sig->data_pol << DI_D3_DATA_POL_SHIFT |
		sig->clk_pol << DI_D3_CLK_POL_SHIFT |
		sig->enable_pol << DI_D3_DRDY_SHARP_POL_SHIFT |
		sig->Hsync_pol << DI_D3_HSYNC_POL_SHIFT |
		sig->Vsync_pol << DI_D3_VSYNC_POL_SHIFT;
	mx3fb_write_reg(mx3fb, old_conf, DI_DISP_SIG_POL);

	map = &di_mappings[mx3fb->disp_data_fmt];
	mx3fb_write_reg(mx3fb, map->b0, DI_DISP3_B0_MAP);
	mx3fb_write_reg(mx3fb, map->b1, DI_DISP3_B1_MAP);
	mx3fb_write_reg(mx3fb, map->b2, DI_DISP3_B2_MAP);

	spin_unlock_irqrestore(&mx3fb->lock, lock_flags);

	dev_dbg(mx3fb->dev, "DI_DISP_IF_CONF = 0x%08X\n",
		mx3fb_read_reg(mx3fb, DI_DISP_IF_CONF));
	dev_dbg(mx3fb->dev, "DI_DISP_SIG_POL = 0x%08X\n",
		mx3fb_read_reg(mx3fb, DI_DISP_SIG_POL));
	dev_dbg(mx3fb->dev, "DI_DISP3_TIME_CONF = 0x%08X\n",
		mx3fb_read_reg(mx3fb, DI_DISP3_TIME_CONF));

	return 0;
}

/**
 * sdc_set_color_key() - set the transparent color key for SDC graphic plane.
 * @mx3fb:	mx3fb context.
 * @channel:	IPU DMAC channel ID.
 * @enable:	boolean to enable or disable color keyl.
 * @color_key:	24-bit RGB color to use as transparent color key.
 * @return:	0 on success or negative error code on failure.
 */
static int sdc_set_color_key(struct mx3fb_data *mx3fb, enum ipu_channel channel,
			     bool enable, uint32_t color_key)
{
	uint32_t reg, sdc_conf;
	unsigned long lock_flags;

	spin_lock_irqsave(&mx3fb->lock, lock_flags);

	sdc_conf = mx3fb_read_reg(mx3fb, SDC_COM_CONF);
	if (channel == IDMAC_SDC_0)
		sdc_conf &= ~SDC_COM_GWSEL;
	else
		sdc_conf |= SDC_COM_GWSEL;

	if (enable) {
		reg = mx3fb_read_reg(mx3fb, SDC_GW_CTRL) & 0xFF000000L;
		mx3fb_write_reg(mx3fb, reg | (color_key & 0x00FFFFFFL),
			     SDC_GW_CTRL);

		sdc_conf |= SDC_COM_KEY_COLOR_G;
	} else {
		sdc_conf &= ~SDC_COM_KEY_COLOR_G;
	}
	mx3fb_write_reg(mx3fb, sdc_conf, SDC_COM_CONF);

	spin_unlock_irqrestore(&mx3fb->lock, lock_flags);

	return 0;
}

/**
 * sdc_set_global_alpha() - set global alpha blending modes.
 * @mx3fb:	mx3fb context.
 * @enable:	boolean to enable or disable global alpha blending. If disabled,
 *		per pixel blending is used.
 * @alpha:	global alpha value.
 * @return:	0 on success or negative error code on failure.
 */
static int sdc_set_global_alpha(struct mx3fb_data *mx3fb, bool enable, uint8_t alpha)
{
	uint32_t reg;
	unsigned long lock_flags;

	spin_lock_irqsave(&mx3fb->lock, lock_flags);

	if (enable) {
		reg = mx3fb_read_reg(mx3fb, SDC_GW_CTRL) & 0x00FFFFFFL;
		mx3fb_write_reg(mx3fb, reg | ((uint32_t) alpha << 24), SDC_GW_CTRL);

		reg = mx3fb_read_reg(mx3fb, SDC_COM_CONF);
		mx3fb_write_reg(mx3fb, reg | SDC_COM_GLB_A, SDC_COM_CONF);
	} else {
		reg = mx3fb_read_reg(mx3fb, SDC_COM_CONF);
		mx3fb_write_reg(mx3fb, reg & ~SDC_COM_GLB_A, SDC_COM_CONF);
	}

	spin_unlock_irqrestore(&mx3fb->lock, lock_flags);

	return 0;
}

static u32 sdc_get_brightness(struct mx3fb_data *mx3fb)
{
	u32 brightness;

	brightness = mx3fb_read_reg(mx3fb, SDC_PWM_CTRL);
	brightness = (brightness >> 16) & 0xFF;

	return brightness;
}

static void sdc_set_brightness(struct mx3fb_data *mx3fb, uint8_t value)
{
	dev_dbg(mx3fb->dev, "%s: value = %d\n", __func__, value);
	/* This might be board-specific */
	mx3fb_write_reg(mx3fb, 0x03000000UL | value << 16, SDC_PWM_CTRL);
	return;
}

static uint32_t bpp_to_pixfmt(int bpp)
{
	uint32_t pixfmt = 0;
	switch (bpp) {
	case 24:
		pixfmt = IPU_PIX_FMT_BGR24;
		break;
	case 32:
		pixfmt = IPU_PIX_FMT_BGR32;
		break;
	case 16:
		pixfmt = IPU_PIX_FMT_RGB565;
		break;
	}
	return pixfmt;
}

static int mx3fb_blank(int blank, struct fb_info *fbi);
static int mx3fb_map_video_memory(struct fb_info *fbi, unsigned int mem_len,
				  bool lock);
static int mx3fb_unmap_video_memory(struct fb_info *fbi);

/**
 * mx3fb_set_fix() - set fixed framebuffer parameters from variable settings.
 * @fbi:	framebuffer information pointer
 * @return:	0 on success or negative error code on failure.
 */
static int mx3fb_set_fix(struct fb_info *fbi)
{
	struct fb_fix_screeninfo *fix = &fbi->fix;
	struct fb_var_screeninfo *var = &fbi->var;

	memcpy(fix->id, "DISP3 BG", 8);

	fix->line_length = var->xres_virtual * var->bits_per_pixel / 8;

	fix->type = FB_TYPE_PACKED_PIXELS;
	fix->accel = FB_ACCEL_NONE;
	fix->visual = FB_VISUAL_TRUECOLOR;
	fix->xpanstep = 1;
	fix->ypanstep = 1;

	return 0;
}

static void mx3fb_dma_done(void *arg)
{
	struct idmac_tx_desc *tx_desc = to_tx_desc(arg);
	struct dma_chan *chan = tx_desc->txd.chan;
	struct idmac_channel *ichannel = to_idmac_chan(chan);
	struct mx3fb_data *mx3fb = ichannel->client;
	struct mx3fb_info *mx3_fbi = mx3fb->fbi->par;

	dev_dbg(mx3fb->dev, "irq %d callback\n", ichannel->eof_irq);

	/* We only need one interrupt, it will be re-enabled as needed */
	disable_irq_nosync(ichannel->eof_irq);

	complete(&mx3_fbi->flip_cmpl);
}

static bool mx3fb_must_set_par(struct fb_info *fbi)
{
	struct mx3fb_info *mx3_fbi = fbi->par;
	struct fb_var_screeninfo old_var = mx3_fbi->cur_var;
	struct fb_var_screeninfo new_var = fbi->var;

	if ((fbi->var.activate & FB_ACTIVATE_FORCE) &&
	    (fbi->var.activate & FB_ACTIVATE_MASK) == FB_ACTIVATE_NOW)
		return true;

	/*
	 * Ignore xoffset and yoffset update,
	 * because pan display handles this case.
	 */
	old_var.xoffset = new_var.xoffset;
	old_var.yoffset = new_var.yoffset;

	return !!memcmp(&old_var, &new_var, sizeof(struct fb_var_screeninfo));
}

static int __set_par(struct fb_info *fbi, bool lock)
{
	u32 mem_len, cur_xoffset, cur_yoffset;
	struct ipu_di_signal_cfg sig_cfg;
	enum ipu_panel mode = IPU_PANEL_TFT;
	struct mx3fb_info *mx3_fbi = fbi->par;
	struct mx3fb_data *mx3fb = mx3_fbi->mx3fb;
	struct idmac_channel *ichan = mx3_fbi->idmac_channel;
	struct idmac_video_param *video = &ichan->params.video;
	struct scatterlist *sg = mx3_fbi->sg;

	/* Total cleanup */
	if (mx3_fbi->txd)
		sdc_disable_channel(mx3_fbi);

	mx3fb_set_fix(fbi);

	mem_len = fbi->var.yres_virtual * fbi->fix.line_length;
	if (mem_len > fbi->fix.smem_len) {
		if (fbi->fix.smem_start)
			mx3fb_unmap_video_memory(fbi);

		if (mx3fb_map_video_memory(fbi, mem_len, lock) < 0)
			return -ENOMEM;
	}

	sg_init_table(&sg[0], 1);
	sg_init_table(&sg[1], 1);

	sg_dma_address(&sg[0]) = fbi->fix.smem_start;
	sg_set_page(&sg[0], virt_to_page(fbi->screen_base),
		    fbi->fix.smem_len,
		    offset_in_page(fbi->screen_base));

	if (mx3_fbi->ipu_ch == IDMAC_SDC_0) {
		memset(&sig_cfg, 0, sizeof(sig_cfg));
		if (fbi->var.sync & FB_SYNC_HOR_HIGH_ACT)
			sig_cfg.Hsync_pol = true;
		if (fbi->var.sync & FB_SYNC_VERT_HIGH_ACT)
			sig_cfg.Vsync_pol = true;
		if (fbi->var.sync & FB_SYNC_CLK_INVERT)
			sig_cfg.clk_pol = true;
		if (fbi->var.sync & FB_SYNC_DATA_INVERT)
			sig_cfg.data_pol = true;
		if (fbi->var.sync & FB_SYNC_OE_ACT_HIGH)
			sig_cfg.enable_pol = true;
		if (fbi->var.sync & FB_SYNC_CLK_IDLE_EN)
			sig_cfg.clkidle_en = true;
		if (fbi->var.sync & FB_SYNC_CLK_SEL_EN)
			sig_cfg.clksel_en = true;
		if (fbi->var.sync & FB_SYNC_SHARP_MODE)
			mode = IPU_PANEL_SHARP_TFT;

		dev_dbg(fbi->device, "pixclock = %u Hz\n",
			(u32) (PICOS2KHZ(fbi->var.pixclock) * 1000UL));

		if (sdc_init_panel(mx3fb, mode,
				   (PICOS2KHZ(fbi->var.pixclock)) * 1000UL,
				   fbi->var.xres, fbi->var.yres,
				   fbi->var.left_margin,
				   fbi->var.hsync_len,
				   fbi->var.right_margin +
				   fbi->var.hsync_len,
				   fbi->var.upper_margin,
				   fbi->var.vsync_len,
				   fbi->var.lower_margin +
				   fbi->var.vsync_len, &sig_cfg) != 0) {
			dev_err(fbi->device,
				"mx3fb: Error initializing panel.\n");
			return -EINVAL;
		}
	}

	sdc_set_window_pos(mx3fb, mx3_fbi->ipu_ch, 0, 0);

	mx3_fbi->cur_ipu_buf	= 0;

	video->out_pixel_fmt	= bpp_to_pixfmt(fbi->var.bits_per_pixel);
	video->out_width	= fbi->var.xres;
	video->out_height	= fbi->var.yres;
	video->out_stride	= fbi->var.xres_virtual;

	if (mx3_fbi->blank == FB_BLANK_UNBLANK) {
		sdc_enable_channel(mx3_fbi);
		/*
		 * sg[0] points to fb smem_start address
		 * and is actually active in controller.
		 */
		mx3_fbi->cur_var.xoffset = 0;
		mx3_fbi->cur_var.yoffset = 0;
	}

	/*
	 * Preserve xoffset and yoffest in case they are
	 * inactive in controller as fb is blanked.
	 */
	cur_xoffset = mx3_fbi->cur_var.xoffset;
	cur_yoffset = mx3_fbi->cur_var.yoffset;
	mx3_fbi->cur_var = fbi->var;
	mx3_fbi->cur_var.xoffset = cur_xoffset;
	mx3_fbi->cur_var.yoffset = cur_yoffset;

	return 0;
}

/**
 * mx3fb_set_par() - set framebuffer parameters and change the operating mode.
 * @fbi:	framebuffer information pointer.
 * @return:	0 on success or negative error code on failure.
 */
static int mx3fb_set_par(struct fb_info *fbi)
{
	struct mx3fb_info *mx3_fbi = fbi->par;
	struct mx3fb_data *mx3fb = mx3_fbi->mx3fb;
	struct idmac_channel *ichan = mx3_fbi->idmac_channel;
	int ret;

	dev_dbg(mx3fb->dev, "%s [%c]\n", __func__, list_empty(&ichan->queue) ? '-' : '+');

	mutex_lock(&mx3_fbi->mutex);

	ret = mx3fb_must_set_par(fbi) ? __set_par(fbi, true) : 0;

	mutex_unlock(&mx3_fbi->mutex);

	return ret;
}

/**
 * mx3fb_check_var() - check and adjust framebuffer variable parameters.
 * @var:	framebuffer variable parameters
 * @fbi:	framebuffer information pointer
 */
static int mx3fb_check_var(struct fb_var_screeninfo *var, struct fb_info *fbi)
{
	struct mx3fb_info *mx3_fbi = fbi->par;
	u32 vtotal;
	u32 htotal;

	dev_dbg(fbi->device, "%s\n", __func__);

	if (var->xres_virtual < var->xres)
		var->xres_virtual = var->xres;
	if (var->yres_virtual < var->yres)
		var->yres_virtual = var->yres;

	if ((var->bits_per_pixel != 32) && (var->bits_per_pixel != 24) &&
	    (var->bits_per_pixel != 16))
		var->bits_per_pixel = default_bpp;

	switch (var->bits_per_pixel) {
	case 16:
		var->red.length = 5;
		var->red.offset = 11;
		var->red.msb_right = 0;

		var->green.length = 6;
		var->green.offset = 5;
		var->green.msb_right = 0;

		var->blue.length = 5;
		var->blue.offset = 0;
		var->blue.msb_right = 0;

		var->transp.length = 0;
		var->transp.offset = 0;
		var->transp.msb_right = 0;
		break;
	case 24:
		var->red.length = 8;
		var->red.offset = 16;
		var->red.msb_right = 0;

		var->green.length = 8;
		var->green.offset = 8;
		var->green.msb_right = 0;

		var->blue.length = 8;
		var->blue.offset = 0;
		var->blue.msb_right = 0;

		var->transp.length = 0;
		var->transp.offset = 0;
		var->transp.msb_right = 0;
		break;
	case 32:
		var->red.length = 8;
		var->red.offset = 16;
		var->red.msb_right = 0;

		var->green.length = 8;
		var->green.offset = 8;
		var->green.msb_right = 0;

		var->blue.length = 8;
		var->blue.offset = 0;
		var->blue.msb_right = 0;

		var->transp.length = 8;
		var->transp.offset = 24;
		var->transp.msb_right = 0;
		break;
	}

	if (var->pixclock < 1000) {
		htotal = var->xres + var->right_margin + var->hsync_len +
		    var->left_margin;
		vtotal = var->yres + var->lower_margin + var->vsync_len +
		    var->upper_margin;
		var->pixclock = (vtotal * htotal * 6UL) / 100UL;
		var->pixclock = KHZ2PICOS(var->pixclock);
		dev_dbg(fbi->device, "pixclock set for 60Hz refresh = %u ps\n",
			var->pixclock);
	}

	var->height = -1;
	var->width = -1;
	var->grayscale = 0;

	/* Preserve sync flags */
	var->sync |= mx3_fbi->cur_var.sync;
	mx3_fbi->cur_var.sync |= var->sync;

	return 0;
}

static u32 chan_to_field(unsigned int chan, struct fb_bitfield *bf)
{
	chan &= 0xffff;
	chan >>= 16 - bf->length;
	return chan << bf->offset;
}

static int mx3fb_setcolreg(unsigned int regno, unsigned int red,
			   unsigned int green, unsigned int blue,
			   unsigned int trans, struct fb_info *fbi)
{
	struct mx3fb_info *mx3_fbi = fbi->par;
	u32 val;
	int ret = 1;

	dev_dbg(fbi->device, "%s, regno = %u\n", __func__, regno);

	mutex_lock(&mx3_fbi->mutex);
	/*
	 * If greyscale is true, then we convert the RGB value
	 * to greyscale no matter what visual we are using.
	 */
	if (fbi->var.grayscale)
		red = green = blue = (19595 * red + 38470 * green +
				      7471 * blue) >> 16;
	switch (fbi->fix.visual) {
	case FB_VISUAL_TRUECOLOR:
		/*
		 * 16-bit True Colour.  We encode the RGB value
		 * according to the RGB bitfield information.
		 */
		if (regno < 16) {
			u32 *pal = fbi->pseudo_palette;

			val = chan_to_field(red, &fbi->var.red);
			val |= chan_to_field(green, &fbi->var.green);
			val |= chan_to_field(blue, &fbi->var.blue);

			pal[regno] = val;

			ret = 0;
		}
		break;

	case FB_VISUAL_STATIC_PSEUDOCOLOR:
	case FB_VISUAL_PSEUDOCOLOR:
		break;
	}
	mutex_unlock(&mx3_fbi->mutex);

	return ret;
}

static void __blank(int blank, struct fb_info *fbi)
{
	struct mx3fb_info *mx3_fbi = fbi->par;
	struct mx3fb_data *mx3fb = mx3_fbi->mx3fb;
	int was_blank = mx3_fbi->blank;

	mx3_fbi->blank = blank;

	/* Attention!
	 * Do not call sdc_disable_channel() for a channel that is disabled
	 * already! This will result in a kernel NULL pointer dereference
	 * (mx3_fbi->txd is NULL). Hide the fact, that all blank modes are
	 * handled equally by this driver.
	 */
	if (blank > FB_BLANK_UNBLANK && was_blank > FB_BLANK_UNBLANK)
		return;

	switch (blank) {
	case FB_BLANK_POWERDOWN:
	case FB_BLANK_VSYNC_SUSPEND:
	case FB_BLANK_HSYNC_SUSPEND:
	case FB_BLANK_NORMAL:
		sdc_set_brightness(mx3fb, 0);
		memset((char *)fbi->screen_base, 0, fbi->fix.smem_len);
		/* Give LCD time to update - enough for 50 and 60 Hz */
		msleep(25);
		sdc_disable_channel(mx3_fbi);
		break;
	case FB_BLANK_UNBLANK:
		sdc_enable_channel(mx3_fbi);
		sdc_set_brightness(mx3fb, mx3fb->backlight_level);
		break;
	}
}

/**
 * mx3fb_blank() - blank the display.
 * @blank:	blank value for the panel
 * @fbi:	framebuffer information pointer
 */
static int mx3fb_blank(int blank, struct fb_info *fbi)
{
	struct mx3fb_info *mx3_fbi = fbi->par;

	dev_dbg(fbi->device, "%s, blank = %d, base %p, len %u\n", __func__,
		blank, fbi->screen_base, fbi->fix.smem_len);

	if (mx3_fbi->blank == blank)
		return 0;

	mutex_lock(&mx3_fbi->mutex);
	__blank(blank, fbi);
	mutex_unlock(&mx3_fbi->mutex);

	return 0;
}

/**
 * mx3fb_pan_display() - pan or wrap the display
 * @var:	variable screen buffer information.
 * @fbi:	framebuffer information pointer.
 *
 * We look only at xoffset, yoffset and the FB_VMODE_YWRAP flag
 */
static int mx3fb_pan_display(struct fb_var_screeninfo *var,
			     struct fb_info *fbi)
{
	struct mx3fb_info *mx3_fbi = fbi->par;
	u32 y_bottom;
	unsigned long base;
	off_t offset;
	dma_cookie_t cookie;
	struct scatterlist *sg = mx3_fbi->sg;
	struct dma_chan *dma_chan = &mx3_fbi->idmac_channel->dma_chan;
	struct dma_async_tx_descriptor *txd;
	int ret;

	dev_dbg(fbi->device, "%s [%c]\n", __func__,
		list_empty(&mx3_fbi->idmac_channel->queue) ? '-' : '+');

	if (var->xoffset > 0) {
		dev_dbg(fbi->device, "x panning not supported\n");
		return -EINVAL;
	}

	if (mx3_fbi->cur_var.xoffset == var->xoffset &&
	    mx3_fbi->cur_var.yoffset == var->yoffset)
		return 0;	/* No change, do nothing */

	y_bottom = var->yoffset;

	if (!(var->vmode & FB_VMODE_YWRAP))
		y_bottom += fbi->var.yres;

	if (y_bottom > fbi->var.yres_virtual)
		return -EINVAL;

	mutex_lock(&mx3_fbi->mutex);

	offset = var->yoffset * fbi->fix.line_length
	       + var->xoffset * (fbi->var.bits_per_pixel / 8);
	base = fbi->fix.smem_start + offset;

	dev_dbg(fbi->device, "Updating SDC BG buf %d address=0x%08lX\n",
		mx3_fbi->cur_ipu_buf, base);

	/*
	 * We enable the End of Frame interrupt, which will free a tx-descriptor,
	 * which we will need for the next dmaengine_prep_slave_sg(). The
	 * IRQ-handler will disable the IRQ again.
	 */
	init_completion(&mx3_fbi->flip_cmpl);
	enable_irq(mx3_fbi->idmac_channel->eof_irq);

	ret = wait_for_completion_timeout(&mx3_fbi->flip_cmpl, HZ / 10);
	if (ret <= 0) {
		mutex_unlock(&mx3_fbi->mutex);
		dev_info(fbi->device, "Panning failed due to %s\n", ret < 0 ?
			 "user interrupt" : "timeout");
		disable_irq(mx3_fbi->idmac_channel->eof_irq);
		return ret ? : -ETIMEDOUT;
	}

	mx3_fbi->cur_ipu_buf = !mx3_fbi->cur_ipu_buf;

	sg_dma_address(&sg[mx3_fbi->cur_ipu_buf]) = base;
	sg_set_page(&sg[mx3_fbi->cur_ipu_buf],
		    virt_to_page(fbi->screen_base + offset), fbi->fix.smem_len,
		    offset_in_page(fbi->screen_base + offset));

	if (mx3_fbi->txd)
		async_tx_ack(mx3_fbi->txd);

	txd = dmaengine_prep_slave_sg(dma_chan, sg +
		mx3_fbi->cur_ipu_buf, 1, DMA_MEM_TO_DEV, DMA_PREP_INTERRUPT);
	if (!txd) {
		dev_err(fbi->device,
			"Error preparing a DMA transaction descriptor.\n");
		mutex_unlock(&mx3_fbi->mutex);
		return -EIO;
	}

	txd->callback_param	= txd;
	txd->callback		= mx3fb_dma_done;

	/*
	 * Emulate original mx3fb behaviour: each new call to idmac_tx_submit()
	 * should switch to another buffer
	 */
	cookie = txd->tx_submit(txd);
	dev_dbg(fbi->device, "%d: Submit %p #%d\n", __LINE__, txd, cookie);
	if (cookie < 0) {
		dev_err(fbi->device,
			"Error updating SDC buf %d to address=0x%08lX\n",
			mx3_fbi->cur_ipu_buf, base);
		mutex_unlock(&mx3_fbi->mutex);
		return -EIO;
	}

	mx3_fbi->txd = txd;

	fbi->var.xoffset = var->xoffset;
	fbi->var.yoffset = var->yoffset;

	if (var->vmode & FB_VMODE_YWRAP)
		fbi->var.vmode |= FB_VMODE_YWRAP;
	else
		fbi->var.vmode &= ~FB_VMODE_YWRAP;

	mx3_fbi->cur_var = fbi->var;

	mutex_unlock(&mx3_fbi->mutex);

	dev_dbg(fbi->device, "Update complete\n");

	return 0;
}

/*
 * This structure contains the pointers to the control functions that are
 * invoked by the core framebuffer driver to perform operations like
 * blitting, rectangle filling, copy regions and cursor definition.
 */
static const struct fb_ops mx3fb_ops = {
	.owner = THIS_MODULE,
	.fb_set_par = mx3fb_set_par,
	.fb_check_var = mx3fb_check_var,
	.fb_setcolreg = mx3fb_setcolreg,
	.fb_pan_display = mx3fb_pan_display,
	.fb_fillrect = cfb_fillrect,
	.fb_copyarea = cfb_copyarea,
	.fb_imageblit = cfb_imageblit,
	.fb_blank = mx3fb_blank,
};

#ifdef CONFIG_PM
/*
 * Power management hooks.      Note that we won't be called from IRQ context,
 * unlike the blank functions above, so we may sleep.
 */

/*
 * Suspends the framebuffer and blanks the screen. Power management support
 */
static int mx3fb_suspend(struct platform_device *pdev, pm_message_t state)
{
	struct mx3fb_data *mx3fb = platform_get_drvdata(pdev);
	struct mx3fb_info *mx3_fbi = mx3fb->fbi->par;

	console_lock();
	fb_set_suspend(mx3fb->fbi, 1);
	console_unlock();

	if (mx3_fbi->blank == FB_BLANK_UNBLANK) {
		sdc_disable_channel(mx3_fbi);
		sdc_set_brightness(mx3fb, 0);

	}
	return 0;
}

/*
 * Resumes the framebuffer and unblanks the screen. Power management support
 */
static int mx3fb_resume(struct platform_device *pdev)
{
	struct mx3fb_data *mx3fb = platform_get_drvdata(pdev);
	struct mx3fb_info *mx3_fbi = mx3fb->fbi->par;

	if (mx3_fbi->blank == FB_BLANK_UNBLANK) {
		sdc_enable_channel(mx3_fbi);
		sdc_set_brightness(mx3fb, mx3fb->backlight_level);
	}

	console_lock();
	fb_set_suspend(mx3fb->fbi, 0);
	console_unlock();

	return 0;
}
#else
#define mx3fb_suspend   NULL
#define mx3fb_resume    NULL
#endif

/*
 * Main framebuffer functions
 */

/**
 * mx3fb_map_video_memory() - allocates the DRAM memory for the frame buffer.
 * @fbi:	framebuffer information pointer
 * @mem_len:	length of mapped memory
 * @lock:	do not lock during initialisation
 * @return:	Error code indicating success or failure
 *
 * This buffer is remapped into a non-cached, non-buffered, memory region to
 * allow palette and pixel writes to occur without flushing the cache. Once this
 * area is remapped, all virtual memory access to the video memory should occur
 * at the new region.
 */
static int mx3fb_map_video_memory(struct fb_info *fbi, unsigned int mem_len,
				  bool lock)
{
	int retval = 0;
	dma_addr_t addr;

	fbi->screen_base = dma_alloc_wc(fbi->device, mem_len, &addr,
					GFP_DMA | GFP_KERNEL);

	if (!fbi->screen_base) {
		dev_err(fbi->device, "Cannot allocate %u bytes framebuffer memory\n",
			mem_len);
		retval = -EBUSY;
		goto err0;
	}

	if (lock)
		mutex_lock(&fbi->mm_lock);
	fbi->fix.smem_start = addr;
	fbi->fix.smem_len = mem_len;
	if (lock)
		mutex_unlock(&fbi->mm_lock);

	dev_dbg(fbi->device, "allocated fb @ p=0x%08x, v=0x%p, size=%d.\n",
		(uint32_t) fbi->fix.smem_start, fbi->screen_base, fbi->fix.smem_len);

	fbi->screen_size = fbi->fix.smem_len;

	/* Clear the screen */
	memset((char *)fbi->screen_base, 0, fbi->fix.smem_len);

	return 0;

err0:
	fbi->fix.smem_len = 0;
	fbi->fix.smem_start = 0;
	fbi->screen_base = NULL;
	return retval;
}

/**
 * mx3fb_unmap_video_memory() - de-allocate frame buffer memory.
 * @fbi:	framebuffer information pointer
 * @return:	error code indicating success or failure
 */
static int mx3fb_unmap_video_memory(struct fb_info *fbi)
{
	dma_free_wc(fbi->device, fbi->fix.smem_len, fbi->screen_base,
		    fbi->fix.smem_start);

	fbi->screen_base = NULL;
	mutex_lock(&fbi->mm_lock);
	fbi->fix.smem_start = 0;
	fbi->fix.smem_len = 0;
	mutex_unlock(&fbi->mm_lock);
	return 0;
}

/**
 * mx3fb_init_fbinfo() - initialize framebuffer information object.
 * @dev: the device
 * @ops:	framebuffer device operations
 * @return:	initialized framebuffer structure.
 */
static struct fb_info *mx3fb_init_fbinfo(struct device *dev,
					 const struct fb_ops *ops)
{
	struct fb_info *fbi;
	struct mx3fb_info *mx3fbi;
	int ret;

	/* Allocate sufficient memory for the fb structure */
	fbi = framebuffer_alloc(sizeof(struct mx3fb_info), dev);
	if (!fbi)
		return NULL;

	mx3fbi			= fbi->par;
	mx3fbi->cookie		= -EINVAL;
	mx3fbi->cur_ipu_buf	= 0;

	fbi->var.activate	= FB_ACTIVATE_NOW;

	fbi->fbops		= ops;
	fbi->pseudo_palette	= mx3fbi->pseudo_palette;

	mutex_init(&mx3fbi->mutex);

	/* Allocate colormap */
	ret = fb_alloc_cmap(&fbi->cmap, 16, 0);
	if (ret < 0) {
		framebuffer_release(fbi);
		return NULL;
	}

	return fbi;
}

static int init_fb_chan(struct mx3fb_data *mx3fb, struct idmac_channel *ichan)
{
	struct device *dev = mx3fb->dev;
	struct mx3fb_platform_data *mx3fb_pdata = dev_get_platdata(dev);
	const char *name = mx3fb_pdata->name;
	struct fb_info *fbi;
	struct mx3fb_info *mx3fbi;
	const struct fb_videomode *mode;
	int ret, num_modes;

	if (mx3fb_pdata->disp_data_fmt >= ARRAY_SIZE(di_mappings)) {
		dev_err(dev, "Illegal display data format %d\n",
				mx3fb_pdata->disp_data_fmt);
		return -EINVAL;
	}

	ichan->client = mx3fb;

	if (ichan->dma_chan.chan_id != IDMAC_SDC_0)
		return -EINVAL;

	fbi = mx3fb_init_fbinfo(dev, &mx3fb_ops);
	if (!fbi)
		return -ENOMEM;

	if (!fb_mode)
		fb_mode = name;

	if (!fb_mode) {
		ret = -EINVAL;
		goto emode;
	}

	if (mx3fb_pdata->mode && mx3fb_pdata->num_modes) {
		mode = mx3fb_pdata->mode;
		num_modes = mx3fb_pdata->num_modes;
	} else {
		mode = mx3fb_modedb;
		num_modes = ARRAY_SIZE(mx3fb_modedb);
	}

	if (!fb_find_mode(&fbi->var, fbi, fb_mode, mode,
			  num_modes, NULL, default_bpp)) {
		ret = -EBUSY;
		goto emode;
	}

	fb_videomode_to_modelist(mode, num_modes, &fbi->modelist);

	/* Default Y virtual size is 2x panel size */
	fbi->var.yres_virtual = fbi->var.yres * 2;

	mx3fb->fbi = fbi;

	/* set Display Interface clock period */
	mx3fb_write_reg(mx3fb, 0x00100010L, DI_HSP_CLK_PER);
	/* Might need to trigger HSP clock change - see 44.3.3.8.5 */

	sdc_set_brightness(mx3fb, 255);
	sdc_set_global_alpha(mx3fb, true, 0xFF);
	sdc_set_color_key(mx3fb, IDMAC_SDC_0, false, 0);

	mx3fbi			= fbi->par;
	mx3fbi->idmac_channel	= ichan;
	mx3fbi->ipu_ch		= ichan->dma_chan.chan_id;
	mx3fbi->mx3fb		= mx3fb;
	mx3fbi->blank		= FB_BLANK_NORMAL;

	mx3fb->disp_data_fmt	= mx3fb_pdata->disp_data_fmt;

	init_completion(&mx3fbi->flip_cmpl);
	disable_irq(ichan->eof_irq);
	dev_dbg(mx3fb->dev, "disabling irq %d\n", ichan->eof_irq);
	ret = __set_par(fbi, false);
	if (ret < 0)
		goto esetpar;

	__blank(FB_BLANK_UNBLANK, fbi);

	dev_info(dev, "registered, using mode %s\n", fb_mode);

	ret = register_framebuffer(fbi);
	if (ret < 0)
		goto erfb;

	return 0;

erfb:
esetpar:
emode:
	fb_dealloc_cmap(&fbi->cmap);
	framebuffer_release(fbi);

	return ret;
}

static bool chan_filter(struct dma_chan *chan, void *arg)
{
	struct dma_chan_request *rq = arg;
	struct device *dev;
	struct mx3fb_platform_data *mx3fb_pdata;

	if (!imx_dma_is_ipu(chan))
		return false;

	if (!rq)
		return false;

	dev = rq->mx3fb->dev;
	mx3fb_pdata = dev_get_platdata(dev);

	return rq->id == chan->chan_id &&
		mx3fb_pdata->dma_dev == chan->device->dev;
}

static void release_fbi(struct fb_info *fbi)
{
	mx3fb_unmap_video_memory(fbi);

	fb_dealloc_cmap(&fbi->cmap);

	unregister_framebuffer(fbi);
	framebuffer_release(fbi);
}

static int mx3fb_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	int ret;
	struct resource *sdc_reg;
	struct mx3fb_data *mx3fb;
	dma_cap_mask_t mask;
	struct dma_chan *chan;
	struct dma_chan_request rq;

	/*
	 * Display Interface (DI) and Synchronous Display Controller (SDC)
	 * registers
	 */
	sdc_reg = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!sdc_reg)
		return -EINVAL;

	mx3fb = devm_kzalloc(&pdev->dev, sizeof(*mx3fb), GFP_KERNEL);
	if (!mx3fb)
		return -ENOMEM;

	spin_lock_init(&mx3fb->lock);

	mx3fb->reg_base = ioremap(sdc_reg->start, resource_size(sdc_reg));
	if (!mx3fb->reg_base) {
		ret = -ENOMEM;
		goto eremap;
	}

	pr_debug("Remapped %pR at %p\n", sdc_reg, mx3fb->reg_base);

	/* IDMAC interface */
	dmaengine_get();

	mx3fb->dev = dev;
	platform_set_drvdata(pdev, mx3fb);

	rq.mx3fb = mx3fb;

	dma_cap_zero(mask);
	dma_cap_set(DMA_SLAVE, mask);
	dma_cap_set(DMA_PRIVATE, mask);
	rq.id = IDMAC_SDC_0;
	chan = dma_request_channel(mask, chan_filter, &rq);
	if (!chan) {
		ret = -EBUSY;
		goto ersdc0;
	}

	mx3fb->backlight_level = 255;

	ret = init_fb_chan(mx3fb, to_idmac_chan(chan));
	if (ret < 0)
		goto eisdc0;

	mx3fb_init_backlight(mx3fb);

	return 0;

eisdc0:
	dma_release_channel(chan);
ersdc0:
	dmaengine_put();
	iounmap(mx3fb->reg_base);
eremap:
	dev_err(dev, "mx3fb: failed to register fb\n");
	return ret;
}

static void mx3fb_remove(struct platform_device *dev)
{
	struct mx3fb_data *mx3fb = platform_get_drvdata(dev);
	struct fb_info *fbi = mx3fb->fbi;
	struct mx3fb_info *mx3_fbi = fbi->par;
	struct dma_chan *chan;

	chan = &mx3_fbi->idmac_channel->dma_chan;
	release_fbi(fbi);

	mx3fb_exit_backlight(mx3fb);

	dma_release_channel(chan);
	dmaengine_put();

	iounmap(mx3fb->reg_base);
}

static struct platform_driver mx3fb_driver = {
	.driver = {
		.name = MX3FB_NAME,
	},
	.probe = mx3fb_probe,
	.remove_new = mx3fb_remove,
	.suspend = mx3fb_suspend,
	.resume = mx3fb_resume,
};

/*
 * Parse user specified options (`video=mx3fb:')
 * example:
 * 	video=mx3fb:bpp=16
 */
static int __init mx3fb_setup(void)
{
#ifndef MODULE
	char *opt, *options = NULL;

	if (fb_get_options("mx3fb", &options))
		return -ENODEV;

	if (!options || !*options)
		return 0;

	while ((opt = strsep(&options, ",")) != NULL) {
		if (!*opt)
			continue;
		if (!strncmp(opt, "bpp=", 4))
			default_bpp = simple_strtoul(opt + 4, NULL, 0);
		else
			fb_mode = opt;
	}
#endif

	return 0;
}

static int __init mx3fb_init(void)
{
	int ret = mx3fb_setup();

	if (ret < 0)
		return ret;

	ret = platform_driver_register(&mx3fb_driver);
	return ret;
}

static void __exit mx3fb_exit(void)
{
	platform_driver_unregister(&mx3fb_driver);
}

module_init(mx3fb_init);
module_exit(mx3fb_exit);

MODULE_AUTHOR("Freescale Semiconductor, Inc.");
MODULE_DESCRIPTION("MX3 framebuffer driver");
MODULE_ALIAS("platform:" MX3FB_NAME);
MODULE_LICENSE("GPL v2");
