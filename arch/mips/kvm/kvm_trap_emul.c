/*
* This file is subject to the terms and conditions of the GNU General Public
* License.  See the file "COPYING" in the main directory of this archive
* for more details.
*
* KVM/MIPS: Deliver/Emulate exceptions to the guest kernel
*
* Copyright (C) 2012  MIPS Technologies, Inc.  All rights reserved.
* Authors: Sanjay Lal <sanjayl@kymasys.com>
*/

#include <linux/errno.h>
#include <linux/err.h>
#include <linux/module.h>
#include <linux/vmalloc.h>

#include <linux/kvm_host.h>

#include "kvm_mips_opcode.h"
#include "kvm_mips_int.h"

static gpa_t kvm_trap_emul_gva_to_gpa_cb(gva_t gva)
{
	gpa_t gpa;
	uint32_t kseg = KSEGX(gva);

	if ((kseg == CKSEG0) || (kseg == CKSEG1))
		gpa = CPHYSADDR(gva);
	else {
		printk("%s: cannot find GPA for GVA: %#lx\n", __func__, gva);
		kvm_mips_dump_host_tlbs();
		gpa = KVM_INVALID_ADDR;
	}

#ifdef DEBUG
	kvm_debug("%s: gva %#lx, gpa: %#llx\n", __func__, gva, gpa);
#endif

	return gpa;
}


static int kvm_trap_emul_handle_cop_unusable(struct kvm_vcpu *vcpu)
{
	struct kvm_run *run = vcpu->run;
	uint32_t __user *opc = (uint32_t __user *) vcpu->arch.pc;
	unsigned long cause = vcpu->arch.host_cp0_cause;
	enum emulation_result er = EMULATE_DONE;
	int ret = RESUME_GUEST;

	if (((cause & CAUSEF_CE) >> CAUSEB_CE) == 1) {
		er = kvm_mips_emulate_fpu_exc(cause, opc, run, vcpu);
	} else
		er = kvm_mips_emulate_inst(cause, opc, run, vcpu);

	switch (er) {
	case EMULATE_DONE:
		ret = RESUME_GUEST;
		break;

	case EMULATE_FAIL:
		run->exit_reason = KVM_EXIT_INTERNAL_ERROR;
		ret = RESUME_HOST;
		break;

	case EMULATE_WAIT:
		run->exit_reason = KVM_EXIT_INTR;
		ret = RESUME_HOST;
		break;

	default:
		BUG();
	}
	return ret;
}

static int kvm_trap_emul_handle_tlb_mod(struct kvm_vcpu *vcpu)
{
	struct kvm_run *run = vcpu->run;
	uint32_t __user *opc = (uint32_t __user *) vcpu->arch.pc;
	unsigned long badvaddr = vcpu->arch.host_cp0_badvaddr;
	unsigned long cause = vcpu->arch.host_cp0_cause;
	enum emulation_result er = EMULATE_DONE;
	int ret = RESUME_GUEST;

	if (KVM_GUEST_KSEGX(badvaddr) < KVM_GUEST_KSEG0
	    || KVM_GUEST_KSEGX(badvaddr) == KVM_GUEST_KSEG23) {
#ifdef DEBUG
		kvm_debug
		    ("USER/KSEG23 ADDR TLB MOD fault: cause %#lx, PC: %p, BadVaddr: %#lx\n",
		     cause, opc, badvaddr);
#endif
		er = kvm_mips_handle_tlbmod(cause, opc, run, vcpu);

		if (er == EMULATE_DONE)
			ret = RESUME_GUEST;
		else {
			run->exit_reason = KVM_EXIT_INTERNAL_ERROR;
			ret = RESUME_HOST;
		}
	} else if (KVM_GUEST_KSEGX(badvaddr) == KVM_GUEST_KSEG0) {
		/* XXXKYMA: The guest kernel does not expect to get this fault when we are not
		 * using HIGHMEM. Need to address this in a HIGHMEM kernel
		 */
		printk
		    ("TLB MOD fault not handled, cause %#lx, PC: %p, BadVaddr: %#lx\n",
		     cause, opc, badvaddr);
		kvm_mips_dump_host_tlbs();
		kvm_arch_vcpu_dump_regs(vcpu);
		run->exit_reason = KVM_EXIT_INTERNAL_ERROR;
		ret = RESUME_HOST;
	} else {
		printk
		    ("Illegal TLB Mod fault address , cause %#lx, PC: %p, BadVaddr: %#lx\n",
		     cause, opc, badvaddr);
		kvm_mips_dump_host_tlbs();
		kvm_arch_vcpu_dump_regs(vcpu);
		run->exit_reason = KVM_EXIT_INTERNAL_ERROR;
		ret = RESUME_HOST;
	}
	return ret;
}

