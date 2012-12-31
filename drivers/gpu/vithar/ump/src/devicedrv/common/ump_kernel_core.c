/*
 *
 * (C) COPYRIGHT 2008-2012 ARM Limited. All rights reserved.
 *
 * This program is free software and is provided to you under the terms of the GNU General Public License version 2
 * as published by the Free Software Foundation, and any use by you of this program is subject to the terms of such GNU licence.
 * 
 * A copy of the licence is included with the program, and can also be obtained from Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 * 
 */



/* module headers */
#include <malisw/mali_stdtypes.h>
#include <ump/ump_kernel_interface.h>
#include <ump/src/ump_ioctl.h>

/* local headers */
#include <common/ump_kernel_core.h>
#include <common/ump_kernel_descriptor_mapping.h>
#include <ump_arch.h>
#include <common/ump_kernel_priv.h>

#define UMP_FLAGS_RANGE ((UMP_PROT_SHAREABLE<<1) - 1u)

static umpp_device device;

ump_result umpp_core_constructor(void)
{
	mutex_init(&device.secure_id_map_lock);
	device.secure_id_map = umpp_descriptor_mapping_create(UMP_EXPECTED_IDS, UMP_MAX_IDS);
	if (NULL != device.secure_id_map)
	{
		if (UMP_OK == umpp_device_initialize())
		{
			return UMP_OK;
		}
		umpp_descriptor_mapping_destroy(device.secure_id_map);
	}
	mutex_destroy(&device.secure_id_map_lock);

	return UMP_ERROR;
}

void umpp_core_destructor(void)
{
	umpp_device_terminate();
	umpp_descriptor_mapping_destroy(device.secure_id_map);
	mutex_destroy(&device.secure_id_map_lock);
}

umpp_session *umpp_core_session_start(void)
{
	umpp_session * session;

	session = kzalloc(sizeof(*session), GFP_KERNEL);
	if (NULL != session)
	{
		mutex_init(&session->session_lock);

		INIT_LIST_HEAD(&session->memory_usage);

		/* try to create import client session, not a failure if they fail to initialize */
		umpp_import_handlers_init(session);
	}

	return session;
}

void umpp_core_session_end(umpp_session *session)
{
	umpp_session_memory_usage * usage, *_usage;
	UMP_ASSERT(session);

	list_for_each_entry_safe(usage, _usage, &session->memory_usage, link)
	{
		printk(KERN_WARNING "UMP: Memory usage cleanup, releasing secure ID %d\n", ump_dd_secure_id_get(usage->mem));
		ump_dd_release(usage->mem);
		kfree(usage);

	}

	/* we should now not hold any imported memory objects,
	 * detatch all import handlers */
	umpp_import_handlers_term(session);

	mutex_destroy(&session->session_lock);
	kfree(session);
}

ump_dd_handle ump_dd_allocate_64(uint64_t size, ump_alloc_flags flags, ump_dd_security_filter filter_func, ump_dd_final_release_callback final_release_func, void* callback_data)
{
	umpp_allocation * alloc;
	int i;

	UMP_ASSERT(size);

	if (flags & (~UMP_FLAGS_RANGE))
	{
		printk(KERN_WARNING "UMP: allocation flags out of allowed bits range\n");
		return UMP_DD_INVALID_MEMORY_HANDLE;
	}

	if( ( flags & (UMP_PROT_CPU_RD | UMP_PROT_W_RD | UMP_PROT_X_RD | UMP_PROT_Y_RD | UMP_PROT_Z_RD ) ) == 0 ||
	    ( flags & (UMP_PROT_CPU_WR | UMP_PROT_W_WR | UMP_PROT_X_WR | UMP_PROT_Y_WR | UMP_PROT_Z_WR )) == 0 )
	{
		printk(KERN_WARNING "UMP: allocation flags should have at least one read and one write permission bit set\n");
		return UMP_DD_INVALID_MEMORY_HANDLE;
	}

	/*check permission flags to be set if hit flags are set too*/
	for (i = UMP_DEVICE_CPU_SHIFT; i<=UMP_DEVICE_Z_SHIFT; i+=4)
	{
		if (flags & (UMP_HINT_DEVICE_RD<<i))
		{
			UMP_ASSERT(flags & (UMP_PROT_DEVICE_RD<<i));
		}
		if (flags & (UMP_HINT_DEVICE_WR<<i))
		{
			UMP_ASSERT(flags & (UMP_PROT_DEVICE_WR<<i));
		}
	}

	alloc = kzalloc(sizeof(*alloc), GFP_KERNEL | __GFP_HARDWALL);

	if (NULL == alloc)
		goto out1;

	alloc->flags = flags;
	alloc->filter_func = filter_func;
	alloc->final_release_func = final_release_func;
	alloc->callback_data = callback_data;
	alloc->size = size;

	INIT_LIST_HEAD(&alloc->map_list);
	atomic_set(&alloc->refcount, 1);

	if (!(alloc->flags & UMP_PROT_SHAREABLE))
	{
		alloc->owner = get_current()->pid;
	}

	if (0 != umpp_phys_commit(alloc))
	{
		goto out2;
	}

	/* all set up, allocate an ID for it */

	mutex_lock(&device.secure_id_map_lock);
	alloc->id = umpp_descriptor_mapping_allocate(device.secure_id_map, (void*)alloc);
	mutex_unlock(&device.secure_id_map_lock);

	if ((int)alloc->id == 0)
	{
		/* failed to allocate a secure_id */
		goto out3;
	}

	return alloc;

out3:
	umpp_phys_free(alloc);
out2:
	kfree(alloc);
out1:
	return UMP_DD_INVALID_MEMORY_HANDLE;
}

