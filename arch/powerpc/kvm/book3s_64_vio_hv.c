/*
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License, version 2, as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 *
 * Copyright 2010 Paul Mackerras, IBM Corp. <paulus@au1.ibm.com>
 * Copyright 2011 David Gibson, IBM Corporation <dwg@au1.ibm.com>
 */

#include <linux/types.h>
#include <linux/string.h>
#include <linux/kvm.h>
#include <linux/kvm_host.h>
#include <linux/highmem.h>
#include <linux/gfp.h>
#include <linux/slab.h>
#include <linux/hugetlb.h>
#include <linux/list.h>

#include <asm/tlbflush.h>
#include <asm/kvm_ppc.h>
#include <asm/kvm_book3s.h>
#include <asm/mmu-hash64.h>
#include <asm/hvcall.h>
#include <asm/synch.h>
#include <asm/ppc-opcode.h>
#include <asm/kvm_host.h>
#include <asm/udbg.h>
#include <asm/iommu.h>

#define TCES_PER_PAGE	(PAGE_SIZE / sizeof(u64))

/*
 * Finds a TCE table descriptor by LIOBN.
 *
 * WARNING: This will be called in real or virtual mode on HV KVM and virtual
 *          mode on PR KVM
 */
static struct kvmppc_spapr_tce_table *kvmppc_find_table(struct kvm_vcpu *vcpu,
		unsigned long liobn)
{
	struct kvm *kvm = vcpu->kvm;
	struct kvmppc_spapr_tce_table *stt;

	list_for_each_entry_lockless(stt, &kvm->arch.spapr_tce_tables, list)
		if (stt->liobn == liobn)
			return stt;

	return NULL;
}

/*
 * Validates IO address.
 *
 * WARNING: This will be called in real-mode on HV KVM and virtual
 *          mode on PR KVM
 */
static long kvmppc_ioba_validate(struct kvmppc_spapr_tce_table *stt,
		unsigned long ioba, unsigned long npages)
{
	unsigned long mask = (1ULL << IOMMU_PAGE_SHIFT_4K) - 1;
	unsigned long idx = ioba >> IOMMU_PAGE_SHIFT_4K;
	unsigned long size = stt->window_size >> IOMMU_PAGE_SHIFT_4K;

	if ((ioba & mask) || (idx + npages > size) || (idx + npages < idx))
		return H_PARAMETER;

	return H_SUCCESS;
}

/* WARNING: This will be called in real-mode on HV KVM and virtual
 *          mode on PR KVM
 */
long kvmppc_h_put_tce(struct kvm_vcpu *vcpu, unsigned long liobn,
		      unsigned long ioba, unsigned long tce)
{
	struct kvmppc_spapr_tce_table *stt = kvmppc_find_table(vcpu, liobn);
	long ret;
	unsigned long idx;
	struct page *page;
	u64 *tbl;

	/* udbg_printf("H_PUT_TCE(): liobn=0x%lx ioba=0x%lx, tce=0x%lx\n", */
	/* 	    liobn, ioba, tce); */

	if (!stt)
		return H_TOO_HARD;

	ret = kvmppc_ioba_validate(stt, ioba, 1);
	if (ret != H_SUCCESS)
		return ret;

	idx = ioba >> IOMMU_PAGE_SHIFT_4K;
	page = stt->pages[idx / TCES_PER_PAGE];
	tbl = (u64 *)page_address(page);

	/* FIXME: Need to validate the TCE itself */
	/* udbg_printf("tce @ %p\n", &tbl[idx % TCES_PER_PAGE]); */
	tbl[idx % TCES_PER_PAGE] = tce;

	return H_SUCCESS;
}
EXPORT_SYMBOL_GPL(kvmppc_h_put_tce);

long kvmppc_h_get_tce(struct kvm_vcpu *vcpu, unsigned long liobn,
		      unsigned long ioba)
{
	struct kvmppc_spapr_tce_table *stt = kvmppc_find_table(vcpu, liobn);
	long ret;
	unsigned long idx;
	struct page *page;
	u64 *tbl;

	if (!stt)
		return H_TOO_HARD;

	ret = kvmppc_ioba_validate(stt, ioba, 1);
	if (ret != H_SUCCESS)
		return ret;

	idx = ioba >> IOMMU_PAGE_SHIFT_4K;
	page = stt->pages[idx / TCES_PER_PAGE];
	tbl = (u64 *)page_address(page);

	vcpu->arch.gpr[4] = tbl[idx % TCES_PER_PAGE];

	return H_SUCCESS;
}
EXPORT_SYMBOL_GPL(kvmppc_h_get_tce);
