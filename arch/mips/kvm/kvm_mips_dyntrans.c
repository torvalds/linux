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
#include <linux/kvm_host.h>
#include <linux/module.h>
#include <linux/vmalloc.h>
#include <linux/fs.h>
#include <linux/bootmem.h>

#include "kvm_mips_comm.h"

#define SYNCI_TEMPLATE  0x041f0000
#define SYNCI_BASE(x)   (((x) >> 21) & 0x1f)
#define SYNCI_OFFSET    ((x) & 0xffff)

#define LW_TEMPLATE     0x8c000000
#define CLEAR_TEMPLATE  0x00000020
#define SW_TEMPLATE     0xac000000

int
kvm_mips_trans_cache_index(uint32_t inst, uint32_t *opc,
			   struct kvm_vcpu *vcpu)
{
	int result = 0;
	unsigned long kseg0_opc;
	uint32_t synci_inst = 0x0;

	/* Replace the CACHE instruction, with a NOP */
	kseg0_opc =
	    CKSEG0ADDR(kvm_mips_translate_guest_kseg0_to_hpa
		       (vcpu, (unsigned long) opc));
	memcpy((void *)kseg0_opc, (void *)&synci_inst, sizeof(uint32_t));
	mips32_SyncICache(kseg0_opc, 32);

	return result;
}

/*
 *  Address based CACHE instructions are transformed into synci(s). A little heavy
 * for just D-cache invalidates, but avoids an expensive trap
 */
int
kvm_mips_trans_cache_va(uint32_t inst, uint32_t *opc,
			struct kvm_vcpu *vcpu)
{
	int result = 0;
	unsigned long kseg0_opc;
	uint32_t synci_inst = SYNCI_TEMPLATE, base, offset;

	base = (inst >> 21) & 0x1f;
	offset = inst & 0xffff;
	synci_inst |= (base << 21);
	synci_inst |= offset;

	kseg0_opc =
	    CKSEG0ADDR(kvm_mips_translate_guest_kseg0_to_hpa
		       (vcpu, (unsigned long) opc));
	memcpy((void *)kseg0_opc, (void *)&synci_inst, sizeof(uint32_t));
	mips32_SyncICache(kseg0_opc, 32);

	return result;
}

int
kvm_mips_trans_mfc0(uint32_t inst, uint32_t *opc, struct kvm_vcpu *vcpu)
{
	int32_t rt, rd, sel;
	uint32_t mfc0_inst;
	unsigned long kseg0_opc, flags;

	rt = (inst >> 16) & 0x1f;
	rd = (inst >> 11) & 0x1f;
	sel = inst & 0x7;

	if ((rd == MIPS_CP0_ERRCTL) && (sel == 0)) {
		mfc0_inst = CLEAR_TEMPLATE;
		mfc0_inst |= ((rt & 0x1f) << 16);
	} else {
		mfc0_inst = LW_TEMPLATE;
		mfc0_inst |= ((rt & 0x1f) << 16);
		mfc0_inst |=
		    offsetof(struct mips_coproc,
			     reg[rd][sel]) + offsetof(struct kvm_mips_commpage,
						      cop0);
	}

	if (KVM_GUEST_KSEGX(opc) == KVM_GUEST_KSEG0) {
		kseg0_opc =
		    CKSEG0ADDR(kvm_mips_translate_guest_kseg0_to_hpa
			       (vcpu, (unsigned long) opc));
		memcpy((void *)kseg0_opc, (void *)&mfc0_inst, sizeof(uint32_t));
		mips32_SyncICache(kseg0_opc, 32);
	} else if (KVM_GUEST_KSEGX((unsigned long) opc) == KVM_GUEST_KSEG23) {
		local_irq_save(flags);
		memcpy((void *)opc, (void *)&mfc0_inst, sizeof(uint32_t));
		mips32_SyncICache((unsigned long) opc, 32);
		local_irq_restore(flags);
	} else {
		kvm_err("%s: Invalid address: %p\n", __func__, opc);
		return -EFAULT;
	}

	return 0;
}

int
kvm_mips_trans_mtc0(uint32_t inst, uint32_t *opc, struct kvm_vcpu *vcpu)
{
	int32_t rt, rd, sel;
	uint32_t mtc0_inst = SW_TEMPLATE;
	unsigned long kseg0_opc, flags;

	rt = (inst >> 16) & 0x1f;
	rd = (inst >> 11) & 0x1f;
	sel = inst & 0x7;

	mtc0_inst |= ((rt & 0x1f) << 16);
	mtc0_inst |=
	    offsetof(struct mips_coproc,
		     reg[rd][sel]) + offsetof(struct kvm_mips_commpage, cop0);

	if (KVM_GUEST_KSEGX(opc) == KVM_GUEST_KSEG0) {
		kseg0_opc =
		    CKSEG0ADDR(kvm_mips_translate_guest_kseg0_to_hpa
			       (vcpu, (unsigned long) opc));
		memcpy((void *)kseg0_opc, (void *)&mtc0_inst, sizeof(uint32_t));
		mips32_SyncICache(kseg0_opc, 32);
	} else if (KVM_GUEST_KSEGX((unsigned long) opc) == KVM_GUEST_KSEG23) {
		local_irq_save(flags);
		memcpy((void *)opc, (void *)&mtc0_inst, sizeof(uint32_t));
		mips32_SyncICache((unsigned long) opc, 32);
		local_irq_restore(flags);
	} else {
		kvm_err("%s: Invalid address: %p\n", __func__, opc);
		return -EFAULT;
	}

	return 0;
}
