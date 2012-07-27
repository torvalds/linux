#ifndef __NOUVEAU_SOFTWARE_H__
#define __NOUVEAU_SOFTWARE_H__

struct nouveau_software_priv {
	struct nouveau_exec_engine base;
	struct list_head vblank;
	spinlock_t peephole_lock;
};

struct nouveau_software_chan {
	struct list_head flip;
	struct {
		struct list_head list;
		u32 channel;
		u32 ctxdma;
		u32 offset;
		u32 value;
		u32 head;
	} vblank;
};

static inline void
nouveau_software_context_new(struct nouveau_software_chan *pch)
{
	INIT_LIST_HEAD(&pch->flip);
	INIT_LIST_HEAD(&pch->vblank.list);
}

static inline void
nouveau_software_create(struct nouveau_software_priv *psw)
{
	INIT_LIST_HEAD(&psw->vblank);
	spin_lock_init(&psw->peephole_lock);
}

static inline u16
nouveau_software_class(struct drm_device *dev)
{
	struct drm_nouveau_private *dev_priv = dev->dev_private;
	if (dev_priv->card_type <= NV_04)
		return 0x006e;
	if (dev_priv->card_type <= NV_40)
		return 0x016e;
	if (dev_priv->card_type <= NV_50)
		return 0x506e;
	if (dev_priv->card_type <= NV_E0)
		return 0x906e;
	return 0x0000;
}

int nv04_software_create(struct drm_device *);
int nv50_software_create(struct drm_device *);
int nvc0_software_create(struct drm_device *);
u64 nvc0_software_crtc(struct nouveau_channel *, int crtc);

#endif
