// SPDX-License-Identifier: GPL-2.0+
/*
 * Azoteq IQS620A/621/622/624/625 Multi-Function Sensors
 *
 * Copyright (C) 2019 Jeff LaBundy <jeff@labundy.com>
 *
 * These devices rely on application-specific register settings and calibration
 * data developed in and exported from a suite of GUIs offered by the vendor. A
 * separate tool converts the GUIs' ASCII-based output into a standard firmware
 * file parsed by the driver.
 *
 * Link to datasheets and GUIs: https://www.azoteq.com/
 *
 * Link to conversion tool: https://github.com/jlabundy/iqs62x-h2bin.git
 */

#include <linux/completion.h>
#include <linux/delay.h>
#include <linux/device.h>
#include <linux/err.h>
#include <linux/firmware.h>
#include <linux/i2c.h>
#include <linux/interrupt.h>
#include <linux/kernel.h>
#include <linux/list.h>
#include <linux/mfd/core.h>
#include <linux/mfd/iqs62x.h>
#include <linux/module.h>
#include <linux/notifier.h>
#include <linux/of_device.h>
#include <linux/property.h>
#include <linux/regmap.h>
#include <linux/slab.h>
#include <asm/unaligned.h>

#define IQS62X_PROD_NUM				0x00

#define IQS62X_SYS_FLAGS			0x10
#define IQS62X_SYS_FLAGS_IN_ATI			BIT(2)

#define IQS620_HALL_FLAGS			0x16
#define IQS621_HALL_FLAGS			0x19
#define IQS622_HALL_FLAGS			IQS621_HALL_FLAGS

#define IQS624_INTERVAL_NUM			0x18
#define IQS625_INTERVAL_NUM			0x12

#define IQS622_PROX_SETTINGS_4			0x48
#define IQS620_PROX_SETTINGS_4			0x50
#define IQS620_PROX_SETTINGS_4_SAR_EN		BIT(7)

#define IQS621_ALS_CAL_DIV_LUX			0x82
#define IQS621_ALS_CAL_DIV_IR			0x83

#define IQS620_TEMP_CAL_MULT			0xC2
#define IQS620_TEMP_CAL_DIV			0xC3
#define IQS620_TEMP_CAL_OFFS			0xC4

#define IQS62X_SYS_SETTINGS			0xD0
#define IQS62X_SYS_SETTINGS_SOFT_RESET		BIT(7)
#define IQS62X_SYS_SETTINGS_ACK_RESET		BIT(6)
#define IQS62X_SYS_SETTINGS_EVENT_MODE		BIT(5)
#define IQS62X_SYS_SETTINGS_CLK_DIV		BIT(4)
#define IQS62X_SYS_SETTINGS_REDO_ATI		BIT(1)

#define IQS62X_PWR_SETTINGS			0xD2
#define IQS62X_PWR_SETTINGS_DIS_AUTO		BIT(5)
#define IQS62X_PWR_SETTINGS_PWR_MODE_MASK	(BIT(4) | BIT(3))
#define IQS62X_PWR_SETTINGS_PWR_MODE_HALT	(BIT(4) | BIT(3))
#define IQS62X_PWR_SETTINGS_PWR_MODE_NORM	0

#define IQS62X_OTP_CMD				0xF0
#define IQS62X_OTP_CMD_FG3			0x13
#define IQS62X_OTP_DATA				0xF1
#define IQS62X_MAX_REG				0xFF

#define IQS62X_HALL_CAL_MASK			GENMASK(3, 0)

#define IQS62X_FW_REC_TYPE_INFO			0
#define IQS62X_FW_REC_TYPE_PROD			1
#define IQS62X_FW_REC_TYPE_HALL			2
#define IQS62X_FW_REC_TYPE_MASK			3
#define IQS62X_FW_REC_TYPE_DATA			4

#define IQS62X_ATI_POLL_SLEEP_US		10000
#define IQS62X_ATI_POLL_TIMEOUT_US		500000
#define IQS62X_ATI_STABLE_DELAY_MS		150

struct iqs62x_fw_rec {
	u8 type;
	u8 addr;
	u8 len;
	u8 data;
} __packed;

struct iqs62x_fw_blk {
	struct list_head list;
	u8 addr;
	u8 mask;
	u8 len;
	u8 data[];
};

struct iqs62x_info {
	u8 prod_num;
	u8 sw_num;
	u8 hw_num;
} __packed;

