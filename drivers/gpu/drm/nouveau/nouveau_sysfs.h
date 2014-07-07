#ifndef __NOUVEAU_SYSFS_H__
#define __NOUVEAU_SYSFS_H__

#include "nouveau_drm.h"

struct nouveau_sysfs {
	struct nouveau_object *ctrl;
};

static inline struct nouveau_sysfs *
nouveau_sysfs(struct drm_device *dev)
{
	return nouveau_drm(dev)->sysfs;
}

int  nouveau_sysfs_init(struct drm_device *);
void nouveau_sysfs_fini(struct drm_device *);

#endif
