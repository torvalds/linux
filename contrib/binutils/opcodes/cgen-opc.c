/* CGEN generic opcode support.

   Copyright 1996, 1997, 1998, 1999, 2000, 2001, 2003, 2005
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
#include "safe-ctype.h"
#include "bfd.h"
#include "symcat.h"
#include "opcode/cgen.h"

#ifdef HAVE_ALLOCA_H
#include <alloca.h>
#endif

static unsigned int hash_keyword_name
  (const CGEN_KEYWORD *, const char *, int);
static unsigned int hash_keyword_value
  (const CGEN_KEYWORD *, unsigned int);
static void build_keyword_hash_tables
  (CGEN_KEYWORD *);

/* Return number of hash table entries to use for N elements.  */
#define KEYWORD_HASH_SIZE(n) ((n) <= 31 ? 17 : 31)

/* Look up *NAMEP in the keyword table KT.
   The result is the keyword entry or NULL if not found.  */

const CGEN_KEYWORD_ENTRY *
cgen_keyword_lookup_name (CGEN_KEYWORD *kt, const char *name)
{
  const CGEN_KEYWORD_ENTRY *ke;
  const char *p,*n;

  if (kt->name_hash_table == NULL)
    build_keyword_hash_tables (kt);

  ke = kt->name_hash_table[hash_keyword_name (kt, name, 0)];

  /* We do case insensitive comparisons.
     If that ever becomes a problem, add an attribute that denotes
     "do case sensitive comparisons".  */

  while (ke != NULL)
    {
      n = name;
      p = ke->name;

      while (*p
	     && (*p == *n
		 || (ISALPHA (*p) && (TOLOWER (*p) == TOLOWER (*n)))))
	++n, ++p;

      if (!*p && !*n)
	return ke;

      ke = ke->next_name;
    }

  if (kt->null_entry)
    return kt->null_entry;
  return NULL;
}

/* Look up VALUE in the keyword table KT.
   The result is the keyword entry or NULL if not found.  */

const CGEN_KEYWORD_ENTRY *
cgen_keyword_lookup_value (CGEN_KEYWORD *kt, int value)
{
  const CGEN_KEYWORD_ENTRY *ke;

  if (kt->name_hash_table == NULL)
    build_keyword_hash_tables (kt);

  ke = kt->value_hash_table[hash_keyword_value (kt, value)];

  while (ke != NULL)
    {
      if (value == ke->value)
	return ke;
      ke = ke->next_value;
    }

  return NULL;
}

/* Add an entry to a keyword table.  */

void
cgen_keyword_add (CGEN_KEYWORD *kt, CGEN_KEYWORD_ENTRY *ke)
{
  unsigned int hash;
  size_t i;

  if (kt->name_hash_table == NULL)
    build_keyword_hash_tables (kt);

  hash = hash_keyword_name (kt, ke->name, 0);
  ke->next_name = kt->name_hash_table[hash];
  kt->name_hash_table[hash] = ke;

  hash = hash_keyword_value (kt, ke->value);
  ke->next_value = kt->value_hash_table[hash];
  kt->value_hash_table[hash] = ke;

  if (ke->name[0] == 0)
    kt->null_entry = ke;

  for (i = 1; i < strlen (ke->name); i++)
    if (! ISALNUM (ke->name[i])
	&& ! strchr (kt->nonalpha_chars, ke->name[i]))
      {
	size_t idx = strlen (kt->nonalpha_chars);
	
	/* If you hit this limit, please don't just
	   increase the size of the field, instead
	   look for a better algorithm.  */
	if (idx >= sizeof (kt->nonalpha_chars) - 1)
	  abort ();
	kt->nonalpha_chars[idx] = ke->name[i];
	kt->nonalpha_chars[idx+1] = 0;
      }
}

/* FIXME: Need function to return count of keywords.  */

