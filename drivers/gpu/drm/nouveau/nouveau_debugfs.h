#ifndef __NOUVEAU_DEBUGFS_H__
#define __NOUVEAU_DEBUGFS_H__

#include <drm/drmP.h>

#if defined(CONFIG_DEBUG_FS)

#include "nouveau_drm.h"

struct nouveau_debugfs {
	struct nvif_object ctrl;
};

static inline struct nouveau_debugfs *
nouveau_debugfs(struct drm_device *dev)
{
	return nouveau_drm(dev)->debugfs;
}

extern int  nouveau_drm_debugfs_init(struct drm_minor *);
extern void nouveau_drm_debugfs_cleanup(struct drm_minor *);
extern int  nouveau_debugfs_init(struct nouveau_drm *);
extern void nouveau_debugfs_fini(struct nouveau_drm *);
#else
static inline int
nouveau_drm_debugfs_init(struct drm_minor *minor)
{
       return 0;
}

static inline void
nouveau_drm_debugfs_cleanup(struct drm_minor *minor)
{
}

static inline int
nouveau_debugfs_init(struct nouveau_drm *drm)
{
	return 0;
}

static inline void
nouveau_debugfs_fini(struct nouveau_drm *drm)
{
}

#endif

#endif
