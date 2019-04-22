// SPDX-License-Identifier: GPL-2.0
// LPC interface for ChromeOS Embedded Controller
//
// Copyright (C) 2012-2015 Google, Inc
//
// This driver uses the ChromeOS EC byte-level message-based protocol for
// communicating the keyboard state (which keys are pressed) from a keyboard EC
// to the AP over some bus (such as i2c, lpc, spi).  The EC does debouncing,
// but everything else (including deghosting) is done here.  The main
// motivation for this is to keep the EC firmware as simple as possible, since
// it cannot be easily upgraded and EC flash/IRAM space is relatively
// expensive.

#include <linux/acpi.h>
#include <linux/dmi.h>
#include <linux/delay.h>
#include <linux/io.h>
#include <linux/interrupt.h>
#include <linux/mfd/cros_ec.h>
#include <linux/mfd/cros_ec_commands.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/printk.h>
#include <linux/suspend.h>

#include "cros_ec_lpc_reg.h"

#define DRV_NAME "cros_ec_lpcs"
#define ACPI_DRV_NAME "GOOG0004"

/* True if ACPI device is present */
static bool cros_ec_lpc_acpi_device_found;

static int ec_response_timed_out(void)
{
	unsigned long one_second = jiffies + HZ;
	u8 data;

	usleep_range(200, 300);
	do {
		if (!(cros_ec_lpc_read_bytes(EC_LPC_ADDR_HOST_CMD, 1, &data) &
		    EC_LPC_STATUS_BUSY_MASK))
			return 0;
		usleep_range(100, 200);
	} while (time_before(jiffies, one_second));

	return 1;
}

static int cros_ec_pkt_xfer_lpc(struct cros_ec_device *ec,
				struct cros_ec_command *msg)
{
	struct ec_host_response response;
	u8 sum;
	int ret = 0;
	u8 *dout;

	ret = cros_ec_prepare_tx(ec, msg);

	/* Write buffer */
	cros_ec_lpc_write_bytes(EC_LPC_ADDR_HOST_PACKET, ret, ec->dout);

	/* Here we go */
	sum = EC_COMMAND_PROTOCOL_3;
	cros_ec_lpc_write_bytes(EC_LPC_ADDR_HOST_CMD, 1, &sum);

	if (ec_response_timed_out()) {
		dev_warn(ec->dev, "EC responsed timed out\n");
		ret = -EIO;
		goto done;
	}

	/* Check result */
	msg->result = cros_ec_lpc_read_bytes(EC_LPC_ADDR_HOST_DATA, 1, &sum);
	ret = cros_ec_check_result(ec, msg);
	if (ret)
		goto done;

	/* Read back response */
	dout = (u8 *)&response;
	sum = cros_ec_lpc_read_bytes(EC_LPC_ADDR_HOST_PACKET, sizeof(response),
				     dout);

	msg->result = response.result;

	if (response.data_len > msg->insize) {
		dev_err(ec->dev,
			"packet too long (%d bytes, expected %d)",
			response.data_len, msg->insize);
		ret = -EMSGSIZE;
		goto done;
	}

	/* Read response and process checksum */
	sum += cros_ec_lpc_read_bytes(EC_LPC_ADDR_HOST_PACKET +
				      sizeof(response), response.data_len,
				      msg->data);

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
	u8 sum;
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
	sum = msg->command + args.flags + args.command_version + args.data_size;

	/* Copy data and update checksum */
	sum += cros_ec_lpc_write_bytes(EC_LPC_ADDR_HOST_PARAM, msg->outsize,
				       msg->data);

	/* Finalize checksum and write args */
	args.checksum = sum;
	cros_ec_lpc_write_bytes(EC_LPC_ADDR_HOST_ARGS, sizeof(args),
				(u8 *)&args);

	/* Here we go */
	sum = msg->command;
	cros_ec_lpc_write_bytes(EC_LPC_ADDR_HOST_CMD, 1, &sum);

	if (ec_response_timed_out()) {
		dev_warn(ec->dev, "EC responsed timed out\n");
		ret = -EIO;
		goto done;
	}

