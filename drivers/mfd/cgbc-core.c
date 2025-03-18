// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Congatec Board Controller core driver.
 *
 * The x86 Congatec modules have an embedded micro controller named Board
 * Controller. This Board Controller has a Watchdog timer, some GPIOs, and two
 * I2C busses.
 *
 * Copyright (C) 2024 Bootlin
 *
 * Author: Thomas Richard <thomas.richard@bootlin.com>
 */

#include <linux/dmi.h>
#include <linux/iopoll.h>
#include <linux/mfd/cgbc.h>
#include <linux/mfd/core.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/sysfs.h>

#define CGBC_IO_SESSION_BASE	0x0E20
#define CGBC_IO_SESSION_END	0x0E30
#define CGBC_IO_CMD_BASE	0x0E00
#define CGBC_IO_CMD_END		0x0E10

#define CGBC_MASK_STATUS	(BIT(6) | BIT(7))
#define CGBC_MASK_DATA_COUNT	0x1F
#define CGBC_MASK_ERROR_CODE	0x1F

#define CGBC_STATUS_DATA_READY	0x00
#define CGBC_STATUS_CMD_READY	BIT(6)
#define CGBC_STATUS_ERROR	(BIT(6) | BIT(7))

#define CGBC_SESSION_CMD		0x00
#define CGBC_SESSION_CMD_IDLE		0x00
#define CGBC_SESSION_CMD_REQUEST	0x01
#define CGBC_SESSION_DATA		0x01
#define CGBC_SESSION_STATUS		0x02
#define CGBC_SESSION_STATUS_FREE	0x03
#define CGBC_SESSION_ACCESS		0x04
#define CGBC_SESSION_ACCESS_GAINED	0x00

#define CGBC_SESSION_VALID_MIN  0x02
#define CGBC_SESSION_VALID_MAX  0xFE

#define CGBC_CMD_STROBE			0x00
#define CGBC_CMD_INDEX			0x02
#define CGBC_CMD_INDEX_CBM_MAN8		0x00
#define CGBC_CMD_INDEX_CBM_AUTO32	0x03
#define CGBC_CMD_DATA			0x04
#define CGBC_CMD_ACCESS			0x0C

#define CGBC_CMD_GET_FW_REV	0x21

static struct platform_device *cgbc_pdev;

/* Wait the Board Controller is ready to receive some session commands */
static int cgbc_wait_device(struct cgbc_device_data *cgbc)
{
	u16 status;
	int ret;

	ret = readx_poll_timeout(ioread16, cgbc->io_session + CGBC_SESSION_STATUS, status,
				 status == CGBC_SESSION_STATUS_FREE, 0, 500000);

	if (ret || ioread32(cgbc->io_session + CGBC_SESSION_ACCESS))
		ret = -ENODEV;

	return ret;
}

static int cgbc_session_command(struct cgbc_device_data *cgbc, u8 cmd)
{
	int ret;
	u8 val;

	ret = readx_poll_timeout(ioread8, cgbc->io_session + CGBC_SESSION_CMD, val,
				 val == CGBC_SESSION_CMD_IDLE, 0, 100000);
	if (ret)
		return ret;

	iowrite8(cmd, cgbc->io_session + CGBC_SESSION_CMD);

	ret = readx_poll_timeout(ioread8, cgbc->io_session + CGBC_SESSION_CMD, val,
				 val == CGBC_SESSION_CMD_IDLE, 0, 100000);
	if (ret)
		return ret;

	ret = (int)ioread8(cgbc->io_session + CGBC_SESSION_DATA);

	iowrite8(CGBC_SESSION_STATUS_FREE, cgbc->io_session + CGBC_SESSION_STATUS);

	return ret;
}

static int cgbc_session_request(struct cgbc_device_data *cgbc)
{
	int ret;

	ret = cgbc_wait_device(cgbc);

	if (ret)
		return dev_err_probe(cgbc->dev, ret, "device not found or not ready\n");

	cgbc->session = cgbc_session_command(cgbc, CGBC_SESSION_CMD_REQUEST);

	/* The Board Controller sent us a wrong session handle, we cannot communicate with it */
	if (cgbc->session < CGBC_SESSION_VALID_MIN || cgbc->session > CGBC_SESSION_VALID_MAX)
		return dev_err_probe(cgbc->dev, -ECONNREFUSED,
				     "failed to get a valid session handle\n");

	return 0;
}

static void cgbc_session_release(struct cgbc_device_data *cgbc)
{
	if (cgbc_session_command(cgbc, cgbc->session) != cgbc->session)
		dev_warn(cgbc->dev, "failed to release session\n");
}

static bool cgbc_command_lock(struct cgbc_device_data *cgbc)
{
	iowrite8(cgbc->session, cgbc->io_cmd + CGBC_CMD_ACCESS);

	return ioread8(cgbc->io_cmd + CGBC_CMD_ACCESS) == cgbc->session;
}

