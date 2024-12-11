// SPDX-License-Identifier: GPL-2.0
/*
 * Driver for Amlogic Meson IR remote receiver
 *
 * Copyright (C) 2014 Beniamino Galvani <b.galvani@gmail.com>
 */

#include <linux/device.h>
#include <linux/err.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/spinlock.h>
#include <linux/bitfield.h>
#include <linux/regmap.h>

#include <media/rc-core.h>

#define DRIVER_NAME		"meson-ir"

#define IR_DEC_LDR_ACTIVE			0x00
#define IR_DEC_LDR_ACTIVE_MAX			GENMASK(28, 16)
#define IR_DEC_LDR_ACTIVE_MIN			GENMASK(12, 0)
#define IR_DEC_LDR_IDLE				0x04
#define IR_DEC_LDR_IDLE_MAX			GENMASK(28, 16)
#define IR_DEC_LDR_IDLE_MIN			GENMASK(12, 0)
#define IR_DEC_LDR_REPEAT			0x08
#define IR_DEC_LDR_REPEAT_MAX			GENMASK(25, 16)
#define IR_DEC_LDR_REPEAT_MIN			GENMASK(9, 0)
#define IR_DEC_BIT_0				0x0c
#define IR_DEC_BIT_0_MAX			GENMASK(25, 16)
#define IR_DEC_BIT_0_MIN			GENMASK(9, 0)
#define IR_DEC_REG0				0x10
#define IR_DEC_REG0_FILTER			GENMASK(30, 28)
#define IR_DEC_REG0_FRAME_TIME_MAX		GENMASK(24, 12)
#define IR_DEC_REG0_BASE_TIME			GENMASK(11, 0)
#define IR_DEC_FRAME				0x14
#define IR_DEC_STATUS				0x18
#define IR_DEC_STATUS_BIT_1_ENABLE		BIT(30)
#define IR_DEC_STATUS_BIT_1_MAX			GENMASK(29, 20)
#define IR_DEC_STATUS_BIT_1_MIN			GENMASK(19, 10)
#define IR_DEC_STATUS_PULSE			BIT(8)
#define IR_DEC_STATUS_BUSY			BIT(7)
#define IR_DEC_STATUS_FRAME_STATUS		GENMASK(3, 0)
#define IR_DEC_REG1				0x1c
#define IR_DEC_REG1_TIME_IV			GENMASK(28, 16)
#define IR_DEC_REG1_FRAME_LEN			GENMASK(13, 8)
#define IR_DEC_REG1_ENABLE			BIT(15)
#define IR_DEC_REG1_HOLD_CODE			BIT(6)
#define IR_DEC_REG1_IRQSEL			GENMASK(3, 2)
#define IR_DEC_REG1_RESET			BIT(0)
/* Meson 6b uses REG1 to configure IR mode */
#define IR_DEC_REG1_MODE			GENMASK(8, 7)

/* The following registers are only available on Meson 8b and newer */
#define IR_DEC_REG2				0x20
#define IR_DEC_REG2_TICK_MODE			BIT(15)
#define IR_DEC_REG2_REPEAT_COUNTER		BIT(13)
#define IR_DEC_REG2_REPEAT_TIME			BIT(12)
#define IR_DEC_REG2_COMPARE_FRAME		BIT(11)
#define IR_DEC_REG2_BIT_ORDER			BIT(8)
/* Meson 8b / GXBB use REG2 to configure IR mode */
#define IR_DEC_REG2_MODE			GENMASK(3, 0)
#define IR_DEC_DURATN2				0x24
#define IR_DEC_DURATN2_MAX			GENMASK(25, 16)
#define IR_DEC_DURATN2_MIN			GENMASK(9, 0)
#define IR_DEC_DURATN3				0x28
#define IR_DEC_DURATN3_MAX			GENMASK(25, 16)
#define IR_DEC_DURATN3_MIN			GENMASK(9, 0)
#define IR_DEC_FRAME1				0x2c

#define FRAME_MSB_FIRST				true
#define FRAME_LSB_FIRST				false

