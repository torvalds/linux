// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2013--2024 Intel Corporation
 */

#include <linux/bitfield.h>
#include <linux/bits.h>
#include <linux/device.h>
#include <linux/io.h>

#include "ipu6-bus.h"
#include "ipu6-isys.h"
#include "ipu6-isys-csi2.h"
#include "ipu6-platform-isys-csi2-reg.h"

/* only use BB0, BB2, BB4, and BB6 on PHY0 */
#define IPU6SE_ISYS_PHY_BB_NUM		4
#define IPU6SE_ISYS_PHY_0_BASE		0x10000

#define PHY_CPHY_DLL_OVRD(x)		(0x100 + 0x100 * (x))
#define PHY_CPHY_RX_CONTROL1(x)		(0x110 + 0x100 * (x))
#define PHY_DPHY_CFG(x)			(0x148 + 0x100 * (x))
#define PHY_BB_AFE_CONFIG(x)		(0x174 + 0x100 * (x))

/*
 * use port_cfg to configure that which data lanes used
 * +---------+     +------+ +-----+
 * | port0 x4<-----|      | |     |
 * |         |     | port | |     |
 * | port1 x2<-----|      | |     |
 * |         |     |      <-| PHY |
 * | port2 x4<-----|      | |     |
 * |         |     |config| |     |
 * | port3 x2<-----|      | |     |
 * +---------+     +------+ +-----+
 */
static const unsigned int csi2_port_cfg[][3] = {
	{0, 0, 0x1f}, /* no link */
	{4, 0, 0x10}, /* x4 + x4 config */
	{2, 0, 0x12}, /* x2 + x2 config */
	{1, 0, 0x13}, /* x1 + x1 config */
	{2, 1, 0x15}, /* x2x1 + x2x1 config */
	{1, 1, 0x16}, /* x1x1 + x1x1 config */
	{2, 2, 0x18}, /* x2x2 + x2x2 config */
	{1, 2, 0x19} /* x1x2 + x1x2 config */
};

/* port, nlanes, bbindex, portcfg */
static const unsigned int phy_port_cfg[][4] = {
	/* sip0 */
	{0, 1, 0, 0x15},
	{0, 2, 0, 0x15},
	{0, 4, 0, 0x15},
	{0, 4, 2, 0x22},
	/* sip1 */
	{2, 1, 4, 0x15},
	{2, 2, 4, 0x15},
	{2, 4, 4, 0x15},
	{2, 4, 6, 0x22}
};

static void ipu6_isys_csi2_phy_config_by_port(struct ipu6_isys *isys,
					      unsigned int port,
					      unsigned int nlanes)
{
	struct device *dev = &isys->adev->auxdev.dev;
	void __iomem *base = isys->adev->isp->base;
	unsigned int bbnum;
	u32 val, reg, i;

	dev_dbg(dev, "port %u with %u lanes", port, nlanes);

	/* only support <1.5Gbps */
	for (i = 0; i < IPU6SE_ISYS_PHY_BB_NUM; i++) {
		/* cphy_dll_ovrd.crcdc_fsm_dlane0 = 13 */
		reg = IPU6SE_ISYS_PHY_0_BASE + PHY_CPHY_DLL_OVRD(i);
		val = readl(base + reg);
		val |= FIELD_PREP(GENMASK(6, 1), 13);
		writel(val, base + reg);

		/* cphy_rx_control1.en_crc1 = 1 */
		reg = IPU6SE_ISYS_PHY_0_BASE + PHY_CPHY_RX_CONTROL1(i);
		val = readl(base + reg);
		val |= BIT(31);
		writel(val, base + reg);

		/* dphy_cfg.reserved = 1, .lden_from_dll_ovrd_0 = 1 */
		reg = IPU6SE_ISYS_PHY_0_BASE + PHY_DPHY_CFG(i);
		val = readl(base + reg);
		val |= BIT(25) | BIT(26);
		writel(val, base + reg);

		/* cphy_dll_ovrd.lden_crcdc_fsm_dlane0 = 1 */
		reg = IPU6SE_ISYS_PHY_0_BASE + PHY_CPHY_DLL_OVRD(i);
		val = readl(base + reg);
		val |= BIT(0);
		writel(val, base + reg);
	}

	/* Front end config, use minimal channel loss */
	for (i = 0; i < ARRAY_SIZE(phy_port_cfg); i++) {
		if (phy_port_cfg[i][0] == port &&
		    phy_port_cfg[i][1] == nlanes) {
			bbnum = phy_port_cfg[i][2] / 2;
			reg = IPU6SE_ISYS_PHY_0_BASE + PHY_BB_AFE_CONFIG(bbnum);
			val = readl(base + reg);
			val |= phy_port_cfg[i][3];
			writel(val, base + reg);
		}
	}
}

