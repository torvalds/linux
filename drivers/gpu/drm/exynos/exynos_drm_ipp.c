/*
 * Copyright (C) 2017 Samsung Electronics Co.Ltd
 * Authors:
 *	Marek Szyprowski <m.szyprowski@samsung.com>
 *
 * Exynos DRM Image Post Processing (IPP) related functions
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 */


#include <drm/drmP.h>
#include <drm/drm_mode.h>
#include <uapi/drm/exynos_drm.h>

#include "exynos_drm_drv.h"
#include "exynos_drm_gem.h"
#include "exynos_drm_ipp.h"

static int num_ipp;
static LIST_HEAD(ipp_list);

/**
 * exynos_drm_ipp_register - Register a new picture processor hardware module
 * @dev: DRM device
 * @ipp: ipp module to init
 * @funcs: callbacks for the new ipp object
 * @caps: bitmask of ipp capabilities (%DRM_EXYNOS_IPP_CAP_*)
 * @formats: array of supported formats
 * @num_formats: size of the supported formats array
 * @name: name (for debugging purposes)
 *
 * Initializes a ipp module.
 *
 * Returns:
 * Zero on success, error code on failure.
 */
int exynos_drm_ipp_register(struct drm_device *dev, struct exynos_drm_ipp *ipp,
		const struct exynos_drm_ipp_funcs *funcs, unsigned int caps,
		const struct exynos_drm_ipp_formats *formats,
		unsigned int num_formats, const char *name)
{
	WARN_ON(!ipp);
	WARN_ON(!funcs);
	WARN_ON(!formats);
	WARN_ON(!num_formats);

	spin_lock_init(&ipp->lock);
	INIT_LIST_HEAD(&ipp->todo_list);
	init_waitqueue_head(&ipp->done_wq);
	ipp->dev = dev;
	ipp->funcs = funcs;
	ipp->capabilities = caps;
	ipp->name = name;
	ipp->formats = formats;
	ipp->num_formats = num_formats;

	/* ipp_list modification is serialized by component framework */
	list_add_tail(&ipp->head, &ipp_list);
	ipp->id = num_ipp++;

	DRM_DEBUG_DRIVER("Registered ipp %d\n", ipp->id);

	return 0;
}

/**
 * exynos_drm_ipp_unregister - Unregister the picture processor module
 * @dev: DRM device
 * @ipp: ipp module
 */
void exynos_drm_ipp_unregister(struct drm_device *dev,
			       struct exynos_drm_ipp *ipp)
{
	WARN_ON(ipp->task);
	WARN_ON(!list_empty(&ipp->todo_list));
	list_del(&ipp->head);
}

/**
 * exynos_drm_ipp_ioctl_get_res_ioctl - enumerate all ipp modules
 * @dev: DRM device
 * @data: ioctl data
 * @file_priv: DRM file info
 *
 * Construct a list of ipp ids.
 *
 * Called by the user via ioctl.
 *
 * Returns:
 * Zero on success, negative errno on failure.
 */
int exynos_drm_ipp_get_res_ioctl(struct drm_device *dev, void *data,
				 struct drm_file *file_priv)
{
	struct drm_exynos_ioctl_ipp_get_res *resp = data;
	struct exynos_drm_ipp *ipp;
	uint32_t __user *ipp_ptr = (uint32_t __user *)
						(unsigned long)resp->ipp_id_ptr;
	unsigned int count = num_ipp, copied = 0;

	/*
	 * This ioctl is called twice, once to determine how much space is
	 * needed, and the 2nd time to fill it.
	 */
	if (count && resp->count_ipps >= count) {
		list_for_each_entry(ipp, &ipp_list, head) {
			if (put_user(ipp->id, ipp_ptr + copied))
				return -EFAULT;
			copied++;
		}
	}
	resp->count_ipps = count;

	return 0;
}

static inline struct exynos_drm_ipp *__ipp_get(uint32_t id)
{
	struct exynos_drm_ipp *ipp;

	list_for_each_entry(ipp, &ipp_list, head)
		if (ipp->id == id)
			return ipp;
	return NULL;
}

