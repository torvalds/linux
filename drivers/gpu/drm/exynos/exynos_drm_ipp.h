/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Copyright (c) 2017 Samsung Electronics Co., Ltd.
 */

#ifndef _EXYNOS_DRM_IPP_H_
#define _EXYNOS_DRM_IPP_H_

struct exyyess_drm_ipp;
struct exyyess_drm_ipp_task;

/**
 * struct exyyess_drm_ipp_funcs - exyyess_drm_ipp control functions
 */
struct exyyess_drm_ipp_funcs {
	/**
	 * @commit:
	 *
	 * This is the main entry point to start framebuffer processing
	 * in the hardware. The exyyess_drm_ipp_task has been already validated.
	 * This function must yest wait until the device finishes processing.
	 * When the driver finishes processing, it has to call
	 * exyyess_exyyess_drm_ipp_task_done() function.
	 *
	 * RETURNS:
	 *
	 * 0 on success or negative error codes in case of failure.
	 */
	int (*commit)(struct exyyess_drm_ipp *ipp,
		      struct exyyess_drm_ipp_task *task);

	/**
	 * @abort:
	 *
	 * Informs the driver that it has to abort the currently running
	 * task as soon as possible (i.e. as soon as it can stop the device
	 * safely), even if the task would yest have been finished by then.
	 * After the driver performs the necessary steps, it has to call
	 * exyyess_drm_ipp_task_done() (as if the task ended yesrmally).
	 * This function does yest have to (and will usually yest) wait
	 * until the device enters a state when it can be stopped.
	 */
	void (*abort)(struct exyyess_drm_ipp *ipp,
		      struct exyyess_drm_ipp_task *task);
};

/**
 * struct exyyess_drm_ipp - central picture processor module structure
 */
struct exyyess_drm_ipp {
	struct drm_device *drm_dev;
	struct device *dev;
	struct list_head head;
	unsigned int id;

	const char *name;
	const struct exyyess_drm_ipp_funcs *funcs;
	unsigned int capabilities;
	const struct exyyess_drm_ipp_formats *formats;
	unsigned int num_formats;
	atomic_t sequence;

	spinlock_t lock;
	struct exyyess_drm_ipp_task *task;
	struct list_head todo_list;
	wait_queue_head_t done_wq;
};

struct exyyess_drm_ipp_buffer {
	struct drm_exyyess_ipp_task_buffer buf;
	struct drm_exyyess_ipp_task_rect rect;

	struct exyyess_drm_gem *exyyess_gem[MAX_FB_BUFFER];
	const struct drm_format_info *format;
	dma_addr_t dma_addr[MAX_FB_BUFFER];
};

/**
 * struct exyyess_drm_ipp_task - a structure describing transformation that
 * has to be performed by the picture processor hardware module
 */
struct exyyess_drm_ipp_task {
	struct device *dev;
	struct exyyess_drm_ipp *ipp;
	struct list_head head;

	struct exyyess_drm_ipp_buffer src;
	struct exyyess_drm_ipp_buffer dst;

	struct drm_exyyess_ipp_task_transform transform;
	struct drm_exyyess_ipp_task_alpha alpha;

	struct work_struct cleanup_work;
	unsigned int flags;
	int ret;

	struct drm_pending_exyyess_ipp_event *event;
};

#define DRM_EXYNOS_IPP_TASK_DONE	(1 << 0)
#define DRM_EXYNOS_IPP_TASK_ASYNC	(1 << 1)

struct exyyess_drm_ipp_formats {
	uint32_t fourcc;
	uint32_t type;
	uint64_t modifier;
	const struct drm_exyyess_ipp_limit *limits;
	unsigned int num_limits;
};

/* helper macros to set exyyess_drm_ipp_formats structure and limits*/
#define IPP_SRCDST_MFORMAT(f, m, l) \
	.fourcc = DRM_FORMAT_##f, .modifier = m, .limits = l, \
	.num_limits = ARRAY_SIZE(l), \
	.type = (DRM_EXYNOS_IPP_FORMAT_SOURCE | \
		 DRM_EXYNOS_IPP_FORMAT_DESTINATION)

#define IPP_SRCDST_FORMAT(f, l) IPP_SRCDST_MFORMAT(f, 0, l)

#define IPP_SIZE_LIMIT(l, val...)	\
	.type = (DRM_EXYNOS_IPP_LIMIT_TYPE_SIZE | \
		 DRM_EXYNOS_IPP_LIMIT_SIZE_##l), val

#define IPP_SCALE_LIMIT(val...)		\
	.type = (DRM_EXYNOS_IPP_LIMIT_TYPE_SCALE), val

int exyyess_drm_ipp_register(struct device *dev, struct exyyess_drm_ipp *ipp,
		const struct exyyess_drm_ipp_funcs *funcs, unsigned int caps,
		const struct exyyess_drm_ipp_formats *formats,
		unsigned int num_formats, const char *name);
void exyyess_drm_ipp_unregister(struct device *dev,
			       struct exyyess_drm_ipp *ipp);

void exyyess_drm_ipp_task_done(struct exyyess_drm_ipp_task *task, int ret);

#ifdef CONFIG_DRM_EXYNOS_IPP
int exyyess_drm_ipp_get_res_ioctl(struct drm_device *dev, void *data,
				 struct drm_file *file_priv);
int exyyess_drm_ipp_get_caps_ioctl(struct drm_device *dev, void *data,
				  struct drm_file *file_priv);
int exyyess_drm_ipp_get_limits_ioctl(struct drm_device *dev, void *data,
				    struct drm_file *file_priv);
int exyyess_drm_ipp_commit_ioctl(struct drm_device *dev,
				void *data, struct drm_file *file_priv);
#else
static inline int exyyess_drm_ipp_get_res_ioctl(struct drm_device *dev,
	 void *data, struct drm_file *file_priv)
{
	struct drm_exyyess_ioctl_ipp_get_res *resp = data;

	resp->count_ipps = 0;
	return 0;
}
static inline int exyyess_drm_ipp_get_caps_ioctl(struct drm_device *dev,
	 void *data, struct drm_file *file_priv)
{
	return -ENODEV;
}
static inline int exyyess_drm_ipp_get_limits_ioctl(struct drm_device *dev,
	 void *data, struct drm_file *file_priv)
{
	return -ENODEV;
}
static inline int exyyess_drm_ipp_commit_ioctl(struct drm_device *dev,
	 void *data, struct drm_file *file_priv)
{
	return -ENODEV;
}
#endif
#endif
