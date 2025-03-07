// SPDX-License-Identifier: GPL-2.0-or-later

#include <linux/ethtool.h>
#include <linux/linkmode.h>
#include <linux/phy.h>

#include "phy-caps.h"

static struct link_capabilities link_caps[__LINK_CAPA_MAX] __ro_after_init = {
	{ SPEED_10, DUPLEX_HALF, {0} }, /* LINK_CAPA_10HD */
	{ SPEED_10, DUPLEX_FULL, {0} }, /* LINK_CAPA_10FD */
	{ SPEED_100, DUPLEX_HALF, {0} }, /* LINK_CAPA_100HD */
	{ SPEED_100, DUPLEX_FULL, {0} }, /* LINK_CAPA_100FD */
	{ SPEED_1000, DUPLEX_HALF, {0} }, /* LINK_CAPA_1000HD */
	{ SPEED_1000, DUPLEX_FULL, {0} }, /* LINK_CAPA_1000FD */
	{ SPEED_2500, DUPLEX_FULL, {0} }, /* LINK_CAPA_2500FD */
	{ SPEED_5000, DUPLEX_FULL, {0} }, /* LINK_CAPA_5000FD */
	{ SPEED_10000, DUPLEX_FULL, {0} }, /* LINK_CAPA_10000FD */
	{ SPEED_20000, DUPLEX_FULL, {0} }, /* LINK_CAPA_20000FD */
	{ SPEED_25000, DUPLEX_FULL, {0} }, /* LINK_CAPA_25000FD */
	{ SPEED_40000, DUPLEX_FULL, {0} }, /* LINK_CAPA_40000FD */
	{ SPEED_50000, DUPLEX_FULL, {0} }, /* LINK_CAPA_50000FD */
	{ SPEED_56000, DUPLEX_FULL, {0} }, /* LINK_CAPA_56000FD */
	{ SPEED_100000, DUPLEX_FULL, {0} }, /* LINK_CAPA_100000FD */
	{ SPEED_200000, DUPLEX_FULL, {0} }, /* LINK_CAPA_200000FD */
	{ SPEED_400000, DUPLEX_FULL, {0} }, /* LINK_CAPA_400000FD */
	{ SPEED_800000, DUPLEX_FULL, {0} }, /* LINK_CAPA_800000FD */
};

static int speed_duplex_to_capa(int speed, unsigned int duplex)
{
	if (duplex == DUPLEX_UNKNOWN ||
	    (speed > SPEED_1000 && duplex != DUPLEX_FULL))
		return -EINVAL;

	switch (speed) {
	case SPEED_10: return duplex == DUPLEX_FULL ?
			      LINK_CAPA_10FD : LINK_CAPA_10HD;
	case SPEED_100: return duplex == DUPLEX_FULL ?
			       LINK_CAPA_100FD : LINK_CAPA_100HD;
	case SPEED_1000: return duplex == DUPLEX_FULL ?
				LINK_CAPA_1000FD : LINK_CAPA_1000HD;
	case SPEED_2500: return LINK_CAPA_2500FD;
	case SPEED_5000: return LINK_CAPA_5000FD;
	case SPEED_10000: return LINK_CAPA_10000FD;
	case SPEED_20000: return LINK_CAPA_20000FD;
	case SPEED_25000: return LINK_CAPA_25000FD;
	case SPEED_40000: return LINK_CAPA_40000FD;
	case SPEED_50000: return LINK_CAPA_50000FD;
	case SPEED_56000: return LINK_CAPA_56000FD;
	case SPEED_100000: return LINK_CAPA_100000FD;
	case SPEED_200000: return LINK_CAPA_200000FD;
	case SPEED_400000: return LINK_CAPA_400000FD;
	case SPEED_800000: return LINK_CAPA_800000FD;
	}

	return -EINVAL;
}

#define for_each_link_caps_asc_speed(cap) \
	for (cap = link_caps; cap < &link_caps[__LINK_CAPA_MAX]; cap++)

#define for_each_link_caps_desc_speed(cap) \
	for (cap = &link_caps[__LINK_CAPA_MAX - 1]; cap >= link_caps; cap--)

/**
 * phy_caps_init() - Initializes the link_caps array from the link_mode_params.
 *
 * Returns: 0 if phy caps init was successful, -EINVAL if we found an
 *	    unexpected linkmode setting that requires LINK_CAPS update.
 *
 */
int phy_caps_init(void)
{
	const struct link_mode_info *linkmode;
	int i, capa;

	/* Fill the caps array from net/ethtool/common.c */
	for (i = 0; i < __ETHTOOL_LINK_MODE_MASK_NBITS; i++) {
		linkmode = &link_mode_params[i];
		capa = speed_duplex_to_capa(linkmode->speed, linkmode->duplex);

		if (capa < 0) {
			if (linkmode->speed != SPEED_UNKNOWN) {
				pr_err("Unknown speed %d, please update LINK_CAPS\n",
				       linkmode->speed);
				return -EINVAL;
			}
			continue;
		}

		__set_bit(i, link_caps[capa].linkmodes);
	}

	return 0;
}