static int iqs62x_dev_init(struct iqs62x_core *iqs62x)
{
	struct iqs62x_fw_blk *fw_blk;
	unsigned int val;
	int ret;
	u8 clk_div = 1;

	list_for_each_entry(fw_blk, &iqs62x->fw_blk_head, list) {
		if (fw_blk->mask)
			ret = regmap_update_bits(iqs62x->regmap, fw_blk->addr,
						 fw_blk->mask, *fw_blk->data);
		else
			ret = regmap_raw_write(iqs62x->regmap, fw_blk->addr,
					       fw_blk->data, fw_blk->len);
		if (ret)
			return ret;
	}

	switch (iqs62x->dev_desc->prod_num) {
	case IQS620_PROD_NUM:
	case IQS622_PROD_NUM:
		ret = regmap_read(iqs62x->regmap,
				  iqs62x->dev_desc->prox_settings, &val);
		if (ret)
			return ret;

		if (val & IQS620_PROX_SETTINGS_4_SAR_EN)
			iqs62x->ui_sel = IQS62X_UI_SAR1;

		fallthrough;

	case IQS621_PROD_NUM:
		ret = regmap_write(iqs62x->regmap, IQS620_GLBL_EVENT_MASK,
				   IQS620_GLBL_EVENT_MASK_PMU |
				   iqs62x->dev_desc->prox_mask |
				   iqs62x->dev_desc->sar_mask |
				   iqs62x->dev_desc->hall_mask |
				   iqs62x->dev_desc->hyst_mask |
				   iqs62x->dev_desc->temp_mask |
				   iqs62x->dev_desc->als_mask |
				   iqs62x->dev_desc->ir_mask);
		if (ret)
			return ret;
		break;

	default:
		ret = regmap_write(iqs62x->regmap, IQS624_HALL_UI,
				   IQS624_HALL_UI_WHL_EVENT |
				   IQS624_HALL_UI_INT_EVENT |
				   IQS624_HALL_UI_AUTO_CAL);
		if (ret)
			return ret;

		/*
		 * The IQS625 default interval divider is below the minimum
		 * permissible value, and the datasheet mandates that it is
		 * corrected during initialization (unless an updated value
		 * has already been provided by firmware).
		 *
		 * To protect against an unacceptably low user-entered value
		 * stored in the firmware, the same check is extended to the
		 * IQS624 as well.
		 */
		ret = regmap_read(iqs62x->regmap, IQS624_INTERVAL_DIV, &val);
		if (ret)
			return ret;

		if (val >= iqs62x->dev_desc->interval_div)
			break;

		ret = regmap_write(iqs62x->regmap, IQS624_INTERVAL_DIV,
				   iqs62x->dev_desc->interval_div);
		if (ret)
			return ret;
	}

	ret = regmap_read(iqs62x->regmap, IQS62X_SYS_SETTINGS, &val);
	if (ret)
		return ret;

	if (val & IQS62X_SYS_SETTINGS_CLK_DIV)
		clk_div = iqs62x->dev_desc->clk_div;

	ret = regmap_write(iqs62x->regmap, IQS62X_SYS_SETTINGS, val |
			   IQS62X_SYS_SETTINGS_ACK_RESET |
			   IQS62X_SYS_SETTINGS_EVENT_MODE |
			   IQS62X_SYS_SETTINGS_REDO_ATI);
	if (ret)
		return ret;

	ret = regmap_read_poll_timeout(iqs62x->regmap, IQS62X_SYS_FLAGS, val,
				       !(val & IQS62X_SYS_FLAGS_IN_ATI),
				       IQS62X_ATI_POLL_SLEEP_US,
				       IQS62X_ATI_POLL_TIMEOUT_US * clk_div);
	if (ret)
		return ret;

	msleep(IQS62X_ATI_STABLE_DELAY_MS * clk_div);

	return 0;
}

