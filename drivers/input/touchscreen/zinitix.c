// SPDX-License-Identifier: GPL-2.0-only

#include <linux/delay.h>
#include <linux/i2c.h>
#include <linux/input.h>
#include <linux/input/mt.h>
#include <linux/input/touchscreen.h>
#include <linux/interrupt.h>
#include <linux/irq.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/property.h>
#include <linux/regulator/consumer.h>
#include <linux/slab.h>

/* Register Map */

#define ZINITIX_SWRESET_CMD			0x0000
#define ZINITIX_WAKEUP_CMD			0x0001

#define ZINITIX_IDLE_CMD			0x0004
#define ZINITIX_SLEEP_CMD			0x0005

#define ZINITIX_CLEAR_INT_STATUS_CMD		0x0003
#define ZINITIX_CALIBRATE_CMD			0x0006
#define ZINITIX_SAVE_STATUS_CMD			0x0007
#define ZINITIX_SAVE_CALIBRATION_CMD		0x0008
#define ZINITIX_RECALL_FACTORY_CMD		0x000f

#define ZINITIX_THRESHOLD			0x0020

#define ZINITIX_LARGE_PALM_REJECT_AREA_TH	0x003F

#define ZINITIX_DEBUG_REG			0x0115 /* 0~7 */

#define ZINITIX_TOUCH_MODE			0x0010

#define ZINITIX_CHIP_REVISION			0x0011
#define ZINITIX_CHIP_BTX0X_MASK			0xF0F0
#define ZINITIX_CHIP_BT4X2			0x4020
#define ZINITIX_CHIP_BT4X3			0x4030
#define ZINITIX_CHIP_BT4X4			0x4040

#define ZINITIX_FIRMWARE_VERSION		0x0012

#define ZINITIX_USB_DETECT			0x116

#define ZINITIX_MINOR_FW_VERSION		0x0121

#define ZINITIX_VENDOR_ID			0x001C
#define ZINITIX_HW_ID				0x0014

#define ZINITIX_DATA_VERSION_REG		0x0013
#define ZINITIX_SUPPORTED_FINGER_NUM		0x0015
#define ZINITIX_EEPROM_INFO			0x0018
#define ZINITIX_INITIAL_TOUCH_MODE		0x0019

#define ZINITIX_TOTAL_NUMBER_OF_X		0x0060
#define ZINITIX_TOTAL_NUMBER_OF_Y		0x0061

#define ZINITIX_DELAY_RAW_FOR_HOST		0x007f

#define ZINITIX_BUTTON_SUPPORTED_NUM		0x00B0
#define ZINITIX_BUTTON_SENSITIVITY		0x00B2
#define ZINITIX_DUMMY_BUTTON_SENSITIVITY	0X00C8

#define ZINITIX_X_RESOLUTION			0x00C0
#define ZINITIX_Y_RESOLUTION			0x00C1

#define ZINITIX_POINT_STATUS_REG		0x0080

#define ZINITIX_BT4X2_ICON_STATUS_REG		0x009A
#define ZINITIX_BT4X3_ICON_STATUS_REG		0x00A0
#define ZINITIX_BT4X4_ICON_STATUS_REG		0x00A0
#define ZINITIX_BT5XX_ICON_STATUS_REG		0x00AA

#define ZINITIX_POINT_COORD_REG			(ZINITIX_POINT_STATUS_REG + 2)

#define ZINITIX_AFE_FREQUENCY			0x0100
#define ZINITIX_DND_N_COUNT			0x0122
#define ZINITIX_DND_U_COUNT			0x0135

#define ZINITIX_RAWDATA_REG			0x0200

#define ZINITIX_EEPROM_INFO_REG			0x0018

#define ZINITIX_INT_ENABLE_FLAG			0x00f0
#define ZINITIX_PERIODICAL_INTERRUPT_INTERVAL	0x00f1

#define ZINITIX_BTN_WIDTH			0x016d

#define ZINITIX_CHECKSUM_RESULT			0x012c

#define ZINITIX_INIT_FLASH			0x01d0
#define ZINITIX_WRITE_FLASH			0x01d1
#define ZINITIX_READ_FLASH			0x01d2

#define ZINITIX_INTERNAL_FLAG_02		0x011e
#define ZINITIX_INTERNAL_FLAG_03		0x011f

