/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (c) 2006  Ralf Baechle (ralf@linux-mips.org)
 */
#ifndef _ASM_FUTEX_H
#define _ASM_FUTEX_H

#ifdef __KERNEL__

#include <linux/futex.h>
#include <linux/uaccess.h>
#include <asm/asm-eva.h>
#include <asm/barrier.h>
#include <asm/compiler.h>
#include <asm/errno.h>
#include <asm/sync.h>
#include <asm/war.h>

#define __futex_atomic_op(insn, ret, oldval, uaddr, oparg)		\
{									\
	if (cpu_has_llsc && IS_ENABLED(CONFIG_WAR_R10000_LLSC)) {	\
		__asm__ __volatile__(					\
		"	.set	push				\n"	\
		"	.set	noat				\n"	\
		"	.set	push				\n"	\
		"	.set	arch=r4000			\n"	\
		"1:	ll	%1, %4	# __futex_atomic_op	\n"	\
		"	.set	pop				\n"	\
		"	" insn	"				\n"	\
		"	.set	arch=r4000			\n"	\
		"2:	sc	$1, %2				\n"	\
		"	beqzl	$1, 1b				\n"	\
		__stringify(__WEAK_LLSC_MB) "			\n"	\
		"3:						\n"	\
		"	.insn					\n"	\
		"	.set	pop				\n"	\
		"	.section .fixup,\"ax\"			\n"	\
		"4:	li	%0, %6				\n"	\
		"	j	3b				\n"	\
		"	.previous				\n"	\
		"	.section __ex_table,\"a\"		\n"	\
		"	"__UA_ADDR "\t1b, 4b			\n"	\
		"	"__UA_ADDR "\t2b, 4b			\n"	\
		"	.previous				\n"	\
		: "=r" (ret), "=&r" (oldval),				\
		  "=" GCC_OFF_SMALL_ASM() (*uaddr)				\
		: "0" (0), GCC_OFF_SMALL_ASM() (*uaddr), "Jr" (oparg),	\
		  "i" (-EFAULT)						\
		: "memory");						\
	} else if (cpu_has_llsc) {					\
		__asm__ __volatile__(					\
		"	.set	push				\n"	\
		"	.set	noat				\n"	\
		"	.set	push				\n"	\
		"	.set	"MIPS_ISA_ARCH_LEVEL"		\n"	\
		"	" __SYNC(full, loongson3_war) "		\n"	\
		"1:	"user_ll("%1", "%4")" # __futex_atomic_op\n"	\
		"	.set	pop				\n"	\
		"	" insn	"				\n"	\
		"	.set	"MIPS_ISA_ARCH_LEVEL"		\n"	\
		"2:	"user_sc("$1", "%2")"			\n"	\
		"	beqz	$1, 1b				\n"	\
		__stringify(__WEAK_LLSC_MB) "			\n"	\
		"3:						\n"	\
		"	.insn					\n"	\
		"	.set	pop				\n"	\
		"	.section .fixup,\"ax\"			\n"	\
		"4:	li	%0, %6				\n"	\
		"	j	3b				\n"	\
		"	.previous				\n"	\
		"	.section __ex_table,\"a\"		\n"	\
		"	"__UA_ADDR "\t1b, 4b			\n"	\
		"	"__UA_ADDR "\t2b, 4b			\n"	\
		"	.previous				\n"	\
		: "=r" (ret), "=&r" (oldval),				\
		  "=" GCC_OFF_SMALL_ASM() (*uaddr)				\
		: "0" (0), GCC_OFF_SMALL_ASM() (*uaddr), "Jr" (oparg),	\
		  "i" (-EFAULT)						\
		: "memory");						\
	} else								\
		ret = -ENOSYS;						\
}

