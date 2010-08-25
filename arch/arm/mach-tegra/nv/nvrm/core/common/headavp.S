/*
 * arch/arm/mach-tegra/headavp.S
 *
 * AVP kernel launcher stub; programs the AVP MMU and jumps to the
 * kernel code. Must use ONLY ARMv4 instructions, and must be compiled
 * in ARM mode.
 *
 * Copyright (c) 2010, NVIDIA Corporation.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
  */

#include <linux/linkage.h>
#include <asm/assembler.h>
#include "headavp.h"

#define PTE0_COMPARE	0
/* the default translation will translate any VA within
 * 0x0010:0000..0x001f:ffff to the (megabyte-aligned) value written to
 * _tegra_avp_launcher_stub_data[AVP_LAUNCHER_MMU_PHYSICAL]
 */
 #define PTE0_DEFAULT	(0x00100000 | 0x3ff0)

#define PTE0_TRANSLATE	4

 #define TRANSLATE_DATA	(1 << 11)
 #define TRANSLATE_CODE	(1 << 10)
 #define TRANSLATE_WR	(1 << 9)
 #define TRANSLATE_RD	(1 << 8)
 #define TRANSLATE_HIT	(1 << 7)
 #define TRANSLATE_EN	(1 << 2)

#define TRANSLATE_OPT (TRANSLATE_DATA | TRANSLATE_CODE | TRANSLATE_WR | \
		       TRANSLATE_RD | TRANSLATE_HIT)

ENTRY(_tegra_avp_launcher_stub)
	adr	r4, _tegra_avp_launcher_stub_data
	ldmia	r4, {r0-r3}
	str	r2, [r0, #PTE0_COMPARE]
	bic	r3, r3, #0xff0
	bic	r3, r3, #0x00f
	orr	r3, r3, #TRANSLATE_OPT
	orr	r3, r3, #TRANSLATE_EN
	str	r3, [r0, #PTE0_TRANSLATE]
	bx	r1
	b	.
ENDPROC(_tegra_avp_launcher_stub)
	.type	_tegra_avp_launcher_stub_data, %object
ENTRY(_tegra_avp_launcher_stub_data)
	.long	AVP_MMU_TLB_BASE
	.long	0xdeadbeef
	.long	PTE0_DEFAULT
	.long	0xdeadd00d
	.size	_tegra_avp_launcher_stub_data, . - _tegra_avp_launcher_stub_data
