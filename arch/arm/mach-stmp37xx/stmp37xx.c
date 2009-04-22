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
	HW_ICOLL_PRIORITYn_CLR(irq / 4, 0x04 << ((irq % 4) * 8));

	/* ACK current interrupt */
	HW_ICOLL_LEVELACK_WR(1);

	/* Barrier */
	(void) HW_ICOLL_STAT_RD();
}

static void stmp37xx_mask_irq(unsigned int irq)
{
	/* IRQ disable */
	HW_ICOLL_PRIORITYn_CLR(irq / 4, 0x04 << ((irq % 4) * 8));
}

static void stmp37xx_unmask_irq(unsigned int irq)
{
	/* IRQ enable */
	HW_ICOLL_PRIORITYn_SET(irq / 4, 0x04 << ((irq % 4) * 8));
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
	int dmabus = channel / 16;

	switch (dmabus) {
	case STMP3XXX_BUS_APBH:
		HW_APBH_CTRL1_SET(1 << (8 + (channel % 16)));
		break;

	case STMP3XXX_BUS_APBX:
		HW_APBX_CTRL1_SET(1 << (8 + (channel % 16)));
		break;
	}
}
EXPORT_SYMBOL(stmp3xxx_arch_dma_enable_interrupt);

void stmp3xxx_arch_dma_clear_interrupt(int channel)
{
	int dmabus = channel / 16;

	switch (dmabus) {
	case STMP3XXX_BUS_APBH:
		HW_APBH_CTRL1_CLR(1 << (channel % 16));
		break;

	case STMP3XXX_BUS_APBX:
		HW_APBX_CTRL1_CLR(1 << (channel % 16));
		break;
	}
}
EXPORT_SYMBOL(stmp3xxx_arch_dma_clear_interrupt);

int stmp3xxx_arch_dma_is_interrupt(int channel)
{
	int r = 0;

	int dmabus = channel / 16;

	switch (dmabus) {
	case STMP3XXX_BUS_APBH:
		r = HW_APBH_CTRL1_RD() & (1 << (channel % 16));
		break;

	case STMP3XXX_BUS_APBX:
		r = HW_APBX_CTRL1_RD() & (1 << (channel % 16));
		break;
	}
	return r;
}
EXPORT_SYMBOL(stmp3xxx_arch_dma_is_interrupt);

void stmp3xxx_arch_dma_reset_channel(int channel)
{
	int dmabus = channel / 16;
	unsigned chbit = 1 << (channel % 16);

	switch (dmabus) {
	case STMP3XXX_BUS_APBH:
		/* Reset channel and wait for it to complete */
		HW_APBH_CTRL0_SET(chbit << BP_APBH_CTRL0_RESET_CHANNEL);
		while (HW_APBH_CTRL0_RD() &
		       (chbit << BP_APBH_CTRL0_RESET_CHANNEL))
			continue;
		break;

	case STMP3XXX_BUS_APBX:
		/* Reset channel and wait for it to complete */
		HW_APBX_CTRL0_SET(chbit << BP_APBX_CTRL0_RESET_CHANNEL);
		while (HW_APBX_CTRL0_RD() &
		       (chbit << BP_APBX_CTRL0_RESET_CHANNEL))
			continue;
		break;
	}
}
EXPORT_SYMBOL(stmp3xxx_arch_dma_reset_channel);

void stmp3xxx_arch_dma_freeze(int channel)
{
	int dmabus = channel / 16;
	unsigned chbit = 1 << (channel % 16);

	switch (dmabus) {
	case STMP3XXX_BUS_APBH:
		HW_APBH_CTRL0_SET(1<<chbit);
		break;
	case STMP3XXX_BUS_APBX:
		HW_APBX_CTRL0_SET(1<<chbit);
		break;
	}
}
EXPORT_SYMBOL(stmp3xxx_arch_dma_freeze);

void stmp3xxx_arch_dma_unfreeze(int channel)
{
	int dmabus = channel / 16;
	unsigned chbit = 1 << (channel % 16);

	switch (dmabus) {
	case STMP3XXX_BUS_APBH:
		HW_APBH_CTRL0_CLR(1<<chbit);
		break;
	case STMP3XXX_BUS_APBX:
		HW_APBX_CTRL0_CLR(1<<chbit);
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
 * f1000000	00000000	256k on-chip SRAM
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
