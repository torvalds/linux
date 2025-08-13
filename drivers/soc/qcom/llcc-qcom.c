// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2017-2019, The Linux Foundation. All rights reserved.
 *
 */

#include <linux/bitfield.h>
#include <linux/bitmap.h>
#include <linux/bitops.h>
#include <linux/cleanup.h>
#include <linux/device.h>
#include <linux/io.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/nvmem-consumer.h>
#include <linux/of.h>
#include <linux/regmap.h>
#include <linux/sizes.h>
#include <linux/slab.h>
#include <linux/soc/qcom/llcc-qcom.h>

#define ACTIVATE                      BIT(0)
#define DEACTIVATE                    BIT(1)
#define ACT_CLEAR                     BIT(0)
#define ACT_COMPLETE                  BIT(4)
#define ACT_CTRL_OPCODE_ACTIVATE      BIT(0)
#define ACT_CTRL_OPCODE_DEACTIVATE    BIT(1)
#define ACT_CTRL_ACT_TRIG             BIT(0)
#define ACT_CTRL_OPCODE_SHIFT         1
#define ATTR1_PROBE_TARGET_WAYS_SHIFT 2
#define ATTR1_FIXED_SIZE_SHIFT        3
#define ATTR1_PRIORITY_SHIFT          4
#define ATTR1_MAX_CAP_SHIFT           16
#define ATTR0_RES_WAYS_MASK           GENMASK(15, 0)
#define ATTR0_BONUS_WAYS_MASK         GENMASK(31, 16)
#define ATTR0_BONUS_WAYS_SHIFT        16
#define ATTR2_PROBE_TARGET_WAYS_MASK  BIT(4)
#define ATTR2_FIXED_SIZE_MASK         BIT(8)
#define ATTR2_PRIORITY_MASK           GENMASK(14, 12)
#define ATTR2_PARENT_SCID_MASK        GENMASK(21, 16)
#define ATTR2_IN_A_GROUP_MASK         BIT(24)
#define LLCC_STATUS_READ_DELAY        100

#define CACHE_LINE_SIZE_SHIFT         6

#define LLCC_LB_CNT_MASK              GENMASK(31, 28)
#define LLCC_LB_CNT_SHIFT             28

#define MAX_CAP_TO_BYTES(n)           (n * SZ_1K)
#define LLCC_TRP_ACT_CTRLn(n)         (n * SZ_4K)
#define LLCC_TRP_ACT_CLEARn(n)        (8 + n * SZ_4K)
#define LLCC_TRP_STATUSn(n)           (4 + n * SZ_4K)
#define LLCC_TRP_ATTR0_CFGn(n)        (0x21000 + SZ_8 * n)
#define LLCC_TRP_ATTR1_CFGn(n)        (0x21004 + SZ_8 * n)
#define LLCC_TRP_ATTR2_CFGn(n)        (0x21100 + SZ_4 * n)
#define LLCC_V6_TRP_ATTR0_CFGn(n)     (cfg->reg_offset[LLCC_TRP_ATTR0_CFG] + SZ_64 * (n))
#define LLCC_V6_TRP_ATTR1_CFGn(n)     (cfg->reg_offset[LLCC_TRP_ATTR1_CFG] + SZ_64 * (n))
#define LLCC_V6_TRP_ATTR2_CFGn(n)     (cfg->reg_offset[LLCC_TRP_ATTR2_CFG] + SZ_64 * (n))
#define LLCC_V6_TRP_ATTR3_CFGn(n)     (cfg->reg_offset[LLCC_TRP_ATTR3_CFG] + SZ_64 * (n))

#define LLCC_TRP_SCID_DIS_CAP_ALLOC   0x21f00
#define LLCC_TRP_PCB_ACT              0x21f04
#define LLCC_TRP_ALGO_CFG1	      0x21f0c
#define LLCC_TRP_ALGO_CFG2	      0x21f10
#define LLCC_TRP_ALGO_CFG3	      0x21f14
#define LLCC_TRP_ALGO_CFG4	      0x21f18
#define LLCC_TRP_ALGO_CFG5	      0x21f1c
#define LLCC_TRP_WRSC_EN              0x21f20
#define LLCC_TRP_ALGO_CFG6	      0x21f24
#define LLCC_TRP_ALGO_CFG7	      0x21f28
#define LLCC_TRP_WRSC_CACHEABLE_EN    0x21f2c
#define LLCC_TRP_ALGO_CFG8	      0x21f30

#define LLCC_VERSION_2_0_0_0          0x02000000
#define LLCC_VERSION_2_1_0_0          0x02010000
#define LLCC_VERSION_4_1_0_0          0x04010000
#define LLCC_VERSION_6_0_0_0          0X06000000

/**
 * struct llcc_slice_config - Data associated with the llcc slice
 * @usecase_id: Unique id for the client's use case
 * @slice_id: llcc slice id for each client
 * @max_cap: The maximum capacity of the cache slice provided in KB
 * @priority: Priority of the client used to select victim line for replacement
 * @fixed_size: Boolean indicating if the slice has a fixed capacity
 * @bonus_ways: Bonus ways are additional ways to be used for any slice,
 *		if client ends up using more than reserved cache ways. Bonus
 *		ways are allocated only if they are not reserved for some
 *		other client.
 * @res_ways: Reserved ways for the cache slice, the reserved ways cannot
 *		be used by any other client than the one its assigned to.
 * @cache_mode: Each slice operates as a cache, this controls the mode of the
 *             slice: normal or TCM(Tightly Coupled Memory)
 * @probe_target_ways: Determines what ways to probe for access hit. When
 *                    configured to 1 only bonus and reserved ways are probed.
 *                    When configured to 0 all ways in llcc are probed.
 * @dis_cap_alloc: Disable capacity based allocation for a client
 * @retain_on_pc: If this bit is set and client has maintained active vote
 *               then the ways assigned to this client are not flushed on power
 *               collapse.
 * @activate_on_init: Activate the slice immediately after it is programmed
 * @write_scid_en: Bit enables write cache support for a given scid.
 * @write_scid_cacheable_en: Enables write cache cacheable support for a
 *			     given scid (not supported on v2 or older hardware).
 * @stale_en: Bit enables stale.
 * @stale_cap_en: Bit enables stale only if current scid is over-cap.
 * @mru_uncap_en: Roll-over on reserved cache ways if current scid is
 *                under-cap.
 * @mru_rollover: Roll-over on reserved cache ways.
 * @alloc_oneway_en: Allways allocate one way on over-cap even if there's no
 *                   same-scid lines for replacement.
 * @ovcap_en: Once current scid is over-capacity, allocate other over-cap SCID.
 * @ovcap_prio: Once current scid is over-capacity, allocate other low priority
 *              over-cap scid. Depends on corresponding bit being set in
 *              ovcap_en.
 * @vict_prio: When current scid is under-capacity, allocate over other
 *             lower-than victim priority-line threshold scid.
 * @parent_slice_id: For grouped slices, specifies the slice id of the parent.
 */
struct llcc_slice_config {
	u32 usecase_id;
	u32 slice_id;
	u32 max_cap;
	u32 priority;
	bool fixed_size;
	u32 bonus_ways;
	u32 res_ways;
	u32 cache_mode;
	u32 probe_target_ways;
	bool dis_cap_alloc;
	bool retain_on_pc;
	bool activate_on_init;
	bool write_scid_en;
	bool write_scid_cacheable_en;
	bool stale_en;
	bool stale_cap_en;
	bool mru_uncap_en;
	bool mru_rollover;
	bool alloc_oneway_en;
	bool ovcap_en;
	bool ovcap_prio;
	bool vict_prio;
	u32 parent_slice_id;
};

struct qcom_llcc_config {
	const struct llcc_slice_config *sct_data;
	const u32 *reg_offset;
	const struct llcc_edac_reg_offset *edac_reg_offset;
	u32 max_cap_shift; /* instead of ATTR1_MAX_CAP_SHIFT */
	u32 num_banks;
	int size;
	bool skip_llcc_cfg;
	bool no_edac;
	bool irq_configured;
	bool no_broadcast_register;
};

struct qcom_sct_config {
	const struct qcom_llcc_config *llcc_config;
	int num_config;
};

enum llcc_reg_offset {
	LLCC_COMMON_HW_INFO,
	LLCC_COMMON_STATUS0,
	LLCC_TRP_ATTR0_CFG,
	LLCC_TRP_ATTR1_CFG,
	LLCC_TRP_ATTR2_CFG,
	LLCC_TRP_ATTR3_CFG,
	LLCC_TRP_SID_DIS_CAP_ALLOC,
	LLCC_TRP_ALGO_STALE_EN,
	LLCC_TRP_ALGO_STALE_CAP_EN,
	LLCC_TRP_ALGO_MRU0,
	LLCC_TRP_ALGO_MRU1,
	LLCC_TRP_ALGO_ALLOC0,
	LLCC_TRP_ALGO_ALLOC1,
	LLCC_TRP_ALGO_ALLOC2,
	LLCC_TRP_ALGO_ALLOC3,
	LLCC_TRP_WRS_EN,
	LLCC_TRP_WRS_CACHEABLE_EN,
};

static const struct llcc_slice_config ipq5424_data[] =  {
	{
		.usecase_id = LLCC_CPUSS,
		.slice_id = 1,
		.max_cap = 768,
		.priority = 1,
		.bonus_ways = 0xFFFF,
		.retain_on_pc = true,
		.activate_on_init = true,
		.write_scid_cacheable_en = true,
		.stale_en = true,
		.stale_cap_en = true,
		.alloc_oneway_en = true,
		.ovcap_en = true,
		.ovcap_prio = true,
		.vict_prio = true,
	},
	{
		.usecase_id = LLCC_VIDSC0,
		.slice_id = 2,
		.max_cap = 256,
		.priority = 2,
		.fixed_size = true,
		.bonus_ways = 0xF000,
		.retain_on_pc = true,
		.activate_on_init = true,
		.write_scid_cacheable_en = true,
		.stale_en = true,
		.stale_cap_en = true,
	},
};

static const struct llcc_slice_config sa8775p_data[] =  {
	{
		.usecase_id = LLCC_CPUSS,
		.slice_id = 1,
		.max_cap = 2048,
		.priority = 1,
		.bonus_ways = 0xff,
		.cache_mode = 0,
		.retain_on_pc = true,
		.activate_on_init = true,
	}, {
		.usecase_id = LLCC_VIDSC0,
		.slice_id = 2,
		.max_cap = 512,
		.priority = 3,
		.fixed_size = true,
		.bonus_ways = 0xff,
		.cache_mode = 0,
		.retain_on_pc = true,
	}, {
		.usecase_id = LLCC_CPUSS1,
		.slice_id = 3,
		.max_cap = 1024,
		.priority = 1,
		.fixed_size = true,
		.bonus_ways = 0xff,
		.cache_mode = 0,
		.retain_on_pc = true,
	}, {
		.usecase_id = LLCC_CPUHWT,
		.slice_id = 5,
		.max_cap = 512,
		.priority = 1,
		.fixed_size = true,
		.bonus_ways = 0xff,
		.cache_mode = 0,
		.retain_on_pc = true,
	}, {
		.usecase_id = LLCC_AUDIO,
		.slice_id = 6,
		.max_cap = 1024,
		.priority = 1,
		.fixed_size = true,
		.bonus_ways = 0xff,
		.cache_mode = 0,
	}, {
		.usecase_id = LLCC_CMPT,
		.slice_id = 10,
		.max_cap = 4096,
		.priority = 1,
		.fixed_size = true,
		.bonus_ways = 0xff,
		.cache_mode = 0,
		.retain_on_pc = true,
	}, {
		.usecase_id = LLCC_GPUHTW,
		.slice_id = 11,
		.max_cap = 1024,
		.priority = 1,
		.fixed_size = true,
		.bonus_ways = 0xff,
		.cache_mode = 0,
		.retain_on_pc = true,
	}, {
		.usecase_id = LLCC_GPU,
		.slice_id = 12,
		.max_cap = 1024,
		.priority = 1,
		.fixed_size = true,
		.bonus_ways = 0xff,
		.cache_mode = 0,
		.retain_on_pc = true,
		.write_scid_en = true,
	}, {
		.usecase_id = LLCC_MMUHWT,
		.slice_id = 13,
		.max_cap = 1024,
		.priority = 1,
		.fixed_size = true,
		.bonus_ways = 0xff,
		.cache_mode = 0,
		.activate_on_init = true,
	}, {
		.usecase_id = LLCC_CMPTDMA,
		.slice_id = 15,
		.max_cap = 1024,
		.priority = 1,
		.fixed_size = true,
		.bonus_ways = 0xff,
		.cache_mode = 0,
		.retain_on_pc = true,
	}, {
		.usecase_id = LLCC_DISP,
		.slice_id = 16,
		.max_cap = 4096,
		.priority = 2,
		.fixed_size = true,
		.bonus_ways = 0xff,
		.cache_mode = 0,
		.retain_on_pc = true,
	}, {
		.usecase_id = LLCC_VIDFW,
		.slice_id = 17,
		.max_cap = 3072,
		.priority = 1,
		.bonus_ways = 0xff,
		.cache_mode = 0,
		.retain_on_pc = true,
	}, {
		.usecase_id = LLCC_AUDHW,
		.slice_id = 22,
		.max_cap = 1024,
		.priority = 1,
		.fixed_size = true,
		.bonus_ways = 0xff,
		.cache_mode = 0,
	}, {
		.usecase_id = LLCC_CVP,
		.slice_id = 28,
		.max_cap = 256,
		.priority = 3,
		.fixed_size = true,
		.bonus_ways = 0xff,
		.cache_mode = 0,
		.retain_on_pc = true,
	}, {
		.usecase_id = LLCC_APTCM,
		.slice_id = 30,
		.max_cap = 1024,
		.priority = 3,
		.fixed_size = true,
		.res_ways = 0xf0,
		.cache_mode = 1,
		.retain_on_pc = true,
	}, {
		.usecase_id = LLCC_WRCACHE,
		.slice_id = 31,
		.max_cap = 512,
		.priority = 1,
		.fixed_size = true,
		.bonus_ways = 0xff,
		.cache_mode = 0,
		.activate_on_init = true,
	},
};

static const struct llcc_slice_config sar1130p_data[] = {
	{
		.usecase_id = LLCC_CPUSS,
		.slice_id = 1,
		.max_cap = 4096,
		.priority = 1,
		.bonus_ways = 0x1fff,
		.res_ways = 0x0,
		.cache_mode = 0,
		.retain_on_pc = true,
		.activate_on_init = true,
	}, {
		.usecase_id = LLCC_VIDSC0,
		.slice_id = 2,
		.max_cap = 512,
		.priority = 3,
		.fixed_size = true,
		.bonus_ways = 0x1fff,
		.res_ways = 0x0,
		.cache_mode = 0,
		.retain_on_pc = true,
	}, {
		.usecase_id = LLCC_AUDIO,
		.slice_id = 6,
		.max_cap = 1024,
		.priority = 3,
		.fixed_size = true,
		.bonus_ways = 0x1fff,
		.res_ways = 0x0,
		.cache_mode = 0,
		.retain_on_pc = true,
	}, {
		.usecase_id = LLCC_CMPT,
		.slice_id = 10,
		.max_cap = 1024,
		.priority = 1,
		.fixed_size = true,
		.bonus_ways = 0x1fff,
		.res_ways = 0x0,
		.cache_mode = 0,
		.retain_on_pc = true,
	}, {
		.usecase_id = LLCC_GPUHTW,
		.slice_id = 11,
		.max_cap = 0,
		.priority = 1,
		.fixed_size = true,
		.bonus_ways = 0x1fff,
		.res_ways = 0x0,
		.cache_mode = 0,
		.retain_on_pc = true,
	}, {
		.usecase_id = LLCC_GPU,
		.slice_id = 12,
		.max_cap = 3072,
		.priority = 3,
		.fixed_size = true,
		.bonus_ways = 0x1fff,
		.res_ways = 0x0,
		.cache_mode = 0,
		.retain_on_pc = true,
		.write_scid_en = true,
	}, {
		.usecase_id = LLCC_MMUHWT,
		.slice_id = 13,
		.max_cap = 512,
		.priority = 1,
		.fixed_size = true,
		.bonus_ways = 0x1fff,
		.res_ways = 0x0,
		.cache_mode = 0,
	}, {
		.usecase_id = LLCC_DISP,
		.slice_id = 16,
		.max_cap = 12800,
		.priority = 1,
		.fixed_size = true,
		.bonus_ways = 0x1fff,
		.res_ways = 0x0,
		.cache_mode = 0,
		.retain_on_pc = true,
	}, {
		.usecase_id = LLCC_CVP,
		.slice_id = 28,
		.max_cap = 256,
		.priority = 3,
		.fixed_size = true,
		.bonus_ways = 0x1fff,
		.res_ways = 0x0,
		.cache_mode = 0,
		.retain_on_pc = true,
	}, {
		.usecase_id = LLCC_APTCM,
		.slice_id = 26,
		.max_cap = 2048,
		.priority = 3,
		.fixed_size = true,
		.bonus_ways = 0x0,
		.res_ways = 0x3,
		.cache_mode = true,
		.dis_cap_alloc = true,
		.retain_on_pc = true,
	}, {
		.usecase_id = LLCC_WRCACHE,
		.slice_id = 31,
		.max_cap = 256,
		.priority = 1,
		.fixed_size = true,
		.bonus_ways = 0x1fff,
		.res_ways = 0x0,
		.cache_mode = 0,
		.activate_on_init = true,
	}, {
		.usecase_id = LLCC_AENPU,
		.slice_id = 30,
		.max_cap = 3072,
		.priority = 3,
		.fixed_size = true,
		.bonus_ways = 0x1fff,
		.res_ways = 0x0,
		.cache_mode = 0,
		.retain_on_pc = true,
	}, {
		.usecase_id = LLCC_DISP_LEFT,
		.slice_id = 17,
		.max_cap = 0,
		.priority = 1,
		.fixed_size = true,
		.bonus_ways = 0x0,
		.res_ways = 0x0,
		.cache_mode = 0,
		.retain_on_pc = true,
	}, {
		.usecase_id = LLCC_DISP_RIGHT,
		.slice_id = 18,
		.max_cap = 0,
		.priority = 1,
		.fixed_size = true,
		.bonus_ways = 0x0,
		.res_ways = 0x0,
		.cache_mode = 0,
		.retain_on_pc = true,
	}, {
		.usecase_id = LLCC_EVCS_LEFT,
		.slice_id = 22,
		.max_cap = 0,
		.priority = 1,
		.fixed_size = true,
		.bonus_ways = 0x0,
		.res_ways = 0x0,
		.cache_mode = 0,
		.retain_on_pc = true,
	}, {
		.usecase_id = LLCC_EVCS_RIGHT,
		.slice_id = 23,
		.max_cap = 0,
		.priority = 1,
		.fixed_size = true,
		.bonus_ways = 0x0,
		.res_ways = 0x0,
		.cache_mode = 0,
		.retain_on_pc = true,
	},
};

