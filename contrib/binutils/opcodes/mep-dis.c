/* Disassembler interface for targets using CGEN. -*- C -*-
   CGEN: Cpu tools GENerator

   THIS FILE IS MACHINE GENERATED WITH CGEN.
   - the resultant file is machine generated, cgen-dis.in isn't

   Copyright 1996, 1997, 1998, 1999, 2000, 2001, 2002, 2003, 2005
   Free Software Foundation, Inc.

   This file is part of the GNU Binutils and GDB, the GNU debugger.

   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; either version 2, or (at your option)
   any later version.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program; if not, write to the Free Software Foundation, Inc.,
   51 Franklin Street - Fifth Floor, Boston, MA 02110-1301, USA.  */

/* ??? Eventually more and more of this stuff can go to cpu-independent files.
   Keep that in mind.  */

#include "sysdep.h"
#include <stdio.h>
#include "ansidecl.h"
#include "dis-asm.h"
#include "bfd.h"
#include "symcat.h"
#include "libiberty.h"
#include "mep-desc.h"
#include "mep-opc.h"
#include "opintl.h"

/* Default text to print if an instruction isn't recognized.  */
#define UNKNOWN_INSN_MSG _("*unknown*")

static void print_normal
  (CGEN_CPU_DESC, void *, long, unsigned int, bfd_vma, int);
static void print_address
  (CGEN_CPU_DESC, void *, bfd_vma, unsigned int, bfd_vma, int) ATTRIBUTE_UNUSED;
static void print_keyword
  (CGEN_CPU_DESC, void *, CGEN_KEYWORD *, long, unsigned int) ATTRIBUTE_UNUSED;
static void print_insn_normal
  (CGEN_CPU_DESC, void *, const CGEN_INSN *, CGEN_FIELDS *, bfd_vma, int);
static int print_insn
  (CGEN_CPU_DESC, bfd_vma,  disassemble_info *, bfd_byte *, unsigned);
static int default_print_insn
  (CGEN_CPU_DESC, bfd_vma, disassemble_info *) ATTRIBUTE_UNUSED;
static int read_insn
  (CGEN_CPU_DESC, bfd_vma, disassemble_info *, bfd_byte *, int, CGEN_EXTRACT_INFO *,
   unsigned long *);

/* -- disassembler routines inserted here.  */

/* -- dis.c */

#include "elf/mep.h"
#include "elf-bfd.h"

#define CGEN_VALIDATE_INSN_SUPPORTED

static void print_tpreg (CGEN_CPU_DESC, PTR, CGEN_KEYWORD *, long, unsigned int);
static void print_spreg (CGEN_CPU_DESC, PTR, CGEN_KEYWORD *, long, unsigned int);

static void
print_tpreg (CGEN_CPU_DESC cd ATTRIBUTE_UNUSED, PTR dis_info,
	     CGEN_KEYWORD *table ATTRIBUTE_UNUSED, long val ATTRIBUTE_UNUSED,
	     unsigned int flags ATTRIBUTE_UNUSED)
{
  disassemble_info *info = (disassemble_info *) dis_info;

  (*info->fprintf_func) (info->stream, "$tp");
}

static void
print_spreg (CGEN_CPU_DESC cd ATTRIBUTE_UNUSED, PTR dis_info, 
	     CGEN_KEYWORD *table ATTRIBUTE_UNUSED, long val ATTRIBUTE_UNUSED,
	     unsigned int flags ATTRIBUTE_UNUSED)
{
  disassemble_info *info = (disassemble_info *) dis_info;

  (*info->fprintf_func) (info->stream, "$sp");
}

/* begin-cop-ip-print-handlers */
static void
print_fmax_cr (CGEN_CPU_DESC cd,
	void *dis_info,
	CGEN_KEYWORD *keyword_table ATTRIBUTE_UNUSED,
	long value,
	unsigned int attrs)
{
  print_keyword (cd, dis_info, & mep_cgen_opval_h_cr_fmax, value, attrs);
}
static void
print_fmax_ccr (CGEN_CPU_DESC cd,
	void *dis_info,
	CGEN_KEYWORD *keyword_table ATTRIBUTE_UNUSED,
	long value,
	unsigned int attrs)
{
  print_keyword (cd, dis_info, & mep_cgen_opval_h_ccr_fmax, value, attrs);
}
/* end-cop-ip-print-handlers */

/************************************************************\
*********************** Experimental *************************
\************************************************************/

#undef  CGEN_PRINT_INSN
#define CGEN_PRINT_INSN mep_print_insn

