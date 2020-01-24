// SPDX-License-Identifier: GPL-2.0
/*
 * Microchip / Atmel ECC (I2C) driver.
 *
 * Copyright (c) 2017, Microchip Technology Inc.
 * Author: Tudor Ambarus <tudor.ambarus@microchip.com>
 */

#include <linux/bitrev.h>
#include <linux/crc16.h>
#include <linux/delay.h>
#include <linux/device.h>
#include <linux/err.h>
#include <linux/errno.h>
#include <linux/i2c.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/scatterlist.h>
#include <linux/slab.h>
#include <linux/workqueue.h>
#include "atmel-i2c.h"

static const struct {
	u8 value;
	const char *error_text;
} error_list[] = {
	{ 0x01, "CheckMac or Verify miscompare" },
	{ 0x03, "Parse Error" },
	{ 0x05, "ECC Fault" },
	{ 0x0F, "Execution Error" },
	{ 0xEE, "Watchdog about to expire" },
	{ 0xFF, "CRC or other communication error" },
};

/**
 * atmel_i2c_checksum() - Generate 16-bit CRC as required by ATMEL ECC.
 * CRC16 verification of the count, opcode, param1, param2 and data bytes.
 * The checksum is saved in little-endian format in the least significant
 * two bytes of the command. CRC polynomial is 0x8005 and the initial register
 * value should be zero.
 *
 * @cmd : structure used for communicating with the device.
 */
static void atmel_i2c_checksum(struct atmel_i2c_cmd *cmd)
{
	u8 *data = &cmd->count;
	size_t len = cmd->count - CRC_SIZE;
	__le16 *__crc16 = (__le16 *)(data + len);

	*__crc16 = cpu_to_le16(bitrev16(crc16(0, data, len)));
}

void atmel_i2c_init_read_cmd(struct atmel_i2c_cmd *cmd)
{
	cmd->word_addr = COMMAND;
	cmd->opcode = OPCODE_READ;
	/*
	 * Read the word from Configuration zone that contains the lock bytes
	 * (UserExtra, Selector, LockValue, LockConfig).
	 */
	cmd->param1 = CONFIG_ZONE;
	cmd->param2 = cpu_to_le16(DEVICE_LOCK_ADDR);
	cmd->count = READ_COUNT;

	atmel_i2c_checksum(cmd);

	cmd->msecs = MAX_EXEC_TIME_READ;
	cmd->rxsize = READ_RSP_SIZE;
}
EXPORT_SYMBOL(atmel_i2c_init_read_cmd);

void atmel_i2c_init_random_cmd(struct atmel_i2c_cmd *cmd)
{
	cmd->word_addr = COMMAND;
	cmd->opcode = OPCODE_RANDOM;
	cmd->param1 = 0;
	cmd->param2 = 0;
	cmd->count = RANDOM_COUNT;

	atmel_i2c_checksum(cmd);

	cmd->msecs = MAX_EXEC_TIME_RANDOM;
	cmd->rxsize = RANDOM_RSP_SIZE;
}
EXPORT_SYMBOL(atmel_i2c_init_random_cmd);

void atmel_i2c_init_genkey_cmd(struct atmel_i2c_cmd *cmd, u16 keyid)
{
	cmd->word_addr = COMMAND;
	cmd->count = GENKEY_COUNT;
	cmd->opcode = OPCODE_GENKEY;
	cmd->param1 = GENKEY_MODE_PRIVATE;
	/* a random private key will be generated and stored in slot keyID */
	cmd->param2 = cpu_to_le16(keyid);

	atmel_i2c_checksum(cmd);

	cmd->msecs = MAX_EXEC_TIME_GENKEY;
	cmd->rxsize = GENKEY_RSP_SIZE;
}
EXPORT_SYMBOL(atmel_i2c_init_genkey_cmd);

int atmel_i2c_init_ecdh_cmd(struct atmel_i2c_cmd *cmd,
			    struct scatterlist *pubkey)
{
	size_t copied;

	cmd->word_addr = COMMAND;
	cmd->count = ECDH_COUNT;
	cmd->opcode = OPCODE_ECDH;
	cmd->param1 = ECDH_PREFIX_MODE;
	/* private key slot */
	cmd->param2 = cpu_to_le16(DATA_SLOT_2);

