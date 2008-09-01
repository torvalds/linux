/****************************************************************************
 * Driver for Solarflare 802.3an compliant PHY
 * Copyright 2007 Solarflare Communications Inc.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 as published
 * by the Free Software Foundation, incorporated herein by reference.
 */

#include <linux/delay.h>
#include <linux/seq_file.h>
#include "efx.h"
#include "gmii.h"
#include "mdio_10g.h"
#include "falcon.h"
#include "phy.h"
#include "falcon_hwdefs.h"
#include "boards.h"
#include "mac.h"

/* We expect these MMDs to be in the package */
/* AN not here as mdio_check_mmds() requires STAT2 support */
#define TENXPRESS_REQUIRED_DEVS (MDIO_MMDREG_DEVS0_PMAPMD | \
				 MDIO_MMDREG_DEVS0_PCS    | \
				 MDIO_MMDREG_DEVS0_PHYXS)

#define TENXPRESS_LOOPBACKS ((1 << LOOPBACK_PHYXS) |	\
			     (1 << LOOPBACK_PCS) |	\
			     (1 << LOOPBACK_PMAPMD) |	\
			     (1 << LOOPBACK_NETWORK))

/* We complain if we fail to see the link partner as 10G capable this many
 * times in a row (must be > 1 as sampling the autoneg. registers is racy)
 */
#define MAX_BAD_LP_TRIES	(5)

/* Extended control register */
#define	PMA_PMD_XCONTROL_REG 0xc000
#define	PMA_PMD_LNPGA_POWERDOWN_LBN 8
#define	PMA_PMD_LNPGA_POWERDOWN_WIDTH 1

/* extended status register */
#define PMA_PMD_XSTATUS_REG 0xc001
#define PMA_PMD_XSTAT_FLP_LBN   (12)

/* LED control register */
#define PMA_PMD_LED_CTRL_REG	(0xc007)
#define PMA_PMA_LED_ACTIVITY_LBN	(3)

/* LED function override register */
#define PMA_PMD_LED_OVERR_REG	(0xc009)
/* Bit positions for different LEDs (there are more but not wired on SFE4001)*/
#define PMA_PMD_LED_LINK_LBN	(0)
#define PMA_PMD_LED_SPEED_LBN	(2)
#define PMA_PMD_LED_TX_LBN	(4)
#define PMA_PMD_LED_RX_LBN	(6)
/* Override settings */
#define	PMA_PMD_LED_AUTO	(0)	/* H/W control */
#define	PMA_PMD_LED_ON		(1)
#define	PMA_PMD_LED_OFF		(2)
#define PMA_PMD_LED_FLASH	(3)
/* All LEDs under hardware control */
#define PMA_PMD_LED_FULL_AUTO	(0)
/* Green and Amber under hardware control, Red off */
#define PMA_PMD_LED_DEFAULT	(PMA_PMD_LED_OFF << PMA_PMD_LED_RX_LBN)


/* Special Software reset register */
#define PMA_PMD_EXT_CTRL_REG 49152
#define PMA_PMD_EXT_SSR_LBN 15

/* Misc register defines */
#define PCS_CLOCK_CTRL_REG 0xd801
#define PLL312_RST_N_LBN 2

#define PCS_SOFT_RST2_REG 0xd806
#define SERDES_RST_N_LBN 13
#define XGXS_RST_N_LBN 12

#define	PCS_TEST_SELECT_REG 0xd807	/* PRM 10.5.8 */
#define	CLK312_EN_LBN 3

/* PHYXS registers */
#define PHYXS_TEST1         (49162)
#define LOOPBACK_NEAR_LBN   (8)
#define LOOPBACK_NEAR_WIDTH (1)

/* Boot status register */
#define PCS_BOOT_STATUS_REG	(0xd000)
#define PCS_BOOT_FATAL_ERR_LBN	(0)
#define PCS_BOOT_PROGRESS_LBN	(1)
#define PCS_BOOT_PROGRESS_WIDTH	(2)
#define PCS_BOOT_COMPLETE_LBN	(3)
#define PCS_BOOT_MAX_DELAY	(100)
#define PCS_BOOT_POLL_DELAY	(10)

/* Time to wait between powering down the LNPGA and turning off the power
 * rails */
#define LNPGA_PDOWN_WAIT	(HZ / 5)

static int crc_error_reset_threshold = 100;
module_param(crc_error_reset_threshold, int, 0644);
MODULE_PARM_DESC(crc_error_reset_threshold,
		 "Max number of CRC errors before XAUI reset");

