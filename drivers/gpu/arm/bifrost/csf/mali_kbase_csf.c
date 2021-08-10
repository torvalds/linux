// SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note
/*
 *
 * (C) COPYRIGHT 2018-2021 ARM Limited. All rights reserved.
 *
 * This program is free software and is provided to you under the terms of the
 * GNU General Public License version 2 as published by the Free Software
 * Foundation, and any use by you of this program is subject to the terms
 * of such GNU license.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, you can access it online at
 * http://www.gnu.org/licenses/gpl-2.0.html.
 *
 */

#include <mali_kbase.h>
#include <gpu/mali_kbase_gpu_fault.h>
#include <mali_kbase_reset_gpu.h>
#include "mali_kbase_csf.h"
#include "backend/gpu/mali_kbase_pm_internal.h"
#include <linux/export.h>
#include <linux/priority_control_manager.h>
#include <linux/shmem_fs.h>
#include <uapi/gpu/arm/bifrost/csf/mali_gpu_csf_registers.h>
#include "mali_kbase_csf_tiler_heap.h"
#include <mmu/mali_kbase_mmu.h>
#include "mali_kbase_csf_timeout.h"
#include <csf/ipa_control/mali_kbase_csf_ipa_control.h>

#define CS_REQ_EXCEPTION_MASK (CS_REQ_FAULT_MASK | CS_REQ_FATAL_MASK)
#define CS_ACK_EXCEPTION_MASK (CS_ACK_FAULT_MASK | CS_ACK_FATAL_MASK)
#define POWER_DOWN_LATEST_FLUSH_VALUE ((u32)1)

/**
 * struct kbase_csf_event - CSF event callback.
 *
 * This structure belongs to the list of events which is part of a Kbase
 * context, and describes a callback function with a custom parameter to pass
 * to it when a CSF event is signalled.
 *
 * @link:      Link to the rest of the list.
 * @kctx:      Pointer to the Kbase context this event belongs to.
 * @callback:  Callback function to call when a CSF event is signalled.
 * @param:     Parameter to pass to the callback function.
 */
struct kbase_csf_event {
	struct list_head link;
	struct kbase_context *kctx;
	kbase_csf_event_callback *callback;
	void *param;
};

const u8 kbasep_csf_queue_group_priority_to_relative[BASE_QUEUE_GROUP_PRIORITY_COUNT] = {
	KBASE_QUEUE_GROUP_PRIORITY_HIGH,
	KBASE_QUEUE_GROUP_PRIORITY_MEDIUM,
	KBASE_QUEUE_GROUP_PRIORITY_LOW,
	KBASE_QUEUE_GROUP_PRIORITY_REALTIME
};
const u8 kbasep_csf_relative_to_queue_group_priority[KBASE_QUEUE_GROUP_PRIORITY_COUNT] = {
	BASE_QUEUE_GROUP_PRIORITY_REALTIME,
	BASE_QUEUE_GROUP_PRIORITY_HIGH,
	BASE_QUEUE_GROUP_PRIORITY_MEDIUM,
	BASE_QUEUE_GROUP_PRIORITY_LOW
};

static void put_user_pages_mmap_handle(struct kbase_context *kctx,
			struct kbase_queue *queue)
{
	unsigned long cookie_nr;

	lockdep_assert_held(&kctx->csf.lock);

	if (queue->handle == BASEP_MEM_INVALID_HANDLE)
		return;

	cookie_nr =
		PFN_DOWN(queue->handle - BASEP_MEM_CSF_USER_IO_PAGES_HANDLE);

	if (!WARN_ON(kctx->csf.user_pages_info[cookie_nr] != queue)) {
		/* free up cookie */
		kctx->csf.user_pages_info[cookie_nr] = NULL;
		bitmap_set(kctx->csf.cookies, cookie_nr, 1);
	}

	queue->handle = BASEP_MEM_INVALID_HANDLE;
}

/* Reserve a cookie, to be returned as a handle to userspace for creating
 * the CPU mapping of the pair of input/output pages and Hw doorbell page.
 * Will return 0 in case of success otherwise negative on failure.
 */
static int get_user_pages_mmap_handle(struct kbase_context *kctx,
			struct kbase_queue *queue)
{
	unsigned long cookie, cookie_nr;

	lockdep_assert_held(&kctx->csf.lock);

	if (bitmap_empty(kctx->csf.cookies,
				KBASE_CSF_NUM_USER_IO_PAGES_HANDLE)) {
		dev_err(kctx->kbdev->dev,
			"No csf cookies available for allocation!");
		return -ENOMEM;
	}

	/* allocate a cookie */
	cookie_nr = find_first_bit(kctx->csf.cookies,
				KBASE_CSF_NUM_USER_IO_PAGES_HANDLE);
	if (kctx->csf.user_pages_info[cookie_nr]) {
		dev_err(kctx->kbdev->dev,
			"Inconsistent state of csf cookies!");
		return -EINVAL;
	}
	kctx->csf.user_pages_info[cookie_nr] = queue;
	bitmap_clear(kctx->csf.cookies, cookie_nr, 1);

	/* relocate to correct base */
	cookie = cookie_nr + PFN_DOWN(BASEP_MEM_CSF_USER_IO_PAGES_HANDLE);
	cookie <<= PAGE_SHIFT;

	queue->handle = (u64)cookie;

	return 0;
}

static void gpu_munmap_user_io_pages(struct kbase_context *kctx,
			struct kbase_va_region *reg)
{
	size_t num_pages = 2;

	kbase_mmu_teardown_pages(kctx->kbdev, &kctx->kbdev->csf.mcu_mmu,
				 reg->start_pfn, num_pages, MCU_AS_NR);

	WARN_ON(reg->flags & KBASE_REG_FREE);

	mutex_lock(&kctx->kbdev->csf.reg_lock);
	kbase_remove_va_region(reg);
	mutex_unlock(&kctx->kbdev->csf.reg_lock);
}

static void init_user_io_pages(struct kbase_queue *queue)
{
	u32 *input_addr = (u32 *)(queue->user_io_addr);
	u32 *output_addr = (u32 *)(queue->user_io_addr + PAGE_SIZE);

	input_addr[CS_INSERT_LO/4] = 0;
	input_addr[CS_INSERT_HI/4] = 0;

	input_addr[CS_EXTRACT_INIT_LO/4] = 0;
	input_addr[CS_EXTRACT_INIT_HI/4] = 0;

	output_addr[CS_EXTRACT_LO/4] = 0;
	output_addr[CS_EXTRACT_HI/4] = 0;

	output_addr[CS_ACTIVE/4] = 0;
}

/* Map the input/output pages in the shared interface segment of MCU firmware
 * address space.
 */
static int gpu_mmap_user_io_pages(struct kbase_device *kbdev,
		struct tagged_addr *phys, struct kbase_va_region *reg)
{
	unsigned long mem_flags = KBASE_REG_GPU_RD;
	const size_t num_pages = 2;
	int ret;

#if ((KERNEL_VERSION(4, 4, 147) >= LINUX_VERSION_CODE) || \
		((KERNEL_VERSION(4, 6, 0) > LINUX_VERSION_CODE) && \
		 (KERNEL_VERSION(4, 5, 0) <= LINUX_VERSION_CODE)))
	mem_flags |=
		KBASE_REG_MEMATTR_INDEX(AS_MEMATTR_INDEX_NON_CACHEABLE);
#else
	if (kbdev->system_coherency == COHERENCY_NONE) {
		mem_flags |=
			KBASE_REG_MEMATTR_INDEX(AS_MEMATTR_INDEX_NON_CACHEABLE);
	} else {
		mem_flags |= KBASE_REG_SHARE_BOTH |
			KBASE_REG_MEMATTR_INDEX(AS_MEMATTR_INDEX_SHARED);
	}
#endif

	mutex_lock(&kbdev->csf.reg_lock);
	ret = kbase_add_va_region_rbtree(kbdev, reg, 0, num_pages, 1);
	reg->flags &= ~KBASE_REG_FREE;
	mutex_unlock(&kbdev->csf.reg_lock);

	if (ret)
		return ret;

	/* Map input page */
	ret = kbase_mmu_insert_pages(kbdev, &kbdev->csf.mcu_mmu,
				     reg->start_pfn, &phys[0],
				     1, mem_flags, MCU_AS_NR,
				     KBASE_MEM_GROUP_CSF_IO);
	if (ret)
		goto bad_insert;

	/* Map output page, it needs rw access */
	mem_flags |= KBASE_REG_GPU_WR;
	ret = kbase_mmu_insert_pages(kbdev, &kbdev->csf.mcu_mmu,
				     reg->start_pfn + 1, &phys[1],
				     1, mem_flags, MCU_AS_NR,
				     KBASE_MEM_GROUP_CSF_IO);
	if (ret)
		goto bad_insert_output_page;

	return 0;

bad_insert_output_page:
	kbase_mmu_teardown_pages(kbdev, &kbdev->csf.mcu_mmu,
				 reg->start_pfn, 1, MCU_AS_NR);
bad_insert:
	mutex_lock(&kbdev->csf.reg_lock);
	kbase_remove_va_region(reg);
	mutex_unlock(&kbdev->csf.reg_lock);

	return ret;
}

static void kernel_unmap_user_io_pages(struct kbase_context *kctx,
			struct kbase_queue *queue)
{
	const size_t num_pages = 2;

	kbase_gpu_vm_lock(kctx);

	vunmap(queue->user_io_addr);

	WARN_ON(num_pages > atomic_read(&kctx->permanent_mapped_pages));
	atomic_sub(num_pages, &kctx->permanent_mapped_pages);

	kbase_gpu_vm_unlock(kctx);
}

static int kernel_map_user_io_pages(struct kbase_context *kctx,
			struct kbase_queue *queue)
{
	struct page *page_list[2];
	pgprot_t cpu_map_prot;
	int ret = 0;
	size_t i;

	kbase_gpu_vm_lock(kctx);

	if (ARRAY_SIZE(page_list) > (KBASE_PERMANENTLY_MAPPED_MEM_LIMIT_PAGES -
			 atomic_read(&kctx->permanent_mapped_pages))) {
		ret = -ENOMEM;
		goto unlock;
	}

	/* The pages are mapped to Userspace also, so use the same mapping
	 * attributes as used inside the CPU page fault handler.
	 */
#if ((KERNEL_VERSION(4, 4, 147) >= LINUX_VERSION_CODE) || \
		((KERNEL_VERSION(4, 6, 0) > LINUX_VERSION_CODE) && \
		 (KERNEL_VERSION(4, 5, 0) <= LINUX_VERSION_CODE)))
	cpu_map_prot = pgprot_device(PAGE_KERNEL);
#else
	if (kctx->kbdev->system_coherency == COHERENCY_NONE)
		cpu_map_prot = pgprot_writecombine(PAGE_KERNEL);
	else
		cpu_map_prot = PAGE_KERNEL;
#endif

	for (i = 0; i < ARRAY_SIZE(page_list); i++)
		page_list[i] = as_page(queue->phys[i]);

	queue->user_io_addr = vmap(page_list, ARRAY_SIZE(page_list), VM_MAP, cpu_map_prot);

	if (!queue->user_io_addr)
		ret = -ENOMEM;
	else
		atomic_add(ARRAY_SIZE(page_list), &kctx->permanent_mapped_pages);

unlock:
	kbase_gpu_vm_unlock(kctx);
	return ret;
}

static void term_queue_group(struct kbase_queue_group *group);
static void get_queue(struct kbase_queue *queue);
static void release_queue(struct kbase_queue *queue);

/**
 * kbase_csf_free_command_stream_user_pages() - Free the resources allocated
 *				    for a queue at the time of bind.
 *
 * @kctx:	Address of the kbase context within which the queue was created.
 * @queue:	Pointer to the queue to be unlinked.
 *
 * This function will free the pair of physical pages allocated for a GPU
 * command queue, and also release the hardware doorbell page, that were mapped
 * into the process address space to enable direct submission of commands to
 * the hardware. Also releases the reference taken on the queue when the mapping
 * was created.
 *
 * This function will be called only when the mapping is being removed and
 * so the resources for queue will not get freed up until the mapping is
 * removed even though userspace could have terminated the queue.
 * Kernel will ensure that the termination of Kbase context would only be
 * triggered after the mapping is removed.
 *
 * If an explicit or implicit unbind was missed by the userspace then the
 * mapping will persist. On process exit kernel itself will remove the mapping.
 */
static void kbase_csf_free_command_stream_user_pages(struct kbase_context *kctx,
		struct kbase_queue *queue)
{
	const size_t num_pages = 2;

	gpu_munmap_user_io_pages(kctx, queue->reg);
	kernel_unmap_user_io_pages(kctx, queue);

	kbase_mem_pool_free_pages(
		&kctx->mem_pools.small[KBASE_MEM_GROUP_CSF_IO],
		num_pages, queue->phys, true, false);

	kfree(queue->reg);
	queue->reg = NULL;

	/* If the queue has already been terminated by userspace
	 * then the ref count for queue object will drop to 0 here.
	 */
	release_queue(queue);
}

int kbase_csf_alloc_command_stream_user_pages(struct kbase_context *kctx,
			struct kbase_queue *queue)
{
	struct kbase_device *kbdev = kctx->kbdev;
	struct kbase_va_region *reg;
	const size_t num_pages = 2;
	int ret;

	lockdep_assert_held(&kctx->csf.lock);

	reg = kbase_alloc_free_region(&kctx->kbdev->csf.shared_reg_rbtree, 0,
				      num_pages, KBASE_REG_ZONE_MCU_SHARED);
	if (!reg)
		return -ENOMEM;

	ret = kbase_mem_pool_alloc_pages(
				&kctx->mem_pools.small[KBASE_MEM_GROUP_CSF_IO],
				num_pages, queue->phys, false);

	if (ret != num_pages)
		goto phys_alloc_failed;

	ret = kernel_map_user_io_pages(kctx, queue);
	if (ret)
		goto kernel_map_failed;

	init_user_io_pages(queue);

	ret = gpu_mmap_user_io_pages(kctx->kbdev, queue->phys, reg);
	if (ret)
		goto gpu_mmap_failed;

