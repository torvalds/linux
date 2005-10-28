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
#include <linux/device.h>
#include <linux/interrupt.h>

#include <asm/hardware.h>
#include <asm/setup.h>
#include <asm/mach-types.h>
#include <asm/mach/arch.h>
#include <asm/irq.h>
#include <asm/mach/irq.h>
#include <asm/mach/map.h>

#include "common.h"

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
		.end	= (USB_PHYS + 0xFF),
		.flags	= IORESOURCE_MEM,
	},
	[1] = {
		.start	= IRQ_USBINTR,
		.end	= IRQ_USBINTR,
		.flags	= IORESOURCE_IRQ,
	},
};

static u64 lh7a40x_usbclient_dma_mask = 0xffffffffUL;

static struct platform_device lh7a40x_usbclient_device = {
	.name		= "lh7a40x_udc",
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

static struct platform_device *lpd7a40x_devs[] __initdata = {
	&smc91x_device,
	&lh7a40x_usbclient_device,
#if defined (CONFIG_ARCH_LH7A404)
	&lh7a404_usbhost_device,
#endif
};

extern void lpd7a400_map_io (void);

static void __init lpd7a40x_init (void)
{
	CPLD_CONTROL |=     (1<<6); /* Mask USB1 connection IRQ */
	CPLD_CONTROL &= ~(0
			  | (1<<1) /* Disable LCD */
			  | (1<<0) /* Enable WLAN */
		);

	platform_add_devices (lpd7a40x_devs, ARRAY_SIZE (lpd7a40x_devs));
}

static void lh7a40x_ack_cpld_irq (u32 irq)
{
	/* CPLD doesn't have ack capability */
}

static void lh7a40x_mask_cpld_irq (u32 irq)
{
	switch (irq) {
	case IRQ_LPD7A40X_ETH_INT:
		CPLD_INTERRUPTS = CPLD_INTERRUPTS | 0x4;
		break;
	case IRQ_LPD7A400_TS:
		CPLD_INTERRUPTS = CPLD_INTERRUPTS | 0x8;
		break;
	}
}

static void lh7a40x_unmask_cpld_irq (u32 irq)
{
	switch (irq) {
	case IRQ_LPD7A40X_ETH_INT:
		CPLD_INTERRUPTS = CPLD_INTERRUPTS & ~ 0x4;
		break;
	case IRQ_LPD7A400_TS:
		CPLD_INTERRUPTS = CPLD_INTERRUPTS & ~ 0x8;
		break;
	}
}

static struct irqchip lpd7a40x_cpld_chip = {
	.ack	= lh7a40x_ack_cpld_irq,
	.mask	= lh7a40x_mask_cpld_irq,
	.unmask	= lh7a40x_unmask_cpld_irq,
};

static void lpd7a40x_cpld_handler (unsigned int irq, struct irqdesc *desc,
				  struct pt_regs *regs)
{
	unsigned int mask = CPLD_INTERRUPTS;

	desc->chip->ack (irq);

	if ((mask & 0x1) == 0)	/* WLAN */
		IRQ_DISPATCH (IRQ_LPD7A40X_ETH_INT);

	if ((mask & 0x2) == 0)	/* Touch */
		IRQ_DISPATCH (IRQ_LPD7A400_TS);

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

	CPLD_INTERRUPTS	=   0x9c; /* Disable all CPLD interrupts */
	GPIO_PFDD	&= ~(1 << pinCPLD); /* Make input */
	GPIO_INTTYPE1	|=  (1 << pinCPLD); /* Edge triggered */
	GPIO_INTTYPE2	&= ~(1 << pinCPLD); /* Active low */
	barrier ();
	GPIO_GPIOFINTEN |=  (1 << pinCPLD); /* Enable */

		/* Cascade CPLD interrupts */

	for (irq = IRQ_BOARD_START;
	     irq < IRQ_BOARD_START + NR_IRQ_BOARD; ++irq) {
		set_irq_chip (irq, &lpd7a40x_cpld_chip);
		set_irq_handler (irq, do_edge_IRQ);
		set_irq_flags (irq, IRQF_VALID);
	}

