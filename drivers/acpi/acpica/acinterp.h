/******************************************************************************
 *
 * Name: acinterp.h - Interpreter subcomponent prototypes and defines
 *
 *****************************************************************************/

/*
 * Copyright (C) 2000 - 2015, Intel Corp.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions, and the following disclaimer,
 *    without modification.
 * 2. Redistributions in binary form must reproduce at minimum a disclaimer
 *    substantially similar to the "NO WARRANTY" disclaimer below
 *    ("Disclaimer") and any redistribution must be conditioned upon
 *    including a substantially similar Disclaimer requirement for further
 *    binary redistribution.
 * 3. Neither the names of the above-listed copyright holders nor the names
 *    of any contributors may be used to endorse or promote products derived
 *    from this software without specific prior written permission.
 *
 * Alternatively, this software may be distributed under the terms of the
 * GNU General Public License ("GPL") version 2 as published by the Free
 * Software Foundation.
 *
 * NO WARRANTY
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTIBILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * HOLDERS OR CONTRIBUTORS BE LIABLE FOR SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
 * STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING
 * IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGES.
 */

#ifndef __ACINTERP_H__
#define __ACINTERP_H__

#define ACPI_WALK_OPERANDS          (&(walk_state->operands [walk_state->num_operands -1]))

/* Macros for tables used for debug output */

#define ACPI_EXD_OFFSET(f)          (u8) ACPI_OFFSET (union acpi_operand_object,f)
#define ACPI_EXD_NSOFFSET(f)        (u8) ACPI_OFFSET (struct acpi_namespace_node,f)
#define ACPI_EXD_TABLE_SIZE(name)   (sizeof(name) / sizeof (struct acpi_exdump_info))

/*
 * If possible, pack the following structures to byte alignment, since we
 * don't care about performance for debug output. Two cases where we cannot
 * pack the structures:
 *
 * 1) Hardware does not support misaligned memory transfers
 * 2) Compiler does not support pointers within packed structures
 */
#if (!defined(ACPI_MISALIGNMENT_NOT_SUPPORTED) && !defined(ACPI_PACKED_POINTERS_NOT_SUPPORTED))
#pragma pack(1)
#endif

typedef const struct acpi_exdump_info {
	u8 opcode;
	u8 offset;
	char *name;

} acpi_exdump_info;

/* Values for the Opcode field above */

#define ACPI_EXD_INIT                   0
#define ACPI_EXD_TYPE                   1
#define ACPI_EXD_UINT8                  2
#define ACPI_EXD_UINT16                 3
#define ACPI_EXD_UINT32                 4
#define ACPI_EXD_UINT64                 5
#define ACPI_EXD_LITERAL                6
#define ACPI_EXD_POINTER                7
#define ACPI_EXD_ADDRESS                8
#define ACPI_EXD_STRING                 9
#define ACPI_EXD_BUFFER                 10
#define ACPI_EXD_PACKAGE                11
#define ACPI_EXD_FIELD                  12
#define ACPI_EXD_REFERENCE              13
#define ACPI_EXD_LIST                   14	/* Operand object list */
#define ACPI_EXD_HDLR_LIST              15	/* Address Handler list */
#define ACPI_EXD_RGN_LIST               16	/* Region list */
#define ACPI_EXD_NODE                   17	/* Namespace Node */

/* restore default alignment */

#pragma pack()

/*
 * exconvrt - object conversion
 */
acpi_status
acpi_ex_convert_to_integer(union acpi_operand_object *obj_desc,
			   union acpi_operand_object **result_desc, u32 flags);

acpi_status
acpi_ex_convert_to_buffer(union acpi_operand_object *obj_desc,
			  union acpi_operand_object **result_desc);

acpi_status
acpi_ex_convert_to_string(union acpi_operand_object *obj_desc,
			  union acpi_operand_object **result_desc, u32 type);

/* Types for ->String conversion */

#define ACPI_EXPLICIT_BYTE_COPY         0x00000000
#define ACPI_EXPLICIT_CONVERT_HEX       0x00000001
#define ACPI_IMPLICIT_CONVERT_HEX       0x00000002
#define ACPI_EXPLICIT_CONVERT_DECIMAL   0x00000003

acpi_status
acpi_ex_convert_to_target_type(acpi_object_type destination_type,
			       union acpi_operand_object *source_desc,
			       union acpi_operand_object **result_desc,
			       struct acpi_walk_state *walk_state);

/*
 * exdebug - AML debug object
 */
