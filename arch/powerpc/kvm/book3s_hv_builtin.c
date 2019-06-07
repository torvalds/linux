/*
 * Copyright 2011 Paul Mackerras, IBM Corp. <paulus@au1.ibm.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License, version 2, as
 * published by the Free Software Foundation.
 */

#include <linux/cpu.h>
#include <linux/kvm_host.h>
#include <linux/preempt.h>
#include <linux/export.h>
#include <linux/sched.h>
#include <linux/spinlock.h>
#include <linux/init.h>
#include <linux/memblock.h>
#include <linux/sizes.h>
#include <linux/cma.h>
#include <linux/bitops.h>

#include <asm/asm-prototypes.h>
#include <asm/cputable.h>
#include <asm/kvm_ppc.h>
#include <asm/kvm_book3s.h>
#include <asm/archrandom.h>
#include <asm/xics.h>
#include <asm/xive.h>
#include <asm/dbell.h>
#include <asm/cputhreads.h>
#include <asm/io.h>
#include <asm/opal.h>
#include <asm/smp.h>

#define KVM_CMA_CHUNK_ORDER	18

#include "book3s_xics.h"
#include "book3s_xive.h"

/*
 * The XIVE module will populate these when it loads
 */
unsigned long (*__xive_vm_h_xirr)(struct kvm_vcpu *vcpu);
unsigned long (*__xive_vm_h_ipoll)(struct kvm_vcpu *vcpu, unsigned long server);
int (*__xive_vm_h_ipi)(struct kvm_vcpu *vcpu, unsigned long server,
		       unsigned long mfrr);
int (*__xive_vm_h_cppr)(struct kvm_vcpu *vcpu, unsigned long cppr);
int (*__xive_vm_h_eoi)(struct kvm_vcpu *vcpu, unsigned long xirr);
EXPORT_SYMBOL_GPL(__xive_vm_h_xirr);
EXPORT_SYMBOL_GPL(__xive_vm_h_ipoll);
EXPORT_SYMBOL_GPL(__xive_vm_h_ipi);
EXPORT_SYMBOL_GPL(__xive_vm_h_cppr);
EXPORT_SYMBOL_GPL(__xive_vm_h_eoi);

/*
 * Hash page table alignment on newer cpus(CPU_FTR_ARCH_206)
 * should be power of 2.
 */
#define HPT_ALIGN_PAGES		((1 << 18) >> PAGE_SHIFT) /* 256k */
/*
 * By default we reserve 5% of memory for hash pagetable allocation.
 */
static unsigned long kvm_cma_resv_ratio = 5;

static struct cma *kvm_cma;

static int __init early_parse_kvm_cma_resv(char *p)
{
	pr_debug("%s(%s)\n", __func__, p);
	if (!p)
		return -EINVAL;
	return kstrtoul(p, 0, &kvm_cma_resv_ratio);
}
early_param("kvm_cma_resv_ratio", early_parse_kvm_cma_resv);

struct page *kvm_alloc_hpt_cma(unsigned long nr_pages)
{
	VM_BUG_ON(order_base_2(nr_pages) < KVM_CMA_CHUNK_ORDER - PAGE_SHIFT);

	return cma_alloc(kvm_cma, nr_pages, order_base_2(HPT_ALIGN_PAGES),
			 false);
}
EXPORT_SYMBOL_GPL(kvm_alloc_hpt_cma);

void kvm_free_hpt_cma(struct page *page, unsigned long nr_pages)
{
	cma_release(kvm_cma, page, nr_pages);
}
EXPORT_SYMBOL_GPL(kvm_free_hpt_cma);

/**
 * kvm_cma_reserve() - reserve area for kvm hash pagetable
 *
 * This function reserves memory from early allocator. It should be
 * called by arch specific code once the memblock allocator
 * has been activated and all other subsystems have already allocated/reserved
 * memory.
 */
