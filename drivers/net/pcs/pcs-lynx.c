// SPDX-License-Identifier: (GPL-2.0+ OR BSD-3-Clause)
/* Copyright 2020 NXP
 * Lynx PCS MDIO helpers
 */

#include <linux/mdio.h>
#include <linux/phylink.h>
#include <linux/pcs-lynx.h>
#include <linux/property.h>

#define SGMII_CLOCK_PERIOD_NS		8 /* PCS is clocked at 125 MHz */
#define LINK_TIMER_VAL(ns)		((u32)((ns) / SGMII_CLOCK_PERIOD_NS))

#define LINK_TIMER_LO			0x12
#define LINK_TIMER_HI			0x13
#define IF_MODE				0x14
#define IF_MODE_SGMII_EN		BIT(0)
#define IF_MODE_USE_SGMII_AN		BIT(1)
#define IF_MODE_SPEED(x)		(((x) << 2) & GENMASK(3, 2))
#define IF_MODE_SPEED_MSK		GENMASK(3, 2)
#define IF_MODE_HALF_DUPLEX		BIT(4)

struct lynx_pcs {
	struct phylink_pcs pcs;
	struct mdio_device *mdio;
};

enum sgmii_speed {
	SGMII_SPEED_10		= 0,
	SGMII_SPEED_100		= 1,
	SGMII_SPEED_1000	= 2,
	SGMII_SPEED_2500	= 2,
};

#define phylink_pcs_to_lynx(pl_pcs) container_of((pl_pcs), struct lynx_pcs, pcs)
#define lynx_to_phylink_pcs(lynx) (&(lynx)->pcs)

static void lynx_pcs_get_state_usxgmii(struct mdio_device *pcs,
				       struct phylink_link_state *state)
{
	struct mii_bus *bus = pcs->bus;
	int addr = pcs->addr;
	int status, lpa;

	status = mdiobus_c45_read(bus, addr, MDIO_MMD_VEND2, MII_BMSR);
	if (status < 0)
		return;

	state->link = !!(status & MDIO_STAT1_LSTATUS);
	state->an_complete = !!(status & MDIO_AN_STAT1_COMPLETE);
	if (!state->link || !state->an_complete)
		return;

	lpa = mdiobus_c45_read(bus, addr, MDIO_MMD_VEND2, MII_LPA);
	if (lpa < 0)
		return;

	phylink_decode_usxgmii_word(state, lpa);
}

static void lynx_pcs_get_state_2500basex(struct mdio_device *pcs,
					 struct phylink_link_state *state)
{
	int bmsr, lpa;

	bmsr = mdiodev_read(pcs, MII_BMSR);
	lpa = mdiodev_read(pcs, MII_LPA);
	if (bmsr < 0 || lpa < 0) {
		state->link = false;
		return;
	}

	state->link = !!(bmsr & BMSR_LSTATUS);
	state->an_complete = !!(bmsr & BMSR_ANEGCOMPLETE);
	if (!state->link)
		return;

	state->speed = SPEED_2500;
	state->pause |= MLO_PAUSE_TX | MLO_PAUSE_RX;
	state->duplex = DUPLEX_FULL;
}

static void lynx_pcs_get_state(struct phylink_pcs *pcs,
			       struct phylink_link_state *state)
{
	struct lynx_pcs *lynx = phylink_pcs_to_lynx(pcs);

	switch (state->interface) {
	case PHY_INTERFACE_MODE_1000BASEX:
	case PHY_INTERFACE_MODE_SGMII:
	case PHY_INTERFACE_MODE_QSGMII:
		phylink_mii_c22_pcs_get_state(lynx->mdio, state);
		break;
	case PHY_INTERFACE_MODE_2500BASEX:
		lynx_pcs_get_state_2500basex(lynx->mdio, state);
		break;
	case PHY_INTERFACE_MODE_USXGMII:
		lynx_pcs_get_state_usxgmii(lynx->mdio, state);
		break;
	case PHY_INTERFACE_MODE_10GBASER:
		phylink_mii_c45_pcs_get_state(lynx->mdio, state);
		break;
	default:
		break;
	}

	dev_dbg(&lynx->mdio->dev,
		"mode=%s/%s/%s link=%u an_complete=%u\n",
		phy_modes(state->interface),
		phy_speed_to_str(state->speed),
		phy_duplex_to_str(state->duplex),
		state->link, state->an_complete);
}

static int lynx_pcs_config_giga(struct mdio_device *pcs,
				phy_interface_t interface,
				const unsigned long *advertising,
				unsigned int neg_mode)
{
	int link_timer_ns;
	u32 link_timer;
	u16 if_mode;
	int err;

	link_timer_ns = phylink_get_link_timer_ns(interface);
	if (link_timer_ns > 0) {
		link_timer = LINK_TIMER_VAL(link_timer_ns);

		mdiodev_write(pcs, LINK_TIMER_LO, link_timer & 0xffff);
		mdiodev_write(pcs, LINK_TIMER_HI, link_timer >> 16);
	}

	if (interface == PHY_INTERFACE_MODE_1000BASEX) {
		if_mode = 0;
	} else {
		/* SGMII and QSGMII */
		if_mode = IF_MODE_SGMII_EN;
		if (neg_mode == PHYLINK_PCS_NEG_INBAND_ENABLED)
			if_mode |= IF_MODE_USE_SGMII_AN;
	}

	err = mdiodev_modify(pcs, IF_MODE,
			     IF_MODE_SGMII_EN | IF_MODE_USE_SGMII_AN,
			     if_mode);
	if (err)
		return err;

	return phylink_mii_c22_pcs_config(pcs, interface, advertising,
					  neg_mode);
}

