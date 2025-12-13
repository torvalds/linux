// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) 2017-2018, The Linux Foundation. All rights reserved. */

#include <linux/cleanup.h>
#include <linux/err.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/pm_domain.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/pm_opp.h>
#include <linux/soc/qcom/smd-rpm.h>

#include <dt-bindings/power/qcom-rpmpd.h>

#define domain_to_rpmpd(domain) container_of(domain, struct rpmpd, pd)

static struct qcom_smd_rpm *rpmpd_smd_rpm;

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
	struct generic_pm_domain *parent;
	struct rpmpd *peer;
	const bool active_only;
	unsigned int corner;
	bool enabled;
	const int res_type;
	const int res_id;
	unsigned int max_state;
	__le32 key;
	bool state_synced;
};

struct rpmpd_desc {
	struct rpmpd **rpmpds;
	size_t num_pds;
	unsigned int max_state;
};

static DEFINE_MUTEX(rpmpd_lock);

/* CX */
static struct rpmpd cx_rwcx0_lvl_ao;
static struct rpmpd cx_rwcx0_lvl = {
	.pd = { .name = "cx", },
	.peer = &cx_rwcx0_lvl_ao,
	.res_type = RPMPD_RWCX,
	.res_id = 0,
	.key = KEY_LEVEL,
};

static struct rpmpd cx_rwcx0_lvl_ao = {
	.pd = { .name = "cx_ao", },
	.peer = &cx_rwcx0_lvl,
	.active_only = true,
	.res_type = RPMPD_RWCX,
	.res_id = 0,
	.key = KEY_LEVEL,
};

static struct rpmpd cx_s1a_corner_ao;
static struct rpmpd cx_s1a_corner = {
	.pd = { .name = "cx", },
	.peer = &cx_s1a_corner_ao,
	.res_type = RPMPD_SMPA,
	.res_id = 1,
	.key = KEY_CORNER,
};

static struct rpmpd cx_s1a_corner_ao = {
	.pd = { .name = "cx_ao", },
	.peer = &cx_s1a_corner,
	.active_only = true,
	.res_type = RPMPD_SMPA,
	.res_id = 1,
	.key = KEY_CORNER,
};

static struct rpmpd cx_s1a_lvl_ao;
static struct rpmpd cx_s1a_lvl = {
	.pd = { .name = "cx", },
	.peer = &cx_s1a_lvl_ao,
	.res_type = RPMPD_SMPA,
	.res_id = 1,
	.key = KEY_LEVEL,
};

static struct rpmpd cx_s1a_lvl_ao = {
	.pd = { .name = "cx_ao", },
	.peer = &cx_s1a_lvl,
	.active_only = true,
	.res_type = RPMPD_SMPA,
	.res_id = 1,
	.key = KEY_LEVEL,
};

static struct rpmpd cx_s2a_corner_ao;
static struct rpmpd cx_s2a_corner = {
	.pd = { .name = "cx", },
	.peer = &cx_s2a_corner_ao,
	.res_type = RPMPD_SMPA,
	.res_id = 2,
	.key = KEY_CORNER,
};

static struct rpmpd cx_s2a_corner_ao = {
	.pd = { .name = "cx_ao", },
	.peer = &cx_s2a_corner,
	.active_only = true,
	.res_type = RPMPD_SMPA,
	.res_id = 2,
	.key = KEY_CORNER,
};

static struct rpmpd cx_s2a_lvl_ao;
static struct rpmpd cx_s2a_lvl = {
	.pd = { .name = "cx", },
	.peer = &cx_s2a_lvl_ao,
	.res_type = RPMPD_SMPA,
	.res_id = 2,
	.key = KEY_LEVEL,
};

static struct rpmpd cx_s2a_lvl_ao = {
	.pd = { .name = "cx_ao", },
	.peer = &cx_s2a_lvl,
	.active_only = true,
	.res_type = RPMPD_SMPA,
	.res_id = 2,
	.key = KEY_LEVEL,
};

static struct rpmpd cx_s3a_lvl_ao;
static struct rpmpd cx_s3a_lvl = {
	.pd = { .name = "cx", },
	.peer = &cx_s3a_lvl_ao,
	.res_type = RPMPD_SMPA,
	.res_id = 3,
	.key = KEY_LEVEL,
};

static struct rpmpd cx_s3a_lvl_ao = {
	.pd = { .name = "cx_ao", },
	.peer = &cx_s3a_lvl,
	.active_only = true,
	.res_type = RPMPD_SMPA,
	.res_id = 3,
	.key = KEY_LEVEL,
};

static struct rpmpd cx_rwcx0_vfl = {
	.pd = { .name = "cx_vfl", },
	.res_type = RPMPD_RWCX,
	.res_id = 0,
	.key = KEY_FLOOR_LEVEL,
};

static struct rpmpd cx_rwsc2_vfl = {
	.pd = { .name = "cx_vfl", },
	.res_type = RPMPD_RWSC,
	.res_id = 2,
	.key = KEY_FLOOR_LEVEL,
};

static struct rpmpd cx_s1a_vfc = {
	.pd = { .name = "cx_vfc", },
	.res_type = RPMPD_SMPA,
	.res_id = 1,
	.key = KEY_FLOOR_CORNER,
};

