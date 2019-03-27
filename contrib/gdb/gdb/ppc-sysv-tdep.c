/* Target-dependent code for PowerPC systems using the SVR4 ABI
   for GDB, the GNU debugger.

   Copyright 2000, 2001, 2002, 2003 Free Software Foundation, Inc.

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
#include "gdbcore.h"
#include "inferior.h"
#include "regcache.h"
#include "value.h"
#include "gdb_string.h"
#include "gdb_assert.h"
#include "ppc-tdep.h"
#include "target.h"
#include "objfiles.h"

/* Pass the arguments in either registers, or in the stack. Using the
   ppc sysv ABI, the first eight words of the argument list (that might
   be less than eight parameters if some parameters occupy more than one
   word) are passed in r3..r10 registers.  float and double parameters are
   passed in fpr's, in addition to that. Rest of the parameters if any
   are passed in user stack. 

   If the function is returning a structure, then the return address is passed
   in r3, then the first 7 words of the parametes can be passed in registers,
   starting from r4. */

CORE_ADDR
ppc_sysv_abi_push_dummy_call (struct gdbarch *gdbarch, CORE_ADDR func_addr,
			      struct regcache *regcache, CORE_ADDR bp_addr,
			      int nargs, struct value **args, CORE_ADDR sp,
			      int struct_return, CORE_ADDR struct_addr)
{
  struct gdbarch_tdep *tdep = gdbarch_tdep (current_gdbarch);
  const CORE_ADDR saved_sp = read_sp ();
  int argspace = 0;		/* 0 is an initial wrong guess.  */
  int write_pass;

  /* Go through the argument list twice.

     Pass 1: Figure out how much new stack space is required for
     arguments and pushed values.  Unlike the PowerOpen ABI, the SysV
     ABI doesn't reserve any extra space for parameters which are put
     in registers, but does always push structures and then pass their
     address.

     Pass 2: Replay the same computation but this time also write the
     values out to the target.  */

  for (write_pass = 0; write_pass < 2; write_pass++)
    {
      int argno;
      /* Next available floating point register for float and double
         arguments.  */
      int freg = 1;
      /* Next available general register for non-float, non-vector
         arguments.  */
      int greg = 3;
      /* Next available vector register for vector arguments.  */
      int vreg = 2;
      /* Arguments start above the "LR save word" and "Back chain".  */
      int argoffset = 2 * tdep->wordsize;
      /* Structures start after the arguments.  */
      int structoffset = argoffset + argspace;

      /* If the function is returning a `struct', then the first word
         (which will be passed in r3) is used for struct return
         address.  In that case we should advance one word and start
         from r4 register to copy parameters.  */
      if (struct_return)
	{
	  if (write_pass)
	    regcache_cooked_write_signed (regcache,
					  tdep->ppc_gp0_regnum + greg,
					  struct_addr);
	  greg++;
	}

      for (argno = 0; argno < nargs; argno++)
	{
	  struct value *arg = args[argno];
	  struct type *type = check_typedef (VALUE_TYPE (arg));
	  int len = TYPE_LENGTH (type);
	  char *val = VALUE_CONTENTS (arg);

	  if (TYPE_CODE (type) == TYPE_CODE_FLT
	      && ppc_floating_point_unit_p (current_gdbarch) && len <= 8)
	    {
	      /* Floating point value converted to "double" then
	         passed in an FP register, when the registers run out,
	         8 byte aligned stack is used.  */
	      if (freg <= 8)
		{
		  if (write_pass)
		    {
		      /* Always store the floating point value using
		         the register's floating-point format.  */
		      char regval[MAX_REGISTER_SIZE];
		      struct type *regtype
			= register_type (gdbarch, FP0_REGNUM + freg);
		      convert_typed_floating (val, type, regval, regtype);
		      regcache_cooked_write (regcache, FP0_REGNUM + freg,
					     regval);
		    }
		  freg++;
		}
	      else
		{
		  /* SysV ABI converts floats to doubles before
		     writing them to an 8 byte aligned stack location.  */
		  argoffset = align_up (argoffset, 8);
		  if (write_pass)
		    {
		      char memval[8];
		      struct type *memtype;
		      switch (TARGET_BYTE_ORDER)
			{
			case BFD_ENDIAN_BIG:
			  memtype = builtin_type_ieee_double_big;
			  break;
			case BFD_ENDIAN_LITTLE:
			  memtype = builtin_type_ieee_double_little;
			  break;
			default:
			  internal_error (__FILE__, __LINE__, "bad switch");
			}
		      convert_typed_floating (val, type, memval, memtype);
		      write_memory (sp + argoffset, val, len);
		    }
		  argoffset += 8;
		}
	    }
	  else if (len == 8 && (TYPE_CODE (type) == TYPE_CODE_INT	/* long long */
				|| (!ppc_floating_point_unit_p (current_gdbarch) && TYPE_CODE (type) == TYPE_CODE_FLT)))	/* double */
	    {
	      /* "long long" or "double" passed in an odd/even
	         register pair with the low addressed word in the odd
	         register and the high addressed word in the even
	         register, or when the registers run out an 8 byte
	         aligned stack location.  */
	      if (greg > 9)
		{
		  /* Just in case GREG was 10.  */
		  greg = 11;
		  argoffset = align_up (argoffset, 8);
		  if (write_pass)
		    write_memory (sp + argoffset, val, len);
		  argoffset += 8;
		}
	      else if (tdep->wordsize == 8)
		{
		  if (write_pass)
		    regcache_cooked_write (regcache,
					   tdep->ppc_gp0_regnum + greg, val);
		  greg += 1;
		}
	      else
		{
		  /* Must start on an odd register - r3/r4 etc.  */
		  if ((greg & 1) == 0)
		    greg++;
		  if (write_pass)
		    {
		      regcache_cooked_write (regcache,
					     tdep->ppc_gp0_regnum + greg + 0,
					     val + 0);
		      regcache_cooked_write (regcache,
					     tdep->ppc_gp0_regnum + greg + 1,
					     val + 4);
		    }
		  greg += 2;
		}
	    }
	  else if (len == 16
		   && TYPE_CODE (type) == TYPE_CODE_ARRAY
		   && TYPE_VECTOR (type) && tdep->ppc_vr0_regnum >= 0)
	    {
	      /* Vector parameter passed in an Altivec register, or
	         when that runs out, 16 byte aligned stack location.  */
	      if (vreg <= 13)
		{
		  if (write_pass)
		    regcache_cooked_write (current_regcache,
					   tdep->ppc_vr0_regnum + vreg, val);
		  vreg++;
		}
	      else
		{
		  argoffset = align_up (argoffset, 16);
		  if (write_pass)
		    write_memory (sp + argoffset, val, 16);
		  argoffset += 16;
		}
	    }
	  else if (len == 8
		   && TYPE_CODE (type) == TYPE_CODE_ARRAY
		   && TYPE_VECTOR (type) && tdep->ppc_ev0_regnum >= 0)
	    {
	      /* Vector parameter passed in an e500 register, or when
	         that runs out, 8 byte aligned stack location.  Note
	         that since e500 vector and general purpose registers
	         both map onto the same underlying register set, a
	         "greg" and not a "vreg" is consumed here.  A cooked
	         write stores the value in the correct locations
	         within the raw register cache.  */
	      if (greg <= 10)
		{
		  if (write_pass)
		    regcache_cooked_write (current_regcache,
					   tdep->ppc_ev0_regnum + greg, val);
		  greg++;
		}
	      else
		{
		  argoffset = align_up (argoffset, 8);
		  if (write_pass)
		    write_memory (sp + argoffset, val, 8);
		  argoffset += 8;
		}
	    }
	  else
	    {
	      /* Reduce the parameter down to something that fits in a
	         "word".  */
	      char word[MAX_REGISTER_SIZE];
	      memset (word, 0, MAX_REGISTER_SIZE);
	      if (len > tdep->wordsize
		  || TYPE_CODE (type) == TYPE_CODE_STRUCT
		  || TYPE_CODE (type) == TYPE_CODE_UNION)
		{
		  /* Structs and large values are put on an 8 byte
		     aligned stack ... */
		  structoffset = align_up (structoffset, 8);
		  if (write_pass)
		    write_memory (sp + structoffset, val, len);
		  /* ... and then a "word" pointing to that address is
		     passed as the parameter.  */
		  store_unsigned_integer (word, tdep->wordsize,
					  sp + structoffset);
		  structoffset += len;
		}
	      else if (TYPE_CODE (type) == TYPE_CODE_INT)
		/* Sign or zero extend the "int" into a "word".  */
		store_unsigned_integer (word, tdep->wordsize,
					unpack_long (type, val));
	      else
		/* Always goes in the low address.  */
		memcpy (word, val, len);
	      /* Store that "word" in a register, or on the stack.
	         The words have "4" byte alignment.  */
	      if (greg <= 10)
		{
		  if (write_pass)
		    regcache_cooked_write (regcache,
					   tdep->ppc_gp0_regnum + greg, word);
		  greg++;
		}
	      else
		{
		  argoffset = align_up (argoffset, tdep->wordsize);
		  if (write_pass)
		    write_memory (sp + argoffset, word, tdep->wordsize);
		  argoffset += tdep->wordsize;
		}
	    }
	}

      /* Compute the actual stack space requirements.  */
      if (!write_pass)
	{
	  /* Remember the amount of space needed by the arguments.  */
	  argspace = argoffset;
	  /* Allocate space for both the arguments and the structures.  */
	  sp -= (argoffset + structoffset);
	  /* Ensure that the stack is still 16 byte aligned.  */
	  sp = align_down (sp, 16);
	}
    }

  /* Update %sp.   */
  regcache_cooked_write_signed (regcache, SP_REGNUM, sp);

  /* Write the backchain (it occupies WORDSIZED bytes).  */
  write_memory_signed_integer (sp, tdep->wordsize, saved_sp);

  /* Point the inferior function call's return address at the dummy's
     breakpoint.  */
  regcache_cooked_write_signed (regcache, tdep->ppc_lr_regnum, bp_addr);

  return sp;
}

