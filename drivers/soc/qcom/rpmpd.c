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

/* Operation Keys */
#define KEY_CORNER		0x6e726f63 /* corn */
#define KEY_ENABLE		0x6e657773 /* swen */
#define KEY_FLOOR_CORNER	0x636676   /* vfc */
#define KEY_FLOOR_LEVEL		0x6c6676   /* vfl */
#define KEY_LEVEL		0x6c766c76 /* vlvl */

#define MAX_CORNER_RPMPD_STATE	6

#define DEFINE_RPMPD_PAIR(_platform, _name, _active, r_type, r_key,	\
			  r_id)						\
	static struct rpmpd _platform##_##_active;			\
	static struct rpmpd _platform##_##_name = {			\
		.pd = {	.name = #_name,	},				\
		.peer = &_platform##_##_active,				\
		.res_type = RPMPD_##r_type,				\
		.res_id = r_id,						\
		.key = KEY_##r_key,					\
	};								\
	static struct rpmpd _platform##_##_active = {			\
		.pd = { .name = #_active, },				\
		.peer = &_platform##_##_name,				\
		.active_only = true,					\
		.res_type = RPMPD_##r_type,				\
		.res_id = r_id,						\
		.key = KEY_##r_key,					\
	}

#define DEFINE_RPMPD_CORNER(_platform, _name, r_type, r_id)		\
	static struct rpmpd _platform##_##_name = {			\
		.pd = { .name = #_name, },				\
		.res_type = RPMPD_##r_type,				\
		.res_id = r_id,						\
		.key = KEY_CORNER,					\
	}

#define DEFINE_RPMPD_LEVEL(_platform, _name, r_type, r_id)		\
	static struct rpmpd _platform##_##_name = {			\
		.pd = { .name = #_name, },				\
		.res_type = RPMPD_##r_type,				\
		.res_id = r_id,						\
		.key = KEY_LEVEL,					\
	}

#define DEFINE_RPMPD_VFC(_platform, _name, r_type, r_id)		\
	static struct rpmpd _platform##_##_name = {			\
		.pd = { .name = #_name, },				\
		.res_type = RPMPD_##r_type,				\
		.res_id = r_id,						\
		.key = KEY_FLOOR_CORNER,				\
	}

#define DEFINE_RPMPD_VFL(_platform, _name, r_type, r_id)		\
	static struct rpmpd _platform##_##_name = {			\
		.pd = { .name = #_name, },				\
		.res_type = RPMPD_##r_type,				\
		.res_id = r_id,						\
		.key = KEY_FLOOR_LEVEL,					\
	}

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

/* mdm9607 RPM Power Domains */
DEFINE_RPMPD_PAIR(mdm9607, vddcx, vddcx_ao, SMPA, LEVEL, 3);
DEFINE_RPMPD_VFL(mdm9607, vddcx_vfl, SMPA, 3);

DEFINE_RPMPD_PAIR(mdm9607, vddmx, vddmx_ao, LDOA, LEVEL, 12);
DEFINE_RPMPD_VFL(mdm9607, vddmx_vfl, LDOA, 12);
static struct rpmpd *mdm9607_rpmpds[] = {
	[MDM9607_VDDCX] =	&mdm9607_vddcx,
	[MDM9607_VDDCX_AO] =	&mdm9607_vddcx_ao,
	[MDM9607_VDDCX_VFL] =	&mdm9607_vddcx_vfl,
	[MDM9607_VDDMX] =	&mdm9607_vddmx,
	[MDM9607_VDDMX_AO] =	&mdm9607_vddmx_ao,
	[MDM9607_VDDMX_VFL] =	&mdm9607_vddmx_vfl,
};

static const struct rpmpd_desc mdm9607_desc = {
	.rpmpds = mdm9607_rpmpds,
	.num_pds = ARRAY_SIZE(mdm9607_rpmpds),
	.max_state = RPM_SMD_LEVEL_TURBO,
};

/* msm8226 RPM Power Domains */
DEFINE_RPMPD_PAIR(msm8226, vddcx, vddcx_ao, SMPA, CORNER, 1);
DEFINE_RPMPD_VFC(msm8226, vddcx_vfc, SMPA, 1);

static struct rpmpd *msm8226_rpmpds[] = {
	[MSM8226_VDDCX] =	&msm8226_vddcx,
	[MSM8226_VDDCX_AO] =	&msm8226_vddcx_ao,
	[MSM8226_VDDCX_VFC] =	&msm8226_vddcx_vfc,
};

