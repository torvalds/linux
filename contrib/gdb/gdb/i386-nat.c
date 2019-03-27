/* Native-dependent code for the i386.

   Copyright 2001, 2004 Free Software Foundation, Inc.

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
#include "breakpoint.h"
#include "command.h"
#include "gdbcmd.h"

/* Support for hardware watchpoints and breakpoints using the i386
   debug registers.

   This provides several functions for inserting and removing
   hardware-assisted breakpoints and watchpoints, testing if one or
   more of the watchpoints triggered and at what address, checking
   whether a given region can be watched, etc.

   A target which wants to use these functions should define several
   macros, such as `target_insert_watchpoint' and
   `target_stopped_data_address', listed in target.h, to call the
   appropriate functions below.  It should also define
   I386_USE_GENERIC_WATCHPOINTS in its tm.h file.

   In addition, each target should provide several low-level macros
   that will be called to insert watchpoints and hardware breakpoints
   into the inferior, remove them, and check their status.  These
   macros are:

      I386_DR_LOW_SET_CONTROL  -- set the debug control (DR7)
				  register to a given value

      I386_DR_LOW_SET_ADDR     -- put an address into one debug
				  register

      I386_DR_LOW_RESET_ADDR   -- reset the address stored in
				  one debug register

      I386_DR_LOW_GET_STATUS   -- return the value of the debug
				  status (DR6) register.

   The functions below implement debug registers sharing by reference
   counts, and allow to watch regions up to 16 bytes long.  */

#ifdef I386_USE_GENERIC_WATCHPOINTS

/* Support for 8-byte wide hw watchpoints.  */
#ifndef TARGET_HAS_DR_LEN_8
#define TARGET_HAS_DR_LEN_8	0
#endif

/* Debug registers' indices.  */
#define DR_NADDR	4	/* The number of debug address registers.  */
#define DR_STATUS	6	/* Index of debug status register (DR6).  */
#define DR_CONTROL	7	/* Index of debug control register (DR7). */

/* DR7 Debug Control register fields.  */

/* How many bits to skip in DR7 to get to R/W and LEN fields.  */
#define DR_CONTROL_SHIFT	16
/* How many bits in DR7 per R/W and LEN field for each watchpoint.  */
#define DR_CONTROL_SIZE		4

/* Watchpoint/breakpoint read/write fields in DR7.  */
#define DR_RW_EXECUTE	(0x0)	/* Break on instruction execution.  */
#define DR_RW_WRITE	(0x1)	/* Break on data writes.  */
#define DR_RW_READ	(0x3)	/* Break on data reads or writes.  */

/* This is here for completeness.  No platform supports this
   functionality yet (as of March 2001).  Note that the DE flag in the
   CR4 register needs to be set to support this.  */
#ifndef DR_RW_IORW
#define DR_RW_IORW	(0x2)	/* Break on I/O reads or writes.  */
#endif

/* Watchpoint/breakpoint length fields in DR7.  The 2-bit left shift
   is so we could OR this with the read/write field defined above.  */
#define DR_LEN_1	(0x0 << 2) /* 1-byte region watch or breakpoint.  */
#define DR_LEN_2	(0x1 << 2) /* 2-byte region watch.  */
#define DR_LEN_4	(0x3 << 2) /* 4-byte region watch.  */
#define DR_LEN_8	(0x2 << 2) /* 8-byte region watch (AMD64).  */

/* Local and Global Enable flags in DR7.

   When the Local Enable flag is set, the breakpoint/watchpoint is
   enabled only for the current task; the processor automatically
   clears this flag on every task switch.  When the Global Enable flag
   is set, the breakpoint/watchpoint is enabled for all tasks; the
   processor never clears this flag.

   Currently, all watchpoint are locally enabled.  If you need to
   enable them globally, read the comment which pertains to this in
   i386_insert_aligned_watchpoint below.  */
#define DR_LOCAL_ENABLE_SHIFT	0 /* Extra shift to the local enable bit.  */
#define DR_GLOBAL_ENABLE_SHIFT	1 /* Extra shift to the global enable bit.  */
#define DR_ENABLE_SIZE		2 /* Two enable bits per debug register.  */

