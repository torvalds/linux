// SPDX-License-Identifier: GPL-2.0
/*
 * CZ.NIC's Turris Omnia MCU driver
 *
 * 2024 by Marek Behún <kabel@kernel.org>
 */

#include <linux/array_size.h>
#include <linux/bits.h>
#include <linux/device.h>
#include <linux/errno.h>
#include <linux/hex.h>
#include <linux/i2c.h>
#include <linux/module.h>
#include <linux/string.h>
#include <linux/sysfs.h>
#include <linux/types.h>

#include <linux/turris-omnia-mcu-interface.h>
#include "turris-omnia-mcu.h"

#define OMNIA_FW_VERSION_LEN		20
#define OMNIA_FW_VERSION_HEX_LEN	(2 * OMNIA_FW_VERSION_LEN + 1)
#define OMNIA_BOARD_INFO_LEN		16

int omnia_cmd_write_read(const struct i2c_client *client,
			 void *cmd, unsigned int cmd_len,
			 void *reply, unsigned int reply_len)
{
	struct i2c_msg msgs[2];
	int ret, num;

	msgs[0].addr = client->addr;
	msgs[0].flags = 0;
	msgs[0].len = cmd_len;
	msgs[0].buf = cmd;
	num = 1;

	if (reply_len) {
		msgs[1].addr = client->addr;
		msgs[1].flags = I2C_M_RD;
		msgs[1].len = reply_len;
		msgs[1].buf = reply;
		num++;
	}

	ret = i2c_transfer(client->adapter, msgs, num);
	if (ret < 0)
		return ret;
	if (ret != num)
		return -EIO;

	return 0;
}
EXPORT_SYMBOL_GPL(omnia_cmd_write_read);

static int omnia_get_version_hash(struct omnia_mcu *mcu, bool bootloader,
				  char version[static OMNIA_FW_VERSION_HEX_LEN])
{
	u8 reply[OMNIA_FW_VERSION_LEN];
	char *p;
	int err;

	err = omnia_cmd_read(mcu->client,
			     bootloader ? OMNIA_CMD_GET_FW_VERSION_BOOT
					: OMNIA_CMD_GET_FW_VERSION_APP,
			     reply, sizeof(reply));
	if (err)
		return err;

	p = bin2hex(version, reply, OMNIA_FW_VERSION_LEN);
	*p = '\0';

	return 0;
}

static ssize_t fw_version_hash_show(struct device *dev, char *buf,
				    bool bootloader)
{
	struct omnia_mcu *mcu = dev_get_drvdata(dev);
	char version[OMNIA_FW_VERSION_HEX_LEN];
	int err;

	err = omnia_get_version_hash(mcu, bootloader, version);
	if (err)
		return err;

	return sysfs_emit(buf, "%s\n", version);
}

static ssize_t fw_version_hash_application_show(struct device *dev,
						struct device_attribute *a,
						char *buf)
{
	return fw_version_hash_show(dev, buf, false);
}
static DEVICE_ATTR_RO(fw_version_hash_application);

static ssize_t fw_version_hash_bootloader_show(struct device *dev,
					       struct device_attribute *a,
					       char *buf)
{
	return fw_version_hash_show(dev, buf, true);
}
static DEVICE_ATTR_RO(fw_version_hash_bootloader);

static ssize_t fw_features_show(struct device *dev, struct device_attribute *a,
				char *buf)
{
	struct omnia_mcu *mcu = dev_get_drvdata(dev);

	return sysfs_emit(buf, "0x%x\n", mcu->features);
}
static DEVICE_ATTR_RO(fw_features);

static ssize_t mcu_type_show(struct device *dev, struct device_attribute *a,
			     char *buf)
{
	struct omnia_mcu *mcu = dev_get_drvdata(dev);

	return sysfs_emit(buf, "%s\n", mcu->type);
}
static DEVICE_ATTR_RO(mcu_type);

static ssize_t reset_selector_show(struct device *dev,
				   struct device_attribute *a, char *buf)
{
	u8 reply;
	int err;

	err = omnia_cmd_read_u8(to_i2c_client(dev), OMNIA_CMD_GET_RESET,
				&reply);
	if (err)
		return err;

	return sysfs_emit(buf, "%d\n", reply);
}
static DEVICE_ATTR_RO(reset_selector);

static ssize_t serial_number_show(struct device *dev,
				  struct device_attribute *a, char *buf)
{
	struct omnia_mcu *mcu = dev_get_drvdata(dev);

	return sysfs_emit(buf, "%016llX\n", mcu->board_serial_number);
}
static DEVICE_ATTR_RO(serial_number);

