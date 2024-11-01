/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Marvell Tauros3 cache controller includes
 *
 * Sebastian Hesselbarth <sebastian.hesselbarth@gmail.com>
 *
 * based on GPL'ed 2.6 kernel sources
 *  (c) Marvell International Ltd.
 */

#ifndef __ASM_ARM_HARDWARE_TAUROS3_H
#define __ASM_ARM_HARDWARE_TAUROS3_H

/*
 * Marvell Tauros3 L2CC is compatible with PL310 r0p0
 * but with PREFETCH_CTRL (r2p0) and an additional event counter.
 * Also, there is AUX2_CTRL for some Marvell specific control.
 */

#define TAUROS3_EVENT_CNT2_CFG		0x224
#define TAUROS3_EVENT_CNT2_VAL		0x228
#define TAUROS3_INV_ALL			0x780
#define TAUROS3_CLEAN_ALL		0x784
#define TAUROS3_AUX2_CTRL		0x820

/* Registers shifts and masks */
#define TAUROS3_AUX2_CTRL_LINEFILL_BURST8_EN	(1 << 2)

#endif
