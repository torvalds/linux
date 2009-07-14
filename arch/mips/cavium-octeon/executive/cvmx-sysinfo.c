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
#include <asm/octeon/cvmx-spinlock.h>
#include <asm/octeon/cvmx-sysinfo.h>

/**
 * This structure defines the private state maintained by sysinfo module.
 *
 */
static struct {
	struct cvmx_sysinfo sysinfo;	   /* system information */
	cvmx_spinlock_t lock;	   /* mutex spinlock */

} state = {
	.lock = CVMX_SPINLOCK_UNLOCKED_INITIALIZER
};


/*
 * Global variables that define the min/max of the memory region set
 * up for 32 bit userspace access.
 */
uint64_t linux_mem32_min;
uint64_t linux_mem32_max;
uint64_t linux_mem32_wired;
uint64_t linux_mem32_offset;

/**
 * This function returns the application information as obtained
 * by the bootloader.  This provides the core mask of the cores
 * running the same application image, as well as the physical
 * memory regions available to the core.
 *
 * Returns  Pointer to the boot information structure
 *
 */
struct cvmx_sysinfo *cvmx_sysinfo_get(void)
{
	return &(state.sysinfo);
}
EXPORT_SYMBOL(cvmx_sysinfo_get);

/**
 * This function is used in non-simple executive environments (such as
 * Linux kernel, u-boot, etc.)  to configure the minimal fields that
 * are required to use simple executive files directly.
 *
 * Locking (if required) must be handled outside of this
 * function
 *
 * @phy_mem_desc_ptr:
 *                   Pointer to global physical memory descriptor
 *                   (bootmem descriptor) @board_type: Octeon board
 *                   type enumeration
 *
 * @board_rev_major:
 *                   Board major revision
 * @board_rev_minor:
 *                   Board minor revision
 * @cpu_clock_hz:
 *                   CPU clock freqency in hertz
 *
 * Returns 0: Failure
 *         1: success
 */
int cvmx_sysinfo_minimal_initialize(void *phy_mem_desc_ptr,
				    uint16_t board_type,
				    uint8_t board_rev_major,
				    uint8_t board_rev_minor,
				    uint32_t cpu_clock_hz)
{

	/* The sysinfo structure was already initialized */
	if (state.sysinfo.board_type)
		return 0;

	memset(&(state.sysinfo), 0x0, sizeof(state.sysinfo));
	state.sysinfo.phy_mem_desc_ptr = phy_mem_desc_ptr;
	state.sysinfo.board_type = board_type;
	state.sysinfo.board_rev_major = board_rev_major;
	state.sysinfo.board_rev_minor = board_rev_minor;
	state.sysinfo.cpu_clock_hz = cpu_clock_hz;

	return 1;
}

