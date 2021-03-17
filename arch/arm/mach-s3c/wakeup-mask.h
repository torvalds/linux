/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright 2010 Ben Dooks <ben-linux@fluff.org>
 *
 * Support for wakeup mask interrupts on newer SoCs
 */

#ifndef __PLAT_WAKEUP_MASK_H
#define __PLAT_WAKEUP_MASK_H __file__

/* if no irq yet defined, but still want to mask */
#define NO_WAKEUP_IRQ (0x90000000)

/**
 * struct samsung_wakeup_mask - wakeup mask information
 * @irq: The interrupt associated with this wakeup.
 * @bit: The bit, as a (1 << bitno) controlling this source.
 */ 
struct samsung_wakeup_mask {
	unsigned int	irq;
	u32		bit;
};

/**
 * samsung_sync_wakemask - sync wakeup mask information for pm
 * @reg: The register that is used.
 * @masks: The list of masks to use.
 * @nr_masks: The number of entries pointed to buy @masks.
 *
 * Synchronise the wakeup mask information at suspend time from the list
 * of interrupts and control bits in @masks. We do this at suspend time
 * as overriding the relevant irq chips is harder and the register is only
 * required to be correct before we enter sleep.
 */
extern void samsung_sync_wakemask(void __iomem *reg,
				  const struct samsung_wakeup_mask *masks,
				  int nr_masks);

#endif /* __PLAT_WAKEUP_MASK_H */
