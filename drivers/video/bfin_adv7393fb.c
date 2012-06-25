/*
 * Frame buffer driver for ADV7393/2 video encoder
 *
 * Copyright 2006-2009 Analog Devices Inc.
 * Licensed under the GPL-2 or late.
 */

/*
 * TODO: Remove Globals
 * TODO: Code Cleanup
 */

#define pr_fmt(fmt) DRIVER_NAME ": " fmt

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/string.h>
#include <linux/mm.h>
#include <linux/tty.h>
#include <linux/slab.h>
#include <linux/delay.h>
#include <linux/fb.h>
#include <linux/ioport.h>
#include <linux/init.h>
#include <linux/types.h>
#include <linux/interrupt.h>
#include <linux/sched.h>
#include <asm/blackfin.h>
#include <asm/irq.h>
#include <asm/dma.h>
#include <linux/uaccess.h>
#include <linux/gpio.h>
#include <asm/portmux.h>

#include <linux/dma-mapping.h>
#include <linux/proc_fs.h>
#include <linux/platform_device.h>
#include <linux/i2c.h>

#include "bfin_adv7393fb.h"

static int mode = VMODE;
static int mem = VMEM;
static int nocursor = 1;

static const unsigned short ppi_pins[] = {
	P_PPI0_CLK, P_PPI0_FS1, P_PPI0_FS2,
	P_PPI0_D0, P_PPI0_D1, P_PPI0_D2, P_PPI0_D3,
	P_PPI0_D4, P_PPI0_D5, P_PPI0_D6, P_PPI0_D7,
	P_PPI0_D8, P_PPI0_D9, P_PPI0_D10, P_PPI0_D11,
	P_PPI0_D12, P_PPI0_D13, P_PPI0_D14, P_PPI0_D15,
	0
};

/*
 * card parameters
 */

static struct bfin_adv7393_fb_par {
	/* structure holding blackfin / adv7393 paramters when
	   screen is blanked */
	struct {
		u8 Mode;	/* ntsc/pal/? */
	} vga_state;
	atomic_t ref_count;
} bfin_par;

/* --------------------------------------------------------------------- */

static struct fb_var_screeninfo bfin_adv7393_fb_defined = {
	.xres = 720,
	.yres = 480,
	.xres_virtual = 720,
	.yres_virtual = 480,
	.bits_per_pixel = 16,
	.activate = FB_ACTIVATE_TEST,
	.height = -1,
	.width = -1,
	.left_margin = 0,
	.right_margin = 0,
	.upper_margin = 0,
	.lower_margin = 0,
	.vmode = FB_VMODE_INTERLACED,
	.red = {11, 5, 0},
	.green = {5, 6, 0},
	.blue = {0, 5, 0},
	.transp = {0, 0, 0},
};

static struct fb_fix_screeninfo bfin_adv7393_fb_fix __devinitdata = {
	.id = "BFIN ADV7393",
	.smem_len = 720 * 480 * 2,
	.type = FB_TYPE_PACKED_PIXELS,
	.visual = FB_VISUAL_TRUECOLOR,
	.xpanstep = 0,
	.ypanstep = 0,
	.line_length = 720 * 2,
	.accel = FB_ACCEL_NONE
};

static struct fb_ops bfin_adv7393_fb_ops = {
	.owner = THIS_MODULE,
	.fb_open = bfin_adv7393_fb_open,
	.fb_release = bfin_adv7393_fb_release,
	.fb_check_var = bfin_adv7393_fb_check_var,
	.fb_pan_display = bfin_adv7393_fb_pan_display,
	.fb_blank = bfin_adv7393_fb_blank,
	.fb_fillrect = cfb_fillrect,
	.fb_copyarea = cfb_copyarea,
	.fb_imageblit = cfb_imageblit,
	.fb_cursor = bfin_adv7393_fb_cursor,
	.fb_setcolreg = bfin_adv7393_fb_setcolreg,
};