	queue->reg = reg;

	mutex_lock(&kbdev->csf.reg_lock);
	if (kbdev->csf.db_file_offsets >
			(U32_MAX - BASEP_QUEUE_NR_MMAP_USER_PAGES + 1))
		kbdev->csf.db_file_offsets = 0;

	queue->db_file_offset = kbdev->csf.db_file_offsets;
	kbdev->csf.db_file_offsets += BASEP_QUEUE_NR_MMAP_USER_PAGES;

	WARN(atomic_read(&queue->refcount) != 1, "Incorrect refcounting for queue object\n");
	/* This is the second reference taken on the queue object and
	 * would be dropped only when the IO mapping is removed either
	 * explicitly by userspace or implicitly by kernel on process exit.
	 */
	get_queue(queue);
	queue->bind_state = KBASE_CSF_QUEUE_BOUND;
	mutex_unlock(&kbdev->csf.reg_lock);

	return 0;

gpu_mmap_failed:
	kernel_unmap_user_io_pages(kctx, queue);

kernel_map_failed:
	kbase_mem_pool_free_pages(
		&kctx->mem_pools.small[KBASE_MEM_GROUP_CSF_IO],
		num_pages, queue->phys, false, false);

phys_alloc_failed:
	kfree(reg);

	return -ENOMEM;
}

static struct kbase_queue_group *find_queue_group(struct kbase_context *kctx,
	u8 group_handle)
{
	uint index = group_handle;

	lockdep_assert_held(&kctx->csf.lock);

	if (index < MAX_QUEUE_GROUP_NUM && kctx->csf.queue_groups[index]) {
		if (WARN_ON(kctx->csf.queue_groups[index]->handle != index))
			return NULL;
		return kctx->csf.queue_groups[index];
	}

	return NULL;
}

int kbase_csf_queue_group_handle_is_valid(struct kbase_context *kctx,
	u8 group_handle)
{
	struct kbase_queue_group *group;

	mutex_lock(&kctx->csf.lock);
	group = find_queue_group(kctx, group_handle);
	mutex_unlock(&kctx->csf.lock);

	return group ? 0 : -EINVAL;
}

static struct kbase_queue *find_queue(struct kbase_context *kctx, u64 base_addr)
{
	struct kbase_queue *queue;

	lockdep_assert_held(&kctx->csf.lock);

	list_for_each_entry(queue, &kctx->csf.queue_list, link) {
		if (base_addr == queue->base_addr)
			return queue;
	}

	return NULL;
}

static void get_queue(struct kbase_queue *queue)
{
	WARN_ON(!atomic_inc_not_zero(&queue->refcount));
}

static void release_queue(struct kbase_queue *queue)
{
	lockdep_assert_held(&queue->kctx->csf.lock);

	WARN_ON(atomic_read(&queue->refcount) <= 0);

	if (atomic_dec_and_test(&queue->refcount)) {
		/* The queue can't still be on the per context list. */
		WARN_ON(!list_empty(&queue->link));
		WARN_ON(queue->group);
		kfree(queue);
	}
}

static void oom_event_worker(struct work_struct *data);
static void fatal_event_worker(struct work_struct *data);

/* Between reg and reg_ex, one and only one must be null */
static int csf_queue_register_internal(struct kbase_context *kctx,
			struct kbase_ioctl_cs_queue_register *reg,
			struct kbase_ioctl_cs_queue_register_ex *reg_ex)
{
	struct kbase_queue *queue;
	int ret = 0;
	struct kbase_va_region *region;
	u64 queue_addr;
	size_t queue_size;

	/* Only one pointer expected, otherwise coding error */
	if ((reg == NULL && reg_ex == NULL) || (reg && reg_ex)) {
		dev_err(kctx->kbdev->dev,
			"Error, one and only one param-ptr expected!");
		return -EINVAL;
	}

	/* struct kbase_ioctl_cs_queue_register_ex contains a full
	 * struct kbase_ioctl_cs_queue_register at the start address. So
	 * the pointer can be safely cast to pointing to a
	 * kbase_ioctl_cs_queue_register object.
	 */
	if (reg_ex)
		reg = (struct kbase_ioctl_cs_queue_register *)reg_ex;

	/* Validate the queue priority */
	if (reg->priority > BASE_QUEUE_MAX_PRIORITY)
		return -EINVAL;

	queue_addr = reg->buffer_gpu_addr;
	queue_size = reg->buffer_size >> PAGE_SHIFT;

	mutex_lock(&kctx->csf.lock);

	/* Check if queue is already registered */
	if (find_queue(kctx, queue_addr) != NULL) {
		ret = -EINVAL;
		goto out;
	}

	/* Check if the queue address is valid */
	kbase_gpu_vm_lock(kctx);
	region = kbase_region_tracker_find_region_enclosing_address(kctx,
								    queue_addr);

	if (kbase_is_region_invalid_or_free(region)) {
		ret = -ENOENT;
		goto out_unlock_vm;
	}

	if (queue_size > (region->nr_pages -
			  ((queue_addr >> PAGE_SHIFT) - region->start_pfn))) {
		ret = -EINVAL;
		goto out_unlock_vm;
	}

	/* Check address validity on cs_trace buffer etc. Don't care
	 * if not enabled (i.e. when size is 0).
	 */
	if (reg_ex && reg_ex->ex_buffer_size) {
		int buf_pages = (reg_ex->ex_buffer_size +
				 (1 << PAGE_SHIFT) - 1) >> PAGE_SHIFT;

		region = kbase_region_tracker_find_region_enclosing_address(
				kctx, reg_ex->ex_buffer_base);
		if (kbase_is_region_invalid_or_free(region)) {
			ret = -ENOENT;
			goto out_unlock_vm;
		}

		if (buf_pages > (region->nr_pages -
				 ((reg_ex->ex_buffer_base >> PAGE_SHIFT) -
				 region->start_pfn))) {
			ret = -EINVAL;
			goto out_unlock_vm;
		}

		region = kbase_region_tracker_find_region_enclosing_address(
				kctx, reg_ex->ex_offset_var_addr);
		if (kbase_is_region_invalid_or_free(region)) {
			ret = -ENOENT;
			goto out_unlock_vm;
		}
	}

	queue = kzalloc(sizeof(struct kbase_queue), GFP_KERNEL);

	if (!queue) {
		ret = -ENOMEM;
		goto out_unlock_vm;
	}

	queue->kctx = kctx;
	queue->base_addr = queue_addr;
	queue->queue_reg = region;
	queue->size = (queue_size << PAGE_SHIFT);
	queue->csi_index = KBASEP_IF_NR_INVALID;
	queue->enabled = false;

	queue->priority = reg->priority;
	atomic_set(&queue->refcount, 1);

	queue->group = NULL;
	queue->bind_state = KBASE_CSF_QUEUE_UNBOUND;
	queue->handle = BASEP_MEM_INVALID_HANDLE;
	queue->doorbell_nr = KBASEP_USER_DB_NR_INVALID;

	queue->status_wait = 0;
	queue->sync_ptr = 0;
	queue->sync_value = 0;

	queue->sb_status = 0;
	queue->blocked_reason = CS_STATUS_BLOCKED_REASON_REASON_UNBLOCKED;

	INIT_LIST_HEAD(&queue->link);
	INIT_LIST_HEAD(&queue->error.link);
	INIT_WORK(&queue->oom_event_work, oom_event_worker);
	INIT_WORK(&queue->fatal_event_work, fatal_event_worker);
	list_add(&queue->link, &kctx->csf.queue_list);

	region->flags |= KBASE_REG_NO_USER_FREE;

	/* Initialize the cs_trace configuration parameters, When buffer_size
	 * is 0, trace is disabled. Here we only update the fields when
	 * enabled, otherwise leave them as default zeros.
	 */
	if (reg_ex && reg_ex->ex_buffer_size) {
		u32 cfg = CS_INSTR_CONFIG_EVENT_SIZE_SET(
					0, reg_ex->ex_event_size);
		cfg = CS_INSTR_CONFIG_EVENT_STATE_SET(
					cfg, reg_ex->ex_event_state);

		queue->trace_cfg = cfg;
		queue->trace_buffer_size = reg_ex->ex_buffer_size;
		queue->trace_buffer_base = reg_ex->ex_buffer_base;
		queue->trace_offset_ptr = reg_ex->ex_offset_var_addr;
	}

out_unlock_vm:
	kbase_gpu_vm_unlock(kctx);
out:
	mutex_unlock(&kctx->csf.lock);

	return ret;
}

int kbase_csf_queue_register(struct kbase_context *kctx,
			     struct kbase_ioctl_cs_queue_register *reg)
{
	return csf_queue_register_internal(kctx, reg, NULL);
}

int kbase_csf_queue_register_ex(struct kbase_context *kctx,
				struct kbase_ioctl_cs_queue_register_ex *reg)
{
	struct kbase_csf_global_iface const *const iface =
						&kctx->kbdev->csf.global_iface;
	u32 const glb_version = iface->version;
	u32 instr = iface->instr_features;
	u8 max_size = GLB_INSTR_FEATURES_EVENT_SIZE_MAX_GET(instr);
	u32 min_buf_size = (1u << reg->ex_event_size) *
			GLB_INSTR_FEATURES_OFFSET_UPDATE_RATE_GET(instr);

	/* If cs_trace_command not supported, the call fails */
	if (glb_version < kbase_csf_interface_version(1, 1, 0))
		return -EINVAL;

	/* Validate the cs_trace configuration parameters */
        if (reg->ex_buffer_size &&
            ((reg->ex_event_size > max_size) ||
             (reg->ex_buffer_size & (reg->ex_buffer_size - 1)) ||
             (reg->ex_buffer_size < min_buf_size)))
          return -EINVAL;

        return csf_queue_register_internal(kctx, NULL, reg);
}

static void unbind_queue(struct kbase_context *kctx,
		struct kbase_queue *queue);

void kbase_csf_queue_terminate(struct kbase_context *kctx,
			      struct kbase_ioctl_cs_queue_terminate *term)
{
	struct kbase_device *kbdev = kctx->kbdev;
	struct kbase_queue *queue;
	int err;
	bool reset_prevented = false;

	err = kbase_reset_gpu_prevent_and_wait(kbdev);
	if (err)
		dev_warn(
			kbdev->dev,
			"Unsuccessful GPU reset detected when terminating queue (buffer_addr=0x%.16llx), attempting to terminate regardless",
			term->buffer_gpu_addr);
	else
		reset_prevented = true;

	mutex_lock(&kctx->csf.lock);
	queue = find_queue(kctx, term->buffer_gpu_addr);

	if (queue) {
		unsigned long flags;

		/* As the GPU queue has been terminated by the
		 * user space, undo the actions that were performed when the
		 * queue was registered i.e. remove the queue from the per
		 * context list & release the initial reference. The subsequent
		 * lookups for the queue in find_queue() would fail.
		 */
		list_del_init(&queue->link);

		/* Stop the CSI to which queue was bound */
		unbind_queue(kctx, queue);

		kbase_gpu_vm_lock(kctx);
		if (!WARN_ON(!queue->queue_reg)) {
			/* After this the Userspace would be able to free the
			 * memory for GPU queue. In case the Userspace missed
			 * terminating the queue, the cleanup will happen on
			 * context termination where teardown of region tracker
			 * would free up the GPU queue memory.
			 */
			queue->queue_reg->flags &= ~KBASE_REG_NO_USER_FREE;
		}
		kbase_gpu_vm_unlock(kctx);

		spin_lock_irqsave(&kctx->csf.event_lock, flags);
		dev_dbg(kctx->kbdev->dev,
			"Remove any pending command queue fatal from context %pK\n",
			(void *)kctx);
		list_del_init(&queue->error.link);
		spin_unlock_irqrestore(&kctx->csf.event_lock, flags);

		release_queue(queue);
	}

	mutex_unlock(&kctx->csf.lock);
	if (reset_prevented)
		kbase_reset_gpu_allow(kbdev);
}

int kbase_csf_queue_bind(struct kbase_context *kctx, union kbase_ioctl_cs_queue_bind *bind)
{
	struct kbase_queue *queue;
	struct kbase_queue_group *group;
	u8 max_streams;
	int ret = -EINVAL;

	mutex_lock(&kctx->csf.lock);

	group = find_queue_group(kctx, bind->in.group_handle);
	queue = find_queue(kctx, bind->in.buffer_gpu_addr);

	if (!group || !queue)
		goto out;

	/* For the time being, all CSGs have the same number of CSs
	 * so we check CSG 0 for this number
	 */
	max_streams = kctx->kbdev->csf.global_iface.groups[0].stream_num;

	if (bind->in.csi_index >= max_streams)
		goto out;

	if (group->run_state == KBASE_CSF_GROUP_TERMINATED)
		goto out;

	if (queue->group || group->bound_queues[bind->in.csi_index])
		goto out;

	ret = get_user_pages_mmap_handle(kctx, queue);
	if (ret)
		goto out;

	bind->out.mmap_handle = queue->handle;
	group->bound_queues[bind->in.csi_index] = queue;
	queue->group = group;
	queue->csi_index = bind->in.csi_index;
	queue->bind_state = KBASE_CSF_QUEUE_BIND_IN_PROGRESS;

out:
	mutex_unlock(&kctx->csf.lock);

	return ret;
}

static struct kbase_queue_group *get_bound_queue_group(
					struct kbase_queue *queue)
{
	struct kbase_context *kctx = queue->kctx;
	struct kbase_queue_group *group;

	if (queue->bind_state == KBASE_CSF_QUEUE_UNBOUND)
		return NULL;

	if (!queue->group)
		return NULL;

	if (queue->csi_index == KBASEP_IF_NR_INVALID) {
		dev_warn(kctx->kbdev->dev, "CS interface index is incorrect\n");
		return NULL;
	}

	group = queue->group;

	if (group->bound_queues[queue->csi_index] != queue) {
		dev_warn(kctx->kbdev->dev, "Incorrect mapping between queues & queue groups\n");
		return NULL;
	}

	return group;
}

