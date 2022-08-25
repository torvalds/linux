// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2017-2019, The Linux Foundation. All rights reserved.
 *
 */

#include <linux/bitfield.h>
#include <linux/bitmap.h>
#include <linux/bitops.h>
#include <linux/device.h>
#include <linux/io.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/regmap.h>
#include <linux/sizes.h>
#include <linux/slab.h>
#include <linux/soc/qcom/llcc-qcom.h>

#define ACTIVATE                      BIT(0)
#define DEACTIVATE                    BIT(1)
#define ACT_CTRL_OPCODE_ACTIVATE      BIT(0)
#define ACT_CTRL_OPCODE_DEACTIVATE    BIT(1)
#define ACT_CTRL_ACT_TRIG             BIT(0)
#define ACT_CTRL_OPCODE_SHIFT         0x01
#define ATTR1_PROBE_TARGET_WAYS_SHIFT 0x02
#define ATTR1_FIXED_SIZE_SHIFT        0x03
#define ATTR1_PRIORITY_SHIFT          0x04
#define ATTR1_MAX_CAP_SHIFT           0x10
#define ATTR0_RES_WAYS_MASK           GENMASK(15, 0)
#define ATTR0_BONUS_WAYS_MASK         GENMASK(31, 16)
#define ATTR0_BONUS_WAYS_SHIFT        0x10
#define LLCC_STATUS_READ_DELAY        100

#define CACHE_LINE_SIZE_SHIFT         6

#define LLCC_LB_CNT_MASK              GENMASK(31, 28)
#define LLCC_LB_CNT_SHIFT             28

#define MAX_CAP_TO_BYTES(n)           (n * SZ_1K)
#define LLCC_TRP_ACT_CTRLn(n)         (n * SZ_4K)
#define LLCC_TRP_STATUSn(n)           (4 + n * SZ_4K)
#define LLCC_TRP_ATTR0_CFGn(n)        (0x21000 + SZ_8 * n)
#define LLCC_TRP_ATTR1_CFGn(n)        (0x21004 + SZ_8 * n)

#define LLCC_TRP_SCID_DIS_CAP_ALLOC   0x21f00
#define LLCC_TRP_PCB_ACT              0x21f04
#define LLCC_TRP_WRSC_EN              0x21f20
#define LLCC_TRP_WRSC_CACHEABLE_EN    0x21f2c

#define BANK_OFFSET_STRIDE	      0x80000

#define LLCC_VERSION_2_0_0_0          0x02000000
#define LLCC_VERSION_2_1_0_0          0x02010000

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
};

struct qcom_llcc_config {
	const struct llcc_slice_config *sct_data;
	int size;
	bool need_llcc_cfg;
	const u32 *reg_offset;
	const struct llcc_edac_reg_offset *edac_reg_offset;
};

enum llcc_reg_offset {
	LLCC_COMMON_HW_INFO,
	LLCC_COMMON_STATUS0,
};

static const struct llcc_slice_config sc7180_data[] =  {
	{ LLCC_CPUSS,    1,  256, 1, 0, 0xf, 0x0, 0, 0, 0, 1, 1 },
	{ LLCC_MDM,      8,  128, 1, 0, 0xf, 0x0, 0, 0, 0, 1, 0 },
	{ LLCC_GPUHTW,   11, 128, 1, 0, 0xf, 0x0, 0, 0, 0, 1, 0 },
	{ LLCC_GPU,      12, 128, 1, 0, 0xf, 0x0, 0, 0, 0, 1, 0 },
};

static const struct llcc_slice_config sc7280_data[] =  {
	{ LLCC_CPUSS,    1,  768, 1, 0, 0x3f, 0x0, 0, 0, 0, 1, 1, 0},
	{ LLCC_MDMHPGRW, 7,  512, 2, 1, 0x3f, 0x0, 0, 0, 0, 1, 0, 0},
	{ LLCC_CMPT,     10, 768, 1, 1, 0x3f, 0x0, 0, 0, 0, 1, 0, 0},
	{ LLCC_GPUHTW,   11, 256, 1, 1, 0x3f, 0x0, 0, 0, 0, 1, 0, 0},
	{ LLCC_GPU,      12, 512, 1, 0, 0x3f, 0x0, 0, 0, 0, 1, 0, 0},
	{ LLCC_MMUHWT,   13, 256, 1, 1, 0x3f, 0x0, 0, 0, 0, 0, 1, 0},
	{ LLCC_MDMPNG,   21, 768, 0, 1, 0x3f, 0x0, 0, 0, 0, 1, 0, 0},
	{ LLCC_WLHW,     24, 256, 1, 1, 0x3f, 0x0, 0, 0, 0, 1, 0, 0},
	{ LLCC_MODPE,    29, 64,  1, 1, 0x3f, 0x0, 0, 0, 0, 1, 0, 0},
};

