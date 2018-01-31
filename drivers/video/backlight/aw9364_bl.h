/* SPDX-License-Identifier: GPL-2.0 */

#ifndef __DRIVERS_TOUCHSCREEN_AW9364_TS_H
#define __DRIVERS_TOUCHSCREEN_AW9364_TS_H

struct aw9364_platform_data {
	int pin_en;
	int current_brightness;
	int (*io_init)(void);
	int (*io_deinit)(void);
};
#endif

