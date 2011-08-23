/*
 * arch/arm/mach-ixp2000/ixdp2x01.c
 *
 * Code common to Intel IXDP2401 and IXDP2801 platforms
 *
 * Original Author: Andrzej Mialkowski <andrzej.mialkowski@intel.com>
 * Maintainer: Deepak Saxena <dsaxena@plexity.net>
 *
 * Copyright (C) 2002-2003 Intel Corp.
 * Copyright (C) 2003-2004 MontaVista Software, Inc.
 *
 *  This program is free software; you can redistribute  it and/or modify it
 *  under  the terms of  the GNU General  Public License as published by the
 *  Free Software Foundation;  either version 2 of the  License, or (at your
 *  option) any later version.
 */

#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/mm.h>
#include <linux/sched.h>
#include <linux/interrupt.h>
#include <linux/bitops.h>
#include <linux/pci.h>
#include <linux/ioport.h>
#include <linux/delay.h>
#include <linux/serial.h>
#include <linux/tty.h>
#include <linux/serial_core.h>
#include <linux/platform_device.h>
#include <linux/serial_8250.h>
#include <linux/io.h>

#include <asm/irq.h>
#include <asm/pgtable.h>
#include <asm/page.h>
#include <asm/system.h>
#include <mach/hardware.h>
#include <asm/mach-types.h>

#include <asm/mach/pci.h>
#include <asm/mach/map.h>
#include <asm/mach/irq.h>
#include <asm/mach/time.h>
#include <asm/mach/arch.h>
#include <asm/mach/flash.h>

/*************************************************************************
 * IXDP2x01 IRQ Handling
 *************************************************************************/
static void ixdp2x01_irq_mask(struct irq_data *d)
{
	ixp2000_reg_wrb(IXDP2X01_INT_MASK_SET_REG,
				IXP2000_BOARD_IRQ_MASK(d->irq));
}

static void ixdp2x01_irq_unmask(struct irq_data *d)
{
	ixp2000_reg_write(IXDP2X01_INT_MASK_CLR_REG,
				IXP2000_BOARD_IRQ_MASK(d->irq));
}

static u32 valid_irq_mask;

static void ixdp2x01_irq_handler(unsigned int irq, struct irq_desc *desc)
{
	u32 ex_interrupt;
	int i;

	desc->irq_data.chip->irq_mask(&desc->irq_data);

	ex_interrupt = *IXDP2X01_INT_STAT_REG & valid_irq_mask;

	if (!ex_interrupt) {
		printk(KERN_ERR "Spurious IXDP2X01 CPLD interrupt!\n");
		return;
	}

	for (i = 0; i < IXP2000_BOARD_IRQS; i++) {
		if (ex_interrupt & (1 << i)) {
			int cpld_irq = IXP2000_BOARD_IRQ(0) + i;
			generic_handle_irq(cpld_irq);
		}
	}

	desc->irq_data.chip->irq_unmask(&desc->irq_data);
}

static struct irq_chip ixdp2x01_irq_chip = {
	.irq_mask	= ixdp2x01_irq_mask,
	.irq_ack	= ixdp2x01_irq_mask,
	.irq_unmask	= ixdp2x01_irq_unmask
};

/*
 * We only do anything if we are the master NPU on the board.
 * The slave NPU only has the ethernet chip going directly to
 * the PCIB interrupt input.
 */
void __init ixdp2x01_init_irq(void)
{
	int irq = 0;

	/* initialize chip specific interrupts */
	ixp2000_init_irq();

	if (machine_is_ixdp2401())
		valid_irq_mask = IXDP2401_VALID_IRQ_MASK;
	else
		valid_irq_mask = IXDP2801_VALID_IRQ_MASK;

	/* Mask all interrupts from CPLD, disable simulation */
	ixp2000_reg_write(IXDP2X01_INT_MASK_SET_REG, 0xffffffff);
	ixp2000_reg_wrb(IXDP2X01_INT_SIM_REG, 0);

	for (irq = NR_IXP2000_IRQS; irq < NR_IXDP2X01_IRQS; irq++) {
		if (irq & valid_irq_mask) {
			irq_set_chip_and_handler(irq, &ixdp2x01_irq_chip,
						 handle_level_irq);
			set_irq_flags(irq, IRQF_VALID);
		} else {
			set_irq_flags(irq, 0);
		}
	}

	/* Hook into PCI interrupts */
	irq_set_chained_handler(IRQ_IXP2000_PCIB, ixdp2x01_irq_handler);
}


