/* arch/arm/mach-lh7a40x/arch-lpd7a40x.c
 *
 *  Copyright (C) 2004 Logic Product Development
 *
 *  This program is free software; you can redistribute it and/or
 *  modify it under the terms of the GNU General Public License
 *  version 2 as published by the Free Software Foundation.
 *
 */

#include <linux/tty.h>
#include <linux/init.h>
#include <linux/platform_device.h>
#include <linux/interrupt.h>
#include <linux/irq.h>

#include <mach/hardware.h>
#include <asm/setup.h>
#include <asm/mach-types.h>
#include <asm/mach/arch.h>
#include <asm/irq.h>
#include <asm/mach/irq.h>
#include <asm/mach/map.h>

#include "common.h"

#define CPLD_INT_NETHERNET	(1<<0)
#define CPLD_INTMASK_ETHERNET	(1<<2)
#if defined (CONFIG_MACH_LPD7A400)
# define CPLD_INT_NTOUCH		(1<<1)
# define CPLD_INTMASK_TOUCH	(1<<3)
# define CPLD_INT_PEN		(1<<4)
# define CPLD_INTMASK_PEN	(1<<4)
# define CPLD_INT_PIRQ		(1<<4)
#endif
#define CPLD_INTMASK_CPLD	(1<<7)
#define CPLD_INT_CPLD		(1<<6)

#define CPLD_CONTROL_SWINT		(1<<7) /* Disable all CPLD IRQs */
#define CPLD_CONTROL_OCMSK		(1<<6) /* Mask USB1 connect IRQ */
#define CPLD_CONTROL_PDRV		(1<<5) /* PCC_nDRV high */
#define CPLD_CONTROL_USB1C		(1<<4) /* USB1 connect IRQ active */
#define CPLD_CONTROL_USB1P		(1<<3) /* USB1 power disable */
#define CPLD_CONTROL_AWKP		(1<<2) /* Auto-wakeup disabled  */
#define CPLD_CONTROL_LCD_ENABLE		(1<<1) /* LCD Vee enable */
#define CPLD_CONTROL_WRLAN_NENABLE	(1<<0) /* SMC91x power disable */


static struct resource smc91x_resources[] = {
	[0] = {
		.start	= CPLD00_PHYS,
		.end	= CPLD00_PHYS + CPLD00_SIZE - 1, /* Only needs 16B */
		.flags	= IORESOURCE_MEM,
	},

	[1] = {
		.start	= IRQ_LPD7A40X_ETH_INT,
		.end	= IRQ_LPD7A40X_ETH_INT,
		.flags	= IORESOURCE_IRQ,
	},

};

static struct platform_device smc91x_device = {
	.name		= "smc91x",
	.id		= 0,
	.num_resources	= ARRAY_SIZE(smc91x_resources),
	.resource	= smc91x_resources,
};

static struct resource lh7a40x_usbclient_resources[] = {
	[0] = {
		.start	= USB_PHYS,
		.end	= (USB_PHYS + PAGE_SIZE),
		.flags	= IORESOURCE_MEM,
	},
	[1] = {
		.start	= IRQ_USB,
		.end	= IRQ_USB,
		.flags	= IORESOURCE_IRQ,
	},
};

static u64 lh7a40x_usbclient_dma_mask = 0xffffffffUL;

static struct platform_device lh7a40x_usbclient_device = {
//	.name		= "lh7a40x_udc",
	.name		= "lh7-udc",
	.id		= 0,
	.dev		= {
		.dma_mask = &lh7a40x_usbclient_dma_mask,
		.coherent_dma_mask = 0xffffffffUL,
	},
	.num_resources	= ARRAY_SIZE (lh7a40x_usbclient_resources),
	.resource	= lh7a40x_usbclient_resources,
};

#if defined (CONFIG_ARCH_LH7A404)

