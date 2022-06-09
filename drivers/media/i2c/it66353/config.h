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
#ifndef _66353_CONFIG_H_
#define _66353_CONFIG_H_

#ifndef _SHOW_PRAGMA_MSG
#define message(ignore)
#endif

/*
 * Switch register i2c address: 0x94(PCADR=0) or 0x96(PCADR=1)
 */
#define SWAddr		0xAC

/*
 * RX register i2c address ( programmable )
 */
#define RXAddr		0xB2

/*
 * CEC register i2c address ( programmable )
 */
#define CECAddr		0xC0

/*
 * EDID RAM i2c address ( programmable )
 */
#define RXEDIDAddr	0xa8

/*
 * Internal compile options:
 */
#define DEBUG_FSM_CHANGE 1
#define USING_WDOG 0

/*
 * 66353 and 66354 don't need this:
 * set to 0
 */
#define CHECK_INT_BEFORE_TXOE 0

/*
 * EN_AUTO_RS: ( compile option )
 * 1: Enable Auto EQ code
 * 0: Disable Auto EQ code
 */
#define EN_H14_SKEW 0

/*
 * EN_AUTO_RS: ( compile option )
 * 1: Enable Auto EQ code
 * 0: Disable Auto EQ code
 */
#define EN_AUTO_RS 1

/*
 * EN_CEC:
 * 1: Enable CEC function
 * 0: Disable CEC function
 */
#define EN_CEC 0

/*
 * FIX_EDID_FOR_ATC_4BLOCK_CTS:
 * 1: For ATC 4 blocks EDID test
 */
#define FIX_EDID_FOR_ATC_4BLOCK_CTS 1

#endif  // _66353_CONFIG_H_