void kbase_csf_ring_csg_doorbell(struct kbase_device *kbdev, int slot)
{
	if (WARN_ON(slot < 0))
		return;

	kbase_csf_ring_csg_slots_doorbell(kbdev, (u32) (1 << slot));
}

void kbase_csf_ring_csg_slots_doorbell(struct kbase_device *kbdev,
				       u32 slot_bitmap)
{
	const struct kbase_csf_global_iface *const global_iface =
		&kbdev->csf.global_iface;
	const u32 allowed_bitmap =
		(u32) ((1U << kbdev->csf.global_iface.group_num) - 1);
	u32 value;

	if (WARN_ON(slot_bitmap > allowed_bitmap))
		return;

	value = kbase_csf_firmware_global_output(global_iface, GLB_DB_ACK);
	value ^= slot_bitmap;
	kbase_csf_firmware_global_input_mask(global_iface, GLB_DB_REQ, value,
					     slot_bitmap);

	kbase_csf_ring_doorbell(kbdev, CSF_KERNEL_DOORBELL_NR);
}

void kbase_csf_ring_cs_user_doorbell(struct kbase_device *kbdev,
			struct kbase_queue *queue)
{
	mutex_lock(&kbdev->csf.reg_lock);

	if (queue->doorbell_nr != KBASEP_USER_DB_NR_INVALID)
		kbase_csf_ring_doorbell(kbdev, queue->doorbell_nr);

	mutex_unlock(&kbdev->csf.reg_lock);
}

void kbase_csf_ring_cs_kernel_doorbell(struct kbase_device *kbdev,
				       int csi_index, int csg_nr,
				       bool ring_csg_doorbell)
{
	struct kbase_csf_cmd_stream_group_info *ginfo;
	u32 value;

	if (WARN_ON(csg_nr < 0) ||
	    WARN_ON(csg_nr >= kbdev->csf.global_iface.group_num))
		return;

	ginfo = &kbdev->csf.global_iface.groups[csg_nr];

	if (WARN_ON(csi_index < 0) ||
	    WARN_ON(csi_index >= ginfo->stream_num))
		return;

	value = kbase_csf_firmware_csg_output(ginfo, CSG_DB_ACK);
	value ^= (1 << csi_index);
	kbase_csf_firmware_csg_input_mask(ginfo, CSG_DB_REQ, value,
					  1 << csi_index);

	if (likely(ring_csg_doorbell))
		kbase_csf_ring_csg_doorbell(kbdev, csg_nr);
}

int kbase_csf_queue_kick(struct kbase_context *kctx,
			 struct kbase_ioctl_cs_queue_kick *kick)
{
	struct kbase_device *kbdev = kctx->kbdev;
	struct kbase_queue_group *group;
	struct kbase_queue *queue;
	int err = 0;

	err = kbase_reset_gpu_prevent_and_wait(kbdev);
	if (err) {
		dev_warn(
			kbdev->dev,
			"Unsuccessful GPU reset detected when kicking queue (buffer_addr=0x%.16llx)",
			kick->buffer_gpu_addr);
		return err;
	}

	mutex_lock(&kctx->csf.lock);
	queue = find_queue(kctx, kick->buffer_gpu_addr);
	if (!queue)
		err = -EINVAL;

	if (!err) {
		group = get_bound_queue_group(queue);
		if (!group) {
			dev_err(kctx->kbdev->dev, "queue not bound\n");
			err = -EINVAL;
		}
	}

	if (!err)
		err = kbase_csf_scheduler_queue_start(queue);
	mutex_unlock(&kctx->csf.lock);
	kbase_reset_gpu_allow(kbdev);

	return err;
}

static void unbind_stopped_queue(struct kbase_context *kctx,
			struct kbase_queue *queue)
{
	lockdep_assert_held(&kctx->csf.lock);

	if (queue->bind_state != KBASE_CSF_QUEUE_UNBOUND) {
		unsigned long flags;

		kbase_csf_scheduler_spin_lock(kctx->kbdev, &flags);
		bitmap_clear(queue->group->protm_pending_bitmap,
				queue->csi_index, 1);
		KBASE_KTRACE_ADD_CSF_GRP_Q(kctx->kbdev, PROTM_PENDING_CLEAR,
			 queue->group, queue, queue->group->protm_pending_bitmap[0]);
		queue->group->bound_queues[queue->csi_index] = NULL;
		queue->group = NULL;
		kbase_csf_scheduler_spin_unlock(kctx->kbdev, flags);

		put_user_pages_mmap_handle(kctx, queue);
		queue->bind_state = KBASE_CSF_QUEUE_UNBOUND;
	}
}
/**
 * unbind_queue() - Remove the linkage between a GPU command queue and the group
 *		    to which it was bound or being bound.
 *
 * @kctx:	Address of the kbase context within which the queue was created.
 * @queue:	Pointer to the queue to be unlinked.
 *
 * This function will also send the stop request to firmware for the CS
 * if the group to which the GPU command queue was bound is scheduled.
 *
 * This function would be called when :-
 * - queue is being unbound. This would happen when the IO mapping
 *   created on bind is removed explicitly by userspace or the process
 *   is getting exited.
 * - queue group is being terminated which still has queues bound
 *   to it. This could happen on an explicit terminate request from userspace
 *   or when the kbase context is being terminated.
 * - queue is being terminated without completing the bind operation.
 *   This could happen if either the queue group is terminated
 *   after the CS_QUEUE_BIND ioctl but before the 2nd part of bind operation
 *   to create the IO mapping is initiated.
 * - There is a failure in executing the 2nd part of bind operation, inside the
 *   mmap handler, which creates the IO mapping for queue.
 */

static void unbind_queue(struct kbase_context *kctx, struct kbase_queue *queue)
{
	kbase_reset_gpu_assert_failed_or_prevented(kctx->kbdev);
	lockdep_assert_held(&kctx->csf.lock);

	if (queue->bind_state != KBASE_CSF_QUEUE_UNBOUND) {
		if (queue->bind_state == KBASE_CSF_QUEUE_BOUND)
			kbase_csf_scheduler_queue_stop(queue);

		unbind_stopped_queue(kctx, queue);
	}
}

void kbase_csf_queue_unbind(struct kbase_queue *queue)
{
	struct kbase_context *kctx = queue->kctx;

	lockdep_assert_held(&kctx->csf.lock);

	/* As the process itself is exiting, the termination of queue group can
	 * be done which would be much faster than stopping of individual
	 * queues. This would ensure a faster exit for the process especially
	 * in the case where CSI gets stuck.
	 * The CSI STOP request will wait for the in flight work to drain
	 * whereas CSG TERM request would result in an immediate abort or
	 * cancellation of the pending work.
	 */
	if (current->flags & PF_EXITING) {
		struct kbase_queue_group *group = get_bound_queue_group(queue);

		if (group)
			term_queue_group(group);

		WARN_ON(queue->bind_state != KBASE_CSF_QUEUE_UNBOUND);
	} else {
		unbind_queue(kctx, queue);
	}

	/* Free the resources, if allocated for this queue. */
	if (queue->reg)
		kbase_csf_free_command_stream_user_pages(kctx, queue);
}

void kbase_csf_queue_unbind_stopped(struct kbase_queue *queue)
{
	struct kbase_context *kctx = queue->kctx;

	lockdep_assert_held(&kctx->csf.lock);

	WARN_ON(queue->bind_state == KBASE_CSF_QUEUE_BOUND);
	unbind_stopped_queue(kctx, queue);

	/* Free the resources, if allocated for this queue. */
	if (queue->reg)
		kbase_csf_free_command_stream_user_pages(kctx, queue);
}

/**
 * find_free_group_handle() - Find a free handle for a queue group
 *
 * @kctx: Address of the kbase context within which the queue group
 *        is to be created.
 *
 * Return: a queue group handle on success, or a negative error code on failure.
 */
static int find_free_group_handle(struct kbase_context *const kctx)
{
	/* find the available index in the array of CSGs per this context */
	int idx, group_handle = -ENOMEM;

	lockdep_assert_held(&kctx->csf.lock);

	for (idx = 0;
		(idx != MAX_QUEUE_GROUP_NUM) && (group_handle < 0);
		idx++) {
		if (!kctx->csf.queue_groups[idx])
			group_handle = idx;
	}

	return group_handle;
}

/**
 * iface_has_enough_streams() - Check that at least one CSG supports
 *                              a given number of CS
 *
 * @kbdev:  Instance of a GPU platform device that implements a CSF interface.
 * @cs_min: Minimum number of CSs required.
 *
 * Return: true if at least one CSG supports the given number
 *         of CSs (or more); otherwise false.
 */
static bool iface_has_enough_streams(struct kbase_device *const kbdev,
	u32 const cs_min)
{
	bool has_enough = false;
	struct kbase_csf_cmd_stream_group_info *const groups =
		kbdev->csf.global_iface.groups;
	const u32 group_num = kbdev->csf.global_iface.group_num;
	u32 i;

	for (i = 0; (i < group_num) && !has_enough; i++) {
		if (groups[i].stream_num >= cs_min)
			has_enough = true;
	}

	return has_enough;
}

/**
 * create_normal_suspend_buffer() - Create normal-mode suspend buffer per
 *					queue group
 *
 * @kctx:	Pointer to kbase context where the queue group is created at
 * @s_buf:	Pointer to suspend buffer that is attached to queue group
 *
 * Return: 0 if suspend buffer is successfully allocated and reflected to GPU
 *         MMU page table. Otherwise -ENOMEM.
 */
static int create_normal_suspend_buffer(struct kbase_context *const kctx,
		struct kbase_normal_suspend_buffer *s_buf)
{
	struct kbase_va_region *reg = NULL;
	const unsigned long mem_flags = KBASE_REG_GPU_RD | KBASE_REG_GPU_WR;
	const size_t nr_pages =
		PFN_UP(kctx->kbdev->csf.global_iface.groups[0].suspend_size);
	int err = 0;

	lockdep_assert_held(&kctx->csf.lock);

	/* Allocate and initialize Region Object */
	reg = kbase_alloc_free_region(&kctx->kbdev->csf.shared_reg_rbtree, 0,
			nr_pages, KBASE_REG_ZONE_MCU_SHARED);

	if (!reg)
		return -ENOMEM;

	s_buf->phy = kcalloc(nr_pages, sizeof(*s_buf->phy), GFP_KERNEL);

	if (!s_buf->phy) {
		err = -ENOMEM;
		goto phy_alloc_failed;
	}

	/* Get physical page for a normal suspend buffer */
	err = kbase_mem_pool_alloc_pages(
			&kctx->mem_pools.small[KBASE_MEM_GROUP_CSF_FW],
			nr_pages, &s_buf->phy[0], false);

	if (err < 0)
		goto phy_pages_alloc_failed;

	/* Insert Region Object into rbtree and make virtual address available
	 * to map it to physical page
	 */
	mutex_lock(&kctx->kbdev->csf.reg_lock);
	err = kbase_add_va_region_rbtree(kctx->kbdev, reg, 0, nr_pages, 1);
	reg->flags &= ~KBASE_REG_FREE;
	mutex_unlock(&kctx->kbdev->csf.reg_lock);

	if (err)
		goto add_va_region_failed;

	/* Update MMU table */
	err = kbase_mmu_insert_pages(kctx->kbdev, &kctx->kbdev->csf.mcu_mmu,
				     reg->start_pfn, &s_buf->phy[0],
				     nr_pages, mem_flags,
				     MCU_AS_NR, KBASE_MEM_GROUP_CSF_FW);
	if (err)
		goto mmu_insert_failed;

	s_buf->reg = reg;

	return 0;

mmu_insert_failed:
	mutex_lock(&kctx->kbdev->csf.reg_lock);
	WARN_ON(kbase_remove_va_region(reg));
	mutex_unlock(&kctx->kbdev->csf.reg_lock);

add_va_region_failed:
	kbase_mem_pool_free_pages(
		&kctx->mem_pools.small[KBASE_MEM_GROUP_CSF_FW], nr_pages,
		&s_buf->phy[0], false, false);

phy_pages_alloc_failed:
	kfree(s_buf->phy);
phy_alloc_failed:
	kfree(reg);

	return err;
}

/**
 * create_protected_suspend_buffer() - Create protected-mode suspend buffer
 *					per queue group
 *
 * @kbdev: Instance of a GPU platform device that implements a CSF interface.
 * @s_buf: Pointer to suspend buffer that is attached to queue group
 *
 * Return: 0 if suspend buffer is successfully allocated and reflected to GPU
 *         MMU page table. Otherwise -ENOMEM.
 */
static int create_protected_suspend_buffer(struct kbase_device *const kbdev,
		struct kbase_protected_suspend_buffer *s_buf)
{
	struct kbase_va_region *reg = NULL;
	struct tagged_addr *phys = NULL;
	const unsigned long mem_flags = KBASE_REG_GPU_RD | KBASE_REG_GPU_WR;
	const size_t nr_pages =
		PFN_UP(kbdev->csf.global_iface.groups[0].suspend_size);
	int err = 0;

	/* Allocate and initialize Region Object */
	reg = kbase_alloc_free_region(&kbdev->csf.shared_reg_rbtree, 0,
			nr_pages, KBASE_REG_ZONE_MCU_SHARED);

	if (!reg)
		return -ENOMEM;

	phys = kcalloc(nr_pages, sizeof(*phys), GFP_KERNEL);
	if (!phys) {
		err = -ENOMEM;
		goto phy_alloc_failed;
	}

	s_buf->pma = kbase_csf_protected_memory_alloc(kbdev, phys,
			nr_pages);
	if (s_buf->pma == NULL) {
		err = -ENOMEM;
		goto pma_alloc_failed;
	}

	/* Insert Region Object into rbtree and make virtual address available
	 * to map it to physical page
	 */
	mutex_lock(&kbdev->csf.reg_lock);
	err = kbase_add_va_region_rbtree(kbdev, reg, 0, nr_pages, 1);
	reg->flags &= ~KBASE_REG_FREE;
	mutex_unlock(&kbdev->csf.reg_lock);

	if (err)
		goto add_va_region_failed;

