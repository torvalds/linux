/*
 * AD714X CapTouch Programmable Controller driver supporting AD7142/3/7/8/7A
 *
 * Copyright 2009-2011 Analog Devices Inc.
 *
 * Licensed under the GPL-2 or later.
 */

#include <linux/device.h>
#include <linux/init.h>
#include <linux/input.h>
#include <linux/interrupt.h>
#include <linux/slab.h>
#include <linux/input/ad714x.h>
#include <linux/module.h>
#include "ad714x.h"

#define AD714X_PWR_CTRL           0x0
#define AD714X_STG_CAL_EN_REG     0x1
#define AD714X_AMB_COMP_CTRL0_REG 0x2
#define AD714X_PARTID_REG         0x17
#define AD7142_PARTID             0xE620
#define AD7143_PARTID             0xE630
#define AD7147_PARTID             0x1470
#define AD7148_PARTID             0x1480
#define AD714X_STAGECFG_REG       0x80
#define AD714X_SYSCFG_REG         0x0

#define STG_LOW_INT_EN_REG     0x5
#define STG_HIGH_INT_EN_REG    0x6
#define STG_COM_INT_EN_REG     0x7
#define STG_LOW_INT_STA_REG    0x8
#define STG_HIGH_INT_STA_REG   0x9
#define STG_COM_INT_STA_REG    0xA

#define CDC_RESULT_S0          0xB
#define CDC_RESULT_S1          0xC
#define CDC_RESULT_S2          0xD
#define CDC_RESULT_S3          0xE
#define CDC_RESULT_S4          0xF
#define CDC_RESULT_S5          0x10
#define CDC_RESULT_S6          0x11
#define CDC_RESULT_S7          0x12
#define CDC_RESULT_S8          0x13
#define CDC_RESULT_S9          0x14
#define CDC_RESULT_S10         0x15
#define CDC_RESULT_S11         0x16

#define STAGE0_AMBIENT		0xF1
#define STAGE1_AMBIENT		0x115
#define STAGE2_AMBIENT		0x139
#define STAGE3_AMBIENT		0x15D
#define STAGE4_AMBIENT		0x181
#define STAGE5_AMBIENT		0x1A5
#define STAGE6_AMBIENT		0x1C9
#define STAGE7_AMBIENT		0x1ED
#define STAGE8_AMBIENT		0x211
#define STAGE9_AMBIENT		0x234
#define STAGE10_AMBIENT		0x259
#define STAGE11_AMBIENT		0x27D

#define PER_STAGE_REG_NUM      36
#define STAGE_CFGREG_NUM       8
#define SYS_CFGREG_NUM         8

/*
 * driver information which will be used to maintain the software flow
 */
enum ad714x_device_state { IDLE, JITTER, ACTIVE, SPACE };

struct ad714x_slider_drv {
	int highest_stage;
	int abs_pos;
	int flt_pos;
	enum ad714x_device_state state;
	struct input_dev *input;
};

struct ad714x_wheel_drv {
	int abs_pos;
	int flt_pos;
	int pre_highest_stage;
	int highest_stage;
	enum ad714x_device_state state;
	struct input_dev *input;
};

struct ad714x_touchpad_drv {
	int x_highest_stage;
	int x_flt_pos;
	int x_abs_pos;
	int y_highest_stage;
	int y_flt_pos;
	int y_abs_pos;
	int left_ep;
	int left_ep_val;
	int right_ep;
	int right_ep_val;
	int top_ep;
	int top_ep_val;
	int bottom_ep;
	int bottom_ep_val;
	enum ad714x_device_state state;
	struct input_dev *input;
};

struct ad714x_button_drv {
	enum ad714x_device_state state;
	/*
	 * Unlike slider/wheel/touchpad, all buttons point to
	 * same input_dev instance
	 */
	struct input_dev *input;
};

struct ad714x_driver_data {
	struct ad714x_slider_drv *slider;
	struct ad714x_wheel_drv *wheel;
	struct ad714x_touchpad_drv *touchpad;
	struct ad714x_button_drv *button;
};

/*
 * information to integrate all things which will be private data
 * of spi/i2c device
 */

static void ad714x_use_com_int(struct ad714x_chip *ad714x,
				int start_stage, int end_stage)
{
	unsigned short data;
	unsigned short mask;

	mask = ((1 << (end_stage + 1)) - 1) - ((1 << start_stage) - 1);

	ad714x->read(ad714x, STG_COM_INT_EN_REG, &data, 1);
	data |= 1 << end_stage;
	ad714x->write(ad714x, STG_COM_INT_EN_REG, data);

	ad714x->read(ad714x, STG_HIGH_INT_EN_REG, &data, 1);
	data &= ~mask;
	ad714x->write(ad714x, STG_HIGH_INT_EN_REG, data);
}

static void ad714x_use_thr_int(struct ad714x_chip *ad714x,
				int start_stage, int end_stage)
{
	unsigned short data;
	unsigned short mask;

	mask = ((1 << (end_stage + 1)) - 1) - ((1 << start_stage) - 1);

	ad714x->read(ad714x, STG_COM_INT_EN_REG, &data, 1);
	data &= ~(1 << end_stage);
	ad714x->write(ad714x, STG_COM_INT_EN_REG, data);

	ad714x->read(ad714x, STG_HIGH_INT_EN_REG, &data, 1);
	data |= mask;
	ad714x->write(ad714x, STG_HIGH_INT_EN_REG, data);
}

static int ad714x_cal_highest_stage(struct ad714x_chip *ad714x,
					int start_stage, int end_stage)
{
	int max_res = 0;
	int max_idx = 0;
	int i;

	for (i = start_stage; i <= end_stage; i++) {
		if (ad714x->sensor_val[i] > max_res) {
			max_res = ad714x->sensor_val[i];
			max_idx = i;
		}
	}

	return max_idx;
}

