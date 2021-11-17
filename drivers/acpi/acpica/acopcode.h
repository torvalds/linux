/* SPDX-License-Identifier: BSD-3-Clause OR GPL-2.0 */
/******************************************************************************
 *
 * Name: acopcode.h - AML opcode information for the AML parser and interpreter
 *
 * Copyright (C) 2000 - 2021, Intel Corp.
 *
 *****************************************************************************/

#ifndef __ACOPCODE_H__
#define __ACOPCODE_H__

#define MAX_EXTENDED_OPCODE         0x88
#define NUM_EXTENDED_OPCODE         (MAX_EXTENDED_OPCODE + 1)
#define MAX_INTERNAL_OPCODE
#define NUM_INTERNAL_OPCODE         (MAX_INTERNAL_OPCODE + 1)

/* Used for non-assigned opcodes */

#define _UNK                        0x6B

/*
 * Reserved ASCII characters. Do not use any of these for
 * internal opcodes, since they are used to differentiate
 * name strings from AML opcodes
 */
#define _ASC                        0x6C
#define _NAM                        0x6C
#define _PFX                        0x6D

/*
 * All AML opcodes and the parse-time arguments for each. Used by the AML
 * parser  Each list is compressed into a 32-bit number and stored in the
 * master opcode table (in psopcode.c).
 */
