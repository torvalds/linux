// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) 2017-2018, The Linux Foundation. All rights reserved. */

#include <linux/err.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/pm_domain.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/platform_device.h>
#include <linux/pm_opp.h>
#include <linux/soc/qcom/smd-rpm.h>

#include <dt-bindings/power/qcom-rpmpd.h>

#define domain_to_rpmpd(domain) container_of(domain, struct rpmpd, pd)

/* Resource types:
 * RPMPD_X is X encoded as a little-endian, lower-case, ASCII string */
#define RPMPD_SMPA 0x61706d73
#define RPMPD_LDOA 0x616f646c
#define RPMPD_SMPB 0x62706d73
#define RPMPD_LDOB 0x626f646c
#define RPMPD_RWCX 0x78637772
#define RPMPD_RWMX 0x786d7772
#define RPMPD_RWLC 0x636c7772
#define RPMPD_RWLM 0x6d6c7772
#define RPMPD_RWSC 0x63737772
#define RPMPD_RWSM 0x6d737772
#define RPMPD_RWGX 0x78677772

/* Operation Keys */
#define KEY_CORNER		0x6e726f63 /* corn */
#define KEY_ENABLE		0x6e657773 /* swen */
#define KEY_FLOOR_CORNER	0x636676   /* vfc */
#define KEY_FLOOR_LEVEL		0x6c6676   /* vfl */
#define KEY_LEVEL		0x6c766c76 /* vlvl */

#define MAX_CORNER_RPMPD_STATE	6

struct rpmpd_req {
	__le32 key;
	__le32 nbytes;
	__le32 value;
};

struct rpmpd {
	struct generic_pm_domain pd;
	struct rpmpd *peer;
	const bool active_only;
	unsigned int corner;
	bool enabled;
	const int res_type;
	const int res_id;
	struct qcom_smd_rpm *rpm;
	unsigned int max_state;
	__le32 key;
};

struct rpmpd_desc {
	struct rpmpd **rpmpds;
	size_t num_pds;
	unsigned int max_state;
};

static DEFINE_MUTEX(rpmpd_lock);

/* CX */
static struct rpmpd RWCX0_LEVEL_cx_ao;
static struct rpmpd RWCX0_LEVEL_cx = {
	.pd = { .name = "cx", },
	.peer = &RWCX0_LEVEL_cx_ao,
	.res_type = RPMPD_RWCX,
	.res_id = 0,
	.key = KEY_LEVEL,
};

static struct rpmpd RWCX0_LEVEL_cx_ao = {
	.pd = { .name = "cx_ao", },
	.peer = &RWCX0_LEVEL_cx,
	.active_only = true,
	.res_type = RPMPD_RWCX,
	.res_id = 0,
	.key = KEY_LEVEL,
};

static struct rpmpd SMPA1_CORNER_cx_ao;
static struct rpmpd SMPA1_CORNER_cx = {
	.pd = { .name = "cx", },
	.peer = &SMPA1_CORNER_cx_ao,
	.res_type = RPMPD_SMPA,
	.res_id = 1,
	.key = KEY_CORNER,
};

static struct rpmpd SMPA1_CORNER_cx_ao = {
	.pd = { .name = "cx_ao", },
	.peer = &SMPA1_CORNER_cx,
	.active_only = true,
	.res_type = RPMPD_SMPA,
	.res_id = 1,
	.key = KEY_CORNER,
};

static struct rpmpd SMPA2_CORNER_cx_ao;
static struct rpmpd SMPA2_CORNER_cx = {
	.pd = { .name = "cx", },
	.peer = &SMPA2_CORNER_cx_ao,
	.res_type = RPMPD_SMPA,
	.res_id = 2,
	.key = KEY_CORNER,
};

static struct rpmpd SMPA2_CORNER_cx_ao = {
	.pd = { .name = "cx_ao", },
	.peer = &SMPA2_CORNER_cx,
	.active_only = true,
	.res_type = RPMPD_SMPA,
	.res_id = 2,
	.key = KEY_CORNER,
};

static struct rpmpd SMPA2_LEVEL_cx_ao;
static struct rpmpd SMPA2_LEVEL_cx = {
	.pd = { .name = "cx", },
	.peer = &SMPA2_LEVEL_cx_ao,
	.res_type = RPMPD_SMPA,
	.res_id = 2,
	.key = KEY_LEVEL,
};

static struct rpmpd SMPA2_LEVEL_cx_ao = {
	.pd = { .name = "cx_ao", },
	.peer = &SMPA2_LEVEL_cx,
	.active_only = true,
	.res_type = RPMPD_SMPA,
	.res_id = 2,
	.key = KEY_LEVEL,
};

static struct rpmpd SMPA3_LEVEL_cx_ao;
static struct rpmpd SMPA3_LEVEL_cx = {
	.pd = { .name = "cx", },
	.peer = &SMPA3_LEVEL_cx_ao,
	.res_type = RPMPD_SMPA,
	.res_id = 3,
	.key = KEY_LEVEL,
};

