/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 2007 Ralf Baechle (ralf@linux-mips.org)
 */
#include <linux/module.h>
#include <linux/init.h>
#include <linux/platform_device.h>
#include <linux/serial_8250.h>

#include <asm/ip32/mace.h>
#include <asm/ip32/ip32_ints.h>

/*
 * .iobase isn't a constant (in the sense of C) so we fill it in at runtime.
 */
#define MACE_PORT(int)							\
{									\
	.irq		= int,						\
	.uartclk	= 1843200,					\
	.iotype		= UPIO_MEM,					\
	.flags		= UPF_SKIP_TEST,				\
	.regshift	= 8,						\
}

static struct plat_serial8250_port uart8250_data[] = {
	MACE_PORT(MACEISA_SERIAL1_IRQ),
	MACE_PORT(MACEISA_SERIAL2_IRQ),
	{ },
};

static struct platform_device uart8250_device = {
	.name			= "serial8250",
	.id			= PLAT8250_DEV_PLATFORM,
	.dev			= {
		.platform_data	= uart8250_data,
	},
};

static int __init uart8250_init(void)
{
	uart8250_data[0].iobase = (unsigned long) &mace->isa.serial1;
	uart8250_data[1].iobase = (unsigned long) &mace->isa.serial1;

	return platform_device_register(&uart8250_device);
}

device_initcall(uart8250_init);

static __init int meth_devinit(void)
{
	struct platform_device *pd;
	int ret;

	pd = platform_device_alloc("meth", -1);
	if (!pd)
		return -ENOMEM;

	ret = platform_device_add(pd);
	if (ret)
		platform_device_put(pd);

	return ret;
}

device_initcall(meth_devinit);

MODULE_AUTHOR("Ralf Baechle <ralf@linux-mips.org>");
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("8250 UART probe driver for SGI IP32 aka O2");