#define DEC_MODE_NEC				0x0
#define DEC_MODE_RAW				0x2
#define DEC_MODE_RC6				0x9
#define DEC_MODE_XMP				0xE
#define DEC_MODE_UNKNOW				0xFF

#define DEC_STATUS_VALID			BIT(3)
#define DEC_STATUS_DATA_CODE_ERR		BIT(2)
#define DEC_STATUS_CUSTOM_CODE_ERR		BIT(1)
#define DEC_STATUS_REPEAT			BIT(0)

#define IRQSEL_DEC_MODE				0
#define IRQSEL_RISE_FALL			1
#define IRQSEL_FALL				2
#define IRQSEL_RISE				3

#define MESON_RAW_TRATE				10	/* us */
#define MESON_HW_TRATE				20	/* us */

/**
 * struct meson_ir_protocol - describe IR Protocol parameter
 *
 * @hw_protocol: select IR Protocol from IR Controller
 * @repeat_counter_enable: enable frame-to-frame time counter, it should work
 *                         with @repeat_compare_enable to detect the repeat frame
 * @repeat_check_enable: enable repeat time check for repeat detection
 * @repeat_compare_enable: enable to compare frame for repeat frame detection.
 *                         Some IR Protocol send the same data as repeat frame.
 *                         In this case, it should work with
 *                         @repeat_counter_enable to detect the repeat frame.
 * @bit_order: bit order, LSB or MSB
 * @bit1_match_enable: enable to check bit 1
 * @hold_code_enable: hold frame code in register IR_DEC_FRAME1, the new one
 *                    frame code will not be store in IR_DEC_FRAME1.
 *                    until IR_DEC_FRAME1 has been read
 * @count_tick_mode: increasing time unit of frame-to-frame time counter.
 *                   0 = 100us, 1 = 10us
 * @code_length: length (N-1) of data frame
 * @frame_time_max: max time for whole frame. Unit: MESON_HW_TRATE
 * @leader_active_max: max time for NEC/RC6 leader active part. Unit: MESON_HW_TRATE
 * @leader_active_min: min time for NEC/RC6 leader active part. Unit: MESON_HW_TRATE
 * @leader_idle_max: max time for NEC/RC6 leader idle part. Unit: MESON_HW_TRATE
 * @leader_idle_min: min time for NEC/RC6 leader idle part. Unit: MESON_HW_TRATE
 * @repeat_leader_max: max time for NEC repeat leader idle part. Unit: MESON_HW_TRATE
 * @repeat_leader_min: min time for NEC repeat leader idle part. Unit: MESON_HW_TRATE
 * @bit0_max: max time for NEC Logic '0', half of RC6 trailer bit, XMP Logic '00'
 * @bit0_min: min time for NEC Logic '0', half of RC6 trailer bit, XMP Logic '00'
 * @bit1_max: max time for NEC Logic '1', whole of RC6 trailer bit, XMP Logic '01'
 * @bit1_min: min time for NEC Logic '1', whole of RC6 trailer bit, XMP Logic '01'
 * @duration2_max: max time for half of RC6 normal bit, XMP Logic '10'
 * @duration2_min: min time for half of RC6 normal bit, XMP Logic '10'
 * @duration3_max: max time for whole of RC6 normal bit, XMP Logic '11'
 * @duration3_min: min time for whole of RC6 normal bit, XMP Logic '11'
 */

struct meson_ir_protocol {
	u8 hw_protocol;
	bool repeat_counter_enable;
	bool repeat_check_enable;
	bool repeat_compare_enable;
	bool bit_order;
	bool bit1_match_enable;
	bool hold_code_enable;
	bool count_tick_mode;
	u8 code_length;
	u16 frame_time_max;
	u16 leader_active_max;
	u16 leader_active_min;
	u16 leader_idle_max;
	u16 leader_idle_min;
	u16 repeat_leader_max;
	u16 repeat_leader_min;
	u16 bit0_max;
	u16 bit0_min;
	u16 bit1_max;
	u16 bit1_min;
	u16 duration2_max;
	u16 duration2_min;
	u16 duration3_max;
	u16 duration3_min;
};

struct meson_ir_param {
	bool support_hw_decoder;
	unsigned int max_register;
};

