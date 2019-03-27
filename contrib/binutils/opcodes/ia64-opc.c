/* ia64-opc.c -- Functions to access the compacted opcode table
   Copyright 1999, 2000, 2001, 2003, 2005 Free Software Foundation, Inc.
   Written by Bob Manson of Cygnus Solutions, <manson@cygnus.com>

   This file is part of GDB, GAS, and the GNU binutils.

   GDB, GAS, and the GNU binutils are free software; you can redistribute
   them and/or modify them under the terms of the GNU General Public
   License as published by the Free Software Foundation; either version
   2, or (at your option) any later version.

   GDB, GAS, and the GNU binutils are distributed in the hope that they
   will be useful, but WITHOUT ANY WARRANTY; without even the implied
   warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See
   the GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this file; see the file COPYING.  If not, write to the
   Free Software Foundation, 51 Franklin Street - Fifth Floor, Boston, MA
   02110-1301, USA.  */

#include "ansidecl.h"
#include "sysdep.h"
#include "libiberty.h"
#include "ia64-asmtab.h"
#include "ia64-asmtab.c"

static void get_opc_prefix (const char **, char *);
static short int find_string_ent (const char *);
static short int find_main_ent (short int);
static short int find_completer (short int, short int, const char *);
static ia64_insn apply_completer (ia64_insn, int);
static int extract_op_bits (int, int, int);
static int extract_op (int, int *, unsigned int *);
static int opcode_verify (ia64_insn, int, enum ia64_insn_type);
static int locate_opcode_ent (ia64_insn, enum ia64_insn_type);
static struct ia64_opcode *make_ia64_opcode
  (ia64_insn, const char *, int, int);
static struct ia64_opcode *ia64_find_matching_opcode
  (const char *, short int);

const struct ia64_templ_desc ia64_templ_desc[16] =
  {
    { 0, { IA64_UNIT_M, IA64_UNIT_I, IA64_UNIT_I }, "MII" },	/* 0 */
    { 2, { IA64_UNIT_M, IA64_UNIT_I, IA64_UNIT_I }, "MII" },
    { 0, { IA64_UNIT_M, IA64_UNIT_L, IA64_UNIT_X }, "MLX" },
    { 0, { 0, },				    "-3-" },
    { 0, { IA64_UNIT_M, IA64_UNIT_M, IA64_UNIT_I }, "MMI" },	/* 4 */
    { 1, { IA64_UNIT_M, IA64_UNIT_M, IA64_UNIT_I }, "MMI" },
    { 0, { IA64_UNIT_M, IA64_UNIT_F, IA64_UNIT_I }, "MFI" },
    { 0, { IA64_UNIT_M, IA64_UNIT_M, IA64_UNIT_F }, "MMF" },
    { 0, { IA64_UNIT_M, IA64_UNIT_I, IA64_UNIT_B }, "MIB" },	/* 8 */
    { 0, { IA64_UNIT_M, IA64_UNIT_B, IA64_UNIT_B }, "MBB" },
    { 0, { 0, },				    "-a-" },
    { 0, { IA64_UNIT_B, IA64_UNIT_B, IA64_UNIT_B }, "BBB" },
    { 0, { IA64_UNIT_M, IA64_UNIT_M, IA64_UNIT_B }, "MMB" },	/* c */
    { 0, { 0, },				    "-d-" },
    { 0, { IA64_UNIT_M, IA64_UNIT_F, IA64_UNIT_B }, "MFB" },
    { 0, { 0, },				    "-f-" },
  };


/* Copy the prefix contained in *PTR (up to a '.' or a NUL) to DEST.
   PTR will be adjusted to point to the start of the next portion
   of the opcode, or at the NUL character. */

static void
get_opc_prefix (const char **ptr, char *dest)
{
  char *c = strchr (*ptr, '.');
  if (c != NULL)
    {
      memcpy (dest, *ptr, c - *ptr);
      dest[c - *ptr] = '\0';
      *ptr = c + 1;
    }
  else
    {
      int l = strlen (*ptr);
      memcpy (dest, *ptr, l);
      dest[l] = '\0';
      *ptr += l;
    }
}

/* Find the index of the entry in the string table corresponding to
   STR; return -1 if one does not exist. */