/*************************************************************************
 * IXDP2x01 memory map
 *************************************************************************/
static struct map_desc ixdp2x01_io_desc __initdata = {
	.virtual	= IXDP2X01_VIRT_CPLD_BASE, 
	.pfn		= __phys_to_pfn(IXDP2X01_PHYS_CPLD_BASE),
	.length		= IXDP2X01_CPLD_REGION_SIZE,
	.type		= MT_DEVICE
};

static void __init ixdp2x01_map_io(void)
{
	ixp2000_map_io();
	iotable_init(&ixdp2x01_io_desc, 1);
}


/*************************************************************************
 * IXDP2x01 serial ports
 *************************************************************************/
static struct plat_serial8250_port ixdp2x01_serial_port1[] = {
	{
		.mapbase	= (unsigned long)IXDP2X01_UART1_PHYS_BASE,
		.membase	= (char *)IXDP2X01_UART1_VIRT_BASE,
		.irq		= IRQ_IXDP2X01_UART1,
		.flags		= UPF_BOOT_AUTOCONF | UPF_SKIP_TEST,
		.iotype		= UPIO_MEM32,
		.regshift	= 2,
		.uartclk	= IXDP2X01_UART_CLK,
	},
	{ }
};

static struct resource ixdp2x01_uart_resource1 = {
	.start		= IXDP2X01_UART1_PHYS_BASE,
	.end		= IXDP2X01_UART1_PHYS_BASE + 0xffff,
	.flags		= IORESOURCE_MEM,
};

static struct platform_device ixdp2x01_serial_device1 = {
	.name		= "serial8250",
	.id		= PLAT8250_DEV_PLATFORM1,
	.dev		= {
		.platform_data		= ixdp2x01_serial_port1,
	},
	.num_resources	= 1,
	.resource	= &ixdp2x01_uart_resource1,
};

static struct plat_serial8250_port ixdp2x01_serial_port2[] = {
	{
		.mapbase	= (unsigned long)IXDP2X01_UART2_PHYS_BASE,
		.membase	= (char *)IXDP2X01_UART2_VIRT_BASE,
		.irq		= IRQ_IXDP2X01_UART2,
		.flags		= UPF_BOOT_AUTOCONF | UPF_SKIP_TEST,
		.iotype		= UPIO_MEM32,
		.regshift	= 2,
		.uartclk	= IXDP2X01_UART_CLK,
	}, 
	{ }
};

static struct resource ixdp2x01_uart_resource2 = {
	.start		= IXDP2X01_UART2_PHYS_BASE,
	.end		= IXDP2X01_UART2_PHYS_BASE + 0xffff,
	.flags		= IORESOURCE_MEM,
};

static struct platform_device ixdp2x01_serial_device2 = {
	.name		= "serial8250",
	.id		= PLAT8250_DEV_PLATFORM2,
	.dev		= {
		.platform_data		= ixdp2x01_serial_port2,
	},
	.num_resources	= 1,
	.resource	= &ixdp2x01_uart_resource2,
};

static void ixdp2x01_uart_init(void)
{
	platform_device_register(&ixdp2x01_serial_device1);
	platform_device_register(&ixdp2x01_serial_device2);
}


/*************************************************************************
 * IXDP2x01 timer tick configuration
 *************************************************************************/
static unsigned int ixdp2x01_clock;

static int __init ixdp2x01_clock_setup(char *str)
{
	ixdp2x01_clock = simple_strtoul(str, NULL, 10);

	return 1;
}

__setup("ixdp2x01_clock=", ixdp2x01_clock_setup);

static void __init ixdp2x01_timer_init(void)
{
	if (!ixdp2x01_clock)
		ixdp2x01_clock = 50000000;

	ixp2000_init_time(ixdp2x01_clock);
}

