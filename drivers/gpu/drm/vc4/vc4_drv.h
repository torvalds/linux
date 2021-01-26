/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2015 Broadcom
 */
#ifndef _VC4_DRV_H_
#define _VC4_DRV_H_

#include <linux/delay.h>
#include <linux/refcount.h>
#include <linux/uaccess.h>

#include <drm/drm_atomic.h>
#include <drm/drm_debugfs.h>
#include <drm/drm_device.h>
#include <drm/drm_encoder.h>
#include <drm/drm_gem_cma_helper.h>
#include <drm/drm_mm.h>
#include <drm/drm_modeset_lock.h>

#include "uapi/drm/vc4_drm.h"

struct drm_device;
struct drm_gem_object;

/* Don't forget to update vc4_bo.c: bo_type_names[] when adding to
 * this.
 */
enum vc4_kernel_bo_type {
	/* Any kernel allocation (gem_create_object hook) before it
	 * gets another type set.
	 */
	VC4_BO_TYPE_KERNEL,
	VC4_BO_TYPE_V3D,
	VC4_BO_TYPE_V3D_SHADER,
	VC4_BO_TYPE_DUMB,
	VC4_BO_TYPE_BIN,
	VC4_BO_TYPE_RCL,
	VC4_BO_TYPE_BCL,
	VC4_BO_TYPE_KERNEL_CACHE,
	VC4_BO_TYPE_COUNT
};

/* Performance monitor object. The perform lifetime is controlled by userspace
 * using perfmon related ioctls. A perfmon can be attached to a submit_cl
 * request, and when this is the case, HW perf counters will be activated just
 * before the submit_cl is submitted to the GPU and disabled when the job is
 * done. This way, only events related to a specific job will be counted.
 */
struct vc4_perfmon {
	/* Tracks the number of users of the perfmon, when this counter reaches
	 * zero the perfmon is destroyed.
	 */
	refcount_t refcnt;

	/* Number of counters activated in this perfmon instance
	 * (should be less than DRM_VC4_MAX_PERF_COUNTERS).
	 */
	u8 ncounters;

	/* Events counted by the HW perf counters. */
	u8 events[DRM_VC4_MAX_PERF_COUNTERS];

	/* Storage for counter values. Counters are incremented by the HW
	 * perf counter values every time the perfmon is attached to a GPU job.
	 * This way, perfmon users don't have to retrieve the results after
	 * each job if they want to track events covering several submissions.
	 * Note that counter values can't be reset, but you can fake a reset by
	 * destroying the perfmon and creating a new one.
	 */
	u64 counters[];
};

struct vc4_dev {
	struct drm_device *dev;

	struct vc4_hvs *hvs;
	struct vc4_v3d *v3d;
	struct vc4_dpi *dpi;
	struct vc4_dsi *dsi1;
	struct vc4_vec *vec;
	struct vc4_txp *txp;

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

	u32 num_labels;
	struct vc4_label {
		const char *name;
		u32 num_allocated;
		u32 size_allocated;
	} *bo_labels;

	/* Protects bo_cache and bo_labels. */
	struct mutex bo_lock;

	/* Purgeable BO pool. All BOs in this pool can have their memory
	 * reclaimed if the driver is unable to allocate new BOs. We also
	 * keep stats related to the purge mechanism here.
	 */
	struct {
		struct list_head list;
		unsigned int num;
		size_t size;
		unsigned int purged_num;
		size_t purged_size;
		struct mutex lock;
	} purgeable;

	uint64_t dma_fence_context;

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

	/* Used to track the active perfmon if any. Access to this field is
	 * protected by job_lock.
	 */
	struct vc4_perfmon *active_perfmon;

	/* List of struct vc4_seqno_cb for callbacks to be made from a
	 * workqueue when the given seqno is passed.
	 */
	struct list_head seqno_cb_list;

	/* The memory used for storing binner tile alloc, tile state,
	 * and overflow memory allocations.  This is freed when V3D
	 * powers down.
	 */
	struct vc4_bo *bin_bo;

