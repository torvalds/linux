/* Cache and manage frames for GDB, the GNU debugger.

   Copyright 1986, 1987, 1989, 1991, 1994, 1995, 1996, 1998, 2000,
   2001, 2002, 2003, 2004 Free Software Foundation, Inc.

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

#include "defs.h"
#include "frame.h"
#include "target.h"
#include "value.h"
#include "inferior.h"	/* for inferior_ptid */
#include "regcache.h"
#include "gdb_assert.h"
#include "gdb_string.h"
#include "user-regs.h"
#include "gdb_obstack.h"
#include "dummy-frame.h"
#include "sentinel-frame.h"
#include "gdbcore.h"
#include "annotate.h"
#include "language.h"
#include "frame-unwind.h"
#include "frame-base.h"
#include "command.h"
#include "gdbcmd.h"

/* We keep a cache of stack frames, each of which is a "struct
   frame_info".  The innermost one gets allocated (in
   wait_for_inferior) each time the inferior stops; current_frame
   points to it.  Additional frames get allocated (in get_prev_frame)
   as needed, and are chained through the next and prev fields.  Any
   time that the frame cache becomes invalid (most notably when we
   execute something, but also if we change how we interpret the
   frames (e.g. "set heuristic-fence-post" in mips-tdep.c, or anything
   which reads new symbols)), we should call reinit_frame_cache.  */

struct frame_info
{
  /* Level of this frame.  The inner-most (youngest) frame is at level
     0.  As you move towards the outer-most (oldest) frame, the level
     increases.  This is a cached value.  It could just as easily be
     computed by counting back from the selected frame to the inner
     most frame.  */
  /* NOTE: cagney/2002-04-05: Perhaphs a level of ``-1'' should be
     reserved to indicate a bogus frame - one that has been created
     just to keep GDB happy (GDB always needs a frame).  For the
     moment leave this as speculation.  */
  int level;

  /* The frame's type.  */
  /* FIXME: cagney/2003-04-02: Should instead be returning
     ->unwind->type.  Unfortunately, legacy code is still explicitly
     setting the type using the method deprecated_set_frame_type.
     Eliminate that method and this field can be eliminated.  */
  enum frame_type type;

  /* For each register, address of where it was saved on entry to the
     frame, or zero if it was not saved on entry to this frame.  This
     includes special registers such as pc and fp saved in special
     ways in the stack frame.  The SP_REGNUM is even more special, the
     address here is the sp for the previous frame, not the address
     where the sp was saved.  */
  /* Allocated by frame_saved_regs_zalloc () which is called /
     initialized by DEPRECATED_FRAME_INIT_SAVED_REGS(). */
  CORE_ADDR *saved_regs;	/*NUM_REGS + NUM_PSEUDO_REGS*/

  /* Anything extra for this structure that may have been defined in
     the machine dependent files. */
  /* Allocated by frame_extra_info_zalloc () which is called /
     initialized by DEPRECATED_INIT_EXTRA_FRAME_INFO */
  struct frame_extra_info *extra_info;

  /* The frame's low-level unwinder and corresponding cache.  The
     low-level unwinder is responsible for unwinding register values
     for the previous frame.  The low-level unwind methods are
     selected based on the presence, or otherwize, of register unwind
     information such as CFI.  */
  void *prologue_cache;
  const struct frame_unwind *unwind;

  /* Cached copy of the previous frame's resume address.  */
  struct {
    int p;
    CORE_ADDR value;
  } prev_pc;
  
  /* Cached copy of the previous frame's function address.  */
  struct
  {
    CORE_ADDR addr;
    int p;
  } prev_func;
  
  /* This frame's ID.  */
  struct
  {
    int p;
    struct frame_id value;
  } this_id;
  
  /* The frame's high-level base methods, and corresponding cache.
     The high level base methods are selected based on the frame's
     debug info.  */
  const struct frame_base *base;
  void *base_cache;

  /* Pointers to the next (down, inner, younger) and previous (up,
     outer, older) frame_info's in the frame cache.  */
  struct frame_info *next; /* down, inner, younger */
  int prev_p;
  struct frame_info *prev; /* up, outer, older */
};

/* Flag to control debugging.  */

static int frame_debug;

/* Flag to indicate whether backtraces should stop at main et.al.  */

static int backtrace_past_main;
static unsigned int backtrace_limit = UINT_MAX;

int (*frame_tdep_pc_fixup)(CORE_ADDR *pc);

void
fprint_frame_id (struct ui_file *file, struct frame_id id)
{
  fprintf_unfiltered (file, "{stack=0x%s,code=0x%s,special=0x%s}",
		      paddr_nz (id.stack_addr),
		      paddr_nz (id.code_addr),
		      paddr_nz (id.special_addr));
}

static void
fprint_frame_type (struct ui_file *file, enum frame_type type)
{
  switch (type)
    {
    case UNKNOWN_FRAME:
      fprintf_unfiltered (file, "UNKNOWN_FRAME");
      return;
    case NORMAL_FRAME:
      fprintf_unfiltered (file, "NORMAL_FRAME");
      return;
    case DUMMY_FRAME:
      fprintf_unfiltered (file, "DUMMY_FRAME");
      return;
    case SIGTRAMP_FRAME:
      fprintf_unfiltered (file, "SIGTRAMP_FRAME");
      return;
    default:
      fprintf_unfiltered (file, "<unknown type>");
      return;
    };
}

static void
fprint_frame (struct ui_file *file, struct frame_info *fi)
{
  if (fi == NULL)
    {
      fprintf_unfiltered (file, "<NULL frame>");
      return;
    }
  fprintf_unfiltered (file, "{");
  fprintf_unfiltered (file, "level=%d", fi->level);
  fprintf_unfiltered (file, ",");
  fprintf_unfiltered (file, "type=");
  fprint_frame_type (file, fi->type);
  fprintf_unfiltered (file, ",");
  fprintf_unfiltered (file, "unwind=");
  if (fi->unwind != NULL)
    gdb_print_host_address (fi->unwind, file);
  else
    fprintf_unfiltered (file, "<unknown>");
  fprintf_unfiltered (file, ",");
  fprintf_unfiltered (file, "pc=");
  if (fi->next != NULL && fi->next->prev_pc.p)
    fprintf_unfiltered (file, "0x%s", paddr_nz (fi->next->prev_pc.value));
  else
    fprintf_unfiltered (file, "<unknown>");
  fprintf_unfiltered (file, ",");
  fprintf_unfiltered (file, "id=");
  if (fi->this_id.p)
    fprint_frame_id (file, fi->this_id.value);
  else
    fprintf_unfiltered (file, "<unknown>");
  fprintf_unfiltered (file, ",");
  fprintf_unfiltered (file, "func=");
  if (fi->next != NULL && fi->next->prev_func.p)
    fprintf_unfiltered (file, "0x%s", paddr_nz (fi->next->prev_func.addr));
  else
    fprintf_unfiltered (file, "<unknown>");
  fprintf_unfiltered (file, "}");
}

/* Return a frame uniq ID that can be used to, later, re-find the
   frame.  */

struct frame_id
get_frame_id (struct frame_info *fi)
{
  if (fi == NULL)
    {
      return null_frame_id;
    }
  if (!fi->this_id.p)
    {
      gdb_assert (!legacy_frame_p (current_gdbarch));
      if (frame_debug)
	fprintf_unfiltered (gdb_stdlog, "{ get_frame_id (fi=%d) ",
			    fi->level);
      /* Find the unwinder.  */
      if (fi->unwind == NULL)
	{
	  fi->unwind = frame_unwind_find_by_frame (fi->next);
	  /* FIXME: cagney/2003-04-02: Rather than storing the frame's
	     type in the frame, the unwinder's type should be returned
	     directly.  Unfortunately, legacy code, called by
	     legacy_get_prev_frame, explicitly set the frames type
	     using the method deprecated_set_frame_type().  */
	  fi->type = fi->unwind->type;
	}
      /* Find THIS frame's ID.  */
      fi->unwind->this_id (fi->next, &fi->prologue_cache, &fi->this_id.value);
      fi->this_id.p = 1;
      if (frame_debug)
	{
	  fprintf_unfiltered (gdb_stdlog, "-> ");
	  fprint_frame_id (gdb_stdlog, fi->this_id.value);
	  fprintf_unfiltered (gdb_stdlog, " }\n");
	}
    }
  return fi->this_id.value;
}

const struct frame_id null_frame_id; /* All zeros.  */

struct frame_id
frame_id_build_special (CORE_ADDR stack_addr, CORE_ADDR code_addr,
                        CORE_ADDR special_addr)
{
  struct frame_id id;
  id.stack_addr = stack_addr;
  id.code_addr = code_addr;
  id.special_addr = special_addr;
  return id;
}

struct frame_id
frame_id_build (CORE_ADDR stack_addr, CORE_ADDR code_addr)
{
  return frame_id_build_special (stack_addr, code_addr, 0);
}

int
frame_id_p (struct frame_id l)
{
  int p;
  /* The .code can be NULL but the .stack cannot.  */
  p = (l.stack_addr != 0);
  if (frame_debug)
    {
      fprintf_unfiltered (gdb_stdlog, "{ frame_id_p (l=");
      fprint_frame_id (gdb_stdlog, l);
      fprintf_unfiltered (gdb_stdlog, ") -> %d }\n", p);
    }
  return p;
}

int
frame_id_eq (struct frame_id l, struct frame_id r)
{
  int eq;
  if (l.stack_addr == 0 || r.stack_addr == 0)
    /* Like a NaN, if either ID is invalid, the result is false.  */
    eq = 0;
  else if (l.stack_addr != r.stack_addr)
    /* If .stack addresses are different, the frames are different.  */
    eq = 0;
  else if (l.code_addr == 0 || r.code_addr == 0)
    /* A zero code addr is a wild card, always succeed.  */
    eq = 1;
  else if (l.code_addr != r.code_addr)
    /* If .code addresses are different, the frames are different.  */
    eq = 0;
  else if (l.special_addr == 0 || r.special_addr == 0)
    /* A zero special addr is a wild card (or unused), always succeed.  */
    eq = 1;
  else if (l.special_addr == r.special_addr)
    /* Frames are equal.  */
    eq = 1;
  else
    /* No luck.  */
    eq = 0;
  if (frame_debug)
    {
      fprintf_unfiltered (gdb_stdlog, "{ frame_id_eq (l=");
      fprint_frame_id (gdb_stdlog, l);
      fprintf_unfiltered (gdb_stdlog, ",r=");
      fprint_frame_id (gdb_stdlog, r);
      fprintf_unfiltered (gdb_stdlog, ") -> %d }\n", eq);
    }
  return eq;
}

