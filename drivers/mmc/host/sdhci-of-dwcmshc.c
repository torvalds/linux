// SPDX-License-Identifier: GPL-2.0
/*
 * Driver for Synopsys DesignWare Cores Mobile Storage Host Controller
 *
 * Copyright (C) 2018 Synaptics Incorporated
 *
 * Author: Jisheng Zhang <jszhang@kernel.org>
 */

#include <linux/acpi.h>
#include <linux/bitfield.h>
#include <linux/clk.h>
#include <linux/dma-mapping.h>
#include <linux/iopoll.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/pm_runtime.h>
#include <linux/reset.h>
#include <linux/sizes.h>

#include "sdhci-pltfm.h"

#define SDHCI_DWCMSHC_ARG2_STUFF	GENMASK(31, 16)

/* DWCMSHC specific Mode Select value */
#define DWCMSHC_CTRL_HS400		0x7

/* DWC IP vendor area 1 pointer */
#define DWCMSHC_P_VENDOR_AREA1		0xe8
#define DWCMSHC_AREA1_MASK		GENMASK(11, 0)
/* Offset inside the  vendor area 1 */
#define DWCMSHC_HOST_CTRL3		0x8
#define DWCMSHC_EMMC_CONTROL		0x2c
#define DWCMSHC_CARD_IS_EMMC		BIT(0)
#define DWCMSHC_ENHANCED_STROBE		BIT(8)
#define DWCMSHC_EMMC_ATCTRL		0x40
/* Tuning and auto-tuning fields in AT_CTRL_R control register */
#define AT_CTRL_AT_EN			BIT(0) /* autotuning is enabled */
#define AT_CTRL_CI_SEL			BIT(1) /* interval to drive center phase select */
#define AT_CTRL_SWIN_TH_EN		BIT(2) /* sampling window threshold enable */
#define AT_CTRL_RPT_TUNE_ERR		BIT(3) /* enable reporting framing errors */
#define AT_CTRL_SW_TUNE_EN		BIT(4) /* enable software managed tuning */
#define AT_CTRL_WIN_EDGE_SEL_MASK	GENMASK(11, 8) /* bits [11:8] */
#define AT_CTRL_WIN_EDGE_SEL		0xf /* sampling window edge select */
#define AT_CTRL_TUNE_CLK_STOP_EN	BIT(16) /* clocks stopped during phase code change */
#define AT_CTRL_PRE_CHANGE_DLY_MASK	GENMASK(18, 17) /* bits [18:17] */
#define AT_CTRL_PRE_CHANGE_DLY		0x1  /* 2-cycle latency */
#define AT_CTRL_POST_CHANGE_DLY_MASK	GENMASK(20, 19) /* bits [20:19] */
#define AT_CTRL_POST_CHANGE_DLY		0x3  /* 4-cycle latency */
#define AT_CTRL_SWIN_TH_VAL_MASK	GENMASK(31, 24) /* bits [31:24] */
#define AT_CTRL_SWIN_TH_VAL		0x9  /* sampling window threshold */

/* Sophgo CV18XX specific Registers */
#define CV18XX_SDHCI_MSHC_CTRL			0x00
#define  CV18XX_EMMC_FUNC_EN			BIT(0)
#define  CV18XX_LATANCY_1T			BIT(1)
#define CV18XX_SDHCI_PHY_TX_RX_DLY		0x40
#define  CV18XX_PHY_TX_DLY_MSK			GENMASK(6, 0)
#define  CV18XX_PHY_TX_SRC_MSK			GENMASK(9, 8)
#define  CV18XX_PHY_TX_SRC_INVERT_CLK_TX	0x1
#define  CV18XX_PHY_RX_DLY_MSK			GENMASK(22, 16)
#define  CV18XX_PHY_RX_SRC_MSK			GENMASK(25, 24)
#define  CV18XX_PHY_RX_SRC_INVERT_RX_CLK	0x1
#define CV18XX_SDHCI_PHY_CONFIG			0x4c
#define  CV18XX_PHY_TX_BPS			BIT(0)

/* Rockchip specific Registers */
#define DWCMSHC_EMMC_DLL_CTRL		0x800
#define DWCMSHC_EMMC_DLL_RXCLK		0x804
#define DWCMSHC_EMMC_DLL_TXCLK		0x808
#define DWCMSHC_EMMC_DLL_STRBIN		0x80c
#define DECMSHC_EMMC_DLL_CMDOUT		0x810
#define DWCMSHC_EMMC_DLL_STATUS0	0x840
#define DWCMSHC_EMMC_DLL_START		BIT(0)
#define DWCMSHC_EMMC_DLL_LOCKED		BIT(8)
#define DWCMSHC_EMMC_DLL_TIMEOUT	BIT(9)
#define DWCMSHC_EMMC_DLL_RXCLK_SRCSEL	29
#define DWCMSHC_EMMC_DLL_START_POINT	16
#define DWCMSHC_EMMC_DLL_INC		8
#define DWCMSHC_EMMC_DLL_BYPASS		BIT(24)
#define DWCMSHC_EMMC_DLL_DLYENA		BIT(27)
#define DLL_TXCLK_TAPNUM_DEFAULT	0x10
#define DLL_TXCLK_TAPNUM_90_DEGREES	0xA
#define DLL_TXCLK_TAPNUM_FROM_SW	BIT(24)
#define DLL_STRBIN_TAPNUM_DEFAULT	0x8
#define DLL_STRBIN_TAPNUM_FROM_SW	BIT(24)
#define DLL_STRBIN_DELAY_NUM_SEL	BIT(26)
#define DLL_STRBIN_DELAY_NUM_OFFSET	16
#define DLL_STRBIN_DELAY_NUM_DEFAULT	0x16
#define DLL_RXCLK_NO_INVERTER		1
#define DLL_RXCLK_INVERTER		0
#define DLL_CMDOUT_TAPNUM_90_DEGREES	0x8
#define DLL_RXCLK_ORI_GATE		BIT(31)
#define DLL_CMDOUT_TAPNUM_FROM_SW	BIT(24)
#define DLL_CMDOUT_SRC_CLK_NEG		BIT(28)
#define DLL_CMDOUT_EN_SRC_CLK_NEG	BIT(29)

#define DLL_LOCK_WO_TMOUT(x) \
	((((x) & DWCMSHC_EMMC_DLL_LOCKED) == DWCMSHC_EMMC_DLL_LOCKED) && \
	(((x) & DWCMSHC_EMMC_DLL_TIMEOUT) == 0))
#define RK35xx_MAX_CLKS 3

/* PHY register area pointer */
#define DWC_MSHC_PTR_PHY_R	0x300

/* PHY general configuration */
#define PHY_CNFG_R		(DWC_MSHC_PTR_PHY_R + 0x00)
#define PHY_CNFG_RSTN_DEASSERT	0x1  /* Deassert PHY reset */
#define PHY_CNFG_PAD_SP_MASK	GENMASK(19, 16) /* bits [19:16] */
#define PHY_CNFG_PAD_SP		0x0c /* PMOS TX drive strength */
#define PHY_CNFG_PAD_SN_MASK	GENMASK(23, 20) /* bits [23:20] */
#define PHY_CNFG_PAD_SN		0x0c /* NMOS TX drive strength */

