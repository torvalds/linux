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
#include "workarounds.h"

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

	if (mmd != MDIO_MMD_AN) {
		/* Read MMD STATUS2 to check it is responding. */
		status = mdio_clause45_read(efx, phy_id, mmd,
					    MDIO_MMDREG_STAT2);
		if (((status >> MDIO_MMDREG_STAT2_PRESENT_LBN) &
		     ((1 << MDIO_MMDREG_STAT2_PRESENT_WIDTH) - 1)) !=
		    MDIO_MMDREG_STAT2_PRESENT_VAL) {
			EFX_ERR(efx, "PHY MMD %d not responding.\n", mmd);
			return -EIO;
		}
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
	u32 devices;
	int mmd = 0, probe_mmd;

	/* Historically we have probed the PHYXS to find out what devices are
	 * present,but that doesn't work so well if the PHYXS isn't expected
	 * to exist, if so just find the first item in the list supplied. */
	probe_mmd = (mmd_mask & MDIO_MMDREG_DEVS_PHYXS) ? MDIO_MMD_PHYXS :
	    __ffs(mmd_mask);
	devices = (mdio_clause45_read(efx, efx->mii.phy_id,
				      probe_mmd, MDIO_MMDREG_DEVS0) |
		   mdio_clause45_read(efx, efx->mii.phy_id,
				      probe_mmd, MDIO_MMDREG_DEVS1) << 16);

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

bool mdio_clause45_links_ok(struct efx_nic *efx, unsigned int mmd_mask)
{
	int phy_id = efx->mii.phy_id;
	u32 reg;
	bool ok = true;
	int mmd = 0;

	/* If the port is in loopback, then we should only consider a subset
	 * of mmd's */
	if (LOOPBACK_INTERNAL(efx))
		return true;
	else if (efx->loopback_mode == LOOPBACK_NETWORK)
		return false;
	else if (efx_phy_mode_disabled(efx->phy_mode))
		return false;
	else if (efx->loopback_mode == LOOPBACK_PHYXS)
		mmd_mask &= ~(MDIO_MMDREG_DEVS_PHYXS |
			      MDIO_MMDREG_DEVS_PCS |
			      MDIO_MMDREG_DEVS_PMAPMD |
			      MDIO_MMDREG_DEVS_AN);
	else if (efx->loopback_mode == LOOPBACK_PCS)
		mmd_mask &= ~(MDIO_MMDREG_DEVS_PCS |
			      MDIO_MMDREG_DEVS_PMAPMD |
			      MDIO_MMDREG_DEVS_AN);
	else if (efx->loopback_mode == LOOPBACK_PMAPMD)
		mmd_mask &= ~(MDIO_MMDREG_DEVS_PMAPMD |
			      MDIO_MMDREG_DEVS_AN);

	if (!mmd_mask) {
		/* Use presence of XGMII faults in leui of link state */
		reg = mdio_clause45_read(efx, phy_id, MDIO_MMD_PHYXS,
					 MDIO_PHYXS_STATUS2);
		return !(reg & (1 << MDIO_PHYXS_STATUS2_RX_FAULT_LBN));
	}

	while (mmd_mask) {
		if (mmd_mask & 1) {
			/* Double reads because link state is latched, and a
			 * read moves the current state into the register */
			reg = mdio_clause45_read(efx, phy_id,
						 mmd, MDIO_MMDREG_STAT1);
			reg = mdio_clause45_read(efx, phy_id,
						 mmd, MDIO_MMDREG_STAT1);
			ok = ok && (reg & (1 << MDIO_MMDREG_STAT1_LINK_LBN));
		}
		mmd_mask = (mmd_mask >> 1);
		mmd++;
	}
	return ok;
}

void mdio_clause45_transmit_disable(struct efx_nic *efx)
{
	mdio_clause45_set_flag(efx, efx->mii.phy_id, MDIO_MMD_PMAPMD,
			       MDIO_MMDREG_TXDIS, MDIO_MMDREG_TXDIS_GLOBAL_LBN,
			       efx->phy_mode & PHY_MODE_TX_DISABLED);
}

void mdio_clause45_phy_reconfigure(struct efx_nic *efx)
{
	int phy_id = efx->mii.phy_id;

	mdio_clause45_set_flag(efx, phy_id, MDIO_MMD_PMAPMD,
			       MDIO_MMDREG_CTRL1, MDIO_PMAPMD_CTRL1_LBACK_LBN,
			       efx->loopback_mode == LOOPBACK_PMAPMD);
	mdio_clause45_set_flag(efx, phy_id, MDIO_MMD_PCS,
			       MDIO_MMDREG_CTRL1, MDIO_MMDREG_CTRL1_LBACK_LBN,
			       efx->loopback_mode == LOOPBACK_PCS);
	mdio_clause45_set_flag(efx, phy_id, MDIO_MMD_PHYXS,
			       MDIO_MMDREG_CTRL1, MDIO_MMDREG_CTRL1_LBACK_LBN,
			       efx->loopback_mode == LOOPBACK_NETWORK);
}

static void mdio_clause45_set_mmd_lpower(struct efx_nic *efx,
					 int lpower, int mmd)
{
	int phy = efx->mii.phy_id;
	int stat = mdio_clause45_read(efx, phy, mmd, MDIO_MMDREG_STAT1);

	EFX_TRACE(efx, "Setting low power mode for MMD %d to %d\n",
		  mmd, lpower);

	if (stat & (1 << MDIO_MMDREG_STAT1_LPABLE_LBN)) {
		mdio_clause45_set_flag(efx, phy, mmd, MDIO_MMDREG_CTRL1,
				       MDIO_MMDREG_CTRL1_LPOWER_LBN, lpower);
	}
}

void mdio_clause45_set_mmds_lpower(struct efx_nic *efx,
				   int low_power, unsigned int mmd_mask)
{
	int mmd = 0;
	mmd_mask &= ~MDIO_MMDREG_DEVS_AN;
	while (mmd_mask) {
		if (mmd_mask & 1)
			mdio_clause45_set_mmd_lpower(efx, low_power, mmd);
		mmd_mask = (mmd_mask >> 1);
		mmd++;
	}
}

static u32 mdio_clause45_get_an(struct efx_nic *efx, u16 addr)
{
	int phy_id = efx->mii.phy_id;
	u32 result = 0;
	int reg;

	reg = mdio_clause45_read(efx, phy_id, MDIO_MMD_AN, addr);
	if (reg & ADVERTISE_10HALF)
		result |= ADVERTISED_10baseT_Half;
	if (reg & ADVERTISE_10FULL)
		result |= ADVERTISED_10baseT_Full;
	if (reg & ADVERTISE_100HALF)
		result |= ADVERTISED_100baseT_Half;
	if (reg & ADVERTISE_100FULL)
		result |= ADVERTISED_100baseT_Full;
	return result;
}

/**
 * mdio_clause45_get_settings - Read (some of) the PHY settings over MDIO.
 * @efx:		Efx NIC
 * @ecmd: 		Buffer for settings
 *
 * On return the 'port', 'speed', 'supported' and 'advertising' fields of
 * ecmd have been filled out.
 */
void mdio_clause45_get_settings(struct efx_nic *efx,
				struct ethtool_cmd *ecmd)
{
	mdio_clause45_get_settings_ext(efx, ecmd, 0, 0);
}

/**
 * mdio_clause45_get_settings_ext - Read (some of) the PHY settings over MDIO.
 * @efx:		Efx NIC
 * @ecmd: 		Buffer for settings
 * @xnp:		Advertised Extended Next Page state
 * @xnp_lpa:		Link Partner's advertised XNP state
 *
 * On return the 'port', 'speed', 'supported' and 'advertising' fields of
 * ecmd have been filled out.
 */
void mdio_clause45_get_settings_ext(struct efx_nic *efx,
				    struct ethtool_cmd *ecmd,
				    u32 npage_adv, u32 npage_lpa)
{
	int phy_id = efx->mii.phy_id;
	int reg;

	ecmd->transceiver = XCVR_INTERNAL;
	ecmd->phy_address = phy_id;

	reg = mdio_clause45_read(efx, phy_id, MDIO_MMD_PMAPMD,
				 MDIO_MMDREG_CTRL2);
	switch (reg & MDIO_PMAPMD_CTRL2_TYPE_MASK) {
	case MDIO_PMAPMD_CTRL2_10G_BT:
	case MDIO_PMAPMD_CTRL2_1G_BT:
	case MDIO_PMAPMD_CTRL2_100_BT:
	case MDIO_PMAPMD_CTRL2_10_BT:
		ecmd->port = PORT_TP;
		ecmd->supported = SUPPORTED_TP;
		reg = mdio_clause45_read(efx, phy_id, MDIO_MMD_PMAPMD,
					 MDIO_MMDREG_SPEED);
		if (reg & (1 << MDIO_MMDREG_SPEED_10G_LBN))
			ecmd->supported |= SUPPORTED_10000baseT_Full;
		if (reg & (1 << MDIO_MMDREG_SPEED_1000M_LBN))
			ecmd->supported |= (SUPPORTED_1000baseT_Full |
					    SUPPORTED_1000baseT_Half);
		if (reg & (1 << MDIO_MMDREG_SPEED_100M_LBN))
			ecmd->supported |= (SUPPORTED_100baseT_Full |
					    SUPPORTED_100baseT_Half);
		if (reg & (1 << MDIO_MMDREG_SPEED_10M_LBN))
			ecmd->supported |= (SUPPORTED_10baseT_Full |
					    SUPPORTED_10baseT_Half);
		ecmd->advertising = ADVERTISED_TP;
		break;

	/* We represent CX4 as fibre in the absence of anything better */
	case MDIO_PMAPMD_CTRL2_10G_CX4:
	/* All the other defined modes are flavours of optical */
	default:
		ecmd->port = PORT_FIBRE;
		ecmd->supported = SUPPORTED_FIBRE;
		ecmd->advertising = ADVERTISED_FIBRE;
		break;
	}

	if (efx->phy_op->mmds & DEV_PRESENT_BIT(MDIO_MMD_AN)) {
		ecmd->supported |= SUPPORTED_Autoneg;
		reg = mdio_clause45_read(efx, phy_id, MDIO_MMD_AN,
					 MDIO_MMDREG_CTRL1);
		if (reg & BMCR_ANENABLE) {
			ecmd->autoneg = AUTONEG_ENABLE;
			ecmd->advertising |=
				ADVERTISED_Autoneg |
				mdio_clause45_get_an(efx, MDIO_AN_ADVERTISE) |
				npage_adv;
		} else
			ecmd->autoneg = AUTONEG_DISABLE;
	} else
		ecmd->autoneg = AUTONEG_DISABLE;

	if (ecmd->autoneg) {
		/* If AN is complete, report best common mode,
		 * otherwise report best advertised mode. */
		u32 modes = 0;
		if (mdio_clause45_read(efx, phy_id, MDIO_MMD_AN,
				       MDIO_MMDREG_STAT1) &
		    (1 << MDIO_AN_STATUS_AN_DONE_LBN))
			modes = (ecmd->advertising &
				 (mdio_clause45_get_an(efx, MDIO_AN_LPA) |
				  npage_lpa));
		if (modes == 0)
			modes = ecmd->advertising;

		if (modes & ADVERTISED_10000baseT_Full) {
			ecmd->speed = SPEED_10000;
			ecmd->duplex = DUPLEX_FULL;
		} else if (modes & (ADVERTISED_1000baseT_Full |
				    ADVERTISED_1000baseT_Half)) {
			ecmd->speed = SPEED_1000;
			ecmd->duplex = !!(modes & ADVERTISED_1000baseT_Full);
		} else if (modes & (ADVERTISED_100baseT_Full |
				    ADVERTISED_100baseT_Half)) {
			ecmd->speed = SPEED_100;
			ecmd->duplex = !!(modes & ADVERTISED_100baseT_Full);
		} else {
			ecmd->speed = SPEED_10;
			ecmd->duplex = !!(modes & ADVERTISED_10baseT_Full);
		}
	} else {
		/* Report forced settings */
		reg = mdio_clause45_read(efx, phy_id, MDIO_MMD_PMAPMD,
					 MDIO_MMDREG_CTRL1);
		ecmd->speed = (((reg & BMCR_SPEED1000) ? 100 : 1) *
			       ((reg & BMCR_SPEED100) ? 100 : 10));
		ecmd->duplex = (reg & BMCR_FULLDPLX ||
				ecmd->speed == SPEED_10000);
	}
}

/**
 * mdio_clause45_set_settings - Set (some of) the PHY settings over MDIO.
 * @efx:		Efx NIC
 * @ecmd: 		New settings
 */
int mdio_clause45_set_settings(struct efx_nic *efx,
			       struct ethtool_cmd *ecmd)
{
	int phy_id = efx->mii.phy_id;
	struct ethtool_cmd prev;
	u32 required;
	int reg;

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
	if (ecmd->autoneg) {
		required = SUPPORTED_Autoneg;
	} else if (ecmd->duplex) {
		switch (ecmd->speed) {
		case SPEED_10:  required = SUPPORTED_10baseT_Full;  break;
		case SPEED_100: required = SUPPORTED_100baseT_Full; break;
		default:        return -EINVAL;
		}
	} else {
		switch (ecmd->speed) {
		case SPEED_10:  required = SUPPORTED_10baseT_Half;  break;
		case SPEED_100: required = SUPPORTED_100baseT_Half; break;
		default:        return -EINVAL;
		}
	}
	required |= ecmd->advertising;
	if (required & ~prev.supported)
		return -EINVAL;

	if (ecmd->autoneg) {
		bool xnp = (ecmd->advertising & ADVERTISED_10000baseT_Full
			    || EFX_WORKAROUND_13204(efx));

		/* Set up the base page */
		reg = ADVERTISE_CSMA;
		if (ecmd->advertising & ADVERTISED_10baseT_Half)
			reg |= ADVERTISE_10HALF;
		if (ecmd->advertising & ADVERTISED_10baseT_Full)
			reg |= ADVERTISE_10FULL;
		if (ecmd->advertising & ADVERTISED_100baseT_Half)
			reg |= ADVERTISE_100HALF;
		if (ecmd->advertising & ADVERTISED_100baseT_Full)
			reg |= ADVERTISE_100FULL;
		if (xnp)
			reg |= ADVERTISE_RESV;
		else if (ecmd->advertising & (ADVERTISED_1000baseT_Half |
					      ADVERTISED_1000baseT_Full))
			reg |= ADVERTISE_NPAGE;
		reg |= efx_fc_advertise(efx->wanted_fc);
		mdio_clause45_write(efx, phy_id, MDIO_MMD_AN,
				    MDIO_AN_ADVERTISE, reg);

		/* Set up the (extended) next page if necessary */
		if (efx->phy_op->set_npage_adv)
			efx->phy_op->set_npage_adv(efx, ecmd->advertising);

		/* Enable and restart AN */
		reg = mdio_clause45_read(efx, phy_id, MDIO_MMD_AN,
					 MDIO_MMDREG_CTRL1);
		reg |= BMCR_ANENABLE;
		if (!(EFX_WORKAROUND_15195(efx) &&
		      LOOPBACK_MASK(efx) & efx->phy_op->loopbacks))
			reg |= BMCR_ANRESTART;
		if (xnp)
			reg |= 1 << MDIO_AN_CTRL_XNP_LBN;
		else
			reg &= ~(1 << MDIO_AN_CTRL_XNP_LBN);
		mdio_clause45_write(efx, phy_id, MDIO_MMD_AN,
				    MDIO_MMDREG_CTRL1, reg);
	} else {
		/* Disable AN */
		mdio_clause45_set_flag(efx, phy_id, MDIO_MMD_AN,
				       MDIO_MMDREG_CTRL1,
				       __ffs(BMCR_ANENABLE), false);

		/* Set the basic control bits */
		reg = mdio_clause45_read(efx, phy_id, MDIO_MMD_PMAPMD,
					 MDIO_MMDREG_CTRL1);
		reg &= ~(BMCR_SPEED1000 | BMCR_SPEED100 | BMCR_FULLDPLX |
			 0x003c);
		if (ecmd->speed == SPEED_100)
			reg |= BMCR_SPEED100;
		if (ecmd->duplex)
			reg |= BMCR_FULLDPLX;
		mdio_clause45_write(efx, phy_id, MDIO_MMD_PMAPMD,
				    MDIO_MMDREG_CTRL1, reg);
	}

	return 0;
}

void mdio_clause45_set_pause(struct efx_nic *efx)
{
	int phy_id = efx->mii.phy_id;
	int reg;

	if (efx->phy_op->mmds & DEV_PRESENT_BIT(MDIO_MMD_AN)) {
		/* Set pause capability advertising */
		reg = mdio_clause45_read(efx, phy_id, MDIO_MMD_AN,
					 MDIO_AN_ADVERTISE);
		reg &= ~(ADVERTISE_PAUSE_CAP | ADVERTISE_PAUSE_ASYM);
		reg |= efx_fc_advertise(efx->wanted_fc);
		mdio_clause45_write(efx, phy_id, MDIO_MMD_AN,
				    MDIO_AN_ADVERTISE, reg);

		/* Restart auto-negotiation */
		reg = mdio_clause45_read(efx, phy_id, MDIO_MMD_AN,
					 MDIO_MMDREG_CTRL1);
		if (reg & BMCR_ANENABLE) {
			reg |= BMCR_ANRESTART;
			mdio_clause45_write(efx, phy_id, MDIO_MMD_AN,
					    MDIO_MMDREG_CTRL1, reg);
		}
	}
}

enum efx_fc_type mdio_clause45_get_pause(struct efx_nic *efx)
{
	int phy_id = efx->mii.phy_id;
	int lpa;

	if (!(efx->phy_op->mmds & DEV_PRESENT_BIT(MDIO_MMD_AN)))
		return efx->wanted_fc;
	lpa = mdio_clause45_read(efx, phy_id, MDIO_MMD_AN, MDIO_AN_LPA);
	return efx_fc_resolve(efx->wanted_fc, lpa);
}

void mdio_clause45_set_flag(struct efx_nic *efx, u8 prt, u8 dev,
			    u16 addr, int bit, bool sense)
{
	int old_val = mdio_clause45_read(efx, prt, dev, addr);
	int new_val;

	if (sense)
		new_val = old_val | (1 << bit);
	else
		new_val = old_val & ~(1 << bit);
	if (old_val != new_val)
		mdio_clause45_write(efx, prt, dev, addr, new_val);
}