static const struct llcc_slice_config sar2130p_data[] = {
	{
		.usecase_id = LLCC_CPUSS,
		.slice_id = 1,
		.max_cap = 6144,
		.priority = 1,
		.fixed_size = 0,
		.bonus_ways = 0x3fffffff,
		.res_ways = 0x0,
		.cache_mode = 0,
		.retain_on_pc = true,
		.activate_on_init = true,
	}, {
		.usecase_id = LLCC_VIDSC0,
		.slice_id = 2,
		.max_cap = 128,
		.priority = 2,
		.fixed_size = true,
		.bonus_ways = 0x3fffffff,
		.res_ways = 0x0,
		.cache_mode = 0,
		.retain_on_pc = true,
	}, {
		.usecase_id = LLCC_AUDIO,
		.slice_id = 6,
		.max_cap = 1024,
		.priority = 3,
		.fixed_size = true,
		.bonus_ways = 0x3fffffff,
		.res_ways = 0x0,
		.cache_mode = 0,
		.retain_on_pc = true,
	}, {
		.usecase_id = LLCC_CMPT,
		.slice_id = 10,
		.max_cap = 1024,
		.priority = 1,
		.fixed_size = true,
		.bonus_ways = 0x3fffffff,
		.res_ways = 0x0,
		.cache_mode = 0,
		.retain_on_pc = true,
	}, {
		.usecase_id = LLCC_GPUHTW,
		.slice_id = 11,
		.max_cap = 0,
		.priority = 1,
		.fixed_size = true,
		.bonus_ways = 0x3fffffff,
		.res_ways = 0x0,
		.cache_mode = 0,
		.retain_on_pc = true,
	}, {
		.usecase_id = LLCC_GPU,
		.slice_id = 12,
		.max_cap = 1536,
		.priority = 2,
		.fixed_size = true,
		.bonus_ways = 0x3fffffff,
		.res_ways = 0x0,
		.cache_mode = 0,
		.retain_on_pc = true,
		.write_scid_en = true,
	}, {
		.usecase_id = LLCC_MMUHWT,
		.slice_id = 13,
		.max_cap = 1024,
		.priority = 1,
		.fixed_size = true,
		.bonus_ways = 0x3fffffff,
		.res_ways = 0x0,
		.cache_mode = 0,
		.activate_on_init = true,
	}, {
		.usecase_id = LLCC_DISP,
		.slice_id = 16,
		.max_cap = 0,
		.priority = 1,
		.fixed_size = true,
		.bonus_ways = 0x3fffffff,
		.res_ways = 0x0,
		.cache_mode = 0,
		.retain_on_pc = true,
	}, {
		.usecase_id = LLCC_APTCM,
		.slice_id = 26,
		.max_cap = 2048,
		.priority = 3,
		.fixed_size = true,
		.bonus_ways = 0x0,
		.res_ways = 0x3,
		.cache_mode = true,
		.dis_cap_alloc = true,
		.retain_on_pc = true,
	}, {
		.usecase_id = LLCC_WRCACHE,
		.slice_id = 31,
		.max_cap = 256,
		.priority = 1,
		.fixed_size = true,
		.bonus_ways = 0x3fffffff,
		.res_ways = 0x0,
		.cache_mode = 0,
		.activate_on_init = true,
	}, {
		.usecase_id = LLCC_VIEYE,
		.slice_id = 7,
		.max_cap = 7168,
		.priority = 4,
		.fixed_size = true,
		.bonus_ways = 0x3fffffff,
		.res_ways = 0x0,
		.cache_mode = 0,
		.retain_on_pc = true,
	}, {
		.usecase_id = LLCC_VIDPTH,
		.slice_id = 8,
		.max_cap = 7168,
		.priority = 4,
		.fixed_size = true,
		.bonus_ways = 0x3fffffff,
		.res_ways = 0x0,
		.cache_mode = 0,
		.retain_on_pc = true,
	}, {
		.usecase_id = LLCC_GPUMV,
		.slice_id = 9,
		.max_cap = 2048,
		.priority = 2,
		.fixed_size = true,
		.bonus_ways = 0x3fffffff,
		.res_ways = 0x0,
		.cache_mode = 0,
		.retain_on_pc = true,
	}, {
		.usecase_id = LLCC_EVA_LEFT,
		.slice_id = 20,
		.max_cap = 7168,
		.priority = 5,
		.fixed_size = true,
		.bonus_ways = 0x3ffffffc,
		.res_ways = 0x0,
		.cache_mode = 0,
		.retain_on_pc = true,
	}, {
		.usecase_id = LLCC_EVA_RIGHT,
		.slice_id = 21,
		.max_cap = 7168,
		.priority = 5,
		.fixed_size = true,
		.bonus_ways = 0x3ffffffc,
		.res_ways = 0x0,
		.cache_mode = 0,
		.retain_on_pc = true,
	}, {
		.usecase_id = LLCC_EVAGAIN,
		.slice_id = 25,
		.max_cap = 1024,
		.priority = 2,
		.fixed_size = true,
		.bonus_ways = 0x3fffffff,
		.res_ways = 0x0,
		.cache_mode = 0,
		.retain_on_pc = true,
	}, {
		.usecase_id = LLCC_AENPU,
		.slice_id = 30,
		.max_cap = 3072,
		.priority = 3,
		.fixed_size = true,
		.bonus_ways = 0x3fffffff,
		.res_ways = 0x0,
		.cache_mode = 0,
		.retain_on_pc = true,
	}, {
		.usecase_id = LLCC_VIPTH,
		.slice_id = 29,
		.max_cap = 1024,
		.priority = 4,
		.fixed_size = true,
		.bonus_ways = 0x3fffffff,
		.res_ways = 0x0,
		.cache_mode = 0,
		.retain_on_pc = true,
	}, {
		.usecase_id = LLCC_DISP_LEFT,
		.slice_id = 17,
		.max_cap = 0,
		.priority = 1,
		.fixed_size = true,
		.bonus_ways = 0x0,
		.res_ways = 0x0,
		.cache_mode = 0,
		.retain_on_pc = true,
	}, {
		.usecase_id = LLCC_DISP_RIGHT,
		.slice_id = 18,
		.max_cap = 0,
		.priority = 1,
		.fixed_size = true,
		.bonus_ways = 0x0,
		.res_ways = 0x0,
		.cache_mode = 0,
		.retain_on_pc = true,
	}, {
		.usecase_id = LLCC_EVCS_LEFT,
		.slice_id = 22,
		.max_cap = 0,
		.priority = 1,
		.fixed_size = true,
		.bonus_ways = 0x0,
		.res_ways = 0x0,
		.cache_mode = 0,
		.retain_on_pc = true,
	}, {
		.usecase_id = LLCC_EVCS_RIGHT,
		.slice_id = 23,
		.max_cap = 0,
		.priority = 1,
		.fixed_size = true,
		.bonus_ways = 0x0,
		.res_ways = 0x0,
		.cache_mode = 0,
		.retain_on_pc = true,
	}, {
		.usecase_id = LLCC_SPAD,
		.slice_id = 24,
		.max_cap = 7168,
		.priority = 1,
		.fixed_size = true,
		.bonus_ways = 0x0,
		.res_ways = 0x0,
		.cache_mode = 0,
		.retain_on_pc = true,
	},
};

static const struct llcc_slice_config sc7180_data[] =  {
	{
		.usecase_id = LLCC_CPUSS,
		.slice_id = 1,
		.max_cap = 256,
		.priority = 1,
		.bonus_ways = 0xf,
		.cache_mode = 0,
		.retain_on_pc = true,
		.activate_on_init = true,
	}, {
		.usecase_id = LLCC_MDM,
		.slice_id = 8,
		.max_cap = 128,
		.priority = 1,
		.bonus_ways = 0xf,
		.cache_mode = 0,
		.retain_on_pc = true,
	}, {
		.usecase_id = LLCC_GPUHTW,
		.slice_id = 11,
		.max_cap = 128,
		.priority = 1,
		.bonus_ways = 0xf,
		.cache_mode = 0,
		.retain_on_pc = true,
	}, {
		.usecase_id = LLCC_GPU,
		.slice_id = 12,
		.max_cap = 128,
		.priority = 1,
		.bonus_ways = 0xf,
		.cache_mode = 0,
		.retain_on_pc = true,
	},
};

static const struct llcc_slice_config sc7280_data[] =  {
	{
		.usecase_id = LLCC_CPUSS,
		.slice_id = 1,
		.max_cap = 768,
		.priority = 1,
		.bonus_ways = 0x3f,
		.cache_mode = 0,
		.retain_on_pc = true,
		.activate_on_init = true,
	}, {
		.usecase_id = LLCC_MDMHPGRW,
		.slice_id = 7,
		.max_cap = 512,
		.priority = 2,
		.fixed_size = true,
		.bonus_ways = 0x3f,
		.cache_mode = 0,
		.retain_on_pc = true,
	}, {
		.usecase_id = LLCC_CMPT,
		.slice_id = 10,
		.max_cap = 768,
		.priority = 1,
		.fixed_size = true,
		.bonus_ways = 0x3f,
		.cache_mode = 0,
		.retain_on_pc = true,
	}, {
		.usecase_id = LLCC_GPUHTW,
		.slice_id = 11,
		.max_cap = 256,
		.priority = 1,
		.fixed_size = true,
		.bonus_ways = 0x3f,
		.cache_mode = 0,
		.retain_on_pc = true,
	}, {
		.usecase_id = LLCC_GPU,
		.slice_id = 12,
		.max_cap = 512,
		.priority = 1,
		.bonus_ways = 0x3f,
		.cache_mode = 0,
		.retain_on_pc = true,
	}, {
		.usecase_id = LLCC_MMUHWT,
		.slice_id = 13,
		.max_cap = 256,
		.priority = 1,
		.fixed_size = true,
		.bonus_ways = 0x3f,
		.cache_mode = 0,
		.activate_on_init = true,
	}, {
		.usecase_id = LLCC_MDMPNG,
		.slice_id = 21,
		.max_cap = 768,
		.priority = 0,
		.fixed_size = true,
		.bonus_ways = 0x3f,
		.cache_mode = 0,
		.retain_on_pc = true,
	}, {
		.usecase_id = LLCC_WLHW,
		.slice_id = 24,
		.max_cap = 256,
		.priority = 1,
		.fixed_size = true,
		.bonus_ways = 0x3f,
		.cache_mode = 0,
		.retain_on_pc = true,
	}, {
		.usecase_id = LLCC_MODPE,
		.slice_id = 29,
		.max_cap = 64,
		.priority = 1,
		.fixed_size = true,
		.bonus_ways = 0x3f,
		.cache_mode = 0,
		.retain_on_pc = true,
	},
};

static const struct llcc_slice_config sc8180x_data[] = {
	{
		.usecase_id = LLCC_CPUSS,
		.slice_id = 1,
		.max_cap = 6144,
		.priority = 1,
		.fixed_size = true,
		.bonus_ways = 0xfff,
		.cache_mode = 0,
		.retain_on_pc = true,
		.activate_on_init = true,
	}, {
		.usecase_id = LLCC_VIDSC0,
		.slice_id = 2,
		.max_cap = 512,
		.priority = 2,
		.fixed_size = true,
		.bonus_ways = 0xfff,
		.cache_mode = 0,
		.retain_on_pc = true,
	}, {
		.usecase_id = LLCC_VIDSC1,
		.slice_id = 3,
		.max_cap = 512,
		.priority = 2,
		.fixed_size = true,
		.bonus_ways = 0xfff,
		.cache_mode = 0,
		.retain_on_pc = true,
	}, {
		.usecase_id = LLCC_AUDIO,
		.slice_id = 6,
		.max_cap = 1024,
		.priority = 1,
		.fixed_size = true,
		.bonus_ways = 0xfff,
		.cache_mode = 0,
		.retain_on_pc = true,
	}, {
		.usecase_id = LLCC_MDMHPGRW,
		.slice_id = 7,
		.max_cap = 3072,
		.priority = 1,
		.fixed_size = true,
		.bonus_ways = 0x3ff,
		.res_ways = 0xc00,
		.cache_mode = 0,
		.retain_on_pc = true,
	}, {
		.usecase_id = LLCC_MDM,
		.slice_id = 8,
		.max_cap = 3072,
		.priority = 1,
		.fixed_size = true,
		.bonus_ways = 0xfff,
		.cache_mode = 0,
		.retain_on_pc = true,
	}, {
		.usecase_id = LLCC_MODHW,
		.slice_id = 9,
		.max_cap = 1024,
		.priority = 1,
		.fixed_size = true,
		.bonus_ways = 0xfff,
		.cache_mode = 0,
		.retain_on_pc = true,
	}, {
		.usecase_id = LLCC_CMPT,
		.slice_id = 10,
		.max_cap = 6144,
		.priority = 1,
		.fixed_size = true,
		.bonus_ways = 0xfff,
		.cache_mode = 0,
		.retain_on_pc = true,
	}, {
		.usecase_id = LLCC_GPUHTW,
		.slice_id = 11,
		.max_cap = 1024,
		.priority = 1,
		.fixed_size = true,
		.bonus_ways = 0xfff,
		.cache_mode = 0,
		.retain_on_pc = true,
	}, {
		.usecase_id = LLCC_GPU,
		.slice_id = 12,
		.max_cap = 5120,
		.priority = 1,
		.fixed_size = true,
		.bonus_ways = 0xfff,
		.cache_mode = 0,
		.retain_on_pc = true,
	}, {
		.usecase_id = LLCC_MMUHWT,
		.slice_id = 13,
		.max_cap = 1024,
		.priority = 1,
		.fixed_size = true,
		.bonus_ways = 0xfff,
		.cache_mode = 0,
		.activate_on_init = true,
	}, {
		.usecase_id = LLCC_CMPTDMA,
		.slice_id = 15,
		.max_cap = 6144,
		.priority = 1,
		.fixed_size = true,
		.bonus_ways = 0xfff,
		.cache_mode = 0,
		.retain_on_pc = true,
	}, {
		.usecase_id = LLCC_DISP,
		.slice_id = 16,
		.max_cap = 6144,
		.priority = 1,
		.fixed_size = true,
		.bonus_ways = 0xfff,
		.cache_mode = 0,
		.retain_on_pc = true,
	}, {
		.usecase_id = LLCC_VIDFW,
		.slice_id = 17,
		.max_cap = 1024,
		.priority = 1,
		.fixed_size = true,
		.bonus_ways = 0xfff,
		.cache_mode = 0,
		.retain_on_pc = true,
	}, {
		.usecase_id = LLCC_MDMHPFX,
		.slice_id = 20,
		.max_cap = 1024,
		.priority = 2,
		.fixed_size = true,
		.bonus_ways = 0xfff,
		.cache_mode = 0,
		.retain_on_pc = true,
	}, {
		.usecase_id = LLCC_MDMPNG,
		.slice_id = 21,
		.max_cap = 1024,
		.priority = 0,
		.fixed_size = true,
		.bonus_ways = 0xc,
		.cache_mode = 0,
		.retain_on_pc = true,
	}, {
		.usecase_id = LLCC_AUDHW,
		.slice_id = 22,
		.max_cap = 1024,
		.priority = 1,
		.fixed_size = true,
		.bonus_ways = 0xfff,
		.cache_mode = 0,
		.retain_on_pc = true,
	}, {
		.usecase_id = LLCC_NPU,
		.slice_id = 23,
		.max_cap = 6144,
		.priority = 1,
		.fixed_size = true,
		.bonus_ways = 0xfff,
		.cache_mode = 0,
		.retain_on_pc = true,
	}, {
		.usecase_id = LLCC_WLHW,
		.slice_id = 24,
		.max_cap = 6144,
		.priority = 1,
		.fixed_size = true,
		.bonus_ways = 0xfff,
		.cache_mode = 0,
		.retain_on_pc = true,
	}, {
		.usecase_id = LLCC_MODPE,
		.slice_id = 29,
		.max_cap = 512,
		.priority = 1,
		.fixed_size = true,
		.bonus_ways = 0xc,
		.cache_mode = 0,
		.retain_on_pc = true,
	}, {
		.usecase_id = LLCC_APTCM,
		.slice_id = 30,
		.max_cap = 512,
		.priority = 3,
		.fixed_size = true,
		.res_ways = 0x1,
		.cache_mode = 1,
		.retain_on_pc = true,
	}, {
		.usecase_id = LLCC_WRCACHE,
		.slice_id = 31,
		.max_cap = 128,
		.priority = 1,
		.fixed_size = true,
		.bonus_ways = 0xfff,
		.cache_mode = 0,
	},
};

