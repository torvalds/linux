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
 *
 * File: kcompat.h
 *
 * Purpose: define kernel compatibility header
 *
 * Author: Lyndon Chen
 *
 * Date: Apr 8, 2002
 *
 */
#ifndef _KCOMPAT_H
#define _KCOMPAT_H

#include <linux/version.h>

#ifndef __init
#define __init
#endif

#ifndef __exit
#define __exit
#endif

#ifndef __devexit
#define __devexit
#endif

#ifndef __devinitdata
#define __devinitdata
#endif

#ifndef MODULE_LICENSE
#define MODULE_LICENSE(license)
#endif

#ifndef MOD_INC_USE_COUNT
#define MOD_INC_USE_COUNT do {} while (0)
#endif

#ifndef MOD_DEC_USE_COUNT
#define MOD_DEC_USE_COUNT do {} while (0)
#endif

#ifndef HAVE_NETDEV_PRIV
#define netdev_priv(dev) (dev->priv)
#endif

#ifndef IRQ_RETVAL
typedef void irqreturn_t;

#ifdef PRIVATE_OBJ
#define IRQ_RETVAL(x)   (int)x
#else
#define IRQ_RETVAL(x)
#endif

#endif


#ifndef MODULE_LICESEN
#define MODULE_LICESEN(x)
#endif


#endif

