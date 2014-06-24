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
	b	cpu_resume
ENDPROC(s3c_cpu_resume)
