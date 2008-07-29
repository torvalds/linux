/****************************************************************************
 * Driver for Solarflare Solarstorm network controllers and boards
 * Copyright 2006-2008 Solarflare Communications Inc.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 as published
 * by the Free Software Foundation, incorporated herein by reference.
 */
/*
 * Useful functions for working with MDIO clause 45 PHYs
 */
#include <linux/types.h>
#include <linux/ethtool.h>
#include <linux/delay.h>
#include "net_driver.h"
#include "mdio_10g.h"
#include "boards.h"

int mdio_clause45_reset_mmd(struct efx_nic *port, int mmd,
			    int spins, int spintime)
{
	u32 ctrl;
	int phy_id = port->mii.phy_id;

	/* Catch callers passing values in the wrong units (or just silly) */
	EFX_BUG_ON_PARANOID(spins * spintime >= 5000);

	mdio_clause45_write(port, phy_id, mmd, MDIO_MMDREG_CTRL1,
			    (1 << MDIO_MMDREG_CTRL1_RESET_LBN));
	/* Wait for the reset bit to clear. */
	do {
		msleep(spintime);
		ctrl = mdio_clause45_read(port, phy_id, mmd, MDIO_MMDREG_CTRL1);
		spins--;

	} while (spins && (ctrl & (1 << MDIO_MMDREG_CTRL1_RESET_LBN)));

	return spins ? spins : -ETIMEDOUT;
}

static int mdio_clause45_check_mmd(struct efx_nic *efx, int mmd,
				   int fault_fatal)
{
	int status;
	int phy_id = efx->mii.phy_id;

	if (LOOPBACK_INTERNAL(efx))
		return 0;

	/* Read MMD STATUS2 to check it is responding. */
	status = mdio_clause45_read(efx, phy_id, mmd, MDIO_MMDREG_STAT2);
	if (((status >> MDIO_MMDREG_STAT2_PRESENT_LBN) &
	     ((1 << MDIO_MMDREG_STAT2_PRESENT_WIDTH) - 1)) !=
	    MDIO_MMDREG_STAT2_PRESENT_VAL) {
		EFX_ERR(efx, "PHY MMD %d not responding.\n", mmd);
		return -EIO;
	}

	/* Read MMD STATUS 1 to check for fault. */
	status = mdio_clause45_read(efx, phy_id, mmd, MDIO_MMDREG_STAT1);
	if ((status & (1 << MDIO_MMDREG_STAT1_FAULT_LBN)) != 0) {
		if (fault_fatal) {
			EFX_ERR(efx, "PHY MMD %d reporting fatal"
				" fault: status %x\n", mmd, status);
			return -EIO;
		} else {
			EFX_LOG(efx, "PHY MMD %d reporting status"
				" %x (expected)\n", mmd, status);
		}
	}
	return 0;
}

/* This ought to be ridiculous overkill. We expect it to fail rarely */
#define MDIO45_RESET_TIME	1000 /* ms */
#define MDIO45_RESET_ITERS	100

int mdio_clause45_wait_reset_mmds(struct efx_nic *efx,
				  unsigned int mmd_mask)
{
	const int spintime = MDIO45_RESET_TIME / MDIO45_RESET_ITERS;
	int tries = MDIO45_RESET_ITERS;
	int rc = 0;
	int in_reset;

	while (tries) {
		int mask = mmd_mask;
		int mmd = 0;
		int stat;
		in_reset = 0;
		while (mask) {
			if (mask & 1) {
				stat = mdio_clause45_read(efx,
							  efx->mii.phy_id,
							  mmd,
							  MDIO_MMDREG_CTRL1);
				if (stat < 0) {
					EFX_ERR(efx, "failed to read status of"
						" MMD %d\n", mmd);
					return -EIO;
				}
				if (stat & (1 << MDIO_MMDREG_CTRL1_RESET_LBN))
					in_reset |= (1 << mmd);
			}
			mask = mask >> 1;
			mmd++;
		}
		if (!in_reset)
			break;
		tries--;
		msleep(spintime);
	}
	if (in_reset != 0) {
		EFX_ERR(efx, "not all MMDs came out of reset in time."
			" MMDs still in reset: %x\n", in_reset);
		rc = -ETIMEDOUT;
	}
	return rc;
}

