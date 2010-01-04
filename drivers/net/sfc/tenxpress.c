/****************************************************************************
 * Driver for Solarflare Solarstorm network controllers and boards
 * Copyright 2007-2009 Solarflare Communications Inc.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 as published
 * by the Free Software Foundation, incorporated herein by reference.
 */

#include <linux/delay.h>
#include <linux/rtnetlink.h>
#include <linux/seq_file.h>
#include "efx.h"
#include "mdio_10g.h"
#include "nic.h"
#include "phy.h"
#include "regs.h"
#include "workarounds.h"
#include "selftest.h"

/* We expect these MMDs to be in the package.  SFT9001 also has a
 * clause 22 extension MMD, but since it doesn't have all the generic
 * MMD registers it is pointless to include it here.
 */
#define TENXPRESS_REQUIRED_DEVS (MDIO_DEVS_PMAPMD	| \
				 MDIO_DEVS_PCS		| \
				 MDIO_DEVS_PHYXS	| \
				 MDIO_DEVS_AN)

#define SFX7101_LOOPBACKS ((1 << LOOPBACK_PHYXS) |	\
			   (1 << LOOPBACK_PCS) |	\
			   (1 << LOOPBACK_PMAPMD) |	\
			   (1 << LOOPBACK_PHYXS_WS))

#define SFT9001_LOOPBACKS ((1 << LOOPBACK_GPHY) |	\
			   (1 << LOOPBACK_PHYXS) |	\
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
#define PMA_PMD_LNPGA_POWERDOWN_LBN 8	/* SFX7101 only */
#define PMA_PMD_LNPGA_POWERDOWN_WIDTH 1
#define PMA_PMD_EXT_CLK312_LBN	8	/* SFT9001 only */
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
#define SFT9001_PMA_PMD_LED_DEFAULT 0
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

/* Cable diagnostics - SFT9001 only */
#define PMA_PMD_CDIAG_CTRL_REG  49213
#define CDIAG_CTRL_IMMED_LBN    15
#define CDIAG_CTRL_BRK_LINK_LBN 12
#define CDIAG_CTRL_IN_PROG_LBN  11
#define CDIAG_CTRL_LEN_UNIT_LBN 10
#define CDIAG_CTRL_LEN_METRES   1
#define PMA_PMD_CDIAG_RES_REG   49174
#define CDIAG_RES_A_LBN         12
#define CDIAG_RES_B_LBN         8
#define CDIAG_RES_C_LBN         4
#define CDIAG_RES_D_LBN         0
#define CDIAG_RES_WIDTH         4
#define CDIAG_RES_OPEN          2
#define CDIAG_RES_OK            1
#define CDIAG_RES_INVALID       0
/* Set of 4 registers for pairs A-D */
#define PMA_PMD_CDIAG_LEN_REG   49175

/* Serdes control registers - SFT9001 only */
#define PMA_PMD_CSERDES_CTRL_REG 64258
/* Set the 156.25 MHz output to 312.5 MHz to drive Falcon's XMAC */
#define PMA_PMD_CSERDES_DEFAULT	0x000f

/* Misc register defines - SFX7101 only */
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
#define GPHY_DUPLEX_LBN	  	8
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

static ssize_t show_phy_short_reach(struct device *dev,
				    struct device_attribute *attr, char *buf)
{
	struct efx_nic *efx = pci_get_drvdata(to_pci_dev(dev));
	int reg;

	reg = efx_mdio_read(efx, MDIO_MMD_PMAPMD, MDIO_PMA_10GBT_TXPWR);
	return sprintf(buf, "%d\n", !!(reg & MDIO_PMA_10GBT_TXPWR_SHORT));
}

