// SPDX-License-Identifier: GPL-2.0
/*
 * arch/arm/mach-ixp4xx/ixdp425-setup.c
 *
 * IXDP425/IXCDP1100 board-setup
 *
 * Copyright (C) 2003-2005 MontaVista Software, Inc.
 *
 * Author: Deepak Saxena <dsaxena@plexity.net>
 */

#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/device.h>
#include <linux/serial.h>
#include <linux/tty.h>
#include <linux/serial_8250.h>
#include <linux/gpio/machine.h>
#include <linux/io.h>
#include <linux/mtd/mtd.h>
#include <linux/mtd/rawnand.h>
#include <linux/mtd/partitions.h>
#include <linux/mtd/platnand.h>
#include <linux/delay.h>
#include <linux/gpio.h>
#include <asm/types.h>
#include <asm/setup.h>
#include <asm/memory.h>
#include <mach/hardware.h>
#include <asm/mach-types.h>
#include <asm/irq.h>
#include <asm/mach/arch.h>
#include <asm/mach/flash.h>

#define IXDP425_SDA_PIN		7
#define IXDP425_SCL_PIN		6

/* NAND Flash pins */
#define IXDP425_NAND_NCE_PIN	12

#define IXDP425_NAND_CMD_BYTE	0x01
#define IXDP425_NAND_ADDR_BYTE	0x02

static struct flash_platform_data ixdp425_flash_data = {
	.map_name	= "cfi_probe",
	.width		= 2,
};

static struct resource ixdp425_flash_resource = {
	.flags		= IORESOURCE_MEM,
};

static struct platform_device ixdp425_flash = {
	.name		= "IXP4XX-Flash",
	.id		= 0,
	.dev		= {
		.platform_data = &ixdp425_flash_data,
	},
	.num_resources	= 1,
	.resource	= &ixdp425_flash_resource,
};

#if defined(CONFIG_MTD_NAND_PLATFORM) || \
    defined(CONFIG_MTD_NAND_PLATFORM_MODULE)

static struct mtd_partition ixdp425_partitions[] = {
	{
		.name	= "ixp400 NAND FS 0",
		.offset	= 0,
		.size 	= SZ_8M
	}, {
		.name	= "ixp400 NAND FS 1",
		.offset	= MTDPART_OFS_APPEND,
		.size	= MTDPART_SIZ_FULL
	},
};

static void
ixdp425_flash_nand_cmd_ctrl(struct nand_chip *this, int cmd, unsigned int ctrl)
{
	int offset = (int)nand_get_controller_data(this);

	if (ctrl & NAND_CTRL_CHANGE) {
		if (ctrl & NAND_NCE) {
			gpio_set_value(IXDP425_NAND_NCE_PIN, 0);
			udelay(5);
		} else
			gpio_set_value(IXDP425_NAND_NCE_PIN, 1);

		offset = (ctrl & NAND_CLE) ? IXDP425_NAND_CMD_BYTE : 0;
		offset |= (ctrl & NAND_ALE) ? IXDP425_NAND_ADDR_BYTE : 0;
		nand_set_controller_data(this, (void *)offset);
	}

	if (cmd != NAND_CMD_NONE)
		writeb(cmd, this->legacy.IO_ADDR_W + offset);
}

static struct platform_nand_data ixdp425_flash_nand_data = {
	.chip = {
		.nr_chips		= 1,
		.chip_delay		= 30,
		.partitions	 	= ixdp425_partitions,
		.nr_partitions	 	= ARRAY_SIZE(ixdp425_partitions),
	},
	.ctrl = {
		.cmd_ctrl 		= ixdp425_flash_nand_cmd_ctrl
	}
};

static struct resource ixdp425_flash_nand_resource = {
	.flags		= IORESOURCE_MEM,
};

static struct platform_device ixdp425_flash_nand = {
	.name		= "gen_nand",
	.id		= -1,
	.dev		= {
		.platform_data = &ixdp425_flash_nand_data,
	},
	.num_resources	= 1,
	.resource	= &ixdp425_flash_nand_resource,
};
#endif	/* CONFIG_MTD_NAND_PLATFORM */

