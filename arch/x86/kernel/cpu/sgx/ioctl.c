// SPDX-License-Identifier: GPL-2.0
/*  Copyright(c) 2016-20 Intel Corporation. */

#include <asm/mman.h>
#include <linux/mman.h>
#include <linux/delay.h>
#include <linux/file.h>
#include <linux/hashtable.h>
#include <linux/highmem.h>
#include <linux/ratelimit.h>
#include <linux/sched/signal.h>
#include <linux/shmem_fs.h>
#include <linux/slab.h>
#include <linux/suspend.h>
#include "driver.h"
#include "encl.h"
#include "encls.h"

static int sgx_encl_create(struct sgx_encl *encl, struct sgx_secs *secs)
{
	struct sgx_epc_page *secs_epc;
	struct sgx_pageinfo pginfo;
	struct sgx_secinfo secinfo;
	unsigned long encl_size;
	long ret;

	/* The extra page goes to SECS. */
	encl_size = secs->size + PAGE_SIZE;

	secs_epc = __sgx_alloc_epc_page();
	if (IS_ERR(secs_epc))
		return PTR_ERR(secs_epc);

	encl->secs.epc_page = secs_epc;

	pginfo.addr = 0;
	pginfo.contents = (unsigned long)secs;
	pginfo.metadata = (unsigned long)&secinfo;
	pginfo.secs = 0;
	memset(&secinfo, 0, sizeof(secinfo));

	ret = __ecreate((void *)&pginfo, sgx_get_epc_virt_addr(secs_epc));
	if (ret) {
		ret = -EIO;
		goto err_out;
	}

	if (secs->attributes & SGX_ATTR_DEBUG)
		set_bit(SGX_ENCL_DEBUG, &encl->flags);

	encl->secs.encl = encl;
	encl->base = secs->base;
	encl->size = secs->size;

	/* Set only after completion, as encl->lock has not been taken. */
	set_bit(SGX_ENCL_CREATED, &encl->flags);

	return 0;

err_out:
	sgx_free_epc_page(encl->secs.epc_page);
	encl->secs.epc_page = NULL;

	return ret;
}

/**
 * sgx_ioc_enclave_create() - handler for %SGX_IOC_ENCLAVE_CREATE
 * @encl:	An enclave pointer.
 * @arg:	The ioctl argument.
 *
 * Allocate kernel data structures for the enclave and invoke ECREATE.
 *
 * Return:
 * - 0:		Success.
 * - -EIO:	ECREATE failed.
 * - -errno:	POSIX error.
 */
static long sgx_ioc_enclave_create(struct sgx_encl *encl, void __user *arg)
{
	struct sgx_enclave_create create_arg;
	void *secs;
	int ret;

	if (test_bit(SGX_ENCL_CREATED, &encl->flags))
		return -EINVAL;

	if (copy_from_user(&create_arg, arg, sizeof(create_arg)))
		return -EFAULT;

	secs = kmalloc(PAGE_SIZE, GFP_KERNEL);
	if (!secs)
		return -ENOMEM;

	if (copy_from_user(secs, (void __user *)create_arg.src, PAGE_SIZE))
		ret = -EFAULT;
	else
		ret = sgx_encl_create(encl, secs);

	kfree(secs);
	return ret;
}

static struct sgx_encl_page *sgx_encl_page_alloc(struct sgx_encl *encl,
						 unsigned long offset,
						 u64 secinfo_flags)
{
	struct sgx_encl_page *encl_page;
	unsigned long prot;

	encl_page = kzalloc(sizeof(*encl_page), GFP_KERNEL);
	if (!encl_page)
		return ERR_PTR(-ENOMEM);

	encl_page->desc = encl->base + offset;
	encl_page->encl = encl;

	prot = _calc_vm_trans(secinfo_flags, SGX_SECINFO_R, PROT_READ)  |
	       _calc_vm_trans(secinfo_flags, SGX_SECINFO_W, PROT_WRITE) |
	       _calc_vm_trans(secinfo_flags, SGX_SECINFO_X, PROT_EXEC);

	/*
	 * TCS pages must always RW set for CPU access while the SECINFO
	 * permissions are *always* zero - the CPU ignores the user provided
	 * values and silently overwrites them with zero permissions.
	 */
	if ((secinfo_flags & SGX_SECINFO_PAGE_TYPE_MASK) == SGX_SECINFO_TCS)
		prot |= PROT_READ | PROT_WRITE;

	/* Calculate maximum of the VM flags for the page. */
	encl_page->vm_max_prot_bits = calc_vm_prot_bits(prot, 0);

	return encl_page;
}

