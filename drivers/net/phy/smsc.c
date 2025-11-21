// SPDX-License-Identifier: GPL-2.0+
/*
 * drivers/net/phy/smsc.c
 *
 * Driver for SMSC PHYs
 *
 * Author: Herbert Valerio Riedel
 *
 * Copyright (c) 2006 Herbert Valerio Riedel <hvr@gnu.org>
 *
 * Support added for SMSC LAN8187 and LAN8700 by steve.glendinning@shawell.net
 *
 */

#include <linux/clk.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/mii.h>
#include <linux/ethtool.h>
#include <linux/of.h>
#include <linux/phy.h>
#include <linux/netdevice.h>
#include <linux/crc16.h>
#include <linux/etherdevice.h>
#include <linux/smscphy.h>

/* Vendor-specific PHY Definitions */
/* EDPD NLP / crossover time configuration */
#define PHY_EDPD_CONFIG			16
#define PHY_EDPD_CONFIG_EXT_CROSSOVER_	0x0001

/* Control/Status Indication Register */
#define SPECIAL_CTRL_STS		27
#define SPECIAL_CTRL_STS_OVRRD_AMDIX_	0x8000
#define SPECIAL_CTRL_STS_AMDIX_ENABLE_	0x4000
#define SPECIAL_CTRL_STS_AMDIX_STATE_	0x2000

#define EDPD_MAX_WAIT_DFLT_MS		640
/* interval between phylib state machine runs in ms */
#define PHY_STATE_MACH_MS		1000

struct smsc_hw_stat {
	const char *string;
	u8 reg;
	u8 bits;
};

static struct smsc_hw_stat smsc_hw_stats[] = {
	{ "phy_symbol_errors", 26, 16},
};

struct smsc_phy_priv {
	unsigned int edpd_enable:1;
	unsigned int edpd_mode_set_by_user:1;
	unsigned int edpd_max_wait_ms;
	bool wol_arp;
};

static int smsc_phy_ack_interrupt(struct phy_device *phydev)
{
	int rc = phy_read(phydev, MII_LAN83C185_ISF);

	return rc < 0 ? rc : 0;
}

int smsc_phy_config_intr(struct phy_device *phydev)
{
	int rc;

	if (phydev->interrupts == PHY_INTERRUPT_ENABLED) {
		rc = smsc_phy_ack_interrupt(phydev);
		if (rc)
			return rc;

		rc = phy_write(phydev, MII_LAN83C185_IM,
			       MII_LAN83C185_ISF_INT_PHYLIB_EVENTS);
	} else {
		rc = phy_write(phydev, MII_LAN83C185_IM, 0);
		if (rc)
			return rc;

		rc = smsc_phy_ack_interrupt(phydev);
	}

	return rc < 0 ? rc : 0;
}
EXPORT_SYMBOL_GPL(smsc_phy_config_intr);

static int smsc_phy_config_edpd(struct phy_device *phydev)
{
	struct smsc_phy_priv *priv = phydev->priv;

	if (priv->edpd_enable)
		return phy_set_bits(phydev, MII_LAN83C185_CTRL_STATUS,
				    MII_LAN83C185_EDPWRDOWN);
	else
		return phy_clear_bits(phydev, MII_LAN83C185_CTRL_STATUS,
				      MII_LAN83C185_EDPWRDOWN);
}

irqreturn_t smsc_phy_handle_interrupt(struct phy_device *phydev)
{
	int irq_status;

	irq_status = phy_read(phydev, MII_LAN83C185_ISF);
	if (irq_status < 0) {
		if (irq_status != -ENODEV)
			phy_error(phydev);

		return IRQ_NONE;
	}

	if (!(irq_status & MII_LAN83C185_ISF_INT_PHYLIB_EVENTS))
		return IRQ_NONE;

	phy_trigger_machine(phydev);

	return IRQ_HANDLED;
}
EXPORT_SYMBOL_GPL(smsc_phy_handle_interrupt);

