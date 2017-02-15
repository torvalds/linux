/*
 * Copyright (C) 2015 Broadcom
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include "drmP.h"
#include "drm_gem_cma_helper.h"

#include <drm/drm_encoder.h>

struct vc4_dev {
	struct drm_device *dev;

	struct vc4_hdmi *hdmi;
	struct vc4_hvs *hvs;
	struct vc4_v3d *v3d;
	struct vc4_dpi *dpi;
	struct vc4_dsi *dsi1;
	struct vc4_vec *vec;

	struct drm_fbdev_cma *fbdev;

	struct vc4_hang_state *hang_state;

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

	/* Sequence number for the last job queued in bin_job_list.
	 * Starts at 0 (no jobs emitted).
	 */
	uint64_t emit_seqno;

	/* Sequence number for the last completed job on the GPU.
	 * Starts at 0 (no jobs completed).
	 */
	uint64_t finished_seqno;

	/* List of all struct vc4_exec_info for jobs to be executed in
	 * the binner.  The first job in the list is the one currently
	 * programmed into ct0ca for execution.
	 */
	struct list_head bin_job_list;

	/* List of all struct vc4_exec_info for jobs that have
	 * completed binning and are ready for rendering.  The first
	 * job in the list is the one currently programmed into ct1ca
	 * for execution.
	 */
	struct list_head render_job_list;

	/* List of the finished vc4_exec_infos waiting to be freed by
	 * job_done_work.
	 */
	struct list_head job_done_list;
	/* Spinlock used to synchronize the job_list and seqno
	 * accesses between the IRQ handler and GEM ioctls.
	 */
	spinlock_t job_lock;
	wait_queue_head_t job_wait_queue;
	struct work_struct job_done_work;

	/* List of struct vc4_seqno_cb for callbacks to be made from a
	 * workqueue when the given seqno is passed.
	 */
	struct list_head seqno_cb_list;

	/* The binner overflow memory that's currently set up in
	 * BPOA/BPOS registers.  When overflow occurs and a new one is
	 * allocated, the previous one will be moved to
	 * vc4->current_exec's free list.
	 */
	struct vc4_bo *overflow_mem;
	struct work_struct overflow_mem_work;

	int power_refcount;

	/* Mutex controlling the power refcount. */
	struct mutex power_lock;

	struct {
		struct timer_list timer;
		struct work_struct reset_work;
	} hangcheck;

	struct semaphore async_modeset;
};

static inline struct vc4_dev *
to_vc4_dev(struct drm_device *dev)
{
	return (struct vc4_dev *)dev->dev_private;
}

struct vc4_bo {
	struct drm_gem_cma_object base;

	/* seqno of the last job to render using this BO. */
	uint64_t seqno;

	/* seqno of the last job to use the RCL to write to this BO.
	 *
	 * Note that this doesn't include binner overflow memory
	 * writes.
	 */
	uint64_t write_seqno;

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

struct vc4_seqno_cb {
	struct work_struct work;
	uint64_t seqno;
	void (*func)(struct vc4_seqno_cb *cb);
};

struct vc4_v3d {
	struct vc4_dev *vc4;
	struct platform_device *pdev;
	void __iomem *regs;
};

struct vc4_hvs {
	struct platform_device *pdev;
	void __iomem *regs;
	u32 __iomem *dlist;

	/* Memory manager for CRTCs to allocate space in the display
	 * list.  Units are dwords.
	 */
	struct drm_mm dlist_mm;
	/* Memory manager for the LBM memory used by HVS scaling. */
	struct drm_mm lbm_mm;
	spinlock_t mm_lock;

	struct drm_mm_node mitchell_netravali_filter;
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
	VC4_ENCODER_TYPE_NONE,
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

struct vc4_exec_info {
	/* Sequence number for this bin/render job. */
	uint64_t seqno;

	/* Latest write_seqno of any BO that binning depends on. */
	uint64_t bin_dep_seqno;

	/* Last current addresses the hardware was processing when the
	 * hangcheck timer checked on us.
	 */
	uint32_t last_ct0ca, last_ct1ca;

