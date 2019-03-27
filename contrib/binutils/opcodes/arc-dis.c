/* Instruction printing code for the ARC.
   Copyright 1994, 1995, 1997, 1998, 2000, 2001, 2002, 2005
   Free Software Foundation, Inc.
   Contributed by Doug Evans (dje@cygnus.com).

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
   Foundation, Inc., 51 Franklin Street - Fifth Floor, Boston,
   MA 02110-1301, USA.  */

#include "ansidecl.h"
#include "libiberty.h"
#include "dis-asm.h"
#include "opcode/arc.h"
#include "elf-bfd.h"
#include "elf/arc.h"
#include <string.h>
#include "opintl.h"

#include <stdarg.h>
#include "arc-dis.h"
#include "arc-ext.h"

#ifndef dbg
#define dbg (0)
#endif

/* Classification of the opcodes for the decoder to print 
   the instructions.  */

typedef enum
{
  CLASS_A4_ARITH,	     
  CLASS_A4_OP3_GENERAL,
  CLASS_A4_FLAG,
  /* All branches other than JC.  */
  CLASS_A4_BRANCH,
  CLASS_A4_JC ,
  /* All loads other than immediate 
     indexed loads.  */
  CLASS_A4_LD0,
  CLASS_A4_LD1,
  CLASS_A4_ST,
  CLASS_A4_SR,
  /* All single operand instructions.  */
  CLASS_A4_OP3_SUBOPC3F,
  CLASS_A4_LR
} a4_decoding_class;

#define BIT(word,n)	((word) & (1 << n))
#define BITS(word,s,e)  (((word) << (31 - e)) >> (s + (31 - e)))
#define OPCODE(word)	(BITS ((word), 27, 31))
#define FIELDA(word)	(BITS ((word), 21, 26))
#define FIELDB(word)	(BITS ((word), 15, 20))
#define FIELDC(word)	(BITS ((word),  9, 14))

/* FIELD D is signed in all of its uses, so we make sure argument is
   treated as signed for bit shifting purposes:  */
#define FIELDD(word)	(BITS (((signed int)word), 0, 8))

#define PUT_NEXT_WORD_IN(a)						\
  do									\
    {									\
      if (is_limm == 1 && !NEXT_WORD (1))				\
        mwerror (state, _("Illegal limm reference in last instruction!\n")); \
      a = state->words[1];						\
    }									\
  while (0)

#define CHECK_FLAG_COND_NULLIFY()				\
  do								\
    {								\
      if (is_shimm == 0)					\
        {							\
          flag = BIT (state->words[0], 8);			\
          state->nullifyMode = BITS (state->words[0], 5, 6);	\
          cond = BITS (state->words[0], 0, 4);			\
        }							\
    }								\
  while (0)

#define CHECK_COND()				\
  do						\
    {						\
      if (is_shimm == 0)			\
        cond = BITS (state->words[0], 0, 4);	\
    }						\
  while (0)

#define CHECK_FIELD(field)			\
  do						\
    {						\
      if (field == 62)				\
        {					\
          is_limm++;				\
	  field##isReg = 0;			\
	  PUT_NEXT_WORD_IN (field);		\
	  limm_value = field;			\
	}					\
      else if (field > 60)			\
        {					\
	  field##isReg = 0;			\
	  is_shimm++;				\
	  flag = (field == 61);			\
	  field = FIELDD (state->words[0]);	\
	}					\
    }						\
  while (0)

#define CHECK_FIELD_A()				\
  do						\
    {						\
      fieldA = FIELDA (state->words[0]);	\
      if (fieldA > 60)				\
        {					\
	  fieldAisReg = 0;			\
	  fieldA = 0;				\
	}					\
    }						\
  while (0)

#define CHECK_FIELD_B()				\
  do						\
    {						\
      fieldB = FIELDB (state->words[0]);	\
      CHECK_FIELD (fieldB);			\
    }						\
  while (0)

#define CHECK_FIELD_C()				\
  do						\
    {						\
      fieldC = FIELDC (state->words[0]);	\
      CHECK_FIELD (fieldC);			\
    }						\
  while (0)

