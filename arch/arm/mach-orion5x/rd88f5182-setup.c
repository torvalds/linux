/*
 * arch/arm/mach-orion5x/rd88f5182-setup.c
 *
 * Marvell Orion-NAS Reference Design Setup
 *
 * Maintainer: Ronen Shitrit <rshitrit@marvell.com>
 *
 * This file is licensed under the terms of the GNU General Public
 * License version 2.  This program is licensed "as is" without any
 * warranty of any kind, whether express or implied.
 */

#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/platform_device.h>
#include <linux/pci.h>
#include <linux/irq.h>
#include <linux/mtd/physmap.h>
#include <linux/mv643xx_eth.h>
#include <linux/ata_platform.h>
#include <linux/i2c.h>
#include <asm/mach-types.h>
#include <asm/gpio.h>
#include <asm/leds.h>
#include <asm/mach/arch.h>
#include <asm/mach/pci.h>
#include <asm/arch/orion5x.h>
#include "common.h"

/*****************************************************************************
 * RD-88F5182 Info
 ****************************************************************************/

/*
 * 512K NOR flash Device bus boot chip select
 */

#define RD88F5182_NOR_BOOT_BASE		0xf4000000
#define RD88F5182_NOR_BOOT_SIZE		SZ_512K

/*
 * 16M NOR flash on Device bus chip select 1
 */

#define RD88F5182_NOR_BASE		0xfc000000
#define RD88F5182_NOR_SIZE		SZ_16M

/*
 * PCI
 */

#define RD88F5182_PCI_SLOT0_OFFS	7
#define RD88F5182_PCI_SLOT0_IRQ_A_PIN	7
#define RD88F5182_PCI_SLOT0_IRQ_B_PIN	6

/*
 * GPIO Debug LED
 */

#define RD88F5182_GPIO_DBG_LED		0

/*****************************************************************************
 * 16M NOR Flash on Device bus CS1
 ****************************************************************************/

static struct physmap_flash_data rd88f5182_nor_flash_data = {
	.width		= 1,
};

static struct resource rd88f5182_nor_flash_resource = {
	.flags			= IORESOURCE_MEM,
	.start			= RD88F5182_NOR_BASE,
	.end			= RD88F5182_NOR_BASE + RD88F5182_NOR_SIZE - 1,
};

static struct platform_device rd88f5182_nor_flash = {
	.name			= "physmap-flash",
	.id			= 0,
	.dev		= {
		.platform_data	= &rd88f5182_nor_flash_data,
	},
	.num_resources		= 1,
	.resource		= &rd88f5182_nor_flash_resource,
};

#ifdef CONFIG_LEDS

/*****************************************************************************
 * Use GPIO debug led as CPU active indication
 ****************************************************************************/

static void rd88f5182_dbgled_event(led_event_t evt)
{
	int val;

	if (evt == led_idle_end)
		val = 1;
	else if (evt == led_idle_start)
		val = 0;
	else
		return;

	gpio_set_value(RD88F5182_GPIO_DBG_LED, val);
}

static int __init rd88f5182_dbgled_init(void)
{
	int pin;

	if (machine_is_rd88f5182()) {
		pin = RD88F5182_GPIO_DBG_LED;

		if (gpio_request(pin, "DBGLED") == 0) {
			if (gpio_direction_output(pin, 0) != 0) {
				printk(KERN_ERR "rd88f5182_dbgled_init failed "
						"to set output pin %d\n", pin);
				gpio_free(pin);
				return 0;
			}
		} else {
			printk(KERN_ERR "rd88f5182_dbgled_init failed "
					"to request gpio %d\n", pin);
			return 0;
		}

		leds_event = rd88f5182_dbgled_event;
	}
	return 0;
}

__initcall(rd88f5182_dbgled_init);

#endif

/*****************************************************************************
 * PCI
 ****************************************************************************/

void __init rd88f5182_pci_preinit(void)
{
	int pin;

	/*
	 * Configure PCI GPIO IRQ pins
	 */
	pin = RD88F5182_PCI_SLOT0_IRQ_A_PIN;
	if (gpio_request(pin, "PCI IntA") == 0) {
		if (gpio_direction_input(pin) == 0) {
			set_irq_type(gpio_to_irq(pin), IRQT_LOW);
		} else {
			printk(KERN_ERR "rd88f5182_pci_preinit faield to "
					"set_irq_type pin %d\n", pin);
			gpio_free(pin);
		}
	} else {
		printk(KERN_ERR "rd88f5182_pci_preinit failed to request gpio %d\n", pin);
	}

	pin = RD88F5182_PCI_SLOT0_IRQ_B_PIN;
	if (gpio_request(pin, "PCI IntB") == 0) {
		if (gpio_direction_input(pin) == 0) {
			set_irq_type(gpio_to_irq(pin), IRQT_LOW);
		} else {
			printk(KERN_ERR "rd88f5182_pci_preinit faield to "
					"set_irq_type pin %d\n", pin);
			gpio_free(pin);
		}
	} else {
		printk(KERN_ERR "rd88f5182_pci_preinit failed to gpio_request %d\n", pin);
	}
}