int smsc_phy_config_init(struct phy_device *phydev)
{
	struct smsc_phy_priv *priv = phydev->priv;

	if (!priv)
		return 0;

	/* don't use EDPD in irq mode except overridden by user */
	if (!priv->edpd_mode_set_by_user && phydev->irq != PHY_POLL)
		priv->edpd_enable = false;

	return smsc_phy_config_edpd(phydev);
}
EXPORT_SYMBOL_GPL(smsc_phy_config_init);

static int smsc_phy_reset(struct phy_device *phydev)
{
	int rc = phy_read(phydev, MII_LAN83C185_SPECIAL_MODES);
	if (rc < 0)
		return rc;

	/* If the SMSC PHY is in power down mode, then set it
	 * in all capable mode before using it.
	 */
	if ((rc & MII_LAN83C185_MODE_MASK) == MII_LAN83C185_MODE_POWERDOWN) {
		/* set "all capable" mode */
		rc |= MII_LAN83C185_MODE_ALL;
		phy_write(phydev, MII_LAN83C185_SPECIAL_MODES, rc);
	}

	/* reset the phy */
	return genphy_soft_reset(phydev);
}

static int lan87xx_config_aneg(struct phy_device *phydev)
{
	u8 mdix_ctrl;
	int val;
	int rc;

	/* When auto-negotiation is disabled (forced mode), the PHY's
	 * Auto-MDIX will continue toggling the TX/RX pairs.
	 *
	 * To establish a stable link, we must select a fixed MDI mode.
	 * If the user has not specified a fixed MDI mode (i.e., mdix_ctrl is
	 * 'auto'), we default to ETH_TP_MDI. This choice of a ETH_TP_MDI mode
	 * mirrors the behavior the hardware would exhibit if the AUTOMDIX_EN
	 * strap were configured for a fixed MDI connection.
	 */
	if (phydev->autoneg == AUTONEG_DISABLE) {
		if (phydev->mdix_ctrl == ETH_TP_MDI_AUTO)
			mdix_ctrl = ETH_TP_MDI;
		else
			mdix_ctrl = phydev->mdix_ctrl;
	} else {
		mdix_ctrl = phydev->mdix_ctrl;
	}

	switch (mdix_ctrl) {
	case ETH_TP_MDI:
		val = SPECIAL_CTRL_STS_OVRRD_AMDIX_;
		break;
	case ETH_TP_MDI_X:
		val = SPECIAL_CTRL_STS_OVRRD_AMDIX_ |
			SPECIAL_CTRL_STS_AMDIX_STATE_;
		break;
	case ETH_TP_MDI_AUTO:
		val = SPECIAL_CTRL_STS_OVRRD_AMDIX_ |
			SPECIAL_CTRL_STS_AMDIX_ENABLE_;
		break;
	default:
		return genphy_config_aneg(phydev);
	}

	rc = phy_read(phydev, SPECIAL_CTRL_STS);
	if (rc < 0)
		return rc;

	rc &= ~(SPECIAL_CTRL_STS_OVRRD_AMDIX_ |
		SPECIAL_CTRL_STS_AMDIX_ENABLE_ |
		SPECIAL_CTRL_STS_AMDIX_STATE_);
	rc |= val;
	phy_write(phydev, SPECIAL_CTRL_STS, rc);

	phydev->mdix = mdix_ctrl;
	return genphy_config_aneg(phydev);
}

static int lan95xx_config_aneg_ext(struct phy_device *phydev)
{
	if (phydev->phy_id == 0x0007c0f0) { /* LAN9500A or LAN9505A */
		/* Extend Manual AutoMDIX timer */
		int rc = phy_set_bits(phydev, PHY_EDPD_CONFIG,
				      PHY_EDPD_CONFIG_EXT_CROSSOVER_);

		if (rc < 0)
			return rc;
	}

	return lan87xx_config_aneg(phydev);
}

