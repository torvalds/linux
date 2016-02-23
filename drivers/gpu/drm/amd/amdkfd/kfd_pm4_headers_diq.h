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

#ifndef KFD_PM4_HEADERS_DIQ_H_
#define KFD_PM4_HEADERS_DIQ_H_

/*--------------------_INDIRECT_BUFFER-------------------- */

#ifndef _PM4__INDIRECT_BUFFER_DEFINED
#define _PM4__INDIRECT_BUFFER_DEFINED
enum _INDIRECT_BUFFER_cache_policy_enum {
	cache_policy___indirect_buffer__lru = 0,
	cache_policy___indirect_buffer__stream = 1,
	cache_policy___indirect_buffer__bypass = 2
};

enum {
	IT_INDIRECT_BUFFER_PASID = 0x5C
};

struct pm4__indirect_buffer_pasid {
	union {
		union PM4_MES_TYPE_3_HEADER header;	/* header */
		unsigned int ordinal1;
	};

	union {
		struct {
			unsigned int reserved1:2;
			unsigned int ib_base_lo:30;
		} bitfields2;
		unsigned int ordinal2;
	};

	union {
		struct {
			unsigned int ib_base_hi:16;
			unsigned int reserved2:16;
		} bitfields3;
		unsigned int ordinal3;
	};

	union {
		unsigned int control;
		unsigned int ordinal4;
	};

	union {
		struct {
			unsigned int pasid:10;
			unsigned int reserved4:22;
		} bitfields5;
		unsigned int ordinal5;
	};

};

#endif

/*--------------------_RELEASE_MEM-------------------- */

#ifndef _PM4__RELEASE_MEM_DEFINED
#define _PM4__RELEASE_MEM_DEFINED
enum _RELEASE_MEM_event_index_enum {
	event_index___release_mem__end_of_pipe = 5,
	event_index___release_mem__shader_done = 6
};

enum _RELEASE_MEM_cache_policy_enum {
	cache_policy___release_mem__lru = 0,
	cache_policy___release_mem__stream = 1,
	cache_policy___release_mem__bypass = 2
};

enum _RELEASE_MEM_dst_sel_enum {
	dst_sel___release_mem__memory_controller = 0,
	dst_sel___release_mem__tc_l2 = 1,
	dst_sel___release_mem__queue_write_pointer_register = 2,
	dst_sel___release_mem__queue_write_pointer_poll_mask_bit = 3
};

enum _RELEASE_MEM_int_sel_enum {
	int_sel___release_mem__none = 0,
	int_sel___release_mem__send_interrupt_only = 1,
	int_sel___release_mem__send_interrupt_after_write_confirm = 2,
	int_sel___release_mem__send_data_after_write_confirm = 3
};

enum _RELEASE_MEM_data_sel_enum {
	data_sel___release_mem__none = 0,
	data_sel___release_mem__send_32_bit_low = 1,
	data_sel___release_mem__send_64_bit_data = 2,
	data_sel___release_mem__send_gpu_clock_counter = 3,
	data_sel___release_mem__send_cp_perfcounter_hi_lo = 4,
	data_sel___release_mem__store_gds_data_to_memory = 5
};

struct pm4__release_mem {
	union {
		union PM4_MES_TYPE_3_HEADER header;	/*header */
		unsigned int ordinal1;
	};

	union {
		struct {
			unsigned int event_type:6;
			unsigned int reserved1:2;
			enum _RELEASE_MEM_event_index_enum event_index:4;
			unsigned int tcl1_vol_action_ena:1;
			unsigned int tc_vol_action_ena:1;
			unsigned int reserved2:1;
			unsigned int tc_wb_action_ena:1;
			unsigned int tcl1_action_ena:1;
			unsigned int tc_action_ena:1;
			unsigned int reserved3:6;
			unsigned int atc:1;
			enum _RELEASE_MEM_cache_policy_enum cache_policy:2;
			unsigned int reserved4:5;
		} bitfields2;
		unsigned int ordinal2;
	};