static int dma_desc_list(struct adv7393fb_device *fbdev, u16 arg)
{
	if (arg == BUILD) {	/* Build */
		fbdev->vb1 = l1_data_sram_zalloc(sizeof(struct dmasg));
		if (fbdev->vb1 == NULL)
			goto error;

		fbdev->av1 = l1_data_sram_zalloc(sizeof(struct dmasg));
		if (fbdev->av1 == NULL)
			goto error;

		fbdev->vb2 = l1_data_sram_zalloc(sizeof(struct dmasg));
		if (fbdev->vb2 == NULL)
			goto error;

		fbdev->av2 = l1_data_sram_zalloc(sizeof(struct dmasg));
		if (fbdev->av2 == NULL)
			goto error;

		/* Build linked DMA descriptor list */
		fbdev->vb1->next_desc_addr = fbdev->av1;
		fbdev->av1->next_desc_addr = fbdev->vb2;
		fbdev->vb2->next_desc_addr = fbdev->av2;
		fbdev->av2->next_desc_addr = fbdev->vb1;

		/* Save list head */
		fbdev->descriptor_list_head = fbdev->av2;

		/* Vertical Blanking Field 1 */
		fbdev->vb1->start_addr = VB_DUMMY_MEMORY_SOURCE;
		fbdev->vb1->cfg = DMA_CFG_VAL;

		fbdev->vb1->x_count =
		    fbdev->modes[mode].xres + fbdev->modes[mode].boeft_blank;

		fbdev->vb1->x_modify = 0;
		fbdev->vb1->y_count = fbdev->modes[mode].vb1_lines;
		fbdev->vb1->y_modify = 0;

		/* Active Video Field 1 */

		fbdev->av1->start_addr = (unsigned long)fbdev->fb_mem;
		fbdev->av1->cfg = DMA_CFG_VAL;
		fbdev->av1->x_count =
		    fbdev->modes[mode].xres + fbdev->modes[mode].boeft_blank;
		fbdev->av1->x_modify = fbdev->modes[mode].bpp / 8;
		fbdev->av1->y_count = fbdev->modes[mode].a_lines;
		fbdev->av1->y_modify =
		    (fbdev->modes[mode].xres - fbdev->modes[mode].boeft_blank +
		     1) * (fbdev->modes[mode].bpp / 8);

		/* Vertical Blanking Field 2 */

		fbdev->vb2->start_addr = VB_DUMMY_MEMORY_SOURCE;
		fbdev->vb2->cfg = DMA_CFG_VAL;
		fbdev->vb2->x_count =
		    fbdev->modes[mode].xres + fbdev->modes[mode].boeft_blank;

		fbdev->vb2->x_modify = 0;
		fbdev->vb2->y_count = fbdev->modes[mode].vb2_lines;
		fbdev->vb2->y_modify = 0;

		/* Active Video Field 2 */

		fbdev->av2->start_addr =
		    (unsigned long)fbdev->fb_mem + fbdev->line_len;

		fbdev->av2->cfg = DMA_CFG_VAL;

		fbdev->av2->x_count =
		    fbdev->modes[mode].xres + fbdev->modes[mode].boeft_blank;

		fbdev->av2->x_modify = (fbdev->modes[mode].bpp / 8);
		fbdev->av2->y_count = fbdev->modes[mode].a_lines;

		fbdev->av2->y_modify =
		    (fbdev->modes[mode].xres - fbdev->modes[mode].boeft_blank +
		     1) * (fbdev->modes[mode].bpp / 8);

		return 1;
	}

error:
	l1_data_sram_free(fbdev->vb1);
	l1_data_sram_free(fbdev->av1);
	l1_data_sram_free(fbdev->vb2);
	l1_data_sram_free(fbdev->av2);

	return 0;
}