int mdio_clause45_check_mmds(struct efx_nic *efx,
			     unsigned int mmd_mask, unsigned int fatal_mask)
{
	int devices, mmd = 0;
	int probe_mmd;

	/* Historically we have probed the PHYXS to find out what devices are
	 * present,but that doesn't work so well if the PHYXS isn't expected
	 * to exist, if so just find the first item in the list supplied. */
	probe_mmd = (mmd_mask & MDIO_MMDREG_DEVS0_PHYXS) ? MDIO_MMD_PHYXS :
	    __ffs(mmd_mask);
	devices = mdio_clause45_read(efx, efx->mii.phy_id,
				     probe_mmd, MDIO_MMDREG_DEVS0);

	/* Check all the expected MMDs are present */
	if (devices < 0) {
		EFX_ERR(efx, "failed to read devices present\n");
		return -EIO;
	}
	if ((devices & mmd_mask) != mmd_mask) {
		EFX_ERR(efx, "required MMDs not present: got %x, "
			"wanted %x\n", devices, mmd_mask);
		return -ENODEV;
	}
	EFX_TRACE(efx, "Devices present: %x\n", devices);

	/* Check all required MMDs are responding and happy. */
	while (mmd_mask) {
		if (mmd_mask & 1) {
			int fault_fatal = fatal_mask & 1;
			if (mdio_clause45_check_mmd(efx, mmd, fault_fatal))
				return -EIO;
		}
		mmd_mask = mmd_mask >> 1;
		fatal_mask = fatal_mask >> 1;
		mmd++;
	}

	return 0;
}

int mdio_clause45_links_ok(struct efx_nic *efx, unsigned int mmd_mask)
{
	int phy_id = efx->mii.phy_id;
	int status;
	int ok = 1;
	int mmd = 0;
	int good;

	/* If the port is in loopback, then we should only consider a subset
	 * of mmd's */
	if (LOOPBACK_INTERNAL(efx))
		return 1;
	else if (efx->loopback_mode == LOOPBACK_NETWORK)
		return 0;
	else if (efx->loopback_mode == LOOPBACK_PHYXS)
		mmd_mask &= ~(MDIO_MMDREG_DEVS0_PHYXS |
			      MDIO_MMDREG_DEVS0_PCS |
			      MDIO_MMDREG_DEVS0_PMAPMD);
	else if (efx->loopback_mode == LOOPBACK_PCS)
		mmd_mask &= ~(MDIO_MMDREG_DEVS0_PCS |
			      MDIO_MMDREG_DEVS0_PMAPMD);
	else if (efx->loopback_mode == LOOPBACK_PMAPMD)
		mmd_mask &= ~MDIO_MMDREG_DEVS0_PMAPMD;

	while (mmd_mask) {
		if (mmd_mask & 1) {
			/* Double reads because link state is latched, and a
			 * read moves the current state into the register */
			status = mdio_clause45_read(efx, phy_id,
						    mmd, MDIO_MMDREG_STAT1);
			status = mdio_clause45_read(efx, phy_id,
						    mmd, MDIO_MMDREG_STAT1);

			good = status & (1 << MDIO_MMDREG_STAT1_LINK_LBN);
			ok = ok && good;
		}
		mmd_mask = (mmd_mask >> 1);
		mmd++;
	}
	return ok;
}

void mdio_clause45_transmit_disable(struct efx_nic *efx)
{
	int phy_id = efx->mii.phy_id;
	int ctrl1, ctrl2;

	ctrl1 = ctrl2 = mdio_clause45_read(efx, phy_id, MDIO_MMD_PMAPMD,
					   MDIO_MMDREG_TXDIS);
	if (efx->tx_disabled)
		ctrl2 |= (1 << MDIO_MMDREG_TXDIS_GLOBAL_LBN);
	else
		ctrl1 &= ~(1 << MDIO_MMDREG_TXDIS_GLOBAL_LBN);
	if (ctrl1 != ctrl2)
		mdio_clause45_write(efx, phy_id, MDIO_MMD_PMAPMD,
				    MDIO_MMDREG_TXDIS, ctrl2);
}

void mdio_clause45_phy_reconfigure(struct efx_nic *efx)
{
	int phy_id = efx->mii.phy_id;
	int ctrl1, ctrl2;

	/* Handle (with debouncing) PMA/PMD loopback */
	ctrl1 = ctrl2 = mdio_clause45_read(efx, phy_id, MDIO_MMD_PMAPMD,
					   MDIO_MMDREG_CTRL1);

	if (efx->loopback_mode == LOOPBACK_PMAPMD)
		ctrl2 |= (1 << MDIO_PMAPMD_CTRL1_LBACK_LBN);
	else
		ctrl2 &= ~(1 << MDIO_PMAPMD_CTRL1_LBACK_LBN);

	if (ctrl1 != ctrl2)
		mdio_clause45_write(efx, phy_id, MDIO_MMD_PMAPMD,
				    MDIO_MMDREG_CTRL1, ctrl2);

	/* Handle (with debouncing) PCS loopback */
	ctrl1 = ctrl2 = mdio_clause45_read(efx, phy_id, MDIO_MMD_PCS,
					   MDIO_MMDREG_CTRL1);
	if (efx->loopback_mode == LOOPBACK_PCS)
		ctrl2 |= (1 << MDIO_MMDREG_CTRL1_LBACK_LBN);
	else
		ctrl2 &= ~(1 << MDIO_MMDREG_CTRL1_LBACK_LBN);

	if (ctrl1 != ctrl2)
		mdio_clause45_write(efx, phy_id, MDIO_MMD_PCS,
				    MDIO_MMDREG_CTRL1, ctrl2);

	/* Handle (with debouncing) PHYXS network loopback */
	ctrl1 = ctrl2 = mdio_clause45_read(efx, phy_id, MDIO_MMD_PHYXS,
					   MDIO_MMDREG_CTRL1);
	if (efx->loopback_mode == LOOPBACK_NETWORK)
		ctrl2 |= (1 << MDIO_MMDREG_CTRL1_LBACK_LBN);
	else
		ctrl2 &= ~(1 << MDIO_MMDREG_CTRL1_LBACK_LBN);

	if (ctrl1 != ctrl2)
		mdio_clause45_write(efx, phy_id, MDIO_MMD_PHYXS,
				    MDIO_MMDREG_CTRL1, ctrl2);
}

