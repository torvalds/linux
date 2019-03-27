/* Find a variable's value in memory, for GDB, the GNU debugger.

   Copyright 1986, 1987, 1988, 1989, 1990, 1991, 1992, 1993, 1994,
   1995, 1996, 1997, 1998, 1999, 2000, 2001, 2003, 2004 Free Software
   Foundation, Inc.

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
#include "symtab.h"
#include "gdbtypes.h"
#include "frame.h"
#include "value.h"
#include "gdbcore.h"
#include "inferior.h"
#include "target.h"
#include "gdb_string.h"
#include "gdb_assert.h"
#include "floatformat.h"
#include "symfile.h"		/* for overlay functions */
#include "regcache.h"
#include "user-regs.h"
#include "block.h"

/* Basic byte-swapping routines.  GDB has needed these for a long time...
   All extract a target-format integer at ADDR which is LEN bytes long.  */

#if TARGET_CHAR_BIT != 8 || HOST_CHAR_BIT != 8
  /* 8 bit characters are a pretty safe assumption these days, so we
     assume it throughout all these swapping routines.  If we had to deal with
     9 bit characters, we would need to make len be in bits and would have
     to re-write these routines...  */
you lose
#endif

LONGEST
extract_signed_integer (const void *addr, int len)
{
  LONGEST retval;
  const unsigned char *p;
  const unsigned char *startaddr = addr;
  const unsigned char *endaddr = startaddr + len;

  if (len > (int) sizeof (LONGEST))
    error ("\
That operation is not available on integers of more than %d bytes.",
	   (int) sizeof (LONGEST));

  /* Start at the most significant end of the integer, and work towards
     the least significant.  */
  if (TARGET_BYTE_ORDER == BFD_ENDIAN_BIG)
    {
      p = startaddr;
      /* Do the sign extension once at the start.  */
      retval = ((LONGEST) * p ^ 0x80) - 0x80;
      for (++p; p < endaddr; ++p)
	retval = (retval << 8) | *p;
    }
  else
    {
      p = endaddr - 1;
      /* Do the sign extension once at the start.  */
      retval = ((LONGEST) * p ^ 0x80) - 0x80;
      for (--p; p >= startaddr; --p)
	retval = (retval << 8) | *p;
    }
  return retval;
}

ULONGEST
extract_unsigned_integer (const void *addr, int len)
{
  ULONGEST retval;
  const unsigned char *p;
  const unsigned char *startaddr = addr;
  const unsigned char *endaddr = startaddr + len;

  if (len > (int) sizeof (ULONGEST))
    error ("\
That operation is not available on integers of more than %d bytes.",
	   (int) sizeof (ULONGEST));

  /* Start at the most significant end of the integer, and work towards
     the least significant.  */
  retval = 0;
  if (TARGET_BYTE_ORDER == BFD_ENDIAN_BIG)
    {
      for (p = startaddr; p < endaddr; ++p)
	retval = (retval << 8) | *p;
    }
  else
    {
      for (p = endaddr - 1; p >= startaddr; --p)
	retval = (retval << 8) | *p;
    }
  return retval;
}

/* Sometimes a long long unsigned integer can be extracted as a
   LONGEST value.  This is done so that we can print these values
   better.  If this integer can be converted to a LONGEST, this
   function returns 1 and sets *PVAL.  Otherwise it returns 0.  */

int
extract_long_unsigned_integer (const void *addr, int orig_len, LONGEST *pval)
{
  char *p, *first_addr;
  int len;

  len = orig_len;
  if (TARGET_BYTE_ORDER == BFD_ENDIAN_BIG)
    {
      for (p = (char *) addr;
	   len > (int) sizeof (LONGEST) && p < (char *) addr + orig_len;
	   p++)
	{
	  if (*p == 0)
	    len--;
	  else
	    break;
	}
      first_addr = p;
    }
  else
    {
      first_addr = (char *) addr;
      for (p = (char *) addr + orig_len - 1;
	   len > (int) sizeof (LONGEST) && p >= (char *) addr;
	   p--)
	{
	  if (*p == 0)
	    len--;
	  else
	    break;
	}
    }

  if (len <= (int) sizeof (LONGEST))
    {
      *pval = (LONGEST) extract_unsigned_integer (first_addr,
						  sizeof (LONGEST));
      return 1;
    }

  return 0;
}


/* Treat the bytes at BUF as a pointer of type TYPE, and return the
   address it represents.  */
