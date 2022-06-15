/*
 * Copyright 2020 Advanced Micro Devices, Inc.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE COPYRIGHT HOLDER(S) OR AUTHOR(S) BE LIABLE FOR ANY CLAIM, DAMAGES OR
 * OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
 * ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 * OTHER DEALINGS IN THE SOFTWARE.
 *
 */
#include "amdgpu.h"
#include "soc15.h"
#include "soc15d.h"

#include "gc/gc_9_4_2_offset.h"
#include "gc/gc_9_4_2_sh_mask.h"
#include "gfx_v9_0.h"

#include "gfx_v9_4_2.h"
#include "amdgpu_ras.h"
#include "amdgpu_gfx.h"

#define SE_ID_MAX 8
#define CU_ID_MAX 16
#define SIMD_ID_MAX 4
#define WAVE_ID_MAX 10

enum gfx_v9_4_2_utc_type {
	VML2_MEM,
	VML2_WALKER_MEM,
	UTCL2_MEM,
	ATC_L2_CACHE_2M,
	ATC_L2_CACHE_32K,
	ATC_L2_CACHE_4K
};

struct gfx_v9_4_2_utc_block {
	enum gfx_v9_4_2_utc_type type;
	uint32_t num_banks;
	uint32_t num_ways;
	uint32_t num_mem_blocks;
	struct soc15_reg idx_reg;
	struct soc15_reg data_reg;
	uint32_t sec_count_mask;
	uint32_t sec_count_shift;
	uint32_t ded_count_mask;
	uint32_t ded_count_shift;
	uint32_t clear;
};

static const struct soc15_reg_golden golden_settings_gc_9_4_2_alde_die_0[] = {
	SOC15_REG_GOLDEN_VALUE(GC, 0, regTCP_CHAN_STEER_0, 0x3fffffff, 0x141dc920),
	SOC15_REG_GOLDEN_VALUE(GC, 0, regTCP_CHAN_STEER_1, 0x3fffffff, 0x3b458b93),
	SOC15_REG_GOLDEN_VALUE(GC, 0, regTCP_CHAN_STEER_2, 0x3fffffff, 0x1a4f5583),
	SOC15_REG_GOLDEN_VALUE(GC, 0, regTCP_CHAN_STEER_3, 0x3fffffff, 0x317717f6),
	SOC15_REG_GOLDEN_VALUE(GC, 0, regTCP_CHAN_STEER_4, 0x3fffffff, 0x107cc1e6),
	SOC15_REG_GOLDEN_VALUE(GC, 0, regTCP_CHAN_STEER_5, 0x3ff, 0x351),
};

static const struct soc15_reg_golden golden_settings_gc_9_4_2_alde_die_1[] = {
	SOC15_REG_GOLDEN_VALUE(GC, 0, regTCP_CHAN_STEER_0, 0x3fffffff, 0x2591aa38),
	SOC15_REG_GOLDEN_VALUE(GC, 0, regTCP_CHAN_STEER_1, 0x3fffffff, 0xac9e88b),
	SOC15_REG_GOLDEN_VALUE(GC, 0, regTCP_CHAN_STEER_2, 0x3fffffff, 0x2bc3369b),
	SOC15_REG_GOLDEN_VALUE(GC, 0, regTCP_CHAN_STEER_3, 0x3fffffff, 0xfb74ee),
	SOC15_REG_GOLDEN_VALUE(GC, 0, regTCP_CHAN_STEER_4, 0x3fffffff, 0x21f0a2fe),
	SOC15_REG_GOLDEN_VALUE(GC, 0, regTCP_CHAN_STEER_5, 0x3ff, 0x49),
};

static const struct soc15_reg_golden golden_settings_gc_9_4_2_alde[] = {
	SOC15_REG_GOLDEN_VALUE(GC, 0, regGB_ADDR_CONFIG, 0xffff77ff, 0x2a114042),
	SOC15_REG_GOLDEN_VALUE(GC, 0, regTA_CNTL_AUX, 0xfffffeef, 0x10b0000),
	SOC15_REG_GOLDEN_VALUE(GC, 0, regTCP_UTCL1_CNTL1, 0xffffffff, 0x30800400),
	SOC15_REG_GOLDEN_VALUE(GC, 0, regTCI_CNTL_3, 0xff, 0x20),
};

/*
 * This shader is used to clear VGPRS and LDS, and also write the input
 * pattern into the write back buffer, which will be used by driver to
 * check whether all SIMDs have been covered.
*/
static const u32 vgpr_init_compute_shader_aldebaran[] = {
	0xb8840904, 0xb8851a04, 0xb8861344, 0xb8831804, 0x9208ff06, 0x00000280,
	0x9209a805, 0x920a8a04, 0x81080908, 0x81080a08, 0x81080308, 0x8e078208,
	0x81078407, 0xc0410080, 0x00000007, 0xbf8c0000, 0xbf8a0000, 0xd3d94000,
	0x18000080, 0xd3d94001, 0x18000080, 0xd3d94002, 0x18000080, 0xd3d94003,
	0x18000080, 0xd3d94004, 0x18000080, 0xd3d94005, 0x18000080, 0xd3d94006,
	0x18000080, 0xd3d94007, 0x18000080, 0xd3d94008, 0x18000080, 0xd3d94009,
	0x18000080, 0xd3d9400a, 0x18000080, 0xd3d9400b, 0x18000080, 0xd3d9400c,
	0x18000080, 0xd3d9400d, 0x18000080, 0xd3d9400e, 0x18000080, 0xd3d9400f,
	0x18000080, 0xd3d94010, 0x18000080, 0xd3d94011, 0x18000080, 0xd3d94012,
	0x18000080, 0xd3d94013, 0x18000080, 0xd3d94014, 0x18000080, 0xd3d94015,
	0x18000080, 0xd3d94016, 0x18000080, 0xd3d94017, 0x18000080, 0xd3d94018,
	0x18000080, 0xd3d94019, 0x18000080, 0xd3d9401a, 0x18000080, 0xd3d9401b,
	0x18000080, 0xd3d9401c, 0x18000080, 0xd3d9401d, 0x18000080, 0xd3d9401e,
	0x18000080, 0xd3d9401f, 0x18000080, 0xd3d94020, 0x18000080, 0xd3d94021,
	0x18000080, 0xd3d94022, 0x18000080, 0xd3d94023, 0x18000080, 0xd3d94024,
	0x18000080, 0xd3d94025, 0x18000080, 0xd3d94026, 0x18000080, 0xd3d94027,
	0x18000080, 0xd3d94028, 0x18000080, 0xd3d94029, 0x18000080, 0xd3d9402a,
	0x18000080, 0xd3d9402b, 0x18000080, 0xd3d9402c, 0x18000080, 0xd3d9402d,
	0x18000080, 0xd3d9402e, 0x18000080, 0xd3d9402f, 0x18000080, 0xd3d94030,
	0x18000080, 0xd3d94031, 0x18000080, 0xd3d94032, 0x18000080, 0xd3d94033,
	0x18000080, 0xd3d94034, 0x18000080, 0xd3d94035, 0x18000080, 0xd3d94036,
	0x18000080, 0xd3d94037, 0x18000080, 0xd3d94038, 0x18000080, 0xd3d94039,
	0x18000080, 0xd3d9403a, 0x18000080, 0xd3d9403b, 0x18000080, 0xd3d9403c,
	0x18000080, 0xd3d9403d, 0x18000080, 0xd3d9403e, 0x18000080, 0xd3d9403f,
	0x18000080, 0xd3d94040, 0x18000080, 0xd3d94041, 0x18000080, 0xd3d94042,
	0x18000080, 0xd3d94043, 0x18000080, 0xd3d94044, 0x18000080, 0xd3d94045,
	0x18000080, 0xd3d94046, 0x18000080, 0xd3d94047, 0x18000080, 0xd3d94048,
	0x18000080, 0xd3d94049, 0x18000080, 0xd3d9404a, 0x18000080, 0xd3d9404b,
	0x18000080, 0xd3d9404c, 0x18000080, 0xd3d9404d, 0x18000080, 0xd3d9404e,
	0x18000080, 0xd3d9404f, 0x18000080, 0xd3d94050, 0x18000080, 0xd3d94051,
	0x18000080, 0xd3d94052, 0x18000080, 0xd3d94053, 0x18000080, 0xd3d94054,
	0x18000080, 0xd3d94055, 0x18000080, 0xd3d94056, 0x18000080, 0xd3d94057,
	0x18000080, 0xd3d94058, 0x18000080, 0xd3d94059, 0x18000080, 0xd3d9405a,
	0x18000080, 0xd3d9405b, 0x18000080, 0xd3d9405c, 0x18000080, 0xd3d9405d,
	0x18000080, 0xd3d9405e, 0x18000080, 0xd3d9405f, 0x18000080, 0xd3d94060,
	0x18000080, 0xd3d94061, 0x18000080, 0xd3d94062, 0x18000080, 0xd3d94063,
	0x18000080, 0xd3d94064, 0x18000080, 0xd3d94065, 0x18000080, 0xd3d94066,
	0x18000080, 0xd3d94067, 0x18000080, 0xd3d94068, 0x18000080, 0xd3d94069,
	0x18000080, 0xd3d9406a, 0x18000080, 0xd3d9406b, 0x18000080, 0xd3d9406c,
	0x18000080, 0xd3d9406d, 0x18000080, 0xd3d9406e, 0x18000080, 0xd3d9406f,
	0x18000080, 0xd3d94070, 0x18000080, 0xd3d94071, 0x18000080, 0xd3d94072,
	0x18000080, 0xd3d94073, 0x18000080, 0xd3d94074, 0x18000080, 0xd3d94075,
	0x18000080, 0xd3d94076, 0x18000080, 0xd3d94077, 0x18000080, 0xd3d94078,
	0x18000080, 0xd3d94079, 0x18000080, 0xd3d9407a, 0x18000080, 0xd3d9407b,
	0x18000080, 0xd3d9407c, 0x18000080, 0xd3d9407d, 0x18000080, 0xd3d9407e,
	0x18000080, 0xd3d9407f, 0x18000080, 0xd3d94080, 0x18000080, 0xd3d94081,
	0x18000080, 0xd3d94082, 0x18000080, 0xd3d94083, 0x18000080, 0xd3d94084,
	0x18000080, 0xd3d94085, 0x18000080, 0xd3d94086, 0x18000080, 0xd3d94087,
	0x18000080, 0xd3d94088, 0x18000080, 0xd3d94089, 0x18000080, 0xd3d9408a,
	0x18000080, 0xd3d9408b, 0x18000080, 0xd3d9408c, 0x18000080, 0xd3d9408d,
	0x18000080, 0xd3d9408e, 0x18000080, 0xd3d9408f, 0x18000080, 0xd3d94090,
	0x18000080, 0xd3d94091, 0x18000080, 0xd3d94092, 0x18000080, 0xd3d94093,
	0x18000080, 0xd3d94094, 0x18000080, 0xd3d94095, 0x18000080, 0xd3d94096,
	0x18000080, 0xd3d94097, 0x18000080, 0xd3d94098, 0x18000080, 0xd3d94099,
	0x18000080, 0xd3d9409a, 0x18000080, 0xd3d9409b, 0x18000080, 0xd3d9409c,
	0x18000080, 0xd3d9409d, 0x18000080, 0xd3d9409e, 0x18000080, 0xd3d9409f,
	0x18000080, 0xd3d940a0, 0x18000080, 0xd3d940a1, 0x18000080, 0xd3d940a2,
	0x18000080, 0xd3d940a3, 0x18000080, 0xd3d940a4, 0x18000080, 0xd3d940a5,
	0x18000080, 0xd3d940a6, 0x18000080, 0xd3d940a7, 0x18000080, 0xd3d940a8,
	0x18000080, 0xd3d940a9, 0x18000080, 0xd3d940aa, 0x18000080, 0xd3d940ab,
	0x18000080, 0xd3d940ac, 0x18000080, 0xd3d940ad, 0x18000080, 0xd3d940ae,
	0x18000080, 0xd3d940af, 0x18000080, 0xd3d940b0, 0x18000080, 0xd3d940b1,
	0x18000080, 0xd3d940b2, 0x18000080, 0xd3d940b3, 0x18000080, 0xd3d940b4,
	0x18000080, 0xd3d940b5, 0x18000080, 0xd3d940b6, 0x18000080, 0xd3d940b7,
	0x18000080, 0xd3d940b8, 0x18000080, 0xd3d940b9, 0x18000080, 0xd3d940ba,
	0x18000080, 0xd3d940bb, 0x18000080, 0xd3d940bc, 0x18000080, 0xd3d940bd,
	0x18000080, 0xd3d940be, 0x18000080, 0xd3d940bf, 0x18000080, 0xd3d940c0,
	0x18000080, 0xd3d940c1, 0x18000080, 0xd3d940c2, 0x18000080, 0xd3d940c3,
	0x18000080, 0xd3d940c4, 0x18000080, 0xd3d940c5, 0x18000080, 0xd3d940c6,
	0x18000080, 0xd3d940c7, 0x18000080, 0xd3d940c8, 0x18000080, 0xd3d940c9,
	0x18000080, 0xd3d940ca, 0x18000080, 0xd3d940cb, 0x18000080, 0xd3d940cc,
	0x18000080, 0xd3d940cd, 0x18000080, 0xd3d940ce, 0x18000080, 0xd3d940cf,
	0x18000080, 0xd3d940d0, 0x18000080, 0xd3d940d1, 0x18000080, 0xd3d940d2,
	0x18000080, 0xd3d940d3, 0x18000080, 0xd3d940d4, 0x18000080, 0xd3d940d5,
	0x18000080, 0xd3d940d6, 0x18000080, 0xd3d940d7, 0x18000080, 0xd3d940d8,
	0x18000080, 0xd3d940d9, 0x18000080, 0xd3d940da, 0x18000080, 0xd3d940db,
	0x18000080, 0xd3d940dc, 0x18000080, 0xd3d940dd, 0x18000080, 0xd3d940de,
	0x18000080, 0xd3d940df, 0x18000080, 0xd3d940e0, 0x18000080, 0xd3d940e1,
	0x18000080, 0xd3d940e2, 0x18000080, 0xd3d940e3, 0x18000080, 0xd3d940e4,
	0x18000080, 0xd3d940e5, 0x18000080, 0xd3d940e6, 0x18000080, 0xd3d940e7,
	0x18000080, 0xd3d940e8, 0x18000080, 0xd3d940e9, 0x18000080, 0xd3d940ea,
	0x18000080, 0xd3d940eb, 0x18000080, 0xd3d940ec, 0x18000080, 0xd3d940ed,
	0x18000080, 0xd3d940ee, 0x18000080, 0xd3d940ef, 0x18000080, 0xd3d940f0,
	0x18000080, 0xd3d940f1, 0x18000080, 0xd3d940f2, 0x18000080, 0xd3d940f3,
	0x18000080, 0xd3d940f4, 0x18000080, 0xd3d940f5, 0x18000080, 0xd3d940f6,
	0x18000080, 0xd3d940f7, 0x18000080, 0xd3d940f8, 0x18000080, 0xd3d940f9,
	0x18000080, 0xd3d940fa, 0x18000080, 0xd3d940fb, 0x18000080, 0xd3d940fc,
	0x18000080, 0xd3d940fd, 0x18000080, 0xd3d940fe, 0x18000080, 0xd3d940ff,
	0x18000080, 0xb07c0000, 0xbe8a00ff, 0x000000f8, 0xbf11080a, 0x7e000280,
	0x7e020280, 0x7e040280, 0x7e060280, 0x7e080280, 0x7e0a0280, 0x7e0c0280,
	0x7e0e0280, 0x808a880a, 0xbe80320a, 0xbf84fff5, 0xbf9c0000, 0xd28c0001,
	0x0001007f, 0xd28d0001, 0x0002027e, 0x10020288, 0xbe8b0004, 0xb78b4000,
	0xd1196a01, 0x00001701, 0xbe8a0087, 0xbefc00c1, 0xd89c4000, 0x00020201,
	0xd89cc080, 0x00040401, 0x320202ff, 0x00000800, 0x808a810a, 0xbf84fff8,
	0xbf810000,
};

const struct soc15_reg_entry vgpr_init_regs_aldebaran[] = {
	{ SOC15_REG_ENTRY(GC, 0, regCOMPUTE_RESOURCE_LIMITS), 0x0000000 },
	{ SOC15_REG_ENTRY(GC, 0, regCOMPUTE_NUM_THREAD_X), 0x40 },
	{ SOC15_REG_ENTRY(GC, 0, regCOMPUTE_NUM_THREAD_Y), 4 },
	{ SOC15_REG_ENTRY(GC, 0, regCOMPUTE_NUM_THREAD_Z), 1 },
	{ SOC15_REG_ENTRY(GC, 0, regCOMPUTE_PGM_RSRC1), 0xbf },
	{ SOC15_REG_ENTRY(GC, 0, regCOMPUTE_PGM_RSRC2), 0x400006 },  /* 64KB LDS */
	{ SOC15_REG_ENTRY(GC, 0, regCOMPUTE_PGM_RSRC3), 0x3F }, /*  63 - accum-offset = 256 */
	{ SOC15_REG_ENTRY(GC, 0, regCOMPUTE_STATIC_THREAD_MGMT_SE0), 0xffffffff },
	{ SOC15_REG_ENTRY(GC, 0, regCOMPUTE_STATIC_THREAD_MGMT_SE1), 0xffffffff },
	{ SOC15_REG_ENTRY(GC, 0, regCOMPUTE_STATIC_THREAD_MGMT_SE2), 0xffffffff },
	{ SOC15_REG_ENTRY(GC, 0, regCOMPUTE_STATIC_THREAD_MGMT_SE3), 0xffffffff },
	{ SOC15_REG_ENTRY(GC, 0, regCOMPUTE_STATIC_THREAD_MGMT_SE4), 0xffffffff },
	{ SOC15_REG_ENTRY(GC, 0, regCOMPUTE_STATIC_THREAD_MGMT_SE5), 0xffffffff },
	{ SOC15_REG_ENTRY(GC, 0, regCOMPUTE_STATIC_THREAD_MGMT_SE6), 0xffffffff },
	{ SOC15_REG_ENTRY(GC, 0, regCOMPUTE_STATIC_THREAD_MGMT_SE7), 0xffffffff },
};