/**
 * mdio_clause45_get_settings - Read (some of) the PHY settings over MDIO.
 * @efx:		Efx NIC
 * @ecmd: 		Buffer for settings
 *
 * On return the 'port', 'speed', 'supported' and 'advertising' fields of
 * ecmd have been filled out based on the PMA type.
 */
void mdio_clause45_get_settings(struct efx_nic *efx,
				struct ethtool_cmd *ecmd)
{
	int pma_type;

	/* If no PMA is present we are presumably talking something XAUI-ish
	 * like CX4. Which we report as FIBRE (see below) */
	if ((efx->phy_op->mmds & DEV_PRESENT_BIT(MDIO_MMD_PMAPMD)) == 0) {
		ecmd->speed = SPEED_10000;
		ecmd->port = PORT_FIBRE;
		ecmd->supported = SUPPORTED_FIBRE;
		ecmd->advertising = ADVERTISED_FIBRE;
		return;
	}

	pma_type = mdio_clause45_read(efx, efx->mii.phy_id,
				      MDIO_MMD_PMAPMD, MDIO_MMDREG_CTRL2);
	pma_type &= MDIO_PMAPMD_CTRL2_TYPE_MASK;

	switch (pma_type) {
		/* We represent CX4 as fibre in the absence of anything
		   better. */
	case MDIO_PMAPMD_CTRL2_10G_CX4:
		ecmd->speed = SPEED_10000;
		ecmd->port = PORT_FIBRE;
		ecmd->supported = SUPPORTED_FIBRE;
		ecmd->advertising = ADVERTISED_FIBRE;
		break;
		/* 10G Base-T */
	case MDIO_PMAPMD_CTRL2_10G_BT:
		ecmd->speed = SPEED_10000;
		ecmd->port = PORT_TP;
		ecmd->supported = SUPPORTED_TP | SUPPORTED_10000baseT_Full;
		ecmd->advertising = (ADVERTISED_FIBRE
				     | ADVERTISED_10000baseT_Full);
		break;
	case MDIO_PMAPMD_CTRL2_1G_BT:
		ecmd->speed = SPEED_1000;
		ecmd->port = PORT_TP;
		ecmd->supported = SUPPORTED_TP | SUPPORTED_1000baseT_Full;
		ecmd->advertising = (ADVERTISED_FIBRE
				     | ADVERTISED_1000baseT_Full);
		break;
	case MDIO_PMAPMD_CTRL2_100_BT:
		ecmd->speed = SPEED_100;
		ecmd->port = PORT_TP;
		ecmd->supported = SUPPORTED_TP | SUPPORTED_100baseT_Full;
		ecmd->advertising = (ADVERTISED_FIBRE
				     | ADVERTISED_100baseT_Full);
		break;
	case MDIO_PMAPMD_CTRL2_10_BT:
		ecmd->speed = SPEED_10;
		ecmd->port = PORT_TP;
		ecmd->supported = SUPPORTED_TP | SUPPORTED_10baseT_Full;
		ecmd->advertising = ADVERTISED_FIBRE | ADVERTISED_10baseT_Full;
		break;
	/* All the other defined modes are flavours of
	 * 10G optical */
	default:
		ecmd->speed = SPEED_10000;
		ecmd->port = PORT_FIBRE;
		ecmd->supported = SUPPORTED_FIBRE;
		ecmd->advertising = ADVERTISED_FIBRE;
		break;
	}
}

/**
 * mdio_clause45_set_settings - Set (some of) the PHY settings over MDIO.
 * @efx:		Efx NIC
 * @ecmd: 		New settings
 *
 * Currently this just enforces that we are _not_ changing the
 * 'port', 'speed', 'supported' or 'advertising' settings as these
 * cannot be changed on any currently supported PHY.
 */
int mdio_clause45_set_settings(struct efx_nic *efx,
			       struct ethtool_cmd *ecmd)
{
	struct ethtool_cmd tmpcmd;
	mdio_clause45_get_settings(efx, &tmpcmd);
	/* None of the current PHYs support more than one mode
	 * of operation (and only 10GBT ever will), so keep things
	 * simple for now */
	if ((ecmd->speed == tmpcmd.speed) && (ecmd->port == tmpcmd.port) &&
	    (ecmd->supported == tmpcmd.supported) &&
	    (ecmd->advertising == tmpcmd.advertising))
		return 0;
	return -EOPNOTSUPP;
}
