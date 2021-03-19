// SPDX-License-Identifier: GPL-2.0
/*
 * motor  driver
 *
 * Copyright (C) 2020 Rockchip Electronics Co., Ltd.
 *
 */
//#define DEBUG
#include <linux/io.h>
#include <linux/of_gpio.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/device.h>
#include <linux/fb.h>
#include <linux/interrupt.h>
#include <linux/kernel.h>
#include <linux/gpio/consumer.h>
#include <linux/of_irq.h>
#include <linux/platform_device.h>
#include <linux/wakelock.h>
#include <linux/hrtimer.h>
#include <linux/pwm.h>
#include <linux/delay.h>
#include <media/v4l2-subdev.h>
#include <media/v4l2-ctrls.h>
#include <media/v4l2-device.h>
#include <linux/mutex.h>
#include <linux/version.h>
#include <linux/rk-camera-module.h>
#include <linux/completion.h>
#include <linux/rk_vcm_head.h>

#define DRIVER_VERSION	KERNEL_VERSION(0, 0x01, 0x00)

#define DRIVER_NAME "mp6507"

#define MAX_START_UP_HZ			(1200)
#define MOTOR_MAX_HZ			(2500)
#define SPEED_QUEUE_MAX			(71)
#define THRESHOLD_TO_SPEEDED_UP_DEF	(500)
#define STEP_PER_SPEED_DEF		(8)
#define SPEED_QUEUE_NUM_DEF		(71)

#define IRIS_MAX_STEP_DEF		80
#define FOCUS_MAX_STEP_DEF		7500
#define ZOOM_MAX_STEP_DEF		7500

#define IRIS_MAX_LOG			80
#define FOCUS_MAX_LOG			7500
#define ZOOM_MAX_LOG			7500
#define IRIS_LOG_STEP			4
#define FOCUS_LOG_STEP			4
#define ZOOM_LOG_STEP			4

enum {
	MOTOR_STATUS_STOPPED = 0,
	MOTOR_STATUS_CW = 1,
	MOTOR_STATUS_CCW = 2,
};

enum ext_dev_type {
	TYPE_IRIS = 0,
	TYPE_FOCUS = 1,
	TYPE_ZOOM = 2,
};

struct speed_s {
	u32 count;
	u64 phase_interval_ns;
};

struct speed_queue_s {
	int count;
	struct speed_s *speed_p;
};

struct ext_dev {
	u8 type;
	u32 step_max;
	u32 cur_pos;
	u32 step_per_pos;
	u32 start_up_speed;
	u32 max_speed;
	u32 speed_queue_num;
	u32 first_speed_step;
	u32 ths_speeded_up;
	u32 speed_up_step_cnt;
	u32 *speed_up_table;
	u32 *speed_down_table;
	u32 length_up;//speed_up_table length
	u32 length_down;
	struct gpio_desc *en_gpio;
	struct rk_cam_vcm_tim mv_tim;
	struct speed_queue_s speed_que;
	struct speed_queue_s one_speed_que;
};

struct motor_dev {
	struct v4l2_subdev sd;
	struct v4l2_ctrl_handler ctrl_handler;
	struct pwm_device *pwm_a1;
	struct pwm_device *pwm_a2;
	struct pwm_device *pwm_b1;
	struct pwm_device *pwm_b2;
	struct v4l2_ctrl *iris_ctrl;
	struct v4l2_ctrl *focus_ctrl;
	struct v4l2_ctrl *zoom_ctrl;
	struct device *dev;
	struct hrtimer timer;
	struct mutex mutex;
	u32 move_status;
	u32 move_cnt;
	u32 module_index;
	const char *module_facing;
	bool resched;
	struct completion complete;
	struct ext_dev iris;
	struct ext_dev focus;
	struct ext_dev zoom;
	struct ext_dev *cur_ext_dev;
	struct speed_queue_s *run_queue;
	struct pwm_state pwm_state;
};

