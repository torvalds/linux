/**************************************************************************
 *
 * Copyright © 2009-2015 VMware, Inc., Palo Alto, CA., USA
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

#ifndef _VMWGFX_DRV_H_
#define _VMWGFX_DRV_H_

#include "vmwgfx_reg.h"
#include <drm/drmP.h>
#include <drm/vmwgfx_drm.h>
#include <drm/drm_hashtab.h>
#include <drm/drm_auth.h>
#include <linux/suspend.h>
#include <drm/ttm/ttm_bo_driver.h>
#include <drm/ttm/ttm_object.h>
#include <drm/ttm/ttm_lock.h>
#include <drm/ttm/ttm_execbuf_util.h>
#include <drm/ttm/ttm_module.h>
#include "vmwgfx_fence.h"

#define VMWGFX_DRIVER_DATE "20170221"
#define VMWGFX_DRIVER_MAJOR 2
#define VMWGFX_DRIVER_MINOR 12
#define VMWGFX_DRIVER_PATCHLEVEL 0
#define VMWGFX_FILE_PAGE_OFFSET 0x00100000
#define VMWGFX_FIFO_STATIC_SIZE (1024*1024)
#define VMWGFX_MAX_RELOCATIONS 2048
#define VMWGFX_MAX_VALIDATIONS 2048
#define VMWGFX_MAX_DISPLAYS 16
#define VMWGFX_CMD_BOUNCE_INIT_SIZE 32768
#define VMWGFX_ENABLE_SCREEN_TARGET_OTABLE 1

/*
 * Perhaps we should have sysfs entries for these.
 */
#define VMWGFX_NUM_GB_CONTEXT 256
#define VMWGFX_NUM_GB_SHADER 20000
#define VMWGFX_NUM_GB_SURFACE 32768
#define VMWGFX_NUM_GB_SCREEN_TARGET VMWGFX_MAX_DISPLAYS
#define VMWGFX_NUM_DXCONTEXT 256
#define VMWGFX_NUM_DXQUERY 512
#define VMWGFX_NUM_MOB (VMWGFX_NUM_GB_CONTEXT +\
			VMWGFX_NUM_GB_SHADER +\
			VMWGFX_NUM_GB_SURFACE +\
			VMWGFX_NUM_GB_SCREEN_TARGET)

#define VMW_PL_GMR (TTM_PL_PRIV + 0)
#define VMW_PL_FLAG_GMR (TTM_PL_FLAG_PRIV << 0)
#define VMW_PL_MOB (TTM_PL_PRIV + 1)
#define VMW_PL_FLAG_MOB (TTM_PL_FLAG_PRIV << 1)

#define VMW_RES_CONTEXT ttm_driver_type0
#define VMW_RES_SURFACE ttm_driver_type1
#define VMW_RES_STREAM ttm_driver_type2
#define VMW_RES_FENCE ttm_driver_type3
#define VMW_RES_SHADER ttm_driver_type4

struct vmw_fpriv {
	struct drm_master *locked_master;
	struct ttm_object_file *tfile;
	bool gb_aware;
};

struct vmw_dma_buffer {
	struct ttm_buffer_object base;
	struct list_head res_list;
	s32 pin_count;
	/* Not ref-counted.  Protected by binding_mutex */
	struct vmw_resource *dx_query_ctx;
};

/**
 * struct vmw_validate_buffer - Carries validation info about buffers.
 *
 * @base: Validation info for TTM.
 * @hash: Hash entry for quick lookup of the TTM buffer object.
 *
 * This structure contains also driver private validation info
 * on top of the info needed by TTM.
 */
struct vmw_validate_buffer {
	struct ttm_validate_buffer base;
	struct drm_hash_item hash;
	bool validate_as_mob;
};

struct vmw_res_func;
struct vmw_resource {
	struct kref kref;
	struct vmw_private *dev_priv;
	int id;
	bool avail;
	unsigned long backup_size;
	bool res_dirty; /* Protected by backup buffer reserved */
	bool backup_dirty; /* Protected by backup buffer reserved */
	struct vmw_dma_buffer *backup;
	unsigned long backup_offset;
	unsigned long pin_count; /* Protected by resource reserved */
	const struct vmw_res_func *func;
	struct list_head lru_head; /* Protected by the resource lock */
	struct list_head mob_head; /* Protected by @backup reserved */
	struct list_head binding_head; /* Protected by binding_mutex */
	void (*res_free) (struct vmw_resource *res);
	void (*hw_destroy) (struct vmw_resource *res);
};


/*
 * Resources that are managed using ioctls.
 */
enum vmw_res_type {
	vmw_res_context,
	vmw_res_surface,
	vmw_res_stream,
	vmw_res_shader,
	vmw_res_dx_context,
	vmw_res_cotable,
	vmw_res_view,
	vmw_res_max
};

/*
 * Resources that are managed using command streams.
 */
enum vmw_cmdbuf_res_type {
	vmw_cmdbuf_res_shader,
	vmw_cmdbuf_res_view
};

struct vmw_cmdbuf_res_manager;

struct vmw_cursor_snooper {
	size_t age;
	uint32_t *image;
};

struct vmw_framebuffer;
struct vmw_surface_offset;

struct vmw_surface {
	struct vmw_resource res;
	uint32_t flags;
	uint32_t format;
	uint32_t mip_levels[DRM_VMW_MAX_SURFACE_FACES];
	struct drm_vmw_size base_size;
	struct drm_vmw_size *sizes;
	uint32_t num_sizes;
	bool scanout;
	uint32_t array_size;
	/* TODO so far just a extra pointer */
	struct vmw_cursor_snooper snooper;
	struct vmw_surface_offset *offsets;
	SVGA3dTextureFilter autogen_filter;
	uint32_t multisample_count;
	struct list_head view_list;
};

struct vmw_marker_queue {
	struct list_head head;
	u64 lag;
	u64 lag_time;
	spinlock_t lock;
};

