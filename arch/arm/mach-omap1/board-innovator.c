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
#include <linux/mtd/physmap.h>
#include <linux/input.h>
#include <linux/smc91x.h>

#include <mach/hardware.h>
#include <asm/mach-types.h>
#include <asm/mach/arch.h>
#include <asm/mach/map.h>

#include <plat/mux.h>
#include <plat/flash.h>
#include <plat/fpga.h>
#include <mach/gpio.h>
#include <plat/tc.h>
#include <plat/usb.h>
#include <plat/keypad.h>
#include <plat/common.h>
#include <plat/mmc.h>

/* At OMAP1610 Innovator the Ethernet is directly connected to CS1 */
#define INNOVATOR1610_ETHR_START	0x04000300

static const unsigned int innovator_keymap[] = {
	KEY(0, 0, KEY_F1),
	KEY(3, 0, KEY_DOWN),
	KEY(1, 1, KEY_F2),
	KEY(2, 1, KEY_RIGHT),
	KEY(0, 2, KEY_F3),
	KEY(1, 2, KEY_F4),
	KEY(2, 2, KEY_UP),
	KEY(2, 3, KEY_ENTER),
	KEY(3, 3, KEY_LEFT),
};

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

static struct physmap_flash_data innovator_flash_data = {
	.width		= 2,
	.set_vpp	= omap1_set_vpp,
	.parts		= innovator_partitions,
	.nr_parts	= ARRAY_SIZE(innovator_partitions),
};

static struct resource innovator_flash_resource = {
	.start		= OMAP_CS0_PHYS,
	.end		= OMAP_CS0_PHYS + SZ_32M - 1,
	.flags		= IORESOURCE_MEM,
};

static struct platform_device innovator_flash_device = {
	.name		= "physmap-flash",
	.id		= 0,
	.dev		= {
		.platform_data	= &innovator_flash_data,
	},
	.num_resources	= 1,
	.resource	= &innovator_flash_resource,
};

static struct resource innovator_kp_resources[] = {
	[0] = {
		.start	= INT_KEYBOARD,
		.end	= INT_KEYBOARD,
		.flags	= IORESOURCE_IRQ,
	},
};

static const struct matrix_keymap_data innovator_keymap_data = {
	.keymap		= innovator_keymap,
	.keymap_size	= ARRAY_SIZE(innovator_keymap),
};

static struct omap_kp_platform_data innovator_kp_data = {
	.rows		= 8,
	.cols		= 8,
	.keymap_data	= &innovator_keymap_data,
	.delay		= 4,
};

static struct platform_device innovator_kp_device = {
	.name		= "omap-keypad",
	.id		= -1,
	.dev		= {
		.platform_data = &innovator_kp_data,
	},
	.num_resources	= ARRAY_SIZE(innovator_kp_resources),
	.resource	= innovator_kp_resources,
};

static struct smc91x_platdata innovator_smc91x_info = {
	.flags	= SMC91X_USE_16BIT | SMC91X_NOWAIT,
	.leda	= RPC_LED_100_10,
	.ledb	= RPC_LED_TX_RX,
};

#ifdef CONFIG_ARCH_OMAP15XX

#include <linux/spi/spi.h>
#include <linux/spi/ads7846.h>


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
		.flags	= IORESOURCE_IRQ | IORESOURCE_IRQ_HIGHEDGE,
	},
};

static struct platform_device innovator1510_smc91x_device = {
	.name		= "smc91x",
	.id		= 0,
	.dev	= {
		.platform_data	= &innovator_smc91x_info,
	},
	.num_resources	= ARRAY_SIZE(innovator1510_smc91x_resources),
	.resource	= innovator1510_smc91x_resources,
};

static struct platform_device innovator1510_lcd_device = {
	.name		= "lcd_inn1510",
	.id		= -1,
};

static struct platform_device innovator1510_spi_device = {
	.name		= "spi_inn1510",
	.id		= -1,
};

static struct platform_device *innovator1510_devices[] __initdata = {
	&innovator_flash_device,
	&innovator1510_smc91x_device,
	&innovator_kp_device,
	&innovator1510_lcd_device,
	&innovator1510_spi_device,
};

