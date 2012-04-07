/****************************************************************************
 * Driver for Solarflare Solarstorm network controllers and boards
 * Copyright 2007-2011 Solarflare Communications Inc.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 as published
 * by the Free Software Foundation, incorporated herein by reference.
 */

#include <linux/delay.h>
#include <linux/rtnetlink.h>
#include <linux/seq_file.h>
#include <linux/slab.h>
#include "efx.h"
#include "mdio_10g.h"
#include "nic.h"
#include "phy.h"
#include "workarounds.h"

/* We expect these MMDs to be in the package. */
#define TENXPRESS_REQUIRED_DEVS (MDIO_DEVS_PMAPMD	| \
				 MDIO_DEVS_PCS		| \
				 MDIO_DEVS_PHYXS	| \
				 MDIO_DEVS_AN)

#define SFX7101_LOOPBACKS ((1 << LOOPBACK_PHYXS) |	\
			   (1 << LOOPBACK_PCS) |	\
			   (1 << LOOPBACK_PMAPMD) |	\
			   (1 << LOOPBACK_PHYXS_WS))

/* We complain if we fail to see the link partner as 10G capable this many
 * times in a row (must be > 1 as sampling the autoneg. registers is racy)
 */
#define MAX_BAD_LP_TRIES	(5)

/* Extended control register */
#define PMA_PMD_XCONTROL_REG	49152
#define PMA_PMD_EXT_GMII_EN_LBN	1
#define PMA_PMD_EXT_GMII_EN_WIDTH 1
#define PMA_PMD_EXT_CLK_OUT_LBN	2
#define PMA_PMD_EXT_CLK_OUT_WIDTH 1
#define PMA_PMD_LNPGA_POWERDOWN_LBN 8
#define PMA_PMD_LNPGA_POWERDOWN_WIDTH 1
#define PMA_PMD_EXT_CLK312_WIDTH 1
#define PMA_PMD_EXT_LPOWER_LBN  12
#define PMA_PMD_EXT_LPOWER_WIDTH 1
#define PMA_PMD_EXT_ROBUST_LBN	14
#define PMA_PMD_EXT_ROBUST_WIDTH 1
#define PMA_PMD_EXT_SSR_LBN	15
#define PMA_PMD_EXT_SSR_WIDTH	1

/* extended status register */
#define PMA_PMD_XSTATUS_REG	49153
#define PMA_PMD_XSTAT_MDIX_LBN	14
#define PMA_PMD_XSTAT_FLP_LBN   (12)

/* LED control register */
#define PMA_PMD_LED_CTRL_REG	49159
#define PMA_PMA_LED_ACTIVITY_LBN	(3)

/* LED function override register */
#define PMA_PMD_LED_OVERR_REG	49161
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
#define PMA_PMD_LED_MASK	3
/* All LEDs under hardware control */
/* Green and Amber under hardware control, Red off */
#define SFX7101_PMA_PMD_LED_DEFAULT (PMA_PMD_LED_OFF << PMA_PMD_LED_RX_LBN)

#define PMA_PMD_SPEED_ENABLE_REG 49192
#define PMA_PMD_100TX_ADV_LBN    1
#define PMA_PMD_100TX_ADV_WIDTH  1
#define PMA_PMD_1000T_ADV_LBN    2
#define PMA_PMD_1000T_ADV_WIDTH  1
#define PMA_PMD_10000T_ADV_LBN   3
#define PMA_PMD_10000T_ADV_WIDTH 1
#define PMA_PMD_SPEED_LBN        4
#define PMA_PMD_SPEED_WIDTH      4

/* Misc register defines */
#define PCS_CLOCK_CTRL_REG	55297
#define PLL312_RST_N_LBN 2

#define PCS_SOFT_RST2_REG	55302
#define SERDES_RST_N_LBN 13
#define XGXS_RST_N_LBN 12

#define	PCS_TEST_SELECT_REG	55303	/* PRM 10.5.8 */
#define	CLK312_EN_LBN 3