struct vmw_fifo_state {
	unsigned long reserved_size;
	u32 *dynamic_buffer;
	u32 *static_buffer;
	unsigned long static_buffer_size;
	bool using_bounce_buffer;
	uint32_t capabilities;
	struct mutex fifo_mutex;
	struct rw_semaphore rwsem;
	struct vmw_marker_queue marker_queue;
	bool dx;
};

struct vmw_relocation {
	SVGAMobId *mob_loc;
	SVGAGuestPtr *location;
	uint32_t index;
};

/**
 * struct vmw_res_cache_entry - resource information cache entry
 *
 * @valid: Whether the entry is valid, which also implies that the execbuf
 * code holds a reference to the resource, and it's placed on the
 * validation list.
 * @handle: User-space handle of a resource.
 * @res: Non-ref-counted pointer to the resource.
 *
 * Used to avoid frequent repeated user-space handle lookups of the
 * same resource.
 */
struct vmw_res_cache_entry {
	bool valid;
	uint32_t handle;
	struct vmw_resource *res;
	struct vmw_resource_val_node *node;
};

/**
 * enum vmw_dma_map_mode - indicate how to perform TTM page dma mappings.
 */
enum vmw_dma_map_mode {
	vmw_dma_phys,           /* Use physical page addresses */
	vmw_dma_alloc_coherent, /* Use TTM coherent pages */
	vmw_dma_map_populate,   /* Unmap from DMA just after unpopulate */
	vmw_dma_map_bind,       /* Unmap from DMA just before unbind */
	vmw_dma_map_max
};

/**
 * struct vmw_sg_table - Scatter/gather table for binding, with additional
 * device-specific information.
 *
 * @sgt: Pointer to a struct sg_table with binding information
 * @num_regions: Number of regions with device-address contiguous pages
 */
struct vmw_sg_table {
	enum vmw_dma_map_mode mode;
	struct page **pages;
	const dma_addr_t *addrs;
	struct sg_table *sgt;
	unsigned long num_regions;
	unsigned long num_pages;
};

/**
 * struct vmw_piter - Page iterator that iterates over a list of pages
 * and DMA addresses that could be either a scatter-gather list or
 * arrays
 *
 * @pages: Array of page pointers to the pages.
 * @addrs: DMA addresses to the pages if coherent pages are used.
 * @iter: Scatter-gather page iterator. Current position in SG list.
 * @i: Current position in arrays.
 * @num_pages: Number of pages total.
 * @next: Function to advance the iterator. Returns false if past the list
 * of pages, true otherwise.
 * @dma_address: Function to return the DMA address of the current page.
 */
struct vmw_piter {
	struct page **pages;
	const dma_addr_t *addrs;
	struct sg_page_iter iter;
	unsigned long i;
	unsigned long num_pages;
	bool (*next)(struct vmw_piter *);
	dma_addr_t (*dma_address)(struct vmw_piter *);
	struct page *(*page)(struct vmw_piter *);
};

/*
 * enum vmw_display_unit_type - Describes the display unit
 */
enum vmw_display_unit_type {
	vmw_du_invalid = 0,
	vmw_du_legacy,
	vmw_du_screen_object,
	vmw_du_screen_target
};


struct vmw_sw_context{
	struct drm_open_hash res_ht;
	bool res_ht_initialized;
	bool kernel; /**< is the called made from the kernel */
	struct vmw_fpriv *fp;
	struct list_head validate_nodes;
	struct vmw_relocation relocs[VMWGFX_MAX_RELOCATIONS];
	uint32_t cur_reloc;
	struct vmw_validate_buffer val_bufs[VMWGFX_MAX_VALIDATIONS];
	uint32_t cur_val_buf;
	uint32_t *cmd_bounce;
	uint32_t cmd_bounce_size;
	struct list_head resource_list;
	struct list_head ctx_resource_list; /* For contexts and cotables */
	struct vmw_dma_buffer *cur_query_bo;
	struct list_head res_relocations;
	uint32_t *buf_start;
	struct vmw_res_cache_entry res_cache[vmw_res_max];
	struct vmw_resource *last_query_ctx;
	bool needs_post_query_barrier;
	struct vmw_resource *error_resource;
	struct vmw_ctx_binding_state *staged_bindings;
	bool staged_bindings_inuse;
	struct list_head staged_cmd_res;
	struct vmw_resource_val_node *dx_ctx_node;
	struct vmw_dma_buffer *dx_query_mob;
	struct vmw_resource *dx_query_ctx;
	struct vmw_cmdbuf_res_manager *man;
};

struct vmw_legacy_display;
struct vmw_overlay;

struct vmw_master {
	struct ttm_lock lock;
};

struct vmw_vga_topology_state {
	uint32_t width;
	uint32_t height;
	uint32_t primary;
	uint32_t pos_x;
	uint32_t pos_y;
};


/*
 * struct vmw_otable - Guest Memory OBject table metadata
 *
 * @size:           Size of the table (page-aligned).
 * @page_table:     Pointer to a struct vmw_mob holding the page table.
 */
struct vmw_otable {
	unsigned long size;
	struct vmw_mob *page_table;
	bool enabled;
};

struct vmw_otable_batch {
	unsigned num_otables;
	struct vmw_otable *otables;
	struct vmw_resource *context;
	struct ttm_buffer_object *otable_bo;
};

struct vmw_private {
	struct ttm_bo_device bdev;
	struct ttm_bo_global_ref bo_global_ref;
	struct drm_global_reference mem_global_ref;

	struct vmw_fifo_state fifo;

