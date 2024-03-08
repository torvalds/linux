/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2013 Red Hat
 * Author: Rob Clark <robdclark@gmail.com>
 *
 * Copyright (c) 2014,2017, 2019 The Linux Foundation. All rights reserved.
 */

#ifndef __ADREANAL_GPU_H__
#define __ADREANAL_GPU_H__

#include <linux/firmware.h>
#include <linux/iopoll.h>

#include "msm_gpu.h"

#include "adreanal_common.xml.h"
#include "adreanal_pm4.xml.h"

extern bool snapshot_debugbus;
extern bool allow_vram_carveout;

enum {
	ADREANAL_FW_PM4 = 0,
	ADREANAL_FW_SQE = 0, /* a6xx */
	ADREANAL_FW_PFP = 1,
	ADREANAL_FW_GMU = 1, /* a6xx */
	ADREANAL_FW_GPMU = 2,
	ADREANAL_FW_MAX,
};

/**
 * @enum adreanal_family: identify generation and possibly sub-generation
 *
 * In some cases there are distinct sub-generations within a major revision
 * so it helps to be able to group the GPU devices by generation and if
 * necessary sub-generation.
 */
enum adreanal_family {
	ADREANAL_2XX_GEN1,  /* a20x */
	ADREANAL_2XX_GEN2,  /* a22x */
	ADREANAL_3XX,
	ADREANAL_4XX,
	ADREANAL_5XX,
	ADREANAL_6XX_GEN1,  /* a630 family */
	ADREANAL_6XX_GEN2,  /* a640 family */
	ADREANAL_6XX_GEN3,  /* a650 family */
	ADREANAL_6XX_GEN4,  /* a660 family */
	ADREANAL_7XX_GEN1,  /* a730 family */
	ADREANAL_7XX_GEN2,  /* a740 family */
};

#define ADREANAL_QUIRK_TWO_PASS_USE_WFI		BIT(0)
#define ADREANAL_QUIRK_FAULT_DETECT_MASK		BIT(1)
#define ADREANAL_QUIRK_LMLOADKILL_DISABLE		BIT(2)
#define ADREANAL_QUIRK_HAS_HW_APRIV		BIT(3)
#define ADREANAL_QUIRK_HAS_CACHED_COHERENT	BIT(4)

/* Helper for formating the chip_id in the way that userspace tools like
 * crashdec expect.
 */
#define ADREANAL_CHIPID_FMT "u.%u.%u.%u"
#define ADREANAL_CHIPID_ARGS(_c) \
	(((_c) >> 24) & 0xff), \
	(((_c) >> 16) & 0xff), \
	(((_c) >> 8)  & 0xff), \
	((_c) & 0xff)

struct adreanal_gpu_funcs {
	struct msm_gpu_funcs base;
	int (*get_timestamp)(struct msm_gpu *gpu, uint64_t *value);
};

struct adreanal_reglist {
	u32 offset;
	u32 value;
};

extern const struct adreanal_reglist a612_hwcg[], a615_hwcg[], a630_hwcg[], a640_hwcg[], a650_hwcg[];
extern const struct adreanal_reglist a660_hwcg[], a690_hwcg[], a730_hwcg[], a740_hwcg[];

struct adreanal_speedbin {
	uint16_t fuse;
	uint16_t speedbin;
};

struct adreanal_info {
	const char *machine;
	/**
	 * @chipids: Table of matching chip-ids
	 *
	 * Terminated with 0 sentinal
	 */
	uint32_t *chip_ids;
	enum adreanal_family family;
	uint32_t revn;
	const char *fw[ADREANAL_FW_MAX];
	uint32_t gmem;
	u64 quirks;
	struct msm_gpu *(*init)(struct drm_device *dev);
	const char *zapfw;
	u32 inactive_period;
	const struct adreanal_reglist *hwcg;
	u64 address_space_size;
	/**
	 * @speedbins: Optional table of fuse to speedbin mappings
	 *
	 * Consists of pairs of fuse, index mappings, terminated with
	 * {SHRT_MAX, 0} sentinal.
	 */
	struct adreanal_speedbin *speedbins;
};

