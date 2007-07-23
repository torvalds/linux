/*
 *  linux/arch/arm/mach-pxa/lubbock.c
 *
 *  Support for the Intel DBPXA250 Development Platform.
 *
 *  Author:	Nicolas Pitre
 *  Created:	Jun 15, 2001
 *  Copyright:	MontaVista Software Inc.
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License version 2 as
 *  published by the Free Software Foundation.
 */
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/platform_device.h>
#include <linux/sysdev.h>
#include <linux/major.h>
#include <linux/fb.h>
#include <linux/interrupt.h>
#include <linux/mtd/mtd.h>
#include <linux/mtd/partitions.h>

#include <linux/spi/spi.h>
#include <linux/spi/ads7846.h>
#include <asm/arch/pxa2xx_spi.h>

#include <asm/setup.h>
#include <asm/memory.h>
#include <asm/mach-types.h>
#include <asm/hardware.h>
#include <asm/irq.h>
#include <asm/sizes.h>

#include <asm/mach/arch.h>
#include <asm/mach/map.h>
#include <asm/mach/irq.h>
#include <asm/mach/flash.h>

#include <asm/hardware/sa1111.h>

#include <asm/arch/pxa-regs.h>
#include <asm/arch/lubbock.h>
#include <asm/arch/udc.h>
#include <asm/arch/irda.h>
#include <asm/arch/pxafb.h>
#include <asm/arch/mmc.h>

#include "generic.h"
#include "devices.h"


#define LUB_MISC_WR		__LUB_REG(LUBBOCK_FPGA_PHYS + 0x080)

void lubbock_set_misc_wr(unsigned int mask, unsigned int set)
{
	unsigned long flags;

	local_irq_save(flags);
	LUB_MISC_WR = (LUB_MISC_WR & ~mask) | (set & mask);
	local_irq_restore(flags);
}
EXPORT_SYMBOL(lubbock_set_misc_wr);

static unsigned long lubbock_irq_enabled;

static void lubbock_mask_irq(unsigned int irq)
{
	int lubbock_irq = (irq - LUBBOCK_IRQ(0));
	LUB_IRQ_MASK_EN = (lubbock_irq_enabled &= ~(1 << lubbock_irq));
}

static void lubbock_unmask_irq(unsigned int irq)
{
	int lubbock_irq = (irq - LUBBOCK_IRQ(0));
	/* the irq can be acknowledged only if deasserted, so it's done here */
	LUB_IRQ_SET_CLR &= ~(1 << lubbock_irq);
	LUB_IRQ_MASK_EN = (lubbock_irq_enabled |= (1 << lubbock_irq));
}

static struct irq_chip lubbock_irq_chip = {
	.name		= "FPGA",
	.ack		= lubbock_mask_irq,
	.mask		= lubbock_mask_irq,
	.unmask		= lubbock_unmask_irq,
};

static void lubbock_irq_handler(unsigned int irq, struct irq_desc *desc)
{
	unsigned long pending = LUB_IRQ_SET_CLR & lubbock_irq_enabled;
	do {
		GEDR(0) = GPIO_bit(0);	/* clear our parent irq */
		if (likely(pending)) {
			irq = LUBBOCK_IRQ(0) + __ffs(pending);
			desc = irq_desc + irq;
			desc_handle_irq(irq, desc);
		}
		pending = LUB_IRQ_SET_CLR & lubbock_irq_enabled;
	} while (pending);
}

static void __init lubbock_init_irq(void)
{
	int irq;

	pxa25x_init_irq();

	/* setup extra lubbock irqs */
	for (irq = LUBBOCK_IRQ(0); irq <= LUBBOCK_LAST_IRQ; irq++) {
		set_irq_chip(irq, &lubbock_irq_chip);
		set_irq_handler(irq, handle_level_irq);
		set_irq_flags(irq, IRQF_VALID | IRQF_PROBE);
	}

	set_irq_chained_handler(IRQ_GPIO(0), lubbock_irq_handler);
	set_irq_type(IRQ_GPIO(0), IRQT_FALLING);
}

#ifdef CONFIG_PM

static int lubbock_irq_resume(struct sys_device *dev)
{
	LUB_IRQ_MASK_EN = lubbock_irq_enabled;
	return 0;
}

static struct sysdev_class lubbock_irq_sysclass = {
	set_kset_name("cpld_irq"),
	.resume = lubbock_irq_resume,
};

static struct sys_device lubbock_irq_device = {
	.cls = &lubbock_irq_sysclass,
};

