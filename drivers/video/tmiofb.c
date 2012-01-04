/*
 * Frame Buffer Device for Toshiba Mobile IO(TMIO) controller
 *
 * Copyright(C) 2005-2006 Chris Humbert
 * Copyright(C) 2005 Dirk Opfer
 * Copytight(C) 2007,2008 Dmitry Baryshkov
 *
 * Based on:
 *	drivers/video/w100fb.c
 *	code written by Sharp/Lineo for 2.4 kernels
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2
 * as published by the Free Software Foundation;
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/fb.h>
#include <linux/interrupt.h>
#include <linux/delay.h>
/* Why should fb driver call console functions? because console_lock() */
#include <linux/console.h>
#include <linux/mfd/core.h>
#include <linux/mfd/tmio.h>
#include <linux/uaccess.h>

/*
 * accelerator commands
 */
#define TMIOFB_ACC_CSADR(x)	(0x00000000 | ((x) & 0x001ffffe))
#define TMIOFB_ACC_CHPIX(x)	(0x01000000 | ((x) & 0x000003ff))
#define TMIOFB_ACC_CVPIX(x)	(0x02000000 | ((x) & 0x000003ff))
#define TMIOFB_ACC_PSADR(x)	(0x03000000 | ((x) & 0x00fffffe))
#define TMIOFB_ACC_PHPIX(x)	(0x04000000 | ((x) & 0x000003ff))
#define TMIOFB_ACC_PVPIX(x)	(0x05000000 | ((x) & 0x000003ff))
#define TMIOFB_ACC_PHOFS(x)	(0x06000000 | ((x) & 0x000003ff))
#define TMIOFB_ACC_PVOFS(x)	(0x07000000 | ((x) & 0x000003ff))
#define TMIOFB_ACC_POADR(x)	(0x08000000 | ((x) & 0x00fffffe))
#define TMIOFB_ACC_RSTR(x)	(0x09000000 | ((x) & 0x000000ff))
#define TMIOFB_ACC_TCLOR(x)	(0x0A000000 | ((x) & 0x0000ffff))
#define TMIOFB_ACC_FILL(x)	(0x0B000000 | ((x) & 0x0000ffff))
#define TMIOFB_ACC_DSADR(x)	(0x0C000000 | ((x) & 0x00fffffe))
#define TMIOFB_ACC_SSADR(x)	(0x0D000000 | ((x) & 0x00fffffe))
#define TMIOFB_ACC_DHPIX(x)	(0x0E000000 | ((x) & 0x000003ff))
#define TMIOFB_ACC_DVPIX(x)	(0x0F000000 | ((x) & 0x000003ff))
#define TMIOFB_ACC_SHPIX(x)	(0x10000000 | ((x) & 0x000003ff))
#define TMIOFB_ACC_SVPIX(x)	(0x11000000 | ((x) & 0x000003ff))
#define TMIOFB_ACC_LBINI(x)	(0x12000000 | ((x) & 0x0000ffff))
#define TMIOFB_ACC_LBK2(x)	(0x13000000 | ((x) & 0x0000ffff))
#define TMIOFB_ACC_SHBINI(x)	(0x14000000 | ((x) & 0x0000ffff))
#define TMIOFB_ACC_SHBK2(x)	(0x15000000 | ((x) & 0x0000ffff))
#define TMIOFB_ACC_SVBINI(x)	(0x16000000 | ((x) & 0x0000ffff))
#define TMIOFB_ACC_SVBK2(x)	(0x17000000 | ((x) & 0x0000ffff))

#define TMIOFB_ACC_CMGO		0x20000000
#define TMIOFB_ACC_CMGO_CEND	0x00000001
#define TMIOFB_ACC_CMGO_INT	0x00000002
#define TMIOFB_ACC_CMGO_CMOD	0x00000010
#define TMIOFB_ACC_CMGO_CDVRV	0x00000020
#define TMIOFB_ACC_CMGO_CDHRV	0x00000040
#define TMIOFB_ACC_CMGO_RUND	0x00008000
#define TMIOFB_ACC_SCGO		0x21000000
#define TMIOFB_ACC_SCGO_CEND	0x00000001
#define TMIOFB_ACC_SCGO_INT	0x00000002
#define TMIOFB_ACC_SCGO_ROP3	0x00000004
#define TMIOFB_ACC_SCGO_TRNS	0x00000008
#define TMIOFB_ACC_SCGO_DVRV	0x00000010
#define TMIOFB_ACC_SCGO_DHRV	0x00000020
#define TMIOFB_ACC_SCGO_SVRV	0x00000040
#define TMIOFB_ACC_SCGO_SHRV	0x00000080
#define TMIOFB_ACC_SCGO_DSTXY	0x00008000
#define TMIOFB_ACC_SBGO		0x22000000
#define TMIOFB_ACC_SBGO_CEND	0x00000001
#define TMIOFB_ACC_SBGO_INT	0x00000002
#define TMIOFB_ACC_SBGO_DVRV	0x00000010
#define TMIOFB_ACC_SBGO_DHRV	0x00000020
#define TMIOFB_ACC_SBGO_SVRV	0x00000040
#define TMIOFB_ACC_SBGO_SHRV	0x00000080
#define TMIOFB_ACC_SBGO_SBMD	0x00000100
#define TMIOFB_ACC_FLGO		0x23000000
#define TMIOFB_ACC_FLGO_CEND	0x00000001
#define TMIOFB_ACC_FLGO_INT	0x00000002
#define TMIOFB_ACC_FLGO_ROP3	0x00000004
#define TMIOFB_ACC_LDGO		0x24000000
#define TMIOFB_ACC_LDGO_CEND	0x00000001
#define TMIOFB_ACC_LDGO_INT	0x00000002
#define TMIOFB_ACC_LDGO_ROP3	0x00000004
#define TMIOFB_ACC_LDGO_ENDPX	0x00000008
#define TMIOFB_ACC_LDGO_LVRV	0x00000010
#define TMIOFB_ACC_LDGO_LHRV	0x00000020
#define TMIOFB_ACC_LDGO_LDMOD	0x00000040

