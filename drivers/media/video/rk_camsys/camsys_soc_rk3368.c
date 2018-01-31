/* SPDX-License-Identifier: GPL-2.0 */
#ifdef CONFIG_ARM64
#include "camsys_soc_priv.h"
#include "camsys_soc_rk3368.h"


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

#if 0
static int camsys_rk3368_mipiphy_wr_reg
(unsigned long phy_virt, unsigned char addr, unsigned char data)
{
	/*TESTEN =1,TESTDIN=addr */
	write_csihost_reg(CSIHOST_PHY_TEST_CTRL1, (0x00010000 | addr));
	/*TESTCLK=0 */
	write_csihost_reg(CSIHOST_PHY_TEST_CTRL0, 0x00000000);
	udelay(10);
	/*TESTEN =0,TESTDIN=data */
	write_csihost_reg(CSIHOST_PHY_TEST_CTRL1, (0x00000000 | data));
	/*TESTCLK=1 */
	write_csihost_reg(CSIHOST_PHY_TEST_CTRL0, 0x00000002);
	udelay(10);

	return 0;
}

static int camsys_rk3368_mipiphy_rd_reg
(unsigned long phy_virt, unsigned char addr)
{
	return (read_csihost_reg(((CSIHOST_PHY_TEST_CTRL1)&0xff00))>>8);
}

static int camsys_rk3368_csiphy_wr_reg
(unsigned long csiphy_virt, unsigned char addr, unsigned char data)
{
	write_csiphy_reg(addr, data);
	return 0;
}

static int camsys_rk3368_csiphy_rd_reg
(unsigned long csiphy_virt, unsigned char addr)
{
	return read_csiphy_reg(addr);
}
#endif
static int camsys_rk3368_mipihpy_cfg(camsys_mipiphy_soc_para_t *para)
{
	unsigned char hsfreqrange = 0xff, i;
	struct mipiphy_hsfreqrange_s *hsfreqrange_p;
	unsigned long phy_virt, phy_index;
	unsigned long base;
	unsigned long csiphy_virt;

	phy_index = para->phy->phy_index;
	if (para->camsys_dev->mipiphy[phy_index].reg != NULL) {
		phy_virt = para->camsys_dev->mipiphy[phy_index].reg->vir_base;
	} else {
		phy_virt = 0x00;
	}
	if (para->camsys_dev->csiphy_reg != NULL) {
		csiphy_virt =
		(unsigned long)para->camsys_dev->csiphy_reg->vir_base;
	} else {
		csiphy_virt = 0x00;
	}
	if ((para->phy->bit_rate == 0) ||
		(para->phy->data_en_bit == 0)) {
		if (para->phy->phy_index == 0) {
			base =
			(unsigned long)
			para->camsys_dev->devmems.registermem->vir_base;
			*((unsigned int *)
				(base + (MRV_MIPI_BASE + MRV_MIPI_CTRL)))
				&= ~(0x0f << 8);
			camsys_trace(1, "mipi phy 0 standby!");
		}

		return 0;
	}

	hsfreqrange_p = mipiphy_hsfreqrange;
	for (i = 0;
		i < (sizeof(mipiphy_hsfreqrange)/
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
	/* isp select */
	write_grf_reg(GRF_SOC_CON6_OFFSET, ISP_MIPI_CSI_HOST_SEL_OFFSET_MASK
				| (1 << ISP_MIPI_CSI_HOST_SEL_OFFSET_BIT));

		/* phy start */
		write_csiphy_reg(MIPI_CSI_DPHY_CTRL_PWRCTL_OFFSET, 0xe4);

		/* set data lane num and enable clock lane */
		write_csiphy_reg(MIPI_CSI_DPHY_CTRL_LANE_ENABLE_OFFSET,
			((para->phy->data_en_bit << MIPI_CSI_DPHY_CTRL_LANE_ENABLE_OFFSET_BIT) |
			(0x1 << 6) | 0x1));
		/* Reset dphy analog part */
		write_csiphy_reg(MIPI_CSI_DPHY_CTRL_PWRCTL_OFFSET, 0xe0);
		usleep_range(500, 1000);
		/* Reset dphy digital part */
		write_csiphy_reg(MIPI_CSI_DPHY_CTRL_DIG_RST_OFFSET, 0x1e);
		write_csiphy_reg(MIPI_CSI_DPHY_CTRL_DIG_RST_OFFSET, 0x1f);

		write_grf_reg(GRF_SOC_CON6_OFFSET,
			MIPI_CSI_DPHY_RX_FORCERXMODE_MASK |
			(0x0 << MIPI_CSI_DPHY_RX_FORCERXMODE_BIT));

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
		/*
		 * MIPI CTRL bit8:11 SHUTDOWN_LANE are invert
		 * connect to dphy pin_enable_x
		 */
	base = (unsigned long)para->camsys_dev->devmems.registermem->vir_base;
	*((unsigned int *)(base + (MRV_MIPI_BASE + MRV_MIPI_CTRL)))
							&= ~(0x0f << 8);
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

#define MRV_AFM_BASE		0x0000
#define VI_IRCL			0x0014
int camsys_rk3368_cfg(
camsys_dev_t *camsys_dev, camsys_soc_cfg_t cfg_cmd, void *cfg_para)
{
	unsigned int *para_int;

	switch (cfg_cmd) {
	case Clk_DriverStrength_Cfg: {
		para_int = (unsigned int *)cfg_para;
		__raw_writel((((*para_int) & 0x03) << 3) | (0x03 << 3),
				(void *)(camsys_dev->rk_grf_base + 0x204));
		/* set 0xffffffff to max all */
		break;
	}

	case Cif_IoDomain_Cfg: {
		para_int = (unsigned int *)cfg_para;
		if (*para_int < 28000000) {
			/* 1.8v IO */
			__raw_writel(((1 << 1) | (1 << (1 + 16))),
				(void *)(camsys_dev->rk_grf_base + 0x0900));
		} else {
			/* 3.3v IO */
			__raw_writel(((0 << 1) | (1 << (1 + 16))),
				(void *)(camsys_dev->rk_grf_base + 0x0900));
			}
		break;
	}

	case Mipi_Phy_Cfg: {
		camsys_rk3368_mipihpy_cfg
			((camsys_mipiphy_soc_para_t *)cfg_para);
		break;
	}

	case Isp_SoftRst: {/* ddl@rock-chips.com: v0.d.0 */
		unsigned long reset;

		reset = (unsigned long)cfg_para;

		if (reset == 1)
			__raw_writel(0x80, (void *)(camsys_dev->rk_isp_base +
			MRV_AFM_BASE + VI_IRCL));
		else
			__raw_writel(0x00, (void *)(camsys_dev->rk_isp_base +
			MRV_AFM_BASE + VI_IRCL));
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
