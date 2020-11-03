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

#include <linux/dma-buf-map.h>
#include <linux/dma-fence.h>
#include <linux/firmware.h>
#include <linux/platform_device.h>
#include <linux/workqueue.h>

#include <drm/drm_crtc.h>
#include <drm/drm_encoder.h>
#include <drm/drm_fb_helper.h>
#include <drm/drm_gem_ttm_helper.h>
#include <drm/drm_ioctl.h>
#include <drm/drm_gem.h>
#include <drm/qxl_drm.h>
#include <drm/ttm/ttm_bo_api.h>
#include <drm/ttm/ttm_bo_driver.h>
#include <drm/ttm/ttm_execbuf_util.h>
#include <drm/ttm/ttm_module.h>
#include <drm/ttm/ttm_placement.h>

#include "qxl_dev.h"

struct dma_buf_map;

#define DRIVER_AUTHOR		"Dave Airlie"

#define DRIVER_NAME		"qxl"
#define DRIVER_DESC		"RH QXL"
#define DRIVER_DATE		"20120117"

#define DRIVER_MAJOR 0
#define DRIVER_MINOR 1
#define DRIVER_PATCHLEVEL 0

#define QXL_DEBUGFS_MAX_COMPONENTS		32

extern int qxl_num_crtc;
extern int qxl_max_ioctls;

#define QXL_INTERRUPT_MASK (\
	QXL_INTERRUPT_DISPLAY |\
	QXL_INTERRUPT_CURSOR |\
	QXL_INTERRUPT_IO_CMD |\
	QXL_INTERRUPT_CLIENT_MONITORS_CONFIG)

struct qxl_bo {
	struct ttm_buffer_object	tbo;

	/* Protected by gem.mutex */
	struct list_head		list;
	/* Protected by tbo.reserved */
	struct ttm_place		placements[3];
	struct ttm_placement		placement;
	struct dma_buf_map		map;
	void				*kptr;
	unsigned int                    map_count;
	int                             type;

	/* Constant after initialization */
	unsigned int is_primary:1; /* is this now a primary surface */
	unsigned int is_dumb:1;
	struct qxl_bo *shadow;
	unsigned int hw_surf_alloc:1;
	struct qxl_surface surf;
	uint32_t surface_id;
	struct qxl_release *surf_create;
};
#define gem_to_qxl_bo(gobj) container_of((gobj), struct qxl_bo, tbo.base)
#define to_qxl_bo(tobj) container_of((tobj), struct qxl_bo, tbo)

struct qxl_gem {
	struct mutex		mutex;
	struct list_head	objects;
};

struct qxl_bo_list {
	struct ttm_validate_buffer tv;
};

struct qxl_crtc {
	struct drm_crtc base;
	int index;

	struct qxl_bo *cursor_bo;
};

struct qxl_output {
	int index;
	struct drm_connector base;
	struct drm_encoder enc;
};

#define to_qxl_crtc(x) container_of(x, struct qxl_crtc, base)
#define drm_connector_to_qxl_output(x) container_of(x, struct qxl_output, base)
#define drm_encoder_to_qxl_output(x) container_of(x, struct qxl_output, enc)

struct qxl_mman {
	struct ttm_bo_device		bdev;
};

struct qxl_memslot {
	int             index;
	const char      *name;
	uint8_t		generation;
	uint64_t	start_phys_addr;
	uint64_t	size;
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
	struct dma_fence base;

	int id;
	int type;
	struct qxl_bo *release_bo;
	uint32_t release_offset;
	uint32_t surface_release_id;
	struct ww_acquire_ctx ticket;
	struct list_head bos;
};

struct qxl_drm_chunk {
	struct list_head head;
	struct qxl_bo *bo;
};

struct qxl_drm_image {
	struct qxl_bo *bo;
	struct list_head chunk_list;
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
	unsigned int num_files;
};

int qxl_debugfs_fence_init(struct qxl_device *rdev);

struct qxl_device {
	struct drm_device ddev;

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

	void *ram_physical;

	struct qxl_ring *release_ring;
	struct qxl_ring *command_ring;
	struct qxl_ring *cursor_ring;

	struct qxl_ram_header *ram_header;

	struct qxl_bo *primary_bo;
	struct qxl_bo *dumb_shadow_bo;
	struct qxl_head *dumb_heads;

	struct qxl_memslot main_slot;
	struct qxl_memslot surfaces_slot;

