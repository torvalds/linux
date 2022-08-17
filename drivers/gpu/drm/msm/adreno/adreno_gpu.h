/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2013 Red Hat
 * Author: Rob Clark <robdclark@gmail.com>
 *
 * Copyright (c) 2014,2017, 2019 The Linux Foundation. All rights reserved.
 */

#ifndef __ADRENO_GPU_H__
#define __ADRENO_GPU_H__

#include <linux/firmware.h>
#include <linux/iopoll.h>

#include "msm_gpu.h"

#include "adreno_common.xml.h"
#include "adreno_pm4.xml.h"

extern bool snapshot_debugbus;
extern bool allow_vram_carveout;

enum {
	ADRENO_FW_PM4 = 0,
	ADRENO_FW_SQE = 0, /* a6xx */
	ADRENO_FW_PFP = 1,
	ADRENO_FW_GMU = 1, /* a6xx */
	ADRENO_FW_GPMU = 2,
	ADRENO_FW_MAX,
};

enum adreno_quirks {
	ADRENO_QUIRK_TWO_PASS_USE_WFI = 1,
	ADRENO_QUIRK_FAULT_DETECT_MASK = 2,
	ADRENO_QUIRK_LMLOADKILL_DISABLE = 3,
};

struct adreno_rev {
	uint8_t  core;
	uint8_t  major;
	uint8_t  minor;
	uint8_t  patchid;
};

#define ANY_ID 0xff

#define ADRENO_REV(core, major, minor, patchid) \
	((struct adreno_rev){ core, major, minor, patchid })

struct adreno_gpu_funcs {
	struct msm_gpu_funcs base;
	int (*get_timestamp)(struct msm_gpu *gpu, uint64_t *value);
};

struct adreno_reglist {
	u32 offset;
	u32 value;
};

extern const struct adreno_reglist a615_hwcg[], a630_hwcg[], a640_hwcg[], a650_hwcg[], a660_hwcg[];

struct adreno_info {
	struct adreno_rev rev;
	uint32_t revn;
	const char *name;
	const char *fw[ADRENO_FW_MAX];
	uint32_t gmem;
	enum adreno_quirks quirks;
	struct msm_gpu *(*init)(struct drm_device *dev);
	const char *zapfw;
	u32 inactive_period;
	const struct adreno_reglist *hwcg;
	u64 address_space_size;
};

const struct adreno_info *adreno_info(struct adreno_rev rev);

struct adreno_gpu {
	struct msm_gpu base;
	struct adreno_rev rev;
	const struct adreno_info *info;
	uint32_t gmem;  /* actual gmem size */
	uint32_t revn;  /* numeric revision name */
	uint16_t speedbin;
	const struct adreno_gpu_funcs *funcs;

	/* interesting register offsets to dump: */
	const unsigned int *registers;

	/*
	 * Are we loading fw from legacy path?  Prior to addition
	 * of gpu firmware to linux-firmware, the fw files were
	 * placed in toplevel firmware directory, following qcom's
	 * android kernel.  But linux-firmware preferred they be
	 * placed in a 'qcom' subdirectory.
	 *
	 * For backwards compatibility, we try first to load from
	 * the new path, using request_firmware_direct() to avoid
	 * any potential timeout waiting for usermode helper, then
	 * fall back to the old path (with direct load).  And
	 * finally fall back to request_firmware() with the new
	 * path to allow the usermode helper.
	 */
	enum {
		FW_LOCATION_UNKNOWN = 0,
		FW_LOCATION_NEW,       /* /lib/firmware/qcom/$fwfile */
		FW_LOCATION_LEGACY,    /* /lib/firmware/$fwfile */
		FW_LOCATION_HELPER,
	} fwloc;

	/* firmware: */
	const struct firmware *fw[ADRENO_FW_MAX];

	/*
	 * Register offsets are different between some GPUs.
	 * GPU specific offsets will be exported by GPU specific
	 * code (a3xx_gpu.c) and stored in this common location.
	 */
	const unsigned int *reg_offsets;
};
#define to_adreno_gpu(x) container_of(x, struct adreno_gpu, base)

