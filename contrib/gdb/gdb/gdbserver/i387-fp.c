/* i387-specific utility functions, for the remote server for GDB.
   Copyright 2000, 2001, 2002
   Free Software Foundation, Inc.

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

#include "server.h"
#include "i387-fp.h"

int num_xmm_registers = 8;

/* Note: These functions preserve the reserved bits in control registers.
   However, gdbserver promptly throws away that information.  */

/* These structs should have the proper sizes and alignment on both
   i386 and x86-64 machines.  */

struct i387_fsave {
  /* All these are only sixteen bits, plus padding, except for fop (which
     is only eleven bits), and fooff / fioff (which are 32 bits each).  */
  unsigned int fctrl;
  unsigned int fstat;
  unsigned int ftag;
  unsigned int fioff;
  unsigned short fiseg;
  unsigned short fop;
  unsigned int fooff;
  unsigned int foseg;

  /* Space for eight 80-bit FP values.  */
  char st_space[80];
};

struct i387_fxsave {
  /* All these are only sixteen bits, plus padding, except for fop (which
     is only eleven bits), and fooff / fioff (which are 32 bits each).  */
  unsigned short fctrl;
  unsigned short fstat;
  unsigned short ftag;
  unsigned short fop;
  unsigned int fioff;
  unsigned int fiseg;
  unsigned int fooff;
  unsigned int foseg;

  unsigned int mxcsr;

  unsigned int _pad1;

  /* Space for eight 80-bit FP values in 128-bit spaces.  */
  char st_space[128];

  /* Space for eight 128-bit XMM values, or 16 on x86-64.  */
  char xmm_space[256];
};

void
i387_cache_to_fsave (void *buf)
{
  struct i387_fsave *fp = (struct i387_fsave *) buf;
  int i;
  int st0_regnum = find_regno ("st0");
  unsigned long val, val2;

  for (i = 0; i < 8; i++)
    collect_register (i + st0_regnum, ((char *) &fp->st_space[0]) + i * 10);

  collect_register_by_name ("fioff", &fp->fioff);
  collect_register_by_name ("fooff", &fp->fooff);
  
  /* This one's 11 bits... */
  collect_register_by_name ("fop", &val2);
  fp->fop = (val2 & 0x7FF) | (fp->fop & 0xF800);

  /* Some registers are 16-bit.  */
  collect_register_by_name ("fctrl", &val);
  *(unsigned short *) &fp->fctrl = val;

  collect_register_by_name ("fstat", &val);
  val &= 0xFFFF;
  *(unsigned short *) &fp->fstat = val;

  collect_register_by_name ("ftag", &val);
  val &= 0xFFFF;
  *(unsigned short *) &fp->ftag = val;

  collect_register_by_name ("fiseg", &val);
  val &= 0xFFFF;
  *(unsigned short *) &fp->fiseg = val;

  collect_register_by_name ("foseg", &val);
  val &= 0xFFFF;
  *(unsigned short *) &fp->foseg = val;
}

void
i387_fsave_to_cache (const void *buf)
{
  struct i387_fsave *fp = (struct i387_fsave *) buf;
  int i;
  int st0_regnum = find_regno ("st0");
  unsigned long val;

  for (i = 0; i < 8; i++)
    supply_register (i + st0_regnum, ((char *) &fp->st_space[0]) + i * 10);

  supply_register_by_name ("fioff", &fp->fioff);
  supply_register_by_name ("fooff", &fp->fooff);
  
  /* Some registers are 16-bit.  */
  val = fp->fctrl & 0xFFFF;
  supply_register_by_name ("fctrl", &val);

  val = fp->fstat & 0xFFFF;
  supply_register_by_name ("fstat", &val);

  val = fp->ftag & 0xFFFF;
  supply_register_by_name ("ftag", &val);

  val = fp->fiseg & 0xFFFF;
  supply_register_by_name ("fiseg", &val);

  val = fp->foseg & 0xFFFF;
  supply_register_by_name ("foseg", &val);

  val = (fp->fop) & 0x7FF;
  supply_register_by_name ("fop", &val);
}