void
acpi_ex_do_debug_object(union acpi_operand_object *source_desc,
			u32 level, u32 index);

/*
 * exfield - ACPI AML (p-code) execution - field manipulation
 */
acpi_status
acpi_ex_common_buffer_setup(union acpi_operand_object *obj_desc,
			    u32 buffer_length, u32 * datum_count);

acpi_status
acpi_ex_write_with_update_rule(union acpi_operand_object *obj_desc,
			       u64 mask,
			       u64 field_value, u32 field_datum_byte_offset);

void
acpi_ex_get_buffer_datum(u64 *datum,
			 void *buffer,
			 u32 buffer_length,
			 u32 byte_granularity, u32 buffer_offset);

void
acpi_ex_set_buffer_datum(u64 merged_datum,
			 void *buffer,
			 u32 buffer_length,
			 u32 byte_granularity, u32 buffer_offset);

acpi_status
acpi_ex_read_data_from_field(struct acpi_walk_state *walk_state,
			     union acpi_operand_object *obj_desc,
			     union acpi_operand_object **ret_buffer_desc);

acpi_status
acpi_ex_write_data_to_field(union acpi_operand_object *source_desc,
			    union acpi_operand_object *obj_desc,
			    union acpi_operand_object **result_desc);

/*
 * exfldio - low level field I/O
 */
acpi_status
acpi_ex_extract_from_field(union acpi_operand_object *obj_desc,
			   void *buffer, u32 buffer_length);

acpi_status
acpi_ex_insert_into_field(union acpi_operand_object *obj_desc,
			  void *buffer, u32 buffer_length);

acpi_status
acpi_ex_access_region(union acpi_operand_object *obj_desc,
		      u32 field_datum_byte_offset, u64 *value, u32 read_write);

/*
 * exmisc - misc support routines
 */
acpi_status
acpi_ex_get_object_reference(union acpi_operand_object *obj_desc,
			     union acpi_operand_object **return_desc,
			     struct acpi_walk_state *walk_state);

acpi_status
acpi_ex_concat_template(union acpi_operand_object *obj_desc,
			union acpi_operand_object *obj_desc2,
			union acpi_operand_object **actual_return_desc,
			struct acpi_walk_state *walk_state);

acpi_status
acpi_ex_do_concatenate(union acpi_operand_object *obj_desc,
		       union acpi_operand_object *obj_desc2,
		       union acpi_operand_object **actual_return_desc,
		       struct acpi_walk_state *walk_state);

acpi_status
acpi_ex_do_logical_numeric_op(u16 opcode,
			      u64 integer0, u64 integer1, u8 *logical_result);

acpi_status
acpi_ex_do_logical_op(u16 opcode,
		      union acpi_operand_object *operand0,
		      union acpi_operand_object *operand1, u8 *logical_result);

u64 acpi_ex_do_math_op(u16 opcode, u64 operand0, u64 operand1);

acpi_status acpi_ex_create_mutex(struct acpi_walk_state *walk_state);

acpi_status acpi_ex_create_processor(struct acpi_walk_state *walk_state);

acpi_status acpi_ex_create_power_resource(struct acpi_walk_state *walk_state);

acpi_status
acpi_ex_create_region(u8 * aml_start,
		      u32 aml_length,
		      u8 region_space, struct acpi_walk_state *walk_state);

acpi_status acpi_ex_create_event(struct acpi_walk_state *walk_state);

acpi_status acpi_ex_create_alias(struct acpi_walk_state *walk_state);

acpi_status
acpi_ex_create_method(u8 * aml_start,
		      u32 aml_length, struct acpi_walk_state *walk_state);

/*
 * exconfig - dynamic table load/unload
 */
acpi_status
acpi_ex_load_op(union acpi_operand_object *obj_desc,
		union acpi_operand_object *target,
		struct acpi_walk_state *walk_state);

acpi_status
acpi_ex_load_table_op(struct acpi_walk_state *walk_state,
		      union acpi_operand_object **return_desc);

acpi_status acpi_ex_unload_table(union acpi_operand_object *ddb_handle);

/*
 * exmutex - mutex support
 */
acpi_status
acpi_ex_acquire_mutex(union acpi_operand_object *time_desc,
		      union acpi_operand_object *obj_desc,
		      struct acpi_walk_state *walk_state);

acpi_status
acpi_ex_acquire_mutex_object(u16 timeout,
			     union acpi_operand_object *obj_desc,
			     acpi_thread_id thread_id);

acpi_status
acpi_ex_release_mutex(union acpi_operand_object *obj_desc,
		      struct acpi_walk_state *walk_state);