static int kvm_trap_emul_handle_tlb_st_miss(struct kvm_vcpu *vcpu)
{
	struct kvm_run *run = vcpu->run;
	uint32_t __user *opc = (uint32_t __user *) vcpu->arch.pc;
	unsigned long badvaddr = vcpu->arch.host_cp0_badvaddr;
	unsigned long cause = vcpu->arch.host_cp0_cause;
	enum emulation_result er = EMULATE_DONE;
	int ret = RESUME_GUEST;

	if (((badvaddr & PAGE_MASK) == KVM_GUEST_COMMPAGE_ADDR)
	    && KVM_GUEST_KERNEL_MODE(vcpu)) {
		if (kvm_mips_handle_commpage_tlb_fault(badvaddr, vcpu) < 0) {
			run->exit_reason = KVM_EXIT_INTERNAL_ERROR;
			ret = RESUME_HOST;
		}
	} else if (KVM_GUEST_KSEGX(badvaddr) < KVM_GUEST_KSEG0
		   || KVM_GUEST_KSEGX(badvaddr) == KVM_GUEST_KSEG23) {
#ifdef DEBUG
		kvm_debug
		    ("USER ADDR TLB LD fault: cause %#lx, PC: %p, BadVaddr: %#lx\n",
		     cause, opc, badvaddr);
#endif
		er = kvm_mips_handle_tlbmiss(cause, opc, run, vcpu);
		if (er == EMULATE_DONE)
			ret = RESUME_GUEST;
		else {
			run->exit_reason = KVM_EXIT_INTERNAL_ERROR;
			ret = RESUME_HOST;
		}
	} else if (KVM_GUEST_KSEGX(badvaddr) == KVM_GUEST_KSEG0) {
		/* All KSEG0 faults are handled by KVM, as the guest kernel does not
		 * expect to ever get them
		 */
		if (kvm_mips_handle_kseg0_tlb_fault
		    (vcpu->arch.host_cp0_badvaddr, vcpu) < 0) {
			run->exit_reason = KVM_EXIT_INTERNAL_ERROR;
			ret = RESUME_HOST;
		}
	} else {
		kvm_err
		    ("Illegal TLB LD fault address , cause %#lx, PC: %p, BadVaddr: %#lx\n",
		     cause, opc, badvaddr);
		kvm_mips_dump_host_tlbs();
		kvm_arch_vcpu_dump_regs(vcpu);
		run->exit_reason = KVM_EXIT_INTERNAL_ERROR;
		ret = RESUME_HOST;
	}
	return ret;
}

static int kvm_trap_emul_handle_tlb_ld_miss(struct kvm_vcpu *vcpu)
{
	struct kvm_run *run = vcpu->run;
	uint32_t __user *opc = (uint32_t __user *) vcpu->arch.pc;
	unsigned long badvaddr = vcpu->arch.host_cp0_badvaddr;
	unsigned long cause = vcpu->arch.host_cp0_cause;
	enum emulation_result er = EMULATE_DONE;
	int ret = RESUME_GUEST;

	if (((badvaddr & PAGE_MASK) == KVM_GUEST_COMMPAGE_ADDR)
	    && KVM_GUEST_KERNEL_MODE(vcpu)) {
		if (kvm_mips_handle_commpage_tlb_fault(badvaddr, vcpu) < 0) {
			run->exit_reason = KVM_EXIT_INTERNAL_ERROR;
			ret = RESUME_HOST;
		}
	} else if (KVM_GUEST_KSEGX(badvaddr) < KVM_GUEST_KSEG0
		   || KVM_GUEST_KSEGX(badvaddr) == KVM_GUEST_KSEG23) {
#ifdef DEBUG
		kvm_debug("USER ADDR TLB ST fault: PC: %#lx, BadVaddr: %#lx\n",
			  vcpu->arch.pc, badvaddr);
#endif

		/* User Address (UA) fault, this could happen if
		 * (1) TLB entry not present/valid in both Guest and shadow host TLBs, in this
		 *     case we pass on the fault to the guest kernel and let it handle it.
		 * (2) TLB entry is present in the Guest TLB but not in the shadow, in this
		 *     case we inject the TLB from the Guest TLB into the shadow host TLB
		 */

		er = kvm_mips_handle_tlbmiss(cause, opc, run, vcpu);
		if (er == EMULATE_DONE)
			ret = RESUME_GUEST;
		else {
			run->exit_reason = KVM_EXIT_INTERNAL_ERROR;
			ret = RESUME_HOST;
		}
	} else if (KVM_GUEST_KSEGX(badvaddr) == KVM_GUEST_KSEG0) {
		if (kvm_mips_handle_kseg0_tlb_fault
		    (vcpu->arch.host_cp0_badvaddr, vcpu) < 0) {
			run->exit_reason = KVM_EXIT_INTERNAL_ERROR;
			ret = RESUME_HOST;
		}
	} else {
		printk
		    ("Illegal TLB ST fault address , cause %#lx, PC: %p, BadVaddr: %#lx\n",
		     cause, opc, badvaddr);
		kvm_mips_dump_host_tlbs();
		kvm_arch_vcpu_dump_regs(vcpu);
		run->exit_reason = KVM_EXIT_INTERNAL_ERROR;
		ret = RESUME_HOST;
	}
	return ret;
}

