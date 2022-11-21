/* SPDX-License-Identifier: BSD-3-Clause OR GPL-2.0 */
/******************************************************************************
 *
 * Name: acobject.h - Definition of union acpi_operand_object  (Internal object only)
 *
 * Copyright (C) 2000 - 2021, Intel Corp.
 *
 *****************************************************************************/

#ifndef _ACOBJECT_H
#define _ACOBJECT_H

/* acpisrc:struct_defs -- for acpisrc conversion */

/*
 * The union acpi_operand_object is used to pass AML operands from the dispatcher
 * to the interpreter, and to keep track of the various handlers such as
 * address space handlers and notify handlers. The object is a constant
 * size in order to allow it to be cached and reused.
 *
 * Note: The object is optimized to be aligned and will not work if it is
 * byte-packed.
 */
#if ACPI_MACHINE_WIDTH == 64
#pragma pack(8)
#else
#pragma pack(4)
#endif

/*******************************************************************************
 *
 * Common Descriptors
 *
 ******************************************************************************/

/*
 * Common area for all objects.
 *
 * descriptor_type is used to differentiate between internal descriptors, and
 * must be in the same place across all descriptors
 *
 * Note: The descriptor_type and Type fields must appear in the identical
 * position in both the struct acpi_namespace_node and union acpi_operand_object
 * structures.
 */
#define ACPI_OBJECT_COMMON_HEADER \
	union acpi_operand_object       *next_object;       /* Objects linked to parent NS node */\
	u8                              descriptor_type;    /* To differentiate various internal objs */\
	u8                              type;               /* acpi_object_type */\
	u16                             reference_count;    /* For object deletion management */\
	u8                              flags;
	/*
	 * Note: There are 3 bytes available here before the
	 * next natural alignment boundary (for both 32/64 cases)
	 */

/* Values for Flag byte above */

#define AOPOBJ_AML_CONSTANT         0x01	/* Integer is an AML constant */
#define AOPOBJ_STATIC_POINTER       0x02	/* Data is part of an ACPI table, don't delete */
#define AOPOBJ_DATA_VALID           0x04	/* Object is initialized and data is valid */
#define AOPOBJ_OBJECT_INITIALIZED   0x08	/* Region is initialized */
#define AOPOBJ_REG_CONNECTED        0x10	/* _REG was run */
#define AOPOBJ_SETUP_COMPLETE       0x20	/* Region setup is complete */
#define AOPOBJ_INVALID              0x40	/* Host OS won't allow a Region address */

/******************************************************************************
 *
 * Basic data types
 *
 *****************************************************************************/

struct acpi_object_common {
ACPI_OBJECT_COMMON_HEADER};

struct acpi_object_integer {
	ACPI_OBJECT_COMMON_HEADER u8 fill[3];	/* Prevent warning on some compilers */
	u64 value;
};

/*
 * Note: The String and Buffer object must be identical through the
 * pointer and length elements. There is code that depends on this.
 *
 * Fields common to both Strings and Buffers
 */
#define ACPI_COMMON_BUFFER_INFO(_type) \
	_type                           *pointer; \
	u32                             length;

/* Null terminated, ASCII characters only */

struct acpi_object_string {
	ACPI_OBJECT_COMMON_HEADER ACPI_COMMON_BUFFER_INFO(char)	/* String in AML stream or allocated string */
};

struct acpi_object_buffer {
	ACPI_OBJECT_COMMON_HEADER ACPI_COMMON_BUFFER_INFO(u8)	/* Buffer in AML stream or allocated buffer */
	u32 aml_length;
	u8 *aml_start;
	struct acpi_namespace_node *node;	/* Link back to parent node */
};

struct acpi_object_package {
	ACPI_OBJECT_COMMON_HEADER struct acpi_namespace_node *node;	/* Link back to parent node */
	union acpi_operand_object **elements;	/* Array of pointers to acpi_objects */
	u8 *aml_start;
	u32 aml_length;
	u32 count;		/* # of elements in package */
};

/******************************************************************************
 *
 * Complex data types
 *
 *****************************************************************************/

struct acpi_object_event {
	ACPI_OBJECT_COMMON_HEADER acpi_semaphore os_semaphore;	/* Actual OS synchronization object */
};