	struct drm_device *dev;
	unsigned long vmw_chipset;
	unsigned int io_start;
	uint32_t vram_start;
	uint32_t vram_size;
	uint32_t prim_bb_mem;
	uint32_t mmio_start;
	uint32_t mmio_size;
	uint32_t fb_max_width;
	uint32_t fb_max_height;
	uint32_t texture_max_width;
	uint32_t texture_max_height;
	uint32_t stdu_max_width;
	uint32_t stdu_max_height;
	uint32_t initial_width;
	uint32_t initial_height;
	u32 *mmio_virt;
	uint32_t capabilities;
	uint32_t max_gmr_ids;
	uint32_t max_gmr_pages;
	uint32_t max_mob_pages;
	uint32_t max_mob_size;
	uint32_t memory_size;
	bool has_gmr;
	bool has_mob;
	spinlock_t hw_lock;
	spinlock_t cap_lock;
	bool has_dx;
	bool assume_16bpp;

	/*
	 * VGA registers.
	 */

	struct vmw_vga_topology_state vga_save[VMWGFX_MAX_DISPLAYS];
	uint32_t vga_width;
	uint32_t vga_height;
	uint32_t vga_bpp;
	uint32_t vga_bpl;
	uint32_t vga_pitchlock;

	uint32_t num_displays;

	/*
	 * Framebuffer info.
	 */

	void *fb_info;
	enum vmw_display_unit_type active_display_unit;
	struct vmw_legacy_display *ldu_priv;
	struct vmw_overlay *overlay_priv;
	struct drm_property *hotplug_mode_update_property;
	struct drm_property *implicit_placement_property;
	unsigned num_implicit;
	struct vmw_framebuffer *implicit_fb;
	struct mutex global_kms_state_mutex;
	spinlock_t cursor_lock;

	/*
	 * Context and surface management.
	 */

	rwlock_t resource_lock;
	struct idr res_idr[vmw_res_max];
	/*
	 * Block lastclose from racing with firstopen.
	 */

	struct mutex init_mutex;

	/*
	 * A resource manager for kernel-only surfaces and
	 * contexts.
	 */

	struct ttm_object_device *tdev;

	/*
	 * Fencing and IRQs.
	 */

	atomic_t marker_seq;
	wait_queue_head_t fence_queue;
	wait_queue_head_t fifo_queue;
	spinlock_t waiter_lock;
	int fence_queue_waiters; /* Protected by waiter_lock */
	int goal_queue_waiters; /* Protected by waiter_lock */
	int cmdbuf_waiters; /* Protected by waiter_lock */
	int error_waiters; /* Protected by waiter_lock */
	int fifo_queue_waiters; /* Protected by waiter_lock */
	uint32_t last_read_seqno;
	struct vmw_fence_manager *fman;
	uint32_t irq_mask; /* Updates protected by waiter_lock */

	/*
	 * Device state
	 */

	uint32_t traces_state;
	uint32_t enable_state;
	uint32_t config_done_state;

	/**
	 * Execbuf
	 */
	/**
	 * Protected by the cmdbuf mutex.
	 */

	struct vmw_sw_context ctx;
	struct mutex cmdbuf_mutex;
	struct mutex binding_mutex;

	/**
	 * Operating mode.
	 */

	bool stealth;
	bool enable_fb;
	spinlock_t svga_lock;

	/**
	 * Master management.
	 */

	struct vmw_master *active_master;
	struct vmw_master fbdev_master;
	struct notifier_block pm_nb;
	bool suspended;
	bool refuse_hibernation;

	struct mutex release_mutex;
	atomic_t num_fifo_resources;

	/*
	 * Replace this with an rwsem as soon as we have down_xx_interruptible()
	 */
	struct ttm_lock reservation_sem;

	/*
	 * Query processing. These members
	 * are protected by the cmdbuf mutex.
	 */

	struct vmw_dma_buffer *dummy_query_bo;
	struct vmw_dma_buffer *pinned_bo;
	uint32_t query_cid;
	uint32_t query_cid_valid;
	bool dummy_query_bo_pinned;

	/*
	 * Surface swapping. The "surface_lru" list is protected by the
	 * resource lock in order to be able to destroy a surface and take
	 * it off the lru atomically. "used_memory_size" is currently
	 * protected by the cmdbuf mutex for simplicity.
	 */

	struct list_head res_lru[vmw_res_max];
	uint32_t used_memory_size;

	/*
	 * DMA mapping stuff.
	 */
	enum vmw_dma_map_mode map_mode;

	/*
	 * Guest Backed stuff
	 */
	struct vmw_otable_batch otable_batch;

	struct vmw_cmdbuf_man *cman;
};

static inline struct vmw_surface *vmw_res_to_srf(struct vmw_resource *res)
{
	return container_of(res, struct vmw_surface, res);
}

static inline struct vmw_private *vmw_priv(struct drm_device *dev)
{
	return (struct vmw_private *)dev->dev_private;
}

static inline struct vmw_fpriv *vmw_fpriv(struct drm_file *file_priv)
{
	return (struct vmw_fpriv *)file_priv->driver_priv;
}

static inline struct vmw_master *vmw_master(struct drm_master *master)
{
	return (struct vmw_master *) master->driver_priv;
}

/*
 * The locking here is fine-grained, so that it is performed once
 * for every read- and write operation. This is of course costly, but we
 * don't perform much register access in the timing critical paths anyway.
 * Instead we have the extra benefit of being sure that we don't forget
 * the hw lock around register accesses.
 */
static inline void vmw_write(struct vmw_private *dev_priv,
			     unsigned int offset, uint32_t value)
{
	unsigned long irq_flags;

	spin_lock_irqsave(&dev_priv->hw_lock, irq_flags);
	outl(offset, dev_priv->io_start + VMWGFX_INDEX_PORT);
	outl(value, dev_priv->io_start + VMWGFX_VALUE_PORT);
	spin_unlock_irqrestore(&dev_priv->hw_lock, irq_flags);
}

