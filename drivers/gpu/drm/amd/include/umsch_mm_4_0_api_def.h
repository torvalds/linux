/* SPDX-License-Identifier: MIT */
/*
 * Copyright 2023 Advanced Micro Devices, Inc.
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

#ifndef __UMSCH_MM_API_DEF_H__
#define __UMSCH_MM_API_DEF_H__

#pragma once

#pragma pack(push, 4)

#define UMSCH_API_VERSION 1

/*
 * Driver submits one API(cmd) as a single Frame and this command size is same for all API
 * to ease the debugging and parsing of ring buffer.
 */
enum { API_FRAME_SIZE_IN_DWORDS = 64 };

/*
 * To avoid command in scheduler context to be overwritten whenever multiple interrupts come in,
 * this creates another queue.
 */
enum { API_NUMBER_OF_COMMAND_MAX = 32 };

enum { UMSCH_INSTANCE_DB_OFFSET_MAX = 16 };

enum UMSCH_API_TYPE {
	UMSCH_API_TYPE_SCHEDULER = 1,
	UMSCH_API_TYPE_MAX
};

enum UMSCH_MS_LOG_CONTEXT_STATE {
	UMSCH_LOG_CONTEXT_STATE_IDLE = 0,
	UMSCH_LOG_CONTEXT_STATE_RUNNING = 1,
	UMSCH_LOG_CONTEXT_STATE_READY = 2,
	UMSCH_LOG_CONTEXT_STATE_READY_STANDBY = 3,
	UMSCH_LOG_CONTEXT_STATE_INVALID = 0xF,
};

enum UMSCH_MS_LOG_OPERATION {
	UMSCH_LOG_OPERATION_CONTEXT_STATE_CHANGE = 0,
	UMSCH_LOG_OPERATION_QUEUE_NEW_WORK = 1,
	UMSCH_LOG_OPERATION_QUEUE_UNWAIT_SYNC_OBJECT = 2,
	UMSCH_LOG_OPERATION_QUEUE_NO_MORE_WORK = 3,
	UMSCH_LOG_OPERATION_QUEUE_WAIT_SYNC_OBJECT = 4,
	UMSCH_LOG_OPERATION_QUEUE_INVALID = 0xF,
};

struct UMSCH_INSTANCE_DB_OFFSET {
	uint32_t instance_index;
	uint32_t doorbell_offset;
};

struct UMSCH_LOG_CONTEXT_STATE_CHANGE {
	uint64_t h_context;
	enum UMSCH_MS_LOG_CONTEXT_STATE new_context_state;
};

struct UMSCH_LOG_QUEUE_NEW_WORK {
	uint64_t h_queue;
	uint64_t reserved;
};

struct UMSCH_LOG_QUEUE_UNWAIT_SYNC_OBJECT {
	uint64_t h_queue;
	uint64_t h_sync_object;
};

struct UMSCH_LOG_QUEUE_NO_MORE_WORK {
	uint64_t h_queue;
	uint64_t reserved;
};

struct UMSCH_LOG_QUEUE_WAIT_SYNC_OBJECT {
	uint64_t h_queue;
	uint64_t h_sync_object;
};

struct UMSCH_LOG_ENTRY_HEADER {
	uint32_t first_free_entry_index;
	uint32_t wraparound_count;
	uint64_t number_of_entries;
	uint64_t reserved[2];
};

struct UMSCH_LOG_ENTRY_DATA {
	uint64_t gpu_time_stamp;
	uint32_t operation_type; /* operation_type is of UMSCH_LOG_OPERATION type */
	uint32_t reserved_operation_type_bits;
	union {
		struct UMSCH_LOG_CONTEXT_STATE_CHANGE context_state_change;
		struct UMSCH_LOG_QUEUE_NEW_WORK queue_new_work;
		struct UMSCH_LOG_QUEUE_UNWAIT_SYNC_OBJECT queue_unwait_sync_object;
		struct UMSCH_LOG_QUEUE_NO_MORE_WORK queue_no_more_work;
		struct UMSCH_LOG_QUEUE_WAIT_SYNC_OBJECT queue_wait_sync_object;
		uint64_t all[2];
	};
};

