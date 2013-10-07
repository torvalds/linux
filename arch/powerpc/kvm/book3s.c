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
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License, version 2, as
 * published by the Free Software Foundation.
 */

#include <linux/kvm_host.h>
#include <linux/err.h>
#include <linux/export.h>
#include <linux/slab.h>

#include <asm/reg.h>
#include <asm/cputable.h>
#include <asm/cacheflush.h>
#include <asm/tlbflush.h>
#include <asm/uaccess.h>
#include <asm/io.h>
#include <asm/kvm_ppc.h>
#include <asm/kvm_book3s.h>
#include <asm/mmu_context.h>
#include <asm/page.h>
#include <linux/gfp.h>
#include <linux/sched.h>
#include <linux/vmalloc.h>
#include <linux/highmem.h>

#include "book3s.h"
#include "trace.h"

#define VCPU_STAT(x) offsetof(struct kvm_vcpu, stat.x), KVM_STAT_VCPU

/* #define EXIT_DEBUG */

struct kvm_stats_debugfs_item debugfs_entries[] = {
	{ "exits",       VCPU_STAT(sum_exits) },
	{ "mmio",        VCPU_STAT(mmio_exits) },
	{ "sig",         VCPU_STAT(signal_exits) },
	{ "sysc",        VCPU_STAT(syscall_exits) },
	{ "inst_emu",    VCPU_STAT(emulated_inst_exits) },
	{ "dec",         VCPU_STAT(dec_exits) },
	{ "ext_intr",    VCPU_STAT(ext_intr_exits) },
	{ "queue_intr",  VCPU_STAT(queue_intr) },
	{ "halt_wakeup", VCPU_STAT(halt_wakeup) },
	{ "pf_storage",  VCPU_STAT(pf_storage) },
	{ "sp_storage",  VCPU_STAT(sp_storage) },
	{ "pf_instruc",  VCPU_STAT(pf_instruc) },
	{ "sp_instruc",  VCPU_STAT(sp_instruc) },
	{ "ld",          VCPU_STAT(ld) },
	{ "ld_slow",     VCPU_STAT(ld_slow) },
	{ "st",          VCPU_STAT(st) },
	{ "st_slow",     VCPU_STAT(st_slow) },
	{ NULL }
};

void kvmppc_core_load_host_debugstate(struct kvm_vcpu *vcpu)
{
}

void kvmppc_core_load_guest_debugstate(struct kvm_vcpu *vcpu)
{
}

static inline unsigned long kvmppc_interrupt_offset(struct kvm_vcpu *vcpu)
{
	if (!is_kvmppc_hv_enabled(vcpu->kvm))
		return to_book3s(vcpu)->hior;
	return 0;
}

static inline void kvmppc_update_int_pending(struct kvm_vcpu *vcpu,
			unsigned long pending_now, unsigned long old_pending)
{
	if (is_kvmppc_hv_enabled(vcpu->kvm))
		return;
	if (pending_now)
		vcpu->arch.shared->int_pending = 1;
	else if (old_pending)
		vcpu->arch.shared->int_pending = 0;
}

static inline bool kvmppc_critical_section(struct kvm_vcpu *vcpu)
{
	ulong crit_raw;
	ulong crit_r1;
	bool crit;

	if (is_kvmppc_hv_enabled(vcpu->kvm))
		return false;

	crit_raw = vcpu->arch.shared->critical;
	crit_r1 = kvmppc_get_gpr(vcpu, 1);

	/* Truncate crit indicators in 32 bit mode */
	if (!(vcpu->arch.shared->msr & MSR_SF)) {
		crit_raw &= 0xffffffff;
		crit_r1 &= 0xffffffff;
	}

	/* Critical section when crit == r1 */
	crit = (crit_raw == crit_r1);
	/* ... and we're in supervisor mode */
	crit = crit && !(vcpu->arch.shared->msr & MSR_PR);

	return crit;
}