static inline uint32_t vmw_read(struct vmw_private *dev_priv,
				unsigned int offset)
{
	unsigned long irq_flags;
	u32 val;

	spin_lock_irqsave(&dev_priv->hw_lock, irq_flags);
	outl(offset, dev_priv->io_start + VMWGFX_INDEX_PORT);
	val = inl(dev_priv->io_start + VMWGFX_VALUE_PORT);
	spin_unlock_irqrestore(&dev_priv->hw_lock, irq_flags);

	return val;
}

extern void vmw_svga_enable(struct vmw_private *dev_priv);
extern void vmw_svga_disable(struct vmw_private *dev_priv);


/**
 * GMR utilities - vmwgfx_gmr.c
 */

extern int vmw_gmr_bind(struct vmw_private *dev_priv,
			const struct vmw_sg_table *vsgt,
			unsigned long num_pages,
			int gmr_id);
extern void vmw_gmr_unbind(struct vmw_private *dev_priv, int gmr_id);

/**
 * Resource utilities - vmwgfx_resource.c
 */
struct vmw_user_resource_conv;

extern void vmw_resource_unreference(struct vmw_resource **p_res);
extern struct vmw_resource *vmw_resource_reference(struct vmw_resource *res);
extern struct vmw_resource *
vmw_resource_reference_unless_doomed(struct vmw_resource *res);
extern int vmw_resource_validate(struct vmw_resource *res);
extern int vmw_resource_reserve(struct vmw_resource *res, bool interruptible,
				bool no_backup);
extern bool vmw_resource_needs_backup(const struct vmw_resource *res);
extern int vmw_user_lookup_handle(struct vmw_private *dev_priv,
				  struct ttm_object_file *tfile,
				  uint32_t handle,
				  struct vmw_surface **out_surf,
				  struct vmw_dma_buffer **out_buf);
extern int vmw_user_resource_lookup_handle(
	struct vmw_private *dev_priv,
	struct ttm_object_file *tfile,
	uint32_t handle,
	const struct vmw_user_resource_conv *converter,
	struct vmw_resource **p_res);
extern void vmw_dmabuf_bo_free(struct ttm_buffer_object *bo);
extern int vmw_dmabuf_init(struct vmw_private *dev_priv,
			   struct vmw_dma_buffer *vmw_bo,
			   size_t size, struct ttm_placement *placement,
			   bool interuptable,
			   void (*bo_free) (struct ttm_buffer_object *bo));
extern int vmw_user_dmabuf_verify_access(struct ttm_buffer_object *bo,
				  struct ttm_object_file *tfile);
extern int vmw_user_dmabuf_alloc(struct vmw_private *dev_priv,
				 struct ttm_object_file *tfile,
				 uint32_t size,
				 bool shareable,
				 uint32_t *handle,
				 struct vmw_dma_buffer **p_dma_buf,
				 struct ttm_base_object **p_base);
extern int vmw_user_dmabuf_reference(struct ttm_object_file *tfile,
				     struct vmw_dma_buffer *dma_buf,
				     uint32_t *handle);
extern int vmw_dmabuf_alloc_ioctl(struct drm_device *dev, void *data,
				  struct drm_file *file_priv);
extern int vmw_dmabuf_unref_ioctl(struct drm_device *dev, void *data,
				  struct drm_file *file_priv);
extern int vmw_user_dmabuf_synccpu_ioctl(struct drm_device *dev, void *data,
					 struct drm_file *file_priv);
extern uint32_t vmw_dmabuf_validate_node(struct ttm_buffer_object *bo,
					 uint32_t cur_validate_node);
extern void vmw_dmabuf_validate_clear(struct ttm_buffer_object *bo);
extern int vmw_user_dmabuf_lookup(struct ttm_object_file *tfile,
				  uint32_t id, struct vmw_dma_buffer **out,
				  struct ttm_base_object **base);
extern int vmw_stream_claim_ioctl(struct drm_device *dev, void *data,
				  struct drm_file *file_priv);
extern int vmw_stream_unref_ioctl(struct drm_device *dev, void *data,
				  struct drm_file *file_priv);
extern int vmw_user_stream_lookup(struct vmw_private *dev_priv,
				  struct ttm_object_file *tfile,
				  uint32_t *inout_id,
				  struct vmw_resource **out);
extern void vmw_resource_unreserve(struct vmw_resource *res,
				   bool switch_backup,
				   struct vmw_dma_buffer *new_backup,
				   unsigned long new_backup_offset);
extern void vmw_resource_move_notify(struct ttm_buffer_object *bo,
				     struct ttm_mem_reg *mem);
extern void vmw_query_move_notify(struct ttm_buffer_object *bo,
				  struct ttm_mem_reg *mem);
extern int vmw_query_readback_all(struct vmw_dma_buffer *dx_query_mob);
extern void vmw_fence_single_bo(struct ttm_buffer_object *bo,
				struct vmw_fence_obj *fence);
extern void vmw_resource_evict_all(struct vmw_private *dev_priv);

/**
 * DMA buffer helper routines - vmwgfx_dmabuf.c
 */
extern int vmw_dmabuf_pin_in_placement(struct vmw_private *vmw_priv,
				       struct vmw_dma_buffer *bo,
				       struct ttm_placement *placement,
				       bool interruptible);
extern int vmw_dmabuf_pin_in_vram(struct vmw_private *dev_priv,
				  struct vmw_dma_buffer *buf,
				  bool interruptible);
extern int vmw_dmabuf_pin_in_vram_or_gmr(struct vmw_private *dev_priv,
					 struct vmw_dma_buffer *buf,
					 bool interruptible);
extern int vmw_dmabuf_pin_in_start_of_vram(struct vmw_private *vmw_priv,
					   struct vmw_dma_buffer *bo,
					   bool interruptible);
extern int vmw_dmabuf_unpin(struct vmw_private *vmw_priv,
			    struct vmw_dma_buffer *bo,
			    bool interruptible);
extern void vmw_bo_get_guest_ptr(const struct ttm_buffer_object *buf,
				 SVGAGuestPtr *ptr);