	/* Size of blocks allocated within bin_bo. */
	uint32_t bin_alloc_size;

	/* Bitmask of the bin_alloc_size chunks in bin_bo that are
	 * used.
	 */
	uint32_t bin_alloc_used;

	/* Bitmask of the current bin_alloc used for overflow memory. */
	uint32_t bin_alloc_overflow;

	/* Incremented when an underrun error happened after an atomic commit.
	 * This is particularly useful to detect when a specific modeset is too
	 * demanding in term of memory or HVS bandwidth which is hard to guess
	 * at atomic check time.
	 */
	atomic_t underrun;

	struct work_struct overflow_mem_work;

	int power_refcount;

	/* Set to true when the load tracker is supported. */
	bool load_tracker_available;

	/* Set to true when the load tracker is active. */
	bool load_tracker_enabled;

	/* Mutex controlling the power refcount. */
	struct mutex power_lock;

	struct {
		struct timer_list timer;
		struct work_struct reset_work;
	} hangcheck;

	struct semaphore async_modeset;

	struct drm_modeset_lock ctm_state_lock;
	struct drm_private_obj ctm_manager;
	struct drm_private_obj load_tracker;

	/* List of vc4_debugfs_info_entry for adding to debugfs once
	 * the minor is available (after drm_dev_register()).
	 */
	struct list_head debugfs_list;

	/* Mutex for binner bo allocation. */
	struct mutex bin_bo_lock;
	/* Reference count for our binner bo. */
	struct kref bin_bo_kref;
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

	bool t_format;

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

	/* One of enum vc4_kernel_bo_type, or VC4_BO_TYPE_COUNT + i
	 * for user-allocated labels.
	 */
	int label;

	/* Count the number of active users. This is needed to determine
	 * whether we can move the BO to the purgeable list or not (when the BO
	 * is used by the GPU or the display engine we can't purge it).
	 */
	refcount_t usecnt;

	/* Store purgeable/purged state here */
	u32 madv;
	struct mutex madv_lock;
};

static inline struct vc4_bo *
to_vc4_bo(struct drm_gem_object *bo)
{
	return (struct vc4_bo *)bo;
}

struct vc4_fence {
	struct dma_fence base;
	struct drm_device *dev;
	/* vc4 seqno for signaled() test */
	uint64_t seqno;
};

static inline struct vc4_fence *
to_vc4_fence(struct dma_fence *fence)
{
	return (struct vc4_fence *)fence;
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
	struct clk *clk;
	struct debugfs_regset32 regset;
};

struct vc4_hvs {
	struct platform_device *pdev;
	void __iomem *regs;
	u32 __iomem *dlist;

	struct clk *core_clk;

	/* Memory manager for CRTCs to allocate space in the display
	 * list.  Units are dwords.
	 */
	struct drm_mm dlist_mm;
	/* Memory manager for the LBM memory used by HVS scaling. */
	struct drm_mm lbm_mm;
	spinlock_t mm_lock;

	struct drm_mm_node mitchell_netravali_filter;

	struct debugfs_regset32 regset;

	/* HVS version 5 flag, therefore requires updated dlist structures */
	bool hvs5;
};

struct vc4_plane {
	struct drm_plane base;
};

static inline struct vc4_plane *
to_vc4_plane(struct drm_plane *plane)
{
	return (struct vc4_plane *)plane;
}

enum vc4_scaling_mode {
	VC4_SCALING_NONE,
	VC4_SCALING_TPZ,
	VC4_SCALING_PPF,
};

struct vc4_plane_state {
	struct drm_plane_state base;
	/* System memory copy of the display list for this element, computed
	 * at atomic_check time.
	 */
	u32 *dlist;
	u32 dlist_size; /* Number of dwords allocated for the display list */
	u32 dlist_count; /* Number of used dwords in the display list. */

	/* Offset in the dlist to various words, for pageflip or
	 * cursor updates.
	 */
	u32 pos0_offset;
	u32 pos2_offset;
	u32 ptr0_offset;
	u32 lbm_offset;