struct meson_ir {
	const struct meson_ir_param *param;
	struct regmap	*reg;
	struct rc_dev	*rc;
	spinlock_t	lock;
};

static struct regmap_config meson_ir_regmap_config = {
	.reg_bits = 32,
	.val_bits = 32,
	.reg_stride = 4,
};

static const struct meson_ir_protocol protocol_timings[] = {
	/* protocol, repeat counter, repeat check, repeat compare, order */
	{DEC_MODE_NEC, false, false, false, FRAME_LSB_FIRST,
	/* bit 1 match, hold code, count tick, len, frame time */
	true, false, false, 32, 4000,
	/* leader active max/min, leader idle max/min, repeat leader max/min */
	500, 400, 300, 200, 150, 80,
	/* bit0 max/min, bit1 max/min, duration2 max/min, duration3 max/min */
	72, 40, 134, 90, 0, 0, 0, 0}
};

static void meson_ir_nec_handler(struct meson_ir *ir)
{
	u32 code = 0;
	u32 status = 0;
	enum rc_proto proto;

	regmap_read(ir->reg, IR_DEC_STATUS, &status);

	if (status & DEC_STATUS_REPEAT) {
		rc_repeat(ir->rc);
	} else {
		regmap_read(ir->reg, IR_DEC_FRAME, &code);

		code = ir_nec_bytes_to_scancode(code, code >> 8,
						code >> 16, code >> 24, &proto);
		rc_keydown(ir->rc, proto, code, 0);
	}
}

static void meson_ir_hw_handler(struct meson_ir *ir)
{
	if (ir->rc->enabled_protocols & RC_PROTO_BIT_NEC)
		meson_ir_nec_handler(ir);
}

static irqreturn_t meson_ir_irq(int irqno, void *dev_id)
{
	struct meson_ir *ir = dev_id;
	u32 duration, status;
	struct ir_raw_event rawir = {};

	spin_lock(&ir->lock);

	regmap_read(ir->reg, IR_DEC_STATUS, &status);

	if (ir->rc->driver_type == RC_DRIVER_IR_RAW) {
		rawir.pulse = !!(status & IR_DEC_STATUS_PULSE);

		regmap_read(ir->reg, IR_DEC_REG1, &duration);
		duration = FIELD_GET(IR_DEC_REG1_TIME_IV, duration);
		rawir.duration = duration * MESON_RAW_TRATE;

		ir_raw_event_store_with_timeout(ir->rc, &rawir);
	} else if (ir->rc->driver_type == RC_DRIVER_SCANCODE) {
		if (status & DEC_STATUS_VALID)
			meson_ir_hw_handler(ir);
	}

	spin_unlock(&ir->lock);

	return IRQ_HANDLED;
}

