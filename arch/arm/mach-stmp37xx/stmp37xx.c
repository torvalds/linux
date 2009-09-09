/*
 * Freescale STMP37XX platform support
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
#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/device.h>
#include <linux/platform_device.h>
#include <linux/irq.h>
#include <linux/io.h>

#include <asm/setup.h>
#include <asm/mach-types.h>

#include <asm/mach/arch.h>
#include <asm/mach/irq.h>
#include <asm/mach/map.h>
#include <asm/mach/time.h>

#include <mach/stmp3xxx.h>
#include <mach/dma.h>

#include <mach/platform.h>
#include <mach/regs-icoll.h>
#include <mach/regs-apbh.h>
#include <mach/regs-apbx.h>
#include "stmp37xx.h"

/*
 * IRQ handling
 */
static void stmp37xx_ack_irq(unsigned int irq)
{
	/* Disable IRQ */
	stmp3xxx_clearl(0x04 << ((irq % 4) * 8),
		REGS_ICOLL_BASE + HW_ICOLL_PRIORITYn + irq / 4 * 0x10);

	/* ACK current interrupt */
	__raw_writel(1, REGS_ICOLL_BASE + HW_ICOLL_LEVELACK);

	/* Barrier */
	(void)__raw_readl(REGS_ICOLL_BASE + HW_ICOLL_STAT);
}

static void stmp37xx_mask_irq(unsigned int irq)
{
	/* IRQ disable */
	stmp3xxx_clearl(0x04 << ((irq % 4) * 8),
		REGS_ICOLL_BASE + HW_ICOLL_PRIORITYn + irq / 4 * 0x10);
}

static void stmp37xx_unmask_irq(unsigned int irq)
{
	/* IRQ enable */
	stmp3xxx_setl(0x04 << ((irq % 4) * 8),
		REGS_ICOLL_BASE + HW_ICOLL_PRIORITYn + irq / 4 * 0x10);
}

static struct irq_chip stmp37xx_chip = {
	.ack	= stmp37xx_ack_irq,
	.mask	= stmp37xx_mask_irq,
	.unmask = stmp37xx_unmask_irq,
};

void __init stmp37xx_init_irq(void)
{
	stmp3xxx_init_irq(&stmp37xx_chip);
}

/*
 * DMA interrupt handling
 */
void stmp3xxx_arch_dma_enable_interrupt(int channel)
{
	switch (STMP3XXX_DMA_BUS(channel)) {
	case STMP3XXX_BUS_APBH:
		stmp3xxx_setl(1 << (8 + STMP3XXX_DMA_CHANNEL(channel)),
			REGS_APBH_BASE + HW_APBH_CTRL1);
		break;

	case STMP3XXX_BUS_APBX:
		stmp3xxx_setl(1 << (8 + STMP3XXX_DMA_CHANNEL(channel)),
			REGS_APBX_BASE + HW_APBX_CTRL1);
		break;
	}
}
EXPORT_SYMBOL(stmp3xxx_arch_dma_enable_interrupt);

void stmp3xxx_arch_dma_clear_interrupt(int channel)
{
	switch (STMP3XXX_DMA_BUS(channel)) {
	case STMP3XXX_BUS_APBH:
		stmp3xxx_clearl(1 << STMP3XXX_DMA_CHANNEL(channel),
				REGS_APBH_BASE + HW_APBH_CTRL1);
		break;

	case STMP3XXX_BUS_APBX:
		stmp3xxx_clearl(1 << STMP3XXX_DMA_CHANNEL(channel),
				REGS_APBX_BASE + HW_APBX_CTRL1);
		break;
	}
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
		r = __raw_readl(REGS_APBH_BASE + HW_APBH_CTRL1) &
			(1 << STMP3XXX_DMA_CHANNEL(channel));
		break;
	}
	return r;
}
EXPORT_SYMBOL(stmp3xxx_arch_dma_is_interrupt);

void stmp3xxx_arch_dma_reset_channel(int channel)
{
	unsigned chbit = 1 << STMP3XXX_DMA_CHANNEL(channel);

	switch (STMP3XXX_DMA_BUS(channel)) {
	case STMP3XXX_BUS_APBH:
		/* Reset channel and wait for it to complete */
		stmp3xxx_setl(chbit << BP_APBH_CTRL0_RESET_CHANNEL,
			REGS_APBH_BASE + HW_APBH_CTRL0);
		while (__raw_readl(REGS_APBH_BASE + HW_APBH_CTRL0) &
		       (chbit << BP_APBH_CTRL0_RESET_CHANNEL))
				cpu_relax();
		break;

	case STMP3XXX_BUS_APBX:
		stmp3xxx_setl(chbit << BP_APBX_CTRL0_RESET_CHANNEL,
			REGS_APBX_BASE + HW_APBX_CTRL0);
		while (__raw_readl(REGS_APBX_BASE + HW_APBX_CTRL0) &
		       (chbit << BP_APBX_CTRL0_RESET_CHANNEL))
				cpu_relax();
		break;
	}
}
EXPORT_SYMBOL(stmp3xxx_arch_dma_reset_channel);

void stmp3xxx_arch_dma_freeze(int channel)
{
	unsigned chbit = 1 << STMP3XXX_DMA_CHANNEL(channel);

	switch (STMP3XXX_DMA_BUS(channel)) {
	case STMP3XXX_BUS_APBH:
		stmp3xxx_setl(1 << chbit, REGS_APBH_BASE + HW_APBH_CTRL0);
		break;
	case STMP3XXX_BUS_APBX:
		stmp3xxx_setl(1 << chbit, REGS_APBH_BASE + HW_APBH_CTRL0);
		break;
	}
}
EXPORT_SYMBOL(stmp3xxx_arch_dma_freeze);

void stmp3xxx_arch_dma_unfreeze(int channel)
{
	unsigned chbit = 1 << STMP3XXX_DMA_CHANNEL(channel);

	switch (STMP3XXX_DMA_BUS(channel)) {
	case STMP3XXX_BUS_APBH:
		stmp3xxx_clearl(1 << chbit, REGS_APBH_BASE + HW_APBH_CTRL0);
		break;
	case STMP3XXX_BUS_APBX:
		stmp3xxx_clearl(1 << chbit, REGS_APBH_BASE + HW_APBH_CTRL0);
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
static struct map_desc stmp37xx_io_desc[] __initdata = {
	{
		.virtual	= (u32)STMP3XXX_REGS_BASE,
		.pfn		= __phys_to_pfn(STMP3XXX_REGS_PHBASE),
		.length		= SZ_1M,
		.type		= MT_DEVICE
	},
	{
		.virtual	= (u32)STMP3XXX_OCRAM_BASE,
		.pfn		= __phys_to_pfn(STMP3XXX_OCRAM_PHBASE),
		.length		= STMP3XXX_OCRAM_SIZE,
		.type		= MT_DEVICE,
	},
};

void __init stmp37xx_map_io(void)
{
	iotable_init(stmp37xx_io_desc, ARRAY_SIZE(stmp37xx_io_desc));
}