struct acpi_object_mutex {
	ACPI_OBJECT_COMMON_HEADER u8 sync_level;	/* 0-15, specified in Mutex() call */
	u16 acquisition_depth;	/* Allow multiple Acquires, same thread */
	acpi_mutex os_mutex;	/* Actual OS synchronization object */
	acpi_thread_id thread_id;	/* Current owner of the mutex */
	struct acpi_thread_state *owner_thread;	/* Current owner of the mutex */
	union acpi_operand_object *prev;	/* Link for list of acquired mutexes */
	union acpi_operand_object *next;	/* Link for list of acquired mutexes */
	struct acpi_namespace_node *node;	/* Containing namespace node */
	u8 original_sync_level;	/* Owner's original sync level (0-15) */
};

struct acpi_object_region {
	ACPI_OBJECT_COMMON_HEADER u8 space_id;
	struct acpi_namespace_node *node;	/* Containing namespace node */
	union acpi_operand_object *handler;	/* Handler for region access */
	union acpi_operand_object *next;
	acpi_physical_address address;
	u32 length;
	void *pointer;		/* Only for data table regions */
};

struct acpi_object_method {
	ACPI_OBJECT_COMMON_HEADER u8 info_flags;
	u8 param_count;
	u8 sync_level;
	union acpi_operand_object *mutex;
	union acpi_operand_object *node;
	u8 *aml_start;
	union {
		acpi_internal_method implementation;
		union acpi_operand_object *handler;
	} dispatch;

	u32 aml_length;
	acpi_owner_id owner_id;
	u8 thread_count;
};

/* Flags for info_flags field above */

#define ACPI_METHOD_MODULE_LEVEL        0x01	/* Method is actually module-level code */
#define ACPI_METHOD_INTERNAL_ONLY       0x02	/* Method is implemented internally (_OSI) */
#define ACPI_METHOD_SERIALIZED          0x04	/* Method is serialized */
#define ACPI_METHOD_SERIALIZED_PENDING  0x08	/* Method is to be marked serialized */
#define ACPI_METHOD_IGNORE_SYNC_LEVEL   0x10	/* Method was auto-serialized at table load time */
#define ACPI_METHOD_MODIFIED_NAMESPACE  0x20	/* Method modified the namespace */

/******************************************************************************
 *
 * Objects that can be notified. All share a common notify_info area.
 *
 *****************************************************************************/

/*
 * Common fields for objects that support ASL notifications
 */
#define ACPI_COMMON_NOTIFY_INFO \
	union acpi_operand_object       *notify_list[2];    /* Handlers for system/device notifies */\
	union acpi_operand_object       *handler;	/* Handler for Address space */

/* COMMON NOTIFY for POWER, PROCESSOR, DEVICE, and THERMAL */

struct acpi_object_notify_common {
ACPI_OBJECT_COMMON_HEADER ACPI_COMMON_NOTIFY_INFO};

struct acpi_object_device {
	ACPI_OBJECT_COMMON_HEADER
	    ACPI_COMMON_NOTIFY_INFO struct acpi_gpe_block_info *gpe_block;
};

struct acpi_object_power_resource {
	ACPI_OBJECT_COMMON_HEADER ACPI_COMMON_NOTIFY_INFO u32 system_level;
	u32 resource_order;
};

struct acpi_object_processor {
	ACPI_OBJECT_COMMON_HEADER
	    /* The next two fields take advantage of the 3-byte space before NOTIFY_INFO */
	u8 proc_id;
	u8 length;
	ACPI_COMMON_NOTIFY_INFO acpi_io_address address;
};

struct acpi_object_thermal_zone {
ACPI_OBJECT_COMMON_HEADER ACPI_COMMON_NOTIFY_INFO};

/******************************************************************************
 *
 * Fields. All share a common header/info field.
 *
 *****************************************************************************/

/*
 * Common bitfield for the field objects
 * "Field Datum"  -- a datum from the actual field object
 * "Buffer Datum" -- a datum from a user buffer, read from or to be written to the field
 */
#define ACPI_COMMON_FIELD_INFO \
	u8                              field_flags;        /* Access, update, and lock bits */\
	u8                              attribute;          /* From access_as keyword */\
	u8                              access_byte_width;  /* Read/Write size in bytes */\
	struct acpi_namespace_node      *node;              /* Link back to parent node */\
	u32                             bit_length;         /* Length of field in bits */\
	u32                             base_byte_offset;   /* Byte offset within containing object */\
	u32                             value;              /* Value to store into the Bank or Index register */\
	u8                              start_field_bit_offset;/* Bit offset within first field datum (0-63) */\
	u8                              access_length;	/* For serial regions/fields */