	/* Offset where the plane's dlist was last stored in the
	 * hardware at vc4_crtc_atomic_flush() time.
	 */
	u32 __iomem *hw_dlist;

	/* Clipped coordinates of the plane on the display. */
	int crtc_x, crtc_y, crtc_w, crtc_h;
	/* Clipped area being scanned from in the FB. */
	u32 src_x, src_y;

	u32 src_w[2], src_h[2];

	/* Scaling selection for the RGB/Y plane and the Cb/Cr planes. */
	enum vc4_scaling_mode x_scaling[2], y_scaling[2];
	bool is_unity;
	bool is_yuv;

	/* Offset to start scanning out from the start of the plane's
	 * BO.
	 */
	u32 offsets[3];

	/* Our allocation in LBM for temporary storage during scaling. */
	struct drm_mm_node lbm;

	/* Set when the plane has per-pixel alpha content or does not cover
	 * the entire screen. This is a hint to the CRTC that it might need
	 * to enable background color fill.
	 */
	bool needs_bg_fill;

	/* Mark the dlist as initialized. Useful to avoid initializing it twice
	 * when async update is not possible.
	 */
	bool dlist_initialized;

	/* Load of this plane on the HVS block. The load is expressed in HVS
	 * cycles/sec.
	 */
	u64 hvs_load;

	/* Memory bandwidth needed for this plane. This is expressed in
	 * bytes/sec.
	 */
	u64 membus_load;
};

static inline struct vc4_plane_state *
to_vc4_plane_state(struct drm_plane_state *state)
{
	return (struct vc4_plane_state *)state;
}

enum vc4_encoder_type {
	VC4_ENCODER_TYPE_NONE,
	VC4_ENCODER_TYPE_HDMI0,
	VC4_ENCODER_TYPE_HDMI1,
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

	void (*pre_crtc_configure)(struct drm_encoder *encoder);
	void (*pre_crtc_enable)(struct drm_encoder *encoder);
	void (*post_crtc_enable)(struct drm_encoder *encoder);

	void (*post_crtc_disable)(struct drm_encoder *encoder);
	void (*post_crtc_powerdown)(struct drm_encoder *encoder);
};

static inline struct vc4_encoder *
to_vc4_encoder(struct drm_encoder *encoder)
{
	return container_of(encoder, struct vc4_encoder, base);
}

struct vc4_crtc_data {
	/* Bitmask of channels (FIFOs) of the HVS that the output can source from */
	unsigned int hvs_available_channels;

	/* Which output of the HVS this pixelvalve sources from. */
	int hvs_output;
};

struct vc4_pv_data {
	struct vc4_crtc_data	base;

	/* Depth of the PixelValve FIFO in bytes */
	unsigned int fifo_depth;

	/* Number of pixels output per clock period */
	u8 pixels_per_clock;

	enum vc4_encoder_type encoder_types[4];
	const char *debugfs_name;

};

struct vc4_crtc {
	struct drm_crtc base;
	struct platform_device *pdev;
	const struct vc4_crtc_data *data;
	void __iomem *regs;

	/* Timestamp at start of vblank irq - unaffected by lock delays. */
	ktime_t t_vblank;

	u8 lut_r[256];
	u8 lut_g[256];
	u8 lut_b[256];

	struct drm_pending_vblank_event *event;

	struct debugfs_regset32 regset;
};

static inline struct vc4_crtc *
to_vc4_crtc(struct drm_crtc *crtc)
{
	return (struct vc4_crtc *)crtc;
}

static inline const struct vc4_crtc_data *
vc4_crtc_to_vc4_crtc_data(const struct vc4_crtc *crtc)
{
	return crtc->data;
}

static inline const struct vc4_pv_data *
vc4_crtc_to_vc4_pv_data(const struct vc4_crtc *crtc)
{
	const struct vc4_crtc_data *data = vc4_crtc_to_vc4_crtc_data(crtc);

	return container_of(data, struct vc4_pv_data, base);
}

