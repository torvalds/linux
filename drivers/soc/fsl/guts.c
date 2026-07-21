// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Freescale QorIQ Platforms GUTS Driver
 *
 * Copyright (C) 2016 Freescale Semiconductor, Inc.
 */

#include <linux/bitfield.h>
#include <linux/bitops.h>
#include <linux/io.h>
#include <linux/iopoll.h>
#include <linux/slab.h>
#include <linux/module.h>
#include <linux/of_fdt.h>
#include <linux/sys_soc.h>
#include <linux/of_address.h>
#include <linux/platform_device.h>
#include <linux/fsl/guts.h>

#define DCFG_CCSR	0
#define DCFG_DCSR	1

#define RCW_TIMEOUT_US	1

#define LS1046A_RCWSR5_SRDS_PRTCL_S1(lane)	\
	GENMASK(19 + 4 * (lane), 16 + 4 * (lane))
#define LS1046A_SRDS_PRTCL_XFI				1
#define LS1046A_SRDS_PRTCL_100BASEX_SGMII		3

#define LS1088A_RCWSR29_SRDS_PRTCL_S1_LNn(lane)	\
	GENMASK(19 + 4 * (3 - (lane)), 16 + 4 * (3 - (lane)))
#define LS1088A_RCWSR30_SRDS_PRTCL_S2_LNn(lane)	\
	GENMASK(3 + 4 * (3 - (lane)), 4 * (3 - (lane)))
#define LS1088A_SRDS_PRTCL_XFI				1
#define LS1088A_SRDS_PRTCL_100BASEX_SGMII		3

#define LS2088A_RCWSR29_SRDS_PRTCL_S1			GENMASK(23, 16)
#define LS2088A_RCWSR30_SRDS_CLK_EN_SEL_XGMII_S1	BIT(14)
#define LS2088A_RCWSR30_SRDS_CLK_SEL_XGMII_Ln_S1(lane)	BIT(6 + (7 - (lane)))
#define LS2088A_RCWSR30_SRDS_CLK_SEL_MSK		GENMASK(13, 6)
#define LS2088A_SRDS_CLK_SEL_XGMII			1
#define LS2088A_SRDS_CLK_SEL_GMII			0

struct fsl_soc_die_attr {
	char	*die;
	u32	svr;
	u32	mask;
};

struct fsl_soc_serdes_rcw_override {
	int offset;
	int mask;
	int val;
};

struct fsl_soc_data {
	const char *sfp_compat;
	u32 uid_offset;
	int num_serdes_blocks;
	int num_serdes_lanes;
	int (*serdes_init_rcwcr)(int index);
	int (*serdes_get_rcw_override)(int index, int lane,
				       enum lynx_lane_mode lane_mode,
				       struct fsl_soc_serdes_rcw_override *override);
};

enum qoriq_die {
	DIE_T4240,
	DIE_T1040,
	DIE_T2080,
	DIE_T1024,
	DIE_LS1043A,
	DIE_LS2080A,
	DIE_LS1088A,
	DIE_LS1012A,
	DIE_LS1046A,
	DIE_LS2088A,
	DIE_LS1021A,
	DIE_LX2160A,
	DIE_LS1028A,
	DIE_MAX,
};

struct rcw_full_srds_proto {
	int srds_proto_val;
	unsigned long xgmii_lanes;
};