static int
mep_print_vliw_insns (CGEN_CPU_DESC cd, bfd_vma pc, disassemble_info *info,
		      bfd_byte *buf, int corelength, int copro1length,
		      int copro2length ATTRIBUTE_UNUSED)
{
  int i;
  int status = 0;
  /* char insnbuf[CGEN_MAX_INSN_SIZE]; */
  bfd_byte insnbuf[64];

  /* If corelength > 0 then there is a core insn present. It
     will be at the beginning of the buffer.  After printing
     the core insn, we need to print the + on the next line.  */
  if (corelength > 0)
    {
      int my_status = 0;
	 
      for (i = 0; i < corelength; i++ )
	insnbuf[i] = buf[i];
      cd->isas = & MEP_CORE_ISA;
	 
      my_status = print_insn (cd, pc, info, insnbuf, corelength);
      if (my_status != corelength)
	{
	  (*info->fprintf_func) (info->stream, UNKNOWN_INSN_MSG);
	  my_status = corelength;
	}
      status += my_status;

      /* Print the + to indicate that the following copro insn is   */
      /* part of a vliw group.                                      */
      if (copro1length > 0)
	(*info->fprintf_func) (info->stream, " + "); 
    }

  /* Now all that is left to be processed is the coprocessor insns
     In vliw mode, there will always be one.  Its positioning will
     be from byte corelength to byte corelength+copro1length -1.
     No need to check for existence.   Also, the first vliw insn,
     will, as spec'd, always be at least as long as the core insn
     so we don't need to flush the buffer.  */
  if (copro1length > 0)
    {
      int my_status = 0;
	 
      for (i = corelength; i < corelength + copro1length; i++ )
	insnbuf[i - corelength] = buf[i];

      switch (copro1length)
	{
	case 0:
	  break;
	case 2:
	  cd->isas = & MEP_COP16_ISA;
	  break;
	case 4:
	  cd->isas = & MEP_COP32_ISA;
	  break;
	case 6:
	  cd->isas = & MEP_COP48_ISA;
	  break;
	case 8:
	  cd->isas = & MEP_COP64_ISA;
	  break; 
	default:
	  /* Shouldn't be anything but 16,32,48,64.  */
	  break;
	}

      my_status = print_insn (cd, pc, info, insnbuf, copro1length);

      if (my_status != copro1length)
	{
	  (*info->fprintf_func) (info->stream, UNKNOWN_INSN_MSG);
	  my_status = copro1length;
	}
      status += my_status;
    }

#if 0
  /* Now we need to process the second copro insn if it exists. We
     have no guarantee that the second copro insn will be longer
     than the first, so we have to flush the buffer if we are have
     a second copro insn to process.  If present, this insn will
     be in the position from byte corelength+copro1length to byte
     corelength+copro1length+copro2length-1 (which better equal 8
     or else we're in big trouble.  */
  if (copro2length > 0)
    {
      int my_status = 0;

      for (i = 0; i < 64 ; i++)
	insnbuf[i] = 0;

      for (i = corelength + copro1length; i < 64; i++)
	insnbuf[i - (corelength + copro1length)] = buf[i];
      
      switch (copro2length)
	{
	case 2:
	  cd->isas = 1 << ISA_EXT_COP1_16;
	  break;
	case 4:
	  cd->isas = 1 << ISA_EXT_COP1_32;
	  break;
	case 6:
	  cd->isas = 1 << ISA_EXT_COP1_48;
	  break;
	case 8:
	  cd->isas = 1 << ISA_EXT_COP1_64; 
	  break;
	default:
	  /* Shouldn't be anything but 16,32,48,64.  */
	  break;
	}

      my_status = print_insn (cd, pc, info, insnbuf, copro2length);

      if (my_status != copro2length)
	{
	  (*info->fprintf_func) (info->stream, UNKNOWN_INSN_MSG);
	  my_status = copro2length;
	}

      status += my_status;
    }
#endif

  /* Status should now be the number of bytes that were printed
     which should be 4 for VLIW32 mode and 64 for VLIW64 mode.  */

  if ((!MEP_VLIW64 && (status != 4)) || (MEP_VLIW64 && (status != 8)))
    return -1;
  else
    return status;
}

/* The two functions mep_examine_vliw[32,64]_insns are used find out 
   which vliw combinaion (16 bit core with 48 bit copro, 32 bit core 
   with 32 bit copro, etc.) is present.  Later on, when internally   
   parallel coprocessors are handled, only these functions should    
   need to be changed.                                               

   At this time only the following combinations are supported: 
   
   VLIW32 Mode:
   16 bit core insn (core) and 16 bit coprocessor insn (cop1)
   32 bit core insn (core)
   32 bit coprocessor insn (cop1)
   Note: As of this time, I do not believe we have enough information
         to distinguish a 32 bit core insn from a 32 bit cop insn. Also,
         no 16 bit coprocessor insns have been specified.  

   VLIW64 Mode:
   16 bit core insn (core) and 48 bit coprocessor insn (cop1)
   32 bit core insn (core) and 32 bit coprocessor insn (cop1)
   64 bit coprocessor insn (cop1)
  
   The framework for an internally parallel coprocessor is also
   present (2nd coprocessor insn is cop2), but at this time it 
   is not used.  This only appears to be valid in VLIW64 mode.  */

static int
mep_examine_vliw32_insns (CGEN_CPU_DESC cd, bfd_vma pc, disassemble_info *info)
{
  int status;
  int buflength;
  int corebuflength;
  int cop1buflength;
  int cop2buflength;
  bfd_byte buf[CGEN_MAX_INSN_SIZE];  
  char indicator16[1];
  char indicatorcop32[2]; 

  /* At this time we're not supporting internally parallel coprocessors,
     so cop2buflength will always be 0.  */
  cop2buflength = 0;

  /* Read in 32 bits.  */
  buflength = 4; /* VLIW insn spans 4 bytes.  */
  status = (*info->read_memory_func) (pc, buf, buflength, info);

  if (status != 0)
    {
      (*info->memory_error_func) (status, pc, info);
      return -1;
    }

  /* Put the big endian representation of the bytes to be examined
     in the temporary buffers for examination.  */

  if (info->endian == BFD_ENDIAN_BIG)
    {
      indicator16[0] = buf[0];
      indicatorcop32[0] = buf[0];
      indicatorcop32[1] = buf[1];
    }
  else
    {
      indicator16[0] = buf[1];
      indicatorcop32[0] = buf[1];
      indicatorcop32[1] = buf[0];
    }

  /* If the two high order bits are 00, 01 or 10, we have a 16 bit
     core insn and a 48 bit copro insn.  */

  if ((indicator16[0] & 0x80) && (indicator16[0] & 0x40))
    {
      if ((indicatorcop32[0] & 0xf0) == 0xf0 && (indicatorcop32[1] & 0x07) == 0x07)
	{
          /* We have a 32 bit copro insn.  */
          corebuflength = 0;
	  /* All 4 4ytes are one copro insn. */
          cop1buflength = 4;
	}
      else
	{
          /* We have a 32 bit core.  */
          corebuflength = 4;
          cop1buflength = 0;
	}
    }
  else
    {
      /* We have a 16 bit core insn and a 16 bit copro insn.  */
      corebuflength = 2;
      cop1buflength = 2;
    }

  /* Now we have the distrubution set.  Print them out.  */
  status = mep_print_vliw_insns (cd, pc, info, buf, corebuflength,
				 cop1buflength, cop2buflength);

  return status;
}

