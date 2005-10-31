/*
 * linux/arch/arm/mach-omap1/board-innovator.c
 *
 * Board specific inits for OMAP-1510 and OMAP-1610 Innovator
 *
 * Copyright (C) 2001 RidgeRun, Inc.
 * Author: Greg Lonnon <glonnon@ridgerun.com>
 *
 * Copyright (C) 2002 MontaVista Software, Inc.
 *
 * Separated FPGA interrupts from innovator1510.c and cleaned up for 2.6
 * Copyright (C) 2004 Nokia Corporation by Tony Lindrgen <tony@atomide.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/platform_device.h>
#include <linux/delay.h>
#include <linux/mtd/mtd.h>
#include <linux/mtd/partitions.h>

#include <asm/hardware.h>
#include <asm/mach-types.h>
#include <asm/mach/arch.h>
#include <asm/mach/flash.h>
#include <asm/mach/map.h>

#include <asm/arch/mux.h>
#include <asm/arch/fpga.h>
#include <asm/arch/gpio.h>
#include <asm/arch/tc.h>
#include <asm/arch/usb.h>
#include <asm/arch/common.h>

static int __initdata innovator_serial_ports[OMAP_MAX_NR_PORTS] = {1, 1, 1};

static struct mtd_partition innovator_partitions[] = {
	/* bootloader (U-Boot, etc) in first sector */
	{
	      .name		= "bootloader",
	      .offset		= 0,
	      .size		= SZ_128K,
	      .mask_flags	= MTD_WRITEABLE, /* force read-only */
	},
	/* bootloader params in the next sector */
	{
	      .name		= "params",
	      .offset		= MTDPART_OFS_APPEND,
	      .size		= SZ_128K,
	      .mask_flags	= 0,
	},
	/* kernel */
	{
	      .name		= "kernel",
	      .offset		= MTDPART_OFS_APPEND,
	      .size		= SZ_2M,
	      .mask_flags	= 0
	},
	/* rest of flash1 is a file system */
	{
	      .name		= "rootfs",
	      .offset		= MTDPART_OFS_APPEND,
	      .size		= SZ_16M - SZ_2M - 2 * SZ_128K,
	      .mask_flags	= 0
	},
	/* file system */
	{
	      .name		= "filesystem",
	      .offset		= MTDPART_OFS_APPEND,
	      .size		= MTDPART_SIZ_FULL,
	      .mask_flags	= 0
	}
};

static struct flash_platform_data innovator_flash_data = {
	.map_name	= "cfi_probe",
	.width		= 2,
	.parts		= innovator_partitions,
	.nr_parts	= ARRAY_SIZE(innovator_partitions),
};

static struct resource innovator_flash_resource = {
	.start		= OMAP_CS0_PHYS,
	.end		= OMAP_CS0_PHYS + SZ_32M - 1,
	.flags		= IORESOURCE_MEM,
};

static struct platform_device innovator_flash_device = {
	.name		= "omapflash",
	.id		= 0,
	.dev		= {
		.platform_data	= &innovator_flash_data,
	},
	.num_resources	= 1,
	.resource	= &innovator_flash_resource,
};

#ifdef CONFIG_ARCH_OMAP1510

/* Only FPGA needs to be mapped here. All others are done with ioremap */
static struct map_desc innovator1510_io_desc[] __initdata = {
	{
		.virtual	= OMAP1510_FPGA_BASE,
		.pfn		= __phys_to_pfn(OMAP1510_FPGA_START),
		.length		= OMAP1510_FPGA_SIZE,
		.type		= MT_DEVICE
	}
};

static struct resource innovator1510_smc91x_resources[] = {
	[0] = {
		.start	= OMAP1510_FPGA_ETHR_START,	/* Physical */
		.end	= OMAP1510_FPGA_ETHR_START + 0xf,
		.flags	= IORESOURCE_MEM,
	},
	[1] = {
		.start	= OMAP1510_INT_ETHER,
		.end	= OMAP1510_INT_ETHER,
		.flags	= IORESOURCE_IRQ,
	},
};

static struct platform_device innovator1510_smc91x_device = {
	.name		= "smc91x",
	.id		= 0,
	.num_resources	= ARRAY_SIZE(innovator1510_smc91x_resources),
	.resource	= innovator1510_smc91x_resources,
};

static struct platform_device *innovator1510_devices[] __initdata = {
	&innovator_flash_device,
	&innovator1510_smc91x_device,
};

#endif /* CONFIG_ARCH_OMAP1510 */

#ifdef CONFIG_ARCH_OMAP16XX

static struct resource innovator1610_smc91x_resources[] = {
	[0] = {
		.start	= INNOVATOR1610_ETHR_START,		/* Physical */
		.end	= INNOVATOR1610_ETHR_START + 0xf,
		.flags	= IORESOURCE_MEM,
	},
	[1] = {
		.start	= OMAP_GPIO_IRQ(0),
		.end	= OMAP_GPIO_IRQ(0),
		.flags	= IORESOURCE_IRQ,
	},
};

