// SPDX-License-Identifier: GPL-2.0
/*
 * XGE PCSR module initialisation
 *
 * Copyright (C) 2014 Texas Instruments Incorporated
 * Authors:	Sandeep Nair <sandeep_n@ti.com>
 *		WingMan Kwok <w-kwok2@ti.com>
 *
 */
#include "netcp.h"

/* XGBE registers */
#define XGBE_CTRL_OFFSET		0x0c
#define XGBE_SGMII_1_OFFSET		0x0114
#define XGBE_SGMII_2_OFFSET		0x0214

/* PCS-R registers */
#define PCSR_CPU_CTRL_OFFSET		0x1fd0
#define POR_EN				BIT(29)

#define reg_rmw(addr, value, mask) \
	writel(((readl(addr) & (~(mask))) | \
			(value & (mask))), (addr))

/* bit mask of width w at offset s */
#define MASK_WID_SH(w, s)		(((1 << w) - 1) << s)

/* shift value v to offset s */
#define VAL_SH(v, s)			(v << s)

#define PHY_A(serdes)			0

struct serdes_cfg {
	u32 ofs;
	u32 val;
	u32 mask;
};

static struct serdes_cfg cfg_phyb_1p25g_156p25mhz_cmu0[] = {
	{0x0000, 0x00800002, 0x00ff00ff},
	{0x0014, 0x00003838, 0x0000ffff},
	{0x0060, 0x1c44e438, 0xffffffff},
	{0x0064, 0x00c18400, 0x00ffffff},
	{0x0068, 0x17078200, 0xffffff00},
	{0x006c, 0x00000014, 0x000000ff},
	{0x0078, 0x0000c000, 0x0000ff00},
	{0x0000, 0x00000003, 0x000000ff},
};

static struct serdes_cfg cfg_phyb_10p3125g_156p25mhz_cmu1[] = {
	{0x0c00, 0x00030002, 0x00ff00ff},
	{0x0c14, 0x00005252, 0x0000ffff},
	{0x0c28, 0x80000000, 0xff000000},
	{0x0c2c, 0x000000f6, 0x000000ff},
	{0x0c3c, 0x04000405, 0xff00ffff},
	{0x0c40, 0xc0800000, 0xffff0000},
	{0x0c44, 0x5a202062, 0xffffffff},
	{0x0c48, 0x40040424, 0xffffffff},
	{0x0c4c, 0x00004002, 0x0000ffff},
	{0x0c50, 0x19001c00, 0xff00ff00},
	{0x0c54, 0x00002100, 0x0000ff00},
	{0x0c58, 0x00000060, 0x000000ff},
	{0x0c60, 0x80131e7c, 0xffffffff},
	{0x0c64, 0x8400cb02, 0xff00ffff},
	{0x0c68, 0x17078200, 0xffffff00},
	{0x0c6c, 0x00000016, 0x000000ff},
	{0x0c74, 0x00000400, 0x0000ff00},
	{0x0c78, 0x0000c000, 0x0000ff00},
	{0x0c00, 0x00000003, 0x000000ff},
};

static struct serdes_cfg cfg_phyb_10p3125g_16bit_lane[] = {
	{0x0204, 0x00000080, 0x000000ff},
	{0x0208, 0x0000920d, 0x0000ffff},
	{0x0204, 0xfc000000, 0xff000000},
	{0x0208, 0x00009104, 0x0000ffff},
	{0x0210, 0x1a000000, 0xff000000},
	{0x0214, 0x00006b58, 0x00ffffff},
	{0x0218, 0x75800084, 0xffff00ff},
	{0x022c, 0x00300000, 0x00ff0000},
	{0x0230, 0x00003800, 0x0000ff00},
	{0x024c, 0x008f0000, 0x00ff0000},
	{0x0250, 0x30000000, 0xff000000},
	{0x0260, 0x00000002, 0x000000ff},
	{0x0264, 0x00000057, 0x000000ff},
	{0x0268, 0x00575700, 0x00ffff00},
	{0x0278, 0xff000000, 0xff000000},
	{0x0280, 0x00500050, 0x00ff00ff},
	{0x0284, 0x00001f15, 0x0000ffff},
	{0x028c, 0x00006f00, 0x0000ff00},
	{0x0294, 0x00000000, 0xffffff00},
	{0x0298, 0x00002640, 0xff00ffff},
	{0x029c, 0x00000003, 0x000000ff},
	{0x02a4, 0x00000f13, 0x0000ffff},
	{0x02a8, 0x0001b600, 0x00ffff00},
	{0x0380, 0x00000030, 0x000000ff},
	{0x03c0, 0x00000200, 0x0000ff00},
	{0x03cc, 0x00000018, 0x000000ff},
	{0x03cc, 0x00000000, 0x000000ff},
};

