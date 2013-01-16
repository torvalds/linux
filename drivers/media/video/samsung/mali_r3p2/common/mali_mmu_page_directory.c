/*
 * Copyright (C) 2011-2012 ARM Limited. All rights reserved.
 * 
 * This program is free software and is provided to you under the terms of the GNU General Public License version 2
 * as published by the Free Software Foundation, and any use by you of this program is subject to the terms of such GNU licence.
 * 
 * A copy of the licence is included with the program, and can also be obtained from Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 */

#include "mali_kernel_common.h"
#include "mali_kernel_core.h"
#include "mali_osk.h"
#include "mali_uk_types.h"
#include "mali_mmu_page_directory.h"
#include "mali_memory.h"
#include "mali_l2_cache.h"
#include "mali_group.h"

static _mali_osk_errcode_t fill_page(mali_io_address mapping, u32 data);

u32 mali_allocate_empty_page(void)
{
	_mali_osk_errcode_t err;
	mali_io_address mapping;
	u32 address;

	if(_MALI_OSK_ERR_OK != mali_mmu_get_table_page(&address, &mapping))
	{
		/* Allocation failed */
		return 0;
	}

	MALI_DEBUG_ASSERT_POINTER( mapping );

	err = fill_page(mapping, 0);
	if (_MALI_OSK_ERR_OK != err)
	{
		mali_mmu_release_table_page(address);
	}
	return address;
}

void mali_free_empty_page(u32 address)
{
	if (MALI_INVALID_PAGE != address)
	{
		mali_mmu_release_table_page(address);
	}
}

_mali_osk_errcode_t mali_create_fault_flush_pages(u32 *page_directory, u32 *page_table, u32 *data_page)
{
	_mali_osk_errcode_t err;
	mali_io_address page_directory_mapping;
	mali_io_address page_table_mapping;
	mali_io_address data_page_mapping;

	err = mali_mmu_get_table_page(data_page, &data_page_mapping);
	if (_MALI_OSK_ERR_OK == err)
	{
		err = mali_mmu_get_table_page(page_table, &page_table_mapping);
		if (_MALI_OSK_ERR_OK == err)
		{
			err = mali_mmu_get_table_page(page_directory, &page_directory_mapping);
			if (_MALI_OSK_ERR_OK == err)
			{
				fill_page(data_page_mapping, 0);
				fill_page(page_table_mapping, *data_page | MALI_MMU_FLAGS_WRITE_PERMISSION | MALI_MMU_FLAGS_READ_PERMISSION | MALI_MMU_FLAGS_PRESENT);
				fill_page(page_directory_mapping, *page_table | MALI_MMU_FLAGS_PRESENT);
				MALI_SUCCESS;
			}
			mali_mmu_release_table_page(*page_table);
			*page_table = MALI_INVALID_PAGE;
		}
		mali_mmu_release_table_page(*data_page);
		*data_page = MALI_INVALID_PAGE;
	}
	return err;
}

void mali_destroy_fault_flush_pages(u32 *page_directory, u32 *page_table, u32 *data_page)
{
	if (MALI_INVALID_PAGE != *page_directory)
	{
		mali_mmu_release_table_page(*page_directory);
		*page_directory = MALI_INVALID_PAGE;
	}

	if (MALI_INVALID_PAGE != *page_table)
	{
		mali_mmu_release_table_page(*page_table);
		*page_table = MALI_INVALID_PAGE;
	}

	if (MALI_INVALID_PAGE != *data_page)
	{
		mali_mmu_release_table_page(*data_page);
		*data_page = MALI_INVALID_PAGE;
	}
}

static _mali_osk_errcode_t fill_page(mali_io_address mapping, u32 data)
{
	int i;
	MALI_DEBUG_ASSERT_POINTER( mapping );

	for(i = 0; i < MALI_MMU_PAGE_SIZE/4; i++)
	{
		_mali_osk_mem_iowrite32_relaxed( mapping, i * sizeof(u32), data);
	}
	_mali_osk_mem_barrier();
	MALI_SUCCESS;
}