/* Initialize a keyword table search.
   SPEC is a specification of what to search for.
   A value of NULL means to find every keyword.
   Currently NULL is the only acceptable value [further specification
   deferred].
   The result is an opaque data item used to record the search status.
   It is passed to each call to cgen_keyword_search_next.  */

CGEN_KEYWORD_SEARCH
cgen_keyword_search_init (CGEN_KEYWORD *kt, const char *spec)
{
  CGEN_KEYWORD_SEARCH search;

  /* FIXME: Need to specify format of params.  */
  if (spec != NULL)
    abort ();

  if (kt->name_hash_table == NULL)
    build_keyword_hash_tables (kt);

  search.table = kt;
  search.spec = spec;
  search.current_hash = 0;
  search.current_entry = NULL;
  return search;
}

/* Return the next keyword specified by SEARCH.
   The result is the next entry or NULL if there are no more.  */

const CGEN_KEYWORD_ENTRY *
cgen_keyword_search_next (CGEN_KEYWORD_SEARCH *search)
{
  /* Has search finished?  */
  if (search->current_hash == search->table->hash_table_size)
    return NULL;

  /* Search in progress?  */
  if (search->current_entry != NULL
      /* Anything left on this hash chain?  */
      && search->current_entry->next_name != NULL)
    {
      search->current_entry = search->current_entry->next_name;
      return search->current_entry;
    }

  /* Move to next hash chain [unless we haven't started yet].  */
  if (search->current_entry != NULL)
    ++search->current_hash;

  while (search->current_hash < search->table->hash_table_size)
    {
      search->current_entry = search->table->name_hash_table[search->current_hash];
      if (search->current_entry != NULL)
	return search->current_entry;
      ++search->current_hash;
    }

  return NULL;
}

/* Return first entry in hash chain for NAME.
   If CASE_SENSITIVE_P is non-zero, return a case sensitive hash.  */

static unsigned int
hash_keyword_name (const CGEN_KEYWORD *kt,
		   const char *name,
		   int case_sensitive_p)
{
  unsigned int hash;

  if (case_sensitive_p)
    for (hash = 0; *name; ++name)
      hash = (hash * 97) + (unsigned char) *name;
  else
    for (hash = 0; *name; ++name)
      hash = (hash * 97) + (unsigned char) TOLOWER (*name);
  return hash % kt->hash_table_size;
}

/* Return first entry in hash chain for VALUE.  */

static unsigned int
hash_keyword_value (const CGEN_KEYWORD *kt, unsigned int value)
{
  return value % kt->hash_table_size;
}

/* Build a keyword table's hash tables.
   We probably needn't build the value hash table for the assembler when
   we're using the disassembler, but we keep things simple.  */

static void
build_keyword_hash_tables (CGEN_KEYWORD *kt)
{
  int i;
  /* Use the number of compiled in entries as an estimate for the
     typical sized table [not too many added at runtime].  */
  unsigned int size = KEYWORD_HASH_SIZE (kt->num_init_entries);

  kt->hash_table_size = size;
  kt->name_hash_table = (CGEN_KEYWORD_ENTRY **)
    xmalloc (size * sizeof (CGEN_KEYWORD_ENTRY *));
  memset (kt->name_hash_table, 0, size * sizeof (CGEN_KEYWORD_ENTRY *));
  kt->value_hash_table = (CGEN_KEYWORD_ENTRY **)
    xmalloc (size * sizeof (CGEN_KEYWORD_ENTRY *));
  memset (kt->value_hash_table, 0, size * sizeof (CGEN_KEYWORD_ENTRY *));

  /* The table is scanned backwards as we want keywords appearing earlier to
     be prefered over later ones.  */
  for (i = kt->num_init_entries - 1; i >= 0; --i)
    cgen_keyword_add (kt, &kt->init_entries[i]);
}

/* Hardware support.  */

/* Lookup a hardware element by its name.
   Returns NULL if NAME is not supported by the currently selected
   mach/isa.  */

