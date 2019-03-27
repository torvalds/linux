/* Routines for GCC for ARM/pe.
   Copyright (C) 1995, 1996, 2000, 2001, 2002, 2004, 2005
   Free Software Foundation, Inc.
   Contributed by Doug Evans (dje@cygnus.com).

   This file is part of GCC.

   GCC is free software; you can redistribute it and/or modify it
   under the terms of the GNU General Public License as published
   by the Free Software Foundation; either version 2, or (at your
   option) any later version.

   GCC is distributed in the hope that it will be useful, but WITHOUT
   ANY WARRANTY; without even the implied warranty of MERCHANTABILITY
   or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public
   License for more details.

   You should have received a copy of the GNU General Public License
   along with GCC; see the file COPYING.  If not, write to
   the Free Software Foundation, 51 Franklin Street, Fifth Floor,
   Boston, MA 02110-1301, USA.  */

#include "config.h"
#include "system.h"
#include "coretypes.h"
#include "tm.h"
#include "rtl.h"
#include "output.h"
#include "flags.h"
#include "tree.h"
#include "expr.h"
#include "toplev.h"
#include "tm_p.h"

extern int current_function_anonymous_args;


/* Return nonzero if DECL is a dllexport'd object.  */

tree current_class_type; /* FIXME */

int
arm_dllexport_p (decl)
     tree decl;
{
  tree exp;

  if (TREE_CODE (decl) != VAR_DECL
      && TREE_CODE (decl) != FUNCTION_DECL)
    return 0;
  exp = lookup_attribute ("dllexport", DECL_ATTRIBUTES (decl));
  if (exp)
    return 1;

  return 0;
}

/* Return nonzero if DECL is a dllimport'd object.  */

int
arm_dllimport_p (decl)
     tree decl;
{
  tree imp;

  if (TREE_CODE (decl) == FUNCTION_DECL
      && TARGET_NOP_FUN_DLLIMPORT)
    return 0;

  if (TREE_CODE (decl) != VAR_DECL
      && TREE_CODE (decl) != FUNCTION_DECL)
    return 0;
  imp = lookup_attribute ("dllimport", DECL_ATTRIBUTES (decl));
  if (imp)
    return 1;

  return 0;
}

/* Return nonzero if SYMBOL is marked as being dllexport'd.  */

int
arm_dllexport_name_p (symbol)
     const char * symbol;
{
  return symbol[0] == ARM_PE_FLAG_CHAR && symbol[1] == 'e' && symbol[2] == '.';
}

/* Return nonzero if SYMBOL is marked as being dllimport'd.  */

int
arm_dllimport_name_p (symbol)
     const char * symbol;
{
  return symbol[0] == ARM_PE_FLAG_CHAR && symbol[1] == 'i' && symbol[2] == '.';
}

/* Mark a DECL as being dllexport'd.
   Note that we override the previous setting (e.g.: dllimport).  */

void
arm_mark_dllexport (decl)
     tree decl;
{
  const char * oldname;
  char * newname;
  rtx rtlname;
  tree idp;

  rtlname = XEXP (DECL_RTL (decl), 0);
  if (GET_CODE (rtlname) == MEM)
    rtlname = XEXP (rtlname, 0);
  gcc_assert (GET_CODE (rtlname) == SYMBOL_REF);
  oldname = XSTR (rtlname, 0);
  
  if (arm_dllimport_name_p (oldname))
    oldname += 9;
  else if (arm_dllexport_name_p (oldname))
    return; /* already done */

  newname = alloca (strlen (oldname) + 4);
  sprintf (newname, "%ce.%s", ARM_PE_FLAG_CHAR, oldname);

  /* We pass newname through get_identifier to ensure it has a unique
     address.  RTL processing can sometimes peek inside the symbol ref
     and compare the string's addresses to see if two symbols are
     identical.  */
  /* ??? At least I think that's why we do this.  */
  idp = get_identifier (newname);

  XEXP (DECL_RTL (decl), 0) =
    gen_rtx_SYMBOL_REF (Pmode, IDENTIFIER_POINTER (idp));
}

/* Mark a DECL as being dllimport'd.  */