static int
mep_examine_vliw64_insns (CGEN_CPU_DESC cd, bfd_vma pc, disassemble_info *info)
{
  int status;
  int buflength;
  int corebuflength;
  int cop1buflength;
  int cop2buflength;
  bfd_byte buf[CGEN_MAX_INSN_SIZE];
  char indicator16[1];
  char indicator64[4];

  /* At this time we're not supporting internally parallel
     coprocessors, so cop2buflength will always be 0.  */
  cop2buflength = 0;

  /* Read in 64 bits.  */
  buflength = 8; /* VLIW insn spans 8 bytes.  */
  status = (*info->read_memory_func) (pc, buf, buflength, info);

  if (status != 0)
    {
      (*info->memory_error_func) (status, pc, info);
      return -1;
    }

  /* We have all 64 bits in the buffer now.  We have to figure out
     what combination of instruction sizes are present.  The two
     high order bits will indicate whether or not we have a 16 bit
     core insn or not.  If not, then we have to look at the 7,8th
     bytes to tell whether we have 64 bit copro insn or a 32 bit
     core insn with a 32 bit copro insn.  Endianness will make a
     difference here.  */

  /* Put the big endian representation of the bytes to be examined
     in the temporary buffers for examination.  */

  /* indicator16[0] = buf[0];  */
  if (info->endian == BFD_ENDIAN_BIG)
    {
      indicator16[0] = buf[0];
      indicator64[0] = buf[0];
      indicator64[1] = buf[1];
      indicator64[2] = buf[2];
      indicator64[3] = buf[3];
    }
  else
    {
      indicator16[0] = buf[1];
      indicator64[0] = buf[1];
      indicator64[1] = buf[0];
      indicator64[2] = buf[3];
      indicator64[3] = buf[2];
    }

  /* If the two high order bits are 00, 01 or 10, we have a 16 bit
     core insn and a 48 bit copro insn.  */

  if ((indicator16[0] & 0x80) && (indicator16[0] & 0x40))
    {
      if ((indicator64[0] & 0xf0) == 0xf0 && (indicator64[1] & 0x07) == 0x07
	  && ((indicator64[2] & 0xfe) != 0xf0 || (indicator64[3] & 0xf4) != 0))
	{
          /* We have a 64 bit copro insn.  */
          corebuflength = 0;
	  /* All 8 bytes are one copro insn.  */
          cop1buflength = 8;
	}
      else
	{
          /* We have a 32 bit core insn and a 32 bit copro insn.  */
          corebuflength = 4;
          cop1buflength = 4;
	}
    }
  else
    {
      /* We have a 16 bit core insn and a 48 bit copro insn.  */
      corebuflength = 2;
      cop1buflength = 6;
    }

  /* Now we have the distrubution set.  Print them out. */
  status = mep_print_vliw_insns (cd, pc, info, buf, corebuflength,
				 cop1buflength, cop2buflength);

  return status;
}

static int
mep_print_insn (CGEN_CPU_DESC cd, bfd_vma pc, disassemble_info *info)
{
  int status;

  /* Extract and adapt to configuration number, if available. */
  if (info->section && info->section->owner)
    {
      bfd *abfd = info->section->owner;
      mep_config_index = abfd->tdata.elf_obj_data->elf_header->e_flags & EF_MEP_INDEX_MASK;
      /* This instantly redefines MEP_CONFIG, MEP_OMASK, .... MEP_VLIW64 */
    }

  /* Picking the right ISA bitmask for the current context is tricky.  */
  if (info->section)
    {
      if (info->section->flags & SEC_MEP_VLIW)
	{
	  /* Are we in 32 or 64 bit vliw mode?  */
	  if (MEP_VLIW64)
	    status = mep_examine_vliw64_insns (cd, pc, info);
	  else
	    status = mep_examine_vliw32_insns (cd, pc, info);
	  /* Both the above branches set their own isa bitmasks.  */
	}
      else
	{
	  cd->isas = & MEP_CORE_ISA;
	  status = default_print_insn (cd, pc, info);
	}
    }
  else /* sid or gdb */
    {
      status = default_print_insn (cd, pc, info);
    }

  return status;
}


/* -- opc.c */

void mep_cgen_print_operand
  (CGEN_CPU_DESC, int, PTR, CGEN_FIELDS *, void const *, bfd_vma, int);

/* Main entry point for printing operands.
   XINFO is a `void *' and not a `disassemble_info *' to not put a requirement
   of dis-asm.h on cgen.h.

   This function is basically just a big switch statement.  Earlier versions
   used tables to look up the function to use, but
   - if the table contains both assembler and disassembler functions then
     the disassembler contains much of the assembler and vice-versa,
   - there's a lot of inlining possibilities as things grow,
   - using a switch statement avoids the function call overhead.

   This function could be moved into `print_insn_normal', but keeping it
   separate makes clear the interface between `print_insn_normal' and each of
   the handlers.  */

