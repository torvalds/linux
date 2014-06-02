/*
 * r8a7779 processor support
 *
 * Copyright (C) 2011, 2013  Renesas Solutions Corp.
 * Copyright (C) 2011  Magnus Damm
 * Copyright (C) 2013  Cogent Embedded, Inc.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; version 2 of the License.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
 */
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/irq.h>
#include <linux/irqchip.h>
#include <linux/irqchip/arm-gic.h>
#include <linux/of_platform.h>
#include <linux/platform_data/dma-rcar-hpbdma.h>
#include <linux/platform_data/gpio-rcar.h>
#include <linux/platform_data/irq-renesas-intc-irqpin.h>
#include <linux/platform_device.h>
#include <linux/delay.h>
#include <linux/input.h>
#include <linux/io.h>
#include <linux/serial_sci.h>
#include <linux/sh_timer.h>
#include <linux/dma-mapping.h>
#include <linux/usb/otg.h>
#include <linux/usb/hcd.h>
#include <linux/usb/ehci_pdriver.h>
#include <linux/usb/ohci_pdriver.h>
#include <linux/pm_runtime.h>
#include <mach/irqs.h>
#include <mach/r8a7779.h>
#include <mach/common.h>
#include <asm/mach-types.h>
#include <asm/mach/arch.h>
#include <asm/mach/time.h>
#include <asm/mach/map.h>
#include <asm/hardware/cache-l2x0.h>

static struct map_desc r8a7779_io_desc[] __initdata = {
	/* 2M entity map for 0xf0000000 (MPCORE) */
	{
		.virtual	= 0xf0000000,
		.pfn		= __phys_to_pfn(0xf0000000),
		.length		= SZ_2M,
		.type		= MT_DEVICE_NONSHARED
	},
	/* 16M entity map for 0xfexxxxxx (DMAC-S/HPBREG/INTC2/LRAM/DBSC) */
	{
		.virtual	= 0xfe000000,
		.pfn		= __phys_to_pfn(0xfe000000),
		.length		= SZ_16M,
		.type		= MT_DEVICE_NONSHARED
	},
};

void __init r8a7779_map_io(void)
{
	iotable_init(r8a7779_io_desc, ARRAY_SIZE(r8a7779_io_desc));
}

/* IRQ */
#define INT2SMSKCR0 IOMEM(0xfe7822a0)
#define INT2SMSKCR1 IOMEM(0xfe7822a4)
#define INT2SMSKCR2 IOMEM(0xfe7822a8)
#define INT2SMSKCR3 IOMEM(0xfe7822ac)
#define INT2SMSKCR4 IOMEM(0xfe7822b0)

#define INT2NTSR0 IOMEM(0xfe700060)
#define INT2NTSR1 IOMEM(0xfe700064)

static struct renesas_intc_irqpin_config irqpin0_platform_data __initdata = {
	.irq_base = irq_pin(0), /* IRQ0 -> IRQ3 */
	.sense_bitfield_width = 2,
};

static struct resource irqpin0_resources[] __initdata = {
	DEFINE_RES_MEM(0xfe78001c, 4), /* ICR1 */
	DEFINE_RES_MEM(0xfe780010, 4), /* INTPRI */
	DEFINE_RES_MEM(0xfe780024, 4), /* INTREQ */
	DEFINE_RES_MEM(0xfe780044, 4), /* INTMSK0 */
	DEFINE_RES_MEM(0xfe780064, 4), /* INTMSKCLR0 */
	DEFINE_RES_IRQ(gic_spi(27)), /* IRQ0 */
	DEFINE_RES_IRQ(gic_spi(28)), /* IRQ1 */
	DEFINE_RES_IRQ(gic_spi(29)), /* IRQ2 */
	DEFINE_RES_IRQ(gic_spi(30)), /* IRQ3 */
};

void __init r8a7779_init_irq_extpin_dt(int irlm)
{
	void __iomem *icr0 = ioremap_nocache(0xfe780000, PAGE_SIZE);
	u32 tmp;

	if (!icr0) {
		pr_warn("r8a7779: unable to setup external irq pin mode\n");
		return;
	}

	tmp = ioread32(icr0);
	if (irlm)
		tmp |= 1 << 23; /* IRQ0 -> IRQ3 as individual pins */
	else
		tmp &= ~(1 << 23); /* IRL mode - not supported */
	tmp |= (1 << 21); /* LVLMODE = 1 */
	iowrite32(tmp, icr0);
	iounmap(icr0);
}