/* Local and global exact breakpoint enable flags (a.k.a. slowdown
   flags).  These are only required on i386, to allow detection of the
   exact instruction which caused a watchpoint to break; i486 and
   later processors do that automatically.  We set these flags for
   backwards compatibility.  */
#define DR_LOCAL_SLOWDOWN	(0x100)
#define DR_GLOBAL_SLOWDOWN     	(0x200)

/* Fields reserved by Intel.  This includes the GD (General Detect
   Enable) flag, which causes a debug exception to be generated when a
   MOV instruction accesses one of the debug registers.

   FIXME: My Intel manual says we should use 0xF800, not 0xFC00.  */
#define DR_CONTROL_RESERVED	(0xFC00)

/* Auxiliary helper macros.  */

/* A value that masks all fields in DR7 that are reserved by Intel.  */
#define I386_DR_CONTROL_MASK	(~DR_CONTROL_RESERVED)

/* The I'th debug register is vacant if its Local and Global Enable
   bits are reset in the Debug Control register.  */
#define I386_DR_VACANT(i) \
  ((dr_control_mirror & (3 << (DR_ENABLE_SIZE * (i)))) == 0)

/* Locally enable the break/watchpoint in the I'th debug register.  */
#define I386_DR_LOCAL_ENABLE(i) \
  dr_control_mirror |= (1 << (DR_LOCAL_ENABLE_SHIFT + DR_ENABLE_SIZE * (i)))

/* Globally enable the break/watchpoint in the I'th debug register.  */
#define I386_DR_GLOBAL_ENABLE(i) \
  dr_control_mirror |= (1 << (DR_GLOBAL_ENABLE_SHIFT + DR_ENABLE_SIZE * (i)))

/* Disable the break/watchpoint in the I'th debug register.  */
#define I386_DR_DISABLE(i) \
  dr_control_mirror &= ~(3 << (DR_ENABLE_SIZE * (i)))

/* Set in DR7 the RW and LEN fields for the I'th debug register.  */
#define I386_DR_SET_RW_LEN(i,rwlen) \
  do { \
    dr_control_mirror &= ~(0x0f << (DR_CONTROL_SHIFT+DR_CONTROL_SIZE*(i)));   \
    dr_control_mirror |= ((rwlen) << (DR_CONTROL_SHIFT+DR_CONTROL_SIZE*(i))); \
  } while (0)

/* Get from DR7 the RW and LEN fields for the I'th debug register.  */
#define I386_DR_GET_RW_LEN(i) \
  ((dr_control_mirror >> (DR_CONTROL_SHIFT + DR_CONTROL_SIZE * (i))) & 0x0f)

/* Did the watchpoint whose address is in the I'th register break?  */
#define I386_DR_WATCH_HIT(i)	(dr_status_mirror & (1 << (i)))

/* A macro to loop over all debug registers.  */
#define ALL_DEBUG_REGISTERS(i)	for (i = 0; i < DR_NADDR; i++)

/* Mirror the inferior's DRi registers.  We keep the status and
   control registers separated because they don't hold addresses.  */
static CORE_ADDR dr_mirror[DR_NADDR];
static unsigned dr_status_mirror, dr_control_mirror;

/* Reference counts for each debug register.  */
static int dr_ref_count[DR_NADDR];

/* Whether or not to print the mirrored debug registers.  */
static int maint_show_dr;

/* Types of operations supported by i386_handle_nonaligned_watchpoint.  */
typedef enum { WP_INSERT, WP_REMOVE, WP_COUNT } i386_wp_op_t;

/* Internal functions.  */

/* Return the value of a 4-bit field for DR7 suitable for watching a
   region of LEN bytes for accesses of type TYPE.  LEN is assumed to
   have the value of 1, 2, or 4.  */
static unsigned i386_length_and_rw_bits (int len, enum target_hw_bp_type type);

/* Insert a watchpoint at address ADDR, which is assumed to be aligned
   according to the length of the region to watch.  LEN_RW_BITS is the
   value of the bit-field from DR7 which describes the length and
   access type of the region to be watched by this watchpoint.  Return
   0 on success, -1 on failure.  */
