// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * arch/arm/mach-iop32x/n2100.c
 *
 * Board support code for the Thecus N2100 platform.
 *
 * Author: Rory Bolt <rorybolt@pacbell.net>
 * Copyright (C) 2002 Rory Bolt
 * Copyright 2003 (c) MontaVista, Software, Inc.
 * Copyright (C) 2004 Intel Corp.
 */

#include <linux/mm.h>
#include <linux/init.h>
#include <linux/f75375s.h>
#include <linux/leds-pca9532.h>
#include <linux/delay.h>
#include <linux/kernel.h>
#include <linux/pci.h>
#include <linux/pm.h>
#include <linux/string.h>
#include <linux/serial_core.h>
#include <linux/serial_8250.h>
#include <linux/mtd/physmap.h>
#include <linux/i2c.h>
#include <linux/platform_device.h>
#include <linux/reboot.h>
#include <linux/io.h>
#include <linux/gpio.h>
#include <linux/gpio/machine.h>
#include <asm/irq.h>
#include <asm/mach/arch.h>
#include <asm/mach/map.h>
#include <asm/mach/pci.h>
#include <asm/mach/time.h>
#include <asm/mach-types.h>
#include <asm/page.h>

#include "hardware.h"
#include "irqs.h"
#include "gpio-iop32x.h"

/*
 * N2100 timer tick configuration.
 */
static void __init n2100_timer_init(void)
{
	/* 33.000 MHz crystal.  */
	iop_init_time(198000000);
}


/*
 * N2100 I/O.
 */
static struct map_desc n2100_io_desc[] __initdata = {
	{	/* on-board devices */
		.virtual	= N2100_UART,
		.pfn		= __phys_to_pfn(N2100_UART),
		.length		= 0x00100000,
		.type		= MT_DEVICE
	},
};

void __init n2100_map_io(void)
{
	iop3xx_map_io();
	iotable_init(n2100_io_desc, ARRAY_SIZE(n2100_io_desc));
}


/*
 * N2100 PCI.
 */
static int n2100_pci_map_irq(const struct pci_dev *dev, u8 slot, u8 pin)
{
	int irq;

	if (PCI_SLOT(dev->devfn) == 1) {
		/* RTL8110SB #1 */
		irq = IRQ_IOP32X_XINT0;
	} else if (PCI_SLOT(dev->devfn) == 2) {
		/* RTL8110SB #2 */
		irq = IRQ_IOP32X_XINT3;
	} else if (PCI_SLOT(dev->devfn) == 3) {
		/* Sil3512 */
		irq = IRQ_IOP32X_XINT2;
	} else if (PCI_SLOT(dev->devfn) == 4 && pin == 1) {
		/* VT6212 INTA */
		irq = IRQ_IOP32X_XINT1;
	} else if (PCI_SLOT(dev->devfn) == 4 && pin == 2) {
		/* VT6212 INTB */
		irq = IRQ_IOP32X_XINT0;
	} else if (PCI_SLOT(dev->devfn) == 4 && pin == 3) {
		/* VT6212 INTC */
		irq = IRQ_IOP32X_XINT2;
	} else if (PCI_SLOT(dev->devfn) == 5) {
		/* Mini-PCI slot */
		irq = IRQ_IOP32X_XINT3;
	} else {
		printk(KERN_ERR "n2100_pci_map_irq() called for unknown "
			"device PCI:%d:%d:%d\n", dev->bus->number,
			PCI_SLOT(dev->devfn), PCI_FUNC(dev->devfn));
		irq = -1;
	}

	return irq;
}

static struct hw_pci n2100_pci __initdata = {
	.nr_controllers = 1,
	.ops		= &iop3xx_ops,
	.setup		= iop3xx_pci_setup,
	.preinit	= iop3xx_pci_preinit,
	.map_irq	= n2100_pci_map_irq,
};

