/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * link caps internal header, for link modes <-> capabilities <-> interfaces
 * conversions.
 */

#ifndef __PHY_CAPS_H
#define __PHY_CAPS_H

#include <linux/ethtool.h>
#include <linux/phy.h>

enum {
	LINK_CAPA_10HD = 0,
	LINK_CAPA_10FD,
	LINK_CAPA_100HD,
	LINK_CAPA_100FD,
	LINK_CAPA_1000HD,
	LINK_CAPA_1000FD,
	LINK_CAPA_2500FD,
	LINK_CAPA_5000FD,
	LINK_CAPA_10000FD,
	LINK_CAPA_20000FD,
	LINK_CAPA_25000FD,
	LINK_CAPA_40000FD,
	LINK_CAPA_50000FD,
	LINK_CAPA_56000FD,
	LINK_CAPA_100000FD,
	LINK_CAPA_200000FD,
	LINK_CAPA_400000FD,
	LINK_CAPA_800000FD,

	__LINK_CAPA_MAX,
};

#define LINK_CAPA_ALL	GENMASK((__LINK_CAPA_MAX - 1), 0)

struct link_capabilities {
	int speed;
	unsigned int duplex;
	__ETHTOOL_DECLARE_LINK_MODE_MASK(linkmodes);
};

int phy_caps_init(void);

size_t phy_caps_speeds(unsigned int *speeds, size_t size,
		       unsigned long *linkmodes);
void phy_caps_linkmode_max_speed(u32 max_speed, unsigned long *linkmodes);
bool phy_caps_valid(int speed, int duplex, const unsigned long *linkmodes);
void phy_caps_linkmodes(unsigned long caps, unsigned long *linkmodes);
unsigned long phy_caps_from_interface(phy_interface_t interface);

const struct link_capabilities *
phy_caps_lookup_by_linkmode(const unsigned long *linkmodes);

const struct link_capabilities *
phy_caps_lookup_by_linkmode_rev(const unsigned long *linkmodes, bool fdx_only);

const struct link_capabilities *
phy_caps_lookup(int speed, unsigned int duplex, const unsigned long *supported,
		bool exact);

#endif /* __PHY_CAPS_H */