static const struct rpmpd_desc msm8226_desc = {
	.rpmpds = msm8226_rpmpds,
	.num_pds = ARRAY_SIZE(msm8226_rpmpds),
	.max_state = MAX_CORNER_RPMPD_STATE,
};

/* msm8939 RPM Power Domains */
DEFINE_RPMPD_PAIR(msm8939, vddmd, vddmd_ao, SMPA, CORNER, 1);
DEFINE_RPMPD_VFC(msm8939, vddmd_vfc, SMPA, 1);

DEFINE_RPMPD_PAIR(msm8939, vddcx, vddcx_ao, SMPA, CORNER, 2);
DEFINE_RPMPD_VFC(msm8939, vddcx_vfc, SMPA, 2);

DEFINE_RPMPD_PAIR(msm8939, vddmx, vddmx_ao, LDOA, CORNER, 3);

static struct rpmpd *msm8939_rpmpds[] = {
	[MSM8939_VDDMDCX] =	&msm8939_vddmd,
	[MSM8939_VDDMDCX_AO] =	&msm8939_vddmd_ao,
	[MSM8939_VDDMDCX_VFC] =	&msm8939_vddmd_vfc,
	[MSM8939_VDDCX] =	&msm8939_vddcx,
	[MSM8939_VDDCX_AO] =	&msm8939_vddcx_ao,
	[MSM8939_VDDCX_VFC] =	&msm8939_vddcx_vfc,
	[MSM8939_VDDMX] =	&msm8939_vddmx,
	[MSM8939_VDDMX_AO] =	&msm8939_vddmx_ao,
};

static const struct rpmpd_desc msm8939_desc = {
	.rpmpds = msm8939_rpmpds,
	.num_pds = ARRAY_SIZE(msm8939_rpmpds),
	.max_state = MAX_CORNER_RPMPD_STATE,
};

/* msm8916 RPM Power Domains */
DEFINE_RPMPD_PAIR(msm8916, vddcx, vddcx_ao, SMPA, CORNER, 1);
DEFINE_RPMPD_PAIR(msm8916, vddmx, vddmx_ao, LDOA, CORNER, 3);

DEFINE_RPMPD_VFC(msm8916, vddcx_vfc, SMPA, 1);

static struct rpmpd *msm8916_rpmpds[] = {
	[MSM8916_VDDCX] =	&msm8916_vddcx,
	[MSM8916_VDDCX_AO] =	&msm8916_vddcx_ao,
	[MSM8916_VDDCX_VFC] =	&msm8916_vddcx_vfc,
	[MSM8916_VDDMX] =	&msm8916_vddmx,
	[MSM8916_VDDMX_AO] =	&msm8916_vddmx_ao,
};

static const struct rpmpd_desc msm8916_desc = {
	.rpmpds = msm8916_rpmpds,
	.num_pds = ARRAY_SIZE(msm8916_rpmpds),
	.max_state = MAX_CORNER_RPMPD_STATE,
};

/* msm8953 RPM Power Domains */
DEFINE_RPMPD_PAIR(msm8953, vddmd, vddmd_ao, SMPA, LEVEL, 1);
DEFINE_RPMPD_PAIR(msm8953, vddcx, vddcx_ao, SMPA, LEVEL, 2);
DEFINE_RPMPD_PAIR(msm8953, vddmx, vddmx_ao, SMPA, LEVEL, 7);

DEFINE_RPMPD_VFL(msm8953, vddcx_vfl, SMPA, 2);

static struct rpmpd *msm8953_rpmpds[] = {
	[MSM8953_VDDMD] =	&msm8953_vddmd,
	[MSM8953_VDDMD_AO] =	&msm8953_vddmd_ao,
	[MSM8953_VDDCX] =	&msm8953_vddcx,
	[MSM8953_VDDCX_AO] =	&msm8953_vddcx_ao,
	[MSM8953_VDDCX_VFL] =	&msm8953_vddcx_vfl,
	[MSM8953_VDDMX] =	&msm8953_vddmx,
	[MSM8953_VDDMX_AO] =	&msm8953_vddmx_ao,
};

static const struct rpmpd_desc msm8953_desc = {
	.rpmpds = msm8953_rpmpds,
	.num_pds = ARRAY_SIZE(msm8953_rpmpds),
	.max_state = RPM_SMD_LEVEL_TURBO,
};

/* msm8976 RPM Power Domains */
DEFINE_RPMPD_PAIR(msm8976, vddcx, vddcx_ao, SMPA, LEVEL, 2);
DEFINE_RPMPD_PAIR(msm8976, vddmx, vddmx_ao, SMPA, LEVEL, 6);