static int iqs62x_firmware_parse(struct iqs62x_core *iqs62x,
				 const struct firmware *fw)
{
	struct i2c_client *client = iqs62x->client;
	struct iqs62x_fw_rec *fw_rec;
	struct iqs62x_fw_blk *fw_blk;
	unsigned int val;
	size_t pos = 0;
	int ret = 0;
	u8 mask, len, *data;
	u8 hall_cal_index = 0;

	while (pos < fw->size) {
		if (pos + sizeof(*fw_rec) > fw->size) {
			ret = -EINVAL;
			break;
		}
		fw_rec = (struct iqs62x_fw_rec *)(fw->data + pos);
		pos += sizeof(*fw_rec);

		if (pos + fw_rec->len - 1 > fw->size) {
			ret = -EINVAL;
			break;
		}
		pos += fw_rec->len - 1;

		switch (fw_rec->type) {
		case IQS62X_FW_REC_TYPE_INFO:
			continue;

		case IQS62X_FW_REC_TYPE_PROD:
			if (fw_rec->data == iqs62x->dev_desc->prod_num)
				continue;

			dev_err(&client->dev,
				"Incompatible product number: 0x%02X\n",
				fw_rec->data);
			ret = -EINVAL;
			break;

		case IQS62X_FW_REC_TYPE_HALL:
			if (!hall_cal_index) {
				ret = regmap_write(iqs62x->regmap,
						   IQS62X_OTP_CMD,
						   IQS62X_OTP_CMD_FG3);
				if (ret)
					break;

				ret = regmap_read(iqs62x->regmap,
						  IQS62X_OTP_DATA, &val);
				if (ret)
					break;

				hall_cal_index = val & IQS62X_HALL_CAL_MASK;
				if (!hall_cal_index) {
					dev_err(&client->dev,
						"Uncalibrated device\n");
					ret = -ENODATA;
					break;
				}
			}

			if (hall_cal_index > fw_rec->len) {
				ret = -EINVAL;
				break;
			}

			mask = 0;
			data = &fw_rec->data + hall_cal_index - 1;
			len = sizeof(*data);
			break;

		case IQS62X_FW_REC_TYPE_MASK:
			if (fw_rec->len < (sizeof(mask) + sizeof(*data))) {
				ret = -EINVAL;
				break;
			}

			mask = fw_rec->data;
			data = &fw_rec->data + sizeof(mask);
			len = sizeof(*data);
			break;

		case IQS62X_FW_REC_TYPE_DATA:
			mask = 0;
			data = &fw_rec->data;
			len = fw_rec->len;
			break;

		default:
			dev_err(&client->dev,
				"Unrecognized record type: 0x%02X\n",
				fw_rec->type);
			ret = -EINVAL;
		}

		if (ret)
			break;

		fw_blk = devm_kzalloc(&client->dev,
				      struct_size(fw_blk, data, len),
				      GFP_KERNEL);
		if (!fw_blk) {
			ret = -ENOMEM;
			break;
		}

		fw_blk->addr = fw_rec->addr;
		fw_blk->mask = mask;
		fw_blk->len = len;
		memcpy(fw_blk->data, data, len);

		list_add(&fw_blk->list, &iqs62x->fw_blk_head);
	}

	release_firmware(fw);

	return ret;
}

const struct iqs62x_event_desc iqs62x_events[IQS62X_NUM_EVENTS] = {
	[IQS62X_EVENT_PROX_CH0_T] = {
		.reg	= IQS62X_EVENT_PROX,
		.mask	= BIT(4),
		.val	= BIT(4),
	},
	[IQS62X_EVENT_PROX_CH0_P] = {
		.reg	= IQS62X_EVENT_PROX,
		.mask	= BIT(0),
		.val	= BIT(0),
	},
	[IQS62X_EVENT_PROX_CH1_T] = {
		.reg	= IQS62X_EVENT_PROX,
		.mask	= BIT(5),
		.val	= BIT(5),
	},
	[IQS62X_EVENT_PROX_CH1_P] = {
		.reg	= IQS62X_EVENT_PROX,
		.mask	= BIT(1),
		.val	= BIT(1),
	},
	[IQS62X_EVENT_PROX_CH2_T] = {
		.reg	= IQS62X_EVENT_PROX,
		.mask	= BIT(6),
		.val	= BIT(6),
	},
	[IQS62X_EVENT_PROX_CH2_P] = {
		.reg	= IQS62X_EVENT_PROX,
		.mask	= BIT(2),
		.val	= BIT(2),
	},
	[IQS62X_EVENT_HYST_POS_T] = {
		.reg	= IQS62X_EVENT_HYST,
		.mask	= BIT(6) | BIT(7),
		.val	= BIT(6),
	},
	[IQS62X_EVENT_HYST_POS_P] = {
		.reg	= IQS62X_EVENT_HYST,
		.mask	= BIT(5) | BIT(7),
		.val	= BIT(5),
	},
	[IQS62X_EVENT_HYST_NEG_T] = {
		.reg	= IQS62X_EVENT_HYST,
		.mask	= BIT(6) | BIT(7),
		.val	= BIT(6) | BIT(7),
	},
	[IQS62X_EVENT_HYST_NEG_P] = {
		.reg	= IQS62X_EVENT_HYST,
		.mask	= BIT(5) | BIT(7),
		.val	= BIT(5) | BIT(7),
	},
	[IQS62X_EVENT_SAR1_ACT] = {
		.reg	= IQS62X_EVENT_HYST,
		.mask	= BIT(4),
		.val	= BIT(4),
	},
	[IQS62X_EVENT_SAR1_QRD] = {
		.reg	= IQS62X_EVENT_HYST,
		.mask	= BIT(2),
		.val	= BIT(2),
	},
	[IQS62X_EVENT_SAR1_MOVE] = {
		.reg	= IQS62X_EVENT_HYST,
		.mask	= BIT(1),
		.val	= BIT(1),
	},
	[IQS62X_EVENT_SAR1_HALT] = {
		.reg	= IQS62X_EVENT_HYST,
		.mask	= BIT(0),
		.val	= BIT(0),
	},
	[IQS62X_EVENT_WHEEL_UP] = {
		.reg	= IQS62X_EVENT_WHEEL,
		.mask	= BIT(7) | BIT(6),
		.val	= BIT(7),
	},
	[IQS62X_EVENT_WHEEL_DN] = {
		.reg	= IQS62X_EVENT_WHEEL,
		.mask	= BIT(7) | BIT(6),
		.val	= BIT(7) | BIT(6),
	},
	[IQS62X_EVENT_HALL_N_T] = {
		.reg	= IQS62X_EVENT_HALL,
		.mask	= BIT(2) | BIT(0),
		.val	= BIT(2),
	},
	[IQS62X_EVENT_HALL_N_P] = {
		.reg	= IQS62X_EVENT_HALL,
		.mask	= BIT(1) | BIT(0),
		.val	= BIT(1),
	},
	[IQS62X_EVENT_HALL_S_T] = {
		.reg	= IQS62X_EVENT_HALL,
		.mask	= BIT(2) | BIT(0),
		.val	= BIT(2) | BIT(0),
	},
	[IQS62X_EVENT_HALL_S_P] = {
		.reg	= IQS62X_EVENT_HALL,
		.mask	= BIT(1) | BIT(0),
		.val	= BIT(1) | BIT(0),
	},
	[IQS62X_EVENT_SYS_RESET] = {
		.reg	= IQS62X_EVENT_SYS,
		.mask	= BIT(7),
		.val	= BIT(7),
	},
};
EXPORT_SYMBOL_GPL(iqs62x_events);

