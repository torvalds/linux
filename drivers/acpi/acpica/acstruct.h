/* SPDX-License-Identifier: BSD-3-Clause OR GPL-2.0 */
/******************************************************************************
 *
 * Name: acstruct.h - Internal structs
 *
 * Copyright (C) 2000 - 2021, Intel Corp.
 *
 *****************************************************************************/

#ifndef __ACSTRUCT_H__
#define __ACSTRUCT_H__

/* acpisrc:struct_defs -- for acpisrc conversion */

/*****************************************************************************
 *
 * Tree walking typedefs and structs
 *
 ****************************************************************************/

/*
 * Walk state - current state of a parse tree walk. Used for both a leisurely
 * stroll through the tree (for whatever reason), and for control method
 * execution.
 */
#define ACPI_NEXT_OP_DOWNWARD       1
#define ACPI_NEXT_OP_UPWARD         2

/*
 * Groups of definitions for walk_type used for different implementations of
 * walkers (never simultaneously) - flags for interpreter:
 */
#define ACPI_WALK_NON_METHOD        0
#define ACPI_WALK_METHOD            0x01
#define ACPI_WALK_METHOD_RESTART    0x02

struct acpi_walk_state {
	struct acpi_walk_state *next;	/* Next walk_state in list */
	u8 descriptor_type;	/* To differentiate various internal objs */
	u8 walk_type;
	u16 opcode;		/* Current AML opcode */
	u8 next_op_info;	/* Info about next_op */
	u8 num_operands;	/* Stack pointer for Operands[] array */
	u8 operand_index;	/* Index into operand stack, to be used by acpi_ds_obj_stack_push */
	acpi_owner_id owner_id;	/* Owner of objects created during the walk */
	u8 last_predicate;	/* Result of last predicate */
	u8 current_result;
	u8 return_used;
	u8 scope_depth;
	u8 pass_number;		/* Parse pass during table load */
	u8 namespace_override;	/* Override existing objects */
	u8 result_size;		/* Total elements for the result stack */
	u8 result_count;	/* Current number of occupied elements of result stack */
	u8 *aml;
	u32 arg_types;
	u32 method_breakpoint;	/* For single stepping */
	u32 user_breakpoint;	/* User AML breakpoint */
	u32 parse_flags;

	struct acpi_parse_state parser_state;	/* Current state of parser */
	u32 prev_arg_types;
	u32 arg_count;		/* push for fixed or var args */
	u16 method_nesting_depth;
	u8 method_is_nested;

	struct acpi_namespace_node arguments[ACPI_METHOD_NUM_ARGS];	/* Control method arguments */
	struct acpi_namespace_node local_variables[ACPI_METHOD_NUM_LOCALS];	/* Control method locals */
	union acpi_operand_object *operands[ACPI_OBJ_NUM_OPERANDS + 1];	/* Operands passed to the interpreter (+1 for NULL terminator) */
	union acpi_operand_object **params;

	u8 *aml_last_while;
	union acpi_operand_object **caller_return_desc;
	union acpi_generic_state *control_state;	/* List of control states (nested IFs) */
	struct acpi_namespace_node *deferred_node;	/* Used when executing deferred opcodes */
	union acpi_operand_object *implicit_return_obj;
	struct acpi_namespace_node *method_call_node;	/* Called method Node */
	union acpi_parse_object *method_call_op;	/* method_call Op if running a method */
	union acpi_operand_object *method_desc;	/* Method descriptor if running a method */
	struct acpi_namespace_node *method_node;	/* Method node if running a method */
	char *method_pathname;	/* Full pathname of running method */
	union acpi_parse_object *op;	/* Current parser op */
	const struct acpi_opcode_info *op_info;	/* Info on current opcode */
	union acpi_parse_object *origin;	/* Start of walk [Obsolete] */
	union acpi_operand_object *result_obj;
	union acpi_generic_state *results;	/* Stack of accumulated results */
	union acpi_operand_object *return_desc;	/* Return object, if any */
	union acpi_generic_state *scope_info;	/* Stack of nested scopes */
	union acpi_parse_object *prev_op;	/* Last op that was processed */
	union acpi_parse_object *next_op;	/* next op to be processed */
	struct acpi_thread_state *thread;
	acpi_parse_downwards descending_callback;
	acpi_parse_upwards ascending_callback;
};