void kvmppc_inject_interrupt(struct kvm_vcpu *vcpu, int vec, u64 flags)
{
	vcpu->arch.shared->srr0 = kvmppc_get_pc(vcpu);
	vcpu->arch.shared->srr1 = vcpu->arch.shared->msr | flags;
	kvmppc_set_pc(vcpu, kvmppc_interrupt_offset(vcpu) + vec);
	vcpu->arch.mmu.reset_msr(vcpu);
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
	case 0x501: prio = BOOK3S_IRQPRIO_EXTERNAL_LEVEL;	break;
	case 0x600: prio = BOOK3S_IRQPRIO_ALIGNMENT;		break;
	case 0x700: prio = BOOK3S_IRQPRIO_PROGRAM;		break;
	case 0x800: prio = BOOK3S_IRQPRIO_FP_UNAVAIL;		break;
	case 0x900: prio = BOOK3S_IRQPRIO_DECREMENTER;		break;
	case 0xc00: prio = BOOK3S_IRQPRIO_SYSCALL;		break;
	case 0xd00: prio = BOOK3S_IRQPRIO_DEBUG;		break;
	case 0xf20: prio = BOOK3S_IRQPRIO_ALTIVEC;		break;
	case 0xf40: prio = BOOK3S_IRQPRIO_VSX;			break;
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

void kvmppc_core_queue_program(struct kvm_vcpu *vcpu, ulong flags)
{
	/* might as well deliver this straight away */
	kvmppc_inject_interrupt(vcpu, BOOK3S_INTERRUPT_PROGRAM, flags);
}
EXPORT_SYMBOL_GPL(kvmppc_core_queue_program);

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
	unsigned int vec = BOOK3S_INTERRUPT_EXTERNAL;

	if (irq->irq == KVM_INTERRUPT_SET_LEVEL)
		vec = BOOK3S_INTERRUPT_EXTERNAL_LEVEL;

	kvmppc_book3s_queue_irqprio(vcpu, vec);
}

void kvmppc_core_dequeue_external(struct kvm_vcpu *vcpu)
{
	kvmppc_book3s_dequeue_irqprio(vcpu, BOOK3S_INTERRUPT_EXTERNAL);
	kvmppc_book3s_dequeue_irqprio(vcpu, BOOK3S_INTERRUPT_EXTERNAL_LEVEL);
}