static struct rpmpd SMPA3_LEVEL_cx_ao = {
	.pd = { .name = "cx_ao", },
	.peer = &SMPA3_LEVEL_cx,
	.active_only = true,
	.res_type = RPMPD_SMPA,
	.res_id = 3,
	.key = KEY_LEVEL,
};

static struct rpmpd RWCX0_cx_vfl = {
	.pd = { .name = "cx_vfl", },
	.res_type = RPMPD_RWCX,
	.res_id = 0,
	.key = KEY_FLOOR_LEVEL,
};

static struct rpmpd RWSC2_cx_vfl = {
	.pd = { .name = "cx_vfl", },
	.res_type = RPMPD_RWSC,
	.res_id = 2,
	.key = KEY_FLOOR_LEVEL,
};

static struct rpmpd SMPA1_cx_vfc = {
	.pd = { .name = "cx_vfc", },
	.res_type = RPMPD_SMPA,
	.res_id = 1,
	.key = KEY_FLOOR_CORNER,
};

static struct rpmpd SMPA2_cx_vfc = {
	.pd = { .name = "cx_vfc", },
	.res_type = RPMPD_SMPA,
	.res_id = 2,
	.key = KEY_FLOOR_CORNER,
};

static struct rpmpd SMPA2_cx_vfl = {
	.pd = { .name = "cx_vfl", },
	.res_type = RPMPD_SMPA,
	.res_id = 2,
	.key = KEY_FLOOR_LEVEL,
};

static struct rpmpd SMPA3_cx_vfl = {
	.pd = { .name = "cx_vfl", },
	.res_type = RPMPD_SMPA,
	.res_id = 3,
	.key = KEY_FLOOR_LEVEL,
};

/* G(F)X */
static struct rpmpd SMPB2_gfx_corner = {
	.pd = { .name = "gfx", },
	.res_type = RPMPD_SMPB,
	.res_id = 2,
	.key = KEY_CORNER,
};

static struct rpmpd SMPB2_gfx_vfc = {
	.pd = { .name = "gfx_vfc", },
	.res_type = RPMPD_SMPB,
	.res_id = 2,
	.key = KEY_FLOOR_CORNER,
};

static struct rpmpd RWGX0_LEVEL_gx_ao;
static struct rpmpd RWGX0_LEVEL_gx = {
	.pd = { .name = "gx", },
	.peer = &RWGX0_LEVEL_gx_ao,
	.res_type = RPMPD_RWGX,
	.res_id = 0,
	.key = KEY_LEVEL,
};

static struct rpmpd RWGX0_LEVEL_gx_ao = {
	.pd = { .name = "gx_ao", },
	.peer = &RWGX0_LEVEL_gx,
	.active_only = true,
	.res_type = RPMPD_RWGX,
	.res_id = 0,
	.key = KEY_LEVEL,
};

/* MX */
static struct rpmpd LDOA3_CORNER_mx_ao;
static struct rpmpd LDOA3_CORNER_mx = {
	.pd = { .name = "mx", },
	.peer = &LDOA3_CORNER_mx_ao,
	.res_type = RPMPD_LDOA,
	.res_id = 3,
	.key = KEY_CORNER,
};

static struct rpmpd LDOA3_CORNER_mx_ao = {
	.pd = { .name = "mx_ao", },
	.peer = &LDOA3_CORNER_mx,
	.active_only = true,
	.res_type = RPMPD_LDOA,
	.res_id = 3,
	.key = KEY_CORNER,
};

static struct rpmpd LDOA12_LEVEL_mx_ao;
static struct rpmpd LDOA12_LEVEL_mx = {
	.pd = { .name = "mx", },
	.peer = &LDOA12_LEVEL_mx_ao,
	.res_type = RPMPD_LDOA,
	.res_id = 12,
	.key = KEY_LEVEL,
};

static struct rpmpd LDOA12_LEVEL_mx_ao = {
	.pd = { .name = "mx_ao", },
	.peer = &LDOA12_LEVEL_mx,
	.active_only = true,
	.res_type = RPMPD_LDOA,
	.res_id = 12,
	.key = KEY_LEVEL,
};

static struct rpmpd SMPA2_CORNER_mx_ao;
static struct rpmpd SMPA2_CORNER_mx = {
	.pd = { .name = "mx", },
	.peer = &SMPA2_CORNER_mx_ao,
	.res_type = RPMPD_SMPA,
	.res_id = 2,
	.key = KEY_CORNER,
};

static struct rpmpd SMPA2_CORNER_mx_ao = {
	.pd = { .name = "mx_ao", },
	.peer = &SMPA2_CORNER_mx,
	.active_only = true,
	.res_type = RPMPD_SMPA,
	.res_id = 2,
	.key = KEY_CORNER,
};

static struct rpmpd RWMX0_LEVEL_mx_ao;
static struct rpmpd RWMX0_LEVEL_mx = {
	.pd = { .name = "mx", },
	.peer = &RWMX0_LEVEL_mx_ao,
	.res_type = RPMPD_RWMX,
	.res_id = 0,
	.key = KEY_LEVEL,
};