/*
 * The below shaders are used to clear SGPRS, and also write the input
 * pattern into the write back buffer. The first two dispatch should be
 * scheduled simultaneously which make sure that all SGPRS could be
 * allocated, so the dispatch 1 need check write back buffer before scheduled,
 * make sure that waves of dispatch 0 are all dispacthed to all simds
 * balanced. both dispatch 0 and dispatch 1 should be halted until all waves
 * are dispatched, and then driver write a pattern to the shared memory to make
 * all waves continue.
*/
static const u32 sgpr112_init_compute_shader_aldebaran[] = {
	0xb8840904, 0xb8851a04, 0xb8861344, 0xb8831804, 0x9208ff06, 0x00000280,
	0x9209a805, 0x920a8a04, 0x81080908, 0x81080a08, 0x81080308, 0x8e078208,
	0x81078407, 0xc0410080, 0x00000007, 0xbf8c0000, 0xbf8e003f, 0xc0030200,
	0x00000000, 0xbf8c0000, 0xbf06ff08, 0xdeadbeaf, 0xbf84fff9, 0x81028102,
	0xc0410080, 0x00000007, 0xbf8c0000, 0xbf8a0000, 0xbefc0080, 0xbeea0080,
	0xbeeb0080, 0xbf00f280, 0xbee60080, 0xbee70080, 0xbee80080, 0xbee90080,
	0xbefe0080, 0xbeff0080, 0xbe880080, 0xbe890080, 0xbe8a0080, 0xbe8b0080,
	0xbe8c0080, 0xbe8d0080, 0xbe8e0080, 0xbe8f0080, 0xbe900080, 0xbe910080,
	0xbe920080, 0xbe930080, 0xbe940080, 0xbe950080, 0xbe960080, 0xbe970080,
	0xbe980080, 0xbe990080, 0xbe9a0080, 0xbe9b0080, 0xbe9c0080, 0xbe9d0080,
	0xbe9e0080, 0xbe9f0080, 0xbea00080, 0xbea10080, 0xbea20080, 0xbea30080,
	0xbea40080, 0xbea50080, 0xbea60080, 0xbea70080, 0xbea80080, 0xbea90080,
	0xbeaa0080, 0xbeab0080, 0xbeac0080, 0xbead0080, 0xbeae0080, 0xbeaf0080,
	0xbeb00080, 0xbeb10080, 0xbeb20080, 0xbeb30080, 0xbeb40080, 0xbeb50080,
	0xbeb60080, 0xbeb70080, 0xbeb80080, 0xbeb90080, 0xbeba0080, 0xbebb0080,
	0xbebc0080, 0xbebd0080, 0xbebe0080, 0xbebf0080, 0xbec00080, 0xbec10080,
	0xbec20080, 0xbec30080, 0xbec40080, 0xbec50080, 0xbec60080, 0xbec70080,
	0xbec80080, 0xbec90080, 0xbeca0080, 0xbecb0080, 0xbecc0080, 0xbecd0080,
	0xbece0080, 0xbecf0080, 0xbed00080, 0xbed10080, 0xbed20080, 0xbed30080,
	0xbed40080, 0xbed50080, 0xbed60080, 0xbed70080, 0xbed80080, 0xbed90080,
	0xbeda0080, 0xbedb0080, 0xbedc0080, 0xbedd0080, 0xbede0080, 0xbedf0080,
	0xbee00080, 0xbee10080, 0xbee20080, 0xbee30080, 0xbee40080, 0xbee50080,
	0xbf810000
};

const struct soc15_reg_entry sgpr112_init_regs_aldebaran[] = {
	{ SOC15_REG_ENTRY(GC, 0, regCOMPUTE_RESOURCE_LIMITS), 0x0000000 },
	{ SOC15_REG_ENTRY(GC, 0, regCOMPUTE_NUM_THREAD_X), 0x40 },
	{ SOC15_REG_ENTRY(GC, 0, regCOMPUTE_NUM_THREAD_Y), 8 },
	{ SOC15_REG_ENTRY(GC, 0, regCOMPUTE_NUM_THREAD_Z), 1 },
	{ SOC15_REG_ENTRY(GC, 0, regCOMPUTE_PGM_RSRC1), 0x340 },
	{ SOC15_REG_ENTRY(GC, 0, regCOMPUTE_PGM_RSRC2), 0x6 },
	{ SOC15_REG_ENTRY(GC, 0, regCOMPUTE_PGM_RSRC3), 0x0 },
	{ SOC15_REG_ENTRY(GC, 0, regCOMPUTE_STATIC_THREAD_MGMT_SE0), 0xffffffff },
	{ SOC15_REG_ENTRY(GC, 0, regCOMPUTE_STATIC_THREAD_MGMT_SE1), 0xffffffff },
	{ SOC15_REG_ENTRY(GC, 0, regCOMPUTE_STATIC_THREAD_MGMT_SE2), 0xffffffff },
	{ SOC15_REG_ENTRY(GC, 0, regCOMPUTE_STATIC_THREAD_MGMT_SE3), 0xffffffff },
	{ SOC15_REG_ENTRY(GC, 0, regCOMPUTE_STATIC_THREAD_MGMT_SE4), 0xffffffff },
	{ SOC15_REG_ENTRY(GC, 0, regCOMPUTE_STATIC_THREAD_MGMT_SE5), 0xffffffff },
	{ SOC15_REG_ENTRY(GC, 0, regCOMPUTE_STATIC_THREAD_MGMT_SE6), 0xffffffff },
	{ SOC15_REG_ENTRY(GC, 0, regCOMPUTE_STATIC_THREAD_MGMT_SE7), 0xffffffff },
};

static const u32 sgpr96_init_compute_shader_aldebaran[] = {
	0xb8840904, 0xb8851a04, 0xb8861344, 0xb8831804, 0x9208ff06, 0x00000280,
	0x9209a805, 0x920a8a04, 0x81080908, 0x81080a08, 0x81080308, 0x8e078208,
	0x81078407, 0xc0410080, 0x00000007, 0xbf8c0000, 0xbf8e003f, 0xc0030200,
	0x00000000, 0xbf8c0000, 0xbf06ff08, 0xdeadbeaf, 0xbf84fff9, 0x81028102,
	0xc0410080, 0x00000007, 0xbf8c0000, 0xbf8a0000, 0xbefc0080, 0xbeea0080,
	0xbeeb0080, 0xbf00f280, 0xbee60080, 0xbee70080, 0xbee80080, 0xbee90080,
	0xbefe0080, 0xbeff0080, 0xbe880080, 0xbe890080, 0xbe8a0080, 0xbe8b0080,
	0xbe8c0080, 0xbe8d0080, 0xbe8e0080, 0xbe8f0080, 0xbe900080, 0xbe910080,
	0xbe920080, 0xbe930080, 0xbe940080, 0xbe950080, 0xbe960080, 0xbe970080,
	0xbe980080, 0xbe990080, 0xbe9a0080, 0xbe9b0080, 0xbe9c0080, 0xbe9d0080,
	0xbe9e0080, 0xbe9f0080, 0xbea00080, 0xbea10080, 0xbea20080, 0xbea30080,
	0xbea40080, 0xbea50080, 0xbea60080, 0xbea70080, 0xbea80080, 0xbea90080,
	0xbeaa0080, 0xbeab0080, 0xbeac0080, 0xbead0080, 0xbeae0080, 0xbeaf0080,
	0xbeb00080, 0xbeb10080, 0xbeb20080, 0xbeb30080, 0xbeb40080, 0xbeb50080,
	0xbeb60080, 0xbeb70080, 0xbeb80080, 0xbeb90080, 0xbeba0080, 0xbebb0080,
	0xbebc0080, 0xbebd0080, 0xbebe0080, 0xbebf0080, 0xbec00080, 0xbec10080,
	0xbec20080, 0xbec30080, 0xbec40080, 0xbec50080, 0xbec60080, 0xbec70080,
	0xbec80080, 0xbec90080, 0xbeca0080, 0xbecb0080, 0xbecc0080, 0xbecd0080,
	0xbece0080, 0xbecf0080, 0xbed00080, 0xbed10080, 0xbed20080, 0xbed30080,
	0xbed40080, 0xbed50080, 0xbed60080, 0xbed70080, 0xbed80080, 0xbed90080,
	0xbf810000,
};

const struct soc15_reg_entry sgpr96_init_regs_aldebaran[] = {
	{ SOC15_REG_ENTRY(GC, 0, regCOMPUTE_RESOURCE_LIMITS), 0x0000000 },
	{ SOC15_REG_ENTRY(GC, 0, regCOMPUTE_NUM_THREAD_X), 0x40 },
	{ SOC15_REG_ENTRY(GC, 0, regCOMPUTE_NUM_THREAD_Y), 0xc },
	{ SOC15_REG_ENTRY(GC, 0, regCOMPUTE_NUM_THREAD_Z), 1 },
	{ SOC15_REG_ENTRY(GC, 0, regCOMPUTE_PGM_RSRC1), 0x2c0 },
	{ SOC15_REG_ENTRY(GC, 0, regCOMPUTE_PGM_RSRC2), 0x6 },
	{ SOC15_REG_ENTRY(GC, 0, regCOMPUTE_PGM_RSRC3), 0x0 },
	{ SOC15_REG_ENTRY(GC, 0, regCOMPUTE_STATIC_THREAD_MGMT_SE0), 0xffffffff },
	{ SOC15_REG_ENTRY(GC, 0, regCOMPUTE_STATIC_THREAD_MGMT_SE1), 0xffffffff },
	{ SOC15_REG_ENTRY(GC, 0, regCOMPUTE_STATIC_THREAD_MGMT_SE2), 0xffffffff },
	{ SOC15_REG_ENTRY(GC, 0, regCOMPUTE_STATIC_THREAD_MGMT_SE3), 0xffffffff },
	{ SOC15_REG_ENTRY(GC, 0, regCOMPUTE_STATIC_THREAD_MGMT_SE4), 0xffffffff },
	{ SOC15_REG_ENTRY(GC, 0, regCOMPUTE_STATIC_THREAD_MGMT_SE5), 0xffffffff },
	{ SOC15_REG_ENTRY(GC, 0, regCOMPUTE_STATIC_THREAD_MGMT_SE6), 0xffffffff },
	{ SOC15_REG_ENTRY(GC, 0, regCOMPUTE_STATIC_THREAD_MGMT_SE7), 0xffffffff },
};

/*
 * This shader is used to clear the uninitiated sgprs after the above
 * two dispatches, because of hardware feature, dispath 0 couldn't clear
 * top hole sgprs. Therefore need 4 waves per SIMD to cover these sgprs
*/
static const u32 sgpr64_init_compute_shader_aldebaran[] = {
	0xb8840904, 0xb8851a04, 0xb8861344, 0xb8831804, 0x9208ff06, 0x00000280,
	0x9209a805, 0x920a8a04, 0x81080908, 0x81080a08, 0x81080308, 0x8e078208,
	0x81078407, 0xc0410080, 0x00000007, 0xbf8c0000, 0xbf8e003f, 0xc0030200,
	0x00000000, 0xbf8c0000, 0xbf06ff08, 0xdeadbeaf, 0xbf84fff9, 0x81028102,
	0xc0410080, 0x00000007, 0xbf8c0000, 0xbf8a0000, 0xbefc0080, 0xbeea0080,
	0xbeeb0080, 0xbf00f280, 0xbee60080, 0xbee70080, 0xbee80080, 0xbee90080,
	0xbefe0080, 0xbeff0080, 0xbe880080, 0xbe890080, 0xbe8a0080, 0xbe8b0080,
	0xbe8c0080, 0xbe8d0080, 0xbe8e0080, 0xbe8f0080, 0xbe900080, 0xbe910080,
	0xbe920080, 0xbe930080, 0xbe940080, 0xbe950080, 0xbe960080, 0xbe970080,
	0xbe980080, 0xbe990080, 0xbe9a0080, 0xbe9b0080, 0xbe9c0080, 0xbe9d0080,
	0xbe9e0080, 0xbe9f0080, 0xbea00080, 0xbea10080, 0xbea20080, 0xbea30080,
	0xbea40080, 0xbea50080, 0xbea60080, 0xbea70080, 0xbea80080, 0xbea90080,
	0xbeaa0080, 0xbeab0080, 0xbeac0080, 0xbead0080, 0xbeae0080, 0xbeaf0080,
	0xbeb00080, 0xbeb10080, 0xbeb20080, 0xbeb30080, 0xbeb40080, 0xbeb50080,
	0xbeb60080, 0xbeb70080, 0xbeb80080, 0xbeb90080, 0xbf810000,
};

const struct soc15_reg_entry sgpr64_init_regs_aldebaran[] = {
	{ SOC15_REG_ENTRY(GC, 0, regCOMPUTE_RESOURCE_LIMITS), 0x0000000 },
	{ SOC15_REG_ENTRY(GC, 0, regCOMPUTE_NUM_THREAD_X), 0x40 },
	{ SOC15_REG_ENTRY(GC, 0, regCOMPUTE_NUM_THREAD_Y), 0x10 },
	{ SOC15_REG_ENTRY(GC, 0, regCOMPUTE_NUM_THREAD_Z), 1 },
	{ SOC15_REG_ENTRY(GC, 0, regCOMPUTE_PGM_RSRC1), 0x1c0 },
	{ SOC15_REG_ENTRY(GC, 0, regCOMPUTE_PGM_RSRC2), 0x6 },
	{ SOC15_REG_ENTRY(GC, 0, regCOMPUTE_PGM_RSRC3), 0x0 },
	{ SOC15_REG_ENTRY(GC, 0, regCOMPUTE_STATIC_THREAD_MGMT_SE0), 0xffffffff },
	{ SOC15_REG_ENTRY(GC, 0, regCOMPUTE_STATIC_THREAD_MGMT_SE1), 0xffffffff },
	{ SOC15_REG_ENTRY(GC, 0, regCOMPUTE_STATIC_THREAD_MGMT_SE2), 0xffffffff },
	{ SOC15_REG_ENTRY(GC, 0, regCOMPUTE_STATIC_THREAD_MGMT_SE3), 0xffffffff },
	{ SOC15_REG_ENTRY(GC, 0, regCOMPUTE_STATIC_THREAD_MGMT_SE4), 0xffffffff },
	{ SOC15_REG_ENTRY(GC, 0, regCOMPUTE_STATIC_THREAD_MGMT_SE5), 0xffffffff },
	{ SOC15_REG_ENTRY(GC, 0, regCOMPUTE_STATIC_THREAD_MGMT_SE6), 0xffffffff },
	{ SOC15_REG_ENTRY(GC, 0, regCOMPUTE_STATIC_THREAD_MGMT_SE7), 0xffffffff },
};

static int gfx_v9_4_2_run_shader(struct amdgpu_device *adev,
				 struct amdgpu_ring *ring,
				 struct amdgpu_ib *ib,
				 const u32 *shader_ptr, u32 shader_size,
				 const struct soc15_reg_entry *init_regs, u32 regs_size,
				 u32 compute_dim_x, u64 wb_gpu_addr, u32 pattern,
				 struct dma_fence **fence_ptr)
{
	int r, i;
	uint32_t total_size, shader_offset;
	u64 gpu_addr;

	total_size = (regs_size * 3 + 4 + 5 + 5) * 4;
	total_size = ALIGN(total_size, 256);
	shader_offset = total_size;
	total_size += ALIGN(shader_size, 256);

	/* allocate an indirect buffer to put the commands in */
	memset(ib, 0, sizeof(*ib));
	r = amdgpu_ib_get(adev, NULL, total_size,
					AMDGPU_IB_POOL_DIRECT, ib);
	if (r) {
		dev_err(adev->dev, "failed to get ib (%d).\n", r);
		return r;
	}

	/* load the compute shaders */
	for (i = 0; i < shader_size/sizeof(u32); i++)
		ib->ptr[i + (shader_offset / 4)] = shader_ptr[i];

	/* init the ib length to 0 */
	ib->length_dw = 0;

	/* write the register state for the compute dispatch */
	for (i = 0; i < regs_size; i++) {
		ib->ptr[ib->length_dw++] = PACKET3(PACKET3_SET_SH_REG, 1);
		ib->ptr[ib->length_dw++] = SOC15_REG_ENTRY_OFFSET(init_regs[i])
								- PACKET3_SET_SH_REG_START;
		ib->ptr[ib->length_dw++] = init_regs[i].reg_value;
	}

	/* write the shader start address: mmCOMPUTE_PGM_LO, mmCOMPUTE_PGM_HI */
	gpu_addr = (ib->gpu_addr + (u64)shader_offset) >> 8;
	ib->ptr[ib->length_dw++] = PACKET3(PACKET3_SET_SH_REG, 2);
	ib->ptr[ib->length_dw++] = SOC15_REG_OFFSET(GC, 0, regCOMPUTE_PGM_LO)
							- PACKET3_SET_SH_REG_START;
	ib->ptr[ib->length_dw++] = lower_32_bits(gpu_addr);
	ib->ptr[ib->length_dw++] = upper_32_bits(gpu_addr);

	/* write the wb buffer address */
	ib->ptr[ib->length_dw++] = PACKET3(PACKET3_SET_SH_REG, 3);
	ib->ptr[ib->length_dw++] = SOC15_REG_OFFSET(GC, 0, regCOMPUTE_USER_DATA_0)
							- PACKET3_SET_SH_REG_START;
	ib->ptr[ib->length_dw++] = lower_32_bits(wb_gpu_addr);
	ib->ptr[ib->length_dw++] = upper_32_bits(wb_gpu_addr);
	ib->ptr[ib->length_dw++] = pattern;

