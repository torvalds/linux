/*
 * SuperH Mobile LCDC Framebuffer
 *
 * Copyright (c) 2008 Magnus Damm
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 */

#include <linux/atomic.h>
#include <linux/backlight.h>
#include <linux/clk.h>
#include <linux/console.h>
#include <linux/ctype.h>
#include <linux/dma-mapping.h>
#include <linux/delay.h>
#include <linux/gpio.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/ioctl.h>
#include <linux/kernel.h>
#include <linux/mm.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/pm_runtime.h>
#include <linux/slab.h>
#include <linux/videodev2.h>
#include <linux/vmalloc.h>

#include <video/sh_mobile_lcdc.h>

#include "sh_mobile_lcdcfb.h"

/* ----------------------------------------------------------------------------
 * Overlay register definitions
 */

#define LDBCR			0xb00
#define LDBCR_UPC(n)		(1 << ((n) + 16))
#define LDBCR_UPF(n)		(1 << ((n) + 8))
#define LDBCR_UPD(n)		(1 << ((n) + 0))
#define LDBnBSIFR(n)		(0xb20 + (n) * 0x20 + 0x00)
#define LDBBSIFR_EN		(1 << 31)
#define LDBBSIFR_VS		(1 << 29)
#define LDBBSIFR_BRSEL		(1 << 28)
#define LDBBSIFR_MX		(1 << 27)
#define LDBBSIFR_MY		(1 << 26)
#define LDBBSIFR_CV3		(3 << 24)
#define LDBBSIFR_CV2		(2 << 24)
#define LDBBSIFR_CV1		(1 << 24)
#define LDBBSIFR_CV0		(0 << 24)
#define LDBBSIFR_CV_MASK	(3 << 24)
#define LDBBSIFR_LAY_MASK	(0xff << 16)
#define LDBBSIFR_LAY_SHIFT	16
#define LDBBSIFR_ROP3_MASK	(0xff << 16)
#define LDBBSIFR_ROP3_SHIFT	16
#define LDBBSIFR_AL_PL8		(3 << 14)
#define LDBBSIFR_AL_PL1		(2 << 14)
#define LDBBSIFR_AL_PK		(1 << 14)
#define LDBBSIFR_AL_1		(0 << 14)
#define LDBBSIFR_AL_MASK	(3 << 14)
#define LDBBSIFR_SWPL		(1 << 10)
#define LDBBSIFR_SWPW		(1 << 9)
#define LDBBSIFR_SWPB		(1 << 8)
#define LDBBSIFR_RY		(1 << 7)
#define LDBBSIFR_CHRR_420	(2 << 0)
#define LDBBSIFR_CHRR_422	(1 << 0)
#define LDBBSIFR_CHRR_444	(0 << 0)
#define LDBBSIFR_RPKF_ARGB32	(0x00 << 0)
#define LDBBSIFR_RPKF_RGB16	(0x03 << 0)
#define LDBBSIFR_RPKF_RGB24	(0x0b << 0)
#define LDBBSIFR_RPKF_MASK	(0x1f << 0)
#define LDBnBSSZR(n)		(0xb20 + (n) * 0x20 + 0x04)
#define LDBBSSZR_BVSS_MASK	(0xfff << 16)
#define LDBBSSZR_BVSS_SHIFT	16
#define LDBBSSZR_BHSS_MASK	(0xfff << 0)
#define LDBBSSZR_BHSS_SHIFT	0
#define LDBnBLOCR(n)		(0xb20 + (n) * 0x20 + 0x08)
#define LDBBLOCR_CVLC_MASK	(0xfff << 16)
#define LDBBLOCR_CVLC_SHIFT	16
#define LDBBLOCR_CHLC_MASK	(0xfff << 0)
#define LDBBLOCR_CHLC_SHIFT	0
#define LDBnBSMWR(n)		(0xb20 + (n) * 0x20 + 0x0c)
#define LDBBSMWR_BSMWA_MASK	(0xffff << 16)
#define LDBBSMWR_BSMWA_SHIFT	16
#define LDBBSMWR_BSMW_MASK	(0xffff << 0)
#define LDBBSMWR_BSMW_SHIFT	0
#define LDBnBSAYR(n)		(0xb20 + (n) * 0x20 + 0x10)
#define LDBBSAYR_FG1A_MASK	(0xff << 24)
#define LDBBSAYR_FG1A_SHIFT	24
#define LDBBSAYR_FG1R_MASK	(0xff << 16)
#define LDBBSAYR_FG1R_SHIFT	16
#define LDBBSAYR_FG1G_MASK	(0xff << 8)
#define LDBBSAYR_FG1G_SHIFT	8
#define LDBBSAYR_FG1B_MASK	(0xff << 0)
#define LDBBSAYR_FG1B_SHIFT	0
#define LDBnBSACR(n)		(0xb20 + (n) * 0x20 + 0x14)
#define LDBBSACR_FG2A_MASK	(0xff << 24)
#define LDBBSACR_FG2A_SHIFT	24
#define LDBBSACR_FG2R_MASK	(0xff << 16)
#define LDBBSACR_FG2R_SHIFT	16
#define LDBBSACR_FG2G_MASK	(0xff << 8)
#define LDBBSACR_FG2G_SHIFT	8
#define LDBBSACR_FG2B_MASK	(0xff << 0)
#define LDBBSACR_FG2B_SHIFT	0
#define LDBnBSAAR(n)		(0xb20 + (n) * 0x20 + 0x18)
#define LDBBSAAR_AP_MASK	(0xff << 24)
#define LDBBSAAR_AP_SHIFT	24
#define LDBBSAAR_R_MASK		(0xff << 16)
#define LDBBSAAR_R_SHIFT	16
#define LDBBSAAR_GY_MASK	(0xff << 8)
#define LDBBSAAR_GY_SHIFT	8
#define LDBBSAAR_B_MASK		(0xff << 0)
#define LDBBSAAR_B_SHIFT	0
#define LDBnBPPCR(n)		(0xb20 + (n) * 0x20 + 0x1c)
#define LDBBPPCR_AP_MASK	(0xff << 24)
#define LDBBPPCR_AP_SHIFT	24
#define LDBBPPCR_R_MASK		(0xff << 16)
#define LDBBPPCR_R_SHIFT	16
#define LDBBPPCR_GY_MASK	(0xff << 8)
#define LDBBPPCR_GY_SHIFT	8
#define LDBBPPCR_B_MASK		(0xff << 0)
#define LDBBPPCR_B_SHIFT	0
#define LDBnBBGCL(n)		(0xb10 + (n) * 0x04)
#define LDBBBGCL_BGA_MASK	(0xff << 24)
#define LDBBBGCL_BGA_SHIFT	24
#define LDBBBGCL_BGR_MASK	(0xff << 16)
#define LDBBBGCL_BGR_SHIFT	16
#define LDBBBGCL_BGG_MASK	(0xff << 8)
#define LDBBBGCL_BGG_SHIFT	8
#define LDBBBGCL_BGB_MASK	(0xff << 0)
#define LDBBBGCL_BGB_SHIFT	0

#define SIDE_B_OFFSET 0x1000
#define MIRROR_OFFSET 0x2000

#define MAX_XRES 1920
#define MAX_YRES 1080

enum sh_mobile_lcdc_overlay_mode {
	LCDC_OVERLAY_BLEND,
	LCDC_OVERLAY_ROP3,
};

/*
 * struct sh_mobile_lcdc_overlay - LCDC display overlay
 *
 * @channel: LCDC channel this overlay belongs to
 * @cfg: Overlay configuration
 * @info: Frame buffer device
 * @index: Overlay index (0-3)
 * @base: Overlay registers base address
 * @enabled: True if the overlay is enabled
 * @mode: Overlay blending mode (alpha blend or ROP3)
 * @alpha: Global alpha blending value (0-255, for alpha blending mode)
 * @rop3: Raster operation (for ROP3 mode)
 * @fb_mem: Frame buffer virtual memory address
 * @fb_size: Frame buffer size in bytes
 * @dma_handle: Frame buffer DMA address
 * @base_addr_y: Overlay base address (RGB or luma component)
 * @base_addr_c: Overlay base address (chroma component)
 * @pan_y_offset: Panning linear offset in bytes (luma component)
 * @format: Current pixelf format
 * @xres: Horizontal visible resolution
 * @xres_virtual: Horizontal total resolution
 * @yres: Vertical visible resolution
 * @yres_virtual: Vertical total resolution
 * @pitch: Overlay line pitch
 * @pos_x: Horizontal overlay position
 * @pos_y: Vertical overlay position
 */
struct sh_mobile_lcdc_overlay {
	struct sh_mobile_lcdc_chan *channel;

	const struct sh_mobile_lcdc_overlay_cfg *cfg;
	struct fb_info *info;

	unsigned int index;
	unsigned long base;

	bool enabled;
	enum sh_mobile_lcdc_overlay_mode mode;
	unsigned int alpha;
	unsigned int rop3;

	void *fb_mem;
	unsigned long fb_size;

	dma_addr_t dma_handle;
	unsigned long base_addr_y;
	unsigned long base_addr_c;
	unsigned long pan_y_offset;

	const struct sh_mobile_lcdc_format_info *format;
	unsigned int xres;
	unsigned int xres_virtual;
	unsigned int yres;
	unsigned int yres_virtual;
	unsigned int pitch;
	int pos_x;
	int pos_y;
};

struct sh_mobile_lcdc_priv {
	void __iomem *base;
	int irq;
	atomic_t hw_usecnt;
	struct device *dev;
	struct clk *dot_clk;
	unsigned long lddckr;

	struct sh_mobile_lcdc_chan ch[2];
	struct sh_mobile_lcdc_overlay overlays[4];

	struct notifier_block notifier;
	int started;
	int forced_fourcc; /* 2 channel LCDC must share fourcc setting */
};

/* -----------------------------------------------------------------------------
 * Registers access
 */

static unsigned long lcdc_offs_mainlcd[NR_CH_REGS] = {
	[LDDCKPAT1R] = 0x400,
	[LDDCKPAT2R] = 0x404,
	[LDMT1R] = 0x418,
	[LDMT2R] = 0x41c,
	[LDMT3R] = 0x420,
	[LDDFR] = 0x424,
	[LDSM1R] = 0x428,
	[LDSM2R] = 0x42c,
	[LDSA1R] = 0x430,
	[LDSA2R] = 0x434,
	[LDMLSR] = 0x438,
	[LDHCNR] = 0x448,
	[LDHSYNR] = 0x44c,
	[LDVLNR] = 0x450,
	[LDVSYNR] = 0x454,
	[LDPMR] = 0x460,
	[LDHAJR] = 0x4a0,
};

static unsigned long lcdc_offs_sublcd[NR_CH_REGS] = {
	[LDDCKPAT1R] = 0x408,
	[LDDCKPAT2R] = 0x40c,
	[LDMT1R] = 0x600,
	[LDMT2R] = 0x604,
	[LDMT3R] = 0x608,
	[LDDFR] = 0x60c,
	[LDSM1R] = 0x610,
	[LDSM2R] = 0x614,
	[LDSA1R] = 0x618,
	[LDMLSR] = 0x620,
	[LDHCNR] = 0x624,
	[LDHSYNR] = 0x628,
	[LDVLNR] = 0x62c,
	[LDVSYNR] = 0x630,
	[LDPMR] = 0x63c,
};

static bool banked(int reg_nr)
{
	switch (reg_nr) {
	case LDMT1R:
	case LDMT2R:
	case LDMT3R:
	case LDDFR:
	case LDSM1R:
	case LDSA1R:
	case LDSA2R:
	case LDMLSR:
	case LDHCNR:
	case LDHSYNR:
	case LDVLNR:
	case LDVSYNR:
		return true;
	}
	return false;
}

static int lcdc_chan_is_sublcd(struct sh_mobile_lcdc_chan *chan)
{
	return chan->cfg->chan == LCDC_CHAN_SUBLCD;
}

static void lcdc_write_chan(struct sh_mobile_lcdc_chan *chan,
			    int reg_nr, unsigned long data)
{
	iowrite32(data, chan->lcdc->base + chan->reg_offs[reg_nr]);
	if (banked(reg_nr))
		iowrite32(data, chan->lcdc->base + chan->reg_offs[reg_nr] +
			  SIDE_B_OFFSET);
}