DEFINE_RPMPD_VFL(msm8976, vddcx_vfl, RWSC, 2);
DEFINE_RPMPD_VFL(msm8976, vddmx_vfl, RWSM, 6);

static struct rpmpd *msm8976_rpmpds[] = {
	[MSM8976_VDDCX] =	&msm8976_vddcx,
	[MSM8976_VDDCX_AO] =	&msm8976_vddcx_ao,
	[MSM8976_VDDCX_VFL] =	&msm8976_vddcx_vfl,
	[MSM8976_VDDMX] =	&msm8976_vddmx,
	[MSM8976_VDDMX_AO] =	&msm8976_vddmx_ao,
	[MSM8976_VDDMX_VFL] =	&msm8976_vddmx_vfl,
};

static const struct rpmpd_desc msm8976_desc = {
	.rpmpds = msm8976_rpmpds,
	.num_pds = ARRAY_SIZE(msm8976_rpmpds),
	.max_state = RPM_SMD_LEVEL_TURBO_HIGH,
};

/* msm8994 RPM Power domains */
DEFINE_RPMPD_PAIR(msm8994, vddcx, vddcx_ao, SMPA, CORNER, 1);
DEFINE_RPMPD_PAIR(msm8994, vddmx, vddmx_ao, SMPA, CORNER, 2);
/* Attention! *Some* 8994 boards with pm8004 may use SMPC here! */
DEFINE_RPMPD_CORNER(msm8994, vddgfx, SMPB, 2);

DEFINE_RPMPD_VFC(msm8994, vddcx_vfc, SMPA, 1);
DEFINE_RPMPD_VFC(msm8994, vddgfx_vfc, SMPB, 2);

static struct rpmpd *msm8994_rpmpds[] = {
	[MSM8994_VDDCX] =	&msm8994_vddcx,
	[MSM8994_VDDCX_AO] =	&msm8994_vddcx_ao,
	[MSM8994_VDDCX_VFC] =	&msm8994_vddcx_vfc,
	[MSM8994_VDDMX] =	&msm8994_vddmx,
	[MSM8994_VDDMX_AO] =	&msm8994_vddmx_ao,
	[MSM8994_VDDGFX] =	&msm8994_vddgfx,
	[MSM8994_VDDGFX_VFC] =	&msm8994_vddgfx_vfc,
};

static const struct rpmpd_desc msm8994_desc = {
	.rpmpds = msm8994_rpmpds,
	.num_pds = ARRAY_SIZE(msm8994_rpmpds),
	.max_state = MAX_CORNER_RPMPD_STATE,
};

/* msm8996 RPM Power domains */
DEFINE_RPMPD_PAIR(msm8996, vddcx, vddcx_ao, SMPA, CORNER, 1);
DEFINE_RPMPD_PAIR(msm8996, vddmx, vddmx_ao, SMPA, CORNER, 2);
DEFINE_RPMPD_CORNER(msm8996, vddsscx, LDOA, 26);

DEFINE_RPMPD_VFC(msm8996, vddcx_vfc, SMPA, 1);
DEFINE_RPMPD_VFC(msm8996, vddsscx_vfc, LDOA, 26);

static struct rpmpd *msm8996_rpmpds[] = {
	[MSM8996_VDDCX] =	&msm8996_vddcx,
	[MSM8996_VDDCX_AO] =	&msm8996_vddcx_ao,
	[MSM8996_VDDCX_VFC] =	&msm8996_vddcx_vfc,
	[MSM8996_VDDMX] =	&msm8996_vddmx,
	[MSM8996_VDDMX_AO] =	&msm8996_vddmx_ao,
	[MSM8996_VDDSSCX] =	&msm8996_vddsscx,
	[MSM8996_VDDSSCX_VFC] =	&msm8996_vddsscx_vfc,
};

static const struct rpmpd_desc msm8996_desc = {
	.rpmpds = msm8996_rpmpds,
	.num_pds = ARRAY_SIZE(msm8996_rpmpds),
	.max_state = MAX_CORNER_RPMPD_STATE,
};

/* msm8998 RPM Power domains */
DEFINE_RPMPD_PAIR(msm8998, vddcx, vddcx_ao, RWCX, LEVEL, 0);
DEFINE_RPMPD_VFL(msm8998, vddcx_vfl, RWCX, 0);

DEFINE_RPMPD_PAIR(msm8998, vddmx, vddmx_ao, RWMX, LEVEL, 0);
DEFINE_RPMPD_VFL(msm8998, vddmx_vfl, RWMX, 0);

