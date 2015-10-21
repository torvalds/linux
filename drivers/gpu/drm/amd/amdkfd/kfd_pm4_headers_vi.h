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
 *
 */

#ifndef F32_MES_PM4_PACKETS_H
#define F32_MES_PM4_PACKETS_H

#ifndef PM4_MES_HEADER_DEFINED
#define PM4_MES_HEADER_DEFINED
union PM4_MES_TYPE_3_HEADER {
	struct {
		uint32_t reserved1 : 8; /* < reserved */
		uint32_t opcode    : 8; /* < IT opcode */
		uint32_t count     : 14;/* < number of DWORDs - 1 in the
		information body. */
		uint32_t type      : 2; /* < packet identifier.
					It should be 3 for type 3 packets */
	};
	uint32_t u32All;
};
#endif /* PM4_MES_HEADER_DEFINED */

/*--------------------MES_SET_RESOURCES--------------------*/

#ifndef PM4_MES_SET_RESOURCES_DEFINED
#define PM4_MES_SET_RESOURCES_DEFINED
enum mes_set_resources_queue_type_enum {
	queue_type__mes_set_resources__kernel_interface_queue_kiq = 0,
	queue_type__mes_set_resources__hsa_interface_queue_hiq = 1,
	queue_type__mes_set_resources__hsa_debug_interface_queue = 4
};


struct pm4_mes_set_resources {
	union {
		union PM4_MES_TYPE_3_HEADER	header;		/* header */
		uint32_t			ordinal1;
	};

	union {
		struct {
			uint32_t vmid_mask:16;
			uint32_t unmap_latency:8;
			uint32_t reserved1:5;
			enum mes_set_resources_queue_type_enum queue_type:3;
		} bitfields2;
		uint32_t ordinal2;
	};

	uint32_t queue_mask_lo;
	uint32_t queue_mask_hi;
	uint32_t gws_mask_lo;
	uint32_t gws_mask_hi;

	union {
		struct {
			uint32_t oac_mask:16;
			uint32_t reserved2:16;
		} bitfields7;
		uint32_t ordinal7;
	};

	union {
		struct {
		uint32_t gds_heap_base:6;
		uint32_t reserved3:5;
		uint32_t gds_heap_size:6;
		uint32_t reserved4:15;
		} bitfields8;
		uint32_t ordinal8;
	};

};
#endif

/*--------------------MES_RUN_LIST--------------------*/

#ifndef PM4_MES_RUN_LIST_DEFINED
#define PM4_MES_RUN_LIST_DEFINED

struct pm4_mes_runlist {
	union {
	    union PM4_MES_TYPE_3_HEADER   header;            /* header */
	    uint32_t            ordinal1;
	};

	union {
		struct {
			uint32_t reserved1:2;
			uint32_t ib_base_lo:30;
		} bitfields2;
		uint32_t ordinal2;
	};

	union {
		struct {
			uint32_t ib_base_hi:16;
			uint32_t reserved2:16;
		} bitfields3;
		uint32_t ordinal3;
	};

	union {
		struct {
			uint32_t ib_size:20;
			uint32_t chain:1;
			uint32_t offload_polling:1;
			uint32_t reserved3:1;
			uint32_t valid:1;
			uint32_t reserved4:8;
		} bitfields4;
		uint32_t ordinal4;
	};

};
#endif

/*--------------------MES_MAP_PROCESS--------------------*/

#ifndef PM4_MES_MAP_PROCESS_DEFINED
#define PM4_MES_MAP_PROCESS_DEFINED

struct pm4_mes_map_process {
	union {
		union PM4_MES_TYPE_3_HEADER   header;            /* header */
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

	uint32_t sh_mem_bases;
	uint32_t sh_mem_ape1_base;
	uint32_t sh_mem_ape1_limit;
	uint32_t sh_mem_config;
	uint32_t gds_addr_lo;
	uint32_t gds_addr_hi;

	union {
		struct {
			uint32_t num_gws:6;
			uint32_t reserved3:2;
			uint32_t num_oac:4;
			uint32_t reserved4:4;
			uint32_t gds_size:6;
			uint32_t num_queues:10;
		} bitfields10;
		uint32_t ordinal10;
	};

};
#endif

/*--------------------MES_MAP_QUEUES--------------------*/

#ifndef PM4_MES_MAP_QUEUES_VI_DEFINED
#define PM4_MES_MAP_QUEUES_VI_DEFINED
enum mes_map_queues_queue_sel_vi_enum {
	queue_sel__mes_map_queues__map_to_specified_queue_slots_vi = 0,
queue_sel__mes_map_queues__map_to_hws_determined_queue_slots_vi = 1
};

enum mes_map_queues_queue_type_vi_enum {
	queue_type__mes_map_queues__normal_compute_vi = 0,
	queue_type__mes_map_queues__debug_interface_queue_vi = 1,
	queue_type__mes_map_queues__normal_latency_static_queue_vi = 2,
queue_type__mes_map_queues__low_latency_static_queue_vi = 3
};

enum mes_map_queues_alloc_format_vi_enum {
	alloc_format__mes_map_queues__one_per_pipe_vi = 0,
alloc_format__mes_map_queues__all_on_one_pipe_vi = 1
};

enum mes_map_queues_engine_sel_vi_enum {
	engine_sel__mes_map_queues__compute_vi = 0,
	engine_sel__mes_map_queues__sdma0_vi = 2,
	engine_sel__mes_map_queues__sdma1_vi = 3
};


struct pm4_mes_map_queues {
	union {
		union PM4_MES_TYPE_3_HEADER   header;            /* header */
		uint32_t            ordinal1;
	};