static int set_motor_running_status(struct motor_dev *motor,
				    struct ext_dev *cur_ext_dev,
				    int status, u32 pos)
{
	int ret = 0;
	u64 mv_us = 0;
	u64 mv_s = 0;
	u64 move_time = 0;
	u32 step_cnt = 0;
	int i = 0;

	if (motor->move_status != MOTOR_STATUS_STOPPED)
		wait_for_completion(&motor->complete);

	motor->cur_ext_dev = cur_ext_dev;
	if (!IS_ERR(cur_ext_dev->en_gpio))
		gpiod_set_value_cansleep(cur_ext_dev->en_gpio, 1);

	motor->move_status = status;
	step_cnt = pos * cur_ext_dev->step_per_pos;
	if (cur_ext_dev->speed_queue_num > 1 &&
	    step_cnt >= cur_ext_dev->ths_speeded_up) {
		motor->run_queue = &cur_ext_dev->speed_que;
		motor->run_queue->speed_p[cur_ext_dev->length_up - 1].count =
			step_cnt - cur_ext_dev->speed_up_step_cnt;
	} else {
		motor->run_queue = &cur_ext_dev->one_speed_que;
		motor->run_queue->speed_p[0].count = step_cnt;
	}
	motor->move_cnt = motor->run_queue->count;
	reinit_completion(&motor->complete);

	cur_ext_dev->mv_tim.vcm_start_t = ns_to_timeval(ktime_get_ns());
	for (i = 0; i < motor->run_queue->count; i++) {
		move_time += (u64)motor->run_queue->speed_p[i].count *
			     (u64)motor->run_queue->speed_p[i].phase_interval_ns;
		dev_dbg(motor->dev, "speed_que.speed[%d], count %d, phase_interval_ns %llu\n",
			i,
			motor->run_queue->speed_p[i].count,
			motor->run_queue->speed_p[i].phase_interval_ns);
	}

	mv_us = div_u64(move_time, 1000);
	dev_dbg(motor->dev, "motor move needs %lld us\n", mv_us);
	mv_us += cur_ext_dev->mv_tim.vcm_start_t.tv_usec;
	if (mv_us >= 1000000) {
		mv_s = div_u64(mv_us, 1000000);
		cur_ext_dev->mv_tim.vcm_end_t.tv_sec =
			cur_ext_dev->mv_tim.vcm_start_t.tv_sec + mv_s;
		cur_ext_dev->mv_tim.vcm_end_t.tv_usec = mv_us - (mv_s * 1000000);
	} else {
		cur_ext_dev->mv_tim.vcm_end_t.tv_sec =
				cur_ext_dev->mv_tim.vcm_start_t.tv_sec;
		cur_ext_dev->mv_tim.vcm_end_t.tv_usec = mv_us;
	}
	hrtimer_start(&motor->timer, ktime_set(0, 0), HRTIMER_MODE_REL);
	return ret;
}