/*
 * The LAN87xx suffers from rare absence of the ENERGYON-bit when Ethernet cable
 * plugs in while LAN87xx is in Energy Detect Power-Down mode. This leads to
 * unstable detection of plugging in Ethernet cable.
 * This workaround disables Energy Detect Power-Down mode and waiting for
 * response on link pulses to detect presence of plugged Ethernet cable.
 * The Energy Detect Power-Down mode is enabled again in the end of procedure to
 * save approximately 220 mW of power if cable is unplugged.
 * The workaround is only applicable to poll mode. Energy Detect Power-Down may
 * not be used in interrupt mode lest link change detection becomes unreliable.
 */
int lan87xx_read_status(struct phy_device *phydev)
{
	struct smsc_phy_priv *priv = phydev->priv;
	int err;

	err = genphy_read_status(phydev);
	if (err)
		return err;

	if (!phydev->link && priv && priv->edpd_enable &&
	    priv->edpd_max_wait_ms) {
		unsigned int max_wait = priv->edpd_max_wait_ms * 1000;
		int rc;

		/* Disable EDPD to wake up PHY */
		rc = phy_read(phydev, MII_LAN83C185_CTRL_STATUS);
		if (rc < 0)
			return rc;

		rc = phy_write(phydev, MII_LAN83C185_CTRL_STATUS,
			       rc & ~MII_LAN83C185_EDPWRDOWN);
		if (rc < 0)
			return rc;

		/* Wait max 640 ms to detect energy and the timeout is not
		 * an actual error.
		 */
		read_poll_timeout(phy_read, rc,
				  rc & MII_LAN83C185_ENERGYON || rc < 0,
				  10000, max_wait, true, phydev,
				  MII_LAN83C185_CTRL_STATUS);
		if (rc < 0)
			return rc;

		/* Re-enable EDPD */
		rc = phy_read(phydev, MII_LAN83C185_CTRL_STATUS);
		if (rc < 0)
			return rc;

		rc = phy_write(phydev, MII_LAN83C185_CTRL_STATUS,
			       rc | MII_LAN83C185_EDPWRDOWN);
		if (rc < 0)
			return rc;
	}

	return err;
}
EXPORT_SYMBOL_GPL(lan87xx_read_status);

static int lan87xx_phy_config_init(struct phy_device *phydev)
{
	int rc;

	/* The LAN87xx PHY's initial MDI-X mode is determined by the AUTOMDIX_EN
	 * hardware strap, but the driver cannot read the strap's status. This
	 * creates an unpredictable initial state.
	 *
	 * To ensure consistent and reliable behavior across all boards,
	 * override the strap configuration on initialization and force the PHY
	 * into a known state with Auto-MDIX enabled, which is the expected
	 * default for modern hardware.
	 */
	rc = phy_modify(phydev, SPECIAL_CTRL_STS,
			SPECIAL_CTRL_STS_OVRRD_AMDIX_ |
			SPECIAL_CTRL_STS_AMDIX_ENABLE_ |
			SPECIAL_CTRL_STS_AMDIX_STATE_,
			SPECIAL_CTRL_STS_OVRRD_AMDIX_ |
			SPECIAL_CTRL_STS_AMDIX_ENABLE_);
	if (rc < 0)
		return rc;

	phydev->mdix_ctrl = ETH_TP_MDI_AUTO;

	return smsc_phy_config_init(phydev);
}

static int lan874x_phy_config_init(struct phy_device *phydev)
{
	u16 val;
	int rc;

	/* Setup LED2/nINT/nPME pin to function as nPME.  May need user option
	 * to use LED1/nINT/nPME.
	 */
	val = MII_LAN874X_PHY_PME2_SET;

	/* The bits MII_LAN874X_PHY_WOL_PFDA_FR, MII_LAN874X_PHY_WOL_WUFR,
	 * MII_LAN874X_PHY_WOL_MPR, and MII_LAN874X_PHY_WOL_BCAST_FR need to
	 * be cleared to de-assert PME signal after a WoL event happens, but
	 * using PME auto clear gets around that.
	 */
	val |= MII_LAN874X_PHY_PME_SELF_CLEAR;
	rc = phy_write_mmd(phydev, MDIO_MMD_PCS, MII_LAN874X_PHY_MMD_WOL_WUCSR,
			   val);
	if (rc < 0)
		return rc;

	/* set nPME self clear delay time */
	rc = phy_write_mmd(phydev, MDIO_MMD_PCS, MII_LAN874X_PHY_MMD_MCFGR,
			   MII_LAN874X_PHY_PME_SELF_CLEAR_DELAY);
	if (rc < 0)
		return rc;

	return smsc_phy_config_init(phydev);
}

