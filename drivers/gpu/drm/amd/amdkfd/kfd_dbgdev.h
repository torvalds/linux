/*
 * Copyright 2014 Advanced Micro Devices, Inc.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE COPYRIGHT HOLDER(S) OR AUTHOR(S) BE LIABLE FOR ANY CLAIM, DAMAGES OR
 * OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
 * ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 * OTHER DEALINGS IN THE SOFTWARE.
 */

#ifndef KFD_DBGDEV_H_
#define KFD_DBGDEV_H_

enum {
	SQ_CMD_VMID_OFFSET = 28,
	ADDRESS_WATCH_CNTL_OFFSET = 24
};

enum {
	PRIV_QUEUE_SYNC_TIME_MS = 200
};

/* CONTEXT reg space definition */
enum {
	CONTEXT_REG_BASE = 0xA000,
	CONTEXT_REG_END = 0xA400,
	CONTEXT_REG_SIZE = CONTEXT_REG_END - CONTEXT_REG_BASE
};

/* USER CONFIG reg space definition */
enum {
	USERCONFIG_REG_BASE = 0xC000,
	USERCONFIG_REG_END = 0x10000,
	USERCONFIG_REG_SIZE = USERCONFIG_REG_END - USERCONFIG_REG_BASE
};

/* CONFIG reg space definition */
enum {
	AMD_CONFIG_REG_BASE = 0x2000,	/* in dwords */
	AMD_CONFIG_REG_END = 0x2B00,
	AMD_CONFIG_REG_SIZE = AMD_CONFIG_REG_END - AMD_CONFIG_REG_BASE
};

/* SH reg space definition */
enum {
	SH_REG_BASE = 0x2C00,
	SH_REG_END = 0x3000,
	SH_REG_SIZE = SH_REG_END - SH_REG_BASE
};

enum SQ_IND_CMD_CMD {
	SQ_IND_CMD_CMD_NULL = 0x00000000,
	SQ_IND_CMD_CMD_HALT = 0x00000001,
	SQ_IND_CMD_CMD_RESUME = 0x00000002,
	SQ_IND_CMD_CMD_KILL = 0x00000003,
	SQ_IND_CMD_CMD_DEBUG = 0x00000004,
	SQ_IND_CMD_CMD_TRAP = 0x00000005,
};

enum SQ_IND_CMD_MODE {
	SQ_IND_CMD_MODE_SINGLE = 0x00000000,
	SQ_IND_CMD_MODE_BROADCAST = 0x00000001,
	SQ_IND_CMD_MODE_BROADCAST_QUEUE = 0x00000002,
	SQ_IND_CMD_MODE_BROADCAST_PIPE = 0x00000003,
	SQ_IND_CMD_MODE_BROADCAST_ME = 0x00000004,
};

union SQ_IND_INDEX_BITS {
	struct {
		uint32_t wave_id:4;
		uint32_t simd_id:2;
		uint32_t thread_id:6;
		 uint32_t:1;
		uint32_t force_read:1;
		uint32_t read_timeout:1;
		uint32_t unindexed:1;
		uint32_t index:16;

	} bitfields, bits;
	uint32_t u32All;
	signed int i32All;
	float f32All;
};

union SQ_IND_CMD_BITS {
	struct {
		uint32_t data:32;
	} bitfields, bits;
	uint32_t u32All;
	signed int i32All;
	float f32All;
};

union SQ_CMD_BITS {
	struct {
		uint32_t cmd:3;
		 uint32_t:1;
		uint32_t mode:3;
		uint32_t check_vmid:1;
		uint32_t trap_id:3;
		 uint32_t:5;
		uint32_t wave_id:4;
		uint32_t simd_id:2;
		 uint32_t:2;
		uint32_t queue_id:3;
		 uint32_t:1;
		uint32_t vm_id:4;
	} bitfields, bits;
	uint32_t u32All;
	signed int i32All;
	float f32All;
};

union SQ_IND_DATA_BITS {
	struct {
		uint32_t data:32;
	} bitfields, bits;
	uint32_t u32All;
	signed int i32All;
	float f32All;
};

union GRBM_GFX_INDEX_BITS {
	struct {
		uint32_t instance_index:8;
		uint32_t sh_index:8;
		uint32_t se_index:8;
		 uint32_t:5;
		uint32_t sh_broadcast_writes:1;
		uint32_t instance_broadcast_writes:1;
		uint32_t se_broadcast_writes:1;
	} bitfields, bits;
	uint32_t u32All;
	signed int i32All;
	float f32All;
};

union TCP_WATCH_ADDR_H_BITS {
	struct {
		uint32_t addr:16;
		 uint32_t:16;

	} bitfields, bits;
	uint32_t u32All;
	signed int i32All;
	float f32All;
};

union TCP_WATCH_ADDR_L_BITS {
	struct {
		uint32_t:6;
		uint32_t addr:26;
	} bitfields, bits;
	uint32_t u32All;
	signed int i32All;
	float f32All;
};

enum {
	QUEUESTATE__INVALID = 0, /* so by default we'll get invalid state */
	QUEUESTATE__ACTIVE_COMPLETION_PENDING,
	QUEUESTATE__ACTIVE
};

union ULARGE_INTEGER {
	struct {
		uint32_t low_part;
		uint32_t high_part;
	} u;
	unsigned long long quad_part;
};


#define KFD_CIK_VMID_START_OFFSET (8)
#define KFD_CIK_VMID_END_OFFSET (KFD_CIK_VMID_START_OFFSET + (8))


void kfd_dbgdev_init(struct kfd_dbgdev *pdbgdev, struct kfd_dev *pdev,
			enum DBGDEV_TYPE type);

#endif	/* KFD_DBGDEV_H_ */