CORE_ADDR
extract_typed_address (const void *buf, struct type *type)
{
  if (TYPE_CODE (type) != TYPE_CODE_PTR
      && TYPE_CODE (type) != TYPE_CODE_REF)
    internal_error (__FILE__, __LINE__,
		    "extract_typed_address: "
		    "type is not a pointer or reference");

  return POINTER_TO_ADDRESS (type, buf);
}


void
store_signed_integer (void *addr, int len, LONGEST val)
{
  unsigned char *p;
  unsigned char *startaddr = (unsigned char *) addr;
  unsigned char *endaddr = startaddr + len;

  /* Start at the least significant end of the integer, and work towards
     the most significant.  */
  if (TARGET_BYTE_ORDER == BFD_ENDIAN_BIG)
    {
      for (p = endaddr - 1; p >= startaddr; --p)
	{
	  *p = val & 0xff;
	  val >>= 8;
	}
    }
  else
    {
      for (p = startaddr; p < endaddr; ++p)
	{
	  *p = val & 0xff;
	  val >>= 8;
	}
    }
}

void
store_unsigned_integer (void *addr, int len, ULONGEST val)
{
  unsigned char *p;
  unsigned char *startaddr = (unsigned char *) addr;
  unsigned char *endaddr = startaddr + len;

  /* Start at the least significant end of the integer, and work towards
     the most significant.  */
  if (TARGET_BYTE_ORDER == BFD_ENDIAN_BIG)
    {
      for (p = endaddr - 1; p >= startaddr; --p)
	{
	  *p = val & 0xff;
	  val >>= 8;
	}
    }
  else
    {
      for (p = startaddr; p < endaddr; ++p)
	{
	  *p = val & 0xff;
	  val >>= 8;
	}
    }
}

/* Store the address ADDR as a pointer of type TYPE at BUF, in target
   form.  */
void
store_typed_address (void *buf, struct type *type, CORE_ADDR addr)
{
  if (TYPE_CODE (type) != TYPE_CODE_PTR
      && TYPE_CODE (type) != TYPE_CODE_REF)
    internal_error (__FILE__, __LINE__,
		    "store_typed_address: "
		    "type is not a pointer or reference");

  ADDRESS_TO_POINTER (type, buf, addr);
}



/* Return a `value' with the contents of (virtual or cooked) register
   REGNUM as found in the specified FRAME.  The register's type is
   determined by register_type().

   NOTE: returns NULL if register value is not available.  Caller will
   check return value or die!  */

struct value *
value_of_register (int regnum, struct frame_info *frame)
{
  CORE_ADDR addr;
  int optim;
  struct value *reg_val;
  int realnum;
  char raw_buffer[MAX_REGISTER_SIZE];
  enum lval_type lval;

  /* User registers lie completely outside of the range of normal
     registers.  Catch them early so that the target never sees them.  */
  if (regnum >= NUM_REGS + NUM_PSEUDO_REGS)
    return value_of_user_reg (regnum, frame);

  frame_register (frame, regnum, &optim, &lval, &addr, &realnum, raw_buffer);

  /* FIXME: cagney/2002-05-15: This test is just bogus.

     It indicates that the target failed to supply a value for a
     register because it was "not available" at this time.  Problem
     is, the target still has the register and so get saved_register()
     may be returning a value saved on the stack.  */

  if (register_cached (regnum) < 0)
    return NULL;		/* register value not available */

  reg_val = allocate_value (register_type (current_gdbarch, regnum));

  /* Convert raw data to virtual format if necessary.  */

  if (DEPRECATED_REGISTER_CONVERTIBLE_P ()
      && DEPRECATED_REGISTER_CONVERTIBLE (regnum))
    {
      DEPRECATED_REGISTER_CONVERT_TO_VIRTUAL (regnum, register_type (current_gdbarch, regnum),
					      raw_buffer, VALUE_CONTENTS_RAW (reg_val));
    }
  else if (DEPRECATED_REGISTER_RAW_SIZE (regnum) == DEPRECATED_REGISTER_VIRTUAL_SIZE (regnum))
    memcpy (VALUE_CONTENTS_RAW (reg_val), raw_buffer,
	    DEPRECATED_REGISTER_RAW_SIZE (regnum));
  else
    internal_error (__FILE__, __LINE__,
		    "Register \"%s\" (%d) has conflicting raw (%d) and virtual (%d) size",
		    REGISTER_NAME (regnum),
		    regnum,
		    DEPRECATED_REGISTER_RAW_SIZE (regnum),
		    DEPRECATED_REGISTER_VIRTUAL_SIZE (regnum));
  VALUE_LVAL (reg_val) = lval;
  VALUE_ADDRESS (reg_val) = addr;
  VALUE_REGNO (reg_val) = regnum;
  VALUE_OPTIMIZED_OUT (reg_val) = optim;
  return reg_val;
}

