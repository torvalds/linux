/*
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License, version 2, as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 *
 * Copyright SUSE Linux Products GmbH 2009
 *
 * Authors: Alexander Graf <agraf@suse.de>
 */

#ifndef __ASM_KVM_BOOK3S_ASM_H__
#define __ASM_KVM_BOOK3S_ASM_H__

#ifdef __ASSEMBLY__

#ifdef CONFIG_KVM_BOOK3S_HANDLER

#include <asm/kvm_asm.h>

.macro DO_KVM intno
	.if (\intno == BOOK3S_INTERRUPT_SYSTEM_RESET) || \
	    (\intno == BOOK3S_INTERRUPT_MACHINE_CHECK) || \
	    (\intno == BOOK3S_INTERRUPT_DATA_STORAGE) || \
	    (\intno == BOOK3S_INTERRUPT_INST_STORAGE) || \
	    (\intno == BOOK3S_INTERRUPT_DATA_SEGMENT) || \
	    (\intno == BOOK3S_INTERRUPT_INST_SEGMENT) || \
	    (\intno == BOOK3S_INTERRUPT_EXTERNAL) || \
	    (\intno == BOOK3S_INTERRUPT_EXTERNAL_HV) || \
	    (\intno == BOOK3S_INTERRUPT_ALIGNMENT) || \
	    (\intno == BOOK3S_INTERRUPT_PROGRAM) || \
	    (\intno == BOOK3S_INTERRUPT_FP_UNAVAIL) || \
	    (\intno == BOOK3S_INTERRUPT_DECREMENTER) || \
	    (\intno == BOOK3S_INTERRUPT_SYSCALL) || \
	    (\intno == BOOK3S_INTERRUPT_TRACE) || \
	    (\intno == BOOK3S_INTERRUPT_PERFMON) || \
	    (\intno == BOOK3S_INTERRUPT_ALTIVEC) || \
	    (\intno == BOOK3S_INTERRUPT_VSX)

	b	kvmppc_trampoline_\intno
kvmppc_resume_\intno:

	.endif
.endm

#else

.macro DO_KVM intno
.endm

#endif /* CONFIG_KVM_BOOK3S_HANDLER */

#else  /*__ASSEMBLY__ */

struct kvmppc_book3s_shadow_vcpu {
	ulong gpr[14];
	u32 cr;
	u32 xer;

	u32 fault_dsisr;
	u32 last_inst;
	ulong ctr;
	ulong lr;
	ulong pc;
	ulong shadow_srr1;
	ulong fault_dar;

	ulong host_r1;
	ulong host_r2;
	ulong handler;
	ulong scratch0;
	ulong scratch1;
	ulong vmhandler;
	u8 in_guest;

#ifdef CONFIG_PPC_BOOK3S_32
	u32     sr[16];			/* Guest SRs */
#endif
#ifdef CONFIG_PPC_BOOK3S_64
	u8 slb_max;			/* highest used guest slb entry */
	struct  {
		u64     esid;
		u64     vsid;
	} slb[64];			/* guest SLB */
#endif
};

#endif /*__ASSEMBLY__ */

#endif /* __ASM_KVM_BOOK3S_ASM_H__ */