static struct rpmpd cx_s1a_vfl = {
	.pd = { .name = "cx_vfl", },
	.res_type = RPMPD_SMPA,
	.res_id = 1,
	.key = KEY_FLOOR_LEVEL,
};

static struct rpmpd cx_s2a_vfc = {
	.pd = { .name = "cx_vfc", },
	.res_type = RPMPD_SMPA,
	.res_id = 2,
	.key = KEY_FLOOR_CORNER,
};

static struct rpmpd cx_s2a_vfl = {
	.pd = { .name = "cx_vfl", },
	.res_type = RPMPD_SMPA,
	.res_id = 2,
	.key = KEY_FLOOR_LEVEL,
};

static struct rpmpd cx_s3a_vfl = {
	.pd = { .name = "cx_vfl", },
	.res_type = RPMPD_SMPA,
	.res_id = 3,
	.key = KEY_FLOOR_LEVEL,
};

static struct rpmpd cx_s2b_corner_ao;
static struct rpmpd cx_s2b_corner = {
	.pd = { .name = "cx", },
	.peer = &cx_s2b_corner_ao,
	.res_type = RPMPD_SMPB,
	.res_id = 2,
	.key = KEY_CORNER,
};

static struct rpmpd cx_s2b_corner_ao = {
	.pd = { .name = "cx_ao", },
	.peer = &cx_s2b_corner,
	.active_only = true,
	.res_type = RPMPD_SMPB,
	.res_id = 2,
	.key = KEY_CORNER,
};

static struct rpmpd cx_s2b_vfc = {
	.pd = { .name = "cx_vfc", },
	.res_type = RPMPD_SMPB,
	.res_id = 2,
	.key = KEY_FLOOR_CORNER,
};

/* G(F)X */
static struct rpmpd gfx_s7a_corner = {
	.pd = { .name = "gfx", },
	.res_type = RPMPD_SMPA,
	.res_id = 7,
	.key = KEY_CORNER,
};

static struct rpmpd gfx_s7a_vfc = {
	.pd = { .name = "gfx_vfc", },
	.res_type = RPMPD_SMPA,
	.res_id = 7,
	.key = KEY_FLOOR_CORNER,
};

static struct rpmpd gfx_s2b_corner = {
	.pd = { .name = "gfx", },
	.res_type = RPMPD_SMPB,
	.res_id = 2,
	.key = KEY_CORNER,
};

static struct rpmpd gfx_s2b_vfc = {
	.pd = { .name = "gfx_vfc", },
	.res_type = RPMPD_SMPB,
	.res_id = 2,
	.key = KEY_FLOOR_CORNER,
};

static struct rpmpd gfx_s4b_corner = {
	.pd = { .name = "gfx", },
	.res_type = RPMPD_SMPB,
	.res_id = 4,
	.key = KEY_CORNER,
};

static struct rpmpd gfx_s4b_vfc = {
	.pd = { .name = "gfx_vfc", },
	.res_type = RPMPD_SMPB,
	.res_id = 4,
	.key = KEY_FLOOR_CORNER,
};

static struct rpmpd mx_rwmx0_lvl;
static struct rpmpd gx_rwgx0_lvl_ao;
static struct rpmpd gx_rwgx0_lvl = {
	.pd = { .name = "gx", },
	.peer = &gx_rwgx0_lvl_ao,
	.res_type = RPMPD_RWGX,
	.parent = &mx_rwmx0_lvl.pd,
	.res_id = 0,
	.key = KEY_LEVEL,
};

static struct rpmpd mx_rwmx0_lvl_ao;
static struct rpmpd gx_rwgx0_lvl_ao = {
	.pd = { .name = "gx_ao", },
	.peer = &gx_rwgx0_lvl,
	.parent = &mx_rwmx0_lvl_ao.pd,
	.active_only = true,
	.res_type = RPMPD_RWGX,
	.res_id = 0,
	.key = KEY_LEVEL,
};

/* MX */
static struct rpmpd mx_l2a_lvl_ao;
static struct rpmpd mx_l2a_lvl = {
	.pd = { .name = "mx", },
	.peer = &mx_l2a_lvl_ao,
	.res_type = RPMPD_LDOA,
	.res_id = 2,
	.key = KEY_LEVEL,
};

static struct rpmpd mx_l2a_lvl_ao = {
	.pd = { .name = "mx_ao", },
	.peer = &mx_l2a_lvl,
	.active_only = true,
	.res_type = RPMPD_LDOA,
	.res_id = 2,
	.key = KEY_LEVEL,
};

static struct rpmpd mx_l3a_corner_ao;
static struct rpmpd mx_l3a_corner = {
	.pd = { .name = "mx", },
	.peer = &mx_l3a_corner_ao,
	.res_type = RPMPD_LDOA,
	.res_id = 3,
	.key = KEY_CORNER,
};

static struct rpmpd mx_l3a_corner_ao = {
	.pd = { .name = "mx_ao", },
	.peer = &mx_l3a_corner,
	.active_only = true,
	.res_type = RPMPD_LDOA,
	.res_id = 3,
	.key = KEY_CORNER,
};