struct tenxpress_phy_data {
	enum efx_loopback_mode loopback_mode;
	atomic_t bad_crc_count;
	enum efx_phy_mode phy_mode;
	int bad_lp_tries;
};

void tenxpress_crc_err(struct efx_nic *efx)
{
	struct tenxpress_phy_data *phy_data = efx->phy_data;
	if (phy_data != NULL)
		atomic_inc(&phy_data->bad_crc_count);
}

/* Check that the C166 has booted successfully */
static int tenxpress_phy_check(struct efx_nic *efx)
{
	int phy_id = efx->mii.phy_id;
	int count = PCS_BOOT_MAX_DELAY / PCS_BOOT_POLL_DELAY;
	int boot_stat;

	/* Wait for the boot to complete (or not) */
	while (count) {
		boot_stat = mdio_clause45_read(efx, phy_id,
					       MDIO_MMD_PCS,
					       PCS_BOOT_STATUS_REG);
		if (boot_stat & (1 << PCS_BOOT_COMPLETE_LBN))
			break;
		count--;
		udelay(PCS_BOOT_POLL_DELAY);
	}

	if (!count) {
		EFX_ERR(efx, "%s: PHY boot timed out. Last status "
			"%x\n", __func__,
			(boot_stat >> PCS_BOOT_PROGRESS_LBN) &
			((1 << PCS_BOOT_PROGRESS_WIDTH) - 1));
		return -ETIMEDOUT;
	}

	return 0;
}

static int tenxpress_init(struct efx_nic *efx)
{
	int rc, reg;

	/* Turn on the clock  */
	reg = (1 << CLK312_EN_LBN);
	mdio_clause45_write(efx, efx->mii.phy_id,
			    MDIO_MMD_PCS, PCS_TEST_SELECT_REG, reg);

	rc = tenxpress_phy_check(efx);
	if (rc < 0)
		return rc;

	/* Set the LEDs up as: Green = Link, Amber = Link/Act, Red = Off */
	reg = mdio_clause45_read(efx, efx->mii.phy_id,
				 MDIO_MMD_PMAPMD, PMA_PMD_LED_CTRL_REG);
	reg |= (1 << PMA_PMA_LED_ACTIVITY_LBN);
	mdio_clause45_write(efx, efx->mii.phy_id, MDIO_MMD_PMAPMD,
			    PMA_PMD_LED_CTRL_REG, reg);

	reg = PMA_PMD_LED_DEFAULT;
	mdio_clause45_write(efx, efx->mii.phy_id, MDIO_MMD_PMAPMD,
			    PMA_PMD_LED_OVERR_REG, reg);

	return rc;
}

static int tenxpress_phy_init(struct efx_nic *efx)
{
	struct tenxpress_phy_data *phy_data;
	int rc = 0;

	phy_data = kzalloc(sizeof(*phy_data), GFP_KERNEL);
	if (!phy_data)
		return -ENOMEM;
	efx->phy_data = phy_data;
	phy_data->phy_mode = efx->phy_mode;

	rc = mdio_clause45_wait_reset_mmds(efx,
					   TENXPRESS_REQUIRED_DEVS);
	if (rc < 0)
		goto fail;

	rc = mdio_clause45_check_mmds(efx, TENXPRESS_REQUIRED_DEVS, 0);
	if (rc < 0)
		goto fail;

	rc = tenxpress_init(efx);
	if (rc < 0)
		goto fail;

	schedule_timeout_uninterruptible(HZ / 5); /* 200ms */

	/* Let XGXS and SerDes out of reset and resets 10XPress */
	falcon_reset_xaui(efx);

	return 0;

 fail:
	kfree(efx->phy_data);
	efx->phy_data = NULL;
	return rc;
}

static int tenxpress_special_reset(struct efx_nic *efx)
{
	int rc, reg;

	/* The XGMAC clock is driven from the SFC7101/SFT9001 312MHz clock, so
	 * a special software reset can glitch the XGMAC sufficiently for stats
	 * requests to fail. Since we don't ofen special_reset, just lock. */
	spin_lock(&efx->stats_lock);

	/* Initiate reset */
	reg = mdio_clause45_read(efx, efx->mii.phy_id,
				 MDIO_MMD_PMAPMD, PMA_PMD_EXT_CTRL_REG);
	reg |= (1 << PMA_PMD_EXT_SSR_LBN);
	mdio_clause45_write(efx, efx->mii.phy_id, MDIO_MMD_PMAPMD,
			    PMA_PMD_EXT_CTRL_REG, reg);

	mdelay(200);

	/* Wait for the blocks to come out of reset */
	rc = mdio_clause45_wait_reset_mmds(efx,
					   TENXPRESS_REQUIRED_DEVS);
	if (rc < 0)
		goto unlock;

	/* Try and reconfigure the device */
	rc = tenxpress_init(efx);
	if (rc < 0)
		goto unlock;

unlock:
	spin_unlock(&efx->stats_lock);
	return rc;
}

