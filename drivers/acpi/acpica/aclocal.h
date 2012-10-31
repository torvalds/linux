/******************************************************************************
 *
 * Name: aclocal.h - Internal data types used across the ACPI subsystem
 *
 *****************************************************************************/

/*
 * Copyright (C) 2000 - 2012, Intel Corp.
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

#ifndef __ACLOCAL_H__
#define __ACLOCAL_H__

/* acpisrc:struct_defs -- for acpisrc conversion */

#define ACPI_SERIALIZED                 0xFF

typedef u32 acpi_mutex_handle;
#define ACPI_GLOBAL_LOCK                (acpi_semaphore) (-1)

/* Total number of aml opcodes defined */

#define AML_NUM_OPCODES                 0x81

/* Forward declarations */

struct acpi_walk_state;
struct acpi_obj_mutex;
union acpi_parse_object;

/*****************************************************************************
 *
 * Mutex typedefs and structs
 *
 ****************************************************************************/

/*
 * Predefined handles for the mutex objects used within the subsystem
 * All mutex objects are automatically created by acpi_ut_mutex_initialize.
 *
 * The acquire/release ordering protocol is implied via this list. Mutexes
 * with a lower value must be acquired before mutexes with a higher value.
 *
 * NOTE: any changes here must be reflected in the acpi_gbl_mutex_names
 * table below also!
 */
#define ACPI_MTX_INTERPRETER            0	/* AML Interpreter, main lock */
#define ACPI_MTX_NAMESPACE              1	/* ACPI Namespace */
#define ACPI_MTX_TABLES                 2	/* Data for ACPI tables */
#define ACPI_MTX_EVENTS                 3	/* Data for ACPI events */
#define ACPI_MTX_CACHES                 4	/* Internal caches, general purposes */
#define ACPI_MTX_MEMORY                 5	/* Debug memory tracking lists */
#define ACPI_MTX_DEBUG_CMD_COMPLETE     6	/* AML debugger */
#define ACPI_MTX_DEBUG_CMD_READY        7	/* AML debugger */

#define ACPI_MAX_MUTEX                  7
#define ACPI_NUM_MUTEX                  ACPI_MAX_MUTEX+1

/* Lock structure for reader/writer interfaces */

struct acpi_rw_lock {
	acpi_mutex writer_mutex;
	acpi_mutex reader_mutex;
	u32 num_readers;
};

/*
 * Predefined handles for spinlocks used within the subsystem.
 * These spinlocks are created by acpi_ut_mutex_initialize
 */
#define ACPI_LOCK_GPES                  0
#define ACPI_LOCK_HARDWARE              1

#define ACPI_MAX_LOCK                   1
#define ACPI_NUM_LOCK                   ACPI_MAX_LOCK+1

/* This Thread ID means that the mutex is not in use (unlocked) */

#define ACPI_MUTEX_NOT_ACQUIRED         (acpi_thread_id) 0

/* Table for the global mutexes */

struct acpi_mutex_info {
	acpi_mutex mutex;
	u32 use_count;
	acpi_thread_id thread_id;
};

/* Lock flag parameter for various interfaces */

#define ACPI_MTX_DO_NOT_LOCK            0
#define ACPI_MTX_LOCK                   1

/* Field access granularities */

#define ACPI_FIELD_BYTE_GRANULARITY     1
#define ACPI_FIELD_WORD_GRANULARITY     2
#define ACPI_FIELD_DWORD_GRANULARITY    4
#define ACPI_FIELD_QWORD_GRANULARITY    8

#define ACPI_ENTRY_NOT_FOUND            NULL

/*****************************************************************************
 *
 * Namespace typedefs and structs
 *
 ****************************************************************************/

/* Operational modes of the AML interpreter/scanner */

typedef enum {
	ACPI_IMODE_LOAD_PASS1 = 0x01,
	ACPI_IMODE_LOAD_PASS2 = 0x02,
	ACPI_IMODE_EXECUTE = 0x03
} acpi_interpreter_mode;

/*
 * The Namespace Node describes a named object that appears in the AML.
 * descriptor_type is used to differentiate between internal descriptors.
 *
 * The node is optimized for both 32-bit and 64-bit platforms:
 * 20 bytes for the 32-bit case, 32 bytes for the 64-bit case.
 *
 * Note: The descriptor_type and Type fields must appear in the identical
 * position in both the struct acpi_namespace_node and union acpi_operand_object
 * structures.
 */
struct acpi_namespace_node {
	union acpi_operand_object *object;	/* Interpreter object */
	u8 descriptor_type;	/* Differentiate object descriptor types */
	u8 type;		/* ACPI Type associated with this name */
	u8 flags;		/* Miscellaneous flags */
	acpi_owner_id owner_id;	/* Node creator */
	union acpi_name_union name;	/* ACPI Name, always 4 chars per ACPI spec */
	struct acpi_namespace_node *parent;	/* Parent node */
	struct acpi_namespace_node *child;	/* First child */
	struct acpi_namespace_node *peer;	/* First peer */

	/*
	 * The following fields are used by the ASL compiler and disassembler only
	 */
#ifdef ACPI_LARGE_NAMESPACE_NODE
	union acpi_parse_object *op;
	u32 value;
	u32 length;
#endif
};

/* Namespace Node flags */

