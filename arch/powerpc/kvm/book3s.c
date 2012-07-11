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

static void kvmppc_book3s_dequeue_irqprio(struct kvm_vcpu *vcpu,
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


void kvmppc_core_queue_program(struct kvm_vcpu *vcpu, ulong flags)
{
	/* might as well deliver this straight away */
	kvmppc_inject_interrupt(vcpu, BOOK3S_INTERRUPT_PROGRAM, flags);
}

void kvmppc_core_queue_dec(struct kvm_vcpu *vcpu)
{
	kvmppc_book3s_queue_irqprio(vcpu, BOOK3S_INTERRUPT_DECREMENTER);
}

int kvmppc_core_pending_dec(struct kvm_vcpu *vcpu)
{
	return test_bit(BOOK3S_IRQPRIO_DECREMENTER, &vcpu->arch.pending_exceptions);
}

void kvmppc_core_dequeue_dec(struct kvm_vcpu *vcpu)
{
	kvmppc_book3s_dequeue_irqprio(vcpu, BOOK3S_INTERRUPT_DECREMENTER);
}

void kvmppc_core_queue_external(struct kvm_vcpu *vcpu,
                                struct kvm_interrupt *irq)
{
	unsigned int vec = BOOK3S_INTERRUPT_EXTERNAL;

	if (irq->irq == KVM_INTERRUPT_SET_LEVEL)
		vec = BOOK3S_INTERRUPT_EXTERNAL_LEVEL;

	kvmppc_book3s_queue_irqprio(vcpu, vec);
}

void kvmppc_core_dequeue_external(struct kvm_vcpu *vcpu,
                                  struct kvm_interrupt *irq)
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

pfn_t kvmppc_gfn_to_pfn(struct kvm_vcpu *vcpu, gfn_t gfn)
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
		return pfn;
	}

	return gfn_to_pfn(vcpu->kvm, gfn);
}

static int kvmppc_xlate(struct kvm_vcpu *vcpu, ulong eaddr, bool data,
			 struct kvmppc_pte *pte)
{
	int relocated = (vcpu->arch.shared->msr & (data ? MSR_DR : MSR_IR));
	int r;

	if (relocated) {
		r = vcpu->arch.mmu.xlate(vcpu, eaddr, pte, data);
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

	if (kvmppc_xlate(vcpu, *eaddr, data, &pte))
		return -ENOENT;

	*eaddr = pte.raddr;

	if (!pte.may_write)
		return -EPERM;

	if (kvm_write_guest(vcpu->kvm, pte.raddr, ptr, size))
		return EMULATE_DO_MMIO;

	return EMULATE_DONE;
}

int kvmppc_ld(struct kvm_vcpu *vcpu, ulong *eaddr, int size, void *ptr,
		      bool data)
{
	struct kvmppc_pte pte;
	hva_t hva = *eaddr;

	vcpu->stat.ld++;

	if (kvmppc_xlate(vcpu, *eaddr, data, &pte))
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

int kvm_arch_vcpu_setup(struct kvm_vcpu *vcpu)
{
	return 0;
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

int kvm_arch_vcpu_ioctl_translate(struct kvm_vcpu *vcpu,
                                  struct kvm_translation *tr)
{
	return 0;
}

void kvmppc_decrementer_func(unsigned long data)
{
	struct kvm_vcpu *vcpu = (struct kvm_vcpu *)data;

	kvmppc_core_queue_dec(vcpu);
	kvm_vcpu_kick(vcpu);
}
