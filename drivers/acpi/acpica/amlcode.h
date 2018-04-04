/******************************************************************************
 *
 * Name: amlcode.h - Definitions for AML, as included in "definition blocks"
 *                   Declarations and definitions contained herein are derived
 *                   directly from the ACPI specification.
 *
 *****************************************************************************/

/*
 * Copyright (C) 2000 - 2018, Intel Corp.
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

#ifndef __AMLCODE_H__
#define __AMLCODE_H__

/* primary opcodes */

#define AML_ZERO_OP                 (u16) 0x00
#define AML_ONE_OP                  (u16) 0x01
#define AML_ALIAS_OP                (u16) 0x06
#define AML_NAME_OP                 (u16) 0x08
#define AML_BYTE_OP                 (u16) 0x0a
#define AML_WORD_OP                 (u16) 0x0b
#define AML_DWORD_OP                (u16) 0x0c
#define AML_STRING_OP               (u16) 0x0d
#define AML_QWORD_OP                (u16) 0x0e	/* ACPI 2.0 */
#define AML_SCOPE_OP                (u16) 0x10
#define AML_BUFFER_OP               (u16) 0x11
#define AML_PACKAGE_OP              (u16) 0x12
#define AML_VARIABLE_PACKAGE_OP     (u16) 0x13	/* ACPI 2.0 */
#define AML_METHOD_OP               (u16) 0x14
#define AML_EXTERNAL_OP             (u16) 0x15	/* ACPI 6.0 */
#define AML_DUAL_NAME_PREFIX        (u16) 0x2e
#define AML_MULTI_NAME_PREFIX       (u16) 0x2f
#define AML_EXTENDED_PREFIX         (u16) 0x5b
#define AML_ROOT_PREFIX             (u16) 0x5c
#define AML_PARENT_PREFIX           (u16) 0x5e
#define AML_FIRST_LOCAL_OP          (u16) 0x60	/* Used for Local op # calculations */
#define AML_LOCAL0                  (u16) 0x60
#define AML_LOCAL1                  (u16) 0x61
#define AML_LOCAL2                  (u16) 0x62
#define AML_LOCAL3                  (u16) 0x63
#define AML_LOCAL4                  (u16) 0x64
#define AML_LOCAL5                  (u16) 0x65
#define AML_LOCAL6                  (u16) 0x66
#define AML_LOCAL7                  (u16) 0x67
#define AML_FIRST_ARG_OP            (u16) 0x68	/* Used for Arg op # calculations */
#define AML_ARG0                    (u16) 0x68
#define AML_ARG1                    (u16) 0x69
#define AML_ARG2                    (u16) 0x6a
#define AML_ARG3                    (u16) 0x6b
#define AML_ARG4                    (u16) 0x6c
#define AML_ARG5                    (u16) 0x6d
#define AML_ARG6                    (u16) 0x6e
#define AML_STORE_OP                (u16) 0x70
#define AML_REF_OF_OP               (u16) 0x71
#define AML_ADD_OP                  (u16) 0x72
#define AML_CONCATENATE_OP          (u16) 0x73
#define AML_SUBTRACT_OP             (u16) 0x74
#define AML_INCREMENT_OP            (u16) 0x75
#define AML_DECREMENT_OP            (u16) 0x76
#define AML_MULTIPLY_OP             (u16) 0x77
#define AML_DIVIDE_OP               (u16) 0x78
#define AML_SHIFT_LEFT_OP           (u16) 0x79
#define AML_SHIFT_RIGHT_OP          (u16) 0x7a
#define AML_BIT_AND_OP              (u16) 0x7b
#define AML_BIT_NAND_OP             (u16) 0x7c
#define AML_BIT_OR_OP               (u16) 0x7d
#define AML_BIT_NOR_OP              (u16) 0x7e
#define AML_BIT_XOR_OP              (u16) 0x7f
#define AML_BIT_NOT_OP              (u16) 0x80
#define AML_FIND_SET_LEFT_BIT_OP    (u16) 0x81
#define AML_FIND_SET_RIGHT_BIT_OP   (u16) 0x82
#define AML_DEREF_OF_OP             (u16) 0x83
#define AML_CONCATENATE_TEMPLATE_OP (u16) 0x84	/* ACPI 2.0 */
#define AML_MOD_OP                  (u16) 0x85	/* ACPI 2.0 */
#define AML_NOTIFY_OP               (u16) 0x86
#define AML_SIZE_OF_OP              (u16) 0x87
#define AML_INDEX_OP                (u16) 0x88
#define AML_MATCH_OP                (u16) 0x89
#define AML_CREATE_DWORD_FIELD_OP   (u16) 0x8a
#define AML_CREATE_WORD_FIELD_OP    (u16) 0x8b
#define AML_CREATE_BYTE_FIELD_OP    (u16) 0x8c
#define AML_CREATE_BIT_FIELD_OP     (u16) 0x8d
#define AML_OBJECT_TYPE_OP          (u16) 0x8e
#define AML_CREATE_QWORD_FIELD_OP   (u16) 0x8f	/* ACPI 2.0 */
#define AML_LOGICAL_AND_OP          (u16) 0x90
#define AML_LOGICAL_OR_OP           (u16) 0x91
#define AML_LOGICAL_NOT_OP          (u16) 0x92
#define AML_LOGICAL_EQUAL_OP        (u16) 0x93
#define AML_LOGICAL_GREATER_OP      (u16) 0x94
#define AML_LOGICAL_LESS_OP         (u16) 0x95
#define AML_TO_BUFFER_OP            (u16) 0x96	/* ACPI 2.0 */
#define AML_TO_DECIMAL_STRING_OP    (u16) 0x97	/* ACPI 2.0 */
#define AML_TO_HEX_STRING_OP        (u16) 0x98	/* ACPI 2.0 */
#define AML_TO_INTEGER_OP           (u16) 0x99	/* ACPI 2.0 */
#define AML_TO_STRING_OP            (u16) 0x9c	/* ACPI 2.0 */
#define AML_COPY_OBJECT_OP          (u16) 0x9d	/* ACPI 2.0 */
#define AML_MID_OP                  (u16) 0x9e	/* ACPI 2.0 */
#define AML_CONTINUE_OP             (u16) 0x9f	/* ACPI 2.0 */
#define AML_IF_OP                   (u16) 0xa0
#define AML_ELSE_OP                 (u16) 0xa1
#define AML_WHILE_OP                (u16) 0xa2
#define AML_NOOP_OP                 (u16) 0xa3
#define AML_RETURN_OP               (u16) 0xa4
#define AML_BREAK_OP                (u16) 0xa5
#define AML_COMMENT_OP              (u16) 0xa9
#define AML_BREAKPOINT_OP          (u16) 0xcc
#define AML_ONES_OP                 (u16) 0xff

