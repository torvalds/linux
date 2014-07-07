/* Driver for Realtek PCI-Express card reader
 * Header file
 *
 * Copyright(c) 2009-2013 Realtek Semiconductor Corp. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; either version 2, or (at your option) any
 * later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, see <http://www.gnu.org/licenses/>.
 *
 * Author:
 *   Wei WANG (wei_wang@realsil.com.cn)
 *   Micky Ching (micky_ching@realsil.com.cn)
 */

#ifndef __REALTEK_RTSX_DEBUG_H
#define __REALTEK_RTSX_DEBUG_H

#include <linux/kernel.h>

#define RTSX_STOR "rts5208: "

#ifdef CONFIG_RTS5208_DEBUG
#define RTSX_DEBUGP(x...) pr_debug(RTSX_STOR x)
#define RTSX_DEBUGPN(x...) pr_debug(x)
#define RTSX_DEBUGPX(x...) printk(x)
#define RTSX_DEBUG(x) x
#else
#define RTSX_DEBUGP(x...)
#define RTSX_DEBUGPN(x...)
#define RTSX_DEBUGPX(x...)
#define RTSX_DEBUG(x)
#endif

#endif   /* __REALTEK_RTSX_DEBUG_H */