static int sgx_validate_secinfo(struct sgx_secinfo *secinfo)
{
	u64 perm = secinfo->flags & SGX_SECINFO_PERMISSION_MASK;
	u64 pt   = secinfo->flags & SGX_SECINFO_PAGE_TYPE_MASK;

	if (pt != SGX_SECINFO_REG && pt != SGX_SECINFO_TCS)
		return -EINVAL;

	if ((perm & SGX_SECINFO_W) && !(perm & SGX_SECINFO_R))
		return -EINVAL;

	/*
	 * CPU will silently overwrite the permissions as zero, which means
	 * that we need to validate it ourselves.
	 */
	if (pt == SGX_SECINFO_TCS && perm)
		return -EINVAL;

	if (secinfo->flags & SGX_SECINFO_RESERVED_MASK)
		return -EINVAL;

	if (memchr_inv(secinfo->reserved, 0, sizeof(secinfo->reserved)))
		return -EINVAL;

	return 0;
}

static int __sgx_encl_add_page(struct sgx_encl *encl,
			       struct sgx_encl_page *encl_page,
			       struct sgx_epc_page *epc_page,
			       struct sgx_secinfo *secinfo, unsigned long src)
{
	struct sgx_pageinfo pginfo;
	struct vm_area_struct *vma;
	struct page *src_page;
	int ret;

	/* Deny noexec. */
	vma = find_vma(current->mm, src);
	if (!vma)
		return -EFAULT;

	if (!(vma->vm_flags & VM_MAYEXEC))
		return -EACCES;

	ret = get_user_pages(src, 1, 0, &src_page, NULL);
	if (ret < 1)
		return -EFAULT;

	pginfo.secs = (unsigned long)sgx_get_epc_virt_addr(encl->secs.epc_page);
	pginfo.addr = encl_page->desc & PAGE_MASK;
	pginfo.metadata = (unsigned long)secinfo;
	pginfo.contents = (unsigned long)kmap_atomic(src_page);

	ret = __eadd(&pginfo, sgx_get_epc_virt_addr(epc_page));

	kunmap_atomic((void *)pginfo.contents);
	put_page(src_page);

	return ret ? -EIO : 0;
}

/*
 * If the caller requires measurement of the page as a proof for the content,
 * use EEXTEND to add a measurement for 256 bytes of the page. Repeat this
 * operation until the entire page is measured."
 */
static int __sgx_encl_extend(struct sgx_encl *encl,
			     struct sgx_epc_page *epc_page)
{
	unsigned long offset;
	int ret;

	for (offset = 0; offset < PAGE_SIZE; offset += SGX_EEXTEND_BLOCK_SIZE) {
		ret = __eextend(sgx_get_epc_virt_addr(encl->secs.epc_page),
				sgx_get_epc_virt_addr(epc_page) + offset);
		if (ret) {
			if (encls_failed(ret))
				ENCLS_WARN(ret, "EEXTEND");

			return -EIO;
		}
	}

	return 0;
}

static int sgx_encl_add_page(struct sgx_encl *encl, unsigned long src,
			     unsigned long offset, struct sgx_secinfo *secinfo,
			     unsigned long flags)
{
	struct sgx_encl_page *encl_page;
	struct sgx_epc_page *epc_page;
	int ret;

	encl_page = sgx_encl_page_alloc(encl, offset, secinfo->flags);
	if (IS_ERR(encl_page))
		return PTR_ERR(encl_page);

	epc_page = __sgx_alloc_epc_page();
	if (IS_ERR(epc_page)) {
		kfree(encl_page);
		return PTR_ERR(epc_page);
	}

	mmap_read_lock(current->mm);
	mutex_lock(&encl->lock);

	/*
	 * Insert prior to EADD in case of OOM.  EADD modifies MRENCLAVE, i.e.
	 * can't be gracefully unwound, while failure on EADD/EXTEND is limited
	 * to userspace errors (or kernel/hardware bugs).
	 */
	ret = xa_insert(&encl->page_array, PFN_DOWN(encl_page->desc),
			encl_page, GFP_KERNEL);
	if (ret)
		goto err_out_unlock;

	ret = __sgx_encl_add_page(encl, encl_page, epc_page, secinfo,
				  src);
	if (ret)
		goto err_out;

	/*
	 * Complete the "add" before doing the "extend" so that the "add"
	 * isn't in a half-baked state in the extremely unlikely scenario
	 * the enclave will be destroyed in response to EEXTEND failure.
	 */
	encl_page->encl = encl;
	encl_page->epc_page = epc_page;
	encl->secs_child_cnt++;

	if (flags & SGX_PAGE_MEASURE) {
		ret = __sgx_encl_extend(encl, epc_page);
		if (ret)
			goto err_out;
	}

	mutex_unlock(&encl->lock);
	mmap_read_unlock(current->mm);
	return ret;

err_out:
	xa_erase(&encl->page_array, PFN_DOWN(encl_page->desc));

err_out_unlock:
	mutex_unlock(&encl->lock);
	mmap_read_unlock(current->mm);

	sgx_free_epc_page(epc_page);
	kfree(encl_page);

	return ret;
}

