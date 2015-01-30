/*
 * Tosa bluetooth built-in chip control.
 *
 * Later it may be shared with some other platforms.
 *
 * Copyright (c) 2008 Dmitry Baryshkov
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 */
#ifndef TOSA_BT_H
#define TOSA_BT_H

struct tosa_bt_data {
	int gpio_pwr;
	int gpio_reset;
};

#endif