struct vc4_crtc_state {
	struct drm_crtc_state base;
	/* Dlist area for this CRTC configuration. */
	struct drm_mm_node mm;
	bool feed_txp;
	bool txp_armed;
	unsigned int assigned_channel;

	struct {
		unsigned int left;
		unsigned int right;
		unsigned int top;
		unsigned int bottom;
	} margins;
};

#define VC4_HVS_CHANNEL_DISABLED ((unsigned int)-1)

static inline struct vc4_crtc_state *
to_vc4_crtc_state(struct drm_crtc_state *crtc_state)
{
	return (struct vc4_crtc_state *)crtc_state;
}

#define V3D_READ(offset) readl(vc4->v3d->regs + offset)
#define V3D_WRITE(offset, val) writel(val, vc4->v3d->regs + offset)
#define HVS_READ(offset) readl(vc4->hvs->regs + offset)
#define HVS_WRITE(offset, val) writel(val, vc4->hvs->regs + offset)

#define VC4_REG32(reg) { .name = #reg, .offset = reg }

struct vc4_exec_info {
	/* Sequence number for this bin/render job. */
	uint64_t seqno;

	/* Latest write_seqno of any BO that binning depends on. */
	uint64_t bin_dep_seqno;

	struct dma_fence *fence;

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
	/* Physical address of the start of the tile alloc array
	 * (where each tile's binned CL will start)
	 */
	uint32_t tile_alloc_offset;
	/* Bitmask of which binner slots are freed when this job completes. */
	uint32_t bin_slots;

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

	/* Pointer to a performance monitor object if the user requested it,
	 * NULL otherwise.
	 */
	struct vc4_perfmon *perfmon;

	/* Whether the exec has taken a reference to the binner BO, which should
	 * happen with a VC4_PACKET_TILE_BINNING_MODE_CONFIG packet.
	 */
	bool bin_bo_used;
};

/* Per-open file private data. Any driver-specific resource that has to be
 * released when the DRM file is closed should be placed here.
 */
struct vc4_file {
	struct {
		struct idr idr;
		struct mutex lock;
	} perfmon;

	bool bin_bo_used;
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
 * __wait_for - magic wait macro
 *
 * Macro to help avoid open coding check/wait/timeout patterns. Note that it's
 * important that we check the condition again after having timed out, since the
 * timeout could be due to preemption or similar and we've never had a chance to
 * check the condition before the timeout.
 */
#define __wait_for(OP, COND, US, Wmin, Wmax) ({ \
	const ktime_t end__ = ktime_add_ns(ktime_get_raw(), 1000ll * (US)); \
	long wait__ = (Wmin); /* recommended min for usleep is 10 us */	\
	int ret__;							\
	might_sleep();							\
	for (;;) {							\
		const bool expired__ = ktime_after(ktime_get_raw(), end__); \
		OP;							\
		/* Guarantee COND check prior to timeout */		\
		barrier();						\
		if (COND) {						\
			ret__ = 0;					\
			break;						\
		}							\
		if (expired__) {					\
			ret__ = -ETIMEDOUT;				\
			break;						\
		}							\
		usleep_range(wait__, wait__ * 2);			\
		if (wait__ < (Wmax))					\
			wait__ <<= 1;					\
	}								\
	ret__;								\
})

#define _wait_for(COND, US, Wmin, Wmax)	__wait_for(, (COND), (US), (Wmin), \
						   (Wmax))
#define wait_for(COND, MS)		_wait_for((COND), (MS) * 1000, 10, 1000)

/* vc4_bo.c */
struct drm_gem_object *vc4_create_object(struct drm_device *dev, size_t size);
void vc4_free_object(struct drm_gem_object *gem_obj);
struct vc4_bo *vc4_bo_create(struct drm_device *dev, size_t size,
			     bool from_cache, enum vc4_kernel_bo_type type);
int vc4_dumb_create(struct drm_file *file_priv,
		    struct drm_device *dev,
		    struct drm_mode_create_dumb *args);
