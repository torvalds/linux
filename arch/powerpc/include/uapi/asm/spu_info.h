/* SPDX-License-Identifier: GPL-2.0+ WITH Linux-syscall-note */
/*
 * SPU info structures
 *
 * (C) Copyright 2006 IBM Corp.
 *
 * Author: Dwayne Grant McConnell <decimal@us.ibm.com>
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
