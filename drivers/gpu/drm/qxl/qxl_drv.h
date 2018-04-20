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

#include <linux/dma-fence.h>
#include <linux/workqueue.h>
#include <linux/firmware.h>
#include <linux/platform_device.h>

#include <drm/drm_crtc.h>
#include <drm/drm_encoder.h>
#include <drm/drm_gem.h>
#include <drm/drmP.h>
#include <drm/ttm/ttm_bo_api.h>
#include <drm/ttm/ttm_bo_driver.h>
/* just for ttm_validate_buffer */
#include <drm/ttm/ttm_execbuf_util.h>
#include <drm/ttm/ttm_module.h>
#include <drm/ttm/ttm_placement.h>
#include <drm/qxl_drm.h>

#include "qxl_dev.h"

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

#define DRM_FILE_OFFSET 0x100000000ULL
#define DRM_FILE_PAGE_OFFSET (DRM_FILE_OFFSET >> PAGE_SHIFT)

#define QXL_INTERRUPT_MASK (\
	QXL_INTERRUPT_DISPLAY |\
	QXL_INTERRUPT_CURSOR |\
	QXL_INTERRUPT_IO_CMD |\
	QXL_INTERRUPT_CLIENT_MONITORS_CONFIG)

struct qxl_bo {
	/* Protected by gem.mutex */
	struct list_head		list;
	/* Protected by tbo.reserved */
	struct ttm_place		placements[3];
	struct ttm_placement		placement;
	struct ttm_buffer_object	tbo;
	struct ttm_bo_kmap_obj		kmap;
	unsigned			pin_count;
	void				*kptr;
	int                             type;

	/* Constant after initialization */
	struct drm_gem_object		gem_base;
	bool is_primary; /* is this now a primary surface */
	bool is_dumb;
	struct qxl_bo *shadow;
	bool hw_surf_alloc;
	struct qxl_surface surf;
	uint32_t surface_id;
	struct qxl_release *surf_create;
};
#define gem_to_qxl_bo(gobj) container_of((gobj), struct qxl_bo, gem_base)
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

struct qxl_framebuffer {
	struct drm_framebuffer base;
	struct drm_gem_object *obj;
};

#define to_qxl_crtc(x) container_of(x, struct qxl_crtc, base)
#define drm_connector_to_qxl_output(x) container_of(x, struct qxl_output, base)
#define drm_encoder_to_qxl_output(x) container_of(x, struct qxl_output, enc)
#define to_qxl_framebuffer(x) container_of(x, struct qxl_framebuffer, base)

struct qxl_mman {
	struct ttm_bo_global_ref        bo_global_ref;
	struct drm_global_reference	mem_global_ref;
	bool				mem_global_referenced;
	struct ttm_bo_device		bdev;
};

struct qxl_mode_info {
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
	struct dma_fence base;

	int id;
	int type;
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
	unsigned		num_files;
};

int qxl_debugfs_add_files(struct qxl_device *rdev,
			     struct drm_info_list *files,
			     unsigned nfiles);
int qxl_debugfs_fence_init(struct qxl_device *rdev);

struct qxl_device;

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

	struct work_struct gc_work;

	struct drm_property *hotplug_mode_update_property;
	int monitors_config_width;
	int monitors_config_height;
};

extern const struct drm_ioctl_desc qxl_ioctls[];
extern int qxl_max_ioctl;

int qxl_device_init(struct qxl_device *qdev, struct drm_driver *drv,
		    struct pci_dev *pdev);
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

static inline void *
qxl_fb_virtual_address(struct qxl_device *qdev, unsigned long physical)
{
	DRM_DEBUG_DRIVER("not implemented (%lu)\n", physical);
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
void qxl_fbdev_set_suspend(struct qxl_device *qdev, int state);

/* qxl_display.c */
void qxl_user_framebuffer_destroy(struct drm_framebuffer *fb);
int
qxl_framebuffer_init(struct drm_device *dev,
		     struct qxl_framebuffer *rfb,
		     const struct drm_mode_fb_cmd2 *mode_cmd,
		     struct drm_gem_object *obj,
		     const struct drm_framebuffer_funcs *funcs);
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
			   unsigned offset,
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

void qxl_release_free(struct qxl_device *qdev,
		      struct qxl_release *release);

/* used by qxl_debugfs_release */
struct qxl_release *qxl_release_from_id_locked(struct qxl_device *qdev,
						   uint64_t id);

bool qxl_queue_garbage_collect(struct qxl_device *qdev, bool flush);
int qxl_garbage_collect(struct qxl_device *qdev);

/* debugfs */

int qxl_debugfs_init(struct drm_minor *minor);
int qxl_ttm_debugfs_init(struct qxl_device *qdev);

/* qxl_prime.c */
int qxl_gem_prime_pin(struct drm_gem_object *obj);
void qxl_gem_prime_unpin(struct drm_gem_object *obj);
struct sg_table *qxl_gem_prime_get_sg_table(struct drm_gem_object *obj);
struct drm_gem_object *qxl_gem_prime_import_sg_table(
	struct drm_device *dev, struct dma_buf_attachment *attach,
	struct sg_table *sgt);
void *qxl_gem_prime_vmap(struct drm_gem_object *obj);
void qxl_gem_prime_vunmap(struct drm_gem_object *obj, void *vaddr);
int qxl_gem_prime_mmap(struct drm_gem_object *obj,
				struct vm_area_struct *vma);

/* qxl_irq.c */
int qxl_irq_init(struct qxl_device *qdev);
irqreturn_t qxl_irq_handler(int irq, void *arg);

/* qxl_fb.c */
bool qxl_fbdev_qobj_is_fb(struct qxl_device *qdev, struct qxl_bo *qobj);

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

#endif
