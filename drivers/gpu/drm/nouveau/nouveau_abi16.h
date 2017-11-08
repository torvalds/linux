/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __NOUVEAU_ABI16_H__
#define __NOUVEAU_ABI16_H__

#define ABI16_IOCTL_ARGS                                                       \
	struct drm_device *dev, void *data, struct drm_file *file_priv

int nouveau_abi16_ioctl_getparam(ABI16_IOCTL_ARGS);
int nouveau_abi16_ioctl_setparam(ABI16_IOCTL_ARGS);
int nouveau_abi16_ioctl_channel_alloc(ABI16_IOCTL_ARGS);
int nouveau_abi16_ioctl_channel_free(ABI16_IOCTL_ARGS);
int nouveau_abi16_ioctl_grobj_alloc(ABI16_IOCTL_ARGS);
int nouveau_abi16_ioctl_notifierobj_alloc(ABI16_IOCTL_ARGS);
int nouveau_abi16_ioctl_gpuobj_free(ABI16_IOCTL_ARGS);

struct nouveau_abi16_ntfy {
	struct nvif_object object;
	struct list_head head;
	struct nvkm_mm_node *node;
};

struct nouveau_abi16_chan {
	struct list_head head;
	struct nouveau_channel *chan;
	struct list_head notifiers;
	struct nouveau_bo *ntfy;
	struct nvkm_vma ntfy_vma;
	struct nvkm_mm  heap;
};

struct nouveau_abi16 {
	struct nvif_device device;
	struct list_head channels;
	u64 handles;
};

struct nouveau_abi16 *nouveau_abi16_get(struct drm_file *);
int  nouveau_abi16_put(struct nouveau_abi16 *, int);
void nouveau_abi16_fini(struct nouveau_abi16 *);
s32  nouveau_abi16_swclass(struct nouveau_drm *);
int  nouveau_abi16_usif(struct drm_file *, void *data, u32 size);

#define NOUVEAU_GEM_DOMAIN_VRAM      (1 << 1)
#define NOUVEAU_GEM_DOMAIN_GART      (1 << 2)

struct drm_nouveau_channel_alloc {
	uint32_t     fb_ctxdma_handle;
	uint32_t     tt_ctxdma_handle;

	int          channel;
	uint32_t     pushbuf_domains;

	/* Notifier memory */
	uint32_t     notifier_handle;

	/* DRM-enforced subchannel assignments */
	struct {
		uint32_t handle;
		uint32_t grclass;
	} subchan[8];
	uint32_t nr_subchan;
};

struct drm_nouveau_channel_free {
	int channel;
};

struct drm_nouveau_grobj_alloc {
	int      channel;
	uint32_t handle;
	int      class;
};

struct drm_nouveau_notifierobj_alloc {
	uint32_t channel;
	uint32_t handle;
	uint32_t size;
	uint32_t offset;
};

struct drm_nouveau_gpuobj_free {
	int      channel;
	uint32_t handle;
};

#define NOUVEAU_GETPARAM_PCI_VENDOR      3
#define NOUVEAU_GETPARAM_PCI_DEVICE      4
#define NOUVEAU_GETPARAM_BUS_TYPE        5
#define NOUVEAU_GETPARAM_FB_SIZE         8
#define NOUVEAU_GETPARAM_AGP_SIZE        9
#define NOUVEAU_GETPARAM_CHIPSET_ID      11
#define NOUVEAU_GETPARAM_VM_VRAM_BASE    12
#define NOUVEAU_GETPARAM_GRAPH_UNITS     13
#define NOUVEAU_GETPARAM_PTIMER_TIME     14
#define NOUVEAU_GETPARAM_HAS_BO_USAGE    15
#define NOUVEAU_GETPARAM_HAS_PAGEFLIP    16
struct drm_nouveau_getparam {
	uint64_t param;
	uint64_t value;
};

struct drm_nouveau_setparam {
	uint64_t param;
	uint64_t value;
};

#define DRM_IOCTL_NOUVEAU_GETPARAM           DRM_IOWR(DRM_COMMAND_BASE + DRM_NOUVEAU_GETPARAM, struct drm_nouveau_getparam)
#define DRM_IOCTL_NOUVEAU_SETPARAM           DRM_IOWR(DRM_COMMAND_BASE + DRM_NOUVEAU_SETPARAM, struct drm_nouveau_setparam)
#define DRM_IOCTL_NOUVEAU_CHANNEL_ALLOC      DRM_IOWR(DRM_COMMAND_BASE + DRM_NOUVEAU_CHANNEL_ALLOC, struct drm_nouveau_channel_alloc)
#define DRM_IOCTL_NOUVEAU_CHANNEL_FREE       DRM_IOW (DRM_COMMAND_BASE + DRM_NOUVEAU_CHANNEL_FREE, struct drm_nouveau_channel_free)
#define DRM_IOCTL_NOUVEAU_GROBJ_ALLOC        DRM_IOW (DRM_COMMAND_BASE + DRM_NOUVEAU_GROBJ_ALLOC, struct drm_nouveau_grobj_alloc)
#define DRM_IOCTL_NOUVEAU_NOTIFIEROBJ_ALLOC  DRM_IOWR(DRM_COMMAND_BASE + DRM_NOUVEAU_NOTIFIEROBJ_ALLOC, struct drm_nouveau_notifierobj_alloc)
#define DRM_IOCTL_NOUVEAU_GPUOBJ_FREE        DRM_IOW (DRM_COMMAND_BASE + DRM_NOUVEAU_GPUOBJ_FREE, struct drm_nouveau_gpuobj_free)

#endif