/**
 * exynos_drm_ipp_ioctl_get_caps - get ipp module capabilities and formats
 * @dev: DRM device
 * @data: ioctl data
 * @file_priv: DRM file info
 *
 * Construct a structure describing ipp module capabilities.
 *
 * Called by the user via ioctl.
 *
 * Returns:
 * Zero on success, negative errno on failure.
 */
int exynos_drm_ipp_get_caps_ioctl(struct drm_device *dev, void *data,
				  struct drm_file *file_priv)
{
	struct drm_exynos_ioctl_ipp_get_caps *resp = data;
	void __user *ptr = (void __user *)(unsigned long)resp->formats_ptr;
	struct exynos_drm_ipp *ipp;
	int i;

	ipp = __ipp_get(resp->ipp_id);
	if (!ipp)
		return -ENOENT;

	resp->ipp_id = ipp->id;
	resp->capabilities = ipp->capabilities;

	/*
	 * This ioctl is called twice, once to determine how much space is
	 * needed, and the 2nd time to fill it.
	 */
	if (resp->formats_count >= ipp->num_formats) {
		for (i = 0; i < ipp->num_formats; i++) {
			struct drm_exynos_ipp_format tmp = {
				.fourcc = ipp->formats[i].fourcc,
				.type = ipp->formats[i].type,
				.modifier = ipp->formats[i].modifier,
			};

			if (copy_to_user(ptr, &tmp, sizeof(tmp)))
				return -EFAULT;
			ptr += sizeof(tmp);
		}
	}
	resp->formats_count = ipp->num_formats;

	return 0;
}

static inline const struct exynos_drm_ipp_formats *__ipp_format_get(
				struct exynos_drm_ipp *ipp, uint32_t fourcc,
				uint64_t mod, unsigned int type)
{
	int i;

	for (i = 0; i < ipp->num_formats; i++) {
		if ((ipp->formats[i].type & type) &&
		    ipp->formats[i].fourcc == fourcc &&
		    ipp->formats[i].modifier == mod)
			return &ipp->formats[i];
	}
	return NULL;
}

/**
 * exynos_drm_ipp_get_limits_ioctl - get ipp module limits
 * @dev: DRM device
 * @data: ioctl data
 * @file_priv: DRM file info
 *
 * Construct a structure describing ipp module limitations for provided
 * picture format.
 *
 * Called by the user via ioctl.
 *
 * Returns:
 * Zero on success, negative errno on failure.
 */
int exynos_drm_ipp_get_limits_ioctl(struct drm_device *dev, void *data,
				    struct drm_file *file_priv)
{
	struct drm_exynos_ioctl_ipp_get_limits *resp = data;
	void __user *ptr = (void __user *)(unsigned long)resp->limits_ptr;
	const struct exynos_drm_ipp_formats *format;
	struct exynos_drm_ipp *ipp;

	if (resp->type != DRM_EXYNOS_IPP_FORMAT_SOURCE &&
	    resp->type != DRM_EXYNOS_IPP_FORMAT_DESTINATION)
		return -EINVAL;

	ipp = __ipp_get(resp->ipp_id);
	if (!ipp)
		return -ENOENT;

	format = __ipp_format_get(ipp, resp->fourcc, resp->modifier,
				  resp->type);
	if (!format)
		return -EINVAL;

	/*
	 * This ioctl is called twice, once to determine how much space is
	 * needed, and the 2nd time to fill it.
	 */
	if (format->num_limits && resp->limits_count >= format->num_limits)
		if (copy_to_user((void __user *)ptr, format->limits,
				 sizeof(*format->limits) * format->num_limits))
			return -EFAULT;
	resp->limits_count = format->num_limits;

	return 0;
}

struct drm_pending_exynos_ipp_event {
	struct drm_pending_event base;
	struct drm_exynos_ipp_event event;
};

static inline struct exynos_drm_ipp_task *
			exynos_drm_ipp_task_alloc(struct exynos_drm_ipp *ipp)
{
	struct exynos_drm_ipp_task *task;

	task = kzalloc(sizeof(*task), GFP_KERNEL);
	if (!task)
		return NULL;

	task->dev = ipp->dev;
	task->ipp = ipp;

	/* some defaults */
	task->src.rect.w = task->dst.rect.w = UINT_MAX;
	task->src.rect.h = task->dst.rect.h = UINT_MAX;
	task->transform.rotation = DRM_MODE_ROTATE_0;

	DRM_DEBUG_DRIVER("Allocated task %pK\n", task);

	return task;
}

