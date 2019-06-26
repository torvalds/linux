/*
 * Intel MIC Platform Software Stack (MPSS)
 *
 * Copyright(c) 2015 Intel Corporation.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License, version 2, as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
 * General Public License for more details.
 *
 * Intel SCIF driver.
 *
 */
#include <linux/intel-iommu.h>
#include <linux/pagemap.h>
#include <linux/sched/mm.h>
#include <linux/sched/signal.h>

#include "scif_main.h"
#include "scif_map.h"

/* Used to skip ulimit checks for registrations with SCIF_MAP_KERNEL flag */
#define SCIF_MAP_ULIMIT 0x40

bool scif_ulimit_check = 1;

/**
 * scif_rma_ep_init:
 * @ep: end point
 *
 * Initialize RMA per EP data structures.
 */
void scif_rma_ep_init(struct scif_endpt *ep)
{
	struct scif_endpt_rma_info *rma = &ep->rma_info;

	mutex_init(&rma->rma_lock);
	init_iova_domain(&rma->iovad, PAGE_SIZE, SCIF_IOVA_START_PFN);
	spin_lock_init(&rma->tc_lock);
	mutex_init(&rma->mmn_lock);
	INIT_LIST_HEAD(&rma->reg_list);
	INIT_LIST_HEAD(&rma->remote_reg_list);
	atomic_set(&rma->tw_refcount, 0);
	atomic_set(&rma->tcw_refcount, 0);
	atomic_set(&rma->tcw_total_pages, 0);
	atomic_set(&rma->fence_refcount, 0);

	rma->async_list_del = 0;
	rma->dma_chan = NULL;
	INIT_LIST_HEAD(&rma->mmn_list);
	INIT_LIST_HEAD(&rma->vma_list);
	init_waitqueue_head(&rma->markwq);
}

/**
 * scif_rma_ep_can_uninit:
 * @ep: end point
 *
 * Returns 1 if an endpoint can be uninitialized and 0 otherwise.
 */
int scif_rma_ep_can_uninit(struct scif_endpt *ep)
{
	int ret = 0;

	mutex_lock(&ep->rma_info.rma_lock);
	/* Destroy RMA Info only if both lists are empty */
	if (list_empty(&ep->rma_info.reg_list) &&
	    list_empty(&ep->rma_info.remote_reg_list) &&
	    list_empty(&ep->rma_info.mmn_list) &&
	    !atomic_read(&ep->rma_info.tw_refcount) &&
	    !atomic_read(&ep->rma_info.tcw_refcount) &&
	    !atomic_read(&ep->rma_info.fence_refcount))
		ret = 1;
	mutex_unlock(&ep->rma_info.rma_lock);
	return ret;
}

/**
 * scif_create_pinned_pages:
 * @nr_pages: number of pages in window
 * @prot: read/write protection
 *
 * Allocate and prepare a set of pinned pages.
 */
static struct scif_pinned_pages *
scif_create_pinned_pages(int nr_pages, int prot)
{
	struct scif_pinned_pages *pin;

	might_sleep();
	pin = scif_zalloc(sizeof(*pin));
	if (!pin)
		goto error;

	pin->pages = scif_zalloc(nr_pages * sizeof(*pin->pages));
	if (!pin->pages)
		goto error_free_pinned_pages;

	pin->prot = prot;
	pin->magic = SCIFEP_MAGIC;
	return pin;

error_free_pinned_pages:
	scif_free(pin, sizeof(*pin));
error:
	return NULL;
}

/**
 * scif_destroy_pinned_pages:
 * @pin: A set of pinned pages.
 *
 * Deallocate resources for pinned pages.
 */
static int scif_destroy_pinned_pages(struct scif_pinned_pages *pin)
{
	int j;
	int writeable = pin->prot & SCIF_PROT_WRITE;
	int kernel = SCIF_MAP_KERNEL & pin->map_flags;

	for (j = 0; j < pin->nr_pages; j++) {
		if (pin->pages[j] && !kernel) {
			if (writeable)
				SetPageDirty(pin->pages[j]);
			put_page(pin->pages[j]);
		}
	}

	scif_free(pin->pages,
		  pin->nr_pages * sizeof(*pin->pages));
	scif_free(pin, sizeof(*pin));
	return 0;
}

/*
 * scif_create_window:
 * @ep: end point
 * @nr_pages: number of pages
 * @offset: registration offset
 * @temp: true if a temporary window is being created
 *
 * Allocate and prepare a self registration window.
 */
struct scif_window *scif_create_window(struct scif_endpt *ep, int nr_pages,
				       s64 offset, bool temp)
{
	struct scif_window *window;

	might_sleep();
	window = scif_zalloc(sizeof(*window));
	if (!window)
		goto error;

	window->dma_addr = scif_zalloc(nr_pages * sizeof(*window->dma_addr));
	if (!window->dma_addr)
		goto error_free_window;

	window->num_pages = scif_zalloc(nr_pages * sizeof(*window->num_pages));
	if (!window->num_pages)
		goto error_free_window;

	window->offset = offset;
	window->ep = (u64)ep;
	window->magic = SCIFEP_MAGIC;
	window->reg_state = OP_IDLE;
	init_waitqueue_head(&window->regwq);
	window->unreg_state = OP_IDLE;
	init_waitqueue_head(&window->unregwq);
	INIT_LIST_HEAD(&window->list);
	window->type = SCIF_WINDOW_SELF;
	window->temp = temp;
	return window;

error_free_window:
	scif_free(window->dma_addr,
		  nr_pages * sizeof(*window->dma_addr));
	scif_free(window, sizeof(*window));
error:
	return NULL;
}

/**
 * scif_destroy_incomplete_window:
 * @ep: end point
 * @window: registration window
 *
 * Deallocate resources for self window.
 */
static void scif_destroy_incomplete_window(struct scif_endpt *ep,
					   struct scif_window *window)
{
	int err;
	int nr_pages = window->nr_pages;
	struct scif_allocmsg *alloc = &window->alloc_handle;
	struct scifmsg msg;

retry:
	/* Wait for a SCIF_ALLOC_GNT/REJ message */
	err = wait_event_timeout(alloc->allocwq,
				 alloc->state != OP_IN_PROGRESS,
				 SCIF_NODE_ALIVE_TIMEOUT);
	if (!err && scifdev_alive(ep))
		goto retry;

	mutex_lock(&ep->rma_info.rma_lock);
	if (alloc->state == OP_COMPLETED) {
		msg.uop = SCIF_FREE_VIRT;
		msg.src = ep->port;
		msg.payload[0] = ep->remote_ep;
		msg.payload[1] = window->alloc_handle.vaddr;
		msg.payload[2] = (u64)window;
		msg.payload[3] = SCIF_REGISTER;
		_scif_nodeqp_send(ep->remote_dev, &msg);
	}
	mutex_unlock(&ep->rma_info.rma_lock);

	scif_free_window_offset(ep, window, window->offset);
	scif_free(window->dma_addr, nr_pages * sizeof(*window->dma_addr));
	scif_free(window->num_pages, nr_pages * sizeof(*window->num_pages));
	scif_free(window, sizeof(*window));
}

/**
 * scif_unmap_window:
 * @remote_dev: SCIF remote device
 * @window: registration window
 *
 * Delete any DMA mappings created for a registered self window
 */