void __init r8a7779_init_irq_extpin(int irlm)
{
	r8a7779_init_irq_extpin_dt(irlm);
	if (irlm)
		platform_device_register_resndata(
			&platform_bus, "renesas_intc_irqpin", -1,
			irqpin0_resources, ARRAY_SIZE(irqpin0_resources),
			&irqpin0_platform_data, sizeof(irqpin0_platform_data));
}

/* PFC/GPIO */
static struct resource r8a7779_pfc_resources[] = {
	DEFINE_RES_MEM(0xfffc0000, 0x023c),
};

static struct platform_device r8a7779_pfc_device = {
	.name		= "pfc-r8a7779",
	.id		= -1,
	.resource	= r8a7779_pfc_resources,
	.num_resources	= ARRAY_SIZE(r8a7779_pfc_resources),
};

#define R8A7779_GPIO(idx, npins) \
static struct resource r8a7779_gpio##idx##_resources[] = {		\
	DEFINE_RES_MEM(0xffc40000 + (0x1000 * (idx)), 0x002c),		\
	DEFINE_RES_IRQ(gic_iid(0xad + (idx))),				\
};									\
									\
static struct gpio_rcar_config r8a7779_gpio##idx##_platform_data = {	\
	.gpio_base	= 32 * (idx),					\
	.irq_base	= 0,						\
	.number_of_pins	= npins,					\
	.pctl_name	= "pfc-r8a7779",				\
};									\
									\
static struct platform_device r8a7779_gpio##idx##_device = {		\
	.name		= "gpio_rcar",					\
	.id		= idx,						\
	.resource	= r8a7779_gpio##idx##_resources,		\
	.num_resources	= ARRAY_SIZE(r8a7779_gpio##idx##_resources),	\
	.dev		= {						\
		.platform_data	= &r8a7779_gpio##idx##_platform_data,	\
	},								\
}

R8A7779_GPIO(0, 32);
R8A7779_GPIO(1, 32);
R8A7779_GPIO(2, 32);
R8A7779_GPIO(3, 32);
R8A7779_GPIO(4, 32);
R8A7779_GPIO(5, 32);
R8A7779_GPIO(6, 9);

static struct platform_device *r8a7779_pinctrl_devices[] __initdata = {
	&r8a7779_pfc_device,
	&r8a7779_gpio0_device,
	&r8a7779_gpio1_device,
	&r8a7779_gpio2_device,
	&r8a7779_gpio3_device,
	&r8a7779_gpio4_device,
	&r8a7779_gpio5_device,
	&r8a7779_gpio6_device,
};

void __init r8a7779_pinmux_init(void)
{
	platform_add_devices(r8a7779_pinctrl_devices,
			    ARRAY_SIZE(r8a7779_pinctrl_devices));
}

/* SCIF */
#define R8A7779_SCIF(index, baseaddr, irq)			\
static struct plat_sci_port scif##index##_platform_data = {	\
	.type		= PORT_SCIF,				\
	.flags		= UPF_BOOT_AUTOCONF | UPF_IOREMAP,	\
	.scscr		= SCSCR_RE | SCSCR_TE | SCSCR_CKE1,	\
};								\
								\
static struct resource scif##index##_resources[] = {		\
	DEFINE_RES_MEM(baseaddr, 0x100),			\
	DEFINE_RES_IRQ(irq),					\
};								\
								\
static struct platform_device scif##index##_device = {		\
	.name		= "sh-sci",				\
	.id		= index,				\
	.resource	= scif##index##_resources,		\
	.num_resources	= ARRAY_SIZE(scif##index##_resources),	\
	.dev		= {					\
		.platform_data	= &scif##index##_platform_data,	\
	},							\
}

R8A7779_SCIF(0, 0xffe40000, gic_iid(0x78));
R8A7779_SCIF(1, 0xffe41000, gic_iid(0x79));
R8A7779_SCIF(2, 0xffe42000, gic_iid(0x7a));
R8A7779_SCIF(3, 0xffe43000, gic_iid(0x7b));
R8A7779_SCIF(4, 0xffe44000, gic_iid(0x7c));
R8A7779_SCIF(5, 0xffe45000, gic_iid(0x7d));

