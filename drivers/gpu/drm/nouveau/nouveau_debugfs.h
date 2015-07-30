#ifndef __NOUVEAU_DEBUGFS_H__
#define __NOUVEAU_DEBUGFS_H__

#include <drm/drmP.h>

#if defined(CONFIG_DEBUG_FS)
extern int  nouveau_drm_debugfs_init(struct drm_minor *);
extern void nouveau_drm_debugfs_cleanup(struct drm_minor *);
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

#endif

#endif