/**
 * phy_caps_speeds() - Fill an array of supported SPEED_* values for given modes
 * @speeds: Output array to store the speeds list into
 * @size: Size of the output array
 * @linkmodes: Linkmodes to get the speeds from
 *
 * Fills the speeds array with all possible speeds that can be achieved with
 * the specified linkmodes.
 *
 * Returns: The number of speeds filled into the array. If the input array isn't
 *	    big enough to store all speeds, fill it as much as possible.
 */
size_t phy_caps_speeds(unsigned int *speeds, size_t size,
		       unsigned long *linkmodes)
{
	struct link_capabilities *lcap;
	size_t count = 0;

	for_each_link_caps_asc_speed(lcap) {
		if (linkmode_intersects(lcap->linkmodes, linkmodes) &&
		    (count == 0 || speeds[count - 1] != lcap->speed)) {
			speeds[count++] = lcap->speed;
			if (count >= size)
				break;
		}
	}

	return count;
}

/**
 * phy_caps_lookup_by_linkmode() - Lookup the fastest matching link_capabilities
 * @linkmodes: Linkmodes to match against
 *
 * Returns: The highest-speed link_capabilities that intersects the given
 *	    linkmodes. In case several DUPLEX_ options exist at that speed,
 *	    DUPLEX_FULL is matched first. NULL is returned if no match.
 */
const struct link_capabilities *
phy_caps_lookup_by_linkmode(const unsigned long *linkmodes)
{
	struct link_capabilities *lcap;

	for_each_link_caps_desc_speed(lcap)
		if (linkmode_intersects(lcap->linkmodes, linkmodes))
			return lcap;

	return NULL;
}

/**
 * phy_caps_lookup_by_linkmode_rev() - Lookup the slowest matching link_capabilities
 * @linkmodes: Linkmodes to match against
 * @fdx_only: Full duplex match only when set
 *
 * Returns: The lowest-speed link_capabilities that intersects the given
 *	    linkmodes. When set, fdx_only will ignore half-duplex matches.
 *	    NULL is returned if no match.
 */
const struct link_capabilities *
phy_caps_lookup_by_linkmode_rev(const unsigned long *linkmodes, bool fdx_only)
{
	struct link_capabilities *lcap;

	for_each_link_caps_asc_speed(lcap) {
		if (fdx_only && lcap->duplex != DUPLEX_FULL)
			continue;

		if (linkmode_intersects(lcap->linkmodes, linkmodes))
			return lcap;
	}

	return NULL;
}

/**
 * phy_caps_lookup() - Lookup capabilities by speed/duplex that matches a mask
 * @speed: Speed to match
 * @duplex: Duplex to match
 * @supported: Mask of linkmodes to match
 * @exact: Perform an exact match or not.
 *
 * Lookup a link_capabilities entry that intersect the supported linkmodes mask,
 * and that matches the passed speed and duplex.
 *
 * When @exact is set, an exact match is performed on speed and duplex, meaning
 * that if the linkmodes for the given speed and duplex intersect the supported
 * mask, this capability is returned, otherwise we don't have a match and return
 * NULL.
 *
 * When @exact is not set, we return either an exact match, or matching capabilities
 * at lower speed, or the lowest matching speed, or NULL.
 *
 * Returns: a matched link_capabilities according to the above process, NULL
 *	    otherwise.
 */
const struct link_capabilities *
phy_caps_lookup(int speed, unsigned int duplex, const unsigned long *supported,
		bool exact)
{
	const struct link_capabilities *lcap, *last = NULL;

	for_each_link_caps_desc_speed(lcap) {
		if (linkmode_intersects(lcap->linkmodes, supported)) {
			last = lcap;
			/* exact match on speed and duplex*/
			if (lcap->speed == speed && lcap->duplex == duplex) {
				return lcap;
			} else if (!exact) {
				if (lcap->speed <= speed)
					return lcap;
			}
		}
	}

	if (!exact)
		return last;

	return NULL;
}
EXPORT_SYMBOL_GPL(phy_caps_lookup);

/**
 * phy_caps_linkmode_max_speed() - Clamp a linkmodes set to a max speed
 * @max_speed: Speed limit for the linkmode set
 * @linkmodes: Linkmodes to limit
 */
void phy_caps_linkmode_max_speed(u32 max_speed, unsigned long *linkmodes)
{
	struct link_capabilities *lcap;

	for_each_link_caps_desc_speed(lcap)
		if (lcap->speed > max_speed)
			linkmode_andnot(linkmodes, linkmodes, lcap->linkmodes);
		else
			break;
}