extern void vmw_bo_pin_reserved(struct vmw_dma_buffer *bo, bool pin);

/**
 * Misc Ioctl functionality - vmwgfx_ioctl.c
 */

extern int vmw_getparam_ioctl(struct drm_device *dev, void *data,
			      struct drm_file *file_priv);
extern int vmw_get_cap_3d_ioctl(struct drm_device *dev, void *data,
				struct drm_file *file_priv);
extern int vmw_present_ioctl(struct drm_device *dev, void *data,
			     struct drm_file *file_priv);
extern int vmw_present_readback_ioctl(struct drm_device *dev, void *data,
				      struct drm_file *file_priv);
extern unsigned int vmw_fops_poll(struct file *filp,
				  struct poll_table_struct *wait);
extern ssize_t vmw_fops_read(struct file *filp, char __user *buffer,
			     size_t count, loff_t *offset);

/**
 * Fifo utilities - vmwgfx_fifo.c
 */

extern int vmw_fifo_init(struct vmw_private *dev_priv,
			 struct vmw_fifo_state *fifo);
extern void vmw_fifo_release(struct vmw_private *dev_priv,
			     struct vmw_fifo_state *fifo);
extern void *vmw_fifo_reserve(struct vmw_private *dev_priv, uint32_t bytes);
extern void *
vmw_fifo_reserve_dx(struct vmw_private *dev_priv, uint32_t bytes, int ctx_id);
extern void vmw_fifo_commit(struct vmw_private *dev_priv, uint32_t bytes);
extern void vmw_fifo_commit_flush(struct vmw_private *dev_priv, uint32_t bytes);
extern int vmw_fifo_send_fence(struct vmw_private *dev_priv,
			       uint32_t *seqno);
extern void vmw_fifo_ping_host_locked(struct vmw_private *, uint32_t reason);
extern void vmw_fifo_ping_host(struct vmw_private *dev_priv, uint32_t reason);
extern bool vmw_fifo_have_3d(struct vmw_private *dev_priv);
extern bool vmw_fifo_have_pitchlock(struct vmw_private *dev_priv);
extern int vmw_fifo_emit_dummy_query(struct vmw_private *dev_priv,
				     uint32_t cid);
extern int vmw_fifo_flush(struct vmw_private *dev_priv,
			  bool interruptible);

/**
 * TTM glue - vmwgfx_ttm_glue.c
 */

extern int vmw_ttm_global_init(struct vmw_private *dev_priv);
extern void vmw_ttm_global_release(struct vmw_private *dev_priv);
extern int vmw_mmap(struct file *filp, struct vm_area_struct *vma);

/**
 * TTM buffer object driver - vmwgfx_buffer.c
 */

extern const size_t vmw_tt_size;
extern struct ttm_placement vmw_vram_placement;
extern struct ttm_placement vmw_vram_ne_placement;
extern struct ttm_placement vmw_vram_sys_placement;
extern struct ttm_placement vmw_vram_gmr_placement;
extern struct ttm_placement vmw_vram_gmr_ne_placement;
extern struct ttm_placement vmw_sys_placement;
extern struct ttm_placement vmw_sys_ne_placement;
extern struct ttm_placement vmw_evictable_placement;
extern struct ttm_placement vmw_srf_placement;
extern struct ttm_placement vmw_mob_placement;
extern struct ttm_placement vmw_mob_ne_placement;
extern struct ttm_bo_driver vmw_bo_driver;
extern int vmw_dma_quiescent(struct drm_device *dev);
extern int vmw_bo_map_dma(struct ttm_buffer_object *bo);
extern void vmw_bo_unmap_dma(struct ttm_buffer_object *bo);
extern const struct vmw_sg_table *
vmw_bo_sg_table(struct ttm_buffer_object *bo);
extern void vmw_piter_start(struct vmw_piter *viter,
			    const struct vmw_sg_table *vsgt,
			    unsigned long p_offs);

/**
 * vmw_piter_next - Advance the iterator one page.
 *
 * @viter: Pointer to the iterator to advance.
 *
 * Returns false if past the list of pages, true otherwise.
 */
static inline bool vmw_piter_next(struct vmw_piter *viter)
{
	return viter->next(viter);
}

/**
 * vmw_piter_dma_addr - Return the DMA address of the current page.
 *
 * @viter: Pointer to the iterator
 *
 * Returns the DMA address of the page pointed to by @viter.
 */
static inline dma_addr_t vmw_piter_dma_addr(struct vmw_piter *viter)
{
	return viter->dma_address(viter);
}

/**
 * vmw_piter_page - Return a pointer to the current page.
 *
 * @viter: Pointer to the iterator
 *
 * Returns the DMA address of the page pointed to by @viter.
 */
static inline struct page *vmw_piter_page(struct vmw_piter *viter)
{
	return viter->page(viter);
}

/**
 * Command submission - vmwgfx_execbuf.c
 */

extern int vmw_execbuf_ioctl(struct drm_device *dev, unsigned long data,
			     struct drm_file *file_priv, size_t size);
extern int vmw_execbuf_process(struct drm_file *file_priv,
			       struct vmw_private *dev_priv,
			       void __user *user_commands,
			       void *kernel_commands,
			       uint32_t command_size,
			       uint64_t throttle_us,
			       uint32_t dx_context_handle,
			       struct drm_vmw_fence_rep __user
			       *user_fence_rep,
			       struct vmw_fence_obj **out_fence);
extern void __vmw_execbuf_release_pinned_bo(struct vmw_private *dev_priv,
					    struct vmw_fence_obj *fence);
extern void vmw_execbuf_release_pinned_bo(struct vmw_private *dev_priv);

extern int vmw_execbuf_fence_commands(struct drm_file *file_priv,
				      struct vmw_private *dev_priv,
				      struct vmw_fence_obj **p_fence,
				      uint32_t *p_handle);