static int fill_speed_squeue(struct device *dev, struct ext_dev *ext_dev)
{
	struct device_node *node = dev->of_node;
	struct property *prop = NULL;
	u32 length_up = 0;
	u32 length_down = 0;
	u32 *speed_up_table = NULL;
	u32 *speed_down_table = NULL;
	int i = 0;
	size_t size;
	u32 step_cnt = 0;
	u32 step_total = 0;
	int ret = 0;

	ext_dev->ths_speeded_up = 0;
	size = sizeof(*ext_dev->one_speed_que.speed_p);
	ext_dev->one_speed_que.speed_p = devm_kzalloc(dev, size, GFP_KERNEL);
	if (!ext_dev->one_speed_que.speed_p)
		return -ENOMEM;
	ext_dev->one_speed_que.count = 1;
	ext_dev->one_speed_que.speed_p[0].count = ext_dev->first_speed_step;
	ext_dev->one_speed_que.speed_p[0].phase_interval_ns =
		div_u64(NSEC_PER_SEC, ext_dev->start_up_speed);
	switch (ext_dev->type) {
	case TYPE_IRIS:
		//max step is 80, needn't speed-up
		return 0;
	case TYPE_FOCUS:
		prop = of_find_property(node, "focus-speed-up-table", &length_up);
		if (prop)
			length_up /= sizeof(u32);
		if (length_up > 0) {
			size = sizeof(*speed_up_table) * length_up;
			speed_up_table = devm_kzalloc(dev, size, GFP_KERNEL);
			if (!speed_up_table)
				return -ENOMEM;
			ret = of_property_read_u32_array(node, "focus-speed-up-table",
							speed_up_table,
							length_up);
			if (ret < 0) {
				dev_info(dev,
					"fail to get speed table, used default speed!\n");
				devm_kfree(dev, speed_up_table);
				speed_up_table = NULL;
				ext_dev->speed_queue_num = 1;
				return 0;
			}
			dev_dbg(dev,
				"dev tpype %d, speed-up table length %d, buf size %u\n",
				ext_dev->type,
				length_up,
				size);
		}
		prop = of_find_property(node, "focus-speed-down-table", &length_down);
		if (prop)
			length_down /= sizeof(u32);
		if (length_down > 0) {
			size = sizeof(*speed_down_table) * length_down;
			speed_down_table = devm_kzalloc(dev, size, GFP_KERNEL);
			if (!speed_down_table)
				return -ENOMEM;
			ret = of_property_read_u32_array(node, "focus-speed-down-table",
							speed_down_table,
							length_down);
			if (ret < 0) {
				dev_info(dev,
					"fail to get speed table, used default speed!\n");
				devm_kfree(dev, speed_down_table);
				speed_down_table = NULL;
			} else {
				dev_dbg(dev,
					"dev tpype %d, speed-down table length %d, buf size %u\n",
					ext_dev->type,
					length_up,
					size);
			}
		}
		break;
	case TYPE_ZOOM:
		prop = of_find_property(node, "zoom-speed-up-table", &length_up);
		if (prop)
			length_up /= sizeof(u32);
		if (length_up > 0) {
			size = sizeof(*speed_up_table) * length_up;
			speed_up_table = devm_kzalloc(dev, size, GFP_KERNEL);
			if (!speed_up_table)
				return -ENOMEM;
			ret = of_property_read_u32_array(node, "zoom-speed-up-table",
							speed_up_table,
							length_up);
			if (ret < 0) {
				dev_info(dev,
					"fail to get speed table, used default speed!\n");
				ext_dev->speed_queue_num = 1;
				devm_kfree(dev, speed_up_table);
				speed_up_table = NULL;
				return 0;
			}
			dev_dbg(dev,
				"dev tpype %d, speed-up table length %d, buf size %u\n",
				ext_dev->type,
				length_up,
				size);
		}
		prop = of_find_property(node, "zoom-speed-down-table", &length_down);
		if (prop)
			length_down /= sizeof(u32);
		if (length_down > 0) {
			size = sizeof(*speed_down_table) * length_down;
			speed_down_table = devm_kzalloc(dev, size, GFP_KERNEL);
			if (!speed_down_table)
				return -ENOMEM;
			ret = of_property_read_u32_array(node, "zoom-speed-down-table",
							speed_down_table,
							length_down);
			if (ret < 0) {
				dev_info(dev,
					"fail to get speed table, used default speed!\n");
				devm_kfree(dev, speed_down_table);
				speed_down_table = NULL;
			} else {
				dev_dbg(dev,
					"dev tpype %d, speed-down table length %d, buf size %u\n",
					ext_dev->type,
					length_up,
					size);
			}
		}
		break;
	default:
		return -EINVAL;
	}
	if (speed_up_table == NULL || speed_up_table[0] > ext_dev->start_up_speed ||
	    speed_up_table[length_up - 1] > ext_dev->max_speed) {
		dev_info(dev,
			"speed_up_table data error, not to used it!\n");
		ext_dev->speed_queue_num = 1;
	} else {
		ext_dev->length_up = length_up;
		if (speed_down_table != NULL)
			ext_dev->speed_queue_num = length_up + length_down;
		else
			ext_dev->speed_queue_num = length_up * 2 - 1;
		size = sizeof(*ext_dev->speed_que.speed_p) * ext_dev->speed_queue_num;
		ext_dev->speed_que.speed_p = devm_kzalloc(dev, size, GFP_KERNEL);
		if (!ext_dev->speed_que.speed_p)
			return -ENOMEM;
		for (i = 0; i < length_up - 1; i++) {
			if (i == 0)
				step_cnt = ext_dev->first_speed_step;
			else
				step_cnt =
					ext_dev->first_speed_step *
					speed_up_table[i] / speed_up_table[0];
			step_cnt = (step_cnt + 3) / 4 * 4;
			ext_dev->speed_que.speed_p[i].count = step_cnt;
			ext_dev->speed_que.speed_p[i].phase_interval_ns =
				div_u64(NSEC_PER_SEC, speed_up_table[i]);
			step_total += step_cnt;
			if (speed_down_table == NULL ||
			    speed_down_table[0] > speed_up_table[length_up - 1]) {
				dev_info(dev,
					"speed_down_table data error, used speed_up_table\n");
				ext_dev->speed_que.speed_p[ext_dev->speed_queue_num - i - 1].count =
					step_cnt;
				ext_dev->speed_que.speed_p[ext_dev->speed_queue_num - i - 1].phase_interval_ns =
					div_u64(NSEC_PER_SEC, speed_up_table[i]);
				step_total += step_cnt;
			}
			dev_info(dev,
				"index %d, speed %d, count %d\n",
				i, speed_up_table[i], ext_dev->speed_que.speed_p[i].count);
		}
		ext_dev->speed_up_table = speed_up_table;

		if (speed_down_table != NULL &&
		    speed_down_table[0] <= speed_up_table[length_up - 1]) {
			for (i = 0; i < length_down; i++) {
				step_cnt =
					ext_dev->first_speed_step *
					speed_down_table[i] / speed_up_table[0];
				step_cnt = (step_cnt + 3) / 4 * 4;
				ext_dev->speed_que.speed_p[length_up + i].count =
					step_cnt;
				ext_dev->speed_que.speed_p[length_up + i].phase_interval_ns =
					div_u64(NSEC_PER_SEC, speed_down_table[i]);
				step_total += step_cnt;
			}
			ext_dev->speed_down_table = speed_down_table;
			ext_dev->length_down = length_down;
		}
		ext_dev->speed_up_step_cnt = step_total;

		step_cnt =
			ext_dev->first_speed_step *
			speed_up_table[length_up - 1] / speed_up_table[0];
		step_cnt = (step_cnt + 3) / 4 * 4;
		ext_dev->speed_que.speed_p[length_up - 1].count = step_cnt;
		ext_dev->speed_que.speed_p[length_up - 1].phase_interval_ns =
			div_u64(NSEC_PER_SEC, speed_up_table[length_up - 1]);
		step_total += step_cnt;

		ext_dev->ths_speeded_up = step_total;
		ext_dev->speed_que.count = ext_dev->speed_queue_num;
	}
	return 0;
}

