/*
 *  Copyright 2008 Freescale Semiconductor, Inc. All Rights Reserved.
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
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

#include <linux/types.h>
#include <linux/init.h>
#include <linux/clk.h>
#include <linux/irq.h>
#include <linux/gpio.h>
#include <linux/smsc911x.h>
#include <linux/platform_device.h>

#include <mach/hardware.h>
#include <asm/mach-types.h>
#include <asm/mach/arch.h>
#include <asm/mach/time.h>
#include <asm/memory.h>
#include <asm/mach/map.h>
#include <mach/common.h>
#include <mach/board-mx31pdk.h>
#include <mach/imx-uart.h>
#include <mach/iomux-mx3.h>
#include "devices.h"

/*!
 * @file mx31pdk.c
 *
 * @brief This file contains the board-specific initialization routines.
 *
 * @ingroup System
 */

static int mx31pdk_pins[] = {
	/* UART1 */
	MX31_PIN_CTS1__CTS1,
	MX31_PIN_RTS1__RTS1,
	MX31_PIN_TXD1__TXD1,
	MX31_PIN_RXD1__RXD1,
	IOMUX_MODE(MX31_PIN_GPIO1_1, IOMUX_CONFIG_GPIO),
};

static struct imxuart_platform_data uart_pdata = {
	.flags = IMXUART_HAVE_RTSCTS,
};

/*
 * Support for the SMSC9217 on the Debug board.
 */

static struct smsc911x_platform_config smsc911x_config = {
	.irq_polarity	= SMSC911X_IRQ_POLARITY_ACTIVE_LOW,
	.irq_type	= SMSC911X_IRQ_TYPE_PUSH_PULL,
	.flags		= SMSC911X_USE_16BIT | SMSC911X_FORCE_INTERNAL_PHY,
	.phy_interface	= PHY_INTERFACE_MODE_MII,
};

static struct resource smsc911x_resources[] = {
	{
		.start		= LAN9217_BASE_ADDR,
		.end		= LAN9217_BASE_ADDR + 0xff,
		.flags		= IORESOURCE_MEM,
	}, {
		.start		= EXPIO_INT_ENET,
		.end		= EXPIO_INT_ENET,
		.flags		= IORESOURCE_IRQ,
	},
};

static struct platform_device smsc911x_device = {
	.name		= "smsc911x",
	.id		= -1,
	.num_resources	= ARRAY_SIZE(smsc911x_resources),
	.resource	= smsc911x_resources,
	.dev		= {
		.platform_data = &smsc911x_config,
	},
};

/*
 * Routines for the CPLD on the debug board. It contains a CPLD handling
 * LEDs, switches, interrupts for Ethernet.
 */

static void mx31pdk_expio_irq_handler(uint32_t irq, struct irq_desc *desc)
{
	uint32_t imr_val;
	uint32_t int_valid;
	uint32_t expio_irq;

	imr_val = __raw_readw(CPLD_INT_MASK_REG);
	int_valid = __raw_readw(CPLD_INT_STATUS_REG) & ~imr_val;

	expio_irq = MXC_EXP_IO_BASE;
	for (; int_valid != 0; int_valid >>= 1, expio_irq++) {
		if ((int_valid & 1) == 0)
			continue;
		generic_handle_irq(expio_irq);
	}
}

/*
 * Disable an expio pin's interrupt by setting the bit in the imr.
 * @param irq           an expio virtual irq number
 */
static void expio_mask_irq(uint32_t irq)
{
	uint16_t reg;
	uint32_t expio = MXC_IRQ_TO_EXPIO(irq);

	/* mask the interrupt */
	reg = __raw_readw(CPLD_INT_MASK_REG);
	reg |= 1 << expio;
	__raw_writew(reg, CPLD_INT_MASK_REG);
}

/*
 * Acknowledge an expanded io pin's interrupt by clearing the bit in the isr.
 * @param irq           an expanded io virtual irq number
 */
static void expio_ack_irq(uint32_t irq)
{
	uint32_t expio = MXC_IRQ_TO_EXPIO(irq);

	/* clear the interrupt status */
	__raw_writew(1 << expio, CPLD_INT_RESET_REG);
	__raw_writew(0, CPLD_INT_RESET_REG);
	/* mask the interrupt */
	expio_mask_irq(irq);
}

/*
 * Enable a expio pin's interrupt by clearing the bit in the imr.
 * @param irq           a expio virtual irq number
 */
