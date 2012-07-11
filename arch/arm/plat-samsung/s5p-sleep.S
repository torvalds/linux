/*
 * Copyright (c) 2011 Samsung Electronics Co., Ltd.
 *		http://www.samsung.com
 *
 * Common S5P Sleep Code
 * Based on S3C64XX sleep code by:
 *	Ben Dooks, (c) 2008 Simtec Electronics
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
*/

#include <linux/linkage.h>
#include <asm/asm-offsets.h>
#include <asm/hardware/cache-l2x0.h>

/*
 *	 The following code is located into the .data section. This is to
 *	 allow l2x0_regs_phys to be accessed with a relative load while we
 *	 can't rely on any MMU translation. We could have put l2x0_regs_phys
 *	 in the .text section as well, but some setups might insist on it to
 *	 be truly read-only. (Reference from: arch/arm/kernel/sleep.S)
 */
	.data
	.align

	/*
	 * sleep magic, to allow the bootloader to check for an valid
	 * image to resume to. Must be the first word before the
	 * s3c_cpu_resume entry.
	 */

	.word	0x2bedf00d

	/*
	 * s3c_cpu_resume
	 *
	 * resume code entry for bootloader to call
	 */

ENTRY(s3c_cpu_resume)
#ifdef CONFIG_CACHE_L2X0
	adr	r0, l2x0_regs_phys
	ldr	r0, [r0]
	ldr	r1, [r0, #L2X0_R_PHY_BASE]
	ldr	r2, [r1, #L2X0_CTRL]
	tst	r2, #0x1
	bne	resume_l2on
	ldr	r2, [r0, #L2X0_R_AUX_CTRL]
	str	r2, [r1, #L2X0_AUX_CTRL]
	ldr	r2, [r0, #L2X0_R_TAG_LATENCY]
	str	r2, [r1, #L2X0_TAG_LATENCY_CTRL]
	ldr	r2, [r0, #L2X0_R_DATA_LATENCY]
	str	r2, [r1, #L2X0_DATA_LATENCY_CTRL]
	ldr	r2, [r0, #L2X0_R_PREFETCH_CTRL]
	str	r2, [r1, #L2X0_PREFETCH_CTRL]
	ldr	r2, [r0, #L2X0_R_PWR_CTRL]
	str	r2, [r1, #L2X0_POWER_CTRL]
	mov	r2, #1
	str	r2, [r1, #L2X0_CTRL]
resume_l2on:
#endif
	b	cpu_resume
ENDPROC(s3c_cpu_resume)
#ifdef CONFIG_CACHE_L2X0
	.globl l2x0_regs_phys
l2x0_regs_phys:
	.long	0
#endif
