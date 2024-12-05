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
#include <linux/kobject.h>
#include <linux/module.h>
#include <linux/platform_data/cros_ec_commands.h>
#include <linux/platform_data/cros_ec_proto.h>
#include <linux/platform_device.h>
#include <linux/printk.h>
#include <linux/reboot.h>
#include <linux/suspend.h>

#include "cros_ec.h"
#include "cros_ec_lpc_mec.h"

#define DRV_NAME "cros_ec_lpcs"
#define ACPI_DRV_NAME "GOOG0004"

/* True if ACPI device is present */
static bool cros_ec_lpc_acpi_device_found;

/*
 * Indicates that lpc_driver_data.quirk_mmio_memory_base should
 * be used as the base port for EC mapped memory.
 */
#define CROS_EC_LPC_QUIRK_REMAP_MEMORY              BIT(0)
/*
 * Indicates that lpc_driver_data.quirk_acpi_id should be used to find
 * the ACPI device.
 */
#define CROS_EC_LPC_QUIRK_ACPI_ID                   BIT(1)
/*
 * Indicates that lpc_driver_data.quirk_aml_mutex_name should be used
 * to find an AML mutex to protect access to Microchip EC.
 */
#define CROS_EC_LPC_QUIRK_AML_MUTEX                 BIT(2)

/**
 * struct lpc_driver_data - driver data attached to a DMI device ID to indicate
 *                          hardware quirks.
 * @quirks: a bitfield composed of quirks from CROS_EC_LPC_QUIRK_*
 * @quirk_mmio_memory_base: The first I/O port addressing EC mapped memory (used
 *                          when quirk ...REMAP_MEMORY is set.)
 * @quirk_acpi_id: An ACPI HID to be used to find the ACPI device.
 * @quirk_aml_mutex_name: The name of an AML mutex to be used to protect access
 *                        to Microchip EC.
 */
struct lpc_driver_data {
	u32 quirks;
	u16 quirk_mmio_memory_base;
	const char *quirk_acpi_id;
	const char *quirk_aml_mutex_name;
};

/**
 * struct cros_ec_lpc - LPC device-specific data
 * @mmio_memory_base: The first I/O port addressing EC mapped memory.
 */
struct cros_ec_lpc {
	u16 mmio_memory_base;
};

/**
 * struct lpc_driver_ops - LPC driver operations
 * @read: Copy length bytes from EC address offset into buffer dest.
 *        Returns a negative error code on error, or the 8-bit checksum
 *        of all bytes read.
 * @write: Copy length bytes from buffer msg into EC address offset.
 *         Returns a negative error code on error, or the 8-bit checksum
 *         of all bytes written.
 */
struct lpc_driver_ops {
	int (*read)(unsigned int offset, unsigned int length, u8 *dest);
	int (*write)(unsigned int offset, unsigned int length, const u8 *msg);
};

static struct lpc_driver_ops cros_ec_lpc_ops = { };

/*
 * A generic instance of the read function of struct lpc_driver_ops, used for
 * the LPC EC.
 */
static int cros_ec_lpc_read_bytes(unsigned int offset, unsigned int length,
				  u8 *dest)
{
	u8 sum = 0;
	int i;

	for (i = 0; i < length; ++i) {
		dest[i] = inb(offset + i);
		sum += dest[i];
	}

	/* Return checksum of all bytes read */
	return sum;
}

/*
 * A generic instance of the write function of struct lpc_driver_ops, used for
 * the LPC EC.
 */
static int cros_ec_lpc_write_bytes(unsigned int offset, unsigned int length,
				   const u8 *msg)
{
	u8 sum = 0;
	int i;

	for (i = 0; i < length; ++i) {
		outb(msg[i], offset + i);
		sum += msg[i];
	}

	/* Return checksum of all bytes written */
	return sum;
}

/*
 * An instance of the read function of struct lpc_driver_ops, used for the
 * MEC variant of LPC EC.
 */
static int cros_ec_lpc_mec_read_bytes(unsigned int offset, unsigned int length,
				      u8 *dest)
{
	int in_range = cros_ec_lpc_mec_in_range(offset, length);

	if (in_range < 0)
		return in_range;

	return in_range ?
		cros_ec_lpc_io_bytes_mec(MEC_IO_READ,
					 offset - EC_HOST_CMD_REGION0,
					 length, dest) :
		cros_ec_lpc_read_bytes(offset, length, dest);
}

/*
 * An instance of the write function of struct lpc_driver_ops, used for the
 * MEC variant of LPC EC.
 */
