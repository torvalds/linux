// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * IBM OPAL I2C driver
 * Copyright (C) 2014 IBM
 */

#include <linux/device.h>
#include <linux/i2c.h>
#include <linux/kernel.h>
#include <linux/mm.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/slab.h>

#include <asm/firmware.h>
#include <asm/opal.h>

static int i2c_opal_translate_error(int rc)
{
	switch (rc) {
	case OPAL_NO_MEM:
		return -ENOMEM;
	case OPAL_PARAMETER:
		return -EINVAL;
	case OPAL_I2C_ARBT_LOST:
		return -EAGAIN;
	case OPAL_I2C_TIMEOUT:
		return -ETIMEDOUT;
	case OPAL_I2C_NACK_RCVD:
		return -ENXIO;
	case OPAL_I2C_STOP_ERR:
		return -EBUSY;
	default:
		return -EIO;
	}
}

static int i2c_opal_send_request(u32 bus_id, struct opal_i2c_request *req)
{
	struct opal_msg msg;
	int token, rc;

	token = opal_async_get_token_interruptible();
	if (token < 0) {
		if (token != -ERESTARTSYS)
			pr_err("Failed to get the async token\n");

		return token;
	}

	rc = opal_i2c_request(token, bus_id, req);
	if (rc != OPAL_ASYNC_COMPLETION) {
		rc = i2c_opal_translate_error(rc);
		goto exit;
	}

	rc = opal_async_wait_response(token, &msg);
	if (rc)
		goto exit;

	rc = opal_get_async_rc(msg);
	if (rc != OPAL_SUCCESS) {
		rc = i2c_opal_translate_error(rc);
		goto exit;
	}

exit:
	opal_async_release_token(token);
	return rc;
}

static int i2c_opal_master_xfer(struct i2c_adapter *adap, struct i2c_msg *msgs,
				int num)
{
	unsigned long opal_id = (unsigned long)adap->algo_data;
	struct opal_i2c_request req;
	int rc, i;

	/* We only support fairly simple combinations here of one
	 * or two messages
	 */
	memset(&req, 0, sizeof(req));
	switch(num) {
	case 1:
		req.type = (msgs[0].flags & I2C_M_RD) ?
			OPAL_I2C_RAW_READ : OPAL_I2C_RAW_WRITE;
		req.addr = cpu_to_be16(msgs[0].addr);
		req.size = cpu_to_be32(msgs[0].len);
		req.buffer_ra = cpu_to_be64(__pa(msgs[0].buf));
		break;
	case 2:
		req.type = (msgs[1].flags & I2C_M_RD) ?
			OPAL_I2C_SM_READ : OPAL_I2C_SM_WRITE;
		req.addr = cpu_to_be16(msgs[0].addr);
		req.subaddr_sz = msgs[0].len;
		for (i = 0; i < msgs[0].len; i++)
			req.subaddr = (req.subaddr << 8) | msgs[0].buf[i];
		req.subaddr = cpu_to_be32(req.subaddr);
		req.size = cpu_to_be32(msgs[1].len);
		req.buffer_ra = cpu_to_be64(__pa(msgs[1].buf));
		break;
	}

	rc = i2c_opal_send_request(opal_id, &req);
	if (rc)
		return rc;

	return num;
}

static int i2c_opal_smbus_xfer(struct i2c_adapter *adap, u16 addr,
			       unsigned short flags, char read_write,
			       u8 command, int size, union i2c_smbus_data *data)
{
	unsigned long opal_id = (unsigned long)adap->algo_data;
	struct opal_i2c_request req;
	u8 local[2];
	int rc;

	memset(&req, 0, sizeof(req));

	req.addr = cpu_to_be16(addr);
	switch (size) {
	case I2C_SMBUS_BYTE:
		req.buffer_ra = cpu_to_be64(__pa(&data->byte));
		req.size = cpu_to_be32(1);
		fallthrough;
	case I2C_SMBUS_QUICK:
		req.type = (read_write == I2C_SMBUS_READ) ?
			OPAL_I2C_RAW_READ : OPAL_I2C_RAW_WRITE;
		break;
	case I2C_SMBUS_BYTE_DATA:
		req.buffer_ra = cpu_to_be64(__pa(&data->byte));
		req.size = cpu_to_be32(1);
		req.subaddr = cpu_to_be32(command);
		req.subaddr_sz = 1;
		req.type = (read_write == I2C_SMBUS_READ) ?
			OPAL_I2C_SM_READ : OPAL_I2C_SM_WRITE;
		break;
	case I2C_SMBUS_WORD_DATA:
		if (!read_write) {
			local[0] = data->word & 0xff;
			local[1] = (data->word >> 8) & 0xff;
		}
		req.buffer_ra = cpu_to_be64(__pa(local));
		req.size = cpu_to_be32(2);
		req.subaddr = cpu_to_be32(command);
		req.subaddr_sz = 1;
		req.type = (read_write == I2C_SMBUS_READ) ?
			OPAL_I2C_SM_READ : OPAL_I2C_SM_WRITE;
		break;
	case I2C_SMBUS_I2C_BLOCK_DATA:
		req.buffer_ra = cpu_to_be64(__pa(&data->block[1]));
		req.size = cpu_to_be32(data->block[0]);
		req.subaddr = cpu_to_be32(command);
		req.subaddr_sz = 1;
		req.type = (read_write == I2C_SMBUS_READ) ?
			OPAL_I2C_SM_READ : OPAL_I2C_SM_WRITE;
		break;
	default:
		return -EINVAL;
	}

	rc = i2c_opal_send_request(opal_id, &req);
	if (!rc && read_write && size == I2C_SMBUS_WORD_DATA) {
		data->word = ((u16)local[1]) << 8;
		data->word |= local[0];
	}

	return rc;
}

