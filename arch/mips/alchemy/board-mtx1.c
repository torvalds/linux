// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * MTX-1 platform devices registration (Au1500)
 *
 * Copyright (C) 2007-2009, Florian Fainelli <florian@openwrt.org>
 */

#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/kernel.h>
#include <linux/platform_device.h>
#include <linux/gpio/machine.h>
#include <linux/gpio/property.h>
#include <linux/input.h>
#include <linux/mtd/partitions.h>
#include <linux/mtd/physmap.h>
#include <mtd/mtd-abi.h>
#include <asm/bootinfo.h>
#include <asm/reboot.h>
#include <asm/setup.h>
#include <asm/mach-au1x00/au1000.h>
#include <asm/mach-au1x00/gpio-au1000.h>
#include <asm/mach-au1x00/au1xxx_eth.h>
#include <prom.h>

const char *get_system_type(void)
{
	return "MTX-1";
}

void prom_putchar(char c)
{
	alchemy_uart_putchar(AU1000_UART0_PHYS_ADDR, c);
}

static void mtx1_reset(char *c)
{
	/* Jump to the reset vector */
	__asm__ __volatile__("jr\t%0" : : "r"(0xbfc00000));
}

static void mtx1_power_off(void)
{
	while (1)
		asm volatile (
		"	.set	mips32					\n"
		"	wait						\n"
		"	.set	mips0					\n");
}

void __init board_setup(void)
{
#if IS_ENABLED(CONFIG_USB_OHCI_HCD)
	/* Enable USB power switch */
	alchemy_gpio_direction_output(204, 0);
#endif /* IS_ENABLED(CONFIG_USB_OHCI_HCD) */

	/* Initialize sys_pinfunc */
	alchemy_wrsys(SYS_PF_NI2, AU1000_SYS_PINFUNC);

	/* Initialize GPIO */
	alchemy_wrsys(~0, AU1000_SYS_TRIOUTCLR);
	alchemy_gpio_direction_output(0, 0);	/* Disable M66EN (PCI 66MHz) */
	alchemy_gpio_direction_output(3, 1);	/* Disable PCI CLKRUN# */
	alchemy_gpio_direction_output(1, 1);	/* Enable EXT_IO3 */
	alchemy_gpio_direction_output(5, 0);	/* Disable eth PHY TX_ER */

	/* Enable LED and set it to green */
	alchemy_gpio_direction_output(211, 1);	/* green on */
	alchemy_gpio_direction_output(212, 0);	/* red off */

	pm_power_off = mtx1_power_off;
	_machine_halt = mtx1_power_off;
	_machine_restart = mtx1_reset;

	printk(KERN_INFO "4G Systems MTX-1 Board\n");
}

/******************************************************************************/

static const struct software_node mtx1_gpiochip_node = {
	.name = "alchemy-gpio2",
};

static const struct software_node mtx1_gpio_keys_node = {
	.name = "mtx1-gpio-keys",
};

static const struct property_entry mtx1_button_props[] = {
	PROPERTY_ENTRY_U32("linux,code", BTN_0),
	PROPERTY_ENTRY_GPIO("gpios", &mtx1_gpiochip_node, 7, GPIO_ACTIVE_HIGH),
	PROPERTY_ENTRY_STRING("label", "System button"),
	{ }
};

static const struct software_node mtx1_button_node = {
	.parent = &mtx1_gpio_keys_node,
	.properties = mtx1_button_props,
};

static const struct software_node *mtx1_gpio_keys_swnodes[] __initconst = {
	&mtx1_gpio_keys_node,
	&mtx1_button_node,
	NULL
};

static void __init mtx1_keys_init(void)
{
	struct platform_device_info keys_info = {
		.name	= "gpio-keys",
		.id	= PLATFORM_DEVID_NONE,
	};
	struct platform_device *pd;
	int err;

	err = software_node_register_node_group(mtx1_gpio_keys_swnodes);
	if (err) {
		pr_err("failed to register gpio-keys software nodes: %d\n", err);
		return;
	}

	keys_info.fwnode = software_node_fwnode(&mtx1_gpio_keys_node);

	pd = platform_device_register_full(&keys_info);
	err = PTR_ERR_OR_ZERO(pd);
	if (err)
		pr_err("failed to create gpio-keys device: %d\n", err);
}