uint64_t ump_dd_size_get_64(const ump_dd_handle mem)
{
	umpp_allocation * alloc;

	UMP_ASSERT(mem);

	alloc = (umpp_allocation*)mem;

	return alloc->size;
}

/*
 * UMP v1 API
 */
unsigned long ump_dd_size_get(ump_dd_handle mem)
{
	umpp_allocation * alloc;

	UMP_ASSERT(mem);

	alloc = (umpp_allocation*)mem;

	UMP_ASSERT(alloc->flags & UMP_CONSTRAINT_32BIT_ADDRESSABLE);
	UMP_ASSERT(alloc->size <= UINT32_MAX);

	return (unsigned long)alloc->size;
}

ump_secure_id ump_dd_secure_id_get(const ump_dd_handle mem)
{
	umpp_allocation * alloc;

	UMP_ASSERT(mem);

	alloc = (umpp_allocation*)mem;

	return alloc->id;
}

ump_alloc_flags ump_dd_allocation_flags_get(const ump_dd_handle mem)
{
	const umpp_allocation * alloc;

	UMP_ASSERT(mem);
	alloc = (const umpp_allocation *)mem;

	return alloc->flags;
}

ump_dd_handle ump_dd_from_secure_id(ump_secure_id secure_id)
{
	umpp_allocation * alloc = UMP_DD_INVALID_MEMORY_HANDLE;

	mutex_lock(&device.secure_id_map_lock);

	if (0 == umpp_descriptor_mapping_lookup(device.secure_id_map, secure_id, (void**)&alloc))
	{
		if (NULL != alloc->filter_func)
		{
			if (!alloc->filter_func(secure_id, alloc, alloc->callback_data))
			{
				alloc = UMP_DD_INVALID_MEMORY_HANDLE; /* the filter denied access */
			}
		}

		/* check permission to access it */
		if ((UMP_DD_INVALID_MEMORY_HANDLE != alloc) && !(alloc->flags & UMP_PROT_SHAREABLE))
		{
			if (alloc->owner != get_current()->pid)
			{
				alloc = UMP_DD_INVALID_MEMORY_HANDLE; /*no rights for the current process*/
			}
		}

		if (UMP_DD_INVALID_MEMORY_HANDLE != alloc)
		{
			if( ump_dd_retain(alloc) != UMP_DD_SUCCESS)
			{
				alloc = UMP_DD_INVALID_MEMORY_HANDLE;
			}
		}
	}
	mutex_unlock(&device.secure_id_map_lock);

	return alloc;
}

/*
 * UMP v1 API
 */
ump_dd_handle ump_dd_handle_create_from_secure_id(ump_secure_id secure_id)
{
	return ump_dd_from_secure_id(secure_id);
}

int ump_dd_retain(ump_dd_handle mem)
{
	umpp_allocation * alloc;

	UMP_ASSERT(mem);

	alloc = (umpp_allocation*)mem;

	/* check for overflow */
	while(1)
	{
		int refcnt = atomic_read(&alloc->refcount);
		if (refcnt + 1 > 0)
		{
			if(atomic_cmpxchg(&alloc->refcount, refcnt, refcnt + 1) == refcnt)
			{
				return 0;
			}
		}
		else
		{
			return -EBUSY;
		}
	}
}

/*
 * UMP v1 API
 */
void ump_dd_reference_add(ump_dd_handle mem)
{
	ump_dd_retain(mem);
}