static u32 i2c_opal_func(struct i2c_adapter *adapter)
{
	return I2C_FUNC_I2C | I2C_FUNC_SMBUS_QUICK | I2C_FUNC_SMBUS_BYTE |
	       I2C_FUNC_SMBUS_BYTE_DATA | I2C_FUNC_SMBUS_WORD_DATA |
	       I2C_FUNC_SMBUS_I2C_BLOCK;
}

static const struct i2c_algorithm i2c_opal_algo = {
	.master_xfer	= i2c_opal_master_xfer,
	.smbus_xfer	= i2c_opal_smbus_xfer,
	.functionality	= i2c_opal_func,
};

/*
 * For two messages, we basically support simple smbus transactions of a
 * write-then-anything.
 */
static const struct i2c_adapter_quirks i2c_opal_quirks = {
	.flags = I2C_AQ_COMB | I2C_AQ_COMB_WRITE_FIRST | I2C_AQ_COMB_SAME_ADDR,
	.max_comb_1st_msg_len = 4,
};

static int i2c_opal_probe(struct platform_device *pdev)
{
	struct i2c_adapter	*adapter;
	const char		*pname;
	u32			opal_id;
	int			rc;

	if (!pdev->dev.of_node)
		return -ENODEV;

	rc = of_property_read_u32(pdev->dev.of_node, "ibm,opal-id", &opal_id);
	if (rc) {
		dev_err(&pdev->dev, "Missing ibm,opal-id property !\n");
		return -EIO;
	}

	adapter = devm_kzalloc(&pdev->dev, sizeof(*adapter), GFP_KERNEL);
	if (!adapter)
		return -ENOMEM;

	adapter->algo = &i2c_opal_algo;
	adapter->algo_data = (void *)(unsigned long)opal_id;
	adapter->quirks = &i2c_opal_quirks;
	adapter->dev.parent = &pdev->dev;
	adapter->dev.of_node = of_node_get(pdev->dev.of_node);
	pname = of_get_property(pdev->dev.of_node, "ibm,port-name", NULL);
	if (pname)
		strscpy(adapter->name, pname, sizeof(adapter->name));
	else
		strscpy(adapter->name, "opal", sizeof(adapter->name));

	platform_set_drvdata(pdev, adapter);
	rc = i2c_add_adapter(adapter);
	if (rc)
		dev_err(&pdev->dev, "Failed to register the i2c adapter\n");

	return rc;
}

static void i2c_opal_remove(struct platform_device *pdev)
{
	struct i2c_adapter *adapter = platform_get_drvdata(pdev);

	i2c_del_adapter(adapter);
}

static const struct of_device_id i2c_opal_of_match[] = {
	{
		.compatible = "ibm,opal-i2c",
	},
	{ }
};
MODULE_DEVICE_TABLE(of, i2c_opal_of_match);

static struct platform_driver i2c_opal_driver = {
	.probe	= i2c_opal_probe,
	.remove_new = i2c_opal_remove,
	.driver	= {
		.name		= "i2c-opal",
		.of_match_table	= i2c_opal_of_match,
	},
};

static int __init i2c_opal_init(void)
{
	if (!firmware_has_feature(FW_FEATURE_OPAL))
		return -ENODEV;

	return platform_driver_register(&i2c_opal_driver);
}
module_init(i2c_opal_init);

static void __exit i2c_opal_exit(void)
{
	return platform_driver_unregister(&i2c_opal_driver);
}
module_exit(i2c_opal_exit);

MODULE_AUTHOR("Neelesh Gupta <neelegup@linux.vnet.ibm.com>");
MODULE_DESCRIPTION("IBM OPAL I2C driver");
MODULE_LICENSE("GPL");
