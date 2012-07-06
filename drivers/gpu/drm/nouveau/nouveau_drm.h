#ifndef __NOUVEAU_DRMCLI_H__
#define __NOUVEAU_DRMCLI_H__

#include <core/client.h>

#include <drmP.h>
#include <drm/nouveau_drm.h>

enum nouveau_drm_handle {
	NVDRM_CLIENT = 0xffffffff,
	NVDRM_DEVICE = 0xdddddddd,
};

struct nouveau_cli {
	struct nouveau_client base;
	struct list_head head;
	struct mutex mutex;
};

struct nouveau_drm {
	struct nouveau_cli client;
	struct drm_device *dev;

	struct nouveau_object *device;
	struct list_head clients;
};

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
