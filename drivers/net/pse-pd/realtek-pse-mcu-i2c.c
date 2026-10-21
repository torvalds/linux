// SPDX-License-Identifier: GPL-2.0-or-later

#include <linux/delay.h>
#include <linux/i2c.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/pse-pd/pse.h>

#include "realtek-pse-mcu.h"

/*
 * The core has already waited RTPSE_MCU_RESPONSE_MS before calling us, so
 * the response is normally ready on the very first read. For commands the
 * MCU produces more slowly, keep polling at the typical response cadence
 * up to the worst-case ceiling.
 */
#define RTPSE_MCU_I2C_RETRY_MS	RTPSE_MCU_RESPONSE_MS
#define RTPSE_MCU_I2C_MAX_TRIES	(RTPSE_MCU_RESPONSE_MAX_MS / RTPSE_MCU_I2C_RETRY_MS)

static int rtpse_mcu_i2c_smbus_send(struct rtpse_mcu_ctrl *pse, const struct rtpse_mcu_msg *req)
{
	struct i2c_client *client = to_i2c_client(pse->dev);

	/* Send opcode as SMBus command byte; remaining 11 bytes as block data */
	return i2c_smbus_write_i2c_block_data(client, req->opcode, RTPSE_MCU_MSG_SIZE - 1,
					      (const u8 *)req + 1);
}

static int rtpse_mcu_i2c_smbus_recv(struct rtpse_mcu_ctrl *pse, const struct rtpse_mcu_msg *req,
				    struct rtpse_mcu_msg *resp)
{
	struct i2c_client *client = to_i2c_client(pse->dev);
	int tries, ret;

	for (tries = 0; tries < RTPSE_MCU_I2C_MAX_TRIES; tries++) {
		if (tries > 0)
			msleep(RTPSE_MCU_I2C_RETRY_MS);

		/* MCU needs 0x00 as command byte for read */
		ret = i2c_smbus_read_i2c_block_data(client, 0x00,
						    RTPSE_MCU_MSG_SIZE,
						    (u8 *)resp);
		if (ret < 0)
			return ret;
		if (ret == RTPSE_MCU_MSG_SIZE && rtpse_mcu_resp_is_final(req, resp))
			return 0;
	}

	return -ETIMEDOUT;
}

static const struct rtpse_mcu_transport_ops rtpse_mcu_i2c_smbus_ops = {
	.send = rtpse_mcu_i2c_smbus_send,
	.recv = rtpse_mcu_i2c_smbus_recv,
};

static int rtpse_mcu_i2c_native_send(struct rtpse_mcu_ctrl *pse, const struct rtpse_mcu_msg *req)
{
	struct i2c_client *client = to_i2c_client(pse->dev);
	int ret;

	ret = i2c_master_send(client, (const u8 *)req, RTPSE_MCU_MSG_SIZE);
	if (ret < 0)
		return ret;
	return ret == RTPSE_MCU_MSG_SIZE ? 0 : -EIO;
}

static int rtpse_mcu_i2c_native_recv(struct rtpse_mcu_ctrl *pse, const struct rtpse_mcu_msg *req,
				     struct rtpse_mcu_msg *resp)
{
	struct i2c_client *client = to_i2c_client(pse->dev);
	int tries, ret;

	for (tries = 0; tries < RTPSE_MCU_I2C_MAX_TRIES; tries++) {
		if (tries > 0)
			msleep(RTPSE_MCU_I2C_RETRY_MS);

		ret = i2c_master_recv(client, (u8 *)resp, RTPSE_MCU_MSG_SIZE);
		if (ret < 0)
			return ret;
		if (ret == RTPSE_MCU_MSG_SIZE && rtpse_mcu_resp_is_final(req, resp))
			return 0;
	}

	return -ETIMEDOUT;
}

static const struct rtpse_mcu_transport_ops rtpse_mcu_i2c_native_ops = {
	.send = rtpse_mcu_i2c_native_send,
	.recv = rtpse_mcu_i2c_native_recv,
};

static int rtpse_mcu_i2c_probe(struct i2c_client *client)
{
	struct device *dev = &client->dev;
	const struct rtpse_mcu_match_data *match;
	struct rtpse_mcu_ctrl *pse;
	bool use_native;

	match = device_get_match_data(dev);
	if (!match)
		return dev_err_probe(dev, -ENODEV, "missing match data\n");

	/* The framing (raw I2C vs SMBus) is carried by the match data. */
	use_native = match->native_i2c;
	if (use_native) {
		if (!i2c_check_functionality(client->adapter, I2C_FUNC_I2C))
			return dev_err_probe(dev, -EOPNOTSUPP,
				"plain-I2C MCU protocol requires I2C-capable adapter\n");
	} else {
		if (!i2c_check_functionality(client->adapter,
					     I2C_FUNC_SMBUS_WRITE_I2C_BLOCK |
					     I2C_FUNC_SMBUS_READ_I2C_BLOCK))
			return dev_err_probe(dev, -EOPNOTSUPP,
				"SMBus MCU protocol requires SMBus I2C-block support\n");
	}

	pse = devm_kzalloc(dev, sizeof(*pse), GFP_KERNEL);
	if (!pse)
		return -ENOMEM;

	pse->dev = dev;
	pse->pcdev.owner = THIS_MODULE;
	pse->transport = use_native ? &rtpse_mcu_i2c_native_ops : &rtpse_mcu_i2c_smbus_ops;

	return rtpse_mcu_register(pse);
}

static const struct of_device_id rtpse_mcu_i2c_of_match[] = {
	{ .compatible = "realtek,pse-mcu-gen1-smbus", .data = &rtpse_mcu_gen1_data },
	{ .compatible = "realtek,pse-mcu-gen2-smbus", .data = &rtpse_mcu_gen2_data },
	{ .compatible = "realtek,pse-mcu-gen2-i2c", .data = &rtpse_mcu_gen2_i2c_data },
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, rtpse_mcu_i2c_of_match);

static struct i2c_driver rtpse_mcu_i2c_driver = {
	.driver = {
		.name		= "realtek-pse-mcu-i2c",
		.of_match_table	= rtpse_mcu_i2c_of_match,
	},
	.probe		= rtpse_mcu_i2c_probe,
};
module_i2c_driver(rtpse_mcu_i2c_driver);

MODULE_AUTHOR("Jonas Jelonek <jelonek.jonas@gmail.com>");
MODULE_DESCRIPTION("Realtek PSE MCU driver (I2C transport)");
MODULE_LICENSE("GPL");
