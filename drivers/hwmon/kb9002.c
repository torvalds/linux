// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Kandou KB9002 PCIe 5.0 retimer hwmon driver.
 *
 * The retimer exposes a system management bus (SMBus 3.0 with PEC)
 * target for firmware-managed status registers. This driver assumes
 * the chip is strapped to SMBus mode and exports the aggregated
 * maximum die temperature as hwmon temp1_input (millidegrees Celsius)
 * plus the firmware version and boot status under debugfs.
 *
 * The raw-I2C path (kb9002_i2c_read/write, used only at probe to switch
 * the host interface) carries the 32-bit register address and data
 * big-endian. The SMBus path (kb9002_fw_read/kb9002_smbus_hw_read)
 * carries the register address and returned data little-endian.
 *
 * Datasheet: Kandou KB9002 PCIe retimer (KA-015171-PD).
 */

#include <linux/bitfield.h>
#include <linux/bitops.h>
#include <linux/cleanup.h>
#include <linux/debugfs.h>
#include <linux/err.h>
#include <linux/hwmon.h>
#include <linux/i2c.h>
#include <linux/iopoll.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/seq_file.h>
#include <linux/types.h>
#include <linux/unaligned.h>

#define KB9002_DEV_NAME			"kb9002"

/*
 * SMBus read command codes. Each read is a two-phase PEC-protected
 * transaction: prime writes the target address, data reads it back with
 * the register contents. FW reads use a 16-bit address, HW reads 32-bit.
 */
#define KB9002_CC_FW_READ_PRIME		0x82
#define KB9002_CC_FW_READ_DATA		0x81
#define KB9002_CC_HW_READ_PRIME		0x8a
#define KB9002_CC_HW_READ_DATA		0x89

/* Firmware register offsets (16-bit). */
#define KB9002_FW_REG_VID		0x0004
#define KB9002_FW_REG_FW_VERSION	0x0500
#define KB9002_FW_REG_TEMP_MAXIMUM	0x0550

#define KB9002_VID_MASK			GENMASK(31, 16)
#define KB9002_VID_KANDOU		0x1e6f

/* Firmware boot status: 0xe8 in the top byte means init completed OK. */
#define KB9002_HW_REG_FW_BOOT_STATUS	0xe0090008
#define KB9002_FW_BOOT_STATUS_OK_MSB	0xe8

/*
 * Hardware registers reached over raw I2C (32-bit addressing). The
 * host-interface bit selects SMBus (set) vs raw-I2C target; parts
 * strapped to raw I2C need it set before SMBus access works.
 */
#define KB9002_HW_REG_REVID		0x00480004
#define KB9002_HW_REG_HOST_IF		0x00480008
#define KB9002_HOST_IF_SMBUS		BIT(1)

#define KB9002_REVID_MASK		GENMASK(7, 0)
#define KB9002_REVID_B0			0x10
#define KB9002_REVID_B1			0x11

/* Retries to drain a stray leading 0xff from the raw-I2C FIFO. */
#define KB9002_REVID_READ_RETRIES	16

/* Temperature: 32-bit Q16.16 absolute Kelvin. */
#define KB9002_TEMP_FRAC_BITS		16
#define KB9002_ABS_ZERO_MILLI_C		(-273150)

/* Firmware takes up to ~2s to respond after a host-interface change. */
#define KB9002_FW_READY_POLL_US		(25 * USEC_PER_MSEC)
#define KB9002_FW_READY_TIMEOUT_US	(2 * USEC_PER_SEC)

struct kb9002_data {
	struct i2c_client *client;
	struct device *hwmon_dev;
};

