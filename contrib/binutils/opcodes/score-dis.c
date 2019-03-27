/* Instruction printing code for Score
   Copyright 2006 Free Software Foundation, Inc.
   Contributed by:
   Mei Ligang (ligang@sunnorth.com.cn)
   Pei-Lin Tsai (pltsai@sunplus.com)

   This file is part of libopcodes.

   This program is free software; you can redistribute it and/or modify it under
   the terms of the GNU General Public License as published by the Free
   Software Foundation; either version 2 of the License, or (at your option)
   any later version.

   This program is distributed in the hope that it will be useful, but WITHOUT
   ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
   FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
   more details.

   You should have received a copy of the GNU General Public License
   along with this program; if not, write to the Free Software
   Foundation, Inc., 51 Franklin Street - Fifth Floor, Boston, MA
   02110-1301, USA.  */

#include "sysdep.h"
#include "dis-asm.h"
#define DEFINE_TABLE
#include "score-opc.h"
#include "opintl.h"
#include "bfd.h"

/* FIXME: This shouldn't be done here.  */
#include "elf-bfd.h"
#include "elf/internal.h"
#include "elf/score.h"

#ifndef streq
#define streq(a,b)	(strcmp ((a), (b)) == 0)
#endif

#ifndef strneq
#define strneq(a,b,n)	(strncmp ((a), (b), (n)) == 0)
#endif

#ifndef NUM_ELEM
#define NUM_ELEM(a)     (sizeof (a) / sizeof (a)[0])
#endif

typedef struct
{
  const char *name;
  const char *description;
  const char *reg_names[32];
} score_regname;

static score_regname regnames[] =
{
  {"gcc", "Select register names used by GCC",
  {"r0", "r1", "r2", "r3", "r4", "r5", "r6", "r7", "r8", "r9", "r10",
   "r11", "r12", "r13", "r14", "r15", "r16", "r17", "r18", "r19", "r20",
   "r21", "r22", "r23", "r24", "r25", "r26", "r27", "gp", "r29", "r30", "r31"}},
};

static unsigned int regname_selected = 0;

#define NUM_SCORE_REGNAMES  NUM_ELEM (regnames)
#define score_regnames      regnames[regname_selected].reg_names

/* Print one instruction from PC on INFO->STREAM.
   Return the size of the instruction.  */
static int
print_insn_score32 (bfd_vma pc, struct disassemble_info *info, long given)
{
  struct score_opcode *insn;
  void *stream = info->stream;
  fprintf_ftype func = info->fprintf_func;

  for (insn = score_opcodes; insn->assembler; insn++)
    {
      if ((insn->mask & 0xffff0000) && (given & insn->mask) == insn->value)
        {
          char *c;

          for (c = insn->assembler; *c; c++)
            {
              if (*c == '%')
                {
                  switch (*++c)
                    {
                    case 'j':
                      {
                        int target;

                        if (info->flags & INSN_HAS_RELOC)
                          pc = 0;
                        target = (pc & 0xfe000000) | (given & 0x01fffffe);
                        (*info->print_address_func) (target, info);
                      }
                      break;
                    case 'b':
                      {
                        /* Sign-extend a 20-bit number.  */
#define SEXT20(x)       ((((x) & 0xfffff) ^ (~ 0x7ffff)) + 0x80000)
                        int disp = ((given & 0x01ff8000) >> 5) | (given & 0x3fe);
                        int target = (pc + SEXT20 (disp));

                        (*info->print_address_func) (target, info);
                      }
                      break;
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
                        int bitstart = *c++ - '0';
                        int bitend = 0;

                        while (*c >= '0' && *c <= '9')
                          bitstart = (bitstart * 10) + *c++ - '0';

                        switch (*c)
                          {
                          case '-':
                            c++;
                            while (*c >= '0' && *c <= '9')
                              bitend = (bitend * 10) + *c++ - '0';

                            if (!bitend)
                              abort ();

                            switch (*c)
                              {
                              case 'r':
                                {
                                  long reg;

                                  reg = given >> bitstart;
                                  reg &= (2 << (bitend - bitstart)) - 1;

                                  func (stream, "%s", score_regnames[reg]);
                                }
                                break;
                              case 'd':
                                {
                                  long reg;

                                  reg = given >> bitstart;
                                  reg &= (2 << (bitend - bitstart)) - 1;

                                  func (stream, "%ld", reg);
                                }
                                break;
                              case 'i':
                                {
                                  long reg;

                                  reg = given >> bitstart;
                                  reg &= (2 << (bitend - bitstart)) - 1;
                                  reg = ((reg ^ (1 << (bitend - bitstart))) -
                                        (1 << (bitend - bitstart)));

                                  if (((given & insn->mask) == 0x0c00000a)      /* ldc1  */
                                      || ((given & insn->mask) == 0x0c000012)   /* ldc2  */
                                      || ((given & insn->mask) == 0x0c00001c)   /* ldc3  */
                                      || ((given & insn->mask) == 0x0c00000b)   /* stc1  */
                                      || ((given & insn->mask) == 0x0c000013)   /* stc2  */
                                      || ((given & insn->mask) == 0x0c00001b))  /* stc3  */
                                    reg <<= 2;

                                  func (stream, "%ld", reg);
                                }
                                break;
                              case 'x':
                                {
                                  long reg;

                                  reg = given >> bitstart;
                                  reg &= (2 << (bitend - bitstart)) - 1;

                                  func (stream, "%lx", reg);
                                }
                                break;
                              default:
                                abort ();
                              }
                            break;
                          case '`':
                            c++;
                            if ((given & (1 << bitstart)) == 0)
                              func (stream, "%c", *c);
                            break;
                          case '\'':
                            c++;
                            if ((given & (1 << bitstart)) != 0)
                              func (stream, "%c", *c);
                            break;
                          default:
                            abort ();
                          }
                        break;

                    default:
                        abort ();
                      }
                    }
                }
              else
                func (stream, "%c", *c);
            }
          return 4;
        }
    }

