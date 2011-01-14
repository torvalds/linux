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

#define STINGRAY_TOUCH_INT_N_GPIO_P2	TEGRA_GPIO_PZ2

static int stingray_touch_reset(void)
{
	gpio_set_value(STINGRAY_TOUCH_RESET_N_GPIO, 0);
	msleep(10);
	gpio_set_value(STINGRAY_TOUCH_RESET_N_GPIO, 1);
	msleep(100); /* value from moto */
	return 0;
}

static int stingray_touch_suspend(int enable)
{
	gpio_set_value(STINGRAY_TOUCH_WAKE_N_GPIO, enable);
	msleep(20);
	return 0;
}

/* mortable M1 */
struct qtouch_ts_platform_data stingray_touch_data_m1 = {

	.flags		= (QTOUCH_USE_MULTITOUCH |
			   QTOUCH_CFG_BACKUPNV),
	.irqflags		= (IRQF_TRIGGER_LOW),
	.abs_min_x		= 0,
	.abs_max_x		= 4095,
	.abs_min_y		= 0,
	.abs_max_y		= 4095,
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
	.touch_fw_cfg = {
		.fw_name = "mXT1386_08_E1.bin",
		.family_id = 0xA0,
		.variant_id = 0x00,
		.fw_version = 0x08,
		.fw_build = 0xE1,
		.boot_version = 0x20,
		.base_fw_version = 0x00,
	},
	.power_cfg	= {
		.idle_acq_int	= 0xff,
		.active_acq_int	= 0xff,
		.active_idle_to	= 0x00,
	},
	.acquire_cfg	= {
		.charge_time	= 0x0A,
		.reserve1	= 0,
		.touch_drift	= 0x14,
		.drift_susp	= 0x14,
		.touch_autocal	= 0x00,
		.reserve5	= 0,
		.atch_cal_suspend_time	= 0,
		.atch_cal_suspend_thres	= 0,
		.atch_cal_force_thres = 0x10,
		.atch_cal_force_ratio = 0,
	},
	.multi_touch_cfg	= {
		.ctrl		= 0x83,
		.x_origin	= 0,
		.y_origin	= 0,
		.x_size		= 0x1b,
		.y_size		= 0x2a,
		.aks_cfg	= 0,
		.burst_len	= 0x10,
		.tch_det_thr	= 45,
		.tch_det_int	= 0x3,
		.orient		= 7,
		.mrg_to		= 0x00,
		.mov_hyst_init	= 0x05,
		.mov_hyst_next	= 0x02,
		.mov_filter	= 0x20,
		.num_touch	= 0x01,
		.merge_hyst	= 0x0A,
		.merge_thresh	= 0x0A,
		.amp_hyst       = 0x0A,
		.x_res		= 0x0FFF,
		.y_res		= 0x0FFF,
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
	.comms_config_cfg = {
		.ctrl		= 0,
		.command	= 0,
	},
	.noise_suppression_cfg = {
		.ctrl			= 0,
		.reserve1		= 0,
		.reserve2		= 0,
		.reserve3		= 0,
		.reserve4		= 0,
		.reserve5		= 0,
		.reserve6		= 0,
		.reserve7		= 0,
		.noise_thres		= 0,
		.reserve9		= 0,
		.freq_hop_scale		= 0,
		.burst_freq_0           = 0,
		.burst_freq_1           = 0,
		.burst_freq_2           = 0,
		.burst_freq_3           = 0,
		.burst_freq_4           = 0,
		.reserve16		= 0,
        },
	.one_touch_gesture_proc_cfg = {
		.ctrl			= 0,
		.num_gestures		= 0,
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
		.num_gestures		= 0,
		.reserve2		= 0,
		.gesture_enable		= 0,
		.rotate_threshold	= 0,
		.zoom_threshold		= 0,
	},
	.cte_config_cfg = {
		.ctrl			= 0,
		.command		= 0,
		.reserve2		= 0,
		.idle_gcaf_depth	= 16,
		.active_gcaf_depth	= 16,
		.voltage		= 60,
	},
	.gripsuppression_t40_cfg = {
		.ctrl			= 0,
		.xlo_grip		= 0,
		.xhi_grip		= 0,
		.ylo_grip		= 0,
		.yhi_grip		= 0,
	},
	.palm_suppression_cfg = {
		.ctrl			= 0,
		.small_obj_thr		= 0,
		.sig_spread_thr		= 0,
		.large_obj_thr		= 0,
		.distance_thr		= 0,
		.sup_ext_to		= 0,
	},
	.spt_digitizer_cfg = {
		.ctrl			= 0,
		.hid_idlerate		= 0,
		.xlength		= 0,
		.ylength		= 0,
	},

	.vkeys			= {
		.count		= 0,
		.keys		= NULL,
	},
};

/* Portable P0 */
struct qtouch_ts_platform_data stingray_touch_data_p0 = {