static struct rpmpd RWMX0_LEVEL_mx_ao = {
	.pd = { .name = "mx_ao", },
	.peer = &RWMX0_LEVEL_mx,
	.active_only = true,
	.res_type = RPMPD_RWMX,
	.res_id = 0,
	.key = KEY_LEVEL,
};

static struct rpmpd SMPA6_LEVEL_mx_ao;
static struct rpmpd SMPA6_LEVEL_mx = {
	.pd = { .name = "mx", },
	.peer = &SMPA6_LEVEL_mx_ao,
	.res_type = RPMPD_SMPA,
	.res_id = 6,
	.key = KEY_LEVEL,
};

static struct rpmpd SMPA6_LEVEL_mx_ao = {
	.pd = { .name = "mx_ao", },
	.peer = &SMPA6_LEVEL_mx,
	.active_only = true,
	.res_type = RPMPD_SMPA,
	.res_id = 6,
	.key = KEY_LEVEL,
};

static struct rpmpd SMPA7_LEVEL_mx_ao;
static struct rpmpd SMPA7_LEVEL_mx = {
	.pd = { .name = "mx", },
	.peer = &SMPA7_LEVEL_mx_ao,
	.res_type = RPMPD_SMPA,
	.res_id = 7,
	.key = KEY_LEVEL,
};

static struct rpmpd SMPA7_LEVEL_mx_ao = {
	.pd = { .name = "mx_ao", },
	.peer = &SMPA7_LEVEL_mx,
	.active_only = true,
	.res_type = RPMPD_SMPA,
	.res_id = 7,
	.key = KEY_LEVEL,
};

static struct rpmpd LDOA12_mx_vfl = {
	.pd = { .name = "mx_vfl", },
	.res_type = RPMPD_LDOA,
	.res_id = 12,
	.key = KEY_FLOOR_LEVEL,
};

static struct rpmpd RWMX0_mx_vfl = {
	.pd = { .name = "mx_vfl", },
	.res_type = RPMPD_RWMX,
	.res_id = 0,
	.key = KEY_FLOOR_LEVEL,
};

static struct rpmpd RWSM6_mx_vfl = {
	.pd = { .name = "mx_vfl", },
	.res_type = RPMPD_RWSM,
	.res_id = 6,
	.key = KEY_FLOOR_LEVEL,
};

/* MD */
static struct rpmpd SMPA1_CORNER_md_ao;
static struct rpmpd SMPA1_CORNER_md = {
	.pd = { .name = "md", },
	.peer = &SMPA1_CORNER_md_ao,
	.res_type = RPMPD_SMPA,
	.res_id = 1,
	.key = KEY_CORNER,
};

static struct rpmpd SMPA1_CORNER_md_ao = {
	.pd = { .name = "md_ao", },
	.peer = &SMPA1_CORNER_md,
	.active_only = true,
	.res_type = RPMPD_SMPA,
	.res_id = 1,
	.key = KEY_CORNER,
};

static struct rpmpd SMPA1_LEVEL_md_ao;
static struct rpmpd SMPA1_LEVEL_md = {
	.pd = { .name = "md", },
	.peer = &SMPA1_LEVEL_md_ao,
	.res_type = RPMPD_SMPA,
	.res_id = 1,
	.key = KEY_LEVEL,
};

static struct rpmpd SMPA1_LEVEL_md_ao = {
	.pd = { .name = "md_ao", },
	.peer = &SMPA1_LEVEL_md,
	.active_only = true,
	.res_type = RPMPD_SMPA,
	.res_id = 1,
	.key = KEY_LEVEL,
};

static struct rpmpd SMPA1_md_vfc = {
	.pd = { .name = "md_vfc", },
	.res_type = RPMPD_SMPA,
	.res_id = 1,
	.key = KEY_FLOOR_CORNER,
};

/* LPI_CX */
static struct rpmpd RWLC0_lpi_cx_lvl = {
	.pd = { .name = "lpi_cx", },
	.res_type = RPMPD_RWLC,
	.res_id = 0,
	.key = KEY_LEVEL,
};

static struct rpmpd RWLC0_lpicx_vfl = {
	.pd = { .name = "lpicx_vfl", },
	.res_type = RPMPD_RWLC,
	.res_id = 0,
	.key = KEY_FLOOR_LEVEL,
};

/* LPI_MX */
static struct rpmpd RWLM0_lpi_mx_lvl = {
	.pd = { .name = "lpi_mx", },
	.res_type = RPMPD_RWLM,
	.res_id = 0,
	.key = KEY_LEVEL,
};

static struct rpmpd RWLM0_lpimx_vfl = {
	.pd = { .name = "lpimx_vfl", },
	.res_type = RPMPD_RWLM,
	.res_id = 0,
	.key = KEY_FLOOR_LEVEL,
};