const CGEN_HW_ENTRY *
cgen_hw_lookup_by_name (CGEN_CPU_DESC cd, const char *name)
{
  unsigned int i;
  const CGEN_HW_ENTRY **hw = cd->hw_table.entries;

  for (i = 0; i < cd->hw_table.num_entries; ++i)
    if (hw[i] && strcmp (name, hw[i]->name) == 0)
      return hw[i];

  return NULL;
}

/* Lookup a hardware element by its number.
   Hardware elements are enumerated, however it may be possible to add some
   at runtime, thus HWNUM is not an enum type but rather an int.
   Returns NULL if HWNUM is not supported by the currently selected mach.  */

const CGEN_HW_ENTRY *
cgen_hw_lookup_by_num (CGEN_CPU_DESC cd, unsigned int hwnum)
{
  unsigned int i;
  const CGEN_HW_ENTRY **hw = cd->hw_table.entries;

  /* ??? This can be speeded up.  */
  for (i = 0; i < cd->hw_table.num_entries; ++i)
    if (hw[i] && hwnum == hw[i]->type)
      return hw[i];

  return NULL;
}

/* Operand support.  */

/* Lookup an operand by its name.
   Returns NULL if NAME is not supported by the currently selected
   mach/isa.  */

const CGEN_OPERAND *
cgen_operand_lookup_by_name (CGEN_CPU_DESC cd, const char *name)
{
  unsigned int i;
  const CGEN_OPERAND **op = cd->operand_table.entries;

  for (i = 0; i < cd->operand_table.num_entries; ++i)
    if (op[i] && strcmp (name, op[i]->name) == 0)
      return op[i];

  return NULL;
}

/* Lookup an operand by its number.
   Operands are enumerated, however it may be possible to add some
   at runtime, thus OPNUM is not an enum type but rather an int.
   Returns NULL if OPNUM is not supported by the currently selected
   mach/isa.  */

const CGEN_OPERAND *
cgen_operand_lookup_by_num (CGEN_CPU_DESC cd, int opnum)
{
  return cd->operand_table.entries[opnum];
}

/* Instruction support.  */

/* Return number of instructions.  This includes any added at runtime.  */

int
cgen_insn_count (CGEN_CPU_DESC cd)
{
  int count = cd->insn_table.num_init_entries;
  CGEN_INSN_LIST *rt_insns = cd->insn_table.new_entries;

  for ( ; rt_insns != NULL; rt_insns = rt_insns->next)
    ++count;

  return count;
}

/* Return number of macro-instructions.
   This includes any added at runtime.  */

int
cgen_macro_insn_count (CGEN_CPU_DESC cd)
{
  int count = cd->macro_insn_table.num_init_entries;
  CGEN_INSN_LIST *rt_insns = cd->macro_insn_table.new_entries;

  for ( ; rt_insns != NULL; rt_insns = rt_insns->next)
    ++count;

  return count;
}

/* Cover function to read and properly byteswap an insn value.  */

CGEN_INSN_INT
cgen_get_insn_value (CGEN_CPU_DESC cd, unsigned char *buf, int length)
{
  int big_p = (cd->insn_endian == CGEN_ENDIAN_BIG);
  int insn_chunk_bitsize = cd->insn_chunk_bitsize;
  CGEN_INSN_INT value = 0;

  if (insn_chunk_bitsize != 0 && insn_chunk_bitsize < length)
    {
      /* We need to divide up the incoming value into insn_chunk_bitsize-length
	 segments, and endian-convert them, one at a time. */
      int i;

      /* Enforce divisibility. */ 
      if ((length % insn_chunk_bitsize) != 0)
	abort ();

      for (i = 0; i < length; i += insn_chunk_bitsize) /* NB: i == bits */
	{
	  int index;
	  bfd_vma this_value;
	  index = i; /* NB: not dependent on endianness; opposite of cgen_put_insn_value! */
	  this_value = bfd_get_bits (& buf[index / 8], insn_chunk_bitsize, big_p);
	  value = (value << insn_chunk_bitsize) | this_value;
	}
    }
  else
    {
      value = bfd_get_bits (buf, length, cd->insn_endian == CGEN_ENDIAN_BIG);
    }

  return value;
}