/* PHYXS registers */
#define PHYXS_XCONTROL_REG	49152
#define PHYXS_RESET_LBN		15
#define PHYXS_RESET_WIDTH	1

#define PHYXS_TEST1         (49162)
#define LOOPBACK_NEAR_LBN   (8)
#define LOOPBACK_NEAR_WIDTH (1)

/* Boot status register */
#define PCS_BOOT_STATUS_REG		53248
#define PCS_BOOT_FATAL_ERROR_LBN	0
#define PCS_BOOT_PROGRESS_LBN		1
#define PCS_BOOT_PROGRESS_WIDTH		2
#define PCS_BOOT_PROGRESS_INIT		0
#define PCS_BOOT_PROGRESS_WAIT_MDIO	1
#define PCS_BOOT_PROGRESS_CHECKSUM	2
#define PCS_BOOT_PROGRESS_JUMP		3
#define PCS_BOOT_DOWNLOAD_WAIT_LBN	3
#define PCS_BOOT_CODE_STARTED_LBN	4

/* 100M/1G PHY registers */
#define GPHY_XCONTROL_REG	49152
#define GPHY_ISOLATE_LBN	10
#define GPHY_ISOLATE_WIDTH	1
#define GPHY_DUPLEX_LBN		8
#define GPHY_DUPLEX_WIDTH	1
#define GPHY_LOOPBACK_NEAR_LBN	14
#define GPHY_LOOPBACK_NEAR_WIDTH 1

#define C22EXT_STATUS_REG       49153
#define C22EXT_STATUS_LINK_LBN  2
#define C22EXT_STATUS_LINK_WIDTH 1

#define C22EXT_MSTSLV_CTRL			49161
#define C22EXT_MSTSLV_CTRL_ADV_1000_HD_LBN	8
#define C22EXT_MSTSLV_CTRL_ADV_1000_FD_LBN	9

#define C22EXT_MSTSLV_STATUS			49162
#define C22EXT_MSTSLV_STATUS_LP_1000_HD_LBN	10
#define C22EXT_MSTSLV_STATUS_LP_1000_FD_LBN	11

/* Time to wait between powering down the LNPGA and turning off the power
 * rails */
#define LNPGA_PDOWN_WAIT	(HZ / 5)

struct tenxpress_phy_data {
	enum efx_loopback_mode loopback_mode;
	enum efx_phy_mode phy_mode;
	int bad_lp_tries;
};

static int tenxpress_init(struct efx_nic *efx)
{
	/* Enable 312.5 MHz clock */
	efx_mdio_write(efx, MDIO_MMD_PCS, PCS_TEST_SELECT_REG,
		       1 << CLK312_EN_LBN);

	/* Set the LEDs up as: Green = Link, Amber = Link/Act, Red = Off */
	efx_mdio_set_flag(efx, MDIO_MMD_PMAPMD, PMA_PMD_LED_CTRL_REG,
			  1 << PMA_PMA_LED_ACTIVITY_LBN, true);
	efx_mdio_write(efx, MDIO_MMD_PMAPMD, PMA_PMD_LED_OVERR_REG,
		       SFX7101_PMA_PMD_LED_DEFAULT);

	return 0;
}

static int tenxpress_phy_probe(struct efx_nic *efx)
{
	struct tenxpress_phy_data *phy_data;

	/* Allocate phy private storage */
	phy_data = kzalloc(sizeof(*phy_data), GFP_KERNEL);
	if (!phy_data)
		return -ENOMEM;
	efx->phy_data = phy_data;
	phy_data->phy_mode = efx->phy_mode;

	efx->mdio.mmds = TENXPRESS_REQUIRED_DEVS;
	efx->mdio.mode_support = MDIO_SUPPORTS_C45;

	efx->loopback_modes = SFX7101_LOOPBACKS | FALCON_XMAC_LOOPBACKS;

	efx->link_advertising = (ADVERTISED_TP | ADVERTISED_Autoneg |
				 ADVERTISED_10000baseT_Full);

	return 0;
}