int
frame_id_inner (struct frame_id l, struct frame_id r)
{
  int inner;
  if (l.stack_addr == 0 || r.stack_addr == 0)
    /* Like NaN, any operation involving an invalid ID always fails.  */
    inner = 0;
  else
    /* Only return non-zero when strictly inner than.  Note that, per
       comment in "frame.h", there is some fuzz here.  Frameless
       functions are not strictly inner than (same .stack but
       different .code and/or .special address).  */
    inner = INNER_THAN (l.stack_addr, r.stack_addr);
  if (frame_debug)
    {
      fprintf_unfiltered (gdb_stdlog, "{ frame_id_inner (l=");
      fprint_frame_id (gdb_stdlog, l);
      fprintf_unfiltered (gdb_stdlog, ",r=");
      fprint_frame_id (gdb_stdlog, r);
      fprintf_unfiltered (gdb_stdlog, ") -> %d }\n", inner);
    }
  return inner;
}

struct frame_info *
frame_find_by_id (struct frame_id id)
{
  struct frame_info *frame;

  /* ZERO denotes the null frame, let the caller decide what to do
     about it.  Should it instead return get_current_frame()?  */
  if (!frame_id_p (id))
    return NULL;

  for (frame = get_current_frame ();
       frame != NULL;
       frame = get_prev_frame (frame))
    {
      struct frame_id this = get_frame_id (frame);
      if (frame_id_eq (id, this))
	/* An exact match.  */
	return frame;
      if (frame_id_inner (id, this))
	/* Gone to far.  */
	return NULL;
      /* Either, we're not yet gone far enough out along the frame
         chain (inner(this,id), or we're comparing frameless functions
         (same .base, different .func, no test available).  Struggle
         on until we've definitly gone to far.  */
    }
  return NULL;
}

CORE_ADDR
frame_pc_unwind (struct frame_info *this_frame)
{
  if (!this_frame->prev_pc.p)
    {
      CORE_ADDR pc;
      if (gdbarch_unwind_pc_p (current_gdbarch))
	{
	  /* The right way.  The `pure' way.  The one true way.  This
	     method depends solely on the register-unwind code to
	     determine the value of registers in THIS frame, and hence
	     the value of this frame's PC (resume address).  A typical
	     implementation is no more than:
	   
	     frame_unwind_register (this_frame, ISA_PC_REGNUM, buf);
	     return extract_unsigned_integer (buf, size of ISA_PC_REGNUM);

	     Note: this method is very heavily dependent on a correct
	     register-unwind implementation, it pays to fix that
	     method first; this method is frame type agnostic, since
	     it only deals with register values, it works with any
	     frame.  This is all in stark contrast to the old
	     FRAME_SAVED_PC which would try to directly handle all the
	     different ways that a PC could be unwound.  */
	  pc = gdbarch_unwind_pc (current_gdbarch, this_frame);
	}
      else if (this_frame->level < 0)
	{
	  /* FIXME: cagney/2003-03-06: Old code and and a sentinel
             frame.  Do like was always done.  Fetch the PC's value
             direct from the global registers array (via read_pc).
             This assumes that this frame belongs to the current
             global register cache.  The assumption is dangerous.  */
	  pc = read_pc ();
	}
      else if (DEPRECATED_FRAME_SAVED_PC_P ())
	{
	  /* FIXME: cagney/2003-03-06: Old code, but not a sentinel
             frame.  Do like was always done.  Note that this method,
             unlike unwind_pc(), tries to handle all the different
             frame cases directly.  It fails.  */
	  pc = DEPRECATED_FRAME_SAVED_PC (this_frame);
	}
      else
	internal_error (__FILE__, __LINE__, "No gdbarch_unwind_pc method");
      this_frame->prev_pc.value = pc;
      this_frame->prev_pc.p = 1;
      if (frame_debug)
	fprintf_unfiltered (gdb_stdlog,
			    "{ frame_pc_unwind (this_frame=%d) -> 0x%s }\n",
			    this_frame->level,
			    paddr_nz (this_frame->prev_pc.value));
    }
  return this_frame->prev_pc.value;
}

CORE_ADDR
frame_func_unwind (struct frame_info *fi)
{
  if (!fi->prev_func.p)
    {
      /* Make certain that this, and not the adjacent, function is
         found.  */
      CORE_ADDR addr_in_block = frame_unwind_address_in_block (fi);
      fi->prev_func.p = 1;
      fi->prev_func.addr = get_pc_function_start (addr_in_block);
      if (frame_debug)
	fprintf_unfiltered (gdb_stdlog,
			    "{ frame_func_unwind (fi=%d) -> 0x%s }\n",
			    fi->level, paddr_nz (fi->prev_func.addr));
    }
  return fi->prev_func.addr;
}

CORE_ADDR
get_frame_func (struct frame_info *fi)
{
  return frame_func_unwind (fi->next);
}

static int
do_frame_unwind_register (void *src, int regnum, void *buf)
{
  frame_unwind_register (src, regnum, buf);
  return 1;
}

void
frame_pop (struct frame_info *this_frame)
{
  struct regcache *scratch_regcache;
  struct cleanup *cleanups;

  if (DEPRECATED_POP_FRAME_P ())
    {
      /* A legacy architecture that has implemented a custom pop
	 function.  All new architectures should instead be using the
	 generic code below.  */
      DEPRECATED_POP_FRAME;
    }
  else
    {
      /* Make a copy of all the register values unwound from this
	 frame.  Save them in a scratch buffer so that there isn't a
	 race betweening trying to extract the old values from the
	 current_regcache while, at the same time writing new values
	 into that same cache.  */
      struct regcache *scratch = regcache_xmalloc (current_gdbarch);
      struct cleanup *cleanups = make_cleanup_regcache_xfree (scratch);
      regcache_save (scratch, do_frame_unwind_register, this_frame);
      /* FIXME: cagney/2003-03-16: It should be possible to tell the
         target's register cache that it is about to be hit with a
         burst register transfer and that the sequence of register
         writes should be batched.  The pair target_prepare_to_store()
         and target_store_registers() kind of suggest this
         functionality.  Unfortunately, they don't implement it.  Their
         lack of a formal definition can lead to targets writing back
         bogus values (arguably a bug in the target code mind).  */
      /* Now copy those saved registers into the current regcache.
         Here, regcache_cpy() calls regcache_restore().  */
      regcache_cpy (current_regcache, scratch);
      do_cleanups (cleanups);
    }
  /* We've made right mess of GDB's local state, just discard
     everything.  */
  flush_cached_frames ();
}

void
frame_register_unwind (struct frame_info *frame, int regnum,
		       int *optimizedp, enum lval_type *lvalp,
		       CORE_ADDR *addrp, int *realnump, void *bufferp)
{
  struct frame_unwind_cache *cache;

  if (frame_debug)
    {
      fprintf_unfiltered (gdb_stdlog, "\
{ frame_register_unwind (frame=%d,regnum=%d(%s),...) ",
			  frame->level, regnum,
			  frame_map_regnum_to_name (frame, regnum));
    }

  /* Require all but BUFFERP to be valid.  A NULL BUFFERP indicates
     that the value proper does not need to be fetched.  */
  gdb_assert (optimizedp != NULL);
  gdb_assert (lvalp != NULL);
  gdb_assert (addrp != NULL);
  gdb_assert (realnump != NULL);
  /* gdb_assert (bufferp != NULL); */

  /* NOTE: cagney/2002-11-27: A program trying to unwind a NULL frame
     is broken.  There is always a frame.  If there, for some reason,
     isn't, there is some pretty busted code as it should have
     detected the problem before calling here.  */
  gdb_assert (frame != NULL);

  /* Find the unwinder.  */
  if (frame->unwind == NULL)
    {
      frame->unwind = frame_unwind_find_by_frame (frame->next);
      /* FIXME: cagney/2003-04-02: Rather than storing the frame's
	 type in the frame, the unwinder's type should be returned
	 directly.  Unfortunately, legacy code, called by
	 legacy_get_prev_frame, explicitly set the frames type using
	 the method deprecated_set_frame_type().  */
      frame->type = frame->unwind->type;
    }

  /* Ask this frame to unwind its register.  See comment in
     "frame-unwind.h" for why NEXT frame and this unwind cace are
     passed in.  */
  frame->unwind->prev_register (frame->next, &frame->prologue_cache, regnum,
				optimizedp, lvalp, addrp, realnump, bufferp);

  if (frame_debug)
    {
      fprintf_unfiltered (gdb_stdlog, "->");
      fprintf_unfiltered (gdb_stdlog, " *optimizedp=%d", (*optimizedp));
      fprintf_unfiltered (gdb_stdlog, " *lvalp=%d", (int) (*lvalp));
      fprintf_unfiltered (gdb_stdlog, " *addrp=0x%s", paddr_nz ((*addrp)));
      fprintf_unfiltered (gdb_stdlog, " *bufferp=");
      if (bufferp == NULL)
	fprintf_unfiltered (gdb_stdlog, "<NULL>");
      else
	{
	  int i;
	  const unsigned char *buf = bufferp;
	  fprintf_unfiltered (gdb_stdlog, "[");
	  for (i = 0; i < register_size (current_gdbarch, regnum); i++)
	    fprintf_unfiltered (gdb_stdlog, "%02x", buf[i]);
	  fprintf_unfiltered (gdb_stdlog, "]");
	}
      fprintf_unfiltered (gdb_stdlog, " }\n");
    }
}

void
frame_register (struct frame_info *frame, int regnum,
		int *optimizedp, enum lval_type *lvalp,
		CORE_ADDR *addrp, int *realnump, void *bufferp)
{
  /* Require all but BUFFERP to be valid.  A NULL BUFFERP indicates
     that the value proper does not need to be fetched.  */
  gdb_assert (optimizedp != NULL);
  gdb_assert (lvalp != NULL);
  gdb_assert (addrp != NULL);
  gdb_assert (realnump != NULL);
  /* gdb_assert (bufferp != NULL); */

  /* Ulgh!  Old code that, for lval_register, sets ADDRP to the offset
     of the register in the register cache.  It should instead return
     the REGNUM corresponding to that register.  Translate the .  */
  if (DEPRECATED_GET_SAVED_REGISTER_P ())
    {
      DEPRECATED_GET_SAVED_REGISTER (bufferp, optimizedp, addrp, frame,
				     regnum, lvalp);
      /* Compute the REALNUM if the caller wants it.  */
      if (*lvalp == lval_register)
	{
	  int regnum;
	  for (regnum = 0; regnum < NUM_REGS + NUM_PSEUDO_REGS; regnum++)
	    {
	      if (*addrp == register_offset_hack (current_gdbarch, regnum))
		{
		  *realnump = regnum;
		  return;
		}
	    }
	  internal_error (__FILE__, __LINE__,
			  "Failed to compute the register number corresponding"
			  " to 0x%s", paddr_d (*addrp));
	}
      *realnump = -1;
      return;
    }

  /* Obtain the register value by unwinding the register from the next
     (more inner frame).  */
  gdb_assert (frame != NULL && frame->next != NULL);
  frame_register_unwind (frame->next, regnum, optimizedp, lvalp, addrp,
			 realnump, bufferp);
}