static int __init rd88f5182_pci_map_irq(struct pci_dev *dev, u8 slot, u8 pin)
{
	int irq;

	/*
	 * Check for devices with hard-wired IRQs.
	 */
	irq = orion5x_pci_map_irq(dev, slot, pin);
	if (irq != -1)
		return irq;

	/*
	 * PCI IRQs are connected via GPIOs
	 */
	switch (slot - RD88F5182_PCI_SLOT0_OFFS) {
	case 0:
		if (pin == 1)
			return gpio_to_irq(RD88F5182_PCI_SLOT0_IRQ_A_PIN);
		else
			return gpio_to_irq(RD88F5182_PCI_SLOT0_IRQ_B_PIN);
	default:
		return -1;
	}
}

static struct hw_pci rd88f5182_pci __initdata = {
	.nr_controllers	= 2,
	.preinit	= rd88f5182_pci_preinit,
	.swizzle	= pci_std_swizzle,
	.setup		= orion5x_pci_sys_setup,
	.scan		= orion5x_pci_sys_scan_bus,
	.map_irq	= rd88f5182_pci_map_irq,
};

static int __init rd88f5182_pci_init(void)
{
	if (machine_is_rd88f5182())
		pci_common_init(&rd88f5182_pci);

	return 0;
}

subsys_initcall(rd88f5182_pci_init);

/*****************************************************************************
 * Ethernet
 ****************************************************************************/

static struct mv643xx_eth_platform_data rd88f5182_eth_data = {
	.phy_addr	= 8,
	.force_phy_addr = 1,
};

/*****************************************************************************
 * RTC DS1338 on I2C bus
 ****************************************************************************/
static struct i2c_board_info __initdata rd88f5182_i2c_rtc = {
	I2C_BOARD_INFO("ds1338", 0x68),
};

/*****************************************************************************
 * Sata
 ****************************************************************************/
static struct mv_sata_platform_data rd88f5182_sata_data = {
	.n_ports        = 2,
};

/*****************************************************************************
 * General Setup
 ****************************************************************************/

static struct platform_device *rd88f5182_devices[] __initdata = {
	&rd88f5182_nor_flash,
};

static void __init rd88f5182_init(void)
{
	/*
	 * Setup basic Orion functions. Need to be called early.
	 */
	orion5x_init();

	/*
	 * Setup the CPU address decode windows for our devices
	 */
	orion5x_setup_dev_boot_win(RD88F5182_NOR_BOOT_BASE,
				RD88F5182_NOR_BOOT_SIZE);
	orion5x_setup_dev1_win(RD88F5182_NOR_BASE, RD88F5182_NOR_SIZE);

	/*
	 * Open a special address decode windows for the PCIe WA.
	 */
	orion5x_setup_pcie_wa_win(ORION5X_PCIE_WA_PHYS_BASE,
				ORION5X_PCIE_WA_SIZE);

	/*
	 * Setup Multiplexing Pins --
	 * MPP[0] Debug Led (GPIO - Out)
	 * MPP[1] Debug Led (GPIO - Out)
	 * MPP[2] N/A
	 * MPP[3] RTC_Int (GPIO - In)
	 * MPP[4] GPIO
	 * MPP[5] GPIO
	 * MPP[6] PCI_intA (GPIO - In)
	 * MPP[7] PCI_intB (GPIO - In)
	 * MPP[8-11] N/A
	 * MPP[12] SATA 0 presence Indication
	 * MPP[13] SATA 1 presence Indication
	 * MPP[14] SATA 0 active Indication
	 * MPP[15] SATA 1 active indication
	 * MPP[16-19] Not used
	 * MPP[20] PCI Clock to MV88F5182
	 * MPP[21] PCI Clock to mini PCI CON11
	 * MPP[22] USB 0 over current indication
	 * MPP[23] USB 1 over current indication
	 * MPP[24] USB 1 over current enable
	 * MPP[25] USB 0 over current enable
	 */

	orion5x_write(MPP_0_7_CTRL, 0x00000003);
	orion5x_write(MPP_8_15_CTRL, 0x55550000);
	orion5x_write(MPP_16_19_CTRL, 0x5555);

	orion5x_gpio_set_valid_pins(0x000000fb);

	platform_add_devices(rd88f5182_devices, ARRAY_SIZE(rd88f5182_devices));
	i2c_register_board_info(0, &rd88f5182_i2c_rtc, 1);
	orion5x_eth_init(&rd88f5182_eth_data);
	orion5x_sata_init(&rd88f5182_sata_data);
}

MACHINE_START(RD88F5182, "Marvell Orion-NAS Reference Design")
	/* Maintainer: Ronen Shitrit <rshitrit@marvell.com> */
	.phys_io	= ORION5X_REGS_PHYS_BASE,
	.io_pg_offst	= ((ORION5X_REGS_VIRT_BASE) >> 18) & 0xFFFC,
	.boot_params	= 0x00000100,
	.init_machine	= rd88f5182_init,
	.map_io		= orion5x_map_io,
	.init_irq	= orion5x_init_irq,
	.timer		= &orion5x_timer,
MACHINE_END