void __init kvm_cma_reserve(void)
{
	unsigned long align_size;
	struct memblock_region *reg;
	phys_addr_t selected_size = 0;

	/*
	 * We need CMA reservation only when we are in HV mode
	 */
	if (!cpu_has_feature(CPU_FTR_HVMODE))
		return;
	/*
	 * We cannot use memblock_phys_mem_size() here, because
	 * memblock_analyze() has not been called yet.
	 */
	for_each_memblock(memory, reg)
		selected_size += memblock_region_memory_end_pfn(reg) -
				 memblock_region_memory_base_pfn(reg);

	selected_size = (selected_size * kvm_cma_resv_ratio / 100) << PAGE_SHIFT;
	if (selected_size) {
		pr_debug("%s: reserving %ld MiB for global area\n", __func__,
			 (unsigned long)selected_size / SZ_1M);
		align_size = HPT_ALIGN_PAGES << PAGE_SHIFT;
		cma_declare_contiguous(0, selected_size, 0, align_size,
			KVM_CMA_CHUNK_ORDER - PAGE_SHIFT, false, "kvm_cma",
			&kvm_cma);
	}
}

/*
 * Real-mode H_CONFER implementation.
 * We check if we are the only vcpu out of this virtual core
 * still running in the guest and not ceded.  If so, we pop up
 * to the virtual-mode implementation; if not, just return to
 * the guest.
 */
long int kvmppc_rm_h_confer(struct kvm_vcpu *vcpu, int target,
			    unsigned int yield_count)
{
	struct kvmppc_vcore *vc = local_paca->kvm_hstate.kvm_vcore;
	int ptid = local_paca->kvm_hstate.ptid;
	int threads_running;
	int threads_ceded;
	int threads_conferring;
	u64 stop = get_tb() + 10 * tb_ticks_per_usec;
	int rv = H_SUCCESS; /* => don't yield */

	set_bit(ptid, &vc->conferring_threads);
	while ((get_tb() < stop) && !VCORE_IS_EXITING(vc)) {
		threads_running = VCORE_ENTRY_MAP(vc);
		threads_ceded = vc->napping_threads;
		threads_conferring = vc->conferring_threads;
		if ((threads_ceded | threads_conferring) == threads_running) {
			rv = H_TOO_HARD; /* => do yield */
			break;
		}
	}
	clear_bit(ptid, &vc->conferring_threads);
	return rv;
}

/*
 * When running HV mode KVM we need to block certain operations while KVM VMs
 * exist in the system. We use a counter of VMs to track this.
 *
 * One of the operations we need to block is onlining of secondaries, so we
 * protect hv_vm_count with get/put_online_cpus().
 */
static atomic_t hv_vm_count;

void kvm_hv_vm_activated(void)
{
	get_online_cpus();
	atomic_inc(&hv_vm_count);
	put_online_cpus();
}
EXPORT_SYMBOL_GPL(kvm_hv_vm_activated);

void kvm_hv_vm_deactivated(void)
{
	get_online_cpus();
	atomic_dec(&hv_vm_count);
	put_online_cpus();
}
EXPORT_SYMBOL_GPL(kvm_hv_vm_deactivated);

bool kvm_hv_mode_active(void)
{
	return atomic_read(&hv_vm_count) != 0;
}

extern int hcall_real_table[], hcall_real_table_end[];

int kvmppc_hcall_impl_hv_realmode(unsigned long cmd)
{
	cmd /= 4;
	if (cmd < hcall_real_table_end - hcall_real_table &&
	    hcall_real_table[cmd])
		return 1;

	return 0;
}
EXPORT_SYMBOL_GPL(kvmppc_hcall_impl_hv_realmode);

int kvmppc_hwrng_present(void)
{
	return powernv_hwrng_present();
}
EXPORT_SYMBOL_GPL(kvmppc_hwrng_present);

