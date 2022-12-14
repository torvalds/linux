/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * This file contains the address data for various TI81XX modules.
 *
 * Copyright (C) 2010 Texas Instruments, Inc. - https://www.ti.com/
 */

#ifndef __ASM_ARCH_TI81XX_H
#define __ASM_ARCH_TI81XX_H

#define L4_SLOW_TI81XX_BASE	0x48000000

#define TI81XX_SCM_BASE		0x48140000
#define TI81XX_CTRL_BASE	TI81XX_SCM_BASE
#define TI81XX_PRCM_BASE	0x48180000

/*
 * Adjust TAP register base such that omap3_check_revision accesses the correct
 * TI81XX register for checking device ID (it adds 0x204 to tap base while
 * TI81XX DEVICE ID register is at offset 0x600 from control base).
 */
#define TI81XX_TAP_BASE		(TI81XX_CTRL_BASE + \
				 TI81XX_CONTROL_DEVICE_ID - 0x204)


#define TI81XX_ARM_INTC_BASE	0x48200000

#endif /* __ASM_ARCH_TI81XX_H */