static int bfin_config_dma(struct adv7393fb_device *fbdev)
{
	BUG_ON(!(fbdev->fb_mem));

	set_dma_x_count(CH_PPI, fbdev->descriptor_list_head->x_count);
	set_dma_x_modify(CH_PPI, fbdev->descriptor_list_head->x_modify);
	set_dma_y_count(CH_PPI, fbdev->descriptor_list_head->y_count);
	set_dma_y_modify(CH_PPI, fbdev->descriptor_list_head->y_modify);
	set_dma_start_addr(CH_PPI, fbdev->descriptor_list_head->start_addr);
	set_dma_next_desc_addr(CH_PPI,
			       fbdev->descriptor_list_head->next_desc_addr);
	set_dma_config(CH_PPI, fbdev->descriptor_list_head->cfg);

	return 1;
}

static void bfin_disable_dma(void)
{
	bfin_write_DMA0_CONFIG(bfin_read_DMA0_CONFIG() & ~DMAEN);
}

static void bfin_config_ppi(struct adv7393fb_device *fbdev)
{
	if (ANOMALY_05000183) {
		bfin_write_TIMER2_CONFIG(WDTH_CAP);
		bfin_write_TIMER_ENABLE(TIMEN2);
	}

	bfin_write_PPI_CONTROL(0x381E);
	bfin_write_PPI_FRAME(fbdev->modes[mode].tot_lines);
	bfin_write_PPI_COUNT(fbdev->modes[mode].xres +
			     fbdev->modes[mode].boeft_blank - 1);
	bfin_write_PPI_DELAY(fbdev->modes[mode].aoeft_blank - 1);
}

static void bfin_enable_ppi(void)
{
	bfin_write_PPI_CONTROL(bfin_read_PPI_CONTROL() | PORT_EN);
}

static void bfin_disable_ppi(void)
{
	bfin_write_PPI_CONTROL(bfin_read_PPI_CONTROL() & ~PORT_EN);
}

static inline int adv7393_write(struct i2c_client *client, u8 reg, u8 value)
{
	return i2c_smbus_write_byte_data(client, reg, value);
}

static inline int adv7393_read(struct i2c_client *client, u8 reg)
{
	return i2c_smbus_read_byte_data(client, reg);
}

static int
adv7393_write_block(struct i2c_client *client,
		    const u8 *data, unsigned int len)
{
	int ret = -1;
	u8 reg;

	while (len >= 2) {
		reg = *data++;
		ret = adv7393_write(client, reg, *data++);
		if (ret < 0)
			break;
		len -= 2;
	}

	return ret;
}

static int adv7393_mode(struct i2c_client *client, u16 mode)
{
	switch (mode) {
	case POWER_ON:		/* ADV7393 Sleep mode OFF */
		adv7393_write(client, 0x00, 0x1E);
		break;
	case POWER_DOWN:	/* ADV7393 Sleep mode ON */
		adv7393_write(client, 0x00, 0x1F);
		break;
	case BLANK_OFF:		/* Pixel Data Valid */
		adv7393_write(client, 0x82, 0xCB);
		break;
	case BLANK_ON:		/* Pixel Data Invalid */
		adv7393_write(client, 0x82, 0x8B);
		break;
	default:
		return -EINVAL;
		break;
	}
	return 0;
}

static irqreturn_t ppi_irq_error(int irq, void *dev_id)
{

	struct adv7393fb_device *fbdev = (struct adv7393fb_device *)dev_id;

	u16 status = bfin_read_PPI_STATUS();

	pr_debug("%s: PPI Status = 0x%X\n", __func__, status);

	if (status) {
		bfin_disable_dma();	/* TODO: Check Sequence */
		bfin_disable_ppi();
		bfin_clear_PPI_STATUS();
		bfin_config_dma(fbdev);
		bfin_enable_ppi();
	}

	return IRQ_HANDLED;

}

static int proc_output(char *buf)
{
	char *p = buf;

	p += sprintf(p,
		"Usage:\n"
		"echo 0x[REG][Value] > adv7393\n"
		"example: echo 0x1234 >adv7393\n"
		"writes 0x34 into Register 0x12\n");

	return p - buf;
}