	.flags		= (QTOUCH_USE_MULTITOUCH |
			   QTOUCH_CFG_BACKUPNV),
	.irqflags		= (IRQF_TRIGGER_LOW),
	.abs_min_x		= 0,
	.abs_max_x		= 4095,
	.abs_min_y		= 255,
	.abs_max_y		= 4058,
	.abs_min_p		= 0,
	.abs_max_p		= 255,
	.abs_min_w		= 0,
	.abs_max_w		= 15,
	.x_delta		= 400,
	.y_delta		= 250,
	.nv_checksum		= 0x14c8ac,
	.fuzz_x			= 0,
	.fuzz_y			= 0,
	.fuzz_p			= 2,
	.fuzz_w			= 2,
	.boot_i2c_addr	= XMEGAT_BL_I2C_ADDR,
	.hw_reset		= stingray_touch_reset,
	.hw_suspend		= stingray_touch_suspend,
	.key_array = {
		.cfg		= NULL,
		.keys		= NULL,
		.num_keys	= 0,
	},
	.touch_fw_cfg = {
		.fw_name = "mXT1386_10_FF.bin",
		.family_id = 0xA0,
		.variant_id = 0x00,
		.fw_version = 0x10,
		.fw_build = 0xFF,
		.boot_version = 0x20,
		.base_fw_version = 0x00,
	},
	.power_cfg	= {
		.idle_acq_int	= 0x12,
		.active_acq_int	= 0xFF,
		.active_idle_to	= 0x0A,
	},
	.acquire_cfg	= {
		.charge_time	= 0x0A,
		.reserve1	= 0,
		.touch_drift	= 0x14,
		.drift_susp	= 0x14,
		.touch_autocal	= 0x4B,
		.reserve5	= 0,
		.atch_cal_suspend_time	= 0,
		.atch_cal_suspend_thres	= 0,
		.atch_cal_force_thres = 0x10,
		.atch_cal_force_ratio = 0x10,
	},
	.multi_touch_cfg	= {
		.ctrl		= 0x8B,
		.x_origin	= 0,
		.y_origin	= 0,
		.x_size		= 0x21,
		.y_size		= 0x2a,
		.aks_cfg	= 0,
		.burst_len      = 0x20,
		.tch_det_thr    = 0x2d,
		.tch_det_int	= 0x2,
		.orient		= 1,
		.mrg_to		= 0x0A,
		.mov_hyst_init	= 0x32,
		.mov_hyst_next	= 0x14,
		.mov_filter	= 0x3D,
		.num_touch	= 0x0A,
		.merge_hyst	= 0x10,
		.merge_thresh	= 0x23,
		.amp_hyst       = 0x0A,
		.x_res		= 0x0FFF,
		.y_res		= 0x0FFF,
		.x_low_clip	= 0x06,
		.x_high_clip	= 0x0A,
		.y_low_clip	= 0x05,
		.y_high_clip	= 0xFF,
		.x_edge_ctrl    = 0xD2,
		.x_edge_dist	= 0x28,
		.y_edge_ctrl    = 0x2E,
		.y_edge_dist	= 0x0A,
		.jump_limit	= 0x20,
		.tch_thres_hyst = 0,
		.xpitch		= 0x2D,
		.ypitch		= 0x2D,
        },
	.comms_config_cfg = {
		.ctrl		= 0,
		.command	= 0,
	},
	.noise_suppression_cfg = {
		.ctrl			= 5,
		.reserve1		= 0,
		.reserve2		= 0,
		.reserve3		= 0,
		.reserve4		= 0,
		.reserve5		= 0,
		.reserve6		= 0,
		.reserve7		= 0,
		.noise_thres		= 32,
		.reserve9		= 0,
		.freq_hop_scale		= 0,
		.burst_freq_0		= 10,
		.burst_freq_1           = 15,
		.burst_freq_2           = 19,
		.burst_freq_3           = 25,
		.burst_freq_4           = 30,
		.reserve16		= 0,
	},
	.one_touch_gesture_proc_cfg = {
		.ctrl			= 0,
		.num_gestures		= 0,
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
		.num_gestures		= 0,
		.reserve2		= 0,
		.gesture_enable		= 0,
		.rotate_threshold	= 0,
		.zoom_threshold		= 0,
	},
	.cte_config_cfg = {
		.ctrl			= 0,
		.command		= 0,
                .reserve2		= 0,
		.idle_gcaf_depth	= 20,
		.active_gcaf_depth	= 20,
		.voltage		= 60,
	},
	.gripsuppression_t40_cfg = {
		.ctrl			= 0,
		.xlo_grip		= 0,
		.xhi_grip		= 0,
		.ylo_grip		= 0,
		.yhi_grip		= 0,
	},
	.palm_suppression_cfg = {
		.ctrl			= 1,
		.small_obj_thr		= 0,
		.sig_spread_thr		= 0,
		.large_obj_thr		= 0xD3,
		.distance_thr		= 0x05,
		.sup_ext_to		= 0x05,
		.strength		= 0xDC,
	},
	.spt_digitizer_cfg = {
		.ctrl			= 0,
		.hid_idlerate		= 0,
		.xlength		= 0,
		.ylength		= 0,
	},

