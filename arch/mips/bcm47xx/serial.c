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

#ifdef CONFIG_BCM47XX_SSB
static int __init uart8250_init_ssb(void)
{
	int i;
	struct ssb_mipscore *mcore = &(bcm47xx_bus.ssb.mipscore);

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
#endif

#ifdef CONFIG_BCM47XX_BCMA
static int __init uart8250_init_bcma(void)
{
	int i;
	struct bcma_drv_cc *cc = &(bcm47xx_bus.bcma.bus.drv_cc);

	memset(&uart8250_data, 0,  sizeof(uart8250_data));

	for (i = 0; i < cc->nr_serial_ports; i++) {
		struct plat_serial8250_port *p = &(uart8250_data[i]);
		struct bcma_serial_port *bcma_port;
		bcma_port = &(cc->serial_ports[i]);

		p->mapbase = (unsigned int) bcma_port->regs;
		p->membase = (void *) bcma_port->regs;
		p->irq = bcma_port->irq + 2;
		p->uartclk = bcma_port->baud_base;
		p->regshift = bcma_port->reg_shift;
		p->iotype = UPIO_MEM;
		p->flags = UPF_BOOT_AUTOCONF | UPF_SHARE_IRQ;
	}
	return platform_device_register(&uart8250_device);
}
#endif

static int __init uart8250_init(void)
{
	switch (bcm47xx_bus_type) {
#ifdef CONFIG_BCM47XX_SSB
	case BCM47XX_BUS_TYPE_SSB:
		return uart8250_init_ssb();
#endif
#ifdef CONFIG_BCM47XX_BCMA
	case BCM47XX_BUS_TYPE_BCMA:
		return uart8250_init_bcma();
#endif
	}
	return -EINVAL;
}

module_init(uart8250_init);

MODULE_AUTHOR("Aurelien Jarno <aurelien@aurel32.net>");
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("8250 UART probe driver for the BCM47XX platforms");
