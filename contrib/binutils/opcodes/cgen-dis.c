/* CGEN generic disassembler support code.

   Copyright 1996, 1997, 1998, 1999, 2000, 2001, 2002, 2003
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

   You should have received a copy of the GNU General Public License along
   with this program; if not, write to the Free Software Foundation, Inc.,
   51 Franklin Street - Fifth Floor, Boston, MA 02110-1301, USA.  */

#include "sysdep.h"
#include <stdio.h>
#include "ansidecl.h"
#include "libiberty.h"
#include "bfd.h"
#include "symcat.h"
#include "opcode/cgen.h"

static CGEN_INSN_LIST *  hash_insn_array        (CGEN_CPU_DESC, const CGEN_INSN *, int, int, CGEN_INSN_LIST **, CGEN_INSN_LIST *);
static CGEN_INSN_LIST *  hash_insn_list         (CGEN_CPU_DESC, const CGEN_INSN_LIST *, CGEN_INSN_LIST **, CGEN_INSN_LIST *);
static void              build_dis_hash_table   (CGEN_CPU_DESC);
static int		 count_decodable_bits   (const CGEN_INSN *);
static void		 add_insn_to_hash_chain (CGEN_INSN_LIST *,
						 const CGEN_INSN *,
						 CGEN_INSN_LIST **,
						 unsigned int);

/* Return the number of decodable bits in this insn.  */
static int
count_decodable_bits (const CGEN_INSN *insn)
{
  unsigned mask = CGEN_INSN_BASE_MASK (insn);
  int bits = 0;
  int m;
  for (m = 1; m != 0; m <<= 1)
    {
      if (mask & m)
	++bits;
    }
  return bits;
}

/* Add an instruction to the hash chain.  */     
static void
add_insn_to_hash_chain (CGEN_INSN_LIST *hentbuf,
			const CGEN_INSN *insn,
			CGEN_INSN_LIST **htable,
			unsigned int hash)
{
  CGEN_INSN_LIST *current_buf;
  CGEN_INSN_LIST *previous_buf;
  int insn_decodable_bits;

  /* Add insns sorted by the number of decodable bits, in decreasing order.
     This ensures that any insn which is a special case of another will be
     checked first.  */
  insn_decodable_bits = count_decodable_bits (insn);
  previous_buf = NULL;
  for (current_buf = htable[hash]; current_buf != NULL;
       current_buf = current_buf->next)
    {
      int current_decodable_bits = count_decodable_bits (current_buf->insn);
      if (insn_decodable_bits >= current_decodable_bits)
	break;
      previous_buf = current_buf;
    }

  /* Now insert the new insn.  */
  hentbuf->insn = insn;
  hentbuf->next = current_buf;
  if (previous_buf == NULL)
    htable[hash] = hentbuf;
  else
    previous_buf->next = hentbuf;
}

/* Subroutine of build_dis_hash_table to add INSNS to the hash table.

   COUNT is the number of elements in INSNS.
   ENTSIZE is sizeof (CGEN_IBASE) for the target.
   ??? No longer used but leave in for now.
   HTABLE points to the hash table.
   HENTBUF is a pointer to sufficiently large buffer of hash entries.
   The result is a pointer to the next entry to use.

   The table is scanned backwards as additions are made to the front of the
   list and we want earlier ones to be prefered.  */

static CGEN_INSN_LIST *
hash_insn_array (CGEN_CPU_DESC cd,
		 const CGEN_INSN * insns,
		 int count,
		 int entsize ATTRIBUTE_UNUSED,
		 CGEN_INSN_LIST ** htable,
		 CGEN_INSN_LIST * hentbuf)
{
  int big_p = CGEN_CPU_ENDIAN (cd) == CGEN_ENDIAN_BIG;
  int i;

  for (i = count - 1; i >= 0; --i, ++hentbuf)
    {
      unsigned int hash;
      char buf [4];
      unsigned long value;
      const CGEN_INSN *insn = &insns[i];

      if (! (* cd->dis_hash_p) (insn))
	continue;

      /* We don't know whether the target uses the buffer or the base insn
	 to hash on, so set both up.  */

      value = CGEN_INSN_BASE_VALUE (insn);
      bfd_put_bits ((bfd_vma) value,
		    buf,
		    CGEN_INSN_MASK_BITSIZE (insn),
		    big_p);
      hash = (* cd->dis_hash) (buf, value);
      add_insn_to_hash_chain (hentbuf, insn, htable, hash);
    }

  return hentbuf;
}