extern void vmw_execbuf_copy_fence_user(struct vmw_private *dev_priv,
					struct vmw_fpriv *vmw_fp,
					int ret,
					struct drm_vmw_fence_rep __user
					*user_fence_rep,
					struct vmw_fence_obj *fence,
					uint32_t fence_handle);
extern int vmw_validate_single_buffer(struct vmw_private *dev_priv,
				      struct ttm_buffer_object *bo,
				      bool interruptible,
				      bool validate_as_mob);


/**
 * IRQs and wating - vmwgfx_irq.c
 */

extern irqreturn_t vmw_irq_handler(int irq, void *arg);
extern int vmw_wait_seqno(struct vmw_private *dev_priv, bool lazy,
			  uint32_t seqno, bool interruptible,
			  unsigned long timeout);
extern void vmw_irq_preinstall(struct drm_device *dev);
extern int vmw_irq_postinstall(struct drm_device *dev);
extern void vmw_irq_uninstall(struct drm_device *dev);
extern bool vmw_seqno_passed(struct vmw_private *dev_priv,
				uint32_t seqno);
extern int vmw_fallback_wait(struct vmw_private *dev_priv,
			     bool lazy,
			     bool fifo_idle,
			     uint32_t seqno,
			     bool interruptible,
			     unsigned long timeout);
extern void vmw_update_seqno(struct vmw_private *dev_priv,
				struct vmw_fifo_state *fifo_state);
extern void vmw_seqno_waiter_add(struct vmw_private *dev_priv);
extern void vmw_seqno_waiter_remove(struct vmw_private *dev_priv);
extern void vmw_goal_waiter_add(struct vmw_private *dev_priv);
extern void vmw_goal_waiter_remove(struct vmw_private *dev_priv);
extern void vmw_generic_waiter_add(struct vmw_private *dev_priv, u32 flag,
				   int *waiter_count);
extern void vmw_generic_waiter_remove(struct vmw_private *dev_priv,
				      u32 flag, int *waiter_count);

/**
 * Rudimentary fence-like objects currently used only for throttling -
 * vmwgfx_marker.c
 */

extern void vmw_marker_queue_init(struct vmw_marker_queue *queue);
extern void vmw_marker_queue_takedown(struct vmw_marker_queue *queue);
extern int vmw_marker_push(struct vmw_marker_queue *queue,
			   uint32_t seqno);
extern int vmw_marker_pull(struct vmw_marker_queue *queue,
			   uint32_t signaled_seqno);
extern int vmw_wait_lag(struct vmw_private *dev_priv,
			struct vmw_marker_queue *queue, uint32_t us);

/**
 * Kernel framebuffer - vmwgfx_fb.c
 */

int vmw_fb_init(struct vmw_private *vmw_priv);
int vmw_fb_close(struct vmw_private *dev_priv);
int vmw_fb_off(struct vmw_private *vmw_priv);
int vmw_fb_on(struct vmw_private *vmw_priv);

/**
 * Kernel modesetting - vmwgfx_kms.c
 */

int vmw_kms_init(struct vmw_private *dev_priv);
int vmw_kms_close(struct vmw_private *dev_priv);
int vmw_kms_save_vga(struct vmw_private *vmw_priv);
int vmw_kms_restore_vga(struct vmw_private *vmw_priv);
int vmw_kms_cursor_bypass_ioctl(struct drm_device *dev, void *data,
				struct drm_file *file_priv);
void vmw_kms_cursor_post_execbuf(struct vmw_private *dev_priv);
void vmw_kms_cursor_snoop(struct vmw_surface *srf,
			  struct ttm_object_file *tfile,
			  struct ttm_buffer_object *bo,
			  SVGA3dCmdHeader *header);
int vmw_kms_write_svga(struct vmw_private *vmw_priv,
		       unsigned width, unsigned height, unsigned pitch,
		       unsigned bpp, unsigned depth);
void vmw_kms_idle_workqueues(struct vmw_master *vmaster);
bool vmw_kms_validate_mode_vram(struct vmw_private *dev_priv,
				uint32_t pitch,
				uint32_t height);
u32 vmw_get_vblank_counter(struct drm_device *dev, unsigned int pipe);
int vmw_enable_vblank(struct drm_device *dev, unsigned int pipe);
void vmw_disable_vblank(struct drm_device *dev, unsigned int pipe);
int vmw_kms_present(struct vmw_private *dev_priv,
		    struct drm_file *file_priv,
		    struct vmw_framebuffer *vfb,
		    struct vmw_surface *surface,
		    uint32_t sid, int32_t destX, int32_t destY,
		    struct drm_vmw_rect *clips,
		    uint32_t num_clips);
int vmw_kms_update_layout_ioctl(struct drm_device *dev, void *data,
				struct drm_file *file_priv);
void vmw_kms_legacy_hotspot_clear(struct vmw_private *dev_priv);

int vmw_dumb_create(struct drm_file *file_priv,
		    struct drm_device *dev,
		    struct drm_mode_create_dumb *args);

int vmw_dumb_map_offset(struct drm_file *file_priv,
			struct drm_device *dev, uint32_t handle,
			uint64_t *offset);
int vmw_dumb_destroy(struct drm_file *file_priv,
		     struct drm_device *dev,
		     uint32_t handle);
extern int vmw_resource_pin(struct vmw_resource *res, bool interruptible);
extern void vmw_resource_unpin(struct vmw_resource *res);
extern enum vmw_res_type vmw_res_type(const struct vmw_resource *res);

/**
 * Overlay control - vmwgfx_overlay.c
 */

int vmw_overlay_init(struct vmw_private *dev_priv);
int vmw_overlay_close(struct vmw_private *dev_priv);
int vmw_overlay_ioctl(struct drm_device *dev, void *data,
		      struct drm_file *file_priv);
