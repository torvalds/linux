/*
 * Copyright (c) 1996, 2003 VIA Networking Technologies, Inc.
 * All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 *
 * File: tmacro.h
 *
 * Purpose: define basic common types and macros
 *
 * Author: Tevin Chen
 *
 * Date: May 21, 1996
 *
 */

#ifndef __TMACRO_H__
#define __TMACRO_H__

/****** Common helper macros ***********************************************/

#if !defined(LOBYTE)
#define LOBYTE(w)           ((u8)(w))
#endif
#if !defined(HIBYTE)
#define HIBYTE(w)           ((u8)(((u16)(w) >> 8) & 0xFF))
#endif

#if !defined(LOWORD)
#define LOWORD(d)           ((u16)(d))
#endif
#if !defined(HIWORD)
#define HIWORD(d)           ((u16)((((u32)(d)) >> 16) & 0xFFFF))
#endif

#define LODWORD(q)          ((q).u.dwLowDword)
#define HIDWORD(q)          ((q).u.dwHighDword)

#if !defined(MAKEWORD)
#define MAKEWORD(lb, hb)    ((u16)(((u8)(lb)) | (((u16)((u8)(hb))) << 8)))
#endif
#if !defined(MAKEDWORD)
#define MAKEDWORD(lw, hw)   ((u32)(((u16)(lw)) | (((u32)((u16)(hw))) << 16)))
#endif

#endif /* __TMACRO_H__ */
