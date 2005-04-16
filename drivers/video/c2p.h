/*
 *  Fast C2P (Chunky-to-Planar) Conversion
 *
 *  Copyright (C) 2003 Geert Uytterhoeven
 *
 *  This file is subject to the terms and conditions of the GNU General Public
 *  License. See the file COPYING in the main directory of this archive
 *  for more details.
 */

#include <linux/types.h>

extern void c2p(u8 *dst, const u8 *src, u32 dx, u32 dy, u32 width, u32 height,
		u32 dst_nextline, u32 dst_nextplane, u32 src_nextline,
		u32 bpp);