/* a FIFO is always allocated, even if acceleration is not used */
#define TMIOFB_FIFO_SIZE	512

/*
 * LCD Host Controller Configuration Register
 *
 * This iomem area supports only 16-bit IO.
 */
#define CCR_CMD			0x04 /* Command				*/
#define CCR_REVID		0x08 /* Revision ID			*/
#define CCR_BASEL		0x10 /* LCD Control Reg Base Addr Low	*/
#define CCR_BASEH		0x12 /* LCD Control Reg Base Addr High	*/
#define CCR_UGCC		0x40 /* Unified Gated Clock Control	*/
#define CCR_GCC			0x42 /* Gated Clock Control		*/
#define CCR_USC			0x50 /* Unified Software Clear		*/
#define CCR_VRAMRTC		0x60 /* VRAM Timing Control		*/
				/* 0x61 VRAM Refresh Control		*/
#define CCR_VRAMSAC		0x62 /* VRAM Access Control		*/
				/* 0x63	VRAM Status			*/
#define CCR_VRAMBC		0x64 /* VRAM Block Control		*/

/*
 * LCD Control Register
 *
 * This iomem area supports only 16-bit IO.
 */
#define LCR_UIS			0x000 /* Unified Interrupt Status	*/
#define LCR_VHPN		0x008 /* VRAM Horizontal Pixel Number	*/
#define LCR_CFSAL		0x00a /* Command FIFO Start Address Low	*/
#define LCR_CFSAH		0x00c /* Command FIFO Start Address High */
#define LCR_CFS			0x00e /* Command FIFO Size		*/
#define LCR_CFWS		0x010 /* Command FIFO Writeable Size	*/
#define LCR_BBIE		0x012 /* BitBLT Interrupt Enable	*/
#define LCR_BBISC		0x014 /* BitBLT Interrupt Status and Clear */
#define LCR_CCS			0x016 /* Command Count Status		*/
#define LCR_BBES		0x018 /* BitBLT Execution Status	*/
#define LCR_CMDL		0x01c /* Command Low			*/
#define LCR_CMDH		0x01e /* Command High			*/
#define LCR_CFC			0x022 /* Command FIFO Clear		*/
#define LCR_CCIFC		0x024 /* CMOS Camera IF Control		*/
#define LCR_HWT			0x026 /* Hardware Test			*/
#define LCR_LCDCCRC		0x100 /* LCDC Clock and Reset Control	*/
#define LCR_LCDCC		0x102 /* LCDC Control			*/
#define LCR_LCDCOPC		0x104 /* LCDC Output Pin Control	*/
#define LCR_LCDIS		0x108 /* LCD Interrupt Status		*/
#define LCR_LCDIM		0x10a /* LCD Interrupt Mask		*/
#define LCR_LCDIE		0x10c /* LCD Interrupt Enable		*/
#define LCR_GDSAL		0x122 /* Graphics Display Start Address Low */
#define LCR_GDSAH		0x124 /* Graphics Display Start Address High */
#define LCR_VHPCL		0x12a /* VRAM Horizontal Pixel Count Low */
#define LCR_VHPCH		0x12c /* VRAM Horizontal Pixel Count High */
#define LCR_GM			0x12e /* Graphic Mode(VRAM access enable) */
#define LCR_HT			0x140 /* Horizontal Total		*/
#define LCR_HDS			0x142 /* Horizontal Display Start	*/
#define LCR_HSS			0x144 /* H-Sync Start			*/
#define LCR_HSE			0x146 /* H-Sync End			*/
#define LCR_HNP			0x14c /* Horizontal Number of Pixels	*/
#define LCR_VT			0x150 /* Vertical Total			*/
#define LCR_VDS			0x152 /* Vertical Display Start		*/
#define LCR_VSS			0x154 /* V-Sync Start			*/
#define LCR_VSE			0x156 /* V-Sync End			*/
#define LCR_CDLN		0x160 /* Current Display Line Number	*/
#define LCR_ILN			0x162 /* Interrupt Line Number		*/
#define LCR_SP			0x164 /* Sync Polarity			*/
#define LCR_MISC		0x166 /* MISC(RGB565 mode)		*/
#define LCR_VIHSS		0x16a /* Video Interface H-Sync Start	*/
#define LCR_VIVS		0x16c /* Video Interface Vertical Start	*/
#define LCR_VIVE		0x16e /* Video Interface Vertical End	*/
#define LCR_VIVSS		0x170 /* Video Interface V-Sync Start	*/
#define LCR_VCCIS		0x17e /* Video / CMOS Camera Interface Select */
#define LCR_VIDWSAL		0x180 /* VI Data Write Start Address Low */
#define LCR_VIDWSAH		0x182 /* VI Data Write Start Address High */
#define LCR_VIDRSAL		0x184 /* VI Data Read Start Address Low	*/
#define LCR_VIDRSAH		0x186 /* VI Data Read Start Address High */
#define LCR_VIPDDST		0x188 /* VI Picture Data Display Start Timing */
#define LCR_VIPDDET		0x186 /* VI Picture Data Display End Timing */
#define LCR_VIE			0x18c /* Video Interface Enable		*/
#define LCR_VCS			0x18e /* Video/Camera Select		*/
#define LCR_VPHWC		0x194 /* Video Picture Horizontal Wait Count */
#define LCR_VPHS		0x196 /* Video Picture Horizontal Size	*/
#define LCR_VPVWC		0x198 /* Video Picture Vertical Wait Count */
#define LCR_VPVS		0x19a /* Video Picture Vertical Size	*/
#define LCR_PLHPIX		0x1a0 /* PLHPIX				*/
#define LCR_XS			0x1a2 /* XStart				*/
#define LCR_XCKHW		0x1a4 /* XCK High Width			*/
#define LCR_STHS		0x1a8 /* STH Start			*/
#define LCR_VT2			0x1aa /* Vertical Total			*/
#define LCR_YCKSW		0x1ac /* YCK Start Wait			*/
#define LCR_YSTS		0x1ae /* YST Start			*/
#define LCR_PPOLS		0x1b0 /* #PPOL Start			*/
#define LCR_PRECW		0x1b2 /* PREC Width			*/
#define LCR_VCLKHW		0x1b4 /* VCLK High Width		*/
#define LCR_OC			0x1b6 /* Output Control			*/

