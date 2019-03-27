/* Definitions for a frame unwinder, for GDB, the GNU debugger.

   Copyright 2003 Free Software Foundation, Inc.

   This file is part of GDB.

   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; either version 2 of the License, or
   (at your option) any later version.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program; if not, write to the Free Software
   Foundation, Inc., 59 Temple Place - Suite 330,
   Boston, MA 02111-1307, USA.  */

#if !defined (FRAME_UNWIND_H)
#define FRAME_UNWIND_H 1

struct frame_info;
struct frame_id;
struct frame_unwind;
struct gdbarch;
struct regcache;

#include "frame.h"		/* For enum frame_type.  */

/* The following unwind functions assume a chain of frames forming the
   sequence: (outer) prev <-> this <-> next (inner).  All the
   functions are called with called with the next frame's `struct
   frame_info' and and this frame's prologue cache.

   THIS frame's register values can be obtained by unwinding NEXT
   frame's registers (a recursive operation).

   THIS frame's prologue cache can be used to cache information such
   as where this frame's prologue stores the previous frame's
   registers.  */

/* Assuming the frame chain: (outer) prev <-> this <-> next (inner);
   use the NEXT frame, and its register unwind method, to determine
   the frame ID of THIS frame.

   A frame ID provides an invariant that can be used to re-identify an
   instance of a frame.  It is a combination of the frame's `base' and
   the frame's function's code address.

   Traditionally, THIS frame's ID was determined by examining THIS
   frame's function's prologue, and identifying the register/offset
   used as THIS frame's base.

   Example: An examination of THIS frame's prologue reveals that, on
   entry, it saves the PC(+12), SP(+8), and R1(+4) registers
   (decrementing the SP by 12).  Consequently, the frame ID's base can
   be determined by adding 12 to the THIS frame's stack-pointer, and
   the value of THIS frame's SP can be obtained by unwinding the NEXT
   frame's SP.

   THIS_PROLOGUE_CACHE can be used to share any prolog analysis data
   with the other unwind methods.  Memory for that cache should be
   allocated using frame_obstack_zalloc().  */

typedef void (frame_this_id_ftype) (struct frame_info *next_frame,
				    void **this_prologue_cache,
				    struct frame_id *this_id);

/* Assuming the frame chain: (outer) prev <-> this <-> next (inner);
   use the NEXT frame, and its register unwind method, to unwind THIS
   frame's registers (returning the value of the specified register
   REGNUM in the previous frame).

   Traditionally, THIS frame's registers were unwound by examining
   THIS frame's function's prologue and identifying which registers
   that prolog code saved on the stack.

   Example: An examination of THIS frame's prologue reveals that, on
   entry, it saves the PC(+12), SP(+8), and R1(+4) registers
   (decrementing the SP by 12).  Consequently, the value of the PC
   register in the previous frame is found in memory at SP+12, and
   THIS frame's SP can be obtained by unwinding the NEXT frame's SP.

   Why not pass in THIS_FRAME?  By passing in NEXT frame and THIS
   cache, the supplied parameters are consistent with the sibling
   function THIS_ID.

   Can the code call ``frame_register (get_prev_frame (NEXT_FRAME))''?
   Won't the call frame_register (THIS_FRAME) be faster?  Well,
   ignoring the possability that the previous frame does not yet
   exist, the ``frame_register (FRAME)'' function is expanded to
   ``frame_register_unwind (get_next_frame (FRAME)'' and hence that
   call will expand to ``frame_register_unwind (get_next_frame
   (get_prev_frame (NEXT_FRAME)))''.  Might as well call
   ``frame_register_unwind (NEXT_FRAME)'' directly.

   THIS_PROLOGUE_CACHE can be used to share any prolog analysis data
   with the other unwind methods.  Memory for that cache should be
   allocated using frame_obstack_zalloc().  */

typedef void (frame_prev_register_ftype) (struct frame_info *next_frame,
					  void **this_prologue_cache,
					  int prev_regnum,
					  int *optimized,
					  enum lval_type * lvalp,
					  CORE_ADDR *addrp,
					  int *realnump, void *valuep);

struct frame_unwind
{
  /* The frame's type.  Should this instead be a collection of
     predicates that test the frame for various attributes?  */
  enum frame_type type;
  /* Should an attribute indicating the frame's address-in-block go
     here?  */
  frame_this_id_ftype *this_id;
  frame_prev_register_ftype *prev_register;
};

/* Given the NEXT frame, take a wiff of THIS frame's registers (namely
   the PC and attributes) and if it is the applicable unwinder return
   the unwind methods, or NULL if it is not.  */

typedef const struct frame_unwind *(frame_unwind_sniffer_ftype) (struct frame_info *next_frame);

/* Add a frame sniffer to the list.  The predicates are polled in the
   order that they are appended.  The initial list contains the dummy
   frame sniffer.  */

extern void frame_unwind_append_sniffer (struct gdbarch *gdbarch,
					 frame_unwind_sniffer_ftype *sniffer);

/* Iterate through the next frame's sniffers until one returns with an
   unwinder implementation.  */

extern const struct frame_unwind *frame_unwind_find_by_frame (struct frame_info *next_frame);

#endif
