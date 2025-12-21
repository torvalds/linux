/* SPDX-License-Identifier: MIT */
/*
 * Copyright 2025 Advanced Micro Devices, Inc.
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

#ifndef __AMDGPU_RAS_CMD_H__
#define __AMDGPU_RAS_CMD_H__
#include "ras.h"

enum amdgpu_ras_cmd_id {
	RAS_CMD__AMDGPU_BEGIN = RAS_CMD_ID_AMDGPU_START,
	RAS_CMD__TRANSLATE_MEMORY_FD,
	RAS_CMD__AMDGPU_SUPPORTED_MAX = RAS_CMD_ID_AMDGPU_END,
};

struct ras_cmd_translate_memory_fd_req {
	struct ras_cmd_dev_handle dev;
	uint32_t type;
	uint32_t fd;
	uint64_t address;
	uint32_t reserved[4];
};

struct ras_cmd_translate_memory_fd_rsp {
	uint32_t version;
	uint32_t padding;
	uint64_t start;
	uint64_t size;
	uint32_t reserved[2];
};

int amdgpu_ras_handle_cmd(struct ras_core_context *ras_core,
		struct ras_cmd_ctx *cmd, void *data);
int amdgpu_ras_submit_cmd(struct ras_core_context *ras_core, struct ras_cmd_ctx *cmd);

#endif
