// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2013 Samsung Electronics Co., Ltd.
 * Author: Jacek Anaszewski <j.anaszewski@samsung.com>
 *
 * IIO features supported by the driver:
 *
 * Read-only raw channels:
 *   - illuminance_clear [lux]
 *   - illuminance_ir
 *   - proximity
 *
 * Triggered buffer:
 *   - illuminance_clear
 *   - illuminance_ir
 *   - proximity
 *
 * Events:
 *   - illuminance_clear (rising and falling)
 *   - proximity (rising and falling)
 *     - both falling and rising thresholds for the proximity events
 *       must be set to the values greater than 0.
 *
 * The driver supports triggered buffers for all the three
 * channels as well as high and low threshold events for the
 * illuminance_clear and proxmimity channels. Triggers
 * can be enabled simultaneously with both illuminance_clear
 * events. Proximity events cannot be enabled simultaneously
 * with any triggers or illuminance events. Enabling/disabling
 * one of the proximity events automatically enables/disables
 * the other one.
 */

#include <linux/debugfs.h>
#include <linux/delay.h>
#include <linux/i2c.h>
#include <linux/interrupt.h>
#include <linux/irq.h>
#include <linux/irq_work.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/of.h>
#include <linux/regmap.h>
#include <linux/regulator/consumer.h>
#include <linux/slab.h>
#include <asm/unaligned.h>
#include <linux/iio/buffer.h>
#include <linux/iio/events.h>
#include <linux/iio/iio.h>
#include <linux/iio/sysfs.h>
#include <linux/iio/trigger.h>
#include <linux/iio/trigger_consumer.h>
#include <linux/iio/triggered_buffer.h>

#define GP2A_I2C_NAME "gp2ap020a00f"

/* Registers */
#define GP2AP020A00F_OP_REG	0x00 /* Basic operations */
#define GP2AP020A00F_ALS_REG	0x01 /* ALS related settings */
#define GP2AP020A00F_PS_REG	0x02 /* PS related settings */
#define GP2AP020A00F_LED_REG	0x03 /* LED reg */
#define GP2AP020A00F_TL_L_REG	0x04 /* ALS: Threshold low LSB */
#define GP2AP020A00F_TL_H_REG	0x05 /* ALS: Threshold low MSB */
#define GP2AP020A00F_TH_L_REG	0x06 /* ALS: Threshold high LSB */
#define GP2AP020A00F_TH_H_REG	0x07 /* ALS: Threshold high MSB */
#define GP2AP020A00F_PL_L_REG	0x08 /* PS: Threshold low LSB */
#define GP2AP020A00F_PL_H_REG	0x09 /* PS: Threshold low MSB */
#define GP2AP020A00F_PH_L_REG	0x0a /* PS: Threshold high LSB */
#define GP2AP020A00F_PH_H_REG	0x0b /* PS: Threshold high MSB */
#define GP2AP020A00F_D0_L_REG	0x0c /* ALS result: Clear/Illuminance LSB */
#define GP2AP020A00F_D0_H_REG	0x0d /* ALS result: Clear/Illuminance MSB */
#define GP2AP020A00F_D1_L_REG	0x0e /* ALS result: IR LSB */
#define GP2AP020A00F_D1_H_REG	0x0f /* ALS result: IR LSB */
#define GP2AP020A00F_D2_L_REG	0x10 /* PS result LSB */
#define GP2AP020A00F_D2_H_REG	0x11 /* PS result MSB */
#define GP2AP020A00F_NUM_REGS	0x12 /* Number of registers */

/* OP_REG bits */
#define GP2AP020A00F_OP3_MASK		0x80 /* Software shutdown */
#define GP2AP020A00F_OP3_SHUTDOWN	0x00
#define GP2AP020A00F_OP3_OPERATION	0x80
#define GP2AP020A00F_OP2_MASK		0x40 /* Auto shutdown/Continuous mode */
#define GP2AP020A00F_OP2_AUTO_SHUTDOWN	0x00
#define GP2AP020A00F_OP2_CONT_OPERATION	0x40
#define GP2AP020A00F_OP_MASK		0x30 /* Operating mode selection  */
#define GP2AP020A00F_OP_ALS_AND_PS	0x00
#define GP2AP020A00F_OP_ALS		0x10
#define GP2AP020A00F_OP_PS		0x20
#define GP2AP020A00F_OP_DEBUG		0x30
#define GP2AP020A00F_PROX_MASK		0x08 /* PS: detection/non-detection */
#define GP2AP020A00F_PROX_NON_DETECT	0x00
#define GP2AP020A00F_PROX_DETECT	0x08
#define GP2AP020A00F_FLAG_P		0x04 /* PS: interrupt result  */
#define GP2AP020A00F_FLAG_A		0x02 /* ALS: interrupt result  */
#define GP2AP020A00F_TYPE_MASK		0x01 /* Output data type selection */
#define GP2AP020A00F_TYPE_MANUAL_CALC	0x00
#define GP2AP020A00F_TYPE_AUTO_CALC	0x01

/* ALS_REG bits */
#define GP2AP020A00F_PRST_MASK		0xc0 /* Number of measurement cycles */
#define GP2AP020A00F_PRST_ONCE		0x00
#define GP2AP020A00F_PRST_4_CYCLES	0x40
#define GP2AP020A00F_PRST_8_CYCLES	0x80
#define GP2AP020A00F_PRST_16_CYCLES	0xc0
#define GP2AP020A00F_RES_A_MASK		0x38 /* ALS: Resolution */
#define GP2AP020A00F_RES_A_800ms	0x00
#define GP2AP020A00F_RES_A_400ms	0x08
#define GP2AP020A00F_RES_A_200ms	0x10
#define GP2AP020A00F_RES_A_100ms	0x18
#define GP2AP020A00F_RES_A_25ms		0x20
#define GP2AP020A00F_RES_A_6_25ms	0x28
#define GP2AP020A00F_RES_A_1_56ms	0x30
#define GP2AP020A00F_RES_A_0_39ms	0x38
#define GP2AP020A00F_RANGE_A_MASK	0x07 /* ALS: Max measurable range */
#define GP2AP020A00F_RANGE_A_x1		0x00
#define GP2AP020A00F_RANGE_A_x2		0x01
#define GP2AP020A00F_RANGE_A_x4		0x02
#define GP2AP020A00F_RANGE_A_x8		0x03
#define GP2AP020A00F_RANGE_A_x16	0x04
#define GP2AP020A00F_RANGE_A_x32	0x05
#define GP2AP020A00F_RANGE_A_x64	0x06
#define GP2AP020A00F_RANGE_A_x128	0x07

/* PS_REG bits */
#define GP2AP020A00F_ALC_MASK		0x80 /* Auto light cancel */
#define GP2AP020A00F_ALC_ON		0x80
#define GP2AP020A00F_ALC_OFF		0x00
#define GP2AP020A00F_INTTYPE_MASK	0x40 /* Interrupt type setting */
#define GP2AP020A00F_INTTYPE_LEVEL	0x00
#define GP2AP020A00F_INTTYPE_PULSE	0x40
#define GP2AP020A00F_RES_P_MASK		0x38 /* PS: Resolution */
#define GP2AP020A00F_RES_P_800ms_x2	0x00
#define GP2AP020A00F_RES_P_400ms_x2	0x08
#define GP2AP020A00F_RES_P_200ms_x2	0x10
#define GP2AP020A00F_RES_P_100ms_x2	0x18
#define GP2AP020A00F_RES_P_25ms_x2	0x20
#define GP2AP020A00F_RES_P_6_25ms_x2	0x28
#define GP2AP020A00F_RES_P_1_56ms_x2	0x30
#define GP2AP020A00F_RES_P_0_39ms_x2	0x38
#define GP2AP020A00F_RANGE_P_MASK	0x07 /* PS: Max measurable range */
#define GP2AP020A00F_RANGE_P_x1		0x00
#define GP2AP020A00F_RANGE_P_x2		0x01
#define GP2AP020A00F_RANGE_P_x4		0x02
#define GP2AP020A00F_RANGE_P_x8		0x03
#define GP2AP020A00F_RANGE_P_x16	0x04
#define GP2AP020A00F_RANGE_P_x32	0x05
#define GP2AP020A00F_RANGE_P_x64	0x06
#define GP2AP020A00F_RANGE_P_x128	0x07

