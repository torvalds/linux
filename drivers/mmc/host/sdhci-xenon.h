/*
 * Copyright (C) 2016 Marvell, All Rights Reserved.
 *
 * Author:	Hu Ziji <huziji@marvell.com>
 * Date:	2016-8-24
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation version 2.
 */
#ifndef SDHCI_XENON_H_
#define SDHCI_XENON_H_

/* Register Offset of Xenon SDHC self-defined register */
#define XENON_SYS_CFG_INFO			0x0104
#define XENON_SLOT_TYPE_SDIO_SHIFT		24
#define XENON_NR_SUPPORTED_SLOT_MASK		0x7

#define XENON_SYS_OP_CTRL			0x0108
#define XENON_AUTO_CLKGATE_DISABLE_MASK		BIT(20)
#define XENON_SDCLK_IDLEOFF_ENABLE_SHIFT	8
#define XENON_SLOT_ENABLE_SHIFT			0

#define XENON_SYS_EXT_OP_CTRL			0x010C
#define XENON_MASK_CMD_CONFLICT_ERR		BIT(8)

#define XENON_SLOT_OP_STATUS_CTRL		0x0128
#define XENON_TUN_CONSECUTIVE_TIMES_SHIFT	16
#define XENON_TUN_CONSECUTIVE_TIMES_MASK	0x7
#define XENON_TUN_CONSECUTIVE_TIMES		0x4
#define XENON_TUNING_STEP_SHIFT			12
#define XENON_TUNING_STEP_MASK			0xF
#define XENON_TUNING_STEP_DIVIDER		BIT(6)

#define XENON_SLOT_EMMC_CTRL			0x0130
#define XENON_ENABLE_RESP_STROBE		BIT(25)
#define XENON_ENABLE_DATA_STROBE		BIT(24)

#define XENON_SLOT_RETUNING_REQ_CTRL		0x0144
/* retuning compatible */
#define XENON_RETUNING_COMPATIBLE		0x1

#define XENON_SLOT_EXT_PRESENT_STATE		0x014C
#define XENON_DLL_LOCK_STATE			0x1

#define XENON_SLOT_DLL_CUR_DLY_VAL		0x0150

/* Tuning Parameter */
#define XENON_TMR_RETUN_NO_PRESENT		0xF
#define XENON_DEF_TUNING_COUNT			0x9

#define XENON_DEFAULT_SDCLK_FREQ		400000
#define XENON_LOWEST_SDCLK_FREQ			100000

/* Xenon specific Mode Select value */
#define XENON_CTRL_HS200			0x5
#define XENON_CTRL_HS400			0x6

struct xenon_priv {
	unsigned char	tuning_count;
	/* idx of SDHC */
	u8		sdhc_id;

	/*
	 * eMMC/SD/SDIO require different register settings.
	 * Xenon driver has to recognize card type
	 * before mmc_host->card is not available.
	 * This field records the card type during init.
	 * It is updated in xenon_init_card().
	 *
	 * It is only valid during initialization after it is updated.
	 * Do not access this variable in normal transfers after
	 * initialization completes.
	 */
	unsigned int	init_card_type;

	/*
	 * The bus_width, timing, and clock fields in below
	 * record the current ios setting of Xenon SDHC.
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
	struct xenon_emmc_phy_regs *emmc_phy_regs;
	bool restore_needed;
};

int xenon_phy_adj(struct sdhci_host *host, struct mmc_ios *ios);
int xenon_phy_parse_dt(struct device_node *np,
		       struct sdhci_host *host);
void xenon_soc_pad_ctrl(struct sdhci_host *host,
			unsigned char signal_voltage);
#endif
