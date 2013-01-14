/*
 * Copyright (C) 2012 ARM Limited. All rights reserved.
 * 
 * This program is free software and is provided to you under the terms of the GNU General Public License version 2
 * as published by the Free Software Foundation, and any use by you of this program is subject to the terms of such GNU licence.
 * 
 * A copy of the licence is included with the program, and can also be obtained from Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 */

#include <linux/fs.h>	   /* file system operations */
#include <asm/uaccess.h>	/* user space access */
#include <linux/dma-buf.h>
#include <linux/scatterlist.h>
#include <linux/rbtree.h>
#include <linux/platform_device.h>

#include "mali_ukk.h"
#include "mali_osk.h"
#include "mali_kernel_common.h"
#include "mali_session.h"
#include "mali_kernel_linux.h"

#include "mali_kernel_memory_engine.h"
#include "mali_memory.h"


struct mali_dma_buf_attachment {
	struct dma_buf *buf;
	struct dma_buf_attachment *attachment;
	struct sg_table *sgt;
	_mali_osk_atomic_t ref;
	struct rb_node rb_node;
};

static struct rb_root mali_dma_bufs = RB_ROOT;
static DEFINE_SPINLOCK(mali_dma_bufs_lock);

static inline struct mali_dma_buf_attachment *mali_dma_buf_lookup(struct rb_root *root, struct dma_buf *target)
{
	struct rb_node *node = root->rb_node;
	struct mali_dma_buf_attachment *res;

	spin_lock(&mali_dma_bufs_lock);
	while (node)
	{
		res = rb_entry(node, struct mali_dma_buf_attachment, rb_node);

		if (target < res->buf) node = node->rb_left;
		else if (target > res->buf) node = node->rb_right;
		else
		{
			_mali_osk_atomic_inc(&res->ref);
			spin_unlock(&mali_dma_bufs_lock);
			return res;
		}
	}
	spin_unlock(&mali_dma_bufs_lock);

	return NULL;
}

static void mali_dma_buf_add(struct rb_root *root, struct mali_dma_buf_attachment *new)
{
	struct rb_node **node = &root->rb_node;
	struct rb_node *parent = NULL;
	struct mali_dma_buf_attachment *res;

	spin_lock(&mali_dma_bufs_lock);
	while (*node)
	{
		parent = *node;
		res = rb_entry(*node, struct mali_dma_buf_attachment, rb_node);

		if (new->buf < res->buf) node = &(*node)->rb_left;
		else node = &(*node)->rb_right;
	}

	rb_link_node(&new->rb_node, parent, node);
	rb_insert_color(&new->rb_node, &mali_dma_bufs);

	spin_unlock(&mali_dma_bufs_lock);

	return;
}


static void mali_dma_buf_release(void *ctx, void *handle)
{
	struct mali_dma_buf_attachment *mem;
	u32 ref;

	mem = (struct mali_dma_buf_attachment *)handle;

	MALI_DEBUG_ASSERT_POINTER(mem);
	MALI_DEBUG_ASSERT_POINTER(mem->attachment);
	MALI_DEBUG_ASSERT_POINTER(mem->buf);

	spin_lock(&mali_dma_bufs_lock);
	ref = _mali_osk_atomic_dec_return(&mem->ref);

	if (0 == ref)
	{
		rb_erase(&mem->rb_node, &mali_dma_bufs);
		spin_unlock(&mali_dma_bufs_lock);

		MALI_DEBUG_ASSERT(0 == _mali_osk_atomic_read(&mem->ref));

		dma_buf_unmap_attachment(mem->attachment, mem->sgt, DMA_BIDIRECTIONAL);

		dma_buf_detach(mem->buf, mem->attachment);
		dma_buf_put(mem->buf);

		_mali_osk_free(mem);
	}
	else
	{
		spin_unlock(&mali_dma_bufs_lock);
	}
}

