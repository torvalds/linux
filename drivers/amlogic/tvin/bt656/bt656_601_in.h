/*
 * BT656IN Driver
 *
 * Author: Xintan Chen <lin.xu@amlogic.com>
 *
 * Copyright (C) 2010 Amlogic Inc.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */


#ifndef __BT656_601_INPUT_H
#define __BT656_601_INPUT_H
#include <linux/cdev.h>
#include <linux/amlogic/tvin/tvin_v4l2.h>
#include <mach/pinmux.h>
#include "../tvin_frontend.h"
#include "../tvin_global.h"

#if MESON_CPU_TYPE < MESON_CPU_TYPE_MESON8
#define WR(x,val)                         WRITE_CBUS_REG(x,val)
#define WR_BITS(x,val,start,length)       WRITE_CBUS_REG_BITS(x,val,start,length)
#define RD(x)                             READ_CBUS_REG(x)
#define RD_BITS(x,start,length)           READ_CBUS_REG_BITS(x,start,length)
#else
#define WR(x,val)                         WRITE_VCBUS_REG(x,val)
#define WR_BITS(x,val,start,length)       WRITE_VCBUS_REG_BITS(x,val,start,length)
#define RD(x)                             READ_VCBUS_REG(x)
#define RD_BITS(x,start,length)           READ_VCBUS_REG_BITS(x,start,length)
#endif

enum am656_status_e{
        TVIN_AM656_STOP,
        TVIN_AM656_RUNING,
};
typedef struct am656in_dev_s{
        int                     index;
        dev_t                   devt;           
        struct cdev             cdev;
        struct device          *dev;
        unsigned int            overflow_cnt;
        unsigned int            skip_vdin_frame_count;
        enum am656_status_e     dec_status;
        struct vdin_parm_s      para;
        struct tvin_frontend_s  frontend; 
}am656in_dev_t;
#endif