void scif_unmap_window(struct scif_dev *remote_dev, struct scif_window *window)
{
	int j;

	if (scif_is_iommu_enabled() && !scifdev_self(remote_dev)) {
		if (window->st) {
			dma_unmap_sg(&remote_dev->sdev->dev,
				     window->st->sgl, window->st->nents,
				     DMA_BIDIRECTIONAL);
			sg_free_table(window->st);
			kfree(window->st);
			window->st = NULL;
		}
	} else {
		for (j = 0; j < window->nr_contig_chunks; j++) {
			if (window->dma_addr[j]) {
				scif_unmap_single(window->dma_addr[j],
						  remote_dev,
						  window->num_pages[j] <<
						  PAGE_SHIFT);
				window->dma_addr[j] = 0x0;
			}
		}
	}
}

static inline struct mm_struct *__scif_acquire_mm(void)
{
	if (scif_ulimit_check)
		return get_task_mm(current);
	return NULL;
}

static inline void __scif_release_mm(struct mm_struct *mm)
{
	if (mm)
		mmput(mm);
}

static inline int
__scif_dec_pinned_vm_lock(struct mm_struct *mm,
			  int nr_pages)
{
	if (!mm || !nr_pages || !scif_ulimit_check)
		return 0;

	atomic64_sub(nr_pages, &mm->pinned_vm);
	return 0;
}

static inline int __scif_check_inc_pinned_vm(struct mm_struct *mm,
					     int nr_pages)
{
	unsigned long locked, lock_limit;

	if (!mm || !nr_pages || !scif_ulimit_check)
		return 0;

	lock_limit = rlimit(RLIMIT_MEMLOCK) >> PAGE_SHIFT;
	locked = atomic64_add_return(nr_pages, &mm->pinned_vm);

	if ((locked > lock_limit) && !capable(CAP_IPC_LOCK)) {
		atomic64_sub(nr_pages, &mm->pinned_vm);
		dev_err(scif_info.mdev.this_device,
			"locked(%lu) > lock_limit(%lu)\n",
			locked, lock_limit);
		return -ENOMEM;
	}
	return 0;
}

/**
 * scif_destroy_window:
 * @ep: end point
 * @window: registration window
 *
 * Deallocate resources for self window.
 */
int scif_destroy_window(struct scif_endpt *ep, struct scif_window *window)
{
	int j;
	struct scif_pinned_pages *pinned_pages = window->pinned_pages;
	int nr_pages = window->nr_pages;

	might_sleep();
	if (!window->temp && window->mm) {
		__scif_dec_pinned_vm_lock(window->mm, window->nr_pages);
		__scif_release_mm(window->mm);
		window->mm = NULL;
	}

	scif_free_window_offset(ep, window, window->offset);
	scif_unmap_window(ep->remote_dev, window);
	/*
	 * Decrement references for this set of pinned pages from
	 * this window.
	 */
	j = atomic_sub_return(1, &pinned_pages->ref_count);
	if (j < 0)
		dev_err(scif_info.mdev.this_device,
			"%s %d incorrect ref count %d\n",
			__func__, __LINE__, j);
	/*
	 * If the ref count for pinned_pages is zero then someone
	 * has already called scif_unpin_pages() for it and we should
	 * destroy the page cache.
	 */
	if (!j)
		scif_destroy_pinned_pages(window->pinned_pages);
	scif_free(window->dma_addr, nr_pages * sizeof(*window->dma_addr));
	scif_free(window->num_pages, nr_pages * sizeof(*window->num_pages));
	window->magic = 0;
	scif_free(window, sizeof(*window));
	return 0;
}

/**
 * scif_create_remote_lookup:
 * @remote_dev: SCIF remote device
 * @window: remote window
 *
 * Allocate and prepare lookup entries for the remote
 * end to copy over the physical addresses.
 * Returns 0 on success and appropriate errno on failure.
 */
static int scif_create_remote_lookup(struct scif_dev *remote_dev,
				     struct scif_window *window)
{
	int i, j, err = 0;
	int nr_pages = window->nr_pages;
	bool vmalloc_dma_phys, vmalloc_num_pages;

	might_sleep();
	/* Map window */
	err = scif_map_single(&window->mapped_offset,
			      window, remote_dev, sizeof(*window));
	if (err)
		goto error_window;

	/* Compute the number of lookup entries. 21 == 2MB Shift */
	window->nr_lookup = ALIGN(nr_pages * PAGE_SIZE,
					((2) * 1024 * 1024)) >> 21;

	window->dma_addr_lookup.lookup =
		scif_alloc_coherent(&window->dma_addr_lookup.offset,
				    remote_dev, window->nr_lookup *
				    sizeof(*window->dma_addr_lookup.lookup),
				    GFP_KERNEL | __GFP_ZERO);
	if (!window->dma_addr_lookup.lookup) {
		err = -ENOMEM;
		goto error_window;
	}

	window->num_pages_lookup.lookup =
		scif_alloc_coherent(&window->num_pages_lookup.offset,
				    remote_dev, window->nr_lookup *
				    sizeof(*window->num_pages_lookup.lookup),
				    GFP_KERNEL | __GFP_ZERO);
	if (!window->num_pages_lookup.lookup) {
		err = -ENOMEM;
		goto error_window;
	}

	vmalloc_dma_phys = is_vmalloc_addr(&window->dma_addr[0]);
	vmalloc_num_pages = is_vmalloc_addr(&window->num_pages[0]);

	/* Now map each of the pages containing physical addresses */
	for (i = 0, j = 0; i < nr_pages; i += SCIF_NR_ADDR_IN_PAGE, j++) {
		err = scif_map_page(&window->dma_addr_lookup.lookup[j],
				    vmalloc_dma_phys ?
				    vmalloc_to_page(&window->dma_addr[i]) :
				    virt_to_page(&window->dma_addr[i]),
				    remote_dev);
		if (err)
			goto error_window;
		err = scif_map_page(&window->num_pages_lookup.lookup[j],
				    vmalloc_num_pages ?
				    vmalloc_to_page(&window->num_pages[i]) :
				    virt_to_page(&window->num_pages[i]),
				    remote_dev);
		if (err)
			goto error_window;
	}
	return 0;
error_window:
	return err;
}

/**
 * scif_destroy_remote_lookup:
 * @remote_dev: SCIF remote device
 * @window: remote window
 *
 * Destroy lookup entries used for the remote
 * end to copy over the physical addresses.
 */
static void scif_destroy_remote_lookup(struct scif_dev *remote_dev,
				       struct scif_window *window)
{
	int i, j;

	if (window->nr_lookup) {
		struct scif_rma_lookup *lup = &window->dma_addr_lookup;
		struct scif_rma_lookup *npup = &window->num_pages_lookup;

		for (i = 0, j = 0; i < window->nr_pages;
			i += SCIF_NR_ADDR_IN_PAGE, j++) {
			if (lup->lookup && lup->lookup[j])
				scif_unmap_single(lup->lookup[j],
						  remote_dev,
						  PAGE_SIZE);
			if (npup->lookup && npup->lookup[j])
				scif_unmap_single(npup->lookup[j],
						  remote_dev,
						  PAGE_SIZE);
		}
		if (lup->lookup)
			scif_free_coherent(lup->lookup, lup->offset,
					   remote_dev, window->nr_lookup *
					   sizeof(*lup->lookup));
		if (npup->lookup)
			scif_free_coherent(npup->lookup, npup->offset,
					   remote_dev, window->nr_lookup *
					   sizeof(*npup->lookup));
		if (window->mapped_offset)
			scif_unmap_single(window->mapped_offset,
					  remote_dev, sizeof(*window));
		window->nr_lookup = 0;
	}
}