/* PHY command/response pad settings */
#define PHY_CMDPAD_CNFG_R	(DWC_MSHC_PTR_PHY_R + 0x04)

/* PHY data pad settings */
#define PHY_DATAPAD_CNFG_R	(DWC_MSHC_PTR_PHY_R + 0x06)

/* PHY clock pad settings */
#define PHY_CLKPAD_CNFG_R	(DWC_MSHC_PTR_PHY_R + 0x08)

/* PHY strobe pad settings */
#define PHY_STBPAD_CNFG_R	(DWC_MSHC_PTR_PHY_R + 0x0a)

/* PHY reset pad settings */
#define PHY_RSTNPAD_CNFG_R	(DWC_MSHC_PTR_PHY_R + 0x0c)

/* Bitfields are common for all pad settings */
#define PHY_PAD_RXSEL_1V8		0x1 /* Receiver type select for 1.8V */
#define PHY_PAD_RXSEL_3V3		0x2 /* Receiver type select for 3.3V */

#define PHY_PAD_WEAKPULL_MASK		GENMASK(4, 3) /* bits [4:3] */
#define PHY_PAD_WEAKPULL_PULLUP		0x1 /* Weak pull up enabled */
#define PHY_PAD_WEAKPULL_PULLDOWN	0x2 /* Weak pull down enabled */

#define PHY_PAD_TXSLEW_CTRL_P_MASK	GENMASK(8, 5) /* bits [8:5] */
#define PHY_PAD_TXSLEW_CTRL_P		0x3 /* Slew control for P-Type pad TX */
#define PHY_PAD_TXSLEW_CTRL_N_MASK	GENMASK(12, 9) /* bits [12:9] */
#define PHY_PAD_TXSLEW_CTRL_N		0x3 /* Slew control for N-Type pad TX */

/* PHY CLK delay line settings */
#define PHY_SDCLKDL_CNFG_R		(DWC_MSHC_PTR_PHY_R + 0x1d)
#define PHY_SDCLKDL_CNFG_UPDATE	BIT(4) /* set before writing to SDCLKDL_DC */

/* PHY CLK delay line delay code */
#define PHY_SDCLKDL_DC_R		(DWC_MSHC_PTR_PHY_R + 0x1e)
#define PHY_SDCLKDL_DC_INITIAL		0x40 /* initial delay code */
#define PHY_SDCLKDL_DC_DEFAULT		0x32 /* default delay code */
#define PHY_SDCLKDL_DC_HS400		0x18 /* delay code for HS400 mode */

/* PHY drift_cclk_rx delay line configuration setting */
#define PHY_ATDL_CNFG_R			(DWC_MSHC_PTR_PHY_R + 0x21)
#define PHY_ATDL_CNFG_INPSEL_MASK	GENMASK(3, 2) /* bits [3:2] */
#define PHY_ATDL_CNFG_INPSEL		0x3 /* delay line input source */

/* PHY DLL control settings */
#define PHY_DLL_CTRL_R			(DWC_MSHC_PTR_PHY_R + 0x24)
#define PHY_DLL_CTRL_DISABLE		0x0 /* PHY DLL is enabled */
#define PHY_DLL_CTRL_ENABLE		0x1 /* PHY DLL is disabled */

/* PHY DLL  configuration register 1 */
#define PHY_DLL_CNFG1_R			(DWC_MSHC_PTR_PHY_R + 0x25)
#define PHY_DLL_CNFG1_SLVDLY_MASK	GENMASK(5, 4) /* bits [5:4] */
#define PHY_DLL_CNFG1_SLVDLY		0x2 /* DLL slave update delay input */
#define PHY_DLL_CNFG1_WAITCYCLE		0x5 /* DLL wait cycle input */

/* PHY DLL configuration register 2 */
#define PHY_DLL_CNFG2_R			(DWC_MSHC_PTR_PHY_R + 0x26)
#define PHY_DLL_CNFG2_JUMPSTEP		0xa /* DLL jump step input */

/* PHY DLL master and slave delay line configuration settings */
#define PHY_DLLDL_CNFG_R		(DWC_MSHC_PTR_PHY_R + 0x28)
#define PHY_DLLDL_CNFG_SLV_INPSEL_MASK	GENMASK(6, 5) /* bits [6:5] */
#define PHY_DLLDL_CNFG_SLV_INPSEL	0x3 /* clock source select for slave DL */

#define FLAG_IO_FIXED_1V8	BIT(0)

#define BOUNDARY_OK(addr, len) \
	((addr | (SZ_128M - 1)) == ((addr + len - 1) | (SZ_128M - 1)))

enum dwcmshc_rk_type {
	DWCMSHC_RK3568,
	DWCMSHC_RK3588,
};

struct rk35xx_priv {
	/* Rockchip specified optional clocks */
	struct clk_bulk_data rockchip_clks[RK35xx_MAX_CLKS];
	struct reset_control *reset;
	enum dwcmshc_rk_type devtype;
	u8 txclk_tapnum;
};

struct dwcmshc_priv {
	struct clk	*bus_clk;
	int vendor_specific_area1; /* P_VENDOR_SPECIFIC_AREA reg */
	void *priv; /* pointer to SoC private stuff */
	u16 delay_line;
	u16 flags;
};

/*
 * If DMA addr spans 128MB boundary, we split the DMA transfer into two
 * so that each DMA transfer doesn't exceed the boundary.
 */
static void dwcmshc_adma_write_desc(struct sdhci_host *host, void **desc,
				    dma_addr_t addr, int len, unsigned int cmd)
{
	int tmplen, offset;

	if (likely(!len || BOUNDARY_OK(addr, len))) {
		sdhci_adma_write_desc(host, desc, addr, len, cmd);
		return;
	}

	offset = addr & (SZ_128M - 1);
	tmplen = SZ_128M - offset;
	sdhci_adma_write_desc(host, desc, addr, tmplen, cmd);

	addr += tmplen;
	len -= tmplen;
	sdhci_adma_write_desc(host, desc, addr, len, cmd);
}

static unsigned int dwcmshc_get_max_clock(struct sdhci_host *host)
{
	struct sdhci_pltfm_host *pltfm_host = sdhci_priv(host);

	if (pltfm_host->clk)
		return sdhci_pltfm_clk_get_max_clock(host);
	else
		return pltfm_host->clock;
}

static unsigned int rk35xx_get_max_clock(struct sdhci_host *host)
{
	struct sdhci_pltfm_host *pltfm_host = sdhci_priv(host);

	return clk_round_rate(pltfm_host->clk, ULONG_MAX);
}

static void dwcmshc_check_auto_cmd23(struct mmc_host *mmc,
				     struct mmc_request *mrq)
{
	struct sdhci_host *host = mmc_priv(mmc);

	/*
	 * No matter V4 is enabled or not, ARGUMENT2 register is 32-bit
	 * block count register which doesn't support stuff bits of
	 * CMD23 argument on dwcmsch host controller.
	 */
	if (mrq->sbc && (mrq->sbc->arg & SDHCI_DWCMSHC_ARG2_STUFF))
		host->flags &= ~SDHCI_AUTO_CMD23;
	else
		host->flags |= SDHCI_AUTO_CMD23;
}

