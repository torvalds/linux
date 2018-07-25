/*
 * Copyright (c) 2015 South Silicon Valley Microelectronics Inc.
 * Copyright (c) 2015 iComm Corporation
 *
 * This program is free software: you can redistribute it and/or modify 
 * it under the terms of the GNU General Public License as published by 
 * the Free Software Foundation, either version 3 of the License, or 
 * (at your option) any later version.
 * This program is distributed in the hope that it will be useful, but 
 * WITHOUT ANY WARRANTY; without even the implied warranty of 
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  
 * See the GNU General Public License for more details.
 * You should have received a copy of the GNU General Public License 
 * along with this program. If not, see <http://www.gnu.org/licenses/>.
 */

#ifndef SSVSDIOBRIDGE_H
#define SSVSDIOBRIDGE_H 
#include <linux/etherdevice.h>
#include <linux/device.h>
#include <linux/interrupt.h>
#include <linux/leds.h>
#include <linux/completion.h>
#include <linux/ioctl.h>
#include <linux/types.h>
#include "sdiobridge_pub.h"
struct ssv_sdiobridge_glue
{
    struct device *dev;
 u8 blockMode;
 u16 blockSize;
 u8 autoAckInt;
    unsigned int dataIOPort;
    unsigned int regIOPort;
    u8 funcFocus;
    atomic_t irq_handling;
    wait_queue_head_t irq_wq;
    wait_queue_head_t read_wq;
    spinlock_t rxbuflock;
    void *bufaddr;
 struct list_head rxbuf;
    struct list_head rxreadybuf;
    struct dentry *debugfs;
    struct dentry *dump_entry;
    u32 dump;
};
#define MANUFACTURER_SSV_CODE 0x3030
#define MANUFACTURER_ID_CABRIO_BASE 0x3030
#endif
