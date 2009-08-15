/*
 * Freescale STMP378X platform support
 *
 * Embedded Alley Solutions, Inc <source@embeddedalley.com>
 *
 * Copyright 2008 Freescale Semiconductor, Inc. All Rights Reserved.
 * Copyright 2008 Embedded Alley Solutions, Inc All Rights Reserved.
 */

/*
 * The code contained herein is licensed under the GNU General Public
 * License. You may obtain a copy of the GNU General Public License
 * Version 2 or later at the following locations:
 *
 * http://www.opensource.org/licenses/gpl-license.html
 * http://www.gnu.org/copyleft/gpl.html
 */
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/platform_device.h>
#include <linux/irq.h>
#include <linux/dma-mapping.h>

#include <asm/dma.h>
#include <asm/setup.h>
#include <asm/mach-types.h>

#include <asm/mach/arch.h>
#include <asm/mach/irq.h>
#include <asm/mach/map.h>
#include <asm/mach/time.h>

#include <mach/pins.h>
#include <mach/pinmux.h>
#include <mach/dma.h>
#include <mach/hardware.h>
#include <mach/system.h>
#include <mach/platform.h>
#include <mach/stmp3xxx.h>
#include <mach/regs-icoll.h>
#include <mach/regs-apbh.h>
#include <mach/regs-apbx.h>
#include <mach/regs-pxp.h>
#include <mach/regs-i2c.h>

#include "stmp378x.h"
/*
 * IRQ handling
 */
static void stmp378x_ack_irq(unsigned int irq)
{
	/* Tell ICOLL to release IRQ line */
	__raw_writel(0, REGS_ICOLL_BASE + HW_ICOLL_VECTOR);

	/* ACK current interrupt */
	__raw_writel(0x01 /* BV_ICOLL_LEVELACK_IRQLEVELACK__LEVEL0 */,
			REGS_ICOLL_BASE + HW_ICOLL_LEVELACK);

	/* Barrier */
	(void)__raw_readl(REGS_ICOLL_BASE + HW_ICOLL_STAT);
}

static void stmp378x_mask_irq(unsigned int irq)
{
	/* IRQ disable */
	stmp3xxx_clearl(BM_ICOLL_INTERRUPTn_ENABLE,
			REGS_ICOLL_BASE + HW_ICOLL_INTERRUPTn + irq * 0x10);
}

static void stmp378x_unmask_irq(unsigned int irq)
{
	/* IRQ enable */
	stmp3xxx_setl(BM_ICOLL_INTERRUPTn_ENABLE,
		      REGS_ICOLL_BASE + HW_ICOLL_INTERRUPTn + irq * 0x10);
}

static struct irq_chip stmp378x_chip = {
	.ack	= stmp378x_ack_irq,
	.mask	= stmp378x_mask_irq,
	.unmask = stmp378x_unmask_irq,
};

void __init stmp378x_init_irq(void)
{
	stmp3xxx_init_irq(&stmp378x_chip);
}

/*
 * DMA interrupt handling
 */
void stmp3xxx_arch_dma_enable_interrupt(int channel)
{
	void __iomem *c1, *c2;

	switch (STMP3XXX_DMA_BUS(channel)) {
	case STMP3XXX_BUS_APBH:
		c1 = REGS_APBH_BASE + HW_APBH_CTRL1;
		c2 = REGS_APBH_BASE + HW_APBH_CTRL2;
		break;

	case STMP3XXX_BUS_APBX:
		c1 = REGS_APBX_BASE + HW_APBX_CTRL1;
		c2 = REGS_APBX_BASE + HW_APBX_CTRL2;
		break;

	default:
		return;
	}
	stmp3xxx_setl(1 << (16 + STMP3XXX_DMA_CHANNEL(channel)), c1);
	stmp3xxx_setl(1 << (16 + STMP3XXX_DMA_CHANNEL(channel)), c2);
}
EXPORT_SYMBOL(stmp3xxx_arch_dma_enable_interrupt);

void stmp3xxx_arch_dma_clear_interrupt(int channel)
{
	void __iomem *c1, *c2;

	switch (STMP3XXX_DMA_BUS(channel)) {
	case STMP3XXX_BUS_APBH:
		c1 = REGS_APBH_BASE + HW_APBH_CTRL1;
		c2 = REGS_APBH_BASE + HW_APBH_CTRL2;
		break;

	case STMP3XXX_BUS_APBX:
		c1 = REGS_APBX_BASE + HW_APBX_CTRL1;
		c2 = REGS_APBX_BASE + HW_APBX_CTRL2;
		break;

	default:
		return;
	}
	stmp3xxx_clearl(1 << STMP3XXX_DMA_CHANNEL(channel), c1);
	stmp3xxx_clearl(1 << STMP3XXX_DMA_CHANNEL(channel), c2);
}
EXPORT_SYMBOL(stmp3xxx_arch_dma_clear_interrupt);

int stmp3xxx_arch_dma_is_interrupt(int channel)
{
	int r = 0;

	switch (STMP3XXX_DMA_BUS(channel)) {
	case STMP3XXX_BUS_APBH:
		r = __raw_readl(REGS_APBH_BASE + HW_APBH_CTRL1) &
			(1 << STMP3XXX_DMA_CHANNEL(channel));
		break;

	case STMP3XXX_BUS_APBX:
		r = __raw_readl(REGS_APBX_BASE + HW_APBX_CTRL1) &
			(1 << STMP3XXX_DMA_CHANNEL(channel));
		break;
	}
	return r;
}
EXPORT_SYMBOL(stmp3xxx_arch_dma_is_interrupt);

