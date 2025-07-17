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

/**
 * @enum adreno_family: identify generation and possibly sub-generation
 *
 * In some cases there are distinct sub-generations within a major revision
 * so it helps to be able to group the GPU devices by generation and if
 * necessary sub-generation.
 */
enum adreno_family {
	ADRENO_2XX_GEN1,  /* a20x */
	ADRENO_2XX_GEN2,  /* a22x */
	ADRENO_3XX,
	ADRENO_4XX,
	ADRENO_5XX,
	ADRENO_6XX_GEN1,  /* a630 family */
	ADRENO_6XX_GEN2,  /* a640 family */
	ADRENO_6XX_GEN3,  /* a650 family */
	ADRENO_6XX_GEN4,  /* a660 family */
	ADRENO_7XX_GEN1,  /* a730 family */
	ADRENO_7XX_GEN2,  /* a740 family */
	ADRENO_7XX_GEN3,  /* a750 family */
};

#define ADRENO_QUIRK_TWO_PASS_USE_WFI		BIT(0)
#define ADRENO_QUIRK_FAULT_DETECT_MASK		BIT(1)
#define ADRENO_QUIRK_LMLOADKILL_DISABLE		BIT(2)
#define ADRENO_QUIRK_HAS_HW_APRIV		BIT(3)
#define ADRENO_QUIRK_HAS_CACHED_COHERENT	BIT(4)
#define ADRENO_QUIRK_PREEMPTION			BIT(5)
#define ADRENO_QUIRK_4GB_VA			BIT(6)

/* Helper for formating the chip_id in the way that userspace tools like
 * crashdec expect.
 */
#define ADRENO_CHIPID_FMT "u.%u.%u.%u"
#define ADRENO_CHIPID_ARGS(_c) \
	(((_c) >> 24) & 0xff), \
	(((_c) >> 16) & 0xff), \
	(((_c) >> 8)  & 0xff), \
	((_c) & 0xff)

struct adreno_gpu_funcs {
	struct msm_gpu_funcs base;
	int (*get_timestamp)(struct msm_gpu *gpu, uint64_t *value);
};

struct adreno_reglist {
	u32 offset;
	u32 value;
};

struct adreno_speedbin {
	uint16_t fuse;
	uint16_t speedbin;
};

struct a6xx_info;

struct adreno_info {
	const char *machine;
	/**
	 * @chipids: Table of matching chip-ids
	 *
	 * Terminated with 0 sentinal
	 */
	uint32_t *chip_ids;
	enum adreno_family family;
	uint32_t revn;
	const char *fw[ADRENO_FW_MAX];
	uint32_t gmem;
	u64 quirks;
	struct msm_gpu *(*init)(struct drm_device *dev);
	const char *zapfw;
	u32 inactive_period;
	union {
		const struct a6xx_info *a6xx;
	};
	/**
	 * @speedbins: Optional table of fuse to speedbin mappings
	 *
	 * Consists of pairs of fuse, index mappings, terminated with
	 * {SHRT_MAX, 0} sentinal.
	 */
	struct adreno_speedbin *speedbins;
	u64 preempt_record_size;
};

#define ADRENO_CHIP_IDS(tbl...) (uint32_t[]) { tbl, 0 }

struct adreno_gpulist {
	const struct adreno_info *gpus;
	unsigned gpus_count;
};

#define DECLARE_ADRENO_GPULIST(name)                  \
const struct adreno_gpulist name ## _gpulist = {      \
	name ## _gpus, ARRAY_SIZE(name ## _gpus)      \
}

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
 *     .speedbins = ADRENO_SPEEDBINS(
 *                      { 0,   0 },
 *                      { 169, 1 },
 *                      { 174, 2 },
 *     ),
 */
#define ADRENO_SPEEDBINS(tbl...) (struct adreno_speedbin[]) { tbl {SHRT_MAX, 0} }

struct adreno_protect {
	const uint32_t *regs;
	uint32_t count;
	uint32_t count_max;
};