static int motor_dev_parse_dt(struct motor_dev *motor)
{
	struct device_node *node = motor->dev->of_node;
	int ret = 0;
	int error = 0;

	motor->pwm_a1 = devm_pwm_get(motor->dev, "ain1");
	motor->pwm_a2 = devm_pwm_get(motor->dev, "ain2");
	motor->pwm_b1 = devm_pwm_get(motor->dev, "bin1");
	motor->pwm_b2 = devm_pwm_get(motor->dev, "bin2");

	if (IS_ERR(motor->pwm_a1)) {
		error = PTR_ERR(motor->pwm_a1);
		if (error != -EPROBE_DEFER)
			dev_err(motor->dev, "Failed to request PWM a1 device: %d\n", error);
		return error;
	}
	if (IS_ERR(motor->pwm_a2)) {
		error = PTR_ERR(motor->pwm_a2);
		if (error != -EPROBE_DEFER)
			dev_err(motor->dev, "Failed to request PWM a2 device: %d\n", error);
		return error;
	}
	if (IS_ERR(motor->pwm_b1)) {
		error = PTR_ERR(motor->pwm_b1);
		if (error != -EPROBE_DEFER)
			dev_err(motor->dev, "Failed to request PWM b1 device: %d\n", error);
		return error;
	}
	if (IS_ERR(motor->pwm_b2)) {
		error = PTR_ERR(motor->pwm_b2);
		if (error != -EPROBE_DEFER)
			dev_err(motor->dev, "Failed to request PWM b2 device: %d\n", error);
		return error;
	}

	/* get iris_en gpio */
	motor->iris.en_gpio = devm_gpiod_get(motor->dev,
					     "iris_en", GPIOD_OUT_LOW);
	if (IS_ERR(motor->iris.en_gpio))
		dev_err(motor->dev, "Failed to get iris_en-gpios\n");

	/* get focus_en gpio */
	motor->focus.en_gpio = devm_gpiod_get(motor->dev,
					      "focus_en", GPIOD_OUT_LOW);
	if (IS_ERR(motor->focus.en_gpio))
		dev_err(motor->dev, "Failed to get focus_en-gpios\n");

	/* get zoom_en gpio */
	motor->zoom.en_gpio = devm_gpiod_get(motor->dev,
					     "zoom_en", GPIOD_OUT_LOW);
	if (IS_ERR(motor->zoom.en_gpio))
		dev_err(motor->dev, "Failed to get zoom_en-gpios\n");

	ret = of_property_read_u32(node,
				   "iris-step-max",
				   &motor->iris.step_max);
	if (ret != 0) {
		motor->iris.step_max = IRIS_MAX_STEP_DEF;
		dev_err(motor->dev,
			"failed get iris iris_pos_max,use dafult value 80\n");
	}

	ret = of_property_read_u32(node,
				   "focus-step-max",
				   &motor->focus.step_max);
	if (ret != 0) {
		motor->focus.step_max = FOCUS_MAX_STEP_DEF;
		dev_err(motor->dev,
			"failed get iris focus_pos_max,use dafult value 7500\n");
	}

	ret = of_property_read_u32(node,
				   "zoom-step-max",
				   &motor->zoom.step_max);
	if (ret != 0) {
		motor->zoom.step_max = ZOOM_MAX_STEP_DEF;
		dev_err(motor->dev,
			"failed get iris zoom_pos_max,use dafult value 7500\n");
	}

	ret = of_property_read_u32(node,
				   "iris-start-up-speed",
				   &motor->iris.start_up_speed);
	if (ret != 0) {
		motor->iris.start_up_speed = MAX_START_UP_HZ;
		dev_err(motor->dev,
			"failed get motor start up speed,use dafult value\n");
	}
	ret = of_property_read_u32(node,
				   "iris-max-speed",
				   &motor->iris.max_speed);
	if (ret != 0) {
		motor->iris.max_speed = MOTOR_MAX_HZ;
		dev_err(motor->dev,
			"failed get motor max speed,use dafult value\n");
	}

	ret = of_property_read_u32(node,
				   "focus-start-up-speed",
				   &motor->focus.start_up_speed);
	if (ret != 0) {
		motor->focus.start_up_speed = MAX_START_UP_HZ;
		dev_err(motor->dev,
			"failed get motor start up speed,use dafult value\n");
	}
	ret = of_property_read_u32(node,
				   "focus-max-speed",
				   &motor->focus.max_speed);
	if (ret != 0) {
		motor->focus.max_speed = MOTOR_MAX_HZ;
		dev_err(motor->dev,
			"failed get motor max speed,use dafult value\n");
	}

	ret = of_property_read_u32(node,
				   "zoom-start-up-speed",
				   &motor->zoom.start_up_speed);
	if (ret != 0) {
		motor->zoom.start_up_speed = MAX_START_UP_HZ;
		dev_err(motor->dev,
			"failed get motor start up speed,use dafult value\n");
	}
	ret = of_property_read_u32(node,
				   "zoom-max-speed",
				   &motor->zoom.max_speed);
	if (ret != 0) {
		motor->zoom.max_speed = MOTOR_MAX_HZ;
		dev_err(motor->dev,
			"failed get motor max speed,use dafult value\n");
	}

	ret = of_property_read_u32(node,
				   "focus-first-speed-step",
				   &motor->focus.first_speed_step);
	if (ret != 0) {
		motor->focus.first_speed_step = STEP_PER_SPEED_DEF;
		dev_err(motor->dev,
			"failed get motor step of first speed,use dafult value\n");
	}
	ret = of_property_read_u32(node,
				   "zoom-first-speed-step",
				   &motor->zoom.first_speed_step);
	if (ret != 0) {
		motor->zoom.first_speed_step = STEP_PER_SPEED_DEF;
		dev_err(motor->dev,
			"failed get motor step of first speed,use dafult value\n");
	}

	motor->iris.type = TYPE_IRIS;
	ret = fill_speed_squeue(motor->dev, &motor->iris);
	motor->focus.type = TYPE_FOCUS;
	ret |= fill_speed_squeue(motor->dev, &motor->focus);
	motor->zoom.type = TYPE_ZOOM;
	ret |= fill_speed_squeue(motor->dev, &motor->zoom);

	ret = of_property_read_u32(node, RKMODULE_CAMERA_MODULE_INDEX,
				   &motor->module_index);
	ret |= of_property_read_string(node, RKMODULE_CAMERA_MODULE_FACING,
				       &motor->module_facing);
	if (ret) {
		dev_err(motor->dev,
			"could not get module information!\n");
		return -EINVAL;
	}
	return 0;
}