static void lcdc_write_chan_mirror(struct sh_mobile_lcdc_chan *chan,
			    int reg_nr, unsigned long data)
{
	iowrite32(data, chan->lcdc->base + chan->reg_offs[reg_nr] +
		  MIRROR_OFFSET);
}

static unsigned long lcdc_read_chan(struct sh_mobile_lcdc_chan *chan,
				    int reg_nr)
{
	return ioread32(chan->lcdc->base + chan->reg_offs[reg_nr]);
}

static void lcdc_write_overlay(struct sh_mobile_lcdc_overlay *ovl,
			       int reg, unsigned long data)
{
	iowrite32(data, ovl->channel->lcdc->base + reg);
	iowrite32(data, ovl->channel->lcdc->base + reg + SIDE_B_OFFSET);
}

static void lcdc_write(struct sh_mobile_lcdc_priv *priv,
		       unsigned long reg_offs, unsigned long data)
{
	iowrite32(data, priv->base + reg_offs);
}

static unsigned long lcdc_read(struct sh_mobile_lcdc_priv *priv,
			       unsigned long reg_offs)
{
	return ioread32(priv->base + reg_offs);
}

static void lcdc_wait_bit(struct sh_mobile_lcdc_priv *priv,
			  unsigned long reg_offs,
			  unsigned long mask, unsigned long until)
{
	while ((lcdc_read(priv, reg_offs) & mask) != until)
		cpu_relax();
}

/* -----------------------------------------------------------------------------
 * Clock management
 */

static void sh_mobile_lcdc_clk_on(struct sh_mobile_lcdc_priv *priv)
{
	if (atomic_inc_and_test(&priv->hw_usecnt)) {
		if (priv->dot_clk)
			clk_prepare_enable(priv->dot_clk);
		pm_runtime_get_sync(priv->dev);
	}
}

static void sh_mobile_lcdc_clk_off(struct sh_mobile_lcdc_priv *priv)
{
	if (atomic_sub_return(1, &priv->hw_usecnt) == -1) {
		pm_runtime_put(priv->dev);
		if (priv->dot_clk)
			clk_disable_unprepare(priv->dot_clk);
	}
}

static int sh_mobile_lcdc_setup_clocks(struct sh_mobile_lcdc_priv *priv,
				       int clock_source)
{
	struct clk *clk;
	char *str;

	switch (clock_source) {
	case LCDC_CLK_BUS:
		str = "bus_clk";
		priv->lddckr = LDDCKR_ICKSEL_BUS;
		break;
	case LCDC_CLK_PERIPHERAL:
		str = "peripheral_clk";
		priv->lddckr = LDDCKR_ICKSEL_MIPI;
		break;
	case LCDC_CLK_EXTERNAL:
		str = NULL;
		priv->lddckr = LDDCKR_ICKSEL_HDMI;
		break;
	default:
		return -EINVAL;
	}

	if (str == NULL)
		return 0;

	clk = clk_get(priv->dev, str);
	if (IS_ERR(clk)) {
		dev_err(priv->dev, "cannot get dot clock %s\n", str);
		return PTR_ERR(clk);
	}

	priv->dot_clk = clk;
	return 0;
}

/* -----------------------------------------------------------------------------
 * Display, panel and deferred I/O
 */

static void lcdc_sys_write_index(void *handle, unsigned long data)
{
	struct sh_mobile_lcdc_chan *ch = handle;

	lcdc_write(ch->lcdc, _LDDWD0R, data | LDDWDxR_WDACT);
	lcdc_wait_bit(ch->lcdc, _LDSR, LDSR_AS, 0);
	lcdc_write(ch->lcdc, _LDDWAR, LDDWAR_WA |
		   (lcdc_chan_is_sublcd(ch) ? 2 : 0));
	lcdc_wait_bit(ch->lcdc, _LDSR, LDSR_AS, 0);
}

static void lcdc_sys_write_data(void *handle, unsigned long data)
{
	struct sh_mobile_lcdc_chan *ch = handle;

	lcdc_write(ch->lcdc, _LDDWD0R, data | LDDWDxR_WDACT | LDDWDxR_RSW);
	lcdc_wait_bit(ch->lcdc, _LDSR, LDSR_AS, 0);
	lcdc_write(ch->lcdc, _LDDWAR, LDDWAR_WA |
		   (lcdc_chan_is_sublcd(ch) ? 2 : 0));
	lcdc_wait_bit(ch->lcdc, _LDSR, LDSR_AS, 0);
}

static unsigned long lcdc_sys_read_data(void *handle)
{
	struct sh_mobile_lcdc_chan *ch = handle;

	lcdc_write(ch->lcdc, _LDDRDR, LDDRDR_RSR);
	lcdc_wait_bit(ch->lcdc, _LDSR, LDSR_AS, 0);
	lcdc_write(ch->lcdc, _LDDRAR, LDDRAR_RA |
		   (lcdc_chan_is_sublcd(ch) ? 2 : 0));
	udelay(1);
	lcdc_wait_bit(ch->lcdc, _LDSR, LDSR_AS, 0);

	return lcdc_read(ch->lcdc, _LDDRDR) & LDDRDR_DRD_MASK;
}

static struct sh_mobile_lcdc_sys_bus_ops sh_mobile_lcdc_sys_bus_ops = {
	.write_index	= lcdc_sys_write_index,
	.write_data	= lcdc_sys_write_data,
	.read_data	= lcdc_sys_read_data,
};

static int sh_mobile_lcdc_sginit(struct fb_info *info,
				  struct list_head *pagelist)
{
	struct sh_mobile_lcdc_chan *ch = info->par;
	unsigned int nr_pages_max = ch->fb_size >> PAGE_SHIFT;
	struct page *page;
	int nr_pages = 0;

	sg_init_table(ch->sglist, nr_pages_max);

	list_for_each_entry(page, pagelist, lru)
		sg_set_page(&ch->sglist[nr_pages++], page, PAGE_SIZE, 0);

	return nr_pages;
}

static void sh_mobile_lcdc_deferred_io(struct fb_info *info,
				       struct list_head *pagelist)
{
	struct sh_mobile_lcdc_chan *ch = info->par;
	const struct sh_mobile_lcdc_panel_cfg *panel = &ch->cfg->panel_cfg;

	/* enable clocks before accessing hardware */
	sh_mobile_lcdc_clk_on(ch->lcdc);

	/*
	 * It's possible to get here without anything on the pagelist via
	 * sh_mobile_lcdc_deferred_io_touch() or via a userspace fsync()
	 * invocation. In the former case, the acceleration routines are
	 * stepped in to when using the framebuffer console causing the
	 * workqueue to be scheduled without any dirty pages on the list.
	 *
	 * Despite this, a panel update is still needed given that the
	 * acceleration routines have their own methods for writing in
	 * that still need to be updated.
	 *
	 * The fsync() and empty pagelist case could be optimized for,
	 * but we don't bother, as any application exhibiting such
	 * behaviour is fundamentally broken anyways.
	 */
	if (!list_empty(pagelist)) {
		unsigned int nr_pages = sh_mobile_lcdc_sginit(info, pagelist);

		/* trigger panel update */
		dma_map_sg(ch->lcdc->dev, ch->sglist, nr_pages, DMA_TO_DEVICE);
		if (panel->start_transfer)
			panel->start_transfer(ch, &sh_mobile_lcdc_sys_bus_ops);
		lcdc_write_chan(ch, LDSM2R, LDSM2R_OSTRG);
		dma_unmap_sg(ch->lcdc->dev, ch->sglist, nr_pages,
			     DMA_TO_DEVICE);
	} else {
		if (panel->start_transfer)
			panel->start_transfer(ch, &sh_mobile_lcdc_sys_bus_ops);
		lcdc_write_chan(ch, LDSM2R, LDSM2R_OSTRG);
	}
}

static void sh_mobile_lcdc_deferred_io_touch(struct fb_info *info)
{
	struct fb_deferred_io *fbdefio = info->fbdefio;

	if (fbdefio)
		schedule_delayed_work(&info->deferred_work, fbdefio->delay);
}

static void sh_mobile_lcdc_display_on(struct sh_mobile_lcdc_chan *ch)
{
	const struct sh_mobile_lcdc_panel_cfg *panel = &ch->cfg->panel_cfg;

	if (ch->tx_dev) {
		int ret;

		ret = ch->tx_dev->ops->display_on(ch->tx_dev);
		if (ret < 0)
			return;

		if (ret == SH_MOBILE_LCDC_DISPLAY_DISCONNECTED)
			ch->info->state = FBINFO_STATE_SUSPENDED;
	}

	/* HDMI must be enabled before LCDC configuration */
	if (panel->display_on)
		panel->display_on();
}

static void sh_mobile_lcdc_display_off(struct sh_mobile_lcdc_chan *ch)
{
	const struct sh_mobile_lcdc_panel_cfg *panel = &ch->cfg->panel_cfg;

	if (panel->display_off)
		panel->display_off();

	if (ch->tx_dev)
		ch->tx_dev->ops->display_off(ch->tx_dev);
}

static bool
sh_mobile_lcdc_must_reconfigure(struct sh_mobile_lcdc_chan *ch,
				const struct fb_videomode *new_mode)
{
	dev_dbg(ch->info->dev, "Old %ux%u, new %ux%u\n",
		ch->display.mode.xres, ch->display.mode.yres,
		new_mode->xres, new_mode->yres);

	/* It can be a different monitor with an equal video-mode */
	if (fb_mode_is_equal(&ch->display.mode, new_mode))
		return false;

	dev_dbg(ch->info->dev, "Switching %u -> %u lines\n",
		ch->display.mode.yres, new_mode->yres);
	ch->display.mode = *new_mode;

	return true;
}

static int sh_mobile_lcdc_check_var(struct fb_var_screeninfo *var,
				    struct fb_info *info);

static int sh_mobile_lcdc_display_notify(struct sh_mobile_lcdc_chan *ch,
					 enum sh_mobile_lcdc_entity_event event,
					 const struct fb_videomode *mode,
					 const struct fb_monspecs *monspec)
{
	struct fb_info *info = ch->info;
	struct fb_var_screeninfo var;
	int ret = 0;

	switch (event) {
	case SH_MOBILE_LCDC_EVENT_DISPLAY_CONNECT:
		/* HDMI plug in */
		console_lock();
		if (lock_fb_info(info)) {


			ch->display.width = monspec->max_x * 10;
			ch->display.height = monspec->max_y * 10;

			if (!sh_mobile_lcdc_must_reconfigure(ch, mode) &&
			    info->state == FBINFO_STATE_RUNNING) {
				/* First activation with the default monitor.
				 * Just turn on, if we run a resume here, the
				 * logo disappears.
				 */
				info->var.width = ch->display.width;
				info->var.height = ch->display.height;
				sh_mobile_lcdc_display_on(ch);
			} else {
				/* New monitor or have to wake up */
				fb_set_suspend(info, 0);
			}


			unlock_fb_info(info);
		}
		console_unlock();
		break;

	case SH_MOBILE_LCDC_EVENT_DISPLAY_DISCONNECT:
		/* HDMI disconnect */
		console_lock();
		if (lock_fb_info(info)) {
			fb_set_suspend(info, 1);
			unlock_fb_info(info);
		}
		console_unlock();
		break;

	case SH_MOBILE_LCDC_EVENT_DISPLAY_MODE:
		/* Validate a proposed new mode */
		fb_videomode_to_var(&var, mode);
		var.bits_per_pixel = info->var.bits_per_pixel;
		var.grayscale = info->var.grayscale;
		ret = sh_mobile_lcdc_check_var(&var, info);
		break;
	}

	return ret;
}

/* -----------------------------------------------------------------------------
 * Format helpers
 */

struct sh_mobile_lcdc_format_info {
	u32 fourcc;
	unsigned int bpp;
	bool yuv;
	u32 lddfr;
};

