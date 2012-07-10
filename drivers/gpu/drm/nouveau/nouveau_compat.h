#ifndef __NOUVEAU_COMPAT_H__
#define __NOUVEAU_COMPAT_H__

u8   _nv_rd08(struct drm_device *, u32);
void _nv_wr08(struct drm_device *, u32, u8);
u32  _nv_rd32(struct drm_device *, u32);
void _nv_wr32(struct drm_device *, u32, u32);
u32  _nv_mask(struct drm_device *, u32, u32, u32);

bool _nv_bios(struct drm_device *, u8 **, u32 *);

#endif