	/* Kernel-space copy of the ioctl arguments */
	struct drm_vc4_submit_cl *args;

	/* This is the array of BOs that were looked up at the start of exec.
	 * Command validation will use indices into this array.
	 */
	struct drm_gem_cma_object **bo;
	uint32_t bo_count;

	/* List of BOs that are being written by the RCL.  Other than
	 * the binner temporary storage, this is all the BOs written
	 * by the job.
	 */
	struct drm_gem_cma_object *rcl_write_bo[4];
	uint32_t rcl_write_bo_count;

	/* Pointers for our position in vc4->job_list */
	struct list_head head;

	/* List of other BOs used in the job that need to be released
	 * once the job is complete.
	 */
	struct list_head unref_list;

	/* Current unvalidated indices into @bo loaded by the non-hardware
	 * VC4_PACKET_GEM_HANDLES.
	 */
	uint32_t bo_index[2];

	/* This is the BO where we store the validated command lists, shader
	 * records, and uniforms.
	 */
	struct drm_gem_cma_object *exec_bo;

	/**
	 * This tracks the per-shader-record state (packet 64) that
	 * determines the length of the shader record and the offset
	 * it's expected to be found at.  It gets read in from the
	 * command lists.
	 */
	struct vc4_shader_state {
		uint32_t addr;
		/* Maximum vertex index referenced by any primitive using this
		 * shader state.
		 */
		uint32_t max_index;
	} *shader_state;

	/** How many shader states the user declared they were using. */
	uint32_t shader_state_size;
	/** How many shader state records the validator has seen. */
	uint32_t shader_state_count;

	bool found_tile_binning_mode_config_packet;
	bool found_start_tile_binning_packet;
	bool found_increment_semaphore_packet;
	bool found_flush;
	uint8_t bin_tiles_x, bin_tiles_y;
	struct drm_gem_cma_object *tile_bo;
	uint32_t tile_alloc_offset;

	/**
	 * Computed addresses pointing into exec_bo where we start the
	 * bin thread (ct0) and render thread (ct1).
	 */
	uint32_t ct0ca, ct0ea;
	uint32_t ct1ca, ct1ea;

	/* Pointer to the unvalidated bin CL (if present). */
	void *bin_u;

	/* Pointers to the shader recs.  These paddr gets incremented as CL
	 * packets are relocated in validate_gl_shader_state, and the vaddrs
	 * (u and v) get incremented and size decremented as the shader recs
	 * themselves are validated.
	 */
	void *shader_rec_u;
	void *shader_rec_v;
	uint32_t shader_rec_p;
	uint32_t shader_rec_size;

	/* Pointers to the uniform data.  These pointers are incremented, and
	 * size decremented, as each batch of uniforms is uploaded.
	 */
	void *uniforms_u;
	void *uniforms_v;
	uint32_t uniforms_p;
	uint32_t uniforms_size;
};

static inline struct vc4_exec_info *
vc4_first_bin_job(struct vc4_dev *vc4)
{
	return list_first_entry_or_null(&vc4->bin_job_list,
					struct vc4_exec_info, head);
}

static inline struct vc4_exec_info *
vc4_first_render_job(struct vc4_dev *vc4)
{
	return list_first_entry_or_null(&vc4->render_job_list,
					struct vc4_exec_info, head);
}

static inline struct vc4_exec_info *
vc4_last_render_job(struct vc4_dev *vc4)
{
	if (list_empty(&vc4->render_job_list))
		return NULL;
	return list_last_entry(&vc4->render_job_list,
			       struct vc4_exec_info, head);
}

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

	uint32_t num_uniform_addr_offsets;
	uint32_t *uniform_addr_offsets;