_mali_osk_errcode_t mali_mmu_pagedir_map(struct mali_page_directory *pagedir, u32 mali_address, u32 size)
{
	const int first_pde = MALI_MMU_PDE_ENTRY(mali_address);
	const int last_pde = MALI_MMU_PDE_ENTRY(mali_address + size - 1);
	_mali_osk_errcode_t err;
	mali_io_address pde_mapping;
	u32 pde_phys;
	int i;

	for(i = first_pde; i <= last_pde; i++)
	{
		if(0 == (_mali_osk_mem_ioread32(pagedir->page_directory_mapped, i*sizeof(u32)) & MALI_MMU_FLAGS_PRESENT))
		{
			/* Page table not present */
			MALI_DEBUG_ASSERT(0 == pagedir->page_entries_usage_count[i]);
			MALI_DEBUG_ASSERT(NULL == pagedir->page_entries_mapped[i]);

			err = mali_mmu_get_table_page(&pde_phys, &pde_mapping);
			if(_MALI_OSK_ERR_OK != err)
			{
				MALI_PRINT_ERROR(("Failed to allocate page table page.\n"));
				return err;
			}
			pagedir->page_entries_mapped[i] = pde_mapping;

			/* Update PDE, mark as present */
			_mali_osk_mem_iowrite32_relaxed(pagedir->page_directory_mapped, i*sizeof(u32),
			                pde_phys | MALI_MMU_FLAGS_PRESENT);

			MALI_DEBUG_ASSERT(0 == pagedir->page_entries_usage_count[i]);
			pagedir->page_entries_usage_count[i] = 1;
		}
		else
		{
			pagedir->page_entries_usage_count[i]++;
		}
	}
	_mali_osk_write_mem_barrier();

	MALI_SUCCESS;
}

MALI_STATIC_INLINE void mali_mmu_zero_pte(mali_io_address page_table, u32 mali_address, u32 size)
{
	int i;
	const int first_pte = MALI_MMU_PTE_ENTRY(mali_address);
	const int last_pte = MALI_MMU_PTE_ENTRY(mali_address + size - 1);

	for (i = first_pte; i <= last_pte; i++)
	{
		_mali_osk_mem_iowrite32_relaxed(page_table, i * sizeof(u32), 0);
	}
}

_mali_osk_errcode_t mali_mmu_pagedir_unmap(struct mali_page_directory *pagedir, u32 mali_address, u32 size)
{
	const int first_pde = MALI_MMU_PDE_ENTRY(mali_address);
	const int last_pde = MALI_MMU_PDE_ENTRY(mali_address + size - 1);
	u32 left = size;
	int i;
#ifndef MALI_UNMAP_FLUSH_ALL_MALI_L2
	mali_bool pd_changed = MALI_FALSE;
	u32 pages_to_invalidate[3]; /* hard-coded to 3: max two pages from the PT level plus max one page from PD level */
	u32 num_pages_inv = 0;
#endif

	/* For all page directory entries in range. */
	for (i = first_pde; i <= last_pde; i++)
	{
		u32 size_in_pde, offset;

		MALI_DEBUG_ASSERT_POINTER(pagedir->page_entries_mapped[i]);
		MALI_DEBUG_ASSERT(0 != pagedir->page_entries_usage_count[i]);

		/* Offset into page table, 0 if mali_address is 4MiB aligned */
		offset = (mali_address & (MALI_MMU_VIRTUAL_PAGE_SIZE - 1));
		if (left < MALI_MMU_VIRTUAL_PAGE_SIZE - offset)
		{
			size_in_pde = left;
		}
		else
		{
			size_in_pde = MALI_MMU_VIRTUAL_PAGE_SIZE - offset;
		}

		pagedir->page_entries_usage_count[i]--;

		/* If entire page table is unused, free it */
		if (0 == pagedir->page_entries_usage_count[i])
		{
			u32 page_address;
			MALI_DEBUG_PRINT(4, ("Releasing page table as this is the last reference\n"));
			/* last reference removed, no need to zero out each PTE  */

			page_address = MALI_MMU_ENTRY_ADDRESS(_mali_osk_mem_ioread32(pagedir->page_directory_mapped, i*sizeof(u32)));
			pagedir->page_entries_mapped[i] = NULL;
			_mali_osk_mem_iowrite32_relaxed(pagedir->page_directory_mapped, i*sizeof(u32), 0);

			mali_mmu_release_table_page(page_address);
#ifndef MALI_UNMAP_FLUSH_ALL_MALI_L2
			pd_changed = MALI_TRUE;
#endif
		}
		else
		{
#ifndef MALI_UNMAP_FLUSH_ALL_MALI_L2
			pages_to_invalidate[num_pages_inv] = mali_page_directory_get_phys_address(pagedir, i);
			num_pages_inv++;
			MALI_DEBUG_ASSERT(num_pages_inv<3);
#endif

			/* If part of the page table is still in use, zero the relevant PTEs */
			mali_mmu_zero_pte(pagedir->page_entries_mapped[i], mali_address, size_in_pde);
		}

		left -= size_in_pde;
		mali_address += size_in_pde;
	}
	_mali_osk_write_mem_barrier();

#ifndef MALI_UNMAP_FLUSH_ALL_MALI_L2
	/* L2 pages invalidation */
	if (MALI_TRUE == pd_changed)
	{
		pages_to_invalidate[num_pages_inv] = pagedir->page_directory;
		num_pages_inv++;
		MALI_DEBUG_ASSERT(num_pages_inv<3);
	}

	if (_MALI_PRODUCT_ID_MALI200 != mali_kernel_core_get_product_id())
	{
		mali_l2_cache_invalidate_pages_conditional(pages_to_invalidate, num_pages_inv);
	}
#endif

	MALI_SUCCESS;
}

