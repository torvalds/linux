// SPDX-License-Identifier: GPL-2.0+
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
#define LOBYTE(w)           ((unsigned char)(w))
#endif
#if !defined(HIBYTE)
#define HIBYTE(w)           ((unsigned char)(((unsigned short)(w) >> 8) & 0xFF))
#endif

#if !defined(LOWORD)
#define LOWORD(d)           ((unsigned short)(d))
#endif
#if !defined(HIWORD)
#define HIWORD(d)           ((unsigned short)((((unsigned long)(d)) >> 16) & 0xFFFF))
#endif

#define LODWORD(q)          ((q).u.dwLowDword)
#define HIDWORD(q)          ((q).u.dwHighDword)

#if !defined(MAKEWORD)
#define MAKEWORD(lb, hb)    ((unsigned short)(((unsigned char)(lb)) | (((unsigned short)((unsigned char)(hb))) << 8)))
#endif
#if !defined(MAKEDWORD)
#define MAKEDWORD(lw, hw)   ((unsigned long)(((unsigned short)(lw)) | (((unsigned long)((unsigned short)(hw))) << 16)))
#endif

#endif /* __TMACRO_H__ */
