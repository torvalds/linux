/*
 * Copyright (C) 2015 Infineon Technologies AG
 * Copyright (C) 2016 STMicroelectronics SAS
 *
 * Authors:
 * Peter Huewe <peter.huewe@infineon.com>
 * Christophe Ricard <christophe-h.ricard@st.com>
 *
 * Maintained by: <tpmdd-devel@lists.sourceforge.net>
 *
 * Device driver for TCG/TCPA TPM (trusted platform module).
 * Specifications at www.trustedcomputinggroup.org
 *
 * This device driver implements the TPM interface as defined in
 * the TCG TPM Interface Spec version 1.3, revision 27 via _raw/native
 * SPI access_.
 *
 * It is based on the original tpm_tis device driver from Leendert van
 * Dorn and Kyleen Hall and Jarko Sakkinnen.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation, version 2 of the
 * License.
 */

#include <linux/init.h>
#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/slab.h>
#include <linux/interrupt.h>
#include <linux/wait.h>
#include <linux/acpi.h>
#include <linux/freezer.h>

#include <linux/spi/spi.h>
#include <linux/gpio.h>
#include <linux/of_irq.h>
#include <linux/of_gpio.h>
#include <linux/tpm.h>
#include "tpm.h"
#include "tpm_tis_core.h"

#define MAX_SPI_FRAMESIZE 64

struct tpm_tis_spi_phy {
	struct tpm_tis_data priv;
	struct spi_device *spi_device;

	u8 tx_buf[MAX_SPI_FRAMESIZE + 4];
	u8 rx_buf[MAX_SPI_FRAMESIZE + 4];
};

static inline struct tpm_tis_spi_phy *to_tpm_tis_spi_phy(struct tpm_tis_data *data)
{
	return container_of(data, struct tpm_tis_spi_phy, priv);
}

static int tpm_tis_spi_read_bytes(struct tpm_tis_data *data, u32 addr,
				  u16 len, u8 *result)
{
	struct tpm_tis_spi_phy *phy = to_tpm_tis_spi_phy(data);
	int ret, i;
	struct spi_message m;
	struct spi_transfer spi_xfer = {
		.tx_buf = phy->tx_buf,
		.rx_buf = phy->rx_buf,
		.len = 4,
	};

	if (len > MAX_SPI_FRAMESIZE)
		return -ENOMEM;

	phy->tx_buf[0] = 0x80 | (len - 1);
	phy->tx_buf[1] = 0xd4;
	phy->tx_buf[2] = (addr >> 8)  & 0xFF;
	phy->tx_buf[3] = addr	      & 0xFF;

	spi_xfer.cs_change = 1;
	spi_message_init(&m);
	spi_message_add_tail(&spi_xfer, &m);

	spi_bus_lock(phy->spi_device->master);
	ret = spi_sync_locked(phy->spi_device, &m);
	if (ret < 0)
		goto exit;

	memset(phy->tx_buf, 0, len);

	/* According to TCG PTP specification, if there is no TPM present at
	 * all, then the design has a weak pull-up on MISO. If a TPM is not
	 * present, a pull-up on MISO means that the SB controller sees a 1,
	 * and will latch in 0xFF on the read.
	 */
	for (i = 0; (phy->rx_buf[0] & 0x01) == 0 && i < TPM_RETRY; i++) {
		spi_xfer.len = 1;
		spi_message_init(&m);
		spi_message_add_tail(&spi_xfer, &m);
		ret = spi_sync_locked(phy->spi_device, &m);
		if (ret < 0)
			goto exit;
	}

	spi_xfer.cs_change = 0;
	spi_xfer.len = len;
	spi_xfer.rx_buf = result;

	spi_message_init(&m);
	spi_message_add_tail(&spi_xfer, &m);
	ret = spi_sync_locked(phy->spi_device, &m);

exit:
	spi_bus_unlock(phy->spi_device->master);
	return ret;
}

