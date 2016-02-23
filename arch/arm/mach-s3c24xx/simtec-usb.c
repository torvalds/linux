/* linux/arch/arm/mach-s3c2410/usb-simtec.c
 *
 * Copyright 2004-2005 Simtec Electronics
 *   Ben Dooks <ben@simtec.co.uk>
 *
 * http://www.simtec.co.uk/products/EB2410ITX/
 *
 * Simtec BAST and Thorcom VR1000 USB port support functions
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
*/

#define DEBUG

#include <linux/kernel.h>
#include <linux/types.h>
#include <linux/interrupt.h>
#include <linux/list.h>
#include <linux/gpio.h>
#include <linux/timer.h>
#include <linux/init.h>
#include <linux/device.h>
#include <linux/io.h>

#include <asm/mach/arch.h>
#include <asm/mach/map.h>
#include <asm/mach/irq.h>

#include <mach/hardware.h>
#include <mach/gpio-samsung.h>
#include <asm/irq.h>

#include <linux/platform_data/usb-ohci-s3c2410.h>
#include <plat/devs.h>

#include "bast.h"
#include "simtec.h"

/* control power and monitor over-current events on various Simtec
 * designed boards.
*/

static unsigned int power_state[2];

static void
usb_simtec_powercontrol(int port, int to)
{
	pr_debug("usb_simtec_powercontrol(%d,%d)\n", port, to);

	power_state[port] = to;

	if (power_state[0] && power_state[1])
		gpio_set_value(S3C2410_GPB(4), 0);
	else
		gpio_set_value(S3C2410_GPB(4), 1);
}

static irqreturn_t
usb_simtec_ocirq(int irq, void *pw)
{
	struct s3c2410_hcd_info *info = pw;

	if (gpio_get_value(S3C2410_GPG(10)) == 0) {
		pr_debug("usb_simtec: over-current irq (oc detected)\n");
		s3c2410_usb_report_oc(info, 3);
	} else {
		pr_debug("usb_simtec: over-current irq (oc cleared)\n");
		s3c2410_usb_report_oc(info, 0);
	}

	return IRQ_HANDLED;
}

static void usb_simtec_enableoc(struct s3c2410_hcd_info *info, int on)
{
	int ret;

	if (on) {
		ret = request_irq(BAST_IRQ_USBOC, usb_simtec_ocirq,
				  IRQF_TRIGGER_RISING | IRQF_TRIGGER_FALLING,
				  "USB Over-current", info);
		if (ret != 0) {
			printk(KERN_ERR "failed to request usb oc irq\n");
		}
	} else {
		free_irq(BAST_IRQ_USBOC, info);
	}
}

static struct s3c2410_hcd_info usb_simtec_info __initdata = {
	.port[0]	= {
		.flags	= S3C_HCDFLG_USED
	},
	.port[1]	= {
		.flags	= S3C_HCDFLG_USED
	},

	.power_control	= usb_simtec_powercontrol,
	.enable_oc	= usb_simtec_enableoc,
};


int __init usb_simtec_init(void)
{
	int ret;

	printk("USB Power Control, Copyright 2004 Simtec Electronics\n");

	ret = gpio_request(S3C2410_GPB(4), "USB power control");
	if (ret < 0) {
		pr_err("%s: failed to get GPB4\n", __func__);
		return ret;
	}

	ret = gpio_request(S3C2410_GPG(10), "USB overcurrent");
	if (ret < 0) {
		pr_err("%s: failed to get GPG10\n", __func__);
		gpio_free(S3C2410_GPB(4));
		return ret;
	}

	/* turn power on */
	gpio_direction_output(S3C2410_GPB(4), 1);
	gpio_direction_input(S3C2410_GPG(10));

	s3c_ohci_set_platdata(&usb_simtec_info);
	return 0;
}
