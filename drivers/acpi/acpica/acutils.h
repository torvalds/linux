/******************************************************************************
 *
 * Name: acutils.h -- prototypes for the common (subsystem-wide) procedures
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

#ifndef _ACUTILS_H
#define _ACUTILS_H

extern const u8 acpi_gbl_resource_aml_sizes[];
extern const u8 acpi_gbl_resource_aml_serial_bus_sizes[];

/* Strings used by the disassembler and debugger resource dump routines */

#if defined(ACPI_DEBUG_OUTPUT) || defined (ACPI_DISASSEMBLER) || defined (ACPI_DEBUGGER)

extern const char *acpi_gbl_bm_decode[];
extern const char *acpi_gbl_config_decode[];
extern const char *acpi_gbl_consume_decode[];
extern const char *acpi_gbl_dec_decode[];
extern const char *acpi_gbl_he_decode[];
extern const char *acpi_gbl_io_decode[];
extern const char *acpi_gbl_ll_decode[];
extern const char *acpi_gbl_max_decode[];
extern const char *acpi_gbl_mem_decode[];
extern const char *acpi_gbl_min_decode[];
extern const char *acpi_gbl_mtp_decode[];
extern const char *acpi_gbl_rng_decode[];
extern const char *acpi_gbl_rw_decode[];
extern const char *acpi_gbl_shr_decode[];
extern const char *acpi_gbl_siz_decode[];
extern const char *acpi_gbl_trs_decode[];
extern const char *acpi_gbl_ttp_decode[];
extern const char *acpi_gbl_typ_decode[];
extern const char *acpi_gbl_ppc_decode[];
extern const char *acpi_gbl_ior_decode[];
extern const char *acpi_gbl_dts_decode[];
extern const char *acpi_gbl_ct_decode[];
extern const char *acpi_gbl_sbt_decode[];
extern const char *acpi_gbl_am_decode[];
extern const char *acpi_gbl_sm_decode[];
extern const char *acpi_gbl_wm_decode[];
extern const char *acpi_gbl_cph_decode[];
extern const char *acpi_gbl_cpo_decode[];
extern const char *acpi_gbl_dp_decode[];
extern const char *acpi_gbl_ed_decode[];
extern const char *acpi_gbl_bpb_decode[];
extern const char *acpi_gbl_sb_decode[];
extern const char *acpi_gbl_fc_decode[];
extern const char *acpi_gbl_pt_decode[];
#endif

/*
 * For the iASL compiler case, the output is redirected to stderr so that
 * any of the various ACPI errors and warnings do not appear in the output
 * files, for either the compiler or disassembler portions of the tool.
 */
#ifdef ACPI_ASL_COMPILER

#include <stdio.h>

#define ACPI_MSG_REDIRECT_BEGIN \
	FILE                            *output_file = acpi_gbl_output_file; \
	acpi_os_redirect_output (stderr);

#define ACPI_MSG_REDIRECT_END \
	acpi_os_redirect_output (output_file);

#else
/*
 * non-iASL case - no redirection, nothing to do
 */
#define ACPI_MSG_REDIRECT_BEGIN
#define ACPI_MSG_REDIRECT_END
#endif

/*
 * Common error message prefixes
 */
#define ACPI_MSG_ERROR          "ACPI Error: "
#define ACPI_MSG_EXCEPTION      "ACPI Exception: "
#define ACPI_MSG_WARNING        "ACPI Warning: "
#define ACPI_MSG_INFO           "ACPI: "

#define ACPI_MSG_BIOS_ERROR     "ACPI BIOS Error (bug): "
#define ACPI_MSG_BIOS_WARNING   "ACPI BIOS Warning (bug): "

/*
 * Common message suffix
 */
#define ACPI_MSG_SUFFIX \
	acpi_os_printf (" (%8.8X/%s-%u)\n", ACPI_CA_VERSION, module_name, line_number)

/* Types for Resource descriptor entries */

#define ACPI_INVALID_RESOURCE           0
#define ACPI_FIXED_LENGTH               1
#define ACPI_VARIABLE_LENGTH            2
#define ACPI_SMALL_VARIABLE_LENGTH      3

