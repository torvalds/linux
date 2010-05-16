/*
 * am300epd.c -- Platform device for AM300 EPD kit
 *
 * Copyright (C) 2008, Jaya Kumar
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License. See the file COPYING in the main directory of this archive for
 * more details.
 *
 * This work was made possible by help and equipment support from E-Ink
 * Corporation. http://support.eink.com/community
 *
 * This driver is written to be used with the Broadsheet display controller.
 * on the AM300 EPD prototype kit/development kit with an E-Ink 800x600
 * Vizplex EPD on a Gumstix board using the Broadsheet interface board.
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

#include <mach/gumstix.h>
#include <mach/mfp-pxa25x.h>
#include <mach/pxafb.h>

#include "generic.h"

#include <video/broadsheetfb.h>

static unsigned int panel_type = 6;
static struct platform_device *am300_device;
static struct broadsheet_board am300_board;

static unsigned long am300_pin_config[] __initdata = {
	GPIO16_GPIO,
	GPIO17_GPIO,
	GPIO32_GPIO,
	GPIO48_GPIO,
	GPIO49_GPIO,
	GPIO51_GPIO,
	GPIO74_GPIO,
	GPIO75_GPIO,
	GPIO76_GPIO,
	GPIO77_GPIO,

	/* this is the 16-bit hdb bus 58-73 */
	GPIO58_GPIO,
	GPIO59_GPIO,
	GPIO60_GPIO,
	GPIO61_GPIO,

	GPIO62_GPIO,
	GPIO63_GPIO,
	GPIO64_GPIO,
	GPIO65_GPIO,

	GPIO66_GPIO,
	GPIO67_GPIO,
	GPIO68_GPIO,
	GPIO69_GPIO,

	GPIO70_GPIO,
	GPIO71_GPIO,
	GPIO72_GPIO,
	GPIO73_GPIO,
};

/* register offsets for gpio control */
#define PWR_GPIO_PIN	16
#define CFG_GPIO_PIN	17
#define RDY_GPIO_PIN	32
#define DC_GPIO_PIN	48
#define RST_GPIO_PIN	49
#define LED_GPIO_PIN	51
#define RD_GPIO_PIN	74
#define WR_GPIO_PIN	75
#define CS_GPIO_PIN	76
#define IRQ_GPIO_PIN	77

/* hdb bus */
#define DB0_GPIO_PIN	58
#define DB15_GPIO_PIN	73

static int gpios[] = { PWR_GPIO_PIN, CFG_GPIO_PIN, RDY_GPIO_PIN, DC_GPIO_PIN,
			RST_GPIO_PIN, RD_GPIO_PIN, WR_GPIO_PIN, CS_GPIO_PIN,
			IRQ_GPIO_PIN, LED_GPIO_PIN };
static char *gpio_names[] = { "PWR", "CFG", "RDY", "DC", "RST", "RD", "WR",
				"CS", "IRQ", "LED" };

static int am300_wait_event(struct broadsheetfb_par *par)
{
	/* todo: improve err recovery */
	wait_event(par->waitq, gpio_get_value(RDY_GPIO_PIN));
	return 0;
}

static int am300_init_gpio_regs(struct broadsheetfb_par *par)
{
	int i;
	int err;
	char dbname[8];

	for (i = 0; i < ARRAY_SIZE(gpios); i++) {
		err = gpio_request(gpios[i], gpio_names[i]);
		if (err) {
			dev_err(&am300_device->dev, "failed requesting "
				"gpio %s, err=%d\n", gpio_names[i], err);
			goto err_req_gpio;
		}
	}

	/* we also need to take care of the hdb bus */
	for (i = DB0_GPIO_PIN; i <= DB15_GPIO_PIN; i++) {
		sprintf(dbname, "DB%d", i);
		err = gpio_request(i, dbname);
		if (err) {
			dev_err(&am300_device->dev, "failed requesting "
				"gpio %d, err=%d\n", i, err);
			while (i >= DB0_GPIO_PIN)
				gpio_free(i--);
			i = ARRAY_SIZE(gpios) - 1;
			goto err_req_gpio;
		}
	}

	/* setup the outputs and init values */
	gpio_direction_output(PWR_GPIO_PIN, 0);
	gpio_direction_output(CFG_GPIO_PIN, 1);
	gpio_direction_output(DC_GPIO_PIN, 0);
	gpio_direction_output(RD_GPIO_PIN, 1);
	gpio_direction_output(WR_GPIO_PIN, 1);
	gpio_direction_output(CS_GPIO_PIN, 1);
	gpio_direction_output(RST_GPIO_PIN, 0);

	/* setup the inputs */
	gpio_direction_input(RDY_GPIO_PIN);
	gpio_direction_input(IRQ_GPIO_PIN);

	/* start the hdb bus as an input */
	for (i = DB0_GPIO_PIN; i <= DB15_GPIO_PIN; i++)
		gpio_direction_output(i, 0);

	/* go into command mode */
	gpio_set_value(CFG_GPIO_PIN, 1);
	gpio_set_value(RST_GPIO_PIN, 0);
	msleep(10);
	gpio_set_value(RST_GPIO_PIN, 1);
	msleep(10);
	am300_wait_event(par);

	return 0;

err_req_gpio:
	while (i > 0)
		gpio_free(gpios[i--]);

	return err;
}