void
frame_unwind_register (struct frame_info *frame, int regnum, void *buf)
{
  int optimized;
  CORE_ADDR addr;
  int realnum;
  enum lval_type lval;
  frame_register_unwind (frame, regnum, &optimized, &lval, &addr,
			 &realnum, buf);
}

void
get_frame_register (struct frame_info *frame,
		    int regnum, void *buf)
{
  frame_unwind_register (frame->next, regnum, buf);
}

LONGEST
frame_unwind_register_signed (struct frame_info *frame, int regnum)
{
  char buf[MAX_REGISTER_SIZE];
  frame_unwind_register (frame, regnum, buf);
  return extract_signed_integer (buf, DEPRECATED_REGISTER_VIRTUAL_SIZE (regnum));
}

LONGEST
get_frame_register_signed (struct frame_info *frame, int regnum)
{
  return frame_unwind_register_signed (frame->next, regnum);
}

ULONGEST
frame_unwind_register_unsigned (struct frame_info *frame, int regnum)
{
  char buf[MAX_REGISTER_SIZE];
  frame_unwind_register (frame, regnum, buf);
  return extract_unsigned_integer (buf, DEPRECATED_REGISTER_VIRTUAL_SIZE (regnum));
}

ULONGEST
get_frame_register_unsigned (struct frame_info *frame, int regnum)
{
  return frame_unwind_register_unsigned (frame->next, regnum);
}

void
frame_unwind_unsigned_register (struct frame_info *frame, int regnum,
				ULONGEST *val)
{
  char buf[MAX_REGISTER_SIZE];
  frame_unwind_register (frame, regnum, buf);
  (*val) = extract_unsigned_integer (buf, DEPRECATED_REGISTER_VIRTUAL_SIZE (regnum));
}

void
put_frame_register (struct frame_info *frame, int regnum, const void *buf)
{
  struct gdbarch *gdbarch = get_frame_arch (frame);
  int realnum;
  int optim;
  enum lval_type lval;
  CORE_ADDR addr;
  frame_register (frame, regnum, &optim, &lval, &addr, &realnum, NULL);
  if (optim)
    error ("Attempt to assign to a value that was optimized out.");
  switch (lval)
    {
    case lval_memory:
      {
	/* FIXME: write_memory doesn't yet take constant buffers.
           Arrrg!  */
	char tmp[MAX_REGISTER_SIZE];
	memcpy (tmp, buf, register_size (gdbarch, regnum));
	write_memory (addr, tmp, register_size (gdbarch, regnum));
	break;
      }
    case lval_register:
      regcache_cooked_write (current_regcache, realnum, buf);
      break;
    default:
      error ("Attempt to assign to an unmodifiable value.");
    }
}

/* frame_register_read ()

   Find and return the value of REGNUM for the specified stack frame.
   The number of bytes copied is DEPRECATED_REGISTER_RAW_SIZE
   (REGNUM).

   Returns 0 if the register value could not be found.  */

int
frame_register_read (struct frame_info *frame, int regnum, void *myaddr)
{
  int optimized;
  enum lval_type lval;
  CORE_ADDR addr;
  int realnum;
  frame_register (frame, regnum, &optimized, &lval, &addr, &realnum, myaddr);

  /* FIXME: cagney/2002-05-15: This test, is just bogus.

     It indicates that the target failed to supply a value for a
     register because it was "not available" at this time.  Problem
     is, the target still has the register and so get saved_register()
     may be returning a value saved on the stack.  */

  if (register_cached (regnum) < 0)
    return 0;			/* register value not available */

  return !optimized;
}


/* Map between a frame register number and its name.  A frame register
   space is a superset of the cooked register space --- it also
   includes builtin registers.  */

int
frame_map_name_to_regnum (struct frame_info *frame, const char *name, int len)
{
  return user_reg_map_name_to_regnum (get_frame_arch (frame), name, len);
}

const char *
frame_map_regnum_to_name (struct frame_info *frame, int regnum)
{
  return user_reg_map_regnum_to_name (get_frame_arch (frame), regnum);
}

/* Create a sentinel frame.  */

static struct frame_info *
create_sentinel_frame (struct regcache *regcache)
{
  struct frame_info *frame = FRAME_OBSTACK_ZALLOC (struct frame_info);
  frame->type = NORMAL_FRAME;
  frame->level = -1;
  /* Explicitly initialize the sentinel frame's cache.  Provide it
     with the underlying regcache.  In the future additional
     information, such as the frame's thread will be added.  */
  frame->prologue_cache = sentinel_frame_cache (regcache);
  /* For the moment there is only one sentinel frame implementation.  */
  frame->unwind = sentinel_frame_unwind;
  /* Link this frame back to itself.  The frame is self referential
     (the unwound PC is the same as the pc), so make it so.  */
  frame->next = frame;
  /* Make the sentinel frame's ID valid, but invalid.  That way all
     comparisons with it should fail.  */
  frame->this_id.p = 1;
  frame->this_id.value = null_frame_id;
  if (frame_debug)
    {
      fprintf_unfiltered (gdb_stdlog, "{ create_sentinel_frame (...) -> ");
      fprint_frame (gdb_stdlog, frame);
      fprintf_unfiltered (gdb_stdlog, " }\n");
    }
  return frame;
}

/* Info about the innermost stack frame (contents of FP register) */

static struct frame_info *current_frame;

/* Cache for frame addresses already read by gdb.  Valid only while
   inferior is stopped.  Control variables for the frame cache should
   be local to this module.  */

static struct obstack frame_cache_obstack;

void *
frame_obstack_zalloc (unsigned long size)
{
  void *data = obstack_alloc (&frame_cache_obstack, size);
  memset (data, 0, size);
  return data;
}

CORE_ADDR *
frame_saved_regs_zalloc (struct frame_info *fi)
{
  fi->saved_regs = (CORE_ADDR *)
    frame_obstack_zalloc (SIZEOF_FRAME_SAVED_REGS);
  return fi->saved_regs;
}

CORE_ADDR *
deprecated_get_frame_saved_regs (struct frame_info *fi)
{
  return fi->saved_regs;
}

/* Return the innermost (currently executing) stack frame.  This is
   split into two functions.  The function unwind_to_current_frame()
   is wrapped in catch exceptions so that, even when the unwind of the
   sentinel frame fails, the function still returns a stack frame.  */

static int
unwind_to_current_frame (struct ui_out *ui_out, void *args)
{
  struct frame_info *frame = get_prev_frame (args);
  /* A sentinel frame can fail to unwind, eg, because it's PC value
     lands in somewhere like start.  */
  if (frame == NULL)
    return 1;
  current_frame = frame;
  return 0;
}

struct frame_info *
get_current_frame (void)
{
  /* First check, and report, the lack of registers.  Having GDB
     report "No stack!" or "No memory" when the target doesn't even
     have registers is very confusing.  Besides, "printcmd.exp"
     explicitly checks that ``print $pc'' with no registers prints "No
     registers".  */
  if (!target_has_registers)
    error ("No registers.");
  if (!target_has_stack)
    error ("No stack.");
  if (!target_has_memory)
    error ("No memory.");
  if (current_frame == NULL)
    {
      struct frame_info *sentinel_frame =
	create_sentinel_frame (current_regcache);
      if (catch_exceptions (uiout, unwind_to_current_frame, sentinel_frame,
			    NULL, RETURN_MASK_ERROR) != 0)
	{
	  /* Oops! Fake a current frame?  Is this useful?  It has a PC
             of zero, for instance.  */
	  current_frame = sentinel_frame;
	}
    }
  return current_frame;
}

/* The "selected" stack frame is used by default for local and arg
   access.  May be zero, for no selected frame.  */

struct frame_info *deprecated_selected_frame;

/* Return the selected frame.  Always non-null (unless there isn't an
   inferior sufficient for creating a frame) in which case an error is
   thrown.  */

struct frame_info *
get_selected_frame (void)
{
  if (deprecated_selected_frame == NULL)
    /* Hey!  Don't trust this.  It should really be re-finding the
       last selected frame of the currently selected thread.  This,
       though, is better than nothing.  */
    select_frame (get_current_frame ());
  /* There is always a frame.  */
  gdb_assert (deprecated_selected_frame != NULL);
  return deprecated_selected_frame;
}

/* This is a variant of get_selected_frame which can be called when
   the inferior does not have a frame; in that case it will return
   NULL instead of calling error ().  */

struct frame_info *
deprecated_safe_get_selected_frame (void)
{
  if (!target_has_registers || !target_has_stack || !target_has_memory)
    return NULL;
  return get_selected_frame ();
}

/* Select frame FI (or NULL - to invalidate the current frame).  */

void
select_frame (struct frame_info *fi)
{
  struct symtab *s;

  deprecated_selected_frame = fi;
  /* NOTE: cagney/2002-05-04: FI can be NULL.  This occures when the
     frame is being invalidated.  */
  if (selected_frame_level_changed_hook)
    selected_frame_level_changed_hook (frame_relative_level (fi));

  /* FIXME: kseitz/2002-08-28: It would be nice to call
     selected_frame_level_changed_event right here, but due to limitations
     in the current interfaces, we would end up flooding UIs with events
     because select_frame is used extensively internally.

     Once we have frame-parameterized frame (and frame-related) commands,
     the event notification can be moved here, since this function will only
     be called when the users selected frame is being changed. */

  /* Ensure that symbols for this frame are read in.  Also, determine the
     source language of this frame, and switch to it if desired.  */
  if (fi)
    {
      /* We retrieve the frame's symtab by using the frame PC.  However
         we cannot use the frame pc as is, because it usually points to
         the instruction following the "call", which is sometimes the
         first instruction of another function.  So we rely on
         get_frame_address_in_block() which provides us with a PC which
         is guaranteed to be inside the frame's code block.  */
      s = find_pc_symtab (get_frame_address_in_block (fi));
      if (s
	  && s->language != current_language->la_language
	  && s->language != language_unknown
	  && language_mode == language_mode_auto)
	{
	  set_language (s->language);
	}
    }
}

/* Return the register saved in the simplistic ``saved_regs'' cache.
   If the value isn't here AND a value is needed, try the next inner
   most frame.  */