static int
adv7393_read_proc(char *page, char **start, off_t off,
		  int count, int *eof, void *data)
{
	int len;

	len = proc_output(page);
	if (len <= off + count)
		*eof = 1;
	*start = page + off;
	len -= off;
	if (len > count)
		len = count;
	if (len < 0)
		len = 0;
	return len;
}

static int
adv7393_write_proc(struct file *file, const char __user * buffer,
		   size_t count, void *data)
{
	struct adv7393fb_device *fbdev = data;
	unsigned int val;
	int ret;

	ret = kstrtouint_from_user(buffer, count, 0, &val);
	if (ret)
		return -EFAULT;

	adv7393_write(fbdev->client, val >> 8, val & 0xff);

	return count;
}

static int __devinit bfin_adv7393_fb_probe(struct i2c_client *client,
					   const struct i2c_device_id *id)
{
	int ret = 0;
	struct proc_dir_entry *entry;
	int num_modes = ARRAY_SIZE(known_modes);

	struct adv7393fb_device *fbdev = NULL;

	if (mem > 2) {
		dev_err(&client->dev, "mem out of allowed range [1;2]\n");
		return -EINVAL;
	}

	if (mode > num_modes) {
		dev_err(&client->dev, "mode %d: not supported", mode);
		return -EFAULT;
	}

	fbdev = kzalloc(sizeof(*fbdev), GFP_KERNEL);
	if (!fbdev) {
		dev_err(&client->dev, "failed to allocate device private record");
		return -ENOMEM;
	}

	i2c_set_clientdata(client, fbdev);

	fbdev->modes = known_modes;
	fbdev->client = client;

	fbdev->fb_len =
	    mem * fbdev->modes[mode].xres * fbdev->modes[mode].xres *
	    (fbdev->modes[mode].bpp / 8);

	fbdev->line_len =
	    fbdev->modes[mode].xres * (fbdev->modes[mode].bpp / 8);

	/* Workaround "PPI Does Not Start Properly In Specific Mode" */
	if (ANOMALY_05000400) {
		ret = gpio_request_one(P_IDENT(P_PPI0_FS3), GPIOF_OUT_INIT_LOW,
					"PPI0_FS3")
		if (ret) {
			dev_err(&client->dev, "PPI0_FS3 GPIO request failed\n");
			ret = -EBUSY;
			goto free_fbdev;
		}
	}

	if (peripheral_request_list(ppi_pins, DRIVER_NAME)) {
		dev_err(&client->dev, "requesting PPI peripheral failed\n");
		ret = -EFAULT;
		goto free_gpio;
	}

	fbdev->fb_mem =
	    dma_alloc_coherent(NULL, fbdev->fb_len, &fbdev->dma_handle,
			       GFP_KERNEL);

	if (NULL == fbdev->fb_mem) {
		dev_err(&client->dev, "couldn't allocate dma buffer (%d bytes)\n",
		       (u32) fbdev->fb_len);
		ret = -ENOMEM;
		goto free_ppi_pins;
	}

	fbdev->info.screen_base = (void *)fbdev->fb_mem;
	bfin_adv7393_fb_fix.smem_start = (int)fbdev->fb_mem;

	bfin_adv7393_fb_fix.smem_len = fbdev->fb_len;
	bfin_adv7393_fb_fix.line_length = fbdev->line_len;

	if (mem > 1)
		bfin_adv7393_fb_fix.ypanstep = 1;

	bfin_adv7393_fb_defined.red.length = 5;
	bfin_adv7393_fb_defined.green.length = 6;
	bfin_adv7393_fb_defined.blue.length = 5;

	bfin_adv7393_fb_defined.xres = fbdev->modes[mode].xres;
	bfin_adv7393_fb_defined.yres = fbdev->modes[mode].yres;
	bfin_adv7393_fb_defined.xres_virtual = fbdev->modes[mode].xres;
	bfin_adv7393_fb_defined.yres_virtual = mem * fbdev->modes[mode].yres;
	bfin_adv7393_fb_defined.bits_per_pixel = fbdev->modes[mode].bpp;

	fbdev->info.fbops = &bfin_adv7393_fb_ops;
	fbdev->info.var = bfin_adv7393_fb_defined;
	fbdev->info.fix = bfin_adv7393_fb_fix;
	fbdev->info.par = &bfin_par;
	fbdev->info.flags = FBINFO_DEFAULT;

	fbdev->info.pseudo_palette = kzalloc(sizeof(u32) * 16, GFP_KERNEL);
	if (!fbdev->info.pseudo_palette) {
		dev_err(&client->dev, "failed to allocate pseudo_palette\n");
		ret = -ENOMEM;
		goto free_fb_mem;
	}

	if (fb_alloc_cmap(&fbdev->info.cmap, BFIN_LCD_NBR_PALETTE_ENTRIES, 0) < 0) {
		dev_err(&client->dev, "failed to allocate colormap (%d entries)\n",
			   BFIN_LCD_NBR_PALETTE_ENTRIES);
		ret = -EFAULT;
		goto free_palette;
	}

	if (request_dma(CH_PPI, "BF5xx_PPI_DMA") < 0) {
		dev_err(&client->dev, "unable to request PPI DMA\n");
		ret = -EFAULT;
		goto free_cmap;
	}

	if (request_irq(IRQ_PPI_ERROR, ppi_irq_error, 0,
			"PPI ERROR", fbdev) < 0) {
		dev_err(&client->dev, "unable to request PPI ERROR IRQ\n");
		ret = -EFAULT;
		goto free_ch_ppi;
	}

	fbdev->open = 0;

	ret = adv7393_write_block(client, fbdev->modes[mode].adv7393_i2c_initd,
				fbdev->modes[mode].adv7393_i2c_initd_len);

	if (ret) {
		dev_err(&client->dev, "i2c attach: init error\n");
		goto free_irq_ppi;
	}


	if (register_framebuffer(&fbdev->info) < 0) {
		dev_err(&client->dev, "unable to register framebuffer\n");
		ret = -EFAULT;
		goto free_irq_ppi;
	}

	dev_info(&client->dev, "fb%d: %s frame buffer device\n",
	       fbdev->info.node, fbdev->info.fix.id);
	dev_info(&client->dev, "fb memory address : 0x%p\n", fbdev->fb_mem);

	entry = create_proc_entry("driver/adv7393", 0, NULL);
	if (!entry) {
		dev_err(&client->dev, "unable to create /proc entry\n");
		ret = -EFAULT;
		goto free_fb;
	}

	entry->read_proc = adv7393_read_proc;
	entry->write_proc = adv7393_write_proc;
	entry->data = fbdev;

	return 0;

free_fb:
	unregister_framebuffer(&fbdev->info);
free_irq_ppi:
	free_irq(IRQ_PPI_ERROR, fbdev);
free_ch_ppi:
	free_dma(CH_PPI);
free_cmap:
	fb_dealloc_cmap(&fbdev->info.cmap);
free_palette:
	kfree(fbdev->info.pseudo_palette);
free_fb_mem:
	dma_free_coherent(NULL, fbdev->fb_len, fbdev->fb_mem,
			  fbdev->dma_handle);
free_ppi_pins:
	peripheral_free_list(ppi_pins);
free_gpio:
	if (ANOMALY_05000400)
		gpio_free(P_IDENT(P_PPI0_FS3));
free_fbdev:
	kfree(fbdev);

	return ret;
}

