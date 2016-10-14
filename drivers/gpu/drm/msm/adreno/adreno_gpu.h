/*
 * Copyright (C) 2013 Red Hat
 * Author: Rob Clark <robdclark@gmail.com>
 *
 * Copyright (c) 2014 The Linux Foundation. All rights reserved.
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
/**
 * adreno_regs: List of registers that are used in across all
 * 3D devices. Each device type has different offset value for the same
 * register, so an array of register offsets are declared for every device
 * and are indexed by the enumeration values defined in this enum
 */
enum adreno_regs {
	REG_ADRENO_CP_DEBUG,
	REG_ADRENO_CP_ME_RAM_WADDR,
	REG_ADRENO_CP_ME_RAM_DATA,
	REG_ADRENO_CP_PFP_UCODE_DATA,
	REG_ADRENO_CP_PFP_UCODE_ADDR,
	REG_ADRENO_CP_WFI_PEND_CTR,
	REG_ADRENO_CP_RB_BASE,
	REG_ADRENO_CP_RB_RPTR_ADDR,
	REG_ADRENO_CP_RB_RPTR,
	REG_ADRENO_CP_RB_WPTR,
	REG_ADRENO_CP_PROTECT_CTRL,
	REG_ADRENO_CP_ME_CNTL,
	REG_ADRENO_CP_RB_CNTL,
	REG_ADRENO_CP_IB1_BASE,
	REG_ADRENO_CP_IB1_BUFSZ,
	REG_ADRENO_CP_IB2_BASE,
	REG_ADRENO_CP_IB2_BUFSZ,
	REG_ADRENO_CP_TIMESTAMP,
	REG_ADRENO_CP_ME_RAM_RADDR,
	REG_ADRENO_CP_ROQ_ADDR,
	REG_ADRENO_CP_ROQ_DATA,
	REG_ADRENO_CP_MERCIU_ADDR,
	REG_ADRENO_CP_MERCIU_DATA,
	REG_ADRENO_CP_MERCIU_DATA2,
	REG_ADRENO_CP_MEQ_ADDR,
	REG_ADRENO_CP_MEQ_DATA,
	REG_ADRENO_CP_HW_FAULT,
	REG_ADRENO_CP_PROTECT_STATUS,
	REG_ADRENO_SCRATCH_ADDR,
	REG_ADRENO_SCRATCH_UMSK,
	REG_ADRENO_SCRATCH_REG2,
	REG_ADRENO_RBBM_STATUS,
	REG_ADRENO_RBBM_PERFCTR_CTL,
	REG_ADRENO_RBBM_PERFCTR_LOAD_CMD0,
	REG_ADRENO_RBBM_PERFCTR_LOAD_CMD1,
	REG_ADRENO_RBBM_PERFCTR_LOAD_CMD2,
	REG_ADRENO_RBBM_PERFCTR_PWR_1_LO,
	REG_ADRENO_RBBM_INT_0_MASK,
	REG_ADRENO_RBBM_INT_0_STATUS,
	REG_ADRENO_RBBM_AHB_ERROR_STATUS,
	REG_ADRENO_RBBM_PM_OVERRIDE2,
	REG_ADRENO_RBBM_AHB_CMD,
	REG_ADRENO_RBBM_INT_CLEAR_CMD,
	REG_ADRENO_RBBM_SW_RESET_CMD,
	REG_ADRENO_RBBM_CLOCK_CTL,
	REG_ADRENO_RBBM_AHB_ME_SPLIT_STATUS,
	REG_ADRENO_RBBM_AHB_PFP_SPLIT_STATUS,
	REG_ADRENO_VPC_DEBUG_RAM_SEL,
	REG_ADRENO_VPC_DEBUG_RAM_READ,
	REG_ADRENO_VSC_SIZE_ADDRESS,
	REG_ADRENO_VFD_CONTROL_0,
	REG_ADRENO_VFD_INDEX_MAX,
	REG_ADRENO_SP_VS_PVT_MEM_ADDR_REG,
	REG_ADRENO_SP_FS_PVT_MEM_ADDR_REG,
	REG_ADRENO_SP_VS_OBJ_START_REG,
	REG_ADRENO_SP_FS_OBJ_START_REG,
	REG_ADRENO_PA_SC_AA_CONFIG,
	REG_ADRENO_SQ_GPR_MANAGEMENT,
	REG_ADRENO_SQ_INST_STORE_MANAGMENT,
	REG_ADRENO_TP0_CHICKEN,
	REG_ADRENO_RBBM_RBBM_CTL,
	REG_ADRENO_UCHE_INVALIDATE0,
	REG_ADRENO_RBBM_PERFCTR_LOAD_VALUE_LO,
	REG_ADRENO_RBBM_PERFCTR_LOAD_VALUE_HI,
	REG_ADRENO_REGISTER_MAX,
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
	uint32_t gmem;
	struct msm_gpu *(*init)(struct drm_device *dev);
};