#define ANOBJ_RESERVED                  0x01	/* Available for use */
#define ANOBJ_TEMPORARY                 0x02	/* Node is create by a method and is temporary */
#define ANOBJ_METHOD_ARG                0x04	/* Node is a method argument */
#define ANOBJ_METHOD_LOCAL              0x08	/* Node is a method local */
#define ANOBJ_SUBTREE_HAS_INI           0x10	/* Used to optimize device initialization */
#define ANOBJ_EVALUATED                 0x20	/* Set on first evaluation of node */
#define ANOBJ_ALLOCATED_BUFFER          0x40	/* Method AML buffer is dynamic (install_method) */

#define ANOBJ_IS_EXTERNAL               0x08	/* i_aSL only: This object created via External() */
#define ANOBJ_METHOD_NO_RETVAL          0x10	/* i_aSL only: Method has no return value */
#define ANOBJ_METHOD_SOME_NO_RETVAL     0x20	/* i_aSL only: Method has at least one return value */
#define ANOBJ_IS_BIT_OFFSET             0x40	/* i_aSL only: Reference is a bit offset */
#define ANOBJ_IS_REFERENCED             0x80	/* i_aSL only: Object was referenced */

/* Internal ACPI table management - master table list */

struct acpi_table_list {
	struct acpi_table_desc *tables;	/* Table descriptor array */
	u32 current_table_count;	/* Tables currently in the array */
	u32 max_table_count;	/* Max tables array will hold */
	u8 flags;
};

/* Flags for above */

#define ACPI_ROOT_ORIGIN_UNKNOWN        (0)	/* ~ORIGIN_ALLOCATED */
#define ACPI_ROOT_ORIGIN_ALLOCATED      (1)
#define ACPI_ROOT_ALLOW_RESIZE          (2)

/* Predefined (fixed) table indexes */

#define ACPI_TABLE_INDEX_DSDT           (0)
#define ACPI_TABLE_INDEX_FACS           (1)

struct acpi_find_context {
	char *search_for;
	acpi_handle *list;
	u32 *count;
};

struct acpi_ns_search_data {
	struct acpi_namespace_node *node;
};

/* Object types used during package copies */

#define ACPI_COPY_TYPE_SIMPLE           0
#define ACPI_COPY_TYPE_PACKAGE          1

/* Info structure used to convert external<->internal namestrings */

struct acpi_namestring_info {
	const char *external_name;
	const char *next_external_char;
	char *internal_name;
	u32 length;
	u32 num_segments;
	u32 num_carats;
	u8 fully_qualified;
};

/* Field creation info */

struct acpi_create_field_info {
	struct acpi_namespace_node *region_node;
	struct acpi_namespace_node *field_node;
	struct acpi_namespace_node *register_node;
	struct acpi_namespace_node *data_register_node;
	struct acpi_namespace_node *connection_node;
	u8 *resource_buffer;
	u32 bank_value;
	u32 field_bit_position;
	u32 field_bit_length;
	u16 resource_length;
	u8 field_flags;
	u8 attribute;
	u8 field_type;
	u8 access_length;
};

typedef
acpi_status(*ACPI_INTERNAL_METHOD) (struct acpi_walk_state * walk_state);

/*
 * Bitmapped ACPI types.  Used internally only
 */
#define ACPI_BTYPE_ANY                  0x00000000
#define ACPI_BTYPE_INTEGER              0x00000001
#define ACPI_BTYPE_STRING               0x00000002
#define ACPI_BTYPE_BUFFER               0x00000004
#define ACPI_BTYPE_PACKAGE              0x00000008
#define ACPI_BTYPE_FIELD_UNIT           0x00000010
#define ACPI_BTYPE_DEVICE               0x00000020
#define ACPI_BTYPE_EVENT                0x00000040
#define ACPI_BTYPE_METHOD               0x00000080
#define ACPI_BTYPE_MUTEX                0x00000100
#define ACPI_BTYPE_REGION               0x00000200
#define ACPI_BTYPE_POWER                0x00000400
#define ACPI_BTYPE_PROCESSOR            0x00000800
#define ACPI_BTYPE_THERMAL              0x00001000
#define ACPI_BTYPE_BUFFER_FIELD         0x00002000
#define ACPI_BTYPE_DDB_HANDLE           0x00004000
#define ACPI_BTYPE_DEBUG_OBJECT         0x00008000
#define ACPI_BTYPE_REFERENCE            0x00010000
#define ACPI_BTYPE_RESOURCE             0x00020000

#define ACPI_BTYPE_COMPUTE_DATA         (ACPI_BTYPE_INTEGER | ACPI_BTYPE_STRING | ACPI_BTYPE_BUFFER)

#define ACPI_BTYPE_DATA                 (ACPI_BTYPE_COMPUTE_DATA  | ACPI_BTYPE_PACKAGE)
#define ACPI_BTYPE_DATA_REFERENCE       (ACPI_BTYPE_DATA | ACPI_BTYPE_REFERENCE | ACPI_BTYPE_DDB_HANDLE)
#define ACPI_BTYPE_DEVICE_OBJECTS       (ACPI_BTYPE_DEVICE | ACPI_BTYPE_THERMAL | ACPI_BTYPE_PROCESSOR)
#define ACPI_BTYPE_OBJECTS_AND_REFS     0x0001FFFF	/* ARG or LOCAL */
#define ACPI_BTYPE_ALL_OBJECTS          0x0000FFFF

