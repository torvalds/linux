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

#include <asm/cputable.h>
#include <asm/kvm_ppc.h>
#include <asm/kvm_book3s.h>

#define KVM_CMA_CHUNK_ORDER	18

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

struct page *kvm_alloc_hpt(unsigned long nr_pages)
{
	VM_BUG_ON(order_base_2(nr_pages) < KVM_CMA_CHUNK_ORDER - PAGE_SHIFT);

	return cma_alloc(kvm_cma, nr_pages, order_base_2(HPT_ALIGN_PAGES));
}
EXPORT_SYMBOL_GPL(kvm_alloc_hpt);

void kvm_release_hpt(struct page *page, unsigned long nr_pages)
{
	cma_release(kvm_cma, page, nr_pages);
}
EXPORT_SYMBOL_GPL(kvm_release_hpt);

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
			KVM_CMA_CHUNK_ORDER - PAGE_SHIFT, false, &kvm_cma);
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
	struct kvmppc_vcore *vc = vcpu->arch.vcore;
	int threads_running;
	int threads_ceded;
	int threads_conferring;
	u64 stop = get_tb() + 10 * tb_ticks_per_usec;
	int rv = H_SUCCESS; /* => don't yield */

	set_bit(vcpu->arch.ptid, &vc->conferring_threads);
	while ((get_tb() < stop) && (VCORE_EXIT_COUNT(vc) == 0)) {
		threads_running = VCORE_ENTRY_COUNT(vc);
		threads_ceded = hweight32(vc->napping_threads);
		threads_conferring = hweight32(vc->conferring_threads);
		if (threads_ceded + threads_conferring >= threads_running) {
			rv = H_TOO_HARD; /* => do yield */
			break;
		}
	}
	clear_bit(vcpu->arch.ptid, &vc->conferring_threads);
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
