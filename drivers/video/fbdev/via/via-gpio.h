/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Support for viafb GPIO ports.
 *
 * Copyright 2009 Jonathan Corbet <corbet@lwn.net>
 */

#ifndef __VIA_GPIO_H__
#define __VIA_GPIO_H__

extern int viafb_gpio_init(void);
extern void viafb_gpio_exit(void);
#endif