static struct serdes_cfg cfg_phyb_10p3125g_comlane[] = {
	{0x0a00, 0x00000800, 0x0000ff00},
	{0x0a84, 0x00000000, 0x000000ff},
	{0x0a8c, 0x00130000, 0x00ff0000},
	{0x0a90, 0x77a00000, 0xffff0000},
	{0x0a94, 0x00007777, 0x0000ffff},
	{0x0b08, 0x000f0000, 0xffff0000},
	{0x0b0c, 0x000f0000, 0x00ffffff},
	{0x0b10, 0xbe000000, 0xff000000},
	{0x0b14, 0x000000ff, 0x000000ff},
	{0x0b18, 0x00000014, 0x000000ff},
	{0x0b5c, 0x981b0000, 0xffff0000},
	{0x0b64, 0x00001100, 0x0000ff00},
	{0x0b78, 0x00000c00, 0x0000ff00},
	{0x0abc, 0xff000000, 0xff000000},
	{0x0ac0, 0x0000008b, 0x000000ff},
};

static struct serdes_cfg cfg_cm_c1_c2[] = {
	{0x0208, 0x00000000, 0x00000f00},
	{0x0208, 0x00000000, 0x0000001f},
	{0x0204, 0x00000000, 0x00040000},
	{0x0208, 0x000000a0, 0x000000e0},
};

static void netcp_xgbe_serdes_cmu_init(void __iomem *serdes_regs)
{
	int i;

	/* cmu0 setup */
	for (i = 0; i < ARRAY_SIZE(cfg_phyb_1p25g_156p25mhz_cmu0); i++) {
		reg_rmw(serdes_regs + cfg_phyb_1p25g_156p25mhz_cmu0[i].ofs,
			cfg_phyb_1p25g_156p25mhz_cmu0[i].val,
			cfg_phyb_1p25g_156p25mhz_cmu0[i].mask);
	}

	/* cmu1 setup */
	for (i = 0; i < ARRAY_SIZE(cfg_phyb_10p3125g_156p25mhz_cmu1); i++) {
		reg_rmw(serdes_regs + cfg_phyb_10p3125g_156p25mhz_cmu1[i].ofs,
			cfg_phyb_10p3125g_156p25mhz_cmu1[i].val,
			cfg_phyb_10p3125g_156p25mhz_cmu1[i].mask);
	}
}

/* lane is 0 based */
static void netcp_xgbe_serdes_lane_config(
			void __iomem *serdes_regs, int lane)
{
	int i;

	/* lane setup */
	for (i = 0; i < ARRAY_SIZE(cfg_phyb_10p3125g_16bit_lane); i++) {
		reg_rmw(serdes_regs +
				cfg_phyb_10p3125g_16bit_lane[i].ofs +
				(0x200 * lane),
			cfg_phyb_10p3125g_16bit_lane[i].val,
			cfg_phyb_10p3125g_16bit_lane[i].mask);
	}

	/* disable auto negotiation*/
	reg_rmw(serdes_regs + (0x200 * lane) + 0x0380,
		0x00000000, 0x00000010);

	/* disable link training */
	reg_rmw(serdes_regs + (0x200 * lane) + 0x03c0,
		0x00000000, 0x00000200);
}

static void netcp_xgbe_serdes_com_enable(void __iomem *serdes_regs)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(cfg_phyb_10p3125g_comlane); i++) {
		reg_rmw(serdes_regs + cfg_phyb_10p3125g_comlane[i].ofs,
			cfg_phyb_10p3125g_comlane[i].val,
			cfg_phyb_10p3125g_comlane[i].mask);
	}
}

static void netcp_xgbe_serdes_lane_enable(
			void __iomem *serdes_regs, int lane)
{
	/* Set Lane Control Rate */
	writel(0xe0e9e038, serdes_regs + 0x1fe0 + (4 * lane));
}

static void netcp_xgbe_serdes_phyb_rst_clr(void __iomem *serdes_regs)
{
	reg_rmw(serdes_regs + 0x0a00, 0x0000001f, 0x000000ff);
}

static void netcp_xgbe_serdes_pll_disable(void __iomem *serdes_regs)
{
	writel(0x88000000, serdes_regs + 0x1ff4);
}

static void netcp_xgbe_serdes_pll_enable(void __iomem *serdes_regs)
{
	netcp_xgbe_serdes_phyb_rst_clr(serdes_regs);
	writel(0xee000000, serdes_regs + 0x1ff4);
}

static int netcp_xgbe_wait_pll_locked(void __iomem *sw_regs)
{
	unsigned long timeout;
	int ret = 0;
	u32 val_1, val_0;

	timeout = jiffies + msecs_to_jiffies(500);
	do {
		val_0 = (readl(sw_regs + XGBE_SGMII_1_OFFSET) & BIT(4));
		val_1 = (readl(sw_regs + XGBE_SGMII_2_OFFSET) & BIT(4));

		if (val_1 && val_0)
			return 0;

		if (time_after(jiffies, timeout)) {
			ret = -ETIMEDOUT;
			break;
		}

		cpu_relax();
	} while (true);

	pr_err("XGBE serdes not locked: time out.\n");
	return ret;
}

