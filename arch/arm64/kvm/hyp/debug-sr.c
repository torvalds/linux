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

#include <linux/compiler.h>
#include <linux/kvm_host.h>

#include <asm/debug-monitors.h>
#include <asm/kvm_asm.h>
#include <asm/kvm_hyp.h>

#define read_debug(r,n)		read_sysreg(r##n##_el1)
#define write_debug(v,r,n)	write_sysreg(v, r##n##_el1)

#define save_debug(ptr,reg,nr)						\
	switch (nr) {							\
	case 15:	ptr[15] = read_debug(reg, 15);			\
	case 14:	ptr[14] = read_debug(reg, 14);			\
	case 13:	ptr[13] = read_debug(reg, 13);			\
	case 12:	ptr[12] = read_debug(reg, 12);			\
	case 11:	ptr[11] = read_debug(reg, 11);			\
	case 10:	ptr[10] = read_debug(reg, 10);			\
	case 9:		ptr[9] = read_debug(reg, 9);			\
	case 8:		ptr[8] = read_debug(reg, 8);			\
	case 7:		ptr[7] = read_debug(reg, 7);			\
	case 6:		ptr[6] = read_debug(reg, 6);			\
	case 5:		ptr[5] = read_debug(reg, 5);			\
	case 4:		ptr[4] = read_debug(reg, 4);			\
	case 3:		ptr[3] = read_debug(reg, 3);			\
	case 2:		ptr[2] = read_debug(reg, 2);			\
	case 1:		ptr[1] = read_debug(reg, 1);			\
	default:	ptr[0] = read_debug(reg, 0);			\
	}

#define restore_debug(ptr,reg,nr)					\
	switch (nr) {							\
	case 15:	write_debug(ptr[15], reg, 15);			\
	case 14:	write_debug(ptr[14], reg, 14);			\
	case 13:	write_debug(ptr[13], reg, 13);			\
	case 12:	write_debug(ptr[12], reg, 12);			\
	case 11:	write_debug(ptr[11], reg, 11);			\
	case 10:	write_debug(ptr[10], reg, 10);			\
	case 9:		write_debug(ptr[9], reg, 9);			\
	case 8:		write_debug(ptr[8], reg, 8);			\
	case 7:		write_debug(ptr[7], reg, 7);			\
	case 6:		write_debug(ptr[6], reg, 6);			\
	case 5:		write_debug(ptr[5], reg, 5);			\
	case 4:		write_debug(ptr[4], reg, 4);			\
	case 3:		write_debug(ptr[3], reg, 3);			\
	case 2:		write_debug(ptr[2], reg, 2);			\
	case 1:		write_debug(ptr[1], reg, 1);			\
	default:	write_debug(ptr[0], reg, 0);			\
	}

void __hyp_text __debug_save_state(struct kvm_vcpu *vcpu,
				   struct kvm_guest_debug_arch *dbg,
				   struct kvm_cpu_context *ctxt)
{
	u64 aa64dfr0;
	int brps, wrps;

	if (!(vcpu->arch.debug_flags & KVM_ARM64_DEBUG_DIRTY))
		return;

	aa64dfr0 = read_sysreg(id_aa64dfr0_el1);
	brps = (aa64dfr0 >> 12) & 0xf;
	wrps = (aa64dfr0 >> 20) & 0xf;

	save_debug(dbg->dbg_bcr, dbgbcr, brps);
	save_debug(dbg->dbg_bvr, dbgbvr, brps);
	save_debug(dbg->dbg_wcr, dbgwcr, wrps);
	save_debug(dbg->dbg_wvr, dbgwvr, wrps);

	ctxt->sys_regs[MDCCINT_EL1] = read_sysreg(mdccint_el1);
}

void __hyp_text __debug_restore_state(struct kvm_vcpu *vcpu,
				      struct kvm_guest_debug_arch *dbg,
				      struct kvm_cpu_context *ctxt)
{
	u64 aa64dfr0;
	int brps, wrps;

	if (!(vcpu->arch.debug_flags & KVM_ARM64_DEBUG_DIRTY))
		return;

	aa64dfr0 = read_sysreg(id_aa64dfr0_el1);

	brps = (aa64dfr0 >> 12) & 0xf;
	wrps = (aa64dfr0 >> 20) & 0xf;

	restore_debug(dbg->dbg_bcr, dbgbcr, brps);
	restore_debug(dbg->dbg_bvr, dbgbvr, brps);
	restore_debug(dbg->dbg_wcr, dbgwcr, wrps);
	restore_debug(dbg->dbg_wvr, dbgwvr, wrps);

	write_sysreg(ctxt->sys_regs[MDCCINT_EL1], mdccint_el1);
}

void __hyp_text __debug_cond_save_host_state(struct kvm_vcpu *vcpu)
{
	/* If any of KDE, MDE or KVM_ARM64_DEBUG_DIRTY is set, perform
	 * a full save/restore cycle. */
	if ((vcpu->arch.ctxt.sys_regs[MDSCR_EL1] & DBG_MDSCR_KDE) ||
	    (vcpu->arch.ctxt.sys_regs[MDSCR_EL1] & DBG_MDSCR_MDE))
		vcpu->arch.debug_flags |= KVM_ARM64_DEBUG_DIRTY;

	__debug_save_state(vcpu, &vcpu->arch.host_debug_state,
			   kern_hyp_va(vcpu->arch.host_cpu_context));
}

void __hyp_text __debug_cond_restore_host_state(struct kvm_vcpu *vcpu)
{
	__debug_restore_state(vcpu, &vcpu->arch.host_debug_state,
			      kern_hyp_va(vcpu->arch.host_cpu_context));

	if (vcpu->arch.debug_flags & KVM_ARM64_DEBUG_DIRTY)
		vcpu->arch.debug_flags &= ~KVM_ARM64_DEBUG_DIRTY;
}

u32 __hyp_text __kvm_get_mdcr_el2(void)
{
	return read_sysreg(mdcr_el2);
}
