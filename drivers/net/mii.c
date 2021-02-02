/*

	mii.c: MII interface library

	Maintained by Jeff Garzik <jgarzik@pobox.com>
	Copyright 2001,2002 Jeff Garzik

	Various code came from myson803.c and other files by
	Donald Becker.  Copyright:

		Written 1998-2002 by Donald Becker.

		This software may be used and distributed according
		to the terms of the GNU General Public License (GPL),
		incorporated herein by reference.  Drivers based on
		or derived from this code fall under the GPL and must
		retain the authorship, copyright and license notice.
		This file is not a complete program and may only be
		used when the entire operating system is licensed
		under the GPL.

		The author may be reached as becker@scyld.com, or C/O
		Scyld Computing Corporation
		410 Severn Ave., Suite 210
		Annapolis MD 21403


 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/netdevice.h>
#include <linux/ethtool.h>
#include <linux/mii.h>

static u32 mii_get_an(struct mii_if_info *mii, u16 addr)
{
	int advert;

	advert = mii->mdio_read(mii->dev, mii->phy_id, addr);

	return mii_lpa_to_ethtool_lpa_t(advert);
}

/**
 * mii_ethtool_gset - get settings that are specified in @ecmd
 * @mii: MII interface
 * @ecmd: requested ethtool_cmd
 *
 * The @ecmd parameter is expected to have been cleared before calling
 * mii_ethtool_gset().
 *
 * Returns 0 for success, negative on error.
 */
int mii_ethtool_gset(struct mii_if_info *mii, struct ethtool_cmd *ecmd)
{
	struct net_device *dev = mii->dev;
	u16 bmcr, bmsr, ctrl1000 = 0, stat1000 = 0;
	u32 nego;

	ecmd->supported =
	    (SUPPORTED_10baseT_Half | SUPPORTED_10baseT_Full |
	     SUPPORTED_100baseT_Half | SUPPORTED_100baseT_Full |
	     SUPPORTED_Autoneg | SUPPORTED_TP | SUPPORTED_MII);
	if (mii->supports_gmii)
		ecmd->supported |= SUPPORTED_1000baseT_Half |
			SUPPORTED_1000baseT_Full;

	/* only supports twisted-pair */
	ecmd->port = PORT_MII;

	/* only supports internal transceiver */
	ecmd->transceiver = XCVR_INTERNAL;

	/* this isn't fully supported at higher layers */
	ecmd->phy_address = mii->phy_id;
	ecmd->mdio_support = ETH_MDIO_SUPPORTS_C22;

	ecmd->advertising = ADVERTISED_TP | ADVERTISED_MII;

	bmcr = mii->mdio_read(dev, mii->phy_id, MII_BMCR);
	bmsr = mii->mdio_read(dev, mii->phy_id, MII_BMSR);
	if (mii->supports_gmii) {
 		ctrl1000 = mii->mdio_read(dev, mii->phy_id, MII_CTRL1000);
		stat1000 = mii->mdio_read(dev, mii->phy_id, MII_STAT1000);
	}

	ecmd->advertising |= mii_get_an(mii, MII_ADVERTISE);
	if (mii->supports_gmii)
		ecmd->advertising |=
			mii_ctrl1000_to_ethtool_adv_t(ctrl1000);

	if (bmcr & BMCR_ANENABLE) {
		ecmd->advertising |= ADVERTISED_Autoneg;
		ecmd->autoneg = AUTONEG_ENABLE;

		if (bmsr & BMSR_ANEGCOMPLETE) {
			ecmd->lp_advertising = mii_get_an(mii, MII_LPA);
			ecmd->lp_advertising |=
					mii_stat1000_to_ethtool_lpa_t(stat1000);
		} else {
			ecmd->lp_advertising = 0;
		}

		nego = ecmd->advertising & ecmd->lp_advertising;

		if (nego & (ADVERTISED_1000baseT_Full |
			    ADVERTISED_1000baseT_Half)) {
			ethtool_cmd_speed_set(ecmd, SPEED_1000);
			ecmd->duplex = !!(nego & ADVERTISED_1000baseT_Full);
		} else if (nego & (ADVERTISED_100baseT_Full |
				   ADVERTISED_100baseT_Half)) {
			ethtool_cmd_speed_set(ecmd, SPEED_100);
			ecmd->duplex = !!(nego & ADVERTISED_100baseT_Full);
		} else {
			ethtool_cmd_speed_set(ecmd, SPEED_10);
			ecmd->duplex = !!(nego & ADVERTISED_10baseT_Full);
		}
	} else {
		ecmd->autoneg = AUTONEG_DISABLE;

		ethtool_cmd_speed_set(ecmd,
				      ((bmcr & BMCR_SPEED1000 &&
					(bmcr & BMCR_SPEED100) == 0) ?
				       SPEED_1000 :
				       ((bmcr & BMCR_SPEED100) ?
					SPEED_100 : SPEED_10)));
		ecmd->duplex = (bmcr & BMCR_FULLDPLX) ? DUPLEX_FULL : DUPLEX_HALF;
	}

	mii->full_duplex = ecmd->duplex;

	/* ignore maxtxpkt, maxrxpkt for now */

	return 0;
}

