/* Copyright (c) 2010, Code Aurora Forum. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 and
 * only version 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA
 * 02110-1301, USA.
 *
 */

#if !defined(CONFIG_ARM_GIC)
#include <mach/msm_iomap.h>

	.macro	get_irqnr_preamble, base, tmp
	@ enable imprecise aborts
	cpsie	a
	mov	\base, #MSM_VIC_BASE
	.endm

	.macro	get_irqnr_and_base, irqnr, irqstat, base, tmp
	@ 0xD0 has irq# or old irq# if the irq has been handled
	@ 0xD4 has irq# or -1 if none pending *but* if you just
	@ read 0xD4 you never get the first irq for some reason
	ldr	\irqnr, [\base, #0xD0]
	ldr	\irqnr, [\base, #0xD4]
	cmp	\irqnr, #0xffffffff
	.endm
#endif
