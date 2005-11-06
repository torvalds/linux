/*
 * Linux/PA-RISC Project (http://www.parisc-linux.org/)
 *
 *  Copyright (C) 2004  Randolph Chung <tausq@debian.org>
 *
 *    This program is free software; you can redistribute it and/or modify
 *    it under the terms of the GNU General Public License as published by
 *    the Free Software Foundation; either version 2, or (at your option)
 *    any later version.
 *
 *    This program is distributed in the hope that it will be useful,
 *    but WITHOUT ANY WARRANTY; without even the implied warranty of
 *    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *    GNU General Public License for more details.
 *
 *    You should have received a copy of the GNU General Public License
 *    along with this program; if not, write to the Free Software
 *    Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 * 
 * Fixup routines for kernel exception handling.
 */
#include <linux/config.h>
#include <asm/asm-offsets.h>
#include <asm/assembly.h>
#include <asm/errno.h>

#ifdef CONFIG_SMP
	.macro  get_fault_ip t1 t2
	addil LT%__per_cpu_offset,%r27
	LDREG RT%__per_cpu_offset(%r1),\t1
	/* t2 = smp_processor_id() */
	mfctl 30,\t2
	ldw TI_CPU(\t2),\t2
#ifdef __LP64__
	extrd,u \t2,63,32,\t2
#endif
	/* t2 = &__per_cpu_offset[smp_processor_id()]; */
	LDREGX \t2(\t1),\t2 
	addil LT%per_cpu__exception_data,%r27
	LDREG RT%per_cpu__exception_data(%r1),\t1
	/* t1 = &__get_cpu_var(exception_data) */
	add,l \t1,\t2,\t1
	/* t1 = t1->fault_ip */
	LDREG EXCDATA_IP(\t1), \t1
	.endm
#else
	.macro  get_fault_ip t1 t2
	/* t1 = &__get_cpu_var(exception_data) */
	addil LT%per_cpu__exception_data,%r27
	LDREG RT%per_cpu__exception_data(%r1),\t2
	/* t1 = t2->fault_ip */
	LDREG EXCDATA_IP(\t2), \t1
	.endm
#endif

	.level LEVEL

	.text
	.section .fixup, "ax"

	/* get_user() fixups, store -EFAULT in r8, and 0 in r9 */
	.export fixup_get_user_skip_1
fixup_get_user_skip_1:
	get_fault_ip %r1,%r8
	ldo 4(%r1), %r1
	ldi -EFAULT, %r8
	bv %r0(%r1)
	copy %r0, %r9

	.export fixup_get_user_skip_2
fixup_get_user_skip_2:
	get_fault_ip %r1,%r8
	ldo 8(%r1), %r1
	ldi -EFAULT, %r8
	bv %r0(%r1)
	copy %r0, %r9

	/* put_user() fixups, store -EFAULT in r8 */
	.export fixup_put_user_skip_1
fixup_put_user_skip_1:
	get_fault_ip %r1,%r8
	ldo 4(%r1), %r1
	bv %r0(%r1)
	ldi -EFAULT, %r8

	.export fixup_put_user_skip_2
fixup_put_user_skip_2:
	get_fault_ip %r1,%r8
	ldo 8(%r1), %r1
	bv %r0(%r1)
	ldi -EFAULT, %r8