static char *mode_option __devinitdata;

struct tmiofb_par {
	u32				pseudo_palette[16];

#ifdef CONFIG_FB_TMIO_ACCELL
	wait_queue_head_t		wait_acc;
	bool				use_polling;
#endif

	void __iomem			*ccr;
	void __iomem			*lcr;
};

/*--------------------------------------------------------------------------*/

/*
 * reasons for an interrupt:
 *	uis	bbisc	lcdis
 *	0100	0001	accelerator command completed
 * 	2000	0001	vsync start
 * 	2000	0002	display start
 * 	2000	0004	line number match(0x1ff mask???)
 */
static irqreturn_t tmiofb_irq(int irq, void *__info)
{
	struct fb_info *info = __info;
	struct tmiofb_par *par = info->par;
	unsigned int bbisc = tmio_ioread16(par->lcr + LCR_BBISC);


	tmio_iowrite16(bbisc, par->lcr + LCR_BBISC);

#ifdef CONFIG_FB_TMIO_ACCELL
	/*
	 * We were in polling mode and now we got correct irq.
	 * Switch back to IRQ-based sync of command FIFO
	 */
	if (unlikely(par->use_polling && irq != -1)) {
		printk(KERN_INFO "tmiofb: switching to waitq\n");
		par->use_polling = false;
	}

	if (bbisc & 1)
		wake_up(&par->wait_acc);
#endif

	return IRQ_HANDLED;
}


/*--------------------------------------------------------------------------*/


/*
 * Turns off the LCD controller and LCD host controller.
 */
static int tmiofb_hw_stop(struct platform_device *dev)
{
	struct tmio_fb_data *data = dev->dev.platform_data;
	struct fb_info *info = platform_get_drvdata(dev);
	struct tmiofb_par *par = info->par;

	tmio_iowrite16(0, par->ccr + CCR_UGCC);
	tmio_iowrite16(0, par->lcr + LCR_GM);
	data->lcd_set_power(dev, 0);
	tmio_iowrite16(0x0010, par->lcr + LCR_LCDCCRC);

	return 0;
}

/*
 * Initializes the LCD host controller.
 */