typedef
acpi_status(*acpi_walk_aml_callback) (u8 *aml,
				      u32 length,
				      u32 offset,
				      u8 resource_index, void **context);

typedef
acpi_status(*acpi_pkg_callback) (u8 object_type,
				 union acpi_operand_object *source_object,
				 union acpi_generic_state * state,
				 void *context);

struct acpi_pkg_info {
	u8 *free_space;
	acpi_size length;
	u32 object_space;
	u32 num_packages;
};

/* Object reference counts */

#define REF_INCREMENT       (u16) 0
#define REF_DECREMENT       (u16) 1

/* acpi_ut_dump_buffer */

#define DB_BYTE_DISPLAY     1
#define DB_WORD_DISPLAY     2
#define DB_DWORD_DISPLAY    4
#define DB_QWORD_DISPLAY    8

/*
 * utglobal - Global data structures and procedures
 */
acpi_status acpi_ut_init_globals(void);

#if defined(ACPI_DEBUG_OUTPUT) || defined(ACPI_DEBUGGER)

char *acpi_ut_get_mutex_name(u32 mutex_id);

const char *acpi_ut_get_notify_name(u32 notify_value, acpi_object_type type);
#endif

char *acpi_ut_get_type_name(acpi_object_type type);

char *acpi_ut_get_node_name(void *object);

char *acpi_ut_get_descriptor_name(void *object);

const char *acpi_ut_get_reference_name(union acpi_operand_object *object);

char *acpi_ut_get_object_type_name(union acpi_operand_object *obj_desc);

char *acpi_ut_get_region_name(u8 space_id);

char *acpi_ut_get_event_name(u32 event_id);

char acpi_ut_hex_to_ascii_char(u64 integer, u32 position);

u8 acpi_ut_valid_object_type(acpi_object_type type);

/*
 * utinit - miscellaneous initialization and shutdown
 */
acpi_status acpi_ut_hardware_initialize(void);

void acpi_ut_subsystem_shutdown(void);

/*
 * utclib - Local implementations of C library functions
 */
#ifndef ACPI_USE_SYSTEM_CLIBRARY

acpi_size acpi_ut_strlen(const char *string);

char *acpi_ut_strcpy(char *dst_string, const char *src_string);

char *acpi_ut_strncpy(char *dst_string,
		      const char *src_string, acpi_size count);

int acpi_ut_memcmp(const char *buffer1, const char *buffer2, acpi_size count);

int acpi_ut_strncmp(const char *string1, const char *string2, acpi_size count);

int acpi_ut_strcmp(const char *string1, const char *string2);

char *acpi_ut_strcat(char *dst_string, const char *src_string);

char *acpi_ut_strncat(char *dst_string,
		      const char *src_string, acpi_size count);

u32 acpi_ut_strtoul(const char *string, char **terminator, u32 base);

char *acpi_ut_strstr(char *string1, char *string2);

void *acpi_ut_memcpy(void *dest, const void *src, acpi_size count);

void *acpi_ut_memset(void *dest, u8 value, acpi_size count);

int acpi_ut_to_upper(int c);

int acpi_ut_to_lower(int c);

extern const u8 _acpi_ctype[];

#define _ACPI_XA     0x00	/* extra alphabetic - not supported */
#define _ACPI_XS     0x40	/* extra space */
#define _ACPI_BB     0x00	/* BEL, BS, etc. - not supported */
#define _ACPI_CN     0x20	/* CR, FF, HT, NL, VT */
#define _ACPI_DI     0x04	/* '0'-'9' */
#define _ACPI_LO     0x02	/* 'a'-'z' */
#define _ACPI_PU     0x10	/* punctuation */
#define _ACPI_SP     0x08	/* space */
#define _ACPI_UP     0x01	/* 'A'-'Z' */
#define _ACPI_XD     0x80	/* '0'-'9', 'A'-'F', 'a'-'f' */