acpi_status acpi_ex_release_mutex_object(union acpi_operand_object *obj_desc);

void acpi_ex_release_all_mutexes(struct acpi_thread_state *thread);

void acpi_ex_unlink_mutex(union acpi_operand_object *obj_desc);

/*
 * exprep - ACPI AML execution - prep utilities
 */
acpi_status
acpi_ex_prep_common_field_object(union acpi_operand_object *obj_desc,
				 u8 field_flags,
				 u8 field_attribute,
				 u32 field_bit_position, u32 field_bit_length);

acpi_status acpi_ex_prep_field_value(struct acpi_create_field_info *info);

/*
 * exsystem - Interface to OS services
 */
acpi_status
acpi_ex_system_do_notify_op(union acpi_operand_object *value,
			    union acpi_operand_object *obj_desc);

acpi_status acpi_ex_system_do_sleep(u64 time);

acpi_status acpi_ex_system_do_stall(u32 time);

acpi_status acpi_ex_system_signal_event(union acpi_operand_object *obj_desc);

acpi_status
acpi_ex_system_wait_event(union acpi_operand_object *time,
			  union acpi_operand_object *obj_desc);

acpi_status acpi_ex_system_reset_event(union acpi_operand_object *obj_desc);

acpi_status
acpi_ex_system_wait_semaphore(acpi_semaphore semaphore, u16 timeout);

acpi_status acpi_ex_system_wait_mutex(acpi_mutex mutex, u16 timeout);

/*
 * exoparg1 - ACPI AML execution, 1 operand
 */
acpi_status acpi_ex_opcode_0A_0T_1R(struct acpi_walk_state *walk_state);

acpi_status acpi_ex_opcode_1A_0T_0R(struct acpi_walk_state *walk_state);

acpi_status acpi_ex_opcode_1A_0T_1R(struct acpi_walk_state *walk_state);

acpi_status acpi_ex_opcode_1A_1T_1R(struct acpi_walk_state *walk_state);

acpi_status acpi_ex_opcode_1A_1T_0R(struct acpi_walk_state *walk_state);

/*
 * exoparg2 - ACPI AML execution, 2 operands
 */
acpi_status acpi_ex_opcode_2A_0T_0R(struct acpi_walk_state *walk_state);

acpi_status acpi_ex_opcode_2A_0T_1R(struct acpi_walk_state *walk_state);

acpi_status acpi_ex_opcode_2A_1T_1R(struct acpi_walk_state *walk_state);

acpi_status acpi_ex_opcode_2A_2T_1R(struct acpi_walk_state *walk_state);

/*
 * exoparg3 - ACPI AML execution, 3 operands
 */
acpi_status acpi_ex_opcode_3A_0T_0R(struct acpi_walk_state *walk_state);

acpi_status acpi_ex_opcode_3A_1T_1R(struct acpi_walk_state *walk_state);

/*
 * exoparg6 - ACPI AML execution, 6 operands
 */
acpi_status acpi_ex_opcode_6A_0T_1R(struct acpi_walk_state *walk_state);

/*
 * exresolv - Object resolution and get value functions
 */
acpi_status
acpi_ex_resolve_to_value(union acpi_operand_object **stack_ptr,
			 struct acpi_walk_state *walk_state);

acpi_status
acpi_ex_resolve_multiple(struct acpi_walk_state *walk_state,
			 union acpi_operand_object *operand,
			 acpi_object_type * return_type,
			 union acpi_operand_object **return_desc);

/*
 * exresnte - resolve namespace node
 */
acpi_status
acpi_ex_resolve_node_to_value(struct acpi_namespace_node **stack_ptr,
			      struct acpi_walk_state *walk_state);

/*
 * exresop - resolve operand to value
 */
acpi_status
acpi_ex_resolve_operands(u16 opcode,
			 union acpi_operand_object **stack_ptr,
			 struct acpi_walk_state *walk_state);

/*
 * exdump - Interpreter debug output routines
 */
void acpi_ex_dump_operand(union acpi_operand_object *obj_desc, u32 depth);

void
acpi_ex_dump_operands(union acpi_operand_object **operands,
		      const char *opcode_name, u32 num_opcodes);

#ifdef	ACPI_FUTURE_USAGE
void
acpi_ex_dump_object_descriptor(union acpi_operand_object *object, u32 flags);

void acpi_ex_dump_namespace_node(struct acpi_namespace_node *node, u32 flags);
#endif				/* ACPI_FUTURE_USAGE */

/*
 * exnames - AML namestring support
 */
