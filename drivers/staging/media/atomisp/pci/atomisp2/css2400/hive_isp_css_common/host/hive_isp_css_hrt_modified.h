/*
 * Support for Intel Camera Imaging ISP subsystem.
 * Copyright (c) 2010-2015, Intel Corporation.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 */

#ifndef _hive_isp_css_hrt_h
#define _hive_isp_css_hrt_h

#include "system_types.h"

#include "hive_isp_css_host_ids_hrt.h"
#include "hive_isp_css_defs.h"

#ifdef HRT_ISP_CSS_CUSTOM_HOST
#ifndef HRT_USE_VIR_ADDRS
#define HRT_USE_VIR_ADDRS
#endif
/*#include "hive_isp_css_custom_host_hrt.h"*/
#endif

#include <gpio_block.h>
#include <gp_regs.h>
#include <gp_timer_hrt.h>
  #include <css_receiver_2400_hrt.h>
//  #include <isp2400_mamoiada_params.h>
//  #include <isp2400_support.h>
  /* insert idle signal clearing and setting around hrt_main */
  #if !defined(HRT_HW) || defined(HRT_ISP_CSS_INSERT_IDLE_SIGNAL)
    #define hrt_main _hrt_isp_css_main
  #endif
  #ifdef _HIVE_ISP_CSS_SPECMAN_SYSTEM
    #include "hive_isp_css_2400_specman_system.h"
  #else
#if defined(IS_ISP_2400_MAMOIADA_SYSTEM)
    #include "hive_isp_css_2400_system.h"
#elif defined(IS_ISP_2401_MAMOIADA_SYSTEM)
    #include "hive_isp_css_2401_system.h"
#else
#error "hive_isp_css_hrt_modified.h: SYSTEM must be one of {2400_MAMOIADA_SYSTEM, 2401_MAMOIADA_SYSTEM}"
#endif
  #endif
#include <sp_hrt.h>
#include <input_system_hrt.h>
#include <input_selector_hrt.h>
#include <sig_monitor_hrt.h>

#include "hive_isp_css_sdram_wakeup_hrt.h"
#include "hive_isp_css_idle_signal_hrt.h"
#include "hive_isp_css_sp_hrt.h"
#include "hive_isp_css_isp_hrt.h"
#include "hive_isp_css_streaming_to_mipi_hrt.h"
#include "hive_isp_css_testbench_hrt.h"
#include "hive_isp_css_streaming_monitors_hrt.h"
#include "hive_isp_css_gp_regs_hrt.h"
#if defined(IS_ISP_2400_MAMOIADA_SYSTEM)
#include "hive_isp_css_irq_hrt.h"
#elif defined(IS_ISP_2401_MAMOIADA_SYSTEM)
#include "hive_isp_css_2401_irq_hrt.h"
#else
#error "hive_isp_css_hrt_modified.h: SYSTEM must be one of {2400_MAMOIADA_SYSTEM, 2401_MAMOIADA_SYSTEM}"
#endif

#include "hive_isp_css_stream_switch_hrt.h"

#include "hive_isp_css_ddr_hrt_modified.h"
#include "hive_isp_css_dma_set_hrt.h"

#define HIVE_ISP_CSS_NO_STREAM_SWITCH 1

#endif /* _hive_isp_css_hrt_h */
