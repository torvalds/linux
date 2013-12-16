/*
 * Copyright 2012 Red Hat
 *
 * This file is subject to the terms and conditions of the GNU General
 * Public License version 2. See the file COPYING in the main
 * directory of this archive for more details.
 *
 * Authors: Matthew Garrett
 *          Dave Airlie
 */
#ifndef __CIRRUS_DRV_H__
#define __CIRRUS_DRV_H__

#include <video/vga.h>

#include <drm/drm_fb_helper.h>

#include <drm/ttm/ttm_bo_api.h>
#include <drm/ttm/ttm_bo_driver.h>
#include <drm/ttm/ttm_placement.h>
#include <drm/ttm/ttm_memory.h>
#include <drm/ttm/ttm_module.h>

#define DRIVER_AUTHOR		"Matthew Garrett"

#define DRIVER_NAME		"cirrus"
#define DRIVER_DESC		"qemu Cirrus emulation"
#define DRIVER_DATE		"20110418"

#define DRIVER_MAJOR		1
#define DRIVER_MINOR		0
#define DRIVER_PATCHLEVEL	0

#define CIRRUSFB_CONN_LIMIT 1

#define RREG8(reg) ioread8(((void __iomem *)cdev->rmmio) + (reg))
#define WREG8(reg, v) iowrite8(v, ((void __iomem *)cdev->rmmio) + (reg))
#define RREG32(reg) ioread32(((void __iomem *)cdev->rmmio) + (reg))
#define WREG32(reg, v) iowrite32(v, ((void __iomem *)cdev->rmmio) + (reg))

#define SEQ_INDEX 4
#define SEQ_DATA 5

#define WREG_SEQ(reg, v)					\
	do {							\
		WREG8(SEQ_INDEX, reg);				\
		WREG8(SEQ_DATA, v);				\
	} while (0)						\

#define CRT_INDEX 0x14
#define CRT_DATA 0x15

#define WREG_CRT(reg, v)					\
	do {							\
		WREG8(CRT_INDEX, reg);				\
		WREG8(CRT_DATA, v);				\
	} while (0)						\

#define GFX_INDEX 0xe
#define GFX_DATA 0xf

#define WREG_GFX(reg, v)					\
	do {							\
		WREG8(GFX_INDEX, reg);				\
		WREG8(GFX_DATA, v);				\
	} while (0)						\

/*
 * Cirrus has a "hidden" DAC register that can be accessed by writing to
 * the pixel mask register to reset the state, then reading from the register
 * four times. The next write will then pass to the DAC
 */
#define VGA_DAC_MASK 0x6

#define WREG_HDR(v)						\
	do {							\
		RREG8(VGA_DAC_MASK);					\
		RREG8(VGA_DAC_MASK);					\
		RREG8(VGA_DAC_MASK);					\
		RREG8(VGA_DAC_MASK);					\
		WREG8(VGA_DAC_MASK, v);					\
	} while (0)						\


#define CIRRUS_MAX_FB_HEIGHT 4096
#define CIRRUS_MAX_FB_WIDTH 4096

#define CIRRUS_DPMS_CLEARED (-1)

#define to_cirrus_crtc(x) container_of(x, struct cirrus_crtc, base)
#define to_cirrus_encoder(x) container_of(x, struct cirrus_encoder, base)
#define to_cirrus_framebuffer(x) container_of(x, struct cirrus_framebuffer, base)

struct cirrus_crtc {
	struct drm_crtc			base;
	u8				lut_r[256], lut_g[256], lut_b[256];
	int				last_dpms;
	bool				enabled;
};

struct cirrus_fbdev;
struct cirrus_mode_info {
	bool				mode_config_initialized;
	struct cirrus_crtc		*crtc;
	/* pointer to fbdev info structure */
	struct cirrus_fbdev		*gfbdev;
};

struct cirrus_encoder {
	struct drm_encoder		base;
	int				last_dpms;
};

struct cirrus_connector {
	struct drm_connector		base;
};

struct cirrus_framebuffer {
	struct drm_framebuffer		base;
	struct drm_gem_object *obj;
};

struct cirrus_mc {
	resource_size_t			vram_size;
	resource_size_t			vram_base;
};

struct cirrus_device {
	struct drm_device		*dev;
	unsigned long			flags;

	resource_size_t			rmmio_base;
	resource_size_t			rmmio_size;
	void __iomem			*rmmio;

	struct cirrus_mc			mc;
	struct cirrus_mode_info		mode_info;

	int				num_crtc;
	int fb_mtrr;

