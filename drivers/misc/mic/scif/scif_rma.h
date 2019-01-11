/*
 * Intel MIC Platform Software Stack (MPSS)
 *
 * This file is provided under a dual BSD/GPLv2 license.  When using or
 * redistributing this file, you may do so under either license.
 *
 * GPL LICENSE SUMMARY
 *
 * Copyright(c) 2015 Intel Corporation.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of version 2 of the GNU General Public License as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * BSD LICENSE
 *
 * Copyright(c) 2015 Intel Corporation.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * * Redistributions of source code must retain the above copyright
 *   notice, this list of conditions and the following disclaimer.
 * * Redistributions in binary form must reproduce the above copyright
 *   notice, this list of conditions and the following disclaimer in
 *   the documentation and/or other materials provided with the
 *   distribution.
 * * Neither the name of Intel Corporation nor the names of its
 *   contributors may be used to endorse or promote products derived
 *   from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 * Intel SCIF driver.
 *
 */
#ifndef SCIF_RMA_H
#define SCIF_RMA_H

#include <linux/intel-iommu.h>
#include <linux/mmu_notifier.h>

#include "../bus/scif_bus.h"

/* If this bit is set then the mark is a remote fence mark */
#define SCIF_REMOTE_FENCE_BIT          31
/* Magic value used to indicate a remote fence request */
#define SCIF_REMOTE_FENCE BIT_ULL(SCIF_REMOTE_FENCE_BIT)

#define SCIF_MAX_UNALIGNED_BUF_SIZE (1024 * 1024ULL)
#define SCIF_KMEM_UNALIGNED_BUF_SIZE (SCIF_MAX_UNALIGNED_BUF_SIZE + \
				      (L1_CACHE_BYTES << 1))

#define SCIF_IOVA_START_PFN		(1)
#define SCIF_IOVA_PFN(addr) ((addr) >> PAGE_SHIFT)
#define SCIF_DMA_64BIT_PFN SCIF_IOVA_PFN(DMA_BIT_MASK(64))
#define SCIF_DMA_63BIT_PFN SCIF_IOVA_PFN(DMA_BIT_MASK(63))

/*
 * struct scif_endpt_rma_info - Per Endpoint Remote Memory Access Information
 *
 * @reg_list: List of registration windows for self
 * @remote_reg_list: List of registration windows for peer
 * @iovad: Offset generator
 * @rma_lock: Synchronizes access to self/remote list and also protects the
 *	      window from being destroyed while RMAs are in progress.
 * @tc_lock: Synchronizes access to temporary cached windows list
 *	     for SCIF Registration Caching.
 * @mmn_lock: Synchronizes access to the list of MMU notifiers registered
 * @tw_refcount: Keeps track of number of outstanding temporary registered
 *		 windows created by scif_vreadfrom/scif_vwriteto which have
 *		 not been destroyed.
 * @tcw_refcount: Same as tw_refcount but for temporary cached windows
 * @tcw_total_pages: Same as tcw_refcount but in terms of pages pinned
 * @mmn_list: MMU notifier so that we can destroy the windows when required
 * @fence_refcount: Keeps track of number of outstanding remote fence
 *		    requests which have been received by the peer.
 * @dma_chan: DMA channel used for all DMA transfers for this endpoint.
 * @async_list_del: Detect asynchronous list entry deletion
 * @vma_list: List of vmas with remote memory mappings
 * @markwq: Wait queue used for scif_fence_mark/scif_fence_wait
*/
struct scif_endpt_rma_info {
	struct list_head reg_list;
	struct list_head remote_reg_list;
	struct iova_domain iovad;
	struct mutex rma_lock;
	spinlock_t tc_lock;
	struct mutex mmn_lock;
	atomic_t tw_refcount;
	atomic_t tcw_refcount;
	atomic_t tcw_total_pages;
	struct list_head mmn_list;
	atomic_t fence_refcount;
	struct dma_chan	*dma_chan;
	int async_list_del;
	struct list_head vma_list;
	wait_queue_head_t markwq;
};

/*
 * struct scif_fence_info - used for tracking fence requests
 *
 * @state: State of this transfer
 * @wq: Fences wait on this queue
 * @dma_mark: Used for storing the DMA mark
 */
struct scif_fence_info {
	enum scif_msg_state state;
	struct completion comp;
	int dma_mark;
};