struct adreno_ocmem {
	struct ocmem *ocmem;
	unsigned long base;
	void *hdl;
};

/* platform config data (ie. from DT, or pdata) */
struct adreno_platform_config {
	struct adreno_rev rev;
};

#define ADRENO_IDLE_TIMEOUT msecs_to_jiffies(1000)

#define spin_until(X) ({                                   \
	int __ret = -ETIMEDOUT;                            \
	unsigned long __t = jiffies + ADRENO_IDLE_TIMEOUT; \
	do {                                               \
		if (X) {                                   \
			__ret = 0;                         \
			break;                             \
		}                                          \
	} while (time_before(jiffies, __t));               \
	__ret;                                             \
})

bool adreno_cmp_rev(struct adreno_rev rev1, struct adreno_rev rev2);

static inline bool adreno_is_a2xx(struct adreno_gpu *gpu)
{
	return (gpu->revn < 300);
}

static inline bool adreno_is_a20x(struct adreno_gpu *gpu)
{
	return (gpu->revn < 210);
}

static inline bool adreno_is_a225(struct adreno_gpu *gpu)
{
	return gpu->revn == 225;
}

static inline bool adreno_is_a305(struct adreno_gpu *gpu)
{
	return gpu->revn == 305;
}

static inline bool adreno_is_a306(struct adreno_gpu *gpu)
{
	/* yes, 307, because a305c is 306 */
	return gpu->revn == 307;
}

static inline bool adreno_is_a320(struct adreno_gpu *gpu)
{
	return gpu->revn == 320;
}

static inline bool adreno_is_a330(struct adreno_gpu *gpu)
{
	return gpu->revn == 330;
}

static inline bool adreno_is_a330v2(struct adreno_gpu *gpu)
{
	return adreno_is_a330(gpu) && (gpu->rev.patchid > 0);
}

static inline int adreno_is_a405(struct adreno_gpu *gpu)
{
	return gpu->revn == 405;
}

static inline int adreno_is_a420(struct adreno_gpu *gpu)
{
	return gpu->revn == 420;
}

static inline int adreno_is_a430(struct adreno_gpu *gpu)
{
	return gpu->revn == 430;
}

static inline int adreno_is_a506(struct adreno_gpu *gpu)
{
	return gpu->revn == 506;
}

static inline int adreno_is_a508(struct adreno_gpu *gpu)
{
	return gpu->revn == 508;
}

static inline int adreno_is_a509(struct adreno_gpu *gpu)
{
	return gpu->revn == 509;
}

static inline int adreno_is_a510(struct adreno_gpu *gpu)
{
	return gpu->revn == 510;
}

static inline int adreno_is_a512(struct adreno_gpu *gpu)
{
	return gpu->revn == 512;
}

static inline int adreno_is_a530(struct adreno_gpu *gpu)
{
	return gpu->revn == 530;
}

static inline int adreno_is_a540(struct adreno_gpu *gpu)
{
	return gpu->revn == 540;
}

static inline int adreno_is_a618(struct adreno_gpu *gpu)
{
	return gpu->revn == 618;
}

static inline int adreno_is_a619(struct adreno_gpu *gpu)
{
	return gpu->revn == 619;
}

static inline int adreno_is_a630(struct adreno_gpu *gpu)
{
	return gpu->revn == 630;
}

static inline int adreno_is_a640_family(struct adreno_gpu *gpu)
{
	return (gpu->revn == 640) || (gpu->revn == 680);
}

static inline int adreno_is_a650(struct adreno_gpu *gpu)
{
	return gpu->revn == 650;
}

static inline int adreno_is_7c3(struct adreno_gpu *gpu)
{
	/* The order of args is important here to handle ANY_ID correctly */
	return adreno_cmp_rev(ADRENO_REV(6, 3, 5, ANY_ID), gpu->rev);
}