static struct rpmpd mx_l3a_lvl_ao;
static struct rpmpd mx_l3a_lvl = {
	.pd = { .name = "mx", },
	.peer = &mx_l3a_lvl_ao,
	.res_type = RPMPD_LDOA,
	.res_id = 3,
	.key = KEY_LEVEL,
};

static struct rpmpd mx_l3a_lvl_ao = {
	.pd = { .name = "mx_ao", },
	.peer = &mx_l3a_lvl,
	.active_only = true,
	.res_type = RPMPD_LDOA,
	.res_id = 3,
	.key = KEY_LEVEL,
};

static struct rpmpd mx_l12a_lvl_ao;
static struct rpmpd mx_l12a_lvl = {
	.pd = { .name = "mx", },
	.peer = &mx_l12a_lvl_ao,
	.res_type = RPMPD_LDOA,
	.res_id = 12,
	.key = KEY_LEVEL,
};

static struct rpmpd mx_l12a_lvl_ao = {
	.pd = { .name = "mx_ao", },
	.peer = &mx_l12a_lvl,
	.active_only = true,
	.res_type = RPMPD_LDOA,
	.res_id = 12,
	.key = KEY_LEVEL,
};

static struct rpmpd mx_s2a_corner_ao;
static struct rpmpd mx_s2a_corner = {
	.pd = { .name = "mx", },
	.peer = &mx_s2a_corner_ao,
	.res_type = RPMPD_SMPA,
	.res_id = 2,
	.key = KEY_CORNER,
};

static struct rpmpd mx_s2a_corner_ao = {
	.pd = { .name = "mx_ao", },
	.peer = &mx_s2a_corner,
	.active_only = true,
	.res_type = RPMPD_SMPA,
	.res_id = 2,
	.key = KEY_CORNER,
};

static struct rpmpd mx_rwmx0_lvl_ao;
static struct rpmpd mx_rwmx0_lvl = {
	.pd = { .name = "mx", },
	.peer = &mx_rwmx0_lvl_ao,
	.res_type = RPMPD_RWMX,
	.res_id = 0,
	.key = KEY_LEVEL,
};

static struct rpmpd mx_rwmx0_lvl_ao = {
	.pd = { .name = "mx_ao", },
	.peer = &mx_rwmx0_lvl,
	.active_only = true,
	.res_type = RPMPD_RWMX,
	.res_id = 0,
	.key = KEY_LEVEL,
};

static struct rpmpd mx_s6a_lvl_ao;
static struct rpmpd mx_s6a_lvl = {
	.pd = { .name = "mx", },
	.peer = &mx_s6a_lvl_ao,
	.res_type = RPMPD_SMPA,
	.res_id = 6,
	.key = KEY_LEVEL,
};

static struct rpmpd mx_s6a_lvl_ao = {
	.pd = { .name = "mx_ao", },
	.peer = &mx_s6a_lvl,
	.active_only = true,
	.res_type = RPMPD_SMPA,
	.res_id = 6,
	.key = KEY_LEVEL,
};

static struct rpmpd mx_s7a_lvl_ao;
static struct rpmpd mx_s7a_lvl = {
	.pd = { .name = "mx", },
	.peer = &mx_s7a_lvl_ao,
	.res_type = RPMPD_SMPA,
	.res_id = 7,
	.key = KEY_LEVEL,
};

static struct rpmpd mx_s7a_lvl_ao = {
	.pd = { .name = "mx_ao", },
	.peer = &mx_s7a_lvl,
	.active_only = true,
	.res_type = RPMPD_SMPA,
	.res_id = 7,
	.key = KEY_LEVEL,
};

static struct rpmpd mx_l12a_vfl = {
	.pd = { .name = "mx_vfl", },
	.res_type = RPMPD_LDOA,
	.res_id = 12,
	.key = KEY_FLOOR_LEVEL,
};

static struct rpmpd mx_rwmx0_vfl = {
	.pd = { .name = "mx_vfl", },
	.res_type = RPMPD_RWMX,
	.res_id = 0,
	.key = KEY_FLOOR_LEVEL,
};

static struct rpmpd mx_rwsm6_vfl = {
	.pd = { .name = "mx_vfl", },
	.res_type = RPMPD_RWSM,
	.res_id = 6,
	.key = KEY_FLOOR_LEVEL,
};

/* MD */
static struct rpmpd md_s1a_corner_ao;
static struct rpmpd md_s1a_corner = {
	.pd = { .name = "md", },
	.peer = &md_s1a_corner_ao,
	.res_type = RPMPD_SMPA,
	.res_id = 1,
	.key = KEY_CORNER,
};

static struct rpmpd md_s1a_corner_ao = {
	.pd = { .name = "md_ao", },
	.peer = &md_s1a_corner,
	.active_only = true,
	.res_type = RPMPD_SMPA,
	.res_id = 1,
	.key = KEY_CORNER,
};

static struct rpmpd md_s1a_lvl_ao;
static struct rpmpd md_s1a_lvl = {
	.pd = { .name = "md", },
	.peer = &md_s1a_lvl_ao,
	.res_type = RPMPD_SMPA,
	.res_id = 1,
	.key = KEY_LEVEL,
};