static void ipu6_isys_csi2_rx_control(struct ipu6_isys *isys)
{
	void __iomem *base = isys->adev->isp->base;
	u32 val, reg;

	reg = CSI2_HUB_GPREG_SIP0_CSI_RX_A_CONTROL;
	val = readl(base + reg);
	val |= BIT(0);
	writel(val, base + CSI2_HUB_GPREG_SIP0_CSI_RX_A_CONTROL);

	reg = CSI2_HUB_GPREG_SIP0_CSI_RX_B_CONTROL;
	val = readl(base + reg);
	val |= BIT(0);
	writel(val, base + CSI2_HUB_GPREG_SIP0_CSI_RX_B_CONTROL);

	reg = CSI2_HUB_GPREG_SIP1_CSI_RX_A_CONTROL;
	val = readl(base + reg);
	val |= BIT(0);
	writel(val, base + CSI2_HUB_GPREG_SIP1_CSI_RX_A_CONTROL);

	reg = CSI2_HUB_GPREG_SIP1_CSI_RX_B_CONTROL;
	val = readl(base + reg);
	val |= BIT(0);
	writel(val, base + CSI2_HUB_GPREG_SIP1_CSI_RX_B_CONTROL);
}

static int ipu6_isys_csi2_set_port_cfg(struct ipu6_isys *isys,
				       unsigned int port, unsigned int nlanes)
{
	struct device *dev = &isys->adev->auxdev.dev;
	unsigned int sip = port / 2;
	unsigned int index;

	switch (nlanes) {
	case 1:
		index = 5;
		break;
	case 2:
		index = 6;
		break;
	case 4:
		index = 1;
		break;
	default:
		dev_err(dev, "lanes nr %u is unsupported\n", nlanes);
		return -EINVAL;
	}

	dev_dbg(dev, "port config for port %u with %u lanes\n",	port, nlanes);

	writel(csi2_port_cfg[index][2],
	       isys->pdata->base + CSI2_HUB_GPREG_SIP_FB_PORT_CFG(sip));

	return 0;
}

static void
ipu6_isys_csi2_set_timing(struct ipu6_isys *isys,
			  const struct ipu6_isys_csi2_timing *timing,
			  unsigned int port, unsigned int nlanes)
{
	struct device *dev = &isys->adev->auxdev.dev;
	void __iomem *reg;
	u32 port_base;
	u32 i;

	port_base = (port % 2) ? CSI2_SIP_TOP_CSI_RX_PORT_BASE_1(port) :
		CSI2_SIP_TOP_CSI_RX_PORT_BASE_0(port);

	dev_dbg(dev, "set timing for port %u with %u lanes\n", port, nlanes);

	reg = isys->pdata->base + port_base;
	reg += CSI2_SIP_TOP_CSI_RX_DLY_CNT_TERMEN_CLANE;

	writel(timing->ctermen, reg);

	reg = isys->pdata->base + port_base;
	reg += CSI2_SIP_TOP_CSI_RX_DLY_CNT_SETTLE_CLANE;
	writel(timing->csettle, reg);

	for (i = 0; i < nlanes; i++) {
		reg = isys->pdata->base + port_base;
		reg += CSI2_SIP_TOP_CSI_RX_DLY_CNT_TERMEN_DLANE(i);
		writel(timing->dtermen, reg);

		reg = isys->pdata->base + port_base;
		reg += CSI2_SIP_TOP_CSI_RX_DLY_CNT_SETTLE_DLANE(i);
		writel(timing->dsettle, reg);
	}
}

#define DPHY_TIMER_INCR	0x28
int ipu6_isys_jsl_phy_set_power(struct ipu6_isys *isys,
				struct ipu6_isys_csi2_config *cfg,
				const struct ipu6_isys_csi2_timing *timing,
				bool on)
{
	struct device *dev = &isys->adev->auxdev.dev;
	void __iomem *isys_base = isys->pdata->base;
	int ret = 0;
	u32 nlanes;
	u32 port;

	if (!on)
		return 0;

	port = cfg->port;
	nlanes = cfg->nlanes;

	if (!isys_base || port >= isys->pdata->ipdata->csi2.nports) {
		dev_warn(dev, "invalid port ID %d\n", port);
		return -EINVAL;
	}

	ipu6_isys_csi2_phy_config_by_port(isys, port, nlanes);

	writel(DPHY_TIMER_INCR,
	       isys->pdata->base + CSI2_HUB_GPREG_DPHY_TIMER_INCR);

	/* set port cfg and rx timing */
	ipu6_isys_csi2_set_timing(isys, timing, port, nlanes);

	ret = ipu6_isys_csi2_set_port_cfg(isys, port, nlanes);
	if (ret)
		return ret;

	ipu6_isys_csi2_rx_control(isys);

	return 0;
}