static const struct llcc_slice_config sc8180x_data[] = {
	{ LLCC_CPUSS,    1, 6144,  1, 1, 0xfff, 0x0,   0, 0, 0, 1, 1 },
	{ LLCC_VIDSC0,   2, 512,   2, 1, 0xfff, 0x0,   0, 0, 0, 1, 0 },
	{ LLCC_VIDSC1,   3, 512,   2, 1, 0xfff, 0x0,   0, 0, 0, 1, 0 },
	{ LLCC_AUDIO,    6, 1024,  1, 1, 0xfff, 0x0,   0, 0, 0, 1, 0 },
	{ LLCC_MDMHPGRW, 7, 3072,  1, 1, 0x3ff, 0xc00, 0, 0, 0, 1, 0 },
	{ LLCC_MDM,      8, 3072,  1, 1, 0xfff, 0x0,   0, 0, 0, 1, 0 },
	{ LLCC_MODHW,    9, 1024,  1, 1, 0xfff, 0x0,   0, 0, 0, 1, 0 },
	{ LLCC_CMPT,     10, 6144, 1, 1, 0xfff, 0x0,   0, 0, 0, 1, 0 },
	{ LLCC_GPUHTW,   11, 1024, 1, 1, 0xfff, 0x0,   0, 0, 0, 1, 0 },
	{ LLCC_GPU,      12, 5120, 1, 1, 0xfff, 0x0,   0, 0, 0, 1, 0 },
	{ LLCC_MMUHWT,   13, 1024, 1, 1, 0xfff, 0x0,   0, 0, 0, 0, 1 },
	{ LLCC_CMPTDMA,  15, 6144, 1, 1, 0xfff, 0x0,   0, 0, 0, 1, 0 },
	{ LLCC_DISP,     16, 6144, 1, 1, 0xfff, 0x0,   0, 0, 0, 1, 0 },
	{ LLCC_VIDFW,    17, 1024, 1, 1, 0xfff, 0x0,   0, 0, 0, 1, 0 },
	{ LLCC_MDMHPFX,  20, 1024, 2, 1, 0xfff, 0x0,   0, 0, 0, 1, 0 },
	{ LLCC_MDMPNG,   21, 1024, 0, 1, 0xc,   0x0,   0, 0, 0, 1, 0 },
	{ LLCC_AUDHW,    22, 1024, 1, 1, 0xfff, 0x0,   0, 0, 0, 1, 0 },
	{ LLCC_NPU,      23, 6144, 1, 1, 0xfff, 0x0,   0, 0, 0, 1, 0 },
	{ LLCC_WLHW,     24, 6144, 1, 1, 0xfff, 0x0,   0, 0, 0, 1, 0 },
	{ LLCC_MODPE,    29, 512,  1, 1, 0xc,   0x0,   0, 0, 0, 1, 0 },
	{ LLCC_APTCM,    30, 512,  3, 1, 0x0,   0x1,   1, 0, 0, 1, 0 },
	{ LLCC_WRCACHE,  31, 128,  1, 1, 0xfff, 0x0,   0, 0, 0, 0, 0 },
};

