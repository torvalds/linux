/* SPDX-License-Identifier: MIT */
#ifndef __NOUVEAU_DRV_H__
#define __NOUVEAU_DRV_H__

#define DRIVER_AUTHOR		"Nouveau Project"
#define DRIVER_EMAIL		"nouveau@lists.freedesktop.org"

#define DRIVER_NAME		"nouveau"
#define DRIVER_DESC		"nVidia Riva/TNT/GeForce/Quadro/Tesla/Tegra K1+"
#define DRIVER_DATE		"20120801"

#define DRIVER_MAJOR		1
#define DRIVER_MINOR		4
#define DRIVER_PATCHLEVEL	0

/*
 * 1.1.1:
 * 	- added support for tiled system memory buffer objects
 *      - added support for NOUVEAU_GETPARAM_GRAPH_UNITS on [nvc0,nve0].
 *      - added support for compressed memory storage types on [nvc0,nve0].
 *      - added support for software methods 0x600,0x644,0x6ac on nvc0
 *        to control registers on the MPs to enable performance counters,
 *        and to control the warp error enable mask (OpenGL requires out of
 *        bounds access to local memory to be silently ignored / return 0).
 * 1.1.2:
 *      - fixes multiple bugs in flip completion events and timestamping
 * 1.2.0:
 * 	- object api exposed to userspace
 * 	- fermi,kepler,maxwell zbc
 * 1.2.1:
 *      - allow concurrent access to bo's mapped read/write.
 * 1.2.2:
 *      - add NOUVEAU_GEM_DOMAIN_COHERENT flag
 * 1.3.0:
 *      - NVIF ABI modified, safe because only (current) users are test
 *        programs that get directly linked with NVKM.
 * 1.3.1:
 *      - implemented limited ABI16/NVIF interop
 */

#include <linux/notifier.h>

#include <nvif/client.h>
#include <nvif/device.h>
#include <nvif/ioctl.h>
#include <nvif/mmu.h>
#include <nvif/vmm.h>

#include <drm/drm_connector.h>
#include <drm/drm_device.h>
#include <drm/drm_drv.h>
#include <drm/drm_file.h>

#include <drm/ttm/ttm_bo.h>
#include <drm/ttm/ttm_placement.h>

#include <drm/drm_audio_component.h>

#include "uapi/drm/nouveau_drm.h"

struct nouveau_channel;
struct platform_device;

#include "nouveau_fence.h"
#include "nouveau_bios.h"
#include "nouveau_sched.h"
#include "nouveau_vmm.h"
#include "nouveau_uvmm.h"

struct nouveau_drm_tile {
	struct nouveau_fence *fence;
	bool used;
};

enum nouveau_drm_object_route {
	NVDRM_OBJECT_NVIF = NVIF_IOCTL_V0_OWNER_NVIF,
	NVDRM_OBJECT_USIF,
	NVDRM_OBJECT_ABI16,
	NVDRM_OBJECT_ANY = NVIF_IOCTL_V0_OWNER_ANY,
};

enum nouveau_drm_handle {
	NVDRM_CHAN    = 0xcccc0000, /* |= client chid */
	NVDRM_NVSW    = 0x55550000,
};

struct nouveau_cli {
	struct nvif_client base;
	struct nouveau_drm *drm;
	struct mutex mutex;

	struct nvif_device device;
	struct nvif_mmu mmu;
	struct nouveau_vmm vmm;
	struct nouveau_vmm svm;
	struct {
		struct nouveau_uvmm *ptr;
		bool disabled;
	} uvmm;

	struct nouveau_sched *sched;

	const struct nvif_mclass *mem;

	struct list_head head;
	void *abi16;
	struct list_head objects;
	char name[32];

	struct work_struct work;
	struct list_head worker;
	struct mutex lock;
};

struct nouveau_cli_work {
	void (*func)(struct nouveau_cli_work *);
	struct nouveau_cli *cli;
	struct list_head head;

	struct dma_fence *fence;
	struct dma_fence_cb cb;
};

static inline struct nouveau_uvmm *
nouveau_cli_uvmm(struct nouveau_cli *cli)
{
	return cli ? cli->uvmm.ptr : NULL;
}

static inline struct nouveau_uvmm *
nouveau_cli_uvmm_locked(struct nouveau_cli *cli)
{
	struct nouveau_uvmm *uvmm;

	mutex_lock(&cli->mutex);
	uvmm = nouveau_cli_uvmm(cli);
	mutex_unlock(&cli->mutex);

	return uvmm;
}

static inline struct nouveau_vmm *
nouveau_cli_vmm(struct nouveau_cli *cli)
{
	struct nouveau_uvmm *uvmm;

	uvmm = nouveau_cli_uvmm(cli);
	if (uvmm)
		return &uvmm->vmm;

	if (cli->svm.cli)
		return &cli->svm;

	return &cli->vmm;
}

static inline void
__nouveau_cli_disable_uvmm_noinit(struct nouveau_cli *cli)
{
	struct nouveau_uvmm *uvmm = nouveau_cli_uvmm(cli);

	if (!uvmm)
		cli->uvmm.disabled = true;
}

static inline void
nouveau_cli_disable_uvmm_noinit(struct nouveau_cli *cli)
{
	mutex_lock(&cli->mutex);
	__nouveau_cli_disable_uvmm_noinit(cli);
	mutex_unlock(&cli->mutex);
}

void nouveau_cli_work_queue(struct nouveau_cli *, struct dma_fence *,
			    struct nouveau_cli_work *);

static inline struct nouveau_cli *
nouveau_cli(struct drm_file *fpriv)
{
	return fpriv ? fpriv->driver_priv : NULL;
}