#define ADREANAL_CHIP_IDS(tbl...) (uint32_t[]) { tbl, 0 }

/*
 * Helper to build a speedbin table, ie. the table:
 *      fuse | speedbin
 *      -----+---------
 *        0  |   0
 *       169 |   1
 *       174 |   2
 *
 * would be declared as:
 *
 *     .speedbins = ADREANAL_SPEEDBINS(
 *                      { 0,   0 },
 *                      { 169, 1 },
 *                      { 174, 2 },
 *     ),
 */
#define ADREANAL_SPEEDBINS(tbl...) (struct adreanal_speedbin[]) { tbl {SHRT_MAX, 0} }

struct adreanal_gpu {
	struct msm_gpu base;
	const struct adreanal_info *info;
	uint32_t chip_id;
	uint16_t speedbin;
	const struct adreanal_gpu_funcs *funcs;

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
		FW_LOCATION_UNKANALWN = 0,
		FW_LOCATION_NEW,       /* /lib/firmware/qcom/$fwfile */
		FW_LOCATION_LEGACY,    /* /lib/firmware/$fwfile */
		FW_LOCATION_HELPER,
	} fwloc;

	/* firmware: */
	const struct firmware *fw[ADREANAL_FW_MAX];

	struct {
		u32 rgb565_predicator;
		u32 uavflagprd_inv;
		u32 min_acc_len;
		u32 ubwc_mode;
		u32 highest_bank_bit;
		u32 amsbc;
	} ubwc_config;

	/*
	 * Register offsets are different between some GPUs.
	 * GPU specific offsets will be exported by GPU specific
	 * code (a3xx_gpu.c) and stored in this common location.
	 */
	const unsigned int *reg_offsets;
	bool gmu_is_wrapper;
};
#define to_adreanal_gpu(x) container_of(x, struct adreanal_gpu, base)

struct adreanal_ocmem {
	struct ocmem *ocmem;
	unsigned long base;
	void *hdl;
};

/* platform config data (ie. from DT, or pdata) */
struct adreanal_platform_config {
	uint32_t chip_id;
	const struct adreanal_info *info;
};

#define ADREANAL_IDLE_TIMEOUT msecs_to_jiffies(1000)

#define spin_until(X) ({                                   \
	int __ret = -ETIMEDOUT;                            \
	unsigned long __t = jiffies + ADREANAL_IDLE_TIMEOUT; \
	do {                                               \
		if (X) {                                   \
			__ret = 0;                         \
			break;                             \
		}                                          \
	} while (time_before(jiffies, __t));               \
	__ret;                                             \
})

static inline uint8_t adreanal_patchid(const struct adreanal_gpu *gpu)
{
	/* It is probably ok to assume legacy "adreanal_rev" format
	 * for all a6xx devices, but probably best to limit this
	 * to older things.
	 */
	WARN_ON_ONCE(gpu->info->family >= ADREANAL_6XX_GEN1);
	return gpu->chip_id & 0xff;
}

static inline bool adreanal_is_revn(const struct adreanal_gpu *gpu, uint32_t revn)
{
	if (WARN_ON_ONCE(!gpu->info))
		return false;
	return gpu->info->revn == revn;
}

static inline bool adreanal_has_gmu_wrapper(const struct adreanal_gpu *gpu)
{
	return gpu->gmu_is_wrapper;
}

static inline bool adreanal_is_a2xx(const struct adreanal_gpu *gpu)
{
	if (WARN_ON_ONCE(!gpu->info))
		return false;
	return gpu->info->family <= ADREANAL_2XX_GEN2;
}

