/*
 * Copyright 2019 Advanced Micro Devices, Inc.
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
 *
 * Authors: AMD
 *
 */
#ifndef _DMUB_META_H_
#define _DMUB_META_H_

#include "dmub_types.h"

#pragma pack(push, 1)

/* Magic value for identifying dmub_fw_meta_info */
#define DMUB_FW_META_MAGIC 0x444D5542

/* Offset from the end of the file to the dmub_fw_meta_info */
#define DMUB_FW_META_OFFSET 0x24

/**
 * struct dmub_fw_meta_info - metadata associated with fw binary
 *
 * NOTE: This should be considered a stable API. Fields should
 *       not be repurposed or reordered. New fields should be
 *       added instead to extend the structure.
 *
 * @magic_value: magic value identifying DMUB firmware meta info
 * @fw_region_size: size of the firmware state region
 * @trace_buffer_size: size of the tracebuffer region
 */
struct dmub_fw_meta_info {
	uint32_t magic_value;
	uint32_t fw_region_size;
	uint32_t trace_buffer_size;
};

/* Ensure that the structure remains 64 bytes. */
union dmub_fw_meta {
	struct dmub_fw_meta_info info;
	uint8_t reserved[64];
};

#pragma pack(pop)

#endif /* _DMUB_META_H_ */