static short
find_string_ent (const char *str)
{
  short start = 0;
  short end = sizeof (ia64_strings) / sizeof (const char *);
  short i = (start + end) / 2;

  if (strcmp (str, ia64_strings[end - 1]) > 0)
    {
      return -1;
    }
  while (start <= end)
    {
      int c = strcmp (str, ia64_strings[i]);
      if (c < 0)
	{
	  end = i - 1;
	}
      else if (c == 0)
	{
	  return i;
	}
      else
	{
	  start = i + 1;
	}
      i = (start + end) / 2;
    }
  return -1;
}

/* Find the opcode in the main opcode table whose name is STRINGINDEX, or
   return -1 if one does not exist. */

static short
find_main_ent (short nameindex)
{
  short start = 0;
  short end = sizeof (main_table) / sizeof (struct ia64_main_table);
  short i = (start + end) / 2;

  if (nameindex < main_table[0].name_index
      || nameindex > main_table[end - 1].name_index)
    {
      return -1;
    }
  while (start <= end)
    {
      if (nameindex < main_table[i].name_index)
	{
	  end = i - 1;
	}
      else if (nameindex == main_table[i].name_index)
	{
	  while (i > 0 && main_table[i - 1].name_index == nameindex)
	    {
	      i--;
	    }
	  return i;
	}
      else
	{
	  start = i + 1;
	}
      i = (start + end) / 2;
    }
  return -1;
}

/* Find the index of the entry in the completer table that is part of
   MAIN_ENT (starting from PREV_COMPLETER) that matches NAME, or
   return -1 if one does not exist. */

static short
find_completer (short main_ent, short prev_completer, const char *name)
{
  short name_index = find_string_ent (name);

  if (name_index < 0)
    {
      return -1;
    }

  if (prev_completer == -1)
    {
      prev_completer = main_table[main_ent].completers;
    }
  else
    {
      prev_completer = completer_table[prev_completer].subentries;
    }

  while (prev_completer != -1)
    {
      if (completer_table[prev_completer].name_index == name_index)
	{
	  return prev_completer;
	}
      prev_completer = completer_table[prev_completer].alternative;
    }
  return -1;
}

/* Apply the completer referred to by COMPLETER_INDEX to OPCODE, and
   return the result. */

static ia64_insn
apply_completer (ia64_insn opcode, int completer_index)
{
  ia64_insn mask = completer_table[completer_index].mask;
  ia64_insn bits = completer_table[completer_index].bits;
  int shiftamt = (completer_table[completer_index].offset & 63);

  mask = mask << shiftamt;
  bits = bits << shiftamt;
  opcode = (opcode & ~mask) | bits;
  return opcode;
}

/* Extract BITS number of bits starting from OP_POINTER + BITOFFSET in
   the dis_table array, and return its value.  (BITOFFSET is numbered
   starting from MSB to LSB, so a BITOFFSET of 0 indicates the MSB of the
   first byte in OP_POINTER.) */

static int
extract_op_bits (int op_pointer, int bitoffset, int bits)
{
  int res = 0;

  op_pointer += (bitoffset / 8);

  if (bitoffset % 8)
    {
      unsigned int op = dis_table[op_pointer++];
      int numb = 8 - (bitoffset % 8);
      int mask = (1 << numb) - 1;
      int bata = (bits < numb) ? bits : numb;
      int delta = numb - bata;

      res = (res << bata) | ((op & mask) >> delta);
      bitoffset += bata;
      bits -= bata;
    }
  while (bits >= 8)
    {
      res = (res << 8) | (dis_table[op_pointer++] & 255);
      bits -= 8;
    }
  if (bits > 0)
    {
      unsigned int op = (dis_table[op_pointer++] & 255);
      res = (res << bits) | (op >> (8 - bits));
    }
  return res;
}

/* Examine the state machine entry at OP_POINTER in the dis_table
   array, and extract its values into OPVAL and OP.  The length of the
   state entry in bits is returned. */