/* Cover function to store an insn value properly byteswapped.  */

void
cgen_put_insn_value (CGEN_CPU_DESC cd,
		     unsigned char *buf,
		     int length,
		     CGEN_INSN_INT value)
{
  int big_p = (cd->insn_endian == CGEN_ENDIAN_BIG);
  int insn_chunk_bitsize = cd->insn_chunk_bitsize;

  if (insn_chunk_bitsize != 0 && insn_chunk_bitsize < length)
    {
      /* We need to divide up the incoming value into insn_chunk_bitsize-length
	 segments, and endian-convert them, one at a time. */
      int i;

      /* Enforce divisibility. */ 
      if ((length % insn_chunk_bitsize) != 0)
	abort ();

      for (i = 0; i < length; i += insn_chunk_bitsize) /* NB: i == bits */
	{
	  int index;
	  index = (length - insn_chunk_bitsize - i); /* NB: not dependent on endianness! */
	  bfd_put_bits ((bfd_vma) value, & buf[index / 8], insn_chunk_bitsize, big_p);
	  value >>= insn_chunk_bitsize;
	}
    }
  else
    {
      bfd_put_bits ((bfd_vma) value, buf, length, big_p);
    }
}

/* Look up instruction INSN_*_VALUE and extract its fields.
   INSN_INT_VALUE is used if CGEN_INT_INSN_P.
   Otherwise INSN_BYTES_VALUE is used.
   INSN, if non-null, is the insn table entry.
   Otherwise INSN_*_VALUE is examined to compute it.
   LENGTH is the bit length of INSN_*_VALUE if known, otherwise 0.
   0 is only valid if `insn == NULL && ! CGEN_INT_INSN_P'.
   If INSN != NULL, LENGTH must be valid.
   ALIAS_P is non-zero if alias insns are to be included in the search.

   The result is a pointer to the insn table entry, or NULL if the instruction
   wasn't recognized.  */

/* ??? Will need to be revisited for VLIW architectures.  */

const CGEN_INSN *
cgen_lookup_insn (CGEN_CPU_DESC cd,
		  const CGEN_INSN *insn,
		  CGEN_INSN_INT insn_int_value,
		  /* ??? CGEN_INSN_BYTES would be a nice type name to use here.  */
		  unsigned char *insn_bytes_value,
		  int length,
		  CGEN_FIELDS *fields,
		  int alias_p)
{
  unsigned char *buf;
  CGEN_INSN_INT base_insn;
  CGEN_EXTRACT_INFO ex_info;
  CGEN_EXTRACT_INFO *info;

  if (cd->int_insn_p)
    {
      info = NULL;
      buf = (unsigned char *) alloca (cd->max_insn_bitsize / 8);
      cgen_put_insn_value (cd, buf, length, insn_int_value);
      base_insn = insn_int_value;
    }
  else
    {
      info = &ex_info;
      ex_info.dis_info = NULL;
      ex_info.insn_bytes = insn_bytes_value;
      ex_info.valid = -1;
      buf = insn_bytes_value;
      base_insn = cgen_get_insn_value (cd, buf, length);
    }

  if (!insn)
    {
      const CGEN_INSN_LIST *insn_list;

      /* The instructions are stored in hash lists.
	 Pick the first one and keep trying until we find the right one.  */

      insn_list = cgen_dis_lookup_insn (cd, (char *) buf, base_insn);
      while (insn_list != NULL)
	{
	  insn = insn_list->insn;

	  if (alias_p
	      /* FIXME: Ensure ALIAS attribute always has same index.  */
	      || ! CGEN_INSN_ATTR_VALUE (insn, CGEN_INSN_ALIAS))
	    {
	      /* Basic bit mask must be correct.  */
	      /* ??? May wish to allow target to defer this check until the
		 extract handler.  */
	      if ((base_insn & CGEN_INSN_BASE_MASK (insn))
		  == CGEN_INSN_BASE_VALUE (insn))
		{
		  /* ??? 0 is passed for `pc' */
		  int elength = CGEN_EXTRACT_FN (cd, insn)
		    (cd, insn, info, base_insn, fields, (bfd_vma) 0);
		  if (elength > 0)
		    {
		      /* sanity check */
		      if (length != 0 && length != elength)
			abort ();
		      return insn;
		    }
		}
	    }

	  insn_list = insn_list->next;
	}
    }
  else
    {
      /* Sanity check: can't pass an alias insn if ! alias_p.  */
      if (! alias_p
	  && CGEN_INSN_ATTR_VALUE (insn, CGEN_INSN_ALIAS))
	abort ();
      /* Sanity check: length must be correct.  */
      if (length != CGEN_INSN_BITSIZE (insn))
	abort ();

      /* ??? 0 is passed for `pc' */
      length = CGEN_EXTRACT_FN (cd, insn)
	(cd, insn, info, base_insn, fields, (bfd_vma) 0);
      /* Sanity check: must succeed.
	 Could relax this later if it ever proves useful.  */
      if (length == 0)
	abort ();
      return insn;
    }

  return NULL;
}

