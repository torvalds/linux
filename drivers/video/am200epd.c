/*
 * linux/drivers/video/am200epd.c -- Platform device for AM200 EPD kit
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
#include <linux/list.h>
#include <linux/uaccess.h>
#include <linux/irq.h>

#include <video/metronomefb.h>

#include <mach/pxa-regs.h>

/* register offsets for gpio control */
#define LED_GPIO_PIN 51
#define STDBY_GPIO_PIN 48
#define RST_GPIO_PIN 49
#define RDY_GPIO_PIN 32
#define ERR_GPIO_PIN 17
#define PCBPWR_GPIO_PIN 16

#define AF_SEL_GPIO_N 0x3
#define GAFR0_U_OFFSET(pin) ((pin - 16) * 2)
#define GAFR1_L_OFFSET(pin) ((pin - 32) * 2)
#define GAFR1_U_OFFSET(pin) ((pin - 48) * 2)
#define GPDR1_OFFSET(pin) (pin - 32)
#define GPCR1_OFFSET(pin) (pin - 32)
#define GPSR1_OFFSET(pin) (pin - 32)
#define GPCR0_OFFSET(pin) (pin)
#define GPSR0_OFFSET(pin) (pin)

static void am200_set_gpio_output(int pin, int val)
{
	u8 index;

	index = pin >> 4;

	switch (index) {
	case 1:
		if (val)
			GPSR0 |= (1 << GPSR0_OFFSET(pin));
		else
			GPCR0 |= (1 << GPCR0_OFFSET(pin));
		break;
	case 2:
		break;
	case 3:
		if (val)
			GPSR1 |= (1 << GPSR1_OFFSET(pin));
		else
			GPCR1 |= (1 << GPCR1_OFFSET(pin));
		break;
	default:
		printk(KERN_ERR "unimplemented\n");
	}
}

static void __devinit am200_init_gpio_pin(int pin, int dir)
{
	u8 index;
	/* dir 0 is output, 1 is input
	- do 2 things here:
	- set gpio alternate function to standard gpio
	- set gpio direction to input or output  */

	index = pin >> 4;
	switch (index) {
	case 1:
		GAFR0_U &= ~(AF_SEL_GPIO_N << GAFR0_U_OFFSET(pin));

		if (dir)
			GPDR0 &= ~(1 << pin);
		else
			GPDR0 |= (1 << pin);
		break;
	case 2:
		GAFR1_L &= ~(AF_SEL_GPIO_N << GAFR1_L_OFFSET(pin));

		if (dir)
			GPDR1 &= ~(1 << GPDR1_OFFSET(pin));
		else
			GPDR1 |= (1 << GPDR1_OFFSET(pin));
		break;
	case 3:
		GAFR1_U &= ~(AF_SEL_GPIO_N << GAFR1_U_OFFSET(pin));

		if (dir)
			GPDR1 &= ~(1 << GPDR1_OFFSET(pin));
		else
			GPDR1 |= (1 << GPDR1_OFFSET(pin));
		break;
	default:
		printk(KERN_ERR "unimplemented\n");
	}
}

static void am200_init_gpio_regs(struct metronomefb_par *par)
{
	am200_init_gpio_pin(LED_GPIO_PIN, 0);
	am200_set_gpio_output(LED_GPIO_PIN, 0);

	am200_init_gpio_pin(STDBY_GPIO_PIN, 0);
	am200_set_gpio_output(STDBY_GPIO_PIN, 0);

	am200_init_gpio_pin(RST_GPIO_PIN, 0);
	am200_set_gpio_output(RST_GPIO_PIN, 0);

	am200_init_gpio_pin(RDY_GPIO_PIN, 1);

	am200_init_gpio_pin(ERR_GPIO_PIN, 1);

	am200_init_gpio_pin(PCBPWR_GPIO_PIN, 0);
	am200_set_gpio_output(PCBPWR_GPIO_PIN, 0);
}

static void am200_disable_lcd_controller(struct metronomefb_par *par)
{
	LCSR = 0xffffffff;	/* Clear LCD Status Register */
	LCCR0 |= LCCR0_DIS;	/* Disable LCD Controller */

	/* we reset and just wait for things to settle */
	msleep(200);
}

static void am200_enable_lcd_controller(struct metronomefb_par *par)
{
	LCSR = 0xffffffff;
	FDADR0 = par->metromem_desc_dma;
	LCCR0 |= LCCR0_ENB;
}

