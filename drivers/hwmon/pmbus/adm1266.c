// SPDX-License-Identifier: GPL-2.0
/*
 * ADM1266 - Cascadable Super Sequencer with Margin
 * Control and Fault Recording
 *
 * Copyright 2020 Analog Devices Inc.
 */

#include <linux/crc8.h>
#include <linux/i2c.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include "pmbus.h"
#include <linux/slab.h>

#define ADM1266_PMBUS_BLOCK_MAX		255

struct adm1266_data {
	struct pmbus_driver_info info;
	struct i2c_client *client;
	struct mutex buf_mutex;
	u8 write_buf[ADM1266_PMBUS_BLOCK_MAX + 1] ____cacheline_aligned;
	u8 read_buf[ADM1266_PMBUS_BLOCK_MAX + 1] ____cacheline_aligned;
};

DECLARE_CRC8_TABLE(pmbus_crc_table);

/*
 * Different from Block Read as it sends data and waits for the slave to
 * return a value dependent on that data. The protocol is simply a Write Block
 * followed by a Read Block without the Read-Block command field and the
 * Write-Block STOP bit.
 */
static int adm1266_pmbus_block_xfer(struct adm1266_data *data, u8 cmd, u8 w_len, u8 *data_w,
				    u8 *data_r)
{
	struct i2c_client *client = data->client;
	struct i2c_msg msgs[2] = {
		{
			.addr = client->addr,
			.flags = I2C_M_DMA_SAFE,
			.buf = data->write_buf,
			.len = w_len + 2,
		},
		{
			.addr = client->addr,
			.flags = I2C_M_RD | I2C_M_DMA_SAFE,
			.buf = data->read_buf,
			.len = ADM1266_PMBUS_BLOCK_MAX + 2,
		}
	};
	u8 addr;
	u8 crc;
	int ret;

	mutex_lock(&data->buf_mutex);

	msgs[0].buf[0] = cmd;
	msgs[0].buf[1] = w_len;
	memcpy(&msgs[0].buf[2], data_w, w_len);

	ret = i2c_transfer(client->adapter, msgs, 2);
	if (ret != 2) {
		if (ret >= 0)
			ret = -EPROTO;

		mutex_unlock(&data->buf_mutex);

		return ret;
	}

	if (client->flags & I2C_CLIENT_PEC) {
		addr = i2c_8bit_addr_from_msg(&msgs[0]);
		crc = crc8(pmbus_crc_table, &addr, 1, 0);
		crc = crc8(pmbus_crc_table, msgs[0].buf,  msgs[0].len, crc);

		addr = i2c_8bit_addr_from_msg(&msgs[1]);
		crc = crc8(pmbus_crc_table, &addr, 1, crc);
		crc = crc8(pmbus_crc_table, msgs[1].buf,  msgs[1].buf[0] + 1, crc);

		if (crc != msgs[1].buf[msgs[1].buf[0] + 1]) {
			mutex_unlock(&data->buf_mutex);
			return -EBADMSG;
		}
	}

	memcpy(data_r, &msgs[1].buf[1], msgs[1].buf[0]);

	ret = msgs[1].buf[0];
	mutex_unlock(&data->buf_mutex);

	return ret;
}

static int adm1266_probe(struct i2c_client *client)
{
	struct adm1266_data *data;
	int i;

	data = devm_kzalloc(&client->dev, sizeof(struct adm1266_data), GFP_KERNEL);
	if (!data)
		return -ENOMEM;

	data->client = client;
	data->info.pages = 17;
	data->info.format[PSC_VOLTAGE_OUT] = linear;
	for (i = 0; i < data->info.pages; i++)
		data->info.func[i] = PMBUS_HAVE_VOUT | PMBUS_HAVE_STATUS_VOUT;

	crc8_populate_msb(pmbus_crc_table, 0x7);
	mutex_init(&data->buf_mutex);

	return pmbus_do_probe(client, &data->info);
}

static const struct of_device_id adm1266_of_match[] = {
	{ .compatible = "adi,adm1266" },
	{ }
};
MODULE_DEVICE_TABLE(of, adm1266_of_match);

static const struct i2c_device_id adm1266_id[] = {
	{ "adm1266", 0 },
	{ }
};
MODULE_DEVICE_TABLE(i2c, adm1266_id);

static struct i2c_driver adm1266_driver = {
	.driver = {
		   .name = "adm1266",
		   .of_match_table = adm1266_of_match,
		  },
	.probe_new = adm1266_probe,
	.remove = pmbus_do_remove,
	.id_table = adm1266_id,
};

module_i2c_driver(adm1266_driver);

MODULE_AUTHOR("Alexandru Tachici <alexandru.tachici@analog.com>");
MODULE_DESCRIPTION("PMBus driver for Analog Devices ADM1266");
MODULE_LICENSE("GPL v2");