long kvmppc_h_random(struct kvm_vcpu *vcpu)
{
	int r;

	/* Only need to do the expensive mfmsr() on radix */
	if (kvm_is_radix(vcpu->kvm) && (mfmsr() & MSR_IR))
		r = powernv_get_random_long(&vcpu->arch.regs.gpr[4]);
	else
		r = powernv_get_random_real_mode(&vcpu->arch.regs.gpr[4]);
	if (r)
		return H_SUCCESS;

	return H_HARDWARE;
}

/*
 * Send an interrupt or message to another CPU.
 * The caller needs to include any barrier needed to order writes
 * to memory vs. the IPI/message.
 */
void kvmhv_rm_send_ipi(int cpu)
{
	void __iomem *xics_phys;
	unsigned long msg = PPC_DBELL_TYPE(PPC_DBELL_SERVER);

	/* For a nested hypervisor, use the XICS via hcall */
	if (kvmhv_on_pseries()) {
		unsigned long retbuf[PLPAR_HCALL_BUFSIZE];

		plpar_hcall_raw(H_IPI, retbuf, get_hard_smp_processor_id(cpu),
				IPI_PRIORITY);
		return;
	}

	/* On POWER9 we can use msgsnd for any destination cpu. */
	if (cpu_has_feature(CPU_FTR_ARCH_300)) {
		msg |= get_hard_smp_processor_id(cpu);
		__asm__ __volatile__ (PPC_MSGSND(%0) : : "r" (msg));
		return;
	}

	/* On POWER8 for IPIs to threads in the same core, use msgsnd. */
	if (cpu_has_feature(CPU_FTR_ARCH_207S) &&
	    cpu_first_thread_sibling(cpu) ==
	    cpu_first_thread_sibling(raw_smp_processor_id())) {
		msg |= cpu_thread_in_core(cpu);
		__asm__ __volatile__ (PPC_MSGSND(%0) : : "r" (msg));
		return;
	}

	/* We should never reach this */
	if (WARN_ON_ONCE(xics_on_xive()))
	    return;

	/* Else poke the target with an IPI */
	xics_phys = paca_ptrs[cpu]->kvm_hstate.xics_phys;
	if (xics_phys)
		__raw_rm_writeb(IPI_PRIORITY, xics_phys + XICS_MFRR);
	else
		opal_int_set_mfrr(get_hard_smp_processor_id(cpu), IPI_PRIORITY);
}

/*
 * The following functions are called from the assembly code
 * in book3s_hv_rmhandlers.S.
 */
static void kvmhv_interrupt_vcore(struct kvmppc_vcore *vc, int active)
{
	int cpu = vc->pcpu;

	/* Order setting of exit map vs. msgsnd/IPI */
	smp_mb();
	for (; active; active >>= 1, ++cpu)
		if (active & 1)
			kvmhv_rm_send_ipi(cpu);
}

