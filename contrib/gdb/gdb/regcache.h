/* Cache and manage the values of registers for GDB, the GNU debugger.

   Copyright 1986, 1987, 1989, 1991, 1994, 1995, 1996, 1998, 2000,
   2001, 2002 Free Software Foundation, Inc.

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

#ifndef REGCACHE_H
#define REGCACHE_H

struct regcache;
struct gdbarch;

extern struct regcache *current_regcache;

void regcache_xfree (struct regcache *regcache);
struct cleanup *make_cleanup_regcache_xfree (struct regcache *regcache);
struct regcache *regcache_xmalloc (struct gdbarch *gdbarch);

/* Return REGCACHE's architecture.  */

extern struct gdbarch *get_regcache_arch (const struct regcache *regcache);

/* Transfer a raw register [0..NUM_REGS) between core-gdb and the
   regcache. */

void regcache_raw_read (struct regcache *regcache, int rawnum, void *buf);
void regcache_raw_write (struct regcache *regcache, int rawnum,
			 const void *buf);
extern void regcache_raw_read_signed (struct regcache *regcache,
				      int regnum, LONGEST *val);
extern void regcache_raw_read_unsigned (struct regcache *regcache,
					int regnum, ULONGEST *val);
extern void regcache_raw_write_signed (struct regcache *regcache,
				       int regnum, LONGEST val);
extern void regcache_raw_write_unsigned (struct regcache *regcache,
					 int regnum, ULONGEST val);

/* Partial transfer of a raw registers.  These perform read, modify,
   write style operations.  */

void regcache_raw_read_part (struct regcache *regcache, int regnum,
			     int offset, int len, void *buf);
void regcache_raw_write_part (struct regcache *regcache, int regnum,
			      int offset, int len, const void *buf);

int regcache_valid_p (struct regcache *regcache, int regnum);

/* Transfer a cooked register [0..NUM_REGS+NUM_PSEUDO_REGS).  */
void regcache_cooked_read (struct regcache *regcache, int rawnum, void *buf);
void regcache_cooked_write (struct regcache *regcache, int rawnum,
			    const void *buf);

/* NOTE: cagney/2002-08-13: At present GDB has no reliable mechanism
   for indicating when a ``cooked'' register was constructed from
   invalid or unavailable ``raw'' registers.  One fairly easy way of
   adding such a mechanism would be for the cooked functions to return
   a register valid indication.  Given the possibility of such a
   change, the extract functions below use a reference parameter,
   rather than a function result.  */

/* Read a register as a signed/unsigned quantity.  */
extern void regcache_cooked_read_signed (struct regcache *regcache,
					 int regnum, LONGEST *val);
extern void regcache_cooked_read_unsigned (struct regcache *regcache,
					   int regnum, ULONGEST *val);
extern void regcache_cooked_write_signed (struct regcache *regcache,
					  int regnum, LONGEST val);
extern void regcache_cooked_write_unsigned (struct regcache *regcache,
					    int regnum, ULONGEST val);

/* Partial transfer of a cooked register.  These perform read, modify,
   write style operations.  */

void regcache_cooked_read_part (struct regcache *regcache, int regnum,
				int offset, int len, void *buf);
void regcache_cooked_write_part (struct regcache *regcache, int regnum,
				 int offset, int len, const void *buf);

/* Transfer a raw register [0..NUM_REGS) between the regcache and the
   target.  These functions are called by the target in response to a
   target_fetch_registers() or target_store_registers().  */

extern void supply_register (int regnum, const void *val);
extern void regcache_collect (int regnum, void *buf);
extern void regcache_raw_supply (struct regcache *regcache,
				 int regnum, const void *buf);
extern void regcache_raw_collect (const struct regcache *regcache,
				  int regnum, void *buf);


/* The register's ``offset''.

   FIXME: cagney/2002-11-07: The frame_register() function, when
   specifying the real location of a register, does so using that
   registers offset in the register cache.  That offset is then used
   by valops.c to determine the location of the register.  The code
   should instead use the register's number and a location expression
   to describe a value spread across multiple registers or memory.  */

extern int register_offset_hack (struct gdbarch *gdbarch, int regnum);


/* The type of a register.  This function is slightly more efficient
   then its gdbarch vector counterpart since it returns a precomputed
   value stored in a table.

   NOTE: cagney/2002-08-17: The original macro was called
   DEPRECATED_REGISTER_VIRTUAL_TYPE.  This was because the register
   could have different raw and cooked (nee virtual) representations.
   The CONVERTABLE methods being used to convert between the two
   representations.  Current code does not do this.  Instead, the
   first [0..NUM_REGS) registers are 1:1 raw:cooked, and the type
   exactly describes the register's representation.  Consequently, the
   ``virtual'' has been dropped.

   FIXME: cagney/2002-08-17: A number of architectures, including the
   MIPS, are currently broken in this regard.  */

