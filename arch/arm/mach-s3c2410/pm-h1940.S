/* linux/arch/arm/mach-s3c2410/pm-h1940.S
 *
 * Copyright (c) 2006 Ben Dooks <ben-linux@fluff.org>
 *
 * H1940 Suspend to RAM
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License
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
#include <asm/assembler.h>
#include <asm/hardware.h>
#include <asm/arch/map.h>

#include <asm/arch/regs-gpio.h>

	.text
	.global	h1940_pm_return

h1940_pm_return:
	mov	r0, #S3C2410_PA_GPIO
	ldr	pc, [ r0, #S3C2410_GSTATUS3 - S3C24XX_VA_GPIO ]