static int bfin_adv7393_fb_open(struct fb_info *info, int user)
{
	struct adv7393fb_device *fbdev = to_adv7393fb_device(info);

	fbdev->info.screen_base = (void *)fbdev->fb_mem;
	if (!fbdev->info.screen_base) {
		dev_err(&fbdev->client->dev, "unable to map device\n");
		return -ENOMEM;
	}

	fbdev->open = 1;
	dma_desc_list(fbdev, BUILD);
	adv7393_mode(fbdev->client, BLANK_OFF);
	bfin_config_ppi(fbdev);
	bfin_config_dma(fbdev);
	bfin_enable_ppi();

	return 0;
}

static int bfin_adv7393_fb_release(struct fb_info *info, int user)
{
	struct adv7393fb_device *fbdev = to_adv7393fb_device(info);

	adv7393_mode(fbdev->client, BLANK_ON);
	bfin_disable_dma();
	bfin_disable_ppi();
	dma_desc_list(fbdev, DESTRUCT);
	fbdev->open = 0;
	return 0;
}

static int
bfin_adv7393_fb_check_var(struct fb_var_screeninfo *var, struct fb_info *info)
{

	switch (var->bits_per_pixel) {
	case 16:/* DIRECTCOLOUR, 64k */
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
		break;
	default:
		pr_debug("%s: depth not supported: %u BPP\n", __func__,
			 var->bits_per_pixel);
		return -EINVAL;
	}

	if (info->var.xres != var->xres ||
	    info->var.yres != var->yres ||
	    info->var.xres_virtual != var->xres_virtual ||
	    info->var.yres_virtual != var->yres_virtual) {
		pr_debug("%s: Resolution not supported: X%u x Y%u\n",
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

static int
bfin_adv7393_fb_pan_display(struct fb_var_screeninfo *var, struct fb_info *info)
{
	int dy;
	u32 dmaaddr;
	struct adv7393fb_device *fbdev = to_adv7393fb_device(info);

	if (!var || !info)
		return -EINVAL;

	if (var->xoffset - info->var.xoffset) {
		/* No support for X panning for now! */
		return -EINVAL;
	}
	dy = var->yoffset - info->var.yoffset;

	if (dy) {
		pr_debug("%s: Panning screen of %d lines\n", __func__, dy);

		dmaaddr = fbdev->av1->start_addr;
		dmaaddr += (info->fix.line_length * dy);
		/* TODO: Wait for current frame to finished */

		fbdev->av1->start_addr = (unsigned long)dmaaddr;
		fbdev->av2->start_addr = (unsigned long)dmaaddr + fbdev->line_len;
	}

	return 0;

}

/* 0 unblank, 1 blank, 2 no vsync, 3 no hsync, 4 off */
static int bfin_adv7393_fb_blank(int blank, struct fb_info *info)
{
	struct adv7393fb_device *fbdev = to_adv7393fb_device(info);

	switch (blank) {

	case VESA_NO_BLANKING:
		/* Turn on panel */
		adv7393_mode(fbdev->client, BLANK_OFF);
		break;

	case VESA_VSYNC_SUSPEND:
	case VESA_HSYNC_SUSPEND:
	case VESA_POWERDOWN:
		/* Turn off panel */
		adv7393_mode(fbdev->client, BLANK_ON);
		break;

	default:
		return -EINVAL;
		break;
	}
	return 0;
}

int bfin_adv7393_fb_cursor(struct fb_info *info, struct fb_cursor *cursor)
{
	if (nocursor)
		return 0;
	else
		return -EINVAL;	/* just to force soft_cursor() call */
}

static int bfin_adv7393_fb_setcolreg(u_int regno, u_int red, u_int green,
				     u_int blue, u_int transp,
				     struct fb_info *info)
{
	if (regno >= BFIN_LCD_NBR_PALETTE_ENTRIES)
		return -EINVAL;

	if (info->var.grayscale)
		/* grayscale = 0.30*R + 0.59*G + 0.11*B */
		red = green = blue = (red * 77 + green * 151 + blue * 28) >> 8;

	if (info->fix.visual == FB_VISUAL_TRUECOLOR) {
		u32 value;
		/* Place color in the pseudopalette */
		if (regno > 16)
			return -EINVAL;

		red   >>= (16 - info->var.red.length);
		green >>= (16 - info->var.green.length);
		blue  >>= (16 - info->var.blue.length);

		value = (red   << info->var.red.offset) |
			(green << info->var.green.offset)|
			(blue  << info->var.blue.offset);
		value &= 0xFFFF;

		((u32 *) (info->pseudo_palette))[regno] = value;
	}

	return 0;
}

static int __devexit bfin_adv7393_fb_remove(struct i2c_client *client)
{
	struct adv7393fb_device *fbdev = i2c_get_clientdata(client);

	adv7393_mode(client, POWER_DOWN);

	if (fbdev->fb_mem)
		dma_free_coherent(NULL, fbdev->fb_len, fbdev->fb_mem, fbdev->dma_handle);
	free_dma(CH_PPI);
	free_irq(IRQ_PPI_ERROR, fbdev);
	unregister_framebuffer(&fbdev->info);
	remove_proc_entry("driver/adv7393", NULL);
	fb_dealloc_cmap(&fbdev->info.cmap);
	kfree(fbdev->info.pseudo_palette);

	if (ANOMALY_05000400)
		gpio_free(P_IDENT(P_PPI0_FS3));	/* FS3 */
	peripheral_free_list(ppi_pins);
	kfree(fbdev);

	return 0;
}

#ifdef CONFIG_PM
static int bfin_adv7393_fb_suspend(struct device *dev)
{
	struct adv7393fb_device *fbdev = dev_get_drvdata(dev);

	if (fbdev->open) {
		bfin_disable_dma();
		bfin_disable_ppi();
		dma_desc_list(fbdev, DESTRUCT);
	}
	adv7393_mode(fbdev->client, POWER_DOWN);

	return 0;
}

static int bfin_adv7393_fb_resume(struct device *dev)
{
	struct adv7393fb_device *fbdev = dev_get_drvdata(dev);

	adv7393_mode(fbdev->client, POWER_ON);

	if (fbdev->open) {
		dma_desc_list(fbdev, BUILD);
		bfin_config_ppi(fbdev);
		bfin_config_dma(fbdev);
		bfin_enable_ppi();
	}

	return 0;
}

static const struct dev_pm_ops bfin_adv7393_dev_pm_ops = {
	.suspend = bfin_adv7393_fb_suspend,
	.resume  = bfin_adv7393_fb_resume,
};
#endif

static const struct i2c_device_id bfin_adv7393_id[] = {
	{DRIVER_NAME, 0},
	{}
};

MODULE_DEVICE_TABLE(i2c, bfin_adv7393_id);

static struct i2c_driver bfin_adv7393_fb_driver = {
	.driver = {
		.name = DRIVER_NAME,
#ifdef CONFIG_PM
		.pm   = &bfin_adv7393_dev_pm_ops,
#endif
	},
	.probe = bfin_adv7393_fb_probe,
	.remove = __devexit_p(bfin_adv7393_fb_remove),
	.id_table = bfin_adv7393_id,
};

static int __init bfin_adv7393_fb_driver_init(void)
{
#if  defined(CONFIG_I2C_BLACKFIN_TWI) || defined(CONFIG_I2C_BLACKFIN_TWI_MODULE)
	request_module("i2c-bfin-twi");
#else
	request_module("i2c-gpio");
#endif

	return i2c_add_driver(&bfin_adv7393_fb_driver);
}
module_init(bfin_adv7393_fb_driver_init);

static void __exit bfin_adv7393_fb_driver_cleanup(void)
{
	i2c_del_driver(&bfin_adv7393_fb_driver);
}
module_exit(bfin_adv7393_fb_driver_cleanup);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Michael Hennerich <hennerich@blackfin.uclinux.org>");
MODULE_DESCRIPTION("Frame buffer driver for ADV7393/2 Video Encoder");

module_param(mode, int, 0);
MODULE_PARM_DESC(mode,
	"Video Mode (0=NTSC,1=PAL,2=NTSC 640x480,3=PAL 640x480,4=NTSC YCbCr input,5=PAL YCbCr input)");

module_param(mem, int, 0);
MODULE_PARM_DESC(mem,
	"Size of frame buffer memory 1=Single 2=Double Size (allows y-panning / frame stacking)");

module_param(nocursor, int, 0644);
MODULE_PARM_DESC(nocursor, "cursor enable/disable");
