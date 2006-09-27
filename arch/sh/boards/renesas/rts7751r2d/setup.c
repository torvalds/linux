/*
 * Renesas Technology Sales RTS7751R2D Support.
 *
 * Copyright (C) 2002 Atom Create Engineering Co., Ltd.
 * Copyright (C) 2004 - 2006 Paul Mundt
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 */
#include <linux/init.h>
#include <linux/platform_device.h>
#include <linux/serial_8250.h>
#include <linux/pm.h>
#include <asm/io.h>
#include <asm/mach/rts7751r2d.h>
#include <asm/voyagergx.h>

static struct plat_serial8250_port uart_platform_data[] = {
	{
		.membase	= (void *)VOYAGER_UART_BASE,
		.mapbase	= VOYAGER_UART_BASE,
		.iotype		= UPIO_MEM,
		.irq		= VOYAGER_UART0_IRQ,
		.flags		= UPF_BOOT_AUTOCONF,
		.regshift	= 2,
		.uartclk	= (9600 * 16),
	}, {
		.flags		= 0,
	},
};

static void __init voyagergx_serial_init(void)
{
	unsigned long val;

	/*
	 * GPIO Control
	 */
	val = inl(GPIO_MUX_HIGH);
	val |= 0x00001fe0;
	outl(val, GPIO_MUX_HIGH);

	/*
	 * Power Mode Gate
	 */
	val = inl(POWER_MODE0_GATE);
	val |= (POWER_MODE0_GATE_U0 | POWER_MODE0_GATE_U1);
	outl(val, POWER_MODE0_GATE);

	val = inl(POWER_MODE1_GATE);
	val |= (POWER_MODE1_GATE_U0 | POWER_MODE1_GATE_U1);
	outl(val, POWER_MODE1_GATE);
}

static struct platform_device uart_device = {
	.name		= "serial8250",
	.id		= -1,
	.dev		= {
		.platform_data	= uart_platform_data,
	},
};

static struct platform_device *rts7751r2d_devices[] __initdata = {
	&uart_device,
};

static int __init rts7751r2d_devices_setup(void)
{
	return platform_add_devices(rts7751r2d_devices,
				    ARRAY_SIZE(rts7751r2d_devices));
}
__initcall(rts7751r2d_devices_setup);

const char *get_system_type(void)
{
	return "RTS7751R2D";
}

static void rts7751r2d_power_off(void)
{
	ctrl_outw(0x0001, PA_POWOFF);
}

/*
 * Initialize the board
 */
void __init platform_setup(void)
{
	printk(KERN_INFO "Renesas Technology Sales RTS7751R2D support.\n");
	ctrl_outw(0x0000, PA_OUTPORT);
	pm_power_off = rts7751r2d_power_off;

	voyagergx_serial_init();

}
