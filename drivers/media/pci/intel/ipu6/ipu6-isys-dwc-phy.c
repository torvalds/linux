// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2013--2024 Intel Corporation
 */

#include <linux/bitfield.h>
#include <linux/bits.h>
#include <linux/delay.h>
#include <linux/device.h>
#include <linux/iopoll.h>
#include <linux/math64.h>

#include "ipu6-bus.h"
#include "ipu6-isys.h"
#include "ipu6-platform-isys-csi2-reg.h"

#define IPU6_DWC_DPHY_BASE(i)			(0x238038 + 0x34 * (i))
#define IPU6_DWC_DPHY_RSTZ			0x00
#define IPU6_DWC_DPHY_SHUTDOWNZ			0x04
#define IPU6_DWC_DPHY_HSFREQRANGE		0x08
#define IPU6_DWC_DPHY_CFGCLKFREQRANGE		0x0c
#define IPU6_DWC_DPHY_TEST_IFC_ACCESS_MODE	0x10
#define IPU6_DWC_DPHY_TEST_IFC_REQ		0x14
#define IPU6_DWC_DPHY_TEST_IFC_REQ_COMPLETION	0x18
#define IPU6_DWC_DPHY_DFT_CTRL0			0x28
#define IPU6_DWC_DPHY_DFT_CTRL1			0x2c
#define IPU6_DWC_DPHY_DFT_CTRL2			0x30

/*
 * test IFC request definition:
 * - req: 0 for read, 1 for write
 * - 12 bits address
 * - 8bits data (will ignore for read)
 * --24----16------4-----0
 * --|-data-|-addr-|-req-|
 */
#define IFC_REQ(req, addr, data) (FIELD_PREP(GENMASK(23, 16), data) | \
				  FIELD_PREP(GENMASK(15, 4), addr) | \
				  FIELD_PREP(GENMASK(1, 0), req))

#define TEST_IFC_REQ_READ	0
#define TEST_IFC_REQ_WRITE	1
#define TEST_IFC_REQ_RESET	2

#define TEST_IFC_ACCESS_MODE_FSM	0
#define TEST_IFC_ACCESS_MODE_IFC_CTL	1

enum phy_fsm_state {
	PHY_FSM_STATE_POWERON = 0,
	PHY_FSM_STATE_BGPON = 1,
	PHY_FSM_STATE_CAL_TYPE = 2,
	PHY_FSM_STATE_BURNIN_CAL = 3,
	PHY_FSM_STATE_TERMCAL = 4,
	PHY_FSM_STATE_OFFSETCAL = 5,
	PHY_FSM_STATE_OFFSET_LANE = 6,
	PHY_FSM_STATE_IDLE = 7,
	PHY_FSM_STATE_ULP = 8,
	PHY_FSM_STATE_DDLTUNNING = 9,
	PHY_FSM_STATE_SKEW_BACKWARD = 10,
	PHY_FSM_STATE_INVALID,
};

static void dwc_dphy_write(struct ipu6_isys *isys, u32 phy_id, u32 addr,
			   u32 data)
{
	struct device *dev = &isys->adev->auxdev.dev;
	void __iomem *isys_base = isys->pdata->base;
	void __iomem *base = isys_base + IPU6_DWC_DPHY_BASE(phy_id);

	dev_dbg(dev, "write: reg 0x%lx = data 0x%x", base + addr - isys_base,
		data);
	writel(data, base + addr);
}

static u32 dwc_dphy_read(struct ipu6_isys *isys, u32 phy_id, u32 addr)
{
	struct device *dev = &isys->adev->auxdev.dev;
	void __iomem *isys_base = isys->pdata->base;
	void __iomem *base = isys_base + IPU6_DWC_DPHY_BASE(phy_id);
	u32 data;

	data = readl(base + addr);
	dev_dbg(dev, "read: reg 0x%lx = data 0x%x", base + addr - isys_base,
		data);

	return data;
}