static int tmiofb_hw_init(struct platform_device *dev)
{
	const struct mfd_cell *cell = mfd_get_cell(dev);
	struct fb_info *info = platform_get_drvdata(dev);
	struct tmiofb_par *par = info->par;
	const struct resource *nlcr = &cell->resources[0];
	const struct resource *vram = &cell->resources[2];
	unsigned long base;

	if (nlcr == NULL || vram == NULL)
		return -EINVAL;

	base = nlcr->start;

	tmio_iowrite16(0x003a, par->ccr + CCR_UGCC);
	tmio_iowrite16(0x003a, par->ccr + CCR_GCC);
	tmio_iowrite16(0x3f00, par->ccr + CCR_USC);

	msleep(2); /* wait for device to settle */

	tmio_iowrite16(0x0000, par->ccr + CCR_USC);
	tmio_iowrite16(base >> 16, par->ccr + CCR_BASEH);
	tmio_iowrite16(base, par->ccr + CCR_BASEL);
	tmio_iowrite16(0x0002, par->ccr + CCR_CMD); /* base address enable */
	tmio_iowrite16(0x40a8, par->ccr + CCR_VRAMRTC); /* VRAMRC, VRAMTC */
	tmio_iowrite16(0x0018, par->ccr + CCR_VRAMSAC); /* VRAMSTS, VRAMAC */
	tmio_iowrite16(0x0002, par->ccr + CCR_VRAMBC);
	msleep(2); /* wait for device to settle */
	tmio_iowrite16(0x000b, par->ccr + CCR_VRAMBC);

	base = vram->start + info->screen_size;
	tmio_iowrite16(base >> 16, par->lcr + LCR_CFSAH);
	tmio_iowrite16(base, par->lcr + LCR_CFSAL);
	tmio_iowrite16(TMIOFB_FIFO_SIZE - 1, par->lcr + LCR_CFS);
	tmio_iowrite16(1, par->lcr + LCR_CFC);
	tmio_iowrite16(1, par->lcr + LCR_BBIE);
	tmio_iowrite16(0, par->lcr + LCR_CFWS);

	return 0;
}

/*
 * Sets the LCD controller's output resolution and pixel clock
 */
static void tmiofb_hw_mode(struct platform_device *dev)
{
	struct tmio_fb_data *data = dev->dev.platform_data;
	struct fb_info *info = platform_get_drvdata(dev);
	struct fb_videomode *mode = info->mode;
	struct tmiofb_par *par = info->par;
	unsigned int i;

	tmio_iowrite16(0, par->lcr + LCR_GM);
	data->lcd_set_power(dev, 0);
	tmio_iowrite16(0x0010, par->lcr + LCR_LCDCCRC);
	data->lcd_mode(dev, mode);
	data->lcd_set_power(dev, 1);

	tmio_iowrite16(info->fix.line_length, par->lcr + LCR_VHPN);
	tmio_iowrite16(0, par->lcr + LCR_GDSAH);
	tmio_iowrite16(0, par->lcr + LCR_GDSAL);
	tmio_iowrite16(info->fix.line_length >> 16, par->lcr + LCR_VHPCH);
	tmio_iowrite16(info->fix.line_length, par->lcr + LCR_VHPCL);
	tmio_iowrite16(i = 0, par->lcr + LCR_HSS);
	tmio_iowrite16(i += mode->hsync_len, par->lcr + LCR_HSE);
	tmio_iowrite16(i += mode->left_margin, par->lcr + LCR_HDS);
	tmio_iowrite16(i += mode->xres + mode->right_margin, par->lcr + LCR_HT);
	tmio_iowrite16(mode->xres, par->lcr + LCR_HNP);
	tmio_iowrite16(i = 0, par->lcr + LCR_VSS);
	tmio_iowrite16(i += mode->vsync_len, par->lcr + LCR_VSE);
	tmio_iowrite16(i += mode->upper_margin, par->lcr + LCR_VDS);
	tmio_iowrite16(i += mode->yres, par->lcr + LCR_ILN);
	tmio_iowrite16(i += mode->lower_margin, par->lcr + LCR_VT);
	tmio_iowrite16(3, par->lcr + LCR_MISC); /* RGB565 mode */
	tmio_iowrite16(1, par->lcr + LCR_GM); /* VRAM enable */
	tmio_iowrite16(0x4007, par->lcr + LCR_LCDCC);
	tmio_iowrite16(3, par->lcr + LCR_SP);  /* sync polarity */

	tmio_iowrite16(0x0010, par->lcr + LCR_LCDCCRC);
	msleep(5); /* wait for device to settle */
	tmio_iowrite16(0x0014, par->lcr + LCR_LCDCCRC); /* STOP_CKP */
	msleep(5); /* wait for device to settle */
	tmio_iowrite16(0x0015, par->lcr + LCR_LCDCCRC); /* STOP_CKP|SOFT_RESET*/
	tmio_iowrite16(0xfffa, par->lcr + LCR_VCS);
}

/*--------------------------------------------------------------------------*/

#ifdef CONFIG_FB_TMIO_ACCELL
static int __must_check
tmiofb_acc_wait(struct fb_info *info, unsigned int ccs)
{
	struct tmiofb_par *par = info->par;
	/*
	 * This code can be called with interrupts disabled.
	 * So instead of relaying on irq to trigger the event,
	 * poll the state till the necessary command is executed.
	 */
	if (irqs_disabled() || par->use_polling) {
		int i = 0;
		while (tmio_ioread16(par->lcr + LCR_CCS) > ccs) {
			udelay(1);
			i++;
			if (i > 10000) {
				pr_err("tmiofb: timeout waiting for %d\n",
						ccs);
				return -ETIMEDOUT;
			}
			tmiofb_irq(-1, info);
		}
	} else {
		if (!wait_event_interruptible_timeout(par->wait_acc,
				tmio_ioread16(par->lcr + LCR_CCS) <= ccs,
				1000)) {
			pr_err("tmiofb: timeout waiting for %d\n", ccs);
			return -ETIMEDOUT;
		}
	}

	return 0;
}