static int lynx_pcs_config_usxgmii(struct mdio_device *pcs,
				   const unsigned long *advertising,
				   unsigned int neg_mode)
{
	struct mii_bus *bus = pcs->bus;
	int addr = pcs->addr;

	if (neg_mode != PHYLINK_PCS_NEG_INBAND_ENABLED) {
		dev_err(&pcs->dev, "USXGMII only supports in-band AN for now\n");
		return -EOPNOTSUPP;
	}

	/* Configure device ability for the USXGMII Replicator */
	return mdiobus_c45_write(bus, addr, MDIO_MMD_VEND2, MII_ADVERTISE,
				 MDIO_USXGMII_10G | MDIO_USXGMII_LINK |
				 MDIO_USXGMII_FULL_DUPLEX |
				 ADVERTISE_SGMII | ADVERTISE_LPACK);
}

static int lynx_pcs_config(struct phylink_pcs *pcs, unsigned int neg_mode,
			   phy_interface_t ifmode,
			   const unsigned long *advertising, bool permit)
{
	struct lynx_pcs *lynx = phylink_pcs_to_lynx(pcs);

	switch (ifmode) {
	case PHY_INTERFACE_MODE_1000BASEX:
	case PHY_INTERFACE_MODE_SGMII:
	case PHY_INTERFACE_MODE_QSGMII:
		return lynx_pcs_config_giga(lynx->mdio, ifmode, advertising,
					    neg_mode);
	case PHY_INTERFACE_MODE_2500BASEX:
		if (neg_mode == PHYLINK_PCS_NEG_INBAND_ENABLED) {
			dev_err(&lynx->mdio->dev,
				"AN not supported on 3.125GHz SerDes lane\n");
			return -EOPNOTSUPP;
		}
		break;
	case PHY_INTERFACE_MODE_USXGMII:
		return lynx_pcs_config_usxgmii(lynx->mdio, advertising,
					       neg_mode);
	case PHY_INTERFACE_MODE_10GBASER:
		/* Nothing to do here for 10GBASER */
		break;
	default:
		return -EOPNOTSUPP;
	}

	return 0;
}

static void lynx_pcs_an_restart(struct phylink_pcs *pcs)
{
	struct lynx_pcs *lynx = phylink_pcs_to_lynx(pcs);

	phylink_mii_c22_pcs_an_restart(lynx->mdio);
}

static void lynx_pcs_link_up_sgmii(struct mdio_device *pcs,
				   unsigned int neg_mode,
				   int speed, int duplex)
{
	u16 if_mode = 0, sgmii_speed;

	/* The PCS needs to be configured manually only
	 * when not operating on in-band mode
	 */
	if (neg_mode == PHYLINK_PCS_NEG_INBAND_ENABLED)
		return;

	if (duplex == DUPLEX_HALF)
		if_mode |= IF_MODE_HALF_DUPLEX;

	switch (speed) {
	case SPEED_1000:
		sgmii_speed = SGMII_SPEED_1000;
		break;
	case SPEED_100:
		sgmii_speed = SGMII_SPEED_100;
		break;
	case SPEED_10:
		sgmii_speed = SGMII_SPEED_10;
		break;
	case SPEED_UNKNOWN:
		/* Silently don't do anything */
		return;
	default:
		dev_err(&pcs->dev, "Invalid PCS speed %d\n", speed);
		return;
	}
	if_mode |= IF_MODE_SPEED(sgmii_speed);

	mdiodev_modify(pcs, IF_MODE,
		       IF_MODE_HALF_DUPLEX | IF_MODE_SPEED_MSK,
		       if_mode);
}

/* 2500Base-X is SerDes protocol 7 on Felix and 6 on ENETC. It is a SerDes lane
 * clocked at 3.125 GHz which encodes symbols with 8b/10b and does not have
 * auto-negotiation of any link parameters. Electrically it is compatible with
 * a single lane of XAUI.
 * The hardware reference manual wants to call this mode SGMII, but it isn't
 * really, since the fundamental features of SGMII:
 * - Downgrading the link speed by duplicating symbols
 * - Auto-negotiation
 * are not there.
 * The speed is configured at 1000 in the IF_MODE because the clock frequency
 * is actually given by a PLL configured in the Reset Configuration Word (RCW).
 * Since there is no difference between fixed speed SGMII w/o AN and 802.3z w/o
 * AN, we call this PHY interface type 2500Base-X. In case a PHY negotiates a
 * lower link speed on line side, the system-side interface remains fixed at
 * 2500 Mbps and we do rate adaptation through pause frames.
 */