/* SSC_CX */
static struct rpmpd LDOA26_sscx_corner = {
	.pd = { .name = "sscx", },
	.res_type = RPMPD_LDOA,
	.res_id = 26,
	.key = KEY_CORNER,
};

static struct rpmpd RWLC0_ssccx_lvl = {
	.pd = { .name = "ssccx", },
	.res_type = RPMPD_RWLC,
	.res_id = 0,
	.key = KEY_LEVEL,
};

static struct rpmpd RWSC0_ssccx_lvl = {
	.pd = { .name = "ssccx", },
	.res_type = RPMPD_RWSC,
	.res_id = 0,
	.key = KEY_LEVEL,
};

static struct rpmpd LDOA26_sscx_vfc = {
	.pd = { .name = "sscx_vfc", },
	.res_type = RPMPD_LDOA,
	.res_id = 26,
	.key = KEY_FLOOR_CORNER,
};

static struct rpmpd RWLC0_ssccx_vfl = {
	.pd = { .name = "ssccx_vfl", },
	.res_type = RPMPD_RWLC,
	.res_id = 0,
	.key = KEY_FLOOR_LEVEL,
};

static struct rpmpd RWSC0_ssccx_vfl = {
	.pd = { .name = "ssccx_vfl", },
	.res_type = RPMPD_RWSC,
	.res_id = 0,
	.key = KEY_FLOOR_LEVEL,
};

/* SSC_MX */
static struct rpmpd RWLM0_sscmx_lvl = {
	.pd = { .name = "sscmx", },
	.res_type = RPMPD_RWLM,
	.res_id = 0,
	.key = KEY_LEVEL,
};

static struct rpmpd RWSM0_sscmx_lvl = {
	.pd = { .name = "sscmx", },
	.res_type = RPMPD_RWSM,
	.res_id = 0,
	.key = KEY_LEVEL,
};

static struct rpmpd RWLM0_sscmx_vfl = {
	.pd = { .name = "sscmx_vfl", },
	.res_type = RPMPD_RWLM,
	.res_id = 0,
	.key = KEY_FLOOR_LEVEL,
};

static struct rpmpd RWSM0_sscmx_vfl = {
	.pd = { .name = "sscmx_vfl", },
	.res_type = RPMPD_RWSM,
	.res_id = 0,
	.key = KEY_FLOOR_LEVEL,
};

/* mdm9607 RPM Power Domains */
static struct rpmpd *mdm9607_rpmpds[] = {
	[MDM9607_VDDCX] =	&SMPA3_LEVEL_cx,
	[MDM9607_VDDCX_AO] =	&SMPA3_LEVEL_cx_ao,
	[MDM9607_VDDCX_VFL] =	&SMPA3_cx_vfl,
	[MDM9607_VDDMX] =	&LDOA12_LEVEL_mx,
	[MDM9607_VDDMX_AO] =	&LDOA12_LEVEL_mx_ao,
	[MDM9607_VDDMX_VFL] =	&LDOA12_mx_vfl,
};

static const struct rpmpd_desc mdm9607_desc = {
	.rpmpds = mdm9607_rpmpds,
	.num_pds = ARRAY_SIZE(mdm9607_rpmpds),
	.max_state = RPM_SMD_LEVEL_TURBO,
};

/* msm8226 RPM Power Domains */
static struct rpmpd *msm8226_rpmpds[] = {
	[MSM8226_VDDCX] =	&SMPA1_CORNER_cx,
	[MSM8226_VDDCX_AO] =	&SMPA1_CORNER_cx_ao,
	[MSM8226_VDDCX_VFC] =	&SMPA1_cx_vfc,
};

static const struct rpmpd_desc msm8226_desc = {
	.rpmpds = msm8226_rpmpds,
	.num_pds = ARRAY_SIZE(msm8226_rpmpds),
	.max_state = MAX_CORNER_RPMPD_STATE,
};

/* msm8939 RPM Power Domains */
static struct rpmpd *msm8939_rpmpds[] = {
	[MSM8939_VDDMDCX] =	&SMPA1_CORNER_md,
	[MSM8939_VDDMDCX_AO] =	&SMPA1_CORNER_md_ao,
	[MSM8939_VDDMDCX_VFC] =	&SMPA1_md_vfc,
	[MSM8939_VDDCX] =	&SMPA2_CORNER_cx,
	[MSM8939_VDDCX_AO] =	&SMPA2_CORNER_cx_ao,
	[MSM8939_VDDCX_VFC] =	&SMPA2_cx_vfc,
	[MSM8939_VDDMX] =	&LDOA3_CORNER_mx,
	[MSM8939_VDDMX_AO] =	&LDOA3_CORNER_mx_ao,
};

static const struct rpmpd_desc msm8939_desc = {
	.rpmpds = msm8939_rpmpds,
	.num_pds = ARRAY_SIZE(msm8939_rpmpds),
	.max_state = MAX_CORNER_RPMPD_STATE,
};