void
mep_cgen_print_operand (CGEN_CPU_DESC cd,
			   int opindex,
			   void * xinfo,
			   CGEN_FIELDS *fields,
			   void const *attrs ATTRIBUTE_UNUSED,
			   bfd_vma pc,
			   int length)
{
  disassemble_info *info = (disassemble_info *) xinfo;

  switch (opindex)
    {
    case MEP_OPERAND_ADDR24A4 :
      print_normal (cd, info, fields->f_24u8a4n, 0|(1<<CGEN_OPERAND_VIRTUAL), pc, length);
      break;
    case MEP_OPERAND_CALLNUM :
      print_normal (cd, info, fields->f_callnum, 0|(1<<CGEN_OPERAND_VIRTUAL), pc, length);
      break;
    case MEP_OPERAND_CCCC :
      print_normal (cd, info, fields->f_rm, 0, pc, length);
      break;
    case MEP_OPERAND_CCRN :
      print_keyword (cd, info, & mep_cgen_opval_h_ccr, fields->f_ccrn, 0|(1<<CGEN_OPERAND_VIRTUAL));
      break;
    case MEP_OPERAND_CDISP8 :
      print_normal (cd, info, fields->f_8s24, 0|(1<<CGEN_OPERAND_SIGNED), pc, length);
      break;
    case MEP_OPERAND_CDISP8A2 :
      print_normal (cd, info, fields->f_8s24a2, 0|(1<<CGEN_OPERAND_SIGNED), pc, length);
      break;
    case MEP_OPERAND_CDISP8A4 :
      print_normal (cd, info, fields->f_8s24a4, 0|(1<<CGEN_OPERAND_SIGNED), pc, length);
      break;
    case MEP_OPERAND_CDISP8A8 :
      print_normal (cd, info, fields->f_8s24a8, 0|(1<<CGEN_OPERAND_SIGNED), pc, length);
      break;
    case MEP_OPERAND_CIMM4 :
      print_normal (cd, info, fields->f_rn, 0, pc, length);
      break;
    case MEP_OPERAND_CIMM5 :
      print_normal (cd, info, fields->f_5u24, 0, pc, length);
      break;
    case MEP_OPERAND_CODE16 :
      print_normal (cd, info, fields->f_16u16, 0, pc, length);
      break;
    case MEP_OPERAND_CODE24 :
      print_normal (cd, info, fields->f_24u4n, 0|(1<<CGEN_OPERAND_VIRTUAL), pc, length);
      break;
    case MEP_OPERAND_CP_FLAG :
      print_keyword (cd, info, & mep_cgen_opval_h_ccr, 0, 0);
      break;
    case MEP_OPERAND_CRN :
      print_keyword (cd, info, & mep_cgen_opval_h_cr, fields->f_crn, 0);
      break;
    case MEP_OPERAND_CRN64 :
      print_keyword (cd, info, & mep_cgen_opval_h_cr64, fields->f_crn, 0);
      break;
    case MEP_OPERAND_CRNX :
      print_keyword (cd, info, & mep_cgen_opval_h_cr, fields->f_crnx, 0|(1<<CGEN_OPERAND_VIRTUAL));
      break;
    case MEP_OPERAND_CRNX64 :
      print_keyword (cd, info, & mep_cgen_opval_h_cr64, fields->f_crnx, 0|(1<<CGEN_OPERAND_VIRTUAL));
      break;
    case MEP_OPERAND_CSRN :
      print_keyword (cd, info, & mep_cgen_opval_h_csr, fields->f_csrn, 0|(1<<CGEN_OPERAND_VIRTUAL));
      break;
    case MEP_OPERAND_CSRN_IDX :
      print_normal (cd, info, fields->f_csrn, 0|(1<<CGEN_OPERAND_VIRTUAL), pc, length);
      break;
    case MEP_OPERAND_DBG :
      print_keyword (cd, info, & mep_cgen_opval_h_csr, 0, 0);
      break;
    case MEP_OPERAND_DEPC :
      print_keyword (cd, info, & mep_cgen_opval_h_csr, 0, 0);
      break;
    case MEP_OPERAND_EPC :
      print_keyword (cd, info, & mep_cgen_opval_h_csr, 0, 0);
      break;
    case MEP_OPERAND_EXC :
      print_keyword (cd, info, & mep_cgen_opval_h_csr, 0, 0);
      break;
    case MEP_OPERAND_FMAX_CCRN :
      print_fmax_ccr (cd, info, & mep_cgen_opval_h_ccr, fields->f_fmax_4_4, 0);
      break;
    case MEP_OPERAND_FMAX_FRD :
      print_fmax_cr (cd, info, & mep_cgen_opval_h_cr, fields->f_fmax_frd, 0|(1<<CGEN_OPERAND_VIRTUAL));
      break;
    case MEP_OPERAND_FMAX_FRD_INT :
      print_fmax_cr (cd, info, & mep_cgen_opval_h_cr, fields->f_fmax_frd, 0|(1<<CGEN_OPERAND_VIRTUAL));
      break;
    case MEP_OPERAND_FMAX_FRM :
      print_fmax_cr (cd, info, & mep_cgen_opval_h_cr, fields->f_fmax_frm, 0|(1<<CGEN_OPERAND_VIRTUAL));
      break;
    case MEP_OPERAND_FMAX_FRN :
      print_fmax_cr (cd, info, & mep_cgen_opval_h_cr, fields->f_fmax_frn, 0|(1<<CGEN_OPERAND_VIRTUAL));
      break;
    case MEP_OPERAND_FMAX_FRN_INT :
      print_fmax_cr (cd, info, & mep_cgen_opval_h_cr, fields->f_fmax_frn, 0|(1<<CGEN_OPERAND_VIRTUAL));
      break;
    case MEP_OPERAND_FMAX_RM :
      print_keyword (cd, info, & mep_cgen_opval_h_gpr, fields->f_fmax_rm, 0);
      break;
    case MEP_OPERAND_HI :
      print_keyword (cd, info, & mep_cgen_opval_h_csr, 0, 0);
      break;
    case MEP_OPERAND_LO :
      print_keyword (cd, info, & mep_cgen_opval_h_csr, 0, 0);
      break;
    case MEP_OPERAND_LP :
      print_keyword (cd, info, & mep_cgen_opval_h_csr, 0, 0);
      break;
    case MEP_OPERAND_MB0 :
      print_keyword (cd, info, & mep_cgen_opval_h_csr, 0, 0);
      break;
    case MEP_OPERAND_MB1 :
      print_keyword (cd, info, & mep_cgen_opval_h_csr, 0, 0);
      break;
    case MEP_OPERAND_ME0 :
      print_keyword (cd, info, & mep_cgen_opval_h_csr, 0, 0);
      break;
    case MEP_OPERAND_ME1 :
      print_keyword (cd, info, & mep_cgen_opval_h_csr, 0, 0);
      break;
    case MEP_OPERAND_NPC :
      print_keyword (cd, info, & mep_cgen_opval_h_csr, 0, 0);
      break;
    case MEP_OPERAND_OPT :
      print_keyword (cd, info, & mep_cgen_opval_h_csr, 0, 0);
      break;
    case MEP_OPERAND_PCABS24A2 :
      print_address (cd, info, fields->f_24u5a2n, 0|(1<<CGEN_OPERAND_ABS_ADDR)|(1<<CGEN_OPERAND_VIRTUAL), pc, length);
      break;
    case MEP_OPERAND_PCREL12A2 :
      print_address (cd, info, fields->f_12s4a2, 0|(1<<CGEN_OPERAND_SIGNED)|(1<<CGEN_OPERAND_RELAX)|(1<<CGEN_OPERAND_PCREL_ADDR), pc, length);
      break;
    case MEP_OPERAND_PCREL17A2 :
      print_address (cd, info, fields->f_17s16a2, 0|(1<<CGEN_OPERAND_SIGNED)|(1<<CGEN_OPERAND_RELAX)|(1<<CGEN_OPERAND_PCREL_ADDR), pc, length);
      break;
    case MEP_OPERAND_PCREL24A2 :
      print_address (cd, info, fields->f_24s5a2n, 0|(1<<CGEN_OPERAND_SIGNED)|(1<<CGEN_OPERAND_PCREL_ADDR)|(1<<CGEN_OPERAND_VIRTUAL), pc, length);
      break;
    case MEP_OPERAND_PCREL8A2 :
      print_address (cd, info, fields->f_8s8a2, 0|(1<<CGEN_OPERAND_SIGNED)|(1<<CGEN_OPERAND_RELAX)|(1<<CGEN_OPERAND_PCREL_ADDR), pc, length);
      break;
    case MEP_OPERAND_PSW :
      print_keyword (cd, info, & mep_cgen_opval_h_csr, 0, 0);
      break;
    case MEP_OPERAND_R0 :
      print_keyword (cd, info, & mep_cgen_opval_h_gpr, 0, 0);
      break;
    case MEP_OPERAND_R1 :
      print_keyword (cd, info, & mep_cgen_opval_h_gpr, 0, 0);
      break;
    case MEP_OPERAND_RL :
      print_keyword (cd, info, & mep_cgen_opval_h_gpr, fields->f_rl, 0);
      break;
    case MEP_OPERAND_RM :
      print_keyword (cd, info, & mep_cgen_opval_h_gpr, fields->f_rm, 0);
      break;
    case MEP_OPERAND_RMA :
      print_keyword (cd, info, & mep_cgen_opval_h_gpr, fields->f_rm, 0);
      break;
    case MEP_OPERAND_RN :
      print_keyword (cd, info, & mep_cgen_opval_h_gpr, fields->f_rn, 0);
      break;
    case MEP_OPERAND_RN3 :
      print_keyword (cd, info, & mep_cgen_opval_h_gpr, fields->f_rn3, 0);
      break;
    case MEP_OPERAND_RN3C :
      print_keyword (cd, info, & mep_cgen_opval_h_gpr, fields->f_rn3, 0);
      break;
    case MEP_OPERAND_RN3L :
      print_keyword (cd, info, & mep_cgen_opval_h_gpr, fields->f_rn3, 0);
      break;
    case MEP_OPERAND_RN3S :
      print_keyword (cd, info, & mep_cgen_opval_h_gpr, fields->f_rn3, 0);
      break;
    case MEP_OPERAND_RN3UC :
      print_keyword (cd, info, & mep_cgen_opval_h_gpr, fields->f_rn3, 0);
      break;
    case MEP_OPERAND_RN3UL :
      print_keyword (cd, info, & mep_cgen_opval_h_gpr, fields->f_rn3, 0);
      break;
    case MEP_OPERAND_RN3US :
      print_keyword (cd, info, & mep_cgen_opval_h_gpr, fields->f_rn3, 0);
      break;
    case MEP_OPERAND_RNC :
      print_keyword (cd, info, & mep_cgen_opval_h_gpr, fields->f_rn, 0);
      break;
    case MEP_OPERAND_RNL :
      print_keyword (cd, info, & mep_cgen_opval_h_gpr, fields->f_rn, 0);
      break;
    case MEP_OPERAND_RNS :
      print_keyword (cd, info, & mep_cgen_opval_h_gpr, fields->f_rn, 0);
      break;
    case MEP_OPERAND_RNUC :
      print_keyword (cd, info, & mep_cgen_opval_h_gpr, fields->f_rn, 0);
      break;
    case MEP_OPERAND_RNUL :
      print_keyword (cd, info, & mep_cgen_opval_h_gpr, fields->f_rn, 0);
      break;
    case MEP_OPERAND_RNUS :
      print_keyword (cd, info, & mep_cgen_opval_h_gpr, fields->f_rn, 0);
      break;
    case MEP_OPERAND_SAR :
      print_keyword (cd, info, & mep_cgen_opval_h_csr, 0, 0);
      break;
    case MEP_OPERAND_SDISP16 :
      print_normal (cd, info, fields->f_16s16, 0|(1<<CGEN_OPERAND_SIGNED), pc, length);
      break;
    case MEP_OPERAND_SIMM16 :
      print_normal (cd, info, fields->f_16s16, 0|(1<<CGEN_OPERAND_SIGNED), pc, length);
      break;
    case MEP_OPERAND_SIMM6 :
      print_normal (cd, info, fields->f_6s8, 0|(1<<CGEN_OPERAND_SIGNED), pc, length);
      break;
    case MEP_OPERAND_SIMM8 :
      print_normal (cd, info, fields->f_8s8, 0|(1<<CGEN_OPERAND_SIGNED)|(1<<CGEN_OPERAND_RELOC_IMPLIES_OVERFLOW), pc, length);
      break;
    case MEP_OPERAND_SP :
      print_keyword (cd, info, & mep_cgen_opval_h_gpr, 0, 0);
      break;
    case MEP_OPERAND_SPR :
      print_spreg (cd, info, & mep_cgen_opval_h_gpr, 0, 0);
      break;
    case MEP_OPERAND_TP :
      print_keyword (cd, info, & mep_cgen_opval_h_gpr, 0, 0);
      break;
    case MEP_OPERAND_TPR :
      print_tpreg (cd, info, & mep_cgen_opval_h_gpr, 0, 0);
      break;
    case MEP_OPERAND_UDISP2 :
      print_normal (cd, info, fields->f_2u6, 0|(1<<CGEN_OPERAND_SIGNED), pc, length);
      break;
    case MEP_OPERAND_UDISP7 :
      print_normal (cd, info, fields->f_7u9, 0, pc, length);
      break;
    case MEP_OPERAND_UDISP7A2 :
      print_normal (cd, info, fields->f_7u9a2, 0, pc, length);
      break;
    case MEP_OPERAND_UDISP7A4 :
      print_normal (cd, info, fields->f_7u9a4, 0, pc, length);
      break;
    case MEP_OPERAND_UIMM16 :
      print_normal (cd, info, fields->f_16u16, 0, pc, length);
      break;
    case MEP_OPERAND_UIMM2 :
      print_normal (cd, info, fields->f_2u10, 0, pc, length);
      break;
    case MEP_OPERAND_UIMM24 :
      print_normal (cd, info, fields->f_24u8n, 0|(1<<CGEN_OPERAND_VIRTUAL), pc, length);
      break;
    case MEP_OPERAND_UIMM3 :
      print_normal (cd, info, fields->f_3u5, 0, pc, length);
      break;
    case MEP_OPERAND_UIMM4 :
      print_normal (cd, info, fields->f_4u8, 0, pc, length);
      break;
    case MEP_OPERAND_UIMM5 :
      print_normal (cd, info, fields->f_5u8, 0, pc, length);
      break;
    case MEP_OPERAND_UIMM7A4 :
      print_normal (cd, info, fields->f_7u9a4, 0, pc, length);
      break;
    case MEP_OPERAND_ZERO :
      print_normal (cd, info, 0, 0|(1<<CGEN_OPERAND_SIGNED), pc, length);
      break;

    default :
      /* xgettext:c-format */
      fprintf (stderr, _("Unrecognized field %d while printing insn.\n"),
	       opindex);
    abort ();
  }
}

