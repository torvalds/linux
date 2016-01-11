/*
 * cros_ec_lpc - LPC access to the Chrome OS Embedded Controller
 *
 * Copyright (C) 2012-2015 Google, Inc
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * This driver uses the Chrome OS EC byte-level message-based protocol for
 * communicating the keyboard state (which keys are pressed) from a keyboard EC
 * to the AP over some bus (such as i2c, lpc, spi).  The EC does debouncing,
 * but everything else (including deghosting) is done here.  The main
 * motivation for this is to keep the EC firmware as simple as possible, since
 * it cannot be easily upgraded and EC flash/IRAM space is relatively
 * expensive.
 */

#include <linux/dmi.h>
#include <linux/delay.h>
#include <linux/io.h>
#include <linux/mfd/cros_ec.h>
#include <linux/mfd/cros_ec_commands.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/printk.h>

#define DRV_NAME "cros_ec_lpc"

static int ec_response_timed_out(void)
{
	unsigned long one_second = jiffies + HZ;

	usleep_range(200, 300);
	do {
		if (!(inb(EC_LPC_ADDR_HOST_CMD) & EC_LPC_STATUS_BUSY_MASK))
			return 0;
		usleep_range(100, 200);
	} while (time_before(jiffies, one_second));

	return 1;
}

static int cros_ec_pkt_xfer_lpc(struct cros_ec_device *ec,
				struct cros_ec_command *msg)
{
	struct ec_host_request *request;
	struct ec_host_response response;
	u8 sum = 0;
	int i;
	int ret = 0;
	u8 *dout;

	ret = cros_ec_prepare_tx(ec, msg);

	/* Write buffer */
	for (i = 0; i < ret; i++)
		outb(ec->dout[i], EC_LPC_ADDR_HOST_PACKET + i);

	request = (struct ec_host_request *)ec->dout;

	/* Here we go */
	outb(EC_COMMAND_PROTOCOL_3, EC_LPC_ADDR_HOST_CMD);

	if (ec_response_timed_out()) {
		dev_warn(ec->dev, "EC responsed timed out\n");
		ret = -EIO;
		goto done;
	}

	/* Check result */
	msg->result = inb(EC_LPC_ADDR_HOST_DATA);
	ret = cros_ec_check_result(ec, msg);
	if (ret)
		goto done;

	/* Read back response */
	dout = (u8 *)&response;
	for (i = 0; i < sizeof(response); i++) {
		dout[i] = inb(EC_LPC_ADDR_HOST_PACKET + i);
		sum += dout[i];
	}

	msg->result = response.result;

	if (response.data_len > msg->insize) {
		dev_err(ec->dev,
			"packet too long (%d bytes, expected %d)",
			response.data_len, msg->insize);
		ret = -EMSGSIZE;
		goto done;
	}

	/* Read response and process checksum */
	for (i = 0; i < response.data_len; i++) {
		msg->data[i] =
			inb(EC_LPC_ADDR_HOST_PACKET + sizeof(response) + i);
		sum += msg->data[i];
	}

	if (sum) {
		dev_err(ec->dev,
			"bad packet checksum %02x\n",
			response.checksum);
		ret = -EBADMSG;
		goto done;
	}

	/* Return actual amount of data received */
	ret = response.data_len;
done:
	return ret;
}

static int cros_ec_cmd_xfer_lpc(struct cros_ec_device *ec,
				struct cros_ec_command *msg)
{
	struct ec_lpc_host_args args;
	int csum;
	int i;
	int ret = 0;

	if (msg->outsize > EC_PROTO2_MAX_PARAM_SIZE ||
	    msg->insize > EC_PROTO2_MAX_PARAM_SIZE) {
		dev_err(ec->dev,
			"invalid buffer sizes (out %d, in %d)\n",
			msg->outsize, msg->insize);
		return -EINVAL;
	}

	/* Now actually send the command to the EC and get the result */
	args.flags = EC_HOST_ARGS_FLAG_FROM_HOST;
	args.command_version = msg->version;
	args.data_size = msg->outsize;