static irqreturn_t iqs62x_irq(int irq, void *context)
{
	struct iqs62x_core *iqs62x = context;
	struct i2c_client *client = iqs62x->client;
	struct iqs62x_event_data event_data;
	struct iqs62x_event_desc event_desc;
	enum iqs62x_event_reg event_reg;
	unsigned long event_flags = 0;
	int ret, i, j;
	u8 event_map[IQS62X_EVENT_SIZE];

	/*
	 * The device asserts the RDY output to signal the beginning of a
	 * communication window, which is closed by an I2C stop condition.
	 * As such, all interrupt status is captured in a single read and
	 * broadcast to any interested sub-device drivers.
	 */
	ret = regmap_raw_read(iqs62x->regmap, IQS62X_SYS_FLAGS, event_map,
			      sizeof(event_map));
	if (ret) {
		dev_err(&client->dev, "Failed to read device status: %d\n",
			ret);
		return IRQ_NONE;
	}

	for (i = 0; i < sizeof(event_map); i++) {
		event_reg = iqs62x->dev_desc->event_regs[iqs62x->ui_sel][i];

		switch (event_reg) {
		case IQS62X_EVENT_UI_LO:
			event_data.ui_data = get_unaligned_le16(&event_map[i]);

			fallthrough;

		case IQS62X_EVENT_UI_HI:
		case IQS62X_EVENT_NONE:
			continue;

		case IQS62X_EVENT_ALS:
			event_data.als_flags = event_map[i];
			continue;

		case IQS62X_EVENT_IR:
			event_data.ir_flags = event_map[i];
			continue;

		case IQS62X_EVENT_INTER:
			event_data.interval = event_map[i];
			continue;

		case IQS62X_EVENT_HYST:
			event_map[i] <<= iqs62x->dev_desc->hyst_shift;

			fallthrough;

		case IQS62X_EVENT_WHEEL:
		case IQS62X_EVENT_HALL:
		case IQS62X_EVENT_PROX:
		case IQS62X_EVENT_SYS:
			break;
		}

		for (j = 0; j < IQS62X_NUM_EVENTS; j++) {
			event_desc = iqs62x_events[j];

			if (event_desc.reg != event_reg)
				continue;

			if ((event_map[i] & event_desc.mask) == event_desc.val)
				event_flags |= BIT(j);
		}
	}

	/*
	 * The device resets itself in response to the I2C master stalling
	 * communication past a fixed timeout. In this case, all registers
	 * are restored and any interested sub-device drivers are notified.
	 */
	if (event_flags & BIT(IQS62X_EVENT_SYS_RESET)) {
		dev_err(&client->dev, "Unexpected device reset\n");

		ret = iqs62x_dev_init(iqs62x);
		if (ret) {
			dev_err(&client->dev,
				"Failed to re-initialize device: %d\n", ret);
			return IRQ_NONE;
		}
	}

	ret = blocking_notifier_call_chain(&iqs62x->nh, event_flags,
					   &event_data);
	if (ret & NOTIFY_STOP_MASK)
		return IRQ_NONE;

	/*
	 * Once the communication window is closed, a small delay is added to
	 * ensure the device's RDY output has been deasserted by the time the
	 * interrupt handler returns.
	 */
	usleep_range(50, 100);

	return IRQ_HANDLED;
}