/* Global number 215 is offset 15 on Alchemy GPIO 2 */
static const struct property_entry mtx1_wdt_props[] = {
	PROPERTY_ENTRY_GPIO("gpios", &mtx1_gpiochip_node, 15, GPIO_ACTIVE_HIGH),
	{ }
};

static struct platform_device_info mtx1_wdt_info __initconst = {
	.name = "mtx1-wdt",
	.id = 0,
	.properties = mtx1_wdt_props,
};

static void __init mtx1_wdt_init(void)
{
	struct platform_device *pd;
	int err;

	pd = platform_device_register_full(&mtx1_wdt_info);
	err = PTR_ERR_OR_ZERO(pd);
	if (err)
		pr_err("failed to create gpio-keys device: %d\n", err);
}

static const struct software_node mtx1_gpio_leds_node = {
	.name = "mtx1-leds",
};

static const struct property_entry mtx1_green_led_props[] = {
	PROPERTY_ENTRY_GPIO("gpios", &mtx1_gpiochip_node, 11, GPIO_ACTIVE_HIGH),
	{ }
};

static const struct software_node mtx1_green_led_node = {
	.name = "mtx1:green",
	.parent = &mtx1_gpio_leds_node,
	.properties = mtx1_green_led_props,
};

static const struct property_entry mtx1_red_led_props[] = {
	PROPERTY_ENTRY_GPIO("gpios", &mtx1_gpiochip_node, 12, GPIO_ACTIVE_HIGH),
	{ }
};

static const struct software_node mtx1_red_led_node = {
	.name = "mtx1:red",
	.parent = &mtx1_gpio_leds_node,
	.properties = mtx1_red_led_props,
};

static const struct software_node *mtx1_gpio_leds_swnodes[] = {
	&mtx1_gpio_leds_node,
	&mtx1_green_led_node,
	&mtx1_red_led_node,
	NULL
};

static void __init mtx1_leds_init(void)
{
	struct platform_device_info led_info = {
		.name	= "leds-gpio",
		.id	= PLATFORM_DEVID_NONE,
	};
	struct platform_device *led_dev;
	int err;

	err = software_node_register_node_group(mtx1_gpio_leds_swnodes);
	if (err) {
		pr_err("failed to register LED software nodes: %d\n", err);
		return;
	}

	led_info.fwnode = software_node_fwnode(&mtx1_gpio_leds_node);

	led_dev = platform_device_register_full(&led_info);
	err = PTR_ERR_OR_ZERO(led_dev);
	if (err)
		pr_err("failed to create LED device: %d\n", err);
}

static struct mtd_partition mtx1_mtd_partitions[] = {
	{
		.name	= "filesystem",
		.size	= 0x01C00000,
		.offset = 0,
	},
	{
		.name	= "yamon",
		.size	= 0x00100000,
		.offset = MTDPART_OFS_APPEND,
		.mask_flags = MTD_WRITEABLE,
	},
	{
		.name	= "kernel",
		.size	= 0x002c0000,
		.offset = MTDPART_OFS_APPEND,
	},
	{
		.name	= "yamon env",
		.size	= 0x00040000,
		.offset = MTDPART_OFS_APPEND,
	},
};

static struct physmap_flash_data mtx1_flash_data = {
	.width		= 4,
	.nr_parts	= 4,
	.parts		= mtx1_mtd_partitions,
};

static struct resource mtx1_mtd_resource = {
	.start	= 0x1e000000,
	.end	= 0x1fffffff,
	.flags	= IORESOURCE_MEM,
};

static struct platform_device mtx1_mtd = {
	.name		= "physmap-flash",
	.dev		= {
		.platform_data	= &mtx1_flash_data,
	},
	.num_resources	= 1,
	.resource	= &mtx1_mtd_resource,
};

static struct resource alchemy_pci_host_res[] = {
	[0] = {
		.start	= AU1500_PCI_PHYS_ADDR,
		.end	= AU1500_PCI_PHYS_ADDR + 0xfff,
		.flags	= IORESOURCE_MEM,
	},
};