void ump_dd_release(ump_dd_handle mem)
{
	umpp_allocation * alloc;
	uint32_t new_cnt;

	UMP_ASSERT(mem);

	alloc = (umpp_allocation*)mem;

	/* secure the id for lookup while releasing */
	mutex_lock(&device.secure_id_map_lock);

	/* do the actual release */
	new_cnt = atomic_sub_return(1, &alloc->refcount);
	if (0 == new_cnt)
	{
		/* remove from the table as this was the last ref */
		umpp_descriptor_mapping_remove(device.secure_id_map, alloc->id);
	}

	/* release the lock as early as possible */
	mutex_unlock(&device.secure_id_map_lock);

	if (0 != new_cnt)
	{
		/* exit if still have refs */
		return;
	}

	/* cleanup */
	if (NULL != alloc->final_release_func)
	{
		alloc->final_release_func(alloc, alloc->callback_data);
	}

	if (0 == (alloc->management_flags & UMP_MGMT_EXTERNAL))
	{
		umpp_phys_free(alloc);
	}

	kfree(alloc);
}

/*
 * UMP v1 API
 */
void ump_dd_reference_release(ump_dd_handle mem)
{
	ump_dd_release(mem);
}

void ump_dd_phys_blocks_get_64(const ump_dd_handle mem, uint64_t * pCount, const ump_dd_physical_block_64 ** pArray)
{
	const umpp_allocation * alloc;
	UMP_ASSERT(pCount);
	UMP_ASSERT(pArray);
	UMP_ASSERT(mem);
	alloc = (const umpp_allocation *)mem;
	*pCount = alloc->blocksCount;
	*pArray = alloc->block_array;
}

/*
 * UMP v1 API
 */
ump_dd_status_code ump_dd_phys_blocks_get(ump_dd_handle mem, ump_dd_physical_block * blocks, unsigned long num_blocks)
{
	const umpp_allocation * alloc;
	unsigned long i;
	UMP_ASSERT(mem);
	UMP_ASSERT(blocks);
	UMP_ASSERT(num_blocks);

	alloc = (const umpp_allocation *)mem;

	UMP_ASSERT(alloc->flags & UMP_CONSTRAINT_32BIT_ADDRESSABLE);

	if((uint64_t)num_blocks != alloc->blocksCount)
	{
		return UMP_DD_INVALID;
	}

	for( i = 0; i < num_blocks; i++)
	{
		UMP_ASSERT(alloc->block_array[i].addr <= UINT32_MAX);
		UMP_ASSERT(alloc->block_array[i].size <= UINT32_MAX);

		blocks[i].addr = (unsigned long)alloc->block_array[i].addr;
		blocks[i].size = (unsigned long)alloc->block_array[i].size;
	}

	return UMP_DD_SUCCESS;
}
/*
 * UMP v1 API
 */
ump_dd_status_code ump_dd_phys_block_get(ump_dd_handle mem, unsigned long index, ump_dd_physical_block * block)
{
	const umpp_allocation * alloc;
	UMP_ASSERT(mem);
	UMP_ASSERT(block);
	alloc = (const umpp_allocation *)mem;

	UMP_ASSERT(alloc->flags & UMP_CONSTRAINT_32BIT_ADDRESSABLE);

	UMP_ASSERT(alloc->block_array[index].addr <= UINT32_MAX);
	UMP_ASSERT(alloc->block_array[index].size <= UINT32_MAX);

	block->addr = (unsigned long)alloc->block_array[index].addr;
	block->size = (unsigned long)alloc->block_array[index].size;

	return UMP_DD_SUCCESS;
}

/*
 * UMP v1 API
 */
unsigned long ump_dd_phys_block_count_get(ump_dd_handle mem)
{
	const umpp_allocation * alloc;
	UMP_ASSERT(mem);
	alloc = (const umpp_allocation *)mem;

	UMP_ASSERT(alloc->flags & UMP_CONSTRAINT_32BIT_ADDRESSABLE);
	UMP_ASSERT(alloc->blocksCount <= UINT32_MAX);

	return (unsigned long)alloc->blocksCount;
}

umpp_cpu_mapping * umpp_dd_find_enclosing_mapping(umpp_allocation * alloc, void *uaddr, size_t size)
{
	umpp_cpu_mapping *map;

	void *target_first = uaddr;
	void *target_last = (void*)((uintptr_t)uaddr - 1 + size);

	if (target_last < target_first) /* wrapped */
	{
		return NULL;
	}

	list_for_each_entry(map, &alloc->map_list, link)
	{
		if ( map->vaddr_start <= target_first &&
		   (void*)((uintptr_t)map->vaddr_start + (map->nr_pages << PAGE_SHIFT) - 1) >= target_last)
		{
			return map;
		}
	}
	return NULL;
}

