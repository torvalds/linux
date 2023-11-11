/* SPDX-License-Identifier: GPL-2.0 OR MIT */
/*
 * Copyright 2014-2022 Advanced Micro Devices, Inc.
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
 */

#ifndef KFD_PM4_HEADERS_H_
#define KFD_PM4_HEADERS_H_

#ifndef PM4_MES_HEADER_DEFINED
#define PM4_MES_HEADER_DEFINED
union PM4_MES_TYPE_3_HEADER {
	struct {
		/* reserved */
		uint32_t reserved1:8;
		/* IT opcode */
		uint32_t opcode:8;
		/* number of DWORDs - 1 in the information body */
		uint32_t count:14;
		/* packet identifier. It should be 3 for type 3 packets */
		uint32_t type:2;
	};
	uint32_t u32all;
};
#endif /* PM4_MES_HEADER_DEFINED */


/*--------------------MES_MAP_PROCESS-------------------- */

#ifndef PM4_MES_MAP_PROCESS_DEFINED
#define PM4_MES_MAP_PROCESS_DEFINED

struct pm4_map_process {
	union {
		union PM4_MES_TYPE_3_HEADER header;	/* header */
		uint32_t ordinal1;
	};

	union {
		struct {
			uint32_t pasid:16;
			uint32_t reserved1:8;
			uint32_t diq_enable:1;
			uint32_t process_quantum:7;
		} bitfields2;
		uint32_t ordinal2;
	};

	union {
		struct {
			uint32_t page_table_base:28;
			uint32_t reserved3:4;
		} bitfields3;
		uint32_t ordinal3;
	};

	uint32_t sh_mem_bases;
	uint32_t sh_mem_ape1_base;
	uint32_t sh_mem_ape1_limit;
	uint32_t sh_mem_config;
	uint32_t gds_addr_lo;
	uint32_t gds_addr_hi;

	union {
		struct {
			uint32_t num_gws:6;
			uint32_t reserved4:2;
			uint32_t num_oac:4;
			uint32_t reserved5:4;
			uint32_t gds_size:6;
			uint32_t num_queues:10;
		} bitfields10;
		uint32_t ordinal10;
	};

};
#endif

#ifndef PM4_MES_MAP_PROCESS_DEFINED_KV_SCRATCH
#define PM4_MES_MAP_PROCESS_DEFINED_KV_SCRATCH

struct pm4_map_process_scratch_kv {
	union {
		union PM4_MES_TYPE_3_HEADER   header; /* header */
		uint32_t            ordinal1;
	};

	union {
		struct {
			uint32_t pasid:16;
			uint32_t reserved1:8;
			uint32_t diq_enable:1;
			uint32_t process_quantum:7;
		} bitfields2;
		uint32_t ordinal2;
	};

	union {
		struct {
			uint32_t page_table_base:28;
			uint32_t reserved2:4;
		} bitfields3;
		uint32_t ordinal3;
	};

	uint32_t reserved3;
	uint32_t sh_mem_bases;
	uint32_t sh_mem_config;
	uint32_t sh_mem_ape1_base;
	uint32_t sh_mem_ape1_limit;
	uint32_t sh_hidden_private_base_vmid;
	uint32_t reserved4;
	uint32_t reserved5;
	uint32_t gds_addr_lo;
	uint32_t gds_addr_hi;

	union {
		struct {
			uint32_t num_gws:6;
			uint32_t reserved6:2;
			uint32_t num_oac:4;
			uint32_t reserved7:4;
			uint32_t gds_size:6;
			uint32_t num_queues:10;
		} bitfields14;
		uint32_t ordinal14;
	};

	uint32_t completion_signal_lo32;
uint32_t completion_signal_hi32;
};
#endif

enum {
	CACHE_FLUSH_AND_INV_TS_EVENT = 0x00000014
};

#endif /* KFD_PM4_HEADERS_H_ */