static int am300_init_board(struct broadsheetfb_par *par)
{
	return am300_init_gpio_regs(par);
}

static void am300_cleanup(struct broadsheetfb_par *par)
{
	int i;

	free_irq(IRQ_GPIO(RDY_GPIO_PIN), par);

	for (i = 0; i < ARRAY_SIZE(gpios); i++)
		gpio_free(gpios[i]);

	for (i = DB0_GPIO_PIN; i <= DB15_GPIO_PIN; i++)
		gpio_free(i);

}

static u16 am300_get_hdb(struct broadsheetfb_par *par)
{
	u16 res = 0;
	int i;

	for (i = 0; i <= (DB15_GPIO_PIN - DB0_GPIO_PIN) ; i++)
		res |= (gpio_get_value(DB0_GPIO_PIN + i)) ? (1 << i) : 0;

	return res;
}

static void am300_set_hdb(struct broadsheetfb_par *par, u16 data)
{
	int i;

	for (i = 0; i <= (DB15_GPIO_PIN - DB0_GPIO_PIN) ; i++)
		gpio_set_value(DB0_GPIO_PIN + i, (data >> i) & 0x01);
}


static void am300_set_ctl(struct broadsheetfb_par *par, unsigned char bit,
				u8 state)
{
	switch (bit) {
	case BS_CS:
		gpio_set_value(CS_GPIO_PIN, state);
		break;
	case BS_DC:
		gpio_set_value(DC_GPIO_PIN, state);
		break;
	case BS_WR:
		gpio_set_value(WR_GPIO_PIN, state);
		break;
	}
}

static int am300_get_panel_type(void)
{
	return panel_type;
}

static irqreturn_t am300_handle_irq(int irq, void *dev_id)
{
	struct broadsheetfb_par *par = dev_id;

	wake_up(&par->waitq);
	return IRQ_HANDLED;
}

static int am300_setup_irq(struct fb_info *info)
{
	int ret;
	struct broadsheetfb_par *par = info->par;

	ret = request_irq(IRQ_GPIO(RDY_GPIO_PIN), am300_handle_irq,
				IRQF_DISABLED|IRQF_TRIGGER_RISING,
				"AM300", par);
	if (ret)
		dev_err(&am300_device->dev, "request_irq failed: %d\n", ret);

	return ret;
}

static struct broadsheet_board am300_board = {
	.owner			= THIS_MODULE,
	.init			= am300_init_board,
	.cleanup		= am300_cleanup,
	.set_hdb		= am300_set_hdb,
	.get_hdb		= am300_get_hdb,
	.set_ctl		= am300_set_ctl,
	.wait_for_rdy		= am300_wait_event,
	.get_panel_type		= am300_get_panel_type,
	.setup_irq		= am300_setup_irq,
};

int __init am300_init(void)
{
	int ret;

	pxa2xx_mfp_config(ARRAY_AND_SIZE(am300_pin_config));

	/* request our platform independent driver */
	request_module("broadsheetfb");

	am300_device = platform_device_alloc("broadsheetfb", -1);
	if (!am300_device)
		return -ENOMEM;

	/* the am300_board that will be seen by broadsheetfb is a copy */
	platform_device_add_data(am300_device, &am300_board,
					sizeof(am300_board));

	ret = platform_device_add(am300_device);

	if (ret) {
		platform_device_put(am300_device);
		return ret;
	}

	return 0;
}

module_param(panel_type, uint, 0);
MODULE_PARM_DESC(panel_type, "Select the panel type: 37, 6, 97");

MODULE_DESCRIPTION("board driver for am300 epd kit");
MODULE_AUTHOR("Jaya Kumar");
MODULE_LICENSE("GPL");
