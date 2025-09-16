/* SPDX-License-Identifier: BSD-3-Clause OR GPL-2.0 */
/******************************************************************************
 *
 * Name: acdispat.h - dispatcher (parser to interpreter interface)
 *
 * Copyright (C) 2000 - 2025, Intel Corp.
 *
 *****************************************************************************/

#ifndef _ACDISPAT_H_
#define _ACDISPAT_H_

#define NAMEOF_LOCAL_NTE    "__L0"
#define NAMEOF_ARG_NTE      "__A0"

/*
 * dsargs - execution of dynamic arguments for static objects
 */
acpi_status
acpi_ds_get_buffer_field_arguments(union acpi_operand_object *obj_desc);

acpi_status
acpi_ds_get_bank_field_arguments(union acpi_operand_object *obj_desc);

acpi_status acpi_ds_get_region_arguments(union acpi_operand_object *rgn_desc);

acpi_status acpi_ds_get_buffer_arguments(union acpi_operand_object *obj_desc);

acpi_status acpi_ds_get_package_arguments(union acpi_operand_object *obj_desc);

/*
 * dscontrol - support for execution control opcodes
 */
acpi_status
acpi_ds_exec_begin_control_op(struct acpi_walk_state *walk_state,
			      union acpi_parse_object *op);

acpi_status
acpi_ds_exec_end_control_op(struct acpi_walk_state *walk_state,
			    union acpi_parse_object *op);

/*
 * dsopcode - support for late operand evaluation
 */
acpi_status
acpi_ds_eval_buffer_field_operands(struct acpi_walk_state *walk_state,
				   union acpi_parse_object *op);

acpi_status
acpi_ds_eval_region_operands(struct acpi_walk_state *walk_state,
			     union acpi_parse_object *op);

acpi_status
acpi_ds_eval_table_region_operands(struct acpi_walk_state *walk_state,
				   union acpi_parse_object *op);

acpi_status
acpi_ds_eval_data_object_operands(struct acpi_walk_state *walk_state,
				  union acpi_parse_object *op,
				  union acpi_operand_object *obj_desc);

acpi_status
acpi_ds_eval_bank_field_operands(struct acpi_walk_state *walk_state,
				 union acpi_parse_object *op);

acpi_status acpi_ds_initialize_region(acpi_handle obj_handle);

/*
 * dsexec - Parser/Interpreter interface, method execution callbacks
 */
acpi_status
acpi_ds_get_predicate_value(struct acpi_walk_state *walk_state,
			    union acpi_operand_object *result_obj);

acpi_status
acpi_ds_exec_begin_op(struct acpi_walk_state *walk_state,
		      union acpi_parse_object **out_op);

acpi_status acpi_ds_exec_end_op(struct acpi_walk_state *state);

/*
 * dsfield - Parser/Interpreter interface for AML fields
 */
acpi_status
acpi_ds_create_field(union acpi_parse_object *op,
		     struct acpi_namespace_node *region_node,
		     struct acpi_walk_state *walk_state);

acpi_status
acpi_ds_create_bank_field(union acpi_parse_object *op,
			  struct acpi_namespace_node *region_node,
			  struct acpi_walk_state *walk_state);

acpi_status
acpi_ds_create_index_field(union acpi_parse_object *op,
			   struct acpi_namespace_node *region_node,
			   struct acpi_walk_state *walk_state);

acpi_status
acpi_ds_create_buffer_field(union acpi_parse_object *op,
			    struct acpi_walk_state *walk_state);

acpi_status
acpi_ds_init_field_objects(union acpi_parse_object *op,
			   struct acpi_walk_state *walk_state);

/*
 * dsload - Parser/Interpreter interface
 */
acpi_status
acpi_ds_init_callbacks(struct acpi_walk_state *walk_state, u32 pass_number);

/* dsload - pass 1 namespace load callbacks */

acpi_status
acpi_ds_load1_begin_op(struct acpi_walk_state *walk_state,
		       union acpi_parse_object **out_op);

acpi_status acpi_ds_load1_end_op(struct acpi_walk_state *walk_state);

/* dsload - pass 2 namespace load callbacks */

acpi_status
acpi_ds_load2_begin_op(struct acpi_walk_state *walk_state,
		       union acpi_parse_object **out_op);

acpi_status acpi_ds_load2_end_op(struct acpi_walk_state *walk_state);

/*
 * dsmthdat - method data (locals/args)
 */
acpi_status
acpi_ds_store_object_to_local(u8 type,
			      u32 index,
			      union acpi_operand_object *src_desc,
			      struct acpi_walk_state *walk_state);

acpi_status
acpi_ds_method_data_get_entry(u16 opcode,
			      u32 index,
			      struct acpi_walk_state *walk_state,
			      union acpi_operand_object ***node);

void acpi_ds_method_data_delete_all(struct acpi_walk_state *walk_state);

u8 acpi_ds_is_method_value(union acpi_operand_object *obj_desc);

acpi_status
acpi_ds_method_data_get_value(u8 type,
			      u32 index,
			      struct acpi_walk_state *walk_state,
			      union acpi_operand_object **dest_desc);

acpi_status
acpi_ds_method_data_init_args(union acpi_operand_object **params,
			      u32 max_param_count,
			      struct acpi_walk_state *walk_state);

acpi_status
acpi_ds_method_data_get_node(u8 type,
			     u32 index,
			     struct acpi_walk_state *walk_state,
			     struct acpi_namespace_node **node);

void acpi_ds_method_data_init(struct acpi_walk_state *walk_state);

/*
 * dsmethod - Parser/Interpreter interface - control method parsing
 */
acpi_status
acpi_ds_auto_serialize_method(struct acpi_namespace_node *node,
			      union acpi_operand_object *obj_desc);