/*
 * Combination opcodes (actually two one-byte opcodes)
 * Used by the disassembler and iASL compiler
 */
#define AML_LOGICAL_GREATER_EQUAL_OP (u16) 0x9295	/* LNot (LLess) */
#define AML_LOGICAL_LESS_EQUAL_OP    (u16) 0x9294	/* LNot (LGreater) */
#define AML_LOGICAL_NOT_EQUAL_OP     (u16) 0x9293	/* LNot (LEqual) */

/* Prefixed (2-byte) opcodes (with AML_EXTENDED_PREFIX) */

#define AML_EXTENDED_OPCODE         (u16) 0x5b00	/* Prefix for 2-byte opcodes */

#define AML_MUTEX_OP                (u16) 0x5b01
#define AML_EVENT_OP                (u16) 0x5b02
#define AML_SHIFT_RIGHT_BIT_OP      (u16) 0x5b10	/* Obsolete, not in ACPI spec */
#define AML_SHIFT_LEFT_BIT_OP       (u16) 0x5b11	/* Obsolete, not in ACPI spec */
#define AML_CONDITIONAL_REF_OF_OP   (u16) 0x5b12
#define AML_CREATE_FIELD_OP         (u16) 0x5b13
#define AML_LOAD_TABLE_OP           (u16) 0x5b1f	/* ACPI 2.0 */
#define AML_LOAD_OP                 (u16) 0x5b20
#define AML_STALL_OP                (u16) 0x5b21
#define AML_SLEEP_OP                (u16) 0x5b22
#define AML_ACQUIRE_OP              (u16) 0x5b23
#define AML_SIGNAL_OP               (u16) 0x5b24
#define AML_WAIT_OP                 (u16) 0x5b25
#define AML_RESET_OP                (u16) 0x5b26
#define AML_RELEASE_OP              (u16) 0x5b27
#define AML_FROM_BCD_OP             (u16) 0x5b28
#define AML_TO_BCD_OP               (u16) 0x5b29
#define AML_UNLOAD_OP               (u16) 0x5b2a
#define AML_REVISION_OP             (u16) 0x5b30
#define AML_DEBUG_OP                (u16) 0x5b31
#define AML_FATAL_OP                (u16) 0x5b32
#define AML_TIMER_OP                (u16) 0x5b33	/* ACPI 3.0 */
#define AML_REGION_OP               (u16) 0x5b80
#define AML_FIELD_OP                (u16) 0x5b81
#define AML_DEVICE_OP               (u16) 0x5b82
#define AML_PROCESSOR_OP            (u16) 0x5b83
#define AML_POWER_RESOURCE_OP       (u16) 0x5b84
#define AML_THERMAL_ZONE_OP         (u16) 0x5b85
#define AML_INDEX_FIELD_OP          (u16) 0x5b86
#define AML_BANK_FIELD_OP           (u16) 0x5b87
#define AML_DATA_REGION_OP          (u16) 0x5b88	/* ACPI 2.0 */

