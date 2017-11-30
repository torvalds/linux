/*
 * Copyright (C) 2013 Red Hat
 * Author: Rob Clark <robdclark@gmail.com>
 *
 * Copyright (c) 2014,2017 The Linux Foundation. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 as published by
 * the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#ifndef __ADRENO_GPU_H__
#define __ADRENO_GPU_H__

#include <linux/firmware.h>

#include "msm_gpu.h"

#include "adreno_common.xml.h"
#include "adreno_pm4.xml.h"

#define REG_ADRENO_DEFINE(_offset, _reg) [_offset] = (_reg) + 1
#define REG_SKIP ~0
#define REG_ADRENO_SKIP(_offset) [_offset] = REG_SKIP

/**
 * adreno_regs: List of registers that are used in across all
 * 3D devices. Each device type has different offset value for the same
 * register, so an array of register offsets are declared for every device
 * and are indexed by the enumeration values defined in this enum
 */
enum adreno_regs {
	REG_ADRENO_CP_RB_BASE,
	REG_ADRENO_CP_RB_BASE_HI,
	REG_ADRENO_CP_RB_RPTR_ADDR,
	REG_ADRENO_CP_RB_RPTR_ADDR_HI,
	REG_ADRENO_CP_RB_RPTR,
	REG_ADRENO_CP_RB_WPTR,
	REG_ADRENO_CP_RB_CNTL,
	REG_ADRENO_REGISTER_MAX,
};

enum adreno_quirks {
	ADRENO_QUIRK_TWO_PASS_USE_WFI = 1,
	ADRENO_QUIRK_FAULT_DETECT_MASK = 2,
};

struct adreno_rev {
	uint8_t  core;
	uint8_t  major;
	uint8_t  minor;
	uint8_t  patchid;
};

#define ADRENO_REV(core, major, minor, patchid) \
	((struct adreno_rev){ core, major, minor, patchid })

struct adreno_gpu_funcs {
	struct msm_gpu_funcs base;
	int (*get_timestamp)(struct msm_gpu *gpu, uint64_t *value);
};

struct adreno_info {
	struct adreno_rev rev;
	uint32_t revn;
	const char *name;
	const char *pm4fw, *pfpfw;
	const char *gpmufw;
	uint32_t gmem;
	enum adreno_quirks quirks;
	struct msm_gpu *(*init)(struct drm_device *dev);
	const char *zapfw;
};

const struct adreno_info *adreno_info(struct adreno_rev rev);

struct adreno_gpu {
	struct msm_gpu base;
	struct adreno_rev rev;
	const struct adreno_info *info;
	uint32_t gmem;  /* actual gmem size */
	uint32_t revn;  /* numeric revision name */
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
	const struct firmware *pm4, *pfp;

	/*
	 * Register offsets are different between some GPUs.
	 * GPU specific offsets will be exported by GPU specific
	 * code (a3xx_gpu.c) and stored in this common location.
	 */
	const unsigned int *reg_offsets;
};
#define to_adreno_gpu(x) container_of(x, struct adreno_gpu, base)

/* platform config data (ie. from DT, or pdata) */
struct adreno_platform_config {
	struct adreno_rev rev;
	uint32_t fast_rate, bus_freq;
#ifdef DOWNSTREAM_CONFIG_MSM_BUS_SCALING
	struct msm_bus_scale_pdata *bus_scale_table;
#endif
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


static inline bool adreno_is_a3xx(struct adreno_gpu *gpu)
{
	return (gpu->revn >= 300) && (gpu->revn < 400);
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

static inline bool adreno_is_a4xx(struct adreno_gpu *gpu)
{
	return (gpu->revn >= 400) && (gpu->revn < 500);
}

static inline int adreno_is_a420(struct adreno_gpu *gpu)
{
	return gpu->revn == 420;
}

static inline int adreno_is_a430(struct adreno_gpu *gpu)
{
       return gpu->revn == 430;
}

static inline int adreno_is_a530(struct adreno_gpu *gpu)
{
	return gpu->revn == 530;
}

int adreno_get_param(struct msm_gpu *gpu, uint32_t param, uint64_t *value);
const struct firmware *adreno_request_fw(struct adreno_gpu *adreno_gpu,
		const char *fwname);
int adreno_hw_init(struct msm_gpu *gpu);
void adreno_recover(struct msm_gpu *gpu);
void adreno_submit(struct msm_gpu *gpu, struct msm_gem_submit *submit,
		struct msm_file_private *ctx);
void adreno_flush(struct msm_gpu *gpu, struct msm_ringbuffer *ring);
bool adreno_idle(struct msm_gpu *gpu, struct msm_ringbuffer *ring);
#ifdef CONFIG_DEBUG_FS
void adreno_show(struct msm_gpu *gpu, struct seq_file *m);
#endif
void adreno_dump_info(struct msm_gpu *gpu);
void adreno_dump(struct msm_gpu *gpu);
void adreno_wait_ring(struct msm_ringbuffer *ring, uint32_t ndwords);
struct msm_ringbuffer *adreno_active_ring(struct msm_gpu *gpu);

int adreno_gpu_init(struct drm_device *drm, struct platform_device *pdev,
		struct adreno_gpu *gpu, const struct adreno_gpu_funcs *funcs,
		int nr_rings);
void adreno_gpu_cleanup(struct adreno_gpu *gpu);


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

/*
 * adreno_reg_check() - Checks the validity of a register enum
 * @gpu:		Pointer to struct adreno_gpu
 * @offset_name:	The register enum that is checked
 */
static inline bool adreno_reg_check(struct adreno_gpu *gpu,
		enum adreno_regs offset_name)
{
	if (offset_name >= REG_ADRENO_REGISTER_MAX ||
			!gpu->reg_offsets[offset_name]) {
		BUG();
	}

	/*
	 * REG_SKIP is a special value that tell us that the register in
	 * question isn't implemented on target but don't trigger a BUG(). This
	 * is used to cleanly implement adreno_gpu_write64() and
	 * adreno_gpu_read64() in a generic fashion
	 */
	if (gpu->reg_offsets[offset_name] == REG_SKIP)
		return false;

	return true;
}

static inline u32 adreno_gpu_read(struct adreno_gpu *gpu,
		enum adreno_regs offset_name)
{
	u32 reg = gpu->reg_offsets[offset_name];
	u32 val = 0;
	if(adreno_reg_check(gpu,offset_name))
		val = gpu_read(&gpu->base, reg - 1);
	return val;
}

static inline void adreno_gpu_write(struct adreno_gpu *gpu,
		enum adreno_regs offset_name, u32 data)
{
	u32 reg = gpu->reg_offsets[offset_name];
	if(adreno_reg_check(gpu, offset_name))
		gpu_write(&gpu->base, reg - 1, data);
}

struct msm_gpu *a3xx_gpu_init(struct drm_device *dev);
struct msm_gpu *a4xx_gpu_init(struct drm_device *dev);
struct msm_gpu *a5xx_gpu_init(struct drm_device *dev);

static inline void adreno_gpu_write64(struct adreno_gpu *gpu,
		enum adreno_regs lo, enum adreno_regs hi, u64 data)
{
	adreno_gpu_write(gpu, lo, lower_32_bits(data));
	adreno_gpu_write(gpu, hi, upper_32_bits(data));
}

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

#endif /* __ADRENO_GPU_H__ */
