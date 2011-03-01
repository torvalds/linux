/****************************************************************************
 * Driver for Solarflare Solarstorm network controllers and boards
 * Copyright 2006-2011 Solarflare Communications Inc.
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
#include "workarounds.h"

unsigned efx_mdio_id_oui(u32 id)
{
	unsigned oui = 0;
	int i;

	/* The bits of the OUI are designated a..x, with a=0 and b variable.
	 * In the id register c is the MSB but the OUI is conventionally
	 * written as bytes h..a, p..i, x..q.  Reorder the bits accordingly. */
	for (i = 0; i < 22; ++i)
		if (id & (1 << (i + 10)))
			oui |= 1 << (i ^ 7);

	return oui;
}

int efx_mdio_reset_mmd(struct efx_nic *port, int mmd,
			    int spins, int spintime)
{
	u32 ctrl;

	/* Catch callers passing values in the wrong units (or just silly) */
	EFX_BUG_ON_PARANOID(spins * spintime >= 5000);

	efx_mdio_write(port, mmd, MDIO_CTRL1, MDIO_CTRL1_RESET);
	/* Wait for the reset bit to clear. */
	do {
		msleep(spintime);
		ctrl = efx_mdio_read(port, mmd, MDIO_CTRL1);
		spins--;

	} while (spins && (ctrl & MDIO_CTRL1_RESET));

	return spins ? spins : -ETIMEDOUT;
}

static int efx_mdio_check_mmd(struct efx_nic *efx, int mmd)
{
	int status;

	if (mmd != MDIO_MMD_AN) {
		/* Read MMD STATUS2 to check it is responding. */
		status = efx_mdio_read(efx, mmd, MDIO_STAT2);
		if ((status & MDIO_STAT2_DEVPRST) != MDIO_STAT2_DEVPRST_VAL) {
			netif_err(efx, hw, efx->net_dev,
				  "PHY MMD %d not responding.\n", mmd);
			return -EIO;
		}
	}

	return 0;
}

/* This ought to be ridiculous overkill. We expect it to fail rarely */
#define MDIO45_RESET_TIME	1000 /* ms */
#define MDIO45_RESET_ITERS	100

int efx_mdio_wait_reset_mmds(struct efx_nic *efx, unsigned int mmd_mask)
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
				stat = efx_mdio_read(efx, mmd, MDIO_CTRL1);
				if (stat < 0) {
					netif_err(efx, hw, efx->net_dev,
						  "failed to read status of"
						  " MMD %d\n", mmd);
					return -EIO;
				}
				if (stat & MDIO_CTRL1_RESET)
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
		netif_err(efx, hw, efx->net_dev,
			  "not all MMDs came out of reset in time."
			  " MMDs still in reset: %x\n", in_reset);
		rc = -ETIMEDOUT;
	}
	return rc;
}

int efx_mdio_check_mmds(struct efx_nic *efx, unsigned int mmd_mask)
{
	int mmd = 0, probe_mmd, devs1, devs2;
	u32 devices;

	/* Historically we have probed the PHYXS to find out what devices are
	 * present,but that doesn't work so well if the PHYXS isn't expected
	 * to exist, if so just find the first item in the list supplied. */
	probe_mmd = (mmd_mask & MDIO_DEVS_PHYXS) ? MDIO_MMD_PHYXS :
	    __ffs(mmd_mask);

	/* Check all the expected MMDs are present */
	devs1 = efx_mdio_read(efx, probe_mmd, MDIO_DEVS1);
	devs2 = efx_mdio_read(efx, probe_mmd, MDIO_DEVS2);
	if (devs1 < 0 || devs2 < 0) {
		netif_err(efx, hw, efx->net_dev,
			  "failed to read devices present\n");
		return -EIO;
	}
	devices = devs1 | (devs2 << 16);
	if ((devices & mmd_mask) != mmd_mask) {
		netif_err(efx, hw, efx->net_dev,
			  "required MMDs not present: got %x, wanted %x\n",
			  devices, mmd_mask);
		return -ENODEV;
	}
	netif_vdbg(efx, hw, efx->net_dev, "Devices present: %x\n", devices);

	/* Check all required MMDs are responding and happy. */
	while (mmd_mask) {
		if ((mmd_mask & 1) && efx_mdio_check_mmd(efx, mmd))
			return -EIO;
		mmd_mask = mmd_mask >> 1;
		mmd++;
	}

	return 0;
}

