#ifdef CONFIG_ARM64
#include "camsys_soc_priv.h"
#include "camsys_soc_rk3399.h"

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
	{800, 1000, 0x10},
	{1000, 1200, 0x11},
	{1200, 1400, 0x12},
	{1400, 1600, 0x13},
	{1600, 1800, 0x14}
};

#if 0
static int camsys_rk3399_mipiphy0_rd_reg(unsigned char addr)
{
    return read_grf_reg(0x0e2a4);
}
#endif

static int camsys_rk3399_mipiphy0_wr_reg
(camsys_mipiphy_soc_para_t *para, unsigned char addr, unsigned char data)
{
	/*TESTCLK=1*/
	write_grf_reg(GRF_SOC_CON25_OFFSET, DPHY_RX0_TESTCLK_MASK |
				(1 << DPHY_RX0_TESTCLK_BIT));
	/*TESTEN =1,TESTDIN=addr*/
	write_grf_reg(GRF_SOC_CON25_OFFSET,
				((addr << DPHY_RX0_TESTDIN_BIT) |
				DPHY_RX0_TESTDIN_MASK |
				(1 << DPHY_RX0_TESTEN_BIT) |
				DPHY_RX0_TESTEN_MASK));
	/*TESTCLK=0*/
	write_grf_reg(GRF_SOC_CON25_OFFSET, DPHY_RX0_TESTCLK_MASK);

	if (data != 0xff) { /*write data ?*/
		/*TESTEN =0,TESTDIN=data*/
		write_grf_reg(GRF_SOC_CON25_OFFSET,
					((data << DPHY_RX0_TESTDIN_BIT) |
					DPHY_RX0_TESTDIN_MASK |
					DPHY_RX0_TESTEN_MASK));

		/*TESTCLK=1*/
		write_grf_reg(GRF_SOC_CON25_OFFSET, DPHY_RX0_TESTCLK_MASK |
					(1 << DPHY_RX0_TESTCLK_BIT));
	}
	return 0;
}

static int camsys_rk3399_mipiphy1_wr_reg
(unsigned long dsiphy_virt, unsigned char addr, unsigned char data)
{
	/*TESTEN =1,TESTDIN=addr*/
	write_dsihost_reg(DSIHOST_PHY_TEST_CTRL1, (0x00010000 | addr));
	/*TESTCLK=0*/
	write_dsihost_reg(DSIHOST_PHY_TEST_CTRL0, 0x00000000);
	/*TESTEN =0,TESTDIN=data*/
	write_dsihost_reg(DSIHOST_PHY_TEST_CTRL1, (0x00000000 | data));
	/*TESTCLK=1 */
	write_dsihost_reg(DSIHOST_PHY_TEST_CTRL0, 0x00000002);

	return 0;
}
/*
static int camsys_rk3399_mipiphy1_rd_reg
(unsigned long dsiphy_virt,unsigned char addr)
{
    return (read_dsihost_reg(((DSIHOST_PHY_TEST_CTRL1)&0xff00))>>8);
}*/