static const struct sh_mobile_lcdc_format_info sh_mobile_format_infos[] = {
	{
		.fourcc = V4L2_PIX_FMT_RGB565,
		.bpp = 16,
		.yuv = false,
		.lddfr = LDDFR_PKF_RGB16,
	}, {
		.fourcc = V4L2_PIX_FMT_BGR24,
		.bpp = 24,
		.yuv = false,
		.lddfr = LDDFR_PKF_RGB24,
	}, {
		.fourcc = V4L2_PIX_FMT_BGR32,
		.bpp = 32,
		.yuv = false,
		.lddfr = LDDFR_PKF_ARGB32,
	}, {
		.fourcc = V4L2_PIX_FMT_NV12,
		.bpp = 12,
		.yuv = true,
		.lddfr = LDDFR_CC | LDDFR_YF_420,
	}, {
		.fourcc = V4L2_PIX_FMT_NV21,
		.bpp = 12,
		.yuv = true,
		.lddfr = LDDFR_CC | LDDFR_YF_420,
	}, {
		.fourcc = V4L2_PIX_FMT_NV16,
		.bpp = 16,
		.yuv = true,
		.lddfr = LDDFR_CC | LDDFR_YF_422,
	}, {
		.fourcc = V4L2_PIX_FMT_NV61,
		.bpp = 16,
		.yuv = true,
		.lddfr = LDDFR_CC | LDDFR_YF_422,
	}, {
		.fourcc = V4L2_PIX_FMT_NV24,
		.bpp = 24,
		.yuv = true,
		.lddfr = LDDFR_CC | LDDFR_YF_444,
	}, {
		.fourcc = V4L2_PIX_FMT_NV42,
		.bpp = 24,
		.yuv = true,
		.lddfr = LDDFR_CC | LDDFR_YF_444,
	},
};

static const struct sh_mobile_lcdc_format_info *
sh_mobile_format_info(u32 fourcc)
{
	unsigned int i;

	for (i = 0; i < ARRAY_SIZE(sh_mobile_format_infos); ++i) {
		if (sh_mobile_format_infos[i].fourcc == fourcc)
			return &sh_mobile_format_infos[i];
	}

	return NULL;
}

static int sh_mobile_format_fourcc(const struct fb_var_screeninfo *var)
{
	if (var->grayscale > 1)
		return var->grayscale;

	switch (var->bits_per_pixel) {
	case 16:
		return V4L2_PIX_FMT_RGB565;
	case 24:
		return V4L2_PIX_FMT_BGR24;
	case 32:
		return V4L2_PIX_FMT_BGR32;
	default:
		return 0;
	}
}

static int sh_mobile_format_is_fourcc(const struct fb_var_screeninfo *var)
{
	return var->grayscale > 1;
}

/* -----------------------------------------------------------------------------
 * Start, stop and IRQ
 */

static irqreturn_t sh_mobile_lcdc_irq(int irq, void *data)
{
	struct sh_mobile_lcdc_priv *priv = data;
	struct sh_mobile_lcdc_chan *ch;
	unsigned long ldintr;
	int is_sub;
	int k;

	/* Acknowledge interrupts and disable further VSYNC End IRQs. */
	ldintr = lcdc_read(priv, _LDINTR);
	lcdc_write(priv, _LDINTR, (ldintr ^ LDINTR_STATUS_MASK) & ~LDINTR_VEE);

	/* figure out if this interrupt is for main or sub lcd */
	is_sub = (lcdc_read(priv, _LDSR) & LDSR_MSS) ? 1 : 0;

	/* wake up channel and disable clocks */
	for (k = 0; k < ARRAY_SIZE(priv->ch); k++) {
		ch = &priv->ch[k];

		if (!ch->enabled)
			continue;

		/* Frame End */
		if (ldintr & LDINTR_FS) {
			if (is_sub == lcdc_chan_is_sublcd(ch)) {
				ch->frame_end = 1;
				wake_up(&ch->frame_end_wait);

				sh_mobile_lcdc_clk_off(priv);
			}
		}

		/* VSYNC End */
		if (ldintr & LDINTR_VES)
			complete(&ch->vsync_completion);
	}

	return IRQ_HANDLED;
}

static int sh_mobile_lcdc_wait_for_vsync(struct sh_mobile_lcdc_chan *ch)
{
	unsigned long ldintr;
	int ret;

	/* Enable VSync End interrupt and be careful not to acknowledge any
	 * pending interrupt.
	 */
	ldintr = lcdc_read(ch->lcdc, _LDINTR);
	ldintr |= LDINTR_VEE | LDINTR_STATUS_MASK;
	lcdc_write(ch->lcdc, _LDINTR, ldintr);

	ret = wait_for_completion_interruptible_timeout(&ch->vsync_completion,
							msecs_to_jiffies(100));
	if (!ret)
		return -ETIMEDOUT;

	return 0;
}

static void sh_mobile_lcdc_start_stop(struct sh_mobile_lcdc_priv *priv,
				      int start)
{
	unsigned long tmp = lcdc_read(priv, _LDCNT2R);
	int k;

	/* start or stop the lcdc */
	if (start)
		lcdc_write(priv, _LDCNT2R, tmp | LDCNT2R_DO);
	else
		lcdc_write(priv, _LDCNT2R, tmp & ~LDCNT2R_DO);

	/* wait until power is applied/stopped on all channels */
	for (k = 0; k < ARRAY_SIZE(priv->ch); k++)
		if (lcdc_read(priv, _LDCNT2R) & priv->ch[k].enabled)
			while (1) {
				tmp = lcdc_read_chan(&priv->ch[k], LDPMR)
				    & LDPMR_LPS;
				if (start && tmp == LDPMR_LPS)
					break;
				if (!start && tmp == 0)
					break;
				cpu_relax();
			}

	if (!start)
		lcdc_write(priv, _LDDCKSTPR, 1); /* stop dotclock */
}

static void sh_mobile_lcdc_geometry(struct sh_mobile_lcdc_chan *ch)
{
	const struct fb_var_screeninfo *var = &ch->info->var;
	const struct fb_videomode *mode = &ch->display.mode;
	unsigned long h_total, hsync_pos, display_h_total;
	u32 tmp;

	tmp = ch->ldmt1r_value;
	tmp |= (var->sync & FB_SYNC_VERT_HIGH_ACT) ? 0 : LDMT1R_VPOL;
	tmp |= (var->sync & FB_SYNC_HOR_HIGH_ACT) ? 0 : LDMT1R_HPOL;
	tmp |= (ch->cfg->flags & LCDC_FLAGS_DWPOL) ? LDMT1R_DWPOL : 0;
	tmp |= (ch->cfg->flags & LCDC_FLAGS_DIPOL) ? LDMT1R_DIPOL : 0;
	tmp |= (ch->cfg->flags & LCDC_FLAGS_DAPOL) ? LDMT1R_DAPOL : 0;
	tmp |= (ch->cfg->flags & LCDC_FLAGS_HSCNT) ? LDMT1R_HSCNT : 0;
	tmp |= (ch->cfg->flags & LCDC_FLAGS_DWCNT) ? LDMT1R_DWCNT : 0;
	lcdc_write_chan(ch, LDMT1R, tmp);

	/* setup SYS bus */
	lcdc_write_chan(ch, LDMT2R, ch->cfg->sys_bus_cfg.ldmt2r);
	lcdc_write_chan(ch, LDMT3R, ch->cfg->sys_bus_cfg.ldmt3r);

	/* horizontal configuration */
	h_total = mode->xres + mode->hsync_len + mode->left_margin
		+ mode->right_margin;
	tmp = h_total / 8; /* HTCN */
	tmp |= (min(mode->xres, ch->xres) / 8) << 16; /* HDCN */
	lcdc_write_chan(ch, LDHCNR, tmp);

	hsync_pos = mode->xres + mode->right_margin;
	tmp = hsync_pos / 8; /* HSYNP */
	tmp |= (mode->hsync_len / 8) << 16; /* HSYNW */
	lcdc_write_chan(ch, LDHSYNR, tmp);

	/* vertical configuration */
	tmp = mode->yres + mode->vsync_len + mode->upper_margin
	    + mode->lower_margin; /* VTLN */
	tmp |= min(mode->yres, ch->yres) << 16; /* VDLN */
	lcdc_write_chan(ch, LDVLNR, tmp);

	tmp = mode->yres + mode->lower_margin; /* VSYNP */
	tmp |= mode->vsync_len << 16; /* VSYNW */
	lcdc_write_chan(ch, LDVSYNR, tmp);

	/* Adjust horizontal synchronisation for HDMI */
	display_h_total = mode->xres + mode->hsync_len + mode->left_margin
			+ mode->right_margin;
	tmp = ((mode->xres & 7) << 24) | ((display_h_total & 7) << 16)
	    | ((mode->hsync_len & 7) << 8) | (hsync_pos & 7);
	lcdc_write_chan(ch, LDHAJR, tmp);
	lcdc_write_chan_mirror(ch, LDHAJR, tmp);
}

static void sh_mobile_lcdc_overlay_setup(struct sh_mobile_lcdc_overlay *ovl)
{
	u32 format = 0;

	if (!ovl->enabled) {
		lcdc_write(ovl->channel->lcdc, LDBCR, LDBCR_UPC(ovl->index));
		lcdc_write_overlay(ovl, LDBnBSIFR(ovl->index), 0);
		lcdc_write(ovl->channel->lcdc, LDBCR,
			   LDBCR_UPF(ovl->index) | LDBCR_UPD(ovl->index));
		return;
	}

	ovl->base_addr_y = ovl->dma_handle;
	ovl->base_addr_c = ovl->dma_handle
			 + ovl->xres_virtual * ovl->yres_virtual;

	switch (ovl->mode) {
	case LCDC_OVERLAY_BLEND:
		format = LDBBSIFR_EN | (ovl->alpha << LDBBSIFR_LAY_SHIFT);
		break;

	case LCDC_OVERLAY_ROP3:
		format = LDBBSIFR_EN | LDBBSIFR_BRSEL
		       | (ovl->rop3 << LDBBSIFR_ROP3_SHIFT);
		break;
	}

	switch (ovl->format->fourcc) {
	case V4L2_PIX_FMT_RGB565:
	case V4L2_PIX_FMT_NV21:
	case V4L2_PIX_FMT_NV61:
	case V4L2_PIX_FMT_NV42:
		format |= LDBBSIFR_SWPL | LDBBSIFR_SWPW;
		break;
	case V4L2_PIX_FMT_BGR24:
	case V4L2_PIX_FMT_NV12:
	case V4L2_PIX_FMT_NV16:
	case V4L2_PIX_FMT_NV24:
		format |= LDBBSIFR_SWPL | LDBBSIFR_SWPW | LDBBSIFR_SWPB;
		break;
	case V4L2_PIX_FMT_BGR32:
	default:
		format |= LDBBSIFR_SWPL;
		break;
	}

	switch (ovl->format->fourcc) {
	case V4L2_PIX_FMT_RGB565:
		format |= LDBBSIFR_AL_1 | LDBBSIFR_RY | LDBBSIFR_RPKF_RGB16;
		break;
	case V4L2_PIX_FMT_BGR24:
		format |= LDBBSIFR_AL_1 | LDBBSIFR_RY | LDBBSIFR_RPKF_RGB24;
		break;
	case V4L2_PIX_FMT_BGR32:
		format |= LDBBSIFR_AL_PK | LDBBSIFR_RY | LDDFR_PKF_ARGB32;
		break;
	case V4L2_PIX_FMT_NV12:
	case V4L2_PIX_FMT_NV21:
		format |= LDBBSIFR_AL_1 | LDBBSIFR_CHRR_420;
		break;
	case V4L2_PIX_FMT_NV16:
	case V4L2_PIX_FMT_NV61:
		format |= LDBBSIFR_AL_1 | LDBBSIFR_CHRR_422;
		break;
	case V4L2_PIX_FMT_NV24:
	case V4L2_PIX_FMT_NV42:
		format |= LDBBSIFR_AL_1 | LDBBSIFR_CHRR_444;
		break;
	}

	lcdc_write(ovl->channel->lcdc, LDBCR, LDBCR_UPC(ovl->index));

	lcdc_write_overlay(ovl, LDBnBSIFR(ovl->index), format);

	lcdc_write_overlay(ovl, LDBnBSSZR(ovl->index),
		(ovl->yres << LDBBSSZR_BVSS_SHIFT) |
		(ovl->xres << LDBBSSZR_BHSS_SHIFT));
	lcdc_write_overlay(ovl, LDBnBLOCR(ovl->index),
		(ovl->pos_y << LDBBLOCR_CVLC_SHIFT) |
		(ovl->pos_x << LDBBLOCR_CHLC_SHIFT));
	lcdc_write_overlay(ovl, LDBnBSMWR(ovl->index),
		ovl->pitch << LDBBSMWR_BSMW_SHIFT);

	lcdc_write_overlay(ovl, LDBnBSAYR(ovl->index), ovl->base_addr_y);
	lcdc_write_overlay(ovl, LDBnBSACR(ovl->index), ovl->base_addr_c);

	lcdc_write(ovl->channel->lcdc, LDBCR,
		   LDBCR_UPF(ovl->index) | LDBCR_UPD(ovl->index));
}