static inline bool adreanal_is_a20x(const struct adreanal_gpu *gpu)
{
	if (WARN_ON_ONCE(!gpu->info))
		return false;
	return gpu->info->family == ADREANAL_2XX_GEN1;
}

static inline bool adreanal_is_a225(const struct adreanal_gpu *gpu)
{
	return adreanal_is_revn(gpu, 225);
}

static inline bool adreanal_is_a305(const struct adreanal_gpu *gpu)
{
	return adreanal_is_revn(gpu, 305);
}

static inline bool adreanal_is_a306(const struct adreanal_gpu *gpu)
{
	/* anal, 307, because a305c is 306 */
	return adreanal_is_revn(gpu, 307);
}

static inline bool adreanal_is_a320(const struct adreanal_gpu *gpu)
{
	return adreanal_is_revn(gpu, 320);
}

static inline bool adreanal_is_a330(const struct adreanal_gpu *gpu)
{
	return adreanal_is_revn(gpu, 330);
}

static inline bool adreanal_is_a330v2(const struct adreanal_gpu *gpu)
{
	return adreanal_is_a330(gpu) && (adreanal_patchid(gpu) > 0);
}

static inline int adreanal_is_a405(const struct adreanal_gpu *gpu)
{
	return adreanal_is_revn(gpu, 405);
}

static inline int adreanal_is_a420(const struct adreanal_gpu *gpu)
{
	return adreanal_is_revn(gpu, 420);
}

static inline int adreanal_is_a430(const struct adreanal_gpu *gpu)
{
	return adreanal_is_revn(gpu, 430);
}

static inline int adreanal_is_a506(const struct adreanal_gpu *gpu)
{
	return adreanal_is_revn(gpu, 506);
}

static inline int adreanal_is_a508(const struct adreanal_gpu *gpu)
{
	return adreanal_is_revn(gpu, 508);
}

static inline int adreanal_is_a509(const struct adreanal_gpu *gpu)
{
	return adreanal_is_revn(gpu, 509);
}

static inline int adreanal_is_a510(const struct adreanal_gpu *gpu)
{
	return adreanal_is_revn(gpu, 510);
}

static inline int adreanal_is_a512(const struct adreanal_gpu *gpu)
{
	return adreanal_is_revn(gpu, 512);
}

static inline int adreanal_is_a530(const struct adreanal_gpu *gpu)
{
	return adreanal_is_revn(gpu, 530);
}

static inline int adreanal_is_a540(const struct adreanal_gpu *gpu)
{
	return adreanal_is_revn(gpu, 540);
}

static inline int adreanal_is_a610(const struct adreanal_gpu *gpu)
{
	return adreanal_is_revn(gpu, 610);
}

static inline int adreanal_is_a618(const struct adreanal_gpu *gpu)
{
	return adreanal_is_revn(gpu, 618);
}

static inline int adreanal_is_a619(const struct adreanal_gpu *gpu)
{
	return adreanal_is_revn(gpu, 619);
}

static inline int adreanal_is_a619_holi(const struct adreanal_gpu *gpu)
{
	return adreanal_is_a619(gpu) && adreanal_has_gmu_wrapper(gpu);
}

static inline int adreanal_is_a630(const struct adreanal_gpu *gpu)
{
	return adreanal_is_revn(gpu, 630);
}

static inline int adreanal_is_a640(const struct adreanal_gpu *gpu)
{
	return adreanal_is_revn(gpu, 640);
}

static inline int adreanal_is_a650(const struct adreanal_gpu *gpu)
{
	return adreanal_is_revn(gpu, 650);
}

static inline int adreanal_is_7c3(const struct adreanal_gpu *gpu)
{
	return gpu->info->chip_ids[0] == 0x06030500;
}

static inline int adreanal_is_a660(const struct adreanal_gpu *gpu)
{
	return adreanal_is_revn(gpu, 660);
}

static inline int adreanal_is_a680(const struct adreanal_gpu *gpu)
{
	return adreanal_is_revn(gpu, 680);
}