/*
 * struct scif_remote_fence_info - used for tracking remote fence requests
 *
 * @msg: List of SCIF node QP fence messages
 * @list: Link to list of remote fence requests
 */
struct scif_remote_fence_info {
	struct scifmsg msg;
	struct list_head list;
};

/*
 * Specifies whether an RMA operation can span across partial windows, a single
 * window or multiple contiguous windows. Mmaps can span across partial windows.
 * Unregistration can span across complete windows. scif_get_pages() can span a
 * single window. A window can also be of type self or peer.
 */
enum scif_window_type {
	SCIF_WINDOW_PARTIAL,
	SCIF_WINDOW_SINGLE,
	SCIF_WINDOW_FULL,
	SCIF_WINDOW_SELF,
	SCIF_WINDOW_PEER
};

/* The number of physical addresses that can be stored in a PAGE. */
#define SCIF_NR_ADDR_IN_PAGE   (0x1000 >> 3)

/*
 * struct scif_rma_lookup - RMA lookup data structure for page list transfers
 *
 * Store an array of lookup offsets. Each offset in this array maps
 * one 4K page containing 512 physical addresses i.e. 2MB. 512 such
 * offsets in a 4K page will correspond to 1GB of registered address space.

 * @lookup: Array of offsets
 * @offset: DMA offset of lookup array
 */
struct scif_rma_lookup {
	dma_addr_t *lookup;
	dma_addr_t offset;
};

/*
 * struct scif_pinned_pages - A set of pinned pages obtained with
 * scif_pin_pages() which could be part of multiple registered
 * windows across different end points.
 *
 * @nr_pages: Number of pages which is defined as a s64 instead of an int
 * to avoid sign extension with buffers >= 2GB
 * @prot: read/write protections
 * @map_flags: Flags specified during the pin operation
 * @ref_count: Reference count bumped in terms of number of pages
 * @magic: A magic value
 * @pages: Array of pointers to struct pages populated with get_user_pages(..)
 */
struct scif_pinned_pages {
	s64 nr_pages;
	int prot;
	int map_flags;
	atomic_t ref_count;
	u64 magic;
	struct page **pages;
};

/*
 * struct scif_status - Stores DMA status update information
 *
 * @src_dma_addr: Source buffer DMA address
 * @val: src location for value to be written to the destination
 * @ep: SCIF endpoint
 */
struct scif_status {
	dma_addr_t src_dma_addr;
	u64 val;
	struct scif_endpt *ep;
};

/*
 * struct scif_cb_arg - Stores the argument of the callback func
 *
 * @src_dma_addr: Source buffer DMA address
 * @status: DMA status
 * @ep: SCIF endpoint
 */
struct scif_cb_arg {
	dma_addr_t src_dma_addr;
	struct scif_status *status;
	struct scif_endpt *ep;
};

/*
 * struct scif_window - Registration Window for Self and Remote
 *
 * @nr_pages: Number of pages which is defined as a s64 instead of an int
 * to avoid sign extension with buffers >= 2GB
 * @nr_contig_chunks: Number of contiguous physical chunks
 * @prot: read/write protections
 * @ref_count: reference count in terms of number of pages
 * @magic: Cookie to detect corruption
 * @offset: registered offset
 * @va_for_temp: va address that this window represents
 * @dma_mark: Used to determine if all DMAs against the window are done
 * @ep: Pointer to EP. Useful for passing EP around with messages to
	avoid expensive list traversals.
 * @list: link to list of windows for the endpoint
 * @type: self or peer window
 * @peer_window: Pointer to peer window. Useful for sending messages to peer
 *		 without requiring an extra list traversal
 * @unreg_state: unregistration state
 * @offset_freed: True if the offset has been freed
 * @temp: True for temporary windows created via scif_vreadfrom/scif_vwriteto
 * @mm: memory descriptor for the task_struct which initiated the RMA
 * @st: scatter gather table for DMA mappings with IOMMU enabled
 * @pinned_pages: The set of pinned_pages backing this window
 * @alloc_handle: Handle for sending ALLOC_REQ
 * @regwq: Wait Queue for an registration (N)ACK
 * @reg_state: Registration state
 * @unregwq: Wait Queue for an unregistration (N)ACK
 * @dma_addr_lookup: Lookup for physical addresses used for DMA
 * @nr_lookup: Number of entries in lookup
 * @mapped_offset: Offset used to map the window by the peer
 * @dma_addr: Array of physical addresses used for Mgmt node & MIC initiated DMA
 * @num_pages: Array specifying number of pages for each physical address
 */