/* LED reg bits */
#define GP2AP020A00F_INTVAL_MASK	0xc0 /* Intermittent operating */
#define GP2AP020A00F_INTVAL_0		0x00
#define GP2AP020A00F_INTVAL_4		0x40
#define GP2AP020A00F_INTVAL_8		0x80
#define GP2AP020A00F_INTVAL_16		0xc0
#define GP2AP020A00F_IS_MASK		0x30 /* ILED drive peak current */
#define GP2AP020A00F_IS_13_8mA		0x00
#define GP2AP020A00F_IS_27_5mA		0x10
#define GP2AP020A00F_IS_55mA		0x20
#define GP2AP020A00F_IS_110mA		0x30
#define GP2AP020A00F_PIN_MASK		0x0c /* INT terminal setting */
#define GP2AP020A00F_PIN_ALS_OR_PS	0x00
#define GP2AP020A00F_PIN_ALS		0x04
#define GP2AP020A00F_PIN_PS		0x08
#define GP2AP020A00F_PIN_PS_DETECT	0x0c
#define GP2AP020A00F_FREQ_MASK		0x02 /* LED modulation frequency */
#define GP2AP020A00F_FREQ_327_5kHz	0x00
#define GP2AP020A00F_FREQ_81_8kHz	0x02
#define GP2AP020A00F_RST		0x01 /* Software reset */

#define GP2AP020A00F_SCAN_MODE_LIGHT_CLEAR	0
#define GP2AP020A00F_SCAN_MODE_LIGHT_IR		1
#define GP2AP020A00F_SCAN_MODE_PROXIMITY	2
#define GP2AP020A00F_CHAN_TIMESTAMP		3

#define GP2AP020A00F_DATA_READY_TIMEOUT		msecs_to_jiffies(1000)
#define GP2AP020A00F_DATA_REG(chan)		(GP2AP020A00F_D0_L_REG + \
							(chan) * 2)
#define GP2AP020A00F_THRESH_REG(th_val_id)	(GP2AP020A00F_TL_L_REG + \
							(th_val_id) * 2)
#define GP2AP020A00F_THRESH_VAL_ID(reg_addr)	((reg_addr - 4) / 2)

#define GP2AP020A00F_SUBTRACT_MODE	0
#define GP2AP020A00F_ADD_MODE		1

#define GP2AP020A00F_MAX_CHANNELS	3

enum gp2ap020a00f_opmode {
	GP2AP020A00F_OPMODE_READ_RAW_CLEAR,
	GP2AP020A00F_OPMODE_READ_RAW_IR,
	GP2AP020A00F_OPMODE_READ_RAW_PROXIMITY,
	GP2AP020A00F_OPMODE_ALS,
	GP2AP020A00F_OPMODE_PS,
	GP2AP020A00F_OPMODE_ALS_AND_PS,
	GP2AP020A00F_OPMODE_PROX_DETECT,
	GP2AP020A00F_OPMODE_SHUTDOWN,
	GP2AP020A00F_NUM_OPMODES,
};

enum gp2ap020a00f_cmd {
	GP2AP020A00F_CMD_READ_RAW_CLEAR,
	GP2AP020A00F_CMD_READ_RAW_IR,
	GP2AP020A00F_CMD_READ_RAW_PROXIMITY,
	GP2AP020A00F_CMD_TRIGGER_CLEAR_EN,
	GP2AP020A00F_CMD_TRIGGER_CLEAR_DIS,
	GP2AP020A00F_CMD_TRIGGER_IR_EN,
	GP2AP020A00F_CMD_TRIGGER_IR_DIS,
	GP2AP020A00F_CMD_TRIGGER_PROX_EN,
	GP2AP020A00F_CMD_TRIGGER_PROX_DIS,
	GP2AP020A00F_CMD_ALS_HIGH_EV_EN,
	GP2AP020A00F_CMD_ALS_HIGH_EV_DIS,
	GP2AP020A00F_CMD_ALS_LOW_EV_EN,
	GP2AP020A00F_CMD_ALS_LOW_EV_DIS,
	GP2AP020A00F_CMD_PROX_HIGH_EV_EN,
	GP2AP020A00F_CMD_PROX_HIGH_EV_DIS,
	GP2AP020A00F_CMD_PROX_LOW_EV_EN,
	GP2AP020A00F_CMD_PROX_LOW_EV_DIS,
};

enum gp2ap020a00f_flags {
	GP2AP020A00F_FLAG_ALS_CLEAR_TRIGGER,
	GP2AP020A00F_FLAG_ALS_IR_TRIGGER,
	GP2AP020A00F_FLAG_PROX_TRIGGER,
	GP2AP020A00F_FLAG_PROX_RISING_EV,
	GP2AP020A00F_FLAG_PROX_FALLING_EV,
	GP2AP020A00F_FLAG_ALS_RISING_EV,
	GP2AP020A00F_FLAG_ALS_FALLING_EV,
	GP2AP020A00F_FLAG_LUX_MODE_HI,
	GP2AP020A00F_FLAG_DATA_READY,
};

enum gp2ap020a00f_thresh_val_id {
	GP2AP020A00F_THRESH_TL,
	GP2AP020A00F_THRESH_TH,
	GP2AP020A00F_THRESH_PL,
	GP2AP020A00F_THRESH_PH,
};

struct gp2ap020a00f_data {
	const struct gp2ap020a00f_platform_data *pdata;
	struct i2c_client *client;
	struct mutex lock;
	char *buffer;
	struct regulator *vled_reg;
	unsigned long flags;
	enum gp2ap020a00f_opmode cur_opmode;
	struct iio_trigger *trig;
	struct regmap *regmap;
	unsigned int thresh_val[4];
	u8 debug_reg_addr;
	struct irq_work work;
	wait_queue_head_t data_ready_queue;
};

static const u8 gp2ap020a00f_reg_init_tab[] = {
	[GP2AP020A00F_OP_REG] = GP2AP020A00F_OP3_SHUTDOWN,
	[GP2AP020A00F_ALS_REG] = GP2AP020A00F_RES_A_25ms |
				 GP2AP020A00F_RANGE_A_x8,
	[GP2AP020A00F_PS_REG] = GP2AP020A00F_ALC_ON |
				GP2AP020A00F_RES_P_1_56ms_x2 |
				GP2AP020A00F_RANGE_P_x4,
	[GP2AP020A00F_LED_REG] = GP2AP020A00F_INTVAL_0 |
				 GP2AP020A00F_IS_110mA |
				 GP2AP020A00F_FREQ_327_5kHz,
	[GP2AP020A00F_TL_L_REG] = 0,
	[GP2AP020A00F_TL_H_REG] = 0,
	[GP2AP020A00F_TH_L_REG] = 0,
	[GP2AP020A00F_TH_H_REG] = 0,
	[GP2AP020A00F_PL_L_REG] = 0,
	[GP2AP020A00F_PL_H_REG] = 0,
	[GP2AP020A00F_PH_L_REG] = 0,
	[GP2AP020A00F_PH_H_REG] = 0,
};

static bool gp2ap020a00f_is_volatile_reg(struct device *dev, unsigned int reg)
{
	switch (reg) {
	case GP2AP020A00F_OP_REG:
	case GP2AP020A00F_D0_L_REG:
	case GP2AP020A00F_D0_H_REG:
	case GP2AP020A00F_D1_L_REG:
	case GP2AP020A00F_D1_H_REG:
	case GP2AP020A00F_D2_L_REG:
	case GP2AP020A00F_D2_H_REG:
		return true;
	default:
		return false;
	}
}

static const struct regmap_config gp2ap020a00f_regmap_config = {
	.reg_bits = 8,
	.val_bits = 8,

	.max_register = GP2AP020A00F_D2_H_REG,
	.cache_type = REGCACHE_RBTREE,

	.volatile_reg = gp2ap020a00f_is_volatile_reg,
};