/*
 * __sh_mobile_lcdc_start - Configure and start the LCDC
 * @priv: LCDC device
 *
 * Configure all enabled channels and start the LCDC device. All external
 * devices (clocks, MERAM, panels, ...) are not touched by this function.
 */
static void __sh_mobile_lcdc_start(struct sh_mobile_lcdc_priv *priv)
{
	struct sh_mobile_lcdc_chan *ch;
	unsigned long tmp;
	int k, m;

	/* Enable LCDC channels. Read data from external memory, avoid using the
	 * BEU for now.
	 */
	lcdc_write(priv, _LDCNT2R, priv->ch[0].enabled | priv->ch[1].enabled);

	/* Stop the LCDC first and disable all interrupts. */
	sh_mobile_lcdc_start_stop(priv, 0);
	lcdc_write(priv, _LDINTR, 0);

	/* Configure power supply, dot clocks and start them. */
	tmp = priv->lddckr;
	for (k = 0; k < ARRAY_SIZE(priv->ch); k++) {
		ch = &priv->ch[k];
		if (!ch->enabled)
			continue;

		/* Power supply */
		lcdc_write_chan(ch, LDPMR, 0);

		m = ch->cfg->clock_divider;
		if (!m)
			continue;

		/* FIXME: sh7724 can only use 42, 48, 54 and 60 for the divider
		 * denominator.
		 */
		lcdc_write_chan(ch, LDDCKPAT1R, 0);
		lcdc_write_chan(ch, LDDCKPAT2R, (1 << (m/2)) - 1);

		if (m == 1)
			m = LDDCKR_MOSEL;
		tmp |= m << (lcdc_chan_is_sublcd(ch) ? 8 : 0);
	}

	lcdc_write(priv, _LDDCKR, tmp);
	lcdc_write(priv, _LDDCKSTPR, 0);
	lcdc_wait_bit(priv, _LDDCKSTPR, ~0, 0);

	/* Setup geometry, format, frame buffer memory and operation mode. */
	for (k = 0; k < ARRAY_SIZE(priv->ch); k++) {
		ch = &priv->ch[k];
		if (!ch->enabled)
			continue;

		sh_mobile_lcdc_geometry(ch);

		tmp = ch->format->lddfr;

		if (ch->format->yuv) {
			switch (ch->colorspace) {
			case V4L2_COLORSPACE_REC709:
				tmp |= LDDFR_CF1;
				break;
			case V4L2_COLORSPACE_JPEG:
				tmp |= LDDFR_CF0;
				break;
			}
		}

		lcdc_write_chan(ch, LDDFR, tmp);
		lcdc_write_chan(ch, LDMLSR, ch->line_size);
		lcdc_write_chan(ch, LDSA1R, ch->base_addr_y);
		if (ch->format->yuv)
			lcdc_write_chan(ch, LDSA2R, ch->base_addr_c);

		/* When using deferred I/O mode, configure the LCDC for one-shot
		 * operation and enable the frame end interrupt. Otherwise use
		 * continuous read mode.
		 */
		if (ch->ldmt1r_value & LDMT1R_IFM &&
		    ch->cfg->sys_bus_cfg.deferred_io_msec) {
			lcdc_write_chan(ch, LDSM1R, LDSM1R_OS);
			lcdc_write(priv, _LDINTR, LDINTR_FE);
		} else {
			lcdc_write_chan(ch, LDSM1R, 0);
		}
	}

	/* Word and long word swap. */
	switch (priv->ch[0].format->fourcc) {
	case V4L2_PIX_FMT_RGB565:
	case V4L2_PIX_FMT_NV21:
	case V4L2_PIX_FMT_NV61:
	case V4L2_PIX_FMT_NV42:
		tmp = LDDDSR_LS | LDDDSR_WS;
		break;
	case V4L2_PIX_FMT_BGR24:
	case V4L2_PIX_FMT_NV12:
	case V4L2_PIX_FMT_NV16:
	case V4L2_PIX_FMT_NV24:
		tmp = LDDDSR_LS | LDDDSR_WS | LDDDSR_BS;
		break;
	case V4L2_PIX_FMT_BGR32:
	default:
		tmp = LDDDSR_LS;
		break;
	}
	lcdc_write(priv, _LDDDSR, tmp);

	/* Enable the display output. */
	lcdc_write(priv, _LDCNT1R, LDCNT1R_DE);
	sh_mobile_lcdc_start_stop(priv, 1);
	priv->started = 1;
}

static int sh_mobile_lcdc_start(struct sh_mobile_lcdc_priv *priv)
{
	struct sh_mobile_lcdc_chan *ch;
	unsigned long tmp;
	int ret;
	int k;

	/* enable clocks before accessing the hardware */
	for (k = 0; k < ARRAY_SIZE(priv->ch); k++) {
		if (priv->ch[k].enabled)
			sh_mobile_lcdc_clk_on(priv);
	}

	/* reset */
	lcdc_write(priv, _LDCNT2R, lcdc_read(priv, _LDCNT2R) | LDCNT2R_BR);
	lcdc_wait_bit(priv, _LDCNT2R, LDCNT2R_BR, 0);

	for (k = 0; k < ARRAY_SIZE(priv->ch); k++) {
		const struct sh_mobile_lcdc_panel_cfg *panel;

		ch = &priv->ch[k];
		if (!ch->enabled)
			continue;

		panel = &ch->cfg->panel_cfg;
		if (panel->setup_sys) {
			ret = panel->setup_sys(ch, &sh_mobile_lcdc_sys_bus_ops);
			if (ret)
				return ret;
		}
	}

	/* Compute frame buffer base address and pitch for each channel. */
	for (k = 0; k < ARRAY_SIZE(priv->ch); k++) {
		ch = &priv->ch[k];
		if (!ch->enabled)
			continue;

		ch->base_addr_y = ch->dma_handle;
		ch->base_addr_c = ch->dma_handle
				+ ch->xres_virtual * ch->yres_virtual;
		ch->line_size = ch->pitch;
	}

	for (k = 0; k < ARRAY_SIZE(priv->overlays); ++k) {
		struct sh_mobile_lcdc_overlay *ovl = &priv->overlays[k];
		sh_mobile_lcdc_overlay_setup(ovl);
	}

	/* Start the LCDC. */
	__sh_mobile_lcdc_start(priv);

	/* Setup deferred I/O, tell the board code to enable the panels, and
	 * turn backlight on.
	 */
	for (k = 0; k < ARRAY_SIZE(priv->ch); k++) {
		ch = &priv->ch[k];
		if (!ch->enabled)
			continue;

		tmp = ch->cfg->sys_bus_cfg.deferred_io_msec;
		if (ch->ldmt1r_value & LDMT1R_IFM && tmp) {
			ch->defio.deferred_io = sh_mobile_lcdc_deferred_io;
			ch->defio.delay = msecs_to_jiffies(tmp);
			ch->info->fbdefio = &ch->defio;
			fb_deferred_io_init(ch->info);
		}

		sh_mobile_lcdc_display_on(ch);

		if (ch->bl) {
			ch->bl->props.power = FB_BLANK_UNBLANK;
			backlight_update_status(ch->bl);
		}
	}

	return 0;
}

static void sh_mobile_lcdc_stop(struct sh_mobile_lcdc_priv *priv)
{
	struct sh_mobile_lcdc_chan *ch;
	int k;

	/* clean up deferred io and ask board code to disable panel */
	for (k = 0; k < ARRAY_SIZE(priv->ch); k++) {
		ch = &priv->ch[k];
		if (!ch->enabled)
			continue;

		/* deferred io mode:
		 * flush frame, and wait for frame end interrupt
		 * clean up deferred io and enable clock
		 */
		if (ch->info && ch->info->fbdefio) {
			ch->frame_end = 0;
			schedule_delayed_work(&ch->info->deferred_work, 0);
			wait_event(ch->frame_end_wait, ch->frame_end);
			fb_deferred_io_cleanup(ch->info);
			ch->info->fbdefio = NULL;
			sh_mobile_lcdc_clk_on(priv);
		}

		if (ch->bl) {
			ch->bl->props.power = FB_BLANK_POWERDOWN;
			backlight_update_status(ch->bl);
		}

		sh_mobile_lcdc_display_off(ch);
	}

	/* stop the lcdc */
	if (priv->started) {
		sh_mobile_lcdc_start_stop(priv, 0);
		priv->started = 0;
	}

	/* stop clocks */
	for (k = 0; k < ARRAY_SIZE(priv->ch); k++)
		if (priv->ch[k].enabled)
			sh_mobile_lcdc_clk_off(priv);
}

static int __sh_mobile_lcdc_check_var(struct fb_var_screeninfo *var,
				      struct fb_info *info)
{
	if (var->xres > MAX_XRES || var->yres > MAX_YRES)
		return -EINVAL;

	/* Make sure the virtual resolution is at least as big as the visible
	 * resolution.
	 */
	if (var->xres_virtual < var->xres)
		var->xres_virtual = var->xres;
	if (var->yres_virtual < var->yres)
		var->yres_virtual = var->yres;

	if (sh_mobile_format_is_fourcc(var)) {
		const struct sh_mobile_lcdc_format_info *format;

		format = sh_mobile_format_info(var->grayscale);
		if (format == NULL)
			return -EINVAL;
		var->bits_per_pixel = format->bpp;

		/* Default to RGB and JPEG color-spaces for RGB and YUV formats
		 * respectively.
		 */
		if (!format->yuv)
			var->colorspace = V4L2_COLORSPACE_SRGB;
		else if (var->colorspace != V4L2_COLORSPACE_REC709)
			var->colorspace = V4L2_COLORSPACE_JPEG;
	} else {
		if (var->bits_per_pixel <= 16) {		/* RGB 565 */
			var->bits_per_pixel = 16;
			var->red.offset = 11;
			var->red.length = 5;
			var->green.offset = 5;
			var->green.length = 6;
			var->blue.offset = 0;
			var->blue.length = 5;
			var->transp.offset = 0;
			var->transp.length = 0;
		} else if (var->bits_per_pixel <= 24) {		/* RGB 888 */
			var->bits_per_pixel = 24;
			var->red.offset = 16;
			var->red.length = 8;
			var->green.offset = 8;
			var->green.length = 8;
			var->blue.offset = 0;
			var->blue.length = 8;
			var->transp.offset = 0;
			var->transp.length = 0;
		} else if (var->bits_per_pixel <= 32) {		/* RGBA 888 */
			var->bits_per_pixel = 32;
			var->red.offset = 16;
			var->red.length = 8;
			var->green.offset = 8;
			var->green.length = 8;
			var->blue.offset = 0;
			var->blue.length = 8;
			var->transp.offset = 24;
			var->transp.length = 8;
		} else
			return -EINVAL;

		var->red.msb_right = 0;
		var->green.msb_right = 0;
		var->blue.msb_right = 0;
		var->transp.msb_right = 0;
	}

	/* Make sure we don't exceed our allocated memory. */
	if (var->xres_virtual * var->yres_virtual * var->bits_per_pixel / 8 >
	    info->fix.smem_len)
		return -EINVAL;

	return 0;
}

/* -----------------------------------------------------------------------------
 * Frame buffer operations - Overlays
 */

static ssize_t
overlay_alpha_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct fb_info *info = dev_get_drvdata(dev);
	struct sh_mobile_lcdc_overlay *ovl = info->par;

	return scnprintf(buf, PAGE_SIZE, "%u\n", ovl->alpha);
}

static ssize_t
overlay_alpha_store(struct device *dev, struct device_attribute *attr,
		    const char *buf, size_t count)
{
	struct fb_info *info = dev_get_drvdata(dev);
	struct sh_mobile_lcdc_overlay *ovl = info->par;
	unsigned int alpha;
	char *endp;

	alpha = simple_strtoul(buf, &endp, 10);
	if (isspace(*endp))
		endp++;

	if (endp - buf != count)
		return -EINVAL;

	if (alpha > 255)
		return -EINVAL;

	if (ovl->alpha != alpha) {
		ovl->alpha = alpha;

		if (ovl->mode == LCDC_OVERLAY_BLEND && ovl->enabled)
			sh_mobile_lcdc_overlay_setup(ovl);
	}

	return count;
}