static void dwcmshc_request(struct mmc_host *mmc, struct mmc_request *mrq)
{
	dwcmshc_check_auto_cmd23(mmc, mrq);

	sdhci_request(mmc, mrq);
}

static void dwcmshc_phy_1_8v_init(struct sdhci_host *host)
{
	struct sdhci_pltfm_host *pltfm_host = sdhci_priv(host);
	struct dwcmshc_priv *priv = sdhci_pltfm_priv(pltfm_host);
	u32 val;

	/* deassert phy reset & set tx drive strength */
	val = PHY_CNFG_RSTN_DEASSERT;
	val |= FIELD_PREP(PHY_CNFG_PAD_SP_MASK, PHY_CNFG_PAD_SP);
	val |= FIELD_PREP(PHY_CNFG_PAD_SN_MASK, PHY_CNFG_PAD_SN);
	sdhci_writel(host, val, PHY_CNFG_R);

	/* disable delay line */
	sdhci_writeb(host, PHY_SDCLKDL_CNFG_UPDATE, PHY_SDCLKDL_CNFG_R);

	/* set delay line */
	sdhci_writeb(host, priv->delay_line, PHY_SDCLKDL_DC_R);
	sdhci_writeb(host, PHY_DLL_CNFG2_JUMPSTEP, PHY_DLL_CNFG2_R);

	/* enable delay lane */
	val = sdhci_readb(host, PHY_SDCLKDL_CNFG_R);
	val &= ~(PHY_SDCLKDL_CNFG_UPDATE);
	sdhci_writeb(host, val, PHY_SDCLKDL_CNFG_R);

	/* configure phy pads */
	val = PHY_PAD_RXSEL_1V8;
	val |= FIELD_PREP(PHY_PAD_WEAKPULL_MASK, PHY_PAD_WEAKPULL_PULLUP);
	val |= FIELD_PREP(PHY_PAD_TXSLEW_CTRL_P_MASK, PHY_PAD_TXSLEW_CTRL_P);
	val |= FIELD_PREP(PHY_PAD_TXSLEW_CTRL_N_MASK, PHY_PAD_TXSLEW_CTRL_N);
	sdhci_writew(host, val, PHY_CMDPAD_CNFG_R);
	sdhci_writew(host, val, PHY_DATAPAD_CNFG_R);
	sdhci_writew(host, val, PHY_RSTNPAD_CNFG_R);

	val = FIELD_PREP(PHY_PAD_TXSLEW_CTRL_P_MASK, PHY_PAD_TXSLEW_CTRL_P);
	val |= FIELD_PREP(PHY_PAD_TXSLEW_CTRL_N_MASK, PHY_PAD_TXSLEW_CTRL_N);
	sdhci_writew(host, val, PHY_CLKPAD_CNFG_R);

	val = PHY_PAD_RXSEL_1V8;
	val |= FIELD_PREP(PHY_PAD_WEAKPULL_MASK, PHY_PAD_WEAKPULL_PULLDOWN);
	val |= FIELD_PREP(PHY_PAD_TXSLEW_CTRL_P_MASK, PHY_PAD_TXSLEW_CTRL_P);
	val |= FIELD_PREP(PHY_PAD_TXSLEW_CTRL_N_MASK, PHY_PAD_TXSLEW_CTRL_N);
	sdhci_writew(host, val, PHY_STBPAD_CNFG_R);

	/* enable data strobe mode */
	sdhci_writeb(host, FIELD_PREP(PHY_DLLDL_CNFG_SLV_INPSEL_MASK, PHY_DLLDL_CNFG_SLV_INPSEL),
		     PHY_DLLDL_CNFG_R);

	/* enable phy dll */
	sdhci_writeb(host, PHY_DLL_CTRL_ENABLE, PHY_DLL_CTRL_R);
}

static void dwcmshc_phy_3_3v_init(struct sdhci_host *host)
{
	struct sdhci_pltfm_host *pltfm_host = sdhci_priv(host);
	struct dwcmshc_priv *priv = sdhci_pltfm_priv(pltfm_host);
	u32 val;

	/* deassert phy reset & set tx drive strength */
	val = PHY_CNFG_RSTN_DEASSERT;
	val |= FIELD_PREP(PHY_CNFG_PAD_SP_MASK, PHY_CNFG_PAD_SP);
	val |= FIELD_PREP(PHY_CNFG_PAD_SN_MASK, PHY_CNFG_PAD_SN);
	sdhci_writel(host, val, PHY_CNFG_R);

	/* disable delay line */
	sdhci_writeb(host, PHY_SDCLKDL_CNFG_UPDATE, PHY_SDCLKDL_CNFG_R);

	/* set delay line */
	sdhci_writeb(host, priv->delay_line, PHY_SDCLKDL_DC_R);
	sdhci_writeb(host, PHY_DLL_CNFG2_JUMPSTEP, PHY_DLL_CNFG2_R);

	/* enable delay lane */
	val = sdhci_readb(host, PHY_SDCLKDL_CNFG_R);
	val &= ~(PHY_SDCLKDL_CNFG_UPDATE);
	sdhci_writeb(host, val, PHY_SDCLKDL_CNFG_R);

	/* configure phy pads */
	val = PHY_PAD_RXSEL_3V3;
	val |= FIELD_PREP(PHY_PAD_WEAKPULL_MASK, PHY_PAD_WEAKPULL_PULLUP);
	val |= FIELD_PREP(PHY_PAD_TXSLEW_CTRL_P_MASK, PHY_PAD_TXSLEW_CTRL_P);
	val |= FIELD_PREP(PHY_PAD_TXSLEW_CTRL_N_MASK, PHY_PAD_TXSLEW_CTRL_N);
	sdhci_writew(host, val, PHY_CMDPAD_CNFG_R);
	sdhci_writew(host, val, PHY_DATAPAD_CNFG_R);
	sdhci_writew(host, val, PHY_RSTNPAD_CNFG_R);

	val = FIELD_PREP(PHY_PAD_TXSLEW_CTRL_P_MASK, PHY_PAD_TXSLEW_CTRL_P);
	val |= FIELD_PREP(PHY_PAD_TXSLEW_CTRL_N_MASK, PHY_PAD_TXSLEW_CTRL_N);
	sdhci_writew(host, val, PHY_CLKPAD_CNFG_R);

	val = PHY_PAD_RXSEL_3V3;
	val |= FIELD_PREP(PHY_PAD_WEAKPULL_MASK, PHY_PAD_WEAKPULL_PULLDOWN);
	val |= FIELD_PREP(PHY_PAD_TXSLEW_CTRL_P_MASK, PHY_PAD_TXSLEW_CTRL_P);
	val |= FIELD_PREP(PHY_PAD_TXSLEW_CTRL_N_MASK, PHY_PAD_TXSLEW_CTRL_N);
	sdhci_writew(host, val, PHY_STBPAD_CNFG_R);

	/* enable phy dll */
	sdhci_writeb(host, PHY_DLL_CTRL_ENABLE, PHY_DLL_CTRL_R);
}

