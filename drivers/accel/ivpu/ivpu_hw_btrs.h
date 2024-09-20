/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2020-2024 Intel Corporation
 */

#ifndef __IVPU_HW_BTRS_H__
#define __IVPU_HW_BTRS_H__

#include "ivpu_drv.h"
#include "ivpu_hw_37xx_reg.h"
#include "ivpu_hw_40xx_reg.h"
#include "ivpu_hw_reg_io.h"

#define PLL_PROFILING_FREQ_DEFAULT   38400000
#define PLL_PROFILING_FREQ_HIGH      400000000
#define PLL_RATIO_TO_FREQ(x)         ((x) * PLL_REF_CLK_FREQ)

#define DCT_DEFAULT_ACTIVE_PERCENT 15u
#define DCT_PERIOD_US		   35300u

int ivpu_hw_btrs_info_init(struct ivpu_device *vdev);
void ivpu_hw_btrs_freq_ratios_init(struct ivpu_device *vdev);
int ivpu_hw_btrs_irqs_clear_with_0_mtl(struct ivpu_device *vdev);
int ivpu_hw_btrs_wp_drive(struct ivpu_device *vdev, bool enable);
int ivpu_hw_btrs_wait_for_clock_res_own_ack(struct ivpu_device *vdev);
int ivpu_hw_btrs_d0i3_enable(struct ivpu_device *vdev);
int ivpu_hw_btrs_d0i3_disable(struct ivpu_device *vdev);
void ivpu_hw_btrs_set_port_arbitration_weights_lnl(struct ivpu_device *vdev);
bool ivpu_hw_btrs_is_idle(struct ivpu_device *vdev);
int ivpu_hw_btrs_wait_for_idle(struct ivpu_device *vdev);
int ivpu_hw_btrs_ip_reset(struct ivpu_device *vdev);
void ivpu_hw_btrs_profiling_freq_reg_set_lnl(struct ivpu_device *vdev);
void ivpu_hw_btrs_ats_print_lnl(struct ivpu_device *vdev);
void ivpu_hw_btrs_clock_relinquish_disable_lnl(struct ivpu_device *vdev);
bool ivpu_hw_btrs_irq_handler_mtl(struct ivpu_device *vdev, int irq);
bool ivpu_hw_btrs_irq_handler_lnl(struct ivpu_device *vdev, int irq);
int ivpu_hw_btrs_dct_get_request(struct ivpu_device *vdev, bool *enable);
void ivpu_hw_btrs_dct_set_status(struct ivpu_device *vdev, bool enable, u32 dct_percent);
u32 ivpu_hw_btrs_pll_freq_get(struct ivpu_device *vdev);
u32 ivpu_hw_btrs_ratio_to_freq(struct ivpu_device *vdev, u32 ratio);
u32 ivpu_hw_btrs_telemetry_offset_get(struct ivpu_device *vdev);
u32 ivpu_hw_btrs_telemetry_size_get(struct ivpu_device *vdev);
u32 ivpu_hw_btrs_telemetry_enable_get(struct ivpu_device *vdev);
void ivpu_hw_btrs_global_int_enable(struct ivpu_device *vdev);
void ivpu_hw_btrs_global_int_disable(struct ivpu_device *vdev);
void ivpu_hw_btrs_irq_enable(struct ivpu_device *vdev);
void ivpu_hw_btrs_irq_disable(struct ivpu_device *vdev);
void ivpu_hw_btrs_diagnose_failure(struct ivpu_device *vdev);

#endif /* __IVPU_HW_BTRS_H__ */
