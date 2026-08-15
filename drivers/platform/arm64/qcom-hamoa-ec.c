// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2024 Maya Matuszczyk <maccraft123mc@gmail.com>
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 */

#include <linux/bitfield.h>
#include <linux/bits.h>
#include <linux/device.h>
#include <linux/dev_printk.h>
#include <linux/err.h>
#include <linux/i2c.h>
#include <linux/interrupt.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/pm.h>
#include <linux/slab.h>
#include <linux/thermal.h>

#define EC_SCI_EVT_READ_CMD	0x05
#define EC_FW_VERSION_CMD	0x0e
#define EC_MODERN_STANDBY_CMD	0x23
#define EC_FAN_DBG_CONTROL_CMD	0x30
#define EC_SCI_EVT_CONTROL_CMD	0x35
#define EC_THERMAL_CAP_CMD	0x42

#define EC_FW_VERSION_RESP_LEN	4
#define EC_THERMAL_CAP_RESP_LEN	3
#define EC_FAN_DEBUG_CMD_LEN	6
#define EC_FAN_SPEED_DATA_SIZE	4

#define EC_MODERN_STANDBY_ENTER	0x01
#define EC_MODERN_STANDBY_EXIT	0x00

#define EC_FAN_DEBUG_MODE_OFF   0
#define EC_FAN_DEBUG_MODE_ON    BIT(0)
#define EC_FAN_ON               BIT(1)
#define EC_FAN_DEBUG_TYPE_PWM   BIT(2)
#define EC_MAX_FAN_CNT		2
#define EC_FAN_NAME_SIZE	20
#define EC_FAN_MAX_PWM		255

enum qcom_ec_sci_events {
	EC_FAN1_STATUS_CHANGE_EVT = 0x30,
	EC_FAN2_STATUS_CHANGE_EVT,
	EC_FAN1_SPEED_CHANGE_EVT,
	EC_FAN2_SPEED_CHANGE_EVT,
	EC_NEW_LUT_SET_EVT,
	EC_FAN_PROFILE_SWITCH_EVT,
	EC_THERMISTOR_1_THRESHOLD_CROSS_EVT,
	EC_THERMISTOR_2_THRESHOLD_CROSS_EVT,
	EC_THERMISTOR_3_THRESHOLD_CROSS_EVT,
	/* Reserved: 0x39 - 0x3c/0x3f */
	EC_RECOVERED_FROM_RESET_EVT = 0x3d,
};

struct qcom_ec_version {
	u8 main_version;
	u8 sub_version;
	u8 test_version;
};

struct qcom_ec_thermal_cap {
#define EC_THERMAL_FAN_CNT(x)		(FIELD_GET(GENMASK(1, 0), (x)))
#define EC_THERMAL_FAN_TYPE(x)		(FIELD_GET(GENMASK(4, 2), (x)))
#define EC_THERMAL_THERMISTOR_MASK(x)	(FIELD_GET(GENMASK(7, 0), (x)))
	u8 fan_cnt;
	u8 fan_type;
	u8 thermistor_mask;
};

struct qcom_ec_cooling_dev {
	struct thermal_cooling_device *cdev;
	struct device *parent_dev;
	u8 fan_id;
	u8 state;
};

struct qcom_ec {
	struct qcom_ec_cooling_dev *ec_cdev;
	struct qcom_ec_thermal_cap thermal_cap;
	struct qcom_ec_version version;
	struct i2c_client *client;
};

static int qcom_ec_read(struct qcom_ec *ec, u8 cmd, u8 resp_len, u8 *resp)
{
	int ret;

	ret = i2c_smbus_read_i2c_block_data(ec->client, cmd, resp_len, resp);
	if (ret < 0)
		return ret;
	else if (ret == 0 || ret == 0xff)
		return -EOPNOTSUPP;

	if (resp[0] >= resp_len)
		return -EINVAL;

	return 0;
}

/*
 * EC Device Firmware Version:
 *
 * Read Response:
 * ----------------------------------------------------------------------
 * | Offset	| Name		| Description				|
 * ----------------------------------------------------------------------
 * | 0x00	| Byte count	| Number of bytes in response		|
 * |		|		| (excluding byte count)		|
 * ----------------------------------------------------------------------
 * | 0x01	| Test-version	| Test-version of EC firmware		|
 * ----------------------------------------------------------------------
 * | 0x02	| Sub-version	| Sub-version of EC firmware		|
 * ----------------------------------------------------------------------
 * | 0x03	| Main-version	| Main-version of EC firmware		|
 * ----------------------------------------------------------------------
 *
 */
