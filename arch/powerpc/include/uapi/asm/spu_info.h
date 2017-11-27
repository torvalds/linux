/* SPDX-License-Identifier: GPL-2.0+ WITH Linux-syscall-note */
/*
 * SPU info structures
 *
 * (C) Copyright 2006 IBM Corp.
 *
 * Author: Dwayne Grant McConnell <decimal@us.ibm.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2, or (at your option)
 * any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.	See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */

#ifndef _UAPI_SPU_INFO_H
#define _UAPI_SPU_INFO_H

#include <linux/types.h>

#ifndef __KERNEL__
struct mfc_cq_sr {
	__u64 mfc_cq_data0_RW;
	__u64 mfc_cq_data1_RW;
	__u64 mfc_cq_data2_RW;
	__u64 mfc_cq_data3_RW;
};
#endif /* __KERNEL__ */

struct spu_dma_info {
	__u64 dma_info_type;
	__u64 dma_info_mask;
	__u64 dma_info_status;
	__u64 dma_info_stall_and_notify;
	__u64 dma_info_atomic_command_status;
	struct mfc_cq_sr dma_info_command_data[16];
};

struct spu_proxydma_info {
	__u64 proxydma_info_type;
	__u64 proxydma_info_mask;
	__u64 proxydma_info_status;
	struct mfc_cq_sr proxydma_info_command_data[8];
};

#endif /* _UAPI_SPU_INFO_H */