/* TMU */
static struct sh_timer_config tmu0_platform_data = {
	.channels_mask = 7,
};

static struct resource tmu0_resources[] = {
	DEFINE_RES_MEM(0xffd80000, 0x30),
	DEFINE_RES_IRQ(gic_iid(0x40)),
	DEFINE_RES_IRQ(gic_iid(0x41)),
	DEFINE_RES_IRQ(gic_iid(0x42)),
};

static struct platform_device tmu0_device = {
	.name		= "sh-tmu",
	.id		= 0,
	.dev = {
		.platform_data	= &tmu0_platform_data,
	},
	.resource	= tmu0_resources,
	.num_resources	= ARRAY_SIZE(tmu0_resources),
};

/* I2C */
static struct resource rcar_i2c0_res[] = {
	{
		.start  = 0xffc70000,
		.end    = 0xffc70fff,
		.flags  = IORESOURCE_MEM,
	}, {
		.start  = gic_iid(0x6f),
		.flags  = IORESOURCE_IRQ,
	},
};

static struct platform_device i2c0_device = {
	.name		= "i2c-rcar",
	.id		= 0,
	.resource	= rcar_i2c0_res,
	.num_resources	= ARRAY_SIZE(rcar_i2c0_res),
};

static struct resource rcar_i2c1_res[] = {
	{
		.start  = 0xffc71000,
		.end    = 0xffc71fff,
		.flags  = IORESOURCE_MEM,
	}, {
		.start  = gic_iid(0x72),
		.flags  = IORESOURCE_IRQ,
	},
};

static struct platform_device i2c1_device = {
	.name		= "i2c-rcar",
	.id		= 1,
	.resource	= rcar_i2c1_res,
	.num_resources	= ARRAY_SIZE(rcar_i2c1_res),
};

static struct resource rcar_i2c2_res[] = {
	{
		.start  = 0xffc72000,
		.end    = 0xffc72fff,
		.flags  = IORESOURCE_MEM,
	}, {
		.start  = gic_iid(0x70),
		.flags  = IORESOURCE_IRQ,
	},
};

static struct platform_device i2c2_device = {
	.name		= "i2c-rcar",
	.id		= 2,
	.resource	= rcar_i2c2_res,
	.num_resources	= ARRAY_SIZE(rcar_i2c2_res),
};

static struct resource rcar_i2c3_res[] = {
	{
		.start  = 0xffc73000,
		.end    = 0xffc73fff,
		.flags  = IORESOURCE_MEM,
	}, {
		.start  = gic_iid(0x71),
		.flags  = IORESOURCE_IRQ,
	},
};

static struct platform_device i2c3_device = {
	.name		= "i2c-rcar",
	.id		= 3,
	.resource	= rcar_i2c3_res,
	.num_resources	= ARRAY_SIZE(rcar_i2c3_res),
};

static struct resource sata_resources[] = {
	[0] = {
		.name	= "rcar-sata",
		.start	= 0xfc600000,
		.end	= 0xfc601fff,
		.flags	= IORESOURCE_MEM,
	},
	[1] = {
		.start	= gic_iid(0x84),
		.flags	= IORESOURCE_IRQ,
	},
};

static struct platform_device sata_device = {
	.name		= "sata_rcar",
	.id		= -1,
	.resource	= sata_resources,
	.num_resources	= ARRAY_SIZE(sata_resources),
	.dev		= {
		.dma_mask		= &sata_device.dev.coherent_dma_mask,
		.coherent_dma_mask	= DMA_BIT_MASK(32),
	},
};

/* USB */
static struct usb_phy *phy;

static int usb_power_on(struct platform_device *pdev)
{
	if (IS_ERR(phy))
		return PTR_ERR(phy);

	pm_runtime_enable(&pdev->dev);
	pm_runtime_get_sync(&pdev->dev);

	usb_phy_init(phy);

	return 0;
}

static void usb_power_off(struct platform_device *pdev)
{
	if (IS_ERR(phy))
		return;

	usb_phy_shutdown(phy);

	pm_runtime_put_sync(&pdev->dev);
	pm_runtime_disable(&pdev->dev);
}