	struct {
		struct drm_global_reference mem_global_ref;
		struct ttm_bo_global_ref bo_global_ref;
		struct ttm_bo_device bdev;
	} ttm;
	bool mm_inited;
};


struct cirrus_fbdev {
	struct drm_fb_helper helper;
	struct cirrus_framebuffer gfb;
	struct list_head fbdev_list;
	void *sysram;
	int size;
	int x1, y1, x2, y2; /* dirty rect */
	spinlock_t dirty_lock;
};

struct cirrus_bo {
	struct ttm_buffer_object bo;
	struct ttm_placement placement;
	struct ttm_bo_kmap_obj kmap;
	struct drm_gem_object gem;
	u32 placements[3];
	int pin_count;
};
#define gem_to_cirrus_bo(gobj) container_of((gobj), struct cirrus_bo, gem)

static inline struct cirrus_bo *
cirrus_bo(struct ttm_buffer_object *bo)
{
	return container_of(bo, struct cirrus_bo, bo);
}


#define to_cirrus_obj(x) container_of(x, struct cirrus_gem_object, base)
#define DRM_FILE_PAGE_OFFSET (0x100000000ULL >> PAGE_SHIFT)

				/* cirrus_mode.c */
void cirrus_crtc_fb_gamma_set(struct drm_crtc *crtc, u16 red, u16 green,
			     u16 blue, int regno);
void cirrus_crtc_fb_gamma_get(struct drm_crtc *crtc, u16 *red, u16 *green,
			     u16 *blue, int regno);


				/* cirrus_main.c */
int cirrus_device_init(struct cirrus_device *cdev,
		      struct drm_device *ddev,
		      struct pci_dev *pdev,
		      uint32_t flags);
void cirrus_device_fini(struct cirrus_device *cdev);
void cirrus_gem_free_object(struct drm_gem_object *obj);
int cirrus_dumb_mmap_offset(struct drm_file *file,
			    struct drm_device *dev,
			    uint32_t handle,
			    uint64_t *offset);
int cirrus_gem_create(struct drm_device *dev,
		   u32 size, bool iskernel,
		      struct drm_gem_object **obj);
int cirrus_dumb_create(struct drm_file *file,
		    struct drm_device *dev,
		       struct drm_mode_create_dumb *args);

int cirrus_framebuffer_init(struct drm_device *dev,
			   struct cirrus_framebuffer *gfb,
			    struct drm_mode_fb_cmd2 *mode_cmd,
			    struct drm_gem_object *obj);

				/* cirrus_display.c */
int cirrus_modeset_init(struct cirrus_device *cdev);
void cirrus_modeset_fini(struct cirrus_device *cdev);

				/* cirrus_fbdev.c */
int cirrus_fbdev_init(struct cirrus_device *cdev);
void cirrus_fbdev_fini(struct cirrus_device *cdev);



				/* cirrus_irq.c */
void cirrus_driver_irq_preinstall(struct drm_device *dev);
int cirrus_driver_irq_postinstall(struct drm_device *dev);
void cirrus_driver_irq_uninstall(struct drm_device *dev);
irqreturn_t cirrus_driver_irq_handler(DRM_IRQ_ARGS);

				/* cirrus_kms.c */
int cirrus_driver_load(struct drm_device *dev, unsigned long flags);
int cirrus_driver_unload(struct drm_device *dev);
extern struct drm_ioctl_desc cirrus_ioctls[];
extern int cirrus_max_ioctl;

int cirrus_mm_init(struct cirrus_device *cirrus);
void cirrus_mm_fini(struct cirrus_device *cirrus);
void cirrus_ttm_placement(struct cirrus_bo *bo, int domain);
int cirrus_bo_create(struct drm_device *dev, int size, int align,
		     uint32_t flags, struct cirrus_bo **pcirrusbo);
int cirrus_mmap(struct file *filp, struct vm_area_struct *vma);

static inline int cirrus_bo_reserve(struct cirrus_bo *bo, bool no_wait)
{
	int ret;

	ret = ttm_bo_reserve(&bo->bo, true, no_wait, false, 0);
	if (ret) {
		if (ret != -ERESTARTSYS && ret != -EBUSY)
			DRM_ERROR("reserve failed %p\n", bo);
		return ret;
	}
	return 0;
}

static inline void cirrus_bo_unreserve(struct cirrus_bo *bo)
{
	ttm_bo_unreserve(&bo->bo);
}

int cirrus_bo_push_sysram(struct cirrus_bo *bo);
int cirrus_bo_pin(struct cirrus_bo *bo, u32 pl_flag, u64 *gpu_addr);
#endif				/* __CIRRUS_DRV_H__ */
