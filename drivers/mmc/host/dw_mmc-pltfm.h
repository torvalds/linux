/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Synopsys DesignWare Multimedia Card Interface Platform driver
 *
 * Copyright (C) 2012, Samsung Electronics Co., Ltd.
 */

#ifndef _DW_MMC_PLTFM_H_
#define _DW_MMC_PLTFM_H_

extern int dw_mci_pltfm_register(struct platform_device *pdev,
				const struct dw_mci_drv_data *drv_data);
extern void dw_mci_pltfm_remove(struct platform_device *pdev);
extern const struct dev_pm_ops dw_mci_pltfm_pmops;

#endif /* _DW_MMC_PLTFM_H_ */