static int ehci_init_internal_buffer(struct usb_hcd *hcd)
{
	/*
	 * Below are recommended values from the datasheet;
	 * see [USB :: Setting of EHCI Internal Buffer].
	 */
	/* EHCI IP internal buffer setting */
	iowrite32(0x00ff0040, hcd->regs + 0x0094);
	/* EHCI IP internal buffer enable */
	iowrite32(0x00000001, hcd->regs + 0x009C);

	return 0;
}

static struct usb_ehci_pdata ehcix_pdata = {
	.power_on	= usb_power_on,
	.power_off	= usb_power_off,
	.power_suspend	= usb_power_off,
	.pre_setup	= ehci_init_internal_buffer,
};

static struct resource ehci0_resources[] = {
	[0] = {
		.start	= 0xffe70000,
		.end	= 0xffe70400 - 1,
		.flags	= IORESOURCE_MEM,
	},
	[1] = {
		.start	= gic_iid(0x4c),
		.flags	= IORESOURCE_IRQ,
	},
};

static struct platform_device ehci0_device = {
	.name	= "ehci-platform",
	.id	= 0,
	.dev	= {
		.dma_mask		= &ehci0_device.dev.coherent_dma_mask,
		.coherent_dma_mask	= 0xffffffff,
		.platform_data		= &ehcix_pdata,
	},
	.num_resources	= ARRAY_SIZE(ehci0_resources),
	.resource	= ehci0_resources,
};

static struct resource ehci1_resources[] = {
	[0] = {
		.start	= 0xfff70000,
		.end	= 0xfff70400 - 1,
		.flags	= IORESOURCE_MEM,
	},
	[1] = {
		.start	= gic_iid(0x4d),
		.flags	= IORESOURCE_IRQ,
	},
};

static struct platform_device ehci1_device = {
	.name	= "ehci-platform",
	.id	= 1,
	.dev	= {
		.dma_mask		= &ehci1_device.dev.coherent_dma_mask,
		.coherent_dma_mask	= 0xffffffff,
		.platform_data		= &ehcix_pdata,
	},
	.num_resources	= ARRAY_SIZE(ehci1_resources),
	.resource	= ehci1_resources,
};

static struct usb_ohci_pdata ohcix_pdata = {
	.power_on	= usb_power_on,
	.power_off	= usb_power_off,
	.power_suspend	= usb_power_off,
};

static struct resource ohci0_resources[] = {
	[0] = {
		.start	= 0xffe70400,
		.end	= 0xffe70800 - 1,
		.flags	= IORESOURCE_MEM,
	},
	[1] = {
		.start	= gic_iid(0x4c),
		.flags	= IORESOURCE_IRQ,
	},
};

static struct platform_device ohci0_device = {
	.name	= "ohci-platform",
	.id	= 0,
	.dev	= {
		.dma_mask		= &ohci0_device.dev.coherent_dma_mask,
		.coherent_dma_mask	= 0xffffffff,
		.platform_data		= &ohcix_pdata,
	},
	.num_resources	= ARRAY_SIZE(ohci0_resources),
	.resource	= ohci0_resources,
};

static struct resource ohci1_resources[] = {
	[0] = {
		.start	= 0xfff70400,
		.end	= 0xfff70800 - 1,
		.flags	= IORESOURCE_MEM,
	},
	[1] = {
		.start	= gic_iid(0x4d),
		.flags	= IORESOURCE_IRQ,
	},
};

static struct platform_device ohci1_device = {
	.name	= "ohci-platform",
	.id	= 1,
	.dev	= {
		.dma_mask		= &ohci1_device.dev.coherent_dma_mask,
		.coherent_dma_mask	= 0xffffffff,
		.platform_data		= &ohcix_pdata,
	},
	.num_resources	= ARRAY_SIZE(ohci1_resources),
	.resource	= ohci1_resources,
};

/* HPB-DMA */

