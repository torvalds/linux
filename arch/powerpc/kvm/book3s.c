// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2009. SUSE Linux Products GmbH. All rights reserved.
 *
 * Authors:
 *    Alexander Graf <agraf@suse.de>
 *    Kevin Wolf <mail@kevin-wolf.de>
 *
 * Description:
 * This file is derived from arch/powerpc/kvm/44x.c,
 * by Hollis Blanchard <hollisb@us.ibm.com>.
 */

#include <linux/kvm_host.h>
#include <linux/err.h>
#include <linux/export.h>
#include <linux/slab.h>
#include <linux/module.h>
#include <linux/miscdevice.h>
#include <linux/gfp.h>
#include <linux/sched.h>
#include <linux/vmalloc.h>
#include <linux/highmem.h>

#include <asm/reg.h>
#include <asm/cputable.h>
#include <asm/cacheflush.h>
#include <linux/uaccess.h>
#include <asm/io.h>
#include <asm/kvm_ppc.h>
#include <asm/kvm_book3s.h>
#include <asm/mmu_context.h>
#include <asm/page.h>
#include <asm/xive.h>

#include "book3s.h"
#include "trace.h"

/* #define EXIT_DEBUG */

const struct _kvm_stats_desc kvm_vm_stats_desc[] = {
	KVM_GENERIC_VM_STATS(),
	STATS_DESC_ICOUNTER(VM, num_2M_pages),
	STATS_DESC_ICOUNTER(VM, num_1G_pages)
};

const struct kvm_stats_header kvm_vm_stats_header = {
	.name_size = KVM_STATS_NAME_SIZE,
	.num_desc = ARRAY_SIZE(kvm_vm_stats_desc),
	.id_offset = sizeof(struct kvm_stats_header),
	.desc_offset = sizeof(struct kvm_stats_header) + KVM_STATS_NAME_SIZE,
	.data_offset = sizeof(struct kvm_stats_header) + KVM_STATS_NAME_SIZE +
		       sizeof(kvm_vm_stats_desc),
};

const struct _kvm_stats_desc kvm_vcpu_stats_desc[] = {
	KVM_GENERIC_VCPU_STATS(),
	STATS_DESC_COUNTER(VCPU, sum_exits),
	STATS_DESC_COUNTER(VCPU, mmio_exits),
	STATS_DESC_COUNTER(VCPU, signal_exits),
	STATS_DESC_COUNTER(VCPU, light_exits),
	STATS_DESC_COUNTER(VCPU, itlb_real_miss_exits),
	STATS_DESC_COUNTER(VCPU, itlb_virt_miss_exits),
	STATS_DESC_COUNTER(VCPU, dtlb_real_miss_exits),
	STATS_DESC_COUNTER(VCPU, dtlb_virt_miss_exits),
	STATS_DESC_COUNTER(VCPU, syscall_exits),
	STATS_DESC_COUNTER(VCPU, isi_exits),
	STATS_DESC_COUNTER(VCPU, dsi_exits),
	STATS_DESC_COUNTER(VCPU, emulated_inst_exits),
	STATS_DESC_COUNTER(VCPU, dec_exits),
	STATS_DESC_COUNTER(VCPU, ext_intr_exits),
	STATS_DESC_COUNTER(VCPU, halt_successful_wait),
	STATS_DESC_COUNTER(VCPU, dbell_exits),
	STATS_DESC_COUNTER(VCPU, gdbell_exits),
	STATS_DESC_COUNTER(VCPU, ld),
	STATS_DESC_COUNTER(VCPU, st),
	STATS_DESC_COUNTER(VCPU, pf_storage),
	STATS_DESC_COUNTER(VCPU, pf_instruc),
	STATS_DESC_COUNTER(VCPU, sp_storage),
	STATS_DESC_COUNTER(VCPU, sp_instruc),
	STATS_DESC_COUNTER(VCPU, queue_intr),
	STATS_DESC_COUNTER(VCPU, ld_slow),
	STATS_DESC_COUNTER(VCPU, st_slow),
	STATS_DESC_COUNTER(VCPU, pthru_all),
	STATS_DESC_COUNTER(VCPU, pthru_host),
	STATS_DESC_COUNTER(VCPU, pthru_bad_aff)
};

const struct kvm_stats_header kvm_vcpu_stats_header = {
	.name_size = KVM_STATS_NAME_SIZE,
	.num_desc = ARRAY_SIZE(kvm_vcpu_stats_desc),
	.id_offset = sizeof(struct kvm_stats_header),
	.desc_offset = sizeof(struct kvm_stats_header) + KVM_STATS_NAME_SIZE,
	.data_offset = sizeof(struct kvm_stats_header) + KVM_STATS_NAME_SIZE +
		       sizeof(kvm_vcpu_stats_desc),
};

static inline void kvmppc_update_int_pending(struct kvm_vcpu *vcpu,
			unsigned long pending_now, unsigned long old_pending)
{
	if (is_kvmppc_hv_enabled(vcpu->kvm))
		return;
	if (pending_now)
		kvmppc_set_int_pending(vcpu, 1);
	else if (old_pending)
		kvmppc_set_int_pending(vcpu, 0);
}