struct dma_buf *vc4_prime_export(struct drm_gem_object *obj, int flags);
int vc4_create_bo_ioctl(struct drm_device *dev, void *data,
			struct drm_file *file_priv);
int vc4_create_shader_bo_ioctl(struct drm_device *dev, void *data,
			       struct drm_file *file_priv);
int vc4_mmap_bo_ioctl(struct drm_device *dev, void *data,
		      struct drm_file *file_priv);
int vc4_set_tiling_ioctl(struct drm_device *dev, void *data,
			 struct drm_file *file_priv);
int vc4_get_tiling_ioctl(struct drm_device *dev, void *data,
			 struct drm_file *file_priv);
int vc4_get_hang_state_ioctl(struct drm_device *dev, void *data,
			     struct drm_file *file_priv);
int vc4_label_bo_ioctl(struct drm_device *dev, void *data,
		       struct drm_file *file_priv);
vm_fault_t vc4_fault(struct vm_fault *vmf);
int vc4_mmap(struct file *filp, struct vm_area_struct *vma);
int vc4_prime_mmap(struct drm_gem_object *obj, struct vm_area_struct *vma);
struct drm_gem_object *vc4_prime_import_sg_table(struct drm_device *dev,
						 struct dma_buf_attachment *attach,
						 struct sg_table *sgt);
void *vc4_prime_vmap(struct drm_gem_object *obj);
int vc4_bo_cache_init(struct drm_device *dev);
void vc4_bo_cache_destroy(struct drm_device *dev);
int vc4_bo_inc_usecnt(struct vc4_bo *bo);
void vc4_bo_dec_usecnt(struct vc4_bo *bo);
void vc4_bo_add_to_purgeable_pool(struct vc4_bo *bo);
void vc4_bo_remove_from_purgeable_pool(struct vc4_bo *bo);

/* vc4_crtc.c */
extern struct platform_driver vc4_crtc_driver;
int vc4_crtc_disable_at_boot(struct drm_crtc *crtc);
int vc4_crtc_init(struct drm_device *drm, struct vc4_crtc *vc4_crtc,
		  const struct drm_crtc_funcs *crtc_funcs,
		  const struct drm_crtc_helper_funcs *crtc_helper_funcs);
void vc4_crtc_destroy(struct drm_crtc *crtc);
int vc4_page_flip(struct drm_crtc *crtc,
		  struct drm_framebuffer *fb,
		  struct drm_pending_vblank_event *event,
		  uint32_t flags,
		  struct drm_modeset_acquire_ctx *ctx);
struct drm_crtc_state *vc4_crtc_duplicate_state(struct drm_crtc *crtc);
void vc4_crtc_destroy_state(struct drm_crtc *crtc,
			    struct drm_crtc_state *state);
void vc4_crtc_reset(struct drm_crtc *crtc);
void vc4_crtc_handle_vblank(struct vc4_crtc *crtc);
void vc4_crtc_get_margins(struct drm_crtc_state *state,
			  unsigned int *right, unsigned int *left,
			  unsigned int *top, unsigned int *bottom);

/* vc4_debugfs.c */
void vc4_debugfs_init(struct drm_minor *minor);
#ifdef CONFIG_DEBUG_FS
void vc4_debugfs_add_file(struct drm_device *drm,
			  const char *filename,
			  int (*show)(struct seq_file*, void*),
			  void *data);
void vc4_debugfs_add_regset32(struct drm_device *drm,
			      const char *filename,
			      struct debugfs_regset32 *regset);
#else
static inline void vc4_debugfs_add_file(struct drm_device *drm,
					const char *filename,
					int (*show)(struct seq_file*, void*),
					void *data)
{
}

static inline void vc4_debugfs_add_regset32(struct drm_device *drm,
					    const char *filename,
					    struct debugfs_regset32 *regset)
{
}
#endif

/* vc4_drv.c */
void __iomem *vc4_ioremap_regs(struct platform_device *dev, int index);

/* vc4_dpi.c */
extern struct platform_driver vc4_dpi_driver;

