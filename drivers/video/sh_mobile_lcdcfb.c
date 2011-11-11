/*
 * SuperH Mobile LCDC Framebuffer
 *
 * Copyright (c) 2008 Magnus Damm
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 */

#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/delay.h>
#include <linux/mm.h>
#include <linux/clk.h>
#include <linux/pm_runtime.h>
#include <linux/platform_device.h>
#include <linux/dma-mapping.h>
#include <linux/interrupt.h>
#include <linux/vmalloc.h>
#include <linux/ioctl.h>
#include <linux/slab.h>
#include <linux/console.h>
#include <linux/backlight.h>
#include <linux/gpio.h>
#include <linux/module.h>
#include <video/sh_mobile_lcdc.h>
#include <video/sh_mobile_meram.h>
#include <linux/atomic.h>

#include "sh_mobile_lcdcfb.h"

#define SIDE_B_OFFSET 0x1000
#define MIRROR_OFFSET 0x2000

#define MAX_XRES 1920
#define MAX_YRES 1080

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

struct sh_mobile_lcdc_priv {
	void __iomem *base;
	int irq;
	atomic_t hw_usecnt;
	struct device *dev;
	struct clk *dot_clk;
	unsigned long lddckr;
	struct sh_mobile_lcdc_chan ch[2];
	struct notifier_block notifier;
	int started;
	int forced_bpp; /* 2 channel LCDC must share bpp setting */
	struct sh_mobile_meram_info *meram_dev;
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

static int lcdc_chan_is_sublcd(struct sh_mobile_lcdc_chan *chan)
{
	return chan->cfg.chan == LCDC_CHAN_SUBLCD;
}

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

struct sh_mobile_lcdc_sys_bus_ops sh_mobile_lcdc_sys_bus_ops = {
	lcdc_sys_write_index,
	lcdc_sys_write_data,
	lcdc_sys_read_data,
};

static void sh_mobile_lcdc_clk_on(struct sh_mobile_lcdc_priv *priv)
{
	if (atomic_inc_and_test(&priv->hw_usecnt)) {
		if (priv->dot_clk)
			clk_enable(priv->dot_clk);
		pm_runtime_get_sync(priv->dev);
		if (priv->meram_dev && priv->meram_dev->pdev)
			pm_runtime_get_sync(&priv->meram_dev->pdev->dev);
	}
}

static void sh_mobile_lcdc_clk_off(struct sh_mobile_lcdc_priv *priv)
{
	if (atomic_sub_return(1, &priv->hw_usecnt) == -1) {
		if (priv->meram_dev && priv->meram_dev->pdev)
			pm_runtime_put_sync(&priv->meram_dev->pdev->dev);
		pm_runtime_put(priv->dev);
		if (priv->dot_clk)
			clk_disable(priv->dot_clk);
	}
}

static int sh_mobile_lcdc_sginit(struct fb_info *info,
				  struct list_head *pagelist)
{
	struct sh_mobile_lcdc_chan *ch = info->par;
	unsigned int nr_pages_max = info->fix.smem_len >> PAGE_SHIFT;
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
	struct sh_mobile_lcdc_board_cfg	*bcfg = &ch->cfg.board_cfg;

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
		dma_map_sg(info->dev, ch->sglist, nr_pages, DMA_TO_DEVICE);
		if (bcfg->start_transfer)
			bcfg->start_transfer(bcfg->board_data, ch,
					     &sh_mobile_lcdc_sys_bus_ops);
		lcdc_write_chan(ch, LDSM2R, LDSM2R_OSTRG);
		dma_unmap_sg(info->dev, ch->sglist, nr_pages, DMA_TO_DEVICE);
	} else {
		if (bcfg->start_transfer)
			bcfg->start_transfer(bcfg->board_data, ch,
					     &sh_mobile_lcdc_sys_bus_ops);
		lcdc_write_chan(ch, LDSM2R, LDSM2R_OSTRG);
	}
}