static const struct exynos_drm_param_map {
	unsigned int id;
	unsigned int size;
	unsigned int offset;
} exynos_drm_ipp_params_maps[] = {
	{
		DRM_EXYNOS_IPP_TASK_BUFFER | DRM_EXYNOS_IPP_TASK_TYPE_SOURCE,
		sizeof(struct drm_exynos_ipp_task_buffer),
		offsetof(struct exynos_drm_ipp_task, src.buf),
	}, {
		DRM_EXYNOS_IPP_TASK_BUFFER |
			DRM_EXYNOS_IPP_TASK_TYPE_DESTINATION,
		sizeof(struct drm_exynos_ipp_task_buffer),
		offsetof(struct exynos_drm_ipp_task, dst.buf),
	}, {
		DRM_EXYNOS_IPP_TASK_RECTANGLE | DRM_EXYNOS_IPP_TASK_TYPE_SOURCE,
		sizeof(struct drm_exynos_ipp_task_rect),
		offsetof(struct exynos_drm_ipp_task, src.rect),
	}, {
		DRM_EXYNOS_IPP_TASK_RECTANGLE |
			DRM_EXYNOS_IPP_TASK_TYPE_DESTINATION,
		sizeof(struct drm_exynos_ipp_task_rect),
		offsetof(struct exynos_drm_ipp_task, dst.rect),
	}, {
		DRM_EXYNOS_IPP_TASK_TRANSFORM,
		sizeof(struct drm_exynos_ipp_task_transform),
		offsetof(struct exynos_drm_ipp_task, transform),
	}, {
		DRM_EXYNOS_IPP_TASK_ALPHA,
		sizeof(struct drm_exynos_ipp_task_alpha),
		offsetof(struct exynos_drm_ipp_task, alpha),
	},
};

static int exynos_drm_ipp_task_set(struct exynos_drm_ipp_task *task,
				   struct drm_exynos_ioctl_ipp_commit *arg)
{
	const struct exynos_drm_param_map *map = exynos_drm_ipp_params_maps;
	void __user *params = (void __user *)(unsigned long)arg->params_ptr;
	unsigned int size = arg->params_size;
	uint32_t id;
	int i;

	while (size) {
		if (get_user(id, (uint32_t __user *)params))
			return -EFAULT;

		for (i = 0; i < ARRAY_SIZE(exynos_drm_ipp_params_maps); i++)
			if (map[i].id == id)
				break;
		if (i == ARRAY_SIZE(exynos_drm_ipp_params_maps) ||
		    map[i].size > size)
			return -EINVAL;

		if (copy_from_user((void *)task + map[i].offset, params,
				   map[i].size))
			return -EFAULT;

		params += map[i].size;
		size -= map[i].size;
	}

	DRM_DEBUG_DRIVER("Got task %pK configuration from userspace\n", task);
	return 0;
}

static int exynos_drm_ipp_task_setup_buffer(struct exynos_drm_ipp_buffer *buf,
					    struct drm_file *filp)
{
	int ret = 0;
	int i;

	/* get GEM buffers and check their size */
	for (i = 0; i < buf->format->num_planes; i++) {
		unsigned int height = (i == 0) ? buf->buf.height :
			     DIV_ROUND_UP(buf->buf.height, buf->format->vsub);
		unsigned long size = height * buf->buf.pitch[i];
		struct exynos_drm_gem *gem = exynos_drm_gem_get(filp,
							    buf->buf.gem_id[i]);
		if (!gem) {
			ret = -ENOENT;
			goto gem_free;
		}
		buf->exynos_gem[i] = gem;

		if (size + buf->buf.offset[i] > buf->exynos_gem[i]->size) {
			i++;
			ret = -EINVAL;
			goto gem_free;
		}
		buf->dma_addr[i] = buf->exynos_gem[i]->dma_addr +
				   buf->buf.offset[i];
	}