static int camsys_rk3399_mipihpy_cfg
(camsys_mipiphy_soc_para_t *para)
{
	unsigned char hsfreqrange = 0xff, i;
	struct mipiphy_hsfreqrange_s *hsfreqrange_p;
	unsigned long phy_virt, phy_index;
	unsigned long base;
	unsigned long csiphy_virt;
	unsigned long dsiphy_virt;
	unsigned long vir_base = 0;

	phy_index = para->phy->phy_index;
	if (para->camsys_dev->mipiphy[phy_index].reg != NULL) {
		phy_virt  = para->camsys_dev->mipiphy[phy_index].reg->vir_base;
	} else {
		phy_virt = 0x00;
	}
	if (para->camsys_dev->csiphy_reg != NULL) {
		csiphy_virt =
			(unsigned long)para->camsys_dev->csiphy_reg->vir_base;
	} else {
		csiphy_virt = 0x00;
	}
	if (para->camsys_dev->dsiphy_reg != NULL) {
		dsiphy_virt =
			(unsigned long)para->camsys_dev->dsiphy_reg->vir_base;
	} else {
		dsiphy_virt = 0x00;
	}

	if ((para->phy->bit_rate == 0) ||
		(para->phy->data_en_bit == 0)) {
		if (para->phy->phy_index == 0) {
			base =
			(para->camsys_dev->devmems.registermem->vir_base);
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
	hsfreqrange <<= 1;
	if (para->phy->phy_index == 0) {
		if (strstr(para->camsys_dev->miscdev.name, "camsys_marvin1")) {
			camsys_err("miscdev.name = %s,mipi phy index %d is invalidate\n",
				para->camsys_dev->miscdev.name,
				para->phy->phy_index);
			goto fail;
		}

		write_grf_reg(GRF_SOC_CON21_OFFSET,
					DPHY_RX0_FORCERXMODE_MASK |
					(0x0 << DPHY_RX0_FORCERXMODE_BIT) |
					DPHY_RX0_FORCETXSTOPMODE_MASK |
					(0x0 << DPHY_RX0_FORCETXSTOPMODE_BIT));

		/*  set lane num*/
		write_grf_reg(GRF_SOC_CON21_OFFSET,
			DPHY_RX0_ENABLE_MASK |
			(para->phy->data_en_bit << DPHY_RX0_ENABLE_BIT));

		/*  set lan turndisab as 1*/
		write_grf_reg(GRF_SOC_CON21_OFFSET,
			DPHY_RX0_TURNDISABLE_MASK |
			(0xf << DPHY_RX0_TURNDISABLE_BIT));
		write_grf_reg(GRF_SOC_CON21_OFFSET, (0x0<<4) | (0xf<<20));

		/*  set lan turnrequest as 0 */
		write_grf_reg(GRF_SOC_CON9_OFFSET,
			DPHY_RX0_TURNREQUEST_MASK |
			(0x0 << DPHY_RX0_TURNREQUEST_BIT));

		/*phy start*/
		{
			write_grf_reg(GRF_SOC_CON25_OFFSET,
				DPHY_RX0_TESTCLK_MASK |
				(0x1 << DPHY_RX0_TESTCLK_BIT)); /*TESTCLK=1 */
			write_grf_reg(GRF_SOC_CON25_OFFSET,
				DPHY_RX0_TESTCLR_MASK |
				(0x1 << DPHY_RX0_TESTCLR_BIT));   /*TESTCLR=1*/
			udelay(100);
			/*TESTCLR=0  zyc*/
			write_grf_reg(GRF_SOC_CON25_OFFSET,
				DPHY_RX0_TESTCLR_MASK);
			udelay(100);

			/*set clock lane*/
			camsys_rk3399_mipiphy0_wr_reg
				(para, 0x34, 0x15);

			if (para->phy->data_en_bit >= 0x00) {
				camsys_rk3399_mipiphy0_wr_reg
					(para, 0x44, hsfreqrange);
			}
			if (para->phy->data_en_bit >= 0x01)
				camsys_rk3399_mipiphy0_wr_reg
				(para, 0x54, hsfreqrange);
			if (para->phy->data_en_bit >= 0x04) {
				camsys_rk3399_mipiphy0_wr_reg
					(para, 0x84, hsfreqrange);
				camsys_rk3399_mipiphy0_wr_reg
					(para, 0x94, hsfreqrange);
			}
			/*Normal operation*/
			camsys_rk3399_mipiphy0_wr_reg(para, 0x0, -1);
			write_grf_reg(GRF_SOC_CON25_OFFSET,
				DPHY_RX0_TESTCLK_MASK |
				(1 << DPHY_RX0_TESTCLK_BIT));    /*TESTCLK=1*/
			/*TESTEN =0 */
			write_grf_reg(GRF_SOC_CON25_OFFSET,
				(DPHY_RX0_TESTEN_MASK));
		}

		base = (para->camsys_dev->devmems.registermem->vir_base);
		*((unsigned int *)(base + (MRV_MIPI_BASE+MRV_MIPI_CTRL))) |=
			(0x0f<<8);

    } else if (para->phy->phy_index == 1) {

		if (!strstr(para->camsys_dev->miscdev.name, "camsys_marvin1")) {
			camsys_err
				("miscdev.name = %s,mipi phy index %d is invalidate\n",
				para->camsys_dev->miscdev.name,
				para->phy->phy_index);
			goto fail;
		}

		write_grf_reg(GRF_SOC_CON23_OFFSET,
			DPHY_RX0_FORCERXMODE_MASK |
			(0x0 << DPHY_RX0_FORCERXMODE_BIT) |
			DPHY_RX0_FORCETXSTOPMODE_MASK |
			(0x0 << DPHY_RX0_FORCETXSTOPMODE_BIT));
		write_grf_reg(GRF_SOC_CON24_OFFSET,
			DPHY_TX1RX1_MASTERSLAVEZ_MASK |
			(0x0 << DPHY_TX1RX1_MASTERSLAVEZ_BIT) |
			DPHY_TX1RX1_BASEDIR_MASK |
			(0x1 << DPHY_TX1RX1_BASEDIR_BIT) |
			DPHY_RX1_MASK | 0x0 << DPHY_RX1_SEL_BIT);


		/*	set lane num*/
		write_grf_reg(GRF_SOC_CON23_OFFSET,
			DPHY_TX1RX1_ENABLE_MASK |
			(para->phy->data_en_bit << DPHY_TX1RX1_ENABLE_BIT));

		/*	set lan turndisab as 1*/
		write_grf_reg(GRF_SOC_CON23_OFFSET,
			DPHY_TX1RX1_TURNDISABLE_MASK |
			(0xf << DPHY_TX1RX1_TURNDISABLE_BIT));
		write_grf_reg(GRF_SOC_CON23_OFFSET, (0x0<<4)|(0xf<<20));

		/*	set lan turnrequest as 0*/
		write_grf_reg(GRF_SOC_CON24_OFFSET,
			DPHY_TX1RX1_TURNREQUEST_MASK |
			(0x0 << DPHY_TX1RX1_TURNREQUEST_BIT));
		/*phy1 start*/
		{
			int res_val = 0;
			res_val = read_dsihost_reg(DSIHOST_PHY_SHUTDOWNZ);
			res_val &= 0xfffffffe;
			/*SHUTDOWNZ=0*/
			write_dsihost_reg(DSIHOST_PHY_SHUTDOWNZ, res_val);

			vir_base = (unsigned long)ioremap(0xff910000, 0x10000);
			/*__raw_writel(0x60000, (void*)(0x1c00+vir_base));*/

			res_val = 0;
			res_val = read_dsihost_reg(DSIHOST_DPHY_RSTZ);
			res_val &= 0xfffffffd;
			/*RSTZ=0*/
			write_dsihost_reg(DSIHOST_DPHY_RSTZ, res_val);
			/*TESTCLK=1*/
			write_dsihost_reg(DSIHOST_PHY_TEST_CTRL0, 0x00000002);
			/*TESTCLR=1 TESTCLK=1 */
			write_dsihost_reg(DSIHOST_PHY_TEST_CTRL0, 0x00000003);
			udelay(100);
			/*TESTCLR=0 TESTCLK=1*/
			write_dsihost_reg(DSIHOST_PHY_TEST_CTRL0, 0x00000002);
			udelay(100);

			/*set clock lane*/
			camsys_rk3399_mipiphy1_wr_reg
				(dsiphy_virt, 0x34, 0x15);
			if (para->phy->data_en_bit >= 0x00)
				camsys_rk3399_mipiphy1_wr_reg
				(dsiphy_virt, 0x44, hsfreqrange);
			if (para->phy->data_en_bit >= 0x01)
				camsys_rk3399_mipiphy1_wr_reg
				(dsiphy_virt, 0x54, hsfreqrange);
			if (para->phy->data_en_bit >= 0x04) {
				camsys_rk3399_mipiphy1_wr_reg
					(dsiphy_virt, 0x84, hsfreqrange);
				camsys_rk3399_mipiphy1_wr_reg
					(dsiphy_virt, 0x94, hsfreqrange);
			}

			/*camsys_rk3399_mipiphy1_rd_reg(dsiphy_virt,0x0);*/
			/*TESTCLK=1*/
			write_dsihost_reg(DSIHOST_PHY_TEST_CTRL0, 0x00000002);
			/*TESTEN =0*/
			write_dsihost_reg(DSIHOST_PHY_TEST_CTRL1, 0x00000000);
			/*SHUTDOWNZ=1*/
			write_dsihost_reg(DSIHOST_PHY_SHUTDOWNZ, 0x00000001);
			/*__raw_writel(0x60f00, (void*)(0x1c00+vir_base));*/
			/*RSTZ=1*/
			write_dsihost_reg(DSIHOST_DPHY_RSTZ, 0x00000001);
		}

	} else {
		camsys_err("mipi phy index %d is invalidate!",
			para->phy->phy_index);
		goto fail;
	}

	camsys_trace(1, "mipi phy(%d) turn on(lane: 0x%x  bit_rate: %dMbps)",
		para->phy->phy_index, para->phy->data_en_bit,
		para->phy->bit_rate);

	return 0;

fail:
	return -1;
}

#define MRV_AFM_BASE		0x0000
#define VI_IRCL			0x0014
int camsys_rk3399_cfg
(camsys_dev_t *camsys_dev, camsys_soc_cfg_t cfg_cmd, void *cfg_para)
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
		camsys_rk3399_mipihpy_cfg
			((camsys_mipiphy_soc_para_t *)cfg_para);
		break;
	}

	case Isp_SoftRst: /* ddl@rock-chips.com: v0.d.0 */ {
		unsigned long reset;
		reset = (unsigned long)cfg_para;

		if (reset == 1)
			__raw_writel(0x80, (void *)(camsys_dev->rk_isp_base +
			MRV_AFM_BASE + VI_IRCL));
		else
			__raw_writel(0x00, (void *)(camsys_dev->rk_isp_base +
			MRV_AFM_BASE + VI_IRCL));
		break;
	}

	default: {
		camsys_warn("cfg_cmd: 0x%x isn't support", cfg_cmd);
		break;
	}

	}

	return 0;
}
#endif /* CONFIG_ARM64 */
