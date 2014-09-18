#ifndef __NOUVEAU_AGP_H__
#define __NOUVEAU_AGP_H__

struct nouveau_drm;

void nouveau_agp_reset(struct nouveau_drm *);
void nouveau_agp_init(struct nouveau_drm *);
void nouveau_agp_fini(struct nouveau_drm *);

#endif
