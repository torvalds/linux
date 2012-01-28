/*
 * am200epd.c -- Platform device for AM200 EPD kit
 *
 * Copyright (C) 2008, Jaya Kumar
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License. See the file COPYING in the main directory of this archive for
 * more details.
 *
 * Layout is based on skeletonfb.c by James Simmons and Geert Uytterhoeven.
 *
 * This work was made possible by help and equipment support from E-Ink
 * Corporation. http://support.eink.com/community
 *
 * This driver is written to be used with the Metronome display controller.
 * on the AM200 EPD prototype kit/development kit with an E-Ink 800x600
 * Vizplex EPD on a Gumstix board using the Lyre interface board.
 *
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/string.h>
#include <linux/delay.h>
#include <linux/interrupt.h>
#include <linux/fb.h>
#include <linux/init.h>
#include <linux/platform_device.h>
#include <linux/irq.h>
#include <linux/gpio.h>

#include <mach/pxa25x.h>
#include <mach/gumstix.h>
#include <mach/pxafb.h>

#include "generic.h"

#include <video/metronomefb.h>

static unsigned int panel_type = 6;
static struct platform_device *am200_device;
static struct metronome_board am200_board;

static struct pxafb_mode_info am200_fb_mode_9inch7 = {
	.pixclock	= 40000,
	.xres		= 1200,
	.yres		= 842,
	.bpp		= 16,
	.hsync_len	= 2,
	.left_margin	= 2,
	.right_margin	= 2,
	.vsync_len	= 1,
	.upper_margin	= 2,
	.lower_margin	= 25,
	.sync		= FB_SYNC_HOR_HIGH_ACT | FB_SYNC_VERT_HIGH_ACT,
};

static struct pxafb_mode_info am200_fb_mode_8inch = {
	.pixclock	= 40000,
	.xres		= 1088,
	.yres		= 791,
	.bpp		= 16,
	.hsync_len	= 28,
	.left_margin	= 8,
	.right_margin	= 30,
	.vsync_len	= 8,
	.upper_margin	= 10,
	.lower_margin	= 8,
	.sync		= FB_SYNC_HOR_HIGH_ACT | FB_SYNC_VERT_HIGH_ACT,
};

static struct pxafb_mode_info am200_fb_mode_6inch = {
	.pixclock	= 40189,
	.xres		= 832,
	.yres		= 622,
	.bpp		= 16,
	.hsync_len	= 28,
	.left_margin	= 34,
	.right_margin	= 34,
	.vsync_len	= 25,
	.upper_margin	= 0,
	.lower_margin	= 2,
	.sync		= FB_SYNC_HOR_HIGH_ACT | FB_SYNC_VERT_HIGH_ACT,
};

static struct pxafb_mach_info am200_fb_info = {
	.modes		= &am200_fb_mode_6inch,
	.num_modes	= 1,
	.lcd_conn	= LCD_TYPE_COLOR_TFT | LCD_PCLK_EDGE_FALL |
			  LCD_AC_BIAS_FREQ(24),
};

/* register offsets for gpio control */
#define LED_GPIO_PIN 51
#define STDBY_GPIO_PIN 48
#define RST_GPIO_PIN 49
#define RDY_GPIO_PIN 32
#define ERR_GPIO_PIN 17
#define PCBPWR_GPIO_PIN 16
static int gpios[] = { LED_GPIO_PIN , STDBY_GPIO_PIN , RST_GPIO_PIN,
			RDY_GPIO_PIN, ERR_GPIO_PIN, PCBPWR_GPIO_PIN };
static char *gpio_names[] = { "LED" , "STDBY" , "RST", "RDY", "ERR", "PCBPWR" };