/*
 * Both r8169 chips on the n2100 exhibit PCI parity problems.  Turn
 * off parity reporting for both ports so we don't get error interrupts
 * for them.
 */
static void n2100_fixup_r8169(struct pci_dev *dev)
{
	if (dev->bus->number == 0 &&
	    (dev->devfn == PCI_DEVFN(1, 0) ||
	     dev->devfn == PCI_DEVFN(2, 0)))
		pci_disable_parity(dev);
}
DECLARE_PCI_FIXUP_FINAL(PCI_VENDOR_ID_REALTEK, PCI_ANY_ID, n2100_fixup_r8169);

static int __init n2100_pci_init(void)
{
	if (machine_is_n2100())
		pci_common_init(&n2100_pci);

	return 0;
}

subsys_initcall(n2100_pci_init);


/*
 * N2100 machine initialisation.
 */
static struct physmap_flash_data n2100_flash_data = {
	.width		= 2,
};

static struct resource n2100_flash_resource = {
	.start		= 0xf0000000,
	.end		= 0xf0ffffff,
	.flags		= IORESOURCE_MEM,
};

static struct platform_device n2100_flash_device = {
	.name		= "physmap-flash",
	.id		= 0,
	.dev		= {
		.platform_data	= &n2100_flash_data,
	},
	.num_resources	= 1,
	.resource	= &n2100_flash_resource,
};


static struct plat_serial8250_port n2100_serial_port[] = {
	{
		.mapbase	= N2100_UART,
		.membase	= (char *)N2100_UART,
		.irq		= 0,
		.flags		= UPF_SKIP_TEST | UPF_AUTO_IRQ | UPF_SHARE_IRQ,
		.iotype		= UPIO_MEM,
		.regshift	= 0,
		.uartclk	= 1843200,
	},
	{ },
};

static struct resource n2100_uart_resource = {
	.start		= N2100_UART,
	.end		= N2100_UART + 7,
	.flags		= IORESOURCE_MEM,
};

static struct platform_device n2100_serial_device = {
	.name		= "serial8250",
	.id		= PLAT8250_DEV_PLATFORM,
	.dev		= {
		.platform_data		= n2100_serial_port,
	},
	.num_resources	= 1,
	.resource	= &n2100_uart_resource,
};

static struct f75375s_platform_data n2100_f75375s = {
	.pwm		= { 255, 255 },
	.pwm_enable = { 0, 0 },
};

static struct pca9532_platform_data n2100_leds = {
	.leds = {
	{	.name = "n2100:red:satafail0",
		.state = PCA9532_OFF,
		.type = PCA9532_TYPE_LED,
	},
	{	.name = "n2100:red:satafail1",
		.state = PCA9532_OFF,
		.type = PCA9532_TYPE_LED,
	},
	{	.name = "n2100:blue:usb",
		.state = PCA9532_OFF,
		.type = PCA9532_TYPE_LED,
	},
	{ 	.type = PCA9532_TYPE_NONE },

	{ 	.type = PCA9532_TYPE_NONE },
	{ 	.type = PCA9532_TYPE_NONE },
	{ 	.type = PCA9532_TYPE_NONE },
	{	.name = "n2100:red:usb",
		.state = PCA9532_OFF,
		.type = PCA9532_TYPE_LED,
	},

	{	.type = PCA9532_TYPE_NONE }, /* power OFF gpio */
	{	.type = PCA9532_TYPE_NONE }, /* reset gpio */
	{	.type = PCA9532_TYPE_NONE },
	{	.type = PCA9532_TYPE_NONE },

	{	.type = PCA9532_TYPE_NONE },
	{	.name = "n2100:orange:system",
		.state = PCA9532_OFF,
		.type = PCA9532_TYPE_LED,
	},
	{	.name = "n2100:red:system",
		.state = PCA9532_OFF,
		.type = PCA9532_TYPE_LED,
	},
	{	.name = "N2100 beeper"  ,
		.state =  PCA9532_OFF,
		.type = PCA9532_TYPE_N2100_BEEP,
	},
	},
	.psc = { 0, 0 },
	.pwm = { 0, 0 },
};

