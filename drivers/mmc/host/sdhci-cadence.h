/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Copyright (C) 2026 Altera Corporation
 *   Author: Tanmay Kathpalia <tanmay.kathpalia@altera.com>
 *
 * Cadence SD/SDIO/eMMC Host Controller driver - common header
 * Shared definitions and structures for the Cadence SDHCI driver.
 * Contains private data and declarations for SD6HC-specific functions
 * called by the main driver in sdhci-cadence-core.c.
 */

#ifndef _MMC_HOST_SDHCI_CADENCE_H
#define _MMC_HOST_SDHCI_CADENCE_H

#include <linux/compiler.h>
#include <linux/spinlock.h>
#include <linux/types.h>

#include "sdhci-pltfm.h"

struct reset_control;

/* HRS - Host Register Set (specific to Cadence) */
#define SDHCI_CDNS_HRS04		0x10		/* PHY access: address port */
#define SDHCI_CDNS_HRS05		0x14		/* PHY access: data port */

/*
 * The tuned val register is 6 bit-wide, but not the whole of the range is
 * available.  The range 0-42 seems to be available (then 43 wraps around to 0)
 * but I am not quite sure if it is official.  Use only 0 to 39 for safety.
 */
#define SDHCI_CDNS_MAX_TUNING_LOOP	40

/**
 * struct sdhci_cdns_priv - Cadence SDHCI private controller data
 * @hrs_addr: Base address of Cadence Host Register Set (HRS) registers.
 * @ctl_addr: Base address for write control registers.
 *            Used only for "amd,pensando-elba-sd4hc" compatible controllers to enable
 *            byte-lane writes.
 * @wrlock: Spinlock for protecting register writes (Elba only).
 * @enhanced_strobe: Flag indicating if Enhanced Strobe (HS400ES) is enabled.
 * @priv_writel: Optional SoC-specific write function for register access.
 *               Used for Elba to ensure correct byte-lane enable.
 * @rst_hw: Hardware reset control for the eMMC card RST_n pin (SD4HC only).
 * @phy: Opaque pointer to variant-specific PHY data.
 *       For SD4HC: points to struct sdhci_cdns4_phy.
 *       For SD6HC: points to struct sdhci_cdns6_phy.
 */
struct sdhci_cdns_priv {
	void __iomem *hrs_addr;
	void __iomem *ctl_addr;	/* write control */
	spinlock_t wrlock;	/* write lock */
	bool enhanced_strobe;
	void (*priv_writel)(struct sdhci_cdns_priv *priv, u32 val, void __iomem *reg);
	struct reset_control *rst_hw;
	void *phy;
};

/**
 * sdhci_cdns_priv - Helper to retrieve Cadence private data from sdhci_host
 * @host: Pointer to struct sdhci_host.
 *
 * Return: Pointer to struct sdhci_cdns_priv.
 */
static inline void *sdhci_cdns_priv(struct sdhci_host *host)
{
	struct sdhci_pltfm_host *pltfm_host = sdhci_priv(host);

	return sdhci_pltfm_priv(pltfm_host);
}

/**
 * sdhci_cdns6_set_clock - Set SDCLK and reprogram SD6HC PHY timings.
 * @host: Pointer to struct sdhci_host.
 * @clock: Requested clock frequency in Hz (0 to disable).
 */
void sdhci_cdns6_set_clock(struct sdhci_host *host, unsigned int clock);

/**
 * sdhci_cdns6_set_uhs_signaling - Program PHY registers for a specific timing mode.
 * @host: Pointer to struct sdhci_host.
 * @timing: MMC timing mode (MMC_TIMING_*).
 */
void sdhci_cdns6_set_uhs_signaling(struct sdhci_host *host, unsigned int timing);

/**
 * sdhci_cdns6_set_tune_val - Set the PHY tuning value.
 * @host: Pointer to struct sdhci_host.
 * @val: Tuning value to program.
 *
 * Return: 0 on success, -ETIMEDOUT if DLL reset release times out.
 */
int sdhci_cdns6_set_tune_val(struct sdhci_host *host, unsigned int val);

/**
 * sdhci_cdns6_phy_probe - Probe and initialize Cadence SD6HC PHY parameters
 * @pdev: Platform device pointer
 * @priv: Pointer to Cadence private data structure
 *
 * Return: 0 on success or a negative error code.
 */
int sdhci_cdns6_phy_probe(struct platform_device *pdev, struct sdhci_cdns_priv *priv);

/**
 * sdhci_cdns6_hw_reset - Perform hardware reset of the Cadence SDHCI controller.
 * @host: Pointer to struct sdhci_host.
 */
void sdhci_cdns6_hw_reset(struct sdhci_host *host);

/**
 * sdhci_cdns6_phy_init - Initialize the SD6HC PHY with current settings.
 * @priv: Pointer to Cadence private data structure.
 *
 * Return: 0 on success, -ETIMEDOUT if PHY initialization times out.
 */
int sdhci_cdns6_phy_init(struct sdhci_cdns_priv *priv);

#endif /* _MMC_HOST_SDHCI_CADENCE_H */