#define ACPI_IS_DIGIT(c)  (_acpi_ctype[(unsigned char)(c)] & (_ACPI_DI))
#define ACPI_IS_SPACE(c)  (_acpi_ctype[(unsigned char)(c)] & (_ACPI_SP))
#define ACPI_IS_XDIGIT(c) (_acpi_ctype[(unsigned char)(c)] & (_ACPI_XD))
#define ACPI_IS_UPPER(c)  (_acpi_ctype[(unsigned char)(c)] & (_ACPI_UP))
#define ACPI_IS_LOWER(c)  (_acpi_ctype[(unsigned char)(c)] & (_ACPI_LO))
#define ACPI_IS_PRINT(c)  (_acpi_ctype[(unsigned char)(c)] & (_ACPI_LO | _ACPI_UP | _ACPI_DI | _ACPI_XS | _ACPI_PU))
#define ACPI_IS_ALPHA(c)  (_acpi_ctype[(unsigned char)(c)] & (_ACPI_LO | _ACPI_UP))

#endif				/* !ACPI_USE_SYSTEM_CLIBRARY */

#define ACPI_IS_ASCII(c)  ((c) < 0x80)

/*
 * utcopy - Object construction and conversion interfaces
 */
acpi_status
acpi_ut_build_simple_object(union acpi_operand_object *obj,
			    union acpi_object *user_obj,
			    u8 *data_space, u32 *buffer_space_used);

acpi_status
acpi_ut_build_package_object(union acpi_operand_object *obj,
			     u8 *buffer, u32 *space_used);

acpi_status
acpi_ut_copy_iobject_to_eobject(union acpi_operand_object *obj,
				struct acpi_buffer *ret_buffer);

acpi_status
acpi_ut_copy_eobject_to_iobject(union acpi_object *obj,
				union acpi_operand_object **internal_obj);

acpi_status
acpi_ut_copy_isimple_to_isimple(union acpi_operand_object *source_obj,
				union acpi_operand_object *dest_obj);

acpi_status
acpi_ut_copy_iobject_to_iobject(union acpi_operand_object *source_desc,
				union acpi_operand_object **dest_desc,
				struct acpi_walk_state *walk_state);

/*
 * utcreate - Object creation
 */
acpi_status
acpi_ut_update_object_reference(union acpi_operand_object *object, u16 action);

/*
 * utdebug - Debug interfaces
 */
void acpi_ut_init_stack_ptr_trace(void);

void acpi_ut_track_stack_ptr(void);

void
acpi_ut_trace(u32 line_number,
	      const char *function_name,
	      const char *module_name, u32 component_id);

void
acpi_ut_trace_ptr(u32 line_number,
		  const char *function_name,
		  const char *module_name, u32 component_id, void *pointer);

void
acpi_ut_trace_u32(u32 line_number,
		  const char *function_name,
		  const char *module_name, u32 component_id, u32 integer);

void
acpi_ut_trace_str(u32 line_number,
		  const char *function_name,
		  const char *module_name, u32 component_id, char *string);

void
acpi_ut_exit(u32 line_number,
	     const char *function_name,
	     const char *module_name, u32 component_id);

void
acpi_ut_status_exit(u32 line_number,
		    const char *function_name,
		    const char *module_name,
		    u32 component_id, acpi_status status);

void
acpi_ut_value_exit(u32 line_number,
		   const char *function_name,
		   const char *module_name, u32 component_id, u64 value);

void
acpi_ut_ptr_exit(u32 line_number,
		 const char *function_name,
		 const char *module_name, u32 component_id, u8 *ptr);

void
acpi_ut_debug_dump_buffer(u8 *buffer, u32 count, u32 display, u32 component_id);

void acpi_ut_dump_buffer(u8 *buffer, u32 count, u32 display, u32 offset);

void acpi_ut_report_error(char *module_name, u32 line_number);

void acpi_ut_report_info(char *module_name, u32 line_number);

void acpi_ut_report_warning(char *module_name, u32 line_number);

/*
 * utdelete - Object deletion and reference counts
 */
void acpi_ut_add_reference(union acpi_operand_object *object);

void acpi_ut_remove_reference(union acpi_operand_object *object);

void acpi_ut_delete_internal_package_object(union acpi_operand_object *object);

void acpi_ut_delete_internal_simple_object(union acpi_operand_object *object);