static void
legacy_saved_regs_prev_register (struct frame_info *next_frame,
				 void **this_prologue_cache,
				 int regnum, int *optimizedp,
				 enum lval_type *lvalp, CORE_ADDR *addrp,
				 int *realnump, void *bufferp)
{
  /* HACK: New code is passed the next frame and this cache.
     Unfortunately, old code expects this frame.  Since this is a
     backward compatibility hack, cheat by walking one level along the
     prologue chain to the frame the old code expects.

     Do not try this at home.  Professional driver, closed course.  */
  struct frame_info *frame = next_frame->prev;
  gdb_assert (frame != NULL);

  if (deprecated_get_frame_saved_regs (frame) == NULL)
    {
      /* If nothing's initialized the saved regs, do it now.  */
      gdb_assert (DEPRECATED_FRAME_INIT_SAVED_REGS_P ());
      DEPRECATED_FRAME_INIT_SAVED_REGS (frame);
      gdb_assert (deprecated_get_frame_saved_regs (frame) != NULL);
    }

  if (deprecated_get_frame_saved_regs (frame) != NULL
      && deprecated_get_frame_saved_regs (frame)[regnum] != 0)
    {
      if (regnum == SP_REGNUM)
	{
	  /* SP register treated specially.  */
	  *optimizedp = 0;
	  *lvalp = not_lval;
	  *addrp = 0;
	  *realnump = -1;
	  if (bufferp != NULL)
	    /* NOTE: cagney/2003-05-09: In-lined store_address with
               it's body - store_unsigned_integer.  */
	    store_unsigned_integer (bufferp, DEPRECATED_REGISTER_RAW_SIZE (regnum),
				    deprecated_get_frame_saved_regs (frame)[regnum]);
	}
      else
	{
	  /* Any other register is saved in memory, fetch it but cache
             a local copy of its value.  */
	  *optimizedp = 0;
	  *lvalp = lval_memory;
	  *addrp = deprecated_get_frame_saved_regs (frame)[regnum];
	  *realnump = -1;
	  if (bufferp != NULL)
	    {
#if 1
	      /* Save each register value, as it is read in, in a
                 frame based cache.  */
	      void **regs = (*this_prologue_cache);
	      if (regs == NULL)
		{
		  int sizeof_cache = ((NUM_REGS + NUM_PSEUDO_REGS)
				      * sizeof (void *));
		  regs = frame_obstack_zalloc (sizeof_cache);
		  (*this_prologue_cache) = regs;
		}
	      if (regs[regnum] == NULL)
		{
		  regs[regnum]
		    = frame_obstack_zalloc (DEPRECATED_REGISTER_RAW_SIZE (regnum));
		  read_memory (deprecated_get_frame_saved_regs (frame)[regnum], regs[regnum],
			       DEPRECATED_REGISTER_RAW_SIZE (regnum));
		}
	      memcpy (bufferp, regs[regnum], DEPRECATED_REGISTER_RAW_SIZE (regnum));
#else
	      /* Read the value in from memory.  */
	      read_memory (deprecated_get_frame_saved_regs (frame)[regnum], bufferp,
			   DEPRECATED_REGISTER_RAW_SIZE (regnum));
#endif
	    }
	}
      return;
    }

  /* No luck.  Assume this and the next frame have the same register
     value.  Pass the unwind request down the frame chain to the next
     frame.  Hopefully that frame will find the register's location.  */
  frame_register_unwind (next_frame, regnum, optimizedp, lvalp, addrp,
			 realnump, bufferp);
}

static void
legacy_saved_regs_this_id (struct frame_info *next_frame,
			   void **this_prologue_cache,
			   struct frame_id *id)
{
  /* A developer is trying to bring up a new architecture, help them
     by providing a default unwinder that refuses to unwind anything
     (the ID is always NULL).  In the case of legacy code,
     legacy_get_prev_frame() will have previously set ->this_id.p, so
     this code won't be called.  */
  (*id) = null_frame_id;
}
	
const struct frame_unwind legacy_saved_regs_unwinder = {
  /* Not really.  It gets overridden by legacy_get_prev_frame.  */
  UNKNOWN_FRAME,
  legacy_saved_regs_this_id,
  legacy_saved_regs_prev_register
};
const struct frame_unwind *legacy_saved_regs_unwind = &legacy_saved_regs_unwinder;


/* Function: deprecated_generic_get_saved_register
   Find register number REGNUM relative to FRAME and put its (raw,
   target format) contents in *RAW_BUFFER.

   Set *OPTIMIZED if the variable was optimized out (and thus can't be
   fetched).  Note that this is never set to anything other than zero
   in this implementation.

   Set *LVAL to lval_memory, lval_register, or not_lval, depending on
   whether the value was fetched from memory, from a register, or in a
   strange and non-modifiable way (e.g. a frame pointer which was
   calculated rather than fetched).  We will use not_lval for values
   fetched from generic dummy frames.

   Set *ADDRP to the address, either in memory or as a
   DEPRECATED_REGISTER_BYTE offset into the registers array.  If the
   value is stored in a dummy frame, set *ADDRP to zero.

   The argument RAW_BUFFER must point to aligned memory.  */

void
deprecated_generic_get_saved_register (char *raw_buffer, int *optimized,
				       CORE_ADDR *addrp,
				       struct frame_info *frame, int regnum,
				       enum lval_type *lval)
{
  if (!target_has_registers)
    error ("No registers.");

  /* Normal systems don't optimize out things with register numbers.  */
  if (optimized != NULL)
    *optimized = 0;

  if (addrp)			/* default assumption: not found in memory */
    *addrp = 0;

  /* Note: since the current frame's registers could only have been
     saved by frames INTERIOR TO the current frame, we skip examining
     the current frame itself: otherwise, we would be getting the
     previous frame's registers which were saved by the current frame.  */

  if (frame != NULL)
    {
      for (frame = get_next_frame (frame);
	   frame_relative_level (frame) >= 0;
	   frame = get_next_frame (frame))
	{
	  if (get_frame_type (frame) == DUMMY_FRAME)
	    {
	      if (lval)		/* found it in a CALL_DUMMY frame */
		*lval = not_lval;
	      if (raw_buffer)
		/* FIXME: cagney/2002-06-26: This should be via the
		   gdbarch_register_read() method so that it, on the
		   fly, constructs either a raw or pseudo register
		   from the raw register cache.  */
		regcache_raw_read
		  (deprecated_find_dummy_frame_regcache (get_frame_pc (frame),
							 get_frame_base (frame)),
		   regnum, raw_buffer);
	      return;
	    }

	  DEPRECATED_FRAME_INIT_SAVED_REGS (frame);
	  if (deprecated_get_frame_saved_regs (frame) != NULL
	      && deprecated_get_frame_saved_regs (frame)[regnum] != 0)
	    {
	      if (lval)		/* found it saved on the stack */
		*lval = lval_memory;
	      if (regnum == SP_REGNUM)
		{
		  if (raw_buffer)	/* SP register treated specially */
		    /* NOTE: cagney/2003-05-09: In-line store_address
                       with it's body - store_unsigned_integer.  */
		    store_unsigned_integer (raw_buffer,
					    DEPRECATED_REGISTER_RAW_SIZE (regnum),
					    deprecated_get_frame_saved_regs (frame)[regnum]);
		}
	      else
		{
		  if (addrp)	/* any other register */
		    *addrp = deprecated_get_frame_saved_regs (frame)[regnum];
		  if (raw_buffer)
		    read_memory (deprecated_get_frame_saved_regs (frame)[regnum], raw_buffer,
				 DEPRECATED_REGISTER_RAW_SIZE (regnum));
		}
	      return;
	    }
	}
    }

  /* If we get thru the loop to this point, it means the register was
     not saved in any frame.  Return the actual live-register value.  */

  if (lval)			/* found it in a live register */
    *lval = lval_register;
  if (addrp)
    *addrp = DEPRECATED_REGISTER_BYTE (regnum);
  if (raw_buffer)
    deprecated_read_register_gen (regnum, raw_buffer);
}

/* Determine the frame's type based on its PC.  */

static enum frame_type
frame_type_from_pc (CORE_ADDR pc)
{
  /* FIXME: cagney/2002-11-24: Can't yet directly call
     pc_in_dummy_frame() as some architectures don't set
     PC_IN_CALL_DUMMY() to generic_pc_in_call_dummy() (remember the
     latter is implemented by simply calling pc_in_dummy_frame).  */
  if (DEPRECATED_USE_GENERIC_DUMMY_FRAMES
      && DEPRECATED_PC_IN_CALL_DUMMY (pc, 0, 0))
    return DUMMY_FRAME;
  else
    {
      char *name;
      find_pc_partial_function (pc, &name, NULL, NULL);
      if (PC_IN_SIGTRAMP (pc, name))
	return SIGTRAMP_FRAME;
      else
	return NORMAL_FRAME;
    }
}

/* Create an arbitrary (i.e. address specified by user) or innermost frame.
   Always returns a non-NULL value.  */

struct frame_info *
create_new_frame (CORE_ADDR addr, CORE_ADDR pc)
{
  struct frame_info *fi;

  if (frame_debug)
    {
      fprintf_unfiltered (gdb_stdlog,
			  "{ create_new_frame (addr=0x%s, pc=0x%s) ",
			  paddr_nz (addr), paddr_nz (pc));
    }

  fi = frame_obstack_zalloc (sizeof (struct frame_info));

  fi->next = create_sentinel_frame (current_regcache);

  /* Select/initialize both the unwind function and the frame's type
     based on the PC.  */
  fi->unwind = frame_unwind_find_by_frame (fi->next);
  if (fi->unwind->type != UNKNOWN_FRAME)
    fi->type = fi->unwind->type;
  else
    fi->type = frame_type_from_pc (pc);

  fi->this_id.p = 1;
  deprecated_update_frame_base_hack (fi, addr);
  deprecated_update_frame_pc_hack (fi, pc);

  if (DEPRECATED_INIT_EXTRA_FRAME_INFO_P ())
    DEPRECATED_INIT_EXTRA_FRAME_INFO (0, fi);

  if (frame_debug)
    {
      fprintf_unfiltered (gdb_stdlog, "-> ");
      fprint_frame (gdb_stdlog, fi);
      fprintf_unfiltered (gdb_stdlog, " }\n");
    }

  return fi;
}

/* Return the frame that THIS_FRAME calls (NULL if THIS_FRAME is the
   innermost frame).  Be careful to not fall off the bottom of the
   frame chain and onto the sentinel frame.  */

struct frame_info *
get_next_frame (struct frame_info *this_frame)
{
  if (this_frame->level > 0)
    return this_frame->next;
  else
    return NULL;
}

/* Flush the entire frame cache.  */

void
flush_cached_frames (void)
{
  /* Since we can't really be sure what the first object allocated was */
  obstack_free (&frame_cache_obstack, 0);
  obstack_init (&frame_cache_obstack);

  current_frame = NULL;		/* Invalidate cache */
  select_frame (NULL);
  annotate_frames_invalid ();
  if (frame_debug)
    fprintf_unfiltered (gdb_stdlog, "{ flush_cached_frames () }\n");
}

/* Flush the frame cache, and start a new one if necessary.  */

void
reinit_frame_cache (void)
{
  flush_cached_frames ();

  /* FIXME: The inferior_ptid test is wrong if there is a corefile.  */
  if (PIDGET (inferior_ptid) != 0)
    {
      select_frame (get_current_frame ());
    }
}

/* Create the previous frame using the deprecated methods
   INIT_EXTRA_INFO, INIT_FRAME_PC and INIT_FRAME_PC_FIRST.  */