	/* write dispatch packet */
	ib->ptr[ib->length_dw++] = PACKET3(PACKET3_DISPATCH_DIRECT, 3);
	ib->ptr[ib->length_dw++] = compute_dim_x; /* x */
	ib->ptr[ib->length_dw++] = 1; /* y */
	ib->ptr[ib->length_dw++] = 1; /* z */
	ib->ptr[ib->length_dw++] =
		REG_SET_FIELD(0, COMPUTE_DISPATCH_INITIATOR, COMPUTE_SHADER_EN, 1);

	/* shedule the ib on the ring */
	r = amdgpu_ib_schedule(ring, 1, ib, NULL, fence_ptr);
	if (r) {
		dev_err(adev->dev, "ib submit failed (%d).\n", r);
		amdgpu_ib_free(adev, ib, NULL);
	}
	return r;
}

static void gfx_v9_4_2_log_wave_assignment(struct amdgpu_device *adev, uint32_t *wb_ptr)
{
	uint32_t se, cu, simd, wave;
	uint32_t offset = 0;
	char *str;
	int size;

	str = kmalloc(256, GFP_KERNEL);
	if (!str)
		return;

	dev_dbg(adev->dev, "wave assignment:\n");

	for (se = 0; se < adev->gfx.config.max_shader_engines; se++) {
		for (cu = 0; cu < CU_ID_MAX; cu++) {
			memset(str, 0, 256);
			size = sprintf(str, "SE[%02d]CU[%02d]: ", se, cu);
			for (simd = 0; simd < SIMD_ID_MAX; simd++) {
				size += sprintf(str + size, "[");
				for (wave = 0; wave < WAVE_ID_MAX; wave++) {
					size += sprintf(str + size, "%x", wb_ptr[offset]);
					offset++;
				}
				size += sprintf(str + size, "]  ");
			}
			dev_dbg(adev->dev, "%s\n", str);
		}
	}

	kfree(str);
}

static int gfx_v9_4_2_wait_for_waves_assigned(struct amdgpu_device *adev,
					      uint32_t *wb_ptr, uint32_t mask,
					      uint32_t pattern, uint32_t num_wave, bool wait)
{
	uint32_t se, cu, simd, wave;
	uint32_t loop = 0;
	uint32_t wave_cnt;
	uint32_t offset;

	do {
		wave_cnt = 0;
		offset = 0;

		for (se = 0; se < adev->gfx.config.max_shader_engines; se++)
			for (cu = 0; cu < CU_ID_MAX; cu++)
				for (simd = 0; simd < SIMD_ID_MAX; simd++)
					for (wave = 0; wave < WAVE_ID_MAX; wave++) {
						if (((1 << wave) & mask) &&
						    (wb_ptr[offset] == pattern))
							wave_cnt++;

						offset++;
					}

		if (wave_cnt == num_wave)
			return 0;

		mdelay(1);
	} while (++loop < 2000 && wait);

	dev_err(adev->dev, "actual wave num: %d, expected wave num: %d\n",
		wave_cnt, num_wave);

	gfx_v9_4_2_log_wave_assignment(adev, wb_ptr);

	return -EBADSLT;
}

static int gfx_v9_4_2_do_sgprs_init(struct amdgpu_device *adev)
{
	int r;
	int wb_size = adev->gfx.config.max_shader_engines *
			 CU_ID_MAX * SIMD_ID_MAX * WAVE_ID_MAX;
	struct amdgpu_ib wb_ib;
	struct amdgpu_ib disp_ibs[3];
	struct dma_fence *fences[3];
	u32 pattern[3] = { 0x1, 0x5, 0xa };

	/* bail if the compute ring is not ready */
	if (!adev->gfx.compute_ring[0].sched.ready ||
		 !adev->gfx.compute_ring[1].sched.ready)
		return 0;

	/* allocate the write-back buffer from IB */
	memset(&wb_ib, 0, sizeof(wb_ib));
	r = amdgpu_ib_get(adev, NULL, (1 + wb_size) * sizeof(uint32_t),
			  AMDGPU_IB_POOL_DIRECT, &wb_ib);
	if (r) {
		dev_err(adev->dev, "failed to get ib (%d) for wb\n", r);
		return r;
	}
	memset(wb_ib.ptr, 0, (1 + wb_size) * sizeof(uint32_t));

	r = gfx_v9_4_2_run_shader(adev,
			&adev->gfx.compute_ring[0],
			&disp_ibs[0],
			sgpr112_init_compute_shader_aldebaran,
			sizeof(sgpr112_init_compute_shader_aldebaran),
			sgpr112_init_regs_aldebaran,
			ARRAY_SIZE(sgpr112_init_regs_aldebaran),
			adev->gfx.cu_info.number,
			wb_ib.gpu_addr, pattern[0], &fences[0]);
	if (r) {
		dev_err(adev->dev, "failed to clear first 224 sgprs\n");
		goto pro_end;
	}

	r = gfx_v9_4_2_wait_for_waves_assigned(adev,
			&wb_ib.ptr[1], 0b11,
			pattern[0],
			adev->gfx.cu_info.number * SIMD_ID_MAX * 2,
			true);
	if (r) {
		dev_err(adev->dev, "wave coverage failed when clear first 224 sgprs\n");
		wb_ib.ptr[0] = 0xdeadbeaf; /* stop waves */
		goto disp0_failed;
	}

	r = gfx_v9_4_2_run_shader(adev,
			&adev->gfx.compute_ring[1],
			&disp_ibs[1],
			sgpr96_init_compute_shader_aldebaran,
			sizeof(sgpr96_init_compute_shader_aldebaran),
			sgpr96_init_regs_aldebaran,
			ARRAY_SIZE(sgpr96_init_regs_aldebaran),
			adev->gfx.cu_info.number * 2,
			wb_ib.gpu_addr, pattern[1], &fences[1]);
	if (r) {
		dev_err(adev->dev, "failed to clear next 576 sgprs\n");
		goto disp0_failed;
	}

	r = gfx_v9_4_2_wait_for_waves_assigned(adev,
			&wb_ib.ptr[1], 0b11111100,
			pattern[1], adev->gfx.cu_info.number * SIMD_ID_MAX * 6,
			true);
	if (r) {
		dev_err(adev->dev, "wave coverage failed when clear first 576 sgprs\n");
		wb_ib.ptr[0] = 0xdeadbeaf; /* stop waves */
		goto disp1_failed;
	}

	wb_ib.ptr[0] = 0xdeadbeaf; /* stop waves */

	/* wait for the GPU to finish processing the IB */
	r = dma_fence_wait(fences[0], false);
	if (r) {
		dev_err(adev->dev, "timeout to clear first 224 sgprs\n");
		goto disp1_failed;
	}

	r = dma_fence_wait(fences[1], false);
	if (r) {
		dev_err(adev->dev, "timeout to clear first 576 sgprs\n");
		goto disp1_failed;
	}

	memset(wb_ib.ptr, 0, (1 + wb_size) * sizeof(uint32_t));
	r = gfx_v9_4_2_run_shader(adev,
			&adev->gfx.compute_ring[0],
			&disp_ibs[2],
			sgpr64_init_compute_shader_aldebaran,
			sizeof(sgpr64_init_compute_shader_aldebaran),
			sgpr64_init_regs_aldebaran,
			ARRAY_SIZE(sgpr64_init_regs_aldebaran),
			adev->gfx.cu_info.number,
			wb_ib.gpu_addr, pattern[2], &fences[2]);
	if (r) {
		dev_err(adev->dev, "failed to clear first 256 sgprs\n");
		goto disp1_failed;
	}

	r = gfx_v9_4_2_wait_for_waves_assigned(adev,
			&wb_ib.ptr[1], 0b1111,
			pattern[2],
			adev->gfx.cu_info.number * SIMD_ID_MAX * 4,
			true);
	if (r) {
		dev_err(adev->dev, "wave coverage failed when clear first 256 sgprs\n");
		wb_ib.ptr[0] = 0xdeadbeaf; /* stop waves */
		goto disp2_failed;
	}

	wb_ib.ptr[0] = 0xdeadbeaf; /* stop waves */

	r = dma_fence_wait(fences[2], false);
	if (r) {
		dev_err(adev->dev, "timeout to clear first 256 sgprs\n");
		goto disp2_failed;
	}

disp2_failed:
	amdgpu_ib_free(adev, &disp_ibs[2], NULL);
	dma_fence_put(fences[2]);
disp1_failed:
	amdgpu_ib_free(adev, &disp_ibs[1], NULL);
	dma_fence_put(fences[1]);
disp0_failed:
	amdgpu_ib_free(adev, &disp_ibs[0], NULL);
	dma_fence_put(fences[0]);
pro_end:
	amdgpu_ib_free(adev, &wb_ib, NULL);

	if (r)
		dev_info(adev->dev, "Init SGPRS Failed\n");
	else
		dev_info(adev->dev, "Init SGPRS Successfully\n");

	return r;
}

static int gfx_v9_4_2_do_vgprs_init(struct amdgpu_device *adev)
{
	int r;
	/* CU_ID: 0~15, SIMD_ID: 0~3, WAVE_ID: 0 ~ 9 */
	int wb_size = adev->gfx.config.max_shader_engines *
			 CU_ID_MAX * SIMD_ID_MAX * WAVE_ID_MAX;
	struct amdgpu_ib wb_ib;
	struct amdgpu_ib disp_ib;
	struct dma_fence *fence;
	u32 pattern = 0xa;

	/* bail if the compute ring is not ready */
	if (!adev->gfx.compute_ring[0].sched.ready)
		return 0;

	/* allocate the write-back buffer from IB */
	memset(&wb_ib, 0, sizeof(wb_ib));
	r = amdgpu_ib_get(adev, NULL, (1 + wb_size) * sizeof(uint32_t),
			  AMDGPU_IB_POOL_DIRECT, &wb_ib);
	if (r) {
		dev_err(adev->dev, "failed to get ib (%d) for wb.\n", r);
		return r;
	}
	memset(wb_ib.ptr, 0, (1 + wb_size) * sizeof(uint32_t));

	r = gfx_v9_4_2_run_shader(adev,
			&adev->gfx.compute_ring[0],
			&disp_ib,
			vgpr_init_compute_shader_aldebaran,
			sizeof(vgpr_init_compute_shader_aldebaran),
			vgpr_init_regs_aldebaran,
			ARRAY_SIZE(vgpr_init_regs_aldebaran),
			adev->gfx.cu_info.number,
			wb_ib.gpu_addr, pattern, &fence);
	if (r) {
		dev_err(adev->dev, "failed to clear vgprs\n");
		goto pro_end;
	}

	/* wait for the GPU to finish processing the IB */
	r = dma_fence_wait(fence, false);
	if (r) {
		dev_err(adev->dev, "timeout to clear vgprs\n");
		goto disp_failed;
	}

	r = gfx_v9_4_2_wait_for_waves_assigned(adev,
			&wb_ib.ptr[1], 0b1,
			pattern,
			adev->gfx.cu_info.number * SIMD_ID_MAX,
			false);
	if (r) {
		dev_err(adev->dev, "failed to cover all simds when clearing vgprs\n");
		goto disp_failed;
	}

disp_failed:
	amdgpu_ib_free(adev, &disp_ib, NULL);
	dma_fence_put(fence);
pro_end:
	amdgpu_ib_free(adev, &wb_ib, NULL);

	if (r)
		dev_info(adev->dev, "Init VGPRS Failed\n");
	else
		dev_info(adev->dev, "Init VGPRS Successfully\n");

	return r;
}

int gfx_v9_4_2_do_edc_gpr_workarounds(struct amdgpu_device *adev)
{
	/* only support when RAS is enabled */
	if (!amdgpu_ras_is_supported(adev, AMDGPU_RAS_BLOCK__GFX))
		return 0;

	/* Workaround for ALDEBARAN, skip GPRs init in GPU reset.
	   Will remove it once GPRs init algorithm works for all CU settings. */
	if (amdgpu_in_reset(adev))
		return 0;

	gfx_v9_4_2_do_sgprs_init(adev);

	gfx_v9_4_2_do_vgprs_init(adev);

	return 0;
}

static void gfx_v9_4_2_query_sq_timeout_status(struct amdgpu_device *adev);
static void gfx_v9_4_2_reset_sq_timeout_status(struct amdgpu_device *adev);

void gfx_v9_4_2_init_golden_registers(struct amdgpu_device *adev,
				      uint32_t die_id)
{
	soc15_program_register_sequence(adev,
					golden_settings_gc_9_4_2_alde,
					ARRAY_SIZE(golden_settings_gc_9_4_2_alde));

	/* apply golden settings per die */
	switch (die_id) {
	case 0:
		soc15_program_register_sequence(adev,
				golden_settings_gc_9_4_2_alde_die_0,
				ARRAY_SIZE(golden_settings_gc_9_4_2_alde_die_0));
		break;
	case 1:
		soc15_program_register_sequence(adev,
				golden_settings_gc_9_4_2_alde_die_1,
				ARRAY_SIZE(golden_settings_gc_9_4_2_alde_die_1));
		break;
	default:
		dev_warn(adev->dev,
			 "invalid die id %d, ignore channel fabricid remap settings\n",
			 die_id);
		break;
	}

	return;
}

void gfx_v9_4_2_debug_trap_config_init(struct amdgpu_device *adev,
				uint32_t first_vmid,
				uint32_t last_vmid)
{
	uint32_t data;
	int i;

	mutex_lock(&adev->srbm_mutex);

	for (i = first_vmid; i < last_vmid; i++) {
		data = 0;
		soc15_grbm_select(adev, 0, 0, 0, i);
		data = REG_SET_FIELD(data, SPI_GDBG_PER_VMID_CNTL, TRAP_EN, 1);
		data = REG_SET_FIELD(data, SPI_GDBG_PER_VMID_CNTL, EXCP_EN, 0);
		data = REG_SET_FIELD(data, SPI_GDBG_PER_VMID_CNTL, EXCP_REPLACE,
					0);
		WREG32(SOC15_REG_OFFSET(GC, 0, regSPI_GDBG_PER_VMID_CNTL), data);
	}

	soc15_grbm_select(adev, 0, 0, 0, 0);
	mutex_unlock(&adev->srbm_mutex);
}

void gfx_v9_4_2_set_power_brake_sequence(struct amdgpu_device *adev)
{
	u32 tmp;

	gfx_v9_0_select_se_sh(adev, 0xffffffff, 0xffffffff, 0xffffffff);

	tmp = 0;
	tmp = REG_SET_FIELD(tmp, GC_THROTTLE_CTRL, PATTERN_MODE, 1);
	WREG32_SOC15(GC, 0, regGC_THROTTLE_CTRL, tmp);

	tmp = 0;
	tmp = REG_SET_FIELD(tmp, GC_THROTTLE_CTRL1, PWRBRK_STALL_EN, 1);
	WREG32_SOC15(GC, 0, regGC_THROTTLE_CTRL1, tmp);

	WREG32_SOC15(GC, 0, regGC_CAC_IND_INDEX, ixPWRBRK_STALL_PATTERN_CTRL);
	tmp = 0;
	tmp = REG_SET_FIELD(tmp, PWRBRK_STALL_PATTERN_CTRL, PWRBRK_END_STEP, 0x12);
	WREG32_SOC15(GC, 0, regGC_CAC_IND_DATA, tmp);
}

static const struct soc15_reg_entry gfx_v9_4_2_edc_counter_regs[] = {
	/* CPF */
	{ SOC15_REG_ENTRY(GC, 0, regCPF_EDC_ROQ_CNT), 0, 1, 1 },
	{ SOC15_REG_ENTRY(GC, 0, regCPF_EDC_TAG_CNT), 0, 1, 1 },
	/* CPC */
	{ SOC15_REG_ENTRY(GC, 0, regCPC_EDC_SCRATCH_CNT), 0, 1, 1 },
	{ SOC15_REG_ENTRY(GC, 0, regCPC_EDC_UCODE_CNT), 0, 1, 1 },
	{ SOC15_REG_ENTRY(GC, 0, regDC_EDC_STATE_CNT), 0, 1, 1 },
	{ SOC15_REG_ENTRY(GC, 0, regDC_EDC_CSINVOC_CNT), 0, 1, 1 },
	{ SOC15_REG_ENTRY(GC, 0, regDC_EDC_RESTORE_CNT), 0, 1, 1 },
	/* GDS */
	{ SOC15_REG_ENTRY(GC, 0, regGDS_EDC_CNT), 0, 1, 1 },
	{ SOC15_REG_ENTRY(GC, 0, regGDS_EDC_GRBM_CNT), 0, 1, 1 },
	{ SOC15_REG_ENTRY(GC, 0, regGDS_EDC_OA_DED), 0, 1, 1 },
	{ SOC15_REG_ENTRY(GC, 0, regGDS_EDC_OA_PHY_CNT), 0, 1, 1 },
	{ SOC15_REG_ENTRY(GC, 0, regGDS_EDC_OA_PIPE_CNT), 0, 1, 1 },
	/* RLC */
	{ SOC15_REG_ENTRY(GC, 0, regRLC_EDC_CNT), 0, 1, 1 },
	{ SOC15_REG_ENTRY(GC, 0, regRLC_EDC_CNT2), 0, 1, 1 },
	/* SPI */
	{ SOC15_REG_ENTRY(GC, 0, regSPI_EDC_CNT), 0, 8, 1 },
	/* SQC */
	{ SOC15_REG_ENTRY(GC, 0, regSQC_EDC_CNT), 0, 8, 7 },
	{ SOC15_REG_ENTRY(GC, 0, regSQC_EDC_CNT2), 0, 8, 7 },
	{ SOC15_REG_ENTRY(GC, 0, regSQC_EDC_CNT3), 0, 8, 7 },
	{ SOC15_REG_ENTRY(GC, 0, regSQC_EDC_PARITY_CNT3), 0, 8, 7 },
	/* SQ */
	{ SOC15_REG_ENTRY(GC, 0, regSQ_EDC_CNT), 0, 8, 14 },
	/* TCP */
	{ SOC15_REG_ENTRY(GC, 0, regTCP_EDC_CNT_NEW), 0, 8, 14 },
	/* TCI */
	{ SOC15_REG_ENTRY(GC, 0, regTCI_EDC_CNT), 0, 1, 69 },
	/* TCC */
	{ SOC15_REG_ENTRY(GC, 0, regTCC_EDC_CNT), 0, 1, 16 },
	{ SOC15_REG_ENTRY(GC, 0, regTCC_EDC_CNT2), 0, 1, 16 },
	/* TCA */
	{ SOC15_REG_ENTRY(GC, 0, regTCA_EDC_CNT), 0, 1, 2 },
	/* TCX */
	{ SOC15_REG_ENTRY(GC, 0, regTCX_EDC_CNT), 0, 1, 2 },
	{ SOC15_REG_ENTRY(GC, 0, regTCX_EDC_CNT2), 0, 1, 2 },
	/* TD */
	{ SOC15_REG_ENTRY(GC, 0, regTD_EDC_CNT), 0, 8, 14 },
	/* TA */
	{ SOC15_REG_ENTRY(GC, 0, regTA_EDC_CNT), 0, 8, 14 },
	/* GCEA */
	{ SOC15_REG_ENTRY(GC, 0, regGCEA_EDC_CNT), 0, 1, 16 },
	{ SOC15_REG_ENTRY(GC, 0, regGCEA_EDC_CNT2), 0, 1, 16 },
	{ SOC15_REG_ENTRY(GC, 0, regGCEA_EDC_CNT3), 0, 1, 16 },
};