void umpp_dd_add_cpu_mapping(umpp_allocation * alloc, umpp_cpu_mapping * map)
{
	list_add(&map->link, &alloc->map_list);
}

void umpp_dd_remove_cpu_mapping(umpp_allocation * alloc, umpp_cpu_mapping * target)
{
	umpp_cpu_mapping * map;

	list_for_each_entry(map, &alloc->map_list, link)
	{
		if (map == target)
		{
			list_del(&target->link);
			kfree(target);
			return;
		}
	}

	/* not found, error */
	UMP_ASSERT(0);
}

int umpp_dd_find_start_block(const umpp_allocation * alloc, uint64_t offset, uint64_t * block_index, uint64_t * block_internal_offset)
{
	uint64_t i;

	for (i = 0 ; i < alloc->blocksCount; i++)
	{
		if (offset < alloc->block_array[i].size)
		{
			/* found the block_array element containing this offset */
			*block_index = i;
			*block_internal_offset = offset;
			return 0;
		}
		offset -= alloc->block_array[i].size;
	}

	return -ENXIO;
}

void umpp_dd_cpu_msync_now(ump_dd_handle mem, ump_cpu_msync_op op, void * address, size_t size)
{
	umpp_allocation * alloc;
	void *vaddr;
	umpp_cpu_mapping * mapping;
	uint64_t virt_page_off; /* offset of given address from beginning of the virtual mapping */
	uint64_t phys_page_off; /* offset of the virtual mapping from the beginning of the physical buffer */
	uint64_t page_count; /* number of pages to sync */
	uint64_t i;
	uint64_t block_idx;
	uint64_t block_offset;
	uint64_t paddr;

	UMP_ASSERT((UMP_MSYNC_CLEAN == op) || (UMP_MSYNC_CLEAN_AND_INVALIDATE == op));

	alloc = (umpp_allocation*)mem;
	vaddr = (void*)(uintptr_t)address;

	if((alloc->flags & UMP_CONSTRAINT_UNCACHED) != 0)
	{
		/* mapping is not cached */
		return;
	}

	mapping = umpp_dd_find_enclosing_mapping(alloc, vaddr, size);
	if (NULL == mapping)
	{
		printk(KERN_WARNING "UMP: Illegal cache sync address %lx\n", (uintptr_t)vaddr);
		return; /* invalid pointer or size causes out-of-bounds */
	}

	/* we already know that address + size don't wrap around as umpp_dd_find_enclosing_mapping didn't fail */
	page_count = ((((((uintptr_t)address + size - 1) & PAGE_MASK) - ((uintptr_t)address & PAGE_MASK))) >> PAGE_SHIFT) + 1;
	virt_page_off = (vaddr - mapping->vaddr_start) >> PAGE_SHIFT;
	phys_page_off = mapping->page_off;

	if (umpp_dd_find_start_block(alloc, virt_page_off + phys_page_off, &block_idx, &block_offset))
	{
		/* should not fail as a valid mapping was found, so the phys mem must exists */
		printk(KERN_WARNING "UMP: Unable to find physical start block with offset %llx\n", virt_page_off + phys_page_off);
		UMP_ASSERT(0);
		return;
	}

	paddr = alloc->block_array[block_idx].addr + block_offset;

	for (i = 0; i < page_count; i++)
	{
		size_t offset = ((uintptr_t)vaddr) & ((1u << PAGE_SHIFT)-1);
		size_t sz = min((size_t)PAGE_SIZE - offset, size);

		/* check if we've overrrun the current block, if so move to the next block */
		if (paddr >= (alloc->block_array[block_idx].addr + alloc->block_array[block_idx].size))
		{
			block_idx++;
			UMP_ASSERT(block_idx < alloc->blocksCount);
			paddr = alloc->block_array[block_idx].addr;
		}

		if (UMP_MSYNC_CLEAN == op)
		{
			ump_sync_to_memory(paddr, vaddr, sz);
		}
		else /* (UMP_MSYNC_CLEAN_AND_INVALIDATE == op) already validated on entry */
		{
			ump_sync_to_cpu(paddr, vaddr, sz);
		}

		/* advance to next page  */
		vaddr = (void*)((uintptr_t)vaddr + sz);
		size -= sz;
		paddr += sz;
	}
}

