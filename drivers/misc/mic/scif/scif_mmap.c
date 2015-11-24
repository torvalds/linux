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
#include "scif_main.h"

/*
 * struct scif_vma_info - Information about a remote memory mapping
 *			  created via scif_mmap(..)
 * @vma: VM area struct
 * @list: link to list of active vmas
 */
struct scif_vma_info {
	struct vm_area_struct *vma;
	struct list_head list;
};

void scif_recv_munmap(struct scif_dev *scifdev, struct scifmsg *msg)
{
	struct scif_rma_req req;
	struct scif_window *window = NULL;
	struct scif_window *recv_window =
		(struct scif_window *)msg->payload[0];
	struct scif_endpt *ep;

	ep = (struct scif_endpt *)recv_window->ep;
	req.out_window = &window;
	req.offset = recv_window->offset;
	req.prot = recv_window->prot;
	req.nr_bytes = recv_window->nr_pages << PAGE_SHIFT;
	req.type = SCIF_WINDOW_FULL;
	req.head = &ep->rma_info.reg_list;
	msg->payload[0] = ep->remote_ep;

	mutex_lock(&ep->rma_info.rma_lock);
	/* Does a valid window exist? */
	if (scif_query_window(&req)) {
		dev_err(&scifdev->sdev->dev,
			"%s %d -ENXIO\n", __func__, __LINE__);
		msg->uop = SCIF_UNREGISTER_ACK;
		goto error;
	}

	scif_put_window(window, window->nr_pages);

	if (!window->ref_count) {
		atomic_inc(&ep->rma_info.tw_refcount);
		ep->rma_info.async_list_del = 1;
		list_del_init(&window->list);
		scif_free_window_offset(ep, window, window->offset);
	}
error:
	mutex_unlock(&ep->rma_info.rma_lock);
	if (window && !window->ref_count)
		scif_queue_for_cleanup(window, &scif_info.rma);
}

/*
 * Remove valid remote memory mappings created via scif_mmap(..) from the
 * process address space since the remote node is lost
 */
static void __scif_zap_mmaps(struct scif_endpt *ep)
{
	struct list_head *item;
	struct scif_vma_info *info;
	struct vm_area_struct *vma;
	unsigned long size;

	spin_lock(&ep->lock);
	list_for_each(item, &ep->rma_info.vma_list) {
		info = list_entry(item, struct scif_vma_info, list);
		vma = info->vma;
		size = vma->vm_end - vma->vm_start;
		zap_vma_ptes(vma, vma->vm_start, size);
		dev_dbg(scif_info.mdev.this_device,
			"%s ep %p zap vma %p size 0x%lx\n",
			__func__, ep, info->vma, size);
	}
	spin_unlock(&ep->lock);
}

/*
 * Traverse the list of endpoints for a particular remote node and
 * zap valid remote memory mappings since the remote node is lost
 */
static void _scif_zap_mmaps(int node, struct list_head *head)
{
	struct scif_endpt *ep;
	struct list_head *item;

	mutex_lock(&scif_info.connlock);
	list_for_each(item, head) {
		ep = list_entry(item, struct scif_endpt, list);
		if (ep->remote_dev->node == node)
			__scif_zap_mmaps(ep);
	}
	mutex_unlock(&scif_info.connlock);
}

/*
 * Wrapper for removing remote memory mappings for a particular node. This API
 * is called by peer nodes as part of handling a lost node.
 */
void scif_zap_mmaps(int node)
{
	_scif_zap_mmaps(node, &scif_info.connected);
	_scif_zap_mmaps(node, &scif_info.disconnected);
}

/*
 * This API is only called while handling a lost node:
 * a) Remote node is dead.
 * b) Remote memory mappings have been zapped
 * So we can traverse the remote_reg_list without any locks. Since
 * the window has not yet been unregistered we can drop the ref count
 * and queue it to the cleanup thread.
 */