/* Raw-I2C read: write the 32-bit BE address, then read 4 BE data bytes. */
static int kb9002_i2c_read(struct i2c_client *client, u32 reg, u32 *val)
{
	u8 addr[4];
	u8 rbuf[4];
	struct i2c_msg msgs[2] = {
		{
			.addr = client->addr,
			.flags = 0,
			.len = sizeof(addr),
			.buf = addr,
		},
		{
			.addr = client->addr,
			.flags = I2C_M_RD,
			.len = sizeof(rbuf),
			.buf = rbuf,
		},
	};
	int ret;

	put_unaligned_be32(reg, addr);

	ret = i2c_transfer(client->adapter, msgs, ARRAY_SIZE(msgs));
	if (ret < 0)
		return ret;
	if (ret != ARRAY_SIZE(msgs))
		return -EIO;

	*val = get_unaligned_be32(rbuf);
	return 0;
}

/* Raw-I2C write: 4 BE address bytes followed by 4 BE data bytes. */
static int kb9002_i2c_write(struct i2c_client *client, u32 reg, u32 val)
{
	u8 buf[8];
	struct i2c_msg msg = {
		.addr = client->addr,
		.flags = 0,
		.len = sizeof(buf),
		.buf = buf,
	};
	int ret;

	put_unaligned_be32(reg, &buf[0]);
	put_unaligned_be32(val, &buf[4]);

	ret = i2c_transfer(client->adapter, &msg, 1);
	if (ret < 0)
		return ret;
	if (ret != 1)
		return -EIO;

	return 0;
}

/*
 * Read the silicon revision ID. A fresh FIFO may start with a stray
 * 0xff that shifts the result, so drain one byte between retries until
 * the top byte is no longer 0xff.
 */
static int kb9002_read_revid(struct i2c_client *client, u32 *revid)
{
	u8 dummy;
	int ret;
	int i;

	for (i = 0; i < KB9002_REVID_READ_RETRIES; i++) {
		ret = kb9002_i2c_read(client, KB9002_HW_REG_REVID, revid);
		if (ret)
			return ret;
		if ((*revid >> 24) != 0xff)
			return 0;
		/* Drain one byte from the chip to re-align the I2C FIFO. */
		i2c_master_recv(client, &dummy, 1);
	}

	return -EIO;
}

/*
 * Read a 32-bit firmware register over SMBus: block-write the 16-bit LE
 * address, then block-read the echoed address plus 4 LE data bytes.
 */
static int kb9002_fw_read(struct kb9002_data *data, u16 reg, u32 *val)
{
	struct i2c_client *client = data->client;
	u8 addr[2];
	u8 rbuf[I2C_SMBUS_BLOCK_MAX];
	int ret;

	put_unaligned_le16(reg, addr);

	ret = i2c_smbus_write_block_data(client, KB9002_CC_FW_READ_PRIME,
					 sizeof(addr), addr);
	if (ret < 0)
		return ret;

	ret = i2c_smbus_read_block_data(client, KB9002_CC_FW_READ_DATA, rbuf);
	if (ret < 0)
		return ret;
	if (ret < (int)(sizeof(addr) + sizeof(*val)))
		return -EIO;

	*val = get_unaligned_le32(&rbuf[sizeof(addr)]);
	return 0;
}

/* Like kb9002_fw_read but for a hardware register (32-bit LE address). */
static int kb9002_smbus_hw_read(struct kb9002_data *data, u32 reg, u32 *val)
{
	struct i2c_client *client = data->client;
	u8 addr[4];
	u8 rbuf[I2C_SMBUS_BLOCK_MAX];
	int ret;

	put_unaligned_le32(reg, addr);

	ret = i2c_smbus_write_block_data(client, KB9002_CC_HW_READ_PRIME,
					 sizeof(addr), addr);
	if (ret < 0)
		return ret;

	ret = i2c_smbus_read_block_data(client, KB9002_CC_HW_READ_DATA, rbuf);
	if (ret < 0)
		return ret;
	if (ret < (int)(sizeof(addr) + sizeof(*val)))
		return -EIO;

	*val = get_unaligned_le32(&rbuf[sizeof(addr)]);
	return 0;
}

/*
 * Switch the host interface from raw-I2C to SMBus and wait for firmware
 * to come back up. Called only when SMBus access failed in probe, i.e.
 * the chip is strapped to raw-I2C mode. Confirms the revision, sets the
 * SMBus-mode bit, then polls until firmware responds again.
 */
