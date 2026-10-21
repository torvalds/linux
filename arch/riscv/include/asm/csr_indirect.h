/* SPDX-License-Identifier: GPL-2.0-only */

#ifndef _ASM_RISCV_CSR_INDIRECT_H
#define _ASM_RISCV_CSR_INDIRECT_H

#include <linux/irqflags.h>

#include <asm/csr.h>

/*
 * These have to be macros rather than functions: csr_read()/csr_write()
 * stringify their CSR argument into the inline asm template via __ASM_STR(),
 * so the CSR number must be a literal token at preprocessing time. Passing it
 * as a function parameter emits "csrr %0, iregcsr", which no assembler can
 * resolve. RISC-V has no register-indirect form of csrr/csrw - the CSR is a
 * 12-bit immediate - so the sireg CSR selecting the indirect window cannot
 * itself be a variable.
 */
#define csr_indirect_read(iregcsr, iselbase, iseloff) ({		\
	unsigned long __value = 0;				\
	unsigned long __flags;					\
	local_irq_save(__flags);				\
	csr_write(CSR_ISELECT, (iselbase) + (iseloff));		\
	__value = csr_read(iregcsr);				\
	local_irq_restore(__flags);				\
	__value;						\
})

#define csr_indirect_write(iregcsr, iselbase, iseloff, value) ({	\
	unsigned long __flags;					\
	local_irq_save(__flags);				\
	csr_write(CSR_ISELECT, (iselbase) + (iseloff));		\
	csr_write(iregcsr, (value));				\
	local_irq_restore(__flags);				\
})

#define csr_indirect_warl(iregcsr, iselbase, iseloff, warl_val) ({	\
	unsigned long __old_val = 0, __value = 0;		\
	unsigned long __flags;					\
	local_irq_save(__flags);				\
	csr_write(CSR_ISELECT, (iselbase) + (iseloff));		\
	__old_val = csr_read(iregcsr);				\
	csr_write(iregcsr, (warl_val));				\
	__value = csr_read(iregcsr);				\
	csr_write(iregcsr, __old_val);				\
	local_irq_restore(__flags);				\
	__value;						\
})

#endif
