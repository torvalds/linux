/*
 * GPR board platform device registration (Au1550)
 *
 * Copyright (C) 2010 Wolfgang Grandegger <wg@denx.de>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
 */

#include <linux/delay.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/kernel.h>
#include <linux/platform_device.h>
#include <linux/pm.h>
#include <linux/mtd/partitions.h>
#include <linux/mtd/physmap.h>
#include <linux/leds.h>
#include <linux/gpio.h>
#include <linux/i2c.h>
#include <linux/i2c-gpio.h>
#include <linux/gpio/machine.h>
#include <asm/bootinfo.h>
#include <asm/idle.h>
#include <asm/reboot.h>
#include <asm/mach-au1x00/au1000.h>
#include <asm/mach-au1x00/gpio-au1000.h>
#include <prom.h>

const char *get_system_type(void)
{
	return "GPR";
}

void __init prom_init(void)
{
	unsigned char *memsize_str;
	unsigned long memsize;

	prom_argc = fw_arg0;
	prom_argv = (char **)fw_arg1;
	prom_envp = (char **)fw_arg2;

	prom_init_cmdline();

	memsize_str = prom_getenv("memsize");
	if (!memsize_str || kstrtoul(memsize_str, 0, &memsize))
		memsize = 0x04000000;
	add_memory_region(0, memsize, BOOT_MEM_RAM);
}

void prom_putchar(unsigned char c)
{
	alchemy_uart_putchar(AU1000_UART0_PHYS_ADDR, c);
}

static void gpr_reset(char *c)
{
	/* switch System-LED to orange (red# and green# on) */
	alchemy_gpio_direction_output(4, 0);
	alchemy_gpio_direction_output(5, 0);

	/* trigger watchdog to reset board in 200ms */
	printk(KERN_EMERG "Triggering watchdog soft reset...\n");
	raw_local_irq_disable();
	alchemy_gpio_direction_output(1, 0);
	udelay(1);
	alchemy_gpio_set_value(1, 1);
	while (1)
		cpu_wait();
}

static void gpr_power_off(void)
{
	while (1)
		cpu_wait();
}

void __init board_setup(void)
{
	printk(KERN_INFO "Trapeze ITS GPR board\n");

	pm_power_off = gpr_power_off;
	_machine_halt = gpr_power_off;
	_machine_restart = gpr_reset;

	/* Enable UART1/3 */
	alchemy_uart_enable(AU1000_UART3_PHYS_ADDR);
	alchemy_uart_enable(AU1000_UART1_PHYS_ADDR);

	/* Take away Reset of UMTS-card */
	alchemy_gpio_direction_output(215, 1);
}

/*
 * Watchdog
 */
static struct resource gpr_wdt_resource[] = {
	[0] = {
		.start	= 1,
		.end	= 1,
		.name	= "gpr-adm6320-wdt",
		.flags	= IORESOURCE_IRQ,
	}
};

static struct platform_device gpr_wdt_device = {
	.name = "adm6320-wdt",
	.id = 0,
	.num_resources = ARRAY_SIZE(gpr_wdt_resource),
	.resource = gpr_wdt_resource,
};

/*
 * FLASH
 *
 * 0x00000000-0x00200000 : "kernel"
 * 0x00200000-0x00a00000 : "rootfs"
 * 0x01d00000-0x01f00000 : "config"
 * 0x01c00000-0x01d00000 : "yamon"
 * 0x01d00000-0x01d40000 : "yamon env vars"
 * 0x00000000-0x00a00000 : "kernel+rootfs"
 */
static struct mtd_partition gpr_mtd_partitions[] = {
	{
		.name	= "kernel",
		.size	= 0x00200000,
		.offset = 0,
	},
	{
		.name	= "rootfs",
		.size	= 0x00800000,
		.offset = MTDPART_OFS_APPEND,
		.mask_flags = MTD_WRITEABLE,
	},
	{
		.name	= "config",
		.size	= 0x00200000,
		.offset = 0x01d00000,
	},
	{
		.name	= "yamon",
		.size	= 0x00100000,
		.offset = 0x01c00000,
	},
	{
		.name	= "yamon env vars",
		.size	= 0x00040000,
		.offset = MTDPART_OFS_APPEND,
	},
	{
		.name	= "kernel+rootfs",
		.size	= 0x00a00000,
		.offset = 0,
	},
};

static struct physmap_flash_data gpr_flash_data = {
	.width		= 4,
	.nr_parts	= ARRAY_SIZE(gpr_mtd_partitions),
	.parts		= gpr_mtd_partitions,
};

