/*
 *
 *  sep_driver_ext_api.h - Security Processor Driver external api definitions
 *
 *  Copyright(c) 2009 Intel Corporation. All rights reserved.
 *  Copyright(c) 2009 Discretix. All rights reserved.
 *
 *  This program is free software; you can redistribute it and/or modify it
 *  under the terms of the GNU General Public License as published by the Free
 *  Software Foundation; either version 2 of the License, or (at your option)
 *  any later version.
 *
 *  This program is distributed in the hope that it will be useful, but WITHOUT
 *  ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 *  FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 *  more details.
 *
 *  You should have received a copy of the GNU General Public License along with
 *  this program; if not, write to the Free Software Foundation, Inc., 59
 *  Temple Place - Suite 330, Boston, MA  02111-1307, USA.
 *
 *  CONTACTS:
 *
 *  Mark Allyn		mark.a.allyn@intel.com
 *
 *  CHANGES:
 *
 *  2009.06.26	Initial publish
 *
 */

#ifndef __SEP_DRIVER_EXT_API_H__
#define __SEP_DRIVER_EXT_API_H__


/* shared variables */
static int sepDebug;

/*
this function loads the ROM code in SEP (needed only in the debug mode on FPGA)
*/
static void sep_load_rom_code(void);

/*
This functions locks the area of the resident and cache sep code (if possible)
*/
static void sep_lock_cache_resident_area(void);

/*
This functions copies the cache and resident from their source location into
destination memory, which is external to Linux VM and is given as physical
address
*/
static int sep_copy_cache_resident_to_area(unsigned long src_cache_addr, unsigned long cache_size_in_bytes, unsigned long src_resident_addr, unsigned long resident_size_in_bytes, unsigned long *dst_new_cache_addr_ptr, unsigned long *dst_new_resident_addr_ptr);

/*
This functions maps and allocates the shared area on the external
RAM (device) The input is shared_area_size - the size of the memory
to allocate. The outputs are kernel_shared_area_addr_ptr - the kerenl
address of the mapped and allocated shared area, and
phys_shared_area_addr_ptr - the physical address of the shared area
*/
static int sep_map_and_alloc_shared_area(unsigned long shared_area_size, unsigned long *kernel_shared_area_addr_ptr, unsigned long *phys_shared_area_addr_ptr);

/*
This functions unmaps and deallocates the shared area on the  external
RAM (device) The input is shared_area_size - the size of the memory to
deallocate,kernel_shared_area_addr_ptr - the kernel address of the
mapped and allocated shared area,phys_shared_area_addr_ptr - the physical
address of the shared area
*/
static void sep_unmap_and_free_shared_area(unsigned long shared_area_size, unsigned long kernel_shared_area_addr, unsigned long phys_shared_area_addr);


/*
This functions returns the physical address inside shared area according
to the virtual address. It can be either on the externa RAM device
(ioremapped), or on the system RAM
*/
static unsigned long sep_shared_area_virt_to_phys(unsigned long virt_address);

/*
This functions returns the vitrual address inside shared area according
to the physical address. It can be either on the externa RAM device
(ioremapped), or on the system RAM This implementation is for the external RAM
*/
static unsigned long sep_shared_area_phys_to_virt(unsigned long phys_address);

/*
This function registers th driver to the device
subsystem (either PCI, USB, etc)
*/
static int sep_register_driver_to_device(void);

#endif /*__SEP_DRIVER_EXT_API_H__*/
