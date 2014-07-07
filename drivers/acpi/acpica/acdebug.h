/******************************************************************************
 *
 * Name: acdebug.h - ACPI/AML debugger
 *
 *****************************************************************************/

/*
 * Copyright (C) 2000 - 2014, Intel Corp.
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

#ifndef __ACDEBUG_H__
#define __ACDEBUG_H__

#define ACPI_DEBUG_BUFFER_SIZE  0x4000	/* 16K buffer for return objects */

struct acpi_db_command_info {
	char *name;		/* Command Name */
	u8 min_args;		/* Minimum arguments required */
};

struct acpi_db_command_help {
	u8 line_count;		/* Number of help lines */
	char *invocation;	/* Command Invocation */
	char *description;	/* Command Description */
};

struct acpi_db_argument_info {
	char *name;		/* Argument Name */
};

struct acpi_db_execute_walk {
	u32 count;
	u32 max_count;
};

#define PARAM_LIST(pl)                  pl
#define DBTEST_OUTPUT_LEVEL(lvl)        if (acpi_gbl_db_opt_verbose)
#define VERBOSE_PRINT(fp)               DBTEST_OUTPUT_LEVEL(lvl) {\
			  acpi_os_printf PARAM_LIST(fp);}

#define EX_NO_SINGLE_STEP               1
#define EX_SINGLE_STEP                  2

/*
 * dbxface - external debugger interfaces
 */
acpi_status acpi_db_initialize(void);

void acpi_db_terminate(void);

acpi_status
acpi_db_single_step(struct acpi_walk_state *walk_state,
		    union acpi_parse_object *op, u32 op_type);

/*
 * dbcmds - debug commands and output routines
 */
struct acpi_namespace_node *acpi_db_convert_to_node(char *in_string);

void acpi_db_display_table_info(char *table_arg);

void acpi_db_display_template(char *buffer_arg);

void acpi_db_unload_acpi_table(char *name);

void acpi_db_send_notify(char *name, u32 value);

void acpi_db_display_interfaces(char *action_arg, char *interface_name_arg);

acpi_status acpi_db_sleep(char *object_arg);

void acpi_db_display_locks(void);

void acpi_db_display_resources(char *object_arg);

ACPI_HW_DEPENDENT_RETURN_VOID(void acpi_db_display_gpes(void))

void acpi_db_display_handlers(void);

ACPI_HW_DEPENDENT_RETURN_VOID(void
			      acpi_db_generate_gpe(char *gpe_arg,
						   char *block_arg))
ACPI_HW_DEPENDENT_RETURN_VOID(void acpi_db_generate_sci(void))

void acpi_db_execute_test(char *type_arg);

/*
 * dbconvert - miscellaneous conversion routines
 */
acpi_status acpi_db_hex_char_to_value(int hex_char, u8 *return_value);

acpi_status acpi_db_convert_to_package(char *string, union acpi_object *object);

acpi_status
acpi_db_convert_to_object(acpi_object_type type,
			  char *string, union acpi_object *object);

u8 *acpi_db_encode_pld_buffer(struct acpi_pld_info *pld_info);

void acpi_db_dump_pld_buffer(union acpi_object *obj_desc);

/*
 * dbmethod - control method commands
 */
void
acpi_db_set_method_breakpoint(char *location,
			      struct acpi_walk_state *walk_state,
			      union acpi_parse_object *op);

void acpi_db_set_method_call_breakpoint(union acpi_parse_object *op);

void acpi_db_set_method_data(char *type_arg, char *index_arg, char *value_arg);

acpi_status acpi_db_disassemble_method(char *name);

void acpi_db_disassemble_aml(char *statements, union acpi_parse_object *op);

void acpi_db_batch_execute(char *count_arg);

/*
 * dbnames - namespace commands
 */
void acpi_db_set_scope(char *name);

void acpi_db_dump_namespace(char *start_arg, char *depth_arg);

void acpi_db_dump_namespace_paths(void);

void acpi_db_dump_namespace_by_owner(char *owner_arg, char *depth_arg);

acpi_status acpi_db_find_name_in_namespace(char *name_arg);

void acpi_db_check_predefined_names(void);

acpi_status
acpi_db_display_objects(char *obj_type_arg, char *display_count_arg);

void acpi_db_check_integrity(void);

void acpi_db_find_references(char *object_arg);

void acpi_db_get_bus_info(void);

/*
 * dbdisply - debug display commands
 */
void acpi_db_display_method_info(union acpi_parse_object *op);

void acpi_db_decode_and_display_object(char *target, char *output_type);

void
acpi_db_display_result_object(union acpi_operand_object *obj_desc,
			      struct acpi_walk_state *walk_state);

acpi_status acpi_db_display_all_methods(char *display_count_arg);

void acpi_db_display_arguments(void);

void acpi_db_display_locals(void);

void acpi_db_display_results(void);

void acpi_db_display_calling_tree(void);

void acpi_db_display_object_type(char *object_arg);

void
acpi_db_display_argument_object(union acpi_operand_object *obj_desc,
				struct acpi_walk_state *walk_state);

/*
 * dbexec - debugger control method execution
 */
void
acpi_db_execute(char *name, char **args, acpi_object_type * types, u32 flags);

void
acpi_db_create_execution_threads(char *num_threads_arg,
				 char *num_loops_arg, char *method_name_arg);

void acpi_db_delete_objects(u32 count, union acpi_object *objects);

#ifdef ACPI_DBG_TRACK_ALLOCATIONS
u32 acpi_db_get_cache_info(struct acpi_memory_list *cache);
#endif

/*
 * dbfileio - Debugger file I/O commands
 */
acpi_object_type
acpi_db_match_argument(char *user_argument,
		       struct acpi_db_argument_info *arguments);

void acpi_db_close_debug_file(void);

void acpi_db_open_debug_file(char *name);

acpi_status acpi_db_load_acpi_table(char *filename);

acpi_status
acpi_db_get_table_from_file(char *filename, struct acpi_table_header **table);

acpi_status
acpi_db_read_table_from_file(char *filename, struct acpi_table_header **table);

/*
 * dbhistry - debugger HISTORY command
 */
void acpi_db_add_to_history(char *command_line);

void acpi_db_display_history(void);

char *acpi_db_get_from_history(char *command_num_arg);

char *acpi_db_get_history_by_index(u32 commandd_num);

/*
 * dbinput - user front-end to the AML debugger
 */
acpi_status
acpi_db_command_dispatch(char *input_buffer,
			 struct acpi_walk_state *walk_state,
			 union acpi_parse_object *op);

void ACPI_SYSTEM_XFACE acpi_db_execute_thread(void *context);

acpi_status acpi_db_user_commands(char prompt, union acpi_parse_object *op);

char *acpi_db_get_next_token(char *string,
			     char **next, acpi_object_type * return_type);

/*
 * dbstats - Generation and display of ACPI table statistics
 */
void acpi_db_generate_statistics(union acpi_parse_object *root, u8 is_method);

acpi_status acpi_db_display_statistics(char *type_arg);

/*
 * dbutils - AML debugger utilities
 */
void acpi_db_set_output_destination(u32 where);

void acpi_db_dump_external_object(union acpi_object *obj_desc, u32 level);

void acpi_db_prep_namestring(char *name);

struct acpi_namespace_node *acpi_db_local_ns_lookup(char *name);

void acpi_db_uint32_to_hex_string(u32 value, char *buffer);

#endif				/* __ACDEBUG_H__ */