static ssize_t
overlay_mode_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct fb_info *info = dev_get_drvdata(dev);
	struct sh_mobile_lcdc_overlay *ovl = info->par;

	return scnprintf(buf, PAGE_SIZE, "%u\n", ovl->mode);
}

static ssize_t
overlay_mode_store(struct device *dev, struct device_attribute *attr,
		   const char *buf, size_t count)
{
	struct fb_info *info = dev_get_drvdata(dev);
	struct sh_mobile_lcdc_overlay *ovl = info->par;
	unsigned int mode;
	char *endp;

	mode = simple_strtoul(buf, &endp, 10);
	if (isspace(*endp))
		endp++;

	if (endp - buf != count)
		return -EINVAL;

	if (mode != LCDC_OVERLAY_BLEND && mode != LCDC_OVERLAY_ROP3)
		return -EINVAL;

	if (ovl->mode != mode) {
		ovl->mode = mode;

		if (ovl->enabled)
			sh_mobile_lcdc_overlay_setup(ovl);
	}

	return count;
}

static ssize_t
overlay_position_show(struct device *dev, struct device_attribute *attr,
		      char *buf)
{
	struct fb_info *info = dev_get_drvdata(dev);
	struct sh_mobile_lcdc_overlay *ovl = info->par;

	return scnprintf(buf, PAGE_SIZE, "%d,%d\n", ovl->pos_x, ovl->pos_y);
}

static ssize_t
overlay_position_store(struct device *dev, struct device_attribute *attr,
		       const char *buf, size_t count)
{
	struct fb_info *info = dev_get_drvdata(dev);
	struct sh_mobile_lcdc_overlay *ovl = info->par;
	char *endp;
	int pos_x;
	int pos_y;

	pos_x = simple_strtol(buf, &endp, 10);
	if (*endp != ',')
		return -EINVAL;

	pos_y = simple_strtol(endp + 1, &endp, 10);
	if (isspace(*endp))
		endp++;

	if (endp - buf != count)
		return -EINVAL;

	if (ovl->pos_x != pos_x || ovl->pos_y != pos_y) {
		ovl->pos_x = pos_x;
		ovl->pos_y = pos_y;

		if (ovl->enabled)
			sh_mobile_lcdc_overlay_setup(ovl);
	}

	return count;
}

static ssize_t
overlay_rop3_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct fb_info *info = dev_get_drvdata(dev);
	struct sh_mobile_lcdc_overlay *ovl = info->par;

	return scnprintf(buf, PAGE_SIZE, "%u\n", ovl->rop3);
}

static ssize_t
overlay_rop3_store(struct device *dev, struct device_attribute *attr,
		    const char *buf, size_t count)
{
	struct fb_info *info = dev_get_drvdata(dev);
	struct sh_mobile_lcdc_overlay *ovl = info->par;
	unsigned int rop3;
	char *endp;

	rop3 = simple_strtoul(buf, &endp, 10);
	if (isspace(*endp))
		endp++;

	if (endp - buf != count)
		return -EINVAL;

	if (rop3 > 255)
		return -EINVAL;

	if (ovl->rop3 != rop3) {
		ovl->rop3 = rop3;

		if (ovl->mode == LCDC_OVERLAY_ROP3 && ovl->enabled)
			sh_mobile_lcdc_overlay_setup(ovl);
	}

	return count;
}

static const struct device_attribute overlay_sysfs_attrs[] = {
	__ATTR(ovl_alpha, S_IRUGO|S_IWUSR,
	       overlay_alpha_show, overlay_alpha_store),
	__ATTR(ovl_mode, S_IRUGO|S_IWUSR,
	       overlay_mode_show, overlay_mode_store),
	__ATTR(ovl_position, S_IRUGO|S_IWUSR,
	       overlay_position_show, overlay_position_store),
	__ATTR(ovl_rop3, S_IRUGO|S_IWUSR,
	       overlay_rop3_show, overlay_rop3_store),
};

static const struct fb_fix_screeninfo sh_mobile_lcdc_overlay_fix  = {
	.id =		"SH Mobile LCDC",
	.type =		FB_TYPE_PACKED_PIXELS,
	.visual =	FB_VISUAL_TRUECOLOR,
	.accel =	FB_ACCEL_NONE,
	.xpanstep =	1,
	.ypanstep =	1,
	.ywrapstep =	0,
	.capabilities =	FB_CAP_FOURCC,
};

static int sh_mobile_lcdc_overlay_pan(struct fb_var_screeninfo *var,
				    struct fb_info *info)
{
	struct sh_mobile_lcdc_overlay *ovl = info->par;
	unsigned long base_addr_y;
	unsigned long base_addr_c;
	unsigned long y_offset;
	unsigned long c_offset;

	if (!ovl->format->yuv) {
		y_offset = (var->yoffset * ovl->xres_virtual + var->xoffset)
			 * ovl->format->bpp / 8;
		c_offset = 0;
	} else {
		unsigned int xsub = ovl->format->bpp < 24 ? 2 : 1;
		unsigned int ysub = ovl->format->bpp < 16 ? 2 : 1;

		y_offset = var->yoffset * ovl->xres_virtual + var->xoffset;
		c_offset = var->yoffset / ysub * ovl->xres_virtual * 2 / xsub
			 + var->xoffset * 2 / xsub;
	}

	/* If the Y offset hasn't changed, the C offset hasn't either. There's
	 * nothing to do in that case.
	 */
	if (y_offset == ovl->pan_y_offset)
		return 0;

	/* Set the source address for the next refresh */
	base_addr_y = ovl->dma_handle + y_offset;
	base_addr_c = ovl->dma_handle + ovl->xres_virtual * ovl->yres_virtual
		    + c_offset;

	ovl->base_addr_y = base_addr_y;
	ovl->base_addr_c = base_addr_c;
	ovl->pan_y_offset = y_offset;

	lcdc_write(ovl->channel->lcdc, LDBCR, LDBCR_UPC(ovl->index));

	lcdc_write_overlay(ovl, LDBnBSAYR(ovl->index), ovl->base_addr_y);
	lcdc_write_overlay(ovl, LDBnBSACR(ovl->index), ovl->base_addr_c);

	lcdc_write(ovl->channel->lcdc, LDBCR,
		   LDBCR_UPF(ovl->index) | LDBCR_UPD(ovl->index));

	return 0;
}

static int sh_mobile_lcdc_overlay_ioctl(struct fb_info *info, unsigned int cmd,
				      unsigned long arg)
{
	struct sh_mobile_lcdc_overlay *ovl = info->par;

	switch (cmd) {
	case FBIO_WAITFORVSYNC:
		return sh_mobile_lcdc_wait_for_vsync(ovl->channel);

	default:
		return -ENOIOCTLCMD;
	}
}

static int sh_mobile_lcdc_overlay_check_var(struct fb_var_screeninfo *var,
					  struct fb_info *info)
{
	return __sh_mobile_lcdc_check_var(var, info);
}

static int sh_mobile_lcdc_overlay_set_par(struct fb_info *info)
{
	struct sh_mobile_lcdc_overlay *ovl = info->par;

	ovl->format =
		sh_mobile_format_info(sh_mobile_format_fourcc(&info->var));

	ovl->xres = info->var.xres;
	ovl->xres_virtual = info->var.xres_virtual;
	ovl->yres = info->var.yres;
	ovl->yres_virtual = info->var.yres_virtual;

	if (ovl->format->yuv)
		ovl->pitch = info->var.xres_virtual;
	else
		ovl->pitch = info->var.xres_virtual * ovl->format->bpp / 8;

	sh_mobile_lcdc_overlay_setup(ovl);

	info->fix.line_length = ovl->pitch;

	if (sh_mobile_format_is_fourcc(&info->var)) {
		info->fix.type = FB_TYPE_FOURCC;
		info->fix.visual = FB_VISUAL_FOURCC;
	} else {
		info->fix.type = FB_TYPE_PACKED_PIXELS;
		info->fix.visual = FB_VISUAL_TRUECOLOR;
	}

	return 0;
}

/* Overlay blanking. Disable the overlay when blanked. */
static int sh_mobile_lcdc_overlay_blank(int blank, struct fb_info *info)
{
	struct sh_mobile_lcdc_overlay *ovl = info->par;

	ovl->enabled = !blank;
	sh_mobile_lcdc_overlay_setup(ovl);

	/* Prevent the backlight from receiving a blanking event by returning
	 * a non-zero value.
	 */
	return 1;
}

static int
sh_mobile_lcdc_overlay_mmap(struct fb_info *info, struct vm_area_struct *vma)
{
	struct sh_mobile_lcdc_overlay *ovl = info->par;

	return dma_mmap_coherent(ovl->channel->lcdc->dev, vma, ovl->fb_mem,
				 ovl->dma_handle, ovl->fb_size);
}

static struct fb_ops sh_mobile_lcdc_overlay_ops = {
	.owner          = THIS_MODULE,
	.fb_read        = fb_sys_read,
	.fb_write       = fb_sys_write,
	.fb_fillrect	= sys_fillrect,
	.fb_copyarea	= sys_copyarea,
	.fb_imageblit	= sys_imageblit,
	.fb_blank	= sh_mobile_lcdc_overlay_blank,
	.fb_pan_display = sh_mobile_lcdc_overlay_pan,
	.fb_ioctl       = sh_mobile_lcdc_overlay_ioctl,
	.fb_check_var	= sh_mobile_lcdc_overlay_check_var,
	.fb_set_par	= sh_mobile_lcdc_overlay_set_par,
	.fb_mmap	= sh_mobile_lcdc_overlay_mmap,
};

static void
sh_mobile_lcdc_overlay_fb_unregister(struct sh_mobile_lcdc_overlay *ovl)
{
	struct fb_info *info = ovl->info;

	if (info == NULL || info->dev == NULL)
		return;

	unregister_framebuffer(ovl->info);
}

static int
sh_mobile_lcdc_overlay_fb_register(struct sh_mobile_lcdc_overlay *ovl)
{
	struct sh_mobile_lcdc_priv *lcdc = ovl->channel->lcdc;
	struct fb_info *info = ovl->info;
	unsigned int i;
	int ret;

	if (info == NULL)
		return 0;

	ret = register_framebuffer(info);
	if (ret < 0)
		return ret;

	dev_info(lcdc->dev, "registered %s/overlay %u as %dx%d %dbpp.\n",
		 dev_name(lcdc->dev), ovl->index, info->var.xres,
		 info->var.yres, info->var.bits_per_pixel);

	for (i = 0; i < ARRAY_SIZE(overlay_sysfs_attrs); ++i) {
		ret = device_create_file(info->dev, &overlay_sysfs_attrs[i]);
		if (ret < 0)
			return ret;
	}

	return 0;
}

static void
sh_mobile_lcdc_overlay_fb_cleanup(struct sh_mobile_lcdc_overlay *ovl)
{
	struct fb_info *info = ovl->info;

	if (info == NULL || info->device == NULL)
		return;

	framebuffer_release(info);
}

static int
sh_mobile_lcdc_overlay_fb_init(struct sh_mobile_lcdc_overlay *ovl)
{
	struct sh_mobile_lcdc_priv *priv = ovl->channel->lcdc;
	struct fb_var_screeninfo *var;
	struct fb_info *info;

	/* Allocate and initialize the frame buffer device. */
	info = framebuffer_alloc(0, priv->dev);
	if (info == NULL) {
		dev_err(priv->dev, "unable to allocate fb_info\n");
		return -ENOMEM;
	}

	ovl->info = info;

	info->flags = FBINFO_FLAG_DEFAULT;
	info->fbops = &sh_mobile_lcdc_overlay_ops;
	info->device = priv->dev;
	info->screen_base = ovl->fb_mem;
	info->par = ovl;

	/* Initialize fixed screen information. Restrict pan to 2 lines steps
	 * for NV12 and NV21.
	 */
	info->fix = sh_mobile_lcdc_overlay_fix;
	snprintf(info->fix.id, sizeof(info->fix.id),
		 "SH Mobile LCDC Overlay %u", ovl->index);
	info->fix.smem_start = ovl->dma_handle;
	info->fix.smem_len = ovl->fb_size;
	info->fix.line_length = ovl->pitch;

	if (ovl->format->yuv)
		info->fix.visual = FB_VISUAL_FOURCC;
	else
		info->fix.visual = FB_VISUAL_TRUECOLOR;

	switch (ovl->format->fourcc) {
	case V4L2_PIX_FMT_NV12:
	case V4L2_PIX_FMT_NV21:
		info->fix.ypanstep = 2;
	case V4L2_PIX_FMT_NV16:
	case V4L2_PIX_FMT_NV61:
		info->fix.xpanstep = 2;
	}

	/* Initialize variable screen information. */
	var = &info->var;
	memset(var, 0, sizeof(*var));
	var->xres = ovl->xres;
	var->yres = ovl->yres;
	var->xres_virtual = ovl->xres_virtual;
	var->yres_virtual = ovl->yres_virtual;
	var->activate = FB_ACTIVATE_NOW;

	/* Use the legacy API by default for RGB formats, and the FOURCC API
	 * for YUV formats.
	 */
	if (!ovl->format->yuv)
		var->bits_per_pixel = ovl->format->bpp;
	else
		var->grayscale = ovl->format->fourcc;

	return sh_mobile_lcdc_overlay_check_var(var, info);
}

