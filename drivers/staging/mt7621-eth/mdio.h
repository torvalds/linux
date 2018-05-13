/*   This program is free software; you can redistribute it and/or modify
 *   it under the terms of the GNU General Public License as published by
 *   the Free Software Foundation; version 2 of the License
 *
 *   This program is distributed in the hope that it will be useful,
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *   GNU General Public License for more details.
 *
 *   Copyright (C) 2009-2016 John Crispin <blogic@openwrt.org>
 *   Copyright (C) 2009-2016 Felix Fietkau <nbd@openwrt.org>
 *   Copyright (C) 2013-2016 Michael Lee <igvtee@gmail.com>
 */

#ifndef _RALINK_MDIO_H__
#define _RALINK_MDIO_H__

#ifdef CONFIG_NET_MEDIATEK_MDIO
int mtk_mdio_init(struct mtk_eth *eth);
void mtk_mdio_cleanup(struct mtk_eth *eth);
int mtk_connect_phy_node(struct mtk_eth *eth, struct mtk_mac *mac,
			 struct device_node *phy_node);
#else
static inline int mtk_mdio_init(struct mtk_eth *eth) { return 0; }
static inline void mtk_mdio_cleanup(struct mtk_eth *eth) {}
#endif
#endif