#define DECLARE_ADRENO_PROTECT(name, __count_max)	\
static const struct adreno_protect name = {		\
	.regs = name ## _regs,				\
	.count = ARRAY_SIZE(name ## _regs),		\
	.count_max = __count_max,			\
};

struct adreno_reglist_list {
	/** @reg: List of register **/
	const u32 *regs;
	/** @count: Number of registers in the list **/
	u32 count;
};

#define DECLARE_ADRENO_REGLIST_LIST(name)	\
static const struct adreno_reglist_list name = {		\
	.regs = name ## _regs,				\
	.count = ARRAY_SIZE(name ## _regs),		\
};

struct adreno_gpu {
	struct msm_gpu base;
	const struct adreno_info *info;
	uint32_t chip_id;
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

	struct {
		/**
		 * @rgb565_predicator: Unknown, introduced with A650 family,
		 * related to UBWC mode/ver 4
		 */
		u32 rgb565_predicator;
		/** @uavflagprd_inv: Unknown, introduced with A650 family */
		u32 uavflagprd_inv;
		/** @min_acc_len: Whether the minimum access length is 64 bits */
		u32 min_acc_len;
		/**
		 * @ubwc_swizzle: Whether to enable level 1, 2 & 3 bank swizzling.
		 *
		 * UBWC 1.0 always enables all three levels.
		 * UBWC 2.0 removes level 1 bank swizzling, leaving levels 2 & 3.
		 * UBWC 4.0 adds the optional ability to disable levels 2 & 3.
		 *
		 * This is a bitmask where BIT(0) enables level 1, BIT(1)
		 * controls level 2, and BIT(2) enables level 3.
		 */
		u32 ubwc_swizzle;
		/**
		 * @highest_bank_bit: Highest Bank Bit
		 *
		 * The Highest Bank Bit value represents the bit of the highest
		 * DDR bank.  This should ideally use DRAM type detection.
		 */
		u32 highest_bank_bit;
		u32 amsbc;
		/**
		 * @macrotile_mode: Macrotile Mode
		 *
		 * Whether to use 4-channel macrotiling mode or the newer
		 * 8-channel macrotiling mode introduced in UBWC 3.1. 0 is
		 * 4-channel and 1 is 8-channel.
		 */
		u32 macrotile_mode;
	} ubwc_config;

	/*
	 * Register offsets are different between some GPUs.
	 * GPU specific offsets will be exported by GPU specific
	 * code (a3xx_gpu.c) and stored in this common location.
	 */
	const unsigned int *reg_offsets;
	bool gmu_is_wrapper;

	bool has_ray_tracing;

	u64 uche_trap_base;
};
#define to_adreno_gpu(x) container_of(x, struct adreno_gpu, base)

struct adreno_ocmem {
	struct ocmem *ocmem;
	unsigned long base;
	void *hdl;
};

/* platform config data (ie. from DT, or pdata) */
struct adreno_platform_config {
	uint32_t chip_id;
	const struct adreno_info *info;
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

static inline uint8_t adreno_patchid(const struct adreno_gpu *gpu)
{
	/* It is probably ok to assume legacy "adreno_rev" format
	 * for all a6xx devices, but probably best to limit this
	 * to older things.
	 */
	WARN_ON_ONCE(gpu->info->family >= ADRENO_6XX_GEN1);
	return gpu->chip_id & 0xff;
}

static inline bool adreno_is_revn(const struct adreno_gpu *gpu, uint32_t revn)
{
	if (WARN_ON_ONCE(!gpu->info))
		return false;
	return gpu->info->revn == revn;
}

static inline bool adreno_has_gmu_wrapper(const struct adreno_gpu *gpu)
{
	return gpu->gmu_is_wrapper;
}

static inline bool adreno_is_a2xx(const struct adreno_gpu *gpu)
{
	if (WARN_ON_ONCE(!gpu->info))
		return false;
	return gpu->info->family <= ADRENO_2XX_GEN2;
}

static inline bool adreno_is_a20x(const struct adreno_gpu *gpu)
{
	if (WARN_ON_ONCE(!gpu->info))
		return false;
	return gpu->info->family == ADRENO_2XX_GEN1;
}

static inline bool adreno_is_a225(const struct adreno_gpu *gpu)
{
	return adreno_is_revn(gpu, 225);
}

static inline bool adreno_is_a305(const struct adreno_gpu *gpu)
{
	return adreno_is_revn(gpu, 305);
}

static inline bool adreno_is_a305b(const struct adreno_gpu *gpu)
{
	return gpu->info->chip_ids[0] == 0x03000512;
}

static inline bool adreno_is_a306(const struct adreno_gpu *gpu)
{
	/* yes, 307, because a305c is 306 */
	return adreno_is_revn(gpu, 307);
}

static inline bool adreno_is_a306a(const struct adreno_gpu *gpu)
{
	/* a306a (marketing name is a308) */
	return adreno_is_revn(gpu, 308);
}

static inline bool adreno_is_a320(const struct adreno_gpu *gpu)
{
	return adreno_is_revn(gpu, 320);
}

static inline bool adreno_is_a330(const struct adreno_gpu *gpu)
{
	return adreno_is_revn(gpu, 330);
}

static inline bool adreno_is_a330v2(const struct adreno_gpu *gpu)
{
	return adreno_is_a330(gpu) && (adreno_patchid(gpu) > 0);
}

static inline int adreno_is_a405(const struct adreno_gpu *gpu)
{
	return adreno_is_revn(gpu, 405);
}

static inline int adreno_is_a420(const struct adreno_gpu *gpu)
{
	return adreno_is_revn(gpu, 420);
}

static inline int adreno_is_a430(const struct adreno_gpu *gpu)
{
	return adreno_is_revn(gpu, 430);
}

static inline int adreno_is_a505(const struct adreno_gpu *gpu)
{
	return adreno_is_revn(gpu, 505);
}

static inline int adreno_is_a506(const struct adreno_gpu *gpu)
{
	return adreno_is_revn(gpu, 506);
}

static inline int adreno_is_a508(const struct adreno_gpu *gpu)
{
	return adreno_is_revn(gpu, 508);
}

static inline int adreno_is_a509(const struct adreno_gpu *gpu)
{
	return adreno_is_revn(gpu, 509);
}

static inline int adreno_is_a510(const struct adreno_gpu *gpu)
{
	return adreno_is_revn(gpu, 510);
}

static inline int adreno_is_a512(const struct adreno_gpu *gpu)
{
	return adreno_is_revn(gpu, 512);
}

static inline int adreno_is_a530(const struct adreno_gpu *gpu)
{
	return adreno_is_revn(gpu, 530);
}

static inline int adreno_is_a540(const struct adreno_gpu *gpu)
{
	return adreno_is_revn(gpu, 540);
}

static inline int adreno_is_a610(const struct adreno_gpu *gpu)
{
	return adreno_is_revn(gpu, 610);
}

static inline int adreno_is_a618(const struct adreno_gpu *gpu)
{
	return adreno_is_revn(gpu, 618);
}

static inline int adreno_is_a619(const struct adreno_gpu *gpu)
{
	return adreno_is_revn(gpu, 619);
}

static inline int adreno_is_a619_holi(const struct adreno_gpu *gpu)
{
	return adreno_is_a619(gpu) && adreno_has_gmu_wrapper(gpu);
}

static inline int adreno_is_a621(const struct adreno_gpu *gpu)
{
	return gpu->info->chip_ids[0] == 0x06020100;
}

static inline int adreno_is_a623(const struct adreno_gpu *gpu)
{
	return gpu->info->chip_ids[0] == 0x06020300;
}

static inline int adreno_is_a630(const struct adreno_gpu *gpu)
{
	return adreno_is_revn(gpu, 630);
}

static inline int adreno_is_a640(const struct adreno_gpu *gpu)
{
	return adreno_is_revn(gpu, 640);
}

static inline int adreno_is_a650(const struct adreno_gpu *gpu)
{
	return adreno_is_revn(gpu, 650);
}

static inline int adreno_is_7c3(const struct adreno_gpu *gpu)
{
	return gpu->info->chip_ids[0] == 0x06030500;
}

static inline int adreno_is_a660(const struct adreno_gpu *gpu)
{
	return adreno_is_revn(gpu, 660);
}

static inline int adreno_is_a680(const struct adreno_gpu *gpu)
{
	return adreno_is_revn(gpu, 680);
}

static inline int adreno_is_a663(const struct adreno_gpu *gpu)
{
	return gpu->info->chip_ids[0] == 0x06060300;
}

static inline int adreno_is_a690(const struct adreno_gpu *gpu)
{
	return gpu->info->chip_ids[0] == 0x06090000;
}

static inline int adreno_is_a702(const struct adreno_gpu *gpu)
{
	return gpu->info->chip_ids[0] == 0x07000200;
}

static inline int adreno_is_a610_family(const struct adreno_gpu *gpu)
{
	if (WARN_ON_ONCE(!gpu->info))
		return false;

	/* TODO: A612 */
	return adreno_is_a610(gpu) || adreno_is_a702(gpu);
}

/* TODO: 615/616 */
static inline int adreno_is_a615_family(const struct adreno_gpu *gpu)
{
	return adreno_is_a618(gpu) ||
	       adreno_is_a619(gpu);
}

static inline int adreno_is_a630_family(const struct adreno_gpu *gpu)
{
	if (WARN_ON_ONCE(!gpu->info))
		return false;
	return gpu->info->family == ADRENO_6XX_GEN1;
}

static inline int adreno_is_a660_family(const struct adreno_gpu *gpu)
{
	if (WARN_ON_ONCE(!gpu->info))
		return false;
	return gpu->info->family == ADRENO_6XX_GEN4;
}

/* check for a650, a660, or any derivatives */
static inline int adreno_is_a650_family(const struct adreno_gpu *gpu)
{
	if (WARN_ON_ONCE(!gpu->info))
		return false;
	return gpu->info->family == ADRENO_6XX_GEN3 ||
	       gpu->info->family == ADRENO_6XX_GEN4;
}

static inline int adreno_is_a640_family(const struct adreno_gpu *gpu)
{
	if (WARN_ON_ONCE(!gpu->info))
		return false;
	return gpu->info->family == ADRENO_6XX_GEN2;
}

static inline int adreno_is_a730(struct adreno_gpu *gpu)
{
	return gpu->info->chip_ids[0] == 0x07030001;
}

static inline int adreno_is_a740(struct adreno_gpu *gpu)
{
	return gpu->info->chip_ids[0] == 0x43050a01;
}

static inline int adreno_is_a750(struct adreno_gpu *gpu)
{
	return gpu->info->chip_ids[0] == 0x43051401;
}

static inline int adreno_is_x185(struct adreno_gpu *gpu)
{
	return gpu->info->chip_ids[0] == 0x43050c01;
}

static inline int adreno_is_a740_family(struct adreno_gpu *gpu)
{
	if (WARN_ON_ONCE(!gpu->info))
		return false;
	return gpu->info->family == ADRENO_7XX_GEN2 ||
	       gpu->info->family == ADRENO_7XX_GEN3;
}

static inline int adreno_is_a750_family(struct adreno_gpu *gpu)
{
	return gpu->info->family == ADRENO_7XX_GEN3;
}

static inline int adreno_is_a7xx(struct adreno_gpu *gpu)
{
	/* Update with non-fake (i.e. non-A702) Gen 7 GPUs */
	return gpu->info->family == ADRENO_7XX_GEN1 ||
	       adreno_is_a740_family(gpu);
}

/* Put vm_start above 32b to catch issues with not setting xyz_BASE_HI */
#define ADRENO_VM_START 0x100000000ULL
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
adreno_create_address_space(struct msm_gpu *gpu,
			    struct platform_device *pdev);

struct msm_gem_address_space *
adreno_iommu_create_address_space(struct msm_gpu *gpu,
				  struct platform_device *pdev,
				  unsigned long quirks);

int adreno_fault_handler(struct msm_gpu *gpu, unsigned long iova, int flags,
			 struct adreno_smmu_fault_info *info, const char *block,
			 u32 scratch[4]);

void adreno_check_and_reenable_stall(struct adreno_gpu *gpu);

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

#define PKT7(opcode, cnt) \
	(CP_TYPE7_PKT | (cnt << 0) | (PM4_PARITY(cnt) << 15) | \
		((opcode & 0x7F) << 16) | (PM4_PARITY(opcode) << 23))

static inline void
OUT_PKT7(struct msm_ringbuffer *ring, uint8_t opcode, uint16_t cnt)
{
	adreno_wait_ring(ring, cnt + 1);
	OUT_RING(ring, PKT7(opcode, cnt));
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