static void netcp_xgbe_serdes_enable_xgmii_port(void __iomem *sw_regs)
{
	writel(0x03, sw_regs + XGBE_CTRL_OFFSET);
}

static u32 netcp_xgbe_serdes_read_tbus_val(void __iomem *serdes_regs)
{
	u32 tmp;

	if (PHY_A(serdes_regs)) {
		tmp  = (readl(serdes_regs + 0x0ec) >> 24) & 0x0ff;
		tmp |= ((readl(serdes_regs + 0x0fc) >> 16) & 0x00f00);
	} else {
		tmp  = (readl(serdes_regs + 0x0f8) >> 16) & 0x0fff;
	}

	return tmp;
}

static void netcp_xgbe_serdes_write_tbus_addr(void __iomem *serdes_regs,
					      int select, int ofs)
{
	if (PHY_A(serdes_regs)) {
		reg_rmw(serdes_regs + 0x0008, ((select << 5) + ofs) << 24,
			~0x00ffffff);
		return;
	}

	/* For 2 lane Phy-B, lane0 is actually lane1 */
	switch (select) {
	case 1:
		select = 2;
		break;
	case 2:
		select = 3;
		break;
	default:
		return;
	}

	reg_rmw(serdes_regs + 0x00fc, ((select << 8) + ofs) << 16, ~0xf800ffff);
}

static u32 netcp_xgbe_serdes_read_select_tbus(void __iomem *serdes_regs,
					      int select, int ofs)
{
	/* Set tbus address */
	netcp_xgbe_serdes_write_tbus_addr(serdes_regs, select, ofs);
	/* Get TBUS Value */
	return netcp_xgbe_serdes_read_tbus_val(serdes_regs);
}

static void netcp_xgbe_serdes_reset_cdr(void __iomem *serdes_regs,
					void __iomem *sig_detect_reg, int lane)
{
	u32 tmp, dlpf, tbus;

	/*Get the DLPF values */
	tmp = netcp_xgbe_serdes_read_select_tbus(
			serdes_regs, lane + 1, 5);

	dlpf = tmp >> 2;

	if (dlpf < 400 || dlpf > 700) {
		reg_rmw(sig_detect_reg, VAL_SH(2, 1), MASK_WID_SH(2, 1));
		mdelay(1);
		reg_rmw(sig_detect_reg, VAL_SH(0, 1), MASK_WID_SH(2, 1));
	} else {
		tbus = netcp_xgbe_serdes_read_select_tbus(serdes_regs, lane +
							  1, 0xe);

		pr_debug("XGBE: CDR centered, DLPF: %4d,%d,%d.\n",
			 tmp >> 2, tmp & 3, (tbus >> 2) & 3);
	}
}

/* Call every 100 ms */
static int netcp_xgbe_check_link_status(void __iomem *serdes_regs,
					void __iomem *sw_regs, u32 lanes,
					u32 *current_state, u32 *lane_down)
{
	void __iomem *pcsr_base = sw_regs + 0x0600;
	void __iomem *sig_detect_reg;
	u32 pcsr_rx_stat, blk_lock, blk_errs;
	int loss, i, status = 1;

	for (i = 0; i < lanes; i++) {
		/* Get the Loss bit */
		loss = readl(serdes_regs + 0x1fc0 + 0x20 + (i * 0x04)) & 0x1;

		/* Get Block Errors and Block Lock bits */
		pcsr_rx_stat = readl(pcsr_base + 0x0c + (i * 0x80));
		blk_lock = (pcsr_rx_stat >> 30) & 0x1;
		blk_errs = (pcsr_rx_stat >> 16) & 0x0ff;

		/* Get Signal Detect Overlay Address */
		sig_detect_reg = serdes_regs + (i * 0x200) + 0x200 + 0x04;

		/* If Block errors maxed out, attempt recovery! */
		if (blk_errs == 0x0ff)
			blk_lock = 0;

		switch (current_state[i]) {
		case 0:
			/* if good link lock the signal detect ON! */
			if (!loss && blk_lock) {
				pr_debug("XGBE PCSR Linked Lane: %d\n", i);
				reg_rmw(sig_detect_reg, VAL_SH(3, 1),
					MASK_WID_SH(2, 1));
				current_state[i] = 1;
			} else if (!blk_lock) {
				/* if no lock, then reset CDR */
				pr_debug("XGBE PCSR Recover Lane: %d\n", i);
				netcp_xgbe_serdes_reset_cdr(serdes_regs,
							    sig_detect_reg, i);
			}
			break;

		case 1:
			if (!blk_lock) {
				/* Link Lost? */
				lane_down[i] = 1;
				current_state[i] = 2;
			}
			break;

		case 2:
			if (blk_lock)
				/* Nope just noise */
				current_state[i] = 1;
			else {
				/* Lost the block lock, reset CDR if it is
				 * not centered and go back to sync state
				 */
				netcp_xgbe_serdes_reset_cdr(serdes_regs,
							    sig_detect_reg, i);
				current_state[i] = 0;
			}
			break;

		default:
			pr_err("XGBE: unknown current_state[%d] %d\n",
			       i, current_state[i]);
			break;
		}

		if (blk_errs > 0) {
			/* Reset the Error counts! */
			reg_rmw(pcsr_base + 0x08 + (i * 0x80), VAL_SH(0x19, 0),
				MASK_WID_SH(8, 0));

			reg_rmw(pcsr_base + 0x08 + (i * 0x80), VAL_SH(0x00, 0),
				MASK_WID_SH(8, 0));
		}

		status &= (current_state[i] == 1);
	}

	return status;
}

