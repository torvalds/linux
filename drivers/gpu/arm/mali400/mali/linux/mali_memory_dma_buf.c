/*
 * This confidential and proprietary software may be used only as
 * authorised by a licensing agreement from ARM Limited
 * (C) COPYRIGHT 2012-2014 ARM Limited
 * ALL RIGHTS RESERVED
 * The entire notice above must be reproduced on all authorised
 * copies and copies may only be made to the extent permitted
 * by a licensing agreement from ARM Limited.
 */

#include <linux/fs.h>      /* file system operations */
#include <asm/uaccess.h>        /* user space access */
#include <linux/dma-buf.h>
#include <linux/scatterlist.h>
#include <linux/rbtree.h>
#include <linux/platform_device.h>
#include <linux/wait.h>
#include <linux/sched.h>
#include <linux/mutex.h>

#include "mali_ukk.h"
#include "mali_osk.h"
#include "mali_kernel_common.h"
#include "mali_session.h"
#include "mali_kernel_linux.h"

#include "mali_memory.h"
#include "mali_memory_dma_buf.h"

#include "mali_pp_job.h"

static void mali_dma_buf_unmap(struct mali_dma_buf_attachment *mem);

struct mali_dma_buf_attachment {
	struct dma_buf *buf;
	struct dma_buf_attachment *attachment;
	struct sg_table *sgt;
	struct mali_session_data *session;
	int map_ref;
	struct mutex map_lock;
	mali_bool is_mapped;
	wait_queue_head_t wait_queue;
};

static void mali_dma_buf_release(struct mali_dma_buf_attachment *mem)
{
	MALI_DEBUG_PRINT(3, ("Mali DMA-buf: release attachment %p\n", mem));

	MALI_DEBUG_ASSERT_POINTER(mem);
	MALI_DEBUG_ASSERT_POINTER(mem->attachment);
	MALI_DEBUG_ASSERT_POINTER(mem->buf);

#if defined(CONFIG_MALI_DMA_BUF_MAP_ON_ATTACH)
	/* We mapped implicitly on attach, so we need to unmap on release */
	mali_dma_buf_unmap(mem);
#endif

	/* Wait for buffer to become unmapped */
	wait_event(mem->wait_queue, !mem->is_mapped);
	MALI_DEBUG_ASSERT(!mem->is_mapped);

	dma_buf_detach(mem->buf, mem->attachment);
	dma_buf_put(mem->buf);

	_mali_osk_free(mem);
}

void mali_mem_dma_buf_release(mali_mem_allocation *descriptor)
{
	struct mali_dma_buf_attachment *mem = descriptor->dma_buf.attachment;

	mali_dma_buf_release(mem);
}

/*
 * Map DMA buf attachment \a mem into \a session at virtual address \a virt.
 */
static int mali_dma_buf_map(struct mali_dma_buf_attachment *mem, struct mali_session_data *session, u32 virt, u32 flags)
{
	struct mali_page_directory *pagedir;
	struct scatterlist *sg;
	int i;

	MALI_DEBUG_ASSERT_POINTER(mem);
	MALI_DEBUG_ASSERT_POINTER(session);
	MALI_DEBUG_ASSERT(mem->session == session);

	mutex_lock(&mem->map_lock);

	mem->map_ref++;

	MALI_DEBUG_PRINT(5, ("Mali DMA-buf: map attachment %p, new map_ref = %d\n", mem, mem->map_ref));

	if (1 == mem->map_ref) {
		/* First reference taken, so we need to map the dma buf */
		MALI_DEBUG_ASSERT(!mem->is_mapped);

		pagedir = mali_session_get_page_directory(session);
		MALI_DEBUG_ASSERT_POINTER(pagedir);

		mem->sgt = dma_buf_map_attachment(mem->attachment, DMA_BIDIRECTIONAL);
		if (IS_ERR_OR_NULL(mem->sgt)) {
			MALI_DEBUG_PRINT_ERROR(("Failed to map dma-buf attachment\n"));
			return -EFAULT;
		}

		for_each_sg(mem->sgt->sgl, sg, mem->sgt->nents, i) {
			u32 size = sg_dma_len(sg);
			dma_addr_t phys = sg_dma_address(sg);

			/* sg must be page aligned. */
			MALI_DEBUG_ASSERT(0 == size % MALI_MMU_PAGE_SIZE);

			mali_mmu_pagedir_update(pagedir, virt, phys, size, MALI_MMU_FLAGS_DEFAULT);

			virt += size;
		}

		if (flags & MALI_MEM_FLAG_MALI_GUARD_PAGE) {
			u32 guard_phys;
			MALI_DEBUG_PRINT(7, ("Mapping in extra guard page\n"));

			guard_phys = sg_dma_address(mem->sgt->sgl);
			mali_mmu_pagedir_update(pagedir, virt, guard_phys, MALI_MMU_PAGE_SIZE, MALI_MMU_FLAGS_DEFAULT);
		}

		mem->is_mapped = MALI_TRUE;
		mutex_unlock(&mem->map_lock);

		/* Wake up any thread waiting for buffer to become mapped */
		wake_up_all(&mem->wait_queue);
	} else {
		MALI_DEBUG_ASSERT(mem->is_mapped);
		mutex_unlock(&mem->map_lock);
	}

	return 0;
}