/* Given a pointer of type TYPE in target form in BUF, return the
   address it represents.  */
CORE_ADDR
unsigned_pointer_to_address (struct type *type, const void *buf)
{
  return extract_unsigned_integer (buf, TYPE_LENGTH (type));
}

CORE_ADDR
signed_pointer_to_address (struct type *type, const void *buf)
{
  return extract_signed_integer (buf, TYPE_LENGTH (type));
}

/* Given an address, store it as a pointer of type TYPE in target
   format in BUF.  */
void
unsigned_address_to_pointer (struct type *type, void *buf, CORE_ADDR addr)
{
  store_unsigned_integer (buf, TYPE_LENGTH (type), addr);
}

void
address_to_signed_pointer (struct type *type, void *buf, CORE_ADDR addr)
{
  store_signed_integer (buf, TYPE_LENGTH (type), addr);
}

/* Will calling read_var_value or locate_var_value on SYM end
   up caring what frame it is being evaluated relative to?  SYM must
   be non-NULL.  */
int
symbol_read_needs_frame (struct symbol *sym)
{
  switch (SYMBOL_CLASS (sym))
    {
      /* All cases listed explicitly so that gcc -Wall will detect it if
         we failed to consider one.  */
    case LOC_COMPUTED:
    case LOC_COMPUTED_ARG:
      /* FIXME: cagney/2004-01-26: It should be possible to
	 unconditionally call the SYMBOL_OPS method when available.
	 Unfortunately DWARF 2 stores the frame-base (instead of the
	 function) location in a function's symbol.  Oops!  For the
	 moment enable this when/where applicable.  */
      return SYMBOL_OPS (sym)->read_needs_frame (sym);

    case LOC_REGISTER:
    case LOC_ARG:
    case LOC_REF_ARG:
    case LOC_REGPARM:
    case LOC_REGPARM_ADDR:
    case LOC_LOCAL:
    case LOC_LOCAL_ARG:
    case LOC_BASEREG:
    case LOC_BASEREG_ARG:
    case LOC_HP_THREAD_LOCAL_STATIC:
      return 1;

    case LOC_UNDEF:
    case LOC_CONST:
    case LOC_STATIC:
    case LOC_INDIRECT:
    case LOC_TYPEDEF:

    case LOC_LABEL:
      /* Getting the address of a label can be done independently of the block,
         even if some *uses* of that address wouldn't work so well without
         the right frame.  */

    case LOC_BLOCK:
    case LOC_CONST_BYTES:
    case LOC_UNRESOLVED:
    case LOC_OPTIMIZED_OUT:
      return 0;
    }
  return 1;
}

/* Given a struct symbol for a variable,
   and a stack frame id, read the value of the variable
   and return a (pointer to a) struct value containing the value. 
   If the variable cannot be found, return a zero pointer.
   If FRAME is NULL, use the deprecated_selected_frame.  */

