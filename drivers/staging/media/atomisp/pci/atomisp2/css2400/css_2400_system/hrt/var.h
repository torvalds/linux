#ifndef ISP2401
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

#ifndef _HRT_VAR_H
#define _HRT_VAR_H

#include "version.h"
#include "system_api.h"
#include "hive_types.h"

#define hrt_int_type_of_char   char
#define hrt_int_type_of_uchar  unsigned char
#define hrt_int_type_of_short  short
#define hrt_int_type_of_ushort unsigned short
#define hrt_int_type_of_int    int
#define hrt_int_type_of_uint   unsigned int
#define hrt_int_type_of_long   long
#define hrt_int_type_of_ulong  unsigned long
#define hrt_int_type_of_ptr    unsigned int

#define hrt_host_type_of_char   char
#define hrt_host_type_of_uchar  unsigned char
#define hrt_host_type_of_short  short
#define hrt_host_type_of_ushort unsigned short
#define hrt_host_type_of_int    int
#define hrt_host_type_of_uint   unsigned int
#define hrt_host_type_of_long   long
#define hrt_host_type_of_ulong  unsigned long
#define hrt_host_type_of_ptr    void*

#define HRT_TYPE_BYTES(cell, type) (HRT_TYPE_BITS(cell, type)/8)
#define HRT_HOST_TYPE(cell_type)   HRTCAT(hrt_host_type_of_, cell_type)
#define HRT_INT_TYPE(type)         HRTCAT(hrt_int_type_of_, type)

#define hrt_scalar_store(cell, type, var, data) \
  HRTCAT(hrt_mem_store_,HRT_TYPE_BITS(cell, type))(\
	       cell, \
	       HRTCAT(HIVE_MEM_,var), \
	       HRTCAT(HIVE_ADDR_,var), \
	       (HRT_INT_TYPE(type))(data))

#define hrt_scalar_load(cell, type, var) \
  (HRT_HOST_TYPE(type))(HRTCAT4(_hrt_mem_load_,HRT_PROC_TYPE(cell),_,type) ( \
	       cell, \
	       HRTCAT(HIVE_MEM_,var), \
	       HRTCAT(HIVE_ADDR_,var)))

#define hrt_indexed_store(cell, type, array, index, data) \
  HRTCAT(hrt_mem_store_,HRT_TYPE_BITS(cell, type))(\
	       cell, \
	       HRTCAT(HIVE_MEM_,array), \
	       (HRTCAT(HIVE_ADDR_,array))+((index)*HRT_TYPE_BYTES(cell, type)), \
	       (HRT_INT_TYPE(type))(data))

#define hrt_indexed_load(cell, type, array, index) \
  (HRT_HOST_TYPE(type))(HRTCAT4(_hrt_mem_load_,HRT_PROC_TYPE(cell),_,type) ( \
         cell, \
	       HRTCAT(HIVE_MEM_,array), \
	       (HRTCAT(HIVE_ADDR_,array))+((index)*HRT_TYPE_BYTES(cell, type))))

#endif /* _HRT_VAR_H */
#endif