/* -----------------------------------------------------------------------------
 * Frame buffer operations - main frame buffer
 */

static int sh_mobile_lcdc_setcolreg(u_int regno,
				    u_int red, u_int green, u_int blue,
				    u_int transp, struct fb_info *info)
{
	u32 *palette = info->pseudo_palette;

	if (regno >= PALETTE_NR)
		return -EINVAL;

	/* only FB_VISUAL_TRUECOLOR supported */

	red >>= 16 - info->var.red.length;
	green >>= 16 - info->var.green.length;
	blue >>= 16 - info->var.blue.length;
	transp >>= 16 - info->var.transp.length;

	palette[regno] = (red << info->var.red.offset) |
	  (green << info->var.green.offset) |
	  (blue << info->var.blue.offset) |
	  (transp << info->var.transp.offset);

	return 0;
}

static const struct fb_fix_screeninfo sh_mobile_lcdc_fix  = {
	.id =		"SH Mobile LCDC",
	.type =		FB_TYPE_PACKED_PIXELS,
	.visual =	FB_VISUAL_TRUECOLOR,
	.accel =	FB_ACCEL_NONE,
	.xpanstep =	1,
	.ypanstep =	1,
	.ywrapstep =	0,
	.capabilities =	FB_CAP_FOURCC,
};

static void sh_mobile_lcdc_fillrect(struct fb_info *info,
				    const struct fb_fillrect *rect)
{
	sys_fillrect(info, rect);
	sh_mobile_lcdc_deferred_io_touch(info);
}

static void sh_mobile_lcdc_copyarea(struct fb_info *info,
				    const struct fb_copyarea *area)
{
	sys_copyarea(info, area);
	sh_mobile_lcdc_deferred_io_touch(info);
}

static void sh_mobile_lcdc_imageblit(struct fb_info *info,
				     const struct fb_image *image)
{
	sys_imageblit(info, image);
	sh_mobile_lcdc_deferred_io_touch(info);
}

static int sh_mobile_lcdc_pan(struct fb_var_screeninfo *var,
			      struct fb_info *info)
{
	struct sh_mobile_lcdc_chan *ch = info->par;
	struct sh_mobile_lcdc_priv *priv = ch->lcdc;
	unsigned long ldrcntr;
	unsigned long base_addr_y, base_addr_c;
	unsigned long y_offset;
	unsigned long c_offset;

	if (!ch->format->yuv) {
		y_offset = (var->yoffset * ch->xres_virtual + var->xoffset)
			 * ch->format->bpp / 8;
		c_offset = 0;
	} else {
		unsigned int xsub = ch->format->bpp < 24 ? 2 : 1;
		unsigned int ysub = ch->format->bpp < 16 ? 2 : 1;

		y_offset = var->yoffset * ch->xres_virtual + var->xoffset;
		c_offset = var->yoffset / ysub * ch->xres_virtual * 2 / xsub
			 + var->xoffset * 2 / xsub;
	}

	/* If the Y offset hasn't changed, the C offset hasn't either. There's
	 * nothing to do in that case.
	 */
	if (y_offset == ch->pan_y_offset)
		return 0;

	/* Set the source address for the next refresh */
	base_addr_y = ch->dma_handle + y_offset;
	base_addr_c = ch->dma_handle + ch->xres_virtual * ch->yres_virtual
		    + c_offset;

	ch->base_addr_y = base_addr_y;
	ch->base_addr_c = base_addr_c;
	ch->pan_y_offset = y_offset;

	lcdc_write_chan_mirror(ch, LDSA1R, base_addr_y);
	if (ch->format->yuv)
		lcdc_write_chan_mirror(ch, LDSA2R, base_addr_c);

	ldrcntr = lcdc_read(priv, _LDRCNTR);
	if (lcdc_chan_is_sublcd(ch))
		lcdc_write(ch->lcdc, _LDRCNTR, ldrcntr ^ LDRCNTR_SRS);
	else
		lcdc_write(ch->lcdc, _LDRCNTR, ldrcntr ^ LDRCNTR_MRS);


	sh_mobile_lcdc_deferred_io_touch(info);

	return 0;
}

static int sh_mobile_lcdc_ioctl(struct fb_info *info, unsigned int cmd,
				unsigned long arg)
{
	struct sh_mobile_lcdc_chan *ch = info->par;
	int retval;

	switch (cmd) {
	case FBIO_WAITFORVSYNC:
		retval = sh_mobile_lcdc_wait_for_vsync(ch);
		break;

	default:
		retval = -ENOIOCTLCMD;
		break;
	}
	return retval;
}

static void sh_mobile_fb_reconfig(struct fb_info *info)
{
	struct sh_mobile_lcdc_chan *ch = info->par;
	struct fb_var_screeninfo var;
	struct fb_videomode mode;
	struct fb_event event;
	int evnt = FB_EVENT_MODE_CHANGE_ALL;

	if (ch->use_count > 1 || (ch->use_count == 1 && !info->fbcon_par))
		/* More framebuffer users are active */
		return;

	fb_var_to_videomode(&mode, &info->var);

	if (fb_mode_is_equal(&ch->display.mode, &mode))
		return;

	/* Display has been re-plugged, framebuffer is free now, reconfigure */
	var = info->var;
	fb_videomode_to_var(&var, &ch->display.mode);
	var.width = ch->display.width;
	var.height = ch->display.height;
	var.activate = FB_ACTIVATE_NOW;

	if (fb_set_var(info, &var) < 0)
		/* Couldn't reconfigure, hopefully, can continue as before */
		return;

	/*
	 * fb_set_var() calls the notifier change internally, only if
	 * FBINFO_MISC_USEREVENT flag is set. Since we do not want to fake a
	 * user event, we have to call the chain ourselves.
	 */
	event.info = info;
	event.data = &ch->display.mode;
	fb_notifier_call_chain(evnt, &event);
}

/*
 * Locking: both .fb_release() and .fb_open() are called with info->lock held if
 * user == 1, or with console sem held, if user == 0.
 */
static int sh_mobile_lcdc_release(struct fb_info *info, int user)
{
	struct sh_mobile_lcdc_chan *ch = info->par;

	mutex_lock(&ch->open_lock);
	dev_dbg(info->dev, "%s(): %d users\n", __func__, ch->use_count);

	ch->use_count--;

	/* Nothing to reconfigure, when called from fbcon */
	if (user) {
		console_lock();
		sh_mobile_fb_reconfig(info);
		console_unlock();
	}

	mutex_unlock(&ch->open_lock);

	return 0;
}

static int sh_mobile_lcdc_open(struct fb_info *info, int user)
{
	struct sh_mobile_lcdc_chan *ch = info->par;

	mutex_lock(&ch->open_lock);
	ch->use_count++;

	dev_dbg(info->dev, "%s(): %d users\n", __func__, ch->use_count);
	mutex_unlock(&ch->open_lock);

	return 0;
}

static int sh_mobile_lcdc_check_var(struct fb_var_screeninfo *var,
				    struct fb_info *info)
{
	struct sh_mobile_lcdc_chan *ch = info->par;
	struct sh_mobile_lcdc_priv *p = ch->lcdc;
	unsigned int best_dist = (unsigned int)-1;
	unsigned int best_xres = 0;
	unsigned int best_yres = 0;
	unsigned int i;
	int ret;

	/* If board code provides us with a list of available modes, make sure
	 * we use one of them. Find the mode closest to the requested one. The
	 * distance between two modes is defined as the size of the
	 * non-overlapping parts of the two rectangles.
	 */
	for (i = 0; i < ch->cfg->num_modes; ++i) {
		const struct fb_videomode *mode = &ch->cfg->lcd_modes[i];
		unsigned int dist;

		/* We can only round up. */
		if (var->xres > mode->xres || var->yres > mode->yres)
			continue;

		dist = var->xres * var->yres + mode->xres * mode->yres
		     - 2 * min(var->xres, mode->xres)
			 * min(var->yres, mode->yres);

		if (dist < best_dist) {
			best_xres = mode->xres;
			best_yres = mode->yres;
			best_dist = dist;
		}
	}

	/* If no available mode can be used, return an error. */
	if (ch->cfg->num_modes != 0) {
		if (best_dist == (unsigned int)-1)
			return -EINVAL;

		var->xres = best_xres;
		var->yres = best_yres;
	}

	ret = __sh_mobile_lcdc_check_var(var, info);
	if (ret < 0)
		return ret;

	/* only accept the forced_fourcc for dual channel configurations */
	if (p->forced_fourcc &&
	    p->forced_fourcc != sh_mobile_format_fourcc(var))
		return -EINVAL;

	return 0;
}

static int sh_mobile_lcdc_set_par(struct fb_info *info)
{
	struct sh_mobile_lcdc_chan *ch = info->par;
	int ret;

	sh_mobile_lcdc_stop(ch->lcdc);

	ch->format = sh_mobile_format_info(sh_mobile_format_fourcc(&info->var));
	ch->colorspace = info->var.colorspace;

	ch->xres = info->var.xres;
	ch->xres_virtual = info->var.xres_virtual;
	ch->yres = info->var.yres;
	ch->yres_virtual = info->var.yres_virtual;

	if (ch->format->yuv)
		ch->pitch = info->var.xres_virtual;
	else
		ch->pitch = info->var.xres_virtual * ch->format->bpp / 8;

	ret = sh_mobile_lcdc_start(ch->lcdc);
	if (ret < 0)
		dev_err(info->dev, "%s: unable to restart LCDC\n", __func__);

	info->fix.line_length = ch->pitch;

	if (sh_mobile_format_is_fourcc(&info->var)) {
		info->fix.type = FB_TYPE_FOURCC;
		info->fix.visual = FB_VISUAL_FOURCC;
	} else {
		info->fix.type = FB_TYPE_PACKED_PIXELS;
		info->fix.visual = FB_VISUAL_TRUECOLOR;
	}

	return ret;
}

/*
 * Screen blanking. Behavior is as follows:
 * FB_BLANK_UNBLANK: screen unblanked, clocks enabled
 * FB_BLANK_NORMAL: screen blanked, clocks enabled
 * FB_BLANK_VSYNC,
 * FB_BLANK_HSYNC,
 * FB_BLANK_POWEROFF: screen blanked, clocks disabled
 */
static int sh_mobile_lcdc_blank(int blank, struct fb_info *info)
{
	struct sh_mobile_lcdc_chan *ch = info->par;
	struct sh_mobile_lcdc_priv *p = ch->lcdc;

	/* blank the screen? */
	if (blank > FB_BLANK_UNBLANK && ch->blank_status == FB_BLANK_UNBLANK) {
		struct fb_fillrect rect = {
			.width = ch->xres,
			.height = ch->yres,
		};
		sh_mobile_lcdc_fillrect(info, &rect);
	}
	/* turn clocks on? */
	if (blank <= FB_BLANK_NORMAL && ch->blank_status > FB_BLANK_NORMAL) {
		sh_mobile_lcdc_clk_on(p);
	}
	/* turn clocks off? */
	if (blank > FB_BLANK_NORMAL && ch->blank_status <= FB_BLANK_NORMAL) {
		/* make sure the screen is updated with the black fill before
		 * switching the clocks off. one vsync is not enough since
		 * blanking may occur in the middle of a refresh. deferred io
		 * mode will reenable the clocks and update the screen in time,
		 * so it does not need this. */
		if (!info->fbdefio) {
			sh_mobile_lcdc_wait_for_vsync(ch);
			sh_mobile_lcdc_wait_for_vsync(ch);
		}
		sh_mobile_lcdc_clk_off(p);
	}

	ch->blank_status = blank;
	return 0;
}

