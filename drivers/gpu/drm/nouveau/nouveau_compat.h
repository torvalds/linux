#ifndef __NOUVEAU_COMPAT_H__
#define __NOUVEAU_COMPAT_H__

u8   _nv_rd08(struct drm_device *, u32);
void _nv_wr08(struct drm_device *, u32, u8);
u32  _nv_rd32(struct drm_device *, u32);
void _nv_wr32(struct drm_device *, u32, u32);
u32  _nv_mask(struct drm_device *, u32, u32, u32);

bool _nv_bios(struct drm_device *, u8 **, u32 *);

struct dcb_gpio_func;
void nouveau_gpio_reset(struct drm_device *);
int  nouveau_gpio_find(struct drm_device *, int, u8, u8, struct dcb_gpio_func *);
bool nouveau_gpio_func_valid(struct drm_device *, u8 tag);
int  nouveau_gpio_func_set(struct drm_device *, u8 tag, int state);
int  nouveau_gpio_func_get(struct drm_device *, u8 tag);
int  nouveau_gpio_irq(struct drm_device *, int idx, u8 tag, u8 line, bool on);
int  nouveau_gpio_isr_add(struct drm_device *, int idx, u8 tag, u8 line,
			  void (*)(void *, int state), void *data);
void nouveau_gpio_isr_del(struct drm_device *, int idx, u8 tag, u8 line,
			  void (*)(void *, int state), void *data);
#endif
