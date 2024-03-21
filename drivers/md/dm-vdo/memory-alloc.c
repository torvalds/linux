// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright 2023 Red Hat
 */

#include <linux/delay.h>
#include <linux/mm.h>
#include <linux/sched/mm.h>
#include <linux/slab.h>
#include <linux/vmalloc.h>

#include "logger.h"
#include "memory-alloc.h"
#include "permassert.h"

/*
 * UDS and VDO keep track of which threads are allowed to allocate memory freely, and which threads
 * must be careful to not do a memory allocation that does an I/O request. The 'allocating_threads'
 * thread_registry and its associated methods implement this tracking.
 */
static struct thread_registry allocating_threads;

static inline bool allocations_allowed(void)
{
	return vdo_lookup_thread(&allocating_threads) != NULL;
}

/*
 * Register the current thread as an allocating thread.
 *
 * An optional flag location can be supplied indicating whether, at any given point in time, the
 * threads associated with that flag should be allocating storage. If the flag is false, a message
 * will be logged.
 *
 * If no flag is supplied, the thread is always allowed to allocate storage without complaint.
 *
 * @new_thread: registered_thread structure to use for the current thread
 * @flag_ptr: Location of the allocation-allowed flag
 */
void vdo_register_allocating_thread(struct registered_thread *new_thread,
				    const bool *flag_ptr)
{
	if (flag_ptr == NULL) {
		static const bool allocation_always_allowed = true;

		flag_ptr = &allocation_always_allowed;
	}

	vdo_register_thread(&allocating_threads, new_thread, flag_ptr);
}

/* Unregister the current thread as an allocating thread. */
void vdo_unregister_allocating_thread(void)
{
	vdo_unregister_thread(&allocating_threads);
}

/*
 * We track how much memory has been allocated and freed. When we unload the module, we log an
 * error if we have not freed all the memory that we allocated. Nearly all memory allocation and
 * freeing is done using this module.
 *
 * We do not use kernel functions like the kvasprintf() method, which allocate memory indirectly
 * using kmalloc.
 *
 * These data structures and methods are used to track the amount of memory used.
 */

/*
 * We allocate very few large objects, and allocation/deallocation isn't done in a
 * performance-critical stage for us, so a linked list should be fine.
 */
struct vmalloc_block_info {
	void *ptr;
	size_t size;
	struct vmalloc_block_info *next;
};

static struct {
	spinlock_t lock;
	size_t kmalloc_blocks;
	size_t kmalloc_bytes;
	size_t vmalloc_blocks;
	size_t vmalloc_bytes;
	size_t peak_bytes;
	struct vmalloc_block_info *vmalloc_list;
} memory_stats __cacheline_aligned;

static void update_peak_usage(void)
{
	size_t total_bytes = memory_stats.kmalloc_bytes + memory_stats.vmalloc_bytes;

	if (total_bytes > memory_stats.peak_bytes)
		memory_stats.peak_bytes = total_bytes;
}

static void add_kmalloc_block(size_t size)
{
	unsigned long flags;

	spin_lock_irqsave(&memory_stats.lock, flags);
	memory_stats.kmalloc_blocks++;
	memory_stats.kmalloc_bytes += size;
	update_peak_usage();
	spin_unlock_irqrestore(&memory_stats.lock, flags);
}

static void remove_kmalloc_block(size_t size)
{
	unsigned long flags;

	spin_lock_irqsave(&memory_stats.lock, flags);
	memory_stats.kmalloc_blocks--;
	memory_stats.kmalloc_bytes -= size;
	spin_unlock_irqrestore(&memory_stats.lock, flags);
}

static void add_vmalloc_block(struct vmalloc_block_info *block)
{
	unsigned long flags;

	spin_lock_irqsave(&memory_stats.lock, flags);
	block->next = memory_stats.vmalloc_list;
	memory_stats.vmalloc_list = block;
	memory_stats.vmalloc_blocks++;
	memory_stats.vmalloc_bytes += block->size;
	update_peak_usage();
	spin_unlock_irqrestore(&memory_stats.lock, flags);
}