/* Handle the return-value conventions specified by the SysV 32-bit
   PowerPC ABI (including all the supplements):

   no floating-point: floating-point values returned using 32-bit
   general-purpose registers.

   Altivec: 128-bit vectors returned using vector registers.

   e500: 64-bit vectors returned using the full full 64 bit EV
   register, floating-point values returned using 32-bit
   general-purpose registers.

   GCC (broken): Small struct values right (instead of left) aligned
   when returned in general-purpose registers.  */

static enum return_value_convention
do_ppc_sysv_return_value (struct gdbarch *gdbarch, struct type *type,
			  struct regcache *regcache, void *readbuf,
			  const void *writebuf, int broken_gcc)
{
  struct gdbarch_tdep *tdep = gdbarch_tdep (gdbarch);
  gdb_assert (tdep->wordsize == 4);
  if (TYPE_CODE (type) == TYPE_CODE_FLT
      && TYPE_LENGTH (type) <= 8
      && ppc_floating_point_unit_p (gdbarch))
    {
      if (readbuf)
	{
	  /* Floats and doubles stored in "f1".  Convert the value to
	     the required type.  */
	  char regval[MAX_REGISTER_SIZE];
	  struct type *regtype = register_type (gdbarch, FP0_REGNUM + 1);
	  regcache_cooked_read (regcache, FP0_REGNUM + 1, regval);
	  convert_typed_floating (regval, regtype, readbuf, type);
	}
      if (writebuf)
	{
	  /* Floats and doubles stored in "f1".  Convert the value to
	     the register's "double" type.  */
	  char regval[MAX_REGISTER_SIZE];
	  struct type *regtype = register_type (gdbarch, FP0_REGNUM);
	  convert_typed_floating (writebuf, type, regval, regtype);
	  regcache_cooked_write (regcache, FP0_REGNUM + 1, regval);
	}
      return RETURN_VALUE_REGISTER_CONVENTION;
    }
  if ((TYPE_CODE (type) == TYPE_CODE_INT && TYPE_LENGTH (type) == 8)
      || (TYPE_CODE (type) == TYPE_CODE_FLT && TYPE_LENGTH (type) == 8))
    {
      if (readbuf)
	{
	  /* A long long, or a double stored in the 32 bit r3/r4.  */
	  regcache_cooked_read (regcache, tdep->ppc_gp0_regnum + 3,
				(bfd_byte *) readbuf + 0);
	  regcache_cooked_read (regcache, tdep->ppc_gp0_regnum + 4,
				(bfd_byte *) readbuf + 4);
	}
      if (writebuf)
	{
	  /* A long long, or a double stored in the 32 bit r3/r4.  */
	  regcache_cooked_write (regcache, tdep->ppc_gp0_regnum + 3,
				 (const bfd_byte *) writebuf + 0);
	  regcache_cooked_write (regcache, tdep->ppc_gp0_regnum + 4,
				 (const bfd_byte *) writebuf + 4);
	}
      return RETURN_VALUE_REGISTER_CONVENTION;
    }
  if (TYPE_CODE (type) == TYPE_CODE_INT
      && TYPE_LENGTH (type) <= tdep->wordsize)
    {
      if (readbuf)
	{
	  /* Some sort of integer stored in r3.  Since TYPE isn't
	     bigger than the register, sign extension isn't a problem
	     - just do everything unsigned.  */
	  ULONGEST regval;
	  regcache_cooked_read_unsigned (regcache, tdep->ppc_gp0_regnum + 3,
					 &regval);
	  store_unsigned_integer (readbuf, TYPE_LENGTH (type), regval);
	}
      if (writebuf)
	{
	  /* Some sort of integer stored in r3.  Use unpack_long since
	     that should handle any required sign extension.  */
	  regcache_cooked_write_unsigned (regcache, tdep->ppc_gp0_regnum + 3,
					  unpack_long (type, writebuf));
	}
      return RETURN_VALUE_REGISTER_CONVENTION;
    }
  if (TYPE_LENGTH (type) == 16
      && TYPE_CODE (type) == TYPE_CODE_ARRAY
      && TYPE_VECTOR (type) && tdep->ppc_vr0_regnum >= 0)
    {
      if (readbuf)
	{
	  /* Altivec places the return value in "v2".  */
	  regcache_cooked_read (regcache, tdep->ppc_vr0_regnum + 2, readbuf);
	}
      if (writebuf)
	{
	  /* Altivec places the return value in "v2".  */
	  regcache_cooked_write (regcache, tdep->ppc_vr0_regnum + 2, writebuf);
	}
      return RETURN_VALUE_REGISTER_CONVENTION;
    }
  if (TYPE_LENGTH (type) == 8
      && TYPE_CODE (type) == TYPE_CODE_ARRAY
      && TYPE_VECTOR (type) && tdep->ppc_ev0_regnum >= 0)
    {
      /* The e500 ABI places return values for the 64-bit DSP types
	 (__ev64_opaque__) in r3.  However, in GDB-speak, ev3
	 corresponds to the entire r3 value for e500, whereas GDB's r3
	 only corresponds to the least significant 32-bits.  So place
	 the 64-bit DSP type's value in ev3.  */
      if (readbuf)
	regcache_cooked_read (regcache, tdep->ppc_ev0_regnum + 3, readbuf);
      if (writebuf)
	regcache_cooked_write (regcache, tdep->ppc_ev0_regnum + 3, writebuf);
      return RETURN_VALUE_REGISTER_CONVENTION;
    }
  if (broken_gcc && TYPE_LENGTH (type) <= 8)
    {
      if (readbuf)
	{
	  /* GCC screwed up.  The last register isn't "left" aligned.
	     Need to extract the least significant part of each
	     register and then store that.  */
	  /* Transfer any full words.  */
	  int word = 0;
	  while (1)
	    {
	      ULONGEST reg;
	      int len = TYPE_LENGTH (type) - word * tdep->wordsize;
	      if (len <= 0)
		break;
	      if (len > tdep->wordsize)
		len = tdep->wordsize;
	      regcache_cooked_read_unsigned (regcache,
					     tdep->ppc_gp0_regnum + 3 + word,
					     &reg);
	      store_unsigned_integer (((bfd_byte *) readbuf
				       + word * tdep->wordsize), len, reg);
	      word++;
	    }
	}
      if (writebuf)
	{
	  /* GCC screwed up.  The last register isn't "left" aligned.
	     Need to extract the least significant part of each
	     register and then store that.  */
	  /* Transfer any full words.  */
	  int word = 0;
	  while (1)
	    {
	      ULONGEST reg;
	      int len = TYPE_LENGTH (type) - word * tdep->wordsize;
	      if (len <= 0)
		break;
	      if (len > tdep->wordsize)
		len = tdep->wordsize;
	      reg = extract_unsigned_integer (((const bfd_byte *) writebuf
					       + word * tdep->wordsize), len);
	      regcache_cooked_write_unsigned (regcache,
					      tdep->ppc_gp0_regnum + 3 + word,
					      reg);
	      word++;
	    }
	}
      return RETURN_VALUE_REGISTER_CONVENTION;
    }
  if (TYPE_LENGTH (type) <= 8)
    {
      if (readbuf)
	{
	  /* This matches SVr4 PPC, it does not match GCC.  */
	  /* The value is right-padded to 8 bytes and then loaded, as
	     two "words", into r3/r4.  */
	  char regvals[MAX_REGISTER_SIZE * 2];
	  regcache_cooked_read (regcache, tdep->ppc_gp0_regnum + 3,
				regvals + 0 * tdep->wordsize);
	  if (TYPE_LENGTH (type) > tdep->wordsize)
	    regcache_cooked_read (regcache, tdep->ppc_gp0_regnum + 4,
				  regvals + 1 * tdep->wordsize);
	  memcpy (readbuf, regvals, TYPE_LENGTH (type));
	}
      if (writebuf)
	{
	  /* This matches SVr4 PPC, it does not match GCC.  */
	  /* The value is padded out to 8 bytes and then loaded, as
	     two "words" into r3/r4.  */
	  char regvals[MAX_REGISTER_SIZE * 2];
	  memset (regvals, 0, sizeof regvals);
	  memcpy (regvals, writebuf, TYPE_LENGTH (type));
	  regcache_cooked_write (regcache, tdep->ppc_gp0_regnum + 3,
				 regvals + 0 * tdep->wordsize);
	  if (TYPE_LENGTH (type) > tdep->wordsize)
	    regcache_cooked_write (regcache, tdep->ppc_gp0_regnum + 4,
				   regvals + 1 * tdep->wordsize);
	}
      return RETURN_VALUE_REGISTER_CONVENTION;
    }
  return RETURN_VALUE_STRUCT_CONVENTION;
}