int vmw_overlay_stop_all(struct vmw_private *dev_priv);
int vmw_overlay_resume_all(struct vmw_private *dev_priv);
int vmw_overlay_pause_all(struct vmw_private *dev_priv);
int vmw_overlay_claim(struct vmw_private *dev_priv, uint32_t *out);
int vmw_overlay_unref(struct vmw_private *dev_priv, uint32_t stream_id);
int vmw_overlay_num_overlays(struct vmw_private *dev_priv);
int vmw_overlay_num_free_overlays(struct vmw_private *dev_priv);

/**
 * GMR Id manager
 */

extern const struct ttm_mem_type_manager_func vmw_gmrid_manager_func;

/**
 * Prime - vmwgfx_prime.c
 */

extern const struct dma_buf_ops vmw_prime_dmabuf_ops;
extern int vmw_prime_fd_to_handle(struct drm_device *dev,
				  struct drm_file *file_priv,
				  int fd, u32 *handle);
extern int vmw_prime_handle_to_fd(struct drm_device *dev,
				  struct drm_file *file_priv,
				  uint32_t handle, uint32_t flags,
				  int *prime_fd);

/*
 * MemoryOBject management -  vmwgfx_mob.c
 */
struct vmw_mob;
extern int vmw_mob_bind(struct vmw_private *dev_priv, struct vmw_mob *mob,
			const struct vmw_sg_table *vsgt,
			unsigned long num_data_pages, int32_t mob_id);
extern void vmw_mob_unbind(struct vmw_private *dev_priv,
			   struct vmw_mob *mob);
extern void vmw_mob_destroy(struct vmw_mob *mob);
extern struct vmw_mob *vmw_mob_create(unsigned long data_pages);
extern int vmw_otables_setup(struct vmw_private *dev_priv);
extern void vmw_otables_takedown(struct vmw_private *dev_priv);

/*
 * Context management - vmwgfx_context.c
 */

extern const struct vmw_user_resource_conv *user_context_converter;

extern int vmw_context_check(struct vmw_private *dev_priv,
			     struct ttm_object_file *tfile,
			     int id,
			     struct vmw_resource **p_res);
extern int vmw_context_define_ioctl(struct drm_device *dev, void *data,
				    struct drm_file *file_priv);
extern int vmw_extended_context_define_ioctl(struct drm_device *dev, void *data,
					     struct drm_file *file_priv);
extern int vmw_context_destroy_ioctl(struct drm_device *dev, void *data,
				     struct drm_file *file_priv);
extern struct list_head *vmw_context_binding_list(struct vmw_resource *ctx);
extern struct vmw_cmdbuf_res_manager *
vmw_context_res_man(struct vmw_resource *ctx);
extern struct vmw_resource *vmw_context_cotable(struct vmw_resource *ctx,
						SVGACOTableType cotable_type);
extern struct list_head *vmw_context_binding_list(struct vmw_resource *ctx);
struct vmw_ctx_binding_state;
extern struct vmw_ctx_binding_state *
vmw_context_binding_state(struct vmw_resource *ctx);
extern void vmw_dx_context_scrub_cotables(struct vmw_resource *ctx,
					  bool readback);
extern int vmw_context_bind_dx_query(struct vmw_resource *ctx_res,
				     struct vmw_dma_buffer *mob);
extern struct vmw_dma_buffer *
vmw_context_get_dx_query_mob(struct vmw_resource *ctx_res);


/*
 * Surface management - vmwgfx_surface.c
 */

extern const struct vmw_user_resource_conv *user_surface_converter;

extern void vmw_surface_res_free(struct vmw_resource *res);
extern int vmw_surface_destroy_ioctl(struct drm_device *dev, void *data,
				     struct drm_file *file_priv);
extern int vmw_surface_define_ioctl(struct drm_device *dev, void *data,
				    struct drm_file *file_priv);
extern int vmw_surface_reference_ioctl(struct drm_device *dev, void *data,
				       struct drm_file *file_priv);
extern int vmw_gb_surface_define_ioctl(struct drm_device *dev, void *data,
				       struct drm_file *file_priv);
extern int vmw_gb_surface_reference_ioctl(struct drm_device *dev, void *data,
					  struct drm_file *file_priv);
extern int vmw_surface_check(struct vmw_private *dev_priv,
			     struct ttm_object_file *tfile,
			     uint32_t handle, int *id);
extern int vmw_surface_validate(struct vmw_private *dev_priv,
				struct vmw_surface *srf);
int vmw_surface_gb_priv_define(struct drm_device *dev,
			       uint32_t user_accounting_size,
			       uint32_t svga3d_flags,
			       SVGA3dSurfaceFormat format,
			       bool for_scanout,
			       uint32_t num_mip_levels,
			       uint32_t multisample_count,
			       uint32_t array_size,
			       struct drm_vmw_size size,
			       struct vmw_surface **srf_out);

/*
 * Shader management - vmwgfx_shader.c
 */

extern const struct vmw_user_resource_conv *user_shader_converter;

extern int vmw_shader_define_ioctl(struct drm_device *dev, void *data,
				   struct drm_file *file_priv);
extern int vmw_shader_destroy_ioctl(struct drm_device *dev, void *data,
				    struct drm_file *file_priv);
extern int vmw_compat_shader_add(struct vmw_private *dev_priv,
				 struct vmw_cmdbuf_res_manager *man,
				 u32 user_key, const void *bytecode,
				 SVGA3dShaderType shader_type,
				 size_t size,
				 struct list_head *list);
extern int vmw_shader_remove(struct vmw_cmdbuf_res_manager *man,
			     u32 user_key, SVGA3dShaderType shader_type,
			     struct list_head *list);
extern int vmw_dx_shader_add(struct vmw_cmdbuf_res_manager *man,
			     struct vmw_resource *ctx,
			     u32 user_key,
			     SVGA3dShaderType shader_type,
			     struct list_head *list);
extern void vmw_dx_shader_cotable_list_scrub(struct vmw_private *dev_priv,
					     struct list_head *list,
					     bool readback);