static int cros_ec_lpc_mec_write_bytes(unsigned int offset, unsigned int length,
				       const u8 *msg)
{
	int in_range = cros_ec_lpc_mec_in_range(offset, length);

	if (in_range < 0)
		return in_range;

	return in_range ?
		cros_ec_lpc_io_bytes_mec(MEC_IO_WRITE,
					 offset - EC_HOST_CMD_REGION0,
					 length, (u8 *)msg) :
		cros_ec_lpc_write_bytes(offset, length, msg);
}

static int ec_response_timed_out(void)
{
	unsigned long one_second = jiffies + HZ;
	u8 data;
	int ret;

	usleep_range(200, 300);
	do {
		ret = cros_ec_lpc_ops.read(EC_LPC_ADDR_HOST_CMD, 1, &data);
		if (ret < 0)
			return ret;
		if (!(data & EC_LPC_STATUS_BUSY_MASK))
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
	if (ret < 0)
		goto done;

	/* Write buffer */
	ret = cros_ec_lpc_ops.write(EC_LPC_ADDR_HOST_PACKET, ret, ec->dout);
	if (ret < 0)
		goto done;

	/* Here we go */
	sum = EC_COMMAND_PROTOCOL_3;
	ret = cros_ec_lpc_ops.write(EC_LPC_ADDR_HOST_CMD, 1, &sum);
	if (ret < 0)
		goto done;

	ret = ec_response_timed_out();
	if (ret < 0)
		goto done;
	if (ret) {
		dev_warn(ec->dev, "EC response timed out\n");
		ret = -EIO;
		goto done;
	}

	/* Check result */
	ret = cros_ec_lpc_ops.read(EC_LPC_ADDR_HOST_DATA, 1, &sum);
	if (ret < 0)
		goto done;
	msg->result = ret;
	ret = cros_ec_check_result(ec, msg);
	if (ret)
		goto done;

	/* Read back response */
	dout = (u8 *)&response;
	ret = cros_ec_lpc_ops.read(EC_LPC_ADDR_HOST_PACKET, sizeof(response),
				   dout);
	if (ret < 0)
		goto done;
	sum = ret;

	msg->result = response.result;

	if (response.data_len > msg->insize) {
		dev_err(ec->dev,
			"packet too long (%d bytes, expected %d)",
			response.data_len, msg->insize);
		ret = -EMSGSIZE;
		goto done;
	}

	/* Read response and process checksum */
	ret = cros_ec_lpc_ops.read(EC_LPC_ADDR_HOST_PACKET +
				   sizeof(response), response.data_len,
				   msg->data);
	if (ret < 0)
		goto done;
	sum += ret;

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
	ret = cros_ec_lpc_ops.write(EC_LPC_ADDR_HOST_PARAM, msg->outsize,
				    msg->data);
	if (ret < 0)
		goto done;
	sum += ret;

	/* Finalize checksum and write args */
	args.checksum = sum;
	ret = cros_ec_lpc_ops.write(EC_LPC_ADDR_HOST_ARGS, sizeof(args),
				    (u8 *)&args);
	if (ret < 0)
		goto done;

	/* Here we go */
	sum = msg->command;
	ret = cros_ec_lpc_ops.write(EC_LPC_ADDR_HOST_CMD, 1, &sum);
	if (ret < 0)
		goto done;

	ret = ec_response_timed_out();
	if (ret < 0)
		goto done;
	if (ret) {
		dev_warn(ec->dev, "EC response timed out\n");
		ret = -EIO;
		goto done;
	}

	/* Check result */
	ret = cros_ec_lpc_ops.read(EC_LPC_ADDR_HOST_DATA, 1, &sum);
	if (ret < 0)
		goto done;
	msg->result = ret;
	ret = cros_ec_check_result(ec, msg);
	if (ret)
		goto done;

	/* Read back args */
	ret = cros_ec_lpc_ops.read(EC_LPC_ADDR_HOST_ARGS, sizeof(args), (u8 *)&args);
	if (ret < 0)
		goto done;

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
	ret = cros_ec_lpc_ops.read(EC_LPC_ADDR_HOST_PARAM, args.data_size,
				   msg->data);
	if (ret < 0)
		goto done;
	sum += ret;

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
	struct cros_ec_lpc *ec_lpc = ec->priv;
	int i = offset;
	char *s = dest;
	int cnt = 0;
	int ret;

	if (offset >= EC_MEMMAP_SIZE - bytes)
		return -EINVAL;

	/* fixed length */
	if (bytes) {
		ret = cros_ec_lpc_ops.read(ec_lpc->mmio_memory_base + offset, bytes, s);
		if (ret < 0)
			return ret;
		return bytes;
	}

	/* string */
	for (; i < EC_MEMMAP_SIZE; i++, s++) {
		ret = cros_ec_lpc_ops.read(ec_lpc->mmio_memory_base + i, 1, s);
		if (ret < 0)
			return ret;
		cnt++;
		if (!*s)
			break;
	}

	return cnt;
}

static void cros_ec_lpc_acpi_notify(acpi_handle device, u32 value, void *data)
{
	static const char *env[] = { "ERROR=PANIC", NULL };
	struct cros_ec_device *ec_dev = data;
	bool ec_has_more_events;
	int ret;

	ec_dev->last_event_time = cros_ec_get_time_ns();

	if (value == ACPI_NOTIFY_CROS_EC_PANIC) {
		dev_emerg(ec_dev->dev, "CrOS EC Panic Reported. Shutdown is imminent!");
		blocking_notifier_call_chain(&ec_dev->panic_notifier, 0, ec_dev);
		kobject_uevent_env(&ec_dev->dev->kobj, KOBJ_CHANGE, (char **)env);
		/* Begin orderly shutdown. EC will force reset after a short period. */
		hw_protection_shutdown("CrOS EC Panic", -1);
		/* Do not query for other events after a panic is reported */
		return;
	}

	if (ec_dev->mkbp_event_supported)
		do {
			ret = cros_ec_get_next_event(ec_dev, NULL,
						     &ec_has_more_events);
			if (ret > 0)
				blocking_notifier_call_chain(
						&ec_dev->event_notifier, 0,
						ec_dev);
		} while (ec_has_more_events);

	if (value == ACPI_NOTIFY_DEVICE_WAKE)
		pm_system_wakeup();
}

static acpi_status cros_ec_lpc_parse_device(acpi_handle handle, u32 level,
					    void *context, void **retval)
{
	*(struct acpi_device **)context = acpi_fetch_acpi_dev(handle);
	return AE_CTRL_TERMINATE;
}

static struct acpi_device *cros_ec_lpc_get_device(const char *id)
{
	struct acpi_device *adev = NULL;
	acpi_status status = acpi_get_devices(id, cros_ec_lpc_parse_device,
					      &adev, NULL);
	if (ACPI_FAILURE(status)) {
		pr_warn(DRV_NAME ": Looking for %s failed\n", id);
		return NULL;
	}

	return adev;
}

static int cros_ec_lpc_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct acpi_device *adev;
	acpi_status status;
	struct cros_ec_device *ec_dev;
	struct cros_ec_lpc *ec_lpc;
	struct lpc_driver_data *driver_data;
	u8 buf[2] = {};
	int irq, ret;
	u32 quirks;

	ec_lpc = devm_kzalloc(dev, sizeof(*ec_lpc), GFP_KERNEL);
	if (!ec_lpc)
		return -ENOMEM;

	ec_lpc->mmio_memory_base = EC_LPC_ADDR_MEMMAP;

	driver_data = platform_get_drvdata(pdev);
	if (driver_data) {
		quirks = driver_data->quirks;

		if (quirks)
			dev_info(dev, "loaded with quirks %8.08x\n", quirks);

		if (quirks & CROS_EC_LPC_QUIRK_REMAP_MEMORY)
			ec_lpc->mmio_memory_base = driver_data->quirk_mmio_memory_base;

		if (quirks & CROS_EC_LPC_QUIRK_ACPI_ID) {
			adev = cros_ec_lpc_get_device(driver_data->quirk_acpi_id);
			if (!adev) {
				dev_err(dev, "failed to get ACPI device '%s'",
					driver_data->quirk_acpi_id);
				return -ENODEV;
			}
			ACPI_COMPANION_SET(dev, adev);
		}

		if (quirks & CROS_EC_LPC_QUIRK_AML_MUTEX) {
			const char *name
				= driver_data->quirk_aml_mutex_name;
			ret = cros_ec_lpc_mec_acpi_mutex(ACPI_COMPANION(dev), name);
			if (ret) {
				dev_err(dev, "failed to get AML mutex '%s'", name);
				return ret;
			}
			dev_info(dev, "got AML mutex '%s'", name);
		}
	}

	/*
	 * The Framework Laptop (and possibly other non-ChromeOS devices)
	 * only exposes the eight I/O ports that are required for the Microchip EC.
	 * Requesting a larger reservation will fail.
	 */
	if (!devm_request_region(dev, EC_HOST_CMD_REGION0,
				 EC_HOST_CMD_MEC_REGION_SIZE, dev_name(dev))) {
		dev_err(dev, "couldn't reserve MEC region\n");
		return -EBUSY;
	}

	cros_ec_lpc_mec_init(EC_HOST_CMD_REGION0,
			     EC_LPC_ADDR_MEMMAP + EC_MEMMAP_SIZE);

	/*
	 * Read the mapped ID twice, the first one is assuming the
	 * EC is a Microchip Embedded Controller (MEC) variant, if the
	 * protocol fails, fallback to the non MEC variant and try to
	 * read again the ID.
	 */
	cros_ec_lpc_ops.read = cros_ec_lpc_mec_read_bytes;
	cros_ec_lpc_ops.write = cros_ec_lpc_mec_write_bytes;
	ret = cros_ec_lpc_ops.read(EC_LPC_ADDR_MEMMAP + EC_MEMMAP_ID, 2, buf);
	if (ret < 0)
		return ret;
	if (buf[0] != 'E' || buf[1] != 'C') {
		if (!devm_request_region(dev, ec_lpc->mmio_memory_base, EC_MEMMAP_SIZE,
					 dev_name(dev))) {
			dev_err(dev, "couldn't reserve memmap region\n");
			return -EBUSY;
		}

		/* Re-assign read/write operations for the non MEC variant */
		cros_ec_lpc_ops.read = cros_ec_lpc_read_bytes;
		cros_ec_lpc_ops.write = cros_ec_lpc_write_bytes;
		ret = cros_ec_lpc_ops.read(ec_lpc->mmio_memory_base + EC_MEMMAP_ID, 2,
					   buf);
		if (ret < 0)
			return ret;
		if (buf[0] != 'E' || buf[1] != 'C') {
			dev_err(dev, "EC ID not detected\n");
			return -ENODEV;
		}

		/* Reserve the remaining I/O ports required by the non-MEC protocol. */
		if (!devm_request_region(dev, EC_HOST_CMD_REGION0 + EC_HOST_CMD_MEC_REGION_SIZE,
					 EC_HOST_CMD_REGION_SIZE - EC_HOST_CMD_MEC_REGION_SIZE,
					 dev_name(dev))) {
			dev_err(dev, "couldn't reserve remainder of region0\n");
			return -EBUSY;
		}
		if (!devm_request_region(dev, EC_HOST_CMD_REGION1,
					 EC_HOST_CMD_REGION_SIZE, dev_name(dev))) {
			dev_err(dev, "couldn't reserve region1\n");
			return -EBUSY;
		}
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
	ec_dev->priv = ec_lpc;

	/*
	 * Some boards do not have an IRQ allotted for cros_ec_lpc,
	 * which makes ENXIO an expected (and safe) scenario.
	 */
	irq = platform_get_irq_optional(pdev, 0);
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

static void cros_ec_lpc_remove(struct platform_device *pdev)
{
	struct cros_ec_device *ec_dev = platform_get_drvdata(pdev);
	struct acpi_device *adev;

	adev = ACPI_COMPANION(&pdev->dev);
	if (adev)
		acpi_remove_notify_handler(adev->handle, ACPI_ALL_NOTIFY,
					   cros_ec_lpc_acpi_notify);

	cros_ec_unregister(ec_dev);
}

static const struct acpi_device_id cros_ec_lpc_acpi_device_ids[] = {
	{ ACPI_DRV_NAME, 0 },
	{ }
};
MODULE_DEVICE_TABLE(acpi, cros_ec_lpc_acpi_device_ids);

static const struct lpc_driver_data framework_laptop_npcx_lpc_driver_data __initconst = {
	.quirks = CROS_EC_LPC_QUIRK_REMAP_MEMORY,
	.quirk_mmio_memory_base = 0xE00,
};

static const struct lpc_driver_data framework_laptop_mec_lpc_driver_data __initconst = {
	.quirks = CROS_EC_LPC_QUIRK_ACPI_ID|CROS_EC_LPC_QUIRK_AML_MUTEX,
	.quirk_acpi_id = "PNP0C09",
	.quirk_aml_mutex_name = "ECMT",
};

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
	/* A small number of non-Chromebook/box machines also use the ChromeOS EC */
	{
		/* Framework Laptop (11th Gen Intel Core) */
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "Framework"),
			DMI_EXACT_MATCH(DMI_PRODUCT_NAME, "Laptop"),
		},
		.driver_data = (void *)&framework_laptop_mec_lpc_driver_data,
	},
	{
		/* Framework Laptop (12th Gen Intel Core) */
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "Framework"),
			DMI_EXACT_MATCH(DMI_PRODUCT_NAME, "12th Gen Intel Core"),
		},
		.driver_data = (void *)&framework_laptop_mec_lpc_driver_data,
	},
	{
		/* Framework Laptop (13th Gen Intel Core) */
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "Framework"),
			DMI_EXACT_MATCH(DMI_PRODUCT_NAME, "13th Gen Intel Core"),
		},
		.driver_data = (void *)&framework_laptop_mec_lpc_driver_data,
	},
	{
		/*
		 * All remaining Framework Laptop models (13 AMD Ryzen, 16 AMD
		 * Ryzen, Intel Core Ultra)
		 */
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "Framework"),
			DMI_MATCH(DMI_PRODUCT_FAMILY, "Laptop"),
		},
		.driver_data = (void *)&framework_laptop_npcx_lpc_driver_data,
	},
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(dmi, cros_ec_lpc_dmi_table);