	.vkeys                  = {
                .count          = 0,
                .keys           = NULL,
        },
};

/* Portable P1 and later versions */
struct qtouch_ts_platform_data stingray_touch_data_p1_or_later = {

	.flags		= (QTOUCH_USE_MULTITOUCH |
			   QTOUCH_CFG_BACKUPNV),
	.irqflags		= (IRQF_TRIGGER_LOW),
	.abs_min_x		= 0,
	.abs_max_x		= 4095,
	.abs_min_y		= 0,
	.abs_max_y		= 4095,
	.abs_min_p		= 0,
	.abs_max_p		= 255,
	.abs_min_w		= 0,
	.abs_max_w		= 15,
	.x_delta		= 400,
	.y_delta		= 250,
	.nv_checksum		= 0x14c8ac,
	.fuzz_x			= 0,
	.fuzz_y			= 0,
	.fuzz_p			= 2,
	.fuzz_w			= 2,
	.boot_i2c_addr	= XMEGAT_BL_I2C_ADDR,
	.hw_reset		= stingray_touch_reset,
	.hw_suspend		= stingray_touch_suspend,
	.key_array = {
		.cfg		= NULL,
		.keys		= NULL,
		.num_keys	= 0,
	},
	.touch_fw_cfg = {
		.fw_name = "mXT1386_10_FF.bin",
		.family_id = 0xA0,
		.variant_id = 0x00,
		.fw_version = 0x10,
		.fw_build = 0xFF,
		.boot_version = 0x20,
		.base_fw_version = 0x00,
	},
	.power_cfg	= {
		.idle_acq_int	= 0x12,
		.active_acq_int	= 0xFF,
		.active_idle_to	= 0x0A,
	},
	.acquire_cfg	= {
		.charge_time	= 0x0A,
		.reserve1	= 0,
		.touch_drift	= 0x14,
		.drift_susp	= 0x14,
		.touch_autocal	= 0x4B,
		.reserve5	= 0,
		.atch_cal_suspend_time	= 0,
		.atch_cal_suspend_thres	= 0,
		.atch_cal_force_thres = 0x10,
		.atch_cal_force_ratio = 0x10,
	},
	.multi_touch_cfg	= {
		.ctrl		= 0x8B,
		.x_origin	= 0,
		.y_origin	= 0,
		.x_size		= 0x21,
		.y_size		= 0x2a,
		.aks_cfg	= 0,
		.burst_len      = 0x20,
		.tch_det_thr    = 0x2d,
		.tch_det_int	= 0x2,
		.orient		= 1,
		.mrg_to		= 0x0A,
		.mov_hyst_init	= 0x32,
		.mov_hyst_next	= 0x14,
		.mov_filter	= 0x3D,
		.num_touch	= 0x0A,
		.merge_hyst	= 0x10,
		.merge_thresh	= 0x23,
		.amp_hyst       = 0x0A,
		.x_res		= 0x0FFF,
		.y_res		= 0x0FFF,
		.x_low_clip	= 0x06,
		.x_high_clip	= 0x0A,
		.y_low_clip	= 0x05,
		.y_high_clip	= 0xFF,
		.x_edge_ctrl    = 0xD2,
		.x_edge_dist	= 0x28,
		.y_edge_ctrl    = 0x2E,
		.y_edge_dist	= 0x0A,
		.jump_limit	= 0x20,
		.tch_thres_hyst = 0,
		.xpitch		= 0x2D,
		.ypitch		= 0x2D,
        },
	.comms_config_cfg = {
		.ctrl		= 0,
		.command	= 0,
	},
	.noise_suppression_cfg = {
		.ctrl			= 5,
		.reserve1		= 0,
		.reserve2		= 0,
		.reserve3		= 0,
		.reserve4		= 0,
		.reserve5		= 0,
		.reserve6		= 0,
		.reserve7		= 0,
		.noise_thres		= 32,
		.reserve9		= 0,
		.freq_hop_scale		= 0,
		.burst_freq_0		= 10,
		.burst_freq_1           = 15,
		.burst_freq_2           = 19,
		.burst_freq_3           = 25,
		.burst_freq_4           = 30,
		.reserve16		= 0,
	},
	.one_touch_gesture_proc_cfg = {
		.ctrl			= 0,
		.num_gestures		= 0,
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
		.num_gestures		= 0,
		.reserve2		= 0,
		.gesture_enable		= 0,
		.rotate_threshold	= 0,
		.zoom_threshold		= 0,
	},
	.cte_config_cfg = {
		.ctrl			= 0,
		.command		= 0,
		.reserve2		= 0,
		.idle_gcaf_depth	= 20,
		.active_gcaf_depth	= 20,
		.voltage		= 60,
	},
	.gripsuppression_t40_cfg = {
		.ctrl			= 0,
		.xlo_grip		= 0,
		.xhi_grip		= 0,
		.ylo_grip		= 0,
		.yhi_grip		= 0,
		},
	.palm_suppression_cfg = {
		.ctrl			= 1,
		.small_obj_thr		= 0,
		.sig_spread_thr		= 0,
		.large_obj_thr		= 0xD3,
		.distance_thr		= 0x05,
		.sup_ext_to		= 0x05,
		.strength		= 0xDC,
	},
	.spt_digitizer_cfg = {
		.ctrl			= 0,
		.hid_idlerate		= 0,
		.xlength		= 0,
		.ylength		= 0,
	},