static struct rpmpd md_s1a_lvl_ao = {
	.pd = { .name = "md_ao", },
	.peer = &md_s1a_lvl,
	.active_only = true,
	.res_type = RPMPD_SMPA,
	.res_id = 1,
	.key = KEY_LEVEL,
};

static struct rpmpd md_s1a_vfc = {
	.pd = { .name = "md_vfc", },
	.res_type = RPMPD_SMPA,
	.res_id = 1,
	.key = KEY_FLOOR_CORNER,
};

/* LPI_CX */
static struct rpmpd lpi_cx_rwlc0_lvl = {
	.pd = { .name = "lpi_cx", },
	.res_type = RPMPD_RWLC,
	.res_id = 0,
	.key = KEY_LEVEL,
};

static struct rpmpd lpi_cx_rwlc0_vfl = {
	.pd = { .name = "lpi_cx_vfl", },
	.res_type = RPMPD_RWLC,
	.res_id = 0,
	.key = KEY_FLOOR_LEVEL,
};

/* LPI_MX */
static struct rpmpd lpi_mx_rwlm0_lvl = {
	.pd = { .name = "lpi_mx", },
	.res_type = RPMPD_RWLM,
	.res_id = 0,
	.key = KEY_LEVEL,
};

static struct rpmpd lpi_mx_rwlm0_vfl = {
	.pd = { .name = "lpi_mx_vfl", },
	.res_type = RPMPD_RWLM,
	.res_id = 0,
	.key = KEY_FLOOR_LEVEL,
};

/* SSC_CX */
static struct rpmpd ssc_cx_l26a_corner = {
	.pd = { .name = "ssc_cx", },
	.res_type = RPMPD_LDOA,
	.res_id = 26,
	.key = KEY_CORNER,
};

static struct rpmpd ssc_cx_rwlc0_lvl = {
	.pd = { .name = "ssc_cx", },
	.res_type = RPMPD_RWLC,
	.res_id = 0,
	.key = KEY_LEVEL,
};

static struct rpmpd ssc_cx_rwsc0_lvl = {
	.pd = { .name = "ssc_cx", },
	.res_type = RPMPD_RWSC,
	.res_id = 0,
	.key = KEY_LEVEL,
};

static struct rpmpd ssc_cx_l26a_vfc = {
	.pd = { .name = "ssc_cx_vfc", },
	.res_type = RPMPD_LDOA,
	.res_id = 26,
	.key = KEY_FLOOR_CORNER,
};

static struct rpmpd ssc_cx_rwlc0_vfl = {
	.pd = { .name = "ssc_cx_vfl", },
	.res_type = RPMPD_RWLC,
	.res_id = 0,
	.key = KEY_FLOOR_LEVEL,
};

static struct rpmpd ssc_cx_rwsc0_vfl = {
	.pd = { .name = "ssc_cx_vfl", },
	.res_type = RPMPD_RWSC,
	.res_id = 0,
	.key = KEY_FLOOR_LEVEL,
};

/* SSC_MX */
static struct rpmpd ssc_mx_rwlm0_lvl = {
	.pd = { .name = "ssc_mx", },
	.res_type = RPMPD_RWLM,
	.res_id = 0,
	.key = KEY_LEVEL,
};

static struct rpmpd ssc_mx_rwsm0_lvl = {
	.pd = { .name = "ssc_mx", },
	.res_type = RPMPD_RWSM,
	.res_id = 0,
	.key = KEY_LEVEL,
};

static struct rpmpd ssc_mx_rwlm0_vfl = {
	.pd = { .name = "ssc_mx_vfl", },
	.res_type = RPMPD_RWLM,
	.res_id = 0,
	.key = KEY_FLOOR_LEVEL,
};

static struct rpmpd ssc_mx_rwsm0_vfl = {
	.pd = { .name = "ssc_mx_vfl", },
	.res_type = RPMPD_RWSM,
	.res_id = 0,
	.key = KEY_FLOOR_LEVEL,
};

static struct rpmpd *mdm9607_rpmpds[] = {
	[RPMPD_VDDCX] =		&cx_s3a_lvl,
	[RPMPD_VDDCX_AO] =	&cx_s3a_lvl_ao,
	[RPMPD_VDDCX_VFL] =	&cx_s3a_vfl,
	[RPMPD_VDDMX] =		&mx_l12a_lvl,
	[RPMPD_VDDMX_AO] =	&mx_l12a_lvl_ao,
	[RPMPD_VDDMX_VFL] =	&mx_l12a_vfl,
};

static const struct rpmpd_desc mdm9607_desc = {
	.rpmpds = mdm9607_rpmpds,
	.num_pds = ARRAY_SIZE(mdm9607_rpmpds),
	.max_state = RPM_SMD_LEVEL_TURBO,
};

static struct rpmpd *msm8226_rpmpds[] = {
	[RPMPD_VDDCX] =		&cx_s1a_corner,
	[RPMPD_VDDCX_AO] =	&cx_s1a_corner_ao,
	[RPMPD_VDDCX_VFC] =	&cx_s1a_vfc,
};