static const struct rcw_full_srds_proto ls2088a_rcw_full_srds1_protos[] = {
	{ 0x05, 0 },
	{ 0x07, 0 },
	{ 0x09, 0 },
	{ 0x0a, 0 },
	{ 0x0c, 0 },
	{ 0x0e, 0 },
	{ 0x10, 0 },
	{ 0x12, 0 },
	{ 0x14, 0 },
	{ 0x16, 0 },
	{ 0x18, 0 },
	{ 0x1a, 0 },
	{ 0x1c, 0 },
	{ 0x1e, 0 },
	{ 0x20, 0 },
	{ 0x22, 0 },
	{ 0x24, 0 },
	{ 0x26, GENMASK(7, 6) },
	{ 0x28, GENMASK(7, 4) },
	{ 0x2a, GENMASK(7, 0) },
	/* 0x2b: unimplemented because of XAUI1 (lanes E-H) */
	/* 0x2d: unimplemented because of XAUI1 (lanes E-H) */
	/* 0x2e: unimplemented because of XAUI1 (lanes E-H) */
	/* 0x30: unimplemented because of XAUI1 (lanes E-H) */
	/* 0x32: unimplemented because of XAUI1 (lanes E-H) and XAUI2 (A-D) */
	/* 0x33: unimplemented because of QSGMII (lanes E-H) */
	/* 0x35: unimplemented because of QSGMII (lanes A-C) */
	/* 0x37: unimplemented because of QSGMII (lanes E-F) */
	{ 0x39, 0 },
	{ 0x3b, GENMASK(6, 4) | GENMASK(2, 0) },
	{ 0x4b, GENMASK(7, 4) },
	{ 0x4c, GENMASK(3, 0) },
	{ 0x4d, 0 },
	{ 0x58, 0 },
};

/* SoC die attribute definition for QorIQ platform */
static const struct fsl_soc_die_attr fsl_soc_die[] = {
	/*
	 * Power Architecture-based SoCs T Series
	 */

	/* Die: T4240, SoC: T4240/T4160/T4080 */
	[DIE_T4240] =
	{ .die		= "T4240",
	  .svr		= 0x82400000,
	  .mask		= 0xfff00000,
	},
	/* Die: T1040, SoC: T1040/T1020/T1042/T1022 */
	[DIE_T1040] =
	{ .die		= "T1040",
	  .svr		= 0x85200000,
	  .mask		= 0xfff00000,
	},
	/* Die: T2080, SoC: T2080/T2081 */
	[DIE_T2080] =
	{ .die		= "T2080",
	  .svr		= 0x85300000,
	  .mask		= 0xfff00000,
	},
	/* Die: T1024, SoC: T1024/T1014/T1023/T1013 */
	[DIE_T1024] =
	{ .die		= "T1024",
	  .svr		= 0x85400000,
	  .mask		= 0xfff00000,
	},

	/*
	 * ARM-based SoCs LS Series
	 */

	/* Die: LS1043A, SoC: LS1043A/LS1023A */
	[DIE_LS1043A] =
	{ .die		= "LS1043A",
	  .svr		= 0x87920000,
	  .mask		= 0xffff0000,
	},
	/* Die: LS2080A, SoC: LS2080A/LS2040A/LS2085A */
	[DIE_LS2080A] =
	{ .die		= "LS2080A",
	  .svr		= 0x87010000,
	  .mask		= 0xff3f0000,
	},
	/* Die: LS1088A, SoC: LS1088A/LS1048A/LS1084A/LS1044A */
	[DIE_LS1088A] =
	{ .die		= "LS1088A",
	  .svr		= 0x87030000,
	  .mask		= 0xff3f0000,
	},
	/* Die: LS1012A, SoC: LS1012A */
	[DIE_LS1012A] =
	{ .die		= "LS1012A",
	  .svr		= 0x87040000,
	  .mask		= 0xffff0000,
	},
	/* Die: LS1046A, SoC: LS1046A/LS1026A */
	[DIE_LS1046A] =
	{ .die		= "LS1046A",
	  .svr		= 0x87070000,
	  .mask		= 0xffff0000,
	},
	/* Die: LS2088A, SoC: LS2088A/LS2048A/LS2084A/LS2044A */
	[DIE_LS2088A] =
	{ .die		= "LS2088A",
	  .svr		= 0x87090000,
	  .mask		= 0xff3f0000,
	},
	/* Die: LS1021A, SoC: LS1021A/LS1020A/LS1022A */
	[DIE_LS1021A] =
	{ .die		= "LS1021A",
	  .svr		= 0x87000000,
	  .mask		= 0xfff70000,
	},
	/* Die: LX2160A, SoC: LX2160A/LX2120A/LX2080A */
	[DIE_LX2160A] =
	{ .die          = "LX2160A",
	  .svr          = 0x87360000,
	  .mask         = 0xff3f0000,
	},
	/* Die: LS1028A, SoC: LS1028A */
	[DIE_LS1028A] =
	{ .die          = "LS1028A",
	  .svr          = 0x870b0000,
	  .mask         = 0xff3f0000,
	},
	{ },
};