	/* Update MMU table */
	err = kbase_mmu_insert_pages(kbdev, &kbdev->csf.mcu_mmu,
				     reg->start_pfn, phys,
				     nr_pages, mem_flags, MCU_AS_NR,
				     KBASE_MEM_GROUP_CSF_FW);
	if (err)
		goto mmu_insert_failed;

	s_buf->reg = reg;
	kfree(phys);
	return 0;

mmu_insert_failed:
	mutex_lock(&kbdev->csf.reg_lock);
	WARN_ON(kbase_remove_va_region(reg));
	mutex_unlock(&kbdev->csf.reg_lock);

add_va_region_failed:
	kbase_csf_protected_memory_free(kbdev, s_buf->pma, nr_pages);
pma_alloc_failed:
	kfree(phys);
phy_alloc_failed:
	kfree(reg);

	return err;
}

static void timer_event_worker(struct work_struct *data);
static void protm_event_worker(struct work_struct *data);
static void term_normal_suspend_buffer(struct kbase_context *const kctx,
		struct kbase_normal_suspend_buffer *s_buf);

/**
 * create_suspend_buffers - Setup normal and protected mode
 *				suspend buffers.
 *
 * @kctx:	Address of the kbase context within which the queue group
 *		is to be created.
 * @group:	Pointer to GPU command queue group data.
 *
 * Return: 0 if suspend buffers are successfully allocated. Otherwise -ENOMEM.
 */
static int create_suspend_buffers(struct kbase_context *const kctx,
		struct kbase_queue_group * const group)
{
	int err = 0;

	if (create_normal_suspend_buffer(kctx, &group->normal_suspend_buf)) {
		dev_err(kctx->kbdev->dev, "Failed to create normal suspend buffer\n");
		return -ENOMEM;
	}

	if (kctx->kbdev->csf.pma_dev) {
		err = create_protected_suspend_buffer(kctx->kbdev,
				&group->protected_suspend_buf);
		if (err) {
			term_normal_suspend_buffer(kctx,
					&group->normal_suspend_buf);
			dev_err(kctx->kbdev->dev, "Failed to create protected suspend buffer\n");
		}
	} else {
		group->protected_suspend_buf.reg = NULL;
	}

	return err;
}

/**
 * generate_group_uid() - Makes an ID unique to all kernel base devices
 *                        and contexts, for a queue group and CSG.
 *
 * Return:      A unique ID in the form of an unsigned 32-bit integer
 */
static u32 generate_group_uid(void)
{
	/* use first KBase device to store max UID */
	struct kbase_device *kbdev = kbase_find_device(-1);
	u32 uid = 1;

	if (kbdev)
		uid = (u32) atomic_inc_return(&kbdev->group_max_uid_in_devices);
	else
		WARN(1, "NULL kbase device pointer in group UID generation");

	return uid;
}

/**
 * create_queue_group() - Create a queue group
 *
 * @kctx:	Address of the kbase context within which the queue group
 *		is to be created.
 * @create:	Address of a structure which contains details of the
 *		queue group which is to be created.
 *
 * Return: a queue group handle on success, or a negative error code on failure.
 */
static int create_queue_group(struct kbase_context *const kctx,
	union kbase_ioctl_cs_queue_group_create *const create)
{
	int group_handle = find_free_group_handle(kctx);

	if (group_handle < 0) {
		dev_err(kctx->kbdev->dev,
			"All queue group handles are already in use\n");
	} else {
		struct kbase_queue_group * const group =
			kmalloc(sizeof(struct kbase_queue_group),
					GFP_KERNEL);

		lockdep_assert_held(&kctx->csf.lock);

		if (!group) {
			dev_err(kctx->kbdev->dev, "Failed to allocate a queue\n");
			group_handle = -ENOMEM;
		} else {
			int err = 0;

			group->kctx = kctx;
			group->handle = group_handle;
			group->csg_nr = KBASEP_CSG_NR_INVALID;

			group->tiler_mask = create->in.tiler_mask;
			group->fragment_mask = create->in.fragment_mask;
			group->compute_mask = create->in.compute_mask;

			group->tiler_max = create->in.tiler_max;
			group->fragment_max = create->in.fragment_max;
			group->compute_max = create->in.compute_max;
			group->priority = kbase_csf_priority_queue_group_priority_to_relative(
				kbase_csf_priority_check(kctx->kbdev, create->in.priority));
			group->doorbell_nr = KBASEP_USER_DB_NR_INVALID;
			group->faulted = false;

			group->group_uid = generate_group_uid();
			create->out.group_uid = group->group_uid;

			INIT_LIST_HEAD(&group->link);
			INIT_LIST_HEAD(&group->link_to_schedule);
			INIT_LIST_HEAD(&group->error_fatal.link);
			INIT_LIST_HEAD(&group->error_timeout.link);
			INIT_LIST_HEAD(&group->error_tiler_oom.link);
			INIT_WORK(&group->timer_event_work, timer_event_worker);
			INIT_WORK(&group->protm_event_work, protm_event_worker);
			bitmap_zero(group->protm_pending_bitmap,
					MAX_SUPPORTED_STREAMS_PER_GROUP);

			group->run_state = KBASE_CSF_GROUP_INACTIVE;
			err = create_suspend_buffers(kctx, group);

			if (err < 0) {
				kfree(group);
				group_handle = err;
			} else {
				int j;

				kctx->csf.queue_groups[group_handle] = group;
				for (j = 0; j < MAX_SUPPORTED_STREAMS_PER_GROUP;
						j++)
					group->bound_queues[j] = NULL;
			}
		}
	}

	return group_handle;
}

int kbase_csf_queue_group_create(struct kbase_context *const kctx,
			union kbase_ioctl_cs_queue_group_create *const create)
{
	int err = 0;
	const u32 tiler_count = hweight64(create->in.tiler_mask);
	const u32 fragment_count = hweight64(create->in.fragment_mask);
	const u32 compute_count = hweight64(create->in.compute_mask);

	mutex_lock(&kctx->csf.lock);

	if ((create->in.tiler_max > tiler_count) ||
	    (create->in.fragment_max > fragment_count) ||
	    (create->in.compute_max > compute_count)) {
		dev_err(kctx->kbdev->dev,
			"Invalid maximum number of endpoints for a queue group\n");
		err = -EINVAL;
	} else if (create->in.priority >= BASE_QUEUE_GROUP_PRIORITY_COUNT) {
		dev_err(kctx->kbdev->dev, "Invalid queue group priority %u\n",
			(unsigned int)create->in.priority);
		err = -EINVAL;
	} else if (!iface_has_enough_streams(kctx->kbdev, create->in.cs_min)) {
		dev_err(kctx->kbdev->dev,
			"No CSG has at least %d CSs\n",
			create->in.cs_min);
		err = -EINVAL;
	} else {
		/* For the CSG which satisfies the condition for having
		 * the needed number of CSs, check whether it also conforms
		 * with the requirements for at least one of its CSs having
		 * the iterator of the needed type
		 * (note: for CSF v1.0 all CSs in a CSG will have access to
		 * the same iterators)
		 */
		const int group_handle = create_queue_group(kctx, create);

		if (group_handle >= 0)
			create->out.group_handle = group_handle;
		else
			err = group_handle;
	}

	mutex_unlock(&kctx->csf.lock);

	return err;
}

/**
 * term_normal_suspend_buffer() - Free normal-mode suspend buffer of queue group
 *
 * @kctx:	Pointer to kbase context where queue group belongs to
 * @s_buf:	Pointer to queue group suspend buffer to be freed
 */
static void term_normal_suspend_buffer(struct kbase_context *const kctx,
		struct kbase_normal_suspend_buffer *s_buf)
{
	const size_t nr_pages =
		PFN_UP(kctx->kbdev->csf.global_iface.groups[0].suspend_size);

	lockdep_assert_held(&kctx->csf.lock);

	WARN_ON(kbase_mmu_teardown_pages(
				kctx->kbdev, &kctx->kbdev->csf.mcu_mmu,
				s_buf->reg->start_pfn, nr_pages, MCU_AS_NR));

	WARN_ON(s_buf->reg->flags & KBASE_REG_FREE);

	mutex_lock(&kctx->kbdev->csf.reg_lock);
	WARN_ON(kbase_remove_va_region(s_buf->reg));
	mutex_unlock(&kctx->kbdev->csf.reg_lock);

	kbase_mem_pool_free_pages(
			&kctx->mem_pools.small[KBASE_MEM_GROUP_CSF_FW],
			nr_pages, &s_buf->phy[0], false, false);

	kfree(s_buf->phy);
	s_buf->phy = NULL;
	kfree(s_buf->reg);
	s_buf->reg = NULL;
}

/**
 * term_protected_suspend_buffer() - Free normal-mode suspend buffer of
 *					queue group
 *
 * @kbdev: Instance of a GPU platform device that implements a CSF interface.
 * @s_buf: Pointer to queue group suspend buffer to be freed
 */
static void term_protected_suspend_buffer(struct kbase_device *const kbdev,
		struct kbase_protected_suspend_buffer *s_buf)
{
	const size_t nr_pages =
		PFN_UP(kbdev->csf.global_iface.groups[0].suspend_size);

	WARN_ON(kbase_mmu_teardown_pages(
			kbdev, &kbdev->csf.mcu_mmu,
			s_buf->reg->start_pfn, nr_pages, MCU_AS_NR));

	WARN_ON(s_buf->reg->flags & KBASE_REG_FREE);

	mutex_lock(&kbdev->csf.reg_lock);
	WARN_ON(kbase_remove_va_region(s_buf->reg));
	mutex_unlock(&kbdev->csf.reg_lock);

	kbase_csf_protected_memory_free(kbdev, s_buf->pma, nr_pages);
	s_buf->pma = NULL;
	kfree(s_buf->reg);
	s_buf->reg = NULL;
}

void kbase_csf_term_descheduled_queue_group(struct kbase_queue_group *group)
{
	struct kbase_context *kctx = group->kctx;

	/* Currently each group supports the same number of CS */
	u32 max_streams =
		kctx->kbdev->csf.global_iface.groups[0].stream_num;
	u32 i;

	lockdep_assert_held(&kctx->csf.lock);

	WARN_ON(group->run_state != KBASE_CSF_GROUP_INACTIVE &&
		group->run_state != KBASE_CSF_GROUP_FAULT_EVICTED);

	for (i = 0; i < max_streams; i++) {
		struct kbase_queue *queue =
				group->bound_queues[i];

		/* The group is already being evicted from the scheduler */
		if (queue)
			unbind_stopped_queue(kctx, queue);
	}

	term_normal_suspend_buffer(kctx, &group->normal_suspend_buf);
	if (kctx->kbdev->csf.pma_dev)
		term_protected_suspend_buffer(kctx->kbdev,
			&group->protected_suspend_buf);

	group->run_state = KBASE_CSF_GROUP_TERMINATED;
}

/**
 * term_queue_group - Terminate a GPU command queue group.
 *
 * @group: Pointer to GPU command queue group data.
 *
 * Terminates a GPU command queue group. From the userspace perspective the
 * group will still exist but it can't bind new queues to it. Userspace can
 * still add work in queues bound to the group but it won't be executed. (This
 * is because the IO mapping created upon binding such queues is still intact.)
 */
static void term_queue_group(struct kbase_queue_group *group)
{
	struct kbase_context *kctx = group->kctx;

	kbase_reset_gpu_assert_failed_or_prevented(kctx->kbdev);
	lockdep_assert_held(&kctx->csf.lock);

	/* Stop the group and evict it from the scheduler */
	kbase_csf_scheduler_group_deschedule(group);

	if (group->run_state == KBASE_CSF_GROUP_TERMINATED)
		return;

	dev_dbg(kctx->kbdev->dev, "group %d terminating", group->handle);

	kbase_csf_term_descheduled_queue_group(group);
}

static void cancel_queue_group_events(struct kbase_queue_group *group)
{
	cancel_work_sync(&group->timer_event_work);
	cancel_work_sync(&group->protm_event_work);
}

void kbase_csf_queue_group_terminate(struct kbase_context *kctx,
				     u8 group_handle)
{
	struct kbase_queue_group *group;
	int err;
	bool reset_prevented = false;
	struct kbase_device *const kbdev = kctx->kbdev;

	err = kbase_reset_gpu_prevent_and_wait(kbdev);
	if (err)
		dev_warn(
			kbdev->dev,
			"Unsuccessful GPU reset detected when terminating group %d, attempting to terminate regardless",
			group_handle);
	else
		reset_prevented = true;

	mutex_lock(&kctx->csf.lock);

	group = find_queue_group(kctx, group_handle);

	if (group) {
		unsigned long flags;

		spin_lock_irqsave(&kctx->csf.event_lock, flags);

		dev_dbg(kbdev->dev,
			"Remove any pending group fatal error from context %pK\n",
			(void *)group->kctx);

		list_del_init(&group->error_tiler_oom.link);
		list_del_init(&group->error_timeout.link);
		list_del_init(&group->error_fatal.link);
		spin_unlock_irqrestore(&kctx->csf.event_lock, flags);

		term_queue_group(group);
		kctx->csf.queue_groups[group_handle] = NULL;
	}

	mutex_unlock(&kctx->csf.lock);
	if (reset_prevented)
		kbase_reset_gpu_allow(kbdev);

	if (!group)
		return;

	/* Cancel any pending event callbacks. If one is in progress
	 * then this thread waits synchronously for it to complete (which
	 * is why we must unlock the context first). We already ensured
	 * that no more callbacks can be enqueued by terminating the group.
	 */
	cancel_queue_group_events(group);
	kfree(group);
}

int kbase_csf_queue_group_suspend(struct kbase_context *kctx,
				  struct kbase_suspend_copy_buffer *sus_buf,
				  u8 group_handle)
{
	struct kbase_device *const kbdev = kctx->kbdev;
	int err;
	struct kbase_queue_group *group;

	err = kbase_reset_gpu_prevent_and_wait(kbdev);
	if (err) {
		dev_warn(
			kbdev->dev,
			"Unsuccessful GPU reset detected when suspending group %d",
			group_handle);
		return err;
	}
	mutex_lock(&kctx->csf.lock);

	group = find_queue_group(kctx, group_handle);
	if (group)
		err = kbase_csf_scheduler_group_copy_suspend_buf(group,
								 sus_buf);
	else
		err = -EINVAL;

	mutex_unlock(&kctx->csf.lock);
	kbase_reset_gpu_allow(kbdev);

	return err;
}

