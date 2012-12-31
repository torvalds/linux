/*
 * Copyright (C) 2010 ARM Limited. All rights reserved.
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

#ifdef CONFIG_ION_EXYNOS
#include <linux/ion.h>
#include <linux/scatterlist.h>
#include "../../../../../gpu/ion/ion_priv.h"
#include "ump_kernel_interface_ref_drv.h"
#include "mali_osk_list.h"
extern struct ion_device *ion_exynos;
extern struct ion_client *ion_client_ump;
#endif

/*
 * IOCTL operation; Allocate UMP memory
 */
int ump_allocate_wrapper(u32 __user * argument, struct ump_session_data  * session_data)
{
	_ump_uk_allocate_s user_interaction;
	_mali_osk_errcode_t err;

	/* Sanity check input parameters */
	if (NULL == argument || NULL == session_data)
	{
		MSG_ERR(("NULL parameter in ump_ioctl_allocate()\n"));
		return -ENOTTY;
	}

	/* Copy the user space memory to kernel space (so we safely can read it) */
	if (0 != copy_from_user(&user_interaction, argument, sizeof(user_interaction)))
	{
		MSG_ERR(("copy_from_user() in ump_ioctl_allocate()\n"));
		return -EFAULT;
	}

	user_interaction.ctx = (void *) session_data;

	err = _ump_ukk_allocate( &user_interaction );
	if( _MALI_OSK_ERR_OK != err )
	{
		DBG_MSG(1, ("_ump_ukk_allocate() failed in ump_ioctl_allocate()\n"));
		return map_errcode(err);
	}
	user_interaction.ctx = NULL;

	if (0 != copy_to_user(argument, &user_interaction, sizeof(user_interaction)))
	{
		/* If the copy fails then we should release the memory. We can use the IOCTL release to accomplish this */
		_ump_uk_release_s release_args;

		MSG_ERR(("copy_to_user() failed in ump_ioctl_allocate()\n"));

		release_args.ctx = (void *) session_data;
		release_args.secure_id = user_interaction.secure_id;

		err = _ump_ukk_release( &release_args );
		if(_MALI_OSK_ERR_OK != err)
		{
			MSG_ERR(("_ump_ukk_release() also failed when trying to release newly allocated memory in ump_ioctl_allocate()\n"));
		}

		return -EFAULT;
	}

	return 0; /* success */
}

#ifdef CONFIG_ION_EXYNOS
/*
 * IOCTL operation; Import fd to  UMP memory
 */
int ump_ion_import_wrapper(u32 __user * argument, struct ump_session_data  * session_data)
{
	_ump_uk_ion_import_s user_interaction;
	ump_dd_handle *ump_handle;
	ump_dd_physical_block * blocks;
	unsigned long num_blocks;
	struct ion_handle *ion_hnd;
	struct scatterlist *sg;
	struct scatterlist *sg_ion;
	unsigned long i = 0;

	ump_session_memory_list_element * session_memory_element = NULL;
	if (ion_client_ump==NULL)
	    ion_client_ump = ion_client_create(ion_exynos, -1, "ump");

	/* Sanity check input parameters */
	if (NULL == argument || NULL == session_data)
	{
		MSG_ERR(("NULL parameter in ump_ioctl_allocate()\n"));
		return -ENOTTY;
	}

	/* Copy the user space memory to kernel space (so we safely can read it) */
	if (0 != copy_from_user(&user_interaction, argument, sizeof(user_interaction)))
	{
		MSG_ERR(("copy_from_user() in ump_ioctl_allocate()\n"));
		return -EFAULT;
	}

	user_interaction.ctx = (void *) session_data;

	/* translate fd to secure ID*/
	ion_hnd = ion_import_fd(ion_client_ump, user_interaction.ion_fd);
	sg_ion = ion_map_dma(ion_client_ump,ion_hnd);

	blocks = (ump_dd_physical_block*)_mali_osk_malloc(sizeof(ump_dd_physical_block)*1024);
	sg = sg_ion;
	do {
		blocks[i].addr = sg_phys(sg);
		blocks[i].size = sg_dma_len(sg);
		i++;
		if (i>=1024) {
			_mali_osk_free(blocks);
			MSG_ERR(("ion_import fail() in ump_ioctl_allocate()\n"));
			return -EFAULT;
		}
		sg = sg_next(sg);
	} while(sg);

	num_blocks = i;

	/* Initialize the session_memory_element, and add it to the session object */
	session_memory_element = _mali_osk_calloc( 1, sizeof(ump_session_memory_list_element));

	if (NULL == session_memory_element)
	{
		_mali_osk_free(blocks);
		DBG_MSG(1, ("Failed to allocate ump_session_memory_list_element in ump_ioctl_allocate()\n"));
		return -EFAULT;
	}

	ump_handle = ump_dd_handle_create_from_phys_blocks(blocks, num_blocks);
	if (UMP_DD_HANDLE_INVALID == ump_handle)
	{
		_mali_osk_free(session_memory_element);
		_mali_osk_free(blocks);
		DBG_MSG(1, ("Failed to allocate ump_session_memory_list_element in ump_ioctl_allocate()\n"));
		return -EFAULT;
	}

	session_memory_element->mem = (ump_dd_mem*)ump_handle;
	_mali_osk_lock_wait(session_data->lock, _MALI_OSK_LOCKMODE_RW);
	_mali_osk_list_add(&(session_memory_element->list), &(session_data->list_head_session_memory_list));
	_mali_osk_lock_signal(session_data->lock, _MALI_OSK_LOCKMODE_RW);
	ion_unmap_dma(ion_client_ump,ion_hnd);
	ion_free(ion_client_ump, ion_hnd);

	_mali_osk_free(blocks);

	user_interaction.secure_id = ump_dd_secure_id_get(ump_handle);
	user_interaction.size = ump_dd_size_get(ump_handle);
	user_interaction.ctx = NULL;

	if (0 != copy_to_user(argument, &user_interaction, sizeof(user_interaction)))
	{
		/* If the copy fails then we should release the memory. We can use the IOCTL release to accomplish this */

		MSG_ERR(("copy_to_user() failed in ump_ioctl_allocate()\n"));

		return -EFAULT;
	}
	return 0; /* success */
}
#endif
