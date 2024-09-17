/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Tosa bluetooth built-in chip control.
 *
 * Later it may be shared with some other platforms.
 *
 * Copyright (c) 2008 Dmitry Baryshkov
 */
#ifndef TOSA_BT_H
#define TOSA_BT_H

struct tosa_bt_data {
	int gpio_pwr;
	int gpio_reset;
};

#endif