	set_irq_chained_handler ((cpld_version == 0x28)
				 ? IRQ_CPLD_V28
				 : IRQ_CPLD_V34,
				 lpd7a40x_cpld_handler);
}

static struct map_desc lpd7a400_io_desc[] __initdata = {
	{
		.virtual	=     IO_VIRT,
		.pfn		= __phys_to_pfn(IO_PHYS),
		.length		= 	    IO_SIZE,
		.type		= MT_DEVICE
	}, {	/* Mapping added to work around chip select problems */
		.virtual	= IOBARRIER_VIRT,
		.pfn		= __phys_to_pfn(IOBARRIER_PHYS),
		.length		= IOBARRIER_SIZE,
		.type		= MT_DEVICE
	}, {
		.virtual	= CF_VIRT,
		.pfn		= __phys_to_pfn(CF_PHYS),
		.length		= 	CF_SIZE,
		.type		= MT_DEVICE
	}, {
		.virtual	= CPLD02_VIRT,
		.pfn		= __phys_to_pfn(CPLD02_PHYS),
		.length		= 	CPLD02_SIZE,
		.type		= MT_DEVICE
	}, {
		.virtual	= CPLD06_VIRT,
		.pfn		= __phys_to_pfn(CPLD06_PHYS),
		.length		= 	CPLD06_SIZE,
		.type		= MT_DEVICE
	}, {
		.virtual	= CPLD08_VIRT,
		.pfn		= __phys_to_pfn(CPLD08_PHYS),
		.length		= 	CPLD08_SIZE,
		.type		= MT_DEVICE
	}, {
		.virtual	= CPLD0C_VIRT,
		.pfn		= __phys_to_pfn(CPLD0C_PHYS),
		.length		= 	CPLD0C_SIZE,
		.type		= MT_DEVICE
	}, {
		.virtual	= CPLD0E_VIRT,
		.pfn		= __phys_to_pfn(CPLD0E_PHYS),
		.length		= 	CPLD0E_SIZE,
		.type		= MT_DEVICE
	}, {
		.virtual	= CPLD10_VIRT,
		.pfn		= __phys_to_pfn(CPLD10_PHYS),
		.length		= 	CPLD10_SIZE,
		.type		= MT_DEVICE
	}, {
		.virtual	= CPLD12_VIRT,
		.pfn		= __phys_to_pfn(CPLD12_PHYS),
		.length		= 	CPLD12_SIZE,
		.type		= MT_DEVICE
	}, {
		.virtual	= CPLD14_VIRT,
		.pfn		= __phys_to_pfn(CPLD14_PHYS),
		.length		= 	CPLD14_SIZE,
		.type		= MT_DEVICE
	}, {
		.virtual	= CPLD16_VIRT,
		.pfn		= __phys_to_pfn(CPLD16_PHYS),
		.length		= 	CPLD16_SIZE,
		.type		= MT_DEVICE
	}, {
		.virtual	= CPLD18_VIRT,
		.pfn		= __phys_to_pfn(CPLD18_PHYS),
		.length		= 	CPLD18_SIZE,
		.type		= MT_DEVICE
	}, {
		.virtual	= CPLD1A_VIRT,
		.pfn		= __phys_to_pfn(CPLD1A_PHYS),
		.length		= 	CPLD1A_SIZE,
		.type		= MT_DEVICE
	},
	/* This mapping is redundant since the smc driver performs another. */
/*	{ CPLD00_VIRT,  CPLD00_PHYS,	CPLD00_SIZE,	MT_DEVICE }, */
};

void __init
lpd7a400_map_io(void)
{
	iotable_init (lpd7a400_io_desc, ARRAY_SIZE (lpd7a400_io_desc));

		/* Fixup (improve) Static Memory Controller settings */
	SMC_BCR0 = 0x200039af;	/* Boot Flash */
	SMC_BCR6 = 0x1000fbe0;	/* CPLD */
	SMC_BCR7 = 0x1000b2c2;	/* Compact Flash */
}

#ifdef CONFIG_MACH_LPD7A400

MACHINE_START (LPD7A400, "Logic Product Development LPD7A400-10")
	/* Maintainer: Marc Singer */
	.phys_ram	= 0xc0000000,
	.phys_io	= 0x80000000,
	.io_pg_offst	= ((io_p2v (0x80000000))>>18) & 0xfffc,
	.boot_params	= 0xc0000100,
	.map_io		= lpd7a400_map_io,
	.init_irq	= lh7a400_init_irq,
	.timer		= &lh7a40x_timer,
	.init_machine	= lpd7a40x_init,
MACHINE_END

#endif

#ifdef CONFIG_MACH_LPD7A404

MACHINE_START (LPD7A404, "Logic Product Development LPD7A404-10")
	/* Maintainer: Marc Singer */
	.phys_ram	= 0xc0000000,
	.phys_io	= 0x80000000,
	.io_pg_offst	= ((io_p2v (0x80000000))>>18) & 0xfffc,
	.boot_params	= 0xc0000100,
	.map_io		= lpd7a400_map_io,
	.init_irq	= lh7a404_init_irq,
	.timer		= &lh7a40x_timer,
	.init_machine	= lpd7a40x_init,
MACHINE_END

#endif