/*
 * Opcodes for "Field" operators
 */
#define AML_FIELD_OFFSET_OP         (u8) 0x00
#define AML_FIELD_ACCESS_OP         (u8) 0x01
#define AML_FIELD_CONNECTION_OP     (u8) 0x02	/* ACPI 5.0 */
#define AML_FIELD_EXT_ACCESS_OP     (u8) 0x03	/* ACPI 5.0 */

/*
 * Internal opcodes
 * Use only "Unknown" AML opcodes, don't attempt to use
 * any valid ACPI ASCII values (A-Z, 0-9, '-')
 */
#define AML_INT_NAMEPATH_OP         (u16) 0x002d
#define AML_INT_NAMEDFIELD_OP       (u16) 0x0030
#define AML_INT_RESERVEDFIELD_OP    (u16) 0x0031
#define AML_INT_ACCESSFIELD_OP      (u16) 0x0032
#define AML_INT_BYTELIST_OP         (u16) 0x0033
#define AML_INT_METHODCALL_OP       (u16) 0x0035
#define AML_INT_RETURN_VALUE_OP     (u16) 0x0036
#define AML_INT_EVAL_SUBTREE_OP     (u16) 0x0037
#define AML_INT_CONNECTION_OP       (u16) 0x0038
#define AML_INT_EXTACCESSFIELD_OP   (u16) 0x0039

#define ARG_NONE                    0x0

/*
 * Argument types for the AML Parser
 * Each field in the arg_types u32 is 5 bits, allowing for a maximum of 6 arguments.
 * There can be up to 31 unique argument types
 * Zero is reserved as end-of-list indicator
 */
#define ARGP_BYTEDATA               0x01
#define ARGP_BYTELIST               0x02
#define ARGP_CHARLIST               0x03
#define ARGP_DATAOBJ                0x04
#define ARGP_DATAOBJLIST            0x05
#define ARGP_DWORDDATA              0x06
#define ARGP_FIELDLIST              0x07
#define ARGP_NAME                   0x08
#define ARGP_NAMESTRING             0x09
#define ARGP_OBJLIST                0x0A
#define ARGP_PKGLENGTH              0x0B
#define ARGP_SUPERNAME              0x0C
#define ARGP_TARGET                 0x0D
#define ARGP_TERMARG                0x0E
#define ARGP_TERMLIST               0x0F
#define ARGP_WORDDATA               0x10
#define ARGP_QWORDDATA              0x11
#define ARGP_SIMPLENAME             0x12	/* name_string | local_term | arg_term */
#define ARGP_NAME_OR_REF            0x13	/* For object_type only */
#define ARGP_MAX                    0x13
#define ARGP_COMMENT                0x14

