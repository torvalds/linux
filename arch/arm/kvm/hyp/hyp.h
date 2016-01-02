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

#define VTTBR		__ACCESS_CP15_64(6, c2)
#define ICIALLUIS	__ACCESS_CP15(c7, 0, c1, 0)
#define TLBIALLIS	__ACCESS_CP15(c8, 0, c3, 0)
#define TLBIALLNSNHIS	__ACCESS_CP15(c8, 4, c3, 4)

#endif /* __ARM_KVM_HYP_H__ */