/*
 * Writes an accelerator command to the accelerator's FIFO.
 */
static int
tmiofb_acc_write(struct fb_info *info, const u32 *cmd, unsigned int count)
{
	struct tmiofb_par *par = info->par;
	int ret;

	ret = tmiofb_acc_wait(info, TMIOFB_FIFO_SIZE - count);
	if (ret)
		return ret;

	for (; count; count--, cmd++) {
		tmio_iowrite16(*cmd >> 16, par->lcr + LCR_CMDH);
		tmio_iowrite16(*cmd, par->lcr + LCR_CMDL);
	}

	return ret;
}

/*
 * Wait for the accelerator to finish its operations before writing
 * to the framebuffer for consistent display output.
 */
static int tmiofb_sync(struct fb_info *fbi)
{
	struct tmiofb_par *par = fbi->par;

	int ret;
	int i = 0;

	ret = tmiofb_acc_wait(fbi, 0);

	while (tmio_ioread16(par->lcr + LCR_BBES) & 2) { /* blit active */
		udelay(1);
		i++ ;
		if (i > 10000) {
			printk(KERN_ERR "timeout waiting for blit to end!\n");
			return -ETIMEDOUT;
		}
	}

	return ret;
}

static void
tmiofb_fillrect(struct fb_info *fbi, const struct fb_fillrect *rect)
{
	const u32 cmd[] = {
		TMIOFB_ACC_DSADR((rect->dy * fbi->mode->xres + rect->dx) * 2),
		TMIOFB_ACC_DHPIX(rect->width - 1),
		TMIOFB_ACC_DVPIX(rect->height - 1),
		TMIOFB_ACC_FILL(rect->color),
		TMIOFB_ACC_FLGO,
	};

	if (fbi->state != FBINFO_STATE_RUNNING ||
	    fbi->flags & FBINFO_HWACCEL_DISABLED) {
		cfb_fillrect(fbi, rect);
		return;
	}

	tmiofb_acc_write(fbi, cmd, ARRAY_SIZE(cmd));
}

static void
tmiofb_copyarea(struct fb_info *fbi, const struct fb_copyarea *area)
{
	const u32 cmd[] = {
		TMIOFB_ACC_DSADR((area->dy * fbi->mode->xres + area->dx) * 2),
		TMIOFB_ACC_DHPIX(area->width - 1),
		TMIOFB_ACC_DVPIX(area->height - 1),
		TMIOFB_ACC_SSADR((area->sy * fbi->mode->xres + area->sx) * 2),
		TMIOFB_ACC_SCGO,
	};

	if (fbi->state != FBINFO_STATE_RUNNING ||
	    fbi->flags & FBINFO_HWACCEL_DISABLED) {
		cfb_copyarea(fbi, area);
		return;
	}

	tmiofb_acc_write(fbi, cmd, ARRAY_SIZE(cmd));
}
#endif

static void tmiofb_clearscreen(struct fb_info *info)
{
	const struct fb_fillrect rect = {
		.dx	= 0,
		.dy	= 0,
		.width	= info->mode->xres,
		.height	= info->mode->yres,
		.color	= 0,
		.rop	= ROP_COPY,
	};

	info->fbops->fb_fillrect(info, &rect);
}

static int tmiofb_vblank(struct fb_info *fbi, struct fb_vblank *vblank)
{
	struct tmiofb_par *par = fbi->par;
	struct fb_videomode *mode = fbi->mode;
	unsigned int vcount = tmio_ioread16(par->lcr + LCR_CDLN);
	unsigned int vds = mode->vsync_len + mode->upper_margin;

	vblank->vcount = vcount;
	vblank->flags = FB_VBLANK_HAVE_VBLANK | FB_VBLANK_HAVE_VCOUNT
						| FB_VBLANK_HAVE_VSYNC;

	if (vcount < mode->vsync_len)
		vblank->flags |= FB_VBLANK_VSYNCING;

	if (vcount < vds || vcount > vds + mode->yres)
		vblank->flags |= FB_VBLANK_VBLANKING;

	return 0;
}


static int tmiofb_ioctl(struct fb_info *fbi,
		unsigned int cmd, unsigned long arg)
{
	switch (cmd) {
	case FBIOGET_VBLANK: {
		struct fb_vblank vblank = {0};
		void __user *argp = (void __user *) arg;

		tmiofb_vblank(fbi, &vblank);
		if (copy_to_user(argp, &vblank, sizeof vblank))
			return -EFAULT;
		return 0;
	}

#ifdef CONFIG_FB_TMIO_ACCELL
	case FBIO_TMIO_ACC_SYNC:
		tmiofb_sync(fbi);
		return 0;

	case FBIO_TMIO_ACC_WRITE: {
		u32 __user *argp = (void __user *) arg;
		u32 len;
		u32 acc[16];

		if (get_user(len, argp))
			return -EFAULT;
		if (len > ARRAY_SIZE(acc))
			return -EINVAL;
		if (copy_from_user(acc, argp + 1, sizeof(u32) * len))
			return -EFAULT;

		return tmiofb_acc_write(fbi, acc, len);
	}
#endif
	}

	return -ENOTTY;
}

