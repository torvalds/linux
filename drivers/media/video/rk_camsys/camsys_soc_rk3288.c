/* SPDX-License-Identifier: GPL-2.0 */
#ifdef CONFIG_ARM
#include "camsys_soc_priv.h"
#include "camsys_soc_rk3288.h"

struct mipiphy_hsfreqrange_s {
	unsigned int range_l;
	unsigned int range_h;
	unsigned char cfg_bit;
};

static struct mipiphy_hsfreqrange_s mipiphy_hsfreqrange[] = {
	{80, 90, 0x00},
	{90, 100, 0x10},
	{100, 110, 0x20},
	{110, 130, 0x01},
	{130, 140, 0x11},
	{140, 150, 0x21},
	{150, 170, 0x02},
	{170, 180, 0x12},
	{180, 200, 0x22},
	{200, 220, 0x03},
	{220, 240, 0x13},
	{240, 250, 0x23},
	{250, 270, 0x4},
	{270, 300, 0x14},
	{300, 330, 0x5},
	{330, 360, 0x15},
	{360, 400, 0x25},
	{400, 450, 0x06},
	{450, 500, 0x16},
	{500, 550, 0x07},
	{550, 600, 0x17},
	{600, 650, 0x08},
	{650, 700, 0x18},
	{700, 750, 0x09},
	{750, 800, 0x19},
	{800, 850, 0x29},
	{850, 900, 0x39},
	{900, 950, 0x0a},
	{950, 1000, 0x1a}

};

static int camsys_rk3288_mipiphy0_wr_reg(
unsigned char addr, unsigned char data, camsys_mipiphy_soc_para_t *para)
{
    /* TESTCLK=1 */
	write_grf_reg(GRF_SOC_CON14_OFFSET,
	    DPHY_RX0_TESTCLK_MASK | DPHY_RX0_TESTCLK);
    /* TESTEN =1,TESTDIN=addr */
	write_grf_reg(GRF_SOC_CON14_OFFSET,
		((addr << DPHY_RX0_TESTDIN_OFFSET)
		| DPHY_RX0_TESTDIN_MASK | DPHY_RX0_TESTEN |
		DPHY_RX0_TESTEN_MASK));
    /* TESTCLK=0 */
	write_grf_reg(GRF_SOC_CON14_OFFSET,
		DPHY_RX0_TESTCLK_MASK);

	if (data != 0xff) { /* write data ? */
		/* TESTEN =0,TESTDIN=data */
		write_grf_reg(GRF_SOC_CON14_OFFSET,
			((data << DPHY_RX0_TESTDIN_OFFSET)
			| DPHY_RX0_TESTDIN_MASK | DPHY_RX0_TESTEN_MASK));

		/* TESTCLK=1 */
		write_grf_reg(GRF_SOC_CON14_OFFSET,
			DPHY_RX0_TESTCLK_MASK |
			DPHY_RX0_TESTCLK);
	}
	return 0;
}

#if 0
static int camsys_rk3288_mipiphy0_rd_reg(unsigned char addr)
{
	return read_grf_reg(GRF_SOC_STATUS21);
}
#endif

static int camsys_rk3288_mipiphy1_wr_reg(
unsigned int phy_virt, unsigned char addr, unsigned char data)
{
	/* TESTEN =1,TESTDIN=addr */
	write_csihost_reg(CSIHOST_PHY_TEST_CTRL1, (0x00010000 | addr));
	/* TESTCLK=0 */
	write_csihost_reg(CSIHOST_PHY_TEST_CTRL0, 0x00000000);
	/* TESTEN =0,TESTDIN=data */
	write_csihost_reg(CSIHOST_PHY_TEST_CTRL1, (0x00000000 | data));
	/* TESTCLK=1 */
	write_csihost_reg(CSIHOST_PHY_TEST_CTRL0, 0x00000002);

	return 0;
}

static int camsys_rk3288_mipiphy1_rd_reg(
unsigned int phy_virt, unsigned char addr)
{
	return (read_csihost_reg
			(((CSIHOST_PHY_TEST_CTRL1) & 0xff00)) >> 8);
}

