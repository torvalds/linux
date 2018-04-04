/* SPDX-License-Identifier: GPL-2.0+ */
/*
 * Samsung Platform - Keypad platform data definitions
 *
 * Copyright (C) 2010 Samsung Electronics Co.Ltd
 * Author: Joonyoung Shim <jy0922.shim@samsung.com>
 */

#ifndef __PLAT_SAMSUNG_KEYPAD_H
#define __PLAT_SAMSUNG_KEYPAD_H

#include <linux/input/samsung-keypad.h>

/**
 * samsung_keypad_set_platdata - Set platform data for Samsung Keypad device.
 * @pd: Platform data to register to device.
 *
 * Register the given platform data for use with Samsung Keypad device.
 * The call will copy the platform data, so the board definitions can
 * make the structure itself __initdata.
 */
extern void samsung_keypad_set_platdata(struct samsung_keypad_platdata *pd);

/* defined by architecture to configure gpio. */
extern void samsung_keypad_cfg_gpio(unsigned int rows, unsigned int cols);

#endif /* __PLAT_SAMSUNG_KEYPAD_H */