static void th1520_sdhci_set_phy(struct sdhci_host *host)
{
	struct sdhci_pltfm_host *pltfm_host = sdhci_priv(host);
	struct dwcmshc_priv *priv = sdhci_pltfm_priv(pltfm_host);
	u32 emmc_caps = MMC_CAP2_NO_SD | MMC_CAP2_NO_SDIO;
	u16 emmc_ctrl;

	/* Before power on, set PHY configs */
	if (priv->flags & FLAG_IO_FIXED_1V8)
		dwcmshc_phy_1_8v_init(host);
	else
		dwcmshc_phy_3_3v_init(host);

	if ((host->mmc->caps2 & emmc_caps) == emmc_caps) {
		emmc_ctrl = sdhci_readw(host, priv->vendor_specific_area1 + DWCMSHC_EMMC_CONTROL);
		emmc_ctrl |= DWCMSHC_CARD_IS_EMMC;
		sdhci_writew(host, emmc_ctrl, priv->vendor_specific_area1 + DWCMSHC_EMMC_CONTROL);
	}

	sdhci_writeb(host, FIELD_PREP(PHY_DLL_CNFG1_SLVDLY_MASK, PHY_DLL_CNFG1_SLVDLY) |
		     PHY_DLL_CNFG1_WAITCYCLE, PHY_DLL_CNFG1_R);
}

static void dwcmshc_set_uhs_signaling(struct sdhci_host *host,
				      unsigned int timing)
{
	struct sdhci_pltfm_host *pltfm_host = sdhci_priv(host);
	struct dwcmshc_priv *priv = sdhci_pltfm_priv(pltfm_host);
	u16 ctrl, ctrl_2;

	ctrl_2 = sdhci_readw(host, SDHCI_HOST_CONTROL2);
	/* Select Bus Speed Mode for host */
	ctrl_2 &= ~SDHCI_CTRL_UHS_MASK;
	if ((timing == MMC_TIMING_MMC_HS200) ||
	    (timing == MMC_TIMING_UHS_SDR104))
		ctrl_2 |= SDHCI_CTRL_UHS_SDR104;
	else if (timing == MMC_TIMING_UHS_SDR12)
		ctrl_2 |= SDHCI_CTRL_UHS_SDR12;
	else if ((timing == MMC_TIMING_UHS_SDR25) ||
		 (timing == MMC_TIMING_MMC_HS))
		ctrl_2 |= SDHCI_CTRL_UHS_SDR25;
	else if (timing == MMC_TIMING_UHS_SDR50)
		ctrl_2 |= SDHCI_CTRL_UHS_SDR50;
	else if ((timing == MMC_TIMING_UHS_DDR50) ||
		 (timing == MMC_TIMING_MMC_DDR52))
		ctrl_2 |= SDHCI_CTRL_UHS_DDR50;
	else if (timing == MMC_TIMING_MMC_HS400) {
		/* set CARD_IS_EMMC bit to enable Data Strobe for HS400 */
		ctrl = sdhci_readw(host, priv->vendor_specific_area1 + DWCMSHC_EMMC_CONTROL);
		ctrl |= DWCMSHC_CARD_IS_EMMC;
		sdhci_writew(host, ctrl, priv->vendor_specific_area1 + DWCMSHC_EMMC_CONTROL);

		ctrl_2 |= DWCMSHC_CTRL_HS400;
	}

	if (priv->flags & FLAG_IO_FIXED_1V8)
		ctrl_2 |= SDHCI_CTRL_VDD_180;
	sdhci_writew(host, ctrl_2, SDHCI_HOST_CONTROL2);
}

static void th1520_set_uhs_signaling(struct sdhci_host *host,
				     unsigned int timing)
{
	struct sdhci_pltfm_host *pltfm_host = sdhci_priv(host);
	struct dwcmshc_priv *priv = sdhci_pltfm_priv(pltfm_host);

	dwcmshc_set_uhs_signaling(host, timing);
	if (timing == MMC_TIMING_MMC_HS400)
		priv->delay_line = PHY_SDCLKDL_DC_HS400;
	else
		sdhci_writeb(host, 0, PHY_DLLDL_CNFG_R);
	th1520_sdhci_set_phy(host);
}

static void dwcmshc_hs400_enhanced_strobe(struct mmc_host *mmc,
					  struct mmc_ios *ios)
{
	u32 vendor;
	struct sdhci_host *host = mmc_priv(mmc);
	struct sdhci_pltfm_host *pltfm_host = sdhci_priv(host);
	struct dwcmshc_priv *priv = sdhci_pltfm_priv(pltfm_host);
	int reg = priv->vendor_specific_area1 + DWCMSHC_EMMC_CONTROL;

	vendor = sdhci_readl(host, reg);
	if (ios->enhanced_strobe)
		vendor |= DWCMSHC_ENHANCED_STROBE;
	else
		vendor &= ~DWCMSHC_ENHANCED_STROBE;

	sdhci_writel(host, vendor, reg);
}