static void remove_vmalloc_block(void *ptr)
{
	struct vmalloc_block_info *block;
	struct vmalloc_block_info **block_ptr;
	unsigned long flags;

	spin_lock_irqsave(&memory_stats.lock, flags);
	for (block_ptr = &memory_stats.vmalloc_list;
	     (block = *block_ptr) != NULL;
	     block_ptr = &block->next) {
		if (block->ptr == ptr) {
			*block_ptr = block->next;
			memory_stats.vmalloc_blocks--;
			memory_stats.vmalloc_bytes -= block->size;
			break;
		}
	}

	spin_unlock_irqrestore(&memory_stats.lock, flags);
	if (block != NULL)
		vdo_free(block);
	else
		vdo_log_info("attempting to remove ptr %px not found in vmalloc list", ptr);
}

/*
 * Determine whether allocating a memory block should use kmalloc or __vmalloc.
 *
 * vmalloc can allocate any integral number of pages.
 *
 * kmalloc can allocate any number of bytes up to a configured limit, which defaults to 8 megabytes
 * on some systems. kmalloc is especially good when memory is being both allocated and freed, and
 * it does this efficiently in a multi CPU environment.
 *
 * kmalloc usually rounds the size of the block up to the next power of two, so when the requested
 * block is bigger than PAGE_SIZE / 2 bytes, kmalloc will never give you less space than the
 * corresponding vmalloc allocation. Sometimes vmalloc will use less overhead than kmalloc.
 *
 * The advantages of kmalloc do not help out UDS or VDO, because we allocate all our memory up
 * front and do not free and reallocate it. Sometimes we have problems using kmalloc, because the
 * Linux memory page map can become so fragmented that kmalloc will not give us a 32KB chunk. We
 * have used vmalloc as a backup to kmalloc in the past, and a follow-up vmalloc of 32KB will work.
 * But there is no strong case to be made for using kmalloc over vmalloc for these size chunks.
 *
 * The kmalloc/vmalloc boundary is set at 4KB, and kmalloc gets the 4KB requests. There is no
 * strong reason for favoring either kmalloc or vmalloc for 4KB requests, except that tracking
 * vmalloc statistics uses a linked list implementation. Using a simple test, this choice of
 * boundary results in 132 vmalloc calls. Using vmalloc for requests of exactly 4KB results in an
 * additional 6374 vmalloc calls, which is much less efficient for tracking.
 *
 * @size: How many bytes to allocate
 */
static inline bool use_kmalloc(size_t size)
{
	return size <= PAGE_SIZE;
}

/*
 * Allocate storage based on memory size and alignment, logging an error if the allocation fails.
 * The memory will be zeroed.
 *
 * @size: The size of an object
 * @align: The required alignment
 * @what: What is being allocated (for error logging)
 * @ptr: A pointer to hold the allocated memory
 *
 * Return: VDO_SUCCESS or an error code
 */
