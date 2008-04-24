/*
 * arch/arm/mach-ns9xxx/gpio-ns9360.h
 *
 * Copyright (C) 2006,2007 by Digi International Inc.
 * All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 as published by
 * the Free Software Foundation.
 */
int __ns9360_gpio_configure(unsigned gpio, int dir, int inv, int func);
int ns9360_gpio_get_value(unsigned gpio);
void ns9360_gpio_set_value(unsigned gpio, int value);