static int innovator_get_pendown_state(void)
{
	return !(fpga_read(OMAP1510_FPGA_TOUCHSCREEN) & (1 << 5));
}

static const struct ads7846_platform_data innovator1510_ts_info = {
	.model			= 7846,
	.vref_delay_usecs	= 100,	/* internal, no capacitor */
	.x_plate_ohms		= 419,
	.y_plate_ohms		= 486,
	.get_pendown_state	= innovator_get_pendown_state,
};

static struct spi_board_info __initdata innovator1510_boardinfo[] = { {
	/* FPGA (bus "10") CS0 has an ads7846e */
	.modalias		= "ads7846",
	.platform_data		= &innovator1510_ts_info,
	.irq			= OMAP1510_INT_FPGA_TS,
	.max_speed_hz		= 120000 /* max sample rate at 3V */
					* 26 /* command + data + overhead */,
	.bus_num		= 10,
	.chip_select		= 0,
} };

#endif /* CONFIG_ARCH_OMAP15XX */

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
		.flags	= IORESOURCE_IRQ | IORESOURCE_IRQ_LOWEDGE,
	},
};

static struct platform_device innovator1610_smc91x_device = {
	.name		= "smc91x",
	.id		= 0,
	.dev	= {
		.platform_data	= &innovator_smc91x_info,
	},
	.num_resources	= ARRAY_SIZE(innovator1610_smc91x_resources),
	.resource	= innovator1610_smc91x_resources,
};

static struct platform_device innovator1610_lcd_device = {
	.name		= "inn1610_lcd",
	.id		= -1,
};

static struct platform_device *innovator1610_devices[] __initdata = {
	&innovator_flash_device,
	&innovator1610_smc91x_device,
	&innovator_kp_device,
	&innovator1610_lcd_device,
};

#endif /* CONFIG_ARCH_OMAP16XX */

static void __init innovator_init_smc91x(void)
{
	if (cpu_is_omap1510()) {
		fpga_write(fpga_read(OMAP1510_FPGA_RST) & ~1,
			   OMAP1510_FPGA_RST);
		udelay(750);
	} else {
		if (gpio_request(0, "SMC91x irq") < 0) {
			printk("Error requesting gpio 0 for smc91x irq\n");
			return;
		}
	}
}

static void __init innovator_init_irq(void)
{
	omap1_init_common_hw();
	omap1_init_irq();
}

#ifdef CONFIG_ARCH_OMAP15XX
static struct omap_usb_config innovator1510_usb_config __initdata = {
	/* for bundled non-standard host and peripheral cables */
	.hmc_mode	= 4,

	.register_host	= 1,
	.pins[1]	= 6,
	.pins[2]	= 6,		/* Conflicts with UART2 */

	.register_dev	= 1,
	.pins[0]	= 2,
};

static struct omap_lcd_config innovator1510_lcd_config __initdata = {
	.ctrl_name	= "internal",
};
#endif

#ifdef CONFIG_ARCH_OMAP16XX
static struct omap_usb_config h2_usb_config __initdata = {
	/* usb1 has a Mini-AB port and external isp1301 transceiver */
	.otg		= 2,

#ifdef	CONFIG_USB_GADGET_OMAP
	.hmc_mode	= 19,	/* 0:host(off) 1:dev|otg 2:disabled */
	/* .hmc_mode	= 21,*/	/* 0:host(off) 1:dev(loopback) 2:host(loopback) */
#elif	defined(CONFIG_USB_OHCI_HCD) || defined(CONFIG_USB_OHCI_HCD_MODULE)
	/* NONSTANDARD CABLE NEEDED (B-to-Mini-B) */
	.hmc_mode	= 20,	/* 1:dev|otg(off) 1:host 2:disabled */
#endif

	.pins[1]	= 3,
};

static struct omap_lcd_config innovator1610_lcd_config __initdata = {
	.ctrl_name	= "internal",
};
#endif

#if defined(CONFIG_MMC_OMAP) || defined(CONFIG_MMC_OMAP_MODULE)

