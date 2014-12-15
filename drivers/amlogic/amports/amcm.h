/*
 * Color Management
 *
 * Author: Lin Xu <lin.xu@amlogic.com>
 *         Bobby Yang <bo.yang@amlogic.com>
 *
 * Copyright (C) 2010 Amlogic Inc.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef __AM_CM_H
#define __AM_CM_H


#include "linux/amlogic/amports/vframe.h"
#include "linux/amlogic/amports/cm.h"

typedef struct cm_regs_s {
    unsigned int val  : 32;
    unsigned int reg  : 14;
    unsigned int port :  2; // port port_addr            port_data            remark
                        // 0    NA                   NA                   direct access
                        // 1    VPP_CHROMA_ADDR_PORT VPP_CHROMA_DATA_PORT CM port registers
                        // 2    NA                   NA                   reserved
                        // 3    NA                   NA                   reserved
    unsigned int bit  :  5;
    unsigned int wid  :  5;
    unsigned int mode :  1; // 0:read, 1:write
    unsigned int rsv  :  5;
} cm_regs_t;


// ***************************************************************************
// *** IOCTL-oriented functions *********************************************
// ***************************************************************************

void cm_set_region(struct cm_region_s *p);
void cm_set_top(struct cm_top_s *p);
void cm_set_demo(struct cm_demo_s *p);

void cm_set_regs(struct cm_regs_s *p);

#endif