void acpi_ut_delete_internal_object_list(union acpi_operand_object **obj_list);

/*
 * uteval - object evaluation
 */
acpi_status
acpi_ut_evaluate_object(struct acpi_namespace_node *prefix_node,
			char *path,
			u32 expected_return_btypes,
			union acpi_operand_object **return_desc);

acpi_status
acpi_ut_evaluate_numeric_object(char *object_name,
				struct acpi_namespace_node *device_node,
				u64 *value);

acpi_status
acpi_ut_execute_STA(struct acpi_namespace_node *device_node, u32 *status_flags);

acpi_status
acpi_ut_execute_power_methods(struct acpi_namespace_node *device_node,
			      const char **method_names,
			      u8 method_count, u8 *out_values);

/*
 * utfileio - file operations
 */
#ifdef ACPI_APPLICATION
acpi_status
acpi_ut_read_table_from_file(char *filename, struct acpi_table_header **table);
#endif

/*
 * utids - device ID support
 */
acpi_status
acpi_ut_execute_HID(struct acpi_namespace_node *device_node,
		    struct acpi_pnp_device_id ** return_id);

acpi_status
acpi_ut_execute_UID(struct acpi_namespace_node *device_node,
		    struct acpi_pnp_device_id ** return_id);

acpi_status
acpi_ut_execute_SUB(struct acpi_namespace_node *device_node,
		    struct acpi_pnp_device_id **return_id);

acpi_status
acpi_ut_execute_CID(struct acpi_namespace_node *device_node,
		    struct acpi_pnp_device_id_list ** return_cid_list);

/*
 * utlock - reader/writer locks
 */
acpi_status acpi_ut_create_rw_lock(struct acpi_rw_lock *lock);

void acpi_ut_delete_rw_lock(struct acpi_rw_lock *lock);

acpi_status acpi_ut_acquire_read_lock(struct acpi_rw_lock *lock);

acpi_status acpi_ut_release_read_lock(struct acpi_rw_lock *lock);

acpi_status acpi_ut_acquire_write_lock(struct acpi_rw_lock *lock);

void acpi_ut_release_write_lock(struct acpi_rw_lock *lock);

/*
 * utobject - internal object create/delete/cache routines
 */
union acpi_operand_object *acpi_ut_create_internal_object_dbg(const char
							      *module_name,
							      u32 line_number,
							      u32 component_id,
							      acpi_object_type
							      type);

void *acpi_ut_allocate_object_desc_dbg(const char *module_name,
				       u32 line_number, u32 component_id);

#define acpi_ut_create_internal_object(t) acpi_ut_create_internal_object_dbg (_acpi_module_name,__LINE__,_COMPONENT,t)
#define acpi_ut_allocate_object_desc()  acpi_ut_allocate_object_desc_dbg (_acpi_module_name,__LINE__,_COMPONENT)

void acpi_ut_delete_object_desc(union acpi_operand_object *object);

u8 acpi_ut_valid_internal_object(void *object);

union acpi_operand_object *acpi_ut_create_package_object(u32 count);

union acpi_operand_object *acpi_ut_create_integer_object(u64 value);

union acpi_operand_object *acpi_ut_create_buffer_object(acpi_size buffer_size);

union acpi_operand_object *acpi_ut_create_string_object(acpi_size string_size);

acpi_status
acpi_ut_get_object_size(union acpi_operand_object *obj, acpi_size * obj_length);

/*
 * utosi - Support for the _OSI predefined control method
 */
acpi_status acpi_ut_initialize_interfaces(void);

acpi_status acpi_ut_interface_terminate(void);

acpi_status acpi_ut_install_interface(acpi_string interface_name);

acpi_status acpi_ut_remove_interface(acpi_string interface_name);

acpi_status acpi_ut_update_interfaces(u8 action);

struct acpi_interface_info *acpi_ut_get_interface(acpi_string interface_name);

acpi_status acpi_ut_osi_implementation(struct acpi_walk_state *walk_state);

/*
 * utpredef - support for predefined names
 */
const union acpi_predefined_info *acpi_ut_get_next_predefined_method(const union
								     acpi_predefined_info
								     *this_name);