struct value *
read_var_value (struct symbol *var, struct frame_info *frame)
{
  struct value *v;
  struct type *type = SYMBOL_TYPE (var);
  CORE_ADDR addr;
  int len;

  v = allocate_value (type);
  VALUE_LVAL (v) = lval_memory;	/* The most likely possibility.  */
  VALUE_BFD_SECTION (v) = SYMBOL_BFD_SECTION (var);

  len = TYPE_LENGTH (type);


  /* FIXME drow/2003-09-06: this call to the selected frame should be
     pushed upwards to the callers.  */
  if (frame == NULL)
    frame = deprecated_safe_get_selected_frame ();

  switch (SYMBOL_CLASS (var))
    {
    case LOC_CONST:
      /* Put the constant back in target format.  */
      store_signed_integer (VALUE_CONTENTS_RAW (v), len,
			    (LONGEST) SYMBOL_VALUE (var));
      VALUE_LVAL (v) = not_lval;
      return v;

    case LOC_LABEL:
      /* Put the constant back in target format.  */
      if (overlay_debugging)
	{
	  CORE_ADDR addr
	    = symbol_overlayed_address (SYMBOL_VALUE_ADDRESS (var),
					SYMBOL_BFD_SECTION (var));
	  store_typed_address (VALUE_CONTENTS_RAW (v), type, addr);
	}
      else
	store_typed_address (VALUE_CONTENTS_RAW (v), type,
			      SYMBOL_VALUE_ADDRESS (var));
      VALUE_LVAL (v) = not_lval;
      return v;

    case LOC_CONST_BYTES:
      {
	char *bytes_addr;
	bytes_addr = SYMBOL_VALUE_BYTES (var);
	memcpy (VALUE_CONTENTS_RAW (v), bytes_addr, len);
	VALUE_LVAL (v) = not_lval;
	return v;
      }

    case LOC_STATIC:
      if (overlay_debugging)
	addr = symbol_overlayed_address (SYMBOL_VALUE_ADDRESS (var),
					 SYMBOL_BFD_SECTION (var));
      else
	addr = SYMBOL_VALUE_ADDRESS (var);
      break;

    case LOC_INDIRECT:
      {
	/* The import slot does not have a real address in it from the
	   dynamic loader (dld.sl on HP-UX), if the target hasn't
	   begun execution yet, so check for that. */
	CORE_ADDR locaddr;
	struct value *loc;
	if (!target_has_execution)
	  error ("\
Attempt to access variable defined in different shared object or load module when\n\
addresses have not been bound by the dynamic loader. Try again when executable is running.");

	locaddr = SYMBOL_VALUE_ADDRESS (var);
	loc = value_at (lookup_pointer_type (type), locaddr, NULL);
	addr = value_as_address (loc);
      }

    case LOC_ARG:
      if (frame == NULL)
	return 0;
      addr = get_frame_args_address (frame);
      if (!addr)
	return 0;
      addr += SYMBOL_VALUE (var);
      break;

    case LOC_REF_ARG:
      {
	struct value *ref;
	CORE_ADDR argref;
	if (frame == NULL)
	  return 0;
	argref = get_frame_args_address (frame);
	if (!argref)
	  return 0;
	argref += SYMBOL_VALUE (var);
	ref = value_at (lookup_pointer_type (type), argref, NULL);
	addr = value_as_address (ref);
	break;
      }

    case LOC_LOCAL:
    case LOC_LOCAL_ARG:
      if (frame == NULL)
	return 0;
      addr = get_frame_locals_address (frame);
      addr += SYMBOL_VALUE (var);
      break;

    case LOC_BASEREG:
    case LOC_BASEREG_ARG:
    case LOC_HP_THREAD_LOCAL_STATIC:
      {
	struct value *regval;

	regval = value_from_register (lookup_pointer_type (type),
				      SYMBOL_BASEREG (var), frame);
	if (regval == NULL)
	  error ("Value of base register not available.");
	addr = value_as_address (regval);
	addr += SYMBOL_VALUE (var);
	break;
      }

    case LOC_TYPEDEF:
      error ("Cannot look up value of a typedef");
      break;

    case LOC_BLOCK:
      if (overlay_debugging)
	VALUE_ADDRESS (v) = symbol_overlayed_address
	  (BLOCK_START (SYMBOL_BLOCK_VALUE (var)), SYMBOL_BFD_SECTION (var));
      else
	VALUE_ADDRESS (v) = BLOCK_START (SYMBOL_BLOCK_VALUE (var));
      return v;

    case LOC_REGISTER:
    case LOC_REGPARM:
    case LOC_REGPARM_ADDR:
      {
	struct block *b;
	int regno = SYMBOL_VALUE (var);
	struct value *regval;

	if (frame == NULL)
	  return 0;
	b = get_frame_block (frame, 0);

	if (SYMBOL_CLASS (var) == LOC_REGPARM_ADDR)
	  {
	    regval = value_from_register (lookup_pointer_type (type),
					  regno,
					  frame);

	    if (regval == NULL)
	      error ("Value of register variable not available.");

	    addr = value_as_address (regval);
	    VALUE_LVAL (v) = lval_memory;
	  }
	else
	  {
	    regval = value_from_register (type, regno, frame);

	    if (regval == NULL)
	      error ("Value of register variable not available.");
	    return regval;
	  }
      }
      break;

    case LOC_COMPUTED:
    case LOC_COMPUTED_ARG:
      /* FIXME: cagney/2004-01-26: It should be possible to
	 unconditionally call the SYMBOL_OPS method when available.
	 Unfortunately DWARF 2 stores the frame-base (instead of the
	 function) location in a function's symbol.  Oops!  For the
	 moment enable this when/where applicable.  */
      if (frame == 0 && SYMBOL_OPS (var)->read_needs_frame (var))
	return 0;
      return SYMBOL_OPS (var)->read_variable (var, frame);

    case LOC_UNRESOLVED:
      {
	struct minimal_symbol *msym;

	msym = lookup_minimal_symbol (DEPRECATED_SYMBOL_NAME (var), NULL, NULL);
	if (msym == NULL)
	  return 0;
	if (overlay_debugging)
	  addr = symbol_overlayed_address (SYMBOL_VALUE_ADDRESS (msym),
					   SYMBOL_BFD_SECTION (msym));
	else
	  addr = SYMBOL_VALUE_ADDRESS (msym);
      }
      break;

    case LOC_OPTIMIZED_OUT:
      VALUE_LVAL (v) = not_lval;
      VALUE_OPTIMIZED_OUT (v) = 1;
      return v;

    default:
      error ("Cannot look up value of a botched symbol.");
      break;
    }

  VALUE_ADDRESS (v) = addr;
  VALUE_LAZY (v) = 1;
  return v;
}

