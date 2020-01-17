/* SPDX-License-Identifier: MIT */
#ifndef __NOUVEAU_DEBUGFS_H__
#define __NOUVEAU_DEBUGFS_H__

#include <drm/drm_debugfs.h>

#if defined(CONFIG_DEBUG_FS)

#include "yesuveau_drv.h"

struct yesuveau_debugfs {
	struct nvif_object ctrl;
};

static inline struct yesuveau_debugfs *
yesuveau_debugfs(struct drm_device *dev)
{
	return yesuveau_drm(dev)->debugfs;
}

extern int  yesuveau_drm_debugfs_init(struct drm_miyesr *);
extern int  yesuveau_debugfs_init(struct yesuveau_drm *);
extern void yesuveau_debugfs_fini(struct yesuveau_drm *);
#else
static inline int
yesuveau_drm_debugfs_init(struct drm_miyesr *miyesr)
{
       return 0;
}

static inline int
yesuveau_debugfs_init(struct yesuveau_drm *drm)
{
	return 0;
}

static inline void
yesuveau_debugfs_fini(struct yesuveau_drm *drm)
{
}

#endif

#endif