static int meson_ir_hw_decoder_init(struct rc_dev *dev, u64 *rc_type)
{
	u8 protocol;
	u32 regval;
	int i;
	unsigned long flags;
	const struct meson_ir_protocol *timings;
	struct meson_ir *ir = dev->priv;

	if (*rc_type & RC_PROTO_BIT_NEC)
		protocol = DEC_MODE_NEC;
	else
		return 0;

	for (i = 0; i < ARRAY_SIZE(protocol_timings); i++)
		if (protocol_timings[i].hw_protocol == protocol)
			break;

	if (i == ARRAY_SIZE(protocol_timings)) {
		dev_err(&dev->dev, "hw protocol isn't supported: %d\n",
			protocol);
		return -EINVAL;
	}
	timings = &protocol_timings[i];

	spin_lock_irqsave(&ir->lock, flags);

	/* Clear controller status */
	regmap_read(ir->reg, IR_DEC_STATUS, &regval);
	regmap_read(ir->reg, IR_DEC_FRAME, &regval);

	/* Reset ir decoder and disable decoder */
	regmap_update_bits(ir->reg, IR_DEC_REG1, IR_DEC_REG1_ENABLE, 0);
	regmap_update_bits(ir->reg, IR_DEC_REG1, IR_DEC_REG1_RESET,
			   IR_DEC_REG1_RESET);

	/* Base time resolution, (19+1)*1us=20us */
	regval = FIELD_PREP(IR_DEC_REG0_BASE_TIME, MESON_HW_TRATE - 1);
	regmap_update_bits(ir->reg, IR_DEC_REG0, IR_DEC_REG0_BASE_TIME, regval);

	/* Monitor timing for input filter */
	regmap_update_bits(ir->reg, IR_DEC_REG0, IR_DEC_REG0_FILTER,
			   FIELD_PREP(IR_DEC_REG0_FILTER, 7));

	/* HW protocol */
	regval = FIELD_PREP(IR_DEC_REG2_MODE, timings->hw_protocol);
	regmap_update_bits(ir->reg, IR_DEC_REG2, IR_DEC_REG2_MODE, regval);

	/* Hold frame data until register was read */
	regmap_update_bits(ir->reg, IR_DEC_REG1, IR_DEC_REG1_HOLD_CODE,
			   timings->hold_code_enable ?
			   IR_DEC_REG1_HOLD_CODE : 0);

	/* Bit order */
	regmap_update_bits(ir->reg, IR_DEC_REG2, IR_DEC_REG2_BIT_ORDER,
			   timings->bit_order ? IR_DEC_REG2_BIT_ORDER : 0);

	/* Select tick mode */
	regmap_update_bits(ir->reg, IR_DEC_REG2, IR_DEC_REG2_TICK_MODE,
			   timings->count_tick_mode ?
			   IR_DEC_REG2_TICK_MODE : 0);

	/*
	 * Some protocols transmit the same data frame as repeat frame
	 * when the key is pressing. In this case, it could be detected as
	 * repeat frame if the repeat checker was enabled.
	 */
	regmap_update_bits(ir->reg, IR_DEC_REG2, IR_DEC_REG2_REPEAT_COUNTER,
			   timings->repeat_counter_enable ?
			   IR_DEC_REG2_REPEAT_COUNTER : 0);
	regmap_update_bits(ir->reg, IR_DEC_REG2, IR_DEC_REG2_REPEAT_TIME,
			   timings->repeat_check_enable ?
			   IR_DEC_REG2_REPEAT_TIME : 0);
	regmap_update_bits(ir->reg, IR_DEC_REG2, IR_DEC_REG2_COMPARE_FRAME,
			   timings->repeat_compare_enable ?
			   IR_DEC_REG2_COMPARE_FRAME : 0);

	/*
	 * FRAME_TIME_MAX should be larger than the time between
	 * data frame and repeat frame
	 */
	regval = FIELD_PREP(IR_DEC_REG0_FRAME_TIME_MAX,
			    timings->frame_time_max);
	regmap_update_bits(ir->reg, IR_DEC_REG0, IR_DEC_REG0_FRAME_TIME_MAX,
			   regval);

	/* Length(N-1) of data frame */
	regval = FIELD_PREP(IR_DEC_REG1_FRAME_LEN, timings->code_length - 1);
	regmap_update_bits(ir->reg, IR_DEC_REG1, IR_DEC_REG1_FRAME_LEN, regval);

	/* Time for leader active part */
	regval = FIELD_PREP(IR_DEC_LDR_ACTIVE_MAX,
			    timings->leader_active_max) |
		 FIELD_PREP(IR_DEC_LDR_ACTIVE_MIN,
			    timings->leader_active_min);
	regmap_update_bits(ir->reg, IR_DEC_LDR_ACTIVE, IR_DEC_LDR_ACTIVE_MAX |
			   IR_DEC_LDR_ACTIVE_MIN, regval);

	/* Time for leader idle part */
	regval = FIELD_PREP(IR_DEC_LDR_IDLE_MAX, timings->leader_idle_max) |
		 FIELD_PREP(IR_DEC_LDR_IDLE_MIN, timings->leader_idle_min);
	regmap_update_bits(ir->reg, IR_DEC_LDR_IDLE,
			   IR_DEC_LDR_IDLE_MAX | IR_DEC_LDR_IDLE_MIN, regval);

	/* Time for repeat leader idle part */
	regval = FIELD_PREP(IR_DEC_LDR_REPEAT_MAX, timings->repeat_leader_max) |
		 FIELD_PREP(IR_DEC_LDR_REPEAT_MIN, timings->repeat_leader_min);
	regmap_update_bits(ir->reg, IR_DEC_LDR_REPEAT, IR_DEC_LDR_REPEAT_MAX |
			   IR_DEC_LDR_REPEAT_MIN, regval);

	/*
	 * NEC: Time for logic '0'
	 * RC6: Time for half of trailer bit
	 */
	regval = FIELD_PREP(IR_DEC_BIT_0_MAX, timings->bit0_max) |
		 FIELD_PREP(IR_DEC_BIT_0_MIN, timings->bit0_min);
	regmap_update_bits(ir->reg, IR_DEC_BIT_0,
			   IR_DEC_BIT_0_MAX | IR_DEC_BIT_0_MIN, regval);

	/*
	 * NEC: Time for logic '1'
	 * RC6: Time for whole of trailer bit
	 */
	regval = FIELD_PREP(IR_DEC_STATUS_BIT_1_MAX, timings->bit1_max) |
		 FIELD_PREP(IR_DEC_STATUS_BIT_1_MIN, timings->bit1_min);
	regmap_update_bits(ir->reg, IR_DEC_STATUS, IR_DEC_STATUS_BIT_1_MAX |
			   IR_DEC_STATUS_BIT_1_MIN, regval);

	/* Enable to match logic '1' */
	regmap_update_bits(ir->reg, IR_DEC_STATUS, IR_DEC_STATUS_BIT_1_ENABLE,
			   timings->bit1_match_enable ?
			   IR_DEC_STATUS_BIT_1_ENABLE : 0);

	/*
	 * NEC: Unused
	 * RC6: Time for halt of logic 0/1
	 */
	regval = FIELD_PREP(IR_DEC_DURATN2_MAX, timings->duration2_max) |
		 FIELD_PREP(IR_DEC_DURATN2_MIN, timings->duration2_min);
	regmap_update_bits(ir->reg, IR_DEC_DURATN2,
			   IR_DEC_DURATN2_MAX | IR_DEC_DURATN2_MIN, regval);

	/*
	 * NEC: Unused
	 * RC6: Time for whole logic 0/1
	 */
	regval = FIELD_PREP(IR_DEC_DURATN3_MAX, timings->duration3_max) |
		 FIELD_PREP(IR_DEC_DURATN3_MIN, timings->duration3_min);
	regmap_update_bits(ir->reg, IR_DEC_DURATN3,
			   IR_DEC_DURATN3_MAX | IR_DEC_DURATN3_MIN, regval);

	/* Reset ir decoder and enable decode */
	regmap_update_bits(ir->reg, IR_DEC_REG1, IR_DEC_REG1_RESET,
			   IR_DEC_REG1_RESET);
	regmap_update_bits(ir->reg, IR_DEC_REG1, IR_DEC_REG1_RESET, 0);
	regmap_update_bits(ir->reg, IR_DEC_REG1, IR_DEC_REG1_ENABLE,
			   IR_DEC_REG1_ENABLE);

	spin_unlock_irqrestore(&ir->lock, flags);

	dev_info(&dev->dev, "hw decoder init, protocol: %d\n", protocol);

	return 0;
}

