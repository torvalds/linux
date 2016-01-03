/*
 * Copyright (C) 2015 - ARM Ltd
 * Author: Marc Zyngier <marc.zyngier@arm.com>
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

#ifndef __ARM_KVM_HYP_H__
#define __ARM_KVM_HYP_H__

#include <linux/compiler.h>
#include <linux/kvm_host.h>
#include <asm/kvm_mmu.h>

#define __hyp_text __section(.hyp.text) notrace

#define kern_hyp_va(v) (v)
#define hyp_kern_va(v) (v)

#define __ACCESS_CP15(CRn, Op1, CRm, Op2)	\
	"mrc", "mcr", __stringify(p15, Op1, %0, CRn, CRm, Op2), u32
#define __ACCESS_CP15_64(Op1, CRm)		\
	"mrrc", "mcrr", __stringify(p15, Op1, %Q0, %R0, CRm), u64

#define __write_sysreg(v, r, w, c, t)	asm volatile(w " " c : : "r" ((t)(v)))
#define write_sysreg(v, ...)		__write_sysreg(v, __VA_ARGS__)

#define __read_sysreg(r, w, c, t) ({				\
	t __val;						\
	asm volatile(r " " c : "=r" (__val));			\
	__val;							\
})
#define read_sysreg(...)		__read_sysreg(__VA_ARGS__)

#define TTBR0		__ACCESS_CP15_64(0, c2)
#define TTBR1		__ACCESS_CP15_64(1, c2)
#define VTTBR		__ACCESS_CP15_64(6, c2)
#define PAR		__ACCESS_CP15_64(0, c7)
#define CSSELR		__ACCESS_CP15(c0, 2, c0, 0)
#define VMPIDR		__ACCESS_CP15(c0, 4, c0, 5)
#define SCTLR		__ACCESS_CP15(c1, 0, c0, 0)
#define CPACR		__ACCESS_CP15(c1, 0, c0, 2)
#define TTBCR		__ACCESS_CP15(c2, 0, c0, 2)
#define DACR		__ACCESS_CP15(c3, 0, c0, 0)
#define DFSR		__ACCESS_CP15(c5, 0, c0, 0)
#define IFSR		__ACCESS_CP15(c5, 0, c0, 1)
#define ADFSR		__ACCESS_CP15(c5, 0, c1, 0)
#define AIFSR		__ACCESS_CP15(c5, 0, c1, 1)
#define DFAR		__ACCESS_CP15(c6, 0, c0, 0)
#define IFAR		__ACCESS_CP15(c6, 0, c0, 2)
#define ICIALLUIS	__ACCESS_CP15(c7, 0, c1, 0)
#define TLBIALLIS	__ACCESS_CP15(c8, 0, c3, 0)
#define TLBIALLNSNHIS	__ACCESS_CP15(c8, 4, c3, 4)
#define PRRR		__ACCESS_CP15(c10, 0, c2, 0)
#define NMRR		__ACCESS_CP15(c10, 0, c2, 1)
#define AMAIR0		__ACCESS_CP15(c10, 0, c3, 0)
#define AMAIR1		__ACCESS_CP15(c10, 0, c3, 1)
#define VBAR		__ACCESS_CP15(c12, 0, c0, 0)
#define CID		__ACCESS_CP15(c13, 0, c0, 1)
#define TID_URW		__ACCESS_CP15(c13, 0, c0, 2)
#define TID_URO		__ACCESS_CP15(c13, 0, c0, 3)
#define TID_PRIV	__ACCESS_CP15(c13, 0, c0, 4)
#define CNTKCTL		__ACCESS_CP15(c14, 0, c1, 0)

void __sysreg_save_state(struct kvm_cpu_context *ctxt);
void __sysreg_restore_state(struct kvm_cpu_context *ctxt);

#endif /* __ARM_KVM_HYP_H__ */
