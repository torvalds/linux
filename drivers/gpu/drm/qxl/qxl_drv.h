/*
 * Copyright 2013 Red Hat Inc.
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
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE COPYRIGHT HOLDER(S) OR AUTHOR(S) BE LIABLE FOR ANY CLAIM, DAMAGES OR
 * OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
 * ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 * OTHER DEALINGS IN THE SOFTWARE.
 *
 * Authors: Dave Airlie
 *          Alon Levy
 */


#ifndef QXL_DRV_H
#define QXL_DRV_H

/*
 * Definitions taken from spice-protocol, plus kernel driver specific bits.
 */

#include <linux/workqueue.h>
#include <linux/firmware.h>
#include <linux/platform_device.h>

#include "drmP.h"
#include "drm_crtc.h"
#include <ttm/ttm_bo_api.h>
#include <ttm/ttm_bo_driver.h>
#include <ttm/ttm_placement.h>
#include <ttm/ttm_module.h>

#include <drm/qxl_drm.h>
#include "qxl_dev.h"

#define DRIVER_AUTHOR		"Dave Airlie"

#define DRIVER_NAME		"qxl"
#define DRIVER_DESC		"RH QXL"
#define DRIVER_DATE		"20120117"

#define DRIVER_MAJOR 0
#define DRIVER_MINOR 1
#define DRIVER_PATCHLEVEL 0

#define QXL_NUM_OUTPUTS 1

#define QXL_DEBUGFS_MAX_COMPONENTS		32

extern int qxl_log_level;

enum {
	QXL_INFO_LEVEL = 1,
	QXL_DEBUG_LEVEL = 2,
};

#define QXL_INFO(qdev, fmt, ...) do { \
		if (qxl_log_level >= QXL_INFO_LEVEL) {	\
			qxl_io_log(qdev, fmt, __VA_ARGS__); \
		}	\
	} while (0)
#define QXL_DEBUG(qdev, fmt, ...) do { \
		if (qxl_log_level >= QXL_DEBUG_LEVEL) {	\
			qxl_io_log(qdev, fmt, __VA_ARGS__); \
		}	\
	} while (0)
#define QXL_INFO_ONCE(qdev, fmt, ...) do { \
		static int done;		\
		if (!done) {			\
			done = 1;			\
			QXL_INFO(qdev, fmt, __VA_ARGS__);	\
		}						\
	} while (0)

#define DRM_FILE_OFFSET 0x100000000ULL
#define DRM_FILE_PAGE_OFFSET (DRM_FILE_OFFSET >> PAGE_SHIFT)

#define QXL_INTERRUPT_MASK (\
	QXL_INTERRUPT_DISPLAY |\
	QXL_INTERRUPT_CURSOR |\
	QXL_INTERRUPT_IO_CMD |\
	QXL_INTERRUPT_CLIENT_MONITORS_CONFIG)

struct qxl_fence {
	struct qxl_device *qdev;
	uint32_t num_active_releases;
	uint32_t *release_ids;
	struct radix_tree_root tree;
};

struct qxl_bo {
	/* Protected by gem.mutex */
	struct list_head		list;
	/* Protected by tbo.reserved */
	u32				placements[3];
	struct ttm_placement		placement;
	struct ttm_buffer_object	tbo;
	struct ttm_bo_kmap_obj		kmap;
	unsigned			pin_count;
	void				*kptr;
	int                             type;
	/* Constant after initialization */
	struct drm_gem_object		gem_base;
	bool is_primary; /* is this now a primary surface */
	bool hw_surf_alloc;
	struct qxl_surface surf;
	uint32_t surface_id;
	struct qxl_fence fence; /* per bo fence  - list of releases */
	struct qxl_release *surf_create;
	atomic_t reserve_count;
};
#define gem_to_qxl_bo(gobj) container_of((gobj), struct qxl_bo, gem_base)

