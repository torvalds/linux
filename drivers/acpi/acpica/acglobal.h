/* SPDX-License-Identifier: BSD-3-Clause OR GPL-2.0 */
/******************************************************************************
 *
 * Name: acglobal.h - Declarations for global variables
 *
 * Copyright (C) 2000 - 2018, Intel Corp.
 *
 *****************************************************************************/

#ifndef __ACGLOBAL_H__
#define __ACGLOBAL_H__

/*****************************************************************************
 *
 * Globals related to the incoming ACPI tables
 *
 ****************************************************************************/

/* Master list of all ACPI tables that were found in the RSDT/XSDT */

ACPI_GLOBAL(struct acpi_table_list, acpi_gbl_root_table_list);

/* DSDT information. Used to check for DSDT corruption */

ACPI_GLOBAL(struct acpi_table_header *, acpi_gbl_DSDT);
ACPI_GLOBAL(struct acpi_table_header, acpi_gbl_original_dsdt_header);
ACPI_INIT_GLOBAL(u32, acpi_gbl_dsdt_index, ACPI_INVALID_TABLE_INDEX);
ACPI_INIT_GLOBAL(u32, acpi_gbl_facs_index, ACPI_INVALID_TABLE_INDEX);
ACPI_INIT_GLOBAL(u32, acpi_gbl_xfacs_index, ACPI_INVALID_TABLE_INDEX);
ACPI_INIT_GLOBAL(u32, acpi_gbl_fadt_index, ACPI_INVALID_TABLE_INDEX);

#if (!ACPI_REDUCED_HARDWARE)
ACPI_GLOBAL(struct acpi_table_facs *, acpi_gbl_FACS);

#endif				/* !ACPI_REDUCED_HARDWARE */

/* These addresses are calculated from the FADT Event Block addresses */

ACPI_GLOBAL(struct acpi_generic_address, acpi_gbl_xpm1a_status);
ACPI_GLOBAL(struct acpi_generic_address, acpi_gbl_xpm1a_enable);

ACPI_GLOBAL(struct acpi_generic_address, acpi_gbl_xpm1b_status);
ACPI_GLOBAL(struct acpi_generic_address, acpi_gbl_xpm1b_enable);

/*
 * Handle both ACPI 1.0 and ACPI 2.0+ Integer widths. The integer width is
 * determined by the revision of the DSDT: If the DSDT revision is less than
 * 2, use only the lower 32 bits of the internal 64-bit Integer.
 */
ACPI_GLOBAL(u8, acpi_gbl_integer_bit_width);
ACPI_GLOBAL(u8, acpi_gbl_integer_byte_width);
ACPI_GLOBAL(u8, acpi_gbl_integer_nybble_width);

/*****************************************************************************
 *
 * Mutual exclusion within the ACPICA subsystem
 *
 ****************************************************************************/

/*
 * Predefined mutex objects. This array contains the
 * actual OS mutex handles, indexed by the local ACPI_MUTEX_HANDLEs.
 * (The table maps local handles to the real OS handles)
 */
ACPI_GLOBAL(struct acpi_mutex_info, acpi_gbl_mutex_info[ACPI_NUM_MUTEX]);

/*
 * Global lock mutex is an actual AML mutex object
 * Global lock semaphore works in conjunction with the actual global lock
 * Global lock spinlock is used for "pending" handshake
 */
ACPI_GLOBAL(union acpi_operand_object *, acpi_gbl_global_lock_mutex);
ACPI_GLOBAL(acpi_semaphore, acpi_gbl_global_lock_semaphore);
ACPI_GLOBAL(acpi_spinlock, acpi_gbl_global_lock_pending_lock);
ACPI_GLOBAL(u16, acpi_gbl_global_lock_handle);
ACPI_GLOBAL(u8, acpi_gbl_global_lock_acquired);
ACPI_GLOBAL(u8, acpi_gbl_global_lock_present);
ACPI_GLOBAL(u8, acpi_gbl_global_lock_pending);

/*
 * Spinlocks are used for interfaces that can be possibly called at
 * interrupt level
 */
ACPI_GLOBAL(acpi_spinlock, acpi_gbl_gpe_lock);	/* For GPE data structs and registers */
ACPI_GLOBAL(acpi_raw_spinlock, acpi_gbl_hardware_lock);	/* For ACPI H/W except GPE registers */
ACPI_GLOBAL(acpi_spinlock, acpi_gbl_reference_count_lock);

