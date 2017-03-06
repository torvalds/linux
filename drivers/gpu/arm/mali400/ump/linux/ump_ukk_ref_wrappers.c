/*
 * Copyright (C) 2010, 2013-2014, 2016-2017 ARM Limited. All rights reserved.
 * 
 * This program is free software and is provided to you under the terms of the GNU General Public License version 2
 * as published by the Free Software Foundation, and any use by you of this program is subject to the terms of such GNU licence.
 * 
 * A copy of the licence is included with the program, and can also be obtained from Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 */

/**
 * @file ump_ukk_wrappers.c
 * Defines the wrapper functions which turn Linux IOCTL calls into _ukk_ calls for the reference implementation
 */


#include <asm/uaccess.h>             /* user space access */

#include "ump_osk.h"
#include "ump_uk_types.h"
#include "ump_ukk.h"
#include "ump_kernel_common.h"
#include <linux/scatterlist.h>
#include "ump_kernel_interface_ref_drv.h"
#include "mali_osk_list.h"

extern struct device *ump_global_mdev;

/*
 * IOCTL operation; Allocate UMP memory
 */
int ump_allocate_wrapper(u32 __user *argument, struct ump_session_data   *session_data)
{
	_ump_uk_allocate_s user_interaction;
	_mali_osk_errcode_t err;

	/* Sanity check input parameters */
	if (NULL == argument || NULL == session_data) {
		MSG_ERR(("NULL parameter in ump_ioctl_allocate()\n"));
		return -ENOTTY;
	}

	/* Copy the user space memory to kernel space (so we safely can read it) */
	if (0 != copy_from_user(&user_interaction, argument, sizeof(user_interaction))) {
		MSG_ERR(("copy_from_user() in ump_ioctl_allocate()\n"));
		return -EFAULT;
	}

	user_interaction.ctx = (void *) session_data;

	err = _ump_ukk_allocate(&user_interaction);
	if (_MALI_OSK_ERR_OK != err) {
		DBG_MSG(1, ("_ump_ukk_allocate() failed in ump_ioctl_allocate()\n"));
		return ump_map_errcode(err);
	}
	user_interaction.ctx = NULL;

	if (0 != copy_to_user(argument, &user_interaction, sizeof(user_interaction))) {
		/* If the copy fails then we should release the memory. We can use the IOCTL release to accomplish this */
		_ump_uk_release_s release_args;

		MSG_ERR(("copy_to_user() failed in ump_ioctl_allocate()\n"));

		release_args.ctx = (void *) session_data;
		release_args.secure_id = user_interaction.secure_id;

		err = _ump_ukk_release(&release_args);
		if (_MALI_OSK_ERR_OK != err) {
			MSG_ERR(("_ump_ukk_release() also failed when trying to release newly allocated memory in ump_ioctl_allocate()\n"));
		}

		return -EFAULT;
	}

	return 0; /* success */
}

#ifdef CONFIG_DMA_SHARED_BUFFER
static ump_dd_handle get_ump_handle_from_dmabuf(struct ump_session_data *session_data,
		struct dma_buf *dmabuf)
{
	ump_session_memory_list_element *session_mem, *tmp;
	struct dma_buf_attachment *attach;
	ump_dd_handle ump_handle;

	DEBUG_ASSERT_POINTER(session_data);

	_mali_osk_mutex_wait(session_data->lock);

	_MALI_OSK_LIST_FOREACHENTRY(session_mem, tmp,
				    &session_data->list_head_session_memory_list,
				    ump_session_memory_list_element, list) {
		if (session_mem->mem->import_attach) {
			attach = session_mem->mem->import_attach;
			if (attach->dmabuf == dmabuf) {
				_mali_osk_mutex_signal(session_data->lock);
				ump_handle = (ump_dd_handle)session_mem->mem;
				ump_random_mapping_get(device.secure_id_map, ump_dd_secure_id_get(ump_handle));
				return ump_handle;
			}
		}
	}

	_mali_osk_mutex_signal(session_data->lock);

	return NULL;
}

