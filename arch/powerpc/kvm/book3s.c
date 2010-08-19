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
#include <linux/gfp.h>
#include <linux/sched.h>
#include <linux/vmalloc.h>
#include <linux/highmem.h>

#define VCPU_STAT(x) offsetof(struct kvm_vcpu, stat.x), KVM_STAT_VCPU

/* #define EXIT_DEBUG */
/* #define EXIT_DEBUG_SIMPLE */
/* #define DEBUG_EXT */

static int kvmppc_handle_ext(struct kvm_vcpu *vcpu, unsigned int exit_nr,
			     ulong msr);

/* Some compatibility defines */
#ifdef CONFIG_PPC_BOOK3S_32
#define MSR_USER32 MSR_USER
#define MSR_USER64 MSR_USER
#define HW_PAGE_SIZE PAGE_SIZE
#endif

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
#ifdef CONFIG_PPC_BOOK3S_64
	memcpy(to_svcpu(vcpu)->slb, to_book3s(vcpu)->slb_shadow, sizeof(to_svcpu(vcpu)->slb));
	memcpy(&get_paca()->shadow_vcpu, to_book3s(vcpu)->shadow_vcpu,
	       sizeof(get_paca()->shadow_vcpu));
	to_svcpu(vcpu)->slb_max = to_book3s(vcpu)->slb_shadow_max;
#endif

#ifdef CONFIG_PPC_BOOK3S_32
	current->thread.kvm_shadow_vcpu = to_book3s(vcpu)->shadow_vcpu;
#endif
}

void kvmppc_core_vcpu_put(struct kvm_vcpu *vcpu)
{
#ifdef CONFIG_PPC_BOOK3S_64
	memcpy(to_book3s(vcpu)->slb_shadow, to_svcpu(vcpu)->slb, sizeof(to_svcpu(vcpu)->slb));
	memcpy(to_book3s(vcpu)->shadow_vcpu, &get_paca()->shadow_vcpu,
	       sizeof(get_paca()->shadow_vcpu));
	to_book3s(vcpu)->slb_shadow_max = to_svcpu(vcpu)->slb_max;
#endif

	kvmppc_giveup_ext(vcpu, MSR_FP);
	kvmppc_giveup_ext(vcpu, MSR_VEC);
	kvmppc_giveup_ext(vcpu, MSR_VSX);
}

#if defined(EXIT_DEBUG)
static u32 kvmppc_get_dec(struct kvm_vcpu *vcpu)
{
	u64 jd = mftb() - vcpu->arch.dec_jiffies;
	return vcpu->arch.dec - jd;
}
#endif

static void kvmppc_recalc_shadow_msr(struct kvm_vcpu *vcpu)
{
	vcpu->arch.shadow_msr = vcpu->arch.msr;
	/* Guest MSR values */
	vcpu->arch.shadow_msr &= MSR_FE0 | MSR_FE1 | MSR_SF | MSR_SE |
				 MSR_BE | MSR_DE;
	/* Process MSR values */
	vcpu->arch.shadow_msr |= MSR_ME | MSR_RI | MSR_IR | MSR_DR | MSR_PR |
				 MSR_EE;
	/* External providers the guest reserved */
	vcpu->arch.shadow_msr |= (vcpu->arch.msr & vcpu->arch.guest_owned_ext);
	/* 64-bit Process MSR values */
#ifdef CONFIG_PPC_BOOK3S_64
	vcpu->arch.shadow_msr |= MSR_ISF | MSR_HV;
#endif
}

void kvmppc_set_msr(struct kvm_vcpu *vcpu, u64 msr)
{
	ulong old_msr = vcpu->arch.msr;

#ifdef EXIT_DEBUG
	printk(KERN_INFO "KVM: Set MSR to 0x%llx\n", msr);
#endif

	msr &= to_book3s(vcpu)->msr_mask;
	vcpu->arch.msr = msr;
	kvmppc_recalc_shadow_msr(vcpu);

	if (msr & (MSR_WE|MSR_POW)) {
		if (!vcpu->arch.pending_exceptions) {
			kvm_vcpu_block(vcpu);
			vcpu->stat.halt_wakeup++;
		}
	}

	if ((vcpu->arch.msr & (MSR_PR|MSR_IR|MSR_DR)) !=
		   (old_msr & (MSR_PR|MSR_IR|MSR_DR))) {
		kvmppc_mmu_flush_segments(vcpu);
		kvmppc_mmu_map_segment(vcpu, kvmppc_get_pc(vcpu));
	}

	/* Preload FPU if it's enabled */
	if (vcpu->arch.msr & MSR_FP)
		kvmppc_handle_ext(vcpu, BOOK3S_INTERRUPT_FP_UNAVAIL, MSR_FP);
}

