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
#include <linux/fb.h>
#include <linux/clk.h>
#include <linux/platform_device.h>
#include <linux/dma-mapping.h>
#include <linux/interrupt.h>
#include <linux/vmalloc.h>
#include <video/sh_mobile_lcdc.h>
#include <asm/atomic.h>

#define PALETTE_NR 16

struct sh_mobile_lcdc_priv;
struct sh_mobile_lcdc_chan {
	struct sh_mobile_lcdc_priv *lcdc;
	unsigned long *reg_offs;
	unsigned long ldmt1r_value;
	unsigned long enabled; /* ME and SE in LDCNT2R */
	struct sh_mobile_lcdc_chan_cfg cfg;
	u32 pseudo_palette[PALETTE_NR];
	struct fb_info info;
	dma_addr_t dma_handle;
	struct fb_deferred_io defio;
	struct scatterlist *sglist;
	unsigned long frame_end;
	wait_queue_head_t frame_end_wait;
};

struct sh_mobile_lcdc_priv {
	void __iomem *base;
	int irq;
#ifdef CONFIG_HAVE_CLK
	atomic_t clk_usecnt;
	struct clk *dot_clk;
	struct clk *clk;
#endif
	unsigned long lddckr;
	struct sh_mobile_lcdc_chan ch[2];
	int started;
};

/* shared registers */
#define _LDDCKR 0x410
#define _LDDCKSTPR 0x414
#define _LDINTR 0x468
#define _LDSR 0x46c
#define _LDCNT1R 0x470
#define _LDCNT2R 0x474
#define _LDDDSR 0x47c
#define _LDDWD0R 0x800
#define _LDDRDR 0x840
#define _LDDWAR 0x900
#define _LDDRAR 0x904

/* per-channel registers */
enum { LDDCKPAT1R, LDDCKPAT2R, LDMT1R, LDMT2R, LDMT3R, LDDFR, LDSM1R,
       LDSM2R, LDSA1R, LDMLSR, LDHCNR, LDHSYNR, LDVLNR, LDVSYNR, LDPMR };

static unsigned long lcdc_offs_mainlcd[] = {
	[LDDCKPAT1R] = 0x400,
	[LDDCKPAT2R] = 0x404,
	[LDMT1R] = 0x418,
	[LDMT2R] = 0x41c,
	[LDMT3R] = 0x420,
	[LDDFR] = 0x424,
	[LDSM1R] = 0x428,
	[LDSM2R] = 0x42c,
	[LDSA1R] = 0x430,
	[LDMLSR] = 0x438,
	[LDHCNR] = 0x448,
	[LDHSYNR] = 0x44c,
	[LDVLNR] = 0x450,
	[LDVSYNR] = 0x454,
	[LDPMR] = 0x460,
};

