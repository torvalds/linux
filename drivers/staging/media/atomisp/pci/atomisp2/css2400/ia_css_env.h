/*
 * Support for Intel Camera Imaging ISP subsystem.
 * Copyright (c) 2015, Intel Corporation.
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

#ifndef __IA_CSS_ENV_H
#define __IA_CSS_ENV_H

#include <type_support.h>
#include <stdarg.h> /* va_list */
#include "ia_css_types.h"
#include "ia_css_acc_types.h"

/** @file
 * This file contains prototypes for functions that need to be provided to the
 * CSS-API host-code by the environment in which the CSS-API code runs.
 */

/** Memory allocation attributes, for use in ia_css_css_mem_env. */
enum ia_css_mem_attr {
	IA_CSS_MEM_ATTR_CACHED = 1 << 0,
	IA_CSS_MEM_ATTR_ZEROED = 1 << 1,
	IA_CSS_MEM_ATTR_PAGEALIGN = 1 << 2,
	IA_CSS_MEM_ATTR_CONTIGUOUS = 1 << 3,
};

/** Environment with function pointers for local IA memory allocation.
 *  This provides the CSS code with environment specific functionality
 *  for memory allocation of small local buffers such as local data structures.
 *  This is never expected to allocate more than one page of memory (4K bytes).
 */
struct ia_css_cpu_mem_env {
	void * (*alloc)(size_t bytes, bool zero_mem);
	/**< Allocation function with boolean argument to indicate whether
	     the allocated memory should be zeroed out or not, true (or 1)
	     meaning the memory given to CSS must be zeroed */
	void (*free)(void *ptr);
	/**< Corresponding free function. The function must also accept
	     a NULL argument, similar to C89 free(). */
	void (*flush)(struct ia_css_acc_fw *fw);
	/**< Flush function to flush the cache for given accelerator. */
#ifdef ISP2401

	#if !defined(__SVOS__)
	/* a set of matching functions with additional debug params */
	void * (*alloc_ex)(size_t bytes, bool zero_mem, const char *caller_func, int caller_line);
	/**< same as alloc above, only with additional debug parameters */
	void (*free_ex)(void *ptr, const char *caller_func, int caller_line);
	/**< same as free above, only with additional debug parameters */
	#endif
#endif
};

/** Environment with function pointers for allocation of memory for the CSS.
 *  The CSS uses its own MMU which has its own set of page tables. These
 *  functions are expected to use and/or update those page tables.
 *  This type of memory allocation is expected to be used for large buffers
 *  for images and statistics.
 *  ISP pointers are always 32 bits whereas IA pointer widths will depend
 *  on the platform.
 *  Attributes can be a combination (OR'ed) of ia_css_mem_attr values.
 */
struct ia_css_css_mem_env {
	ia_css_ptr(*alloc)(size_t bytes, uint32_t attributes);
	/**< Allocate memory, cached or uncached, zeroed out or not. */
	void (*free)(ia_css_ptr ptr);
	/**< Free ISP shared memory. The function must also accept
	     a NULL argument, similar to C89 free(). */
	int (*load)(ia_css_ptr ptr, void *data, size_t bytes);
	/**< Load from ISP shared memory. This function is necessary because
	     the IA MMU does not share page tables with the ISP MMU. This means
	     that the IA needs to do the virtual-to-physical address
	     translation in software. This function performs this translation.*/
	int (*store)(ia_css_ptr ptr, const void *data, size_t bytes);
	/**< Same as the above load function but then to write data into ISP
	     shared memory. */
	int (*set)(ia_css_ptr ptr, int c, size_t bytes);
	/**< Set an ISP shared memory region to a particular value. Each byte
	     in this region will be set to this value. In most cases this is
	     used to zero-out memory sections in which case the argument c
	     would have the value zero. */
	ia_css_ptr (*mmap)(const void *ptr, const size_t size,
			   uint16_t attribute, void *context);
	/**< Map an pre-allocated memory region to an address. */
#ifdef ISP2401

	/* a set of matching functions with additional debug params */
	ia_css_ptr(*alloc_ex)(size_t bytes, uint32_t attributes, const char *caller_func, int caller_line);
	/**< same as alloc above, only with additional debug parameters */
	void (*free_ex)(ia_css_ptr ptr, const char *caller_func, int caller_line);
	/**< same as free above, only with additional debug parameters */
	int (*load_ex)(ia_css_ptr ptr, void *data, size_t bytes, const char *caller_func, int caller_line);
	/**< same as load above, only with additional debug parameters */
	int (*store_ex)(ia_css_ptr ptr, const void *data, size_t bytes, const char *caller_func, int caller_line);
	/**< same as store above, only with additional debug parameters */
	int (*set_ex)(ia_css_ptr ptr, int c, size_t bytes, const char *caller_func, int caller_line);
	/**< same as set above, only with additional debug parameters */
#endif
};

/** Environment with function pointers to access the CSS hardware. This includes
 *  registers and local memories.
 */
struct ia_css_hw_access_env {
	void (*store_8)(hrt_address addr, uint8_t data);
	/**< Store an 8 bit value into an address in the CSS HW address space.
	     The address must be an 8 bit aligned address. */
	void (*store_16)(hrt_address addr, uint16_t data);
	/**< Store a 16 bit value into an address in the CSS HW address space.
	     The address must be a 16 bit aligned address. */
	void (*store_32)(hrt_address addr, uint32_t data);
	/**< Store a 32 bit value into an address in the CSS HW address space.
	     The address must be a 32 bit aligned address. */
	uint8_t (*load_8)(hrt_address addr);
	/**< Load an 8 bit value from an address in the CSS HW address
	     space. The address must be an 8 bit aligned address. */
	uint16_t (*load_16)(hrt_address addr);
	/**< Load a 16 bit value from an address in the CSS HW address
	     space. The address must be a 16 bit aligned address. */
	uint32_t (*load_32)(hrt_address addr);
	/**< Load a 32 bit value from an address in the CSS HW address
	     space. The address must be a 32 bit aligned address. */
	void (*store)(hrt_address addr, const void *data, uint32_t bytes);
	/**< Store a number of bytes into a byte-aligned address in the CSS HW address space. */
	void (*load)(hrt_address addr, void *data, uint32_t bytes);
	/**< Load a number of bytes from a byte-aligned address in the CSS HW address space. */
};

/** Environment with function pointers to print error and debug messages.
 */
struct ia_css_print_env {
	int (*debug_print)(const char *fmt, va_list args);
	/**< Print a debug message. */
	int (*error_print)(const char *fmt, va_list args);
	/**< Print an error message.*/
};

/** Environment structure. This includes function pointers to access several
 *  features provided by the environment in which the CSS API is used.
 *  This is used to run the camera IP in multiple platforms such as Linux,
 *  Windows and several simulation environments.
 */
struct ia_css_env {
	struct ia_css_cpu_mem_env   cpu_mem_env;   /**< local malloc and free. */
	struct ia_css_css_mem_env   css_mem_env;   /**< CSS/ISP buffer alloc/free */
	struct ia_css_hw_access_env hw_access_env; /**< CSS HW access functions */
	struct ia_css_print_env     print_env;     /**< Message printing env. */
};

#endif /* __IA_CSS_ENV_H */