static void iqs62x_firmware_load(const struct firmware *fw, void *context)
{
	struct iqs62x_core *iqs62x = context;
	struct i2c_client *client = iqs62x->client;
	int ret;

	if (fw) {
		ret = iqs62x_firmware_parse(iqs62x, fw);
		if (ret) {
			dev_err(&client->dev, "Failed to parse firmware: %d\n",
				ret);
			goto err_out;
		}
	}

	ret = iqs62x_dev_init(iqs62x);
	if (ret) {
		dev_err(&client->dev, "Failed to initialize device: %d\n", ret);
		goto err_out;
	}

	ret = devm_request_threaded_irq(&client->dev, client->irq,
					NULL, iqs62x_irq, IRQF_ONESHOT,
					client->name, iqs62x);
	if (ret) {
		dev_err(&client->dev, "Failed to request IRQ: %d\n", ret);
		goto err_out;
	}

	ret = devm_mfd_add_devices(&client->dev, PLATFORM_DEVID_NONE,
				   iqs62x->dev_desc->sub_devs,
				   iqs62x->dev_desc->num_sub_devs,
				   NULL, 0, NULL);
	if (ret)
		dev_err(&client->dev, "Failed to add sub-devices: %d\n", ret);

err_out:
	complete_all(&iqs62x->fw_done);
}

static const struct mfd_cell iqs620at_sub_devs[] = {
	{
		.name = "iqs62x-keys",
		.of_compatible = "azoteq,iqs620a-keys",
	},
	{
		.name = "iqs620a-pwm",
		.of_compatible = "azoteq,iqs620a-pwm",
	},
	{ .name = "iqs620at-temp", },
};

static const struct mfd_cell iqs620a_sub_devs[] = {
	{
		.name = "iqs62x-keys",
		.of_compatible = "azoteq,iqs620a-keys",
	},
	{
		.name = "iqs620a-pwm",
		.of_compatible = "azoteq,iqs620a-pwm",
	},
};

static const struct mfd_cell iqs621_sub_devs[] = {
	{
		.name = "iqs62x-keys",
		.of_compatible = "azoteq,iqs621-keys",
	},
	{ .name = "iqs621-als", },
};

static const struct mfd_cell iqs622_sub_devs[] = {
	{
		.name = "iqs62x-keys",
		.of_compatible = "azoteq,iqs622-keys",
	},
	{ .name = "iqs621-als", },
};

static const struct mfd_cell iqs624_sub_devs[] = {
	{
		.name = "iqs62x-keys",
		.of_compatible = "azoteq,iqs624-keys",
	},
	{ .name = "iqs624-pos", },
};

static const struct mfd_cell iqs625_sub_devs[] = {
	{
		.name = "iqs62x-keys",
		.of_compatible = "azoteq,iqs625-keys",
	},
	{ .name = "iqs624-pos", },
};

static const u8 iqs620at_cal_regs[] = {
	IQS620_TEMP_CAL_MULT,
	IQS620_TEMP_CAL_DIV,
	IQS620_TEMP_CAL_OFFS,
};

static const u8 iqs621_cal_regs[] = {
	IQS621_ALS_CAL_DIV_LUX,
	IQS621_ALS_CAL_DIV_IR,
};

static const enum iqs62x_event_reg iqs620a_event_regs[][IQS62X_EVENT_SIZE] = {
	[IQS62X_UI_PROX] = {
		IQS62X_EVENT_SYS,	/* 0x10 */
		IQS62X_EVENT_NONE,
		IQS62X_EVENT_PROX,	/* 0x12 */
		IQS62X_EVENT_HYST,	/* 0x13 */
		IQS62X_EVENT_NONE,
		IQS62X_EVENT_NONE,
		IQS62X_EVENT_HALL,	/* 0x16 */
		IQS62X_EVENT_NONE,
		IQS62X_EVENT_NONE,
		IQS62X_EVENT_NONE,
	},
	[IQS62X_UI_SAR1] = {
		IQS62X_EVENT_SYS,	/* 0x10 */
		IQS62X_EVENT_NONE,
		IQS62X_EVENT_NONE,
		IQS62X_EVENT_HYST,	/* 0x13 */
		IQS62X_EVENT_NONE,
		IQS62X_EVENT_NONE,
		IQS62X_EVENT_HALL,	/* 0x16 */
		IQS62X_EVENT_NONE,
		IQS62X_EVENT_NONE,
		IQS62X_EVENT_NONE,
	},
};

static const enum iqs62x_event_reg iqs621_event_regs[][IQS62X_EVENT_SIZE] = {
	[IQS62X_UI_PROX] = {
		IQS62X_EVENT_SYS,	/* 0x10 */
		IQS62X_EVENT_NONE,
		IQS62X_EVENT_PROX,	/* 0x12 */
		IQS62X_EVENT_HYST,	/* 0x13 */
		IQS62X_EVENT_NONE,
		IQS62X_EVENT_NONE,
		IQS62X_EVENT_ALS,	/* 0x16 */
		IQS62X_EVENT_UI_LO,	/* 0x17 */
		IQS62X_EVENT_UI_HI,	/* 0x18 */
		IQS62X_EVENT_HALL,	/* 0x19 */
	},
};