/*
 * Resolved argument types for the AML Interpreter
 * Each field in the arg_types u32 is 5 bits, allowing for a maximum of 6 arguments.
 * There can be up to 31 unique argument types (0 is end-of-arg-list indicator)
 *
 * Note1: These values are completely independent from the ACPI_TYPEs
 *        i.e., ARGI_INTEGER != ACPI_TYPE_INTEGER
 *
 * Note2: If and when 5 bits becomes insufficient, it would probably be best
 * to convert to a 6-byte array of argument types, allowing 8 bits per argument.
 */

/* Single, simple types */

#define ARGI_ANYTYPE                0x01	/* Don't care */
#define ARGI_PACKAGE                0x02
#define ARGI_EVENT                  0x03
#define ARGI_MUTEX                  0x04
#define ARGI_DDBHANDLE              0x05

/* Interchangeable types (via implicit conversion) */

#define ARGI_INTEGER                0x06
#define ARGI_STRING                 0x07
#define ARGI_BUFFER                 0x08
#define ARGI_BUFFER_OR_STRING       0x09	/* Used by MID op only */
#define ARGI_COMPUTEDATA            0x0A	/* Buffer, String, or Integer */

/* Reference objects */

#define ARGI_INTEGER_REF            0x0B
#define ARGI_OBJECT_REF             0x0C
#define ARGI_DEVICE_REF             0x0D
#define ARGI_REFERENCE              0x0E
#define ARGI_TARGETREF              0x0F	/* Target, subject to implicit conversion */
#define ARGI_SIMPLE_TARGET          0x10	/* Name, Local, Arg -- no implicit conversion */
#define ARGI_STORE_TARGET           0x11	/* Target for store is TARGETREF + package objects */
/*
 * #define ARGI_FIXED_TARGET           0x10     Target, no implicit conversion
 *
 * Removed 10/2016. ARGI_FIXED_TARGET was used for these operators:
 *      from_BCD
 *      to_BCD
 *      to_decimal_string
 *      to_hex_string
 *      to_integer
 *      to_buffer
 * The purpose of this type was to disable "implicit result conversion",
 * but this was incorrect per the ACPI spec and other ACPI implementations.
 * These operators now have the target operand defined as a normal
 * ARGI_TARGETREF.
 */

/* Multiple/complex types */

#define ARGI_DATAOBJECT             0x13	/* Buffer, String, package or reference to a node - Used only by size_of operator */
#define ARGI_COMPLEXOBJ             0x14	/* Buffer, String, or package (Used by INDEX op only) */
#define ARGI_REF_OR_STRING          0x15	/* Reference or String (Used by DEREFOF op only) */
#define ARGI_REGION_OR_BUFFER       0x16	/* Used by LOAD op only */
#define ARGI_DATAREFOBJ             0x17

/* Note: types above can expand to 0x1F maximum */

#define ARGI_INVALID_OPCODE         0xFFFFFFFF

/*
 * Some of the flags and types below are of the form:
 *
 * AML_FLAGS_EXEC_#A_#T,#R, or
 * AML_TYPE_EXEC_#A_#T,#R where:
 *
 *      #A is the number of required arguments
 *      #T is the number of target operands
 *      #R indicates whether there is a return value
 *
 * These types are used for the top-level dispatch of the AML
 * opcode. They group similar operators that can share common
 * front-end code before dispatch to the final code that implements
 * the operator.
 */

/*
 * Opcode information flags
 */
#define AML_LOGICAL                 0x0001
#define AML_LOGICAL_NUMERIC         0x0002
#define AML_MATH                    0x0004
#define AML_CREATE                  0x0008
#define AML_FIELD                   0x0010
#define AML_DEFER                   0x0020
#define AML_NAMED                   0x0040
#define AML_NSNODE                  0x0080
#define AML_NSOPCODE                0x0100
#define AML_NSOBJECT                0x0200
#define AML_HAS_RETVAL              0x0400
#define AML_HAS_TARGET              0x0800
#define AML_HAS_ARGS                0x1000
#define AML_CONSTANT                0x2000
#define AML_NO_OPERAND_RESOLVE      0x4000

/* Convenient flag groupings of the flags above */