/* Mutex for _OSI support */

ACPI_GLOBAL(acpi_mutex, acpi_gbl_osi_mutex);

/* Reader/Writer lock is used for namespace walk and dynamic table unload */

ACPI_GLOBAL(struct acpi_rw_lock, acpi_gbl_namespace_rw_lock);

/*****************************************************************************
 *
 * Miscellaneous globals
 *
 ****************************************************************************/

/* Object caches */

ACPI_GLOBAL(acpi_cache_t *, acpi_gbl_namespace_cache);
ACPI_GLOBAL(acpi_cache_t *, acpi_gbl_state_cache);
ACPI_GLOBAL(acpi_cache_t *, acpi_gbl_ps_node_cache);
ACPI_GLOBAL(acpi_cache_t *, acpi_gbl_ps_node_ext_cache);
ACPI_GLOBAL(acpi_cache_t *, acpi_gbl_operand_cache);

/* System */

ACPI_INIT_GLOBAL(u32, acpi_gbl_startup_flags, 0);
ACPI_INIT_GLOBAL(u8, acpi_gbl_shutdown, TRUE);
ACPI_INIT_GLOBAL(u8, acpi_gbl_early_initialization, TRUE);

/* Global handlers */

ACPI_GLOBAL(struct acpi_global_notify_handler, acpi_gbl_global_notify[2]);
ACPI_GLOBAL(acpi_exception_handler, acpi_gbl_exception_handler);
ACPI_GLOBAL(acpi_init_handler, acpi_gbl_init_handler);
ACPI_GLOBAL(acpi_table_handler, acpi_gbl_table_handler);
ACPI_GLOBAL(void *, acpi_gbl_table_handler_context);
ACPI_GLOBAL(acpi_interface_handler, acpi_gbl_interface_handler);
ACPI_GLOBAL(struct acpi_sci_handler_info *, acpi_gbl_sci_handler_list);

/* Owner ID support */

ACPI_GLOBAL(u32, acpi_gbl_owner_id_mask[ACPI_NUM_OWNERID_MASKS]);
ACPI_GLOBAL(u8, acpi_gbl_last_owner_id_index);
ACPI_GLOBAL(u8, acpi_gbl_next_owner_id_offset);

/* Initialization sequencing */

ACPI_INIT_GLOBAL(u8, acpi_gbl_namespace_initialized, FALSE);

/* Miscellaneous */

ACPI_GLOBAL(u32, acpi_gbl_original_mode);
ACPI_GLOBAL(u32, acpi_gbl_ns_lookup_count);
ACPI_GLOBAL(u32, acpi_gbl_ps_find_count);
ACPI_GLOBAL(u16, acpi_gbl_pm1_enable_register_save);
ACPI_GLOBAL(u8, acpi_gbl_debugger_configuration);
ACPI_GLOBAL(u8, acpi_gbl_step_to_next_call);
ACPI_GLOBAL(u8, acpi_gbl_acpi_hardware_present);
ACPI_GLOBAL(u8, acpi_gbl_events_initialized);
ACPI_GLOBAL(struct acpi_interface_info *, acpi_gbl_supported_interfaces);
ACPI_GLOBAL(struct acpi_address_range *,
	    acpi_gbl_address_range_list[ACPI_ADDRESS_RANGE_MAX]);

/* Other miscellaneous, declared and initialized in utglobal */

extern const char *acpi_gbl_sleep_state_names[ACPI_S_STATE_COUNT];
extern const char *acpi_gbl_lowest_dstate_names[ACPI_NUM_sx_w_METHODS];
extern const char *acpi_gbl_highest_dstate_names[ACPI_NUM_sx_d_METHODS];
extern const char *acpi_gbl_region_types[ACPI_NUM_PREDEFINED_REGIONS];
extern const char acpi_gbl_lower_hex_digits[];
extern const char acpi_gbl_upper_hex_digits[];
extern const struct acpi_opcode_info acpi_gbl_aml_op_info[AML_NUM_OPCODES];

/* Lists for tracking memory allocations (debug only) */

#ifdef ACPI_DBG_TRACK_ALLOCATIONS
ACPI_GLOBAL(struct acpi_memory_list *, acpi_gbl_global_list);
ACPI_GLOBAL(struct acpi_memory_list *, acpi_gbl_ns_node_list);
ACPI_GLOBAL(u8, acpi_gbl_display_final_mem_stats);
ACPI_GLOBAL(u8, acpi_gbl_disable_mem_tracking);
#endif