static void __scif_cleanup_rma_for_zombies(struct scif_endpt *ep)
{
	struct list_head *pos, *tmp;
	struct scif_window *window;

	list_for_each_safe(pos, tmp, &ep->rma_info.remote_reg_list) {
		window = list_entry(pos, struct scif_window, list);
		if (window->ref_count)
			scif_put_window(window, window->nr_pages);
		else
			dev_err(scif_info.mdev.this_device,
				"%s %d unexpected\n",
				__func__, __LINE__);
		if (!window->ref_count) {
			atomic_inc(&ep->rma_info.tw_refcount);
			list_del_init(&window->list);
			scif_queue_for_cleanup(window, &scif_info.rma);
		}
	}
}

/* Cleanup remote registration lists for zombie endpoints */
void scif_cleanup_rma_for_zombies(int node)
{
	struct scif_endpt *ep;
	struct list_head *item;

	mutex_lock(&scif_info.eplock);
	list_for_each(item, &scif_info.zombie) {
		ep = list_entry(item, struct scif_endpt, list);
		if (ep->remote_dev && ep->remote_dev->node == node)
			__scif_cleanup_rma_for_zombies(ep);
	}
	mutex_unlock(&scif_info.eplock);
	flush_work(&scif_info.misc_work);
}

/* Insert the VMA into the per endpoint VMA list */
static int scif_insert_vma(struct scif_endpt *ep, struct vm_area_struct *vma)
{
	struct scif_vma_info *info;
	int err = 0;

	info = kzalloc(sizeof(*info), GFP_KERNEL);
	if (!info) {
		err = -ENOMEM;
		goto done;
	}
	info->vma = vma;
	spin_lock(&ep->lock);
	list_add_tail(&info->list, &ep->rma_info.vma_list);
	spin_unlock(&ep->lock);
done:
	return err;
}

/* Delete the VMA from the per endpoint VMA list */
static void scif_delete_vma(struct scif_endpt *ep, struct vm_area_struct *vma)
{
	struct list_head *item;
	struct scif_vma_info *info;

	spin_lock(&ep->lock);
	list_for_each(item, &ep->rma_info.vma_list) {
		info = list_entry(item, struct scif_vma_info, list);
		if (info->vma == vma) {
			list_del(&info->list);
			kfree(info);
			break;
		}
	}
	spin_unlock(&ep->lock);
}

static phys_addr_t scif_get_phys(phys_addr_t phys, struct scif_endpt *ep)
{
	struct scif_dev *scifdev = (struct scif_dev *)ep->remote_dev;
	struct scif_hw_dev *sdev = scifdev->sdev;
	phys_addr_t out_phys, apt_base = 0;

	/*
	 * If the DMA address is card relative then we need to add the
	 * aperture base for mmap to work correctly
	 */
	if (!scifdev_self(scifdev) && sdev->aper && sdev->card_rel_da)
		apt_base = sdev->aper->pa;
	out_phys = apt_base + phys;
	return out_phys;
}

int scif_get_pages(scif_epd_t epd, off_t offset, size_t len,
		   struct scif_range **pages)
{
	struct scif_endpt *ep = (struct scif_endpt *)epd;
	struct scif_rma_req req;
	struct scif_window *window = NULL;
	int nr_pages, err, i;

	dev_dbg(scif_info.mdev.this_device,
		"SCIFAPI get_pinned_pages: ep %p offset 0x%lx len 0x%lx\n",
		ep, offset, len);
	err = scif_verify_epd(ep);
	if (err)
		return err;

	if (!len || (offset < 0) ||
	    (offset + len < offset) ||
	    (ALIGN(offset, PAGE_SIZE) != offset) ||
	    (ALIGN(len, PAGE_SIZE) != len))
		return -EINVAL;

	nr_pages = len >> PAGE_SHIFT;

	req.out_window = &window;
	req.offset = offset;
	req.prot = 0;
	req.nr_bytes = len;
	req.type = SCIF_WINDOW_SINGLE;
	req.head = &ep->rma_info.remote_reg_list;

	mutex_lock(&ep->rma_info.rma_lock);
	/* Does a valid window exist? */
	err = scif_query_window(&req);
	if (err) {
		dev_err(&ep->remote_dev->sdev->dev,
			"%s %d err %d\n", __func__, __LINE__, err);
		goto error;
	}