#define IS_SMALL(x)                 (((field##x) < 256) && ((field##x) > -257))
#define IS_REG(x)                    (field##x##isReg)
#define WRITE_FORMAT_LB_Rx_RB(x)     WRITE_FORMAT (x, "[","]","","")
#define WRITE_FORMAT_x_COMMA_LB(x)   WRITE_FORMAT (x, "",",[","",",[")
#define WRITE_FORMAT_COMMA_x_RB(x)   WRITE_FORMAT (x, ",","]",",","]")
#define WRITE_FORMAT_x_RB(x)         WRITE_FORMAT (x, "","]","","]")
#define WRITE_FORMAT_COMMA_x(x)      WRITE_FORMAT (x, ",","",",","")
#define WRITE_FORMAT_x_COMMA(x)      WRITE_FORMAT (x, "",",","",",")
#define WRITE_FORMAT_x(x)            WRITE_FORMAT (x, "","","","")
#define WRITE_FORMAT(x,cb1,ca1,cb,ca) strcat (formatString,		\
				     (IS_REG (x) ? cb1"%r"ca1 :		\
				      usesAuxReg ? cb"%a"ca :		\
				      IS_SMALL (x) ? cb"%d"ca : cb"%h"ca))
#define WRITE_FORMAT_RB()	strcat (formatString, "]")
#define WRITE_COMMENT(str)	(state->comm[state->commNum++] = (str))
#define WRITE_NOP_COMMENT()	if (!fieldAisReg && !flag) WRITE_COMMENT ("nop");

#define NEXT_WORD(x)	(offset += 4, state->words[x])

#define add_target(x)	(state->targets[state->tcnt++] = (x))

static char comment_prefix[] = "\t; ";

static const char *
core_reg_name (struct arcDisState * state, int val)
{
  if (state->coreRegName)
    return (*state->coreRegName)(state->_this, val);
  return 0;
}

static const char *
aux_reg_name (struct arcDisState * state, int val)
{
  if (state->auxRegName)
    return (*state->auxRegName)(state->_this, val);
  return 0;
}

static const char *
cond_code_name (struct arcDisState * state, int val)
{
  if (state->condCodeName)
    return (*state->condCodeName)(state->_this, val);
  return 0;
}

static const char *
instruction_name (struct arcDisState * state,
		  int    op1,
		  int    op2,
		  int *  flags)
{
  if (state->instName)
    return (*state->instName)(state->_this, op1, op2, flags);
  return 0;
}

static void
mwerror (struct arcDisState * state, const char * msg)
{
  if (state->err != 0)
    (*state->err)(state->_this, (msg));
}

static const char *
post_address (struct arcDisState * state, int addr)
{
  static char id[3 * ARRAY_SIZE (state->addresses)];
  int j, i = state->acnt;

  if (i < ((int) ARRAY_SIZE (state->addresses)))
    {
      state->addresses[i] = addr;
      ++state->acnt;
      j = i*3;
      id[j+0] = '@';
      id[j+1] = '0'+i;
      id[j+2] = 0;

      return id + j;
    }
  return "";
}