static int qcom_ec_read_fw_version(struct device *dev)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct qcom_ec *ec = i2c_get_clientdata(client);
	struct qcom_ec_version *version = &ec->version;
	u8 resp[EC_FW_VERSION_RESP_LEN];
	int ret;

	ret = qcom_ec_read(ec, EC_FW_VERSION_CMD, EC_FW_VERSION_RESP_LEN, resp);
	if (ret < 0)
		return ret;

	version->main_version = resp[3];
	version->sub_version = resp[2];
	version->test_version = resp[1];

	dev_dbg(dev, "EC Version %d.%d.%d\n",
		version->main_version, version->sub_version, version->test_version);

	return 0;
}

/*
 * EC Device Thermal Capabilities:
 *
 * Read Response:
 * ------------------------------------------------------------------------------
 * | Offset		| Name		| Description				|
 * ------------------------------------------------------------------------------
 * | 0x00		| Byte count	| Number of bytes in response		|
 * |			|		| (excluding byte count)		|
 * ------------------------------------------------------------------------------
 * | 0x02 (LSB)		| EC Thermal	| Bit 0-1: Number of fans		|
 * | 0x03		| Capabilities	| Bit 2-4: Type of fan			|
 * |			|		| Bit 5-6: Reserved			|
 * |			|		| Bit 7: Data Valid/Invalid		|
 * |			|		|	 (Valid - 1, Invalid - 0)	|
 * |			|		| Bit 8-15: Thermistor 0 - 7 presence	|
 * |			|		|	    (1 present, 0 absent)	|
 * ------------------------------------------------------------------------------
 *
 */
static int qcom_ec_thermal_capabilities(struct device *dev)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct qcom_ec *ec = i2c_get_clientdata(client);
	struct qcom_ec_thermal_cap *cap = &ec->thermal_cap;
	u8 resp[EC_THERMAL_CAP_RESP_LEN];
	int ret;

	ret = qcom_ec_read(ec, EC_THERMAL_CAP_CMD, EC_THERMAL_CAP_RESP_LEN, resp);
	if (ret < 0)
		return ret;

	cap->fan_cnt = min(EC_MAX_FAN_CNT, EC_THERMAL_FAN_CNT(resp[1]));
	cap->fan_type = EC_THERMAL_FAN_TYPE(resp[1]);
	cap->thermistor_mask = EC_THERMAL_THERMISTOR_MASK(resp[2]);

	dev_dbg(dev, "Fan count: %d Fan Type: %d Thermistor Mask: %x\n",
		cap->fan_cnt, cap->fan_type, cap->thermistor_mask);

	return 0;
}

static irqreturn_t qcom_ec_irq(int irq, void *data)
{
	struct qcom_ec *ec = data;
	struct device *dev = &ec->client->dev;
	int val;

	val = i2c_smbus_read_byte_data(ec->client, EC_SCI_EVT_READ_CMD);
	if (val < 0) {
		dev_err_ratelimited(dev, "Failed to read EC SCI Event: %d\n", val);
		return IRQ_HANDLED;
	}

	switch (val) {
	case EC_FAN1_STATUS_CHANGE_EVT:
		dev_dbg_ratelimited(dev, "Fan1 status changed\n");
		break;
	case EC_FAN2_STATUS_CHANGE_EVT:
		dev_dbg_ratelimited(dev, "Fan2 status changed\n");
		break;
	case EC_FAN1_SPEED_CHANGE_EVT:
		dev_dbg_ratelimited(dev, "Fan1 speed crossed low/high trip point\n");
		break;
	case EC_FAN2_SPEED_CHANGE_EVT:
		dev_dbg_ratelimited(dev, "Fan2 speed crossed low/high trip point\n");
		break;
	case EC_NEW_LUT_SET_EVT:
		dev_dbg_ratelimited(dev, "New LUT set\n");
		break;
	case EC_FAN_PROFILE_SWITCH_EVT:
		dev_dbg_ratelimited(dev, "FAN Profile switched\n");
		break;
	case EC_THERMISTOR_1_THRESHOLD_CROSS_EVT:
		dev_dbg_ratelimited(dev, "Thermistor 1 threshold crossed\n");
		break;
	case EC_THERMISTOR_2_THRESHOLD_CROSS_EVT:
		dev_dbg_ratelimited(dev, "Thermistor 2 threshold crossed\n");
		break;
	case EC_THERMISTOR_3_THRESHOLD_CROSS_EVT:
		dev_dbg_ratelimited(dev, "Thermistor 3 threshold crossed\n");
		break;
	case EC_RECOVERED_FROM_RESET_EVT:
		dev_dbg_ratelimited(dev, "EC recovered from reset\n");
		break;
	default:
		dev_notice_ratelimited(dev, "Unknown EC event: %d\n", val);
		break;
	}

	return IRQ_HANDLED;
}