static const struct rpmpd_desc msm8226_desc = {
	.rpmpds = msm8226_rpmpds,
	.num_pds = ARRAY_SIZE(msm8226_rpmpds),
	.max_state = MAX_CORNER_RPMPD_STATE,
};

static struct rpmpd *msm8939_rpmpds[] = {
	[MSM8939_VDDMDCX] =	&md_s1a_corner,
	[MSM8939_VDDMDCX_AO] =	&md_s1a_corner_ao,
	[MSM8939_VDDMDCX_VFC] =	&md_s1a_vfc,
	[MSM8939_VDDCX] =	&cx_s2a_corner,
	[MSM8939_VDDCX_AO] =	&cx_s2a_corner_ao,
	[MSM8939_VDDCX_VFC] =	&cx_s2a_vfc,
	[MSM8939_VDDMX] =	&mx_l3a_corner,
	[MSM8939_VDDMX_AO] =	&mx_l3a_corner_ao,
};

static const struct rpmpd_desc msm8939_desc = {
	.rpmpds = msm8939_rpmpds,
	.num_pds = ARRAY_SIZE(msm8939_rpmpds),
	.max_state = MAX_CORNER_RPMPD_STATE,
};

static struct rpmpd *msm8916_rpmpds[] = {
	[RPMPD_VDDCX] =		&cx_s1a_corner,
	[RPMPD_VDDCX_AO] =	&cx_s1a_corner_ao,
	[RPMPD_VDDCX_VFC] =	&cx_s1a_vfc,
	[RPMPD_VDDMX] =		&mx_l3a_corner,
	[RPMPD_VDDMX_AO] =	&mx_l3a_corner_ao,
};

static const struct rpmpd_desc msm8916_desc = {
	.rpmpds = msm8916_rpmpds,
	.num_pds = ARRAY_SIZE(msm8916_rpmpds),
	.max_state = MAX_CORNER_RPMPD_STATE,
};

static struct rpmpd *msm8917_rpmpds[] = {
	[RPMPD_VDDCX] =		&cx_s2a_lvl,
	[RPMPD_VDDCX_AO] =	&cx_s2a_lvl_ao,
	[RPMPD_VDDCX_VFL] =	&cx_s2a_vfl,
	[RPMPD_VDDMX] =		&mx_l3a_lvl,
	[RPMPD_VDDMX_AO] =	&mx_l3a_lvl_ao,
};

static const struct rpmpd_desc msm8917_desc = {
	.rpmpds = msm8917_rpmpds,
	.num_pds = ARRAY_SIZE(msm8917_rpmpds),
	.max_state = RPM_SMD_LEVEL_TURBO,
};

static struct rpmpd *msm8953_rpmpds[] = {
	[MSM8953_VDDMD] =	&md_s1a_lvl,
	[MSM8953_VDDMD_AO] =	&md_s1a_lvl_ao,
	[MSM8953_VDDCX] =	&cx_s2a_lvl,
	[MSM8953_VDDCX_AO] =	&cx_s2a_lvl_ao,
	[MSM8953_VDDCX_VFL] =	&cx_s2a_vfl,
	[MSM8953_VDDMX] =	&mx_s7a_lvl,
	[MSM8953_VDDMX_AO] =	&mx_s7a_lvl_ao,
};

static const struct rpmpd_desc msm8953_desc = {
	.rpmpds = msm8953_rpmpds,
	.num_pds = ARRAY_SIZE(msm8953_rpmpds),
	.max_state = RPM_SMD_LEVEL_TURBO,
};

static struct rpmpd *msm8974_rpmpds[] = {
	[MSM8974_VDDCX] =	&cx_s2b_corner,
	[MSM8974_VDDCX_AO] =	&cx_s2b_corner_ao,
	[MSM8974_VDDCX_VFC] =	&cx_s2b_vfc,
	[MSM8974_VDDGFX] =	&gfx_s4b_corner,
	[MSM8974_VDDGFX_VFC] =	&gfx_s4b_vfc,
};

static const struct rpmpd_desc msm8974_desc = {
	.rpmpds = msm8974_rpmpds,
	.num_pds = ARRAY_SIZE(msm8974_rpmpds),
	.max_state = MAX_CORNER_RPMPD_STATE,
};

static struct rpmpd *msm8974pro_pma8084_rpmpds[] = {
	[MSM8974_VDDCX] =	&cx_s2a_corner,
	[MSM8974_VDDCX_AO] =	&cx_s2a_corner_ao,
	[MSM8974_VDDCX_VFC] =	&cx_s2a_vfc,
	[MSM8974_VDDGFX] =	&gfx_s7a_corner,
	[MSM8974_VDDGFX_VFC] =	&gfx_s7a_vfc,
};

static const struct rpmpd_desc msm8974pro_pma8084_desc = {
	.rpmpds = msm8974pro_pma8084_rpmpds,
	.num_pds = ARRAY_SIZE(msm8974pro_pma8084_rpmpds),
	.max_state = MAX_CORNER_RPMPD_STATE,
};