static const enum iqs62x_event_reg iqs622_event_regs[][IQS62X_EVENT_SIZE] = {
	[IQS62X_UI_PROX] = {
		IQS62X_EVENT_SYS,	/* 0x10 */
		IQS62X_EVENT_NONE,
		IQS62X_EVENT_PROX,	/* 0x12 */
		IQS62X_EVENT_NONE,
		IQS62X_EVENT_ALS,	/* 0x14 */
		IQS62X_EVENT_NONE,
		IQS62X_EVENT_IR,	/* 0x16 */
		IQS62X_EVENT_UI_LO,	/* 0x17 */
		IQS62X_EVENT_UI_HI,	/* 0x18 */
		IQS62X_EVENT_HALL,	/* 0x19 */
	},
	[IQS62X_UI_SAR1] = {
		IQS62X_EVENT_SYS,	/* 0x10 */
		IQS62X_EVENT_NONE,
		IQS62X_EVENT_NONE,
		IQS62X_EVENT_HYST,	/* 0x13 */
		IQS62X_EVENT_ALS,	/* 0x14 */
		IQS62X_EVENT_NONE,
		IQS62X_EVENT_IR,	/* 0x16 */
		IQS62X_EVENT_UI_LO,	/* 0x17 */
		IQS62X_EVENT_UI_HI,	/* 0x18 */
		IQS62X_EVENT_HALL,	/* 0x19 */
	},
};

static const enum iqs62x_event_reg iqs624_event_regs[][IQS62X_EVENT_SIZE] = {
	[IQS62X_UI_PROX] = {
		IQS62X_EVENT_SYS,	/* 0x10 */
		IQS62X_EVENT_NONE,
		IQS62X_EVENT_PROX,	/* 0x12 */
		IQS62X_EVENT_NONE,
		IQS62X_EVENT_WHEEL,	/* 0x14 */
		IQS62X_EVENT_NONE,
		IQS62X_EVENT_UI_LO,	/* 0x16 */
		IQS62X_EVENT_UI_HI,	/* 0x17 */
		IQS62X_EVENT_INTER,	/* 0x18 */
		IQS62X_EVENT_NONE,
	},
};

static const enum iqs62x_event_reg iqs625_event_regs[][IQS62X_EVENT_SIZE] = {
	[IQS62X_UI_PROX] = {
		IQS62X_EVENT_SYS,	/* 0x10 */
		IQS62X_EVENT_PROX,	/* 0x11 */
		IQS62X_EVENT_INTER,	/* 0x12 */
		IQS62X_EVENT_NONE,
		IQS62X_EVENT_NONE,
		IQS62X_EVENT_NONE,
		IQS62X_EVENT_NONE,
		IQS62X_EVENT_NONE,
		IQS62X_EVENT_NONE,
		IQS62X_EVENT_NONE,
	},
};