static int ad714x_cal_abs_pos(struct ad714x_chip *ad714x,
				int start_stage, int end_stage,
				int highest_stage, int max_coord)
{
	int a_param, b_param;

	if (highest_stage == start_stage) {
		a_param = ad714x->sensor_val[start_stage + 1];
		b_param = ad714x->sensor_val[start_stage] +
			ad714x->sensor_val[start_stage + 1];
	} else if (highest_stage == end_stage) {
		a_param = ad714x->sensor_val[end_stage] *
			(end_stage - start_stage) +
			ad714x->sensor_val[end_stage - 1] *
			(end_stage - start_stage - 1);
		b_param = ad714x->sensor_val[end_stage] +
			ad714x->sensor_val[end_stage - 1];
	} else {
		a_param = ad714x->sensor_val[highest_stage] *
			(highest_stage - start_stage) +
			ad714x->sensor_val[highest_stage - 1] *
			(highest_stage - start_stage - 1) +
			ad714x->sensor_val[highest_stage + 1] *
			(highest_stage - start_stage + 1);
		b_param = ad714x->sensor_val[highest_stage] +
			ad714x->sensor_val[highest_stage - 1] +
			ad714x->sensor_val[highest_stage + 1];
	}

	return (max_coord / (end_stage - start_stage)) * a_param / b_param;
}

/*
 * One button can connect to multi positive and negative of CDCs
 * Multi-buttons can connect to same positive/negative of one CDC
 */
static void ad714x_button_state_machine(struct ad714x_chip *ad714x, int idx)
{
	struct ad714x_button_plat *hw = &ad714x->hw->button[idx];
	struct ad714x_button_drv *sw = &ad714x->sw->button[idx];

	switch (sw->state) {
	case IDLE:
		if (((ad714x->h_state & hw->h_mask) == hw->h_mask) &&
		    ((ad714x->l_state & hw->l_mask) == hw->l_mask)) {
			dev_dbg(ad714x->dev, "button %d touched\n", idx);
			input_report_key(sw->input, hw->keycode, 1);
			input_sync(sw->input);
			sw->state = ACTIVE;
		}
		break;

	case ACTIVE:
		if (((ad714x->h_state & hw->h_mask) != hw->h_mask) ||
		    ((ad714x->l_state & hw->l_mask) != hw->l_mask)) {
			dev_dbg(ad714x->dev, "button %d released\n", idx);
			input_report_key(sw->input, hw->keycode, 0);
			input_sync(sw->input);
			sw->state = IDLE;
		}
		break;

	default:
		break;
	}
}

/*
 * The response of a sensor is defined by the absolute number of codes
 * between the current CDC value and the ambient value.
 */
static void ad714x_slider_cal_sensor_val(struct ad714x_chip *ad714x, int idx)
{
	struct ad714x_slider_plat *hw = &ad714x->hw->slider[idx];
	int i;

	ad714x->read(ad714x, CDC_RESULT_S0 + hw->start_stage,
			&ad714x->adc_reg[hw->start_stage],
			hw->end_stage - hw->start_stage + 1);

	for (i = hw->start_stage; i <= hw->end_stage; i++) {
		ad714x->read(ad714x, STAGE0_AMBIENT + i * PER_STAGE_REG_NUM,
				&ad714x->amb_reg[i], 1);

		ad714x->sensor_val[i] =
			abs(ad714x->adc_reg[i] - ad714x->amb_reg[i]);
	}
}

static void ad714x_slider_cal_highest_stage(struct ad714x_chip *ad714x, int idx)
{
	struct ad714x_slider_plat *hw = &ad714x->hw->slider[idx];
	struct ad714x_slider_drv *sw = &ad714x->sw->slider[idx];

	sw->highest_stage = ad714x_cal_highest_stage(ad714x, hw->start_stage,
			hw->end_stage);

	dev_dbg(ad714x->dev, "slider %d highest_stage:%d\n", idx,
		sw->highest_stage);
}

/*
 * The formulae are very straight forward. It uses the sensor with the
 * highest response and the 2 adjacent ones.
 * When Sensor 0 has the highest response, only sensor 0 and sensor 1
 * are used in the calculations. Similarly when the last sensor has the
 * highest response, only the last sensor and the second last sensors
 * are used in the calculations.
 *
 * For i= idx_of_peak_Sensor-1 to i= idx_of_peak_Sensor+1
 *         v += Sensor response(i)*i
 *         w += Sensor response(i)
 * POS=(Number_of_Positions_Wanted/(Number_of_Sensors_Used-1)) *(v/w)
 */
static void ad714x_slider_cal_abs_pos(struct ad714x_chip *ad714x, int idx)
{
	struct ad714x_slider_plat *hw = &ad714x->hw->slider[idx];
	struct ad714x_slider_drv *sw = &ad714x->sw->slider[idx];

	sw->abs_pos = ad714x_cal_abs_pos(ad714x, hw->start_stage, hw->end_stage,
		sw->highest_stage, hw->max_coord);

	dev_dbg(ad714x->dev, "slider %d absolute position:%d\n", idx,
		sw->abs_pos);
}

/*
 * To minimise the Impact of the noise on the algorithm, ADI developed a
 * routine that filters the CDC results after they have been read by the
 * host processor.
 * The filter used is an Infinite Input Response(IIR) filter implemented
 * in firmware and attenuates the noise on the CDC results after they've
 * been read by the host processor.
 * Filtered_CDC_result = (Filtered_CDC_result * (10 - Coefficient) +
 *				Latest_CDC_result * Coefficient)/10
 */
static void ad714x_slider_cal_flt_pos(struct ad714x_chip *ad714x, int idx)
{
	struct ad714x_slider_drv *sw = &ad714x->sw->slider[idx];

	sw->flt_pos = (sw->flt_pos * (10 - 4) +
			sw->abs_pos * 4)/10;

	dev_dbg(ad714x->dev, "slider %d filter position:%d\n", idx,
		sw->flt_pos);
}

static void ad714x_slider_use_com_int(struct ad714x_chip *ad714x, int idx)
{
	struct ad714x_slider_plat *hw = &ad714x->hw->slider[idx];

	ad714x_use_com_int(ad714x, hw->start_stage, hw->end_stage);
}

static void ad714x_slider_use_thr_int(struct ad714x_chip *ad714x, int idx)
{
	struct ad714x_slider_plat *hw = &ad714x->hw->slider[idx];

	ad714x_use_thr_int(ad714x, hw->start_stage, hw->end_stage);
}

