/*
 * Copyright 2003-2011 NetLogic Microsystems, Inc. (NetLogic). All rights
 * reserved.
 *
 * This software is available to you under a choice of one of two
 * licenses.  You may choose to be licensed under the terms of the GNU
 * General Public License (GPL) Version 2, available from the file
 * COPYING in the main directory of this source tree, or the NetLogic
 * license below:
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in
 *    the documentation and/or other materials provided with the
 *    distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY NETLOGIC ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL NETLOGIC OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR
 * BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
 * WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE
 * OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN
 * IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#ifndef _ASM_NLM_MIPS_EXTS_H
#define _ASM_NLM_MIPS_EXTS_H

/*
 * XLR and XLP interrupt request and interrupt mask registers
 */
#define read_c0_eirr()		__read_64bit_c0_register($9, 6)
#define read_c0_eimr()		__read_64bit_c0_register($9, 7)
#define write_c0_eirr(val)	__write_64bit_c0_register($9, 6, val)

/*
 * NOTE: Do not save/restore flags around write_c0_eimr().
 * On non-R2 platforms the flags has part of EIMR that is shadowed in STATUS
 * register. Restoring flags will overwrite the lower 8 bits of EIMR.
 *
 * Call with interrupts disabled.
 */
#define write_c0_eimr(val)						\
do {									\
	if (sizeof(unsigned long) == 4) {				\
		__asm__ __volatile__(					\
			".set\tmips64\n\t"				\
			"dsll\t%L0, %L0, 32\n\t"			\
			"dsrl\t%L0, %L0, 32\n\t"			\
			"dsll\t%M0, %M0, 32\n\t"			\
			"or\t%L0, %L0, %M0\n\t"				\
			"dmtc0\t%L0, $9, 7\n\t"				\
			".set\tmips0"					\
			: : "r" (val));					\
	} else								\
		__write_64bit_c0_register($9, 7, (val));		\
} while (0)

/*
 * Handling the 64 bit EIMR and EIRR registers in 32-bit mode with
 * standard functions will be very inefficient. This provides
 * optimized functions for the normal operations on the registers.
 *
 * Call with interrupts disabled.
 */
static inline void ack_c0_eirr(int irq)
{
	__asm__ __volatile__(
		".set	push\n\t"
		".set	mips64\n\t"
		".set	noat\n\t"
		"li	$1, 1\n\t"
		"dsllv	$1, $1, %0\n\t"
		"dmtc0	$1, $9, 6\n\t"
		".set	pop"
		: : "r" (irq));
}

static inline void set_c0_eimr(int irq)
{
	__asm__ __volatile__(
		".set	push\n\t"
		".set	mips64\n\t"
		".set	noat\n\t"
		"li	$1, 1\n\t"
		"dsllv	%0, $1, %0\n\t"
		"dmfc0	$1, $9, 7\n\t"
		"or	$1, %0\n\t"
		"dmtc0	$1, $9, 7\n\t"
		".set	pop"
		: "+r" (irq));
}

static inline void clear_c0_eimr(int irq)
{
	__asm__ __volatile__(
		".set	push\n\t"
		".set	mips64\n\t"
		".set	noat\n\t"
		"li	$1, 1\n\t"
		"dsllv	%0, $1, %0\n\t"
		"dmfc0	$1, $9, 7\n\t"
		"or	$1, %0\n\t"
		"xor	$1, %0\n\t"
		"dmtc0	$1, $9, 7\n\t"
		".set	pop"
		: "+r" (irq));
}

/*
 * Read c0 eimr and c0 eirr, do AND of the two values, the result is
 * the interrupts which are raised and are not masked.
 */
static inline uint64_t read_c0_eirr_and_eimr(void)
{
	uint64_t val;

#ifdef CONFIG_64BIT
	val = read_c0_eimr() & read_c0_eirr();
#else
	__asm__ __volatile__(
		".set	push\n\t"
		".set	mips64\n\t"
		".set	noat\n\t"
		"dmfc0	%M0, $9, 6\n\t"
		"dmfc0	%L0, $9, 7\n\t"
		"and	%M0, %L0\n\t"
		"dsll	%L0, %M0, 32\n\t"
		"dsra	%M0, %M0, 32\n\t"
		"dsra	%L0, %L0, 32\n\t"
		".set	pop"
		: "=r" (val));
#endif

	return val;
}

static inline int hard_smp_processor_id(void)
{
	return __read_32bit_c0_register($15, 1) & 0x3ff;
}

static inline int nlm_nodeid(void)
{
	return (__read_32bit_c0_register($15, 1) >> 5) & 0x3;
}

static inline unsigned int nlm_core_id(void)
{
	return (read_c0_ebase() & 0x1c) >> 2;
}

static inline unsigned int nlm_thread_id(void)
{
	return read_c0_ebase() & 0x3;
}