extern struct type *register_type (struct gdbarch *gdbarch, int regnum);


/* Return the size of register REGNUM.  All registers should have only
   one size.

   FIXME: cagney/2003-02-28:

   Unfortunately, thanks to some legacy architectures, this doesn't
   hold.  A register's cooked (nee virtual) and raw size can differ
   (see MIPS).  Such architectures should be using different register
   numbers for the different sized views of identical registers.

   Anyway, the up-shot is that, until that mess is fixed, core code
   can end up being very confused - should the RAW or VIRTUAL size be
   used?  As a rule of thumb, use DEPRECATED_REGISTER_VIRTUAL_SIZE in
   cooked code, but with the comment:

   OK: REGISTER_VIRTUAL_SIZE

   or just

   OK

   appended to the end of the line.  */
   
extern int register_size (struct gdbarch *gdbarch, int regnum);


/* Save/restore a register cache.  The set of registers saved /
   restored into the DST regcache determined by the save_reggroup /
   restore_reggroup respectively.  COOKED_READ returns zero iff the
   register's value can't be returned.  */

typedef int (regcache_cooked_read_ftype) (void *src, int regnum, void *buf);

extern void regcache_save (struct regcache *dst,
			   regcache_cooked_read_ftype *cooked_read,
			   void *src);
extern void regcache_restore (struct regcache *dst,
			      regcache_cooked_read_ftype *cooked_read,
			      void *src);

/* Copy/duplicate the contents of a register cache.  By default, the
   operation is pass-through.  Writes to DST and reads from SRC will
   go through to the target.

   The ``cpy'' functions can not have overlapping SRC and DST buffers.

   ``no passthrough'' versions do not go through to the target.  They
   only transfer values already in the cache.  */

extern struct regcache *regcache_dup (struct regcache *regcache);
extern struct regcache *regcache_dup_no_passthrough (struct regcache *regcache);
extern void regcache_cpy (struct regcache *dest, struct regcache *src);
extern void regcache_cpy_no_passthrough (struct regcache *dest, struct regcache *src);

/* NOTE: cagney/2002-11-02: The below have been superseded by the
   regcache_cooked_*() functions found above, and the frame_*()
   functions found in "frame.h".  Take care though, often more than a
   simple substitution is required when updating the code.  The
   change, as far as practical, should avoid adding references to
   global variables (e.g., current_regcache, current_frame,
   current_gdbarch or deprecated_selected_frame) and instead refer to
   the FRAME or REGCACHE that has been passed into the containing
   function as parameters.  Consequently, the change typically
   involves modifying the containing function so that it takes a FRAME
   or REGCACHE parameter.  In the case of an architecture vector
   method, there should already be a non-deprecated variant that is
   parameterized with FRAME or REGCACHE.  */

extern char *deprecated_grub_regcache_for_registers (struct regcache *);
extern void deprecated_read_register_gen (int regnum, char *myaddr);
extern void deprecated_write_register_gen (int regnum, char *myaddr);
extern void deprecated_read_register_bytes (int regbyte, char *myaddr,
					    int len);
extern void deprecated_write_register_bytes (int regbyte, char *myaddr,
					     int len);

/* Character array containing the current state of each register
   (unavailable<0, invalid=0, valid>0) for the most recently
   referenced thread.  This global is often found in close proximity
   to code that is directly manipulating the deprecated_registers[]
   array.  In such cases, it should be possible to replace the lot
   with a call to supply_register().  If you find yourself in dire
   straits, still needing access to the cache status bit, the
   regcache_valid_p() and set_register_cached() functions are
   available.  */
extern signed char *deprecated_register_valid;

/* Character array containing an image of the inferior programs'
   registers for the most recently referenced thread.

   NOTE: cagney/2002-11-14: Target side code should be using
   supply_register() and/or regcache_collect() while architecture side
   code should use the more generic regcache methods.  */

extern char *deprecated_registers;

/* NOTE: cagney/2002-11-05: This function, and its co-conspirator
   deprecated_registers[], have been superseeded by supply_register().  */
extern void deprecated_registers_fetched (void);

extern int register_cached (int regnum);

extern void set_register_cached (int regnum, int state);

extern void registers_changed (void);


/* Rename to read_unsigned_register()? */
extern ULONGEST read_register (int regnum);

/* Rename to read_unsigned_register_pid()? */
extern ULONGEST read_register_pid (int regnum, ptid_t ptid);

extern void write_register (int regnum, LONGEST val);

extern void write_register_pid (int regnum, CORE_ADDR val, ptid_t ptid);

#endif /* REGCACHE_H */