static int i386_insert_aligned_watchpoint (CORE_ADDR addr,
					   unsigned len_rw_bits);

/* Remove a watchpoint at address ADDR, which is assumed to be aligned
   according to the length of the region to watch.  LEN_RW_BITS is the
   value of the bits from DR7 which describes the length and access
   type of the region watched by this watchpoint.  Return 0 on
   success, -1 on failure.  */
static int i386_remove_aligned_watchpoint (CORE_ADDR addr,
					   unsigned len_rw_bits);

/* Insert or remove a (possibly non-aligned) watchpoint, or count the
   number of debug registers required to watch a region at address
   ADDR whose length is LEN for accesses of type TYPE.  Return 0 on
   successful insertion or removal, a positive number when queried
   about the number of registers, or -1 on failure.  If WHAT is not a
   valid value, bombs through internal_error.  */
static int i386_handle_nonaligned_watchpoint (i386_wp_op_t what,
					      CORE_ADDR addr, int len,
					      enum target_hw_bp_type type);

/* Implementation.  */

/* Clear the reference counts and forget everything we knew about the
   debug registers.  */

void
i386_cleanup_dregs (void)
{
  int i;

  ALL_DEBUG_REGISTERS(i)
    {
      dr_mirror[i] = 0;
      dr_ref_count[i] = 0;
    }
  dr_control_mirror = 0;
  dr_status_mirror  = 0;
}

#ifndef LINUX_CHILD_POST_STARTUP_INFERIOR

/* Reset all debug registers at each new startup to avoid missing
   watchpoints after restart.  */

void
child_post_startup_inferior (ptid_t ptid)
{
  i386_cleanup_dregs ();
}

#endif /* LINUX_CHILD_POST_STARTUP_INFERIOR */

/* Print the values of the mirrored debug registers.  This is called
   when maint_show_dr is non-zero.  To set that up, type "maint
   show-debug-regs" at GDB's prompt.  */

static void
i386_show_dr (const char *func, CORE_ADDR addr,
	      int len, enum target_hw_bp_type type)
{
  int i;

  puts_unfiltered (func);
  if (addr || len)
    printf_unfiltered (" (addr=%lx, len=%d, type=%s)",
		       /* This code is for ia32, so casting CORE_ADDR
			  to unsigned long should be okay.  */
		       (unsigned long)addr, len,
		       type == hw_write ? "data-write"
		       : (type == hw_read ? "data-read"
			  : (type == hw_access ? "data-read/write"
			     : (type == hw_execute ? "instruction-execute"
				/* FIXME: if/when I/O read/write
				   watchpoints are supported, add them
				   here.  */
				: "??unknown??"))));
  puts_unfiltered (":\n");
  printf_unfiltered ("\tCONTROL (DR7): %08x          STATUS (DR6): %08x\n",
		     dr_control_mirror, dr_status_mirror);
  ALL_DEBUG_REGISTERS(i)
    {
      printf_unfiltered ("\
\tDR%d: addr=0x%s, ref.count=%d  DR%d: addr=0x%s, ref.count=%d\n",
			 i, paddr(dr_mirror[i]), dr_ref_count[i],
			 i+1, paddr(dr_mirror[i+1]), dr_ref_count[i+1]);
      i++;
    }
}

/* Return the value of a 4-bit field for DR7 suitable for watching a
   region of LEN bytes for accesses of type TYPE.  LEN is assumed to
   have the value of 1, 2, or 4.  */

static unsigned
i386_length_and_rw_bits (int len, enum target_hw_bp_type type)
{
  unsigned rw;

  switch (type)
    {
      case hw_execute:
	rw = DR_RW_EXECUTE;
	break;
      case hw_write:
	rw = DR_RW_WRITE;
	break;
      case hw_read:
	/* The i386 doesn't support data-read watchpoints.  */
      case hw_access:
	rw = DR_RW_READ;
	break;
#if 0
	/* Not yet supported.  */
      case hw_io_access:
	rw = DR_RW_IORW;
	break;
#endif
      default:
	internal_error (__FILE__, __LINE__, "\
Invalid hardware breakpoint type %d in i386_length_and_rw_bits.\n",
			(int) type);
    }

  switch (len)
    {
      case 1:
	return (DR_LEN_1 | rw);
      case 2:
	return (DR_LEN_2 | rw);
      case 4:
	return (DR_LEN_4 | rw);
      case 8:
        if (TARGET_HAS_DR_LEN_8)
 	  return (DR_LEN_8 | rw);
      default:
	internal_error (__FILE__, __LINE__, "\
Invalid hardware breakpoint length %d in i386_length_and_rw_bits.\n", len);
    }
}