struct scif_window {
	s64 nr_pages;
	int nr_contig_chunks;
	int prot;
	int ref_count;
	u64 magic;
	s64 offset;
	unsigned long va_for_temp;
	int dma_mark;
	u64 ep;
	struct list_head list;
	enum scif_window_type type;
	u64 peer_window;
	enum scif_msg_state unreg_state;
	bool offset_freed;
	bool temp;
	struct mm_struct *mm;
	struct sg_table *st;
	union {
		struct {
			struct scif_pinned_pages *pinned_pages;
			struct scif_allocmsg alloc_handle;
			wait_queue_head_t regwq;
			enum scif_msg_state reg_state;
			wait_queue_head_t unregwq;
		};
		struct {
			struct scif_rma_lookup dma_addr_lookup;
			struct scif_rma_lookup num_pages_lookup;
			int nr_lookup;
			dma_addr_t mapped_offset;
		};
	};
	dma_addr_t *dma_addr;
	u64 *num_pages;
} __packed;

/*
 * scif_mmu_notif - SCIF mmu notifier information
 *
 * @mmu_notifier ep_mmu_notifier: MMU notifier operations
 * @tc_reg_list: List of temp registration windows for self
 * @mm: memory descriptor for the task_struct which initiated the RMA
 * @ep: SCIF endpoint
 * @list: link to list of MMU notifier information
 */
struct scif_mmu_notif {
#ifdef CONFIG_MMU_NOTIFIER
	struct mmu_notifier ep_mmu_notifier;
#endif
	struct list_head tc_reg_list;
	struct mm_struct *mm;
	struct scif_endpt *ep;
	struct list_head list;
};

enum scif_rma_dir {
	SCIF_LOCAL_TO_REMOTE,
	SCIF_REMOTE_TO_LOCAL
};

extern struct kmem_cache *unaligned_cache;
/* Initialize RMA for this EP */
void scif_rma_ep_init(struct scif_endpt *ep);
/* Check if epd can be uninitialized */
int scif_rma_ep_can_uninit(struct scif_endpt *ep);
/* Obtain a new offset. Callee must grab RMA lock */
int scif_get_window_offset(struct scif_endpt *ep, int flags,
			   s64 offset, int nr_pages, s64 *out_offset);
/* Free offset. Callee must grab RMA lock */
void scif_free_window_offset(struct scif_endpt *ep,
			     struct scif_window *window, s64 offset);
/* Create self registration window */
struct scif_window *scif_create_window(struct scif_endpt *ep, int nr_pages,
				       s64 offset, bool temp);
/* Destroy self registration window.*/
int scif_destroy_window(struct scif_endpt *ep, struct scif_window *window);
void scif_unmap_window(struct scif_dev *remote_dev, struct scif_window *window);
/* Map pages of self window to Aperture/PCI */
int scif_map_window(struct scif_dev *remote_dev,
		    struct scif_window *window);
/* Unregister a self window */
int scif_unregister_window(struct scif_window *window);
/* Destroy remote registration window */
void
scif_destroy_remote_window(struct scif_window *window);
/* remove valid remote memory mappings from process address space */
void scif_zap_mmaps(int node);
/* Query if any applications have remote memory mappings */
bool scif_rma_do_apps_have_mmaps(int node);
/* Cleanup remote registration lists for zombie endpoints */
void scif_cleanup_rma_for_zombies(int node);
/* Reserve a DMA channel for a particular endpoint */
int scif_reserve_dma_chan(struct scif_endpt *ep);
/* Setup a DMA mark for an endpoint */
int _scif_fence_mark(scif_epd_t epd, int *mark);
int scif_prog_signal(scif_epd_t epd, off_t offset, u64 val,
		     enum scif_window_type type);