static ssize_t first_mac_address_show(struct device *dev,
				      struct device_attribute *a, char *buf)
{
	struct omnia_mcu *mcu = dev_get_drvdata(dev);

	return sysfs_emit(buf, "%pM\n", mcu->board_first_mac);
}
static DEVICE_ATTR_RO(first_mac_address);

static ssize_t board_revision_show(struct device *dev,
				   struct device_attribute *a, char *buf)
{
	struct omnia_mcu *mcu = dev_get_drvdata(dev);

	return sysfs_emit(buf, "%u\n", mcu->board_revision);
}
static DEVICE_ATTR_RO(board_revision);

static struct attribute *omnia_mcu_base_attrs[] = {
	&dev_attr_fw_version_hash_application.attr,
	&dev_attr_fw_version_hash_bootloader.attr,
	&dev_attr_fw_features.attr,
	&dev_attr_mcu_type.attr,
	&dev_attr_reset_selector.attr,
	&dev_attr_serial_number.attr,
	&dev_attr_first_mac_address.attr,
	&dev_attr_board_revision.attr,
	NULL
};

static umode_t omnia_mcu_base_attrs_visible(struct kobject *kobj,
					    struct attribute *a, int n)
{
	struct device *dev = kobj_to_dev(kobj);
	struct omnia_mcu *mcu = dev_get_drvdata(dev);

	if ((a == &dev_attr_serial_number.attr ||
	     a == &dev_attr_first_mac_address.attr ||
	     a == &dev_attr_board_revision.attr) &&
	    !(mcu->features & OMNIA_FEAT_BOARD_INFO))
		return 0;

	return a->mode;
}

static const struct attribute_group omnia_mcu_base_group = {
	.attrs = omnia_mcu_base_attrs,
	.is_visible = omnia_mcu_base_attrs_visible,
};

static const struct attribute_group *omnia_mcu_groups[] = {
	&omnia_mcu_base_group,
#ifdef CONFIG_TURRIS_OMNIA_MCU_GPIO
	&omnia_mcu_gpio_group,
#endif
#ifdef CONFIG_TURRIS_OMNIA_MCU_SYSOFF_WAKEUP
	&omnia_mcu_poweroff_group,
#endif
	NULL
};

static void omnia_mcu_print_version_hash(struct omnia_mcu *mcu, bool bootloader)
{
	const char *type = bootloader ? "bootloader" : "application";
	struct device *dev = &mcu->client->dev;
	char version[OMNIA_FW_VERSION_HEX_LEN];
	int err;

	err = omnia_get_version_hash(mcu, bootloader, version);
	if (err) {
		dev_err(dev, "Cannot read MCU %s firmware version: %d\n",
			type, err);
		return;
	}

	dev_info(dev, "MCU %s firmware version hash: %s\n", type, version);
}

static const char *omnia_status_to_mcu_type(u16 status)
{
	switch (status & OMNIA_STS_MCU_TYPE_MASK) {
	case OMNIA_STS_MCU_TYPE_STM32:
		return "STM32";
	case OMNIA_STS_MCU_TYPE_GD32:
		return "GD32";
	case OMNIA_STS_MCU_TYPE_MKL:
		return "MKL";
	default:
		return "unknown";
	}
}

static void omnia_info_missing_feature(struct device *dev, const char *feature)
{
	dev_info(dev,
		 "Your board's MCU firmware does not support the %s feature.\n",
		 feature);
}