static const struct llcc_slice_config sc8280xp_data[] = {
	{ LLCC_CPUSS,    1,  6144, 1, 1, 0xfff, 0x0, 0, 0, 0, 1, 1, 0 },
	{ LLCC_VIDSC0,   2,  512,  3, 1, 0xfff, 0x0, 0, 0, 0, 1, 0, 0 },
	{ LLCC_AUDIO,    6,  1024, 1, 1, 0xfff, 0x0, 0, 0, 0, 0, 0, 0 },
	{ LLCC_CMPT,     10, 6144, 1, 1, 0xfff, 0x0, 0, 0, 0, 0, 0, 0 },
	{ LLCC_GPUHTW,   11, 1024, 1, 1, 0xfff, 0x0, 0, 0, 0, 1, 0, 0 },
	{ LLCC_GPU,      12, 4096, 1, 1, 0xfff, 0x0, 0, 0, 0, 1, 0, 1 },
	{ LLCC_MMUHWT,   13, 1024, 1, 1, 0xfff, 0x0, 0, 0, 0, 0, 1, 0 },
	{ LLCC_DISP,     16, 6144, 1, 1, 0xfff, 0x0, 0, 0, 0, 1, 0, 0 },
	{ LLCC_AUDHW,    22, 2048, 1, 1, 0xfff, 0x0, 0, 0, 0, 1, 0, 0 },
	{ LLCC_DRE,      26, 1024, 1, 1, 0xfff, 0x0, 0, 0, 0, 1, 0, 0 },
	{ LLCC_CVP,      28, 512,  3, 1, 0xfff, 0x0, 0, 0, 0, 1, 0, 0 },
	{ LLCC_APTCM,    30, 1024, 3, 1, 0x0,   0x1, 1, 0, 0, 1, 0, 0 },
	{ LLCC_WRCACHE,  31, 1024, 1, 1, 0xfff, 0x0, 0, 0, 0, 0, 1, 0 },
	{ LLCC_CVPFW,    32, 512,  1, 0, 0xfff, 0x0, 0, 0, 0, 1, 0, 0 },
	{ LLCC_CPUSS1,   33, 2048, 1, 1, 0xfff, 0x0, 0, 0, 0, 1, 0, 0 },
	{ LLCC_CPUHWT,   36, 512,  1, 1, 0xfff, 0x0, 0, 0, 0, 0, 1, 0 },
};

static const struct llcc_slice_config sdm845_data[] =  {
	{ LLCC_CPUSS,    1,  2816, 1, 0, 0xffc, 0x2,   0, 0, 1, 1, 1 },
	{ LLCC_VIDSC0,   2,  512,  2, 1, 0x0,   0x0f0, 0, 0, 1, 1, 0 },
	{ LLCC_VIDSC1,   3,  512,  2, 1, 0x0,   0x0f0, 0, 0, 1, 1, 0 },
	{ LLCC_ROTATOR,  4,  563,  2, 1, 0x0,   0x00e, 2, 0, 1, 1, 0 },
	{ LLCC_VOICE,    5,  2816, 1, 0, 0xffc, 0x2,   0, 0, 1, 1, 0 },
	{ LLCC_AUDIO,    6,  2816, 1, 0, 0xffc, 0x2,   0, 0, 1, 1, 0 },
	{ LLCC_MDMHPGRW, 7,  1024, 2, 0, 0xfc,  0xf00, 0, 0, 1, 1, 0 },
	{ LLCC_MDM,      8,  2816, 1, 0, 0xffc, 0x2,   0, 0, 1, 1, 0 },
	{ LLCC_CMPT,     10, 2816, 1, 0, 0xffc, 0x2,   0, 0, 1, 1, 0 },
	{ LLCC_GPUHTW,   11, 512,  1, 1, 0xc,   0x0,   0, 0, 1, 1, 0 },
	{ LLCC_GPU,      12, 2304, 1, 0, 0xff0, 0x2,   0, 0, 1, 1, 0 },
	{ LLCC_MMUHWT,   13, 256,  2, 0, 0x0,   0x1,   0, 0, 1, 0, 1 },
	{ LLCC_CMPTDMA,  15, 2816, 1, 0, 0xffc, 0x2,   0, 0, 1, 1, 0 },
	{ LLCC_DISP,     16, 2816, 1, 0, 0xffc, 0x2,   0, 0, 1, 1, 0 },
	{ LLCC_VIDFW,    17, 2816, 1, 0, 0xffc, 0x2,   0, 0, 1, 1, 0 },
	{ LLCC_MDMHPFX,  20, 1024, 2, 1, 0x0,   0xf00, 0, 0, 1, 1, 0 },
	{ LLCC_MDMPNG,   21, 1024, 0, 1, 0x1e,  0x0,   0, 0, 1, 1, 0 },
	{ LLCC_AUDHW,    22, 1024, 1, 1, 0xffc, 0x2,   0, 0, 1, 1, 0 },
};

