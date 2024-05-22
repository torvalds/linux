// SPDX-License-Identifier: GPL-2.0-or-later

#include <linux/debugfs.h>
#include <linux/delay.h>
#include <linux/err.h>
#include <linux/i2c.h>
#include <linux/init.h>
#include <linux/hwmon.h>
#include <linux/module.h>
#include <linux/mutex.h>

/* Aries current average temp ADC code CSR */
#define ARIES_CURRENT_AVG_TEMP_ADC_CSR	0x42c

/* Device Load check register */
#define ARIES_CODE_LOAD_REG	0x605
/* Value indicating FW was loaded properly, [3:1] = 3'b111 */
#define ARIES_LOAD_CODE	0xe

/* Main Micro Heartbeat register */
#define ARIES_MM_HEARTBEAT_ADDR	0x923

/* Reg offset to specify Address for MM assisted accesses */
#define ARIES_MM_ASSIST_REG_ADDR_OFFSET	0xd99
/* Reg offset to specify Command for MM assisted accesses */
#define ARIES_MM_ASSIST_CMD_OFFSET	0xd9d
/* Reg offset to MM SPARE 0 used specify Address[7:0] */
#define ARIES_MM_ASSIST_SPARE_0_OFFSET	0xd9f
/* Reg offset to MM SPARE 3 used specify Data Byte 0 */
#define ARIES_MM_ASSIST_SPARE_3_OFFSET	0xda2
/* Wide register reads */
#define ARIES_MM_RD_WIDE_REG_2B	0x1d
#define ARIES_MM_RD_WIDE_REG_3B	0x1e
#define ARIES_MM_RD_WIDE_REG_4B	0x1f
#define ARIES_MM_RD_WIDE_REG_5B	0x20

/* Time delay between checking MM status of EEPROM write (microseconds) */
#define ARIES_MM_STATUS_TIME	5000

/* AL Main SRAM DMEM offset (A0) */
#define AL_MAIN_SRAM_DMEM_OFFSET	(64 * 1024)
/* SRAM read command */
#define AL_TG_RD_LOC_IND_SRAM	0x16

/* Offset for main micro FW info */
#define ARIES_MAIN_MICRO_FW_INFO	(96 * 1024 - 128)
/* FW Info (Major) offset location in struct */
#define ARIES_MM_FW_VERSION_MAJOR	0
/* FW Info (Minor) offset location in struct */
#define ARIES_MM_FW_VERSION_MINOR	1
/* FW Info (Build no.) offset location in struct */
#define ARIES_MM_FW_VERSION_BUILD	2

#define ARIES_TEMP_CAL_CODE_DEFAULT	84

/* Struct defining FW version loaded on an Aries device */
struct pt5161l_fw_ver {
	u8 major;
	u8 minor;
	u16 build;
};

/* Each client has this additional data */
struct pt5161l_data {
	struct i2c_client *client;
	struct dentry *debugfs;
	struct pt5161l_fw_ver fw_ver;
	struct mutex lock; /* for atomic I2C transactions */
	bool init_done;
	bool code_load_okay; /* indicate if code load reg value is expected */
	bool mm_heartbeat_okay; /* indicate if Main Micro heartbeat is good */
	bool mm_wide_reg_access; /* MM assisted wide register access */
};

static struct dentry *pt5161l_debugfs_dir;

/*
 * Write multiple data bytes to Aries over I2C
 */
static int pt5161l_write_block_data(struct pt5161l_data *data, u32 address,
				    u8 len, u8 *val)
{
	struct i2c_client *client = data->client;
	int ret;
	u8 remain_len = len;
	u8 xfer_len, curr_len;
	u8 buf[16];
	u8 cmd = 0x0F; /* [7]:pec_en, [4:2]:func, [1]:start, [0]:end */
	u8 config = 0x40; /* [6]:cfg_type, [4:1]:burst_len, [0]:address bit16 */

	while (remain_len > 0) {
		if (remain_len > 4) {
			curr_len = 4;
			remain_len -= 4;
		} else {
			curr_len = remain_len;
			remain_len = 0;
		}

		buf[0] = config | (curr_len - 1) << 1 | ((address >> 16) & 0x1);
		buf[1] = (address >> 8) & 0xff;
		buf[2] = address & 0xff;
		memcpy(&buf[3], val, curr_len);

		xfer_len = 3 + curr_len;
		ret = i2c_smbus_write_block_data(client, cmd, xfer_len, buf);
		if (ret)
			return ret;

		val += curr_len;
		address += curr_len;
	}

	return 0;
}

