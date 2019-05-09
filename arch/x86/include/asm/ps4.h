/*
 * ps4.h: Sony PS4 platform setup code
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; version 2
 * of the License.
 */
#ifndef _ASM_X86_PS4_H
#define _ASM_X86_PS4_H

#ifdef CONFIG_X86_PS4

#include <linux/irqdomain.h>

#define PS4_DEFAULT_TSC_FREQ 1594000000

#define EMC_TIMER_BASE 0xd0281000
#define EMC_TIMER_VALUE 0x28

extern unsigned long ps4_calibrate_tsc(void);

/*
 * The PS4 Aeolia southbridge device is a composite device containing some
 * standard-ish, some not-so-standard, and some completely custom functions,
 * all using special MSI handling. This function does the equivalent of
 * pci_enable_msi_range and friends, for those devices. Only works after the
 * Aeolia MSR routing function device (function 4) has been probed.
 * Returns 1 or count, depending on IRQ allocation constraints, or negative on
 * error. Assigned IRQ(s) start at dev->irq.
 */
extern int apcie_assign_irqs(struct pci_dev *dev, int nvec);
extern void apcie_free_irqs(unsigned int virq, unsigned int nr_irqs);

extern int apcie_status(void);
extern int apcie_icc_cmd(u8 major, u16 minor, const void *data,
			 u16 length, void *reply, u16 reply_length);


#else

static inline int apcie_assign_irqs(struct pci_dev *dev, int nvec)
{
	return -ENODEV;
}
static inline void apcie_free_irqs(unsigned int virq, unsigned int nvec)
{
}
static inline int apcie_status(void)
{
	return -ENODEV;
}
static inline int apcie_icc_cmd(u8 major, u16 minor, const void *data,
				u16 length, void *reply, u16 reply_length)
{
	return -ENODEV;
}

#endif
#endif
