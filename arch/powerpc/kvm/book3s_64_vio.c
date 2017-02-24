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
 * Copyright 2016 Alexey Kardashevskiy, IBM Corporation <aik@au1.ibm.com>
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
#include <linux/anon_inodes.h>

#include <asm/tlbflush.h>
#include <asm/kvm_ppc.h>
#include <asm/kvm_book3s.h>
#include <asm/book3s/64/mmu-hash.h>
#include <asm/hvcall.h>
#include <asm/synch.h>
#include <asm/ppc-opcode.h>
#include <asm/kvm_host.h>
#include <asm/udbg.h>
#include <asm/iommu.h>
#include <asm/tce.h>

static unsigned long kvmppc_tce_pages(unsigned long iommu_pages)
{
	return ALIGN(iommu_pages * sizeof(u64), PAGE_SIZE) / PAGE_SIZE;
}

static unsigned long kvmppc_stt_pages(unsigned long tce_pages)
{
	unsigned long stt_bytes = sizeof(struct kvmppc_spapr_tce_table) +
			(tce_pages * sizeof(struct page *));

	return tce_pages + ALIGN(stt_bytes, PAGE_SIZE) / PAGE_SIZE;
}

static long kvmppc_account_memlimit(unsigned long stt_pages, bool inc)
{
	long ret = 0;

	if (!current || !current->mm)
		return ret; /* process exited */

	down_write(&current->mm->mmap_sem);

	if (inc) {
		unsigned long locked, lock_limit;

		locked = current->mm->locked_vm + stt_pages;
		lock_limit = rlimit(RLIMIT_MEMLOCK) >> PAGE_SHIFT;
		if (locked > lock_limit && !capable(CAP_IPC_LOCK))
			ret = -ENOMEM;
		else
			current->mm->locked_vm += stt_pages;
	} else {
		if (WARN_ON_ONCE(stt_pages > current->mm->locked_vm))
			stt_pages = current->mm->locked_vm;

		current->mm->locked_vm -= stt_pages;
	}

	pr_debug("[%d] RLIMIT_MEMLOCK KVM %c%ld %ld/%ld%s\n", current->pid,
			inc ? '+' : '-',
			stt_pages << PAGE_SHIFT,
			current->mm->locked_vm << PAGE_SHIFT,
			rlimit(RLIMIT_MEMLOCK),
			ret ? " - exceeded" : "");

	up_write(&current->mm->mmap_sem);

	return ret;
}

static void release_spapr_tce_table(struct rcu_head *head)
{
	struct kvmppc_spapr_tce_table *stt = container_of(head,
			struct kvmppc_spapr_tce_table, rcu);
	unsigned long i, npages = kvmppc_tce_pages(stt->size);

	for (i = 0; i < npages; i++)
		__free_page(stt->pages[i]);

	kfree(stt);
}

static int kvm_spapr_tce_fault(struct vm_fault *vmf)
{
	struct kvmppc_spapr_tce_table *stt = vmf->vma->vm_file->private_data;
	struct page *page;

	if (vmf->pgoff >= kvmppc_tce_pages(stt->size))
		return VM_FAULT_SIGBUS;

	page = stt->pages[vmf->pgoff];
	get_page(page);
	vmf->page = page;
	return 0;
}

static const struct vm_operations_struct kvm_spapr_tce_vm_ops = {
	.fault = kvm_spapr_tce_fault,
};

static int kvm_spapr_tce_mmap(struct file *file, struct vm_area_struct *vma)
{
	vma->vm_ops = &kvm_spapr_tce_vm_ops;
	return 0;
}

static int kvm_spapr_tce_release(struct inode *inode, struct file *filp)
{
	struct kvmppc_spapr_tce_table *stt = filp->private_data;

	list_del_rcu(&stt->list);

	kvm_put_kvm(stt->kvm);

	kvmppc_account_memlimit(
		kvmppc_stt_pages(kvmppc_tce_pages(stt->size)), false);
	call_rcu(&stt->rcu, release_spapr_tce_table);

	return 0;
}

static const struct file_operations kvm_spapr_tce_fops = {
	.mmap           = kvm_spapr_tce_mmap,
	.release	= kvm_spapr_tce_release,
};

long kvm_vm_ioctl_create_spapr_tce(struct kvm *kvm,
				   struct kvm_create_spapr_tce_64 *args)
{
	struct kvmppc_spapr_tce_table *stt = NULL;
	unsigned long npages, size;
	int ret = -ENOMEM;
	int i;

	if (!args->size)
		return -EINVAL;

	/* Check this LIOBN hasn't been previously allocated */
	list_for_each_entry(stt, &kvm->arch.spapr_tce_tables, list) {
		if (stt->liobn == args->liobn)
			return -EBUSY;
	}

	size = args->size;
	npages = kvmppc_tce_pages(size);
	ret = kvmppc_account_memlimit(kvmppc_stt_pages(npages), true);
	if (ret) {
		stt = NULL;
		goto fail;
	}