static void
arc_sprintf (struct arcDisState *state, char *buf, const char *format, ...)
{
  char *bp;
  const char *p;
  int size, leading_zero, regMap[2];
  long auxNum;
  va_list ap;

  va_start (ap, format);

  bp = buf;
  *bp = 0;
  p = format;
  auxNum = -1;
  regMap[0] = 0;
  regMap[1] = 0;

  while (1)
    switch (*p++)
      {
      case 0:
	goto DOCOMM; /* (return)  */
      default:
	*bp++ = p[-1];
	break;
      case '%':
	size = 0;
	leading_zero = 0;
      RETRY: ;
	switch (*p++)
	  {
	  case '0':
	  case '1':
	  case '2':
	  case '3':
	  case '4':
	  case '5':
	  case '6':
	  case '7':
	  case '8':
	  case '9':
	    {
	      /* size.  */
	      size = p[-1] - '0';
	      if (size == 0)
		leading_zero = 1; /* e.g. %08x  */
	      while (*p >= '0' && *p <= '9')
		{
		  size = size * 10 + *p - '0';
		  p++;
		}
	      goto RETRY;
	    }
#define inc_bp() bp = bp + strlen (bp)

	  case 'h':
	    {
	      unsigned u = va_arg (ap, int);

	      /* Hex.  We can change the format to 0x%08x in
		 one place, here, if we wish.
		 We add underscores for easy reading.  */
	      if (u > 65536)
		sprintf (bp, "0x%x_%04x", u >> 16, u & 0xffff);
	      else
		sprintf (bp, "0x%x", u);
	      inc_bp ();
	    }
	    break;
	  case 'X': case 'x':
	    {
	      int val = va_arg (ap, int);

	      if (size != 0)
		if (leading_zero)
		  sprintf (bp, "%0*x", size, val);
		else
		  sprintf (bp, "%*x", size, val);
	      else
		sprintf (bp, "%x", val);
	      inc_bp ();
	    }
	    break;
	  case 'd':
	    {
	      int val = va_arg (ap, int);

	      if (size != 0)
		sprintf (bp, "%*d", size, val);
	      else
		sprintf (bp, "%d", val);
	      inc_bp ();
	    }
	    break;
	  case 'r':
	    {
	      /* Register.  */
	      int val = va_arg (ap, int);

#define REG2NAME(num, name) case num: sprintf (bp, ""name); \
  regMap[(num < 32) ? 0 : 1] |= 1 << (num - ((num < 32) ? 0 : 32)); break;

	      switch (val)
		{
		  REG2NAME (26, "gp");
		  REG2NAME (27, "fp");
		  REG2NAME (28, "sp");
		  REG2NAME (29, "ilink1");
		  REG2NAME (30, "ilink2");
		  REG2NAME (31, "blink");
		  REG2NAME (60, "lp_count");
		default:
		  {
		    const char * ext;

		    ext = core_reg_name (state, val);
		    if (ext)
		      sprintf (bp, "%s", ext);
		    else
		      sprintf (bp,"r%d",val);
		  }
		  break;
		}
	      inc_bp ();
	    } break;

	  case 'a':
	    {
	      /* Aux Register.  */
	      int val = va_arg (ap, int);

#define AUXREG2NAME(num, name) case num: sprintf (bp,name); break;

	      switch (val)
		{
		  AUXREG2NAME (0x0, "status");
		  AUXREG2NAME (0x1, "semaphore");
		  AUXREG2NAME (0x2, "lp_start");
		  AUXREG2NAME (0x3, "lp_end");
		  AUXREG2NAME (0x4, "identity");
		  AUXREG2NAME (0x5, "debug");
		default:
		  {
		    const char *ext;

		    ext = aux_reg_name (state, val);
		    if (ext)
		      sprintf (bp, "%s", ext);
		    else
		      arc_sprintf (state, bp, "%h", val);
		  }
		  break;
		}
	      inc_bp ();
	    }
	    break;

	  case 's':
	    {
	      sprintf (bp, "%s", va_arg (ap, char *));
	      inc_bp ();
	    }
	    break;

	  default:
	    fprintf (stderr, "?? format %c\n", p[-1]);
	    break;
	  }
      }

 DOCOMM: *bp = 0;
  va_end (ap);
}

static void
write_comments_(struct arcDisState * state,
		int shimm,
		int is_limm,
		long limm_value)
{
  if (state->commentBuffer != 0)
    {
      int i;

      if (is_limm)
	{
	  const char *name = post_address (state, limm_value + shimm);

	  if (*name != 0)
	    WRITE_COMMENT (name);
	}
      for (i = 0; i < state->commNum; i++)
	{
	  if (i == 0)
	    strcpy (state->commentBuffer, comment_prefix);
	  else
	    strcat (state->commentBuffer, ", ");
	  strncat (state->commentBuffer, state->comm[i],
		   sizeof (state->commentBuffer));
	}
    }
}

#define write_comments2(x) write_comments_ (state, x, is_limm, limm_value)
#define write_comments()   write_comments2 (0)

static const char *condName[] =
{
  /* 0..15.  */
  ""   , "z"  , "nz" , "p"  , "n"  , "c"  , "nc" , "v"  ,
  "nv" , "gt" , "ge" , "lt" , "le" , "hi" , "ls" , "pnz"
};

static void
write_instr_name_(struct arcDisState * state,
		  const char * instrName,
		  int cond,
		  int condCodeIsPartOfName,
		  int flag,
		  int signExtend,
		  int addrWriteBack,
		  int directMem)
{
  strcpy (state->instrBuffer, instrName);

  if (cond > 0)
    {
      const char *cc = 0;

      if (!condCodeIsPartOfName)
	strcat (state->instrBuffer, ".");

      if (cond < 16)
	cc = condName[cond];
      else
	cc = cond_code_name (state, cond);

      if (!cc)
	cc = "???";

      strcat (state->instrBuffer, cc);
    }

  if (flag)
    strcat (state->instrBuffer, ".f");

  switch (state->nullifyMode)
    {
    case BR_exec_always:
      strcat (state->instrBuffer, ".d");
      break;
    case BR_exec_when_jump:
      strcat (state->instrBuffer, ".jd");
      break;
    }

  if (signExtend)
    strcat (state->instrBuffer, ".x");

  if (addrWriteBack)
    strcat (state->instrBuffer, ".a");

  if (directMem)
    strcat (state->instrBuffer, ".di");
}