static const struct gp2ap020a00f_mutable_config_regs {
	u8 op_reg;
	u8 als_reg;
	u8 ps_reg;
	u8 led_reg;
} opmode_regs_settings[GP2AP020A00F_NUM_OPMODES] = {
	[GP2AP020A00F_OPMODE_READ_RAW_CLEAR] = {
		GP2AP020A00F_OP_ALS | GP2AP020A00F_OP2_CONT_OPERATION
		| GP2AP020A00F_OP3_OPERATION
		| GP2AP020A00F_TYPE_AUTO_CALC,
		GP2AP020A00F_PRST_ONCE,
		GP2AP020A00F_INTTYPE_LEVEL,
		GP2AP020A00F_PIN_ALS
	},
	[GP2AP020A00F_OPMODE_READ_RAW_IR] = {
		GP2AP020A00F_OP_ALS | GP2AP020A00F_OP2_CONT_OPERATION
		| GP2AP020A00F_OP3_OPERATION
		| GP2AP020A00F_TYPE_MANUAL_CALC,
		GP2AP020A00F_PRST_ONCE,
		GP2AP020A00F_INTTYPE_LEVEL,
		GP2AP020A00F_PIN_ALS
	},
	[GP2AP020A00F_OPMODE_READ_RAW_PROXIMITY] = {
		GP2AP020A00F_OP_PS | GP2AP020A00F_OP2_CONT_OPERATION
		| GP2AP020A00F_OP3_OPERATION
		| GP2AP020A00F_TYPE_MANUAL_CALC,
		GP2AP020A00F_PRST_ONCE,
		GP2AP020A00F_INTTYPE_LEVEL,
		GP2AP020A00F_PIN_PS
	},
	[GP2AP020A00F_OPMODE_PROX_DETECT] = {
		GP2AP020A00F_OP_PS | GP2AP020A00F_OP2_CONT_OPERATION
		| GP2AP020A00F_OP3_OPERATION
		| GP2AP020A00F_TYPE_MANUAL_CALC,
		GP2AP020A00F_PRST_4_CYCLES,
		GP2AP020A00F_INTTYPE_PULSE,
		GP2AP020A00F_PIN_PS_DETECT
	},
	[GP2AP020A00F_OPMODE_ALS] = {
		GP2AP020A00F_OP_ALS | GP2AP020A00F_OP2_CONT_OPERATION
		| GP2AP020A00F_OP3_OPERATION
		| GP2AP020A00F_TYPE_AUTO_CALC,
		GP2AP020A00F_PRST_ONCE,
		GP2AP020A00F_INTTYPE_LEVEL,
		GP2AP020A00F_PIN_ALS
	},
	[GP2AP020A00F_OPMODE_PS] = {
		GP2AP020A00F_OP_PS | GP2AP020A00F_OP2_CONT_OPERATION
		| GP2AP020A00F_OP3_OPERATION
		| GP2AP020A00F_TYPE_MANUAL_CALC,
		GP2AP020A00F_PRST_4_CYCLES,
		GP2AP020A00F_INTTYPE_LEVEL,
		GP2AP020A00F_PIN_PS
	},
	[GP2AP020A00F_OPMODE_ALS_AND_PS] = {
		GP2AP020A00F_OP_ALS_AND_PS
		| GP2AP020A00F_OP2_CONT_OPERATION
		| GP2AP020A00F_OP3_OPERATION
		| GP2AP020A00F_TYPE_AUTO_CALC,
		GP2AP020A00F_PRST_4_CYCLES,
		GP2AP020A00F_INTTYPE_LEVEL,
		GP2AP020A00F_PIN_ALS_OR_PS
	},
	[GP2AP020A00F_OPMODE_SHUTDOWN] = { GP2AP020A00F_OP3_SHUTDOWN, },
};

static int gp2ap020a00f_set_operation_mode(struct gp2ap020a00f_data *data,
					enum gp2ap020a00f_opmode op)
{
	unsigned int op_reg_val;
	int err;

	if (op != GP2AP020A00F_OPMODE_SHUTDOWN) {
		err = regmap_read(data->regmap, GP2AP020A00F_OP_REG,
					&op_reg_val);
		if (err < 0)
			return err;
		/*
		 * Shutdown the device if the operation being executed entails
		 * mode transition.
		 */
		if ((opmode_regs_settings[op].op_reg & GP2AP020A00F_OP_MASK) !=
		    (op_reg_val & GP2AP020A00F_OP_MASK)) {
			/* set shutdown mode */
			err = regmap_update_bits(data->regmap,
				GP2AP020A00F_OP_REG, GP2AP020A00F_OP3_MASK,
				GP2AP020A00F_OP3_SHUTDOWN);
			if (err < 0)
				return err;
		}

		err = regmap_update_bits(data->regmap, GP2AP020A00F_ALS_REG,
			GP2AP020A00F_PRST_MASK, opmode_regs_settings[op]
								.als_reg);
		if (err < 0)
			return err;

		err = regmap_update_bits(data->regmap, GP2AP020A00F_PS_REG,
			GP2AP020A00F_INTTYPE_MASK, opmode_regs_settings[op]
								.ps_reg);
		if (err < 0)
			return err;

		err = regmap_update_bits(data->regmap, GP2AP020A00F_LED_REG,
			GP2AP020A00F_PIN_MASK, opmode_regs_settings[op]
								.led_reg);
		if (err < 0)
			return err;
	}

	/* Set OP_REG and apply operation mode (power on / off) */
	err = regmap_update_bits(data->regmap,
				 GP2AP020A00F_OP_REG,
				 GP2AP020A00F_OP_MASK | GP2AP020A00F_OP2_MASK |
				 GP2AP020A00F_OP3_MASK | GP2AP020A00F_TYPE_MASK,
				 opmode_regs_settings[op].op_reg);
	if (err < 0)
		return err;

	data->cur_opmode = op;

	return 0;
}

static bool gp2ap020a00f_als_enabled(struct gp2ap020a00f_data *data)
{
	return test_bit(GP2AP020A00F_FLAG_ALS_CLEAR_TRIGGER, &data->flags) ||
	       test_bit(GP2AP020A00F_FLAG_ALS_IR_TRIGGER, &data->flags) ||
	       test_bit(GP2AP020A00F_FLAG_ALS_RISING_EV, &data->flags) ||
	       test_bit(GP2AP020A00F_FLAG_ALS_FALLING_EV, &data->flags);
}

static bool gp2ap020a00f_prox_detect_enabled(struct gp2ap020a00f_data *data)
{
	return test_bit(GP2AP020A00F_FLAG_PROX_RISING_EV, &data->flags) ||
	       test_bit(GP2AP020A00F_FLAG_PROX_FALLING_EV, &data->flags);
}

static int gp2ap020a00f_write_event_threshold(struct gp2ap020a00f_data *data,
				enum gp2ap020a00f_thresh_val_id th_val_id,
				bool enable)
{
	__le16 thresh_buf = 0;
	unsigned int thresh_reg_val;

	if (!enable)
		thresh_reg_val = 0;
	else if (test_bit(GP2AP020A00F_FLAG_LUX_MODE_HI, &data->flags) &&
		 th_val_id != GP2AP020A00F_THRESH_PL &&
		 th_val_id != GP2AP020A00F_THRESH_PH)
		/*
		 * For the high lux mode ALS threshold has to be scaled down
		 * to allow for proper comparison with the output value.
		 */
		thresh_reg_val = data->thresh_val[th_val_id] / 16;
	else
		thresh_reg_val = data->thresh_val[th_val_id] > 16000 ?
					16000 :
					data->thresh_val[th_val_id];

	thresh_buf = cpu_to_le16(thresh_reg_val);

	return regmap_bulk_write(data->regmap,
				 GP2AP020A00F_THRESH_REG(th_val_id),
				 (u8 *)&thresh_buf, 2);
}

static int gp2ap020a00f_alter_opmode(struct gp2ap020a00f_data *data,
			enum gp2ap020a00f_opmode diff_mode, int add_sub)
{
	enum gp2ap020a00f_opmode new_mode;

	if (diff_mode != GP2AP020A00F_OPMODE_ALS &&
	    diff_mode != GP2AP020A00F_OPMODE_PS)
		return -EINVAL;

	if (add_sub == GP2AP020A00F_ADD_MODE) {
		if (data->cur_opmode == GP2AP020A00F_OPMODE_SHUTDOWN)
			new_mode =  diff_mode;
		else
			new_mode = GP2AP020A00F_OPMODE_ALS_AND_PS;
	} else {
		if (data->cur_opmode == GP2AP020A00F_OPMODE_ALS_AND_PS)
			new_mode = (diff_mode == GP2AP020A00F_OPMODE_ALS) ?
					GP2AP020A00F_OPMODE_PS :
					GP2AP020A00F_OPMODE_ALS;
		else
			new_mode = GP2AP020A00F_OPMODE_SHUTDOWN;
	}

	return gp2ap020a00f_set_operation_mode(data, new_mode);
}

static int gp2ap020a00f_exec_cmd(struct gp2ap020a00f_data *data,
					enum gp2ap020a00f_cmd cmd)
{
	int err = 0;

