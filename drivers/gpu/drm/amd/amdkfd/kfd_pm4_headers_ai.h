/* SPDX-License-Identifier: GPL-2.0 OR MIT */
/*
 * Copyright 2016-2022 Advanced Micro Devices, Inc.
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
					 *   information body.
					 */
		uint32_t type      : 2; /* < packet identifier.
					 *   It should be 3 for type 3 packets
					 */
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
		uint32_t gds_heap_base:10;
		uint32_t reserved3:1;
		uint32_t gds_heap_size:10;
		uint32_t reserved4:11;
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
		union PM4_MES_TYPE_3_HEADER header; /* header */
		uint32_t ordinal1;
	};

	union {
		struct {
			uint32_t reserved1:2;
			uint32_t ib_base_lo:30;
		} bitfields2;
		uint32_t ordinal2;
	};

	uint32_t ib_base_hi;

	union {
		struct {
			uint32_t ib_size:20;
			uint32_t chain:1;
			uint32_t offload_polling:1;
			uint32_t chained_runlist_idle_disable:1;
			uint32_t valid:1;
			uint32_t process_cnt:4;
			uint32_t reserved3:4;
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
		union PM4_MES_TYPE_3_HEADER header;	/* header */
		uint32_t ordinal1;
	};

	union {
		struct {
			uint32_t pasid:16;		/* 0 - 15  */
			uint32_t reserved1:1;		/* 16      */
			uint32_t exec_cleaner_shader:1;	/* 17      */
			uint32_t debug_vmid:4;
			uint32_t new_debug:1;
			uint32_t reserved2:1;
			uint32_t diq_enable:1;
			uint32_t process_quantum:7;
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

	uint32_t completion_signal_lo;

	uint32_t completion_signal_hi;

};

#endif

/*--------------------MES_MAP_PROCESS_VM--------------------*/

#ifndef PM4_MES_MAP_PROCESS_VM_DEFINED
#define PM4_MES_MAP_PROCESS_VM_DEFINED

struct PM4_MES_MAP_PROCESS_VM {
	union {
		union PM4_MES_TYPE_3_HEADER header;	/* header */
		uint32_t ordinal1;
	};

	uint32_t reserved1;

	uint32_t vm_context_cntl;

	uint32_t reserved2;

	uint32_t vm_context_page_table_end_addr_lo32;

	uint32_t vm_context_page_table_end_addr_hi32;

	uint32_t vm_context_page_table_start_addr_lo32;

	uint32_t vm_context_page_table_start_addr_hi32;

	uint32_t reserved3;

	uint32_t reserved4;

	uint32_t reserved5;

	uint32_t reserved6;

	uint32_t reserved7;

	uint32_t reserved8;

	uint32_t completion_signal_lo32;

	uint32_t completion_signal_hi32;

};
#endif

/*--------------------MES_MAP_QUEUES--------------------*/

#ifndef PM4_MES_MAP_QUEUES_VI_DEFINED
#define PM4_MES_MAP_QUEUES_VI_DEFINED
enum mes_map_queues_queue_sel_enum {
	queue_sel__mes_map_queues__map_to_specified_queue_slots_vi = 0,
queue_sel__mes_map_queues__map_to_hws_determined_queue_slots_vi = 1
};

enum mes_map_queues_queue_type_enum {
	queue_type__mes_map_queues__normal_compute_vi = 0,
	queue_type__mes_map_queues__debug_interface_queue_vi = 1,
	queue_type__mes_map_queues__normal_latency_static_queue_vi = 2,
queue_type__mes_map_queues__low_latency_static_queue_vi = 3
};

enum mes_map_queues_engine_sel_enum {
	engine_sel__mes_map_queues__compute_vi = 0,
	engine_sel__mes_map_queues__sdma0_vi = 2,
	engine_sel__mes_map_queues__sdma1_vi = 3
};

enum mes_map_queues_extended_engine_sel_enum {
	extended_engine_sel__mes_map_queues__legacy_engine_sel = 0,
	extended_engine_sel__mes_map_queues__sdma0_to_7_sel  = 1,
	extended_engine_sel__mes_map_queues__sdma8_to_15_sel = 2
};

struct pm4_mes_map_queues {
	union {
		union PM4_MES_TYPE_3_HEADER   header;            /* header */
		uint32_t            ordinal1;
	};

	union {
		struct {
			uint32_t reserved1:2;
			enum mes_map_queues_extended_engine_sel_enum extended_engine_sel:2;
			enum mes_map_queues_queue_sel_enum queue_sel:2;
			uint32_t reserved5:6;
			uint32_t gws_control_queue:1;
			uint32_t reserved2:8;
			enum mes_map_queues_queue_type_enum queue_type:3;
			uint32_t reserved3:2;
			enum mes_map_queues_engine_sel_enum engine_sel:3;
			uint32_t num_queues:3;
		} bitfields2;
		uint32_t ordinal2;
	};

	union {
		struct {
			uint32_t reserved3:1;
			uint32_t check_disable:1;
			uint32_t doorbell_offset:26;
			uint32_t reserved4:4;
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
			enum mes_query_status_interrupt_sel_enum	interrupt_sel:2;
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
			uint32_t doorbell_offset:26;
			enum mes_query_status_engine_sel_enum engine_sel:3;
			uint32_t reserved3:1;
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

enum mes_unmap_queues_extended_engine_sel_enum {
	extended_engine_sel__mes_unmap_queues__legacy_engine_sel = 0,
	extended_engine_sel__mes_unmap_queues__sdma0_to_7_sel = 1
};

struct pm4_mes_unmap_queues {
	union {
		union PM4_MES_TYPE_3_HEADER   header;            /* header */
		uint32_t            ordinal1;
	};

	union {
		struct {
			enum mes_unmap_queues_action_enum action:2;
			enum mes_unmap_queues_extended_engine_sel_enum extended_engine_sel:2;
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
			uint32_t doorbell_offset0:26;
			int32_t reserved5:4;
		} bitfields3b;
		uint32_t ordinal3;
	};

	union {
	struct {
			uint32_t reserved6:2;
			uint32_t doorbell_offset1:26;
			uint32_t reserved7:4;
		} bitfields4;
		uint32_t ordinal4;
	};

	union {
		struct {
			uint32_t reserved8:2;
			uint32_t doorbell_offset2:26;
			uint32_t reserved9:4;
		} bitfields5;
		uint32_t ordinal5;
	};

	union {
		struct {
			uint32_t reserved10:2;
			uint32_t doorbell_offset3:26;
			uint32_t reserved11:4;
		} bitfields6;
		uint32_t ordinal6;
	};
};
#endif

#ifndef PM4_MEC_RELEASE_MEM_DEFINED
#define PM4_MEC_RELEASE_MEM_DEFINED

enum mec_release_mem_event_index_enum {
	event_index__mec_release_mem__end_of_pipe = 5,
	event_index__mec_release_mem__shader_done = 6
};

enum mec_release_mem_cache_policy_enum {
	cache_policy__mec_release_mem__lru = 0,
	cache_policy__mec_release_mem__stream = 1
};

enum mec_release_mem_pq_exe_status_enum {
	pq_exe_status__mec_release_mem__default = 0,
	pq_exe_status__mec_release_mem__phase_update = 1
};

enum mec_release_mem_dst_sel_enum {
	dst_sel__mec_release_mem__memory_controller = 0,
	dst_sel__mec_release_mem__tc_l2 = 1,
	dst_sel__mec_release_mem__queue_write_pointer_register = 2,
	dst_sel__mec_release_mem__queue_write_pointer_poll_mask_bit = 3
};

enum mec_release_mem_int_sel_enum {
	int_sel__mec_release_mem__none = 0,
	int_sel__mec_release_mem__send_interrupt_only = 1,
	int_sel__mec_release_mem__send_interrupt_after_write_confirm = 2,
	int_sel__mec_release_mem__send_data_after_write_confirm = 3,
	int_sel__mec_release_mem__unconditionally_send_int_ctxid = 4,
	int_sel__mec_release_mem__conditionally_send_int_ctxid_based_on_32_bit_compare = 5,
	int_sel__mec_release_mem__conditionally_send_int_ctxid_based_on_64_bit_compare = 6
};

enum mec_release_mem_data_sel_enum {
	data_sel__mec_release_mem__none = 0,
	data_sel__mec_release_mem__send_32_bit_low = 1,
	data_sel__mec_release_mem__send_64_bit_data = 2,
	data_sel__mec_release_mem__send_gpu_clock_counter = 3,
	data_sel__mec_release_mem__send_cp_perfcounter_hi_lo = 4,
	data_sel__mec_release_mem__store_gds_data_to_memory = 5
};

struct pm4_mec_release_mem {
	union {
		union PM4_MES_TYPE_3_HEADER header;     /*header */
		unsigned int ordinal1;
	};

	union {
		struct {
			unsigned int event_type:6;
			unsigned int reserved1:2;
			enum mec_release_mem_event_index_enum event_index:4;
			unsigned int tcl1_vol_action_ena:1;
			unsigned int tc_vol_action_ena:1;
			unsigned int reserved2:1;
			unsigned int tc_wb_action_ena:1;
			unsigned int tcl1_action_ena:1;
			unsigned int tc_action_ena:1;
			uint32_t reserved3:1;
			uint32_t tc_nc_action_ena:1;
			uint32_t tc_wc_action_ena:1;
			uint32_t tc_md_action_ena:1;
			uint32_t reserved4:3;
			enum mec_release_mem_cache_policy_enum cache_policy:2;
			uint32_t reserved5:2;
			enum mec_release_mem_pq_exe_status_enum pq_exe_status:1;
			uint32_t reserved6:2;
		} bitfields2;
		unsigned int ordinal2;
	};

	union {
		struct {
			uint32_t reserved7:16;
			enum mec_release_mem_dst_sel_enum dst_sel:2;
			uint32_t reserved8:6;
			enum mec_release_mem_int_sel_enum int_sel:3;
			uint32_t reserved9:2;
			enum mec_release_mem_data_sel_enum data_sel:3;
		} bitfields3;
		unsigned int ordinal3;
	};

	union {
		struct {
			uint32_t reserved10:2;
			unsigned int address_lo_32b:30;
		} bitfields4;
		struct {
			uint32_t reserved11:3;
			uint32_t address_lo_64b:29;
		} bitfields4b;
		uint32_t reserved12;
		unsigned int ordinal4;
	};

	union {
		uint32_t address_hi;
		uint32_t reserved13;
		uint32_t ordinal5;
	};

	union {
		uint32_t data_lo;
		uint32_t cmp_data_lo;
		struct {
			uint32_t dw_offset:16;
			uint32_t num_dwords:16;
		} bitfields6c;
		uint32_t reserved14;
		uint32_t ordinal6;
	};

	union {
		uint32_t data_hi;
		uint32_t cmp_data_hi;
		uint32_t reserved15;
		uint32_t reserved16;
		uint32_t ordinal7;
	};

	uint32_t int_ctxid;

};

#endif

#ifndef PM4_MEC_WRITE_DATA_DEFINED
#define PM4_MEC_WRITE_DATA_DEFINED

enum WRITE_DATA_dst_sel_enum {
	dst_sel___write_data__mem_mapped_register = 0,
	dst_sel___write_data__tc_l2 = 2,
	dst_sel___write_data__gds = 3,
	dst_sel___write_data__memory = 5,
	dst_sel___write_data__memory_mapped_adc_persistent_state = 6,
};

enum WRITE_DATA_addr_incr_enum {
	addr_incr___write_data__increment_address = 0,
	addr_incr___write_data__do_not_increment_address = 1
};

enum WRITE_DATA_wr_confirm_enum {
	wr_confirm___write_data__do_not_wait_for_write_confirmation = 0,
	wr_confirm___write_data__wait_for_write_confirmation = 1
};

enum WRITE_DATA_cache_policy_enum {
	cache_policy___write_data__lru = 0,
	cache_policy___write_data__stream = 1
};


struct pm4_mec_write_data_mmio {
	union {
		union PM4_MES_TYPE_3_HEADER header;     /*header */
		unsigned int ordinal1;
	};

	union {
		struct {
			unsigned int reserved1:8;
			unsigned int dst_sel:4;
			unsigned int reserved2:4;
			unsigned int addr_incr:1;
			unsigned int reserved3:2;
			unsigned int resume_vf:1;
			unsigned int wr_confirm:1;
			unsigned int reserved4:4;
			unsigned int cache_policy:2;
			unsigned int reserved5:5;
		} bitfields2;
		unsigned int ordinal2;
	};

	union {
		struct {
			unsigned int dst_mmreg_addr:18;
			unsigned int reserved6:14;
		} bitfields3;
		unsigned int ordinal3;
	};

	uint32_t reserved7;

	uint32_t data;

};

#endif

enum {
	CACHE_FLUSH_AND_INV_TS_EVENT = 0x00000014
};
#endif

