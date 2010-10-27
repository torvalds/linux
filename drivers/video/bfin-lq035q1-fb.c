/*
 * Blackfin LCD Framebuffer driver SHARP LQ035Q1DH02
 *
 * Copyright 2008-2009 Analog Devices Inc.
 * Licensed under the GPL-2 or later.
 */

#define DRIVER_NAME "bfin-lq035q1"
#define pr_fmt(fmt) DRIVER_NAME ": " fmt

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/string.h>
#include <linux/fb.h>
#include <linux/slab.h>
#include <linux/init.h>
#include <linux/types.h>
#include <linux/interrupt.h>
#include <linux/device.h>
#include <linux/backlight.h>
#include <linux/lcd.h>
#include <linux/dma-mapping.h>
#include <linux/platform_device.h>
#include <linux/spi/spi.h>

#include <asm/blackfin.h>
#include <asm/irq.h>
#include <asm/dma.h>
#include <asm/portmux.h>
#include <asm/gptimers.h>

#include <asm/bfin-lq035q1.h>

#if defined(BF533_FAMILY) || defined(BF538_FAMILY)
#define TIMER_HSYNC_id			TIMER1_id
#define TIMER_HSYNCbit			TIMER1bit
#define TIMER_HSYNC_STATUS_TRUN		TIMER_STATUS_TRUN1
#define TIMER_HSYNC_STATUS_TIMIL	TIMER_STATUS_TIMIL1
#define TIMER_HSYNC_STATUS_TOVF		TIMER_STATUS_TOVF1

#define TIMER_VSYNC_id			TIMER2_id
#define TIMER_VSYNCbit			TIMER2bit
#define TIMER_VSYNC_STATUS_TRUN		TIMER_STATUS_TRUN2
#define TIMER_VSYNC_STATUS_TIMIL	TIMER_STATUS_TIMIL2
#define TIMER_VSYNC_STATUS_TOVF		TIMER_STATUS_TOVF2
#else
#define TIMER_HSYNC_id			TIMER0_id
#define TIMER_HSYNCbit			TIMER0bit
#define TIMER_HSYNC_STATUS_TRUN		TIMER_STATUS_TRUN0
#define TIMER_HSYNC_STATUS_TIMIL	TIMER_STATUS_TIMIL0
#define TIMER_HSYNC_STATUS_TOVF		TIMER_STATUS_TOVF0

#define TIMER_VSYNC_id			TIMER1_id
#define TIMER_VSYNCbit			TIMER1bit
#define TIMER_VSYNC_STATUS_TRUN		TIMER_STATUS_TRUN1
#define TIMER_VSYNC_STATUS_TIMIL	TIMER_STATUS_TIMIL1
#define TIMER_VSYNC_STATUS_TOVF		TIMER_STATUS_TOVF1
#endif

#define LCD_X_RES		320	/* Horizontal Resolution */
#define LCD_Y_RES		240	/* Vertical Resolution */
#define	DMA_BUS_SIZE		16
#define U_LINE			4	/* Blanking Lines */


/* Interface 16/18-bit TFT over an 8-bit wide PPI using a small Programmable Logic Device (CPLD)
 * http://blackfin.uclinux.org/gf/project/stamp/frs/?action=FrsReleaseBrowse&frs_package_id=165
 */


#define BFIN_LCD_NBR_PALETTE_ENTRIES	256

#define PPI_TX_MODE			0x2
#define PPI_XFER_TYPE_11		0xC
#define PPI_PORT_CFG_01			0x10
#define PPI_POLS_1			0x8000

#define LQ035_INDEX			0x74
#define LQ035_DATA			0x76

#define LQ035_DRIVER_OUTPUT_CTL		0x1
#define LQ035_SHUT_CTL			0x11

#define LQ035_DRIVER_OUTPUT_MASK	(LQ035_LR | LQ035_TB | LQ035_BGR | LQ035_REV)
#define LQ035_DRIVER_OUTPUT_DEFAULT	(0x2AEF & ~LQ035_DRIVER_OUTPUT_MASK)