enum return_value_convention
ppc_sysv_abi_return_value (struct gdbarch *gdbarch, struct type *valtype,
			   struct regcache *regcache, void *readbuf,
			   const void *writebuf)
{
  return do_ppc_sysv_return_value (gdbarch, valtype, regcache, readbuf,
				   writebuf, 0);
}

enum return_value_convention
ppc_sysv_abi_broken_return_value (struct gdbarch *gdbarch,
				  struct type *valtype,
				  struct regcache *regcache,
				  void *readbuf, const void *writebuf)
{
  return do_ppc_sysv_return_value (gdbarch, valtype, regcache, readbuf,
				   writebuf, 1);
}

/* Pass the arguments in either registers, or in the stack. Using the
   ppc 64 bit SysV ABI.

   This implements a dumbed down version of the ABI.  It always writes
   values to memory, GPR and FPR, even when not necessary.  Doing this
   greatly simplifies the logic. */

CORE_ADDR
ppc64_sysv_abi_push_dummy_call (struct gdbarch *gdbarch, CORE_ADDR func_addr,
				struct regcache *regcache, CORE_ADDR bp_addr,
				int nargs, struct value **args, CORE_ADDR sp,
				int struct_return, CORE_ADDR struct_addr)
{
  struct gdbarch_tdep *tdep = gdbarch_tdep (current_gdbarch);
  /* By this stage in the proceedings, SP has been decremented by "red
     zone size" + "struct return size".  Fetch the stack-pointer from
     before this and use that as the BACK_CHAIN.  */
  const CORE_ADDR back_chain = read_sp ();
  /* See for-loop comment below.  */
  int write_pass;
  /* Size of the Altivec's vector parameter region, the final value is
     computed in the for-loop below.  */
  LONGEST vparam_size = 0;
  /* Size of the general parameter region, the final value is computed
     in the for-loop below.  */
  LONGEST gparam_size = 0;
  /* Kevin writes ... I don't mind seeing tdep->wordsize used in the
     calls to align_up(), align_down(), etc.  because this makes it
     easier to reuse this code (in a copy/paste sense) in the future,
     but it is a 64-bit ABI and asserting that the wordsize is 8 bytes
     at some point makes it easier to verify that this function is
     correct without having to do a non-local analysis to figure out
     the possible values of tdep->wordsize.  */
  gdb_assert (tdep->wordsize == 8);

  /* Go through the argument list twice.

     Pass 1: Compute the function call's stack space and register
     requirements.

     Pass 2: Replay the same computation but this time also write the
     values out to the target.  */

  for (write_pass = 0; write_pass < 2; write_pass++)
    {
      int argno;
      /* Next available floating point register for float and double
         arguments.  */
      int freg = 1;
      /* Next available general register for non-vector (but possibly
         float) arguments.  */
      int greg = 3;
      /* Next available vector register for vector arguments.  */
      int vreg = 2;
      /* The address, at which the next general purpose parameter
         (integer, struct, float, ...) should be saved.  */
      CORE_ADDR gparam;
      /* Address, at which the next Altivec vector parameter should be
         saved.  */
      CORE_ADDR vparam;

      if (!write_pass)
	{
	  /* During the first pass, GPARAM and VPARAM are more like
	     offsets (start address zero) than addresses.  That way
	     the accumulate the total stack space each region
	     requires.  */
	  gparam = 0;
	  vparam = 0;
	}
      else
	{
	  /* Decrement the stack pointer making space for the Altivec
	     and general on-stack parameters.  Set vparam and gparam
	     to their corresponding regions.  */
	  vparam = align_down (sp - vparam_size, 16);
	  gparam = align_down (vparam - gparam_size, 16);
	  /* Add in space for the TOC, link editor double word,
	     compiler double word, LR save area, CR save area.  */
	  sp = align_down (gparam - 48, 16);
	}

      /* If the function is returning a `struct', then there is an
         extra hidden parameter (which will be passed in r3)
         containing the address of that struct..  In that case we
         should advance one word and start from r4 register to copy
         parameters.  This also consumes one on-stack parameter slot.  */
      if (struct_return)
	{
	  if (write_pass)
	    regcache_cooked_write_signed (regcache,
					  tdep->ppc_gp0_regnum + greg,
					  struct_addr);
	  greg++;
	  gparam = align_up (gparam + tdep->wordsize, tdep->wordsize);
	}

      for (argno = 0; argno < nargs; argno++)
	{
	  struct value *arg = args[argno];
	  struct type *type = check_typedef (VALUE_TYPE (arg));
	  char *val = VALUE_CONTENTS (arg);
	  if (TYPE_CODE (type) == TYPE_CODE_FLT && TYPE_LENGTH (type) <= 8)
	    {
	      /* Floats and Doubles go in f1 .. f13.  They also
	         consume a left aligned GREG,, and can end up in
	         memory.  */
	      if (write_pass)
		{
		  if (ppc_floating_point_unit_p (current_gdbarch)
		      && freg <= 13)
		    {
		      char regval[MAX_REGISTER_SIZE];
		      struct type *regtype = register_type (gdbarch,
							    FP0_REGNUM);
		      convert_typed_floating (val, type, regval, regtype);
		      regcache_cooked_write (regcache, FP0_REGNUM + freg,
					     regval);
		    }
		  if (greg <= 10)
		    {
		      /* The ABI states "Single precision floating
		         point values are mapped to the first word in
		         a single doubleword" and "... floating point
		         values mapped to the first eight doublewords
		         of the parameter save area are also passed in
		         general registers").

		         This code interprets that to mean: store it,
		         left aligned, in the general register.  */
		      char regval[MAX_REGISTER_SIZE];
		      memset (regval, 0, sizeof regval);
		      memcpy (regval, val, TYPE_LENGTH (type));
		      regcache_cooked_write (regcache,
					     tdep->ppc_gp0_regnum + greg,
					     regval);
		    }
		  write_memory (gparam, val, TYPE_LENGTH (type));
		}
	      /* Always consume parameter stack space.  */
	      freg++;
	      greg++;
	      gparam = align_up (gparam + TYPE_LENGTH (type), tdep->wordsize);
	    }
	  else if (TYPE_LENGTH (type) == 16 && TYPE_VECTOR (type)
		   && TYPE_CODE (type) == TYPE_CODE_ARRAY
		   && tdep->ppc_vr0_regnum >= 0)
	    {
	      /* In the Altivec ABI, vectors go in the vector
	         registers v2 .. v13, or when that runs out, a vector
	         annex which goes above all the normal parameters.
	         NOTE: cagney/2003-09-21: This is a guess based on the
	         PowerOpen Altivec ABI.  */
	      if (vreg <= 13)
		{
		  if (write_pass)
		    regcache_cooked_write (regcache,
					   tdep->ppc_vr0_regnum + vreg, val);
		  vreg++;
		}
	      else
		{
		  if (write_pass)
		    write_memory (vparam, val, TYPE_LENGTH (type));
		  vparam = align_up (vparam + TYPE_LENGTH (type), 16);
		}
	    }
	  else if ((TYPE_CODE (type) == TYPE_CODE_INT
		    || TYPE_CODE (type) == TYPE_CODE_ENUM)
		   && TYPE_LENGTH (type) <= 8)
	    {
	      /* Scalars get sign[un]extended and go in gpr3 .. gpr10.
	         They can also end up in memory.  */
	      if (write_pass)
		{
		  /* Sign extend the value, then store it unsigned.  */
		  ULONGEST word = unpack_long (type, val);
		  if (greg <= 10)
		    regcache_cooked_write_unsigned (regcache,
						    tdep->ppc_gp0_regnum +
						    greg, word);
		  write_memory_unsigned_integer (gparam, tdep->wordsize,
						 word);
		}
	      greg++;
	      gparam = align_up (gparam + TYPE_LENGTH (type), tdep->wordsize);
	    }
	  else
	    {
	      int byte;
	      for (byte = 0; byte < TYPE_LENGTH (type);
		   byte += tdep->wordsize)
		{
		  if (write_pass && greg <= 10)
		    {
		      char regval[MAX_REGISTER_SIZE];
		      int len = TYPE_LENGTH (type) - byte;
		      if (len > tdep->wordsize)
			len = tdep->wordsize;
		      memset (regval, 0, sizeof regval);
		      /* WARNING: cagney/2003-09-21: As best I can
		         tell, the ABI specifies that the value should
		         be left aligned.  Unfortunately, GCC doesn't
		         do this - it instead right aligns even sized
		         values and puts odd sized values on the
		         stack.  Work around that by putting both a
		         left and right aligned value into the
		         register (hopefully no one notices :-^).
		         Arrrgh!  */
		      /* Left aligned (8 byte values such as pointers
		         fill the buffer).  */
		      memcpy (regval, val + byte, len);
		      /* Right aligned (but only if even).  */
		      if (len == 1 || len == 2 || len == 4)
			memcpy (regval + tdep->wordsize - len,
				val + byte, len);
		      regcache_cooked_write (regcache, greg, regval);
		    }
		  greg++;
		}
	      if (write_pass)
		/* WARNING: cagney/2003-09-21: Strictly speaking, this
		   isn't necessary, unfortunately, GCC appears to get
		   "struct convention" parameter passing wrong putting
		   odd sized structures in memory instead of in a
		   register.  Work around this by always writing the
		   value to memory.  Fortunately, doing this
		   simplifies the code.  */
		write_memory (gparam, val, TYPE_LENGTH (type));
	      /* Always consume parameter stack space.  */
	      gparam = align_up (gparam + TYPE_LENGTH (type), tdep->wordsize);
	    }
	}

      if (!write_pass)
	{
	  /* Save the true region sizes ready for the second pass.  */
	  vparam_size = vparam;
	  /* Make certain that the general parameter save area is at
	     least the minimum 8 registers (or doublewords) in size.  */
	  if (greg < 8)
	    gparam_size = 8 * tdep->wordsize;
	  else
	    gparam_size = gparam;
	}
    }

  /* Update %sp.   */
  regcache_cooked_write_signed (regcache, SP_REGNUM, sp);

  /* Write the backchain (it occupies WORDSIZED bytes).  */
  write_memory_signed_integer (sp, tdep->wordsize, back_chain);

  /* Point the inferior function call's return address at the dummy's
     breakpoint.  */
  regcache_cooked_write_signed (regcache, tdep->ppc_lr_regnum, bp_addr);

  /* Find a value for the TOC register.  Every symbol should have both
     ".FN" and "FN" in the minimal symbol table.  "FN" points at the
     FN's descriptor, while ".FN" points at the entry point (which
     matches FUNC_ADDR).  Need to reverse from FUNC_ADDR back to the
     FN's descriptor address (while at the same time being careful to
     find "FN" in the same object file as ".FN").  */
  {
    /* Find the minimal symbol that corresponds to FUNC_ADDR (should
       have the name ".FN").  */
    struct minimal_symbol *dot_fn = lookup_minimal_symbol_by_pc (func_addr);
    if (dot_fn != NULL && SYMBOL_LINKAGE_NAME (dot_fn)[0] == '.')
      {
	/* Get the section that contains FUNC_ADR.  Need this for the
           "objfile" that it contains.  */
	struct obj_section *dot_fn_section = find_pc_section (func_addr);
	if (dot_fn_section != NULL && dot_fn_section->objfile != NULL)
	  {
	    /* Now find the corresponding "FN" (dropping ".") minimal
	       symbol's address.  Only look for the minimal symbol in
	       ".FN"'s object file - avoids problems when two object
	       files (i.e., shared libraries) contain a minimal symbol
	       with the same name.  */
	    struct minimal_symbol *fn =
	      lookup_minimal_symbol (SYMBOL_LINKAGE_NAME (dot_fn) + 1, NULL,
				     dot_fn_section->objfile);
	    if (fn != NULL)
	      {
		/* Got the address of that descriptor.  The TOC is the
		   second double word.  */
		CORE_ADDR toc =
		  read_memory_unsigned_integer (SYMBOL_VALUE_ADDRESS (fn)
						+ tdep->wordsize,
						tdep->wordsize);
		regcache_cooked_write_unsigned (regcache,
						tdep->ppc_gp0_regnum + 2, toc);
	      }
	  }
      }
  }

  return sp;
}