/*
 * Read multiple data bytes from Aries over I2C
 */
static int pt5161l_read_block_data(struct pt5161l_data *data, u32 address,
				   u8 len, u8 *val)
{
	struct i2c_client *client = data->client;
	int ret, tries;
	u8 remain_len = len;
	u8 curr_len;
	u8 wbuf[16], rbuf[24];
	u8 cmd = 0x08; /* [7]:pec_en, [4:2]:func, [1]:start, [0]:end */
	u8 config = 0x00; /* [6]:cfg_type, [4:1]:burst_len, [0]:address bit16 */

	while (remain_len > 0) {
		if (remain_len > 16) {
			curr_len = 16;
			remain_len -= 16;
		} else {
			curr_len = remain_len;
			remain_len = 0;
		}

		wbuf[0] = config | (curr_len - 1) << 1 |
			  ((address >> 16) & 0x1);
		wbuf[1] = (address >> 8) & 0xff;
		wbuf[2] = address & 0xff;

		for (tries = 0; tries < 3; tries++) {
			ret = i2c_smbus_write_block_data(client, (cmd | 0x2), 3,
							 wbuf);
			if (ret)
				return ret;

			ret = i2c_smbus_read_block_data(client, (cmd | 0x1),
							rbuf);
			if (ret == curr_len)
				break;
		}
		if (tries >= 3)
			return ret;

		memcpy(val, rbuf, curr_len);
		val += curr_len;
		address += curr_len;
	}

	return 0;
}

static int pt5161l_read_wide_reg(struct pt5161l_data *data, u32 address,
				 u8 width, u8 *val)
{
	int ret, tries;
	u8 buf[8];
	u8 status;

	/*
	 * Safely access wide registers using mailbox method to prevent
	 * risking conflict with Aries firmware; otherwise fallback to
	 * legacy, less secure method.
	 */
	if (data->mm_wide_reg_access) {
		buf[0] = address & 0xff;
		buf[1] = (address >> 8) & 0xff;
		buf[2] = (address >> 16) & 0x1;
		ret = pt5161l_write_block_data(data,
					       ARIES_MM_ASSIST_SPARE_0_OFFSET,
					       3, buf);
		if (ret)
			return ret;

		/* Set command based on width */
		switch (width) {
		case 2:
			buf[0] = ARIES_MM_RD_WIDE_REG_2B;
			break;
		case 3:
			buf[0] = ARIES_MM_RD_WIDE_REG_3B;
			break;
		case 4:
			buf[0] = ARIES_MM_RD_WIDE_REG_4B;
			break;
		case 5:
			buf[0] = ARIES_MM_RD_WIDE_REG_5B;
			break;
		default:
			return -EINVAL;
		}
		ret = pt5161l_write_block_data(data, ARIES_MM_ASSIST_CMD_OFFSET,
					       1, buf);
		if (ret)
			return ret;

		status = 0xff;
		for (tries = 0; tries < 100; tries++) {
			ret = pt5161l_read_block_data(data,
						      ARIES_MM_ASSIST_CMD_OFFSET,
						      1, &status);
			if (ret)
				return ret;

			if (status == 0)
				break;

			usleep_range(ARIES_MM_STATUS_TIME,
				     ARIES_MM_STATUS_TIME + 1000);
		}
		if (status != 0)
			return -ETIMEDOUT;

		ret = pt5161l_read_block_data(data,
					      ARIES_MM_ASSIST_SPARE_3_OFFSET,
					      width, val);
		if (ret)
			return ret;
	} else {
		return pt5161l_read_block_data(data, address, width, val);
	}

	return 0;
}

/*
 * Read multiple (up to eight) data bytes from micro SRAM over I2C
 */