#define ZINITIX_I2C_CHECKSUM_WCNT		0x016a
#define ZINITIX_I2C_CHECKSUM_RESULT		0x016c

/* Interrupt & status register flags */

#define BIT_PT_CNT_CHANGE			BIT(0)
#define BIT_DOWN				BIT(1)
#define BIT_MOVE				BIT(2)
#define BIT_UP					BIT(3)
#define BIT_PALM				BIT(4)
#define BIT_PALM_REJECT				BIT(5)
#define BIT_RESERVED_0				BIT(6)
#define BIT_RESERVED_1				BIT(7)
#define BIT_WEIGHT_CHANGE			BIT(8)
#define BIT_PT_NO_CHANGE			BIT(9)
#define BIT_REJECT				BIT(10)
#define BIT_PT_EXIST				BIT(11)
#define BIT_RESERVED_2				BIT(12)
#define BIT_ERROR				BIT(13)
#define BIT_DEBUG				BIT(14)
#define BIT_ICON_EVENT				BIT(15)

#define SUB_BIT_EXIST				BIT(0)
#define SUB_BIT_DOWN				BIT(1)
#define SUB_BIT_MOVE				BIT(2)
#define SUB_BIT_UP				BIT(3)
#define SUB_BIT_UPDATE				BIT(4)
#define SUB_BIT_WAIT				BIT(5)

#define DEFAULT_TOUCH_POINT_MODE		2
#define MAX_SUPPORTED_FINGER_NUM		5
#define MAX_SUPPORTED_BUTTON_NUM		8

#define CHIP_ON_DELAY				15 // ms
#define FIRMWARE_ON_DELAY			40 // ms

struct point_coord {
	__le16	x;
	__le16	y;
	u8	width;
	u8	sub_status;
	// currently unused, but needed as padding:
	u8	minor_width;
	u8	angle;
};

struct touch_event {
	__le16	status;
	u8	finger_mask;
	u8	time_stamp;
	struct point_coord point_coord[MAX_SUPPORTED_FINGER_NUM];
};

struct bt541_ts_data {
	struct i2c_client *client;
	struct input_dev *input_dev;
	struct touchscreen_properties prop;
	struct regulator_bulk_data supplies[2];
	u32 zinitix_mode;
	u32 keycodes[MAX_SUPPORTED_BUTTON_NUM];
	int num_keycodes;
	bool have_versioninfo;
	u16 chip_revision;
	u16 firmware_version;
	u16 regdata_version;
	u16 icon_status_reg;
};

static int zinitix_read_data(struct i2c_client *client,
			     u16 reg, void *values, size_t length)
{
	__le16 reg_le = cpu_to_le16(reg);
	int ret;

	/* A single i2c_transfer() transaction does not work here. */
	ret = i2c_master_send(client, (u8 *)&reg_le, sizeof(reg_le));
	if (ret != sizeof(reg_le))
		return ret < 0 ? ret : -EIO;

	ret = i2c_master_recv(client, (u8 *)values, length);
	if (ret != length)
		return ret < 0 ? ret : -EIO;

	return 0;
}

static int zinitix_write_u16(struct i2c_client *client, u16 reg, u16 value)
{
	__le16 packet[2] = {cpu_to_le16(reg), cpu_to_le16(value)};
	int ret;

	ret = i2c_master_send(client, (u8 *)packet, sizeof(packet));
	if (ret != sizeof(packet))
		return ret < 0 ? ret : -EIO;

	return 0;
}

static int zinitix_write_cmd(struct i2c_client *client, u16 reg)
{
	__le16 reg_le = cpu_to_le16(reg);
	int ret;

	ret = i2c_master_send(client, (u8 *)&reg_le, sizeof(reg_le));
	if (ret != sizeof(reg_le))
		return ret < 0 ? ret : -EIO;

	return 0;
}

static u16 zinitix_get_u16_reg(struct bt541_ts_data *bt541, u16 vreg)
{
	struct i2c_client *client = bt541->client;
	int error;
	__le16 val;

	error = zinitix_read_data(client, vreg, (void *)&val, 2);
	if (error)
		return U8_MAX;

	return le16_to_cpu(val);
}