static void ad714x_slider_state_machine(struct ad714x_chip *ad714x, int idx)
{
	struct ad714x_slider_plat *hw = &ad714x->hw->slider[idx];
	struct ad714x_slider_drv *sw = &ad714x->sw->slider[idx];
	unsigned short h_state, c_state;
	unsigned short mask;

	mask = ((1 << (hw->end_stage + 1)) - 1) - ((1 << hw->start_stage) - 1);

	h_state = ad714x->h_state & mask;
	c_state = ad714x->c_state & mask;

	switch (sw->state) {
	case IDLE:
		if (h_state) {
			sw->state = JITTER;
			/* In End of Conversion interrupt mode, the AD714X
			 * continuously generates hardware interrupts.
			 */
			ad714x_slider_use_com_int(ad714x, idx);
			dev_dbg(ad714x->dev, "slider %d touched\n", idx);
		}
		break;

	case JITTER:
		if (c_state == mask) {
			ad714x_slider_cal_sensor_val(ad714x, idx);
			ad714x_slider_cal_highest_stage(ad714x, idx);
			ad714x_slider_cal_abs_pos(ad714x, idx);
			sw->flt_pos = sw->abs_pos;
			sw->state = ACTIVE;
		}
		break;

	case ACTIVE:
		if (c_state == mask) {
			if (h_state) {
				ad714x_slider_cal_sensor_val(ad714x, idx);
				ad714x_slider_cal_highest_stage(ad714x, idx);
				ad714x_slider_cal_abs_pos(ad714x, idx);
				ad714x_slider_cal_flt_pos(ad714x, idx);
				input_report_abs(sw->input, ABS_X, sw->flt_pos);
				input_report_key(sw->input, BTN_TOUCH, 1);
			} else {
				/* When the user lifts off the sensor, configure
				 * the AD714X back to threshold interrupt mode.
				 */
				ad714x_slider_use_thr_int(ad714x, idx);
				sw->state = IDLE;
				input_report_key(sw->input, BTN_TOUCH, 0);
				dev_dbg(ad714x->dev, "slider %d released\n",
					idx);
			}
			input_sync(sw->input);
		}
		break;

	default:
		break;
	}
}

/*
 * When the scroll wheel is activated, we compute the absolute position based
 * on the sensor values. To calculate the position, we first determine the
 * sensor that has the greatest response among the 8 sensors that constitutes
 * the scrollwheel. Then we determined the 2 sensors on either sides of the
 * sensor with the highest response and we apply weights to these sensors.
 */
static void ad714x_wheel_cal_highest_stage(struct ad714x_chip *ad714x, int idx)
{
	struct ad714x_wheel_plat *hw = &ad714x->hw->wheel[idx];
	struct ad714x_wheel_drv *sw = &ad714x->sw->wheel[idx];

	sw->pre_highest_stage = sw->highest_stage;
	sw->highest_stage = ad714x_cal_highest_stage(ad714x, hw->start_stage,
			hw->end_stage);

	dev_dbg(ad714x->dev, "wheel %d highest_stage:%d\n", idx,
		sw->highest_stage);
}

static void ad714x_wheel_cal_sensor_val(struct ad714x_chip *ad714x, int idx)
{
	struct ad714x_wheel_plat *hw = &ad714x->hw->wheel[idx];
	int i;

	ad714x->read(ad714x, CDC_RESULT_S0 + hw->start_stage,
			&ad714x->adc_reg[hw->start_stage],
			hw->end_stage - hw->start_stage + 1);

	for (i = hw->start_stage; i <= hw->end_stage; i++) {
		ad714x->read(ad714x, STAGE0_AMBIENT + i * PER_STAGE_REG_NUM,
				&ad714x->amb_reg[i], 1);
		if (ad714x->adc_reg[i] > ad714x->amb_reg[i])
			ad714x->sensor_val[i] =
				ad714x->adc_reg[i] - ad714x->amb_reg[i];
		else
			ad714x->sensor_val[i] = 0;
	}
}

/*
 * When the scroll wheel is activated, we compute the absolute position based
 * on the sensor values. To calculate the position, we first determine the
 * sensor that has the greatest response among the sensors that constitutes
 * the scrollwheel. Then we determined the sensors on either sides of the
 * sensor with the highest response and we apply weights to these sensors. The
 * result of this computation gives us the mean value.
 */

static void ad714x_wheel_cal_abs_pos(struct ad714x_chip *ad714x, int idx)
{
	struct ad714x_wheel_plat *hw = &ad714x->hw->wheel[idx];
	struct ad714x_wheel_drv *sw = &ad714x->sw->wheel[idx];
	int stage_num = hw->end_stage - hw->start_stage + 1;
	int first_before, highest, first_after;
	int a_param, b_param;

	first_before = (sw->highest_stage + stage_num - 1) % stage_num;
	highest = sw->highest_stage;
	first_after = (sw->highest_stage + stage_num + 1) % stage_num;

	a_param = ad714x->sensor_val[highest] *
		(highest - hw->start_stage) +
		ad714x->sensor_val[first_before] *
		(highest - hw->start_stage - 1) +
		ad714x->sensor_val[first_after] *
		(highest - hw->start_stage + 1);
	b_param = ad714x->sensor_val[highest] +
		ad714x->sensor_val[first_before] +
		ad714x->sensor_val[first_after];

	sw->abs_pos = ((hw->max_coord / (hw->end_stage - hw->start_stage)) *
			a_param) / b_param;

	if (sw->abs_pos > hw->max_coord)
		sw->abs_pos = hw->max_coord;
	else if (sw->abs_pos < 0)
		sw->abs_pos = 0;
}

static void ad714x_wheel_cal_flt_pos(struct ad714x_chip *ad714x, int idx)
{
	struct ad714x_wheel_plat *hw = &ad714x->hw->wheel[idx];
	struct ad714x_wheel_drv *sw = &ad714x->sw->wheel[idx];
	if (((sw->pre_highest_stage == hw->end_stage) &&
			(sw->highest_stage == hw->start_stage)) ||
	    ((sw->pre_highest_stage == hw->start_stage) &&
			(sw->highest_stage == hw->end_stage)))
		sw->flt_pos = sw->abs_pos;
	else
		sw->flt_pos = ((sw->flt_pos * 30) + (sw->abs_pos * 71)) / 100;

	if (sw->flt_pos > hw->max_coord)
		sw->flt_pos = hw->max_coord;
}