/* Subroutine of build_dis_hash_table to add INSNS to the hash table.
   This function is identical to hash_insn_array except the insns are
   in a list.  */

static CGEN_INSN_LIST *
hash_insn_list (CGEN_CPU_DESC cd,
		const CGEN_INSN_LIST *insns,
		CGEN_INSN_LIST **htable,
		CGEN_INSN_LIST *hentbuf)
{
  int big_p = CGEN_CPU_ENDIAN (cd) == CGEN_ENDIAN_BIG;
  const CGEN_INSN_LIST *ilist;

  for (ilist = insns; ilist != NULL; ilist = ilist->next, ++ hentbuf)
    {
      unsigned int hash;
      char buf[4];
      unsigned long value;

      if (! (* cd->dis_hash_p) (ilist->insn))
	continue;

      /* We don't know whether the target uses the buffer or the base insn
	 to hash on, so set both up.  */

      value = CGEN_INSN_BASE_VALUE (ilist->insn);
      bfd_put_bits((bfd_vma) value,
		   buf,
		   CGEN_INSN_MASK_BITSIZE (ilist->insn),
		   big_p);
      hash = (* cd->dis_hash) (buf, value);
      add_insn_to_hash_chain (hentbuf, ilist->insn, htable, hash);
    }

  return hentbuf;
}

/* Build the disassembler instruction hash table.  */

static void
build_dis_hash_table (CGEN_CPU_DESC cd)
{
  int count = cgen_insn_count (cd) + cgen_macro_insn_count (cd);
  CGEN_INSN_TABLE *insn_table = & cd->insn_table;
  CGEN_INSN_TABLE *macro_insn_table = & cd->macro_insn_table;
  unsigned int hash_size = cd->dis_hash_size;
  CGEN_INSN_LIST *hash_entry_buf;
  CGEN_INSN_LIST **dis_hash_table;
  CGEN_INSN_LIST *dis_hash_table_entries;

  /* The space allocated for the hash table consists of two parts:
     the hash table and the hash lists.  */

  dis_hash_table = (CGEN_INSN_LIST **)
    xmalloc (hash_size * sizeof (CGEN_INSN_LIST *));
  memset (dis_hash_table, 0, hash_size * sizeof (CGEN_INSN_LIST *));
  dis_hash_table_entries = hash_entry_buf = (CGEN_INSN_LIST *)
    xmalloc (count * sizeof (CGEN_INSN_LIST));

  /* Add compiled in insns.
     Don't include the first one as it is a reserved entry.  */
  /* ??? It was the end of all hash chains, and also the special
     "invalid insn" marker.  May be able to do it differently now.  */

  hash_entry_buf = hash_insn_array (cd,
				    insn_table->init_entries + 1,
				    insn_table->num_init_entries - 1,
				    insn_table->entry_size,
				    dis_hash_table, hash_entry_buf);

  /* Add compiled in macro-insns.  */

  hash_entry_buf = hash_insn_array (cd, macro_insn_table->init_entries,
				    macro_insn_table->num_init_entries,
				    macro_insn_table->entry_size,
				    dis_hash_table, hash_entry_buf);

  /* Add runtime added insns.
     Later added insns will be prefered over earlier ones.  */

  hash_entry_buf = hash_insn_list (cd, insn_table->new_entries,
				   dis_hash_table, hash_entry_buf);

  /* Add runtime added macro-insns.  */

  hash_insn_list (cd, macro_insn_table->new_entries,
		  dis_hash_table, hash_entry_buf);

  cd->dis_hash_table = dis_hash_table;
  cd->dis_hash_table_entries = dis_hash_table_entries;
}

/* Return the first entry in the hash list for INSN.  */

CGEN_INSN_LIST *
cgen_dis_lookup_insn (CGEN_CPU_DESC cd, const char * buf, CGEN_INSN_INT value)
{
  unsigned int hash;

  if (cd->dis_hash_table == NULL)
    build_dis_hash_table (cd);

  hash = (* cd->dis_hash) (buf, value);

  return cd->dis_hash_table[hash];
}