static int __init lubbock_irq_device_init(void)
{
	int ret = sysdev_class_register(&lubbock_irq_sysclass);
	if (ret == 0)
		ret = sysdev_register(&lubbock_irq_device);
	return ret;
}

device_initcall(lubbock_irq_device_init);

#endif

static int lubbock_udc_is_connected(void)
{
	return (LUB_MISC_RD & (1 << 9)) == 0;
}

static struct pxa2xx_udc_mach_info udc_info __initdata = {
	.udc_is_connected	= lubbock_udc_is_connected,
	// no D+ pullup; lubbock can't connect/disconnect in software
};

static struct platform_device lub_audio_device = {
	.name		= "pxa2xx-ac97",
	.id		= -1,
};

static struct resource sa1111_resources[] = {
	[0] = {
		.start	= 0x10000000,
		.end	= 0x10001fff,
		.flags	= IORESOURCE_MEM,
	},
	[1] = {
		.start	= LUBBOCK_SA1111_IRQ,
		.end	= LUBBOCK_SA1111_IRQ,
		.flags	= IORESOURCE_IRQ,
	},
};

static struct platform_device sa1111_device = {
	.name		= "sa1111",
	.id		= -1,
	.num_resources	= ARRAY_SIZE(sa1111_resources),
	.resource	= sa1111_resources,
};

static struct resource smc91x_resources[] = {
	[0] = {
		.name	= "smc91x-regs",
		.start	= 0x0c000c00,
		.end	= 0x0c0fffff,
		.flags	= IORESOURCE_MEM,
	},
	[1] = {
		.start	= LUBBOCK_ETH_IRQ,
		.end	= LUBBOCK_ETH_IRQ,
		.flags	= IORESOURCE_IRQ,
	},
	[2] = {
		.name	= "smc91x-attrib",
		.start	= 0x0e000000,
		.end	= 0x0e0fffff,
		.flags	= IORESOURCE_MEM,
	},
};

/* ADS7846 is connected through SSP ... and if your board has J5 populated,
 * you can select it to replace the ucb1400 by switching the touchscreen cable
 * (to J5) and poking board registers (as done below).  Else it's only useful
 * for the temperature sensors.
 */
static struct resource pxa_ssp_resources[] = {
	[0] = {
		.start	= __PREG(SSCR0_P(1)),
		.end	= __PREG(SSCR0_P(1)) + 0x14,
		.flags	= IORESOURCE_MEM,
	},
	[1] = {
		.start	= IRQ_SSP,
		.end	= IRQ_SSP,
		.flags	= IORESOURCE_IRQ,
	},
};

static struct pxa2xx_spi_master pxa_ssp_master_info = {
	.ssp_type	= PXA25x_SSP,
	.clock_enable	= CKEN_SSP,
	.num_chipselect	= 0,
};

static struct platform_device pxa_ssp = {
	.name		= "pxa2xx-spi",
	.id		= 1,
	.resource	= pxa_ssp_resources,
	.num_resources	= ARRAY_SIZE(pxa_ssp_resources),
	.dev = {
		.platform_data	= &pxa_ssp_master_info,
	},
};

static int lubbock_ads7846_pendown_state(void)
{
	/* TS_BUSY is bit 8 in LUB_MISC_RD, but pendown is irq-only */
	return 0;
}

static struct ads7846_platform_data ads_info = {
	.model			= 7846,
	.vref_delay_usecs	= 100,		/* internal, no cap */
	.get_pendown_state	= lubbock_ads7846_pendown_state,
	// .x_plate_ohms		= 500,	/* GUESS! */
	// .y_plate_ohms		= 500,	/* GUESS! */
};

static void ads7846_cs(u32 command)
{
	static const unsigned	TS_nCS = 1 << 11;
	lubbock_set_misc_wr(TS_nCS, (command == PXA2XX_CS_ASSERT) ? 0 : TS_nCS);
}

static struct pxa2xx_spi_chip ads_hw = {
	.tx_threshold		= 1,
	.rx_threshold		= 2,
	.cs_control		= ads7846_cs,
};

static struct spi_board_info spi_board_info[] __initdata = { {
	.modalias	= "ads7846",
	.platform_data	= &ads_info,
	.controller_data = &ads_hw,
	.irq		= LUBBOCK_BB_IRQ,
	.max_speed_hz	= 120000 /* max sample rate at 3V */
				* 26 /* command + data + overhead */,
	.bus_num	= 1,
	.chip_select	= 0,
},
};

