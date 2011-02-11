/*
 *  Atheros AR71XX/AR724X/AR913X specific kernel entry setup
 *
 *  Copyright (C) 2009 Gabor Juhos <juhosg@openwrt.org>
 *
 *  This program is free software; you can redistribute it and/or modify it
 *  under the terms of the GNU General Public License version 2 as published
 *  by the Free Software Foundation.
 *
 */
#ifndef __ASM_MACH_ATH79_KERNEL_ENTRY_H
#define __ASM_MACH_ATH79_KERNEL_ENTRY_H

	/*
	 * Some bootloaders set the 'Kseg0 coherency algorithm' to
	 * 'Cacheable, noncoherent, write-through, no write allocate'
	 * and this cause performance issues. Let's go and change it to
	 * 'Cacheable, noncoherent, write-back, write allocate'
	 */
	.macro	kernel_entry_setup
	mfc0	t0, CP0_CONFIG
	li	t1, ~CONF_CM_CMASK
	and	t0, t1
	ori	t0, CONF_CM_CACHABLE_NONCOHERENT
	mtc0	t0, CP0_CONFIG
	nop
	.endm

	.macro	smp_slave_setup
	.endm

#endif /* __ASM_MACH_ATH79_KERNEL_ENTRY_H */