/**
 * add_error() - Add an error to the list of errors to report to user space
 *
 * @kctx:  Address of a base context associated with a GPU address space.
 * @error: Address of the item to be added to the context's pending error list.
 * @data:  Error data to be returned to userspace.
 *
 * Does not wake up the event queue blocking a user thread in kbase_poll. This
 * is to make it more efficient to add multiple errors.
 *
 * The added error must not already be on the context's list of errors waiting
 * to be reported (e.g. because a previous error concerning the same object has
 * not yet been reported).
 */
static void add_error(struct kbase_context *const kctx,
		      struct kbase_csf_notification *const error,
		      struct base_csf_notification const *const data)
{
	unsigned long flags;

	if (WARN_ON(!kctx))
		return;

	if (WARN_ON(!error))
		return;

	if (WARN_ON(!data))
		return;

	spin_lock_irqsave(&kctx->csf.event_lock, flags);

	if (!WARN_ON(!list_empty(&error->link))) {
		error->data = *data;
		list_add_tail(&error->link, &kctx->csf.error_list);
		dev_dbg(kctx->kbdev->dev,
			"Added error %pK of type %d in context %pK\n",
			(void *)error, data->type, (void *)kctx);
	}

	spin_unlock_irqrestore(&kctx->csf.event_lock, flags);
}

void kbase_csf_add_group_fatal_error(
	struct kbase_queue_group *const group,
	struct base_gpu_queue_group_error const *const err_payload)
{
	struct base_csf_notification error;

	if (WARN_ON(!group))
		return;

	if (WARN_ON(!err_payload))
		return;

	error = (struct base_csf_notification) {
		.type = BASE_CSF_NOTIFICATION_GPU_QUEUE_GROUP_ERROR,
		.payload = {
			.csg_error = {
				.handle = group->handle,
				.error = *err_payload
			}
		}
	};

	add_error(group->kctx, &group->error_fatal, &error);
}

void kbase_csf_active_queue_groups_reset(struct kbase_device *kbdev,
					 struct kbase_context *kctx)
{
	struct list_head evicted_groups;
	struct kbase_queue_group *group;
	int i;

	INIT_LIST_HEAD(&evicted_groups);

	mutex_lock(&kctx->csf.lock);

	kbase_csf_scheduler_evict_ctx_slots(kbdev, kctx, &evicted_groups);
	while (!list_empty(&evicted_groups)) {
		group = list_first_entry(&evicted_groups,
				struct kbase_queue_group, link);

		dev_dbg(kbdev->dev, "Context %d_%d active group %d terminated",
			    kctx->tgid, kctx->id, group->handle);
		kbase_csf_term_descheduled_queue_group(group);
		list_del_init(&group->link);
	}

	/* Acting on the queue groups that are pending to be terminated. */
	for (i = 0; i < MAX_QUEUE_GROUP_NUM; i++) {
		group = kctx->csf.queue_groups[i];
		if (group &&
		    group->run_state == KBASE_CSF_GROUP_FAULT_EVICTED)
			kbase_csf_term_descheduled_queue_group(group);
	}

	mutex_unlock(&kctx->csf.lock);
}

int kbase_csf_ctx_init(struct kbase_context *kctx)
{
	struct kbase_device *kbdev = kctx->kbdev;
	int err = -ENOMEM;

	INIT_LIST_HEAD(&kctx->csf.event_callback_list);
	INIT_LIST_HEAD(&kctx->csf.queue_list);
	INIT_LIST_HEAD(&kctx->csf.link);
	INIT_LIST_HEAD(&kctx->csf.error_list);

	spin_lock_init(&kctx->csf.event_lock);
	kctx->csf.user_reg_vma = NULL;
	mutex_lock(&kbdev->pm.lock);
	/* The inode information for /dev/malixx file is not available at the
	 * time of device probe as the inode is created when the device node
	 * is created by udevd (through mknod).
	 */
	if (kctx->filp) {
		if (!kbdev->csf.mali_file_inode)
			kbdev->csf.mali_file_inode = kctx->filp->f_inode;

		/* inode is unique for a file */
		WARN_ON(kbdev->csf.mali_file_inode != kctx->filp->f_inode);
	}
	mutex_unlock(&kbdev->pm.lock);

	/* Mark all the cookies as 'free' */
	bitmap_fill(kctx->csf.cookies, KBASE_CSF_NUM_USER_IO_PAGES_HANDLE);

	kctx->csf.wq = alloc_workqueue("mali_kbase_csf_wq",
					WQ_UNBOUND, 1);

	if (likely(kctx->csf.wq)) {
		err = kbase_csf_scheduler_context_init(kctx);

		if (likely(!err)) {
			err = kbase_csf_kcpu_queue_context_init(kctx);

			if (likely(!err)) {
				err = kbase_csf_tiler_heap_context_init(kctx);

				if (likely(!err))
					mutex_init(&kctx->csf.lock);
				else
					kbase_csf_kcpu_queue_context_term(kctx);
			}

			if (unlikely(err))
				kbase_csf_scheduler_context_term(kctx);
		}

		if (unlikely(err))
			destroy_workqueue(kctx->csf.wq);
	}

	return err;
}

void kbase_csf_ctx_handle_fault(struct kbase_context *kctx,
		struct kbase_fault *fault)
{
	int gr;
	bool reported = false;
	struct base_gpu_queue_group_error err_payload;
	int err;
	struct kbase_device *kbdev;

	if (WARN_ON(!kctx))
		return;

	if (WARN_ON(!fault))
		return;

	kbdev = kctx->kbdev;
	err = kbase_reset_gpu_try_prevent(kbdev);
	/* Regardless of whether reset failed or is currently happening, exit
	 * early
	 */
	if (err)
		return;

	err_payload = (struct base_gpu_queue_group_error) {
		.error_type = BASE_GPU_QUEUE_GROUP_ERROR_FATAL,
		.payload = {
			.fatal_group = {
				.sideband = fault->addr,
				.status = fault->status,
			}
		}
	};

	mutex_lock(&kctx->csf.lock);

	for (gr = 0; gr < MAX_QUEUE_GROUP_NUM; gr++) {
		struct kbase_queue_group *const group =
			kctx->csf.queue_groups[gr];

		if (group && group->run_state != KBASE_CSF_GROUP_TERMINATED) {
			term_queue_group(group);
			kbase_csf_add_group_fatal_error(group, &err_payload);
			reported = true;
		}
	}

	mutex_unlock(&kctx->csf.lock);

	if (reported)
		kbase_event_wakeup(kctx);

	kbase_reset_gpu_allow(kbdev);
}

void kbase_csf_ctx_term(struct kbase_context *kctx)
{
	struct kbase_device *kbdev = kctx->kbdev;
	struct kbase_as *as = NULL;
	unsigned long flags;
	u32 i;
	int err;
	bool reset_prevented = false;

	/* As the kbase context is terminating, its debugfs sub-directory would
	 * have been removed already and so would be the debugfs file created
	 * for queue groups & kcpu queues, hence no need to explicitly remove
	 * those debugfs files.
	 */
	kbase_csf_event_wait_remove_all(kctx);

	/* Wait for a GPU reset if it is happening, prevent it if not happening */
	err = kbase_reset_gpu_prevent_and_wait(kbdev);
	if (err)
		dev_warn(
			kbdev->dev,
			"Unsuccessful GPU reset detected when terminating csf context (%d_%d), attempting to terminate regardless",
			kctx->tgid, kctx->id);
	else
		reset_prevented = true;

	mutex_lock(&kctx->csf.lock);
	/* Iterate through the queue groups that were not terminated by
	 * userspace and issue the term request to firmware for them.
	 */
	for (i = 0; i < MAX_QUEUE_GROUP_NUM; i++) {
		if (kctx->csf.queue_groups[i])
			term_queue_group(kctx->csf.queue_groups[i]);
	}
	mutex_unlock(&kctx->csf.lock);

	if (reset_prevented)
		kbase_reset_gpu_allow(kbdev);

	/* Now that all queue groups have been terminated, there can be no
	 * more OoM or timer event interrupts but there can be inflight work
	 * items. Destroying the wq will implicitly flush those work items.
	 */
	destroy_workqueue(kctx->csf.wq);

	/* Wait for the firmware error work item to also finish as it could
	 * be affecting this outgoing context also.
	 */
	flush_work(&kctx->kbdev->csf.fw_error_work);

	/* A work item to handle page_fault/bus_fault/gpu_fault could be
	 * pending for the outgoing context. Flush the workqueue that will
	 * execute that work item.
	 */
	spin_lock_irqsave(&kctx->kbdev->hwaccess_lock, flags);
	if (kctx->as_nr != KBASEP_AS_NR_INVALID)
		as = &kctx->kbdev->as[kctx->as_nr];
	spin_unlock_irqrestore(&kctx->kbdev->hwaccess_lock, flags);
	if (as)
		flush_workqueue(as->pf_wq);

	mutex_lock(&kctx->csf.lock);

	for (i = 0; i < MAX_QUEUE_GROUP_NUM; i++) {
		kfree(kctx->csf.queue_groups[i]);
		kctx->csf.queue_groups[i] = NULL;
	}

	/* Iterate through the queues that were not terminated by
	 * userspace and do the required cleanup for them.
	 */
	while (!list_empty(&kctx->csf.queue_list)) {
		struct kbase_queue *queue;

		queue = list_first_entry(&kctx->csf.queue_list,
						struct kbase_queue, link);

		/* The reference held when the IO mapping was created on bind
		 * would have been dropped otherwise the termination of Kbase
		 * context itself wouldn't have kicked-in. So there shall be
		 * only one reference left that was taken when queue was
		 * registered.
		 */
		if (atomic_read(&queue->refcount) != 1)
			dev_warn(kctx->kbdev->dev,
				 "Releasing queue with incorrect refcounting!\n");
		list_del_init(&queue->link);
		release_queue(queue);
	}

	mutex_unlock(&kctx->csf.lock);

	kbase_csf_tiler_heap_context_term(kctx);
	kbase_csf_kcpu_queue_context_term(kctx);
	kbase_csf_scheduler_context_term(kctx);

	mutex_destroy(&kctx->csf.lock);
}

int kbase_csf_event_wait_add(struct kbase_context *kctx,
			     kbase_csf_event_callback *callback, void *param)
{
	int err = -ENOMEM;
	struct kbase_csf_event *event =
		kzalloc(sizeof(struct kbase_csf_event), GFP_KERNEL);

	if (event) {
		unsigned long flags;

		event->kctx = kctx;
		event->callback = callback;
		event->param = param;

		spin_lock_irqsave(&kctx->csf.event_lock, flags);
		list_add_tail(&event->link, &kctx->csf.event_callback_list);
		dev_dbg(kctx->kbdev->dev,
			"Added event handler %pK with param %pK\n", event,
			event->param);
		spin_unlock_irqrestore(&kctx->csf.event_lock, flags);

		err = 0;
	}

	return err;
}

void kbase_csf_event_wait_remove(struct kbase_context *kctx,
		kbase_csf_event_callback *callback, void *param)
{
	struct kbase_csf_event *event;
	unsigned long flags;

	spin_lock_irqsave(&kctx->csf.event_lock, flags);

	list_for_each_entry(event, &kctx->csf.event_callback_list, link) {
		if ((event->callback == callback) && (event->param == param)) {
			list_del(&event->link);
			dev_dbg(kctx->kbdev->dev,
				"Removed event handler %pK with param %pK\n",
				event, event->param);
			kfree(event);
			break;
		}
	}
	spin_unlock_irqrestore(&kctx->csf.event_lock, flags);
}

bool kbase_csf_read_error(struct kbase_context *kctx,
		struct base_csf_notification *event_data)
{
	bool got_event = true;
	struct kbase_csf_notification *error_data = NULL;
	unsigned long flags;

	spin_lock_irqsave(&kctx->csf.event_lock, flags);

	if (likely(!list_empty(&kctx->csf.error_list))) {
		error_data = list_first_entry(&kctx->csf.error_list,
			struct kbase_csf_notification, link);
		list_del_init(&error_data->link);
		*event_data = error_data->data;
		dev_dbg(kctx->kbdev->dev, "Dequeued error %pK in context %pK\n",
			(void *)error_data, (void *)kctx);
	} else {
		got_event = false;
	}

	spin_unlock_irqrestore(&kctx->csf.event_lock, flags);

	return got_event;
}

bool kbase_csf_error_pending(struct kbase_context *kctx)
{
	bool event_pended = false;
	unsigned long flags;

	spin_lock_irqsave(&kctx->csf.event_lock, flags);
	event_pended = !list_empty(&kctx->csf.error_list);
	dev_dbg(kctx->kbdev->dev, "%s error is pending in context %pK\n",
		event_pended ? "An" : "No", (void *)kctx);
	spin_unlock_irqrestore(&kctx->csf.event_lock, flags);

	return event_pended;
}

void kbase_csf_event_signal(struct kbase_context *kctx, bool notify_gpu)
{
	struct kbase_csf_event *event, *next_event;
	unsigned long flags;

	dev_dbg(kctx->kbdev->dev,
		"Signal event (%s GPU notify) for context %pK\n",
		notify_gpu ? "with" : "without", (void *)kctx);

	/* First increment the signal count and wake up event thread.
	 */
	atomic_set(&kctx->event_count, 1);
	kbase_event_wakeup(kctx);

	/* Signal the CSF firmware. This is to ensure that pending command
	 * stream synch object wait operations are re-evaluated.
	 * Write to GLB_DOORBELL would suffice as spec says that all pending
	 * synch object wait operations are re-evaluated on a write to any
	 * CS_DOORBELL/GLB_DOORBELL register.
	 */
	if (notify_gpu) {
		spin_lock_irqsave(&kctx->kbdev->hwaccess_lock, flags);
		if (kctx->kbdev->pm.backend.gpu_powered)
			kbase_csf_ring_doorbell(kctx->kbdev, CSF_KERNEL_DOORBELL_NR);
		KBASE_KTRACE_ADD(kctx->kbdev, SYNC_UPDATE_EVENT_NOTIFY_GPU, kctx, 0u);
		spin_unlock_irqrestore(&kctx->kbdev->hwaccess_lock, flags);
	}

	/* Now invoke the callbacks registered on backend side.
	 * Allow item removal inside the loop, if requested by the callback.
	 */
	spin_lock_irqsave(&kctx->csf.event_lock, flags);

	list_for_each_entry_safe(
		event, next_event, &kctx->csf.event_callback_list, link) {
		enum kbase_csf_event_callback_action action;

		dev_dbg(kctx->kbdev->dev,
			"Calling event handler %pK with param %pK\n",
			(void *)event, event->param);
		action = event->callback(event->param);
		if (action == KBASE_CSF_EVENT_CALLBACK_REMOVE) {
			list_del(&event->link);
			kfree(event);
		}
	}

	spin_unlock_irqrestore(&kctx->csf.event_lock, flags);
}

