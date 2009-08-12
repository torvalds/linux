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
 * File: device_cfg.h
 *
 * Purpose: Driver configuration header
 * Author: Lyndon Chen
 *
 * Date: Dec 9, 2005
 *
 */
#ifndef __DEVICE_CONFIG_H
#define __DEVICE_CONFIG_H

#include <linux/types.h>

#include "ttype.h"

typedef
struct _version {
    unsigned char   major;
    unsigned char   minor;
    unsigned char   build;
} version_t, *pversion_t;

#ifndef FALSE
#define FALSE   (0)
#endif

#ifndef TRUE
#define TRUE    (!(FALSE))
#endif

#define VID_TABLE_SIZE      64
#define MCAST_TABLE_SIZE    64
#define MCAM_SIZE           32
#define VCAM_SIZE           32
#define TX_QUEUE_NO         8

#define DEVICE_NAME         "vt6656"
#define DEVICE_FULL_DRV_NAM "VIA Networking Wireless LAN USB Driver"

#ifndef MAJOR_VERSION
#define MAJOR_VERSION       1
#endif

#ifndef MINOR_VERSION
#define MINOR_VERSION       13
#endif

#ifndef DEVICE_VERSION
#define DEVICE_VERSION       "1.19_12"
#endif

//config file
#include <linux/fs.h>
#include <linux/fcntl.h>
#ifndef CONFIG_PATH
#define CONFIG_PATH            "/etc/vntconfiguration.dat"
#endif

//Max: 2378=2312Payload + 30HD +4CRC + 2Padding + 4Len + 8TSF + 4RSR
#define PKT_BUF_SZ          2390


#define MAX_UINTS           8
#define OPTION_DEFAULT      { [0 ... MAX_UINTS-1] = -1}



typedef enum  _chip_type{
    VT3184=1
} CHIP_TYPE, *PCHIP_TYPE;



#ifdef VIAWET_DEBUG
#define ASSERT(x) { \
    if (!(x)) { \
        printk(KERN_ERR "assertion %s failed: file %s line %d\n", #x,\
        __FUNCTION__, __LINE__);\
        *(int*) 0=0;\
    }\
}
#define DBG_PORT80(value)                   outb(value, 0x80)
#else
#define ASSERT(x)
#define DBG_PORT80(value)
#endif


#endif