static struct sys_timer ixdp2x01_timer = {
	.init		= ixdp2x01_timer_init,
	.offset		= ixp2000_gettimeoffset,
};

/*************************************************************************
 * IXDP2x01 PCI
 *************************************************************************/
void __init ixdp2x01_pci_preinit(void)
{
	ixp2000_reg_write(IXP2000_PCI_ADDR_EXT, 0x00000000);
	ixp2000_pci_preinit();
	pcibios_setup("firmware");
}

#define DEVPIN(dev, pin) ((pin) | ((dev) << 3))

static int __init ixdp2x01_pci_map_irq(const struct pci_dev *dev, u8 slot,
	u8 pin)
{
	u8 bus = dev->bus->number;
	u32 devpin = DEVPIN(PCI_SLOT(dev->devfn), pin);
	struct pci_bus *tmp_bus = dev->bus;

	/* Primary bus, no interrupts here */
	if (bus == 0) {
		return -1;
	}

	/* Lookup first leaf in bus tree */
	while ((tmp_bus->parent != NULL) && (tmp_bus->parent->parent != NULL)) {
		tmp_bus = tmp_bus->parent;
	}

	/* Select between known bridges */
	switch (tmp_bus->self->devfn | (tmp_bus->self->bus->number << 8)) {
	/* Device is located after first MB bridge */
	case 0x0008:
		if (tmp_bus == dev->bus) {
			/* Device is located directly after first MB bridge */
			switch (devpin) {
			case DEVPIN(1, 1):	/* Onboard 82546 ch 0 */
				if (machine_is_ixdp2401())
					return IRQ_IXDP2401_INTA_82546;
				return -1;
			case DEVPIN(1, 2):	/* Onboard 82546 ch 1 */
				if (machine_is_ixdp2401())
					return IRQ_IXDP2401_INTB_82546;
				return -1;
			case DEVPIN(0, 1):	/* PMC INTA# */
				return IRQ_IXDP2X01_SPCI_PMC_INTA;
			case DEVPIN(0, 2):	/* PMC INTB# */
				return IRQ_IXDP2X01_SPCI_PMC_INTB;
			case DEVPIN(0, 3):	/* PMC INTC# */
				return IRQ_IXDP2X01_SPCI_PMC_INTC;
			case DEVPIN(0, 4):	/* PMC INTD# */
				return IRQ_IXDP2X01_SPCI_PMC_INTD;
			}
		}
		break;
	case 0x0010:
		if (tmp_bus == dev->bus) {
			/* Device is located directly after second MB bridge */
			/* Secondary bus of second bridge */
			switch (devpin) {
			case DEVPIN(0, 1):	/* DB#0 */
				return IRQ_IXDP2X01_SPCI_DB_0;
			case DEVPIN(1, 1):	/* DB#1 */
				return IRQ_IXDP2X01_SPCI_DB_1;
			}
		} else {
			/* Device is located indirectly after second MB bridge */
			/* Not supported now */
		}
		break;
	}

	return -1;
}


static int ixdp2x01_pci_setup(int nr, struct pci_sys_data *sys)
{
	sys->mem_offset = 0xe0000000;

	if (machine_is_ixdp2801() || machine_is_ixdp28x5())
		sys->mem_offset -= ((*IXP2000_PCI_ADDR_EXT & 0xE000) << 16);

	return ixp2000_pci_setup(nr, sys);
}

struct hw_pci ixdp2x01_pci __initdata = {
	.nr_controllers	= 1,
	.setup		= ixdp2x01_pci_setup,
	.preinit	= ixdp2x01_pci_preinit,
	.scan		= ixp2000_pci_scan_bus,
	.map_irq	= ixdp2x01_pci_map_irq,
};

int __init ixdp2x01_pci_init(void)
{
	if (machine_is_ixdp2401() || machine_is_ixdp2801() ||\
		machine_is_ixdp28x5())
		pci_common_init(&ixdp2x01_pci);

	return 0;
}

subsys_initcall(ixdp2x01_pci_init);

