#ifndef __NOUVEAU_SOFTWARE_H__
#define __NOUVEAU_SOFTWARE_H__

struct nouveau_software_priv {
	struct nouveau_exec_engine base;
	struct list_head vblank;
};

struct nouveau_software_chan {
	struct list_head flip;
	struct {
		struct list_head list;
		struct nouveau_bo *bo;
		u32 offset;
		u32 value;
		u32 head;
	} vblank;
};

static inline void
nouveau_software_vblank(struct drm_device *dev, int crtc)
{
	struct nouveau_software_priv *psw = nv_engine(dev, NVOBJ_ENGINE_SW);
	struct nouveau_software_chan *pch, *tmp;

	list_for_each_entry_safe(pch, tmp, &psw->vblank, vblank.list) {
		if (pch->vblank.head != crtc)
			continue;

		nouveau_bo_wr32(pch->vblank.bo, pch->vblank.offset,
						pch->vblank.value);
		list_del(&pch->vblank.list);
		drm_vblank_put(dev, crtc);
	}
}

static inline void
nouveau_software_context_new(struct nouveau_software_chan *pch)
{
	INIT_LIST_HEAD(&pch->flip);
}

static inline void
nouveau_software_create(struct nouveau_software_priv *psw)
{
	INIT_LIST_HEAD(&psw->vblank);
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

#endif