static const struct llcc_slice_config sm6350_data[] =  {
	{ LLCC_CPUSS,    1,  768, 1, 0, 0xFFF, 0x0, 0, 0, 0, 0, 1, 1 },
	{ LLCC_MDM,      8,  512, 2, 0, 0xFFF, 0x0, 0, 0, 0, 0, 1, 0 },
	{ LLCC_GPUHTW,   11, 256, 1, 0, 0xFFF, 0x0, 0, 0, 0, 0, 1, 0 },
	{ LLCC_GPU,      12, 512, 1, 0, 0xFFF, 0x0, 0, 0, 0, 0, 1, 0 },
	{ LLCC_MDMPNG,   21, 768, 0, 1, 0xFFF, 0x0, 0, 0, 0, 0, 1, 0 },
	{ LLCC_NPU,      23, 768, 1, 0, 0xFFF, 0x0, 0, 0, 0, 0, 1, 0 },
	{ LLCC_MODPE,    29,  64, 1, 1, 0xFFF, 0x0, 0, 0, 0, 0, 1, 0 },
};

static const struct llcc_slice_config sm8150_data[] =  {
	{  LLCC_CPUSS,    1, 3072, 1, 1, 0xFFF, 0x0,   0, 0, 0, 1, 1 },
	{  LLCC_VIDSC0,   2, 512,  2, 1, 0xFFF, 0x0,   0, 0, 0, 1, 0 },
	{  LLCC_VIDSC1,   3, 512,  2, 1, 0xFFF, 0x0,   0, 0, 0, 1, 0 },
	{  LLCC_AUDIO,    6, 1024, 1, 1, 0xFFF, 0x0,   0, 0, 0, 1, 0 },
	{  LLCC_MDMHPGRW, 7, 3072, 1, 0, 0xFF,  0xF00, 0, 0, 0, 1, 0 },
	{  LLCC_MDM,      8, 3072, 1, 1, 0xFFF, 0x0,   0, 0, 0, 1, 0 },
	{  LLCC_MODHW,    9, 1024, 1, 1, 0xFFF, 0x0,   0, 0, 0, 1, 0 },
	{  LLCC_CMPT,    10, 3072, 1, 1, 0xFFF, 0x0,   0, 0, 0, 1, 0 },
	{  LLCC_GPUHTW , 11, 512,  1, 1, 0xFFF, 0x0,   0, 0, 0, 1, 0 },
	{  LLCC_GPU,     12, 2560, 1, 1, 0xFFF, 0x0,   0, 0, 0, 1, 0 },
	{  LLCC_MMUHWT,  13, 1024, 1, 1, 0xFFF, 0x0,   0, 0, 0, 0, 1 },
	{  LLCC_CMPTDMA, 15, 3072, 1, 1, 0xFFF, 0x0,   0, 0, 0, 1, 0 },
	{  LLCC_DISP,    16, 3072, 1, 1, 0xFFF, 0x0,   0, 0, 0, 1, 0 },
	{  LLCC_MDMHPFX, 20, 1024, 2, 1, 0xFFF, 0x0,   0, 0, 0, 1, 0 },
	{  LLCC_MDMHPFX, 21, 1024, 0, 1, 0xF,   0x0,   0, 0, 0, 1, 0 },
	{  LLCC_AUDHW,   22, 1024, 1, 1, 0xFFF, 0x0,   0, 0, 0, 1, 0 },
	{  LLCC_NPU,     23, 3072, 1, 1, 0xFFF, 0x0,   0, 0, 0, 1, 0 },
	{  LLCC_WLHW,    24, 3072, 1, 1, 0xFFF, 0x0,   0, 0, 0, 1, 0 },
	{  LLCC_MODPE,   29, 256,  1, 1, 0xF,   0x0,   0, 0, 0, 1, 0 },
	{  LLCC_APTCM,   30, 256,  3, 1, 0x0,   0x1,   1, 0, 0, 1, 0 },
	{  LLCC_WRCACHE, 31, 128,  1, 1, 0xFFF, 0x0,   0, 0, 0, 0, 0 },
};

