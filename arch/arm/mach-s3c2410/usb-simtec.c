/* linux/arch/arm/mach-s3c2410/usb-simtec.c
 *
 * Copyright (c) 2004,2005 Simtec Electronics
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
#include <linux/timer.h>
#include <linux/init.h>
#include <linux/device.h>

#include <asm/mach/arch.h>
#include <asm/mach/map.h>
#include <asm/mach/irq.h>

#include <mach/bast-map.h>
#include <mach/bast-irq.h>
#include <mach/usb-control.h>
#include <mach/regs-gpio.h>

#include <mach/hardware.h>
#include <asm/io.h>
#include <asm/irq.h>

#include <plat/devs.h>
#include "usb-simtec.h"

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
		s3c2410_gpio_setpin(S3C2410_GPB4, 0);
	else
		s3c2410_gpio_setpin(S3C2410_GPB4, 1);
}

static irqreturn_t
usb_simtec_ocirq(int irq, void *pw)
{
	struct s3c2410_hcd_info *info = pw;

	if (s3c2410_gpio_getpin(S3C2410_GPG10) == 0) {
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
		ret = request_irq(IRQ_USBOC, usb_simtec_ocirq,
				  IRQF_DISABLED | IRQF_TRIGGER_RISING |
				   IRQF_TRIGGER_FALLING,
				  "USB Over-current", info);
		if (ret != 0) {
			printk(KERN_ERR "failed to request usb oc irq\n");
		}
	} else {
		free_irq(IRQ_USBOC, info);
	}
}

static struct s3c2410_hcd_info usb_simtec_info = {
	.port[0]	= {
		.flags	= S3C_HCDFLG_USED
	},
	.port[1]	= {
		.flags	= S3C_HCDFLG_USED
	},

	.power_control	= usb_simtec_powercontrol,
	.enable_oc	= usb_simtec_enableoc,
};


int usb_simtec_init(void)
{
	printk("USB Power Control, (c) 2004 Simtec Electronics\n");
	s3c_device_usb.dev.platform_data = &usb_simtec_info;

	s3c2410_gpio_cfgpin(S3C2410_GPB4, S3C2410_GPB4_OUTP);
	s3c2410_gpio_setpin(S3C2410_GPB4, 1);
	return 0;
}