void kbase_csf_event_wait_remove_all(struct kbase_context *kctx)
{
	struct kbase_csf_event *event, *next_event;
	unsigned long flags;

	spin_lock_irqsave(&kctx->csf.event_lock, flags);

	list_for_each_entry_safe(
		event, next_event, &kctx->csf.event_callback_list, link) {
		list_del(&event->link);
		dev_dbg(kctx->kbdev->dev,
			"Removed event handler %pK with param %pK\n",
			(void *)event, event->param);
		kfree(event);
	}

	spin_unlock_irqrestore(&kctx->csf.event_lock, flags);
}

/**
 * handle_oom_event - Handle the OoM event generated by the firmware for the
 *                    CSI.
 *
 * This function will handle the OoM event request from the firmware for the
 * CS. It will retrieve the address of heap context and heap's
 * statistics (like number of render passes in-flight) from the CS's kernel
 * kernel output page and pass them to the tiler heap function to allocate a
 * new chunk.
 * It will also update the CS's kernel input page with the address
 * of a new chunk that was allocated.
 *
 * @kctx: Pointer to the kbase context in which the tiler heap was initialized.
 * @stream: Pointer to the structure containing info provided by the firmware
 *          about the CSI.
 *
 * Return: 0 if successfully handled the request, otherwise a negative error
 *         code on failure.
 */
static int handle_oom_event(struct kbase_context *const kctx,
		struct kbase_csf_cmd_stream_info const *const stream)
{
	u64 gpu_heap_va =
		kbase_csf_firmware_cs_output(stream, CS_HEAP_ADDRESS_LO) |
		((u64)kbase_csf_firmware_cs_output(stream, CS_HEAP_ADDRESS_HI) << 32);
	const u32 vt_start =
		kbase_csf_firmware_cs_output(stream, CS_HEAP_VT_START);
	const u32 vt_end =
		kbase_csf_firmware_cs_output(stream, CS_HEAP_VT_END);
	const u32 frag_end =
		kbase_csf_firmware_cs_output(stream, CS_HEAP_FRAG_END);
	u32 renderpasses_in_flight;
	u32 pending_frag_count;
	u64 new_chunk_ptr;
	int err;

	if ((frag_end > vt_end) || (vt_end >= vt_start)) {
		dev_warn(kctx->kbdev->dev, "Invalid Heap statistics provided by firmware: vt_start %d, vt_end %d, frag_end %d\n",
			 vt_start, vt_end, frag_end);
		return -EINVAL;
	}

	renderpasses_in_flight = vt_start - frag_end;
	pending_frag_count = vt_end - frag_end;

	err = kbase_csf_tiler_heap_alloc_new_chunk(kctx,
		gpu_heap_va, renderpasses_in_flight, pending_frag_count, &new_chunk_ptr);

	/* It is okay to acknowledge with a NULL chunk (firmware will then wait
	 * for the fragment jobs to complete and release chunks)
	 */
	if (err == -EBUSY)
		new_chunk_ptr = 0;
	else if (err)
		return err;

	kbase_csf_firmware_cs_input(stream, CS_TILER_HEAP_START_LO,
				new_chunk_ptr & 0xFFFFFFFF);
	kbase_csf_firmware_cs_input(stream, CS_TILER_HEAP_START_HI,
				new_chunk_ptr >> 32);

	kbase_csf_firmware_cs_input(stream, CS_TILER_HEAP_END_LO,
				new_chunk_ptr & 0xFFFFFFFF);
	kbase_csf_firmware_cs_input(stream, CS_TILER_HEAP_END_HI,
				new_chunk_ptr >> 32);

	return 0;
}

/**
 * report_tiler_oom_error - Report a CSG error due to a tiler heap OOM event
 *
 * @group: Pointer to the GPU command queue group that encountered the error
 */
static void report_tiler_oom_error(struct kbase_queue_group *group)
{
	struct base_csf_notification const
		error = { .type = BASE_CSF_NOTIFICATION_GPU_QUEUE_GROUP_ERROR,
			  .payload = {
				  .csg_error = {
					  .handle = group->handle,
					  .error = {
						  .error_type =
							  BASE_GPU_QUEUE_GROUP_ERROR_TILER_HEAP_OOM,
					  } } } };

	add_error(group->kctx, &group->error_tiler_oom, &error);
	kbase_event_wakeup(group->kctx);
}

/**
 * kbase_queue_oom_event - Handle tiler out-of-memory for a GPU command queue.
 *
 * @queue: Pointer to queue for which out-of-memory event was received.
 *
 * Called with the CSF locked for the affected GPU virtual address space.
 * Do not call in interrupt context.
 *
 * Handles tiler out-of-memory for a GPU command queue and then clears the
 * notification to allow the firmware to report out-of-memory again in future.
 * If the out-of-memory condition was successfully handled then this function
 * rings the relevant doorbell to notify the firmware; otherwise, it terminates
 * the GPU command queue group to which the queue is bound. See
 * term_queue_group() for details.
 */
static void kbase_queue_oom_event(struct kbase_queue *const queue)
{
	struct kbase_context *const kctx = queue->kctx;
	struct kbase_device *const kbdev = kctx->kbdev;
	struct kbase_queue_group *group;
	int slot_num, err;
	struct kbase_csf_cmd_stream_group_info const *ginfo;
	struct kbase_csf_cmd_stream_info const *stream;
	int csi_index = queue->csi_index;
	u32 cs_oom_ack, cs_oom_req;

	lockdep_assert_held(&kctx->csf.lock);

	group = get_bound_queue_group(queue);
	if (!group) {
		dev_warn(kctx->kbdev->dev, "queue not bound\n");
		return;
	}

	kbase_csf_scheduler_lock(kbdev);

	slot_num = kbase_csf_scheduler_group_get_slot(group);

	/* The group could have gone off slot before this work item got
	 * a chance to execute.
	 */
	if (slot_num < 0)
		goto unlock;

	/* If the bound group is on slot yet the kctx is marked with disabled
	 * on address-space fault, the group is pending to be killed. So skip
	 * the inflight oom operation.
	 */
	if (kbase_ctx_flag(kctx, KCTX_AS_DISABLED_ON_FAULT))
		goto unlock;

	ginfo = &kbdev->csf.global_iface.groups[slot_num];
	stream = &ginfo->streams[csi_index];
	cs_oom_ack = kbase_csf_firmware_cs_output(stream, CS_ACK) &
		     CS_ACK_TILER_OOM_MASK;
	cs_oom_req = kbase_csf_firmware_cs_input_read(stream, CS_REQ) &
		     CS_REQ_TILER_OOM_MASK;

	/* The group could have already undergone suspend-resume cycle before
	 * this work item got a chance to execute. On CSG resume the CS_ACK
	 * register is set by firmware to reflect the CS_REQ register, which
	 * implies that all events signaled before suspension are implicitly
	 * acknowledged.
	 * A new OoM event is expected to be generated after resume.
	 */
	if (cs_oom_ack == cs_oom_req)
		goto unlock;

	err = handle_oom_event(kctx, stream);

	kbase_csf_firmware_cs_input_mask(stream, CS_REQ, cs_oom_ack,
					 CS_REQ_TILER_OOM_MASK);

	if (err) {
		dev_warn(
			kbdev->dev,
			"Queue group to be terminated, couldn't handle the OoM event\n");
		kbase_csf_scheduler_unlock(kbdev);
		term_queue_group(group);
		report_tiler_oom_error(group);
		return;
	}

	kbase_csf_ring_cs_kernel_doorbell(kbdev, csi_index, slot_num, true);
unlock:
	kbase_csf_scheduler_unlock(kbdev);
}

/**
 * oom_event_worker - Tiler out-of-memory handler called from a workqueue.
 *
 * @data: Pointer to a work_struct embedded in GPU command queue data.
 *
 * Handles a tiler out-of-memory condition for a GPU command queue and then
 * releases a reference that was added to prevent the queue being destroyed
 * while this work item was pending on a workqueue.
 */
static void oom_event_worker(struct work_struct *data)
{
	struct kbase_queue *queue =
		container_of(data, struct kbase_queue, oom_event_work);
	struct kbase_context *kctx = queue->kctx;
	struct kbase_device *const kbdev = kctx->kbdev;

	int err = kbase_reset_gpu_try_prevent(kbdev);
	/* Regardless of whether reset failed or is currently happening, exit
	 * early
	 */
	if (err)
		return;

	mutex_lock(&kctx->csf.lock);

	kbase_queue_oom_event(queue);
	release_queue(queue);

	mutex_unlock(&kctx->csf.lock);
	kbase_reset_gpu_allow(kbdev);
}

/**
 * report_group_timeout_error - Report the timeout error for the group to userspace.
 *
 * @group: Pointer to the group for which timeout error occurred
 */
static void report_group_timeout_error(struct kbase_queue_group *const group)
{
	struct base_csf_notification const
		error = { .type = BASE_CSF_NOTIFICATION_GPU_QUEUE_GROUP_ERROR,
			  .payload = {
				  .csg_error = {
					  .handle = group->handle,
					  .error = {
						  .error_type =
							  BASE_GPU_QUEUE_GROUP_ERROR_TIMEOUT,
					  } } } };

	dev_warn(group->kctx->kbdev->dev,
		 "Notify the event notification thread, forward progress timeout (%llu cycles)\n",
		 kbase_csf_timeout_get(group->kctx->kbdev));

	add_error(group->kctx, &group->error_timeout, &error);
	kbase_event_wakeup(group->kctx);
}

/**
 * timer_event_worker - Handle the progress timeout error for the group
 *
 * @data: Pointer to a work_struct embedded in GPU command queue group data.
 *
 * Terminate the CSG and report the error to userspace
 */
static void timer_event_worker(struct work_struct *data)
{
	struct kbase_queue_group *const group =
		container_of(data, struct kbase_queue_group, timer_event_work);
	struct kbase_context *const kctx = group->kctx;
	bool reset_prevented = false;
	int err = kbase_reset_gpu_prevent_and_wait(kctx->kbdev);

	if (err)
		dev_warn(
			kctx->kbdev->dev,
			"Unsuccessful GPU reset detected when terminating group %d on progress timeout, attempting to terminate regardless",
			group->handle);
	else
		reset_prevented = true;

	mutex_lock(&kctx->csf.lock);

	term_queue_group(group);
	report_group_timeout_error(group);

	mutex_unlock(&kctx->csf.lock);
	if (reset_prevented)
		kbase_reset_gpu_allow(kctx->kbdev);
}

/**
 * handle_progress_timer_event - Progress timer timeout event handler.
 *
 * @group: Pointer to GPU queue group for which the timeout event is received.
 *
 * Enqueue a work item to terminate the group and notify the event notification
 * thread of progress timeout fault for the GPU command queue group.
 */
static void handle_progress_timer_event(struct kbase_queue_group *const group)
{
	queue_work(group->kctx->csf.wq, &group->timer_event_work);
}

/**
 * protm_event_worker - Protected mode switch request event handler
 *			called from a workqueue.
 *
 * @data: Pointer to a work_struct embedded in GPU command queue group data.
 *
 * Request to switch to protected mode.
 */
static void protm_event_worker(struct work_struct *data)
{
	struct kbase_queue_group *const group =
		container_of(data, struct kbase_queue_group, protm_event_work);

	KBASE_KTRACE_ADD_CSF_GRP(group->kctx->kbdev, PROTM_EVENT_WORKER_BEGIN,
				 group, 0u);
	kbase_csf_scheduler_group_protm_enter(group);
	KBASE_KTRACE_ADD_CSF_GRP(group->kctx->kbdev, PROTM_EVENT_WORKER_END,
				 group, 0u);
}

static void report_queue_fatal_error(struct kbase_queue *const queue,
				     u32 cs_fatal, u64 cs_fatal_info,
				     u8 group_handle)
{
	struct base_csf_notification error =
		{ .type = BASE_CSF_NOTIFICATION_GPU_QUEUE_GROUP_ERROR,
		  .payload = {
			  .csg_error = {
				  .handle = group_handle,
				  .error = {
					  .error_type =
						  BASE_GPU_QUEUE_GROUP_QUEUE_ERROR_FATAL,
					  .payload = {
						  .fatal_queue = {
							  .sideband =
								  cs_fatal_info,
							  .status = cs_fatal,
							  .csi_index =
								  queue->csi_index,
						  } } } } } };

	add_error(queue->kctx, &queue->error, &error);
	kbase_event_wakeup(queue->kctx);
}

/**
 * handle_fault_event - Handler for CS fault.
 *
 * @queue:  Pointer to queue for which fault event was received.
 * @stream: Pointer to the structure containing info provided by the
 *          firmware about the CSI.
 *
 * Prints meaningful CS fault information.
 *
 */