static struct resource lh7a404_usbhost_resources [] = {
	[0] = {
		.start	= USBH_PHYS,
		.end	= (USBH_PHYS + 0xFF),
		.flags	= IORESOURCE_MEM,
	},
	[1] = {
		.start	= IRQ_USHINTR,
		.end	= IRQ_USHINTR,
		.flags	= IORESOURCE_IRQ,
	},
};

static u64 lh7a404_usbhost_dma_mask = 0xffffffffUL;

static struct platform_device lh7a404_usbhost_device = {
	.name		= "lh7a404-ohci",
	.id		= 0,
	.dev		= {
		.dma_mask = &lh7a404_usbhost_dma_mask,
		.coherent_dma_mask = 0xffffffffUL,
	},
	.num_resources	= ARRAY_SIZE (lh7a404_usbhost_resources),
	.resource	= lh7a404_usbhost_resources,
};

#endif

static struct platform_device* lpd7a40x_devs[] __initdata = {
	&smc91x_device,
	&lh7a40x_usbclient_device,
#if defined (CONFIG_ARCH_LH7A404)
	&lh7a404_usbhost_device,
#endif
};

extern void lpd7a400_map_io (void);

static void __init lpd7a40x_init (void)
{
#if defined (CONFIG_MACH_LPD7A400)
	CPLD_CONTROL |= 0
		| CPLD_CONTROL_SWINT /* Disable software interrupt */
		| CPLD_CONTROL_OCMSK; /* Mask USB1 connection IRQ */
	CPLD_CONTROL &= ~(0
			  | CPLD_CONTROL_LCD_ENABLE	/* Disable LCD */
			  | CPLD_CONTROL_WRLAN_NENABLE	/* Enable SMC91x */
		);
#endif

#if defined (CONFIG_MACH_LPD7A404)
	CPLD_CONTROL &= ~(0
			  | CPLD_CONTROL_WRLAN_NENABLE	/* Enable SMC91x */
		);
#endif

	platform_add_devices (lpd7a40x_devs, ARRAY_SIZE (lpd7a40x_devs));
#if defined (CONFIG_FB_ARMCLCD)
        lh7a40x_clcd_init ();
#endif
}

static void lh7a40x_ack_cpld_irq (u32 irq)
{
	/* CPLD doesn't have ack capability, but some devices may */

#if defined (CPLD_INTMASK_TOUCH)
	/* The touch control *must* mask the interrupt because the
	 * interrupt bit is read by the driver to determine if the pen
	 * is still down. */
	if (irq == IRQ_TOUCH)
		CPLD_INTERRUPTS |= CPLD_INTMASK_TOUCH;
#endif
}

static void lh7a40x_mask_cpld_irq (u32 irq)
{
	switch (irq) {
	case IRQ_LPD7A40X_ETH_INT:
		CPLD_INTERRUPTS |= CPLD_INTMASK_ETHERNET;
		break;
#if defined (IRQ_TOUCH)
	case IRQ_TOUCH:
		CPLD_INTERRUPTS |= CPLD_INTMASK_TOUCH;
		break;
#endif
	}
}

static void lh7a40x_unmask_cpld_irq (u32 irq)
{
	switch (irq) {
	case IRQ_LPD7A40X_ETH_INT:
		CPLD_INTERRUPTS &= ~CPLD_INTMASK_ETHERNET;
		break;
#if defined (IRQ_TOUCH)
	case IRQ_TOUCH:
		CPLD_INTERRUPTS &= ~CPLD_INTMASK_TOUCH;
		break;
#endif
	}
}

static struct irq_chip lpd7a40x_cpld_chip = {
	.name	= "CPLD",
	.ack	= lh7a40x_ack_cpld_irq,
	.mask	= lh7a40x_mask_cpld_irq,
	.unmask	= lh7a40x_unmask_cpld_irq,
};

