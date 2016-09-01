/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * KVM/MIPS: Binary Patching for privileged instructions, reduces traps.
 *
 * Copyright (C) 2012  MIPS Technologies, Inc.  All rights reserved.
 * Authors: Sanjay Lal <sanjayl@kymasys.com>
 */

#include <linux/errno.h>
#include <linux/err.h>
#include <linux/highmem.h>
#include <linux/kvm_host.h>
#include <linux/module.h>
#include <linux/vmalloc.h>
#include <linux/fs.h>
#include <linux/bootmem.h>
#include <asm/cacheflush.h>

#include "commpage.h"

/**
 * kvm_mips_trans_replace() - Replace trapping instruction in guest memory.
 * @vcpu:	Virtual CPU.
 * @opc:	PC of instruction to replace.
 * @replace:	Instruction to write
 */
static int kvm_mips_trans_replace(struct kvm_vcpu *vcpu, u32 *opc,
				  union mips_instruction replace)
{
	unsigned long paddr, flags;
	void *vaddr;

	if (KVM_GUEST_KSEGX((unsigned long)opc) == KVM_GUEST_KSEG0) {
		paddr = kvm_mips_translate_guest_kseg0_to_hpa(vcpu,
							    (unsigned long)opc);
		vaddr = kmap_atomic(pfn_to_page(PHYS_PFN(paddr)));
		vaddr += paddr & ~PAGE_MASK;
		memcpy(vaddr, (void *)&replace, sizeof(u32));
		local_flush_icache_range((unsigned long)vaddr,
					 (unsigned long)vaddr + 32);
		kunmap_atomic(vaddr);
	} else if (KVM_GUEST_KSEGX((unsigned long) opc) == KVM_GUEST_KSEG23) {
		local_irq_save(flags);
		memcpy((void *)opc, (void *)&replace, sizeof(u32));
		__local_flush_icache_user_range((unsigned long)opc,
						(unsigned long)opc + 32);
		local_irq_restore(flags);
	} else {
		kvm_err("%s: Invalid address: %p\n", __func__, opc);
		return -EFAULT;
	}

	return 0;
}

int kvm_mips_trans_cache_index(union mips_instruction inst, u32 *opc,
			       struct kvm_vcpu *vcpu)
{
	union mips_instruction nop_inst = { 0 };

	/* Replace the CACHE instruction, with a NOP */
	return kvm_mips_trans_replace(vcpu, opc, nop_inst);
}

/*
 * Address based CACHE instructions are transformed into synci(s). A little
 * heavy for just D-cache invalidates, but avoids an expensive trap
 */
int kvm_mips_trans_cache_va(union mips_instruction inst, u32 *opc,
			    struct kvm_vcpu *vcpu)
{
	union mips_instruction synci_inst = { 0 };

	synci_inst.i_format.opcode = bcond_op;
	synci_inst.i_format.rs = inst.i_format.rs;
	synci_inst.i_format.rt = synci_op;
	if (cpu_has_mips_r6)
		synci_inst.i_format.simmediate = inst.spec3_format.simmediate;
	else
		synci_inst.i_format.simmediate = inst.i_format.simmediate;

	return kvm_mips_trans_replace(vcpu, opc, synci_inst);
}

int kvm_mips_trans_mfc0(union mips_instruction inst, u32 *opc,
			struct kvm_vcpu *vcpu)
{
	union mips_instruction mfc0_inst = { 0 };
	u32 rd, sel;

	rd = inst.c0r_format.rd;
	sel = inst.c0r_format.sel;

	if (rd == MIPS_CP0_ERRCTL && sel == 0) {
		mfc0_inst.r_format.opcode = spec_op;
		mfc0_inst.r_format.rd = inst.c0r_format.rt;
		mfc0_inst.r_format.func = add_op;
	} else {
		mfc0_inst.i_format.opcode = lw_op;
		mfc0_inst.i_format.rt = inst.c0r_format.rt;
		mfc0_inst.i_format.simmediate = KVM_GUEST_COMMPAGE_ADDR |
			offsetof(struct kvm_mips_commpage, cop0.reg[rd][sel]);
#ifdef CONFIG_CPU_BIG_ENDIAN
		if (sizeof(vcpu->arch.cop0->reg[0][0]) == 8)
			mfc0_inst.i_format.simmediate |= 4;
#endif
	}

	return kvm_mips_trans_replace(vcpu, opc, mfc0_inst);
}

int kvm_mips_trans_mtc0(union mips_instruction inst, u32 *opc,
			struct kvm_vcpu *vcpu)
{
	union mips_instruction mtc0_inst = { 0 };
	u32 rd, sel;

	rd = inst.c0r_format.rd;
	sel = inst.c0r_format.sel;

	mtc0_inst.i_format.opcode = sw_op;
	mtc0_inst.i_format.rt = inst.c0r_format.rt;
	mtc0_inst.i_format.simmediate = KVM_GUEST_COMMPAGE_ADDR |
		offsetof(struct kvm_mips_commpage, cop0.reg[rd][sel]);
#ifdef CONFIG_CPU_BIG_ENDIAN
	if (sizeof(vcpu->arch.cop0->reg[0][0]) == 8)
		mtc0_inst.i_format.simmediate |= 4;
#endif

	return kvm_mips_trans_replace(vcpu, opc, mtc0_inst);
}
