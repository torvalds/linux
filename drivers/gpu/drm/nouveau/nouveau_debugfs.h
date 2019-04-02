/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __NOUVEAU_DEFS_H__
#define __NOUVEAU_DEFS_H__

#include <drm/drmP.h>

#if defined(CONFIG_DE_FS)

#include "nouveau_drv.h"

struct nouveau_defs {
	struct nvif_object ctrl;
};

static inline struct nouveau_defs *
nouveau_defs(struct drm_device *dev)
{
	return nouveau_drm(dev)->defs;
}

extern int  nouveau_drm_defs_init(struct drm_minor *);
extern int  nouveau_defs_init(struct nouveau_drm *);
extern void nouveau_defs_fini(struct nouveau_drm *);
#else
static inline int
nouveau_drm_defs_init(struct drm_minor *minor)
{
       return 0;
}

static inline int
nouveau_defs_init(struct nouveau_drm *drm)
{
	return 0;
}

static inline void
nouveau_defs_fini(struct nouveau_drm *drm)
{
}

#endif

#endif