static void gfx_v9_4_2_select_se_sh(struct amdgpu_device *adev, u32 se_num,
				  u32 sh_num, u32 instance)
{
	u32 data;

	if (instance == 0xffffffff)
		data = REG_SET_FIELD(0, GRBM_GFX_INDEX,
				     INSTANCE_BROADCAST_WRITES, 1);
	else
		data = REG_SET_FIELD(0, GRBM_GFX_INDEX, INSTANCE_INDEX,
				     instance);

	if (se_num == 0xffffffff)
		data = REG_SET_FIELD(data, GRBM_GFX_INDEX, SE_BROADCAST_WRITES,
				     1);
	else
		data = REG_SET_FIELD(data, GRBM_GFX_INDEX, SE_INDEX, se_num);

	if (sh_num == 0xffffffff)
		data = REG_SET_FIELD(data, GRBM_GFX_INDEX, SH_BROADCAST_WRITES,
				     1);
	else
		data = REG_SET_FIELD(data, GRBM_GFX_INDEX, SH_INDEX, sh_num);

	WREG32_SOC15_RLC_SHADOW_EX(reg, GC, 0, regGRBM_GFX_INDEX, data);
}

static const struct soc15_ras_field_entry gfx_v9_4_2_ras_fields[] = {
	/* CPF */
	{ "CPF_ROQ_ME2", SOC15_REG_ENTRY(GC, 0, regCPF_EDC_ROQ_CNT),
	  SOC15_REG_FIELD(CPF_EDC_ROQ_CNT, SEC_COUNT_ME2),
	  SOC15_REG_FIELD(CPF_EDC_ROQ_CNT, DED_COUNT_ME2) },
	{ "CPF_ROQ_ME1", SOC15_REG_ENTRY(GC, 0, regCPF_EDC_ROQ_CNT),
	  SOC15_REG_FIELD(CPF_EDC_ROQ_CNT, SEC_COUNT_ME1),
	  SOC15_REG_FIELD(CPF_EDC_ROQ_CNT, DED_COUNT_ME1) },
	{ "CPF_TCIU_TAG", SOC15_REG_ENTRY(GC, 0, regCPF_EDC_TAG_CNT),
	  SOC15_REG_FIELD(CPF_EDC_TAG_CNT, SEC_COUNT),
	  SOC15_REG_FIELD(CPF_EDC_TAG_CNT, DED_COUNT) },

	/* CPC */
	{ "CPC_SCRATCH", SOC15_REG_ENTRY(GC, 0, regCPC_EDC_SCRATCH_CNT),
	  SOC15_REG_FIELD(CPC_EDC_SCRATCH_CNT, SEC_COUNT),
	  SOC15_REG_FIELD(CPC_EDC_SCRATCH_CNT, DED_COUNT) },
	{ "CPC_UCODE", SOC15_REG_ENTRY(GC, 0, regCPC_EDC_UCODE_CNT),
	  SOC15_REG_FIELD(CPC_EDC_UCODE_CNT, SEC_COUNT),
	  SOC15_REG_FIELD(CPC_EDC_UCODE_CNT, DED_COUNT) },
	{ "CPC_DC_STATE_RAM_ME1", SOC15_REG_ENTRY(GC, 0, regDC_EDC_STATE_CNT),
	  SOC15_REG_FIELD(DC_EDC_STATE_CNT, SEC_COUNT_ME1),
	  SOC15_REG_FIELD(DC_EDC_STATE_CNT, DED_COUNT_ME1) },
	{ "CPC_DC_CSINVOC_RAM_ME1",
	  SOC15_REG_ENTRY(GC, 0, regDC_EDC_CSINVOC_CNT),
	  SOC15_REG_FIELD(DC_EDC_CSINVOC_CNT, SEC_COUNT_ME1),
	  SOC15_REG_FIELD(DC_EDC_CSINVOC_CNT, DED_COUNT_ME1) },
	{ "CPC_DC_RESTORE_RAM_ME1",
	  SOC15_REG_ENTRY(GC, 0, regDC_EDC_RESTORE_CNT),
	  SOC15_REG_FIELD(DC_EDC_RESTORE_CNT, SEC_COUNT_ME1),
	  SOC15_REG_FIELD(DC_EDC_RESTORE_CNT, DED_COUNT_ME1) },
	{ "CPC_DC_CSINVOC_RAM1_ME1",
	  SOC15_REG_ENTRY(GC, 0, regDC_EDC_CSINVOC_CNT),
	  SOC15_REG_FIELD(DC_EDC_CSINVOC_CNT, SEC_COUNT1_ME1),
	  SOC15_REG_FIELD(DC_EDC_CSINVOC_CNT, DED_COUNT1_ME1) },
	{ "CPC_DC_RESTORE_RAM1_ME1",
	  SOC15_REG_ENTRY(GC, 0, regDC_EDC_RESTORE_CNT),
	  SOC15_REG_FIELD(DC_EDC_RESTORE_CNT, SEC_COUNT1_ME1),
	  SOC15_REG_FIELD(DC_EDC_RESTORE_CNT, DED_COUNT1_ME1) },

	/* GDS */
	{ "GDS_GRBM", SOC15_REG_ENTRY(GC, 0, regGDS_EDC_GRBM_CNT),
	  SOC15_REG_FIELD(GDS_EDC_GRBM_CNT, SEC),
	  SOC15_REG_FIELD(GDS_EDC_GRBM_CNT, DED) },
	{ "GDS_MEM", SOC15_REG_ENTRY(GC, 0, regGDS_EDC_CNT),
	  SOC15_REG_FIELD(GDS_EDC_CNT, GDS_MEM_SEC),
	  SOC15_REG_FIELD(GDS_EDC_CNT, GDS_MEM_DED) },
	{ "GDS_PHY_CMD_RAM_MEM", SOC15_REG_ENTRY(GC, 0, regGDS_EDC_OA_PHY_CNT),
	  SOC15_REG_FIELD(GDS_EDC_OA_PHY_CNT, PHY_CMD_RAM_MEM_SEC),
	  SOC15_REG_FIELD(GDS_EDC_OA_PHY_CNT, PHY_CMD_RAM_MEM_DED) },
	{ "GDS_PHY_DATA_RAM_MEM", SOC15_REG_ENTRY(GC, 0, regGDS_EDC_OA_PHY_CNT),
	  SOC15_REG_FIELD(GDS_EDC_OA_PHY_CNT, PHY_DATA_RAM_MEM_SEC),
	  SOC15_REG_FIELD(GDS_EDC_OA_PHY_CNT, PHY_DATA_RAM_MEM_DED) },
	{ "GDS_ME0_CS_PIPE_MEM", SOC15_REG_ENTRY(GC, 0, regGDS_EDC_OA_PHY_CNT),
	  SOC15_REG_FIELD(GDS_EDC_OA_PHY_CNT, ME0_CS_PIPE_MEM_SEC),
	  SOC15_REG_FIELD(GDS_EDC_OA_PHY_CNT, ME0_CS_PIPE_MEM_DED) },
	{ "GDS_ME1_PIPE0_PIPE_MEM",
	  SOC15_REG_ENTRY(GC, 0, regGDS_EDC_OA_PIPE_CNT),
	  SOC15_REG_FIELD(GDS_EDC_OA_PIPE_CNT, ME1_PIPE0_PIPE_MEM_SEC),
	  SOC15_REG_FIELD(GDS_EDC_OA_PIPE_CNT, ME1_PIPE0_PIPE_MEM_DED) },
	{ "GDS_ME1_PIPE1_PIPE_MEM",
	  SOC15_REG_ENTRY(GC, 0, regGDS_EDC_OA_PIPE_CNT),
	  SOC15_REG_FIELD(GDS_EDC_OA_PIPE_CNT, ME1_PIPE1_PIPE_MEM_SEC),
	  SOC15_REG_FIELD(GDS_EDC_OA_PIPE_CNT, ME1_PIPE1_PIPE_MEM_DED) },
	{ "GDS_ME1_PIPE2_PIPE_MEM",
	  SOC15_REG_ENTRY(GC, 0, regGDS_EDC_OA_PIPE_CNT),
	  SOC15_REG_FIELD(GDS_EDC_OA_PIPE_CNT, ME1_PIPE2_PIPE_MEM_SEC),
	  SOC15_REG_FIELD(GDS_EDC_OA_PIPE_CNT, ME1_PIPE2_PIPE_MEM_DED) },
	{ "GDS_ME1_PIPE3_PIPE_MEM",
	  SOC15_REG_ENTRY(GC, 0, regGDS_EDC_OA_PIPE_CNT),
	  SOC15_REG_FIELD(GDS_EDC_OA_PIPE_CNT, ME1_PIPE3_PIPE_MEM_SEC),
	  SOC15_REG_FIELD(GDS_EDC_OA_PIPE_CNT, ME1_PIPE3_PIPE_MEM_DED) },
	{ "GDS_ME0_GFXHP3D_PIX_DED",
	  SOC15_REG_ENTRY(GC, 0, regGDS_EDC_OA_DED), 0, 0,
	  SOC15_REG_FIELD(GDS_EDC_OA_DED, ME0_GFXHP3D_PIX_DED) },
	{ "GDS_ME0_GFXHP3D_VTX_DED",
	  SOC15_REG_ENTRY(GC, 0, regGDS_EDC_OA_DED), 0, 0,
	  SOC15_REG_FIELD(GDS_EDC_OA_DED, ME0_GFXHP3D_VTX_DED) },
	{ "GDS_ME0_CS_DED",
	  SOC15_REG_ENTRY(GC, 0, regGDS_EDC_OA_DED), 0, 0,
	  SOC15_REG_FIELD(GDS_EDC_OA_DED, ME0_CS_DED) },
	{ "GDS_ME0_GFXHP3D_GS_DED",
	  SOC15_REG_ENTRY(GC, 0, regGDS_EDC_OA_DED), 0, 0,
	  SOC15_REG_FIELD(GDS_EDC_OA_DED, ME0_GFXHP3D_GS_DED) },
	{ "GDS_ME1_PIPE0_DED",
	  SOC15_REG_ENTRY(GC, 0, regGDS_EDC_OA_DED), 0, 0,
	  SOC15_REG_FIELD(GDS_EDC_OA_DED, ME1_PIPE0_DED) },
	{ "GDS_ME1_PIPE1_DED",
	  SOC15_REG_ENTRY(GC, 0, regGDS_EDC_OA_DED), 0, 0,
	  SOC15_REG_FIELD(GDS_EDC_OA_DED, ME1_PIPE1_DED) },
	{ "GDS_ME1_PIPE2_DED",
	  SOC15_REG_ENTRY(GC, 0, regGDS_EDC_OA_DED), 0, 0,
	  SOC15_REG_FIELD(GDS_EDC_OA_DED, ME1_PIPE2_DED) },
	{ "GDS_ME1_PIPE3_DED",
	  SOC15_REG_ENTRY(GC, 0, regGDS_EDC_OA_DED), 0, 0,
	  SOC15_REG_FIELD(GDS_EDC_OA_DED, ME1_PIPE3_DED) },
	{ "GDS_ME2_PIPE0_DED",
	  SOC15_REG_ENTRY(GC, 0, regGDS_EDC_OA_DED), 0, 0,
	  SOC15_REG_FIELD(GDS_EDC_OA_DED, ME2_PIPE0_DED) },
	{ "GDS_ME2_PIPE1_DED",
	  SOC15_REG_ENTRY(GC, 0, regGDS_EDC_OA_DED), 0, 0,
	  SOC15_REG_FIELD(GDS_EDC_OA_DED, ME2_PIPE1_DED) },
	{ "GDS_ME2_PIPE2_DED",
	  SOC15_REG_ENTRY(GC, 0, regGDS_EDC_OA_DED), 0, 0,
	  SOC15_REG_FIELD(GDS_EDC_OA_DED, ME2_PIPE2_DED) },
	{ "GDS_ME2_PIPE3_DED",
	  SOC15_REG_ENTRY(GC, 0, regGDS_EDC_OA_DED), 0, 0,
	  SOC15_REG_FIELD(GDS_EDC_OA_DED, ME2_PIPE3_DED) },

	/* RLC */
	{ "RLCG_INSTR_RAM", SOC15_REG_ENTRY(GC, 0, regRLC_EDC_CNT),
	  SOC15_REG_FIELD(RLC_EDC_CNT, RLCG_INSTR_RAM_SEC_COUNT),
	  SOC15_REG_FIELD(RLC_EDC_CNT, RLCG_INSTR_RAM_DED_COUNT) },
	{ "RLCG_SCRATCH_RAM", SOC15_REG_ENTRY(GC, 0, regRLC_EDC_CNT),
	  SOC15_REG_FIELD(RLC_EDC_CNT, RLCG_SCRATCH_RAM_SEC_COUNT),
	  SOC15_REG_FIELD(RLC_EDC_CNT, RLCG_SCRATCH_RAM_DED_COUNT) },
	{ "RLCV_INSTR_RAM", SOC15_REG_ENTRY(GC, 0, regRLC_EDC_CNT),
	  SOC15_REG_FIELD(RLC_EDC_CNT, RLCV_INSTR_RAM_SEC_COUNT),
	  SOC15_REG_FIELD(RLC_EDC_CNT, RLCV_INSTR_RAM_DED_COUNT) },
	{ "RLCV_SCRATCH_RAM", SOC15_REG_ENTRY(GC, 0, regRLC_EDC_CNT),
	  SOC15_REG_FIELD(RLC_EDC_CNT, RLCV_SCRATCH_RAM_SEC_COUNT),
	  SOC15_REG_FIELD(RLC_EDC_CNT, RLCV_SCRATCH_RAM_DED_COUNT) },
	{ "RLC_TCTAG_RAM", SOC15_REG_ENTRY(GC, 0, regRLC_EDC_CNT),
	  SOC15_REG_FIELD(RLC_EDC_CNT, RLC_TCTAG_RAM_SEC_COUNT),
	  SOC15_REG_FIELD(RLC_EDC_CNT, RLC_TCTAG_RAM_DED_COUNT) },
	{ "RLC_SPM_SCRATCH_RAM", SOC15_REG_ENTRY(GC, 0, regRLC_EDC_CNT),
	  SOC15_REG_FIELD(RLC_EDC_CNT, RLC_SPM_SCRATCH_RAM_SEC_COUNT),
	  SOC15_REG_FIELD(RLC_EDC_CNT, RLC_SPM_SCRATCH_RAM_DED_COUNT) },
	{ "RLC_SRM_DATA_RAM", SOC15_REG_ENTRY(GC, 0, regRLC_EDC_CNT),
	  SOC15_REG_FIELD(RLC_EDC_CNT, RLC_SRM_DATA_RAM_SEC_COUNT),
	  SOC15_REG_FIELD(RLC_EDC_CNT, RLC_SRM_DATA_RAM_DED_COUNT) },
	{ "RLC_SRM_ADDR_RAM", SOC15_REG_ENTRY(GC, 0, regRLC_EDC_CNT),
	  SOC15_REG_FIELD(RLC_EDC_CNT, RLC_SRM_ADDR_RAM_SEC_COUNT),
	  SOC15_REG_FIELD(RLC_EDC_CNT, RLC_SRM_ADDR_RAM_DED_COUNT) },
	{ "RLC_SPM_SE0_SCRATCH_RAM", SOC15_REG_ENTRY(GC, 0, regRLC_EDC_CNT2),
	  SOC15_REG_FIELD(RLC_EDC_CNT2, RLC_SPM_SE0_SCRATCH_RAM_SEC_COUNT),
	  SOC15_REG_FIELD(RLC_EDC_CNT2, RLC_SPM_SE0_SCRATCH_RAM_DED_COUNT) },
	{ "RLC_SPM_SE1_SCRATCH_RAM", SOC15_REG_ENTRY(GC, 0, regRLC_EDC_CNT2),
	  SOC15_REG_FIELD(RLC_EDC_CNT2, RLC_SPM_SE1_SCRATCH_RAM_SEC_COUNT),
	  SOC15_REG_FIELD(RLC_EDC_CNT2, RLC_SPM_SE1_SCRATCH_RAM_DED_COUNT) },
	{ "RLC_SPM_SE2_SCRATCH_RAM", SOC15_REG_ENTRY(GC, 0, regRLC_EDC_CNT2),
	  SOC15_REG_FIELD(RLC_EDC_CNT2, RLC_SPM_SE2_SCRATCH_RAM_SEC_COUNT),
	  SOC15_REG_FIELD(RLC_EDC_CNT2, RLC_SPM_SE2_SCRATCH_RAM_DED_COUNT) },
	{ "RLC_SPM_SE3_SCRATCH_RAM", SOC15_REG_ENTRY(GC, 0, regRLC_EDC_CNT2),
	  SOC15_REG_FIELD(RLC_EDC_CNT2, RLC_SPM_SE3_SCRATCH_RAM_SEC_COUNT),
	  SOC15_REG_FIELD(RLC_EDC_CNT2, RLC_SPM_SE3_SCRATCH_RAM_DED_COUNT) },
	{ "RLC_SPM_SE4_SCRATCH_RAM", SOC15_REG_ENTRY(GC, 0, regRLC_EDC_CNT2),
	  SOC15_REG_FIELD(RLC_EDC_CNT2, RLC_SPM_SE4_SCRATCH_RAM_SEC_COUNT),
	  SOC15_REG_FIELD(RLC_EDC_CNT2, RLC_SPM_SE4_SCRATCH_RAM_DED_COUNT) },
	{ "RLC_SPM_SE5_SCRATCH_RAM", SOC15_REG_ENTRY(GC, 0, regRLC_EDC_CNT2),
	  SOC15_REG_FIELD(RLC_EDC_CNT2, RLC_SPM_SE5_SCRATCH_RAM_SEC_COUNT),
	  SOC15_REG_FIELD(RLC_EDC_CNT2, RLC_SPM_SE5_SCRATCH_RAM_DED_COUNT) },
	{ "RLC_SPM_SE6_SCRATCH_RAM", SOC15_REG_ENTRY(GC, 0, regRLC_EDC_CNT2),
	  SOC15_REG_FIELD(RLC_EDC_CNT2, RLC_SPM_SE6_SCRATCH_RAM_SEC_COUNT),
	  SOC15_REG_FIELD(RLC_EDC_CNT2, RLC_SPM_SE6_SCRATCH_RAM_DED_COUNT) },
	{ "RLC_SPM_SE7_SCRATCH_RAM", SOC15_REG_ENTRY(GC, 0, regRLC_EDC_CNT2),
	  SOC15_REG_FIELD(RLC_EDC_CNT2, RLC_SPM_SE7_SCRATCH_RAM_SEC_COUNT),
	  SOC15_REG_FIELD(RLC_EDC_CNT2, RLC_SPM_SE7_SCRATCH_RAM_DED_COUNT) },

	/* SPI */
	{ "SPI_SR_MEM", SOC15_REG_ENTRY(GC, 0, regSPI_EDC_CNT),
	  SOC15_REG_FIELD(SPI_EDC_CNT, SPI_SR_MEM_SEC_COUNT),
	  SOC15_REG_FIELD(SPI_EDC_CNT, SPI_SR_MEM_DED_COUNT) },
	{ "SPI_GDS_EXPREQ", SOC15_REG_ENTRY(GC, 0, regSPI_EDC_CNT),
	  SOC15_REG_FIELD(SPI_EDC_CNT, SPI_GDS_EXPREQ_SEC_COUNT),
	  SOC15_REG_FIELD(SPI_EDC_CNT, SPI_GDS_EXPREQ_DED_COUNT) },
	{ "SPI_WB_GRANT_30", SOC15_REG_ENTRY(GC, 0, regSPI_EDC_CNT),
	  SOC15_REG_FIELD(SPI_EDC_CNT, SPI_WB_GRANT_30_SEC_COUNT),
	  SOC15_REG_FIELD(SPI_EDC_CNT, SPI_WB_GRANT_30_DED_COUNT) },
	{ "SPI_LIFE_CNT", SOC15_REG_ENTRY(GC, 0, regSPI_EDC_CNT),
	  SOC15_REG_FIELD(SPI_EDC_CNT, SPI_LIFE_CNT_SEC_COUNT),
	  SOC15_REG_FIELD(SPI_EDC_CNT, SPI_LIFE_CNT_DED_COUNT) },

	/* SQC - regSQC_EDC_CNT */
	{ "SQC_DATA_CU0_WRITE_DATA_BUF", SOC15_REG_ENTRY(GC, 0, regSQC_EDC_CNT),
	  SOC15_REG_FIELD(SQC_EDC_CNT, DATA_CU0_WRITE_DATA_BUF_SEC_COUNT),
	  SOC15_REG_FIELD(SQC_EDC_CNT, DATA_CU0_WRITE_DATA_BUF_DED_COUNT) },
	{ "SQC_DATA_CU0_UTCL1_LFIFO", SOC15_REG_ENTRY(GC, 0, regSQC_EDC_CNT),
	  SOC15_REG_FIELD(SQC_EDC_CNT, DATA_CU0_UTCL1_LFIFO_SEC_COUNT),
	  SOC15_REG_FIELD(SQC_EDC_CNT, DATA_CU0_UTCL1_LFIFO_DED_COUNT) },
	{ "SQC_DATA_CU1_WRITE_DATA_BUF", SOC15_REG_ENTRY(GC, 0, regSQC_EDC_CNT),
	  SOC15_REG_FIELD(SQC_EDC_CNT, DATA_CU1_WRITE_DATA_BUF_SEC_COUNT),
	  SOC15_REG_FIELD(SQC_EDC_CNT, DATA_CU1_WRITE_DATA_BUF_DED_COUNT) },
	{ "SQC_DATA_CU1_UTCL1_LFIFO", SOC15_REG_ENTRY(GC, 0, regSQC_EDC_CNT),
	  SOC15_REG_FIELD(SQC_EDC_CNT, DATA_CU1_UTCL1_LFIFO_SEC_COUNT),
	  SOC15_REG_FIELD(SQC_EDC_CNT, DATA_CU1_UTCL1_LFIFO_DED_COUNT) },
	{ "SQC_DATA_CU2_WRITE_DATA_BUF", SOC15_REG_ENTRY(GC, 0, regSQC_EDC_CNT),
	  SOC15_REG_FIELD(SQC_EDC_CNT, DATA_CU2_WRITE_DATA_BUF_SEC_COUNT),
	  SOC15_REG_FIELD(SQC_EDC_CNT, DATA_CU2_WRITE_DATA_BUF_DED_COUNT) },
	{ "SQC_DATA_CU2_UTCL1_LFIFO", SOC15_REG_ENTRY(GC, 0, regSQC_EDC_CNT),
	  SOC15_REG_FIELD(SQC_EDC_CNT, DATA_CU2_UTCL1_LFIFO_SEC_COUNT),
	  SOC15_REG_FIELD(SQC_EDC_CNT, DATA_CU2_UTCL1_LFIFO_DED_COUNT) },
	{ "SQC_DATA_CU3_WRITE_DATA_BUF", SOC15_REG_ENTRY(GC, 0, regSQC_EDC_CNT),
	  SOC15_REG_FIELD(SQC_EDC_CNT, DATA_CU3_WRITE_DATA_BUF_SEC_COUNT),
	  SOC15_REG_FIELD(SQC_EDC_CNT, DATA_CU3_WRITE_DATA_BUF_DED_COUNT) },
	{ "SQC_DATA_CU3_UTCL1_LFIFO", SOC15_REG_ENTRY(GC, 0, regSQC_EDC_CNT),
	  SOC15_REG_FIELD(SQC_EDC_CNT, DATA_CU3_UTCL1_LFIFO_SEC_COUNT),
	  SOC15_REG_FIELD(SQC_EDC_CNT, DATA_CU3_UTCL1_LFIFO_DED_COUNT) },

	/* SQC - regSQC_EDC_CNT2 */
	{ "SQC_INST_BANKA_TAG_RAM", SOC15_REG_ENTRY(GC, 0, regSQC_EDC_CNT2),
	  SOC15_REG_FIELD(SQC_EDC_CNT2, INST_BANKA_TAG_RAM_SEC_COUNT),
	  SOC15_REG_FIELD(SQC_EDC_CNT2, INST_BANKA_TAG_RAM_DED_COUNT) },
	{ "SQC_INST_BANKA_BANK_RAM", SOC15_REG_ENTRY(GC, 0, regSQC_EDC_CNT2),
	  SOC15_REG_FIELD(SQC_EDC_CNT2, INST_BANKA_BANK_RAM_SEC_COUNT),
	  SOC15_REG_FIELD(SQC_EDC_CNT2, INST_BANKA_BANK_RAM_DED_COUNT) },
	{ "SQC_DATA_BANKA_TAG_RAM", SOC15_REG_ENTRY(GC, 0, regSQC_EDC_CNT2),
	  SOC15_REG_FIELD(SQC_EDC_CNT2, DATA_BANKA_TAG_RAM_SEC_COUNT),
	  SOC15_REG_FIELD(SQC_EDC_CNT2, DATA_BANKA_TAG_RAM_DED_COUNT) },
	{ "SQC_DATA_BANKA_BANK_RAM", SOC15_REG_ENTRY(GC, 0, regSQC_EDC_CNT2),
	  SOC15_REG_FIELD(SQC_EDC_CNT2, DATA_BANKA_BANK_RAM_SEC_COUNT),
	  SOC15_REG_FIELD(SQC_EDC_CNT2, DATA_BANKA_BANK_RAM_DED_COUNT) },
	{ "SQC_INST_UTCL1_LFIFO", SOC15_REG_ENTRY(GC, 0, regSQC_EDC_CNT2),
	  SOC15_REG_FIELD(SQC_EDC_CNT2, INST_UTCL1_LFIFO_SEC_COUNT),
	  SOC15_REG_FIELD(SQC_EDC_CNT2, INST_UTCL1_LFIFO_DED_COUNT) },
	{ "SQC_DATA_BANKA_DIRTY_BIT_RAM", SOC15_REG_ENTRY(GC, 0, regSQC_EDC_CNT2),
	  SOC15_REG_FIELD(SQC_EDC_CNT2, DATA_BANKA_DIRTY_BIT_RAM_SEC_COUNT),
	  SOC15_REG_FIELD(SQC_EDC_CNT2, DATA_BANKA_DIRTY_BIT_RAM_DED_COUNT) },

	/* SQC - regSQC_EDC_CNT3 */
	{ "SQC_INST_BANKB_TAG_RAM", SOC15_REG_ENTRY(GC, 0, regSQC_EDC_CNT3),
	  SOC15_REG_FIELD(SQC_EDC_CNT3, INST_BANKB_TAG_RAM_SEC_COUNT),
	  SOC15_REG_FIELD(SQC_EDC_CNT3, INST_BANKB_TAG_RAM_DED_COUNT) },
	{ "SQC_INST_BANKB_BANK_RAM", SOC15_REG_ENTRY(GC, 0, regSQC_EDC_CNT3),
	  SOC15_REG_FIELD(SQC_EDC_CNT3, INST_BANKB_BANK_RAM_SEC_COUNT),
	  SOC15_REG_FIELD(SQC_EDC_CNT3, INST_BANKB_BANK_RAM_DED_COUNT) },
	{ "SQC_DATA_BANKB_TAG_RAM", SOC15_REG_ENTRY(GC, 0, regSQC_EDC_CNT3),
	  SOC15_REG_FIELD(SQC_EDC_CNT3, DATA_BANKB_TAG_RAM_SEC_COUNT),
	  SOC15_REG_FIELD(SQC_EDC_CNT3, DATA_BANKB_TAG_RAM_DED_COUNT) },
	{ "SQC_DATA_BANKB_BANK_RAM", SOC15_REG_ENTRY(GC, 0, regSQC_EDC_CNT3),
	  SOC15_REG_FIELD(SQC_EDC_CNT3, DATA_BANKB_BANK_RAM_SEC_COUNT),
	  SOC15_REG_FIELD(SQC_EDC_CNT3, DATA_BANKB_BANK_RAM_DED_COUNT) },
	{ "SQC_DATA_BANKB_DIRTY_BIT_RAM", SOC15_REG_ENTRY(GC, 0, regSQC_EDC_CNT3),
	  SOC15_REG_FIELD(SQC_EDC_CNT3, DATA_BANKB_DIRTY_BIT_RAM_SEC_COUNT),
	  SOC15_REG_FIELD(SQC_EDC_CNT3, DATA_BANKB_DIRTY_BIT_RAM_DED_COUNT) },

	/* SQC - regSQC_EDC_PARITY_CNT3 */
	{ "SQC_INST_BANKA_UTCL1_MISS_FIFO", SOC15_REG_ENTRY(GC, 0, regSQC_EDC_PARITY_CNT3),
	  SOC15_REG_FIELD(SQC_EDC_PARITY_CNT3, INST_BANKA_UTCL1_MISS_FIFO_SEC_COUNT),
	  SOC15_REG_FIELD(SQC_EDC_PARITY_CNT3, INST_BANKA_UTCL1_MISS_FIFO_DED_COUNT) },
	{ "SQC_INST_BANKA_MISS_FIFO", SOC15_REG_ENTRY(GC, 0, regSQC_EDC_PARITY_CNT3),
	  SOC15_REG_FIELD(SQC_EDC_PARITY_CNT3, INST_BANKA_MISS_FIFO_SEC_COUNT),
	  SOC15_REG_FIELD(SQC_EDC_PARITY_CNT3, INST_BANKA_MISS_FIFO_DED_COUNT) },
	{ "SQC_DATA_BANKA_HIT_FIFO", SOC15_REG_ENTRY(GC, 0, regSQC_EDC_PARITY_CNT3),
	  SOC15_REG_FIELD(SQC_EDC_PARITY_CNT3, DATA_BANKA_HIT_FIFO_SEC_COUNT),
	  SOC15_REG_FIELD(SQC_EDC_PARITY_CNT3, DATA_BANKA_HIT_FIFO_DED_COUNT) },
	{ "SQC_DATA_BANKA_MISS_FIFO", SOC15_REG_ENTRY(GC, 0, regSQC_EDC_PARITY_CNT3),
	  SOC15_REG_FIELD(SQC_EDC_PARITY_CNT3, DATA_BANKA_MISS_FIFO_SEC_COUNT),
	  SOC15_REG_FIELD(SQC_EDC_PARITY_CNT3, DATA_BANKA_MISS_FIFO_DED_COUNT) },
	{ "SQC_INST_BANKB_UTCL1_MISS_FIFO", SOC15_REG_ENTRY(GC, 0, regSQC_EDC_PARITY_CNT3),
	  SOC15_REG_FIELD(SQC_EDC_PARITY_CNT3, INST_BANKB_UTCL1_MISS_FIFO_SEC_COUNT),
	  SOC15_REG_FIELD(SQC_EDC_PARITY_CNT3, INST_BANKB_UTCL1_MISS_FIFO_DED_COUNT) },
	{ "SQC_INST_BANKB_MISS_FIFO", SOC15_REG_ENTRY(GC, 0, regSQC_EDC_PARITY_CNT3),
	  SOC15_REG_FIELD(SQC_EDC_PARITY_CNT3, INST_BANKB_MISS_FIFO_SEC_COUNT),
	  SOC15_REG_FIELD(SQC_EDC_PARITY_CNT3, INST_BANKB_MISS_FIFO_DED_COUNT) },
	{ "SQC_DATA_BANKB_HIT_FIFO", SOC15_REG_ENTRY(GC, 0, regSQC_EDC_PARITY_CNT3),
	  SOC15_REG_FIELD(SQC_EDC_PARITY_CNT3, DATA_BANKB_HIT_FIFO_SEC_COUNT),
	  SOC15_REG_FIELD(SQC_EDC_PARITY_CNT3, DATA_BANKB_HIT_FIFO_DED_COUNT) },
	{ "SQC_DATA_BANKB_MISS_FIFO", SOC15_REG_ENTRY(GC, 0, regSQC_EDC_PARITY_CNT3),
	  SOC15_REG_FIELD(SQC_EDC_PARITY_CNT3, DATA_BANKB_MISS_FIFO_SEC_COUNT),
	  SOC15_REG_FIELD(SQC_EDC_PARITY_CNT3, DATA_BANKB_MISS_FIFO_DED_COUNT) },

	/* SQ */
	{ "SQ_LDS_D", SOC15_REG_ENTRY(GC, 0, regSQ_EDC_CNT),
	  SOC15_REG_FIELD(SQ_EDC_CNT, LDS_D_SEC_COUNT),
	  SOC15_REG_FIELD(SQ_EDC_CNT, LDS_D_DED_COUNT) },
	{ "SQ_LDS_I", SOC15_REG_ENTRY(GC, 0, regSQ_EDC_CNT),
	  SOC15_REG_FIELD(SQ_EDC_CNT, LDS_I_SEC_COUNT),
	  SOC15_REG_FIELD(SQ_EDC_CNT, LDS_I_DED_COUNT) },
	{ "SQ_SGPR", SOC15_REG_ENTRY(GC, 0, regSQ_EDC_CNT),
	  SOC15_REG_FIELD(SQ_EDC_CNT, SGPR_SEC_COUNT),
	  SOC15_REG_FIELD(SQ_EDC_CNT, SGPR_DED_COUNT) },
	{ "SQ_VGPR0", SOC15_REG_ENTRY(GC, 0, regSQ_EDC_CNT),
	  SOC15_REG_FIELD(SQ_EDC_CNT, VGPR0_SEC_COUNT),
	  SOC15_REG_FIELD(SQ_EDC_CNT, VGPR0_DED_COUNT) },
	{ "SQ_VGPR1", SOC15_REG_ENTRY(GC, 0, regSQ_EDC_CNT),
	  SOC15_REG_FIELD(SQ_EDC_CNT, VGPR1_SEC_COUNT),
	  SOC15_REG_FIELD(SQ_EDC_CNT, VGPR1_DED_COUNT) },
	{ "SQ_VGPR2", SOC15_REG_ENTRY(GC, 0, regSQ_EDC_CNT),
	  SOC15_REG_FIELD(SQ_EDC_CNT, VGPR2_SEC_COUNT),
	  SOC15_REG_FIELD(SQ_EDC_CNT, VGPR2_DED_COUNT) },
	{ "SQ_VGPR3", SOC15_REG_ENTRY(GC, 0, regSQ_EDC_CNT),
	  SOC15_REG_FIELD(SQ_EDC_CNT, VGPR3_SEC_COUNT),
	  SOC15_REG_FIELD(SQ_EDC_CNT, VGPR3_DED_COUNT) },

	/* TCP */
	{ "TCP_CACHE_RAM", SOC15_REG_ENTRY(GC, 0, regTCP_EDC_CNT_NEW),
	  SOC15_REG_FIELD(TCP_EDC_CNT_NEW, CACHE_RAM_SEC_COUNT),
	  SOC15_REG_FIELD(TCP_EDC_CNT_NEW, CACHE_RAM_DED_COUNT) },
	{ "TCP_LFIFO_RAM", SOC15_REG_ENTRY(GC, 0, regTCP_EDC_CNT_NEW),
	  SOC15_REG_FIELD(TCP_EDC_CNT_NEW, LFIFO_RAM_SEC_COUNT),
	  SOC15_REG_FIELD(TCP_EDC_CNT_NEW, LFIFO_RAM_DED_COUNT) },
	{ "TCP_CMD_FIFO", SOC15_REG_ENTRY(GC, 0, regTCP_EDC_CNT_NEW),
	  SOC15_REG_FIELD(TCP_EDC_CNT_NEW, CMD_FIFO_SEC_COUNT),
	  SOC15_REG_FIELD(TCP_EDC_CNT_NEW, CMD_FIFO_DED_COUNT) },
	{ "TCP_VM_FIFO", SOC15_REG_ENTRY(GC, 0, regTCP_EDC_CNT_NEW),
	  SOC15_REG_FIELD(TCP_EDC_CNT_NEW, VM_FIFO_SEC_COUNT),
	  SOC15_REG_FIELD(TCP_EDC_CNT_NEW, VM_FIFO_DED_COUNT) },
	{ "TCP_DB_RAM", SOC15_REG_ENTRY(GC, 0, regTCP_EDC_CNT_NEW),
	  SOC15_REG_FIELD(TCP_EDC_CNT_NEW, DB_RAM_SEC_COUNT),
	  SOC15_REG_FIELD(TCP_EDC_CNT_NEW, DB_RAM_DED_COUNT) },
	{ "TCP_UTCL1_LFIFO0", SOC15_REG_ENTRY(GC, 0, regTCP_EDC_CNT_NEW),
	  SOC15_REG_FIELD(TCP_EDC_CNT_NEW, UTCL1_LFIFO0_SEC_COUNT),
	  SOC15_REG_FIELD(TCP_EDC_CNT_NEW, UTCL1_LFIFO0_DED_COUNT) },
	{ "TCP_UTCL1_LFIFO1", SOC15_REG_ENTRY(GC, 0, regTCP_EDC_CNT_NEW),
	  SOC15_REG_FIELD(TCP_EDC_CNT_NEW, UTCL1_LFIFO1_SEC_COUNT),
	  SOC15_REG_FIELD(TCP_EDC_CNT_NEW, UTCL1_LFIFO1_DED_COUNT) },

	/* TCI */
	{ "TCI_WRITE_RAM", SOC15_REG_ENTRY(GC, 0, regTCI_EDC_CNT),
	  SOC15_REG_FIELD(TCI_EDC_CNT, WRITE_RAM_SEC_COUNT),
	  SOC15_REG_FIELD(TCI_EDC_CNT, WRITE_RAM_DED_COUNT) },

	/* TCC */
	{ "TCC_CACHE_DATA", SOC15_REG_ENTRY(GC, 0, regTCC_EDC_CNT),
	  SOC15_REG_FIELD(TCC_EDC_CNT, CACHE_DATA_SEC_COUNT),
	  SOC15_REG_FIELD(TCC_EDC_CNT, CACHE_DATA_DED_COUNT) },
	{ "TCC_CACHE_DIRTY", SOC15_REG_ENTRY(GC, 0, regTCC_EDC_CNT),
	  SOC15_REG_FIELD(TCC_EDC_CNT, CACHE_DIRTY_SEC_COUNT),
	  SOC15_REG_FIELD(TCC_EDC_CNT, CACHE_DIRTY_DED_COUNT) },
	{ "TCC_HIGH_RATE_TAG", SOC15_REG_ENTRY(GC, 0, regTCC_EDC_CNT),
	  SOC15_REG_FIELD(TCC_EDC_CNT, HIGH_RATE_TAG_SEC_COUNT),
	  SOC15_REG_FIELD(TCC_EDC_CNT, HIGH_RATE_TAG_DED_COUNT) },
	{ "TCC_LOW_RATE_TAG", SOC15_REG_ENTRY(GC, 0, regTCC_EDC_CNT),
	  SOC15_REG_FIELD(TCC_EDC_CNT, LOW_RATE_TAG_SEC_COUNT),
	  SOC15_REG_FIELD(TCC_EDC_CNT, LOW_RATE_TAG_DED_COUNT) },
	{ "TCC_SRC_FIFO", SOC15_REG_ENTRY(GC, 0, regTCC_EDC_CNT),
	  SOC15_REG_FIELD(TCC_EDC_CNT, SRC_FIFO_SEC_COUNT),
	  SOC15_REG_FIELD(TCC_EDC_CNT, SRC_FIFO_DED_COUNT) },
	{ "TCC_LATENCY_FIFO", SOC15_REG_ENTRY(GC, 0, regTCC_EDC_CNT),
	  SOC15_REG_FIELD(TCC_EDC_CNT, LATENCY_FIFO_SEC_COUNT),
	  SOC15_REG_FIELD(TCC_EDC_CNT, LATENCY_FIFO_DED_COUNT) },
	{ "TCC_LATENCY_FIFO_NEXT_RAM", SOC15_REG_ENTRY(GC, 0, regTCC_EDC_CNT),
	  SOC15_REG_FIELD(TCC_EDC_CNT, LATENCY_FIFO_NEXT_RAM_SEC_COUNT),
	  SOC15_REG_FIELD(TCC_EDC_CNT, LATENCY_FIFO_NEXT_RAM_DED_COUNT) },
	{ "TCC_CACHE_TAG_PROBE_FIFO", SOC15_REG_ENTRY(GC, 0, regTCC_EDC_CNT2),
	  SOC15_REG_FIELD(TCC_EDC_CNT2, CACHE_TAG_PROBE_FIFO_SEC_COUNT),
	  SOC15_REG_FIELD(TCC_EDC_CNT2, CACHE_TAG_PROBE_FIFO_DED_COUNT) },
	{ "TCC_UC_ATOMIC_FIFO", SOC15_REG_ENTRY(GC, 0, regTCC_EDC_CNT2),
	  SOC15_REG_FIELD(TCC_EDC_CNT2, UC_ATOMIC_FIFO_SEC_COUNT),
	  SOC15_REG_FIELD(TCC_EDC_CNT2, UC_ATOMIC_FIFO_DED_COUNT) },
	{ "TCC_WRITE_CACHE_READ", SOC15_REG_ENTRY(GC, 0, regTCC_EDC_CNT2),
	  SOC15_REG_FIELD(TCC_EDC_CNT2, WRITE_CACHE_READ_SEC_COUNT),
	  SOC15_REG_FIELD(TCC_EDC_CNT2, WRITE_CACHE_READ_DED_COUNT) },
	{ "TCC_RETURN_CONTROL", SOC15_REG_ENTRY(GC, 0, regTCC_EDC_CNT2),
	  SOC15_REG_FIELD(TCC_EDC_CNT2, RETURN_CONTROL_SEC_COUNT),
	  SOC15_REG_FIELD(TCC_EDC_CNT2, RETURN_CONTROL_DED_COUNT) },
	{ "TCC_IN_USE_TRANSFER", SOC15_REG_ENTRY(GC, 0, regTCC_EDC_CNT2),
	  SOC15_REG_FIELD(TCC_EDC_CNT2, IN_USE_TRANSFER_SEC_COUNT),
	  SOC15_REG_FIELD(TCC_EDC_CNT2, IN_USE_TRANSFER_DED_COUNT) },
	{ "TCC_IN_USE_DEC", SOC15_REG_ENTRY(GC, 0, regTCC_EDC_CNT2),
	  SOC15_REG_FIELD(TCC_EDC_CNT2, IN_USE_DEC_SEC_COUNT),
	  SOC15_REG_FIELD(TCC_EDC_CNT2, IN_USE_DEC_DED_COUNT) },
	{ "TCC_WRITE_RETURN", SOC15_REG_ENTRY(GC, 0, regTCC_EDC_CNT2),
	  SOC15_REG_FIELD(TCC_EDC_CNT2, WRITE_RETURN_SEC_COUNT),
	  SOC15_REG_FIELD(TCC_EDC_CNT2, WRITE_RETURN_DED_COUNT) },
	{ "TCC_RETURN_DATA", SOC15_REG_ENTRY(GC, 0, regTCC_EDC_CNT2),
	  SOC15_REG_FIELD(TCC_EDC_CNT2, RETURN_DATA_SEC_COUNT),
	  SOC15_REG_FIELD(TCC_EDC_CNT2, RETURN_DATA_DED_COUNT) },

	/* TCA */
	{ "TCA_HOLE_FIFO", SOC15_REG_ENTRY(GC, 0, regTCA_EDC_CNT),
	  SOC15_REG_FIELD(TCA_EDC_CNT, HOLE_FIFO_SEC_COUNT),
	  SOC15_REG_FIELD(TCA_EDC_CNT, HOLE_FIFO_DED_COUNT) },
	{ "TCA_REQ_FIFO", SOC15_REG_ENTRY(GC, 0, regTCA_EDC_CNT),
	  SOC15_REG_FIELD(TCA_EDC_CNT, REQ_FIFO_SEC_COUNT),
	  SOC15_REG_FIELD(TCA_EDC_CNT, REQ_FIFO_DED_COUNT) },

	/* TCX */
	{ "TCX_GROUP0", SOC15_REG_ENTRY(GC, 0, regTCX_EDC_CNT),
	  SOC15_REG_FIELD(TCX_EDC_CNT, GROUP0_SEC_COUNT),
	  SOC15_REG_FIELD(TCX_EDC_CNT, GROUP0_DED_COUNT) },
	{ "TCX_GROUP1", SOC15_REG_ENTRY(GC, 0, regTCX_EDC_CNT),
	  SOC15_REG_FIELD(TCX_EDC_CNT, GROUP1_SEC_COUNT),
	  SOC15_REG_FIELD(TCX_EDC_CNT, GROUP1_DED_COUNT) },
	{ "TCX_GROUP2", SOC15_REG_ENTRY(GC, 0, regTCX_EDC_CNT),
	  SOC15_REG_FIELD(TCX_EDC_CNT, GROUP2_SEC_COUNT),
	  SOC15_REG_FIELD(TCX_EDC_CNT, GROUP2_DED_COUNT) },
	{ "TCX_GROUP3", SOC15_REG_ENTRY(GC, 0, regTCX_EDC_CNT),
	  SOC15_REG_FIELD(TCX_EDC_CNT, GROUP3_SEC_COUNT),
	  SOC15_REG_FIELD(TCX_EDC_CNT, GROUP3_DED_COUNT) },
	{ "TCX_GROUP4", SOC15_REG_ENTRY(GC, 0, regTCX_EDC_CNT),
	  SOC15_REG_FIELD(TCX_EDC_CNT, GROUP4_SEC_COUNT),
	  SOC15_REG_FIELD(TCX_EDC_CNT, GROUP4_DED_COUNT) },
	{ "TCX_GROUP5", SOC15_REG_ENTRY(GC, 0, regTCX_EDC_CNT),
	  SOC15_REG_FIELD(TCX_EDC_CNT, GROUP5_SED_COUNT), 0, 0 },
	{ "TCX_GROUP6", SOC15_REG_ENTRY(GC, 0, regTCX_EDC_CNT),
	  SOC15_REG_FIELD(TCX_EDC_CNT, GROUP6_SED_COUNT), 0, 0 },
	{ "TCX_GROUP7", SOC15_REG_ENTRY(GC, 0, regTCX_EDC_CNT),
	  SOC15_REG_FIELD(TCX_EDC_CNT, GROUP7_SED_COUNT), 0, 0 },
	{ "TCX_GROUP8", SOC15_REG_ENTRY(GC, 0, regTCX_EDC_CNT),
	  SOC15_REG_FIELD(TCX_EDC_CNT, GROUP8_SED_COUNT), 0, 0 },
	{ "TCX_GROUP9", SOC15_REG_ENTRY(GC, 0, regTCX_EDC_CNT),
	  SOC15_REG_FIELD(TCX_EDC_CNT, GROUP9_SED_COUNT), 0, 0 },
	{ "TCX_GROUP10", SOC15_REG_ENTRY(GC, 0, regTCX_EDC_CNT),
	  SOC15_REG_FIELD(TCX_EDC_CNT, GROUP10_SED_COUNT), 0, 0 },
	{ "TCX_GROUP11", SOC15_REG_ENTRY(GC, 0, regTCX_EDC_CNT2),
	  SOC15_REG_FIELD(TCX_EDC_CNT2, GROUP11_SED_COUNT), 0, 0 },
	{ "TCX_GROUP12", SOC15_REG_ENTRY(GC, 0, regTCX_EDC_CNT2),
	  SOC15_REG_FIELD(TCX_EDC_CNT2, GROUP12_SED_COUNT), 0, 0 },
	{ "TCX_GROUP13", SOC15_REG_ENTRY(GC, 0, regTCX_EDC_CNT2),
	  SOC15_REG_FIELD(TCX_EDC_CNT2, GROUP13_SED_COUNT), 0, 0 },
	{ "TCX_GROUP14", SOC15_REG_ENTRY(GC, 0, regTCX_EDC_CNT2),
	  SOC15_REG_FIELD(TCX_EDC_CNT2, GROUP14_SED_COUNT), 0, 0 },

	/* TD */
	{ "TD_SS_FIFO_LO", SOC15_REG_ENTRY(GC, 0, regTD_EDC_CNT),
	  SOC15_REG_FIELD(TD_EDC_CNT, SS_FIFO_LO_SEC_COUNT),
	  SOC15_REG_FIELD(TD_EDC_CNT, SS_FIFO_LO_DED_COUNT) },
	{ "TD_SS_FIFO_HI", SOC15_REG_ENTRY(GC, 0, regTD_EDC_CNT),
	  SOC15_REG_FIELD(TD_EDC_CNT, SS_FIFO_HI_SEC_COUNT),
	  SOC15_REG_FIELD(TD_EDC_CNT, SS_FIFO_HI_DED_COUNT) },
	{ "TD_CS_FIFO", SOC15_REG_ENTRY(GC, 0, regTD_EDC_CNT),
	  SOC15_REG_FIELD(TD_EDC_CNT, CS_FIFO_SEC_COUNT),
	  SOC15_REG_FIELD(TD_EDC_CNT, CS_FIFO_DED_COUNT) },

	/* TA */
	{ "TA_FS_DFIFO", SOC15_REG_ENTRY(GC, 0, regTA_EDC_CNT),
	  SOC15_REG_FIELD(TA_EDC_CNT, TA_FS_DFIFO_SEC_COUNT),
	  SOC15_REG_FIELD(TA_EDC_CNT, TA_FS_DFIFO_DED_COUNT) },
	{ "TA_FS_AFIFO_LO", SOC15_REG_ENTRY(GC, 0, regTA_EDC_CNT),
	  SOC15_REG_FIELD(TA_EDC_CNT, TA_FS_AFIFO_LO_SEC_COUNT),
	  SOC15_REG_FIELD(TA_EDC_CNT, TA_FS_AFIFO_LO_DED_COUNT) },
	{ "TA_FL_LFIFO", SOC15_REG_ENTRY(GC, 0, regTA_EDC_CNT),
	  SOC15_REG_FIELD(TA_EDC_CNT, TA_FL_LFIFO_SEC_COUNT),
	  SOC15_REG_FIELD(TA_EDC_CNT, TA_FL_LFIFO_DED_COUNT) },
	{ "TA_FX_LFIFO", SOC15_REG_ENTRY(GC, 0, regTA_EDC_CNT),
	  SOC15_REG_FIELD(TA_EDC_CNT, TA_FX_LFIFO_SEC_COUNT),
	  SOC15_REG_FIELD(TA_EDC_CNT, TA_FX_LFIFO_DED_COUNT) },
	{ "TA_FS_CFIFO", SOC15_REG_ENTRY(GC, 0, regTA_EDC_CNT),
	  SOC15_REG_FIELD(TA_EDC_CNT, TA_FS_CFIFO_SEC_COUNT),
	  SOC15_REG_FIELD(TA_EDC_CNT, TA_FS_CFIFO_DED_COUNT) },
	{ "TA_FS_AFIFO_HI", SOC15_REG_ENTRY(GC, 0, regTA_EDC_CNT),
	  SOC15_REG_FIELD(TA_EDC_CNT, TA_FS_AFIFO_HI_SEC_COUNT),
	  SOC15_REG_FIELD(TA_EDC_CNT, TA_FS_AFIFO_HI_DED_COUNT) },

	/* EA - regGCEA_EDC_CNT */
	{ "EA_DRAMRD_CMDMEM", SOC15_REG_ENTRY(GC, 0, regGCEA_EDC_CNT),
	  SOC15_REG_FIELD(GCEA_EDC_CNT, DRAMRD_CMDMEM_SEC_COUNT),
	  SOC15_REG_FIELD(GCEA_EDC_CNT, DRAMRD_CMDMEM_DED_COUNT) },
	{ "EA_DRAMWR_CMDMEM", SOC15_REG_ENTRY(GC, 0, regGCEA_EDC_CNT),
	  SOC15_REG_FIELD(GCEA_EDC_CNT, DRAMWR_CMDMEM_SEC_COUNT),
	  SOC15_REG_FIELD(GCEA_EDC_CNT, DRAMWR_CMDMEM_DED_COUNT) },
	{ "EA_DRAMWR_DATAMEM", SOC15_REG_ENTRY(GC, 0, regGCEA_EDC_CNT),
	  SOC15_REG_FIELD(GCEA_EDC_CNT, DRAMWR_DATAMEM_SEC_COUNT),
	  SOC15_REG_FIELD(GCEA_EDC_CNT, DRAMWR_DATAMEM_DED_COUNT) },
	{ "EA_RRET_TAGMEM", SOC15_REG_ENTRY(GC, 0, regGCEA_EDC_CNT),
	  SOC15_REG_FIELD(GCEA_EDC_CNT, RRET_TAGMEM_SEC_COUNT),
	  SOC15_REG_FIELD(GCEA_EDC_CNT, RRET_TAGMEM_DED_COUNT) },
	{ "EA_WRET_TAGMEM", SOC15_REG_ENTRY(GC, 0, regGCEA_EDC_CNT),
	  SOC15_REG_FIELD(GCEA_EDC_CNT, WRET_TAGMEM_SEC_COUNT),
	  SOC15_REG_FIELD(GCEA_EDC_CNT, WRET_TAGMEM_DED_COUNT) },
	{ "EA_IOWR_DATAMEM", SOC15_REG_ENTRY(GC, 0, regGCEA_EDC_CNT),
	  SOC15_REG_FIELD(GCEA_EDC_CNT, IOWR_DATAMEM_SEC_COUNT),
	  SOC15_REG_FIELD(GCEA_EDC_CNT, IOWR_DATAMEM_DED_COUNT) },
	{ "EA_DRAMRD_PAGEMEM", SOC15_REG_ENTRY(GC, 0, regGCEA_EDC_CNT),
	  SOC15_REG_FIELD(GCEA_EDC_CNT, DRAMRD_PAGEMEM_SED_COUNT), 0, 0 },
	{ "EA_DRAMWR_PAGEMEM", SOC15_REG_ENTRY(GC, 0, regGCEA_EDC_CNT),
	  SOC15_REG_FIELD(GCEA_EDC_CNT, DRAMWR_PAGEMEM_SED_COUNT), 0, 0 },
	{ "EA_IORD_CMDMEM", SOC15_REG_ENTRY(GC, 0, regGCEA_EDC_CNT),
	  SOC15_REG_FIELD(GCEA_EDC_CNT, IORD_CMDMEM_SED_COUNT), 0, 0 },
	{ "EA_IOWR_CMDMEM", SOC15_REG_ENTRY(GC, 0, regGCEA_EDC_CNT),
	  SOC15_REG_FIELD(GCEA_EDC_CNT, IOWR_CMDMEM_SED_COUNT), 0, 0 },

	/* EA - regGCEA_EDC_CNT2 */
	{ "EA_GMIRD_CMDMEM", SOC15_REG_ENTRY(GC, 0, regGCEA_EDC_CNT2),
	  SOC15_REG_FIELD(GCEA_EDC_CNT2, GMIRD_CMDMEM_SEC_COUNT),
	  SOC15_REG_FIELD(GCEA_EDC_CNT2, GMIRD_CMDMEM_DED_COUNT) },
	{ "EA_GMIWR_CMDMEM", SOC15_REG_ENTRY(GC, 0, regGCEA_EDC_CNT2),
	  SOC15_REG_FIELD(GCEA_EDC_CNT2, GMIWR_CMDMEM_SEC_COUNT),
	  SOC15_REG_FIELD(GCEA_EDC_CNT2, GMIWR_CMDMEM_DED_COUNT) },
	{ "EA_GMIWR_DATAMEM", SOC15_REG_ENTRY(GC, 0, regGCEA_EDC_CNT2),
	  SOC15_REG_FIELD(GCEA_EDC_CNT2, GMIWR_DATAMEM_SEC_COUNT),
	  SOC15_REG_FIELD(GCEA_EDC_CNT2, GMIWR_DATAMEM_DED_COUNT) },
	{ "EA_GMIRD_PAGEMEM", SOC15_REG_ENTRY(GC, 0, regGCEA_EDC_CNT2),
	  SOC15_REG_FIELD(GCEA_EDC_CNT2, GMIRD_PAGEMEM_SED_COUNT), 0, 0 },
	{ "EA_GMIWR_PAGEMEM", SOC15_REG_ENTRY(GC, 0, regGCEA_EDC_CNT2),
	  SOC15_REG_FIELD(GCEA_EDC_CNT2, GMIWR_PAGEMEM_SED_COUNT), 0, 0 },
	{ "EA_MAM_D0MEM", SOC15_REG_ENTRY(GC, 0, regGCEA_EDC_CNT2),
	  SOC15_REG_FIELD(GCEA_EDC_CNT2, MAM_D0MEM_SED_COUNT),
	  SOC15_REG_FIELD(GCEA_EDC_CNT2, MAM_D0MEM_DED_COUNT) },
	{ "EA_MAM_D1MEM", SOC15_REG_ENTRY(GC, 0, regGCEA_EDC_CNT2),
	  SOC15_REG_FIELD(GCEA_EDC_CNT2, MAM_D1MEM_SED_COUNT),
	  SOC15_REG_FIELD(GCEA_EDC_CNT2, MAM_D1MEM_DED_COUNT) },
	{ "EA_MAM_D2MEM", SOC15_REG_ENTRY(GC, 0, regGCEA_EDC_CNT2),
	  SOC15_REG_FIELD(GCEA_EDC_CNT2, MAM_D2MEM_SED_COUNT),
	  SOC15_REG_FIELD(GCEA_EDC_CNT2, MAM_D2MEM_DED_COUNT) },
	{ "EA_MAM_D3MEM", SOC15_REG_ENTRY(GC, 0, regGCEA_EDC_CNT2),
	  SOC15_REG_FIELD(GCEA_EDC_CNT2, MAM_D3MEM_SED_COUNT),
	  SOC15_REG_FIELD(GCEA_EDC_CNT2, MAM_D3MEM_DED_COUNT) },

	/* EA - regGCEA_EDC_CNT3 */
	{ "EA_DRAMRD_PAGEMEM", SOC15_REG_ENTRY(GC, 0, regGCEA_EDC_CNT3), 0, 0,
	  SOC15_REG_FIELD(GCEA_EDC_CNT3, DRAMRD_PAGEMEM_DED_COUNT) },
	{ "EA_DRAMWR_PAGEMEM", SOC15_REG_ENTRY(GC, 0, regGCEA_EDC_CNT3), 0, 0,
	  SOC15_REG_FIELD(GCEA_EDC_CNT3, DRAMWR_PAGEMEM_DED_COUNT) },
	{ "EA_IORD_CMDMEM", SOC15_REG_ENTRY(GC, 0, regGCEA_EDC_CNT3), 0, 0,
	  SOC15_REG_FIELD(GCEA_EDC_CNT3, IORD_CMDMEM_DED_COUNT) },
	{ "EA_IOWR_CMDMEM", SOC15_REG_ENTRY(GC, 0, regGCEA_EDC_CNT3), 0, 0,
	  SOC15_REG_FIELD(GCEA_EDC_CNT3, IOWR_CMDMEM_DED_COUNT) },
	{ "EA_GMIRD_PAGEMEM", SOC15_REG_ENTRY(GC, 0, regGCEA_EDC_CNT3), 0, 0,
	  SOC15_REG_FIELD(GCEA_EDC_CNT3, GMIRD_PAGEMEM_DED_COUNT) },
	{ "EA_GMIWR_PAGEMEM", SOC15_REG_ENTRY(GC, 0, regGCEA_EDC_CNT3), 0, 0,
	  SOC15_REG_FIELD(GCEA_EDC_CNT3, GMIWR_PAGEMEM_DED_COUNT) },
	{ "EA_MAM_A0MEM", SOC15_REG_ENTRY(GC, 0, regGCEA_EDC_CNT3),
	  SOC15_REG_FIELD(GCEA_EDC_CNT3, MAM_A0MEM_SEC_COUNT),
	  SOC15_REG_FIELD(GCEA_EDC_CNT3, MAM_A0MEM_DED_COUNT) },
	{ "EA_MAM_A1MEM", SOC15_REG_ENTRY(GC, 0, regGCEA_EDC_CNT3),
	  SOC15_REG_FIELD(GCEA_EDC_CNT3, MAM_A1MEM_SEC_COUNT),
	  SOC15_REG_FIELD(GCEA_EDC_CNT3, MAM_A1MEM_DED_COUNT) },
	{ "EA_MAM_A2MEM", SOC15_REG_ENTRY(GC, 0, regGCEA_EDC_CNT3),
	  SOC15_REG_FIELD(GCEA_EDC_CNT3, MAM_A2MEM_SEC_COUNT),
	  SOC15_REG_FIELD(GCEA_EDC_CNT3, MAM_A2MEM_DED_COUNT) },
	{ "EA_MAM_A3MEM", SOC15_REG_ENTRY(GC, 0, regGCEA_EDC_CNT3),
	  SOC15_REG_FIELD(GCEA_EDC_CNT3, MAM_A3MEM_SEC_COUNT),
	  SOC15_REG_FIELD(GCEA_EDC_CNT3, MAM_A3MEM_DED_COUNT) },
	{ "EA_MAM_AFMEM", SOC15_REG_ENTRY(GC, 0, regGCEA_EDC_CNT3),
	  SOC15_REG_FIELD(GCEA_EDC_CNT3, MAM_AFMEM_SEC_COUNT),
	  SOC15_REG_FIELD(GCEA_EDC_CNT3, MAM_AFMEM_DED_COUNT) },
};