static const struct llcc_slice_config sc8280xp_data[] = {
	{
		.usecase_id = LLCC_CPUSS,
		.slice_id = 1,
		.max_cap = 6144,
		.priority = 1,
		.fixed_size = true,
		.bonus_ways = 0xfff,
		.cache_mode = 0,
		.retain_on_pc = true,
		.activate_on_init = true,
	}, {
		.usecase_id = LLCC_VIDSC0,
		.slice_id = 2,
		.max_cap = 512,
		.priority = 3,
		.fixed_size = true,
		.bonus_ways = 0xfff,
		.cache_mode = 0,
		.retain_on_pc = true,
	}, {
		.usecase_id = LLCC_AUDIO,
		.slice_id = 6,
		.max_cap = 1024,
		.priority = 1,
		.fixed_size = true,
		.bonus_ways = 0xfff,
		.cache_mode = 0,
	}, {
		.usecase_id = LLCC_CMPT,
		.slice_id = 10,
		.max_cap = 6144,
		.priority = 1,
		.fixed_size = true,
		.bonus_ways = 0xfff,
		.cache_mode = 0,
	}, {
		.usecase_id = LLCC_GPUHTW,
		.slice_id = 11,
		.max_cap = 1024,
		.priority = 1,
		.fixed_size = true,
		.bonus_ways = 0xfff,
		.cache_mode = 0,
		.retain_on_pc = true,
	}, {
		.usecase_id = LLCC_GPU,
		.slice_id = 12,
		.max_cap = 4096,
		.priority = 1,
		.fixed_size = true,
		.bonus_ways = 0xfff,
		.cache_mode = 0,
		.retain_on_pc = true,
		.write_scid_en = true,
	}, {
		.usecase_id = LLCC_MMUHWT,
		.slice_id = 13,
		.max_cap = 1024,
		.priority = 1,
		.fixed_size = true,
		.bonus_ways = 0xfff,
		.cache_mode = 0,
		.activate_on_init = true,
	}, {
		.usecase_id = LLCC_DISP,
		.slice_id = 16,
		.max_cap = 6144,
		.priority = 1,
		.fixed_size = true,
		.bonus_ways = 0xfff,
		.cache_mode = 0,
		.retain_on_pc = true,
	}, {
		.usecase_id = LLCC_AUDHW,
		.slice_id = 22,
		.max_cap = 2048,
		.priority = 1,
		.fixed_size = true,
		.bonus_ways = 0xfff,
		.cache_mode = 0,
		.retain_on_pc = true,
	}, {
		.usecase_id = LLCC_ECC,
		.slice_id = 26,
		.max_cap = 1024,
		.priority = 1,
		.fixed_size = true,
		.bonus_ways = 0xfff,
		.cache_mode = 0,
		.retain_on_pc = true,
	}, {
		.usecase_id = LLCC_CVP,
		.slice_id = 28,
		.max_cap = 512,
		.priority = 3,
		.fixed_size = true,
		.bonus_ways = 0xfff,
		.cache_mode = 0,
		.retain_on_pc = true,
	}, {
		.usecase_id = LLCC_APTCM,
		.slice_id = 30,
		.max_cap = 1024,
		.priority = 3,
		.fixed_size = true,
		.res_ways = 0x1,
		.cache_mode = 1,
		.retain_on_pc = true,
	}, {
		.usecase_id = LLCC_WRCACHE,
		.slice_id = 31,
		.max_cap = 1024,
		.priority = 1,
		.fixed_size = true,
		.bonus_ways = 0xfff,
		.cache_mode = 0,
		.activate_on_init = true,
	}, {
		.usecase_id = LLCC_CVPFW,
		.slice_id = 17,
		.max_cap = 512,
		.priority = 1,
		.bonus_ways = 0xfff,
		.cache_mode = 0,
		.retain_on_pc = true,
	}, {
		.usecase_id = LLCC_CPUSS1,
		.slice_id = 3,
		.max_cap = 2048,
		.priority = 1,
		.fixed_size = true,
		.bonus_ways = 0xfff,
		.cache_mode = 0,
		.retain_on_pc = true,
	}, {
		.usecase_id = LLCC_CPUHWT,
		.slice_id = 5,
		.max_cap = 512,
		.priority = 1,
		.fixed_size = true,
		.bonus_ways = 0xfff,
		.cache_mode = 0,
		.activate_on_init = true,
	},
};

static const struct llcc_slice_config sdm845_data[] =  {{
		.usecase_id = LLCC_CPUSS,
		.slice_id = 1,
		.max_cap = 2816,
		.priority = 1,
		.bonus_ways = 0xffc,
		.res_ways = 0x2,
		.cache_mode = 0,
		.dis_cap_alloc = true,
		.retain_on_pc = true,
		.activate_on_init = true,
	}, {
		.usecase_id = LLCC_VIDSC0,
		.slice_id = 2,
		.max_cap = 512,
		.priority = 2,
		.fixed_size = true,
		.res_ways = 0xf0,
		.cache_mode = 0,
		.dis_cap_alloc = true,
		.retain_on_pc = true,
	}, {
		.usecase_id = LLCC_VIDSC1,
		.slice_id = 3,
		.max_cap = 512,
		.priority = 2,
		.fixed_size = true,
		.res_ways = 0xf0,
		.cache_mode = 0,
		.dis_cap_alloc = true,
		.retain_on_pc = true,
	}, {
		.usecase_id = LLCC_ROTATOR,
		.slice_id = 4,
		.max_cap = 563,
		.priority = 2,
		.fixed_size = true,
		.res_ways = 0xe,
		.cache_mode = 2,
		.dis_cap_alloc = true,
		.retain_on_pc = true,
	}, {
		.usecase_id = LLCC_VOICE,
		.slice_id = 5,
		.max_cap = 2816,
		.priority = 1,
		.bonus_ways = 0xffc,
		.res_ways = 0x2,
		.cache_mode = 0,
		.dis_cap_alloc = true,
		.retain_on_pc = true,
	}, {
		.usecase_id = LLCC_AUDIO,
		.slice_id = 6,
		.max_cap = 2816,
		.priority = 1,
		.bonus_ways = 0xffc,
		.res_ways = 0x2,
		.cache_mode = 0,
		.dis_cap_alloc = true,
		.retain_on_pc = true,
	}, {
		.usecase_id = LLCC_MDMHPGRW,
		.slice_id = 7,
		.max_cap = 1024,
		.priority = 2,
		.bonus_ways = 0xfc,
		.res_ways = 0xf00,
		.cache_mode = 0,
		.dis_cap_alloc = true,
		.retain_on_pc = true,
	}, {
		.usecase_id = LLCC_MDM,
		.slice_id = 8,
		.max_cap = 2816,
		.priority = 1,
		.bonus_ways = 0xffc,
		.res_ways = 0x2,
		.cache_mode = 0,
		.dis_cap_alloc = true,
		.retain_on_pc = true,
	}, {
		.usecase_id = LLCC_CMPT,
		.slice_id = 10,
		.max_cap = 2816,
		.priority = 1,
		.bonus_ways = 0xffc,
		.res_ways = 0x2,
		.cache_mode = 0,
		.dis_cap_alloc = true,
		.retain_on_pc = true,
	}, {
		.usecase_id = LLCC_GPUHTW,
		.slice_id = 11,
		.max_cap = 512,
		.priority = 1,
		.fixed_size = true,
		.bonus_ways = 0xc,
		.cache_mode = 0,
		.dis_cap_alloc = true,
		.retain_on_pc = true,
	}, {
		.usecase_id = LLCC_GPU,
		.slice_id = 12,
		.max_cap = 2304,
		.priority = 1,
		.bonus_ways = 0xff0,
		.res_ways = 0x2,
		.cache_mode = 0,
		.dis_cap_alloc = true,
		.retain_on_pc = true,
	}, {
		.usecase_id = LLCC_MMUHWT,
		.slice_id = 13,
		.max_cap = 256,
		.priority = 2,
		.res_ways = 0x1,
		.cache_mode = 0,
		.dis_cap_alloc = true,
		.activate_on_init = true,
	}, {
		.usecase_id = LLCC_CMPTDMA,
		.slice_id = 15,
		.max_cap = 2816,
		.priority = 1,
		.bonus_ways = 0xffc,
		.res_ways = 0x2,
		.cache_mode = 0,
		.dis_cap_alloc = true,
		.retain_on_pc = true,
	}, {
		.usecase_id = LLCC_DISP,
		.slice_id = 16,
		.max_cap = 2816,
		.priority = 1,
		.bonus_ways = 0xffc,
		.res_ways = 0x2,
		.cache_mode = 0,
		.dis_cap_alloc = true,
		.retain_on_pc = true,
	}, {
		.usecase_id = LLCC_VIDFW,
		.slice_id = 17,
		.max_cap = 2816,
		.priority = 1,
		.bonus_ways = 0xffc,
		.res_ways = 0x2,
		.cache_mode = 0,
		.dis_cap_alloc = true,
		.retain_on_pc = true,
	}, {
		.usecase_id = LLCC_MDMHPFX,
		.slice_id = 20,
		.max_cap = 1024,
		.priority = 2,
		.fixed_size = true,
		.res_ways = 0xf00,
		.cache_mode = 0,
		.dis_cap_alloc = true,
		.retain_on_pc = true,
	}, {
		.usecase_id = LLCC_MDMPNG,
		.slice_id = 21,
		.max_cap = 1024,
		.priority = 0,
		.fixed_size = true,
		.bonus_ways = 0x1e,
		.cache_mode = 0,
		.dis_cap_alloc = true,
		.retain_on_pc = true,
	}, {
		.usecase_id = LLCC_AUDHW,
		.slice_id = 22,
		.max_cap = 1024,
		.priority = 1,
		.fixed_size = true,
		.bonus_ways = 0xffc,
		.res_ways = 0x2,
		.cache_mode = 0,
		.dis_cap_alloc = true,
		.retain_on_pc = true,
	},
};

static const struct llcc_slice_config sm6350_data[] =  {
	{
		.usecase_id = LLCC_CPUSS,
		.slice_id = 1,
		.max_cap = 768,
		.priority = 1,
		.bonus_ways = 0xfff,
		.cache_mode = 0,
		.activate_on_init = true,
		.write_scid_en = true,
	}, {
		.usecase_id = LLCC_MDM,
		.slice_id = 8,
		.max_cap = 512,
		.priority = 2,
		.bonus_ways = 0xfff,
		.cache_mode = 0,
		.activate_on_init = true,
	}, {
		.usecase_id = LLCC_GPUHTW,
		.slice_id = 11,
		.max_cap = 256,
		.priority = 1,
		.bonus_ways = 0xfff,
		.cache_mode = 0,
		.activate_on_init = true,
	}, {
		.usecase_id = LLCC_GPU,
		.slice_id = 12,
		.max_cap = 512,
		.priority = 1,
		.bonus_ways = 0xfff,
		.cache_mode = 0,
		.activate_on_init = true,
	}, {
		.usecase_id = LLCC_MDMPNG,
		.slice_id = 21,
		.max_cap = 768,
		.priority = 0,
		.fixed_size = true,
		.bonus_ways = 0xfff,
		.cache_mode = 0,
		.activate_on_init = true,
	}, {
		.usecase_id = LLCC_NPU,
		.slice_id = 23,
		.max_cap = 768,
		.priority = 1,
		.bonus_ways = 0xfff,
		.cache_mode = 0,
		.activate_on_init = true,
	}, {
		.usecase_id = LLCC_MODPE,
		.slice_id = 29,
		.max_cap = 64,
		.priority = 1,
		.fixed_size = true,
		.bonus_ways = 0xfff,
		.cache_mode = 0,
		.activate_on_init = true,
	},
};

static const struct llcc_slice_config sm7150_data[] =  {
	{
		.usecase_id = LLCC_CPUSS,
		.slice_id = 1,
		.max_cap = 512,
		.priority = 1,
		.bonus_ways = 0xf,
		.cache_mode = 0,
		.retain_on_pc = true,
		.activate_on_init = true,
	}, {
		.usecase_id = LLCC_MDM,
		.slice_id = 8,
		.max_cap = 128,
		.priority = 2,
		.bonus_ways = 0xf,
		.cache_mode = 0,
		.retain_on_pc = true,
	}, {
		.usecase_id = LLCC_GPUHTW,
		.slice_id = 11,
		.max_cap = 256,
		.priority = 1,
		.fixed_size = true,
		.bonus_ways = 0xf,
		.cache_mode = 0,
		.retain_on_pc = true,
	}, {
		.usecase_id = LLCC_GPU,
		.slice_id = 12,
		.max_cap = 256,
		.priority = 1,
		.fixed_size = true,
		.bonus_ways = 0xf,
		.cache_mode = 0,
		.retain_on_pc = true,
	}, {
		.usecase_id = LLCC_NPU,
		.slice_id = 23,
		.max_cap = 512,
		.priority = 1,
		.bonus_ways = 0xf,
		.cache_mode = 0,
		.retain_on_pc = true,
	},
};

static const struct llcc_slice_config sm8150_data[] =  {
	{
		.usecase_id = LLCC_CPUSS,
		.slice_id = 1,
		.max_cap = 3072,
		.priority = 1,
		.fixed_size = true,
		.bonus_ways = 0xfff,
		.cache_mode = 0,
		.retain_on_pc = true,
		.activate_on_init = true,
	}, {
		.usecase_id = LLCC_VIDSC0,
		.slice_id = 2,
		.max_cap = 512,
		.priority = 2,
		.fixed_size = true,
		.bonus_ways = 0xfff,
		.cache_mode = 0,
		.retain_on_pc = true,
	}, {
		.usecase_id = LLCC_VIDSC1,
		.slice_id = 3,
		.max_cap = 512,
		.priority = 2,
		.fixed_size = true,
		.bonus_ways = 0xfff,
		.cache_mode = 0,
		.retain_on_pc = true,
	}, {
		.usecase_id = LLCC_AUDIO,
		.slice_id = 6,
		.max_cap = 1024,
		.priority = 1,
		.fixed_size = true,
		.bonus_ways = 0xfff,
		.cache_mode = 0,
		.retain_on_pc = true,
	}, {
		.usecase_id = LLCC_MDMHPGRW,
		.slice_id = 7,
		.max_cap = 3072,
		.priority = 1,
		.bonus_ways = 0xff,
		.res_ways = 0xf00,
		.cache_mode = 0,
		.retain_on_pc = true,
	}, {
		.usecase_id = LLCC_MDM,
		.slice_id = 8,
		.max_cap = 3072,
		.priority = 1,
		.fixed_size = true,
		.bonus_ways = 0xfff,
		.cache_mode = 0,
		.retain_on_pc = true,
	}, {
		.usecase_id = LLCC_MODHW,
		.slice_id = 9,
		.max_cap = 1024,
		.priority = 1,
		.fixed_size = true,
		.bonus_ways = 0xfff,
		.cache_mode = 0,
		.retain_on_pc = true,
	}, {
		.usecase_id = LLCC_CMPT,
		.slice_id = 10,
		.max_cap = 3072,
		.priority = 1,
		.fixed_size = true,
		.bonus_ways = 0xfff,
		.cache_mode = 0,
		.retain_on_pc = true,
	}, {
		.usecase_id = LLCC_GPUHTW,
		.slice_id = 11,
		.max_cap = 512,
		.priority = 1,
		.fixed_size = true,
		.bonus_ways = 0xfff,
		.cache_mode = 0,
		.retain_on_pc = true,
	}, {
		.usecase_id = LLCC_GPU,
		.slice_id = 12,
		.max_cap = 2560,
		.priority = 1,
		.fixed_size = true,
		.bonus_ways = 0xfff,
		.cache_mode = 0,
		.retain_on_pc = true,
	}, {
		.usecase_id = LLCC_MMUHWT,
		.slice_id = 13,
		.max_cap = 1024,
		.priority = 1,
		.fixed_size = true,
		.bonus_ways = 0xfff,
		.cache_mode = 0,
		.activate_on_init = true,
	}, {
		.usecase_id = LLCC_CMPTDMA,
		.slice_id = 15,
		.max_cap = 3072,
		.priority = 1,
		.fixed_size = true,
		.bonus_ways = 0xfff,
		.cache_mode = 0,
		.retain_on_pc = true,
	}, {
		.usecase_id = LLCC_DISP,
		.slice_id = 16,
		.max_cap = 3072,
		.priority = 1,
		.fixed_size = true,
		.bonus_ways = 0xfff,
		.cache_mode = 0,
		.retain_on_pc = true,
	}, {
		.usecase_id = LLCC_MDMHPFX,
		.slice_id = 20,
		.max_cap = 1024,
		.priority = 2,
		.fixed_size = true,
		.bonus_ways = 0xfff,
		.cache_mode = 0,
		.retain_on_pc = true,
	}, {
		.usecase_id = LLCC_MDMHPFX,
		.slice_id = 21,
		.max_cap = 1024,
		.priority = 0,
		.fixed_size = true,
		.bonus_ways = 0xf,
		.cache_mode = 0,
		.retain_on_pc = true,
	}, {
		.usecase_id = LLCC_AUDHW,
		.slice_id = 22,
		.max_cap = 1024,
		.priority = 1,
		.fixed_size = true,
		.bonus_ways = 0xfff,
		.cache_mode = 0,
		.retain_on_pc = true,
	}, {
		.usecase_id = LLCC_NPU,
		.slice_id = 23,
		.max_cap = 3072,
		.priority = 1,
		.fixed_size = true,
		.bonus_ways = 0xfff,
		.cache_mode = 0,
		.retain_on_pc = true,
	}, {
		.usecase_id = LLCC_WLHW,
		.slice_id = 24,
		.max_cap = 3072,
		.priority = 1,
		.fixed_size = true,
		.bonus_ways = 0xfff,
		.cache_mode = 0,
		.retain_on_pc = true,
	}, {
		.usecase_id = LLCC_MODPE,
		.slice_id = 29,
		.max_cap = 256,
		.priority = 1,
		.fixed_size = true,
		.bonus_ways = 0xf,
		.cache_mode = 0,
		.retain_on_pc = true,
	}, {
		.usecase_id = LLCC_APTCM,
		.slice_id = 30,
		.max_cap = 256,
		.priority = 3,
		.fixed_size = true,
		.res_ways = 0x1,
		.cache_mode = 1,
		.retain_on_pc = true,
	}, {
		.usecase_id = LLCC_WRCACHE,
		.slice_id = 31,
		.max_cap = 128,
		.priority = 1,
		.fixed_size = true,
		.bonus_ways = 0xfff,
		.cache_mode = 0,
	},
};

