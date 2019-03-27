/* params.h - Run-time parameters.
   Copyright (C) 2001, 2003, 2004, 2005 Free Software Foundation, Inc.
   Written by Mark Mitchell <mark@codesourcery.com>.

This file is part of GCC.

GCC is free software; you can redistribute it and/or modify it under
the terms of the GNU General Public License as published by the Free
Software Foundation; either version 2, or (at your option) any later
version.

GCC is distributed in the hope that it will be useful, but WITHOUT ANY
WARRANTY; without even the implied warranty of MERCHANTABILITY or
FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
for more details.

You should have received a copy of the GNU General Public License
along with GCC; see the file COPYING.  If not, write to the Free
Software Foundation, 51 Franklin Street, Fifth Floor, Boston, MA
02110-1301, USA.

*/

/* This module provides a means for setting integral parameters
   dynamically.  Instead of encoding magic numbers in various places,
   use this module to organize all the magic numbers in a single
   place.  The values of the parameters can be set on the
   command-line, thereby providing a way to control the amount of
   effort spent on particular optimization passes, or otherwise tune
   the behavior of the compiler.

   Since their values can be set on the command-line, these parameters
   should not be used for non-dynamic memory allocation.  */

#ifndef GCC_PARAMS_H
#define GCC_PARAMS_H

/* No parameter shall have this value.  */

#define INVALID_PARAM_VAL (-1)

/* The information associated with each parameter.  */

typedef struct param_info
{
  /* The name used with the `--param <name>=<value>' switch to set this
     value.  */
  const char *const option;
  /* The associated value.  */
  int value;

  /* Minimum acceptable value.  */
  int min_value;
  
  /* Maximum acceptable value, if greater than minimum  */
  int max_value;
  
  /* A short description of the option.  */
  const char *const help;
} param_info;

/* An array containing the compiler parameters and their current
   values.  */

extern param_info *compiler_params;

/* Add the N PARAMS to the current list of compiler parameters.  */

extern void add_params (const param_info params[], size_t n);

/* Set the VALUE associated with the parameter given by NAME.  */

extern void set_param_value (const char *name, int value);


/* The parameters in use by language-independent code.  */

typedef enum compiler_param
{
#define DEFPARAM(enumerator, option, msgid, default, min, max) \
  enumerator,
#include "params.def"
#undef DEFPARAM
  LAST_PARAM
} compiler_param;

/* The value of the parameter given by ENUM.  */
#define PARAM_VALUE(ENUM) \
  (compiler_params[(int) ENUM].value)

/* Macros for the various parameters.  */
#define SALIAS_MAX_IMPLICIT_FIELDS \
  PARAM_VALUE (PARAM_SALIAS_MAX_IMPLICIT_FIELDS)
#define SALIAS_MAX_ARRAY_ELEMENTS \
  PARAM_VALUE (PARAM_SALIAS_MAX_ARRAY_ELEMENTS)
#define SRA_MAX_STRUCTURE_SIZE \
  PARAM_VALUE (PARAM_SRA_MAX_STRUCTURE_SIZE)
#define SRA_MAX_STRUCTURE_COUNT \
  PARAM_VALUE (PARAM_SRA_MAX_STRUCTURE_COUNT)
#define SRA_FIELD_STRUCTURE_RATIO \
  PARAM_VALUE (PARAM_SRA_FIELD_STRUCTURE_RATIO)
#define MAX_INLINE_INSNS_SINGLE \
  PARAM_VALUE (PARAM_MAX_INLINE_INSNS_SINGLE)
#define MAX_INLINE_INSNS \
  PARAM_VALUE (PARAM_MAX_INLINE_INSNS)
#define MAX_INLINE_SLOPE \
  PARAM_VALUE (PARAM_MAX_INLINE_SLOPE)
#define MIN_INLINE_INSNS \
  PARAM_VALUE (PARAM_MIN_INLINE_INSNS)
#define MAX_INLINE_INSNS_AUTO \
  PARAM_VALUE (PARAM_MAX_INLINE_INSNS_AUTO)
#define MAX_VARIABLE_EXPANSIONS \
  PARAM_VALUE (PARAM_MAX_VARIABLE_EXPANSIONS)
#define MAX_DELAY_SLOT_INSN_SEARCH \
  PARAM_VALUE (PARAM_MAX_DELAY_SLOT_INSN_SEARCH)
#define MAX_DELAY_SLOT_LIVE_SEARCH \
  PARAM_VALUE (PARAM_MAX_DELAY_SLOT_LIVE_SEARCH)
#define MAX_PENDING_LIST_LENGTH \
  PARAM_VALUE (PARAM_MAX_PENDING_LIST_LENGTH)
#define MAX_GCSE_MEMORY \
  ((size_t) PARAM_VALUE (PARAM_MAX_GCSE_MEMORY))
#define MAX_GCSE_PASSES \
  PARAM_VALUE (PARAM_MAX_GCSE_PASSES)
#define GCSE_AFTER_RELOAD_PARTIAL_FRACTION \
  PARAM_VALUE (PARAM_GCSE_AFTER_RELOAD_PARTIAL_FRACTION)
#define GCSE_AFTER_RELOAD_CRITICAL_FRACTION \
  PARAM_VALUE (PARAM_GCSE_AFTER_RELOAD_CRITICAL_FRACTION)
#define MAX_UNROLLED_INSNS \
  PARAM_VALUE (PARAM_MAX_UNROLLED_INSNS)
#define MAX_SMS_LOOP_NUMBER \
  PARAM_VALUE (PARAM_MAX_SMS_LOOP_NUMBER)
#define SMS_MAX_II_FACTOR \
  PARAM_VALUE (PARAM_SMS_MAX_II_FACTOR)
#define SMS_DFA_HISTORY \
  PARAM_VALUE (PARAM_SMS_DFA_HISTORY)
#define SMS_LOOP_AVERAGE_COUNT_THRESHOLD \
  PARAM_VALUE (PARAM_SMS_LOOP_AVERAGE_COUNT_THRESHOLD)
#define GLOBAL_VAR_THRESHOLD \
  PARAM_VALUE (PARAM_GLOBAL_VAR_THRESHOLD)
#define MAX_ALIASED_VOPS \
  PARAM_VALUE (PARAM_MAX_ALIASED_VOPS)
#define INTEGER_SHARE_LIMIT \
  PARAM_VALUE (PARAM_INTEGER_SHARE_LIMIT)
#define MAX_LAST_VALUE_RTL \
  PARAM_VALUE (PARAM_MAX_LAST_VALUE_RTL)
#define MIN_VIRTUAL_MAPPINGS \
  PARAM_VALUE (PARAM_MIN_VIRTUAL_MAPPINGS)
#define VIRTUAL_MAPPINGS_TO_SYMS_RATIO \
  PARAM_VALUE (PARAM_VIRTUAL_MAPPINGS_TO_SYMS_RATIO)
#define MAX_FIELDS_FOR_FIELD_SENSITIVE \
  ((size_t) PARAM_VALUE (PARAM_MAX_FIELDS_FOR_FIELD_SENSITIVE))
#define MAX_SCHED_READY_INSNS \
  PARAM_VALUE (PARAM_MAX_SCHED_READY_INSNS)
#endif /* ! GCC_PARAMS_H */