static void dwc_dphy_write_mask(struct ipu6_isys *isys, u32 phy_id, u32 addr,
				u32 data, u8 shift, u8 width)
{
	u32 temp;
	u32 mask;

	mask = (1 << width) - 1;
	temp = dwc_dphy_read(isys, phy_id, addr);
	temp &= ~(mask << shift);
	temp |= (data & mask) << shift;
	dwc_dphy_write(isys, phy_id, addr, temp);
}

static u32 __maybe_unused dwc_dphy_read_mask(struct ipu6_isys *isys, u32 phy_id,
					     u32 addr, u8 shift,  u8 width)
{
	u32 val;

	val = dwc_dphy_read(isys, phy_id, addr) >> shift;
	return val & ((1 << width) - 1);
}

#define DWC_DPHY_TIMEOUT (5 * USEC_PER_SEC)
static int dwc_dphy_ifc_read(struct ipu6_isys *isys, u32 phy_id, u32 addr,
			     u32 *val)
{
	struct device *dev = &isys->adev->auxdev.dev;
	void __iomem *isys_base = isys->pdata->base;
	void __iomem *base = isys_base + IPU6_DWC_DPHY_BASE(phy_id);
	void __iomem *reg;
	u32 completion;
	int ret;

	dwc_dphy_write(isys, phy_id, IPU6_DWC_DPHY_TEST_IFC_REQ,
		       IFC_REQ(TEST_IFC_REQ_READ, addr, 0));
	reg = base + IPU6_DWC_DPHY_TEST_IFC_REQ_COMPLETION;
	ret = readl_poll_timeout(reg, completion, !(completion & BIT(0)),
				 10, DWC_DPHY_TIMEOUT);
	if (ret) {
		dev_err(dev, "DWC ifc request read timeout\n");
		return ret;
	}

	*val = completion >> 8 & 0xff;
	*val = FIELD_GET(GENMASK(15, 8), completion);
	dev_dbg(dev, "DWC ifc read 0x%x = 0x%x", addr, *val);

	return 0;
}

static int dwc_dphy_ifc_write(struct ipu6_isys *isys, u32 phy_id, u32 addr,
			      u32 data)
{
	struct device *dev = &isys->adev->auxdev.dev;
	void __iomem *isys_base = isys->pdata->base;
	void __iomem *base = isys_base + IPU6_DWC_DPHY_BASE(phy_id);
	void __iomem *reg;
	u32 completion;
	int ret;

	dwc_dphy_write(isys, phy_id, IPU6_DWC_DPHY_TEST_IFC_REQ,
		       IFC_REQ(TEST_IFC_REQ_WRITE, addr, data));
	completion = readl(base + IPU6_DWC_DPHY_TEST_IFC_REQ_COMPLETION);
	reg = base + IPU6_DWC_DPHY_TEST_IFC_REQ_COMPLETION;
	ret = readl_poll_timeout(reg, completion, !(completion & BIT(0)),
				 10, DWC_DPHY_TIMEOUT);
	if (ret)
		dev_err(dev, "DWC ifc request write timeout\n");

	return ret;
}

static void dwc_dphy_ifc_write_mask(struct ipu6_isys *isys, u32 phy_id,
				    u32 addr, u32 data, u8 shift, u8 width)
{
	u32 temp, mask;
	int ret;

	ret = dwc_dphy_ifc_read(isys, phy_id, addr, &temp);
	if (ret)
		return;

	mask = (1 << width) - 1;
	temp &= ~(mask << shift);
	temp |= (data & mask) << shift;
	dwc_dphy_ifc_write(isys, phy_id, addr, temp);
}

static u32 dwc_dphy_ifc_read_mask(struct ipu6_isys *isys, u32 phy_id, u32 addr,
				  u8 shift, u8 width)
{
	int ret;
	u32 val;

	ret = dwc_dphy_ifc_read(isys, phy_id, addr, &val);
	if (ret)
		return 0;

	return ((val >> shift) & ((1 << width) - 1));
}

