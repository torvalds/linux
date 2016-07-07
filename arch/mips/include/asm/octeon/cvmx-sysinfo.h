/***********************license start***************
 * Author: Cavium Networks
 *
 * Contact: support@caviumnetworks.com
 * This file is part of the OCTEON SDK
 *
 * Copyright (c) 2003-2016 Cavium, Inc.
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
 * This module provides system/board information obtained by the bootloader.
 */

#ifndef __CVMX_SYSINFO_H__
#define __CVMX_SYSINFO_H__

#include "cvmx-coremask.h"

#define OCTEON_SERIAL_LEN 20
/**
 * Structure describing application specific information.
 * __cvmx_app_init() populates this from the cvmx boot descriptor.
 * This structure is private to simple executive applications, so
 * no versioning is required.
 *
 * This structure must be provided with some fields set in order to
 * use simple executive functions in other applications (Linux kernel,
 * u-boot, etc.)  The cvmx_sysinfo_minimal_initialize() function is
 * provided to set the required values in these cases.
 */
struct cvmx_sysinfo {
	/* System wide variables */
	/* installed DRAM in system, in bytes */
	uint64_t system_dram_size;

	/* ptr to memory descriptor block */
	uint64_t phy_mem_desc_addr;

	/* Application image specific variables */
	/* stack top address (virtual) */
	uint64_t stack_top;
	/* heap base address (virtual) */
	uint64_t heap_base;
	/* stack size in bytes */
	uint32_t stack_size;
	/* heap size in bytes */
	uint32_t heap_size;
	/* coremask defining cores running application */
	struct cvmx_coremask core_mask;
	/* Deprecated, use cvmx_coremask_first_core() to select init core */
	uint32_t init_core;

	/* exception base address, as set by bootloader */
	uint64_t exception_base_addr;

	/* cpu clock speed in hz */
	uint32_t cpu_clock_hz;

	/* dram data rate in hz (data rate = 2 * clock rate */
	uint32_t dram_data_rate_hz;


	uint16_t board_type;
	uint8_t board_rev_major;
	uint8_t board_rev_minor;
	uint8_t mac_addr_base[6];
	uint8_t mac_addr_count;
	char board_serial_number[OCTEON_SERIAL_LEN];
	/*
	 * Several boards support compact flash on the Octeon boot
	 * bus.	 The CF memory spaces may be mapped to different
	 * addresses on different boards.  These values will be 0 if
	 * CF is not present.  Note that these addresses are physical
	 * addresses, and it is up to the application to use the
	 * proper addressing mode (XKPHYS, KSEG0, etc.)
	 */
	uint64_t compact_flash_common_base_addr;
	uint64_t compact_flash_attribute_base_addr;
	/*
	 * Base address of the LED display (as on EBT3000 board) This
	 * will be 0 if LED display not present.  Note that this
	 * address is a physical address, and it is up to the
	 * application to use the proper addressing mode (XKPHYS,
	 * KSEG0, etc.)
	 */
	uint64_t led_display_base_addr;
	/* DFA reference clock in hz (if applicable)*/
	uint32_t dfa_ref_clock_hz;
	/* configuration flags from bootloader */
	uint32_t bootloader_config_flags;

	/* Uart number used for console */
	uint8_t console_uart_num;
};

/**
 * This function returns the system/board information as obtained
 * by the bootloader.
 *
 *
 * Returns  Pointer to the boot information structure
 *
 */

extern struct cvmx_sysinfo *cvmx_sysinfo_get(void);

#endif /* __CVMX_SYSINFO_H__ */
