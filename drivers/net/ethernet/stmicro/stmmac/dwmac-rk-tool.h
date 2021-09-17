/* SPDX-License-Identifier: (GPL-2.0+ OR MIT) */
/*
 * Copyright (c) 2020 Fuzhou Rockchip Electronics Co., Ltd
 */

#ifndef __DWMAC_RK_TOOL_H__
#define __DWMAC_RK_TOOL_H__

#include <linux/phy.h>
#include "stmmac.h"

void dwmac_rk_set_rgmii_delayline(struct stmmac_priv *priv, int tx_delay, int rx_delay);
void dwmac_rk_get_rgmii_delayline(struct stmmac_priv *priv, int *tx_delay, int *rx_delay);
int dwmac_rk_get_phy_interface(struct stmmac_priv *priv);

#ifdef CONFIG_DWMAC_ROCKCHIP_TOOL
int dwmac_rk_create_loopback_sysfs(struct device *dev);
int dwmac_rk_remove_loopback_sysfs(struct device *device);
#else
static inline int dwmac_rk_create_loopback_sysfs(struct device *dev)
{
	return 0;
}

static inline int dwmac_rk_remove_loopback_sysfs(struct device *device)
{
	return 0;
}
#endif

#ifdef CONFIG_DWMAC_RK_AUTO_DELAYLINE
int dwmac_rk_get_rgmii_delayline_from_vendor(struct stmmac_priv *priv);
int dwmac_rk_search_rgmii_delayline(struct stmmac_priv *priv);
#endif

#endif /* __DWMAC_RK_TOOL_H__ */