static int netcp_xgbe_serdes_check_lane(void __iomem *serdes_regs,
					void __iomem *sw_regs)
{
	u32 current_state[2] = {0, 0};
	int retries = 0, link_up;
	u32 lane_down[2];

	do {
		lane_down[0] = 0;
		lane_down[1] = 0;

		link_up = netcp_xgbe_check_link_status(serdes_regs, sw_regs, 2,
						       current_state,
						       lane_down);

		/* if we did not get link up then wait 100ms before calling
		 * it again
		 */
		if (link_up)
			break;

		if (lane_down[0])
			pr_debug("XGBE: detected link down on lane 0\n");

		if (lane_down[1])
			pr_debug("XGBE: detected link down on lane 1\n");

		if (++retries > 1) {
			pr_debug("XGBE: timeout waiting for serdes link up\n");
			return -ETIMEDOUT;
		}
		mdelay(100);
	} while (!link_up);

	pr_debug("XGBE: PCSR link is up\n");
	return 0;
}

static void netcp_xgbe_serdes_setup_cm_c1_c2(void __iomem *serdes_regs,
					     int lane, int cm, int c1, int c2)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(cfg_cm_c1_c2); i++) {
		reg_rmw(serdes_regs + cfg_cm_c1_c2[i].ofs + (0x200 * lane),
			cfg_cm_c1_c2[i].val,
			cfg_cm_c1_c2[i].mask);
	}
}

static void netcp_xgbe_reset_serdes(void __iomem *serdes_regs)
{
	/* Toggle the POR_EN bit in CONFIG.CPU_CTRL */
	/* enable POR_EN bit */
	reg_rmw(serdes_regs + PCSR_CPU_CTRL_OFFSET, POR_EN, POR_EN);
	usleep_range(10, 100);

	/* disable POR_EN bit */
	reg_rmw(serdes_regs + PCSR_CPU_CTRL_OFFSET, 0, POR_EN);
	usleep_range(10, 100);
}

static int netcp_xgbe_serdes_config(void __iomem *serdes_regs,
				    void __iomem *sw_regs)
{
	u32 ret, i;

	netcp_xgbe_serdes_pll_disable(serdes_regs);
	netcp_xgbe_serdes_cmu_init(serdes_regs);

	for (i = 0; i < 2; i++)
		netcp_xgbe_serdes_lane_config(serdes_regs, i);

	netcp_xgbe_serdes_com_enable(serdes_regs);
	/* This is EVM + RTM-BOC specific */
	for (i = 0; i < 2; i++)
		netcp_xgbe_serdes_setup_cm_c1_c2(serdes_regs, i, 0, 0, 5);

	netcp_xgbe_serdes_pll_enable(serdes_regs);
	for (i = 0; i < 2; i++)
		netcp_xgbe_serdes_lane_enable(serdes_regs, i);

	/* SB PLL Status Poll */
	ret = netcp_xgbe_wait_pll_locked(sw_regs);
	if (ret)
		return ret;

	netcp_xgbe_serdes_enable_xgmii_port(sw_regs);
	netcp_xgbe_serdes_check_lane(serdes_regs, sw_regs);
	return ret;
}

int netcp_xgbe_serdes_init(void __iomem *serdes_regs, void __iomem *xgbe_regs)
{
	u32 val;

	/* read COMLANE bits 4:0 */
	val = readl(serdes_regs + 0xa00);
	if (val & 0x1f) {
		pr_debug("XGBE: serdes already in operation - reset\n");
		netcp_xgbe_reset_serdes(serdes_regs);
	}
	return netcp_xgbe_serdes_config(serdes_regs, xgbe_regs);
}