#define __read_64bit_c2_split(source, sel)				\
({									\
	unsigned long long __val;					\
	unsigned long __flags;						\
									\
	local_irq_save(__flags);					\
	if (sel == 0)							\
		__asm__ __volatile__(					\
			".set\tmips64\n\t"				\
			"dmfc2\t%M0, " #source "\n\t"			\
			"dsll\t%L0, %M0, 32\n\t"			\
			"dsra\t%M0, %M0, 32\n\t"			\
			"dsra\t%L0, %L0, 32\n\t"			\
			".set\tmips0\n\t"				\
			: "=r" (__val));				\
	else								\
		__asm__ __volatile__(					\
			".set\tmips64\n\t"				\
			"dmfc2\t%M0, " #source ", " #sel "\n\t"		\
			"dsll\t%L0, %M0, 32\n\t"			\
			"dsra\t%M0, %M0, 32\n\t"			\
			"dsra\t%L0, %L0, 32\n\t"			\
			".set\tmips0\n\t"				\
			: "=r" (__val));				\
	local_irq_restore(__flags);					\
									\
	__val;								\
})

#define __write_64bit_c2_split(source, sel, val)			\
do {									\
	unsigned long __flags;						\
									\
	local_irq_save(__flags);					\
	if (sel == 0)							\
		__asm__ __volatile__(					\
			".set\tmips64\n\t"				\
			"dsll\t%L0, %L0, 32\n\t"			\
			"dsrl\t%L0, %L0, 32\n\t"			\
			"dsll\t%M0, %M0, 32\n\t"			\
			"or\t%L0, %L0, %M0\n\t"				\
			"dmtc2\t%L0, " #source "\n\t"			\
			".set\tmips0\n\t"				\
			: : "r" (val));					\
	else								\
		__asm__ __volatile__(					\
			".set\tmips64\n\t"				\
			"dsll\t%L0, %L0, 32\n\t"			\
			"dsrl\t%L0, %L0, 32\n\t"			\
			"dsll\t%M0, %M0, 32\n\t"			\
			"or\t%L0, %L0, %M0\n\t"				\
			"dmtc2\t%L0, " #source ", " #sel "\n\t"		\
			".set\tmips0\n\t"				\
			: : "r" (val));					\
	local_irq_restore(__flags);					\
} while (0)

#define __read_32bit_c2_register(source, sel)				\
({ uint32_t __res;							\
	if (sel == 0)							\
		__asm__ __volatile__(					\
			".set\tmips32\n\t"				\
			"mfc2\t%0, " #source "\n\t"			\
			".set\tmips0\n\t"				\
			: "=r" (__res));				\
	else								\
		__asm__ __volatile__(					\
			".set\tmips32\n\t"				\
			"mfc2\t%0, " #source ", " #sel "\n\t"		\
			".set\tmips0\n\t"				\
			: "=r" (__res));				\
	__res;								\
})

#define __read_64bit_c2_register(source, sel)				\
({ unsigned long long __res;						\
	if (sizeof(unsigned long) == 4)					\
		__res = __read_64bit_c2_split(source, sel);		\
	else if (sel == 0)						\
		__asm__ __volatile__(					\
			".set\tmips64\n\t"				\
			"dmfc2\t%0, " #source "\n\t"			\
			".set\tmips0\n\t"				\
			: "=r" (__res));				\
	else								\
		__asm__ __volatile__(					\
			".set\tmips64\n\t"				\
			"dmfc2\t%0, " #source ", " #sel "\n\t"		\
			".set\tmips0\n\t"				\
			: "=r" (__res));				\
	__res;								\
})

#define __write_64bit_c2_register(register, sel, value)			\
do {									\
	if (sizeof(unsigned long) == 4)					\
		__write_64bit_c2_split(register, sel, value);		\
	else if (sel == 0)						\
		__asm__ __volatile__(					\
			".set\tmips64\n\t"				\
			"dmtc2\t%z0, " #register "\n\t"			\
			".set\tmips0\n\t"				\
			: : "Jr" (value));				\
	else								\
		__asm__ __volatile__(					\
			".set\tmips64\n\t"				\
			"dmtc2\t%z0, " #register ", " #sel "\n\t"	\
			".set\tmips0\n\t"				\
			: : "Jr" (value));				\
} while (0)

#define __write_32bit_c2_register(reg, sel, value)			\
({									\
	if (sel == 0)							\
		__asm__ __volatile__(					\
			".set\tmips32\n\t"				\
			"mtc2\t%z0, " #reg "\n\t"			\
			".set\tmips0\n\t"				\
			: : "Jr" (value));				\
	else								\
		__asm__ __volatile__(					\
			".set\tmips32\n\t"				\
			"mtc2\t%z0, " #reg ", " #sel "\n\t"		\
			".set\tmips0\n\t"				\
			: : "Jr" (value));				\
})

#endif /*_ASM_NLM_MIPS_EXTS_H */
