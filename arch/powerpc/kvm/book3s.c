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

#include <asm/reg.h>
#include <asm/cputable.h>
#include <asm/cacheflush.h>
#include <asm/tlbflush.h>
#include <asm/uaccess.h>
#include <asm/io.h>
#include <asm/kvm_ppc.h>
#include <asm/kvm_book3s.h>
#include <asm/mmu_context.h>
#include <linux/sched.h>
#include <linux/vmalloc.h>

#define VCPU_STAT(x) offsetof(struct kvm_vcpu, stat.x), KVM_STAT_VCPU

/* #define EXIT_DEBUG */
/* #define EXIT_DEBUG_SIMPLE */

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

void kvmppc_core_vcpu_load(struct kvm_vcpu *vcpu, int cpu)
{
	memcpy(get_paca()->kvm_slb, to_book3s(vcpu)->slb_shadow, sizeof(get_paca()->kvm_slb));
	get_paca()->kvm_slb_max = to_book3s(vcpu)->slb_shadow_max;
}

void kvmppc_core_vcpu_put(struct kvm_vcpu *vcpu)
{
	memcpy(to_book3s(vcpu)->slb_shadow, get_paca()->kvm_slb, sizeof(get_paca()->kvm_slb));
	to_book3s(vcpu)->slb_shadow_max = get_paca()->kvm_slb_max;
}

#if defined(EXIT_DEBUG)
static u32 kvmppc_get_dec(struct kvm_vcpu *vcpu)
{
	u64 jd = mftb() - vcpu->arch.dec_jiffies;
	return vcpu->arch.dec - jd;
}
#endif

void kvmppc_set_msr(struct kvm_vcpu *vcpu, u64 msr)
{
	ulong old_msr = vcpu->arch.msr;

#ifdef EXIT_DEBUG
	printk(KERN_INFO "KVM: Set MSR to 0x%llx\n", msr);
#endif
	msr &= to_book3s(vcpu)->msr_mask;
	vcpu->arch.msr = msr;
	vcpu->arch.shadow_msr = msr | MSR_USER32;
	vcpu->arch.shadow_msr &= ( MSR_VEC | MSR_VSX | MSR_FP | MSR_FE0 |
				   MSR_USER64 | MSR_SE | MSR_BE | MSR_DE |
				   MSR_FE1);

	if (msr & (MSR_WE|MSR_POW)) {
		if (!vcpu->arch.pending_exceptions) {
			kvm_vcpu_block(vcpu);
			vcpu->stat.halt_wakeup++;
		}
	}

	if (((vcpu->arch.msr & (MSR_IR|MSR_DR)) != (old_msr & (MSR_IR|MSR_DR))) ||
	    (vcpu->arch.msr & MSR_PR) != (old_msr & MSR_PR)) {
		kvmppc_mmu_flush_segments(vcpu);
		kvmppc_mmu_map_segment(vcpu, vcpu->arch.pc);
	}
}