static int omnia_mcu_read_features(struct omnia_mcu *mcu)
{
	static const struct {
		u16 mask;
		const char *name;
	} features[] = {
#define _DEF_FEAT(_n, _m) { OMNIA_FEAT_ ## _n, _m }
		_DEF_FEAT(EXT_CMDS,		"extended control and status"),
		_DEF_FEAT(WDT_PING,		"watchdog pinging"),
		_DEF_FEAT(LED_STATE_EXT_MASK,	"peripheral LED pins reading"),
		_DEF_FEAT(NEW_INT_API,		"new interrupt API"),
		_DEF_FEAT(POWEROFF_WAKEUP,	"poweroff and wakeup"),
		_DEF_FEAT(TRNG,			"true random number generator"),
		_DEF_FEAT(BRIGHTNESS_INT,	"LED panel brightness change interrupt"),
		_DEF_FEAT(LED_GAMMA_CORRECTION,	"LED gamma correction"),
#undef _DEF_FEAT
	};
	struct i2c_client *client = mcu->client;
	struct device *dev = &client->dev;
	bool suggest_fw_upgrade = false;
	u16 status;
	int err;

	/* status word holds MCU type, which we need below */
	err = omnia_cmd_read_u16(client, OMNIA_CMD_GET_STATUS_WORD, &status);
	if (err)
		return err;

	/*
	 * Check whether MCU firmware supports the OMNIA_CMD_GET_FEATURES
	 * command.
	 */
	if (status & OMNIA_STS_FEATURES_SUPPORTED) {
		/* try read 32-bit features */
		err = omnia_cmd_read_u32(client, OMNIA_CMD_GET_FEATURES,
					 &mcu->features);
		if (err) {
			/* try read 16-bit features */
			u16 features16;

			err = omnia_cmd_read_u16(client, OMNIA_CMD_GET_FEATURES,
						 &features16);
			if (err)
				return err;

			mcu->features = features16;
		} else {
			if (mcu->features & OMNIA_FEAT_FROM_BIT_16_INVALID)
				mcu->features &= GENMASK(15, 0);
		}
	} else {
		dev_info(dev,
			 "Your board's MCU firmware does not support feature reading.\n");
		suggest_fw_upgrade = true;
	}

	mcu->type = omnia_status_to_mcu_type(status);
	dev_info(dev, "MCU type %s%s\n", mcu->type,
		 (mcu->features & OMNIA_FEAT_PERIPH_MCU) ?
			", with peripheral resets wired" : "");

	omnia_mcu_print_version_hash(mcu, true);

	if (mcu->features & OMNIA_FEAT_BOOTLOADER)
		dev_warn(dev,
			 "MCU is running bootloader firmware. Was firmware upgrade interrupted?\n");
	else
		omnia_mcu_print_version_hash(mcu, false);

	for (unsigned int i = 0; i < ARRAY_SIZE(features); i++) {
		if (mcu->features & features[i].mask)
			continue;

		omnia_info_missing_feature(dev, features[i].name);
		suggest_fw_upgrade = true;
	}

	if (suggest_fw_upgrade)
		dev_info(dev,
			 "Consider upgrading MCU firmware with the omnia-mcutool utility.\n");

	return 0;
}

static int omnia_mcu_read_board_info(struct omnia_mcu *mcu)
{
	u8 reply[1 + OMNIA_BOARD_INFO_LEN];
	int err;

	err = omnia_cmd_read(mcu->client, OMNIA_CMD_BOARD_INFO_GET, reply,
			     sizeof(reply));
	if (err)
		return err;

	if (reply[0] != OMNIA_BOARD_INFO_LEN)
		return -EIO;

	mcu->board_serial_number = get_unaligned_le64(&reply[1]);

	/* we can't use ether_addr_copy() because reply is not u16-aligned */
	memcpy(mcu->board_first_mac, &reply[9], sizeof(mcu->board_first_mac));

	mcu->board_revision = reply[15];

	return 0;
}

static int omnia_mcu_probe(struct i2c_client *client)
{
	struct device *dev = &client->dev;
	struct omnia_mcu *mcu;
	int err;

	if (!client->irq)
		return dev_err_probe(dev, -EINVAL, "IRQ resource not found\n");

	mcu = devm_kzalloc(dev, sizeof(*mcu), GFP_KERNEL);
	if (!mcu)
		return -ENOMEM;

	mcu->client = client;
	i2c_set_clientdata(client, mcu);

	err = omnia_mcu_read_features(mcu);
	if (err)
		return dev_err_probe(dev, err,
				     "Cannot determine MCU supported features\n");

	if (mcu->features & OMNIA_FEAT_BOARD_INFO) {
		err = omnia_mcu_read_board_info(mcu);
		if (err)
			return dev_err_probe(dev, err,
					     "Cannot read board info\n");
	}

	err = omnia_mcu_register_sys_off_and_wakeup(mcu);
	if (err)
		return err;

	err = omnia_mcu_register_watchdog(mcu);
	if (err)
		return err;

	err = omnia_mcu_register_gpiochip(mcu);
	if (err)
		return err;

	return omnia_mcu_register_trng(mcu);
}

static const struct of_device_id of_omnia_mcu_match[] = {
	{ .compatible = "cznic,turris-omnia-mcu" },
	{}
};

static struct i2c_driver omnia_mcu_driver = {
	.probe		= omnia_mcu_probe,
	.driver		= {
		.name	= "turris-omnia-mcu",
		.of_match_table = of_omnia_mcu_match,
		.dev_groups = omnia_mcu_groups,
	},
};
module_i2c_driver(omnia_mcu_driver);

MODULE_AUTHOR("Marek Behun <kabel@kernel.org>");
MODULE_DESCRIPTION("CZ.NIC's Turris Omnia MCU");
MODULE_LICENSE("GPL");