static struct rpmpd *msm8976_rpmpds[] = {
	[RPMPD_VDDCX] =		&cx_s2a_lvl,
	[RPMPD_VDDCX_AO] =	&cx_s2a_lvl_ao,
	[RPMPD_VDDCX_VFL] =	&cx_rwsc2_vfl,
	[RPMPD_VDDMX] =		&mx_s6a_lvl,
	[RPMPD_VDDMX_AO] =	&mx_s6a_lvl_ao,
	[RPMPD_VDDMX_VFL] =	&mx_rwsm6_vfl,
};

static const struct rpmpd_desc msm8976_desc = {
	.rpmpds = msm8976_rpmpds,
	.num_pds = ARRAY_SIZE(msm8976_rpmpds),
	.max_state = RPM_SMD_LEVEL_TURBO_HIGH,
};

static struct rpmpd *msm8994_rpmpds[] = {
	[MSM8994_VDDCX] =	&cx_s1a_corner,
	[MSM8994_VDDCX_AO] =	&cx_s1a_corner_ao,
	[MSM8994_VDDCX_VFC] =	&cx_s1a_vfc,
	[MSM8994_VDDMX] =	&mx_s2a_corner,
	[MSM8994_VDDMX_AO] =	&mx_s2a_corner_ao,

	/* Attention! *Some* 8994 boards with pm8004 may use SMPC here! */
	[MSM8994_VDDGFX] =	&gfx_s2b_corner,
	[MSM8994_VDDGFX_VFC] =	&gfx_s2b_vfc,
};

static const struct rpmpd_desc msm8994_desc = {
	.rpmpds = msm8994_rpmpds,
	.num_pds = ARRAY_SIZE(msm8994_rpmpds),
	.max_state = MAX_CORNER_RPMPD_STATE,
};

static struct rpmpd *msm8996_rpmpds[] = {
	[MSM8996_VDDCX] =	&cx_s1a_corner,
	[MSM8996_VDDCX_AO] =	&cx_s1a_corner_ao,
	[MSM8996_VDDCX_VFC] =	&cx_s1a_vfc,
	[MSM8996_VDDMX] =	&mx_s2a_corner,
	[MSM8996_VDDMX_AO] =	&mx_s2a_corner_ao,
	[MSM8996_VDDSSCX] =	&ssc_cx_l26a_corner,
	[MSM8996_VDDSSCX_VFC] =	&ssc_cx_l26a_vfc,
};

static const struct rpmpd_desc msm8996_desc = {
	.rpmpds = msm8996_rpmpds,
	.num_pds = ARRAY_SIZE(msm8996_rpmpds),
	.max_state = MAX_CORNER_RPMPD_STATE,
};

static struct rpmpd *msm8998_rpmpds[] = {
	[RPMPD_VDDCX] =		&cx_rwcx0_lvl,
	[RPMPD_VDDCX_AO] =	&cx_rwcx0_lvl_ao,
	[RPMPD_VDDCX_VFL] =	&cx_rwcx0_vfl,
	[RPMPD_VDDMX] =		&mx_rwmx0_lvl,
	[RPMPD_VDDMX_AO] =	&mx_rwmx0_lvl_ao,
	[RPMPD_VDDMX_VFL] =	&mx_rwmx0_vfl,
	[RPMPD_SSCCX] =		&ssc_cx_rwsc0_lvl,
	[RPMPD_SSCCX_VFL] =	&ssc_cx_rwsc0_vfl,
	[RPMPD_SSCMX] =		&ssc_mx_rwsm0_lvl,
	[RPMPD_SSCMX_VFL] =	&ssc_mx_rwsm0_vfl,
};

static const struct rpmpd_desc msm8998_desc = {
	.rpmpds = msm8998_rpmpds,
	.num_pds = ARRAY_SIZE(msm8998_rpmpds),
	.max_state = RPM_SMD_LEVEL_BINNING,
};

static struct rpmpd *qcs404_rpmpds[] = {
	[QCS404_VDDMX] =	&mx_rwmx0_lvl,
	[QCS404_VDDMX_AO] =	&mx_rwmx0_lvl_ao,
	[QCS404_VDDMX_VFL] =	&mx_rwmx0_vfl,
	[QCS404_LPICX] =	&lpi_cx_rwlc0_lvl,
	[QCS404_LPICX_VFL] =	&lpi_cx_rwlc0_vfl,
	[QCS404_LPIMX] =	&lpi_mx_rwlm0_lvl,
	[QCS404_LPIMX_VFL] =	&lpi_mx_rwlm0_vfl,
};

static const struct rpmpd_desc qcs404_desc = {
	.rpmpds = qcs404_rpmpds,
	.num_pds = ARRAY_SIZE(qcs404_rpmpds),
	.max_state = RPM_SMD_LEVEL_BINNING,
};

static struct rpmpd *qm215_rpmpds[] = {
	[RPMPD_VDDCX] =		&cx_s1a_lvl,
	[RPMPD_VDDCX_AO] =	&cx_s1a_lvl_ao,
	[RPMPD_VDDCX_VFL] =	&cx_s1a_vfl,
	[RPMPD_VDDMX] =		&mx_l2a_lvl,
	[RPMPD_VDDMX_AO] =	&mx_l2a_lvl_ao,
};