static int
sh_mobile_lcdc_mmap(struct fb_info *info, struct vm_area_struct *vma)
{
	struct sh_mobile_lcdc_chan *ch = info->par;

	return dma_mmap_coherent(ch->lcdc->dev, vma, ch->fb_mem,
				 ch->dma_handle, ch->fb_size);
}

static struct fb_ops sh_mobile_lcdc_ops = {
	.owner          = THIS_MODULE,
	.fb_setcolreg	= sh_mobile_lcdc_setcolreg,
	.fb_read        = fb_sys_read,
	.fb_write       = fb_sys_write,
	.fb_fillrect	= sh_mobile_lcdc_fillrect,
	.fb_copyarea	= sh_mobile_lcdc_copyarea,
	.fb_imageblit	= sh_mobile_lcdc_imageblit,
	.fb_blank	= sh_mobile_lcdc_blank,
	.fb_pan_display = sh_mobile_lcdc_pan,
	.fb_ioctl       = sh_mobile_lcdc_ioctl,
	.fb_open	= sh_mobile_lcdc_open,
	.fb_release	= sh_mobile_lcdc_release,
	.fb_check_var	= sh_mobile_lcdc_check_var,
	.fb_set_par	= sh_mobile_lcdc_set_par,
	.fb_mmap	= sh_mobile_lcdc_mmap,
};

static void
sh_mobile_lcdc_channel_fb_unregister(struct sh_mobile_lcdc_chan *ch)
{
	if (ch->info && ch->info->dev)
		unregister_framebuffer(ch->info);
}

static int
sh_mobile_lcdc_channel_fb_register(struct sh_mobile_lcdc_chan *ch)
{
	struct fb_info *info = ch->info;
	int ret;

	if (info->fbdefio) {
		ch->sglist = vmalloc(sizeof(struct scatterlist) *
				     ch->fb_size >> PAGE_SHIFT);
		if (!ch->sglist)
			return -ENOMEM;
	}

	info->bl_dev = ch->bl;

	ret = register_framebuffer(info);
	if (ret < 0)
		return ret;

	dev_info(ch->lcdc->dev, "registered %s/%s as %dx%d %dbpp.\n",
		 dev_name(ch->lcdc->dev), (ch->cfg->chan == LCDC_CHAN_MAINLCD) ?
		 "mainlcd" : "sublcd", info->var.xres, info->var.yres,
		 info->var.bits_per_pixel);

	/* deferred io mode: disable clock to save power */
	if (info->fbdefio || info->state == FBINFO_STATE_SUSPENDED)
		sh_mobile_lcdc_clk_off(ch->lcdc);

	return ret;
}

static void
sh_mobile_lcdc_channel_fb_cleanup(struct sh_mobile_lcdc_chan *ch)
{
	struct fb_info *info = ch->info;

	if (!info || !info->device)
		return;

	vfree(ch->sglist);

	fb_dealloc_cmap(&info->cmap);
	framebuffer_release(info);
}

static int
sh_mobile_lcdc_channel_fb_init(struct sh_mobile_lcdc_chan *ch,
			       const struct fb_videomode *modes,
			       unsigned int num_modes)
{
	struct sh_mobile_lcdc_priv *priv = ch->lcdc;
	struct fb_var_screeninfo *var;
	struct fb_info *info;
	int ret;

	/* Allocate and initialize the frame buffer device. Create the modes
	 * list and allocate the color map.
	 */
	info = framebuffer_alloc(0, priv->dev);
	if (info == NULL) {
		dev_err(priv->dev, "unable to allocate fb_info\n");
		return -ENOMEM;
	}

	ch->info = info;

	info->flags = FBINFO_FLAG_DEFAULT;
	info->fbops = &sh_mobile_lcdc_ops;
	info->device = priv->dev;
	info->screen_base = ch->fb_mem;
	info->pseudo_palette = &ch->pseudo_palette;
	info->par = ch;

	fb_videomode_to_modelist(modes, num_modes, &info->modelist);

	ret = fb_alloc_cmap(&info->cmap, PALETTE_NR, 0);
	if (ret < 0) {
		dev_err(priv->dev, "unable to allocate cmap\n");
		return ret;
	}

	/* Initialize fixed screen information. Restrict pan to 2 lines steps
	 * for NV12 and NV21.
	 */
	info->fix = sh_mobile_lcdc_fix;
	info->fix.smem_start = ch->dma_handle;
	info->fix.smem_len = ch->fb_size;
	info->fix.line_length = ch->pitch;

	if (ch->format->yuv)
		info->fix.visual = FB_VISUAL_FOURCC;
	else
		info->fix.visual = FB_VISUAL_TRUECOLOR;

	switch (ch->format->fourcc) {
	case V4L2_PIX_FMT_NV12:
	case V4L2_PIX_FMT_NV21:
		info->fix.ypanstep = 2;
	case V4L2_PIX_FMT_NV16:
	case V4L2_PIX_FMT_NV61:
		info->fix.xpanstep = 2;
	}

	/* Initialize variable screen information using the first mode as
	 * default.
	 */
	var = &info->var;
	fb_videomode_to_var(var, modes);
	var->width = ch->display.width;
	var->height = ch->display.height;
	var->xres_virtual = ch->xres_virtual;
	var->yres_virtual = ch->yres_virtual;
	var->activate = FB_ACTIVATE_NOW;

	/* Use the legacy API by default for RGB formats, and the FOURCC API
	 * for YUV formats.
	 */
	if (!ch->format->yuv)
		var->bits_per_pixel = ch->format->bpp;
	else
		var->grayscale = ch->format->fourcc;

	ret = sh_mobile_lcdc_check_var(var, info);
	if (ret)
		return ret;

	return 0;
}

/* -----------------------------------------------------------------------------
 * Backlight
 */

static int sh_mobile_lcdc_update_bl(struct backlight_device *bdev)
{
	struct sh_mobile_lcdc_chan *ch = bl_get_data(bdev);
	int brightness = bdev->props.brightness;

	if (bdev->props.power != FB_BLANK_UNBLANK ||
	    bdev->props.state & (BL_CORE_SUSPENDED | BL_CORE_FBBLANK))
		brightness = 0;

	ch->bl_brightness = brightness;
	return ch->cfg->bl_info.set_brightness(brightness);
}

static int sh_mobile_lcdc_get_brightness(struct backlight_device *bdev)
{
	struct sh_mobile_lcdc_chan *ch = bl_get_data(bdev);

	return ch->bl_brightness;
}

static int sh_mobile_lcdc_check_fb(struct backlight_device *bdev,
				   struct fb_info *info)
{
	return (info->bl_dev == bdev);
}

static const struct backlight_ops sh_mobile_lcdc_bl_ops = {
	.options	= BL_CORE_SUSPENDRESUME,
	.update_status	= sh_mobile_lcdc_update_bl,
	.get_brightness	= sh_mobile_lcdc_get_brightness,
	.check_fb	= sh_mobile_lcdc_check_fb,
};

static struct backlight_device *sh_mobile_lcdc_bl_probe(struct device *parent,
					       struct sh_mobile_lcdc_chan *ch)
{
	struct backlight_device *bl;

	bl = backlight_device_register(ch->cfg->bl_info.name, parent, ch,
				       &sh_mobile_lcdc_bl_ops, NULL);
	if (IS_ERR(bl)) {
		dev_err(parent, "unable to register backlight device: %ld\n",
			PTR_ERR(bl));
		return NULL;
	}

	bl->props.max_brightness = ch->cfg->bl_info.max_brightness;
	bl->props.brightness = bl->props.max_brightness;
	backlight_update_status(bl);

	return bl;
}

static void sh_mobile_lcdc_bl_remove(struct backlight_device *bdev)
{
	backlight_device_unregister(bdev);
}

/* -----------------------------------------------------------------------------
 * Power management
 */

static int sh_mobile_lcdc_suspend(struct device *dev)
{
	struct platform_device *pdev = to_platform_device(dev);

	sh_mobile_lcdc_stop(platform_get_drvdata(pdev));
	return 0;
}

static int sh_mobile_lcdc_resume(struct device *dev)
{
	struct platform_device *pdev = to_platform_device(dev);

	return sh_mobile_lcdc_start(platform_get_drvdata(pdev));
}

static int sh_mobile_lcdc_runtime_suspend(struct device *dev)
{
	struct sh_mobile_lcdc_priv *priv = dev_get_drvdata(dev);

	/* turn off LCDC hardware */
	lcdc_write(priv, _LDCNT1R, 0);

	return 0;
}

static int sh_mobile_lcdc_runtime_resume(struct device *dev)
{
	struct sh_mobile_lcdc_priv *priv = dev_get_drvdata(dev);

	__sh_mobile_lcdc_start(priv);

	return 0;
}

static const struct dev_pm_ops sh_mobile_lcdc_dev_pm_ops = {
	.suspend = sh_mobile_lcdc_suspend,
	.resume = sh_mobile_lcdc_resume,
	.runtime_suspend = sh_mobile_lcdc_runtime_suspend,
	.runtime_resume = sh_mobile_lcdc_runtime_resume,
};

/* -----------------------------------------------------------------------------
 * Framebuffer notifier
 */

/* locking: called with info->lock held */
static int sh_mobile_lcdc_notify(struct notifier_block *nb,
				 unsigned long action, void *data)
{
	struct fb_event *event = data;
	struct fb_info *info = event->info;
	struct sh_mobile_lcdc_chan *ch = info->par;

	if (&ch->lcdc->notifier != nb)
		return NOTIFY_DONE;

	dev_dbg(info->dev, "%s(): action = %lu, data = %p\n",
		__func__, action, event->data);

	switch(action) {
	case FB_EVENT_SUSPEND:
		sh_mobile_lcdc_display_off(ch);
		sh_mobile_lcdc_stop(ch->lcdc);
		break;
	case FB_EVENT_RESUME:
		mutex_lock(&ch->open_lock);
		sh_mobile_fb_reconfig(info);
		mutex_unlock(&ch->open_lock);

		sh_mobile_lcdc_display_on(ch);
		sh_mobile_lcdc_start(ch->lcdc);
	}

	return NOTIFY_OK;
}

/* -----------------------------------------------------------------------------
 * Probe/remove and driver init/exit
 */

static const struct fb_videomode default_720p = {
	.name = "HDMI 720p",
	.xres = 1280,
	.yres = 720,

	.left_margin = 220,
	.right_margin = 110,
	.hsync_len = 40,

	.upper_margin = 20,
	.lower_margin = 5,
	.vsync_len = 5,

	.pixclock = 13468,
	.refresh = 60,
	.sync = FB_SYNC_VERT_HIGH_ACT | FB_SYNC_HOR_HIGH_ACT,
};

static int sh_mobile_lcdc_remove(struct platform_device *pdev)
{
	struct sh_mobile_lcdc_priv *priv = platform_get_drvdata(pdev);
	unsigned int i;

	fb_unregister_client(&priv->notifier);

	for (i = 0; i < ARRAY_SIZE(priv->overlays); i++)
		sh_mobile_lcdc_overlay_fb_unregister(&priv->overlays[i]);
	for (i = 0; i < ARRAY_SIZE(priv->ch); i++)
		sh_mobile_lcdc_channel_fb_unregister(&priv->ch[i]);

	sh_mobile_lcdc_stop(priv);

	for (i = 0; i < ARRAY_SIZE(priv->overlays); i++) {
		struct sh_mobile_lcdc_overlay *ovl = &priv->overlays[i];

		sh_mobile_lcdc_overlay_fb_cleanup(ovl);

		if (ovl->fb_mem)
			dma_free_coherent(&pdev->dev, ovl->fb_size,
					  ovl->fb_mem, ovl->dma_handle);
	}

	for (i = 0; i < ARRAY_SIZE(priv->ch); i++) {
		struct sh_mobile_lcdc_chan *ch = &priv->ch[i];

		if (ch->tx_dev) {
			ch->tx_dev->lcdc = NULL;
			module_put(ch->cfg->tx_dev->dev.driver->owner);
		}

		sh_mobile_lcdc_channel_fb_cleanup(ch);

		if (ch->fb_mem)
			dma_free_coherent(&pdev->dev, ch->fb_size,
					  ch->fb_mem, ch->dma_handle);
	}

	for (i = 0; i < ARRAY_SIZE(priv->ch); i++) {
		struct sh_mobile_lcdc_chan *ch = &priv->ch[i];

		if (ch->bl)
			sh_mobile_lcdc_bl_remove(ch->bl);
		mutex_destroy(&ch->open_lock);
	}

	if (priv->dot_clk) {
		pm_runtime_disable(&pdev->dev);
		clk_put(priv->dot_clk);
	}

	if (priv->base)
		iounmap(priv->base);

	if (priv->irq)
		free_irq(priv->irq, priv);
	kfree(priv);
	return 0;
}