static ssize_t set_phy_short_reach(struct device *dev,
				   struct device_attribute *attr,
				   const char *buf, size_t count)
{
	struct efx_nic *efx = pci_get_drvdata(to_pci_dev(dev));
	int rc;

	rtnl_lock();
	if (efx->state != STATE_RUNNING) {
		rc = -EBUSY;
	} else {
		efx_mdio_set_flag(efx, MDIO_MMD_PMAPMD, MDIO_PMA_10GBT_TXPWR,
				  MDIO_PMA_10GBT_TXPWR_SHORT,
				  count != 0 && *buf != '0');
		rc = efx_reconfigure_port(efx);
	}
	rtnl_unlock();

	return rc < 0 ? rc : (ssize_t)count;
}

static DEVICE_ATTR(phy_short_reach, 0644, show_phy_short_reach,
		   set_phy_short_reach);

int sft9001_wait_boot(struct efx_nic *efx)
{
	unsigned long timeout = jiffies + HZ + 1;
	int boot_stat;

	for (;;) {
		boot_stat = efx_mdio_read(efx, MDIO_MMD_PCS,
					  PCS_BOOT_STATUS_REG);
		if (boot_stat >= 0) {
			EFX_LOG(efx, "PHY boot status = %#x\n", boot_stat);
			switch (boot_stat &
				((1 << PCS_BOOT_FATAL_ERROR_LBN) |
				 (3 << PCS_BOOT_PROGRESS_LBN) |
				 (1 << PCS_BOOT_DOWNLOAD_WAIT_LBN) |
				 (1 << PCS_BOOT_CODE_STARTED_LBN))) {
			case ((1 << PCS_BOOT_FATAL_ERROR_LBN) |
			      (PCS_BOOT_PROGRESS_CHECKSUM <<
			       PCS_BOOT_PROGRESS_LBN)):
			case ((1 << PCS_BOOT_FATAL_ERROR_LBN) |
			      (PCS_BOOT_PROGRESS_INIT <<
			       PCS_BOOT_PROGRESS_LBN) |
			      (1 << PCS_BOOT_DOWNLOAD_WAIT_LBN)):
				return -EINVAL;
			case ((PCS_BOOT_PROGRESS_WAIT_MDIO <<
			       PCS_BOOT_PROGRESS_LBN) |
			      (1 << PCS_BOOT_DOWNLOAD_WAIT_LBN)):
				return (efx->phy_mode & PHY_MODE_SPECIAL) ?
					0 : -EIO;
			case ((PCS_BOOT_PROGRESS_JUMP <<
			       PCS_BOOT_PROGRESS_LBN) |
			      (1 << PCS_BOOT_CODE_STARTED_LBN)):
			case ((PCS_BOOT_PROGRESS_JUMP <<
			       PCS_BOOT_PROGRESS_LBN) |
			      (1 << PCS_BOOT_DOWNLOAD_WAIT_LBN) |
			      (1 << PCS_BOOT_CODE_STARTED_LBN)):
				return (efx->phy_mode & PHY_MODE_SPECIAL) ?
					-EIO : 0;
			default:
				if (boot_stat & (1 << PCS_BOOT_FATAL_ERROR_LBN))
					return -EIO;
				break;
			}
		}

		if (time_after_eq(jiffies, timeout))
			return -ETIMEDOUT;

		msleep(50);
	}
}