static void mali_dma_buf_unmap(struct mali_dma_buf_attachment *mem)
{
	MALI_DEBUG_ASSERT_POINTER(mem);
	MALI_DEBUG_ASSERT_POINTER(mem->attachment);
	MALI_DEBUG_ASSERT_POINTER(mem->buf);

	mutex_lock(&mem->map_lock);

	mem->map_ref--;

	MALI_DEBUG_PRINT(5, ("Mali DMA-buf: unmap attachment %p, new map_ref = %d\n", mem, mem->map_ref));

	if (0 == mem->map_ref) {
		dma_buf_unmap_attachment(mem->attachment, mem->sgt, DMA_BIDIRECTIONAL);

		mem->is_mapped = MALI_FALSE;
	}

	mutex_unlock(&mem->map_lock);

	/* Wake up any thread waiting for buffer to become unmapped */
	wake_up_all(&mem->wait_queue);
}

#if !defined(CONFIG_MALI_DMA_BUF_MAP_ON_ATTACH)
int mali_dma_buf_map_job(struct mali_pp_job *job)
{
	mali_mem_allocation *descriptor;
	struct mali_dma_buf_attachment *mem;
	_mali_osk_errcode_t err;
	int i;
	int ret = 0;

	_mali_osk_mutex_wait(job->session->memory_lock);

	for (i = 0; i < job->num_memory_cookies; i++) {
		int cookie = job->memory_cookies[i];

		if (0 == cookie) {
			/* 0 is not a valid cookie */
			MALI_DEBUG_ASSERT(NULL == job->dma_bufs[i]);
			continue;
		}

		MALI_DEBUG_ASSERT(0 < cookie);

		err = mali_descriptor_mapping_get(job->session->descriptor_mapping,
						  cookie, (void **)&descriptor);

		if (_MALI_OSK_ERR_OK != err) {
			MALI_DEBUG_PRINT_ERROR(("Mali DMA-buf: Failed to get descriptor for cookie %d\n", cookie));
			ret = -EFAULT;
			MALI_DEBUG_ASSERT(NULL == job->dma_bufs[i]);
			continue;
		}

		if (MALI_MEM_DMA_BUF != descriptor->type) {
			/* Not a DMA-buf */
			MALI_DEBUG_ASSERT(NULL == job->dma_bufs[i]);
			continue;
		}

		mem = descriptor->dma_buf.attachment;

		MALI_DEBUG_ASSERT_POINTER(mem);
		MALI_DEBUG_ASSERT(mem->session == job->session);

		err = mali_dma_buf_map(mem, mem->session, descriptor->mali_mapping.addr, descriptor->flags);
		if (0 != err) {
			MALI_DEBUG_PRINT_ERROR(("Mali DMA-buf: Failed to map dma-buf for cookie %d at mali address %x\b",
						cookie, descriptor->mali_mapping.addr));
			ret = -EFAULT;
			MALI_DEBUG_ASSERT(NULL == job->dma_bufs[i]);
			continue;
		}

		/* Add mem to list of DMA-bufs mapped for this job */
		job->dma_bufs[i] = mem;
	}

	_mali_osk_mutex_signal(job->session->memory_lock);

	return ret;
}