int kvmppc_book3s_irqprio_deliver(struct kvm_vcpu *vcpu, unsigned int priority)
{
	int deliver = 1;
	int vec = 0;
	bool crit = kvmppc_critical_section(vcpu);

	switch (priority) {
	case BOOK3S_IRQPRIO_DECREMENTER:
		deliver = (vcpu->arch.shared->msr & MSR_EE) && !crit;
		vec = BOOK3S_INTERRUPT_DECREMENTER;
		break;
	case BOOK3S_IRQPRIO_EXTERNAL:
	case BOOK3S_IRQPRIO_EXTERNAL_LEVEL:
		deliver = (vcpu->arch.shared->msr & MSR_EE) && !crit;
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
		case BOOK3S_IRQPRIO_EXTERNAL_LEVEL:
			/* External interrupts get cleared by userspace */
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

pfn_t kvmppc_gfn_to_pfn(struct kvm_vcpu *vcpu, gfn_t gfn, bool writing,
			bool *writable)
{
	ulong mp_pa = vcpu->arch.magic_page_pa;

	if (!(vcpu->arch.shared->msr & MSR_SF))
		mp_pa = (uint32_t)mp_pa;

	/* Magic page override */
	if (unlikely(mp_pa) &&
	    unlikely(((gfn << PAGE_SHIFT) & KVM_PAM) ==
		     ((mp_pa & PAGE_MASK) & KVM_PAM))) {
		ulong shared_page = ((ulong)vcpu->arch.shared) & PAGE_MASK;
		pfn_t pfn;

		pfn = (pfn_t)virt_to_phys((void*)shared_page) >> PAGE_SHIFT;
		get_page(pfn_to_page(pfn));
		if (writable)
			*writable = true;
		return pfn;
	}

	return gfn_to_pfn_prot(vcpu->kvm, gfn, writing, writable);
}
EXPORT_SYMBOL_GPL(kvmppc_gfn_to_pfn);

static int kvmppc_xlate(struct kvm_vcpu *vcpu, ulong eaddr, bool data,
			bool iswrite, struct kvmppc_pte *pte)
{
	int relocated = (vcpu->arch.shared->msr & (data ? MSR_DR : MSR_IR));
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
	}

	return r;
}

static hva_t kvmppc_bad_hva(void)
{
	return PAGE_OFFSET;
}

static hva_t kvmppc_pte_to_hva(struct kvm_vcpu *vcpu, struct kvmppc_pte *pte,
			       bool read)
{
	hva_t hpage;

	if (read && !pte->may_read)
		goto err;

	if (!read && !pte->may_write)
		goto err;

	hpage = gfn_to_hva(vcpu->kvm, pte->raddr >> PAGE_SHIFT);
	if (kvm_is_error_hva(hpage))
		goto err;

	return hpage | (pte->raddr & ~PAGE_MASK);
err:
	return kvmppc_bad_hva();
}

int kvmppc_st(struct kvm_vcpu *vcpu, ulong *eaddr, int size, void *ptr,
	      bool data)
{
	struct kvmppc_pte pte;

	vcpu->stat.st++;

	if (kvmppc_xlate(vcpu, *eaddr, data, true, &pte))
		return -ENOENT;

	*eaddr = pte.raddr;

	if (!pte.may_write)
		return -EPERM;

	if (kvm_write_guest(vcpu->kvm, pte.raddr, ptr, size))
		return EMULATE_DO_MMIO;

	return EMULATE_DONE;
}
EXPORT_SYMBOL_GPL(kvmppc_st);

int kvmppc_ld(struct kvm_vcpu *vcpu, ulong *eaddr, int size, void *ptr,
		      bool data)
{
	struct kvmppc_pte pte;
	hva_t hva = *eaddr;

	vcpu->stat.ld++;

	if (kvmppc_xlate(vcpu, *eaddr, data, false, &pte))
		goto nopte;

	*eaddr = pte.raddr;

	hva = kvmppc_pte_to_hva(vcpu, &pte, true);
	if (kvm_is_error_hva(hva))
		goto mmio;

	if (copy_from_user(ptr, (void __user *)hva, size)) {
		printk(KERN_INFO "kvmppc_ld at 0x%lx failed\n", hva);
		goto mmio;
	}

	return EMULATE_DONE;

nopte:
	return -ENOENT;
mmio:
	return EMULATE_DO_MMIO;
}
EXPORT_SYMBOL_GPL(kvmppc_ld);

int kvm_arch_vcpu_setup(struct kvm_vcpu *vcpu)
{
	return 0;
}

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
	return vcpu->kvm->arch.kvm_ops->get_sregs(vcpu, sregs);
}

int kvm_arch_vcpu_ioctl_set_sregs(struct kvm_vcpu *vcpu,
				  struct kvm_sregs *sregs)
{
	return vcpu->kvm->arch.kvm_ops->set_sregs(vcpu, sregs);
}