static void dwcmshc_rk3568_set_clock(struct sdhci_host *host, unsigned int clock)
{
	struct sdhci_pltfm_host *pltfm_host = sdhci_priv(host);
	struct dwcmshc_priv *dwc_priv = sdhci_pltfm_priv(pltfm_host);
	struct rk35xx_priv *priv = dwc_priv->priv;
	u8 txclk_tapnum = DLL_TXCLK_TAPNUM_DEFAULT;
	u32 extra, reg;
	int err;

	host->mmc->actual_clock = 0;

	if (clock == 0) {
		/* Disable interface clock at initial state. */
		sdhci_set_clock(host, clock);
		return;
	}

	/* Rockchip platform only support 375KHz for identify mode */
	if (clock <= 400000)
		clock = 375000;

	err = clk_set_rate(pltfm_host->clk, clock);
	if (err)
		dev_err(mmc_dev(host->mmc), "fail to set clock %d", clock);

	sdhci_set_clock(host, clock);

	/* Disable cmd conflict check */
	reg = dwc_priv->vendor_specific_area1 + DWCMSHC_HOST_CTRL3;
	extra = sdhci_readl(host, reg);
	extra &= ~BIT(0);
	sdhci_writel(host, extra, reg);

	if (clock <= 52000000) {
		/*
		 * Disable DLL and reset both of sample and drive clock.
		 * The bypass bit and start bit need to be set if DLL is not locked.
		 */
		sdhci_writel(host, DWCMSHC_EMMC_DLL_BYPASS | DWCMSHC_EMMC_DLL_START, DWCMSHC_EMMC_DLL_CTRL);
		sdhci_writel(host, DLL_RXCLK_ORI_GATE, DWCMSHC_EMMC_DLL_RXCLK);
		sdhci_writel(host, 0, DWCMSHC_EMMC_DLL_TXCLK);
		sdhci_writel(host, 0, DECMSHC_EMMC_DLL_CMDOUT);
		/*
		 * Before switching to hs400es mode, the driver will enable
		 * enhanced strobe first. PHY needs to configure the parameters
		 * of enhanced strobe first.
		 */
		extra = DWCMSHC_EMMC_DLL_DLYENA |
			DLL_STRBIN_DELAY_NUM_SEL |
			DLL_STRBIN_DELAY_NUM_DEFAULT << DLL_STRBIN_DELAY_NUM_OFFSET;
		sdhci_writel(host, extra, DWCMSHC_EMMC_DLL_STRBIN);
		return;
	}

	/* Reset DLL */
	sdhci_writel(host, BIT(1), DWCMSHC_EMMC_DLL_CTRL);
	udelay(1);
	sdhci_writel(host, 0x0, DWCMSHC_EMMC_DLL_CTRL);

	/*
	 * We shouldn't set DLL_RXCLK_NO_INVERTER for identify mode but
	 * we must set it in higher speed mode.
	 */
	extra = DWCMSHC_EMMC_DLL_DLYENA;
	if (priv->devtype == DWCMSHC_RK3568)
		extra |= DLL_RXCLK_NO_INVERTER << DWCMSHC_EMMC_DLL_RXCLK_SRCSEL;
	sdhci_writel(host, extra, DWCMSHC_EMMC_DLL_RXCLK);

	/* Init DLL settings */
	extra = 0x5 << DWCMSHC_EMMC_DLL_START_POINT |
		0x2 << DWCMSHC_EMMC_DLL_INC |
		DWCMSHC_EMMC_DLL_START;
	sdhci_writel(host, extra, DWCMSHC_EMMC_DLL_CTRL);
	err = readl_poll_timeout(host->ioaddr + DWCMSHC_EMMC_DLL_STATUS0,
				 extra, DLL_LOCK_WO_TMOUT(extra), 1,
				 500 * USEC_PER_MSEC);
	if (err) {
		dev_err(mmc_dev(host->mmc), "DLL lock timeout!\n");
		return;
	}

	extra = 0x1 << 16 | /* tune clock stop en */
		0x3 << 17 | /* pre-change delay */
		0x3 << 19;  /* post-change delay */
	sdhci_writel(host, extra, dwc_priv->vendor_specific_area1 + DWCMSHC_EMMC_ATCTRL);

	if (host->mmc->ios.timing == MMC_TIMING_MMC_HS200 ||
	    host->mmc->ios.timing == MMC_TIMING_MMC_HS400)
		txclk_tapnum = priv->txclk_tapnum;

	if ((priv->devtype == DWCMSHC_RK3588) && host->mmc->ios.timing == MMC_TIMING_MMC_HS400) {
		txclk_tapnum = DLL_TXCLK_TAPNUM_90_DEGREES;

		extra = DLL_CMDOUT_SRC_CLK_NEG |
			DLL_CMDOUT_EN_SRC_CLK_NEG |
			DWCMSHC_EMMC_DLL_DLYENA |
			DLL_CMDOUT_TAPNUM_90_DEGREES |
			DLL_CMDOUT_TAPNUM_FROM_SW;
		sdhci_writel(host, extra, DECMSHC_EMMC_DLL_CMDOUT);
	}

	extra = DWCMSHC_EMMC_DLL_DLYENA |
		DLL_TXCLK_TAPNUM_FROM_SW |
		DLL_RXCLK_NO_INVERTER << DWCMSHC_EMMC_DLL_RXCLK_SRCSEL |
		txclk_tapnum;
	sdhci_writel(host, extra, DWCMSHC_EMMC_DLL_TXCLK);

	extra = DWCMSHC_EMMC_DLL_DLYENA |
		DLL_STRBIN_TAPNUM_DEFAULT |
		DLL_STRBIN_TAPNUM_FROM_SW;
	sdhci_writel(host, extra, DWCMSHC_EMMC_DLL_STRBIN);
}

static void rk35xx_sdhci_reset(struct sdhci_host *host, u8 mask)
{
	struct sdhci_pltfm_host *pltfm_host = sdhci_priv(host);
	struct dwcmshc_priv *dwc_priv = sdhci_pltfm_priv(pltfm_host);
	struct rk35xx_priv *priv = dwc_priv->priv;

	if (mask & SDHCI_RESET_ALL && priv->reset) {
		reset_control_assert(priv->reset);
		udelay(1);
		reset_control_deassert(priv->reset);
	}

	sdhci_reset(host, mask);
}

static int th1520_execute_tuning(struct sdhci_host *host, u32 opcode)
{
	struct sdhci_pltfm_host *pltfm_host = sdhci_priv(host);
	struct dwcmshc_priv *priv = sdhci_pltfm_priv(pltfm_host);
	u32 val = 0;

	if (host->flags & SDHCI_HS400_TUNING)
		return 0;

	sdhci_writeb(host, FIELD_PREP(PHY_ATDL_CNFG_INPSEL_MASK, PHY_ATDL_CNFG_INPSEL),
		     PHY_ATDL_CNFG_R);
	val = sdhci_readl(host, priv->vendor_specific_area1 + DWCMSHC_EMMC_ATCTRL);

	/*
	 * configure tuning settings:
	 *  - center phase select code driven in block gap interval
	 *  - disable reporting of framing errors
	 *  - disable software managed tuning
	 *  - disable user selection of sampling window edges,
	 *    instead tuning calculated edges are used
	 */
	val &= ~(AT_CTRL_CI_SEL | AT_CTRL_RPT_TUNE_ERR | AT_CTRL_SW_TUNE_EN |
		 FIELD_PREP(AT_CTRL_WIN_EDGE_SEL_MASK, AT_CTRL_WIN_EDGE_SEL));

	/*
	 * configure tuning settings:
	 *  - enable auto-tuning
	 *  - enable sampling window threshold
	 *  - stop clocks during phase code change
	 *  - set max latency in cycles between tx and rx clocks
	 *  - set max latency in cycles to switch output phase
	 *  - set max sampling window threshold value
	 */
	val |= AT_CTRL_AT_EN | AT_CTRL_SWIN_TH_EN | AT_CTRL_TUNE_CLK_STOP_EN;
	val |= FIELD_PREP(AT_CTRL_PRE_CHANGE_DLY_MASK, AT_CTRL_PRE_CHANGE_DLY);
	val |= FIELD_PREP(AT_CTRL_POST_CHANGE_DLY_MASK, AT_CTRL_POST_CHANGE_DLY);
	val |= FIELD_PREP(AT_CTRL_SWIN_TH_VAL_MASK, AT_CTRL_SWIN_TH_VAL);

	sdhci_writel(host, val, priv->vendor_specific_area1 + DWCMSHC_EMMC_ATCTRL);
	val = sdhci_readl(host, priv->vendor_specific_area1 + DWCMSHC_EMMC_ATCTRL);

	/* perform tuning */
	sdhci_start_tuning(host);
	host->tuning_err = __sdhci_execute_tuning(host, opcode);
	if (host->tuning_err) {
		/* disable auto-tuning upon tuning error */
		val &= ~AT_CTRL_AT_EN;
		sdhci_writel(host, val, priv->vendor_specific_area1 + DWCMSHC_EMMC_ATCTRL);
		dev_err(mmc_dev(host->mmc), "tuning failed: %d\n", host->tuning_err);
		return -EIO;
	}
	sdhci_end_tuning(host);

	return 0;
}