static int
extract_op (int op_pointer, int *opval, unsigned int *op)
{
  int oplen = 5;

  *op = dis_table[op_pointer];

  if ((*op) & 0x40)
    {
      opval[0] = extract_op_bits (op_pointer, oplen, 5);
      oplen += 5;
    }
  switch ((*op) & 0x30)
    {
    case 0x10:
      {
	opval[1] = extract_op_bits (op_pointer, oplen, 8);
	oplen += 8;
	opval[1] += op_pointer;
	break;
      }
    case 0x20:
      {
	opval[1] = extract_op_bits (op_pointer, oplen, 16);
	if (! (opval[1] & 32768))
	  {
	    opval[1] += op_pointer;
	  }
	oplen += 16;
	break;
      }
    case 0x30:
      {
	oplen--;
	opval[2] = extract_op_bits (op_pointer, oplen, 12);
	oplen += 12;
	opval[2] |= 32768;
	break;
      }
    }
  if (((*op) & 0x08) && (((*op) & 0x30) != 0x30))
    {
      opval[2] = extract_op_bits (op_pointer, oplen, 16);
      oplen += 16;
      if (! (opval[2] & 32768))
	{
	  opval[2] += op_pointer;
	}
    }
  return oplen;
}

/* Returns a non-zero value if the opcode in the main_table list at
   PLACE matches OPCODE and is of type TYPE. */

static int
opcode_verify (ia64_insn opcode, int place, enum ia64_insn_type type)
{
  if (main_table[place].opcode_type != type)
    {
      return 0;
    }
  if (main_table[place].flags
      & (IA64_OPCODE_F2_EQ_F3 | IA64_OPCODE_LEN_EQ_64MCNT))
    {
      const struct ia64_operand *o1, *o2;
      ia64_insn f2, f3;

      if (main_table[place].flags & IA64_OPCODE_F2_EQ_F3)
	{
	  o1 = elf64_ia64_operands + IA64_OPND_F2;
	  o2 = elf64_ia64_operands + IA64_OPND_F3;
	  (*o1->extract) (o1, opcode, &f2);
	  (*o2->extract) (o2, opcode, &f3);
	  if (f2 != f3)
	    return 0;
	}
      else
	{
	  ia64_insn len, count;

	  /* length must equal 64-count: */
	  o1 = elf64_ia64_operands + IA64_OPND_LEN6;
	  o2 = elf64_ia64_operands + main_table[place].operands[2];
	  (*o1->extract) (o1, opcode, &len);
	  (*o2->extract) (o2, opcode, &count);
	  if (len != 64 - count)
	    return 0;
	}
    }
  return 1;
}

/* Find an instruction entry in the ia64_dis_names array that matches
   opcode OPCODE and is of type TYPE.  Returns either a positive index
   into the array, or a negative value if an entry for OPCODE could
   not be found.  Checks all matches and returns the one with the highest
   priority. */

