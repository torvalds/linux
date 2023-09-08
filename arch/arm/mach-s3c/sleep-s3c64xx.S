/* SPDX-License-Identifier: GPL-2.0 */
/* linux/arch/arm/plat-s3c64xx/sleep.S
 *
 * Copyright 2008 Openmoko, Inc.
 * Copyright 2008 Simtec Electronics
 *	Ben Dooks <ben@simtec.co.uk>
 *	http://armlinux.simtec.co.uk/
 *
 * S3C64XX CPU sleep code
 */

#include <linux/linkage.h>
#include <asm/assembler.h>
#include "map.h"

#undef S3C64XX_VA_GPIO
#define S3C64XX_VA_GPIO (0x0)

#include "regs-gpio.h"

#define LL_UART (S3C_PA_UART + (0x400 * CONFIG_S3C_LOWLEVEL_UART_PORT))

	.text

	/* Sleep magic, the word before the resume entry point so that the
	 * bootloader can check for a resumeable image. */

	.word	0x2bedf00d

	/* s3c_cpu_reusme
	 *
	 * This is the entry point, stored by whatever method the bootloader
	 * requires to get the kernel runnign again. This code expects to be
	 * entered with no caches live and the MMU disabled. It will then
	 * restore the MMU and other basic CP registers saved and restart
	 * the kernel C code to finish the resume code.
	*/

ENTRY(s3c_cpu_resume)
	msr	cpsr_c, #PSR_I_BIT | PSR_F_BIT | SVC_MODE
	ldr	r2, =LL_UART		/* for debug */
	b	cpu_resume