int vdo_allocate_memory(size_t size, size_t align, const char *what, void *ptr)
{
	/*
	 * The __GFP_RETRY_MAYFAIL flag means the VM implementation will retry memory reclaim
	 * procedures that have previously failed if there is some indication that progress has
	 * been made elsewhere. It can wait for other tasks to attempt high level approaches to
	 * freeing memory such as compaction (which removes fragmentation) and page-out. There is
	 * still a definite limit to the number of retries, but it is a larger limit than with
	 * __GFP_NORETRY. Allocations with this flag may fail, but only when there is genuinely
	 * little unused memory. While these allocations do not directly trigger the OOM killer,
	 * their failure indicates that the system is likely to need to use the OOM killer soon.
	 * The caller must handle failure, but can reasonably do so by failing a higher-level
	 * request, or completing it only in a much less efficient manner.
	 */
	const gfp_t gfp_flags = GFP_KERNEL | __GFP_ZERO | __GFP_RETRY_MAYFAIL;
	unsigned int noio_flags;
	bool allocations_restricted = !allocations_allowed();
	unsigned long start_time;
	void *p = NULL;

	if (unlikely(ptr == NULL))
		return -EINVAL;

	if (size == 0) {
		*((void **) ptr) = NULL;
		return VDO_SUCCESS;
	}

	if (allocations_restricted)
		noio_flags = memalloc_noio_save();

	start_time = jiffies;
	if (use_kmalloc(size) && (align < PAGE_SIZE)) {
		p = kmalloc(size, gfp_flags | __GFP_NOWARN);
		if (p == NULL) {
			/*
			 * It is possible for kmalloc to fail to allocate memory because there is
			 * no page available. A short sleep may allow the page reclaimer to
			 * free a page.
			 */
			fsleep(1000);
			p = kmalloc(size, gfp_flags);
		}

		if (p != NULL)
			add_kmalloc_block(ksize(p));
	} else {
		struct vmalloc_block_info *block;

		if (vdo_allocate(1, struct vmalloc_block_info, __func__, &block) == VDO_SUCCESS) {
			/*
			 * It is possible for __vmalloc to fail to allocate memory because there
			 * are no pages available. A short sleep may allow the page reclaimer
			 * to free enough pages for a small allocation.
			 *
			 * For larger allocations, the page_alloc code is racing against the page
			 * reclaimer. If the page reclaimer can stay ahead of page_alloc, the
			 * __vmalloc will succeed. But if page_alloc overtakes the page reclaimer,
			 * the allocation fails. It is possible that more retries will succeed.
			 */
			for (;;) {
				p = __vmalloc(size, gfp_flags | __GFP_NOWARN);
				if (p != NULL)
					break;

				if (jiffies_to_msecs(jiffies - start_time) > 1000) {
					/* Try one more time, logging a failure for this call. */
					p = __vmalloc(size, gfp_flags);
					break;
				}

				fsleep(1000);
			}

			if (p == NULL) {
				vdo_free(block);
			} else {
				block->ptr = p;
				block->size = PAGE_ALIGN(size);
				add_vmalloc_block(block);
			}
		}
	}

	if (allocations_restricted)
		memalloc_noio_restore(noio_flags);

	if (unlikely(p == NULL)) {
		vdo_log_error("Could not allocate %zu bytes for %s in %u msecs",
			      size, what, jiffies_to_msecs(jiffies - start_time));
		return -ENOMEM;
	}

	*((void **) ptr) = p;
	return VDO_SUCCESS;
}

/*
 * Allocate storage based on memory size, failing immediately if the required memory is not
 * available. The memory will be zeroed.
 *
 * @size: The size of an object.
 * @what: What is being allocated (for error logging)
 *
 * Return: pointer to the allocated memory, or NULL if the required space is not available.
 */
void *vdo_allocate_memory_nowait(size_t size, const char *what __maybe_unused)
{
	void *p = kmalloc(size, GFP_NOWAIT | __GFP_ZERO);

	if (p != NULL)
		add_kmalloc_block(ksize(p));

	return p;
}

void vdo_free(void *ptr)
{
	if (ptr != NULL) {
		if (is_vmalloc_addr(ptr)) {
			remove_vmalloc_block(ptr);
			vfree(ptr);
		} else {
			remove_kmalloc_block(ksize(ptr));
			kfree(ptr);
		}
	}
}

/*
 * Reallocate dynamically allocated memory. There are no alignment guarantees for the reallocated
 * memory. If the new memory is larger than the old memory, the new space will be zeroed.
 *
 * @ptr: The memory to reallocate.
 * @old_size: The old size of the memory
 * @size: The new size to allocate
 * @what: What is being allocated (for error logging)
 * @new_ptr: A pointer to hold the reallocated pointer
 *
 * Return: VDO_SUCCESS or an error code
 */
