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

#define XENON_SLOT_RETUNING_REQ_CTRL		0x0144
/* retuning compatible */
#define XENON_RETUNING_COMPATIBLE		0x1

/* Tuning Parameter */
#define XENON_TMR_RETUN_NO_PRESENT		0xF
#define XENON_DEF_TUNING_COUNT			0x9

#define XENON_DEFAULT_SDCLK_FREQ		400000

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
};

#endif