	/* Initialize checksum */
	csum = msg->command + args.flags +
		args.command_version + args.data_size;

	/* Copy data and update checksum */
	for (i = 0; i < msg->outsize; i++) {
		outb(msg->data[i], EC_LPC_ADDR_HOST_PARAM + i);
		csum += msg->data[i];
	}

	/* Finalize checksum and write args */
	args.checksum = csum & 0xFF;
	outb(args.flags, EC_LPC_ADDR_HOST_ARGS);
	outb(args.command_version, EC_LPC_ADDR_HOST_ARGS + 1);
	outb(args.data_size, EC_LPC_ADDR_HOST_ARGS + 2);
	outb(args.checksum, EC_LPC_ADDR_HOST_ARGS + 3);

	/* Here we go */
	outb(msg->command, EC_LPC_ADDR_HOST_CMD);

	if (ec_response_timed_out()) {
		dev_warn(ec->dev, "EC responsed timed out\n");
		ret = -EIO;
		goto done;
	}

	/* Check result */
	msg->result = inb(EC_LPC_ADDR_HOST_DATA);
	ret = cros_ec_check_result(ec, msg);
	if (ret)
		goto done;

	/* Read back args */
	args.flags = inb(EC_LPC_ADDR_HOST_ARGS);
	args.command_version = inb(EC_LPC_ADDR_HOST_ARGS + 1);
	args.data_size = inb(EC_LPC_ADDR_HOST_ARGS + 2);
	args.checksum = inb(EC_LPC_ADDR_HOST_ARGS + 3);

	if (args.data_size > msg->insize) {
		dev_err(ec->dev,
			"packet too long (%d bytes, expected %d)",
			args.data_size, msg->insize);
		ret = -ENOSPC;
		goto done;
	}

	/* Start calculating response checksum */
	csum = msg->command + args.flags +
		args.command_version + args.data_size;

	/* Read response and update checksum */
	for (i = 0; i < args.data_size; i++) {
		msg->data[i] = inb(EC_LPC_ADDR_HOST_PARAM + i);
		csum += msg->data[i];
	}

	/* Verify checksum */
	if (args.checksum != (csum & 0xFF)) {
		dev_err(ec->dev,
			"bad packet checksum, expected %02x, got %02x\n",
			args.checksum, csum & 0xFF);
		ret = -EBADMSG;
		goto done;
	}

	/* Return actual amount of data received */
	ret = args.data_size;
done:
	return ret;
}

/* Returns num bytes read, or negative on error. Doesn't need locking. */
static int cros_ec_lpc_readmem(struct cros_ec_device *ec, unsigned int offset,
			       unsigned int bytes, void *dest)
{
	int i = offset;
	char *s = dest;
	int cnt = 0;

	if (offset >= EC_MEMMAP_SIZE - bytes)
		return -EINVAL;

	/* fixed length */
	if (bytes) {
		for (; cnt < bytes; i++, s++, cnt++)
			*s = inb(EC_LPC_ADDR_MEMMAP + i);
		return cnt;
	}

	/* string */
	for (; i < EC_MEMMAP_SIZE; i++, s++) {
		*s = inb(EC_LPC_ADDR_MEMMAP + i);
		cnt++;
		if (!*s)
			break;
	}

	return cnt;
}