/**
 * mii_ethtool_get_link_ksettings - get settings that are specified in @cmd
 * @mii: MII interface
 * @cmd: requested ethtool_link_ksettings
 *
 * The @cmd parameter is expected to have been cleared before calling
 * mii_ethtool_get_link_ksettings().
 */
void mii_ethtool_get_link_ksettings(struct mii_if_info *mii,
				    struct ethtool_link_ksettings *cmd)
{
	struct net_device *dev = mii->dev;
	u16 bmcr, bmsr, ctrl1000 = 0, stat1000 = 0;
	u32 nego, supported, advertising, lp_advertising;

	supported = (SUPPORTED_10baseT_Half | SUPPORTED_10baseT_Full |
		     SUPPORTED_100baseT_Half | SUPPORTED_100baseT_Full |
		     SUPPORTED_Autoneg | SUPPORTED_TP | SUPPORTED_MII);
	if (mii->supports_gmii)
		supported |= SUPPORTED_1000baseT_Half |
			SUPPORTED_1000baseT_Full;

	/* only supports twisted-pair */
	cmd->base.port = PORT_MII;

	/* this isn't fully supported at higher layers */
	cmd->base.phy_address = mii->phy_id;
	cmd->base.mdio_support = ETH_MDIO_SUPPORTS_C22;

	advertising = ADVERTISED_TP | ADVERTISED_MII;

	bmcr = mii->mdio_read(dev, mii->phy_id, MII_BMCR);
	bmsr = mii->mdio_read(dev, mii->phy_id, MII_BMSR);
	if (mii->supports_gmii) {
		ctrl1000 = mii->mdio_read(dev, mii->phy_id, MII_CTRL1000);
		stat1000 = mii->mdio_read(dev, mii->phy_id, MII_STAT1000);
	}

	advertising |= mii_get_an(mii, MII_ADVERTISE);
	if (mii->supports_gmii)
		advertising |= mii_ctrl1000_to_ethtool_adv_t(ctrl1000);

	if (bmcr & BMCR_ANENABLE) {
		advertising |= ADVERTISED_Autoneg;
		cmd->base.autoneg = AUTONEG_ENABLE;

		if (bmsr & BMSR_ANEGCOMPLETE) {
			lp_advertising = mii_get_an(mii, MII_LPA);
			lp_advertising |=
					mii_stat1000_to_ethtool_lpa_t(stat1000);
		} else {
			lp_advertising = 0;
		}

		nego = advertising & lp_advertising;

		if (nego & (ADVERTISED_1000baseT_Full |
			    ADVERTISED_1000baseT_Half)) {
			cmd->base.speed = SPEED_1000;
			cmd->base.duplex = !!(nego & ADVERTISED_1000baseT_Full);
		} else if (nego & (ADVERTISED_100baseT_Full |
				   ADVERTISED_100baseT_Half)) {
			cmd->base.speed = SPEED_100;
			cmd->base.duplex = !!(nego & ADVERTISED_100baseT_Full);
		} else {
			cmd->base.speed = SPEED_10;
			cmd->base.duplex = !!(nego & ADVERTISED_10baseT_Full);
		}
	} else {
		cmd->base.autoneg = AUTONEG_DISABLE;

		cmd->base.speed = ((bmcr & BMCR_SPEED1000 &&
				    (bmcr & BMCR_SPEED100) == 0) ?
				   SPEED_1000 :
				   ((bmcr & BMCR_SPEED100) ?
				    SPEED_100 : SPEED_10));
		cmd->base.duplex = (bmcr & BMCR_FULLDPLX) ?
			DUPLEX_FULL : DUPLEX_HALF;

		lp_advertising = 0;
	}