	return 0;
gem_free:
	while (i--) {
		exynos_drm_gem_put(buf->exynos_gem[i]);
		buf->exynos_gem[i] = NULL;
	}
	return ret;
}

static void exynos_drm_ipp_task_release_buf(struct exynos_drm_ipp_buffer *buf)
{
	int i;

	if (!buf->exynos_gem[0])
		return;
	for (i = 0; i < buf->format->num_planes; i++)
		exynos_drm_gem_put(buf->exynos_gem[i]);
}

static void exynos_drm_ipp_task_free(struct exynos_drm_ipp *ipp,
				 struct exynos_drm_ipp_task *task)
{
	DRM_DEBUG_DRIVER("Freeing task %pK\n", task);

	exynos_drm_ipp_task_release_buf(&task->src);
	exynos_drm_ipp_task_release_buf(&task->dst);
	if (task->event)
		drm_event_cancel_free(ipp->dev, &task->event->base);
	kfree(task);
}

struct drm_ipp_limit {
	struct drm_exynos_ipp_limit_val h;
	struct drm_exynos_ipp_limit_val v;
};

enum drm_ipp_size_id {
	IPP_LIMIT_BUFFER, IPP_LIMIT_AREA, IPP_LIMIT_ROTATED, IPP_LIMIT_MAX
};

static const enum drm_exynos_ipp_limit_type limit_id_fallback[IPP_LIMIT_MAX][4] = {
	[IPP_LIMIT_BUFFER]  = { DRM_EXYNOS_IPP_LIMIT_SIZE_BUFFER },
	[IPP_LIMIT_AREA]    = { DRM_EXYNOS_IPP_LIMIT_SIZE_AREA,
				DRM_EXYNOS_IPP_LIMIT_SIZE_BUFFER },
	[IPP_LIMIT_ROTATED] = { DRM_EXYNOS_IPP_LIMIT_SIZE_ROTATED,
				DRM_EXYNOS_IPP_LIMIT_SIZE_AREA,
				DRM_EXYNOS_IPP_LIMIT_SIZE_BUFFER },
};

static inline void __limit_set_val(unsigned int *ptr, unsigned int val)
{
	if (!*ptr)
		*ptr = val;
}

static void __get_size_limit(const struct drm_exynos_ipp_limit *limits,
			     unsigned int num_limits, enum drm_ipp_size_id id,
			     struct drm_ipp_limit *res)
{
	const struct drm_exynos_ipp_limit *l = limits;
	int i = 0;

	memset(res, 0, sizeof(*res));
	for (i = 0; limit_id_fallback[id][i]; i++)
		for (l = limits; l - limits < num_limits; l++) {
			if (((l->type & DRM_EXYNOS_IPP_LIMIT_TYPE_MASK) !=
			      DRM_EXYNOS_IPP_LIMIT_TYPE_SIZE) ||
			    ((l->type & DRM_EXYNOS_IPP_LIMIT_SIZE_MASK) !=
						     limit_id_fallback[id][i]))
				continue;
			__limit_set_val(&res->h.min, l->h.min);
			__limit_set_val(&res->h.max, l->h.max);
			__limit_set_val(&res->h.align, l->h.align);
			__limit_set_val(&res->v.min, l->v.min);
			__limit_set_val(&res->v.max, l->v.max);
			__limit_set_val(&res->v.align, l->v.align);
		}
}

static inline bool __align_check(unsigned int val, unsigned int align)
{
	if (align && (val & (align - 1))) {
		DRM_DEBUG_DRIVER("Value %d exceeds HW limits (align %d)\n",
				 val, align);
		return false;
	}
	return true;
}

static inline bool __size_limit_check(unsigned int val,
				 struct drm_exynos_ipp_limit_val *l)
{
	if ((l->min && val < l->min) || (l->max && val > l->max)) {
		DRM_DEBUG_DRIVER("Value %d exceeds HW limits (min %d, max %d)\n",
				 val, l->min, l->max);
		return false;
	}
	return __align_check(val, l->align);
}