static struct fsl_soc_guts {
	struct ccsr_guts __iomem *dcfg_ccsr;
	struct ccsr_guts __iomem *dcfg_dcsr;
	const struct fsl_soc_data *data;
	bool little_endian;
	u32 svr;
	spinlock_t rcwcr_lock; /* serializes concurrent writes to the RCWCR */
} soc;

static unsigned int fsl_guts_read(const void __iomem *reg)
{
	if (soc.little_endian)
		return ioread32(reg);

	return ioread32be(reg);
}

static void fsl_guts_write(void __iomem *reg, u32 val)
{
	if (soc.little_endian)
		iowrite32(val, reg);
	else
		iowrite32be(val, reg);
}

/* Some fields of the Reset Configuration Word (RCW) can be overridden at
 * runtime by writing to the RCWCRn registers contained within the DCSR space
 * of the Device Configuration (DCFG) block. The layout of the RCWCRn registers
 * is identical with the read-only RCWSRn from the CCSR space.
 */
static int fsl_guts_rcw_rmw(int offset, u32 val, u32 mask)
{
	u32 rcwcr, rcwsr = fsl_guts_read(&soc.dcfg_ccsr->rcwsr[offset]);

	rcwcr = rcwsr & ~mask;
	rcwcr |= val;
	fsl_guts_write(&soc.dcfg_dcsr->rcwcr[offset], rcwcr);

	pr_debug("RCW override: RCWSR%d 0x%x -> RCWCR%d 0x%x\n",
		 offset + 1, rcwsr, offset + 1, rcwcr);

	/* Updates to RCWCR should be visible back in RCWSR immediately */
	return read_poll_timeout_atomic(fsl_guts_read, rcwsr, rcwsr == rcwcr,
					0, RCW_TIMEOUT_US, false,
					&soc.dcfg_ccsr->rcwsr[offset]);
}

static bool fsl_soc_die_match_one(u32 svr, const struct fsl_soc_die_attr *match)
{
	return match->svr == (svr & match->mask);
}

static const struct fsl_soc_die_attr *fsl_soc_die_match(
	u32 svr, const struct fsl_soc_die_attr *matches)
{
	while (matches->svr) {
		if (fsl_soc_die_match_one(svr, matches))
			return matches;
		matches++;
	}
	return NULL;
}

static int
fsl_guts_serdes_get_rcw_override(int serdes_idx, int lane,
				 enum lynx_lane_mode lane_mode,
				 struct fsl_soc_serdes_rcw_override *override)
{
	const struct fsl_soc_data *soc_data = soc.data;

	if (!soc_data)
		return -ENODEV;

	/* serdes_idx is one-based */
	if (serdes_idx > soc_data->num_serdes_blocks || serdes_idx <= 0)
		return -ERANGE;

	if (lane >= soc_data->num_serdes_lanes || lane < 0)
		return -ERANGE;

	if (!soc_data->serdes_get_rcw_override) {
		pr_debug("RCW override not implemented for SoC\n");
		return -EINVAL;
	}

	if (!soc.dcfg_dcsr) {
		pr_debug("Device tree does not define DCFG_DCSR region necessary for RCW override\n");
		return -EINVAL;
	}

	return soc_data->serdes_get_rcw_override(serdes_idx, lane, lane_mode,
						 override);
}