static struct platform_device smc91x_device = {
	.name		= "smc91x",
	.id		= -1,
	.num_resources	= ARRAY_SIZE(smc91x_resources),
	.resource	= smc91x_resources,
};

static struct resource flash_resources[] = {
	[0] = {
		.start	= 0x00000000,
		.end	= SZ_64M - 1,
		.flags	= IORESOURCE_MEM,
	},
	[1] = {
		.start	= 0x04000000,
		.end	= 0x04000000 + SZ_64M - 1,
		.flags	= IORESOURCE_MEM,
	},
};

static struct mtd_partition lubbock_partitions[] = {
	{
		.name =		"Bootloader",
		.size =		0x00040000,
		.offset =	0,
		.mask_flags =	MTD_WRITEABLE  /* force read-only */
	},{
		.name =		"Kernel",
		.size =		0x00100000,
		.offset =	0x00040000,
	},{
		.name =		"Filesystem",
		.size =		MTDPART_SIZ_FULL,
		.offset =	0x00140000
	}
};

static struct flash_platform_data lubbock_flash_data[2] = {
	{
		.map_name	= "cfi_probe",
		.parts		= lubbock_partitions,
		.nr_parts	= ARRAY_SIZE(lubbock_partitions),
	}, {
		.map_name	= "cfi_probe",
		.parts		= NULL,
		.nr_parts	= 0,
	}
};

static struct platform_device lubbock_flash_device[2] = {
	{
		.name		= "pxa2xx-flash",
		.id		= 0,
		.dev = {
			.platform_data = &lubbock_flash_data[0],
		},
		.resource = &flash_resources[0],
		.num_resources = 1,
	},
	{
		.name		= "pxa2xx-flash",
		.id		= 1,
		.dev = {
			.platform_data = &lubbock_flash_data[1],
		},
		.resource = &flash_resources[1],
		.num_resources = 1,
	},
};

static struct platform_device *devices[] __initdata = {
	&sa1111_device,
	&lub_audio_device,
	&smc91x_device,
	&lubbock_flash_device[0],
	&lubbock_flash_device[1],
	&pxa_ssp,
};

static struct pxafb_mode_info sharp_lm8v31_mode = {
	.pixclock	= 270000,
	.xres		= 640,
	.yres		= 480,
	.bpp		= 16,
	.hsync_len	= 1,
	.left_margin	= 3,
	.right_margin	= 3,
	.vsync_len	= 1,
	.upper_margin	= 0,
	.lower_margin	= 0,
	.sync		= FB_SYNC_HOR_HIGH_ACT | FB_SYNC_VERT_HIGH_ACT,
	.cmap_greyscale	= 0,
};

static struct pxafb_mach_info sharp_lm8v31 = {
	.modes		= &sharp_lm8v31_mode,
	.num_modes	= 1,
	.cmap_inverse	= 0,
	.cmap_static	= 0,
	.lccr0		= LCCR0_SDS,
	.lccr3		= LCCR3_PCP | LCCR3_Acb(255),
};

#define	MMC_POLL_RATE		msecs_to_jiffies(1000)

static void lubbock_mmc_poll(unsigned long);
static irq_handler_t mmc_detect_int;

static struct timer_list mmc_timer = {
	.function	= lubbock_mmc_poll,
};

static void lubbock_mmc_poll(unsigned long data)
{
	unsigned long flags;

	/* clear any previous irq state, then ... */
	local_irq_save(flags);
	LUB_IRQ_SET_CLR &= ~(1 << 0);
	local_irq_restore(flags);

	/* poll until mmc/sd card is removed */
	if (LUB_IRQ_SET_CLR & (1 << 0))
		mod_timer(&mmc_timer, jiffies + MMC_POLL_RATE);
	else {
		(void) mmc_detect_int(LUBBOCK_SD_IRQ, (void *)data);
		enable_irq(LUBBOCK_SD_IRQ);
	}
}

static irqreturn_t lubbock_detect_int(int irq, void *data)
{
	/* IRQ is level triggered; disable, and poll for removal */
	disable_irq(irq);
	mod_timer(&mmc_timer, jiffies + MMC_POLL_RATE);

	return mmc_detect_int(irq, data);
}

static int lubbock_mci_init(struct device *dev,
		irq_handler_t detect_int,
		void *data)
{
	/* setup GPIO for PXA25x MMC controller	*/
	pxa_gpio_mode(GPIO6_MMCCLK_MD);
	pxa_gpio_mode(GPIO8_MMCCS0_MD);

	/* detect card insert/eject */
	mmc_detect_int = detect_int;
	init_timer(&mmc_timer);
	mmc_timer.data = (unsigned long) data;
	return request_irq(LUBBOCK_SD_IRQ, lubbock_detect_int,
			IRQF_SAMPLE_RANDOM, "lubbock-sd-detect", data);
}