static struct gpiod_lookup_table ixdp425_i2c_gpiod_table = {
	.dev_id		= "i2c-gpio.0",
	.table		= {
		GPIO_LOOKUP_IDX("IXP4XX_GPIO_CHIP", IXDP425_SDA_PIN,
				NULL, 0, GPIO_ACTIVE_HIGH | GPIO_OPEN_DRAIN),
		GPIO_LOOKUP_IDX("IXP4XX_GPIO_CHIP", IXDP425_SCL_PIN,
				NULL, 1, GPIO_ACTIVE_HIGH | GPIO_OPEN_DRAIN),
	},
};

static struct platform_device ixdp425_i2c_gpio = {
	.name		= "i2c-gpio",
	.id		= 0,
	.dev	 = {
		.platform_data	= NULL,
	},
};

static struct resource ixdp425_uart_resources[] = {
	{
		.start		= IXP4XX_UART1_BASE_PHYS,
		.end		= IXP4XX_UART1_BASE_PHYS + 0x0fff,
		.flags		= IORESOURCE_MEM
	},
	{
		.start		= IXP4XX_UART2_BASE_PHYS,
		.end		= IXP4XX_UART2_BASE_PHYS + 0x0fff,
		.flags		= IORESOURCE_MEM
	}
};

static struct plat_serial8250_port ixdp425_uart_data[] = {
	{
		.mapbase	= IXP4XX_UART1_BASE_PHYS,
		.membase	= (char *)IXP4XX_UART1_BASE_VIRT + REG_OFFSET,
		.irq		= IRQ_IXP4XX_UART1,
		.flags		= UPF_BOOT_AUTOCONF | UPF_SKIP_TEST,
		.iotype		= UPIO_MEM,
		.regshift	= 2,
		.uartclk	= IXP4XX_UART_XTAL,
	},
	{
		.mapbase	= IXP4XX_UART2_BASE_PHYS,
		.membase	= (char *)IXP4XX_UART2_BASE_VIRT + REG_OFFSET,
		.irq		= IRQ_IXP4XX_UART2,
		.flags		= UPF_BOOT_AUTOCONF | UPF_SKIP_TEST,
		.iotype		= UPIO_MEM,
		.regshift	= 2,
		.uartclk	= IXP4XX_UART_XTAL,
	},
	{ },
};

static struct platform_device ixdp425_uart = {
	.name			= "serial8250",
	.id			= PLAT8250_DEV_PLATFORM,
	.dev.platform_data	= ixdp425_uart_data,
	.num_resources		= 2,
	.resource		= ixdp425_uart_resources
};

/* Built-in 10/100 Ethernet MAC interfaces */
static struct eth_plat_info ixdp425_plat_eth[] = {
	{
		.phy		= 0,
		.rxq		= 3,
		.txreadyq	= 20,
	}, {
		.phy		= 1,
		.rxq		= 4,
		.txreadyq	= 21,
	}
};

static struct platform_device ixdp425_eth[] = {
	{
		.name			= "ixp4xx_eth",
		.id			= IXP4XX_ETH_NPEB,
		.dev.platform_data	= ixdp425_plat_eth,
	}, {
		.name			= "ixp4xx_eth",
		.id			= IXP4XX_ETH_NPEC,
		.dev.platform_data	= ixdp425_plat_eth + 1,
	}
};

static struct platform_device *ixdp425_devices[] __initdata = {
	&ixdp425_i2c_gpio,
	&ixdp425_flash,
#if defined(CONFIG_MTD_NAND_PLATFORM) || \
    defined(CONFIG_MTD_NAND_PLATFORM_MODULE)
	&ixdp425_flash_nand,
#endif
	&ixdp425_uart,
	&ixdp425_eth[0],
	&ixdp425_eth[1],
};