static int zinitix_init_touch(struct bt541_ts_data *bt541)
{
	struct i2c_client *client = bt541->client;
	int i;
	int error;
	u16 int_flags;

	error = zinitix_write_cmd(client, ZINITIX_SWRESET_CMD);
	if (error) {
		dev_err(&client->dev, "Failed to write reset command\n");
		return error;
	}

	/*
	 * Read and cache the chip revision and firmware version the first time
	 * we get here.
	 */
	if (!bt541->have_versioninfo) {
		bt541->chip_revision = zinitix_get_u16_reg(bt541,
						ZINITIX_CHIP_REVISION);
		bt541->firmware_version = zinitix_get_u16_reg(bt541,
						ZINITIX_FIRMWARE_VERSION);
		bt541->regdata_version = zinitix_get_u16_reg(bt541,
						ZINITIX_DATA_VERSION_REG);
		bt541->have_versioninfo = true;

		dev_dbg(&client->dev,
			"chip revision %04x firmware version %04x regdata version %04x\n",
			bt541->chip_revision, bt541->firmware_version,
			bt541->regdata_version);

		/*
		 * Determine the "icon" status register which varies by the
		 * chip.
		 */
		switch (bt541->chip_revision & ZINITIX_CHIP_BTX0X_MASK) {
		case ZINITIX_CHIP_BT4X2:
			bt541->icon_status_reg = ZINITIX_BT4X2_ICON_STATUS_REG;
			break;

		case ZINITIX_CHIP_BT4X3:
			bt541->icon_status_reg = ZINITIX_BT4X3_ICON_STATUS_REG;
			break;

		case ZINITIX_CHIP_BT4X4:
			bt541->icon_status_reg = ZINITIX_BT4X4_ICON_STATUS_REG;
			break;

		default:
			bt541->icon_status_reg = ZINITIX_BT5XX_ICON_STATUS_REG;
			break;
		}
	}

	error = zinitix_write_u16(client, ZINITIX_INT_ENABLE_FLAG, 0x0);
	if (error) {
		dev_err(&client->dev,
			"Failed to reset interrupt enable flag\n");
		return error;
	}

	/* initialize */
	error = zinitix_write_u16(client, ZINITIX_X_RESOLUTION,
				  bt541->prop.max_x);
	if (error)
		return error;

	error = zinitix_write_u16(client, ZINITIX_Y_RESOLUTION,
				  bt541->prop.max_y);
	if (error)
		return error;

	error = zinitix_write_u16(client, ZINITIX_SUPPORTED_FINGER_NUM,
				  MAX_SUPPORTED_FINGER_NUM);
	if (error)
		return error;

	error = zinitix_write_u16(client, ZINITIX_BUTTON_SUPPORTED_NUM,
				  bt541->num_keycodes);
	if (error)
		return error;

	error = zinitix_write_u16(client, ZINITIX_INITIAL_TOUCH_MODE,
				  bt541->zinitix_mode);
	if (error)
		return error;

	error = zinitix_write_u16(client, ZINITIX_TOUCH_MODE,
				  bt541->zinitix_mode);
	if (error)
		return error;

	int_flags = BIT_PT_CNT_CHANGE | BIT_DOWN | BIT_MOVE | BIT_UP;
	if (bt541->num_keycodes)
		int_flags |= BIT_ICON_EVENT;

	error = zinitix_write_u16(client, ZINITIX_INT_ENABLE_FLAG, int_flags);
	if (error)
		return error;

	/* clear queue */
	for (i = 0; i < 10; i++) {
		zinitix_write_cmd(client, ZINITIX_CLEAR_INT_STATUS_CMD);
		udelay(10);
	}

	return 0;
}

static int zinitix_init_regulators(struct bt541_ts_data *bt541)
{
	struct device *dev = &bt541->client->dev;
	int error;

	/*
	 * Some older device trees have erroneous names for the regulators,
	 * so check if "vddo" is present and in that case use these names.
	 * Else use the proper supply names on the component.
	 */
	if (of_property_present(dev->of_node, "vddo-supply")) {
		bt541->supplies[0].supply = "vdd";
		bt541->supplies[1].supply = "vddo";
	} else {
		/* Else use the proper supply names */
		bt541->supplies[0].supply = "vcca";
		bt541->supplies[1].supply = "vdd";
	}
	error = devm_regulator_bulk_get(dev,
					ARRAY_SIZE(bt541->supplies),
					bt541->supplies);
	if (error < 0) {
		dev_err(dev, "Failed to get regulators: %d\n", error);
		return error;
	}

	return 0;
}