static inline bool kvmppc_critical_section(struct kvm_vcpu *vcpu)
{
	ulong crit_raw;
	ulong crit_r1;
	bool crit;

	if (is_kvmppc_hv_enabled(vcpu->kvm))
		return false;

	crit_raw = kvmppc_get_critical(vcpu);
	crit_r1 = kvmppc_get_gpr(vcpu, 1);

	/* Truncate crit indicators in 32 bit mode */
	if (!(kvmppc_get_msr(vcpu) & MSR_SF)) {
		crit_raw &= 0xffffffff;
		crit_r1 &= 0xffffffff;
	}

	/* Critical section when crit == r1 */
	crit = (crit_raw == crit_r1);
	/* ... and we're in supervisor mode */
	crit = crit && !(kvmppc_get_msr(vcpu) & MSR_PR);

	return crit;
}

void kvmppc_inject_interrupt(struct kvm_vcpu *vcpu, int vec, u64 flags)
{
	vcpu->kvm->arch.kvm_ops->inject_interrupt(vcpu, vec, flags);
}

static int kvmppc_book3s_vec2irqprio(unsigned int vec)
{
	unsigned int prio;

	switch (vec) {
	case 0x100: prio = BOOK3S_IRQPRIO_SYSTEM_RESET;		break;
	case 0x200: prio = BOOK3S_IRQPRIO_MACHINE_CHECK;	break;
	case 0x300: prio = BOOK3S_IRQPRIO_DATA_STORAGE;		break;
	case 0x380: prio = BOOK3S_IRQPRIO_DATA_SEGMENT;		break;
	case 0x400: prio = BOOK3S_IRQPRIO_INST_STORAGE;		break;
	case 0x480: prio = BOOK3S_IRQPRIO_INST_SEGMENT;		break;
	case 0x500: prio = BOOK3S_IRQPRIO_EXTERNAL;		break;
	case 0x600: prio = BOOK3S_IRQPRIO_ALIGNMENT;		break;
	case 0x700: prio = BOOK3S_IRQPRIO_PROGRAM;		break;
	case 0x800: prio = BOOK3S_IRQPRIO_FP_UNAVAIL;		break;
	case 0x900: prio = BOOK3S_IRQPRIO_DECREMENTER;		break;
	case 0xc00: prio = BOOK3S_IRQPRIO_SYSCALL;		break;
	case 0xd00: prio = BOOK3S_IRQPRIO_DEBUG;		break;
	case 0xf20: prio = BOOK3S_IRQPRIO_ALTIVEC;		break;
	case 0xf40: prio = BOOK3S_IRQPRIO_VSX;			break;
	case 0xf60: prio = BOOK3S_IRQPRIO_FAC_UNAVAIL;		break;
	default:    prio = BOOK3S_IRQPRIO_MAX;			break;
	}

	return prio;
}

void kvmppc_book3s_dequeue_irqprio(struct kvm_vcpu *vcpu,
					  unsigned int vec)
{
	unsigned long old_pending = vcpu->arch.pending_exceptions;

	clear_bit(kvmppc_book3s_vec2irqprio(vec),
		  &vcpu->arch.pending_exceptions);

	kvmppc_update_int_pending(vcpu, vcpu->arch.pending_exceptions,
				  old_pending);
}

void kvmppc_book3s_queue_irqprio(struct kvm_vcpu *vcpu, unsigned int vec)
{
	vcpu->stat.queue_intr++;

	set_bit(kvmppc_book3s_vec2irqprio(vec),
		&vcpu->arch.pending_exceptions);
#ifdef EXIT_DEBUG
	printk(KERN_INFO "Queueing interrupt %x\n", vec);
#endif
}
EXPORT_SYMBOL_GPL(kvmppc_book3s_queue_irqprio);

void kvmppc_core_queue_machine_check(struct kvm_vcpu *vcpu, ulong flags)
{
	/* might as well deliver this straight away */
	kvmppc_inject_interrupt(vcpu, BOOK3S_INTERRUPT_MACHINE_CHECK, flags);
}
EXPORT_SYMBOL_GPL(kvmppc_core_queue_machine_check);

void kvmppc_core_queue_syscall(struct kvm_vcpu *vcpu)
{
	kvmppc_inject_interrupt(vcpu, BOOK3S_INTERRUPT_SYSCALL, 0);
}
EXPORT_SYMBOL(kvmppc_core_queue_syscall);

void kvmppc_core_queue_program(struct kvm_vcpu *vcpu, ulong flags)
{
	/* might as well deliver this straight away */
	kvmppc_inject_interrupt(vcpu, BOOK3S_INTERRUPT_PROGRAM, flags);
}
EXPORT_SYMBOL_GPL(kvmppc_core_queue_program);

void kvmppc_core_queue_fpunavail(struct kvm_vcpu *vcpu)
{
	/* might as well deliver this straight away */
	kvmppc_inject_interrupt(vcpu, BOOK3S_INTERRUPT_FP_UNAVAIL, 0);
}

void kvmppc_core_queue_vec_unavail(struct kvm_vcpu *vcpu)
{
	/* might as well deliver this straight away */
	kvmppc_inject_interrupt(vcpu, BOOK3S_INTERRUPT_ALTIVEC, 0);
}

void kvmppc_core_queue_vsx_unavail(struct kvm_vcpu *vcpu)
{
	/* might as well deliver this straight away */
	kvmppc_inject_interrupt(vcpu, BOOK3S_INTERRUPT_VSX, 0);
}

void kvmppc_core_queue_dec(struct kvm_vcpu *vcpu)
{
	kvmppc_book3s_queue_irqprio(vcpu, BOOK3S_INTERRUPT_DECREMENTER);
}
EXPORT_SYMBOL_GPL(kvmppc_core_queue_dec);