static struct frame_info *
legacy_get_prev_frame (struct frame_info *this_frame)
{
  CORE_ADDR address = 0;
  struct frame_info *prev;
  int fromleaf;

  /* Don't frame_debug print legacy_get_prev_frame() here, just
     confuses the output.  */

  /* Allocate the new frame.

     There is no reason to worry about memory leaks, should the
     remainder of the function fail.  The allocated memory will be
     quickly reclaimed when the frame cache is flushed, and the `we've
     been here before' check, in get_prev_frame will stop repeated
     memory allocation calls.  */
  prev = FRAME_OBSTACK_ZALLOC (struct frame_info);
  prev->level = this_frame->level + 1;

  /* Do not completely wire it in to the frame chain.  Some (bad) code
     in INIT_FRAME_EXTRA_INFO tries to look along frame->prev to pull
     some fancy tricks (of course such code is, by definition,
     recursive).
  
     On the other hand, methods, such as get_frame_pc() and
     get_frame_base() rely on being able to walk along the frame
     chain.  Make certain that at least they work by providing that
     link.  Of course things manipulating prev can't go back.  */
  prev->next = this_frame;

  /* NOTE: cagney/2002-11-18: Should have been correctly setting the
     frame's type here, before anything else, and not last, at the
     bottom of this function.  The various
     DEPRECATED_INIT_EXTRA_FRAME_INFO, DEPRECATED_INIT_FRAME_PC,
     DEPRECATED_INIT_FRAME_PC_FIRST and
     DEPRECATED_FRAME_INIT_SAVED_REGS methods are full of work-arounds
     that handle the frame not being correctly set from the start.
     Unfortunately those same work-arounds rely on the type defaulting
     to NORMAL_FRAME.  Ulgh!  The new frame code does not have this
     problem.  */
  prev->type = UNKNOWN_FRAME;

  /* A legacy frame's ID is always computed here.  Mark it as valid.  */
  prev->this_id.p = 1;

  /* Handle sentinel frame unwind as a special case.  */
  if (this_frame->level < 0)
    {
      /* Try to unwind the PC.  If that doesn't work, assume we've reached
	 the oldest frame and simply return.  Is there a better sentinal
	 value?  The unwound PC value is then used to initialize the new
	 previous frame's type.

	 Note that the pc-unwind is intentionally performed before the
	 frame chain.  This is ok since, for old targets, both
	 frame_pc_unwind (nee, DEPRECATED_FRAME_SAVED_PC) and
	 DEPRECATED_FRAME_CHAIN()) assume THIS_FRAME's data structures
	 have already been initialized (using
	 DEPRECATED_INIT_EXTRA_FRAME_INFO) and hence the call order
	 doesn't matter.
	 
	 By unwinding the PC first, it becomes possible to, in the case of
	 a dummy frame, avoid also unwinding the frame ID.  This is
	 because (well ignoring the PPC) a dummy frame can be located
	 using THIS_FRAME's frame ID.  */
      
      deprecated_update_frame_pc_hack (prev, frame_pc_unwind (this_frame));
      if (get_frame_pc (prev) == 0)
	{
	  /* The allocated PREV_FRAME will be reclaimed when the frame
	     obstack is next purged.  */
	  if (frame_debug)
	    {
	      fprintf_unfiltered (gdb_stdlog, "-> ");
	      fprint_frame (gdb_stdlog, NULL);
	      fprintf_unfiltered (gdb_stdlog,
				  " // unwound legacy PC zero }\n");
	    }
	  return NULL;
	}

      /* Set the unwind functions based on that identified PC.  Ditto
         for the "type" but strongly prefer the unwinder's frame type.  */
      prev->unwind = frame_unwind_find_by_frame (prev->next);
      if (prev->unwind->type == UNKNOWN_FRAME)
	prev->type = frame_type_from_pc (get_frame_pc (prev));
      else
	prev->type = prev->unwind->type;

      /* Find the prev's frame's ID.  */
      if (prev->type == DUMMY_FRAME
	  && gdbarch_unwind_dummy_id_p (current_gdbarch))
	{
	  /* When unwinding a normal frame, the stack structure is
	     determined by analyzing the frame's function's code (be
	     it using brute force prologue analysis, or the dwarf2
	     CFI).  In the case of a dummy frame, that simply isn't
	     possible.  The The PC is either the program entry point,
	     or some random address on the stack.  Trying to use that
	     PC to apply standard frame ID unwind techniques is just
	     asking for trouble.  */
	  /* Use an architecture specific method to extract the prev's
	     dummy ID from the next frame.  Note that this method uses
	     frame_register_unwind to obtain the register values
	     needed to determine the dummy frame's ID.  */
	  prev->this_id.value = gdbarch_unwind_dummy_id (current_gdbarch,
							 this_frame);
	}
      else
	{
	  /* We're unwinding a sentinel frame, the PC of which is
	     pointing at a stack dummy.  Fake up the dummy frame's ID
	     using the same sequence as is found a traditional
	     unwinder.  Once all architectures supply the
	     unwind_dummy_id method, this code can go away.  */
	  prev->this_id.value = frame_id_build (deprecated_read_fp (),
						read_pc ());
	}

      /* Check that the unwound ID is valid.  */
      if (!frame_id_p (prev->this_id.value))
	{
	  if (frame_debug)
	    {
	      fprintf_unfiltered (gdb_stdlog, "-> ");
	      fprint_frame (gdb_stdlog, NULL);
	      fprintf_unfiltered (gdb_stdlog,
				  " // unwound legacy ID invalid }\n");
	    }
	  return NULL;
	}

      /* Check that the new frame isn't inner to (younger, below,
	 next) the old frame.  If that happens the frame unwind is
	 going backwards.  */
      /* FIXME: cagney/2003-02-25: Ignore the sentinel frame since
	 that doesn't have a valid frame ID.  Should instead set the
	 sentinel frame's frame ID to a `sentinel'.  Leave it until
	 after the switch to storing the frame ID, instead of the
	 frame base, in the frame object.  */

      /* Link it in.  */
      this_frame->prev = prev;

      /* FIXME: cagney/2002-01-19: This call will go away.  Instead of
	 initializing extra info, all frames will use the frame_cache
	 (passed to the unwind functions) to store additional frame
	 info.  Unfortunately legacy targets can't use
	 legacy_get_prev_frame() to unwind the sentinel frame and,
	 consequently, are forced to take this code path and rely on
	 the below call to DEPRECATED_INIT_EXTRA_FRAME_INFO to
	 initialize the inner-most frame.  */
      if (DEPRECATED_INIT_EXTRA_FRAME_INFO_P ())
	{
	  DEPRECATED_INIT_EXTRA_FRAME_INFO (0, prev);
	}

      if (prev->type == NORMAL_FRAME)
	prev->this_id.value.code_addr
	  = get_pc_function_start (prev->this_id.value.code_addr);

      if (frame_debug)
	{
	  fprintf_unfiltered (gdb_stdlog, "-> ");
	  fprint_frame (gdb_stdlog, prev);
	  fprintf_unfiltered (gdb_stdlog, " } // legacy innermost frame\n");
	}
      return prev;
    }

  /* This code only works on normal frames.  A sentinel frame, where
     the level is -1, should never reach this code.  */
  gdb_assert (this_frame->level >= 0);

  /* On some machines it is possible to call a function without
     setting up a stack frame for it.  On these machines, we
     define this macro to take two args; a frameinfo pointer
     identifying a frame and a variable to set or clear if it is
     or isn't leafless.  */

  /* Still don't want to worry about this except on the innermost
     frame.  This macro will set FROMLEAF if THIS_FRAME is a frameless
     function invocation.  */
  if (this_frame->level == 0)
    /* FIXME: 2002-11-09: Frameless functions can occure anywhere in
       the frame chain, not just the inner most frame!  The generic,
       per-architecture, frame code should handle this and the below
       should simply be removed.  */
    fromleaf = (DEPRECATED_FRAMELESS_FUNCTION_INVOCATION_P ()
		&& DEPRECATED_FRAMELESS_FUNCTION_INVOCATION (this_frame));
  else
    fromleaf = 0;

  if (fromleaf)
    /* A frameless inner-most frame.  The `FP' (which isn't an
       architecture frame-pointer register!) of the caller is the same
       as the callee.  */
    /* FIXME: 2002-11-09: There isn't any reason to special case this
       edge condition.  Instead the per-architecture code should hande
       it locally.  */
    /* FIXME: cagney/2003-06-16: This returns the inner most stack
       address for the previous frame, that, however, is wrong.  It
       should be the inner most stack address for the previous to
       previous frame.  This is because it is the previous to previous
       frame's innermost stack address that is constant through out
       the lifetime of the previous frame (trust me :-).  */
    address = get_frame_base (this_frame);
  else
    {
      /* Two macros defined in tm.h specify the machine-dependent
         actions to be performed here.

         First, get the frame's chain-pointer.

         If that is zero, the frame is the outermost frame or a leaf
         called by the outermost frame.  This means that if start
         calls main without a frame, we'll return 0 (which is fine
         anyway).

         Nope; there's a problem.  This also returns when the current
         routine is a leaf of main.  This is unacceptable.  We move
         this to after the ffi test; I'd rather have backtraces from
         start go curfluy than have an abort called from main not show
         main.  */
      if (DEPRECATED_FRAME_CHAIN_P ())
	address = DEPRECATED_FRAME_CHAIN (this_frame);
      else
	{
	  /* Someone is part way through coverting an old architecture
             to the new frame code.  Implement FRAME_CHAIN the way the
             new frame will.  */
	  /* Find PREV frame's unwinder.  */
	  prev->unwind = frame_unwind_find_by_frame (this_frame->next);
	  /* FIXME: cagney/2003-04-02: Rather than storing the frame's
	     type in the frame, the unwinder's type should be returned
	     directly.  Unfortunately, legacy code, called by
	     legacy_get_prev_frame, explicitly set the frames type
	     using the method deprecated_set_frame_type().  */
	  prev->type = prev->unwind->type;
	  /* Find PREV frame's ID.  */
	  prev->unwind->this_id (this_frame,
				 &prev->prologue_cache,
				 &prev->this_id.value);
	  prev->this_id.p = 1;
	  address = prev->this_id.value.stack_addr;
	}

      if (!legacy_frame_chain_valid (address, this_frame))
	{
	  if (frame_debug)
	    {
	      fprintf_unfiltered (gdb_stdlog, "-> ");
	      fprint_frame (gdb_stdlog, NULL);
	      fprintf_unfiltered (gdb_stdlog,
				  " // legacy frame chain invalid }\n");
	    }
	  return NULL;
	}
    }
  if (address == 0)
    {
      if (frame_debug)
	{
	  fprintf_unfiltered (gdb_stdlog, "-> ");
	  fprint_frame (gdb_stdlog, NULL);
	  fprintf_unfiltered (gdb_stdlog,
			      " // legacy frame chain NULL }\n");
	}
      return NULL;
    }

  /* Link in the already allocated prev frame.  */
  this_frame->prev = prev;
  deprecated_update_frame_base_hack (prev, address);

  /* This change should not be needed, FIXME!  We should determine
     whether any targets *need* DEPRECATED_INIT_FRAME_PC to happen
     after DEPRECATED_INIT_EXTRA_FRAME_INFO and come up with a simple
     way to express what goes on here.

     DEPRECATED_INIT_EXTRA_FRAME_INFO is called from two places:
     create_new_frame (where the PC is already set up) and here (where
     it isn't).  DEPRECATED_INIT_FRAME_PC is only called from here,
     always after DEPRECATED_INIT_EXTRA_FRAME_INFO.

     The catch is the MIPS, where DEPRECATED_INIT_EXTRA_FRAME_INFO
     requires the PC value (which hasn't been set yet).  Some other
     machines appear to require DEPRECATED_INIT_EXTRA_FRAME_INFO
     before they can do DEPRECATED_INIT_FRAME_PC.  Phoo.

     We shouldn't need DEPRECATED_INIT_FRAME_PC_FIRST to add more
     complication to an already overcomplicated part of GDB.
     gnu@cygnus.com, 15Sep92.

     Assuming that some machines need DEPRECATED_INIT_FRAME_PC after
     DEPRECATED_INIT_EXTRA_FRAME_INFO, one possible scheme:

     SETUP_INNERMOST_FRAME(): Default version is just create_new_frame
     (deprecated_read_fp ()), read_pc ()).  Machines with extra frame
     info would do that (or the local equivalent) and then set the
     extra fields.

     SETUP_ARBITRARY_FRAME(argc, argv): Only change here is that
     create_new_frame would no longer init extra frame info;
     SETUP_ARBITRARY_FRAME would have to do that.

     INIT_PREV_FRAME(fromleaf, prev) Replace
     DEPRECATED_INIT_EXTRA_FRAME_INFO and DEPRECATED_INIT_FRAME_PC.
     This should also return a flag saying whether to keep the new
     frame, or whether to discard it, because on some machines (e.g.
     mips) it is really awkward to have DEPRECATED_FRAME_CHAIN_VALID
     called BEFORE DEPRECATED_INIT_EXTRA_FRAME_INFO (there is no good
     way to get information deduced in DEPRECATED_FRAME_CHAIN_VALID
     into the extra fields of the new frame).  std_frame_pc(fromleaf,
     prev)

     This is the default setting for INIT_PREV_FRAME.  It just does
     what the default DEPRECATED_INIT_FRAME_PC does.  Some machines
     will call it from INIT_PREV_FRAME (either at the beginning, the
     end, or in the middle).  Some machines won't use it.

     kingdon@cygnus.com, 13Apr93, 31Jan94, 14Dec94.  */

  /* NOTE: cagney/2002-11-09: Just ignore the above!  There is no
     reason for things to be this complicated.

     The trick is to assume that there is always a frame.  Instead of
     special casing the inner-most frame, create fake frame
     (containing the hardware registers) that is inner to the
     user-visible inner-most frame (...) and then unwind from that.
     That way architecture code can use use the standard
     frame_XX_unwind() functions and not differentiate between the
     inner most and any other case.

     Since there is always a frame to unwind from, there is always
     somewhere (THIS_FRAME) to store all the info needed to construct
     a new (previous) frame without having to first create it.  This
     means that the convolution below - needing to carefully order a
     frame's initialization - isn't needed.

     The irony here though, is that DEPRECATED_FRAME_CHAIN(), at least
     for a more up-to-date architecture, always calls
     FRAME_SAVED_PC(), and FRAME_SAVED_PC() computes the PC but
     without first needing the frame!  Instead of the convolution
     below, we could have simply called FRAME_SAVED_PC() and been done
     with it!  Note that FRAME_SAVED_PC() is being superseed by
     frame_pc_unwind() and that function does have somewhere to cache
     that PC value.  */

  if (DEPRECATED_INIT_FRAME_PC_FIRST_P ())
    deprecated_update_frame_pc_hack (prev,
				     DEPRECATED_INIT_FRAME_PC_FIRST (fromleaf,
								     prev));

  if (DEPRECATED_INIT_EXTRA_FRAME_INFO_P ())
    DEPRECATED_INIT_EXTRA_FRAME_INFO (fromleaf, prev);

  /* This entry is in the frame queue now, which is good since
     FRAME_SAVED_PC may use that queue to figure out its value (see
     tm-sparc.h).  We want the pc saved in the inferior frame. */
  if (DEPRECATED_INIT_FRAME_PC_P ())
    deprecated_update_frame_pc_hack (prev,
				     DEPRECATED_INIT_FRAME_PC (fromleaf,
							       prev));

  /* If ->frame and ->pc are unchanged, we are in the process of
     getting ourselves into an infinite backtrace.  Some architectures
     check this in DEPRECATED_FRAME_CHAIN or thereabouts, but it seems
     like there is no reason this can't be an architecture-independent
     check.  */
  if (get_frame_base (prev) == get_frame_base (this_frame)
      && get_frame_pc (prev) == get_frame_pc (this_frame))
    {
      this_frame->prev = NULL;
      obstack_free (&frame_cache_obstack, prev);
      if (frame_debug)
	{
	  fprintf_unfiltered (gdb_stdlog, "-> ");
	  fprint_frame (gdb_stdlog, NULL);
	  fprintf_unfiltered (gdb_stdlog,
			      " // legacy this.id == prev.id }\n");
	}
      return NULL;
    }

  /* Initialize the code used to unwind the frame PREV based on the PC
     (and probably other architectural information).  The PC lets you
     check things like the debug info at that point (dwarf2cfi?) and
     use that to decide how the frame should be unwound.

     If there isn't a FRAME_CHAIN, the code above will have already
     done this.  */
  if (prev->unwind == NULL)
    prev->unwind = frame_unwind_find_by_frame (prev->next);

  /* If the unwinder provides a frame type, use it.  Otherwize
     continue on to that heuristic mess.  */
  if (prev->unwind->type != UNKNOWN_FRAME)
    {
      prev->type = prev->unwind->type;
      if (prev->type == NORMAL_FRAME)
	/* FIXME: cagney/2003-06-16: would get_frame_pc() be better?  */
	prev->this_id.value.code_addr
	  = get_pc_function_start (prev->this_id.value.code_addr);
      if (frame_debug)
	{
	  fprintf_unfiltered (gdb_stdlog, "-> ");
	  fprint_frame (gdb_stdlog, prev);
	  fprintf_unfiltered (gdb_stdlog, " } // legacy with unwound type\n");
	}
      return prev;
    }

  /* NOTE: cagney/2002-11-18: The code segments, found in
     create_new_frame and get_prev_frame(), that initializes the
     frames type is subtly different.  The latter only updates ->type
     when it encounters a SIGTRAMP_FRAME or DUMMY_FRAME.  This stops
     get_prev_frame() overriding the frame's type when the INIT code
     has previously set it.  This is really somewhat bogus.  The
     initialization, as seen in create_new_frame(), should occur
     before the INIT function has been called.  */
  if (DEPRECATED_USE_GENERIC_DUMMY_FRAMES
      && (DEPRECATED_PC_IN_CALL_DUMMY_P ()
	  ? DEPRECATED_PC_IN_CALL_DUMMY (get_frame_pc (prev), 0, 0)
	  : pc_in_dummy_frame (get_frame_pc (prev))))
    prev->type = DUMMY_FRAME;
  else
    {
      /* FIXME: cagney/2002-11-10: This should be moved to before the
	 INIT code above so that the INIT code knows what the frame's
	 type is (in fact, for a [generic] dummy-frame, the type can
	 be set and then the entire initialization can be skipped.
	 Unforunatly, its the INIT code that sets the PC (Hmm, catch
	 22).  */
      char *name;
      find_pc_partial_function (get_frame_pc (prev), &name, NULL, NULL);
      if (PC_IN_SIGTRAMP (get_frame_pc (prev), name))
	prev->type = SIGTRAMP_FRAME;
      /* FIXME: cagney/2002-11-11: Leave prev->type alone.  Some
         architectures are forcing the frame's type in INIT so we
         don't want to override it here.  Remember, NORMAL_FRAME == 0,
         so it all works (just :-/).  Once this initialization is
         moved to the start of this function, all this nastness will
         go away.  */
    }

  if (prev->type == NORMAL_FRAME)
    prev->this_id.value.code_addr
      = get_pc_function_start (prev->this_id.value.code_addr);

  if (frame_debug)
    {
      fprintf_unfiltered (gdb_stdlog, "-> ");
      fprint_frame (gdb_stdlog, prev);
      fprintf_unfiltered (gdb_stdlog, " } // legacy with confused type\n");
    }

  return prev;
}