/* Asynchronous mode register bits */
#define HPB_DMAE_ASYNCMDR_ASMD43_MASK		BIT(23)	/* MMC1 */
#define HPB_DMAE_ASYNCMDR_ASMD43_SINGLE		BIT(23)	/* MMC1 */
#define HPB_DMAE_ASYNCMDR_ASMD43_MULTI		0	/* MMC1 */
#define HPB_DMAE_ASYNCMDR_ASBTMD43_MASK		BIT(22)	/* MMC1 */
#define HPB_DMAE_ASYNCMDR_ASBTMD43_BURST	BIT(22)	/* MMC1 */
#define HPB_DMAE_ASYNCMDR_ASBTMD43_NBURST	0	/* MMC1 */
#define HPB_DMAE_ASYNCMDR_ASMD24_MASK		BIT(21)	/* MMC0 */
#define HPB_DMAE_ASYNCMDR_ASMD24_SINGLE		BIT(21)	/* MMC0 */
#define HPB_DMAE_ASYNCMDR_ASMD24_MULTI		0	/* MMC0 */
#define HPB_DMAE_ASYNCMDR_ASBTMD24_MASK		BIT(20)	/* MMC0 */
#define HPB_DMAE_ASYNCMDR_ASBTMD24_BURST	BIT(20)	/* MMC0 */
#define HPB_DMAE_ASYNCMDR_ASBTMD24_NBURST	0	/* MMC0 */
#define HPB_DMAE_ASYNCMDR_ASMD41_MASK		BIT(19)	/* SDHI3 */
#define HPB_DMAE_ASYNCMDR_ASMD41_SINGLE		BIT(19)	/* SDHI3 */
#define HPB_DMAE_ASYNCMDR_ASMD41_MULTI		0	/* SDHI3 */
#define HPB_DMAE_ASYNCMDR_ASBTMD41_MASK		BIT(18)	/* SDHI3 */
#define HPB_DMAE_ASYNCMDR_ASBTMD41_BURST	BIT(18)	/* SDHI3 */
#define HPB_DMAE_ASYNCMDR_ASBTMD41_NBURST	0	/* SDHI3 */
#define HPB_DMAE_ASYNCMDR_ASMD40_MASK		BIT(17)	/* SDHI3 */
#define HPB_DMAE_ASYNCMDR_ASMD40_SINGLE		BIT(17)	/* SDHI3 */
#define HPB_DMAE_ASYNCMDR_ASMD40_MULTI		0	/* SDHI3 */
#define HPB_DMAE_ASYNCMDR_ASBTMD40_MASK		BIT(16)	/* SDHI3 */
#define HPB_DMAE_ASYNCMDR_ASBTMD40_BURST	BIT(16)	/* SDHI3 */
#define HPB_DMAE_ASYNCMDR_ASBTMD40_NBURST	0	/* SDHI3 */
#define HPB_DMAE_ASYNCMDR_ASMD39_MASK		BIT(15)	/* SDHI3 */
#define HPB_DMAE_ASYNCMDR_ASMD39_SINGLE		BIT(15)	/* SDHI3 */
#define HPB_DMAE_ASYNCMDR_ASMD39_MULTI		0	/* SDHI3 */
#define HPB_DMAE_ASYNCMDR_ASBTMD39_MASK		BIT(14)	/* SDHI3 */
#define HPB_DMAE_ASYNCMDR_ASBTMD39_BURST	BIT(14)	/* SDHI3 */
#define HPB_DMAE_ASYNCMDR_ASBTMD39_NBURST	0	/* SDHI3 */
#define HPB_DMAE_ASYNCMDR_ASMD27_MASK		BIT(13)	/* SDHI2 */
#define HPB_DMAE_ASYNCMDR_ASMD27_SINGLE		BIT(13)	/* SDHI2 */
#define HPB_DMAE_ASYNCMDR_ASMD27_MULTI		0	/* SDHI2 */
#define HPB_DMAE_ASYNCMDR_ASBTMD27_MASK		BIT(12)	/* SDHI2 */
#define HPB_DMAE_ASYNCMDR_ASBTMD27_BURST	BIT(12)	/* SDHI2 */
#define HPB_DMAE_ASYNCMDR_ASBTMD27_NBURST	0	/* SDHI2 */
#define HPB_DMAE_ASYNCMDR_ASMD26_MASK		BIT(11)	/* SDHI2 */
#define HPB_DMAE_ASYNCMDR_ASMD26_SINGLE		BIT(11)	/* SDHI2 */
#define HPB_DMAE_ASYNCMDR_ASMD26_MULTI		0	/* SDHI2 */
#define HPB_DMAE_ASYNCMDR_ASBTMD26_MASK		BIT(10)	/* SDHI2 */
#define HPB_DMAE_ASYNCMDR_ASBTMD26_BURST	BIT(10)	/* SDHI2 */
#define HPB_DMAE_ASYNCMDR_ASBTMD26_NBURST	0	/* SDHI2 */
#define HPB_DMAE_ASYNCMDR_ASMD25_MASK		BIT(9)	/* SDHI2 */
#define HPB_DMAE_ASYNCMDR_ASMD25_SINGLE		BIT(9)	/* SDHI2 */
#define HPB_DMAE_ASYNCMDR_ASMD25_MULTI		0	/* SDHI2 */
#define HPB_DMAE_ASYNCMDR_ASBTMD25_MASK		BIT(8)	/* SDHI2 */
#define HPB_DMAE_ASYNCMDR_ASBTMD25_BURST	BIT(8)	/* SDHI2 */
#define HPB_DMAE_ASYNCMDR_ASBTMD25_NBURST	0	/* SDHI2 */
#define HPB_DMAE_ASYNCMDR_ASMD23_MASK		BIT(7)	/* SDHI0 */
#define HPB_DMAE_ASYNCMDR_ASMD23_SINGLE		BIT(7)	/* SDHI0 */
#define HPB_DMAE_ASYNCMDR_ASMD23_MULTI		0	/* SDHI0 */
#define HPB_DMAE_ASYNCMDR_ASBTMD23_MASK		BIT(6)	/* SDHI0 */
#define HPB_DMAE_ASYNCMDR_ASBTMD23_BURST	BIT(6)	/* SDHI0 */
#define HPB_DMAE_ASYNCMDR_ASBTMD23_NBURST	0	/* SDHI0 */
#define HPB_DMAE_ASYNCMDR_ASMD22_MASK		BIT(5)	/* SDHI0 */
#define HPB_DMAE_ASYNCMDR_ASMD22_SINGLE		BIT(5)	/* SDHI0 */
#define HPB_DMAE_ASYNCMDR_ASMD22_MULTI		0	/* SDHI0 */
#define HPB_DMAE_ASYNCMDR_ASBTMD22_MASK		BIT(4)	/* SDHI0 */
#define HPB_DMAE_ASYNCMDR_ASBTMD22_BURST	BIT(4)	/* SDHI0 */
#define HPB_DMAE_ASYNCMDR_ASBTMD22_NBURST	0	/* SDHI0 */
#define HPB_DMAE_ASYNCMDR_ASMD21_MASK		BIT(3)	/* SDHI0 */
#define HPB_DMAE_ASYNCMDR_ASMD21_SINGLE		BIT(3)	/* SDHI0 */
#define HPB_DMAE_ASYNCMDR_ASMD21_MULTI		0	/* SDHI0 */
#define HPB_DMAE_ASYNCMDR_ASBTMD21_MASK		BIT(2)	/* SDHI0 */
#define HPB_DMAE_ASYNCMDR_ASBTMD21_BURST	BIT(2)	/* SDHI0 */
#define HPB_DMAE_ASYNCMDR_ASBTMD21_NBURST	0	/* SDHI0 */
#define HPB_DMAE_ASYNCMDR_ASMD20_MASK		BIT(1)	/* SDHI1 */
#define HPB_DMAE_ASYNCMDR_ASMD20_SINGLE		BIT(1)	/* SDHI1 */
#define HPB_DMAE_ASYNCMDR_ASMD20_MULTI		0	/* SDHI1 */
#define HPB_DMAE_ASYNCMDR_ASBTMD20_MASK		BIT(0)	/* SDHI1 */
#define HPB_DMAE_ASYNCMDR_ASBTMD20_BURST	BIT(0)	/* SDHI1 */
#define HPB_DMAE_ASYNCMDR_ASBTMD20_NBURST	0	/* SDHI1 */