/**
 * scif_create_remote_window:
 * @ep: end point
 * @nr_pages: number of pages in window
 *
 * Allocate and prepare a remote registration window.
 */
static struct scif_window *
scif_create_remote_window(struct scif_dev *scifdev, int nr_pages)
{
	struct scif_window *window;

	might_sleep();
	window = scif_zalloc(sizeof(*window));
	if (!window)
		goto error_ret;

	window->magic = SCIFEP_MAGIC;
	window->nr_pages = nr_pages;

	window->dma_addr = scif_zalloc(nr_pages * sizeof(*window->dma_addr));
	if (!window->dma_addr)
		goto error_window;

	window->num_pages = scif_zalloc(nr_pages *
					sizeof(*window->num_pages));
	if (!window->num_pages)
		goto error_window;

	if (scif_create_remote_lookup(scifdev, window))
		goto error_window;

	window->type = SCIF_WINDOW_PEER;
	window->unreg_state = OP_IDLE;
	INIT_LIST_HEAD(&window->list);
	return window;
error_window:
	scif_destroy_remote_window(window);
error_ret:
	return NULL;
}

/**
 * scif_destroy_remote_window:
 * @ep: end point
 * @window: remote registration window
 *
 * Deallocate resources for remote window.
 */
void
scif_destroy_remote_window(struct scif_window *window)
{
	scif_free(window->dma_addr, window->nr_pages *
		  sizeof(*window->dma_addr));
	scif_free(window->num_pages, window->nr_pages *
		  sizeof(*window->num_pages));
	window->magic = 0;
	scif_free(window, sizeof(*window));
}

/**
 * scif_iommu_map: create DMA mappings if the IOMMU is enabled
 * @remote_dev: SCIF remote device
 * @window: remote registration window
 *
 * Map the physical pages using dma_map_sg(..) and then detect the number
 * of contiguous DMA mappings allocated
 */
static int scif_iommu_map(struct scif_dev *remote_dev,
			  struct scif_window *window)
{
	struct scatterlist *sg;
	int i, err;
	scif_pinned_pages_t pin = window->pinned_pages;

	window->st = kzalloc(sizeof(*window->st), GFP_KERNEL);
	if (!window->st)
		return -ENOMEM;

	err = sg_alloc_table(window->st, window->nr_pages, GFP_KERNEL);
	if (err)
		return err;

	for_each_sg(window->st->sgl, sg, window->st->nents, i)
		sg_set_page(sg, pin->pages[i], PAGE_SIZE, 0x0);

	err = dma_map_sg(&remote_dev->sdev->dev, window->st->sgl,
			 window->st->nents, DMA_BIDIRECTIONAL);
	if (!err)
		return -ENOMEM;
	/* Detect contiguous ranges of DMA mappings */
	sg = window->st->sgl;
	for (i = 0; sg; i++) {
		dma_addr_t last_da;

		window->dma_addr[i] = sg_dma_address(sg);
		window->num_pages[i] = sg_dma_len(sg) >> PAGE_SHIFT;
		last_da = sg_dma_address(sg) + sg_dma_len(sg);
		while ((sg = sg_next(sg)) && sg_dma_address(sg) == last_da) {
			window->num_pages[i] +=
				(sg_dma_len(sg) >> PAGE_SHIFT);
			last_da = window->dma_addr[i] +
				sg_dma_len(sg);
		}
		window->nr_contig_chunks++;
	}
	return 0;
}

/**
 * scif_map_window:
 * @remote_dev: SCIF remote device
 * @window: self registration window
 *
 * Map pages of a window into the aperture/PCI.
 * Also determine addresses required for DMA.
 */
int
scif_map_window(struct scif_dev *remote_dev, struct scif_window *window)
{
	int i, j, k, err = 0, nr_contig_pages;
	scif_pinned_pages_t pin;
	phys_addr_t phys_prev, phys_curr;

	might_sleep();

	pin = window->pinned_pages;

	if (intel_iommu_enabled && !scifdev_self(remote_dev))
		return scif_iommu_map(remote_dev, window);

	for (i = 0, j = 0; i < window->nr_pages; i += nr_contig_pages, j++) {
		phys_prev = page_to_phys(pin->pages[i]);
		nr_contig_pages = 1;

		/* Detect physically contiguous chunks */
		for (k = i + 1; k < window->nr_pages; k++) {
			phys_curr = page_to_phys(pin->pages[k]);
			if (phys_curr != (phys_prev + PAGE_SIZE))
				break;
			phys_prev = phys_curr;
			nr_contig_pages++;
		}
		window->num_pages[j] = nr_contig_pages;
		window->nr_contig_chunks++;
		if (scif_is_mgmt_node()) {
			/*
			 * Management node has to deal with SMPT on X100 and
			 * hence the DMA mapping is required
			 */
			err = scif_map_single(&window->dma_addr[j],
					      phys_to_virt(page_to_phys(
							   pin->pages[i])),
					      remote_dev,
					      nr_contig_pages << PAGE_SHIFT);
			if (err)
				return err;
		} else {
			window->dma_addr[j] = page_to_phys(pin->pages[i]);
		}
	}
	return err;
}

/**
 * scif_send_scif_unregister:
 * @ep: end point
 * @window: self registration window
 *
 * Send a SCIF_UNREGISTER message.
 */
static int scif_send_scif_unregister(struct scif_endpt *ep,
				     struct scif_window *window)
{
	struct scifmsg msg;

	msg.uop = SCIF_UNREGISTER;
	msg.src = ep->port;
	msg.payload[0] = window->alloc_handle.vaddr;
	msg.payload[1] = (u64)window;
	return scif_nodeqp_send(ep->remote_dev, &msg);
}

/**
 * scif_unregister_window:
 * @window: self registration window
 *
 * Send an unregistration request and wait for a response.
 */
int scif_unregister_window(struct scif_window *window)
{
	int err = 0;
	struct scif_endpt *ep = (struct scif_endpt *)window->ep;
	bool send_msg = false;

	might_sleep();
	switch (window->unreg_state) {
	case OP_IDLE:
	{
		window->unreg_state = OP_IN_PROGRESS;
		send_msg = true;
	}
		/* fall through */
	case OP_IN_PROGRESS:
	{
		scif_get_window(window, 1);
		mutex_unlock(&ep->rma_info.rma_lock);
		if (send_msg) {
			err = scif_send_scif_unregister(ep, window);
			if (err) {
				window->unreg_state = OP_COMPLETED;
				goto done;
			}
		} else {
			/* Return ENXIO since unregistration is in progress */
			mutex_lock(&ep->rma_info.rma_lock);
			return -ENXIO;
		}
retry:
		/* Wait for a SCIF_UNREGISTER_(N)ACK message */
		err = wait_event_timeout(window->unregwq,
					 window->unreg_state != OP_IN_PROGRESS,
					 SCIF_NODE_ALIVE_TIMEOUT);
		if (!err && scifdev_alive(ep))
			goto retry;
		if (!err) {
			err = -ENODEV;
			window->unreg_state = OP_COMPLETED;
			dev_err(scif_info.mdev.this_device,
				"%s %d err %d\n", __func__, __LINE__, err);
		}
		if (err > 0)
			err = 0;
done:
		mutex_lock(&ep->rma_info.rma_lock);
		scif_put_window(window, 1);
		break;
	}
	case OP_FAILED:
	{
		if (!scifdev_alive(ep)) {
			err = -ENODEV;
			window->unreg_state = OP_COMPLETED;
		}
		break;
	}
	case OP_COMPLETED:
		break;
	default:
		err = -ENODEV;
	}

	if (window->unreg_state == OP_COMPLETED && window->ref_count)
		scif_put_window(window, window->nr_pages);

	if (!window->ref_count) {
		atomic_inc(&ep->rma_info.tw_refcount);
		list_del_init(&window->list);
		scif_free_window_offset(ep, window, window->offset);
		mutex_unlock(&ep->rma_info.rma_lock);
		if ((!!(window->pinned_pages->map_flags & SCIF_MAP_KERNEL)) &&
		    scifdev_alive(ep)) {
			scif_drain_dma_intr(ep->remote_dev->sdev,
					    ep->rma_info.dma_chan);
		} else {
			if (!__scif_dec_pinned_vm_lock(window->mm,
						       window->nr_pages)) {
				__scif_release_mm(window->mm);
				window->mm = NULL;
			}
		}
		scif_queue_for_cleanup(window, &scif_info.rma);
		mutex_lock(&ep->rma_info.rma_lock);
	}
	return err;
}