void kvmhv_commence_exit(int trap)
{
	struct kvmppc_vcore *vc = local_paca->kvm_hstate.kvm_vcore;
	int ptid = local_paca->kvm_hstate.ptid;
	struct kvm_split_mode *sip = local_paca->kvm_hstate.kvm_split_mode;
	int me, ee, i, t;
	int cpu0;

	/* Set our bit in the threads-exiting-guest map in the 0xff00
	   bits of vcore->entry_exit_map */
	me = 0x100 << ptid;
	do {
		ee = vc->entry_exit_map;
	} while (cmpxchg(&vc->entry_exit_map, ee, ee | me) != ee);

	/* Are we the first here? */
	if ((ee >> 8) != 0)
		return;

	/*
	 * Trigger the other threads in this vcore to exit the guest.
	 * If this is a hypervisor decrementer interrupt then they
	 * will be already on their way out of the guest.
	 */
	if (trap != BOOK3S_INTERRUPT_HV_DECREMENTER)
		kvmhv_interrupt_vcore(vc, ee & ~(1 << ptid));

	/*
	 * If we are doing dynamic micro-threading, interrupt the other
	 * subcores to pull them out of their guests too.
	 */
	if (!sip)
		return;

	for (i = 0; i < MAX_SUBCORES; ++i) {
		vc = sip->vc[i];
		if (!vc)
			break;
		do {
			ee = vc->entry_exit_map;
			/* Already asked to exit? */
			if ((ee >> 8) != 0)
				break;
		} while (cmpxchg(&vc->entry_exit_map, ee,
				 ee | VCORE_EXIT_REQ) != ee);
		if ((ee >> 8) == 0)
			kvmhv_interrupt_vcore(vc, ee);
	}

	/*
	 * On POWER9 when running a HPT guest on a radix host (sip != NULL),
	 * we have to interrupt inactive CPU threads to get them to
	 * restore the host LPCR value.
	 */
	if (sip->lpcr_req) {
		if (cmpxchg(&sip->do_restore, 0, 1) == 0) {
			vc = local_paca->kvm_hstate.kvm_vcore;
			cpu0 = vc->pcpu + ptid - local_paca->kvm_hstate.tid;
			for (t = 1; t < threads_per_core; ++t) {
				if (sip->napped[t])
					kvmhv_rm_send_ipi(cpu0 + t);
			}
		}
	}
}

struct kvmppc_host_rm_ops *kvmppc_host_rm_ops_hv;
EXPORT_SYMBOL_GPL(kvmppc_host_rm_ops_hv);

#ifdef CONFIG_KVM_XICS
static struct kvmppc_irq_map *get_irqmap(struct kvmppc_passthru_irqmap *pimap,
					 u32 xisr)
{
	int i;

	/*
	 * We access the mapped array here without a lock.  That
	 * is safe because we never reduce the number of entries
	 * in the array and we never change the v_hwirq field of
	 * an entry once it is set.
	 *
	 * We have also carefully ordered the stores in the writer
	 * and the loads here in the reader, so that if we find a matching
	 * hwirq here, the associated GSI and irq_desc fields are valid.
	 */
	for (i = 0; i < pimap->n_mapped; i++)  {
		if (xisr == pimap->mapped[i].r_hwirq) {
			/*
			 * Order subsequent reads in the caller to serialize
			 * with the writer.
			 */
			smp_rmb();
			return &pimap->mapped[i];
		}
	}
	return NULL;
}

/*
 * If we have an interrupt that's not an IPI, check if we have a
 * passthrough adapter and if so, check if this external interrupt
 * is for the adapter.
 * We will attempt to deliver the IRQ directly to the target VCPU's
 * ICP, the virtual ICP (based on affinity - the xive value in ICS).
 *
 * If the delivery fails or if this is not for a passthrough adapter,
 * return to the host to handle this interrupt. We earlier
 * saved a copy of the XIRR in the PACA, it will be picked up by
 * the host ICP driver.
 */
static int kvmppc_check_passthru(u32 xisr, __be32 xirr, bool *again)
{
	struct kvmppc_passthru_irqmap *pimap;
	struct kvmppc_irq_map *irq_map;
	struct kvm_vcpu *vcpu;

	vcpu = local_paca->kvm_hstate.kvm_vcpu;
	if (!vcpu)
		return 1;
	pimap = kvmppc_get_passthru_irqmap(vcpu->kvm);
	if (!pimap)
		return 1;
	irq_map = get_irqmap(pimap, xisr);
	if (!irq_map)
		return 1;

	/* We're handling this interrupt, generic code doesn't need to */
	local_paca->kvm_hstate.saved_xirr = 0;

	return kvmppc_deliver_irq_passthru(vcpu, xirr, irq_map, pimap, again);
}

#else
static inline int kvmppc_check_passthru(u32 xisr, __be32 xirr, bool *again)
{
	return 1;
}
#endif

/*
 * Determine what sort of external interrupt is pending (if any).
 * Returns:
 *	0 if no interrupt is pending
 *	1 if an interrupt is pending that needs to be handled by the host
 *	2 Passthrough that needs completion in the host
 *	-1 if there was a guest wakeup IPI (which has now been cleared)
 *	-2 if there is PCI passthrough external interrupt that was handled
 */
