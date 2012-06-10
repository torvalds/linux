/*
 * arch/arm/plat-spear/include/plat/pl080.h
 *
 * DMAC pl080 definitions for SPEAr platform
 *
 * Copyright (C) 2012 ST Microelectronics
 * Viresh Kumar <viresh.kumar@st.com>
 *
 * This file is licensed under the terms of the GNU General Public
 * License version 2. This program is licensed "as is" without any
 * warranty of any kind, whether express or implied.
 */

#ifndef __PLAT_PL080_H
#define __PLAT_PL080_H

struct pl08x_dma_chan;
int pl080_get_signal(struct pl08x_dma_chan *ch);
void pl080_put_signal(struct pl08x_dma_chan *ch);

#endif /* __PLAT_PL080_H */
