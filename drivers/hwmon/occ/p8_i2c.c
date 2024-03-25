// SPDX-License-Identifier: GPL-2.0+
// Copyright IBM Corp 2019

#include <linux/device.h>
#include <linux/errno.h>
#include <linux/fsi-occ.h>
#include <linux/i2c.h>
#include <linux/jiffies.h>
#include <linux/module.h>
#include <linux/sched.h>
#include <asm/unaligned.h>

#include "common.h"

#define OCC_TIMEOUT_MS			1000
#define OCC_CMD_IN_PRG_WAIT_MS		50

/* OCB (on-chip control bridge - interface to OCC) registers */
#define OCB_DATA1			0x6B035
#define OCB_ADDR			0x6B070
#define OCB_DATA3			0x6B075

/* OCC SRAM address space */
#define OCC_SRAM_ADDR_CMD		0xFFFF6000
#define OCC_SRAM_ADDR_RESP		0xFFFF7000

#define OCC_DATA_ATTN			0x20010000

struct p8_i2c_occ {
	struct occ occ;
	struct i2c_client *client;
};

#define to_p8_i2c_occ(x)	container_of((x), struct p8_i2c_occ, occ)

static int p8_i2c_occ_getscom(struct i2c_client *client, u32 address, u8 *data)
{
	ssize_t rc;
	__be64 buf;
	struct i2c_msg msgs[2];

	/* p8 i2c slave requires shift */
	address <<= 1;

	msgs[0].addr = client->addr;
	msgs[0].flags = client->flags & I2C_M_TEN;
	msgs[0].len = sizeof(u32);
	/* address is a scom address; bus-endian */
	msgs[0].buf = (char *)&address;

	/* data from OCC is big-endian */
	msgs[1].addr = client->addr;
	msgs[1].flags = (client->flags & I2C_M_TEN) | I2C_M_RD;
	msgs[1].len = sizeof(u64);
	msgs[1].buf = (char *)&buf;

	rc = i2c_transfer(client->adapter, msgs, 2);
	if (rc < 0)
		return rc;

	*(u64 *)data = be64_to_cpu(buf);

	return 0;
}

static int p8_i2c_occ_putscom(struct i2c_client *client, u32 address, u8 *data)
{
	u32 buf[3];
	ssize_t rc;

	/* p8 i2c slave requires shift */
	address <<= 1;

	/* address is bus-endian; data passed through from user as-is */
	buf[0] = address;
	memcpy(&buf[1], &data[4], sizeof(u32));
	memcpy(&buf[2], data, sizeof(u32));

	rc = i2c_master_send(client, (const char *)buf, sizeof(buf));
	if (rc < 0)
		return rc;
	else if (rc != sizeof(buf))
		return -EIO;

	return 0;
}

static int p8_i2c_occ_putscom_u32(struct i2c_client *client, u32 address,
				  u32 data0, u32 data1)
{
	u8 buf[8];

	memcpy(buf, &data0, 4);
	memcpy(buf + 4, &data1, 4);

	return p8_i2c_occ_putscom(client, address, buf);
}

static int p8_i2c_occ_putscom_be(struct i2c_client *client, u32 address,
				 u8 *data, size_t len)
{
	__be32 data0 = 0, data1 = 0;

	memcpy(&data0, data, min_t(size_t, len, 4));
	if (len > 4) {
		len -= 4;
		memcpy(&data1, data + 4, min_t(size_t, len, 4));
	}

	return p8_i2c_occ_putscom_u32(client, address, be32_to_cpu(data0),
				      be32_to_cpu(data1));
}