/*************************************************************************
 * IXDP2x01 Machine Initialization
 *************************************************************************/
static struct flash_platform_data ixdp2x01_flash_platform_data = {
	.map_name	= "cfi_probe",
	.width		= 1,
};

static unsigned long ixdp2x01_flash_bank_setup(unsigned long ofs)
{
	ixp2000_reg_wrb(IXDP2X01_CPLD_FLASH_REG,
		((ofs >> IXDP2X01_FLASH_WINDOW_BITS) | IXDP2X01_CPLD_FLASH_INTERN));
	return (ofs & IXDP2X01_FLASH_WINDOW_MASK);
}

static struct ixp2000_flash_data ixdp2x01_flash_data = {
	.platform_data	= &ixdp2x01_flash_platform_data,
	.bank_setup	= ixdp2x01_flash_bank_setup
};

static struct resource ixdp2x01_flash_resource = {
	.start		= 0xc4000000,
	.end		= 0xc4000000 + 0x01ffffff,
	.flags		= IORESOURCE_MEM,
};

static struct platform_device ixdp2x01_flash = {
	.name		= "IXP2000-Flash",
	.id		= 0,
	.dev		= {
		.platform_data = &ixdp2x01_flash_data,
	},
	.num_resources	= 1,
	.resource	= &ixdp2x01_flash_resource,
};

static struct ixp2000_i2c_pins ixdp2x01_i2c_gpio_pins = {
	.sda_pin	= IXDP2X01_GPIO_SDA,
	.scl_pin	= IXDP2X01_GPIO_SCL,
};

static struct platform_device ixdp2x01_i2c_controller = {
	.name		= "IXP2000-I2C",
	.id		= 0,
	.dev		= {
		.platform_data = &ixdp2x01_i2c_gpio_pins,
	},
	.num_resources	= 0
};

static struct platform_device *ixdp2x01_devices[] __initdata = {
	&ixdp2x01_flash,
	&ixdp2x01_i2c_controller
};

static void __init ixdp2x01_init_machine(void)
{
	ixp2000_reg_wrb(IXDP2X01_CPLD_FLASH_REG,
		(IXDP2X01_CPLD_FLASH_BANK_MASK | IXDP2X01_CPLD_FLASH_INTERN));
	
	ixdp2x01_flash_data.nr_banks =
		((*IXDP2X01_CPLD_FLASH_REG & IXDP2X01_CPLD_FLASH_BANK_MASK) + 1);

	platform_add_devices(ixdp2x01_devices, ARRAY_SIZE(ixdp2x01_devices));
	ixp2000_uart_init();
	ixdp2x01_uart_init();
}


#ifdef CONFIG_ARCH_IXDP2401
MACHINE_START(IXDP2401, "Intel IXDP2401 Development Platform")
	/* Maintainer: MontaVista Software, Inc. */
	.atag_offset	= 0x100,
	.map_io		= ixdp2x01_map_io,
	.init_irq	= ixdp2x01_init_irq,
	.timer		= &ixdp2x01_timer,
	.init_machine	= ixdp2x01_init_machine,
MACHINE_END
#endif

#ifdef CONFIG_ARCH_IXDP2801
MACHINE_START(IXDP2801, "Intel IXDP2801 Development Platform")
	/* Maintainer: MontaVista Software, Inc. */
	.atag_offset	= 0x100,
	.map_io		= ixdp2x01_map_io,
	.init_irq	= ixdp2x01_init_irq,
	.timer		= &ixdp2x01_timer,
	.init_machine	= ixdp2x01_init_machine,
MACHINE_END

/*
 * IXDP28x5 is basically an IXDP2801 with a different CPU but Intel
 * changed the machine ID in the bootloader
 */
MACHINE_START(IXDP28X5, "Intel IXDP2805/2855 Development Platform")
	/* Maintainer: MontaVista Software, Inc. */
	.atag_offset	= 0x100,
	.map_io		= ixdp2x01_map_io,
	.init_irq	= ixdp2x01_init_irq,
	.timer		= &ixdp2x01_timer,
	.init_machine	= ixdp2x01_init_machine,
MACHINE_END
#endif