/* msm8916 RPM Power Domains */
static struct rpmpd *msm8916_rpmpds[] = {
	[MSM8916_VDDCX] =	&SMPA1_CORNER_cx,
	[MSM8916_VDDCX_AO] =	&SMPA1_CORNER_cx_ao,
	[MSM8916_VDDCX_VFC] =	&SMPA1_cx_vfc,
	[MSM8916_VDDMX] =	&LDOA3_CORNER_mx,
	[MSM8916_VDDMX_AO] =	&LDOA3_CORNER_mx_ao,
};

static const struct rpmpd_desc msm8916_desc = {
	.rpmpds = msm8916_rpmpds,
	.num_pds = ARRAY_SIZE(msm8916_rpmpds),
	.max_state = MAX_CORNER_RPMPD_STATE,
};

/* msm8953 RPM Power Domains */
static struct rpmpd *msm8953_rpmpds[] = {
	[MSM8953_VDDMD] =	&SMPA1_LEVEL_md,
	[MSM8953_VDDMD_AO] =	&SMPA1_LEVEL_md_ao,
	[MSM8953_VDDCX] =	&SMPA2_LEVEL_cx,
	[MSM8953_VDDCX_AO] =	&SMPA2_LEVEL_cx_ao,
	[MSM8953_VDDCX_VFL] =	&SMPA2_cx_vfl,
	[MSM8953_VDDMX] =	&SMPA7_LEVEL_mx,
	[MSM8953_VDDMX_AO] =	&SMPA7_LEVEL_mx_ao,
};

static const struct rpmpd_desc msm8953_desc = {
	.rpmpds = msm8953_rpmpds,
	.num_pds = ARRAY_SIZE(msm8953_rpmpds),
	.max_state = RPM_SMD_LEVEL_TURBO,
};

/* msm8976 RPM Power Domains */
static struct rpmpd *msm8976_rpmpds[] = {
	[MSM8976_VDDCX] =	&SMPA2_LEVEL_cx,
	[MSM8976_VDDCX_AO] =	&SMPA2_LEVEL_cx_ao,
	[MSM8976_VDDCX_VFL] =	&RWSC2_cx_vfl,
	[MSM8976_VDDMX] =	&SMPA6_LEVEL_mx,
	[MSM8976_VDDMX_AO] =	&SMPA6_LEVEL_mx_ao,
	[MSM8976_VDDMX_VFL] =	&RWSM6_mx_vfl,
};

static const struct rpmpd_desc msm8976_desc = {
	.rpmpds = msm8976_rpmpds,
	.num_pds = ARRAY_SIZE(msm8976_rpmpds),
	.max_state = RPM_SMD_LEVEL_TURBO_HIGH,
};

/* msm8994 RPM Power domains */
static struct rpmpd *msm8994_rpmpds[] = {
	[MSM8994_VDDCX] =	&SMPA1_CORNER_cx,
	[MSM8994_VDDCX_AO] =	&SMPA1_CORNER_cx_ao,
	[MSM8994_VDDCX_VFC] =	&SMPA1_cx_vfc,
	[MSM8994_VDDMX] =	&SMPA2_CORNER_mx,
	[MSM8994_VDDMX_AO] =	&SMPA2_CORNER_mx_ao,

	/* Attention! *Some* 8994 boards with pm8004 may use SMPC here! */
	[MSM8994_VDDGFX] =	&SMPB2_gfx_corner,
	[MSM8994_VDDGFX_VFC] =	&SMPB2_gfx_vfc,
};

static const struct rpmpd_desc msm8994_desc = {
	.rpmpds = msm8994_rpmpds,
	.num_pds = ARRAY_SIZE(msm8994_rpmpds),
	.max_state = MAX_CORNER_RPMPD_STATE,
};

/* msm8996 RPM Power domains */
static struct rpmpd *msm8996_rpmpds[] = {
	[MSM8996_VDDCX] =	&SMPA1_CORNER_cx,
	[MSM8996_VDDCX_AO] =	&SMPA1_CORNER_cx_ao,
	[MSM8996_VDDCX_VFC] =	&SMPA1_cx_vfc,
	[MSM8996_VDDMX] =	&SMPA2_CORNER_mx,
	[MSM8996_VDDMX_AO] =	&SMPA2_CORNER_mx_ao,
	[MSM8996_VDDSSCX] =	&LDOA26_sscx_corner,
	[MSM8996_VDDSSCX_VFC] =	&LDOA26_sscx_vfc,
};

static const struct rpmpd_desc msm8996_desc = {
	.rpmpds = msm8996_rpmpds,
	.num_pds = ARRAY_SIZE(msm8996_rpmpds),
	.max_state = MAX_CORNER_RPMPD_STATE,
};