#define LQ035_SHUT			(1 << 0)	/* Shutdown */
#define LQ035_ON			(0 << 0)	/* Shutdown */

struct bfin_lq035q1fb_info {
	struct fb_info *fb;
	struct device *dev;
	struct spi_driver spidrv;
	struct bfin_lq035q1fb_disp_info *disp_info;
	unsigned char *fb_buffer;	/* RGB Buffer */
	dma_addr_t dma_handle;
	int lq035_open_cnt;
	int irq;
	spinlock_t lock;	/* lock */
	u32 pseudo_pal[16];

	u32 lcd_bpp;
	u32 h_actpix;
	u32 h_period;
	u32 h_pulse;
	u32 h_start;
	u32 v_lines;
	u32 v_pulse;
	u32 v_period;
};

static int nocursor;
module_param(nocursor, int, 0644);
MODULE_PARM_DESC(nocursor, "cursor enable/disable");

struct spi_control {
	unsigned short mode;
};

static int lq035q1_control(struct spi_device *spi, unsigned char reg, unsigned short value)
{
	int ret;
	u8 regs[3] = { LQ035_INDEX, 0, 0 };
	u8 dat[3] = { LQ035_DATA, 0, 0 };

	if (!spi)
		return -ENODEV;

	regs[2] = reg;
	dat[1] = value >> 8;
	dat[2] = value & 0xFF;

	ret = spi_write(spi, regs, ARRAY_SIZE(regs));
	ret |= spi_write(spi, dat, ARRAY_SIZE(dat));
	return ret;
}

static int __devinit lq035q1_spidev_probe(struct spi_device *spi)
{
	int ret;
	struct spi_control *ctl;
	struct bfin_lq035q1fb_info *info = container_of(spi->dev.driver,
						struct bfin_lq035q1fb_info,
						spidrv.driver);

	ctl = kzalloc(sizeof(*ctl), GFP_KERNEL);

	if (!ctl)
		return -ENOMEM;

	ctl->mode = (info->disp_info->mode &
		LQ035_DRIVER_OUTPUT_MASK) | LQ035_DRIVER_OUTPUT_DEFAULT;

	ret = lq035q1_control(spi, LQ035_SHUT_CTL, LQ035_ON);
	ret |= lq035q1_control(spi, LQ035_DRIVER_OUTPUT_CTL, ctl->mode);
	if (ret)
		return ret;

	spi_set_drvdata(spi, ctl);

	return 0;
}

static int lq035q1_spidev_remove(struct spi_device *spi)
{
	return lq035q1_control(spi, LQ035_SHUT_CTL, LQ035_SHUT);
}

#ifdef CONFIG_PM
static int lq035q1_spidev_suspend(struct spi_device *spi, pm_message_t state)
{
	return lq035q1_control(spi, LQ035_SHUT_CTL, LQ035_SHUT);
}

static int lq035q1_spidev_resume(struct spi_device *spi)
{
	int ret;
	struct spi_control *ctl = spi_get_drvdata(spi);

	ret = lq035q1_control(spi, LQ035_DRIVER_OUTPUT_CTL, ctl->mode);
	if (ret)
		return ret;

	return lq035q1_control(spi, LQ035_SHUT_CTL, LQ035_ON);
}
#else
# define lq035q1_spidev_suspend NULL
# define lq035q1_spidev_resume  NULL
#endif

/* Power down all displays on reboot, poweroff or halt */
static void lq035q1_spidev_shutdown(struct spi_device *spi)
{
	lq035q1_control(spi, LQ035_SHUT_CTL, LQ035_SHUT);
}

static int lq035q1_backlight(struct bfin_lq035q1fb_info *info, unsigned arg)
{
	if (info->disp_info->use_bl)
		gpio_set_value(info->disp_info->gpio_bl, arg);

	return 0;
}