static void lan874x_get_wol(struct phy_device *phydev,
			    struct ethtool_wolinfo *wol)
{
	struct smsc_phy_priv *priv = phydev->priv;
	int rc;

	wol->supported = (WAKE_UCAST | WAKE_BCAST | WAKE_MAGIC |
			  WAKE_ARP | WAKE_MCAST);
	wol->wolopts = 0;

	rc = phy_read_mmd(phydev, MDIO_MMD_PCS, MII_LAN874X_PHY_MMD_WOL_WUCSR);
	if (rc < 0)
		return;

	if (rc & MII_LAN874X_PHY_WOL_PFDAEN)
		wol->wolopts |= WAKE_UCAST;

	if (rc & MII_LAN874X_PHY_WOL_BCSTEN)
		wol->wolopts |= WAKE_BCAST;

	if (rc & MII_LAN874X_PHY_WOL_MPEN)
		wol->wolopts |= WAKE_MAGIC;

	if (rc & MII_LAN874X_PHY_WOL_WUEN) {
		if (priv->wol_arp)
			wol->wolopts |= WAKE_ARP;
		else
			wol->wolopts |= WAKE_MCAST;
	}
}

static u16 smsc_crc16(const u8 *buffer, size_t len)
{
	return bitrev16(crc16(0xFFFF, buffer, len));
}

static int lan874x_chk_wol_pattern(const u8 pattern[], const u16 *mask,
				   u8 len, u8 *data, u8 *datalen)
{
	size_t i, j, k;
	int ret = 0;
	u16 bits;

	/* Pattern filtering can match up to 128 bytes of frame data.  There
	 * are 8 registers to program the 16-bit masks, where each bit means
	 * the byte will be compared.  The frame data will then go through a
	 * CRC16 calculation for hardware comparison.  This helper function
	 * makes sure only relevant frame data are included in this
	 * calculation.  It provides a warning when the masks and expected
	 * data size do not match.
	 */
	i = 0;
	k = 0;
	while (len > 0) {
		bits = *mask;
		for (j = 0; j < 16; j++, i++, len--) {
			/* No more pattern. */
			if (!len) {
				/* The rest of bitmap is not empty. */
				if (bits)
					ret = i + 1;
				break;
			}
			if (bits & 1)
				data[k++] = pattern[i];
			bits >>= 1;
		}
		mask++;
	}
	*datalen = k;
	return ret;
}

static int lan874x_set_wol_pattern(struct phy_device *phydev, u16 val,
				   const u8 data[], u8 datalen,
				   const u16 *mask, u8 masklen)
{
	u16 crc, reg;
	int rc;

	/* Starting pattern offset is set before calling this function. */
	val |= MII_LAN874X_PHY_WOL_FILTER_EN;
	rc = phy_write_mmd(phydev, MDIO_MMD_PCS,
			   MII_LAN874X_PHY_MMD_WOL_WUF_CFGA, val);
	if (rc < 0)
		return rc;

	crc = smsc_crc16(data, datalen);
	rc = phy_write_mmd(phydev, MDIO_MMD_PCS,
			   MII_LAN874X_PHY_MMD_WOL_WUF_CFGB, crc);
	if (rc < 0)
		return rc;

	masklen = (masklen + 15) & ~0xf;
	reg = MII_LAN874X_PHY_MMD_WOL_WUF_MASK7;
	while (masklen >= 16) {
		rc = phy_write_mmd(phydev, MDIO_MMD_PCS, reg, *mask);
		if (rc < 0)
			return rc;
		reg--;
		mask++;
		masklen -= 16;
	}

	/* Clear out the rest of mask registers. */
	while (reg != MII_LAN874X_PHY_MMD_WOL_WUF_MASK0) {
		phy_write_mmd(phydev, MDIO_MMD_PCS, reg, 0);
		reg--;
	}
	return rc;
}