/*****************************************************************************
 *
 * ACPI Namespace
 *
 ****************************************************************************/

#if !defined (ACPI_NO_METHOD_EXECUTION) || defined (ACPI_CONSTANT_EVAL_ONLY)
#define NUM_PREDEFINED_NAMES            10
#else
#define NUM_PREDEFINED_NAMES            9
#endif

ACPI_GLOBAL(struct acpi_namespace_node, acpi_gbl_root_node_struct);
ACPI_GLOBAL(struct acpi_namespace_node *, acpi_gbl_root_node);
ACPI_GLOBAL(struct acpi_namespace_node *, acpi_gbl_fadt_gpe_device);
ACPI_GLOBAL(union acpi_operand_object *, acpi_gbl_module_code_list);

extern const u8 acpi_gbl_ns_properties[ACPI_NUM_NS_TYPES];
extern const struct acpi_predefined_names
    acpi_gbl_pre_defined_names[NUM_PREDEFINED_NAMES];

#ifdef ACPI_DEBUG_OUTPUT
ACPI_GLOBAL(u32, acpi_gbl_current_node_count);
ACPI_GLOBAL(u32, acpi_gbl_current_node_size);
ACPI_GLOBAL(u32, acpi_gbl_max_concurrent_node_count);
ACPI_GLOBAL(acpi_size *, acpi_gbl_entry_stack_pointer);
ACPI_GLOBAL(acpi_size *, acpi_gbl_lowest_stack_pointer);
ACPI_GLOBAL(u32, acpi_gbl_deepest_nesting);
ACPI_INIT_GLOBAL(u32, acpi_gbl_nesting_level, 0);
#endif

/*****************************************************************************
 *
 * Interpreter/Parser globals
 *
 ****************************************************************************/

/* Control method single step flag */

ACPI_GLOBAL(u8, acpi_gbl_cm_single_step);
ACPI_GLOBAL(struct acpi_thread_state *, acpi_gbl_current_walk_list);
ACPI_INIT_GLOBAL(union acpi_parse_object, *acpi_gbl_current_scope, NULL);

/* ASL/ASL+ converter */

ACPI_INIT_GLOBAL(u8, acpi_gbl_capture_comments, FALSE);
ACPI_INIT_GLOBAL(struct acpi_comment_node, *acpi_gbl_last_list_head, NULL);

/*****************************************************************************
 *
 * Hardware globals
 *
 ****************************************************************************/

extern struct acpi_bit_register_info
    acpi_gbl_bit_register_info[ACPI_NUM_BITREG];
ACPI_GLOBAL(u8, acpi_gbl_sleep_type_a);
ACPI_GLOBAL(u8, acpi_gbl_sleep_type_b);

/*****************************************************************************
 *
 * Event and GPE globals
 *
 ****************************************************************************/

#if (!ACPI_REDUCED_HARDWARE)
ACPI_GLOBAL(u8, acpi_gbl_all_gpes_initialized);
ACPI_GLOBAL(struct acpi_gpe_xrupt_info *, acpi_gbl_gpe_xrupt_list_head);
ACPI_GLOBAL(struct acpi_gpe_block_info *,
	    acpi_gbl_gpe_fadt_blocks[ACPI_MAX_GPE_BLOCKS]);
ACPI_GLOBAL(acpi_gbl_event_handler, acpi_gbl_global_event_handler);
ACPI_GLOBAL(void *, acpi_gbl_global_event_handler_context);
ACPI_GLOBAL(struct acpi_fixed_event_handler,
	    acpi_gbl_fixed_event_handlers[ACPI_NUM_FIXED_EVENTS]);
extern struct acpi_fixed_event_info
    acpi_gbl_fixed_event_info[ACPI_NUM_FIXED_EVENTS];
#endif				/* !ACPI_REDUCED_HARDWARE */

/*****************************************************************************
 *
 * Debug support
 *
 ****************************************************************************/

/* Event counters */

ACPI_GLOBAL(u32, acpi_method_count);
ACPI_GLOBAL(u32, acpi_gpe_count);
ACPI_GLOBAL(u32, acpi_sci_count);
ACPI_GLOBAL(u32, acpi_fixed_event_count[ACPI_NUM_FIXED_EVENTS]);

