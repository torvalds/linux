/*
 *
 * Copyright (c) 2011, The Linux Foundation. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 and
 * only version 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 */
#ifndef __MMC_QCOM_DML_H__
#define __MMC_QCOM_DML_H__

#ifdef CONFIG_MMC_QCOM_DML
int dml_hw_init(struct mmci_host *host, struct device_node *np);
void dml_start_xfer(struct mmci_host *host, struct mmc_data *data);
#else
static inline int dml_hw_init(struct mmci_host *host, struct device_node *np)
{
	return -ENOSYS;
}
static inline void dml_start_xfer(struct mmci_host *host, struct mmc_data *data)
{
}
#endif /* CONFIG_MMC_QCOM_DML */

#endif /* __MMC_QCOM_DML_H__ */