/*
 * Information structure for ACPI predefined names.
 * Each entry in the table contains the following items:
 *
 * name                 - The ACPI reserved name
 * param_count          - Number of arguments to the method
 * expected_return_btypes - Allowed type(s) for the return value
 */
struct acpi_name_info {
	char name[ACPI_NAME_SIZE];
	u8 param_count;
	u8 expected_btypes;
};

/*
 * Secondary information structures for ACPI predefined objects that return
 * package objects. This structure appears as the next entry in the table
 * after the NAME_INFO structure above.
 *
 * The reason for this is to minimize the size of the predefined name table.
 */

/*
 * Used for ACPI_PTYPE1_FIXED, ACPI_PTYPE1_VAR, ACPI_PTYPE2,
 * ACPI_PTYPE2_MIN, ACPI_PTYPE2_PKG_COUNT, ACPI_PTYPE2_COUNT,
 * ACPI_PTYPE2_FIX_VAR
 */
struct acpi_package_info {
	u8 type;
	u8 object_type1;
	u8 count1;
	u8 object_type2;
	u8 count2;
	u8 reserved;
};

/* Used for ACPI_PTYPE2_FIXED */

struct acpi_package_info2 {
	u8 type;
	u8 count;
	u8 object_type[4];
};

/* Used for ACPI_PTYPE1_OPTION */

struct acpi_package_info3 {
	u8 type;
	u8 count;
	u8 object_type[2];
	u8 tail_object_type;
	u8 reserved;
};

union acpi_predefined_info {
	struct acpi_name_info info;
	struct acpi_package_info ret_info;
	struct acpi_package_info2 ret_info2;
	struct acpi_package_info3 ret_info3;
};

/* Data block used during object validation */

struct acpi_predefined_data {
	char *pathname;
	const union acpi_predefined_info *predefined;
	union acpi_operand_object *parent_package;
	struct acpi_namespace_node *node;
	u32 flags;
	u8 node_flags;
};

/* Defines for Flags field above */

#define ACPI_OBJECT_REPAIRED    1
#define ACPI_OBJECT_WRAPPED     2

/*
 * Bitmapped return value types
 * Note: the actual data types must be contiguous, a loop in nspredef.c
 * depends on this.
 */
#define ACPI_RTYPE_ANY                  0x00
#define ACPI_RTYPE_NONE                 0x01
#define ACPI_RTYPE_INTEGER              0x02
#define ACPI_RTYPE_STRING               0x04
#define ACPI_RTYPE_BUFFER               0x08
#define ACPI_RTYPE_PACKAGE              0x10
#define ACPI_RTYPE_REFERENCE            0x20
#define ACPI_RTYPE_ALL                  0x3F

#define ACPI_NUM_RTYPES                 5	/* Number of actual object types */

/*****************************************************************************
 *
 * Event typedefs and structs
 *
 ****************************************************************************/

/* Dispatch info for each GPE -- either a method or handler, cannot be both */

struct acpi_gpe_handler_info {
	acpi_gpe_handler address;	/* Address of handler, if any */
	void *context;		/* Context to be passed to handler */
	struct acpi_namespace_node *method_node;	/* Method node for this GPE level (saved) */
	u8 original_flags;      /* Original (pre-handler) GPE info */
	u8 originally_enabled;  /* True if GPE was originally enabled */
};

/* Notify info for implicit notify, multiple device objects */

struct acpi_gpe_notify_info {
	struct acpi_namespace_node *device_node;	/* Device to be notified */
	struct acpi_gpe_notify_info *next;
};

struct acpi_gpe_notify_object {
	struct acpi_namespace_node *node;
	struct acpi_gpe_notify_object *next;
};

union acpi_gpe_dispatch_info {
	struct acpi_namespace_node *method_node;	/* Method node for this GPE level */
	struct acpi_gpe_handler_info *handler;  /* Installed GPE handler */
	struct acpi_gpe_notify_info *notify_list;	/* List of _PRW devices for implicit notifies */
};

/*
 * Information about a GPE, one per each GPE in an array.
 * NOTE: Important to keep this struct as small as possible.
 */
struct acpi_gpe_event_info {
	union acpi_gpe_dispatch_info dispatch;	/* Either Method, Handler, or notify_list */
	struct acpi_gpe_register_info *register_info;	/* Backpointer to register info */
	u8 flags;		/* Misc info about this GPE */
	u8 gpe_number;		/* This GPE */
	u8 runtime_count;	/* References to a run GPE */
};

/* Information about a GPE register pair, one per each status/enable pair in an array */

struct acpi_gpe_register_info {
	struct acpi_generic_address status_address;	/* Address of status reg */
	struct acpi_generic_address enable_address;	/* Address of enable reg */
	u8 enable_for_wake;	/* GPEs to keep enabled when sleeping */
	u8 enable_for_run;	/* GPEs to keep enabled when running */
	u8 base_gpe_number;	/* Base GPE number for this register */
};

/*
 * Information about a GPE register block, one per each installed block --
 * GPE0, GPE1, and one per each installed GPE Block Device.
 */