/* Dynamic control method tracing mechanism */

ACPI_GLOBAL(u32, acpi_gbl_original_dbg_level);
ACPI_GLOBAL(u32, acpi_gbl_original_dbg_layer);

/*****************************************************************************
 *
 * Debugger and Disassembler
 *
 ****************************************************************************/

ACPI_INIT_GLOBAL(u8, acpi_gbl_db_output_flags, ACPI_DB_CONSOLE_OUTPUT);

#ifdef ACPI_DISASSEMBLER

/* Do not disassemble buffers to resource descriptors */

ACPI_INIT_GLOBAL(u8, acpi_gbl_no_resource_disassembly, FALSE);
ACPI_INIT_GLOBAL(u8, acpi_gbl_ignore_noop_operator, FALSE);
ACPI_INIT_GLOBAL(u8, acpi_gbl_cstyle_disassembly, TRUE);
ACPI_INIT_GLOBAL(u8, acpi_gbl_force_aml_disassembly, FALSE);
ACPI_INIT_GLOBAL(u8, acpi_gbl_dm_opt_verbose, TRUE);
ACPI_INIT_GLOBAL(u8, acpi_gbl_dm_emit_external_opcodes, FALSE);
ACPI_INIT_GLOBAL(u8, acpi_gbl_do_disassembler_optimizations, TRUE);
ACPI_INIT_GLOBAL(ACPI_PARSE_OBJECT_LIST, *acpi_gbl_temp_list_head, NULL);

ACPI_GLOBAL(u8, acpi_gbl_dm_opt_disasm);
ACPI_GLOBAL(u8, acpi_gbl_dm_opt_listing);
ACPI_GLOBAL(u8, acpi_gbl_num_external_methods);
ACPI_GLOBAL(u32, acpi_gbl_resolved_external_methods);
ACPI_GLOBAL(struct acpi_external_list *, acpi_gbl_external_list);
ACPI_GLOBAL(struct acpi_external_file *, acpi_gbl_external_file_list);
#endif

#ifdef ACPI_DEBUGGER
ACPI_INIT_GLOBAL(u8, acpi_gbl_abort_method, FALSE);
ACPI_INIT_GLOBAL(acpi_thread_id, acpi_gbl_db_thread_id, ACPI_INVALID_THREAD_ID);

ACPI_GLOBAL(u8, acpi_gbl_db_opt_no_ini_methods);
ACPI_GLOBAL(u8, acpi_gbl_db_opt_no_region_support);
ACPI_GLOBAL(u8, acpi_gbl_db_output_to_file);
ACPI_GLOBAL(char *, acpi_gbl_db_buffer);
ACPI_GLOBAL(char *, acpi_gbl_db_filename);
ACPI_GLOBAL(u32, acpi_gbl_db_debug_level);
ACPI_GLOBAL(u32, acpi_gbl_db_console_debug_level);
ACPI_GLOBAL(struct acpi_namespace_node *, acpi_gbl_db_scope_node);
ACPI_GLOBAL(u8, acpi_gbl_db_terminate_loop);
ACPI_GLOBAL(u8, acpi_gbl_db_threads_terminated);
ACPI_GLOBAL(char *, acpi_gbl_db_args[ACPI_DEBUGGER_MAX_ARGS]);
ACPI_GLOBAL(acpi_object_type, acpi_gbl_db_arg_types[ACPI_DEBUGGER_MAX_ARGS]);

/* These buffers should all be the same size */

ACPI_GLOBAL(char, acpi_gbl_db_parsed_buf[ACPI_DB_LINE_BUFFER_SIZE]);
ACPI_GLOBAL(char, acpi_gbl_db_scope_buf[ACPI_DB_LINE_BUFFER_SIZE]);
ACPI_GLOBAL(char, acpi_gbl_db_debug_filename[ACPI_DB_LINE_BUFFER_SIZE]);

/* Statistics globals */

ACPI_GLOBAL(u16, acpi_gbl_obj_type_count[ACPI_TOTAL_TYPES]);
ACPI_GLOBAL(u16, acpi_gbl_node_type_count[ACPI_TOTAL_TYPES]);
ACPI_GLOBAL(u16, acpi_gbl_obj_type_count_misc);
ACPI_GLOBAL(u16, acpi_gbl_node_type_count_misc);
ACPI_GLOBAL(u32, acpi_gbl_num_nodes);
ACPI_GLOBAL(u32, acpi_gbl_num_objects);
#endif				/* ACPI_DEBUGGER */