#if (SCORE_SIMULATOR_ACTIVE)
  func (stream, _("<illegal instruction>"));
  return 4;
#endif

  abort ();
}

static void
print_insn_parallel_sym (struct disassemble_info *info)
{
  void *stream = info->stream;
  fprintf_ftype func = info->fprintf_func;

  /* 10:       0000            nop!
     4 space + 1 colon + 1 space + 1 tab + 8 opcode + 2 space + 1 tab.
     FIXME: the space number is not accurate.  */
  func (stream, "%s", " ||\n      \t          \t");
}

/* Print one instruction from PC on INFO->STREAM.
   Return the size of the instruction.  */
static int
print_insn_score16 (bfd_vma pc, struct disassemble_info *info, long given)
{
  struct score_opcode *insn;
  void *stream = info->stream;
  fprintf_ftype func = info->fprintf_func;

  given &= 0xffff;
  for (insn = score_opcodes; insn->assembler; insn++)
    {
      if (!(insn->mask & 0xffff0000) && (given & insn->mask) == insn->value)
        {
          char *c = insn->assembler;

          info->bytes_per_chunk = 2;
          info->bytes_per_line = 4;
          given &= 0xffff;

          for (; *c; c++)
            {
              if (*c == '%')
                {
                  switch (*++c)
                    {

                    case 'j':
                      {
                        int target;

                        if (info->flags & INSN_HAS_RELOC)
                          pc = 0;

                        target = (pc & 0xfffff000) | (given & 0x00000ffe);
                        (*info->print_address_func) (target, info);
                      }
                      break;
                    case 'b':
                      {
                        /* Sign-extend a 9-bit number.  */
#define SEXT9(x)           ((((x) & 0x1ff) ^ (~ 0xff)) + 0x100)
                        int disp = (given & 0xff) << 1;
                        int target = (pc + SEXT9 (disp));

                        (*info->print_address_func) (target, info);
                      }
                      break;

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
                        int bitstart = *c++ - '0';
                        int bitend = 0;

                        while (*c >= '0' && *c <= '9')
                          bitstart = (bitstart * 10) + *c++ - '0';

                        switch (*c)
                          {
                          case '-':
                            {
                              long reg;

                              c++;
                              while (*c >= '0' && *c <= '9')
                                bitend = (bitend * 10) + *c++ - '0';
                              if (!bitend)
                                abort ();
                              reg = given >> bitstart;
                              reg &= (2 << (bitend - bitstart)) - 1;
                              switch (*c)
                                {
                                case 'R':
                                  func (stream, "%s", score_regnames[reg + 16]);
                                  break;
                                case 'r':
                                  func (stream, "%s", score_regnames[reg]);
                                  break;
                                case 'd':
                                  if (*(c + 1) == '\0')
                                    func (stream, "%ld", reg);
                                  else
                                    {
                                      c++;
                                      if (*c == '1')
                                        func (stream, "%ld", reg << 1);
                                      else if (*c == '2')
                                        func (stream, "%ld", reg << 2);
                                    }
                                  break;

                                case 'x':
                                  if (*(c + 1) == '\0')
                                    func (stream, "%lx", reg);
                                  else
                                    {
                                      c++;
                                      if (*c == '1')
                                        func (stream, "%lx", reg << 1);
                                      else if (*c == '2')
                                        func (stream, "%lx", reg << 2);
                                    }
                                  break;
                                case 'i':
				  reg = ((reg ^ (1 << bitend)) - (1 << bitend));
				  func (stream, "%ld", reg);
                                  break;
                                default:
                                  abort ();
                                }
                            }
                            break;

                          case '\'':
                            c++;
                            if ((given & (1 << bitstart)) != 0)
                              func (stream, "%c", *c);
                            break;
                          default:
                            abort ();
                          }
                      }
                      break;
                    default:
                      abort ();
                    }
                }
              else
                func (stream, "%c", *c);
            }

          return 2;
        }
    }