static int dwc_dphy_pwr_up(struct ipu6_isys *isys, u32 phy_id)
{
	struct device *dev = &isys->adev->auxdev.dev;
	u32 fsm_state;
	int ret;

	dwc_dphy_write(isys, phy_id, IPU6_DWC_DPHY_RSTZ, 1);
	usleep_range(10, 20);
	dwc_dphy_write(isys, phy_id, IPU6_DWC_DPHY_SHUTDOWNZ, 1);

	ret = read_poll_timeout(dwc_dphy_ifc_read_mask, fsm_state,
				(fsm_state == PHY_FSM_STATE_IDLE ||
				 fsm_state == PHY_FSM_STATE_ULP),
				100, DWC_DPHY_TIMEOUT, false, isys,
				phy_id, 0x1e, 0, 4);

	if (ret)
		dev_err(dev, "Dphy %d power up failed, state 0x%x", phy_id,
			fsm_state);

	return ret;
}

struct dwc_dphy_freq_range {
	u8 hsfreq;
	u16 min;
	u16 max;
	u16 default_mbps;
	u16 osc_freq_target;
};

#define DPHY_FREQ_RANGE_NUM		(63)
#define DPHY_FREQ_RANGE_INVALID_INDEX	(0xff)
static const struct dwc_dphy_freq_range freqranges[DPHY_FREQ_RANGE_NUM] = {
	{0x00,	80,	97,	80,	335},
	{0x10,	80,	107,	90,	335},
	{0x20,	84,	118,	100,	335},
	{0x30,	93,	128,	110,	335},
	{0x01,	103,	139,	120,	335},
	{0x11,	112,	149,	130,	335},
	{0x21,	122,	160,	140,	335},
	{0x31,	131,	170,	150,	335},
	{0x02,	141,	181,	160,	335},
	{0x12,	150,	191,	170,	335},
	{0x22,	160,	202,	180,	335},
	{0x32,	169,	212,	190,	335},
	{0x03,	183,	228,	205,	335},
	{0x13,	198,	244,	220,	335},
	{0x23,	212,	259,	235,	335},
	{0x33,	226,	275,	250,	335},
	{0x04,	250,	301,	275,	335},
	{0x14,	274,	328,	300,	335},
	{0x25,	297,	354,	325,	335},
	{0x35,	321,	380,	350,	335},
	{0x05,	369,	433,	400,	335},
	{0x16,	416,	485,	450,	335},
	{0x26,	464,	538,	500,	335},
	{0x37,	511,	590,	550,	335},
	{0x07,	559,	643,	600,	335},
	{0x18,	606,	695,	650,	335},
	{0x28,	654,	748,	700,	335},
	{0x39,	701,	800,	750,	335},
	{0x09,	749,	853,	800,	335},
	{0x19,	796,	905,	850,	335},
	{0x29,	844,	958,	900,	335},
	{0x3a,	891,	1010,	950,	335},
	{0x0a,	939,	1063,	1000,	335},
	{0x1a,	986,	1115,	1050,	335},
	{0x2a,	1034,	1168,	1100,	335},
	{0x3b,	1081,	1220,	1150,	335},
	{0x0b,	1129,	1273,	1200,	335},
	{0x1b,	1176,	1325,	1250,	335},
	{0x2b,	1224,	1378,	1300,	335},
	{0x3c,	1271,	1430,	1350,	335},
	{0x0c,	1319,	1483,	1400,	335},
	{0x1c,	1366,	1535,	1450,	335},
	{0x2c,	1414,	1588,	1500,	335},
	{0x3d,	1461,	1640,	1550,	208},
	{0x0d,	1509,	1693,	1600,	214},
	{0x1d,	1556,	1745,	1650,	221},
	{0x2e,	1604,	1798,	1700,	228},
	{0x3e,	1651,	1850,	1750,	234},
	{0x0e,	1699,	1903,	1800,	241},
	{0x1e,	1746,	1955,	1850,	248},
	{0x2f,	1794,	2008,	1900,	255},
	{0x3f,	1841,	2060,	1950,	261},
	{0x0f,	1889,	2113,	2000,	268},
	{0x40,	1936,	2165,	2050,	275},
	{0x41,	1984,	2218,	2100,	281},
	{0x42,	2031,	2270,	2150,	288},
	{0x43,	2079,	2323,	2200,	294},
	{0x44,	2126,	2375,	2250,	302},
	{0x45,	2174,	2428,	2300,	308},
	{0x46,	2221,	2480,	2350,	315},
	{0x47,	2269,	2500,	2400,	321},
	{0x48,	2316,	2500,	2450,	328},
	{0x49,	2364,	2500,	2500,	335}
};

