/* SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note */
/*
 *
 * (C) COPYRIGHT 2022 ARM Limited. All rights reserved.
 *
 * This program is free software and is provided to you under the terms of the
 * GNU General Public License version 2 as published by the Free Software
 * Foundation, and any use by you of this program is subject to the terms
 * of such GNU license.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, you can access it online at
 * http://www.gnu.org/licenses/gpl-2.0.html.
 *
 */

#ifndef _CORESIGHT_MALI_COMMON_H
#define _CORESIGHT_MALI_COMMON_H

#include <linux/types.h>
#include <linux/mali_kbase_debug_coresight_csf.h>

/* Macros for CoreSight OP types. */
#define WRITE_IMM_OP(_reg_addr, _val)                                                              \
	{                                                                                          \
		.type = KBASE_DEBUG_CORESIGHT_CSF_OP_TYPE_WRITE_IMM,                               \
		.op.write_imm.reg_addr = _reg_addr, .op.write_imm.val = _val                       \
	}

#define WRITE_RANGE_OP(_reg_start, _reg_end, _val)                                                 \
	{                                                                                          \
		.type = KBASE_DEBUG_CORESIGHT_CSF_OP_TYPE_WRITE_IMM_RANGE,                         \
		.op.write_imm_range.reg_start = _reg_start,                                        \
		.op.write_imm_range.reg_end = _reg_end, .op.write_imm_range.val = _val             \
	}

#define WRITE_PTR_OP(_reg_addr, _ptr)                                                              \
	{                                                                                          \
		.type = KBASE_DEBUG_CORESIGHT_CSF_OP_TYPE_WRITE, .op.write.reg_addr = _reg_addr,   \
		.op.write.ptr = _ptr                                                               \
	}

#define READ_OP(_reg_addr, _ptr)                                                                   \
	{                                                                                          \
		.type = KBASE_DEBUG_CORESIGHT_CSF_OP_TYPE_READ, .op.read.reg_addr = _reg_addr,     \
		.op.read.ptr = _ptr                                                                \
	}

#define POLL_OP(_reg_addr, _mask, _val)                                                            \
	{                                                                                          \
		.type = KBASE_DEBUG_CORESIGHT_CSF_OP_TYPE_POLL, .op.poll.reg_addr = _reg_addr,     \
		.op.poll.mask = _mask, .op.poll.val = _val                                         \
	}

#define BIT_OR_OP(_ptr, _val)                                                                      \
	{                                                                                          \
		.type = KBASE_DEBUG_CORESIGHT_CSF_OP_TYPE_BIT_OR, .op.bitw.ptr = _ptr,             \
		.op.bitw.val = _val                                                                \
	}

#define BIT_XOR_OP(_ptr, _val)                                                                     \
	{                                                                                          \
		.type = KBASE_DEBUG_CORESIGHT_CSF_OP_TYPE_BIT_XOR, .op.bitw.ptr = _ptr,            \
		.op.bitw.val = _val                                                                \
	}

#define BIT_AND_OP(_ptr, _val)                                                                     \
	{                                                                                          \
		.type = KBASE_DEBUG_CORESIGHT_CSF_OP_TYPE_BIT_AND, .op.bitw.ptr = _ptr,            \
		.op.bitw.val = _val                                                                \
	}

#define BIT_NOT_OP(_ptr)                                                                           \
	{                                                                                          \
		.type = KBASE_DEBUG_CORESIGHT_CSF_OP_TYPE_BIT_NOT, .op.bitw.ptr = _ptr,            \
	}

#ifndef CS_MALI_UNLOCK_COMPONENT
/**
 * CS_MALI_UNLOCK_COMPONENT - A write of 0xC5ACCE55 enables write access to the block
 */
#define CS_MALI_UNLOCK_COMPONENT 0xC5ACCE55
#endif

/**
 * struct coresight_mali_drvdata - Coresight mali driver data
 *
 * @csdev:        Coresight device pointer
 * @dev:          Device pointer
 * @kbase_client: Pointer to coresight mali client
 * @config:       Pointer to coresight mali config, used for enabling and
 *                disabling the coresight component
 * @enable_seq:   Enable sequence needed to enable coresight block
 * @disable_seq:  Disable sequence needed to enable coresight block
 * @gpu_dev:      Pointer to gpu device structure
 * @mode:         Mode in which the driver operates
 */
struct coresight_mali_drvdata {
	struct coresight_device *csdev;
	struct device *dev;
	void *kbase_client;
	void *config;
	struct kbase_debug_coresight_csf_sequence enable_seq;
	struct kbase_debug_coresight_csf_sequence disable_seq;
	void *gpu_dev;
	u32 mode;
};

/**
 * coresight_mali_enable_component - Generic enable for a coresight block
 *
 * @csdev:  Coresight device to be enabled
 * @mode: Mode in which the block should start operating in
 *
 * Return: 0 if success. Error code on failure.
 */
int coresight_mali_enable_component(struct coresight_device *csdev, u32 mode);

/**
 * coresight_mali_disable_component - Generic disable for a coresight block
 *
 * @csdev:  Coresight device to be disabled
 *
 * Return: 0 if success. Error code on failure.
 */
int coresight_mali_disable_component(struct coresight_device *csdev);

#endif /* _CORESIGHT_MALI_COMMON_H */