	mii->full_duplex = cmd->base.duplex;

	ethtool_convert_legacy_u32_to_link_mode(cmd->link_modes.supported,
						supported);
	ethtool_convert_legacy_u32_to_link_mode(cmd->link_modes.advertising,
						advertising);
	ethtool_convert_legacy_u32_to_link_mode(cmd->link_modes.lp_advertising,
						lp_advertising);

	/* ignore maxtxpkt, maxrxpkt for now */
}

/**
 * mii_ethtool_sset - set settings that are specified in @ecmd
 * @mii: MII interface
 * @ecmd: requested ethtool_cmd
 *
 * Returns 0 for success, negative on error.
 */
int mii_ethtool_sset(struct mii_if_info *mii, struct ethtool_cmd *ecmd)
{
	struct net_device *dev = mii->dev;
	u32 speed = ethtool_cmd_speed(ecmd);

	if (speed != SPEED_10 &&
	    speed != SPEED_100 &&
	    speed != SPEED_1000)
		return -EINVAL;
	if (ecmd->duplex != DUPLEX_HALF && ecmd->duplex != DUPLEX_FULL)
		return -EINVAL;
	if (ecmd->port != PORT_MII)
		return -EINVAL;
	if (ecmd->transceiver != XCVR_INTERNAL)
		return -EINVAL;
	if (ecmd->phy_address != mii->phy_id)
		return -EINVAL;
	if (ecmd->autoneg != AUTONEG_DISABLE && ecmd->autoneg != AUTONEG_ENABLE)
		return -EINVAL;
	if ((speed == SPEED_1000) && (!mii->supports_gmii))
		return -EINVAL;

	/* ignore supported, maxtxpkt, maxrxpkt */

	if (ecmd->autoneg == AUTONEG_ENABLE) {
		u32 bmcr, advert, tmp;
		u32 advert2 = 0, tmp2 = 0;

		if ((ecmd->advertising & (ADVERTISED_10baseT_Half |
					  ADVERTISED_10baseT_Full |
					  ADVERTISED_100baseT_Half |
					  ADVERTISED_100baseT_Full |
					  ADVERTISED_1000baseT_Half |
					  ADVERTISED_1000baseT_Full)) == 0)
			return -EINVAL;

		/* advertise only what has been requested */
		advert = mii->mdio_read(dev, mii->phy_id, MII_ADVERTISE);
		tmp = advert & ~(ADVERTISE_ALL | ADVERTISE_100BASE4);
		if (mii->supports_gmii) {
			advert2 = mii->mdio_read(dev, mii->phy_id, MII_CTRL1000);
			tmp2 = advert2 & ~(ADVERTISE_1000HALF | ADVERTISE_1000FULL);
		}
		tmp |= ethtool_adv_to_mii_adv_t(ecmd->advertising);

		if (mii->supports_gmii)
			tmp2 |=
			      ethtool_adv_to_mii_ctrl1000_t(ecmd->advertising);
		if (advert != tmp) {
			mii->mdio_write(dev, mii->phy_id, MII_ADVERTISE, tmp);
			mii->advertising = tmp;
		}
		if ((mii->supports_gmii) && (advert2 != tmp2))
			mii->mdio_write(dev, mii->phy_id, MII_CTRL1000, tmp2);

		/* turn on autonegotiation, and force a renegotiate */
		bmcr = mii->mdio_read(dev, mii->phy_id, MII_BMCR);
		bmcr |= (BMCR_ANENABLE | BMCR_ANRESTART);
		mii->mdio_write(dev, mii->phy_id, MII_BMCR, bmcr);

		mii->force_media = 0;
	} else {
		u32 bmcr, tmp;

		/* turn off auto negotiation, set speed and duplexity */
		bmcr = mii->mdio_read(dev, mii->phy_id, MII_BMCR);
		tmp = bmcr & ~(BMCR_ANENABLE | BMCR_SPEED100 |
			       BMCR_SPEED1000 | BMCR_FULLDPLX);
		if (speed == SPEED_1000)
			tmp |= BMCR_SPEED1000;
		else if (speed == SPEED_100)
			tmp |= BMCR_SPEED100;
		if (ecmd->duplex == DUPLEX_FULL) {
			tmp |= BMCR_FULLDPLX;
			mii->full_duplex = 1;
		} else
			mii->full_duplex = 0;
		if (bmcr != tmp)
			mii->mdio_write(dev, mii->phy_id, MII_BMCR, tmp);

		mii->force_media = 1;
	}
	return 0;
}

