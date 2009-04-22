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

#include "stmp378x.h"
/*
 * IRQ handling
 */
static void stmp378x_ack_irq(unsigned int irq)
{
	/* Tell ICOLL to release IRQ line */
	HW_ICOLL_VECTOR_WR(0x0);

	/* ACK current interrupt */
	HW_ICOLL_LEVELACK_WR(BV_ICOLL_LEVELACK_IRQLEVELACK__LEVEL0);

	/* Barrier */
	(void) HW_ICOLL_STAT_RD();
}

static void stmp378x_mask_irq(unsigned int irq)
{
	/* IRQ disable */
	HW_ICOLL_INTERRUPTn_CLR(irq, BM_ICOLL_INTERRUPTn_ENABLE);
}

static void stmp378x_unmask_irq(unsigned int irq)
{
	/* IRQ enable */
	HW_ICOLL_INTERRUPTn_SET(irq, BM_ICOLL_INTERRUPTn_ENABLE);
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
	int dmabus = channel / 16;

	switch (dmabus) {
	case STMP3XXX_BUS_APBH:
		HW_APBH_CTRL1_SET(1 << (16 + (channel % 16)));
		HW_APBH_CTRL2_SET(1 << (16 + (channel % 16)));
		break;

	case STMP3XXX_BUS_APBX:
		HW_APBX_CTRL1_SET(1 << (16 + (channel % 16)));
		HW_APBX_CTRL2_SET(1 << (16 + (channel % 16)));
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
		HW_APBH_CTRL2_CLR(1 << (channel % 16));
		break;

	case STMP3XXX_BUS_APBX:
		HW_APBX_CTRL1_CLR(1 << (channel % 16));
		HW_APBX_CTRL2_CLR(1 << (channel % 16));
		break;
	}
}
EXPORT_SYMBOL(stmp3xxx_arch_dma_clear_interrupt);

int stmp3xxx_arch_dma_is_interrupt(int channel)
{
	int dmabus = channel / 16;
	int r = 0;

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
		HW_APBH_CTRL0_SET(chbit <<
				 BP_APBH_CTRL0_RESET_CHANNEL);
		while (HW_APBH_CTRL0_RD() &
		       (chbit << BP_APBH_CTRL0_RESET_CHANNEL))
			continue;
		break;

	case STMP3XXX_BUS_APBX:
		/* Reset channel and wait for it to complete */
		HW_APBX_CHANNEL_CTRL_SET(
			BF_APBX_CHANNEL_CTRL_RESET_CHANNEL(chbit));
		while (HW_APBX_CHANNEL_CTRL_RD() &
			BF_APBX_CHANNEL_CTRL_RESET_CHANNEL(chbit))
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
		HW_APBX_CHANNEL_CTRL_SET(1<<chbit);
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
		HW_APBX_CHANNEL_CTRL_CLR(1<<chbit);
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

void __init stmp378x_map_io(void)
{
	iotable_init(stmp378x_io_desc, ARRAY_SIZE(stmp378x_io_desc));
}
