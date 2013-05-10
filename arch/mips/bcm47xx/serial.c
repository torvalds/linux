/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 2007 Aurelien Jarno <aurelien@aurel32.net>
 */

#include <linux/module.h>
#include <linux/init.h>
#include <linux/serial.h>
#include <linux/serial_8250.h>
#include <linux/ssb/ssb.h>
#include <bcm47xx.h>

static struct plat_serial8250_port uart8250_data[5];

static struct platform_device uart8250_device = {
	.name			= "serial8250",
	.id			= PLAT8250_DEV_PLATFORM,
	.dev			= {
		.platform_data	= uart8250_data,
	},
};

static int __init uart8250_init(void)
{
	int i;
	struct ssb_mipscore *mcore = &(ssb_bcm47xx.mipscore);

	memset(&uart8250_data, 0,  sizeof(uart8250_data));

	for (i = 0; i < mcore->nr_serial_ports; i++) {
		struct plat_serial8250_port *p = &(uart8250_data[i]);
		struct ssb_serial_port *ssb_port = &(mcore->serial_ports[i]);

		p->mapbase = (unsigned int) ssb_port->regs;
		p->membase = (void *) ssb_port->regs;
		p->irq = ssb_port->irq + 2;
		p->uartclk = ssb_port->baud_base;
		p->regshift = ssb_port->reg_shift;
		p->iotype = UPIO_MEM;
		p->flags = UPF_BOOT_AUTOCONF | UPF_SHARE_IRQ;
	}
	return platform_device_register(&uart8250_device);
}

module_init(uart8250_init);

MODULE_AUTHOR("Aurelien Jarno <aurelien@aurel32.net>");
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("8250 UART probe driver for the BCM47XX platforms");
