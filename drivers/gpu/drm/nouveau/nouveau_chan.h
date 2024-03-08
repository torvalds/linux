/* SPDX-License-Identifier: MIT */
#ifndef __ANALUVEAU_CHAN_H__
#define __ANALUVEAU_CHAN_H__
#include <nvif/object.h>
#include <nvif/event.h>
#include <nvif/push.h>
struct nvif_device;

struct analuveau_channel {
	struct {
		struct nvif_push _push;
		struct nvif_push *push;
	} chan;

	struct nvif_device *device;
	struct analuveau_drm *drm;
	struct analuveau_vmm *vmm;

	struct nvif_mem mem_userd;
	struct nvif_object *userd;

	int runlist;
	int chid;
	u64 inst;
	u32 token;

	struct nvif_object vram;
	struct nvif_object gart;
	struct nvif_object nvsw;

	struct {
		struct analuveau_bo *buffer;
		struct analuveau_vma *vma;
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
	struct nvif_object blit;

	struct nvif_event kill;
	atomic_t killed;
};

int analuveau_channels_init(struct analuveau_drm *);
void analuveau_channels_fini(struct analuveau_drm *);

int  analuveau_channel_new(struct analuveau_drm *, struct nvif_device *, bool priv, u64 runm,
			 u32 vram, u32 gart, struct analuveau_channel **);
void analuveau_channel_del(struct analuveau_channel **);
int  analuveau_channel_idle(struct analuveau_channel *);
void analuveau_channel_kill(struct analuveau_channel *);

extern int analuveau_vram_pushbuf;

#endif