static int sh_mobile_lcdc_check_interface(struct sh_mobile_lcdc_chan *ch)
{
	int interface_type = ch->cfg->interface_type;

	switch (interface_type) {
	case RGB8:
	case RGB9:
	case RGB12A:
	case RGB12B:
	case RGB16:
	case RGB18:
	case RGB24:
	case SYS8A:
	case SYS8B:
	case SYS8C:
	case SYS8D:
	case SYS9:
	case SYS12:
	case SYS16A:
	case SYS16B:
	case SYS16C:
	case SYS18:
	case SYS24:
		break;
	default:
		return -EINVAL;
	}

	/* SUBLCD only supports SYS interface */
	if (lcdc_chan_is_sublcd(ch)) {
		if (!(interface_type & LDMT1R_IFM))
			return -EINVAL;

		interface_type &= ~LDMT1R_IFM;
	}

	ch->ldmt1r_value = interface_type;
	return 0;
}

static int
sh_mobile_lcdc_overlay_init(struct sh_mobile_lcdc_overlay *ovl)
{
	const struct sh_mobile_lcdc_format_info *format;
	struct device *dev = ovl->channel->lcdc->dev;
	int ret;

	if (ovl->cfg->fourcc == 0)
		return 0;

	/* Validate the format. */
	format = sh_mobile_format_info(ovl->cfg->fourcc);
	if (format == NULL) {
		dev_err(dev, "Invalid FOURCC %08x\n", ovl->cfg->fourcc);
		return -EINVAL;
	}

	ovl->enabled = false;
	ovl->mode = LCDC_OVERLAY_BLEND;
	ovl->alpha = 255;
	ovl->rop3 = 0;
	ovl->pos_x = 0;
	ovl->pos_y = 0;

	/* The default Y virtual resolution is twice the panel size to allow for
	 * double-buffering.
	 */
	ovl->format = format;
	ovl->xres = ovl->cfg->max_xres;
	ovl->xres_virtual = ovl->xres;
	ovl->yres = ovl->cfg->max_yres;
	ovl->yres_virtual = ovl->yres * 2;

	if (!format->yuv)
		ovl->pitch = ovl->xres_virtual * format->bpp / 8;
	else
		ovl->pitch = ovl->xres_virtual;

	/* Allocate frame buffer memory. */
	ovl->fb_size = ovl->cfg->max_xres * ovl->cfg->max_yres
		       * format->bpp / 8 * 2;
	ovl->fb_mem = dma_alloc_coherent(dev, ovl->fb_size, &ovl->dma_handle,
					 GFP_KERNEL);
	if (!ovl->fb_mem) {
		dev_err(dev, "unable to allocate buffer\n");
		return -ENOMEM;
	}

	ret = sh_mobile_lcdc_overlay_fb_init(ovl);
	if (ret < 0)
		return ret;

	return 0;
}

static int
sh_mobile_lcdc_channel_init(struct sh_mobile_lcdc_chan *ch)
{
	const struct sh_mobile_lcdc_format_info *format;
	const struct sh_mobile_lcdc_chan_cfg *cfg = ch->cfg;
	struct device *dev = ch->lcdc->dev;
	const struct fb_videomode *max_mode;
	const struct fb_videomode *mode;
	unsigned int num_modes;
	unsigned int max_size;
	unsigned int i;

	ch->notify = sh_mobile_lcdc_display_notify;

	/* Validate the format. */
	format = sh_mobile_format_info(cfg->fourcc);
	if (format == NULL) {
		dev_err(dev, "Invalid FOURCC %08x.\n", cfg->fourcc);
		return -EINVAL;
	}

	/* Iterate through the modes to validate them and find the highest
	 * resolution.
	 */
	max_mode = NULL;
	max_size = 0;

	for (i = 0, mode = cfg->lcd_modes; i < cfg->num_modes; i++, mode++) {
		unsigned int size = mode->yres * mode->xres;

		/* NV12/NV21 buffers must have even number of lines */
		if ((cfg->fourcc == V4L2_PIX_FMT_NV12 ||
		     cfg->fourcc == V4L2_PIX_FMT_NV21) && (mode->yres & 0x1)) {
			dev_err(dev, "yres must be multiple of 2 for "
				"YCbCr420 mode.\n");
			return -EINVAL;
		}

		if (size > max_size) {
			max_mode = mode;
			max_size = size;
		}
	}

	if (!max_size)
		max_size = MAX_XRES * MAX_YRES;
	else
		dev_dbg(dev, "Found largest videomode %ux%u\n",
			max_mode->xres, max_mode->yres);

	if (cfg->lcd_modes == NULL) {
		mode = &default_720p;
		num_modes = 1;
	} else {
		mode = cfg->lcd_modes;
		num_modes = cfg->num_modes;
	}

	/* Use the first mode as default. The default Y virtual resolution is
	 * twice the panel size to allow for double-buffering.
	 */
	ch->format = format;
	ch->xres = mode->xres;
	ch->xres_virtual = mode->xres;
	ch->yres = mode->yres;
	ch->yres_virtual = mode->yres * 2;

	if (!format->yuv) {
		ch->colorspace = V4L2_COLORSPACE_SRGB;
		ch->pitch = ch->xres_virtual * format->bpp / 8;
	} else {
		ch->colorspace = V4L2_COLORSPACE_REC709;
		ch->pitch = ch->xres_virtual;
	}

	ch->display.width = cfg->panel_cfg.width;
	ch->display.height = cfg->panel_cfg.height;
	ch->display.mode = *mode;

	/* Allocate frame buffer memory. */
	ch->fb_size = max_size * format->bpp / 8 * 2;
	ch->fb_mem = dma_alloc_coherent(dev, ch->fb_size, &ch->dma_handle,
					GFP_KERNEL);
	if (ch->fb_mem == NULL) {
		dev_err(dev, "unable to allocate buffer\n");
		return -ENOMEM;
	}

	/* Initialize the transmitter device if present. */
	if (cfg->tx_dev) {
		if (!cfg->tx_dev->dev.driver ||
		    !try_module_get(cfg->tx_dev->dev.driver->owner)) {
			dev_warn(dev, "unable to get transmitter device\n");
			return -EINVAL;
		}
		ch->tx_dev = platform_get_drvdata(cfg->tx_dev);
		ch->tx_dev->lcdc = ch;
		ch->tx_dev->def_mode = *mode;
	}

	return sh_mobile_lcdc_channel_fb_init(ch, mode, num_modes);
}

static int sh_mobile_lcdc_probe(struct platform_device *pdev)
{
	struct sh_mobile_lcdc_info *pdata = pdev->dev.platform_data;
	struct sh_mobile_lcdc_priv *priv;
	struct resource *res;
	int num_channels;
	int error;
	int irq, i;

	if (!pdata) {
		dev_err(&pdev->dev, "no platform data defined\n");
		return -EINVAL;
	}

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	irq = platform_get_irq(pdev, 0);
	if (!res || irq < 0) {
		dev_err(&pdev->dev, "cannot get platform resources\n");
		return -ENOENT;
	}

	priv = kzalloc(sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	priv->dev = &pdev->dev;

	for (i = 0; i < ARRAY_SIZE(priv->ch); i++)
		mutex_init(&priv->ch[i].open_lock);
	platform_set_drvdata(pdev, priv);

	error = request_irq(irq, sh_mobile_lcdc_irq, 0,
			    dev_name(&pdev->dev), priv);
	if (error) {
		dev_err(&pdev->dev, "unable to request irq\n");
		goto err1;
	}

	priv->irq = irq;
	atomic_set(&priv->hw_usecnt, -1);

	for (i = 0, num_channels = 0; i < ARRAY_SIZE(pdata->ch); i++) {
		struct sh_mobile_lcdc_chan *ch = priv->ch + num_channels;

		ch->lcdc = priv;
		ch->cfg = &pdata->ch[i];

		error = sh_mobile_lcdc_check_interface(ch);
		if (error) {
			dev_err(&pdev->dev, "unsupported interface type\n");
			goto err1;
		}
		init_waitqueue_head(&ch->frame_end_wait);
		init_completion(&ch->vsync_completion);

		/* probe the backlight is there is one defined */
		if (ch->cfg->bl_info.max_brightness)
			ch->bl = sh_mobile_lcdc_bl_probe(&pdev->dev, ch);

		switch (pdata->ch[i].chan) {
		case LCDC_CHAN_MAINLCD:
			ch->enabled = LDCNT2R_ME;
			ch->reg_offs = lcdc_offs_mainlcd;
			num_channels++;
			break;
		case LCDC_CHAN_SUBLCD:
			ch->enabled = LDCNT2R_SE;
			ch->reg_offs = lcdc_offs_sublcd;
			num_channels++;
			break;
		}
	}

	if (!num_channels) {
		dev_err(&pdev->dev, "no channels defined\n");
		error = -EINVAL;
		goto err1;
	}

	/* for dual channel LCDC (MAIN + SUB) force shared format setting */
	if (num_channels == 2)
		priv->forced_fourcc = pdata->ch[0].fourcc;

	priv->base = ioremap_nocache(res->start, resource_size(res));
	if (!priv->base) {
		error = -ENOMEM;
		goto err1;
	}

	error = sh_mobile_lcdc_setup_clocks(priv, pdata->clock_source);
	if (error) {
		dev_err(&pdev->dev, "unable to setup clocks\n");
		goto err1;
	}

	/* Enable runtime PM. */
	pm_runtime_enable(&pdev->dev);

	for (i = 0; i < num_channels; i++) {
		struct sh_mobile_lcdc_chan *ch = &priv->ch[i];

		error = sh_mobile_lcdc_channel_init(ch);
		if (error)
			goto err1;
	}

	for (i = 0; i < ARRAY_SIZE(pdata->overlays); i++) {
		struct sh_mobile_lcdc_overlay *ovl = &priv->overlays[i];

		ovl->cfg = &pdata->overlays[i];
		ovl->channel = &priv->ch[0];

		error = sh_mobile_lcdc_overlay_init(ovl);
		if (error)
			goto err1;
	}

	error = sh_mobile_lcdc_start(priv);
	if (error) {
		dev_err(&pdev->dev, "unable to start hardware\n");
		goto err1;
	}

	for (i = 0; i < num_channels; i++) {
		struct sh_mobile_lcdc_chan *ch = priv->ch + i;

		error = sh_mobile_lcdc_channel_fb_register(ch);
		if (error)
			goto err1;
	}

	for (i = 0; i < ARRAY_SIZE(pdata->overlays); i++) {
		struct sh_mobile_lcdc_overlay *ovl = &priv->overlays[i];

		error = sh_mobile_lcdc_overlay_fb_register(ovl);
		if (error)
			goto err1;
	}

	/* Failure ignored */
	priv->notifier.notifier_call = sh_mobile_lcdc_notify;
	fb_register_client(&priv->notifier);

	return 0;
err1:
	sh_mobile_lcdc_remove(pdev);

	return error;
}

static struct platform_driver sh_mobile_lcdc_driver = {
	.driver		= {
		.name		= "sh_mobile_lcdc_fb",
		.pm		= &sh_mobile_lcdc_dev_pm_ops,
	},
	.probe		= sh_mobile_lcdc_probe,
	.remove		= sh_mobile_lcdc_remove,
};

module_platform_driver(sh_mobile_lcdc_driver);

MODULE_DESCRIPTION("SuperH Mobile LCDC Framebuffer driver");
MODULE_AUTHOR("Magnus Damm <damm@opensource.se>");
MODULE_LICENSE("GPL v2");