UMP_KERNEL_API_EXPORT ump_dd_handle ump_dd_create_from_phys_blocks_64(const ump_dd_physical_block_64 * blocks, uint64_t num_blocks, ump_alloc_flags flags, ump_dd_security_filter filter_func, ump_dd_final_release_callback final_release_func, void* callback_data)
{
	uint64_t size = 0;
	uint64_t i;
	umpp_allocation * alloc;

	UMP_ASSERT(blocks);
	UMP_ASSERT(num_blocks);

	for (i = 0; i < num_blocks; i++)
	{
		size += blocks[i].size;
	}
	UMP_ASSERT(size);

	if (flags & (~UMP_FLAGS_RANGE))
	{
		printk(KERN_WARNING "UMP: allocation flags out of allowed bits range\n");
		return UMP_DD_INVALID_MEMORY_HANDLE;
	}

	if( ( flags & (UMP_PROT_CPU_RD | UMP_PROT_W_RD | UMP_PROT_X_RD | UMP_PROT_Y_RD | UMP_PROT_Z_RD
	    | UMP_PROT_CPU_WR | UMP_PROT_W_WR | UMP_PROT_X_WR | UMP_PROT_Y_WR | UMP_PROT_Z_WR )) == 0 )
	{
		printk(KERN_WARNING "UMP: allocation flags should have at least one read or write permission bit set\n");
		return UMP_DD_INVALID_MEMORY_HANDLE;
	}

	/*check permission flags to be set if hit flags are set too*/
	for (i = UMP_DEVICE_CPU_SHIFT; i<=UMP_DEVICE_Z_SHIFT; i+=4)
	{
		if (flags & (UMP_HINT_DEVICE_RD<<i))
		{
			UMP_ASSERT(flags & (UMP_PROT_DEVICE_RD<<i));
		}
		if (flags & (UMP_HINT_DEVICE_WR<<i))
		{
			UMP_ASSERT(flags & (UMP_PROT_DEVICE_WR<<i));
		}
	}

	alloc = kzalloc(sizeof(*alloc),__GFP_HARDWALL | GFP_KERNEL);

	if (NULL == alloc)
	{
		goto out1;
	}

	alloc->block_array = kzalloc(sizeof(ump_dd_physical_block_64) * num_blocks,__GFP_HARDWALL | GFP_KERNEL);
	if (NULL == alloc->block_array)
	{
		goto out2;
	}

	memcpy(alloc->block_array, blocks, sizeof(ump_dd_physical_block_64) * num_blocks);

	alloc->size = size;
	alloc->blocksCount = num_blocks;
	alloc->flags = flags;
	alloc->filter_func = filter_func;
	alloc->final_release_func = final_release_func;
	alloc->callback_data = callback_data;

	if (!(alloc->flags & UMP_PROT_SHAREABLE))
	{
		alloc->owner = get_current()->pid;
	}


	INIT_LIST_HEAD(&alloc->map_list);
	atomic_set(&alloc->refcount, 1);

	/* all set up, allocate an ID */

	mutex_lock(&device.secure_id_map_lock);
	alloc->id = umpp_descriptor_mapping_allocate(device.secure_id_map, (void*)alloc);
	mutex_unlock(&device.secure_id_map_lock);

	if ((int)alloc->id == 0)
	{
		/* failed to allocate a secure_id */
		goto out3;
	}

	alloc->management_flags |= UMP_MGMT_EXTERNAL;

	return alloc;

out3:
	kfree(alloc->block_array);
out2:
	kfree(alloc);
out1:
	return UMP_DD_INVALID_MEMORY_HANDLE;
}


/*
 * UMP v1 API
 */
UMP_KERNEL_API_EXPORT ump_dd_handle ump_dd_handle_create_from_phys_blocks(ump_dd_physical_block * blocks, unsigned long num_blocks)
{
	ump_dd_handle mem;
	ump_dd_physical_block_64 *block_64_array;
	ump_alloc_flags flags = UMP_V1_API_DEFAULT_ALLOCATION_FLAGS;
	unsigned long i;

	UMP_ASSERT(blocks);
	UMP_ASSERT(num_blocks);

	block_64_array = kzalloc(num_blocks * sizeof(*block_64_array), __GFP_HARDWALL | GFP_KERNEL);

	if(block_64_array == NULL)
	{
		return UMP_DD_INVALID_MEMORY_HANDLE;
	}

	/* copy physical blocks */
	for( i = 0; i < num_blocks; i++)
	{
		block_64_array[i].addr = blocks[i].addr;
		block_64_array[i].size = blocks[i].size;
	}

	mem = ump_dd_create_from_phys_blocks_64(block_64_array, num_blocks, flags, NULL, NULL, NULL);

	kfree(block_64_array);

	return mem;

}