static void th1520_sdhci_reset(struct sdhci_host *host, u8 mask)
{
	struct sdhci_pltfm_host *pltfm_host = sdhci_priv(host);
	struct dwcmshc_priv *priv = sdhci_pltfm_priv(pltfm_host);
	u16 ctrl_2;

	sdhci_reset(host, mask);

	if (priv->flags & FLAG_IO_FIXED_1V8) {
		ctrl_2 = sdhci_readw(host, SDHCI_HOST_CONTROL2);
		if (!(ctrl_2 & SDHCI_CTRL_VDD_180)) {
			ctrl_2 |= SDHCI_CTRL_VDD_180;
			sdhci_writew(host, ctrl_2, SDHCI_HOST_CONTROL2);
		}
	}
}

static void cv18xx_sdhci_reset(struct sdhci_host *host, u8 mask)
{
	struct sdhci_pltfm_host *pltfm_host = sdhci_priv(host);
	struct dwcmshc_priv *priv = sdhci_pltfm_priv(pltfm_host);
	u32 val, emmc_caps = MMC_CAP2_NO_SD | MMC_CAP2_NO_SDIO;

	sdhci_reset(host, mask);

	if ((host->mmc->caps2 & emmc_caps) == emmc_caps) {
		val = sdhci_readl(host, priv->vendor_specific_area1 + CV18XX_SDHCI_MSHC_CTRL);
		val |= CV18XX_EMMC_FUNC_EN;
		sdhci_writel(host, val, priv->vendor_specific_area1 + CV18XX_SDHCI_MSHC_CTRL);
	}

	val = sdhci_readl(host, priv->vendor_specific_area1 + CV18XX_SDHCI_MSHC_CTRL);
	val |= CV18XX_LATANCY_1T;
	sdhci_writel(host, val, priv->vendor_specific_area1 + CV18XX_SDHCI_MSHC_CTRL);

	val = sdhci_readl(host, priv->vendor_specific_area1 + CV18XX_SDHCI_PHY_CONFIG);
	val |= CV18XX_PHY_TX_BPS;
	sdhci_writel(host, val, priv->vendor_specific_area1 + CV18XX_SDHCI_PHY_CONFIG);

	val =  (FIELD_PREP(CV18XX_PHY_TX_DLY_MSK, 0) |
		FIELD_PREP(CV18XX_PHY_TX_SRC_MSK, CV18XX_PHY_TX_SRC_INVERT_CLK_TX) |
		FIELD_PREP(CV18XX_PHY_RX_DLY_MSK, 0) |
		FIELD_PREP(CV18XX_PHY_RX_SRC_MSK, CV18XX_PHY_RX_SRC_INVERT_RX_CLK));
	sdhci_writel(host, val, priv->vendor_specific_area1 + CV18XX_SDHCI_PHY_TX_RX_DLY);
}

static const struct sdhci_ops sdhci_dwcmshc_ops = {
	.set_clock		= sdhci_set_clock,
	.set_bus_width		= sdhci_set_bus_width,
	.set_uhs_signaling	= dwcmshc_set_uhs_signaling,
	.get_max_clock		= dwcmshc_get_max_clock,
	.reset			= sdhci_reset,
	.adma_write_desc	= dwcmshc_adma_write_desc,
};

static const struct sdhci_ops sdhci_dwcmshc_rk35xx_ops = {
	.set_clock		= dwcmshc_rk3568_set_clock,
	.set_bus_width		= sdhci_set_bus_width,
	.set_uhs_signaling	= dwcmshc_set_uhs_signaling,
	.get_max_clock		= rk35xx_get_max_clock,
	.reset			= rk35xx_sdhci_reset,
	.adma_write_desc	= dwcmshc_adma_write_desc,
};

static const struct sdhci_ops sdhci_dwcmshc_th1520_ops = {
	.set_clock		= sdhci_set_clock,
	.set_bus_width		= sdhci_set_bus_width,
	.set_uhs_signaling	= th1520_set_uhs_signaling,
	.get_max_clock		= dwcmshc_get_max_clock,
	.reset			= th1520_sdhci_reset,
	.adma_write_desc	= dwcmshc_adma_write_desc,
	.voltage_switch		= dwcmshc_phy_1_8v_init,
	.platform_execute_tuning = &th1520_execute_tuning,
};

static const struct sdhci_ops sdhci_dwcmshc_cv18xx_ops = {
	.set_clock		= sdhci_set_clock,
	.set_bus_width		= sdhci_set_bus_width,
	.set_uhs_signaling	= dwcmshc_set_uhs_signaling,
	.get_max_clock		= dwcmshc_get_max_clock,
	.reset			= cv18xx_sdhci_reset,
	.adma_write_desc	= dwcmshc_adma_write_desc,
};

static const struct sdhci_pltfm_data sdhci_dwcmshc_pdata = {
	.ops = &sdhci_dwcmshc_ops,
	.quirks = SDHCI_QUIRK_CAP_CLOCK_BASE_BROKEN,
	.quirks2 = SDHCI_QUIRK2_PRESET_VALUE_BROKEN,
};

#ifdef CONFIG_ACPI
static const struct sdhci_pltfm_data sdhci_dwcmshc_bf3_pdata = {
	.ops = &sdhci_dwcmshc_ops,
	.quirks = SDHCI_QUIRK_CAP_CLOCK_BASE_BROKEN,
	.quirks2 = SDHCI_QUIRK2_PRESET_VALUE_BROKEN |
		   SDHCI_QUIRK2_ACMD23_BROKEN,
};
#endif

static const struct sdhci_pltfm_data sdhci_dwcmshc_rk35xx_pdata = {
	.ops = &sdhci_dwcmshc_rk35xx_ops,
	.quirks = SDHCI_QUIRK_CAP_CLOCK_BASE_BROKEN |
		  SDHCI_QUIRK_BROKEN_TIMEOUT_VAL,
	.quirks2 = SDHCI_QUIRK2_PRESET_VALUE_BROKEN |
		   SDHCI_QUIRK2_CLOCK_DIV_ZERO_BROKEN,
};

static const struct sdhci_pltfm_data sdhci_dwcmshc_th1520_pdata = {
	.ops = &sdhci_dwcmshc_th1520_ops,
	.quirks = SDHCI_QUIRK_CAP_CLOCK_BASE_BROKEN,
	.quirks2 = SDHCI_QUIRK2_PRESET_VALUE_BROKEN,
};

static const struct sdhci_pltfm_data sdhci_dwcmshc_cv18xx_pdata = {
	.ops = &sdhci_dwcmshc_cv18xx_ops,
	.quirks = SDHCI_QUIRK_CAP_CLOCK_BASE_BROKEN,
	.quirks2 = SDHCI_QUIRK2_PRESET_VALUE_BROKEN,
};

