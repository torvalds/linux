/*
 * Video Enhancement
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

#ifndef __AM_VE_H
#define __AM_VE_H

#include "linux/amlogic/vframe.h"
#include "linux/amlogic/ve.h"

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

typedef struct ve_regs_s {
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
} ve_regs_t;


//void ve_on_vs(vframe_t *vf, int* change_notify);

void ve_set_bext(struct ve_bext_s *p);
void ve_set_dnlp(struct ve_dnlp_s *p);
void ve_set_new_dnlp(struct ve_dnlp_table_s *p);
void ve_set_dnlp_2(void);
void ve_set_hsvs(struct ve_hsvs_s *p);
void ve_set_ccor(struct ve_ccor_s *p);
void ve_set_benh(struct ve_benh_s *p);
void ve_set_demo(struct ve_demo_s *p);
void ve_set_regs(struct ve_regs_s *p);
void ve_set_regmap(struct ve_regmap_s *p);
extern struct ve_hist_s video_ve_hist;
extern void ve_enable_dnlp(void);
extern void ve_disable_dnlp(void);

extern void vpp_enable_lcd_gamma_table(void);
extern void vpp_disable_lcd_gamma_table(void);
extern void vpp_set_lcd_gamma_table(u16 *data, u32 rgb_mask);
extern void vpp_set_rgb_ogo(struct tcon_rgb_ogo_s *p);
extern void vd1_brightness_contrast(signed int brightness, signed int contrast);
extern void vpp_phase_lock_on_vs(unsigned int cycle,
                                 unsigned int stamp,
                                 bool         lock50,
                                 unsigned int range_fast,
                                 unsigned int range_slow);

extern unsigned int vecm_latch_flag;
extern unsigned int cm_size;
//#if defined(CONFIG_ARCH_MESON2)
//unsigned long long ve_get_vs_cnt(void);
//#endif

#endif

