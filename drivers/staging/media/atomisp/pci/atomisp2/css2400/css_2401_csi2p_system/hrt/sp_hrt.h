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

#ifndef _sp_hrt_h_
#define _sp_hrt_h_

#define hrt_sp_dmem(cell) HRT_PROC_TYPE_PROP(cell, _dmem)

#define hrt_sp_dmem_master_port_address(cell) hrt_mem_master_port_address(cell, hrt_sp_dmem(cell))

#endif /* _sp_hrt_h_ */
#endif