/* Return a value of type TYPE, stored in register REGNUM, in frame
   FRAME.

   NOTE: returns NULL if register value is not available.
   Caller will check return value or die!  */

struct value *
value_from_register (struct type *type, int regnum, struct frame_info *frame)
{
  struct gdbarch *gdbarch = get_frame_arch (frame);
  struct value *v = allocate_value (type);
  CHECK_TYPEDEF (type);

  if (TYPE_LENGTH (type) == 0)
    {
      /* It doesn't matter much what we return for this: since the
         length is zero, it could be anything.  But if allowed to see
         a zero-length type, the register-finding loop below will set
         neither mem_stor nor reg_stor, and then report an internal
         error.  

         Zero-length types can legitimately arise from declarations
         like 'struct {}' (a GCC extension, not valid ISO C).  GDB may
         also create them when it finds bogus debugging information;
         for example, in GCC 2.95.4 and binutils 2.11.93.0.2, the
         STABS BINCL->EXCL compression process can create bad type
         numbers.  GDB reads these as TYPE_CODE_UNDEF types, with zero
         length.  (That bug is actually the only known way to get a
         zero-length value allocated to a register --- which is what
         it takes to make it here.)

         We'll just attribute the value to the original register.  */
      VALUE_LVAL (v) = lval_register;
      VALUE_ADDRESS (v) = regnum;
      VALUE_REGNO (v) = regnum;
    }
  else if (CONVERT_REGISTER_P (regnum, type))
    {
      /* The ISA/ABI need to something weird when obtaining the
         specified value from this register.  It might need to
         re-order non-adjacent, starting with REGNUM (see MIPS and
         i386).  It might need to convert the [float] register into
         the corresponding [integer] type (see Alpha).  The assumption
         is that REGISTER_TO_VALUE populates the entire value
         including the location.  */
      REGISTER_TO_VALUE (frame, regnum, type, VALUE_CONTENTS_RAW (v));
      VALUE_LVAL (v) = lval_reg_frame_relative;
      VALUE_FRAME_ID (v) = get_frame_id (frame);
      VALUE_FRAME_REGNUM (v) = regnum;
    }
  else
    {
      int local_regnum;
      int mem_stor = 0, reg_stor = 0;
      int mem_tracking = 1;
      CORE_ADDR last_addr = 0;
      CORE_ADDR first_addr = 0;
      int first_realnum = regnum;
      int len = TYPE_LENGTH (type);
      int value_bytes_copied;
      int optimized = 0;
      char *value_bytes = (char *) alloca (len + MAX_REGISTER_SIZE);

      /* Copy all of the data out, whereever it may be.  */
      for (local_regnum = regnum, value_bytes_copied = 0;
	   value_bytes_copied < len;
	   (value_bytes_copied += DEPRECATED_REGISTER_RAW_SIZE (local_regnum),
	    ++local_regnum))
	{
	  int realnum;
	  int optim;
	  enum lval_type lval;
	  CORE_ADDR addr;
	  frame_register (frame, local_regnum, &optim, &lval, &addr,
			  &realnum, value_bytes + value_bytes_copied);
	  optimized += optim;
	  if (register_cached (local_regnum) == -1)
	    return NULL;	/* register value not available */
	  
	  if (regnum == local_regnum)
	    {
	      first_addr = addr;
	      first_realnum = realnum;
	    }
	  if (lval == lval_register)
	    reg_stor++;
	  else
	    {
	      mem_stor++;
	      
	      mem_tracking = (mem_tracking
			      && (regnum == local_regnum
				  || addr == last_addr));
	    }
	  last_addr = addr;
	}
      
      /* FIXME: cagney/2003-06-04: Shouldn't this always use
         lval_reg_frame_relative?  If it doesn't and the register's
         location changes (say after a resume) then this value is
         going to have wrong information.  */
      if ((reg_stor && mem_stor)
	  || (mem_stor && !mem_tracking))
	/* Mixed storage; all of the hassle we just went through was
	   for some good purpose.  */
	{
	  VALUE_LVAL (v) = lval_reg_frame_relative;
	  VALUE_FRAME_ID (v) = get_frame_id (frame);
	  VALUE_FRAME_REGNUM (v) = regnum;
	}
      else if (mem_stor)
	{
	  VALUE_LVAL (v) = lval_memory;
	  VALUE_ADDRESS (v) = first_addr;
	}
      else if (reg_stor)
	{
	  VALUE_LVAL (v) = lval_register;
	  VALUE_ADDRESS (v) = first_addr;
	  VALUE_REGNO (v) = first_realnum;
	}
      else
	internal_error (__FILE__, __LINE__,
			"value_from_register: Value not stored anywhere!");
      
      VALUE_OPTIMIZED_OUT (v) = optimized;
      
      /* Any structure stored in more than one register will always be
         an integral number of registers.  Otherwise, you need to do
         some fiddling with the last register copied here for little
         endian machines.  */
      if (TARGET_BYTE_ORDER == BFD_ENDIAN_BIG
	  && len < DEPRECATED_REGISTER_RAW_SIZE (regnum))
	/* Big-endian, and we want less than full size.  */
	VALUE_OFFSET (v) = DEPRECATED_REGISTER_RAW_SIZE (regnum) - len;
      else
	VALUE_OFFSET (v) = 0;
      memcpy (VALUE_CONTENTS_RAW (v), value_bytes + VALUE_OFFSET (v), len);
    }
  return v;
}


