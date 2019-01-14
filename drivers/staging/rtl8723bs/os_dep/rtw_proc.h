/* SPDX-License-Identifier: GPL-2.0 */
/******************************************************************************
 *
 * Copyright(c) 2007 - 2013 Realtek Corporation. All rights reserved.
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

#ifdef PROC_DEBUG

int rtw_drv_proc_init(void);
void rtw_drv_proc_deinit(void);
struct proc_dir_entry *rtw_adapter_proc_init(struct net_device *dev);
void rtw_adapter_proc_deinit(struct net_device *dev);
void rtw_adapter_proc_replace(struct net_device *dev);

#else //!PROC_DEBUG

static inline int rtw_drv_proc_init(void) {return 0;}
static inline void rtw_drv_proc_deinit(void) {}
static inline struct proc_dir_entry *rtw_adapter_proc_init(struct net_device *dev){return NULL;}
static inline void rtw_adapter_proc_deinit(struct net_device *dev){}
static inline void rtw_adapter_proc_replace(struct net_device *dev){}

#endif //!PROC_DEBUG

#endif //__RTW_PROC_H__
