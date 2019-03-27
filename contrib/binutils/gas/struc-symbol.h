/* struct_symbol.h - Internal symbol structure
   Copyright 1987, 1992, 1993, 1994, 1995, 1998, 1999, 2000, 2001, 2005
   Free Software Foundation, Inc.

   This file is part of GAS, the GNU Assembler.

   GAS is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; either version 2, or (at your option)
   any later version.

   GAS is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with GAS; see the file COPYING.  If not, write to the Free
   Software Foundation, 51 Franklin Street - Fifth Floor, Boston, MA
   02110-1301, USA.  */

#ifndef __struc_symbol_h__
#define __struc_symbol_h__

/* The information we keep for a symbol.  Note that the symbol table
   holds pointers both to this and to local_symbol structures.  See
   below.  */

struct symbol
{
  /* BFD symbol */
  asymbol *bsym;

  /* The value of the symbol.  */
  expressionS sy_value;

  /* Forwards and (optionally) backwards chain pointers.  */
  struct symbol *sy_next;
  struct symbol *sy_previous;

  /* Pointer to the frag this symbol is attached to, if any.
     Otherwise, NULL.  */
  struct frag *sy_frag;

  unsigned int written : 1;
  /* Whether symbol value has been completely resolved (used during
     final pass over symbol table).  */
  unsigned int sy_resolved : 1;
  /* Whether the symbol value is currently being resolved (used to
     detect loops in symbol dependencies).  */
  unsigned int sy_resolving : 1;
  /* Whether the symbol value is used in a reloc.  This is used to
     ensure that symbols used in relocs are written out, even if they
     are local and would otherwise not be.  */
  unsigned int sy_used_in_reloc : 1;

  /* Whether the symbol is used as an operand or in an expression.
     NOTE:  Not all the backends keep this information accurate;
     backends which use this bit are responsible for setting it when
     a symbol is used in backend routines.  */
  unsigned int sy_used : 1;

  /* Whether the symbol can be re-defined.  */
  unsigned int sy_volatile : 1;

  /* Whether the symbol is a forward reference.  */
  unsigned int sy_forward_ref : 1;

  /* This is set if the symbol is defined in an MRI common section.
     We handle such sections as single common symbols, so symbols
     defined within them must be treated specially by the relocation
     routines.  */
  unsigned int sy_mri_common : 1;

  /* This is set if the symbol is set with a .weakref directive.  */
  unsigned int sy_weakrefr : 1;

  /* This is set when the symbol is referenced as part of a .weakref
     directive, but only if the symbol was not in the symbol table
     before.  It is cleared as soon as any direct reference to the
     symbol is present.  */
  unsigned int sy_weakrefd : 1;

#ifdef OBJ_SYMFIELD_TYPE
  OBJ_SYMFIELD_TYPE sy_obj;
#endif

#ifdef TC_SYMFIELD_TYPE
  TC_SYMFIELD_TYPE sy_tc;
#endif

#ifdef TARGET_SYMBOL_FIELDS
  TARGET_SYMBOL_FIELDS
#endif
};

/* A pointer in the symbol may point to either a complete symbol
   (struct symbol above) or to a local symbol (struct local_symbol
   defined here).  The symbol code can detect the case by examining
   the first field.  It is always NULL for a local symbol.

   We do this because we ordinarily only need a small amount of
   information for a local symbol.  The symbol table takes up a lot of
   space, and storing less information for a local symbol can make a
   big difference in assembler memory usage when assembling a large
   file.  */

struct local_symbol
{
  /* This pointer is always NULL to indicate that this is a local
     symbol.  */
  asymbol *lsy_marker;

  /* The symbol section.  This also serves as a flag.  If this is
     reg_section, then this symbol has been converted into a regular
     symbol, and lsy_sym points to it.  */
  segT lsy_section;

  /* The symbol name.  */
  const char *lsy_name;

  /* The symbol frag or the real symbol, depending upon the value in
     lsy_section.  If the symbol has been fully resolved, lsy_frag is
     set to NULL.  */
  union
  {
    fragS *lsy_frag;
    symbolS *lsy_sym;
  } u;

  /* The value of the symbol.  */
  valueT lsy_value;

#ifdef TC_LOCAL_SYMFIELD_TYPE
  TC_LOCAL_SYMFIELD_TYPE lsy_tc;
#endif
};

#define local_symbol_converted_p(l) ((l)->lsy_section == reg_section)
#define local_symbol_mark_converted(l) ((l)->lsy_section = reg_section)
#define local_symbol_resolved_p(l) ((l)->u.lsy_frag == NULL)
#define local_symbol_mark_resolved(l) ((l)->u.lsy_frag = NULL)
#define local_symbol_get_frag(l) ((l)->u.lsy_frag)
#define local_symbol_set_frag(l, f) ((l)->u.lsy_frag = (f))
#define local_symbol_get_real_symbol(l) ((l)->u.lsy_sym)
#define local_symbol_set_real_symbol(l, s) ((l)->u.lsy_sym = (s))

#endif /* __struc_symbol_h__ */
