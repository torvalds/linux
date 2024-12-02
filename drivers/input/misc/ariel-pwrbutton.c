// SPDX-License-Identifier: BSD-2-Clause OR GPL-2.0-or-later
/*
 * Dell Wyse 3020 a.k.a. "Ariel" Power Button Driver
 *
 * Copyright (C) 2020 Lubomir Rintel
 */

#include <linux/device.h>
#include <linux/gfp.h>
#include <linux/input.h>
#include <linux/interrupt.h>
#include <linux/mod_devicetable.h>
#include <linux/module.h>
#include <linux/spi/spi.h>

#define RESP_COUNTER(response)	(response.header & 0x3)
#define RESP_SIZE(response)	((response.header >> 2) & 0x3)
#define RESP_TYPE(response)	((response.header >> 4) & 0xf)

struct ec_input_response {
	u8 reserved;
	u8 header;
	u8 data[3];
} __packed;

struct ariel_pwrbutton {
	struct spi_device *client;
	struct input_dev *input;
	u8 msg_counter;
};

static int ec_input_read(struct ariel_pwrbutton *priv,
			 struct ec_input_response *response)
{
	u8 read_request[] = { 0x00, 0x5a, 0xa5, 0x00, 0x00 };
	struct spi_device *spi = priv->client;
	struct spi_transfer t = {
		.tx_buf = read_request,
		.rx_buf = response,
		.len = sizeof(read_request),
	};

	compiletime_assert(sizeof(read_request) == sizeof(*response),
			   "SPI xfer request/response size mismatch");

	return spi_sync_transfer(spi, &t, 1);
}

static irqreturn_t ec_input_interrupt(int irq, void *dev_id)
{
	struct ariel_pwrbutton *priv = dev_id;
	struct spi_device *spi = priv->client;
	struct ec_input_response response;
	int error;
	int i;

	error = ec_input_read(priv, &response);
	if (error < 0) {
		dev_err(&spi->dev, "EC read failed: %d\n", error);
		goto out;
	}

	if (priv->msg_counter == RESP_COUNTER(response)) {
		dev_warn(&spi->dev, "No new data to read?\n");
		goto out;
	}

	priv->msg_counter = RESP_COUNTER(response);

	if (RESP_TYPE(response) != 0x3 && RESP_TYPE(response) != 0xc) {
		dev_dbg(&spi->dev, "Ignoring message that's not kbd data\n");
		goto out;
	}

	for (i = 0; i < RESP_SIZE(response); i++) {
		switch (response.data[i]) {
		case 0x74:
			input_report_key(priv->input, KEY_POWER, 1);
			input_sync(priv->input);
			break;
		case 0xf4:
			input_report_key(priv->input, KEY_POWER, 0);
			input_sync(priv->input);
			break;
		default:
			dev_dbg(&spi->dev, "Unknown scan code: %02x\n",
				response.data[i]);
		}
	}

out:
	return IRQ_HANDLED;
}

static int ariel_pwrbutton_probe(struct spi_device *spi)
{
	struct ec_input_response response;
	struct ariel_pwrbutton *priv;
	int error;

	if (!spi->irq) {
		dev_err(&spi->dev, "Missing IRQ.\n");
		return -EINVAL;
	}

	priv = devm_kzalloc(&spi->dev, sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	priv->client = spi;
	spi_set_drvdata(spi, priv);

	priv->input = devm_input_allocate_device(&spi->dev);
	if (!priv->input)
		return -ENOMEM;
	priv->input->name = "Power Button";
	priv->input->dev.parent = &spi->dev;
	input_set_capability(priv->input, EV_KEY, KEY_POWER);
	error = input_register_device(priv->input);
	if (error) {
		dev_err(&spi->dev, "error registering input device: %d\n", error);
		return error;
	}

	error = ec_input_read(priv, &response);
	if (error < 0) {
		dev_err(&spi->dev, "EC read failed: %d\n", error);
		return error;
	}
	priv->msg_counter = RESP_COUNTER(response);

	error = devm_request_threaded_irq(&spi->dev, spi->irq, NULL,
					  ec_input_interrupt,
					  IRQF_ONESHOT,
					  "Ariel EC Input", priv);

	if (error) {
		dev_err(&spi->dev, "Failed to request IRQ %d: %d\n",
			spi->irq, error);
		return error;
	}

	return 0;
}

static const struct of_device_id ariel_pwrbutton_of_match[] = {
	{ .compatible = "dell,wyse-ariel-ec-input" },
	{ }
};
MODULE_DEVICE_TABLE(of, ariel_pwrbutton_of_match);

static const struct spi_device_id ariel_pwrbutton_spi_ids[] = {
	{ .name = "wyse-ariel-ec-input" },
	{ }
};
MODULE_DEVICE_TABLE(spi, ariel_pwrbutton_spi_ids);

static struct spi_driver ariel_pwrbutton_driver = {
	.driver = {
		.name = "dell-wyse-ariel-ec-input",
		.of_match_table = ariel_pwrbutton_of_match,
	},
	.probe = ariel_pwrbutton_probe,
	.id_table = ariel_pwrbutton_spi_ids,
};
module_spi_driver(ariel_pwrbutton_driver);

MODULE_AUTHOR("Lubomir Rintel <lkundrak@v3.sk>");
MODULE_DESCRIPTION("Dell Wyse 3020 Power Button Input Driver");
MODULE_LICENSE("Dual BSD/GPL");
