// SPDX-License-Identifier: MIT
/*
 * Copyright © 2020 Intel Corporation
 */

#include <linux/mm.h>
#include <linux/pagemap.h>
#include <linux/shmem_fs.h>

#include "gem/i915_gem_object.h"
#include "shmem_utils.h"

struct file *shmem_create_from_data(const char *name, void *data, size_t len)
{
	struct file *file;
	int err;

	file = shmem_file_setup(name, PAGE_ALIGN(len), VM_NORESERVE);
	if (IS_ERR(file))
		return file;

	err = shmem_write(file, 0, data, len);
	if (err) {
		fput(file);
		return ERR_PTR(err);
	}

	return file;
}

struct file *shmem_create_from_object(struct drm_i915_gem_object *obj)
{
	struct file *file;
	void *ptr;

	if (obj->ops == &i915_gem_shmem_ops) {
		file = obj->base.filp;
		atomic_long_inc(&file->f_count);
		return file;
	}

	ptr = i915_gem_object_pin_map(obj, I915_MAP_WB);
	if (IS_ERR(ptr))
		return ERR_CAST(ptr);

	file = shmem_create_from_data("", ptr, obj->base.size);
	i915_gem_object_unpin_map(obj);

	return file;
}

static size_t shmem_npte(struct file *file)
{
	return file->f_mapping->host->i_size >> PAGE_SHIFT;
}

static void __shmem_unpin_map(struct file *file, void *ptr, size_t n_pte)
{
	unsigned long pfn;

	vunmap(ptr);

	for (pfn = 0; pfn < n_pte; pfn++) {
		struct page *page;

		page = shmem_read_mapping_page_gfp(file->f_mapping, pfn,
						   GFP_KERNEL);
		if (!WARN_ON(IS_ERR(page))) {
			put_page(page);
			put_page(page);
		}
	}
}

void *shmem_pin_map(struct file *file)
{
	const size_t n_pte = shmem_npte(file);
	pte_t *stack[32], **ptes, **mem;
	struct vm_struct *area;
	unsigned long pfn;

	mem = stack;
	if (n_pte > ARRAY_SIZE(stack)) {
		mem = kvmalloc_array(n_pte, sizeof(*mem), GFP_KERNEL);
		if (!mem)
			return NULL;
	}

	area = alloc_vm_area(n_pte << PAGE_SHIFT, mem);
	if (!area) {
		if (mem != stack)
			kvfree(mem);
		return NULL;
	}

	ptes = mem;
	for (pfn = 0; pfn < n_pte; pfn++) {
		struct page *page;

		page = shmem_read_mapping_page_gfp(file->f_mapping, pfn,
						   GFP_KERNEL);
		if (IS_ERR(page))
			goto err_page;

		**ptes++ = mk_pte(page,  PAGE_KERNEL);
	}

	if (mem != stack)
		kvfree(mem);

	mapping_set_unevictable(file->f_mapping);
	return area->addr;

err_page:
	if (mem != stack)
		kvfree(mem);

	__shmem_unpin_map(file, area->addr, pfn);
	return NULL;
}

void shmem_unpin_map(struct file *file, void *ptr)
{
	mapping_clear_unevictable(file->f_mapping);
	__shmem_unpin_map(file, ptr, shmem_npte(file));
}

static int __shmem_rw(struct file *file, loff_t off,
		      void *ptr, size_t len,
		      bool write)
{
	unsigned long pfn;

	for (pfn = off >> PAGE_SHIFT; len; pfn++) {
		unsigned int this =
			min_t(size_t, PAGE_SIZE - offset_in_page(off), len);
		struct page *page;
		void *vaddr;

		page = shmem_read_mapping_page_gfp(file->f_mapping, pfn,
						   GFP_KERNEL);
		if (IS_ERR(page))
			return PTR_ERR(page);

		vaddr = kmap(page);
		if (write)
			memcpy(vaddr + offset_in_page(off), ptr, this);
		else
			memcpy(ptr, vaddr + offset_in_page(off), this);
		kunmap(page);
		put_page(page);

		len -= this;
		ptr += this;
		off = 0;
	}

	return 0;
}

int shmem_read(struct file *file, loff_t off, void *dst, size_t len)
{
	return __shmem_rw(file, off, dst, len, false);
}

int shmem_write(struct file *file, loff_t off, void *src, size_t len)
{
	return __shmem_rw(file, off, src, len, true);
}

#if IS_ENABLED(CONFIG_DRM_I915_SELFTEST)
#include "st_shmem_utils.c"
#endif