/**
 * scif_send_alloc_request:
 * @ep: end point
 * @window: self registration window
 *
 * Send a remote window allocation request
 */
static int scif_send_alloc_request(struct scif_endpt *ep,
				   struct scif_window *window)
{
	struct scifmsg msg;
	struct scif_allocmsg *alloc = &window->alloc_handle;

	/* Set up the Alloc Handle */
	alloc->state = OP_IN_PROGRESS;
	init_waitqueue_head(&alloc->allocwq);

	/* Send out an allocation request */
	msg.uop = SCIF_ALLOC_REQ;
	msg.payload[1] = window->nr_pages;
	msg.payload[2] = (u64)&window->alloc_handle;
	return _scif_nodeqp_send(ep->remote_dev, &msg);
}

/**
 * scif_prep_remote_window:
 * @ep: end point
 * @window: self registration window
 *
 * Send a remote window allocation request, wait for an allocation response,
 * and prepares the remote window by copying over the page lists
 */
static int scif_prep_remote_window(struct scif_endpt *ep,
				   struct scif_window *window)
{
	struct scifmsg msg;
	struct scif_window *remote_window;
	struct scif_allocmsg *alloc = &window->alloc_handle;
	dma_addr_t *dma_phys_lookup, *tmp, *num_pages_lookup, *tmp1;
	int i = 0, j = 0;
	int nr_contig_chunks, loop_nr_contig_chunks;
	int remaining_nr_contig_chunks, nr_lookup;
	int err, map_err;

	map_err = scif_map_window(ep->remote_dev, window);
	if (map_err)
		dev_err(&ep->remote_dev->sdev->dev,
			"%s %d map_err %d\n", __func__, __LINE__, map_err);
	remaining_nr_contig_chunks = window->nr_contig_chunks;
	nr_contig_chunks = window->nr_contig_chunks;
retry:
	/* Wait for a SCIF_ALLOC_GNT/REJ message */
	err = wait_event_timeout(alloc->allocwq,
				 alloc->state != OP_IN_PROGRESS,
				 SCIF_NODE_ALIVE_TIMEOUT);
	mutex_lock(&ep->rma_info.rma_lock);
	/* Synchronize with the thread waking up allocwq */
	mutex_unlock(&ep->rma_info.rma_lock);
	if (!err && scifdev_alive(ep))
		goto retry;

	if (!err)
		err = -ENODEV;

	if (err > 0)
		err = 0;
	else
		return err;

	/* Bail out. The remote end rejected this request */
	if (alloc->state == OP_FAILED)
		return -ENOMEM;

	if (map_err) {
		dev_err(&ep->remote_dev->sdev->dev,
			"%s %d err %d\n", __func__, __LINE__, map_err);
		msg.uop = SCIF_FREE_VIRT;
		msg.src = ep->port;
		msg.payload[0] = ep->remote_ep;
		msg.payload[1] = window->alloc_handle.vaddr;
		msg.payload[2] = (u64)window;
		msg.payload[3] = SCIF_REGISTER;
		spin_lock(&ep->lock);
		if (ep->state == SCIFEP_CONNECTED)
			err = _scif_nodeqp_send(ep->remote_dev, &msg);
		else
			err = -ENOTCONN;
		spin_unlock(&ep->lock);
		return err;
	}

	remote_window = scif_ioremap(alloc->phys_addr, sizeof(*window),
				     ep->remote_dev);

	/* Compute the number of lookup entries. 21 == 2MB Shift */
	nr_lookup = ALIGN(nr_contig_chunks, SCIF_NR_ADDR_IN_PAGE)
			  >> ilog2(SCIF_NR_ADDR_IN_PAGE);

	dma_phys_lookup =
		scif_ioremap(remote_window->dma_addr_lookup.offset,
			     nr_lookup *
			     sizeof(*remote_window->dma_addr_lookup.lookup),
			     ep->remote_dev);
	num_pages_lookup =
		scif_ioremap(remote_window->num_pages_lookup.offset,
			     nr_lookup *
			     sizeof(*remote_window->num_pages_lookup.lookup),
			     ep->remote_dev);

	while (remaining_nr_contig_chunks) {
		loop_nr_contig_chunks = min_t(int, remaining_nr_contig_chunks,
					      (int)SCIF_NR_ADDR_IN_PAGE);
		/* #1/2 - Copy  physical addresses over to the remote side */

		/* #2/2 - Copy DMA addresses (addresses that are fed into the
		 * DMA engine) We transfer bus addresses which are then
		 * converted into a MIC physical address on the remote
		 * side if it is a MIC, if the remote node is a mgmt node we
		 * transfer the MIC physical address
		 */
		tmp = scif_ioremap(dma_phys_lookup[j],
				   loop_nr_contig_chunks *
				   sizeof(*window->dma_addr),
				   ep->remote_dev);
		tmp1 = scif_ioremap(num_pages_lookup[j],
				    loop_nr_contig_chunks *
				    sizeof(*window->num_pages),
				    ep->remote_dev);
		if (scif_is_mgmt_node()) {
			memcpy_toio((void __force __iomem *)tmp,
				    &window->dma_addr[i], loop_nr_contig_chunks
				    * sizeof(*window->dma_addr));
			memcpy_toio((void __force __iomem *)tmp1,
				    &window->num_pages[i], loop_nr_contig_chunks
				    * sizeof(*window->num_pages));
		} else {
			if (scifdev_is_p2p(ep->remote_dev)) {
				/*
				 * add remote node's base address for this node
				 * to convert it into a MIC address
				 */
				int m;
				dma_addr_t dma_addr;

				for (m = 0; m < loop_nr_contig_chunks; m++) {
					dma_addr = window->dma_addr[i + m] +
						ep->remote_dev->base_addr;
					writeq(dma_addr,
					       (void __force __iomem *)&tmp[m]);
				}
				memcpy_toio((void __force __iomem *)tmp1,
					    &window->num_pages[i],
					    loop_nr_contig_chunks
					    * sizeof(*window->num_pages));
			} else {
				/* Mgmt node or loopback - transfer DMA
				 * addresses as is, this is the same as a
				 * MIC physical address (we use the dma_addr
				 * and not the phys_addr array since the
				 * phys_addr is only setup if there is a mmap()
				 * request from the mgmt node)
				 */
				memcpy_toio((void __force __iomem *)tmp,
					    &window->dma_addr[i],
					    loop_nr_contig_chunks *
					    sizeof(*window->dma_addr));
				memcpy_toio((void __force __iomem *)tmp1,
					    &window->num_pages[i],
					    loop_nr_contig_chunks *
					    sizeof(*window->num_pages));
			}
		}
		remaining_nr_contig_chunks -= loop_nr_contig_chunks;
		i += loop_nr_contig_chunks;
		j++;
		scif_iounmap(tmp, loop_nr_contig_chunks *
			     sizeof(*window->dma_addr), ep->remote_dev);
		scif_iounmap(tmp1, loop_nr_contig_chunks *
			     sizeof(*window->num_pages), ep->remote_dev);
	}