/*--------------------------------------------------------------------------*/

/* Select the smallest mode that allows the desired resolution to be
 * displayed.  If desired, the x and y parameters can be rounded up to
 * match the selected mode.
 */
static struct fb_videomode *
tmiofb_find_mode(struct fb_info *info, struct fb_var_screeninfo *var)
{
	struct tmio_fb_data *data = info->device->platform_data;
	struct fb_videomode *best = NULL;
	int i;

	for (i = 0; i < data->num_modes; i++) {
		struct fb_videomode *mode = data->modes + i;

		if (mode->xres >= var->xres && mode->yres >= var->yres
				&& (!best || (mode->xres < best->xres
					   && mode->yres < best->yres)))
			best = mode;
	}

	return best;
}

static int tmiofb_check_var(struct fb_var_screeninfo *var, struct fb_info *info)
{

	struct fb_videomode *mode;
	struct tmio_fb_data *data = info->device->platform_data;

	mode = tmiofb_find_mode(info, var);
	if (!mode || var->bits_per_pixel > 16)
		return -EINVAL;

	fb_videomode_to_var(var, mode);

	var->xres_virtual = mode->xres;
	var->yres_virtual = info->screen_size / (mode->xres * 2);

	if (var->yres_virtual < var->yres)
		return -EINVAL;

	var->xoffset = 0;
	var->yoffset = 0;
	var->bits_per_pixel = 16;
	var->grayscale = 0;
	var->red.offset = 11;
	var->red.length = 5;
	var->green.offset = 5;
	var->green.length = 6;
	var->blue.offset = 0;
	var->blue.length = 5;
	var->transp.offset = 0;
	var->transp.length = 0;
	var->nonstd = 0;
	var->height = data->height; /* mm */
	var->width = data->width; /* mm */
	var->rotate = 0;
	return 0;
}

static int tmiofb_set_par(struct fb_info *info)
{
	struct fb_var_screeninfo *var = &info->var;
	struct fb_videomode *mode;

	mode = tmiofb_find_mode(info, var);
	if (!mode)
		return -EINVAL;

	info->mode = mode;
	info->fix.line_length = info->mode->xres *
			var->bits_per_pixel / 8;

	tmiofb_hw_mode(to_platform_device(info->device));
	tmiofb_clearscreen(info);
	return 0;
}

static int tmiofb_setcolreg(unsigned regno, unsigned red, unsigned green,
			   unsigned blue, unsigned transp,
			   struct fb_info *info)
{
	struct tmiofb_par *par = info->par;

	if (regno < ARRAY_SIZE(par->pseudo_palette)) {
		par->pseudo_palette[regno] =
			((red & 0xf800)) |
			((green & 0xfc00) >>  5) |
			((blue & 0xf800) >> 11);
		return 0;
	}

	return -EINVAL;
}

static int tmiofb_blank(int blank, struct fb_info *info)
{
	/*
	 * everything is done in lcd/bl drivers.
	 * this is purely to make sysfs happy and work.
	 */
	return 0;
}

static struct fb_ops tmiofb_ops = {
	.owner		= THIS_MODULE,

	.fb_ioctl	= tmiofb_ioctl,
	.fb_check_var	= tmiofb_check_var,
	.fb_set_par	= tmiofb_set_par,
	.fb_setcolreg	= tmiofb_setcolreg,
	.fb_blank	= tmiofb_blank,
	.fb_imageblit	= cfb_imageblit,
#ifdef CONFIG_FB_TMIO_ACCELL
	.fb_sync	= tmiofb_sync,
	.fb_fillrect	= tmiofb_fillrect,
	.fb_copyarea	= tmiofb_copyarea,
#else
	.fb_fillrect	= cfb_fillrect,
	.fb_copyarea	= cfb_copyarea,
#endif
};

/*--------------------------------------------------------------------------*/