	switch (cmd) {
	case GP2AP020A00F_CMD_READ_RAW_CLEAR:
		if (data->cur_opmode != GP2AP020A00F_OPMODE_SHUTDOWN)
			return -EBUSY;
		err = gp2ap020a00f_set_operation_mode(data,
					GP2AP020A00F_OPMODE_READ_RAW_CLEAR);
		break;
	case GP2AP020A00F_CMD_READ_RAW_IR:
		if (data->cur_opmode != GP2AP020A00F_OPMODE_SHUTDOWN)
			return -EBUSY;
		err = gp2ap020a00f_set_operation_mode(data,
					GP2AP020A00F_OPMODE_READ_RAW_IR);
		break;
	case GP2AP020A00F_CMD_READ_RAW_PROXIMITY:
		if (data->cur_opmode != GP2AP020A00F_OPMODE_SHUTDOWN)
			return -EBUSY;
		err = gp2ap020a00f_set_operation_mode(data,
					GP2AP020A00F_OPMODE_READ_RAW_PROXIMITY);
		break;
	case GP2AP020A00F_CMD_TRIGGER_CLEAR_EN:
		if (data->cur_opmode == GP2AP020A00F_OPMODE_PROX_DETECT)
			return -EBUSY;
		if (!gp2ap020a00f_als_enabled(data))
			err = gp2ap020a00f_alter_opmode(data,
						GP2AP020A00F_OPMODE_ALS,
						GP2AP020A00F_ADD_MODE);
		set_bit(GP2AP020A00F_FLAG_ALS_CLEAR_TRIGGER, &data->flags);
		break;
	case GP2AP020A00F_CMD_TRIGGER_CLEAR_DIS:
		clear_bit(GP2AP020A00F_FLAG_ALS_CLEAR_TRIGGER, &data->flags);
		if (gp2ap020a00f_als_enabled(data))
			break;
		err = gp2ap020a00f_alter_opmode(data,
						GP2AP020A00F_OPMODE_ALS,
						GP2AP020A00F_SUBTRACT_MODE);
		break;
	case GP2AP020A00F_CMD_TRIGGER_IR_EN:
		if (data->cur_opmode == GP2AP020A00F_OPMODE_PROX_DETECT)
			return -EBUSY;
		if (!gp2ap020a00f_als_enabled(data))
			err = gp2ap020a00f_alter_opmode(data,
						GP2AP020A00F_OPMODE_ALS,
						GP2AP020A00F_ADD_MODE);
		set_bit(GP2AP020A00F_FLAG_ALS_IR_TRIGGER, &data->flags);
		break;
	case GP2AP020A00F_CMD_TRIGGER_IR_DIS:
		clear_bit(GP2AP020A00F_FLAG_ALS_IR_TRIGGER, &data->flags);
		if (gp2ap020a00f_als_enabled(data))
			break;
		err = gp2ap020a00f_alter_opmode(data,
						GP2AP020A00F_OPMODE_ALS,
						GP2AP020A00F_SUBTRACT_MODE);
		break;
	case GP2AP020A00F_CMD_TRIGGER_PROX_EN:
		if (data->cur_opmode == GP2AP020A00F_OPMODE_PROX_DETECT)
			return -EBUSY;
		err = gp2ap020a00f_alter_opmode(data,
						GP2AP020A00F_OPMODE_PS,
						GP2AP020A00F_ADD_MODE);
		set_bit(GP2AP020A00F_FLAG_PROX_TRIGGER, &data->flags);
		break;
	case GP2AP020A00F_CMD_TRIGGER_PROX_DIS:
		clear_bit(GP2AP020A00F_FLAG_PROX_TRIGGER, &data->flags);
		err = gp2ap020a00f_alter_opmode(data,
						GP2AP020A00F_OPMODE_PS,
						GP2AP020A00F_SUBTRACT_MODE);
		break;
	case GP2AP020A00F_CMD_ALS_HIGH_EV_EN:
		if (test_bit(GP2AP020A00F_FLAG_ALS_RISING_EV, &data->flags))
			return 0;
		if (data->cur_opmode == GP2AP020A00F_OPMODE_PROX_DETECT)
			return -EBUSY;
		if (!gp2ap020a00f_als_enabled(data)) {
			err = gp2ap020a00f_alter_opmode(data,
						GP2AP020A00F_OPMODE_ALS,
						GP2AP020A00F_ADD_MODE);
			if (err < 0)
				return err;
		}
		set_bit(GP2AP020A00F_FLAG_ALS_RISING_EV, &data->flags);
		err =  gp2ap020a00f_write_event_threshold(data,
					GP2AP020A00F_THRESH_TH, true);
		break;
	case GP2AP020A00F_CMD_ALS_HIGH_EV_DIS:
		if (!test_bit(GP2AP020A00F_FLAG_ALS_RISING_EV, &data->flags))
			return 0;
		clear_bit(GP2AP020A00F_FLAG_ALS_RISING_EV, &data->flags);
		if (!gp2ap020a00f_als_enabled(data)) {
			err = gp2ap020a00f_alter_opmode(data,
						GP2AP020A00F_OPMODE_ALS,
						GP2AP020A00F_SUBTRACT_MODE);
			if (err < 0)
				return err;
		}
		err =  gp2ap020a00f_write_event_threshold(data,
					GP2AP020A00F_THRESH_TH, false);
		break;
	case GP2AP020A00F_CMD_ALS_LOW_EV_EN:
		if (test_bit(GP2AP020A00F_FLAG_ALS_FALLING_EV, &data->flags))
			return 0;
		if (data->cur_opmode == GP2AP020A00F_OPMODE_PROX_DETECT)
			return -EBUSY;
		if (!gp2ap020a00f_als_enabled(data)) {
			err = gp2ap020a00f_alter_opmode(data,
						GP2AP020A00F_OPMODE_ALS,
						GP2AP020A00F_ADD_MODE);
			if (err < 0)
				return err;
		}
		set_bit(GP2AP020A00F_FLAG_ALS_FALLING_EV, &data->flags);
		err =  gp2ap020a00f_write_event_threshold(data,
					GP2AP020A00F_THRESH_TL, true);
		break;
	case GP2AP020A00F_CMD_ALS_LOW_EV_DIS:
		if (!test_bit(GP2AP020A00F_FLAG_ALS_FALLING_EV, &data->flags))
			return 0;
		clear_bit(GP2AP020A00F_FLAG_ALS_FALLING_EV, &data->flags);
		if (!gp2ap020a00f_als_enabled(data)) {
			err = gp2ap020a00f_alter_opmode(data,
						GP2AP020A00F_OPMODE_ALS,
						GP2AP020A00F_SUBTRACT_MODE);
			if (err < 0)
				return err;
		}
		err =  gp2ap020a00f_write_event_threshold(data,
					GP2AP020A00F_THRESH_TL, false);
		break;
	case GP2AP020A00F_CMD_PROX_HIGH_EV_EN:
		if (test_bit(GP2AP020A00F_FLAG_PROX_RISING_EV, &data->flags))
			return 0;
		if (gp2ap020a00f_als_enabled(data) ||
		    data->cur_opmode == GP2AP020A00F_OPMODE_PS)
			return -EBUSY;
		if (!gp2ap020a00f_prox_detect_enabled(data)) {
			err = gp2ap020a00f_set_operation_mode(data,
					GP2AP020A00F_OPMODE_PROX_DETECT);
			if (err < 0)
				return err;
		}
		set_bit(GP2AP020A00F_FLAG_PROX_RISING_EV, &data->flags);
		err =  gp2ap020a00f_write_event_threshold(data,
					GP2AP020A00F_THRESH_PH, true);
		break;
	case GP2AP020A00F_CMD_PROX_HIGH_EV_DIS:
		if (!test_bit(GP2AP020A00F_FLAG_PROX_RISING_EV, &data->flags))
			return 0;
		clear_bit(GP2AP020A00F_FLAG_PROX_RISING_EV, &data->flags);
		err = gp2ap020a00f_set_operation_mode(data,
					GP2AP020A00F_OPMODE_SHUTDOWN);
		if (err < 0)
			return err;
		err =  gp2ap020a00f_write_event_threshold(data,
					GP2AP020A00F_THRESH_PH, false);
		break;
	case GP2AP020A00F_CMD_PROX_LOW_EV_EN:
		if (test_bit(GP2AP020A00F_FLAG_PROX_FALLING_EV, &data->flags))
			return 0;
		if (gp2ap020a00f_als_enabled(data) ||
		    data->cur_opmode == GP2AP020A00F_OPMODE_PS)
			return -EBUSY;
		if (!gp2ap020a00f_prox_detect_enabled(data)) {
			err = gp2ap020a00f_set_operation_mode(data,
					GP2AP020A00F_OPMODE_PROX_DETECT);
			if (err < 0)
				return err;
		}
		set_bit(GP2AP020A00F_FLAG_PROX_FALLING_EV, &data->flags);
		err =  gp2ap020a00f_write_event_threshold(data,
					GP2AP020A00F_THRESH_PL, true);
		break;
	case GP2AP020A00F_CMD_PROX_LOW_EV_DIS:
		if (!test_bit(GP2AP020A00F_FLAG_PROX_FALLING_EV, &data->flags))
			return 0;
		clear_bit(GP2AP020A00F_FLAG_PROX_FALLING_EV, &data->flags);
		err = gp2ap020a00f_set_operation_mode(data,
					GP2AP020A00F_OPMODE_SHUTDOWN);
		if (err < 0)
			return err;
		err =  gp2ap020a00f_write_event_threshold(data,
					GP2AP020A00F_THRESH_PL, false);
		break;
	}