struct UMSCH_LOG_BUFFER {
	struct UMSCH_LOG_ENTRY_HEADER header;
	struct UMSCH_LOG_ENTRY_DATA entries[1];
};

enum UMSCH_API_OPCODE {
	UMSCH_API_SET_HW_RSRC = 0x00,
	UMSCH_API_SET_SCHEDULING_CONFIG = 0x1,
	UMSCH_API_ADD_QUEUE = 0x2,
	UMSCH_API_REMOVE_QUEUE = 0x3,
	UMSCH_API_PERFORM_YIELD = 0x4,
	UMSCH_API_SUSPEND = 0x5,
	UMSCH_API_RESUME = 0x6,
	UMSCH_API_RESET = 0x7,
	UMSCH_API_SET_LOG_BUFFER = 0x8,
	UMSCH_API_CHANGE_CONTEXT_PRIORITY = 0x9,
	UMSCH_API_QUERY_SCHEDULER_STATUS = 0xA,
	UMSCH_API_UPDATE_AFFINITY = 0xB,
	UMSCH_API_MAX = 0xFF
};

union UMSCH_API_HEADER {
	struct {
		uint32_t type : 4; /* 0 - Invalid; 1 - Scheduling; 2 - TBD */
		uint32_t opcode : 8;
		uint32_t dwsize : 8;
		uint32_t reserved : 12;
	};

	uint32_t u32All;
};

enum UMSCH_AMD_PRIORITY_LEVEL {
	AMD_PRIORITY_LEVEL_IDLE = 0,
	AMD_PRIORITY_LEVEL_NORMAL = 1,
	AMD_PRIORITY_LEVEL_FOCUS = 2,
	AMD_PRIORITY_LEVEL_REALTIME = 3,
	AMD_PRIORITY_NUM_LEVELS
};

enum UMSCH_ENGINE_TYPE {
	UMSCH_ENGINE_TYPE_VCN0 = 0,
	UMSCH_ENGINE_TYPE_VCN1 = 1,
	UMSCH_ENGINE_TYPE_VCN = 2,
	UMSCH_ENGINE_TYPE_VPE = 3,
	UMSCH_ENGINE_TYPE_MAX
};

#define AFFINITY_DISABLE 0
#define AFFINITY_ENABLE 1
#define AFFINITY_MAX 2

union UMSCH_AFFINITY {
	struct {
		unsigned int vcn0Affinity : 2; /* enable 1 disable 0 */
		unsigned int vcn1Affinity : 2;
		unsigned int reserved : 28;
	};
	unsigned int u32All;
};

struct UMSCH_API_STATUS {
	uint64_t api_completion_fence_addr;
	uint32_t api_completion_fence_value;
};

enum { MAX_VCN0_INSTANCES = 1 };
enum { MAX_VCN1_INSTANCES = 1 };
enum { MAX_VCN_INSTANCES = 2 };

enum { MAX_VPE_INSTANCES = 1 };

enum { MAX_VCN_QUEUES = 4 };
enum { MAX_VPE_QUEUES = 8 };

enum { MAX_QUEUES_IN_A_CONTEXT = 1 };

enum { UMSCH_MAX_HWIP_SEGMENT = 8 };

enum VM_HUB_TYPE {
	VM_HUB_TYPE_GC = 0,
	VM_HUB_TYPE_MM = 1,
	VM_HUB_TYPE_MAX,
};

enum { VMID_INVALID = 0xffff };

enum { MAX_VMID_MMHUB = 16 };