static void lpd7a40x_cpld_handler (unsigned int irq, struct irq_desc *desc)
{
	unsigned int mask = CPLD_INTERRUPTS;

	desc->chip->ack (irq);

	if ((mask & (1<<0)) == 0)	/* WLAN */
		generic_handle_irq(IRQ_LPD7A40X_ETH_INT);

#if defined (IRQ_TOUCH)
	if ((mask & (1<<1)) == 0)	/* Touch */
		generic_handle_irq(IRQ_TOUCH);
#endif

	desc->chip->unmask (irq); /* Level-triggered need this */
}


void __init lh7a40x_init_board_irq (void)
{
	int irq;

		/* Rev A (v2.8): PF0, PF1, PF2, and PF3 are available IRQs.
		                 PF7 supports the CPLD.
		   Rev B (v3.4): PF0, PF1, and PF2 are available IRQs.
		                 PF3 supports the CPLD.
		   (Some) LPD7A404 prerelease boards report a version
		   number of 0x16, but we force an override since the
		   hardware is of the newer variety.
		*/

	unsigned char cpld_version = CPLD_REVISION;
	int pinCPLD = (cpld_version == 0x28) ? 7 : 3;

#if defined CONFIG_MACH_LPD7A404
	cpld_version = 0x34;	/* Coerce LPD7A404 to RevB */
#endif

		/* First, configure user controlled GPIOF interrupts  */

	GPIO_PFDD	&= ~0x0f; /* PF0-3 are inputs */
	GPIO_INTTYPE1	&= ~0x0f; /* PF0-3 are level triggered */
	GPIO_INTTYPE2	&= ~0x0f; /* PF0-3 are active low */
	barrier ();
	GPIO_GPIOFINTEN |=  0x0f; /* Enable PF0, PF1, PF2, and PF3 IRQs */

		/* Then, configure CPLD interrupt */

			/* Disable all CPLD interrupts */
#if defined (CONFIG_MACH_LPD7A400)
	CPLD_INTERRUPTS	= CPLD_INTMASK_TOUCH | CPLD_INTMASK_PEN
		| CPLD_INTMASK_ETHERNET;
	/* *** FIXME: don't know why we need 7 and 4. 7 is way wrong
               and 4 is uncefined. */
	// (1<<7)|(1<<4)|(1<<3)|(1<<2);
#endif
#if defined (CONFIG_MACH_LPD7A404)
	CPLD_INTERRUPTS	= CPLD_INTMASK_ETHERNET;
	/* *** FIXME: don't know why we need 6 and 5, neither is defined. */
	// (1<<6)|(1<<5)|(1<<3);
#endif
	GPIO_PFDD	&= ~(1 << pinCPLD); /* Make input */
	GPIO_INTTYPE1	&= ~(1 << pinCPLD); /* Level triggered */
	GPIO_INTTYPE2	&= ~(1 << pinCPLD); /* Active low */
	barrier ();
	GPIO_GPIOFINTEN |=  (1 << pinCPLD); /* Enable */

		/* Cascade CPLD interrupts */

	for (irq = IRQ_BOARD_START;
	     irq < IRQ_BOARD_START + NR_IRQ_BOARD; ++irq) {
		set_irq_chip (irq, &lpd7a40x_cpld_chip);
		set_irq_handler (irq, handle_level_irq);
		set_irq_flags (irq, IRQF_VALID);
	}

	set_irq_chained_handler ((cpld_version == 0x28)
				 ? IRQ_CPLD_V28
				 : IRQ_CPLD_V34,
				 lpd7a40x_cpld_handler);
}