struct acpi_gpe_block_info {
	struct acpi_namespace_node *node;
	struct acpi_gpe_block_info *previous;
	struct acpi_gpe_block_info *next;
	struct acpi_gpe_xrupt_info *xrupt_block;	/* Backpointer to interrupt block */
	struct acpi_gpe_register_info *register_info;	/* One per GPE register pair */
	struct acpi_gpe_event_info *event_info;	/* One for each GPE */
	struct acpi_generic_address block_address;	/* Base address of the block */
	u32 register_count;	/* Number of register pairs in block */
	u16 gpe_count;		/* Number of individual GPEs in block */
	u8 block_base_number;	/* Base GPE number for this block */
	u8 initialized;         /* TRUE if this block is initialized */
};

/* Information about GPE interrupt handlers, one per each interrupt level used for GPEs */

struct acpi_gpe_xrupt_info {
	struct acpi_gpe_xrupt_info *previous;
	struct acpi_gpe_xrupt_info *next;
	struct acpi_gpe_block_info *gpe_block_list_head;	/* List of GPE blocks for this xrupt */
	u32 interrupt_number;	/* System interrupt number */
};

struct acpi_gpe_walk_info {
	struct acpi_namespace_node *gpe_device;
	struct acpi_gpe_block_info *gpe_block;
	u16 count;
	acpi_owner_id owner_id;
	u8 execute_by_owner_id;
};

struct acpi_gpe_device_info {
	u32 index;
	u32 next_block_base_index;
	acpi_status status;
	struct acpi_namespace_node *gpe_device;
};

typedef acpi_status(*acpi_gpe_callback) (struct acpi_gpe_xrupt_info *gpe_xrupt_info,
		struct acpi_gpe_block_info *gpe_block, void *context);

/* Information about each particular fixed event */

struct acpi_fixed_event_handler {
	acpi_event_handler handler;	/* Address of handler. */
	void *context;		/* Context to be passed to handler */
};

struct acpi_fixed_event_info {
	u8 status_register_id;
	u8 enable_register_id;
	u16 status_bit_mask;
	u16 enable_bit_mask;
};

/* Information used during field processing */

struct acpi_field_info {
	u8 skip_field;
	u8 field_flag;
	u32 pkg_length;
};

/*****************************************************************************
 *
 * Generic "state" object for stacks
 *
 ****************************************************************************/

#define ACPI_CONTROL_NORMAL                  0xC0
#define ACPI_CONTROL_CONDITIONAL_EXECUTING   0xC1
#define ACPI_CONTROL_PREDICATE_EXECUTING     0xC2
#define ACPI_CONTROL_PREDICATE_FALSE         0xC3
#define ACPI_CONTROL_PREDICATE_TRUE          0xC4

#define ACPI_STATE_COMMON \
	void                            *next; \
	u8                              descriptor_type; /* To differentiate various internal objs */\
	u8                              flags; \
	u16                             value; \
	u16                             state;

	/* There are 2 bytes available here until the next natural alignment boundary */

struct acpi_common_state {
ACPI_STATE_COMMON};

/*
 * Update state - used to traverse complex objects such as packages
 */
struct acpi_update_state {
	ACPI_STATE_COMMON union acpi_operand_object *object;
};

/*
 * Pkg state - used to traverse nested package structures
 */
struct acpi_pkg_state {
	ACPI_STATE_COMMON u16 index;
	union acpi_operand_object *source_object;
	union acpi_operand_object *dest_object;
	struct acpi_walk_state *walk_state;
	void *this_target_obj;
	u32 num_packages;
};

/*
 * Control state - one per if/else and while constructs.
 * Allows nesting of these constructs
 */
struct acpi_control_state {
	ACPI_STATE_COMMON u16 opcode;
	union acpi_parse_object *predicate_op;
	u8 *aml_predicate_start;	/* Start of if/while predicate */
	u8 *package_end;	/* End of if/while block */
	u32 loop_count;		/* While() loop counter */
};

/*
 * Scope state - current scope during namespace lookups
 */
struct acpi_scope_state {
	ACPI_STATE_COMMON struct acpi_namespace_node *node;
};

struct acpi_pscope_state {
	ACPI_STATE_COMMON u32 arg_count;	/* Number of fixed arguments */
	union acpi_parse_object *op;	/* Current op being parsed */
	u8 *arg_end;		/* Current argument end */
	u8 *pkg_end;		/* Current package end */
	u32 arg_list;		/* Next argument to parse */
};

/*
 * Thread state - one per thread across multiple walk states.  Multiple walk
 * states are created when there are nested control methods executing.
 */
struct acpi_thread_state {
	ACPI_STATE_COMMON u8 current_sync_level;	/* Mutex Sync (nested acquire) level */
	struct acpi_walk_state *walk_state_list;	/* Head of list of walk_states for this thread */
	union acpi_operand_object *acquired_mutex_list;	/* List of all currently acquired mutexes */
	acpi_thread_id thread_id;	/* Running thread ID */
};

/*
 * Result values - used to accumulate the results of nested
 * AML arguments
 */
struct acpi_result_values {
	ACPI_STATE_COMMON
	    union acpi_operand_object *obj_desc[ACPI_RESULTS_FRAME_OBJ_NUM];
};

typedef
acpi_status(*acpi_parse_downwards) (struct acpi_walk_state * walk_state,
				    union acpi_parse_object ** out_op);