static int bfin_lq035q1_calc_timing(struct bfin_lq035q1fb_info *fbi)
{
	unsigned long clocks_per_pix, cpld_pipeline_delay_cor;

	/*
	 * Interface 16/18-bit TFT over an 8-bit wide PPI using a small
	 * Programmable Logic Device (CPLD)
	 * http://blackfin.uclinux.org/gf/project/stamp/frs/?action=FrsReleaseBrowse&frs_package_id=165
	 */

	switch (fbi->disp_info->ppi_mode) {
	case USE_RGB565_16_BIT_PPI:
		fbi->lcd_bpp = 16;
		clocks_per_pix = 1;
		cpld_pipeline_delay_cor = 0;
		break;
	case USE_RGB565_8_BIT_PPI:
		fbi->lcd_bpp = 16;
		clocks_per_pix = 2;
		cpld_pipeline_delay_cor = 3;
		break;
	case USE_RGB888_8_BIT_PPI:
		fbi->lcd_bpp = 24;
		clocks_per_pix = 3;
		cpld_pipeline_delay_cor = 5;
		break;
	default:
		return -EINVAL;
	}

	/*
	 * HS and VS timing parameters (all in number of PPI clk ticks)
	 */

	fbi->h_actpix = (LCD_X_RES * clocks_per_pix);	/* active horizontal pixel */
	fbi->h_period = (336 * clocks_per_pix);		/* HS period */
	fbi->h_pulse = (2 * clocks_per_pix);				/* HS pulse width */
	fbi->h_start = (7 * clocks_per_pix + cpld_pipeline_delay_cor);	/* first valid pixel */

	fbi->v_lines = (LCD_Y_RES + U_LINE);		/* total vertical lines */
	fbi->v_pulse = (2 * clocks_per_pix);		/* VS pulse width (1-5 H_PERIODs) */
	fbi->v_period =	(fbi->h_period * fbi->v_lines);	/* VS period */

	return 0;
}

static void bfin_lq035q1_config_ppi(struct bfin_lq035q1fb_info *fbi)
{
	unsigned ppi_pmode;

	if (fbi->disp_info->ppi_mode == USE_RGB565_16_BIT_PPI)
		ppi_pmode = DLEN_16;
	else
		ppi_pmode = (DLEN_8 | PACK_EN);

	bfin_write_PPI_DELAY(fbi->h_start);
	bfin_write_PPI_COUNT(fbi->h_actpix - 1);
	bfin_write_PPI_FRAME(fbi->v_lines);

	bfin_write_PPI_CONTROL(PPI_TX_MODE |	   /* output mode , PORT_DIR */
				PPI_XFER_TYPE_11 | /* sync mode XFR_TYPE */
				PPI_PORT_CFG_01 |  /* two frame sync PORT_CFG */
				ppi_pmode |	   /* 8/16 bit data length / PACK_EN? */
				PPI_POLS_1);	   /* faling edge syncs POLS */
}

static inline void bfin_lq035q1_disable_ppi(void)
{
	bfin_write_PPI_CONTROL(bfin_read_PPI_CONTROL() & ~PORT_EN);
}

static inline void bfin_lq035q1_enable_ppi(void)
{
	bfin_write_PPI_CONTROL(bfin_read_PPI_CONTROL() | PORT_EN);
}

static void bfin_lq035q1_start_timers(void)
{
	enable_gptimers(TIMER_VSYNCbit | TIMER_HSYNCbit);
}

static void bfin_lq035q1_stop_timers(void)
{
	disable_gptimers(TIMER_HSYNCbit | TIMER_VSYNCbit);

	set_gptimer_status(0, TIMER_HSYNC_STATUS_TRUN | TIMER_VSYNC_STATUS_TRUN |
				TIMER_HSYNC_STATUS_TIMIL | TIMER_VSYNC_STATUS_TIMIL |
				 TIMER_HSYNC_STATUS_TOVF | TIMER_VSYNC_STATUS_TOVF);

}