	spinlock_t	release_lock;
	struct idr	release_idr;
	uint32_t	release_seqno;
	spinlock_t release_idr_lock;
	struct mutex	async_io_mutex;
	unsigned int last_sent_io_cmd;

	/* interrupt handling */
	atomic_t irq_received;
	atomic_t irq_received_display;
	atomic_t irq_received_cursor;
	atomic_t irq_received_io_cmd;
	unsigned int irq_received_error;
	wait_queue_head_t display_event;
	wait_queue_head_t cursor_event;
	wait_queue_head_t io_cmd_event;
	struct work_struct client_monitors_config_work;

	/* debugfs */
	struct qxl_debugfs	debugfs[QXL_DEBUGFS_MAX_COMPONENTS];
	unsigned int debugfs_count;

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

	struct work_struct gc_work;

	struct drm_property *hotplug_mode_update_property;
	int monitors_config_width;
	int monitors_config_height;
};

#define to_qxl(dev) container_of(dev, struct qxl_device, ddev)

extern const struct drm_ioctl_desc qxl_ioctls[];
extern int qxl_max_ioctl;

int qxl_device_init(struct qxl_device *qdev, struct pci_dev *pdev);
void qxl_device_fini(struct qxl_device *qdev);

int qxl_modeset_init(struct qxl_device *qdev);
void qxl_modeset_fini(struct qxl_device *qdev);

int qxl_bo_init(struct qxl_device *qdev);
void qxl_bo_fini(struct qxl_device *qdev);

void qxl_reinit_memslots(struct qxl_device *qdev);
int qxl_surf_evict(struct qxl_device *qdev);
int qxl_vram_evict(struct qxl_device *qdev);

struct qxl_ring *qxl_ring_create(struct qxl_ring_header *header,
				 int element_size,
				 int n_elements,
				 int prod_notify,
				 bool set_prod_notify,
				 wait_queue_head_t *push_event);
void qxl_ring_free(struct qxl_ring *ring);
void qxl_ring_init_hdr(struct qxl_ring *ring);
int qxl_check_idle(struct qxl_ring *ring);

static inline uint64_t
qxl_bo_physical_address(struct qxl_device *qdev, struct qxl_bo *bo,
			unsigned long offset)
{
	struct qxl_memslot *slot =
		(bo->tbo.mem.mem_type == TTM_PL_VRAM)
		? &qdev->main_slot : &qdev->surfaces_slot;

       /* TODO - need to hold one of the locks to read bo->tbo.mem.start */

	return slot->high_bits | ((bo->tbo.mem.start << PAGE_SHIFT) + offset);
}

/* qxl_display.c */
void qxl_display_read_client_monitors_config(struct qxl_device *qdev);
int qxl_create_monitors_object(struct qxl_device *qdev);
int qxl_destroy_monitors_object(struct qxl_device *qdev);

/* qxl_gem.c */
void qxl_gem_init(struct qxl_device *qdev);
void qxl_gem_fini(struct qxl_device *qdev);
int qxl_gem_object_create(struct qxl_device *qdev, int size,
			  int alignment, int initial_domain,
			  bool discardable, bool kernel,
			  struct qxl_surface *surf,
			  struct drm_gem_object **obj);
int qxl_gem_object_create_with_handle(struct qxl_device *qdev,
				      struct drm_file *file_priv,
				      u32 domain,
				      size_t size,
				      struct qxl_surface *surf,
				      struct qxl_bo **qobj,
				      uint32_t *handle);
void qxl_gem_object_free(struct drm_gem_object *gobj);
int qxl_gem_object_open(struct drm_gem_object *obj, struct drm_file *file_priv);
void qxl_gem_object_close(struct drm_gem_object *obj,
			  struct drm_file *file_priv);
void qxl_bo_force_delete(struct qxl_device *qdev);

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
int qxl_ttm_io_mem_reserve(struct ttm_bo_device *bdev,
			   struct ttm_resource *mem);

/* qxl image */

int qxl_image_init(struct qxl_device *qdev,
		   struct qxl_release *release,
		   struct qxl_drm_image *dimage,
		   const uint8_t *data,
		   int x, int y, int width, int height,
		   int depth, int stride);
int
qxl_image_alloc_objects(struct qxl_device *qdev,
			struct qxl_release *release,
			struct qxl_drm_image **image_ptr,
			int height, int stride);