struct qxl_gem {
	struct mutex		mutex;
	struct list_head	objects;
};

struct qxl_bo_list {
	struct list_head lhead;
	struct qxl_bo *bo;
};

struct qxl_reloc_list {
	struct list_head bos;
};

struct qxl_crtc {
	struct drm_crtc base;
	int cur_x;
	int cur_y;
};

struct qxl_output {
	int index;
	struct drm_connector base;
	struct drm_encoder enc;
};

struct qxl_framebuffer {
	struct drm_framebuffer base;
	struct drm_gem_object *obj;
};

#define to_qxl_crtc(x) container_of(x, struct qxl_crtc, base)
#define drm_connector_to_qxl_output(x) container_of(x, struct qxl_output, base)
#define drm_encoder_to_qxl_output(x) container_of(x, struct qxl_output, base)
#define to_qxl_framebuffer(x) container_of(x, struct qxl_framebuffer, base)

struct qxl_mman {
	struct ttm_bo_global_ref        bo_global_ref;
	struct drm_global_reference	mem_global_ref;
	bool				mem_global_referenced;
	struct ttm_bo_device		bdev;
};

struct qxl_mode_info {
	int num_modes;
	struct qxl_mode *modes;
	bool mode_config_initialized;

	/* pointer to fbdev info structure */
	struct qxl_fbdev *qfbdev;
};


struct qxl_memslot {
	uint8_t		generation;
	uint64_t	start_phys_addr;
	uint64_t	end_phys_addr;
	uint64_t	high_bits;
};

enum {
	QXL_RELEASE_DRAWABLE,
	QXL_RELEASE_SURFACE_CMD,
	QXL_RELEASE_CURSOR_CMD,
};

/* drm_ prefix to differentiate from qxl_release_info in
 * spice-protocol/qxl_dev.h */
#define QXL_MAX_RES 96
struct qxl_release {
	int id;
	int type;
	int bo_count;
	uint32_t release_offset;
	uint32_t surface_release_id;
	struct qxl_bo *bos[QXL_MAX_RES];
};

struct qxl_fb_image {
	struct qxl_device *qdev;
	uint32_t pseudo_palette[16];
	struct fb_image fb_image;
	uint32_t visual;
};

struct qxl_draw_fill {
	struct qxl_device *qdev;
	struct qxl_rect rect;
	uint32_t color;
	uint16_t rop;
};

/*
 * Debugfs
 */
struct qxl_debugfs {
	struct drm_info_list	*files;
	unsigned		num_files;
};

int qxl_debugfs_add_files(struct qxl_device *rdev,
			     struct drm_info_list *files,
			     unsigned nfiles);
int qxl_debugfs_fence_init(struct qxl_device *rdev);
void qxl_debugfs_remove_files(struct qxl_device *qdev);

struct qxl_device;

struct qxl_device {
	struct device			*dev;
	struct drm_device		*ddev;
	struct pci_dev			*pdev;
	unsigned long flags;

	resource_size_t vram_base, vram_size;
	resource_size_t surfaceram_base, surfaceram_size;
	resource_size_t rom_base, rom_size;
	struct qxl_rom *rom;

	struct qxl_mode *modes;
	struct qxl_bo *monitors_config_bo;
	struct qxl_monitors_config *monitors_config;

	/* last received client_monitors_config */
	struct qxl_monitors_config *client_monitors_config;

	int io_base;
	void *ram;
	struct qxl_mman		mman;
	struct qxl_gem		gem;
	struct qxl_mode_info mode_info;

	struct fb_info			*fbdev_info;
	struct qxl_framebuffer	*fbdev_qfb;
	void *ram_physical;

	struct qxl_ring *release_ring;
	struct qxl_ring *command_ring;
	struct qxl_ring *cursor_ring;

	struct qxl_ram_header *ram_header;

	bool primary_created;

	struct qxl_memslot	*mem_slots;
	uint8_t		n_mem_slots;

