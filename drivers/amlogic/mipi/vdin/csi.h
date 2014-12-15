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


#ifndef __CSI_INPUT_H
#define __CSI_INPUT_H
#include <linux/cdev.h>
#include <linux/tvin/tvin_v4l2.h>
#include <mach/pinmux.h>
#include "../../tvin/tvin_frontend.h"
#include "../../tvin/tvin_global.h"

enum amcsi_status_e{
        TVIN_AMCSI_STOP,
        TVIN_AMCSI_RUNNING,
        TVIN_AMCSI_START,
};
typedef struct csi_parm_s {
        unsigned int            skip_frames;
        tvin_color_fmt_t        csi_ofmt;
}csi_parm_t;

typedef struct amcsi_dev_s{
        int                     index;
        dev_t                   devt;           
        struct cdev             cdev;
        struct device          *dev;
        unsigned int            overflow_cnt;
        enum amcsi_status_e     dec_status;
        struct vdin_parm_s      para;
	csi_parm_t              csi_parm;
        struct tvin_frontend_s  frontend; 
}amcsi_dev_t;
#endif