static void bfin_lq035q1_init_timers(struct bfin_lq035q1fb_info *fbi)
{

	bfin_lq035q1_stop_timers();

	set_gptimer_period(TIMER_HSYNC_id, fbi->h_period);
	set_gptimer_pwidth(TIMER_HSYNC_id, fbi->h_pulse);
	set_gptimer_config(TIMER_HSYNC_id, TIMER_MODE_PWM | TIMER_PERIOD_CNT |
				      TIMER_TIN_SEL | TIMER_CLK_SEL|
				      TIMER_EMU_RUN);

	set_gptimer_period(TIMER_VSYNC_id, fbi->v_period);
	set_gptimer_pwidth(TIMER_VSYNC_id, fbi->v_pulse);
	set_gptimer_config(TIMER_VSYNC_id, TIMER_MODE_PWM | TIMER_PERIOD_CNT |
				      TIMER_TIN_SEL | TIMER_CLK_SEL |
				      TIMER_EMU_RUN);

}

static void bfin_lq035q1_config_dma(struct bfin_lq035q1fb_info *fbi)
{


	set_dma_config(CH_PPI,
		       set_bfin_dma_config(DIR_READ, DMA_FLOW_AUTO,
					   INTR_DISABLE, DIMENSION_2D,
					   DATA_SIZE_16,
					   DMA_NOSYNC_KEEP_DMA_BUF));
	set_dma_x_count(CH_PPI, (LCD_X_RES * fbi->lcd_bpp) / DMA_BUS_SIZE);
	set_dma_x_modify(CH_PPI, DMA_BUS_SIZE / 8);
	set_dma_y_count(CH_PPI, fbi->v_lines);

	set_dma_y_modify(CH_PPI, DMA_BUS_SIZE / 8);
	set_dma_start_addr(CH_PPI, (unsigned long)fbi->fb_buffer);

}

static const u16 ppi0_req_16[] = {P_PPI0_CLK, P_PPI0_FS1, P_PPI0_FS2,
			    P_PPI0_D0, P_PPI0_D1, P_PPI0_D2,
			    P_PPI0_D3, P_PPI0_D4, P_PPI0_D5,
			    P_PPI0_D6, P_PPI0_D7, P_PPI0_D8,
			    P_PPI0_D9, P_PPI0_D10, P_PPI0_D11,
			    P_PPI0_D12, P_PPI0_D13, P_PPI0_D14,
			    P_PPI0_D15, 0};

static const u16 ppi0_req_8[] = {P_PPI0_CLK, P_PPI0_FS1, P_PPI0_FS2,
			    P_PPI0_D0, P_PPI0_D1, P_PPI0_D2,
			    P_PPI0_D3, P_PPI0_D4, P_PPI0_D5,
			    P_PPI0_D6, P_PPI0_D7, 0};

static inline void bfin_lq035q1_free_ports(unsigned ppi16)
{
	if (ppi16)
		peripheral_free_list(ppi0_req_16);
	else
		peripheral_free_list(ppi0_req_8);

	if (ANOMALY_05000400)
		gpio_free(P_IDENT(P_PPI0_FS3));
}

static int __devinit bfin_lq035q1_request_ports(struct platform_device *pdev,
						unsigned ppi16)
{
	int ret;
	/* ANOMALY_05000400 - PPI Does Not Start Properly In Specific Mode:
	 * Drive PPI_FS3 Low
	 */
	if (ANOMALY_05000400) {
		int ret = gpio_request(P_IDENT(P_PPI0_FS3), "PPI_FS3");
		if (ret)
			return ret;
		gpio_direction_output(P_IDENT(P_PPI0_FS3), 0);
	}

	if (ppi16)
		ret = peripheral_request_list(ppi0_req_16, DRIVER_NAME);
	else
		ret = peripheral_request_list(ppi0_req_8, DRIVER_NAME);

	if (ret) {
		dev_err(&pdev->dev, "requesting peripherals failed\n");
		return -EFAULT;
	}

	return 0;
}

