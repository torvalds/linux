/*
 *      Intel_SCU 0.2:  An Intel SCU IOH Based Watchdog Device
 *			for Intel part #(s):
 *				- AF82MP20 PCH
 *
 *      Copyright (C) 2009-2010 Intel Corporation. All rights reserved.
 *
 *      This program is free software; you can redistribute it and/or
 *      modify it under the terms of version 2 of the GNU General
 *      Public License as published by the Free Software Foundation.
 *
 *      This program is distributed in the hope that it will be
 *      useful, but WITHOUT ANY WARRANTY; without even the implied
 *      warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
 *      PURPOSE.  See the GNU General Public License for more details.
 *      You should have received a copy of the GNU General Public
 *      License along with this program; if not, write to the Free
 *      Software Foundation, Inc., 59 Temple Place - Suite 330,
 *      Boston, MA  02111-1307, USA.
 *      The full GNU General Public License is included in this
 *      distribution in the file called COPYING.
 *
 */

#ifndef __INTEL_SCU_WATCHDOG_H
#define __INTEL_SCU_WATCHDOG_H

#define PFX "Intel_SCU: "
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