static int kb9002_enable_smbus_target(struct kb9002_data *data)
{
	struct i2c_client *client = data->client;
	u32 revid;
	u32 val;
	int op_ret;
	int ret;

	if (!i2c_check_functionality(client->adapter, I2C_FUNC_I2C))
		return dev_err_probe(&client->dev, -ENODEV,
				     "raw I2C required to switch to SMBus mode\n");

	ret = kb9002_read_revid(client, &revid);
	if (ret)
		return dev_err_probe(&client->dev, ret,
				     "revision ID read failed\n");

	switch (FIELD_GET(KB9002_REVID_MASK, revid)) {
	case KB9002_REVID_B0:
	case KB9002_REVID_B1:
		break;
	default:
		return dev_err_probe(&client->dev, -ENODEV,
				     "unsupported revision ID 0x%08x\n", revid);
	}

	ret = kb9002_i2c_read(client, KB9002_HW_REG_HOST_IF, &val);
	if (ret)
		return dev_err_probe(&client->dev, ret,
				     "host interface read failed\n");

	val |= KB9002_HOST_IF_SMBUS;

	ret = kb9002_i2c_write(client, KB9002_HW_REG_HOST_IF, val);
	if (ret)
		return dev_err_probe(&client->dev, ret,
				     "host interface write failed\n");

	/* Wait until firmware re-initialisation completes. */
	ret = read_poll_timeout(kb9002_fw_read, op_ret, op_ret == 0,
				KB9002_FW_READY_POLL_US,
				KB9002_FW_READY_TIMEOUT_US, true,
				data, KB9002_FW_REG_VID, &val);
	if (ret)
		return dev_err_probe(&client->dev, ret,
				     "firmware not responding over SMBus\n");

	return 0;
}

/* Convert Q16.16 absolute Kelvin to millidegrees Celsius. */
static long kb9002_temp_to_milli_c(u32 raw)
{
	s64 milli_k = ((s64)raw * 1000) >> KB9002_TEMP_FRAC_BITS;

	return (long)milli_k + KB9002_ABS_ZERO_MILLI_C;
}

static int kb9002_read_temp(struct kb9002_data *data, long *val)
{
	u32 raw;
	int ret;

	ret = kb9002_fw_read(data, KB9002_FW_REG_TEMP_MAXIMUM, &raw);
	if (ret)
		return ret;

	*val = kb9002_temp_to_milli_c(raw);
	return 0;
}

static umode_t kb9002_is_visible(const void *drvdata,
				 enum hwmon_sensor_types type,
				 u32 attr, int channel)
{
	return 0444;
}

static int kb9002_read(struct device *dev, enum hwmon_sensor_types type,
		       u32 attr, int channel, long *val)
{
	struct kb9002_data *data = dev_get_drvdata(dev);

	if (type == hwmon_temp && attr == hwmon_temp_input)
		return kb9002_read_temp(data, val);

	return -EOPNOTSUPP;
}

static const struct hwmon_ops kb9002_hwmon_ops = {
	.is_visible = kb9002_is_visible,
	.read = kb9002_read,
};

static const struct hwmon_channel_info * const kb9002_hwmon_info[] = {
	HWMON_CHANNEL_INFO(temp, HWMON_T_INPUT),
	NULL,
};

static const struct hwmon_chip_info kb9002_chip_info = {
	.ops = &kb9002_hwmon_ops,
	.info = kb9002_hwmon_info,
};

static int kb9002_fw_version_show(struct seq_file *s, void *unused)
{
	struct kb9002_data *data = s->private;
	u32 ver;
	int ret;

	guard(hwmon_lock)(data->hwmon_dev);

	ret = kb9002_fw_read(data, KB9002_FW_REG_FW_VERSION, &ver);
	if (ret)
		return ret;

	seq_printf(s, "%u.%02u.%02u.%u\n",
		   (ver >> 24) & 0xff, (ver >> 16) & 0xff,
		   (ver >>  8) & 0xff, (ver >>  0) & 0xff);
	return 0;
}
DEFINE_SHOW_ATTRIBUTE(kb9002_fw_version);

