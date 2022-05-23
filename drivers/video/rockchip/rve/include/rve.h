/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (C) Rockchip Electronics Co., Ltd.
 *
 * Author: Huang Lee <Putin.li@rock-chips.com>
 */
#ifndef _RVE_DRIVER_H_
#define _RVE_DRIVER_H_

#include <linux/mutex.h>
#include <linux/scatterlist.h>

/* Use 'r' as magic number */
#define RVE_IOC_MAGIC		'r'
#define RVE_IOW(nr, type)	_IOW(RVE_IOC_MAGIC, nr, type)
#define RVE_IOR(nr, type)	_IOR(RVE_IOC_MAGIC, nr, type)
#define RVE_IOWR(nr, type)	_IOWR(RVE_IOC_MAGIC, nr, type)

#define RVE_IOC_GET_VER				RVE_IOR(0x1, struct rve_version_t)
#define RVE_IOC_GET_HW_VER			RVE_IOR(0x2, struct rve_hw_versions_t)
#define RVE_IOC_IMPORT_BUFFER		RVE_IOWR(0x3, struct rve_buffer_pool)
#define RVE_IOC_RELEASE_BUFFER		RVE_IOW(0x4, struct rve_buffer_pool)

#define RVE_IOC_START_CONFIG		RVE_IOR(0x5, uint32_t)
#define RVE_IOC_END_CONFIG			RVE_IOWR(0x6, struct rve_user_ctx_t)
#define RVE_IOC_CMD_CONFIG			RVE_IOWR(0x7, struct rve_user_ctx_t)
#define RVE_IOC_CANCEL_CONFIG		RVE_IOWR(0x8, uint32_t)

#define RVE_CMD_NUM_MAX 10

#define RVE_BUFFER_POOL_SIZE_MAX 40

enum rve_memory_type {
	RVE_DMA_BUFFER = 0,
	RVE_VIRTUAL_ADDRESS,
	RVE_PHYSICAL_ADDRESS
};

#define RVE_SCHED_PRIORITY_DEFAULT 0
#define RVE_SCHED_PRIORITY_MAX 6

#define RVE_VERSION_SIZE	16
#define RVE_HW_SIZE		5

struct rve_version_t {
	uint32_t major;
	uint32_t minor;
	uint32_t revision;
	uint32_t prod_num;
	uint8_t str[RVE_VERSION_SIZE];
};

struct rve_hw_versions_t {
	struct rve_version_t version[RVE_HW_SIZE];
	uint32_t size;
};

struct rve_user_ctx_t {
	uint32_t header;
	uint64_t regcmd_data;
	int32_t in_fence_fd;
	int32_t out_fence_fd;
	int32_t cmd_num;
	uint32_t id;
	uint8_t priority;
	uint32_t sync_mode;
	uint32_t disable_auto_cancel;

	uint32_t reserve[31];
};

#endif /*_RVE_DRIVER_H_*/