/* Return a structure containing various interesting information
   about the frame that called THIS_FRAME.  Returns NULL
   if there is no such frame.

   This function tests some target-independent conditions that should
   terminate the frame chain, such as unwinding past main().  It
   should not contain any target-dependent tests, such as checking
   whether the program-counter is zero.  */

struct frame_info *
get_prev_frame (struct frame_info *this_frame)
{
  struct frame_info *prev_frame;

  if (frame_debug)
    {
      fprintf_unfiltered (gdb_stdlog, "{ get_prev_frame (this_frame=");
      if (this_frame != NULL)
	fprintf_unfiltered (gdb_stdlog, "%d", this_frame->level);
      else
	fprintf_unfiltered (gdb_stdlog, "<NULL>");
      fprintf_unfiltered (gdb_stdlog, ") ");
    }

  /* Return the inner-most frame, when the caller passes in NULL.  */
  /* NOTE: cagney/2002-11-09: Not sure how this would happen.  The
     caller should have previously obtained a valid frame using
     get_selected_frame() and then called this code - only possibility
     I can think of is code behaving badly.

     NOTE: cagney/2003-01-10: Talk about code behaving badly.  Check
     block_innermost_frame().  It does the sequence: frame = NULL;
     while (1) { frame = get_prev_frame (frame); .... }.  Ulgh!  Why
     it couldn't be written better, I don't know.

     NOTE: cagney/2003-01-11: I suspect what is happening is
     block_innermost_frame() is, when the target has no state
     (registers, memory, ...), still calling this function.  The
     assumption being that this function will return NULL indicating
     that a frame isn't possible, rather than checking that the target
     has state and then calling get_current_frame() and
     get_prev_frame().  This is a guess mind.  */
  if (this_frame == NULL)
    {
      /* NOTE: cagney/2002-11-09: There was a code segment here that
	 would error out when CURRENT_FRAME was NULL.  The comment
	 that went with it made the claim ...

	 ``This screws value_of_variable, which just wants a nice
	 clean NULL return from block_innermost_frame if there are no
	 frames.  I don't think I've ever seen this message happen
	 otherwise.  And returning NULL here is a perfectly legitimate
	 thing to do.''

         Per the above, this code shouldn't even be called with a NULL
         THIS_FRAME.  */
      return current_frame;
    }

  /* There is always a frame.  If this assertion fails, suspect that
     something should be calling get_selected_frame() or
     get_current_frame().  */
  gdb_assert (this_frame != NULL);

  /* Make sure we pass an address within THIS_FRAME's code block to
     inside_main_func.  Otherwise, we might stop unwinding at a
     function which has a call instruction as its last instruction if
     that function immediately precedes main().  */
  if (this_frame->level >= 0
      && !backtrace_past_main
      && inside_main_func (get_frame_address_in_block (this_frame)))
    /* Don't unwind past main(), bug always unwind the sentinel frame.
       Note, this is done _before_ the frame has been marked as
       previously unwound.  That way if the user later decides to
       allow unwinds past main(), that just happens.  */
    {
      if (frame_debug)
	fprintf_unfiltered (gdb_stdlog, "-> NULL // inside main func }\n");
      return NULL;
    }

  if (this_frame->level > backtrace_limit)
    {
      error ("Backtrace limit of %d exceeded", backtrace_limit);
    }

  /* If we're already inside the entry function for the main objfile,
     then it isn't valid.  Don't apply this test to a dummy frame -
     dummy frame PC's typically land in the entry func.  Don't apply
     this test to the sentinel frame.  Sentinel frames should always
     be allowed to unwind.  */
  /* NOTE: cagney/2003-02-25: Don't enable until someone has found
     hard evidence that this is needed.  */
  /* NOTE: cagney/2003-07-07: Fixed a bug in inside_main_func - wasn't
     checking for "main" in the minimal symbols.  With that fixed
     asm-source tests now stop in "main" instead of halting the
     backtrace in wierd and wonderful ways somewhere inside the entry
     file.  Suspect that deprecated_inside_entry_file and
     inside_entry_func tests were added to work around that (now
     fixed) case.  */
  /* NOTE: cagney/2003-07-15: danielj (if I'm reading it right)
     suggested having the inside_entry_func test use the
     inside_main_func msymbol trick (along with entry_point_address I
     guess) to determine the address range of the start function.
     That should provide a far better stopper than the current
     heuristics.  */
  /* NOTE: cagney/2003-07-15: Need to add a "set backtrace
     beyond-entry-func" command so that this can be selectively
     disabled.  */
  if (0
#if 0
      && backtrace_beyond_entry_func
#endif
      && this_frame->type != DUMMY_FRAME && this_frame->level >= 0
      && inside_entry_func (this_frame))
    {
      if (frame_debug)
	{
	  fprintf_unfiltered (gdb_stdlog, "-> ");
	  fprint_frame (gdb_stdlog, NULL);
	  fprintf_unfiltered (gdb_stdlog, "// inside entry func }\n");
	}
      return NULL;
    }

  /* Assume that the only way to get a zero PC is through something
     like a SIGSEGV or a dummy frame, and hence that NORMAL frames
     will never unwind a zero PC.  */
  if (this_frame->level > 0
      && get_frame_type (this_frame) == NORMAL_FRAME
      && get_frame_type (get_next_frame (this_frame)) == NORMAL_FRAME
      && get_frame_pc (this_frame) == 0)
    {
      if (frame_debug)
	{
	  fprintf_unfiltered (gdb_stdlog, "-> ");
	  fprint_frame (gdb_stdlog, this_frame->prev);
	  fprintf_unfiltered (gdb_stdlog, " // zero PC \n");
	}
      return NULL;
    }

  /* Only try to do the unwind once.  */
  if (this_frame->prev_p)
    {
      if (frame_debug)
	{
	  fprintf_unfiltered (gdb_stdlog, "-> ");
	  fprint_frame (gdb_stdlog, this_frame->prev);
	  fprintf_unfiltered (gdb_stdlog, " // cached \n");
	}
      return this_frame->prev;
    }
  this_frame->prev_p = 1;

  /* If we're inside the entry file, it isn't valid.  Don't apply this
     test to a dummy frame - dummy frame PC's typically land in the
     entry file.  Don't apply this test to the sentinel frame.
     Sentinel frames should always be allowed to unwind.  */
  /* NOTE: drow/2002-12-25: should there be a way to disable this
     check?  It assumes a single small entry file, and the way some
     debug readers (e.g.  dbxread) figure out which object is the
     entry file is somewhat hokey.  */
  /* NOTE: cagney/2003-01-10: If there is a way of disabling this test
     then it should probably be moved to before the ->prev_p test,
     above.  */
  /* NOTE: vinschen/2003-04-01: Disabled.  It turns out that the call
     to deprecated_inside_entry_file destroys a meaningful backtrace
     under some conditions.  E. g. the backtrace tests in the
     asm-source testcase are broken for some targets.  In this test
     the functions are all implemented as part of one file and the
     testcase is not necessarily linked with a start file (depending
     on the target).  What happens is, that the first frame is printed
     normaly and following frames are treated as being inside the
     enttry file then.  This way, only the #0 frame is printed in the
     backtrace output.  */
  if (0
      && this_frame->type != DUMMY_FRAME && this_frame->level >= 0
      && deprecated_inside_entry_file (get_frame_pc (this_frame)))
    {
      if (frame_debug)
	{
	  fprintf_unfiltered (gdb_stdlog, "-> ");
	  fprint_frame (gdb_stdlog, NULL);
	  fprintf_unfiltered (gdb_stdlog, " // inside entry file }\n");
	}
      return NULL;
    }

  /* If any of the old frame initialization methods are around, use
     the legacy get_prev_frame method.  */
  if (legacy_frame_p (current_gdbarch))
    {
      prev_frame = legacy_get_prev_frame (this_frame);
      return prev_frame;
    }

  /* Check that this frame's ID was valid.  If it wasn't, don't try to
     unwind to the prev frame.  Be careful to not apply this test to
     the sentinel frame.  */
  if (this_frame->level >= 0 && !frame_id_p (get_frame_id (this_frame)))
    {
      if (frame_debug)
	{
	  fprintf_unfiltered (gdb_stdlog, "-> ");
	  fprint_frame (gdb_stdlog, NULL);
	  fprintf_unfiltered (gdb_stdlog, " // this ID is NULL }\n");
	}
      return NULL;
    }

  /* Check that this frame's ID isn't inner to (younger, below, next)
     the next frame.  This happens when a frame unwind goes backwards.
     Since the sentinel frame doesn't really exist, don't compare the
     inner-most against that sentinel.  */
  if (this_frame->level > 0
      && frame_id_inner (get_frame_id (this_frame),
			 get_frame_id (this_frame->next)))
    error ("Previous frame inner to this frame (corrupt stack?)");

  /* Check that this and the next frame are not identical.  If they
     are, there is most likely a stack cycle.  As with the inner-than
     test above, avoid comparing the inner-most and sentinel frames.  */
  if (this_frame->level > 0
      && frame_id_eq (get_frame_id (this_frame),
		      get_frame_id (this_frame->next)))
    error ("Previous frame identical to this frame (corrupt stack?)");

  /* Allocate the new frame but do not wire it in to the frame chain.
     Some (bad) code in INIT_FRAME_EXTRA_INFO tries to look along
     frame->next to pull some fancy tricks (of course such code is, by
     definition, recursive).  Try to prevent it.

     There is no reason to worry about memory leaks, should the
     remainder of the function fail.  The allocated memory will be
     quickly reclaimed when the frame cache is flushed, and the `we've
     been here before' check above will stop repeated memory
     allocation calls.  */
  prev_frame = FRAME_OBSTACK_ZALLOC (struct frame_info);
  prev_frame->level = this_frame->level + 1;

  /* Don't yet compute ->unwind (and hence ->type).  It is computed
     on-demand in get_frame_type, frame_register_unwind, and
     get_frame_id.  */

  /* Don't yet compute the frame's ID.  It is computed on-demand by
     get_frame_id().  */

  /* The unwound frame ID is validate at the start of this function,
     as part of the logic to decide if that frame should be further
     unwound, and not here while the prev frame is being created.
     Doing this makes it possible for the user to examine a frame that
     has an invalid frame ID.

     Some very old VAX code noted: [...]  For the sake of argument,
     suppose that the stack is somewhat trashed (which is one reason
     that "info frame" exists).  So, return 0 (indicating we don't
     know the address of the arglist) if we don't know what frame this
     frame calls.  */

  /* Link it in.  */
  this_frame->prev = prev_frame;
  prev_frame->next = this_frame;

  if (frame_debug)
    {
      fprintf_unfiltered (gdb_stdlog, "-> ");
      fprint_frame (gdb_stdlog, prev_frame);
      fprintf_unfiltered (gdb_stdlog, " }\n");
    }

  return prev_frame;
}