bool efx_mdio_links_ok(struct efx_nic *efx, unsigned int mmd_mask)
{
	/* If the port is in loopback, then we should only consider a subset
	 * of mmd's */
	if (LOOPBACK_INTERNAL(efx))
		return true;
	else if (LOOPBACK_MASK(efx) & LOOPBACKS_WS)
		return false;
	else if (efx_phy_mode_disabled(efx->phy_mode))
		return false;
	else if (efx->loopback_mode == LOOPBACK_PHYXS)
		mmd_mask &= ~(MDIO_DEVS_PHYXS |
			      MDIO_DEVS_PCS |
			      MDIO_DEVS_PMAPMD |
			      MDIO_DEVS_AN);
	else if (efx->loopback_mode == LOOPBACK_PCS)
		mmd_mask &= ~(MDIO_DEVS_PCS |
			      MDIO_DEVS_PMAPMD |
			      MDIO_DEVS_AN);
	else if (efx->loopback_mode == LOOPBACK_PMAPMD)
		mmd_mask &= ~(MDIO_DEVS_PMAPMD |
			      MDIO_DEVS_AN);

	return mdio45_links_ok(&efx->mdio, mmd_mask);
}

void efx_mdio_transmit_disable(struct efx_nic *efx)
{
	efx_mdio_set_flag(efx, MDIO_MMD_PMAPMD,
			  MDIO_PMA_TXDIS, MDIO_PMD_TXDIS_GLOBAL,
			  efx->phy_mode & PHY_MODE_TX_DISABLED);
}

void efx_mdio_phy_reconfigure(struct efx_nic *efx)
{
	efx_mdio_set_flag(efx, MDIO_MMD_PMAPMD,
			  MDIO_CTRL1, MDIO_PMA_CTRL1_LOOPBACK,
			  efx->loopback_mode == LOOPBACK_PMAPMD);
	efx_mdio_set_flag(efx, MDIO_MMD_PCS,
			  MDIO_CTRL1, MDIO_PCS_CTRL1_LOOPBACK,
			  efx->loopback_mode == LOOPBACK_PCS);
	efx_mdio_set_flag(efx, MDIO_MMD_PHYXS,
			  MDIO_CTRL1, MDIO_PHYXS_CTRL1_LOOPBACK,
			  efx->loopback_mode == LOOPBACK_PHYXS_WS);
}

static void efx_mdio_set_mmd_lpower(struct efx_nic *efx,
				    int lpower, int mmd)
{
	int stat = efx_mdio_read(efx, mmd, MDIO_STAT1);

	netif_vdbg(efx, drv, efx->net_dev, "Setting low power mode for MMD %d to %d\n",
		  mmd, lpower);

	if (stat & MDIO_STAT1_LPOWERABLE) {
		efx_mdio_set_flag(efx, mmd, MDIO_CTRL1,
				  MDIO_CTRL1_LPOWER, lpower);
	}
}

void efx_mdio_set_mmds_lpower(struct efx_nic *efx,
			      int low_power, unsigned int mmd_mask)
{
	int mmd = 0;
	mmd_mask &= ~MDIO_DEVS_AN;
	while (mmd_mask) {
		if (mmd_mask & 1)
			efx_mdio_set_mmd_lpower(efx, low_power, mmd);
		mmd_mask = (mmd_mask >> 1);
		mmd++;
	}
}