#define ARGP_ACCESSFIELD_OP             ARGP_LIST1 (ARGP_NAMESTRING)
#define ARGP_ACQUIRE_OP                 ARGP_LIST2 (ARGP_SUPERNAME,  ARGP_WORDDATA)
#define ARGP_ADD_OP                     ARGP_LIST3 (ARGP_TERMARG,    ARGP_TERMARG,       ARGP_TARGET)
#define ARGP_ALIAS_OP                   ARGP_LIST2 (ARGP_NAMESTRING, ARGP_NAME)
#define ARGP_ARG0                       ARG_NONE
#define ARGP_ARG1                       ARG_NONE
#define ARGP_ARG2                       ARG_NONE
#define ARGP_ARG3                       ARG_NONE
#define ARGP_ARG4                       ARG_NONE
#define ARGP_ARG5                       ARG_NONE
#define ARGP_ARG6                       ARG_NONE
#define ARGP_BANK_FIELD_OP              ARGP_LIST6 (ARGP_PKGLENGTH,  ARGP_NAMESTRING,    ARGP_NAMESTRING,ARGP_TERMARG,   ARGP_BYTEDATA,  ARGP_FIELDLIST)
#define ARGP_BIT_AND_OP                 ARGP_LIST3 (ARGP_TERMARG,    ARGP_TERMARG,       ARGP_TARGET)
#define ARGP_BIT_NAND_OP                ARGP_LIST3 (ARGP_TERMARG,    ARGP_TERMARG,       ARGP_TARGET)
#define ARGP_BIT_NOR_OP                 ARGP_LIST3 (ARGP_TERMARG,    ARGP_TERMARG,       ARGP_TARGET)
#define ARGP_BIT_NOT_OP                 ARGP_LIST2 (ARGP_TERMARG,    ARGP_TARGET)
#define ARGP_BIT_OR_OP                  ARGP_LIST3 (ARGP_TERMARG,    ARGP_TERMARG,       ARGP_TARGET)
#define ARGP_BIT_XOR_OP                 ARGP_LIST3 (ARGP_TERMARG,    ARGP_TERMARG,       ARGP_TARGET)
#define ARGP_BREAK_OP                   ARG_NONE
#define ARGP_BREAK_POINT_OP             ARG_NONE
#define ARGP_BUFFER_OP                  ARGP_LIST3 (ARGP_PKGLENGTH,  ARGP_TERMARG,       ARGP_BYTELIST)
#define ARGP_BYTE_OP                    ARGP_LIST1 (ARGP_BYTEDATA)
#define ARGP_BYTELIST_OP                ARGP_LIST1 (ARGP_NAMESTRING)
#define ARGP_COMMENT_OP                 ARGP_LIST2 (ARGP_BYTEDATA,   ARGP_COMMENT)
#define ARGP_CONCAT_OP                  ARGP_LIST3 (ARGP_TERMARG,    ARGP_TERMARG,       ARGP_TARGET)
#define ARGP_CONCAT_RES_OP              ARGP_LIST3 (ARGP_TERMARG,    ARGP_TERMARG,       ARGP_TARGET)
#define ARGP_COND_REF_OF_OP             ARGP_LIST2 (ARGP_SIMPLENAME, ARGP_TARGET)
#define ARGP_CONNECTFIELD_OP            ARGP_LIST1 (ARGP_NAMESTRING)
#define ARGP_CONTINUE_OP                ARG_NONE
#define ARGP_COPY_OP                    ARGP_LIST2 (ARGP_TERMARG,    ARGP_SIMPLENAME)
#define ARGP_CREATE_BIT_FIELD_OP        ARGP_LIST3 (ARGP_TERMARG,    ARGP_TERMARG,       ARGP_NAME)
#define ARGP_CREATE_BYTE_FIELD_OP       ARGP_LIST3 (ARGP_TERMARG,    ARGP_TERMARG,       ARGP_NAME)
#define ARGP_CREATE_DWORD_FIELD_OP      ARGP_LIST3 (ARGP_TERMARG,    ARGP_TERMARG,       ARGP_NAME)
#define ARGP_CREATE_FIELD_OP            ARGP_LIST4 (ARGP_TERMARG,    ARGP_TERMARG,       ARGP_TERMARG,   ARGP_NAME)
#define ARGP_CREATE_QWORD_FIELD_OP      ARGP_LIST3 (ARGP_TERMARG,    ARGP_TERMARG,       ARGP_NAME)
#define ARGP_CREATE_WORD_FIELD_OP       ARGP_LIST3 (ARGP_TERMARG,    ARGP_TERMARG,       ARGP_NAME)
#define ARGP_DATA_REGION_OP             ARGP_LIST4 (ARGP_NAME,       ARGP_TERMARG,       ARGP_TERMARG,   ARGP_TERMARG)
#define ARGP_DEBUG_OP                   ARG_NONE
#define ARGP_DECREMENT_OP               ARGP_LIST1 (ARGP_SUPERNAME)
#define ARGP_DEREF_OF_OP                ARGP_LIST1 (ARGP_SUPERNAME)
#define ARGP_DEVICE_OP                  ARGP_LIST3 (ARGP_PKGLENGTH,  ARGP_NAME,          ARGP_OBJLIST)
#define ARGP_DIVIDE_OP                  ARGP_LIST4 (ARGP_TERMARG,    ARGP_TERMARG,       ARGP_TARGET,    ARGP_TARGET)
#define ARGP_DWORD_OP                   ARGP_LIST1 (ARGP_DWORDDATA)
#define ARGP_ELSE_OP                    ARGP_LIST2 (ARGP_PKGLENGTH,  ARGP_TERMLIST)
#define ARGP_EVENT_OP                   ARGP_LIST1 (ARGP_NAME)
#define ARGP_EXTERNAL_OP                ARGP_LIST3 (ARGP_NAME,       ARGP_BYTEDATA,      ARGP_BYTEDATA)
#define ARGP_FATAL_OP                   ARGP_LIST3 (ARGP_BYTEDATA,   ARGP_DWORDDATA,     ARGP_TERMARG)
#define ARGP_FIELD_OP                   ARGP_LIST4 (ARGP_PKGLENGTH,  ARGP_NAMESTRING,    ARGP_BYTEDATA,  ARGP_FIELDLIST)
#define ARGP_FIND_SET_LEFT_BIT_OP       ARGP_LIST2 (ARGP_TERMARG,    ARGP_TARGET)
#define ARGP_FIND_SET_RIGHT_BIT_OP      ARGP_LIST2 (ARGP_TERMARG,    ARGP_TARGET)
#define ARGP_FROM_BCD_OP                ARGP_LIST2 (ARGP_TERMARG,    ARGP_TARGET)
#define ARGP_IF_OP                      ARGP_LIST3 (ARGP_PKGLENGTH,  ARGP_TERMARG,       ARGP_TERMLIST)
#define ARGP_INCREMENT_OP               ARGP_LIST1 (ARGP_SUPERNAME)
#define ARGP_INDEX_FIELD_OP             ARGP_LIST5 (ARGP_PKGLENGTH,  ARGP_NAMESTRING,    ARGP_NAMESTRING,ARGP_BYTEDATA,  ARGP_FIELDLIST)
#define ARGP_INDEX_OP                   ARGP_LIST3 (ARGP_TERMARG,    ARGP_TERMARG,       ARGP_TARGET)
#define ARGP_LAND_OP                    ARGP_LIST2 (ARGP_TERMARG,    ARGP_TERMARG)
#define ARGP_LEQUAL_OP                  ARGP_LIST2 (ARGP_TERMARG,    ARGP_TERMARG)
#define ARGP_LGREATER_OP                ARGP_LIST2 (ARGP_TERMARG,    ARGP_TERMARG)
#define ARGP_LGREATEREQUAL_OP           ARGP_LIST2 (ARGP_TERMARG,    ARGP_TERMARG)
#define ARGP_LLESS_OP                   ARGP_LIST2 (ARGP_TERMARG,    ARGP_TERMARG)
#define ARGP_LLESSEQUAL_OP              ARGP_LIST2 (ARGP_TERMARG,    ARGP_TERMARG)
#define ARGP_LNOT_OP                    ARGP_LIST1 (ARGP_TERMARG)
#define ARGP_LNOTEQUAL_OP               ARGP_LIST2 (ARGP_TERMARG,    ARGP_TERMARG)
#define ARGP_LOAD_OP                    ARGP_LIST2 (ARGP_NAMESTRING, ARGP_SUPERNAME)
#define ARGP_LOAD_TABLE_OP              ARGP_LIST6 (ARGP_TERMARG,    ARGP_TERMARG,       ARGP_TERMARG,   ARGP_TERMARG,  ARGP_TERMARG,   ARGP_TERMARG)
#define ARGP_LOCAL0                     ARG_NONE
#define ARGP_LOCAL1                     ARG_NONE
#define ARGP_LOCAL2                     ARG_NONE
#define ARGP_LOCAL3                     ARG_NONE
#define ARGP_LOCAL4                     ARG_NONE
#define ARGP_LOCAL5                     ARG_NONE
#define ARGP_LOCAL6                     ARG_NONE
#define ARGP_LOCAL7                     ARG_NONE
#define ARGP_LOR_OP                     ARGP_LIST2 (ARGP_TERMARG,    ARGP_TERMARG)
#define ARGP_MATCH_OP                   ARGP_LIST6 (ARGP_TERMARG,    ARGP_BYTEDATA,      ARGP_TERMARG,   ARGP_BYTEDATA,  ARGP_TERMARG,   ARGP_TERMARG)
#define ARGP_METHOD_OP                  ARGP_LIST4 (ARGP_PKGLENGTH,  ARGP_NAME,          ARGP_BYTEDATA,  ARGP_TERMLIST)
#define ARGP_METHODCALL_OP              ARGP_LIST1 (ARGP_NAMESTRING)
#define ARGP_MID_OP                     ARGP_LIST4 (ARGP_TERMARG,    ARGP_TERMARG,       ARGP_TERMARG,   ARGP_TARGET)
#define ARGP_MOD_OP                     ARGP_LIST3 (ARGP_TERMARG,    ARGP_TERMARG,       ARGP_TARGET)
#define ARGP_MULTIPLY_OP                ARGP_LIST3 (ARGP_TERMARG,    ARGP_TERMARG,       ARGP_TARGET)
#define ARGP_MUTEX_OP                   ARGP_LIST2 (ARGP_NAME,       ARGP_BYTEDATA)
#define ARGP_NAME_OP                    ARGP_LIST2 (ARGP_NAME,       ARGP_DATAOBJ)
#define ARGP_NAMEDFIELD_OP              ARGP_LIST1 (ARGP_NAMESTRING)
#define ARGP_NAMEPATH_OP                ARGP_LIST1 (ARGP_NAMESTRING)
#define ARGP_NOOP_OP                    ARG_NONE
#define ARGP_NOTIFY_OP                  ARGP_LIST2 (ARGP_SUPERNAME,  ARGP_TERMARG)
#define ARGP_OBJECT_TYPE_OP             ARGP_LIST1 (ARGP_SIMPLENAME)
#define ARGP_ONE_OP                     ARG_NONE
#define ARGP_ONES_OP                    ARG_NONE
#define ARGP_PACKAGE_OP                 ARGP_LIST3 (ARGP_PKGLENGTH,  ARGP_BYTEDATA,      ARGP_DATAOBJLIST)
#define ARGP_POWER_RES_OP               ARGP_LIST5 (ARGP_PKGLENGTH,  ARGP_NAME,          ARGP_BYTEDATA,  ARGP_WORDDATA,  ARGP_OBJLIST)
#define ARGP_PROCESSOR_OP               ARGP_LIST6 (ARGP_PKGLENGTH,  ARGP_NAME,          ARGP_BYTEDATA,  ARGP_DWORDDATA, ARGP_BYTEDATA,  ARGP_OBJLIST)
#define ARGP_QWORD_OP                   ARGP_LIST1 (ARGP_QWORDDATA)
#define ARGP_REF_OF_OP                  ARGP_LIST1 (ARGP_SIMPLENAME)
#define ARGP_REGION_OP                  ARGP_LIST4 (ARGP_NAME,       ARGP_BYTEDATA,      ARGP_TERMARG,   ARGP_TERMARG)
#define ARGP_RELEASE_OP                 ARGP_LIST1 (ARGP_SUPERNAME)
#define ARGP_RESERVEDFIELD_OP           ARGP_LIST1 (ARGP_NAMESTRING)
#define ARGP_RESET_OP                   ARGP_LIST1 (ARGP_SUPERNAME)
#define ARGP_RETURN_OP                  ARGP_LIST1 (ARGP_TERMARG)
#define ARGP_REVISION_OP                ARG_NONE
#define ARGP_SCOPE_OP                   ARGP_LIST3 (ARGP_PKGLENGTH,  ARGP_NAME,          ARGP_TERMLIST)
#define ARGP_SERIALFIELD_OP             ARGP_LIST1 (ARGP_NAMESTRING)
#define ARGP_SHIFT_LEFT_OP              ARGP_LIST3 (ARGP_TERMARG,    ARGP_TERMARG,       ARGP_TARGET)
#define ARGP_SHIFT_RIGHT_OP             ARGP_LIST3 (ARGP_TERMARG,    ARGP_TERMARG,       ARGP_TARGET)
#define ARGP_SIGNAL_OP                  ARGP_LIST1 (ARGP_SUPERNAME)
#define ARGP_SIZE_OF_OP                 ARGP_LIST1 (ARGP_SUPERNAME)
#define ARGP_SLEEP_OP                   ARGP_LIST1 (ARGP_TERMARG)
#define ARGP_STALL_OP                   ARGP_LIST1 (ARGP_TERMARG)
#define ARGP_STATICSTRING_OP            ARGP_LIST1 (ARGP_NAMESTRING)
#define ARGP_STORE_OP                   ARGP_LIST2 (ARGP_TERMARG,    ARGP_SUPERNAME)
#define ARGP_STRING_OP                  ARGP_LIST1 (ARGP_CHARLIST)
#define ARGP_SUBTRACT_OP                ARGP_LIST3 (ARGP_TERMARG,    ARGP_TERMARG,       ARGP_TARGET)
#define ARGP_THERMAL_ZONE_OP            ARGP_LIST3 (ARGP_PKGLENGTH,  ARGP_NAME,          ARGP_OBJLIST)
#define ARGP_TIMER_OP                   ARG_NONE
#define ARGP_TO_BCD_OP                  ARGP_LIST2 (ARGP_TERMARG,    ARGP_TARGET)
#define ARGP_TO_BUFFER_OP               ARGP_LIST2 (ARGP_TERMARG,    ARGP_TARGET)
#define ARGP_TO_DEC_STR_OP              ARGP_LIST2 (ARGP_TERMARG,    ARGP_TARGET)
#define ARGP_TO_HEX_STR_OP              ARGP_LIST2 (ARGP_TERMARG,    ARGP_TARGET)
#define ARGP_TO_INTEGER_OP              ARGP_LIST2 (ARGP_TERMARG,    ARGP_TARGET)
#define ARGP_TO_STRING_OP               ARGP_LIST3 (ARGP_TERMARG,    ARGP_TERMARG,       ARGP_TARGET)
#define ARGP_UNLOAD_OP                  ARGP_LIST1 (ARGP_SUPERNAME)
#define ARGP_VAR_PACKAGE_OP             ARGP_LIST3 (ARGP_PKGLENGTH,  ARGP_TERMARG,       ARGP_DATAOBJLIST)
#define ARGP_WAIT_OP                    ARGP_LIST2 (ARGP_SUPERNAME,  ARGP_TERMARG)
#define ARGP_WHILE_OP                   ARGP_LIST3 (ARGP_PKGLENGTH,  ARGP_TERMARG,       ARGP_TERMLIST)
#define ARGP_WORD_OP                    ARGP_LIST1 (ARGP_WORDDATA)
#define ARGP_ZERO_OP                    ARG_NONE

