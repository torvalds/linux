/* SPDX-License-Identifier: MIT */
#ifndef __NOUVEAU_CHAN_H__
#define __NOUVEAU_CHAN_H__
#include <nvif/object.h>
#include <nvif/notify.h>
struct nvif_device;

struct nouveau_channel {
	struct nvif_device *device;
	struct nouveau_drm *drm;
	struct nouveau_vmm *vmm;

	int chid;
	u64 inst;
	u32 token;

	struct nvif_object vram;
	struct nvif_object gart;
	struct nvif_object nvsw;

	struct {
		struct nouveau_bo *buffer;
		struct nouveau_vma *vma;
		struct nvif_object ctxdma;
		u64 addr;
	} push;

	/* TODO: this will be reworked in the near future */
	bool accel_done;
	void *fence;
	struct {
		int max;
		int free;
		int cur;
		int put;
		int ib_base;
		int ib_max;
		int ib_free;
		int ib_put;
	} dma;
	u32 user_get_hi;
	u32 user_get;
	u32 user_put;

	struct nvif_object user;

	struct nvif_notify kill;
	atomic_t killed;
};

int nouveau_channels_init(struct nouveau_drm *);

int  nouveau_channel_new(struct nouveau_drm *, struct nvif_device *,
			 u32 arg0, u32 arg1, bool priv,
			 struct nouveau_channel **);
void nouveau_channel_del(struct nouveau_channel **);
int  nouveau_channel_idle(struct nouveau_channel *);

extern int nouveau_vram_pushbuf;

#endif
