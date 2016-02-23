/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * KVM/MIPS: commpage: mapped into get kernel space
 *
 * Copyright (C) 2012  MIPS Technologies, Inc.  All rights reserved.
 * Authors: Sanjay Lal <sanjayl@kymasys.com>
 */

#ifndef __KVM_MIPS_COMMPAGE_H__
#define __KVM_MIPS_COMMPAGE_H__

struct kvm_mips_commpage {
	/* COP0 state is mapped into Guest kernel via commpage */
	struct mips_coproc cop0;
};

#define KVM_MIPS_COMM_EIDI_OFFSET       0x0

extern void kvm_mips_commpage_init(struct kvm_vcpu *vcpu);

#endif /* __KVM_MIPS_COMMPAGE_H__ */