static int tpm_tis_spi_write_bytes(struct tpm_tis_data *data, u32 addr,
				   u16 len, u8 *value)
{
	struct tpm_tis_spi_phy *phy = to_tpm_tis_spi_phy(data);
	int ret, i;
	struct spi_message m;
	struct spi_transfer spi_xfer = {
		.tx_buf = phy->tx_buf,
		.rx_buf = phy->rx_buf,
		.len = 4,
	};

	if (len > MAX_SPI_FRAMESIZE)
		return -ENOMEM;

	phy->tx_buf[0] = len - 1;
	phy->tx_buf[1] = 0xd4;
	phy->tx_buf[2] = (addr >> 8)  & 0xFF;
	phy->tx_buf[3] = addr         & 0xFF;

	spi_xfer.cs_change = 1;
	spi_message_init(&m);
	spi_message_add_tail(&spi_xfer, &m);

	spi_bus_lock(phy->spi_device->master);
	ret = spi_sync_locked(phy->spi_device, &m);
	if (ret < 0)
		goto exit;

	memset(phy->tx_buf, 0, len);

	/* According to TCG PTP specification, if there is no TPM present at
	 * all, then the design has a weak pull-up on MISO. If a TPM is not
	 * present, a pull-up on MISO means that the SB controller sees a 1,
	 * and will latch in 0xFF on the read.
	 */
	for (i = 0; (phy->rx_buf[0] & 0x01) == 0 && i < TPM_RETRY; i++) {
		spi_xfer.len = 1;
		spi_message_init(&m);
		spi_message_add_tail(&spi_xfer, &m);
		ret = spi_sync_locked(phy->spi_device, &m);
		if (ret < 0)
			goto exit;
	}

	spi_xfer.len = len;
	spi_xfer.tx_buf = value;
	spi_xfer.cs_change = 0;
	spi_xfer.tx_buf = value;
	spi_message_init(&m);
	spi_message_add_tail(&spi_xfer, &m);
	ret = spi_sync_locked(phy->spi_device, &m);

exit:
	spi_bus_unlock(phy->spi_device->master);
	return ret;
}

static int tpm_tis_spi_read16(struct tpm_tis_data *data, u32 addr, u16 *result)
{
	int rc;

	rc = data->phy_ops->read_bytes(data, addr, sizeof(u16), (u8 *)result);
	if (!rc)
		*result = le16_to_cpu(*result);
	return rc;
}

static int tpm_tis_spi_read32(struct tpm_tis_data *data, u32 addr, u32 *result)
{
	int rc;

	rc = data->phy_ops->read_bytes(data, addr, sizeof(u32), (u8 *)result);
	if (!rc)
		*result = le32_to_cpu(*result);
	return rc;
}

static int tpm_tis_spi_write32(struct tpm_tis_data *data, u32 addr, u32 value)
{
	value = cpu_to_le32(value);
	return data->phy_ops->write_bytes(data, addr, sizeof(u32),
					   (u8 *)&value);
}

static const struct tpm_tis_phy_ops tpm_spi_phy_ops = {
	.read_bytes = tpm_tis_spi_read_bytes,
	.write_bytes = tpm_tis_spi_write_bytes,
	.read16 = tpm_tis_spi_read16,
	.read32 = tpm_tis_spi_read32,
	.write32 = tpm_tis_spi_write32,
};

static int tpm_tis_spi_probe(struct spi_device *dev)
{
	struct tpm_tis_spi_phy *phy;

	phy = devm_kzalloc(&dev->dev, sizeof(struct tpm_tis_spi_phy),
			   GFP_KERNEL);
	if (!phy)
		return -ENOMEM;

	phy->spi_device = dev;

	return tpm_tis_core_init(&dev->dev, &phy->priv, -1, &tpm_spi_phy_ops,
				 NULL);
}

static SIMPLE_DEV_PM_OPS(tpm_tis_pm, tpm_pm_suspend, tpm_tis_resume);

static int tpm_tis_spi_remove(struct spi_device *dev)
{
	struct tpm_chip *chip = spi_get_drvdata(dev);

	tpm_chip_unregister(chip);
	tpm_tis_remove(chip);
	return 0;
}

static const struct spi_device_id tpm_tis_spi_id[] = {
	{"tpm_tis_spi", 0},
	{}
};
MODULE_DEVICE_TABLE(spi, tpm_tis_spi_id);

static const struct of_device_id of_tis_spi_match[] = {
	{ .compatible = "st,st33htpm-spi", },
	{ .compatible = "infineon,slb9670", },
	{ .compatible = "tcg,tpm_tis-spi", },
	{}
};
MODULE_DEVICE_TABLE(of, of_tis_spi_match);

static const struct acpi_device_id acpi_tis_spi_match[] = {
	{"SMO0768", 0},
	{}
};
MODULE_DEVICE_TABLE(acpi, acpi_tis_spi_match);

static struct spi_driver tpm_tis_spi_driver = {
	.driver = {
		.owner = THIS_MODULE,
		.name = "tpm_tis_spi",
		.pm = &tpm_tis_pm,
		.of_match_table = of_match_ptr(of_tis_spi_match),
		.acpi_match_table = ACPI_PTR(acpi_tis_spi_match),
	},
	.probe = tpm_tis_spi_probe,
	.remove = tpm_tis_spi_remove,
	.id_table = tpm_tis_spi_id,
};
module_spi_driver(tpm_tis_spi_driver);

MODULE_DESCRIPTION("TPM Driver for native SPI access");
MODULE_LICENSE("GPL");
