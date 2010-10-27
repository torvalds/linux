/*
 * Pin-multiplex helper macros for TI DaVinci family devices
 *
 * Author: Vladimir Barinov, MontaVista Software, Inc. <source@mvista.com>
 *
 * 2007 (c) MontaVista Software, Inc. This file is licensed under
 * the terms of the GNU General Public License version 2. This program
 * is licensed "as is" without any warranty of any kind, whether express
 * or implied.
 *
 * Copyright (C) 2008 Texas Instruments.
 */
#ifndef _MACH_DAVINCI_MUX_H_
#define _MACH_DAVINCI_MUX_H_

#include <mach/mux.h>

#define MUX_CFG(soc, desc, muxreg, mode_offset, mode_mask, mux_mode, dbg)\
[soc##_##desc] = {							\
			.name =  #desc,					\
			.debug = dbg,					\
			.mux_reg_name = "PINMUX"#muxreg,		\
			.mux_reg = PINMUX(muxreg),			\
			.mask_offset = mode_offset,			\
			.mask = mode_mask,				\
			.mode = mux_mode,				\
		},

#define INT_CFG(soc, desc, mode_offset, mode_mask, mux_mode, dbg)	\
[soc##_##desc] = {							\
			.name =  #desc,					\
			.debug = dbg,					\
			.mux_reg_name = "INTMUX",			\
			.mux_reg = INTMUX,				\
			.mask_offset = mode_offset,			\
			.mask = mode_mask,				\
			.mode = mux_mode,				\
		},

#define EVT_CFG(soc, desc, mode_offset, mode_mask, mux_mode, dbg)	\
[soc##_##desc] = {							\
			.name =  #desc,					\
			.debug = dbg,					\
			.mux_reg_name = "EVTMUX",			\
			.mux_reg = EVTMUX,				\
			.mask_offset = mode_offset,			\
			.mask = mode_mask,				\
			.mode = mux_mode,				\
		},

#endif /* _MACH_DAVINCI_MUX_H */
