/*
 *
 * (C) COPYRIGHT 2010-2011 ARM Limited. All rights reserved.
 *
 * This program is free software and is provided to you under the terms of the GNU General Public License version 2
 * as published by the Free Software Foundation, and any use by you of this program is subject to the terms of such GNU licence.
 * 
 * A copy of the licence is included with the program, and can also be obtained from Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 * 
 */



/**
 * @file mali_kbase_mem_linux.c
 * Base kernel memory APIs, Linux implementation.
 */

/* #define DEBUG	1 */

#include <linux/kernel.h>
#include <linux/bug.h>
#include <linux/mm.h>
#include <linux/fs.h>
#include <linux/dma-mapping.h>

#include <kbase/src/common/mali_kbase.h>
#include <kbase/src/linux/mali_kbase_mem_linux.h>

struct kbase_va_region *kbase_pmem_alloc(struct kbase_context *kctx, u32 size,
					 u32 flags, u16 *pmem_cookie)
{
	struct kbase_va_region *reg;
	u16 cookie;

	OSK_ASSERT(kctx != NULL);
	OSK_ASSERT(pmem_cookie != NULL);

	if ( 0 == size )
	{
		goto out1;
	}

	if (!kbase_check_alloc_flags(flags))
	{
		goto out1;
	}

	reg = kbase_alloc_free_region(kctx, 0, size, KBASE_REG_ZONE_PMEM);
	if (!reg)
		goto out1;

	reg->flags &= ~KBASE_REG_FREE;

	kbase_update_region_flags(reg, flags, MALI_FALSE);

	if (kbase_alloc_phy_pages(reg, size, size))
		goto out2;

	reg->nr_alloc_pages = size;
	reg->extent = 0;

	kbase_gpu_vm_lock(kctx);
	if (!kctx->osctx.cookies)
		goto out3;
	
	cookie = __ffs(kctx->osctx.cookies);
	kctx->osctx.cookies &= ~(1UL << cookie);
	reg->flags &= ~KBASE_REG_COOKIE_MASK;
	reg->flags |= KBASE_REG_COOKIE(cookie);
	
	OSK_DLIST_PUSH_FRONT(&kctx->osctx.reg_pending, reg,
				struct kbase_va_region, link);

	*pmem_cookie = cookie;
	kbase_gpu_vm_unlock(kctx);

	return reg;

out3:
	kbase_gpu_vm_unlock(kctx);
	kbase_free_phy_pages(reg);
out2:
	osk_free(reg);
out1:
	return NULL;
	
}
KBASE_EXPORT_TEST_API(kbase_pmem_alloc)

/*
 * Callback for munmap(). PMEM receives a special treatment, as it
 * frees the memory at the same time it gets unmapped. This avoids the
 * map/unmap race where map reuses a memory range that has been
 * unmapped from CPU, but still mapped on GPU.
 */
STATIC void kbase_cpu_vm_close(struct vm_area_struct *vma)
{
	struct kbase_va_region *reg = vma->vm_private_data;
	kbase_context *kctx = reg->kctx;
	mali_error err;

	kbase_gpu_vm_lock(kctx);

	err = kbase_cpu_free_mapping(reg, vma);
	if (!err &&
	    (reg->flags & KBASE_REG_ZONE_MASK) == KBASE_REG_ZONE_PMEM)
	{
		kbase_mem_free_region(kctx, reg);
	}

	kbase_gpu_vm_unlock(kctx);
}
KBASE_EXPORT_TEST_API(kbase_cpu_vm_close)

static const struct vm_operations_struct kbase_vm_ops = {
	.close = kbase_cpu_vm_close,
};