cgen_print_fn * const mep_cgen_print_handlers[] = 
{
  print_insn_normal,
};


void
mep_cgen_init_dis (CGEN_CPU_DESC cd)
{
  mep_cgen_init_opcode_table (cd);
  mep_cgen_init_ibld_table (cd);
  cd->print_handlers = & mep_cgen_print_handlers[0];
  cd->print_operand = mep_cgen_print_operand;
}


/* Default print handler.  */

static void
print_normal (CGEN_CPU_DESC cd ATTRIBUTE_UNUSED,
	      void *dis_info,
	      long value,
	      unsigned int attrs,
	      bfd_vma pc ATTRIBUTE_UNUSED,
	      int length ATTRIBUTE_UNUSED)
{
  disassemble_info *info = (disassemble_info *) dis_info;

#ifdef CGEN_PRINT_NORMAL
  CGEN_PRINT_NORMAL (cd, info, value, attrs, pc, length);
#endif

  /* Print the operand as directed by the attributes.  */
  if (CGEN_BOOL_ATTR (attrs, CGEN_OPERAND_SEM_ONLY))
    ; /* nothing to do */
  else if (CGEN_BOOL_ATTR (attrs, CGEN_OPERAND_SIGNED))
    (*info->fprintf_func) (info->stream, "%ld", value);
  else
    (*info->fprintf_func) (info->stream, "0x%lx", value);
}