static void tenxpress_set_bad_lp(struct efx_nic *efx, bool bad_lp)
{
	struct tenxpress_phy_data *pd = efx->phy_data;
	int reg;

	/* Nothing to do if all is well and was previously so. */
	if (!(bad_lp || pd->bad_lp_tries))
		return;

	reg = mdio_clause45_read(efx, efx->mii.phy_id,
				 MDIO_MMD_PMAPMD, PMA_PMD_LED_OVERR_REG);

	if (bad_lp)
		pd->bad_lp_tries++;
	else
		pd->bad_lp_tries = 0;

	if (pd->bad_lp_tries == MAX_BAD_LP_TRIES) {
		pd->bad_lp_tries = 0;	/* Restart count */
		reg &= ~(PMA_PMD_LED_FLASH << PMA_PMD_LED_RX_LBN);
		reg |= (PMA_PMD_LED_FLASH << PMA_PMD_LED_RX_LBN);
		EFX_ERR(efx, "This NIC appears to be plugged into"
			" a port that is not 10GBASE-T capable.\n"
			" This PHY is 10GBASE-T ONLY, so no link can"
			" be established.\n");
	} else {
		reg |= (PMA_PMD_LED_OFF << PMA_PMD_LED_RX_LBN);
	}
	mdio_clause45_write(efx, efx->mii.phy_id, MDIO_MMD_PMAPMD,
			    PMA_PMD_LED_OVERR_REG, reg);
}

/* Check link status and return a boolean OK value. If the link is NOT
 * OK we have a quick rummage round to see if we appear to be plugged
 * into a non-10GBT port and if so warn the user that they won't get
 * link any time soon as we are 10GBT only, unless caller specified
 * not to do this check (it isn't useful in loopback) */
static bool tenxpress_link_ok(struct efx_nic *efx, bool check_lp)
{
	bool ok = mdio_clause45_links_ok(efx, TENXPRESS_REQUIRED_DEVS);

	if (ok) {
		tenxpress_set_bad_lp(efx, false);
	} else if (check_lp) {
		/* Are we plugged into the wrong sort of link? */
		bool bad_lp = false;
		int phy_id = efx->mii.phy_id;
		int an_stat = mdio_clause45_read(efx, phy_id, MDIO_MMD_AN,
						 MDIO_AN_STATUS);
		int xphy_stat = mdio_clause45_read(efx, phy_id,
						   MDIO_MMD_PMAPMD,
						   PMA_PMD_XSTATUS_REG);
		/* Are we plugged into anything that sends FLPs? If
		 * not we can't distinguish between not being plugged
		 * in and being plugged into a non-AN antique. The FLP
		 * bit has the advantage of not clearing when autoneg
		 * restarts. */
		if (!(xphy_stat & (1 << PMA_PMD_XSTAT_FLP_LBN))) {
			tenxpress_set_bad_lp(efx, false);
			return ok;
		}

		/* If it can do 10GBT it must be XNP capable */
		bad_lp = !(an_stat & (1 << MDIO_AN_STATUS_XNP_LBN));
		if (!bad_lp && (an_stat & (1 << MDIO_AN_STATUS_PAGE_LBN))) {
			bad_lp = !(mdio_clause45_read(efx, phy_id,
					MDIO_MMD_AN, MDIO_AN_10GBT_STATUS) &
					(1 << MDIO_AN_10GBT_STATUS_LP_10G_LBN));
		}
		tenxpress_set_bad_lp(efx, bad_lp);
	}
	return ok;
}

static void tenxpress_phyxs_loopback(struct efx_nic *efx)
{
	int phy_id = efx->mii.phy_id;
	int ctrl1, ctrl2;

	ctrl1 = ctrl2 = mdio_clause45_read(efx, phy_id, MDIO_MMD_PHYXS,
					   PHYXS_TEST1);
	if (efx->loopback_mode == LOOPBACK_PHYXS)
		ctrl2 |= (1 << LOOPBACK_NEAR_LBN);
	else
		ctrl2 &= ~(1 << LOOPBACK_NEAR_LBN);
	if (ctrl1 != ctrl2)
		mdio_clause45_write(efx, phy_id, MDIO_MMD_PHYXS,
				    PHYXS_TEST1, ctrl2);
}