static long kvmppc_read_one_intr(bool *again);

long kvmppc_read_intr(void)
{
	long ret = 0;
	long rc;
	bool again;

	if (xive_enabled())
		return 1;

	do {
		again = false;
		rc = kvmppc_read_one_intr(&again);
		if (rc && (ret == 0 || rc > ret))
			ret = rc;
	} while (again);
	return ret;
}

static long kvmppc_read_one_intr(bool *again)
{
	void __iomem *xics_phys;
	u32 h_xirr;
	__be32 xirr;
	u32 xisr;
	u8 host_ipi;
	int64_t rc;

	if (xive_enabled())
		return 1;

	/* see if a host IPI is pending */
	host_ipi = local_paca->kvm_hstate.host_ipi;
	if (host_ipi)
		return 1;

	/* Now read the interrupt from the ICP */
	if (kvmhv_on_pseries()) {
		unsigned long retbuf[PLPAR_HCALL_BUFSIZE];

		rc = plpar_hcall_raw(H_XIRR, retbuf, 0xFF);
		xirr = cpu_to_be32(retbuf[0]);
	} else {
		xics_phys = local_paca->kvm_hstate.xics_phys;
		rc = 0;
		if (!xics_phys)
			rc = opal_int_get_xirr(&xirr, false);
		else
			xirr = __raw_rm_readl(xics_phys + XICS_XIRR);
	}
	if (rc < 0)
		return 1;

	/*
	 * Save XIRR for later. Since we get control in reverse endian
	 * on LE systems, save it byte reversed and fetch it back in
	 * host endian. Note that xirr is the value read from the
	 * XIRR register, while h_xirr is the host endian version.
	 */
	h_xirr = be32_to_cpu(xirr);
	local_paca->kvm_hstate.saved_xirr = h_xirr;
	xisr = h_xirr & 0xffffff;
	/*
	 * Ensure that the store/load complete to guarantee all side
	 * effects of loading from XIRR has completed
	 */
	smp_mb();

	/* if nothing pending in the ICP */
	if (!xisr)
		return 0;

	/* We found something in the ICP...
	 *
	 * If it is an IPI, clear the MFRR and EOI it.
	 */
	if (xisr == XICS_IPI) {
		rc = 0;
		if (kvmhv_on_pseries()) {
			unsigned long retbuf[PLPAR_HCALL_BUFSIZE];

			plpar_hcall_raw(H_IPI, retbuf,
					hard_smp_processor_id(), 0xff);
			plpar_hcall_raw(H_EOI, retbuf, h_xirr);
		} else if (xics_phys) {
			__raw_rm_writeb(0xff, xics_phys + XICS_MFRR);
			__raw_rm_writel(xirr, xics_phys + XICS_XIRR);
		} else {
			opal_int_set_mfrr(hard_smp_processor_id(), 0xff);
			rc = opal_int_eoi(h_xirr);
		}
		/* If rc > 0, there is another interrupt pending */
		*again = rc > 0;

		/*
		 * Need to ensure side effects of above stores
		 * complete before proceeding.
		 */
		smp_mb();

		/*
		 * We need to re-check host IPI now in case it got set in the
		 * meantime. If it's clear, we bounce the interrupt to the
		 * guest
		 */
		host_ipi = local_paca->kvm_hstate.host_ipi;
		if (unlikely(host_ipi != 0)) {
			/* We raced with the host,
			 * we need to resend that IPI, bummer
			 */
			if (kvmhv_on_pseries()) {
				unsigned long retbuf[PLPAR_HCALL_BUFSIZE];

				plpar_hcall_raw(H_IPI, retbuf,
						hard_smp_processor_id(),
						IPI_PRIORITY);
			} else if (xics_phys)
				__raw_rm_writeb(IPI_PRIORITY,
						xics_phys + XICS_MFRR);
			else
				opal_int_set_mfrr(hard_smp_processor_id(),
						  IPI_PRIORITY);
			/* Let side effects complete */
			smp_mb();
			return 1;
		}

		/* OK, it's an IPI for us */
		local_paca->kvm_hstate.saved_xirr = 0;
		return -1;
	}

	return kvmppc_check_passthru(xisr, xirr, again);
}