static int exynos_drm_ipp_check_size_limits(struct exynos_drm_ipp_buffer *buf,
	const struct drm_exynos_ipp_limit *limits, unsigned int num_limits,
	bool rotate, bool swap)
{
	enum drm_ipp_size_id id = rotate ? IPP_LIMIT_ROTATED : IPP_LIMIT_AREA;
	struct drm_ipp_limit l;
	struct drm_exynos_ipp_limit_val *lh = &l.h, *lv = &l.v;
	int real_width = buf->buf.pitch[0] / buf->format->cpp[0];

	if (!limits)
		return 0;

	__get_size_limit(limits, num_limits, IPP_LIMIT_BUFFER, &l);
	if (!__size_limit_check(real_width, &l.h) ||
	    !__size_limit_check(buf->buf.height, &l.v))
		return -EINVAL;

	if (swap) {
		lv = &l.h;
		lh = &l.v;
	}
	__get_size_limit(limits, num_limits, id, &l);
	if (!__size_limit_check(buf->rect.w, lh) ||
	    !__align_check(buf->rect.x, lh->align) ||
	    !__size_limit_check(buf->rect.h, lv) ||
	    !__align_check(buf->rect.y, lv->align))
		return -EINVAL;

	return 0;
}

static inline bool __scale_limit_check(unsigned int src, unsigned int dst,
				       unsigned int min, unsigned int max)
{
	if ((max && (dst << 16) > src * max) ||
	    (min && (dst << 16) < src * min)) {
		DRM_DEBUG_DRIVER("Scale from %d to %d exceeds HW limits (ratio min %d.%05d, max %d.%05d)\n",
			 src, dst,
			 min >> 16, 100000 * (min & 0xffff) / (1 << 16),
			 max >> 16, 100000 * (max & 0xffff) / (1 << 16));
		return false;
	}
	return true;
}

static int exynos_drm_ipp_check_scale_limits(
				struct drm_exynos_ipp_task_rect *src,
				struct drm_exynos_ipp_task_rect *dst,
				const struct drm_exynos_ipp_limit *limits,
				unsigned int num_limits, bool swap)
{
	const struct drm_exynos_ipp_limit_val *lh, *lv;
	int dw, dh;

	for (; num_limits; limits++, num_limits--)
		if ((limits->type & DRM_EXYNOS_IPP_LIMIT_TYPE_MASK) ==
		    DRM_EXYNOS_IPP_LIMIT_TYPE_SCALE)
			break;
	if (!num_limits)
		return 0;

	lh = (!swap) ? &limits->h : &limits->v;
	lv = (!swap) ? &limits->v : &limits->h;
	dw = (!swap) ? dst->w : dst->h;
	dh = (!swap) ? dst->h : dst->w;

	if (!__scale_limit_check(src->w, dw, lh->min, lh->max) ||
	    !__scale_limit_check(src->h, dh, lv->min, lv->max))
		return -EINVAL;

	return 0;
}

static int exynos_drm_ipp_check_format(struct exynos_drm_ipp_task *task,
				       struct exynos_drm_ipp_buffer *buf,
				       struct exynos_drm_ipp_buffer *src,
				       struct exynos_drm_ipp_buffer *dst,
				       bool rotate, bool swap)
{
	const struct exynos_drm_ipp_formats *fmt;
	int ret, i;

	fmt = __ipp_format_get(task->ipp, buf->buf.fourcc, buf->buf.modifier,
			       buf == src ? DRM_EXYNOS_IPP_FORMAT_SOURCE :
					    DRM_EXYNOS_IPP_FORMAT_DESTINATION);
	if (!fmt) {
		DRM_DEBUG_DRIVER("Task %pK: %s format not supported\n", task,
				 buf == src ? "src" : "dst");
		return -EINVAL;
	}

	/* basic checks */
	if (buf->buf.width == 0 || buf->buf.height == 0)
		return -EINVAL;

	buf->format = drm_format_info(buf->buf.fourcc);
	for (i = 0; i < buf->format->num_planes; i++) {
		unsigned int width = (i == 0) ? buf->buf.width :
			     DIV_ROUND_UP(buf->buf.width, buf->format->hsub);

		if (buf->buf.pitch[i] == 0)
			buf->buf.pitch[i] = width * buf->format->cpp[i];
		if (buf->buf.pitch[i] < width * buf->format->cpp[i])
			return -EINVAL;
		if (!buf->buf.gem_id[i])
			return -ENOENT;
	}

	/* pitch for additional planes must match */
	if (buf->format->num_planes > 2 &&
	    buf->buf.pitch[1] != buf->buf.pitch[2])
		return -EINVAL;

	/* check driver limits */
	ret = exynos_drm_ipp_check_size_limits(buf, fmt->limits,
					       fmt->num_limits,
					       rotate,
					       buf == dst ? swap : false);
	if (ret)
		return ret;
	ret = exynos_drm_ipp_check_scale_limits(&src->rect, &dst->rect,
						fmt->limits,
						fmt->num_limits, swap);
	return ret;
}