union UMSCHAPI__SET_HW_RESOURCES {
	struct {
		union UMSCH_API_HEADER header;
		uint32_t vmid_mask_mm_vcn;
		uint32_t vmid_mask_mm_vpe;
		uint32_t collaboration_mask_vpe;
		uint32_t engine_mask;
		uint32_t logging_vmid;
		uint32_t vcn0_hqd_mask[MAX_VCN0_INSTANCES];
		uint32_t vcn1_hqd_mask[MAX_VCN1_INSTANCES];
		uint32_t vcn_hqd_mask[MAX_VCN_INSTANCES];
		uint32_t vpe_hqd_mask[MAX_VPE_INSTANCES];
		uint64_t g_sch_ctx_gpu_mc_ptr;
		uint32_t mmhub_base[UMSCH_MAX_HWIP_SEGMENT];
		uint32_t mmhub_version;
		uint32_t osssys_base[UMSCH_MAX_HWIP_SEGMENT];
		uint32_t osssys_version;
		uint32_t vcn_version;
		uint32_t vpe_version;
		struct UMSCH_API_STATUS api_status;
		union {
			struct {
				uint32_t disable_reset : 1;
				uint32_t disable_umsch_log : 1;
				uint32_t enable_level_process_quantum_check : 1;
				uint32_t is_vcn0_enabled : 1;
				uint32_t is_vcn1_enabled : 1;
				uint32_t use_rs64mem_for_proc_ctx_csa : 1;
				uint32_t reserved : 26;
			};
			uint32_t uint32_all;
		};
	};

	uint32_t max_dwords_in_api[API_FRAME_SIZE_IN_DWORDS];
};
static_assert(sizeof(union UMSCHAPI__SET_HW_RESOURCES) <= API_FRAME_SIZE_IN_DWORDS * sizeof(uint32_t),
			  "size of UMSCHAPI__SET_HW_RESOURCES must be less than 256 bytes");

union UMSCHAPI__SET_SCHEDULING_CONFIG {
	struct {
		union UMSCH_API_HEADER header;
		/*
		 * Grace period when preempting another priority band for this priority band.
		 * The value for idle priority band is ignored, as it never preempts other bands.
		 */
		uint64_t grace_period_other_levels[AMD_PRIORITY_NUM_LEVELS];

		/* Default quantum for scheduling across processes within a priority band. */
		uint64_t process_quantum_for_level[AMD_PRIORITY_NUM_LEVELS];

		/* Default grace period for processes that preempt each other within a priority band. */
		uint64_t process_grace_period_same_level[AMD_PRIORITY_NUM_LEVELS];

		/*
		 * For normal level this field specifies the target GPU percentage in situations
		 * when it's starved by the high level. Valid values are between 0 and 50,
		 * with the default being 10.
		 */
		uint32_t normal_yield_percent;

		struct UMSCH_API_STATUS api_status;
	};

	uint32_t max_dwords_in_api[API_FRAME_SIZE_IN_DWORDS];
};

union UMSCHAPI__ADD_QUEUE {
	struct {
		union UMSCH_API_HEADER header;
		uint32_t process_id;
		uint64_t page_table_base_addr;
		uint64_t process_va_start;
		uint64_t process_va_end;
		uint64_t process_quantum;
		uint64_t process_csa_addr;
		uint64_t context_quantum;
		uint64_t context_csa_addr;
		uint32_t inprocess_context_priority;
		enum UMSCH_AMD_PRIORITY_LEVEL context_global_priority_level;
		uint32_t doorbell_offset_0;
		uint32_t doorbell_offset_1;
		union UMSCH_AFFINITY affinity;
		uint64_t mqd_addr;
		uint64_t h_context;
		uint64_t h_queue;
		enum UMSCH_ENGINE_TYPE engine_type;
		uint32_t vm_context_cntl;

		struct {
			uint32_t is_context_suspended : 1;
			uint32_t collaboration_mode : 1;
			uint32_t reserved : 30;
		};
		struct UMSCH_API_STATUS api_status;
		uint32_t process_csa_array_index;
		uint32_t context_csa_array_index;
	};

	uint32_t max_dwords_in_api[API_FRAME_SIZE_IN_DWORDS];
};


union UMSCHAPI__REMOVE_QUEUE {
	struct {
		union UMSCH_API_HEADER header;
		uint32_t doorbell_offset_0;
		uint32_t doorbell_offset_1;
		uint64_t context_csa_addr;

		struct UMSCH_API_STATUS api_status;
		uint32_t context_csa_array_index;
	};

