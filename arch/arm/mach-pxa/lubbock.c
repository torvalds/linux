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
#include <linux/device.h>
#include <linux/sysdev.h>
#include <linux/major.h>
#include <linux/fb.h>
#include <linux/interrupt.h>

#include <asm/setup.h>
#include <asm/memory.h>
#include <asm/mach-types.h>
#include <asm/hardware.h>
#include <asm/irq.h>

#include <asm/mach/arch.h>
#include <asm/mach/map.h>
#include <asm/mach/irq.h>

#include <asm/hardware/sa1111.h>

#include <asm/arch/pxa-regs.h>
#include <asm/arch/lubbock.h>
#include <asm/arch/udc.h>
#include <asm/arch/pxafb.h>
#include <asm/arch/mmc.h>

#include "generic.h"


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

static struct irqchip lubbock_irq_chip = {
	.ack		= lubbock_mask_irq,
	.mask		= lubbock_mask_irq,
	.unmask		= lubbock_unmask_irq,
};

static void lubbock_irq_handler(unsigned int irq, struct irqdesc *desc,
				struct pt_regs *regs)
{
	unsigned long pending = LUB_IRQ_SET_CLR & lubbock_irq_enabled;
	do {
		GEDR(0) = GPIO_bit(0);	/* clear our parent irq */
		if (likely(pending)) {
			irq = LUBBOCK_IRQ(0) + __ffs(pending);
			desc = irq_desc + irq;
			desc_handle_irq(irq, desc, regs);
		}
		pending = LUB_IRQ_SET_CLR & lubbock_irq_enabled;
	} while (pending);
}

static void __init lubbock_init_irq(void)
{
	int irq;

	pxa_init_irq();

	/* setup extra lubbock irqs */
	for (irq = LUBBOCK_IRQ(0); irq <= LUBBOCK_LAST_IRQ; irq++) {
		set_irq_chip(irq, &lubbock_irq_chip);
		set_irq_handler(irq, do_level_IRQ);
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
		.start	= 0x0c000000,
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

static struct platform_device smc91x_device = {
	.name		= "smc91x",
	.id		= -1,
	.num_resources	= ARRAY_SIZE(smc91x_resources),
	.resource	= smc91x_resources,
};

static struct platform_device *devices[] __initdata = {
	&sa1111_device,
	&lub_audio_device,
	&smc91x_device,
};

static struct pxafb_mach_info sharp_lm8v31 __initdata = {
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
	.cmap_inverse	= 0,
	.cmap_static	= 0,
	.lccr0		= LCCR0_SDS,
	.lccr3		= LCCR3_PCP | LCCR3_Acb(255),
};

static int lubbock_mci_init(struct device *dev, irqreturn_t (*lubbock_detect_int)(int, void *, struct pt_regs *), void *data)
{
	/* setup GPIO for PXA25x MMC controller	*/
	pxa_gpio_mode(GPIO6_MMCCLK_MD);
	pxa_gpio_mode(GPIO8_MMCCS0_MD);

	return 0;
}

static struct pxamci_platform_data lubbock_mci_platform_data = {
	.ocr_mask	= MMC_VDD_32_33|MMC_VDD_33_34,
	.init 		= lubbock_mci_init,
};

static void __init lubbock_init(void)
{
	pxa_set_udc_info(&udc_info);
	set_pxa_fb_info(&sharp_lm8v31);
	pxa_set_mci_info(&lubbock_mci_platform_data);
	(void) platform_add_devices(devices, ARRAY_SIZE(devices));
}

static struct map_desc lubbock_io_desc[] __initdata = {
  { LUBBOCK_FPGA_VIRT, LUBBOCK_FPGA_PHYS, 0x00100000, MT_DEVICE }, /* CPLD */
};

static void __init lubbock_map_io(void)
{
	pxa_map_io();
	iotable_init(lubbock_io_desc, ARRAY_SIZE(lubbock_io_desc));

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
	.phys_ram	= 0xa0000000,
	.phys_io	= 0x40000000,
	.io_pg_offst	= (io_p2v(0x40000000) >> 18) & 0xfffc,
	.map_io		= lubbock_map_io,
	.init_irq	= lubbock_init_irq,
	.timer		= &pxa_timer,
	.init_machine	= lubbock_init,
MACHINE_END