static enum hrtimer_restart motor_timer_func(struct hrtimer *timer)
{
	struct motor_dev *motor;
	struct pwm_state *pwm_state;
	int idx = 0;
	u64 time_cnt = 0;

	motor = container_of(timer, struct motor_dev, timer);
	pwm_state = &motor->pwm_state;
	if (motor->move_cnt < 1 || motor->move_status == MOTOR_STATUS_STOPPED) {
		pwm_state->enabled = false;
		pwm_apply_state(motor->pwm_b1, pwm_state);
		pwm_apply_state(motor->pwm_b2, pwm_state);
		pwm_apply_state(motor->pwm_a1, pwm_state);
		pwm_apply_state(motor->pwm_a2, pwm_state);
		if (!IS_ERR(motor->cur_ext_dev->en_gpio))
			gpiod_set_value(motor->cur_ext_dev->en_gpio, 0);
		motor->move_status = MOTOR_STATUS_STOPPED;
		motor->resched = false;
		complete(&motor->complete);
		dev_dbg(motor->dev, "motor stop\n");

	} else {
		/* do phase change */
		switch (motor->move_status) {
		case MOTOR_STATUS_CW:
			if (motor->resched == true) {
				pwm_state->enabled = false;
				pwm_apply_state(motor->pwm_b1, pwm_state);
				pwm_apply_state(motor->pwm_b2, pwm_state);
				pwm_apply_state(motor->pwm_a1, pwm_state);
				pwm_apply_state(motor->pwm_a2, pwm_state);
			}
			idx = motor->run_queue->count - motor->move_cnt;
			pwm_state->polarity = PWM_POLARITY_INVERSED;
			pwm_state->enabled = true;
			pwm_state->period =
				motor->run_queue->speed_p[idx].phase_interval_ns * 4;
			pwm_state->duty_cycle =
				motor->run_queue->speed_p[idx].phase_interval_ns * 2;
			pwm_apply_state(motor->pwm_b1, pwm_state);
			pwm_state->enabled = true;
			pwm_state->polarity = PWM_POLARITY_NORMAL;
			pwm_apply_state(motor->pwm_b2, pwm_state);
			pwm_state->polarity = PWM_POLARITY_NORMAL;
			pwm_state->enabled = true;
			pwm_apply_state(motor->pwm_a1, pwm_state);
			pwm_state->polarity = PWM_POLARITY_INVERSED;
			pwm_state->enabled = true;
			pwm_apply_state(motor->pwm_a2, pwm_state);
			break;
		case MOTOR_STATUS_CCW:
			if (motor->resched == true) {
				pwm_state->enabled = false;
				pwm_apply_state(motor->pwm_b1, pwm_state);
				pwm_apply_state(motor->pwm_b2, pwm_state);
				pwm_apply_state(motor->pwm_a1, pwm_state);
				pwm_apply_state(motor->pwm_a2, pwm_state);
			}
			idx = motor->run_queue->count - motor->move_cnt;
			pwm_state->polarity = PWM_POLARITY_INVERSED;
			pwm_state->enabled = true;
			pwm_state->period =
				motor->run_queue->speed_p[idx].phase_interval_ns * 4;
			pwm_state->duty_cycle =
				motor->run_queue->speed_p[idx].phase_interval_ns * 2;
			pwm_apply_state(motor->pwm_b1, pwm_state);
			pwm_state->polarity = PWM_POLARITY_NORMAL;
			pwm_state->enabled = true;
			pwm_apply_state(motor->pwm_b2, pwm_state);
			pwm_state->polarity = PWM_POLARITY_INVERSED;
			pwm_state->enabled = true;
			pwm_apply_state(motor->pwm_a1, pwm_state);
			pwm_state->polarity = PWM_POLARITY_NORMAL;
			pwm_state->enabled = true;
			pwm_apply_state(motor->pwm_a2, pwm_state);
			break;
		default:
			break;
		}
		if (motor->resched == false)
			motor->resched = true;
		motor->move_cnt--;
	}
	if (motor->resched) {
		time_cnt = ((u64)motor->run_queue->speed_p[idx].phase_interval_ns *
			motor->run_queue->speed_p[idx].count);
		hrtimer_forward_now(timer,
			ns_to_ktime(time_cnt - 80000));
		return HRTIMER_RESTART;
	}
	return HRTIMER_NORESTART;
}