static int dwcmshc_rk35xx_init(struct sdhci_host *host, struct dwcmshc_priv *dwc_priv)
{
	int err;
	struct rk35xx_priv *priv = dwc_priv->priv;

	priv->reset = devm_reset_control_array_get_optional_exclusive(mmc_dev(host->mmc));
	if (IS_ERR(priv->reset)) {
		err = PTR_ERR(priv->reset);
		dev_err(mmc_dev(host->mmc), "failed to get reset control %d\n", err);
		return err;
	}

	priv->rockchip_clks[0].id = "axi";
	priv->rockchip_clks[1].id = "block";
	priv->rockchip_clks[2].id = "timer";
	err = devm_clk_bulk_get_optional(mmc_dev(host->mmc), RK35xx_MAX_CLKS,
					 priv->rockchip_clks);
	if (err) {
		dev_err(mmc_dev(host->mmc), "failed to get clocks %d\n", err);
		return err;
	}

	err = clk_bulk_prepare_enable(RK35xx_MAX_CLKS, priv->rockchip_clks);
	if (err) {
		dev_err(mmc_dev(host->mmc), "failed to enable clocks %d\n", err);
		return err;
	}

	if (of_property_read_u8(mmc_dev(host->mmc)->of_node, "rockchip,txclk-tapnum",
				&priv->txclk_tapnum))
		priv->txclk_tapnum = DLL_TXCLK_TAPNUM_DEFAULT;

	/* Disable cmd conflict check */
	sdhci_writel(host, 0x0, dwc_priv->vendor_specific_area1 + DWCMSHC_HOST_CTRL3);
	/* Reset previous settings */
	sdhci_writel(host, 0, DWCMSHC_EMMC_DLL_TXCLK);
	sdhci_writel(host, 0, DWCMSHC_EMMC_DLL_STRBIN);

	return 0;
}

static void dwcmshc_rk35xx_postinit(struct sdhci_host *host, struct dwcmshc_priv *dwc_priv)
{
	/*
	 * Don't support highspeed bus mode with low clk speed as we
	 * cannot use DLL for this condition.
	 */
	if (host->mmc->f_max <= 52000000) {
		dev_info(mmc_dev(host->mmc), "Disabling HS200/HS400, frequency too low (%d)\n",
			 host->mmc->f_max);
		host->mmc->caps2 &= ~(MMC_CAP2_HS200 | MMC_CAP2_HS400);
		host->mmc->caps &= ~(MMC_CAP_3_3V_DDR | MMC_CAP_1_8V_DDR);
	}
}

static const struct of_device_id sdhci_dwcmshc_dt_ids[] = {
	{
		.compatible = "rockchip,rk3588-dwcmshc",
		.data = &sdhci_dwcmshc_rk35xx_pdata,
	},
	{
		.compatible = "rockchip,rk3568-dwcmshc",
		.data = &sdhci_dwcmshc_rk35xx_pdata,
	},
	{
		.compatible = "snps,dwcmshc-sdhci",
		.data = &sdhci_dwcmshc_pdata,
	},
	{
		.compatible = "sophgo,cv1800b-dwcmshc",
		.data = &sdhci_dwcmshc_cv18xx_pdata,
	},
	{
		.compatible = "sophgo,sg2002-dwcmshc",
		.data = &sdhci_dwcmshc_cv18xx_pdata,
	},
	{
		.compatible = "thead,th1520-dwcmshc",
		.data = &sdhci_dwcmshc_th1520_pdata,
	},
	{},
};
MODULE_DEVICE_TABLE(of, sdhci_dwcmshc_dt_ids);

#ifdef CONFIG_ACPI
static const struct acpi_device_id sdhci_dwcmshc_acpi_ids[] = {
	{
		.id = "MLNXBF30",
		.driver_data = (kernel_ulong_t)&sdhci_dwcmshc_bf3_pdata,
	},
	{}
};
MODULE_DEVICE_TABLE(acpi, sdhci_dwcmshc_acpi_ids);
#endif

static int dwcmshc_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct sdhci_pltfm_host *pltfm_host;
	struct sdhci_host *host;
	struct dwcmshc_priv *priv;
	struct rk35xx_priv *rk_priv = NULL;
	const struct sdhci_pltfm_data *pltfm_data;
	int err;
	u32 extra;

	pltfm_data = device_get_match_data(&pdev->dev);
	if (!pltfm_data) {
		dev_err(&pdev->dev, "Error: No device match data found\n");
		return -ENODEV;
	}

	host = sdhci_pltfm_init(pdev, pltfm_data,
				sizeof(struct dwcmshc_priv));
	if (IS_ERR(host))
		return PTR_ERR(host);

	/*
	 * extra adma table cnt for cross 128M boundary handling.
	 */
	extra = DIV_ROUND_UP_ULL(dma_get_required_mask(dev), SZ_128M);
	if (extra > SDHCI_MAX_SEGS)
		extra = SDHCI_MAX_SEGS;
	host->adma_table_cnt += extra;

	pltfm_host = sdhci_priv(host);
	priv = sdhci_pltfm_priv(pltfm_host);

	if (dev->of_node) {
		pltfm_host->clk = devm_clk_get(dev, "core");
		if (IS_ERR(pltfm_host->clk)) {
			err = PTR_ERR(pltfm_host->clk);
			dev_err(dev, "failed to get core clk: %d\n", err);
			goto free_pltfm;
		}
		err = clk_prepare_enable(pltfm_host->clk);
		if (err)
			goto free_pltfm;

		priv->bus_clk = devm_clk_get(dev, "bus");
		if (!IS_ERR(priv->bus_clk))
			clk_prepare_enable(priv->bus_clk);
	}

	err = mmc_of_parse(host->mmc);
	if (err)
		goto err_clk;

	sdhci_get_of_property(pdev);

	priv->vendor_specific_area1 =
		sdhci_readl(host, DWCMSHC_P_VENDOR_AREA1) & DWCMSHC_AREA1_MASK;

	host->mmc_host_ops.request = dwcmshc_request;
	host->mmc_host_ops.hs400_enhanced_strobe = dwcmshc_hs400_enhanced_strobe;

	if (pltfm_data == &sdhci_dwcmshc_rk35xx_pdata) {
		rk_priv = devm_kzalloc(&pdev->dev, sizeof(struct rk35xx_priv), GFP_KERNEL);
		if (!rk_priv) {
			err = -ENOMEM;
			goto err_clk;
		}

		if (of_device_is_compatible(pdev->dev.of_node, "rockchip,rk3588-dwcmshc"))
			rk_priv->devtype = DWCMSHC_RK3588;
		else
			rk_priv->devtype = DWCMSHC_RK3568;

		priv->priv = rk_priv;

		err = dwcmshc_rk35xx_init(host, priv);
		if (err)
			goto err_clk;
	}

	if (pltfm_data == &sdhci_dwcmshc_th1520_pdata) {
		priv->delay_line = PHY_SDCLKDL_DC_DEFAULT;

		if (device_property_read_bool(dev, "mmc-ddr-1_8v") ||
		    device_property_read_bool(dev, "mmc-hs200-1_8v") ||
		    device_property_read_bool(dev, "mmc-hs400-1_8v"))
			priv->flags |= FLAG_IO_FIXED_1V8;
		else
			priv->flags &= ~FLAG_IO_FIXED_1V8;

		/*
		 * start_signal_voltage_switch() will try 3.3V first
		 * then 1.8V. Use SDHCI_SIGNALING_180 rather than
		 * SDHCI_SIGNALING_330 to avoid setting voltage to 3.3V
		 * in sdhci_start_signal_voltage_switch().
		 */
		if (priv->flags & FLAG_IO_FIXED_1V8) {
			host->flags &= ~SDHCI_SIGNALING_330;
			host->flags |=  SDHCI_SIGNALING_180;
		}

		sdhci_enable_v4_mode(host);
	}