/**
 * mii_ethtool_set_link_ksettings - set settings that are specified in @cmd
 * @mii: MII interfaces
 * @cmd: requested ethtool_link_ksettings
 *
 * Returns 0 for success, negative on error.
 */
int mii_ethtool_set_link_ksettings(struct mii_if_info *mii,
				   const struct ethtool_link_ksettings *cmd)
{
	struct net_device *dev = mii->dev;
	u32 speed = cmd->base.speed;

	if (speed != SPEED_10 &&
	    speed != SPEED_100 &&
	    speed != SPEED_1000)
		return -EINVAL;
	if (cmd->base.duplex != DUPLEX_HALF && cmd->base.duplex != DUPLEX_FULL)
		return -EINVAL;
	if (cmd->base.port != PORT_MII)
		return -EINVAL;
	if (cmd->base.phy_address != mii->phy_id)
		return -EINVAL;
	if (cmd->base.autoneg != AUTONEG_DISABLE &&
	    cmd->base.autoneg != AUTONEG_ENABLE)
		return -EINVAL;
	if ((speed == SPEED_1000) && (!mii->supports_gmii))
		return -EINVAL;

	/* ignore supported, maxtxpkt, maxrxpkt */

	if (cmd->base.autoneg == AUTONEG_ENABLE) {
		u32 bmcr, advert, tmp;
		u32 advert2 = 0, tmp2 = 0;
		u32 advertising;

		ethtool_convert_link_mode_to_legacy_u32(
			&advertising, cmd->link_modes.advertising);

		if ((advertising & (ADVERTISED_10baseT_Half |
				    ADVERTISED_10baseT_Full |
				    ADVERTISED_100baseT_Half |
				    ADVERTISED_100baseT_Full |
				    ADVERTISED_1000baseT_Half |
				    ADVERTISED_1000baseT_Full)) == 0)
			return -EINVAL;

		/* advertise only what has been requested */
		advert = mii->mdio_read(dev, mii->phy_id, MII_ADVERTISE);
		tmp = advert & ~(ADVERTISE_ALL | ADVERTISE_100BASE4);
		if (mii->supports_gmii) {
			advert2 = mii->mdio_read(dev, mii->phy_id,
						 MII_CTRL1000);
			tmp2 = advert2 &
				~(ADVERTISE_1000HALF | ADVERTISE_1000FULL);
		}
		tmp |= ethtool_adv_to_mii_adv_t(advertising);

		if (mii->supports_gmii)
			tmp2 |= ethtool_adv_to_mii_ctrl1000_t(advertising);
		if (advert != tmp) {
			mii->mdio_write(dev, mii->phy_id, MII_ADVERTISE, tmp);
			mii->advertising = tmp;
		}
		if ((mii->supports_gmii) && (advert2 != tmp2))
			mii->mdio_write(dev, mii->phy_id, MII_CTRL1000, tmp2);

		/* turn on autonegotiation, and force a renegotiate */
		bmcr = mii->mdio_read(dev, mii->phy_id, MII_BMCR);
		bmcr |= (BMCR_ANENABLE | BMCR_ANRESTART);
		mii->mdio_write(dev, mii->phy_id, MII_BMCR, bmcr);

		mii->force_media = 0;
	} else {
		u32 bmcr, tmp;

		/* turn off auto negotiation, set speed and duplexity */
		bmcr = mii->mdio_read(dev, mii->phy_id, MII_BMCR);
		tmp = bmcr & ~(BMCR_ANENABLE | BMCR_SPEED100 |
			       BMCR_SPEED1000 | BMCR_FULLDPLX);
		if (speed == SPEED_1000)
			tmp |= BMCR_SPEED1000;
		else if (speed == SPEED_100)
			tmp |= BMCR_SPEED100;
		if (cmd->base.duplex == DUPLEX_FULL) {
			tmp |= BMCR_FULLDPLX;
			mii->full_duplex = 1;
		} else {
			mii->full_duplex = 0;
		}
		if (bmcr != tmp)
			mii->mdio_write(dev, mii->phy_id, MII_BMCR, tmp);

		mii->force_media = 1;
	}
	return 0;
}