	uint8_t		main_mem_slot;
	uint8_t		surfaces_mem_slot;
	uint8_t		slot_id_bits;
	uint8_t		slot_gen_bits;
	uint64_t	va_slot_mask;

	struct idr	release_idr;
	spinlock_t release_idr_lock;
	struct mutex	async_io_mutex;
	unsigned int last_sent_io_cmd;

	/* interrupt handling */
	atomic_t irq_received;
	atomic_t irq_received_display;
	atomic_t irq_received_cursor;
	atomic_t irq_received_io_cmd;
	unsigned irq_received_error;
	wait_queue_head_t display_event;
	wait_queue_head_t cursor_event;
	wait_queue_head_t io_cmd_event;
	struct work_struct client_monitors_config_work;

	/* debugfs */
	struct qxl_debugfs	debugfs[QXL_DEBUGFS_MAX_COMPONENTS];
	unsigned		debugfs_count;

	struct mutex		update_area_mutex;

	struct idr	surf_id_idr;
	spinlock_t surf_id_idr_lock;
	int last_alloced_surf_id;

	struct mutex surf_evict_mutex;
	struct io_mapping *vram_mapping;
	struct io_mapping *surface_mapping;

	/* */
	struct mutex release_mutex;
	struct qxl_bo *current_release_bo[3];
	int current_release_bo_offset[3];

	struct workqueue_struct *gc_queue;
	struct work_struct gc_work;

};

/* forward declaration for QXL_INFO_IO */
void qxl_io_log(struct qxl_device *qdev, const char *fmt, ...);

extern struct drm_ioctl_desc qxl_ioctls[];
extern int qxl_max_ioctl;

int qxl_driver_load(struct drm_device *dev, unsigned long flags);
int qxl_driver_unload(struct drm_device *dev);

int qxl_modeset_init(struct qxl_device *qdev);
void qxl_modeset_fini(struct qxl_device *qdev);

int qxl_bo_init(struct qxl_device *qdev);
void qxl_bo_fini(struct qxl_device *qdev);

struct qxl_ring *qxl_ring_create(struct qxl_ring_header *header,
				 int element_size,
				 int n_elements,
				 int prod_notify,
				 bool set_prod_notify,
				 wait_queue_head_t *push_event);
void qxl_ring_free(struct qxl_ring *ring);

static inline void *
qxl_fb_virtual_address(struct qxl_device *qdev, unsigned long physical)
{
	QXL_INFO(qdev, "not implemented (%lu)\n", physical);
	return 0;
}

static inline uint64_t
qxl_bo_physical_address(struct qxl_device *qdev, struct qxl_bo *bo,
			unsigned long offset)
{
	int slot_id = bo->type == QXL_GEM_DOMAIN_VRAM ? qdev->main_mem_slot : qdev->surfaces_mem_slot;
	struct qxl_memslot *slot = &(qdev->mem_slots[slot_id]);

	/* TODO - need to hold one of the locks to read tbo.offset */
	return slot->high_bits | (bo->tbo.offset + offset);
}

/* qxl_fb.c */
#define QXLFB_CONN_LIMIT 1

int qxl_fbdev_init(struct qxl_device *qdev);
void qxl_fbdev_fini(struct qxl_device *qdev);
int qxl_get_handle_for_primary_fb(struct qxl_device *qdev,
				  struct drm_file *file_priv,
				  uint32_t *handle);

/* qxl_display.c */
int
qxl_framebuffer_init(struct drm_device *dev,
		     struct qxl_framebuffer *rfb,
		     struct drm_mode_fb_cmd2 *mode_cmd,
		     struct drm_gem_object *obj);
void qxl_display_read_client_monitors_config(struct qxl_device *qdev);
void qxl_send_monitors_config(struct qxl_device *qdev);

/* used by qxl_debugfs only */
void qxl_crtc_set_from_monitors_config(struct qxl_device *qdev);
void qxl_alloc_client_monitors_config(struct qxl_device *qdev, unsigned count);