static struct i2c_board_info __initdata n2100_i2c_devices[] = {
	{
		I2C_BOARD_INFO("rs5c372b", 0x32),
	},
	{
		I2C_BOARD_INFO("f75375", 0x2e),
		.platform_data = &n2100_f75375s,
	},
	{
		I2C_BOARD_INFO("pca9532", 0x60),
		.platform_data = &n2100_leds,
	},
};

/*
 * Pull PCA9532 GPIO #8 low to power off the machine.
 */
static void n2100_power_off(void)
{
	local_irq_disable();

	/* Start condition, I2C address of PCA9532, write transaction.  */
	*IOP3XX_IDBR0 = 0xc0;
	*IOP3XX_ICR0 = 0xe9;
	mdelay(1);

	/* Write address 0x08.  */
	*IOP3XX_IDBR0 = 0x08;
	*IOP3XX_ICR0 = 0xe8;
	mdelay(1);

	/* Write data 0x01, stop condition.  */
	*IOP3XX_IDBR0 = 0x01;
	*IOP3XX_ICR0 = 0xea;

	while (1)
		;
}

static void n2100_restart(enum reboot_mode mode, const char *cmd)
{
	int ret;

	ret = gpio_direction_output(N2100_HARDWARE_RESET, 0);
	if (ret) {
		pr_crit("could not drive reset GPIO low\n");
		return;
	}
	/* Wait for reset to happen */
	while (1)
		;
}


static struct timer_list power_button_poll_timer;

static void power_button_poll(struct timer_list *unused)
{
	if (gpio_get_value(N2100_POWER_BUTTON) == 0) {
		ctrl_alt_del();
		return;
	}

	power_button_poll_timer.expires = jiffies + (HZ / 10);
	add_timer(&power_button_poll_timer);
}

static int __init n2100_request_gpios(void)
{
	int ret;

	if (!machine_is_n2100())
		return 0;

	ret = gpio_request(N2100_HARDWARE_RESET, "reset");
	if (ret)
		pr_err("could not request reset GPIO\n");

	ret = gpio_request(N2100_POWER_BUTTON, "power");
	if (ret)
		pr_err("could not request power GPIO\n");
	else {
		ret = gpio_direction_input(N2100_POWER_BUTTON);
		if (ret)
			pr_err("could not set power GPIO as input\n");
	}
	/* Set up power button poll timer */
	timer_setup(&power_button_poll_timer, power_button_poll, 0);
	power_button_poll_timer.expires = jiffies + (HZ / 10);
	add_timer(&power_button_poll_timer);
	return 0;
}
device_initcall(n2100_request_gpios);

static void __init n2100_init_machine(void)
{
	register_iop32x_gpio();
	gpiod_add_lookup_table(&iop3xx_i2c0_gpio_lookup);
	platform_device_register(&iop3xx_i2c0_device);
	platform_device_register(&n2100_flash_device);
	platform_device_register(&n2100_serial_device);
	platform_device_register(&iop3xx_dma_0_channel);
	platform_device_register(&iop3xx_dma_1_channel);

	i2c_register_board_info(0, n2100_i2c_devices,
		ARRAY_SIZE(n2100_i2c_devices));

	pm_power_off = n2100_power_off;
}

MACHINE_START(N2100, "Thecus N2100")
	/* Maintainer: Lennert Buytenhek <buytenh@wantstofly.org> */
	.atag_offset	= 0x100,
	.nr_irqs	= IOP32X_NR_IRQS,
	.map_io		= n2100_map_io,
	.init_irq	= iop32x_init_irq,
	.init_time	= n2100_timer_init,
	.init_machine	= n2100_init_machine,
	.restart	= n2100_restart,
MACHINE_END
