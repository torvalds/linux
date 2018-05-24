/*
 * Copyright (C) 2015 Regents of the University of California
 *
 *   This program is free software; you can redistribute it and/or
 *   modify it under the terms of the GNU General Public License
 *   as published by the Free Software Foundation, version 2.
 *
 *   This program is distributed in the hope that it will be useful,
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *   GNU General Public License for more details.
 */

#ifndef _ASM_RISCV_CSR_H
#define _ASM_RISCV_CSR_H

#include <linux/const.h>

/* Status register flags */
#define SR_SIE	_AC(0x00000002, UL) /* Supervisor Interrupt Enable */
#define SR_SPIE	_AC(0x00000020, UL) /* Previous Supervisor IE */
#define SR_SPP	_AC(0x00000100, UL) /* Previously Supervisor */
#define SR_SUM	_AC(0x00040000, UL) /* Supervisor may access User Memory */

#define SR_FS           _AC(0x00006000, UL) /* Floating-point Status */
#define SR_FS_OFF       _AC(0x00000000, UL)
#define SR_FS_INITIAL   _AC(0x00002000, UL)
#define SR_FS_CLEAN     _AC(0x00004000, UL)
#define SR_FS_DIRTY     _AC(0x00006000, UL)

#define SR_XS           _AC(0x00018000, UL) /* Extension Status */
#define SR_XS_OFF       _AC(0x00000000, UL)
#define SR_XS_INITIAL   _AC(0x00008000, UL)
#define SR_XS_CLEAN     _AC(0x00010000, UL)
#define SR_XS_DIRTY     _AC(0x00018000, UL)

#ifndef CONFIG_64BIT
#define SR_SD   _AC(0x80000000, UL) /* FS/XS dirty */
#else
#define SR_SD   _AC(0x8000000000000000, UL) /* FS/XS dirty */
#endif

/* SATP flags */
#if __riscv_xlen == 32
#define SATP_PPN     _AC(0x003FFFFF, UL)
#define SATP_MODE_32 _AC(0x80000000, UL)
#define SATP_MODE    SATP_MODE_32
#else
#define SATP_PPN     _AC(0x00000FFFFFFFFFFF, UL)
#define SATP_MODE_39 _AC(0x8000000000000000, UL)
#define SATP_MODE    SATP_MODE_39
#endif

/* Interrupt Enable and Interrupt Pending flags */
#define SIE_SSIE _AC(0x00000002, UL) /* Software Interrupt Enable */
#define SIE_STIE _AC(0x00000020, UL) /* Timer Interrupt Enable */

#define EXC_INST_MISALIGNED     0
#define EXC_INST_ACCESS         1
#define EXC_BREAKPOINT          3
#define EXC_LOAD_ACCESS         5
#define EXC_STORE_ACCESS        7
#define EXC_SYSCALL             8
#define EXC_INST_PAGE_FAULT     12
#define EXC_LOAD_PAGE_FAULT     13
#define EXC_STORE_PAGE_FAULT    15

#ifndef __ASSEMBLY__

#define csr_swap(csr, val)					\
({								\
	unsigned long __v = (unsigned long)(val);		\
	__asm__ __volatile__ ("csrrw %0, " #csr ", %1"		\
			      : "=r" (__v) : "rK" (__v)		\
			      : "memory");			\
	__v;							\
})

#define csr_read(csr)						\
({								\
	register unsigned long __v;				\
	__asm__ __volatile__ ("csrr %0, " #csr			\
			      : "=r" (__v) :			\
			      : "memory");			\
	__v;							\
})

#define csr_write(csr, val)					\
({								\
	unsigned long __v = (unsigned long)(val);		\
	__asm__ __volatile__ ("csrw " #csr ", %0"		\
			      : : "rK" (__v)			\
			      : "memory");			\
})

#define csr_read_set(csr, val)					\
({								\
	unsigned long __v = (unsigned long)(val);		\
	__asm__ __volatile__ ("csrrs %0, " #csr ", %1"		\
			      : "=r" (__v) : "rK" (__v)		\
			      : "memory");			\
	__v;							\
})

#define csr_set(csr, val)					\
({								\
	unsigned long __v = (unsigned long)(val);		\
	__asm__ __volatile__ ("csrs " #csr ", %0"		\
			      : : "rK" (__v)			\
			      : "memory");			\
})

#define csr_read_clear(csr, val)				\
({								\
	unsigned long __v = (unsigned long)(val);		\
	__asm__ __volatile__ ("csrrc %0, " #csr ", %1"		\
			      : "=r" (__v) : "rK" (__v)		\
			      : "memory");			\
	__v;							\
})

#define csr_clear(csr, val)					\
({								\
	unsigned long __v = (unsigned long)(val);		\
	__asm__ __volatile__ ("csrc " #csr ", %0"		\
			      : : "rK" (__v)			\
			      : "memory");			\
})

#endif /* __ASSEMBLY__ */

#endif /* _ASM_RISCV_CSR_H */