static int tenxpress_init(struct efx_nic *efx)
{
	int reg;

	if (efx->phy_type == PHY_TYPE_SFX7101) {
		/* Enable 312.5 MHz clock */
		efx_mdio_write(efx, MDIO_MMD_PCS, PCS_TEST_SELECT_REG,
			       1 << CLK312_EN_LBN);
	} else {
		/* Enable 312.5 MHz clock and GMII */
		reg = efx_mdio_read(efx, MDIO_MMD_PMAPMD, PMA_PMD_XCONTROL_REG);
		reg |= ((1 << PMA_PMD_EXT_GMII_EN_LBN) |
			(1 << PMA_PMD_EXT_CLK_OUT_LBN) |
			(1 << PMA_PMD_EXT_CLK312_LBN) |
			(1 << PMA_PMD_EXT_ROBUST_LBN));

		efx_mdio_write(efx, MDIO_MMD_PMAPMD, PMA_PMD_XCONTROL_REG, reg);
		efx_mdio_set_flag(efx, MDIO_MMD_C22EXT,
			      GPHY_XCONTROL_REG, 1 << GPHY_ISOLATE_LBN,
			      false);
	}

	/* Set the LEDs up as: Green = Link, Amber = Link/Act, Red = Off */
	if (efx->phy_type == PHY_TYPE_SFX7101) {
		efx_mdio_set_flag(efx, MDIO_MMD_PMAPMD, PMA_PMD_LED_CTRL_REG,
				  1 << PMA_PMA_LED_ACTIVITY_LBN, true);
		efx_mdio_write(efx, MDIO_MMD_PMAPMD, PMA_PMD_LED_OVERR_REG,
			       SFX7101_PMA_PMD_LED_DEFAULT);
	}

	return 0;
}

static int tenxpress_phy_probe(struct efx_nic *efx)
{
	struct tenxpress_phy_data *phy_data;
	int rc;

	/* Allocate phy private storage */
	phy_data = kzalloc(sizeof(*phy_data), GFP_KERNEL);
	if (!phy_data)
		return -ENOMEM;
	efx->phy_data = phy_data;
	phy_data->phy_mode = efx->phy_mode;

	/* Create any special files */
	if (efx->phy_type == PHY_TYPE_SFT9001B) {
		rc = device_create_file(&efx->pci_dev->dev,
					&dev_attr_phy_short_reach);
		if (rc)
			goto fail;
	}

	if (efx->phy_type == PHY_TYPE_SFX7101) {
		efx->mdio.mmds = TENXPRESS_REQUIRED_DEVS;
		efx->mdio.mode_support = MDIO_SUPPORTS_C45;

		efx->loopback_modes = SFX7101_LOOPBACKS | FALCON_XMAC_LOOPBACKS;

		efx->link_advertising = (ADVERTISED_TP | ADVERTISED_Autoneg |
					 ADVERTISED_10000baseT_Full);
	} else {
		efx->mdio.mmds = TENXPRESS_REQUIRED_DEVS;
		efx->mdio.mode_support = MDIO_SUPPORTS_C45 | MDIO_EMULATE_C22;

		efx->loopback_modes = (SFT9001_LOOPBACKS |
				       FALCON_XMAC_LOOPBACKS | 
				       FALCON_GMAC_LOOPBACKS);

		efx->link_advertising = (ADVERTISED_TP | ADVERTISED_Autoneg |
					 ADVERTISED_10000baseT_Full |
					 ADVERTISED_1000baseT_Full |
					 ADVERTISED_100baseT_Full);
	}

	return 0;

fail:
	kfree(efx->phy_data);
	efx->phy_data = NULL;
	return rc;
}

