#ifndef __NOUVEAU_IRQ_H__
#define __NOUVEAU_IRQ_H__

extern int         nouveau_irq_init(struct drm_device *);
extern void        nouveau_irq_fini(struct drm_device *);
extern irqreturn_t nouveau_irq_handler(DRM_IRQ_ARGS);
extern void        nouveau_irq_preinstall(struct drm_device *);
extern int         nouveau_irq_postinstall(struct drm_device *);
extern void        nouveau_irq_uninstall(struct drm_device *);

#endif
