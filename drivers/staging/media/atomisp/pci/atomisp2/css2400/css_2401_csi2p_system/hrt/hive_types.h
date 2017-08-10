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

#ifndef _HRT_HIVE_TYPES_H 
#define _HRT_HIVE_TYPES_H 

#include "version.h"
#include "defs.h"

#ifndef HRTCAT3
#define _HRTCAT3(m,n,o)     m##n##o
#define HRTCAT3(m,n,o)      _HRTCAT3(m,n,o)
#endif

#ifndef HRTCAT4
#define _HRTCAT4(m,n,o,p)     m##n##o##p
#define HRTCAT4(m,n,o,p)      _HRTCAT4(m,n,o,p)
#endif

#ifndef HRTMIN
#define HRTMIN(a,b) (((a)<(b))?(a):(b))
#endif
                                 
#ifndef HRTMAX
#define HRTMAX(a,b) (((a)>(b))?(a):(b))
#endif

/* boolean data type */
typedef unsigned int hive_bool;
#define hive_false 0
#define hive_true  1

typedef char                 hive_int8;
typedef short                hive_int16;
typedef int                  hive_int32;
typedef long long            hive_int64;

typedef unsigned char        hive_uint8;
typedef unsigned short       hive_uint16;
typedef unsigned int         hive_uint32;
typedef unsigned long long   hive_uint64;

/* by default assume 32 bit master port (both data and address) */
#ifndef HRT_DATA_WIDTH
#define HRT_DATA_WIDTH 32
#endif
#ifndef HRT_ADDRESS_WIDTH
#define HRT_ADDRESS_WIDTH 32
#endif

#define HRT_DATA_BYTES    (HRT_DATA_WIDTH/8)
#define HRT_ADDRESS_BYTES (HRT_ADDRESS_WIDTH/8)

#if HRT_DATA_WIDTH == 64
typedef hive_uint64 hrt_data;
#elif HRT_DATA_WIDTH == 32
typedef hive_uint32 hrt_data;
#else
#error data width not supported
#endif

#if HRT_ADDRESS_WIDTH == 64
typedef hive_uint64 hrt_address; 
#elif HRT_ADDRESS_WIDTH == 32
typedef hive_uint32 hrt_address;
#else
#error adddres width not supported
#endif

/* The SP side representation of an HMM virtual address */
typedef hive_uint32 hrt_vaddress;

/* use 64 bit addresses in simulation, where possible */
typedef hive_uint64  hive_sim_address;

/* below is for csim, not for hrt, rename and move this elsewhere */

typedef unsigned int hive_uint;
typedef hive_uint32  hive_address;
typedef hive_address hive_slave_address;
typedef hive_address hive_mem_address;

/* MMIO devices */
typedef hive_uint    hive_mmio_id;
typedef hive_mmio_id hive_slave_id;
typedef hive_mmio_id hive_port_id;
typedef hive_mmio_id hive_master_id; 
typedef hive_mmio_id hive_mem_id;
typedef hive_mmio_id hive_dev_id;
typedef hive_mmio_id hive_fifo_id;

typedef hive_uint      hive_hier_id;
typedef hive_hier_id   hive_device_id;
typedef hive_device_id hive_proc_id;
typedef hive_device_id hive_cell_id;
typedef hive_device_id hive_host_id;
typedef hive_device_id hive_bus_id;
typedef hive_device_id hive_bridge_id;
typedef hive_device_id hive_fifo_adapter_id;
typedef hive_device_id hive_custom_device_id;

typedef hive_uint hive_slot_id;
typedef hive_uint hive_fu_id;
typedef hive_uint hive_reg_file_id;
typedef hive_uint hive_reg_id;

/* Streaming devices */
typedef hive_uint hive_outport_id;
typedef hive_uint hive_inport_id;

typedef hive_uint hive_msink_id;

/* HRT specific */
typedef char* hive_program;
typedef char* hive_function;

#endif /* _HRT_HIVE_TYPES_H */