static void
handle_fault_event(struct kbase_queue *const queue,
		   struct kbase_csf_cmd_stream_info const *const stream)
{
	const u32 cs_fault = kbase_csf_firmware_cs_output(stream, CS_FAULT);
	const u64 cs_fault_info =
		kbase_csf_firmware_cs_output(stream, CS_FAULT_INFO_LO) |
		((u64)kbase_csf_firmware_cs_output(stream, CS_FAULT_INFO_HI)
		 << 32);
	const u8 cs_fault_exception_type =
		CS_FAULT_EXCEPTION_TYPE_GET(cs_fault);
	const u32 cs_fault_exception_data =
		CS_FAULT_EXCEPTION_DATA_GET(cs_fault);
	const u64 cs_fault_info_exception_data =
		CS_FAULT_INFO_EXCEPTION_DATA_GET(cs_fault_info);
	struct kbase_device *const kbdev = queue->kctx->kbdev;

	kbase_csf_scheduler_spin_lock_assert_held(kbdev);

	dev_warn(kbdev->dev,
		 "Ctx %d_%d Group %d CSG %d CSI: %d\n"
		 "CS_FAULT.EXCEPTION_TYPE: 0x%x (%s)\n"
		 "CS_FAULT.EXCEPTION_DATA: 0x%x\n"
		 "CS_FAULT_INFO.EXCEPTION_DATA: 0x%llx\n",
		 queue->kctx->tgid, queue->kctx->id, queue->group->handle,
		 queue->group->csg_nr, queue->csi_index,
		 cs_fault_exception_type,
		 kbase_gpu_exception_name(cs_fault_exception_type),
		 cs_fault_exception_data, cs_fault_info_exception_data);

	if (cs_fault_exception_type ==
	    CS_FAULT_EXCEPTION_TYPE_RESOURCE_EVICTION_TIMEOUT)
		report_queue_fatal_error(queue, GPU_EXCEPTION_TYPE_SW_FAULT_2,
					 0, queue->group->handle);
}

/**
 * fatal_event_worker - Handle the fatal error for the GPU queue
 *
 * @data: Pointer to a work_struct embedded in GPU command queue.
 *
 * Terminate the CSG and report the error to userspace.
 */
static void fatal_event_worker(struct work_struct *const data)
{
	struct kbase_queue *const queue =
		container_of(data, struct kbase_queue, fatal_event_work);
	struct kbase_context *const kctx = queue->kctx;
	struct kbase_device *const kbdev = kctx->kbdev;
	struct kbase_queue_group *group;
	u8 group_handle;
	bool reset_prevented = false;
	int err = kbase_reset_gpu_prevent_and_wait(kbdev);

	if (err)
		dev_warn(
			kbdev->dev,
			"Unsuccessful GPU reset detected when terminating group to handle fatal event, attempting to terminate regardless");
	else
		reset_prevented = true;

	mutex_lock(&kctx->csf.lock);

	group = get_bound_queue_group(queue);
	if (!group) {
		dev_warn(kbdev->dev, "queue not bound when handling fatal event");
		goto unlock;
	}

	group_handle = group->handle;
	term_queue_group(group);
	report_queue_fatal_error(queue, queue->cs_fatal, queue->cs_fatal_info,
				 group_handle);

unlock:
	release_queue(queue);
	mutex_unlock(&kctx->csf.lock);
	if (reset_prevented)
		kbase_reset_gpu_allow(kbdev);
}

/**
 * handle_fatal_event - Handler for CS fatal.
 *
 * @queue:    Pointer to queue for which fatal event was received.
 * @stream:   Pointer to the structure containing info provided by the
 *            firmware about the CSI.
 *
 * Prints meaningful CS fatal information.
 * Enqueue a work item to terminate the group and report the fatal error
 * to user space.
 */
static void
handle_fatal_event(struct kbase_queue *const queue,
		   struct kbase_csf_cmd_stream_info const *const stream)
{
	const u32 cs_fatal = kbase_csf_firmware_cs_output(stream, CS_FATAL);
	const u64 cs_fatal_info =
		kbase_csf_firmware_cs_output(stream, CS_FATAL_INFO_LO) |
		((u64)kbase_csf_firmware_cs_output(stream, CS_FATAL_INFO_HI)
		 << 32);
	const u32 cs_fatal_exception_type =
		CS_FATAL_EXCEPTION_TYPE_GET(cs_fatal);
	const u32 cs_fatal_exception_data =
		CS_FATAL_EXCEPTION_DATA_GET(cs_fatal);
	const u64 cs_fatal_info_exception_data =
		CS_FATAL_INFO_EXCEPTION_DATA_GET(cs_fatal_info);
	struct kbase_device *const kbdev = queue->kctx->kbdev;

	kbase_csf_scheduler_spin_lock_assert_held(kbdev);

	dev_warn(kbdev->dev,
		 "Ctx %d_%d Group %d CSG %d CSI: %d\n"
		 "CS_FATAL.EXCEPTION_TYPE: 0x%x (%s)\n"
		 "CS_FATAL.EXCEPTION_DATA: 0x%x\n"
		 "CS_FATAL_INFO.EXCEPTION_DATA: 0x%llx\n",
		 queue->kctx->tgid, queue->kctx->id, queue->group->handle,
		 queue->group->csg_nr, queue->csi_index,
		 cs_fatal_exception_type,
		 kbase_gpu_exception_name(cs_fatal_exception_type),
		 cs_fatal_exception_data, cs_fatal_info_exception_data);

	if (cs_fatal_exception_type ==
			CS_FATAL_EXCEPTION_TYPE_FIRMWARE_INTERNAL_ERROR) {
		queue_work(system_wq, &kbdev->csf.fw_error_work);
	} else {
		get_queue(queue);
		queue->cs_fatal = cs_fatal;
		queue->cs_fatal_info = cs_fatal_info;
		if (!queue_work(queue->kctx->csf.wq, &queue->fatal_event_work))
			release_queue(queue);
	}
}

/**
 * handle_queue_exception_event - Handler for CS fatal/fault exception events.
 *
 * @queue:  Pointer to queue for which fatal/fault event was received.
 * @cs_req: Value of the CS_REQ register from the CS's input page.
 * @cs_ack: Value of the CS_ACK register from the CS's output page.
 */
static void handle_queue_exception_event(struct kbase_queue *const queue,
					 const u32 cs_req, const u32 cs_ack)
{
	struct kbase_csf_cmd_stream_group_info const *ginfo;
	struct kbase_csf_cmd_stream_info const *stream;
	struct kbase_context *const kctx = queue->kctx;
	struct kbase_device *const kbdev = kctx->kbdev;
	struct kbase_queue_group *group = queue->group;
	int csi_index = queue->csi_index;
	int slot_num = group->csg_nr;

	kbase_csf_scheduler_spin_lock_assert_held(kbdev);

	ginfo = &kbdev->csf.global_iface.groups[slot_num];
	stream = &ginfo->streams[csi_index];

	if ((cs_ack & CS_ACK_FATAL_MASK) != (cs_req & CS_REQ_FATAL_MASK)) {
		handle_fatal_event(queue, stream);
		kbase_csf_firmware_cs_input_mask(stream, CS_REQ, cs_ack,
						 CS_REQ_FATAL_MASK);
	}

	if ((cs_ack & CS_ACK_FAULT_MASK) != (cs_req & CS_REQ_FAULT_MASK)) {
		handle_fault_event(queue, stream);
		kbase_csf_firmware_cs_input_mask(stream, CS_REQ, cs_ack,
						 CS_REQ_FAULT_MASK);
		kbase_csf_ring_cs_kernel_doorbell(kbdev, csi_index, slot_num, true);
	}
}

/**
 * process_cs_interrupts - Process interrupts for a CS.
 *
 * @group:  Pointer to GPU command queue group data.
 * @ginfo:  The CSG interface provided by the firmware.
 * @irqreq: CSG's IRQ request bitmask (one bit per CS).
 * @irqack: CSG's IRQ acknowledge bitmask (one bit per CS).
 *
 * If the interrupt request bitmask differs from the acknowledge bitmask
 * then the firmware is notifying the host of an event concerning those
 * CSs indicated by bits whose value differs. The actions required
 * are then determined by examining which notification flags differ between
 * the request and acknowledge registers for the individual CS(s).
 */
static void process_cs_interrupts(struct kbase_queue_group *const group,
		      struct kbase_csf_cmd_stream_group_info const *const ginfo,
		      u32 const irqreq, u32 const irqack)
{
	struct kbase_device *const kbdev = group->kctx->kbdev;
	u32 remaining = irqreq ^ irqack;
	bool protm_pend = false;
	const bool group_suspending =
		!kbase_csf_scheduler_group_events_enabled(kbdev, group);

	kbase_csf_scheduler_spin_lock_assert_held(kbdev);

	while (remaining != 0) {
		int const i = ffs(remaining) - 1;
		struct kbase_queue *const queue = group->bound_queues[i];

		remaining &= ~(1 << i);

		/* The queue pointer can be NULL, but if it isn't NULL then it
		 * cannot disappear since scheduler spinlock is held and before
		 * freeing a bound queue it has to be first unbound which
		 * requires scheduler spinlock.
		 */
		if (queue && !WARN_ON(queue->csi_index != i)) {
			struct kbase_csf_cmd_stream_info const *const stream =
				&ginfo->streams[i];
			u32 const cs_req = kbase_csf_firmware_cs_input_read(
				stream, CS_REQ);
			u32 const cs_ack =
				kbase_csf_firmware_cs_output(stream, CS_ACK);
			struct workqueue_struct *wq = group->kctx->csf.wq;

			if ((cs_req & CS_REQ_EXCEPTION_MASK) ^
			    (cs_ack & CS_ACK_EXCEPTION_MASK)) {
				KBASE_KTRACE_ADD_CSF_GRP_Q(kbdev, CSI_FAULT_INTERRUPT, group, queue, cs_req ^ cs_ack);
				handle_queue_exception_event(queue, cs_req, cs_ack);
			}

			/* PROTM_PEND and TILER_OOM can be safely ignored
			 * because they will be raised again if the group
			 * is assigned a CSG slot in future.
			 */
			if (group_suspending) {
				u32 const cs_req_remain = cs_req & ~CS_REQ_EXCEPTION_MASK;
				u32 const cs_ack_remain = cs_ack & ~CS_ACK_EXCEPTION_MASK;

				KBASE_KTRACE_ADD_CSF_GRP_Q(kbdev, CSI_IGNORED_INTERRUPTS_GROUP_SUSPEND,
							   group, queue, cs_req_remain ^ cs_ack_remain);
				continue;
			}

			if (((cs_req & CS_REQ_TILER_OOM_MASK) ^
			     (cs_ack & CS_ACK_TILER_OOM_MASK))) {
				get_queue(queue);
				KBASE_KTRACE_ADD_CSF_GRP_Q(kbdev, CSI_TILER_OOM_INTERRUPT, group, queue,
							   cs_req ^ cs_ack);
				if (WARN_ON(!queue_work(wq, &queue->oom_event_work))) {
					/* The work item shall not have been
					 * already queued, there can be only
					 * one pending OoM event for a
					 * queue.
					 */
					release_queue(queue);
				}
			}

			if ((cs_req & CS_REQ_PROTM_PEND_MASK) ^
			    (cs_ack & CS_ACK_PROTM_PEND_MASK)) {
				KBASE_KTRACE_ADD_CSF_GRP_Q(kbdev, CSI_PROTM_PEND_INTERRUPT, group, queue,
							   cs_req ^ cs_ack);

				dev_dbg(kbdev->dev,
					"Protected mode entry request for queue on csi %d bound to group-%d on slot %d",
					queue->csi_index, group->handle,
					group->csg_nr);

				bitmap_set(group->protm_pending_bitmap, i, 1);
				KBASE_KTRACE_ADD_CSF_GRP_Q(kbdev, PROTM_PENDING_SET, group, queue,
							   group->protm_pending_bitmap[0]);
				protm_pend = true;
			}
		}
	}

	if (protm_pend)
		queue_work(group->kctx->csf.wq, &group->protm_event_work);
}

/**
 * process_csg_interrupts - Process interrupts for a CSG.
 *
 * @kbdev: Instance of a GPU platform device that implements a CSF interface.
 * @csg_nr: CSG number.
 *
 * Handles interrupts for a CSG and for CSs within it.
 *
 * If the CSG's request register value differs from its acknowledge register
 * then the firmware is notifying the host of an event concerning the whole
 * group. The actions required are then determined by examining which
 * notification flags differ between those two register values.
 *
 * See process_cs_interrupts() for details of per-stream interrupt handling.
 */