/* The 64 bit ABI retun value convention.

   Return non-zero if the return-value is stored in a register, return
   0 if the return-value is instead stored on the stack (a.k.a.,
   struct return convention).

   For a return-value stored in a register: when WRITEBUF is non-NULL,
   copy the buffer to the corresponding register return-value location
   location; when READBUF is non-NULL, fill the buffer from the
   corresponding register return-value location.  */
enum return_value_convention
ppc64_sysv_abi_return_value (struct gdbarch *gdbarch, struct type *valtype,
			     struct regcache *regcache, void *readbuf,
			     const void *writebuf)
{
  struct gdbarch_tdep *tdep = gdbarch_tdep (gdbarch);
  /* Floats and doubles in F1.  */
  if (TYPE_CODE (valtype) == TYPE_CODE_FLT && TYPE_LENGTH (valtype) <= 8)
    {
      char regval[MAX_REGISTER_SIZE];
      struct type *regtype = register_type (gdbarch, FP0_REGNUM);
      if (writebuf != NULL)
	{
	  convert_typed_floating (writebuf, valtype, regval, regtype);
	  regcache_cooked_write (regcache, FP0_REGNUM + 1, regval);
	}
      if (readbuf != NULL)
	{
	  regcache_cooked_read (regcache, FP0_REGNUM + 1, regval);
	  convert_typed_floating (regval, regtype, readbuf, valtype);
	}
      return RETURN_VALUE_REGISTER_CONVENTION;
    }
  if (TYPE_CODE (valtype) == TYPE_CODE_INT && TYPE_LENGTH (valtype) <= 8)
    {
      /* Integers in r3.  */
      if (writebuf != NULL)
	{
	  /* Be careful to sign extend the value.  */
	  regcache_cooked_write_unsigned (regcache, tdep->ppc_gp0_regnum + 3,
					  unpack_long (valtype, writebuf));
	}
      if (readbuf != NULL)
	{
	  /* Extract the integer from r3.  Since this is truncating the
	     value, there isn't a sign extension problem.  */
	  ULONGEST regval;
	  regcache_cooked_read_unsigned (regcache, tdep->ppc_gp0_regnum + 3,
					 &regval);
	  store_unsigned_integer (readbuf, TYPE_LENGTH (valtype), regval);
	}
      return RETURN_VALUE_REGISTER_CONVENTION;
    }
  /* All pointers live in r3.  */
  if (TYPE_CODE (valtype) == TYPE_CODE_PTR)
    {
      /* All pointers live in r3.  */
      if (writebuf != NULL)
	regcache_cooked_write (regcache, tdep->ppc_gp0_regnum + 3, writebuf);
      if (readbuf != NULL)
	regcache_cooked_read (regcache, tdep->ppc_gp0_regnum + 3, readbuf);
      return RETURN_VALUE_REGISTER_CONVENTION;
    }
  if (TYPE_CODE (valtype) == TYPE_CODE_ARRAY
      && TYPE_LENGTH (valtype) <= 8
      && TYPE_CODE (TYPE_TARGET_TYPE (valtype)) == TYPE_CODE_INT
      && TYPE_LENGTH (TYPE_TARGET_TYPE (valtype)) == 1)
    {
      /* Small character arrays are returned, right justified, in r3.  */
      int offset = (register_size (gdbarch, tdep->ppc_gp0_regnum + 3)
		    - TYPE_LENGTH (valtype));
      if (writebuf != NULL)
	regcache_cooked_write_part (regcache, tdep->ppc_gp0_regnum + 3,
				    offset, TYPE_LENGTH (valtype), writebuf);
      if (readbuf != NULL)
	regcache_cooked_read_part (regcache, tdep->ppc_gp0_regnum + 3,
				   offset, TYPE_LENGTH (valtype), readbuf);
      return RETURN_VALUE_REGISTER_CONVENTION;
    }
  /* Big floating point values get stored in adjacent floating
     point registers.  */
  if (TYPE_CODE (valtype) == TYPE_CODE_FLT
      && (TYPE_LENGTH (valtype) == 16 || TYPE_LENGTH (valtype) == 32))
    {
      if (writebuf || readbuf != NULL)
	{
	  int i;
	  for (i = 0; i < TYPE_LENGTH (valtype) / 8; i++)
	    {
	      if (writebuf != NULL)
		regcache_cooked_write (regcache, FP0_REGNUM + 1 + i,
				       (const bfd_byte *) writebuf + i * 8);
	      if (readbuf != NULL)
		regcache_cooked_read (regcache, FP0_REGNUM + 1 + i,
				      (bfd_byte *) readbuf + i * 8);
	    }
	}
      return RETURN_VALUE_REGISTER_CONVENTION;
    }
  /* Complex values get returned in f1:f2, need to convert.  */
  if (TYPE_CODE (valtype) == TYPE_CODE_COMPLEX
      && (TYPE_LENGTH (valtype) == 8 || TYPE_LENGTH (valtype) == 16))
    {
      if (regcache != NULL)
	{
	  int i;
	  for (i = 0; i < 2; i++)
	    {
	      char regval[MAX_REGISTER_SIZE];
	      struct type *regtype =
		register_type (current_gdbarch, FP0_REGNUM);
	      if (writebuf != NULL)
		{
		  convert_typed_floating ((const bfd_byte *) writebuf +
					  i * (TYPE_LENGTH (valtype) / 2),
					  valtype, regval, regtype);
		  regcache_cooked_write (regcache, FP0_REGNUM + 1 + i,
					 regval);
		}
	      if (readbuf != NULL)
		{
		  regcache_cooked_read (regcache, FP0_REGNUM + 1 + i, regval);
		  convert_typed_floating (regval, regtype,
					  (bfd_byte *) readbuf +
					  i * (TYPE_LENGTH (valtype) / 2),
					  valtype);
		}
	    }
	}
      return RETURN_VALUE_REGISTER_CONVENTION;
    }
  /* Big complex values get stored in f1:f4.  */
  if (TYPE_CODE (valtype) == TYPE_CODE_COMPLEX && TYPE_LENGTH (valtype) == 32)
    {
      if (regcache != NULL)
	{
	  int i;
	  for (i = 0; i < 4; i++)
	    {
	      if (writebuf != NULL)
		regcache_cooked_write (regcache, FP0_REGNUM + 1 + i,
				       (const bfd_byte *) writebuf + i * 8);
	      if (readbuf != NULL)
		regcache_cooked_read (regcache, FP0_REGNUM + 1 + i,
				      (bfd_byte *) readbuf + i * 8);
	    }
	}
      return RETURN_VALUE_REGISTER_CONVENTION;
    }
  return RETURN_VALUE_STRUCT_CONVENTION;
}

CORE_ADDR
ppc64_sysv_abi_adjust_breakpoint_address (struct gdbarch *gdbarch,
					  CORE_ADDR bpaddr)
{
  /* PPC64 SYSV specifies that the minimal-symbol "FN" should point at
     a function-descriptor while the corresponding minimal-symbol
     ".FN" should point at the entry point.  Consequently, a command
     like "break FN" applied to an object file with only minimal
     symbols, will insert the breakpoint into the descriptor at "FN"
     and not the function at ".FN".  Avoid this confusion by adjusting
     any attempt to set a descriptor breakpoint into a corresponding
     function breakpoint.  Note that GDB warns the user when this
     adjustment is applied - that's ok as otherwise the user will have
     no way of knowing why their breakpoint at "FN" resulted in the
     program stopping at ".FN".  */
  return gdbarch_convert_from_func_ptr_addr (gdbarch, bpaddr, &current_target);
}