static int bfin_lq035q1_fb_open(struct fb_info *info, int user)
{
	struct bfin_lq035q1fb_info *fbi = info->par;

	spin_lock(&fbi->lock);
	fbi->lq035_open_cnt++;

	if (fbi->lq035_open_cnt <= 1) {

		bfin_lq035q1_disable_ppi();
		SSYNC();

		bfin_lq035q1_config_dma(fbi);
		bfin_lq035q1_config_ppi(fbi);
		bfin_lq035q1_init_timers(fbi);

		/* start dma */
		enable_dma(CH_PPI);
		bfin_lq035q1_enable_ppi();
		bfin_lq035q1_start_timers();
		lq035q1_backlight(fbi, 1);
	}

	spin_unlock(&fbi->lock);

	return 0;
}

static int bfin_lq035q1_fb_release(struct fb_info *info, int user)
{
	struct bfin_lq035q1fb_info *fbi = info->par;

	spin_lock(&fbi->lock);

	fbi->lq035_open_cnt--;

	if (fbi->lq035_open_cnt <= 0) {
		lq035q1_backlight(fbi, 0);
		bfin_lq035q1_disable_ppi();
		SSYNC();
		disable_dma(CH_PPI);
		bfin_lq035q1_stop_timers();
	}

	spin_unlock(&fbi->lock);

	return 0;
}

static int bfin_lq035q1_fb_check_var(struct fb_var_screeninfo *var,
				     struct fb_info *info)
{
	struct bfin_lq035q1fb_info *fbi = info->par;

	if (var->bits_per_pixel == fbi->lcd_bpp) {
		var->red.offset = info->var.red.offset;
		var->green.offset = info->var.green.offset;
		var->blue.offset = info->var.blue.offset;
		var->red.length = info->var.red.length;
		var->green.length = info->var.green.length;
		var->blue.length = info->var.blue.length;
		var->transp.offset = 0;
		var->transp.length = 0;
		var->transp.msb_right = 0;
		var->red.msb_right = 0;
		var->green.msb_right = 0;
		var->blue.msb_right = 0;
	} else {
		pr_debug("%s: depth not supported: %u BPP\n", __func__,
			 var->bits_per_pixel);
		return -EINVAL;
	}

	if (info->var.xres != var->xres || info->var.yres != var->yres ||
	    info->var.xres_virtual != var->xres_virtual ||
	    info->var.yres_virtual != var->yres_virtual) {
		pr_debug("%s: Resolution not supported: X%u x Y%u \n",
			 __func__, var->xres, var->yres);
		return -EINVAL;
	}

	/*
	 *  Memory limit
	 */

	if ((info->fix.line_length * var->yres_virtual) > info->fix.smem_len) {
		pr_debug("%s: Memory Limit requested yres_virtual = %u\n",
			 __func__, var->yres_virtual);
		return -ENOMEM;
	}


	return 0;
}

int bfin_lq035q1_fb_cursor(struct fb_info *info, struct fb_cursor *cursor)
{
	if (nocursor)
		return 0;
	else
		return -EINVAL;	/* just to force soft_cursor() call */
}

static int bfin_lq035q1_fb_setcolreg(u_int regno, u_int red, u_int green,
				   u_int blue, u_int transp,
				   struct fb_info *info)
{
	if (regno >= BFIN_LCD_NBR_PALETTE_ENTRIES)
		return -EINVAL;

	if (info->var.grayscale) {
		/* grayscale = 0.30*R + 0.59*G + 0.11*B */
		red = green = blue = (red * 77 + green * 151 + blue * 28) >> 8;
	}

	if (info->fix.visual == FB_VISUAL_TRUECOLOR) {

		u32 value;
		/* Place color in the pseudopalette */
		if (regno > 16)
			return -EINVAL;

		red >>= (16 - info->var.red.length);
		green >>= (16 - info->var.green.length);
		blue >>= (16 - info->var.blue.length);

		value = (red << info->var.red.offset) |
		    (green << info->var.green.offset) |
		    (blue << info->var.blue.offset);
		value &= 0xFFFFFF;

		((u32 *) (info->pseudo_palette))[regno] = value;

	}

	return 0;
}