static u16 get_hsfreq_by_mbps(u32 mbps)
{
	unsigned int i = DPHY_FREQ_RANGE_NUM;

	while (i--) {
		if (freqranges[i].default_mbps == mbps ||
		    (mbps >= freqranges[i].min && mbps <= freqranges[i].max))
			return i;
	}

	return DPHY_FREQ_RANGE_INVALID_INDEX;
}

static int ipu6_isys_dwc_phy_config(struct ipu6_isys *isys,
				    u32 phy_id, u32 mbps)
{
	struct ipu6_bus_device *adev = isys->adev;
	struct device *dev = &adev->auxdev.dev;
	struct ipu6_device *isp = adev->isp;
	u32 cfg_clk_freqrange;
	u32 osc_freq_target;
	u32 index;

	dev_dbg(dev, "config Dphy %u with %u mbps", phy_id, mbps);

	index = get_hsfreq_by_mbps(mbps);
	if (index == DPHY_FREQ_RANGE_INVALID_INDEX) {
		dev_err(dev, "link freq not found for mbps %u", mbps);
		return -EINVAL;
	}

	dwc_dphy_write_mask(isys, phy_id, IPU6_DWC_DPHY_HSFREQRANGE,
			    freqranges[index].hsfreq, 0, 7);

	/* Force termination Calibration */
	if (isys->phy_termcal_val) {
		dwc_dphy_ifc_write_mask(isys, phy_id, 0x20a, 0x1, 0, 1);
		dwc_dphy_ifc_write_mask(isys, phy_id, 0x209, 0x3, 0, 2);
		dwc_dphy_ifc_write_mask(isys, phy_id, 0x209,
					isys->phy_termcal_val, 4, 4);
	}

	/*
	 * Enable override to configure the DDL target oscillation
	 * frequency on bit 0 of register 0xe4
	 */
	dwc_dphy_ifc_write_mask(isys, phy_id, 0xe4, 0x1, 0, 1);
	/*
	 * configure registers 0xe2, 0xe3 with the
	 * appropriate DDL target oscillation frequency
	 * 0x1cc(460)
	 */
	osc_freq_target = freqranges[index].osc_freq_target;
	dwc_dphy_ifc_write_mask(isys, phy_id, 0xe2,
				osc_freq_target & 0xff, 0, 8);
	dwc_dphy_ifc_write_mask(isys, phy_id, 0xe3,
				(osc_freq_target >> 8) & 0xf, 0, 4);

	if (mbps < 1500) {
		/* deskew_polarity_rw, for < 1.5Gbps */
		dwc_dphy_ifc_write_mask(isys, phy_id, 0x8, 0x1, 5, 1);
	}

	/*
	 * Set cfgclkfreqrange[5:0] = round[(Fcfg_clk(MHz)-17)*4]
	 * (38.4 - 17) * 4 = ~85 (0x55)
	 */
	cfg_clk_freqrange = (isp->buttress.ref_clk - 170) * 4 / 10;
	dev_dbg(dev, "ref_clk = %u clk_freqrange = %u",
		isp->buttress.ref_clk, cfg_clk_freqrange);
	dwc_dphy_write_mask(isys, phy_id, IPU6_DWC_DPHY_CFGCLKFREQRANGE,
			    cfg_clk_freqrange, 0, 8);

	dwc_dphy_write_mask(isys, phy_id, IPU6_DWC_DPHY_DFT_CTRL2, 0x1, 4, 1);
	dwc_dphy_write_mask(isys, phy_id, IPU6_DWC_DPHY_DFT_CTRL2, 0x1, 8, 1);

	return 0;
}