static const struct hpb_dmae_slave_config hpb_dmae_slaves[] = {
	{
		.id	= HPBDMA_SLAVE_SDHI0_TX,
		.addr	= 0xffe4c000 + 0x30,
		.dcr	= HPB_DMAE_DCR_SPDS_16BIT |
			  HPB_DMAE_DCR_DMDL |
			  HPB_DMAE_DCR_DPDS_16BIT,
		.rstr	= HPB_DMAE_ASYNCRSTR_ASRST21 |
			  HPB_DMAE_ASYNCRSTR_ASRST22 |
			  HPB_DMAE_ASYNCRSTR_ASRST23,
		.mdr	= HPB_DMAE_ASYNCMDR_ASMD21_SINGLE |
			  HPB_DMAE_ASYNCMDR_ASBTMD21_NBURST,
		.mdm	= HPB_DMAE_ASYNCMDR_ASMD21_MASK |
			  HPB_DMAE_ASYNCMDR_ASBTMD21_MASK,
		.port	= 0x0D0C,
		.flags	= HPB_DMAE_SET_ASYNC_RESET | HPB_DMAE_SET_ASYNC_MODE,
		.dma_ch	= 21,
	}, {
		.id	= HPBDMA_SLAVE_SDHI0_RX,
		.addr	= 0xffe4c000 + 0x30,
		.dcr	= HPB_DMAE_DCR_SMDL |
			  HPB_DMAE_DCR_SPDS_16BIT |
			  HPB_DMAE_DCR_DPDS_16BIT,
		.rstr	= HPB_DMAE_ASYNCRSTR_ASRST21 |
			  HPB_DMAE_ASYNCRSTR_ASRST22 |
			  HPB_DMAE_ASYNCRSTR_ASRST23,
		.mdr	= HPB_DMAE_ASYNCMDR_ASMD22_SINGLE |
			  HPB_DMAE_ASYNCMDR_ASBTMD22_NBURST,
		.mdm	= HPB_DMAE_ASYNCMDR_ASMD22_MASK |
			  HPB_DMAE_ASYNCMDR_ASBTMD22_MASK,
		.port	= 0x0D0C,
		.flags	= HPB_DMAE_SET_ASYNC_RESET | HPB_DMAE_SET_ASYNC_MODE,
		.dma_ch	= 22,
	},
};