void kvmppc_inject_interrupt(struct kvm_vcpu *vcpu, int vec, u64 flags)
{
	vcpu->arch.srr0 = kvmppc_get_pc(vcpu);
	vcpu->arch.srr1 = vcpu->arch.msr | flags;
	kvmppc_set_pc(vcpu, to_book3s(vcpu)->hior + vec);
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


void kvmppc_core_queue_program(struct kvm_vcpu *vcpu, ulong flags)
{
	to_book3s(vcpu)->prog_flags = flags;
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

void kvmppc_core_dequeue_external(struct kvm_vcpu *vcpu,
                                  struct kvm_interrupt *irq)
{
	kvmppc_book3s_dequeue_irqprio(vcpu, BOOK3S_INTERRUPT_EXTERNAL);
}

int kvmppc_book3s_irqprio_deliver(struct kvm_vcpu *vcpu, unsigned int priority)
{
	int deliver = 1;
	int vec = 0;
	ulong flags = 0ULL;

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
		flags = to_book3s(vcpu)->prog_flags;
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
		kvmppc_inject_interrupt(vcpu, vec, flags);

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
	while (priority < BOOK3S_IRQPRIO_MAX) {
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
	u32 host_pvr;

	vcpu->arch.hflags &= ~BOOK3S_HFLAG_SLB;
	vcpu->arch.pvr = pvr;
#ifdef CONFIG_PPC_BOOK3S_64
	if ((pvr >= 0x330000) && (pvr < 0x70330000)) {
		kvmppc_mmu_book3s_64_init(vcpu);
		to_book3s(vcpu)->hior = 0xfff00000;
		to_book3s(vcpu)->msr_mask = 0xffffffffffffffffULL;
	} else
#endif
	{
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

	/* Cell performs badly if MSR_FEx are set. So let's hope nobody
	   really needs them in a VM on Cell and force disable them. */
	if (!strcmp(cur_cpu_spec->platform, "ppc-cell-be"))
		to_book3s(vcpu)->msr_mask &= ~(MSR_FE0 | MSR_FE1);

#ifdef CONFIG_PPC_BOOK3S_32
	/* 32 bit Book3S always has 32 byte dcbz */
	vcpu->arch.hflags |= BOOK3S_HFLAG_DCBZ32;
#endif

	/* On some CPUs we can execute paired single operations natively */
	asm ( "mfpvr %0" : "=r"(host_pvr));
	switch (host_pvr) {
	case 0x00080200:	/* lonestar 2.0 */
	case 0x00088202:	/* lonestar 2.2 */
	case 0x70000100:	/* gekko 1.0 */
	case 0x00080100:	/* gekko 2.0 */
	case 0x00083203:	/* gekko 2.3a */
	case 0x00083213:	/* gekko 2.3b */
	case 0x00083204:	/* gekko 2.4 */
	case 0x00083214:	/* gekko 2.4e (8SE) - retail HW2 */
	case 0x00087200:	/* broadway */
		vcpu->arch.hflags |= BOOK3S_HFLAG_NATIVE_PS;
		/* Enable HID2.PSE - in case we need it later */
		mtspr(SPRN_HID2_GEKKO, mfspr(SPRN_HID2_GEKKO) | (1 << 29));
	}
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
	struct page *hpage;
	u64 hpage_offset;
	u32 *page;
	int i;

	hpage = gfn_to_page(vcpu->kvm, pte->raddr >> PAGE_SHIFT);
	if (is_error_page(hpage))
		return;

	hpage_offset = pte->raddr & ~PAGE_MASK;
	hpage_offset &= ~0xFFFULL;
	hpage_offset /= 4;

	get_page(hpage);
	page = kmap_atomic(hpage, KM_USER0);

	/* patch dcbz into reserved instruction, so we trap */
	for (i=hpage_offset; i < hpage_offset + (HW_PAGE_SIZE / 4); i++)
		if ((page[i] & 0xff0007ff) == INS_DCBZ)
			page[i] &= 0xfffffff7;

	kunmap_atomic(page, KM_USER0);
	put_page(hpage);
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
	bool dr = (vcpu->arch.msr & MSR_DR) ? true : false;
	bool ir = (vcpu->arch.msr & MSR_IR) ? true : false;
	u64 vsid;

	relocated = data ? dr : ir;

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
	}

	switch (vcpu->arch.msr & (MSR_DR|MSR_IR)) {
	case 0:
		pte.vpage |= ((u64)VSID_REAL << (SID_SHIFT - 12));
		break;
	case MSR_DR:
	case MSR_IR:
		vcpu->arch.mmu.esid_to_vsid(vcpu, eaddr >> SID_SHIFT, &vsid);

		if ((vcpu->arch.msr & (MSR_DR|MSR_IR)) == MSR_DR)
			pte.vpage |= ((u64)VSID_REAL_DR << (SID_SHIFT - 12));
		else
			pte.vpage |= ((u64)VSID_REAL_IR << (SID_SHIFT - 12));
		pte.vpage |= vsid;

		if (vsid == -1)
			page_found = -EINVAL;
		break;
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
		vcpu->arch.dear = kvmppc_get_fault_dar(vcpu);
		to_book3s(vcpu)->dsisr = to_svcpu(vcpu)->fault_dsisr;
		vcpu->arch.msr |= (to_svcpu(vcpu)->shadow_srr1 & 0x00000000f8000000ULL);
		kvmppc_book3s_queue_irqprio(vcpu, vec);
	} else if (page_found == -EPERM) {
		/* Storage protection */
		vcpu->arch.dear = kvmppc_get_fault_dar(vcpu);
		to_book3s(vcpu)->dsisr = to_svcpu(vcpu)->fault_dsisr & ~DSISR_NOHPTE;
		to_book3s(vcpu)->dsisr |= DSISR_PROTFAULT;
		vcpu->arch.msr |= (to_svcpu(vcpu)->shadow_srr1 & 0x00000000f8000000ULL);
		kvmppc_book3s_queue_irqprio(vcpu, vec);
	} else if (page_found == -EINVAL) {
		/* Page not found in guest SLB */
		vcpu->arch.dear = kvmppc_get_fault_dar(vcpu);
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
	}

	return r;
}

static inline int get_fpr_index(int i)
{
#ifdef CONFIG_VSX
	i *= 2;
#endif
	return i;
}

/* Give up external provider (FPU, Altivec, VSX) */
void kvmppc_giveup_ext(struct kvm_vcpu *vcpu, ulong msr)
{
	struct thread_struct *t = &current->thread;
	u64 *vcpu_fpr = vcpu->arch.fpr;
#ifdef CONFIG_VSX
	u64 *vcpu_vsx = vcpu->arch.vsr;
#endif
	u64 *thread_fpr = (u64*)t->fpr;
	int i;

	if (!(vcpu->arch.guest_owned_ext & msr))
		return;

#ifdef DEBUG_EXT
	printk(KERN_INFO "Giving up ext 0x%lx\n", msr);
#endif

	switch (msr) {
	case MSR_FP:
		giveup_fpu(current);
		for (i = 0; i < ARRAY_SIZE(vcpu->arch.fpr); i++)
			vcpu_fpr[i] = thread_fpr[get_fpr_index(i)];

		vcpu->arch.fpscr = t->fpscr.val;
		break;
	case MSR_VEC:
#ifdef CONFIG_ALTIVEC
		giveup_altivec(current);
		memcpy(vcpu->arch.vr, t->vr, sizeof(vcpu->arch.vr));
		vcpu->arch.vscr = t->vscr;
#endif
		break;
	case MSR_VSX:
#ifdef CONFIG_VSX
		__giveup_vsx(current);
		for (i = 0; i < ARRAY_SIZE(vcpu->arch.vsr); i++)
			vcpu_vsx[i] = thread_fpr[get_fpr_index(i) + 1];
#endif
		break;
	default:
		BUG();
	}

	vcpu->arch.guest_owned_ext &= ~msr;
	current->thread.regs->msr &= ~msr;
	kvmppc_recalc_shadow_msr(vcpu);
}

static int kvmppc_read_inst(struct kvm_vcpu *vcpu)
{
	ulong srr0 = kvmppc_get_pc(vcpu);
	u32 last_inst = kvmppc_get_last_inst(vcpu);
	int ret;

	ret = kvmppc_ld(vcpu, &srr0, sizeof(u32), &last_inst, false);
	if (ret == -ENOENT) {
		vcpu->arch.msr = kvmppc_set_field(vcpu->arch.msr, 33, 33, 1);
		vcpu->arch.msr = kvmppc_set_field(vcpu->arch.msr, 34, 36, 0);
		vcpu->arch.msr = kvmppc_set_field(vcpu->arch.msr, 42, 47, 0);
		kvmppc_book3s_queue_irqprio(vcpu, BOOK3S_INTERRUPT_INST_STORAGE);
		return EMULATE_AGAIN;
	}

	return EMULATE_DONE;
}

static int kvmppc_check_ext(struct kvm_vcpu *vcpu, unsigned int exit_nr)
{

	/* Need to do paired single emulation? */
	if (!(vcpu->arch.hflags & BOOK3S_HFLAG_PAIRED_SINGLE))
		return EMULATE_DONE;

	/* Read out the instruction */
	if (kvmppc_read_inst(vcpu) == EMULATE_DONE)
		/* Need to emulate */
		return EMULATE_FAIL;

	return EMULATE_AGAIN;
}

/* Handle external providers (FPU, Altivec, VSX) */
static int kvmppc_handle_ext(struct kvm_vcpu *vcpu, unsigned int exit_nr,
			     ulong msr)
{
	struct thread_struct *t = &current->thread;
	u64 *vcpu_fpr = vcpu->arch.fpr;
#ifdef CONFIG_VSX
	u64 *vcpu_vsx = vcpu->arch.vsr;
#endif
	u64 *thread_fpr = (u64*)t->fpr;
	int i;

	/* When we have paired singles, we emulate in software */
	if (vcpu->arch.hflags & BOOK3S_HFLAG_PAIRED_SINGLE)
		return RESUME_GUEST;

	if (!(vcpu->arch.msr & msr)) {
		kvmppc_book3s_queue_irqprio(vcpu, exit_nr);
		return RESUME_GUEST;
	}

	/* We already own the ext */
	if (vcpu->arch.guest_owned_ext & msr) {
		return RESUME_GUEST;
	}

#ifdef DEBUG_EXT
	printk(KERN_INFO "Loading up ext 0x%lx\n", msr);
#endif

	current->thread.regs->msr |= msr;

	switch (msr) {
	case MSR_FP:
		for (i = 0; i < ARRAY_SIZE(vcpu->arch.fpr); i++)
			thread_fpr[get_fpr_index(i)] = vcpu_fpr[i];

		t->fpscr.val = vcpu->arch.fpscr;
		t->fpexc_mode = 0;
		kvmppc_load_up_fpu();
		break;
	case MSR_VEC:
#ifdef CONFIG_ALTIVEC
		memcpy(t->vr, vcpu->arch.vr, sizeof(vcpu->arch.vr));
		t->vscr = vcpu->arch.vscr;
		t->vrsave = -1;
		kvmppc_load_up_altivec();
#endif
		break;
	case MSR_VSX:
#ifdef CONFIG_VSX
		for (i = 0; i < ARRAY_SIZE(vcpu->arch.vsr); i++)
			thread_fpr[get_fpr_index(i) + 1] = vcpu_vsx[i];
		kvmppc_load_up_vsx();
#endif
		break;
	default:
		BUG();
	}

	vcpu->arch.guest_owned_ext |= msr;

	kvmppc_recalc_shadow_msr(vcpu);

	return RESUME_GUEST;
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
		exit_nr, kvmppc_get_pc(vcpu), kvmppc_get_fault_dar(vcpu),
		kvmppc_get_dec(vcpu), to_svcpu(vcpu)->shadow_srr1);
#elif defined (EXIT_DEBUG_SIMPLE)
	if ((exit_nr != 0x900) && (exit_nr != 0x500))
		printk(KERN_EMERG "exit_nr=0x%x | pc=0x%lx | dar=0x%lx | msr=0x%lx\n",
			exit_nr, kvmppc_get_pc(vcpu), kvmppc_get_fault_dar(vcpu),
			vcpu->arch.msr);
#endif
	kvm_resched(vcpu);
	switch (exit_nr) {
	case BOOK3S_INTERRUPT_INST_STORAGE:
		vcpu->stat.pf_instruc++;

#ifdef CONFIG_PPC_BOOK3S_32
		/* We set segments as unused segments when invalidating them. So
		 * treat the respective fault as segment fault. */
		if (to_svcpu(vcpu)->sr[kvmppc_get_pc(vcpu) >> SID_SHIFT]
		    == SR_INVALID) {
			kvmppc_mmu_map_segment(vcpu, kvmppc_get_pc(vcpu));
			r = RESUME_GUEST;
			break;
		}
#endif

		/* only care about PTEG not found errors, but leave NX alone */
		if (to_svcpu(vcpu)->shadow_srr1 & 0x40000000) {
			r = kvmppc_handle_pagefault(run, vcpu, kvmppc_get_pc(vcpu), exit_nr);
			vcpu->stat.sp_instruc++;
		} else if (vcpu->arch.mmu.is_dcbz32(vcpu) &&
			  (!(vcpu->arch.hflags & BOOK3S_HFLAG_DCBZ32))) {
			/*
			 * XXX If we do the dcbz hack we use the NX bit to flush&patch the page,
			 *     so we can't use the NX bit inside the guest. Let's cross our fingers,
			 *     that no guest that needs the dcbz hack does NX.
			 */
			kvmppc_mmu_pte_flush(vcpu, kvmppc_get_pc(vcpu), ~0xFFFUL);
			r = RESUME_GUEST;
		} else {
			vcpu->arch.msr |= to_svcpu(vcpu)->shadow_srr1 & 0x58000000;
			kvmppc_book3s_queue_irqprio(vcpu, exit_nr);
			kvmppc_mmu_pte_flush(vcpu, kvmppc_get_pc(vcpu), ~0xFFFUL);
			r = RESUME_GUEST;
		}
		break;
	case BOOK3S_INTERRUPT_DATA_STORAGE:
	{
		ulong dar = kvmppc_get_fault_dar(vcpu);
		vcpu->stat.pf_storage++;

#ifdef CONFIG_PPC_BOOK3S_32
		/* We set segments as unused segments when invalidating them. So
		 * treat the respective fault as segment fault. */
		if ((to_svcpu(vcpu)->sr[dar >> SID_SHIFT]) == SR_INVALID) {
			kvmppc_mmu_map_segment(vcpu, dar);
			r = RESUME_GUEST;
			break;
		}
#endif

		/* The only case we need to handle is missing shadow PTEs */
		if (to_svcpu(vcpu)->fault_dsisr & DSISR_NOHPTE) {
			r = kvmppc_handle_pagefault(run, vcpu, dar, exit_nr);
		} else {
			vcpu->arch.dear = dar;
			to_book3s(vcpu)->dsisr = to_svcpu(vcpu)->fault_dsisr;
			kvmppc_book3s_queue_irqprio(vcpu, exit_nr);
			kvmppc_mmu_pte_flush(vcpu, vcpu->arch.dear, ~0xFFFUL);
			r = RESUME_GUEST;
		}
		break;
	}
	case BOOK3S_INTERRUPT_DATA_SEGMENT:
		if (kvmppc_mmu_map_segment(vcpu, kvmppc_get_fault_dar(vcpu)) < 0) {
			vcpu->arch.dear = kvmppc_get_fault_dar(vcpu);
			kvmppc_book3s_queue_irqprio(vcpu,
				BOOK3S_INTERRUPT_DATA_SEGMENT);
		}
		r = RESUME_GUEST;
		break;
	case BOOK3S_INTERRUPT_INST_SEGMENT:
		if (kvmppc_mmu_map_segment(vcpu, kvmppc_get_pc(vcpu)) < 0) {
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
	case BOOK3S_INTERRUPT_PERFMON:
		r = RESUME_GUEST;
		break;
	case BOOK3S_INTERRUPT_PROGRAM:
	{
		enum emulation_result er;
		ulong flags;

program_interrupt:
		flags = to_svcpu(vcpu)->shadow_srr1 & 0x1f0000ull;

		if (vcpu->arch.msr & MSR_PR) {
#ifdef EXIT_DEBUG
			printk(KERN_INFO "Userspace triggered 0x700 exception at 0x%lx (0x%x)\n", kvmppc_get_pc(vcpu), kvmppc_get_last_inst(vcpu));
#endif
			if ((kvmppc_get_last_inst(vcpu) & 0xff0007ff) !=
			    (INS_DCBZ & 0xfffffff7)) {
				kvmppc_core_queue_program(vcpu, flags);
				r = RESUME_GUEST;
				break;
			}
		}

		vcpu->stat.emulated_inst_exits++;
		er = kvmppc_emulate_instruction(run, vcpu);
		switch (er) {
		case EMULATE_DONE:
			r = RESUME_GUEST_NV;
			break;
		case EMULATE_AGAIN:
			r = RESUME_GUEST;
			break;
		case EMULATE_FAIL:
			printk(KERN_CRIT "%s: emulation at %lx failed (%08x)\n",
			       __func__, kvmppc_get_pc(vcpu), kvmppc_get_last_inst(vcpu));
			kvmppc_core_queue_program(vcpu, flags);
			r = RESUME_GUEST;
			break;
		case EMULATE_DO_MMIO:
			run->exit_reason = KVM_EXIT_MMIO;
			r = RESUME_HOST_NV;
			break;
		default:
			BUG();
		}
		break;
	}
	case BOOK3S_INTERRUPT_SYSCALL:
		// XXX make user settable
		if (vcpu->arch.osi_enabled &&
		    (((u32)kvmppc_get_gpr(vcpu, 3)) == OSI_SC_MAGIC_R3) &&
		    (((u32)kvmppc_get_gpr(vcpu, 4)) == OSI_SC_MAGIC_R4)) {
			u64 *gprs = run->osi.gprs;
			int i;

			run->exit_reason = KVM_EXIT_OSI;
			for (i = 0; i < 32; i++)
				gprs[i] = kvmppc_get_gpr(vcpu, i);
			vcpu->arch.osi_needed = 1;
			r = RESUME_HOST_NV;

		} else {
			vcpu->stat.syscall_exits++;
			kvmppc_book3s_queue_irqprio(vcpu, exit_nr);
			r = RESUME_GUEST;
		}
		break;
	case BOOK3S_INTERRUPT_FP_UNAVAIL:
	case BOOK3S_INTERRUPT_ALTIVEC:
	case BOOK3S_INTERRUPT_VSX:
	{
		int ext_msr = 0;

		switch (exit_nr) {
		case BOOK3S_INTERRUPT_FP_UNAVAIL: ext_msr = MSR_FP;  break;
		case BOOK3S_INTERRUPT_ALTIVEC:    ext_msr = MSR_VEC; break;
		case BOOK3S_INTERRUPT_VSX:        ext_msr = MSR_VSX; break;
		}

		switch (kvmppc_check_ext(vcpu, exit_nr)) {
		case EMULATE_DONE:
			/* everything ok - let's enable the ext */
			r = kvmppc_handle_ext(vcpu, exit_nr, ext_msr);
			break;
		case EMULATE_FAIL:
			/* we need to emulate this instruction */
			goto program_interrupt;
			break;
		default:
			/* nothing to worry about - go again */
			break;
		}
		break;
	}
	case BOOK3S_INTERRUPT_ALIGNMENT:
		if (kvmppc_read_inst(vcpu) == EMULATE_DONE) {
			to_book3s(vcpu)->dsisr = kvmppc_alignment_dsisr(vcpu,
				kvmppc_get_last_inst(vcpu));
			vcpu->arch.dear = kvmppc_alignment_dar(vcpu,
				kvmppc_get_last_inst(vcpu));
			kvmppc_book3s_queue_irqprio(vcpu, exit_nr);
		}
		r = RESUME_GUEST;
		break;
	case BOOK3S_INTERRUPT_MACHINE_CHECK:
	case BOOK3S_INTERRUPT_TRACE:
		kvmppc_book3s_queue_irqprio(vcpu, exit_nr);
		r = RESUME_GUEST;
		break;
	default:
		/* Ugh - bork here! What did we get? */
		printk(KERN_EMERG "exit_nr=0x%x | pc=0x%lx | msr=0x%lx\n",
			exit_nr, kvmppc_get_pc(vcpu), to_svcpu(vcpu)->shadow_srr1);
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
	printk(KERN_EMERG "KVM exit: vcpu=0x%p pc=0x%lx r=0x%x\n", vcpu, kvmppc_get_pc(vcpu), r);
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

	regs->pc = kvmppc_get_pc(vcpu);
	regs->cr = kvmppc_get_cr(vcpu);
	regs->ctr = kvmppc_get_ctr(vcpu);
	regs->lr = kvmppc_get_lr(vcpu);
	regs->xer = kvmppc_get_xer(vcpu);
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
	vcpu->arch.srr0 = regs->srr0;
	vcpu->arch.srr1 = regs->srr1;
	vcpu->arch.sprg0 = regs->sprg0;
	vcpu->arch.sprg1 = regs->sprg1;
	vcpu->arch.sprg2 = regs->sprg2;
	vcpu->arch.sprg3 = regs->sprg3;
	vcpu->arch.sprg5 = regs->sprg4;
	vcpu->arch.sprg6 = regs->sprg5;
	vcpu->arch.sprg7 = regs->sprg6;

	for (i = 0; i < ARRAY_SIZE(regs->gpr); i++)
		kvmppc_set_gpr(vcpu, i, regs->gpr[i]);

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
	int r;
	unsigned long n;

	mutex_lock(&kvm->slots_lock);

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

		n = kvm_dirty_bitmap_bytes(memslot);
		memset(memslot->dirty_bitmap, 0, n);
	}

	r = 0;
out:
	mutex_unlock(&kvm->slots_lock);
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
	int err = -ENOMEM;

	vcpu_book3s = vmalloc(sizeof(struct kvmppc_vcpu_book3s));
	if (!vcpu_book3s)
		goto out;

	memset(vcpu_book3s, 0, sizeof(struct kvmppc_vcpu_book3s));

	vcpu_book3s->shadow_vcpu = (struct kvmppc_book3s_shadow_vcpu *)
		kzalloc(sizeof(*vcpu_book3s->shadow_vcpu), GFP_KERNEL);
	if (!vcpu_book3s->shadow_vcpu)
		goto free_vcpu;

	vcpu = &vcpu_book3s->vcpu;
	err = kvm_vcpu_init(vcpu, kvm, id);
	if (err)
		goto free_shadow_vcpu;

	vcpu->arch.host_retip = kvm_return_point;
	vcpu->arch.host_msr = mfmsr();
#ifdef CONFIG_PPC_BOOK3S_64
	/* default to book3s_64 (970fx) */
	vcpu->arch.pvr = 0x3C0301;
#else
	/* default to book3s_32 (750) */
	vcpu->arch.pvr = 0x84202;
#endif
	kvmppc_set_pvr(vcpu, vcpu->arch.pvr);
	vcpu_book3s->slb_nr = 64;

	/* remember where some real-mode handlers are */
	vcpu->arch.trampoline_lowmem = kvmppc_trampoline_lowmem;
	vcpu->arch.trampoline_enter = kvmppc_trampoline_enter;
	vcpu->arch.highmem_handler = (ulong)kvmppc_handler_highmem;
#ifdef CONFIG_PPC_BOOK3S_64
	vcpu->arch.rmcall = *(ulong*)kvmppc_rmcall;
#else
	vcpu->arch.rmcall = (ulong)kvmppc_rmcall;
#endif

	vcpu->arch.shadow_msr = MSR_USER64;

	err = kvmppc_mmu_init(vcpu);
	if (err < 0)
		goto free_shadow_vcpu;

	return vcpu;

free_shadow_vcpu:
	kfree(vcpu_book3s->shadow_vcpu);
free_vcpu:
	vfree(vcpu_book3s);
out:
	return ERR_PTR(err);
}

void kvmppc_core_vcpu_free(struct kvm_vcpu *vcpu)
{
	struct kvmppc_vcpu_book3s *vcpu_book3s = to_book3s(vcpu);

	kvm_vcpu_uninit(vcpu);
	kfree(vcpu_book3s->shadow_vcpu);
	vfree(vcpu_book3s);
}

extern int __kvmppc_vcpu_entry(struct kvm_run *kvm_run, struct kvm_vcpu *vcpu);
int __kvmppc_vcpu_run(struct kvm_run *kvm_run, struct kvm_vcpu *vcpu)
{
	int ret;
	double fpr[32][TS_FPRWIDTH];
	unsigned int fpscr;
	int fpexc_mode;
#ifdef CONFIG_ALTIVEC
	vector128 vr[32];
	vector128 vscr;
	unsigned long uninitialized_var(vrsave);
	int used_vr;
#endif
#ifdef CONFIG_VSX
	int used_vsr;
#endif
	ulong ext_msr;

	/* No need to go into the guest when all we do is going out */
	if (signal_pending(current)) {
		kvm_run->exit_reason = KVM_EXIT_INTR;
		return -EINTR;
	}

	/* Save FPU state in stack */
	if (current->thread.regs->msr & MSR_FP)
		giveup_fpu(current);
	memcpy(fpr, current->thread.fpr, sizeof(current->thread.fpr));
	fpscr = current->thread.fpscr.val;
	fpexc_mode = current->thread.fpexc_mode;

#ifdef CONFIG_ALTIVEC
	/* Save Altivec state in stack */
	used_vr = current->thread.used_vr;
	if (used_vr) {
		if (current->thread.regs->msr & MSR_VEC)
			giveup_altivec(current);
		memcpy(vr, current->thread.vr, sizeof(current->thread.vr));
		vscr = current->thread.vscr;
		vrsave = current->thread.vrsave;
	}
#endif

#ifdef CONFIG_VSX
	/* Save VSX state in stack */
	used_vsr = current->thread.used_vsr;
	if (used_vsr && (current->thread.regs->msr & MSR_VSX))
			__giveup_vsx(current);
#endif

	/* Remember the MSR with disabled extensions */
	ext_msr = current->thread.regs->msr;

	/* XXX we get called with irq disabled - change that! */
	local_irq_enable();

	/* Preload FPU if it's enabled */
	if (vcpu->arch.msr & MSR_FP)
		kvmppc_handle_ext(vcpu, BOOK3S_INTERRUPT_FP_UNAVAIL, MSR_FP);

	ret = __kvmppc_vcpu_entry(kvm_run, vcpu);

	local_irq_disable();

	current->thread.regs->msr = ext_msr;

	/* Make sure we save the guest FPU/Altivec/VSX state */
	kvmppc_giveup_ext(vcpu, MSR_FP);
	kvmppc_giveup_ext(vcpu, MSR_VEC);
	kvmppc_giveup_ext(vcpu, MSR_VSX);

	/* Restore FPU state from stack */
	memcpy(current->thread.fpr, fpr, sizeof(current->thread.fpr));
	current->thread.fpscr.val = fpscr;
	current->thread.fpexc_mode = fpexc_mode;

#ifdef CONFIG_ALTIVEC
	/* Restore Altivec state from stack */
	if (used_vr && current->thread.used_vr) {
		memcpy(current->thread.vr, vr, sizeof(current->thread.vr));
		current->thread.vscr = vscr;
		current->thread.vrsave = vrsave;
	}
	current->thread.used_vr = used_vr;
#endif

#ifdef CONFIG_VSX
	current->thread.used_vsr = used_vsr;
#endif

	return ret;
}

static int kvmppc_book3s_init(void)
{
	int r;

	r = kvm_init(NULL, sizeof(struct kvmppc_vcpu_book3s), 0,
		     THIS_MODULE);

	if (r)
		return r;

	r = kvmppc_mmu_hpte_sysinit();

	return r;
}

static void kvmppc_book3s_exit(void)
{
	kvmppc_mmu_hpte_sysexit();
	kvm_exit();
}

module_init(kvmppc_book3s_init);
module_exit(kvmppc_book3s_exit);