static int
locate_opcode_ent (ia64_insn opcode, enum ia64_insn_type type)
{
  int currtest[41];
  int bitpos[41];
  int op_ptr[41];
  int currstatenum = 0;
  short found_disent = -1;
  short found_priority = -1;

  currtest[currstatenum] = 0;
  op_ptr[currstatenum] = 0;
  bitpos[currstatenum] = 40;

  while (1)
    {
      int op_pointer = op_ptr[currstatenum];
      unsigned int op;
      int currbitnum = bitpos[currstatenum];
      int oplen;
      int opval[3] = {0};
      int next_op;
      int currbit;

      oplen = extract_op (op_pointer, opval, &op);

      bitpos[currstatenum] = currbitnum;

      /* Skip opval[0] bits in the instruction. */
      if (op & 0x40)
	{
	  currbitnum -= opval[0];
	}

      /* The value of the current bit being tested. */
      currbit = opcode & (((ia64_insn) 1) << currbitnum) ? 1 : 0;
      next_op = -1;

      /* We always perform the tests specified in the current state in
	 a particular order, falling through to the next test if the
	 previous one failed. */
      switch (currtest[currstatenum])
	{
	case 0:
	  currtest[currstatenum]++;
	  if (currbit == 0 && (op & 0x80))
	    {
	      /* Check for a zero bit.  If this test solely checks for
		 a zero bit, we can check for up to 8 consecutive zero
		 bits (the number to check is specified by the lower 3
		 bits in the state code.)

		 If the state instruction matches, we go to the very
		 next state instruction; otherwise, try the next test. */

	      if ((op & 0xf8) == 0x80)
		{
		  int count = op & 0x7;
		  int x;

		  for (x = 0; x <= count; x++)
		    {
		      int i =
			opcode & (((ia64_insn) 1) << (currbitnum - x)) ? 1 : 0;
		      if (i)
			{
			  break;
			}
		    }
		  if (x > count)
		    {
		      next_op = op_pointer + ((oplen + 7) / 8);
		      currbitnum -= count;
		      break;
		    }
		}
	      else if (! currbit)
		{
		  next_op = op_pointer + ((oplen + 7) / 8);
		  break;
		}
	    }
	  /* FALLTHROUGH */
	case 1:
	  /* If the bit in the instruction is one, go to the state
	     instruction specified by opval[1]. */
	  currtest[currstatenum]++;
	  if (currbit && (op & 0x30) != 0 && ((op & 0x30) != 0x30))
	    {
	      next_op = opval[1];
	      break;
	    }
	  /* FALLTHROUGH */
	case 2:
	  /* Don't care.  Skip the current bit and go to the state
	     instruction specified by opval[2].

	     An encoding of 0x30 is special; this means that a 12-bit
	     offset into the ia64_dis_names[] array is specified.  */
	  currtest[currstatenum]++;
	  if ((op & 0x08) || ((op & 0x30) == 0x30))
	    {
	      next_op = opval[2];
	      break;
	    }
	}

      /* If bit 15 is set in the address of the next state, an offset
	 in the ia64_dis_names array was specified instead.  We then
	 check to see if an entry in the list of opcodes matches the
	 opcode we were given; if so, we have succeeded.  */

      if ((next_op >= 0) && (next_op & 32768))
	{
	  short disent = next_op & 32767;
          short priority = -1;

	  if (next_op > 65535)
	    {
	      abort ();
	    }

	  /* Run through the list of opcodes to check, trying to find
	     one that matches.  */
	  while (disent >= 0)
	    {
	      int place = ia64_dis_names[disent].insn_index;

              priority = ia64_dis_names[disent].priority;

	      if (opcode_verify (opcode, place, type)
                  && priority > found_priority)
		{
		  break;
		}
	      if (ia64_dis_names[disent].next_flag)
		{
		  disent++;
		}
	      else
		{
		  disent = -1;
		}
	    }

	  if (disent >= 0)
	    {
              found_disent = disent;
              found_priority = priority;
	    }
          /* Try the next test in this state, regardless of whether a match
             was found. */
          next_op = -2;
	}

      /* next_op == -1 is "back up to the previous state".
	 next_op == -2 is "stay in this state and try the next test".
	 Otherwise, transition to the state indicated by next_op. */

      if (next_op == -1)
	{
	  currstatenum--;
	  if (currstatenum < 0)
	    {
              return found_disent;
	    }
	}
      else if (next_op >= 0)
	{
	  currstatenum++;
	  bitpos[currstatenum] = currbitnum - 1;
	  op_ptr[currstatenum] = next_op;
	  currtest[currstatenum] = 0;
	}
    }
}

/* Construct an ia64_opcode entry based on OPCODE, NAME and PLACE. */

static struct ia64_opcode *
make_ia64_opcode (ia64_insn opcode, const char *name, int place, int depind)
{
  struct ia64_opcode *res =
    (struct ia64_opcode *) xmalloc (sizeof (struct ia64_opcode));
  res->name = xstrdup (name);
  res->type = main_table[place].opcode_type;
  res->num_outputs = main_table[place].num_outputs;
  res->opcode = opcode;
  res->mask = main_table[place].mask;
  res->operands[0] = main_table[place].operands[0];
  res->operands[1] = main_table[place].operands[1];
  res->operands[2] = main_table[place].operands[2];
  res->operands[3] = main_table[place].operands[3];
  res->operands[4] = main_table[place].operands[4];
  res->flags = main_table[place].flags;
  res->ent_index = place;
  res->dependencies = &op_dependencies[depind];
  return res;
}

/* Determine the ia64_opcode entry for the opcode specified by INSN
   and TYPE.  If a valid entry is not found, return NULL. */