	union {
		struct {
			uint32_t reserved1:4;
			enum mes_map_queues_queue_sel_vi_enum queue_sel:2;
			uint32_t reserved2:15;
			enum mes_map_queues_queue_type_vi_enum queue_type:3;
			enum mes_map_queues_alloc_format_vi_enum alloc_format:2;
			enum mes_map_queues_engine_sel_vi_enum engine_sel:3;
			uint32_t num_queues:3;
		} bitfields2;
		uint32_t ordinal2;
	};

	union {
		struct {
			uint32_t reserved3:1;
			uint32_t check_disable:1;
			uint32_t doorbell_offset:21;
			uint32_t reserved4:3;
			uint32_t queue:6;
		} bitfields3;
		uint32_t ordinal3;
	};

	uint32_t mqd_addr_lo;
	uint32_t mqd_addr_hi;
	uint32_t wptr_addr_lo;
	uint32_t wptr_addr_hi;
};
#endif

/*--------------------MES_QUERY_STATUS--------------------*/

#ifndef PM4_MES_QUERY_STATUS_DEFINED
#define PM4_MES_QUERY_STATUS_DEFINED
enum mes_query_status_interrupt_sel_enum {
	interrupt_sel__mes_query_status__completion_status = 0,
	interrupt_sel__mes_query_status__process_status = 1,
	interrupt_sel__mes_query_status__queue_status = 2
};

enum mes_query_status_command_enum {
	command__mes_query_status__interrupt_only = 0,
	command__mes_query_status__fence_only_immediate = 1,
	command__mes_query_status__fence_only_after_write_ack = 2,
	command__mes_query_status__fence_wait_for_write_ack_send_interrupt = 3
};

enum mes_query_status_engine_sel_enum {
	engine_sel__mes_query_status__compute = 0,
	engine_sel__mes_query_status__sdma0_queue = 2,
	engine_sel__mes_query_status__sdma1_queue = 3
};

struct pm4_mes_query_status {
	union {
		union PM4_MES_TYPE_3_HEADER   header;            /* header */
		uint32_t            ordinal1;
	};

	union {
		struct {
			uint32_t context_id:28;
			enum mes_query_status_interrupt_sel_enum
				interrupt_sel:2;
			enum mes_query_status_command_enum command:2;
		} bitfields2;
		uint32_t ordinal2;
	};

	union {
		struct {
			uint32_t pasid:16;
			uint32_t reserved1:16;
		} bitfields3a;
		struct {
			uint32_t reserved2:2;
			uint32_t doorbell_offset:21;
			uint32_t reserved3:2;
			enum mes_query_status_engine_sel_enum engine_sel:3;
			uint32_t reserved4:4;
		} bitfields3b;
		uint32_t ordinal3;
	};

	uint32_t addr_lo;
	uint32_t addr_hi;
	uint32_t data_lo;
	uint32_t data_hi;
};
#endif

/*--------------------MES_UNMAP_QUEUES--------------------*/

#ifndef PM4_MES_UNMAP_QUEUES_DEFINED
#define PM4_MES_UNMAP_QUEUES_DEFINED
enum mes_unmap_queues_action_enum {
	action__mes_unmap_queues__preempt_queues = 0,
	action__mes_unmap_queues__reset_queues = 1,
	action__mes_unmap_queues__disable_process_queues = 2,
	action__mes_unmap_queues__reserved = 3
};

enum mes_unmap_queues_queue_sel_enum {
	queue_sel__mes_unmap_queues__perform_request_on_specified_queues = 0,
	queue_sel__mes_unmap_queues__perform_request_on_pasid_queues = 1,
	queue_sel__mes_unmap_queues__unmap_all_queues = 2,
	queue_sel__mes_unmap_queues__unmap_all_non_static_queues = 3
};

enum mes_unmap_queues_engine_sel_enum {
	engine_sel__mes_unmap_queues__compute = 0,
	engine_sel__mes_unmap_queues__sdma0 = 2,
	engine_sel__mes_unmap_queues__sdmal = 3
};

struct PM4_MES_UNMAP_QUEUES {
	union {
		union PM4_MES_TYPE_3_HEADER   header;            /* header */
		uint32_t            ordinal1;
	};

	union {
		struct {
			enum mes_unmap_queues_action_enum action:2;
			uint32_t reserved1:2;
			enum mes_unmap_queues_queue_sel_enum queue_sel:2;
			uint32_t reserved2:20;
			enum mes_unmap_queues_engine_sel_enum engine_sel:3;
			uint32_t num_queues:3;
		} bitfields2;
		uint32_t ordinal2;
	};

	union {
		struct {
			uint32_t pasid:16;
			uint32_t reserved3:16;
		} bitfields3a;
		struct {
			uint32_t reserved4:2;
			uint32_t doorbell_offset0:21;
			uint32_t reserved5:9;
		} bitfields3b;
		uint32_t ordinal3;
	};

	union {
	struct {
			uint32_t reserved6:2;
			uint32_t doorbell_offset1:21;
			uint32_t reserved7:9;
		} bitfields4;
		uint32_t ordinal4;
	};

	union {
		struct {
			uint32_t reserved8:2;
			uint32_t doorbell_offset2:21;
			uint32_t reserved9:9;
		} bitfields5;
		uint32_t ordinal5;
	};

	union {
		struct {
			uint32_t reserved10:2;
			uint32_t doorbell_offset3:21;
			uint32_t reserved11:9;
		} bitfields6;
		uint32_t ordinal6;
	};
};
#endif

#endif