static const struct llcc_slice_config sm8250_data[] =  {
	{
		.usecase_id = LLCC_CPUSS,
		.slice_id = 1,
		.max_cap = 3072,
		.priority = 1,
		.fixed_size = true,
		.bonus_ways = 0xfff,
		.cache_mode = 0,
		.retain_on_pc = true,
		.activate_on_init = true,
	}, {
		.usecase_id = LLCC_VIDSC0,
		.slice_id = 2,
		.max_cap = 512,
		.priority = 3,
		.fixed_size = true,
		.bonus_ways = 0xfff,
		.cache_mode = 0,
		.retain_on_pc = true,
	}, {
		.usecase_id = LLCC_AUDIO,
		.slice_id = 6,
		.max_cap = 1024,
		.priority = 1,
		.bonus_ways = 0xfff,
		.cache_mode = 0,
	}, {
		.usecase_id = LLCC_CMPT,
		.slice_id = 10,
		.max_cap = 1024,
		.priority = 1,
		.bonus_ways = 0xfff,
		.cache_mode = 0,
	}, {
		.usecase_id = LLCC_GPUHTW,
		.slice_id = 11,
		.max_cap = 1024,
		.priority = 1,
		.fixed_size = true,
		.bonus_ways = 0xfff,
		.cache_mode = 0,
		.retain_on_pc = true,
	}, {
		.usecase_id = LLCC_GPU,
		.slice_id = 12,
		.max_cap = 1024,
		.priority = 1,
		.bonus_ways = 0xfff,
		.cache_mode = 0,
		.retain_on_pc = true,
		.write_scid_en = true,
	}, {
		.usecase_id = LLCC_MMUHWT,
		.slice_id = 13,
		.max_cap = 1024,
		.priority = 1,
		.fixed_size = true,
		.bonus_ways = 0xfff,
		.cache_mode = 0,
		.activate_on_init = true,
	}, {
		.usecase_id = LLCC_CMPTDMA,
		.slice_id = 15,
		.max_cap = 1024,
		.priority = 1,
		.bonus_ways = 0xfff,
		.cache_mode = 0,
		.retain_on_pc = true,
	}, {
		.usecase_id = LLCC_DISP,
		.slice_id = 16,
		.max_cap = 3072,
		.priority = 1,
		.fixed_size = true,
		.bonus_ways = 0xfff,
		.cache_mode = 0,
		.retain_on_pc = true,
	}, {
		.usecase_id = LLCC_VIDFW,
		.slice_id = 17,
		.max_cap = 512,
		.priority = 1,
		.bonus_ways = 0xfff,
		.cache_mode = 0,
		.retain_on_pc = true,
	}, {
		.usecase_id = LLCC_AUDHW,
		.slice_id = 22,
		.max_cap = 1024,
		.priority = 1,
		.fixed_size = true,
		.bonus_ways = 0xfff,
		.cache_mode = 0,
		.retain_on_pc = true,
	}, {
		.usecase_id = LLCC_NPU,
		.slice_id = 23,
		.max_cap = 3072,
		.priority = 1,
		.fixed_size = true,
		.bonus_ways = 0xfff,
		.cache_mode = 0,
		.retain_on_pc = true,
	}, {
		.usecase_id = LLCC_WLHW,
		.slice_id = 24,
		.max_cap = 1024,
		.priority = 1,
		.bonus_ways = 0xfff,
		.cache_mode = 0,
		.retain_on_pc = true,
	}, {
		.usecase_id = LLCC_CVP,
		.slice_id = 28,
		.max_cap = 256,
		.priority = 3,
		.fixed_size = true,
		.bonus_ways = 0xfff,
		.cache_mode = 0,
		.retain_on_pc = true,
	}, {
		.usecase_id = LLCC_APTCM,
		.slice_id = 30,
		.max_cap = 128,
		.priority = 3,
		.res_ways = 0x3,
		.cache_mode = 1,
		.retain_on_pc = true,
	}, {
		.usecase_id = LLCC_WRCACHE,
		.slice_id = 31,
		.max_cap = 256,
		.priority = 1,
		.fixed_size = true,
		.bonus_ways = 0xfff,
		.cache_mode = 0,
		.activate_on_init = true,
	},
};

static const struct llcc_slice_config sm8350_data[] =  {
	{
		.usecase_id = LLCC_CPUSS,
		.slice_id = 1,
		.max_cap = 3072,
		.priority = 1,
		.fixed_size = true,
		.bonus_ways = 0xfff,
		.cache_mode = 0,
		.activate_on_init = true,
		.write_scid_en = true,
	}, {
		.usecase_id = LLCC_VIDSC0,
		.slice_id = 2,
		.max_cap = 512,
		.priority = 3,
		.fixed_size = true,
		.bonus_ways = 0xfff,
		.cache_mode = 0,
		.activate_on_init = true,
	}, {
		.usecase_id = LLCC_AUDIO,
		.slice_id = 6,
		.max_cap = 1024,
		.priority = 1,
		.fixed_size = true,
		.bonus_ways = 0xfff,
		.cache_mode = 0,
	}, {
		.usecase_id = LLCC_MDMHPGRW,
		.slice_id = 7,
		.max_cap = 1024,
		.priority = 3,
		.bonus_ways = 0xfff,
		.cache_mode = 0,
		.activate_on_init = true,
	}, {
		.usecase_id = LLCC_MODHW,
		.slice_id = 9,
		.max_cap = 1024,
		.priority = 1,
		.fixed_size = true,
		.bonus_ways = 0xfff,
		.cache_mode = 0,
		.activate_on_init = true,
	}, {
		.usecase_id = LLCC_CMPT,
		.slice_id = 10,
		.max_cap = 3072,
		.priority = 1,
		.fixed_size = true,
		.bonus_ways = 0xfff,
		.cache_mode = 0,
		.activate_on_init = true,
	}, {
		.usecase_id = LLCC_GPUHTW,
		.slice_id = 11,
		.max_cap = 1024,
		.priority = 1,
		.fixed_size = true,
		.bonus_ways = 0xfff,
		.cache_mode = 0,
		.activate_on_init = true,
	}, {
		.usecase_id = LLCC_GPU,
		.slice_id = 12,
		.max_cap = 1024,
		.priority = 1,
		.bonus_ways = 0xfff,
		.cache_mode = 0,
		.retain_on_pc = true,
		.activate_on_init = true,
	}, {
		.usecase_id = LLCC_MMUHWT,
		.slice_id = 13,
		.max_cap = 1024,
		.priority = 1,
		.fixed_size = true,
		.bonus_ways = 0xfff,
		.cache_mode = 0,
		.write_scid_en = true,
	}, {
		.usecase_id = LLCC_DISP,
		.slice_id = 16,
		.max_cap = 3072,
		.priority = 2,
		.fixed_size = true,
		.bonus_ways = 0xfff,
		.cache_mode = 0,
		.activate_on_init = true,
	}, {
		.usecase_id = LLCC_MDMPNG,
		.slice_id = 21,
		.max_cap = 1024,
		.priority = 0,
		.fixed_size = true,
		.bonus_ways = 0xf,
		.cache_mode = 0,
		.activate_on_init = true,
	}, {
		.usecase_id = LLCC_AUDHW,
		.slice_id = 22,
		.max_cap = 1024,
		.priority = 1,
		.fixed_size = true,
		.bonus_ways = 0xfff,
		.cache_mode = 0,
		.activate_on_init = true,
	}, {
		.usecase_id = LLCC_CVP,
		.slice_id = 28,
		.max_cap = 512,
		.priority = 3,
		.fixed_size = true,
		.bonus_ways = 0xfff,
		.cache_mode = 0,
		.activate_on_init = true,
	}, {
		.usecase_id = LLCC_MODPE,
		.slice_id = 29,
		.max_cap = 256,
		.priority = 1,
		.fixed_size = true,
		.bonus_ways = 0xf,
		.cache_mode = 0,
		.activate_on_init = true,
	}, {
		.usecase_id = LLCC_APTCM,
		.slice_id = 30,
		.max_cap = 1024,
		.priority = 3,
		.fixed_size = true,
		.res_ways = 0x1,
		.cache_mode = 1,
		.activate_on_init = true,
	}, {
		.usecase_id = LLCC_WRCACHE,
		.slice_id = 31,
		.max_cap = 512,
		.priority = 1,
		.fixed_size = true,
		.bonus_ways = 0xfff,
		.cache_mode = 0,
		.write_scid_en = true,
	}, {
		.usecase_id = LLCC_CVPFW,
		.slice_id = 17,
		.max_cap = 512,
		.priority = 1,
		.bonus_ways = 0xfff,
		.cache_mode = 0,
		.activate_on_init = true,
	}, {
		.usecase_id = LLCC_CPUSS1,
		.slice_id = 3,
		.max_cap = 1024,
		.priority = 1,
		.fixed_size = true,
		.bonus_ways = 0xfff,
		.cache_mode = 0,
		.activate_on_init = true,
	}, {
		.usecase_id = LLCC_CPUHWT,
		.slice_id = 5,
		.max_cap = 512,
		.priority = 1,
		.fixed_size = true,
		.bonus_ways = 0xfff,
		.cache_mode = 0,
		.write_scid_en = true,
	},
};

static const struct llcc_slice_config sm8450_data[] =  {
	{
		.usecase_id = LLCC_CPUSS,
		.slice_id = 1,
		.max_cap = 3072,
		.priority = 1,
		.bonus_ways = 0xffff,
		.cache_mode = 0,
		.retain_on_pc = true,
		.activate_on_init = true,
	}, {
		.usecase_id = LLCC_VIDSC0,
		.slice_id = 2,
		.max_cap = 512,
		.priority = 3,
		.fixed_size = true,
		.bonus_ways = 0xffff,
		.cache_mode = 0,
		.retain_on_pc = true,
	}, {
		.usecase_id = LLCC_AUDIO,
		.slice_id = 6,
		.max_cap = 1024,
		.priority = 1,
		.fixed_size = true,
		.bonus_ways = 0xffff,
		.cache_mode = 0,
	}, {
		.usecase_id = LLCC_MDMHPGRW,
		.slice_id = 7,
		.max_cap = 1024,
		.priority = 3,
		.bonus_ways = 0xffff,
		.cache_mode = 0,
		.retain_on_pc = true,
	}, {
		.usecase_id = LLCC_MODHW,
		.slice_id = 9,
		.max_cap = 1024,
		.priority = 1,
		.fixed_size = true,
		.bonus_ways = 0xffff,
		.cache_mode = 0,
		.retain_on_pc = true,
	}, {
		.usecase_id = LLCC_CMPT,
		.slice_id = 10,
		.max_cap = 4096,
		.priority = 1,
		.fixed_size = true,
		.bonus_ways = 0xffff,
		.cache_mode = 0,
		.retain_on_pc = true,
	}, {
		.usecase_id = LLCC_GPUHTW,
		.slice_id = 11,
		.max_cap = 512,
		.priority = 1,
		.fixed_size = true,
		.bonus_ways = 0xffff,
		.cache_mode = 0,
		.retain_on_pc = true,
	}, {
		.usecase_id = LLCC_GPU,
		.slice_id = 12,
		.max_cap = 2048,
		.priority = 1,
		.fixed_size = true,
		.bonus_ways = 0xffff,
		.cache_mode = 0,
		.retain_on_pc = true,
		.write_scid_en = true,
	}, {
		.usecase_id = LLCC_MMUHWT,
		.slice_id = 13,
		.max_cap = 768,
		.priority = 1,
		.fixed_size = true,
		.bonus_ways = 0xffff,
		.cache_mode = 0,
		.activate_on_init = true,
	}, {
		.usecase_id = LLCC_DISP,
		.slice_id = 16,
		.max_cap = 4096,
		.priority = 2,
		.fixed_size = true,
		.bonus_ways = 0xffff,
		.cache_mode = 0,
		.retain_on_pc = true,
	}, {
		.usecase_id = LLCC_MDMPNG,
		.slice_id = 21,
		.max_cap = 1024,
		.priority = 1,
		.fixed_size = true,
		.bonus_ways = 0xf000,
		.cache_mode = 0,
		.retain_on_pc = true,
	}, {
		.usecase_id = LLCC_AUDHW,
		.slice_id = 22,
		.max_cap = 1024,
		.priority = 1,
		.fixed_size = true,
		.bonus_ways = 0xffff,
		.cache_mode = 0,
	}, {
		.usecase_id = LLCC_CVP,
		.slice_id = 28,
		.max_cap = 256,
		.priority = 3,
		.fixed_size = true,
		.bonus_ways = 0xffff,
		.cache_mode = 0,
		.retain_on_pc = true,
	}, {
		.usecase_id = LLCC_MODPE,
		.slice_id = 29,
		.max_cap = 64,
		.priority = 1,
		.fixed_size = true,
		.bonus_ways = 0xf000,
		.cache_mode = 0,
		.retain_on_pc = true,
	}, {
		.usecase_id = LLCC_APTCM,
		.slice_id = 30,
		.max_cap = 1024,
		.priority = 3,
		.fixed_size = true,
		.res_ways = 0xf0,
		.cache_mode = 1,
		.retain_on_pc = true,
	}, {
		.usecase_id = LLCC_WRCACHE,
		.slice_id = 31,
		.max_cap = 512,
		.priority = 1,
		.fixed_size = true,
		.bonus_ways = 0xffff,
		.cache_mode = 0,
		.activate_on_init = true,
	}, {
		.usecase_id = LLCC_CVPFW,
		.slice_id = 17,
		.max_cap = 512,
		.priority = 1,
		.fixed_size = true,
		.bonus_ways = 0xffff,
		.cache_mode = 0,
		.retain_on_pc = true,
	}, {
		.usecase_id = LLCC_CPUSS1,
		.slice_id = 3,
		.max_cap = 1024,
		.priority = 1,
		.fixed_size = true,
		.bonus_ways = 0xffff,
		.cache_mode = 0,
		.retain_on_pc = true,
	}, {
		.usecase_id = LLCC_CAMEXP0,
		.slice_id = 4,
		.max_cap = 256,
		.priority = 3,
		.fixed_size = true,
		.bonus_ways = 0xffff,
		.cache_mode = 0,
		.retain_on_pc = true,
	}, {
		.usecase_id = LLCC_CPUMTE,
		.slice_id = 23,
		.max_cap = 256,
		.priority = 1,
		.fixed_size = true,
		.bonus_ways = 0xfff,
		.cache_mode = 0,
		.activate_on_init = true,
	}, {
		.usecase_id = LLCC_CPUHWT,
		.slice_id = 5,
		.max_cap = 512,
		.priority = 1,
		.fixed_size = true,
		.bonus_ways = 0xffff,
		.cache_mode = 0,
		.retain_on_pc = true,
		.activate_on_init = true,
	}, {
		.usecase_id = LLCC_CAMEXP1,
		.slice_id = 27,
		.max_cap = 256,
		.priority = 3,
		.fixed_size = true,
		.bonus_ways = 0xffff,
		.cache_mode = 0,
		.retain_on_pc = true,
	}, {
		.usecase_id = LLCC_AENPU,
		.slice_id = 8,
		.max_cap = 2048,
		.priority = 1,
		.fixed_size = true,
		.bonus_ways = 0xffff,
		.cache_mode = 0,
	},
};

static const struct llcc_slice_config sm8550_data[] =  {
	{
		.usecase_id = LLCC_CPUSS,
		.slice_id = 1,
		.max_cap = 5120,
		.priority = 1,
		.bonus_ways = 0xffffff,
		.cache_mode = 0,
		.activate_on_init = true,
		.write_scid_en = true,
	}, {
		.usecase_id = LLCC_VIDSC0,
		.slice_id = 2,
		.max_cap = 512,
		.priority = 4,
		.fixed_size = true,
		.bonus_ways = 0xffffff,
		.cache_mode = 0,
	}, {
		.usecase_id = LLCC_AUDIO,
		.slice_id = 6,
		.max_cap = 1024,
		.priority = 1,
		.fixed_size = true,
		.bonus_ways = 0xffffff,
		.cache_mode = 0,
	}, {
		.usecase_id = LLCC_MDMHPGRW,
		.slice_id = 25,
		.max_cap = 1024,
		.priority = 4,
		.bonus_ways = 0xffffff,
		.cache_mode = 0,
	}, {
		.usecase_id = LLCC_MODHW,
		.slice_id = 26,
		.max_cap = 1024,
		.priority = 1,
		.fixed_size = true,
		.bonus_ways = 0xffffff,
		.cache_mode = 0,
	}, {
		.usecase_id = LLCC_CMPT,
		.slice_id = 10,
		.max_cap = 4096,
		.priority = 1,
		.fixed_size = true,
		.bonus_ways = 0xffffff,
		.cache_mode = 0,
	}, {
		.usecase_id = LLCC_GPUHTW,
		.slice_id = 11,
		.max_cap = 512,
		.priority = 1,
		.fixed_size = true,
		.bonus_ways = 0xffffff,
		.cache_mode = 0,
	}, {
		.usecase_id = LLCC_GPU,
		.slice_id = 9,
		.max_cap = 3096,
		.priority = 1,
		.bonus_ways = 0xffffff,
		.cache_mode = 0,
		.write_scid_en = true,
		.write_scid_cacheable_en = true,
	}, {
		.usecase_id = LLCC_MMUHWT,
		.slice_id = 18,
		.max_cap = 768,
		.priority = 1,
		.fixed_size = true,
		.bonus_ways = 0xffffff,
		.cache_mode = 0,
		.activate_on_init = true,
	}, {
		.usecase_id = LLCC_DISP,
		.slice_id = 16,
		.max_cap = 6144,
		.priority = 1,
		.fixed_size = true,
		.bonus_ways = 0xffffff,
		.cache_mode = 2,
	}, {
		.usecase_id = LLCC_MDMPNG,
		.slice_id = 27,
		.max_cap = 1024,
		.priority = 0,
		.fixed_size = true,
		.bonus_ways = 0xf00000,
		.cache_mode = 0,
	}, {
		.usecase_id = LLCC_AUDHW,
		.slice_id = 22,
		.max_cap = 1024,
		.priority = 1,
		.fixed_size = true,
		.bonus_ways = 0xffffff,
		.cache_mode = 0,
	}, {
		.usecase_id = LLCC_CVP,
		.slice_id = 8,
		.max_cap = 256,
		.priority = 4,
		.fixed_size = true,
		.bonus_ways = 0xffffff,
		.cache_mode = 0,
	}, {
		.usecase_id = LLCC_MODPE,
		.slice_id = 29,
		.max_cap = 64,
		.priority = 1,
		.fixed_size = true,
		.bonus_ways = 0xf00000,
		.cache_mode = 0,
		.alloc_oneway_en = true,
		.vict_prio = true,
	}, {
		.usecase_id = LLCC_WRCACHE,
		.slice_id = 31,
		.max_cap = 512,
		.priority = 1,
		.fixed_size = true,
		.bonus_ways = 0xffffff,
		.cache_mode = 0,
		.activate_on_init = true,
	}, {
		.usecase_id = LLCC_CAMEXP0,
		.slice_id = 4,
		.max_cap = 256,
		.priority = 4,
		.fixed_size = true,
		.bonus_ways = 0xf,
		.cache_mode = 0,
	}, {
		.usecase_id = LLCC_CPUHWT,
		.slice_id = 5,
		.max_cap = 512,
		.priority = 1,
		.fixed_size = true,
		.bonus_ways = 0xffffff,
		.cache_mode = 0,
		.activate_on_init = true,
	}, {
		.usecase_id = LLCC_CAMEXP1,
		.slice_id = 7,
		.max_cap = 3200,
		.priority = 3,
		.fixed_size = true,
		.bonus_ways = 0xfffff0,
		.cache_mode = 2,
	}, {
		.usecase_id = LLCC_CMPTHCP,
		.slice_id = 17,
		.max_cap = 256,
		.priority = 4,
		.fixed_size = true,
		.bonus_ways = 0xffffff,
		.cache_mode = 0,
	}, {
		.usecase_id = LLCC_LCPDARE,
		.slice_id = 30,
		.max_cap = 128,
		.priority = 4,
		.fixed_size = true,
		.bonus_ways = 0xffffff,
		.cache_mode = 0,
		.activate_on_init = true,
		.alloc_oneway_en = true,
		.vict_prio = true,
	}, {
		.usecase_id = LLCC_AENPU,
		.slice_id = 3,
		.max_cap = 3072,
		.priority = 1,
		.fixed_size = true,
		.bonus_ways = 0xfe01ff,
		.cache_mode = 2,
	}, {
		.usecase_id = LLCC_ISLAND1,
		.slice_id = 12,
		.max_cap = 1792,
		.priority = 7,
		.fixed_size = true,
		.bonus_ways = 0xfe00,
		.cache_mode = 0,
	}, {
		.usecase_id = LLCC_ISLAND4,
		.slice_id = 15,
		.max_cap = 256,
		.priority = 7,
		.fixed_size = true,
		.bonus_ways = 0x10000,
		.cache_mode = 0,
	}, {
		.usecase_id = LLCC_CAMEXP2,
		.slice_id = 19,
		.max_cap = 3200,
		.priority = 3,
		.fixed_size = true,
		.bonus_ways = 0xfffff0,
		.cache_mode = 2,
	}, {
		.usecase_id = LLCC_CAMEXP3,
		.slice_id = 20,
		.max_cap = 3200,
		.priority = 2,
		.fixed_size = true,
		.bonus_ways = 0xfffff0,
		.cache_mode = 2,
	}, {
		.usecase_id = LLCC_CAMEXP4,
		.slice_id = 21,
		.max_cap = 3200,
		.priority = 2,
		.fixed_size = true,
		.bonus_ways = 0xfffff0,
		.cache_mode = 2,
	}, {
		.usecase_id = LLCC_DISP_WB,
		.slice_id = 23,
		.max_cap = 1024,
		.priority = 4,
		.fixed_size = true,
		.bonus_ways = 0xffffff,
		.cache_mode = 0,
	}, {
		.usecase_id = LLCC_DISP_1,
		.slice_id = 24,
		.max_cap = 6144,
		.priority = 1,
		.fixed_size = true,
		.bonus_ways = 0xffffff,
		.cache_mode = 2,
	}, {
		.usecase_id = LLCC_VIDVSP,
		.slice_id = 28,
		.max_cap = 256,
		.priority = 4,
		.fixed_size = true,
		.bonus_ways = 0xffffff,
		.cache_mode = 0,
	},
};