static void tenxpress_phy_reconfigure(struct efx_nic *efx)
{
	struct tenxpress_phy_data *phy_data = efx->phy_data;
	bool loop_change = LOOPBACK_OUT_OF(phy_data, efx,
					   TENXPRESS_LOOPBACKS);

	if (efx->phy_mode & PHY_MODE_SPECIAL) {
		phy_data->phy_mode = efx->phy_mode;
		return;
	}

	/* When coming out of transmit disable, coming out of low power
	 * mode, or moving out of any PHY internal loopback mode,
	 * perform a special software reset */
	if ((efx->phy_mode == PHY_MODE_NORMAL &&
	     phy_data->phy_mode != PHY_MODE_NORMAL) ||
	    loop_change) {
		tenxpress_special_reset(efx);
		falcon_reset_xaui(efx);
	}

	mdio_clause45_transmit_disable(efx);
	mdio_clause45_phy_reconfigure(efx);
	tenxpress_phyxs_loopback(efx);

	phy_data->loopback_mode = efx->loopback_mode;
	phy_data->phy_mode = efx->phy_mode;
	efx->link_up = tenxpress_link_ok(efx, false);
	efx->link_options = GM_LPA_10000FULL;
}

static void tenxpress_phy_clear_interrupt(struct efx_nic *efx)
{
	/* Nothing done here - LASI interrupts aren't reliable so poll  */
}


/* Poll PHY for interrupt */
static int tenxpress_phy_check_hw(struct efx_nic *efx)
{
	struct tenxpress_phy_data *phy_data = efx->phy_data;
	bool link_ok;

	link_ok = (phy_data->phy_mode == PHY_MODE_NORMAL &&
		   tenxpress_link_ok(efx, true));

	if (link_ok != efx->link_up)
		falcon_xmac_sim_phy_event(efx);

	if (phy_data->phy_mode != PHY_MODE_NORMAL)
		return 0;

	if (atomic_read(&phy_data->bad_crc_count) > crc_error_reset_threshold) {
		EFX_ERR(efx, "Resetting XAUI due to too many CRC errors\n");
		falcon_reset_xaui(efx);
		atomic_set(&phy_data->bad_crc_count, 0);
	}

	return 0;
}

static void tenxpress_phy_fini(struct efx_nic *efx)
{
	int reg;

	/* Power down the LNPGA */
	reg = (1 << PMA_PMD_LNPGA_POWERDOWN_LBN);
	mdio_clause45_write(efx, efx->mii.phy_id, MDIO_MMD_PMAPMD,
			    PMA_PMD_XCONTROL_REG, reg);

	/* Waiting here ensures that the board fini, which can turn off the
	 * power to the PHY, won't get run until the LNPGA powerdown has been
	 * given long enough to complete. */
	schedule_timeout_uninterruptible(LNPGA_PDOWN_WAIT); /* 200 ms */

	kfree(efx->phy_data);
	efx->phy_data = NULL;
}


/* Set the RX and TX LEDs and Link LED flashing. The other LEDs
 * (which probably aren't wired anyway) are left in AUTO mode */
void tenxpress_phy_blink(struct efx_nic *efx, bool blink)
{
	int reg;

	if (blink)
		reg = (PMA_PMD_LED_FLASH << PMA_PMD_LED_TX_LBN) |
			(PMA_PMD_LED_FLASH << PMA_PMD_LED_RX_LBN) |
			(PMA_PMD_LED_FLASH << PMA_PMD_LED_LINK_LBN);
	else
		reg = PMA_PMD_LED_DEFAULT;

	mdio_clause45_write(efx, efx->mii.phy_id, MDIO_MMD_PMAPMD,
			    PMA_PMD_LED_OVERR_REG, reg);
}

static int tenxpress_phy_test(struct efx_nic *efx)
{
	/* BIST is automatically run after a special software reset */
	return tenxpress_special_reset(efx);
}

struct efx_phy_operations falcon_tenxpress_phy_ops = {
	.init             = tenxpress_phy_init,
	.reconfigure      = tenxpress_phy_reconfigure,
	.check_hw         = tenxpress_phy_check_hw,
	.fini             = tenxpress_phy_fini,
	.clear_interrupt  = tenxpress_phy_clear_interrupt,
	.test             = tenxpress_phy_test,
	.mmds             = TENXPRESS_REQUIRED_DEVS,
	.loopbacks        = TENXPRESS_LOOPBACKS,
};