CORE_ADDR
get_frame_pc (struct frame_info *frame)
{
  gdb_assert (frame->next != NULL);
  return frame_pc_unwind (frame->next);
}

/* Return an address of that falls within the frame's code block.  */

CORE_ADDR
frame_unwind_address_in_block (struct frame_info *next_frame)
{
  /* A draft address.  */
  CORE_ADDR pc = frame_pc_unwind (next_frame);

  if ((frame_tdep_pc_fixup != NULL) && (frame_tdep_pc_fixup(&pc) == 0))
  	return pc;

  /* If THIS frame is not inner most (i.e., NEXT isn't the sentinel),
     and NEXT is `normal' (i.e., not a sigtramp, dummy, ....) THIS
     frame's PC ends up pointing at the instruction fallowing the
     "call".  Adjust that PC value so that it falls on the call
     instruction (which, hopefully, falls within THIS frame's code
     block.  So far it's proved to be a very good approximation.  See
     get_frame_type for why ->type can't be used.  */
  if (next_frame->level >= 0
      && get_frame_type (next_frame) == NORMAL_FRAME)
    --pc;
  return pc;
}

CORE_ADDR
get_frame_address_in_block (struct frame_info *this_frame)
{
  return frame_unwind_address_in_block (this_frame->next);
}

static int
pc_notcurrent (struct frame_info *frame)
{
  /* If FRAME is not the innermost frame, that normally means that
     FRAME->pc points at the return instruction (which is *after* the
     call instruction), and we want to get the line containing the
     call (because the call is where the user thinks the program is).
     However, if the next frame is either a SIGTRAMP_FRAME or a
     DUMMY_FRAME, then the next frame will contain a saved interrupt
     PC and such a PC indicates the current (rather than next)
     instruction/line, consequently, for such cases, want to get the
     line containing fi->pc.  */
  struct frame_info *next = get_next_frame (frame);
  int notcurrent = (next != NULL && get_frame_type (next) == NORMAL_FRAME);
  return notcurrent;
}

void
find_frame_sal (struct frame_info *frame, struct symtab_and_line *sal)
{
  (*sal) = find_pc_line (get_frame_pc (frame), pc_notcurrent (frame));
}

/* Per "frame.h", return the ``address'' of the frame.  Code should
   really be using get_frame_id().  */
CORE_ADDR
get_frame_base (struct frame_info *fi)
{
  return get_frame_id (fi).stack_addr;
}

/* High-level offsets into the frame.  Used by the debug info.  */