static int tenxpress_phy_init(struct efx_nic *efx)
{
	int rc;

	falcon_board(efx)->type->init_phy(efx);

	if (!(efx->phy_mode & PHY_MODE_SPECIAL)) {
		rc = efx_mdio_wait_reset_mmds(efx, TENXPRESS_REQUIRED_DEVS);
		if (rc < 0)
			return rc;

		rc = efx_mdio_check_mmds(efx, TENXPRESS_REQUIRED_DEVS);
		if (rc < 0)
			return rc;
	}

	rc = tenxpress_init(efx);
	if (rc < 0)
		return rc;

	/* Reinitialise flow control settings */
	efx_link_set_wanted_fc(efx, efx->wanted_fc);
	efx_mdio_an_reconfigure(efx);

	schedule_timeout_uninterruptible(HZ / 5); /* 200ms */

	/* Let XGXS and SerDes out of reset */
	falcon_reset_xaui(efx);

	return 0;
}

/* Perform a "special software reset" on the PHY. The caller is
 * responsible for saving and restoring the PHY hardware registers
 * properly, and masking/unmasking LASI */
static int tenxpress_special_reset(struct efx_nic *efx)
{
	int rc, reg;

	/* The XGMAC clock is driven from the SFX7101 312MHz clock, so
	 * a special software reset can glitch the XGMAC sufficiently for stats
	 * requests to fail. */
	falcon_stop_nic_stats(efx);

	/* Initiate reset */
	reg = efx_mdio_read(efx, MDIO_MMD_PMAPMD, PMA_PMD_XCONTROL_REG);
	reg |= (1 << PMA_PMD_EXT_SSR_LBN);
	efx_mdio_write(efx, MDIO_MMD_PMAPMD, PMA_PMD_XCONTROL_REG, reg);

	mdelay(200);

	/* Wait for the blocks to come out of reset */
	rc = efx_mdio_wait_reset_mmds(efx, TENXPRESS_REQUIRED_DEVS);
	if (rc < 0)
		goto out;

	/* Try and reconfigure the device */
	rc = tenxpress_init(efx);
	if (rc < 0)
		goto out;

	/* Wait for the XGXS state machine to churn */
	mdelay(10);
out:
	falcon_start_nic_stats(efx);
	return rc;
}

static void sfx7101_check_bad_lp(struct efx_nic *efx, bool link_ok)
{
	struct tenxpress_phy_data *pd = efx->phy_data;
	bool bad_lp;
	int reg;

	if (link_ok) {
		bad_lp = false;
	} else {
		/* Check that AN has started but not completed. */
		reg = efx_mdio_read(efx, MDIO_MMD_AN, MDIO_STAT1);
		if (!(reg & MDIO_AN_STAT1_LPABLE))
			return; /* LP status is unknown */
		bad_lp = !(reg & MDIO_AN_STAT1_COMPLETE);
		if (bad_lp)
			pd->bad_lp_tries++;
	}

	/* Nothing to do if all is well and was previously so. */
	if (!pd->bad_lp_tries)
		return;

	/* Use the RX (red) LED as an error indicator once we've seen AN
	 * failure several times in a row, and also log a message. */
	if (!bad_lp || pd->bad_lp_tries == MAX_BAD_LP_TRIES) {
		reg = efx_mdio_read(efx, MDIO_MMD_PMAPMD,
				    PMA_PMD_LED_OVERR_REG);
		reg &= ~(PMA_PMD_LED_MASK << PMA_PMD_LED_RX_LBN);
		if (!bad_lp) {
			reg |= PMA_PMD_LED_OFF << PMA_PMD_LED_RX_LBN;
		} else {
			reg |= PMA_PMD_LED_FLASH << PMA_PMD_LED_RX_LBN;
			netif_err(efx, link, efx->net_dev,
				  "appears to be plugged into a port"
				  " that is not 10GBASE-T capable. The PHY"
				  " supports 10GBASE-T ONLY, so no link can"
				  " be established\n");
		}
		efx_mdio_write(efx, MDIO_MMD_PMAPMD,
			       PMA_PMD_LED_OVERR_REG, reg);
		pd->bad_lp_tries = bad_lp;
	}
}