	/* Prepare the remote window for the peer */
	remote_window->peer_window = (u64)window;
	remote_window->offset = window->offset;
	remote_window->prot = window->prot;
	remote_window->nr_contig_chunks = nr_contig_chunks;
	remote_window->ep = ep->remote_ep;
	scif_iounmap(num_pages_lookup,
		     nr_lookup *
		     sizeof(*remote_window->num_pages_lookup.lookup),
		     ep->remote_dev);
	scif_iounmap(dma_phys_lookup,
		     nr_lookup *
		     sizeof(*remote_window->dma_addr_lookup.lookup),
		     ep->remote_dev);
	scif_iounmap(remote_window, sizeof(*remote_window), ep->remote_dev);
	window->peer_window = alloc->vaddr;
	return err;
}

/**
 * scif_send_scif_register:
 * @ep: end point
 * @window: self registration window
 *
 * Send a SCIF_REGISTER message if EP is connected and wait for a
 * SCIF_REGISTER_(N)ACK message else send a SCIF_FREE_VIRT
 * message so that the peer can free its remote window allocated earlier.
 */
static int scif_send_scif_register(struct scif_endpt *ep,
				   struct scif_window *window)
{
	int err = 0;
	struct scifmsg msg;

	msg.src = ep->port;
	msg.payload[0] = ep->remote_ep;
	msg.payload[1] = window->alloc_handle.vaddr;
	msg.payload[2] = (u64)window;
	spin_lock(&ep->lock);
	if (ep->state == SCIFEP_CONNECTED) {
		msg.uop = SCIF_REGISTER;
		window->reg_state = OP_IN_PROGRESS;
		err = _scif_nodeqp_send(ep->remote_dev, &msg);
		spin_unlock(&ep->lock);
		if (!err) {
retry:
			/* Wait for a SCIF_REGISTER_(N)ACK message */
			err = wait_event_timeout(window->regwq,
						 window->reg_state !=
						 OP_IN_PROGRESS,
						 SCIF_NODE_ALIVE_TIMEOUT);
			if (!err && scifdev_alive(ep))
				goto retry;
			err = !err ? -ENODEV : 0;
			if (window->reg_state == OP_FAILED)
				err = -ENOTCONN;
		}
	} else {
		msg.uop = SCIF_FREE_VIRT;
		msg.payload[3] = SCIF_REGISTER;
		err = _scif_nodeqp_send(ep->remote_dev, &msg);
		spin_unlock(&ep->lock);
		if (!err)
			err = -ENOTCONN;
	}
	return err;
}

/**
 * scif_get_window_offset:
 * @ep: end point descriptor
 * @flags: flags
 * @offset: offset hint
 * @num_pages: number of pages
 * @out_offset: computed offset returned by reference.
 *
 * Compute/Claim a new offset for this EP.
 */
int scif_get_window_offset(struct scif_endpt *ep, int flags, s64 offset,
			   int num_pages, s64 *out_offset)
{
	s64 page_index;
	struct iova *iova_ptr;
	int err = 0;

	if (flags & SCIF_MAP_FIXED) {
		page_index = SCIF_IOVA_PFN(offset);
		iova_ptr = reserve_iova(&ep->rma_info.iovad, page_index,
					page_index + num_pages - 1);
		if (!iova_ptr)
			err = -EADDRINUSE;
	} else {
		iova_ptr = alloc_iova(&ep->rma_info.iovad, num_pages,
				      SCIF_DMA_63BIT_PFN - 1, 0);
		if (!iova_ptr)
			err = -ENOMEM;
	}
	if (!err)
		*out_offset = (iova_ptr->pfn_lo) << PAGE_SHIFT;
	return err;
}

/**
 * scif_free_window_offset:
 * @ep: end point descriptor
 * @window: registration window
 * @offset: Offset to be freed
 *
 * Free offset for this EP. The callee is supposed to grab
 * the RMA mutex before calling this API.
 */
void scif_free_window_offset(struct scif_endpt *ep,
			     struct scif_window *window, s64 offset)
{
	if ((window && !window->offset_freed) || !window) {
		free_iova(&ep->rma_info.iovad, offset >> PAGE_SHIFT);
		if (window)
			window->offset_freed = true;
	}
}

/**
 * scif_alloc_req: Respond to SCIF_ALLOC_REQ interrupt message
 * @msg:        Interrupt message
 *
 * Remote side is requesting a memory allocation.
 */
void scif_alloc_req(struct scif_dev *scifdev, struct scifmsg *msg)
{
	int err;
	struct scif_window *window = NULL;
	int nr_pages = msg->payload[1];

	window = scif_create_remote_window(scifdev, nr_pages);
	if (!window) {
		err = -ENOMEM;
		goto error;
	}

	/* The peer's allocation request is granted */
	msg->uop = SCIF_ALLOC_GNT;
	msg->payload[0] = (u64)window;
	msg->payload[1] = window->mapped_offset;
	err = scif_nodeqp_send(scifdev, msg);
	if (err)
		scif_destroy_remote_window(window);
	return;
error:
	/* The peer's allocation request is rejected */
	dev_err(&scifdev->sdev->dev,
		"%s %d error %d alloc_ptr %p nr_pages 0x%x\n",
		__func__, __LINE__, err, window, nr_pages);
	msg->uop = SCIF_ALLOC_REJ;
	scif_nodeqp_send(scifdev, msg);
}

/**
 * scif_alloc_gnt_rej: Respond to SCIF_ALLOC_GNT/REJ interrupt message
 * @msg:        Interrupt message
 *
 * Remote side responded to a memory allocation.
 */
void scif_alloc_gnt_rej(struct scif_dev *scifdev, struct scifmsg *msg)
{
	struct scif_allocmsg *handle = (struct scif_allocmsg *)msg->payload[2];
	struct scif_window *window = container_of(handle, struct scif_window,
						  alloc_handle);
	struct scif_endpt *ep = (struct scif_endpt *)window->ep;

	mutex_lock(&ep->rma_info.rma_lock);
	handle->vaddr = msg->payload[0];
	handle->phys_addr = msg->payload[1];
	if (msg->uop == SCIF_ALLOC_GNT)
		handle->state = OP_COMPLETED;
	else
		handle->state = OP_FAILED;
	wake_up(&handle->allocwq);
	mutex_unlock(&ep->rma_info.rma_lock);
}

/**
 * scif_free_virt: Respond to SCIF_FREE_VIRT interrupt message
 * @msg:        Interrupt message
 *
 * Free up memory kmalloc'd earlier.
 */
void scif_free_virt(struct scif_dev *scifdev, struct scifmsg *msg)
{
	struct scif_window *window = (struct scif_window *)msg->payload[1];

	scif_destroy_remote_window(window);
}