int ump_dmabuf_import_wrapper(u32 __user *argument,
			      struct ump_session_data  *session_data)
{
	ump_session_memory_list_element *session = NULL;
	_ump_uk_dmabuf_s ump_dmabuf;
	ump_dd_handle ump_handle;
	ump_dd_physical_block *blocks = NULL;
	struct dma_buf_attachment *attach = NULL;
	struct dma_buf *dma_buf;
	struct sg_table *sgt = NULL;
	struct scatterlist *sgl;
	unsigned int i = 0;
	int ret = 0;

	/* Sanity check input parameters */
	if (!argument || !session_data) {
		MSG_ERR(("NULL parameter.\n"));
		return -EINVAL;
	}

	if (copy_from_user(&ump_dmabuf, argument,
			   sizeof(_ump_uk_dmabuf_s))) {
		MSG_ERR(("copy_from_user() failed.\n"));
		return -EFAULT;
	}

	dma_buf = dma_buf_get(ump_dmabuf.fd);
	if (IS_ERR(dma_buf))
		return PTR_ERR(dma_buf);

	/*
	 * if already imported then increase a refcount to the ump descriptor
	 * and call dma_buf_put() and then go to found to return previous
	 * ump secure id.
	 */
	ump_handle = get_ump_handle_from_dmabuf(session_data, dma_buf);
	if (ump_handle) {
		dma_buf_put(dma_buf);
		goto found;
	}

	attach = dma_buf_attach(dma_buf, ump_global_mdev);
	if (IS_ERR(attach)) {
		ret = PTR_ERR(attach);
		goto err_dma_buf_put;
	}

	sgt = dma_buf_map_attachment(attach, DMA_BIDIRECTIONAL);
	if (IS_ERR(sgt)) {
		ret = PTR_ERR(sgt);
		goto err_dma_buf_detach;
	}

	blocks = (ump_dd_physical_block *)_mali_osk_malloc(sizeof(ump_dd_physical_block) * sgt->nents);
	if (!blocks) {
		DBG_MSG(1, ("Failed to allocate blocks.\n"));
		ret = -EFAULT;
		goto err_dma_buf_unmap;
	}
	for_each_sg(sgt->sgl, sgl, sgt->nents, i) {
		blocks[i].addr = sg_phys(sgl);
		blocks[i].size = sg_dma_len(sgl);
	}

	/*
	 * Initialize the session memory list element, and add it
	 * to the session object
	 */
	session = _mali_osk_calloc(1, sizeof(*session));
	if (!session) {
		DBG_MSG(1, ("Failed to allocate session.\n"));
		ret = -EFAULT;
		goto err_free_block;
	}

	ump_handle = ump_dd_handle_create_from_phys_blocks(blocks, i);
	if (UMP_DD_HANDLE_INVALID == ump_handle) {
		DBG_MSG(1, ("Failed to create ump handle.\n"));
		ret = -EFAULT;
		goto err_free_session;
	}

	session->mem = (ump_dd_mem *)ump_handle;
	session->mem->import_attach = attach;
	session->mem->sgt = sgt;

	_mali_osk_mutex_wait(session_data->lock);
	_mali_osk_list_add(&(session->list),
			   &(session_data->list_head_session_memory_list));
	_mali_osk_mutex_signal(session_data->lock);

	_mali_osk_free(blocks);

found:
	ump_dmabuf.ctx = (void *)session_data;
	ump_dmabuf.secure_id = ump_dd_secure_id_get(ump_handle);
	ump_dmabuf.size = ump_dd_size_get(ump_handle);

	if (copy_to_user(argument, &ump_dmabuf,
			 sizeof(_ump_uk_dmabuf_s))) {
		MSG_ERR(("copy_to_user() failed.\n"));
		ret =  -EFAULT;
		goto err_release_ump_handle;
	}

	return ret;

err_release_ump_handle:
	ump_dd_reference_release(ump_handle);
err_free_session:
	_mali_osk_free(session);
err_free_block:
	_mali_osk_free(blocks);
err_dma_buf_unmap:
	dma_buf_unmap_attachment(attach, sgt, DMA_BIDIRECTIONAL);
err_dma_buf_detach:
	dma_buf_detach(dma_buf, attach);
err_dma_buf_put:
	dma_buf_put(dma_buf);
	return ret;
}
#endif
