// SPDX-License-Identifier: GPL-2.0

/*
 * Copyright 2016-2019 HabanaLabs, Ltd.
 * All Rights Reserved.
 */

#include "habanalabs.h"

#include <linux/uaccess.h>
#include <linux/slab.h>

/*
 * hl_pin_host_memory - pins a chunk of host memory
 *
 * @hdev                : pointer to the habanalabs device structure
 * @addr                : the user-space virtual address of the memory area
 * @size                : the size of the memory area
 * @userptr	        : pointer to hl_userptr structure
 *
 * This function does the following:
 * - Pins the physical pages
 * - Create a SG list from those pages
 */
int hl_pin_host_memory(struct hl_device *hdev, u64 addr, u32 size,
			struct hl_userptr *userptr)
{
	u64 start, end;
	u32 npages, offset;
	int rc;

	if (!size) {
		dev_err(hdev->dev, "size to pin is invalid - %d\n",
			size);
		return -EINVAL;
	}

	if (!access_ok((void __user *) (uintptr_t) addr, size)) {
		dev_err(hdev->dev, "user pointer is invalid - 0x%llx\n",
			addr);
		return -EFAULT;
	}

	/*
	 * If the combination of the address and size requested for this memory
	 * region causes an integer overflow, return error.
	 */
	if (((addr + size) < addr) ||
			PAGE_ALIGN(addr + size) < (addr + size)) {
		dev_err(hdev->dev,
			"user pointer 0x%llx + %u causes integer overflow\n",
			addr, size);
		return -EINVAL;
	}

	start = addr & PAGE_MASK;
	offset = addr & ~PAGE_MASK;
	end = PAGE_ALIGN(addr + size);
	npages = (end - start) >> PAGE_SHIFT;

	userptr->size = size;
	userptr->addr = addr;
	userptr->dma_mapped = false;
	INIT_LIST_HEAD(&userptr->job_node);

	userptr->vec = frame_vector_create(npages);
	if (!userptr->vec) {
		dev_err(hdev->dev, "Failed to create frame vector\n");
		return -ENOMEM;
	}

	rc = get_vaddr_frames(start, npages, FOLL_FORCE | FOLL_WRITE,
				userptr->vec);

	if (rc != npages) {
		dev_err(hdev->dev,
			"Failed to map host memory, user ptr probably wrong\n");
		if (rc < 0)
			goto destroy_framevec;
		rc = -EFAULT;
		goto put_framevec;
	}

	if (frame_vector_to_pages(userptr->vec) < 0) {
		dev_err(hdev->dev,
			"Failed to translate frame vector to pages\n");
		rc = -EFAULT;
		goto put_framevec;
	}

	userptr->sgt = kzalloc(sizeof(*userptr->sgt), GFP_ATOMIC);
	if (!userptr->sgt) {
		rc = -ENOMEM;
		goto put_framevec;
	}

	rc = sg_alloc_table_from_pages(userptr->sgt,
					frame_vector_pages(userptr->vec),
					npages, offset, size, GFP_ATOMIC);
	if (rc < 0) {
		dev_err(hdev->dev, "failed to create SG table from pages\n");
		goto free_sgt;
	}

	return 0;

free_sgt:
	kfree(userptr->sgt);
put_framevec:
	put_vaddr_frames(userptr->vec);
destroy_framevec:
	frame_vector_destroy(userptr->vec);
	return rc;
}

/*
 * hl_unpin_host_memory - unpins a chunk of host memory
 *
 * @hdev                : pointer to the habanalabs device structure
 * @userptr             : pointer to hl_userptr structure
 *
 * This function does the following:
 * - Unpins the physical pages related to the host memory
 * - Free the SG list
 */
int hl_unpin_host_memory(struct hl_device *hdev, struct hl_userptr *userptr)
{
	struct page **pages;

	if (userptr->dma_mapped)
		hdev->asic_funcs->hl_dma_unmap_sg(hdev,
				userptr->sgt->sgl,
				userptr->sgt->nents,
				userptr->dir);

	pages = frame_vector_pages(userptr->vec);
	if (!IS_ERR(pages)) {
		int i;

		for (i = 0; i < frame_vector_count(userptr->vec); i++)
			set_page_dirty_lock(pages[i]);
	}
	put_vaddr_frames(userptr->vec);
	frame_vector_destroy(userptr->vec);

	list_del(&userptr->job_node);

	sg_free_table(userptr->sgt);
	kfree(userptr->sgt);

	return 0;
}

/*
 * hl_userptr_delete_list - clear userptr list
 *
 * @hdev                : pointer to the habanalabs device structure
 * @userptr_list        : pointer to the list to clear
 *
 * This function does the following:
 * - Iterates over the list and unpins the host memory and frees the userptr
 *   structure.
 */
void hl_userptr_delete_list(struct hl_device *hdev,
				struct list_head *userptr_list)
{
	struct hl_userptr *userptr, *tmp;

	list_for_each_entry_safe(userptr, tmp, userptr_list, job_node) {
		hl_unpin_host_memory(hdev, userptr);
		kfree(userptr);
	}

	INIT_LIST_HEAD(userptr_list);
}

/*
 * hl_userptr_is_pinned - returns whether the given userptr is pinned
 *
 * @hdev                : pointer to the habanalabs device structure
 * @userptr_list        : pointer to the list to clear
 * @userptr             : pointer to userptr to check
 *
 * This function does the following:
 * - Iterates over the list and checks if the given userptr is in it, means is
 *   pinned. If so, returns true, otherwise returns false.
 */
bool hl_userptr_is_pinned(struct hl_device *hdev, u64 addr,
				u32 size, struct list_head *userptr_list,
				struct hl_userptr **userptr)
{
	list_for_each_entry((*userptr), userptr_list, job_node) {
		if ((addr == (*userptr)->addr) && (size == (*userptr)->size))
			return true;
	}

	return false;
}
