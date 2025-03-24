/* SPDX-License-Identifier: GPL-2.0 or MIT */
/* Copyright 2018 Marty E. Plummer <hanetzer@startmail.com> */
/* Copyright 2019 Collabora ltd. */

#ifndef __PANTHOR_GPU_H__
#define __PANTHOR_GPU_H__

#include <linux/types.h>

struct panthor_device;

int panthor_gpu_init(struct panthor_device *ptdev);
void panthor_gpu_unplug(struct panthor_device *ptdev);
void panthor_gpu_suspend(struct panthor_device *ptdev);
void panthor_gpu_resume(struct panthor_device *ptdev);

int panthor_gpu_block_power_on(struct panthor_device *ptdev,
			       const char *blk_name,
			       u32 pwron_reg, u32 pwrtrans_reg,
			       u32 rdy_reg, u64 mask, u32 timeout_us);
int panthor_gpu_block_power_off(struct panthor_device *ptdev,
				const char *blk_name,
				u32 pwroff_reg, u32 pwrtrans_reg,
				u64 mask, u32 timeout_us);

/**
 * panthor_gpu_power_on() - Power on the GPU block.
 *
 * Return: 0 on success, a negative error code otherwise.
 */
#define panthor_gpu_power_on(ptdev, type, mask, timeout_us) \
	panthor_gpu_block_power_on(ptdev, #type, \
				  type ## _PWRON_LO, \
				  type ## _PWRTRANS_LO, \
				  type ## _READY_LO, \
				  mask, timeout_us)

/**
 * panthor_gpu_power_off() - Power off the GPU block.
 *
 * Return: 0 on success, a negative error code otherwise.
 */
#define panthor_gpu_power_off(ptdev, type, mask, timeout_us) \
	panthor_gpu_block_power_off(ptdev, #type, \
				   type ## _PWROFF_LO, \
				   type ## _PWRTRANS_LO, \
				   mask, timeout_us)

int panthor_gpu_l2_power_on(struct panthor_device *ptdev);
int panthor_gpu_flush_caches(struct panthor_device *ptdev,
			     u32 l2, u32 lsc, u32 other);
int panthor_gpu_soft_reset(struct panthor_device *ptdev);
u64 panthor_gpu_read_timestamp(struct panthor_device *ptdev);
u64 panthor_gpu_read_timestamp_offset(struct panthor_device *ptdev);

#endif
