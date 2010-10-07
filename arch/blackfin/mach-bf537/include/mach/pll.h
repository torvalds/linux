/*
 * Copyright 2005-2008 Analog Devices Inc.
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
	unsigned long flags, iwr;

	if (val == bfin_read_PLL_CTL())
		return;

	local_irq_save_hw(flags);
	/* Enable the PLL Wakeup bit in SIC IWR */
	iwr = bfin_read32(SIC_IWR);
	/* Only allow PPL Wakeup) */
	bfin_write32(SIC_IWR, IWR_ENABLE(0));

	bfin_write16(PLL_CTL, val);
	SSYNC();
	asm("IDLE;");

	bfin_write32(SIC_IWR, iwr);
	local_irq_restore_hw(flags);
}

/* Writing to VR_CTL initiates a PLL relock sequence. */
static __inline__ void bfin_write_VR_CTL(unsigned int val)
{
	unsigned long flags, iwr;

	if (val == bfin_read_VR_CTL())
		return;

	local_irq_save_hw(flags);
	/* Enable the PLL Wakeup bit in SIC IWR */
	iwr = bfin_read32(SIC_IWR);
	/* Only allow PPL Wakeup) */
	bfin_write32(SIC_IWR, IWR_ENABLE(0));

	bfin_write16(VR_CTL, val);
	SSYNC();
	asm("IDLE;");

	bfin_write32(SIC_IWR, iwr);
	local_irq_restore_hw(flags);
}

#endif /* _MACH_PLL_H */