static int lan874x_set_wol(struct phy_device *phydev,
			   struct ethtool_wolinfo *wol)
{
	struct net_device *ndev = phydev->attached_dev;
	struct smsc_phy_priv *priv = phydev->priv;
	u16 val, val_wucsr;
	u8 data[128];
	u8 datalen;
	int rc;

	/* lan874x has only one WoL filter pattern */
	if ((wol->wolopts & (WAKE_ARP | WAKE_MCAST)) ==
	    (WAKE_ARP | WAKE_MCAST)) {
		phydev_info(phydev,
			    "lan874x WoL supports one of ARP|MCAST at a time\n");
		return -EOPNOTSUPP;
	}

	rc = phy_read_mmd(phydev, MDIO_MMD_PCS, MII_LAN874X_PHY_MMD_WOL_WUCSR);
	if (rc < 0)
		return rc;

	val_wucsr = rc;

	if (wol->wolopts & WAKE_UCAST)
		val_wucsr |= MII_LAN874X_PHY_WOL_PFDAEN;
	else
		val_wucsr &= ~MII_LAN874X_PHY_WOL_PFDAEN;

	if (wol->wolopts & WAKE_BCAST)
		val_wucsr |= MII_LAN874X_PHY_WOL_BCSTEN;
	else
		val_wucsr &= ~MII_LAN874X_PHY_WOL_BCSTEN;

	if (wol->wolopts & WAKE_MAGIC)
		val_wucsr |= MII_LAN874X_PHY_WOL_MPEN;
	else
		val_wucsr &= ~MII_LAN874X_PHY_WOL_MPEN;

	/* Need to use pattern matching */
	if (wol->wolopts & (WAKE_ARP | WAKE_MCAST))
		val_wucsr |= MII_LAN874X_PHY_WOL_WUEN;
	else
		val_wucsr &= ~MII_LAN874X_PHY_WOL_WUEN;

	if (wol->wolopts & WAKE_ARP) {
		const u8 pattern[2] = { 0x08, 0x06 };
		const u16 mask[1] = { 0x0003 };

		rc = lan874x_chk_wol_pattern(pattern, mask, 2, data,
					     &datalen);
		if (rc)
			phydev_dbg(phydev, "pattern not valid at %d\n", rc);

		/* Need to match broadcast destination address and provided
		 * data pattern at offset 12.
		 */
		val = 12 | MII_LAN874X_PHY_WOL_FILTER_BCSTEN;
		rc = lan874x_set_wol_pattern(phydev, val, data, datalen, mask,
					     2);
		if (rc < 0)
			return rc;
		priv->wol_arp = true;
	}

	if (wol->wolopts & WAKE_MCAST) {
		/* Need to match multicast destination address. */
		val = MII_LAN874X_PHY_WOL_FILTER_MCASTTEN;
		rc = lan874x_set_wol_pattern(phydev, val, data, 0, NULL, 0);
		if (rc < 0)
			return rc;
		priv->wol_arp = false;
	}

	if (wol->wolopts & (WAKE_MAGIC | WAKE_UCAST)) {
		const u8 *mac = (const u8 *)ndev->dev_addr;
		int i, reg;

		reg = MII_LAN874X_PHY_MMD_WOL_RX_ADDRC;
		for (i = 0; i < 6; i += 2, reg--) {
			rc = phy_write_mmd(phydev, MDIO_MMD_PCS, reg,
					   ((mac[i + 1] << 8) | mac[i]));
			if (rc < 0)
				return rc;
		}
	}

	rc = phy_write_mmd(phydev, MDIO_MMD_PCS, MII_LAN874X_PHY_MMD_WOL_WUCSR,
			   val_wucsr);
	if (rc < 0)
		return rc;

	return 0;
}