static const char * const vml2_walker_mems[] = {
	"UTC_VML2_CACHE_PDE0_MEM0",
	"UTC_VML2_CACHE_PDE0_MEM1",
	"UTC_VML2_CACHE_PDE1_MEM0",
	"UTC_VML2_CACHE_PDE1_MEM1",
	"UTC_VML2_CACHE_PDE2_MEM0",
	"UTC_VML2_CACHE_PDE2_MEM1",
	"UTC_VML2_RDIF_ARADDRS",
	"UTC_VML2_RDIF_LOG_FIFO",
	"UTC_VML2_QUEUE_REQ",
	"UTC_VML2_QUEUE_RET",
};

static struct gfx_v9_4_2_utc_block gfx_v9_4_2_utc_blocks[] = {
	{ VML2_MEM, 8, 2, 2,
	  { SOC15_REG_ENTRY(GC, 0, regVML2_MEM_ECC_INDEX) },
	  { SOC15_REG_ENTRY(GC, 0, regVML2_MEM_ECC_CNTL) },
	  SOC15_REG_FIELD(VML2_MEM_ECC_CNTL, SEC_COUNT),
	  SOC15_REG_FIELD(VML2_MEM_ECC_CNTL, DED_COUNT),
	  REG_SET_FIELD(0, VML2_MEM_ECC_CNTL, WRITE_COUNTERS, 1) },
	{ VML2_WALKER_MEM, ARRAY_SIZE(vml2_walker_mems), 1, 1,
	  { SOC15_REG_ENTRY(GC, 0, regVML2_WALKER_MEM_ECC_INDEX) },
	  { SOC15_REG_ENTRY(GC, 0, regVML2_WALKER_MEM_ECC_CNTL) },
	  SOC15_REG_FIELD(VML2_WALKER_MEM_ECC_CNTL, SEC_COUNT),
	  SOC15_REG_FIELD(VML2_WALKER_MEM_ECC_CNTL, DED_COUNT),
	  REG_SET_FIELD(0, VML2_WALKER_MEM_ECC_CNTL, WRITE_COUNTERS, 1) },
	{ UTCL2_MEM, 18, 1, 2,
	  { SOC15_REG_ENTRY(GC, 0, regUTCL2_MEM_ECC_INDEX) },
	  { SOC15_REG_ENTRY(GC, 0, regUTCL2_MEM_ECC_CNTL) },
	  SOC15_REG_FIELD(UTCL2_MEM_ECC_CNTL, SEC_COUNT),
	  SOC15_REG_FIELD(UTCL2_MEM_ECC_CNTL, DED_COUNT),
	  REG_SET_FIELD(0, UTCL2_MEM_ECC_CNTL, WRITE_COUNTERS, 1) },
	{ ATC_L2_CACHE_2M, 8, 2, 1,
	  { SOC15_REG_ENTRY(GC, 0, regATC_L2_CACHE_2M_DSM_INDEX) },
	  { SOC15_REG_ENTRY(GC, 0, regATC_L2_CACHE_2M_DSM_CNTL) },
	  SOC15_REG_FIELD(ATC_L2_CACHE_2M_DSM_CNTL, SEC_COUNT),
	  SOC15_REG_FIELD(ATC_L2_CACHE_2M_DSM_CNTL, DED_COUNT),
	  REG_SET_FIELD(0, ATC_L2_CACHE_2M_DSM_CNTL, WRITE_COUNTERS, 1) },
	{ ATC_L2_CACHE_32K, 8, 2, 2,
	  { SOC15_REG_ENTRY(GC, 0, regATC_L2_CACHE_32K_DSM_INDEX) },
	  { SOC15_REG_ENTRY(GC, 0, regATC_L2_CACHE_32K_DSM_CNTL) },
	  SOC15_REG_FIELD(ATC_L2_CACHE_32K_DSM_CNTL, SEC_COUNT),
	  SOC15_REG_FIELD(ATC_L2_CACHE_32K_DSM_CNTL, DED_COUNT),
	  REG_SET_FIELD(0, ATC_L2_CACHE_32K_DSM_CNTL, WRITE_COUNTERS, 1) },
	{ ATC_L2_CACHE_4K, 8, 2, 8,
	  { SOC15_REG_ENTRY(GC, 0, regATC_L2_CACHE_4K_DSM_INDEX) },
	  { SOC15_REG_ENTRY(GC, 0, regATC_L2_CACHE_4K_DSM_CNTL) },
	  SOC15_REG_FIELD(ATC_L2_CACHE_4K_DSM_CNTL, SEC_COUNT),
	  SOC15_REG_FIELD(ATC_L2_CACHE_4K_DSM_CNTL, DED_COUNT),
	  REG_SET_FIELD(0, ATC_L2_CACHE_4K_DSM_CNTL, WRITE_COUNTERS, 1) },
};