static int am200_init_gpio_regs(struct metronomefb_par *par)
{
	int i;
	int err;

	for (i = 0; i < ARRAY_SIZE(gpios); i++) {
		err = gpio_request(gpios[i], gpio_names[i]);
		if (err) {
			dev_err(&am200_device->dev, "failed requesting "
				"gpio %s, err=%d\n", gpio_names[i], err);
			goto err_req_gpio;
		}
	}

	gpio_direction_output(LED_GPIO_PIN, 0);
	gpio_direction_output(STDBY_GPIO_PIN, 0);
	gpio_direction_output(RST_GPIO_PIN, 0);

	gpio_direction_input(RDY_GPIO_PIN);
	gpio_direction_input(ERR_GPIO_PIN);

	gpio_direction_output(PCBPWR_GPIO_PIN, 0);

	return 0;

err_req_gpio:
	while (--i >= 0)
		gpio_free(gpios[i]);

	return err;
}

static void am200_cleanup(struct metronomefb_par *par)
{
	int i;

	free_irq(PXA_GPIO_TO_IRQ(RDY_GPIO_PIN), par);

	for (i = 0; i < ARRAY_SIZE(gpios); i++)
		gpio_free(gpios[i]);
}

static int am200_share_video_mem(struct fb_info *info)
{
	/* rough check if this is our desired fb and not something else */
	if ((info->var.xres != am200_fb_info.modes->xres)
		|| (info->var.yres != am200_fb_info.modes->yres))
		return 0;

	/* we've now been notified that we have our new fb */
	am200_board.metromem = info->screen_base;
	am200_board.host_fbinfo = info;

	/* try to refcount host drv since we are the consumer after this */
	if (!try_module_get(info->fbops->owner))
		return -ENODEV;

	return 0;
}

static int am200_unshare_video_mem(struct fb_info *info)
{
	dev_dbg(&am200_device->dev, "ENTER %s\n", __func__);

	if (info != am200_board.host_fbinfo)
		return 0;

	module_put(am200_board.host_fbinfo->fbops->owner);
	return 0;
}

static int am200_fb_notifier_callback(struct notifier_block *self,
				 unsigned long event, void *data)
{
	struct fb_event *evdata = data;
	struct fb_info *info = evdata->info;

	dev_dbg(&am200_device->dev, "ENTER %s\n", __func__);

	if (event == FB_EVENT_FB_REGISTERED)
		return am200_share_video_mem(info);
	else if (event == FB_EVENT_FB_UNREGISTERED)
		return am200_unshare_video_mem(info);

	return 0;
}

static struct notifier_block am200_fb_notif = {
	.notifier_call = am200_fb_notifier_callback,
};

/* this gets called as part of our init. these steps must be done now so
 * that we can use pxa_set_fb_info */
static void __init am200_presetup_fb(void)
{
	int fw;
	int fh;
	int padding_size;
	int totalsize;

	switch (panel_type) {
	case 6:
		am200_fb_info.modes = &am200_fb_mode_6inch;
		break;
	case 8:
		am200_fb_info.modes = &am200_fb_mode_8inch;
		break;
	case 97:
		am200_fb_info.modes = &am200_fb_mode_9inch7;
		break;
	default:
		dev_err(&am200_device->dev, "invalid panel_type selection,"
						" setting to 6\n");
		am200_fb_info.modes = &am200_fb_mode_6inch;
		break;
	}

	/* the frame buffer is divided as follows:
	command | CRC | padding
	16kb waveform data | CRC | padding
	image data | CRC
	*/

	fw = am200_fb_info.modes->xres;
	fh = am200_fb_info.modes->yres;

	/* waveform must be 16k + 2 for checksum */
	am200_board.wfm_size = roundup(16*1024 + 2, fw);

	padding_size = PAGE_SIZE + (4 * fw);

	/* total is 1 cmd , 1 wfm, padding and image */
	totalsize = fw + am200_board.wfm_size + padding_size + (fw*fh);

	/* save this off because we're manipulating fw after this and
	 * we'll need it when we're ready to setup the framebuffer */
	am200_board.fw = fw;
	am200_board.fh = fh;

	/* the reason we do this adjustment is because we want to acquire
	 * more framebuffer memory without imposing custom awareness on the
	 * underlying pxafb driver */
	am200_fb_info.modes->yres = DIV_ROUND_UP(totalsize, fw);

	/* we divide since we told the LCD controller we're 16bpp */
	am200_fb_info.modes->xres /= 2;

	pxa_set_fb_info(NULL, &am200_fb_info);

}