void kvmppc_inject_interrupt(struct kvm_vcpu *vcpu, int vec, u64 flags)
{
	vcpu->arch.srr0 = vcpu->arch.pc;
	vcpu->arch.srr1 = vcpu->arch.msr | flags;
	vcpu->arch.pc = to_book3s(vcpu)->hior + vec;
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
	clear_bit(kvmppc_book3s_vec2irqprio(vec),
		  &vcpu->arch.pending_exceptions);
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


void kvmppc_core_queue_program(struct kvm_vcpu *vcpu)
{
	kvmppc_book3s_queue_irqprio(vcpu, BOOK3S_INTERRUPT_PROGRAM);
}

void kvmppc_core_queue_dec(struct kvm_vcpu *vcpu)
{
	kvmppc_book3s_queue_irqprio(vcpu, BOOK3S_INTERRUPT_DECREMENTER);
}

int kvmppc_core_pending_dec(struct kvm_vcpu *vcpu)
{
	return test_bit(BOOK3S_INTERRUPT_DECREMENTER >> 7, &vcpu->arch.pending_exceptions);
}

void kvmppc_core_dequeue_dec(struct kvm_vcpu *vcpu)
{
	kvmppc_book3s_dequeue_irqprio(vcpu, BOOK3S_INTERRUPT_DECREMENTER);
}

void kvmppc_core_queue_external(struct kvm_vcpu *vcpu,
                                struct kvm_interrupt *irq)
{
	kvmppc_book3s_queue_irqprio(vcpu, BOOK3S_INTERRUPT_EXTERNAL);
}

int kvmppc_book3s_irqprio_deliver(struct kvm_vcpu *vcpu, unsigned int priority)
{
	int deliver = 1;
	int vec = 0;

	switch (priority) {
	case BOOK3S_IRQPRIO_DECREMENTER:
		deliver = vcpu->arch.msr & MSR_EE;
		vec = BOOK3S_INTERRUPT_DECREMENTER;
		break;
	case BOOK3S_IRQPRIO_EXTERNAL:
		deliver = vcpu->arch.msr & MSR_EE;
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
		kvmppc_inject_interrupt(vcpu, vec, 0ULL);

	return deliver;
}

void kvmppc_core_deliver_interrupts(struct kvm_vcpu *vcpu)
{
	unsigned long *pending = &vcpu->arch.pending_exceptions;
	unsigned int priority;

#ifdef EXIT_DEBUG
	if (vcpu->arch.pending_exceptions)
		printk(KERN_EMERG "KVM: Check pending: %lx\n", vcpu->arch.pending_exceptions);
#endif
	priority = __ffs(*pending);
	while (priority <= (sizeof(unsigned int) * 8)) {
		if (kvmppc_book3s_irqprio_deliver(vcpu, priority) &&
		    (priority != BOOK3S_IRQPRIO_DECREMENTER)) {
			/* DEC interrupts get cleared by mtdec */
			clear_bit(priority, &vcpu->arch.pending_exceptions);
			break;
		}

		priority = find_next_bit(pending,
					 BITS_PER_BYTE * sizeof(*pending),
					 priority + 1);
	}
}

void kvmppc_set_pvr(struct kvm_vcpu *vcpu, u32 pvr)
{
	vcpu->arch.hflags &= ~BOOK3S_HFLAG_SLB;
	vcpu->arch.pvr = pvr;
	if ((pvr >= 0x330000) && (pvr < 0x70330000)) {
		kvmppc_mmu_book3s_64_init(vcpu);
		to_book3s(vcpu)->hior = 0xfff00000;
		to_book3s(vcpu)->msr_mask = 0xffffffffffffffffULL;
	} else {
		kvmppc_mmu_book3s_32_init(vcpu);
		to_book3s(vcpu)->hior = 0;
		to_book3s(vcpu)->msr_mask = 0xffffffffULL;
	}

	/* If we are in hypervisor level on 970, we can tell the CPU to
	 * treat DCBZ as 32 bytes store */
	vcpu->arch.hflags &= ~BOOK3S_HFLAG_DCBZ32;
	if (vcpu->arch.mmu.is_dcbz32(vcpu) && (mfmsr() & MSR_HV) &&
	    !strcmp(cur_cpu_spec->platform, "ppc970"))
		vcpu->arch.hflags |= BOOK3S_HFLAG_DCBZ32;

}

/* Book3s_32 CPUs always have 32 bytes cache line size, which Linux assumes. To
 * make Book3s_32 Linux work on Book3s_64, we have to make sure we trap dcbz to
 * emulate 32 bytes dcbz length.
 *
 * The Book3s_64 inventors also realized this case and implemented a special bit
 * in the HID5 register, which is a hypervisor ressource. Thus we can't use it.
 *
 * My approach here is to patch the dcbz instruction on executing pages.
 */
static void kvmppc_patch_dcbz(struct kvm_vcpu *vcpu, struct kvmppc_pte *pte)
{
	bool touched = false;
	hva_t hpage;
	u32 *page;
	int i;

	hpage = gfn_to_hva(vcpu->kvm, pte->raddr >> PAGE_SHIFT);
	if (kvm_is_error_hva(hpage))
		return;

	hpage |= pte->raddr & ~PAGE_MASK;
	hpage &= ~0xFFFULL;

	page = vmalloc(HW_PAGE_SIZE);

	if (copy_from_user(page, (void __user *)hpage, HW_PAGE_SIZE))
		goto out;

	for (i=0; i < HW_PAGE_SIZE / 4; i++)
		if ((page[i] & 0xff0007ff) == INS_DCBZ) {
			page[i] &= 0xfffffff7; // reserved instruction, so we trap
			touched = true;
		}

	if (touched)
		copy_to_user((void __user *)hpage, page, HW_PAGE_SIZE);

out:
	vfree(page);
}

static int kvmppc_xlate(struct kvm_vcpu *vcpu, ulong eaddr, bool data,
			 struct kvmppc_pte *pte)
{
	int relocated = (vcpu->arch.msr & (data ? MSR_DR : MSR_IR));
	int r;

	if (relocated) {
		r = vcpu->arch.mmu.xlate(vcpu, eaddr, pte, data);
	} else {
		pte->eaddr = eaddr;
		pte->raddr = eaddr & 0xffffffff;
		pte->vpage = eaddr >> 12;
		switch (vcpu->arch.msr & (MSR_DR|MSR_IR)) {
		case 0:
			pte->vpage |= VSID_REAL;
		case MSR_DR:
			pte->vpage |= VSID_REAL_DR;
		case MSR_IR:
			pte->vpage |= VSID_REAL_IR;
		}
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

int kvmppc_st(struct kvm_vcpu *vcpu, ulong eaddr, int size, void *ptr)
{
	struct kvmppc_pte pte;
	hva_t hva = eaddr;

	vcpu->stat.st++;

	if (kvmppc_xlate(vcpu, eaddr, false, &pte))
		goto err;

	hva = kvmppc_pte_to_hva(vcpu, &pte, false);
	if (kvm_is_error_hva(hva))
		goto err;

	if (copy_to_user((void __user *)hva, ptr, size)) {
		printk(KERN_INFO "kvmppc_st at 0x%lx failed\n", hva);
		goto err;
	}

	return 0;

err:
	return -ENOENT;
}

int kvmppc_ld(struct kvm_vcpu *vcpu, ulong eaddr, int size, void *ptr,
		      bool data)
{
	struct kvmppc_pte pte;
	hva_t hva = eaddr;

	vcpu->stat.ld++;

	if (kvmppc_xlate(vcpu, eaddr, data, &pte))
		goto err;

	hva = kvmppc_pte_to_hva(vcpu, &pte, true);
	if (kvm_is_error_hva(hva))
		goto err;

	if (copy_from_user(ptr, (void __user *)hva, size)) {
		printk(KERN_INFO "kvmppc_ld at 0x%lx failed\n", hva);
		goto err;
	}

	return 0;

err:
	return -ENOENT;
}

static int kvmppc_visible_gfn(struct kvm_vcpu *vcpu, gfn_t gfn)
{
	return kvm_is_visible_gfn(vcpu->kvm, gfn);
}

int kvmppc_handle_pagefault(struct kvm_run *run, struct kvm_vcpu *vcpu,
			    ulong eaddr, int vec)
{
	bool data = (vec == BOOK3S_INTERRUPT_DATA_STORAGE);
	int r = RESUME_GUEST;
	int relocated;
	int page_found = 0;
	struct kvmppc_pte pte;
	bool is_mmio = false;

	if ( vec == BOOK3S_INTERRUPT_DATA_STORAGE ) {
		relocated = (vcpu->arch.msr & MSR_DR);
	} else {
		relocated = (vcpu->arch.msr & MSR_IR);
	}

	/* Resolve real address if translation turned on */
	if (relocated) {
		page_found = vcpu->arch.mmu.xlate(vcpu, eaddr, &pte, data);
	} else {
		pte.may_execute = true;
		pte.may_read = true;
		pte.may_write = true;
		pte.raddr = eaddr & 0xffffffff;
		pte.eaddr = eaddr;
		pte.vpage = eaddr >> 12;
		switch (vcpu->arch.msr & (MSR_DR|MSR_IR)) {
		case 0:
			pte.vpage |= VSID_REAL;
		case MSR_DR:
			pte.vpage |= VSID_REAL_DR;
		case MSR_IR:
			pte.vpage |= VSID_REAL_IR;
		}
	}

	if (vcpu->arch.mmu.is_dcbz32(vcpu) &&
	   (!(vcpu->arch.hflags & BOOK3S_HFLAG_DCBZ32))) {
		/*
		 * If we do the dcbz hack, we have to NX on every execution,
		 * so we can patch the executing code. This renders our guest
		 * NX-less.
		 */
		pte.may_execute = !data;
	}

	if (page_found == -ENOENT) {
		/* Page not found in guest PTE entries */
		vcpu->arch.dear = vcpu->arch.fault_dear;
		to_book3s(vcpu)->dsisr = vcpu->arch.fault_dsisr;
		vcpu->arch.msr |= (vcpu->arch.shadow_msr & 0x00000000f8000000ULL);
		kvmppc_book3s_queue_irqprio(vcpu, vec);
	} else if (page_found == -EPERM) {
		/* Storage protection */
		vcpu->arch.dear = vcpu->arch.fault_dear;
		to_book3s(vcpu)->dsisr = vcpu->arch.fault_dsisr & ~DSISR_NOHPTE;
		to_book3s(vcpu)->dsisr |= DSISR_PROTFAULT;
		vcpu->arch.msr |= (vcpu->arch.shadow_msr & 0x00000000f8000000ULL);
		kvmppc_book3s_queue_irqprio(vcpu, vec);
	} else if (page_found == -EINVAL) {
		/* Page not found in guest SLB */
		vcpu->arch.dear = vcpu->arch.fault_dear;
		kvmppc_book3s_queue_irqprio(vcpu, vec + 0x80);
	} else if (!is_mmio &&
		   kvmppc_visible_gfn(vcpu, pte.raddr >> PAGE_SHIFT)) {
		/* The guest's PTE is not mapped yet. Map on the host */
		kvmppc_mmu_map_page(vcpu, &pte);
		if (data)
			vcpu->stat.sp_storage++;
		else if (vcpu->arch.mmu.is_dcbz32(vcpu) &&
			(!(vcpu->arch.hflags & BOOK3S_HFLAG_DCBZ32)))
			kvmppc_patch_dcbz(vcpu, &pte);
	} else {
		/* MMIO */
		vcpu->stat.mmio_exits++;
		vcpu->arch.paddr_accessed = pte.raddr;
		r = kvmppc_emulate_mmio(run, vcpu);
		if ( r == RESUME_HOST_NV )
			r = RESUME_HOST;
		if ( r == RESUME_GUEST_NV )
			r = RESUME_GUEST;
	}

	return r;
}

int kvmppc_handle_exit(struct kvm_run *run, struct kvm_vcpu *vcpu,
                       unsigned int exit_nr)
{
	int r = RESUME_HOST;

	vcpu->stat.sum_exits++;

	run->exit_reason = KVM_EXIT_UNKNOWN;
	run->ready_for_interrupt_injection = 1;
#ifdef EXIT_DEBUG
	printk(KERN_EMERG "exit_nr=0x%x | pc=0x%lx | dar=0x%lx | dec=0x%x | msr=0x%lx\n",
		exit_nr, vcpu->arch.pc, vcpu->arch.fault_dear,
		kvmppc_get_dec(vcpu), vcpu->arch.msr);
#elif defined (EXIT_DEBUG_SIMPLE)
	if ((exit_nr != 0x900) && (exit_nr != 0x500))
		printk(KERN_EMERG "exit_nr=0x%x | pc=0x%lx | dar=0x%lx | msr=0x%lx\n",
			exit_nr, vcpu->arch.pc, vcpu->arch.fault_dear,
			vcpu->arch.msr);
#endif
	kvm_resched(vcpu);
	switch (exit_nr) {
	case BOOK3S_INTERRUPT_INST_STORAGE:
		vcpu->stat.pf_instruc++;
		/* only care about PTEG not found errors, but leave NX alone */
		if (vcpu->arch.shadow_msr & 0x40000000) {
			r = kvmppc_handle_pagefault(run, vcpu, vcpu->arch.pc, exit_nr);
			vcpu->stat.sp_instruc++;
		} else if (vcpu->arch.mmu.is_dcbz32(vcpu) &&
			  (!(vcpu->arch.hflags & BOOK3S_HFLAG_DCBZ32))) {
			/*
			 * XXX If we do the dcbz hack we use the NX bit to flush&patch the page,
			 *     so we can't use the NX bit inside the guest. Let's cross our fingers,
			 *     that no guest that needs the dcbz hack does NX.
			 */
			kvmppc_mmu_pte_flush(vcpu, vcpu->arch.pc, ~0xFFFULL);
		} else {
			vcpu->arch.msr |= (vcpu->arch.shadow_msr & 0x58000000);
			kvmppc_book3s_queue_irqprio(vcpu, exit_nr);
			kvmppc_mmu_pte_flush(vcpu, vcpu->arch.pc, ~0xFFFULL);
			r = RESUME_GUEST;
		}
		break;
	case BOOK3S_INTERRUPT_DATA_STORAGE:
		vcpu->stat.pf_storage++;
		/* The only case we need to handle is missing shadow PTEs */
		if (vcpu->arch.fault_dsisr & DSISR_NOHPTE) {
			r = kvmppc_handle_pagefault(run, vcpu, vcpu->arch.fault_dear, exit_nr);
		} else {
			vcpu->arch.dear = vcpu->arch.fault_dear;
			to_book3s(vcpu)->dsisr = vcpu->arch.fault_dsisr;
			kvmppc_book3s_queue_irqprio(vcpu, exit_nr);
			kvmppc_mmu_pte_flush(vcpu, vcpu->arch.dear, ~0xFFFULL);
			r = RESUME_GUEST;
		}
		break;
	case BOOK3S_INTERRUPT_DATA_SEGMENT:
		if (kvmppc_mmu_map_segment(vcpu, vcpu->arch.fault_dear) < 0) {
			vcpu->arch.dear = vcpu->arch.fault_dear;
			kvmppc_book3s_queue_irqprio(vcpu,
				BOOK3S_INTERRUPT_DATA_SEGMENT);
		}
		r = RESUME_GUEST;
		break;
	case BOOK3S_INTERRUPT_INST_SEGMENT:
		if (kvmppc_mmu_map_segment(vcpu, vcpu->arch.pc) < 0) {
			kvmppc_book3s_queue_irqprio(vcpu,
				BOOK3S_INTERRUPT_INST_SEGMENT);
		}
		r = RESUME_GUEST;
		break;
	/* We're good on these - the host merely wanted to get our attention */
	case BOOK3S_INTERRUPT_DECREMENTER:
		vcpu->stat.dec_exits++;
		r = RESUME_GUEST;
		break;
	case BOOK3S_INTERRUPT_EXTERNAL:
		vcpu->stat.ext_intr_exits++;
		r = RESUME_GUEST;
		break;
	case BOOK3S_INTERRUPT_PROGRAM:
	{
		enum emulation_result er;

		if (vcpu->arch.msr & MSR_PR) {
#ifdef EXIT_DEBUG
			printk(KERN_INFO "Userspace triggered 0x700 exception at 0x%lx (0x%x)\n", vcpu->arch.pc, vcpu->arch.last_inst);
#endif
			if ((vcpu->arch.last_inst & 0xff0007ff) !=
			    (INS_DCBZ & 0xfffffff7)) {
				kvmppc_book3s_queue_irqprio(vcpu, exit_nr);
				r = RESUME_GUEST;
				break;
			}
		}

		vcpu->stat.emulated_inst_exits++;
		er = kvmppc_emulate_instruction(run, vcpu);
		switch (er) {
		case EMULATE_DONE:
			r = RESUME_GUEST;
			break;
		case EMULATE_FAIL:
			printk(KERN_CRIT "%s: emulation at %lx failed (%08x)\n",
			       __func__, vcpu->arch.pc, vcpu->arch.last_inst);
			kvmppc_book3s_queue_irqprio(vcpu, exit_nr);
			r = RESUME_GUEST;
			break;
		default:
			BUG();
		}
		break;
	}
	case BOOK3S_INTERRUPT_SYSCALL:
#ifdef EXIT_DEBUG
		printk(KERN_INFO "Syscall Nr %d\n", (int)vcpu->arch.gpr[0]);
#endif
		vcpu->stat.syscall_exits++;
		kvmppc_book3s_queue_irqprio(vcpu, exit_nr);
		r = RESUME_GUEST;
		break;
	case BOOK3S_INTERRUPT_MACHINE_CHECK:
	case BOOK3S_INTERRUPT_FP_UNAVAIL:
	case BOOK3S_INTERRUPT_TRACE:
	case BOOK3S_INTERRUPT_ALTIVEC:
	case BOOK3S_INTERRUPT_VSX:
		kvmppc_book3s_queue_irqprio(vcpu, exit_nr);
		r = RESUME_GUEST;
		break;
	default:
		/* Ugh - bork here! What did we get? */
		printk(KERN_EMERG "exit_nr=0x%x | pc=0x%lx | msr=0x%lx\n", exit_nr, vcpu->arch.pc, vcpu->arch.shadow_msr);
		r = RESUME_HOST;
		BUG();
		break;
	}


	if (!(r & RESUME_HOST)) {
		/* To avoid clobbering exit_reason, only check for signals if
		 * we aren't already exiting to userspace for some other
		 * reason. */
		if (signal_pending(current)) {
#ifdef EXIT_DEBUG
			printk(KERN_EMERG "KVM: Going back to host\n");
#endif
			vcpu->stat.signal_exits++;
			run->exit_reason = KVM_EXIT_INTR;
			r = -EINTR;
		} else {
			/* In case an interrupt came in that was triggered
			 * from userspace (like DEC), we need to check what
			 * to inject now! */
			kvmppc_core_deliver_interrupts(vcpu);
		}
	}

#ifdef EXIT_DEBUG
	printk(KERN_EMERG "KVM exit: vcpu=0x%p pc=0x%lx r=0x%x\n", vcpu, vcpu->arch.pc, r);
#endif

	return r;
}

int kvm_arch_vcpu_setup(struct kvm_vcpu *vcpu)
{
	return 0;
}

int kvm_arch_vcpu_ioctl_get_regs(struct kvm_vcpu *vcpu, struct kvm_regs *regs)
{
	int i;

	regs->pc = vcpu->arch.pc;
	regs->cr = vcpu->arch.cr;
	regs->ctr = vcpu->arch.ctr;
	regs->lr = vcpu->arch.lr;
	regs->xer = vcpu->arch.xer;
	regs->msr = vcpu->arch.msr;
	regs->srr0 = vcpu->arch.srr0;
	regs->srr1 = vcpu->arch.srr1;
	regs->pid = vcpu->arch.pid;
	regs->sprg0 = vcpu->arch.sprg0;
	regs->sprg1 = vcpu->arch.sprg1;
	regs->sprg2 = vcpu->arch.sprg2;
	regs->sprg3 = vcpu->arch.sprg3;
	regs->sprg5 = vcpu->arch.sprg4;
	regs->sprg6 = vcpu->arch.sprg5;
	regs->sprg7 = vcpu->arch.sprg6;

	for (i = 0; i < ARRAY_SIZE(regs->gpr); i++)
		regs->gpr[i] = vcpu->arch.gpr[i];

	return 0;
}

int kvm_arch_vcpu_ioctl_set_regs(struct kvm_vcpu *vcpu, struct kvm_regs *regs)
{
	int i;

	vcpu->arch.pc = regs->pc;
	vcpu->arch.cr = regs->cr;
	vcpu->arch.ctr = regs->ctr;
	vcpu->arch.lr = regs->lr;
	vcpu->arch.xer = regs->xer;
	kvmppc_set_msr(vcpu, regs->msr);
	vcpu->arch.srr0 = regs->srr0;
	vcpu->arch.srr1 = regs->srr1;
	vcpu->arch.sprg0 = regs->sprg0;
	vcpu->arch.sprg1 = regs->sprg1;
	vcpu->arch.sprg2 = regs->sprg2;
	vcpu->arch.sprg3 = regs->sprg3;
	vcpu->arch.sprg5 = regs->sprg4;
	vcpu->arch.sprg6 = regs->sprg5;
	vcpu->arch.sprg7 = regs->sprg6;

	for (i = 0; i < ARRAY_SIZE(vcpu->arch.gpr); i++)
		vcpu->arch.gpr[i] = regs->gpr[i];

	return 0;
}

int kvm_arch_vcpu_ioctl_get_sregs(struct kvm_vcpu *vcpu,
                                  struct kvm_sregs *sregs)
{
	struct kvmppc_vcpu_book3s *vcpu3s = to_book3s(vcpu);
	int i;

	sregs->pvr = vcpu->arch.pvr;

	sregs->u.s.sdr1 = to_book3s(vcpu)->sdr1;
	if (vcpu->arch.hflags & BOOK3S_HFLAG_SLB) {
		for (i = 0; i < 64; i++) {
			sregs->u.s.ppc64.slb[i].slbe = vcpu3s->slb[i].orige | i;
			sregs->u.s.ppc64.slb[i].slbv = vcpu3s->slb[i].origv;
		}
	} else {
		for (i = 0; i < 16; i++) {
			sregs->u.s.ppc32.sr[i] = vcpu3s->sr[i].raw;
			sregs->u.s.ppc32.sr[i] = vcpu3s->sr[i].raw;
		}
		for (i = 0; i < 8; i++) {
			sregs->u.s.ppc32.ibat[i] = vcpu3s->ibat[i].raw;
			sregs->u.s.ppc32.dbat[i] = vcpu3s->dbat[i].raw;
		}
	}
	return 0;
}

int kvm_arch_vcpu_ioctl_set_sregs(struct kvm_vcpu *vcpu,
                                  struct kvm_sregs *sregs)
{
	struct kvmppc_vcpu_book3s *vcpu3s = to_book3s(vcpu);
	int i;

	kvmppc_set_pvr(vcpu, sregs->pvr);

	vcpu3s->sdr1 = sregs->u.s.sdr1;
	if (vcpu->arch.hflags & BOOK3S_HFLAG_SLB) {
		for (i = 0; i < 64; i++) {
			vcpu->arch.mmu.slbmte(vcpu, sregs->u.s.ppc64.slb[i].slbv,
						    sregs->u.s.ppc64.slb[i].slbe);
		}
	} else {
		for (i = 0; i < 16; i++) {
			vcpu->arch.mmu.mtsrin(vcpu, i, sregs->u.s.ppc32.sr[i]);
		}
		for (i = 0; i < 8; i++) {
			kvmppc_set_bat(vcpu, &(vcpu3s->ibat[i]), false,
				       (u32)sregs->u.s.ppc32.ibat[i]);
			kvmppc_set_bat(vcpu, &(vcpu3s->ibat[i]), true,
				       (u32)(sregs->u.s.ppc32.ibat[i] >> 32));
			kvmppc_set_bat(vcpu, &(vcpu3s->dbat[i]), false,
				       (u32)sregs->u.s.ppc32.dbat[i]);
			kvmppc_set_bat(vcpu, &(vcpu3s->dbat[i]), true,
				       (u32)(sregs->u.s.ppc32.dbat[i] >> 32));
		}
	}

	/* Flush the MMU after messing with the segments */
	kvmppc_mmu_pte_flush(vcpu, 0, 0);
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

/*
 * Get (and clear) the dirty memory log for a memory slot.
 */
int kvm_vm_ioctl_get_dirty_log(struct kvm *kvm,
				      struct kvm_dirty_log *log)
{
	struct kvm_memory_slot *memslot;
	struct kvm_vcpu *vcpu;
	ulong ga, ga_end;
	int is_dirty = 0;
	int r, n;

	down_write(&kvm->slots_lock);

	r = kvm_get_dirty_log(kvm, log, &is_dirty);
	if (r)
		goto out;

	/* If nothing is dirty, don't bother messing with page tables. */
	if (is_dirty) {
		memslot = &kvm->memslots->memslots[log->slot];

		ga = memslot->base_gfn << PAGE_SHIFT;
		ga_end = ga + (memslot->npages << PAGE_SHIFT);

		kvm_for_each_vcpu(n, vcpu, kvm)
			kvmppc_mmu_pte_pflush(vcpu, ga, ga_end);

		n = ALIGN(memslot->npages, BITS_PER_LONG) / 8;
		memset(memslot->dirty_bitmap, 0, n);
	}

	r = 0;
out:
	up_write(&kvm->slots_lock);
	return r;
}

int kvmppc_core_check_processor_compat(void)
{
	return 0;
}

struct kvm_vcpu *kvmppc_core_vcpu_create(struct kvm *kvm, unsigned int id)
{
	struct kvmppc_vcpu_book3s *vcpu_book3s;
	struct kvm_vcpu *vcpu;
	int err;

	vcpu_book3s = (struct kvmppc_vcpu_book3s *)__get_free_pages( GFP_KERNEL | __GFP_ZERO,
			get_order(sizeof(struct kvmppc_vcpu_book3s)));
	if (!vcpu_book3s) {
		err = -ENOMEM;
		goto out;
	}

	vcpu = &vcpu_book3s->vcpu;
	err = kvm_vcpu_init(vcpu, kvm, id);
	if (err)
		goto free_vcpu;

	vcpu->arch.host_retip = kvm_return_point;
	vcpu->arch.host_msr = mfmsr();
	/* default to book3s_64 (970fx) */
	vcpu->arch.pvr = 0x3C0301;
	kvmppc_set_pvr(vcpu, vcpu->arch.pvr);
	vcpu_book3s->slb_nr = 64;

	/* remember where some real-mode handlers are */
	vcpu->arch.trampoline_lowmem = kvmppc_trampoline_lowmem;
	vcpu->arch.trampoline_enter = kvmppc_trampoline_enter;
	vcpu->arch.highmem_handler = (ulong)kvmppc_handler_highmem;

	vcpu->arch.shadow_msr = MSR_USER64;

	err = __init_new_context();
	if (err < 0)
		goto free_vcpu;
	vcpu_book3s->context_id = err;

	vcpu_book3s->vsid_max = ((vcpu_book3s->context_id + 1) << USER_ESID_BITS) - 1;
	vcpu_book3s->vsid_first = vcpu_book3s->context_id << USER_ESID_BITS;
	vcpu_book3s->vsid_next = vcpu_book3s->vsid_first;

	return vcpu;

free_vcpu:
	free_pages((long)vcpu_book3s, get_order(sizeof(struct kvmppc_vcpu_book3s)));
out:
	return ERR_PTR(err);
}

void kvmppc_core_vcpu_free(struct kvm_vcpu *vcpu)
{
	struct kvmppc_vcpu_book3s *vcpu_book3s = to_book3s(vcpu);

	__destroy_context(vcpu_book3s->context_id);
	kvm_vcpu_uninit(vcpu);
	free_pages((long)vcpu_book3s, get_order(sizeof(struct kvmppc_vcpu_book3s)));
}

extern int __kvmppc_vcpu_entry(struct kvm_run *kvm_run, struct kvm_vcpu *vcpu);
int __kvmppc_vcpu_run(struct kvm_run *kvm_run, struct kvm_vcpu *vcpu)
{
	int ret;

	/* No need to go into the guest when all we do is going out */
	if (signal_pending(current)) {
		kvm_run->exit_reason = KVM_EXIT_INTR;
		return -EINTR;
	}

	/* XXX we get called with irq disabled - change that! */
	local_irq_enable();

	ret = __kvmppc_vcpu_entry(kvm_run, vcpu);

	local_irq_disable();

	return ret;
}

static int kvmppc_book3s_init(void)
{
	return kvm_init(NULL, sizeof(struct kvmppc_vcpu_book3s), THIS_MODULE);
}

static void kvmppc_book3s_exit(void)
{
	kvm_exit();
}

module_init(kvmppc_book3s_init);
module_exit(kvmppc_book3s_exit);