/**
 * fsl_guts_lane_validate() - Validate that SerDes protocol is implemented and
 *	supported on current SoC
 * @serdes_idx: one-based SerDes block index
 * @lane: zero-based lane index within SerDes
 * @lane_mode: requested SerDes protocol
 *
 * Should be called before actually requesting the RCW override procedure to be
 * applied using %fsl_guts_lane_set_mode()
 *
 * Return: 0 if RCW override to protocol is possible, negative error otherwise
 */
int fsl_guts_lane_validate(int serdes_idx, int lane, enum lynx_lane_mode lane_mode)
{
	struct fsl_soc_serdes_rcw_override override;

	return fsl_guts_serdes_get_rcw_override(serdes_idx, lane, lane_mode,
						&override);
}
EXPORT_SYMBOL_NS_GPL(fsl_guts_lane_validate, "FSL_GUTS");

/**
 * fsl_guts_lane_set_mode() - apply RCW override procedure for SerDes lane
 * @serdes_idx: one-based SerDes block index
 * @lane: zero-based lane index within SerDes
 * @lane_mode: requested SerDes protocol
 *
 * Return: 0 on success, negative error otherwise
 */
int fsl_guts_lane_set_mode(int serdes_idx, int lane, enum lynx_lane_mode lane_mode)
{
	struct fsl_soc_serdes_rcw_override override;
	int err;

	err = fsl_guts_serdes_get_rcw_override(serdes_idx, lane, lane_mode,
					       &override);
	if (err)
		return err;

	spin_lock(&soc.rcwcr_lock);

	if (soc.data->serdes_init_rcwcr) {
		err = soc.data->serdes_init_rcwcr(serdes_idx);
		if (err)
			goto out_unlock;
	}

	err = fsl_guts_rcw_rmw(override.offset,
			       override.val << __ffs(override.mask),
			       override.mask);
	if (err)
		pr_err("RCW override failed: %pe\n", ERR_PTR(err));

out_unlock:
	spin_unlock(&soc.rcwcr_lock);

	return err;
}
EXPORT_SYMBOL_NS_GPL(fsl_guts_lane_set_mode, "FSL_GUTS");

static u64 fsl_guts_get_soc_uid(const char *compat, unsigned int offset)
{
	struct device_node *np;
	void __iomem *sfp_base;
	u64 uid;

	np = of_find_compatible_node(NULL, NULL, compat);
	if (!np)
		return 0;

	sfp_base = of_iomap(np, 0);
	if (!sfp_base) {
		of_node_put(np);
		return 0;
	}

	uid = ioread32(sfp_base + offset);
	uid <<= 32;
	uid |= ioread32(sfp_base + offset + 4);

	iounmap(sfp_base);
	of_node_put(np);

	return uid;
}

static int ls1046a_serdes_get_rcw_override(int index, int lane,
					   enum lynx_lane_mode lane_mode,
					   struct fsl_soc_serdes_rcw_override *override)
{
	/* The RCW override procedure has to write to different registers
	 * depending on the SerDes block index.
	 */
	switch (index) {
	case 1:
		override->offset = 4;
		override->mask = LS1046A_RCWSR5_SRDS_PRTCL_S1(lane);
		break;
	default:
		return -EINVAL;
	}

	if (lynx_lane_mode_uses_xgmii_mac(lane_mode))
		override->val = LS1046A_SRDS_PRTCL_XFI;
	else if (lynx_lane_mode_uses_gmii_mac(lane_mode))
		override->val = LS1046A_SRDS_PRTCL_100BASEX_SGMII;
	else
		return -EINVAL;

	return 0;
}

static int ls1088a_serdes_get_rcw_override(int index, int lane,
					   enum lynx_lane_mode lane_mode,
					   struct fsl_soc_serdes_rcw_override *override)
{
	/* The RCW override procedure has to write to different registers
	 * depending on the SerDes block index.
	 */
	switch (index) {
	case 1:
		override->offset = 28;
		override->mask = LS1088A_RCWSR29_SRDS_PRTCL_S1_LNn(lane);
		break;
	case 2:
		override->offset = 29;
		override->mask = LS1088A_RCWSR30_SRDS_PRTCL_S2_LNn(lane);
		break;
	default:
		return -EINVAL;
	}

