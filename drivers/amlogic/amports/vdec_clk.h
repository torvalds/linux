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

#ifndef VDEC_CLK_H
#define VDEC_CLK_H

#include <mach/cpu.h>

extern void vdec_clock_enable(void);
extern void vdec_clock_hi_enable(void);
extern void vdec2_clock_enable(void);
extern void vdec2_clock_hi_enable(void);
extern void hcodec_clock_enable(void);
extern void hevc_clock_enable(void);
extern void hevc_clock_hi_enable(void);

extern void vdec_clock_on(void);
extern void vdec_clock_off(void);
extern void vdec2_clock_on(void);
extern void vdec2_clock_off(void);
extern void hcodec_clock_on(void);
extern void hcodec_clock_off(void);
extern void hevc_clock_on(void);
extern void hevc_clock_off(void);
extern int vdec_clock_level(vdec_type_t core);

extern void vdec_clock_prepare_switch(void);
extern void hevc_clock_prepare_switch(void);

#endif /* VDEC_CLK_H */