#ifdef CONFIG_PM_SLEEP
static int cros_ec_lpc_prepare(struct device *dev)
{
	struct cros_ec_device *ec_dev = dev_get_drvdata(dev);
	return cros_ec_suspend_prepare(ec_dev);
}

static void cros_ec_lpc_complete(struct device *dev)
{
	struct cros_ec_device *ec_dev = dev_get_drvdata(dev);
	cros_ec_resume_complete(ec_dev);
}

static int cros_ec_lpc_suspend_late(struct device *dev)
{
	struct cros_ec_device *ec_dev = dev_get_drvdata(dev);

	return cros_ec_suspend_late(ec_dev);
}

static int cros_ec_lpc_resume_early(struct device *dev)
{
	struct cros_ec_device *ec_dev = dev_get_drvdata(dev);

	return cros_ec_resume_early(ec_dev);
}
#endif

static const struct dev_pm_ops cros_ec_lpc_pm_ops = {
#ifdef CONFIG_PM_SLEEP
	.prepare = cros_ec_lpc_prepare,
	.complete = cros_ec_lpc_complete,
#endif
	SET_LATE_SYSTEM_SLEEP_PM_OPS(cros_ec_lpc_suspend_late, cros_ec_lpc_resume_early)
};

static struct platform_driver cros_ec_lpc_driver = {
	.driver = {
		.name = DRV_NAME,
		.acpi_match_table = cros_ec_lpc_acpi_device_ids,
		.pm = &cros_ec_lpc_pm_ops,
		/*
		 * ACPI child devices may probe before us, and they racily
		 * check our drvdata pointer. Force synchronous probe until
		 * those races are resolved.
		 */
		.probe_type = PROBE_FORCE_SYNCHRONOUS,
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
	const struct dmi_system_id *dmi_match;

	cros_ec_lpc_acpi_device_found = !!cros_ec_lpc_get_device(ACPI_DRV_NAME);

	dmi_match = dmi_first_match(cros_ec_lpc_dmi_table);

	if (!cros_ec_lpc_acpi_device_found && !dmi_match) {
		pr_err(DRV_NAME ": unsupported system.\n");
		return -ENODEV;
	}

	/* Register the driver */
	ret = platform_driver_register(&cros_ec_lpc_driver);
	if (ret) {
		pr_err(DRV_NAME ": can't register driver: %d\n", ret);
		return ret;
	}

	if (!cros_ec_lpc_acpi_device_found) {
		/* Pass the DMI match's driver data down to the platform device */
		platform_set_drvdata(&cros_ec_lpc_device, dmi_match->driver_data);

		/* Register the device, and it'll get hooked up automatically */
		ret = platform_device_register(&cros_ec_lpc_device);
		if (ret) {
			pr_err(DRV_NAME ": can't register device: %d\n", ret);
			platform_driver_unregister(&cros_ec_lpc_driver);
		}
	}

	return ret;
}

static void __exit cros_ec_lpc_exit(void)
{
	if (!cros_ec_lpc_acpi_device_found)
		platform_device_unregister(&cros_ec_lpc_device);
	platform_driver_unregister(&cros_ec_lpc_driver);
}

module_init(cros_ec_lpc_init);
module_exit(cros_ec_lpc_exit);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("ChromeOS EC LPC driver");
