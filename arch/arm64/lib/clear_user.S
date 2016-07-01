/*
 * Based on arch/arm/lib/clear_user.S
 *
 * Copyright (C) 2012 ARM Ltd.
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
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */
#include <linux/linkage.h>

#include <asm/assembler.h>
#include <asm/cpufeature.h>
#include <asm/sysreg.h>
#include <asm/uaccess.h>

	.text

/* Prototype: int __clear_user(void *addr, size_t sz)
 * Purpose  : clear some user memory
 * Params   : addr - user memory address to clear
 *          : sz   - number of bytes to clear
 * Returns  : number of bytes NOT cleared
 *
 * Alignment fixed up by hardware.
 */
ENTRY(__clear_user)
	uaccess_enable_not_uao x2, x3
	mov	x2, x1			// save the size for fixup return
	subs	x1, x1, #8
	b.mi	2f
1:
uao_user_alternative 9f, str, sttr, xzr, x0, 8
	subs	x1, x1, #8
	b.pl	1b
2:	adds	x1, x1, #4
	b.mi	3f
uao_user_alternative 9f, str, sttr, wzr, x0, 4
	sub	x1, x1, #4
3:	adds	x1, x1, #2
	b.mi	4f
uao_user_alternative 9f, strh, sttrh, wzr, x0, 2
	sub	x1, x1, #2
4:	adds	x1, x1, #1
	b.mi	5f
uao_user_alternative 9f, strb, sttrb, wzr, x0, 0
5:	mov	x0, #0
	uaccess_disable_not_uao x2
	ret
ENDPROC(__clear_user)

	.section .fixup,"ax"
	.align	2
9:	mov	x0, x2			// return the original size
	ret
	.previous