#ifdef CONFIG_KVM_XICS
static inline bool is_rm(void)
{
	return !(mfmsr() & MSR_DR);
}

unsigned long kvmppc_rm_h_xirr(struct kvm_vcpu *vcpu)
{
	if (!kvmppc_xics_enabled(vcpu))
		return H_TOO_HARD;
	if (xics_on_xive()) {
		if (is_rm())
			return xive_rm_h_xirr(vcpu);
		if (unlikely(!__xive_vm_h_xirr))
			return H_NOT_AVAILABLE;
		return __xive_vm_h_xirr(vcpu);
	} else
		return xics_rm_h_xirr(vcpu);
}

unsigned long kvmppc_rm_h_xirr_x(struct kvm_vcpu *vcpu)
{
	if (!kvmppc_xics_enabled(vcpu))
		return H_TOO_HARD;
	vcpu->arch.regs.gpr[5] = get_tb();
	if (xics_on_xive()) {
		if (is_rm())
			return xive_rm_h_xirr(vcpu);
		if (unlikely(!__xive_vm_h_xirr))
			return H_NOT_AVAILABLE;
		return __xive_vm_h_xirr(vcpu);
	} else
		return xics_rm_h_xirr(vcpu);
}

unsigned long kvmppc_rm_h_ipoll(struct kvm_vcpu *vcpu, unsigned long server)
{
	if (!kvmppc_xics_enabled(vcpu))
		return H_TOO_HARD;
	if (xics_on_xive()) {
		if (is_rm())
			return xive_rm_h_ipoll(vcpu, server);
		if (unlikely(!__xive_vm_h_ipoll))
			return H_NOT_AVAILABLE;
		return __xive_vm_h_ipoll(vcpu, server);
	} else
		return H_TOO_HARD;
}

int kvmppc_rm_h_ipi(struct kvm_vcpu *vcpu, unsigned long server,
		    unsigned long mfrr)
{
	if (!kvmppc_xics_enabled(vcpu))
		return H_TOO_HARD;
	if (xics_on_xive()) {
		if (is_rm())
			return xive_rm_h_ipi(vcpu, server, mfrr);
		if (unlikely(!__xive_vm_h_ipi))
			return H_NOT_AVAILABLE;
		return __xive_vm_h_ipi(vcpu, server, mfrr);
	} else
		return xics_rm_h_ipi(vcpu, server, mfrr);
}

int kvmppc_rm_h_cppr(struct kvm_vcpu *vcpu, unsigned long cppr)
{
	if (!kvmppc_xics_enabled(vcpu))
		return H_TOO_HARD;
	if (xics_on_xive()) {
		if (is_rm())
			return xive_rm_h_cppr(vcpu, cppr);
		if (unlikely(!__xive_vm_h_cppr))
			return H_NOT_AVAILABLE;
		return __xive_vm_h_cppr(vcpu, cppr);
	} else
		return xics_rm_h_cppr(vcpu, cppr);
}

int kvmppc_rm_h_eoi(struct kvm_vcpu *vcpu, unsigned long xirr)
{
	if (!kvmppc_xics_enabled(vcpu))
		return H_TOO_HARD;
	if (xics_on_xive()) {
		if (is_rm())
			return xive_rm_h_eoi(vcpu, xirr);
		if (unlikely(!__xive_vm_h_eoi))
			return H_NOT_AVAILABLE;
		return __xive_vm_h_eoi(vcpu, xirr);
	} else
		return xics_rm_h_eoi(vcpu, xirr);
}
#endif /* CONFIG_KVM_XICS */

