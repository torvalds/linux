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

#ifndef __NLM_HAL_HALDEFS_H__
#define __NLM_HAL_HALDEFS_H__

#include <linux/irqflags.h>	/* for local_irq_disable */

/*
 * This file contains platform specific memory mapped IO implementation
 * and will provide a way to read 32/64 bit memory mapped registers in
 * all ABIs
 */
/*
 * For o32 compilation, we have to disable interrupts and enable KX bit to
 * access 64 bit addresses or data.
 *
 * We need to disable interrupts because we save just the lower 32 bits of
 * registers in	 interrupt handling. So if we get hit by an interrupt while
 * using the upper 32 bits of a register, we lose.
 */
static inline uint32_t nlm_save_flags_kx(void)
{
	return change_c0_status(ST0_KX | ST0_IE, ST0_KX);
}

static inline uint32_t nlm_save_flags_cop2(void)
{
	return change_c0_status(ST0_CU2 | ST0_IE, ST0_CU2);
}

static inline void nlm_restore_flags(uint32_t sr)
{
	write_c0_status(sr);
}

/*
 * The n64 implementations are simple, the o32 implementations when they
 * are added, will have to disable interrupts and enable KX before doing
 * 64 bit ops.
 */
static inline uint32_t
nlm_read_reg(uint64_t base, uint32_t reg)
{
	volatile uint32_t *addr = (volatile uint32_t *)(long)base + reg;

	return *addr;
}

static inline void
nlm_write_reg(uint64_t base, uint32_t reg, uint32_t val)
{
	volatile uint32_t *addr = (volatile uint32_t *)(long)base + reg;

	*addr = val;
}

/*
 * For o32 compilation, we have to disable interrupts to access 64 bit
 * registers
 *
 * We need to disable interrupts because we save just the lower 32 bits of
 * registers in  interrupt handling. So if we get hit by an interrupt while
 * using the upper 32 bits of a register, we lose.
 */

static inline uint64_t
nlm_read_reg64(uint64_t base, uint32_t reg)
{
	uint64_t addr = base + (reg >> 1) * sizeof(uint64_t);
	volatile uint64_t *ptr = (volatile uint64_t *)(long)addr;
	uint64_t val;

	if (sizeof(unsigned long) == 4) {
		unsigned long flags;

		local_irq_save(flags);
		__asm__ __volatile__(
			".set	push"			"\n\t"
			".set	mips64"			"\n\t"
			"ld	%L0, %1"		"\n\t"
			"dsra32	%M0, %L0, 0"		"\n\t"
			"sll	%L0, %L0, 0"		"\n\t"
			".set	pop"			"\n"
			: "=r" (val)
			: "m" (*ptr));
		local_irq_restore(flags);
	} else
		val = *ptr;

	return val;
}

static inline void
nlm_write_reg64(uint64_t base, uint32_t reg, uint64_t val)
{
	uint64_t addr = base + (reg >> 1) * sizeof(uint64_t);
	volatile uint64_t *ptr = (volatile uint64_t *)(long)addr;

	if (sizeof(unsigned long) == 4) {
		unsigned long flags;
		uint64_t tmp;

		local_irq_save(flags);
		__asm__ __volatile__(
			".set	push"			"\n\t"
			".set	mips64"			"\n\t"
			"dsll32	%L0, %L0, 0"		"\n\t"
			"dsrl32	%L0, %L0, 0"		"\n\t"
			"dsll32	%M0, %M0, 0"		"\n\t"
			"or	%L0, %L0, %M0"		"\n\t"
			"sd	%L0, %2"		"\n\t"
			".set	pop"			"\n"
			: "=r" (tmp)
			: "0" (val), "m" (*ptr));
		local_irq_restore(flags);
	} else
		*ptr = val;
}

/*
 * Routines to store 32/64 bit values to 64 bit addresses,
 * used when going thru XKPHYS to access registers
 */
static inline uint32_t
nlm_read_reg_xkphys(uint64_t base, uint32_t reg)
{
	return nlm_read_reg(base, reg);
}

static inline void
nlm_write_reg_xkphys(uint64_t base, uint32_t reg, uint32_t val)
{
	nlm_write_reg(base, reg, val);
}

static inline uint64_t
nlm_read_reg64_xkphys(uint64_t base, uint32_t reg)
{
	return nlm_read_reg64(base, reg);
}

static inline void
nlm_write_reg64_xkphys(uint64_t base, uint32_t reg, uint64_t val)
{
	nlm_write_reg64(base, reg, val);
}

/* Location where IO base is mapped */
extern uint64_t nlm_io_base;

#if defined(CONFIG_CPU_XLP)
static inline uint64_t
nlm_pcicfg_base(uint32_t devoffset)
{
	return nlm_io_base + devoffset;
}

static inline uint64_t
nlm_xkphys_map_pcibar0(uint64_t pcibase)
{
	uint64_t paddr;

	paddr = nlm_read_reg(pcibase, 0x4) & ~0xfu;
	return (uint64_t)0x9000000000000000 | paddr;
}
#elif defined(CONFIG_CPU_XLR)

static inline uint64_t
nlm_mmio_base(uint32_t devoffset)
{
	return nlm_io_base + devoffset;
}
#endif

#endif