	ret = -ENOMEM;
	stt = kzalloc(sizeof(*stt) + npages * sizeof(struct page *),
		      GFP_KERNEL);
	if (!stt)
		goto fail;

	stt->liobn = args->liobn;
	stt->page_shift = args->page_shift;
	stt->offset = args->offset;
	stt->size = size;
	stt->kvm = kvm;

	for (i = 0; i < npages; i++) {
		stt->pages[i] = alloc_page(GFP_KERNEL | __GFP_ZERO);
		if (!stt->pages[i])
			goto fail;
	}

	kvm_get_kvm(kvm);

	mutex_lock(&kvm->lock);
	list_add_rcu(&stt->list, &kvm->arch.spapr_tce_tables);

	mutex_unlock(&kvm->lock);

	return anon_inode_getfd("kvm-spapr-tce", &kvm_spapr_tce_fops,
				stt, O_RDWR | O_CLOEXEC);

fail:
	if (stt) {
		for (i = 0; i < npages; i++)
			if (stt->pages[i])
				__free_page(stt->pages[i]);

		kfree(stt);
	}
	return ret;
}

long kvmppc_h_put_tce(struct kvm_vcpu *vcpu, unsigned long liobn,
		      unsigned long ioba, unsigned long tce)
{
	struct kvmppc_spapr_tce_table *stt = kvmppc_find_table(vcpu, liobn);
	long ret;

	/* udbg_printf("H_PUT_TCE(): liobn=0x%lx ioba=0x%lx, tce=0x%lx\n", */
	/* 	    liobn, ioba, tce); */

	if (!stt)
		return H_TOO_HARD;

	ret = kvmppc_ioba_validate(stt, ioba, 1);
	if (ret != H_SUCCESS)
		return ret;

	ret = kvmppc_tce_validate(stt, tce);
	if (ret != H_SUCCESS)
		return ret;

	kvmppc_tce_put(stt, ioba >> stt->page_shift, tce);

	return H_SUCCESS;
}
EXPORT_SYMBOL_GPL(kvmppc_h_put_tce);

long kvmppc_h_put_tce_indirect(struct kvm_vcpu *vcpu,
		unsigned long liobn, unsigned long ioba,
		unsigned long tce_list, unsigned long npages)
{
	struct kvmppc_spapr_tce_table *stt;
	long i, ret = H_SUCCESS, idx;
	unsigned long entry, ua = 0;
	u64 __user *tces;
	u64 tce;

	stt = kvmppc_find_table(vcpu, liobn);
	if (!stt)
		return H_TOO_HARD;

	entry = ioba >> stt->page_shift;
	/*
	 * SPAPR spec says that the maximum size of the list is 512 TCEs
	 * so the whole table fits in 4K page
	 */
	if (npages > 512)
		return H_PARAMETER;

	if (tce_list & (SZ_4K - 1))
		return H_PARAMETER;

	ret = kvmppc_ioba_validate(stt, ioba, npages);
	if (ret != H_SUCCESS)
		return ret;

	idx = srcu_read_lock(&vcpu->kvm->srcu);
	if (kvmppc_gpa_to_ua(vcpu->kvm, tce_list, &ua, NULL)) {
		ret = H_TOO_HARD;
		goto unlock_exit;
	}
	tces = (u64 __user *) ua;

	for (i = 0; i < npages; ++i) {
		if (get_user(tce, tces + i)) {
			ret = H_TOO_HARD;
			goto unlock_exit;
		}
		tce = be64_to_cpu(tce);

		ret = kvmppc_tce_validate(stt, tce);
		if (ret != H_SUCCESS)
			goto unlock_exit;

		kvmppc_tce_put(stt, entry + i, tce);
	}

unlock_exit:
	srcu_read_unlock(&vcpu->kvm->srcu, idx);

	return ret;
}
EXPORT_SYMBOL_GPL(kvmppc_h_put_tce_indirect);

long kvmppc_h_stuff_tce(struct kvm_vcpu *vcpu,
		unsigned long liobn, unsigned long ioba,
		unsigned long tce_value, unsigned long npages)
{
	struct kvmppc_spapr_tce_table *stt;
	long i, ret;

	stt = kvmppc_find_table(vcpu, liobn);
	if (!stt)
		return H_TOO_HARD;

	ret = kvmppc_ioba_validate(stt, ioba, npages);
	if (ret != H_SUCCESS)
		return ret;

	/* Check permission bits only to allow userspace poison TCE for debug */
	if (tce_value & (TCE_PCI_WRITE | TCE_PCI_READ))
		return H_PARAMETER;

	for (i = 0; i < npages; ++i, ioba += (1ULL << stt->page_shift))
		kvmppc_tce_put(stt, ioba >> stt->page_shift, tce_value);

	return H_SUCCESS;
}
EXPORT_SYMBOL_GPL(kvmppc_h_stuff_tce);