struct mali_page_directory *mali_mmu_pagedir_alloc(void)
{
	struct mali_page_directory *pagedir;

	pagedir = _mali_osk_calloc(1, sizeof(struct mali_page_directory));
	if(NULL == pagedir)
	{
		return NULL;
	}

	if(_MALI_OSK_ERR_OK != mali_mmu_get_table_page(&pagedir->page_directory, &pagedir->page_directory_mapped))
	{
		_mali_osk_free(pagedir);
		return NULL;
	}

	/* Zero page directory */
	fill_page(pagedir->page_directory_mapped, 0);

	return pagedir;
}

void mali_mmu_pagedir_free(struct mali_page_directory *pagedir)
{
	const int num_page_table_entries = sizeof(pagedir->page_entries_mapped) / sizeof(pagedir->page_entries_mapped[0]);
	int i;

	/* Free referenced page tables and zero PDEs. */
	for (i = 0; i < num_page_table_entries; i++)
	{
		if (pagedir->page_directory_mapped && (_mali_osk_mem_ioread32(pagedir->page_directory_mapped, sizeof(u32)*i) & MALI_MMU_FLAGS_PRESENT))
		{
			mali_mmu_release_table_page( _mali_osk_mem_ioread32(pagedir->page_directory_mapped, i*sizeof(u32)) & ~MALI_MMU_FLAGS_MASK);
			_mali_osk_mem_iowrite32_relaxed(pagedir->page_directory_mapped, i * sizeof(u32), 0);
		}
	}
	_mali_osk_write_mem_barrier();

	/* Free the page directory page. */
	mali_mmu_release_table_page(pagedir->page_directory);

	_mali_osk_free(pagedir);
}


void mali_mmu_pagedir_update(struct mali_page_directory *pagedir, u32 mali_address, u32 phys_address, u32 size, mali_memory_cache_settings cache_settings)
{
	u32 end_address = mali_address + size;
	u32 permission_bits;

	switch ( cache_settings )
	{
		case MALI_CACHE_GP_READ_ALLOCATE:
		MALI_DEBUG_PRINT(5, ("Map L2 GP_Read_allocate\n"));
		permission_bits = MALI_MMU_FLAGS_FORCE_GP_READ_ALLOCATE;
		break;

		case MALI_CACHE_STANDARD:
		MALI_DEBUG_PRINT(5, ("Map L2 Standard\n"));
		/*falltrough */
		default:
		if ( MALI_CACHE_STANDARD != cache_settings) MALI_PRINT_ERROR(("Wrong cache settings\n"));
		permission_bits = MALI_MMU_FLAGS_WRITE_PERMISSION | MALI_MMU_FLAGS_READ_PERMISSION | MALI_MMU_FLAGS_PRESENT;
	}

	/* Map physical pages into MMU page tables */
	for ( ; mali_address < end_address; mali_address += MALI_MMU_PAGE_SIZE, phys_address += MALI_MMU_PAGE_SIZE)
	{
		MALI_DEBUG_ASSERT_POINTER(pagedir->page_entries_mapped[MALI_MMU_PDE_ENTRY(mali_address)]);
		_mali_osk_mem_iowrite32_relaxed(pagedir->page_entries_mapped[MALI_MMU_PDE_ENTRY(mali_address)],
		                MALI_MMU_PTE_ENTRY(mali_address) * sizeof(u32),
			        phys_address | permission_bits);
	}
	_mali_osk_write_mem_barrier();
}

u32 mali_page_directory_get_phys_address(struct mali_page_directory *pagedir, u32 index)
{
	return (_mali_osk_mem_ioread32(pagedir->page_directory_mapped, index*sizeof(u32)) & ~MALI_MMU_FLAGS_MASK);
}

/* For instrumented */
struct dump_info
{
	u32 buffer_left;
	u32 register_writes_size;
	u32 page_table_dump_size;
	u32 *buffer;
};

static _mali_osk_errcode_t writereg(u32 where, u32 what, const char *comment, struct dump_info *info)
{
	if (NULL != info)
	{
		info->register_writes_size += sizeof(u32)*2; /* two 32-bit words */

		if (NULL != info->buffer)
		{
			/* check that we have enough space */
			if (info->buffer_left < sizeof(u32)*2) MALI_ERROR(_MALI_OSK_ERR_NOMEM);

			*info->buffer = where;
			info->buffer++;

			*info->buffer = what;
			info->buffer++;

			info->buffer_left -= sizeof(u32)*2;
		}
	}

	MALI_SUCCESS;
}