static void cgbc_command_unlock(struct cgbc_device_data *cgbc)
{
	iowrite8(cgbc->session, cgbc->io_cmd + CGBC_CMD_ACCESS);
}

int cgbc_command(struct cgbc_device_data *cgbc, void *cmd, unsigned int cmd_size, void *data,
		 unsigned int data_size, u8 *status)
{
	u8 checksum = 0, data_checksum = 0, istatus = 0, val;
	u8 *_data = (u8 *)data;
	u8 *_cmd = (u8 *)cmd;
	int mode_change = -1;
	bool lock;
	int ret, i;

	mutex_lock(&cgbc->lock);

	/* Request access */
	ret = readx_poll_timeout(cgbc_command_lock, cgbc, lock, lock, 0, 100000);
	if (ret)
		goto out;

	/* Wait board controller is ready */
	ret = readx_poll_timeout(ioread8, cgbc->io_cmd + CGBC_CMD_STROBE, val,
				 val == CGBC_CMD_STROBE, 0, 100000);
	if (ret)
		goto release;

	/* Write command packet */
	if (cmd_size <= 2) {
		iowrite8(CGBC_CMD_INDEX_CBM_MAN8, cgbc->io_cmd + CGBC_CMD_INDEX);
	} else {
		iowrite8(CGBC_CMD_INDEX_CBM_AUTO32, cgbc->io_cmd + CGBC_CMD_INDEX);
		if ((cmd_size % 4) != 0x03)
			mode_change = (cmd_size & 0xFFFC) - 1;
	}

	for (i = 0; i < cmd_size; i++) {
		iowrite8(_cmd[i], cgbc->io_cmd + CGBC_CMD_DATA + (i % 4));
		checksum ^= _cmd[i];
		if (mode_change == i)
			iowrite8((i + 1) | CGBC_CMD_INDEX_CBM_MAN8, cgbc->io_cmd + CGBC_CMD_INDEX);
	}

	/* Append checksum byte */
	iowrite8(checksum, cgbc->io_cmd + CGBC_CMD_DATA + (i % 4));

	/* Perform command strobe */
	iowrite8(cgbc->session, cgbc->io_cmd + CGBC_CMD_STROBE);

	/* Rewind cmd buffer index */
	iowrite8(CGBC_CMD_INDEX_CBM_AUTO32, cgbc->io_cmd + CGBC_CMD_INDEX);

	/* Wait command completion */
	ret = read_poll_timeout(ioread8, val, val == CGBC_CMD_STROBE, 0, 100000, false,
				cgbc->io_cmd + CGBC_CMD_STROBE);
	if (ret)
		goto release;

	istatus = ioread8(cgbc->io_cmd + CGBC_CMD_DATA);
	checksum = istatus;

	/* Check command status */
	switch (istatus & CGBC_MASK_STATUS) {
	case CGBC_STATUS_DATA_READY:
		if (istatus > data_size)
			istatus = data_size;
		for (i = 0; i < istatus; i++) {
			_data[i] = ioread8(cgbc->io_cmd + CGBC_CMD_DATA + ((i + 1) % 4));
			checksum ^= _data[i];
		}
		data_checksum = ioread8(cgbc->io_cmd + CGBC_CMD_DATA + ((i + 1) % 4));
		istatus &= CGBC_MASK_DATA_COUNT;
		break;
	case CGBC_STATUS_ERROR:
	case CGBC_STATUS_CMD_READY:
		data_checksum = ioread8(cgbc->io_cmd + CGBC_CMD_DATA + 1);
		if ((istatus & CGBC_MASK_STATUS) == CGBC_STATUS_ERROR)
			ret = -EIO;
		istatus = istatus & CGBC_MASK_ERROR_CODE;
		break;
	default:
		data_checksum = ioread8(cgbc->io_cmd + CGBC_CMD_DATA + 1);
		istatus &= CGBC_MASK_ERROR_CODE;
		ret = -EIO;
		break;
	}

	/* Checksum verification */
	if (ret == 0 && data_checksum != checksum)
		ret = -EIO;

release:
	cgbc_command_unlock(cgbc);

out:
	mutex_unlock(&cgbc->lock);

	if (status)
		*status = istatus;

	return ret;
}
EXPORT_SYMBOL_GPL(cgbc_command);

static struct mfd_cell cgbc_devs[] = {
	{ .name = "cgbc-wdt"	},
	{ .name = "cgbc-gpio"	},
	{ .name = "cgbc-i2c", .id = 1 },
	{ .name = "cgbc-i2c", .id = 2 },
	{ .name = "cgbc-hwmon"	},
};

static int cgbc_map(struct cgbc_device_data *cgbc)
{
	struct device *dev = cgbc->dev;
	struct platform_device *pdev = to_platform_device(dev);
	struct resource *ioport;

	ioport = platform_get_resource(pdev, IORESOURCE_IO, 0);
	if (!ioport)
		return -EINVAL;

	cgbc->io_session = devm_ioport_map(dev, ioport->start, resource_size(ioport));
	if (!cgbc->io_session)
		return -ENOMEM;

	ioport = platform_get_resource(pdev, IORESOURCE_IO, 1);
	if (!ioport)
		return -EINVAL;

	cgbc->io_cmd = devm_ioport_map(dev, ioport->start, resource_size(ioport));
	if (!cgbc->io_cmd)
		return -ENOMEM;

	return 0;
}