	bool is_threaded;
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
int vc4_get_hang_state_ioctl(struct drm_device *dev, void *data,
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
bool vc4_event_pending(struct drm_crtc *crtc);
int vc4_crtc_debugfs_regs(struct seq_file *m, void *arg);
int vc4_crtc_get_scanoutpos(struct drm_device *dev, unsigned int crtc_id,
			    unsigned int flags, int *vpos, int *hpos,
			    ktime_t *stime, ktime_t *etime,
			    const struct drm_display_mode *mode);
int vc4_crtc_get_vblank_timestamp(struct drm_device *dev, unsigned int crtc_id,
				  int *max_error, struct timeval *vblank_time,
				  unsigned flags);

/* vc4_debugfs.c */
int vc4_debugfs_init(struct drm_minor *minor);

/* vc4_drv.c */
void __iomem *vc4_ioremap_regs(struct platform_device *dev, int index);

/* vc4_dpi.c */
extern struct platform_driver vc4_dpi_driver;
int vc4_dpi_debugfs_regs(struct seq_file *m, void *unused);

/* vc4_dsi.c */
extern struct platform_driver vc4_dsi_driver;
int vc4_dsi_debugfs_regs(struct seq_file *m, void *unused);

/* vc4_gem.c */
void vc4_gem_init(struct drm_device *dev);
void vc4_gem_destroy(struct drm_device *dev);
int vc4_submit_cl_ioctl(struct drm_device *dev, void *data,
			struct drm_file *file_priv);
int vc4_wait_seqno_ioctl(struct drm_device *dev, void *data,
			 struct drm_file *file_priv);
int vc4_wait_bo_ioctl(struct drm_device *dev, void *data,
		      struct drm_file *file_priv);
void vc4_submit_next_bin_job(struct drm_device *dev);
void vc4_submit_next_render_job(struct drm_device *dev);
void vc4_move_job_to_render(struct drm_device *dev, struct vc4_exec_info *exec);
int vc4_wait_for_seqno(struct drm_device *dev, uint64_t seqno,
		       uint64_t timeout_ns, bool interruptible);
void vc4_job_handle_completed(struct vc4_dev *vc4);
int vc4_queue_seqno_cb(struct drm_device *dev,
		       struct vc4_seqno_cb *cb, uint64_t seqno,
		       void (*func)(struct vc4_seqno_cb *cb));

/* vc4_hdmi.c */
extern struct platform_driver vc4_hdmi_driver;
int vc4_hdmi_debugfs_regs(struct seq_file *m, void *unused);

/* vc4_hdmi.c */
extern struct platform_driver vc4_vec_driver;
int vc4_vec_debugfs_regs(struct seq_file *m, void *unused);

/* vc4_irq.c */
irqreturn_t vc4_irq(int irq, void *arg);
void vc4_irq_preinstall(struct drm_device *dev);
int vc4_irq_postinstall(struct drm_device *dev);
void vc4_irq_uninstall(struct drm_device *dev);
void vc4_irq_reset(struct drm_device *dev);

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
u32 vc4_plane_dlist_size(const struct drm_plane_state *state);
void vc4_plane_async_set_fb(struct drm_plane *plane,
			    struct drm_framebuffer *fb);

/* vc4_v3d.c */
extern struct platform_driver vc4_v3d_driver;
int vc4_v3d_debugfs_ident(struct seq_file *m, void *unused);
int vc4_v3d_debugfs_regs(struct seq_file *m, void *unused);

/* vc4_validate.c */
int
vc4_validate_bin_cl(struct drm_device *dev,
		    void *validated,
		    void *unvalidated,
		    struct vc4_exec_info *exec);

int
vc4_validate_shader_recs(struct drm_device *dev, struct vc4_exec_info *exec);

struct drm_gem_cma_object *vc4_use_bo(struct vc4_exec_info *exec,
				      uint32_t hindex);

int vc4_get_rcl(struct drm_device *dev, struct vc4_exec_info *exec);

bool vc4_check_tex_size(struct vc4_exec_info *exec,
			struct drm_gem_cma_object *fbo,
			uint32_t offset, uint8_t tiling_format,
			uint32_t width, uint32_t height, uint8_t cpp);

/* vc4_validate_shader.c */
struct vc4_validated_shader_info *
vc4_validate_shader(struct drm_gem_cma_object *shader_obj);