	/* Check result */
	msg->result = cros_ec_lpc_read_bytes(EC_LPC_ADDR_HOST_DATA, 1, &sum);
	ret = cros_ec_check_result(ec, msg);
	if (ret)
		goto done;

	/* Read back args */
	cros_ec_lpc_read_bytes(EC_LPC_ADDR_HOST_ARGS, sizeof(args),
			       (u8 *)&args);

	if (args.data_size > msg->insize) {
		dev_err(ec->dev,
			"packet too long (%d bytes, expected %d)",
			args.data_size, msg->insize);
		ret = -ENOSPC;
		goto done;
	}

	/* Start calculating response checksum */
	sum = msg->command + args.flags + args.command_version + args.data_size;

	/* Read response and update checksum */
	sum += cros_ec_lpc_read_bytes(EC_LPC_ADDR_HOST_PARAM, args.data_size,
				      msg->data);

	/* Verify checksum */
	if (args.checksum != sum) {
		dev_err(ec->dev,
			"bad packet checksum, expected %02x, got %02x\n",
			args.checksum, sum);
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
		cros_ec_lpc_read_bytes(EC_LPC_ADDR_MEMMAP + offset, bytes, s);
		return bytes;
	}

	/* string */
	for (; i < EC_MEMMAP_SIZE; i++, s++) {
		cros_ec_lpc_read_bytes(EC_LPC_ADDR_MEMMAP + i, 1, s);
		cnt++;
		if (!*s)
			break;
	}

	return cnt;
}

static void cros_ec_lpc_acpi_notify(acpi_handle device, u32 value, void *data)
{
	struct cros_ec_device *ec_dev = data;

	if (ec_dev->mkbp_event_supported &&
	    cros_ec_get_next_event(ec_dev, NULL) > 0)
		blocking_notifier_call_chain(&ec_dev->event_notifier, 0,
					     ec_dev);

	if (value == ACPI_NOTIFY_DEVICE_WAKE)
		pm_system_wakeup();
}

static int cros_ec_lpc_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct acpi_device *adev;
	acpi_status status;
	struct cros_ec_device *ec_dev;
	u8 buf[2];
	int irq, ret;

	if (!devm_request_region(dev, EC_LPC_ADDR_MEMMAP, EC_MEMMAP_SIZE,
				 dev_name(dev))) {
		dev_err(dev, "couldn't reserve memmap region\n");
		return -EBUSY;
	}

	cros_ec_lpc_read_bytes(EC_LPC_ADDR_MEMMAP + EC_MEMMAP_ID, 2, buf);
	if (buf[0] != 'E' || buf[1] != 'C') {
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

	/*
	 * Some boards do not have an IRQ allotted for cros_ec_lpc,
	 * which makes ENXIO an expected (and safe) scenario.
	 */
	irq = platform_get_irq(pdev, 0);
	if (irq > 0)
		ec_dev->irq = irq;
	else if (irq != -ENXIO) {
		dev_err(dev, "couldn't retrieve IRQ number (%d)\n", irq);
		return irq;
	}

	ret = cros_ec_register(ec_dev);
	if (ret) {
		dev_err(dev, "couldn't register ec_dev (%d)\n", ret);
		return ret;
	}

	/*
	 * Connect a notify handler to process MKBP messages if we have a
	 * companion ACPI device.
	 */
	adev = ACPI_COMPANION(dev);
	if (adev) {
		status = acpi_install_notify_handler(adev->handle,
						     ACPI_ALL_NOTIFY,
						     cros_ec_lpc_acpi_notify,
						     ec_dev);
		if (ACPI_FAILURE(status))
			dev_warn(dev, "Failed to register notifier %08x\n",
				 status);
	}

	return 0;
}

static int cros_ec_lpc_remove(struct platform_device *pdev)
{
	struct acpi_device *adev;

	adev = ACPI_COMPANION(&pdev->dev);
	if (adev)
		acpi_remove_notify_handler(adev->handle, ACPI_ALL_NOTIFY,
					   cros_ec_lpc_acpi_notify);

	return 0;
}