static void ad714x_wheel_use_com_int(struct ad714x_chip *ad714x, int idx)
{
	struct ad714x_wheel_plat *hw = &ad714x->hw->wheel[idx];

	ad714x_use_com_int(ad714x, hw->start_stage, hw->end_stage);
}

static void ad714x_wheel_use_thr_int(struct ad714x_chip *ad714x, int idx)
{
	struct ad714x_wheel_plat *hw = &ad714x->hw->wheel[idx];

	ad714x_use_thr_int(ad714x, hw->start_stage, hw->end_stage);
}

static void ad714x_wheel_state_machine(struct ad714x_chip *ad714x, int idx)
{
	struct ad714x_wheel_plat *hw = &ad714x->hw->wheel[idx];
	struct ad714x_wheel_drv *sw = &ad714x->sw->wheel[idx];
	unsigned short h_state, c_state;
	unsigned short mask;

	mask = ((1 << (hw->end_stage + 1)) - 1) - ((1 << hw->start_stage) - 1);

	h_state = ad714x->h_state & mask;
	c_state = ad714x->c_state & mask;

	switch (sw->state) {
	case IDLE:
		if (h_state) {
			sw->state = JITTER;
			/* In End of Conversion interrupt mode, the AD714X
			 * continuously generates hardware interrupts.
			 */
			ad714x_wheel_use_com_int(ad714x, idx);
			dev_dbg(ad714x->dev, "wheel %d touched\n", idx);
		}
		break;

	case JITTER:
		if (c_state == mask)	{
			ad714x_wheel_cal_sensor_val(ad714x, idx);
			ad714x_wheel_cal_highest_stage(ad714x, idx);
			ad714x_wheel_cal_abs_pos(ad714x, idx);
			sw->flt_pos = sw->abs_pos;
			sw->state = ACTIVE;
		}
		break;

	case ACTIVE:
		if (c_state == mask) {
			if (h_state) {
				ad714x_wheel_cal_sensor_val(ad714x, idx);
				ad714x_wheel_cal_highest_stage(ad714x, idx);
				ad714x_wheel_cal_abs_pos(ad714x, idx);
				ad714x_wheel_cal_flt_pos(ad714x, idx);
				input_report_abs(sw->input, ABS_WHEEL,
					sw->flt_pos);
				input_report_key(sw->input, BTN_TOUCH, 1);
			} else {
				/* When the user lifts off the sensor, configure
				 * the AD714X back to threshold interrupt mode.
				 */
				ad714x_wheel_use_thr_int(ad714x, idx);
				sw->state = IDLE;
				input_report_key(sw->input, BTN_TOUCH, 0);

				dev_dbg(ad714x->dev, "wheel %d released\n",
					idx);
			}
			input_sync(sw->input);
		}
		break;

	default:
		break;
	}
}

static void touchpad_cal_sensor_val(struct ad714x_chip *ad714x, int idx)
{
	struct ad714x_touchpad_plat *hw = &ad714x->hw->touchpad[idx];
	int i;

	ad714x->read(ad714x, CDC_RESULT_S0 + hw->x_start_stage,
			&ad714x->adc_reg[hw->x_start_stage],
			hw->x_end_stage - hw->x_start_stage + 1);

	for (i = hw->x_start_stage; i <= hw->x_end_stage; i++) {
		ad714x->read(ad714x, STAGE0_AMBIENT + i * PER_STAGE_REG_NUM,
				&ad714x->amb_reg[i], 1);
		if (ad714x->adc_reg[i] > ad714x->amb_reg[i])
			ad714x->sensor_val[i] =
				ad714x->adc_reg[i] - ad714x->amb_reg[i];
		else
			ad714x->sensor_val[i] = 0;
	}
}

static void touchpad_cal_highest_stage(struct ad714x_chip *ad714x, int idx)
{
	struct ad714x_touchpad_plat *hw = &ad714x->hw->touchpad[idx];
	struct ad714x_touchpad_drv *sw = &ad714x->sw->touchpad[idx];

	sw->x_highest_stage = ad714x_cal_highest_stage(ad714x,
		hw->x_start_stage, hw->x_end_stage);
	sw->y_highest_stage = ad714x_cal_highest_stage(ad714x,
		hw->y_start_stage, hw->y_end_stage);

	dev_dbg(ad714x->dev,
		"touchpad %d x_highest_stage:%d, y_highest_stage:%d\n",
		idx, sw->x_highest_stage, sw->y_highest_stage);
}

/*
 * If 2 fingers are touching the sensor then 2 peaks can be observed in the
 * distribution.
 * The arithmetic doesn't support to get absolute coordinates for multi-touch
 * yet.
 */
static int touchpad_check_second_peak(struct ad714x_chip *ad714x, int idx)
{
	struct ad714x_touchpad_plat *hw = &ad714x->hw->touchpad[idx];
	struct ad714x_touchpad_drv *sw = &ad714x->sw->touchpad[idx];
	int i;

	for (i = hw->x_start_stage; i < sw->x_highest_stage; i++) {
		if ((ad714x->sensor_val[i] - ad714x->sensor_val[i + 1])
			> (ad714x->sensor_val[i + 1] / 10))
			return 1;
	}

	for (i = sw->x_highest_stage; i < hw->x_end_stage; i++) {
		if ((ad714x->sensor_val[i + 1] - ad714x->sensor_val[i])
			> (ad714x->sensor_val[i] / 10))
			return 1;
	}

	for (i = hw->y_start_stage; i < sw->y_highest_stage; i++) {
		if ((ad714x->sensor_val[i] - ad714x->sensor_val[i + 1])
			> (ad714x->sensor_val[i + 1] / 10))
			return 1;
	}

	for (i = sw->y_highest_stage; i < hw->y_end_stage; i++) {
		if ((ad714x->sensor_val[i + 1] - ad714x->sensor_val[i])
			> (ad714x->sensor_val[i] / 10))
			return 1;
	}

	return 0;
}