#define write_instr_name()						\
  do									\
    {									\
      write_instr_name_(state, instrName,cond, condCodeIsPartOfName,	\
			flag, signExtend, addrWriteBack, directMem);	\
      formatString[0] = '\0';						\
    }									\
  while (0)

enum
{
  op_LD0 = 0, op_LD1 = 1, op_ST  = 2, op_3   = 3,
  op_BC  = 4, op_BLC = 5, op_LPC = 6, op_JC  = 7,
  op_ADD = 8, op_ADC = 9, op_SUB = 10, op_SBC = 11,
  op_AND = 12, op_OR  = 13, op_BIC = 14, op_XOR = 15
};

extern disassemble_info tm_print_insn_info;

static int
dsmOneArcInst (bfd_vma addr, struct arcDisState * state)
{
  int condCodeIsPartOfName = 0;
  a4_decoding_class decodingClass;
  const char * instrName;
  int repeatsOp = 0;
  int fieldAisReg = 1;
  int fieldBisReg = 1;
  int fieldCisReg = 1;
  int fieldA;
  int fieldB;
  int fieldC = 0;
  int flag = 0;
  int cond = 0;
  int is_shimm = 0;
  int is_limm = 0;
  long limm_value = 0;
  int signExtend = 0;
  int addrWriteBack = 0;
  int directMem = 0;
  int is_linked = 0;
  int offset = 0;
  int usesAuxReg = 0;
  int flags;
  int ignoreFirstOpd;
  char formatString[60];

  state->instructionLen = 4;
  state->nullifyMode = BR_exec_when_no_jump;
  state->opWidth = 12;
  state->isBranch = 0;

  state->_mem_load = 0;
  state->_ea_present = 0;
  state->_load_len = 0;
  state->ea_reg1 = no_reg;
  state->ea_reg2 = no_reg;
  state->_offset = 0;

  if (! NEXT_WORD (0))
    return 0;

  state->_opcode = OPCODE (state->words[0]);
  instrName = 0;
  decodingClass = CLASS_A4_ARITH; /* default!  */
  repeatsOp = 0;
  condCodeIsPartOfName=0;
  state->commNum = 0;
  state->tcnt = 0;
  state->acnt = 0;
  state->flow = noflow;
  ignoreFirstOpd = 0;

  if (state->commentBuffer)
    state->commentBuffer[0] = '\0';

  switch (state->_opcode)
    {
    case op_LD0:
      switch (BITS (state->words[0],1,2))
	{
	case 0:
	  instrName = "ld";
	  state->_load_len = 4;
	  break;
	case 1:
	  instrName = "ldb";
	  state->_load_len = 1;
	  break;
	case 2:
	  instrName = "ldw";
	  state->_load_len = 2;
	  break;
	default:
	  instrName = "??? (0[3])";
	  state->flow = invalid_instr;
	  break;
	}
      decodingClass = CLASS_A4_LD0;
      break;

    case op_LD1:
      if (BIT (state->words[0],13))
	{
	  instrName = "lr";
	  decodingClass = CLASS_A4_LR;
	}
      else
	{
	  switch (BITS (state->words[0], 10, 11))
	    {
	    case 0:
	      instrName = "ld";
	      state->_load_len = 4;
	      break;
	    case 1:
	      instrName = "ldb";
	      state->_load_len = 1;
	      break;
	    case 2:
	      instrName = "ldw";
	      state->_load_len = 2;
	      break;
	    default:
	      instrName = "??? (1[3])";
	      state->flow = invalid_instr;
	      break;
	    }
	  decodingClass = CLASS_A4_LD1;
	}
      break;

    case op_ST:
      if (BIT (state->words[0], 25))
	{
	  instrName = "sr";
	  decodingClass = CLASS_A4_SR;
	}
      else
	{
	  switch (BITS (state->words[0], 22, 23))
	    {
	    case 0:
	      instrName = "st";
	      break;
	    case 1:
	      instrName = "stb";
	      break;
	    case 2:
	      instrName = "stw";
	      break;
	    default:
	      instrName = "??? (2[3])";
	      state->flow = invalid_instr;
	      break;
	    }
	  decodingClass = CLASS_A4_ST;
	}
      break;

    case op_3:
      decodingClass = CLASS_A4_OP3_GENERAL;  /* default for opcode 3...  */
      switch (FIELDC (state->words[0]))
	{
	case  0:
	  instrName = "flag";
	  decodingClass = CLASS_A4_FLAG;
	  break;
	case  1:
	  instrName = "asr";
	  break;
	case  2:
	  instrName = "lsr";
	  break;
	case  3:
	  instrName = "ror";
	  break;
	case  4:
	  instrName = "rrc";
	  break;
	case  5:
	  instrName = "sexb";
	  break;
	case  6:
	  instrName = "sexw";
	  break;
	case  7:
	  instrName = "extb";
	  break;
	case  8:
	  instrName = "extw";
	  break;
	case  0x3f:
	  {
	    decodingClass = CLASS_A4_OP3_SUBOPC3F;
	    switch (FIELDD (state->words[0]))
	      {
	      case 0:
		instrName = "brk";
		break;
	      case 1:
		instrName = "sleep";
		break;
	      case 2:
		instrName = "swi";
		break;
	      default:
		instrName = "???";
		state->flow=invalid_instr;
		break;
	      }
	  }
	  break;

	  /* ARC Extension Library Instructions
	     NOTE: We assume that extension codes are these instrs.  */
	default:
	  instrName = instruction_name (state,
					state->_opcode,
					FIELDC (state->words[0]),
					&flags);
	  if (!instrName)
	    {
	      instrName = "???";
	      state->flow = invalid_instr;
	    }
	  if (flags & IGNORE_FIRST_OPD)
	    ignoreFirstOpd = 1;
	  break;
	}
      break;

    case op_BC:
      instrName = "b";
    case op_BLC:
      if (!instrName)
	instrName = "bl";
    case op_LPC:
      if (!instrName)
	instrName = "lp";
    case op_JC:
      if (!instrName)
	{
	  if (BITS (state->words[0],9,9))
	    {
	      instrName = "jl";
	      is_linked = 1;
	    }
	  else
	    {
	      instrName = "j";
	      is_linked = 0;
	    }
	}
      condCodeIsPartOfName = 1;
      decodingClass = ((state->_opcode == op_JC) ? CLASS_A4_JC : CLASS_A4_BRANCH );
      state->isBranch = 1;
      break;

    case op_ADD:
    case op_ADC:
    case op_AND:
      repeatsOp = (FIELDC (state->words[0]) == FIELDB (state->words[0]));

      switch (state->_opcode)
	{
	case op_ADD:
	  instrName = (repeatsOp ? "asl" : "add");
	  break;
	case op_ADC:
	  instrName = (repeatsOp ? "rlc" : "adc");
	  break;
	case op_AND:
	  instrName = (repeatsOp ? "mov" : "and");
	  break;
	}
      break;

    case op_SUB: instrName = "sub";
      break;
    case op_SBC: instrName = "sbc";
      break;
    case op_OR:  instrName = "or";
      break;
    case op_BIC: instrName = "bic";
      break;

    case op_XOR:
      if (state->words[0] == 0x7fffffff)
	{
	  /* NOP encoded as xor -1, -1, -1.   */
	  instrName = "nop";
	  decodingClass = CLASS_A4_OP3_SUBOPC3F;
	}
      else
	instrName = "xor";
      break;

    default:
      instrName = instruction_name (state,state->_opcode,0,&flags);
      /* if (instrName) printf("FLAGS=0x%x\n", flags);  */
      if (!instrName)
	{
	  instrName = "???";
	  state->flow=invalid_instr;
	}
      if (flags & IGNORE_FIRST_OPD)
	ignoreFirstOpd = 1;
      break;
    }

  fieldAisReg = fieldBisReg = fieldCisReg = 1; /* Assume regs for now.  */
  flag = cond = is_shimm = is_limm = 0;
  state->nullifyMode = BR_exec_when_no_jump;	/* 0  */
  signExtend = addrWriteBack = directMem = 0;
  usesAuxReg = 0;

  switch (decodingClass)
    {
    case CLASS_A4_ARITH:
      CHECK_FIELD_A ();
      CHECK_FIELD_B ();
      if (!repeatsOp)
	CHECK_FIELD_C ();
      CHECK_FLAG_COND_NULLIFY ();

      write_instr_name ();
      if (!ignoreFirstOpd)
	{
	  WRITE_FORMAT_x (A);
	  WRITE_FORMAT_COMMA_x (B);
	  if (!repeatsOp)
	    WRITE_FORMAT_COMMA_x (C);
	  WRITE_NOP_COMMENT ();
	  arc_sprintf (state, state->operandBuffer, formatString,
		      fieldA, fieldB, fieldC);
	}
      else
	{
	  WRITE_FORMAT_x (B);
	  if (!repeatsOp)
	    WRITE_FORMAT_COMMA_x (C);
	  arc_sprintf (state, state->operandBuffer, formatString,
		      fieldB, fieldC);
	}
      write_comments ();
      break;

    case CLASS_A4_OP3_GENERAL:
      CHECK_FIELD_A ();
      CHECK_FIELD_B ();
      CHECK_FLAG_COND_NULLIFY ();

      write_instr_name ();
      if (!ignoreFirstOpd)
	{
	  WRITE_FORMAT_x (A);
	  WRITE_FORMAT_COMMA_x (B);
	  WRITE_NOP_COMMENT ();
	  arc_sprintf (state, state->operandBuffer, formatString,
		      fieldA, fieldB);
	}
      else
	{
	  WRITE_FORMAT_x (B);
	  arc_sprintf (state, state->operandBuffer, formatString, fieldB);
	}
      write_comments ();
      break;

    case CLASS_A4_FLAG:
      CHECK_FIELD_B ();
      CHECK_FLAG_COND_NULLIFY ();
      flag = 0; /* This is the FLAG instruction -- it's redundant.  */

      write_instr_name ();
      WRITE_FORMAT_x (B);
      arc_sprintf (state, state->operandBuffer, formatString, fieldB);
      write_comments ();
      break;

    case CLASS_A4_BRANCH:
      fieldA = BITS (state->words[0],7,26) << 2;
      fieldA = (fieldA << 10) >> 10; /* Make it signed.  */
      fieldA += addr + 4;
      CHECK_FLAG_COND_NULLIFY ();
      flag = 0;

      write_instr_name ();
      /* This address could be a label we know. Convert it.  */
      if (state->_opcode != op_LPC /* LP  */)
	{
	  add_target (fieldA); /* For debugger.  */
	  state->flow = state->_opcode == op_BLC /* BL  */
	    ? direct_call
	    : direct_jump;
	  /* indirect calls are achieved by "lr blink,[status];
	     lr dest<- func addr; j [dest]"  */
	}

      strcat (formatString, "%s"); /* Address/label name.  */
      arc_sprintf (state, state->operandBuffer, formatString,
		  post_address (state, fieldA));
      write_comments ();
      break;

    case CLASS_A4_JC:
      /* For op_JC -- jump to address specified.
	 Also covers jump and link--bit 9 of the instr. word
	 selects whether linked, thus "is_linked" is set above.  */
      fieldA = 0;
      CHECK_FIELD_B ();
      CHECK_FLAG_COND_NULLIFY ();

      if (!fieldBisReg)
	{
	  fieldAisReg = 0;
	  fieldA = (fieldB >> 25) & 0x7F; /* Flags.  */
	  fieldB = (fieldB & 0xFFFFFF) << 2;
	  state->flow = is_linked ? direct_call : direct_jump;
	  add_target (fieldB);
	  /* Screwy JLcc requires .jd mode to execute correctly
	     but we pretend it is .nd (no delay slot).  */
	  if (is_linked && state->nullifyMode == BR_exec_when_jump)
	    state->nullifyMode = BR_exec_when_no_jump;
	}
      else
	{
	  state->flow = is_linked ? indirect_call : indirect_jump;
	  /* We should also treat this as indirect call if NOT linked
	     but the preceding instruction was a "lr blink,[status]"
	     and we have a delay slot with "add blink,blink,2".
	     For now we can't detect such.  */
	  state->register_for_indirect_jump = fieldB;
	}

      write_instr_name ();
      strcat (formatString,
	      IS_REG (B) ? "[%r]" : "%s"); /* Address/label name.  */
      if (fieldA != 0)
	{
	  fieldAisReg = 0;
	  WRITE_FORMAT_COMMA_x (A);
	}
      if (IS_REG (B))
	arc_sprintf (state, state->operandBuffer, formatString, fieldB, fieldA);
      else
	arc_sprintf (state, state->operandBuffer, formatString,
		    post_address (state, fieldB), fieldA);
      write_comments ();
      break;

    case CLASS_A4_LD0:
      /* LD instruction.
	 B and C can be regs, or one (both?) can be limm.  */
      CHECK_FIELD_A ();
      CHECK_FIELD_B ();
      CHECK_FIELD_C ();
      if (dbg)
	printf ("5:b reg %d %d c reg %d %d  \n",
		fieldBisReg,fieldB,fieldCisReg,fieldC);
      state->_offset = 0;
      state->_ea_present = 1;
      if (fieldBisReg)
	state->ea_reg1 = fieldB;
      else
	state->_offset += fieldB;
      if (fieldCisReg)
	state->ea_reg2 = fieldC;
      else
	state->_offset += fieldC;
      state->_mem_load = 1;

      directMem     = BIT (state->words[0], 5);
      addrWriteBack = BIT (state->words[0], 3);
      signExtend    = BIT (state->words[0], 0);

      write_instr_name ();
      WRITE_FORMAT_x_COMMA_LB(A);
      if (fieldBisReg || fieldB != 0)
	WRITE_FORMAT_x_COMMA (B);
      else
	fieldB = fieldC;

      WRITE_FORMAT_x_RB (C);
      arc_sprintf (state, state->operandBuffer, formatString,
		  fieldA, fieldB, fieldC);
      write_comments ();
      break;

    case CLASS_A4_LD1:
      /* LD instruction.  */
      CHECK_FIELD_B ();
      CHECK_FIELD_A ();
      fieldC = FIELDD (state->words[0]);

      if (dbg)
	printf ("6:b reg %d %d c 0x%x  \n",
		fieldBisReg, fieldB, fieldC);
      state->_ea_present = 1;
      state->_offset = fieldC;
      state->_mem_load = 1;
      if (fieldBisReg)
	state->ea_reg1 = fieldB;
      /* Field B is either a shimm (same as fieldC) or limm (different!)
	 Say ea is not present, so only one of us will do the name lookup.  */
      else
	state->_offset += fieldB, state->_ea_present = 0;

      directMem     = BIT (state->words[0],14);
      addrWriteBack = BIT (state->words[0],12);
      signExtend    = BIT (state->words[0],9);

      write_instr_name ();
      WRITE_FORMAT_x_COMMA_LB (A);
      if (!fieldBisReg)
	{
	  fieldB = state->_offset;
	  WRITE_FORMAT_x_RB (B);
	}
      else
	{
	  WRITE_FORMAT_x (B);
	  if (fieldC != 0 && !BIT (state->words[0],13))
	    {
	      fieldCisReg = 0;
	      WRITE_FORMAT_COMMA_x_RB (C);
	    }
	  else
	    WRITE_FORMAT_RB ();
	}
      arc_sprintf (state, state->operandBuffer, formatString,
		  fieldA, fieldB, fieldC);
      write_comments ();
      break;

    case CLASS_A4_ST:
      /* ST instruction.  */
      CHECK_FIELD_B();
      CHECK_FIELD_C();
      fieldA = FIELDD(state->words[0]); /* shimm  */

      /* [B,A offset]  */
      if (dbg) printf("7:b reg %d %x off %x\n",
		      fieldBisReg,fieldB,fieldA);
      state->_ea_present = 1;
      state->_offset = fieldA;
      if (fieldBisReg)
	state->ea_reg1 = fieldB;
      /* Field B is either a shimm (same as fieldA) or limm (different!)
	 Say ea is not present, so only one of us will do the name lookup.
	 (for is_limm we do the name translation here).  */
      else
	state->_offset += fieldB, state->_ea_present = 0;

      directMem     = BIT (state->words[0], 26);
      addrWriteBack = BIT (state->words[0], 24);

      write_instr_name ();
      WRITE_FORMAT_x_COMMA_LB(C);

      if (!fieldBisReg)
	{
	  fieldB = state->_offset;
	  WRITE_FORMAT_x_RB (B);
	}
      else
	{
	  WRITE_FORMAT_x (B);
	  if (fieldBisReg && fieldA != 0)
	    {
	      fieldAisReg = 0;
	      WRITE_FORMAT_COMMA_x_RB(A);
	    }
	  else
	    WRITE_FORMAT_RB();
	}
      arc_sprintf (state, state->operandBuffer, formatString,
		  fieldC, fieldB, fieldA);
      write_comments2 (fieldA);
      break;

    case CLASS_A4_SR:
      /* SR instruction  */
      CHECK_FIELD_B();
      CHECK_FIELD_C();

      write_instr_name ();
      WRITE_FORMAT_x_COMMA_LB(C);
      /* Try to print B as an aux reg if it is not a core reg.  */
      usesAuxReg = 1;
      WRITE_FORMAT_x (B);
      WRITE_FORMAT_RB ();
      arc_sprintf (state, state->operandBuffer, formatString, fieldC, fieldB);
      write_comments ();
      break;

    case CLASS_A4_OP3_SUBOPC3F:
      write_instr_name ();
      state->operandBuffer[0] = '\0';
      break;

    case CLASS_A4_LR:
      /* LR instruction */
      CHECK_FIELD_A ();
      CHECK_FIELD_B ();

      write_instr_name ();
      WRITE_FORMAT_x_COMMA_LB (A);
      /* Try to print B as an aux reg if it is not a core reg. */
      usesAuxReg = 1;
      WRITE_FORMAT_x (B);
      WRITE_FORMAT_RB ();
      arc_sprintf (state, state->operandBuffer, formatString, fieldA, fieldB);
      write_comments ();
      break;

    default:
      mwerror (state, "Bad decoding class in ARC disassembler");
      break;
    }

  state->_cond = cond;
  return state->instructionLen = offset;
}