/* msm8998 RPM Power domains */
static struct rpmpd *msm8998_rpmpds[] = {
	[MSM8998_VDDCX] =		&RWCX0_LEVEL_cx,
	[MSM8998_VDDCX_AO] =		&RWCX0_LEVEL_cx_ao,
	[MSM8998_VDDCX_VFL] =		&RWCX0_cx_vfl,
	[MSM8998_VDDMX] =		&RWMX0_LEVEL_mx,
	[MSM8998_VDDMX_AO] =		&RWMX0_LEVEL_mx_ao,
	[MSM8998_VDDMX_VFL] =		&RWMX0_mx_vfl,
	[MSM8998_SSCCX] =		&RWSC0_ssccx_lvl,
	[MSM8998_SSCCX_VFL] =		&RWSC0_ssccx_vfl,
	[MSM8998_SSCMX] =		&RWSM0_sscmx_lvl,
	[MSM8998_SSCMX_VFL] =		&RWSM0_sscmx_vfl,
};

static const struct rpmpd_desc msm8998_desc = {
	.rpmpds = msm8998_rpmpds,
	.num_pds = ARRAY_SIZE(msm8998_rpmpds),
	.max_state = RPM_SMD_LEVEL_BINNING,
};

/* qcs404 RPM Power domains */
static struct rpmpd *qcs404_rpmpds[] = {
	[QCS404_VDDMX] = &RWMX0_LEVEL_mx,
	[QCS404_VDDMX_AO] = &RWMX0_LEVEL_mx_ao,
	[QCS404_VDDMX_VFL] = &RWMX0_mx_vfl,
	[QCS404_LPICX] = &RWLC0_lpi_cx_lvl,
	[QCS404_LPICX_VFL] = &RWLC0_lpicx_vfl,
	[QCS404_LPIMX] = &RWLM0_lpi_mx_lvl,
	[QCS404_LPIMX_VFL] = &RWLM0_lpimx_vfl,
};

static const struct rpmpd_desc qcs404_desc = {
	.rpmpds = qcs404_rpmpds,
	.num_pds = ARRAY_SIZE(qcs404_rpmpds),
	.max_state = RPM_SMD_LEVEL_BINNING,
};

/* sdm660 RPM Power domains */
static struct rpmpd *sdm660_rpmpds[] = {
	[SDM660_VDDCX] =		&RWCX0_LEVEL_cx,
	[SDM660_VDDCX_AO] =		&RWCX0_LEVEL_cx_ao,
	[SDM660_VDDCX_VFL] =		&RWCX0_cx_vfl,
	[SDM660_VDDMX] =		&RWMX0_LEVEL_mx,
	[SDM660_VDDMX_AO] =		&RWMX0_LEVEL_mx_ao,
	[SDM660_VDDMX_VFL] =		&RWMX0_mx_vfl,
	[SDM660_SSCCX] =		&RWLC0_ssccx_lvl,
	[SDM660_SSCCX_VFL] =		&RWLC0_ssccx_vfl,
	[SDM660_SSCMX] =		&RWLM0_sscmx_lvl,
	[SDM660_SSCMX_VFL] =		&RWLM0_sscmx_vfl,
};

static const struct rpmpd_desc sdm660_desc = {
	.rpmpds = sdm660_rpmpds,
	.num_pds = ARRAY_SIZE(sdm660_rpmpds),
	.max_state = RPM_SMD_LEVEL_TURBO,
};

/* sm4250/6115 RPM Power domains */
static struct rpmpd *sm6115_rpmpds[] = {
	[SM6115_VDDCX] =		&RWCX0_LEVEL_cx,
	[SM6115_VDDCX_AO] =		&RWCX0_LEVEL_cx_ao,
	[SM6115_VDDCX_VFL] =		&RWCX0_cx_vfl,
	[SM6115_VDDMX] =		&RWMX0_LEVEL_mx,
	[SM6115_VDDMX_AO] =		&RWMX0_LEVEL_mx_ao,
	[SM6115_VDDMX_VFL] =		&RWMX0_mx_vfl,
	[SM6115_VDD_LPI_CX] =		&RWLC0_lpi_cx_lvl,
	[SM6115_VDD_LPI_MX] =		&RWLM0_lpi_mx_lvl,
};

static const struct rpmpd_desc sm6115_desc = {
	.rpmpds = sm6115_rpmpds,
	.num_pds = ARRAY_SIZE(sm6115_rpmpds),
	.max_state = RPM_SMD_LEVEL_TURBO_NO_CPR,
};

/* sm6125 RPM Power domains */
static struct rpmpd *sm6125_rpmpds[] = {
	[SM6125_VDDCX] =		&RWCX0_LEVEL_cx,
	[SM6125_VDDCX_AO] =		&RWCX0_LEVEL_cx_ao,
	[SM6125_VDDCX_VFL] =		&RWCX0_cx_vfl,
	[SM6125_VDDMX] =		&RWMX0_LEVEL_mx,
	[SM6125_VDDMX_AO] =		&RWMX0_LEVEL_mx_ao,
	[SM6125_VDDMX_VFL] =		&RWMX0_mx_vfl,
};

static const struct rpmpd_desc sm6125_desc = {
	.rpmpds = sm6125_rpmpds,
	.num_pds = ARRAY_SIZE(sm6125_rpmpds),
	.max_state = RPM_SMD_LEVEL_BINNING,
};