static inline void
u_free(void *addr)
{
	kvfree(addr);
}

static inline void *
u_memcpya(uint64_t user, unsigned int nmemb, unsigned int size)
{
	void __user *userptr = u64_to_user_ptr(user);
	size_t bytes;

	if (unlikely(check_mul_overflow(nmemb, size, &bytes)))
		return ERR_PTR(-EOVERFLOW);
	return vmemdup_user(userptr, bytes);
}

#include <nvif/object.h>
#include <nvif/parent.h>

struct nouveau_drm {
	struct nvif_parent parent;
	struct nouveau_cli master;
	struct nouveau_cli client;
	struct drm_device *dev;

	struct list_head clients;

	/**
	 * @clients_lock: Protects access to the @clients list of &struct nouveau_cli.
	 */
	struct mutex clients_lock;

	u8 old_pm_cap;

	struct {
		struct agp_bridge_data *bridge;
		u32 base;
		u32 size;
		bool cma;
	} agp;

	/* TTM interface support */
	struct {
		struct ttm_device bdev;
		atomic_t validate_sequence;
		int (*move)(struct nouveau_channel *,
			    struct ttm_buffer_object *,
			    struct ttm_resource *, struct ttm_resource *);
		struct nouveau_channel *chan;
		struct nvif_object copy;
		int mtrr;
		int type_vram;
		int type_host[2];
		int type_ncoh[2];
		struct mutex io_reserve_mutex;
		struct list_head io_reserve_lru;
	} ttm;

	/* GEM interface support */
	struct {
		u64 vram_available;
		u64 gart_available;
	} gem;

	/* synchronisation */
	void *fence;

	/* Global channel management. */
	int chan_total; /* Number of channels across all runlists. */
	int chan_nr;	/* 0 if per-runlist CHIDs. */
	int runl_nr;
	struct {
		int chan_nr;
		int chan_id_base;
		u64 context_base;
	} *runl;

	/* Workqueue used for channel schedulers. */
	struct workqueue_struct *sched_wq;

	/* context for accelerated drm-internal operations */
	struct nouveau_channel *cechan;
	struct nouveau_channel *channel;
	struct nvkm_gpuobj *notify;
	struct nvif_object ntfy;

	/* nv10-nv40 tiling regions */
	struct {
		struct nouveau_drm_tile reg[15];
		spinlock_t lock;
	} tile;

	/* modesetting */
	struct nvbios vbios;
	struct nouveau_display *display;
	struct work_struct hpd_work;
	spinlock_t hpd_lock;
	u32 hpd_pending;
#ifdef CONFIG_ACPI
	struct notifier_block acpi_nb;
#endif

	/* power management */
	struct nouveau_hwmon *hwmon;
	struct nouveau_debugfs *debugfs;

	/* led management */
	struct nouveau_led *led;

	struct dev_pm_domain vga_pm_domain;

	struct nouveau_svm *svm;

	struct nouveau_dmem *dmem;

	struct {
		struct drm_audio_component *component;
		struct mutex lock;
		bool component_registered;
	} audio;
};

static inline struct nouveau_drm *
nouveau_drm(struct drm_device *dev)
{
	return dev->dev_private;
}

static inline bool
nouveau_drm_use_coherent_gpu_mapping(struct nouveau_drm *drm)
{
	struct nvif_mmu *mmu = &drm->client.mmu;
	return !(mmu->type[drm->ttm.type_host[0]].type & NVIF_MEM_UNCACHED);
}

int nouveau_pmops_suspend(struct device *);
int nouveau_pmops_resume(struct device *);
bool nouveau_pmops_runtime(void);

#include <nvkm/core/tegra.h>

struct drm_device *
nouveau_platform_device_create(const struct nvkm_device_tegra_func *,
			       struct platform_device *, struct nvkm_device **);
void nouveau_drm_device_remove(struct drm_device *dev);

#define NV_PRINTK(l,c,f,a...) do {                                             \
	struct nouveau_cli *_cli = (c);                                        \
	dev_##l(_cli->drm->dev->dev, "%s: "f, _cli->name, ##a);                \
} while(0)

#define NV_FATAL(drm,f,a...) NV_PRINTK(crit, &(drm)->client, f, ##a)
#define NV_ERROR(drm,f,a...) NV_PRINTK(err, &(drm)->client, f, ##a)
#define NV_WARN(drm,f,a...) NV_PRINTK(warn, &(drm)->client, f, ##a)
#define NV_INFO(drm,f,a...) NV_PRINTK(info, &(drm)->client, f, ##a)

#define NV_DEBUG(drm,f,a...) do {                                              \
	if (drm_debug_enabled(DRM_UT_DRIVER))                                  \
		NV_PRINTK(info, &(drm)->client, f, ##a);                       \
} while(0)
#define NV_ATOMIC(drm,f,a...) do {                                             \
	if (drm_debug_enabled(DRM_UT_ATOMIC))                                  \
		NV_PRINTK(info, &(drm)->client, f, ##a);                       \
} while(0)

#define NV_PRINTK_ONCE(l,c,f,a...) NV_PRINTK(l##_once,c,f, ##a)

#define NV_ERROR_ONCE(drm,f,a...) NV_PRINTK_ONCE(err, &(drm)->client, f, ##a)
#define NV_WARN_ONCE(drm,f,a...) NV_PRINTK_ONCE(warn, &(drm)->client, f, ##a)
#define NV_INFO_ONCE(drm,f,a...) NV_PRINTK_ONCE(info, &(drm)->client, f, ##a)

extern int nouveau_modeset;

#endif
