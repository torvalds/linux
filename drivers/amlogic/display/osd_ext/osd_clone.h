/*
 * Amlogic OSD Driver
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

#ifndef _OSD_CLONE_H_
#define _OSD_CLONE_H_

#ifdef CONFIG_AM_GE2D
#define OSD_EXT_GE2D_CLONE_SUPPORT 1
#endif

#ifdef OSD_EXT_GE2D_CLONE_SUPPORT
extern void osd_ext_clone_set_angle(int angle);
extern void osd_ext_clone_update_pan(int pan);
extern int osd_ext_clone_task_start(void);
extern void osd_ext_clone_task_stop(void);
#else
static inline void osd_ext_clone_set_angle(int angle) {}
static inline void osd_ext_clone_update_pan(int pan) {}
static inline int osd_ext_clone_task_start(void)
{
	printk("++ osd_ext_clone depends on GE2D module!\n");
	return 0;
}
static inline void osd_ext_clone_task_stop(void)
{
	printk("-- osd_ext_clone depends on GE2D module!\n");
	return;
}
#endif

#endif
