/*
 * Copyright (C) 2015 Broadcom
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include "drmP.h"
#include "drm_gem_cma_helper.h"

struct vc4_dev {
	struct drm_device *dev;

	struct vc4_hdmi *hdmi;
	struct vc4_hvs *hvs;
	struct vc4_crtc *crtc[3];
	struct vc4_v3d *v3d;

	struct drm_fbdev_cma *fbdev;

	/* The kernel-space BO cache.  Tracks buffers that have been
	 * unreferenced by all other users (refcounts of 0!) but not
	 * yet freed, so we can do cheap allocations.
	 */
	struct vc4_bo_cache {
		/* Array of list heads for entries in the BO cache,
		 * based on number of pages, so we can do O(1) lookups
		 * in the cache when allocating.
		 */
		struct list_head *size_list;
		uint32_t size_list_size;

		/* List of all BOs in the cache, ordered by age, so we
		 * can do O(1) lookups when trying to free old
		 * buffers.
		 */
		struct list_head time_list;
		struct work_struct time_work;
		struct timer_list time_timer;
	} bo_cache;

	struct vc4_bo_stats {
		u32 num_allocated;
		u32 size_allocated;
		u32 num_cached;
		u32 size_cached;
	} bo_stats;

	/* Protects bo_cache and the BO stats. */
	struct mutex bo_lock;
};

static inline struct vc4_dev *
to_vc4_dev(struct drm_device *dev)
{
	return (struct vc4_dev *)dev->dev_private;
}

struct vc4_bo {
	struct drm_gem_cma_object base;

	/* List entry for the BO's position in either
	 * vc4_exec_info->unref_list or vc4_dev->bo_cache.time_list
	 */
	struct list_head unref_head;

	/* Time in jiffies when the BO was put in vc4->bo_cache. */
	unsigned long free_time;

	/* List entry for the BO's position in vc4_dev->bo_cache.size_list */
	struct list_head size_head;

	/* Struct for shader validation state, if created by
	 * DRM_IOCTL_VC4_CREATE_SHADER_BO.
	 */
	struct vc4_validated_shader_info *validated_shader;
};

static inline struct vc4_bo *
to_vc4_bo(struct drm_gem_object *bo)
{
	return (struct vc4_bo *)bo;
}

struct vc4_v3d {
	struct platform_device *pdev;
	void __iomem *regs;
};

struct vc4_hvs {
	struct platform_device *pdev;
	void __iomem *regs;
	void __iomem *dlist;
};

struct vc4_plane {
	struct drm_plane base;
};

static inline struct vc4_plane *
to_vc4_plane(struct drm_plane *plane)
{
	return (struct vc4_plane *)plane;
}

enum vc4_encoder_type {
	VC4_ENCODER_TYPE_HDMI,
	VC4_ENCODER_TYPE_VEC,
	VC4_ENCODER_TYPE_DSI0,
	VC4_ENCODER_TYPE_DSI1,
	VC4_ENCODER_TYPE_SMI,
	VC4_ENCODER_TYPE_DPI,
};

struct vc4_encoder {
	struct drm_encoder base;
	enum vc4_encoder_type type;
	u32 clock_select;
};

static inline struct vc4_encoder *
to_vc4_encoder(struct drm_encoder *encoder)
{
	return container_of(encoder, struct vc4_encoder, base);
}

#define V3D_READ(offset) readl(vc4->v3d->regs + offset)
#define V3D_WRITE(offset, val) writel(val, vc4->v3d->regs + offset)
#define HVS_READ(offset) readl(vc4->hvs->regs + offset)
#define HVS_WRITE(offset, val) writel(val, vc4->hvs->regs + offset)

/**
 * struct vc4_texture_sample_info - saves the offsets into the UBO for texture
 * setup parameters.
 *
 * This will be used at draw time to relocate the reference to the texture
 * contents in p0, and validate that the offset combined with
 * width/height/stride/etc. from p1 and p2/p3 doesn't sample outside the BO.
 * Note that the hardware treats unprovided config parameters as 0, so not all
 * of them need to be set up for every texure sample, and we'll store ~0 as
 * the offset to mark the unused ones.
 *
 * See the VC4 3D architecture guide page 41 ("Texture and Memory Lookup Unit
 * Setup") for definitions of the texture parameters.
 */
struct vc4_texture_sample_info {
	bool is_direct;
	uint32_t p_offset[4];
};