static void sh_mobile_lcdc_deferred_io_touch(struct fb_info *info)
{
	struct fb_deferred_io *fbdefio = info->fbdefio;

	if (fbdefio)
		schedule_delayed_work(&info->deferred_work, fbdefio->delay);
}

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
	struct fb_var_screeninfo *var = &ch->info->var, *display_var = &ch->display_var;
	unsigned long h_total, hsync_pos, display_h_total;
	u32 tmp;

	tmp = ch->ldmt1r_value;
	tmp |= (var->sync & FB_SYNC_VERT_HIGH_ACT) ? 0 : LDMT1R_VPOL;
	tmp |= (var->sync & FB_SYNC_HOR_HIGH_ACT) ? 0 : LDMT1R_HPOL;
	tmp |= (ch->cfg.flags & LCDC_FLAGS_DWPOL) ? LDMT1R_DWPOL : 0;
	tmp |= (ch->cfg.flags & LCDC_FLAGS_DIPOL) ? LDMT1R_DIPOL : 0;
	tmp |= (ch->cfg.flags & LCDC_FLAGS_DAPOL) ? LDMT1R_DAPOL : 0;
	tmp |= (ch->cfg.flags & LCDC_FLAGS_HSCNT) ? LDMT1R_HSCNT : 0;
	tmp |= (ch->cfg.flags & LCDC_FLAGS_DWCNT) ? LDMT1R_DWCNT : 0;
	lcdc_write_chan(ch, LDMT1R, tmp);

	/* setup SYS bus */
	lcdc_write_chan(ch, LDMT2R, ch->cfg.sys_bus_cfg.ldmt2r);
	lcdc_write_chan(ch, LDMT3R, ch->cfg.sys_bus_cfg.ldmt3r);

	/* horizontal configuration */
	h_total = display_var->xres + display_var->hsync_len +
		display_var->left_margin + display_var->right_margin;
	tmp = h_total / 8; /* HTCN */
	tmp |= (min(display_var->xres, var->xres) / 8) << 16; /* HDCN */
	lcdc_write_chan(ch, LDHCNR, tmp);

	hsync_pos = display_var->xres + display_var->right_margin;
	tmp = hsync_pos / 8; /* HSYNP */
	tmp |= (display_var->hsync_len / 8) << 16; /* HSYNW */
	lcdc_write_chan(ch, LDHSYNR, tmp);

	/* vertical configuration */
	tmp = display_var->yres + display_var->vsync_len +
		display_var->upper_margin + display_var->lower_margin; /* VTLN */
	tmp |= min(display_var->yres, var->yres) << 16; /* VDLN */
	lcdc_write_chan(ch, LDVLNR, tmp);

	tmp = display_var->yres + display_var->lower_margin; /* VSYNP */
	tmp |= display_var->vsync_len << 16; /* VSYNW */
	lcdc_write_chan(ch, LDVSYNR, tmp);

	/* Adjust horizontal synchronisation for HDMI */
	display_h_total = display_var->xres + display_var->hsync_len +
		display_var->left_margin + display_var->right_margin;
	tmp = ((display_var->xres & 7) << 24) |
		((display_h_total & 7) << 16) |
		((display_var->hsync_len & 7) << 8) |
		hsync_pos;
	lcdc_write_chan(ch, LDHAJR, tmp);
}

/*
 * __sh_mobile_lcdc_start - Configure and tart the LCDC
 * @priv: LCDC device
 *
 * Configure all enabled channels and start the LCDC device. All external
 * devices (clocks, MERAM, panels, ...) are not touched by this function.
 */
static void __sh_mobile_lcdc_start(struct sh_mobile_lcdc_priv *priv)
{
	struct sh_mobile_lcdc_chan *ch;
	unsigned long tmp;
	int bpp = 0;
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

		if (!bpp)
			bpp = ch->info->var.bits_per_pixel;

		/* Power supply */
		lcdc_write_chan(ch, LDPMR, 0);

		m = ch->cfg.clock_divider;
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

		if (ch->info->var.nonstd) {
			tmp = (ch->info->var.nonstd << 16);
			switch (ch->info->var.bits_per_pixel) {
			case 12:
				tmp |= LDDFR_YF_420;
				break;
			case 16:
				tmp |= LDDFR_YF_422;
				break;
			case 24:
			default:
				tmp |= LDDFR_YF_444;
				break;
			}
		} else {
			switch (ch->info->var.bits_per_pixel) {
			case 16:
				tmp = LDDFR_PKF_RGB16;
				break;
			case 24:
				tmp = LDDFR_PKF_RGB24;
				break;
			case 32:
			default:
				tmp = LDDFR_PKF_ARGB32;
				break;
			}
		}

		lcdc_write_chan(ch, LDDFR, tmp);
		lcdc_write_chan(ch, LDMLSR, ch->pitch);
		lcdc_write_chan(ch, LDSA1R, ch->base_addr_y);
		if (ch->info->var.nonstd)
			lcdc_write_chan(ch, LDSA2R, ch->base_addr_c);

		/* When using deferred I/O mode, configure the LCDC for one-shot
		 * operation and enable the frame end interrupt. Otherwise use
		 * continuous read mode.
		 */
		if (ch->ldmt1r_value & LDMT1R_IFM &&
		    ch->cfg.sys_bus_cfg.deferred_io_msec) {
			lcdc_write_chan(ch, LDSM1R, LDSM1R_OS);
			lcdc_write(priv, _LDINTR, LDINTR_FE);
		} else {
			lcdc_write_chan(ch, LDSM1R, 0);
		}
	}

	/* Word and long word swap. */
	if  (priv->ch[0].info->var.nonstd)
		tmp = LDDDSR_LS | LDDDSR_WS | LDDDSR_BS;
	else {
		switch (bpp) {
		case 16:
			tmp = LDDDSR_LS | LDDDSR_WS;
			break;
		case 24:
			tmp = LDDDSR_LS | LDDDSR_WS | LDDDSR_BS;
			break;
		case 32:
		default:
			tmp = LDDDSR_LS;
			break;
		}
	}
	lcdc_write(priv, _LDDDSR, tmp);

	/* Enable the display output. */
	lcdc_write(priv, _LDCNT1R, LDCNT1R_DE);
	sh_mobile_lcdc_start_stop(priv, 1);
	priv->started = 1;
}