int kvmppc_core_pending_dec(struct kvm_vcpu *vcpu)
{
	return test_bit(BOOK3S_IRQPRIO_DECREMENTER, &vcpu->arch.pending_exceptions);
}
EXPORT_SYMBOL_GPL(kvmppc_core_pending_dec);

void kvmppc_core_dequeue_dec(struct kvm_vcpu *vcpu)
{
	kvmppc_book3s_dequeue_irqprio(vcpu, BOOK3S_INTERRUPT_DECREMENTER);
}
EXPORT_SYMBOL_GPL(kvmppc_core_dequeue_dec);

void kvmppc_core_queue_external(struct kvm_vcpu *vcpu,
                                struct kvm_interrupt *irq)
{
	/*
	 * This case (KVM_INTERRUPT_SET) should never actually arise for
	 * a pseries guest (because pseries guests expect their interrupt
	 * controllers to continue asserting an external interrupt request
	 * until it is acknowledged at the interrupt controller), but is
	 * included to avoid ABI breakage and potentially for other
	 * sorts of guest.
	 *
	 * There is a subtlety here: HV KVM does not test the
	 * external_oneshot flag in the code that synthesizes
	 * external interrupts for the guest just before entering
	 * the guest.  That is OK even if userspace did do a
	 * KVM_INTERRUPT_SET on a pseries guest vcpu, because the
	 * caller (kvm_vcpu_ioctl_interrupt) does a kvm_vcpu_kick()
	 * which ends up doing a smp_send_reschedule(), which will
	 * pull the guest all the way out to the host, meaning that
	 * we will call kvmppc_core_prepare_to_enter() before entering
	 * the guest again, and that will handle the external_oneshot
	 * flag correctly.
	 */
	if (irq->irq == KVM_INTERRUPT_SET)
		vcpu->arch.external_oneshot = 1;

	kvmppc_book3s_queue_irqprio(vcpu, BOOK3S_INTERRUPT_EXTERNAL);
}

void kvmppc_core_dequeue_external(struct kvm_vcpu *vcpu)
{
	kvmppc_book3s_dequeue_irqprio(vcpu, BOOK3S_INTERRUPT_EXTERNAL);
}

void kvmppc_core_queue_data_storage(struct kvm_vcpu *vcpu, ulong dar,
				    ulong flags)
{
	kvmppc_set_dar(vcpu, dar);
	kvmppc_set_dsisr(vcpu, flags);
	kvmppc_inject_interrupt(vcpu, BOOK3S_INTERRUPT_DATA_STORAGE, 0);
}
EXPORT_SYMBOL_GPL(kvmppc_core_queue_data_storage);

void kvmppc_core_queue_inst_storage(struct kvm_vcpu *vcpu, ulong flags)
{
	kvmppc_inject_interrupt(vcpu, BOOK3S_INTERRUPT_INST_STORAGE, flags);
}
EXPORT_SYMBOL_GPL(kvmppc_core_queue_inst_storage);

static int kvmppc_book3s_irqprio_deliver(struct kvm_vcpu *vcpu,
					 unsigned int priority)
{
	int deliver = 1;
	int vec = 0;
	bool crit = kvmppc_critical_section(vcpu);

	switch (priority) {
	case BOOK3S_IRQPRIO_DECREMENTER:
		deliver = (kvmppc_get_msr(vcpu) & MSR_EE) && !crit;
		vec = BOOK3S_INTERRUPT_DECREMENTER;
		break;
	case BOOK3S_IRQPRIO_EXTERNAL:
		deliver = (kvmppc_get_msr(vcpu) & MSR_EE) && !crit;
		vec = BOOK3S_INTERRUPT_EXTERNAL;
		break;
	case BOOK3S_IRQPRIO_SYSTEM_RESET:
		vec = BOOK3S_INTERRUPT_SYSTEM_RESET;
		break;
	case BOOK3S_IRQPRIO_MACHINE_CHECK:
		vec = BOOK3S_INTERRUPT_MACHINE_CHECK;
		break;
	case BOOK3S_IRQPRIO_DATA_STORAGE:
		vec = BOOK3S_INTERRUPT_DATA_STORAGE;
		break;
	case BOOK3S_IRQPRIO_INST_STORAGE:
		vec = BOOK3S_INTERRUPT_INST_STORAGE;
		break;
	case BOOK3S_IRQPRIO_DATA_SEGMENT:
		vec = BOOK3S_INTERRUPT_DATA_SEGMENT;
		break;
	case BOOK3S_IRQPRIO_INST_SEGMENT:
		vec = BOOK3S_INTERRUPT_INST_SEGMENT;
		break;
	case BOOK3S_IRQPRIO_ALIGNMENT:
		vec = BOOK3S_INTERRUPT_ALIGNMENT;
		break;
	case BOOK3S_IRQPRIO_PROGRAM:
		vec = BOOK3S_INTERRUPT_PROGRAM;
		break;
	case BOOK3S_IRQPRIO_VSX:
		vec = BOOK3S_INTERRUPT_VSX;
		break;
	case BOOK3S_IRQPRIO_ALTIVEC:
		vec = BOOK3S_INTERRUPT_ALTIVEC;
		break;
	case BOOK3S_IRQPRIO_FP_UNAVAIL:
		vec = BOOK3S_INTERRUPT_FP_UNAVAIL;
		break;
	case BOOK3S_IRQPRIO_SYSCALL:
		vec = BOOK3S_INTERRUPT_SYSCALL;
		break;
	case BOOK3S_IRQPRIO_DEBUG:
		vec = BOOK3S_INTERRUPT_TRACE;
		break;
	case BOOK3S_IRQPRIO_PERFORMANCE_MONITOR:
		vec = BOOK3S_INTERRUPT_PERFMON;
		break;
	case BOOK3S_IRQPRIO_FAC_UNAVAIL:
		vec = BOOK3S_INTERRUPT_FAC_UNAVAIL;
		break;
	default:
		deliver = 0;
		printk(KERN_ERR "KVM: Unknown interrupt: 0x%x\n", priority);
		break;
	}

#if 0
	printk(KERN_INFO "Deliver interrupt 0x%x? %x\n", vec, deliver);
#endif

	if (deliver)
		kvmppc_inject_interrupt(vcpu, vec, 0);

	return deliver;
}