static bool sfx7101_link_ok(struct efx_nic *efx)
{
	return efx_mdio_links_ok(efx,
				 MDIO_DEVS_PMAPMD |
				 MDIO_DEVS_PCS |
				 MDIO_DEVS_PHYXS);
}

static void tenxpress_ext_loopback(struct efx_nic *efx)
{
	efx_mdio_set_flag(efx, MDIO_MMD_PHYXS, PHYXS_TEST1,
			  1 << LOOPBACK_NEAR_LBN,
			  efx->loopback_mode == LOOPBACK_PHYXS);
}

static void tenxpress_low_power(struct efx_nic *efx)
{
	efx_mdio_set_mmds_lpower(
		efx, !!(efx->phy_mode & PHY_MODE_LOW_POWER),
		TENXPRESS_REQUIRED_DEVS);
}

static int tenxpress_phy_reconfigure(struct efx_nic *efx)
{
	struct tenxpress_phy_data *phy_data = efx->phy_data;
	bool phy_mode_change, loop_reset;

	if (efx->phy_mode & (PHY_MODE_OFF | PHY_MODE_SPECIAL)) {
		phy_data->phy_mode = efx->phy_mode;
		return 0;
	}

	phy_mode_change = (efx->phy_mode == PHY_MODE_NORMAL &&
			   phy_data->phy_mode != PHY_MODE_NORMAL);
	loop_reset = (LOOPBACK_OUT_OF(phy_data, efx, LOOPBACKS_EXTERNAL(efx)) ||
		      LOOPBACK_CHANGED(phy_data, efx, 1 << LOOPBACK_GPHY));

	if (loop_reset || phy_mode_change) {
		tenxpress_special_reset(efx);
		falcon_reset_xaui(efx);
	}

	tenxpress_low_power(efx);
	efx_mdio_transmit_disable(efx);
	efx_mdio_phy_reconfigure(efx);
	tenxpress_ext_loopback(efx);
	efx_mdio_an_reconfigure(efx);

	phy_data->loopback_mode = efx->loopback_mode;
	phy_data->phy_mode = efx->phy_mode;

	return 0;
}

static void
tenxpress_get_settings(struct efx_nic *efx, struct ethtool_cmd *ecmd);

/* Poll for link state changes */
static bool tenxpress_phy_poll(struct efx_nic *efx)
{
	struct efx_link_state old_state = efx->link_state;

	efx->link_state.up = sfx7101_link_ok(efx);
	efx->link_state.speed = 10000;
	efx->link_state.fd = true;
	efx->link_state.fc = efx_mdio_get_pause(efx);

	sfx7101_check_bad_lp(efx, efx->link_state.up);

	return !efx_link_state_equal(&efx->link_state, &old_state);
}

static void sfx7101_phy_fini(struct efx_nic *efx)
{
	int reg;

	/* Power down the LNPGA */
	reg = (1 << PMA_PMD_LNPGA_POWERDOWN_LBN);
	efx_mdio_write(efx, MDIO_MMD_PMAPMD, PMA_PMD_XCONTROL_REG, reg);

	/* Waiting here ensures that the board fini, which can turn
	 * off the power to the PHY, won't get run until the LNPGA
	 * powerdown has been given long enough to complete. */
	schedule_timeout_uninterruptible(LNPGA_PDOWN_WAIT); /* 200 ms */
}

static void tenxpress_phy_remove(struct efx_nic *efx)
{
	kfree(efx->phy_data);
	efx->phy_data = NULL;
}


