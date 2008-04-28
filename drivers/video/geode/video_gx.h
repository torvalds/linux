/*
 * Geode GX video device
 *
 * Copyright (C) 2006 Arcom Control Systems Ltd.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */
#ifndef __VIDEO_GX_H__
#define __VIDEO_GX_H__

extern struct geode_vid_ops gx_vid_ops;

/* GX Flatpanel control MSR */
#define GX_VP_PAD_SELECT_MASK          0x3FFFFFFF
#define GX_VP_PAD_SELECT_TFT           0x1FFFFFFF

/* Geode GX clock control MSRs */

#  define MSR_GLCP_SYS_RSTPLL_DOTPREDIV2	(0x0000000000000002ull)
#  define MSR_GLCP_SYS_RSTPLL_DOTPREMULT2	(0x0000000000000004ull)
#  define MSR_GLCP_SYS_RSTPLL_DOTPOSTDIV3	(0x0000000000000008ull)

#  define MSR_GLCP_DOTPLL_DOTRESET		(0x0000000000000001ull)
#  define MSR_GLCP_DOTPLL_BYPASS		(0x0000000000008000ull)
#  define MSR_GLCP_DOTPLL_LOCK			(0x0000000002000000ull)

#endif /* !__VIDEO_GX_H__ */
