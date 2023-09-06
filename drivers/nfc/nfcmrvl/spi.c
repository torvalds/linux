// SPDX-License-Identifier: GPL-2.0-only
/*
 * Marvell NFC-over-SPI driver: SPI interface related functions
 *
 * Copyright (C) 2015, Marvell International Ltd.
 */

#include <linux/module.h>
#include <linux/interrupt.h>
#include <linux/nfc.h>
#include <linux/of_irq.h>
#include <net/nfc/nci.h>
#include <net/nfc/nci_core.h>
#include <linux/spi/spi.h>
#include "nfcmrvl.h"

#define SPI_WAIT_HANDSHAKE	1

struct nfcmrvl_spi_drv_data {
	unsigned long flags;
	struct spi_device *spi;
	struct nci_spi *nci_spi;
	struct completion handshake_completion;
	struct nfcmrvl_private *priv;
};

static irqreturn_t nfcmrvl_spi_int_irq_thread_fn(int irq, void *drv_data_ptr)
{
	struct nfcmrvl_spi_drv_data *drv_data = drv_data_ptr;
	struct sk_buff *skb;

	/*
	 * Special case where we are waiting for SPI_INT deassertion to start a
	 * transfer.
	 */
	if (test_and_clear_bit(SPI_WAIT_HANDSHAKE, &drv_data->flags)) {
		complete(&drv_data->handshake_completion);
		return IRQ_HANDLED;
	}

	/* Normal case, SPI_INT deasserted by slave to trigger a master read */

	skb = nci_spi_read(drv_data->nci_spi);
	if (!skb) {
		nfc_err(&drv_data->spi->dev, "failed to read spi packet");
		return IRQ_HANDLED;
	}

	if (nfcmrvl_nci_recv_frame(drv_data->priv, skb) < 0)
		nfc_err(&drv_data->spi->dev, "corrupted RX packet");

	return IRQ_HANDLED;
}

static int nfcmrvl_spi_nci_open(struct nfcmrvl_private *priv)
{
	return 0;
}

static int nfcmrvl_spi_nci_close(struct nfcmrvl_private *priv)
{
	return 0;
}

static int nfcmrvl_spi_nci_send(struct nfcmrvl_private *priv,
				struct sk_buff *skb)
{
	struct nfcmrvl_spi_drv_data *drv_data = priv->drv_data;
	int err;

	/* Reinit completion for slave handshake */
	reinit_completion(&drv_data->handshake_completion);
	set_bit(SPI_WAIT_HANDSHAKE, &drv_data->flags);

	/*
	 * Append a dummy byte at the end of SPI frame. This is due to a
	 * specific DMA implementation in the controller
	 */
	skb_put(skb, 1);

	/* Send the SPI packet */
	err = nci_spi_send(drv_data->nci_spi, &drv_data->handshake_completion,
			   skb);
	if (err)
		nfc_err(priv->dev, "spi_send failed %d", err);

	return err;
}

static void nfcmrvl_spi_nci_update_config(struct nfcmrvl_private *priv,
					  const void *param)
{
	struct nfcmrvl_spi_drv_data *drv_data = priv->drv_data;
	const struct nfcmrvl_fw_spi_config *config = param;

	drv_data->nci_spi->xfer_speed_hz = config->clk;
}

static const struct nfcmrvl_if_ops spi_ops = {
	.nci_open = nfcmrvl_spi_nci_open,
	.nci_close = nfcmrvl_spi_nci_close,
	.nci_send = nfcmrvl_spi_nci_send,
	.nci_update_config = nfcmrvl_spi_nci_update_config,
};

static int nfcmrvl_spi_parse_dt(struct device_node *node,
				struct nfcmrvl_platform_data *pdata)
{
	int ret;

	ret = nfcmrvl_parse_dt(node, pdata);
	if (ret < 0) {
		pr_err("Failed to get generic entries\n");
		return ret;
	}

	ret = irq_of_parse_and_map(node, 0);
	if (!ret) {
		pr_err("Unable to get irq\n");
		return -EINVAL;
	}
	pdata->irq = ret;

	return 0;
}

static int nfcmrvl_spi_probe(struct spi_device *spi)
{
	const struct nfcmrvl_platform_data *pdata;
	struct nfcmrvl_platform_data config;
	struct nfcmrvl_spi_drv_data *drv_data;
	int ret = 0;

	drv_data = devm_kzalloc(&spi->dev, sizeof(*drv_data), GFP_KERNEL);
	if (!drv_data)
		return -ENOMEM;

	drv_data->spi = spi;
	drv_data->priv = NULL;
	spi_set_drvdata(spi, drv_data);

	pdata = spi->dev.platform_data;

	if (!pdata && spi->dev.of_node)
		if (nfcmrvl_spi_parse_dt(spi->dev.of_node, &config) == 0)
			pdata = &config;

	if (!pdata)
		return -EINVAL;

	ret = devm_request_threaded_irq(&drv_data->spi->dev, pdata->irq,
					NULL, nfcmrvl_spi_int_irq_thread_fn,
					IRQF_TRIGGER_FALLING | IRQF_ONESHOT,
					"nfcmrvl_spi_int", drv_data);
	if (ret < 0) {
		nfc_err(&drv_data->spi->dev, "Unable to register IRQ handler");
		return -ENODEV;
	}

	drv_data->priv = nfcmrvl_nci_register_dev(NFCMRVL_PHY_SPI,
						  drv_data, &spi_ops,
						  &drv_data->spi->dev,
						  pdata);
	if (IS_ERR(drv_data->priv))
		return PTR_ERR(drv_data->priv);

	drv_data->priv->support_fw_dnld = true;

	drv_data->nci_spi = nci_spi_allocate_spi(drv_data->spi, 0, 10,
						 drv_data->priv->ndev);

	/* Init completion for slave handshake */
	init_completion(&drv_data->handshake_completion);
	return 0;
}

static int nfcmrvl_spi_remove(struct spi_device *spi)
{
	struct nfcmrvl_spi_drv_data *drv_data = spi_get_drvdata(spi);

	nfcmrvl_nci_unregister_dev(drv_data->priv);
	return 0;
}

static const struct of_device_id of_nfcmrvl_spi_match[] __maybe_unused = {
	{ .compatible = "marvell,nfc-spi", },
	{},
};
MODULE_DEVICE_TABLE(of, of_nfcmrvl_spi_match);

static const struct spi_device_id nfcmrvl_spi_id_table[] = {
	{ "nfcmrvl_spi", 0 },
	{ }
};
MODULE_DEVICE_TABLE(spi, nfcmrvl_spi_id_table);

static struct spi_driver nfcmrvl_spi_driver = {
	.probe		= nfcmrvl_spi_probe,
	.remove		= nfcmrvl_spi_remove,
	.id_table	= nfcmrvl_spi_id_table,
	.driver		= {
		.name		= "nfcmrvl_spi",
		.owner		= THIS_MODULE,
		.of_match_table	= of_match_ptr(of_nfcmrvl_spi_match),
	},
};

module_spi_driver(nfcmrvl_spi_driver);

MODULE_AUTHOR("Marvell International Ltd.");
MODULE_DESCRIPTION("Marvell NFC-over-SPI driver");
MODULE_LICENSE("GPL v2");
