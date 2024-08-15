/* SPDX-License-Identifier: GPL-2.0 OR MIT */
/*
 * Copyright 2020-2022 Advanced Micro Devices, Inc.
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

/*--------------------MES_MAP_PROCESS (PER DEBUG VMID)--------------------*/

#ifndef PM4_MES_MAP_PROCESS_PER_DEBUG_VMID_DEFINED
#define PM4_MES_MAP_PROCESS_PER_DEBUG_VMID_DEFINED

struct pm4_mes_map_process_aldebaran {
	union {
		union PM4_MES_TYPE_3_HEADER header;	/* header */
		uint32_t ordinal1;
	};

	union {
		struct {
			uint32_t pasid:16;	    /* 0 - 15  */
			uint32_t single_memops:1;   /* 16      */
			uint32_t reserved1:1;	    /* 17      */
			uint32_t debug_vmid:4;	    /* 18 - 21 */
			uint32_t new_debug:1;	    /* 22      */
			uint32_t tmz:1;		    /* 23      */
			uint32_t diq_enable:1;      /* 24      */
			uint32_t process_quantum:7; /* 25 - 31 */
		} bitfields2;
		uint32_t ordinal2;
	};

	uint32_t vm_context_page_table_base_addr_lo32;

	uint32_t vm_context_page_table_base_addr_hi32;

	uint32_t sh_mem_bases;

	uint32_t sh_mem_config;

	uint32_t sq_shader_tba_lo;

	uint32_t sq_shader_tba_hi;

	uint32_t sq_shader_tma_lo;

	uint32_t sq_shader_tma_hi;

	uint32_t reserved6;

	uint32_t gds_addr_lo;

	uint32_t gds_addr_hi;

	union {
		struct {
			uint32_t num_gws:7;
			uint32_t sdma_enable:1;
			uint32_t num_oac:4;
			uint32_t gds_size_hi:4;
			uint32_t gds_size:6;
			uint32_t num_queues:10;
		} bitfields14;
		uint32_t ordinal14;
	};

	uint32_t spi_gdbg_per_vmid_cntl;

	uint32_t tcp_watch_cntl[4];

	uint32_t completion_signal_lo;

	uint32_t completion_signal_hi;

};

#endif