static int zinitix_send_power_on_sequence(struct bt541_ts_data *bt541)
{
	int error;
	struct i2c_client *client = bt541->client;

	error = zinitix_write_u16(client, 0xc000, 0x0001);
	if (error) {
		dev_err(&client->dev,
			"Failed to send power sequence(vendor cmd enable)\n");
		return error;
	}
	udelay(10);

	error = zinitix_write_cmd(client, 0xc004);
	if (error) {
		dev_err(&client->dev,
			"Failed to send power sequence (intn clear)\n");
		return error;
	}
	udelay(10);

	error = zinitix_write_u16(client, 0xc002, 0x0001);
	if (error) {
		dev_err(&client->dev,
			"Failed to send power sequence (nvm init)\n");
		return error;
	}
	mdelay(2);

	error = zinitix_write_u16(client, 0xc001, 0x0001);
	if (error) {
		dev_err(&client->dev,
			"Failed to send power sequence (program start)\n");
		return error;
	}
	msleep(FIRMWARE_ON_DELAY);

	return 0;
}

static void zinitix_report_finger(struct bt541_ts_data *bt541, int slot,
				  const struct point_coord *p)
{
	u16 x, y;

	if (unlikely(!(p->sub_status &
		       (SUB_BIT_UP | SUB_BIT_DOWN | SUB_BIT_MOVE)))) {
		dev_dbg(&bt541->client->dev, "unknown finger event %#02x\n",
			p->sub_status);
		return;
	}

	x = le16_to_cpu(p->x);
	y = le16_to_cpu(p->y);

	input_mt_slot(bt541->input_dev, slot);
	if (input_mt_report_slot_state(bt541->input_dev, MT_TOOL_FINGER,
				       !(p->sub_status & SUB_BIT_UP))) {
		touchscreen_report_pos(bt541->input_dev,
				       &bt541->prop, x, y, true);
		input_report_abs(bt541->input_dev,
				 ABS_MT_TOUCH_MAJOR, p->width);
		dev_dbg(&bt541->client->dev, "finger %d %s (%u, %u)\n",
			slot, p->sub_status & SUB_BIT_DOWN ? "down" : "move",
			x, y);
	} else {
		dev_dbg(&bt541->client->dev, "finger %d up (%u, %u)\n",
			slot, x, y);
	}
}

static void zinitix_report_keys(struct bt541_ts_data *bt541, u16 icon_events)
{
	int i;

	for (i = 0; i < bt541->num_keycodes; i++)
		input_report_key(bt541->input_dev,
				 bt541->keycodes[i], icon_events & BIT(i));
}

static irqreturn_t zinitix_ts_irq_handler(int irq, void *bt541_handler)
{
	struct bt541_ts_data *bt541 = bt541_handler;
	struct i2c_client *client = bt541->client;
	struct touch_event touch_event;
	unsigned long finger_mask;
	__le16 icon_events;
	int error;
	int i;

	memset(&touch_event, 0, sizeof(struct touch_event));

	error = zinitix_read_data(bt541->client, ZINITIX_POINT_STATUS_REG,
				  &touch_event, sizeof(struct touch_event));
	if (error) {
		dev_err(&client->dev, "Failed to read in touchpoint struct\n");
		goto out;
	}

	if (le16_to_cpu(touch_event.status) & BIT_ICON_EVENT) {
		error = zinitix_read_data(bt541->client, bt541->icon_status_reg,
					  &icon_events, sizeof(icon_events));
		if (error) {
			dev_err(&client->dev, "Failed to read icon events\n");
			goto out;
		}

		zinitix_report_keys(bt541, le16_to_cpu(icon_events));
	}

	finger_mask = touch_event.finger_mask;
	for_each_set_bit(i, &finger_mask, MAX_SUPPORTED_FINGER_NUM) {
		const struct point_coord *p = &touch_event.point_coord[i];

		/* Only process contacts that are actually reported */
		if (p->sub_status & SUB_BIT_EXIST)
			zinitix_report_finger(bt541, i, p);
	}

	input_mt_sync_frame(bt541->input_dev);
	input_sync(bt541->input_dev);

out:
	zinitix_write_cmd(bt541->client, ZINITIX_CLEAR_INT_STATUS_CMD);
	return IRQ_HANDLED;
}