static struct platform_device innovator1610_smc91x_device = {
	.name		= "smc91x",
	.id		= 0,
	.num_resources	= ARRAY_SIZE(innovator1610_smc91x_resources),
	.resource	= innovator1610_smc91x_resources,
};

static struct platform_device *innovator1610_devices[] __initdata = {
	&innovator_flash_device,
	&innovator1610_smc91x_device,
};

#endif /* CONFIG_ARCH_OMAP16XX */

static void __init innovator_init_smc91x(void)
{
	if (cpu_is_omap1510()) {
		fpga_write(fpga_read(OMAP1510_FPGA_RST) & ~1,
			   OMAP1510_FPGA_RST);
		udelay(750);
	} else {
		if ((omap_request_gpio(0)) < 0) {
			printk("Error requesting gpio 0 for smc91x irq\n");
			return;
		}
	}
}

void innovator_init_irq(void)
{
	omap_init_irq();
	omap_gpio_init();
#ifdef CONFIG_ARCH_OMAP1510
	if (cpu_is_omap1510()) {
		omap1510_fpga_init_irq();
	}
#endif
	innovator_init_smc91x();
}

#ifdef CONFIG_ARCH_OMAP1510
static struct omap_usb_config innovator1510_usb_config __initdata = {
	/* for bundled non-standard host and peripheral cables */
	.hmc_mode	= 4,

	.register_host	= 1,
	.pins[1]	= 6,
	.pins[2]	= 6,		/* Conflicts with UART2 */

	.register_dev	= 1,
	.pins[0]	= 2,
};
#endif

#ifdef CONFIG_ARCH_OMAP16XX
static struct omap_usb_config h2_usb_config __initdata = {
	/* usb1 has a Mini-AB port and external isp1301 transceiver */
	.otg		= 2,

#ifdef	CONFIG_USB_GADGET_OMAP
	.hmc_mode	= 19,	// 0:host(off) 1:dev|otg 2:disabled
	// .hmc_mode	= 21,	// 0:host(off) 1:dev(loopback) 2:host(loopback)
#elif	defined(CONFIG_USB_OHCI_HCD) || defined(CONFIG_USB_OHCI_HCD_MODULE)
	/* NONSTANDARD CABLE NEEDED (B-to-Mini-B) */
	.hmc_mode	= 20,	// 1:dev|otg(off) 1:host 2:disabled
#endif

	.pins[1]	= 3,
};
#endif

static struct omap_mmc_config innovator_mmc_config __initdata = {
	.mmc [0] = {
		.enabled 	= 1,
		.wire4		= 1,
		.wp_pin		= OMAP_MPUIO(3),
		.power_pin	= -1,	/* FPGA F3 UIO42 */
		.switch_pin	= -1,	/* FPGA F4 UIO43 */
	},
};

static struct omap_board_config_kernel innovator_config[] = {
	{ OMAP_TAG_USB,         NULL },
	{ OMAP_TAG_MMC,		&innovator_mmc_config },
};

static void __init innovator_init(void)
{
#ifdef CONFIG_ARCH_OMAP1510
	if (cpu_is_omap1510()) {
		platform_add_devices(innovator1510_devices, ARRAY_SIZE(innovator1510_devices));
	}
#endif
#ifdef CONFIG_ARCH_OMAP16XX
	if (!cpu_is_omap1510()) {
		platform_add_devices(innovator1610_devices, ARRAY_SIZE(innovator1610_devices));
	}
#endif

#ifdef CONFIG_ARCH_OMAP1510
	if (cpu_is_omap1510())
		innovator_config[0].data = &innovator1510_usb_config;
#endif
#ifdef CONFIG_ARCH_OMAP16XX
	if (cpu_is_omap1610())
		innovator_config[0].data = &h2_usb_config;
#endif
	omap_board_config = innovator_config;
	omap_board_config_size = ARRAY_SIZE(innovator_config);
}

static void __init innovator_map_io(void)
{
	omap_map_common_io();

#ifdef CONFIG_ARCH_OMAP1510
	if (cpu_is_omap1510()) {
		iotable_init(innovator1510_io_desc, ARRAY_SIZE(innovator1510_io_desc));
		udelay(10);	/* Delay needed for FPGA */

		/* Dump the Innovator FPGA rev early - useful info for support. */
		printk("Innovator FPGA Rev %d.%d Board Rev %d\n",
		       fpga_read(OMAP1510_FPGA_REV_HIGH),
		       fpga_read(OMAP1510_FPGA_REV_LOW),
		       fpga_read(OMAP1510_FPGA_BOARD_REV));
	}
#endif
	omap_serial_init(innovator_serial_ports);
}

MACHINE_START(OMAP_INNOVATOR, "TI-Innovator")
	/* Maintainer: MontaVista Software, Inc. */
	.phys_ram	= 0x10000000,
	.phys_io	= 0xfff00000,
	.io_pg_offst	= ((0xfef00000) >> 18) & 0xfffc,
	.boot_params	= 0x10000100,
	.map_io		= innovator_map_io,
	.init_irq	= innovator_init_irq,
	.init_machine	= innovator_init,
	.timer		= &omap_timer,
MACHINE_END