/* Override the RX, TX and link LEDs */
void tenxpress_set_id_led(struct efx_nic *efx, enum efx_led_mode mode)
{
	int reg;

	switch (mode) {
	case EFX_LED_OFF:
		reg = (PMA_PMD_LED_OFF << PMA_PMD_LED_TX_LBN) |
			(PMA_PMD_LED_OFF << PMA_PMD_LED_RX_LBN) |
			(PMA_PMD_LED_OFF << PMA_PMD_LED_LINK_LBN);
		break;
	case EFX_LED_ON:
		reg = (PMA_PMD_LED_ON << PMA_PMD_LED_TX_LBN) |
			(PMA_PMD_LED_ON << PMA_PMD_LED_RX_LBN) |
			(PMA_PMD_LED_ON << PMA_PMD_LED_LINK_LBN);
		break;
	default:
		reg = SFX7101_PMA_PMD_LED_DEFAULT;
		break;
	}

	efx_mdio_write(efx, MDIO_MMD_PMAPMD, PMA_PMD_LED_OVERR_REG, reg);
}

static const char *const sfx7101_test_names[] = {
	"bist"
};

static const char *sfx7101_test_name(struct efx_nic *efx, unsigned int index)
{
	if (index < ARRAY_SIZE(sfx7101_test_names))
		return sfx7101_test_names[index];
	return NULL;
}

static int
sfx7101_run_tests(struct efx_nic *efx, int *results, unsigned flags)
{
	int rc;

	if (!(flags & ETH_TEST_FL_OFFLINE))
		return 0;

	/* BIST is automatically run after a special software reset */
	rc = tenxpress_special_reset(efx);
	results[0] = rc ? -1 : 1;

	efx_mdio_an_reconfigure(efx);

	return rc;
}

static void
tenxpress_get_settings(struct efx_nic *efx, struct ethtool_cmd *ecmd)
{
	u32 adv = 0, lpa = 0;
	int reg;

	reg = efx_mdio_read(efx, MDIO_MMD_AN, MDIO_AN_10GBT_CTRL);
	if (reg & MDIO_AN_10GBT_CTRL_ADV10G)
		adv |= ADVERTISED_10000baseT_Full;
	reg = efx_mdio_read(efx, MDIO_MMD_AN, MDIO_AN_10GBT_STAT);
	if (reg & MDIO_AN_10GBT_STAT_LP10G)
		lpa |= ADVERTISED_10000baseT_Full;

	mdio45_ethtool_gset_npage(&efx->mdio, ecmd, adv, lpa);

	/* In loopback, the PHY automatically brings up the correct interface,
	 * but doesn't advertise the correct speed. So override it */
	if (LOOPBACK_EXTERNAL(efx))
		ethtool_cmd_speed_set(ecmd, SPEED_10000);
}

static int tenxpress_set_settings(struct efx_nic *efx, struct ethtool_cmd *ecmd)
{
	if (!ecmd->autoneg)
		return -EINVAL;

	return efx_mdio_set_settings(efx, ecmd);
}

static void sfx7101_set_npage_adv(struct efx_nic *efx, u32 advertising)
{
	efx_mdio_set_flag(efx, MDIO_MMD_AN, MDIO_AN_10GBT_CTRL,
			  MDIO_AN_10GBT_CTRL_ADV10G,
			  advertising & ADVERTISED_10000baseT_Full);
}

const struct efx_phy_operations falcon_sfx7101_phy_ops = {
	.probe		  = tenxpress_phy_probe,
	.init             = tenxpress_phy_init,
	.reconfigure      = tenxpress_phy_reconfigure,
	.poll             = tenxpress_phy_poll,
	.fini             = sfx7101_phy_fini,
	.remove		  = tenxpress_phy_remove,
	.get_settings	  = tenxpress_get_settings,
	.set_settings	  = tenxpress_set_settings,
	.set_npage_adv    = sfx7101_set_npage_adv,
	.test_alive	  = efx_mdio_test_alive,
	.test_name	  = sfx7101_test_name,
	.run_tests	  = sfx7101_run_tests,
};