	return err;
}

static int wait_conversion_complete_irq(struct gp2ap020a00f_data *data)
{
	int ret;

	ret = wait_event_timeout(data->data_ready_queue,
				 test_bit(GP2AP020A00F_FLAG_DATA_READY,
					  &data->flags),
				 GP2AP020A00F_DATA_READY_TIMEOUT);
	clear_bit(GP2AP020A00F_FLAG_DATA_READY, &data->flags);

	return ret > 0 ? 0 : -ETIME;
}

static int gp2ap020a00f_read_output(struct gp2ap020a00f_data *data,
					unsigned int output_reg, int *val)
{
	u8 reg_buf[2];
	int err;

	err = wait_conversion_complete_irq(data);
	if (err < 0)
		dev_dbg(&data->client->dev, "data ready timeout\n");

	err = regmap_bulk_read(data->regmap, output_reg, reg_buf, 2);
	if (err < 0)
		return err;

	*val = le16_to_cpup((__le16 *)reg_buf);

	return err;
}

static bool gp2ap020a00f_adjust_lux_mode(struct gp2ap020a00f_data *data,
				 int output_val)
{
	u8 new_range = 0xff;
	int err;

	if (!test_bit(GP2AP020A00F_FLAG_LUX_MODE_HI, &data->flags)) {
		if (output_val > 16000) {
			set_bit(GP2AP020A00F_FLAG_LUX_MODE_HI, &data->flags);
			new_range = GP2AP020A00F_RANGE_A_x128;
		}
	} else {
		if (output_val < 1000) {
			clear_bit(GP2AP020A00F_FLAG_LUX_MODE_HI, &data->flags);
			new_range = GP2AP020A00F_RANGE_A_x8;
		}
	}

	if (new_range != 0xff) {
		/* Clear als threshold registers to avoid spurious
		 * events caused by lux mode transition.
		 */
		err =  gp2ap020a00f_write_event_threshold(data,
					GP2AP020A00F_THRESH_TH, false);
		if (err < 0) {
			dev_err(&data->client->dev,
				"Clearing als threshold register failed.\n");
			return false;
		}

		err =  gp2ap020a00f_write_event_threshold(data,
					GP2AP020A00F_THRESH_TL, false);
		if (err < 0) {
			dev_err(&data->client->dev,
				"Clearing als threshold register failed.\n");
			return false;
		}

		/* Change lux mode */
		err = regmap_update_bits(data->regmap,
			GP2AP020A00F_OP_REG,
			GP2AP020A00F_OP3_MASK,
			GP2AP020A00F_OP3_SHUTDOWN);

		if (err < 0) {
			dev_err(&data->client->dev,
				"Shutting down the device failed.\n");
			return false;
		}

		err = regmap_update_bits(data->regmap,
			GP2AP020A00F_ALS_REG,
			GP2AP020A00F_RANGE_A_MASK,
			new_range);

		if (err < 0) {
			dev_err(&data->client->dev,
				"Adjusting device lux mode failed.\n");
			return false;
		}

		err = regmap_update_bits(data->regmap,
			GP2AP020A00F_OP_REG,
			GP2AP020A00F_OP3_MASK,
			GP2AP020A00F_OP3_OPERATION);

		if (err < 0) {
			dev_err(&data->client->dev,
				"Powering up the device failed.\n");
			return false;
		}

		/* Adjust als threshold register values to the new lux mode */
		if (test_bit(GP2AP020A00F_FLAG_ALS_RISING_EV, &data->flags)) {
			err =  gp2ap020a00f_write_event_threshold(data,
					GP2AP020A00F_THRESH_TH, true);
			if (err < 0) {
				dev_err(&data->client->dev,
				"Adjusting als threshold value failed.\n");
				return false;
			}
		}

		if (test_bit(GP2AP020A00F_FLAG_ALS_FALLING_EV, &data->flags)) {
			err =  gp2ap020a00f_write_event_threshold(data,
					GP2AP020A00F_THRESH_TL, true);
			if (err < 0) {
				dev_err(&data->client->dev,
				"Adjusting als threshold value failed.\n");
				return false;
			}
		}

		return true;
	}

	return false;
}

static void gp2ap020a00f_output_to_lux(struct gp2ap020a00f_data *data,
						int *output_val)
{
	if (test_bit(GP2AP020A00F_FLAG_LUX_MODE_HI, &data->flags))
		*output_val *= 16;
}

static void gp2ap020a00f_iio_trigger_work(struct irq_work *work)
{
	struct gp2ap020a00f_data *data =
		container_of(work, struct gp2ap020a00f_data, work);

	iio_trigger_poll(data->trig);
}

static irqreturn_t gp2ap020a00f_prox_sensing_handler(int irq, void *data)
{
	struct iio_dev *indio_dev = data;
	struct gp2ap020a00f_data *priv = iio_priv(indio_dev);
	unsigned int op_reg_val;
	int ret;

	/* Read interrupt flags */
	ret = regmap_read(priv->regmap, GP2AP020A00F_OP_REG, &op_reg_val);
	if (ret < 0)
		return IRQ_HANDLED;

	if (gp2ap020a00f_prox_detect_enabled(priv)) {
		if (op_reg_val & GP2AP020A00F_PROX_DETECT) {
			iio_push_event(indio_dev,
			       IIO_UNMOD_EVENT_CODE(
				    IIO_PROXIMITY,
				    GP2AP020A00F_SCAN_MODE_PROXIMITY,
				    IIO_EV_TYPE_ROC,
				    IIO_EV_DIR_RISING),
			       iio_get_time_ns(indio_dev));
		} else {
			iio_push_event(indio_dev,
			       IIO_UNMOD_EVENT_CODE(
				    IIO_PROXIMITY,
				    GP2AP020A00F_SCAN_MODE_PROXIMITY,
				    IIO_EV_TYPE_ROC,
				    IIO_EV_DIR_FALLING),
			       iio_get_time_ns(indio_dev));
		}
	}

	return IRQ_HANDLED;
}

static irqreturn_t gp2ap020a00f_thresh_event_handler(int irq, void *data)
{
	struct iio_dev *indio_dev = data;
	struct gp2ap020a00f_data *priv = iio_priv(indio_dev);
	u8 op_reg_flags, d0_reg_buf[2];
	unsigned int output_val, op_reg_val;
	int thresh_val_id, ret;

	/* Read interrupt flags */
	ret = regmap_read(priv->regmap, GP2AP020A00F_OP_REG,
							&op_reg_val);
	if (ret < 0)
		goto done;

	op_reg_flags = op_reg_val & (GP2AP020A00F_FLAG_A | GP2AP020A00F_FLAG_P
					| GP2AP020A00F_PROX_DETECT);

	op_reg_val &= (~GP2AP020A00F_FLAG_A & ~GP2AP020A00F_FLAG_P
					& ~GP2AP020A00F_PROX_DETECT);

	/* Clear interrupt flags (if not in INTTYPE_PULSE mode) */
	if (priv->cur_opmode != GP2AP020A00F_OPMODE_PROX_DETECT) {
		ret = regmap_write(priv->regmap, GP2AP020A00F_OP_REG,
								op_reg_val);
		if (ret < 0)
			goto done;
	}

	if (op_reg_flags & GP2AP020A00F_FLAG_A) {
		/* Check D0 register to assess if the lux mode
		 * transition is required.
		 */
		ret = regmap_bulk_read(priv->regmap, GP2AP020A00F_D0_L_REG,
							d0_reg_buf, 2);
		if (ret < 0)
			goto done;

		output_val = le16_to_cpup((__le16 *)d0_reg_buf);

		if (gp2ap020a00f_adjust_lux_mode(priv, output_val))
			goto done;

		gp2ap020a00f_output_to_lux(priv, &output_val);

		/*
		 * We need to check output value to distinguish
		 * between high and low ambient light threshold event.
		 */
		if (test_bit(GP2AP020A00F_FLAG_ALS_RISING_EV, &priv->flags)) {
			thresh_val_id =
			    GP2AP020A00F_THRESH_VAL_ID(GP2AP020A00F_TH_L_REG);
			if (output_val > priv->thresh_val[thresh_val_id])
				iio_push_event(indio_dev,
				       IIO_MOD_EVENT_CODE(
					    IIO_LIGHT,
					    GP2AP020A00F_SCAN_MODE_LIGHT_CLEAR,
					    IIO_MOD_LIGHT_CLEAR,
					    IIO_EV_TYPE_THRESH,
					    IIO_EV_DIR_RISING),
				       iio_get_time_ns(indio_dev));
		}

		if (test_bit(GP2AP020A00F_FLAG_ALS_FALLING_EV, &priv->flags)) {
			thresh_val_id =
			    GP2AP020A00F_THRESH_VAL_ID(GP2AP020A00F_TL_L_REG);
			if (output_val < priv->thresh_val[thresh_val_id])
				iio_push_event(indio_dev,
				       IIO_MOD_EVENT_CODE(
					    IIO_LIGHT,
					    GP2AP020A00F_SCAN_MODE_LIGHT_CLEAR,
					    IIO_MOD_LIGHT_CLEAR,
					    IIO_EV_TYPE_THRESH,
					    IIO_EV_DIR_FALLING),
				       iio_get_time_ns(indio_dev));
		}
	}

	if (priv->cur_opmode == GP2AP020A00F_OPMODE_READ_RAW_CLEAR ||
	    priv->cur_opmode == GP2AP020A00F_OPMODE_READ_RAW_IR ||
	    priv->cur_opmode == GP2AP020A00F_OPMODE_READ_RAW_PROXIMITY) {
		set_bit(GP2AP020A00F_FLAG_DATA_READY, &priv->flags);
		wake_up(&priv->data_ready_queue);
		goto done;
	}

	if (test_bit(GP2AP020A00F_FLAG_ALS_CLEAR_TRIGGER, &priv->flags) ||
	    test_bit(GP2AP020A00F_FLAG_ALS_IR_TRIGGER, &priv->flags) ||
	    test_bit(GP2AP020A00F_FLAG_PROX_TRIGGER, &priv->flags))
		/* This fires off the trigger. */
		irq_work_queue(&priv->work);

done:
	return IRQ_HANDLED;
}