static struct fb_ops bfin_lq035q1_fb_ops = {
	.owner = THIS_MODULE,
	.fb_open = bfin_lq035q1_fb_open,
	.fb_release = bfin_lq035q1_fb_release,
	.fb_check_var = bfin_lq035q1_fb_check_var,
	.fb_fillrect = cfb_fillrect,
	.fb_copyarea = cfb_copyarea,
	.fb_imageblit = cfb_imageblit,
	.fb_cursor = bfin_lq035q1_fb_cursor,
	.fb_setcolreg = bfin_lq035q1_fb_setcolreg,
};

static irqreturn_t bfin_lq035q1_irq_error(int irq, void *dev_id)
{
	/*struct bfin_lq035q1fb_info *info = (struct bfin_lq035q1fb_info *)dev_id;*/

	u16 status = bfin_read_PPI_STATUS();
	bfin_write_PPI_STATUS(-1);

	if (status) {
		bfin_lq035q1_disable_ppi();
		disable_dma(CH_PPI);

		/* start dma */
		enable_dma(CH_PPI);
		bfin_lq035q1_enable_ppi();
		bfin_write_PPI_STATUS(-1);
	}

	return IRQ_HANDLED;
}

static int __devinit bfin_lq035q1_probe(struct platform_device *pdev)
{
	struct bfin_lq035q1fb_info *info;
	struct fb_info *fbinfo;
	u32 active_video_mem_offset;
	int ret;

	ret = request_dma(CH_PPI, DRIVER_NAME"_CH_PPI");
	if (ret < 0) {
		dev_err(&pdev->dev, "PPI DMA unavailable\n");
		goto out1;
	}

	fbinfo = framebuffer_alloc(sizeof(*info), &pdev->dev);
	if (!fbinfo) {
		ret = -ENOMEM;
		goto out2;
	}

	info = fbinfo->par;
	info->fb = fbinfo;
	info->dev = &pdev->dev;

	info->disp_info = pdev->dev.platform_data;

	platform_set_drvdata(pdev, fbinfo);

	ret = bfin_lq035q1_calc_timing(info);
	if (ret < 0) {
		dev_err(&pdev->dev, "Failed PPI Mode\n");
		goto out3;
	}

	strcpy(fbinfo->fix.id, DRIVER_NAME);

	fbinfo->fix.type = FB_TYPE_PACKED_PIXELS;
	fbinfo->fix.type_aux = 0;
	fbinfo->fix.xpanstep = 0;
	fbinfo->fix.ypanstep = 0;
	fbinfo->fix.ywrapstep = 0;
	fbinfo->fix.accel = FB_ACCEL_NONE;
	fbinfo->fix.visual = FB_VISUAL_TRUECOLOR;

	fbinfo->var.nonstd = 0;
	fbinfo->var.activate = FB_ACTIVATE_NOW;
	fbinfo->var.height = -1;
	fbinfo->var.width = -1;
	fbinfo->var.accel_flags = 0;
	fbinfo->var.vmode = FB_VMODE_NONINTERLACED;

	fbinfo->var.xres = LCD_X_RES;
	fbinfo->var.xres_virtual = LCD_X_RES;
	fbinfo->var.yres = LCD_Y_RES;
	fbinfo->var.yres_virtual = LCD_Y_RES;
	fbinfo->var.bits_per_pixel = info->lcd_bpp;

	if (info->disp_info->mode & LQ035_BGR) {
		if (info->lcd_bpp == 24) {
			fbinfo->var.red.offset = 0;
			fbinfo->var.green.offset = 8;
			fbinfo->var.blue.offset = 16;
		} else {
			fbinfo->var.red.offset = 0;
			fbinfo->var.green.offset = 5;
			fbinfo->var.blue.offset = 11;
		}
	} else {
		if (info->lcd_bpp == 24) {
			fbinfo->var.red.offset = 16;
			fbinfo->var.green.offset = 8;
			fbinfo->var.blue.offset = 0;
		} else {
			fbinfo->var.red.offset = 11;
			fbinfo->var.green.offset = 5;
			fbinfo->var.blue.offset = 0;
		}
	}

	fbinfo->var.transp.offset = 0;

	if (info->lcd_bpp == 24) {
		fbinfo->var.red.length = 8;
		fbinfo->var.green.length = 8;
		fbinfo->var.blue.length = 8;
	} else {
		fbinfo->var.red.length = 5;
		fbinfo->var.green.length = 6;
		fbinfo->var.blue.length = 5;
	}

	fbinfo->var.transp.length = 0;

	active_video_mem_offset = ((U_LINE / 2) * LCD_X_RES * (info->lcd_bpp / 8));

	fbinfo->fix.smem_len = LCD_X_RES * LCD_Y_RES * info->lcd_bpp / 8
				+ active_video_mem_offset;

	fbinfo->fix.line_length = fbinfo->var.xres_virtual *
	    fbinfo->var.bits_per_pixel / 8;


	fbinfo->fbops = &bfin_lq035q1_fb_ops;
	fbinfo->flags = FBINFO_FLAG_DEFAULT;

	info->fb_buffer =
	    dma_alloc_coherent(NULL, fbinfo->fix.smem_len, &info->dma_handle,
			       GFP_KERNEL);

	if (NULL == info->fb_buffer) {
		dev_err(&pdev->dev, "couldn't allocate dma buffer\n");
		ret = -ENOMEM;
		goto out3;
	}

	fbinfo->screen_base = (void *)info->fb_buffer + active_video_mem_offset;
	fbinfo->fix.smem_start = (int)info->fb_buffer + active_video_mem_offset;

	fbinfo->fbops = &bfin_lq035q1_fb_ops;

	fbinfo->pseudo_palette = &info->pseudo_pal;

	ret = fb_alloc_cmap(&fbinfo->cmap, BFIN_LCD_NBR_PALETTE_ENTRIES, 0);
	if (ret < 0) {
		dev_err(&pdev->dev, "failed to allocate colormap (%d entries)\n",
		       BFIN_LCD_NBR_PALETTE_ENTRIES);
		goto out4;
	}

	ret = bfin_lq035q1_request_ports(pdev,
			info->disp_info->ppi_mode == USE_RGB565_16_BIT_PPI);
	if (ret) {
		dev_err(&pdev->dev, "couldn't request gpio port\n");
		goto out6;
	}

	info->irq = platform_get_irq(pdev, 0);
	if (info->irq < 0) {
		ret = -EINVAL;
		goto out7;
	}

	ret = request_irq(info->irq, bfin_lq035q1_irq_error, IRQF_DISABLED,
			DRIVER_NAME" PPI ERROR", info);
	if (ret < 0) {
		dev_err(&pdev->dev, "unable to request PPI ERROR IRQ\n");
		goto out7;
	}

	info->spidrv.driver.name = DRIVER_NAME"-spi";
	info->spidrv.probe    = lq035q1_spidev_probe;
	info->spidrv.remove   = __devexit_p(lq035q1_spidev_remove);
	info->spidrv.shutdown = lq035q1_spidev_shutdown;
	info->spidrv.suspend  = lq035q1_spidev_suspend;
	info->spidrv.resume   = lq035q1_spidev_resume;

	ret = spi_register_driver(&info->spidrv);
	if (ret < 0) {
		dev_err(&pdev->dev, "couldn't register SPI Interface\n");
		goto out8;
	}

	if (info->disp_info->use_bl) {
		ret = gpio_request(info->disp_info->gpio_bl, "LQ035 Backlight");

		if (ret) {
			dev_err(&pdev->dev, "failed to request GPIO %d\n",
				info->disp_info->gpio_bl);
			goto out9;
		}
		gpio_direction_output(info->disp_info->gpio_bl, 0);
	}

	ret = register_framebuffer(fbinfo);
	if (ret < 0) {
		dev_err(&pdev->dev, "unable to register framebuffer\n");
		goto out10;
	}

	dev_info(&pdev->dev, "%dx%d %d-bit RGB FrameBuffer initialized\n",
		LCD_X_RES, LCD_Y_RES, info->lcd_bpp);

	return 0;

 out10:
	if (info->disp_info->use_bl)
		gpio_free(info->disp_info->gpio_bl);
 out9:
	spi_unregister_driver(&info->spidrv);
 out8:
	free_irq(info->irq, info);
 out7:
	bfin_lq035q1_free_ports(info->disp_info->ppi_mode ==
				USE_RGB565_16_BIT_PPI);
 out6:
	fb_dealloc_cmap(&fbinfo->cmap);
 out4:
	dma_free_coherent(NULL, fbinfo->fix.smem_len, info->fb_buffer,
			  info->dma_handle);
 out3:
	framebuffer_release(fbinfo);
 out2:
	free_dma(CH_PPI);
 out1:
	platform_set_drvdata(pdev, NULL);

	return ret;
}