/* qxl_gem.c */
int qxl_gem_init(struct qxl_device *qdev);
void qxl_gem_fini(struct qxl_device *qdev);
int qxl_gem_object_create(struct qxl_device *qdev, int size,
			  int alignment, int initial_domain,
			  bool discardable, bool kernel,
			  struct qxl_surface *surf,
			  struct drm_gem_object **obj);
int qxl_gem_object_pin(struct drm_gem_object *obj, uint32_t pin_domain,
			  uint64_t *gpu_addr);
void qxl_gem_object_unpin(struct drm_gem_object *obj);
int qxl_gem_object_create_with_handle(struct qxl_device *qdev,
				      struct drm_file *file_priv,
				      u32 domain,
				      size_t size,
				      struct qxl_surface *surf,
				      struct qxl_bo **qobj,
				      uint32_t *handle);
int qxl_gem_object_init(struct drm_gem_object *obj);
void qxl_gem_object_free(struct drm_gem_object *gobj);
int qxl_gem_object_open(struct drm_gem_object *obj, struct drm_file *file_priv);
void qxl_gem_object_close(struct drm_gem_object *obj,
			  struct drm_file *file_priv);
void qxl_bo_force_delete(struct qxl_device *qdev);
int qxl_bo_kmap(struct qxl_bo *bo, void **ptr);

/* qxl_dumb.c */
int qxl_mode_dumb_create(struct drm_file *file_priv,
			 struct drm_device *dev,
			 struct drm_mode_create_dumb *args);
int qxl_mode_dumb_mmap(struct drm_file *filp,
		       struct drm_device *dev,
		       uint32_t handle, uint64_t *offset_p);


/* qxl ttm */
int qxl_ttm_init(struct qxl_device *qdev);
void qxl_ttm_fini(struct qxl_device *qdev);
int qxl_mmap(struct file *filp, struct vm_area_struct *vma);

/* qxl image */

int qxl_image_create(struct qxl_device *qdev,
		     struct qxl_release *release,
		     struct qxl_bo **image_bo,
		     const uint8_t *data,
		     int x, int y, int width, int height,
		     int depth, int stride);
void qxl_update_screen(struct qxl_device *qxl);

/* qxl io operations (qxl_cmd.c) */

void qxl_io_create_primary(struct qxl_device *qdev,
			   unsigned width, unsigned height, unsigned offset,
			   struct qxl_bo *bo);
void qxl_io_destroy_primary(struct qxl_device *qdev);
void qxl_io_memslot_add(struct qxl_device *qdev, uint8_t id);
void qxl_io_notify_oom(struct qxl_device *qdev);

int qxl_io_update_area(struct qxl_device *qdev, struct qxl_bo *surf,
		       const struct qxl_rect *area);

void qxl_io_reset(struct qxl_device *qdev);
void qxl_io_monitors_config(struct qxl_device *qdev);
int qxl_ring_push(struct qxl_ring *ring, const void *new_elt, bool interruptible);
void qxl_io_flush_release(struct qxl_device *qdev);
void qxl_io_flush_surfaces(struct qxl_device *qdev);

int qxl_release_reserve(struct qxl_device *qdev,
			struct qxl_release *release, bool no_wait);
void qxl_release_unreserve(struct qxl_device *qdev,
			   struct qxl_release *release);
union qxl_release_info *qxl_release_map(struct qxl_device *qdev,
					struct qxl_release *release);
void qxl_release_unmap(struct qxl_device *qdev,
		       struct qxl_release *release,
		       union qxl_release_info *info);
/*
 * qxl_bo_add_resource.
 *
 */
void qxl_bo_add_resource(struct qxl_bo *main_bo, struct qxl_bo *resource);

int qxl_alloc_surface_release_reserved(struct qxl_device *qdev,
				       enum qxl_surface_cmd_type surface_cmd_type,
				       struct qxl_release *create_rel,
				       struct qxl_release **release);