static void am200_init_lcdc_regs(struct metronomefb_par *par)
{
	/* here we do:
	- disable the lcd controller
	- setup lcd control registers
	- setup dma descriptor
	- reenable lcd controller
	*/

	/* disable the lcd controller */
	am200_disable_lcd_controller(par);

	/* setup lcd control registers */
	LCCR0 = LCCR0_LDM | LCCR0_SFM | LCCR0_IUM | LCCR0_EFM | LCCR0_PAS
		| LCCR0_QDM | LCCR0_BM | LCCR0_OUM;

	LCCR1 = (par->info->var.xres/2 - 1) /* pixels per line */
		| (27 << 10) /* hsync pulse width - 1 */
		| (33 << 16) /* eol pixel count */
		| (33 << 24); /* bol pixel count */

	LCCR2 = (par->info->var.yres - 1) /* lines per panel */
		| (24 << 10) /* vsync pulse width - 1 */
		| (2 << 16) /* eof pixel count */
		| (0 << 24); /* bof pixel count */

	LCCR3 = 2 /* pixel clock divisor */
		| (24 << 8) /* AC Bias pin freq */
		| LCCR3_16BPP /* BPP */
		| LCCR3_PCP;  /* PCP falling edge */

}

static void am200_post_dma_setup(struct metronomefb_par *par)
{
	par->metromem_desc->mFDADR0 = par->metromem_desc_dma;
	par->metromem_desc->mFSADR0 = par->metromem_dma;
	par->metromem_desc->mFIDR0 = 0;
	par->metromem_desc->mLDCMD0 = par->info->var.xres
					* par->info->var.yres;
	am200_enable_lcd_controller(par);
}

static void am200_free_irq(struct fb_info *info)
{
	free_irq(IRQ_GPIO(RDY_GPIO_PIN), info);
}

static irqreturn_t am200_handle_irq(int irq, void *dev_id)
{
	struct fb_info *info = dev_id;
	struct metronomefb_par *par = info->par;

	wake_up_interruptible(&par->waitq);
	return IRQ_HANDLED;
}

static int am200_setup_irq(struct fb_info *info)
{
	int retval;

	retval = request_irq(IRQ_GPIO(RDY_GPIO_PIN), am200_handle_irq,
				IRQF_DISABLED, "AM200", info);
	if (retval) {
		printk(KERN_ERR "am200epd: request_irq failed: %d\n", retval);
		return retval;
	}

	return set_irq_type(IRQ_GPIO(RDY_GPIO_PIN), IRQ_TYPE_EDGE_FALLING);
}

static void am200_set_rst(struct metronomefb_par *par, int state)
{
	am200_set_gpio_output(RST_GPIO_PIN, state);
}

static void am200_set_stdby(struct metronomefb_par *par, int state)
{
	am200_set_gpio_output(STDBY_GPIO_PIN, state);
}

static int am200_wait_event(struct metronomefb_par *par)
{
	return wait_event_timeout(par->waitq, (GPLR1 & 0x01), HZ);
}

static int am200_wait_event_intr(struct metronomefb_par *par)
{
	return wait_event_interruptible_timeout(par->waitq, (GPLR1 & 0x01), HZ);
}

static struct metronome_board am200_board = {
	.owner			= THIS_MODULE,
	.free_irq		= am200_free_irq,
	.setup_irq		= am200_setup_irq,
	.init_gpio_regs		= am200_init_gpio_regs,
	.init_lcdc_regs		= am200_init_lcdc_regs,
	.post_dma_setup		= am200_post_dma_setup,
	.set_rst		= am200_set_rst,
	.set_stdby		= am200_set_stdby,
	.met_wait_event		= am200_wait_event,
	.met_wait_event_intr	= am200_wait_event_intr,
};

static struct platform_device *am200_device;

static int __init am200_init(void)
{
	int ret;

	/* request our platform independent driver */
	request_module("metronomefb");

	am200_device = platform_device_alloc("metronomefb", -1);
	if (!am200_device)
		return -ENOMEM;

	platform_device_add_data(am200_device, &am200_board,
					sizeof(am200_board));

	/* this _add binds metronomefb to am200. metronomefb refcounts am200 */
	ret = platform_device_add(am200_device);

	if (ret)
		platform_device_put(am200_device);

	return ret;
}

static void __exit am200_exit(void)
{
	platform_device_unregister(am200_device);
}

module_init(am200_init);
module_exit(am200_exit);

MODULE_DESCRIPTION("board driver for am200 metronome epd kit");
MODULE_AUTHOR("Jaya Kumar");
MODULE_LICENSE("GPL");
