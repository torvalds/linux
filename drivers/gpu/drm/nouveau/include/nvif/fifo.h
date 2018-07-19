#ifndef __NVIF_FIFO_H__
#define __NVIF_FIFO_H__
#include <nvif/device.h>

/* Returns mask of runlists that support a NV_DEVICE_INFO_ENGINE_* type. */
u64 nvif_fifo_runlist(struct nvif_device *, u64 engine);

/* CE-supporting runlists (excluding GRCE, if others exist). */
static inline u64
nvif_fifo_runlist_ce(struct nvif_device *device)
{
	u64 runmgr = nvif_fifo_runlist(device, NV_DEVICE_INFO_ENGINE_GR);
	u64 runmce = nvif_fifo_runlist(device, NV_DEVICE_INFO_ENGINE_CE);
	if (runmce && !(runmce &= ~runmgr))
		runmce = runmgr;
	return runmce;
}
#endif