int vdo_reallocate_memory(void *ptr, size_t old_size, size_t size, const char *what,
			  void *new_ptr)
{
	int result;

	if (size == 0) {
		vdo_free(ptr);
		*(void **) new_ptr = NULL;
		return VDO_SUCCESS;
	}

	result = vdo_allocate(size, char, what, new_ptr);
	if (result != VDO_SUCCESS)
		return result;

	if (ptr != NULL) {
		if (old_size < size)
			size = old_size;

		memcpy(*((void **) new_ptr), ptr, size);
		vdo_free(ptr);
	}

	return VDO_SUCCESS;
}

int vdo_duplicate_string(const char *string, const char *what, char **new_string)
{
	int result;
	u8 *dup;

	result = vdo_allocate(strlen(string) + 1, u8, what, &dup);
	if (result != VDO_SUCCESS)
		return result;

	memcpy(dup, string, strlen(string) + 1);
	*new_string = dup;
	return VDO_SUCCESS;
}

void vdo_memory_init(void)
{
	spin_lock_init(&memory_stats.lock);
	vdo_initialize_thread_registry(&allocating_threads);
}

void vdo_memory_exit(void)
{
	VDO_ASSERT_LOG_ONLY(memory_stats.kmalloc_bytes == 0,
			    "kmalloc memory used (%zd bytes in %zd blocks) is returned to the kernel",
			    memory_stats.kmalloc_bytes, memory_stats.kmalloc_blocks);
	VDO_ASSERT_LOG_ONLY(memory_stats.vmalloc_bytes == 0,
			    "vmalloc memory used (%zd bytes in %zd blocks) is returned to the kernel",
			    memory_stats.vmalloc_bytes, memory_stats.vmalloc_blocks);
	vdo_log_debug("peak usage %zd bytes", memory_stats.peak_bytes);
}

void vdo_get_memory_stats(u64 *bytes_used, u64 *peak_bytes_used)
{
	unsigned long flags;

	spin_lock_irqsave(&memory_stats.lock, flags);
	*bytes_used = memory_stats.kmalloc_bytes + memory_stats.vmalloc_bytes;
	*peak_bytes_used = memory_stats.peak_bytes;
	spin_unlock_irqrestore(&memory_stats.lock, flags);
}

/*
 * Report stats on any allocated memory that we're tracking. Not all allocation types are
 * guaranteed to be tracked in bytes (e.g., bios).
 */
void vdo_report_memory_usage(void)
{
	unsigned long flags;
	u64 kmalloc_blocks;
	u64 kmalloc_bytes;
	u64 vmalloc_blocks;
	u64 vmalloc_bytes;
	u64 peak_usage;
	u64 total_bytes;

	spin_lock_irqsave(&memory_stats.lock, flags);
	kmalloc_blocks = memory_stats.kmalloc_blocks;
	kmalloc_bytes = memory_stats.kmalloc_bytes;
	vmalloc_blocks = memory_stats.vmalloc_blocks;
	vmalloc_bytes = memory_stats.vmalloc_bytes;
	peak_usage = memory_stats.peak_bytes;
	spin_unlock_irqrestore(&memory_stats.lock, flags);
	total_bytes = kmalloc_bytes + vmalloc_bytes;
	vdo_log_info("current module memory tracking (actual allocation sizes, not requested):");
	vdo_log_info("  %llu bytes in %llu kmalloc blocks",
		     (unsigned long long) kmalloc_bytes,
		     (unsigned long long) kmalloc_blocks);
	vdo_log_info("  %llu bytes in %llu vmalloc blocks",
		     (unsigned long long) vmalloc_bytes,
		     (unsigned long long) vmalloc_blocks);
	vdo_log_info("  total %llu bytes, peak usage %llu bytes",
		     (unsigned long long) total_bytes, (unsigned long long) peak_usage);
}