static int
pt5161l_read_block_data_main_micro_indirect(struct pt5161l_data *data,
					    u32 address, u8 len, u8 *val)
{
	int ret, tries;
	u8 buf[8];
	u8 i, status;
	u32 uind_offs = ARIES_MM_ASSIST_REG_ADDR_OFFSET;
	u32 eeprom_base, eeprom_addr;

	/* No multi-byte indirect support here. Hence read a byte at a time */
	eeprom_base = address - AL_MAIN_SRAM_DMEM_OFFSET;
	for (i = 0; i < len; i++) {
		eeprom_addr = eeprom_base + i;
		buf[0] = eeprom_addr & 0xff;
		buf[1] = (eeprom_addr >> 8) & 0xff;
		buf[2] = (eeprom_addr >> 16) & 0xff;
		ret = pt5161l_write_block_data(data, uind_offs, 3, buf);
		if (ret)
			return ret;

		buf[0] = AL_TG_RD_LOC_IND_SRAM;
		ret = pt5161l_write_block_data(data, uind_offs + 4, 1, buf);
		if (ret)
			return ret;

		status = 0xff;
		for (tries = 0; tries < 255; tries++) {
			ret = pt5161l_read_block_data(data, uind_offs + 4, 1,
						      &status);
			if (ret)
				return ret;

			if (status == 0)
				break;
		}
		if (status != 0)
			return -ETIMEDOUT;

		ret = pt5161l_read_block_data(data, uind_offs + 3, 1, buf);
		if (ret)
			return ret;

		val[i] = buf[0];
	}

	return 0;
}

/*
 * Check firmware load status
 */
static int pt5161l_fw_load_check(struct pt5161l_data *data)
{
	int ret;
	u8 buf[8];

	ret = pt5161l_read_block_data(data, ARIES_CODE_LOAD_REG, 1, buf);
	if (ret)
		return ret;

	if (buf[0] < ARIES_LOAD_CODE) {
		dev_dbg(&data->client->dev,
			"Code Load reg unexpected. Not all modules are loaded %x\n",
			buf[0]);
		data->code_load_okay = false;
	} else {
		data->code_load_okay = true;
	}

	return 0;
}

/*
 * Check main micro heartbeat
 */
static int pt5161l_heartbeat_check(struct pt5161l_data *data)
{
	int ret, tries;
	u8 buf[8];
	u8 heartbeat;
	bool hb_changed = false;

	ret = pt5161l_read_block_data(data, ARIES_MM_HEARTBEAT_ADDR, 1, buf);
	if (ret)
		return ret;

	heartbeat = buf[0];
	for (tries = 0; tries < 100; tries++) {
		ret = pt5161l_read_block_data(data, ARIES_MM_HEARTBEAT_ADDR, 1,
					      buf);
		if (ret)
			return ret;

		if (buf[0] != heartbeat) {
			hb_changed = true;
			break;
		}
	}
	data->mm_heartbeat_okay = hb_changed;

	return 0;
}

/*
 * Check the status of firmware
 */
static int pt5161l_fwsts_check(struct pt5161l_data *data)
{
	int ret;
	u8 buf[8];
	u8 major = 0, minor = 0;
	u16 build = 0;

	ret = pt5161l_fw_load_check(data);
	if (ret)
		return ret;

	ret = pt5161l_heartbeat_check(data);
	if (ret)
		return ret;

	if (data->code_load_okay && data->mm_heartbeat_okay) {
		ret = pt5161l_read_block_data_main_micro_indirect(data, ARIES_MAIN_MICRO_FW_INFO +
								  ARIES_MM_FW_VERSION_MAJOR,
								  1, &major);
		if (ret)
			return ret;

		ret = pt5161l_read_block_data_main_micro_indirect(data, ARIES_MAIN_MICRO_FW_INFO +
								  ARIES_MM_FW_VERSION_MINOR,
								  1, &minor);
		if (ret)
			return ret;

		ret = pt5161l_read_block_data_main_micro_indirect(data, ARIES_MAIN_MICRO_FW_INFO +
								  ARIES_MM_FW_VERSION_BUILD,
								  2, buf);
		if (ret)
			return ret;
		build = buf[1] << 8 | buf[0];
	}
	data->fw_ver.major = major;
	data->fw_ver.minor = minor;
	data->fw_ver.build = build;

	return 0;
}

