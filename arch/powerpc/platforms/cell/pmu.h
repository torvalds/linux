/*
 * Cell Broadband Engine Performance Monitor
 *
 * (C) Copyright IBM Corporation 2001,2006
 *
 * Author:
 *   David Erb (djerb@us.ibm.com)
 *   Kevin Corry (kevcorry@us.ibm.com)
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2, or (at your option)
 * any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */

#ifndef __PERFMON_H__
#define __PERFMON_H__

enum pm_reg_name {
	group_control,
	debug_bus_control,
	trace_address,
	ext_tr_timer,
	pm_status,
	pm_control,
	pm_interval,
	pm_start_stop,
};

extern u32  cbe_read_phys_ctr(u32 cpu, u32 phys_ctr);
extern void cbe_write_phys_ctr(u32 cpu, u32 phys_ctr, u32 val);
extern u32  cbe_read_ctr(u32 cpu, u32 ctr);
extern void cbe_write_ctr(u32 cpu, u32 ctr, u32 val);

extern u32  cbe_read_pm07_control(u32 cpu, u32 ctr);
extern void cbe_write_pm07_control(u32 cpu, u32 ctr, u32 val);
extern u32  cbe_read_pm (u32 cpu, enum pm_reg_name reg);
extern void cbe_write_pm (u32 cpu, enum pm_reg_name reg, u32 val);

extern u32  cbe_get_ctr_size(u32 cpu, u32 phys_ctr);
extern void cbe_set_ctr_size(u32 cpu, u32 phys_ctr, u32 ctr_size);

extern void cbe_enable_pm(u32 cpu);
extern void cbe_disable_pm(u32 cpu);

extern void cbe_read_trace_buffer(u32 cpu, u64 *buf);

#endif