void mali_dma_buf_unmap_job(struct mali_pp_job *job)
{
	int i;
	for (i = 0; i < job->num_dma_bufs; i++) {
		if (NULL == job->dma_bufs[i]) continue;

		mali_dma_buf_unmap(job->dma_bufs[i]);
		job->dma_bufs[i] = NULL;
	}
}
#endif /* !CONFIG_MALI_DMA_BUF_MAP_ON_ATTACH */

int mali_attach_dma_buf(struct mali_session_data *session, _mali_uk_attach_dma_buf_s __user *user_arg)
{
	struct dma_buf *buf;
	struct mali_dma_buf_attachment *mem;
	_mali_uk_attach_dma_buf_s args;
	mali_mem_allocation *descriptor;
	int md;
	int fd;

	/* Get call arguments from user space. copy_from_user returns how many bytes which where NOT copied */
	if (0 != copy_from_user(&args, (void __user *)user_arg, sizeof(_mali_uk_attach_dma_buf_s))) {
		return -EFAULT;
	}

	if (args.mali_address & ~PAGE_MASK) {
		MALI_DEBUG_PRINT_ERROR(("Requested address (0x%08x) is not page aligned\n", args.mali_address));
		return -EINVAL;
	}

	if (args.mali_address >= args.mali_address + args.size) {
		MALI_DEBUG_PRINT_ERROR(("Requested address and size (0x%08x + 0x%08x) is too big\n", args.mali_address, args.size));
		return -EINVAL;
	}

	fd = args.mem_fd;

	buf = dma_buf_get(fd);
	if (IS_ERR_OR_NULL(buf)) {
		MALI_DEBUG_PRINT_ERROR(("Failed to get dma-buf from fd: %d\n", fd));
		return PTR_RET(buf);
	}

	/* Currently, mapping of the full buffer are supported. */
	if (args.size != buf->size) {
		MALI_DEBUG_PRINT_ERROR(("dma-buf size doesn't match mapping size.\n"));
		dma_buf_put(buf);
		return -EINVAL;
	}

	mem = _mali_osk_calloc(1, sizeof(struct mali_dma_buf_attachment));
	if (NULL == mem) {
		MALI_DEBUG_PRINT_ERROR(("Failed to allocate dma-buf tracing struct\n"));
		dma_buf_put(buf);
		return -ENOMEM;
	}

	mem->buf = buf;
	mem->session = session;
	mem->map_ref = 0;
	mutex_init(&mem->map_lock);
	init_waitqueue_head(&mem->wait_queue);

	mem->attachment = dma_buf_attach(mem->buf, &mali_platform_device->dev);
	if (NULL == mem->attachment) {
		MALI_DEBUG_PRINT_ERROR(("Failed to attach to dma-buf %d\n", fd));
		dma_buf_put(mem->buf);
		_mali_osk_free(mem);
		return -EFAULT;
	}

	/* Set up Mali memory descriptor */
	descriptor = mali_mem_descriptor_create(session, MALI_MEM_DMA_BUF);
	if (NULL == descriptor) {
		MALI_DEBUG_PRINT_ERROR(("Failed to allocate descriptor dma-buf %d\n", fd));
		mali_dma_buf_release(mem);
		return -ENOMEM;
	}

	descriptor->size = args.size;
	descriptor->mali_mapping.addr = args.mali_address;

	descriptor->dma_buf.attachment = mem;

	descriptor->flags |= MALI_MEM_FLAG_DONT_CPU_MAP;
	if (args.flags & _MALI_MAP_EXTERNAL_MAP_GUARD_PAGE) {
		descriptor->flags = MALI_MEM_FLAG_MALI_GUARD_PAGE;
	}

	_mali_osk_mutex_wait(session->memory_lock);

	/* Map dma-buf into this session's page tables */
	if (_MALI_OSK_ERR_OK != mali_mem_mali_map_prepare(descriptor)) {
		_mali_osk_mutex_signal(session->memory_lock);
		MALI_DEBUG_PRINT_ERROR(("Failed to map dma-buf on Mali\n"));
		mali_mem_descriptor_destroy(descriptor);
		mali_dma_buf_release(mem);
		return -ENOMEM;
	}

#if defined(CONFIG_MALI_DMA_BUF_MAP_ON_ATTACH)
	/* Map memory into session's Mali virtual address space. */

	if (0 != mali_dma_buf_map(mem, session, descriptor->mali_mapping.addr, descriptor->flags)) {
		mali_mem_mali_map_free(descriptor);
		_mali_osk_mutex_signal(session->memory_lock);

		MALI_DEBUG_PRINT_ERROR(("Failed to map dma-buf %d into Mali address space\n", fd));
		mali_mem_descriptor_destroy(descriptor);
		mali_dma_buf_release(mem);
		return -ENOMEM;
	}

#endif

	_mali_osk_mutex_signal(session->memory_lock);

	/* Get descriptor mapping for memory. */
	if (_MALI_OSK_ERR_OK != mali_descriptor_mapping_allocate_mapping(session->descriptor_mapping, descriptor, &md)) {
		_mali_osk_mutex_wait(session->memory_lock);
		mali_mem_mali_map_free(descriptor);
		_mali_osk_mutex_signal(session->memory_lock);

		MALI_DEBUG_PRINT_ERROR(("Failed to create descriptor mapping for dma-buf %d\n", fd));
		mali_mem_descriptor_destroy(descriptor);
		mali_dma_buf_release(mem);
		return -EFAULT;
	}

	/* Return stuff to user space */
	if (0 != put_user(md, &user_arg->cookie)) {
		_mali_osk_mutex_wait(session->memory_lock);
		mali_mem_mali_map_free(descriptor);
		_mali_osk_mutex_signal(session->memory_lock);

		MALI_DEBUG_PRINT_ERROR(("Failed to return descriptor to user space for dma-buf %d\n", fd));
		mali_descriptor_mapping_free(session->descriptor_mapping, md);
		mali_dma_buf_release(mem);
		return -EFAULT;
	}

	return 0;
}

