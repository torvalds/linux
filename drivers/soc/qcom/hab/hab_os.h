/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2019-2021, The Linux Foundation. All rights reserved.
 * Copyright (c) 2022-2024, Qualcomm Innovation Center, Inc. All rights reserved.
 */
#ifndef __HAB_OS_H
#define __HAB_OS_H

#ifdef pr_fmt
#undef pr_fmt
#endif
#define pr_fmt(fmt) "hab:%s:%d " fmt, __func__, __LINE__

#include <linux/types.h>

#include <linux/habmm.h>
#include <linux/hab_ioctl.h>

#include <linux/kernel.h>
#include <linux/interrupt.h>
#include <linux/device.h>
#include <linux/fs.h>
#include <linux/mm.h>
#include <linux/vmalloc.h>
#include <linux/slab.h>
#include <linux/kthread.h>
#include <linux/sched.h>
#include <linux/cdev.h>
#include <linux/list.h>
#include <linux/spinlock.h>
#include <linux/rbtree.h>
#include <linux/idr.h>
#include <linux/module.h>
#include <linux/uaccess.h>
#include <linux/dma-map-ops.h>
#include <linux/jiffies.h>
#include <linux/reboot.h>
#include <linux/kobject.h>
#include <linux/sysfs.h>
#include <linux/delay.h>
#include <linux/version.h>
#include <linux/devcoredump.h>

void hab_rb_init(struct rb_root *root);

#define hab_rb_remove(root, pos) rb_erase(&(pos)->node, root)
#define hab_rb_min(root, type, node) rb_entry_safe(rb_first(root), type, node)
#define hab_rb_max(root, type, node) rb_entry_safe(rb_last(root), type, node)
#define hab_rb_for_each_entry(pos, n, head, member)	\
	rbtree_postorder_for_each_entry_safe(pos, n, head, member)
#define HAB_RB_ENTRY struct rb_node
#define HAB_RB_ROOT struct rb_root

#if defined(CONFIG_MSM_VHOST_HAB) || defined(CONFIG_MSM_VIRTIO_HAB)
#include <asm/arch_timer.h>
static inline unsigned long long msm_timer_get_sclk_ticks(void)
{
	return __arch_counter_get_cntpct();
}
#elif IS_ENABLED(CONFIG_MSM_BOOT_TIME_MARKER)
#include <soc/qcom/boot_stats.h>
#else
static inline unsigned long long msm_timer_get_sclk_ticks(void)
{
	return 0;
}
#endif

#endif /*__HAB_OS_H*/