static int cros_ec_lpc_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct cros_ec_device *ec_dev;
	int ret;

	if (!devm_request_region(dev, EC_LPC_ADDR_MEMMAP, EC_MEMMAP_SIZE,
				 dev_name(dev))) {
		dev_err(dev, "couldn't reserve memmap region\n");
		return -EBUSY;
	}

	if ((inb(EC_LPC_ADDR_MEMMAP + EC_MEMMAP_ID) != 'E') ||
	    (inb(EC_LPC_ADDR_MEMMAP + EC_MEMMAP_ID + 1) != 'C')) {
		dev_err(dev, "EC ID not detected\n");
		return -ENODEV;
	}

	if (!devm_request_region(dev, EC_HOST_CMD_REGION0,
				 EC_HOST_CMD_REGION_SIZE, dev_name(dev))) {
		dev_err(dev, "couldn't reserve region0\n");
		return -EBUSY;
	}
	if (!devm_request_region(dev, EC_HOST_CMD_REGION1,
				 EC_HOST_CMD_REGION_SIZE, dev_name(dev))) {
		dev_err(dev, "couldn't reserve region1\n");
		return -EBUSY;
	}

	ec_dev = devm_kzalloc(dev, sizeof(*ec_dev), GFP_KERNEL);
	if (!ec_dev)
		return -ENOMEM;

	platform_set_drvdata(pdev, ec_dev);
	ec_dev->dev = dev;
	ec_dev->phys_name = dev_name(dev);
	ec_dev->cmd_xfer = cros_ec_cmd_xfer_lpc;
	ec_dev->pkt_xfer = cros_ec_pkt_xfer_lpc;
	ec_dev->cmd_readmem = cros_ec_lpc_readmem;
	ec_dev->din_size = sizeof(struct ec_host_response) +
			   sizeof(struct ec_response_get_protocol_info);
	ec_dev->dout_size = sizeof(struct ec_host_request);

	ret = cros_ec_register(ec_dev);
	if (ret) {
		dev_err(dev, "couldn't register ec_dev (%d)\n", ret);
		return ret;
	}

	return 0;
}

static int cros_ec_lpc_remove(struct platform_device *pdev)
{
	struct cros_ec_device *ec_dev;

	ec_dev = platform_get_drvdata(pdev);
	cros_ec_remove(ec_dev);

	return 0;
}

static struct dmi_system_id cros_ec_lpc_dmi_table[] __initdata = {
	{
		/*
		 * Today all Chromebooks/boxes ship with Google_* as version and
		 * coreboot as bios vendor. No other systems with this
		 * combination are known to date.
		 */
		.matches = {
			DMI_MATCH(DMI_BIOS_VENDOR, "coreboot"),
			DMI_MATCH(DMI_BIOS_VERSION, "Google_"),
		},
	},
	{
		/* x86-link, the Chromebook Pixel. */
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "GOOGLE"),
			DMI_MATCH(DMI_PRODUCT_NAME, "Link"),
		},
	},
	{
		/* x86-samus, the Chromebook Pixel 2. */
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "GOOGLE"),
			DMI_MATCH(DMI_PRODUCT_NAME, "Samus"),
		},
	},
	{
		/* x86-peppy, the Acer C720 Chromebook. */
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "Acer"),
			DMI_MATCH(DMI_PRODUCT_NAME, "Peppy"),
		},
	},
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(dmi, cros_ec_lpc_dmi_table);

static struct platform_driver cros_ec_lpc_driver = {
	.driver = {
		.name = DRV_NAME,
	},
	.probe = cros_ec_lpc_probe,
	.remove = cros_ec_lpc_remove,
};

static struct platform_device cros_ec_lpc_device = {
	.name = DRV_NAME
};

static int __init cros_ec_lpc_init(void)
{
	int ret;

	if (!dmi_check_system(cros_ec_lpc_dmi_table)) {
		pr_err(DRV_NAME ": unsupported system.\n");
		return -ENODEV;
	}

	/* Register the driver */
	ret = platform_driver_register(&cros_ec_lpc_driver);
	if (ret) {
		pr_err(DRV_NAME ": can't register driver: %d\n", ret);
		return ret;
	}

	/* Register the device, and it'll get hooked up automatically */
	ret = platform_device_register(&cros_ec_lpc_device);
	if (ret) {
		pr_err(DRV_NAME ": can't register device: %d\n", ret);
		platform_driver_unregister(&cros_ec_lpc_driver);
		return ret;
	}

	return 0;
}

static void __exit cros_ec_lpc_exit(void)
{
	platform_device_unregister(&cros_ec_lpc_device);
	platform_driver_unregister(&cros_ec_lpc_driver);
}

module_init(cros_ec_lpc_init);
module_exit(cros_ec_lpc_exit);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("ChromeOS EC LPC driver");