/**
 * phy_caps_valid() - Validate a linkmodes set agains given speed and duplex
 * @speed: input speed to validate
 * @duplex: input duplex to validate. Passing DUPLEX_UNKNOWN is always not valid
 * @linkmodes: The linkmodes to validate
 *
 * Returns: True if at least one of the linkmodes in @linkmodes can function at
 *          the given speed and duplex, false otherwise.
 */
bool phy_caps_valid(int speed, int duplex, const unsigned long *linkmodes)
{
	int capa = speed_duplex_to_capa(speed, duplex);

	if (capa < 0)
		return false;

	return linkmode_intersects(link_caps[capa].linkmodes, linkmodes);
}

/**
 * phy_caps_linkmodes() - Convert a bitfield of capabilities into linkmodes
 * @caps: The list of caps, each bit corresponding to a LINK_CAPA value
 * @linkmodes: The set of linkmodes to fill. Must be previously initialized.
 */
void phy_caps_linkmodes(unsigned long caps, unsigned long *linkmodes)
{
	unsigned long capa;

	for_each_set_bit(capa, &caps, __LINK_CAPA_MAX)
		linkmode_or(linkmodes, linkmodes, link_caps[capa].linkmodes);
}
EXPORT_SYMBOL_GPL(phy_caps_linkmodes);

/**
 * phy_caps_from_interface() - Get the link capa from a given PHY interface
 * @interface: The PHY interface we want to get the possible Speed/Duplex from
 *
 * Returns: A bitmask of LINK_CAPA_xxx values that can be achieved with the
 *          provided interface.
 */
unsigned long phy_caps_from_interface(phy_interface_t interface)
{
	unsigned long link_caps = 0;

	switch (interface) {
	case PHY_INTERFACE_MODE_USXGMII:
		link_caps |= BIT(LINK_CAPA_10000FD) | BIT(LINK_CAPA_5000FD);
		fallthrough;

	case PHY_INTERFACE_MODE_10G_QXGMII:
		link_caps |= BIT(LINK_CAPA_2500FD);
		fallthrough;

	case PHY_INTERFACE_MODE_RGMII_TXID:
	case PHY_INTERFACE_MODE_RGMII_RXID:
	case PHY_INTERFACE_MODE_RGMII_ID:
	case PHY_INTERFACE_MODE_RGMII:
	case PHY_INTERFACE_MODE_PSGMII:
	case PHY_INTERFACE_MODE_QSGMII:
	case PHY_INTERFACE_MODE_QUSGMII:
	case PHY_INTERFACE_MODE_SGMII:
	case PHY_INTERFACE_MODE_GMII:
		link_caps |= BIT(LINK_CAPA_1000HD) | BIT(LINK_CAPA_1000FD);
		fallthrough;

	case PHY_INTERFACE_MODE_REVRMII:
	case PHY_INTERFACE_MODE_RMII:
	case PHY_INTERFACE_MODE_SMII:
	case PHY_INTERFACE_MODE_REVMII:
	case PHY_INTERFACE_MODE_MII:
		link_caps |= BIT(LINK_CAPA_10HD) | BIT(LINK_CAPA_10FD);
		fallthrough;

	case PHY_INTERFACE_MODE_100BASEX:
		link_caps |= BIT(LINK_CAPA_100HD) | BIT(LINK_CAPA_100FD);
		break;

	case PHY_INTERFACE_MODE_TBI:
	case PHY_INTERFACE_MODE_MOCA:
	case PHY_INTERFACE_MODE_RTBI:
	case PHY_INTERFACE_MODE_1000BASEX:
		link_caps |= BIT(LINK_CAPA_1000HD);
		fallthrough;
	case PHY_INTERFACE_MODE_1000BASEKX:
	case PHY_INTERFACE_MODE_TRGMII:
		link_caps |= BIT(LINK_CAPA_1000FD);
		break;

	case PHY_INTERFACE_MODE_2500BASEX:
		link_caps |= BIT(LINK_CAPA_2500FD);
		break;

	case PHY_INTERFACE_MODE_5GBASER:
		link_caps |= BIT(LINK_CAPA_5000FD);
		break;

	case PHY_INTERFACE_MODE_XGMII:
	case PHY_INTERFACE_MODE_RXAUI:
	case PHY_INTERFACE_MODE_XAUI:
	case PHY_INTERFACE_MODE_10GBASER:
	case PHY_INTERFACE_MODE_10GKR:
		link_caps |= BIT(LINK_CAPA_10000FD);
		break;

	case PHY_INTERFACE_MODE_25GBASER:
		link_caps |= BIT(LINK_CAPA_25000FD);
		break;

	case PHY_INTERFACE_MODE_XLGMII:
		link_caps |= BIT(LINK_CAPA_40000FD);
		break;

	case PHY_INTERFACE_MODE_INTERNAL:
		link_caps |= LINK_CAPA_ALL;
		break;

	case PHY_INTERFACE_MODE_NA:
	case PHY_INTERFACE_MODE_MAX:
		break;
	}

	return link_caps;
}
EXPORT_SYMBOL_GPL(phy_caps_from_interface);
