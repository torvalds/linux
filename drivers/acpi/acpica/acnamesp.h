/******************************************************************************
 *
 * Name: acnamesp.h - Namespace subcomponent prototypes and defines
 *
 *****************************************************************************/

/*
 * Copyright (C) 2000 - 2013, Intel Corp.
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

#ifndef __ACNAMESP_H__
#define __ACNAMESP_H__

/* To search the entire name space, pass this as search_base */

#define ACPI_NS_ALL                 ((acpi_handle)0)

/*
 * Elements of acpi_ns_properties are bit significant
 * and should be one-to-one with values of acpi_object_type
 */
#define ACPI_NS_NORMAL              0
#define ACPI_NS_NEWSCOPE            1	/* a definition of this type opens a name scope */
#define ACPI_NS_LOCAL               2	/* suppress search of enclosing scopes */

/* Flags for acpi_ns_lookup, acpi_ns_search_and_enter */

#define ACPI_NS_NO_UPSEARCH         0
#define ACPI_NS_SEARCH_PARENT       0x01
#define ACPI_NS_DONT_OPEN_SCOPE     0x02
#define ACPI_NS_NO_PEER_SEARCH      0x04
#define ACPI_NS_ERROR_IF_FOUND      0x08
#define ACPI_NS_PREFIX_IS_SCOPE     0x10
#define ACPI_NS_EXTERNAL            0x20
#define ACPI_NS_TEMPORARY           0x40

/* Flags for acpi_ns_walk_namespace */

#define ACPI_NS_WALK_NO_UNLOCK      0
#define ACPI_NS_WALK_UNLOCK         0x01
#define ACPI_NS_WALK_TEMP_NODES     0x02

/* Object is not a package element */

#define ACPI_NOT_PACKAGE_ELEMENT    ACPI_UINT32_MAX

/* Always emit warning message, not dependent on node flags */

#define ACPI_WARN_ALWAYS            0

/*
 * nsinit - Namespace initialization
 */
acpi_status acpi_ns_initialize_objects(void);

acpi_status acpi_ns_initialize_devices(void);

/*
 * nsload -  Namespace loading
 */
acpi_status acpi_ns_load_namespace(void);

acpi_status
acpi_ns_load_table(u32 table_index, struct acpi_namespace_node *node);

/*
 * nswalk - walk the namespace
 */
acpi_status
acpi_ns_walk_namespace(acpi_object_type type,
		       acpi_handle start_object,
		       u32 max_depth,
		       u32 flags,
		       acpi_walk_callback pre_order_visit,
		       acpi_walk_callback post_order_visit,
		       void *context, void **return_value);

struct acpi_namespace_node *acpi_ns_get_next_node(struct acpi_namespace_node
						  *parent,
						  struct acpi_namespace_node
						  *child);

struct acpi_namespace_node *acpi_ns_get_next_node_typed(acpi_object_type type,
							struct
							acpi_namespace_node
							*parent,
							struct
							acpi_namespace_node
							*child);

/*
 * nsparse - table parsing
 */
acpi_status
acpi_ns_parse_table(u32 table_index, struct acpi_namespace_node *start_node);

acpi_status
acpi_ns_one_complete_parse(u32 pass_number,
			   u32 table_index,
			   struct acpi_namespace_node *start_node);

/*
 * nsaccess - Top-level namespace access
 */
acpi_status acpi_ns_root_initialize(void);

acpi_status
acpi_ns_lookup(union acpi_generic_state *scope_info,
	       char *name,
	       acpi_object_type type,
	       acpi_interpreter_mode interpreter_mode,
	       u32 flags,
	       struct acpi_walk_state *walk_state,
	       struct acpi_namespace_node **ret_node);

/*
 * nsalloc - Named object allocation/deallocation
 */
struct acpi_namespace_node *acpi_ns_create_node(u32 name);

void acpi_ns_delete_node(struct acpi_namespace_node *node);

void acpi_ns_remove_node(struct acpi_namespace_node *node);

void
acpi_ns_delete_namespace_subtree(struct acpi_namespace_node *parent_handle);

void acpi_ns_delete_namespace_by_owner(acpi_owner_id owner_id);

void acpi_ns_detach_object(struct acpi_namespace_node *node);

void acpi_ns_delete_children(struct acpi_namespace_node *parent);

int acpi_ns_compare_names(char *name1, char *name2);

/*
 * nsdump - Namespace dump/print utilities
 */
#ifdef	ACPI_FUTURE_USAGE
void acpi_ns_dump_tables(acpi_handle search_base, u32 max_depth);
#endif				/* ACPI_FUTURE_USAGE */

void acpi_ns_dump_entry(acpi_handle handle, u32 debug_level);

void
acpi_ns_dump_pathname(acpi_handle handle, char *msg, u32 level, u32 component);

void acpi_ns_print_pathname(u32 num_segments, char *pathname);

acpi_status
acpi_ns_dump_one_object(acpi_handle obj_handle,
			u32 level, void *context, void **return_value);

#ifdef	ACPI_FUTURE_USAGE
void
acpi_ns_dump_objects(acpi_object_type type,
		     u8 display_type,
		     u32 max_depth,
		     acpi_owner_id owner_id, acpi_handle start_handle);
#endif				/* ACPI_FUTURE_USAGE */

/*
 * nseval - Namespace evaluation functions
 */
acpi_status acpi_ns_evaluate(struct acpi_evaluate_info *info);