static int motor_s_ctrl(struct v4l2_ctrl *ctrl)
{
	int ret = 0;
	struct motor_dev *motor = container_of(ctrl->handler,
					     struct motor_dev, ctrl_handler);

	switch (ctrl->id) {
	case V4L2_CID_IRIS_ABSOLUTE:
		if (ctrl->val > motor->iris.cur_pos)
			ret = set_motor_running_status(motor,
				&motor->iris,
				MOTOR_STATUS_CCW,
				abs(ctrl->val - motor->iris.cur_pos));
		else
			ret = set_motor_running_status(motor,
				&motor->iris,
				MOTOR_STATUS_CW,
				abs(ctrl->val - motor->iris.cur_pos));
		motor->iris.cur_pos = ctrl->val;
		dev_dbg(motor->dev, "set iris pos %d\n", ctrl->val);
		break;
	case V4L2_CID_FOCUS_ABSOLUTE:
		if (ctrl->val > motor->focus.cur_pos)
			ret = set_motor_running_status(motor,
				&motor->focus,
				MOTOR_STATUS_CCW,
				abs(ctrl->val - motor->focus.cur_pos));
		else
			ret = set_motor_running_status(motor,
				&motor->focus,
				MOTOR_STATUS_CW,
				abs(ctrl->val - motor->focus.cur_pos));
		motor->focus.cur_pos = ctrl->val;
		dev_dbg(motor->dev, "set focus pos %d\n", ctrl->val);
		break;
	case V4L2_CID_ZOOM_ABSOLUTE:
		if (ctrl->val > motor->zoom.cur_pos)
			ret = set_motor_running_status(motor,
				&motor->zoom,
				MOTOR_STATUS_CCW,
				abs(ctrl->val - motor->zoom.cur_pos));
		else
			ret = set_motor_running_status(motor,
				&motor->zoom,
				MOTOR_STATUS_CW,
				abs(ctrl->val - motor->zoom.cur_pos));
		motor->zoom.cur_pos = ctrl->val;
		dev_dbg(motor->dev, "set zoom pos %d\n", ctrl->val);
		break;
	default:
		dev_err(motor->dev, "not support cmd %d\n", ctrl->id);
		break;
	}
	return ret;
}

static int motor_init_iris_status(struct motor_dev *motor)
{
	int ret = 0;

	ret = set_motor_running_status(motor, &motor->iris,
				       MOTOR_STATUS_CCW, IRIS_MAX_LOG);
	motor->iris.cur_pos = IRIS_MAX_LOG;
	__v4l2_ctrl_modify_range(motor->iris_ctrl,
				 0,
				 IRIS_MAX_LOG,
				 IRIS_LOG_STEP,
				 motor->iris.cur_pos);
	return ret;
}

static int motor_init_focus_status(struct motor_dev *motor)
{
	int ret = 0;

	ret = set_motor_running_status(motor, &motor->focus,
				       MOTOR_STATUS_CW, FOCUS_MAX_LOG);
	motor->focus.cur_pos = 0;
	__v4l2_ctrl_modify_range(motor->focus_ctrl,
				 0,
				 FOCUS_MAX_LOG,
				 FOCUS_LOG_STEP,
				 motor->focus.cur_pos);
	return ret;
}

static int motor_init_zoom_status(struct motor_dev *motor)
{
	int ret = 0;

	ret = set_motor_running_status(motor, &motor->zoom,
				       MOTOR_STATUS_CW, ZOOM_MAX_LOG);
	motor->zoom.cur_pos = 0;
	__v4l2_ctrl_modify_range(motor->zoom_ctrl,
				 0,
				 ZOOM_MAX_LOG,
				 ZOOM_LOG_STEP,
				 motor->zoom.cur_pos);
	return ret;
}

static long motor_ioctl(struct v4l2_subdev *sd, unsigned int cmd, void *arg)
{
	struct rk_cam_vcm_tim *mv_tim;
	struct motor_dev *motor = container_of(sd, struct motor_dev, sd);

	switch (cmd) {
	case RK_VIDIOC_VCM_TIMEINFO:
		mv_tim = (struct rk_cam_vcm_tim *)arg;
		memcpy(mv_tim, &motor->focus.mv_tim, sizeof(*mv_tim));

		dev_dbg(motor->dev, "get_focus_move_tim 0x%lx, 0x%lx, 0x%lx, 0x%lx\n",
			mv_tim->vcm_start_t.tv_sec,
			mv_tim->vcm_start_t.tv_usec,
			mv_tim->vcm_end_t.tv_sec,
			mv_tim->vcm_end_t.tv_usec);
		break;
	case RK_VIDIOC_IRIS_TIMEINFO:
		mv_tim = (struct rk_cam_vcm_tim *)arg;
		memcpy(mv_tim, &motor->iris.mv_tim, sizeof(*mv_tim));

		dev_dbg(motor->dev, "get_iris_move_tim 0x%lx, 0x%lx, 0x%lx, 0x%lx\n",
			mv_tim->vcm_start_t.tv_sec,
			mv_tim->vcm_start_t.tv_usec,
			mv_tim->vcm_end_t.tv_sec,
			mv_tim->vcm_end_t.tv_usec);
		break;
	case RK_VIDIOC_ZOOM_TIMEINFO:
		mv_tim = (struct rk_cam_vcm_tim *)arg;
		memcpy(mv_tim, &motor->zoom.mv_tim, sizeof(*mv_tim));

		dev_dbg(motor->dev, "get_zoom_move_tim 0x%lx, 0x%lx, 0x%lx, 0x%lx\n",
			mv_tim->vcm_start_t.tv_sec,
			mv_tim->vcm_start_t.tv_usec,
			mv_tim->vcm_end_t.tv_sec,
			mv_tim->vcm_end_t.tv_usec);
		break;
	case RK_VIDIOC_IRIS_CORRECTION:
		motor_init_iris_status(motor);
		break;
	case RK_VIDIOC_FOCUS_CORRECTION:
		motor_init_focus_status(motor);
		break;
	case RK_VIDIOC_ZOOM_CORRECTION:
		motor_init_zoom_status(motor);
		break;
	default:
		break;
	}
	return 0;
}

