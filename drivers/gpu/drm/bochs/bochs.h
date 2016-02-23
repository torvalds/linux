#include <linux/io.h>
#include <linux/fb.h>
#include <linux/console.h>

#include <drm/drmP.h>
#include <drm/drm_crtc.h>
#include <drm/drm_crtc_helper.h>
#include <drm/drm_fb_helper.h>

#include <drm/drm_gem.h>

#include <ttm/ttm_bo_driver.h>
#include <ttm/ttm_page_alloc.h>

/* ---------------------------------------------------------------------- */

#define VBE_DISPI_IOPORT_INDEX           0x01CE
#define VBE_DISPI_IOPORT_DATA            0x01CF

#define VBE_DISPI_INDEX_ID               0x0
#define VBE_DISPI_INDEX_XRES             0x1
#define VBE_DISPI_INDEX_YRES             0x2
#define VBE_DISPI_INDEX_BPP              0x3
#define VBE_DISPI_INDEX_ENABLE           0x4
#define VBE_DISPI_INDEX_BANK             0x5
#define VBE_DISPI_INDEX_VIRT_WIDTH       0x6
#define VBE_DISPI_INDEX_VIRT_HEIGHT      0x7
#define VBE_DISPI_INDEX_X_OFFSET         0x8
#define VBE_DISPI_INDEX_Y_OFFSET         0x9
#define VBE_DISPI_INDEX_VIDEO_MEMORY_64K 0xa

#define VBE_DISPI_ID0                    0xB0C0
#define VBE_DISPI_ID1                    0xB0C1
#define VBE_DISPI_ID2                    0xB0C2
#define VBE_DISPI_ID3                    0xB0C3
#define VBE_DISPI_ID4                    0xB0C4
#define VBE_DISPI_ID5                    0xB0C5

#define VBE_DISPI_DISABLED               0x00
#define VBE_DISPI_ENABLED                0x01
#define VBE_DISPI_GETCAPS                0x02
#define VBE_DISPI_8BIT_DAC               0x20
#define VBE_DISPI_LFB_ENABLED            0x40
#define VBE_DISPI_NOCLEARMEM             0x80

/* ---------------------------------------------------------------------- */

enum bochs_types {
	BOCHS_QEMU_STDVGA,
	BOCHS_UNKNOWN,
};

struct bochs_framebuffer {
	struct drm_framebuffer base;
	struct drm_gem_object *obj;
};

struct bochs_device {
	/* hw */
	void __iomem   *mmio;
	int            ioports;
	void __iomem   *fb_map;
	unsigned long  fb_base;
	unsigned long  fb_size;

	/* mode */
	u16 xres;
	u16 yres;
	u16 yres_virtual;
	u32 stride;
	u32 bpp;

	/* drm */
	struct drm_device  *dev;
	struct drm_crtc crtc;
	struct drm_encoder encoder;
	struct drm_connector connector;
	bool mode_config_initialized;

	/* ttm */
	struct {
		struct drm_global_reference mem_global_ref;
		struct ttm_bo_global_ref bo_global_ref;
		struct ttm_bo_device bdev;
		bool initialized;
	} ttm;

	/* fbdev */
	struct {
		struct bochs_framebuffer gfb;
		struct drm_fb_helper helper;
		int size;
		bool initialized;
	} fb;
};

#define to_bochs_framebuffer(x) container_of(x, struct bochs_framebuffer, base)

struct bochs_bo {
	struct ttm_buffer_object bo;
	struct ttm_placement placement;
	struct ttm_bo_kmap_obj kmap;
	struct drm_gem_object gem;
	struct ttm_place placements[3];
	int pin_count;
};

static inline struct bochs_bo *bochs_bo(struct ttm_buffer_object *bo)
{
	return container_of(bo, struct bochs_bo, bo);
}

static inline struct bochs_bo *gem_to_bochs_bo(struct drm_gem_object *gem)
{
	return container_of(gem, struct bochs_bo, gem);
}

#define DRM_FILE_PAGE_OFFSET (0x100000000ULL >> PAGE_SHIFT)

static inline u64 bochs_bo_mmap_offset(struct bochs_bo *bo)
{
	return drm_vma_node_offset_addr(&bo->bo.vma_node);
}

/* ---------------------------------------------------------------------- */

/* bochs_hw.c */
int bochs_hw_init(struct drm_device *dev, uint32_t flags);
void bochs_hw_fini(struct drm_device *dev);

void bochs_hw_setmode(struct bochs_device *bochs,
		      struct drm_display_mode *mode);
void bochs_hw_setbase(struct bochs_device *bochs,
		      int x, int y, u64 addr);

/* bochs_mm.c */
int bochs_mm_init(struct bochs_device *bochs);
void bochs_mm_fini(struct bochs_device *bochs);
int bochs_mmap(struct file *filp, struct vm_area_struct *vma);

int bochs_gem_create(struct drm_device *dev, u32 size, bool iskernel,
		     struct drm_gem_object **obj);
int bochs_gem_init_object(struct drm_gem_object *obj);
void bochs_gem_free_object(struct drm_gem_object *obj);
int bochs_dumb_create(struct drm_file *file, struct drm_device *dev,
		      struct drm_mode_create_dumb *args);
int bochs_dumb_mmap_offset(struct drm_file *file, struct drm_device *dev,
			   uint32_t handle, uint64_t *offset);

int bochs_framebuffer_init(struct drm_device *dev,
			   struct bochs_framebuffer *gfb,
			   const struct drm_mode_fb_cmd2 *mode_cmd,
			   struct drm_gem_object *obj);
int bochs_bo_pin(struct bochs_bo *bo, u32 pl_flag, u64 *gpu_addr);
int bochs_bo_unpin(struct bochs_bo *bo);

extern const struct drm_mode_config_funcs bochs_mode_funcs;

/* bochs_kms.c */
int bochs_kms_init(struct bochs_device *bochs);
void bochs_kms_fini(struct bochs_device *bochs);

/* bochs_fbdev.c */
int bochs_fbdev_init(struct bochs_device *bochs);
void bochs_fbdev_fini(struct bochs_device *bochs);