static const struct iqs62x_dev_desc iqs62x_devs[] = {
	{
		.dev_name	= "iqs620at",
		.sub_devs	= iqs620at_sub_devs,
		.num_sub_devs	= ARRAY_SIZE(iqs620at_sub_devs),

		.prod_num	= IQS620_PROD_NUM,
		.sw_num		= 0x08,
		.cal_regs	= iqs620at_cal_regs,
		.num_cal_regs	= ARRAY_SIZE(iqs620at_cal_regs),

		.prox_mask	= BIT(0),
		.sar_mask	= BIT(1) | BIT(7),
		.hall_mask	= BIT(2),
		.hyst_mask	= BIT(3),
		.temp_mask	= BIT(4),

		.prox_settings	= IQS620_PROX_SETTINGS_4,
		.hall_flags	= IQS620_HALL_FLAGS,

		.clk_div	= 4,
		.fw_name	= "iqs620a.bin",
		.event_regs	= &iqs620a_event_regs[IQS62X_UI_PROX],
	},
	{
		.dev_name	= "iqs620a",
		.sub_devs	= iqs620a_sub_devs,
		.num_sub_devs	= ARRAY_SIZE(iqs620a_sub_devs),

		.prod_num	= IQS620_PROD_NUM,
		.sw_num		= 0x08,

		.prox_mask	= BIT(0),
		.sar_mask	= BIT(1) | BIT(7),
		.hall_mask	= BIT(2),
		.hyst_mask	= BIT(3),
		.temp_mask	= BIT(4),

		.prox_settings	= IQS620_PROX_SETTINGS_4,
		.hall_flags	= IQS620_HALL_FLAGS,

		.clk_div	= 4,
		.fw_name	= "iqs620a.bin",
		.event_regs	= &iqs620a_event_regs[IQS62X_UI_PROX],
	},
	{
		.dev_name	= "iqs621",
		.sub_devs	= iqs621_sub_devs,
		.num_sub_devs	= ARRAY_SIZE(iqs621_sub_devs),

		.prod_num	= IQS621_PROD_NUM,
		.sw_num		= 0x09,
		.cal_regs	= iqs621_cal_regs,
		.num_cal_regs	= ARRAY_SIZE(iqs621_cal_regs),

		.prox_mask	= BIT(0),
		.hall_mask	= BIT(1),
		.als_mask	= BIT(2),
		.hyst_mask	= BIT(3),
		.temp_mask	= BIT(4),

		.als_flags	= IQS621_ALS_FLAGS,
		.hall_flags	= IQS621_HALL_FLAGS,
		.hyst_shift	= 5,

		.clk_div	= 2,
		.fw_name	= "iqs621.bin",
		.event_regs	= &iqs621_event_regs[IQS62X_UI_PROX],
	},
	{
		.dev_name	= "iqs622",
		.sub_devs	= iqs622_sub_devs,
		.num_sub_devs	= ARRAY_SIZE(iqs622_sub_devs),

		.prod_num	= IQS622_PROD_NUM,
		.sw_num		= 0x06,

		.prox_mask	= BIT(0),
		.sar_mask	= BIT(1),
		.hall_mask	= BIT(2),
		.als_mask	= BIT(3),
		.ir_mask	= BIT(4),

		.prox_settings	= IQS622_PROX_SETTINGS_4,
		.als_flags	= IQS622_ALS_FLAGS,
		.hall_flags	= IQS622_HALL_FLAGS,

		.clk_div	= 2,
		.fw_name	= "iqs622.bin",
		.event_regs	= &iqs622_event_regs[IQS62X_UI_PROX],
	},
	{
		.dev_name	= "iqs624",
		.sub_devs	= iqs624_sub_devs,
		.num_sub_devs	= ARRAY_SIZE(iqs624_sub_devs),

		.prod_num	= IQS624_PROD_NUM,
		.sw_num		= 0x0B,

		.interval	= IQS624_INTERVAL_NUM,
		.interval_div	= 3,

		.clk_div	= 2,
		.fw_name	= "iqs624.bin",
		.event_regs	= &iqs624_event_regs[IQS62X_UI_PROX],
	},
	{
		.dev_name	= "iqs625",
		.sub_devs	= iqs625_sub_devs,
		.num_sub_devs	= ARRAY_SIZE(iqs625_sub_devs),

		.prod_num	= IQS625_PROD_NUM,
		.sw_num		= 0x0B,

		.interval	= IQS625_INTERVAL_NUM,
		.interval_div	= 10,

		.clk_div	= 2,
		.fw_name	= "iqs625.bin",
		.event_regs	= &iqs625_event_regs[IQS62X_UI_PROX],
	},
};

static const struct regmap_config iqs62x_map_config = {
	.reg_bits = 8,
	.val_bits = 8,
	.max_register = IQS62X_MAX_REG,
};