static void __init ixdp425_init(void)
{
	ixp4xx_sys_init();

	ixdp425_flash_resource.start = IXP4XX_EXP_BUS_BASE(0);
	ixdp425_flash_resource.end =
		IXP4XX_EXP_BUS_BASE(0) + ixp4xx_exp_bus_size - 1;

#if defined(CONFIG_MTD_NAND_PLATFORM) || \
    defined(CONFIG_MTD_NAND_PLATFORM_MODULE)
	ixdp425_flash_nand_resource.start = IXP4XX_EXP_BUS_BASE(3),
	ixdp425_flash_nand_resource.end   = IXP4XX_EXP_BUS_BASE(3) + 0x10 - 1;

	gpio_request(IXDP425_NAND_NCE_PIN, "NAND NCE pin");
	gpio_direction_output(IXDP425_NAND_NCE_PIN, 0);

	/* Configure expansion bus for NAND Flash */
	*IXP4XX_EXP_CS3 = IXP4XX_EXP_BUS_CS_EN |
			  IXP4XX_EXP_BUS_STROBE_T(1) |	/* extend by 1 clock */
			  IXP4XX_EXP_BUS_CYCLES(0) |	/* Intel cycles */
			  IXP4XX_EXP_BUS_SIZE(0) |	/* 512bytes addr space*/
			  IXP4XX_EXP_BUS_WR_EN |
			  IXP4XX_EXP_BUS_BYTE_EN;	/* 8 bit data bus */
#endif

	if (cpu_is_ixp43x()) {
		ixdp425_uart.num_resources = 1;
		ixdp425_uart_data[1].flags = 0;
	}

	gpiod_add_lookup_table(&ixdp425_i2c_gpiod_table);
	platform_add_devices(ixdp425_devices, ARRAY_SIZE(ixdp425_devices));
}

#ifdef CONFIG_ARCH_IXDP425
MACHINE_START(IXDP425, "Intel IXDP425 Development Platform")
	/* Maintainer: MontaVista Software, Inc. */
	.map_io		= ixp4xx_map_io,
	.init_early	= ixp4xx_init_early,
	.init_irq	= ixp4xx_init_irq,
	.init_time	= ixp4xx_timer_init,
	.atag_offset	= 0x100,
	.init_machine	= ixdp425_init,
#if defined(CONFIG_PCI)
	.dma_zone_size	= SZ_64M,
#endif
	.restart	= ixp4xx_restart,
MACHINE_END
#endif

#ifdef CONFIG_MACH_IXDP465
MACHINE_START(IXDP465, "Intel IXDP465 Development Platform")
	/* Maintainer: MontaVista Software, Inc. */
	.map_io		= ixp4xx_map_io,
	.init_early	= ixp4xx_init_early,
	.init_irq	= ixp4xx_init_irq,
	.init_time	= ixp4xx_timer_init,
	.atag_offset	= 0x100,
	.init_machine	= ixdp425_init,
#if defined(CONFIG_PCI)
	.dma_zone_size	= SZ_64M,
#endif
MACHINE_END
#endif

#ifdef CONFIG_ARCH_PRPMC1100
MACHINE_START(IXCDP1100, "Intel IXCDP1100 Development Platform")
	/* Maintainer: MontaVista Software, Inc. */
	.map_io		= ixp4xx_map_io,
	.init_early	= ixp4xx_init_early,
	.init_irq	= ixp4xx_init_irq,
	.init_time	= ixp4xx_timer_init,
	.atag_offset	= 0x100,
	.init_machine	= ixdp425_init,
#if defined(CONFIG_PCI)
	.dma_zone_size	= SZ_64M,
#endif
MACHINE_END
#endif

#ifdef CONFIG_MACH_KIXRP435
MACHINE_START(KIXRP435, "Intel KIXRP435 Reference Platform")
	/* Maintainer: MontaVista Software, Inc. */
	.map_io		= ixp4xx_map_io,
	.init_early	= ixp4xx_init_early,
	.init_irq	= ixp4xx_init_irq,
	.init_time	= ixp4xx_timer_init,
	.atag_offset	= 0x100,
	.init_machine	= ixdp425_init,
#if defined(CONFIG_PCI)
	.dma_zone_size	= SZ_64M,
#endif
MACHINE_END
#endif
