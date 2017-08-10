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

#ifndef _isp2400_support_h
#define _isp2400_support_h

#ifndef ISP2400_VECTOR_TYPES
/* This typedef is to be able to include hive header files
   in the host code which is useful in crun */
typedef char *tmemvectors, *tmemvectoru, *tvector;
#endif

#define hrt_isp_vamem1_store_16(cell, addr, val) hrt_mem_store_16(cell, HRT_PROC_TYPE_PROP(cell, _simd_vamem1), addr, val)
#define hrt_isp_vamem2_store_16(cell, addr, val) hrt_mem_store_16(cell, HRT_PROC_TYPE_PROP(cell, _simd_vamem2), addr, val)

#define hrt_isp_dmem(cell) HRT_PROC_TYPE_PROP(cell, _base_dmem)
#define hrt_isp_vmem(cell) HRT_PROC_TYPE_PROP(cell, _simd_vmem)

#define hrt_isp_dmem_master_port_address(cell) hrt_mem_master_port_address(cell, hrt_isp_dmem(cell))
#define hrt_isp_vmem_master_port_address(cell) hrt_mem_master_port_address(cell, hrt_isp_vmem(cell))

#if ISP_HAS_HIST
  #define hrt_isp_hist(cell) HRT_PROC_TYPE_PROP(cell, _simd_histogram)
  #define hrt_isp_hist_master_port_address(cell) hrt_mem_master_port_address(cell, hrt_isp_hist(cell))
#endif

#endif /* _isp2400_support_h */