static int kbase_cpu_mmap(struct kbase_va_region *reg, struct vm_area_struct *vma, void *kaddr, u32 nr_pages)
{
	struct kbase_cpu_mapping *map;
	u64 start_off = vma->vm_pgoff - reg->start_pfn;
	osk_phy_addr *page_array;
	int err = 0;
	int i;

	map = osk_calloc(sizeof(*map));
	if (!map)
	{
		WARN_ON(1);
		err = -ENOMEM;
		goto out;
	}

	/*
	 * VM_DONTCOPY - don't make this mapping available in fork'ed processes
	 * VM_DONTEXPAND - disable mremap on this region
	 * VM_RESERVED & VM_IO - disables paging
	 * VM_MIXEDMAP - Support mixing struct page*s and raw pfns.
	 *               This is needed to support using the dedicated and
	 *               the OS based memory backends together.
	 */
	/*
	 * This will need updating to propagate coherency flags
	 * See MIDBASE-1057
	 */
	vma->vm_flags |= VM_DONTCOPY | VM_DONTEXPAND | VM_RESERVED | VM_IO | VM_MIXEDMAP;
	vma->vm_ops = &kbase_vm_ops;
	vma->vm_private_data = reg;

	page_array = kbase_get_phy_pages(reg);

	if (!(reg->flags & KBASE_REG_CPU_CACHED))
	{
		/* We can't map vmalloc'd memory uncached.
		 * Other memory will have been returned from
		 * osk_phy_pages_alloc which should have done the cache
		 * maintenance necessary to support an uncached mapping
		 */
		BUG_ON(kaddr);
		vma->vm_page_prot = pgprot_writecombine(vma->vm_page_prot);
	}

	if (!kaddr)
	{
		for (i = 0; i < nr_pages; i++)
		{
			err = vm_insert_mixed(vma, vma->vm_start + (i << OSK_PAGE_SHIFT), page_array[i + start_off] >> OSK_PAGE_SHIFT);
			WARN_ON(err);
			if (err)
				break;
		}
	}
	else
	{
		/* vmalloc remaping is easy... */
		err = remap_vmalloc_range(vma, kaddr, 0);
		WARN_ON(err);
	}

	if (err)
	{
		osk_free(map);
		goto out;
	}

	map->uaddr = (osk_virt_addr)vma->vm_start;
	map->nr_pages = nr_pages;
	map->page_off = start_off;
	map->private = vma;

	OSK_DLIST_PUSH_FRONT(&reg->map_list, map,
				struct kbase_cpu_mapping, link);

out:
	return err;
}

static int kbase_rb_mmap(struct kbase_context *kctx,
			 struct vm_area_struct *vma,
			 struct kbase_va_region **reg,
			 void **kmap_addr)
{
	struct kbase_va_region *new_reg;
	void *kaddr;
	u32 nr_pages;
	size_t size;
	int err = 0;
	mali_error m_err =  MALI_ERROR_NONE;

	pr_debug("in kbase_rb_mmap\n");
	size = (vma->vm_end - vma->vm_start);
	nr_pages = size  >> OSK_PAGE_SHIFT;

	if (kctx->jctx.pool_size < size)
	{
		err = -EINVAL;
		goto out;
	}

	kaddr = kctx->jctx.pool;

	new_reg = kbase_alloc_free_region(kctx, 0, nr_pages, KBASE_REG_ZONE_PMEM);
	if (!new_reg)
	{
		err = -ENOMEM;
		WARN_ON(1);
		goto out;
	}

	new_reg->flags	&= ~KBASE_REG_FREE;
	new_reg->flags	|= KBASE_REG_IS_RB | KBASE_REG_CPU_CACHED;

	m_err = kbase_add_va_region(kctx, new_reg, vma->vm_start, nr_pages, 1);
	if (MALI_ERROR_NONE != m_err)
	{
		pr_debug("kbase_rb_mmap: kbase_add_va_region failed\n");
		/* Free allocated new_reg */
		kbase_free_alloced_region(new_reg);
		err = -ENOMEM;
		goto out;
	}

	*kmap_addr	= kaddr;
	*reg		= new_reg;

	pr_debug("kbase_rb_mmap done\n");
	return 0;

out:
	return err;
}

static int  kbase_trace_buffer_mmap(struct kbase_context * kctx, struct vm_area_struct * vma, struct kbase_va_region **reg, void **kaddr)
{
	struct kbase_va_region *new_reg;
	u32 nr_pages;
	size_t size;
	int err = 0;
	u32 * tb;

	pr_debug("in %s\n", __func__);
	size = (vma->vm_end - vma->vm_start);
	nr_pages = size  >> OSK_PAGE_SHIFT;

	if (!kctx->jctx.tb)
	{
		tb = osk_vmalloc(size);
		if (NULL == tb)
		{
			err = -ENOMEM;
			goto out;
		}

		kbase_device_trace_buffer_install(kctx, tb, size);
	}
	else
	{
		err = -EINVAL;
		goto out;
	}

	*kaddr = kctx->jctx.tb;

	new_reg = kbase_alloc_free_region(kctx, 0, nr_pages, KBASE_REG_ZONE_PMEM);
	if (!new_reg)
	{
		err = -ENOMEM;
		WARN_ON(1);
		goto disconnect;
	}

	new_reg->flags	&= ~KBASE_REG_FREE;
	new_reg->flags	|= KBASE_REG_IS_TB | KBASE_REG_CPU_CACHED;

	if (kbase_add_va_region(kctx, new_reg, vma->vm_start, nr_pages, 1))
		BUG_ON(1);

	*reg		= new_reg;

	/* map read only, noexec */
	vma->vm_flags &= ~(VM_WRITE|VM_EXEC);
	/* the rest of the flags is added by the cpu_mmap handler */

	pr_debug("%s done\n", __func__);
	return 0;

disconnect:
	kbase_device_trace_buffer_uninstall(kctx);
	osk_vfree(tb);
out:
	return err;

}