	/*
	 * The device only supports NIST P256 ECC keys. The public key size will
	 * always be the same. Use a macro for the key size to avoid unnecessary
	 * computations.
	 */
	copied = sg_copy_to_buffer(pubkey,
				   sg_nents_for_len(pubkey,
						    ATMEL_ECC_PUBKEY_SIZE),
				   cmd->data, ATMEL_ECC_PUBKEY_SIZE);
	if (copied != ATMEL_ECC_PUBKEY_SIZE)
		return -EINVAL;

	atmel_i2c_checksum(cmd);

	cmd->msecs = MAX_EXEC_TIME_ECDH;
	cmd->rxsize = ECDH_RSP_SIZE;

	return 0;
}
EXPORT_SYMBOL(atmel_i2c_init_ecdh_cmd);

/*
 * After wake and after execution of a command, there will be error, status, or
 * result bytes in the device's output register that can be retrieved by the
 * system. When the length of that group is four bytes, the codes returned are
 * detailed in error_list.
 */
static int atmel_i2c_status(struct device *dev, u8 *status)
{
	size_t err_list_len = ARRAY_SIZE(error_list);
	int i;
	u8 err_id = status[1];

	if (*status != STATUS_SIZE)
		return 0;

	if (err_id == STATUS_WAKE_SUCCESSFUL || err_id == STATUS_NOERR)
		return 0;

	for (i = 0; i < err_list_len; i++)
		if (error_list[i].value == err_id)
			break;

	/* if err_id is not in the error_list then ignore it */
	if (i != err_list_len) {
		dev_err(dev, "%02x: %s:\n", err_id, error_list[i].error_text);
		return err_id;
	}

	return 0;
}

static int atmel_i2c_wakeup(struct i2c_client *client)
{
	struct atmel_i2c_client_priv *i2c_priv = i2c_get_clientdata(client);
	u8 status[STATUS_RSP_SIZE];
	int ret;

	/*
	 * The device ignores any levels or transitions on the SCL pin when the
	 * device is idle, asleep or during waking up. Don't check for error
	 * when waking up the device.
	 */
	i2c_master_send(client, i2c_priv->wake_token, i2c_priv->wake_token_sz);

	/*
	 * Wait to wake the device. Typical execution times for ecdh and genkey
	 * are around tens of milliseconds. Delta is chosen to 50 microseconds.
	 */
	usleep_range(TWHI_MIN, TWHI_MAX);

	ret = i2c_master_recv(client, status, STATUS_SIZE);
	if (ret < 0)
		return ret;

	return atmel_i2c_status(&client->dev, status);
}

static int atmel_i2c_sleep(struct i2c_client *client)
{
	u8 sleep = SLEEP_TOKEN;

	return i2c_master_send(client, &sleep, 1);
}

/*
 * atmel_i2c_send_receive() - send a command to the device and receive its
 *                            response.
 * @client: i2c client device
 * @cmd   : structure used to communicate with the device
 *
 * After the device receives a Wake token, a watchdog counter starts within the
 * device. After the watchdog timer expires, the device enters sleep mode
 * regardless of whether some I/O transmission or command execution is in
 * progress. If a command is attempted when insufficient time remains prior to
 * watchdog timer execution, the device will return the watchdog timeout error
 * code without attempting to execute the command. There is no way to reset the
 * counter other than to put the device into sleep or idle mode and then
 * wake it up again.
 */
int atmel_i2c_send_receive(struct i2c_client *client, struct atmel_i2c_cmd *cmd)
{
	struct atmel_i2c_client_priv *i2c_priv = i2c_get_clientdata(client);
	int ret;

	mutex_lock(&i2c_priv->lock);

	ret = atmel_i2c_wakeup(client);
	if (ret)
		goto err;

	/* send the command */
	ret = i2c_master_send(client, (u8 *)cmd, cmd->count + WORD_ADDR_SIZE);
	if (ret < 0)
		goto err;

	/* delay the appropriate amount of time for command to execute */
	msleep(cmd->msecs);

	/* receive the response */
	ret = i2c_master_recv(client, cmd->data, cmd->rxsize);
	if (ret < 0)
		goto err;

	/* put the device into low-power mode */
	ret = atmel_i2c_sleep(client);
	if (ret < 0)
		goto err;

	mutex_unlock(&i2c_priv->lock);
	return atmel_i2c_status(&client->dev, cmd->data);
err:
	mutex_unlock(&i2c_priv->lock);
	return ret;
}
EXPORT_SYMBOL(atmel_i2c_send_receive);