static int __devexit bfin_lq035q1_remove(struct platform_device *pdev)
{
	struct fb_info *fbinfo = platform_get_drvdata(pdev);
	struct bfin_lq035q1fb_info *info = fbinfo->par;

	if (info->disp_info->use_bl)
		gpio_free(info->disp_info->gpio_bl);

	spi_unregister_driver(&info->spidrv);

	unregister_framebuffer(fbinfo);

	free_dma(CH_PPI);
	free_irq(info->irq, info);

	if (info->fb_buffer != NULL)
		dma_free_coherent(NULL, fbinfo->fix.smem_len, info->fb_buffer,
				  info->dma_handle);

	fb_dealloc_cmap(&fbinfo->cmap);

	bfin_lq035q1_free_ports(info->disp_info->ppi_mode ==
				USE_RGB565_16_BIT_PPI);

	platform_set_drvdata(pdev, NULL);
	framebuffer_release(fbinfo);

	dev_info(&pdev->dev, "unregistered LCD driver\n");

	return 0;
}

#ifdef CONFIG_PM
static int bfin_lq035q1_suspend(struct device *dev)
{
	struct fb_info *fbinfo = dev_get_drvdata(dev);
	struct bfin_lq035q1fb_info *info = fbinfo->par;

	if (info->lq035_open_cnt) {
		lq035q1_backlight(info, 0);
		bfin_lq035q1_disable_ppi();
		SSYNC();
		disable_dma(CH_PPI);
		bfin_lq035q1_stop_timers();
		bfin_write_PPI_STATUS(-1);
	}

	return 0;
}

