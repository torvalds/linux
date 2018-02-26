/*
 *************************************************************************
 * Copyright (C) 2018 Fuzhou Rockchip Electronics Co., Ltd.
 *
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *************************************************************************
 */

#ifdef CONFIG_ARM64
#include "camsys_soc_priv.h"
#include "camsys_soc_rk3326.h"
#include "camsys_marvin.h"

struct mipiphy_hsfreqrange_s {
	unsigned int range_l;
	unsigned int range_h;
	unsigned char cfg_bit;
};

static struct mipiphy_hsfreqrange_s mipiphy_hsfreqrange[] = {
	{80, 110, 0x00},
	{110, 150, 0x01},
	{150, 200, 0x02},
	{200, 250, 0x03},
	{250, 300, 0x04},
	{300, 400, 0x05},
	{400, 500, 0x06},
	{500, 600, 0x07},
	{600, 700, 0x08},
	{700, 800, 0x09},
	{800, 1000, 0xa},
	{1000, 1100, 0xb},
	{1100, 1250, 0xc},
	{1250, 1350, 0xd},
	{1350, 1500, 0xe}
};

static int camsys_rk3326_mipihpy_cfg(camsys_mipiphy_soc_para_t *para)
{
	unsigned char hsfreqrange = 0xff, i;
	struct mipiphy_hsfreqrange_s *hsfreqrange_p;
	unsigned long csiphy_virt;
	//unsigned long base;

	if (para->camsys_dev->csiphy_reg) {
		csiphy_virt =
		(unsigned long)para->camsys_dev->csiphy_reg->vir_base;
	} else {
		csiphy_virt = 0x00;
	}
	if (para->phy->bit_rate == 0 ||
		para->phy->data_en_bit == 0) {
		if (para->phy->phy_index == 0) {
			write_grf_reg(GRF_PD_VI_CON_OFFSET,
				DPHY_CSIPHY_CLKLANE_EN_OFFSET_MASK |
				(0 << DPHY_CSIPHY_CLKLANE_EN_OFFSET_BITS));
			write_grf_reg(GRF_PD_VI_CON_OFFSET,
				DPHY_CSIPHY_DATALANE_EN_OFFSET_MASK |
				(0 << DPHY_CSIPHY_DATALANE_EN_OFFSET_BITS));
			camsys_trace(1, "mipi phy 0 standby!");
		}

		return 0;
	}

	hsfreqrange_p = mipiphy_hsfreqrange;
	for (i = 0;
		i < (sizeof(mipiphy_hsfreqrange) /
			sizeof(struct mipiphy_hsfreqrange_s));
		i++) {
		if ((para->phy->bit_rate > hsfreqrange_p->range_l) &&
			(para->phy->bit_rate <= hsfreqrange_p->range_h)) {
			hsfreqrange = hsfreqrange_p->cfg_bit;
			break;
		}
		hsfreqrange_p++;
	}

	if (hsfreqrange == 0xff) {
		camsys_err("mipi phy config bitrate %d Mbps isn't supported!",
			para->phy->bit_rate);
		hsfreqrange = 0x00;
	}

	if (para->phy->phy_index == 0) {
		/* phy start */
		write_csiphy_reg(MIPI_CSI_DPHY_CTRL_PWRCTL_OFFSET, 0xe4);

		/* set data lane num and enable clock lane */
		write_csiphy_reg(MIPI_CSI_DPHY_CTRL_LANE_ENABLE_OFFSET,
			((para->phy->data_en_bit << MIPI_CSI_DPHY_CTRL_DATALANE_ENABLE_OFFSET_BIT) |
			(0x1 << MIPI_CSI_DPHY_CTRL_CLKLANE_ENABLE_OFFSET_BIT) | 0x1));
		/* Reset dphy analog part */
		write_csiphy_reg(MIPI_CSI_DPHY_CTRL_PWRCTL_OFFSET, 0xe0);
		usleep_range(500, 1000);
		/* Reset dphy digital part */
		write_csiphy_reg(MIPI_CSI_DPHY_CTRL_DIG_RST_OFFSET, 0x1e);
		write_csiphy_reg(MIPI_CSI_DPHY_CTRL_DIG_RST_OFFSET, 0x1f);
		/* not into receive mode/wait stopstate */
		write_grf_reg(GRF_PD_VI_CON_OFFSET,
			DPHY_CSIPHY_FORCERXMODE_OFFSET_MASK |
			(0x0 << DPHY_CSIPHY_FORCERXMODE_OFFSET_BITS));

		write_csiphy_reg((MIPI_CSI_DPHY_LANEX_THS_SETTLE_OFFSET + 0x100),
			hsfreqrange |
			(read_csiphy_reg(MIPI_CSI_DPHY_LANEX_THS_SETTLE_OFFSET
			+ 0x100) & (~0xf)));

		if (para->phy->data_en_bit > 0x00) {
			write_csiphy_reg((MIPI_CSI_DPHY_LANEX_THS_SETTLE_OFFSET
			+ 0x180), hsfreqrange |
			(read_csiphy_reg(MIPI_CSI_DPHY_LANEX_THS_SETTLE_OFFSET
			+ 0x180) & (~0xf)));
		}
		if (para->phy->data_en_bit > 0x02) {
			write_csiphy_reg(MIPI_CSI_DPHY_LANEX_THS_SETTLE_OFFSET
			+ 0x200, hsfreqrange |
			(read_csiphy_reg(MIPI_CSI_DPHY_LANEX_THS_SETTLE_OFFSET
			+ 0x200) & (~0xf)));
		}
		if (para->phy->data_en_bit > 0x04) {
			write_csiphy_reg(MIPI_CSI_DPHY_LANEX_THS_SETTLE_OFFSET
				+ 0x280, hsfreqrange |
				(read_csiphy_reg(MIPI_CSI_DPHY_LANEX_THS_SETTLE_OFFSET
				+ 0x280) & (~0xf)));
			write_csiphy_reg(MIPI_CSI_DPHY_LANEX_THS_SETTLE_OFFSET
				+ 0x300, hsfreqrange |
				(read_csiphy_reg(MIPI_CSI_DPHY_LANEX_THS_SETTLE_OFFSET
				+ 0x300) & (~0xf)));
		}

	write_grf_reg(GRF_PD_VI_CON_OFFSET,
		DPHY_CSIPHY_CLKLANE_EN_OFFSET_MASK |
		(1 << DPHY_CSIPHY_CLKLANE_EN_OFFSET_BITS));
	write_grf_reg(GRF_PD_VI_CON_OFFSET,
		DPHY_CSIPHY_DATALANE_EN_OFFSET_MASK |
		(0x0f << DPHY_CSIPHY_DATALANE_EN_OFFSET_BITS));

	} else {
		camsys_err("mipi phy index %d is invalidate!",
			para->phy->phy_index);
		goto fail;
	}

	camsys_trace(1, "mipi phy(%d) turn on(lane: 0x%x  bit_rate: %dMbps)",
		para->phy->phy_index,
		para->phy->data_en_bit, para->phy->bit_rate);

	return 0;

fail:
	return -1;
}