static inline int adreno_is_a660(struct adreno_gpu *gpu)
{
	return gpu->revn == 660;
}

/* check for a615, a616, a618, a619 or any derivatives */
static inline int adreno_is_a615_family(struct adreno_gpu *gpu)
{
	return gpu->revn == 615 || gpu->revn == 616 || gpu->revn == 618 || gpu->revn == 619;
}

static inline int adreno_is_a660_family(struct adreno_gpu *gpu)
{
	return adreno_is_a660(gpu) || adreno_is_7c3(gpu);
}

/* check for a650, a660, or any derivatives */
static inline int adreno_is_a650_family(struct adreno_gpu *gpu)
{
	return gpu->revn == 650 || gpu->revn == 620 || adreno_is_a660_family(gpu);
}

u64 adreno_private_address_space_size(struct msm_gpu *gpu);
int adreno_get_param(struct msm_gpu *gpu, struct msm_file_private *ctx,
		     uint32_t param, uint64_t *value, uint32_t *len);
int adreno_set_param(struct msm_gpu *gpu, struct msm_file_private *ctx,
		     uint32_t param, uint64_t value, uint32_t len);
const struct firmware *adreno_request_fw(struct adreno_gpu *adreno_gpu,
		const char *fwname);
struct drm_gem_object *adreno_fw_create_bo(struct msm_gpu *gpu,
		const struct firmware *fw, u64 *iova);
int adreno_hw_init(struct msm_gpu *gpu);
void adreno_recover(struct msm_gpu *gpu);
void adreno_flush(struct msm_gpu *gpu, struct msm_ringbuffer *ring, u32 reg);
bool adreno_idle(struct msm_gpu *gpu, struct msm_ringbuffer *ring);
#if defined(CONFIG_DEBUG_FS) || defined(CONFIG_DEV_COREDUMP)
void adreno_show(struct msm_gpu *gpu, struct msm_gpu_state *state,
		struct drm_printer *p);
#endif
void adreno_dump_info(struct msm_gpu *gpu);
void adreno_dump(struct msm_gpu *gpu);
void adreno_wait_ring(struct msm_ringbuffer *ring, uint32_t ndwords);
struct msm_ringbuffer *adreno_active_ring(struct msm_gpu *gpu);

int adreno_gpu_ocmem_init(struct device *dev, struct adreno_gpu *adreno_gpu,
			  struct adreno_ocmem *ocmem);
void adreno_gpu_ocmem_cleanup(struct adreno_ocmem *ocmem);

int adreno_gpu_init(struct drm_device *drm, struct platform_device *pdev,
		struct adreno_gpu *gpu, const struct adreno_gpu_funcs *funcs,
		int nr_rings);
void adreno_gpu_cleanup(struct adreno_gpu *gpu);
int adreno_load_fw(struct adreno_gpu *adreno_gpu);

void adreno_gpu_state_destroy(struct msm_gpu_state *state);

int adreno_gpu_state_get(struct msm_gpu *gpu, struct msm_gpu_state *state);
int adreno_gpu_state_put(struct msm_gpu_state *state);
void adreno_show_object(struct drm_printer *p, void **ptr, int len,
		bool *encoded);

/*
 * Common helper function to initialize the default address space for arm-smmu
 * attached targets
 */
struct msm_gem_address_space *
adreno_iommu_create_address_space(struct msm_gpu *gpu,
		struct platform_device *pdev);

void adreno_set_llc_attributes(struct iommu_domain *iommu);

int adreno_read_speedbin(struct device *dev, u32 *speedbin);

/*
 * For a5xx and a6xx targets load the zap shader that is used to pull the GPU
 * out of secure mode
 */
int adreno_zap_shader_load(struct msm_gpu *gpu, u32 pasid);

/* ringbuffer helpers (the parts that are adreno specific) */