static const struct llcc_slice_config sm8250_data[] =  {
	{ LLCC_CPUSS,    1, 3072, 1, 1, 0xfff, 0x0, 0, 0, 0, 1, 1, 0 },
	{ LLCC_VIDSC0,   2, 512,  3, 1, 0xfff, 0x0, 0, 0, 0, 1, 0, 0 },
	{ LLCC_AUDIO,    6, 1024, 1, 0, 0xfff, 0x0, 0, 0, 0, 0, 0, 0 },
	{ LLCC_CMPT,    10, 1024, 1, 0, 0xfff, 0x0, 0, 0, 0, 0, 0, 0 },
	{ LLCC_GPUHTW,  11, 1024, 1, 1, 0xfff, 0x0, 0, 0, 0, 1, 0, 0 },
	{ LLCC_GPU,     12, 1024, 1, 0, 0xfff, 0x0, 0, 0, 0, 1, 0, 1 },
	{ LLCC_MMUHWT,  13, 1024, 1, 1, 0xfff, 0x0, 0, 0, 0, 0, 1, 0 },
	{ LLCC_CMPTDMA, 15, 1024, 1, 0, 0xfff, 0x0, 0, 0, 0, 1, 0, 0 },
	{ LLCC_DISP,    16, 3072, 1, 1, 0xfff, 0x0, 0, 0, 0, 1, 0, 0 },
	{ LLCC_VIDFW,   17, 512,  1, 0, 0xfff, 0x0, 0, 0, 0, 1, 0, 0 },
	{ LLCC_AUDHW,   22, 1024, 1, 1, 0xfff, 0x0, 0, 0, 0, 1, 0, 0 },
	{ LLCC_NPU,     23, 3072, 1, 1, 0xfff, 0x0, 0, 0, 0, 1, 0, 0 },
	{ LLCC_WLHW,    24, 1024, 1, 0, 0xfff, 0x0, 0, 0, 0, 1, 0, 0 },
	{ LLCC_CVP,     28, 256,  3, 1, 0xfff, 0x0, 0, 0, 0, 1, 0, 0 },
	{ LLCC_APTCM,   30, 128,  3, 0, 0x0,   0x3, 1, 0, 0, 1, 0, 0 },
	{ LLCC_WRCACHE, 31, 256,  1, 1, 0xfff, 0x0, 0, 0, 0, 0, 1, 0 },
};

static const struct llcc_slice_config sm8350_data[] =  {
	{ LLCC_CPUSS,    1, 3072,  1, 1, 0xfff, 0x0, 0, 0, 0, 0, 1, 1 },
	{ LLCC_VIDSC0,   2, 512,   3, 1, 0xfff, 0x0, 0, 0, 0, 0, 1, 0 },
	{ LLCC_AUDIO,    6, 1024,  1, 1, 0xfff, 0x0, 0, 0, 0, 0, 0, 0 },
	{ LLCC_MDMHPGRW, 7, 1024,  3, 0, 0xfff, 0x0, 0, 0, 0, 0, 1, 0 },
	{ LLCC_MODHW,    9, 1024,  1, 1, 0xfff, 0x0, 0, 0, 0, 0, 1, 0 },
	{ LLCC_CMPT,     10, 3072, 1, 1, 0xfff, 0x0, 0, 0, 0, 0, 1, 0 },
	{ LLCC_GPUHTW,   11, 1024, 1, 1, 0xfff, 0x0, 0, 0, 0, 0, 1, 0 },
	{ LLCC_GPU,      12, 1024, 1, 0, 0xfff, 0x0, 0, 0, 0, 1, 1, 0 },
	{ LLCC_MMUHWT,   13, 1024, 1, 1, 0xfff, 0x0, 0, 0, 0, 0, 0, 1 },
	{ LLCC_DISP,     16, 3072, 2, 1, 0xfff, 0x0, 0, 0, 0, 0, 1, 0 },
	{ LLCC_MDMPNG,   21, 1024, 0, 1, 0xf,   0x0, 0, 0, 0, 0, 1, 0 },
	{ LLCC_AUDHW,    22, 1024, 1, 1, 0xfff, 0x0, 0, 0, 0, 0, 1, 0 },
	{ LLCC_CVP,      28, 512,  3, 1, 0xfff, 0x0, 0, 0, 0, 0, 1, 0 },
	{ LLCC_MODPE,    29, 256,  1, 1, 0xf,   0x0, 0, 0, 0, 0, 1, 0 },
	{ LLCC_APTCM,    30, 1024, 3, 1, 0x0,   0x1, 1, 0, 0, 0, 1, 0 },
	{ LLCC_WRCACHE,  31, 512,  1, 1, 0xfff, 0x0, 0, 0, 0, 0, 0, 1 },
	{ LLCC_CVPFW,    17, 512,  1, 0, 0xfff, 0x0, 0, 0, 0, 0, 1, 0 },
	{ LLCC_CPUSS1,   3, 1024,  1, 1, 0xfff, 0x0, 0, 0, 0, 0, 1, 0 },
	{ LLCC_CPUHWT,   5, 512,   1, 1, 0xfff, 0x0, 0, 0, 0, 0, 0, 1 },
};

