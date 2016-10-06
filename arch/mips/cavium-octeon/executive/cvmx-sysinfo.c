/***********************license start***************
 * Author: Cavium Networks
 *
 * Contact: support@caviumnetworks.com
 * This file is part of the OCTEON SDK
 *
 * Copyright (c) 2003-2008 Cavium Networks
 *
 * This file is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License, Version 2, as
 * published by the Free Software Foundation.
 *
 * This file is distributed in the hope that it will be useful, but
 * AS-IS and WITHOUT ANY WARRANTY; without even the implied warranty
 * of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE, TITLE, or
 * NONINFRINGEMENT.  See the GNU General Public License for more
 * details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this file; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301 USA
 * or visit http://www.gnu.org/licenses/.
 *
 * This file may also be available under a different license from Cavium.
 * Contact Cavium Networks for more information
 ***********************license end**************************************/

/*
 * This module provides system/board/application information obtained
 * by the bootloader.
 */
#include <linux/module.h>

#include <asm/octeon/cvmx.h>
#include <asm/octeon/cvmx-sysinfo.h>

/*
 * This structure defines the private state maintained by sysinfo module.
 */
static struct cvmx_sysinfo sysinfo;	   /* system information */

/*
 * Returns the application information as obtained
 * by the bootloader.  This provides the core mask of the cores
 * running the same application image, as well as the physical
 * memory regions available to the core.
 */
struct cvmx_sysinfo *cvmx_sysinfo_get(void)
{
	return &sysinfo;
}
EXPORT_SYMBOL(cvmx_sysinfo_get);

