/*
 * Copyright 2007-2008 Analog Devices Inc.
 *
 * Licensed under the GPL-2 or later
 */

#ifndef _MACH_PLL_H
#define _MACH_PLL_H

#include <asm/blackfin.h>
#include <asm/irqflags.h>

/* Writing to PLL_CTL initiates a PLL relock sequence. */
static __inline__ void bfin_write_PLL_CTL(unsigned int val)
{
	unsigned long flags, iwr0, iwr1;

	if (val == bfin_read_PLL_CTL())
		return;

	flags = hard_local_irq_save();
	/* Enable the PLL Wakeup bit in SIC IWR */
	iwr0 = bfin_read32(SIC_IWR0);
	iwr1 = bfin_read32(SIC_IWR1);
	/* Only allow PPL Wakeup) */
	bfin_write32(SIC_IWR0, IWR_ENABLE(0));
	bfin_write32(SIC_IWR1, 0);

	bfin_write16(PLL_CTL, val);
	SSYNC();
	asm("IDLE;");

	bfin_write32(SIC_IWR0, iwr0);
	bfin_write32(SIC_IWR1, iwr1);
	hard_local_irq_restore(flags);
}

/* Writing to VR_CTL initiates a PLL relock sequence. */
static __inline__ void bfin_write_VR_CTL(unsigned int val)
{
	unsigned long flags, iwr0, iwr1;

	if (val == bfin_read_VR_CTL())
		return;

	flags = hard_local_irq_save();
	/* Enable the PLL Wakeup bit in SIC IWR */
	iwr0 = bfin_read32(SIC_IWR0);
	iwr1 = bfin_read32(SIC_IWR1);
	/* Only allow PPL Wakeup) */
	bfin_write32(SIC_IWR0, IWR_ENABLE(0));
	bfin_write32(SIC_IWR1, 0);

	bfin_write16(VR_CTL, val);
	SSYNC();
	asm("IDLE;");

	bfin_write32(SIC_IWR0, iwr0);
	bfin_write32(SIC_IWR1, iwr1);
	hard_local_irq_restore(flags);
}

#endif /* _MACH_PLL_H */