static struct map_desc lpd7a40x_io_desc[] __initdata = {
	{
		.virtual	= IO_VIRT,
		.pfn		= __phys_to_pfn(IO_PHYS),
		.length		= IO_SIZE,
		.type		= MT_DEVICE
	},
	{	/* Mapping added to work around chip select problems */
		.virtual	= IOBARRIER_VIRT,
		.pfn		= __phys_to_pfn(IOBARRIER_PHYS),
		.length		= IOBARRIER_SIZE,
		.type		= MT_DEVICE
	},
	{
		.virtual	= CF_VIRT,
		.pfn		= __phys_to_pfn(CF_PHYS),
		.length		= CF_SIZE,
		.type		= MT_DEVICE
	},
	{
		.virtual	= CPLD02_VIRT,
		.pfn		= __phys_to_pfn(CPLD02_PHYS),
		.length		= CPLD02_SIZE,
		.type		= MT_DEVICE
	},
	{
		.virtual	= CPLD06_VIRT,
		.pfn		= __phys_to_pfn(CPLD06_PHYS),
		.length		= CPLD06_SIZE,
		.type		= MT_DEVICE
	},
	{
		.virtual	= CPLD08_VIRT,
		.pfn		= __phys_to_pfn(CPLD08_PHYS),
		.length		= CPLD08_SIZE,
		.type		= MT_DEVICE
	},
	{
		.virtual	= CPLD08_VIRT,
		.pfn		= __phys_to_pfn(CPLD08_PHYS),
		.length		= CPLD08_SIZE,
		.type		= MT_DEVICE
	},
	{
		.virtual	= CPLD0A_VIRT,
		.pfn		= __phys_to_pfn(CPLD0A_PHYS),
		.length		= CPLD0A_SIZE,
		.type		= MT_DEVICE
	},
	{
		.virtual	= CPLD0C_VIRT,
		.pfn		= __phys_to_pfn(CPLD0C_PHYS),
		.length		= CPLD0C_SIZE,
		.type		= MT_DEVICE
	},
	{
		.virtual	= CPLD0E_VIRT,
		.pfn		= __phys_to_pfn(CPLD0E_PHYS),
		.length		= CPLD0E_SIZE,
		.type		= MT_DEVICE
	},
	{
		.virtual	= CPLD10_VIRT,
		.pfn		= __phys_to_pfn(CPLD10_PHYS),
		.length		= CPLD10_SIZE,
		.type		= MT_DEVICE
	},
	{
		.virtual	= CPLD12_VIRT,
		.pfn		= __phys_to_pfn(CPLD12_PHYS),
		.length		= CPLD12_SIZE,
		.type		= MT_DEVICE
	},
	{
		.virtual	= CPLD14_VIRT,
		.pfn		= __phys_to_pfn(CPLD14_PHYS),
		.length		= CPLD14_SIZE,
		.type		= MT_DEVICE
	},
	{
		.virtual	= CPLD16_VIRT,
		.pfn		= __phys_to_pfn(CPLD16_PHYS),
		.length		= CPLD16_SIZE,
		.type		= MT_DEVICE
	},
	{
		.virtual	= CPLD18_VIRT,
		.pfn		= __phys_to_pfn(CPLD18_PHYS),
		.length		= CPLD18_SIZE,
		.type		= MT_DEVICE
	},
	{
		.virtual	= CPLD1A_VIRT,
		.pfn		= __phys_to_pfn(CPLD1A_PHYS),
		.length		= CPLD1A_SIZE,
		.type		= MT_DEVICE
	},
};

void __init
lpd7a40x_map_io(void)
{
	iotable_init (lpd7a40x_io_desc, ARRAY_SIZE (lpd7a40x_io_desc));
}

#ifdef CONFIG_MACH_LPD7A400

MACHINE_START (LPD7A400, "Logic Product Development LPD7A400-10")
	/* Maintainer: Marc Singer */
	.boot_params	= 0xc0000100,
	.map_io		= lpd7a40x_map_io,
	.init_irq	= lh7a400_init_irq,
	.timer		= &lh7a40x_timer,
	.init_machine	= lpd7a40x_init,
MACHINE_END

#endif

#ifdef CONFIG_MACH_LPD7A404

MACHINE_START (LPD7A404, "Logic Product Development LPD7A404-10")
	/* Maintainer: Marc Singer */
	.boot_params	= 0xc0000100,
	.map_io		= lpd7a40x_map_io,
	.init_irq	= lh7a404_init_irq,
	.timer		= &lh7a40x_timer,
	.init_machine	= lpd7a40x_init,
MACHINE_END

#endif