static int bfin_lq035q1_resume(struct device *dev)
{
	struct fb_info *fbinfo = dev_get_drvdata(dev);
	struct bfin_lq035q1fb_info *info = fbinfo->par;

	if (info->lq035_open_cnt) {
		bfin_lq035q1_disable_ppi();
		SSYNC();

		bfin_lq035q1_config_dma(info);
		bfin_lq035q1_config_ppi(info);
		bfin_lq035q1_init_timers(info);

		/* start dma */
		enable_dma(CH_PPI);
		bfin_lq035q1_enable_ppi();
		bfin_lq035q1_start_timers();
		lq035q1_backlight(info, 1);
	}

	return 0;
}

static struct dev_pm_ops bfin_lq035q1_dev_pm_ops = {
	.suspend = bfin_lq035q1_suspend,
	.resume  = bfin_lq035q1_resume,
};
#endif

static struct platform_driver bfin_lq035q1_driver = {
	.probe   = bfin_lq035q1_probe,
	.remove  = __devexit_p(bfin_lq035q1_remove),
	.driver = {
		.name = DRIVER_NAME,
#ifdef CONFIG_PM
		.pm   = &bfin_lq035q1_dev_pm_ops,
#endif
	},
};

static int __init bfin_lq035q1_driver_init(void)
{
	return platform_driver_register(&bfin_lq035q1_driver);
}
module_init(bfin_lq035q1_driver_init);

static void __exit bfin_lq035q1_driver_cleanup(void)
{
	platform_driver_unregister(&bfin_lq035q1_driver);
}
module_exit(bfin_lq035q1_driver_cleanup);

MODULE_DESCRIPTION("Blackfin TFT LCD Driver");
MODULE_LICENSE("GPL");
