/*
 * Populate sysfs with topology information
 *
 * Written by: Matthew Dobson, IBM Corporation
 * Original Code: Paul Dorwin, IBM Corporation, Patrick Mochel, OSDL
 *
 * Copyright (C) 2002, IBM Corp.
 *
 * All rights reserved.
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
 * Send feedback to <colpatch@us.ibm.com>
 */
#include <linux/interrupt.h>
#include <linux/nodemask.h>
#include <linux/export.h>
#include <linux/mmzone.h>
#include <linux/init.h>
#include <linux/smp.h>
#include <linux/irq.h>
#include <asm/io_apic.h>
#include <asm/cpu.h>

#ifdef CONFIG_HOTPLUG_CPU
bool arch_cpu_is_hotpluggable(int cpu)
{
	return cpu > 0;
}
#endif /* CONFIG_HOTPLUG_CPU */