static int smsc_get_sset_count(struct phy_device *phydev)
{
	return ARRAY_SIZE(smsc_hw_stats);
}

static void smsc_get_strings(struct phy_device *phydev, u8 *data)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(smsc_hw_stats); i++)
		ethtool_puts(&data, smsc_hw_stats[i].string);
}

static u64 smsc_get_stat(struct phy_device *phydev, int i)
{
	struct smsc_hw_stat stat = smsc_hw_stats[i];
	int val;
	u64 ret;

	val = phy_read(phydev, stat.reg);
	if (val < 0)
		ret = U64_MAX;
	else
		ret = val;

	return ret;
}

static void smsc_get_stats(struct phy_device *phydev,
			   struct ethtool_stats *stats, u64 *data)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(smsc_hw_stats); i++)
		data[i] = smsc_get_stat(phydev, i);
}

static int smsc_phy_get_edpd(struct phy_device *phydev, u16 *edpd)
{
	struct smsc_phy_priv *priv = phydev->priv;

	if (!priv)
		return -EOPNOTSUPP;

	if (!priv->edpd_enable)
		*edpd = ETHTOOL_PHY_EDPD_DISABLE;
	else if (!priv->edpd_max_wait_ms)
		*edpd = ETHTOOL_PHY_EDPD_NO_TX;
	else
		*edpd = PHY_STATE_MACH_MS + priv->edpd_max_wait_ms;

	return 0;
}

static int smsc_phy_set_edpd(struct phy_device *phydev, u16 edpd)
{
	struct smsc_phy_priv *priv = phydev->priv;

	if (!priv)
		return -EOPNOTSUPP;

	switch (edpd) {
	case ETHTOOL_PHY_EDPD_DISABLE:
		priv->edpd_enable = false;
		break;
	case ETHTOOL_PHY_EDPD_NO_TX:
		priv->edpd_enable = true;
		priv->edpd_max_wait_ms = 0;
		break;
	case ETHTOOL_PHY_EDPD_DFLT_TX_MSECS:
		edpd = PHY_STATE_MACH_MS + EDPD_MAX_WAIT_DFLT_MS;
		fallthrough;
	default:
		if (phydev->irq != PHY_POLL)
			return -EOPNOTSUPP;
		if (edpd < PHY_STATE_MACH_MS || edpd > PHY_STATE_MACH_MS + 1000)
			return -EINVAL;
		priv->edpd_enable = true;
		priv->edpd_max_wait_ms = edpd - PHY_STATE_MACH_MS;
	}

	priv->edpd_mode_set_by_user = true;

	return smsc_phy_config_edpd(phydev);
}

int smsc_phy_get_tunable(struct phy_device *phydev,
			 struct ethtool_tunable *tuna, void *data)
{
	switch (tuna->id) {
	case ETHTOOL_PHY_EDPD:
		return smsc_phy_get_edpd(phydev, data);
	default:
		return -EOPNOTSUPP;
	}
}
EXPORT_SYMBOL_GPL(smsc_phy_get_tunable);

int smsc_phy_set_tunable(struct phy_device *phydev,
			 struct ethtool_tunable *tuna, const void *data)
{
	switch (tuna->id) {
	case ETHTOOL_PHY_EDPD:
		return smsc_phy_set_edpd(phydev, *(u16 *)data);
	default:
		return -EOPNOTSUPP;
	}
}
EXPORT_SYMBOL_GPL(smsc_phy_set_tunable);