static int zinitix_start(struct bt541_ts_data *bt541)
{
	int error;

	error = regulator_bulk_enable(ARRAY_SIZE(bt541->supplies),
				      bt541->supplies);
	if (error) {
		dev_err(&bt541->client->dev,
			"Failed to enable regulators: %d\n", error);
		return error;
	}

	msleep(CHIP_ON_DELAY);

	error = zinitix_send_power_on_sequence(bt541);
	if (error) {
		dev_err(&bt541->client->dev,
			"Error while sending power-on sequence: %d\n", error);
		return error;
	}

	error = zinitix_init_touch(bt541);
	if (error) {
		dev_err(&bt541->client->dev,
			"Error while configuring touch IC\n");
		return error;
	}

	enable_irq(bt541->client->irq);

	return 0;
}

static int zinitix_stop(struct bt541_ts_data *bt541)
{
	int error;

	disable_irq(bt541->client->irq);

	error = regulator_bulk_disable(ARRAY_SIZE(bt541->supplies),
				       bt541->supplies);
	if (error) {
		dev_err(&bt541->client->dev,
			"Failed to disable regulators: %d\n", error);
		return error;
	}

	return 0;
}

static int zinitix_input_open(struct input_dev *dev)
{
	struct bt541_ts_data *bt541 = input_get_drvdata(dev);

	return zinitix_start(bt541);
}

static void zinitix_input_close(struct input_dev *dev)
{
	struct bt541_ts_data *bt541 = input_get_drvdata(dev);

	zinitix_stop(bt541);
}

static int zinitix_init_input_dev(struct bt541_ts_data *bt541)
{
	struct input_dev *input_dev;
	int error;
	int i;

	input_dev = devm_input_allocate_device(&bt541->client->dev);
	if (!input_dev) {
		dev_err(&bt541->client->dev,
			"Failed to allocate input device.");
		return -ENOMEM;
	}

	input_set_drvdata(input_dev, bt541);
	bt541->input_dev = input_dev;

	input_dev->name = "Zinitix Capacitive TouchScreen";
	input_dev->phys = "input/ts";
	input_dev->id.bustype = BUS_I2C;
	input_dev->open = zinitix_input_open;
	input_dev->close = zinitix_input_close;

	if (bt541->num_keycodes) {
		input_dev->keycode = bt541->keycodes;
		input_dev->keycodemax = bt541->num_keycodes;
		input_dev->keycodesize = sizeof(bt541->keycodes[0]);
		for (i = 0; i < bt541->num_keycodes; i++)
			input_set_capability(input_dev, EV_KEY, bt541->keycodes[i]);
	}

	input_set_capability(input_dev, EV_ABS, ABS_MT_POSITION_X);
	input_set_capability(input_dev, EV_ABS, ABS_MT_POSITION_Y);
	input_set_abs_params(input_dev, ABS_MT_WIDTH_MAJOR, 0, 255, 0, 0);
	input_set_abs_params(input_dev, ABS_MT_TOUCH_MAJOR, 0, 255, 0, 0);

	touchscreen_parse_properties(input_dev, true, &bt541->prop);
	if (!bt541->prop.max_x || !bt541->prop.max_y) {
		dev_err(&bt541->client->dev,
			"Touchscreen-size-x and/or touchscreen-size-y not set in dts\n");
		return -EINVAL;
	}

	error = input_mt_init_slots(input_dev, MAX_SUPPORTED_FINGER_NUM,
				    INPUT_MT_DIRECT | INPUT_MT_DROP_UNUSED);
	if (error) {
		dev_err(&bt541->client->dev,
			"Failed to initialize MT slots: %d", error);
		return error;
	}

	error = input_register_device(input_dev);
	if (error) {
		dev_err(&bt541->client->dev,
			"Failed to register input device: %d", error);
		return error;
	}

	return 0;
}