static const struct llcc_slice_config sm8650_data[] = {
	{
		.usecase_id = LLCC_CPUSS,
		.slice_id = 1,
		.max_cap = 5120,
		.priority = 1,
		.bonus_ways = 0xffffff,
		.cache_mode = 0,
		.activate_on_init = true,
		.stale_en = true,
	}, {
		.usecase_id = LLCC_VIDSC0,
		.slice_id = 2,
		.max_cap = 512,
		.priority = 3,
		.fixed_size = true,
		.bonus_ways = 0xffffff,
		.cache_mode = 0,
	}, {
		.usecase_id = LLCC_AUDIO,
		.slice_id = 6,
		.max_cap = 512,
		.priority = 1,
		.fixed_size = true,
		.bonus_ways = 0xffffff,
		.cache_mode = 0,
	}, {
		.usecase_id = LLCC_MDMHPGRW,
		.slice_id = 25,
		.max_cap = 1024,
		.priority = 3,
		.bonus_ways = 0xffffff,
		.cache_mode = 0,
	}, {
		.usecase_id = LLCC_MODHW,
		.slice_id = 26,
		.max_cap = 1024,
		.priority = 1,
		.fixed_size = true,
		.bonus_ways = 0xffffff,
		.cache_mode = 0,
	}, {
		.usecase_id = LLCC_CMPT,
		.slice_id = 10,
		.max_cap = 4096,
		.priority = 1,
		.fixed_size = true,
		.bonus_ways = 0xffffff,
		.cache_mode = 0,
	}, {
		.usecase_id = LLCC_GPUHTW,
		.slice_id = 11,
		.max_cap = 512,
		.priority = 1,
		.fixed_size = true,
		.bonus_ways = 0xffffff,
		.cache_mode = 0,
	}, {
		.usecase_id = LLCC_GPU,
		.slice_id = 9,
		.max_cap = 3096,
		.priority = 1,
		.bonus_ways = 0xffffff,
		.cache_mode = 0,
		.write_scid_en = true,
		.write_scid_cacheable_en = true,
	}, {
		.usecase_id = LLCC_MMUHWT,
		.slice_id = 18,
		.max_cap = 768,
		.priority = 1,
		.fixed_size = true,
		.bonus_ways = 0xffffff,
		.cache_mode = 0,
		.activate_on_init = true,
	}, {
		.usecase_id = LLCC_DISP,
		.slice_id = 16,
		.max_cap = 6144,
		.priority = 1,
		.fixed_size = true,
		.bonus_ways = 0xffffff,
		.cache_mode = 2,
	}, {
		.usecase_id = LLCC_MDMHPFX,
		.slice_id = 24,
		.max_cap = 1024,
		.priority = 3,
		.fixed_size = true,
		.bonus_ways = 0xffffff,
		.cache_mode = 0,
	}, {
		.usecase_id = LLCC_MDMPNG,
		.slice_id = 27,
		.max_cap = 1024,
		.priority = 0,
		.fixed_size = true,
		.cache_mode = 0,
	}, {
		.usecase_id = LLCC_AUDHW,
		.slice_id = 22,
		.max_cap = 1024,
		.priority = 1,
		.fixed_size = true,
		.bonus_ways = 0xffffff,
		.cache_mode = 0,
	}, {
		.usecase_id = LLCC_CVP,
		.slice_id = 8,
		.max_cap = 256,
		.priority = 3,
		.fixed_size = true,
		.bonus_ways = 0xffffff,
		.cache_mode = 0,
	}, {
		.usecase_id = LLCC_MODPE,
		.slice_id = 29,
		.max_cap = 128,
		.priority = 1,
		.fixed_size = true,
		.bonus_ways = 0xf00000,
		.cache_mode = 0,
		.alloc_oneway_en = true,
	}, {
		.usecase_id = LLCC_WRCACHE,
		.slice_id = 31,
		.max_cap = 512,
		.priority = 1,
		.fixed_size = true,
		.bonus_ways = 0xffffff,
		.cache_mode = 0,
		.activate_on_init = true,
	}, {
		.usecase_id = LLCC_CAMEXP0,
		.slice_id = 4,
		.max_cap = 256,
		.priority = 3,
		.fixed_size = true,
		.bonus_ways = 0xf,
		.cache_mode = 0,
	}, {
		.usecase_id = LLCC_CAMEXP1,
		.slice_id = 7,
		.max_cap = 3200,
		.priority = 3,
		.fixed_size = true,
		.bonus_ways = 0xfffff0,
		.cache_mode = 2,
	}, {
		.usecase_id = LLCC_CMPTHCP,
		.slice_id = 17,
		.max_cap = 256,
		.priority = 3,
		.fixed_size = true,
		.bonus_ways = 0xffffff,
		.cache_mode = 0,
	}, {
		.usecase_id = LLCC_LCPDARE,
		.slice_id = 30,
		.max_cap = 128,
		.priority = 3,
		.fixed_size = true,
		.bonus_ways = 0xffffff,
		.cache_mode = 0,
		.activate_on_init = true,
		.alloc_oneway_en = true,
	}, {
		.usecase_id = LLCC_AENPU,
		.slice_id = 3,
		.max_cap = 3072,
		.priority = 1,
		.fixed_size = true,
		.bonus_ways = 0xffffff,
		.cache_mode = 2,
	}, {
		.usecase_id = LLCC_ISLAND1,
		.slice_id = 12,
		.max_cap = 5888,
		.priority = 7,
		.fixed_size = true,
		.res_ways = 0x7fffff,
		.cache_mode = 0,
	}, {
		.usecase_id = LLCC_DISP_WB,
		.slice_id = 23,
		.max_cap = 1024,
		.priority = 3,
		.fixed_size = true,
		.bonus_ways = 0xffffff,
		.cache_mode = 0,
	}, {
		.usecase_id = LLCC_VIDVSP,
		.slice_id = 28,
		.max_cap = 256,
		.priority = 3,
		.fixed_size = true,
		.bonus_ways = 0xffffff,
		.cache_mode = 0,
	},
};

static const struct llcc_slice_config sm8750_data[] = {
	{
		.usecase_id = LLCC_CPUSS,
		.slice_id = 1,
		.max_cap = 5120,
		.priority = 1,
		.bonus_ways = 0xffffffff,
		.activate_on_init = true,
		.write_scid_en = true,
	}, {
		.usecase_id = LLCC_MDMHPFX,
		.slice_id = 24,
		.max_cap = 1024,
		.priority = 5,
		.fixed_size = true,
		.bonus_ways = 0xffffffff,
	}, {
		.usecase_id = LLCC_VIDSC0,
		.slice_id = 2,
		.max_cap = 512,
		.priority = 4,
		.fixed_size = true,
		.bonus_ways = 0xffffffff,
	}, {
		.usecase_id = LLCC_AUDIO,
		.slice_id = 35,
		.max_cap = 512,
		.priority = 1,
		.fixed_size = true,
		.bonus_ways = 0xffffffff,
	}, {
		.usecase_id = LLCC_MDMHPGRW,
		.slice_id = 25,
		.max_cap = 1024,
		.priority = 5,
		.bonus_ways = 0xffffffff,
	}, {
		.usecase_id = LLCC_MODHW,
		.slice_id = 26,
		.max_cap = 1024,
		.priority = 1,
		.fixed_size = true,
		.bonus_ways = 0xffffffff,
	}, {
		.usecase_id = LLCC_CMPT,
		.slice_id = 34,
		.max_cap = 4096,
		.priority = 1,
		.fixed_size = true,
		.bonus_ways = 0xffffffff,
	}, {
		.usecase_id = LLCC_GPUHTW,
		.slice_id = 11,
		.max_cap = 512,
		.priority = 1,
		.fixed_size = true,
		.bonus_ways = 0xffffffff,
	}, {
		.usecase_id = LLCC_GPU,
		.slice_id = 9,
		.max_cap = 5632,
		.priority = 1,
		.fixed_size = true,
		.bonus_ways = 0xffffffff,
		.write_scid_en = true,
		.write_scid_cacheable_en = true
	}, {
		.usecase_id = LLCC_MMUHWT,
		.slice_id = 18,
		.max_cap = 768,
		.priority = 1,
		.fixed_size = true,
		.bonus_ways = 0xffffffff,
		.activate_on_init = true,
	}, {
		.usecase_id = LLCC_DISP,
		.slice_id = 16,
		.max_cap = 7168,
		.priority = 1,
		.fixed_size = true,
		.bonus_ways = 0xffffffff,
		.cache_mode = 2,
		.stale_en = true,
	}, {
		.usecase_id = LLCC_VIDFW,
		.slice_id = 17,
		.priority = 4,
		.fixed_size = true,
		.bonus_ways = 0xffffffff,
	}, {
		.usecase_id = LLCC_CAMFW,
		.slice_id = 20,
		.priority = 4,
		.fixed_size = true,
		.bonus_ways = 0xffffffff,
	}, {
		.usecase_id = LLCC_MDMPNG,
		.slice_id = 27,
		.max_cap = 256,
		.priority = 5,
		.fixed_size = true,
		.bonus_ways = 0xf0000000,
	}, {
		.usecase_id = LLCC_AUDHW,
		.slice_id = 22,
		.max_cap = 512,
		.priority = 1,
		.fixed_size = true,
		.bonus_ways = 0xffffffff,
	}, {
		.usecase_id = LLCC_CVP,
		.slice_id = 8,
		.max_cap = 800,
		.priority = 5,
		.fixed_size = true,
		.bonus_ways = 0xffffffff,
		.vict_prio = true,
	}, {
		.usecase_id = LLCC_MODPE,
		.slice_id = 29,
		.max_cap = 256,
		.priority = 1,
		.fixed_size = true,
		.bonus_ways = 0xf0000000,
		.alloc_oneway_en = true,
	}, {
		.usecase_id = LLCC_WRCACHE,
		.slice_id = 31,
		.max_cap = 512,
		.priority = 1,
		.fixed_size = true,
		.bonus_ways = 0xffffffff,
		.activate_on_init = true,
	}, {
		.usecase_id = LLCC_CVPFW,
		.slice_id = 19,
		.max_cap = 64,
		.priority = 4,
		.fixed_size = true,
		.bonus_ways = 0xffffffff,
	}, {
		.usecase_id = LLCC_CMPTHCP,
		.slice_id = 15,
		.max_cap = 256,
		.priority = 4,
		.fixed_size = true,
		.bonus_ways = 0xffffffff,
	}, {
		.usecase_id = LLCC_LCPDARE,
		.slice_id = 30,
		.max_cap = 128,
		.priority = 5,
		.fixed_size = true,
		.bonus_ways = 0xffffffff,
		.activate_on_init = true,
		.alloc_oneway_en = true,
	}, {
		.usecase_id = LLCC_AENPU,
		.slice_id = 3,
		.max_cap = 3072,
		.priority = 1,
		.fixed_size = true,
		.bonus_ways = 0xffffffff,
		.cache_mode = 2,
	}, {
		.usecase_id = LLCC_ISLAND1,
		.slice_id = 12,
		.max_cap = 7936,
		.priority = 7,
		.fixed_size = true,
		.bonus_ways = 0x7fffffff,
	}, {
		.usecase_id = LLCC_DISP_WB,
		.slice_id = 23,
		.max_cap = 512,
		.priority = 4,
		.fixed_size = true,
		.bonus_ways = 0xffffffff,
	}, {
		.usecase_id = LLCC_VIDVSP,
		.slice_id = 4,
		.max_cap = 256,
		.priority = 4,
		.fixed_size = true,
		.bonus_ways = 0xffffffff,
	}, {
		.usecase_id = LLCC_VIDDEC,
		.slice_id = 5,
		.max_cap = 6144,
		.priority = 4,
		.fixed_size = true,
		.bonus_ways = 0xffffffff,
		.cache_mode = 2,
		.ovcap_prio = true,
		.parent_slice_id = 33,
	}, {
		.usecase_id = LLCC_CAMOFE,
		.slice_id = 33,
		.max_cap = 6144,
		.priority = 4,
		.fixed_size = true,
		.bonus_ways = 0xffffffff,
		.stale_en = true,
		.ovcap_prio = true,
		.parent_slice_id = 33,
	}, {
		.usecase_id = LLCC_CAMRTIP,
		.slice_id = 13,
		.max_cap = 1024,
		.priority = 4,
		.fixed_size = true,
		.bonus_ways = 0xffffffff,
		.stale_en = true,
		.ovcap_prio = true,
		.parent_slice_id = 33,
	}, {
		.usecase_id = LLCC_CAMSRTIP,
		.slice_id = 14,
		.max_cap = 6144,
		.priority = 4,
		.fixed_size = true,
		.bonus_ways = 0xffffffff,
		.stale_en = true,
		.ovcap_prio = true,
		.parent_slice_id = 33,
	}, {
		.usecase_id = LLCC_CAMRTRF,
		.slice_id = 7,
		.max_cap = 3584,
		.priority = 1,
		.fixed_size = true,
		.bonus_ways = 0xffffffff,
		.stale_en = true,
		.ovcap_prio = true,
		.parent_slice_id = 33,
	}, {
		.usecase_id = LLCC_CAMSRTRF,
		.slice_id = 21,
		.max_cap = 6144,
		.priority = 1,
		.fixed_size = true,
		.bonus_ways = 0xffffffff,
		.stale_en = true,
		.ovcap_prio = true,
		.parent_slice_id = 33,
	}, {
		.usecase_id = LLCC_CPUSSMPAM,
		.slice_id = 6,
		.max_cap = 2048,
		.priority = 1,
		.fixed_size = true,
		.bonus_ways = 0xffffffff,
		.activate_on_init = true,
		.write_scid_en = true,
	},
};

static const struct llcc_slice_config qcs615_data[] = {
	{
		.usecase_id = LLCC_CPUSS,
		.slice_id = 1,
		.max_cap = 128,
		.priority = 1,
		.bonus_ways = 0xf,
		.cache_mode = 0,
		.activate_on_init = true,
		.write_scid_en = true,
	}, {
		.usecase_id = LLCC_MDM,
		.slice_id = 8,
		.max_cap = 256,
		.priority = 0,
		.fixed_size = true,
		.bonus_ways = 0xf,
		.cache_mode = 0,
		.activate_on_init = true,
	}, {
		.usecase_id = LLCC_GPUHTW,
		.slice_id = 11,
		.max_cap = 128,
		.priority = 1,
		.fixed_size = true,
		.bonus_ways = 0xf,
		.cache_mode = 0,
		.activate_on_init = true,
	}, {
		.usecase_id = LLCC_GPU,
		.slice_id = 12,
		.max_cap = 128,
		.priority = 1,
		.bonus_ways = 0xf,
		.cache_mode = 0,
		.activate_on_init = true,
	},
};

static const struct llcc_slice_config qcs8300_data[] = {
	{
		.usecase_id = LLCC_GPUHTW,
		.slice_id = 11,
		.max_cap = 128,
		.priority = 1,
		.fixed_size = true,
		.bonus_ways = 0xf,
		.cache_mode = 0,
		.retain_on_pc = true,
	}, {
		.usecase_id = LLCC_GPU,
		.slice_id = 12,
		.max_cap = 512,
		.priority = 1,
		.fixed_size = true,
		.bonus_ways = 0xf,
		.cache_mode = 0,
		.retain_on_pc = true,
		.write_scid_en = true,
	}, {
		.usecase_id = LLCC_MMUHWT,
		.slice_id = 13,
		.max_cap = 128,
		.priority = 1,
		.fixed_size = true,
		.bonus_ways = 0xf,
		.cache_mode = 0,
		.activate_on_init = true,
	}, {
		.usecase_id = LLCC_ECC,
		.slice_id = 26,
		.max_cap = 256,
		.priority = 3,
		.fixed_size = true,
		.bonus_ways = 0xf,
		.cache_mode = 0,
		.activate_on_init = true,
	}, {
		.usecase_id = LLCC_WRCACHE,
		.slice_id = 31,
		.max_cap = 128,
		.priority = 1,
		.fixed_size = true,
		.bonus_ways = 0xf,
		.cache_mode = 0,
		.activate_on_init = true,
	},
};