#define AML_FLAGS_EXEC_0A_0T_1R                                     AML_HAS_RETVAL
#define AML_FLAGS_EXEC_1A_0T_0R     AML_HAS_ARGS	/* Monadic1  */
#define AML_FLAGS_EXEC_1A_0T_1R     AML_HAS_ARGS |                  AML_HAS_RETVAL	/* Monadic2  */
#define AML_FLAGS_EXEC_1A_1T_0R     AML_HAS_ARGS | AML_HAS_TARGET
#define AML_FLAGS_EXEC_1A_1T_1R     AML_HAS_ARGS | AML_HAS_TARGET | AML_HAS_RETVAL	/* monadic2_r */
#define AML_FLAGS_EXEC_2A_0T_0R     AML_HAS_ARGS	/* Dyadic1   */
#define AML_FLAGS_EXEC_2A_0T_1R     AML_HAS_ARGS |                  AML_HAS_RETVAL	/* Dyadic2   */
#define AML_FLAGS_EXEC_2A_1T_1R     AML_HAS_ARGS | AML_HAS_TARGET | AML_HAS_RETVAL	/* dyadic2_r  */
#define AML_FLAGS_EXEC_2A_2T_1R     AML_HAS_ARGS | AML_HAS_TARGET | AML_HAS_RETVAL
#define AML_FLAGS_EXEC_3A_0T_0R     AML_HAS_ARGS
#define AML_FLAGS_EXEC_3A_1T_1R     AML_HAS_ARGS | AML_HAS_TARGET | AML_HAS_RETVAL
#define AML_FLAGS_EXEC_6A_0T_1R     AML_HAS_ARGS |                  AML_HAS_RETVAL

/*
 * The opcode Type is used in a dispatch table, do not change
 * or add anything new without updating the table.
 */
#define AML_TYPE_EXEC_0A_0T_1R      0x00	/* 0 Args, 0 Target, 1 ret_val */
#define AML_TYPE_EXEC_1A_0T_0R      0x01	/* 1 Args, 0 Target, 0 ret_val */
#define AML_TYPE_EXEC_1A_0T_1R      0x02	/* 1 Args, 0 Target, 1 ret_val */
#define AML_TYPE_EXEC_1A_1T_0R      0x03	/* 1 Args, 1 Target, 0 ret_val */
#define AML_TYPE_EXEC_1A_1T_1R      0x04	/* 1 Args, 1 Target, 1 ret_val */
#define AML_TYPE_EXEC_2A_0T_0R      0x05	/* 2 Args, 0 Target, 0 ret_val */
#define AML_TYPE_EXEC_2A_0T_1R      0x06	/* 2 Args, 0 Target, 1 ret_val */
#define AML_TYPE_EXEC_2A_1T_1R      0x07	/* 2 Args, 1 Target, 1 ret_val */
#define AML_TYPE_EXEC_2A_2T_1R      0x08	/* 2 Args, 2 Target, 1 ret_val */
#define AML_TYPE_EXEC_3A_0T_0R      0x09	/* 3 Args, 0 Target, 0 ret_val */
#define AML_TYPE_EXEC_3A_1T_1R      0x0A	/* 3 Args, 1 Target, 1 ret_val */
#define AML_TYPE_EXEC_6A_0T_1R      0x0B	/* 6 Args, 0 Target, 1 ret_val */
/* End of types used in dispatch table */

#define AML_TYPE_LITERAL            0x0C
#define AML_TYPE_CONSTANT           0x0D
#define AML_TYPE_METHOD_ARGUMENT    0x0E
#define AML_TYPE_LOCAL_VARIABLE     0x0F
#define AML_TYPE_DATA_TERM          0x10

/* Generic for an op that returns a value */

#define AML_TYPE_METHOD_CALL        0x11

/* Miscellaneous types */

#define AML_TYPE_CREATE_FIELD       0x12
#define AML_TYPE_CREATE_OBJECT      0x13
#define AML_TYPE_CONTROL            0x14
#define AML_TYPE_NAMED_NO_OBJ       0x15
#define AML_TYPE_NAMED_FIELD        0x16
#define AML_TYPE_NAMED_SIMPLE       0x17
#define AML_TYPE_NAMED_COMPLEX      0x18
#define AML_TYPE_RETURN             0x19
#define AML_TYPE_UNDEFINED          0x1A
#define AML_TYPE_BOGUS              0x1B