static void
scif_fixup_aper_base(struct scif_dev *dev, struct scif_window *window)
{
	int j;
	struct scif_hw_dev *sdev = dev->sdev;
	phys_addr_t apt_base = 0;

	/*
	 * Add the aperture base if the DMA address is not card relative
	 * since the DMA addresses need to be an offset into the bar
	 */
	if (!scifdev_self(dev) && window->type == SCIF_WINDOW_PEER &&
	    sdev->aper && !sdev->card_rel_da)
		apt_base = sdev->aper->pa;
	else
		return;

	for (j = 0; j < window->nr_contig_chunks; j++) {
		if (window->num_pages[j])
			window->dma_addr[j] += apt_base;
		else
			break;
	}
}

/**
 * scif_recv_reg: Respond to SCIF_REGISTER interrupt message
 * @msg:        Interrupt message
 *
 * Update remote window list with a new registered window.
 */
void scif_recv_reg(struct scif_dev *scifdev, struct scifmsg *msg)
{
	struct scif_endpt *ep = (struct scif_endpt *)msg->payload[0];
	struct scif_window *window =
		(struct scif_window *)msg->payload[1];

	mutex_lock(&ep->rma_info.rma_lock);
	spin_lock(&ep->lock);
	if (ep->state == SCIFEP_CONNECTED) {
		msg->uop = SCIF_REGISTER_ACK;
		scif_nodeqp_send(ep->remote_dev, msg);
		scif_fixup_aper_base(ep->remote_dev, window);
		/* No further failures expected. Insert new window */
		scif_insert_window(window, &ep->rma_info.remote_reg_list);
	} else {
		msg->uop = SCIF_REGISTER_NACK;
		scif_nodeqp_send(ep->remote_dev, msg);
	}
	spin_unlock(&ep->lock);
	mutex_unlock(&ep->rma_info.rma_lock);
	/* free up any lookup resources now that page lists are transferred */
	scif_destroy_remote_lookup(ep->remote_dev, window);
	/*
	 * We could not insert the window but we need to
	 * destroy the window.
	 */
	if (msg->uop == SCIF_REGISTER_NACK)
		scif_destroy_remote_window(window);
}

/**
 * scif_recv_unreg: Respond to SCIF_UNREGISTER interrupt message
 * @msg:        Interrupt message
 *
 * Remove window from remote registration list;
 */
void scif_recv_unreg(struct scif_dev *scifdev, struct scifmsg *msg)
{
	struct scif_rma_req req;
	struct scif_window *window = NULL;
	struct scif_window *recv_window =
		(struct scif_window *)msg->payload[0];
	struct scif_endpt *ep;
	int del_window = 0;

	ep = (struct scif_endpt *)recv_window->ep;
	req.out_window = &window;
	req.offset = recv_window->offset;
	req.prot = 0;
	req.nr_bytes = recv_window->nr_pages << PAGE_SHIFT;
	req.type = SCIF_WINDOW_FULL;
	req.head = &ep->rma_info.remote_reg_list;
	msg->payload[0] = ep->remote_ep;

	mutex_lock(&ep->rma_info.rma_lock);
	/* Does a valid window exist? */
	if (scif_query_window(&req)) {
		dev_err(&scifdev->sdev->dev,
			"%s %d -ENXIO\n", __func__, __LINE__);
		msg->uop = SCIF_UNREGISTER_ACK;
		goto error;
	}
	if (window) {
		if (window->ref_count)
			scif_put_window(window, window->nr_pages);
		else
			dev_err(&scifdev->sdev->dev,
				"%s %d ref count should be +ve\n",
				__func__, __LINE__);
		window->unreg_state = OP_COMPLETED;
		if (!window->ref_count) {
			msg->uop = SCIF_UNREGISTER_ACK;
			atomic_inc(&ep->rma_info.tw_refcount);
			ep->rma_info.async_list_del = 1;
			list_del_init(&window->list);
			del_window = 1;
		} else {
			/* NACK! There are valid references to this window */
			msg->uop = SCIF_UNREGISTER_NACK;
		}
	} else {
		/* The window did not make its way to the list at all. ACK */
		msg->uop = SCIF_UNREGISTER_ACK;
		scif_destroy_remote_window(recv_window);
	}
error:
	mutex_unlock(&ep->rma_info.rma_lock);
	if (del_window)
		scif_drain_dma_intr(ep->remote_dev->sdev,
				    ep->rma_info.dma_chan);
	scif_nodeqp_send(ep->remote_dev, msg);
	if (del_window)
		scif_queue_for_cleanup(window, &scif_info.rma);
}

/**
 * scif_recv_reg_ack: Respond to SCIF_REGISTER_ACK interrupt message
 * @msg:        Interrupt message
 *
 * Wake up the window waiting to complete registration.
 */
void scif_recv_reg_ack(struct scif_dev *scifdev, struct scifmsg *msg)
{
	struct scif_window *window =
		(struct scif_window *)msg->payload[2];
	struct scif_endpt *ep = (struct scif_endpt *)window->ep;

	mutex_lock(&ep->rma_info.rma_lock);
	window->reg_state = OP_COMPLETED;
	wake_up(&window->regwq);
	mutex_unlock(&ep->rma_info.rma_lock);
}

/**
 * scif_recv_reg_nack: Respond to SCIF_REGISTER_NACK interrupt message
 * @msg:        Interrupt message
 *
 * Wake up the window waiting to inform it that registration
 * cannot be completed.
 */
void scif_recv_reg_nack(struct scif_dev *scifdev, struct scifmsg *msg)
{
	struct scif_window *window =
		(struct scif_window *)msg->payload[2];
	struct scif_endpt *ep = (struct scif_endpt *)window->ep;

	mutex_lock(&ep->rma_info.rma_lock);
	window->reg_state = OP_FAILED;
	wake_up(&window->regwq);
	mutex_unlock(&ep->rma_info.rma_lock);
}

/**
 * scif_recv_unreg_ack: Respond to SCIF_UNREGISTER_ACK interrupt message
 * @msg:        Interrupt message
 *
 * Wake up the window waiting to complete unregistration.
 */
void scif_recv_unreg_ack(struct scif_dev *scifdev, struct scifmsg *msg)
{
	struct scif_window *window =
		(struct scif_window *)msg->payload[1];
	struct scif_endpt *ep = (struct scif_endpt *)window->ep;

	mutex_lock(&ep->rma_info.rma_lock);
	window->unreg_state = OP_COMPLETED;
	wake_up(&window->unregwq);
	mutex_unlock(&ep->rma_info.rma_lock);
}

/**
 * scif_recv_unreg_nack: Respond to SCIF_UNREGISTER_NACK interrupt message
 * @msg:        Interrupt message
 *
 * Wake up the window waiting to inform it that unregistration
 * cannot be completed immediately.
 */
void scif_recv_unreg_nack(struct scif_dev *scifdev, struct scifmsg *msg)
{
	struct scif_window *window =
		(struct scif_window *)msg->payload[1];
	struct scif_endpt *ep = (struct scif_endpt *)window->ep;

	mutex_lock(&ep->rma_info.rma_lock);
	window->unreg_state = OP_FAILED;
	wake_up(&window->unregwq);
	mutex_unlock(&ep->rma_info.rma_lock);
}