/*
 * This function determines if an irqprio should be cleared once issued.
 */
static bool clear_irqprio(struct kvm_vcpu *vcpu, unsigned int priority)
{
	switch (priority) {
		case BOOK3S_IRQPRIO_DECREMENTER:
			/* DEC interrupts get cleared by mtdec */
			return false;
		case BOOK3S_IRQPRIO_EXTERNAL:
			/*
			 * External interrupts get cleared by userspace
			 * except when set by the KVM_INTERRUPT ioctl with
			 * KVM_INTERRUPT_SET (not KVM_INTERRUPT_SET_LEVEL).
			 */
			if (vcpu->arch.external_oneshot) {
				vcpu->arch.external_oneshot = 0;
				return true;
			}
			return false;
	}

	return true;
}

int kvmppc_core_prepare_to_enter(struct kvm_vcpu *vcpu)
{
	unsigned long *pending = &vcpu->arch.pending_exceptions;
	unsigned long old_pending = vcpu->arch.pending_exceptions;
	unsigned int priority;

#ifdef EXIT_DEBUG
	if (vcpu->arch.pending_exceptions)
		printk(KERN_EMERG "KVM: Check pending: %lx\n", vcpu->arch.pending_exceptions);
#endif
	priority = __ffs(*pending);
	while (priority < BOOK3S_IRQPRIO_MAX) {
		if (kvmppc_book3s_irqprio_deliver(vcpu, priority) &&
		    clear_irqprio(vcpu, priority)) {
			clear_bit(priority, &vcpu->arch.pending_exceptions);
			break;
		}

		priority = find_next_bit(pending,
					 BITS_PER_BYTE * sizeof(*pending),
					 priority + 1);
	}

	/* Tell the guest about our interrupt status */
	kvmppc_update_int_pending(vcpu, *pending, old_pending);

	return 0;
}
EXPORT_SYMBOL_GPL(kvmppc_core_prepare_to_enter);

kvm_pfn_t kvmppc_gpa_to_pfn(struct kvm_vcpu *vcpu, gpa_t gpa, bool writing,
			bool *writable)
{
	ulong mp_pa = vcpu->arch.magic_page_pa & KVM_PAM;
	gfn_t gfn = gpa >> PAGE_SHIFT;

	if (!(kvmppc_get_msr(vcpu) & MSR_SF))
		mp_pa = (uint32_t)mp_pa;

	/* Magic page override */
	gpa &= ~0xFFFULL;
	if (unlikely(mp_pa) && unlikely((gpa & KVM_PAM) == mp_pa)) {
		ulong shared_page = ((ulong)vcpu->arch.shared) & PAGE_MASK;
		kvm_pfn_t pfn;

		pfn = (kvm_pfn_t)virt_to_phys((void*)shared_page) >> PAGE_SHIFT;
		get_page(pfn_to_page(pfn));
		if (writable)
			*writable = true;
		return pfn;
	}

	return gfn_to_pfn_prot(vcpu->kvm, gfn, writing, writable);
}
EXPORT_SYMBOL_GPL(kvmppc_gpa_to_pfn);

int kvmppc_xlate(struct kvm_vcpu *vcpu, ulong eaddr, enum xlate_instdata xlid,
		 enum xlate_readwrite xlrw, struct kvmppc_pte *pte)
{
	bool data = (xlid == XLATE_DATA);
	bool iswrite = (xlrw == XLATE_WRITE);
	int relocated = (kvmppc_get_msr(vcpu) & (data ? MSR_DR : MSR_IR));
	int r;

	if (relocated) {
		r = vcpu->arch.mmu.xlate(vcpu, eaddr, pte, data, iswrite);
	} else {
		pte->eaddr = eaddr;
		pte->raddr = eaddr & KVM_PAM;
		pte->vpage = VSID_REAL | eaddr >> 12;
		pte->may_read = true;
		pte->may_write = true;
		pte->may_execute = true;
		r = 0;

		if ((kvmppc_get_msr(vcpu) & (MSR_IR | MSR_DR)) == MSR_DR &&
		    !data) {
			if ((vcpu->arch.hflags & BOOK3S_HFLAG_SPLIT_HACK) &&
			    ((eaddr & SPLIT_HACK_MASK) == SPLIT_HACK_OFFS))
			pte->raddr &= ~SPLIT_HACK_MASK;
		}
	}

	return r;
}

int kvmppc_load_last_inst(struct kvm_vcpu *vcpu,
		enum instruction_fetch_type type, u32 *inst)
{
	ulong pc = kvmppc_get_pc(vcpu);
	int r;

	if (type == INST_SC)
		pc -= 4;

	r = kvmppc_ld(vcpu, &pc, sizeof(u32), inst, false);
	if (r == EMULATE_DONE)
		return r;
	else
		return EMULATE_AGAIN;
}
EXPORT_SYMBOL_GPL(kvmppc_load_last_inst);

