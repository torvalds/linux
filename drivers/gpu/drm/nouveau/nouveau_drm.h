#ifndef __NOUVEAU_DRMCLI_H__
#define __NOUVEAU_DRMCLI_H__

#include <core/client.h>

#include <subdev/vm.h>

#include <drmP.h>
#include <drm/nouveau_drm.h>

#include "ttm/ttm_bo_api.h"
#include "ttm/ttm_bo_driver.h"
#include "ttm/ttm_placement.h"
#include "ttm/ttm_memory.h"
#include "ttm/ttm_module.h"
#include "ttm/ttm_page_alloc.h"

struct nouveau_channel;

#define DRM_FILE_PAGE_OFFSET (0x100000000ULL >> PAGE_SHIFT)

#include "nouveau_revcompat.h"
#include "nouveau_fence.h"

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
	struct nouveau_channel *channel;
	struct nouveau_gpuobj *notify;
	struct nouveau_fbdev *fbcon;

	/* nv10-nv40 tiling regions */
	struct {
		struct nouveau_drm_tile reg[15];
		spinlock_t lock;
	} tile;
};

static inline struct nouveau_drm *
nouveau_drm(struct drm_device *dev)
{
	return nouveau_newpriv(dev);
}

int nouveau_drm_suspend(struct pci_dev *, pm_message_t);
int nouveau_drm_resume(struct pci_dev *);

#define NV_PRINTK(level, code, drm, fmt, args...)                              \
	printk(level "nouveau " code "[     DRM][%s] " fmt,                    \
	       pci_name((drm)->dev->pdev), ##args)
#define NV_FATAL(drm, fmt, args...)                                            \
	NV_PRINTK(KERN_CRIT, "!", (drm), fmt, ##args)
#define NV_ERROR(drm, fmt, args...)                                            \
	NV_PRINTK(KERN_ERR, "E", (drm), fmt, ##args)
#define NV_WARN(drm, fmt, args...)                                             \
	NV_PRINTK(KERN_WARNING, "W", (drm), fmt, ##args)
#define NV_INFO(drm, fmt, args...)                                             \
	NV_PRINTK(KERN_INFO, " ", (drm), fmt, ##args)
#define NV_DEBUG(drm, fmt, args...) do {                                       \
	if (drm_debug & DRM_UT_DRIVER)                                         \
		NV_PRINTK(KERN_DEBUG, "D", drm, fmt, ##args);                  \
} while (0)

#endif
