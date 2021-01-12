/*
 * drivers/i2c/busses/i2c-tegra-bpmp.c
 *
 * Copyright (c) 2016 NVIDIA Corporation.  All rights reserved.
 *
 * Author: Shardar Shariff Md <smohammed@nvidia.com>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <linux/err.h>
#include <linux/i2c.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/of_device.h>
#include <linux/platform_device.h>
#include <linux/pm_runtime.h>

#include <soc/tegra/bpmp-abi.h>
#include <soc/tegra/bpmp.h>

/*
 * Serialized I2C message header size is 6 bytes and includes address, flags
 * and length
 */
#define SERIALI2C_HDR_SIZE 6

struct tegra_bpmp_i2c {
	struct i2c_adapter adapter;
	struct device *dev;

	struct tegra_bpmp *bpmp;
	unsigned int bus;
};

/*
 * Linux flags are translated to BPMP defined I2C flags that are used in BPMP
 * firmware I2C driver to avoid any issues in future if Linux I2C flags are
 * changed.
 */
static int tegra_bpmp_xlate_flags(u16 flags, u16 *out)
{
	if (flags & I2C_M_TEN) {
		*out |= SERIALI2C_TEN;
		flags &= ~I2C_M_TEN;
	}

	if (flags & I2C_M_RD) {
		*out |= SERIALI2C_RD;
		flags &= ~I2C_M_RD;
	}

	if (flags & I2C_M_STOP) {
		*out |= SERIALI2C_STOP;
		flags &= ~I2C_M_STOP;
	}

	if (flags & I2C_M_NOSTART) {
		*out |= SERIALI2C_NOSTART;
		flags &= ~I2C_M_NOSTART;
	}

	if (flags & I2C_M_REV_DIR_ADDR) {
		*out |= SERIALI2C_REV_DIR_ADDR;
		flags &= ~I2C_M_REV_DIR_ADDR;
	}

	if (flags & I2C_M_IGNORE_NAK) {
		*out |= SERIALI2C_IGNORE_NAK;
		flags &= ~I2C_M_IGNORE_NAK;
	}

	if (flags & I2C_M_NO_RD_ACK) {
		*out |= SERIALI2C_NO_RD_ACK;
		flags &= ~I2C_M_NO_RD_ACK;
	}

	if (flags & I2C_M_RECV_LEN) {
		*out |= SERIALI2C_RECV_LEN;
		flags &= ~I2C_M_RECV_LEN;
	}

	return 0;
}

/**
 * The serialized I2C format is simply the following:
 * [addr little-endian][flags little-endian][len little-endian][data if write]
 * [addr little-endian][flags little-endian][len little-endian][data if write]
 *  ...
 *
 * The flags are translated from Linux kernel representation to seriali2c
 * representation. Any undefined flag being set causes an error.
 *
 * The data is there only for writes. Reads have the data transferred in the
 * other direction, and thus data is not present.
 *
 * See deserialize_i2c documentation for the data format in the other direction.
 */
static int tegra_bpmp_serialize_i2c_msg(struct tegra_bpmp_i2c *i2c,
					struct mrq_i2c_request *request,
					struct i2c_msg *msgs,
					unsigned int num)
{
	char *buf = request->xfer.data_buf;
	unsigned int i, j, pos = 0;
	int err;

	for (i = 0; i < num; i++) {
		struct i2c_msg *msg = &msgs[i];
		u16 flags = 0;

		err = tegra_bpmp_xlate_flags(msg->flags, &flags);
		if (err < 0)
			return err;

		buf[pos++] = msg->addr & 0xff;
		buf[pos++] = (msg->addr & 0xff00) >> 8;
		buf[pos++] = flags & 0xff;
		buf[pos++] = (flags & 0xff00) >> 8;
		buf[pos++] = msg->len & 0xff;
		buf[pos++] = (msg->len & 0xff00) >> 8;

		if ((flags & SERIALI2C_RD) == 0) {
			for (j = 0; j < msg->len; j++)
				buf[pos++] = msg->buf[j];
		}
	}

	request->xfer.data_size = pos;

	return 0;
}

/**
 * The data in the BPMP -> CPU direction is composed of sequential blocks for
 * those messages that have I2C_M_RD. So, for example, if you have:
 *
 * - !I2C_M_RD, len == 5, data == a0 01 02 03 04
 * - !I2C_M_RD, len == 1, data == a0
 * - I2C_M_RD, len == 2, data == [uninitialized buffer 1]
 * - !I2C_M_RD, len == 1, data == a2
 * - I2C_M_RD, len == 2, data == [uninitialized buffer 2]
 *
 * ...then the data in the BPMP -> CPU direction would be 4 bytes total, and
 * would contain 2 bytes that will go to uninitialized buffer 1, and 2 bytes
 * that will go to uninitialized buffer 2.
 */
static int tegra_bpmp_i2c_deserialize(struct tegra_bpmp_i2c *i2c,
				      struct mrq_i2c_response *response,
				      struct i2c_msg *msgs,
				      unsigned int num)
{
	size_t size = response->xfer.data_size, len = 0, pos = 0;
	char *buf = response->xfer.data_buf;
	unsigned int i;

	for (i = 0; i < num; i++)
		if (msgs[i].flags & I2C_M_RD)
			len += msgs[i].len;

	if (len != size)
		return -EINVAL;

	for (i = 0; i < num; i++) {
		if (msgs[i].flags & I2C_M_RD) {
			memcpy(msgs[i].buf, buf + pos, msgs[i].len);
			pos += msgs[i].len;
		}
	}

	return 0;
}

