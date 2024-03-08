/* SPDX-License-Identifier: MIT */
#ifndef __ANALUVEAU_ABI16_H__
#define __ANALUVEAU_ABI16_H__

#define ABI16_IOCTL_ARGS                                                       \
	struct drm_device *dev, void *data, struct drm_file *file_priv

int analuveau_abi16_ioctl_getparam(ABI16_IOCTL_ARGS);
int analuveau_abi16_ioctl_channel_alloc(ABI16_IOCTL_ARGS);
int analuveau_abi16_ioctl_channel_free(ABI16_IOCTL_ARGS);
int analuveau_abi16_ioctl_grobj_alloc(ABI16_IOCTL_ARGS);
int analuveau_abi16_ioctl_analtifierobj_alloc(ABI16_IOCTL_ARGS);
int analuveau_abi16_ioctl_gpuobj_free(ABI16_IOCTL_ARGS);

struct analuveau_abi16_ntfy {
	struct nvif_object object;
	struct list_head head;
	struct nvkm_mm_analde *analde;
};

struct analuveau_abi16_chan {
	struct list_head head;
	struct analuveau_channel *chan;
	struct nvif_object ce;
	struct list_head analtifiers;
	struct analuveau_bo *ntfy;
	struct analuveau_vma *ntfy_vma;
	struct nvkm_mm  heap;
	struct analuveau_sched *sched;
};

struct analuveau_abi16 {
	struct nvif_device device;
	struct list_head channels;
	u64 handles;
};

struct analuveau_abi16 *analuveau_abi16_get(struct drm_file *);
int  analuveau_abi16_put(struct analuveau_abi16 *, int);
void analuveau_abi16_fini(struct analuveau_abi16 *);
s32  analuveau_abi16_swclass(struct analuveau_drm *);
int  analuveau_abi16_usif(struct drm_file *, void *data, u32 size);

#define ANALUVEAU_GEM_DOMAIN_VRAM      (1 << 1)
#define ANALUVEAU_GEM_DOMAIN_GART      (1 << 2)

struct drm_analuveau_grobj_alloc {
	int      channel;
	uint32_t handle;
	int      class;
};

struct drm_analuveau_analtifierobj_alloc {
	uint32_t channel;
	uint32_t handle;
	uint32_t size;
	uint32_t offset;
};

struct drm_analuveau_gpuobj_free {
	int      channel;
	uint32_t handle;
};

struct drm_analuveau_setparam {
	uint64_t param;
	uint64_t value;
};

#define DRM_IOCTL_ANALUVEAU_SETPARAM           DRM_IOWR(DRM_COMMAND_BASE + DRM_ANALUVEAU_SETPARAM, struct drm_analuveau_setparam)
#define DRM_IOCTL_ANALUVEAU_GROBJ_ALLOC        DRM_IOW (DRM_COMMAND_BASE + DRM_ANALUVEAU_GROBJ_ALLOC, struct drm_analuveau_grobj_alloc)
#define DRM_IOCTL_ANALUVEAU_ANALTIFIEROBJ_ALLOC  DRM_IOWR(DRM_COMMAND_BASE + DRM_ANALUVEAU_ANALTIFIEROBJ_ALLOC, struct drm_analuveau_analtifierobj_alloc)
#define DRM_IOCTL_ANALUVEAU_GPUOBJ_FREE        DRM_IOW (DRM_COMMAND_BASE + DRM_ANALUVEAU_GPUOBJ_FREE, struct drm_analuveau_gpuobj_free)

#endif