int kvm_arch_vcpu_ioctl_get_regs(struct kvm_vcpu *vcpu, struct kvm_regs *regs)
{
	int i;

	regs->pc = kvmppc_get_pc(vcpu);
	regs->cr = kvmppc_get_cr(vcpu);
	regs->ctr = kvmppc_get_ctr(vcpu);
	regs->lr = kvmppc_get_lr(vcpu);
	regs->xer = kvmppc_get_xer(vcpu);
	regs->msr = vcpu->arch.shared->msr;
	regs->srr0 = vcpu->arch.shared->srr0;
	regs->srr1 = vcpu->arch.shared->srr1;
	regs->pid = vcpu->arch.pid;
	regs->sprg0 = vcpu->arch.shared->sprg0;
	regs->sprg1 = vcpu->arch.shared->sprg1;
	regs->sprg2 = vcpu->arch.shared->sprg2;
	regs->sprg3 = vcpu->arch.shared->sprg3;
	regs->sprg4 = vcpu->arch.shared->sprg4;
	regs->sprg5 = vcpu->arch.shared->sprg5;
	regs->sprg6 = vcpu->arch.shared->sprg6;
	regs->sprg7 = vcpu->arch.shared->sprg7;

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
	vcpu->arch.shared->srr0 = regs->srr0;
	vcpu->arch.shared->srr1 = regs->srr1;
	vcpu->arch.shared->sprg0 = regs->sprg0;
	vcpu->arch.shared->sprg1 = regs->sprg1;
	vcpu->arch.shared->sprg2 = regs->sprg2;
	vcpu->arch.shared->sprg3 = regs->sprg3;
	vcpu->arch.shared->sprg4 = regs->sprg4;
	vcpu->arch.shared->sprg5 = regs->sprg5;
	vcpu->arch.shared->sprg6 = regs->sprg6;
	vcpu->arch.shared->sprg7 = regs->sprg7;

	for (i = 0; i < ARRAY_SIZE(regs->gpr); i++)
		kvmppc_set_gpr(vcpu, i, regs->gpr[i]);

	return 0;
}

int kvm_arch_vcpu_ioctl_get_fpu(struct kvm_vcpu *vcpu, struct kvm_fpu *fpu)
{
	return -ENOTSUPP;
}

int kvm_arch_vcpu_ioctl_set_fpu(struct kvm_vcpu *vcpu, struct kvm_fpu *fpu)
{
	return -ENOTSUPP;
}

int kvm_vcpu_ioctl_get_one_reg(struct kvm_vcpu *vcpu, struct kvm_one_reg *reg)
{
	int r;
	union kvmppc_one_reg val;
	int size;
	long int i;

	size = one_reg_size(reg->id);
	if (size > sizeof(val))
		return -EINVAL;

	r = vcpu->kvm->arch.kvm_ops->get_one_reg(vcpu, reg->id, &val);
	if (r == -EINVAL) {
		r = 0;
		switch (reg->id) {
		case KVM_REG_PPC_DAR:
			val = get_reg_val(reg->id, vcpu->arch.shared->dar);
			break;
		case KVM_REG_PPC_DSISR:
			val = get_reg_val(reg->id, vcpu->arch.shared->dsisr);
			break;
		case KVM_REG_PPC_FPR0 ... KVM_REG_PPC_FPR31:
			i = reg->id - KVM_REG_PPC_FPR0;
			val = get_reg_val(reg->id, vcpu->arch.fpr[i]);
			break;
		case KVM_REG_PPC_FPSCR:
			val = get_reg_val(reg->id, vcpu->arch.fpscr);
			break;
#ifdef CONFIG_ALTIVEC
		case KVM_REG_PPC_VR0 ... KVM_REG_PPC_VR31:
			if (!cpu_has_feature(CPU_FTR_ALTIVEC)) {
				r = -ENXIO;
				break;
			}
			val.vval = vcpu->arch.vr[reg->id - KVM_REG_PPC_VR0];
			break;
		case KVM_REG_PPC_VSCR:
			if (!cpu_has_feature(CPU_FTR_ALTIVEC)) {
				r = -ENXIO;
				break;
			}
			val = get_reg_val(reg->id, vcpu->arch.vscr.u[3]);
			break;
		case KVM_REG_PPC_VRSAVE:
			val = get_reg_val(reg->id, vcpu->arch.vrsave);
			break;
#endif /* CONFIG_ALTIVEC */
		case KVM_REG_PPC_DEBUG_INST: {
			u32 opcode = INS_TW;
			r = copy_to_user((u32 __user *)(long)reg->addr,
					 &opcode, sizeof(u32));
			break;
		}
#ifdef CONFIG_KVM_XICS
		case KVM_REG_PPC_ICP_STATE:
			if (!vcpu->arch.icp) {
				r = -ENXIO;
				break;
			}
			val = get_reg_val(reg->id, kvmppc_xics_get_icp(vcpu));
			break;
#endif /* CONFIG_KVM_XICS */
		default:
			r = -EINVAL;
			break;
		}
	}
	if (r)
		return r;

	if (copy_to_user((char __user *)(unsigned long)reg->addr, &val, size))
		r = -EFAULT;

	return r;
}