static const struct v4l2_subdev_core_ops motor_core_ops = {
	.ioctl = motor_ioctl,
};

static const struct v4l2_subdev_ops motor_subdev_ops = {
	.core	= &motor_core_ops,
};

static const struct v4l2_ctrl_ops motor_ctrl_ops = {
	.s_ctrl = motor_s_ctrl,
};

static int motor_initialize_controls(struct motor_dev *motor)
{
	struct v4l2_ctrl_handler *handler;
	int ret = 0;

	handler = &motor->ctrl_handler;
	ret = v4l2_ctrl_handler_init(handler, 3);
	if (ret)
		return ret;
	handler->lock = &motor->mutex;
	if (!IS_ERR(motor->iris.en_gpio)) {
		motor->iris_ctrl = v4l2_ctrl_new_std(handler, &motor_ctrl_ops,
			V4L2_CID_IRIS_ABSOLUTE, 0, IRIS_MAX_LOG, IRIS_LOG_STEP, 0);

		ret = motor_init_iris_status(motor);
	}
	if (!IS_ERR(motor->focus.en_gpio)) {
		motor->focus_ctrl = v4l2_ctrl_new_std(handler, &motor_ctrl_ops,
			V4L2_CID_FOCUS_ABSOLUTE, 0, FOCUS_MAX_LOG,
			FOCUS_LOG_STEP, 0);
	}
	if (!IS_ERR(motor->zoom.en_gpio)) {
		motor->zoom_ctrl = v4l2_ctrl_new_std(handler, &motor_ctrl_ops,
			V4L2_CID_ZOOM_ABSOLUTE, 0, ZOOM_MAX_LOG,
			ZOOM_LOG_STEP, 0);
	}
	if (handler->error) {
		ret = handler->error;
		dev_err(motor->dev,
			"Failed to init controls(%d)\n", ret);
		goto err_free_handler;
	}

	motor->sd.ctrl_handler = handler;
	return ret;

err_free_handler:
	v4l2_ctrl_handler_free(handler);

	return ret;
}
#define USED_SYS_DEBUG
#ifdef USED_SYS_DEBUG

static ssize_t set_iris_correction(struct device *dev,
	struct device_attribute *attr,
	const char *buf,
	size_t count)
{
	struct motor_dev *motor = dev_get_drvdata(dev);
	int status = 0;
	int ret = 0;

	ret = kstrtoint(buf, 0, &status);
	if (!ret && status >= 0 && status < 2) {
		if (status)
			motor_init_iris_status(motor);
		dev_info(dev, "camera iris position correction\n");
	}
	return count;
}

static ssize_t set_focus_correction(struct device *dev,
	struct device_attribute *attr,
	const char *buf,
	size_t count)
{
	struct motor_dev *motor = dev_get_drvdata(dev);
	int status = 0;
	int ret = 0;

	ret = kstrtoint(buf, 0, &status);
	if (!ret && status >= 0 && status < 2) {
		if (status)
			motor_init_focus_status(motor);
		dev_info(dev, "camera focus position correction\n");
	}
	return count;
}

static ssize_t set_zoom_correction(struct device *dev,
	struct device_attribute *attr,
	const char *buf,
	size_t count)
{
	struct motor_dev *motor = dev_get_drvdata(dev);
	int status = 0;
	int ret = 0;

	ret = kstrtoint(buf, 0, &status);
	if (!ret && status >= 0 && status < 2) {
		if (status)
			motor_init_zoom_status(motor);
		dev_info(dev, "camera zoom position correction\n");
	}
	return count;
}

static struct device_attribute attributes[] = {
	__ATTR(is_iris_correction, S_IWUSR, NULL, set_iris_correction),
	__ATTR(is_focus_correction, S_IWUSR, NULL, set_focus_correction),
	__ATTR(is_zoom_correction, S_IWUSR, NULL, set_zoom_correction),
};

static int add_sysfs_interfaces(struct device *dev)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(attributes); i++)
		if (device_create_file(dev, attributes + i))
			goto undo;
	return 0;