static int __devinit tmiofb_probe(struct platform_device *dev)
{
	const struct mfd_cell *cell = mfd_get_cell(dev);
	struct tmio_fb_data *data = dev->dev.platform_data;
	struct resource *ccr = platform_get_resource(dev, IORESOURCE_MEM, 1);
	struct resource *lcr = platform_get_resource(dev, IORESOURCE_MEM, 0);
	struct resource *vram = platform_get_resource(dev, IORESOURCE_MEM, 2);
	int irq = platform_get_irq(dev, 0);
	struct fb_info *info;
	struct tmiofb_par *par;
	int retval;

	/*
	 * This is the only way ATM to disable the fb
	 */
	if (data == NULL) {
		dev_err(&dev->dev, "NULL platform data!\n");
		return -EINVAL;
	}

	info = framebuffer_alloc(sizeof(struct tmiofb_par), &dev->dev);

	if (!info)
		return -ENOMEM;

	par = info->par;

#ifdef CONFIG_FB_TMIO_ACCELL
	init_waitqueue_head(&par->wait_acc);

	par->use_polling = true;

	info->flags = FBINFO_DEFAULT | FBINFO_HWACCEL_COPYAREA
			| FBINFO_HWACCEL_FILLRECT;
#else
	info->flags = FBINFO_DEFAULT;
#endif

	info->fbops = &tmiofb_ops;

	strcpy(info->fix.id, "tmio-fb");
	info->fix.smem_start = vram->start;
	info->fix.smem_len = resource_size(vram);
	info->fix.type = FB_TYPE_PACKED_PIXELS;
	info->fix.visual = FB_VISUAL_TRUECOLOR;
	info->fix.mmio_start = lcr->start;
	info->fix.mmio_len = resource_size(lcr);
	info->fix.accel = FB_ACCEL_NONE;
	info->screen_size = info->fix.smem_len - (4 * TMIOFB_FIFO_SIZE);
	info->pseudo_palette = par->pseudo_palette;

	par->ccr = ioremap(ccr->start, resource_size(ccr));
	if (!par->ccr) {
		retval = -ENOMEM;
		goto err_ioremap_ccr;
	}

	par->lcr = ioremap(info->fix.mmio_start, info->fix.mmio_len);
	if (!par->lcr) {
		retval = -ENOMEM;
		goto err_ioremap_lcr;
	}

	info->screen_base = ioremap(info->fix.smem_start, info->fix.smem_len);
	if (!info->screen_base) {
		retval = -ENOMEM;
		goto err_ioremap_vram;
	}

	retval = request_irq(irq, &tmiofb_irq, 0,
					dev_name(&dev->dev), info);

	if (retval)
		goto err_request_irq;

	platform_set_drvdata(dev, info);

	retval = fb_find_mode(&info->var, info, mode_option,
			data->modes, data->num_modes,
			data->modes, 16);
	if (!retval) {
		retval = -EINVAL;
		goto err_find_mode;
	}

	if (cell->enable) {
		retval = cell->enable(dev);
		if (retval)
			goto err_enable;
	}

	retval = tmiofb_hw_init(dev);
	if (retval)
		goto err_hw_init;

	fb_videomode_to_modelist(data->modes, data->num_modes,
				 &info->modelist);

	retval = register_framebuffer(info);
	if (retval < 0)
		goto err_register_framebuffer;

	printk(KERN_INFO "fb%d: %s frame buffer device\n",
				info->node, info->fix.id);

	return 0;

err_register_framebuffer:
/*err_set_par:*/
	tmiofb_hw_stop(dev);
err_hw_init:
	if (cell->disable)
		cell->disable(dev);
err_enable:
err_find_mode:
	platform_set_drvdata(dev, NULL);
	free_irq(irq, info);
err_request_irq:
	iounmap(info->screen_base);
err_ioremap_vram:
	iounmap(par->lcr);
err_ioremap_lcr:
	iounmap(par->ccr);
err_ioremap_ccr:
	framebuffer_release(info);
	return retval;
}

static int __devexit tmiofb_remove(struct platform_device *dev)
{
	const struct mfd_cell *cell = mfd_get_cell(dev);
	struct fb_info *info = platform_get_drvdata(dev);
	int irq = platform_get_irq(dev, 0);
	struct tmiofb_par *par;

	if (info) {
		par = info->par;
		unregister_framebuffer(info);

		tmiofb_hw_stop(dev);

		if (cell->disable)
			cell->disable(dev);

		platform_set_drvdata(dev, NULL);

		free_irq(irq, info);

		iounmap(info->screen_base);
		iounmap(par->lcr);
		iounmap(par->ccr);

		framebuffer_release(info);
	}

	return 0;
}