/**
 * mii_check_gmii_support - check if the MII supports Gb interfaces
 * @mii: the MII interface
 */
int mii_check_gmii_support(struct mii_if_info *mii)
{
	int reg;

	reg = mii->mdio_read(mii->dev, mii->phy_id, MII_BMSR);
	if (reg & BMSR_ESTATEN) {
		reg = mii->mdio_read(mii->dev, mii->phy_id, MII_ESTATUS);
		if (reg & (ESTATUS_1000_TFULL | ESTATUS_1000_THALF))
			return 1;
	}

	return 0;
}

/**
 * mii_link_ok - is link status up/ok
 * @mii: the MII interface
 *
 * Returns 1 if the MII reports link status up/ok, 0 otherwise.
 */
int mii_link_ok (struct mii_if_info *mii)
{
	/* first, a dummy read, needed to latch some MII phys */
	mii->mdio_read(mii->dev, mii->phy_id, MII_BMSR);
	if (mii->mdio_read(mii->dev, mii->phy_id, MII_BMSR) & BMSR_LSTATUS)
		return 1;
	return 0;
}

/**
 * mii_nway_restart - restart NWay (autonegotiation) for this interface
 * @mii: the MII interface
 *
 * Returns 0 on success, negative on error.
 */
int mii_nway_restart (struct mii_if_info *mii)
{
	int bmcr;
	int r = -EINVAL;

	/* if autoneg is off, it's an error */
	bmcr = mii->mdio_read(mii->dev, mii->phy_id, MII_BMCR);

	if (bmcr & BMCR_ANENABLE) {
		bmcr |= BMCR_ANRESTART;
		mii->mdio_write(mii->dev, mii->phy_id, MII_BMCR, bmcr);
		r = 0;
	}

	return r;
}

/**
 * mii_check_link - check MII link status
 * @mii: MII interface
 *
 * If the link status changed (previous != current), call
 * netif_carrier_on() if current link status is Up or call
 * netif_carrier_off() if current link status is Down.
 */
void mii_check_link (struct mii_if_info *mii)
{
	int cur_link = mii_link_ok(mii);
	int prev_link = netif_carrier_ok(mii->dev);

	if (cur_link && !prev_link)
		netif_carrier_on(mii->dev);
	else if (prev_link && !cur_link)
		netif_carrier_off(mii->dev);
}

/**
 * mii_check_media - check the MII interface for a carrier/speed/duplex change
 * @mii: the MII interface
 * @ok_to_print: OK to print link up/down messages
 * @init_media: OK to save duplex mode in @mii
 *
 * Returns 1 if the duplex mode changed, 0 if not.
 * If the media type is forced, always returns 0.
 */
unsigned int mii_check_media (struct mii_if_info *mii,
			      unsigned int ok_to_print,
			      unsigned int init_media)
{
	unsigned int old_carrier, new_carrier;
	int advertise, lpa, media, duplex;
	int lpa2 = 0;

	/* check current and old link status */
	old_carrier = netif_carrier_ok(mii->dev) ? 1 : 0;
	new_carrier = (unsigned int) mii_link_ok(mii);

	/* if carrier state did not change, this is a "bounce",
	 * just exit as everything is already set correctly
	 */
	if ((!init_media) && (old_carrier == new_carrier))
		return 0; /* duplex did not change */

	/* no carrier, nothing much to do */
	if (!new_carrier) {
		netif_carrier_off(mii->dev);
		if (ok_to_print)
			netdev_info(mii->dev, "link down\n");
		return 0; /* duplex did not change */
	}

	/*
	 * we have carrier, see who's on the other end
	 */
	netif_carrier_on(mii->dev);

	if (mii->force_media) {
		if (ok_to_print)
			netdev_info(mii->dev, "link up\n");
		return 0; /* duplex did not change */
	}

	/* get MII advertise and LPA values */
	if ((!init_media) && (mii->advertising))
		advertise = mii->advertising;
	else {
		advertise = mii->mdio_read(mii->dev, mii->phy_id, MII_ADVERTISE);
		mii->advertising = advertise;
	}
	lpa = mii->mdio_read(mii->dev, mii->phy_id, MII_LPA);
	if (mii->supports_gmii)
		lpa2 = mii->mdio_read(mii->dev, mii->phy_id, MII_STAT1000);

	/* figure out media and duplex from advertise and LPA values */
	media = mii_nway_result(lpa & advertise);
	duplex = (media & ADVERTISE_FULL) ? 1 : 0;
	if (lpa2 & LPA_1000FULL)
		duplex = 1;

	if (ok_to_print)
		netdev_info(mii->dev, "link up, %uMbps, %s-duplex, lpa 0x%04X\n",
			    lpa2 & (LPA_1000FULL | LPA_1000HALF) ? 1000 :
			    media & (ADVERTISE_100FULL | ADVERTISE_100HALF) ?
			    100 : 10,
			    duplex ? "full" : "half",
			    lpa);

	if ((init_media) || (mii->full_duplex != duplex)) {
		mii->full_duplex = duplex;
		return 1; /* duplex changed */
	}

	return 0; /* duplex did not change */
}

