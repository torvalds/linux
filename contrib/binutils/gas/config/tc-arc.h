/* tc-arc.h - Macros and type defines for the ARC.
   Copyright 1994, 1995, 1997, 2000, 2001, 2002, 2005
   Free Software Foundation, Inc.
   Contributed by Doug Evans (dje@cygnus.com).

   This file is part of GAS, the GNU Assembler.

   GAS is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as
   published by the Free Software Foundation; either version 2,
   or (at your option) any later version.

   GAS is distributed in the hope that it will be useful, but
   WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See
   the GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with GAS; see the file COPYING.  If not, write to the Free
   Software Foundation, 51 Franklin Street - Fifth Floor, Boston, MA
   02110-1301, USA.  */

#define TC_ARC 1

#define TARGET_BYTES_BIG_ENDIAN 0

#define LOCAL_LABELS_FB 1

#define TARGET_ARCH bfd_arch_arc

#define DIFF_EXPR_OK
#define REGISTER_PREFIX '%'

#ifdef LITTLE_ENDIAN
#undef LITTLE_ENDIAN
#endif

#ifdef BIG_ENDIAN
#undef BIG_ENDIAN
#endif

#define LITTLE_ENDIAN   1234

#define BIG_ENDIAN      4321

/* The endianness of the target format may change based on command
   line arguments.  */
extern const char * arc_target_format;

#define DEFAULT_TARGET_FORMAT  "elf32-littlearc"
#define TARGET_FORMAT          arc_target_format
#define DEFAULT_BYTE_ORDER     LITTLE_ENDIAN
#define WORKING_DOT_WORD
#define LISTING_HEADER         "ARC GAS "

/* The ARC needs to parse reloc specifiers in .word.  */

extern void arc_parse_cons_expression (struct expressionS *, unsigned);
#define TC_PARSE_CONS_EXPRESSION(EXP, NBYTES) \
  arc_parse_cons_expression (EXP, NBYTES)

extern void arc_cons_fix_new (struct frag *, int, int, struct expressionS *);
#define TC_CONS_FIX_NEW(FRAG, WHERE, NBYTES, EXP) \
  arc_cons_fix_new (FRAG, WHERE, NBYTES, EXP)

#define DWARF2_LINE_MIN_INSN_LENGTH 4

/* Values passed to md_apply_fix don't include the symbol value.  */
#define MD_APPLY_SYM_VALUE(FIX) 0

/* No shared lib support, so we don't need to ensure externally
   visible symbols can be overridden.  */
#define EXTERN_FORCE_RELOC 0