static inline void
OUT_PKT0(struct msm_ringbuffer *ring, uint16_t regindx, uint16_t cnt)
{
	adreno_wait_ring(ring, cnt+1);
	OUT_RING(ring, CP_TYPE0_PKT | ((cnt-1) << 16) | (regindx & 0x7FFF));
}

/* no-op packet: */
static inline void
OUT_PKT2(struct msm_ringbuffer *ring)
{
	adreno_wait_ring(ring, 1);
	OUT_RING(ring, CP_TYPE2_PKT);
}

static inline void
OUT_PKT3(struct msm_ringbuffer *ring, uint8_t opcode, uint16_t cnt)
{
	adreno_wait_ring(ring, cnt+1);
	OUT_RING(ring, CP_TYPE3_PKT | ((cnt-1) << 16) | ((opcode & 0xFF) << 8));
}

static inline u32 PM4_PARITY(u32 val)
{
	return (0x9669 >> (0xF & (val ^
		(val >> 4) ^ (val >> 8) ^ (val >> 12) ^
		(val >> 16) ^ ((val) >> 20) ^ (val >> 24) ^
		(val >> 28)))) & 1;
}

/* Maximum number of values that can be executed for one opcode */
#define TYPE4_MAX_PAYLOAD 127

#define PKT4(_reg, _cnt) \
	(CP_TYPE4_PKT | ((_cnt) << 0) | (PM4_PARITY((_cnt)) << 7) | \
	 (((_reg) & 0x3FFFF) << 8) | (PM4_PARITY((_reg)) << 27))

static inline void
OUT_PKT4(struct msm_ringbuffer *ring, uint16_t regindx, uint16_t cnt)
{
	adreno_wait_ring(ring, cnt + 1);
	OUT_RING(ring, PKT4(regindx, cnt));
}

static inline void
OUT_PKT7(struct msm_ringbuffer *ring, uint8_t opcode, uint16_t cnt)
{
	adreno_wait_ring(ring, cnt + 1);
	OUT_RING(ring, CP_TYPE7_PKT | (cnt << 0) | (PM4_PARITY(cnt) << 15) |
		((opcode & 0x7F) << 16) | (PM4_PARITY(opcode) << 23));
}

struct msm_gpu *a2xx_gpu_init(struct drm_device *dev);
struct msm_gpu *a3xx_gpu_init(struct drm_device *dev);
struct msm_gpu *a4xx_gpu_init(struct drm_device *dev);
struct msm_gpu *a5xx_gpu_init(struct drm_device *dev);
struct msm_gpu *a6xx_gpu_init(struct drm_device *dev);

static inline uint32_t get_wptr(struct msm_ringbuffer *ring)
{
	return (ring->cur - ring->start) % (MSM_GPU_RINGBUFFER_SZ >> 2);
}

/*
 * Given a register and a count, return a value to program into
 * REG_CP_PROTECT_REG(n) - this will block both reads and writes for _len
 * registers starting at _reg.
 *
 * The register base needs to be a multiple of the length. If it is not, the
 * hardware will quietly mask off the bits for you and shift the size. For
 * example, if you intend the protection to start at 0x07 for a length of 4
 * (0x07-0x0A) the hardware will actually protect (0x04-0x07) which might
 * expose registers you intended to protect!
 */
#define ADRENO_PROTECT_RW(_reg, _len) \
	((1 << 30) | (1 << 29) | \
	((ilog2((_len)) & 0x1F) << 24) | (((_reg) << 2) & 0xFFFFF))

/*
 * Same as above, but allow reads over the range. For areas of mixed use (such
 * as performance counters) this allows us to protect a much larger range with a
 * single register
 */
#define ADRENO_PROTECT_RDONLY(_reg, _len) \
	((1 << 29) \
	((ilog2((_len)) & 0x1F) << 24) | (((_reg) << 2) & 0xFFFFF))


#define gpu_poll_timeout(gpu, addr, val, cond, interval, timeout) \
	readl_poll_timeout((gpu)->mmio + ((addr) << 2), val, cond, \
		interval, timeout)

#endif /* __ADRENO_GPU_H__ */