acpi_status
acpi_ex_get_name_string(acpi_object_type data_type,
			u8 * in_aml_address,
			char **out_name_string, u32 * out_name_length);

/*
 * exstore - Object store support
 */
acpi_status
acpi_ex_store(union acpi_operand_object *val_desc,
	      union acpi_operand_object *dest_desc,
	      struct acpi_walk_state *walk_state);

acpi_status
acpi_ex_store_object_to_node(union acpi_operand_object *source_desc,
			     struct acpi_namespace_node *node,
			     struct acpi_walk_state *walk_state,
			     u8 implicit_conversion);

#define ACPI_IMPLICIT_CONVERSION        TRUE
#define ACPI_NO_IMPLICIT_CONVERSION     FALSE

/*
 * exstoren - resolve/store object
 */
acpi_status
acpi_ex_resolve_object(union acpi_operand_object **source_desc_ptr,
		       acpi_object_type target_type,
		       struct acpi_walk_state *walk_state);

acpi_status
acpi_ex_store_object_to_object(union acpi_operand_object *source_desc,
			       union acpi_operand_object *dest_desc,
			       union acpi_operand_object **new_desc,
			       struct acpi_walk_state *walk_state);

/*
 * exstorob - store object - buffer/string
 */
acpi_status
acpi_ex_store_buffer_to_buffer(union acpi_operand_object *source_desc,
			       union acpi_operand_object *target_desc);

acpi_status
acpi_ex_store_string_to_string(union acpi_operand_object *source_desc,
			       union acpi_operand_object *target_desc);

/*
 * excopy - object copy
 */
acpi_status
acpi_ex_copy_integer_to_index_field(union acpi_operand_object *source_desc,
				    union acpi_operand_object *target_desc);

acpi_status
acpi_ex_copy_integer_to_bank_field(union acpi_operand_object *source_desc,
				   union acpi_operand_object *target_desc);

acpi_status
acpi_ex_copy_data_to_named_field(union acpi_operand_object *source_desc,
				 struct acpi_namespace_node *node);

acpi_status
acpi_ex_copy_integer_to_buffer_field(union acpi_operand_object *source_desc,
				     union acpi_operand_object *target_desc);

/*
 * exutils - interpreter/scanner utilities
 */
void acpi_ex_enter_interpreter(void);

void acpi_ex_exit_interpreter(void);

u8 acpi_ex_truncate_for32bit_table(union acpi_operand_object *obj_desc);

void acpi_ex_acquire_global_lock(u32 rule);

void acpi_ex_release_global_lock(u32 rule);

void acpi_ex_eisa_id_to_string(char *dest, u64 compressed_id);

void acpi_ex_integer_to_string(char *dest, u64 value);

void acpi_ex_pci_cls_to_string(char *dest, u8 class_code[3]);

u8 acpi_is_valid_space_id(u8 space_id);

/*
 * exregion - default op_region handlers
 */
acpi_status
acpi_ex_system_memory_space_handler(u32 function,
				    acpi_physical_address address,
				    u32 bit_width,
				    u64 *value,
				    void *handler_context,
				    void *region_context);

acpi_status
acpi_ex_system_io_space_handler(u32 function,
				acpi_physical_address address,
				u32 bit_width,
				u64 *value,
				void *handler_context, void *region_context);

acpi_status
acpi_ex_pci_config_space_handler(u32 function,
				 acpi_physical_address address,
				 u32 bit_width,
				 u64 *value,
				 void *handler_context, void *region_context);

acpi_status
acpi_ex_cmos_space_handler(u32 function,
			   acpi_physical_address address,
			   u32 bit_width,
			   u64 *value,
			   void *handler_context, void *region_context);

acpi_status
acpi_ex_pci_bar_space_handler(u32 function,
			      acpi_physical_address address,
			      u32 bit_width,
			      u64 *value,
			      void *handler_context, void *region_context);

acpi_status
acpi_ex_embedded_controller_space_handler(u32 function,
					  acpi_physical_address address,
					  u32 bit_width,
					  u64 *value,
					  void *handler_context,
					  void *region_context);

acpi_status
acpi_ex_sm_bus_space_handler(u32 function,
			     acpi_physical_address address,
			     u32 bit_width,
			     u64 *value,
			     void *handler_context, void *region_context);

acpi_status
acpi_ex_data_table_space_handler(u32 function,
				 acpi_physical_address address,
				 u32 bit_width,
				 u64 *value,
				 void *handler_context, void *region_context);

#endif				/* __INTERP_H__ */