static irqreturn_t gp2ap020a00f_trigger_handler(int irq, void *data)
{
	struct iio_poll_func *pf = data;
	struct iio_dev *indio_dev = pf->indio_dev;
	struct gp2ap020a00f_data *priv = iio_priv(indio_dev);
	size_t d_size = 0;
	int i, out_val, ret;

	for_each_set_bit(i, indio_dev->active_scan_mask,
		indio_dev->masklength) {
		ret = regmap_bulk_read(priv->regmap,
				GP2AP020A00F_DATA_REG(i),
				&priv->buffer[d_size], 2);
		if (ret < 0)
			goto done;

		if (i == GP2AP020A00F_SCAN_MODE_LIGHT_CLEAR ||
		    i == GP2AP020A00F_SCAN_MODE_LIGHT_IR) {
			out_val = le16_to_cpup((__le16 *)&priv->buffer[d_size]);
			gp2ap020a00f_output_to_lux(priv, &out_val);

			put_unaligned_le32(out_val, &priv->buffer[d_size]);
			d_size += 4;
		} else {
			d_size += 2;
		}
	}

	iio_push_to_buffers_with_timestamp(indio_dev, priv->buffer,
		pf->timestamp);
done:
	iio_trigger_notify_done(indio_dev->trig);

	return IRQ_HANDLED;
}

static u8 gp2ap020a00f_get_thresh_reg(const struct iio_chan_spec *chan,
					     enum iio_event_direction event_dir)
{
	switch (chan->type) {
	case IIO_PROXIMITY:
		if (event_dir == IIO_EV_DIR_RISING)
			return GP2AP020A00F_PH_L_REG;
		else
			return GP2AP020A00F_PL_L_REG;
	case IIO_LIGHT:
		if (event_dir == IIO_EV_DIR_RISING)
			return GP2AP020A00F_TH_L_REG;
		else
			return GP2AP020A00F_TL_L_REG;
	default:
		break;
	}

	return -EINVAL;
}

static int gp2ap020a00f_write_event_val(struct iio_dev *indio_dev,
					const struct iio_chan_spec *chan,
					enum iio_event_type type,
					enum iio_event_direction dir,
					enum iio_event_info info,
					int val, int val2)
{
	struct gp2ap020a00f_data *data = iio_priv(indio_dev);
	bool event_en = false;
	u8 thresh_val_id;
	u8 thresh_reg_l;
	int err = 0;

	mutex_lock(&data->lock);

	thresh_reg_l = gp2ap020a00f_get_thresh_reg(chan, dir);
	thresh_val_id = GP2AP020A00F_THRESH_VAL_ID(thresh_reg_l);

	if (thresh_val_id > GP2AP020A00F_THRESH_PH) {
		err = -EINVAL;
		goto error_unlock;
	}

	switch (thresh_reg_l) {
	case GP2AP020A00F_TH_L_REG:
		event_en = test_bit(GP2AP020A00F_FLAG_ALS_RISING_EV,
							&data->flags);
		break;
	case GP2AP020A00F_TL_L_REG:
		event_en = test_bit(GP2AP020A00F_FLAG_ALS_FALLING_EV,
							&data->flags);
		break;
	case GP2AP020A00F_PH_L_REG:
		if (val == 0) {
			err = -EINVAL;
			goto error_unlock;
		}
		event_en = test_bit(GP2AP020A00F_FLAG_PROX_RISING_EV,
							&data->flags);
		break;
	case GP2AP020A00F_PL_L_REG:
		if (val == 0) {
			err = -EINVAL;
			goto error_unlock;
		}
		event_en = test_bit(GP2AP020A00F_FLAG_PROX_FALLING_EV,
							&data->flags);
		break;
	}

	data->thresh_val[thresh_val_id] = val;
	err =  gp2ap020a00f_write_event_threshold(data, thresh_val_id,
							event_en);
error_unlock:
	mutex_unlock(&data->lock);

	return err;
}

static int gp2ap020a00f_read_event_val(struct iio_dev *indio_dev,
				       const struct iio_chan_spec *chan,
				       enum iio_event_type type,
				       enum iio_event_direction dir,
				       enum iio_event_info info,
				       int *val, int *val2)
{
	struct gp2ap020a00f_data *data = iio_priv(indio_dev);
	u8 thresh_reg_l;
	int err = IIO_VAL_INT;

	mutex_lock(&data->lock);

	thresh_reg_l = gp2ap020a00f_get_thresh_reg(chan, dir);

	if (thresh_reg_l > GP2AP020A00F_PH_L_REG) {
		err = -EINVAL;
		goto error_unlock;
	}

	*val = data->thresh_val[GP2AP020A00F_THRESH_VAL_ID(thresh_reg_l)];

error_unlock:
	mutex_unlock(&data->lock);

	return err;
}

static int gp2ap020a00f_write_prox_event_config(struct iio_dev *indio_dev,
						int state)
{
	struct gp2ap020a00f_data *data = iio_priv(indio_dev);
	enum gp2ap020a00f_cmd cmd_high_ev, cmd_low_ev;
	int err;

	cmd_high_ev = state ? GP2AP020A00F_CMD_PROX_HIGH_EV_EN :
			      GP2AP020A00F_CMD_PROX_HIGH_EV_DIS;
	cmd_low_ev = state ? GP2AP020A00F_CMD_PROX_LOW_EV_EN :
			     GP2AP020A00F_CMD_PROX_LOW_EV_DIS;

	/*
	 * In order to enable proximity detection feature in the device
	 * both high and low threshold registers have to be written
	 * with different values, greater than zero.
	 */
	if (state) {
		if (data->thresh_val[GP2AP020A00F_THRESH_PL] == 0)
			return -EINVAL;

		if (data->thresh_val[GP2AP020A00F_THRESH_PH] == 0)
			return -EINVAL;
	}

	err = gp2ap020a00f_exec_cmd(data, cmd_high_ev);
	if (err < 0)
		return err;

	err = gp2ap020a00f_exec_cmd(data, cmd_low_ev);
	if (err < 0)
		return err;

	free_irq(data->client->irq, indio_dev);

	if (state)
		err = request_threaded_irq(data->client->irq, NULL,
					   &gp2ap020a00f_prox_sensing_handler,
					   IRQF_TRIGGER_RISING |
					   IRQF_TRIGGER_FALLING |
					   IRQF_ONESHOT,
					   "gp2ap020a00f_prox_sensing",
					   indio_dev);
	else {
		err = request_threaded_irq(data->client->irq, NULL,
					   &gp2ap020a00f_thresh_event_handler,
					   IRQF_TRIGGER_FALLING |
					   IRQF_ONESHOT,
					   "gp2ap020a00f_thresh_event",
					   indio_dev);
	}

	return err;
}

