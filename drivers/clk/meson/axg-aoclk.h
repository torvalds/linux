/* SPDX-License-Identifier: (GPL-2.0+ OR MIT) */
/*
 * Copyright (c) 2017 BayLibre, SAS
 * Author: Neil Armstrong <narmstrong@baylibre.com>
 *
 * Copyright (c) 2018 Amlogic, inc.
 * Author: Qiufang Dai <qiufang.dai@amlogic.com>
 */

#ifndef __AXG_AOCLKC_H
#define __AXG_AOCLKC_H

#define NR_CLKS	11
/* AO Configuration Clock registers offsets
 * Register offsets from the data sheet must be multiplied by 4.
 */
#define AO_RTI_PWR_CNTL_REG1	0x0C
#define AO_RTI_PWR_CNTL_REG0	0x10
#define AO_RTI_GEN_CNTL_REG0	0x40
#define AO_OSCIN_CNTL		0x58
#define AO_CRT_CLK_CNTL1	0x68
#define AO_SAR_CLK		0x90
#define AO_RTC_ALT_CLK_CNTL0	0x94
#define AO_RTC_ALT_CLK_CNTL1	0x98

#include <dt-bindings/clock/axg-aoclkc.h>
#include <dt-bindings/reset/axg-aoclkc.h>

#endif /* __AXG_AOCLKC_H */