static int kvm_trap_emul_handle_addr_err_st(struct kvm_vcpu *vcpu)
{
	struct kvm_run *run = vcpu->run;
	uint32_t __user *opc = (uint32_t __user *) vcpu->arch.pc;
	unsigned long badvaddr = vcpu->arch.host_cp0_badvaddr;
	unsigned long cause = vcpu->arch.host_cp0_cause;
	enum emulation_result er = EMULATE_DONE;
	int ret = RESUME_GUEST;

	if (KVM_GUEST_KERNEL_MODE(vcpu)
	    && (KSEGX(badvaddr) == CKSEG0 || KSEGX(badvaddr) == CKSEG1)) {
#ifdef DEBUG
		kvm_debug("Emulate Store to MMIO space\n");
#endif
		er = kvm_mips_emulate_inst(cause, opc, run, vcpu);
		if (er == EMULATE_FAIL) {
			printk("Emulate Store to MMIO space failed\n");
			run->exit_reason = KVM_EXIT_INTERNAL_ERROR;
			ret = RESUME_HOST;
		} else {
			run->exit_reason = KVM_EXIT_MMIO;
			ret = RESUME_HOST;
		}
	} else {
		printk
		    ("Address Error (STORE): cause %#lx, PC: %p, BadVaddr: %#lx\n",
		     cause, opc, badvaddr);
		run->exit_reason = KVM_EXIT_INTERNAL_ERROR;
		ret = RESUME_HOST;
	}
	return ret;
}

static int kvm_trap_emul_handle_addr_err_ld(struct kvm_vcpu *vcpu)
{
	struct kvm_run *run = vcpu->run;
	uint32_t __user *opc = (uint32_t __user *) vcpu->arch.pc;
	unsigned long badvaddr = vcpu->arch.host_cp0_badvaddr;
	unsigned long cause = vcpu->arch.host_cp0_cause;
	enum emulation_result er = EMULATE_DONE;
	int ret = RESUME_GUEST;

	if (KSEGX(badvaddr) == CKSEG0 || KSEGX(badvaddr) == CKSEG1) {
#ifdef DEBUG
		kvm_debug("Emulate Load from MMIO space @ %#lx\n", badvaddr);
#endif
		er = kvm_mips_emulate_inst(cause, opc, run, vcpu);
		if (er == EMULATE_FAIL) {
			printk("Emulate Load from MMIO space failed\n");
			run->exit_reason = KVM_EXIT_INTERNAL_ERROR;
			ret = RESUME_HOST;
		} else {
			run->exit_reason = KVM_EXIT_MMIO;
			ret = RESUME_HOST;
		}
	} else {
		printk
		    ("Address Error (LOAD): cause %#lx, PC: %p, BadVaddr: %#lx\n",
		     cause, opc, badvaddr);
		run->exit_reason = KVM_EXIT_INTERNAL_ERROR;
		ret = RESUME_HOST;
		er = EMULATE_FAIL;
	}
	return ret;
}

static int kvm_trap_emul_handle_syscall(struct kvm_vcpu *vcpu)
{
	struct kvm_run *run = vcpu->run;
	uint32_t __user *opc = (uint32_t __user *) vcpu->arch.pc;
	unsigned long cause = vcpu->arch.host_cp0_cause;
	enum emulation_result er = EMULATE_DONE;
	int ret = RESUME_GUEST;

	er = kvm_mips_emulate_syscall(cause, opc, run, vcpu);
	if (er == EMULATE_DONE)
		ret = RESUME_GUEST;
	else {
		run->exit_reason = KVM_EXIT_INTERNAL_ERROR;
		ret = RESUME_HOST;
	}
	return ret;
}