static int gp2ap020a00f_write_event_config(struct iio_dev *indio_dev,
					   const struct iio_chan_spec *chan,
					   enum iio_event_type type,
					   enum iio_event_direction dir,
					   int state)
{
	struct gp2ap020a00f_data *data = iio_priv(indio_dev);
	enum gp2ap020a00f_cmd cmd;
	int err;

	mutex_lock(&data->lock);

	switch (chan->type) {
	case IIO_PROXIMITY:
		err = gp2ap020a00f_write_prox_event_config(indio_dev, state);
		break;
	case IIO_LIGHT:
		if (dir == IIO_EV_DIR_RISING) {
			cmd = state ? GP2AP020A00F_CMD_ALS_HIGH_EV_EN :
				      GP2AP020A00F_CMD_ALS_HIGH_EV_DIS;
			err = gp2ap020a00f_exec_cmd(data, cmd);
		} else {
			cmd = state ? GP2AP020A00F_CMD_ALS_LOW_EV_EN :
				      GP2AP020A00F_CMD_ALS_LOW_EV_DIS;
			err = gp2ap020a00f_exec_cmd(data, cmd);
		}
		break;
	default:
		err = -EINVAL;
	}

	mutex_unlock(&data->lock);

	return err;
}

static int gp2ap020a00f_read_event_config(struct iio_dev *indio_dev,
					   const struct iio_chan_spec *chan,
					   enum iio_event_type type,
					   enum iio_event_direction dir)
{
	struct gp2ap020a00f_data *data = iio_priv(indio_dev);
	int event_en = 0;

	mutex_lock(&data->lock);

	switch (chan->type) {
	case IIO_PROXIMITY:
		if (dir == IIO_EV_DIR_RISING)
			event_en = test_bit(GP2AP020A00F_FLAG_PROX_RISING_EV,
								&data->flags);
		else
			event_en = test_bit(GP2AP020A00F_FLAG_PROX_FALLING_EV,
								&data->flags);
		break;
	case IIO_LIGHT:
		if (dir == IIO_EV_DIR_RISING)
			event_en = test_bit(GP2AP020A00F_FLAG_ALS_RISING_EV,
								&data->flags);
		else
			event_en = test_bit(GP2AP020A00F_FLAG_ALS_FALLING_EV,
								&data->flags);
		break;
	default:
		event_en = -EINVAL;
		break;
	}

	mutex_unlock(&data->lock);

	return event_en;
}

static int gp2ap020a00f_read_channel(struct gp2ap020a00f_data *data,
				struct iio_chan_spec const *chan, int *val)
{
	enum gp2ap020a00f_cmd cmd;
	int err;

	switch (chan->scan_index) {
	case GP2AP020A00F_SCAN_MODE_LIGHT_CLEAR:
		cmd = GP2AP020A00F_CMD_READ_RAW_CLEAR;
		break;
	case GP2AP020A00F_SCAN_MODE_LIGHT_IR:
		cmd = GP2AP020A00F_CMD_READ_RAW_IR;
		break;
	case GP2AP020A00F_SCAN_MODE_PROXIMITY:
		cmd = GP2AP020A00F_CMD_READ_RAW_PROXIMITY;
		break;
	default:
		return -EINVAL;
	}

	err = gp2ap020a00f_exec_cmd(data, cmd);
	if (err < 0) {
		dev_err(&data->client->dev,
			"gp2ap020a00f_exec_cmd failed\n");
		goto error_ret;
	}

	err = gp2ap020a00f_read_output(data, chan->address, val);
	if (err < 0)
		dev_err(&data->client->dev,
			"gp2ap020a00f_read_output failed\n");

	err = gp2ap020a00f_set_operation_mode(data,
					GP2AP020A00F_OPMODE_SHUTDOWN);
	if (err < 0)
		dev_err(&data->client->dev,
			"Failed to shut down the device.\n");

	if (cmd == GP2AP020A00F_CMD_READ_RAW_CLEAR ||
	    cmd == GP2AP020A00F_CMD_READ_RAW_IR)
		gp2ap020a00f_output_to_lux(data, val);

error_ret:
	return err;
}

static int gp2ap020a00f_read_raw(struct iio_dev *indio_dev,
			   struct iio_chan_spec const *chan,
			   int *val, int *val2,
			   long mask)
{
	struct gp2ap020a00f_data *data = iio_priv(indio_dev);
	int err = -EINVAL;

	if (mask == IIO_CHAN_INFO_RAW) {
		err = iio_device_claim_direct_mode(indio_dev);
		if (err)
			return err;

		err = gp2ap020a00f_read_channel(data, chan, val);
		iio_device_release_direct_mode(indio_dev);
	}
	return err < 0 ? err : IIO_VAL_INT;
}

static const struct iio_event_spec gp2ap020a00f_event_spec_light[] = {
	{
		.type = IIO_EV_TYPE_THRESH,
		.dir = IIO_EV_DIR_RISING,
		.mask_separate = BIT(IIO_EV_INFO_VALUE) |
			BIT(IIO_EV_INFO_ENABLE),
	}, {
		.type = IIO_EV_TYPE_THRESH,
		.dir = IIO_EV_DIR_FALLING,
		.mask_separate = BIT(IIO_EV_INFO_VALUE) |
			BIT(IIO_EV_INFO_ENABLE),
	},
};

static const struct iio_event_spec gp2ap020a00f_event_spec_prox[] = {
	{
		.type = IIO_EV_TYPE_ROC,
		.dir = IIO_EV_DIR_RISING,
		.mask_separate = BIT(IIO_EV_INFO_VALUE) |
			BIT(IIO_EV_INFO_ENABLE),
	}, {
		.type = IIO_EV_TYPE_ROC,
		.dir = IIO_EV_DIR_FALLING,
		.mask_separate = BIT(IIO_EV_INFO_VALUE) |
			BIT(IIO_EV_INFO_ENABLE),
	},
};

static const struct iio_chan_spec gp2ap020a00f_channels[] = {
	{
		.type = IIO_LIGHT,
		.channel2 = IIO_MOD_LIGHT_CLEAR,
		.modified = 1,
		.info_mask_separate = BIT(IIO_CHAN_INFO_RAW),
		.scan_type = {
			.sign = 'u',
			.realbits = 24,
			.shift = 0,
			.storagebits = 32,
			.endianness = IIO_LE,
		},
		.scan_index = GP2AP020A00F_SCAN_MODE_LIGHT_CLEAR,
		.address = GP2AP020A00F_D0_L_REG,
		.event_spec = gp2ap020a00f_event_spec_light,
		.num_event_specs = ARRAY_SIZE(gp2ap020a00f_event_spec_light),
	},
	{
		.type = IIO_LIGHT,
		.channel2 = IIO_MOD_LIGHT_IR,
		.modified = 1,
		.info_mask_separate = BIT(IIO_CHAN_INFO_RAW),
		.scan_type = {
			.sign = 'u',
			.realbits = 24,
			.shift = 0,
			.storagebits = 32,
			.endianness = IIO_LE,
		},
		.scan_index = GP2AP020A00F_SCAN_MODE_LIGHT_IR,
		.address = GP2AP020A00F_D1_L_REG,
	},
	{
		.type = IIO_PROXIMITY,
		.modified = 0,
		.info_mask_separate = BIT(IIO_CHAN_INFO_RAW),
		.scan_type = {
			.sign = 'u',
			.realbits = 16,
			.shift = 0,
			.storagebits = 16,
			.endianness = IIO_LE,
		},
		.scan_index = GP2AP020A00F_SCAN_MODE_PROXIMITY,
		.address = GP2AP020A00F_D2_L_REG,
		.event_spec = gp2ap020a00f_event_spec_prox,
		.num_event_specs = ARRAY_SIZE(gp2ap020a00f_event_spec_prox),
	},
	IIO_CHAN_SOFT_TIMESTAMP(GP2AP020A00F_CHAN_TIMESTAMP),
};

static const struct iio_info gp2ap020a00f_info = {
	.read_raw = &gp2ap020a00f_read_raw,
	.read_event_value = &gp2ap020a00f_read_event_val,
	.read_event_config = &gp2ap020a00f_read_event_config,
	.write_event_value = &gp2ap020a00f_write_event_val,
	.write_event_config = &gp2ap020a00f_write_event_config,
};