/* Fill in the operand instances used by INSN whose operands are FIELDS.
   INDICES is a pointer to a buffer of MAX_OPERAND_INSTANCES ints to be filled
   in.  */

void
cgen_get_insn_operands (CGEN_CPU_DESC cd,
			const CGEN_INSN *insn,
			const CGEN_FIELDS *fields,
			int *indices)
{
  const CGEN_OPINST *opinst;
  int i;

  if (insn->opinst == NULL)
    abort ();
  for (i = 0, opinst = insn->opinst; opinst->type != CGEN_OPINST_END; ++i, ++opinst)
    {
      enum cgen_operand_type op_type = opinst->op_type;
      if (op_type == CGEN_OPERAND_NIL)
	indices[i] = opinst->index;
      else
	indices[i] = (*cd->get_int_operand) (cd, op_type, fields);
    }
}

/* Cover function to cgen_get_insn_operands when either INSN or FIELDS
   isn't known.
   The INSN, INSN_*_VALUE, and LENGTH arguments are passed to
   cgen_lookup_insn unchanged.
   INSN_INT_VALUE is used if CGEN_INT_INSN_P.
   Otherwise INSN_BYTES_VALUE is used.

   The result is the insn table entry or NULL if the instruction wasn't
   recognized.  */

const CGEN_INSN *
cgen_lookup_get_insn_operands (CGEN_CPU_DESC cd,
			       const CGEN_INSN *insn,
			       CGEN_INSN_INT insn_int_value,
			       /* ??? CGEN_INSN_BYTES would be a nice type name to use here.  */
			       unsigned char *insn_bytes_value,
			       int length,
			       int *indices,
			       CGEN_FIELDS *fields)
{
  /* Pass non-zero for ALIAS_P only if INSN != NULL.
     If INSN == NULL, we want a real insn.  */
  insn = cgen_lookup_insn (cd, insn, insn_int_value, insn_bytes_value,
			   length, fields, insn != NULL);
  if (! insn)
    return NULL;

  cgen_get_insn_operands (cd, insn, fields, indices);
  return insn;
}

/* Allow signed overflow of instruction fields.  */
void
cgen_set_signed_overflow_ok (CGEN_CPU_DESC cd)
{
  cd->signed_overflow_ok_p = 1;
}

/* Generate an error message if a signed field in an instruction overflows.  */
void
cgen_clear_signed_overflow_ok (CGEN_CPU_DESC cd)
{
  cd->signed_overflow_ok_p = 0;
}

/* Will an error message be generated if a signed field in an instruction overflows ? */
unsigned int
cgen_signed_overflow_ok_p (CGEN_CPU_DESC cd)
{
  return cd->signed_overflow_ok_p;
}