static int camsys_rk3288_mipihpy_cfg(
camsys_mipiphy_soc_para_t *para)
{
	unsigned char hsfreqrange = 0xff, i;
	struct mipiphy_hsfreqrange_s *hsfreqrange_p;
	unsigned int phy_virt, phy_index;
	unsigned int *base;
	unsigned int data_en_bit, data_en_num = 0;

	phy_index = para->phy->phy_index;
	if (para->camsys_dev->mipiphy[phy_index].reg != NULL) {
		phy_virt  =
		para->camsys_dev->mipiphy[phy_index].reg->vir_base;
	} else {
		phy_virt = 0x00;
	}

	if ((para->phy->bit_rate == 0) ||
		(para->phy->data_en_bit == 0)) {
		if (para->phy->phy_index == 0) {
			base =
				(unsigned int *)
				para->camsys_dev->devmems.registermem->vir_base;
			*(base + (MRV_MIPI_BASE + MRV_MIPI_CTRL) / 4)
				&= ~(0x0f << 8);
			camsys_trace(1, "mipi phy 0 standby!");
		} else if (para->phy->phy_index == 1) {
			/* SHUTDOWNZ=0 */
			write_csihost_reg
				(CSIHOST_PHY_SHUTDOWNZ, 0x00000000);
			/* RSTZ=0 */
			write_csihost_reg(CSIHOST_DPHY_RSTZ, 0x00000000);

			camsys_trace(1, "mipi phy 1 standby!");
		}

		return 0;
	}

	hsfreqrange_p = mipiphy_hsfreqrange;
	for (i = 0;
		i <
			(sizeof(mipiphy_hsfreqrange) /
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
	hsfreqrange <<= 1;

	if (para->phy->phy_index == 0) {
		write_grf_reg(GRF_SOC_CON6_OFFSET,
					MIPI_PHY_DPHYSEL_OFFSET_MASK
					| (para->phy->phy_index
					<< MIPI_PHY_DPHYSEL_OFFSET_BIT));

		/*  set lane num */
		write_grf_reg(GRF_SOC_CON10_OFFSET,
					DPHY_RX0_ENABLE_MASK
					| (para->phy->data_en_bit
					<< DPHY_RX0_ENABLE_OFFSET_BITS));
		/*  set lan turndisab as 1 */
		write_grf_reg(GRF_SOC_CON10_OFFSET,
					DPHY_RX0_TURN_DISABLE_MASK
					| (0xf
					<< DPHY_RX0_TURN_DISABLE_OFFSET_BITS));
		write_grf_reg(GRF_SOC_CON10_OFFSET,
					(0x0 << 4) | (0xf << 20));
		/*  set lan turnrequest as 0 */
		write_grf_reg(GRF_SOC_CON15_OFFSET,
					DPHY_RX0_TURN_REQUEST_MASK
					| (0x0
					<< DPHY_RX0_TURN_REQUEST_OFFSET_BITS));

		/* phy start */
		{
			/* TESTCLK=1 */
			write_grf_reg(GRF_SOC_CON14_OFFSET,
						DPHY_RX0_TESTCLK_MASK
						| DPHY_RX0_TESTCLK);
			/* TESTCLR=1 */
			write_grf_reg(GRF_SOC_CON14_OFFSET,
						DPHY_RX0_TESTCLR_MASK
						| DPHY_RX0_TESTCLR);
			udelay(100);
			 /* TESTCLR=0  zyc */
			write_grf_reg(GRF_SOC_CON14_OFFSET,
						DPHY_RX0_TESTCLR_MASK);
			udelay(100);

			/* set clock lane */
			camsys_rk3288_mipiphy0_wr_reg
				(0x34, 0x15, para);
			if (para->phy->data_en_bit >= 0x00)
				camsys_rk3288_mipiphy0_wr_reg
					(0x44, hsfreqrange, para);
			if (para->phy->data_en_bit >= 0x01)
				camsys_rk3288_mipiphy0_wr_reg(
					0x54, hsfreqrange, para);
			if (para->phy->data_en_bit >= 0x04) {
				camsys_rk3288_mipiphy0_wr_reg
					(0x84, hsfreqrange, para);
				camsys_rk3288_mipiphy0_wr_reg
					(0x94, hsfreqrange, para);
			}

			/* Normal operation */
			camsys_rk3288_mipiphy0_wr_reg(0x0, -1, para);
		    /* TESTCLK=1 */
			write_grf_reg(GRF_SOC_CON14_OFFSET,
					DPHY_RX0_TESTCLK_MASK
					| DPHY_RX0_TESTCLK);
			/* TESTEN =0 */
			write_grf_reg(GRF_SOC_CON14_OFFSET,
					(DPHY_RX0_TESTEN_MASK));
		}

		base =
			(unsigned int *)
			para->camsys_dev->devmems.registermem->vir_base;
		*(base + (MRV_MIPI_BASE + MRV_MIPI_CTRL) / 4)
			|= (0x0f << 8);

	} else if (para->phy->phy_index == 1) {
		/* 7. Set BASEDIR_N to the desired values. */
		/* Real IC route must set firstly */
		write_grf_reg(GRF_SOC_CON14_OFFSET,
					DPHY_TX1RX1_BASEDIR_REC
					| DPHY_TX1RX1_BASEDIR_OFFSET);
		/* 1'b1: MIPI PHY TX1RX1 1'b0: MIPI PHY RX0 */
		write_grf_reg(GRF_SOC_CON6_OFFSET,
					MIPI_PHY_DPHYSEL_OFFSET_MASK
					| (para->phy->phy_index
					<< MIPI_PHY_DPHYSEL_OFFSET_BIT));
		/* 1'b1: CSI host 1'b0: DSI host1 */
		write_grf_reg(GRF_SOC_CON6_OFFSET,
					DSI_CSI_TESTBUS_SEL_MASK
					| (1
					<< DSI_CSI_TESTBUS_SEL_OFFSET_BIT));
		/* 1'b1: isp 1'b0: csi host */
		write_grf_reg(GRF_SOC_CON14_OFFSET,
					DPHY_RX1_SRC_SEL_CSI
					| DPHY_RX1_SRC_SEL_MASK);
		/* 1.Set RSTZ = 1'b0 */
		write_csihost_reg(CSIHOST_DPHY_RSTZ, 0x00000000);
		/* 2. Set SHUTDOWNZ = 1'b0. */
		write_csihost_reg(CSIHOST_PHY_SHUTDOWNZ, 0x00000000);
		/* 3. Set TESTCLEAR = 1'b1. */
		write_csihost_reg(CSIHOST_PHY_TEST_CTRL0, 0x00000001);
		/* 4.Apply REFCLK signal with the appropriate frequency;  */
		/* 5. Apply CFG_CLK signal with the appropriate frequency; */
		/* 6. Set MASTERSLAVEZ = 1'b1 (for MASTER) / 1'b0 (for SLAVE). */
		write_grf_reg(GRF_SOC_CON14_OFFSET,
					DPHY_TX1RX1_SLAVEZ
					| DPHY_TX1RX1_MASTERSLAVEZ_MASK);
		/* 8. Set all REQUEST inputs to zero. */
		write_grf_reg(GRF_SOC_CON15_OFFSET,
					DPHY_TX1RX1_TURN_REQUEST_MASK
					| (0x0
					<< DPHY_TX1RX1_TURN_REQUEST_OFFSET_BITS));
		/* MIPI DPHY TX1RX1 disable turn around control */
		write_grf_reg(GRF_SOC_CON9_OFFSET,
					DPHY_TX1RX1_TURN_DISABLE_MASK
					| (0xf
					<< DPHY_TX1RX1_TURN_DISABLE_OFFSET_BITS));
		/* MIPI DPHY TX1RX1 force lane into receive mode */
		/* wait for stop sta */
		write_grf_reg(GRF_SOC_CON9_OFFSET,
					(DPHY_TX1RX1_FORCE_RX_MODE_OFFSET_BITS
					<< 4)
					| DPHY_TX1RX1_FORCE_RX_MODE_MASK);
		/* 9. Wait for 15 ns. */
		udelay(1);
		/* 10. Set TESTCLR to low. */
		write_csihost_reg(CSIHOST_PHY_TEST_CTRL0, 0x00000002);
		/* 11. Wait for 15 ns. */
		udelay(1);
		camsys_rk3288_mipiphy1_wr_reg(phy_virt, 0x34, 0x15);
		/* 12. Configure Test Code 0x44 hsfreqrange. */
		camsys_rk3288_mipiphy1_wr_reg(phy_virt, 0x44, hsfreqrange);
		camsys_rk3288_mipiphy1_rd_reg(phy_virt, 0x0);
		write_csihost_reg(CSIHOST_PHY_TEST_CTRL0, 0x00000002);
		write_csihost_reg(CSIHOST_PHY_TEST_CTRL1, 0x00000000);
		/* 15.Set ENABLE_N=1'b1. */
		data_en_bit = para->phy->data_en_bit;
		data_en_bit = data_en_bit >> 1;
		data_en_num = 0;
		while (data_en_bit) {
			data_en_num++;
			data_en_bit = data_en_bit >> 1;
		}
		write_csihost_reg(CSIHOST_N_LANES, data_en_num);
		write_grf_reg(GRF_SOC_CON14_OFFSET,
					DPHY_TX1RX1_ENABLECLK
					| DPHY_TX1RX1_ENABLECLK_MASK);
		/* 16. Wait for 5 ns. */
		udelay(1);
		/* 17. Set SHUTDOWNZ = 1'b1. */
		write_csihost_reg(CSIHOST_PHY_SHUTDOWNZ, 0x00000001);
		/* 18. Wait for 5 ns. */
		udelay(1);
		/* 19.Set RSTZ = 1'b1 */
		write_csihost_reg
			(CSIHOST_DPHY_RSTZ, 0x00000001);
	} else {
		camsys_err("mipi phy index %d is invalidate!",
			para->phy->phy_index);
		goto fail;
}

	camsys_trace(1, "mipi phy(%d) turn on(lane: 0x%x  bit_rate: %dMbps)",
			para->phy->phy_index,
			para->phy->data_en_bit,
			para->phy->bit_rate);

	return 0;

fail:
	return -1;
}

#define MRV_AFM_BASE    0x0000
#define VI_IRCL         0x0014

int camsys_rk3288_cfg(
camsys_dev_t *camsys_dev, camsys_soc_cfg_t cfg_cmd, void *cfg_para)
{
	unsigned int *para_int;

	switch (cfg_cmd) {
	case Clk_DriverStrength_Cfg: {
		para_int = (unsigned int *)cfg_para;
		__raw_writel((((*para_int) & 0x03) << 3)|(0x03 << 3),
			(void *)(camsys_dev->rk_grf_base + 0x01d4));
		break;
	}

	case Cif_IoDomain_Cfg: {
		para_int = (unsigned int *)cfg_para;
		if (*para_int < 28000000) {
			/* 1.8v IO */
			__raw_writel
				(((1 << 1) | (1 << (1 + 16))),
				(void *)(camsys_dev->rk_grf_base + 0x0380));
		} else {
			/* 3.3v IO */
			__raw_writel
				(((0 << 1) | (1 << (1 + 16))),
				(void *)(camsys_dev->rk_grf_base + 0x0380));
		}
		break;
	}

	case Mipi_Phy_Cfg: {
		camsys_mipiphy_soc_para_t *para
			= (camsys_mipiphy_soc_para_t *)cfg_para;

		if (para->phy->dir == CamSys_Mipiphy_Tx &&
			para->phy->phy_index == 1) {
			/* TX1/RX1 DPHY switch to RX status */
			__raw_writel(0xa000a000,
				(void *)(camsys_dev->rk_grf_base + 0x027c));
		} else {
			camsys_rk3288_mipihpy_cfg
				((camsys_mipiphy_soc_para_t *)cfg_para);
		}
		break;
	}

	case Isp_SoftRst: {        /* ddl@rock-chips.com: v0.d.0 */
		unsigned int reset;

		reset = (unsigned int)cfg_para;
		if (reset == 1)
			__raw_writel(0x80, (void *)(camsys_dev->rk_isp_base +
				MRV_AFM_BASE + VI_IRCL));
		else
			__raw_writel(0x00, (void *)(camsys_dev->rk_isp_base +
				MRV_AFM_BASE + VI_IRCL));
		camsys_trace(2, "Isp self soft rst: %d", reset);
		break;
	}

	default: {
		camsys_warn("cfg_cmd: 0x%x isn't support", cfg_cmd);
		break;
	}
	}

	return 0;
}
#endif /* CONFIG_ARM  */