static int kb9002_fw_load_status_show(struct seq_file *s, void *unused)
{
	struct kb9002_data *data = s->private;
	u32 status;
	int ret;

	guard(hwmon_lock)(data->hwmon_dev);

	ret = kb9002_smbus_hw_read(data, KB9002_HW_REG_FW_BOOT_STATUS, &status);
	if (ret)
		return ret;

	seq_printf(s, "%s\n",
		   (status >> 24) == KB9002_FW_BOOT_STATUS_OK_MSB ?
		   "normal" : "abnormal");
	return 0;
}
DEFINE_SHOW_ATTRIBUTE(kb9002_fw_load_status);

static void kb9002_debugfs_init(struct kb9002_data *data)
{
	struct dentry *dir = data->client->debugfs;

	debugfs_create_file("fw_ver", 0444, dir, data,
			    &kb9002_fw_version_fops);
	debugfs_create_file("fw_load_status", 0444, dir, data,
			    &kb9002_fw_load_status_fops);
}

static int kb9002_probe(struct i2c_client *client)
{
	struct device *dev = &client->dev;
	struct kb9002_data *data;
	struct device *hwmon_dev;
	u32 vid;
	int ret;

	if (!i2c_check_functionality(client->adapter,
				     I2C_FUNC_SMBUS_BLOCK_DATA |
				     I2C_FUNC_SMBUS_PEC))
		return -ENODEV;

	data = devm_kzalloc(dev, sizeof(*data), GFP_KERNEL);
	if (!data)
		return -ENOMEM;

	data->client = client;

	/* All firmware register accesses are PEC-protected. */
	client->flags |= I2C_CLIENT_PEC;

	i2c_set_clientdata(client, data);

	/*
	 * Try SMBus first. If the chip is strapped to raw-I2C mode it
	 * will not respond to SMBus framing, so fall back to switching
	 * the host interface over raw I2C and retry.
	 */
	ret = kb9002_fw_read(data, KB9002_FW_REG_VID, &vid);
	if (ret) {
		dev_dbg(dev, "SMBus probe failed (%d), trying raw-I2C host-interface switch\n",
			ret);
		ret = kb9002_enable_smbus_target(data);
		if (ret)
			return ret;
		ret = kb9002_fw_read(data, KB9002_FW_REG_VID, &vid);
		if (ret)
			return dev_err_probe(dev, ret,
					     "VID read failed after host-interface switch\n");
	}
	if (FIELD_GET(KB9002_VID_MASK, vid) != KB9002_VID_KANDOU)
		return dev_err_probe(dev, -ENODEV,
				     "unexpected VID 0x%08x\n", vid);

	hwmon_dev = devm_hwmon_device_register_with_info(dev, KB9002_DEV_NAME,
							 data,
							 &kb9002_chip_info,
							 NULL);
	if (IS_ERR(hwmon_dev))
		return PTR_ERR(hwmon_dev);

	data->hwmon_dev = hwmon_dev;
	kb9002_debugfs_init(data);
	return 0;
}

static const struct i2c_device_id kb9002_id[] = {
	{ .name = KB9002_DEV_NAME },
	{ }
};
MODULE_DEVICE_TABLE(i2c, kb9002_id);

static const struct of_device_id kb9002_of_match[] = {
	{ .compatible = "kandou,kb9002" },
	{ }
};
MODULE_DEVICE_TABLE(of, kb9002_of_match);

static struct i2c_driver kb9002_driver = {
	.driver = {
		.name = KB9002_DEV_NAME,
		.of_match_table = kb9002_of_match,
	},
	.probe = kb9002_probe,
	.id_table = kb9002_id,
};
module_i2c_driver(kb9002_driver);

MODULE_AUTHOR("Andy Chung <andy.chung@amd.com>");
MODULE_DESCRIPTION("Kandou KB9002 PCIe retimer hwmon driver");
MODULE_LICENSE("GPL");