static struct rpmpd *sm6375_rpmpds[] = {
	[SM6375_VDDCX] = &RWCX0_LEVEL_cx,
	[SM6375_VDDCX_AO] = &RWCX0_LEVEL_cx_ao,
	[SM6375_VDDCX_VFL] = &RWCX0_cx_vfl,
	[SM6375_VDDMX] = &RWMX0_LEVEL_mx,
	[SM6375_VDDMX_AO] = &RWMX0_LEVEL_mx_ao,
	[SM6375_VDDMX_VFL] = &RWMX0_mx_vfl,
	[SM6375_VDDGX] = &RWGX0_LEVEL_gx,
	[SM6375_VDDGX_AO] = &RWGX0_LEVEL_gx_ao,
	[SM6375_VDD_LPI_CX] = &RWLC0_lpi_cx_lvl,
	[SM6375_VDD_LPI_MX] = &RWLM0_lpi_mx_lvl,
};

static const struct rpmpd_desc sm6375_desc = {
	.rpmpds = sm6375_rpmpds,
	.num_pds = ARRAY_SIZE(sm6375_rpmpds),
	.max_state = RPM_SMD_LEVEL_TURBO_NO_CPR,
};

static struct rpmpd *qcm2290_rpmpds[] = {
	[QCM2290_VDDCX] = &RWCX0_LEVEL_cx,
	[QCM2290_VDDCX_AO] = &RWCX0_LEVEL_cx_ao,
	[QCM2290_VDDCX_VFL] = &RWCX0_cx_vfl,
	[QCM2290_VDDMX] = &RWMX0_LEVEL_mx,
	[QCM2290_VDDMX_AO] = &RWMX0_LEVEL_mx_ao,
	[QCM2290_VDDMX_VFL] = &RWMX0_mx_vfl,
	[QCM2290_VDD_LPI_CX] = &RWLC0_lpi_cx_lvl,
	[QCM2290_VDD_LPI_MX] = &RWLM0_lpi_mx_lvl,
};

static const struct rpmpd_desc qcm2290_desc = {
	.rpmpds = qcm2290_rpmpds,
	.num_pds = ARRAY_SIZE(qcm2290_rpmpds),
	.max_state = RPM_SMD_LEVEL_TURBO_NO_CPR,
};

static const struct of_device_id rpmpd_match_table[] = {
	{ .compatible = "qcom,mdm9607-rpmpd", .data = &mdm9607_desc },
	{ .compatible = "qcom,msm8226-rpmpd", .data = &msm8226_desc },
	{ .compatible = "qcom,msm8909-rpmpd", .data = &msm8916_desc },
	{ .compatible = "qcom,msm8916-rpmpd", .data = &msm8916_desc },
	{ .compatible = "qcom,msm8939-rpmpd", .data = &msm8939_desc },
	{ .compatible = "qcom,msm8953-rpmpd", .data = &msm8953_desc },
	{ .compatible = "qcom,msm8976-rpmpd", .data = &msm8976_desc },
	{ .compatible = "qcom,msm8994-rpmpd", .data = &msm8994_desc },
	{ .compatible = "qcom,msm8996-rpmpd", .data = &msm8996_desc },
	{ .compatible = "qcom,msm8998-rpmpd", .data = &msm8998_desc },
	{ .compatible = "qcom,qcm2290-rpmpd", .data = &qcm2290_desc },
	{ .compatible = "qcom,qcs404-rpmpd", .data = &qcs404_desc },
	{ .compatible = "qcom,sdm660-rpmpd", .data = &sdm660_desc },
	{ .compatible = "qcom,sm6115-rpmpd", .data = &sm6115_desc },
	{ .compatible = "qcom,sm6125-rpmpd", .data = &sm6125_desc },
	{ .compatible = "qcom,sm6375-rpmpd", .data = &sm6375_desc },
	{ }
};
MODULE_DEVICE_TABLE(of, rpmpd_match_table);

static int rpmpd_send_enable(struct rpmpd *pd, bool enable)
{
	struct rpmpd_req req = {
		.key = KEY_ENABLE,
		.nbytes = cpu_to_le32(sizeof(u32)),
		.value = cpu_to_le32(enable),
	};

	return qcom_rpm_smd_write(pd->rpm, QCOM_SMD_RPM_ACTIVE_STATE,
				  pd->res_type, pd->res_id, &req, sizeof(req));
}

static int rpmpd_send_corner(struct rpmpd *pd, int state, unsigned int corner)
{
	struct rpmpd_req req = {
		.key = pd->key,
		.nbytes = cpu_to_le32(sizeof(u32)),
		.value = cpu_to_le32(corner),
	};

	return qcom_rpm_smd_write(pd->rpm, state, pd->res_type, pd->res_id,
				  &req, sizeof(req));
};

static void to_active_sleep(struct rpmpd *pd, unsigned int corner,
			    unsigned int *active, unsigned int *sleep)
{
	*active = corner;

	if (pd->active_only)
		*sleep = 0;
	else
		*sleep = *active;
}

