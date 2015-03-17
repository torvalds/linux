/******************************************************************************
 *
 * Copyright(c) 2007 - 2013 Realtek Corporation. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of version 2 of the GNU General Public License as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110, USA
 *
 *
 ******************************************************************************/
#ifndef __RTW_PROC_H__
#define __RTW_PROC_H__

#include <linux/proc_fs.h>
#include <linux/seq_file.h>

struct rtw_proc_hdl {
	char *name;
	int (*show)(struct seq_file *, void *);
	ssize_t (*write)(struct file *file, const char __user *buffer, size_t count, loff_t *pos, void *data);
};

#ifdef CONFIG_PROC_DEBUG

struct proc_dir_entry *get_rtw_drv_proc(void);
int rtw_drv_proc_init(void);
void rtw_drv_proc_deinit(void);
struct proc_dir_entry *rtw_adapter_proc_init(struct net_device *dev);
void rtw_adapter_proc_deinit(struct net_device *dev);
void rtw_adapter_proc_replace(struct net_device *dev);

#else //!CONFIG_PROC_DEBUG

struct proc_dir_entry *get_rtw_drv_proc(void) {return NULL;}
int rtw_drv_proc_init(void) {return 0;}
void rtw_drv_proc_deinit(void) {}
struct proc_dir_entry *rtw_adapter_proc_init(struct net_device *dev){return NULL;}
void rtw_adapter_proc_deinit(struct net_device *dev){}
void rtw_adapter_proc_replace(struct net_device *dev){}

#endif //!CONFIG_PROC_DEBUG

#endif //__RTW_PROC_H__