static const struct llcc_slice_config sm8450_data[] =  {
	{LLCC_CPUSS,     1, 3072, 1, 0, 0xFFFF, 0x0,   0, 0, 0, 1, 1, 0, 0 },
	{LLCC_VIDSC0,    2,  512, 3, 1, 0xFFFF, 0x0,   0, 0, 0, 1, 0, 0, 0 },
	{LLCC_AUDIO,     6, 1024, 1, 1, 0xFFFF, 0x0,   0, 0, 0, 0, 0, 0, 0 },
	{LLCC_MDMHPGRW,  7, 1024, 3, 0, 0xFFFF, 0x0,   0, 0, 0, 1, 0, 0, 0 },
	{LLCC_MODHW,     9, 1024, 1, 1, 0xFFFF, 0x0,   0, 0, 0, 1, 0, 0, 0 },
	{LLCC_CMPT,     10, 4096, 1, 1, 0xFFFF, 0x0,   0, 0, 0, 1, 0, 0, 0 },
	{LLCC_GPUHTW,   11,  512, 1, 1, 0xFFFF, 0x0,   0, 0, 0, 1, 0, 0, 0 },
	{LLCC_GPU,      12, 2048, 1, 1, 0xFFFF, 0x0,   0, 0, 0, 1, 0, 1, 0 },
	{LLCC_MMUHWT,   13,  768, 1, 1, 0xFFFF, 0x0,   0, 0, 0, 0, 1, 0, 0 },
	{LLCC_DISP,     16, 4096, 2, 1, 0xFFFF, 0x0,   0, 0, 0, 1, 0, 0, 0 },
	{LLCC_MDMPNG,   21, 1024, 1, 1, 0xF000, 0x0,   0, 0, 0, 1, 0, 0, 0 },
	{LLCC_AUDHW,    22, 1024, 1, 1, 0xFFFF, 0x0,   0, 0, 0, 0, 0, 0, 0 },
	{LLCC_CVP,      28,  256, 3, 1, 0xFFFF, 0x0,   0, 0, 0, 1, 0, 0, 0 },
	{LLCC_MODPE,    29,   64, 1, 1, 0xF000, 0x0,   0, 0, 0, 1, 0, 0, 0 },
	{LLCC_APTCM,    30, 1024, 3, 1, 0x0,    0xF0,  1, 0, 0, 1, 0, 0, 0 },
	{LLCC_WRCACHE,  31,  512, 1, 1, 0xFFFF, 0x0,   0, 0, 0, 0, 1, 0, 0 },
	{LLCC_CVPFW,    17,  512, 1, 1, 0xFFFF, 0x0,   0, 0, 0, 1, 0, 0, 0 },
	{LLCC_CPUSS1,    3, 1024, 1, 1, 0xFFFF, 0x0,   0, 0, 0, 1, 0, 0, 0 },
	{LLCC_CAMEXP0,   4,  256, 3, 1, 0xFFFF, 0x0,   0, 0, 0, 1, 0, 0, 0 },
	{LLCC_CPUMTE,   23,  256, 1, 1, 0x0FFF, 0x0,   0, 0, 0, 0, 1, 0, 0 },
	{LLCC_CPUHWT,    5,  512, 1, 1, 0xFFFF, 0x0,   0, 0, 0, 1, 1, 0, 0 },
	{LLCC_CAMEXP1,  27,  256, 3, 1, 0xFFFF, 0x0,   0, 0, 0, 1, 0, 0, 0 },
	{LLCC_AENPU,     8, 2048, 1, 1, 0xFFFF, 0x0,   0, 0, 0, 0, 0, 0, 0 },
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

static const struct qcom_llcc_config sc7180_cfg = {
	.sct_data	= sc7180_data,
	.size		= ARRAY_SIZE(sc7180_data),
	.need_llcc_cfg	= true,
	.reg_offset	= llcc_v1_reg_offset,
	.edac_reg_offset = &llcc_v1_edac_reg_offset,
};

static const struct qcom_llcc_config sc7280_cfg = {
	.sct_data	= sc7280_data,
	.size		= ARRAY_SIZE(sc7280_data),
	.need_llcc_cfg	= true,
	.reg_offset	= llcc_v1_reg_offset,
	.edac_reg_offset = &llcc_v1_edac_reg_offset,
};

static const struct qcom_llcc_config sc8180x_cfg = {
	.sct_data	= sc8180x_data,
	.size		= ARRAY_SIZE(sc8180x_data),
	.need_llcc_cfg	= true,
	.reg_offset	= llcc_v1_reg_offset,
	.edac_reg_offset = &llcc_v1_edac_reg_offset,
};

static const struct qcom_llcc_config sc8280xp_cfg = {
	.sct_data	= sc8280xp_data,
	.size		= ARRAY_SIZE(sc8280xp_data),
	.need_llcc_cfg	= true,
	.reg_offset	= llcc_v1_reg_offset,
	.edac_reg_offset = &llcc_v1_edac_reg_offset,
};

static const struct qcom_llcc_config sdm845_cfg = {
	.sct_data	= sdm845_data,
	.size		= ARRAY_SIZE(sdm845_data),
	.need_llcc_cfg	= false,
	.reg_offset	= llcc_v1_reg_offset,
	.edac_reg_offset = &llcc_v1_edac_reg_offset,
};

static const struct qcom_llcc_config sm6350_cfg = {
	.sct_data	= sm6350_data,
	.size		= ARRAY_SIZE(sm6350_data),
	.need_llcc_cfg	= true,
	.reg_offset	= llcc_v1_reg_offset,
	.edac_reg_offset = &llcc_v1_edac_reg_offset,
};

static const struct qcom_llcc_config sm8150_cfg = {
	.sct_data       = sm8150_data,
	.size           = ARRAY_SIZE(sm8150_data),
	.need_llcc_cfg	= true,
	.reg_offset	= llcc_v1_reg_offset,
	.edac_reg_offset = &llcc_v1_edac_reg_offset,
};

static const struct qcom_llcc_config sm8250_cfg = {
	.sct_data       = sm8250_data,
	.size           = ARRAY_SIZE(sm8250_data),
	.need_llcc_cfg	= true,
	.reg_offset	= llcc_v1_reg_offset,
	.edac_reg_offset = &llcc_v1_edac_reg_offset,
};

static const struct qcom_llcc_config sm8350_cfg = {
	.sct_data       = sm8350_data,
	.size           = ARRAY_SIZE(sm8350_data),
	.need_llcc_cfg	= true,
	.reg_offset	= llcc_v1_reg_offset,
	.edac_reg_offset = &llcc_v1_edac_reg_offset,
};

static const struct qcom_llcc_config sm8450_cfg = {
	.sct_data       = sm8450_data,
	.size           = ARRAY_SIZE(sm8450_data),
	.need_llcc_cfg	= true,
	.reg_offset	= llcc_v2_1_reg_offset,
	.edac_reg_offset = &llcc_v2_1_edac_reg_offset,
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
 * llcc_slice_putd - llcc slice descritpor
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
	u32 act_ctrl_reg;
	u32 status_reg;
	u32 slice_status;
	int ret;

	if (IS_ERR(drv_data))
		return PTR_ERR(drv_data);

	act_ctrl_reg = LLCC_TRP_ACT_CTRLn(sid);
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

	ret = regmap_read_poll_timeout(drv_data->bcast_regmap, status_reg,
				      slice_status, !(slice_status & status),
				      0, LLCC_STATUS_READ_DELAY);
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
	u32 attr1_cfg;
	u32 attr0_cfg;
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
	attr1_val |= max_cap_cacheline << ATTR1_MAX_CAP_SHIFT;

	attr1_cfg = LLCC_TRP_ATTR1_CFGn(config->slice_id);

	ret = regmap_write(drv_data->bcast_regmap, attr1_cfg, attr1_val);
	if (ret)
		return ret;

	attr0_val = config->res_ways & ATTR0_RES_WAYS_MASK;
	attr0_val |= config->bonus_ways << ATTR0_BONUS_WAYS_SHIFT;

	attr0_cfg = LLCC_TRP_ATTR0_CFGn(config->slice_id);

	ret = regmap_write(drv_data->bcast_regmap, attr0_cfg, attr0_val);
	if (ret)
		return ret;

	if (cfg->need_llcc_cfg) {
		u32 disable_cap_alloc, retain_pc;

		disable_cap_alloc = config->dis_cap_alloc << config->slice_id;
		ret = regmap_write(drv_data->bcast_regmap,
				LLCC_TRP_SCID_DIS_CAP_ALLOC, disable_cap_alloc);
		if (ret)
			return ret;

		retain_pc = config->retain_on_pc << config->slice_id;
		ret = regmap_write(drv_data->bcast_regmap,
				LLCC_TRP_PCB_ACT, retain_pc);
		if (ret)
			return ret;
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

	if (config->activate_on_init) {
		desc.slice_id = config->slice_id;
		ret = llcc_slice_activate(&desc);
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

	for (i = 0; i < sz; i++) {
		ret = _qcom_llcc_cfg_program(&llcc_table[i], cfg);
		if (ret)
			return ret;
	}

	return ret;
}

static int qcom_llcc_remove(struct platform_device *pdev)
{
	/* Set the global pointer to a error code to avoid referencing it */
	drv_data = ERR_PTR(-ENODEV);
	return 0;
}

static struct regmap *qcom_llcc_init_mmio(struct platform_device *pdev,
		const char *name)
{
	void __iomem *base;
	struct regmap_config llcc_regmap_config = {
		.reg_bits = 32,
		.reg_stride = 4,
		.val_bits = 32,
		.fast_io = true,
	};

	base = devm_platform_ioremap_resource_byname(pdev, name);
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
	const struct qcom_llcc_config *cfg;
	const struct llcc_slice_config *llcc_cfg;
	u32 sz;
	u32 version;

	drv_data = devm_kzalloc(dev, sizeof(*drv_data), GFP_KERNEL);
	if (!drv_data) {
		ret = -ENOMEM;
		goto err;
	}

	drv_data->regmap = qcom_llcc_init_mmio(pdev, "llcc_base");
	if (IS_ERR(drv_data->regmap)) {
		ret = PTR_ERR(drv_data->regmap);
		goto err;
	}

	drv_data->bcast_regmap =
		qcom_llcc_init_mmio(pdev, "llcc_broadcast_base");
	if (IS_ERR(drv_data->bcast_regmap)) {
		ret = PTR_ERR(drv_data->bcast_regmap);
		goto err;
	}

	cfg = of_device_get_match_data(&pdev->dev);

	/* Extract version of the IP */
	ret = regmap_read(drv_data->bcast_regmap, cfg->reg_offset[LLCC_COMMON_HW_INFO],
			  &version);
	if (ret)
		goto err;

	drv_data->version = version;

	ret = regmap_read(drv_data->regmap, cfg->reg_offset[LLCC_COMMON_STATUS0],
			  &num_banks);
	if (ret)
		goto err;

	num_banks &= LLCC_LB_CNT_MASK;
	num_banks >>= LLCC_LB_CNT_SHIFT;
	drv_data->num_banks = num_banks;

	llcc_cfg = cfg->sct_data;
	sz = cfg->size;

	for (i = 0; i < sz; i++)
		if (llcc_cfg[i].slice_id > drv_data->max_slices)
			drv_data->max_slices = llcc_cfg[i].slice_id;

	drv_data->offsets = devm_kcalloc(dev, num_banks, sizeof(u32),
							GFP_KERNEL);
	if (!drv_data->offsets) {
		ret = -ENOMEM;
		goto err;
	}

	for (i = 0; i < num_banks; i++)
		drv_data->offsets[i] = i * BANK_OFFSET_STRIDE;

	drv_data->bitmap = devm_bitmap_zalloc(dev, drv_data->max_slices,
					      GFP_KERNEL);
	if (!drv_data->bitmap) {
		ret = -ENOMEM;
		goto err;
	}

	drv_data->cfg = llcc_cfg;
	drv_data->cfg_size = sz;
	drv_data->edac_reg_offset = cfg->edac_reg_offset;
	mutex_init(&drv_data->lock);
	platform_set_drvdata(pdev, drv_data);

	ret = qcom_llcc_cfg_program(pdev, cfg);
	if (ret)
		goto err;

	drv_data->ecc_irq = platform_get_irq(pdev, 0);
	if (drv_data->ecc_irq >= 0) {
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
	{ .compatible = "qcom,sc7180-llcc", .data = &sc7180_cfg },
	{ .compatible = "qcom,sc7280-llcc", .data = &sc7280_cfg },
	{ .compatible = "qcom,sc8180x-llcc", .data = &sc8180x_cfg },
	{ .compatible = "qcom,sc8280xp-llcc", .data = &sc8280xp_cfg },
	{ .compatible = "qcom,sdm845-llcc", .data = &sdm845_cfg },
	{ .compatible = "qcom,sm6350-llcc", .data = &sm6350_cfg },
	{ .compatible = "qcom,sm8150-llcc", .data = &sm8150_cfg },
	{ .compatible = "qcom,sm8250-llcc", .data = &sm8250_cfg },
	{ .compatible = "qcom,sm8350-llcc", .data = &sm8350_cfg },
	{ .compatible = "qcom,sm8450-llcc", .data = &sm8450_cfg },
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
