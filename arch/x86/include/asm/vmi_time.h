/*
 * VMI Time wrappers
 *
 * Copyright (C) 2006, VMware, Inc.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY OR FITNESS FOR A PARTICULAR PURPOSE, GOOD TITLE or
 * NON INFRINGEMENT.  See the GNU General Public License for more
 * details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 *
 * Send feedback to dhecht@vmware.com
 *
 */

#ifndef _ASM_X86_VMI_TIME_H
#define _ASM_X86_VMI_TIME_H

/*
 * Raw VMI call indices for timer functions
 */
#define VMI_CALL_GetCycleFrequency	66
#define VMI_CALL_GetCycleCounter	67
#define VMI_CALL_SetAlarm		68
#define VMI_CALL_CancelAlarm		69
#define VMI_CALL_GetWallclockTime	70
#define VMI_CALL_WallclockUpdated	71

/* Cached VMI timer operations */
extern struct vmi_timer_ops {
	u64 (*get_cycle_frequency)(void);
	u64 (*get_cycle_counter)(int);
	u64 (*get_wallclock)(void);
	int (*wallclock_updated)(void);
	void (*set_alarm)(u32 flags, u64 expiry, u64 period);
	void (*cancel_alarm)(u32 flags);
} vmi_timer_ops;

/* Prototypes */
extern void __init vmi_time_init(void);
extern unsigned long vmi_get_wallclock(void);
extern int vmi_set_wallclock(unsigned long now);
extern unsigned long long vmi_sched_clock(void);
extern unsigned long vmi_tsc_khz(void);

#ifdef CONFIG_X86_LOCAL_APIC
extern void __devinit vmi_time_bsp_init(void);
extern void __devinit vmi_time_ap_init(void);
#endif

/*
 * When run under a hypervisor, a vcpu is always in one of three states:
 * running, halted, or ready.  The vcpu is in the 'running' state if it
 * is executing.  When the vcpu executes the halt interface, the vcpu
 * enters the 'halted' state and remains halted until there is some work
 * pending for the vcpu (e.g. an alarm expires, host I/O completes on
 * behalf of virtual I/O).  At this point, the vcpu enters the 'ready'
 * state (waiting for the hypervisor to reschedule it).  Finally, at any
 * time when the vcpu is not in the 'running' state nor the 'halted'
 * state, it is in the 'ready' state.
 *
 * Real time is advances while the vcpu is 'running', 'ready', or
 * 'halted'.  Stolen time is the time in which the vcpu is in the
 * 'ready' state.  Available time is the remaining time -- the vcpu is
 * either 'running' or 'halted'.
 *
 * All three views of time are accessible through the VMI cycle
 * counters.
 */

/* The cycle counters. */
#define VMI_CYCLES_REAL         0
#define VMI_CYCLES_AVAILABLE    1
#define VMI_CYCLES_STOLEN       2

/* The alarm interface 'flags' bits */
#define VMI_ALARM_COUNTERS      2

#define VMI_ALARM_COUNTER_MASK  0x000000ff

#define VMI_ALARM_WIRED_IRQ0    0x00000000
#define VMI_ALARM_WIRED_LVTT    0x00010000

#define VMI_ALARM_IS_ONESHOT    0x00000000
#define VMI_ALARM_IS_PERIODIC   0x00000100

#define CONFIG_VMI_ALARM_HZ	100

#endif /* _ASM_X86_VMI_TIME_H */