static const struct soc15_reg_entry gfx_v9_4_2_ea_err_status_regs = {
	SOC15_REG_ENTRY(GC, 0, regGCEA_ERR_STATUS), 0, 1, 16
};

static int gfx_v9_4_2_get_reg_error_count(struct amdgpu_device *adev,
					  const struct soc15_reg_entry *reg,
					  uint32_t se_id, uint32_t inst_id,
					  uint32_t value, uint32_t *sec_count,
					  uint32_t *ded_count)
{
	uint32_t i;
	uint32_t sec_cnt, ded_cnt;

	for (i = 0; i < ARRAY_SIZE(gfx_v9_4_2_ras_fields); i++) {
		if (gfx_v9_4_2_ras_fields[i].reg_offset != reg->reg_offset ||
		    gfx_v9_4_2_ras_fields[i].seg != reg->seg ||
		    gfx_v9_4_2_ras_fields[i].inst != reg->inst)
			continue;

		sec_cnt = SOC15_RAS_REG_FIELD_VAL(
			value, gfx_v9_4_2_ras_fields[i], sec);
		if (sec_cnt) {
			dev_info(adev->dev,
				 "GFX SubBlock %s, Instance[%d][%d], SEC %d\n",
				 gfx_v9_4_2_ras_fields[i].name, se_id, inst_id,
				 sec_cnt);
			*sec_count += sec_cnt;
		}

		ded_cnt = SOC15_RAS_REG_FIELD_VAL(
			value, gfx_v9_4_2_ras_fields[i], ded);
		if (ded_cnt) {
			dev_info(adev->dev,
				 "GFX SubBlock %s, Instance[%d][%d], DED %d\n",
				 gfx_v9_4_2_ras_fields[i].name, se_id, inst_id,
				 ded_cnt);
			*ded_count += ded_cnt;
		}
	}

	return 0;
}