static int qcom_ec_sci_evt_control(struct device *dev, bool enable)
{
	struct i2c_client *client = to_i2c_client(dev);

	return i2c_smbus_write_byte_data(client, EC_SCI_EVT_CONTROL_CMD, enable ? 1 : 0);
}

static int qcom_ec_fan_get_max_state(struct thermal_cooling_device *cdev, unsigned long *state)
{
	*state = EC_FAN_MAX_PWM;

	return 0;
}

static int qcom_ec_fan_get_cur_state(struct thermal_cooling_device *cdev, unsigned long *state)
{
	struct qcom_ec_cooling_dev *ec_cdev = cdev->devdata;

	*state = ec_cdev->state;

	return 0;
}

/*
 * Fan Debug control command:
 *
 * Command Payload:
 * --------------------------------------------------------------------------------------
 * | Offset		| Name		| Description					|
 * --------------------------------------------------------------------------------------
 * | 0x00		| Command	| Fan control command				|
 * --------------------------------------------------------------------------------------
 * | 0x01		| Fan ID	| 0x1 : Fan 1					|
 * |			|		| 0x2 : Fan 2					|
 * --------------------------------------------------------------------------------------
 * | 0x02		| Byte count = 4| Size of data to set fan speed			|
 * --------------------------------------------------------------------------------------
 * | 0x03		| Mode		| Bit 0: Debug Mode On/Off (0 - OFF, 1 - ON )	|
 * |			|		| Bit 1: Fan On/Off (0 - Off, 1 - ON)		|
 * |			|		| Bit 2: Debug Type (0 - RPM, 1 - PWM)		|
 * --------------------------------------------------------------------------------------
 * | 0x04 (LSB)		| Speed in RPM	| RPM value, if mode selected is RPM		|
 * | 0x05		|		|						|
 * --------------------------------------------------------------------------------------
 * | 0x06		| Speed in PWM	| PWM value, if mode selected is PWM (0 - 255)	|
 * ______________________________________________________________________________________
 *
 */
static int qcom_ec_fan_debug_mode_off(struct qcom_ec_cooling_dev *ec_cdev)
{
	struct device *dev = ec_cdev->parent_dev;
	struct i2c_client *client = to_i2c_client(dev);
	u8 request[6] = { ec_cdev->fan_id, EC_FAN_SPEED_DATA_SIZE,
			  EC_FAN_DEBUG_MODE_OFF, 0, 0, 0 };
	int ret;

	ret = i2c_smbus_write_i2c_block_data(client, EC_FAN_DBG_CONTROL_CMD,
					     sizeof(request), request);
	if (ret) {
		dev_err(dev, "Failed to turn off fan%d debug mode: %d\n",
			ec_cdev->fan_id, ret);
	}

	return ret;
}

static int qcom_ec_fan_set_cur_state(struct thermal_cooling_device *cdev, unsigned long state)
{
	struct qcom_ec_cooling_dev *ec_cdev = cdev->devdata;
	struct device *dev = ec_cdev->parent_dev;
	struct i2c_client *client = to_i2c_client(dev);
	u8 request[6] = { ec_cdev->fan_id, EC_FAN_SPEED_DATA_SIZE,
			  EC_FAN_DEBUG_MODE_ON | EC_FAN_ON | EC_FAN_DEBUG_TYPE_PWM,
			  0, 0, state };
	int ret;

	ret = i2c_smbus_write_i2c_block_data(client, EC_FAN_DBG_CONTROL_CMD,
					     sizeof(request), request);
	if (ret) {
		dev_err(dev, "Failed to set fan pwm: %d\n", ret);
		return ret;
	}

	ec_cdev->state = state;

	return 0;
}

static const struct thermal_cooling_device_ops qcom_ec_thermal_ops = {
	.get_max_state = qcom_ec_fan_get_max_state,
	.get_cur_state = qcom_ec_fan_get_cur_state,
	.set_cur_state = qcom_ec_fan_set_cur_state,
};