static void expio_unmask_irq(uint32_t irq)
{
	uint16_t reg;
	uint32_t expio = MXC_IRQ_TO_EXPIO(irq);

	/* unmask the interrupt */
	reg = __raw_readw(CPLD_INT_MASK_REG);
	reg &= ~(1 << expio);
	__raw_writew(reg, CPLD_INT_MASK_REG);
}

static struct irq_chip expio_irq_chip = {
	.ack = expio_ack_irq,
	.mask = expio_mask_irq,
	.unmask = expio_unmask_irq,
};

static int __init mx31pdk_init_expio(void)
{
	int i;
	int ret;

	/* Check if there's a debug board connected */
	if ((__raw_readw(CPLD_MAGIC_NUMBER1_REG) != 0xAAAA) ||
	    (__raw_readw(CPLD_MAGIC_NUMBER2_REG) != 0x5555) ||
	    (__raw_readw(CPLD_MAGIC_NUMBER3_REG) != 0xCAFE)) {
		/* No Debug board found */
		return -ENODEV;
	}

	pr_info("i.MX31PDK Debug board detected, rev = 0x%04X\n",
		__raw_readw(CPLD_CODE_VER_REG));

	/*
	 * Configure INT line as GPIO input
	 */
	ret = gpio_request(IOMUX_TO_GPIO(MX31_PIN_GPIO1_1), "sms9217-irq");
	if (ret)
		pr_warning("could not get LAN irq gpio\n");
	else
		gpio_direction_input(IOMUX_TO_GPIO(MX31_PIN_GPIO1_1));

	/* Disable the interrupts and clear the status */
	__raw_writew(0, CPLD_INT_MASK_REG);
	__raw_writew(0xFFFF, CPLD_INT_RESET_REG);
	__raw_writew(0, CPLD_INT_RESET_REG);
	__raw_writew(0x1F, CPLD_INT_MASK_REG);
	for (i = MXC_EXP_IO_BASE;
	     i < (MXC_EXP_IO_BASE + MXC_MAX_EXP_IO_LINES);
	     i++) {
		set_irq_chip(i, &expio_irq_chip);
		set_irq_handler(i, handle_level_irq);
		set_irq_flags(i, IRQF_VALID);
	}
	set_irq_type(EXPIO_PARENT_INT, IRQ_TYPE_LEVEL_LOW);
	set_irq_chained_handler(EXPIO_PARENT_INT, mx31pdk_expio_irq_handler);

	return 0;
}

/*
 * This structure defines the MX31 memory map.
 */
static struct map_desc mx31pdk_io_desc[] __initdata = {
	{
		.virtual = MX31_CS5_BASE_ADDR_VIRT,
		.pfn = __phys_to_pfn(MX31_CS5_BASE_ADDR),
		.length = MX31_CS5_SIZE,
		.type = MT_DEVICE,
	},
};

/*
 * Set up static virtual mappings.
 */
static void __init mx31pdk_map_io(void)
{
	mx31_map_io();
	iotable_init(mx31pdk_io_desc, ARRAY_SIZE(mx31pdk_io_desc));
}

/*!
 * Board specific initialization.
 */
static void __init mxc_board_init(void)
{
	mxc_iomux_setup_multiple_pins(mx31pdk_pins, ARRAY_SIZE(mx31pdk_pins),
				      "mx31pdk");

	mxc_register_device(&mxc_uart_device0, &uart_pdata);

	if (!mx31pdk_init_expio())
		platform_device_register(&smsc911x_device);
}

static void __init mx31pdk_timer_init(void)
{
	mx31_clocks_init(26000000);
}

static struct sys_timer mx31pdk_timer = {
	.init	= mx31pdk_timer_init,
};

/*
 * The following uses standard kernel macros defined in arch.h in order to
 * initialize __mach_desc_MX31PDK data structure.
 */
MACHINE_START(MX31_3DS, "Freescale MX31PDK (3DS)")
	/* Maintainer: Freescale Semiconductor, Inc. */
	.phys_io	= MX31_AIPS1_BASE_ADDR,
	.io_pg_offst	= (MX31_AIPS1_BASE_ADDR_VIRT >> 18) & 0xfffc,
	.boot_params    = MX3x_PHYS_OFFSET + 0x100,
	.map_io         = mx31pdk_map_io,
	.init_irq       = mx31_init_irq,
	.init_machine   = mxc_board_init,
	.timer          = &mx31pdk_timer,
MACHINE_END