int kvmppc_subarch_vcpu_init(struct kvm_vcpu *vcpu)
{
	return 0;
}

void kvmppc_subarch_vcpu_uninit(struct kvm_vcpu *vcpu)
{
}

int kvm_arch_vcpu_ioctl_get_sregs(struct kvm_vcpu *vcpu,
				  struct kvm_sregs *sregs)
{
	int ret;

	vcpu_load(vcpu);
	ret = vcpu->kvm->arch.kvm_ops->get_sregs(vcpu, sregs);
	vcpu_put(vcpu);

	return ret;
}

int kvm_arch_vcpu_ioctl_set_sregs(struct kvm_vcpu *vcpu,
				  struct kvm_sregs *sregs)
{
	int ret;

	vcpu_load(vcpu);
	ret = vcpu->kvm->arch.kvm_ops->set_sregs(vcpu, sregs);
	vcpu_put(vcpu);

	return ret;
}

int kvm_arch_vcpu_ioctl_get_regs(struct kvm_vcpu *vcpu, struct kvm_regs *regs)
{
	int i;

	regs->pc = kvmppc_get_pc(vcpu);
	regs->cr = kvmppc_get_cr(vcpu);
	regs->ctr = kvmppc_get_ctr(vcpu);
	regs->lr = kvmppc_get_lr(vcpu);
	regs->xer = kvmppc_get_xer(vcpu);
	regs->msr = kvmppc_get_msr(vcpu);
	regs->srr0 = kvmppc_get_srr0(vcpu);
	regs->srr1 = kvmppc_get_srr1(vcpu);
	regs->pid = vcpu->arch.pid;
	regs->sprg0 = kvmppc_get_sprg0(vcpu);
	regs->sprg1 = kvmppc_get_sprg1(vcpu);
	regs->sprg2 = kvmppc_get_sprg2(vcpu);
	regs->sprg3 = kvmppc_get_sprg3(vcpu);
	regs->sprg4 = kvmppc_get_sprg4(vcpu);
	regs->sprg5 = kvmppc_get_sprg5(vcpu);
	regs->sprg6 = kvmppc_get_sprg6(vcpu);
	regs->sprg7 = kvmppc_get_sprg7(vcpu);

	for (i = 0; i < ARRAY_SIZE(regs->gpr); i++)
		regs->gpr[i] = kvmppc_get_gpr(vcpu, i);

	return 0;
}

int kvm_arch_vcpu_ioctl_set_regs(struct kvm_vcpu *vcpu, struct kvm_regs *regs)
{
	int i;

	kvmppc_set_pc(vcpu, regs->pc);
	kvmppc_set_cr(vcpu, regs->cr);
	kvmppc_set_ctr(vcpu, regs->ctr);
	kvmppc_set_lr(vcpu, regs->lr);
	kvmppc_set_xer(vcpu, regs->xer);
	kvmppc_set_msr(vcpu, regs->msr);
	kvmppc_set_srr0(vcpu, regs->srr0);
	kvmppc_set_srr1(vcpu, regs->srr1);
	kvmppc_set_sprg0(vcpu, regs->sprg0);
	kvmppc_set_sprg1(vcpu, regs->sprg1);
	kvmppc_set_sprg2(vcpu, regs->sprg2);
	kvmppc_set_sprg3(vcpu, regs->sprg3);
	kvmppc_set_sprg4(vcpu, regs->sprg4);
	kvmppc_set_sprg5(vcpu, regs->sprg5);
	kvmppc_set_sprg6(vcpu, regs->sprg6);
	kvmppc_set_sprg7(vcpu, regs->sprg7);

	for (i = 0; i < ARRAY_SIZE(regs->gpr); i++)
		kvmppc_set_gpr(vcpu, i, regs->gpr[i]);

	return 0;
}

int kvm_arch_vcpu_ioctl_get_fpu(struct kvm_vcpu *vcpu, struct kvm_fpu *fpu)
{
	return -EOPNOTSUPP;
}

int kvm_arch_vcpu_ioctl_set_fpu(struct kvm_vcpu *vcpu, struct kvm_fpu *fpu)
{
	return -EOPNOTSUPP;
}

int kvmppc_get_one_reg(struct kvm_vcpu *vcpu, u64 id,
			union kvmppc_one_reg *val)
{
	int r = 0;
	long int i;