typedef acpi_status(*acpi_parse_upwards) (struct acpi_walk_state * walk_state);

/* Global handlers for AML Notifies */

struct acpi_global_notify_handler {
	acpi_notify_handler handler;
	void *context;
};

/*
 * Notify info - used to pass info to the deferred notify
 * handler/dispatcher.
 */
struct acpi_notify_info {
	ACPI_STATE_COMMON u8 handler_list_id;
	struct acpi_namespace_node *node;
	union acpi_operand_object *handler_list_head;
	struct acpi_global_notify_handler *global;
};

/* Generic state is union of structs above */

union acpi_generic_state {
	struct acpi_common_state common;
	struct acpi_control_state control;
	struct acpi_update_state update;
	struct acpi_scope_state scope;
	struct acpi_pscope_state parse_scope;
	struct acpi_pkg_state pkg;
	struct acpi_thread_state thread;
	struct acpi_result_values results;
	struct acpi_notify_info notify;
};

/*****************************************************************************
 *
 * Interpreter typedefs and structs
 *
 ****************************************************************************/

typedef acpi_status(*ACPI_EXECUTE_OP) (struct acpi_walk_state * walk_state);

/* Address Range info block */

struct acpi_address_range {
	struct acpi_address_range *next;
	struct acpi_namespace_node *region_node;
	acpi_physical_address start_address;
	acpi_physical_address end_address;
};

/*****************************************************************************
 *
 * Parser typedefs and structs
 *
 ****************************************************************************/

/*
 * AML opcode, name, and argument layout
 */
struct acpi_opcode_info {
#if defined(ACPI_DISASSEMBLER) || defined(ACPI_DEBUG_OUTPUT)
	char *name;		/* Opcode name (disassembler/debug only) */
#endif
	u32 parse_args;		/* Grammar/Parse time arguments */
	u32 runtime_args;	/* Interpret time arguments */
	u16 flags;		/* Misc flags */
	u8 object_type;		/* Corresponding internal object type */
	u8 class;		/* Opcode class */
	u8 type;		/* Opcode type */
};

union acpi_parse_value {
	u64 integer;		/* Integer constant (Up to 64 bits) */
	u32 size;		/* bytelist or field size */
	char *string;		/* NULL terminated string */
	u8 *buffer;		/* buffer or string */
	char *name;		/* NULL terminated string */
	union acpi_parse_object *arg;	/* arguments and contained ops */
};

#ifdef ACPI_DISASSEMBLER
#define ACPI_DISASM_ONLY_MEMBERS(a)     a;
#else
#define ACPI_DISASM_ONLY_MEMBERS(a)
#endif

#define ACPI_PARSE_COMMON \
	union acpi_parse_object         *parent;        /* Parent op */\
	u8                              descriptor_type; /* To differentiate various internal objs */\
	u8                              flags;          /* Type of Op */\
	u16                             aml_opcode;     /* AML opcode */\
	u32                             aml_offset;     /* Offset of declaration in AML */\
	union acpi_parse_object         *next;          /* Next op */\
	struct acpi_namespace_node      *node;          /* For use by interpreter */\
	union acpi_parse_value          value;          /* Value or args associated with the opcode */\
	u8                              arg_list_length; /* Number of elements in the arg list */\
	ACPI_DISASM_ONLY_MEMBERS (\
	u8                              disasm_flags;   /* Used during AML disassembly */\
	u8                              disasm_opcode;  /* Subtype used for disassembly */\
	char                            aml_op_name[16])	/* Op name (debug only) */

/* Flags for disasm_flags field above */

#define ACPI_DASM_BUFFER                0x00	/* Buffer is a simple data buffer */
#define ACPI_DASM_RESOURCE              0x01	/* Buffer is a Resource Descriptor */
#define ACPI_DASM_STRING                0x02	/* Buffer is a ASCII string */
#define ACPI_DASM_UNICODE               0x03	/* Buffer is a Unicode string */
#define ACPI_DASM_PLD_METHOD            0x04	/* Buffer is a _PLD method bit-packed buffer */
#define ACPI_DASM_EISAID                0x05	/* Integer is an EISAID */
#define ACPI_DASM_MATCHOP               0x06	/* Parent opcode is a Match() operator */
#define ACPI_DASM_LNOT_PREFIX           0x07	/* Start of a Lnot_equal (etc.) pair of opcodes */
#define ACPI_DASM_LNOT_SUFFIX           0x08	/* End  of a Lnot_equal (etc.) pair of opcodes */
#define ACPI_DASM_IGNORE                0x09	/* Not used at this time */

/*
 * Generic operation (for example:  If, While, Store)
 */
struct acpi_parse_obj_common {
ACPI_PARSE_COMMON};

/*
 * Extended Op for named ops (Scope, Method, etc.), deferred ops (Methods and op_regions),
 * and bytelists.
 */
struct acpi_parse_obj_named {
	ACPI_PARSE_COMMON u8 *path;
	u8 *data;		/* AML body or bytelist data */
	u32 length;		/* AML length */
	u32 name;		/* 4-byte name or zero if no name */
};

/* This version is used by the iASL compiler only */

#define ACPI_MAX_PARSEOP_NAME   20