	uint32_t max_dwords_in_api[API_FRAME_SIZE_IN_DWORDS];
};

union UMSCHAPI__PERFORM_YIELD {
	struct {
		union UMSCH_API_HEADER header;
		uint32_t dummy;
		struct UMSCH_API_STATUS api_status;
	};

	uint32_t max_dwords_in_api[API_FRAME_SIZE_IN_DWORDS];
};

union UMSCHAPI__SUSPEND {
	struct {
		union UMSCH_API_HEADER header;
		uint64_t context_csa_addr;
		uint64_t suspend_fence_addr;
		uint32_t suspend_fence_value;

		struct UMSCH_API_STATUS api_status;
		uint32_t context_csa_array_index;
	};

	uint32_t max_dwords_in_api[API_FRAME_SIZE_IN_DWORDS];
};

enum UMSCH_RESUME_OPTION {
	CONTEXT_RESUME = 0,
	ENGINE_SCHEDULE_RESUME = 1,
};

union UMSCHAPI__RESUME {
	struct {
		union UMSCH_API_HEADER header;

		enum UMSCH_RESUME_OPTION resume_option;
		uint64_t context_csa_addr; /* valid only for UMSCH_SWIP_CONTEXT_RESUME */
		enum UMSCH_ENGINE_TYPE engine_type;

		struct UMSCH_API_STATUS api_status;
		uint32_t context_csa_array_index;
	};

	uint32_t max_dwords_in_api[API_FRAME_SIZE_IN_DWORDS];
};

enum UMSCH_RESET_OPTION {
	HANG_DETECT_AND_RESET = 0,
	HANG_DETECT_ONLY = 1,
};

union UMSCHAPI__RESET {
	struct {
		union UMSCH_API_HEADER header;

		enum UMSCH_RESET_OPTION reset_option;
		uint64_t doorbell_offset_addr;
		enum UMSCH_ENGINE_TYPE engine_type;

		struct UMSCH_API_STATUS api_status;
	};

	uint32_t max_dwords_in_api[API_FRAME_SIZE_IN_DWORDS];
};

union UMSCHAPI__SET_LOGGING_BUFFER {
	struct {
		union UMSCH_API_HEADER header;
		/* There are separate log buffers for each queue type */
		enum UMSCH_ENGINE_TYPE log_type;
		/* Log buffer GPU Address */
		uint64_t logging_buffer_addr;
		/* Number of entries in the log buffer */
		uint32_t number_of_entries;
		/* Entry index at which CPU interrupt needs to be signalled */
		uint32_t interrupt_entry;

		struct UMSCH_API_STATUS api_status;
	};

	uint32_t max_dwords_in_api[API_FRAME_SIZE_IN_DWORDS];
};

union UMSCHAPI__UPDATE_AFFINITY {
	struct {
		union UMSCH_API_HEADER header;
		union UMSCH_AFFINITY affinity;
		uint64_t context_csa_addr;
		struct UMSCH_API_STATUS api_status;
		uint32_t context_csa_array_index;
	};

	uint32_t max_dwords_in_api[API_FRAME_SIZE_IN_DWORDS];
};

union UMSCHAPI__CHANGE_CONTEXT_PRIORITY_LEVEL {
	struct {
		union UMSCH_API_HEADER header;
		uint32_t inprocess_context_priority;
		enum UMSCH_AMD_PRIORITY_LEVEL context_global_priority_level;
		uint64_t context_quantum;
		uint64_t context_csa_addr;
		struct UMSCH_API_STATUS api_status;
		uint32_t context_csa_array_index;
	};

	uint32_t max_dwords_in_api[API_FRAME_SIZE_IN_DWORDS];
};

union UMSCHAPI__QUERY_UMSCH_STATUS {
	struct {
		union UMSCH_API_HEADER header;
		bool umsch_mm_healthy; /* 0 - not healthy, 1 - healthy */
		struct UMSCH_API_STATUS api_status;
	};

	uint32_t max_dwords_in_api[API_FRAME_SIZE_IN_DWORDS];
};

#pragma pack(pop)

#endif