static void ipu6_isys_dwc_phy_aggr_setup(struct ipu6_isys *isys, u32 master,
					 u32 slave, u32 mbps)
{
	/* Config mastermacro */
	dwc_dphy_ifc_write_mask(isys, master, 0x133, 0x1, 0, 1);
	dwc_dphy_ifc_write_mask(isys, slave, 0x133, 0x0, 0, 1);

	/* Config master PHY clk lane to drive long channel clk */
	dwc_dphy_ifc_write_mask(isys, master, 0x307, 0x1, 2, 1);
	dwc_dphy_ifc_write_mask(isys, slave, 0x307, 0x0, 2, 1);

	/* Config both PHYs data lanes to get clk from long channel */
	dwc_dphy_ifc_write_mask(isys, master, 0x508, 0x1, 5, 1);
	dwc_dphy_ifc_write_mask(isys, slave, 0x508, 0x1, 5, 1);
	dwc_dphy_ifc_write_mask(isys, master, 0x708, 0x1, 5, 1);
	dwc_dphy_ifc_write_mask(isys, slave, 0x708, 0x1, 5, 1);

	/* Config slave PHY clk lane to bypass long channel clk to DDR clk */
	dwc_dphy_ifc_write_mask(isys, master, 0x308, 0x0, 3, 1);
	dwc_dphy_ifc_write_mask(isys, slave, 0x308, 0x1, 3, 1);

	/* Override slave PHY clk lane enable (DPHYRXCLK_CLL_demux module) */
	dwc_dphy_ifc_write_mask(isys, slave, 0xe0, 0x3, 0, 2);

	/* Override slave PHY DDR clk lane enable (DPHYHSRX_div124 module) */
	dwc_dphy_ifc_write_mask(isys, slave, 0xe1, 0x1, 1, 1);
	dwc_dphy_ifc_write_mask(isys, slave, 0x307, 0x1, 3, 1);

	/* Turn off slave PHY LP-RX clk lane */
	dwc_dphy_ifc_write_mask(isys, slave, 0x304, 0x1, 7, 1);
	dwc_dphy_ifc_write_mask(isys, slave, 0x305, 0xa, 0, 5);
}

#define PHY_E	4
static int ipu6_isys_dwc_phy_powerup_ack(struct ipu6_isys *isys, u32 phy_id)
{
	struct device *dev = &isys->adev->auxdev.dev;
	u32 rescal_done;
	int ret;

	ret = dwc_dphy_pwr_up(isys, phy_id);
	if (ret != 0) {
		dev_err(dev, "Dphy %u power up failed(%d)", phy_id, ret);
		return ret;
	}

	/* reset forcerxmode */
	dwc_dphy_write_mask(isys, phy_id, IPU6_DWC_DPHY_DFT_CTRL2, 0, 4, 1);
	dwc_dphy_write_mask(isys, phy_id, IPU6_DWC_DPHY_DFT_CTRL2, 0, 8, 1);

	dev_dbg(dev, "Dphy %u is ready!", phy_id);

	if (phy_id != PHY_E || isys->phy_termcal_val)
		return 0;

	usleep_range(100, 200);
	rescal_done = dwc_dphy_ifc_read_mask(isys, phy_id, 0x221, 7, 1);
	if (rescal_done) {
		isys->phy_termcal_val = dwc_dphy_ifc_read_mask(isys, phy_id,
							       0x220, 2, 4);
		dev_dbg(dev, "termcal done with value = %u",
			isys->phy_termcal_val);
	}

	return 0;
}