/*
 * If only one finger is used to activate the touch pad then only 1 peak will be
 * registered in the distribution. This peak and the 2 adjacent sensors will be
 * used in the calculation of the absolute position. This will prevent hand
 * shadows to affect the absolute position calculation.
 */
static void touchpad_cal_abs_pos(struct ad714x_chip *ad714x, int idx)
{
	struct ad714x_touchpad_plat *hw = &ad714x->hw->touchpad[idx];
	struct ad714x_touchpad_drv *sw = &ad714x->sw->touchpad[idx];

	sw->x_abs_pos = ad714x_cal_abs_pos(ad714x, hw->x_start_stage,
			hw->x_end_stage, sw->x_highest_stage, hw->x_max_coord);
	sw->y_abs_pos = ad714x_cal_abs_pos(ad714x, hw->y_start_stage,
			hw->y_end_stage, sw->y_highest_stage, hw->y_max_coord);

	dev_dbg(ad714x->dev, "touchpad %d absolute position:(%d, %d)\n", idx,
			sw->x_abs_pos, sw->y_abs_pos);
}

static void touchpad_cal_flt_pos(struct ad714x_chip *ad714x, int idx)
{
	struct ad714x_touchpad_drv *sw = &ad714x->sw->touchpad[idx];

	sw->x_flt_pos = (sw->x_flt_pos * (10 - 4) +
			sw->x_abs_pos * 4)/10;
	sw->y_flt_pos = (sw->y_flt_pos * (10 - 4) +
			sw->y_abs_pos * 4)/10;

	dev_dbg(ad714x->dev, "touchpad %d filter position:(%d, %d)\n",
			idx, sw->x_flt_pos, sw->y_flt_pos);
}

/*
 * To prevent distortion from showing in the absolute position, it is
 * necessary to detect the end points. When endpoints are detected, the
 * driver stops updating the status variables with absolute positions.
 * End points are detected on the 4 edges of the touchpad sensor. The
 * method to detect them is the same for all 4.
 * To detect the end points, the firmware computes the difference in
 * percent between the sensor on the edge and the adjacent one. The
 * difference is calculated in percent in order to make the end point
 * detection independent of the pressure.
 */

#define LEFT_END_POINT_DETECTION_LEVEL                  550
#define RIGHT_END_POINT_DETECTION_LEVEL                 750
#define LEFT_RIGHT_END_POINT_DEAVTIVALION_LEVEL         850
#define TOP_END_POINT_DETECTION_LEVEL                   550
#define BOTTOM_END_POINT_DETECTION_LEVEL                950
#define TOP_BOTTOM_END_POINT_DEAVTIVALION_LEVEL         700
static int touchpad_check_endpoint(struct ad714x_chip *ad714x, int idx)
{
	struct ad714x_touchpad_plat *hw = &ad714x->hw->touchpad[idx];
	struct ad714x_touchpad_drv *sw  = &ad714x->sw->touchpad[idx];
	int percent_sensor_diff;

	/* left endpoint detect */
	percent_sensor_diff = (ad714x->sensor_val[hw->x_start_stage] -
			ad714x->sensor_val[hw->x_start_stage + 1]) * 100 /
			ad714x->sensor_val[hw->x_start_stage + 1];
	if (!sw->left_ep) {
		if (percent_sensor_diff >= LEFT_END_POINT_DETECTION_LEVEL)  {
			sw->left_ep = 1;
			sw->left_ep_val =
				ad714x->sensor_val[hw->x_start_stage + 1];
		}
	} else {
		if ((percent_sensor_diff < LEFT_END_POINT_DETECTION_LEVEL) &&
		    (ad714x->sensor_val[hw->x_start_stage + 1] >
		     LEFT_RIGHT_END_POINT_DEAVTIVALION_LEVEL + sw->left_ep_val))
			sw->left_ep = 0;
	}

	/* right endpoint detect */
	percent_sensor_diff = (ad714x->sensor_val[hw->x_end_stage] -
			ad714x->sensor_val[hw->x_end_stage - 1]) * 100 /
			ad714x->sensor_val[hw->x_end_stage - 1];
	if (!sw->right_ep) {
		if (percent_sensor_diff >= RIGHT_END_POINT_DETECTION_LEVEL)  {
			sw->right_ep = 1;
			sw->right_ep_val =
				ad714x->sensor_val[hw->x_end_stage - 1];
		}
	} else {
		if ((percent_sensor_diff < RIGHT_END_POINT_DETECTION_LEVEL) &&
		(ad714x->sensor_val[hw->x_end_stage - 1] >
		LEFT_RIGHT_END_POINT_DEAVTIVALION_LEVEL + sw->right_ep_val))
			sw->right_ep = 0;
	}

	/* top endpoint detect */
	percent_sensor_diff = (ad714x->sensor_val[hw->y_start_stage] -
			ad714x->sensor_val[hw->y_start_stage + 1]) * 100 /
			ad714x->sensor_val[hw->y_start_stage + 1];
	if (!sw->top_ep) {
		if (percent_sensor_diff >= TOP_END_POINT_DETECTION_LEVEL)  {
			sw->top_ep = 1;
			sw->top_ep_val =
				ad714x->sensor_val[hw->y_start_stage + 1];
		}
	} else {
		if ((percent_sensor_diff < TOP_END_POINT_DETECTION_LEVEL) &&
		(ad714x->sensor_val[hw->y_start_stage + 1] >
		TOP_BOTTOM_END_POINT_DEAVTIVALION_LEVEL + sw->top_ep_val))
			sw->top_ep = 0;
	}

	/* bottom endpoint detect */
	percent_sensor_diff = (ad714x->sensor_val[hw->y_end_stage] -
		ad714x->sensor_val[hw->y_end_stage - 1]) * 100 /
		ad714x->sensor_val[hw->y_end_stage - 1];
	if (!sw->bottom_ep) {
		if (percent_sensor_diff >= BOTTOM_END_POINT_DETECTION_LEVEL)  {
			sw->bottom_ep = 1;
			sw->bottom_ep_val =
				ad714x->sensor_val[hw->y_end_stage - 1];
		}
	} else {
		if ((percent_sensor_diff < BOTTOM_END_POINT_DETECTION_LEVEL) &&
		(ad714x->sensor_val[hw->y_end_stage - 1] >
		 TOP_BOTTOM_END_POINT_DEAVTIVALION_LEVEL + sw->bottom_ep_val))
			sw->bottom_ep = 0;
	}

	return sw->left_ep || sw->right_ep || sw->top_ep || sw->bottom_ep;
}

