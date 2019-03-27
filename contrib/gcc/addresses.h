/* Inline functions to test validity of reg classes for addressing modes.
   Copyright (C) 2006 Free Software Foundation, Inc.

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
02110-1301, USA.  */

/* Wrapper function to unify target macros MODE_CODE_BASE_REG_CLASS,
   MODE_BASE_REG_REG_CLASS, MODE_BASE_REG_CLASS and BASE_REG_CLASS.
   Arguments as for the MODE_CODE_BASE_REG_CLASS macro.  */

static inline enum reg_class
base_reg_class (enum machine_mode mode ATTRIBUTE_UNUSED,
		enum rtx_code outer_code ATTRIBUTE_UNUSED,
		enum rtx_code index_code ATTRIBUTE_UNUSED)
{
#ifdef MODE_CODE_BASE_REG_CLASS
  return MODE_CODE_BASE_REG_CLASS (mode, outer_code, index_code);
#else
#ifdef MODE_BASE_REG_REG_CLASS
  if (index_code == REG)
    return MODE_BASE_REG_REG_CLASS (mode);
#endif
#ifdef MODE_BASE_REG_CLASS
  return MODE_BASE_REG_CLASS (mode);
#else
  return BASE_REG_CLASS;
#endif
#endif
}

/* Wrapper function to unify target macros REGNO_MODE_CODE_OK_FOR_BASE_P,
   REGNO_MODE_OK_FOR_REG_BASE_P, REGNO_MODE_OK_FOR_BASE_P and
   REGNO_OK_FOR_BASE_P.
   Arguments as for the REGNO_MODE_CODE_OK_FOR_BASE_P macro.  */

static inline bool
ok_for_base_p_1 (unsigned regno, enum machine_mode mode ATTRIBUTE_UNUSED,
		 enum rtx_code outer_code ATTRIBUTE_UNUSED,
		 enum rtx_code index_code ATTRIBUTE_UNUSED)
{
#ifdef REGNO_MODE_CODE_OK_FOR_BASE_P
  return REGNO_MODE_CODE_OK_FOR_BASE_P (regno, mode, outer_code, index_code);
#else
#ifdef REGNO_MODE_OK_FOR_REG_BASE_P
  if (index_code == REG)
    return REGNO_MODE_OK_FOR_REG_BASE_P (regno, mode);
#endif
#ifdef REGNO_MODE_OK_FOR_BASE_P
  return REGNO_MODE_OK_FOR_BASE_P (regno, mode);
#else
  return REGNO_OK_FOR_BASE_P (regno);
#endif
#endif
}

/* Wrapper around ok_for_base_p_1, for use after register allocation is
   complete.  Arguments as for the called function.  */

static inline bool
regno_ok_for_base_p (unsigned regno, enum machine_mode mode,
		     enum rtx_code outer_code, enum rtx_code index_code)
{
  if (regno >= FIRST_PSEUDO_REGISTER && reg_renumber[regno] >= 0)
    regno = reg_renumber[regno];

  return ok_for_base_p_1 (regno, mode, outer_code, index_code);
}