static int tegra_bpmp_i2c_msg_len_check(struct i2c_msg *msgs, unsigned int num)
{
	size_t tx_len = 0, rx_len = 0;
	unsigned int i;

	for (i = 0; i < num; i++)
		if (!(msgs[i].flags & I2C_M_RD))
			tx_len += SERIALI2C_HDR_SIZE + msgs[i].len;

	if (tx_len > TEGRA_I2C_IPC_MAX_IN_BUF_SIZE)
		return -EINVAL;

	for (i = 0; i < num; i++)
		if ((msgs[i].flags & I2C_M_RD))
			rx_len += msgs[i].len;

	if (rx_len > TEGRA_I2C_IPC_MAX_OUT_BUF_SIZE)
		return -EINVAL;

	return 0;
}

static int tegra_bpmp_i2c_msg_xfer(struct tegra_bpmp_i2c *i2c,
				   struct mrq_i2c_request *request,
				   struct mrq_i2c_response *response)
{
	struct tegra_bpmp_message msg;
	int err;

	request->cmd = CMD_I2C_XFER;
	request->xfer.bus_id = i2c->bus;

	memset(&msg, 0, sizeof(msg));
	msg.mrq = MRQ_I2C;
	msg.tx.data = request;
	msg.tx.size = sizeof(*request);
	msg.rx.data = response;
	msg.rx.size = sizeof(*response);

	if (irqs_disabled())
		err = tegra_bpmp_transfer_atomic(i2c->bpmp, &msg);
	else
		err = tegra_bpmp_transfer(i2c->bpmp, &msg);

	return err;
}

static int tegra_bpmp_i2c_xfer(struct i2c_adapter *adapter,
			       struct i2c_msg *msgs, int num)
{
	struct tegra_bpmp_i2c *i2c = i2c_get_adapdata(adapter);
	struct mrq_i2c_response response;
	struct mrq_i2c_request request;
	int err;

	err = tegra_bpmp_i2c_msg_len_check(msgs, num);
	if (err < 0) {
		dev_err(i2c->dev, "unsupported message length\n");
		return err;
	}

	memset(&request, 0, sizeof(request));
	memset(&response, 0, sizeof(response));

	err = tegra_bpmp_serialize_i2c_msg(i2c, &request, msgs, num);
	if (err < 0) {
		dev_err(i2c->dev, "failed to serialize message: %d\n", err);
		return err;
	}

	err = tegra_bpmp_i2c_msg_xfer(i2c, &request, &response);
	if (err < 0) {
		dev_err(i2c->dev, "failed to transfer message: %d\n", err);
		return err;
	}

	err = tegra_bpmp_i2c_deserialize(i2c, &response, msgs, num);
	if (err < 0) {
		dev_err(i2c->dev, "failed to deserialize message: %d\n", err);
		return err;
	}

	return num;
}

static u32 tegra_bpmp_i2c_func(struct i2c_adapter *adapter)
{
	return I2C_FUNC_I2C | I2C_FUNC_SMBUS_EMUL | I2C_FUNC_10BIT_ADDR |
	       I2C_FUNC_PROTOCOL_MANGLING | I2C_FUNC_NOSTART;
}

static const struct i2c_algorithm tegra_bpmp_i2c_algo = {
	.master_xfer = tegra_bpmp_i2c_xfer,
	.functionality = tegra_bpmp_i2c_func,
};

static int tegra_bpmp_i2c_probe(struct platform_device *pdev)
{
	struct tegra_bpmp_i2c *i2c;
	u32 value;
	int err;

	i2c = devm_kzalloc(&pdev->dev, sizeof(*i2c), GFP_KERNEL);
	if (!i2c)
		return -ENOMEM;

	i2c->dev = &pdev->dev;

	i2c->bpmp = dev_get_drvdata(pdev->dev.parent);
	if (!i2c->bpmp)
		return -ENODEV;

	err = of_property_read_u32(pdev->dev.of_node, "nvidia,bpmp-bus-id",
				   &value);
	if (err < 0)
		return err;

	i2c->bus = value;

	i2c_set_adapdata(&i2c->adapter, i2c);
	i2c->adapter.owner = THIS_MODULE;
	strlcpy(i2c->adapter.name, "Tegra BPMP I2C adapter",
		sizeof(i2c->adapter.name));
	i2c->adapter.algo = &tegra_bpmp_i2c_algo;
	i2c->adapter.dev.parent = &pdev->dev;
	i2c->adapter.dev.of_node = pdev->dev.of_node;

	platform_set_drvdata(pdev, i2c);

	return i2c_add_adapter(&i2c->adapter);
}

static int tegra_bpmp_i2c_remove(struct platform_device *pdev)
{
	struct tegra_bpmp_i2c *i2c = platform_get_drvdata(pdev);

	i2c_del_adapter(&i2c->adapter);

	return 0;
}

static const struct of_device_id tegra_bpmp_i2c_of_match[] = {
	{ .compatible = "nvidia,tegra186-bpmp-i2c", },
	{ }
};
MODULE_DEVICE_TABLE(of, tegra_bpmp_i2c_of_match);

static struct platform_driver tegra_bpmp_i2c_driver = {
	.driver = {
		.name = "tegra-bpmp-i2c",
		.of_match_table = tegra_bpmp_i2c_of_match,
	},
	.probe = tegra_bpmp_i2c_probe,
	.remove = tegra_bpmp_i2c_remove,
};
module_platform_driver(tegra_bpmp_i2c_driver);

MODULE_DESCRIPTION("NVIDIA Tegra BPMP I2C bus controller driver");
MODULE_AUTHOR("Shardar Shariff Md <smohammed@nvidia.com>");
MODULE_AUTHOR("Juha-Matti Tilli");
MODULE_LICENSE("GPL v2");
