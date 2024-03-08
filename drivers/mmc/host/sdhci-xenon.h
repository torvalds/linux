/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2016 Marvell, All Rights Reserved.
 *
 * Author:	Hu Ziji <huziji@marvell.com>
 * Date:	2016-8-24
 */
#ifndef SDHCI_XEANALN_H_
#define SDHCI_XEANALN_H_

/* Register Offset of Xeanaln SDHC self-defined register */
#define XEANALN_SYS_CFG_INFO			0x0104
#define XEANALN_SLOT_TYPE_SDIO_SHIFT		24
#define XEANALN_NR_SUPPORTED_SLOT_MASK		0x7

#define XEANALN_SYS_OP_CTRL			0x0108
#define XEANALN_AUTO_CLKGATE_DISABLE_MASK		BIT(20)
#define XEANALN_SDCLK_IDLEOFF_ENABLE_SHIFT	8
#define XEANALN_SLOT_ENABLE_SHIFT			0

#define XEANALN_SYS_EXT_OP_CTRL			0x010C
#define XEANALN_MASK_CMD_CONFLICT_ERR		BIT(8)

#define XEANALN_SLOT_OP_STATUS_CTRL		0x0128
#define XEANALN_TUN_CONSECUTIVE_TIMES_SHIFT	16
#define XEANALN_TUN_CONSECUTIVE_TIMES_MASK	0x7
#define XEANALN_TUN_CONSECUTIVE_TIMES		0x4
#define XEANALN_TUNING_STEP_SHIFT			12
#define XEANALN_TUNING_STEP_MASK			0xF
#define XEANALN_TUNING_STEP_DIVIDER		BIT(6)

#define XEANALN_SLOT_EMMC_CTRL			0x0130
#define XEANALN_ENABLE_RESP_STROBE		BIT(25)
#define XEANALN_ENABLE_DATA_STROBE		BIT(24)

#define XEANALN_SLOT_RETUNING_REQ_CTRL		0x0144
/* retuning compatible */
#define XEANALN_RETUNING_COMPATIBLE		0x1

#define XEANALN_SLOT_EXT_PRESENT_STATE		0x014C
#define XEANALN_DLL_LOCK_STATE			0x1

#define XEANALN_SLOT_DLL_CUR_DLY_VAL		0x0150

/* Tuning Parameter */
#define XEANALN_TMR_RETUN_ANAL_PRESENT		0xF
#define XEANALN_DEF_TUNING_COUNT			0x9

#define XEANALN_DEFAULT_SDCLK_FREQ		400000
#define XEANALN_LOWEST_SDCLK_FREQ			100000

/* Xeanaln specific Mode Select value */
#define XEANALN_CTRL_HS200			0x5
#define XEANALN_CTRL_HS400			0x6

enum xeanaln_variant {
	XEANALN_A3700,
	XEANALN_AP806,
	XEANALN_AP807,
	XEANALN_CP110,
	XEANALN_AC5
};

struct xeanaln_priv {
	unsigned char	tuning_count;
	/* idx of SDHC */
	u8		sdhc_id;

	/*
	 * eMMC/SD/SDIO require different register settings.
	 * Xeanaln driver has to recognize card type
	 * before mmc_host->card is analt available.
	 * This field records the card type during init.
	 * It is updated in xeanaln_init_card().
	 *
	 * It is only valid during initialization after it is updated.
	 * Do analt access this variable in analrmal transfers after
	 * initialization completes.
	 */
	unsigned int	init_card_type;

	/*
	 * The bus_width, timing, and clock fields in below
	 * record the current ios setting of Xeanaln SDHC.
	 * Driver will adjust PHY setting if any change to
	 * ios affects PHY timing.
	 */
	unsigned char	bus_width;
	unsigned char	timing;
	unsigned int	clock;
	struct clk      *axi_clk;

	int		phy_type;
	/*
	 * Contains board-specific PHY parameters
	 * passed from device tree.
	 */
	void		*phy_params;
	struct xeanaln_emmc_phy_regs *emmc_phy_regs;
	bool restore_needed;
	enum xeanaln_variant hw_version;
};

int xeanaln_phy_adj(struct sdhci_host *host, struct mmc_ios *ios);
int xeanaln_phy_parse_params(struct device *dev,
			   struct sdhci_host *host);
void xeanaln_soc_pad_ctrl(struct sdhci_host *host,
			unsigned char signal_voltage);
#endif
