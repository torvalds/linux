/*
 * arch/arm/mach-rk29/include/mach/iomux.h
 *
 *Copyright (C) 2010 ROCKCHIP, Inc.
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#ifndef __RK29_IOMUX_H__
#define __RK29_IOMUX_H__

#include "rk29_iomap.h"

#define RK29_IOMUX_GPIO0L_CON			RK29_GRF_BASE+0x48
#define RK29_IOMUX_GPIO0H_CON			RK29_GRF_BASE+0x4c
#define RK29_IOMUX_GPIO1L_CON			RK29_GRF_BASE+0x50
#define RK29_IOMUX_GPIO1H_CON			RK29_GRF_BASE+0x54
#define RK29_IOMUX_GPIO2L_CON			RK29_GRF_BASE+0x58
#define RK29_IOMUX_GPIO2H_CON			RK29_GRF_BASE+0x5c
#define RK29_IOMUX_GPIO3L_CON			RK29_GRF_BASE+0x60
#define RK29_IOMUX_GPIO3H_CON			RK29_GRF_BASE+0x64
#define RK29_IOMUX_GPIO4L_CON			RK29_GRF_BASE+0x68
#define RK29_IOMUX_GPIO4H_CON			RK29_GRF_BASE+0x6c
#define RK29_IOMUX_GPIO5L_CON			RK29_GRF_BASE+0x70
#define RK29_IOMUX_GPIO5H_CON			RK29_GRF_BASE+0x74


#define MUX_CFG(desc,reg,off,interl,mux_mode,bflags)	\
{						  	\
        .name = desc,                                   \
        .offset = off,                               	\
        .interleave = interl,                       	\
        .mux_reg = RK29_IOMUX_##reg##_CON,          \
        .mode = mux_mode,                               \
        .premode = mux_mode,                            \
        .flags = bflags,				\
},

struct mux_config {
	char *name;
	const unsigned int offset;
	unsigned int mode;
	unsigned int premode;
	const unsigned int mux_reg;
	const unsigned int interleave;
	unsigned int flags;
};


#endif