static int gfx_v9_4_2_query_sram_edc_count(struct amdgpu_device *adev,
				uint32_t *sec_count, uint32_t *ded_count)
{
	uint32_t i, j, k, data;
	uint32_t sec_cnt = 0, ded_cnt = 0;

	if (sec_count && ded_count) {
		*sec_count = 0;
		*ded_count = 0;
	}

	mutex_lock(&adev->grbm_idx_mutex);

	for (i = 0; i < ARRAY_SIZE(gfx_v9_4_2_edc_counter_regs); i++) {
		for (j = 0; j < gfx_v9_4_2_edc_counter_regs[i].se_num; j++) {
			for (k = 0; k < gfx_v9_4_2_edc_counter_regs[i].instance;
			     k++) {
				gfx_v9_4_2_select_se_sh(adev, j, 0, k);

				/* if sec/ded_count is null, just clear counter */
				if (!sec_count || !ded_count) {
					WREG32(SOC15_REG_ENTRY_OFFSET(
						gfx_v9_4_2_edc_counter_regs[i]), 0);
					continue;
				}

				data = RREG32(SOC15_REG_ENTRY_OFFSET(
					gfx_v9_4_2_edc_counter_regs[i]));

				if (!data)
					continue;

				gfx_v9_4_2_get_reg_error_count(adev,
					&gfx_v9_4_2_edc_counter_regs[i],
					j, k, data, &sec_cnt, &ded_cnt);

				/* clear counter after read */
				WREG32(SOC15_REG_ENTRY_OFFSET(
					gfx_v9_4_2_edc_counter_regs[i]), 0);
			}
		}
	}

	if (sec_count && ded_count) {
		*sec_count += sec_cnt;
		*ded_count += ded_cnt;
	}

	gfx_v9_4_2_select_se_sh(adev, 0xffffffff, 0xffffffff, 0xffffffff);
	mutex_unlock(&adev->grbm_idx_mutex);

	return 0;
}