	union {
		struct {
			unsigned int reserved5:16;
			enum _RELEASE_MEM_dst_sel_enum dst_sel:2;
			unsigned int reserved6:6;
			enum _RELEASE_MEM_int_sel_enum int_sel:3;
			unsigned int reserved7:2;
			enum _RELEASE_MEM_data_sel_enum data_sel:3;
		} bitfields3;
		unsigned int ordinal3;
	};

	union {
		struct {
			unsigned int reserved8:2;
			unsigned int address_lo_32b:30;
		} bitfields4;
		struct {
			unsigned int reserved9:3;
			unsigned int address_lo_64b:29;
		} bitfields5;
		unsigned int ordinal4;
	};

	unsigned int address_hi;

	unsigned int data_lo;

	unsigned int data_hi;

};
#endif


/*--------------------_SET_CONFIG_REG-------------------- */

#ifndef _PM4__SET_CONFIG_REG_DEFINED
#define _PM4__SET_CONFIG_REG_DEFINED

struct pm4__set_config_reg {
	union {
		union PM4_MES_TYPE_3_HEADER header;	/*header */
		unsigned int ordinal1;
	};

	union {
		struct {
			unsigned int reg_offset:16;
			unsigned int reserved1:7;
			unsigned int vmid_shift:5;
			unsigned int insert_vmid:1;
			unsigned int reserved2:3;
		} bitfields2;
		unsigned int ordinal2;
	};

	unsigned int reg_data[1];	/*1..N of these fields */

};
#endif

/*--------------------_WAIT_REG_MEM-------------------- */

#ifndef _PM4__WAIT_REG_MEM_DEFINED
#define _PM4__WAIT_REG_MEM_DEFINED
enum _WAIT_REG_MEM_function_enum {
	function___wait_reg_mem__always_pass = 0,
	function___wait_reg_mem__less_than_ref_value = 1,
	function___wait_reg_mem__less_than_equal_to_the_ref_value = 2,
	function___wait_reg_mem__equal_to_the_reference_value = 3,
	function___wait_reg_mem__not_equal_reference_value = 4,
	function___wait_reg_mem__greater_than_or_equal_reference_value = 5,
	function___wait_reg_mem__greater_than_reference_value = 6,
	function___wait_reg_mem__reserved = 7
};

enum _WAIT_REG_MEM_mem_space_enum {
	mem_space___wait_reg_mem__register_space = 0,
	mem_space___wait_reg_mem__memory_space = 1
};

enum _WAIT_REG_MEM_operation_enum {
	operation___wait_reg_mem__wait_reg_mem = 0,
	operation___wait_reg_mem__wr_wait_wr_reg = 1
};

struct pm4__wait_reg_mem {
	union {
		union PM4_MES_TYPE_3_HEADER header;	/*header */
		unsigned int ordinal1;
	};

	union {
		struct {
			enum _WAIT_REG_MEM_function_enum function:3;
			unsigned int reserved1:1;
			enum _WAIT_REG_MEM_mem_space_enum mem_space:2;
			enum _WAIT_REG_MEM_operation_enum operation:2;
			unsigned int reserved2:24;
		} bitfields2;
		unsigned int ordinal2;
	};

	union {
		struct {
			unsigned int reserved3:2;
			unsigned int memory_poll_addr_lo:30;
		} bitfields3;
		struct {
			unsigned int register_poll_addr:16;
			unsigned int reserved4:16;
		} bitfields4;
		struct {
			unsigned int register_write_addr:16;
			unsigned int reserved5:16;
		} bitfields5;
		unsigned int ordinal3;
	};

	union {
		struct {
			unsigned int poll_address_hi:16;
			unsigned int reserved6:16;
		} bitfields6;
		struct {
			unsigned int register_write_addr:16;
			unsigned int reserved7:16;
		} bitfields7;
		unsigned int ordinal4;
	};

	unsigned int reference;

	unsigned int mask;

	union {
		struct {
			unsigned int poll_interval:16;
			unsigned int reserved8:16;
		} bitfields8;
		unsigned int ordinal7;
	};

};
#endif


#endif /* KFD_PM4_HEADERS_DIQ_H_ */