/**
 * generic_mii_ioctl - main MII ioctl interface
 * @mii_if: the MII interface
 * @mii_data: MII ioctl data structure
 * @cmd: MII ioctl command
 * @duplex_chg_out: pointer to @duplex_changed status if there was no
 *	ioctl error
 *
 * Returns 0 on success, negative on error.
 */
int generic_mii_ioctl(struct mii_if_info *mii_if,
		      struct mii_ioctl_data *mii_data, int cmd,
		      unsigned int *duplex_chg_out)
{
	int rc = 0;
	unsigned int duplex_changed = 0;

	if (duplex_chg_out)
		*duplex_chg_out = 0;

	mii_data->phy_id &= mii_if->phy_id_mask;
	mii_data->reg_num &= mii_if->reg_num_mask;

	switch(cmd) {
	case SIOCGMIIPHY:
		mii_data->phy_id = mii_if->phy_id;
		fallthrough;

	case SIOCGMIIREG:
		mii_data->val_out =
			mii_if->mdio_read(mii_if->dev, mii_data->phy_id,
					  mii_data->reg_num);
		break;

	case SIOCSMIIREG: {
		u16 val = mii_data->val_in;

		if (mii_data->phy_id == mii_if->phy_id) {
			switch(mii_data->reg_num) {
			case MII_BMCR: {
				unsigned int new_duplex = 0;
				if (val & (BMCR_RESET|BMCR_ANENABLE))
					mii_if->force_media = 0;
				else
					mii_if->force_media = 1;
				if (mii_if->force_media &&
				    (val & BMCR_FULLDPLX))
					new_duplex = 1;
				if (mii_if->full_duplex != new_duplex) {
					duplex_changed = 1;
					mii_if->full_duplex = new_duplex;
				}
				break;
			}
			case MII_ADVERTISE:
				mii_if->advertising = val;
				break;
			default:
				/* do nothing */
				break;
			}
		}

		mii_if->mdio_write(mii_if->dev, mii_data->phy_id,
				   mii_data->reg_num, val);
		break;
	}

	default:
		rc = -EOPNOTSUPP;
		break;
	}

	if ((rc == 0) && (duplex_chg_out) && (duplex_changed))
		*duplex_chg_out = 1;

	return rc;
}

MODULE_AUTHOR ("Jeff Garzik <jgarzik@pobox.com>");
MODULE_DESCRIPTION ("MII hardware support library");
MODULE_LICENSE("GPL");

EXPORT_SYMBOL(mii_link_ok);
EXPORT_SYMBOL(mii_nway_restart);
EXPORT_SYMBOL(mii_ethtool_gset);
EXPORT_SYMBOL(mii_ethtool_get_link_ksettings);
EXPORT_SYMBOL(mii_ethtool_sset);
EXPORT_SYMBOL(mii_ethtool_set_link_ksettings);
EXPORT_SYMBOL(mii_check_link);
EXPORT_SYMBOL(mii_check_media);
EXPORT_SYMBOL(mii_check_gmii_support);
EXPORT_SYMBOL(generic_mii_ioctl);