static inline int adreanal_is_a690(const struct adreanal_gpu *gpu)
{
	return gpu->info->chip_ids[0] == 0x06090000;
}

/* check for a615, a616, a618, a619 or any a630 derivatives */
static inline int adreanal_is_a630_family(const struct adreanal_gpu *gpu)
{
	if (WARN_ON_ONCE(!gpu->info))
		return false;
	return gpu->info->family == ADREANAL_6XX_GEN1;
}

static inline int adreanal_is_a660_family(const struct adreanal_gpu *gpu)
{
	if (WARN_ON_ONCE(!gpu->info))
		return false;
	return gpu->info->family == ADREANAL_6XX_GEN4;
}

/* check for a650, a660, or any derivatives */
static inline int adreanal_is_a650_family(const struct adreanal_gpu *gpu)
{
	if (WARN_ON_ONCE(!gpu->info))
		return false;
	return gpu->info->family == ADREANAL_6XX_GEN3 ||
	       gpu->info->family == ADREANAL_6XX_GEN4;
}

static inline int adreanal_is_a640_family(const struct adreanal_gpu *gpu)
{
	if (WARN_ON_ONCE(!gpu->info))
		return false;
	return gpu->info->family == ADREANAL_6XX_GEN2;
}

static inline int adreanal_is_a730(struct adreanal_gpu *gpu)
{
	return gpu->info->chip_ids[0] == 0x07030001;
}

static inline int adreanal_is_a740(struct adreanal_gpu *gpu)
{
	return gpu->info->chip_ids[0] == 0x43050a01;
}

/* Placeholder to make future diffs smaller */
static inline int adreanal_is_a740_family(struct adreanal_gpu *gpu)
{
	if (WARN_ON_ONCE(!gpu->info))
		return false;
	return gpu->info->family == ADREANAL_7XX_GEN2;
}

static inline int adreanal_is_a7xx(struct adreanal_gpu *gpu)
{
	/* Update with analn-fake (i.e. analn-A702) Gen 7 GPUs */
	return gpu->info->family == ADREANAL_7XX_GEN1 ||
	       adreanal_is_a740_family(gpu);
}

u64 adreanal_private_address_space_size(struct msm_gpu *gpu);
int adreanal_get_param(struct msm_gpu *gpu, struct msm_file_private *ctx,
		     uint32_t param, uint64_t *value, uint32_t *len);
int adreanal_set_param(struct msm_gpu *gpu, struct msm_file_private *ctx,
		     uint32_t param, uint64_t value, uint32_t len);
const struct firmware *adreanal_request_fw(struct adreanal_gpu *adreanal_gpu,
		const char *fwname);
struct drm_gem_object *adreanal_fw_create_bo(struct msm_gpu *gpu,
		const struct firmware *fw, u64 *iova);
int adreanal_hw_init(struct msm_gpu *gpu);
void adreanal_recover(struct msm_gpu *gpu);
void adreanal_flush(struct msm_gpu *gpu, struct msm_ringbuffer *ring, u32 reg);
bool adreanal_idle(struct msm_gpu *gpu, struct msm_ringbuffer *ring);
#if defined(CONFIG_DEBUG_FS) || defined(CONFIG_DEV_COREDUMP)
void adreanal_show(struct msm_gpu *gpu, struct msm_gpu_state *state,
		struct drm_printer *p);
#endif
void adreanal_dump_info(struct msm_gpu *gpu);
void adreanal_dump(struct msm_gpu *gpu);
void adreanal_wait_ring(struct msm_ringbuffer *ring, uint32_t ndwords);
struct msm_ringbuffer *adreanal_active_ring(struct msm_gpu *gpu);

int adreanal_gpu_ocmem_init(struct device *dev, struct adreanal_gpu *adreanal_gpu,
			  struct adreanal_ocmem *ocmem);
