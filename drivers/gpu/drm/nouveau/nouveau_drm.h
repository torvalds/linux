#ifndef __NOUVEAU_DRMCLI_H__
#define __NOUVEAU_DRMCLI_H__

#define DRIVER_AUTHOR		"Nouveau Project"
#define DRIVER_EMAIL		"nouveau@lists.freedesktop.org"

#define DRIVER_NAME		"nouveau"
#define DRIVER_DESC		"nVidia Riva/TNT/GeForce/Quadro/Tesla"
#define DRIVER_DATE		"20120801"

#define DRIVER_MAJOR		1
#define DRIVER_MINOR		1
#define DRIVER_PATCHLEVEL	0

#include <core/client.h>
#include <core/event.h>

#include <subdev/vm.h>

#include <drmP.h>
#include <drm/nouveau_drm.h>

#include <drm/ttm/ttm_bo_api.h>
#include <drm/ttm/ttm_bo_driver.h>
#include <drm/ttm/ttm_placement.h>
#include <drm/ttm/ttm_memory.h>
#include <drm/ttm/ttm_module.h>
#include <drm/ttm/ttm_page_alloc.h>

struct nouveau_channel;

#define DRM_FILE_PAGE_OFFSET (0x100000000ULL >> PAGE_SHIFT)

#include "nouveau_fence.h"
#include "nouveau_bios.h"

struct nouveau_drm_tile {
	struct nouveau_fence *fence;
	bool used;
};

enum nouveau_drm_handle {
	NVDRM_CLIENT = 0xffffffff,
	NVDRM_DEVICE = 0xdddddddd,
	NVDRM_PUSH   = 0xbbbb0000, /* |= client chid */
	NVDRM_CHAN   = 0xcccc0000, /* |= client chid */
};

struct nouveau_cli {
	struct nouveau_client base;
	struct list_head head;
	struct mutex mutex;
	void *abi16;
};

static inline struct nouveau_cli *
nouveau_cli(struct drm_file *fpriv)
{
	return fpriv ? fpriv->driver_priv : NULL;
}

struct nouveau_drm {
	struct nouveau_cli client;
	struct drm_device *dev;

	struct nouveau_object *device;
	struct list_head clients;

	struct {
		enum {
			UNKNOWN = 0,
			DISABLE = 1,
			ENABLED = 2
		} stat;
		u32 base;
		u32 size;
	} agp;

	/* TTM interface support */
	struct {
		struct drm_global_reference mem_global_ref;
		struct ttm_bo_global_ref bo_global_ref;
		struct ttm_bo_device bdev;
		atomic_t validate_sequence;
		int (*move)(struct nouveau_channel *,
			    struct ttm_buffer_object *,
			    struct ttm_mem_reg *, struct ttm_mem_reg *);
		int mtrr;
	} ttm;

	/* GEM interface support */
	struct {
		u64 vram_available;
		u64 gart_available;
	} gem;

	/* synchronisation */
	void *fence;

	/* context for accelerated drm-internal operations */
	struct nouveau_channel *cechan;
	struct nouveau_channel *channel;
	struct nouveau_gpuobj *notify;
	struct nouveau_fbdev *fbcon;

	/* nv10-nv40 tiling regions */
	struct {
		struct nouveau_drm_tile reg[15];
		spinlock_t lock;
	} tile;

	/* modesetting */
	struct nvbios vbios;
	struct nouveau_display *display;
	struct backlight_device *backlight;
	struct nouveau_eventh vblank;

	/* power management */
	struct nouveau_pm *pm;
};

static inline struct nouveau_drm *
nouveau_drm(struct drm_device *dev)
{
	return dev->dev_private;
}

static inline struct nouveau_device *
nouveau_dev(struct drm_device *dev)
{
	return nv_device(nouveau_drm(dev)->device);
}

int nouveau_pmops_suspend(struct device *);
int nouveau_pmops_resume(struct device *);

#define NV_FATAL(cli, fmt, args...) nv_fatal((cli), fmt, ##args)
#define NV_ERROR(cli, fmt, args...) nv_error((cli), fmt, ##args)
#define NV_WARN(cli, fmt, args...) nv_warn((cli), fmt, ##args)
#define NV_INFO(cli, fmt, args...) nv_info((cli), fmt, ##args)
#define NV_DEBUG(cli, fmt, args...) do {                                       \
	if (drm_debug & DRM_UT_DRIVER)                                         \
		nv_info((cli), fmt, ##args);                                   \
} while (0)

extern int nouveau_modeset;

#endif