static unsigned long lcdc_offs_sublcd[] = {
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

#define START_LCDC	0x00000001
#define LCDC_RESET	0x00000100
#define DISPLAY_BEU	0x00000008
#define LCDC_ENABLE	0x00000001
#define LDINTR_FE	0x00000400
#define LDINTR_FS	0x00000004

static void lcdc_write_chan(struct sh_mobile_lcdc_chan *chan,
			    int reg_nr, unsigned long data)
{
	iowrite32(data, chan->lcdc->base + chan->reg_offs[reg_nr]);
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

	lcdc_write(ch->lcdc, _LDDWD0R, data | 0x10000000);
	lcdc_wait_bit(ch->lcdc, _LDSR, 2, 0);
	lcdc_write(ch->lcdc, _LDDWAR, 1 | (lcdc_chan_is_sublcd(ch) ? 2 : 0));
}

static void lcdc_sys_write_data(void *handle, unsigned long data)
{
	struct sh_mobile_lcdc_chan *ch = handle;

	lcdc_write(ch->lcdc, _LDDWD0R, data | 0x11000000);
	lcdc_wait_bit(ch->lcdc, _LDSR, 2, 0);
	lcdc_write(ch->lcdc, _LDDWAR, 1 | (lcdc_chan_is_sublcd(ch) ? 2 : 0));
}

static unsigned long lcdc_sys_read_data(void *handle)
{
	struct sh_mobile_lcdc_chan *ch = handle;

	lcdc_write(ch->lcdc, _LDDRDR, 0x01000000);
	lcdc_wait_bit(ch->lcdc, _LDSR, 2, 0);
	lcdc_write(ch->lcdc, _LDDRAR, 1 | (lcdc_chan_is_sublcd(ch) ? 2 : 0));
	udelay(1);

	return lcdc_read(ch->lcdc, _LDDRDR) & 0xffff;
}

struct sh_mobile_lcdc_sys_bus_ops sh_mobile_lcdc_sys_bus_ops = {
	lcdc_sys_write_index,
	lcdc_sys_write_data,
	lcdc_sys_read_data,
};

#ifdef CONFIG_HAVE_CLK
static void sh_mobile_lcdc_clk_on(struct sh_mobile_lcdc_priv *priv)
{
	if (atomic_inc_and_test(&priv->clk_usecnt)) {
		clk_enable(priv->clk);
		if (priv->dot_clk)
			clk_enable(priv->dot_clk);
	}
}

static void sh_mobile_lcdc_clk_off(struct sh_mobile_lcdc_priv *priv)
{
	if (atomic_sub_return(1, &priv->clk_usecnt) == -1) {
		if (priv->dot_clk)
			clk_disable(priv->dot_clk);
		clk_disable(priv->clk);
	}
}
#else
static void sh_mobile_lcdc_clk_on(struct sh_mobile_lcdc_priv *priv) {}
static void sh_mobile_lcdc_clk_off(struct sh_mobile_lcdc_priv *priv) {}
#endif

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
	unsigned int nr_pages;

	/* enable clocks before accessing hardware */
	sh_mobile_lcdc_clk_on(ch->lcdc);

	nr_pages = sh_mobile_lcdc_sginit(info, pagelist);
	dma_map_sg(info->dev, ch->sglist, nr_pages, DMA_TO_DEVICE);

	/* trigger panel update */
	lcdc_write_chan(ch, LDSM2R, 1);

	dma_unmap_sg(info->dev, ch->sglist, nr_pages, DMA_TO_DEVICE);
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
	unsigned long tmp;
	int is_sub;
	int k;

	/* acknowledge interrupt */
	tmp = lcdc_read(priv, _LDINTR);
	tmp &= 0xffffff00; /* mask in high 24 bits */
	tmp |= 0x000000ff ^ LDINTR_FS; /* status in low 8 */
	lcdc_write(priv, _LDINTR, tmp);

	/* figure out if this interrupt is for main or sub lcd */
	is_sub = (lcdc_read(priv, _LDSR) & (1 << 10)) ? 1 : 0;

	/* wake up channel and disable clocks*/
	for (k = 0; k < ARRAY_SIZE(priv->ch); k++) {
		ch = &priv->ch[k];

		if (!ch->enabled)
			continue;

		if (is_sub == lcdc_chan_is_sublcd(ch)) {
			ch->frame_end = 1;
			wake_up(&ch->frame_end_wait);

			sh_mobile_lcdc_clk_off(priv);
		}
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
		lcdc_write(priv, _LDCNT2R, tmp | START_LCDC);
	else
		lcdc_write(priv, _LDCNT2R, tmp & ~START_LCDC);

	/* wait until power is applied/stopped on all channels */
	for (k = 0; k < ARRAY_SIZE(priv->ch); k++)
		if (lcdc_read(priv, _LDCNT2R) & priv->ch[k].enabled)
			while (1) {
				tmp = lcdc_read_chan(&priv->ch[k], LDPMR) & 3;
				if (start && tmp == 3)
					break;
				if (!start && tmp == 0)
					break;
				cpu_relax();
			}

	if (!start)
		lcdc_write(priv, _LDDCKSTPR, 1); /* stop dotclock */
}

static int sh_mobile_lcdc_start(struct sh_mobile_lcdc_priv *priv)
{
	struct sh_mobile_lcdc_chan *ch;
	struct fb_videomode *lcd_cfg;
	struct sh_mobile_lcdc_board_cfg	*board_cfg;
	unsigned long tmp;
	int k, m;
	int ret = 0;

	/* enable clocks before accessing the hardware */
	for (k = 0; k < ARRAY_SIZE(priv->ch); k++)
		if (priv->ch[k].enabled)
			sh_mobile_lcdc_clk_on(priv);

	/* reset */
	lcdc_write(priv, _LDCNT2R, lcdc_read(priv, _LDCNT2R) | LCDC_RESET);
	lcdc_wait_bit(priv, _LDCNT2R, LCDC_RESET, 0);

	/* enable LCDC channels */
	tmp = lcdc_read(priv, _LDCNT2R);
	tmp |= priv->ch[0].enabled;
	tmp |= priv->ch[1].enabled;
	lcdc_write(priv, _LDCNT2R, tmp);

	/* read data from external memory, avoid using the BEU for now */
	lcdc_write(priv, _LDCNT2R, lcdc_read(priv, _LDCNT2R) & ~DISPLAY_BEU);

	/* stop the lcdc first */
	sh_mobile_lcdc_start_stop(priv, 0);

	/* configure clocks */
	tmp = priv->lddckr;
	for (k = 0; k < ARRAY_SIZE(priv->ch); k++) {
		ch = &priv->ch[k];

		if (!priv->ch[k].enabled)
			continue;

		m = ch->cfg.clock_divider;
		if (!m)
			continue;

		if (m == 1)
			m = 1 << 6;
		tmp |= m << (lcdc_chan_is_sublcd(ch) ? 8 : 0);

		lcdc_write_chan(ch, LDDCKPAT1R, 0x00000000);
		lcdc_write_chan(ch, LDDCKPAT2R, (1 << (m/2)) - 1);
	}

	lcdc_write(priv, _LDDCKR, tmp);

	/* start dotclock again */
	lcdc_write(priv, _LDDCKSTPR, 0);
	lcdc_wait_bit(priv, _LDDCKSTPR, ~0, 0);

	/* interrupts are disabled to begin with */
	lcdc_write(priv, _LDINTR, 0);

	for (k = 0; k < ARRAY_SIZE(priv->ch); k++) {
		ch = &priv->ch[k];
		lcd_cfg = &ch->cfg.lcd_cfg;

		if (!ch->enabled)
			continue;

		tmp = ch->ldmt1r_value;
		tmp |= (lcd_cfg->sync & FB_SYNC_VERT_HIGH_ACT) ? 0 : 1 << 28;
		tmp |= (lcd_cfg->sync & FB_SYNC_HOR_HIGH_ACT) ? 0 : 1 << 27;
		tmp |= (ch->cfg.flags & LCDC_FLAGS_DWPOL) ? 1 << 26 : 0;
		tmp |= (ch->cfg.flags & LCDC_FLAGS_DIPOL) ? 1 << 25 : 0;
		tmp |= (ch->cfg.flags & LCDC_FLAGS_DAPOL) ? 1 << 24 : 0;
		tmp |= (ch->cfg.flags & LCDC_FLAGS_HSCNT) ? 1 << 17 : 0;
		tmp |= (ch->cfg.flags & LCDC_FLAGS_DWCNT) ? 1 << 16 : 0;
		lcdc_write_chan(ch, LDMT1R, tmp);

		/* setup SYS bus */
		lcdc_write_chan(ch, LDMT2R, ch->cfg.sys_bus_cfg.ldmt2r);
		lcdc_write_chan(ch, LDMT3R, ch->cfg.sys_bus_cfg.ldmt3r);

		/* horizontal configuration */
		tmp = lcd_cfg->xres + lcd_cfg->hsync_len;
		tmp += lcd_cfg->left_margin;
		tmp += lcd_cfg->right_margin;
		tmp /= 8; /* HTCN */
		tmp |= (lcd_cfg->xres / 8) << 16; /* HDCN */
		lcdc_write_chan(ch, LDHCNR, tmp);

		tmp = lcd_cfg->xres;
		tmp += lcd_cfg->right_margin;
		tmp /= 8; /* HSYNP */
		tmp |= (lcd_cfg->hsync_len / 8) << 16; /* HSYNW */
		lcdc_write_chan(ch, LDHSYNR, tmp);

		/* power supply */
		lcdc_write_chan(ch, LDPMR, 0);

		/* vertical configuration */
		tmp = lcd_cfg->yres + lcd_cfg->vsync_len;
		tmp += lcd_cfg->upper_margin;
		tmp += lcd_cfg->lower_margin; /* VTLN */
		tmp |= lcd_cfg->yres << 16; /* VDLN */
		lcdc_write_chan(ch, LDVLNR, tmp);

		tmp = lcd_cfg->yres;
		tmp += lcd_cfg->lower_margin; /* VSYNP */
		tmp |= lcd_cfg->vsync_len << 16; /* VSYNW */
		lcdc_write_chan(ch, LDVSYNR, tmp);

		board_cfg = &ch->cfg.board_cfg;
		if (board_cfg->setup_sys)
			ret = board_cfg->setup_sys(board_cfg->board_data, ch,
						   &sh_mobile_lcdc_sys_bus_ops);
		if (ret)
			return ret;
	}

	/* word and long word swap */
	lcdc_write(priv, _LDDDSR, lcdc_read(priv, _LDDDSR) | 6);

	for (k = 0; k < ARRAY_SIZE(priv->ch); k++) {
		ch = &priv->ch[k];

		if (!priv->ch[k].enabled)
			continue;

		/* set bpp format in PKF[4:0] */
		tmp = lcdc_read_chan(ch, LDDFR);
		tmp &= ~(0x0001001f);
		tmp |= (priv->ch[k].info.var.bits_per_pixel == 16) ? 3 : 0;
		lcdc_write_chan(ch, LDDFR, tmp);

		/* point out our frame buffer */
		lcdc_write_chan(ch, LDSA1R, ch->info.fix.smem_start);

		/* set line size */
		lcdc_write_chan(ch, LDMLSR, ch->info.fix.line_length);

		/* setup deferred io if SYS bus */
		tmp = ch->cfg.sys_bus_cfg.deferred_io_msec;
		if (ch->ldmt1r_value & (1 << 12) && tmp) {
			ch->defio.deferred_io = sh_mobile_lcdc_deferred_io;
			ch->defio.delay = msecs_to_jiffies(tmp);
			ch->info.fbdefio = &ch->defio;
			fb_deferred_io_init(&ch->info);

			/* one-shot mode */
			lcdc_write_chan(ch, LDSM1R, 1);

			/* enable "Frame End Interrupt Enable" bit */
			lcdc_write(priv, _LDINTR, LDINTR_FE);

		} else {
			/* continuous read mode */
			lcdc_write_chan(ch, LDSM1R, 0);
		}
	}

	/* display output */
	lcdc_write(priv, _LDCNT1R, LCDC_ENABLE);

	/* start the lcdc */
	sh_mobile_lcdc_start_stop(priv, 1);
	priv->started = 1;

	/* tell the board code to enable the panel */
	for (k = 0; k < ARRAY_SIZE(priv->ch); k++) {
		ch = &priv->ch[k];
		board_cfg = &ch->cfg.board_cfg;
		if (board_cfg->display_on)
			board_cfg->display_on(board_cfg->board_data);
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

		/* deferred io mode:
		 * flush frame, and wait for frame end interrupt
		 * clean up deferred io and enable clock
		 */
		if (ch->info.fbdefio) {
			ch->frame_end = 0;
			schedule_delayed_work(&ch->info.deferred_work, 0);
			wait_event(ch->frame_end_wait, ch->frame_end);
			fb_deferred_io_cleanup(&ch->info);
			ch->info.fbdefio = NULL;
			sh_mobile_lcdc_clk_on(priv);
		}

		board_cfg = &ch->cfg.board_cfg;
		if (board_cfg->display_off)
			board_cfg->display_off(board_cfg->board_data);

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
	int ifm, miftyp;

	switch (ch->cfg.interface_type) {
	case RGB8: ifm = 0; miftyp = 0; break;
	case RGB9: ifm = 0; miftyp = 4; break;
	case RGB12A: ifm = 0; miftyp = 5; break;
	case RGB12B: ifm = 0; miftyp = 6; break;
	case RGB16: ifm = 0; miftyp = 7; break;
	case RGB18: ifm = 0; miftyp = 10; break;
	case RGB24: ifm = 0; miftyp = 11; break;
	case SYS8A: ifm = 1; miftyp = 0; break;
	case SYS8B: ifm = 1; miftyp = 1; break;
	case SYS8C: ifm = 1; miftyp = 2; break;
	case SYS8D: ifm = 1; miftyp = 3; break;
	case SYS9: ifm = 1; miftyp = 4; break;
	case SYS12: ifm = 1; miftyp = 5; break;
	case SYS16A: ifm = 1; miftyp = 7; break;
	case SYS16B: ifm = 1; miftyp = 8; break;
	case SYS16C: ifm = 1; miftyp = 9; break;
	case SYS18: ifm = 1; miftyp = 10; break;
	case SYS24: ifm = 1; miftyp = 11; break;
	default: goto bad;
	}

	/* SUBLCD only supports SYS interface */
	if (lcdc_chan_is_sublcd(ch)) {
		if (ifm == 0)
			goto bad;
		else
			ifm = 0;
	}

	ch->ldmt1r_value = (ifm << 12) | miftyp;
	return 0;
 bad:
	return -EINVAL;
}

static int sh_mobile_lcdc_setup_clocks(struct platform_device *pdev,
				       int clock_source,
				       struct sh_mobile_lcdc_priv *priv)
{
#ifdef CONFIG_HAVE_CLK
	char clk_name[8];
#endif
	char *str;
	int icksel;

	switch (clock_source) {
	case LCDC_CLK_BUS: str = "bus_clk"; icksel = 0; break;
	case LCDC_CLK_PERIPHERAL: str = "peripheral_clk"; icksel = 1; break;
	case LCDC_CLK_EXTERNAL: str = NULL; icksel = 2; break;
	default:
		return -EINVAL;
	}

	priv->lddckr = icksel << 16;

#ifdef CONFIG_HAVE_CLK
	atomic_set(&priv->clk_usecnt, -1);
	snprintf(clk_name, sizeof(clk_name), "lcdc%d", pdev->id);
	priv->clk = clk_get(&pdev->dev, clk_name);
	if (IS_ERR(priv->clk)) {
		dev_err(&pdev->dev, "cannot get clock \"%s\"\n", clk_name);
		return PTR_ERR(priv->clk);
	}
	
	if (str) {
		priv->dot_clk = clk_get(&pdev->dev, str);
		if (IS_ERR(priv->dot_clk)) {
			dev_err(&pdev->dev, "cannot get dot clock %s\n", str);
			clk_put(priv->clk);
			return PTR_ERR(priv->dot_clk);
		}
	}
#endif

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

static struct fb_ops sh_mobile_lcdc_ops = {
	.fb_setcolreg	= sh_mobile_lcdc_setcolreg,
	.fb_read        = fb_sys_read,
	.fb_write       = fb_sys_write,
	.fb_fillrect	= sh_mobile_lcdc_fillrect,
	.fb_copyarea	= sh_mobile_lcdc_copyarea,
	.fb_imageblit	= sh_mobile_lcdc_imageblit,
};

static int sh_mobile_lcdc_set_bpp(struct fb_var_screeninfo *var, int bpp)
{
	switch (bpp) {
	case 16: /* PKF[4:0] = 00011 - RGB 565 */
		var->red.offset = 11;
		var->red.length = 5;
		var->green.offset = 5;
		var->green.length = 6;
		var->blue.offset = 0;
		var->blue.length = 5;
		var->transp.offset = 0;
		var->transp.length = 0;
		break;

	case 32: /* PKF[4:0] = 00000 - RGB 888
		  * sh7722 pdf says 00RRGGBB but reality is GGBB00RR
		  * this may be because LDDDSR has word swap enabled..
		  */
		var->red.offset = 0;
		var->red.length = 8;
		var->green.offset = 24;
		var->green.length = 8;
		var->blue.offset = 16;
		var->blue.length = 8;
		var->transp.offset = 0;
		var->transp.length = 0;
		break;
	default:
		return -EINVAL;
	}
	var->bits_per_pixel = bpp;
	var->red.msb_right = 0;
	var->green.msb_right = 0;
	var->blue.msb_right = 0;
	var->transp.msb_right = 0;
	return 0;
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

static struct dev_pm_ops sh_mobile_lcdc_dev_pm_ops = {
	.suspend = sh_mobile_lcdc_suspend,
	.resume = sh_mobile_lcdc_resume,
};

static int sh_mobile_lcdc_remove(struct platform_device *pdev);

static int __init sh_mobile_lcdc_probe(struct platform_device *pdev)
{
	struct fb_info *info;
	struct sh_mobile_lcdc_priv *priv;
	struct sh_mobile_lcdc_info *pdata;
	struct sh_mobile_lcdc_chan_cfg *cfg;
	struct resource *res;
	int error;
	void *buf;
	int i, j;

	if (!pdev->dev.platform_data) {
		dev_err(&pdev->dev, "no platform data defined\n");
		error = -EINVAL;
		goto err0;
	}

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	i = platform_get_irq(pdev, 0);
	if (!res || i < 0) {
		dev_err(&pdev->dev, "cannot get platform resources\n");
		error = -ENOENT;
		goto err0;
	}

	priv = kzalloc(sizeof(*priv), GFP_KERNEL);
	if (!priv) {
		dev_err(&pdev->dev, "cannot allocate device data\n");
		error = -ENOMEM;
		goto err0;
	}

	error = request_irq(i, sh_mobile_lcdc_irq, IRQF_DISABLED,
			    dev_name(&pdev->dev), priv);
	if (error) {
		dev_err(&pdev->dev, "unable to request irq\n");
		goto err1;
	}

	priv->irq = i;
	platform_set_drvdata(pdev, priv);
	pdata = pdev->dev.platform_data;

	j = 0;
	for (i = 0; i < ARRAY_SIZE(pdata->ch); i++) {
		priv->ch[j].lcdc = priv;
		memcpy(&priv->ch[j].cfg, &pdata->ch[i], sizeof(pdata->ch[i]));

		error = sh_mobile_lcdc_check_interface(&priv->ch[i]);
		if (error) {
			dev_err(&pdev->dev, "unsupported interface type\n");
			goto err1;
		}
		init_waitqueue_head(&priv->ch[i].frame_end_wait);

		switch (pdata->ch[i].chan) {
		case LCDC_CHAN_MAINLCD:
			priv->ch[j].enabled = 1 << 1;
			priv->ch[j].reg_offs = lcdc_offs_mainlcd;
			j++;
			break;
		case LCDC_CHAN_SUBLCD:
			priv->ch[j].enabled = 1 << 2;
			priv->ch[j].reg_offs = lcdc_offs_sublcd;
			j++;
			break;
		}
	}

	if (!j) {
		dev_err(&pdev->dev, "no channels defined\n");
		error = -EINVAL;
		goto err1;
	}

	error = sh_mobile_lcdc_setup_clocks(pdev, pdata->clock_source, priv);
	if (error) {
		dev_err(&pdev->dev, "unable to setup clocks\n");
		goto err1;
	}

	priv->base = ioremap_nocache(res->start, (res->end - res->start) + 1);

	for (i = 0; i < j; i++) {
		info = &priv->ch[i].info;
		cfg = &priv->ch[i].cfg;

		info->fbops = &sh_mobile_lcdc_ops;
		info->var.xres = info->var.xres_virtual = cfg->lcd_cfg.xres;
		info->var.yres = info->var.yres_virtual = cfg->lcd_cfg.yres;
		info->var.width = cfg->lcd_size_cfg.width;
		info->var.height = cfg->lcd_size_cfg.height;
		info->var.activate = FB_ACTIVATE_NOW;
		error = sh_mobile_lcdc_set_bpp(&info->var, cfg->bpp);
		if (error)
			break;

		info->fix = sh_mobile_lcdc_fix;
		info->fix.line_length = cfg->lcd_cfg.xres * (cfg->bpp / 8);
		info->fix.smem_len = info->fix.line_length * cfg->lcd_cfg.yres;

		buf = dma_alloc_coherent(&pdev->dev, info->fix.smem_len,
					 &priv->ch[i].dma_handle, GFP_KERNEL);
		if (!buf) {
			dev_err(&pdev->dev, "unable to allocate buffer\n");
			error = -ENOMEM;
			break;
		}

		info->pseudo_palette = &priv->ch[i].pseudo_palette;
		info->flags = FBINFO_FLAG_DEFAULT;

		error = fb_alloc_cmap(&info->cmap, PALETTE_NR, 0);
		if (error < 0) {
			dev_err(&pdev->dev, "unable to allocate cmap\n");
			dma_free_coherent(&pdev->dev, info->fix.smem_len,
					  buf, priv->ch[i].dma_handle);
			break;
		}

		memset(buf, 0, info->fix.smem_len);
		info->fix.smem_start = priv->ch[i].dma_handle;
		info->screen_base = buf;
		info->device = &pdev->dev;
		info->par = &priv->ch[i];
	}

	if (error)
		goto err1;

	error = sh_mobile_lcdc_start(priv);
	if (error) {
		dev_err(&pdev->dev, "unable to start hardware\n");
		goto err1;
	}

	for (i = 0; i < j; i++) {
		struct sh_mobile_lcdc_chan *ch = priv->ch + i;

		info = &ch->info;

		if (info->fbdefio) {
			priv->ch->sglist = vmalloc(sizeof(struct scatterlist) *
					info->fix.smem_len >> PAGE_SHIFT);
			if (!priv->ch->sglist) {
				dev_err(&pdev->dev, "cannot allocate sglist\n");
				goto err1;
			}
		}

		error = register_framebuffer(info);
		if (error < 0)
			goto err1;

		dev_info(info->dev,
			 "registered %s/%s as %dx%d %dbpp.\n",
			 pdev->name,
			 (ch->cfg.chan == LCDC_CHAN_MAINLCD) ?
			 "mainlcd" : "sublcd",
			 (int) ch->cfg.lcd_cfg.xres,
			 (int) ch->cfg.lcd_cfg.yres,
			 ch->cfg.bpp);

		/* deferred io mode: disable clock to save power */
		if (info->fbdefio)
			sh_mobile_lcdc_clk_off(priv);
	}

	return 0;
 err1:
	sh_mobile_lcdc_remove(pdev);
 err0:
	return error;
}

static int sh_mobile_lcdc_remove(struct platform_device *pdev)
{
	struct sh_mobile_lcdc_priv *priv = platform_get_drvdata(pdev);
	struct fb_info *info;
	int i;

	for (i = 0; i < ARRAY_SIZE(priv->ch); i++)
		if (priv->ch[i].info.dev)
			unregister_framebuffer(&priv->ch[i].info);

	sh_mobile_lcdc_stop(priv);

	for (i = 0; i < ARRAY_SIZE(priv->ch); i++) {
		info = &priv->ch[i].info;

		if (!info->device)
			continue;

		if (priv->ch[i].sglist)
			vfree(priv->ch[i].sglist);

		dma_free_coherent(&pdev->dev, info->fix.smem_len,
				  info->screen_base, priv->ch[i].dma_handle);
		fb_dealloc_cmap(&info->cmap);
	}

#ifdef CONFIG_HAVE_CLK
	if (priv->dot_clk)
		clk_put(priv->dot_clk);
	clk_put(priv->clk);
#endif

	if (priv->base)
		iounmap(priv->base);

	if (priv->irq)
		free_irq(priv->irq, priv);
	kfree(priv);
	return 0;
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