int __scif_pin_pages(void *addr, size_t len, int *out_prot,
		     int map_flags, scif_pinned_pages_t *pages)
{
	struct scif_pinned_pages *pinned_pages;
	int nr_pages, err = 0, i;
	bool vmalloc_addr = false;
	bool try_upgrade = false;
	int prot = *out_prot;
	int ulimit = 0;
	struct mm_struct *mm = NULL;

	/* Unsupported flags */
	if (map_flags & ~(SCIF_MAP_KERNEL | SCIF_MAP_ULIMIT))
		return -EINVAL;
	ulimit = !!(map_flags & SCIF_MAP_ULIMIT);

	/* Unsupported protection requested */
	if (prot & ~(SCIF_PROT_READ | SCIF_PROT_WRITE))
		return -EINVAL;

	/* addr/len must be page aligned. len should be non zero */
	if (!len ||
	    (ALIGN((u64)addr, PAGE_SIZE) != (u64)addr) ||
	    (ALIGN((u64)len, PAGE_SIZE) != (u64)len))
		return -EINVAL;

	might_sleep();

	nr_pages = len >> PAGE_SHIFT;

	/* Allocate a set of pinned pages */
	pinned_pages = scif_create_pinned_pages(nr_pages, prot);
	if (!pinned_pages)
		return -ENOMEM;

	if (map_flags & SCIF_MAP_KERNEL) {
		if (is_vmalloc_addr(addr))
			vmalloc_addr = true;

		for (i = 0; i < nr_pages; i++) {
			if (vmalloc_addr)
				pinned_pages->pages[i] =
					vmalloc_to_page(addr + (i * PAGE_SIZE));
			else
				pinned_pages->pages[i] =
					virt_to_page(addr + (i * PAGE_SIZE));
		}
		pinned_pages->nr_pages = nr_pages;
		pinned_pages->map_flags = SCIF_MAP_KERNEL;
	} else {
		/*
		 * SCIF supports registration caching. If a registration has
		 * been requested with read only permissions, then we try
		 * to pin the pages with RW permissions so that a subsequent
		 * transfer with RW permission can hit the cache instead of
		 * invalidating it. If the upgrade fails with RW then we
		 * revert back to R permission and retry
		 */
		if (prot == SCIF_PROT_READ)
			try_upgrade = true;
		prot |= SCIF_PROT_WRITE;
retry:
		mm = current->mm;
		if (ulimit) {
			err = __scif_check_inc_pinned_vm(mm, nr_pages);
			if (err) {
				pinned_pages->nr_pages = 0;
				goto error_unmap;
			}
		}

		pinned_pages->nr_pages = get_user_pages_fast(
				(u64)addr,
				nr_pages,
				(prot & SCIF_PROT_WRITE) ? FOLL_WRITE : 0,
				pinned_pages->pages);
		if (nr_pages != pinned_pages->nr_pages) {
			if (try_upgrade) {
				if (ulimit)
					__scif_dec_pinned_vm_lock(mm, nr_pages);
				/* Roll back any pinned pages */
				for (i = 0; i < pinned_pages->nr_pages; i++) {
					if (pinned_pages->pages[i])
						put_page(
						pinned_pages->pages[i]);
				}
				prot &= ~SCIF_PROT_WRITE;
				try_upgrade = false;
				goto retry;
			}
		}
		pinned_pages->map_flags = 0;
	}

	if (pinned_pages->nr_pages < nr_pages) {
		err = -EFAULT;
		pinned_pages->nr_pages = nr_pages;
		goto dec_pinned;
	}

	*out_prot = prot;
	atomic_set(&pinned_pages->ref_count, 1);
	*pages = pinned_pages;
	return err;
dec_pinned:
	if (ulimit)
		__scif_dec_pinned_vm_lock(mm, nr_pages);
	/* Something went wrong! Rollback */
error_unmap:
	pinned_pages->nr_pages = nr_pages;
	scif_destroy_pinned_pages(pinned_pages);
	*pages = NULL;
	dev_dbg(scif_info.mdev.this_device,
		"%s %d err %d len 0x%lx\n", __func__, __LINE__, err, len);
	return err;
}

int scif_pin_pages(void *addr, size_t len, int prot,
		   int map_flags, scif_pinned_pages_t *pages)
{
	return __scif_pin_pages(addr, len, &prot, map_flags, pages);
}
EXPORT_SYMBOL_GPL(scif_pin_pages);

int scif_unpin_pages(scif_pinned_pages_t pinned_pages)
{
	int err = 0, ret;

	if (!pinned_pages || SCIFEP_MAGIC != pinned_pages->magic)
		return -EINVAL;

	ret = atomic_sub_return(1, &pinned_pages->ref_count);
	if (ret < 0) {
		dev_err(scif_info.mdev.this_device,
			"%s %d scif_unpin_pages called without pinning? rc %d\n",
			__func__, __LINE__, ret);
		return -EINVAL;
	}
	/*
	 * Destroy the window if the ref count for this set of pinned
	 * pages has dropped to zero. If it is positive then there is
	 * a valid registered window which is backed by these pages and
	 * it will be destroyed once all such windows are unregistered.
	 */
	if (!ret)
		err = scif_destroy_pinned_pages(pinned_pages);

	return err;
}
EXPORT_SYMBOL_GPL(scif_unpin_pages);

static inline void
scif_insert_local_window(struct scif_window *window, struct scif_endpt *ep)
{
	mutex_lock(&ep->rma_info.rma_lock);
	scif_insert_window(window, &ep->rma_info.reg_list);
	mutex_unlock(&ep->rma_info.rma_lock);
}

off_t scif_register_pinned_pages(scif_epd_t epd,
				 scif_pinned_pages_t pinned_pages,
				 off_t offset, int map_flags)
{
	struct scif_endpt *ep = (struct scif_endpt *)epd;
	s64 computed_offset;
	struct scif_window *window;
	int err;
	size_t len;
	struct device *spdev;

	/* Unsupported flags */
	if (map_flags & ~SCIF_MAP_FIXED)
		return -EINVAL;

	len = pinned_pages->nr_pages << PAGE_SHIFT;

	/*
	 * Offset is not page aligned/negative or offset+len
	 * wraps around with SCIF_MAP_FIXED.
	 */
	if ((map_flags & SCIF_MAP_FIXED) &&
	    ((ALIGN(offset, PAGE_SIZE) != offset) ||
	    (offset < 0) ||
	    (len > LONG_MAX - offset)))
		return -EINVAL;

	might_sleep();

	err = scif_verify_epd(ep);
	if (err)
		return err;
	/*
	 * It is an error to pass pinned_pages to scif_register_pinned_pages()
	 * after calling scif_unpin_pages().
	 */
	if (!atomic_add_unless(&pinned_pages->ref_count, 1, 0))
		return -EINVAL;

	/* Compute the offset for this registration */
	err = scif_get_window_offset(ep, map_flags, offset,
				     len, &computed_offset);
	if (err) {
		atomic_sub(1, &pinned_pages->ref_count);
		return err;
	}

	/* Allocate and prepare self registration window */
	window = scif_create_window(ep, pinned_pages->nr_pages,
				    computed_offset, false);
	if (!window) {
		atomic_sub(1, &pinned_pages->ref_count);
		scif_free_window_offset(ep, NULL, computed_offset);
		return -ENOMEM;
	}

	window->pinned_pages = pinned_pages;
	window->nr_pages = pinned_pages->nr_pages;
	window->prot = pinned_pages->prot;

	spdev = scif_get_peer_dev(ep->remote_dev);
	if (IS_ERR(spdev)) {
		err = PTR_ERR(spdev);
		scif_destroy_window(ep, window);
		return err;
	}
	err = scif_send_alloc_request(ep, window);
	if (err) {
		dev_err(&ep->remote_dev->sdev->dev,
			"%s %d err %d\n", __func__, __LINE__, err);
		goto error_unmap;
	}

	/* Prepare the remote registration window */
	err = scif_prep_remote_window(ep, window);
	if (err) {
		dev_err(&ep->remote_dev->sdev->dev,
			"%s %d err %d\n", __func__, __LINE__, err);
		goto error_unmap;
	}

	/* Tell the peer about the new window */
	err = scif_send_scif_register(ep, window);
	if (err) {
		dev_err(&ep->remote_dev->sdev->dev,
			"%s %d err %d\n", __func__, __LINE__, err);
		goto error_unmap;
	}

	scif_put_peer_dev(spdev);
	/* No further failures expected. Insert new window */
	scif_insert_local_window(window, ep);
	return computed_offset;
error_unmap:
	scif_destroy_window(ep, window);
	scif_put_peer_dev(spdev);
	dev_err(&ep->remote_dev->sdev->dev,
		"%s %d err %d\n", __func__, __LINE__, err);
	return err;
}
EXPORT_SYMBOL_GPL(scif_register_pinned_pages);