static int sh_mobile_lcdc_start(struct sh_mobile_lcdc_priv *priv)
{
	struct sh_mobile_meram_info *mdev = priv->meram_dev;
	struct sh_mobile_lcdc_board_cfg	*board_cfg;
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
		ch = &priv->ch[k];

		if (!ch->enabled)
			continue;

		board_cfg = &ch->cfg.board_cfg;
		if (board_cfg->setup_sys) {
			ret = board_cfg->setup_sys(board_cfg->board_data, ch,
						   &sh_mobile_lcdc_sys_bus_ops);
			if (ret)
				return ret;
		}
	}

	/* Compute frame buffer base address and pitch for each channel. */
	for (k = 0; k < ARRAY_SIZE(priv->ch); k++) {
		struct sh_mobile_meram_cfg *cfg;
		int pixelformat;

		ch = &priv->ch[k];
		if (!ch->enabled)
			continue;

		ch->base_addr_y = ch->info->fix.smem_start;
		ch->base_addr_c = ch->base_addr_y
				+ ch->info->var.xres
				* ch->info->var.yres_virtual;
		ch->pitch = ch->info->fix.line_length;

		/* Enable MERAM if possible. */
		cfg = ch->cfg.meram_cfg;
		if (mdev == NULL || mdev->ops == NULL || cfg == NULL)
			continue;

		/* we need to de-init configured ICBs before we can
		 * re-initialize them.
		 */
		if (ch->meram_enabled) {
			mdev->ops->meram_unregister(mdev, cfg);
			ch->meram_enabled = 0;
		}

		if (!ch->info->var.nonstd)
			pixelformat = SH_MOBILE_MERAM_PF_RGB;
		else if (ch->info->var.bits_per_pixel == 24)
			pixelformat = SH_MOBILE_MERAM_PF_NV24;
		else
			pixelformat = SH_MOBILE_MERAM_PF_NV;

		ret = mdev->ops->meram_register(mdev, cfg, ch->pitch,
					ch->info->var.yres, pixelformat,
					ch->base_addr_y, ch->base_addr_c,
					&ch->base_addr_y, &ch->base_addr_c,
					&ch->pitch);
		if (!ret)
			ch->meram_enabled = 1;
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

		tmp = ch->cfg.sys_bus_cfg.deferred_io_msec;
		if (ch->ldmt1r_value & LDMT1R_IFM && tmp) {
			ch->defio.deferred_io = sh_mobile_lcdc_deferred_io;
			ch->defio.delay = msecs_to_jiffies(tmp);
			ch->info->fbdefio = &ch->defio;
			fb_deferred_io_init(ch->info);
		}

		board_cfg = &ch->cfg.board_cfg;
		if (board_cfg->display_on && try_module_get(board_cfg->owner)) {
			board_cfg->display_on(board_cfg->board_data, ch->info);
			module_put(board_cfg->owner);
		}

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
	struct sh_mobile_lcdc_board_cfg	*board_cfg;
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

		board_cfg = &ch->cfg.board_cfg;
		if (board_cfg->display_off && try_module_get(board_cfg->owner)) {
			board_cfg->display_off(board_cfg->board_data);
			module_put(board_cfg->owner);
		}

		/* disable the meram */
		if (ch->meram_enabled) {
			struct sh_mobile_meram_cfg *cfg;
			struct sh_mobile_meram_info *mdev;
			cfg = ch->cfg.meram_cfg;
			mdev = priv->meram_dev;
			mdev->ops->meram_unregister(mdev, cfg);
			ch->meram_enabled = 0;
		}

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

static int sh_mobile_lcdc_check_interface(struct sh_mobile_lcdc_chan *ch)
{
	int interface_type = ch->cfg.interface_type;

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

static int sh_mobile_lcdc_setup_clocks(struct platform_device *pdev,
				       int clock_source,
				       struct sh_mobile_lcdc_priv *priv)
{
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

	if (str) {
		priv->dot_clk = clk_get(&pdev->dev, str);
		if (IS_ERR(priv->dot_clk)) {
			dev_err(&pdev->dev, "cannot get dot clock %s\n", str);
			return PTR_ERR(priv->dot_clk);
		}
	}

	/* Runtime PM support involves two step for this driver:
	 * 1) Enable Runtime PM
	 * 2) Force Runtime PM Resume since hardware is accessed from probe()
	 */
	priv->dev = &pdev->dev;
	pm_runtime_enable(priv->dev);
	pm_runtime_resume(priv->dev);
	return 0;
}

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

static struct fb_fix_screeninfo sh_mobile_lcdc_fix  = {
	.id =		"SH Mobile LCDC",
	.type =		FB_TYPE_PACKED_PIXELS,
	.visual =	FB_VISUAL_TRUECOLOR,
	.accel =	FB_ACCEL_NONE,
	.xpanstep =	0,
	.ypanstep =	1,
	.ywrapstep =	0,
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

static int sh_mobile_fb_pan_display(struct fb_var_screeninfo *var,
				     struct fb_info *info)
{
	struct sh_mobile_lcdc_chan *ch = info->par;
	struct sh_mobile_lcdc_priv *priv = ch->lcdc;
	unsigned long ldrcntr;
	unsigned long new_pan_offset;
	unsigned long base_addr_y, base_addr_c;
	unsigned long c_offset;

	if (!info->var.nonstd)
		new_pan_offset = var->yoffset * info->fix.line_length
			       + var->xoffset * (info->var.bits_per_pixel / 8);
	else
		new_pan_offset = var->yoffset * info->fix.line_length
			       + var->xoffset;

	if (new_pan_offset == ch->pan_offset)
		return 0;	/* No change, do nothing */

	ldrcntr = lcdc_read(priv, _LDRCNTR);

	/* Set the source address for the next refresh */
	base_addr_y = ch->dma_handle + new_pan_offset;
	if (info->var.nonstd) {
		/* Set y offset */
		c_offset = var->yoffset * info->fix.line_length
			 * (info->var.bits_per_pixel - 8) / 8;
		base_addr_c = ch->dma_handle
			    + info->var.xres * info->var.yres_virtual
			    + c_offset;
		/* Set x offset */
		if (info->var.bits_per_pixel == 24)
			base_addr_c += 2 * var->xoffset;
		else
			base_addr_c += var->xoffset;
	}

	if (ch->meram_enabled) {
		struct sh_mobile_meram_cfg *cfg;
		struct sh_mobile_meram_info *mdev;
		int ret;

		cfg = ch->cfg.meram_cfg;
		mdev = priv->meram_dev;
		ret = mdev->ops->meram_update(mdev, cfg,
					base_addr_y, base_addr_c,
					&base_addr_y, &base_addr_c);
		if (ret)
			return ret;
	}

	ch->base_addr_y = base_addr_y;
	ch->base_addr_c = base_addr_c;

	lcdc_write_chan_mirror(ch, LDSA1R, base_addr_y);
	if (info->var.nonstd)
		lcdc_write_chan_mirror(ch, LDSA2R, base_addr_c);

	if (lcdc_chan_is_sublcd(ch))
		lcdc_write(ch->lcdc, _LDRCNTR, ldrcntr ^ LDRCNTR_SRS);
	else
		lcdc_write(ch->lcdc, _LDRCNTR, ldrcntr ^ LDRCNTR_MRS);

	ch->pan_offset = new_pan_offset;

	sh_mobile_lcdc_deferred_io_touch(info);

	return 0;
}

static int sh_mobile_wait_for_vsync(struct fb_info *info)
{
	struct sh_mobile_lcdc_chan *ch = info->par;
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

static int sh_mobile_ioctl(struct fb_info *info, unsigned int cmd,
		       unsigned long arg)
{
	int retval;

	switch (cmd) {
	case FBIO_WAITFORVSYNC:
		retval = sh_mobile_wait_for_vsync(info);
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
	struct fb_videomode mode1, mode2;
	struct fb_event event;
	int evnt = FB_EVENT_MODE_CHANGE_ALL;

	if (ch->use_count > 1 || (ch->use_count == 1 && !info->fbcon_par))
		/* More framebuffer users are active */
		return;

	fb_var_to_videomode(&mode1, &ch->display_var);
	fb_var_to_videomode(&mode2, &info->var);

	if (fb_mode_is_equal(&mode1, &mode2))
		return;

	/* Display has been re-plugged, framebuffer is free now, reconfigure */
	if (fb_set_var(info, &ch->display_var) < 0)
		/* Couldn't reconfigure, hopefully, can continue as before */
		return;

	/*
	 * fb_set_var() calls the notifier change internally, only if
	 * FBINFO_MISC_USEREVENT flag is set. Since we do not want to fake a
	 * user event, we have to call the chain ourselves.
	 */
	event.info = info;
	event.data = &mode1;
	fb_notifier_call_chain(evnt, &event);
}

/*
 * Locking: both .fb_release() and .fb_open() are called with info->lock held if
 * user == 1, or with console sem held, if user == 0.
 */
static int sh_mobile_release(struct fb_info *info, int user)
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

static int sh_mobile_open(struct fb_info *info, int user)
{
	struct sh_mobile_lcdc_chan *ch = info->par;

	mutex_lock(&ch->open_lock);
	ch->use_count++;

	dev_dbg(info->dev, "%s(): %d users\n", __func__, ch->use_count);
	mutex_unlock(&ch->open_lock);

	return 0;
}

static int sh_mobile_check_var(struct fb_var_screeninfo *var, struct fb_info *info)
{
	struct sh_mobile_lcdc_chan *ch = info->par;
	struct sh_mobile_lcdc_priv *p = ch->lcdc;
	unsigned int best_dist = (unsigned int)-1;
	unsigned int best_xres = 0;
	unsigned int best_yres = 0;
	unsigned int i;

	if (var->xres > MAX_XRES || var->yres > MAX_YRES)
		return -EINVAL;

	/* If board code provides us with a list of available modes, make sure
	 * we use one of them. Find the mode closest to the requested one. The
	 * distance between two modes is defined as the size of the
	 * non-overlapping parts of the two rectangles.
	 */
	for (i = 0; i < ch->cfg.num_cfg; ++i) {
		const struct fb_videomode *mode = &ch->cfg.lcd_cfg[i];
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
	if (ch->cfg.num_cfg != 0) {
		if (best_dist == (unsigned int)-1)
			return -EINVAL;

		var->xres = best_xres;
		var->yres = best_yres;
	}

	/* Make sure the virtual resolution is at least as big as the visible
	 * resolution.
	 */
	if (var->xres_virtual < var->xres)
		var->xres_virtual = var->xres;
	if (var->yres_virtual < var->yres)
		var->yres_virtual = var->yres;

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

	/* Make sure we don't exceed our allocated memory. */
	if (var->xres_virtual * var->yres_virtual * var->bits_per_pixel / 8 >
	    info->fix.smem_len)
		return -EINVAL;

	/* only accept the forced_bpp for dual channel configurations */
	if (p->forced_bpp && p->forced_bpp != var->bits_per_pixel)
		return -EINVAL;

	return 0;
}

static int sh_mobile_set_par(struct fb_info *info)
{
	struct sh_mobile_lcdc_chan *ch = info->par;
	u32 line_length = info->fix.line_length;
	int ret;

	sh_mobile_lcdc_stop(ch->lcdc);

	if (info->var.nonstd)
		info->fix.line_length = info->var.xres;
	else
		info->fix.line_length = info->var.xres
				      * info->var.bits_per_pixel / 8;

	ret = sh_mobile_lcdc_start(ch->lcdc);
	if (ret < 0) {
		dev_err(info->dev, "%s: unable to restart LCDC\n", __func__);
		info->fix.line_length = line_length;
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
			.width = info->var.xres,
			.height = info->var.yres,
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
			sh_mobile_wait_for_vsync(info);
			sh_mobile_wait_for_vsync(info);
		}
		sh_mobile_lcdc_clk_off(p);
	}

	ch->blank_status = blank;
	return 0;
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
	.fb_pan_display = sh_mobile_fb_pan_display,
	.fb_ioctl       = sh_mobile_ioctl,
	.fb_open	= sh_mobile_open,
	.fb_release	= sh_mobile_release,
	.fb_check_var	= sh_mobile_check_var,
	.fb_set_par	= sh_mobile_set_par,
};

static int sh_mobile_lcdc_update_bl(struct backlight_device *bdev)
{
	struct sh_mobile_lcdc_chan *ch = bl_get_data(bdev);
	struct sh_mobile_lcdc_board_cfg *cfg = &ch->cfg.board_cfg;
	int brightness = bdev->props.brightness;

	if (bdev->props.power != FB_BLANK_UNBLANK ||
	    bdev->props.state & (BL_CORE_SUSPENDED | BL_CORE_FBBLANK))
		brightness = 0;

	return cfg->set_brightness(cfg->board_data, brightness);
}

static int sh_mobile_lcdc_get_brightness(struct backlight_device *bdev)
{
	struct sh_mobile_lcdc_chan *ch = bl_get_data(bdev);
	struct sh_mobile_lcdc_board_cfg *cfg = &ch->cfg.board_cfg;

	return cfg->get_brightness(cfg->board_data);
}

static int sh_mobile_lcdc_check_fb(struct backlight_device *bdev,
				   struct fb_info *info)
{
	return (info->bl_dev == bdev);
}

static struct backlight_ops sh_mobile_lcdc_bl_ops = {
	.options	= BL_CORE_SUSPENDRESUME,
	.update_status	= sh_mobile_lcdc_update_bl,
	.get_brightness	= sh_mobile_lcdc_get_brightness,
	.check_fb	= sh_mobile_lcdc_check_fb,
};

static struct backlight_device *sh_mobile_lcdc_bl_probe(struct device *parent,
					       struct sh_mobile_lcdc_chan *ch)
{
	struct backlight_device *bl;

	bl = backlight_device_register(ch->cfg.bl_info.name, parent, ch,
				       &sh_mobile_lcdc_bl_ops, NULL);
	if (IS_ERR(bl)) {
		dev_err(parent, "unable to register backlight device: %ld\n",
			PTR_ERR(bl));
		return NULL;
	}

	bl->props.max_brightness = ch->cfg.bl_info.max_brightness;
	bl->props.brightness = bl->props.max_brightness;
	backlight_update_status(bl);

	return bl;
}

static void sh_mobile_lcdc_bl_remove(struct backlight_device *bdev)
{
	backlight_device_unregister(bdev);
}

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
	struct platform_device *pdev = to_platform_device(dev);
	struct sh_mobile_lcdc_priv *priv = platform_get_drvdata(pdev);

	/* turn off LCDC hardware */
	lcdc_write(priv, _LDCNT1R, 0);

	return 0;
}

static int sh_mobile_lcdc_runtime_resume(struct device *dev)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct sh_mobile_lcdc_priv *priv = platform_get_drvdata(pdev);

	__sh_mobile_lcdc_start(priv);

	return 0;
}

static const struct dev_pm_ops sh_mobile_lcdc_dev_pm_ops = {
	.suspend = sh_mobile_lcdc_suspend,
	.resume = sh_mobile_lcdc_resume,
	.runtime_suspend = sh_mobile_lcdc_runtime_suspend,
	.runtime_resume = sh_mobile_lcdc_runtime_resume,
};

/* locking: called with info->lock held */
static int sh_mobile_lcdc_notify(struct notifier_block *nb,
				 unsigned long action, void *data)
{
	struct fb_event *event = data;
	struct fb_info *info = event->info;
	struct sh_mobile_lcdc_chan *ch = info->par;
	struct sh_mobile_lcdc_board_cfg	*board_cfg = &ch->cfg.board_cfg;

	if (&ch->lcdc->notifier != nb)
		return NOTIFY_DONE;

	dev_dbg(info->dev, "%s(): action = %lu, data = %p\n",
		__func__, action, event->data);

	switch(action) {
	case FB_EVENT_SUSPEND:
		if (board_cfg->display_off && try_module_get(board_cfg->owner)) {
			board_cfg->display_off(board_cfg->board_data);
			module_put(board_cfg->owner);
		}
		sh_mobile_lcdc_stop(ch->lcdc);
		break;
	case FB_EVENT_RESUME:
		mutex_lock(&ch->open_lock);
		sh_mobile_fb_reconfig(info);
		mutex_unlock(&ch->open_lock);

		/* HDMI must be enabled before LCDC configuration */
		if (board_cfg->display_on && try_module_get(board_cfg->owner)) {
			board_cfg->display_on(board_cfg->board_data, info);
			module_put(board_cfg->owner);
		}

		sh_mobile_lcdc_start(ch->lcdc);
	}

	return NOTIFY_OK;
}

static int sh_mobile_lcdc_remove(struct platform_device *pdev)
{
	struct sh_mobile_lcdc_priv *priv = platform_get_drvdata(pdev);
	struct fb_info *info;
	int i;

	fb_unregister_client(&priv->notifier);

	for (i = 0; i < ARRAY_SIZE(priv->ch); i++)
		if (priv->ch[i].info && priv->ch[i].info->dev)
			unregister_framebuffer(priv->ch[i].info);

	sh_mobile_lcdc_stop(priv);

	for (i = 0; i < ARRAY_SIZE(priv->ch); i++) {
		info = priv->ch[i].info;

		if (!info || !info->device)
			continue;

		if (priv->ch[i].sglist)
			vfree(priv->ch[i].sglist);

		if (info->screen_base)
			dma_free_coherent(&pdev->dev, info->fix.smem_len,
					  info->screen_base,
					  priv->ch[i].dma_handle);
		fb_dealloc_cmap(&info->cmap);
		framebuffer_release(info);
	}

	for (i = 0; i < ARRAY_SIZE(priv->ch); i++) {
		if (priv->ch[i].bl)
			sh_mobile_lcdc_bl_remove(priv->ch[i].bl);
	}

	if (priv->dot_clk)
		clk_put(priv->dot_clk);

	if (priv->dev)
		pm_runtime_disable(priv->dev);

	if (priv->base)
		iounmap(priv->base);

	if (priv->irq)
		free_irq(priv->irq, priv);
	kfree(priv);
	return 0;
}

static int __devinit sh_mobile_lcdc_channel_init(struct sh_mobile_lcdc_chan *ch,
						 struct device *dev)
{
	struct sh_mobile_lcdc_chan_cfg *cfg = &ch->cfg;
	const struct fb_videomode *max_mode;
	const struct fb_videomode *mode;
	struct fb_var_screeninfo *var;
	struct fb_info *info;
	unsigned int max_size;
	int num_cfg;
	void *buf;
	int ret;
	int i;

	mutex_init(&ch->open_lock);

	/* Allocate the frame buffer device. */
	ch->info = framebuffer_alloc(0, dev);
	if (!ch->info) {
		dev_err(dev, "unable to allocate fb_info\n");
		return -ENOMEM;
	}

	info = ch->info;
	info->fbops = &sh_mobile_lcdc_ops;
	info->par = ch;
	info->pseudo_palette = &ch->pseudo_palette;
	info->flags = FBINFO_FLAG_DEFAULT;

	/* Iterate through the modes to validate them and find the highest
	 * resolution.
	 */
	max_mode = NULL;
	max_size = 0;

	for (i = 0, mode = cfg->lcd_cfg; i < cfg->num_cfg; i++, mode++) {
		unsigned int size = mode->yres * mode->xres;

		/* NV12 buffers must have even number of lines */
		if ((cfg->nonstd) && cfg->bpp == 12 &&
				(mode->yres & 0x1)) {
			dev_err(dev, "yres must be multiple of 2 for YCbCr420 "
				"mode.\n");
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

	/* Initialize fixed screen information. Restrict pan to 2 lines steps
	 * for NV12.
	 */
	info->fix = sh_mobile_lcdc_fix;
	info->fix.smem_len = max_size * 2 * cfg->bpp / 8;
	if (cfg->nonstd && cfg->bpp == 12)
		info->fix.ypanstep = 2;

	/* Create the mode list. */
	if (cfg->lcd_cfg == NULL) {
		mode = &default_720p;
		num_cfg = 1;
	} else {
		mode = cfg->lcd_cfg;
		num_cfg = cfg->num_cfg;
	}

	fb_videomode_to_modelist(mode, num_cfg, &info->modelist);

	/* Initialize variable screen information using the first mode as
	 * default. The default Y virtual resolution is twice the panel size to
	 * allow for double-buffering.
	 */
	var = &info->var;
	fb_videomode_to_var(var, mode);
	var->bits_per_pixel = cfg->bpp;
	var->width = cfg->lcd_size_cfg.width;
	var->height = cfg->lcd_size_cfg.height;
	var->yres_virtual = var->yres * 2;
	var->activate = FB_ACTIVATE_NOW;

	ret = sh_mobile_check_var(var, info);
	if (ret)
		return ret;

	/* Allocate frame buffer memory and color map. */
	buf = dma_alloc_coherent(dev, info->fix.smem_len, &ch->dma_handle,
				 GFP_KERNEL);
	if (!buf) {
		dev_err(dev, "unable to allocate buffer\n");
		return -ENOMEM;
	}

	ret = fb_alloc_cmap(&info->cmap, PALETTE_NR, 0);
	if (ret < 0) {
		dev_err(dev, "unable to allocate cmap\n");
		dma_free_coherent(dev, info->fix.smem_len,
				  buf, ch->dma_handle);
		return ret;
	}

	info->fix.smem_start = ch->dma_handle;
	if (var->nonstd)
		info->fix.line_length = var->xres;
	else
		info->fix.line_length = var->xres * (cfg->bpp / 8);

	info->screen_base = buf;
	info->device = dev;
	ch->display_var = *var;

	return 0;
}

static int __devinit sh_mobile_lcdc_probe(struct platform_device *pdev)
{
	struct sh_mobile_lcdc_info *pdata = pdev->dev.platform_data;
	struct sh_mobile_lcdc_priv *priv;
	struct resource *res;
	int num_channels;
	int error;
	int i;

	if (!pdata) {
		dev_err(&pdev->dev, "no platform data defined\n");
		return -EINVAL;
	}

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	i = platform_get_irq(pdev, 0);
	if (!res || i < 0) {
		dev_err(&pdev->dev, "cannot get platform resources\n");
		return -ENOENT;
	}

	priv = kzalloc(sizeof(*priv), GFP_KERNEL);
	if (!priv) {
		dev_err(&pdev->dev, "cannot allocate device data\n");
		return -ENOMEM;
	}

	platform_set_drvdata(pdev, priv);

	error = request_irq(i, sh_mobile_lcdc_irq, 0,
			    dev_name(&pdev->dev), priv);
	if (error) {
		dev_err(&pdev->dev, "unable to request irq\n");
		goto err1;
	}

	priv->irq = i;
	atomic_set(&priv->hw_usecnt, -1);

	for (i = 0, num_channels = 0; i < ARRAY_SIZE(pdata->ch); i++) {
		struct sh_mobile_lcdc_chan *ch = priv->ch + num_channels;

		ch->lcdc = priv;
		memcpy(&ch->cfg, &pdata->ch[i], sizeof(pdata->ch[i]));

		error = sh_mobile_lcdc_check_interface(ch);
		if (error) {
			dev_err(&pdev->dev, "unsupported interface type\n");
			goto err1;
		}
		init_waitqueue_head(&ch->frame_end_wait);
		init_completion(&ch->vsync_completion);
		ch->pan_offset = 0;

		/* probe the backlight is there is one defined */
		if (ch->cfg.bl_info.max_brightness)
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

	/* for dual channel LCDC (MAIN + SUB) force shared bpp setting */
	if (num_channels == 2)
		priv->forced_bpp = pdata->ch[0].bpp;

	priv->base = ioremap_nocache(res->start, resource_size(res));
	if (!priv->base)
		goto err1;

	error = sh_mobile_lcdc_setup_clocks(pdev, pdata->clock_source, priv);
	if (error) {
		dev_err(&pdev->dev, "unable to setup clocks\n");
		goto err1;
	}

	priv->meram_dev = pdata->meram_dev;

	for (i = 0; i < num_channels; i++) {
		struct sh_mobile_lcdc_chan *ch = priv->ch + i;

		error = sh_mobile_lcdc_channel_init(ch, &pdev->dev);
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
		struct fb_info *info = ch->info;

		if (info->fbdefio) {
			ch->sglist = vmalloc(sizeof(struct scatterlist) *
					info->fix.smem_len >> PAGE_SHIFT);
			if (!ch->sglist) {
				dev_err(&pdev->dev, "cannot allocate sglist\n");
				goto err1;
			}
		}

		info->bl_dev = ch->bl;

		error = register_framebuffer(info);
		if (error < 0)
			goto err1;

		dev_info(info->dev,
			 "registered %s/%s as %dx%d %dbpp.\n",
			 pdev->name,
			 (ch->cfg.chan == LCDC_CHAN_MAINLCD) ?
			 "mainlcd" : "sublcd",
			 info->var.xres, info->var.yres,
			 ch->cfg.bpp);

		/* deferred io mode: disable clock to save power */
		if (info->fbdefio || info->state == FBINFO_STATE_SUSPENDED)
			sh_mobile_lcdc_clk_off(priv);
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
		.owner		= THIS_MODULE,
		.pm		= &sh_mobile_lcdc_dev_pm_ops,
	},
	.probe		= sh_mobile_lcdc_probe,
	.remove		= sh_mobile_lcdc_remove,
};

static int __init sh_mobile_lcdc_init(void)
{
	return platform_driver_register(&sh_mobile_lcdc_driver);
}

static void __exit sh_mobile_lcdc_exit(void)
{
	platform_driver_unregister(&sh_mobile_lcdc_driver);
}

module_init(sh_mobile_lcdc_init);
module_exit(sh_mobile_lcdc_exit);

MODULE_DESCRIPTION("SuperH Mobile LCDC Framebuffer driver");
MODULE_AUTHOR("Magnus Damm <damm@opensource.se>");
MODULE_LICENSE("GPL v2");