/* Insert a watchpoint at address ADDR, which is assumed to be aligned
   according to the length of the region to watch.  LEN_RW_BITS is the
   value of the bits from DR7 which describes the length and access
   type of the region to be watched by this watchpoint.  Return 0 on
   success, -1 on failure.  */

static int
i386_insert_aligned_watchpoint (CORE_ADDR addr, unsigned len_rw_bits)
{
  int i;

  /* First, look for an occupied debug register with the same address
     and the same RW and LEN definitions.  If we find one, we can
     reuse it for this watchpoint as well (and save a register).  */
  ALL_DEBUG_REGISTERS(i)
    {
      if (!I386_DR_VACANT (i)
	  && dr_mirror[i] == addr
	  && I386_DR_GET_RW_LEN (i) == len_rw_bits)
	{
	  dr_ref_count[i]++;
	  return 0;
	}
    }

  /* Next, look for a vacant debug register.  */
  ALL_DEBUG_REGISTERS(i)
    {
      if (I386_DR_VACANT (i))
	break;
    }

  /* No more debug registers!  */
  if (i >= DR_NADDR)
    return -1;

  /* Now set up the register I to watch our region.  */

  /* Record the info in our local mirrored array.  */
  dr_mirror[i] = addr;
  dr_ref_count[i] = 1;
  I386_DR_SET_RW_LEN (i, len_rw_bits);
  /* Note: we only enable the watchpoint locally, i.e. in the current
     task.  Currently, no i386 target allows or supports global
     watchpoints; however, if any target would want that in the
     future, GDB should probably provide a command to control whether
     to enable watchpoints globally or locally, and the code below
     should use global or local enable and slow-down flags as
     appropriate.  */
  I386_DR_LOCAL_ENABLE (i);
  dr_control_mirror |= DR_LOCAL_SLOWDOWN;
  dr_control_mirror &= I386_DR_CONTROL_MASK;

  /* Finally, actually pass the info to the inferior.  */
  I386_DR_LOW_SET_ADDR (i, addr);
  I386_DR_LOW_SET_CONTROL (dr_control_mirror);

  return 0;
}

/* Remove a watchpoint at address ADDR, which is assumed to be aligned
   according to the length of the region to watch.  LEN_RW_BITS is the
   value of the bits from DR7 which describes the length and access
   type of the region watched by this watchpoint.  Return 0 on
   success, -1 on failure.  */

static int
i386_remove_aligned_watchpoint (CORE_ADDR addr, unsigned len_rw_bits)
{
  int i, retval = -1;

  ALL_DEBUG_REGISTERS(i)
    {
      if (!I386_DR_VACANT (i)
	  && dr_mirror[i] == addr
	  && I386_DR_GET_RW_LEN (i) == len_rw_bits)
	{
	  if (--dr_ref_count[i] == 0) /* no longer in use? */
	    {
	      /* Reset our mirror.  */
	      dr_mirror[i] = 0;
	      I386_DR_DISABLE (i);
	      /* Reset it in the inferior.  */
	      I386_DR_LOW_SET_CONTROL (dr_control_mirror);
	      I386_DR_LOW_RESET_ADDR (i);
	    }
	  retval = 0;
	}
    }

  return retval;
}

/* Insert or remove a (possibly non-aligned) watchpoint, or count the
   number of debug registers required to watch a region at address
   ADDR whose length is LEN for accesses of type TYPE.  Return 0 on
   successful insertion or removal, a positive number when queried
   about the number of registers, or -1 on failure.  If WHAT is not a
   valid value, bombs through internal_error.  */