static int tenxpress_phy_init(struct efx_nic *efx)
{
	int rc;

	falcon_board(efx)->type->init_phy(efx);

	if (!(efx->phy_mode & PHY_MODE_SPECIAL)) {
		if (efx->phy_type == PHY_TYPE_SFT9001A) {
			int reg;
			reg = efx_mdio_read(efx, MDIO_MMD_PMAPMD,
					    PMA_PMD_XCONTROL_REG);
			reg |= (1 << PMA_PMD_EXT_SSR_LBN);
			efx_mdio_write(efx, MDIO_MMD_PMAPMD,
				       PMA_PMD_XCONTROL_REG, reg);
			mdelay(200);
		}

		rc = efx_mdio_wait_reset_mmds(efx, TENXPRESS_REQUIRED_DEVS);
		if (rc < 0)
			return rc;

		rc = efx_mdio_check_mmds(efx, TENXPRESS_REQUIRED_DEVS, 0);
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

	/* The XGMAC clock is driven from the SFC7101/SFT9001 312MHz clock, so
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
			EFX_ERR(efx, "appears to be plugged into a port"
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

static bool sft9001_link_ok(struct efx_nic *efx, struct ethtool_cmd *ecmd)
{
	u32 reg;

	if (efx_phy_mode_disabled(efx->phy_mode))
		return false;
	else if (efx->loopback_mode == LOOPBACK_GPHY)
		return true;
	else if (efx->loopback_mode)
		return efx_mdio_links_ok(efx,
					 MDIO_DEVS_PMAPMD |
					 MDIO_DEVS_PHYXS);

	/* We must use the same definition of link state as LASI,
	 * otherwise we can miss a link state transition
	 */
	if (ecmd->speed == 10000) {
		reg = efx_mdio_read(efx, MDIO_MMD_PCS, MDIO_PCS_10GBRT_STAT1);
		return reg & MDIO_PCS_10GBRT_STAT1_BLKLK;
	} else {
		reg = efx_mdio_read(efx, MDIO_MMD_C22EXT, C22EXT_STATUS_REG);
		return reg & (1 << C22EXT_STATUS_LINK_LBN);
	}
}

static void tenxpress_ext_loopback(struct efx_nic *efx)
{
	efx_mdio_set_flag(efx, MDIO_MMD_PHYXS, PHYXS_TEST1,
			  1 << LOOPBACK_NEAR_LBN,
			  efx->loopback_mode == LOOPBACK_PHYXS);
	if (efx->phy_type != PHY_TYPE_SFX7101)
		efx_mdio_set_flag(efx, MDIO_MMD_C22EXT, GPHY_XCONTROL_REG,
				  1 << GPHY_LOOPBACK_NEAR_LBN,
				  efx->loopback_mode == LOOPBACK_GPHY);
}

static void tenxpress_low_power(struct efx_nic *efx)
{
	if (efx->phy_type == PHY_TYPE_SFX7101)
		efx_mdio_set_mmds_lpower(
			efx, !!(efx->phy_mode & PHY_MODE_LOW_POWER),
			TENXPRESS_REQUIRED_DEVS);
	else
		efx_mdio_set_flag(
			efx, MDIO_MMD_PMAPMD, PMA_PMD_XCONTROL_REG,
			1 << PMA_PMD_EXT_LPOWER_LBN,
			!!(efx->phy_mode & PHY_MODE_LOW_POWER));
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

		/* Reset XAUI if we were in 10G, and are staying
		 * in 10G. If we're moving into and out of 10G
		 * then xaui will be reset anyway */
		if (EFX_IS10G(efx))
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

	if (efx->phy_type == PHY_TYPE_SFX7101) {
		efx->link_state.up = sfx7101_link_ok(efx);
		efx->link_state.speed = 10000;
		efx->link_state.fd = true;
		efx->link_state.fc = efx_mdio_get_pause(efx);

		sfx7101_check_bad_lp(efx, efx->link_state.up);
	} else {
		struct ethtool_cmd ecmd;

		/* Check the LASI alarm first */
		if (efx->loopback_mode == LOOPBACK_NONE &&
		    !(efx_mdio_read(efx, MDIO_MMD_PMAPMD, MDIO_PMA_LASI_STAT) &
		      MDIO_PMA_LASI_LSALARM))
			return false;

		tenxpress_get_settings(efx, &ecmd);

		efx->link_state.up = sft9001_link_ok(efx, &ecmd);
		efx->link_state.speed = ecmd.speed;
		efx->link_state.fd = (ecmd.duplex == DUPLEX_FULL);
		efx->link_state.fc = efx_mdio_get_pause(efx);
	}

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
	if (efx->phy_type == PHY_TYPE_SFT9001B)
		device_remove_file(&efx->pci_dev->dev,
				   &dev_attr_phy_short_reach);

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
		if (efx->phy_type == PHY_TYPE_SFX7101)
			reg = SFX7101_PMA_PMD_LED_DEFAULT;
		else
			reg = SFT9001_PMA_PMD_LED_DEFAULT;
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

static const char *const sft9001_test_names[] = {
	"bist",
	"cable.pairA.status",
	"cable.pairB.status",
	"cable.pairC.status",
	"cable.pairD.status",
	"cable.pairA.length",
	"cable.pairB.length",
	"cable.pairC.length",
	"cable.pairD.length",
};

static const char *sft9001_test_name(struct efx_nic *efx, unsigned int index)
{
	if (index < ARRAY_SIZE(sft9001_test_names))
		return sft9001_test_names[index];
	return NULL;
}

static int sft9001_run_tests(struct efx_nic *efx, int *results, unsigned flags)
{
	int rc = 0, rc2, i, ctrl_reg, res_reg;

	/* Initialise cable diagnostic results to unknown failure */
	for (i = 1; i < 9; ++i)
		results[i] = -1;

	/* Run cable diagnostics; wait up to 5 seconds for them to complete.
	 * A cable fault is not a self-test failure, but a timeout is. */
	ctrl_reg = ((1 << CDIAG_CTRL_IMMED_LBN) |
		    (CDIAG_CTRL_LEN_METRES << CDIAG_CTRL_LEN_UNIT_LBN));
	if (flags & ETH_TEST_FL_OFFLINE) {
		/* Break the link in order to run full diagnostics.  We
		 * must reset the PHY to resume normal service. */
		ctrl_reg |= (1 << CDIAG_CTRL_BRK_LINK_LBN);
	}
	efx_mdio_write(efx, MDIO_MMD_PMAPMD, PMA_PMD_CDIAG_CTRL_REG,
		       ctrl_reg);
	i = 0;
	while (efx_mdio_read(efx, MDIO_MMD_PMAPMD, PMA_PMD_CDIAG_CTRL_REG) &
	       (1 << CDIAG_CTRL_IN_PROG_LBN)) {
		if (++i == 50) {
			rc = -ETIMEDOUT;
			goto out;
		}
		msleep(100);
	}
	res_reg = efx_mdio_read(efx, MDIO_MMD_PMAPMD, PMA_PMD_CDIAG_RES_REG);
	for (i = 0; i < 4; i++) {
		int pair_res =
			(res_reg >> (CDIAG_RES_A_LBN - i * CDIAG_RES_WIDTH))
			& ((1 << CDIAG_RES_WIDTH) - 1);
		int len_reg = efx_mdio_read(efx, MDIO_MMD_PMAPMD,
					    PMA_PMD_CDIAG_LEN_REG + i);
		if (pair_res == CDIAG_RES_OK)
			results[1 + i] = 1;
		else if (pair_res == CDIAG_RES_INVALID)
			results[1 + i] = -1;
		else
			results[1 + i] = -pair_res;
		if (pair_res != CDIAG_RES_INVALID &&
		    pair_res != CDIAG_RES_OPEN &&
		    len_reg != 0xffff)
			results[5 + i] = len_reg;
	}

out:
	if (flags & ETH_TEST_FL_OFFLINE) {
		/* Reset, running the BIST and then resuming normal service. */
		rc2 = tenxpress_special_reset(efx);
		results[0] = rc2 ? -1 : 1;
		if (!rc)
			rc = rc2;

		efx_mdio_an_reconfigure(efx);
	}

	return rc;
}

static void
tenxpress_get_settings(struct efx_nic *efx, struct ethtool_cmd *ecmd)
{
	u32 adv = 0, lpa = 0;
	int reg;

	if (efx->phy_type != PHY_TYPE_SFX7101) {
		reg = efx_mdio_read(efx, MDIO_MMD_C22EXT, C22EXT_MSTSLV_CTRL);
		if (reg & (1 << C22EXT_MSTSLV_CTRL_ADV_1000_FD_LBN))
			adv |= ADVERTISED_1000baseT_Full;
		reg = efx_mdio_read(efx, MDIO_MMD_C22EXT, C22EXT_MSTSLV_STATUS);
		if (reg & (1 << C22EXT_MSTSLV_STATUS_LP_1000_HD_LBN))
			lpa |= ADVERTISED_1000baseT_Half;
		if (reg & (1 << C22EXT_MSTSLV_STATUS_LP_1000_FD_LBN))
			lpa |= ADVERTISED_1000baseT_Full;
	}
	reg = efx_mdio_read(efx, MDIO_MMD_AN, MDIO_AN_10GBT_CTRL);
	if (reg & MDIO_AN_10GBT_CTRL_ADV10G)
		adv |= ADVERTISED_10000baseT_Full;
	reg = efx_mdio_read(efx, MDIO_MMD_AN, MDIO_AN_10GBT_STAT);
	if (reg & MDIO_AN_10GBT_STAT_LP10G)
		lpa |= ADVERTISED_10000baseT_Full;

	mdio45_ethtool_gset_npage(&efx->mdio, ecmd, adv, lpa);

	if (efx->phy_type != PHY_TYPE_SFX7101) {
		ecmd->supported |= (SUPPORTED_100baseT_Full |
				    SUPPORTED_1000baseT_Full);
		if (ecmd->speed != SPEED_10000) {
			ecmd->eth_tp_mdix =
				(efx_mdio_read(efx, MDIO_MMD_PMAPMD,
					       PMA_PMD_XSTATUS_REG) &
				 (1 << PMA_PMD_XSTAT_MDIX_LBN))
				? ETH_TP_MDI_X : ETH_TP_MDI;
		}
	}

	/* In loopback, the PHY automatically brings up the correct interface,
	 * but doesn't advertise the correct speed. So override it */
	if (efx->loopback_mode == LOOPBACK_GPHY)
		ecmd->speed = SPEED_1000;
	else if (LOOPBACK_EXTERNAL(efx))
		ecmd->speed = SPEED_10000;
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

static void sft9001_set_npage_adv(struct efx_nic *efx, u32 advertising)
{
	efx_mdio_set_flag(efx, MDIO_MMD_C22EXT, C22EXT_MSTSLV_CTRL,
			  1 << C22EXT_MSTSLV_CTRL_ADV_1000_FD_LBN,
			  advertising & ADVERTISED_1000baseT_Full);
	efx_mdio_set_flag(efx, MDIO_MMD_AN, MDIO_AN_10GBT_CTRL,
			  MDIO_AN_10GBT_CTRL_ADV10G,
			  advertising & ADVERTISED_10000baseT_Full);
}

struct efx_phy_operations falcon_sfx7101_phy_ops = {
	.probe		  = tenxpress_phy_probe,
	.init             = tenxpress_phy_init,
	.reconfigure      = tenxpress_phy_reconfigure,
	.poll             = tenxpress_phy_poll,
	.fini             = sfx7101_phy_fini,
	.remove		  = tenxpress_phy_remove,
	.get_settings	  = tenxpress_get_settings,
	.set_settings	  = tenxpress_set_settings,
	.set_npage_adv    = sfx7101_set_npage_adv,
	.test_name	  = sfx7101_test_name,
	.run_tests	  = sfx7101_run_tests,
};

struct efx_phy_operations falcon_sft9001_phy_ops = {
	.probe		  = tenxpress_phy_probe,
	.init             = tenxpress_phy_init,
	.reconfigure      = tenxpress_phy_reconfigure,
	.poll             = tenxpress_phy_poll,
	.fini             = efx_port_dummy_op_void,
	.remove		  = tenxpress_phy_remove,
	.get_settings	  = tenxpress_get_settings,
	.set_settings	  = tenxpress_set_settings,
	.set_npage_adv    = sft9001_set_npage_adv,
	.test_name	  = sft9001_test_name,
	.run_tests	  = sft9001_run_tests,
};