static const struct llcc_slice_config qdu1000_data_2ch[] = {
	{
		.usecase_id = LLCC_MDMHPGRW,
		.slice_id = 7,
		.max_cap = 512,
		.priority = 1,
		.fixed_size = true,
		.bonus_ways = 0xfff,
		.cache_mode = 0,
		.retain_on_pc = true,
	}, {
		.usecase_id = LLCC_MODHW,
		.slice_id = 9,
		.max_cap = 256,
		.priority = 1,
		.fixed_size = true,
		.bonus_ways = 0xfff,
		.cache_mode = 0,
		.retain_on_pc = true,
	}, {
		.usecase_id = LLCC_MDMPNG,
		.slice_id = 21,
		.max_cap = 256,
		.priority = 0,
		.fixed_size = true,
		.bonus_ways = 0x3,
		.cache_mode = 0,
		.retain_on_pc = true,
	}, {
		.usecase_id = LLCC_ECC,
		.slice_id = 26,
		.max_cap = 512,
		.priority = 3,
		.fixed_size = true,
		.bonus_ways = 0xffc,
		.cache_mode = 0,
		.activate_on_init = true,
	}, {
		.usecase_id = LLCC_MODPE,
		.slice_id = 29,
		.max_cap = 256,
		.priority = 1,
		.fixed_size = true,
		.bonus_ways = 0xfff,
		.cache_mode = 0,
		.retain_on_pc = true,
	}, {
		.usecase_id = LLCC_APTCM,
		.slice_id = 30,
		.max_cap = 256,
		.priority = 3,
		.fixed_size = true,
		.res_ways = 0xc,
		.cache_mode = 1,
		.retain_on_pc = true,
	}, {
		.usecase_id = LLCC_WRCACHE,
		.slice_id = 31,
		.max_cap = 128,
		.priority = 1,
		.fixed_size = true,
		.bonus_ways = 0x3,
		.cache_mode = 0,
		.activate_on_init = true,
	},
};

static const struct llcc_slice_config qdu1000_data_4ch[] = {
	{
		.usecase_id = LLCC_MDMHPGRW,
		.slice_id = 7,
		.max_cap = 1024,
		.priority = 1,
		.fixed_size = true,
		.bonus_ways = 0xfff,
		.cache_mode = 0,
		.retain_on_pc = true,
	}, {
		.usecase_id = LLCC_MODHW,
		.slice_id = 9,
		.max_cap = 512,
		.priority = 1,
		.fixed_size = true,
		.bonus_ways = 0xfff,
		.cache_mode = 0,
		.retain_on_pc = true,
	}, {
		.usecase_id = LLCC_MDMPNG,
		.slice_id = 21,
		.max_cap = 512,
		.priority = 0,
		.fixed_size = true,
		.bonus_ways = 0x3,
		.cache_mode = 0,
		.retain_on_pc = true,
	}, {
		.usecase_id = LLCC_ECC,
		.slice_id = 26,
		.max_cap = 1024,
		.priority = 3,
		.fixed_size = true,
		.bonus_ways = 0xffc,
		.cache_mode = 0,
		.activate_on_init = true,
	}, {
		.usecase_id = LLCC_MODPE,
		.slice_id = 29,
		.max_cap = 512,
		.priority = 1,
		.fixed_size = true,
		.bonus_ways = 0xfff,
		.cache_mode = 0,
		.retain_on_pc = true,
	}, {
		.usecase_id = LLCC_APTCM,
		.slice_id = 30,
		.max_cap = 512,
		.priority = 3,
		.fixed_size = true,
		.res_ways = 0xc,
		.cache_mode = 1,
		.retain_on_pc = true,
	}, {
		.usecase_id = LLCC_WRCACHE,
		.slice_id = 31,
		.max_cap = 256,
		.priority = 1,
		.fixed_size = true,
		.bonus_ways = 0x3,
		.cache_mode = 0,
		.activate_on_init = true,
	},
};

static const struct llcc_slice_config qdu1000_data_8ch[] = {
	{
		.usecase_id = LLCC_MDMHPGRW,
		.slice_id = 7,
		.max_cap = 2048,
		.priority = 1,
		.fixed_size = true,
		.bonus_ways = 0xfff,
		.cache_mode = 0,
		.retain_on_pc = true,
	}, {
		.usecase_id = LLCC_MODHW,
		.slice_id = 9,
		.max_cap = 1024,
		.priority = 1,
		.fixed_size = true,
		.bonus_ways = 0xfff,
		.cache_mode = 0,
		.retain_on_pc = true,
	}, {
		.usecase_id = LLCC_MDMPNG,
		.slice_id = 21,
		.max_cap = 1024,
		.priority = 0,
		.fixed_size = true,
		.bonus_ways = 0x3,
		.cache_mode = 0,
		.retain_on_pc = true,
	}, {
		.usecase_id = LLCC_ECC,
		.slice_id = 26,
		.max_cap = 2048,
		.priority = 3,
		.fixed_size = true,
		.bonus_ways = 0xffc,
		.cache_mode = 0,
		.activate_on_init = true,
	}, {
		.usecase_id = LLCC_MODPE,
		.slice_id = 29,
		.max_cap = 1024,
		.priority = 1,
		.fixed_size = true,
		.bonus_ways = 0xfff,
		.cache_mode = 0,
		.retain_on_pc = true,
	}, {
		.usecase_id = LLCC_APTCM,
		.slice_id = 30,
		.max_cap = 1024,
		.priority = 3,
		.fixed_size = true,
		.res_ways = 0xc,
		.cache_mode = 1,
		.retain_on_pc = true,
	}, {
		.usecase_id = LLCC_WRCACHE,
		.slice_id = 31,
		.max_cap = 512,
		.priority = 1,
		.fixed_size = true,
		.bonus_ways = 0x3,
		.cache_mode = 0,
		.activate_on_init = true,
	},
};

static const struct llcc_slice_config x1e80100_data[] = {
	{
		.usecase_id = LLCC_CPUSS,
		.slice_id = 1,
		.max_cap = 6144,
		.priority = 1,
		.fixed_size = true,
		.bonus_ways = 0xfff,
		.cache_mode = 0,
		.activate_on_init = true,
	}, {
		.usecase_id = LLCC_VIDSC0,
		.slice_id = 2,
		.max_cap = 512,
		.priority = 4,
		.fixed_size = true,
		.bonus_ways = 0xfff,
		.cache_mode = 0,
	}, {
		.usecase_id = LLCC_AUDIO,
		.slice_id = 6,
		.max_cap = 1024,
		.priority = 1,
		.fixed_size = true,
		.bonus_ways = 0xfff,
		.cache_mode = 0,
	}, {
		.usecase_id = LLCC_CMPT,
		.slice_id = 10,
		.max_cap = 6144,
		.priority = 1,
		.fixed_size = true,
		.bonus_ways = 0xfff,
		.cache_mode = 0,
	}, {
		.usecase_id = LLCC_GPUHTW,
		.slice_id = 11,
		.max_cap = 512,
		.priority = 1,
		.fixed_size = true,
		.bonus_ways = 0xfff,
		.cache_mode = 0,
	}, {
		.usecase_id = LLCC_GPU,
		.slice_id = 9,
		.max_cap = 4608,
		.priority = 1,
		.bonus_ways = 0xfff,
		.cache_mode = 0,
		.write_scid_en = true,
		.write_scid_cacheable_en = true,
		.stale_en = true,
	}, {
		.usecase_id = LLCC_MMUHWT,
		.slice_id = 18,
		.max_cap = 512,
		.priority = 1,
		.fixed_size = true,
		.bonus_ways = 0xfff,
		.cache_mode = 0,
		.activate_on_init = true,
	}, {
		.usecase_id = LLCC_AUDHW,
		.slice_id = 22,
		.max_cap = 1024,
		.priority = 1,
		.fixed_size = true,
		.bonus_ways = 0xfff,
		.cache_mode = 0,
	}, {
		.usecase_id = LLCC_CVP,
		.slice_id = 8,
		.max_cap = 512,
		.priority = 4,
		.fixed_size = true,
		.bonus_ways = 0xfff,
		.cache_mode = 0,
	}, {
		.usecase_id = LLCC_WRCACHE,
		.slice_id = 31,
		.max_cap = 1024,
		.priority = 1,
		.fixed_size = true,
		.bonus_ways = 0xfff,
		.cache_mode = 0,
		.activate_on_init = true,
	}, {
		.usecase_id = LLCC_CAMEXP0,
		.slice_id = 4,
		.max_cap = 256,
		.priority = 4,
		.fixed_size = true,
		.bonus_ways = 0x3,
		.cache_mode = 0,
	}, {
		.usecase_id = LLCC_CAMEXP1,
		.slice_id = 7,
		.max_cap = 3072,
		.priority = 3,
		.fixed_size = true,
		.bonus_ways = 0xffc,
		.cache_mode = 2,
	}, {
		.usecase_id = LLCC_LCPDARE,
		.slice_id = 30,
		.max_cap = 512,
		.priority = 3,
		.fixed_size = true,
		.bonus_ways = 0xfff,
		.cache_mode = 0,
		.activate_on_init = true,
		.alloc_oneway_en = true,
	}, {
		.usecase_id = LLCC_AENPU,
		.slice_id = 3,
		.max_cap = 3072,
		.priority = 1,
		.fixed_size = true,
		.bonus_ways = 0xfff,
		.cache_mode = 2,
	}, {
		.usecase_id = LLCC_ISLAND1,
		.slice_id = 12,
		.max_cap = 2048,
		.priority = 7,
		.fixed_size = true,
		.res_ways = 0xf,
		.cache_mode = 0,
	}, {
		.usecase_id = LLCC_CAMEXP2,
		.slice_id = 19,
		.max_cap = 3072,
		.priority = 3,
		.fixed_size = true,
		.bonus_ways = 0xffc,
		.cache_mode = 2,
	}, {
		.usecase_id = LLCC_CAMEXP3,
		.slice_id = 20,
		.max_cap = 3072,
		.priority = 2,
		.fixed_size = true,
		.bonus_ways = 0xffc,
		.cache_mode = 2,
	}, {
		.usecase_id = LLCC_CAMEXP4,
		.slice_id = 21,
		.max_cap = 3072,
		.priority = 2,
		.fixed_size = true,
		.bonus_ways = 0xffc,
		.cache_mode = 2,
	},
};

static const struct llcc_edac_reg_offset llcc_v1_edac_reg_offset = {
	.trp_ecc_error_status0 = 0x20344,
	.trp_ecc_error_status1 = 0x20348,
	.trp_ecc_sb_err_syn0 = 0x2304c,
	.trp_ecc_db_err_syn0 = 0x20370,
	.trp_ecc_error_cntr_clear = 0x20440,
	.trp_interrupt_0_status = 0x20480,
	.trp_interrupt_0_clear = 0x20484,
	.trp_interrupt_0_enable = 0x20488,

	/* LLCC Common registers */
	.cmn_status0 = 0x3000c,
	.cmn_interrupt_0_enable = 0x3001c,
	.cmn_interrupt_2_enable = 0x3003c,

	/* LLCC DRP registers */
	.drp_ecc_error_cfg = 0x40000,
	.drp_ecc_error_cntr_clear = 0x40004,
	.drp_interrupt_status = 0x41000,
	.drp_interrupt_clear = 0x41008,
	.drp_interrupt_enable = 0x4100c,
	.drp_ecc_error_status0 = 0x42044,
	.drp_ecc_error_status1 = 0x42048,
	.drp_ecc_sb_err_syn0 = 0x4204c,
	.drp_ecc_db_err_syn0 = 0x42070,
};

static const struct llcc_edac_reg_offset llcc_v2_1_edac_reg_offset = {
	.trp_ecc_error_status0 = 0x20344,
	.trp_ecc_error_status1 = 0x20348,
	.trp_ecc_sb_err_syn0 = 0x2034c,
	.trp_ecc_db_err_syn0 = 0x20370,
	.trp_ecc_error_cntr_clear = 0x20440,
	.trp_interrupt_0_status = 0x20480,
	.trp_interrupt_0_clear = 0x20484,
	.trp_interrupt_0_enable = 0x20488,

	/* LLCC Common registers */
	.cmn_status0 = 0x3400c,
	.cmn_interrupt_0_enable = 0x3401c,
	.cmn_interrupt_2_enable = 0x3403c,

	/* LLCC DRP registers */
	.drp_ecc_error_cfg = 0x50000,
	.drp_ecc_error_cntr_clear = 0x50004,
	.drp_interrupt_status = 0x50020,
	.drp_interrupt_clear = 0x50028,
	.drp_interrupt_enable = 0x5002c,
	.drp_ecc_error_status0 = 0x520f4,
	.drp_ecc_error_status1 = 0x520f8,
	.drp_ecc_sb_err_syn0 = 0x520fc,
	.drp_ecc_db_err_syn0 = 0x52120,
};

static const struct llcc_edac_reg_offset llcc_v6_edac_reg_offset = {
	.trp_ecc_error_status0 = 0x47448,
	.trp_ecc_error_status1 = 0x47450,
	.trp_ecc_sb_err_syn0 = 0x47490,
	.trp_ecc_db_err_syn0 = 0x474d0,
	.trp_ecc_error_cntr_clear = 0x47444,
	.trp_interrupt_0_status = 0x47600,
	.trp_interrupt_0_clear = 0x47604,
	.trp_interrupt_0_enable = 0x47608,

	/* LLCC Common registers */
	.cmn_status0 = 0x6400c,
	.cmn_interrupt_0_enable = 0x6401c,
	.cmn_interrupt_2_enable = 0x6403c,

	/* LLCC DRP registers */
	.drp_ecc_error_cfg = 0x80000,
	.drp_ecc_error_cntr_clear = 0x80004,
	.drp_interrupt_status = 0x80020,
	.drp_interrupt_clear = 0x80028,
	.drp_interrupt_enable = 0x8002c,
	.drp_ecc_error_status0 = 0x820f4,
	.drp_ecc_error_status1 = 0x820f8,
	.drp_ecc_sb_err_syn0 = 0x820fc,
	.drp_ecc_db_err_syn0 = 0x82120,
};

/* LLCC register offset starting from v1.0.0 */
static const u32 llcc_v1_reg_offset[] = {
	[LLCC_COMMON_HW_INFO]	= 0x00030000,
	[LLCC_COMMON_STATUS0]	= 0x0003000c,
};

/* LLCC register offset starting from v2.0.1 */
static const u32 llcc_v2_1_reg_offset[] = {
	[LLCC_COMMON_HW_INFO]	= 0x00034000,
	[LLCC_COMMON_STATUS0]	= 0x0003400c,
};

/* LLCC register offset starting from v6.0.0 */
static const u32 llcc_v6_reg_offset[] = {
	[LLCC_COMMON_HW_INFO]	        = 0x00064000,
	[LLCC_COMMON_STATUS0]	        = 0x0006400c,
	[LLCC_TRP_ATTR0_CFG]		= 0x00041000,
	[LLCC_TRP_ATTR1_CFG]		= 0x00041008,
	[LLCC_TRP_ATTR2_CFG]		= 0x00041010,
	[LLCC_TRP_ATTR3_CFG]		= 0x00041014,
	[LLCC_TRP_SID_DIS_CAP_ALLOC]	= 0x00042000,
	[LLCC_TRP_ALGO_STALE_EN]	= 0x00042008,
	[LLCC_TRP_ALGO_STALE_CAP_EN]	= 0x00042010,
	[LLCC_TRP_ALGO_MRU0]		= 0x00042018,
	[LLCC_TRP_ALGO_MRU1]		= 0x00042020,
	[LLCC_TRP_ALGO_ALLOC0]		= 0x00042028,
	[LLCC_TRP_ALGO_ALLOC1]		= 0x00042030,
	[LLCC_TRP_ALGO_ALLOC2]		= 0x00042038,
	[LLCC_TRP_ALGO_ALLOC3]		= 0x00042040,
	[LLCC_TRP_WRS_EN]		= 0x00042080,
	[LLCC_TRP_WRS_CACHEABLE_EN]	= 0x00042088,
};

static const struct qcom_llcc_config qcs615_cfg[] = {
	{
		.sct_data	= qcs615_data,
		.size		= ARRAY_SIZE(qcs615_data),
		.reg_offset	= llcc_v1_reg_offset,
		.edac_reg_offset = &llcc_v1_edac_reg_offset,
	},
};

static const struct qcom_llcc_config qcs8300_cfg[] = {
	{
		.sct_data	= qcs8300_data,
		.size		= ARRAY_SIZE(qcs8300_data),
		.reg_offset	= llcc_v2_1_reg_offset,
		.edac_reg_offset = &llcc_v2_1_edac_reg_offset,
		.num_banks	= 4,
	},
};

static const struct qcom_llcc_config qdu1000_cfg[] = {
	{
		.sct_data       = qdu1000_data_8ch,
		.size		= ARRAY_SIZE(qdu1000_data_8ch),
		.reg_offset	= llcc_v2_1_reg_offset,
		.edac_reg_offset = &llcc_v2_1_edac_reg_offset,
	},
	{
		.sct_data       = qdu1000_data_4ch,
		.size           = ARRAY_SIZE(qdu1000_data_4ch),
		.reg_offset     = llcc_v2_1_reg_offset,
		.edac_reg_offset = &llcc_v2_1_edac_reg_offset,
	},
	{
		.sct_data       = qdu1000_data_4ch,
		.size           = ARRAY_SIZE(qdu1000_data_4ch),
		.reg_offset     = llcc_v2_1_reg_offset,
		.edac_reg_offset = &llcc_v2_1_edac_reg_offset,
	},
	{
		.sct_data       = qdu1000_data_2ch,
		.size           = ARRAY_SIZE(qdu1000_data_2ch),
		.reg_offset     = llcc_v2_1_reg_offset,
		.edac_reg_offset = &llcc_v2_1_edac_reg_offset,
	},
};

static const struct qcom_llcc_config ipq5424_cfg[] = {
	{
		.sct_data       = ipq5424_data,
		.size           = ARRAY_SIZE(ipq5424_data),
		.reg_offset     = llcc_v2_1_reg_offset,
		.edac_reg_offset = &llcc_v2_1_edac_reg_offset,
		.no_broadcast_register = true,
	},
};

static const struct qcom_llcc_config sa8775p_cfg[] = {
	{
		.sct_data	= sa8775p_data,
		.size		= ARRAY_SIZE(sa8775p_data),
		.reg_offset	= llcc_v2_1_reg_offset,
		.edac_reg_offset = &llcc_v2_1_edac_reg_offset,
	},
};

static const struct qcom_llcc_config sar1130p_cfg[] = {
	{
		.sct_data	= sar1130p_data,
		.size		= ARRAY_SIZE(sar1130p_data),
		.reg_offset	= llcc_v2_1_reg_offset,
		.edac_reg_offset = &llcc_v2_1_edac_reg_offset,
		.max_cap_shift	= 14,
		.num_banks	= 2,
	},
};

static const struct qcom_llcc_config sar2130p_cfg[] = {
	{
		.sct_data	= sar2130p_data,
		.size		= ARRAY_SIZE(sar2130p_data),
		.reg_offset	= llcc_v2_1_reg_offset,
		.edac_reg_offset = &llcc_v2_1_edac_reg_offset,
		.max_cap_shift	= 14,
		.num_banks	= 2,
	},
};

