/**************************************************************************
 *
 * Copyright © 2011-2012 VMware, Inc., Palo Alto, CA., USA
 * All Rights Reserved.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the
 * "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish,
 * distribute, sub license, and/or sell copies of the Software, and to
 * permit persons to whom the Software is furnished to do so, subject to
 * the following conditions:
 *
 * The above copyright notice and this permission notice (including the
 * next paragraph) shall be included in all copies or substantial portions
 * of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NON-INFRINGEMENT. IN NO EVENT SHALL
 * THE COPYRIGHT HOLDERS, AUTHORS AND/OR ITS SUPPLIERS BE LIABLE FOR ANY CLAIM,
 * DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR
 * OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE
 * USE OR OTHER DEALINGS IN THE SOFTWARE.
 *
 **************************************************************************/

#ifndef _VMWGFX_FENCE_H_

#include <linux/fence.h>

#define VMW_FENCE_WAIT_TIMEOUT (5*HZ)

struct vmw_private;

struct vmw_fence_manager;

/**
 *
 *
 */
enum vmw_action_type {
	VMW_ACTION_EVENT = 0,
	VMW_ACTION_MAX
};

struct vmw_fence_action {
	struct list_head head;
	enum vmw_action_type type;
	void (*seq_passed) (struct vmw_fence_action *action);
	void (*cleanup) (struct vmw_fence_action *action);
};

struct vmw_fence_obj {
	struct fence base;

	struct list_head head;
	struct list_head seq_passed_actions;
	void (*destroy)(struct vmw_fence_obj *fence);
};

extern struct vmw_fence_manager *
vmw_fence_manager_init(struct vmw_private *dev_priv);

extern void vmw_fence_manager_takedown(struct vmw_fence_manager *fman);

static inline void
vmw_fence_obj_unreference(struct vmw_fence_obj **fence_p)
{
	struct vmw_fence_obj *fence = *fence_p;

	*fence_p = NULL;
	if (fence)
		fence_put(&fence->base);
}

static inline struct vmw_fence_obj *
vmw_fence_obj_reference(struct vmw_fence_obj *fence)
{
	if (fence)
		fence_get(&fence->base);
	return fence;
}

extern void vmw_fences_update(struct vmw_fence_manager *fman);

extern bool vmw_fence_obj_signaled(struct vmw_fence_obj *fence);

extern int vmw_fence_obj_wait(struct vmw_fence_obj *fence,
			      bool lazy,
			      bool interruptible, unsigned long timeout);

extern void vmw_fence_obj_flush(struct vmw_fence_obj *fence);

extern int vmw_fence_create(struct vmw_fence_manager *fman,
			    uint32_t seqno,
			    struct vmw_fence_obj **p_fence);

extern int vmw_user_fence_create(struct drm_file *file_priv,
				 struct vmw_fence_manager *fman,
				 uint32_t sequence,
				 struct vmw_fence_obj **p_fence,
				 uint32_t *p_handle);

extern void vmw_fence_fifo_up(struct vmw_fence_manager *fman);

extern void vmw_fence_fifo_down(struct vmw_fence_manager *fman);

extern int vmw_fence_obj_wait_ioctl(struct drm_device *dev, void *data,
				    struct drm_file *file_priv);

extern int vmw_fence_obj_signaled_ioctl(struct drm_device *dev, void *data,
					struct drm_file *file_priv);

extern int vmw_fence_obj_unref_ioctl(struct drm_device *dev, void *data,
				     struct drm_file *file_priv);
extern int vmw_fence_event_ioctl(struct drm_device *dev, void *data,
				 struct drm_file *file_priv);
extern int vmw_event_fence_action_queue(struct drm_file *filee_priv,
					struct vmw_fence_obj *fence,
					struct drm_pending_event *event,
					uint32_t *tv_sec,
					uint32_t *tv_usec,
					bool interruptible);
#endif /* _VMWGFX_FENCE_H_ */