void acpi_ns_exec_module_code_list(void);

/*
 * nspredef - Support for predefined/reserved names
 */
acpi_status
acpi_ns_check_predefined_names(struct acpi_namespace_node *node,
			       u32 user_param_count,
			       acpi_status return_status,
			       union acpi_operand_object **return_object);

const union acpi_predefined_info *acpi_ns_check_for_predefined_name(struct
								    acpi_namespace_node
								    *node);

void
acpi_ns_check_parameter_count(char *pathname,
			      struct acpi_namespace_node *node,
			      u32 user_param_count,
			      const union acpi_predefined_info *info);

acpi_status
acpi_ns_check_object_type(struct acpi_predefined_data *data,
			  union acpi_operand_object **return_object_ptr,
			  u32 expected_btypes, u32 package_index);

/*
 * nsprepkg - Validation of predefined name packages
 */
acpi_status
acpi_ns_check_package(struct acpi_predefined_data *data,
		      union acpi_operand_object **return_object_ptr);

/*
 * nsnames - Name and Scope manipulation
 */
u32 acpi_ns_opens_scope(acpi_object_type type);

acpi_status
acpi_ns_build_external_path(struct acpi_namespace_node *node,
			    acpi_size size, char *name_buffer);

char *acpi_ns_get_external_pathname(struct acpi_namespace_node *node);

char *acpi_ns_name_of_current_scope(struct acpi_walk_state *walk_state);

acpi_status
acpi_ns_handle_to_pathname(acpi_handle target_handle,
			   struct acpi_buffer *buffer);

u8
acpi_ns_pattern_match(struct acpi_namespace_node *obj_node, char *search_for);

acpi_status
acpi_ns_get_node(struct acpi_namespace_node *prefix_node,
		 const char *external_pathname,
		 u32 flags, struct acpi_namespace_node **out_node);

acpi_size acpi_ns_get_pathname_length(struct acpi_namespace_node *node);

/*
 * nsobject - Object management for namespace nodes
 */
acpi_status
acpi_ns_attach_object(struct acpi_namespace_node *node,
		      union acpi_operand_object *object, acpi_object_type type);

union acpi_operand_object *acpi_ns_get_attached_object(struct
						       acpi_namespace_node
						       *node);

union acpi_operand_object *acpi_ns_get_secondary_object(union
							acpi_operand_object
							*obj_desc);

acpi_status
acpi_ns_attach_data(struct acpi_namespace_node *node,
		    acpi_object_handler handler, void *data);

acpi_status
acpi_ns_detach_data(struct acpi_namespace_node *node,
		    acpi_object_handler handler);

acpi_status
acpi_ns_get_attached_data(struct acpi_namespace_node *node,
			  acpi_object_handler handler, void **data);

/*
 * nsrepair - General return object repair for all
 * predefined methods/objects
 */
acpi_status
acpi_ns_simple_repair(struct acpi_predefined_data *data,
		      u32 expected_btypes,
		      u32 package_index,
		      union acpi_operand_object **return_object_ptr);

acpi_status
acpi_ns_wrap_with_package(struct acpi_predefined_data *data,
			  union acpi_operand_object *original_object,
			  union acpi_operand_object **obj_desc_ptr);

acpi_status
acpi_ns_repair_null_element(struct acpi_predefined_data *data,
			    u32 expected_btypes,
			    u32 package_index,
			    union acpi_operand_object **return_object_ptr);

void
acpi_ns_remove_null_elements(struct acpi_predefined_data *data,
			     u8 package_type,
			     union acpi_operand_object *obj_desc);

/*
 * nsrepair2 - Return object repair for specific
 * predefined methods/objects
 */
acpi_status
acpi_ns_complex_repairs(struct acpi_predefined_data *data,
			struct acpi_namespace_node *node,
			acpi_status validate_status,
			union acpi_operand_object **return_object_ptr);

/*
 * nssearch - Namespace searching and entry
 */
acpi_status
acpi_ns_search_and_enter(u32 entry_name,
			 struct acpi_walk_state *walk_state,
			 struct acpi_namespace_node *node,
			 acpi_interpreter_mode interpreter_mode,
			 acpi_object_type type,
			 u32 flags, struct acpi_namespace_node **ret_node);

acpi_status
acpi_ns_search_one_scope(u32 entry_name,
			 struct acpi_namespace_node *node,
			 acpi_object_type type,
			 struct acpi_namespace_node **ret_node);

void
acpi_ns_install_node(struct acpi_walk_state *walk_state,
		     struct acpi_namespace_node *parent_node,
		     struct acpi_namespace_node *node, acpi_object_type type);

/*
 * nsutils - Utility functions
 */
acpi_object_type acpi_ns_get_type(struct acpi_namespace_node *node);

u32 acpi_ns_local(acpi_object_type type);

void
acpi_ns_print_node_pathname(struct acpi_namespace_node *node, const char *msg);

acpi_status acpi_ns_build_internal_name(struct acpi_namestring_info *info);

void acpi_ns_get_internal_name_length(struct acpi_namestring_info *info);

acpi_status
acpi_ns_internalize_name(const char *dotted_name, char **converted_name);

acpi_status
acpi_ns_externalize_name(u32 internal_name_length,
			 const char *internal_name,
			 u32 * converted_name_length, char **converted_name);

struct acpi_namespace_node *acpi_ns_validate_handle(acpi_handle handle);

void acpi_ns_terminate(void);

#endif				/* __ACNAMESP_H__ */