struct acpi_parse_obj_asl {
	ACPI_PARSE_COMMON union acpi_parse_object *child;
	union acpi_parse_object *parent_method;
	char *filename;
	char *external_name;
	char *namepath;
	char name_seg[4];
	u32 extra_value;
	u32 column;
	u32 line_number;
	u32 logical_line_number;
	u32 logical_byte_offset;
	u32 end_line;
	u32 end_logical_line;
	u32 acpi_btype;
	u32 aml_length;
	u32 aml_subtree_length;
	u32 final_aml_length;
	u32 final_aml_offset;
	u32 compile_flags;
	u16 parse_opcode;
	u8 aml_opcode_length;
	u8 aml_pkg_len_bytes;
	u8 extra;
	char parse_op_name[ACPI_MAX_PARSEOP_NAME];
};

union acpi_parse_object {
	struct acpi_parse_obj_common common;
	struct acpi_parse_obj_named named;
	struct acpi_parse_obj_asl asl;
};

/*
 * Parse state - one state per parser invocation and each control
 * method.
 */
struct acpi_parse_state {
	u8 *aml_start;		/* First AML byte */
	u8 *aml;		/* Next AML byte */
	u8 *aml_end;		/* (last + 1) AML byte */
	u8 *pkg_start;		/* Current package begin */
	u8 *pkg_end;		/* Current package end */
	union acpi_parse_object *start_op;	/* Root of parse tree */
	struct acpi_namespace_node *start_node;
	union acpi_generic_state *scope;	/* Current scope */
	union acpi_parse_object *start_scope;
	u32 aml_size;
};

/* Parse object flags */

#define ACPI_PARSEOP_GENERIC            0x01
#define ACPI_PARSEOP_NAMED              0x02
#define ACPI_PARSEOP_DEFERRED           0x04
#define ACPI_PARSEOP_BYTELIST           0x08
#define ACPI_PARSEOP_IN_STACK           0x10
#define ACPI_PARSEOP_TARGET             0x20
#define ACPI_PARSEOP_IN_CACHE           0x80

/* Parse object disasm_flags */

#define ACPI_PARSEOP_IGNORE             0x01
#define ACPI_PARSEOP_PARAMLIST          0x02
#define ACPI_PARSEOP_EMPTY_TERMLIST     0x04
#define ACPI_PARSEOP_PREDEF_CHECKED     0x08
#define ACPI_PARSEOP_SPECIAL            0x10

/*****************************************************************************
 *
 * Hardware (ACPI registers) and PNP
 *
 ****************************************************************************/

struct acpi_bit_register_info {
	u8 parent_register;
	u8 bit_position;
	u16 access_bit_mask;
};

/*
 * Some ACPI registers have bits that must be ignored -- meaning that they
 * must be preserved.
 */
#define ACPI_PM1_STATUS_PRESERVED_BITS          0x0800	/* Bit 11 */

/* Write-only bits must be zeroed by software */

#define ACPI_PM1_CONTROL_WRITEONLY_BITS         0x2004	/* Bits 13, 2 */

/* For control registers, both ignored and reserved bits must be preserved */

/*
 * For PM1 control, the SCI enable bit (bit 0, SCI_EN) is defined by the
 * ACPI specification to be a "preserved" bit - "OSPM always preserves this
 * bit position", section 4.7.3.2.1. However, on some machines the OS must
 * write a one to this bit after resume for the machine to work properly.
 * To enable this, we no longer attempt to preserve this bit. No machines
 * are known to fail if the bit is not preserved. (May 2009)
 */
#define ACPI_PM1_CONTROL_IGNORED_BITS           0x0200	/* Bit 9 */
#define ACPI_PM1_CONTROL_RESERVED_BITS          0xC1F8	/* Bits 14-15, 3-8 */
#define ACPI_PM1_CONTROL_PRESERVED_BITS \
	       (ACPI_PM1_CONTROL_IGNORED_BITS | ACPI_PM1_CONTROL_RESERVED_BITS)

#define ACPI_PM2_CONTROL_PRESERVED_BITS         0xFFFFFFFE	/* All except bit 0 */

/*
 * Register IDs
 * These are the full ACPI registers
 */
#define ACPI_REGISTER_PM1_STATUS                0x01
#define ACPI_REGISTER_PM1_ENABLE                0x02
#define ACPI_REGISTER_PM1_CONTROL               0x03
#define ACPI_REGISTER_PM2_CONTROL               0x04
#define ACPI_REGISTER_PM_TIMER                  0x05
#define ACPI_REGISTER_PROCESSOR_BLOCK           0x06
#define ACPI_REGISTER_SMI_COMMAND_BLOCK         0x07

/* Masks used to access the bit_registers */

#define ACPI_BITMASK_TIMER_STATUS               0x0001
#define ACPI_BITMASK_BUS_MASTER_STATUS          0x0010
#define ACPI_BITMASK_GLOBAL_LOCK_STATUS         0x0020
#define ACPI_BITMASK_POWER_BUTTON_STATUS        0x0100
#define ACPI_BITMASK_SLEEP_BUTTON_STATUS        0x0200
#define ACPI_BITMASK_RT_CLOCK_STATUS            0x0400
#define ACPI_BITMASK_PCIEXP_WAKE_STATUS         0x4000	/* ACPI 3.0 */
#define ACPI_BITMASK_WAKE_STATUS                0x8000