void kvmppc_bad_interrupt(struct pt_regs *regs)
{
	/*
	 * 100 could happen at any time, 200 can happen due to invalid real
	 * address access for example (or any time due to a hardware problem).
	 */
	if (TRAP(regs) == 0x100) {
		get_paca()->in_nmi++;
		system_reset_exception(regs);
		get_paca()->in_nmi--;
	} else if (TRAP(regs) == 0x200) {
		machine_check_exception(regs);
	} else {
		die("Bad interrupt in KVM entry/exit code", regs, SIGABRT);
	}
	panic("Bad KVM trap");
}

/*
 * Functions used to switch LPCR HR and UPRT bits on all threads
 * when entering and exiting HPT guests on a radix host.
 */

#define PHASE_REALMODE		1	/* in real mode */
#define PHASE_SET_LPCR		2	/* have set LPCR */
#define PHASE_OUT_OF_GUEST	4	/* have finished executing in guest */
#define PHASE_RESET_LPCR	8	/* have reset LPCR to host value */

#define ALL(p)		(((p) << 24) | ((p) << 16) | ((p) << 8) | (p))

static void wait_for_sync(struct kvm_split_mode *sip, int phase)
{
	int thr = local_paca->kvm_hstate.tid;

	sip->lpcr_sync.phase[thr] |= phase;
	phase = ALL(phase);
	while ((sip->lpcr_sync.allphases & phase) != phase) {
		HMT_low();
		barrier();
	}
	HMT_medium();
}

void kvmhv_p9_set_lpcr(struct kvm_split_mode *sip)
{
	unsigned long rb, set;

	/* wait for every other thread to get to real mode */
	wait_for_sync(sip, PHASE_REALMODE);

	/* Set LPCR and LPIDR */
	mtspr(SPRN_LPCR, sip->lpcr_req);
	mtspr(SPRN_LPID, sip->lpidr_req);
	isync();

	/* Invalidate the TLB on thread 0 */
	if (local_paca->kvm_hstate.tid == 0) {
		sip->do_set = 0;
		asm volatile("ptesync" : : : "memory");
		for (set = 0; set < POWER9_TLB_SETS_RADIX; ++set) {
			rb = TLBIEL_INVAL_SET_LPID +
				(set << TLBIEL_INVAL_SET_SHIFT);
			asm volatile(PPC_TLBIEL(%0, %1, 0, 0, 0) : :
				     "r" (rb), "r" (0));
		}
		asm volatile("ptesync" : : : "memory");
	}

	/* indicate that we have done so and wait for others */
	wait_for_sync(sip, PHASE_SET_LPCR);
	/* order read of sip->lpcr_sync.allphases vs. sip->do_set */
	smp_rmb();
}

/*
 * Called when a thread that has been in the guest needs
 * to reload the host LPCR value - but only on POWER9 when
 * running a HPT guest on a radix host.
 */
void kvmhv_p9_restore_lpcr(struct kvm_split_mode *sip)
{
	/* we're out of the guest... */
	wait_for_sync(sip, PHASE_OUT_OF_GUEST);

	mtspr(SPRN_LPID, 0);
	mtspr(SPRN_LPCR, sip->host_lpcr);
	isync();

	if (local_paca->kvm_hstate.tid == 0) {
		sip->do_restore = 0;
		smp_wmb();	/* order store of do_restore vs. phase */
	}

	wait_for_sync(sip, PHASE_RESET_LPCR);
	smp_mb();
	local_paca->kvm_hstate.kvm_split_mode = NULL;
}

/*
 * Is there a PRIV_DOORBELL pending for the guest (on POWER9)?
 * Can we inject a Decrementer or a External interrupt?
 */
