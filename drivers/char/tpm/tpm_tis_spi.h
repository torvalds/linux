/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2015 Infineon Technologies AG
 * Copyright (C) 2016 STMicroelectronics SAS
 */

#ifndef TPM_TIS_SPI_H
#define TPM_TIS_SPI_H

#include "tpm_tis_core.h"

struct tpm_tis_spi_phy {
	struct tpm_tis_data priv;
	struct spi_device *spi_device;
	int (*flow_control)(struct tpm_tis_spi_phy *phy,
			     struct spi_transfer *xfer);
	struct completion ready;
	unsigned long wake_after;

	u8 *iobuf;
};

static inline struct tpm_tis_spi_phy *to_tpm_tis_spi_phy(struct tpm_tis_data *data)
{
	return container_of(data, struct tpm_tis_spi_phy, priv);
}

extern int tpm_tis_spi_init(struct spi_device *spi, struct tpm_tis_spi_phy *phy,
			    int irq, const struct tpm_tis_phy_ops *phy_ops);

extern int tpm_tis_spi_transfer(struct tpm_tis_data *data, u32 addr, u16 len,
				u8 *in, const u8 *out);

#ifdef CONFIG_TCG_TIS_SPI_CR50
extern int cr50_spi_probe(struct spi_device *spi);
#else
static inline int cr50_spi_probe(struct spi_device *spi)
{
	return -ENODEV;
}
#endif

#if defined(CONFIG_PM_SLEEP) && defined(CONFIG_TCG_TIS_SPI_CR50)
extern int tpm_tis_spi_resume(struct device *dev);
#else
#define tpm_tis_spi_resume	NULL
#endif

#endif