	if (lynx_lane_mode_uses_xgmii_mac(lane_mode))
		override->val = LS1088A_SRDS_PRTCL_XFI;
	else if (lynx_lane_mode_uses_gmii_mac(lane_mode))
		override->val = LS1088A_SRDS_PRTCL_100BASEX_SGMII;
	else
		return -EINVAL;

	return 0;
}

static const struct rcw_full_srds_proto *ls2088a_get_full_serdes1_proto(void)
{
	u32 rcwsr29 = fsl_guts_read(&soc.dcfg_ccsr->rcwsr[28]);
	u32 srds_prtcl_s1 = FIELD_GET(LS2088A_RCWSR29_SRDS_PRTCL_S1, rcwsr29);

	for (int i = 0; i < ARRAY_SIZE(ls2088a_rcw_full_srds1_protos); i++) {
		const struct rcw_full_srds_proto *proto;

		proto = &ls2088a_rcw_full_srds1_protos[i];
		if (proto->srds_proto_val == srds_prtcl_s1)
			return proto;
	}

	return NULL;
}

static int ls2088a_serdes_get_rcw_override(int index, int lane,
					   enum lynx_lane_mode lane_mode,
					   struct fsl_soc_serdes_rcw_override *override)
{
	switch (index) {
	case 1:
		override->offset = 29;
		override->mask = LS2088A_RCWSR30_SRDS_CLK_SEL_XGMII_Ln_S1(lane);
		break;
	default:
		return -EINVAL;
	}

	/* RCW override only supported if we know how to handle the initial
	 * RCWSR29[SRDS_PRTCL_S1] value and turn it into an override.
	 */
	if (!ls2088a_get_full_serdes1_proto()) {
		u32 rcwsr30 = fsl_guts_read(&soc.dcfg_ccsr->rcwsr[29]);

		/* If a SerDes-level override is already in place (probably
		 * left there by a previous boot stage), use it.
		 */
		if (!(rcwsr30 & LS2088A_RCWSR30_SRDS_CLK_EN_SEL_XGMII_S1))
			return -EINVAL;
	}

	if (lynx_lane_mode_uses_xgmii_mac(lane_mode))
		override->val = LS2088A_SRDS_CLK_SEL_XGMII;
	else if (lynx_lane_mode_uses_gmii_mac(lane_mode))
		override->val = LS2088A_SRDS_CLK_SEL_GMII;
	else
		return -EINVAL;

	return 0;
}

static int ls2088a_serdes_init_rcwcr(int serdes_idx)
{
	const struct rcw_full_srds_proto *srds_prtcl_s1;
	const struct fsl_soc_data *soc_data = soc.data;
	u32 rcwsr30;
	int i, err;

	/* SerDes 2 supports only SGMII for networking. There should be
	 * no need for RCW override
	 */
	if (serdes_idx != 1)
		return -EINVAL;

	/* SRDS_CLK_EN_SEL_XGMII_S1: SerDes Clock Enable Select XGMII Serdes 1:
	 * Enables to select GMII/XGMII clock according to
	 * SRDS_CLK_SEL_XGMII_Ln_S1.
	 * If the GMII/XGMII select override has already been set, use it.
	 * Otherwise, derive an initial override for all lanes based on the
	 * full SerDes protocol table.
	 */
	rcwsr30 = fsl_guts_read(&soc.dcfg_ccsr->rcwsr[29]);
	if (rcwsr30 & LS2088A_RCWSR30_SRDS_CLK_EN_SEL_XGMII_S1) {
		pr_debug("RCWSR30 = 0x%x, using this.\n", rcwsr30);
		return 0;
	}

	srds_prtcl_s1 = ls2088a_get_full_serdes1_proto();

	rcwsr30 = LS2088A_RCWSR30_SRDS_CLK_EN_SEL_XGMII_S1;

	/* We need to configure the initial state of all lanes for
	 * the SerDes block #1
	 */
	for_each_set_bit(i, &srds_prtcl_s1->xgmii_lanes, soc_data->num_serdes_lanes)
		rcwsr30 |= LS2088A_RCWSR30_SRDS_CLK_SEL_XGMII_Ln_S1(i);

	pr_debug("Setting initial RCWSR30 = 0x%x based on SRDS_PRTCL_S1 = 0x%x\n",
		 rcwsr30, srds_prtcl_s1->srds_proto_val);

	err = fsl_guts_rcw_rmw(29, rcwsr30,
			       LS2088A_RCWSR30_SRDS_CLK_EN_SEL_XGMII_S1 |
			       LS2088A_RCWSR30_SRDS_CLK_SEL_MSK);
	if (err) {
		pr_err("Setting up initial RCWCR failed: %pe\n", ERR_PTR(err));
		return err;
	}

	return 0;
}