	/* Allocate scif_range */
	*pages = kzalloc(sizeof(**pages), GFP_KERNEL);
	if (!*pages) {
		err = -ENOMEM;
		goto error;
	}

	/* Allocate phys addr array */
	(*pages)->phys_addr = scif_zalloc(nr_pages * sizeof(dma_addr_t));
	if (!((*pages)->phys_addr)) {
		err = -ENOMEM;
		goto error;
	}

	if (scif_is_mgmt_node() && !scifdev_self(ep->remote_dev)) {
		/* Allocate virtual address array */
		((*pages)->va = scif_zalloc(nr_pages * sizeof(void *)));
		if (!(*pages)->va) {
			err = -ENOMEM;
			goto error;
		}
	}
	/* Populate the values */
	(*pages)->cookie = window;
	(*pages)->nr_pages = nr_pages;
	(*pages)->prot_flags = window->prot;

	for (i = 0; i < nr_pages; i++) {
		(*pages)->phys_addr[i] =
			__scif_off_to_dma_addr(window, offset +
					       (i * PAGE_SIZE));
		(*pages)->phys_addr[i] = scif_get_phys((*pages)->phys_addr[i],
							ep);
		if (scif_is_mgmt_node() && !scifdev_self(ep->remote_dev))
			(*pages)->va[i] =
				ep->remote_dev->sdev->aper->va +
				(*pages)->phys_addr[i] -
				ep->remote_dev->sdev->aper->pa;
	}

	scif_get_window(window, nr_pages);
error:
	mutex_unlock(&ep->rma_info.rma_lock);
	if (err) {
		if (*pages) {
			scif_free((*pages)->phys_addr,
				  nr_pages * sizeof(dma_addr_t));
			scif_free((*pages)->va,
				  nr_pages * sizeof(void *));
			kfree(*pages);
			*pages = NULL;
		}
		dev_err(&ep->remote_dev->sdev->dev,
			"%s %d err %d\n", __func__, __LINE__, err);
	}
	return err;
}
EXPORT_SYMBOL_GPL(scif_get_pages);

int scif_put_pages(struct scif_range *pages)
{
	struct scif_endpt *ep;
	struct scif_window *window;
	struct scifmsg msg;

	if (!pages || !pages->cookie)
		return -EINVAL;

	window = pages->cookie;

	if (!window || window->magic != SCIFEP_MAGIC)
		return -EINVAL;

	ep = (struct scif_endpt *)window->ep;
	/*
	 * If the state is SCIFEP_CONNECTED or SCIFEP_DISCONNECTED then the
	 * callee should be allowed to release references to the pages,
	 * else the endpoint was not connected in the first place,
	 * hence the ENOTCONN.
	 */
	if (ep->state != SCIFEP_CONNECTED && ep->state != SCIFEP_DISCONNECTED)
		return -ENOTCONN;

	mutex_lock(&ep->rma_info.rma_lock);

	scif_put_window(window, pages->nr_pages);

	/* Initiate window destruction if ref count is zero */
	if (!window->ref_count) {
		list_del(&window->list);
		mutex_unlock(&ep->rma_info.rma_lock);
		scif_drain_dma_intr(ep->remote_dev->sdev,
				    ep->rma_info.dma_chan);
		/* Inform the peer about this window being destroyed. */
		msg.uop = SCIF_MUNMAP;
		msg.src = ep->port;
		msg.payload[0] = window->peer_window;
		/* No error handling for notification messages */
		scif_nodeqp_send(ep->remote_dev, &msg);
		/* Destroy this window from the peer's registered AS */
		scif_destroy_remote_window(window);
	} else {
		mutex_unlock(&ep->rma_info.rma_lock);
	}

	scif_free(pages->phys_addr, pages->nr_pages * sizeof(dma_addr_t));
	scif_free(pages->va, pages->nr_pages * sizeof(void *));
	kfree(pages);
	return 0;
}
EXPORT_SYMBOL_GPL(scif_put_pages);

