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


#include "linux/amlogic/vframe.h"
#include "linux/amlogic/cm.h"

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
#ifdef AMVIDEO_REG_TABLE_DYNAMIC
void am_set_regmap(unsigned int cnt, struct am_reg_s *p);
#else
void am_set_regmap(struct am_regs_s *p);
#endif

#if (MESON_CPU_TYPE >= MESON_CPU_TYPE_MESON8)
#undef WRITE_CBUS_REG
#undef WRITE_CBUS_REG_BITS
#undef READ_CBUS_REG
#undef READ_CBUS_REG_BITS

#define WRITE_CBUS_REG(x,val)				WRITE_VCBUS_REG(x,val)
#define WRITE_CBUS_REG_BITS(x,val,start,length)		WRITE_VCBUS_REG_BITS(x,val,start,length)
#define READ_CBUS_REG(x)				READ_VCBUS_REG(x)
#define READ_CBUS_REG_BITS(x,start,length)		READ_VCBUS_REG_BITS(x,start,length)
#endif

#endif

