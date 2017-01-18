/*
 * Based on arch/arm/include/asm/ptrace.h
 *
 * Copyright (C) 1996-2003 Russell King
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
#ifndef _UAPI__ASM_PTRACE_H
#define _UAPI__ASM_PTRACE_H

#include <linux/types.h>

#include <asm/hwcap.h>


/*
 * PSR bits
 */
#define PSR_MODE_EL0t	0x00000000
#define PSR_MODE_EL1t	0x00000004
#define PSR_MODE_EL1h	0x00000005
#define PSR_MODE_EL2t	0x00000008
#define PSR_MODE_EL2h	0x00000009
#define PSR_MODE_EL3t	0x0000000c
#define PSR_MODE_EL3h	0x0000000d
#define PSR_MODE_MASK	0x0000000f

/* AArch32 CPSR bits */
#define PSR_MODE32_BIT		0x00000010

/* AArch64 SPSR bits */
#define PSR_F_BIT	0x00000040
#define PSR_I_BIT	0x00000080
#define PSR_A_BIT	0x00000100
#define PSR_D_BIT	0x00000200
#define PSR_PAN_BIT	0x00400000
#define PSR_Q_BIT	0x08000000
#define PSR_V_BIT	0x10000000
#define PSR_C_BIT	0x20000000
#define PSR_Z_BIT	0x40000000
#define PSR_N_BIT	0x80000000

/*
 * Groups of PSR bits
 */
#define PSR_f		0xff000000	/* Flags		*/
#define PSR_s		0x00ff0000	/* Status		*/
#define PSR_x		0x0000ff00	/* Extension		*/
#define PSR_c		0x000000ff	/* Control		*/


#ifndef __ASSEMBLY__

/*
 * User structures for general purpose, floating point and debug registers.
 */
struct user_pt_regs {
	__u64		regs[31];
	__u64		sp;
	__u64		pc;
	__u64		pstate;
};

struct user_fpsimd_state {
	__uint128_t	vregs[32];
	__u32		fpsr;
	__u32		fpcr;
	__u32		__reserved[2];
};

struct user_hwdebug_state {
	__u32		dbg_info;
	__u32		pad;
	struct {
		__u64	addr;
		__u32	ctrl;
		__u32	pad;
	}		dbg_regs[16];
};

#endif /* __ASSEMBLY__ */

#endif /* _UAPI__ASM_PTRACE_H */