static inline int
arch_futex_atomic_op_inuser(int op, int oparg, int *oval, u32 __user *uaddr)
{
	int oldval = 0, ret;

	if (!access_ok(uaddr, sizeof(u32)))
		return -EFAULT;

	switch (op) {
	case FUTEX_OP_SET:
		__futex_atomic_op("move $1, %z5", ret, oldval, uaddr, oparg);
		break;

	case FUTEX_OP_ADD:
		__futex_atomic_op("addu $1, %1, %z5",
				  ret, oldval, uaddr, oparg);
		break;
	case FUTEX_OP_OR:
		__futex_atomic_op("or	$1, %1, %z5",
				  ret, oldval, uaddr, oparg);
		break;
	case FUTEX_OP_ANDN:
		__futex_atomic_op("and	$1, %1, %z5",
				  ret, oldval, uaddr, ~oparg);
		break;
	case FUTEX_OP_XOR:
		__futex_atomic_op("xor	$1, %1, %z5",
				  ret, oldval, uaddr, oparg);
		break;
	default:
		ret = -ENOSYS;
	}

	if (!ret)
		*oval = oldval;

	return ret;
}

static inline int
futex_atomic_cmpxchg_inatomic(u32 *uval, u32 __user *uaddr,
			      u32 oldval, u32 newval)
{
	int ret = 0;
	u32 val;

	if (!access_ok(uaddr, sizeof(u32)))
		return -EFAULT;

	if (cpu_has_llsc && IS_ENABLED(CONFIG_WAR_R10000_LLSC)) {
		__asm__ __volatile__(
		"# futex_atomic_cmpxchg_inatomic			\n"
		"	.set	push					\n"
		"	.set	noat					\n"
		"	.set	push					\n"
		"	.set	arch=r4000				\n"
		"1:	ll	%1, %3					\n"
		"	bne	%1, %z4, 3f				\n"
		"	.set	pop					\n"
		"	move	$1, %z5					\n"
		"	.set	arch=r4000				\n"
		"2:	sc	$1, %2					\n"
		"	beqzl	$1, 1b					\n"
		__stringify(__WEAK_LLSC_MB) "				\n"
		"3:							\n"
		"	.insn						\n"
		"	.set	pop					\n"
		"	.section .fixup,\"ax\"				\n"
		"4:	li	%0, %6					\n"
		"	j	3b					\n"
		"	.previous					\n"
		"	.section __ex_table,\"a\"			\n"
		"	"__UA_ADDR "\t1b, 4b				\n"
		"	"__UA_ADDR "\t2b, 4b				\n"
		"	.previous					\n"
		: "+r" (ret), "=&r" (val), "=" GCC_OFF_SMALL_ASM() (*uaddr)
		: GCC_OFF_SMALL_ASM() (*uaddr), "Jr" (oldval), "Jr" (newval),
		  "i" (-EFAULT)
		: "memory");
	} else if (cpu_has_llsc) {
		__asm__ __volatile__(
		"# futex_atomic_cmpxchg_inatomic			\n"
		"	.set	push					\n"
		"	.set	noat					\n"
		"	.set	push					\n"
		"	.set	"MIPS_ISA_ARCH_LEVEL"			\n"
		"	" __SYNC(full, loongson3_war) "			\n"
		"1:	"user_ll("%1", "%3")"				\n"
		"	bne	%1, %z4, 3f				\n"
		"	.set	pop					\n"
		"	move	$1, %z5					\n"
		"	.set	"MIPS_ISA_ARCH_LEVEL"			\n"
		"2:	"user_sc("$1", "%2")"				\n"
		"	beqz	$1, 1b					\n"
		"3:	" __SYNC_ELSE(full, loongson3_war, __WEAK_LLSC_MB) "\n"
		"	.insn						\n"
		"	.set	pop					\n"
		"	.section .fixup,\"ax\"				\n"
		"4:	li	%0, %6					\n"
		"	j	3b					\n"
		"	.previous					\n"
		"	.section __ex_table,\"a\"			\n"
		"	"__UA_ADDR "\t1b, 4b				\n"
		"	"__UA_ADDR "\t2b, 4b				\n"
		"	.previous					\n"
		: "+r" (ret), "=&r" (val), "=" GCC_OFF_SMALL_ASM() (*uaddr)
		: GCC_OFF_SMALL_ASM() (*uaddr), "Jr" (oldval), "Jr" (newval),
		  "i" (-EFAULT)
		: "memory");
	} else
		return -ENOSYS;

	*uval = val;
	return ret;
}

#endif
#endif /* _ASM_FUTEX_H */