#define ACPI_BITMASK_ALL_FIXED_STATUS           (\
	ACPI_BITMASK_TIMER_STATUS          | \
	ACPI_BITMASK_BUS_MASTER_STATUS     | \
	ACPI_BITMASK_GLOBAL_LOCK_STATUS    | \
	ACPI_BITMASK_POWER_BUTTON_STATUS   | \
	ACPI_BITMASK_SLEEP_BUTTON_STATUS   | \
	ACPI_BITMASK_RT_CLOCK_STATUS       | \
	ACPI_BITMASK_PCIEXP_WAKE_STATUS    | \
	ACPI_BITMASK_WAKE_STATUS)

#define ACPI_BITMASK_TIMER_ENABLE               0x0001
#define ACPI_BITMASK_GLOBAL_LOCK_ENABLE         0x0020
#define ACPI_BITMASK_POWER_BUTTON_ENABLE        0x0100
#define ACPI_BITMASK_SLEEP_BUTTON_ENABLE        0x0200
#define ACPI_BITMASK_RT_CLOCK_ENABLE            0x0400
#define ACPI_BITMASK_PCIEXP_WAKE_DISABLE        0x4000	/* ACPI 3.0 */

#define ACPI_BITMASK_SCI_ENABLE                 0x0001
#define ACPI_BITMASK_BUS_MASTER_RLD             0x0002
#define ACPI_BITMASK_GLOBAL_LOCK_RELEASE        0x0004
#define ACPI_BITMASK_SLEEP_TYPE                 0x1C00
#define ACPI_BITMASK_SLEEP_ENABLE               0x2000

#define ACPI_BITMASK_ARB_DISABLE                0x0001

/* Raw bit position of each bit_register */

#define ACPI_BITPOSITION_TIMER_STATUS           0x00
#define ACPI_BITPOSITION_BUS_MASTER_STATUS      0x04
#define ACPI_BITPOSITION_GLOBAL_LOCK_STATUS     0x05
#define ACPI_BITPOSITION_POWER_BUTTON_STATUS    0x08
#define ACPI_BITPOSITION_SLEEP_BUTTON_STATUS    0x09
#define ACPI_BITPOSITION_RT_CLOCK_STATUS        0x0A
#define ACPI_BITPOSITION_PCIEXP_WAKE_STATUS     0x0E	/* ACPI 3.0 */
#define ACPI_BITPOSITION_WAKE_STATUS            0x0F

#define ACPI_BITPOSITION_TIMER_ENABLE           0x00
#define ACPI_BITPOSITION_GLOBAL_LOCK_ENABLE     0x05
#define ACPI_BITPOSITION_POWER_BUTTON_ENABLE    0x08
#define ACPI_BITPOSITION_SLEEP_BUTTON_ENABLE    0x09
#define ACPI_BITPOSITION_RT_CLOCK_ENABLE        0x0A
#define ACPI_BITPOSITION_PCIEXP_WAKE_DISABLE    0x0E	/* ACPI 3.0 */

#define ACPI_BITPOSITION_SCI_ENABLE             0x00
#define ACPI_BITPOSITION_BUS_MASTER_RLD         0x01
#define ACPI_BITPOSITION_GLOBAL_LOCK_RELEASE    0x02
#define ACPI_BITPOSITION_SLEEP_TYPE             0x0A
#define ACPI_BITPOSITION_SLEEP_ENABLE           0x0D

#define ACPI_BITPOSITION_ARB_DISABLE            0x00

/* Structs and definitions for _OSI support and I/O port validation */

#define ACPI_OSI_WIN_2000               0x01
#define ACPI_OSI_WIN_XP                 0x02
#define ACPI_OSI_WIN_XP_SP1             0x03
#define ACPI_OSI_WINSRV_2003            0x04
#define ACPI_OSI_WIN_XP_SP2             0x05
#define ACPI_OSI_WINSRV_2003_SP1        0x06
#define ACPI_OSI_WIN_VISTA              0x07
#define ACPI_OSI_WINSRV_2008            0x08
#define ACPI_OSI_WIN_VISTA_SP1          0x09
#define ACPI_OSI_WIN_VISTA_SP2          0x0A
#define ACPI_OSI_WIN_7                  0x0B
#define ACPI_OSI_WIN_8                  0x0C

#define ACPI_ALWAYS_ILLEGAL             0x00

struct acpi_interface_info {
	char *name;
	struct acpi_interface_info *next;
	u8 flags;
	u8 value;
};

#define ACPI_OSI_INVALID                0x01
#define ACPI_OSI_DYNAMIC                0x02

struct acpi_port_info {
	char *name;
	u16 start;
	u16 end;
	u8 osi_dependency;
};

/*****************************************************************************
 *
 * Resource descriptors
 *
 ****************************************************************************/

/* resource_type values */

#define ACPI_ADDRESS_TYPE_MEMORY_RANGE          0
#define ACPI_ADDRESS_TYPE_IO_RANGE              1
#define ACPI_ADDRESS_TYPE_BUS_NUMBER_RANGE      2

/* Resource descriptor types and masks */

#define ACPI_RESOURCE_NAME_LARGE                0x80
#define ACPI_RESOURCE_NAME_SMALL                0x00