undo:
	for (i--; i >= 0 ; i--)
		device_remove_file(dev, attributes + i);
	dev_err(dev, "%s: failed to create sysfs interface\n", __func__);
	return -ENODEV;
}
#endif

static void dev_param_init(struct motor_dev *motor)
{

	motor->iris.step_per_pos = motor->iris.step_max / IRIS_MAX_LOG;
	motor->iris.mv_tim.vcm_start_t = ns_to_timeval(ktime_get_ns());
	motor->iris.mv_tim.vcm_end_t = ns_to_timeval(ktime_get_ns());

	motor->focus.step_per_pos = motor->focus.step_max / FOCUS_MAX_LOG;
	motor->focus.mv_tim.vcm_start_t = ns_to_timeval(ktime_get_ns());
	motor->focus.mv_tim.vcm_end_t = ns_to_timeval(ktime_get_ns());

	motor->zoom.step_per_pos = motor->zoom.step_max / ZOOM_MAX_LOG;
	motor->zoom.mv_tim.vcm_start_t = ns_to_timeval(ktime_get_ns());
	motor->zoom.mv_tim.vcm_end_t = ns_to_timeval(ktime_get_ns());

	motor->move_status = MOTOR_STATUS_STOPPED;
	motor->resched = false;
}

static int motor_dev_probe(struct platform_device *pdev)
{
	int ret = 0;
	struct motor_dev *motor;
	struct v4l2_subdev *sd;
	char facing[2];

	dev_info(&pdev->dev, "driver version: %02x.%02x.%02x",
		DRIVER_VERSION >> 16,
		(DRIVER_VERSION & 0xff00) >> 8,
		DRIVER_VERSION & 0x00ff);
	motor = devm_kzalloc(&pdev->dev, sizeof(*motor), GFP_KERNEL);
	if (!motor)
		return -ENOMEM;
	motor->dev = &pdev->dev;
	dev_set_name(motor->dev, "motor");
	dev_set_drvdata(motor->dev, motor);
	if (motor_dev_parse_dt(motor)) {
		dev_err(motor->dev, "parse dt error\n");
		return -EINVAL;
	}
	dev_param_init(motor);
	mutex_init(&motor->mutex);
	hrtimer_init(&motor->timer, CLOCK_MONOTONIC, HRTIMER_MODE_REL);
	motor->timer.function = motor_timer_func;
	init_completion(&motor->complete);
	v4l2_subdev_init(&motor->sd, &motor_subdev_ops);
	motor->sd.owner = pdev->dev.driver->owner;
	motor->sd.flags |= V4L2_SUBDEV_FL_HAS_DEVNODE;
	motor->sd.dev = &pdev->dev;
	v4l2_set_subdevdata(&motor->sd, pdev);
	platform_set_drvdata(pdev, &motor->sd);
	motor_initialize_controls(motor);
	if (ret)
		goto err_free;
	ret = media_entity_pads_init(&motor->sd.entity, 0, NULL);
	if (ret < 0)
		goto err_free;
	sd = &motor->sd;
	sd->entity.function = MEDIA_ENT_F_LENS;
	sd->entity.flags = 0;

	memset(facing, 0, sizeof(facing));
	if (strcmp(motor->module_facing, "back") == 0)
		facing[0] = 'b';
	else
		facing[0] = 'f';
	snprintf(sd->name, sizeof(sd->name), "m%02d_%s_%s",
		 motor->module_index, facing,
		 DRIVER_NAME);
	ret = v4l2_async_register_subdev(sd);
	if (ret)
		dev_err(&pdev->dev, "v4l2 async register subdev failed\n");
#ifdef USED_SYS_DEBUG
	add_sysfs_interfaces(&pdev->dev);
#endif
	dev_info(motor->dev, "gpio motor driver probe success\n");
	return 0;
err_free:
	v4l2_ctrl_handler_free(&motor->ctrl_handler);
	v4l2_device_unregister_subdev(&motor->sd);
	media_entity_cleanup(&motor->sd.entity);
	return ret;
}

static int motor_dev_remove(struct platform_device *pdev)
{
	struct v4l2_subdev *sd = platform_get_drvdata(pdev);
	struct motor_dev *motor;

	if (sd)
		motor = v4l2_get_subdevdata(sd);
	else
		return -ENODEV;
	hrtimer_cancel(&motor->timer);
	if (sd)
		v4l2_device_unregister_subdev(sd);
	v4l2_ctrl_handler_free(&motor->ctrl_handler);
	media_entity_cleanup(&motor->sd.entity);
	return 0;
}

#if defined(CONFIG_OF)
static const struct of_device_id motor_dev_of_match[] = {
	{ .compatible = "monolithicpower,mp6507", },
	{},
};
#endif

static struct platform_driver motor_dev_driver = {
	.driver = {
		.name = DRIVER_NAME,
		.owner = THIS_MODULE,
		.of_match_table = of_match_ptr(motor_dev_of_match),
	},
	.probe = motor_dev_probe,
	.remove = motor_dev_remove,
};

module_platform_driver(motor_dev_driver);

MODULE_LICENSE("GPL");
MODULE_ALIAS("platform:motor");
MODULE_AUTHOR("ROCKCHIP");