static void touchpad_use_com_int(struct ad714x_chip *ad714x, int idx)
{
	struct ad714x_touchpad_plat *hw = &ad714x->hw->touchpad[idx];

	ad714x_use_com_int(ad714x, hw->x_start_stage, hw->x_end_stage);
}

static void touchpad_use_thr_int(struct ad714x_chip *ad714x, int idx)
{
	struct ad714x_touchpad_plat *hw = &ad714x->hw->touchpad[idx];

	ad714x_use_thr_int(ad714x, hw->x_start_stage, hw->x_end_stage);
	ad714x_use_thr_int(ad714x, hw->y_start_stage, hw->y_end_stage);
}

static void ad714x_touchpad_state_machine(struct ad714x_chip *ad714x, int idx)
{
	struct ad714x_touchpad_plat *hw = &ad714x->hw->touchpad[idx];
	struct ad714x_touchpad_drv *sw = &ad714x->sw->touchpad[idx];
	unsigned short h_state, c_state;
	unsigned short mask;

	mask = (((1 << (hw->x_end_stage + 1)) - 1) -
		((1 << hw->x_start_stage) - 1)) +
		(((1 << (hw->y_end_stage + 1)) - 1) -
		((1 << hw->y_start_stage) - 1));

	h_state = ad714x->h_state & mask;
	c_state = ad714x->c_state & mask;

	switch (sw->state) {
	case IDLE:
		if (h_state) {
			sw->state = JITTER;
			/* In End of Conversion interrupt mode, the AD714X
			 * continuously generates hardware interrupts.
			 */
			touchpad_use_com_int(ad714x, idx);
			dev_dbg(ad714x->dev, "touchpad %d touched\n", idx);
		}
		break;

	case JITTER:
		if (c_state == mask) {
			touchpad_cal_sensor_val(ad714x, idx);
			touchpad_cal_highest_stage(ad714x, idx);
			if ((!touchpad_check_second_peak(ad714x, idx)) &&
				(!touchpad_check_endpoint(ad714x, idx))) {
				dev_dbg(ad714x->dev,
					"touchpad%d, 2 fingers or endpoint\n",
					idx);
				touchpad_cal_abs_pos(ad714x, idx);
				sw->x_flt_pos = sw->x_abs_pos;
				sw->y_flt_pos = sw->y_abs_pos;
				sw->state = ACTIVE;
			}
		}
		break;

	case ACTIVE:
		if (c_state == mask) {
			if (h_state) {
				touchpad_cal_sensor_val(ad714x, idx);
				touchpad_cal_highest_stage(ad714x, idx);
				if ((!touchpad_check_second_peak(ad714x, idx))
				  && (!touchpad_check_endpoint(ad714x, idx))) {
					touchpad_cal_abs_pos(ad714x, idx);
					touchpad_cal_flt_pos(ad714x, idx);
					input_report_abs(sw->input, ABS_X,
						sw->x_flt_pos);
					input_report_abs(sw->input, ABS_Y,
						sw->y_flt_pos);
					input_report_key(sw->input, BTN_TOUCH,
						1);
				}
			} else {
				/* When the user lifts off the sensor, configure
				 * the AD714X back to threshold interrupt mode.
				 */
				touchpad_use_thr_int(ad714x, idx);
				sw->state = IDLE;
				input_report_key(sw->input, BTN_TOUCH, 0);
				dev_dbg(ad714x->dev, "touchpad %d released\n",
					idx);
			}
			input_sync(sw->input);
		}
		break;

	default:
		break;
	}
}

static int ad714x_hw_detect(struct ad714x_chip *ad714x)
{
	unsigned short data;

	ad714x->read(ad714x, AD714X_PARTID_REG, &data, 1);
	switch (data & 0xFFF0) {
	case AD7142_PARTID:
		ad714x->product = 0x7142;
		ad714x->version = data & 0xF;
		dev_info(ad714x->dev, "found AD7142 captouch, rev:%d\n",
				ad714x->version);
		return 0;

	case AD7143_PARTID:
		ad714x->product = 0x7143;
		ad714x->version = data & 0xF;
		dev_info(ad714x->dev, "found AD7143 captouch, rev:%d\n",
				ad714x->version);
		return 0;

	case AD7147_PARTID:
		ad714x->product = 0x7147;
		ad714x->version = data & 0xF;
		dev_info(ad714x->dev, "found AD7147(A) captouch, rev:%d\n",
				ad714x->version);
		return 0;

	case AD7148_PARTID:
		ad714x->product = 0x7148;
		ad714x->version = data & 0xF;
		dev_info(ad714x->dev, "found AD7148 captouch, rev:%d\n",
				ad714x->version);
		return 0;

	default:
		dev_err(ad714x->dev,
			"fail to detect AD714X captouch, read ID is %04x\n",
			data);
		return -ENODEV;
	}
}

static void ad714x_hw_init(struct ad714x_chip *ad714x)
{
	int i, j;
	unsigned short reg_base;
	unsigned short data;

	/* configuration CDC and interrupts */

	for (i = 0; i < STAGE_NUM; i++) {
		reg_base = AD714X_STAGECFG_REG + i * STAGE_CFGREG_NUM;
		for (j = 0; j < STAGE_CFGREG_NUM; j++)
			ad714x->write(ad714x, reg_base + j,
					ad714x->hw->stage_cfg_reg[i][j]);
	}

	for (i = 0; i < SYS_CFGREG_NUM; i++)
		ad714x->write(ad714x, AD714X_SYSCFG_REG + i,
			ad714x->hw->sys_cfg_reg[i]);
	for (i = 0; i < SYS_CFGREG_NUM; i++)
		ad714x->read(ad714x, AD714X_SYSCFG_REG + i, &data, 1);

	ad714x->write(ad714x, AD714X_STG_CAL_EN_REG, 0xFFF);

	/* clear all interrupts */
	ad714x->read(ad714x, STG_LOW_INT_STA_REG, &ad714x->l_state, 3);
}