static int pt5161l_fw_is_at_least(struct pt5161l_data *data, u8 major, u8 minor,
				  u16 build)
{
	u32 ver = major << 24 | minor << 16 | build;
	u32 curr_ver = data->fw_ver.major << 24 | data->fw_ver.minor << 16 |
		       data->fw_ver.build;

	if (curr_ver >= ver)
		return true;

	return false;
}

static int pt5161l_init_dev(struct pt5161l_data *data)
{
	int ret;

	mutex_lock(&data->lock);
	ret = pt5161l_fwsts_check(data);
	mutex_unlock(&data->lock);
	if (ret)
		return ret;

	/* Firmware 2.2.0 enables safe access to wide registers */
	if (pt5161l_fw_is_at_least(data, 2, 2, 0))
		data->mm_wide_reg_access = true;

	data->init_done = true;

	return 0;
}

static int pt5161l_read(struct device *dev, enum hwmon_sensor_types type,
			u32 attr, int channel, long *val)
{
	struct pt5161l_data *data = dev_get_drvdata(dev);
	int ret;
	u8 buf[8];
	long adc_code;

	switch (attr) {
	case hwmon_temp_input:
		if (!data->init_done) {
			ret = pt5161l_init_dev(data);
			if (ret)
				return ret;
		}

		mutex_lock(&data->lock);
		ret = pt5161l_read_wide_reg(data,
					    ARIES_CURRENT_AVG_TEMP_ADC_CSR, 4,
					    buf);
		mutex_unlock(&data->lock);
		if (ret) {
			dev_dbg(dev, "Read adc_code failed %d\n", ret);
			return ret;
		}

		adc_code = buf[3] << 24 | buf[2] << 16 | buf[1] << 8 | buf[0];
		if (adc_code == 0 || adc_code >= 0x3ff) {
			dev_dbg(dev, "Invalid adc_code %lx\n", adc_code);
			return -EIO;
		}

		*val = 110000 +
		       ((adc_code - (ARIES_TEMP_CAL_CODE_DEFAULT + 250)) *
			-320);
		break;
	default:
		return -EOPNOTSUPP;
	}

	return 0;
}

static umode_t pt5161l_is_visible(const void *data,
				  enum hwmon_sensor_types type, u32 attr,
				  int channel)
{
	switch (attr) {
	case hwmon_temp_input:
		return 0444;
	default:
		break;
	}

	return 0;
}

static const struct hwmon_channel_info *pt5161l_info[] = {
	HWMON_CHANNEL_INFO(temp, HWMON_T_INPUT),
	NULL
};

static const struct hwmon_ops pt5161l_hwmon_ops = {
	.is_visible = pt5161l_is_visible,
	.read = pt5161l_read,
};

static const struct hwmon_chip_info pt5161l_chip_info = {
	.ops = &pt5161l_hwmon_ops,
	.info = pt5161l_info,
};

static ssize_t pt5161l_debugfs_read_fw_ver(struct file *file, char __user *buf,
					   size_t count, loff_t *ppos)
{
	struct pt5161l_data *data = file->private_data;
	int ret;
	char ver[32];

	mutex_lock(&data->lock);
	ret = pt5161l_fwsts_check(data);
	mutex_unlock(&data->lock);
	if (ret)
		return ret;

	ret = snprintf(ver, sizeof(ver), "%u.%u.%u\n", data->fw_ver.major,
		       data->fw_ver.minor, data->fw_ver.build);

	return simple_read_from_buffer(buf, count, ppos, ver, ret);
}

static const struct file_operations pt5161l_debugfs_ops_fw_ver = {
	.read = pt5161l_debugfs_read_fw_ver,
	.open = simple_open,
};

static ssize_t pt5161l_debugfs_read_fw_load_sts(struct file *file,
						char __user *buf, size_t count,
						loff_t *ppos)
{
	struct pt5161l_data *data = file->private_data;
	int ret;
	bool status = false;
	char health[16];

	mutex_lock(&data->lock);
	ret = pt5161l_fw_load_check(data);
	mutex_unlock(&data->lock);
	if (ret == 0)
		status = data->code_load_okay;

	ret = snprintf(health, sizeof(health), "%s\n",
		       status ? "normal" : "abnormal");

	return simple_read_from_buffer(buf, count, ppos, health, ret);
}