/* AML Package Length encodings */

#define ACPI_AML_PACKAGE_TYPE1      0x40
#define ACPI_AML_PACKAGE_TYPE2      0x4000
#define ACPI_AML_PACKAGE_TYPE3      0x400000
#define ACPI_AML_PACKAGE_TYPE4      0x40000000

/*
 * Opcode classes
 */
#define AML_CLASS_EXECUTE           0x00
#define AML_CLASS_CREATE            0x01
#define AML_CLASS_ARGUMENT          0x02
#define AML_CLASS_NAMED_OBJECT      0x03
#define AML_CLASS_CONTROL           0x04
#define AML_CLASS_ASCII             0x05
#define AML_CLASS_PREFIX            0x06
#define AML_CLASS_INTERNAL          0x07
#define AML_CLASS_RETURN_VALUE      0x08
#define AML_CLASS_METHOD_CALL       0x09
#define AML_CLASS_UNKNOWN           0x0A

/* Comparison operation codes for match_op operator */

typedef enum {
	MATCH_MTR = 0,
	MATCH_MEQ = 1,
	MATCH_MLE = 2,
	MATCH_MLT = 3,
	MATCH_MGE = 4,
	MATCH_MGT = 5
} AML_MATCH_OPERATOR;

#define MAX_MATCH_OPERATOR          5

/*
 * field_flags
 *
 * This byte is extracted from the AML and includes three separate
 * pieces of information about the field:
 * 1) The field access type
 * 2) The field update rule
 * 3) The lock rule for the field
 *
 * Bits 00 - 03 : access_type (any_acc, byte_acc, etc.)
 *      04      : lock_rule (1 == Lock)
 *      05 - 06 : update_rule
 */
#define AML_FIELD_ACCESS_TYPE_MASK  0x0F
#define AML_FIELD_LOCK_RULE_MASK    0x10
#define AML_FIELD_UPDATE_RULE_MASK  0x60

/* 1) Field Access Types */

typedef enum {
	AML_FIELD_ACCESS_ANY = 0x00,
	AML_FIELD_ACCESS_BYTE = 0x01,
	AML_FIELD_ACCESS_WORD = 0x02,
	AML_FIELD_ACCESS_DWORD = 0x03,
	AML_FIELD_ACCESS_QWORD = 0x04,	/* ACPI 2.0 */
	AML_FIELD_ACCESS_BUFFER = 0x05	/* ACPI 2.0 */
} AML_ACCESS_TYPE;

/* 2) Field Lock Rules */

typedef enum {
	AML_FIELD_LOCK_NEVER = 0x00,
	AML_FIELD_LOCK_ALWAYS = 0x10
} AML_LOCK_RULE;

/* 3) Field Update Rules */

typedef enum {
	AML_FIELD_UPDATE_PRESERVE = 0x00,
	AML_FIELD_UPDATE_WRITE_AS_ONES = 0x20,
	AML_FIELD_UPDATE_WRITE_AS_ZEROS = 0x40
} AML_UPDATE_RULE;

/*
 * Field Access Attributes.
 * This byte is extracted from the AML via the
 * access_as keyword
 */
typedef enum {
	AML_FIELD_ATTRIB_QUICK = 0x02,
	AML_FIELD_ATTRIB_SEND_RCV = 0x04,
	AML_FIELD_ATTRIB_BYTE = 0x06,
	AML_FIELD_ATTRIB_WORD = 0x08,
	AML_FIELD_ATTRIB_BLOCK = 0x0A,
	AML_FIELD_ATTRIB_MULTIBYTE = 0x0B,
	AML_FIELD_ATTRIB_WORD_CALL = 0x0C,
	AML_FIELD_ATTRIB_BLOCK_CALL = 0x0D,
	AML_FIELD_ATTRIB_RAW_BYTES = 0x0E,
	AML_FIELD_ATTRIB_RAW_PROCESS = 0x0F
} AML_ACCESS_ATTRIBUTE;

/* Bit fields in the AML method_flags byte */

#define AML_METHOD_ARG_COUNT        0x07
#define AML_METHOD_SERIALIZED       0x08
#define AML_METHOD_SYNC_LEVEL       0xF0

#endif				/* __AMLCODE_H__ */