static void meson_ir_sw_decoder_init(struct rc_dev *dev)
{
	unsigned long flags;
	struct meson_ir *ir = dev->priv;

	spin_lock_irqsave(&ir->lock, flags);

	/* Reset the decoder */
	regmap_update_bits(ir->reg, IR_DEC_REG1, IR_DEC_REG1_RESET,
			   IR_DEC_REG1_RESET);
	regmap_update_bits(ir->reg, IR_DEC_REG1, IR_DEC_REG1_RESET, 0);

	/* Set general operation mode (= raw/software decoding) */
	if (of_device_is_compatible(dev->dev.of_node, "amlogic,meson6-ir"))
		regmap_update_bits(ir->reg, IR_DEC_REG1, IR_DEC_REG1_MODE,
				   FIELD_PREP(IR_DEC_REG1_MODE,
					      DEC_MODE_RAW));
	else
		regmap_update_bits(ir->reg, IR_DEC_REG2, IR_DEC_REG2_MODE,
				   FIELD_PREP(IR_DEC_REG2_MODE,
					      DEC_MODE_RAW));

	/* Set rate */
	regmap_update_bits(ir->reg, IR_DEC_REG0, IR_DEC_REG0_BASE_TIME,
			   FIELD_PREP(IR_DEC_REG0_BASE_TIME,
				      MESON_RAW_TRATE - 1));
	/* IRQ on rising and falling edges */
	regmap_update_bits(ir->reg, IR_DEC_REG1, IR_DEC_REG1_IRQSEL,
			   FIELD_PREP(IR_DEC_REG1_IRQSEL, IRQSEL_RISE_FALL));
	/* Enable the decoder */
	regmap_update_bits(ir->reg, IR_DEC_REG1, IR_DEC_REG1_ENABLE,
			   IR_DEC_REG1_ENABLE);

	spin_unlock_irqrestore(&ir->lock, flags);

	dev_info(&dev->dev, "sw decoder init\n");
}

