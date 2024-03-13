/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Support for Intel Camera Imaging ISP subsystem.
 * Copyright (c) 2015, Intel Corporation.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 */

#ifndef _HRT_DEFS_H_
#define _HRT_DEFS_H_

#ifndef HRTCAT
#define _HRTCAT(m, n)     m##n
#define HRTCAT(m, n)      _HRTCAT(m, n)
#endif

#ifndef HRTSTR
#define _HRTSTR(x)   #x
#define HRTSTR(x)    _HRTSTR(x)
#endif

#ifndef HRTMIN
#define HRTMIN(a, b) (((a) < (b)) ? (a) : (b))
#endif

#ifndef HRTMAX
#define HRTMAX(a, b) (((a) > (b)) ? (a) : (b))
#endif

#endif /* _HRT_DEFS_H_ */