static irqreturn_t ad714x_interrupt_thread(int irq, void *data)
{
	struct ad714x_chip *ad714x = data;
	int i;

	mutex_lock(&ad714x->mutex);

	ad714x->read(ad714x, STG_LOW_INT_STA_REG, &ad714x->l_state, 3);

	for (i = 0; i < ad714x->hw->button_num; i++)
		ad714x_button_state_machine(ad714x, i);
	for (i = 0; i < ad714x->hw->slider_num; i++)
		ad714x_slider_state_machine(ad714x, i);
	for (i = 0; i < ad714x->hw->wheel_num; i++)
		ad714x_wheel_state_machine(ad714x, i);
	for (i = 0; i < ad714x->hw->touchpad_num; i++)
		ad714x_touchpad_state_machine(ad714x, i);

	mutex_unlock(&ad714x->mutex);

	return IRQ_HANDLED;
}

#define MAX_DEVICE_NUM 8
struct ad714x_chip *ad714x_probe(struct device *dev, u16 bus_type, int irq,
				 ad714x_read_t read, ad714x_write_t write)
{
	int i, alloc_idx;
	int error;
	struct input_dev *input[MAX_DEVICE_NUM];

	struct ad714x_platform_data *plat_data = dev->platform_data;
	struct ad714x_chip *ad714x;
	void *drv_mem;
	unsigned long irqflags;

	struct ad714x_button_drv *bt_drv;
	struct ad714x_slider_drv *sd_drv;
	struct ad714x_wheel_drv *wl_drv;
	struct ad714x_touchpad_drv *tp_drv;


	if (irq <= 0) {
		dev_err(dev, "IRQ not configured!\n");
		error = -EINVAL;
		goto err_out;
	}

	if (dev->platform_data == NULL) {
		dev_err(dev, "platform data for ad714x doesn't exist\n");
		error = -EINVAL;
		goto err_out;
	}

	ad714x = kzalloc(sizeof(*ad714x) + sizeof(*ad714x->sw) +
			 sizeof(*sd_drv) * plat_data->slider_num +
			 sizeof(*wl_drv) * plat_data->wheel_num +
			 sizeof(*tp_drv) * plat_data->touchpad_num +
			 sizeof(*bt_drv) * plat_data->button_num, GFP_KERNEL);
	if (!ad714x) {
		error = -ENOMEM;
		goto err_out;
	}

	ad714x->hw = plat_data;

	drv_mem = ad714x + 1;
	ad714x->sw = drv_mem;
	drv_mem += sizeof(*ad714x->sw);
	ad714x->sw->slider = sd_drv = drv_mem;
	drv_mem += sizeof(*sd_drv) * ad714x->hw->slider_num;
	ad714x->sw->wheel = wl_drv = drv_mem;
	drv_mem += sizeof(*wl_drv) * ad714x->hw->wheel_num;
	ad714x->sw->touchpad = tp_drv = drv_mem;
	drv_mem += sizeof(*tp_drv) * ad714x->hw->touchpad_num;
	ad714x->sw->button = bt_drv = drv_mem;
	drv_mem += sizeof(*bt_drv) * ad714x->hw->button_num;

	ad714x->read = read;
	ad714x->write = write;
	ad714x->irq = irq;
	ad714x->dev = dev;

	error = ad714x_hw_detect(ad714x);
	if (error)
		goto err_free_mem;

	/* initialize and request sw/hw resources */

	ad714x_hw_init(ad714x);
	mutex_init(&ad714x->mutex);

	/*
	 * Allocate and register AD714X input device
	 */
	alloc_idx = 0;

	/* a slider uses one input_dev instance */
	if (ad714x->hw->slider_num > 0) {
		struct ad714x_slider_plat *sd_plat = ad714x->hw->slider;

		for (i = 0; i < ad714x->hw->slider_num; i++) {
			sd_drv[i].input = input[alloc_idx] = input_allocate_device();
			if (!input[alloc_idx]) {
				error = -ENOMEM;
				goto err_free_dev;
			}

			__set_bit(EV_ABS, input[alloc_idx]->evbit);
			__set_bit(EV_KEY, input[alloc_idx]->evbit);
			__set_bit(ABS_X, input[alloc_idx]->absbit);
			__set_bit(BTN_TOUCH, input[alloc_idx]->keybit);
			input_set_abs_params(input[alloc_idx],
				ABS_X, 0, sd_plat->max_coord, 0, 0);

			input[alloc_idx]->id.bustype = bus_type;
			input[alloc_idx]->id.product = ad714x->product;
			input[alloc_idx]->id.version = ad714x->version;
			input[alloc_idx]->name = "ad714x_captouch_slider";
			input[alloc_idx]->dev.parent = dev;

			error = input_register_device(input[alloc_idx]);
			if (error)
				goto err_free_dev;

			alloc_idx++;
		}
	}

	/* a wheel uses one input_dev instance */
	if (ad714x->hw->wheel_num > 0) {
		struct ad714x_wheel_plat *wl_plat = ad714x->hw->wheel;

		for (i = 0; i < ad714x->hw->wheel_num; i++) {
			wl_drv[i].input = input[alloc_idx] = input_allocate_device();
			if (!input[alloc_idx]) {
				error = -ENOMEM;
				goto err_free_dev;
			}

			__set_bit(EV_KEY, input[alloc_idx]->evbit);
			__set_bit(EV_ABS, input[alloc_idx]->evbit);
			__set_bit(ABS_WHEEL, input[alloc_idx]->absbit);
			__set_bit(BTN_TOUCH, input[alloc_idx]->keybit);
			input_set_abs_params(input[alloc_idx],
				ABS_WHEEL, 0, wl_plat->max_coord, 0, 0);

			input[alloc_idx]->id.bustype = bus_type;
			input[alloc_idx]->id.product = ad714x->product;
			input[alloc_idx]->id.version = ad714x->version;
			input[alloc_idx]->name = "ad714x_captouch_wheel";
			input[alloc_idx]->dev.parent = dev;

			error = input_register_device(input[alloc_idx]);
			if (error)
				goto err_free_dev;

			alloc_idx++;
		}
	}

	/* a touchpad uses one input_dev instance */
	if (ad714x->hw->touchpad_num > 0) {
		struct ad714x_touchpad_plat *tp_plat = ad714x->hw->touchpad;

		for (i = 0; i < ad714x->hw->touchpad_num; i++) {
			tp_drv[i].input = input[alloc_idx] = input_allocate_device();
			if (!input[alloc_idx]) {
				error = -ENOMEM;
				goto err_free_dev;
			}

			__set_bit(EV_ABS, input[alloc_idx]->evbit);
			__set_bit(EV_KEY, input[alloc_idx]->evbit);
			__set_bit(ABS_X, input[alloc_idx]->absbit);
			__set_bit(ABS_Y, input[alloc_idx]->absbit);
			__set_bit(BTN_TOUCH, input[alloc_idx]->keybit);
			input_set_abs_params(input[alloc_idx],
				ABS_X, 0, tp_plat->x_max_coord, 0, 0);
			input_set_abs_params(input[alloc_idx],
				ABS_Y, 0, tp_plat->y_max_coord, 0, 0);

			input[alloc_idx]->id.bustype = bus_type;
			input[alloc_idx]->id.product = ad714x->product;
			input[alloc_idx]->id.version = ad714x->version;
			input[alloc_idx]->name = "ad714x_captouch_pad";
			input[alloc_idx]->dev.parent = dev;

			error = input_register_device(input[alloc_idx]);
			if (error)
				goto err_free_dev;

			alloc_idx++;
		}
	}

	/* all buttons use one input node */
	if (ad714x->hw->button_num > 0) {
		struct ad714x_button_plat *bt_plat = ad714x->hw->button;

		input[alloc_idx] = input_allocate_device();
		if (!input[alloc_idx]) {
			error = -ENOMEM;
			goto err_free_dev;
		}

		__set_bit(EV_KEY, input[alloc_idx]->evbit);
		for (i = 0; i < ad714x->hw->button_num; i++) {
			bt_drv[i].input = input[alloc_idx];
			__set_bit(bt_plat[i].keycode, input[alloc_idx]->keybit);
		}

		input[alloc_idx]->id.bustype = bus_type;
		input[alloc_idx]->id.product = ad714x->product;
		input[alloc_idx]->id.version = ad714x->version;
		input[alloc_idx]->name = "ad714x_captouch_button";
		input[alloc_idx]->dev.parent = dev;

		error = input_register_device(input[alloc_idx]);
		if (error)
			goto err_free_dev;

		alloc_idx++;
	}

	irqflags = plat_data->irqflags ?: IRQF_TRIGGER_FALLING;
	irqflags |= IRQF_ONESHOT;

	error = request_threaded_irq(ad714x->irq, NULL, ad714x_interrupt_thread,
				     irqflags, "ad714x_captouch", ad714x);
	if (error) {
		dev_err(dev, "can't allocate irq %d\n", ad714x->irq);
		goto err_unreg_dev;
	}

	return ad714x;

 err_free_dev:
	dev_err(dev, "failed to setup AD714x input device %i\n", alloc_idx);
	input_free_device(input[alloc_idx]);
 err_unreg_dev:
	while (--alloc_idx >= 0)
		input_unregister_device(input[alloc_idx]);
 err_free_mem:
	kfree(ad714x);
 err_out:
	return ERR_PTR(error);
}
EXPORT_SYMBOL(ad714x_probe);