/* Callback from memory engine which will map into Mali virtual address space */
static mali_physical_memory_allocation_result mali_dma_buf_commit(void* ctx, mali_allocation_engine * engine, mali_memory_allocation * descriptor, u32* offset, mali_physical_memory_allocation * alloc_info)
{
	struct mali_session_data *session;
	struct mali_page_directory *pagedir;
	struct mali_dma_buf_attachment *mem;
	struct scatterlist *sg;
	int i;
	u32 virt;

	MALI_DEBUG_ASSERT_POINTER(ctx);
	MALI_DEBUG_ASSERT_POINTER(engine);
	MALI_DEBUG_ASSERT_POINTER(descriptor);
	MALI_DEBUG_ASSERT_POINTER(offset);
	MALI_DEBUG_ASSERT_POINTER(alloc_info);

	/* Mapping dma-buf with an offset is not supported. */
	MALI_DEBUG_ASSERT(0 == *offset);

	virt = descriptor->mali_address;
	session = (struct mali_session_data *)descriptor->mali_addr_mapping_info;
	pagedir = mali_session_get_page_directory(session);

	MALI_DEBUG_ASSERT_POINTER(session);

	mem = (struct mali_dma_buf_attachment *)ctx;

	MALI_DEBUG_ASSERT_POINTER(mem);

	mem->sgt = dma_buf_map_attachment(mem->attachment, DMA_BIDIRECTIONAL);
	if (IS_ERR_OR_NULL(mem->sgt))
	{
		MALI_PRINT_ERROR(("Failed to map dma-buf attachment\n"));
		return MALI_MEM_ALLOC_INTERNAL_FAILURE;
	}

	for_each_sg(mem->sgt->sgl, sg, mem->sgt->nents, i)
	{
		u32 size = sg_dma_len(sg);
		dma_addr_t phys = sg_dma_address(sg);

		/* sg must be page aligned. */
		MALI_DEBUG_ASSERT(0 == size % MALI_MMU_PAGE_SIZE);

		mali_mmu_pagedir_update(pagedir, virt, phys, size, MALI_CACHE_STANDARD);

		virt += size;
		*offset += size;
	}

	if (descriptor->flags & MALI_MEMORY_ALLOCATION_FLAG_MAP_GUARD_PAGE)
	{
		u32 guard_phys;
		MALI_DEBUG_PRINT(7, ("Mapping in extra guard page\n"));

		guard_phys = sg_dma_address(mem->sgt->sgl);
		mali_mmu_pagedir_update(mali_session_get_page_directory(session), virt, guard_phys, MALI_MMU_PAGE_SIZE, MALI_CACHE_STANDARD);
	}

	MALI_DEBUG_ASSERT(*offset == descriptor->size);

	alloc_info->ctx = NULL;
	alloc_info->handle = mem;
	alloc_info->next = NULL;
	alloc_info->release = mali_dma_buf_release;

	return MALI_MEM_ALLOC_FINISHED;
}

int mali_attach_dma_buf(struct mali_session_data *session, _mali_uk_attach_dma_buf_s __user *user_arg)
{
	mali_physical_memory_allocator external_memory_allocator;
	struct dma_buf *buf;
	struct mali_dma_buf_attachment *mem;
	_mali_uk_attach_dma_buf_s args;
	mali_memory_allocation *descriptor;
	int md;
	int fd;

	/* Get call arguments from user space. copy_from_user returns how many bytes which where NOT copied */
	if (0 != copy_from_user(&args, (void __user *)user_arg, sizeof(_mali_uk_attach_dma_buf_s)))
	{
		return -EFAULT;
	}


	fd = args.mem_fd;

	buf = dma_buf_get(fd);
	if (IS_ERR_OR_NULL(buf))
	{
		MALI_DEBUG_PRINT(2, ("Failed to get dma-buf from fd: %d\n", fd));
		return PTR_RET(buf);
	}

	/* Currently, mapping of the full buffer are supported. */
	if (args.size != buf->size)
	{
		MALI_DEBUG_PRINT(2, ("dma-buf size doesn't match mapping size.\n"));
		dma_buf_put(buf);
		return -EINVAL;
	}


	mem = mali_dma_buf_lookup(&mali_dma_bufs, buf);
	if (NULL == mem)
	{
		/* dma-buf is not already attached to Mali */
		mem = _mali_osk_calloc(1, sizeof(struct mali_dma_buf_attachment));
		if (NULL == mem)
		{
			MALI_PRINT_ERROR(("Failed to allocate dma-buf tracing struct\n"));
			dma_buf_put(buf);
			return -ENOMEM;
		}
		_mali_osk_atomic_init(&mem->ref, 1);
		mem->buf = buf;

		mem->attachment = dma_buf_attach(mem->buf, &mali_platform_device->dev);
		if (NULL == mem->attachment)
		{
			MALI_DEBUG_PRINT(2, ("Failed to attach to dma-buf %d\n", fd));
			dma_buf_put(mem->buf);
			_mali_osk_free(mem);
			return -EFAULT;
		}

		mali_dma_buf_add(&mali_dma_bufs, mem);
	}
	else
	{
		/* dma-buf is already attached to Mali */
		/* Give back the reference we just took, mali_dma_buf_lookup grabbed a new reference for us. */
		dma_buf_put(buf);
	}

	/* Map dma-buf into this session's page tables */

	/* Set up Mali memory descriptor */
	descriptor = _mali_osk_calloc(1, sizeof(mali_memory_allocation));
	if (NULL == descriptor)
	{
		MALI_PRINT_ERROR(("Failed to allocate descriptor dma-buf %d\n", fd));
		mali_dma_buf_release(NULL, mem);
		return -ENOMEM;
	}

	descriptor->size = args.size;
	descriptor->mapping = NULL;
	descriptor->mali_address = args.mali_address;
	descriptor->mali_addr_mapping_info = (void*)session;
	descriptor->process_addr_mapping_info = NULL; /* do not map to process address space */
	descriptor->lock = session->memory_lock;

	if (args.flags & _MALI_MAP_EXTERNAL_MAP_GUARD_PAGE)
	{
		descriptor->flags = MALI_MEMORY_ALLOCATION_FLAG_MAP_GUARD_PAGE;
	}
	_mali_osk_list_init( &descriptor->list );

	/* Get descriptor mapping for memory. */
	if (_MALI_OSK_ERR_OK != mali_descriptor_mapping_allocate_mapping(session->descriptor_mapping, descriptor, &md))
	{
		MALI_PRINT_ERROR(("Failed to create descriptor mapping for dma-buf %d\n", fd));
		_mali_osk_free(descriptor);
		mali_dma_buf_release(NULL, mem);
		return -EFAULT;
	}

	external_memory_allocator.allocate = mali_dma_buf_commit;
	external_memory_allocator.allocate_page_table_block = NULL;
	external_memory_allocator.ctx = mem;
	external_memory_allocator.name = "DMA-BUF Memory";
	external_memory_allocator.next = NULL;

	/* Map memory into session's Mali virtual address space. */
	_mali_osk_lock_wait(session->memory_lock, _MALI_OSK_LOCKMODE_RW);
	if (_MALI_OSK_ERR_OK != mali_allocation_engine_allocate_memory(mali_mem_get_memory_engine(), descriptor, &external_memory_allocator, NULL))
	{
		_mali_osk_lock_signal(session->memory_lock, _MALI_OSK_LOCKMODE_RW);

		MALI_PRINT_ERROR(("Failed to map dma-buf %d into Mali address space\n", fd));
		mali_descriptor_mapping_free(session->descriptor_mapping, md);
		mali_dma_buf_release(NULL, mem);
		return -ENOMEM;
	}
	_mali_osk_lock_signal(session->memory_lock, _MALI_OSK_LOCKMODE_RW);

	/* Return stuff to user space */
	if (0 != put_user(md, &user_arg->cookie))
	{
		/* Roll back */
		MALI_PRINT_ERROR(("Failed to return descriptor to user space for dma-buf %d\n", fd));
		mali_descriptor_mapping_free(session->descriptor_mapping, md);
		mali_dma_buf_release(NULL, mem);
		return -EFAULT;
	}

	return 0;
}

