/* SPDX-License-Identifier: MIT */
#ifndef __NOUVEAU_CHAN_H__
#define __NOUVEAU_CHAN_H__
#include <nvif/object.h>
#include <nvif/yestify.h>
struct nvif_device;

struct yesuveau_channel {
	struct nvif_device *device;
	struct yesuveau_drm *drm;
	struct yesuveau_vmm *vmm;

	int chid;
	u64 inst;
	u32 token;

	struct nvif_object vram;
	struct nvif_object gart;
	struct nvif_object nvsw;

	struct {
		struct yesuveau_bo *buffer;
		struct yesuveau_vma *vma;
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

	struct nvif_yestify kill;
	atomic_t killed;
};

int yesuveau_channels_init(struct yesuveau_drm *);

int  yesuveau_channel_new(struct yesuveau_drm *, struct nvif_device *,
			 u32 arg0, u32 arg1, bool priv,
			 struct yesuveau_channel **);
void yesuveau_channel_del(struct yesuveau_channel **);
int  yesuveau_channel_idle(struct yesuveau_channel *);

extern int yesuveau_vram_pushbuf;

#endif
