/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2025 Yao Zi <ziyao@disroot.org>
 */

#ifndef __STMMAC_LIBPCI_H__
#define __STMMAC_LIBPCI_H__

int stmmac_pci_plat_suspend(struct device *dev, void *bsp_priv);
int stmmac_pci_plat_resume(struct device *dev, void *bsp_priv);

#endif /* __STMMAC_LIBPCI_H__ */