const union acpi_predefined_info *acpi_ut_match_predefined_method(char *name);

const union acpi_predefined_info *acpi_ut_match_resource_name(char *name);

void
acpi_ut_display_predefined_method(char *buffer,
				  const union acpi_predefined_info *this_name,
				  u8 multi_line);

void acpi_ut_get_expected_return_types(char *buffer, u32 expected_btypes);

u32 acpi_ut_get_resource_bit_width(char *buffer, u16 types);

/*
 * utstate - Generic state creation/cache routines
 */
void
acpi_ut_push_generic_state(union acpi_generic_state **list_head,
			   union acpi_generic_state *state);

union acpi_generic_state *acpi_ut_pop_generic_state(union acpi_generic_state
						    **list_head);

union acpi_generic_state *acpi_ut_create_generic_state(void);

struct acpi_thread_state *acpi_ut_create_thread_state(void);

union acpi_generic_state *acpi_ut_create_update_state(union acpi_operand_object
						      *object, u16 action);

union acpi_generic_state *acpi_ut_create_pkg_state(void *internal_object,
						   void *external_object,
						   u16 index);

acpi_status
acpi_ut_create_update_state_and_push(union acpi_operand_object *object,
				     u16 action,
				     union acpi_generic_state **state_list);

#ifdef	ACPI_FUTURE_USAGE
acpi_status
acpi_ut_create_pkg_state_and_push(void *internal_object,
				  void *external_object,
				  u16 index,
				  union acpi_generic_state **state_list);
#endif				/* ACPI_FUTURE_USAGE */

union acpi_generic_state *acpi_ut_create_control_state(void);

void acpi_ut_delete_generic_state(union acpi_generic_state *state);

/*
 * utmath
 */
acpi_status
acpi_ut_divide(u64 in_dividend,
	       u64 in_divisor, u64 *out_quotient, u64 *out_remainder);

acpi_status
acpi_ut_short_divide(u64 in_dividend,
		     u32 divisor, u64 *out_quotient, u32 *out_remainder);

/*
 * utmisc
 */
const struct acpi_exception_info *acpi_ut_validate_exception(acpi_status
							     status);

u8 acpi_ut_is_pci_root_bridge(char *id);

u8 acpi_ut_is_aml_table(struct acpi_table_header *table);

acpi_status
acpi_ut_walk_package_tree(union acpi_operand_object *source_object,
			  void *target_object,
			  acpi_pkg_callback walk_callback, void *context);

/* Values for Base above (16=Hex, 10=Decimal) */

#define ACPI_ANY_BASE        0

u32 acpi_ut_dword_byte_swap(u32 value);

void acpi_ut_set_integer_width(u8 revision);

#ifdef ACPI_DEBUG_OUTPUT
void
acpi_ut_display_init_pathname(u8 type,
			      struct acpi_namespace_node *obj_handle,
			      char *path);
#endif

/*
 * utownerid - Support for Table/Method Owner IDs
 */
acpi_status acpi_ut_allocate_owner_id(acpi_owner_id * owner_id);

void acpi_ut_release_owner_id(acpi_owner_id * owner_id);

/*
 * utresrc
 */
acpi_status
acpi_ut_walk_aml_resources(struct acpi_walk_state *walk_state,
			   u8 *aml,
			   acpi_size aml_length,
			   acpi_walk_aml_callback user_function,
			   void **context);

acpi_status
acpi_ut_validate_resource(struct acpi_walk_state *walk_state,
			  void *aml, u8 *return_index);

u32 acpi_ut_get_descriptor_length(void *aml);

u16 acpi_ut_get_resource_length(void *aml);

u8 acpi_ut_get_resource_header_length(void *aml);

u8 acpi_ut_get_resource_type(void *aml);

acpi_status
acpi_ut_get_resource_end_tag(union acpi_operand_object *obj_desc, u8 **end_tag);

/*
 * utstring - String and character utilities
 */
void acpi_ut_strupr(char *src_string);

void acpi_ut_strlwr(char *src_string);

int acpi_ut_stricmp(char *string1, char *string2);