#ifdef CONFIG_ACPI
	if (pltfm_data == &sdhci_dwcmshc_bf3_pdata)
		sdhci_enable_v4_mode(host);
#endif

	host->mmc->caps |= MMC_CAP_WAIT_WHILE_BUSY;

	pm_runtime_get_noresume(dev);
	pm_runtime_set_active(dev);
	pm_runtime_enable(dev);

	err = sdhci_setup_host(host);
	if (err)
		goto err_rpm;

	if (rk_priv)
		dwcmshc_rk35xx_postinit(host, priv);

	err = __sdhci_add_host(host);
	if (err)
		goto err_setup_host;

	pm_runtime_put(dev);

	return 0;

err_setup_host:
	sdhci_cleanup_host(host);
err_rpm:
	pm_runtime_disable(dev);
	pm_runtime_put_noidle(dev);
err_clk:
	clk_disable_unprepare(pltfm_host->clk);
	clk_disable_unprepare(priv->bus_clk);
	if (rk_priv)
		clk_bulk_disable_unprepare(RK35xx_MAX_CLKS,
					   rk_priv->rockchip_clks);
free_pltfm:
	sdhci_pltfm_free(pdev);
	return err;
}

static void dwcmshc_remove(struct platform_device *pdev)
{
	struct sdhci_host *host = platform_get_drvdata(pdev);
	struct sdhci_pltfm_host *pltfm_host = sdhci_priv(host);
	struct dwcmshc_priv *priv = sdhci_pltfm_priv(pltfm_host);
	struct rk35xx_priv *rk_priv = priv->priv;

	sdhci_remove_host(host, 0);

	clk_disable_unprepare(pltfm_host->clk);
	clk_disable_unprepare(priv->bus_clk);
	if (rk_priv)
		clk_bulk_disable_unprepare(RK35xx_MAX_CLKS,
					   rk_priv->rockchip_clks);
	sdhci_pltfm_free(pdev);
}

#ifdef CONFIG_PM_SLEEP
static int dwcmshc_suspend(struct device *dev)
{
	struct sdhci_host *host = dev_get_drvdata(dev);
	struct sdhci_pltfm_host *pltfm_host = sdhci_priv(host);
	struct dwcmshc_priv *priv = sdhci_pltfm_priv(pltfm_host);
	struct rk35xx_priv *rk_priv = priv->priv;
	int ret;

	pm_runtime_resume(dev);

	ret = sdhci_suspend_host(host);
	if (ret)
		return ret;

	clk_disable_unprepare(pltfm_host->clk);
	if (!IS_ERR(priv->bus_clk))
		clk_disable_unprepare(priv->bus_clk);

	if (rk_priv)
		clk_bulk_disable_unprepare(RK35xx_MAX_CLKS,
					   rk_priv->rockchip_clks);

	return ret;
}

static int dwcmshc_resume(struct device *dev)
{
	struct sdhci_host *host = dev_get_drvdata(dev);
	struct sdhci_pltfm_host *pltfm_host = sdhci_priv(host);
	struct dwcmshc_priv *priv = sdhci_pltfm_priv(pltfm_host);
	struct rk35xx_priv *rk_priv = priv->priv;
	int ret;

	ret = clk_prepare_enable(pltfm_host->clk);
	if (ret)
		return ret;

	if (!IS_ERR(priv->bus_clk)) {
		ret = clk_prepare_enable(priv->bus_clk);
		if (ret)
			goto disable_clk;
	}

	if (rk_priv) {
		ret = clk_bulk_prepare_enable(RK35xx_MAX_CLKS,
					      rk_priv->rockchip_clks);
		if (ret)
			goto disable_bus_clk;
	}

	ret = sdhci_resume_host(host);
	if (ret)
		goto disable_rockchip_clks;

	return 0;

disable_rockchip_clks:
	if (rk_priv)
		clk_bulk_disable_unprepare(RK35xx_MAX_CLKS,
					   rk_priv->rockchip_clks);
disable_bus_clk:
	if (!IS_ERR(priv->bus_clk))
		clk_disable_unprepare(priv->bus_clk);
disable_clk:
	clk_disable_unprepare(pltfm_host->clk);
	return ret;
}
#endif

#ifdef CONFIG_PM

static void dwcmshc_enable_card_clk(struct sdhci_host *host)
{
	u16 ctrl;

	ctrl = sdhci_readw(host, SDHCI_CLOCK_CONTROL);
	if ((ctrl & SDHCI_CLOCK_INT_EN) && !(ctrl & SDHCI_CLOCK_CARD_EN)) {
		ctrl |= SDHCI_CLOCK_CARD_EN;
		sdhci_writew(host, ctrl, SDHCI_CLOCK_CONTROL);
	}
}

static void dwcmshc_disable_card_clk(struct sdhci_host *host)
{
	u16 ctrl;

	ctrl = sdhci_readw(host, SDHCI_CLOCK_CONTROL);
	if (ctrl & SDHCI_CLOCK_CARD_EN) {
		ctrl &= ~SDHCI_CLOCK_CARD_EN;
		sdhci_writew(host, ctrl, SDHCI_CLOCK_CONTROL);
	}
}

static int dwcmshc_runtime_suspend(struct device *dev)
{
	struct sdhci_host *host = dev_get_drvdata(dev);

	dwcmshc_disable_card_clk(host);

	return 0;
}

static int dwcmshc_runtime_resume(struct device *dev)
{
	struct sdhci_host *host = dev_get_drvdata(dev);

	dwcmshc_enable_card_clk(host);

	return 0;
}

#endif

static const struct dev_pm_ops dwcmshc_pmops = {
	SET_SYSTEM_SLEEP_PM_OPS(dwcmshc_suspend, dwcmshc_resume)
	SET_RUNTIME_PM_OPS(dwcmshc_runtime_suspend,
			   dwcmshc_runtime_resume, NULL)
};

static struct platform_driver sdhci_dwcmshc_driver = {
	.driver	= {
		.name	= "sdhci-dwcmshc",
		.probe_type = PROBE_PREFER_ASYNCHRONOUS,
		.of_match_table = sdhci_dwcmshc_dt_ids,
		.acpi_match_table = ACPI_PTR(sdhci_dwcmshc_acpi_ids),
		.pm = &dwcmshc_pmops,
	},
	.probe	= dwcmshc_probe,
	.remove_new = dwcmshc_remove,
};
module_platform_driver(sdhci_dwcmshc_driver);

MODULE_DESCRIPTION("SDHCI platform driver for Synopsys DWC MSHC");
MODULE_AUTHOR("Jisheng Zhang <jszhang@kernel.org>");
MODULE_LICENSE("GPL v2");