static const struct fsl_soc_data ls1088a_data = {
	.serdes_get_rcw_override = ls1088a_serdes_get_rcw_override,
	.num_serdes_blocks = 2,
	.num_serdes_lanes = 4,
};

static const struct fsl_soc_data ls1046a_data = {
	.serdes_get_rcw_override = ls1046a_serdes_get_rcw_override,
	.num_serdes_blocks = 2,
	.num_serdes_lanes = 4,
};

static const struct fsl_soc_data ls2088a_data = {
	.serdes_get_rcw_override = ls2088a_serdes_get_rcw_override,
	.serdes_init_rcwcr = ls2088a_serdes_init_rcwcr,
	.num_serdes_blocks = 2,
	.num_serdes_lanes = 8,
};

static const struct fsl_soc_data ls1028a_data = {
	.sfp_compat = "fsl,ls1028a-sfp",
	.uid_offset = 0x21c,
	.num_serdes_blocks = 1,
	.num_serdes_lanes = 4,
};

/*
 * Table for matching compatible strings, for device tree
 * guts node, for Freescale QorIQ SOCs.
 */
static const struct of_device_id fsl_guts_of_match[] = {
	{ .compatible = "fsl,qoriq-device-config-1.0", },
	{ .compatible = "fsl,qoriq-device-config-2.0", },
	{ .compatible = "fsl,p1010-guts", },
	{ .compatible = "fsl,p1020-guts", },
	{ .compatible = "fsl,p1021-guts", },
	{ .compatible = "fsl,p1022-guts", },
	{ .compatible = "fsl,p1023-guts", },
	{ .compatible = "fsl,p2020-guts", },
	{ .compatible = "fsl,bsc9131-guts", },
	{ .compatible = "fsl,bsc9132-guts", },
	{ .compatible = "fsl,mpc8536-guts", },
	{ .compatible = "fsl,mpc8544-guts", },
	{ .compatible = "fsl,mpc8548-guts", },
	{ .compatible = "fsl,mpc8568-guts", },
	{ .compatible = "fsl,mpc8569-guts", },
	{ .compatible = "fsl,mpc8572-guts", },
	{ .compatible = "fsl,ls1021a-dcfg", },
	{ .compatible = "fsl,ls1043a-dcfg", },
	{ .compatible = "fsl,ls2080a-dcfg", .data = &ls2088a_data},
	{ .compatible = "fsl,ls1088a-dcfg", .data = &ls1088a_data},
	{ .compatible = "fsl,ls1012a-dcfg", },
	{ .compatible = "fsl,ls1046a-dcfg", .data = &ls1046a_data},
	{ .compatible = "fsl,lx2160a-dcfg", },
	{ .compatible = "fsl,ls1028a-dcfg", .data = &ls1028a_data},
	{}
};

