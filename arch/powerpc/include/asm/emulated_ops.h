/*
 *  Copyright 2007 Sony Corporation
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; version 2 of the License.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program.
 *  If not, see <http://www.gnu.org/licenses/>.
 */

#ifndef _ASM_POWERPC_EMULATED_OPS_H
#define _ASM_POWERPC_EMULATED_OPS_H

#include <asm/atomic.h>


#ifdef CONFIG_PPC_EMULATED_STATS

struct ppc_emulated_entry {
	const char *name;
	atomic_t val;
};

extern struct ppc_emulated {
#ifdef CONFIG_ALTIVEC
	struct ppc_emulated_entry altivec;
#endif
	struct ppc_emulated_entry dcba;
	struct ppc_emulated_entry dcbz;
	struct ppc_emulated_entry fp_pair;
	struct ppc_emulated_entry isel;
	struct ppc_emulated_entry mcrxr;
	struct ppc_emulated_entry mfpvr;
	struct ppc_emulated_entry multiple;
	struct ppc_emulated_entry popcntb;
	struct ppc_emulated_entry spe;
	struct ppc_emulated_entry string;
	struct ppc_emulated_entry unaligned;
#ifdef CONFIG_MATH_EMULATION
	struct ppc_emulated_entry math;
#elif defined(CONFIG_8XX_MINIMAL_FPEMU)
	struct ppc_emulated_entry 8xx;
#endif
#ifdef CONFIG_VSX
	struct ppc_emulated_entry vsx;
#endif
} ppc_emulated;

extern u32 ppc_warn_emulated;

extern void ppc_warn_emulated_print(const char *type);

#define __PPC_WARN_EMULATED(type)					 \
	do {								 \
		atomic_inc(&ppc_emulated.type.val);			 \
		if (ppc_warn_emulated)					 \
			ppc_warn_emulated_print(ppc_emulated.type.name); \
	} while (0)

#else /* !CONFIG_PPC_EMULATED_STATS */

#define __PPC_WARN_EMULATED(type)	do { } while (0)

#endif /* !CONFIG_PPC_EMULATED_STATS */

#define PPC_WARN_EMULATED(type, regs)	__PPC_WARN_EMULATED(type)
#define PPC_WARN_ALIGNMENT(type, regs)	__PPC_WARN_EMULATED(type)

#endif /* _ASM_POWERPC_EMULATED_OPS_H */
