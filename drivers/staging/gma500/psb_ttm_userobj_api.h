/**************************************************************************
 *
 * Copyright (c) 2006-2008 Tungsten Graphics, Inc., Cedar Park, TX., USA
 * All Rights Reserved.
 * Copyright (c) 2009 VMware, Inc., Palo Alto, CA., USA
 * All Rights Reserved.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin St - Fifth Floor, Boston, MA 02110-1301 USA.
 *
 **************************************************************************/
/*
 * Authors: Thomas Hellstrom <thomas-at-tungstengraphics-dot-com>
 */

#ifndef _TTM_USEROBJ_API_H_
#define _TTM_USEROBJ_API_H_

#include "psb_ttm_placement_user.h"
#include "psb_ttm_fence_user.h"
#include "ttm/ttm_object.h"
#include "psb_ttm_fence_api.h"
#include "ttm/ttm_bo_api.h"

struct ttm_lock;

/*
 * User ioctls.
 */

extern int ttm_pl_create_ioctl(struct ttm_object_file *tfile,
			       struct ttm_bo_device *bdev,
			       struct ttm_lock *lock, void *data);
extern int ttm_pl_ub_create_ioctl(struct ttm_object_file *tfile,
				  struct ttm_bo_device *bdev,
				  struct ttm_lock *lock, void *data);
extern int ttm_pl_reference_ioctl(struct ttm_object_file *tfile, void *data);
extern int ttm_pl_unref_ioctl(struct ttm_object_file *tfile, void *data);
extern int ttm_pl_synccpu_ioctl(struct ttm_object_file *tfile, void *data);
extern int ttm_pl_setstatus_ioctl(struct ttm_object_file *tfile,
				  struct ttm_lock *lock, void *data);
extern int ttm_pl_waitidle_ioctl(struct ttm_object_file *tfile, void *data);
extern int ttm_fence_signaled_ioctl(struct ttm_object_file *tfile, void *data);
extern int ttm_fence_finish_ioctl(struct ttm_object_file *tfile, void *data);
extern int ttm_fence_unref_ioctl(struct ttm_object_file *tfile, void *data);

extern int
ttm_fence_user_create(struct ttm_fence_device *fdev,
		      struct ttm_object_file *tfile,
		      uint32_t fence_class,
		      uint32_t fence_types,
		      uint32_t create_flags,
		      struct ttm_fence_object **fence, uint32_t * user_handle);

extern struct ttm_buffer_object *ttm_buffer_object_lookup(struct ttm_object_file
							  *tfile,
							  uint32_t handle);

extern int
ttm_pl_verify_access(struct ttm_buffer_object *bo,
		     struct ttm_object_file *tfile);

extern int ttm_buffer_object_create(struct ttm_bo_device *bdev,
			unsigned long size,
			enum ttm_bo_type type,
			uint32_t flags,
			uint32_t page_alignment,
			unsigned long buffer_start,
			bool interruptible,
			struct file *persistant_swap_storage,
			struct ttm_buffer_object **p_bo);

extern int psb_ttm_bo_check_placement(struct ttm_buffer_object *bo,
				struct ttm_placement *placement);
#endif