	.vkeys			= {
		.count		= 0,
		.keys		= NULL,
	},
};

static struct i2c_board_info __initdata stingray_i2c_bus1_touch_info[] = {
	{
		I2C_BOARD_INFO(QTOUCH_TS_NAME, 0x5B),
	},
};

int __init stingray_touch_init(void)
{
	unsigned touch_int_gpio;

	if (stingray_revision() == STINGRAY_REVISION_P2)
		touch_int_gpio = STINGRAY_TOUCH_INT_N_GPIO_P2;
	else
		touch_int_gpio = STINGRAY_TOUCH_INT_N_GPIO;

	tegra_gpio_enable(touch_int_gpio);
	gpio_request(touch_int_gpio, "touch_irq");
	gpio_direction_input(touch_int_gpio);

	tegra_gpio_enable(STINGRAY_TOUCH_WAKE_N_GPIO);
	gpio_request(STINGRAY_TOUCH_WAKE_N_GPIO, "touch_wake");
	gpio_direction_output(STINGRAY_TOUCH_WAKE_N_GPIO, 0);

	tegra_gpio_enable(STINGRAY_TOUCH_RESET_N_GPIO);
	gpio_request(STINGRAY_TOUCH_RESET_N_GPIO, "touch_reset");
	gpio_direction_output(STINGRAY_TOUCH_RESET_N_GPIO, 1);

	stingray_i2c_bus1_touch_info[0].irq =
		 TEGRA_GPIO_TO_IRQ(touch_int_gpio);

	if ((stingray_revision() == STINGRAY_REVISION_P1) ||
		(stingray_revision() == STINGRAY_REVISION_P2) ||
		(stingray_revision() == STINGRAY_REVISION_P3))
		stingray_i2c_bus1_touch_info[0].platform_data =
				 &stingray_touch_data_p1_or_later;
	else if (stingray_revision() == STINGRAY_REVISION_P0)
		stingray_i2c_bus1_touch_info[0].platform_data = &stingray_touch_data_p0;
	else
		stingray_i2c_bus1_touch_info[0].platform_data = &stingray_touch_data_m1;

	i2c_register_board_info(0, stingray_i2c_bus1_touch_info, 1);

	return 0;
}