static int
i386_handle_nonaligned_watchpoint (i386_wp_op_t what, CORE_ADDR addr, int len,
				   enum target_hw_bp_type type)
{
  int retval = 0, status = 0;
  int max_wp_len = TARGET_HAS_DR_LEN_8 ? 8 : 4;

  static int size_try_array[8][8] =
  {
    {1, 1, 1, 1, 1, 1, 1, 1},	/* Trying size one.  */
    {2, 1, 2, 1, 2, 1, 2, 1},	/* Trying size two.  */
    {2, 1, 2, 1, 2, 1, 2, 1},	/* Trying size three.  */
    {4, 1, 2, 1, 4, 1, 2, 1},	/* Trying size four.  */
    {4, 1, 2, 1, 4, 1, 2, 1},	/* Trying size five.  */
    {4, 1, 2, 1, 4, 1, 2, 1},	/* Trying size six.  */
    {4, 1, 2, 1, 4, 1, 2, 1},	/* Trying size seven.  */
    {8, 1, 2, 1, 4, 1, 2, 1},	/* Trying size eight.  */
  };

  while (len > 0)
    {
      int align = addr % max_wp_len;
      /* Four (eigth on AMD64) is the maximum length a debug register
	 can watch.  */
      int try = (len > max_wp_len ? (max_wp_len - 1) : len - 1);
      int size = size_try_array[try][align];

      if (what == WP_COUNT)
	{
	  /* size_try_array[] is defined such that each iteration
	     through the loop is guaranteed to produce an address and a
	     size that can be watched with a single debug register.
	     Thus, for counting the registers required to watch a
	     region, we simply need to increment the count on each
	     iteration.  */
	  retval++;
	}
      else
	{
	  unsigned len_rw = i386_length_and_rw_bits (size, type);

	  if (what == WP_INSERT)
	    status = i386_insert_aligned_watchpoint (addr, len_rw);
	  else if (what == WP_REMOVE)
	    status = i386_remove_aligned_watchpoint (addr, len_rw);
	  else
	    internal_error (__FILE__, __LINE__, "\
Invalid value %d of operation in i386_handle_nonaligned_watchpoint.\n",
			    (int)what);
	  /* We keep the loop going even after a failure, because some
	     of the other aligned watchpoints might still succeed
	     (e.g. if they watch addresses that are already watched,
	     in which case we just increment the reference counts of
	     occupied debug registers).  If we break out of the loop
	     too early, we could cause those addresses watched by
	     other watchpoints to be disabled when breakpoint.c reacts
	     to our failure to insert this watchpoint and tries to
	     remove it.  */
	  if (status)
	    retval = status;
	}

      addr += size;
      len -= size;
    }

  return retval;
}

/* Insert a watchpoint to watch a memory region which starts at
   address ADDR and whose length is LEN bytes.  Watch memory accesses
   of the type TYPE.  Return 0 on success, -1 on failure.  */

int
i386_insert_watchpoint (CORE_ADDR addr, int len, int type)
{
  int retval;

  if (((len != 1 && len !=2 && len !=4) && !(TARGET_HAS_DR_LEN_8 && len == 8))
      || addr % len != 0)
    retval = i386_handle_nonaligned_watchpoint (WP_INSERT, addr, len, type);
  else
    {
      unsigned len_rw = i386_length_and_rw_bits (len, type);

      retval = i386_insert_aligned_watchpoint (addr, len_rw);
    }

  if (maint_show_dr)
    i386_show_dr ("insert_watchpoint", addr, len, type);

  return retval;
}

/* Remove a watchpoint that watched the memory region which starts at
   address ADDR, whose length is LEN bytes, and for accesses of the
   type TYPE.  Return 0 on success, -1 on failure.  */
int
i386_remove_watchpoint (CORE_ADDR addr, int len, int type)
{
  int retval;

  if (((len != 1 && len !=2 && len !=4) && !(TARGET_HAS_DR_LEN_8 && len == 8))
      || addr % len != 0)
    retval = i386_handle_nonaligned_watchpoint (WP_REMOVE, addr, len, type);
  else
    {
      unsigned len_rw = i386_length_and_rw_bits (len, type);

      retval = i386_remove_aligned_watchpoint (addr, len_rw);
    }

  if (maint_show_dr)
    i386_show_dr ("remove_watchpoint", addr, len, type);

  return retval;
}