const struct adreno_info *adreno_info(struct adreno_rev rev);

struct adreno_rbmemptrs {
	volatile uint32_t rptr;
	volatile uint32_t wptr;
	volatile uint32_t fence;
};

struct adreno_gpu {
	struct msm_gpu base;
	struct adreno_rev rev;
	const struct adreno_info *info;
	uint32_t gmem;  /* actual gmem size */
	uint32_t revn;  /* numeric revision name */
	const struct adreno_gpu_funcs *funcs;

	/* interesting register offsets to dump: */
	const unsigned int *registers;

	/* firmware: */
	const struct firmware *pm4, *pfp;

	/* ringbuffer rptr/wptr: */
	// TODO should this be in msm_ringbuffer?  I think it would be
	// different for z180..
	struct adreno_rbmemptrs *memptrs;
	struct drm_gem_object *memptrs_bo;
	uint32_t memptrs_iova;

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
	uint32_t fast_rate, slow_rate, bus_freq;
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

int adreno_get_param(struct msm_gpu *gpu, uint32_t param, uint64_t *value);
int adreno_hw_init(struct msm_gpu *gpu);
uint32_t adreno_last_fence(struct msm_gpu *gpu);
void adreno_recover(struct msm_gpu *gpu);
void adreno_submit(struct msm_gpu *gpu, struct msm_gem_submit *submit,
		struct msm_file_private *ctx);
void adreno_flush(struct msm_gpu *gpu);
void adreno_idle(struct msm_gpu *gpu);
#ifdef CONFIG_DEBUG_FS
void adreno_show(struct msm_gpu *gpu, struct seq_file *m);
#endif
void adreno_dump_info(struct msm_gpu *gpu);
void adreno_dump(struct msm_gpu *gpu);
void adreno_wait_ring(struct msm_gpu *gpu, uint32_t ndwords);

int adreno_gpu_init(struct drm_device *drm, struct platform_device *pdev,
		struct adreno_gpu *gpu, const struct adreno_gpu_funcs *funcs);
void adreno_gpu_cleanup(struct adreno_gpu *gpu);


/* ringbuffer helpers (the parts that are adreno specific) */

static inline void
OUT_PKT0(struct msm_ringbuffer *ring, uint16_t regindx, uint16_t cnt)
{
	adreno_wait_ring(ring->gpu, cnt+1);
	OUT_RING(ring, CP_TYPE0_PKT | ((cnt-1) << 16) | (regindx & 0x7FFF));
}

/* no-op packet: */
static inline void
OUT_PKT2(struct msm_ringbuffer *ring)
{
	adreno_wait_ring(ring->gpu, 1);
	OUT_RING(ring, CP_TYPE2_PKT);
}

static inline void
OUT_PKT3(struct msm_ringbuffer *ring, uint8_t opcode, uint16_t cnt)
{
	adreno_wait_ring(ring->gpu, cnt+1);
	OUT_RING(ring, CP_TYPE3_PKT | ((cnt-1) << 16) | ((opcode & 0xFF) << 8));
}

/*
 * adreno_checkreg_off() - Checks the validity of a register enum
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

#endif /* __ADRENO_GPU_H__ */