static int meson_ir_probe(struct platform_device *pdev)
{
	const struct meson_ir_param *match_data;
	struct device *dev = &pdev->dev;
	struct device_node *node = dev->of_node;
	void __iomem *res_start;
	const char *map_name;
	struct meson_ir *ir;
	int irq, ret;

	ir = devm_kzalloc(dev, sizeof(struct meson_ir), GFP_KERNEL);
	if (!ir)
		return -ENOMEM;

	match_data = of_device_get_match_data(dev);
	if (!match_data)
		return dev_err_probe(dev, -ENODEV, "failed to get match data\n");

	ir->param = match_data;

	res_start = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(res_start))
		return PTR_ERR(res_start);

	meson_ir_regmap_config.max_register = ir->param->max_register;
	ir->reg = devm_regmap_init_mmio(&pdev->dev, res_start,
					&meson_ir_regmap_config);
	if (IS_ERR(ir->reg))
		return PTR_ERR(ir->reg);

	irq = platform_get_irq(pdev, 0);
	if (irq < 0)
		return irq;

	if (ir->param->support_hw_decoder)
		ir->rc = devm_rc_allocate_device(&pdev->dev,
						 RC_DRIVER_SCANCODE);
	else
		ir->rc = devm_rc_allocate_device(&pdev->dev, RC_DRIVER_IR_RAW);

	if (!ir->rc) {
		dev_err(dev, "failed to allocate rc device\n");
		return -ENOMEM;
	}

	if (ir->rc->driver_type == RC_DRIVER_IR_RAW) {
		ir->rc->allowed_protocols = RC_PROTO_BIT_ALL_IR_DECODER;
		ir->rc->rx_resolution = MESON_RAW_TRATE;
		ir->rc->min_timeout = 1;
		ir->rc->timeout = IR_DEFAULT_TIMEOUT;
		ir->rc->max_timeout = 10 * IR_DEFAULT_TIMEOUT;
	} else if (ir->rc->driver_type == RC_DRIVER_SCANCODE) {
		ir->rc->allowed_protocols = RC_PROTO_BIT_NEC;
		ir->rc->change_protocol = meson_ir_hw_decoder_init;
	}

	ir->rc->priv = ir;
	ir->rc->device_name = DRIVER_NAME;
	ir->rc->input_phys = DRIVER_NAME "/input0";
	ir->rc->input_id.bustype = BUS_HOST;
	map_name = of_get_property(node, "linux,rc-map-name", NULL);
	ir->rc->map_name = map_name ? map_name : RC_MAP_EMPTY;
	ir->rc->driver_name = DRIVER_NAME;

	spin_lock_init(&ir->lock);
	platform_set_drvdata(pdev, ir);

	ret = devm_rc_register_device(dev, ir->rc);
	if (ret) {
		dev_err(dev, "failed to register rc device\n");
		return ret;
	}

	if (ir->rc->driver_type == RC_DRIVER_IR_RAW)
		meson_ir_sw_decoder_init(ir->rc);

	ret = devm_request_irq(dev, irq, meson_ir_irq, 0, "meson_ir", ir);
	if (ret) {
		dev_err(dev, "failed to request irq\n");
		return ret;
	}

	dev_info(dev, "receiver initialized\n");

	return 0;
}