/**
 * efx_mdio_set_settings - Set (some of) the PHY settings over MDIO.
 * @efx:		Efx NIC
 * @ecmd: 		New settings
 */
int efx_mdio_set_settings(struct efx_nic *efx, struct ethtool_cmd *ecmd)
{
	struct ethtool_cmd prev;

	efx->phy_op->get_settings(efx, &prev);

	if (ecmd->advertising == prev.advertising &&
	    ecmd->speed == prev.speed &&
	    ecmd->duplex == prev.duplex &&
	    ecmd->port == prev.port &&
	    ecmd->autoneg == prev.autoneg)
		return 0;

	/* We can only change these settings for -T PHYs */
	if (prev.port != PORT_TP || ecmd->port != PORT_TP)
		return -EINVAL;

	/* Check that PHY supports these settings */
	if (!ecmd->autoneg ||
	    (ecmd->advertising | SUPPORTED_Autoneg) & ~prev.supported)
		return -EINVAL;

	efx_link_set_advertising(efx, ecmd->advertising | ADVERTISED_Autoneg);
	efx_mdio_an_reconfigure(efx);
	return 0;
}

/**
 * efx_mdio_an_reconfigure - Push advertising flags and restart autonegotiation
 * @efx:		Efx NIC
 */
void efx_mdio_an_reconfigure(struct efx_nic *efx)
{
	int reg;

	WARN_ON(!(efx->mdio.mmds & MDIO_DEVS_AN));

	/* Set up the base page */
	reg = ADVERTISE_CSMA | ADVERTISE_RESV;
	if (efx->link_advertising & ADVERTISED_Pause)
		reg |= ADVERTISE_PAUSE_CAP;
	if (efx->link_advertising & ADVERTISED_Asym_Pause)
		reg |= ADVERTISE_PAUSE_ASYM;
	efx_mdio_write(efx, MDIO_MMD_AN, MDIO_AN_ADVERTISE, reg);

	/* Set up the (extended) next page */
	efx->phy_op->set_npage_adv(efx, efx->link_advertising);

	/* Enable and restart AN */
	reg = efx_mdio_read(efx, MDIO_MMD_AN, MDIO_CTRL1);
	reg |= MDIO_AN_CTRL1_ENABLE | MDIO_AN_CTRL1_RESTART | MDIO_AN_CTRL1_XNP;
	efx_mdio_write(efx, MDIO_MMD_AN, MDIO_CTRL1, reg);
}

enum efx_fc_type efx_mdio_get_pause(struct efx_nic *efx)
{
	BUILD_BUG_ON(EFX_FC_AUTO & (EFX_FC_RX | EFX_FC_TX));

	if (!(efx->wanted_fc & EFX_FC_AUTO))
		return efx->wanted_fc;

	WARN_ON(!(efx->mdio.mmds & MDIO_DEVS_AN));

	return mii_resolve_flowctrl_fdx(
		mii_advertise_flowctrl(efx->wanted_fc),
		efx_mdio_read(efx, MDIO_MMD_AN, MDIO_AN_LPA));
}

int efx_mdio_test_alive(struct efx_nic *efx)
{
	int rc;
	int devad = __ffs(efx->mdio.mmds);
	u16 physid1, physid2;

	mutex_lock(&efx->mac_lock);

	physid1 = efx_mdio_read(efx, devad, MDIO_DEVID1);
	physid2 = efx_mdio_read(efx, devad, MDIO_DEVID2);

	if ((physid1 == 0x0000) || (physid1 == 0xffff) ||
	    (physid2 == 0x0000) || (physid2 == 0xffff)) {
		netif_err(efx, hw, efx->net_dev,
			  "no MDIO PHY present with ID %d\n", efx->mdio.prtad);
		rc = -EINVAL;
	} else {
		rc = efx_mdio_check_mmds(efx, efx->mdio.mmds);
	}

	mutex_unlock(&efx->mac_lock);
	return rc;
}