/*
 * scif_rma_list_mmap:
 *
 * Traverse the remote registration list starting from start_window:
 * 1) Create VtoP mappings via remap_pfn_range(..)
 * 2) Once step 1) and 2) complete successfully then traverse the range of
 *    windows again and bump the reference count.
 * RMA lock must be held.
 */
static int scif_rma_list_mmap(struct scif_window *start_window, s64 offset,
			      int nr_pages, struct vm_area_struct *vma)
{
	s64 end_offset, loop_offset = offset;
	struct scif_window *window = start_window;
	int loop_nr_pages, nr_pages_left = nr_pages;
	struct scif_endpt *ep = (struct scif_endpt *)start_window->ep;
	struct list_head *head = &ep->rma_info.remote_reg_list;
	int i, err = 0;
	dma_addr_t phys_addr;
	struct scif_window_iter src_win_iter;
	size_t contig_bytes = 0;

	might_sleep();
	list_for_each_entry_from(window, head, list) {
		end_offset = window->offset +
			(window->nr_pages << PAGE_SHIFT);
		loop_nr_pages = min_t(int,
				      (end_offset - loop_offset) >> PAGE_SHIFT,
				      nr_pages_left);
		scif_init_window_iter(window, &src_win_iter);
		for (i = 0; i < loop_nr_pages; i++) {
			phys_addr = scif_off_to_dma_addr(window, loop_offset,
							 &contig_bytes,
							 &src_win_iter);
			phys_addr = scif_get_phys(phys_addr, ep);
			err = remap_pfn_range(vma,
					      vma->vm_start +
					      loop_offset - offset,
					      phys_addr >> PAGE_SHIFT,
					      PAGE_SIZE,
					      vma->vm_page_prot);
			if (err)
				goto error;
			loop_offset += PAGE_SIZE;
		}
		nr_pages_left -= loop_nr_pages;
		if (!nr_pages_left)
			break;
	}
	/*
	 * No more failures expected. Bump up the ref count for all
	 * the windows. Another traversal from start_window required
	 * for handling errors encountered across windows during
	 * remap_pfn_range(..).
	 */
	loop_offset = offset;
	nr_pages_left = nr_pages;
	window = start_window;
	head = &ep->rma_info.remote_reg_list;
	list_for_each_entry_from(window, head, list) {
		end_offset = window->offset +
			(window->nr_pages << PAGE_SHIFT);
		loop_nr_pages = min_t(int,
				      (end_offset - loop_offset) >> PAGE_SHIFT,
				      nr_pages_left);
		scif_get_window(window, loop_nr_pages);
		nr_pages_left -= loop_nr_pages;
		loop_offset += (loop_nr_pages << PAGE_SHIFT);
		if (!nr_pages_left)
			break;
	}
error:
	if (err)
		dev_err(scif_info.mdev.this_device,
			"%s %d err %d\n", __func__, __LINE__, err);
	return err;
}

/*
 * scif_rma_list_munmap:
 *
 * Traverse the remote registration list starting from window:
 * 1) Decrement ref count.
 * 2) If the ref count drops to zero then send a SCIF_MUNMAP message to peer.
 * RMA lock must be held.
 */
static void scif_rma_list_munmap(struct scif_window *start_window,
				 s64 offset, int nr_pages)
{
	struct scifmsg msg;
	s64 loop_offset = offset, end_offset;
	int loop_nr_pages, nr_pages_left = nr_pages;
	struct scif_endpt *ep = (struct scif_endpt *)start_window->ep;
	struct list_head *head = &ep->rma_info.remote_reg_list;
	struct scif_window *window = start_window, *_window;

	msg.uop = SCIF_MUNMAP;
	msg.src = ep->port;
	loop_offset = offset;
	nr_pages_left = nr_pages;
	list_for_each_entry_safe_from(window, _window, head, list) {
		end_offset = window->offset +
			(window->nr_pages << PAGE_SHIFT);
		loop_nr_pages = min_t(int,
				      (end_offset - loop_offset) >> PAGE_SHIFT,
				      nr_pages_left);
		scif_put_window(window, loop_nr_pages);
		if (!window->ref_count) {
			struct scif_dev *rdev = ep->remote_dev;

			scif_drain_dma_intr(rdev->sdev,
					    ep->rma_info.dma_chan);
			/* Inform the peer about this munmap */
			msg.payload[0] = window->peer_window;
			/* No error handling for Notification messages. */
			scif_nodeqp_send(ep->remote_dev, &msg);
			list_del(&window->list);
			/* Destroy this window from the peer's registered AS */
			scif_destroy_remote_window(window);
		}
		nr_pages_left -= loop_nr_pages;
		loop_offset += (loop_nr_pages << PAGE_SHIFT);
		if (!nr_pages_left)
			break;
	}
}