	r = vcpu->kvm->arch.kvm_ops->get_one_reg(vcpu, id, val);
	if (r == -EINVAL) {
		r = 0;
		switch (id) {
		case KVM_REG_PPC_DAR:
			*val = get_reg_val(id, kvmppc_get_dar(vcpu));
			break;
		case KVM_REG_PPC_DSISR:
			*val = get_reg_val(id, kvmppc_get_dsisr(vcpu));
			break;
		case KVM_REG_PPC_FPR0 ... KVM_REG_PPC_FPR31:
			i = id - KVM_REG_PPC_FPR0;
			*val = get_reg_val(id, VCPU_FPR(vcpu, i));
			break;
		case KVM_REG_PPC_FPSCR:
			*val = get_reg_val(id, vcpu->arch.fp.fpscr);
			break;
#ifdef CONFIG_VSX
		case KVM_REG_PPC_VSR0 ... KVM_REG_PPC_VSR31:
			if (cpu_has_feature(CPU_FTR_VSX)) {
				i = id - KVM_REG_PPC_VSR0;
				val->vsxval[0] = vcpu->arch.fp.fpr[i][0];
				val->vsxval[1] = vcpu->arch.fp.fpr[i][1];
			} else {
				r = -ENXIO;
			}
			break;
#endif /* CONFIG_VSX */
		case KVM_REG_PPC_DEBUG_INST:
			*val = get_reg_val(id, INS_TW);
			break;
#ifdef CONFIG_KVM_XICS
		case KVM_REG_PPC_ICP_STATE:
			if (!vcpu->arch.icp && !vcpu->arch.xive_vcpu) {
				r = -ENXIO;
				break;
			}
			if (xics_on_xive())
				*val = get_reg_val(id, kvmppc_xive_get_icp(vcpu));
			else
				*val = get_reg_val(id, kvmppc_xics_get_icp(vcpu));
			break;
#endif /* CONFIG_KVM_XICS */
#ifdef CONFIG_KVM_XIVE
		case KVM_REG_PPC_VP_STATE:
			if (!vcpu->arch.xive_vcpu) {
				r = -ENXIO;
				break;
			}
			if (xive_enabled())
				r = kvmppc_xive_native_get_vp(vcpu, val);
			else
				r = -ENXIO;
			break;
#endif /* CONFIG_KVM_XIVE */
		case KVM_REG_PPC_FSCR:
			*val = get_reg_val(id, vcpu->arch.fscr);
			break;
		case KVM_REG_PPC_TAR:
			*val = get_reg_val(id, vcpu->arch.tar);
			break;
		case KVM_REG_PPC_EBBHR:
			*val = get_reg_val(id, vcpu->arch.ebbhr);
			break;
		case KVM_REG_PPC_EBBRR:
			*val = get_reg_val(id, vcpu->arch.ebbrr);
			break;
		case KVM_REG_PPC_BESCR:
			*val = get_reg_val(id, vcpu->arch.bescr);
			break;
		case KVM_REG_PPC_IC:
			*val = get_reg_val(id, vcpu->arch.ic);
			break;
		default:
			r = -EINVAL;
			break;
		}
	}

	return r;
}

int kvmppc_set_one_reg(struct kvm_vcpu *vcpu, u64 id,
			union kvmppc_one_reg *val)
{
	int r = 0;
	long int i;

	r = vcpu->kvm->arch.kvm_ops->set_one_reg(vcpu, id, val);
	if (r == -EINVAL) {
		r = 0;
		switch (id) {
		case KVM_REG_PPC_DAR:
			kvmppc_set_dar(vcpu, set_reg_val(id, *val));
			break;
		case KVM_REG_PPC_DSISR:
			kvmppc_set_dsisr(vcpu, set_reg_val(id, *val));
			break;
		case KVM_REG_PPC_FPR0 ... KVM_REG_PPC_FPR31:
			i = id - KVM_REG_PPC_FPR0;
			VCPU_FPR(vcpu, i) = set_reg_val(id, *val);
			break;
		case KVM_REG_PPC_FPSCR:
			vcpu->arch.fp.fpscr = set_reg_val(id, *val);
			break;
#ifdef CONFIG_VSX
		case KVM_REG_PPC_VSR0 ... KVM_REG_PPC_VSR31:
			if (cpu_has_feature(CPU_FTR_VSX)) {
				i = id - KVM_REG_PPC_VSR0;
				vcpu->arch.fp.fpr[i][0] = val->vsxval[0];
				vcpu->arch.fp.fpr[i][1] = val->vsxval[1];
			} else {
				r = -ENXIO;
			}
			break;
#endif /* CONFIG_VSX */
#ifdef CONFIG_KVM_XICS
		case KVM_REG_PPC_ICP_STATE:
			if (!vcpu->arch.icp && !vcpu->arch.xive_vcpu) {
				r = -ENXIO;
				break;
			}
			if (xics_on_xive())
				r = kvmppc_xive_set_icp(vcpu, set_reg_val(id, *val));
			else
				r = kvmppc_xics_set_icp(vcpu, set_reg_val(id, *val));
			break;
#endif /* CONFIG_KVM_XICS */
#ifdef CONFIG_KVM_XIVE
		case KVM_REG_PPC_VP_STATE:
			if (!vcpu->arch.xive_vcpu) {
				r = -ENXIO;
				break;
			}
			if (xive_enabled())
				r = kvmppc_xive_native_set_vp(vcpu, val);
			else
				r = -ENXIO;
			break;
#endif /* CONFIG_KVM_XIVE */
		case KVM_REG_PPC_FSCR:
			vcpu->arch.fscr = set_reg_val(id, *val);
			break;
		case KVM_REG_PPC_TAR:
			vcpu->arch.tar = set_reg_val(id, *val);
			break;
		case KVM_REG_PPC_EBBHR:
			vcpu->arch.ebbhr = set_reg_val(id, *val);
			break;
		case KVM_REG_PPC_EBBRR:
			vcpu->arch.ebbrr = set_reg_val(id, *val);
			break;
		case KVM_REG_PPC_BESCR:
			vcpu->arch.bescr = set_reg_val(id, *val);
			break;
		case KVM_REG_PPC_IC:
			vcpu->arch.ic = set_reg_val(id, *val);
			break;
		default:
			r = -EINVAL;
			break;
		}
	}

	return r;
}

void kvmppc_core_vcpu_load(struct kvm_vcpu *vcpu, int cpu)
{
	vcpu->kvm->arch.kvm_ops->vcpu_load(vcpu, cpu);
}

void kvmppc_core_vcpu_put(struct kvm_vcpu *vcpu)
{
	vcpu->kvm->arch.kvm_ops->vcpu_put(vcpu);
}