static const struct rpmpd_desc qm215_desc = {
	.rpmpds = qm215_rpmpds,
	.num_pds = ARRAY_SIZE(qm215_rpmpds),
	.max_state = RPM_SMD_LEVEL_TURBO,
};

static struct rpmpd *sdm660_rpmpds[] = {
	[RPMPD_VDDCX] =		&cx_rwcx0_lvl,
	[RPMPD_VDDCX_AO] =	&cx_rwcx0_lvl_ao,
	[RPMPD_VDDCX_VFL] =	&cx_rwcx0_vfl,
	[RPMPD_VDDMX] =		&mx_rwmx0_lvl,
	[RPMPD_VDDMX_AO] =	&mx_rwmx0_lvl_ao,
	[RPMPD_VDDMX_VFL] =	&mx_rwmx0_vfl,
	[RPMPD_SSCCX] =		&ssc_cx_rwlc0_lvl,
	[RPMPD_SSCCX_VFL] =	&ssc_cx_rwlc0_vfl,
	[RPMPD_SSCMX] =		&ssc_mx_rwlm0_lvl,
	[RPMPD_SSCMX_VFL] =	&ssc_mx_rwlm0_vfl,
};

static const struct rpmpd_desc sdm660_desc = {
	.rpmpds = sdm660_rpmpds,
	.num_pds = ARRAY_SIZE(sdm660_rpmpds),
	.max_state = RPM_SMD_LEVEL_TURBO,
};

static struct rpmpd *sm6115_rpmpds[] = {
	[SM6115_VDDCX] =	&cx_rwcx0_lvl,
	[SM6115_VDDCX_AO] =	&cx_rwcx0_lvl_ao,
	[SM6115_VDDCX_VFL] =	&cx_rwcx0_vfl,
	[SM6115_VDDMX] =	&mx_rwmx0_lvl,
	[SM6115_VDDMX_AO] =	&mx_rwmx0_lvl_ao,
	[SM6115_VDDMX_VFL] =	&mx_rwmx0_vfl,
	[SM6115_VDD_LPI_CX] =	&lpi_cx_rwlc0_lvl,
	[SM6115_VDD_LPI_MX] =	&lpi_mx_rwlm0_lvl,
};

static const struct rpmpd_desc sm6115_desc = {
	.rpmpds = sm6115_rpmpds,
	.num_pds = ARRAY_SIZE(sm6115_rpmpds),
	.max_state = RPM_SMD_LEVEL_TURBO_NO_CPR,
};

static struct rpmpd *sm6125_rpmpds[] = {
	[RPMPD_VDDCX] =		&cx_rwcx0_lvl,
	[RPMPD_VDDCX_AO] =	&cx_rwcx0_lvl_ao,
	[RPMPD_VDDCX_VFL] =	&cx_rwcx0_vfl,
	[RPMPD_VDDMX] =		&mx_rwmx0_lvl,
	[RPMPD_VDDMX_AO] =	&mx_rwmx0_lvl_ao,
	[RPMPD_VDDMX_VFL] =	&mx_rwmx0_vfl,
};

static const struct rpmpd_desc sm6125_desc = {
	.rpmpds = sm6125_rpmpds,
	.num_pds = ARRAY_SIZE(sm6125_rpmpds),
	.max_state = RPM_SMD_LEVEL_BINNING,
};

static struct rpmpd *sm6375_rpmpds[] = {
	[SM6375_VDDCX] =	&cx_rwcx0_lvl,
	[SM6375_VDDCX_AO] =	&cx_rwcx0_lvl_ao,
	[SM6375_VDDCX_VFL] =	&cx_rwcx0_vfl,
	[SM6375_VDDMX] =	&mx_rwmx0_lvl,
	[SM6375_VDDMX_AO] =	&mx_rwmx0_lvl_ao,
	[SM6375_VDDMX_VFL] =	&mx_rwmx0_vfl,
	[SM6375_VDDGX] =	&gx_rwgx0_lvl,
	[SM6375_VDDGX_AO] =	&gx_rwgx0_lvl_ao,
	[SM6375_VDD_LPI_CX] =	&lpi_cx_rwlc0_lvl,
	[SM6375_VDD_LPI_MX] =	&lpi_mx_rwlm0_lvl,
};

static const struct rpmpd_desc sm6375_desc = {
	.rpmpds = sm6375_rpmpds,
	.num_pds = ARRAY_SIZE(sm6375_rpmpds),
	.max_state = RPM_SMD_LEVEL_TURBO_NO_CPR,
};