int kvm_vcpu_ioctl_set_one_reg(struct kvm_vcpu *vcpu, struct kvm_one_reg *reg)
{
	int r;
	union kvmppc_one_reg val;
	int size;
	long int i;

	size = one_reg_size(reg->id);
	if (size > sizeof(val))
		return -EINVAL;

	if (copy_from_user(&val, (char __user *)(unsigned long)reg->addr, size))
		return -EFAULT;

	r = vcpu->kvm->arch.kvm_ops->set_one_reg(vcpu, reg->id, &val);
	if (r == -EINVAL) {
		r = 0;
		switch (reg->id) {
		case KVM_REG_PPC_DAR:
			vcpu->arch.shared->dar = set_reg_val(reg->id, val);
			break;
		case KVM_REG_PPC_DSISR:
			vcpu->arch.shared->dsisr = set_reg_val(reg->id, val);
			break;
		case KVM_REG_PPC_FPR0 ... KVM_REG_PPC_FPR31:
			i = reg->id - KVM_REG_PPC_FPR0;
			vcpu->arch.fpr[i] = set_reg_val(reg->id, val);
			break;
		case KVM_REG_PPC_FPSCR:
			vcpu->arch.fpscr = set_reg_val(reg->id, val);
			break;
#ifdef CONFIG_ALTIVEC
		case KVM_REG_PPC_VR0 ... KVM_REG_PPC_VR31:
			if (!cpu_has_feature(CPU_FTR_ALTIVEC)) {
				r = -ENXIO;
				break;
			}
			vcpu->arch.vr[reg->id - KVM_REG_PPC_VR0] = val.vval;
			break;
		case KVM_REG_PPC_VSCR:
			if (!cpu_has_feature(CPU_FTR_ALTIVEC)) {
				r = -ENXIO;
				break;
			}
			vcpu->arch.vscr.u[3] = set_reg_val(reg->id, val);
			break;
		case KVM_REG_PPC_VRSAVE:
			if (!cpu_has_feature(CPU_FTR_ALTIVEC)) {
				r = -ENXIO;
				break;
			}
			vcpu->arch.vrsave = set_reg_val(reg->id, val);
			break;
#endif /* CONFIG_ALTIVEC */
#ifdef CONFIG_KVM_XICS
		case KVM_REG_PPC_ICP_STATE:
			if (!vcpu->arch.icp) {
				r = -ENXIO;
				break;
			}
			r = kvmppc_xics_set_icp(vcpu,
						set_reg_val(reg->id, val));
			break;
#endif /* CONFIG_KVM_XICS */
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

int kvmppc_vcpu_run(struct kvm_run *kvm_run, struct kvm_vcpu *vcpu)
{
	return vcpu->kvm->arch.kvm_ops->vcpu_run(kvm_run, vcpu);
}

int kvm_arch_vcpu_ioctl_translate(struct kvm_vcpu *vcpu,
                                  struct kvm_translation *tr)
{
	return 0;
}

int kvm_arch_vcpu_ioctl_set_guest_debug(struct kvm_vcpu *vcpu,
					struct kvm_guest_debug *dbg)
{
	return -EINVAL;
}

void kvmppc_decrementer_func(unsigned long data)
{
	struct kvm_vcpu *vcpu = (struct kvm_vcpu *)data;

	kvmppc_core_queue_dec(vcpu);
	kvm_vcpu_kick(vcpu);
}

struct kvm_vcpu *kvmppc_core_vcpu_create(struct kvm *kvm, unsigned int id)
{
	return kvm->arch.kvm_ops->vcpu_create(kvm, id);
}

void kvmppc_core_vcpu_free(struct kvm_vcpu *vcpu)
{
	vcpu->kvm->arch.kvm_ops->vcpu_free(vcpu);
}

int kvmppc_core_check_requests(struct kvm_vcpu *vcpu)
{
	return vcpu->kvm->arch.kvm_ops->check_requests(vcpu);
}

int kvm_vm_ioctl_get_dirty_log(struct kvm *kvm, struct kvm_dirty_log *log)
{
	return kvm->arch.kvm_ops->get_dirty_log(kvm, log);
}

void kvmppc_core_free_memslot(struct kvm *kvm, struct kvm_memory_slot *free,
			      struct kvm_memory_slot *dont)
{
	kvm->arch.kvm_ops->free_memslot(free, dont);
}

int kvmppc_core_create_memslot(struct kvm *kvm, struct kvm_memory_slot *slot,
			       unsigned long npages)
{
	return kvm->arch.kvm_ops->create_memslot(slot, npages);
}

void kvmppc_core_flush_memslot(struct kvm *kvm, struct kvm_memory_slot *memslot)
{
	kvm->arch.kvm_ops->flush_memslot(kvm, memslot);
}

int kvmppc_core_prepare_memory_region(struct kvm *kvm,
				struct kvm_memory_slot *memslot,
				struct kvm_userspace_memory_region *mem)
{
	return kvm->arch.kvm_ops->prepare_memory_region(kvm, memslot, mem);
}

void kvmppc_core_commit_memory_region(struct kvm *kvm,
				struct kvm_userspace_memory_region *mem,
				const struct kvm_memory_slot *old)
{
	kvm->arch.kvm_ops->commit_memory_region(kvm, mem, old);
}

int kvm_unmap_hva(struct kvm *kvm, unsigned long hva)
{
	return kvm->arch.kvm_ops->unmap_hva(kvm, hva);
}
EXPORT_SYMBOL_GPL(kvm_unmap_hva);

int kvm_unmap_hva_range(struct kvm *kvm, unsigned long start, unsigned long end)
{
	return kvm->arch.kvm_ops->unmap_hva_range(kvm, start, end);
}

int kvm_age_hva(struct kvm *kvm, unsigned long hva)
{
	return kvm->arch.kvm_ops->age_hva(kvm, hva);
}

int kvm_test_age_hva(struct kvm *kvm, unsigned long hva)
{
	return kvm->arch.kvm_ops->test_age_hva(kvm, hva);
}

void kvm_set_spte_hva(struct kvm *kvm, unsigned long hva, pte_t pte)
{
	kvm->arch.kvm_ops->set_spte_hva(kvm, hva, pte);
}

void kvmppc_mmu_destroy(struct kvm_vcpu *vcpu)
{
	vcpu->kvm->arch.kvm_ops->mmu_destroy(vcpu);
}

int kvmppc_core_init_vm(struct kvm *kvm)
{

#ifdef CONFIG_PPC64
	INIT_LIST_HEAD(&kvm->arch.spapr_tce_tables);
	INIT_LIST_HEAD(&kvm->arch.rtas_tokens);
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
}

int kvmppc_core_check_processor_compat(void)
{
	/*
	 * We always return 0 for book3s. We check
	 * for compatability while loading the HV
	 * or PR module
	 */
	return 0;
}

static int kvmppc_book3s_init(void)
{
	int r;

	r = kvm_init(NULL, sizeof(struct kvm_vcpu), 0, THIS_MODULE);
	if (r)
		return r;
#ifdef CONFIG_KVM_BOOK3S_32
	r = kvmppc_book3s_init_pr();
#endif
	return r;

}

static void kvmppc_book3s_exit(void)
{
#ifdef CONFIG_KVM_BOOK3S_32
	kvmppc_book3s_exit_pr();
#endif
	kvm_exit();
}

module_init(kvmppc_book3s_init);
module_exit(kvmppc_book3s_exit);