void stmp3xxx_arch_dma_reset_channel(int channel)
{
	unsigned chbit = 1 << STMP3XXX_DMA_CHANNEL(channel);
	void __iomem *c0;
	u32 mask;

	switch (STMP3XXX_DMA_BUS(channel)) {
	case STMP3XXX_BUS_APBH:
		c0 = REGS_APBH_BASE + HW_APBH_CTRL0;
		mask = chbit << BP_APBH_CTRL0_RESET_CHANNEL;
		break;
	case STMP3XXX_BUS_APBX:
		c0 = REGS_APBX_BASE + HW_APBX_CHANNEL_CTRL;
		mask = chbit << BP_APBX_CHANNEL_CTRL_RESET_CHANNEL;
		break;
	default:
		return;
	}

	/* Reset channel and wait for it to complete */
	stmp3xxx_setl(mask, c0);
	while (__raw_readl(c0) & mask)
		cpu_relax();
}
EXPORT_SYMBOL(stmp3xxx_arch_dma_reset_channel);

void stmp3xxx_arch_dma_freeze(int channel)
{
	unsigned chbit = 1 << STMP3XXX_DMA_CHANNEL(channel);
	u32 mask = 1 << chbit;

	switch (STMP3XXX_DMA_BUS(channel)) {
	case STMP3XXX_BUS_APBH:
		stmp3xxx_setl(mask, REGS_APBH_BASE + HW_APBH_CTRL0);
		break;
	case STMP3XXX_BUS_APBX:
		stmp3xxx_setl(mask, REGS_APBX_BASE + HW_APBX_CHANNEL_CTRL);
		break;
	}
}
EXPORT_SYMBOL(stmp3xxx_arch_dma_freeze);

void stmp3xxx_arch_dma_unfreeze(int channel)
{
	unsigned chbit = 1 << STMP3XXX_DMA_CHANNEL(channel);
	u32 mask = 1 << chbit;

	switch (STMP3XXX_DMA_BUS(channel)) {
	case STMP3XXX_BUS_APBH:
		stmp3xxx_clearl(mask, REGS_APBH_BASE + HW_APBH_CTRL0);
		break;
	case STMP3XXX_BUS_APBX:
		stmp3xxx_clearl(mask, REGS_APBX_BASE + HW_APBX_CHANNEL_CTRL);
		break;
	}
}
EXPORT_SYMBOL(stmp3xxx_arch_dma_unfreeze);

/*
 * The registers are all very closely mapped, so we might as well map them all
 * with a single mapping
 *
 * Logical      Physical
 * f0000000	80000000	On-chip registers
 * f1000000	00000000	32k on-chip SRAM
 */

static struct map_desc stmp378x_io_desc[] __initdata = {
	{
		.virtual	= (u32)STMP3XXX_REGS_BASE,
		.pfn		= __phys_to_pfn(STMP3XXX_REGS_PHBASE),
		.length		= STMP3XXX_REGS_SIZE,
		.type		= MT_DEVICE,
	},
	{
		.virtual	= (u32)STMP3XXX_OCRAM_BASE,
		.pfn		= __phys_to_pfn(STMP3XXX_OCRAM_PHBASE),
		.length		= STMP3XXX_OCRAM_SIZE,
		.type		= MT_DEVICE,
	},
};


static u64 common_dmamask = DMA_BIT_MASK(32);

/*
 * devices that are present only on stmp378x, not on all 3xxx boards:
 * 	PxP
 * 	I2C
 */
static struct resource pxp_resource[] = {
	{
		.flags	= IORESOURCE_MEM,
		.start	= REGS_PXP_PHYS,
		.end	= REGS_PXP_PHYS + REGS_PXP_SIZE,
	}, {
		.flags	= IORESOURCE_IRQ,
		.start	= IRQ_PXP,
		.end	= IRQ_PXP,
	},
};

struct platform_device stmp378x_pxp = {
	.name		= "stmp3xxx-pxp",
	.id		= -1,
	.dev		= {
		.dma_mask		= &common_dmamask,
		.coherent_dma_mask	= DMA_BIT_MASK(32),
	},
	.num_resources	= ARRAY_SIZE(pxp_resource),
	.resource	= pxp_resource,
};

static struct resource i2c_resources[] = {
	{
		.flags = IORESOURCE_IRQ,
		.start = IRQ_I2C_ERROR,
		.end = IRQ_I2C_ERROR,
	}, {
		.flags = IORESOURCE_MEM,
		.start = REGS_I2C_PHYS,
		.end = REGS_I2C_PHYS + REGS_I2C_SIZE,
	}, {
		.flags = IORESOURCE_DMA,
		.start = STMP3XXX_DMA(3, STMP3XXX_BUS_APBX),
		.end = STMP3XXX_DMA(3, STMP3XXX_BUS_APBX),
	},
};

struct platform_device stmp378x_i2c = {
	.name = "i2c_stmp3xxx",
	.id = 0,
	.dev	= {
		.dma_mask	= &common_dmamask,
		.coherent_dma_mask = DMA_BIT_MASK(32),
	},
	.resource = i2c_resources,
	.num_resources = ARRAY_SIZE(i2c_resources),
};

void __init stmp378x_map_io(void)
{
	iotable_init(stmp378x_io_desc, ARRAY_SIZE(stmp378x_io_desc));
}