static int qcom_ec_resume(struct device *dev)
{
	struct i2c_client *client = to_i2c_client(dev);

	return i2c_smbus_write_byte_data(client, EC_MODERN_STANDBY_CMD,
					 EC_MODERN_STANDBY_EXIT);
}

static int qcom_ec_suspend(struct device *dev)
{
	struct i2c_client *client = to_i2c_client(dev);

	return i2c_smbus_write_byte_data(client, EC_MODERN_STANDBY_CMD,
					 EC_MODERN_STANDBY_ENTER);
}

static int qcom_ec_probe(struct i2c_client *client)
{
	struct device *dev = &client->dev;
	struct qcom_ec *ec;
	unsigned int i;
	int ret;

	ec = devm_kzalloc(dev, sizeof(*ec), GFP_KERNEL);
	if (!ec)
		return -ENOMEM;

	ec->client = client;

	ret = devm_request_threaded_irq(dev, client->irq, NULL, qcom_ec_irq,
					IRQF_ONESHOT, "qcom_ec", ec);
	if (ret < 0)
		return ret;

	i2c_set_clientdata(client, ec);

	ret = qcom_ec_read_fw_version(dev);
	if (ret < 0)
		return dev_err_probe(dev, ret, "Failed to read EC firmware version\n");

	ret = qcom_ec_sci_evt_control(dev, true);
	if (ret < 0)
		return dev_err_probe(dev, ret, "Failed to enable SCI events\n");

	ret = qcom_ec_thermal_capabilities(dev);
	if (ret < 0)
		return dev_err_probe(dev, ret, "Failed to read thermal capabilities\n");

	if (ec->thermal_cap.fan_cnt == 0) {
		dev_warn(dev, FW_BUG "Failed to get fan count, firmware update required\n");
		return 0;
	}

	ec->ec_cdev = devm_kcalloc(dev, ec->thermal_cap.fan_cnt, sizeof(*ec->ec_cdev), GFP_KERNEL);
	if (!ec->ec_cdev)
		return -ENOMEM;

	for (i = 0; i < ec->thermal_cap.fan_cnt; i++) {
		struct qcom_ec_cooling_dev *ec_cdev = &ec->ec_cdev[i];
		char name[EC_FAN_NAME_SIZE];

		scnprintf(name, sizeof(name), "qcom_ec_fan_%u", i);
		ec_cdev->fan_id = i + 1;
		ec_cdev->parent_dev = dev;

		ec_cdev->cdev = devm_thermal_of_child_cooling_device_register(dev, NULL, name, ec_cdev,
									&qcom_ec_thermal_ops);
		if (IS_ERR(ec_cdev->cdev)) {
			return dev_err_probe(dev, PTR_ERR(ec_cdev->cdev),
					     "Failed to register fan%d cooling device\n", i);
		}
	}

	return 0;
}

static void qcom_ec_remove(struct i2c_client *client)
{
	struct qcom_ec *ec = i2c_get_clientdata(client);
	struct device *dev = &client->dev;
	int ret;

	ret = qcom_ec_sci_evt_control(dev, false);
	if (ret < 0)
		dev_err(dev, "Failed to disable SCI events: %d\n", ret);

	for (int i = 0; i < ec->thermal_cap.fan_cnt; i++) {
		struct qcom_ec_cooling_dev *ec_cdev = &ec->ec_cdev[i];

		qcom_ec_fan_debug_mode_off(ec_cdev);
	}
}

static const struct of_device_id qcom_ec_of_match[] = {
	{ .compatible = "qcom,hamoa-crd-ec" },
	{}
};
MODULE_DEVICE_TABLE(of, qcom_ec_of_match);

static const struct i2c_device_id qcom_ec_i2c_id_table[] = {
	{ "qcom-hamoa-ec", },
	{}
};
MODULE_DEVICE_TABLE(i2c, qcom_ec_i2c_id_table);

static DEFINE_SIMPLE_DEV_PM_OPS(qcom_ec_pm_ops,
		qcom_ec_suspend,
		qcom_ec_resume);

static struct i2c_driver qcom_ec_i2c_driver = {
	.driver = {
		.name = "qcom-hamoa-ec",
		.of_match_table = qcom_ec_of_match,
		.pm = &qcom_ec_pm_ops,
	},
	.probe = qcom_ec_probe,
	.remove = qcom_ec_remove,
	.id_table = qcom_ec_i2c_id_table,
};
module_i2c_driver(qcom_ec_i2c_driver);

MODULE_DESCRIPTION("QCOM Hamoa Embedded Controller");
MODULE_LICENSE("GPL");