static int exynos_drm_ipp_task_check(struct exynos_drm_ipp_task *task)
{
	struct exynos_drm_ipp *ipp = task->ipp;
	struct exynos_drm_ipp_buffer *src = &task->src, *dst = &task->dst;
	unsigned int rotation = task->transform.rotation;
	int ret = 0;
	bool swap = drm_rotation_90_or_270(rotation);
	bool rotate = (rotation != DRM_MODE_ROTATE_0);
	bool scale = false;

	DRM_DEBUG_DRIVER("Checking task %pK\n", task);

	if (src->rect.w == UINT_MAX)
		src->rect.w = src->buf.width;
	if (src->rect.h == UINT_MAX)
		src->rect.h = src->buf.height;
	if (dst->rect.w == UINT_MAX)
		dst->rect.w = dst->buf.width;
	if (dst->rect.h == UINT_MAX)
		dst->rect.h = dst->buf.height;

	if (src->rect.x + src->rect.w > (src->buf.width) ||
	    src->rect.y + src->rect.h > (src->buf.height) ||
	    dst->rect.x + dst->rect.w > (dst->buf.width) ||
	    dst->rect.y + dst->rect.h > (dst->buf.height)) {
		DRM_DEBUG_DRIVER("Task %pK: defined area is outside provided buffers\n",
				 task);
		return -EINVAL;
	}

	if ((!swap && (src->rect.w != dst->rect.w ||
		       src->rect.h != dst->rect.h)) ||
	    (swap && (src->rect.w != dst->rect.h ||
		      src->rect.h != dst->rect.w)))
		scale = true;

	if ((!(ipp->capabilities & DRM_EXYNOS_IPP_CAP_CROP) &&
	     (src->rect.x || src->rect.y || dst->rect.x || dst->rect.y)) ||
	    (!(ipp->capabilities & DRM_EXYNOS_IPP_CAP_ROTATE) && rotate) ||
	    (!(ipp->capabilities & DRM_EXYNOS_IPP_CAP_SCALE) && scale) ||
	    (!(ipp->capabilities & DRM_EXYNOS_IPP_CAP_CONVERT) &&
	     src->buf.fourcc != dst->buf.fourcc)) {
		DRM_DEBUG_DRIVER("Task %pK: hw capabilities exceeded\n", task);
		return -EINVAL;
	}

	ret = exynos_drm_ipp_check_format(task, src, src, dst, rotate, swap);
	if (ret)
		return ret;

	ret = exynos_drm_ipp_check_format(task, dst, src, dst, false, swap);
	if (ret)
		return ret;

	DRM_DEBUG_DRIVER("Task %pK: all checks done.\n", task);

	return ret;
}

static int exynos_drm_ipp_task_setup_buffers(struct exynos_drm_ipp_task *task,
				     struct drm_file *filp)
{
	struct exynos_drm_ipp_buffer *src = &task->src, *dst = &task->dst;
	int ret = 0;

	DRM_DEBUG_DRIVER("Setting buffer for task %pK\n", task);

	ret = exynos_drm_ipp_task_setup_buffer(src, filp);
	if (ret) {
		DRM_DEBUG_DRIVER("Task %pK: src buffer setup failed\n", task);
		return ret;
	}
	ret = exynos_drm_ipp_task_setup_buffer(dst, filp);
	if (ret) {
		DRM_DEBUG_DRIVER("Task %pK: dst buffer setup failed\n", task);
		return ret;
	}

	DRM_DEBUG_DRIVER("Task %pK: buffers prepared.\n", task);

	return ret;
}