/* Info used by acpi_ns_initialize_objects and acpi_ds_initialize_objects */

struct acpi_init_walk_info {
	u32 table_index;
	u32 object_count;
	u32 method_count;
	u32 serial_method_count;
	u32 non_serial_method_count;
	u32 serialized_method_count;
	u32 device_count;
	u32 op_region_count;
	u32 field_count;
	u32 buffer_count;
	u32 package_count;
	u32 op_region_init;
	u32 field_init;
	u32 buffer_init;
	u32 package_init;
	acpi_owner_id owner_id;
};

struct acpi_get_devices_info {
	acpi_walk_callback user_function;
	void *context;
	const char *hid;
};

union acpi_aml_operands {
	union acpi_operand_object *operands[7];

	struct {
		struct acpi_object_integer *type;
		struct acpi_object_integer *code;
		struct acpi_object_integer *argument;

	} fatal;

	struct {
		union acpi_operand_object *source;
		struct acpi_object_integer *index;
		union acpi_operand_object *target;

	} index;

	struct {
		union acpi_operand_object *source;
		struct acpi_object_integer *index;
		struct acpi_object_integer *length;
		union acpi_operand_object *target;

	} mid;
};

/*
 * Structure used to pass object evaluation information and parameters.
 * Purpose is to reduce CPU stack use.
 */
struct acpi_evaluate_info {
	/* The first 3 elements are passed by the caller to acpi_ns_evaluate */

	struct acpi_namespace_node *prefix_node;	/* Input: starting node */
	const char *relative_pathname;	/* Input: path relative to prefix_node */
	union acpi_operand_object **parameters;	/* Input: argument list */

	struct acpi_namespace_node *node;	/* Resolved node (prefix_node:relative_pathname) */
	union acpi_operand_object *obj_desc;	/* Object attached to the resolved node */
	char *full_pathname;	/* Full pathname of the resolved node */

	const union acpi_predefined_info *predefined;	/* Used if Node is a predefined name */
	union acpi_operand_object *return_object;	/* Object returned from the evaluation */
	union acpi_operand_object *parent_package;	/* Used if return object is a Package */

	u32 return_flags;	/* Used for return value analysis */
	u32 return_btype;	/* Bitmapped type of the returned object */
	u16 param_count;	/* Count of the input argument list */
	u16 node_flags;		/* Same as Node->Flags */
	u8 pass_number;		/* Parser pass number */
	u8 return_object_type;	/* Object type of the returned object */
	u8 flags;		/* General flags */
};

/* Values for Flags above */

#define ACPI_IGNORE_RETURN_VALUE    1

/* Defines for return_flags field above */

#define ACPI_OBJECT_REPAIRED        1
#define ACPI_OBJECT_WRAPPED         2

/* Info used by acpi_ns_initialize_devices */

struct acpi_device_walk_info {
	struct acpi_table_desc *table_desc;
	struct acpi_evaluate_info *evaluate_info;
	u32 device_count;
	u32 num_STA;
	u32 num_INI;
};

/* Info used by Acpi  acpi_db_display_fields */

struct acpi_region_walk_info {
	u32 debug_level;
	u32 count;
	acpi_owner_id owner_id;
	u8 display_type;
	u32 address_space_id;
};

/* TBD: [Restructure] Merge with struct above */

struct acpi_walk_info {
	u32 debug_level;
	u32 count;
	acpi_owner_id owner_id;
	u8 display_type;
};

/* Display Types */

#define ACPI_DISPLAY_SUMMARY        (u8) 0
#define ACPI_DISPLAY_OBJECTS        (u8) 1
#define ACPI_DISPLAY_MASK           (u8) 1

#define ACPI_DISPLAY_SHORT          (u8) 2

#endif
