/*
 * arch/arm/mach-tegra/board-stingray-touch.c
 *
 * Copyright (C) 2010 Motorola, Inc.
 * Copyright (C) 2010 Google, Inc.
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 */

#include <linux/resource.h>
#include <linux/platform_device.h>
#include <linux/delay.h>
#include <linux/gpio.h>
#include <linux/i2c.h>
#include <asm/mach-types.h>
#include <mach/irqs.h>
#include <mach/iomap.h>
#include <mach/pinmux.h>
#include <linux/qtouch_obp_ts.h>
#include <linux/interrupt.h>
#include <linux/input.h>

#include "board-stingray.h"
#include "gpio-names.h"

#define XMEGAT_BL_I2C_ADDR	0x35
#define STINGRAY_TOUCH_RESET_N_GPIO	TEGRA_GPIO_PR4
#define STINGRAY_TOUCH_INT_N_GPIO	TEGRA_GPIO_PV2
#define STINGRAY_TOUCH_WAKE_N_GPIO	TEGRA_GPIO_PJ2

static int stingray_touch_reset(void)
{
	gpio_set_value(STINGRAY_TOUCH_RESET_N_GPIO, 0);
	msleep(10);
	gpio_set_value(STINGRAY_TOUCH_RESET_N_GPIO, 1);
	msleep(100); /* value from moto */
	return 0;
}

struct qtouch_ts_platform_data stingray_touch_data = {