static int mtx1_pci_idsel(unsigned int devsel, int assert)
{
	/* This function is only necessary to support a proprietary Cardbus
	 * adapter on the mtx-1 "singleboard" variant. It triggers a custom
	 * logic chip connected to EXT_IO3 (GPIO1) to suppress IDSEL signals.
	 */
	udelay(1);

	if (assert && devsel != 0)
		/* Suppress signal to Cardbus */
		alchemy_gpio_set_value(1, 0);	/* set EXT_IO3 OFF */
	else
		alchemy_gpio_set_value(1, 1);	/* set EXT_IO3 ON */

	udelay(1);
	return 1;
}

static const char mtx1_irqtab[][5] = {
	[0] = { -1, AU1500_PCI_INTA, AU1500_PCI_INTA, 0xff, 0xff }, /* IDSEL 00 - AdapterA-Slot0 (top) */
	[1] = { -1, AU1500_PCI_INTB, AU1500_PCI_INTA, 0xff, 0xff }, /* IDSEL 01 - AdapterA-Slot1 (bottom) */
	[2] = { -1, AU1500_PCI_INTC, AU1500_PCI_INTD, 0xff, 0xff }, /* IDSEL 02 - AdapterB-Slot0 (top) */
	[3] = { -1, AU1500_PCI_INTD, AU1500_PCI_INTC, 0xff, 0xff }, /* IDSEL 03 - AdapterB-Slot1 (bottom) */
	[4] = { -1, AU1500_PCI_INTA, AU1500_PCI_INTB, 0xff, 0xff }, /* IDSEL 04 - AdapterC-Slot0 (top) */
	[5] = { -1, AU1500_PCI_INTB, AU1500_PCI_INTA, 0xff, 0xff }, /* IDSEL 05 - AdapterC-Slot1 (bottom) */
	[6] = { -1, AU1500_PCI_INTC, AU1500_PCI_INTD, 0xff, 0xff }, /* IDSEL 06 - AdapterD-Slot0 (top) */
	[7] = { -1, AU1500_PCI_INTD, AU1500_PCI_INTC, 0xff, 0xff }, /* IDSEL 07 - AdapterD-Slot1 (bottom) */
};

static int mtx1_map_pci_irq(const struct pci_dev *d, u8 slot, u8 pin)
{
	return mtx1_irqtab[slot][pin];
}

static struct alchemy_pci_platdata mtx1_pci_pd = {
	.board_map_irq	 = mtx1_map_pci_irq,
	.board_pci_idsel = mtx1_pci_idsel,
	.pci_cfg_set	 = PCI_CONFIG_AEN | PCI_CONFIG_R2H | PCI_CONFIG_R1H |
			   PCI_CONFIG_CH |
#if defined(__MIPSEB__)
			   PCI_CONFIG_SIC_HWA_DAT | PCI_CONFIG_SM,
#else
			   0,
#endif
};

static struct platform_device mtx1_pci_host = {
	.dev.platform_data = &mtx1_pci_pd,
	.name		= "alchemy-pci",
	.id		= 0,
	.num_resources	= ARRAY_SIZE(alchemy_pci_host_res),
	.resource	= alchemy_pci_host_res,
};

static struct platform_device *mtx1_devs[] __initdata = {
	&mtx1_pci_host,
	&mtx1_mtd,
};

static struct au1000_eth_platform_data mtx1_au1000_eth0_pdata = {
	.phy_search_highest_addr	= 1,
	.phy1_search_mac0		= 1,
};

static int __init mtx1_register_devices(void)
{
	int rc;

	irq_set_irq_type(AU1500_GPIO204_INT, IRQ_TYPE_LEVEL_HIGH);
	irq_set_irq_type(AU1500_GPIO201_INT, IRQ_TYPE_LEVEL_LOW);
	irq_set_irq_type(AU1500_GPIO202_INT, IRQ_TYPE_LEVEL_LOW);
	irq_set_irq_type(AU1500_GPIO203_INT, IRQ_TYPE_LEVEL_LOW);
	irq_set_irq_type(AU1500_GPIO205_INT, IRQ_TYPE_LEVEL_LOW);

	au1xxx_override_eth_cfg(0, &mtx1_au1000_eth0_pdata);

	rc = software_node_register(&mtx1_gpiochip_node);
	if (rc)
		return rc;

	rc = platform_add_devices(mtx1_devs, ARRAY_SIZE(mtx1_devs));
	if (rc)
		return rc;

	mtx1_leds_init();
	mtx1_wdt_init();
	mtx1_keys_init();

	return 0;
}
arch_initcall(mtx1_register_devices);