static int kbase_mmu_dump_mmap( struct kbase_context *kctx,
                                struct vm_area_struct *vma,
                                struct kbase_va_region **reg,
                                void **kmap_addr )
{
	struct kbase_va_region *new_reg;
	void *kaddr;
	u32 nr_pages;
	size_t size;
	int err = 0;

	pr_debug("in kbase_mmu_dump_mmap\n");
	size = (vma->vm_end - vma->vm_start);
	nr_pages = size  >> OSK_PAGE_SHIFT;

	kaddr = kbase_mmu_dump(kctx, nr_pages);
	
	if (!kaddr)
	{
		err = -ENOMEM;
		goto out;
	}

	new_reg = kbase_alloc_free_region(kctx, 0, nr_pages, KBASE_REG_ZONE_PMEM);
	if (!new_reg)
	{
		err = -ENOMEM;
		WARN_ON(1);
		goto out;
	}

	new_reg->flags &= ~KBASE_REG_FREE;
	new_reg->flags |= KBASE_REG_IS_MMU_DUMP | KBASE_REG_CPU_CACHED;

	if (kbase_add_va_region(kctx, new_reg, vma->vm_start, nr_pages, 1))
		BUG_ON(1);

	*kmap_addr  = kaddr;
	*reg        = new_reg;

	pr_debug("kbase_mmu_dump_mmap done\n");
	return 0;

out:
	return err;
}

/* must be called with the gpu vm lock held */

struct kbase_va_region * kbase_lookup_cookie(struct kbase_context * kctx, mali_addr64 cookie)
{
	struct kbase_va_region * reg;
	mali_addr64 test_cookie;

	OSK_ASSERT(kctx != NULL);

	test_cookie = KBASE_REG_COOKIE(cookie);

	OSK_DLIST_FOREACH(&kctx->osctx.reg_pending, struct kbase_va_region, link, reg)
	{
		if ((reg->flags & KBASE_REG_COOKIE_MASK) == test_cookie)
		{
			return reg;
		}
	}

	return NULL; /* not found */
}
KBASE_EXPORT_TEST_API(kbase_lookup_cookie)

void kbase_unlink_cookie(struct kbase_context * kctx, mali_addr64 cookie, struct kbase_va_region * reg)
{
	OSKP_ASSERT(kctx != NULL);
	OSKP_ASSERT(reg != NULL);
	OSKP_ASSERT(MALI_TRUE == OSK_DLIST_MEMBER_OF(&kctx->osctx.reg_pending, reg, link));
	OSKP_ASSERT(KBASE_REG_COOKIE(cookie) == (reg->flags & KBASE_REG_COOKIE_MASK));
	OSKP_ASSERT((kctx->osctx.cookies & (1UL << cookie)) == 0);

	OSK_DLIST_REMOVE(&kctx->osctx.reg_pending, reg, link);
	kctx->osctx.cookies |= (1UL << cookie); /* mark as resolved */
}

KBASE_EXPORT_TEST_API(kbase_unlink_cookie)

void kbase_os_mem_map_lock(struct kbase_context * kctx)
{
	struct mm_struct * mm = current->mm;
	(void)kctx;
	down_read(&mm->mmap_sem);
}

void kbase_os_mem_map_unlock(struct kbase_context * kctx)
{
	struct mm_struct * mm = current->mm;
	(void)kctx;
	up_read(&mm->mmap_sem);
}