/* this gets called by metronomefb as part of its init, in our case, we
 * have already completed initial framebuffer init in presetup_fb so we
 * can just setup the fb access pointers */
static int am200_setup_fb(struct metronomefb_par *par)
{
	int fw;
	int fh;

	fw = am200_board.fw;
	fh = am200_board.fh;

	/* metromem was set up by the notifier in share_video_mem so now
	 * we can use its value to calculate the other entries */
	par->metromem_cmd = (struct metromem_cmd *) am200_board.metromem;
	par->metromem_wfm = am200_board.metromem + fw;
	par->metromem_img = par->metromem_wfm + am200_board.wfm_size;
	par->metromem_img_csum = (u16 *) (par->metromem_img + (fw * fh));
	par->metromem_dma = am200_board.host_fbinfo->fix.smem_start;

	return 0;
}

static int am200_get_panel_type(void)
{
	return panel_type;
}

static irqreturn_t am200_handle_irq(int irq, void *dev_id)
{
	struct metronomefb_par *par = dev_id;

	wake_up_interruptible(&par->waitq);
	return IRQ_HANDLED;
}

static int am200_setup_irq(struct fb_info *info)
{
	int ret;

	ret = request_irq(PXA_GPIO_TO_IRQ(RDY_GPIO_PIN), am200_handle_irq,
				IRQF_DISABLED|IRQF_TRIGGER_FALLING,
				"AM200", info->par);
	if (ret)
		dev_err(&am200_device->dev, "request_irq failed: %d\n", ret);

	return ret;
}

static void am200_set_rst(struct metronomefb_par *par, int state)
{
	gpio_set_value(RST_GPIO_PIN, state);
}

static void am200_set_stdby(struct metronomefb_par *par, int state)
{
	gpio_set_value(STDBY_GPIO_PIN, state);
}

static int am200_wait_event(struct metronomefb_par *par)
{
	return wait_event_timeout(par->waitq, gpio_get_value(RDY_GPIO_PIN), HZ);
}

static int am200_wait_event_intr(struct metronomefb_par *par)
{
	return wait_event_interruptible_timeout(par->waitq,
					gpio_get_value(RDY_GPIO_PIN), HZ);
}

static struct metronome_board am200_board = {
	.owner			= THIS_MODULE,
	.setup_irq		= am200_setup_irq,
	.setup_io		= am200_init_gpio_regs,
	.setup_fb		= am200_setup_fb,
	.set_rst		= am200_set_rst,
	.set_stdby		= am200_set_stdby,
	.met_wait_event		= am200_wait_event,
	.met_wait_event_intr	= am200_wait_event_intr,
	.get_panel_type		= am200_get_panel_type,
	.cleanup		= am200_cleanup,
};

static unsigned long am200_pin_config[] __initdata = {
	GPIO51_GPIO,
	GPIO49_GPIO,
	GPIO48_GPIO,
	GPIO32_GPIO,
	GPIO17_GPIO,
	GPIO16_GPIO,
};

int __init am200_init(void)
{
	int ret;

	/* before anything else, we request notification for any fb
	 * creation events */
	fb_register_client(&am200_fb_notif);

	pxa2xx_mfp_config(ARRAY_AND_SIZE(am200_pin_config));

	/* request our platform independent driver */
	request_module("metronomefb");

	am200_device = platform_device_alloc("metronomefb", -1);
	if (!am200_device)
		return -ENOMEM;

	/* the am200_board that will be seen by metronomefb is a copy */
	platform_device_add_data(am200_device, &am200_board,
					sizeof(am200_board));

	/* this _add binds metronomefb to am200. metronomefb refcounts am200 */
	ret = platform_device_add(am200_device);

	if (ret) {
		platform_device_put(am200_device);
		fb_unregister_client(&am200_fb_notif);
		return ret;
	}

	am200_presetup_fb();

	return 0;
}

module_param(panel_type, uint, 0);
MODULE_PARM_DESC(panel_type, "Select the panel type: 6, 8, 97");

MODULE_DESCRIPTION("board driver for am200 metronome epd kit");
MODULE_AUTHOR("Jaya Kumar");
MODULE_LICENSE("GPL");
