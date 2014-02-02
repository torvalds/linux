/*
 * Marvell Tauros3 cache controller includes
 *
 * Sebastian Hesselbarth <sebastian.hesselbarth@gmail.com>
 *
 * based on GPL'ed 2.6 kernel sources
 *  (c) Marvell International Ltd.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA
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