/*
 * The private data field of each VMA used to mmap a remote window
 * points to an instance of struct vma_pvt
 */
struct vma_pvt {
	struct scif_endpt *ep;	/* End point for remote window */
	s64 offset;		/* offset within remote window */
	bool valid_offset;	/* offset is valid only if the original
				 * mmap request was for a single page
				 * else the offset within the vma is
				 * the correct offset
				 */
	struct kref ref;
};

static void vma_pvt_release(struct kref *ref)
{
	struct vma_pvt *vmapvt = container_of(ref, struct vma_pvt, ref);

	kfree(vmapvt);
}

/**
 * scif_vma_open - VMA open driver callback
 * @vma: VMM memory area.
 * The open method is called by the kernel to allow the subsystem implementing
 * the VMA to initialize the area. This method is invoked any time a new
 * reference to the VMA is made (when a process forks, for example).
 * The one exception happens when the VMA is first created by mmap;
 * in this case, the driver's mmap method is called instead.
 * This function is also invoked when an existing VMA is split by the kernel
 * due to a call to munmap on a subset of the VMA resulting in two VMAs.
 * The kernel invokes this function only on one of the two VMAs.
 */
static void scif_vma_open(struct vm_area_struct *vma)
{
	struct vma_pvt *vmapvt = vma->vm_private_data;

	dev_dbg(scif_info.mdev.this_device,
		"SCIFAPI vma open: vma_start 0x%lx vma_end 0x%lx\n",
		vma->vm_start, vma->vm_end);
	scif_insert_vma(vmapvt->ep, vma);
	kref_get(&vmapvt->ref);
}

/**
 * scif_munmap - VMA close driver callback.
 * @vma: VMM memory area.
 * When an area is destroyed, the kernel calls its close operation.
 * Note that there's no usage count associated with VMA's; the area
 * is opened and closed exactly once by each process that uses it.
 */
static void scif_munmap(struct vm_area_struct *vma)
{
	struct scif_endpt *ep;
	struct vma_pvt *vmapvt = vma->vm_private_data;
	int nr_pages = (vma->vm_end - vma->vm_start) >> PAGE_SHIFT;
	s64 offset;
	struct scif_rma_req req;
	struct scif_window *window = NULL;
	int err;

	might_sleep();
	dev_dbg(scif_info.mdev.this_device,
		"SCIFAPI munmap: vma_start 0x%lx vma_end 0x%lx\n",
		vma->vm_start, vma->vm_end);
	ep = vmapvt->ep;
	offset = vmapvt->valid_offset ? vmapvt->offset :
		(vma->vm_pgoff) << PAGE_SHIFT;
	dev_dbg(scif_info.mdev.this_device,
		"SCIFAPI munmap: ep %p nr_pages 0x%x offset 0x%llx\n",
		ep, nr_pages, offset);
	req.out_window = &window;
	req.offset = offset;
	req.nr_bytes = vma->vm_end - vma->vm_start;
	req.prot = vma->vm_flags & (VM_READ | VM_WRITE);
	req.type = SCIF_WINDOW_PARTIAL;
	req.head = &ep->rma_info.remote_reg_list;

	mutex_lock(&ep->rma_info.rma_lock);

	err = scif_query_window(&req);
	if (err)
		dev_err(scif_info.mdev.this_device,
			"%s %d err %d\n", __func__, __LINE__, err);
	else
		scif_rma_list_munmap(window, offset, nr_pages);

	mutex_unlock(&ep->rma_info.rma_lock);
	/*
	 * The kernel probably zeroes these out but we still want
	 * to clean up our own mess just in case.
	 */
	vma->vm_ops = NULL;
	vma->vm_private_data = NULL;
	kref_put(&vmapvt->ref, vma_pvt_release);
	scif_delete_vma(ep, vma);
}