void ad714x_remove(struct ad714x_chip *ad714x)
{
	struct ad714x_platform_data *hw = ad714x->hw;
	struct ad714x_driver_data *sw = ad714x->sw;
	int i;

	free_irq(ad714x->irq, ad714x);

	/* unregister and free all input devices */

	for (i = 0; i < hw->slider_num; i++)
		input_unregister_device(sw->slider[i].input);

	for (i = 0; i < hw->wheel_num; i++)
		input_unregister_device(sw->wheel[i].input);

	for (i = 0; i < hw->touchpad_num; i++)
		input_unregister_device(sw->touchpad[i].input);

	if (hw->button_num)
		input_unregister_device(sw->button[0].input);

	kfree(ad714x);
}
EXPORT_SYMBOL(ad714x_remove);

#ifdef CONFIG_PM
int ad714x_disable(struct ad714x_chip *ad714x)
{
	unsigned short data;

	dev_dbg(ad714x->dev, "%s enter\n", __func__);

	mutex_lock(&ad714x->mutex);

	data = ad714x->hw->sys_cfg_reg[AD714X_PWR_CTRL] | 0x3;
	ad714x->write(ad714x, AD714X_PWR_CTRL, data);

	mutex_unlock(&ad714x->mutex);

	return 0;
}
EXPORT_SYMBOL(ad714x_disable);

int ad714x_enable(struct ad714x_chip *ad714x)
{
	dev_dbg(ad714x->dev, "%s enter\n", __func__);

	mutex_lock(&ad714x->mutex);

	/* resume to non-shutdown mode */

	ad714x->write(ad714x, AD714X_PWR_CTRL,
			ad714x->hw->sys_cfg_reg[AD714X_PWR_CTRL]);

	/* make sure the interrupt output line is not low level after resume,
	 * otherwise we will get no chance to enter falling-edge irq again
	 */

	ad714x->read(ad714x, STG_LOW_INT_STA_REG, &ad714x->l_state, 3);

	mutex_unlock(&ad714x->mutex);

	return 0;
}
EXPORT_SYMBOL(ad714x_enable);
#endif

MODULE_DESCRIPTION("Analog Devices AD714X Capacitance Touch Sensor Driver");
MODULE_AUTHOR("Barry Song <21cnbao@gmail.com>");
MODULE_LICENSE("GPL");
