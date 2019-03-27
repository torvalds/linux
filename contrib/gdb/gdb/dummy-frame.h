/* Code dealing with dummy stack frames, for GDB, the GNU debugger.

   Copyright 2002 Free Software Foundation, Inc.

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

#if !defined (DUMMY_FRAME_H)
#define DUMMY_FRAME_H 1

struct frame_info;
struct regcache;
struct frame_unwind;
struct frame_id;

/* GENERIC DUMMY FRAMES
  
   The following code serves to maintain the dummy stack frames for
   inferior function calls (ie. when gdb calls into the inferior via
   call_function_by_hand).  This code saves the machine state before
   the call in host memory, so we must maintain an independent stack
   and keep it consistant etc.  I am attempting to make this code
   generic enough to be used by many targets.
 
   The cheapest and most generic way to do CALL_DUMMY on a new target
   is probably to define CALL_DUMMY to be empty,
   DEPRECATED_CALL_DUMMY_LENGTH to zero, and CALL_DUMMY_LOCATION to
   AT_ENTRY.  Then you must remember to define PUSH_RETURN_ADDRESS,
   because no call instruction will be being executed by the target.
   Also DEPRECATED_FRAME_CHAIN_VALID as
   generic_{file,func}_frame_chain_valid and do not set
   DEPRECATED_FIX_CALL_DUMMY.  */

/* If the PC falls in a dummy frame, return a dummy frame
   unwinder.  */

extern const struct frame_unwind *dummy_frame_sniffer (struct frame_info *next_frame);

/* Does the PC fall in a dummy frame?

   This function is used by "frame.c" when creating a new `struct
   frame_info'.

   Note that there is also very similar code in breakpoint.c (where
   the bpstat stop reason is computed).  It is looking for a PC
   falling on a dummy_frame breakpoint.  Perhaphs this, and that code
   should be combined?

   Architecture dependant code, that has access to a frame, should not
   use this function.  Instead (get_frame_type() == DUMMY_FRAME)
   should be used.

   Hmm, but what about threads?  When the dummy-frame code tries to
   relocate a dummy frame's saved registers it definitly needs to
   differentiate between threads (otherwize it will do things like
   clean-up the wrong threads frames).  However, when just trying to
   identify a dummy-frame that shouldn't matter.  The wost that can
   happen is that a thread is marked as sitting in a dummy frame when,
   in reality, its corrupted its stack, to the point that a PC is
   pointing into a dummy frame.  */

extern int pc_in_dummy_frame (CORE_ADDR pc);

/* Return the regcache that belongs to the dummy-frame identifed by PC
   and FP, or NULL if no such frame exists.  */
/* FIXME: cagney/2002-11-08: The function only exists because of
   deprecated_generic_get_saved_register.  Eliminate that function and
   this, to, can go.  */

extern struct regcache *deprecated_find_dummy_frame_regcache (CORE_ADDR pc,
							      CORE_ADDR fp);
#endif /* !defined (DUMMY_FRAME_H)  */
