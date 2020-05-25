/*
 * Support for Intel Camera Imaging ISP subsystem.
 * Copyright (c) 2015-2017, Intel Corporation.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 */

#ifndef __MEMORY_ACCESS_H_INCLUDED__
#define __MEMORY_ACCESS_H_INCLUDED__

/*!
 * \brief
 * Define the public interface for virtual memory
 * access functions. Access types are limited to
 * those defined in <stdint.h>
 *
 * The address representation is private to the system
 * and represented as "hrt_vaddress" rather than a
 * pointer, as the memory allocation cannot be accessed
 * by dereferencing but reaquires load and store access
 * functions
 *
 * The page table selection or virtual memory context;
 * The page table base index; Is implicit. This page
 * table base index must be set by the implementation
 * of the access function
 *
 * "store" is a transfer to the system
 * "load" is a transfer from the system
 *
 * Allocation properties can be specified by setting
 * attributes (see below) in case of multiple physical
 * memories the memory ID is encoded on the attribute
 *
 * Allocations in the same physical memory, but in a
 * different (set of) page tables can be shared through
 * a page table information mapping function
 */

#include <type_support.h>
#include "platform_support.h"	/* for __func__ */

/*
 * User provided file that defines the (sub)system address types:
 *	- hrt_vaddress	a type that can hold the (sub)system virtual address range
 */
#include "system_types.h"

/*
 * The MMU base address is a physical address, thus the same type is used
 * as for the device base address
 */
#include "device_access.h"

#include "hmm/hmm.h"

/*!
 * \brief
 * Bit masks for specialised allocation functions
 * the default is "uncached", "not contiguous",
 * "not page aligned" and "not cleared"
 *
 * Forcing alignment (usually) returns a pointer
 * at an alignment boundary that is offset from
 * the allocated pointer. Without storing this
 * pointer/offset, we cannot free it. The memory
 * manager is responsible for the bookkeeping, e.g.
 * the allocation function creates a sentinel
 * within the allocation referencable from the
 * returned pointer/address.
 */

#define mmgr_NULL		((hrt_vaddress)0)
#define mmgr_EXCEPTION		((hrt_vaddress)-1)

/*! Return the address of an allocation in memory

 \param	size[in]		Size in bytes of the allocation
 \param	attribute[in]		Bit vector specifying the properties
				of the allocation including zero initialisation

 \return vaddress
 */

hrt_vaddress mmgr_alloc_attr(const size_t size, const uint16_t attribute);

/*! Read an array of bytes from a virtual memory address

 \param	vaddr[in]		Address of an allocation
 \param	data[out]		pointer to the destination array
 \param	size[in]		number of bytes to read

 \return none
 */
void mmgr_load(const hrt_vaddress vaddr, void *data, const size_t size);

/*! Write an array of bytes to device registers or memory in the device

 \param	vaddr[in]		Address of an allocation
 \param	data[in]		pointer to the source array
 \param	size[in]		number of bytes to write

 \return none
 */
void mmgr_store(const hrt_vaddress vaddr, const void *data, const size_t size);

#endif /* __MEMORY_ACCESS_H_INCLUDED__ */