/**
 * sgx_ioc_enclave_add_pages() - The handler for %SGX_IOC_ENCLAVE_ADD_PAGES
 * @encl:       an enclave pointer
 * @arg:	a user pointer to a struct sgx_enclave_add_pages instance
 *
 * Add one or more pages to an uninitialized enclave, and optionally extend the
 * measurement with the contents of the page. The SECINFO and measurement mask
 * are applied to all pages.
 *
 * A SECINFO for a TCS is required to always contain zero permissions because
 * CPU silently zeros them. Allowing anything else would cause a mismatch in
 * the measurement.
 *
 * mmap()'s protection bits are capped by the page permissions. For each page
 * address, the maximum protection bits are computed with the following
 * heuristics:
 *
 * 1. A regular page: PROT_R, PROT_W and PROT_X match the SECINFO permissions.
 * 2. A TCS page: PROT_R | PROT_W.
 *
 * mmap() is not allowed to surpass the minimum of the maximum protection bits
 * within the given address range.
 *
 * The function deinitializes kernel data structures for enclave and returns
 * -EIO in any of the following conditions:
 *
 * - Enclave Page Cache (EPC), the physical memory holding enclaves, has
 *   been invalidated. This will cause EADD and EEXTEND to fail.
 * - If the source address is corrupted somehow when executing EADD.
 *
 * Return:
 * - 0:		Success.
 * - -EACCES:	The source page is located in a noexec partition.
 * - -ENOMEM:	Out of EPC pages.
 * - -EINTR:	The call was interrupted before data was processed.
 * - -EIO:	Either EADD or EEXTEND failed because invalid source address
 *		or power cycle.
 * - -errno:	POSIX error.
 */
static long sgx_ioc_enclave_add_pages(struct sgx_encl *encl, void __user *arg)
{
	struct sgx_enclave_add_pages add_arg;
	struct sgx_secinfo secinfo;
	unsigned long c;
	int ret;

	if (!test_bit(SGX_ENCL_CREATED, &encl->flags))
		return -EINVAL;

	if (copy_from_user(&add_arg, arg, sizeof(add_arg)))
		return -EFAULT;

	if (!IS_ALIGNED(add_arg.offset, PAGE_SIZE) ||
	    !IS_ALIGNED(add_arg.src, PAGE_SIZE))
		return -EINVAL;

	if (add_arg.length & (PAGE_SIZE - 1))
		return -EINVAL;

	if (add_arg.offset + add_arg.length - PAGE_SIZE >= encl->size)
		return -EINVAL;

	if (copy_from_user(&secinfo, (void __user *)add_arg.secinfo,
			   sizeof(secinfo)))
		return -EFAULT;

	if (sgx_validate_secinfo(&secinfo))
		return -EINVAL;

	for (c = 0 ; c < add_arg.length; c += PAGE_SIZE) {
		if (signal_pending(current)) {
			if (!c)
				ret = -EINTR;

			break;
		}

		if (need_resched())
			cond_resched();

		ret = sgx_encl_add_page(encl, add_arg.src + c, add_arg.offset + c,
					&secinfo, add_arg.flags);
		if (ret)
			break;
	}

	add_arg.count = c;

	if (copy_to_user(arg, &add_arg, sizeof(add_arg)))
		return -EFAULT;

	return ret;
}

long sgx_ioctl(struct file *filep, unsigned int cmd, unsigned long arg)
{
	struct sgx_encl *encl = filep->private_data;
	int ret;

	if (test_and_set_bit(SGX_ENCL_IOCTL, &encl->flags))
		return -EBUSY;

	switch (cmd) {
	case SGX_IOC_ENCLAVE_CREATE:
		ret = sgx_ioc_enclave_create(encl, (void __user *)arg);
		break;
	case SGX_IOC_ENCLAVE_ADD_PAGES:
		ret = sgx_ioc_enclave_add_pages(encl, (void __user *)arg);
		break;
	default:
		ret = -ENOIOCTLCMD;
		break;
	}

	clear_bit(SGX_ENCL_IOCTL, &encl->flags);
	return ret;
}