static int __init fsl_guts_init(void)
{
	struct soc_device_attribute *soc_dev_attr = NULL;
	static struct soc_device *soc_dev;
	const struct fsl_soc_die_attr *soc_die;
	const struct of_device_id *match;
	struct device_node *np;
	u64 soc_uid = 0;
	int ret;

	spin_lock_init(&soc.rcwcr_lock);

	np = of_find_matching_node_and_match(NULL, fsl_guts_of_match, &match);
	if (!np)
		return 0;
	soc.data = match->data;

	soc.dcfg_ccsr = of_iomap(np, DCFG_CCSR);
	if (!soc.dcfg_ccsr) {
		of_node_put(np);
		ret = -ENOMEM;
		goto err_clear_soc_data;
	}
	/* DCFG_DCSR is optional */
	soc.dcfg_dcsr = of_iomap(np, DCFG_DCSR);

	soc.little_endian = of_property_read_bool(np, "little-endian");
	soc.svr = fsl_guts_read(&soc.dcfg_ccsr->svr);
	of_node_put(np);

	/* Register soc device */
	soc_dev_attr = kzalloc_obj(*soc_dev_attr);
	if (!soc_dev_attr) {
		ret = -ENOMEM;
		goto err_unmap_dcfg_ccsr_dcsr;
	}

	ret = soc_attr_read_machine(soc_dev_attr);
	if (ret)
		of_machine_read_compatible(&soc_dev_attr->machine, 0);

	soc_die = fsl_soc_die_match(soc.svr, fsl_soc_die);
	if (soc_die) {
		soc_dev_attr->family = kasprintf(GFP_KERNEL, "QorIQ %s",
						 soc_die->die);
	} else {
		soc_dev_attr->family = kasprintf(GFP_KERNEL, "QorIQ");
	}
	if (!soc_dev_attr->family) {
		ret = -ENOMEM;
		goto err_free_soc_dev_attr;
	}

	soc_dev_attr->soc_id = kasprintf(GFP_KERNEL, "svr:0x%08x", soc.svr);
	if (!soc_dev_attr->soc_id) {
		ret = -ENOMEM;
		goto err_free_family;
	}

	soc_dev_attr->revision = kasprintf(GFP_KERNEL, "%d.%d",
					   (soc.svr >>  4) & 0xf, soc.svr & 0xf);
	if (!soc_dev_attr->revision) {
		ret = -ENOMEM;
		goto err_free_soc_id;
	}

	if (soc.data)
		soc_uid = fsl_guts_get_soc_uid(soc.data->sfp_compat,
					       soc.data->uid_offset);
	if (soc_uid)
		soc_dev_attr->serial_number = kasprintf(GFP_KERNEL, "%016llX",
							soc_uid);

	soc_dev = soc_device_register(soc_dev_attr);
	if (IS_ERR(soc_dev)) {
		ret = PTR_ERR(soc_dev);
		goto err_free_serial_number;
	}

	pr_info("Machine: %s\n", soc_dev_attr->machine);
	pr_info("SoC family: %s\n", soc_dev_attr->family);
	pr_info("SoC ID: %s, Revision: %s\n",
		soc_dev_attr->soc_id, soc_dev_attr->revision);

	return 0;

err_free_serial_number:
	kfree(soc_dev_attr->serial_number);
	kfree(soc_dev_attr->revision);
err_free_soc_id:
	kfree(soc_dev_attr->soc_id);
err_free_family:
	kfree(soc_dev_attr->family);
err_free_soc_dev_attr:
	kfree(soc_dev_attr);
err_unmap_dcfg_ccsr_dcsr:
	if (soc.dcfg_dcsr) {
		iounmap(soc.dcfg_dcsr);
		soc.dcfg_dcsr = NULL;
	}
	iounmap(soc.dcfg_ccsr);
	soc.dcfg_ccsr = NULL;
err_clear_soc_data:
	soc.data = NULL;

	return ret;
}
core_initcall(fsl_guts_init);