#define VI_IRCL			    0x0014
/**
 * reset on too high isp_clk rate will result in bus dead.
 * The signoff isp_clk rate is 350M, and the recommended rate
 * on reset from IC is NOT greater than 300M.
 */
#define SAFETY_RESET_ISPCLK_RATE_LIMIT 300000000
int camsys_rk3326_cfg
(
	camsys_dev_t *camsys_dev,
	camsys_soc_cfg_t cfg_cmd,
	void *cfg_para
)
{
	unsigned int *para_int;

	switch (cfg_cmd) {
	case Clk_DriverStrength_Cfg: {
		para_int = (unsigned int *)cfg_para;
		__raw_writel((((*para_int) & 0x03) << 6) | (0x03 << 22),
				(void *)(camsys_dev->rk_grf_base + 0x104));//m0 cifclk_out
		break;
	}

	case Cif_IoDomain_Cfg: {
		para_int = (unsigned int *)cfg_para;
		if (*para_int < 28000000) {
			/* 1.8v IO */
			__raw_writel((1 << GRF_IO_VSEL_VCCIO3_BITS) | GRF_IO_VSEL_VCCIO3_MASK,
				(void *)(camsys_dev->rk_grf_base + GRF_IO_VSEL_OFFSET));
		} else {
			/* 3.3v IO */
			__raw_writel((0 << GRF_IO_VSEL_VCCIO3_BITS) | GRF_IO_VSEL_VCCIO3_MASK,
				(void *)(camsys_dev->rk_grf_base + GRF_IO_VSEL_OFFSET));
			}
		break;
	}

	case Mipi_Phy_Cfg: {
		camsys_rk3326_mipihpy_cfg
			((camsys_mipiphy_soc_para_t *)cfg_para);
		break;
	}

	case Isp_SoftRst: {/* ddl@rock-chips.com: v0.d.0 */
		unsigned long reset;

		reset = (unsigned long)cfg_para;

		if (reset == 1) {
			camsys_mrv_clk_t *clk =
				(camsys_mrv_clk_t *)camsys_dev->clk;
			long old_ispclk_rate = clk_get_rate(clk->isp);

			/* check the isp_clk before isp reset operation */
			if (old_ispclk_rate > SAFETY_RESET_ISPCLK_RATE_LIMIT)
				clk_set_rate(clk->isp,
					     SAFETY_RESET_ISPCLK_RATE_LIMIT);
			__raw_writel(0x80, (void *)(camsys_dev->rk_isp_base +
			VI_IRCL));
			usleep_range(100, 200);
			__raw_writel(0x00, (void *)(camsys_dev->rk_isp_base +
			VI_IRCL));
			/* restore the old ispclk after reset */
			if (old_ispclk_rate != SAFETY_RESET_ISPCLK_RATE_LIMIT)
				clk_set_rate(clk->isp, old_ispclk_rate);
		}
		camsys_trace(2, "Isp self soft rst: %ld", reset);
		break;
	}
	default:
	{
		camsys_warn("cfg_cmd: 0x%x isn't support", cfg_cmd);
		break;
	}

	}

	return 0;
}
#endif /* CONFIG_ARM64 */
