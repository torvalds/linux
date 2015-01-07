/*
 * Copyright (C) 2010-2014 ARM Limited. All rights reserved.
 * 
 * This program is free software and is provided to you under the terms of the GNU General Public License version 2
 * as published by the Free Software Foundation, and any use by you of this program is subject to the terms of such GNU licence.
 * 
 * A copy of the licence is included with the program, and can also be obtained from Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 */

/**
 * @file mali_osk_low_level_mem.c
 * Implementation of the OS abstraction layer for the kernel device driver
 */

#include <asm/io.h>
#include <linux/ioport.h>
#include <linux/slab.h>

#include "mali_kernel_common.h"
#include "mali_osk.h"
#include "mali_ukk.h"

void _mali_osk_mem_barrier(void)
{
	mb();
}

void _mali_osk_write_mem_barrier(void)
{
	wmb();
}

mali_io_address _mali_osk_mem_mapioregion(uintptr_t phys, u32 size, const char *description)
{
	return (mali_io_address)ioremap_nocache(phys, size);
}

void _mali_osk_mem_unmapioregion(uintptr_t phys, u32 size, mali_io_address virt)
{
	iounmap((void *)virt);
}

_mali_osk_errcode_t inline _mali_osk_mem_reqregion(uintptr_t phys, u32 size, const char *description)
{
#if MALI_LICENSE_IS_GPL
	return _MALI_OSK_ERR_OK; /* GPL driver gets the mem region for the resources registered automatically */
#else
	return ((NULL == request_mem_region(phys, size, description)) ? _MALI_OSK_ERR_NOMEM : _MALI_OSK_ERR_OK);
#endif
}

void inline _mali_osk_mem_unreqregion(uintptr_t phys, u32 size)
{
#if !MALI_LICENSE_IS_GPL
	release_mem_region(phys, size);
#endif
}

void inline _mali_osk_mem_iowrite32_relaxed(volatile mali_io_address addr, u32 offset, u32 val)
{
	__raw_writel(cpu_to_le32(val), ((u8 *)addr) + offset);
}

u32 inline _mali_osk_mem_ioread32(volatile mali_io_address addr, u32 offset)
{
	return ioread32(((u8 *)addr) + offset);
}

void inline _mali_osk_mem_iowrite32(volatile mali_io_address addr, u32 offset, u32 val)
{
	iowrite32(val, ((u8 *)addr) + offset);
}

void _mali_osk_cache_flushall(void)
{
	/** @note Cached memory is not currently supported in this implementation */
}

void _mali_osk_cache_ensure_uncached_range_flushed(void *uncached_mapping, u32 offset, u32 size)
{
	_mali_osk_write_mem_barrier();
}

u32 _mali_osk_mem_write_safe(void __user *dest, const void __user *src, u32 size)
{
#define MALI_MEM_SAFE_COPY_BLOCK_SIZE 4096
	u32 retval = 0;
	void *temp_buf;

	temp_buf = kmalloc(MALI_MEM_SAFE_COPY_BLOCK_SIZE, GFP_KERNEL);
	if (NULL != temp_buf) {
		u32 bytes_left_to_copy = size;
		u32 i;
		for (i = 0; i < size; i += MALI_MEM_SAFE_COPY_BLOCK_SIZE) {
			u32 size_to_copy;
			u32 size_copied;
			u32 bytes_left;

			if (bytes_left_to_copy > MALI_MEM_SAFE_COPY_BLOCK_SIZE) {
				size_to_copy = MALI_MEM_SAFE_COPY_BLOCK_SIZE;
			} else {
				size_to_copy = bytes_left_to_copy;
			}

			bytes_left = copy_from_user(temp_buf, ((char *)src) + i, size_to_copy);
			size_copied = size_to_copy - bytes_left;

			bytes_left = copy_to_user(((char *)dest) + i, temp_buf, size_copied);
			size_copied -= bytes_left;

			bytes_left_to_copy -= size_copied;
			retval += size_copied;

			if (size_copied != size_to_copy) {
				break; /* Early out, we was not able to copy this entire block */
			}
		}

		kfree(temp_buf);
	}

	return retval;
}

_mali_osk_errcode_t _mali_ukk_mem_write_safe(_mali_uk_mem_write_safe_s *args)
{
	void __user *src;
	void __user *dst;
	struct mali_session_data *session;

	MALI_DEBUG_ASSERT_POINTER(args);

	session = (struct mali_session_data *)(uintptr_t)args->ctx;

	if (NULL == session) {
		return _MALI_OSK_ERR_INVALID_ARGS;
	}

	src = (void __user *)(uintptr_t)args->src;
	dst = (void __user *)(uintptr_t)args->dest;

	/* Return number of bytes actually copied */
	args->size = _mali_osk_mem_write_safe(dst, src, args->size);
	return _MALI_OSK_ERR_OK;
}