static int gp2ap020a00f_buffer_postenable(struct iio_dev *indio_dev)
{
	struct gp2ap020a00f_data *data = iio_priv(indio_dev);
	int i, err = 0;

	mutex_lock(&data->lock);

	err = iio_triggered_buffer_postenable(indio_dev);
	if (err < 0) {
		mutex_unlock(&data->lock);
		return err;
	}

	/*
	 * Enable triggers according to the scan_mask. Enabling either
	 * LIGHT_CLEAR or LIGHT_IR scan mode results in enabling ALS
	 * module in the device, which generates samples in both D0 (clear)
	 * and D1 (ir) registers. As the two registers are bound to the
	 * two separate IIO channels they are treated in the driver logic
	 * as if they were controlled independently.
	 */
	for_each_set_bit(i, indio_dev->active_scan_mask,
		indio_dev->masklength) {
		switch (i) {
		case GP2AP020A00F_SCAN_MODE_LIGHT_CLEAR:
			err = gp2ap020a00f_exec_cmd(data,
					GP2AP020A00F_CMD_TRIGGER_CLEAR_EN);
			break;
		case GP2AP020A00F_SCAN_MODE_LIGHT_IR:
			err = gp2ap020a00f_exec_cmd(data,
					GP2AP020A00F_CMD_TRIGGER_IR_EN);
			break;
		case GP2AP020A00F_SCAN_MODE_PROXIMITY:
			err = gp2ap020a00f_exec_cmd(data,
					GP2AP020A00F_CMD_TRIGGER_PROX_EN);
			break;
		}
	}

	if (err < 0)
		goto error_unlock;

	data->buffer = kmalloc(indio_dev->scan_bytes, GFP_KERNEL);
	if (!data->buffer)
		err = -ENOMEM;

error_unlock:
	if (err < 0)
		iio_triggered_buffer_predisable(indio_dev);
	mutex_unlock(&data->lock);

	return err;
}

static int gp2ap020a00f_buffer_predisable(struct iio_dev *indio_dev)
{
	struct gp2ap020a00f_data *data = iio_priv(indio_dev);
	int i, err = 0;

	mutex_lock(&data->lock);

	for_each_set_bit(i, indio_dev->active_scan_mask,
		indio_dev->masklength) {
		switch (i) {
		case GP2AP020A00F_SCAN_MODE_LIGHT_CLEAR:
			err = gp2ap020a00f_exec_cmd(data,
					GP2AP020A00F_CMD_TRIGGER_CLEAR_DIS);
			break;
		case GP2AP020A00F_SCAN_MODE_LIGHT_IR:
			err = gp2ap020a00f_exec_cmd(data,
					GP2AP020A00F_CMD_TRIGGER_IR_DIS);
			break;
		case GP2AP020A00F_SCAN_MODE_PROXIMITY:
			err = gp2ap020a00f_exec_cmd(data,
					GP2AP020A00F_CMD_TRIGGER_PROX_DIS);
			break;
		}
	}

	if (err == 0)
		kfree(data->buffer);

	iio_triggered_buffer_predisable(indio_dev);

	mutex_unlock(&data->lock);

	return err;
}

static const struct iio_buffer_setup_ops gp2ap020a00f_buffer_setup_ops = {
	.postenable = &gp2ap020a00f_buffer_postenable,
	.predisable = &gp2ap020a00f_buffer_predisable,
};

static const struct iio_trigger_ops gp2ap020a00f_trigger_ops = {
};

static int gp2ap020a00f_probe(struct i2c_client *client,
				const struct i2c_device_id *id)
{
	struct gp2ap020a00f_data *data;
	struct iio_dev *indio_dev;
	struct regmap *regmap;
	int err;

	indio_dev = devm_iio_device_alloc(&client->dev, sizeof(*data));
	if (!indio_dev)
		return -ENOMEM;

	data = iio_priv(indio_dev);

	data->vled_reg = devm_regulator_get(&client->dev, "vled");
	if (IS_ERR(data->vled_reg))
		return PTR_ERR(data->vled_reg);

	err = regulator_enable(data->vled_reg);
	if (err)
		return err;

	regmap = devm_regmap_init_i2c(client, &gp2ap020a00f_regmap_config);
	if (IS_ERR(regmap)) {
		dev_err(&client->dev, "Regmap initialization failed.\n");
		err = PTR_ERR(regmap);
		goto error_regulator_disable;
	}

	/* Initialize device registers */
	err = regmap_bulk_write(regmap, GP2AP020A00F_OP_REG,
			gp2ap020a00f_reg_init_tab,
			ARRAY_SIZE(gp2ap020a00f_reg_init_tab));

	if (err < 0) {
		dev_err(&client->dev, "Device initialization failed.\n");
		goto error_regulator_disable;
	}

	i2c_set_clientdata(client, indio_dev);

	data->client = client;
	data->cur_opmode = GP2AP020A00F_OPMODE_SHUTDOWN;
	data->regmap = regmap;
	init_waitqueue_head(&data->data_ready_queue);

	mutex_init(&data->lock);
	indio_dev->dev.parent = &client->dev;
	indio_dev->channels = gp2ap020a00f_channels;
	indio_dev->num_channels = ARRAY_SIZE(gp2ap020a00f_channels);
	indio_dev->info = &gp2ap020a00f_info;
	indio_dev->name = id->name;
	indio_dev->modes = INDIO_DIRECT_MODE;

	/* Allocate buffer */
	err = iio_triggered_buffer_setup(indio_dev, &iio_pollfunc_store_time,
		&gp2ap020a00f_trigger_handler, &gp2ap020a00f_buffer_setup_ops);
	if (err < 0)
		goto error_regulator_disable;

	/* Allocate trigger */
	data->trig = devm_iio_trigger_alloc(&client->dev, "%s-trigger",
							indio_dev->name);
	if (data->trig == NULL) {
		err = -ENOMEM;
		dev_err(&indio_dev->dev, "Failed to allocate iio trigger.\n");
		goto error_uninit_buffer;
	}

	/* This needs to be requested here for read_raw calls to work. */
	err = request_threaded_irq(client->irq, NULL,
				   &gp2ap020a00f_thresh_event_handler,
				   IRQF_TRIGGER_FALLING |
				   IRQF_ONESHOT,
				   "gp2ap020a00f_als_event",
				   indio_dev);
	if (err < 0) {
		dev_err(&client->dev, "Irq request failed.\n");
		goto error_uninit_buffer;
	}

	data->trig->ops = &gp2ap020a00f_trigger_ops;
	data->trig->dev.parent = &data->client->dev;

	init_irq_work(&data->work, gp2ap020a00f_iio_trigger_work);

	err = iio_trigger_register(data->trig);
	if (err < 0) {
		dev_err(&client->dev, "Failed to register iio trigger.\n");
		goto error_free_irq;
	}

	err = iio_device_register(indio_dev);
	if (err < 0)
		goto error_trigger_unregister;

	return 0;

error_trigger_unregister:
	iio_trigger_unregister(data->trig);
error_free_irq:
	free_irq(client->irq, indio_dev);
error_uninit_buffer:
	iio_triggered_buffer_cleanup(indio_dev);
error_regulator_disable:
	regulator_disable(data->vled_reg);

	return err;
}

static int gp2ap020a00f_remove(struct i2c_client *client)
{
	struct iio_dev *indio_dev = i2c_get_clientdata(client);
	struct gp2ap020a00f_data *data = iio_priv(indio_dev);
	int err;

	err = gp2ap020a00f_set_operation_mode(data,
					GP2AP020A00F_OPMODE_SHUTDOWN);
	if (err < 0)
		dev_err(&indio_dev->dev, "Failed to power off the device.\n");

	iio_device_unregister(indio_dev);
	iio_trigger_unregister(data->trig);
	free_irq(client->irq, indio_dev);
	iio_triggered_buffer_cleanup(indio_dev);
	regulator_disable(data->vled_reg);

	return 0;
}

static const struct i2c_device_id gp2ap020a00f_id[] = {
	{ GP2A_I2C_NAME, 0 },
	{ }
};

MODULE_DEVICE_TABLE(i2c, gp2ap020a00f_id);

#ifdef CONFIG_OF
static const struct of_device_id gp2ap020a00f_of_match[] = {
	{ .compatible = "sharp,gp2ap020a00f" },
	{ }
};
MODULE_DEVICE_TABLE(of, gp2ap020a00f_of_match);
#endif

static struct i2c_driver gp2ap020a00f_driver = {
	.driver = {
		.name	= GP2A_I2C_NAME,
		.of_match_table = of_match_ptr(gp2ap020a00f_of_match),
	},
	.probe		= gp2ap020a00f_probe,
	.remove		= gp2ap020a00f_remove,
	.id_table	= gp2ap020a00f_id,
};

module_i2c_driver(gp2ap020a00f_driver);

MODULE_AUTHOR("Jacek Anaszewski <j.anaszewski@samsung.com>");
MODULE_DESCRIPTION("Sharp GP2AP020A00F Proximity/ALS sensor driver");
MODULE_LICENSE("GPL v2");