int mali_release_dma_buf(struct mali_session_data *session, _mali_uk_release_dma_buf_s __user *user_arg)
{
	_mali_uk_release_dma_buf_s args;
	mali_memory_allocation *descriptor;

	/* get call arguments from user space. copy_from_user returns how many bytes which where NOT copied */
	if ( 0 != copy_from_user(&args, (void __user *)user_arg, sizeof(_mali_uk_release_dma_buf_s)) )
	{
		return -EFAULT;
	}

	if (_MALI_OSK_ERR_OK != mali_descriptor_mapping_get(session->descriptor_mapping, args.cookie, (void**)&descriptor))
	{
		MALI_DEBUG_PRINT(1, ("Invalid memory descriptor %d used to release dma-buf\n", args.cookie));
		return -EINVAL;
	}

	descriptor = mali_descriptor_mapping_free(session->descriptor_mapping, args.cookie);

	if (NULL != descriptor)
	{
		_mali_osk_lock_wait( session->memory_lock, _MALI_OSK_LOCKMODE_RW );

		/* Will call back to mali_dma_buf_release() which will release the dma-buf attachment. */
		mali_allocation_engine_release_memory(mali_mem_get_memory_engine(), descriptor);

		_mali_osk_lock_signal( session->memory_lock, _MALI_OSK_LOCKMODE_RW );

		_mali_osk_free(descriptor);
	}

	/* Return the error that _mali_ukk_map_external_ump_mem produced */
	return 0;
}

int mali_dma_buf_get_size(struct mali_session_data *session, _mali_uk_dma_buf_get_size_s __user *user_arg)
{
	_mali_uk_dma_buf_get_size_s args;
	int fd;
	struct dma_buf *buf;

	/* get call arguments from user space. copy_from_user returns how many bytes which where NOT copied */
	if ( 0 != copy_from_user(&args, (void __user *)user_arg, sizeof(_mali_uk_dma_buf_get_size_s)) )
	{
		return -EFAULT;
	}

	/* Do DMA-BUF stuff */
	fd = args.mem_fd;

	buf = dma_buf_get(fd);
	if (IS_ERR_OR_NULL(buf))
	{
		MALI_DEBUG_PRINT(2, ("Failed to get dma-buf from fd: %d\n", fd));
		return PTR_RET(buf);
	}

	if (0 != put_user(buf->size, &user_arg->size))
	{
		dma_buf_put(buf);
		return -EFAULT;
	}

	dma_buf_put(buf);

	return 0;
}