int mali_release_dma_buf(struct mali_session_data *session, _mali_uk_release_dma_buf_s __user *user_arg)
{
	int ret = 0;
	_mali_uk_release_dma_buf_s args;
	mali_mem_allocation *descriptor;

	/* get call arguments from user space. copy_from_user returns how many bytes which where NOT copied */
	if (0 != copy_from_user(&args, (void __user *)user_arg, sizeof(_mali_uk_release_dma_buf_s))) {
		return -EFAULT;
	}

	MALI_DEBUG_PRINT(3, ("Mali DMA-buf: release descriptor cookie %ld\n", args.cookie));

	_mali_osk_mutex_wait(session->memory_lock);

	descriptor = mali_descriptor_mapping_free(session->descriptor_mapping, (u32)args.cookie);

	if (NULL != descriptor) {
		MALI_DEBUG_PRINT(3, ("Mali DMA-buf: Releasing dma-buf at mali address %x\n", descriptor->mali_mapping.addr));

		mali_mem_mali_map_free(descriptor);

		mali_dma_buf_release(descriptor->dma_buf.attachment);

		mali_mem_descriptor_destroy(descriptor);
	} else {
		MALI_DEBUG_PRINT_ERROR(("Invalid memory descriptor %ld used to release dma-buf\n", args.cookie));
		ret = -EINVAL;
	}

	_mali_osk_mutex_signal(session->memory_lock);

	/* Return the error that _mali_ukk_map_external_ump_mem produced */
	return ret;
}

int mali_dma_buf_get_size(struct mali_session_data *session, _mali_uk_dma_buf_get_size_s __user *user_arg)
{
	_mali_uk_dma_buf_get_size_s args;
	int fd;
	struct dma_buf *buf;

	/* get call arguments from user space. copy_from_user returns how many bytes which where NOT copied */
	if (0 != copy_from_user(&args, (void __user *)user_arg, sizeof(_mali_uk_dma_buf_get_size_s))) {
		return -EFAULT;
	}

	/* Do DMA-BUF stuff */
	fd = args.mem_fd;

	buf = dma_buf_get(fd);
	if (IS_ERR_OR_NULL(buf)) {
		MALI_DEBUG_PRINT_ERROR(("Failed to get dma-buf from fd: %d\n", fd));
		return PTR_RET(buf);
	}

	if (0 != put_user(buf->size, &user_arg->size)) {
		dma_buf_put(buf);
		return -EFAULT;
	}

	dma_buf_put(buf);

	return 0;
}