static struct resource gpr_mtd_resource = {
	.start	= 0x1e000000,
	.end	= 0x1fffffff,
	.flags	= IORESOURCE_MEM,
};

static struct platform_device gpr_mtd_device = {
	.name		= "physmap-flash",
	.dev		= {
		.platform_data	= &gpr_flash_data,
	},
	.num_resources	= 1,
	.resource	= &gpr_mtd_resource,
};

/*
 * LEDs
 */
static const struct gpio_led gpr_gpio_leds[] = {
	{	/* green */
		.name			= "gpr:green",
		.gpio			= 4,
		.active_low		= 1,
	},
	{	/* red */
		.name			= "gpr:red",
		.gpio			= 5,
		.active_low		= 1,
	}
};

static struct gpio_led_platform_data gpr_led_data = {
	.num_leds = ARRAY_SIZE(gpr_gpio_leds),
	.leds = gpr_gpio_leds,
};

static struct platform_device gpr_led_devices = {
	.name = "leds-gpio",
	.id = -1,
	.dev = {
		.platform_data = &gpr_led_data,
	}
};

/*
 * I2C
 */
static struct gpiod_lookup_table gpr_i2c_gpiod_table = {
	.dev_id = "i2c-gpio",
	.table = {
		/*
		 * This should be on "GPIO2" which has base at 200 so
		 * the global numbers 209 and 210 should correspond to
		 * local offsets 9 and 10.
		 */
		GPIO_LOOKUP_IDX("alchemy-gpio2", 9, NULL, 0,
				GPIO_ACTIVE_HIGH),
		GPIO_LOOKUP_IDX("alchemy-gpio2", 10, NULL, 1,
				GPIO_ACTIVE_HIGH),
	},
};

static struct i2c_gpio_platform_data gpr_i2c_data = {
	/*
	 * The open drain mode is hardwired somewhere or an electrical
	 * property of the alchemy GPIO controller.
	 */
	.sda_is_open_drain	= 1,
	.scl_is_open_drain	= 1,
	.udelay			= 2,		/* ~100 kHz */
	.timeout		= HZ,
};

static struct platform_device gpr_i2c_device = {
	.name			= "i2c-gpio",
	.id			= -1,
	.dev.platform_data	= &gpr_i2c_data,
};

static struct i2c_board_info gpr_i2c_info[] __initdata = {
	{
		I2C_BOARD_INFO("lm83", 0x18),
	}
};



static struct resource alchemy_pci_host_res[] = {
	[0] = {
		.start	= AU1500_PCI_PHYS_ADDR,
		.end	= AU1500_PCI_PHYS_ADDR + 0xfff,
		.flags	= IORESOURCE_MEM,
	},
};

static int gpr_map_pci_irq(const struct pci_dev *d, u8 slot, u8 pin)
{
	if ((slot == 0) && (pin == 1))
		return AU1550_PCI_INTA;
	else if ((slot == 0) && (pin == 2))
		return AU1550_PCI_INTB;

	return 0xff;
}

static struct alchemy_pci_platdata gpr_pci_pd = {
	.board_map_irq	= gpr_map_pci_irq,
	.pci_cfg_set	= PCI_CONFIG_AEN | PCI_CONFIG_R2H | PCI_CONFIG_R1H |
			  PCI_CONFIG_CH |
#if defined(__MIPSEB__)
			  PCI_CONFIG_SIC_HWA_DAT | PCI_CONFIG_SM,
#else
			  0,
#endif
};

static struct platform_device gpr_pci_host_dev = {
	.dev.platform_data = &gpr_pci_pd,
	.name		= "alchemy-pci",
	.id		= 0,
	.num_resources	= ARRAY_SIZE(alchemy_pci_host_res),
	.resource	= alchemy_pci_host_res,
};

static struct platform_device *gpr_devices[] __initdata = {
	&gpr_wdt_device,
	&gpr_mtd_device,
	&gpr_i2c_device,
	&gpr_led_devices,
};

static int __init gpr_pci_init(void)
{
	return platform_device_register(&gpr_pci_host_dev);
}
/* must be arch_initcall; MIPS PCI scans busses in a subsys_initcall */
arch_initcall(gpr_pci_init);


static int __init gpr_dev_init(void)
{
	gpiod_add_lookup_table(&gpr_i2c_gpiod_table);
	i2c_register_board_info(0, gpr_i2c_info, ARRAY_SIZE(gpr_i2c_info));

	return platform_add_devices(gpr_devices, ARRAY_SIZE(gpr_devices));
}
device_initcall(gpr_dev_init);
