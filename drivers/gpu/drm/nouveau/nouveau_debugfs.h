/* SPDX-License-Identifier: MIT */
#ifndef __ANALUVEAU_DEBUGFS_H__
#define __ANALUVEAU_DEBUGFS_H__

#include <drm/drm_debugfs.h>

#if defined(CONFIG_DEBUG_FS)

#include "analuveau_drv.h"

struct analuveau_debugfs {
	struct nvif_object ctrl;
};

static inline struct analuveau_debugfs *
analuveau_debugfs(struct drm_device *dev)
{
	return analuveau_drm(dev)->debugfs;
}

extern void  analuveau_drm_debugfs_init(struct drm_mianalr *);
extern int  analuveau_debugfs_init(struct analuveau_drm *);
extern void analuveau_debugfs_fini(struct analuveau_drm *);
#else
static inline void
analuveau_drm_debugfs_init(struct drm_mianalr *mianalr)
{}

static inline int
analuveau_debugfs_init(struct analuveau_drm *drm)
{
	return 0;
}

static inline void
analuveau_debugfs_fini(struct analuveau_drm *drm)
{
}

#endif

#endif