static void process_csg_interrupts(struct kbase_device *const kbdev,
	int const csg_nr)
{
	struct kbase_csf_cmd_stream_group_info *ginfo;
	struct kbase_queue_group *group = NULL;
	u32 req, ack, irqreq, irqack;

	kbase_csf_scheduler_spin_lock_assert_held(kbdev);

	if (WARN_ON(csg_nr >= kbdev->csf.global_iface.group_num))
		return;

	KBASE_KTRACE_ADD(kbdev, CSG_INTERRUPT_PROCESS, NULL, csg_nr);

	ginfo = &kbdev->csf.global_iface.groups[csg_nr];
	req = kbase_csf_firmware_csg_input_read(ginfo, CSG_REQ);
	ack = kbase_csf_firmware_csg_output(ginfo, CSG_ACK);
	irqreq = kbase_csf_firmware_csg_output(ginfo, CSG_IRQ_REQ);
	irqack = kbase_csf_firmware_csg_input_read(ginfo, CSG_IRQ_ACK);

	/* There may not be any pending CSG/CS interrupts to process */
	if ((req == ack) && (irqreq == irqack))
		goto out;

	/* Immediately set IRQ_ACK bits to be same as the IRQ_REQ bits before
	 * examining the CS_ACK & CS_REQ bits. This would ensure that Host
	 * doesn't misses an interrupt for the CS in the race scenario where
	 * whilst Host is servicing an interrupt for the CS, firmware sends
	 * another interrupt for that CS.
	 */
	kbase_csf_firmware_csg_input(ginfo, CSG_IRQ_ACK, irqreq);

	group = kbase_csf_scheduler_get_group_on_slot(kbdev, csg_nr);

	/* The group pointer can be NULL here if interrupts for the group
	 * (like SYNC_UPDATE, IDLE notification) were delayed and arrived
	 * just after the suspension of group completed. However if not NULL
	 * then the group pointer cannot disappear even if User tries to
	 * terminate the group whilst this loop is running as scheduler
	 * spinlock is held and for freeing a group that is resident on a CSG
	 * slot scheduler spinlock is required.
	 */
	if (!group)
		goto out;

	if (WARN_ON(kbase_csf_scheduler_group_get_slot_locked(group) != csg_nr))
		goto out;

	if ((req ^ ack) & CSG_REQ_SYNC_UPDATE_MASK) {
		kbase_csf_firmware_csg_input_mask(ginfo,
			CSG_REQ, ack, CSG_REQ_SYNC_UPDATE_MASK);

		KBASE_KTRACE_ADD_CSF_GRP(kbdev, CSG_SYNC_UPDATE_INTERRUPT, group, req ^ ack);
		kbase_csf_event_signal_cpu_only(group->kctx);
	}

	if ((req ^ ack) & CSG_REQ_IDLE_MASK) {
		struct kbase_csf_scheduler *scheduler =	&kbdev->csf.scheduler;

		kbase_csf_firmware_csg_input_mask(ginfo, CSG_REQ, ack,
			CSG_REQ_IDLE_MASK);

		set_bit(csg_nr, scheduler->csg_slots_idle_mask);
		KBASE_KTRACE_ADD_CSF_GRP(kbdev, CSG_SLOT_IDLE_SET, group,
					 scheduler->csg_slots_idle_mask[0]);
		KBASE_KTRACE_ADD_CSF_GRP(kbdev,  CSG_IDLE_INTERRUPT, group, req ^ ack);
		dev_dbg(kbdev->dev, "Idle notification received for Group %u on slot %d\n",
			 group->handle, csg_nr);

		/* Check if the scheduling tick can be advanced */
		if (kbase_csf_scheduler_all_csgs_idle(kbdev) &&
		    !scheduler->gpu_idle_fw_timer_enabled) {
			kbase_csf_scheduler_advance_tick_nolock(kbdev);
		}
	}

	if ((req ^ ack) & CSG_REQ_PROGRESS_TIMER_EVENT_MASK) {
		kbase_csf_firmware_csg_input_mask(ginfo, CSG_REQ, ack,
			CSG_REQ_PROGRESS_TIMER_EVENT_MASK);

		KBASE_KTRACE_ADD_CSF_GRP(kbdev, CSG_PROGRESS_TIMER_INTERRUPT,
					 group, req ^ ack);
		dev_info(kbdev->dev,
			"Timeout notification received for group %u of ctx %d_%d on slot %d\n",
			group->handle, group->kctx->tgid, group->kctx->id, csg_nr);

		handle_progress_timer_event(group);
	}

	process_cs_interrupts(group, ginfo, irqreq, irqack);

out:
	/* group may still be NULL here */
	KBASE_KTRACE_ADD_CSF_GRP(kbdev, CSG_INTERRUPT_PROCESS_END, group,
				 ((u64)req ^ ack) | (((u64)irqreq ^ irqack) << 32));
}

/**
 * process_prfcnt_interrupts - Process performance counter interrupts.
 *
 * @kbdev:   Instance of a GPU platform device that implements a CSF interface.
 * @glb_req: Global request register value.
 * @glb_ack: Global acknowledge register value.
 *
 * Handles interrupts issued by the firmware that relate to the performance
 * counters. For example, on completion of a performance counter sample. It is
 * expected that the scheduler spinlock is already held on calling this
 * function.
 */
static void process_prfcnt_interrupts(struct kbase_device *kbdev, u32 glb_req,
				      u32 glb_ack)
{
	const struct kbase_csf_global_iface *const global_iface =
		&kbdev->csf.global_iface;

	lockdep_assert_held(&kbdev->csf.scheduler.interrupt_lock);

	/* Process PRFCNT_SAMPLE interrupt. */
	if (kbdev->csf.hwcnt.request_pending &&
	    ((glb_req & GLB_REQ_PRFCNT_SAMPLE_MASK) ==
	     (glb_ack & GLB_REQ_PRFCNT_SAMPLE_MASK))) {
		kbdev->csf.hwcnt.request_pending = false;

		dev_dbg(kbdev->dev, "PRFCNT_SAMPLE done interrupt received.");

		kbase_hwcnt_backend_csf_on_prfcnt_sample(
			&kbdev->hwcnt_gpu_iface);
	}

	/* Process PRFCNT_ENABLE interrupt. */
	if (kbdev->csf.hwcnt.enable_pending &&
	    ((glb_req & GLB_REQ_PRFCNT_ENABLE_MASK) ==
	     (glb_ack & GLB_REQ_PRFCNT_ENABLE_MASK))) {
		kbdev->csf.hwcnt.enable_pending = false;

		dev_dbg(kbdev->dev,
			"PRFCNT_ENABLE status changed interrupt received.");

		if (glb_ack & GLB_REQ_PRFCNT_ENABLE_MASK)
			kbase_hwcnt_backend_csf_on_prfcnt_enable(
				&kbdev->hwcnt_gpu_iface);
		else
			kbase_hwcnt_backend_csf_on_prfcnt_disable(
				&kbdev->hwcnt_gpu_iface);
	}

	/* Process PRFCNT_THRESHOLD interrupt. */
	if ((glb_req ^ glb_ack) & GLB_REQ_PRFCNT_THRESHOLD_MASK) {
		dev_dbg(kbdev->dev, "PRFCNT_THRESHOLD interrupt received.");

		kbase_hwcnt_backend_csf_on_prfcnt_threshold(
			&kbdev->hwcnt_gpu_iface);

		/* Set the GLB_REQ.PRFCNT_THRESHOLD flag back to
		 * the same value as GLB_ACK.PRFCNT_THRESHOLD
		 * flag in order to enable reporting of another
		 * PRFCNT_THRESHOLD event.
		 */
		kbase_csf_firmware_global_input_mask(
			global_iface, GLB_REQ, glb_ack,
			GLB_REQ_PRFCNT_THRESHOLD_MASK);
	}

	/* Process PRFCNT_OVERFLOW interrupt. */
	if ((glb_req ^ glb_ack) & GLB_REQ_PRFCNT_OVERFLOW_MASK) {
		dev_dbg(kbdev->dev, "PRFCNT_OVERFLOW interrupt received.");

		kbase_hwcnt_backend_csf_on_prfcnt_overflow(
			&kbdev->hwcnt_gpu_iface);

		/* Set the GLB_REQ.PRFCNT_OVERFLOW flag back to
		 * the same value as GLB_ACK.PRFCNT_OVERFLOW
		 * flag in order to enable reporting of another
		 * PRFCNT_OVERFLOW event.
		 */
		kbase_csf_firmware_global_input_mask(
			global_iface, GLB_REQ, glb_ack,
			GLB_REQ_PRFCNT_OVERFLOW_MASK);
	}
}

void kbase_csf_interrupt(struct kbase_device *kbdev, u32 val)
{
	unsigned long flags;
	u32 remaining = val;

	lockdep_assert_held(&kbdev->hwaccess_lock);

	KBASE_KTRACE_ADD(kbdev, CSF_INTERRUPT, NULL, val);
	kbase_reg_write(kbdev, JOB_CONTROL_REG(JOB_IRQ_CLEAR), val);

	if (val & JOB_IRQ_GLOBAL_IF) {
		const struct kbase_csf_global_iface *const global_iface =
			&kbdev->csf.global_iface;
		struct kbase_csf_scheduler *scheduler =	&kbdev->csf.scheduler;

		kbdev->csf.interrupt_received = true;
		remaining &= ~JOB_IRQ_GLOBAL_IF;

		if (!kbdev->csf.firmware_reloaded)
			kbase_csf_firmware_reload_completed(kbdev);
		else if (global_iface->output) {
			u32 glb_req, glb_ack;

			kbase_csf_scheduler_spin_lock(kbdev, &flags);
			glb_req = kbase_csf_firmware_global_input_read(
					global_iface, GLB_REQ);
			glb_ack = kbase_csf_firmware_global_output(
					global_iface, GLB_ACK);
			KBASE_KTRACE_ADD(kbdev, GLB_REQ_ACQ, NULL, glb_req ^ glb_ack);

			if ((glb_req ^ glb_ack) & GLB_REQ_PROTM_EXIT_MASK) {
				dev_dbg(kbdev->dev, "Protected mode exit interrupt received");
				kbase_csf_firmware_global_input_mask(
						global_iface, GLB_REQ, glb_ack,
						GLB_REQ_PROTM_EXIT_MASK);
				WARN_ON(!kbase_csf_scheduler_protected_mode_in_use(kbdev));
				KBASE_KTRACE_ADD_CSF_GRP(kbdev, SCHEDULER_EXIT_PROTM, scheduler->active_protm_grp, 0u);
				scheduler->active_protm_grp = NULL;
				kbdev->protected_mode = false;
				kbase_ipa_control_protm_exited(kbdev);
				kbase_hwcnt_backend_csf_protm_exited(
					&kbdev->hwcnt_gpu_iface);
			}

			/* Handle IDLE Hysteresis notification event */
			if ((glb_req ^ glb_ack) & GLB_REQ_IDLE_EVENT_MASK) {
				int non_idle_offslot_grps;
				bool can_suspend_on_idle;
				dev_dbg(kbdev->dev, "Idle-hysteresis event flagged");
				kbase_csf_firmware_global_input_mask(
						global_iface, GLB_REQ, glb_ack,
						GLB_REQ_IDLE_EVENT_MASK);

				non_idle_offslot_grps = atomic_read(&scheduler->non_idle_offslot_grps);
				can_suspend_on_idle = kbase_pm_idle_groups_sched_suspendable(kbdev);
				KBASE_KTRACE_ADD(kbdev, SCHEDULER_CAN_IDLE, NULL,
					((u64)(u32)non_idle_offslot_grps) | (((u64)can_suspend_on_idle) << 32));

				if (!non_idle_offslot_grps) {
					if (can_suspend_on_idle)
						queue_work(system_highpri_wq,
							   &scheduler->gpu_idle_work);
				} else {
					/* Advance the scheduling tick to get
					 * the non-idle suspended groups loaded
					 * soon.
					 */
					kbase_csf_scheduler_advance_tick_nolock(
						kbdev);
				}
			}

			process_prfcnt_interrupts(kbdev, glb_req, glb_ack);

			kbase_csf_scheduler_spin_unlock(kbdev, flags);

			/* Invoke the MCU state machine as a state transition
			 * might have completed.
			 */
			kbase_pm_update_state(kbdev);
		}

		if (!remaining) {
			wake_up_all(&kbdev->csf.event_wait);
			KBASE_KTRACE_ADD(kbdev, CSF_INTERRUPT_END, NULL, val);
			return;
		}
	}

	kbase_csf_scheduler_spin_lock(kbdev, &flags);
	while (remaining != 0) {
		int const csg_nr = ffs(remaining) - 1;

		process_csg_interrupts(kbdev, csg_nr);
		remaining &= ~(1 << csg_nr);
	}
	kbase_csf_scheduler_spin_unlock(kbdev, flags);

	wake_up_all(&kbdev->csf.event_wait);
	KBASE_KTRACE_ADD(kbdev, CSF_INTERRUPT_END, NULL, val);
}

void kbase_csf_doorbell_mapping_term(struct kbase_device *kbdev)
{
	if (kbdev->csf.db_filp) {
		struct page *page = as_page(kbdev->csf.dummy_db_page);

		kbase_mem_pool_free(
			&kbdev->mem_pools.small[KBASE_MEM_GROUP_CSF_FW],
			page, false);

		fput(kbdev->csf.db_filp);
	}
}

int kbase_csf_doorbell_mapping_init(struct kbase_device *kbdev)
{
	struct tagged_addr phys;
	struct file *filp;
	int ret;

	filp = shmem_file_setup("mali csf", MAX_LFS_FILESIZE, VM_NORESERVE);
	if (IS_ERR(filp))
		return PTR_ERR(filp);

	ret = kbase_mem_pool_alloc_pages(
		&kbdev->mem_pools.small[KBASE_MEM_GROUP_CSF_FW],
		1, &phys, false);

	if (ret <= 0) {
		fput(filp);
		return ret;
	}

	kbdev->csf.db_filp = filp;
	kbdev->csf.dummy_db_page = phys;
	kbdev->csf.db_file_offsets = 0;

	return 0;
}

void kbase_csf_free_dummy_user_reg_page(struct kbase_device *kbdev)
{
	if (as_phys_addr_t(kbdev->csf.dummy_user_reg_page)) {
		struct page *page = as_page(kbdev->csf.dummy_user_reg_page);

		kbase_mem_pool_free(
			&kbdev->mem_pools.small[KBASE_MEM_GROUP_CSF_FW], page,
			false);
	}
}

int kbase_csf_setup_dummy_user_reg_page(struct kbase_device *kbdev)
{
	struct tagged_addr phys;
	struct page *page;
	u32 *addr;
	int ret;

	kbdev->csf.dummy_user_reg_page = as_tagged(0);

	ret = kbase_mem_pool_alloc_pages(
		&kbdev->mem_pools.small[KBASE_MEM_GROUP_CSF_FW], 1, &phys,
		false);

	if (ret <= 0)
		return ret;

	page = as_page(phys);
	addr = kmap_atomic(page);

	/* Write a special value for the latest flush register inside the
	 * dummy page
	 */
	addr[LATEST_FLUSH / sizeof(u32)] = POWER_DOWN_LATEST_FLUSH_VALUE;

	kbase_sync_single_for_device(kbdev, kbase_dma_addr(page), sizeof(u32),
				     DMA_BIDIRECTIONAL);
	kunmap_atomic(addr);

	kbdev->csf.dummy_user_reg_page = phys;

	return 0;
}

u8 kbase_csf_priority_check(struct kbase_device *kbdev, u8 req_priority)
{
	struct priority_control_manager_device *pcm_device = kbdev->pcm_dev;
	u8 out_priority = req_priority;

	if (pcm_device) {
		req_priority = kbase_csf_priority_queue_group_priority_to_relative(req_priority);
		out_priority = pcm_device->ops.pcm_scheduler_priority_check(pcm_device, current, req_priority);
		out_priority = kbase_csf_priority_relative_to_queue_group_priority(out_priority);
	}

	return out_priority;
}