#if defined (ACPI_DISASSEMBLER) || defined (ACPI_ASL_COMPILER)
ACPI_GLOBAL(const char, *acpi_gbl_pld_panel_list[]);
ACPI_GLOBAL(const char, *acpi_gbl_pld_vertical_position_list[]);
ACPI_GLOBAL(const char, *acpi_gbl_pld_horizontal_position_list[]);
ACPI_GLOBAL(const char, *acpi_gbl_pld_shape_list[]);
ACPI_INIT_GLOBAL(u8, acpi_gbl_disasm_flag, FALSE);
#endif

/*****************************************************************************
 *
 * ACPICA application-specific globals
 *
 ****************************************************************************/

/* ASL-to-ASL+ conversion utility (implemented within the iASL compiler) */

#ifdef ACPI_ASL_COMPILER
ACPI_INIT_GLOBAL(char *, acpi_gbl_current_inline_comment, NULL);
ACPI_INIT_GLOBAL(char *, acpi_gbl_current_end_node_comment, NULL);
ACPI_INIT_GLOBAL(char *, acpi_gbl_current_open_brace_comment, NULL);
ACPI_INIT_GLOBAL(char *, acpi_gbl_current_close_brace_comment, NULL);

ACPI_INIT_GLOBAL(char *, acpi_gbl_root_filename, NULL);
ACPI_INIT_GLOBAL(char *, acpi_gbl_current_filename, NULL);
ACPI_INIT_GLOBAL(char *, acpi_gbl_current_parent_filename, NULL);
ACPI_INIT_GLOBAL(char *, acpi_gbl_current_include_filename, NULL);

ACPI_INIT_GLOBAL(struct acpi_comment_node, *acpi_gbl_def_blk_comment_list_head,
		 NULL);
ACPI_INIT_GLOBAL(struct acpi_comment_node, *acpi_gbl_def_blk_comment_list_tail,
		 NULL);
ACPI_INIT_GLOBAL(struct acpi_comment_node, *acpi_gbl_reg_comment_list_head,
		 NULL);
ACPI_INIT_GLOBAL(struct acpi_comment_node, *acpi_gbl_reg_comment_list_tail,
		 NULL);
ACPI_INIT_GLOBAL(struct acpi_comment_node, *acpi_gbl_inc_comment_list_head,
		 NULL);
ACPI_INIT_GLOBAL(struct acpi_comment_node, *acpi_gbl_inc_comment_list_tail,
		 NULL);
ACPI_INIT_GLOBAL(struct acpi_comment_node, *acpi_gbl_end_blk_comment_list_head,
		 NULL);
ACPI_INIT_GLOBAL(struct acpi_comment_node, *acpi_gbl_end_blk_comment_list_tail,
		 NULL);

ACPI_INIT_GLOBAL(struct acpi_comment_addr_node,
		 *acpi_gbl_comment_addr_list_head, NULL);
ACPI_INIT_GLOBAL(struct acpi_file_node, *acpi_gbl_file_tree_root, NULL);

ACPI_GLOBAL(acpi_cache_t *, acpi_gbl_reg_comment_cache);
ACPI_GLOBAL(acpi_cache_t *, acpi_gbl_comment_addr_cache);
ACPI_GLOBAL(acpi_cache_t *, acpi_gbl_file_cache);

ACPI_INIT_GLOBAL(u8, acpi_gbl_debug_asl_conversion, FALSE);
ACPI_INIT_GLOBAL(ACPI_FILE, acpi_gbl_conv_debug_file, NULL);
ACPI_GLOBAL(char, acpi_gbl_table_sig[4]);
#endif

#ifdef ACPI_APPLICATION
ACPI_INIT_GLOBAL(ACPI_FILE, acpi_gbl_debug_file, NULL);
ACPI_INIT_GLOBAL(ACPI_FILE, acpi_gbl_output_file, NULL);
ACPI_INIT_GLOBAL(u8, acpi_gbl_debug_timeout, FALSE);

/* Print buffer */

ACPI_GLOBAL(acpi_spinlock, acpi_gbl_print_lock);	/* For print buffer */
ACPI_GLOBAL(char, acpi_gbl_print_buffer[1024]);
#endif				/* ACPI_APPLICATION */

#endif				/* __ACGLOBAL_H__ */