static const struct qcom_llcc_config sc7180_cfg[] = {
	{
		.sct_data	= sc7180_data,
		.size		= ARRAY_SIZE(sc7180_data),
		.reg_offset	= llcc_v1_reg_offset,
		.edac_reg_offset = &llcc_v1_edac_reg_offset,
	},
};

static const struct qcom_llcc_config sc7280_cfg[] = {
	{
		.sct_data	= sc7280_data,
		.size		= ARRAY_SIZE(sc7280_data),
		.reg_offset	= llcc_v1_reg_offset,
		.edac_reg_offset = &llcc_v1_edac_reg_offset,
	},
};

static const struct qcom_llcc_config sc8180x_cfg[] = {
	{
		.sct_data	= sc8180x_data,
		.size		= ARRAY_SIZE(sc8180x_data),
		.reg_offset	= llcc_v1_reg_offset,
		.edac_reg_offset = &llcc_v1_edac_reg_offset,
	},
};

static const struct qcom_llcc_config sc8280xp_cfg[] = {
	{
		.sct_data	= sc8280xp_data,
		.size		= ARRAY_SIZE(sc8280xp_data),
		.reg_offset	= llcc_v1_reg_offset,
		.edac_reg_offset = &llcc_v1_edac_reg_offset,
	},
};

static const struct qcom_llcc_config sdm845_cfg[] = {
	{
		.sct_data	= sdm845_data,
		.size		= ARRAY_SIZE(sdm845_data),
		.skip_llcc_cfg	= true,
		.reg_offset	= llcc_v1_reg_offset,
		.edac_reg_offset = &llcc_v1_edac_reg_offset,
		.no_edac	= true,
	},
};

static const struct qcom_llcc_config sm6350_cfg[] = {
	{
		.sct_data	= sm6350_data,
		.size		= ARRAY_SIZE(sm6350_data),
		.reg_offset	= llcc_v1_reg_offset,
		.edac_reg_offset = &llcc_v1_edac_reg_offset,
	},
};

static const struct qcom_llcc_config sm7150_cfg[] = {
	{
		.sct_data       = sm7150_data,
		.size           = ARRAY_SIZE(sm7150_data),
		.reg_offset	= llcc_v1_reg_offset,
		.edac_reg_offset = &llcc_v1_edac_reg_offset,
	},
};

static const struct qcom_llcc_config sm8150_cfg[] = {
	{
		.sct_data       = sm8150_data,
		.size           = ARRAY_SIZE(sm8150_data),
		.reg_offset	= llcc_v1_reg_offset,
		.edac_reg_offset = &llcc_v1_edac_reg_offset,
	},
};

static const struct qcom_llcc_config sm8250_cfg[] = {
	{
		.sct_data       = sm8250_data,
		.size           = ARRAY_SIZE(sm8250_data),
		.reg_offset	= llcc_v1_reg_offset,
		.edac_reg_offset = &llcc_v1_edac_reg_offset,
	},
};

static const struct qcom_llcc_config sm8350_cfg[] = {
	{
		.sct_data       = sm8350_data,
		.size           = ARRAY_SIZE(sm8350_data),
		.reg_offset	= llcc_v1_reg_offset,
		.edac_reg_offset = &llcc_v1_edac_reg_offset,
	},
};

static const struct qcom_llcc_config sm8450_cfg[] = {
	{
		.sct_data       = sm8450_data,
		.size           = ARRAY_SIZE(sm8450_data),
		.reg_offset	= llcc_v2_1_reg_offset,
		.edac_reg_offset = &llcc_v2_1_edac_reg_offset,
	},
};

static const struct qcom_llcc_config sm8550_cfg[] = {
	{
		.sct_data       = sm8550_data,
		.size           = ARRAY_SIZE(sm8550_data),
		.reg_offset	= llcc_v2_1_reg_offset,
		.edac_reg_offset = &llcc_v2_1_edac_reg_offset,
	},
};

static const struct qcom_llcc_config sm8650_cfg[] = {
	{
		.sct_data       = sm8650_data,
		.size           = ARRAY_SIZE(sm8650_data),
		.reg_offset	= llcc_v2_1_reg_offset,
		.edac_reg_offset = &llcc_v2_1_edac_reg_offset,
	},
};

static const struct qcom_llcc_config sm8750_cfg[] = {
	{
		.sct_data		= sm8750_data,
		.size			= ARRAY_SIZE(sm8750_data),
		.skip_llcc_cfg	= false,
		.reg_offset		= llcc_v6_reg_offset,
		.edac_reg_offset = &llcc_v6_edac_reg_offset,
	},
};

static const struct qcom_llcc_config x1e80100_cfg[] = {
	{
		.sct_data	= x1e80100_data,
		.size		= ARRAY_SIZE(x1e80100_data),
		.reg_offset	= llcc_v2_1_reg_offset,
		.edac_reg_offset = &llcc_v2_1_edac_reg_offset,
		.irq_configured = true,
	},
};

static const struct qcom_sct_config qcs615_cfgs = {
	.llcc_config	= qcs615_cfg,
	.num_config	= ARRAY_SIZE(qcs615_cfg),
};

static const struct qcom_sct_config qcs8300_cfgs = {
	.llcc_config	= qcs8300_cfg,
	.num_config	= ARRAY_SIZE(qcs8300_cfg),
};

static const struct qcom_sct_config qdu1000_cfgs = {
	.llcc_config	= qdu1000_cfg,
	.num_config	= ARRAY_SIZE(qdu1000_cfg),
};

static const struct qcom_sct_config ipq5424_cfgs = {
	.llcc_config	= ipq5424_cfg,
	.num_config	= ARRAY_SIZE(ipq5424_cfg),
};

static const struct qcom_sct_config sa8775p_cfgs = {
	.llcc_config	= sa8775p_cfg,
	.num_config	= ARRAY_SIZE(sa8775p_cfg),
};

static const struct qcom_sct_config sar1130p_cfgs = {
	.llcc_config	= sar1130p_cfg,
	.num_config	= ARRAY_SIZE(sar1130p_cfg),
};

static const struct qcom_sct_config sar2130p_cfgs = {
	.llcc_config	= sar2130p_cfg,
	.num_config	= ARRAY_SIZE(sar2130p_cfg),
};

static const struct qcom_sct_config sc7180_cfgs = {
	.llcc_config	= sc7180_cfg,
	.num_config	= ARRAY_SIZE(sc7180_cfg),
};

static const struct qcom_sct_config sc7280_cfgs = {
	.llcc_config	= sc7280_cfg,
	.num_config	= ARRAY_SIZE(sc7280_cfg),
};

static const struct qcom_sct_config sc8180x_cfgs = {
	.llcc_config	= sc8180x_cfg,
	.num_config	= ARRAY_SIZE(sc8180x_cfg),
};

static const struct qcom_sct_config sc8280xp_cfgs = {
	.llcc_config	= sc8280xp_cfg,
	.num_config	= ARRAY_SIZE(sc8280xp_cfg),
};

static const struct qcom_sct_config sdm845_cfgs = {
	.llcc_config	= sdm845_cfg,
	.num_config	= ARRAY_SIZE(sdm845_cfg),
};

static const struct qcom_sct_config sm6350_cfgs = {
	.llcc_config	= sm6350_cfg,
	.num_config	= ARRAY_SIZE(sm6350_cfg),
};

static const struct qcom_sct_config sm7150_cfgs = {
	.llcc_config	= sm7150_cfg,
	.num_config	= ARRAY_SIZE(sm7150_cfg),
};

static const struct qcom_sct_config sm8150_cfgs = {
	.llcc_config	= sm8150_cfg,
	.num_config	= ARRAY_SIZE(sm8150_cfg),
};

static const struct qcom_sct_config sm8250_cfgs = {
	.llcc_config	= sm8250_cfg,
	.num_config	= ARRAY_SIZE(sm8250_cfg),
};

static const struct qcom_sct_config sm8350_cfgs = {
	.llcc_config	= sm8350_cfg,
	.num_config	= ARRAY_SIZE(sm8350_cfg),
};

static const struct qcom_sct_config sm8450_cfgs = {
	.llcc_config	= sm8450_cfg,
	.num_config	= ARRAY_SIZE(sm8450_cfg),
};

static const struct qcom_sct_config sm8550_cfgs = {
	.llcc_config	= sm8550_cfg,
	.num_config	= ARRAY_SIZE(sm8550_cfg),
};

static const struct qcom_sct_config sm8650_cfgs = {
	.llcc_config	= sm8650_cfg,
	.num_config	= ARRAY_SIZE(sm8650_cfg),
};

static const struct qcom_sct_config sm8750_cfgs = {
	.llcc_config	= sm8750_cfg,
	.num_config	= ARRAY_SIZE(sm8750_cfg),
};

static const struct qcom_sct_config x1e80100_cfgs = {
	.llcc_config	= x1e80100_cfg,
	.num_config	= ARRAY_SIZE(x1e80100_cfg),
};

static struct llcc_drv_data *drv_data = (void *) -EPROBE_DEFER;

/**
 * llcc_slice_getd - get llcc slice descriptor
 * @uid: usecase_id for the client
 *
 * A pointer to llcc slice descriptor will be returned on success
 * and error pointer is returned on failure
 */
struct llcc_slice_desc *llcc_slice_getd(u32 uid)
{
	const struct llcc_slice_config *cfg;
	struct llcc_slice_desc *desc;
	u32 sz, count;

	if (IS_ERR(drv_data))
		return ERR_CAST(drv_data);

	cfg = drv_data->cfg;
	sz = drv_data->cfg_size;

	for (count = 0; cfg && count < sz; count++, cfg++)
		if (cfg->usecase_id == uid)
			break;

	if (count == sz || !cfg)
		return ERR_PTR(-ENODEV);

	desc = kzalloc(sizeof(*desc), GFP_KERNEL);
	if (!desc)
		return ERR_PTR(-ENOMEM);

	desc->slice_id = cfg->slice_id;
	desc->slice_size = cfg->max_cap;

	return desc;
}
EXPORT_SYMBOL_GPL(llcc_slice_getd);

/**
 * llcc_slice_putd - llcc slice descriptor
 * @desc: Pointer to llcc slice descriptor
 */
void llcc_slice_putd(struct llcc_slice_desc *desc)
{
	if (!IS_ERR_OR_NULL(desc))
		kfree(desc);
}
EXPORT_SYMBOL_GPL(llcc_slice_putd);

static int llcc_update_act_ctrl(u32 sid,
				u32 act_ctrl_reg_val, u32 status)
{
	struct regmap *regmap;
	u32 act_ctrl_reg;
	u32 act_clear_reg;
	u32 status_reg;
	u32 slice_status;
	int ret;

	if (IS_ERR(drv_data))
		return PTR_ERR(drv_data);

	act_ctrl_reg = LLCC_TRP_ACT_CTRLn(sid);
	act_clear_reg = LLCC_TRP_ACT_CLEARn(sid);
	status_reg = LLCC_TRP_STATUSn(sid);

	/* Set the ACTIVE trigger */
	act_ctrl_reg_val |= ACT_CTRL_ACT_TRIG;
	ret = regmap_write(drv_data->bcast_regmap, act_ctrl_reg,
				act_ctrl_reg_val);
	if (ret)
		return ret;

	/* Clear the ACTIVE trigger */
	act_ctrl_reg_val &= ~ACT_CTRL_ACT_TRIG;
	ret = regmap_write(drv_data->bcast_regmap, act_ctrl_reg,
				act_ctrl_reg_val);
	if (ret)
		return ret;

	if (drv_data->version >= LLCC_VERSION_4_1_0_0) {
		regmap = drv_data->bcast_and_regmap ?: drv_data->bcast_regmap;
		ret = regmap_read_poll_timeout(regmap, status_reg,
				      slice_status, (slice_status & ACT_COMPLETE),
				      0, LLCC_STATUS_READ_DELAY);
		if (ret)
			return ret;
	}

	ret = regmap_read_poll_timeout(drv_data->bcast_regmap, status_reg,
				      slice_status, !(slice_status & status),
				      0, LLCC_STATUS_READ_DELAY);
	if (ret)
		return ret;

	if (drv_data->version >= LLCC_VERSION_4_1_0_0)
		ret = regmap_write(drv_data->bcast_regmap, act_clear_reg,
					ACT_CLEAR);

	return ret;
}

/**
 * llcc_slice_activate - Activate the llcc slice
 * @desc: Pointer to llcc slice descriptor
 *
 * A value of zero will be returned on success and a negative errno will
 * be returned in error cases
 */
int llcc_slice_activate(struct llcc_slice_desc *desc)
{
	int ret;
	u32 act_ctrl_val;

	if (IS_ERR(drv_data))
		return PTR_ERR(drv_data);

	if (IS_ERR_OR_NULL(desc))
		return -EINVAL;

	mutex_lock(&drv_data->lock);
	if (test_bit(desc->slice_id, drv_data->bitmap)) {
		mutex_unlock(&drv_data->lock);
		return 0;
	}

	act_ctrl_val = ACT_CTRL_OPCODE_ACTIVATE << ACT_CTRL_OPCODE_SHIFT;

	ret = llcc_update_act_ctrl(desc->slice_id, act_ctrl_val,
				  DEACTIVATE);
	if (ret) {
		mutex_unlock(&drv_data->lock);
		return ret;
	}

	__set_bit(desc->slice_id, drv_data->bitmap);
	mutex_unlock(&drv_data->lock);

	return ret;
}
EXPORT_SYMBOL_GPL(llcc_slice_activate);

/**
 * llcc_slice_deactivate - Deactivate the llcc slice
 * @desc: Pointer to llcc slice descriptor
 *
 * A value of zero will be returned on success and a negative errno will
 * be returned in error cases
 */
int llcc_slice_deactivate(struct llcc_slice_desc *desc)
{
	u32 act_ctrl_val;
	int ret;

	if (IS_ERR(drv_data))
		return PTR_ERR(drv_data);

	if (IS_ERR_OR_NULL(desc))
		return -EINVAL;

	mutex_lock(&drv_data->lock);
	if (!test_bit(desc->slice_id, drv_data->bitmap)) {
		mutex_unlock(&drv_data->lock);
		return 0;
	}
	act_ctrl_val = ACT_CTRL_OPCODE_DEACTIVATE << ACT_CTRL_OPCODE_SHIFT;

	ret = llcc_update_act_ctrl(desc->slice_id, act_ctrl_val,
				  ACTIVATE);
	if (ret) {
		mutex_unlock(&drv_data->lock);
		return ret;
	}

	__clear_bit(desc->slice_id, drv_data->bitmap);
	mutex_unlock(&drv_data->lock);

	return ret;
}
EXPORT_SYMBOL_GPL(llcc_slice_deactivate);

/**
 * llcc_get_slice_id - return the slice id
 * @desc: Pointer to llcc slice descriptor
 */
int llcc_get_slice_id(struct llcc_slice_desc *desc)
{
	if (IS_ERR_OR_NULL(desc))
		return -EINVAL;

	return desc->slice_id;
}
EXPORT_SYMBOL_GPL(llcc_get_slice_id);

/**
 * llcc_get_slice_size - return the slice id
 * @desc: Pointer to llcc slice descriptor
 */
size_t llcc_get_slice_size(struct llcc_slice_desc *desc)
{
	if (IS_ERR_OR_NULL(desc))
		return 0;

	return desc->slice_size;
}
EXPORT_SYMBOL_GPL(llcc_get_slice_size);

static int _qcom_llcc_cfg_program(const struct llcc_slice_config *config,
				  const struct qcom_llcc_config *cfg)
{
	int ret;
	u32 attr2_cfg;
	u32 attr1_cfg;
	u32 attr0_cfg;
	u32 attr2_val;
	u32 attr1_val;
	u32 attr0_val;
	u32 max_cap_cacheline;
	struct llcc_slice_desc desc;

	attr1_val = config->cache_mode;
	attr1_val |= config->probe_target_ways << ATTR1_PROBE_TARGET_WAYS_SHIFT;
	attr1_val |= config->fixed_size << ATTR1_FIXED_SIZE_SHIFT;
	attr1_val |= config->priority << ATTR1_PRIORITY_SHIFT;

	max_cap_cacheline = MAX_CAP_TO_BYTES(config->max_cap);

	/*
	 * LLCC instances can vary for each target.
	 * The SW writes to broadcast register which gets propagated
	 * to each llcc instance (llcc0,.. llccN).
	 * Since the size of the memory is divided equally amongst the
	 * llcc instances, we need to configure the max cap accordingly.
	 */
	max_cap_cacheline = max_cap_cacheline / drv_data->num_banks;
	max_cap_cacheline >>= CACHE_LINE_SIZE_SHIFT;
	if (cfg->max_cap_shift)
		attr1_val |= max_cap_cacheline << cfg->max_cap_shift;
	else
		attr1_val |= max_cap_cacheline << ATTR1_MAX_CAP_SHIFT;

	attr1_cfg = LLCC_TRP_ATTR1_CFGn(config->slice_id);

	ret = regmap_write(drv_data->bcast_regmap, attr1_cfg, attr1_val);
	if (ret)
		return ret;

	if (drv_data->version >= LLCC_VERSION_4_1_0_0) {
		attr2_cfg = LLCC_TRP_ATTR2_CFGn(config->slice_id);
		attr0_val = config->res_ways;
		attr2_val = config->bonus_ways;
	} else {
		attr0_val = config->res_ways & ATTR0_RES_WAYS_MASK;
		attr0_val |= config->bonus_ways << ATTR0_BONUS_WAYS_SHIFT;
	}

	attr0_cfg = LLCC_TRP_ATTR0_CFGn(config->slice_id);

	ret = regmap_write(drv_data->bcast_regmap, attr0_cfg, attr0_val);
	if (ret)
		return ret;

	if (drv_data->version >= LLCC_VERSION_4_1_0_0) {
		ret = regmap_write(drv_data->bcast_regmap, attr2_cfg, attr2_val);
		if (ret)
			return ret;
	}

	/* At least SDM845 disallows non-secure writes to these registers */
	if (!cfg->skip_llcc_cfg) {
		u32 disable_cap_alloc, retain_pc;

		disable_cap_alloc = config->dis_cap_alloc << config->slice_id;
		ret = regmap_update_bits(drv_data->bcast_regmap, LLCC_TRP_SCID_DIS_CAP_ALLOC,
					 BIT(config->slice_id), disable_cap_alloc);
		if (ret)
			return ret;

		if (drv_data->version < LLCC_VERSION_4_1_0_0) {
			retain_pc = config->retain_on_pc << config->slice_id;
			ret = regmap_update_bits(drv_data->bcast_regmap, LLCC_TRP_PCB_ACT,
						 BIT(config->slice_id), retain_pc);
			if (ret)
				return ret;
		}
	}

	if (drv_data->version >= LLCC_VERSION_2_0_0_0) {
		u32 wren;

		wren = config->write_scid_en << config->slice_id;
		ret = regmap_update_bits(drv_data->bcast_regmap, LLCC_TRP_WRSC_EN,
					 BIT(config->slice_id), wren);
		if (ret)
			return ret;
	}

	if (drv_data->version >= LLCC_VERSION_2_1_0_0) {
		u32 wr_cache_en;

		wr_cache_en = config->write_scid_cacheable_en << config->slice_id;
		ret = regmap_update_bits(drv_data->bcast_regmap, LLCC_TRP_WRSC_CACHEABLE_EN,
					 BIT(config->slice_id), wr_cache_en);
		if (ret)
			return ret;
	}

	if (drv_data->version >= LLCC_VERSION_4_1_0_0) {
		u32 stale_en;
		u32 stale_cap_en;
		u32 mru_uncap_en;
		u32 mru_rollover;
		u32 alloc_oneway_en;
		u32 ovcap_en;
		u32 ovcap_prio;
		u32 vict_prio;

		stale_en = config->stale_en << config->slice_id;
		ret = regmap_update_bits(drv_data->bcast_regmap, LLCC_TRP_ALGO_CFG1,
					 BIT(config->slice_id), stale_en);
		if (ret)
			return ret;

		stale_cap_en = config->stale_cap_en << config->slice_id;
		ret = regmap_update_bits(drv_data->bcast_regmap, LLCC_TRP_ALGO_CFG2,
					 BIT(config->slice_id), stale_cap_en);
		if (ret)
			return ret;

		mru_uncap_en = config->mru_uncap_en << config->slice_id;
		ret = regmap_update_bits(drv_data->bcast_regmap, LLCC_TRP_ALGO_CFG3,
					 BIT(config->slice_id), mru_uncap_en);
		if (ret)
			return ret;

		mru_rollover = config->mru_rollover << config->slice_id;
		ret = regmap_update_bits(drv_data->bcast_regmap, LLCC_TRP_ALGO_CFG4,
					 BIT(config->slice_id), mru_rollover);
		if (ret)
			return ret;

		alloc_oneway_en = config->alloc_oneway_en << config->slice_id;
		ret = regmap_update_bits(drv_data->bcast_regmap, LLCC_TRP_ALGO_CFG5,
					 BIT(config->slice_id), alloc_oneway_en);
		if (ret)
			return ret;

		ovcap_en = config->ovcap_en << config->slice_id;
		ret = regmap_update_bits(drv_data->bcast_regmap, LLCC_TRP_ALGO_CFG6,
					 BIT(config->slice_id), ovcap_en);
		if (ret)
			return ret;

		ovcap_prio = config->ovcap_prio << config->slice_id;
		ret = regmap_update_bits(drv_data->bcast_regmap, LLCC_TRP_ALGO_CFG7,
					 BIT(config->slice_id), ovcap_prio);
		if (ret)
			return ret;

		vict_prio = config->vict_prio << config->slice_id;
		ret = regmap_update_bits(drv_data->bcast_regmap, LLCC_TRP_ALGO_CFG8,
					 BIT(config->slice_id), vict_prio);
		if (ret)
			return ret;
	}

	if (config->activate_on_init) {
		desc.slice_id = config->slice_id;
		ret = llcc_slice_activate(&desc);
	}

	return ret;
}