void kvmppc_set_msr(struct kvm_vcpu *vcpu, u64 msr)
{
	vcpu->kvm->arch.kvm_ops->set_msr(vcpu, msr);
}
EXPORT_SYMBOL_GPL(kvmppc_set_msr);

int kvmppc_vcpu_run(struct kvm_vcpu *vcpu)
{
	return vcpu->kvm->arch.kvm_ops->vcpu_run(vcpu);
}

int kvm_arch_vcpu_ioctl_translate(struct kvm_vcpu *vcpu,
                                  struct kvm_translation *tr)
{
	return 0;
}

int kvm_arch_vcpu_ioctl_set_guest_debug(struct kvm_vcpu *vcpu,
					struct kvm_guest_debug *dbg)
{
	vcpu_load(vcpu);
	vcpu->guest_debug = dbg->control;
	vcpu_put(vcpu);
	return 0;
}

void kvmppc_decrementer_func(struct kvm_vcpu *vcpu)
{
	kvmppc_core_queue_dec(vcpu);
	kvm_vcpu_kick(vcpu);
}

int kvmppc_core_vcpu_create(struct kvm_vcpu *vcpu)
{
	return vcpu->kvm->arch.kvm_ops->vcpu_create(vcpu);
}

void kvmppc_core_vcpu_free(struct kvm_vcpu *vcpu)
{
	vcpu->kvm->arch.kvm_ops->vcpu_free(vcpu);
}

int kvmppc_core_check_requests(struct kvm_vcpu *vcpu)
{
	return vcpu->kvm->arch.kvm_ops->check_requests(vcpu);
}

void kvm_arch_sync_dirty_log(struct kvm *kvm, struct kvm_memory_slot *memslot)
{

}

int kvm_vm_ioctl_get_dirty_log(struct kvm *kvm, struct kvm_dirty_log *log)
{
	return kvm->arch.kvm_ops->get_dirty_log(kvm, log);
}

void kvmppc_core_free_memslot(struct kvm *kvm, struct kvm_memory_slot *slot)
{
	kvm->arch.kvm_ops->free_memslot(slot);
}

void kvmppc_core_flush_memslot(struct kvm *kvm, struct kvm_memory_slot *memslot)
{
	kvm->arch.kvm_ops->flush_memslot(kvm, memslot);
}

int kvmppc_core_prepare_memory_region(struct kvm *kvm,
				struct kvm_memory_slot *memslot,
				const struct kvm_userspace_memory_region *mem,
				enum kvm_mr_change change)
{
	return kvm->arch.kvm_ops->prepare_memory_region(kvm, memslot, mem,
							change);
}

void kvmppc_core_commit_memory_region(struct kvm *kvm,
				const struct kvm_userspace_memory_region *mem,
				const struct kvm_memory_slot *old,
				const struct kvm_memory_slot *new,
				enum kvm_mr_change change)
{
	kvm->arch.kvm_ops->commit_memory_region(kvm, mem, old, new, change);
}

bool kvm_unmap_gfn_range(struct kvm *kvm, struct kvm_gfn_range *range)
{
	return kvm->arch.kvm_ops->unmap_gfn_range(kvm, range);
}

bool kvm_age_gfn(struct kvm *kvm, struct kvm_gfn_range *range)
{
	return kvm->arch.kvm_ops->age_gfn(kvm, range);
}

bool kvm_test_age_gfn(struct kvm *kvm, struct kvm_gfn_range *range)
{
	return kvm->arch.kvm_ops->test_age_gfn(kvm, range);
}

bool kvm_set_spte_gfn(struct kvm *kvm, struct kvm_gfn_range *range)
{
	return kvm->arch.kvm_ops->set_spte_gfn(kvm, range);
}

int kvmppc_core_init_vm(struct kvm *kvm)
{

#ifdef CONFIG_PPC64
	INIT_LIST_HEAD_RCU(&kvm->arch.spapr_tce_tables);
	INIT_LIST_HEAD(&kvm->arch.rtas_tokens);
	mutex_init(&kvm->arch.rtas_token_lock);
#endif

	return kvm->arch.kvm_ops->init_vm(kvm);
}

void kvmppc_core_destroy_vm(struct kvm *kvm)
{
	kvm->arch.kvm_ops->destroy_vm(kvm);

#ifdef CONFIG_PPC64
	kvmppc_rtas_tokens_free(kvm);
	WARN_ON(!list_empty(&kvm->arch.spapr_tce_tables));
#endif

#ifdef CONFIG_KVM_XICS
	/*
	 * Free the XIVE and XICS devices which are not directly freed by the
	 * device 'release' method
	 */
	kfree(kvm->arch.xive_devices.native);
	kvm->arch.xive_devices.native = NULL;
	kfree(kvm->arch.xive_devices.xics_on_xive);
	kvm->arch.xive_devices.xics_on_xive = NULL;
	kfree(kvm->arch.xics_device);
	kvm->arch.xics_device = NULL;
#endif /* CONFIG_KVM_XICS */
}