/* COMMON FIELD (for BUFFER, REGION, BANK, and INDEX fields) */

struct acpi_object_field_common {
	ACPI_OBJECT_COMMON_HEADER ACPI_COMMON_FIELD_INFO union acpi_operand_object *region_obj;	/* Parent Operation Region object (REGION/BANK fields only) */
};

struct acpi_object_region_field {
	ACPI_OBJECT_COMMON_HEADER ACPI_COMMON_FIELD_INFO u16 resource_length;
	union acpi_operand_object *region_obj;	/* Containing op_region object */
	u8 *resource_buffer;	/* resource_template for serial regions/fields */
	u16 pin_number_index;	/* Index relative to previous Connection/Template */
	u8 *internal_pcc_buffer;	/* Internal buffer for fields associated with PCC */
};

struct acpi_object_bank_field {
	ACPI_OBJECT_COMMON_HEADER ACPI_COMMON_FIELD_INFO union acpi_operand_object *region_obj;	/* Containing op_region object */
	union acpi_operand_object *bank_obj;	/* bank_select Register object */
};

struct acpi_object_index_field {
	ACPI_OBJECT_COMMON_HEADER ACPI_COMMON_FIELD_INFO
	    /*
	     * No "RegionObj" pointer needed since the Index and Data registers
	     * are each field definitions unto themselves.
	     */
	union acpi_operand_object *index_obj;	/* Index register */
	union acpi_operand_object *data_obj;	/* Data register */
};

/* The buffer_field is different in that it is part of a Buffer, not an op_region */

struct acpi_object_buffer_field {
	ACPI_OBJECT_COMMON_HEADER ACPI_COMMON_FIELD_INFO u8 is_create_field;	/* Special case for objects created by create_field() */
	union acpi_operand_object *buffer_obj;	/* Containing Buffer object */
};

/******************************************************************************
 *
 * Objects for handlers
 *
 *****************************************************************************/

struct acpi_object_notify_handler {
	ACPI_OBJECT_COMMON_HEADER struct acpi_namespace_node *node;	/* Parent device */
	u32 handler_type;	/* Type: Device/System/Both */
	acpi_notify_handler handler;	/* Handler address */
	void *context;
	union acpi_operand_object *next[2];	/* Device and System handler lists */
};

struct acpi_object_addr_handler {
	ACPI_OBJECT_COMMON_HEADER u8 space_id;
	u8 handler_flags;
	acpi_adr_space_handler handler;
	struct acpi_namespace_node *node;	/* Parent device */
	void *context;
	acpi_mutex context_mutex;
	acpi_adr_space_setup setup;
	union acpi_operand_object *region_list;	/* Regions using this handler */
	union acpi_operand_object *next;
};

/* Flags for address handler (handler_flags) */

#define ACPI_ADDR_HANDLER_DEFAULT_INSTALLED  0x01

/******************************************************************************
 *
 * Special internal objects
 *
 *****************************************************************************/

/*
 * The Reference object is used for these opcodes:
 * Arg[0-6], Local[0-7], index_op, name_op, ref_of_op, load_op, load_table_op, debug_op
 * The Reference.Class differentiates these types.
 */
struct acpi_object_reference {
	ACPI_OBJECT_COMMON_HEADER u8 class;	/* Reference Class */
	u8 target_type;		/* Used for Index Op */
	u8 resolved;		/* Reference has been resolved to a value */
	void *object;		/* name_op=>HANDLE to obj, index_op=>union acpi_operand_object */
	struct acpi_namespace_node *node;	/* ref_of or Namepath */
	union acpi_operand_object **where;	/* Target of Index */
	u8 *index_pointer;	/* Used for Buffers and Strings */
	u8 *aml;		/* Used for deferred resolution of the ref */
	u32 value;		/* Used for Local/Arg/Index/ddb_handle */
};

/* Values for Reference.Class above */

typedef enum {
	ACPI_REFCLASS_LOCAL = 0,	/* Method local */
	ACPI_REFCLASS_ARG = 1,	/* Method argument */
	ACPI_REFCLASS_REFOF = 2,	/* Result of ref_of() TBD: Split to Ref/Node and Ref/operand_obj? */
	ACPI_REFCLASS_INDEX = 3,	/* Result of Index() */
	ACPI_REFCLASS_TABLE = 4,	/* ddb_handle - Load(), load_table() */
	ACPI_REFCLASS_NAME = 5,	/* Reference to a named object */
	ACPI_REFCLASS_DEBUG = 6,	/* Debug object */

	ACPI_REFCLASS_MAX = 6
} ACPI_REFERENCE_CLASSES;