	.flags		= (QTOUCH_FLIP_X |
			   QTOUCH_FLIP_Y |
			   QTOUCH_USE_MULTITOUCH |
			   QTOUCH_CFG_BACKUPNV),
	.irqflags		= (IRQF_TRIGGER_LOW),
	.abs_min_x		= 0,
	.abs_max_x		= 320, //1023,
	.abs_min_y		= 0,
	.abs_max_y		= 736, //1023,
	.abs_min_p		= 0,
	.abs_max_p		= 255,
	.abs_min_w		= 0,
	.abs_max_w		= 15,
	.x_delta		= 400,
	.y_delta		= 250,
	.nv_checksum		= 0x2c30,
	.fuzz_x			= 0,
	.fuzz_y			= 0,
	.fuzz_p			= 2,
	.fuzz_w			= 2,
	.boot_i2c_addr	= XMEGAT_BL_I2C_ADDR,
	.hw_reset		= stingray_touch_reset,
	.key_array = {
		.cfg		= NULL,
		.keys		= NULL,
		.num_keys	= 0,
	},
	.power_cfg	= {
		.idle_acq_int	= 0xff,
		.active_acq_int	= 0xff,
		.active_idle_to	= 0x00,
	},
	.acquire_cfg	= {
		.charge_time	= 0x0A,
		.atouch_drift	= 0x14,
		.touch_drift	= 0x14,
		.drift_susp	= 0x14,
		.touch_autocal	= 0x00,
		.sync		= 0,
		.atch_cal_suspend_time	= 0,
		.atch_cal_suspend_thres	= 0,
	},
	.multi_touch_cfg	= {
		.ctrl		= 0x83,
		.x_origin	= 0,
		.y_origin	= 0,
		.x_size		= 0x1b,
		.y_size		= 0x2a,
		.aks_cfg	= 0,
		.burst_len	= 0x30,
		.tch_det_thr	= 0x2D,
		.tch_det_int	= 0x3,
		.orient		= 1,
		.mrg_to		= 0x00,
		.mov_hyst_init	= 0x05,
		.mov_hyst_next	= 0x02,
		.mov_filter	= 0x20,
		.num_touch	= 0x01,
		.merge_hyst	= 0x0A,
		.merge_thresh	= 0x0A,
		.amp_hyst       = 0x0A,
		.x_res		= 0x031F,
		.y_res		= 0x04FF,
		.x_low_clip	= 0x00,
		.x_high_clip	= 0x00,
		.y_low_clip	= 0x00,
		.y_high_clip	= 0x00,
		.x_edge_ctrl	= 0,
		.x_edge_dist	= 0,
		.y_edge_ctrl	= 0,
		.y_edge_dist	= 0,
		.jump_limit	= 0,
	},
	.linear_tbl_cfg = {
		.ctrl		= 0x00,
		.x_offset	= 0x0000,
		.x_segment = {
			0x00, 0x00, 0x00, 0x00,
			0x00, 0x00, 0x00, 0x00,
			0x00, 0x00, 0x00, 0x00,
			0x00, 0x00, 0x00, 0x00
		},
		.y_offset = 0x0000,
		.y_segment = {
			0x00, 0x00, 0x00, 0x00,
			0x00, 0x00, 0x00, 0x00,
			0x00, 0x00, 0x00, 0x00,
			0x00, 0x00, 0x00, 0x00
		},
	},
	.comms_config_cfg = {
		.ctrl		= 0,
		.command	= 0,
	},
	.gpio_pwm_cfg = {
		.ctrl			= 0,
		.report_mask		= 0,
		.pin_direction		= 0,
		.internal_pullup	= 0,
		.output_value		= 0,
		.wake_on_change		= 0,
		.pwm_enable		= 0,
		.pwm_period		= 0,
		.duty_cycle_0		= 0,
		.duty_cycle_1		= 0,
		.duty_cycle_2		= 0,
		.duty_cycle_3		= 0,
		.trigger_0		= 0,
		.trigger_1		= 0,
		.trigger_2		= 0,
		.trigger_3		= 0,
	},
	.grip_suppression_cfg = {
		.ctrl		= 0x00,
		.xlogrip	= 0x2F,
		.xhigrip	= 0x2F,
		.ylogrip	= 0x2F,
		.yhigrip	= 0x2F,
		.maxtchs	= 0x00,
		.reserve0	= 0x00,
		.szthr1		= 0x00,
		.szthr2		= 0x00,
		.shpthr1	= 0x00,
		.shpthr2	= 0x00,
		.supextto	= 0x00,
	},
	.noise_suppression_cfg = {
		.ctrl			= 0,
		.outlier_filter_len	= 0,
		.reserve0		= 0,
		.gcaf_upper_limit	= 0,
		.gcaf_lower_limit	= 0,
		.gcaf_low_count		= 0,
		.noise_threshold	= 0,
		.reserve1		= 0,
		.freq_hop_scale		= 0,
		.burst_freq_0		= 0,
		.burst_freq_1		= 0x0A,
		.burst_freq_2		= 0x0F,
		.burst_freq_3		= 0x14,
		.burst_freq_4		= 0x19,
		.idle_gcaf_valid	= 0,
	},
	.touch_proximity_cfg = {
		.ctrl			= 0,
		.x_origin		= 0,
		.y_origin		= 0,
		.x_size			= 0,
		.y_size			= 0,
		.reserve0		= 0,
		.blen			= 0,
		.tch_thresh		= 0,
		.tch_detect_int		= 0,
		.average		= 0,
		.move_null_rate		= 0,
		.move_det_tresh		= 0,
	},
	.one_touch_gesture_proc_cfg = {
		.ctrl			= 0,
		.reserve0		= 0,
		.gesture_enable		= 0,
		.pres_proc		= 0,
		.tap_time_out		= 0,
		.flick_time_out		= 0,
		.drag_time_out		= 0,
		.short_press_time_out	= 0,
		.long_press_time_out	= 0,
		.repeat_press_time_out	= 0,
		.flick_threshold	= 0,
		.drag_threshold		= 0,
		.tap_threshold		= 0,
		.throw_threshold	= 0,
	},
	.self_test_cfg = {
		.ctrl			= 0,
		.command		= 0,
		.high_signal_limit_0	= 0,
		.low_signal_limit_0	= 0,
		.high_signal_limit_1	= 0,
		.low_signal_limit_1	= 0,
		.high_signal_limit_2	= 0,
		.low_signal_limit_2	= 0,
	},
	.two_touch_gesture_proc_cfg = {
		.ctrl			= 0,
		.reserved0		= 0,
		.reserved1		= 0,
		.gesture_enable		= 0,
		.rotate_threshold	= 0,
		.zoom_threshold		= 0,
	},
	.cte_config_cfg = {
		.ctrl			= 0,
		.command		= 0,
		.mode			= 0,
		.idle_gcaf_depth	= 16,
		.active_gcaf_depth	= 16,
		.voltage		= 60,
	},
	.noise1_suppression_cfg = {
		.ctrl		= 0x01,
		.version	= 0x01,
		.atch_thr	= 0x64,
		.duty_cycle	= 0x08,
		.drift_thr	= 0x00,
		.clamp_thr	= 0x00,
		.diff_thr	= 0x00,
		.adjustment	= 0x00,
		.average	= 0x0000,
		.temp		= 0x00,
		.offset = {
			0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
			0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
			0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
			0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
			0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
			0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
			0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
			0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
			0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
			0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
			0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
			0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
			0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
			0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
			0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
			0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
			0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
			0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
			0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
			0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
			0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00
		},
		.bad_chan = {
			0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
			0x00, 0x00, 0x00
		},
		.x_short	= 0x00,
	},
	.vkeys			= {
		.count		= 0,
		.keys		= NULL,
	},
};

static struct i2c_board_info __initdata stingray_i2c_bus1_touch_info[] = {
	{
		I2C_BOARD_INFO(QTOUCH_TS_NAME, 0x5B),
		.platform_data = &stingray_touch_data,
		.irq = TEGRA_GPIO_TO_IRQ(STINGRAY_TOUCH_INT_N_GPIO),
	},
};


int __init stingray_touch_init(void)
{
	tegra_gpio_enable(STINGRAY_TOUCH_INT_N_GPIO);
	gpio_request(STINGRAY_TOUCH_INT_N_GPIO, "touch_irq");
	gpio_direction_input(STINGRAY_TOUCH_INT_N_GPIO);

	tegra_gpio_enable(STINGRAY_TOUCH_WAKE_N_GPIO);
	gpio_request(STINGRAY_TOUCH_WAKE_N_GPIO, "touch_wake");
	gpio_direction_output(STINGRAY_TOUCH_WAKE_N_GPIO, 0);

	tegra_gpio_enable(STINGRAY_TOUCH_RESET_N_GPIO);
	gpio_request(STINGRAY_TOUCH_RESET_N_GPIO, "touch_reset");
	gpio_direction_output(STINGRAY_TOUCH_RESET_N_GPIO, 1);

	i2c_register_board_info(0, stingray_i2c_bus1_touch_info, 1);

	return 0;
}