/* vc4_dsi.c */
extern struct platform_driver vc4_dsi_driver;

/* vc4_fence.c */
extern const struct dma_fence_ops vc4_fence_ops;

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
int vc4_gem_madvise_ioctl(struct drm_device *dev, void *data,
			  struct drm_file *file_priv);

/* vc4_hdmi.c */
extern struct platform_driver vc4_hdmi_driver;

/* vc4_vec.c */
extern struct platform_driver vc4_vec_driver;

/* vc4_txp.c */
extern struct platform_driver vc4_txp_driver;

/* vc4_irq.c */
irqreturn_t vc4_irq(int irq, void *arg);
void vc4_irq_preinstall(struct drm_device *dev);
int vc4_irq_postinstall(struct drm_device *dev);
void vc4_irq_uninstall(struct drm_device *dev);
void vc4_irq_reset(struct drm_device *dev);

/* vc4_hvs.c */
extern struct platform_driver vc4_hvs_driver;
void vc4_hvs_stop_channel(struct drm_device *dev, unsigned int output);
int vc4_hvs_get_fifo_from_output(struct drm_device *dev, unsigned int output);
int vc4_hvs_atomic_check(struct drm_crtc *crtc, struct drm_crtc_state *state);
void vc4_hvs_atomic_enable(struct drm_crtc *crtc, struct drm_crtc_state *old_state);
void vc4_hvs_atomic_disable(struct drm_crtc *crtc, struct drm_crtc_state *old_state);
void vc4_hvs_atomic_flush(struct drm_crtc *crtc, struct drm_crtc_state *state);
void vc4_hvs_dump_state(struct drm_device *dev);
void vc4_hvs_unmask_underrun(struct drm_device *dev, int channel);
void vc4_hvs_mask_underrun(struct drm_device *dev, int channel);

/* vc4_kms.c */
int vc4_kms_load(struct drm_device *dev);

/* vc4_plane.c */
struct drm_plane *vc4_plane_init(struct drm_device *dev,
				 enum drm_plane_type type);
int vc4_plane_create_additional_planes(struct drm_device *dev);
u32 vc4_plane_write_dlist(struct drm_plane *plane, u32 __iomem *dlist);
u32 vc4_plane_dlist_size(const struct drm_plane_state *state);
void vc4_plane_async_set_fb(struct drm_plane *plane,
			    struct drm_framebuffer *fb);

/* vc4_v3d.c */
extern struct platform_driver vc4_v3d_driver;
extern const struct of_device_id vc4_v3d_dt_match[];
int vc4_v3d_get_bin_slot(struct vc4_dev *vc4);
int vc4_v3d_bin_bo_get(struct vc4_dev *vc4, bool *used);
void vc4_v3d_bin_bo_put(struct vc4_dev *vc4);
int vc4_v3d_pm_get(struct vc4_dev *vc4);
void vc4_v3d_pm_put(struct vc4_dev *vc4);

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

/* vc4_perfmon.c */
void vc4_perfmon_get(struct vc4_perfmon *perfmon);
void vc4_perfmon_put(struct vc4_perfmon *perfmon);
void vc4_perfmon_start(struct vc4_dev *vc4, struct vc4_perfmon *perfmon);
void vc4_perfmon_stop(struct vc4_dev *vc4, struct vc4_perfmon *perfmon,
		      bool capture);
struct vc4_perfmon *vc4_perfmon_find(struct vc4_file *vc4file, int id);
void vc4_perfmon_open_file(struct vc4_file *vc4file);
void vc4_perfmon_close_file(struct vc4_file *vc4file);
int vc4_perfmon_create_ioctl(struct drm_device *dev, void *data,
			     struct drm_file *file_priv);
int vc4_perfmon_destroy_ioctl(struct drm_device *dev, void *data,
			      struct drm_file *file_priv);
int vc4_perfmon_get_values_ioctl(struct drm_device *dev, void *data,
				 struct drm_file *file_priv);

#endif /* _VC4_DRV_H_ */