int qxl_alloc_release_reserved(struct qxl_device *qdev, unsigned long size,
			       int type, struct qxl_release **release,
			       struct qxl_bo **rbo);
int qxl_fence_releaseable(struct qxl_device *qdev,
			  struct qxl_release *release);
int
qxl_push_command_ring_release(struct qxl_device *qdev, struct qxl_release *release,
			      uint32_t type, bool interruptible);
int
qxl_push_cursor_ring_release(struct qxl_device *qdev, struct qxl_release *release,
			     uint32_t type, bool interruptible);
int qxl_alloc_bo_reserved(struct qxl_device *qdev, unsigned long size,
			  struct qxl_bo **_bo);
/* qxl drawing commands */

void qxl_draw_opaque_fb(const struct qxl_fb_image *qxl_fb_image,
			int stride /* filled in if 0 */);

void qxl_draw_dirty_fb(struct qxl_device *qdev,
		       struct qxl_framebuffer *qxl_fb,
		       struct qxl_bo *bo,
		       unsigned flags, unsigned color,
		       struct drm_clip_rect *clips,
		       unsigned num_clips, int inc);

void qxl_draw_fill(struct qxl_draw_fill *qxl_draw_fill_rec);

void qxl_draw_copyarea(struct qxl_device *qdev,
		       u32 width, u32 height,
		       u32 sx, u32 sy,
		       u32 dx, u32 dy);

uint64_t
qxl_release_alloc(struct qxl_device *qdev, int type,
		  struct qxl_release **ret);

void qxl_release_free(struct qxl_device *qdev,
		      struct qxl_release *release);
void qxl_release_add_res(struct qxl_device *qdev,
			 struct qxl_release *release,
			 struct qxl_bo *bo);
/* used by qxl_debugfs_release */
struct qxl_release *qxl_release_from_id_locked(struct qxl_device *qdev,
						   uint64_t id);

bool qxl_queue_garbage_collect(struct qxl_device *qdev, bool flush);
int qxl_garbage_collect(struct qxl_device *qdev);

/* debugfs */

int qxl_debugfs_init(struct drm_minor *minor);
void qxl_debugfs_takedown(struct drm_minor *minor);

/* qxl_irq.c */
int qxl_irq_init(struct qxl_device *qdev);
irqreturn_t qxl_irq_handler(DRM_IRQ_ARGS);

/* qxl_fb.c */
int qxl_fb_init(struct qxl_device *qdev);

int qxl_debugfs_add_files(struct qxl_device *qdev,
			  struct drm_info_list *files,
			  unsigned nfiles);

int qxl_surface_id_alloc(struct qxl_device *qdev,
			 struct qxl_bo *surf);
void qxl_surface_id_dealloc(struct qxl_device *qdev,
			    uint32_t surface_id);
int qxl_hw_surface_alloc(struct qxl_device *qdev,
			 struct qxl_bo *surf,
			 struct ttm_mem_reg *mem);
int qxl_hw_surface_dealloc(struct qxl_device *qdev,
			   struct qxl_bo *surf);

int qxl_bo_check_id(struct qxl_device *qdev, struct qxl_bo *bo);

struct qxl_drv_surface *
qxl_surface_lookup(struct drm_device *dev, int surface_id);
void qxl_surface_evict(struct qxl_device *qdev, struct qxl_bo *surf, bool freeing);
int qxl_update_surface(struct qxl_device *qdev, struct qxl_bo *surf);

/* qxl_fence.c */
int qxl_fence_add_release(struct qxl_fence *qfence, uint32_t rel_id);
int qxl_fence_remove_release(struct qxl_fence *qfence, uint32_t rel_id);
int qxl_fence_init(struct qxl_device *qdev, struct qxl_fence *qfence);
void qxl_fence_fini(struct qxl_fence *qfence);

#endif
