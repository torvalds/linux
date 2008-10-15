/*
 * linux/arch/sh/kernel/cpu/sh4/sh4_fpu.h
 *
 * Copyright (C) 2006 STMicroelectronics Limited
 * Author: Carl Shaw <carl.shaw@st.com>
 *
 * May be copied or modified under the terms of the GNU General Public
 * License Version 2.  See linux/COPYING for more information.
 *
 * Definitions for SH4 FPU operations
 */

#ifndef __CPU_SH4_FPU_H
#define __CPU_SH4_FPU_H

#define FPSCR_ENABLE_MASK	0x00000f80UL

#define FPSCR_FMOV_DOUBLE	(1<<1)

#define FPSCR_CAUSE_INEXACT	(1<<12)
#define FPSCR_CAUSE_UNDERFLOW	(1<<13)
#define FPSCR_CAUSE_OVERFLOW	(1<<14)
#define FPSCR_CAUSE_DIVZERO	(1<<15)
#define FPSCR_CAUSE_INVALID	(1<<16)
#define FPSCR_CAUSE_ERROR 	(1<<17)

#define FPSCR_DBL_PRECISION	(1<<19)
#define FPSCR_ROUNDING_MODE(x)	((x >> 20) & 3)
#define FPSCR_RM_NEAREST	(0)
#define FPSCR_RM_ZERO		(1)

#endif
