/*
 * STMicroelectronics TPM I2C Linux driver for TPM ST33ZP24
 * Copyright (C) 2009 - 2016 STMicroelectronics
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, see <http://www.gnu.org/licenses/>.
 */

#include <linux/module.h>
#include <linux/i2c.h>
#include <linux/gpio.h>
#include <linux/gpio/consumer.h>
#include <linux/of_irq.h>
#include <linux/of_gpio.h>
#include <linux/acpi.h>
#include <linux/tpm.h>
#include <linux/platform_data/st33zp24.h>

#include "../tpm.h"
#include "st33zp24.h"

#define TPM_DUMMY_BYTE			0xAA

struct st33zp24_i2c_phy {
	struct i2c_client *client;
	u8 buf[ST33ZP24_BUFSIZE + 1];
	int io_lpcpd;
};

/*
 * write8_reg
 * Send byte to the TIS register according to the ST33ZP24 I2C protocol.
 * @param: tpm_register, the tpm tis register where the data should be written
 * @param: tpm_data, the tpm_data to write inside the tpm_register
 * @param: tpm_size, The length of the data
 * @return: Returns negative errno, or else the number of bytes written.
 */
static int write8_reg(void *phy_id, u8 tpm_register, u8 *tpm_data, int tpm_size)
{
	struct st33zp24_i2c_phy *phy = phy_id;

	phy->buf[0] = tpm_register;
	memcpy(phy->buf + 1, tpm_data, tpm_size);
	return i2c_master_send(phy->client, phy->buf, tpm_size + 1);
} /* write8_reg() */

/*
 * read8_reg
 * Recv byte from the TIS register according to the ST33ZP24 I2C protocol.
 * @param: tpm_register, the tpm tis register where the data should be read
 * @param: tpm_data, the TPM response
 * @param: tpm_size, tpm TPM response size to read.
 * @return: number of byte read successfully: should be one if success.
 */
static int read8_reg(void *phy_id, u8 tpm_register, u8 *tpm_data, int tpm_size)
{
	struct st33zp24_i2c_phy *phy = phy_id;
	u8 status = 0;
	u8 data;

	data = TPM_DUMMY_BYTE;
	status = write8_reg(phy, tpm_register, &data, 1);
	if (status == 2)
		status = i2c_master_recv(phy->client, tpm_data, tpm_size);
	return status;
} /* read8_reg() */

/*
 * st33zp24_i2c_send
 * Send byte to the TIS register according to the ST33ZP24 I2C protocol.
 * @param: phy_id, the phy description
 * @param: tpm_register, the tpm tis register where the data should be written
 * @param: tpm_data, the tpm_data to write inside the tpm_register
 * @param: tpm_size, the length of the data
 * @return: number of byte written successfully: should be one if success.
 */
static int st33zp24_i2c_send(void *phy_id, u8 tpm_register, u8 *tpm_data,
			     int tpm_size)
{
	return write8_reg(phy_id, tpm_register | TPM_WRITE_DIRECTION, tpm_data,
			  tpm_size);
}

/*
 * st33zp24_i2c_recv
 * Recv byte from the TIS register according to the ST33ZP24 I2C protocol.
 * @param: phy_id, the phy description
 * @param: tpm_register, the tpm tis register where the data should be read
 * @param: tpm_data, the TPM response
 * @param: tpm_size, tpm TPM response size to read.
 * @return: number of byte read successfully: should be one if success.
 */
static int st33zp24_i2c_recv(void *phy_id, u8 tpm_register, u8 *tpm_data,
			     int tpm_size)
{
	return read8_reg(phy_id, tpm_register, tpm_data, tpm_size);
}

static const struct st33zp24_phy_ops i2c_phy_ops = {
	.send = st33zp24_i2c_send,
	.recv = st33zp24_i2c_recv,
};

static const struct acpi_gpio_params lpcpd_gpios = { 1, 0, false };

static const struct acpi_gpio_mapping acpi_st33zp24_gpios[] = {
	{ "lpcpd-gpios", &lpcpd_gpios, 1 },
	{},
};

static int st33zp24_i2c_acpi_request_resources(struct i2c_client *client)
{
	struct tpm_chip *chip = i2c_get_clientdata(client);
	struct st33zp24_dev *tpm_dev = dev_get_drvdata(&chip->dev);
	struct st33zp24_i2c_phy *phy = tpm_dev->phy_id;
	struct gpio_desc *gpiod_lpcpd;
	struct device *dev = &client->dev;
	int ret;

	ret = devm_acpi_dev_add_driver_gpios(dev, acpi_st33zp24_gpios);
	if (ret)
		return ret;

	/* Get LPCPD GPIO from ACPI */
	gpiod_lpcpd = devm_gpiod_get(dev, "lpcpd", GPIOD_OUT_HIGH);
	if (IS_ERR(gpiod_lpcpd)) {
		dev_err(&client->dev,
			"Failed to retrieve lpcpd-gpios from acpi.\n");
		phy->io_lpcpd = -1;
		/*
		 * lpcpd pin is not specified. This is not an issue as
		 * power management can be also managed by TPM specific
		 * commands. So leave with a success status code.
		 */
		return 0;
	}

	phy->io_lpcpd = desc_to_gpio(gpiod_lpcpd);

	return 0;
}