DEFINE_RPMPD_LEVEL(msm8998, vdd_ssccx, RWSC, 0);
DEFINE_RPMPD_VFL(msm8998, vdd_ssccx_vfl, RWSC, 0);

DEFINE_RPMPD_LEVEL(msm8998, vdd_sscmx, RWSM, 0);
DEFINE_RPMPD_VFL(msm8998, vdd_sscmx_vfl, RWSM, 0);

static struct rpmpd *msm8998_rpmpds[] = {
	[MSM8998_VDDCX] =		&msm8998_vddcx,
	[MSM8998_VDDCX_AO] =		&msm8998_vddcx_ao,
	[MSM8998_VDDCX_VFL] =		&msm8998_vddcx_vfl,
	[MSM8998_VDDMX] =		&msm8998_vddmx,
	[MSM8998_VDDMX_AO] =		&msm8998_vddmx_ao,
	[MSM8998_VDDMX_VFL] =		&msm8998_vddmx_vfl,
	[MSM8998_SSCCX] =		&msm8998_vdd_ssccx,
	[MSM8998_SSCCX_VFL] =		&msm8998_vdd_ssccx_vfl,
	[MSM8998_SSCMX] =		&msm8998_vdd_sscmx,
	[MSM8998_SSCMX_VFL] =		&msm8998_vdd_sscmx_vfl,
};

static const struct rpmpd_desc msm8998_desc = {
	.rpmpds = msm8998_rpmpds,
	.num_pds = ARRAY_SIZE(msm8998_rpmpds),
	.max_state = RPM_SMD_LEVEL_BINNING,
};

/* qcs404 RPM Power domains */
DEFINE_RPMPD_PAIR(qcs404, vddmx, vddmx_ao, RWMX, LEVEL, 0);
DEFINE_RPMPD_VFL(qcs404, vddmx_vfl, RWMX, 0);

DEFINE_RPMPD_LEVEL(qcs404, vdd_lpicx, RWLC, 0);
DEFINE_RPMPD_VFL(qcs404, vdd_lpicx_vfl, RWLC, 0);

DEFINE_RPMPD_LEVEL(qcs404, vdd_lpimx, RWLM, 0);
DEFINE_RPMPD_VFL(qcs404, vdd_lpimx_vfl, RWLM, 0);

static struct rpmpd *qcs404_rpmpds[] = {
	[QCS404_VDDMX] = &qcs404_vddmx,
	[QCS404_VDDMX_AO] = &qcs404_vddmx_ao,
	[QCS404_VDDMX_VFL] = &qcs404_vddmx_vfl,
	[QCS404_LPICX] = &qcs404_vdd_lpicx,
	[QCS404_LPICX_VFL] = &qcs404_vdd_lpicx_vfl,
	[QCS404_LPIMX] = &qcs404_vdd_lpimx,
	[QCS404_LPIMX_VFL] = &qcs404_vdd_lpimx_vfl,
};

static const struct rpmpd_desc qcs404_desc = {
	.rpmpds = qcs404_rpmpds,
	.num_pds = ARRAY_SIZE(qcs404_rpmpds),
	.max_state = RPM_SMD_LEVEL_BINNING,
};

/* sdm660 RPM Power domains */
DEFINE_RPMPD_PAIR(sdm660, vddcx, vddcx_ao, RWCX, LEVEL, 0);
DEFINE_RPMPD_VFL(sdm660, vddcx_vfl, RWCX, 0);

DEFINE_RPMPD_PAIR(sdm660, vddmx, vddmx_ao, RWMX, LEVEL, 0);
DEFINE_RPMPD_VFL(sdm660, vddmx_vfl, RWMX, 0);

DEFINE_RPMPD_LEVEL(sdm660, vdd_ssccx, RWLC, 0);
DEFINE_RPMPD_VFL(sdm660, vdd_ssccx_vfl, RWLC, 0);

DEFINE_RPMPD_LEVEL(sdm660, vdd_sscmx, RWLM, 0);
DEFINE_RPMPD_VFL(sdm660, vdd_sscmx_vfl, RWLM, 0);