static int mmc_set_power(struct device *dev, int slot, int power_on,
				int vdd)
{
	if (power_on)
		fpga_write(fpga_read(OMAP1510_FPGA_POWER) | (1 << 3),
				OMAP1510_FPGA_POWER);
	else
		fpga_write(fpga_read(OMAP1510_FPGA_POWER) & ~(1 << 3),
				OMAP1510_FPGA_POWER);

	return 0;
}

/*
 * Innovator could use the following functions tested:
 * - mmc_get_wp that uses OMAP_MPUIO(3)
 * - mmc_get_cover_state that uses FPGA F4 UIO43
 */
static struct omap_mmc_platform_data mmc1_data = {
	.nr_slots                       = 1,
	.slots[0]       = {
		.set_power		= mmc_set_power,
		.wires			= 4,
		.name                   = "mmcblk",
	},
};

static struct omap_mmc_platform_data *mmc_data[OMAP16XX_NR_MMC];

static void __init innovator_mmc_init(void)
{
	mmc_data[0] = &mmc1_data;
	omap1_init_mmc(mmc_data, OMAP15XX_NR_MMC);
}

#else
static inline void innovator_mmc_init(void)
{
}
#endif

static struct omap_board_config_kernel innovator_config[] = {
	{ OMAP_TAG_LCD,		NULL },
};

static void __init innovator_init(void)
{
	if (cpu_is_omap1510())
		omap1510_fpga_init_irq();
	innovator_init_smc91x();

#ifdef CONFIG_ARCH_OMAP15XX
	if (cpu_is_omap1510()) {
		unsigned char reg;

		/* mux pins for uarts */
		omap_cfg_reg(UART1_TX);
		omap_cfg_reg(UART1_RTS);
		omap_cfg_reg(UART2_TX);
		omap_cfg_reg(UART2_RTS);
		omap_cfg_reg(UART3_TX);
		omap_cfg_reg(UART3_RX);

		reg = fpga_read(OMAP1510_FPGA_POWER);
		reg |= OMAP1510_FPGA_PCR_COM1_EN;
		fpga_write(reg, OMAP1510_FPGA_POWER);
		udelay(10);

		reg = fpga_read(OMAP1510_FPGA_POWER);
		reg |= OMAP1510_FPGA_PCR_COM2_EN;
		fpga_write(reg, OMAP1510_FPGA_POWER);
		udelay(10);

		platform_add_devices(innovator1510_devices, ARRAY_SIZE(innovator1510_devices));
		spi_register_board_info(innovator1510_boardinfo,
				ARRAY_SIZE(innovator1510_boardinfo));
	}
#endif
#ifdef CONFIG_ARCH_OMAP16XX
	if (!cpu_is_omap1510()) {
		platform_add_devices(innovator1610_devices, ARRAY_SIZE(innovator1610_devices));
	}
#endif

#ifdef CONFIG_ARCH_OMAP15XX
	if (cpu_is_omap1510()) {
		omap1_usb_init(&innovator1510_usb_config);
		innovator_config[1].data = &innovator1510_lcd_config;
	}
#endif
#ifdef CONFIG_ARCH_OMAP16XX
	if (cpu_is_omap1610()) {
		omap1_usb_init(&h2_usb_config);
		innovator_config[1].data = &innovator1610_lcd_config;
	}
#endif
	omap_board_config = innovator_config;
	omap_board_config_size = ARRAY_SIZE(innovator_config);
	omap_serial_init();
	omap_register_i2c_bus(1, 100, NULL, 0);
	innovator_mmc_init();
}

static void __init innovator_map_io(void)
{
	omap1_map_common_io();

#ifdef CONFIG_ARCH_OMAP15XX
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
}

MACHINE_START(OMAP_INNOVATOR, "TI-Innovator")
	/* Maintainer: MontaVista Software, Inc. */
	.boot_params	= 0x10000100,
	.map_io		= innovator_map_io,
	.reserve	= omap_reserve,
	.init_irq	= innovator_init_irq,
	.init_machine	= innovator_init,
	.timer		= &omap1_timer,
MACHINE_END