static _mali_osk_errcode_t dump_page(mali_io_address page, u32 phys_addr, struct dump_info * info)
{
	if (NULL != info)
	{
		/* 4096 for the page and 4 bytes for the address */
		const u32 page_size_in_elements = MALI_MMU_PAGE_SIZE / 4;
		const u32 page_size_in_bytes = MALI_MMU_PAGE_SIZE;
		const u32 dump_size_in_bytes = MALI_MMU_PAGE_SIZE + 4;

		info->page_table_dump_size += dump_size_in_bytes;

		if (NULL != info->buffer)
		{
			if (info->buffer_left < dump_size_in_bytes) MALI_ERROR(_MALI_OSK_ERR_NOMEM);

			*info->buffer = phys_addr;
			info->buffer++;

			_mali_osk_memcpy(info->buffer, page, page_size_in_bytes);
			info->buffer += page_size_in_elements;

			info->buffer_left -= dump_size_in_bytes;
		}
	}

	MALI_SUCCESS;
}

static _mali_osk_errcode_t dump_mmu_page_table(struct mali_page_directory *pagedir, struct dump_info * info)
{
	MALI_DEBUG_ASSERT_POINTER(pagedir);
	MALI_DEBUG_ASSERT_POINTER(info);

	if (NULL != pagedir->page_directory_mapped)
	{
		int i;

		MALI_CHECK_NO_ERROR(
			dump_page(pagedir->page_directory_mapped, pagedir->page_directory, info)
			);

		for (i = 0; i < 1024; i++)
		{
			if (NULL != pagedir->page_entries_mapped[i])
			{
				MALI_CHECK_NO_ERROR(
				    dump_page(pagedir->page_entries_mapped[i],
				        _mali_osk_mem_ioread32(pagedir->page_directory_mapped,
				        i * sizeof(u32)) & ~MALI_MMU_FLAGS_MASK, info)
				);
			}
		}
	}

	MALI_SUCCESS;
}

static _mali_osk_errcode_t dump_mmu_registers(struct mali_page_directory *pagedir, struct dump_info * info)
{
	MALI_CHECK_NO_ERROR(writereg(0x00000000, pagedir->page_directory,
	                             "set the page directory address", info));
	MALI_CHECK_NO_ERROR(writereg(0x00000008, 4, "zap???", info));
	MALI_CHECK_NO_ERROR(writereg(0x00000008, 0, "enable paging", info));
	MALI_SUCCESS;
}

_mali_osk_errcode_t _mali_ukk_query_mmu_page_table_dump_size( _mali_uk_query_mmu_page_table_dump_size_s *args )
{
	struct dump_info info = { 0, 0, 0, NULL };
	struct mali_session_data * session_data;

	MALI_DEBUG_ASSERT_POINTER(args);
  	MALI_CHECK_NON_NULL(args->ctx, _MALI_OSK_ERR_INVALID_ARGS);

	session_data = (struct mali_session_data *)(args->ctx);

	MALI_CHECK_NO_ERROR(dump_mmu_registers(session_data->page_directory, &info));
	MALI_CHECK_NO_ERROR(dump_mmu_page_table(session_data->page_directory, &info));
	args->size = info.register_writes_size + info.page_table_dump_size;
	MALI_SUCCESS;
}

_mali_osk_errcode_t _mali_ukk_dump_mmu_page_table( _mali_uk_dump_mmu_page_table_s * args )
{
	struct dump_info info = { 0, 0, 0, NULL };
	struct mali_session_data * session_data;

  	MALI_DEBUG_ASSERT_POINTER(args);
  	MALI_CHECK_NON_NULL(args->ctx, _MALI_OSK_ERR_INVALID_ARGS);
	MALI_CHECK_NON_NULL(args->buffer, _MALI_OSK_ERR_INVALID_ARGS);

	session_data = (struct mali_session_data *)(args->ctx);

	info.buffer_left = args->size;
	info.buffer = args->buffer;

	args->register_writes = info.buffer;
	MALI_CHECK_NO_ERROR(dump_mmu_registers(session_data->page_directory, &info));

	args->page_table_dump = info.buffer;
	MALI_CHECK_NO_ERROR(dump_mmu_page_table(session_data->page_directory, &info));

	args->register_writes_size = info.register_writes_size;
	args->page_table_dump_size = info.page_table_dump_size;

	MALI_SUCCESS;
}
