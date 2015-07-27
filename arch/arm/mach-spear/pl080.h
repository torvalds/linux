/*
 * arch/arm/plat-spear/include/plat/pl080.h
 *
 * DMAC pl080 definitions for SPEAr platform
 *
 * Copyright (C) 2012 ST Microelectronics
 * Viresh Kumar <vireshk@kernel.org>
 *
 * This file is licensed under the terms of the GNU General Public
 * License version 2. This program is licensed "as is" without any
 * warranty of any kind, whether express or implied.
 */

#ifndef __PLAT_PL080_H
#define __PLAT_PL080_H

struct pl08x_channel_data;
int pl080_get_signal(const struct pl08x_channel_data *cd);
void pl080_put_signal(const struct pl08x_channel_data *cd, int signal);

#endif /* __PLAT_PL080_H */
