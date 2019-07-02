/* SPDX-License-Identifier: GPL-2.0-only */
/*
 *      Intel_SCU 0.2:  An Intel SCU IOH Based Watchdog Device
 *			for Intel part #(s):
 *				- AF82MP20 PCH
 *
 *      Copyright (C) 2009-2010 Intel Corporation. All rights reserved.
 */

#ifndef __INTEL_SCU_WATCHDOG_H
#define __INTEL_SCU_WATCHDOG_H

#define WDT_VER "0.3"

/* minimum time between interrupts */
#define MIN_TIME_CYCLE 1

/* Time from warning to reboot is 2 seconds */
#define DEFAULT_SOFT_TO_HARD_MARGIN 2

#define MAX_TIME 170

#define DEFAULT_TIME 5

#define MAX_SOFT_TO_HARD_MARGIN (MAX_TIME-MIN_TIME_CYCLE)

/* Ajustment to clock tick frequency to make timing come out right */
#define FREQ_ADJUSTMENT 8

struct intel_scu_watchdog_dev {
	ulong driver_open;
	ulong driver_closed;
	u32 timer_started;
	u32 timer_set;
	u32 threshold;
	u32 soft_threshold;
	u32 __iomem *timer_load_count_addr;
	u32 __iomem *timer_current_value_addr;
	u32 __iomem *timer_control_addr;
	u32 __iomem *timer_clear_interrupt_addr;
	u32 __iomem *timer_interrupt_status_addr;
	struct sfi_timer_table_entry *timer_tbl_ptr;
	struct notifier_block intel_scu_notifier;
	struct miscdevice miscdev;
};

extern int sfi_mtimer_num;

/* extern struct sfi_timer_table_entry *sfi_get_mtmr(int hint); */
#endif /* __INTEL_SCU_WATCHDOG_H */
