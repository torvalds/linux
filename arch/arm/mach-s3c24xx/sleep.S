/* linux/arch/arm/plat-s3c24xx/sleep.S
 *
 * Copyright (c) 2004 Simtec Electronics
 *	Ben Dooks <ben@simtec.co.uk>
 *
 * S3C2410 Power Manager (Suspend-To-RAM) support
 *
 * Based on PXA/SA1100 sleep code by:
 *	Nicolas Pitre, (c) 2002 Monta Vista Software Inc
 *	Cliff Brake, (c) 2001
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
#include <linux/serial_s3c.h>
#include <asm/assembler.h>
#include <mach/hardware.h>
#include <mach/map.h>

#include <mach/regs-gpio.h>
#include <mach/regs-clock.h>

/*
 * S3C24XX_DEBUG_RESUME is dangerous if your bootloader does not
 * reset the UART configuration, only enable if you really need this!
 */
//#define S3C24XX_DEBUG_RESUME

	.text

	/* sleep magic, to allow the bootloader to check for an valid
	 * image to resume to. Must be the first word before the
	 * s3c_cpu_resume entry.
	*/

	.word	0x2bedf00d

	/* s3c_cpu_resume
	 *
	 * resume code entry for bootloader to call
	*/

ENTRY(s3c_cpu_resume)
	mov	r0, #PSR_I_BIT | PSR_F_BIT | SVC_MODE
	msr	cpsr_c, r0

	@@ load UART to allow us to print the two characters for
	@@ resume debug

	mov	r2, #S3C24XX_PA_UART & 0xff000000
	orr	r2, r2, #S3C24XX_PA_UART & 0xff000

#if 0
	/* SMDK2440 LED set */
	mov	r14, #S3C24XX_PA_GPIO
	ldr	r12, [ r14, #0x54 ]
	bic	r12, r12, #3<<4
	orr	r12, r12, #1<<7
	str	r12, [ r14, #0x54 ]
#endif

#ifdef S3C24XX_DEBUG_RESUME
	mov	r3, #'L'
	strb	r3, [ r2, #S3C2410_UTXH ]
1001:
	ldrb	r14, [ r3, #S3C2410_UTRSTAT ]
	tst	r14, #S3C2410_UTRSTAT_TXE
	beq	1001b
#endif /* S3C24XX_DEBUG_RESUME */

	b	cpu_resume