int kbase_mmap(struct file *file, struct vm_area_struct *vma)
{
	struct kbase_context *kctx = file->private_data;
	struct kbase_va_region *reg;
	void *kaddr = NULL;
	u32 nr_pages;
	int err = 0;

	pr_debug("kbase_mmap\n");
	nr_pages = (vma->vm_end - vma->vm_start) >> OSK_PAGE_SHIFT;
	
	if ( 0 == nr_pages )
	{
		err = -EINVAL;
		goto out;
	}

	kbase_gpu_vm_lock(kctx);

	if (vma->vm_pgoff == KBASE_REG_COOKIE_RB)
	{
		/* Reserve offset 0 for the shared ring-buffer */
		if ((err = kbase_rb_mmap(kctx, vma, &reg, &kaddr)))
			goto out_unlock;

		pr_debug("kbase_rb_mmap ok\n");
		goto map;
	}
	else if (vma->vm_pgoff == KBASE_REG_COOKIE_TB)
	{
		err = kbase_trace_buffer_mmap(kctx, vma, &reg, &kaddr);
		if (0 != err)
			goto out_unlock;
		pr_debug("kbase_trace_buffer_mmap ok\n");
		goto map;
	}
	else if (vma->vm_pgoff == KBASE_REG_COOKIE_MMU_DUMP)
	{
		/* MMU dump */
		if ((err = kbase_mmu_dump_mmap(kctx, vma, &reg, &kaddr)))
			goto out_unlock;

		goto map;
	}

	if (vma->vm_pgoff < OSK_PAGE_SIZE) /* first page is reserved for cookie resolution */
	{
		/* PMEM stuff, fetch the right region */
		reg = kbase_lookup_cookie(kctx, vma->vm_pgoff);

		if (NULL != reg)
		{
			if (reg->nr_pages != nr_pages)
			{
				/* incorrect mmap size */
				/* leave the cookie for a potential later mapping, or to be reclaimed later when the context is freed */
				err = -ENOMEM;
				goto out_unlock;
			}

			kbase_unlink_cookie(kctx, vma->vm_pgoff, reg);

			/*
			 * If we cannot map it in GPU space,
			 * then something is *very* wrong. We
			 * might as well die now.
			 */
			if (kbase_gpu_mmap(kctx, reg, vma->vm_start,
					   nr_pages, 1))
				BUG_ON(1);

			/*
			 * Overwrite the offset with the
			 * region start_pfn, so we effectively
			 * map from offset 0 in the region.
			 */
			vma->vm_pgoff = reg->start_pfn;
			goto map;
		}

		err = -ENOMEM;
		goto out_unlock;
	}
	else if (vma->vm_pgoff < KBASE_REG_ZONE_TMEM_BASE)
	{
		/* invalid offset as it identifies an already mapped pmem */
		err = -ENOMEM;
		goto out_unlock;
	}
	else
	{
		/* TMEM case */
		OSK_DLIST_FOREACH(&kctx->reg_list,
				     struct kbase_va_region, link, reg)
		{
			if (reg->start_pfn <= vma->vm_pgoff &&
			    (reg->start_pfn + reg->nr_alloc_pages) >= (vma->vm_pgoff + nr_pages) &&
			    (reg->flags & (KBASE_REG_ZONE_MASK | KBASE_REG_FREE)) == KBASE_REG_ZONE_TMEM)
			{
				/* Match! */
				goto map;
			}
			    
		}

		err = -ENOMEM;
		goto out_unlock;
	}
map:
	err = kbase_cpu_mmap(reg, vma, kaddr, nr_pages);
	
	if (vma->vm_pgoff == KBASE_REG_COOKIE_MMU_DUMP) {
		/* MMU dump - userspace should now have a reference on
		 * the pages, so we can now free the kernel mapping */
		osk_vfree(kaddr);
	}
out_unlock:
	kbase_gpu_vm_unlock(kctx);
out:
	if (err)
	{
		pr_err("mmap failed %d\n", err);
	}
	return err;
}
KBASE_EXPORT_TEST_API(kbase_mmap)

mali_error kbase_create_os_context(kbase_os_context *osctx)
{
	OSK_ASSERT(osctx != NULL);

	OSK_DLIST_INIT(&osctx->reg_pending);
	osctx->cookies = ~KBASE_REG_RESERVED_COOKIES;
	init_waitqueue_head(&osctx->event_queue);

	return MALI_ERROR_NONE;
}
KBASE_EXPORT_TEST_API(kbase_create_os_context)

static void kbase_reg_pending_dtor(struct kbase_va_region *reg)
{
	kbase_free_phy_pages(reg);
	pr_info("Freeing pending unmapped region\n");
	osk_free(reg);
}

void kbase_destroy_os_context(kbase_os_context *osctx)
{
	OSK_ASSERT(osctx != NULL);

	OSK_DLIST_EMPTY_LIST(&osctx->reg_pending, struct kbase_va_region,
				link, kbase_reg_pending_dtor);
}
KBASE_EXPORT_TEST_API(kbase_destroy_os_context)