/* Default address handler.  */

static void
print_address (CGEN_CPU_DESC cd ATTRIBUTE_UNUSED,
	       void *dis_info,
	       bfd_vma value,
	       unsigned int attrs,
	       bfd_vma pc ATTRIBUTE_UNUSED,
	       int length ATTRIBUTE_UNUSED)
{
  disassemble_info *info = (disassemble_info *) dis_info;

#ifdef CGEN_PRINT_ADDRESS
  CGEN_PRINT_ADDRESS (cd, info, value, attrs, pc, length);
#endif

  /* Print the operand as directed by the attributes.  */
  if (CGEN_BOOL_ATTR (attrs, CGEN_OPERAND_SEM_ONLY))
    ; /* Nothing to do.  */
  else if (CGEN_BOOL_ATTR (attrs, CGEN_OPERAND_PCREL_ADDR))
    (*info->print_address_func) (value, info);
  else if (CGEN_BOOL_ATTR (attrs, CGEN_OPERAND_ABS_ADDR))
    (*info->print_address_func) (value, info);
  else if (CGEN_BOOL_ATTR (attrs, CGEN_OPERAND_SIGNED))
    (*info->fprintf_func) (info->stream, "%ld", (long) value);
  else
    (*info->fprintf_func) (info->stream, "0x%lx", (long) value);
}

/* Keyword print handler.  */

static void
print_keyword (CGEN_CPU_DESC cd ATTRIBUTE_UNUSED,
	       void *dis_info,
	       CGEN_KEYWORD *keyword_table,
	       long value,
	       unsigned int attrs ATTRIBUTE_UNUSED)
{
  disassemble_info *info = (disassemble_info *) dis_info;
  const CGEN_KEYWORD_ENTRY *ke;

  ke = cgen_keyword_lookup_value (keyword_table, value);
  if (ke != NULL)
    (*info->fprintf_func) (info->stream, "%s", ke->name);
  else
    (*info->fprintf_func) (info->stream, "???");
}