void
i387_cache_to_fxsave (void *buf)
{
  struct i387_fxsave *fp = (struct i387_fxsave *) buf;
  int i;
  int st0_regnum = find_regno ("st0");
  int xmm0_regnum = find_regno ("xmm0");
  unsigned long val, val2;

  for (i = 0; i < 8; i++)
    collect_register (i + st0_regnum, ((char *) &fp->st_space[0]) + i * 16);
  for (i = 0; i < num_xmm_registers; i++)
    collect_register (i + xmm0_regnum, ((char *) &fp->xmm_space[0]) + i * 16);

  collect_register_by_name ("fioff", &fp->fioff);
  collect_register_by_name ("fooff", &fp->fooff);
  collect_register_by_name ("mxcsr", &fp->mxcsr);
  
  /* This one's 11 bits... */
  collect_register_by_name ("fop", &val2);
  fp->fop = (val2 & 0x7FF) | (fp->fop & 0xF800);

  /* Some registers are 16-bit.  */
  collect_register_by_name ("fctrl", &val);
  *(unsigned short *) &fp->fctrl = val;

  collect_register_by_name ("fstat", &val);
  val &= 0xFFFF;
  *(unsigned short *) &fp->fstat = val;

  /* Convert to the simplifed tag form stored in fxsave data.  */
  collect_register_by_name ("ftag", &val);
  val &= 0xFFFF;
  for (i = 7; i >= 0; i--)
    {
      int tag = (val >> (i * 2)) & 3;

      if (tag != 3)
	val2 |= (1 << i);
    }
  *(unsigned short *) &fp->ftag = val2;

  collect_register_by_name ("fiseg", &val);
  val &= 0xFFFF;
  *(unsigned short *) &fp->fiseg = val;

  collect_register_by_name ("foseg", &val);
  val &= 0xFFFF;
  *(unsigned short *) &fp->foseg = val;
}

static int
i387_ftag (struct i387_fxsave *fp, int regno)
{
  unsigned char *raw = &fp->st_space[regno * 16];
  unsigned int exponent;
  unsigned long fraction[2];
  int integer;

  integer = raw[7] & 0x80;
  exponent = (((raw[9] & 0x7f) << 8) | raw[8]);
  fraction[0] = ((raw[3] << 24) | (raw[2] << 16) | (raw[1] << 8) | raw[0]);
  fraction[1] = (((raw[7] & 0x7f) << 24) | (raw[6] << 16)
                 | (raw[5] << 8) | raw[4]);

  if (exponent == 0x7fff)
    {
      /* Special.  */
      return (2);
    }
  else if (exponent == 0x0000)
    {
      if (fraction[0] == 0x0000 && fraction[1] == 0x0000 && !integer)
        {
          /* Zero.  */
          return (1);
        }
      else
        {
          /* Special.  */
          return (2);
        }
    }
  else
    {
      if (integer)
        {
          /* Valid.  */
          return (0);
        }
      else
        {
          /* Special.  */
          return (2);
        }
    }
}

void
i387_fxsave_to_cache (const void *buf)
{
  struct i387_fxsave *fp = (struct i387_fxsave *) buf;
  int i, top;
  int st0_regnum = find_regno ("st0");
  int xmm0_regnum = find_regno ("xmm0");
  unsigned long val;

  for (i = 0; i < 8; i++)
    supply_register (i + st0_regnum, ((char *) &fp->st_space[0]) + i * 16);
  for (i = 0; i < num_xmm_registers; i++)
    supply_register (i + xmm0_regnum, ((char *) &fp->xmm_space[0]) + i * 16);

  supply_register_by_name ("fioff", &fp->fioff);
  supply_register_by_name ("fooff", &fp->fooff);
  supply_register_by_name ("mxcsr", &fp->mxcsr);
  
  /* Some registers are 16-bit.  */
  val = fp->fctrl & 0xFFFF;
  supply_register_by_name ("fctrl", &val);

  val = fp->fstat & 0xFFFF;
  supply_register_by_name ("fstat", &val);

  /* Generate the form of ftag data that GDB expects.  */
  top = (fp->fstat >> 11) & 0x7;
  val = 0;
  for (i = 7; i >= 0; i--)
    {
      int tag;
      if (val & (1 << i))
	tag = i387_ftag (fp, (i + 8 - top) % 8);
      else
	tag = 3;
      val |= tag << (2 * i);
    }
  supply_register_by_name ("ftag", &val);

  val = fp->fiseg & 0xFFFF;
  supply_register_by_name ("fiseg", &val);

  val = fp->foseg & 0xFFFF;
  supply_register_by_name ("foseg", &val);

  val = (fp->fop) & 0x7FF;
  supply_register_by_name ("fop", &val);
}