static int zinitix_ts_probe(struct i2c_client *client)
{
	struct bt541_ts_data *bt541;
	int error;

	if (!i2c_check_functionality(client->adapter, I2C_FUNC_I2C)) {
		dev_err(&client->dev,
			"Failed to assert adapter's support for plain I2C.\n");
		return -ENXIO;
	}

	bt541 = devm_kzalloc(&client->dev, sizeof(*bt541), GFP_KERNEL);
	if (!bt541)
		return -ENOMEM;

	bt541->client = client;
	i2c_set_clientdata(client, bt541);

	error = zinitix_init_regulators(bt541);
	if (error) {
		dev_err(&client->dev,
			"Failed to initialize regulators: %d\n", error);
		return error;
	}

	error = devm_request_threaded_irq(&client->dev, client->irq,
					  NULL, zinitix_ts_irq_handler,
					  IRQF_ONESHOT | IRQF_NO_AUTOEN,
					  client->name, bt541);
	if (error) {
		dev_err(&client->dev, "Failed to request IRQ: %d\n", error);
		return error;
	}

	if (device_property_present(&client->dev, "linux,keycodes")) {
		bt541->num_keycodes = device_property_count_u32(&client->dev,
								"linux,keycodes");
		if (bt541->num_keycodes < 0) {
			dev_err(&client->dev, "Failed to count keys (%d)\n",
				bt541->num_keycodes);
			return bt541->num_keycodes;
		} else if (bt541->num_keycodes > ARRAY_SIZE(bt541->keycodes)) {
			dev_err(&client->dev, "Too many keys defined (%d)\n",
				bt541->num_keycodes);
			return -EINVAL;
		}

		error = device_property_read_u32_array(&client->dev,
						       "linux,keycodes",
						       bt541->keycodes,
						       bt541->num_keycodes);
		if (error) {
			dev_err(&client->dev,
				"Unable to parse \"linux,keycodes\" property: %d\n",
				error);
			return error;
		}
	}

	error = zinitix_init_input_dev(bt541);
	if (error) {
		dev_err(&client->dev,
			"Failed to initialize input device: %d\n", error);
		return error;
	}

	error = device_property_read_u32(&client->dev, "zinitix,mode",
					 &bt541->zinitix_mode);
	if (error < 0) {
		/* fall back to mode 2 */
		bt541->zinitix_mode = DEFAULT_TOUCH_POINT_MODE;
	}

	if (bt541->zinitix_mode != 2) {
		/*
		 * If there are devices that don't support mode 2, support
		 * for other modes (0, 1) will be needed.
		 */
		dev_err(&client->dev,
			"Malformed zinitix,mode property, must be 2 (supplied: %d)\n",
			bt541->zinitix_mode);
		return -EINVAL;
	}

	return 0;
}

static int zinitix_suspend(struct device *dev)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct bt541_ts_data *bt541 = i2c_get_clientdata(client);

	mutex_lock(&bt541->input_dev->mutex);

	if (input_device_enabled(bt541->input_dev))
		zinitix_stop(bt541);

	mutex_unlock(&bt541->input_dev->mutex);

	return 0;
}

static int zinitix_resume(struct device *dev)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct bt541_ts_data *bt541 = i2c_get_clientdata(client);
	int ret = 0;

	mutex_lock(&bt541->input_dev->mutex);

	if (input_device_enabled(bt541->input_dev))
		ret = zinitix_start(bt541);

	mutex_unlock(&bt541->input_dev->mutex);

	return ret;
}

static DEFINE_SIMPLE_DEV_PM_OPS(zinitix_pm_ops, zinitix_suspend, zinitix_resume);

#ifdef CONFIG_OF
static const struct of_device_id zinitix_of_match[] = {
	{ .compatible = "zinitix,bt402" },
	{ .compatible = "zinitix,bt403" },
	{ .compatible = "zinitix,bt404" },
	{ .compatible = "zinitix,bt412" },
	{ .compatible = "zinitix,bt413" },
	{ .compatible = "zinitix,bt431" },
	{ .compatible = "zinitix,bt432" },
	{ .compatible = "zinitix,bt531" },
	{ .compatible = "zinitix,bt532" },
	{ .compatible = "zinitix,bt538" },
	{ .compatible = "zinitix,bt541" },
	{ .compatible = "zinitix,bt548" },
	{ .compatible = "zinitix,bt554" },
	{ .compatible = "zinitix,at100" },
	{ }
};
MODULE_DEVICE_TABLE(of, zinitix_of_match);
#endif

static struct i2c_driver zinitix_ts_driver = {
	.probe = zinitix_ts_probe,
	.driver = {
		.name = "Zinitix-TS",
		.pm = pm_sleep_ptr(&zinitix_pm_ops),
		.of_match_table = of_match_ptr(zinitix_of_match),
	},
};
module_i2c_driver(zinitix_ts_driver);

MODULE_AUTHOR("Michael Srba <Michael.Srba@seznam.cz>");
MODULE_DESCRIPTION("Zinitix touchscreen driver");
MODULE_LICENSE("GPL v2");