#define ACPI_RESOURCE_NAME_SMALL_MASK           0x78	/* Bits 6:3 contain the type */
#define ACPI_RESOURCE_NAME_SMALL_LENGTH_MASK    0x07	/* Bits 2:0 contain the length */
#define ACPI_RESOURCE_NAME_LARGE_MASK           0x7F	/* Bits 6:0 contain the type */

/*
 * Small resource descriptor "names" as defined by the ACPI specification.
 * Note: Bits 2:0 are used for the descriptor length
 */
#define ACPI_RESOURCE_NAME_IRQ                  0x20
#define ACPI_RESOURCE_NAME_DMA                  0x28
#define ACPI_RESOURCE_NAME_START_DEPENDENT      0x30
#define ACPI_RESOURCE_NAME_END_DEPENDENT        0x38
#define ACPI_RESOURCE_NAME_IO                   0x40
#define ACPI_RESOURCE_NAME_FIXED_IO             0x48
#define ACPI_RESOURCE_NAME_FIXED_DMA            0x50
#define ACPI_RESOURCE_NAME_RESERVED_S2          0x58
#define ACPI_RESOURCE_NAME_RESERVED_S3          0x60
#define ACPI_RESOURCE_NAME_RESERVED_S4          0x68
#define ACPI_RESOURCE_NAME_VENDOR_SMALL         0x70
#define ACPI_RESOURCE_NAME_END_TAG              0x78

/*
 * Large resource descriptor "names" as defined by the ACPI specification.
 * Note: includes the Large Descriptor bit in bit[7]
 */
#define ACPI_RESOURCE_NAME_MEMORY24             0x81
#define ACPI_RESOURCE_NAME_GENERIC_REGISTER     0x82
#define ACPI_RESOURCE_NAME_RESERVED_L1          0x83
#define ACPI_RESOURCE_NAME_VENDOR_LARGE         0x84
#define ACPI_RESOURCE_NAME_MEMORY32             0x85
#define ACPI_RESOURCE_NAME_FIXED_MEMORY32       0x86
#define ACPI_RESOURCE_NAME_ADDRESS32            0x87
#define ACPI_RESOURCE_NAME_ADDRESS16            0x88
#define ACPI_RESOURCE_NAME_EXTENDED_IRQ         0x89
#define ACPI_RESOURCE_NAME_ADDRESS64            0x8A
#define ACPI_RESOURCE_NAME_EXTENDED_ADDRESS64   0x8B
#define ACPI_RESOURCE_NAME_GPIO                 0x8C
#define ACPI_RESOURCE_NAME_SERIAL_BUS           0x8E
#define ACPI_RESOURCE_NAME_LARGE_MAX            0x8E

/*****************************************************************************
 *
 * Miscellaneous
 *
 ****************************************************************************/

#define ACPI_ASCII_ZERO                 0x30

/*****************************************************************************
 *
 * Debugger
 *
 ****************************************************************************/

struct acpi_db_method_info {
	acpi_handle method;
	acpi_handle main_thread_gate;
	acpi_handle thread_complete_gate;
	acpi_handle info_gate;
	acpi_thread_id *threads;
	u32 num_threads;
	u32 num_created;
	u32 num_completed;

	char *name;
	u32 flags;
	u32 num_loops;
	char pathname[128];
	char **args;
	acpi_object_type *types;

	/*
	 * Arguments to be passed to method for the command
	 * Threads -
	 *   the Number of threads, ID of current thread and
	 *   Index of current thread inside all them created.
	 */
	char init_args;
	char *arguments[4];
	char num_threads_str[11];
	char id_of_thread_str[11];
	char index_of_thread_str[11];
};

struct acpi_integrity_info {
	u32 nodes;
	u32 objects;
};

#define ACPI_DB_REDIRECTABLE_OUTPUT     0x01
#define ACPI_DB_CONSOLE_OUTPUT          0x02
#define ACPI_DB_DUPLICATE_OUTPUT        0x03

/*****************************************************************************
 *
 * Debug
 *
 ****************************************************************************/

/* Entry for a memory allocation (debug only) */

#define ACPI_MEM_MALLOC                 0
#define ACPI_MEM_CALLOC                 1
#define ACPI_MAX_MODULE_NAME            16

#define ACPI_COMMON_DEBUG_MEM_HEADER \
	struct acpi_debug_mem_block     *previous; \
	struct acpi_debug_mem_block     *next; \
	u32                             size; \
	u32                             component; \
	u32                             line; \
	char                            module[ACPI_MAX_MODULE_NAME]; \
	u8                              alloc_type;

struct acpi_debug_mem_header {
ACPI_COMMON_DEBUG_MEM_HEADER};

struct acpi_debug_mem_block {
	ACPI_COMMON_DEBUG_MEM_HEADER u64 user_space;
};

#define ACPI_MEM_LIST_GLOBAL            0
#define ACPI_MEM_LIST_NSNODE            1
#define ACPI_MEM_LIST_MAX               1
#define ACPI_NUM_MEM_LISTS              2

/*****************************************************************************
 *
 * Info/help support
 *
 ****************************************************************************/

struct ah_predefined_name {
	char *name;
	char *description;
#ifndef ACPI_ASL_COMPILER
	char *action;
#endif
};

#endif				/* __ACLOCAL_H__ */