CORE_ADDR
get_frame_base_address (struct frame_info *fi)
{
  if (get_frame_type (fi) != NORMAL_FRAME)
    return 0;
  if (fi->base == NULL)
    fi->base = frame_base_find_by_frame (fi->next);
  /* Sneaky: If the low-level unwind and high-level base code share a
     common unwinder, let them share the prologue cache.  */
  if (fi->base->unwind == fi->unwind)
    return fi->base->this_base (fi->next, &fi->prologue_cache);
  return fi->base->this_base (fi->next, &fi->base_cache);
}

CORE_ADDR
get_frame_locals_address (struct frame_info *fi)
{
  void **cache;
  if (get_frame_type (fi) != NORMAL_FRAME)
    return 0;
  /* If there isn't a frame address method, find it.  */
  if (fi->base == NULL)
    fi->base = frame_base_find_by_frame (fi->next);
  /* Sneaky: If the low-level unwind and high-level base code share a
     common unwinder, let them share the prologue cache.  */
  if (fi->base->unwind == fi->unwind)
    cache = &fi->prologue_cache;
  else
    cache = &fi->base_cache;
  return fi->base->this_locals (fi->next, cache);
}

CORE_ADDR
get_frame_args_address (struct frame_info *fi)
{
  void **cache;
  if (get_frame_type (fi) != NORMAL_FRAME)
    return 0;
  /* If there isn't a frame address method, find it.  */
  if (fi->base == NULL)
    fi->base = frame_base_find_by_frame (fi->next);
  /* Sneaky: If the low-level unwind and high-level base code share a
     common unwinder, let them share the prologue cache.  */
  if (fi->base->unwind == fi->unwind)
    cache = &fi->prologue_cache;
  else
    cache = &fi->base_cache;
  return fi->base->this_args (fi->next, cache);
}

/* Level of the selected frame: 0 for innermost, 1 for its caller, ...
   or -1 for a NULL frame.  */

int
frame_relative_level (struct frame_info *fi)
{
  if (fi == NULL)
    return -1;
  else
    return fi->level;
}

enum frame_type
get_frame_type (struct frame_info *frame)
{
  /* Some targets still don't use [generic] dummy frames.  Catch them
     here.  */
  if (!DEPRECATED_USE_GENERIC_DUMMY_FRAMES
      && deprecated_frame_in_dummy (frame))
    return DUMMY_FRAME;

  /* Some legacy code, e.g, mips_init_extra_frame_info() wants
     to determine the frame's type prior to it being completely
     initialized.  Don't attempt to lazily initialize ->unwind for
     legacy code.  It will be initialized in legacy_get_prev_frame().  */
  if (frame->unwind == NULL && !legacy_frame_p (current_gdbarch))
    {
      /* Initialize the frame's unwinder because it is that which
         provides the frame's type.  */
      frame->unwind = frame_unwind_find_by_frame (frame->next);
      /* FIXME: cagney/2003-04-02: Rather than storing the frame's
	 type in the frame, the unwinder's type should be returned
	 directly.  Unfortunately, legacy code, called by
	 legacy_get_prev_frame, explicitly set the frames type using
	 the method deprecated_set_frame_type().  */
      frame->type = frame->unwind->type;
    }
  if (frame->type == UNKNOWN_FRAME)
    return NORMAL_FRAME;
  else
    return frame->type;
}

void
deprecated_set_frame_type (struct frame_info *frame, enum frame_type type)
{
  /* Arrrg!  See comment in "frame.h".  */
  frame->type = type;
}

struct frame_extra_info *
get_frame_extra_info (struct frame_info *fi)
{
  return fi->extra_info;
}

struct frame_extra_info *
frame_extra_info_zalloc (struct frame_info *fi, long size)
{
  fi->extra_info = frame_obstack_zalloc (size);
  return fi->extra_info;
}

void
deprecated_update_frame_pc_hack (struct frame_info *frame, CORE_ADDR pc)
{
  if (frame_debug)
    fprintf_unfiltered (gdb_stdlog,
			"{ deprecated_update_frame_pc_hack (frame=%d,pc=0x%s) }\n",
			frame->level, paddr_nz (pc));
  /* NOTE: cagney/2003-03-11: Some architectures (e.g., Arm) are
     maintaining a locally allocated frame object.  Since such frame's
     are not in the frame chain, it isn't possible to assume that the
     frame has a next.  Sigh.  */
  if (frame->next != NULL)
    {
      /* While we're at it, update this frame's cached PC value, found
	 in the next frame.  Oh for the day when "struct frame_info"
	 is opaque and this hack on hack can just go away.  */
      frame->next->prev_pc.value = pc;
      frame->next->prev_pc.p = 1;
    }
}

void
deprecated_update_frame_base_hack (struct frame_info *frame, CORE_ADDR base)
{
  if (frame_debug)
    fprintf_unfiltered (gdb_stdlog,
			"{ deprecated_update_frame_base_hack (frame=%d,base=0x%s) }\n",
			frame->level, paddr_nz (base));
  /* See comment in "frame.h".  */
  frame->this_id.value.stack_addr = base;
}

struct frame_info *
deprecated_frame_xmalloc_with_cleanup (long sizeof_saved_regs,
				       long sizeof_extra_info)
{
  struct frame_info *frame = XMALLOC (struct frame_info);
  memset (frame, 0, sizeof (*frame));
  frame->this_id.p = 1;
  make_cleanup (xfree, frame);
  if (sizeof_saved_regs > 0)
    {
      frame->saved_regs = xcalloc (1, sizeof_saved_regs);
      make_cleanup (xfree, frame->saved_regs);
    }
  if (sizeof_extra_info > 0)
    {
      frame->extra_info = xcalloc (1, sizeof_extra_info);
      make_cleanup (xfree, frame->extra_info);
    }
  return frame;
}

/* Memory access methods.  */

void
get_frame_memory (struct frame_info *this_frame, CORE_ADDR addr, void *buf,
		  int len)
{
  read_memory (addr, buf, len);
}

LONGEST
get_frame_memory_signed (struct frame_info *this_frame, CORE_ADDR addr,
			 int len)
{
  return read_memory_integer (addr, len);
}

ULONGEST
get_frame_memory_unsigned (struct frame_info *this_frame, CORE_ADDR addr,
			   int len)
{
  return read_memory_unsigned_integer (addr, len);
}

/* Architecture method.  */

struct gdbarch *
get_frame_arch (struct frame_info *this_frame)
{
  return current_gdbarch;
}

/* Stack pointer methods.  */

CORE_ADDR
get_frame_sp (struct frame_info *this_frame)
{
  return frame_sp_unwind (this_frame->next);
}

CORE_ADDR
frame_sp_unwind (struct frame_info *next_frame)
{
  /* Normality, an architecture that provides a way of obtaining any
     frame inner-most address.  */
  if (gdbarch_unwind_sp_p (current_gdbarch))
    return gdbarch_unwind_sp (current_gdbarch, next_frame);
  /* Things are looking grim.  If it's the inner-most frame and there
     is a TARGET_READ_SP then that can be used.  */
  if (next_frame->level < 0 && TARGET_READ_SP_P ())
    return TARGET_READ_SP ();
  /* Now things are really are grim.  Hope that the value returned by
     the SP_REGNUM register is meaningful.  */
  if (SP_REGNUM >= 0)
    {
      ULONGEST sp;
      frame_unwind_unsigned_register (next_frame, SP_REGNUM, &sp);
      return sp;
    }
  internal_error (__FILE__, __LINE__, "Missing unwind SP method");
}


int
legacy_frame_p (struct gdbarch *current_gdbarch)
{
  if (DEPRECATED_INIT_FRAME_PC_P ()
      || DEPRECATED_INIT_FRAME_PC_FIRST_P ()
      || DEPRECATED_INIT_EXTRA_FRAME_INFO_P ()
      || DEPRECATED_FRAME_CHAIN_P ())
    /* No question, it's a legacy frame.  */
    return 1;
  if (gdbarch_unwind_dummy_id_p (current_gdbarch))
    /* No question, it's not a legacy frame (provided none of the
       deprecated methods checked above are present that is).  */
    return 0;
  if (DEPRECATED_TARGET_READ_FP_P ()
      || DEPRECATED_FP_REGNUM >= 0)
    /* Assume it's legacy.  If you're trying to convert a legacy frame
       target to the new mechanism, get rid of these.  legacy
       get_prev_frame requires these when unwind_frame_id isn't
       available.  */
    return 1;
  /* Default to assuming that it's brand new code, and hence not
     legacy.  Force it down the non-legacy path so that the new code
     uses the new frame mechanism from day one.  Dummy frame's won't
     work very well but we can live with that.  */
  return 0;
}

extern initialize_file_ftype _initialize_frame; /* -Wmissing-prototypes */

static struct cmd_list_element *set_backtrace_cmdlist;
static struct cmd_list_element *show_backtrace_cmdlist;

static void
set_backtrace_cmd (char *args, int from_tty)
{
  help_list (set_backtrace_cmdlist, "set backtrace ", -1, gdb_stdout);
}

static void
show_backtrace_cmd (char *args, int from_tty)
{
  cmd_show_list (show_backtrace_cmdlist, from_tty, "");
}

void
_initialize_frame (void)
{
  obstack_init (&frame_cache_obstack);

  add_prefix_cmd ("backtrace", class_maintenance, set_backtrace_cmd, "\
Set backtrace specific variables.\n\
Configure backtrace variables such as the backtrace limit",
		  &set_backtrace_cmdlist, "set backtrace ",
		  0/*allow-unknown*/, &setlist);
  add_prefix_cmd ("backtrace", class_maintenance, show_backtrace_cmd, "\
Show backtrace specific variables\n\
Show backtrace variables such as the backtrace limit",
		  &show_backtrace_cmdlist, "show backtrace ",
		  0/*allow-unknown*/, &showlist);

  add_setshow_boolean_cmd ("past-main", class_obscure,
			   &backtrace_past_main, "\
Set whether backtraces should continue past \"main\".\n\
Normally the caller of \"main\" is not of interest, so GDB will terminate\n\
the backtrace at \"main\".  Set this variable if you need to see the rest\n\
of the stack trace.", "\
Show whether backtraces should continue past \"main\".\n\
Normally the caller of \"main\" is not of interest, so GDB will terminate\n\
the backtrace at \"main\".  Set this variable if you need to see the rest\n\
of the stack trace.",
			   NULL, NULL, &set_backtrace_cmdlist,
			   &show_backtrace_cmdlist);

  add_setshow_uinteger_cmd ("limit", class_obscure,
			    &backtrace_limit, "\
Set an upper bound on the number of backtrace levels.\n\
No more than the specified number of frames can be displayed or examined.\n\
Zero is unlimited.", "\
Show the upper bound on the number of backtrace levels.",
			    NULL, NULL, &set_backtrace_cmdlist,
			    &show_backtrace_cmdlist);

  /* Debug this files internals. */
  add_show_from_set (add_set_cmd ("frame", class_maintenance, var_zinteger,
				  &frame_debug, "Set frame debugging.\n\
When non-zero, frame specific internal debugging is enabled.", &setdebuglist),
		     &showdebuglist);
}