static const struct hpb_dmae_channel hpb_dmae_channels[] = {
	HPB_DMAE_CHANNEL(0x93, HPBDMA_SLAVE_SDHI0_TX), /* ch. 21 */
	HPB_DMAE_CHANNEL(0x93, HPBDMA_SLAVE_SDHI0_RX), /* ch. 22 */
};

static struct hpb_dmae_pdata dma_platform_data __initdata = {
	.slaves			= hpb_dmae_slaves,
	.num_slaves		= ARRAY_SIZE(hpb_dmae_slaves),
	.channels		= hpb_dmae_channels,
	.num_channels		= ARRAY_SIZE(hpb_dmae_channels),
	.ts_shift		= {
		[XMIT_SZ_8BIT]	= 0,
		[XMIT_SZ_16BIT]	= 1,
		[XMIT_SZ_32BIT]	= 2,
	},
	.num_hw_channels	= 44,
};

static struct resource hpb_dmae_resources[] __initdata = {
	/* Channel registers */
	DEFINE_RES_MEM(0xffc08000, 0x1000),
	/* Common registers */
	DEFINE_RES_MEM(0xffc09000, 0x170),
	/* Asynchronous reset registers */
	DEFINE_RES_MEM(0xffc00300, 4),
	/* Asynchronous mode registers */
	DEFINE_RES_MEM(0xffc00400, 4),
	/* IRQ for DMA channels */
	DEFINE_RES_NAMED(gic_iid(0x8e), 12, NULL, IORESOURCE_IRQ),
};

static void __init r8a7779_register_hpb_dmae(void)
{
	platform_device_register_resndata(&platform_bus, "hpb-dma-engine", -1,
					  hpb_dmae_resources,
					  ARRAY_SIZE(hpb_dmae_resources),
					  &dma_platform_data,
					  sizeof(dma_platform_data));
}

static struct platform_device *r8a7779_devices_dt[] __initdata = {
	&scif0_device,
	&scif1_device,
	&scif2_device,
	&scif3_device,
	&scif4_device,
	&scif5_device,
	&tmu0_device,
};

static struct platform_device *r8a7779_standard_devices[] __initdata = {
	&i2c0_device,
	&i2c1_device,
	&i2c2_device,
	&i2c3_device,
	&sata_device,
};