extern struct vmw_resource *
vmw_shader_lookup(struct vmw_cmdbuf_res_manager *man,
		  u32 user_key, SVGA3dShaderType shader_type);

/*
 * Command buffer managed resources - vmwgfx_cmdbuf_res.c
 */

extern struct vmw_cmdbuf_res_manager *
vmw_cmdbuf_res_man_create(struct vmw_private *dev_priv);
extern void vmw_cmdbuf_res_man_destroy(struct vmw_cmdbuf_res_manager *man);
extern size_t vmw_cmdbuf_res_man_size(void);
extern struct vmw_resource *
vmw_cmdbuf_res_lookup(struct vmw_cmdbuf_res_manager *man,
		      enum vmw_cmdbuf_res_type res_type,
		      u32 user_key);
extern void vmw_cmdbuf_res_revert(struct list_head *list);
extern void vmw_cmdbuf_res_commit(struct list_head *list);
extern int vmw_cmdbuf_res_add(struct vmw_cmdbuf_res_manager *man,
			      enum vmw_cmdbuf_res_type res_type,
			      u32 user_key,
			      struct vmw_resource *res,
			      struct list_head *list);
extern int vmw_cmdbuf_res_remove(struct vmw_cmdbuf_res_manager *man,
				 enum vmw_cmdbuf_res_type res_type,
				 u32 user_key,
				 struct list_head *list,
				 struct vmw_resource **res);

/*
 * COTable management - vmwgfx_cotable.c
 */
extern const SVGACOTableType vmw_cotable_scrub_order[];
extern struct vmw_resource *vmw_cotable_alloc(struct vmw_private *dev_priv,
					      struct vmw_resource *ctx,
					      u32 type);
extern int vmw_cotable_notify(struct vmw_resource *res, int id);
extern int vmw_cotable_scrub(struct vmw_resource *res, bool readback);
extern void vmw_cotable_add_resource(struct vmw_resource *ctx,
				     struct list_head *head);

/*
 * Command buffer managerment vmwgfx_cmdbuf.c
 */
struct vmw_cmdbuf_man;
struct vmw_cmdbuf_header;

extern struct vmw_cmdbuf_man *
vmw_cmdbuf_man_create(struct vmw_private *dev_priv);
extern int vmw_cmdbuf_set_pool_size(struct vmw_cmdbuf_man *man,
				    size_t size, size_t default_size);
extern void vmw_cmdbuf_remove_pool(struct vmw_cmdbuf_man *man);
extern void vmw_cmdbuf_man_destroy(struct vmw_cmdbuf_man *man);
extern int vmw_cmdbuf_idle(struct vmw_cmdbuf_man *man, bool interruptible,
			   unsigned long timeout);
extern void *vmw_cmdbuf_reserve(struct vmw_cmdbuf_man *man, size_t size,
				int ctx_id, bool interruptible,
				struct vmw_cmdbuf_header *header);
extern void vmw_cmdbuf_commit(struct vmw_cmdbuf_man *man, size_t size,
			      struct vmw_cmdbuf_header *header,
			      bool flush);
extern void vmw_cmdbuf_tasklet_schedule(struct vmw_cmdbuf_man *man);
extern void *vmw_cmdbuf_alloc(struct vmw_cmdbuf_man *man,
			      size_t size, bool interruptible,
			      struct vmw_cmdbuf_header **p_header);
extern void vmw_cmdbuf_header_free(struct vmw_cmdbuf_header *header);
extern int vmw_cmdbuf_cur_flush(struct vmw_cmdbuf_man *man,
				bool interruptible);


/**
 * Inline helper functions
 */

static inline void vmw_surface_unreference(struct vmw_surface **srf)
{
	struct vmw_surface *tmp_srf = *srf;
	struct vmw_resource *res = &tmp_srf->res;
	*srf = NULL;

	vmw_resource_unreference(&res);
}

static inline struct vmw_surface *vmw_surface_reference(struct vmw_surface *srf)
{
	(void) vmw_resource_reference(&srf->res);
	return srf;
}

static inline void vmw_dmabuf_unreference(struct vmw_dma_buffer **buf)
{
	struct vmw_dma_buffer *tmp_buf = *buf;

	*buf = NULL;
	if (tmp_buf != NULL) {
		struct ttm_buffer_object *bo = &tmp_buf->base;

		ttm_bo_unref(&bo);
	}
}

static inline struct vmw_dma_buffer *vmw_dmabuf_reference(struct vmw_dma_buffer *buf)
{
	if (ttm_bo_reference(&buf->base))
		return buf;
	return NULL;
}

static inline struct ttm_mem_global *vmw_mem_glob(struct vmw_private *dev_priv)
{
	return (struct ttm_mem_global *) dev_priv->mem_global_ref.object;
}

static inline void vmw_fifo_resource_inc(struct vmw_private *dev_priv)
{
	atomic_inc(&dev_priv->num_fifo_resources);
}

static inline void vmw_fifo_resource_dec(struct vmw_private *dev_priv)
{
	atomic_dec(&dev_priv->num_fifo_resources);
}

/**
 * vmw_mmio_read - Perform a MMIO read from volatile memory
 *
 * @addr: The address to read from
 *
 * This function is intended to be equivalent to ioread32() on
 * memremap'd memory, but without byteswapping.
 */
static inline u32 vmw_mmio_read(u32 *addr)
{
	return READ_ONCE(*addr);
}

/**
 * vmw_mmio_write - Perform a MMIO write to volatile memory
 *
 * @addr: The address to write to
 *
 * This function is intended to be equivalent to iowrite32 on
 * memremap'd memory, but without byteswapping.
 */
static inline void vmw_mmio_write(u32 value, u32 *addr)
{
	WRITE_ONCE(*addr, value);
}

/**
 * Add vmw_msg module function
 */
extern int vmw_host_log(const char *log);

#endif
