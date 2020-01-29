/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (C) 2015-2018 Etnaviv Project
 */

#ifndef __ETNAVIV_GPU_H__
#define __ETNAVIV_GPU_H__

#include "etnaviv_cmdbuf.h"
#include "etnaviv_gem.h"
#include "etnaviv_mmu.h"
#include "etnaviv_drv.h"

struct etnaviv_gem_submit;
struct etnaviv_vram_mapping;

struct etnaviv_chip_identity {
	/* Chip model. */
	u32 model;

	/* Revision value.*/
	u32 revision;

	/* Supported feature fields. */
	u32 features;

	/* Supported minor feature fields. */
	u32 minor_features0;
	u32 minor_features1;
	u32 minor_features2;
	u32 minor_features3;
	u32 minor_features4;
	u32 minor_features5;
	u32 minor_features6;
	u32 minor_features7;
	u32 minor_features8;
	u32 minor_features9;
	u32 minor_features10;
	u32 minor_features11;

	/* Number of streams supported. */
	u32 stream_count;

	/* Total number of temporary registers per thread. */
	u32 register_max;

	/* Maximum number of threads. */
	u32 thread_count;

	/* Number of shader cores. */
	u32 shader_core_count;

	/* Size of the vertex cache. */
	u32 vertex_cache_size;

	/* Number of entries in the vertex output buffer. */
	u32 vertex_output_buffer_size;

	/* Number of pixel pipes. */
	u32 pixel_pipes;

	/* Number of instructions. */
	u32 instruction_count;

	/* Number of constants. */
	u32 num_constants;

	/* Buffer size */
	u32 buffer_size;

	/* Number of varyings */
	u8 varyings_count;
};

enum etnaviv_sec_mode {
	ETNA_SEC_NONE = 0,
	ETNA_SEC_KERNEL,
	ETNA_SEC_TZ
};

struct etnaviv_event {
	struct dma_fence *fence;
	struct etnaviv_gem_submit *submit;

	void (*sync_point)(struct etnaviv_gpu *gpu, struct etnaviv_event *event);
};

struct etnaviv_cmdbuf_suballoc;
struct regulator;
struct clk;

#define ETNA_NR_EVENTS 30

struct etnaviv_gpu {
	struct drm_device *drm;
	struct thermal_cooling_device *cooling;
	struct device *dev;
	struct mutex lock;
	struct etnaviv_chip_identity identity;
	enum etnaviv_sec_mode sec_mode;
	struct workqueue_struct *wq;
	struct drm_gpu_scheduler sched;
	bool initialized;

	/* 'ring'-buffer: */
	struct etnaviv_cmdbuf buffer;
	int exec_state;

	/* event management: */
	DECLARE_BITMAP(event_bitmap, ETNA_NR_EVENTS);
	struct etnaviv_event event[ETNA_NR_EVENTS];
	struct completion event_free;
	spinlock_t event_spinlock;

	u32 idle_mask;

	/* Fencing support */
	struct mutex fence_lock;
	struct idr fence_idr;
	u32 next_fence;
	u32 completed_fence;
	wait_queue_head_t fence_event;
	u64 fence_context;
	spinlock_t fence_spinlock;

	/* worker for handling 'sync' points: */
	struct work_struct sync_point_work;
	int sync_point_event;

	/* hang detection */
	u32 hangcheck_dma_addr;

	void __iomem *mmio;
	int irq;

	struct etnaviv_iommu_context *mmu_context;
	unsigned int flush_seq;

	/* Power Control: */
	struct clk *clk_bus;
	struct clk *clk_reg;
	struct clk *clk_core;
	struct clk *clk_shader;

	unsigned int freq_scale;
	unsigned long base_rate_core;
	unsigned long base_rate_shader;
};

static inline void gpu_write(struct etnaviv_gpu *gpu, u32 reg, u32 data)
{
	writel(data, gpu->mmio + reg);
}

static inline u32 gpu_read(struct etnaviv_gpu *gpu, u32 reg)
{
	return readl(gpu->mmio + reg);
}

int etnaviv_gpu_get_param(struct etnaviv_gpu *gpu, u32 param, u64 *value);

int etnaviv_gpu_init(struct etnaviv_gpu *gpu);
bool etnaviv_fill_identity_from_hwdb(struct etnaviv_gpu *gpu);

#ifdef CONFIG_DEBUG_FS
int etnaviv_gpu_debugfs(struct etnaviv_gpu *gpu, struct seq_file *m);
#endif

void etnaviv_gpu_recover_hang(struct etnaviv_gpu *gpu);
void etnaviv_gpu_retire(struct etnaviv_gpu *gpu);
int etnaviv_gpu_wait_fence_interruptible(struct etnaviv_gpu *gpu,
	u32 fence, struct drm_etnaviv_timespec *timeout);
int etnaviv_gpu_wait_obj_inactive(struct etnaviv_gpu *gpu,
	struct etnaviv_gem_object *etnaviv_obj,
	struct drm_etnaviv_timespec *timeout);
struct dma_fence *etnaviv_gpu_submit(struct etnaviv_gem_submit *submit);
int etnaviv_gpu_pm_get_sync(struct etnaviv_gpu *gpu);
void etnaviv_gpu_pm_put(struct etnaviv_gpu *gpu);
int etnaviv_gpu_wait_idle(struct etnaviv_gpu *gpu, unsigned int timeout_ms);
void etnaviv_gpu_start_fe(struct etnaviv_gpu *gpu, u32 address, u16 prefetch);

extern struct platform_driver etnaviv_gpu_driver;

#endif /* __ETNAVIV_GPU_H__ */
