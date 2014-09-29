/*
 * AMLOGIC Audio/Video streaming port driver.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the named License,
 * or any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307, USA
 *
 * Author:  Tim Yao <timyao@amlogic.com>
 *
 */

#ifndef AMVDEC_H
#define AMVDEC_H
#include "amports_config.h"

#include <mach/cpu.h>

#define UCODE_ALIGN         8
#define UCODE_ALIGN_MASK    7UL

typedef struct amvdec_dec_reg_s {
    unsigned long mem_start;
    unsigned long mem_end;
    struct device *cma_dev;
    struct dec_sysinfo *dec_sysinfo;
} amvdec_dec_reg_t;

extern  s32 amvdec_loadmc(const u32 *p);
extern void amvdec_start(void);
extern void amvdec_stop(void);
extern void amvdec_enable(void);
extern void amvdec_disable(void);
s32 amvdec_loadmc_ex(const char*name,char *def);

#if MESON_CPU_TYPE >= MESON_CPU_TYPE_MESON6
extern  s32 amvdec2_loadmc(const u32 *p);
extern void amvdec2_start(void);
extern void amvdec2_stop(void);
extern void amvdec2_enable(void);
extern void amvdec2_disable(void);
#endif

#if MESON_CPU_TYPE >= MESON_CPU_TYPE_MESON8
extern  s32 amhevc_loadmc(const u32 *p);
extern void amhevc_start(void);
extern void amhevc_stop(void);
extern void amhevc_enable(void);
extern void amhevc_disable(void);
#endif

#if HAS_HDEC
extern s32 amhcodec_loadmc(const u32 *p);
extern void amhcodec_start(void);
extern void amhcodec_stop(void);
#endif

extern int amvdev_pause(void);
extern int amvdev_resume(void);

#ifdef CONFIG_PM
extern int amvdec_suspend(struct platform_device *dev, pm_message_t event);
extern int amvdec_resume(struct platform_device *dec);
#endif

#if MESON_CPU_TYPE >= MESON_CPU_TYPE_MESON6
#define AMVDEC_CLK_GATE_ON(a)
#define AMVDEC_CLK_GATE_OFF(a)
#else
#define AMVDEC_CLK_GATE_ON(a) CLK_GATE_ON(a)
#define AMVDEC_CLK_GATE_OFF(a) CLK_GATE_OFF(a)
#endif

#if MESON_CPU_TYPE >= MESON_CPU_TYPE_MESON6
// TODO: move to register headers
#define RESET_VCPU          (1<<7)
#define RESET_CCPU          (1<<8)
#endif

#endif /* AMVDEC_H */