static void gfx_v9_4_2_log_utc_edc_count(struct amdgpu_device *adev,
					 struct gfx_v9_4_2_utc_block *blk,
					 uint32_t instance, uint32_t sec_cnt,
					 uint32_t ded_cnt)
{
	uint32_t bank, way, mem;
	static const char *vml2_way_str[] = { "BIGK", "4K" };
	static const char *utcl2_rounter_str[] = { "VMC", "APT" };

	mem = instance % blk->num_mem_blocks;
	way = (instance / blk->num_mem_blocks) % blk->num_ways;
	bank = instance / (blk->num_mem_blocks * blk->num_ways);

	switch (blk->type) {
	case VML2_MEM:
		dev_info(
			adev->dev,
			"GFX SubBlock UTC_VML2_BANK_CACHE_%d_%s_MEM%d, SED %d, DED %d\n",
			bank, vml2_way_str[way], mem, sec_cnt, ded_cnt);
		break;
	case VML2_WALKER_MEM:
		dev_info(adev->dev, "GFX SubBlock %s, SED %d, DED %d\n",
			 vml2_walker_mems[bank], sec_cnt, ded_cnt);
		break;
	case UTCL2_MEM:
		dev_info(
			adev->dev,
			"GFX SubBlock UTCL2_ROUTER_IFIF%d_GROUP0_%s, SED %d, DED %d\n",
			bank, utcl2_rounter_str[mem], sec_cnt, ded_cnt);
		break;
	case ATC_L2_CACHE_2M:
		dev_info(
			adev->dev,
			"GFX SubBlock UTC_ATCL2_CACHE_2M_BANK%d_WAY%d_MEM, SED %d, DED %d\n",
			bank, way, sec_cnt, ded_cnt);
		break;
	case ATC_L2_CACHE_32K:
		dev_info(
			adev->dev,
			"GFX SubBlock UTC_ATCL2_CACHE_32K_BANK%d_WAY%d_MEM%d, SED %d, DED %d\n",
			bank, way, mem, sec_cnt, ded_cnt);
		break;
	case ATC_L2_CACHE_4K:
		dev_info(
			adev->dev,
			"GFX SubBlock UTC_ATCL2_CACHE_4K_BANK%d_WAY%d_MEM%d, SED %d, DED %d\n",
			bank, way, mem, sec_cnt, ded_cnt);
		break;
	}
}

static int gfx_v9_4_2_query_utc_edc_count(struct amdgpu_device *adev,
					  uint32_t *sec_count,
					  uint32_t *ded_count)
{
	uint32_t i, j, data;
	uint32_t sec_cnt, ded_cnt;
	uint32_t num_instances;
	struct gfx_v9_4_2_utc_block *blk;

	if (sec_count && ded_count) {
		*sec_count = 0;
		*ded_count = 0;
	}

	for (i = 0; i < ARRAY_SIZE(gfx_v9_4_2_utc_blocks); i++) {
		blk = &gfx_v9_4_2_utc_blocks[i];
		num_instances =
			blk->num_banks * blk->num_ways * blk->num_mem_blocks;
		for (j = 0; j < num_instances; j++) {
			WREG32(SOC15_REG_ENTRY_OFFSET(blk->idx_reg), j);

			/* if sec/ded_count is NULL, just clear counter */
			if (!sec_count || !ded_count) {
				WREG32(SOC15_REG_ENTRY_OFFSET(blk->data_reg),
				       blk->clear);
				continue;
			}

			data = RREG32(SOC15_REG_ENTRY_OFFSET(blk->data_reg));
			if (!data)
				continue;

			sec_cnt = SOC15_RAS_REG_FIELD_VAL(data, *blk, sec);
			*sec_count += sec_cnt;
			ded_cnt = SOC15_RAS_REG_FIELD_VAL(data, *blk, ded);
			*ded_count += ded_cnt;

			/* clear counter after read */
			WREG32(SOC15_REG_ENTRY_OFFSET(blk->data_reg),
			       blk->clear);

			/* print the edc count */
			if (sec_cnt || ded_cnt)
				gfx_v9_4_2_log_utc_edc_count(adev, blk, j, sec_cnt,
							     ded_cnt);
		}
	}

	return 0;
}

static void gfx_v9_4_2_query_ras_error_count(struct amdgpu_device *adev,
					    void *ras_error_status)
{
	struct ras_err_data *err_data = (struct ras_err_data *)ras_error_status;
	uint32_t sec_count = 0, ded_count = 0;

	if (!amdgpu_ras_is_supported(adev, AMDGPU_RAS_BLOCK__GFX))
		return;

	err_data->ue_count = 0;
	err_data->ce_count = 0;

	gfx_v9_4_2_query_sram_edc_count(adev, &sec_count, &ded_count);
	err_data->ce_count += sec_count;
	err_data->ue_count += ded_count;

	gfx_v9_4_2_query_utc_edc_count(adev, &sec_count, &ded_count);
	err_data->ce_count += sec_count;
	err_data->ue_count += ded_count;

}

static void gfx_v9_4_2_reset_utc_err_status(struct amdgpu_device *adev)
{
	WREG32_SOC15(GC, 0, regUTCL2_MEM_ECC_STATUS, 0x3);
	WREG32_SOC15(GC, 0, regVML2_MEM_ECC_STATUS, 0x3);
	WREG32_SOC15(GC, 0, regVML2_WALKER_MEM_ECC_STATUS, 0x3);
}

static void gfx_v9_4_2_reset_ea_err_status(struct amdgpu_device *adev)
{
	uint32_t i, j;
	uint32_t value;

	mutex_lock(&adev->grbm_idx_mutex);
	for (i = 0; i < gfx_v9_4_2_ea_err_status_regs.se_num; i++) {
		for (j = 0; j < gfx_v9_4_2_ea_err_status_regs.instance;
		     j++) {
			gfx_v9_4_2_select_se_sh(adev, i, 0, j);
			value = RREG32(SOC15_REG_ENTRY_OFFSET(
				gfx_v9_4_2_ea_err_status_regs));
			value = REG_SET_FIELD(value, GCEA_ERR_STATUS, CLEAR_ERROR_STATUS, 0x1);
			WREG32(SOC15_REG_ENTRY_OFFSET(gfx_v9_4_2_ea_err_status_regs), value);
		}
	}
	gfx_v9_4_2_select_se_sh(adev, 0xffffffff, 0xffffffff, 0xffffffff);
	mutex_unlock(&adev->grbm_idx_mutex);
}

static void gfx_v9_4_2_reset_ras_error_count(struct amdgpu_device *adev)
{
	if (!amdgpu_ras_is_supported(adev, AMDGPU_RAS_BLOCK__GFX))
		return;

	gfx_v9_4_2_query_sram_edc_count(adev, NULL, NULL);
	gfx_v9_4_2_query_utc_edc_count(adev, NULL, NULL);
}

static int gfx_v9_4_2_ras_error_inject(struct amdgpu_device *adev, void *inject_if)
{
	struct ras_inject_if *info = (struct ras_inject_if *)inject_if;
	int ret;
	struct ta_ras_trigger_error_input block_info = { 0 };

	if (!amdgpu_ras_is_supported(adev, AMDGPU_RAS_BLOCK__GFX))
		return -EINVAL;

	block_info.block_id = amdgpu_ras_block_to_ta(info->head.block);
	block_info.sub_block_index = info->head.sub_block_index;
	block_info.inject_error_type = amdgpu_ras_error_to_ta(info->head.type);
	block_info.address = info->address;
	block_info.value = info->value;

	mutex_lock(&adev->grbm_idx_mutex);
	ret = psp_ras_trigger_error(&adev->psp, &block_info);
	mutex_unlock(&adev->grbm_idx_mutex);

	return ret;
}

static void gfx_v9_4_2_query_ea_err_status(struct amdgpu_device *adev)
{
	uint32_t i, j;
	uint32_t reg_value;

	mutex_lock(&adev->grbm_idx_mutex);

	for (i = 0; i < gfx_v9_4_2_ea_err_status_regs.se_num; i++) {
		for (j = 0; j < gfx_v9_4_2_ea_err_status_regs.instance;
		     j++) {
			gfx_v9_4_2_select_se_sh(adev, i, 0, j);
			reg_value = RREG32(SOC15_REG_ENTRY_OFFSET(
				gfx_v9_4_2_ea_err_status_regs));

			if (REG_GET_FIELD(reg_value, GCEA_ERR_STATUS, SDP_RDRSP_STATUS) ||
			    REG_GET_FIELD(reg_value, GCEA_ERR_STATUS, SDP_WRRSP_STATUS) ||
			    REG_GET_FIELD(reg_value, GCEA_ERR_STATUS, SDP_RDRSP_DATAPARITY_ERROR)) {
				dev_warn(adev->dev, "GCEA err detected at instance: %d, status: 0x%x!\n",
						j, reg_value);
			}
			/* clear after read */
			reg_value = REG_SET_FIELD(reg_value, GCEA_ERR_STATUS,
						  CLEAR_ERROR_STATUS, 0x1);
			WREG32(SOC15_REG_ENTRY_OFFSET(gfx_v9_4_2_ea_err_status_regs), reg_value);
		}
	}

	gfx_v9_4_2_select_se_sh(adev, 0xffffffff, 0xffffffff, 0xffffffff);
	mutex_unlock(&adev->grbm_idx_mutex);
}

static void gfx_v9_4_2_query_utc_err_status(struct amdgpu_device *adev)
{
	uint32_t data;

	data = RREG32_SOC15(GC, 0, regUTCL2_MEM_ECC_STATUS);
	if (data) {
		dev_warn(adev->dev, "GFX UTCL2 Mem Ecc Status: 0x%x!\n", data);
		WREG32_SOC15(GC, 0, regUTCL2_MEM_ECC_STATUS, 0x3);
	}

	data = RREG32_SOC15(GC, 0, regVML2_MEM_ECC_STATUS);
	if (data) {
		dev_warn(adev->dev, "GFX VML2 Mem Ecc Status: 0x%x!\n", data);
		WREG32_SOC15(GC, 0, regVML2_MEM_ECC_STATUS, 0x3);
	}

	data = RREG32_SOC15(GC, 0, regVML2_WALKER_MEM_ECC_STATUS);
	if (data) {
		dev_warn(adev->dev, "GFX VML2 Walker Mem Ecc Status: 0x%x!\n", data);
		WREG32_SOC15(GC, 0, regVML2_WALKER_MEM_ECC_STATUS, 0x3);
	}
}

static void gfx_v9_4_2_query_ras_error_status(struct amdgpu_device *adev)
{
	if (!amdgpu_ras_is_supported(adev, AMDGPU_RAS_BLOCK__GFX))
		return;

	gfx_v9_4_2_query_ea_err_status(adev);
	gfx_v9_4_2_query_utc_err_status(adev);
	gfx_v9_4_2_query_sq_timeout_status(adev);
}

static void gfx_v9_4_2_reset_ras_error_status(struct amdgpu_device *adev)
{
	if (!amdgpu_ras_is_supported(adev, AMDGPU_RAS_BLOCK__GFX))
		return;

	gfx_v9_4_2_reset_utc_err_status(adev);
	gfx_v9_4_2_reset_ea_err_status(adev);
	gfx_v9_4_2_reset_sq_timeout_status(adev);
}

static void gfx_v9_4_2_enable_watchdog_timer(struct amdgpu_device *adev)
{
	uint32_t i;
	uint32_t data;

	data = REG_SET_FIELD(0, SQ_TIMEOUT_CONFIG, TIMEOUT_FATAL_DISABLE,
			     amdgpu_watchdog_timer.timeout_fatal_disable ? 1 :
									   0);

	if (amdgpu_watchdog_timer.timeout_fatal_disable &&
	    (amdgpu_watchdog_timer.period < 1 ||
	     amdgpu_watchdog_timer.period > 0x23)) {
		dev_warn(adev->dev, "Watchdog period range is 1 to 0x23\n");
		amdgpu_watchdog_timer.period = 0x23;
	}
	data = REG_SET_FIELD(data, SQ_TIMEOUT_CONFIG, PERIOD_SEL,
			     amdgpu_watchdog_timer.period);

	mutex_lock(&adev->grbm_idx_mutex);
	for (i = 0; i < adev->gfx.config.max_shader_engines; i++) {
		gfx_v9_4_2_select_se_sh(adev, i, 0xffffffff, 0xffffffff);
		WREG32_SOC15(GC, 0, regSQ_TIMEOUT_CONFIG, data);
	}
	gfx_v9_4_2_select_se_sh(adev, 0xffffffff, 0xffffffff, 0xffffffff);
	mutex_unlock(&adev->grbm_idx_mutex);
}

static uint32_t wave_read_ind(struct amdgpu_device *adev, uint32_t simd, uint32_t wave, uint32_t address)
{
	WREG32_SOC15_RLC_EX(reg, GC, 0, regSQ_IND_INDEX,
		(wave << SQ_IND_INDEX__WAVE_ID__SHIFT) |
		(simd << SQ_IND_INDEX__SIMD_ID__SHIFT) |
		(address << SQ_IND_INDEX__INDEX__SHIFT) |
		(SQ_IND_INDEX__FORCE_READ_MASK));
	return RREG32_SOC15(GC, 0, regSQ_IND_DATA);
}

static void gfx_v9_4_2_log_cu_timeout_status(struct amdgpu_device *adev,
					uint32_t status)
{
	struct amdgpu_cu_info *cu_info = &adev->gfx.cu_info;
	uint32_t i, simd, wave;
	uint32_t wave_status;
	uint32_t wave_pc_lo, wave_pc_hi;
	uint32_t wave_exec_lo, wave_exec_hi;
	uint32_t wave_inst_dw0, wave_inst_dw1;
	uint32_t wave_ib_sts;

	for (i = 0; i < 32; i++) {
		if (!((i << 1) & status))
			continue;

		simd = i / cu_info->max_waves_per_simd;
		wave = i % cu_info->max_waves_per_simd;

		wave_status = wave_read_ind(adev, simd, wave, ixSQ_WAVE_STATUS);
		wave_pc_lo = wave_read_ind(adev, simd, wave, ixSQ_WAVE_PC_LO);
		wave_pc_hi = wave_read_ind(adev, simd, wave, ixSQ_WAVE_PC_HI);
		wave_exec_lo =
			wave_read_ind(adev, simd, wave, ixSQ_WAVE_EXEC_LO);
		wave_exec_hi =
			wave_read_ind(adev, simd, wave, ixSQ_WAVE_EXEC_HI);
		wave_inst_dw0 =
			wave_read_ind(adev, simd, wave, ixSQ_WAVE_INST_DW0);
		wave_inst_dw1 =
			wave_read_ind(adev, simd, wave, ixSQ_WAVE_INST_DW1);
		wave_ib_sts = wave_read_ind(adev, simd, wave, ixSQ_WAVE_IB_STS);

		dev_info(
			adev->dev,
			"\t SIMD %d, Wave %d: status 0x%x, pc 0x%llx, exec 0x%llx, inst 0x%llx, ib_sts 0x%x\n",
			simd, wave, wave_status,
			((uint64_t)wave_pc_hi << 32 | wave_pc_lo),
			((uint64_t)wave_exec_hi << 32 | wave_exec_lo),
			((uint64_t)wave_inst_dw1 << 32 | wave_inst_dw0),
			wave_ib_sts);
	}
}

static void gfx_v9_4_2_query_sq_timeout_status(struct amdgpu_device *adev)
{
	uint32_t se_idx, sh_idx, cu_idx;
	uint32_t status;

	mutex_lock(&adev->grbm_idx_mutex);
	for (se_idx = 0; se_idx < adev->gfx.config.max_shader_engines;
	     se_idx++) {
		for (sh_idx = 0; sh_idx < adev->gfx.config.max_sh_per_se;
		     sh_idx++) {
			for (cu_idx = 0;
			     cu_idx < adev->gfx.config.max_cu_per_sh;
			     cu_idx++) {
				gfx_v9_4_2_select_se_sh(adev, se_idx, sh_idx,
							cu_idx);
				status = RREG32_SOC15(GC, 0,
						      regSQ_TIMEOUT_STATUS);
				if (status != 0) {
					dev_info(
						adev->dev,
						"GFX Watchdog Timeout: SE %d, SH %d, CU %d\n",
						se_idx, sh_idx, cu_idx);
					gfx_v9_4_2_log_cu_timeout_status(
						adev, status);
				}
				/* clear old status */
				WREG32_SOC15(GC, 0, regSQ_TIMEOUT_STATUS, 0);
			}
		}
	}
	gfx_v9_4_2_select_se_sh(adev, 0xffffffff, 0xffffffff, 0xffffffff);
	mutex_unlock(&adev->grbm_idx_mutex);
}

static void gfx_v9_4_2_reset_sq_timeout_status(struct amdgpu_device *adev)
{
	uint32_t se_idx, sh_idx, cu_idx;

	mutex_lock(&adev->grbm_idx_mutex);
	for (se_idx = 0; se_idx < adev->gfx.config.max_shader_engines;
	     se_idx++) {
		for (sh_idx = 0; sh_idx < adev->gfx.config.max_sh_per_se;
		     sh_idx++) {
			for (cu_idx = 0;
			     cu_idx < adev->gfx.config.max_cu_per_sh;
			     cu_idx++) {
				gfx_v9_4_2_select_se_sh(adev, se_idx, sh_idx,
							cu_idx);
				WREG32_SOC15(GC, 0, regSQ_TIMEOUT_STATUS, 0);
			}
		}
	}
	gfx_v9_4_2_select_se_sh(adev, 0xffffffff, 0xffffffff, 0xffffffff);
	mutex_unlock(&adev->grbm_idx_mutex);
}

static bool gfx_v9_4_2_query_uctl2_poison_status(struct amdgpu_device *adev)
{
	u32 status = 0;
	struct amdgpu_vmhub *hub;

	hub = &adev->vmhub[AMDGPU_GFXHUB_0];
	status = RREG32(hub->vm_l2_pro_fault_status);
	/* reset page fault status */
	WREG32_P(hub->vm_l2_pro_fault_cntl, 1, ~1);

	return REG_GET_FIELD(status, VM_L2_PROTECTION_FAULT_STATUS, FED);
}

struct amdgpu_ras_block_hw_ops  gfx_v9_4_2_ras_ops = {
		.ras_error_inject = &gfx_v9_4_2_ras_error_inject,
		.query_ras_error_count = &gfx_v9_4_2_query_ras_error_count,
		.reset_ras_error_count = &gfx_v9_4_2_reset_ras_error_count,
		.query_ras_error_status = &gfx_v9_4_2_query_ras_error_status,
		.reset_ras_error_status = &gfx_v9_4_2_reset_ras_error_status,
};

struct amdgpu_gfx_ras gfx_v9_4_2_ras = {
	.ras_block = {
		.hw_ops = &gfx_v9_4_2_ras_ops,
	},
	.enable_watchdog_timer = &gfx_v9_4_2_enable_watchdog_timer,
	.query_utcl2_poison_status = gfx_v9_4_2_query_uctl2_poison_status,
};