/* Default insn printer.

   DIS_INFO is defined as `void *' so the disassembler needn't know anything
   about disassemble_info.  */

static void
print_insn_normal (CGEN_CPU_DESC cd,
		   void *dis_info,
		   const CGEN_INSN *insn,
		   CGEN_FIELDS *fields,
		   bfd_vma pc,
		   int length)
{
  const CGEN_SYNTAX *syntax = CGEN_INSN_SYNTAX (insn);
  disassemble_info *info = (disassemble_info *) dis_info;
  const CGEN_SYNTAX_CHAR_TYPE *syn;

  CGEN_INIT_PRINT (cd);

  for (syn = CGEN_SYNTAX_STRING (syntax); *syn; ++syn)
    {
      if (CGEN_SYNTAX_MNEMONIC_P (*syn))
	{
	  (*info->fprintf_func) (info->stream, "%s", CGEN_INSN_MNEMONIC (insn));
	  continue;
	}
      if (CGEN_SYNTAX_CHAR_P (*syn))
	{
	  (*info->fprintf_func) (info->stream, "%c", CGEN_SYNTAX_CHAR (*syn));
	  continue;
	}

      /* We have an operand.  */
      mep_cgen_print_operand (cd, CGEN_SYNTAX_FIELD (*syn), info,
				 fields, CGEN_INSN_ATTRS (insn), pc, length);
    }
}

/* Subroutine of print_insn. Reads an insn into the given buffers and updates
   the extract info.
   Returns 0 if all is well, non-zero otherwise.  */

static int
read_insn (CGEN_CPU_DESC cd ATTRIBUTE_UNUSED,
	   bfd_vma pc,
	   disassemble_info *info,
	   bfd_byte *buf,
	   int buflen,
	   CGEN_EXTRACT_INFO *ex_info,
	   unsigned long *insn_value)
{
  int status = (*info->read_memory_func) (pc, buf, buflen, info);

  if (status != 0)
    {
      (*info->memory_error_func) (status, pc, info);
      return -1;
    }

  ex_info->dis_info = info;
  ex_info->valid = (1 << buflen) - 1;
  ex_info->insn_bytes = buf;

  *insn_value = bfd_get_bits (buf, buflen * 8, info->endian == BFD_ENDIAN_BIG);
  return 0;
}

/* Utility to print an insn.
   BUF is the base part of the insn, target byte order, BUFLEN bytes long.
   The result is the size of the insn in bytes or zero for an unknown insn
   or -1 if an error occurs fetching data (memory_error_func will have
   been called).  */

static int
print_insn (CGEN_CPU_DESC cd,
	    bfd_vma pc,
	    disassemble_info *info,
	    bfd_byte *buf,
	    unsigned int buflen)
{
  CGEN_INSN_INT insn_value;
  const CGEN_INSN_LIST *insn_list;
  CGEN_EXTRACT_INFO ex_info;
  int basesize;

  /* Extract base part of instruction, just in case CGEN_DIS_* uses it. */
  basesize = cd->base_insn_bitsize < buflen * 8 ?
                                     cd->base_insn_bitsize : buflen * 8;
  insn_value = cgen_get_insn_value (cd, buf, basesize);


  /* Fill in ex_info fields like read_insn would.  Don't actually call
     read_insn, since the incoming buffer is already read (and possibly
     modified a la m32r).  */
  ex_info.valid = (1 << buflen) - 1;
  ex_info.dis_info = info;
  ex_info.insn_bytes = buf;

  /* The instructions are stored in hash lists.
     Pick the first one and keep trying until we find the right one.  */

  insn_list = CGEN_DIS_LOOKUP_INSN (cd, (char *) buf, insn_value);
  while (insn_list != NULL)
    {
      const CGEN_INSN *insn = insn_list->insn;
      CGEN_FIELDS fields;
      int length;
      unsigned long insn_value_cropped;

#ifdef CGEN_VALIDATE_INSN_SUPPORTED 
      /* Not needed as insn shouldn't be in hash lists if not supported.  */
      /* Supported by this cpu?  */
      if (! mep_cgen_insn_supported (cd, insn))
        {
          insn_list = CGEN_DIS_NEXT_INSN (insn_list);
	  continue;
        }
#endif

      /* Basic bit mask must be correct.  */
      /* ??? May wish to allow target to defer this check until the extract
	 handler.  */

      /* Base size may exceed this instruction's size.  Extract the
         relevant part from the buffer. */
      if ((unsigned) (CGEN_INSN_BITSIZE (insn) / 8) < buflen &&
	  (unsigned) (CGEN_INSN_BITSIZE (insn) / 8) <= sizeof (unsigned long))
	insn_value_cropped = bfd_get_bits (buf, CGEN_INSN_BITSIZE (insn), 
					   info->endian == BFD_ENDIAN_BIG);
      else
	insn_value_cropped = insn_value;

      if ((insn_value_cropped & CGEN_INSN_BASE_MASK (insn))
	  == CGEN_INSN_BASE_VALUE (insn))
	{
	  /* Printing is handled in two passes.  The first pass parses the
	     machine insn and extracts the fields.  The second pass prints
	     them.  */

	  /* Make sure the entire insn is loaded into insn_value, if it
	     can fit.  */
	  if (((unsigned) CGEN_INSN_BITSIZE (insn) > cd->base_insn_bitsize) &&
	      (unsigned) (CGEN_INSN_BITSIZE (insn) / 8) <= sizeof (unsigned long))
	    {
	      unsigned long full_insn_value;
	      int rc = read_insn (cd, pc, info, buf,
				  CGEN_INSN_BITSIZE (insn) / 8,
				  & ex_info, & full_insn_value);
	      if (rc != 0)
		return rc;
	      length = CGEN_EXTRACT_FN (cd, insn)
		(cd, insn, &ex_info, full_insn_value, &fields, pc);
	    }
	  else
	    length = CGEN_EXTRACT_FN (cd, insn)
	      (cd, insn, &ex_info, insn_value_cropped, &fields, pc);

	  /* Length < 0 -> error.  */
	  if (length < 0)
	    return length;
	  if (length > 0)
	    {
	      CGEN_PRINT_FN (cd, insn) (cd, info, insn, &fields, pc, length);
	      /* Length is in bits, result is in bytes.  */
	      return length / 8;
	    }
	}

      insn_list = CGEN_DIS_NEXT_INSN (insn_list);
    }

  return 0;
}