static const struct resource cgbc_resources[] = {
	{
		.start  = CGBC_IO_SESSION_BASE,
		.end    = CGBC_IO_SESSION_END,
		.flags  = IORESOURCE_IO,
	},
	{
		.start  = CGBC_IO_CMD_BASE,
		.end    = CGBC_IO_CMD_END,
		.flags  = IORESOURCE_IO,
	},
};

static ssize_t cgbc_version_show(struct device *dev,
				 struct device_attribute *attr, char *buf)
{
	struct cgbc_device_data *cgbc = dev_get_drvdata(dev);

	return sysfs_emit(buf, "CGBCP%c%c%c\n", cgbc->version.feature, cgbc->version.major,
			  cgbc->version.minor);
}

static DEVICE_ATTR_RO(cgbc_version);

static struct attribute *cgbc_attrs[] = {
	&dev_attr_cgbc_version.attr,
	NULL
};

ATTRIBUTE_GROUPS(cgbc);

static int cgbc_get_version(struct cgbc_device_data *cgbc)
{
	u8 cmd = CGBC_CMD_GET_FW_REV;
	u8 data[4];
	int ret;

	ret = cgbc_command(cgbc, &cmd, 1, &data, sizeof(data), NULL);
	if (ret)
		return ret;

	cgbc->version.feature = data[0];
	cgbc->version.major = data[1];
	cgbc->version.minor = data[2];

	return 0;
}

static int cgbc_init_device(struct cgbc_device_data *cgbc)
{
	int ret;

	ret = cgbc_session_request(cgbc);
	if (ret)
		return ret;

	ret = cgbc_get_version(cgbc);
	if (ret)
		goto release_session;

	ret = mfd_add_devices(cgbc->dev, -1, cgbc_devs, ARRAY_SIZE(cgbc_devs),
			      NULL, 0, NULL);
	if (ret)
		goto release_session;

	return 0;

release_session:
	cgbc_session_release(cgbc);
	return ret;
}

static int cgbc_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct cgbc_device_data *cgbc;
	int ret;

	cgbc = devm_kzalloc(dev, sizeof(*cgbc), GFP_KERNEL);
	if (!cgbc)
		return -ENOMEM;

	cgbc->dev = dev;

	ret = cgbc_map(cgbc);
	if (ret)
		return ret;

	mutex_init(&cgbc->lock);

	platform_set_drvdata(pdev, cgbc);

	return cgbc_init_device(cgbc);
}

static void cgbc_remove(struct platform_device *pdev)
{
	struct cgbc_device_data *cgbc = platform_get_drvdata(pdev);

	cgbc_session_release(cgbc);

	mfd_remove_devices(&pdev->dev);
}

static struct platform_driver cgbc_driver = {
	.driver		= {
		.name		= "cgbc",
		.dev_groups	= cgbc_groups,
	},
	.probe		= cgbc_probe,
	.remove		= cgbc_remove,
};

static const struct dmi_system_id cgbc_dmi_table[] __initconst = {
	{
		.ident = "SA7",
		.matches = {
			DMI_MATCH(DMI_BOARD_VENDOR, "congatec"),
			DMI_MATCH(DMI_BOARD_NAME, "conga-SA7"),
		},
	},
	{
		.ident = "SA8",
		.matches = {
			DMI_MATCH(DMI_BOARD_VENDOR, "congatec"),
			DMI_MATCH(DMI_BOARD_NAME, "conga-SA8"),
		},
	},
	{}
};
MODULE_DEVICE_TABLE(dmi, cgbc_dmi_table);

static int __init cgbc_init(void)
{
	const struct dmi_system_id *id;
	int ret = -ENODEV;

	id = dmi_first_match(cgbc_dmi_table);
	if (IS_ERR_OR_NULL(id))
		return ret;

	cgbc_pdev = platform_device_register_simple("cgbc", PLATFORM_DEVID_NONE, cgbc_resources,
						    ARRAY_SIZE(cgbc_resources));
	if (IS_ERR(cgbc_pdev))
		return PTR_ERR(cgbc_pdev);

	return platform_driver_register(&cgbc_driver);
}

static void __exit cgbc_exit(void)
{
	platform_device_unregister(cgbc_pdev);
	platform_driver_unregister(&cgbc_driver);
}

module_init(cgbc_init);
module_exit(cgbc_exit);

MODULE_DESCRIPTION("Congatec Board Controller Core Driver");
MODULE_AUTHOR("Thomas Richard <thomas.richard@bootlin.com>");
MODULE_LICENSE("GPL");
MODULE_ALIAS("platform:cgbc-core");