static int p8_i2c_occ_send_cmd(struct occ *occ, u8 *cmd, size_t len,
			       void *resp, size_t resp_len)
{
	int i, rc;
	unsigned long start;
	u16 data_length;
	const unsigned long timeout = msecs_to_jiffies(OCC_TIMEOUT_MS);
	const long wait_time = msecs_to_jiffies(OCC_CMD_IN_PRG_WAIT_MS);
	struct p8_i2c_occ *ctx = to_p8_i2c_occ(occ);
	struct i2c_client *client = ctx->client;
	struct occ_response *or = (struct occ_response *)resp;

	start = jiffies;

	/* set sram address for command */
	rc = p8_i2c_occ_putscom_u32(client, OCB_ADDR, OCC_SRAM_ADDR_CMD, 0);
	if (rc)
		return rc;

	/* write command (expected to already be BE), we need bus-endian... */
	rc = p8_i2c_occ_putscom_be(client, OCB_DATA3, cmd, len);
	if (rc)
		return rc;

	/* trigger OCC attention */
	rc = p8_i2c_occ_putscom_u32(client, OCB_DATA1, OCC_DATA_ATTN, 0);
	if (rc)
		return rc;

	do {
		/* set sram address for response */
		rc = p8_i2c_occ_putscom_u32(client, OCB_ADDR,
					    OCC_SRAM_ADDR_RESP, 0);
		if (rc)
			return rc;

		rc = p8_i2c_occ_getscom(client, OCB_DATA3, (u8 *)resp);
		if (rc)
			return rc;

		/* wait for OCC */
		if (or->return_status == OCC_RESP_CMD_IN_PRG) {
			rc = -EALREADY;

			if (time_after(jiffies, start + timeout))
				break;

			set_current_state(TASK_INTERRUPTIBLE);
			schedule_timeout(wait_time);
		}
	} while (rc);

	/* check the OCC response */
	switch (or->return_status) {
	case OCC_RESP_CMD_IN_PRG:
		rc = -ETIMEDOUT;
		break;
	case OCC_RESP_SUCCESS:
		rc = 0;
		break;
	case OCC_RESP_CMD_INVAL:
	case OCC_RESP_CMD_LEN_INVAL:
	case OCC_RESP_DATA_INVAL:
	case OCC_RESP_CHKSUM_ERR:
		rc = -EINVAL;
		break;
	case OCC_RESP_INT_ERR:
	case OCC_RESP_BAD_STATE:
	case OCC_RESP_CRIT_EXCEPT:
	case OCC_RESP_CRIT_INIT:
	case OCC_RESP_CRIT_WATCHDOG:
	case OCC_RESP_CRIT_OCB:
	case OCC_RESP_CRIT_HW:
		rc = -EREMOTEIO;
		break;
	default:
		rc = -EPROTO;
	}

	if (rc < 0)
		return rc;

	data_length = get_unaligned_be16(&or->data_length);
	if ((data_length + 7) > resp_len)
		return -EMSGSIZE;

	/* fetch the rest of the response data */
	for (i = 8; i < data_length + 7; i += 8) {
		rc = p8_i2c_occ_getscom(client, OCB_DATA3, ((u8 *)resp) + i);
		if (rc)
			return rc;
	}

	return 0;
}

static int p8_i2c_occ_probe(struct i2c_client *client)
{
	struct occ *occ;
	struct p8_i2c_occ *ctx = devm_kzalloc(&client->dev, sizeof(*ctx),
					      GFP_KERNEL);
	if (!ctx)
		return -ENOMEM;

	ctx->client = client;
	occ = &ctx->occ;
	occ->bus_dev = &client->dev;
	dev_set_drvdata(&client->dev, occ);

	occ->powr_sample_time_us = 250;
	occ->poll_cmd_data = 0x10;		/* P8 OCC poll data */
	occ->send_cmd = p8_i2c_occ_send_cmd;

	return occ_setup(occ);
}

static void p8_i2c_occ_remove(struct i2c_client *client)
{
	struct occ *occ = dev_get_drvdata(&client->dev);

	occ_shutdown(occ);
}

static const struct of_device_id p8_i2c_occ_of_match[] = {
	{ .compatible = "ibm,p8-occ-hwmon" },
	{}
};
MODULE_DEVICE_TABLE(of, p8_i2c_occ_of_match);

static struct i2c_driver p8_i2c_occ_driver = {
	.driver = {
		.name = "occ-hwmon",
		.of_match_table = p8_i2c_occ_of_match,
	},
	.probe = p8_i2c_occ_probe,
	.remove = p8_i2c_occ_remove,
};

module_i2c_driver(p8_i2c_occ_driver);

MODULE_AUTHOR("Eddie James <eajames@linux.ibm.com>");
MODULE_DESCRIPTION("BMC P8 OCC hwmon driver");
MODULE_LICENSE("GPL");