static int st33zp24_i2c_of_request_resources(struct i2c_client *client)
{
	struct tpm_chip *chip = i2c_get_clientdata(client);
	struct st33zp24_dev *tpm_dev = dev_get_drvdata(&chip->dev);
	struct st33zp24_i2c_phy *phy = tpm_dev->phy_id;
	struct device_node *pp;
	int gpio;
	int ret;

	pp = client->dev.of_node;
	if (!pp) {
		dev_err(&client->dev, "No platform data\n");
		return -ENODEV;
	}

	/* Get GPIO from device tree */
	gpio = of_get_named_gpio(pp, "lpcpd-gpios", 0);
	if (gpio < 0) {
		dev_err(&client->dev,
			"Failed to retrieve lpcpd-gpios from dts.\n");
		phy->io_lpcpd = -1;
		/*
		 * lpcpd pin is not specified. This is not an issue as
		 * power management can be also managed by TPM specific
		 * commands. So leave with a success status code.
		 */
		return 0;
	}
	/* GPIO request and configuration */
	ret = devm_gpio_request_one(&client->dev, gpio,
			GPIOF_OUT_INIT_HIGH, "TPM IO LPCPD");
	if (ret) {
		dev_err(&client->dev, "Failed to request lpcpd pin\n");
		return -ENODEV;
	}
	phy->io_lpcpd = gpio;

	return 0;
}

static int st33zp24_i2c_request_resources(struct i2c_client *client)
{
	struct tpm_chip *chip = i2c_get_clientdata(client);
	struct st33zp24_dev *tpm_dev = dev_get_drvdata(&chip->dev);
	struct st33zp24_i2c_phy *phy = tpm_dev->phy_id;
	struct st33zp24_platform_data *pdata;
	int ret;

	pdata = client->dev.platform_data;
	if (!pdata) {
		dev_err(&client->dev, "No platform data\n");
		return -ENODEV;
	}

	/* store for late use */
	phy->io_lpcpd = pdata->io_lpcpd;

	if (gpio_is_valid(pdata->io_lpcpd)) {
		ret = devm_gpio_request_one(&client->dev,
				pdata->io_lpcpd, GPIOF_OUT_INIT_HIGH,
				"TPM IO_LPCPD");
		if (ret) {
			dev_err(&client->dev, "Failed to request lpcpd pin\n");
			return ret;
		}
	}

	return 0;
}

/*
 * st33zp24_i2c_probe initialize the TPM device
 * @param: client, the i2c_client drescription (TPM I2C description).
 * @param: id, the i2c_device_id struct.
 * @return: 0 in case of success.
 *	 -1 in other case.
 */
static int st33zp24_i2c_probe(struct i2c_client *client,
			      const struct i2c_device_id *id)
{
	int ret;
	struct st33zp24_platform_data *pdata;
	struct st33zp24_i2c_phy *phy;

	if (!client) {
		pr_info("%s: i2c client is NULL. Device not accessible.\n",
			__func__);
		return -ENODEV;
	}

	if (!i2c_check_functionality(client->adapter, I2C_FUNC_I2C)) {
		dev_info(&client->dev, "client not i2c capable\n");
		return -ENODEV;
	}

	phy = devm_kzalloc(&client->dev, sizeof(struct st33zp24_i2c_phy),
			   GFP_KERNEL);
	if (!phy)
		return -ENOMEM;

	phy->client = client;

	pdata = client->dev.platform_data;
	if (!pdata && client->dev.of_node) {
		ret = st33zp24_i2c_of_request_resources(client);
		if (ret)
			return ret;
	} else if (pdata) {
		ret = st33zp24_i2c_request_resources(client);
		if (ret)
			return ret;
	} else if (ACPI_HANDLE(&client->dev)) {
		ret = st33zp24_i2c_acpi_request_resources(client);
		if (ret)
			return ret;
	}

	return st33zp24_probe(phy, &i2c_phy_ops, &client->dev, client->irq,
			      phy->io_lpcpd);
}

/*
 * st33zp24_i2c_remove remove the TPM device
 * @param: client, the i2c_client description (TPM I2C description).
 * @return: 0 in case of success.
 */
static int st33zp24_i2c_remove(struct i2c_client *client)
{
	struct tpm_chip *chip = i2c_get_clientdata(client);
	int ret;

	ret = st33zp24_remove(chip);
	if (ret)
		return ret;

	return 0;
}

static const struct i2c_device_id st33zp24_i2c_id[] = {
	{TPM_ST33_I2C, 0},
	{}
};
MODULE_DEVICE_TABLE(i2c, st33zp24_i2c_id);

static const struct of_device_id of_st33zp24_i2c_match[] = {
	{ .compatible = "st,st33zp24-i2c", },
	{}
};
MODULE_DEVICE_TABLE(of, of_st33zp24_i2c_match);

static const struct acpi_device_id st33zp24_i2c_acpi_match[] = {
	{"SMO3324"},
	{}
};
MODULE_DEVICE_TABLE(acpi, st33zp24_i2c_acpi_match);

static SIMPLE_DEV_PM_OPS(st33zp24_i2c_ops, st33zp24_pm_suspend,
			 st33zp24_pm_resume);

static struct i2c_driver st33zp24_i2c_driver = {
	.driver = {
		.name = TPM_ST33_I2C,
		.pm = &st33zp24_i2c_ops,
		.of_match_table = of_match_ptr(of_st33zp24_i2c_match),
		.acpi_match_table = ACPI_PTR(st33zp24_i2c_acpi_match),
	},
	.probe = st33zp24_i2c_probe,
	.remove = st33zp24_i2c_remove,
	.id_table = st33zp24_i2c_id
};

module_i2c_driver(st33zp24_i2c_driver);

MODULE_AUTHOR("TPM support (TPMsupport@list.st.com)");
MODULE_DESCRIPTION("STM TPM 1.2 I2C ST33 Driver");
MODULE_VERSION("1.3.0");
MODULE_LICENSE("GPL");