void kvmppc_guest_entry_inject_int(struct kvm_vcpu *vcpu)
{
	int ext;
	unsigned long vec = 0;
	unsigned long lpcr;

	/* Insert EXTERNAL bit into LPCR at the MER bit position */
	ext = (vcpu->arch.pending_exceptions >> BOOK3S_IRQPRIO_EXTERNAL) & 1;
	lpcr = mfspr(SPRN_LPCR);
	lpcr |= ext << LPCR_MER_SH;
	mtspr(SPRN_LPCR, lpcr);
	isync();

	if (vcpu->arch.shregs.msr & MSR_EE) {
		if (ext) {
			vec = BOOK3S_INTERRUPT_EXTERNAL;
		} else {
			long int dec = mfspr(SPRN_DEC);
			if (!(lpcr & LPCR_LD))
				dec = (int) dec;
			if (dec < 0)
				vec = BOOK3S_INTERRUPT_DECREMENTER;
		}
	}
	if (vec) {
		unsigned long msr, old_msr = vcpu->arch.shregs.msr;

		kvmppc_set_srr0(vcpu, kvmppc_get_pc(vcpu));
		kvmppc_set_srr1(vcpu, old_msr);
		kvmppc_set_pc(vcpu, vec);
		msr = vcpu->arch.intr_msr;
		if (MSR_TM_ACTIVE(old_msr))
			msr |= MSR_TS_S;
		vcpu->arch.shregs.msr = msr;
	}

	if (vcpu->arch.doorbell_request) {
		mtspr(SPRN_DPDES, 1);
		vcpu->arch.vcore->dpdes = 1;
		smp_wmb();
		vcpu->arch.doorbell_request = 0;
	}
}

static void flush_guest_tlb(struct kvm *kvm)
{
	unsigned long rb, set;

	rb = PPC_BIT(52);	/* IS = 2 */
	if (kvm_is_radix(kvm)) {
		/* R=1 PRS=1 RIC=2 */
		asm volatile(PPC_TLBIEL(%0, %4, %3, %2, %1)
			     : : "r" (rb), "i" (1), "i" (1), "i" (2),
			       "r" (0) : "memory");
		for (set = 1; set < kvm->arch.tlb_sets; ++set) {
			rb += PPC_BIT(51);	/* increment set number */
			/* R=1 PRS=1 RIC=0 */
			asm volatile(PPC_TLBIEL(%0, %4, %3, %2, %1)
				     : : "r" (rb), "i" (1), "i" (1), "i" (0),
				       "r" (0) : "memory");
		}
	} else {
		for (set = 0; set < kvm->arch.tlb_sets; ++set) {
			/* R=0 PRS=0 RIC=0 */
			asm volatile(PPC_TLBIEL(%0, %4, %3, %2, %1)
				     : : "r" (rb), "i" (0), "i" (0), "i" (0),
				       "r" (0) : "memory");
			rb += PPC_BIT(51);	/* increment set number */
		}
	}
	asm volatile("ptesync": : :"memory");
}

void kvmppc_check_need_tlb_flush(struct kvm *kvm, int pcpu,
				 struct kvm_nested_guest *nested)
{
	cpumask_t *need_tlb_flush;

	/*
	 * On POWER9, individual threads can come in here, but the
	 * TLB is shared between the 4 threads in a core, hence
	 * invalidating on one thread invalidates for all.
	 * Thus we make all 4 threads use the same bit.
	 */
	if (cpu_has_feature(CPU_FTR_ARCH_300))
		pcpu = cpu_first_thread_sibling(pcpu);

	if (nested)
		need_tlb_flush = &nested->need_tlb_flush;
	else
		need_tlb_flush = &kvm->arch.need_tlb_flush;

	if (cpumask_test_cpu(pcpu, need_tlb_flush)) {
		flush_guest_tlb(kvm);

		/* Clear the bit after the TLB flush */
		cpumask_clear_cpu(pcpu, need_tlb_flush);
	}
}
EXPORT_SYMBOL_GPL(kvmppc_check_need_tlb_flush);
