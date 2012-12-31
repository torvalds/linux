/*
 *
 * (C) COPYRIGHT 2008-2011 ARM Limited. All rights reserved.
 *
 * This program is free software and is provided to you under the terms of the GNU General Public License version 2
 * as published by the Free Software Foundation, and any use by you of this program is subject to the terms of such GNU licence.
 * 
 * A copy of the licence is included with the program, and can also be obtained from Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 * 
 */



#include <ump/ump_kernel_interface.h>
#include <ump/src/ump_ioctl.h>

#include <linux/module.h>            /* kernel module definitions */
#include <linux/fs.h>                /* file system operations */
#include <linux/cdev.h>              /* character device definitions */
#include <linux/ioport.h>            /* request_mem_region */
#include <linux/mm.h> /* memory mananger definitions */
#include <linux/pfn.h>
#include <linux/highmem.h> /*kmap*/

#include <linux/compat.h> /* is_compat_task */

#include <common/ump_kernel_core.h>
#include <ump_arch.h>
#include <common/ump_kernel_priv.h>

static void umpp_vm_close(struct vm_area_struct *vma)
{
	umpp_cpu_mapping * mapping;
	umpp_session * session;
	ump_dd_handle handle;

	mapping = (umpp_cpu_mapping*)vma->vm_private_data;
	session = mapping->session;
	handle = mapping->handle;

	mutex_lock(&session->session_lock);
	umpp_dd_remove_cpu_mapping(mapping->handle, mapping); /* will free the mapping object */
	mutex_unlock(&session->session_lock);

	ump_dd_release(handle);
}


static const struct vm_operations_struct umpp_vm_ops = {
	.close = umpp_vm_close
};

int umpp_phys_commit(umpp_allocation * alloc)
{
	uint64_t i;

	/* round up to a page boundary */
	alloc->size = (alloc->size + PAGE_SIZE - 1) & ~((uint64_t)PAGE_SIZE-1) ;
	/* calculate number of pages */
	alloc->blocksCount = alloc->size >> PAGE_SHIFT;

	if( (sizeof(ump_dd_physical_block_64) * alloc->blocksCount) > ((size_t)-1))
	{
		printk(KERN_WARNING "UMP: umpp_phys_commit - trying to allocate more than possible\n");
		return -ENOMEM;
	}

	alloc->block_array = kmalloc(sizeof(ump_dd_physical_block_64) * alloc->blocksCount, __GFP_HARDWALL | GFP_KERNEL | __GFP_NORETRY | __GFP_NOWARN);
	if (NULL == alloc->block_array)
	{
		return -ENOMEM;
	}

	for (i = 0; i < alloc->blocksCount; i++)
	{
		void * mp;
		struct page * page = alloc_page(GFP_HIGHUSER | __GFP_NORETRY | __GFP_NOWARN | __GFP_COLD);
		if (NULL == page)
		{
			break;
		}

		alloc->block_array[i].addr = page_to_pfn(page) << PAGE_SHIFT;
		alloc->block_array[i].size = PAGE_SIZE;

		mp = kmap(page);
		if (NULL == mp)
		{
			__free_page(page);
			break;
		}

		memset(mp, 0x00, PAGE_SIZE); /* instead of __GFP_ZERO, so we can do cache maintenance */
		ump_sync_to_memory(PFN_PHYS(page_to_pfn(page)), mp, PAGE_SIZE);
		kunmap(page);
	}

	if (i == alloc->blocksCount)
	{
		return 0;
	}
	else
	{
		uint64_t j;
		for (j = 0; j < i; j++)
		{
			struct page * page;
			page = pfn_to_page(alloc->block_array[j].addr >> PAGE_SHIFT);
			__free_page(page);
		}
		
		kfree(alloc->block_array);

		return -ENOMEM;
	}
}

void umpp_phys_free(umpp_allocation * alloc)
{
	uint64_t i;

	for (i = 0; i < alloc->blocksCount; i++)
	{
		__free_page(pfn_to_page(alloc->block_array[i].addr >> PAGE_SHIFT));
	}

	kfree(alloc->block_array);
}

int umpp_linux_mmap(struct file * filp, struct vm_area_struct * vma)
{
	ump_secure_id id;
	ump_dd_handle h;
	size_t offset;
	int err = -EINVAL;
	size_t length = vma->vm_end - vma->vm_start;

	umpp_cpu_mapping * map = NULL;
	umpp_session *session = filp->private_data;

	if ( 0 == length )
	{
		return -EINVAL;
	}

	map = kzalloc(sizeof(*map), GFP_KERNEL);
	if (NULL == map)
	{
		WARN_ON(1);
		err = -ENOMEM;
		goto out;
	}

	/* unpack our arg */
#if defined CONFIG_64BIT && CONFIG_64BIT
	if (is_compat_task())
	{
#endif
		id = vma->vm_pgoff >> UMP_LINUX_OFFSET_BITS_32;
		offset = vma->vm_pgoff & UMP_LINUX_OFFSET_MASK_32;
#if defined CONFIG_64BIT && CONFIG_64BIT
	}
	else
	{
		id = vma->vm_pgoff >> UMP_LINUX_OFFSET_BITS_64;
		offset = vma->vm_pgoff & UMP_LINUX_OFFSET_MASK_64;
	}
#endif

	h = ump_dd_from_secure_id(id);
	if (UMP_DD_INVALID_MEMORY_HANDLE != h)
	{
		uint64_t i;
		uint64_t block_idx;
		uint64_t block_offset;
		uint64_t paddr;
		umpp_allocation * alloc;
		uint64_t last_byte;

		vma->vm_flags |= VM_DONTCOPY | VM_DONTEXPAND | VM_RESERVED | VM_IO | VM_MIXEDMAP;
		vma->vm_ops = &umpp_vm_ops;
		vma->vm_private_data = map;

		alloc = (umpp_allocation*)h;

		if( (alloc->flags & UMP_CONSTRAINT_UNCACHED) != 0)
		{
			/* cache disabled flag set, disable caching for cpu mappings */
			vma->vm_page_prot = pgprot_writecombine(vma->vm_page_prot);
		}

		last_byte = length + (offset << PAGE_SHIFT) - 1;
		if (last_byte >= alloc->size || last_byte < (offset << PAGE_SHIFT))
		{
			goto err_out;
		}

		if (umpp_dd_find_start_block(alloc, offset << PAGE_SHIFT, &block_idx, &block_offset))
		{
			goto err_out;
		}

		paddr = alloc->block_array[block_idx].addr + block_offset;

		for (i = 0; i < (length >> PAGE_SHIFT); i++)
		{
			/* check if we've overrrun the current block, if so move to the next block */
			if (paddr >= (alloc->block_array[block_idx].addr + alloc->block_array[block_idx].size))
			{
				block_idx++;
				UMP_ASSERT(block_idx < alloc->blocksCount);
				paddr = alloc->block_array[block_idx].addr;
			}

			err = vm_insert_mixed(vma, vma->vm_start + (i << PAGE_SHIFT), paddr >> PAGE_SHIFT);
			paddr += PAGE_SIZE;
		}

		map->vaddr_start = (void*)vma->vm_start;
		map->nr_pages = length >> PAGE_SHIFT;
		map->page_off = offset;
		map->handle = h;
		map->session = session;

		mutex_lock(&session->session_lock);
		umpp_dd_add_cpu_mapping(h, map);
		mutex_unlock(&session->session_lock);

		return 0;

		err_out:

		ump_dd_release(h);
	}

	kfree(map);

out:

	return err;
}

