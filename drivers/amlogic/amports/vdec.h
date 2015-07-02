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

#ifndef VDEC_H
#define VDEC_H
#include <mach/cpu.h>
#include "amports_config.h"

#include <linux/platform_device.h>

#include <mach/cpu.h>

typedef struct vdec_dev_reg_s {
    unsigned long mem_start;
    unsigned long mem_end;
    struct device *cma_dev;
    struct dec_sysinfo *sys_info;
} vdec_dev_reg_t;

extern void vdec_set_decinfo(struct dec_sysinfo *p);
extern int vdec_set_resource(unsigned long start, unsigned long end, struct device *p);

extern s32 vdec_init(vformat_t vf);
extern s32 vdec_release(vformat_t vf);

s32 vdec_dev_register(void);
s32 vdec_dev_unregister(void);
void vdec_power_mode(int level);

typedef enum {
    VDEC_1 = 0,
    VDEC_HCODEC,
    VDEC_2,
    VDEC_HEVC,
    VDEC_MAX
} vdec_type_t;

#if MESON_CPU_TYPE >= MESON_CPU_TYPE_MESON6TVD
extern void vdec2_power_mode(int level);
extern void vdec_poweron(vdec_type_t core);
extern void vdec_poweroff(vdec_type_t core);
extern bool vdec_on(vdec_type_t core);
#else
#define vdec_poweron(core)
#define vdec_poweroff(core)
#endif

#if MESON_CPU_TYPE >= MESON_CPU_TYPE_MESON6TVD
typedef enum {
    USAGE_NONE,
    USAGE_DEC_4K2K,
    USAGE_ENCODE,
} vdec2_usage_t;

extern void set_vdec2_usage(vdec2_usage_t usage);
extern vdec2_usage_t get_vdec2_usage(void);
#endif
#endif /* VDEC_H */