acpi_status acpi_ut_strtoul64(char *string, u32 base, u64 *ret_integer);

void acpi_ut_print_string(char *string, u16 max_length);

void ut_convert_backslashes(char *pathname);

u8 acpi_ut_valid_acpi_name(char *name);

u8 acpi_ut_valid_acpi_char(char character, u32 position);

void acpi_ut_repair_name(char *name);

#if defined (ACPI_DEBUGGER) || defined (ACPI_APPLICATION)
u8 acpi_ut_safe_strcpy(char *dest, acpi_size dest_size, char *source);

u8 acpi_ut_safe_strcat(char *dest, acpi_size dest_size, char *source);

u8
acpi_ut_safe_strncat(char *dest,
		     acpi_size dest_size,
		     char *source, acpi_size max_transfer_length);
#endif

/*
 * utmutex - mutex support
 */
acpi_status acpi_ut_mutex_initialize(void);

void acpi_ut_mutex_terminate(void);

acpi_status acpi_ut_acquire_mutex(acpi_mutex_handle mutex_id);

acpi_status acpi_ut_release_mutex(acpi_mutex_handle mutex_id);

/*
 * utalloc - memory allocation and object caching
 */
acpi_status acpi_ut_create_caches(void);

acpi_status acpi_ut_delete_caches(void);

acpi_status acpi_ut_validate_buffer(struct acpi_buffer *buffer);

acpi_status
acpi_ut_initialize_buffer(struct acpi_buffer *buffer,
			  acpi_size required_length);

#ifdef ACPI_DBG_TRACK_ALLOCATIONS
void *acpi_ut_allocate_and_track(acpi_size size,
				 u32 component, const char *module, u32 line);

void *acpi_ut_allocate_zeroed_and_track(acpi_size size,
					u32 component,
					const char *module, u32 line);

void
acpi_ut_free_and_track(void *address,
		       u32 component, const char *module, u32 line);

#ifdef	ACPI_FUTURE_USAGE
void acpi_ut_dump_allocation_info(void);
#endif				/* ACPI_FUTURE_USAGE */

void acpi_ut_dump_allocations(u32 component, const char *module);

acpi_status
acpi_ut_create_list(char *list_name,
		    u16 object_size, struct acpi_memory_list **return_cache);

#endif				/* ACPI_DBG_TRACK_ALLOCATIONS */

/*
 * utaddress - address range check
 */
acpi_status
acpi_ut_add_address_range(acpi_adr_space_type space_id,
			  acpi_physical_address address,
			  u32 length, struct acpi_namespace_node *region_node);

void
acpi_ut_remove_address_range(acpi_adr_space_type space_id,
			     struct acpi_namespace_node *region_node);

u32
acpi_ut_check_address_range(acpi_adr_space_type space_id,
			    acpi_physical_address address, u32 length, u8 warn);

void acpi_ut_delete_address_lists(void);

/*
 * utxferror - various error/warning output functions
 */
void ACPI_INTERNAL_VAR_XFACE
acpi_ut_predefined_warning(const char *module_name,
			   u32 line_number,
			   char *pathname,
			   u8 node_flags, const char *format, ...);

void ACPI_INTERNAL_VAR_XFACE
acpi_ut_predefined_info(const char *module_name,
			u32 line_number,
			char *pathname, u8 node_flags, const char *format, ...);

void ACPI_INTERNAL_VAR_XFACE
acpi_ut_predefined_bios_error(const char *module_name,
			      u32 line_number,
			      char *pathname,
			      u8 node_flags, const char *format, ...);

void
acpi_ut_namespace_error(const char *module_name,
			u32 line_number,
			const char *internal_name, acpi_status lookup_status);

void
acpi_ut_method_error(const char *module_name,
		     u32 line_number,
		     const char *message,
		     struct acpi_namespace_node *node,
		     const char *path, acpi_status lookup_status);

/*
 * Utility functions for ACPI names and IDs
 */
const struct ah_predefined_name *acpi_ah_match_predefined_name(char *nameseg);

const struct ah_device_id *acpi_ah_match_hardware_id(char *hid);

#endif				/* _ACUTILS_H */