static int lubbock_mci_get_ro(struct device *dev)
{
	return (LUB_MISC_RD & (1 << 2)) != 0;
}

static void lubbock_mci_exit(struct device *dev, void *data)
{
	free_irq(LUBBOCK_SD_IRQ, data);
	del_timer_sync(&mmc_timer);
}

static struct pxamci_platform_data lubbock_mci_platform_data = {
	.ocr_mask	= MMC_VDD_32_33|MMC_VDD_33_34,
	.detect_delay	= 1,
	.init 		= lubbock_mci_init,
	.get_ro		= lubbock_mci_get_ro,
	.exit 		= lubbock_mci_exit,
};

static void lubbock_irda_transceiver_mode(struct device *dev, int mode)
{
	unsigned long flags;

	local_irq_save(flags);
	if (mode & IR_SIRMODE) {
		LUB_MISC_WR &= ~(1 << 4);
	} else if (mode & IR_FIRMODE) {
		LUB_MISC_WR |= 1 << 4;
	}
	local_irq_restore(flags);
}

static struct pxaficp_platform_data lubbock_ficp_platform_data = {
	.transceiver_cap  = IR_SIRMODE | IR_FIRMODE,
	.transceiver_mode = lubbock_irda_transceiver_mode,
};

static void __init lubbock_init(void)
{
	int flashboot = (LUB_CONF_SWITCHES & 1);

	pxa_set_udc_info(&udc_info);
	set_pxa_fb_info(&sharp_lm8v31);
	pxa_set_mci_info(&lubbock_mci_platform_data);
	pxa_set_ficp_info(&lubbock_ficp_platform_data);

	lubbock_flash_data[0].width = lubbock_flash_data[1].width =
		(BOOT_DEF & 1) ? 2 : 4;
	/* Compensate for the nROMBT switch which swaps the flash banks */
	printk(KERN_NOTICE "Lubbock configured to boot from %s (bank %d)\n",
	       flashboot?"Flash":"ROM", flashboot);

	lubbock_flash_data[flashboot^1].name = "application-flash";
	lubbock_flash_data[flashboot].name = "boot-rom";
	(void) platform_add_devices(devices, ARRAY_SIZE(devices));

	spi_register_board_info(spi_board_info, ARRAY_SIZE(spi_board_info));
}

static struct map_desc lubbock_io_desc[] __initdata = {
  	{	/* CPLD */
		.virtual	=  LUBBOCK_FPGA_VIRT,
		.pfn		= __phys_to_pfn(LUBBOCK_FPGA_PHYS),
		.length		= 0x00100000,
		.type		= MT_DEVICE
	}
};

static void __init lubbock_map_io(void)
{
	pxa_map_io();
	iotable_init(lubbock_io_desc, ARRAY_SIZE(lubbock_io_desc));

	/* SSP data pins */
	pxa_gpio_mode(GPIO23_SCLK_MD);
	pxa_gpio_mode(GPIO25_STXD_MD);
	pxa_gpio_mode(GPIO26_SRXD_MD);

	/* This enables the BTUART */
	pxa_gpio_mode(GPIO42_BTRXD_MD);
	pxa_gpio_mode(GPIO43_BTTXD_MD);
	pxa_gpio_mode(GPIO44_BTCTS_MD);
	pxa_gpio_mode(GPIO45_BTRTS_MD);

	/* This is for the SMC chip select */
	pxa_gpio_mode(GPIO79_nCS_3_MD);

	/* setup sleep mode values */
	PWER  = 0x00000002;
	PFER  = 0x00000000;
	PRER  = 0x00000002;
	PGSR0 = 0x00008000;
	PGSR1 = 0x003F0202;
	PGSR2 = 0x0001C000;
	PCFR |= PCFR_OPDE;
}

MACHINE_START(LUBBOCK, "Intel DBPXA250 Development Platform (aka Lubbock)")
	/* Maintainer: MontaVista Software Inc. */
	.phys_io	= 0x40000000,
	.io_pg_offst	= (io_p2v(0x40000000) >> 18) & 0xfffc,
	.map_io		= lubbock_map_io,
	.init_irq	= lubbock_init_irq,
	.timer		= &pxa_timer,
	.init_machine	= lubbock_init,
MACHINE_END