static const struct acpi_device_id cros_ec_lpc_acpi_device_ids[] = {
	{ ACPI_DRV_NAME, 0 },
	{ }
};
MODULE_DEVICE_TABLE(acpi, cros_ec_lpc_acpi_device_ids);

static const struct dmi_system_id cros_ec_lpc_dmi_table[] __initconst = {
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
		/*
		 * If the box is running custom coreboot firmware then the
		 * DMI BIOS version string will not be matched by "Google_",
		 * but the system vendor string will still be matched by
		 * "GOOGLE".
		 */
		.matches = {
			DMI_MATCH(DMI_BIOS_VENDOR, "coreboot"),
			DMI_MATCH(DMI_SYS_VENDOR, "GOOGLE"),
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
	{
		/* x86-glimmer, the Lenovo Thinkpad Yoga 11e. */
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "GOOGLE"),
			DMI_MATCH(DMI_PRODUCT_NAME, "Glimmer"),
		},
	},
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(dmi, cros_ec_lpc_dmi_table);

#ifdef CONFIG_PM_SLEEP
static int cros_ec_lpc_suspend(struct device *dev)
{
	struct cros_ec_device *ec_dev = dev_get_drvdata(dev);

	return cros_ec_suspend(ec_dev);
}

static int cros_ec_lpc_resume(struct device *dev)
{
	struct cros_ec_device *ec_dev = dev_get_drvdata(dev);

	return cros_ec_resume(ec_dev);
}
#endif

const struct dev_pm_ops cros_ec_lpc_pm_ops = {
	SET_LATE_SYSTEM_SLEEP_PM_OPS(cros_ec_lpc_suspend, cros_ec_lpc_resume)
};

static struct platform_driver cros_ec_lpc_driver = {
	.driver = {
		.name = DRV_NAME,
		.acpi_match_table = cros_ec_lpc_acpi_device_ids,
		.pm = &cros_ec_lpc_pm_ops,
	},
	.probe = cros_ec_lpc_probe,
	.remove = cros_ec_lpc_remove,
};

static struct platform_device cros_ec_lpc_device = {
	.name = DRV_NAME
};

static acpi_status cros_ec_lpc_parse_device(acpi_handle handle, u32 level,
					    void *context, void **retval)
{
	*(bool *)context = true;
	return AE_CTRL_TERMINATE;
}

static int __init cros_ec_lpc_init(void)
{
	int ret;
	acpi_status status;

	status = acpi_get_devices(ACPI_DRV_NAME, cros_ec_lpc_parse_device,
				  &cros_ec_lpc_acpi_device_found, NULL);
	if (ACPI_FAILURE(status))
		pr_warn(DRV_NAME ": Looking for %s failed\n", ACPI_DRV_NAME);

	if (!cros_ec_lpc_acpi_device_found &&
	    !dmi_check_system(cros_ec_lpc_dmi_table)) {
		pr_err(DRV_NAME ": unsupported system.\n");
		return -ENODEV;
	}

	cros_ec_lpc_reg_init();

	/* Register the driver */
	ret = platform_driver_register(&cros_ec_lpc_driver);
	if (ret) {
		pr_err(DRV_NAME ": can't register driver: %d\n", ret);
		cros_ec_lpc_reg_destroy();
		return ret;
	}

	if (!cros_ec_lpc_acpi_device_found) {
		/* Register the device, and it'll get hooked up automatically */
		ret = platform_device_register(&cros_ec_lpc_device);
		if (ret) {
			pr_err(DRV_NAME ": can't register device: %d\n", ret);
			platform_driver_unregister(&cros_ec_lpc_driver);
			cros_ec_lpc_reg_destroy();
		}
	}

	return ret;
}

static void __exit cros_ec_lpc_exit(void)
{
	if (!cros_ec_lpc_acpi_device_found)
		platform_device_unregister(&cros_ec_lpc_device);
	platform_driver_unregister(&cros_ec_lpc_driver);
	cros_ec_lpc_reg_destroy();
}

module_init(cros_ec_lpc_init);
module_exit(cros_ec_lpc_exit);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("ChromeOS EC LPC driver");