static int rpmpd_aggregate_corner(struct rpmpd *pd)
{
	int ret;
	struct rpmpd *peer = pd->peer;
	unsigned int active_corner, sleep_corner;
	unsigned int this_active_corner = 0, this_sleep_corner = 0;
	unsigned int peer_active_corner = 0, peer_sleep_corner = 0;

	to_active_sleep(pd, pd->corner, &this_active_corner, &this_sleep_corner);

	if (peer && peer->enabled)
		to_active_sleep(peer, peer->corner, &peer_active_corner,
				&peer_sleep_corner);

	active_corner = max(this_active_corner, peer_active_corner);

	ret = rpmpd_send_corner(pd, QCOM_SMD_RPM_ACTIVE_STATE, active_corner);
	if (ret)
		return ret;

	sleep_corner = max(this_sleep_corner, peer_sleep_corner);

	return rpmpd_send_corner(pd, QCOM_SMD_RPM_SLEEP_STATE, sleep_corner);
}

static int rpmpd_power_on(struct generic_pm_domain *domain)
{
	int ret;
	struct rpmpd *pd = domain_to_rpmpd(domain);

	mutex_lock(&rpmpd_lock);

	ret = rpmpd_send_enable(pd, true);
	if (ret)
		goto out;

	pd->enabled = true;

	if (pd->corner)
		ret = rpmpd_aggregate_corner(pd);

out:
	mutex_unlock(&rpmpd_lock);

	return ret;
}

static int rpmpd_power_off(struct generic_pm_domain *domain)
{
	int ret;
	struct rpmpd *pd = domain_to_rpmpd(domain);

	mutex_lock(&rpmpd_lock);

	ret = rpmpd_send_enable(pd, false);
	if (!ret)
		pd->enabled = false;

	mutex_unlock(&rpmpd_lock);

	return ret;
}

static int rpmpd_set_performance(struct generic_pm_domain *domain,
				 unsigned int state)
{
	int ret = 0;
	struct rpmpd *pd = domain_to_rpmpd(domain);

	if (state > pd->max_state)
		state = pd->max_state;

	mutex_lock(&rpmpd_lock);

	pd->corner = state;

	/* Always send updates for vfc and vfl */
	if (!pd->enabled && pd->key != KEY_FLOOR_CORNER &&
	    pd->key != KEY_FLOOR_LEVEL)
		goto out;

	ret = rpmpd_aggregate_corner(pd);

out:
	mutex_unlock(&rpmpd_lock);

	return ret;
}

static unsigned int rpmpd_get_performance(struct generic_pm_domain *genpd,
					  struct dev_pm_opp *opp)
{
	return dev_pm_opp_get_level(opp);
}

static int rpmpd_probe(struct platform_device *pdev)
{
	int i;
	size_t num;
	struct genpd_onecell_data *data;
	struct qcom_smd_rpm *rpm;
	struct rpmpd **rpmpds;
	const struct rpmpd_desc *desc;

	rpm = dev_get_drvdata(pdev->dev.parent);
	if (!rpm) {
		dev_err(&pdev->dev, "Unable to retrieve handle to RPM\n");
		return -ENODEV;
	}

	desc = of_device_get_match_data(&pdev->dev);
	if (!desc)
		return -EINVAL;

	rpmpds = desc->rpmpds;
	num = desc->num_pds;

	data = devm_kzalloc(&pdev->dev, sizeof(*data), GFP_KERNEL);
	if (!data)
		return -ENOMEM;

	data->domains = devm_kcalloc(&pdev->dev, num, sizeof(*data->domains),
				     GFP_KERNEL);
	if (!data->domains)
		return -ENOMEM;

	data->num_domains = num;

	for (i = 0; i < num; i++) {
		if (!rpmpds[i]) {
			dev_warn(&pdev->dev, "rpmpds[] with empty entry at index=%d\n",
				 i);
			continue;
		}

		rpmpds[i]->rpm = rpm;
		rpmpds[i]->max_state = desc->max_state;
		rpmpds[i]->pd.power_off = rpmpd_power_off;
		rpmpds[i]->pd.power_on = rpmpd_power_on;
		rpmpds[i]->pd.set_performance_state = rpmpd_set_performance;
		rpmpds[i]->pd.opp_to_performance_state = rpmpd_get_performance;
		pm_genpd_init(&rpmpds[i]->pd, NULL, true);

		data->domains[i] = &rpmpds[i]->pd;
	}

	return of_genpd_add_provider_onecell(pdev->dev.of_node, data);
}

static struct platform_driver rpmpd_driver = {
	.driver = {
		.name = "qcom-rpmpd",
		.of_match_table = rpmpd_match_table,
		.suppress_bind_attrs = true,
	},
	.probe = rpmpd_probe,
};

static int __init rpmpd_init(void)
{
	return platform_driver_register(&rpmpd_driver);
}
core_initcall(rpmpd_init);

MODULE_DESCRIPTION("Qualcomm Technologies, Inc. RPM Power Domain Driver");
MODULE_LICENSE("GPL v2");