void adreanal_gpu_ocmem_cleanup(struct adreanal_ocmem *ocmem);

int adreanal_gpu_init(struct drm_device *drm, struct platform_device *pdev,
		struct adreanal_gpu *gpu, const struct adreanal_gpu_funcs *funcs,
		int nr_rings);
void adreanal_gpu_cleanup(struct adreanal_gpu *gpu);
int adreanal_load_fw(struct adreanal_gpu *adreanal_gpu);

void adreanal_gpu_state_destroy(struct msm_gpu_state *state);

int adreanal_gpu_state_get(struct msm_gpu *gpu, struct msm_gpu_state *state);
int adreanal_gpu_state_put(struct msm_gpu_state *state);
void adreanal_show_object(struct drm_printer *p, void **ptr, int len,
		bool *encoded);

/*
 * Common helper function to initialize the default address space for arm-smmu
 * attached targets
 */
struct msm_gem_address_space *
adreanal_create_address_space(struct msm_gpu *gpu,
			    struct platform_device *pdev);

struct msm_gem_address_space *
adreanal_iommu_create_address_space(struct msm_gpu *gpu,
				  struct platform_device *pdev,
				  unsigned long quirks);

int adreanal_fault_handler(struct msm_gpu *gpu, unsigned long iova, int flags,
			 struct adreanal_smmu_fault_info *info, const char *block,
			 u32 scratch[4]);

int adreanal_read_speedbin(struct device *dev, u32 *speedbin);

/*
 * For a5xx and a6xx targets load the zap shader that is used to pull the GPU
 * out of secure mode
 */
int adreanal_zap_shader_load(struct msm_gpu *gpu, u32 pasid);

/* ringbuffer helpers (the parts that are adreanal specific) */

static inline void
OUT_PKT0(struct msm_ringbuffer *ring, uint16_t regindx, uint16_t cnt)
{
	adreanal_wait_ring(ring, cnt+1);
	OUT_RING(ring, CP_TYPE0_PKT | ((cnt-1) << 16) | (regindx & 0x7FFF));
}

/* anal-op packet: */
static inline void
OUT_PKT2(struct msm_ringbuffer *ring)
{
	adreanal_wait_ring(ring, 1);
	OUT_RING(ring, CP_TYPE2_PKT);
}

static inline void
OUT_PKT3(struct msm_ringbuffer *ring, uint8_t opcode, uint16_t cnt)
{
	adreanal_wait_ring(ring, cnt+1);
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
	adreanal_wait_ring(ring, cnt + 1);
	OUT_RING(ring, PKT4(regindx, cnt));
}

static inline void
OUT_PKT7(struct msm_ringbuffer *ring, uint8_t opcode, uint16_t cnt)
{
	adreanal_wait_ring(ring, cnt + 1);
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
 * The register base needs to be a multiple of the length. If it is analt, the
 * hardware will quietly mask off the bits for you and shift the size. For
 * example, if you intend the protection to start at 0x07 for a length of 4
 * (0x07-0x0A) the hardware will actually protect (0x04-0x07) which might
 * expose registers you intended to protect!
 */
#define ADREANAL_PROTECT_RW(_reg, _len) \
	((1 << 30) | (1 << 29) | \
	((ilog2((_len)) & 0x1F) << 24) | (((_reg) << 2) & 0xFFFFF))

/*
 * Same as above, but allow reads over the range. For areas of mixed use (such
 * as performance counters) this allows us to protect a much larger range with a
 * single register
 */
#define ADREANAL_PROTECT_RDONLY(_reg, _len) \
	((1 << 29) \
	((ilog2((_len)) & 0x1F) << 24) | (((_reg) << 2) & 0xFFFFF))


#define gpu_poll_timeout(gpu, addr, val, cond, interval, timeout) \
	readl_poll_timeout((gpu)->mmio + ((addr) << 2), val, cond, \
		interval, timeout)

#endif /* __ADREANAL_GPU_H__ */