struct ia64_opcode *
ia64_dis_opcode (ia64_insn insn, enum ia64_insn_type type)
{
  int disent = locate_opcode_ent (insn, type);

  if (disent < 0)
    {
      return NULL;
    }
  else
    {
      unsigned int cb = ia64_dis_names[disent].completer_index;
      static char name[128];
      int place = ia64_dis_names[disent].insn_index;
      int ci = main_table[place].completers;
      ia64_insn tinsn = main_table[place].opcode;

      strcpy (name, ia64_strings [main_table[place].name_index]);

      while (cb)
	{
	  if (cb & 1)
	    {
	      int cname = completer_table[ci].name_index;

	      tinsn = apply_completer (tinsn, ci);

	      if (ia64_strings[cname][0] != '\0')
		{
		  strcat (name, ".");
		  strcat (name, ia64_strings[cname]);
		}
	      if (cb != 1)
		{
		  ci = completer_table[ci].subentries;
		}
	    }
	  else
	    {
	      ci = completer_table[ci].alternative;
	    }
	  if (ci < 0)
	    {
	      abort ();
	    }
	  cb = cb >> 1;
	}
      if (tinsn != (insn & main_table[place].mask))
	{
	  abort ();
	}
      return make_ia64_opcode (insn, name, place,
                               completer_table[ci].dependencies);
    }
}

/* Search the main_opcode table starting from PLACE for an opcode that
   matches NAME.  Return NULL if one is not found. */

static struct ia64_opcode *
ia64_find_matching_opcode (const char *name, short place)
{
  char op[129];
  const char *suffix;
  short name_index;

  if (strlen (name) > 128)
    {
      return NULL;
    }
  suffix = name;
  get_opc_prefix (&suffix, op);
  name_index = find_string_ent (op);
  if (name_index < 0)
    {
      return NULL;
    }

  while (main_table[place].name_index == name_index)
    {
      const char *curr_suffix = suffix;
      ia64_insn curr_insn = main_table[place].opcode;
      short completer = -1;

      do {
	if (suffix[0] == '\0')
	  {
	    completer = find_completer (place, completer, suffix);
	  }
	else
	  {
	    get_opc_prefix (&curr_suffix, op);
	    completer = find_completer (place, completer, op);
	  }
	if (completer != -1)
	  {
	    curr_insn = apply_completer (curr_insn, completer);
	  }
      } while (completer != -1 && curr_suffix[0] != '\0');

      if (completer != -1 && curr_suffix[0] == '\0'
	  && completer_table[completer].terminal_completer)
	{
          int depind = completer_table[completer].dependencies;
	  return make_ia64_opcode (curr_insn, name, place, depind);
	}
      else
	{
	  place++;
	}
    }
  return NULL;
}

/* Find the next opcode after PREV_ENT that matches PREV_ENT, or return NULL
   if one does not exist.

   It is the caller's responsibility to invoke ia64_free_opcode () to
   release any resources used by the returned entry. */

struct ia64_opcode *
ia64_find_next_opcode (struct ia64_opcode *prev_ent)
{
  return ia64_find_matching_opcode (prev_ent->name,
				    prev_ent->ent_index + 1);
}

/* Find the first opcode that matches NAME, or return NULL if it does
   not exist.

   It is the caller's responsibility to invoke ia64_free_opcode () to
   release any resources used by the returned entry. */

struct ia64_opcode *
ia64_find_opcode (const char *name)
{
  char op[129];
  const char *suffix;
  short place;
  short name_index;

  if (strlen (name) > 128)
    {
      return NULL;
    }
  suffix = name;
  get_opc_prefix (&suffix, op);
  name_index = find_string_ent (op);
  if (name_index < 0)
    {
      return NULL;
    }

  place = find_main_ent (name_index);

  if (place < 0)
    {
      return NULL;
    }
  return ia64_find_matching_opcode (name, place);
}

/* Free any resources used by ENT. */
void
ia64_free_opcode (struct ia64_opcode *ent)
{
  free ((void *)ent->name);
  free (ent);
}

const struct ia64_dependency *
ia64_find_dependency (int index)
{
  index = DEP(index);

  if (index < 0
      || index >= (int)(sizeof(dependencies) / sizeof(dependencies[0])))
    return NULL;

  return &dependencies[index];
}