#if (SCORE_SIMULATOR_ACTIVE)
  func (stream, _("<illegal instruction>"));
  return 2;
#endif
  /* No match.  */
  abort ();
}

/* NOTE: There are no checks in these routines that
   the relevant number of data bytes exist.  */
static int
print_insn (bfd_vma pc, struct disassemble_info *info, bfd_boolean little)
{
  unsigned char b[4];
  long given;
  long ridparity;
  int status;
  bfd_boolean insn_pce_p = FALSE;
  bfd_boolean insn_16_p = FALSE;

  info->display_endian = little ? BFD_ENDIAN_LITTLE : BFD_ENDIAN_BIG;

  if (pc & 0x2)
    {
      info->bytes_per_chunk = 2;
      status = info->read_memory_func (pc, (bfd_byte *) b, 2, info);
      b[3] = b[2] = 0;
      insn_16_p = TRUE;
    }
  else
    {
      info->bytes_per_chunk = 4;
      status = info->read_memory_func (pc, (bfd_byte *) & b[0], 4, info);
      if (status != 0)
	{
          info->bytes_per_chunk = 2;
          status = info->read_memory_func (pc, (bfd_byte *) b, 2, info);
          b[3] = b[2] = 0;
          insn_16_p = TRUE;
	}
    }

  if (status != 0)
    {
      info->memory_error_func (status, pc, info);
      return -1;
    }

  if (little)
    {
      given = (b[0]) | (b[1] << 8) | (b[2] << 16) | (b[3] << 24);
    }
  else
    {
      given = (b[0] << 24) | (b[1] << 16) | (b[2] << 8) | (b[3]);
    }

  if ((given & 0x80008000) == 0x80008000)
    {
      insn_pce_p = FALSE;
      insn_16_p = FALSE;
    }
  else if ((given & 0x8000) == 0x8000)
    {
      insn_pce_p = TRUE;
    }
  else
    {
      insn_16_p = TRUE;
    }

  /* 16 bit instruction.  */
  if (insn_16_p)
    {
      if (little)
	{
          given = b[0] | (b[1] << 8);
	}
      else
	{
          given = (b[0] << 8) | b[1];
	}

      status = print_insn_score16 (pc, info, given);
    }
  /* pce instruction.  */
  else if (insn_pce_p)
    {
      long other;

      other = given & 0xFFFF;
      given = (given & 0xFFFF0000) >> 16;

      status = print_insn_score16 (pc, info, given);
      print_insn_parallel_sym (info);
      status += print_insn_score16 (pc, info, other);
      /* disassemble_bytes() will output 4 byte per chunk for pce instructio.  */
      info->bytes_per_chunk = 4;
    }
  /* 32 bit instruction.  */
  else
    {
      /* Get rid of parity.  */
      ridparity = (given & 0x7FFF);
      ridparity |= (given & 0x7FFF0000) >> 1;
      given = ridparity;
      status = print_insn_score32 (pc, info, given);
    }

  return status;
}

int
print_insn_big_score (bfd_vma pc, struct disassemble_info *info)
{
  return print_insn (pc, info, FALSE);
}

int
print_insn_little_score (bfd_vma pc, struct disassemble_info *info)
{
  return print_insn (pc, info, TRUE);
}
