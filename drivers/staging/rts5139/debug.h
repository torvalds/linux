/* Driver for Realtek RTS51xx USB card reader
 * Header file
 *
 * Copyright(c) 2009 Realtek Semiconductor Corp. All rights reserved.
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
 *   wwang (wei_wang@realsil.com.cn)
 *   No. 450, Shenhu Road, Suzhou Industry Park, Suzhou, China
 * Maintainer:
 *   Edwin Rong (edwin_rong@realsil.com.cn)
 *   No. 450, Shenhu Road, Suzhou Industry Park, Suzhou, China
 */

#ifndef __RTS51X_DEBUG_H
#define __RTS51X_DEBUG_H

#include <linux/kernel.h>

#define RTS51X_TIP "rts51x: "

#ifdef CONFIG_RTS5139_DEBUG
#define RTS51X_DEBUGP(x...) printk(KERN_DEBUG RTS51X_TIP x)
#define RTS51X_DEBUGPN(x...) printk(KERN_DEBUG x)
#define RTS51X_DEBUGPX(x...) printk(x)
#define RTS51X_DEBUG(x) x
#else
#define RTS51X_DEBUGP(x...)
#define RTS51X_DEBUGPN(x...)
#define RTS51X_DEBUGPX(x...)
#define RTS51X_DEBUG(x)
#endif

#endif /* __RTS51X_DEBUG_H */