static int _qcom_llcc_cfg_program_v6(const struct llcc_slice_config *config,
				     const struct qcom_llcc_config *cfg)
{
	u32 stale_en, stale_cap_en, mru_uncap_en, mru_rollover;
	u32 alloc_oneway_en, ovcap_en, ovcap_prio, vict_prio;
	u32 attr0_cfg, attr1_cfg, attr2_cfg, attr3_cfg;
	u32 attr0_val, attr1_val, attr2_val, attr3_val;
	u32 slice_offset, reg_offset;
	struct llcc_slice_desc *desc;
	u32 wren, wr_cache_en;
	int ret;

	attr0_cfg = LLCC_V6_TRP_ATTR0_CFGn(config->slice_id);
	attr1_cfg = LLCC_V6_TRP_ATTR1_CFGn(config->slice_id);
	attr2_cfg = LLCC_V6_TRP_ATTR2_CFGn(config->slice_id);
	attr3_cfg = LLCC_V6_TRP_ATTR3_CFGn(config->slice_id);

	attr0_val = config->res_ways;
	attr1_val = config->bonus_ways;
	attr2_val = config->cache_mode;
	attr2_val |= FIELD_PREP(ATTR2_PROBE_TARGET_WAYS_MASK, config->probe_target_ways);
	attr2_val |= FIELD_PREP(ATTR2_FIXED_SIZE_MASK, config->fixed_size);
	attr2_val |= FIELD_PREP(ATTR2_PRIORITY_MASK, config->priority);

	if (config->parent_slice_id && config->fixed_size) {
		attr2_val |= FIELD_PREP(ATTR2_PARENT_SCID_MASK, config->parent_slice_id);
		attr2_val |= ATTR2_IN_A_GROUP_MASK;
	}

	attr3_val = MAX_CAP_TO_BYTES(config->max_cap);
	attr3_val /= drv_data->num_banks;
	attr3_val >>= CACHE_LINE_SIZE_SHIFT;

	ret = regmap_write(drv_data->bcast_regmap, attr0_cfg, attr0_val);
	if (ret)
		return ret;

	ret = regmap_write(drv_data->bcast_regmap, attr1_cfg, attr1_val);
	if (ret)
		return ret;

	ret = regmap_write(drv_data->bcast_regmap, attr2_cfg, attr2_val);
	if (ret)
		return ret;

	ret = regmap_write(drv_data->bcast_regmap, attr3_cfg, attr3_val);
	if (ret)
		return ret;

	slice_offset = config->slice_id % 32;
	reg_offset = (config->slice_id / 32) * 4;

	wren = config->write_scid_en << slice_offset;
	ret = regmap_update_bits(drv_data->bcast_regmap,
				 cfg->reg_offset[LLCC_TRP_WRS_EN] + reg_offset,
				 BIT(slice_offset), wren);
	if (ret)
		return ret;

	wr_cache_en = config->write_scid_cacheable_en << slice_offset;
	ret = regmap_update_bits(drv_data->bcast_regmap,
				 cfg->reg_offset[LLCC_TRP_WRS_CACHEABLE_EN] + reg_offset,
				 BIT(slice_offset), wr_cache_en);
	if (ret)
		return ret;

	stale_en = config->stale_en << slice_offset;
	ret = regmap_update_bits(drv_data->bcast_regmap,
				 cfg->reg_offset[LLCC_TRP_ALGO_STALE_EN] + reg_offset,
				 BIT(slice_offset), stale_en);
	if (ret)
		return ret;

	stale_cap_en = config->stale_cap_en << slice_offset;
	ret = regmap_update_bits(drv_data->bcast_regmap,
				 cfg->reg_offset[LLCC_TRP_ALGO_STALE_CAP_EN] + reg_offset,
				 BIT(slice_offset), stale_cap_en);
	if (ret)
		return ret;

	mru_uncap_en = config->mru_uncap_en << slice_offset;
	ret = regmap_update_bits(drv_data->bcast_regmap,
				 cfg->reg_offset[LLCC_TRP_ALGO_MRU0] + reg_offset,
				 BIT(slice_offset), mru_uncap_en);
	if (ret)
		return ret;

	mru_rollover = config->mru_rollover << slice_offset;
	ret = regmap_update_bits(drv_data->bcast_regmap,
				 cfg->reg_offset[LLCC_TRP_ALGO_MRU1] + reg_offset,
				 BIT(slice_offset), mru_rollover);
	if (ret)
		return ret;

	alloc_oneway_en = config->alloc_oneway_en << slice_offset;
	ret = regmap_update_bits(drv_data->bcast_regmap,
				 cfg->reg_offset[LLCC_TRP_ALGO_ALLOC0] + reg_offset,
				 BIT(slice_offset), alloc_oneway_en);
	if (ret)
		return ret;

	ovcap_en = config->ovcap_en << slice_offset;
	ret = regmap_update_bits(drv_data->bcast_regmap,
				 cfg->reg_offset[LLCC_TRP_ALGO_ALLOC1] + reg_offset,
				 BIT(slice_offset), ovcap_en);
	if (ret)
		return ret;

	ovcap_prio = config->ovcap_prio << slice_offset;
	ret = regmap_update_bits(drv_data->bcast_regmap,
				 cfg->reg_offset[LLCC_TRP_ALGO_ALLOC2] + reg_offset,
				 BIT(slice_offset), ovcap_prio);
	if (ret)
		return ret;

	vict_prio = config->vict_prio << slice_offset;
	ret = regmap_update_bits(drv_data->bcast_regmap,
				 cfg->reg_offset[LLCC_TRP_ALGO_ALLOC3] + reg_offset,
				 BIT(slice_offset), vict_prio);
	if (ret)
		return ret;

	if (config->activate_on_init) {
		desc = llcc_slice_getd(config->usecase_id);
		if (PTR_ERR_OR_ZERO(desc))
			return -EINVAL;

		ret = llcc_slice_activate(desc);
	}

	return ret;
}

static int qcom_llcc_cfg_program(struct platform_device *pdev,
				 const struct qcom_llcc_config *cfg)
{
	int i;
	u32 sz;
	int ret = 0;
	const struct llcc_slice_config *llcc_table;

	sz = drv_data->cfg_size;
	llcc_table = drv_data->cfg;

	if (drv_data->version >= LLCC_VERSION_6_0_0_0) {
		for (i = 0; i < sz; i++) {
			ret = _qcom_llcc_cfg_program_v6(&llcc_table[i], cfg);
			if (ret)
				return ret;
		}
	} else {
		for (i = 0; i < sz; i++) {
			ret = _qcom_llcc_cfg_program(&llcc_table[i], cfg);
			if (ret)
				return ret;
		}
	}

	return ret;
}

static int qcom_llcc_get_cfg_index(struct platform_device *pdev, u8 *cfg_index, int num_config)
{
	int ret;

	ret = nvmem_cell_read_u8(&pdev->dev, "multi-chan-ddr", cfg_index);
	if (ret == -ENOENT || ret == -EOPNOTSUPP) {
		if (num_config > 1)
			return -EINVAL;
		*cfg_index = 0;
		return 0;
	}

	if (!ret && *cfg_index >= num_config)
		ret = -EINVAL;

	return ret;
}

static void qcom_llcc_remove(struct platform_device *pdev)
{
	/* Set the global pointer to a error code to avoid referencing it */
	drv_data = ERR_PTR(-ENODEV);
}

static struct regmap *qcom_llcc_init_mmio(struct platform_device *pdev, u8 index,
					  const char *name)
{
	void __iomem *base;
	struct regmap_config llcc_regmap_config = {
		.reg_bits = 32,
		.reg_stride = 4,
		.val_bits = 32,
	};

	base = devm_platform_ioremap_resource(pdev, index);
	if (IS_ERR(base))
		return ERR_CAST(base);

	llcc_regmap_config.name = name;
	return devm_regmap_init_mmio(&pdev->dev, base, &llcc_regmap_config);
}

static int qcom_llcc_probe(struct platform_device *pdev)
{
	u32 num_banks;
	struct device *dev = &pdev->dev;
	int ret, i;
	struct platform_device *llcc_edac;
	const struct qcom_sct_config *cfgs;
	const struct qcom_llcc_config *cfg;
	const struct llcc_slice_config *llcc_cfg;
	u32 sz;
	u8 cfg_index;
	u32 version;
	struct regmap *regmap;

	if (!IS_ERR(drv_data))
		return -EBUSY;

	drv_data = devm_kzalloc(dev, sizeof(*drv_data), GFP_KERNEL);
	if (!drv_data) {
		ret = -ENOMEM;
		goto err;
	}

	/* Initialize the first LLCC bank regmap */
	regmap = qcom_llcc_init_mmio(pdev, 0, "llcc0_base");
	if (IS_ERR(regmap)) {
		ret = PTR_ERR(regmap);
		goto err;
	}

	cfgs = of_device_get_match_data(&pdev->dev);
	if (!cfgs) {
		ret = -EINVAL;
		goto err;
	}
	ret = qcom_llcc_get_cfg_index(pdev, &cfg_index, cfgs->num_config);
	if (ret)
		goto err;
	cfg = &cfgs->llcc_config[cfg_index];

	if (cfg->num_banks) {
		num_banks = cfg->num_banks;
	} else {
		ret = regmap_read(regmap, cfg->reg_offset[LLCC_COMMON_STATUS0], &num_banks);
		if (ret)
			goto err;

		num_banks &= LLCC_LB_CNT_MASK;
		num_banks >>= LLCC_LB_CNT_SHIFT;
	}

	drv_data->num_banks = num_banks;

	drv_data->regmaps = devm_kcalloc(dev, num_banks, sizeof(*drv_data->regmaps), GFP_KERNEL);
	if (!drv_data->regmaps) {
		ret = -ENOMEM;
		goto err;
	}

	drv_data->regmaps[0] = regmap;

	/* Initialize rest of LLCC bank regmaps */
	for (i = 1; i < num_banks; i++) {
		char *base __free(kfree) = kasprintf(GFP_KERNEL, "llcc%d_base", i);

		drv_data->regmaps[i] = qcom_llcc_init_mmio(pdev, i, base);
		if (IS_ERR(drv_data->regmaps[i])) {
			ret = PTR_ERR(drv_data->regmaps[i]);
			goto err;
		}
	}

	drv_data->bcast_regmap = qcom_llcc_init_mmio(pdev, i, "llcc_broadcast_base");
	if (IS_ERR(drv_data->bcast_regmap)) {
		if (cfg->no_broadcast_register) {
			drv_data->bcast_regmap = regmap;
		} else {
			ret = PTR_ERR(drv_data->bcast_regmap);
			goto err;
		}
	}

	/* Extract version of the IP */
	ret = regmap_read(drv_data->bcast_regmap, cfg->reg_offset[LLCC_COMMON_HW_INFO],
			  &version);
	if (ret)
		goto err;

	drv_data->version = version;

	/* Applicable only when drv_data->version >= 4.1 */
	if (drv_data->version >= LLCC_VERSION_4_1_0_0) {
		drv_data->bcast_and_regmap = qcom_llcc_init_mmio(pdev, i + 1, "llcc_broadcast_and_base");
		if (IS_ERR(drv_data->bcast_and_regmap)) {
			ret = PTR_ERR(drv_data->bcast_and_regmap);
			if (ret == -EINVAL)
				drv_data->bcast_and_regmap = NULL;
			else
				goto err;
		}
	}

	llcc_cfg = cfg->sct_data;
	sz = cfg->size;

	for (i = 0; i < sz; i++)
		if (llcc_cfg[i].slice_id > drv_data->max_slices)
			drv_data->max_slices = llcc_cfg[i].slice_id;

	drv_data->bitmap = devm_bitmap_zalloc(dev, drv_data->max_slices,
					      GFP_KERNEL);
	if (!drv_data->bitmap) {
		ret = -ENOMEM;
		goto err;
	}

	drv_data->cfg = llcc_cfg;
	drv_data->cfg_size = sz;
	drv_data->edac_reg_offset = cfg->edac_reg_offset;
	drv_data->ecc_irq_configured = cfg->irq_configured;
	mutex_init(&drv_data->lock);
	platform_set_drvdata(pdev, drv_data);

	ret = qcom_llcc_cfg_program(pdev, cfg);
	if (ret)
		goto err;

	drv_data->ecc_irq = platform_get_irq_optional(pdev, 0);

	/*
	 * On some platforms, the access to EDAC registers will be locked by
	 * the bootloader. So probing the EDAC driver will result in a crash.
	 * Hence, disable the creation of EDAC platform device for the
	 * problematic platforms.
	 */
	if (!cfg->no_edac) {
		llcc_edac = platform_device_register_data(&pdev->dev,
						"qcom_llcc_edac", -1, drv_data,
						sizeof(*drv_data));
		if (IS_ERR(llcc_edac))
			dev_err(dev, "Failed to register llcc edac driver\n");
	}

	return 0;
err:
	drv_data = ERR_PTR(-ENODEV);
	return ret;
}

static const struct of_device_id qcom_llcc_of_match[] = {
	{ .compatible = "qcom,ipq5424-llcc", .data = &ipq5424_cfgs},
	{ .compatible = "qcom,qcs615-llcc", .data = &qcs615_cfgs},
	{ .compatible = "qcom,qcs8300-llcc", .data = &qcs8300_cfgs},
	{ .compatible = "qcom,qdu1000-llcc", .data = &qdu1000_cfgs},
	{ .compatible = "qcom,sa8775p-llcc", .data = &sa8775p_cfgs },
	{ .compatible = "qcom,sar1130p-llcc", .data = &sar1130p_cfgs },
	{ .compatible = "qcom,sar2130p-llcc", .data = &sar2130p_cfgs },
	{ .compatible = "qcom,sc7180-llcc", .data = &sc7180_cfgs },
	{ .compatible = "qcom,sc7280-llcc", .data = &sc7280_cfgs },
	{ .compatible = "qcom,sc8180x-llcc", .data = &sc8180x_cfgs },
	{ .compatible = "qcom,sc8280xp-llcc", .data = &sc8280xp_cfgs },
	{ .compatible = "qcom,sdm845-llcc", .data = &sdm845_cfgs },
	{ .compatible = "qcom,sm6350-llcc", .data = &sm6350_cfgs },
	{ .compatible = "qcom,sm7150-llcc", .data = &sm7150_cfgs },
	{ .compatible = "qcom,sm8150-llcc", .data = &sm8150_cfgs },
	{ .compatible = "qcom,sm8250-llcc", .data = &sm8250_cfgs },
	{ .compatible = "qcom,sm8350-llcc", .data = &sm8350_cfgs },
	{ .compatible = "qcom,sm8450-llcc", .data = &sm8450_cfgs },
	{ .compatible = "qcom,sm8550-llcc", .data = &sm8550_cfgs },
	{ .compatible = "qcom,sm8650-llcc", .data = &sm8650_cfgs },
	{ .compatible = "qcom,sm8750-llcc", .data = &sm8750_cfgs },
	{ .compatible = "qcom,x1e80100-llcc", .data = &x1e80100_cfgs },
	{ }
};
MODULE_DEVICE_TABLE(of, qcom_llcc_of_match);

static struct platform_driver qcom_llcc_driver = {
	.driver = {
		.name = "qcom-llcc",
		.of_match_table = qcom_llcc_of_match,
	},
	.probe = qcom_llcc_probe,
	.remove = qcom_llcc_remove,
};
module_platform_driver(qcom_llcc_driver);

MODULE_DESCRIPTION("Qualcomm Last Level Cache Controller");
MODULE_LICENSE("GPL v2");