/*
 * All AML opcodes and the runtime arguments for each. Used by the AML
 * interpreter  Each list is compressed into a 32-bit number and stored
 * in the master opcode table (in psopcode.c).
 *
 * (Used by prep_operands procedure and the ASL Compiler)
 */
#define ARGI_ACCESSFIELD_OP             ARGI_INVALID_OPCODE
#define ARGI_ACQUIRE_OP                 ARGI_LIST2 (ARGI_MUTEX,      ARGI_INTEGER)
#define ARGI_ADD_OP                     ARGI_LIST3 (ARGI_INTEGER,    ARGI_INTEGER,       ARGI_TARGETREF)
#define ARGI_ALIAS_OP                   ARGI_INVALID_OPCODE
#define ARGI_ARG0                       ARG_NONE
#define ARGI_ARG1                       ARG_NONE
#define ARGI_ARG2                       ARG_NONE
#define ARGI_ARG3                       ARG_NONE
#define ARGI_ARG4                       ARG_NONE
#define ARGI_ARG5                       ARG_NONE
#define ARGI_ARG6                       ARG_NONE
#define ARGI_BANK_FIELD_OP              ARGI_LIST1 (ARGI_INTEGER)
#define ARGI_BIT_AND_OP                 ARGI_LIST3 (ARGI_INTEGER,    ARGI_INTEGER,       ARGI_TARGETREF)
#define ARGI_BIT_NAND_OP                ARGI_LIST3 (ARGI_INTEGER,    ARGI_INTEGER,       ARGI_TARGETREF)
#define ARGI_BIT_NOR_OP                 ARGI_LIST3 (ARGI_INTEGER,    ARGI_INTEGER,       ARGI_TARGETREF)
#define ARGI_BIT_NOT_OP                 ARGI_LIST2 (ARGI_INTEGER,    ARGI_TARGETREF)
#define ARGI_BIT_OR_OP                  ARGI_LIST3 (ARGI_INTEGER,    ARGI_INTEGER,       ARGI_TARGETREF)
#define ARGI_BIT_XOR_OP                 ARGI_LIST3 (ARGI_INTEGER,    ARGI_INTEGER,       ARGI_TARGETREF)
#define ARGI_BREAK_OP                   ARG_NONE
#define ARGI_BREAK_POINT_OP             ARG_NONE
#define ARGI_BUFFER_OP                  ARGI_LIST1 (ARGI_INTEGER)
#define ARGI_BYTE_OP                    ARGI_INVALID_OPCODE
#define ARGI_BYTELIST_OP                ARGI_INVALID_OPCODE
#define ARGI_COMMENT_OP                 ARGI_INVALID_OPCODE
#define ARGI_CONCAT_OP                  ARGI_LIST3 (ARGI_ANYTYPE,    ARGI_ANYTYPE,       ARGI_TARGETREF)
#define ARGI_CONCAT_RES_OP              ARGI_LIST3 (ARGI_BUFFER,     ARGI_BUFFER,        ARGI_TARGETREF)
#define ARGI_COND_REF_OF_OP             ARGI_LIST2 (ARGI_OBJECT_REF, ARGI_TARGETREF)
#define ARGI_CONNECTFIELD_OP            ARGI_INVALID_OPCODE
#define ARGI_CONTINUE_OP                ARGI_INVALID_OPCODE
#define ARGI_COPY_OP                    ARGI_LIST2 (ARGI_ANYTYPE,    ARGI_SIMPLE_TARGET)
#define ARGI_CREATE_BIT_FIELD_OP        ARGI_LIST3 (ARGI_BUFFER,     ARGI_INTEGER,       ARGI_REFERENCE)
#define ARGI_CREATE_BYTE_FIELD_OP       ARGI_LIST3 (ARGI_BUFFER,     ARGI_INTEGER,       ARGI_REFERENCE)
#define ARGI_CREATE_DWORD_FIELD_OP      ARGI_LIST3 (ARGI_BUFFER,     ARGI_INTEGER,       ARGI_REFERENCE)
#define ARGI_CREATE_FIELD_OP            ARGI_LIST4 (ARGI_BUFFER,     ARGI_INTEGER,       ARGI_INTEGER,      ARGI_REFERENCE)
#define ARGI_CREATE_QWORD_FIELD_OP      ARGI_LIST3 (ARGI_BUFFER,     ARGI_INTEGER,       ARGI_REFERENCE)
#define ARGI_CREATE_WORD_FIELD_OP       ARGI_LIST3 (ARGI_BUFFER,     ARGI_INTEGER,       ARGI_REFERENCE)
#define ARGI_DATA_REGION_OP             ARGI_LIST3 (ARGI_STRING,     ARGI_STRING,        ARGI_STRING)
#define ARGI_DEBUG_OP                   ARG_NONE
#define ARGI_DECREMENT_OP               ARGI_LIST1 (ARGI_TARGETREF)
#define ARGI_DEREF_OF_OP                ARGI_LIST1 (ARGI_REF_OR_STRING)
#define ARGI_DEVICE_OP                  ARGI_INVALID_OPCODE
#define ARGI_DIVIDE_OP                  ARGI_LIST4 (ARGI_INTEGER,    ARGI_INTEGER,       ARGI_TARGETREF,    ARGI_TARGETREF)
#define ARGI_DWORD_OP                   ARGI_INVALID_OPCODE
#define ARGI_ELSE_OP                    ARGI_INVALID_OPCODE
#define ARGI_EVENT_OP                   ARGI_INVALID_OPCODE
#define ARGI_EXTERNAL_OP                ARGI_LIST3 (ARGI_STRING,     ARGI_INTEGER,       ARGI_INTEGER)
#define ARGI_FATAL_OP                   ARGI_LIST3 (ARGI_INTEGER,    ARGI_INTEGER,       ARGI_INTEGER)
#define ARGI_FIELD_OP                   ARGI_INVALID_OPCODE
#define ARGI_FIND_SET_LEFT_BIT_OP       ARGI_LIST2 (ARGI_INTEGER,    ARGI_TARGETREF)
#define ARGI_FIND_SET_RIGHT_BIT_OP      ARGI_LIST2 (ARGI_INTEGER,    ARGI_TARGETREF)
#define ARGI_FROM_BCD_OP                ARGI_LIST2 (ARGI_INTEGER,    ARGI_FIXED_TARGET)
#define ARGI_IF_OP                      ARGI_INVALID_OPCODE
#define ARGI_INCREMENT_OP               ARGI_LIST1 (ARGI_TARGETREF)
#define ARGI_INDEX_FIELD_OP             ARGI_INVALID_OPCODE
#define ARGI_INDEX_OP                   ARGI_LIST3 (ARGI_COMPLEXOBJ, ARGI_INTEGER,       ARGI_TARGETREF)
#define ARGI_LAND_OP                    ARGI_LIST2 (ARGI_INTEGER,    ARGI_INTEGER)
#define ARGI_LEQUAL_OP                  ARGI_LIST2 (ARGI_COMPUTEDATA,ARGI_COMPUTEDATA)
#define ARGI_LGREATER_OP                ARGI_LIST2 (ARGI_COMPUTEDATA,ARGI_COMPUTEDATA)
#define ARGI_LGREATEREQUAL_OP           ARGI_INVALID_OPCODE
#define ARGI_LLESS_OP                   ARGI_LIST2 (ARGI_COMPUTEDATA,ARGI_COMPUTEDATA)
#define ARGI_LLESSEQUAL_OP              ARGI_INVALID_OPCODE
#define ARGI_LNOT_OP                    ARGI_LIST1 (ARGI_INTEGER)
#define ARGI_LNOTEQUAL_OP               ARGI_INVALID_OPCODE
#define ARGI_LOAD_OP                    ARGI_LIST2 (ARGI_REGION_OR_BUFFER,ARGI_TARGETREF)
#define ARGI_LOAD_TABLE_OP              ARGI_LIST6 (ARGI_STRING,     ARGI_STRING,        ARGI_STRING,       ARGI_STRING,    ARGI_STRING, ARGI_ANYTYPE)
#define ARGI_LOCAL0                     ARG_NONE
#define ARGI_LOCAL1                     ARG_NONE
#define ARGI_LOCAL2                     ARG_NONE
#define ARGI_LOCAL3                     ARG_NONE
#define ARGI_LOCAL4                     ARG_NONE
#define ARGI_LOCAL5                     ARG_NONE
#define ARGI_LOCAL6                     ARG_NONE
#define ARGI_LOCAL7                     ARG_NONE
#define ARGI_LOR_OP                     ARGI_LIST2 (ARGI_INTEGER,    ARGI_INTEGER)
#define ARGI_MATCH_OP                   ARGI_LIST6 (ARGI_PACKAGE,    ARGI_INTEGER,   ARGI_COMPUTEDATA,      ARGI_INTEGER,ARGI_COMPUTEDATA,ARGI_INTEGER)
#define ARGI_METHOD_OP                  ARGI_INVALID_OPCODE
#define ARGI_METHODCALL_OP              ARGI_INVALID_OPCODE
#define ARGI_MID_OP                     ARGI_LIST4 (ARGI_BUFFER_OR_STRING,ARGI_INTEGER,  ARGI_INTEGER,      ARGI_TARGETREF)
#define ARGI_MOD_OP                     ARGI_LIST3 (ARGI_INTEGER,    ARGI_INTEGER,       ARGI_TARGETREF)
#define ARGI_MULTIPLY_OP                ARGI_LIST3 (ARGI_INTEGER,    ARGI_INTEGER,       ARGI_TARGETREF)
#define ARGI_MUTEX_OP                   ARGI_INVALID_OPCODE
#define ARGI_NAME_OP                    ARGI_INVALID_OPCODE
#define ARGI_NAMEDFIELD_OP              ARGI_INVALID_OPCODE
#define ARGI_NAMEPATH_OP                ARGI_INVALID_OPCODE
#define ARGI_NOOP_OP                    ARG_NONE
#define ARGI_NOTIFY_OP                  ARGI_LIST2 (ARGI_DEVICE_REF, ARGI_INTEGER)
#define ARGI_OBJECT_TYPE_OP             ARGI_LIST1 (ARGI_ANYTYPE)
#define ARGI_ONE_OP                     ARG_NONE
#define ARGI_ONES_OP                    ARG_NONE
#define ARGI_PACKAGE_OP                 ARGI_LIST1 (ARGI_INTEGER)
#define ARGI_POWER_RES_OP               ARGI_INVALID_OPCODE
#define ARGI_PROCESSOR_OP               ARGI_INVALID_OPCODE
#define ARGI_QWORD_OP                   ARGI_INVALID_OPCODE
#define ARGI_REF_OF_OP                  ARGI_LIST1 (ARGI_OBJECT_REF)
#define ARGI_REGION_OP                  ARGI_LIST2 (ARGI_INTEGER,    ARGI_INTEGER)
#define ARGI_RELEASE_OP                 ARGI_LIST1 (ARGI_MUTEX)
#define ARGI_RESERVEDFIELD_OP           ARGI_INVALID_OPCODE
#define ARGI_RESET_OP                   ARGI_LIST1 (ARGI_EVENT)
#define ARGI_RETURN_OP                  ARGI_INVALID_OPCODE
#define ARGI_REVISION_OP                ARG_NONE
#define ARGI_SCOPE_OP                   ARGI_INVALID_OPCODE
#define ARGI_SERIALFIELD_OP             ARGI_INVALID_OPCODE
#define ARGI_SHIFT_LEFT_OP              ARGI_LIST3 (ARGI_INTEGER,    ARGI_INTEGER,       ARGI_TARGETREF)
#define ARGI_SHIFT_RIGHT_OP             ARGI_LIST3 (ARGI_INTEGER,    ARGI_INTEGER,       ARGI_TARGETREF)
#define ARGI_SIGNAL_OP                  ARGI_LIST1 (ARGI_EVENT)
#define ARGI_SIZE_OF_OP                 ARGI_LIST1 (ARGI_DATAOBJECT)
#define ARGI_SLEEP_OP                   ARGI_LIST1 (ARGI_INTEGER)
#define ARGI_STALL_OP                   ARGI_LIST1 (ARGI_INTEGER)
#define ARGI_STATICSTRING_OP            ARGI_INVALID_OPCODE
#define ARGI_STORE_OP                   ARGI_LIST2 (ARGI_DATAREFOBJ, ARGI_STORE_TARGET)
#define ARGI_STRING_OP                  ARGI_INVALID_OPCODE
#define ARGI_SUBTRACT_OP                ARGI_LIST3 (ARGI_INTEGER,    ARGI_INTEGER,       ARGI_TARGETREF)
#define ARGI_THERMAL_ZONE_OP            ARGI_INVALID_OPCODE
#define ARGI_TIMER_OP                   ARG_NONE
#define ARGI_TO_BCD_OP                  ARGI_LIST2 (ARGI_INTEGER,    ARGI_FIXED_TARGET)
#define ARGI_TO_BUFFER_OP               ARGI_LIST2 (ARGI_COMPUTEDATA,ARGI_FIXED_TARGET)
#define ARGI_TO_DEC_STR_OP              ARGI_LIST2 (ARGI_COMPUTEDATA,ARGI_FIXED_TARGET)
#define ARGI_TO_HEX_STR_OP              ARGI_LIST2 (ARGI_COMPUTEDATA,ARGI_FIXED_TARGET)
#define ARGI_TO_INTEGER_OP              ARGI_LIST2 (ARGI_COMPUTEDATA,ARGI_FIXED_TARGET)
#define ARGI_TO_STRING_OP               ARGI_LIST3 (ARGI_BUFFER,     ARGI_INTEGER,       ARGI_FIXED_TARGET)
#define ARGI_UNLOAD_OP                  ARGI_LIST1 (ARGI_DDBHANDLE)
#define ARGI_VAR_PACKAGE_OP             ARGI_LIST1 (ARGI_INTEGER)
#define ARGI_WAIT_OP                    ARGI_LIST2 (ARGI_EVENT,      ARGI_INTEGER)
#define ARGI_WHILE_OP                   ARGI_INVALID_OPCODE
#define ARGI_WORD_OP                    ARGI_INVALID_OPCODE
#define ARGI_ZERO_OP                    ARG_NONE

#endif				/* __ACOPCODE_H__ */