static int exynos_drm_ipp_event_create(struct exynos_drm_ipp_task *task,
				 struct drm_file *file_priv, uint64_t user_data)
{
	struct drm_pending_exynos_ipp_event *e = NULL;
	int ret;

	e = kzalloc(sizeof(*e), GFP_KERNEL);
	if (!e)
		return -ENOMEM;

	e->event.base.type = DRM_EXYNOS_IPP_EVENT;
	e->event.base.length = sizeof(e->event);
	e->event.user_data = user_data;

	ret = drm_event_reserve_init(task->dev, file_priv, &e->base,
				     &e->event.base);
	if (ret)
		goto free;

	task->event = e;
	return 0;
free:
	kfree(e);
	return ret;
}

static void exynos_drm_ipp_event_send(struct exynos_drm_ipp_task *task)
{
	struct timespec64 now;

	ktime_get_ts64(&now);
	task->event->event.tv_sec = now.tv_sec;
	task->event->event.tv_usec = now.tv_nsec / NSEC_PER_USEC;
	task->event->event.sequence = atomic_inc_return(&task->ipp->sequence);

	drm_send_event(task->dev, &task->event->base);
}

static int exynos_drm_ipp_task_cleanup(struct exynos_drm_ipp_task *task)
{
	int ret = task->ret;

	if (ret == 0 && task->event) {
		exynos_drm_ipp_event_send(task);
		/* ensure event won't be canceled on task free */
		task->event = NULL;
	}

	exynos_drm_ipp_task_free(task->ipp, task);
	return ret;
}

static void exynos_drm_ipp_cleanup_work(struct work_struct *work)
{
	struct exynos_drm_ipp_task *task = container_of(work,
				      struct exynos_drm_ipp_task, cleanup_work);

	exynos_drm_ipp_task_cleanup(task);
}

static void exynos_drm_ipp_next_task(struct exynos_drm_ipp *ipp);

/**
 * exynos_drm_ipp_task_done - finish given task and set return code
 * @task: ipp task to finish
 * @ret: error code or 0 if operation has been performed successfully
 */
void exynos_drm_ipp_task_done(struct exynos_drm_ipp_task *task, int ret)
{
	struct exynos_drm_ipp *ipp = task->ipp;
	unsigned long flags;

	DRM_DEBUG_DRIVER("ipp: %d, task %pK done: %d\n", ipp->id, task, ret);

	spin_lock_irqsave(&ipp->lock, flags);
	if (ipp->task == task)
		ipp->task = NULL;
	task->flags |= DRM_EXYNOS_IPP_TASK_DONE;
	task->ret = ret;
	spin_unlock_irqrestore(&ipp->lock, flags);

	exynos_drm_ipp_next_task(ipp);
	wake_up(&ipp->done_wq);

	if (task->flags & DRM_EXYNOS_IPP_TASK_ASYNC) {
		INIT_WORK(&task->cleanup_work, exynos_drm_ipp_cleanup_work);
		schedule_work(&task->cleanup_work);
	}
}

static void exynos_drm_ipp_next_task(struct exynos_drm_ipp *ipp)
{
	struct exynos_drm_ipp_task *task;
	unsigned long flags;
	int ret;

	DRM_DEBUG_DRIVER("ipp: %d, try to run new task\n", ipp->id);

	spin_lock_irqsave(&ipp->lock, flags);

	if (ipp->task || list_empty(&ipp->todo_list)) {
		spin_unlock_irqrestore(&ipp->lock, flags);
		return;
	}

	task = list_first_entry(&ipp->todo_list, struct exynos_drm_ipp_task,
				head);
	list_del_init(&task->head);
	ipp->task = task;

	spin_unlock_irqrestore(&ipp->lock, flags);

	DRM_DEBUG_DRIVER("ipp: %d, selected task %pK to run\n", ipp->id, task);

	ret = ipp->funcs->commit(ipp, task);
	if (ret)
		exynos_drm_ipp_task_done(task, ret);
}

static void exynos_drm_ipp_schedule_task(struct exynos_drm_ipp *ipp,
					 struct exynos_drm_ipp_task *task)
{
	unsigned long flags;