static void meson_ir_remove(struct platform_device *pdev)
{
	struct meson_ir *ir = platform_get_drvdata(pdev);
	unsigned long flags;

	/* Disable the decoder */
	spin_lock_irqsave(&ir->lock, flags);
	regmap_update_bits(ir->reg, IR_DEC_REG1, IR_DEC_REG1_ENABLE, 0);
	spin_unlock_irqrestore(&ir->lock, flags);
}

static void meson_ir_shutdown(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct device_node *node = dev->of_node;
	struct meson_ir *ir = platform_get_drvdata(pdev);
	unsigned long flags;

	spin_lock_irqsave(&ir->lock, flags);

	/*
	 * Set operation mode to NEC/hardware decoding to give
	 * bootloader a chance to power the system back on
	 */
	if (of_device_is_compatible(node, "amlogic,meson6-ir"))
		regmap_update_bits(ir->reg, IR_DEC_REG1, IR_DEC_REG1_MODE,
				   FIELD_PREP(IR_DEC_REG1_MODE, DEC_MODE_NEC));
	else
		regmap_update_bits(ir->reg, IR_DEC_REG2, IR_DEC_REG2_MODE,
				   FIELD_PREP(IR_DEC_REG2_MODE, DEC_MODE_NEC));

	/* Set rate to default value */
	regmap_update_bits(ir->reg, IR_DEC_REG0, IR_DEC_REG0_BASE_TIME,
			   FIELD_PREP(IR_DEC_REG0_BASE_TIME,
				      MESON_HW_TRATE - 1));

	spin_unlock_irqrestore(&ir->lock, flags);
}

static __maybe_unused int meson_ir_resume(struct device *dev)
{
	struct meson_ir *ir = dev_get_drvdata(dev);

	if (ir->param->support_hw_decoder)
		meson_ir_hw_decoder_init(ir->rc, &ir->rc->enabled_protocols);
	else
		meson_ir_sw_decoder_init(ir->rc);

	return 0;
}

static __maybe_unused int meson_ir_suspend(struct device *dev)
{
	struct meson_ir *ir = dev_get_drvdata(dev);
	unsigned long flags;

	spin_lock_irqsave(&ir->lock, flags);
	regmap_update_bits(ir->reg, IR_DEC_REG1, IR_DEC_REG1_ENABLE, 0);
	spin_unlock_irqrestore(&ir->lock, flags);

	return 0;
}

static SIMPLE_DEV_PM_OPS(meson_ir_pm_ops, meson_ir_suspend, meson_ir_resume);

static const struct meson_ir_param meson6_ir_param = {
	.support_hw_decoder = false,
	.max_register = IR_DEC_REG1,
};

static const struct meson_ir_param meson8b_ir_param = {
	.support_hw_decoder = false,
	.max_register = IR_DEC_REG2,
};

static const struct meson_ir_param meson_s4_ir_param = {
	.support_hw_decoder = true,
	.max_register = IR_DEC_FRAME1,
};

static const struct of_device_id meson_ir_match[] = {
	{
		.compatible = "amlogic,meson6-ir",
		.data = &meson6_ir_param,
	}, {
		.compatible = "amlogic,meson8b-ir",
		.data = &meson8b_ir_param,
	}, {
		.compatible = "amlogic,meson-gxbb-ir",
		.data = &meson8b_ir_param,
	}, {
		.compatible = "amlogic,meson-s4-ir",
		.data = &meson_s4_ir_param,
	},
	{},
};
MODULE_DEVICE_TABLE(of, meson_ir_match);

static struct platform_driver meson_ir_driver = {
	.probe		= meson_ir_probe,
	.remove		= meson_ir_remove,
	.shutdown	= meson_ir_shutdown,
	.driver = {
		.name		= DRIVER_NAME,
		.of_match_table	= meson_ir_match,
		.pm = pm_ptr(&meson_ir_pm_ops),
	},
};

module_platform_driver(meson_ir_driver);

MODULE_DESCRIPTION("Amlogic Meson IR remote receiver driver");
MODULE_AUTHOR("Beniamino Galvani <b.galvani@gmail.com>");
MODULE_LICENSE("GPL v2");
