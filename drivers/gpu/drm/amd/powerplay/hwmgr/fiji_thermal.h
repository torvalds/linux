/*
 * Copyright 2015 Advanced Micro Devices, Inc.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE COPYRIGHT HOLDER(S) OR AUTHOR(S) BE LIABLE FOR ANY CLAIM, DAMAGES OR
 * OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
 * ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 * OTHER DEALINGS IN THE SOFTWARE.
 *
 */

#ifndef FIJI_THERMAL_H
#define FIJI_THERMAL_H

#include "hwmgr.h"

#define FIJI_THERMAL_HIGH_ALERT_MASK         0x1
#define FIJI_THERMAL_LOW_ALERT_MASK          0x2

#define FIJI_THERMAL_MINIMUM_TEMP_READING    -256
#define FIJI_THERMAL_MAXIMUM_TEMP_READING    255

#define FIJI_THERMAL_MINIMUM_ALERT_TEMP      0
#define FIJI_THERMAL_MAXIMUM_ALERT_TEMP      255

#define FDO_PWM_MODE_STATIC  1
#define FDO_PWM_MODE_STATIC_RPM 5


extern int tf_fiji_thermal_initialize(struct pp_hwmgr *hwmgr, void *input, void *output, void *storage, int result);
extern int tf_fiji_thermal_set_temperature_range(struct pp_hwmgr *hwmgr, void *input, void *output, void *storage, int result);
extern int tf_fiji_thermal_enable_alert(struct pp_hwmgr *hwmgr, void *input, void *output, void *storage, int result);

extern int fiji_thermal_get_temperature(struct pp_hwmgr *hwmgr);
extern int fiji_thermal_stop_thermal_controller(struct pp_hwmgr *hwmgr);
extern int fiji_fan_ctrl_get_fan_speed_info(struct pp_hwmgr *hwmgr, struct phm_fan_speed_info *fan_speed_info);
extern int fiji_fan_ctrl_get_fan_speed_percent(struct pp_hwmgr *hwmgr, uint32_t *speed);
extern int fiji_fan_ctrl_set_default_mode(struct pp_hwmgr *hwmgr);
extern int fiji_fan_ctrl_set_static_mode(struct pp_hwmgr *hwmgr, uint32_t mode);
extern int fiji_fan_ctrl_set_fan_speed_percent(struct pp_hwmgr *hwmgr, uint32_t speed);
extern int fiji_fan_ctrl_reset_fan_speed_to_default(struct pp_hwmgr *hwmgr);
extern int pp_fiji_thermal_initialize(struct pp_hwmgr *hwmgr);
extern int fiji_thermal_ctrl_uninitialize_thermal_controller(struct pp_hwmgr *hwmgr);
extern int fiji_fan_ctrl_set_fan_speed_rpm(struct pp_hwmgr *hwmgr, uint32_t speed);
extern int fiji_fan_ctrl_get_fan_speed_rpm(struct pp_hwmgr *hwmgr, uint32_t *speed);
extern int fiji_fan_ctrl_stop_smc_fan_control(struct pp_hwmgr *hwmgr);
extern uint32_t tonga_get_xclk(struct pp_hwmgr *hwmgr);

#endif