void scif_alloc_req(struct scif_dev *scifdev, struct scifmsg *msg);
void scif_alloc_gnt_rej(struct scif_dev *scifdev, struct scifmsg *msg);
void scif_free_virt(struct scif_dev *scifdev, struct scifmsg *msg);
void scif_recv_reg(struct scif_dev *scifdev, struct scifmsg *msg);
void scif_recv_unreg(struct scif_dev *scifdev, struct scifmsg *msg);
void scif_recv_reg_ack(struct scif_dev *scifdev, struct scifmsg *msg);
void scif_recv_reg_nack(struct scif_dev *scifdev, struct scifmsg *msg);
void scif_recv_unreg_ack(struct scif_dev *scifdev, struct scifmsg *msg);
void scif_recv_unreg_nack(struct scif_dev *scifdev, struct scifmsg *msg);
void scif_recv_munmap(struct scif_dev *scifdev, struct scifmsg *msg);
void scif_recv_mark(struct scif_dev *scifdev, struct scifmsg *msg);
void scif_recv_mark_resp(struct scif_dev *scifdev, struct scifmsg *msg);
void scif_recv_wait(struct scif_dev *scifdev, struct scifmsg *msg);
void scif_recv_wait_resp(struct scif_dev *scifdev, struct scifmsg *msg);
void scif_recv_sig_local(struct scif_dev *scifdev, struct scifmsg *msg);
void scif_recv_sig_remote(struct scif_dev *scifdev, struct scifmsg *msg);
void scif_recv_sig_resp(struct scif_dev *scifdev, struct scifmsg *msg);
void scif_mmu_notif_handler(struct work_struct *work);
void scif_rma_handle_remote_fences(void);
void scif_rma_destroy_windows(void);
void scif_rma_destroy_tcw_invalid(void);
int scif_drain_dma_intr(struct scif_hw_dev *sdev, struct dma_chan *chan);

struct scif_window_iter {
	s64 offset;
	int index;
};

static inline void
scif_init_window_iter(struct scif_window *window, struct scif_window_iter *iter)
{
	iter->offset = window->offset;
	iter->index = 0;
}

dma_addr_t scif_off_to_dma_addr(struct scif_window *window, s64 off,
				size_t *nr_bytes,
				struct scif_window_iter *iter);
static inline
dma_addr_t __scif_off_to_dma_addr(struct scif_window *window, s64 off)
{
	return scif_off_to_dma_addr(window, off, NULL, NULL);
}

static inline bool scif_unaligned(off_t src_offset, off_t dst_offset)
{
	src_offset = src_offset & (L1_CACHE_BYTES - 1);
	dst_offset = dst_offset & (L1_CACHE_BYTES - 1);
	return !(src_offset == dst_offset);
}

/*
 * scif_zalloc:
 * @size: Size of the allocation request.
 *
 * Helper API which attempts to allocate zeroed pages via
 * __get_free_pages(..) first and then falls back on
 * vzalloc(..) if that fails.
 */
static inline void *scif_zalloc(size_t size)
{
	void *ret = NULL;
	size_t align = ALIGN(size, PAGE_SIZE);

	if (align && get_order(align) < MAX_ORDER)
		ret = (void *)__get_free_pages(GFP_KERNEL | __GFP_ZERO,
					       get_order(align));
	return ret ? ret : vzalloc(align);
}

/*
 * scif_free:
 * @addr: Address to be freed.
 * @size: Size of the allocation.
 * Helper API which frees memory allocated via scif_zalloc().
 */
static inline void scif_free(void *addr, size_t size)
{
	size_t align = ALIGN(size, PAGE_SIZE);

	if (is_vmalloc_addr(addr))
		vfree(addr);
	else
		free_pages((unsigned long)addr, get_order(align));
}

static inline void scif_get_window(struct scif_window *window, int nr_pages)
{
	window->ref_count += nr_pages;
}

static inline void scif_put_window(struct scif_window *window, int nr_pages)
{
	window->ref_count -= nr_pages;
}

static inline void scif_set_window_ref(struct scif_window *window, int nr_pages)
{
	window->ref_count = nr_pages;
}

static inline void
scif_queue_for_cleanup(struct scif_window *window, struct list_head *list)
{
	spin_lock(&scif_info.rmalock);
	list_add_tail(&window->list, list);
	spin_unlock(&scif_info.rmalock);
	schedule_work(&scif_info.misc_work);
}

static inline void __scif_rma_destroy_tcw_helper(struct scif_window *window)
{
	list_del_init(&window->list);
	scif_queue_for_cleanup(window, &scif_info.rma_tc);
}

static inline bool scif_is_iommu_enabled(void)
{
#ifdef CONFIG_INTEL_IOMMU
	return intel_iommu_enabled;
#else
	return false;
#endif
}
#endif /* SCIF_RMA_H */