static int kvm_trap_emul_handle_res_inst(struct kvm_vcpu *vcpu)
{
	struct kvm_run *run = vcpu->run;
	uint32_t __user *opc = (uint32_t __user *) vcpu->arch.pc;
	unsigned long cause = vcpu->arch.host_cp0_cause;
	enum emulation_result er = EMULATE_DONE;
	int ret = RESUME_GUEST;

	er = kvm_mips_handle_ri(cause, opc, run, vcpu);
	if (er == EMULATE_DONE)
		ret = RESUME_GUEST;
	else {
		run->exit_reason = KVM_EXIT_INTERNAL_ERROR;
		ret = RESUME_HOST;
	}
	return ret;
}

static int kvm_trap_emul_handle_break(struct kvm_vcpu *vcpu)
{
	struct kvm_run *run = vcpu->run;
	uint32_t __user *opc = (uint32_t __user *) vcpu->arch.pc;
	unsigned long cause = vcpu->arch.host_cp0_cause;
	enum emulation_result er = EMULATE_DONE;
	int ret = RESUME_GUEST;

	er = kvm_mips_emulate_bp_exc(cause, opc, run, vcpu);
	if (er == EMULATE_DONE)
		ret = RESUME_GUEST;
	else {
		run->exit_reason = KVM_EXIT_INTERNAL_ERROR;
		ret = RESUME_HOST;
	}
	return ret;
}

static int
kvm_trap_emul_ioctl_set_regs(struct kvm_vcpu *vcpu, struct kvm_regs *regs)
{
	struct mips_coproc *cop0 = vcpu->arch.cop0;

	kvm_write_c0_guest_index(cop0, regs->cp0reg[MIPS_CP0_TLB_INDEX][0]);
	kvm_write_c0_guest_context(cop0, regs->cp0reg[MIPS_CP0_TLB_CONTEXT][0]);
	kvm_write_c0_guest_badvaddr(cop0, regs->cp0reg[MIPS_CP0_BAD_VADDR][0]);
	kvm_write_c0_guest_entryhi(cop0, regs->cp0reg[MIPS_CP0_TLB_HI][0]);
	kvm_write_c0_guest_epc(cop0, regs->cp0reg[MIPS_CP0_EXC_PC][0]);

	kvm_write_c0_guest_status(cop0, regs->cp0reg[MIPS_CP0_STATUS][0]);
	kvm_write_c0_guest_cause(cop0, regs->cp0reg[MIPS_CP0_CAUSE][0]);
	kvm_write_c0_guest_pagemask(cop0,
				    regs->cp0reg[MIPS_CP0_TLB_PG_MASK][0]);
	kvm_write_c0_guest_wired(cop0, regs->cp0reg[MIPS_CP0_TLB_WIRED][0]);
	kvm_write_c0_guest_errorepc(cop0, regs->cp0reg[MIPS_CP0_ERROR_PC][0]);

	return 0;
}

static int
kvm_trap_emul_ioctl_get_regs(struct kvm_vcpu *vcpu, struct kvm_regs *regs)
{
	struct mips_coproc *cop0 = vcpu->arch.cop0;

	regs->cp0reg[MIPS_CP0_TLB_INDEX][0] = kvm_read_c0_guest_index(cop0);
	regs->cp0reg[MIPS_CP0_TLB_CONTEXT][0] = kvm_read_c0_guest_context(cop0);
	regs->cp0reg[MIPS_CP0_BAD_VADDR][0] = kvm_read_c0_guest_badvaddr(cop0);
	regs->cp0reg[MIPS_CP0_TLB_HI][0] = kvm_read_c0_guest_entryhi(cop0);
	regs->cp0reg[MIPS_CP0_EXC_PC][0] = kvm_read_c0_guest_epc(cop0);

	regs->cp0reg[MIPS_CP0_STATUS][0] = kvm_read_c0_guest_status(cop0);
	regs->cp0reg[MIPS_CP0_CAUSE][0] = kvm_read_c0_guest_cause(cop0);
	regs->cp0reg[MIPS_CP0_TLB_PG_MASK][0] =
	    kvm_read_c0_guest_pagemask(cop0);
	regs->cp0reg[MIPS_CP0_TLB_WIRED][0] = kvm_read_c0_guest_wired(cop0);
	regs->cp0reg[MIPS_CP0_ERROR_PC][0] = kvm_read_c0_guest_errorepc(cop0);

	regs->cp0reg[MIPS_CP0_CONFIG][0] = kvm_read_c0_guest_config(cop0);
	regs->cp0reg[MIPS_CP0_CONFIG][1] = kvm_read_c0_guest_config1(cop0);
	regs->cp0reg[MIPS_CP0_CONFIG][2] = kvm_read_c0_guest_config2(cop0);
	regs->cp0reg[MIPS_CP0_CONFIG][3] = kvm_read_c0_guest_config3(cop0);
	regs->cp0reg[MIPS_CP0_CONFIG][7] = kvm_read_c0_guest_config7(cop0);

	return 0;
}