off_t scif_register(scif_epd_t epd, void *addr, size_t len, off_t offset,
		    int prot, int map_flags)
{
	scif_pinned_pages_t pinned_pages;
	off_t err;
	struct scif_endpt *ep = (struct scif_endpt *)epd;
	s64 computed_offset;
	struct scif_window *window;
	struct mm_struct *mm = NULL;
	struct device *spdev;

	dev_dbg(scif_info.mdev.this_device,
		"SCIFAPI register: ep %p addr %p len 0x%lx offset 0x%lx prot 0x%x map_flags 0x%x\n",
		epd, addr, len, offset, prot, map_flags);
	/* Unsupported flags */
	if (map_flags & ~(SCIF_MAP_FIXED | SCIF_MAP_KERNEL))
		return -EINVAL;

	/*
	 * Offset is not page aligned/negative or offset+len
	 * wraps around with SCIF_MAP_FIXED.
	 */
	if ((map_flags & SCIF_MAP_FIXED) &&
	    ((ALIGN(offset, PAGE_SIZE) != offset) ||
	    (offset < 0) ||
	    (len > LONG_MAX - offset)))
		return -EINVAL;

	/* Unsupported protection requested */
	if (prot & ~(SCIF_PROT_READ | SCIF_PROT_WRITE))
		return -EINVAL;

	/* addr/len must be page aligned. len should be non zero */
	if (!len || (ALIGN((u64)addr, PAGE_SIZE) != (u64)addr) ||
	    (ALIGN(len, PAGE_SIZE) != len))
		return -EINVAL;

	might_sleep();

	err = scif_verify_epd(ep);
	if (err)
		return err;

	/* Compute the offset for this registration */
	err = scif_get_window_offset(ep, map_flags, offset,
				     len >> PAGE_SHIFT, &computed_offset);
	if (err)
		return err;

	spdev = scif_get_peer_dev(ep->remote_dev);
	if (IS_ERR(spdev)) {
		err = PTR_ERR(spdev);
		scif_free_window_offset(ep, NULL, computed_offset);
		return err;
	}
	/* Allocate and prepare self registration window */
	window = scif_create_window(ep, len >> PAGE_SHIFT,
				    computed_offset, false);
	if (!window) {
		scif_free_window_offset(ep, NULL, computed_offset);
		scif_put_peer_dev(spdev);
		return -ENOMEM;
	}

	window->nr_pages = len >> PAGE_SHIFT;

	err = scif_send_alloc_request(ep, window);
	if (err) {
		scif_destroy_incomplete_window(ep, window);
		scif_put_peer_dev(spdev);
		return err;
	}

	if (!(map_flags & SCIF_MAP_KERNEL)) {
		mm = __scif_acquire_mm();
		map_flags |= SCIF_MAP_ULIMIT;
	}
	/* Pin down the pages */
	err = __scif_pin_pages(addr, len, &prot,
			       map_flags & (SCIF_MAP_KERNEL | SCIF_MAP_ULIMIT),
			       &pinned_pages);
	if (err) {
		scif_destroy_incomplete_window(ep, window);
		__scif_release_mm(mm);
		goto error;
	}

	window->pinned_pages = pinned_pages;
	window->prot = pinned_pages->prot;
	window->mm = mm;

	/* Prepare the remote registration window */
	err = scif_prep_remote_window(ep, window);
	if (err) {
		dev_err(&ep->remote_dev->sdev->dev,
			"%s %d err %ld\n", __func__, __LINE__, err);
		goto error_unmap;
	}

	/* Tell the peer about the new window */
	err = scif_send_scif_register(ep, window);
	if (err) {
		dev_err(&ep->remote_dev->sdev->dev,
			"%s %d err %ld\n", __func__, __LINE__, err);
		goto error_unmap;
	}

	scif_put_peer_dev(spdev);
	/* No further failures expected. Insert new window */
	scif_insert_local_window(window, ep);
	dev_dbg(&ep->remote_dev->sdev->dev,
		"SCIFAPI register: ep %p addr %p len 0x%lx computed_offset 0x%llx\n",
		epd, addr, len, computed_offset);
	return computed_offset;
error_unmap:
	scif_destroy_window(ep, window);
error:
	scif_put_peer_dev(spdev);
	dev_err(&ep->remote_dev->sdev->dev,
		"%s %d err %ld\n", __func__, __LINE__, err);
	return err;
}
EXPORT_SYMBOL_GPL(scif_register);

int
scif_unregister(scif_epd_t epd, off_t offset, size_t len)
{
	struct scif_endpt *ep = (struct scif_endpt *)epd;
	struct scif_window *window = NULL;
	struct scif_rma_req req;
	int nr_pages, err;
	struct device *spdev;

	dev_dbg(scif_info.mdev.this_device,
		"SCIFAPI unregister: ep %p offset 0x%lx len 0x%lx\n",
		ep, offset, len);
	/* len must be page aligned. len should be non zero */
	if (!len ||
	    (ALIGN((u64)len, PAGE_SIZE) != (u64)len))
		return -EINVAL;

	/* Offset is not page aligned or offset+len wraps around */
	if ((ALIGN(offset, PAGE_SIZE) != offset) ||
	    (offset < 0) ||
	    (len > LONG_MAX - offset))
		return -EINVAL;

	err = scif_verify_epd(ep);
	if (err)
		return err;

	might_sleep();
	nr_pages = len >> PAGE_SHIFT;

	req.out_window = &window;
	req.offset = offset;
	req.prot = 0;
	req.nr_bytes = len;
	req.type = SCIF_WINDOW_FULL;
	req.head = &ep->rma_info.reg_list;

	spdev = scif_get_peer_dev(ep->remote_dev);
	if (IS_ERR(spdev)) {
		err = PTR_ERR(spdev);
		return err;
	}
	mutex_lock(&ep->rma_info.rma_lock);
	/* Does a valid window exist? */
	err = scif_query_window(&req);
	if (err) {
		dev_err(&ep->remote_dev->sdev->dev,
			"%s %d err %d\n", __func__, __LINE__, err);
		goto error;
	}
	/* Unregister all the windows in this range */
	err = scif_rma_list_unregister(window, offset, nr_pages);
	if (err)
		dev_err(&ep->remote_dev->sdev->dev,
			"%s %d err %d\n", __func__, __LINE__, err);
error:
	mutex_unlock(&ep->rma_info.rma_lock);
	scif_put_peer_dev(spdev);
	return err;
}
EXPORT_SYMBOL_GPL(scif_unregister);