static struct rpmpd *qcm2290_rpmpds[] = {
	[QCM2290_VDDCX] =	&cx_rwcx0_lvl,
	[QCM2290_VDDCX_AO] =	&cx_rwcx0_lvl_ao,
	[QCM2290_VDDCX_VFL] =	&cx_rwcx0_vfl,
	[QCM2290_VDDMX] =	&mx_rwmx0_lvl,
	[QCM2290_VDDMX_AO] =	&mx_rwmx0_lvl_ao,
	[QCM2290_VDDMX_VFL] =	&mx_rwmx0_vfl,
	[QCM2290_VDD_LPI_CX] =	&lpi_cx_rwlc0_lvl,
	[QCM2290_VDD_LPI_MX] =	&lpi_mx_rwlm0_lvl,
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
	{ .compatible = "qcom,msm8917-rpmpd", .data = &msm8917_desc },
	{ .compatible = "qcom,msm8939-rpmpd", .data = &msm8939_desc },
	{ .compatible = "qcom,msm8953-rpmpd", .data = &msm8953_desc },
	{ .compatible = "qcom,msm8974-rpmpd", .data = &msm8974_desc },
	{ .compatible = "qcom,msm8974pro-pma8084-rpmpd", .data = &msm8974pro_pma8084_desc },
	{ .compatible = "qcom,msm8976-rpmpd", .data = &msm8976_desc },
	{ .compatible = "qcom,msm8994-rpmpd", .data = &msm8994_desc },
	{ .compatible = "qcom,msm8996-rpmpd", .data = &msm8996_desc },
	{ .compatible = "qcom,msm8998-rpmpd", .data = &msm8998_desc },
	{ .compatible = "qcom,qcm2290-rpmpd", .data = &qcm2290_desc },
	{ .compatible = "qcom,qcs404-rpmpd", .data = &qcs404_desc },
	{ .compatible = "qcom,qm215-rpmpd", .data = &qm215_desc },
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

	return qcom_rpm_smd_write(rpmpd_smd_rpm, QCOM_SMD_RPM_ACTIVE_STATE,
				  pd->res_type, pd->res_id, &req, sizeof(req));
}

static int rpmpd_send_corner(struct rpmpd *pd, int state, unsigned int corner)
{
	struct rpmpd_req req = {
		.key = pd->key,
		.nbytes = cpu_to_le32(sizeof(u32)),
		.value = cpu_to_le32(corner),
	};

	return qcom_rpm_smd_write(rpmpd_smd_rpm, state, pd->res_type, pd->res_id,
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

	/* Clamp to the highest corner/level if sync_state isn't done yet */
	if (!pd->state_synced)
		this_active_corner = this_sleep_corner = pd->max_state - 1;
	else
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

	guard(mutex)(&rpmpd_lock);

	ret = rpmpd_send_enable(pd, true);
	if (ret)
		return ret;

	pd->enabled = true;

	if (pd->corner)
		ret = rpmpd_aggregate_corner(pd);

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
	struct rpmpd *pd = domain_to_rpmpd(domain);

	if (state > pd->max_state)
		state = pd->max_state;

	guard(mutex)(&rpmpd_lock);

	pd->corner = state;

	/* Always send updates for vfc and vfl */
	if (!pd->enabled && pd->key != cpu_to_le32(KEY_FLOOR_CORNER) &&
	    pd->key != cpu_to_le32(KEY_FLOOR_LEVEL))
		return 0;

	return rpmpd_aggregate_corner(pd);
}

static int rpmpd_probe(struct platform_device *pdev)
{
	int i;
	size_t num;
	struct genpd_onecell_data *data;
	struct rpmpd **rpmpds;
	const struct rpmpd_desc *desc;

	rpmpd_smd_rpm = dev_get_drvdata(pdev->dev.parent);
	if (!rpmpd_smd_rpm) {
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

		rpmpds[i]->max_state = desc->max_state;
		rpmpds[i]->pd.power_off = rpmpd_power_off;
		rpmpds[i]->pd.power_on = rpmpd_power_on;
		rpmpds[i]->pd.set_performance_state = rpmpd_set_performance;
		rpmpds[i]->pd.flags = GENPD_FLAG_ACTIVE_WAKEUP;
		pm_genpd_init(&rpmpds[i]->pd, NULL, true);

		data->domains[i] = &rpmpds[i]->pd;
	}

	/* Add subdomains */
	for (i = 0; i < num; i++) {
		if (!rpmpds[i])
			continue;

		if (rpmpds[i]->parent)
			pm_genpd_add_subdomain(rpmpds[i]->parent, &rpmpds[i]->pd);
	}

	return of_genpd_add_provider_onecell(pdev->dev.of_node, data);
}

static void rpmpd_sync_state(struct device *dev)
{
	const struct rpmpd_desc *desc = of_device_get_match_data(dev);
	struct rpmpd **rpmpds = desc->rpmpds;
	struct rpmpd *pd;
	unsigned int i;
	int ret;

	of_genpd_sync_state(dev->of_node);

	mutex_lock(&rpmpd_lock);
	for (i = 0; i < desc->num_pds; i++) {
		pd = rpmpds[i];
		if (!pd)
			continue;

		pd->state_synced = true;

		if (!pd->enabled)
			pd->corner = 0;

		ret = rpmpd_aggregate_corner(pd);
		if (ret)
			dev_err(dev, "failed to sync %s: %d\n", pd->pd.name, ret);
	}
	mutex_unlock(&rpmpd_lock);
}

static struct platform_driver rpmpd_driver = {
	.driver = {
		.name = "qcom-rpmpd",
		.of_match_table = rpmpd_match_table,
		.suppress_bind_attrs = true,
		.sync_state = rpmpd_sync_state,
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