static int kvm_trap_emul_vm_init(struct kvm *kvm)
{
	return 0;
}

static int kvm_trap_emul_vcpu_init(struct kvm_vcpu *vcpu)
{
	return 0;
}

static int kvm_trap_emul_vcpu_setup(struct kvm_vcpu *vcpu)
{
	struct mips_coproc *cop0 = vcpu->arch.cop0;
	uint32_t config1;
	int vcpu_id = vcpu->vcpu_id;

	/* Arch specific stuff, set up config registers properly so that the
	 * guest will come up as expected, for now we simulate a
	 * MIPS 24kc
	 */
	kvm_write_c0_guest_prid(cop0, 0x00019300);
	kvm_write_c0_guest_config(cop0,
				  MIPS_CONFIG0 | (0x1 << CP0C0_AR) |
				  (MMU_TYPE_R4000 << CP0C0_MT));

	/* Read the cache characteristics from the host Config1 Register */
	config1 = (read_c0_config1() & ~0x7f);

	/* Set up MMU size */
	config1 &= ~(0x3f << 25);
	config1 |= ((KVM_MIPS_GUEST_TLB_SIZE - 1) << 25);

	/* We unset some bits that we aren't emulating */
	config1 &=
	    ~((1 << CP0C1_C2) | (1 << CP0C1_MD) | (1 << CP0C1_PC) |
	      (1 << CP0C1_WR) | (1 << CP0C1_CA));
	kvm_write_c0_guest_config1(cop0, config1);

	kvm_write_c0_guest_config2(cop0, MIPS_CONFIG2);
	/* MIPS_CONFIG2 | (read_c0_config2() & 0xfff) */
	kvm_write_c0_guest_config3(cop0,
				   MIPS_CONFIG3 | (0 << CP0C3_VInt) | (1 <<
								       CP0C3_ULRI));

	/* Set Wait IE/IXMT Ignore in Config7, IAR, AR */
	kvm_write_c0_guest_config7(cop0, (MIPS_CONF7_WII) | (1 << 10));

	/* Setup IntCtl defaults, compatibilty mode for timer interrupts (HW5) */
	kvm_write_c0_guest_intctl(cop0, 0xFC000000);

	/* Put in vcpu id as CPUNum into Ebase Reg to handle SMP Guests */
	kvm_write_c0_guest_ebase(cop0, KVM_GUEST_KSEG0 | (vcpu_id & 0xFF));

	return 0;
}

static struct kvm_mips_callbacks kvm_trap_emul_callbacks = {
	/* exit handlers */
	.handle_cop_unusable = kvm_trap_emul_handle_cop_unusable,
	.handle_tlb_mod = kvm_trap_emul_handle_tlb_mod,
	.handle_tlb_st_miss = kvm_trap_emul_handle_tlb_st_miss,
	.handle_tlb_ld_miss = kvm_trap_emul_handle_tlb_ld_miss,
	.handle_addr_err_st = kvm_trap_emul_handle_addr_err_st,
	.handle_addr_err_ld = kvm_trap_emul_handle_addr_err_ld,
	.handle_syscall = kvm_trap_emul_handle_syscall,
	.handle_res_inst = kvm_trap_emul_handle_res_inst,
	.handle_break = kvm_trap_emul_handle_break,

	.vm_init = kvm_trap_emul_vm_init,
	.vcpu_init = kvm_trap_emul_vcpu_init,
	.vcpu_setup = kvm_trap_emul_vcpu_setup,
	.gva_to_gpa = kvm_trap_emul_gva_to_gpa_cb,
	.queue_timer_int = kvm_mips_queue_timer_int_cb,
	.dequeue_timer_int = kvm_mips_dequeue_timer_int_cb,
	.queue_io_int = kvm_mips_queue_io_int_cb,
	.dequeue_io_int = kvm_mips_dequeue_io_int_cb,
	.irq_deliver = kvm_mips_irq_deliver_cb,
	.irq_clear = kvm_mips_irq_clear_cb,
	.vcpu_ioctl_get_regs = kvm_trap_emul_ioctl_get_regs,
	.vcpu_ioctl_set_regs = kvm_trap_emul_ioctl_set_regs,
};

int kvm_mips_emulation_init(struct kvm_mips_callbacks **install_callbacks)
{
	*install_callbacks = &kvm_trap_emul_callbacks;
	return 0;
}