/**
 * struct vc4_validated_shader_info - information about validated shaders that
 * needs to be used from command list validation.
 *
 * For a given shader, each time a shader state record references it, we need
 * to verify that the shader doesn't read more uniforms than the shader state
 * record's uniform BO pointer can provide, and we need to apply relocations
 * and validate the shader state record's uniforms that define the texture
 * samples.
 */
struct vc4_validated_shader_info {
	uint32_t uniforms_size;
	uint32_t uniforms_src_size;
	uint32_t num_texture_samples;
	struct vc4_texture_sample_info *texture_samples;
};

/**
 * _wait_for - magic (register) wait macro
 *
 * Does the right thing for modeset paths when run under kdgb or similar atomic
 * contexts. Note that it's important that we check the condition again after
 * having timed out, since the timeout could be due to preemption or similar and
 * we've never had a chance to check the condition before the timeout.
 */
#define _wait_for(COND, MS, W) ({ \
	unsigned long timeout__ = jiffies + msecs_to_jiffies(MS) + 1;	\
	int ret__ = 0;							\
	while (!(COND)) {						\
		if (time_after(jiffies, timeout__)) {			\
			if (!(COND))					\
				ret__ = -ETIMEDOUT;			\
			break;						\
		}							\
		if (W && drm_can_sleep())  {				\
			msleep(W);					\
		} else {						\
			cpu_relax();					\
		}							\
	}								\
	ret__;								\
})

#define wait_for(COND, MS) _wait_for(COND, MS, 1)

/* vc4_bo.c */
struct drm_gem_object *vc4_create_object(struct drm_device *dev, size_t size);
void vc4_free_object(struct drm_gem_object *gem_obj);
struct vc4_bo *vc4_bo_create(struct drm_device *dev, size_t size,
			     bool from_cache);
int vc4_dumb_create(struct drm_file *file_priv,
		    struct drm_device *dev,
		    struct drm_mode_create_dumb *args);
struct dma_buf *vc4_prime_export(struct drm_device *dev,
				 struct drm_gem_object *obj, int flags);
int vc4_create_bo_ioctl(struct drm_device *dev, void *data,
			struct drm_file *file_priv);
int vc4_create_shader_bo_ioctl(struct drm_device *dev, void *data,
			       struct drm_file *file_priv);
int vc4_mmap_bo_ioctl(struct drm_device *dev, void *data,
		      struct drm_file *file_priv);
int vc4_mmap(struct file *filp, struct vm_area_struct *vma);
int vc4_prime_mmap(struct drm_gem_object *obj, struct vm_area_struct *vma);
void *vc4_prime_vmap(struct drm_gem_object *obj);
void vc4_bo_cache_init(struct drm_device *dev);
void vc4_bo_cache_destroy(struct drm_device *dev);
int vc4_bo_stats_debugfs(struct seq_file *m, void *arg);

/* vc4_crtc.c */
extern struct platform_driver vc4_crtc_driver;
int vc4_enable_vblank(struct drm_device *dev, unsigned int crtc_id);
void vc4_disable_vblank(struct drm_device *dev, unsigned int crtc_id);
void vc4_cancel_page_flip(struct drm_crtc *crtc, struct drm_file *file);
int vc4_crtc_debugfs_regs(struct seq_file *m, void *arg);

/* vc4_debugfs.c */
int vc4_debugfs_init(struct drm_minor *minor);
void vc4_debugfs_cleanup(struct drm_minor *minor);

/* vc4_drv.c */
void __iomem *vc4_ioremap_regs(struct platform_device *dev, int index);

/* vc4_hdmi.c */
extern struct platform_driver vc4_hdmi_driver;
int vc4_hdmi_debugfs_regs(struct seq_file *m, void *unused);

/* vc4_hvs.c */
extern struct platform_driver vc4_hvs_driver;
void vc4_hvs_dump_state(struct drm_device *dev);
int vc4_hvs_debugfs_regs(struct seq_file *m, void *unused);

/* vc4_kms.c */
int vc4_kms_load(struct drm_device *dev);

/* vc4_plane.c */
struct drm_plane *vc4_plane_init(struct drm_device *dev,
				 enum drm_plane_type type);
u32 vc4_plane_write_dlist(struct drm_plane *plane, u32 __iomem *dlist);
u32 vc4_plane_dlist_size(struct drm_plane_state *state);

/* vc4_v3d.c */
extern struct platform_driver vc4_v3d_driver;
int vc4_v3d_debugfs_ident(struct seq_file *m, void *unused);
int vc4_v3d_debugfs_regs(struct seq_file *m, void *unused);

/* vc4_validate_shader.c */
struct vc4_validated_shader_info *
vc4_validate_shader(struct drm_gem_cma_object *shader_obj);