int smsc_phy_probe(struct phy_device *phydev)
{
	struct device *dev = &phydev->mdio.dev;
	struct smsc_phy_priv *priv;
	struct clk *refclk;

	priv = devm_kzalloc(dev, sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	priv->edpd_enable = true;
	priv->edpd_max_wait_ms = EDPD_MAX_WAIT_DFLT_MS;

	if (device_property_present(dev, "smsc,disable-energy-detect"))
		priv->edpd_enable = false;

	phydev->priv = priv;

	/* Make clk optional to keep DTB backward compatibility. */
	refclk = devm_clk_get_optional_enabled_with_rate(dev, NULL,
							 50 * 1000 * 1000);
	if (IS_ERR(refclk))
		return dev_err_probe(dev, PTR_ERR(refclk),
				     "Failed to request clock\n");

	return 0;
}
EXPORT_SYMBOL_GPL(smsc_phy_probe);

static struct phy_driver smsc_phy_driver[] = {
{
	.phy_id		= 0x0007c0a0, /* OUI=0x00800f, Model#=0x0a */
	.phy_id_mask	= 0xfffffff0,
	.name		= "SMSC LAN83C185",

	/* PHY_BASIC_FEATURES */

	.probe		= smsc_phy_probe,

	/* basic functions */
	.config_init	= smsc_phy_config_init,
	.soft_reset	= smsc_phy_reset,

	/* IRQ related */
	.config_intr	= smsc_phy_config_intr,
	.handle_interrupt = smsc_phy_handle_interrupt,

	.suspend	= genphy_suspend,
	.resume		= genphy_resume,
}, {
	.phy_id		= 0x0007c0b0, /* OUI=0x00800f, Model#=0x0b */
	.phy_id_mask	= 0xfffffff0,
	.name		= "SMSC LAN8187",

	/* PHY_BASIC_FEATURES */

	.probe		= smsc_phy_probe,

	/* basic functions */
	.config_init	= smsc_phy_config_init,
	.soft_reset	= smsc_phy_reset,

	/* IRQ related */
	.config_intr	= smsc_phy_config_intr,
	.handle_interrupt = smsc_phy_handle_interrupt,

	/* Statistics */
	.get_sset_count = smsc_get_sset_count,
	.get_strings	= smsc_get_strings,
	.get_stats	= smsc_get_stats,

	.suspend	= genphy_suspend,
	.resume		= genphy_resume,
}, {
	/* This covers internal PHY (phy_id: 0x0007C0C3) for
	 * LAN9500 (PID: 0x9500), LAN9514 (PID: 0xec00), LAN9505 (PID: 0x9505)
	 */
	.phy_id		= 0x0007c0c0, /* OUI=0x00800f, Model#=0x0c */
	.phy_id_mask	= 0xfffffff0,
	.name		= "SMSC LAN8700",

	/* PHY_BASIC_FEATURES */

	.probe		= smsc_phy_probe,

	/* basic functions */
	.read_status	= lan87xx_read_status,
	.config_init	= lan87xx_phy_config_init,
	.soft_reset	= smsc_phy_reset,
	.config_aneg	= lan87xx_config_aneg,

	/* IRQ related */
	.config_intr	= smsc_phy_config_intr,
	.handle_interrupt = smsc_phy_handle_interrupt,

	/* Statistics */
	.get_sset_count = smsc_get_sset_count,
	.get_strings	= smsc_get_strings,
	.get_stats	= smsc_get_stats,

	.get_tunable	= smsc_phy_get_tunable,
	.set_tunable	= smsc_phy_set_tunable,

	.suspend	= genphy_suspend,
	.resume		= genphy_resume,
}, {
	.phy_id		= 0x0007c0d0, /* OUI=0x00800f, Model#=0x0d */
	.phy_id_mask	= 0xfffffff0,
	.name		= "SMSC LAN911x Internal PHY",

	/* PHY_BASIC_FEATURES */

	.probe		= smsc_phy_probe,

	/* IRQ related */
	.config_intr	= smsc_phy_config_intr,
	.handle_interrupt = smsc_phy_handle_interrupt,

	.suspend	= genphy_suspend,
	.resume		= genphy_resume,
}, {
	/* This covers internal PHY (phy_id: 0x0007C0F0) for
	 * LAN9500A (PID: 0x9E00), LAN9505A (PID: 0x9E01)
	 */
	.phy_id		= 0x0007c0f0, /* OUI=0x00800f, Model#=0x0f */
	.phy_id_mask	= 0xfffffff0,
	.name		= "SMSC LAN8710/LAN8720",

	/* PHY_BASIC_FEATURES */

	.flags		= PHY_RST_AFTER_CLK_EN,
	.probe		= smsc_phy_probe,

	/* basic functions */
	.read_status	= lan87xx_read_status,
	.config_init	= smsc_phy_config_init,
	.soft_reset	= smsc_phy_reset,
	.config_aneg	= lan95xx_config_aneg_ext,

	/* IRQ related */
	.config_intr	= smsc_phy_config_intr,
	.handle_interrupt = smsc_phy_handle_interrupt,

	/* Statistics */
	.get_sset_count = smsc_get_sset_count,
	.get_strings	= smsc_get_strings,
	.get_stats	= smsc_get_stats,

	.get_tunable	= smsc_phy_get_tunable,
	.set_tunable	= smsc_phy_set_tunable,

	.suspend	= genphy_suspend,
	.resume		= genphy_resume,
}, {
	.phy_id		= 0x0007c110,
	.phy_id_mask	= 0xfffffff0,
	.name		= "SMSC LAN8740",

	/* PHY_BASIC_FEATURES */
	.flags		= PHY_RST_AFTER_CLK_EN,

	.probe		= smsc_phy_probe,

	/* basic functions */
	.read_status	= lan87xx_read_status,
	.config_init	= lan874x_phy_config_init,
	.soft_reset	= smsc_phy_reset,

	/* IRQ related */
	.config_intr	= smsc_phy_config_intr,
	.handle_interrupt = smsc_phy_handle_interrupt,

	/* Statistics */
	.get_sset_count = smsc_get_sset_count,
	.get_strings	= smsc_get_strings,
	.get_stats	= smsc_get_stats,

	.get_tunable	= smsc_phy_get_tunable,
	.set_tunable	= smsc_phy_set_tunable,

	/* WoL */
	.set_wol	= lan874x_set_wol,
	.get_wol	= lan874x_get_wol,

	.suspend	= genphy_suspend,
	.resume		= genphy_resume,
}, {
	.phy_id		= 0x0007c130,	/* 0x0007c130 and 0x0007c131 */
	/* This mask (0xfffffff2) is to differentiate from
	 * LAN88xx (phy_id 0x0007c132)
	 * and allows future phy_id revisions.
	 */
	.phy_id_mask	= 0xfffffff2,
	.name		= "Microchip LAN8742",

	/* PHY_BASIC_FEATURES */
	.flags		= PHY_RST_AFTER_CLK_EN,

	.probe		= smsc_phy_probe,

	/* basic functions */
	.read_status	= lan87xx_read_status,
	.config_init	= lan874x_phy_config_init,
	.soft_reset	= smsc_phy_reset,

	/* IRQ related */
	.config_intr	= smsc_phy_config_intr,
	.handle_interrupt = smsc_phy_handle_interrupt,

	/* Statistics */
	.get_sset_count = smsc_get_sset_count,
	.get_strings	= smsc_get_strings,
	.get_stats	= smsc_get_stats,

	.get_tunable	= smsc_phy_get_tunable,
	.set_tunable	= smsc_phy_set_tunable,

	/* WoL */
	.set_wol	= lan874x_set_wol,
	.get_wol	= lan874x_get_wol,

	.suspend	= genphy_suspend,
	.resume		= genphy_resume,
} };

module_phy_driver(smsc_phy_driver);

MODULE_DESCRIPTION("SMSC PHY driver");
MODULE_AUTHOR("Herbert Valerio Riedel");
MODULE_LICENSE("GPL");

static const struct mdio_device_id __maybe_unused smsc_tbl[] = {
	{ 0x0007c0a0, 0xfffffff0 },
	{ 0x0007c0b0, 0xfffffff0 },
	{ 0x0007c0c0, 0xfffffff0 },
	{ 0x0007c0d0, 0xfffffff0 },
	{ 0x0007c0f0, 0xfffffff0 },
	{ 0x0007c110, 0xfffffff0 },
	{ 0x0007c130, 0xfffffff2 },
	{ }
};

MODULE_DEVICE_TABLE(mdio, smsc_tbl);