int kvmppc_h_logical_ci_load(struct kvm_vcpu *vcpu)
{
	unsigned long size = kvmppc_get_gpr(vcpu, 4);
	unsigned long addr = kvmppc_get_gpr(vcpu, 5);
	u64 buf;
	int srcu_idx;
	int ret;

	if (!is_power_of_2(size) || (size > sizeof(buf)))
		return H_TOO_HARD;

	srcu_idx = srcu_read_lock(&vcpu->kvm->srcu);
	ret = kvm_io_bus_read(vcpu, KVM_MMIO_BUS, addr, size, &buf);
	srcu_read_unlock(&vcpu->kvm->srcu, srcu_idx);
	if (ret != 0)
		return H_TOO_HARD;

	switch (size) {
	case 1:
		kvmppc_set_gpr(vcpu, 4, *(u8 *)&buf);
		break;

	case 2:
		kvmppc_set_gpr(vcpu, 4, be16_to_cpu(*(__be16 *)&buf));
		break;

	case 4:
		kvmppc_set_gpr(vcpu, 4, be32_to_cpu(*(__be32 *)&buf));
		break;

	case 8:
		kvmppc_set_gpr(vcpu, 4, be64_to_cpu(*(__be64 *)&buf));
		break;

	default:
		BUG();
	}

	return H_SUCCESS;
}
EXPORT_SYMBOL_GPL(kvmppc_h_logical_ci_load);

int kvmppc_h_logical_ci_store(struct kvm_vcpu *vcpu)
{
	unsigned long size = kvmppc_get_gpr(vcpu, 4);
	unsigned long addr = kvmppc_get_gpr(vcpu, 5);
	unsigned long val = kvmppc_get_gpr(vcpu, 6);
	u64 buf;
	int srcu_idx;
	int ret;

	switch (size) {
	case 1:
		*(u8 *)&buf = val;
		break;

	case 2:
		*(__be16 *)&buf = cpu_to_be16(val);
		break;

	case 4:
		*(__be32 *)&buf = cpu_to_be32(val);
		break;

	case 8:
		*(__be64 *)&buf = cpu_to_be64(val);
		break;

	default:
		return H_TOO_HARD;
	}

	srcu_idx = srcu_read_lock(&vcpu->kvm->srcu);
	ret = kvm_io_bus_write(vcpu, KVM_MMIO_BUS, addr, size, &buf);
	srcu_read_unlock(&vcpu->kvm->srcu, srcu_idx);
	if (ret != 0)
		return H_TOO_HARD;

	return H_SUCCESS;
}
EXPORT_SYMBOL_GPL(kvmppc_h_logical_ci_store);

int kvmppc_core_check_processor_compat(void)
{
	/*
	 * We always return 0 for book3s. We check
	 * for compatibility while loading the HV
	 * or PR module
	 */
	return 0;
}

int kvmppc_book3s_hcall_implemented(struct kvm *kvm, unsigned long hcall)
{
	return kvm->arch.kvm_ops->hcall_implemented(hcall);
}

#ifdef CONFIG_KVM_XICS
int kvm_set_irq(struct kvm *kvm, int irq_source_id, u32 irq, int level,
		bool line_status)
{
	if (xics_on_xive())
		return kvmppc_xive_set_irq(kvm, irq_source_id, irq, level,
					   line_status);
	else
		return kvmppc_xics_set_irq(kvm, irq_source_id, irq, level,
					   line_status);
}

int kvm_arch_set_irq_inatomic(struct kvm_kernel_irq_routing_entry *irq_entry,
			      struct kvm *kvm, int irq_source_id,
			      int level, bool line_status)
{
	return kvm_set_irq(kvm, irq_source_id, irq_entry->gsi,
			   level, line_status);
}
static int kvmppc_book3s_set_irq(struct kvm_kernel_irq_routing_entry *e,
				 struct kvm *kvm, int irq_source_id, int level,
				 bool line_status)
{
	return kvm_set_irq(kvm, irq_source_id, e->gsi, level, line_status);
}

int kvm_irq_map_gsi(struct kvm *kvm,
		    struct kvm_kernel_irq_routing_entry *entries, int gsi)
{
	entries->gsi = gsi;
	entries->type = KVM_IRQ_ROUTING_IRQCHIP;
	entries->set = kvmppc_book3s_set_irq;
	entries->irqchip.irqchip = 0;
	entries->irqchip.pin = gsi;
	return 1;
}

int kvm_irq_map_chip_pin(struct kvm *kvm, unsigned irqchip, unsigned pin)
{
	return pin;
}

#endif /* CONFIG_KVM_XICS */

static int kvmppc_book3s_init(void)
{
	int r;

	r = kvm_init(NULL, sizeof(struct kvm_vcpu), 0, THIS_MODULE);
	if (r)
		return r;
#ifdef CONFIG_KVM_BOOK3S_32_HANDLER
	r = kvmppc_book3s_init_pr();
#endif

#ifdef CONFIG_KVM_XICS
#ifdef CONFIG_KVM_XIVE
	if (xics_on_xive()) {
		kvm_register_device_ops(&kvm_xive_ops, KVM_DEV_TYPE_XICS);
		if (kvmppc_xive_native_supported())
			kvm_register_device_ops(&kvm_xive_native_ops,
						KVM_DEV_TYPE_XIVE);
	} else
#endif
		kvm_register_device_ops(&kvm_xics_ops, KVM_DEV_TYPE_XICS);
#endif
	return r;
}

static void kvmppc_book3s_exit(void)
{
#ifdef CONFIG_KVM_BOOK3S_32_HANDLER
	kvmppc_book3s_exit_pr();
#endif
	kvm_exit();
}

module_init(kvmppc_book3s_init);
module_exit(kvmppc_book3s_exit);

/* On 32bit this is our one and only kernel module */
#ifdef CONFIG_KVM_BOOK3S_32_HANDLER
MODULE_ALIAS_MISCDEV(KVM_MINOR);
MODULE_ALIAS("devname:kvm");
#endif