static int iqs62x_probe(struct i2c_client *client)
{
	struct iqs62x_core *iqs62x;
	struct iqs62x_info info;
	unsigned int val;
	int ret, i, j;
	u8 sw_num = 0;
	const char *fw_name = NULL;

	iqs62x = devm_kzalloc(&client->dev, sizeof(*iqs62x), GFP_KERNEL);
	if (!iqs62x)
		return -ENOMEM;

	i2c_set_clientdata(client, iqs62x);
	iqs62x->client = client;

	BLOCKING_INIT_NOTIFIER_HEAD(&iqs62x->nh);
	INIT_LIST_HEAD(&iqs62x->fw_blk_head);
	init_completion(&iqs62x->fw_done);

	iqs62x->regmap = devm_regmap_init_i2c(client, &iqs62x_map_config);
	if (IS_ERR(iqs62x->regmap)) {
		ret = PTR_ERR(iqs62x->regmap);
		dev_err(&client->dev, "Failed to initialize register map: %d\n",
			ret);
		return ret;
	}

	ret = regmap_raw_read(iqs62x->regmap, IQS62X_PROD_NUM, &info,
			      sizeof(info));
	if (ret)
		return ret;

	/*
	 * The following sequence validates the device's product and software
	 * numbers. It then determines if the device is factory-calibrated by
	 * checking for nonzero values in the device's designated calibration
	 * registers (if applicable). Depending on the device, the absence of
	 * calibration data indicates a reduced feature set or invalid device.
	 *
	 * For devices given in both calibrated and uncalibrated versions, the
	 * calibrated version (e.g. IQS620AT) appears first in the iqs62x_devs
	 * array. The uncalibrated version (e.g. IQS620A) appears next and has
	 * the same product and software numbers, but no calibration registers
	 * are specified.
	 */
	for (i = 0; i < ARRAY_SIZE(iqs62x_devs); i++) {
		if (info.prod_num != iqs62x_devs[i].prod_num)
			continue;

		iqs62x->dev_desc = &iqs62x_devs[i];

		if (info.sw_num < iqs62x->dev_desc->sw_num)
			continue;

		sw_num = info.sw_num;

		/*
		 * Read each of the device's designated calibration registers,
		 * if any, and exit from the inner loop early if any are equal
		 * to zero (indicating the device is uncalibrated). This could
		 * be acceptable depending on the device (e.g. IQS620A instead
		 * of IQS620AT).
		 */
		for (j = 0; j < iqs62x->dev_desc->num_cal_regs; j++) {
			ret = regmap_read(iqs62x->regmap,
					  iqs62x->dev_desc->cal_regs[j], &val);
			if (ret)
				return ret;

			if (!val)
				break;
		}

		/*
		 * If the number of nonzero values read from the device equals
		 * the number of designated calibration registers (which could
		 * be zero), exit from the outer loop early to signal that the
		 * device's product and software numbers match a known device,
		 * and the device is calibrated (if applicable).
		 */
		if (j == iqs62x->dev_desc->num_cal_regs)
			break;
	}

	if (!iqs62x->dev_desc) {
		dev_err(&client->dev, "Unrecognized product number: 0x%02X\n",
			info.prod_num);
		return -EINVAL;
	}

	if (!sw_num) {
		dev_err(&client->dev, "Unrecognized software number: 0x%02X\n",
			info.sw_num);
		return -EINVAL;
	}

	if (i == ARRAY_SIZE(iqs62x_devs)) {
		dev_err(&client->dev, "Uncalibrated device\n");
		return -ENODATA;
	}

	device_property_read_string(&client->dev, "firmware-name", &fw_name);

	ret = request_firmware_nowait(THIS_MODULE, FW_ACTION_HOTPLUG,
				      fw_name ? : iqs62x->dev_desc->fw_name,
				      &client->dev, GFP_KERNEL, iqs62x,
				      iqs62x_firmware_load);
	if (ret)
		dev_err(&client->dev, "Failed to request firmware: %d\n", ret);

	return ret;
}

static int iqs62x_remove(struct i2c_client *client)
{
	struct iqs62x_core *iqs62x = i2c_get_clientdata(client);

	wait_for_completion(&iqs62x->fw_done);

	return 0;
}

static int __maybe_unused iqs62x_suspend(struct device *dev)
{
	struct iqs62x_core *iqs62x = dev_get_drvdata(dev);
	int ret;

	wait_for_completion(&iqs62x->fw_done);

	/*
	 * As per the datasheet, automatic mode switching must be disabled
	 * before the device is placed in or taken out of halt mode.
	 */
	ret = regmap_update_bits(iqs62x->regmap, IQS62X_PWR_SETTINGS,
				 IQS62X_PWR_SETTINGS_DIS_AUTO, 0xFF);
	if (ret)
		return ret;

	return regmap_update_bits(iqs62x->regmap, IQS62X_PWR_SETTINGS,
				  IQS62X_PWR_SETTINGS_PWR_MODE_MASK,
				  IQS62X_PWR_SETTINGS_PWR_MODE_HALT);
}

static int __maybe_unused iqs62x_resume(struct device *dev)
{
	struct iqs62x_core *iqs62x = dev_get_drvdata(dev);
	int ret;

	ret = regmap_update_bits(iqs62x->regmap, IQS62X_PWR_SETTINGS,
				 IQS62X_PWR_SETTINGS_PWR_MODE_MASK,
				 IQS62X_PWR_SETTINGS_PWR_MODE_NORM);
	if (ret)
		return ret;

	return regmap_update_bits(iqs62x->regmap, IQS62X_PWR_SETTINGS,
				  IQS62X_PWR_SETTINGS_DIS_AUTO, 0);
}

static SIMPLE_DEV_PM_OPS(iqs62x_pm, iqs62x_suspend, iqs62x_resume);

static const struct of_device_id iqs62x_of_match[] = {
	{ .compatible = "azoteq,iqs620a" },
	{ .compatible = "azoteq,iqs621" },
	{ .compatible = "azoteq,iqs622" },
	{ .compatible = "azoteq,iqs624" },
	{ .compatible = "azoteq,iqs625" },
	{ }
};
MODULE_DEVICE_TABLE(of, iqs62x_of_match);

static struct i2c_driver iqs62x_i2c_driver = {
	.driver = {
		.name = "iqs62x",
		.of_match_table = iqs62x_of_match,
		.pm = &iqs62x_pm,
	},
	.probe_new = iqs62x_probe,
	.remove = iqs62x_remove,
};
module_i2c_driver(iqs62x_i2c_driver);

MODULE_AUTHOR("Jeff LaBundy <jeff@labundy.com>");
MODULE_DESCRIPTION("Azoteq IQS620A/621/622/624/625 Multi-Function Sensors");
MODULE_LICENSE("GPL");