static void atmel_i2c_work_handler(struct work_struct *work)
{
	struct atmel_i2c_work_data *work_data =
			container_of(work, struct atmel_i2c_work_data, work);
	struct atmel_i2c_cmd *cmd = &work_data->cmd;
	struct i2c_client *client = work_data->client;
	int status;

	status = atmel_i2c_send_receive(client, cmd);
	work_data->cbk(work_data, work_data->areq, status);
}

void atmel_i2c_enqueue(struct atmel_i2c_work_data *work_data,
		       void (*cbk)(struct atmel_i2c_work_data *work_data,
				   void *areq, int status),
		       void *areq)
{
	work_data->cbk = (void *)cbk;
	work_data->areq = areq;

	INIT_WORK(&work_data->work, atmel_i2c_work_handler);
	schedule_work(&work_data->work);
}
EXPORT_SYMBOL(atmel_i2c_enqueue);

static inline size_t atmel_i2c_wake_token_sz(u32 bus_clk_rate)
{
	u32 no_of_bits = DIV_ROUND_UP(TWLO_USEC * bus_clk_rate, USEC_PER_SEC);

	/* return the size of the wake_token in bytes */
	return DIV_ROUND_UP(no_of_bits, 8);
}

static int device_sanity_check(struct i2c_client *client)
{
	struct atmel_i2c_cmd *cmd;
	int ret;

	cmd = kmalloc(sizeof(*cmd), GFP_KERNEL);
	if (!cmd)
		return -ENOMEM;

	atmel_i2c_init_read_cmd(cmd);

	ret = atmel_i2c_send_receive(client, cmd);
	if (ret)
		goto free_cmd;

	/*
	 * It is vital that the Configuration, Data and OTP zones be locked
	 * prior to release into the field of the system containing the device.
	 * Failure to lock these zones may permit modification of any secret
	 * keys and may lead to other security problems.
	 */
	if (cmd->data[LOCK_CONFIG_IDX] || cmd->data[LOCK_VALUE_IDX]) {
		dev_err(&client->dev, "Configuration or Data and OTP zones are unlocked!\n");
		ret = -ENOTSUPP;
	}

	/* fall through */
free_cmd:
	kfree(cmd);
	return ret;
}

int atmel_i2c_probe(struct i2c_client *client, const struct i2c_device_id *id)
{
	struct atmel_i2c_client_priv *i2c_priv;
	struct device *dev = &client->dev;
	int ret;
	u32 bus_clk_rate;

	if (!i2c_check_functionality(client->adapter, I2C_FUNC_I2C)) {
		dev_err(dev, "I2C_FUNC_I2C not supported\n");
		return -ENODEV;
	}

	bus_clk_rate = i2c_acpi_find_bus_speed(&client->adapter->dev);
	if (!bus_clk_rate) {
		ret = device_property_read_u32(&client->adapter->dev,
					       "clock-frequency", &bus_clk_rate);
		if (ret) {
			dev_err(dev, "failed to read clock-frequency property\n");
			return ret;
		}
	}

	if (bus_clk_rate > 1000000L) {
		dev_err(dev, "%d exceeds maximum supported clock frequency (1MHz)\n",
			bus_clk_rate);
		return -EINVAL;
	}

	i2c_priv = devm_kmalloc(dev, sizeof(*i2c_priv), GFP_KERNEL);
	if (!i2c_priv)
		return -ENOMEM;

	i2c_priv->client = client;
	mutex_init(&i2c_priv->lock);

	/*
	 * WAKE_TOKEN_MAX_SIZE was calculated for the maximum bus_clk_rate -
	 * 1MHz. The previous bus_clk_rate check ensures us that wake_token_sz
	 * will always be smaller than or equal to WAKE_TOKEN_MAX_SIZE.
	 */
	i2c_priv->wake_token_sz = atmel_i2c_wake_token_sz(bus_clk_rate);

	memset(i2c_priv->wake_token, 0, sizeof(i2c_priv->wake_token));

	atomic_set(&i2c_priv->tfm_count, 0);

	i2c_set_clientdata(client, i2c_priv);

	ret = device_sanity_check(client);
	if (ret)
		return ret;

	return 0;
}
EXPORT_SYMBOL(atmel_i2c_probe);

MODULE_AUTHOR("Tudor Ambarus <tudor.ambarus@microchip.com>");
MODULE_DESCRIPTION("Microchip / Atmel ECC (I2C) driver");
MODULE_LICENSE("GPL v2");
