/*
 * CSI Driver
 *
 * Author: Xintan Chen <lin.xu@amlogic.com>
 *
 * Copyright (C) 2010 Amlogic Inc.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */


#ifndef __CSI_INPUT_H
#define __CSI_INPUT_H
#include <linux/cdev.h>
#include <mach/pinmux.h>
#include <linux/amlogic/tvin/tvin_v4l2.h>
#include <linux/amlogic/mipi/am_mipi_csi2.h>
#include "../tvin_frontend.h"
#include "../tvin_global.h"
#ifdef PRINT_DEBUG_INFO
#define DPRINT(...)		printk(__VA_ARGS__)
#else
#define DPRINT(...)
#endif

enum amcsi_status_e{
        TVIN_AMCSI_STOP,
        TVIN_AMCSI_RUNNING,
        TVIN_AMCSI_START,
};

typedef struct amcsi_dev_s{
        int                     index;
        dev_t                   devt;           
        struct cdev             cdev;
        struct device          *dev;
        unsigned int            overflow_cnt;
        enum amcsi_status_e     dec_status;
        struct vdin_parm_s      para;
        csi_parm_t              csi_parm;
        unsigned char           reset;
        unsigned int            reset_count;
        unsigned int            irq_num;
        struct tvin_frontend_s  frontend; 
        unsigned int            period;
        unsigned int            min_frmrate;
        struct timer_list       t;
}amcsi_dev_t;
//unsigned long msecs_to_jiffies(const unsigned int m);
#endif
