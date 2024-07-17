/* SPDX-License-Identifier: GPL-2.0-only */
/*******************************************************************************
  Copyright (C) 2007-2009  STMicroelectronics Ltd


  Author: Giuseppe Cavallaro <peppe.cavallaro@st.com>
*******************************************************************************/

#ifndef __STMMAC_PLATFORM_H__
#define __STMMAC_PLATFORM_H__

#include "stmmac.h"

struct plat_stmmacenet_data *
devm_stmmac_probe_config_dt(struct platform_device *pdev, u8 *mac);

int stmmac_get_platform_resources(struct platform_device *pdev,
				  struct stmmac_resources *stmmac_res);

int stmmac_pltfr_probe(struct platform_device *pdev,
		       struct plat_stmmacenet_data *plat,
		       struct stmmac_resources *res);
int devm_stmmac_pltfr_probe(struct platform_device *pdev,
			    struct plat_stmmacenet_data *plat,
			    struct stmmac_resources *res);
void stmmac_pltfr_remove(struct platform_device *pdev);
extern const struct dev_pm_ops stmmac_pltfr_pm_ops;

static inline void *get_stmmac_bsp_priv(struct device *dev)
{
	struct net_device *ndev = dev_get_drvdata(dev);
	struct stmmac_priv *priv = netdev_priv(ndev);

	return priv->plat->bsp_priv;
}

#endif /* __STMMAC_PLATFORM_H__ */
