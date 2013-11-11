/*
 * Copyright 2010 Red Hat Inc.
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
 * Authors: Ben Skeggs
 */

#ifndef __NOUVEAU_PM_H__
#define __NOUVEAU_PM_H__

#include <subdev/bios/pll.h>
#include <subdev/clock.h>

struct nouveau_pm_voltage_level {
	u32 voltage; /* microvolts */
	u8  vid;
};

struct nouveau_pm_voltage {
	bool supported;
	u8 version;
	u8 vid_mask;

	struct nouveau_pm_voltage_level *level;
	int nr_level;
};

/* Exclusive upper limits */
#define NV_MEM_CL_DDR2_MAX 8
#define NV_MEM_WR_DDR2_MAX 9
#define NV_MEM_CL_DDR3_MAX 17
#define NV_MEM_WR_DDR3_MAX 17
#define NV_MEM_CL_GDDR3_MAX 16
#define NV_MEM_WR_GDDR3_MAX 18
#define NV_MEM_CL_GDDR5_MAX 21
#define NV_MEM_WR_GDDR5_MAX 20

struct nouveau_pm_memtiming {
	int id;

	u32 reg[9];
	u32 mr[4];

	u8 tCWL;

	u8 odt;
	u8 drive_strength;
};

struct nouveau_pm_tbl_header {
	u8 version;
	u8 header_len;
	u8 entry_cnt;
	u8 entry_len;
};

struct nouveau_pm_tbl_entry {
	u8 tWR;
	u8 tWTR;
	u8 tCL;
	u8 tRC;
	u8 empty_4;
	u8 tRFC;	/* Byte 5 */
	u8 empty_6;
	u8 tRAS;	/* Byte 7 */
	u8 empty_8;
	u8 tRP;		/* Byte 9 */
	u8 tRCDRD;
	u8 tRCDWR;
	u8 tRRD;
	u8 tUNK_13;
	u8 RAM_FT1;		/* 14, a bitmask of random RAM features */
	u8 empty_15;
	u8 tUNK_16;
	u8 empty_17;
	u8 tUNK_18;
	u8 tCWL;
	u8 tUNK_20, tUNK_21;
};

struct nouveau_pm_profile;
struct nouveau_pm_profile_func {
	void (*destroy)(struct nouveau_pm_profile *);
	void (*init)(struct nouveau_pm_profile *);
	void (*fini)(struct nouveau_pm_profile *);
	struct nouveau_pm_level *(*select)(struct nouveau_pm_profile *);
};

struct nouveau_pm_profile {
	const struct nouveau_pm_profile_func *func;
	struct list_head head;
	char name[8];
};

#define NOUVEAU_PM_MAX_LEVEL 8
struct nouveau_pm_level {
	struct nouveau_pm_profile profile;
	struct device_attribute dev_attr;
	char name[32];
	int id;

	struct nouveau_pm_memtiming timing;
	u32 memory;
	u16 memscript;

	u32 core;
	u32 shader;
	u32 rop;
	u32 copy;
	u32 daemon;
	u32 vdec;
	u32 dom6;
	u32 unka0;	/* nva3:nvc0 */
	u32 hub01;	/* nvc0- */
	u32 hub06;	/* nvc0- */
	u32 hub07;	/* nvc0- */

	u32 volt_min; /* microvolts */
	u32 volt_max;
	u8  fanspeed;
};

struct nouveau_pm_temp_sensor_constants {
	u16 offset_constant;
	s16 offset_mult;
	s16 offset_div;
	s16 slope_mult;
	s16 slope_div;
};

struct nouveau_pm_threshold_temp {
	s16 critical;
	s16 down_clock;
};

struct nouveau_pm {
	struct drm_device *dev;

	struct nouveau_pm_voltage voltage;
	struct nouveau_pm_level perflvl[NOUVEAU_PM_MAX_LEVEL];
	int nr_perflvl;
	struct nouveau_pm_temp_sensor_constants sensor_constants;
	struct nouveau_pm_threshold_temp threshold_temp;

	struct nouveau_pm_profile *profile_ac;
	struct nouveau_pm_profile *profile_dc;
	struct nouveau_pm_profile *profile;
	struct list_head profiles;

	struct nouveau_pm_level boot;
	struct nouveau_pm_level *cur;

	struct device *hwmon;
	struct notifier_block acpi_nb;

	int  (*clocks_get)(struct drm_device *, struct nouveau_pm_level *);
	void *(*clocks_pre)(struct drm_device *, struct nouveau_pm_level *);
	int (*clocks_set)(struct drm_device *, void *);

	int (*voltage_get)(struct drm_device *);
	int (*voltage_set)(struct drm_device *, int voltage);
};

static inline struct nouveau_pm *
nouveau_pm(struct drm_device *dev)
{
	return nouveau_drm(dev)->pm;
}