static void lynx_pcs_link_up_2500basex(struct mdio_device *pcs,
				       unsigned int neg_mode,
				       int speed, int duplex)
{
	u16 if_mode = 0;

	if (neg_mode == PHYLINK_PCS_NEG_INBAND_ENABLED) {
		dev_err(&pcs->dev, "AN not supported for 2500BaseX\n");
		return;
	}

	if (duplex == DUPLEX_HALF)
		if_mode |= IF_MODE_HALF_DUPLEX;
	if_mode |= IF_MODE_SPEED(SGMII_SPEED_2500);

	mdiodev_modify(pcs, IF_MODE,
		       IF_MODE_HALF_DUPLEX | IF_MODE_SPEED_MSK,
		       if_mode);
}

static void lynx_pcs_link_up(struct phylink_pcs *pcs, unsigned int neg_mode,
			     phy_interface_t interface,
			     int speed, int duplex)
{
	struct lynx_pcs *lynx = phylink_pcs_to_lynx(pcs);

	switch (interface) {
	case PHY_INTERFACE_MODE_SGMII:
	case PHY_INTERFACE_MODE_QSGMII:
		lynx_pcs_link_up_sgmii(lynx->mdio, neg_mode, speed, duplex);
		break;
	case PHY_INTERFACE_MODE_2500BASEX:
		lynx_pcs_link_up_2500basex(lynx->mdio, neg_mode, speed, duplex);
		break;
	case PHY_INTERFACE_MODE_USXGMII:
		/* At the moment, only in-band AN is supported for USXGMII
		 * so nothing to do in link_up
		 */
		break;
	default:
		break;
	}
}

static const struct phylink_pcs_ops lynx_pcs_phylink_ops = {
	.pcs_get_state = lynx_pcs_get_state,
	.pcs_config = lynx_pcs_config,
	.pcs_an_restart = lynx_pcs_an_restart,
	.pcs_link_up = lynx_pcs_link_up,
};

static struct phylink_pcs *lynx_pcs_create(struct mdio_device *mdio)
{
	struct lynx_pcs *lynx;

	lynx = kzalloc(sizeof(*lynx), GFP_KERNEL);
	if (!lynx)
		return ERR_PTR(-ENOMEM);

	mdio_device_get(mdio);
	lynx->mdio = mdio;
	lynx->pcs.ops = &lynx_pcs_phylink_ops;
	lynx->pcs.neg_mode = true;
	lynx->pcs.poll = true;

	return lynx_to_phylink_pcs(lynx);
}

struct phylink_pcs *lynx_pcs_create_mdiodev(struct mii_bus *bus, int addr)
{
	struct mdio_device *mdio;
	struct phylink_pcs *pcs;

	mdio = mdio_device_create(bus, addr);
	if (IS_ERR(mdio))
		return ERR_CAST(mdio);

	pcs = lynx_pcs_create(mdio);

	/* lynx_create() has taken a refcount on the mdiodev if it was
	 * successful. If lynx_create() fails, this will free the mdio
	 * device here. In any case, we don't need to hold our reference
	 * anymore, and putting it here will allow mdio_device_put() in
	 * lynx_destroy() to automatically free the mdio device.
	 */
	mdio_device_put(mdio);

	return pcs;
}
EXPORT_SYMBOL(lynx_pcs_create_mdiodev);

/*
 * lynx_pcs_create_fwnode() creates a lynx PCS instance from the fwnode
 * device indicated by node.
 *
 * Returns:
 *  -ENODEV if the fwnode is marked unavailable
 *  -EPROBE_DEFER if we fail to find the device
 *  -ENOMEM if we fail to allocate memory
 *  pointer to a phylink_pcs on success
 */
struct phylink_pcs *lynx_pcs_create_fwnode(struct fwnode_handle *node)
{
	struct mdio_device *mdio;
	struct phylink_pcs *pcs;

	if (!fwnode_device_is_available(node))
		return ERR_PTR(-ENODEV);

	mdio = fwnode_mdio_find_device(node);
	if (!mdio)
		return ERR_PTR(-EPROBE_DEFER);

	pcs = lynx_pcs_create(mdio);

	/* lynx_create() has taken a refcount on the mdiodev if it was
	 * successful. If lynx_create() fails, this will free the mdio
	 * device here. In any case, we don't need to hold our reference
	 * anymore, and putting it here will allow mdio_device_put() in
	 * lynx_destroy() to automatically free the mdio device.
	 */
	mdio_device_put(mdio);

	return pcs;
}
EXPORT_SYMBOL_GPL(lynx_pcs_create_fwnode);

void lynx_pcs_destroy(struct phylink_pcs *pcs)
{
	struct lynx_pcs *lynx = phylink_pcs_to_lynx(pcs);

	mdio_device_put(lynx->mdio);
	kfree(lynx);
}
EXPORT_SYMBOL(lynx_pcs_destroy);

MODULE_DESCRIPTION("NXP Lynx PCS phylink library");
MODULE_LICENSE("Dual BSD/GPL");