/* Returns the name the user specified core extension register.  */

static const char *
_coreRegName(void * arg ATTRIBUTE_UNUSED, int regval)
{
  return arcExtMap_coreRegName (regval);
}

/* Returns the name the user specified AUX extension register.  */

static const char *
_auxRegName(void *_this ATTRIBUTE_UNUSED, int regval)
{
  return arcExtMap_auxRegName(regval);
}

/* Returns the name the user specified condition code name.  */

static const char *
_condCodeName(void *_this ATTRIBUTE_UNUSED, int regval)
{
  return arcExtMap_condCodeName(regval);
}

/* Returns the name the user specified extension instruction.  */

static const char *
_instName (void *_this ATTRIBUTE_UNUSED, int majop, int minop, int *flags)
{
  return arcExtMap_instName(majop, minop, flags);
}

/* Decode an instruction returning the size of the instruction
   in bytes or zero if unrecognized.  */

static int
decodeInstr (bfd_vma            address, /* Address of this instruction.  */
	     disassemble_info * info)
{
  int status;
  bfd_byte buffer[4];
  struct arcDisState s;		/* ARC Disassembler state.  */
  void *stream = info->stream; 	/* Output stream.  */
  fprintf_ftype func = info->fprintf_func;
  int bytes;

  memset (&s, 0, sizeof(struct arcDisState));

  /* read first instruction  */
  status = (*info->read_memory_func) (address, buffer, 4, info);
  if (status != 0)
    {
      (*info->memory_error_func) (status, address, info);
      return 0;
    }
  if (info->endian == BFD_ENDIAN_LITTLE)
    s.words[0] = bfd_getl32(buffer);
  else
    s.words[0] = bfd_getb32(buffer);
  /* Always read second word in case of limm.  */

  /* We ignore the result since last insn may not have a limm.  */
  status = (*info->read_memory_func) (address + 4, buffer, 4, info);
  if (info->endian == BFD_ENDIAN_LITTLE)
    s.words[1] = bfd_getl32(buffer);
  else
    s.words[1] = bfd_getb32(buffer);

  s._this = &s;
  s.coreRegName = _coreRegName;
  s.auxRegName = _auxRegName;
  s.condCodeName = _condCodeName;
  s.instName = _instName;

  /* Disassemble.  */
  bytes = dsmOneArcInst (address, (void *)& s);

  /* Display the disassembly instruction.  */
  (*func) (stream, "%08lx ", s.words[0]);
  (*func) (stream, "    ");
  (*func) (stream, "%-10s ", s.instrBuffer);

  if (__TRANSLATION_REQUIRED (s))
    {
      bfd_vma addr = s.addresses[s.operandBuffer[1] - '0'];

      (*info->print_address_func) ((bfd_vma) addr, info);
      (*func) (stream, "\n");
    }
  else
    (*func) (stream, "%s",s.operandBuffer);

  return s.instructionLen;
}

/* Return the print_insn function to use.
   Side effect: load (possibly empty) extension section  */

disassembler_ftype
arc_get_disassembler (void *ptr)
{
  if (ptr)
    build_ARC_extmap (ptr);
  return decodeInstr;
}