/*
 * Extra object is used as additional storage for types that
 * have AML code in their declarations (term_args) that must be
 * evaluated at run time.
 *
 * Currently: Region and field_unit types
 */
struct acpi_object_extra {
	ACPI_OBJECT_COMMON_HEADER struct acpi_namespace_node *method_REG;	/* _REG method for this region (if any) */
	struct acpi_namespace_node *scope_node;
	void *region_context;	/* Region-specific data */
	u8 *aml_start;
	u32 aml_length;
};

/* Additional data that can be attached to namespace nodes */

struct acpi_object_data {
	ACPI_OBJECT_COMMON_HEADER acpi_object_handler handler;
	void *pointer;
};

/* Structure used when objects are cached for reuse */

struct acpi_object_cache_list {
	ACPI_OBJECT_COMMON_HEADER union acpi_operand_object *next;	/* Link for object cache and internal lists */
};

/******************************************************************************
 *
 * union acpi_operand_object descriptor - a giant union of all of the above
 *
 *****************************************************************************/

union acpi_operand_object {
	struct acpi_object_common common;
	struct acpi_object_integer integer;
	struct acpi_object_string string;
	struct acpi_object_buffer buffer;
	struct acpi_object_package package;
	struct acpi_object_event event;
	struct acpi_object_method method;
	struct acpi_object_mutex mutex;
	struct acpi_object_region region;
	struct acpi_object_notify_common common_notify;
	struct acpi_object_device device;
	struct acpi_object_power_resource power_resource;
	struct acpi_object_processor processor;
	struct acpi_object_thermal_zone thermal_zone;
	struct acpi_object_field_common common_field;
	struct acpi_object_region_field field;
	struct acpi_object_buffer_field buffer_field;
	struct acpi_object_bank_field bank_field;
	struct acpi_object_index_field index_field;
	struct acpi_object_notify_handler notify;
	struct acpi_object_addr_handler address_space;
	struct acpi_object_reference reference;
	struct acpi_object_extra extra;
	struct acpi_object_data data;
	struct acpi_object_cache_list cache;

	/*
	 * Add namespace node to union in order to simplify code that accepts both
	 * ACPI_OPERAND_OBJECTs and ACPI_NAMESPACE_NODEs. The structures share
	 * a common descriptor_type field in order to differentiate them.
	 */
	struct acpi_namespace_node node;
};

/******************************************************************************
 *
 * union acpi_descriptor - objects that share a common descriptor identifier
 *
 *****************************************************************************/

/* Object descriptor types */

#define ACPI_DESC_TYPE_CACHED           0x01	/* Used only when object is cached */
#define ACPI_DESC_TYPE_STATE            0x02
#define ACPI_DESC_TYPE_STATE_UPDATE     0x03
#define ACPI_DESC_TYPE_STATE_PACKAGE    0x04
#define ACPI_DESC_TYPE_STATE_CONTROL    0x05
#define ACPI_DESC_TYPE_STATE_RPSCOPE    0x06
#define ACPI_DESC_TYPE_STATE_PSCOPE     0x07
#define ACPI_DESC_TYPE_STATE_WSCOPE     0x08
#define ACPI_DESC_TYPE_STATE_RESULT     0x09
#define ACPI_DESC_TYPE_STATE_NOTIFY     0x0A
#define ACPI_DESC_TYPE_STATE_THREAD     0x0B
#define ACPI_DESC_TYPE_WALK             0x0C
#define ACPI_DESC_TYPE_PARSER           0x0D
#define ACPI_DESC_TYPE_OPERAND          0x0E
#define ACPI_DESC_TYPE_NAMED            0x0F
#define ACPI_DESC_TYPE_MAX              0x0F

struct acpi_common_descriptor {
	void *common_pointer;
	u8 descriptor_type;	/* To differentiate various internal objs */
};

union acpi_descriptor {
	struct acpi_common_descriptor common;
	union acpi_operand_object object;
	struct acpi_namespace_node node;
	union acpi_parse_object op;
};

#pragma pack()

#endif				/* _ACOBJECT_H */