void __init r8a7779_add_standard_devices(void)
{
#ifdef CONFIG_CACHE_L2X0
	/* Early BRESP enable, Shared attribute override enable, 64K*16way */
	l2x0_init(IOMEM(0xf0100000), 0x40470000, 0x82000fff);
#endif
	r8a7779_pm_init();

	r8a7779_init_pm_domains();

	platform_add_devices(r8a7779_devices_dt,
			    ARRAY_SIZE(r8a7779_devices_dt));
	platform_add_devices(r8a7779_standard_devices,
			    ARRAY_SIZE(r8a7779_standard_devices));
	r8a7779_register_hpb_dmae();
}

/* do nothing for !CONFIG_SMP or !CONFIG_HAVE_TWD */
void __init __weak r8a7779_register_twd(void) { }

void __init r8a7779_earlytimer_init(void)
{
	r8a7779_clock_init();
	r8a7779_register_twd();
	shmobile_earlytimer_init();
}

void __init r8a7779_add_early_devices(void)
{
	early_platform_add_devices(r8a7779_devices_dt,
				   ARRAY_SIZE(r8a7779_devices_dt));

	/* Early serial console setup is not included here due to
	 * memory map collisions. The SCIF serial ports in r8a7779
	 * are difficult to entity map 1:1 due to collision with the
	 * virtual memory range used by the coherent DMA code on ARM.
	 *
	 * Anyone wanting to debug early can remove UPF_IOREMAP from
	 * the sh-sci serial console platform data, adjust mapbase
	 * to a static M:N virt:phys mapping that needs to be added to
	 * the mappings passed with iotable_init() above.
	 *
	 * Then add a call to shmobile_setup_console() from this function.
	 *
	 * As a final step pass earlyprint=sh-sci.2,115200 on the kernel
	 * command line in case of the marzen board.
	 */
}

static struct platform_device *r8a7779_late_devices[] __initdata = {
	&ehci0_device,
	&ehci1_device,
	&ohci0_device,
	&ohci1_device,
};

void __init r8a7779_init_late(void)
{
	/* get USB PHY */
	phy = usb_get_phy(USB_PHY_TYPE_USB2);

	shmobile_init_late();
	platform_add_devices(r8a7779_late_devices,
			     ARRAY_SIZE(r8a7779_late_devices));
}

#ifdef CONFIG_USE_OF
static int r8a7779_set_wake(struct irq_data *data, unsigned int on)
{
	return 0; /* always allow wakeup */
}

void __init r8a7779_init_irq_dt(void)
{
	gic_arch_extn.irq_set_wake = r8a7779_set_wake;

	irqchip_init();

	/* route all interrupts to ARM */
	__raw_writel(0xffffffff, INT2NTSR0);
	__raw_writel(0x3fffffff, INT2NTSR1);

	/* unmask all known interrupts in INTCS2 */
	__raw_writel(0xfffffff0, INT2SMSKCR0);
	__raw_writel(0xfff7ffff, INT2SMSKCR1);
	__raw_writel(0xfffbffdf, INT2SMSKCR2);
	__raw_writel(0xbffffffc, INT2SMSKCR3);
	__raw_writel(0x003fee3f, INT2SMSKCR4);
}

void __init r8a7779_init_delay(void)
{
	shmobile_setup_delay(1000, 2, 4); /* Cortex-A9 @ 1000MHz */
}

void __init r8a7779_add_standard_devices_dt(void)
{
	/* clocks are setup late during boot in the case of DT */
	r8a7779_clock_init();

	platform_add_devices(r8a7779_devices_dt,
			     ARRAY_SIZE(r8a7779_devices_dt));
	of_platform_populate(NULL, of_default_bus_match_table, NULL, NULL);
}

static const char *r8a7779_compat_dt[] __initdata = {
	"renesas,r8a7779",
	NULL,
};

DT_MACHINE_START(R8A7779_DT, "Generic R8A7779 (Flattened Device Tree)")
	.map_io		= r8a7779_map_io,
	.init_early	= r8a7779_init_delay,
	.nr_irqs	= NR_IRQS_LEGACY,
	.init_irq	= r8a7779_init_irq_dt,
	.init_machine	= r8a7779_add_standard_devices_dt,
	.init_late	= r8a7779_init_late,
	.dt_compat	= r8a7779_compat_dt,
MACHINE_END
#endif /* CONFIG_USE_OF */