/* Default value for CGEN_PRINT_INSN.
   The result is the size of the insn in bytes or zero for an unknown insn
   or -1 if an error occured fetching bytes.  */

#ifndef CGEN_PRINT_INSN
#define CGEN_PRINT_INSN default_print_insn
#endif

static int
default_print_insn (CGEN_CPU_DESC cd, bfd_vma pc, disassemble_info *info)
{
  bfd_byte buf[CGEN_MAX_INSN_SIZE];
  int buflen;
  int status;

  /* Attempt to read the base part of the insn.  */
  buflen = cd->base_insn_bitsize / 8;
  status = (*info->read_memory_func) (pc, buf, buflen, info);

  /* Try again with the minimum part, if min < base.  */
  if (status != 0 && (cd->min_insn_bitsize < cd->base_insn_bitsize))
    {
      buflen = cd->min_insn_bitsize / 8;
      status = (*info->read_memory_func) (pc, buf, buflen, info);
    }

  if (status != 0)
    {
      (*info->memory_error_func) (status, pc, info);
      return -1;
    }

  return print_insn (cd, pc, info, buf, buflen);
}

/* Main entry point.
   Print one instruction from PC on INFO->STREAM.
   Return the size of the instruction (in bytes).  */

typedef struct cpu_desc_list
{
  struct cpu_desc_list *next;
  CGEN_BITSET *isa;
  int mach;
  int endian;
  CGEN_CPU_DESC cd;
} cpu_desc_list;

int
print_insn_mep (bfd_vma pc, disassemble_info *info)
{
  static cpu_desc_list *cd_list = 0;
  cpu_desc_list *cl = 0;
  static CGEN_CPU_DESC cd = 0;
  static CGEN_BITSET *prev_isa;
  static int prev_mach;
  static int prev_endian;
  int length;
  CGEN_BITSET *isa;
  int mach;
  int endian = (info->endian == BFD_ENDIAN_BIG
		? CGEN_ENDIAN_BIG
		: CGEN_ENDIAN_LITTLE);
  enum bfd_architecture arch;

  /* ??? gdb will set mach but leave the architecture as "unknown" */
#ifndef CGEN_BFD_ARCH
#define CGEN_BFD_ARCH bfd_arch_mep
#endif
  arch = info->arch;
  if (arch == bfd_arch_unknown)
    arch = CGEN_BFD_ARCH;
   
  /* There's no standard way to compute the machine or isa number
     so we leave it to the target.  */
#ifdef CGEN_COMPUTE_MACH
  mach = CGEN_COMPUTE_MACH (info);
#else
  mach = info->mach;
#endif

#ifdef CGEN_COMPUTE_ISA
  {
    static CGEN_BITSET *permanent_isa;

    if (!permanent_isa)
      permanent_isa = cgen_bitset_create (MAX_ISAS);
    isa = permanent_isa;
    cgen_bitset_clear (isa);
    cgen_bitset_add (isa, CGEN_COMPUTE_ISA (info));
  }
#else
  isa = info->insn_sets;
#endif

  /* If we've switched cpu's, try to find a handle we've used before */
  if (cd
      && (cgen_bitset_compare (isa, prev_isa) != 0
	  || mach != prev_mach
	  || endian != prev_endian))
    {
      cd = 0;
      for (cl = cd_list; cl; cl = cl->next)
	{
	  if (cgen_bitset_compare (cl->isa, isa) == 0 &&
	      cl->mach == mach &&
	      cl->endian == endian)
	    {
	      cd = cl->cd;
 	      prev_isa = cd->isas;
	      break;
	    }
	}
    } 

  /* If we haven't initialized yet, initialize the opcode table.  */
  if (! cd)
    {
      const bfd_arch_info_type *arch_type = bfd_lookup_arch (arch, mach);
      const char *mach_name;

      if (!arch_type)
	abort ();
      mach_name = arch_type->printable_name;

      prev_isa = cgen_bitset_copy (isa);
      prev_mach = mach;
      prev_endian = endian;
      cd = mep_cgen_cpu_open (CGEN_CPU_OPEN_ISAS, prev_isa,
				 CGEN_CPU_OPEN_BFDMACH, mach_name,
				 CGEN_CPU_OPEN_ENDIAN, prev_endian,
				 CGEN_CPU_OPEN_END);
      if (!cd)
	abort ();

      /* Save this away for future reference.  */
      cl = xmalloc (sizeof (struct cpu_desc_list));
      cl->cd = cd;
      cl->isa = prev_isa;
      cl->mach = mach;
      cl->endian = endian;
      cl->next = cd_list;
      cd_list = cl;

      mep_cgen_init_dis (cd);
    }

  /* We try to have as much common code as possible.
     But at this point some targets need to take over.  */
  /* ??? Some targets may need a hook elsewhere.  Try to avoid this,
     but if not possible try to move this hook elsewhere rather than
     have two hooks.  */
  length = CGEN_PRINT_INSN (cd, pc, info);
  if (length > 0)
    return length;
  if (length < 0)
    return -1;

  (*info->fprintf_func) (info->stream, UNKNOWN_INSN_MSG);
  return cd->default_insn_bitsize / 8;
}