/* Given a struct symbol for a variable or function,
   and a stack frame id, 
   return a (pointer to a) struct value containing the properly typed
   address.  */

struct value *
locate_var_value (struct symbol *var, struct frame_info *frame)
{
  CORE_ADDR addr = 0;
  struct type *type = SYMBOL_TYPE (var);
  struct value *lazy_value;

  /* Evaluate it first; if the result is a memory address, we're fine.
     Lazy evaluation pays off here. */

  lazy_value = read_var_value (var, frame);
  if (lazy_value == 0)
    error ("Address of \"%s\" is unknown.", SYMBOL_PRINT_NAME (var));

  if (VALUE_LAZY (lazy_value)
      || TYPE_CODE (type) == TYPE_CODE_FUNC)
    {
      struct value *val;

      addr = VALUE_ADDRESS (lazy_value);
      val = value_from_pointer (lookup_pointer_type (type), addr);
      VALUE_BFD_SECTION (val) = VALUE_BFD_SECTION (lazy_value);
      return val;
    }

  /* Not a memory address; check what the problem was.  */
  switch (VALUE_LVAL (lazy_value))
    {
    case lval_register:
	gdb_assert (REGISTER_NAME (VALUE_REGNO (lazy_value)) != NULL
	            && *REGISTER_NAME (VALUE_REGNO (lazy_value)) != '\0');
      error("Address requested for identifier "
	    "\"%s\" which is in register $%s",
            SYMBOL_PRINT_NAME (var), 
	    REGISTER_NAME (VALUE_REGNO (lazy_value)));
      break;

    case lval_reg_frame_relative:
	gdb_assert (REGISTER_NAME (VALUE_FRAME_REGNUM (lazy_value)) != NULL
	            && *REGISTER_NAME (VALUE_FRAME_REGNUM (lazy_value)) != '\0');
      error("Address requested for identifier "
	    "\"%s\" which is in frame register $%s",
            SYMBOL_PRINT_NAME (var), 
	    REGISTER_NAME (VALUE_FRAME_REGNUM (lazy_value)));
      break;

    default:
      error ("Can't take address of \"%s\" which isn't an lvalue.",
	     SYMBOL_PRINT_NAME (var));
      break;
    }
  return 0;			/* For lint -- never reached */
}