/* Return non-zero if we can watch a memory region that starts at
   address ADDR and whose length is LEN bytes.  */

int
i386_region_ok_for_watchpoint (CORE_ADDR addr, int len)
{
  int nregs;

  /* Compute how many aligned watchpoints we would need to cover this
     region.  */
  nregs = i386_handle_nonaligned_watchpoint (WP_COUNT, addr, len, hw_write);
  return nregs <= DR_NADDR ? 1 : 0;
}

/* If the inferior has some watchpoint that triggered, return the
   address associated with that watchpoint.  Otherwise, return zero.  */

CORE_ADDR
i386_stopped_data_address (void)
{
  CORE_ADDR addr = 0;
  int i;

  dr_status_mirror = I386_DR_LOW_GET_STATUS ();

  ALL_DEBUG_REGISTERS(i)
    {
      if (I386_DR_WATCH_HIT (i)
	  /* This second condition makes sure DRi is set up for a data
	     watchpoint, not a hardware breakpoint.  The reason is
	     that GDB doesn't call the target_stopped_data_address
	     method except for data watchpoints.  In other words, I'm
	     being paranoid.  */
	  && I386_DR_GET_RW_LEN (i) != 0)
	{
	  addr = dr_mirror[i];
	  if (maint_show_dr)
	    i386_show_dr ("watchpoint_hit", addr, -1, hw_write);
	}
    }
  if (maint_show_dr && addr == 0)
    i386_show_dr ("stopped_data_addr", 0, 0, hw_write);

  return addr;
}

/* Return non-zero if the inferior has some break/watchpoint that
   triggered.  */

int
i386_stopped_by_hwbp (void)
{
  int i;

  dr_status_mirror = I386_DR_LOW_GET_STATUS ();
  if (maint_show_dr)
    i386_show_dr ("stopped_by_hwbp", 0, 0, hw_execute);

  ALL_DEBUG_REGISTERS(i)
    {
      if (I386_DR_WATCH_HIT (i))
	return 1;
    }

  return 0;
}

/* Insert a hardware-assisted breakpoint at address ADDR.  SHADOW is
   unused.  Return 0 on success, EBUSY on failure.  */
int
i386_insert_hw_breakpoint (CORE_ADDR addr, void *shadow)
{
  unsigned len_rw = i386_length_and_rw_bits (1, hw_execute);
  int retval = i386_insert_aligned_watchpoint (addr, len_rw) ? EBUSY : 0;

  if (maint_show_dr)
    i386_show_dr ("insert_hwbp", addr, 1, hw_execute);

  return retval;
}

/* Remove a hardware-assisted breakpoint at address ADDR.  SHADOW is
   unused.  Return 0 on success, -1 on failure.  */

int
i386_remove_hw_breakpoint (CORE_ADDR addr, void *shadow)
{
  unsigned len_rw = i386_length_and_rw_bits (1, hw_execute);
  int retval = i386_remove_aligned_watchpoint (addr, len_rw);

  if (maint_show_dr)
    i386_show_dr ("remove_hwbp", addr, 1, hw_execute);

  return retval;
}

#endif /* I386_USE_GENERIC_WATCHPOINTS */


/* Provide a prototype to silence -Wmissing-prototypes.  */
void _initialize_i386_nat (void);

void
_initialize_i386_nat (void)
{
#ifdef I386_USE_GENERIC_WATCHPOINTS
  /* A maintenance command to enable printing the internal DRi mirror
     variables.  */
  add_set_cmd ("show-debug-regs", class_maintenance,
	       var_boolean, (char *) &maint_show_dr,
	       "\
Set whether to show variables that mirror the x86 debug registers.\n\
Use \"on\" to enable, \"off\" to disable.\n\
If enabled, the debug registers values are shown when GDB inserts\n\
or removes a hardware breakpoint or watchpoint, and when the inferior\n\
triggers a breakpoint or watchpoint.", &maintenancelist);
#endif
}