acpi_status
acpi_ds_call_control_method(struct acpi_thread_state *thread,
			    struct acpi_walk_state *walk_state,
			    union acpi_parse_object *op);

acpi_status
acpi_ds_restart_control_method(struct acpi_walk_state *walk_state,
			       union acpi_operand_object *return_desc);

void
acpi_ds_terminate_control_method(union acpi_operand_object *method_desc,
				 struct acpi_walk_state *walk_state);

acpi_status
acpi_ds_begin_method_execution(struct acpi_namespace_node *method_node,
			       union acpi_operand_object *obj_desc,
			       struct acpi_walk_state *walk_state);

acpi_status
acpi_ds_method_error(acpi_status status, struct acpi_walk_state *walk_state);

/*
 * dsinit
 */
acpi_status
acpi_ds_initialize_objects(u32 table_index,
			   struct acpi_namespace_node *start_node);

/*
 * dsobject - Parser/Interpreter interface - object initialization and conversion
 */
acpi_status
acpi_ds_build_internal_object(struct acpi_walk_state *walk_state,
			      union acpi_parse_object *op,
			      union acpi_operand_object **obj_desc_ptr);

acpi_status
acpi_ds_build_internal_buffer_obj(struct acpi_walk_state *walk_state,
				  union acpi_parse_object *op,
				  u32 buffer_length,
				  union acpi_operand_object **obj_desc_ptr);

acpi_status
acpi_ds_build_internal_package_obj(struct acpi_walk_state *walk_state,
				   union acpi_parse_object *op,
				   u32 package_length,
				   union acpi_operand_object **obj_desc);

acpi_status
acpi_ds_init_object_from_op(struct acpi_walk_state *walk_state,
			    union acpi_parse_object *op,
			    u16 opcode, union acpi_operand_object **obj_desc);

acpi_status
acpi_ds_create_node(struct acpi_walk_state *walk_state,
		    struct acpi_namespace_node *node,
		    union acpi_parse_object *op);

/*
 * dspkginit - Package object initialization
 */
acpi_status
acpi_ds_init_package_element(u8 object_type,
			     union acpi_operand_object *source_object,
			     union acpi_generic_state *state, void *context);

/*
 * dsutils - Parser/Interpreter interface utility routines
 */
void acpi_ds_clear_implicit_return(struct acpi_walk_state *walk_state);

u8
acpi_ds_do_implicit_return(union acpi_operand_object *return_desc,
			   struct acpi_walk_state *walk_state,
			   u8 add_reference);

u8
acpi_ds_is_result_used(union acpi_parse_object *op,
		       struct acpi_walk_state *walk_state);

void
acpi_ds_delete_result_if_not_used(union acpi_parse_object *op,
				  union acpi_operand_object *result_obj,
				  struct acpi_walk_state *walk_state);

acpi_status
acpi_ds_create_operand(struct acpi_walk_state *walk_state,
		       union acpi_parse_object *arg, u32 args_remaining);

acpi_status
acpi_ds_create_operands(struct acpi_walk_state *walk_state,
			union acpi_parse_object *first_arg);

acpi_status acpi_ds_resolve_operands(struct acpi_walk_state *walk_state);

void acpi_ds_clear_operands(struct acpi_walk_state *walk_state);

acpi_status acpi_ds_evaluate_name_path(struct acpi_walk_state *walk_state);

/*
 * dswscope - Scope Stack manipulation
 */
acpi_status
acpi_ds_scope_stack_push(struct acpi_namespace_node *node,
			 acpi_object_type type,
			 struct acpi_walk_state *walk_state);

acpi_status acpi_ds_scope_stack_pop(struct acpi_walk_state *walk_state);

void acpi_ds_scope_stack_clear(struct acpi_walk_state *walk_state);

/*
 * dswstate - parser WALK_STATE management routines
 */
acpi_status
acpi_ds_obj_stack_push(void *object, struct acpi_walk_state *walk_state);

acpi_status
acpi_ds_obj_stack_pop(u32 pop_count, struct acpi_walk_state *walk_state);

struct acpi_walk_state * acpi_ds_create_walk_state(acpi_owner_id owner_id,
						   union acpi_parse_object
						   *origin,
						   union acpi_operand_object
						   *mth_desc,
						   struct acpi_thread_state
						   *thread);

acpi_status
acpi_ds_init_aml_walk(struct acpi_walk_state *walk_state,
		      union acpi_parse_object *op,
		      struct acpi_namespace_node *method_node,
		      u8 * aml_start,
		      u32 aml_length,
		      struct acpi_evaluate_info *info, u8 pass_number);

void
acpi_ds_obj_stack_pop_and_delete(u32 pop_count,
				 struct acpi_walk_state *walk_state);

void acpi_ds_delete_walk_state(struct acpi_walk_state *walk_state);

struct acpi_walk_state *acpi_ds_pop_walk_state(struct acpi_thread_state
					       *thread);

void
acpi_ds_push_walk_state(struct acpi_walk_state *walk_state,
			struct acpi_thread_state *thread);

acpi_status acpi_ds_result_stack_clear(struct acpi_walk_state *walk_state);

struct acpi_walk_state *acpi_ds_get_current_walk_state(struct acpi_thread_state
						       *thread);

acpi_status
acpi_ds_result_pop(union acpi_operand_object **object,
		   struct acpi_walk_state *walk_state);

acpi_status
acpi_ds_result_push(union acpi_operand_object *object,
		    struct acpi_walk_state *walk_state);

/*
 * dsdebug - parser debugging routines
 */
void
acpi_ds_dump_method_stack(acpi_status status,
			  struct acpi_walk_state *walk_state,
			  union acpi_parse_object *op);

#endif				/* _ACDISPAT_H_ */