static void ipu6_isys_dwc_phy_reset(struct ipu6_isys *isys, u32 phy_id)
{
	dev_dbg(&isys->adev->auxdev.dev, "Reset phy %u", phy_id);

	dwc_dphy_write(isys, phy_id, IPU6_DWC_DPHY_SHUTDOWNZ, 0);
	dwc_dphy_write(isys, phy_id, IPU6_DWC_DPHY_RSTZ, 0);
	dwc_dphy_write(isys, phy_id, IPU6_DWC_DPHY_TEST_IFC_ACCESS_MODE,
		       TEST_IFC_ACCESS_MODE_FSM);
	dwc_dphy_write(isys, phy_id, IPU6_DWC_DPHY_TEST_IFC_REQ,
		       TEST_IFC_REQ_RESET);
}

int ipu6_isys_dwc_phy_set_power(struct ipu6_isys *isys,
				struct ipu6_isys_csi2_config *cfg,
				const struct ipu6_isys_csi2_timing *timing,
				bool on)
{
	struct device *dev = &isys->adev->auxdev.dev;
	void __iomem *isys_base = isys->pdata->base;
	u32 phy_id, primary, secondary;
	u32 nlanes, port, mbps;
	s64 link_freq;
	int ret;

	port = cfg->port;

	if (!isys_base || port >= isys->pdata->ipdata->csi2.nports) {
		dev_warn(dev, "invalid port ID %d\n", port);
		return -EINVAL;
	}

	nlanes = cfg->nlanes;
	/* only port 0, 2 and 4 support 4 lanes */
	if (nlanes == 4 && port % 2) {
		dev_err(dev, "invalid csi-port %u with %u lanes\n", port,
			nlanes);
		return -EINVAL;
	}

	link_freq = ipu6_isys_csi2_get_link_freq(&isys->csi2[port]);
	if (link_freq < 0) {
		dev_err(dev, "get link freq failed(%lld).\n", link_freq);
		return link_freq;
	}

	mbps = div_u64(link_freq, 500000);

	phy_id = port;
	primary = port & ~1;
	secondary = primary + 1;
	if (on) {
		if (nlanes == 4) {
			dev_dbg(dev, "config phy %u and %u in aggr mode\n",
				primary, secondary);

			ipu6_isys_dwc_phy_reset(isys, primary);
			ipu6_isys_dwc_phy_reset(isys, secondary);
			ipu6_isys_dwc_phy_aggr_setup(isys, primary,
						     secondary, mbps);

			ret = ipu6_isys_dwc_phy_config(isys, primary, mbps);
			if (ret)
				return ret;
			ret = ipu6_isys_dwc_phy_config(isys, secondary, mbps);
			if (ret)
				return ret;

			ret = ipu6_isys_dwc_phy_powerup_ack(isys, primary);
			if (ret)
				return ret;

			ret = ipu6_isys_dwc_phy_powerup_ack(isys, secondary);
			return ret;
		}

		dev_dbg(dev, "config phy %u with %u lanes in non-aggr mode\n",
			phy_id, nlanes);

		ipu6_isys_dwc_phy_reset(isys, phy_id);
		ret = ipu6_isys_dwc_phy_config(isys, phy_id, mbps);
		if (ret)
			return ret;

		ret = ipu6_isys_dwc_phy_powerup_ack(isys, phy_id);
		return ret;
	}

	if (nlanes == 4) {
		dev_dbg(dev, "Power down phy %u and phy %u for port %u\n",
			primary, secondary, port);
		ipu6_isys_dwc_phy_reset(isys, secondary);
		ipu6_isys_dwc_phy_reset(isys, primary);

		return 0;
	}

	dev_dbg(dev, "Powerdown phy %u with %u lanes\n", phy_id, nlanes);

	ipu6_isys_dwc_phy_reset(isys, phy_id);

	return 0;
}