struct nouveau_mem_exec_func {
	struct drm_device *dev;
	void (*precharge)(struct nouveau_mem_exec_func *);
	void (*refresh)(struct nouveau_mem_exec_func *);
	void (*refresh_auto)(struct nouveau_mem_exec_func *, bool);
	void (*refresh_self)(struct nouveau_mem_exec_func *, bool);
	void (*wait)(struct nouveau_mem_exec_func *, u32 nsec);
	u32  (*mrg)(struct nouveau_mem_exec_func *, int mr);
	void (*mrs)(struct nouveau_mem_exec_func *, int mr, u32 data);
	void (*clock_set)(struct nouveau_mem_exec_func *);
	void (*timing_set)(struct nouveau_mem_exec_func *);
	void *priv;
};

/* nouveau_mem.c */
int  nouveau_mem_exec(struct nouveau_mem_exec_func *,
		      struct nouveau_pm_level *);

/* nouveau_pm.c */
int  nouveau_pm_init(struct drm_device *dev);
void nouveau_pm_fini(struct drm_device *dev);
void nouveau_pm_resume(struct drm_device *dev);
extern const struct nouveau_pm_profile_func nouveau_pm_static_profile_func;
void nouveau_pm_trigger(struct drm_device *dev);

/* nouveau_volt.c */
void nouveau_volt_init(struct drm_device *);
void nouveau_volt_fini(struct drm_device *);
int  nouveau_volt_vid_lookup(struct drm_device *, int voltage);
int  nouveau_volt_lvl_lookup(struct drm_device *, int vid);
int  nouveau_voltage_gpio_get(struct drm_device *);
int  nouveau_voltage_gpio_set(struct drm_device *, int voltage);

/* nouveau_perf.c */
void nouveau_perf_init(struct drm_device *);
void nouveau_perf_fini(struct drm_device *);
u8 *nouveau_perf_rammap(struct drm_device *, u32 freq, u8 *ver,
			u8 *hdr, u8 *cnt, u8 *len);
u8 *nouveau_perf_ramcfg(struct drm_device *, u32 freq, u8 *ver, u8 *len);
u8 *nouveau_perf_timing(struct drm_device *, u32 freq, u8 *ver, u8 *len);

/* nouveau_mem.c */
void nouveau_mem_timing_init(struct drm_device *);
void nouveau_mem_timing_fini(struct drm_device *);

/* nv04_pm.c */
int nv04_pm_clocks_get(struct drm_device *, struct nouveau_pm_level *);
void *nv04_pm_clocks_pre(struct drm_device *, struct nouveau_pm_level *);
int nv04_pm_clocks_set(struct drm_device *, void *);

/* nv40_pm.c */
int nv40_pm_clocks_get(struct drm_device *, struct nouveau_pm_level *);
void *nv40_pm_clocks_pre(struct drm_device *, struct nouveau_pm_level *);
int nv40_pm_clocks_set(struct drm_device *, void *);
int nv40_pm_pwm_get(struct drm_device *, int, u32 *, u32 *);
int nv40_pm_pwm_set(struct drm_device *, int, u32, u32);

/* nv50_pm.c */
int nv50_pm_clocks_get(struct drm_device *, struct nouveau_pm_level *);
void *nv50_pm_clocks_pre(struct drm_device *, struct nouveau_pm_level *);
int nv50_pm_clocks_set(struct drm_device *, void *);
int nv50_pm_pwm_get(struct drm_device *, int, u32 *, u32 *);
int nv50_pm_pwm_set(struct drm_device *, int, u32, u32);

/* nva3_pm.c */
int nva3_pm_clocks_get(struct drm_device *, struct nouveau_pm_level *);
void *nva3_pm_clocks_pre(struct drm_device *, struct nouveau_pm_level *);
int nva3_pm_clocks_set(struct drm_device *, void *);

/* nvc0_pm.c */
int nvc0_pm_clocks_get(struct drm_device *, struct nouveau_pm_level *);
void *nvc0_pm_clocks_pre(struct drm_device *, struct nouveau_pm_level *);
int nvc0_pm_clocks_set(struct drm_device *, void *);

/* nouveau_mem.c */
int  nouveau_mem_timing_calc(struct drm_device *, u32 freq,
			     struct nouveau_pm_memtiming *);
void nouveau_mem_timing_read(struct drm_device *,
			     struct nouveau_pm_memtiming *);

static inline int
nva3_calc_pll(struct drm_device *dev, struct nvbios_pll *pll, u32 freq,
	      int *N, int *fN, int *M, int *P)
{
	struct nouveau_device *device = nouveau_dev(dev);
	struct nouveau_clock *clk = nouveau_clock(device);
	struct nouveau_pll_vals pv;
	int ret;

	ret = clk->pll_calc(clk, pll, freq, &pv);
	*N = pv.N1;
	*M = pv.M1;
	*P = pv.log2P;
	return ret;
}

#endif