static const struct file_operations pt5161l_debugfs_ops_fw_load_sts = {
	.read = pt5161l_debugfs_read_fw_load_sts,
	.open = simple_open,
};

static ssize_t pt5161l_debugfs_read_hb_sts(struct file *file, char __user *buf,
					   size_t count, loff_t *ppos)
{
	struct pt5161l_data *data = file->private_data;
	int ret;
	bool status = false;
	char health[16];

	mutex_lock(&data->lock);
	ret = pt5161l_heartbeat_check(data);
	mutex_unlock(&data->lock);
	if (ret == 0)
		status = data->mm_heartbeat_okay;

	ret = snprintf(health, sizeof(health), "%s\n",
		       status ? "normal" : "abnormal");

	return simple_read_from_buffer(buf, count, ppos, health, ret);
}

static const struct file_operations pt5161l_debugfs_ops_hb_sts = {
	.read = pt5161l_debugfs_read_hb_sts,
	.open = simple_open,
};

static int pt5161l_init_debugfs(struct pt5161l_data *data)
{
	data->debugfs = debugfs_create_dir(dev_name(&data->client->dev),
					   pt5161l_debugfs_dir);

	debugfs_create_file("fw_ver", 0444, data->debugfs, data,
			    &pt5161l_debugfs_ops_fw_ver);

	debugfs_create_file("fw_load_status", 0444, data->debugfs, data,
			    &pt5161l_debugfs_ops_fw_load_sts);

	debugfs_create_file("heartbeat_status", 0444, data->debugfs, data,
			    &pt5161l_debugfs_ops_hb_sts);

	return 0;
}

static int pt5161l_probe(struct i2c_client *client)
{
	struct device *dev = &client->dev;
	struct device *hwmon_dev;
	struct pt5161l_data *data;

	data = devm_kzalloc(dev, sizeof(*data), GFP_KERNEL);
	if (!data)
		return -ENOMEM;

	data->client = client;
	mutex_init(&data->lock);
	pt5161l_init_dev(data);
	dev_set_drvdata(dev, data);

	hwmon_dev = devm_hwmon_device_register_with_info(dev, client->name,
							 data,
							 &pt5161l_chip_info,
							 NULL);

	pt5161l_init_debugfs(data);

	return PTR_ERR_OR_ZERO(hwmon_dev);
}

static void pt5161l_remove(struct i2c_client *client)
{
	struct pt5161l_data *data = i2c_get_clientdata(client);

	debugfs_remove_recursive(data->debugfs);
}

static const struct of_device_id __maybe_unused pt5161l_of_match[] = {
	{ .compatible = "asteralabs,pt5161l" },
	{},
};
MODULE_DEVICE_TABLE(of, pt5161l_of_match);

static const struct acpi_device_id __maybe_unused pt5161l_acpi_match[] = {
	{ "PT5161L", 0 },
	{},
};
MODULE_DEVICE_TABLE(acpi, pt5161l_acpi_match);

static const struct i2c_device_id pt5161l_id[] = {
	{ "pt5161l" },
	{}
};
MODULE_DEVICE_TABLE(i2c, pt5161l_id);

static struct i2c_driver pt5161l_driver = {
	.class = I2C_CLASS_HWMON,
	.driver = {
		.name = "pt5161l",
		.of_match_table = of_match_ptr(pt5161l_of_match),
		.acpi_match_table = ACPI_PTR(pt5161l_acpi_match),
	},
	.probe = pt5161l_probe,
	.remove = pt5161l_remove,
	.id_table = pt5161l_id,
};

static int __init pt5161l_init(void)
{
	pt5161l_debugfs_dir = debugfs_create_dir("pt5161l", NULL);
	return i2c_add_driver(&pt5161l_driver);
}

static void __exit pt5161l_exit(void)
{
	i2c_del_driver(&pt5161l_driver);
	debugfs_remove_recursive(pt5161l_debugfs_dir);
}

module_init(pt5161l_init);
module_exit(pt5161l_exit);

MODULE_AUTHOR("Cosmo Chou <cosmo.chou@quantatw.com>");
MODULE_DESCRIPTION("Hwmon driver for Astera Labs Aries PCIe retimer");
MODULE_LICENSE("GPL");