void
arm_mark_dllimport (decl)
     tree decl;
{
  const char * oldname;
  char * newname;
  tree idp;
  rtx rtlname, newrtl;

  rtlname = XEXP (DECL_RTL (decl), 0);
  
  if (GET_CODE (rtlname) == MEM)
    rtlname = XEXP (rtlname, 0);
  gcc_assert (GET_CODE (rtlname) == SYMBOL_REF);
  oldname = XSTR (rtlname, 0);
  
  gcc_assert (!arm_dllexport_name_p (oldname));
  if (arm_dllimport_name_p (oldname))
    return; /* already done */

  /* ??? One can well ask why we're making these checks here,
     and that would be a good question.  */

  /* Imported variables can't be initialized.  */
  if (TREE_CODE (decl) == VAR_DECL
      && !DECL_VIRTUAL_P (decl)
      && DECL_INITIAL (decl))
    {
      error ("initialized variable %q+D is marked dllimport", decl);
      return;
    }
  /* Nor can they be static.  */
  if (TREE_CODE (decl) == VAR_DECL
      /* ??? Is this test for vtables needed?  */
      && !DECL_VIRTUAL_P (decl)
      && 0 /*???*/)
    {
      error ("static variable %q+D is marked dllimport", decl);
      return;
    }

  /* `extern' needn't be specified with dllimport.
     Specify `extern' now and hope for the best.  Sigh.  */
  if (TREE_CODE (decl) == VAR_DECL
      /* ??? Is this test for vtables needed?  */
      && !DECL_VIRTUAL_P (decl))
    {
      DECL_EXTERNAL (decl) = 1;
      TREE_PUBLIC (decl) = 1;
    }

  newname = alloca (strlen (oldname) + 11);
  sprintf (newname, "%ci.__imp_%s", ARM_PE_FLAG_CHAR, oldname);

  /* We pass newname through get_identifier to ensure it has a unique
     address.  RTL processing can sometimes peek inside the symbol ref
     and compare the string's addresses to see if two symbols are
     identical.  */
  /* ??? At least I think that's why we do this.  */
  idp = get_identifier (newname);

  newrtl = gen_rtx_MEM (Pmode,
			gen_rtx_SYMBOL_REF (Pmode,
					    IDENTIFIER_POINTER (idp)));
  XEXP (DECL_RTL (decl), 0) = newrtl;
}

void
arm_pe_encode_section_info (decl, rtl, first)
     tree decl;
     rtx rtl;
     int first ATTRIBUTE_UNUSED;
{
  /* This bit is copied from arm_encode_section_info.  */
  if (optimize > 0 && TREE_CONSTANT (decl))
    SYMBOL_REF_FLAG (XEXP (rtl, 0)) = 1;

  /* Mark the decl so we can tell from the rtl whether the object is
     dllexport'd or dllimport'd.  */
  if (arm_dllexport_p (decl))
    arm_mark_dllexport (decl);
  else if (arm_dllimport_p (decl))
    arm_mark_dllimport (decl);
  /* It might be that DECL has already been marked as dllimport, but a
     subsequent definition nullified that.  The attribute is gone but
     DECL_RTL still has @i.__imp_foo.  We need to remove that.  */
  else if ((TREE_CODE (decl) == FUNCTION_DECL
	    || TREE_CODE (decl) == VAR_DECL)
	   && DECL_RTL (decl) != NULL_RTX
	   && GET_CODE (DECL_RTL (decl)) == MEM
	   && GET_CODE (XEXP (DECL_RTL (decl), 0)) == MEM
	   && GET_CODE (XEXP (XEXP (DECL_RTL (decl), 0), 0)) == SYMBOL_REF
	   && arm_dllimport_name_p (XSTR (XEXP (XEXP (DECL_RTL (decl), 0), 0), 0)))
    {
      const char *oldname = XSTR (XEXP (XEXP (DECL_RTL (decl), 0), 0), 0);
      tree idp = get_identifier (oldname + 9);
      rtx newrtl = gen_rtx_SYMBOL_REF (Pmode, IDENTIFIER_POINTER (idp));

      XEXP (DECL_RTL (decl), 0) = newrtl;

      /* We previously set TREE_PUBLIC and DECL_EXTERNAL.
	 ??? We leave these alone for now.  */
    }
}

void
arm_pe_unique_section (decl, reloc)
     tree decl;
     int reloc;
{
  int len;
  const char * name;
  char * string;
  const char * prefix;

  name = IDENTIFIER_POINTER (DECL_ASSEMBLER_NAME (decl));
  name = arm_strip_name_encoding (name);

  /* The object is put in, for example, section .text$foo.
     The linker will then ultimately place them in .text
     (everything from the $ on is stripped).  */
  if (TREE_CODE (decl) == FUNCTION_DECL)
    prefix = ".text$";
  else if (decl_readonly_section (decl, reloc))
    prefix = ".rdata$";
  else
    prefix = ".data$";
  len = strlen (name) + strlen (prefix);
  string = alloca (len + 1);
  sprintf (string, "%s%s", prefix, name);

  DECL_SECTION_NAME (decl) = build_string (len, string);
}