static struct rpmpd *sdm660_rpmpds[] = {
	[SDM660_VDDCX] =		&sdm660_vddcx,
	[SDM660_VDDCX_AO] =		&sdm660_vddcx_ao,
	[SDM660_VDDCX_VFL] =		&sdm660_vddcx_vfl,
	[SDM660_VDDMX] =		&sdm660_vddmx,
	[SDM660_VDDMX_AO] =		&sdm660_vddmx_ao,
	[SDM660_VDDMX_VFL] =		&sdm660_vddmx_vfl,
	[SDM660_SSCCX] =		&sdm660_vdd_ssccx,
	[SDM660_SSCCX_VFL] =		&sdm660_vdd_ssccx_vfl,
	[SDM660_SSCMX] =		&sdm660_vdd_sscmx,
	[SDM660_SSCMX_VFL] =		&sdm660_vdd_sscmx_vfl,
};

static const struct rpmpd_desc sdm660_desc = {
	.rpmpds = sdm660_rpmpds,
	.num_pds = ARRAY_SIZE(sdm660_rpmpds),
	.max_state = RPM_SMD_LEVEL_TURBO,
};

/* sm4250/6115 RPM Power domains */
DEFINE_RPMPD_PAIR(sm6115, vddcx, vddcx_ao, RWCX, LEVEL, 0);
DEFINE_RPMPD_VFL(sm6115, vddcx_vfl, RWCX, 0);

DEFINE_RPMPD_PAIR(sm6115, vddmx, vddmx_ao, RWMX, LEVEL, 0);
DEFINE_RPMPD_VFL(sm6115, vddmx_vfl, RWMX, 0);

DEFINE_RPMPD_LEVEL(sm6115, vdd_lpi_cx, RWLC, 0);
DEFINE_RPMPD_LEVEL(sm6115, vdd_lpi_mx, RWLM, 0);

static struct rpmpd *sm6115_rpmpds[] = {
	[SM6115_VDDCX] =		&sm6115_vddcx,
	[SM6115_VDDCX_AO] =		&sm6115_vddcx_ao,
	[SM6115_VDDCX_VFL] =		&sm6115_vddcx_vfl,
	[SM6115_VDDMX] =		&sm6115_vddmx,
	[SM6115_VDDMX_AO] =		&sm6115_vddmx_ao,
	[SM6115_VDDMX_VFL] =		&sm6115_vddmx_vfl,
	[SM6115_VDD_LPI_CX] =		&sm6115_vdd_lpi_cx,
	[SM6115_VDD_LPI_MX] =		&sm6115_vdd_lpi_mx,
};

static const struct rpmpd_desc sm6115_desc = {
	.rpmpds = sm6115_rpmpds,
	.num_pds = ARRAY_SIZE(sm6115_rpmpds),
	.max_state = RPM_SMD_LEVEL_TURBO_NO_CPR,
};

/* sm6125 RPM Power domains */
DEFINE_RPMPD_PAIR(sm6125, vddcx, vddcx_ao, RWCX, LEVEL, 0);
DEFINE_RPMPD_VFL(sm6125, vddcx_vfl, RWCX, 0);

DEFINE_RPMPD_PAIR(sm6125, vddmx, vddmx_ao, RWMX, LEVEL, 0);
DEFINE_RPMPD_VFL(sm6125, vddmx_vfl, RWMX, 0);

static struct rpmpd *sm6125_rpmpds[] = {
	[SM6125_VDDCX] =		&sm6125_vddcx,
	[SM6125_VDDCX_AO] =		&sm6125_vddcx_ao,
	[SM6125_VDDCX_VFL] =		&sm6125_vddcx_vfl,
	[SM6125_VDDMX] =		&sm6125_vddmx,
	[SM6125_VDDMX_AO] =		&sm6125_vddmx_ao,
	[SM6125_VDDMX_VFL] =		&sm6125_vddmx_vfl,
};

static const struct rpmpd_desc sm6125_desc = {
	.rpmpds = sm6125_rpmpds,
	.num_pds = ARRAY_SIZE(sm6125_rpmpds),
	.max_state = RPM_SMD_LEVEL_BINNING,
};

static struct rpmpd *qcm2290_rpmpds[] = {
	[QCM2290_VDDCX] = &sm6115_vddcx,
	[QCM2290_VDDCX_AO] = &sm6115_vddcx_ao,
	[QCM2290_VDDCX_VFL] = &sm6115_vddcx_vfl,
	[QCM2290_VDDMX] = &sm6115_vddmx,
	[QCM2290_VDDMX_AO] = &sm6115_vddmx_ao,
	[QCM2290_VDDMX_VFL] = &sm6115_vddmx_vfl,
	[QCM2290_VDD_LPI_CX] = &sm6115_vdd_lpi_cx,
	[QCM2290_VDD_LPI_MX] = &sm6115_vdd_lpi_mx,
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
