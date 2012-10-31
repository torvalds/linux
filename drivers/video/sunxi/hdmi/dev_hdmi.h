/*
 * Copyright (C) 2007-2012 Allwinner Technology Co., Ltd.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of
 * the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.	 See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston,
 * MA 02111-1307 USA
 */

#ifndef __DEV_DISPLAY_H__
#define __DEV_DISPLAY_H__

#include "drv_hdmi_i.h"

int hdmi_open(struct inode *inode, struct file *file);
int hdmi_release(struct inode *inode, struct file *file);
ssize_t hdmi_read(struct file *file, char __user *buf, size_t count,
		  loff_t *ppos);
ssize_t hdmi_write(struct file *file, const char __user *buf, size_t count,
		   loff_t *ppos);
int hdmi_mmap(struct file *file, struct vm_area_struct *vma);
long hdmi_ioctl(struct file *file, unsigned int cmd, unsigned long arg);

extern __s32 Hdmi_init(void);
extern __s32 Hdmi_exit(void);
extern __s32 Fb_Init(__u32 from);

typedef struct {
	__bool bopen;
	__disp_tv_mode_t mode;
	__u32 base_hdmi;
} hdmi_info_t;

extern hdmi_info_t ghdmi;

#endif