void qxl_image_free_objects(struct qxl_device *qdev, struct qxl_drm_image *dimage);

void qxl_update_screen(struct qxl_device *qxl);

/* qxl io operations (qxl_cmd.c) */

void qxl_io_create_primary(struct qxl_device *qdev,
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

union qxl_release_info *qxl_release_map(struct qxl_device *qdev,
					struct qxl_release *release);
void qxl_release_unmap(struct qxl_device *qdev,
		       struct qxl_release *release,
		       union qxl_release_info *info);
int qxl_release_list_add(struct qxl_release *release, struct qxl_bo *bo);
int qxl_release_reserve_list(struct qxl_release *release, bool no_intr);
void qxl_release_backoff_reserve_list(struct qxl_release *release);
void qxl_release_fence_buffer_objects(struct qxl_release *release);

int qxl_alloc_surface_release_reserved(struct qxl_device *qdev,
				       enum qxl_surface_cmd_type surface_cmd_type,
				       struct qxl_release *create_rel,
				       struct qxl_release **release);
int qxl_alloc_release_reserved(struct qxl_device *qdev, unsigned long size,
			       int type, struct qxl_release **release,
			       struct qxl_bo **rbo);

int
qxl_push_command_ring_release(struct qxl_device *qdev, struct qxl_release *release,
			      uint32_t type, bool interruptible);
int
qxl_push_cursor_ring_release(struct qxl_device *qdev, struct qxl_release *release,
			     uint32_t type, bool interruptible);
int qxl_alloc_bo_reserved(struct qxl_device *qdev,
			  struct qxl_release *release,
			  unsigned long size,
			  struct qxl_bo **_bo);
/* qxl drawing commands */

void qxl_draw_dirty_fb(struct qxl_device *qdev,
		       struct drm_framebuffer *fb,
		       struct qxl_bo *bo,
		       unsigned int flags, unsigned int color,
		       struct drm_clip_rect *clips,
		       unsigned int num_clips, int inc,
		       uint32_t dumb_shadow_offset);

void qxl_release_free(struct qxl_device *qdev,
		      struct qxl_release *release);

/* used by qxl_debugfs_release */
struct qxl_release *qxl_release_from_id_locked(struct qxl_device *qdev,
						   uint64_t id);

bool qxl_queue_garbage_collect(struct qxl_device *qdev, bool flush);
int qxl_garbage_collect(struct qxl_device *qdev);

/* debugfs */

void qxl_debugfs_init(struct drm_minor *minor);
void qxl_ttm_debugfs_init(struct qxl_device *qdev);

/* qxl_prime.c */
int qxl_gem_prime_pin(struct drm_gem_object *obj);
void qxl_gem_prime_unpin(struct drm_gem_object *obj);
struct sg_table *qxl_gem_prime_get_sg_table(struct drm_gem_object *obj);
struct drm_gem_object *qxl_gem_prime_import_sg_table(
	struct drm_device *dev, struct dma_buf_attachment *attach,
	struct sg_table *sgt);
int qxl_gem_prime_vmap(struct drm_gem_object *obj, struct dma_buf_map *map);
void qxl_gem_prime_vunmap(struct drm_gem_object *obj,
			  struct dma_buf_map *map);
int qxl_gem_prime_mmap(struct drm_gem_object *obj,
				struct vm_area_struct *vma);

/* qxl_irq.c */
int qxl_irq_init(struct qxl_device *qdev);
irqreturn_t qxl_irq_handler(int irq, void *arg);

void qxl_debugfs_add_files(struct qxl_device *qdev,
			   struct drm_info_list *files,
			   unsigned int nfiles);

int qxl_surface_id_alloc(struct qxl_device *qdev,
			 struct qxl_bo *surf);
void qxl_surface_id_dealloc(struct qxl_device *qdev,
			    uint32_t surface_id);
int qxl_hw_surface_alloc(struct qxl_device *qdev,
			 struct qxl_bo *surf);
int qxl_hw_surface_dealloc(struct qxl_device *qdev,
			   struct qxl_bo *surf);

int qxl_bo_check_id(struct qxl_device *qdev, struct qxl_bo *bo);

struct qxl_drv_surface *
qxl_surface_lookup(struct drm_device *dev, int surface_id);
void qxl_surface_evict(struct qxl_device *qdev, struct qxl_bo *surf, bool freeing);

#endif
