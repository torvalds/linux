// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2022 Rockchip Electronics Co. Ltd.
 *
 * it66353 HDMI 3 in 1 out driver.
 *
 * Author: Kenneth.Hung@ite.com.tw
 * 	   Wangqiang Guo <kay.guo@rock-chips.com>
 * Version: IT66353_SAMPLE_1.08
 *
 */
#ifndef _DEBUG_H_
#define _DEBUG_H_

#include <linux/kernel.h>
#include "platform.h"

#define DBG_TXOE_1(x) {  }
#define DBG_TXOE_0(x) {  }

#define DBG_CLKSTABLE_0(x) {  }
#define DBG_CLKSTABLE_1(x) {  }

#define DBG_SYMLOCK_0(x) {  }
#define DBG_SYMLOCK_1(x) {  }

enum {
	RX_SWITCH_PORT,
	RX_HPD_HIGH,
	RX_HPD_LOW,
	CLK_STABLE,
	CLK_UNSTABLE,
	AEQ_TOGGLE_HPD,
	TXOE0,
	TXOE1,
};

#define DBG_TM(n) // { __debug_set_io(n); }
int set_port(int portnum, int wrmask, int wrdata);
void __debug_set_io(u8 n);
#endif
