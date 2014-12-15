/*
 * Amlogic Ethernet Driver
 *
 * Copyright (C) 2012 Amlogic, Inc.
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
 * Author: Platform-BJ@amlogic.com
 *
 */

#include <linux/kernel.h>
#include <linux/types.h>
#include <linux/errno.h>
#include <linux/interrupt.h>
#include <linux/timer.h>
#include <linux/amlogic/amports/canvas.h>
#include <linux/amlogic/ge2d/ge2d_main.h>
#include <linux/amlogic/ge2d/ge2d.h>
#include <linux/kthread.h>
#include <linux/delay.h>
#include <linux/semaphore.h>
#include <linux/sched.h>
#include <linux/amlogic/amlog.h>
#include <linux/amlogic/vout/vout_notify.h>
#include <linux/amlogic/vout/vinfo.h>

#include <mach/am_regs.h>
#include <mach/mod_gate.h>

#include "osd_clone.h"

#ifdef OSD_EXT_GE2D_CLONE_SUPPORT
typedef struct {
	bool inited;
	int angle;
	int pan;
	config_para_ex_t ge2d_config;
	ge2d_context_t *ge2d_context;
} osd_ext_clone_t;

static DEFINE_MUTEX(osd_ext_clone_mutex);
static osd_ext_clone_t s_osd_ext_clone;


static void osd_clone_process(void)
{
	canvas_t cs, cd;
	u32 x0 = 0;
	u32 y0 = 0;
	u32 y1 = 0;
	unsigned char x_rev = 0;
	unsigned char y_rev = 0;
	unsigned char xy_swap = 0;
	config_para_ex_t *ge2d_config = &s_osd_ext_clone.ge2d_config;
	ge2d_context_t *context = s_osd_ext_clone.ge2d_context;

	canvas_read(OSD1_CANVAS_INDEX, &cs);
	canvas_read(OSD3_CANVAS_INDEX, &cd);

	if (s_osd_ext_clone.pan == 1) {
		y0 = cs.height/2;
		y1 = cd.height/2;
	}

	if (s_osd_ext_clone.angle == 1) {
		xy_swap = 1;
		x_rev = 1;
	} else if (s_osd_ext_clone.angle == 2) {
		x_rev = 1;
		y_rev = 1;
	} else if (s_osd_ext_clone.angle == 3) {
		xy_swap = 1;
		y_rev = 1;
	}

	memset(ge2d_config, 0, sizeof(config_para_ex_t));
	ge2d_config->alu_const_color = 0;
	ge2d_config->bitmask_en = 0;
	ge2d_config->src1_gb_alpha = 0;
	ge2d_config->dst_xy_swap = 0;

	ge2d_config->src_planes[0].addr = cs.addr;
	ge2d_config->src_planes[0].w = cs.width/4;
	ge2d_config->src_planes[0].h = cs.height;

	ge2d_config->dst_planes[0].addr = cd.addr;
	ge2d_config->dst_planes[0].w = cd.width/4;
	ge2d_config->dst_planes[0].h = cd.height;

	ge2d_config->src_para.canvas_index = OSD1_CANVAS_INDEX;
	ge2d_config->src_para.mem_type = CANVAS_OSD0;
	ge2d_config->dst_para.mem_type = CANVAS_TYPE_INVALID;
	ge2d_config->dst_para.format = GE2D_FORMAT_S32_ARGB;
	ge2d_config->src_para.fill_color_en = 0;
	ge2d_config->src_para.fill_mode = 0;
	ge2d_config->src_para.x_rev = 0;
	ge2d_config->src_para.y_rev = 0;
	ge2d_config->src_para.color = 0xffffffff;
	ge2d_config->src_para.top = 0;
	ge2d_config->src_para.left = 0;
	ge2d_config->src_para.width = cs.width/4;
	ge2d_config->src_para.height = cs.height;

	ge2d_config->dst_para.canvas_index = OSD3_CANVAS_INDEX;
	ge2d_config->dst_para.mem_type = CANVAS_TYPE_INVALID;
	ge2d_config->dst_para.format = GE2D_FORMAT_S32_ARGB;
	ge2d_config->dst_para.top = 0;
	ge2d_config->dst_para.left = 0;
	ge2d_config->dst_para.width = cd.width/4;
	ge2d_config->dst_para.height = cd.height;
	ge2d_config->dst_para.fill_color_en = 0;
	ge2d_config->dst_para.fill_mode = 0;
	ge2d_config->dst_para.color = 0;
	ge2d_config->dst_para.x_rev = x_rev;
	ge2d_config->dst_para.y_rev = y_rev;
	ge2d_config->dst_xy_swap = xy_swap;

	if (ge2d_context_config_ex(context, ge2d_config) < 0) {
		printk("++ osd clone ge2d config error.\n");
		return;
	}
	stretchblt(context, x0, y0, cs.width/4, cs.height/2, x0, y1, cd.width/4, cd.height/2);
}

void osd_ext_clone_update_pan(int pan)
{
	if (!s_osd_ext_clone.inited)
		return;

	mutex_lock(&osd_ext_clone_mutex);
	s_osd_ext_clone.pan = pan;
	mutex_unlock(&osd_ext_clone_mutex);
	osd_clone_process();
}

void osd_ext_clone_set_angle(int angle)
{
	mutex_lock(&osd_ext_clone_mutex);
	s_osd_ext_clone.angle = angle;
	mutex_unlock(&osd_ext_clone_mutex);
}

int osd_ext_clone_task_start(void)
{
	if (s_osd_ext_clone.inited) {
		printk("osd_ext_clone_task already started.\n");
		return 0;
	}

	printk("osd_ext_clone_task start.\n");
#if MESON_CPU_TYPE >= MESON_CPU_TYPE_MESON6
	switch_mod_gate_by_name("ge2d", 1);
#endif
	if (s_osd_ext_clone.ge2d_context == NULL)
		s_osd_ext_clone.ge2d_context = create_ge2d_work_queue();

	memset(&s_osd_ext_clone.ge2d_config, 0, sizeof(config_para_ex_t));
	s_osd_ext_clone.inited = true;

	return 0;
}

void osd_ext_clone_task_stop(void)
{
	if (!s_osd_ext_clone.inited) {
		printk("osd_ext_clone_task already stopped.\n");
		return;
	}

	printk("osd_ext_clone_task stop.\n");
#if MESON_CPU_TYPE >= MESON_CPU_TYPE_MESON6
	switch_mod_gate_by_name("ge2d", 0);
#endif
	if (s_osd_ext_clone.ge2d_context) {
		destroy_ge2d_work_queue(s_osd_ext_clone.ge2d_context);
		s_osd_ext_clone.ge2d_context = NULL;
	}
	s_osd_ext_clone.inited = false;
}
#endif
