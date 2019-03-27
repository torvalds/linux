/* dwarf2dbg.h - DWARF2 debug support
   Copyright 1999, 2000, 2002, 2003 Free Software Foundation, Inc.

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

#ifndef AS_DWARF2DBG_H
#define AS_DWARF2DBG_H

#include "as.h"

#define DWARF2_FLAG_IS_STMT		(1 << 0)
#define DWARF2_FLAG_BASIC_BLOCK		(1 << 1)
#define DWARF2_FLAG_PROLOGUE_END	(1 << 2)
#define DWARF2_FLAG_EPILOGUE_BEGIN	(1 << 3)

struct dwarf2_line_info {
  unsigned int filenum;
  unsigned int line;
  unsigned int column;
  unsigned int isa;
  unsigned int flags;
};

/* Implements the .file FILENO "FILENAME" directive.  FILENO can be 0
   to indicate that no file number has been assigned.  All real file
   number must be >0.  */
extern char *dwarf2_directive_file (int dummy);

/* Implements the .loc FILENO LINENO [COLUMN] directive.  FILENO is
   the file number, LINENO the line number and the (optional) COLUMN
   the column of the source code that the following instruction
   corresponds to.  FILENO can be 0 to indicate that the filename
   specified by the textually most recent .file directive should be
   used.  */
extern void dwarf2_directive_loc (int dummy);

/* Implements the .loc_mark_labels {0,1} directive.  */
extern void dwarf2_directive_loc_mark_labels (int dummy);

/* Returns the current source information.  If .file directives have
   been encountered, the info for the corresponding source file is
   returned.  Otherwise, the info for the assembly source file is
   returned.  */
extern void dwarf2_where (struct dwarf2_line_info *l);

/* A hook to allow the target backend to inform the line number state 
   machine of isa changes when assembler debug info is enabled.  */
extern void dwarf2_set_isa (unsigned int isa);

/* This function generates .debug_line info based on the address and
   source information passed in the arguments.  ADDR should be the
   frag-relative offset of the instruction the information is for and
   L is the source information that should be associated with that
   address.  */
extern void dwarf2_gen_line_info (addressT addr, struct dwarf2_line_info *l);

/* Must be called for each generated instruction.  */
extern void dwarf2_emit_insn (int);

/* Should be called for each code label.  */
extern void dwarf2_emit_label (symbolS *);

/* True when we're supposed to set the basic block mark whenever a label
   is seen.  Unless the target is doing Something Weird, just call 
   dwarf2_emit_label.  */
bfd_boolean dwarf2_loc_mark_labels;

extern void dwarf2_finish (void);

extern int dwarf2dbg_estimate_size_before_relax (fragS *);
extern int dwarf2dbg_relax_frag (fragS *);
extern void dwarf2dbg_convert_frag (fragS *);

/* An enumeration which describes the sizes of offsets (to DWARF sections)
   and the mechanism by which the size is indicated.  */
enum dwarf2_format {
  /* 32-bit format: the initial length field is 4 bytes long.  */
  dwarf2_format_32bit,
  /* DWARF3 64-bit format: the representation of the initial length
     (of a DWARF section) is 0xffffffff (4 bytes) followed by eight
     bytes indicating the actual length.  */
  dwarf2_format_64bit,
  /* SGI extension to DWARF2: The initial length is eight bytes.  */
  dwarf2_format_64bit_irix
};

#endif /* AS_DWARF2DBG_H */
