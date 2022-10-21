/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * arch/arm/plat-spear/include/plat/pl080.h
 *
 * DMAC pl080 definitions for SPEAr platform
 *
 * Copyright (C) 2012 ST Microelectronics
 * Viresh Kumar <vireshk@kernel.org>
 */

#ifndef __PLAT_PL080_H
#define __PLAT_PL080_H

struct pl08x_channel_data;
int pl080_get_signal(const struct pl08x_channel_data *cd);
void pl080_put_signal(const struct pl08x_channel_data *cd, int signal);

#endif /* __PLAT_PL080_H */