	spin_lock_irqsave(&ipp->lock, flags);
	list_add(&task->head, &ipp->todo_list);
	spin_unlock_irqrestore(&ipp->lock, flags);

	exynos_drm_ipp_next_task(ipp);
}

static void exynos_drm_ipp_task_abort(struct exynos_drm_ipp *ipp,
				      struct exynos_drm_ipp_task *task)
{
	unsigned long flags;

	spin_lock_irqsave(&ipp->lock, flags);
	if (task->flags & DRM_EXYNOS_IPP_TASK_DONE) {
		/* already completed task */
		exynos_drm_ipp_task_cleanup(task);
	} else if (ipp->task != task) {
		/* task has not been scheduled for execution yet */
		list_del_init(&task->head);
		exynos_drm_ipp_task_cleanup(task);
	} else {
		/*
		 * currently processed task, call abort() and perform
		 * cleanup with async worker
		 */
		task->flags |= DRM_EXYNOS_IPP_TASK_ASYNC;
		spin_unlock_irqrestore(&ipp->lock, flags);
		if (ipp->funcs->abort)
			ipp->funcs->abort(ipp, task);
		return;
	}
	spin_unlock_irqrestore(&ipp->lock, flags);
}

/**
 * exynos_drm_ipp_commit_ioctl - perform image processing operation
 * @dev: DRM device
 * @data: ioctl data
 * @file_priv: DRM file info
 *
 * Construct a ipp task from the set of properties provided from the user
 * and try to schedule it to framebuffer processor hardware.
 *
 * Called by the user via ioctl.
 *
 * Returns:
 * Zero on success, negative errno on failure.
 */
int exynos_drm_ipp_commit_ioctl(struct drm_device *dev, void *data,
				struct drm_file *file_priv)
{
	struct drm_exynos_ioctl_ipp_commit *arg = data;
	struct exynos_drm_ipp *ipp;
	struct exynos_drm_ipp_task *task;
	int ret = 0;

	if ((arg->flags & ~DRM_EXYNOS_IPP_FLAGS) || arg->reserved)
		return -EINVAL;

	/* can't test and expect an event at the same time */
	if ((arg->flags & DRM_EXYNOS_IPP_FLAG_TEST_ONLY) &&
			(arg->flags & DRM_EXYNOS_IPP_FLAG_EVENT))
		return -EINVAL;

	ipp = __ipp_get(arg->ipp_id);
	if (!ipp)
		return -ENOENT;

	task = exynos_drm_ipp_task_alloc(ipp);
	if (!task)
		return -ENOMEM;

	ret = exynos_drm_ipp_task_set(task, arg);
	if (ret)
		goto free;

	ret = exynos_drm_ipp_task_check(task);
	if (ret)
		goto free;

	ret = exynos_drm_ipp_task_setup_buffers(task, file_priv);
	if (ret || arg->flags & DRM_EXYNOS_IPP_FLAG_TEST_ONLY)
		goto free;

	if (arg->flags & DRM_EXYNOS_IPP_FLAG_EVENT) {
		ret = exynos_drm_ipp_event_create(task, file_priv,
						 arg->user_data);
		if (ret)
			goto free;
	}

	/*
	 * Queue task for processing on the hardware. task object will be
	 * then freed after exynos_drm_ipp_task_done()
	 */
	if (arg->flags & DRM_EXYNOS_IPP_FLAG_NONBLOCK) {
		DRM_DEBUG_DRIVER("ipp: %d, nonblocking processing task %pK\n",
				 ipp->id, task);

		task->flags |= DRM_EXYNOS_IPP_TASK_ASYNC;
		exynos_drm_ipp_schedule_task(task->ipp, task);
		ret = 0;
	} else {
		DRM_DEBUG_DRIVER("ipp: %d, processing task %pK\n", ipp->id,
				 task);
		exynos_drm_ipp_schedule_task(ipp, task);
		ret = wait_event_interruptible(ipp->done_wq,
					task->flags & DRM_EXYNOS_IPP_TASK_DONE);
		if (ret)
			exynos_drm_ipp_task_abort(ipp, task);
		else
			ret = exynos_drm_ipp_task_cleanup(task);
	}
	return ret;
free:
	exynos_drm_ipp_task_free(ipp, task);

	return ret;
}