#ifdef DEBUG
static void tmiofb_dump_regs(struct platform_device *dev)
{
	struct fb_info *info = platform_get_drvdata(dev);
	struct tmiofb_par *par = info->par;

	printk(KERN_DEBUG "lhccr:\n");
#define CCR_PR(n)	printk(KERN_DEBUG "\t" #n " = \t%04x\n",\
		tmio_ioread16(par->ccr + CCR_ ## n));
	CCR_PR(CMD);
	CCR_PR(REVID);
	CCR_PR(BASEL);
	CCR_PR(BASEH);
	CCR_PR(UGCC);
	CCR_PR(GCC);
	CCR_PR(USC);
	CCR_PR(VRAMRTC);
	CCR_PR(VRAMSAC);
	CCR_PR(VRAMBC);
#undef CCR_PR

	printk(KERN_DEBUG "lcr: \n");
#define LCR_PR(n)	printk(KERN_DEBUG "\t" #n " = \t%04x\n",\
		tmio_ioread16(par->lcr + LCR_ ## n));
	LCR_PR(UIS);
	LCR_PR(VHPN);
	LCR_PR(CFSAL);
	LCR_PR(CFSAH);
	LCR_PR(CFS);
	LCR_PR(CFWS);
	LCR_PR(BBIE);
	LCR_PR(BBISC);
	LCR_PR(CCS);
	LCR_PR(BBES);
	LCR_PR(CMDL);
	LCR_PR(CMDH);
	LCR_PR(CFC);
	LCR_PR(CCIFC);
	LCR_PR(HWT);
	LCR_PR(LCDCCRC);
	LCR_PR(LCDCC);
	LCR_PR(LCDCOPC);
	LCR_PR(LCDIS);
	LCR_PR(LCDIM);
	LCR_PR(LCDIE);
	LCR_PR(GDSAL);
	LCR_PR(GDSAH);
	LCR_PR(VHPCL);
	LCR_PR(VHPCH);
	LCR_PR(GM);
	LCR_PR(HT);
	LCR_PR(HDS);
	LCR_PR(HSS);
	LCR_PR(HSE);
	LCR_PR(HNP);
	LCR_PR(VT);
	LCR_PR(VDS);
	LCR_PR(VSS);
	LCR_PR(VSE);
	LCR_PR(CDLN);
	LCR_PR(ILN);
	LCR_PR(SP);
	LCR_PR(MISC);
	LCR_PR(VIHSS);
	LCR_PR(VIVS);
	LCR_PR(VIVE);
	LCR_PR(VIVSS);
	LCR_PR(VCCIS);
	LCR_PR(VIDWSAL);
	LCR_PR(VIDWSAH);
	LCR_PR(VIDRSAL);
	LCR_PR(VIDRSAH);
	LCR_PR(VIPDDST);
	LCR_PR(VIPDDET);
	LCR_PR(VIE);
	LCR_PR(VCS);
	LCR_PR(VPHWC);
	LCR_PR(VPHS);
	LCR_PR(VPVWC);
	LCR_PR(VPVS);
	LCR_PR(PLHPIX);
	LCR_PR(XS);
	LCR_PR(XCKHW);
	LCR_PR(STHS);
	LCR_PR(VT2);
	LCR_PR(YCKSW);
	LCR_PR(YSTS);
	LCR_PR(PPOLS);
	LCR_PR(PRECW);
	LCR_PR(VCLKHW);
	LCR_PR(OC);
#undef LCR_PR
}
#endif

#ifdef CONFIG_PM
static int tmiofb_suspend(struct platform_device *dev, pm_message_t state)
{
	struct fb_info *info = platform_get_drvdata(dev);
#ifdef CONFIG_FB_TMIO_ACCELL
	struct tmiofb_par *par = info->par;
#endif
	const struct mfd_cell *cell = mfd_get_cell(dev);
	int retval = 0;

	console_lock();

	fb_set_suspend(info, 1);

	if (info->fbops->fb_sync)
		info->fbops->fb_sync(info);


#ifdef CONFIG_FB_TMIO_ACCELL
	/*
	 * The fb should be usable even if interrupts are disabled (and they are
	 * during suspend/resume). Switch temporary to forced polling.
	 */
	printk(KERN_INFO "tmiofb: switching to polling\n");
	par->use_polling = true;
#endif
	tmiofb_hw_stop(dev);

	if (cell->suspend)
		retval = cell->suspend(dev);

	console_unlock();

	return retval;
}

static int tmiofb_resume(struct platform_device *dev)
{
	struct fb_info *info = platform_get_drvdata(dev);
	const struct mfd_cell *cell = mfd_get_cell(dev);
	int retval = 0;

	console_lock();

	if (cell->resume) {
		retval = cell->resume(dev);
		if (retval)
			goto out;
	}

	tmiofb_irq(-1, info);

	tmiofb_hw_init(dev);

	tmiofb_hw_mode(dev);

	fb_set_suspend(info, 0);
out:
	console_unlock();
	return retval;
}
#else
#define tmiofb_suspend	NULL
#define tmiofb_resume	NULL
#endif

static struct platform_driver tmiofb_driver = {
	.driver.name	= "tmio-fb",
	.driver.owner	= THIS_MODULE,
	.probe		= tmiofb_probe,
	.remove		= __devexit_p(tmiofb_remove),
	.suspend	= tmiofb_suspend,
	.resume		= tmiofb_resume,
};

/*--------------------------------------------------------------------------*/

#ifndef MODULE
static void __init tmiofb_setup(char *options)
{
	char *this_opt;

	if (!options || !*options)
		return;

	while ((this_opt = strsep(&options, ",")) != NULL) {
		if (!*this_opt)
			continue;
		/*
		 * FIXME
		 */
	}
}
#endif

static int __init tmiofb_init(void)
{
#ifndef MODULE
	char *option = NULL;

	if (fb_get_options("tmiofb", &option))
		return -ENODEV;
	tmiofb_setup(option);
#endif
	return platform_driver_register(&tmiofb_driver);
}

static void __exit tmiofb_cleanup(void)
{
	platform_driver_unregister(&tmiofb_driver);
}

module_init(tmiofb_init);
module_exit(tmiofb_cleanup);

MODULE_DESCRIPTION("TMIO framebuffer driver");
MODULE_AUTHOR("Chris Humbert, Dirk Opfer, Dmitry Baryshkov");
MODULE_LICENSE("GPL");