static const struct vm_operations_struct scif_vm_ops = {
	.open = scif_vma_open,
	.close = scif_munmap,
};

/**
 * scif_mmap - Map pages in virtual address space to a remote window.
 * @vma: VMM memory area.
 * @epd: endpoint descriptor
 *
 * Return: Upon successful completion, scif_mmap() returns zero
 * else an apt error is returned as documented in scif.h
 */
int scif_mmap(struct vm_area_struct *vma, scif_epd_t epd)
{
	struct scif_rma_req req;
	struct scif_window *window = NULL;
	struct scif_endpt *ep = (struct scif_endpt *)epd;
	s64 start_offset = vma->vm_pgoff << PAGE_SHIFT;
	int nr_pages = (vma->vm_end - vma->vm_start) >> PAGE_SHIFT;
	int err;
	struct vma_pvt *vmapvt;

	dev_dbg(scif_info.mdev.this_device,
		"SCIFAPI mmap: ep %p start_offset 0x%llx nr_pages 0x%x\n",
		ep, start_offset, nr_pages);
	err = scif_verify_epd(ep);
	if (err)
		return err;

	might_sleep();

	err = scif_insert_vma(ep, vma);
	if (err)
		return err;

	vmapvt = kzalloc(sizeof(*vmapvt), GFP_KERNEL);
	if (!vmapvt) {
		scif_delete_vma(ep, vma);
		return -ENOMEM;
	}

	vmapvt->ep = ep;
	kref_init(&vmapvt->ref);

	req.out_window = &window;
	req.offset = start_offset;
	req.nr_bytes = vma->vm_end - vma->vm_start;
	req.prot = vma->vm_flags & (VM_READ | VM_WRITE);
	req.type = SCIF_WINDOW_PARTIAL;
	req.head = &ep->rma_info.remote_reg_list;

	mutex_lock(&ep->rma_info.rma_lock);
	/* Does a valid window exist? */
	err = scif_query_window(&req);
	if (err) {
		dev_err(&ep->remote_dev->sdev->dev,
			"%s %d err %d\n", __func__, __LINE__, err);
		goto error_unlock;
	}

	/* Default prot for loopback */
	if (!scifdev_self(ep->remote_dev))
		vma->vm_page_prot = pgprot_writecombine(vma->vm_page_prot);

	/*
	 * VM_DONTCOPY - Do not copy this vma on fork
	 * VM_DONTEXPAND - Cannot expand with mremap()
	 * VM_RESERVED - Count as reserved_vm like IO
	 * VM_PFNMAP - Page-ranges managed without "struct page"
	 * VM_IO - Memory mapped I/O or similar
	 *
	 * We do not want to copy this VMA automatically on a fork(),
	 * expand this VMA due to mremap() or swap out these pages since
	 * the VMA is actually backed by physical pages in the remote
	 * node's physical memory and not via a struct page.
	 */
	vma->vm_flags |= VM_DONTCOPY | VM_DONTEXPAND | VM_DONTDUMP;

	if (!scifdev_self(ep->remote_dev))
		vma->vm_flags |= VM_IO | VM_PFNMAP;

	/* Map this range of windows */
	err = scif_rma_list_mmap(window, start_offset, nr_pages, vma);
	if (err) {
		dev_err(&ep->remote_dev->sdev->dev,
			"%s %d err %d\n", __func__, __LINE__, err);
		goto error_unlock;
	}
	/* Set up the driver call back */
	vma->vm_ops = &scif_vm_ops;
	vma->vm_private_data = vmapvt;
error_unlock:
	mutex_unlock(&ep->rma_info.rma_lock);
	if (err) {
		kfree(vmapvt);
		dev_err(&ep->remote_dev->sdev->dev,
			"%s %d err %d\n", __func__, __LINE__, err);
		scif_delete_vma(ep, vma);
	}
	return err;
}
