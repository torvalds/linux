/* Common subexpression elimination library for GNU compiler.
   Copyright (C) 1987, 1988, 1989, 1992, 1993, 1994, 1995, 1996, 1997, 1998,
   1999, 2000, 2001, 2003, 2004, 2005 Free Software Foundation, Inc.

This file is part of GCC.

GCC is free software; you can redistribute it and/or modify it under
the terms of the GNU General Public License as published by the Free
Software Foundation; either version 2, or (at your option) any later
version.

GCC is distributed in the hope that it will be useful, but WITHOUT ANY
WARRANTY; without even the implied warranty of MERCHANTABILITY or
FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
for more details.

You should have received a copy of the GNU General Public License
along with GCC; see the file COPYING.  If not, write to the Free
Software Foundation, 51 Franklin Street, Fifth Floor, Boston, MA
02110-1301, USA.  */

#include "config.h"
#include "system.h"
#include "coretypes.h"
#include "tm.h"

#include "rtl.h"
#include "tm_p.h"
#include "regs.h"
#include "hard-reg-set.h"
#include "flags.h"
#include "real.h"
#include "insn-config.h"
#include "recog.h"
#include "function.h"
#include "emit-rtl.h"
#include "toplev.h"
#include "output.h"
#include "ggc.h"
#include "hashtab.h"
#include "cselib.h"
#include "params.h"
#include "alloc-pool.h"
#include "target.h"

static bool cselib_record_memory;
static int entry_and_rtx_equal_p (const void *, const void *);
static hashval_t get_value_hash (const void *);
static struct elt_list *new_elt_list (struct elt_list *, cselib_val *);
static struct elt_loc_list *new_elt_loc_list (struct elt_loc_list *, rtx);
static void unchain_one_value (cselib_val *);
static void unchain_one_elt_list (struct elt_list **);
static void unchain_one_elt_loc_list (struct elt_loc_list **);
static int discard_useless_locs (void **, void *);
static int discard_useless_values (void **, void *);
static void remove_useless_values (void);
static rtx wrap_constant (enum machine_mode, rtx);
static unsigned int cselib_hash_rtx (rtx, int);
static cselib_val *new_cselib_val (unsigned int, enum machine_mode);
static void add_mem_for_addr (cselib_val *, cselib_val *, rtx);
static cselib_val *cselib_lookup_mem (rtx, int);
static void cselib_invalidate_regno (unsigned int, enum machine_mode);
static void cselib_invalidate_mem (rtx);
static void cselib_record_set (rtx, cselib_val *, cselib_val *);
static void cselib_record_sets (rtx);

/* There are three ways in which cselib can look up an rtx:
   - for a REG, the reg_values table (which is indexed by regno) is used
   - for a MEM, we recursively look up its address and then follow the
     addr_list of that value
   - for everything else, we compute a hash value and go through the hash
     table.  Since different rtx's can still have the same hash value,
     this involves walking the table entries for a given value and comparing
     the locations of the entries with the rtx we are looking up.  */

/* A table that enables us to look up elts by their value.  */
static htab_t cselib_hash_table;

/* This is a global so we don't have to pass this through every function.
   It is used in new_elt_loc_list to set SETTING_INSN.  */
static rtx cselib_current_insn;
static bool cselib_current_insn_in_libcall;

/* Every new unknown value gets a unique number.  */
static unsigned int next_unknown_value;

/* The number of registers we had when the varrays were last resized.  */
static unsigned int cselib_nregs;

/* Count values without known locations.  Whenever this grows too big, we
   remove these useless values from the table.  */
static int n_useless_values;

/* Number of useless values before we remove them from the hash table.  */
#define MAX_USELESS_VALUES 32

/* This table maps from register number to values.  It does not
   contain pointers to cselib_val structures, but rather elt_lists.
   The purpose is to be able to refer to the same register in
   different modes.  The first element of the list defines the mode in
   which the register was set; if the mode is unknown or the value is
   no longer valid in that mode, ELT will be NULL for the first
   element.  */
static struct elt_list **reg_values;
static unsigned int reg_values_size;
#define REG_VALUES(i) reg_values[i]

/* The largest number of hard regs used by any entry added to the
   REG_VALUES table.  Cleared on each cselib_clear_table() invocation.  */
static unsigned int max_value_regs;

/* Here the set of indices I with REG_VALUES(I) != 0 is saved.  This is used
   in cselib_clear_table() for fast emptying.  */
static unsigned int *used_regs;
static unsigned int n_used_regs;

/* We pass this to cselib_invalidate_mem to invalidate all of
   memory for a non-const call instruction.  */
static GTY(()) rtx callmem;

/* Set by discard_useless_locs if it deleted the last location of any
   value.  */
static int values_became_useless;

/* Used as stop element of the containing_mem list so we can check
   presence in the list by checking the next pointer.  */
static cselib_val dummy_val;

/* Used to list all values that contain memory reference.
   May or may not contain the useless values - the list is compacted
   each time memory is invalidated.  */
static cselib_val *first_containing_mem = &dummy_val;
static alloc_pool elt_loc_list_pool, elt_list_pool, cselib_val_pool, value_pool;


/* Allocate a struct elt_list and fill in its two elements with the
   arguments.  */

static inline struct elt_list *
new_elt_list (struct elt_list *next, cselib_val *elt)
{
  struct elt_list *el;
  el = pool_alloc (elt_list_pool);
  el->next = next;
  el->elt = elt;
  return el;
}

/* Allocate a struct elt_loc_list and fill in its two elements with the
   arguments.  */

static inline struct elt_loc_list *
new_elt_loc_list (struct elt_loc_list *next, rtx loc)
{
  struct elt_loc_list *el;
  el = pool_alloc (elt_loc_list_pool);
  el->next = next;
  el->loc = loc;
  el->setting_insn = cselib_current_insn;
  el->in_libcall = cselib_current_insn_in_libcall;
  return el;
}

/* The elt_list at *PL is no longer needed.  Unchain it and free its
   storage.  */

static inline void
unchain_one_elt_list (struct elt_list **pl)
{
  struct elt_list *l = *pl;

  *pl = l->next;
  pool_free (elt_list_pool, l);
}

/* Likewise for elt_loc_lists.  */

static void
unchain_one_elt_loc_list (struct elt_loc_list **pl)
{
  struct elt_loc_list *l = *pl;

  *pl = l->next;
  pool_free (elt_loc_list_pool, l);
}

/* Likewise for cselib_vals.  This also frees the addr_list associated with
   V.  */

static void
unchain_one_value (cselib_val *v)
{
  while (v->addr_list)
    unchain_one_elt_list (&v->addr_list);

  pool_free (cselib_val_pool, v);
}

/* Remove all entries from the hash table.  Also used during
   initialization.  If CLEAR_ALL isn't set, then only clear the entries
   which are known to have been used.  */

void
cselib_clear_table (void)
{
  unsigned int i;

  for (i = 0; i < n_used_regs; i++)
    REG_VALUES (used_regs[i]) = 0;

  max_value_regs = 0;

  n_used_regs = 0;

  htab_empty (cselib_hash_table);

  n_useless_values = 0;

  next_unknown_value = 0;

  first_containing_mem = &dummy_val;
}

/* The equality test for our hash table.  The first argument ENTRY is a table
   element (i.e. a cselib_val), while the second arg X is an rtx.  We know
   that all callers of htab_find_slot_with_hash will wrap CONST_INTs into a
   CONST of an appropriate mode.  */

static int
entry_and_rtx_equal_p (const void *entry, const void *x_arg)
{
  struct elt_loc_list *l;
  const cselib_val *v = (const cselib_val *) entry;
  rtx x = (rtx) x_arg;
  enum machine_mode mode = GET_MODE (x);

  gcc_assert (GET_CODE (x) != CONST_INT
	      && (mode != VOIDmode || GET_CODE (x) != CONST_DOUBLE));
  
  if (mode != GET_MODE (v->u.val_rtx))
    return 0;

  /* Unwrap X if necessary.  */
  if (GET_CODE (x) == CONST
      && (GET_CODE (XEXP (x, 0)) == CONST_INT
	  || GET_CODE (XEXP (x, 0)) == CONST_DOUBLE))
    x = XEXP (x, 0);

  /* We don't guarantee that distinct rtx's have different hash values,
     so we need to do a comparison.  */
  for (l = v->locs; l; l = l->next)
    if (rtx_equal_for_cselib_p (l->loc, x))
      return 1;

  return 0;
}

/* The hash function for our hash table.  The value is always computed with
   cselib_hash_rtx when adding an element; this function just extracts the
   hash value from a cselib_val structure.  */

static hashval_t
get_value_hash (const void *entry)
{
  const cselib_val *v = (const cselib_val *) entry;
  return v->value;
}

/* Return true if X contains a VALUE rtx.  If ONLY_USELESS is set, we
   only return true for values which point to a cselib_val whose value
   element has been set to zero, which implies the cselib_val will be
   removed.  */

int
references_value_p (rtx x, int only_useless)
{
  enum rtx_code code = GET_CODE (x);
  const char *fmt = GET_RTX_FORMAT (code);
  int i, j;

  if (GET_CODE (x) == VALUE
      && (! only_useless || CSELIB_VAL_PTR (x)->locs == 0))
    return 1;

  for (i = GET_RTX_LENGTH (code) - 1; i >= 0; i--)
    {
      if (fmt[i] == 'e' && references_value_p (XEXP (x, i), only_useless))
	return 1;
      else if (fmt[i] == 'E')
	for (j = 0; j < XVECLEN (x, i); j++)
	  if (references_value_p (XVECEXP (x, i, j), only_useless))
	    return 1;
    }

  return 0;
}

/* For all locations found in X, delete locations that reference useless
   values (i.e. values without any location).  Called through
   htab_traverse.  */

static int
discard_useless_locs (void **x, void *info ATTRIBUTE_UNUSED)
{
  cselib_val *v = (cselib_val *)*x;
  struct elt_loc_list **p = &v->locs;
  int had_locs = v->locs != 0;

  while (*p)
    {
      if (references_value_p ((*p)->loc, 1))
	unchain_one_elt_loc_list (p);
      else
	p = &(*p)->next;
    }

  if (had_locs && v->locs == 0)
    {
      n_useless_values++;
      values_became_useless = 1;
    }
  return 1;
}

/* If X is a value with no locations, remove it from the hashtable.  */

static int
discard_useless_values (void **x, void *info ATTRIBUTE_UNUSED)
{
  cselib_val *v = (cselib_val *)*x;

  if (v->locs == 0)
    {
      CSELIB_VAL_PTR (v->u.val_rtx) = NULL;
      htab_clear_slot (cselib_hash_table, x);
      unchain_one_value (v);
      n_useless_values--;
    }

  return 1;
}

/* Clean out useless values (i.e. those which no longer have locations
   associated with them) from the hash table.  */

static void
remove_useless_values (void)
{
  cselib_val **p, *v;
  /* First pass: eliminate locations that reference the value.  That in
     turn can make more values useless.  */
  do
    {
      values_became_useless = 0;
      htab_traverse (cselib_hash_table, discard_useless_locs, 0);
    }
  while (values_became_useless);

  /* Second pass: actually remove the values.  */

  p = &first_containing_mem;
  for (v = *p; v != &dummy_val; v = v->next_containing_mem)
    if (v->locs)
      {
	*p = v;
	p = &(*p)->next_containing_mem;
      }
  *p = &dummy_val;

  htab_traverse (cselib_hash_table, discard_useless_values, 0);

  gcc_assert (!n_useless_values);
}

/* Return the mode in which a register was last set.  If X is not a
   register, return its mode.  If the mode in which the register was
   set is not known, or the value was already clobbered, return
   VOIDmode.  */

enum machine_mode
cselib_reg_set_mode (rtx x)
{
  if (!REG_P (x))
    return GET_MODE (x);

  if (REG_VALUES (REGNO (x)) == NULL
      || REG_VALUES (REGNO (x))->elt == NULL)
    return VOIDmode;

  return GET_MODE (REG_VALUES (REGNO (x))->elt->u.val_rtx);
}

/* Return nonzero if we can prove that X and Y contain the same value, taking
   our gathered information into account.  */

int
rtx_equal_for_cselib_p (rtx x, rtx y)
{
  enum rtx_code code;
  const char *fmt;
  int i;

  if (REG_P (x) || MEM_P (x))
    {
      cselib_val *e = cselib_lookup (x, GET_MODE (x), 0);

      if (e)
	x = e->u.val_rtx;
    }

  if (REG_P (y) || MEM_P (y))
    {
      cselib_val *e = cselib_lookup (y, GET_MODE (y), 0);

      if (e)
	y = e->u.val_rtx;
    }

  if (x == y)
    return 1;

  if (GET_CODE (x) == VALUE && GET_CODE (y) == VALUE)
    return CSELIB_VAL_PTR (x) == CSELIB_VAL_PTR (y);

  if (GET_CODE (x) == VALUE)
    {
      cselib_val *e = CSELIB_VAL_PTR (x);
      struct elt_loc_list *l;

      for (l = e->locs; l; l = l->next)
	{
	  rtx t = l->loc;

	  /* Avoid infinite recursion.  */
	  if (REG_P (t) || MEM_P (t))
	    continue;
	  else if (rtx_equal_for_cselib_p (t, y))
	    return 1;
	}

      return 0;
    }

  if (GET_CODE (y) == VALUE)
    {
      cselib_val *e = CSELIB_VAL_PTR (y);
      struct elt_loc_list *l;

      for (l = e->locs; l; l = l->next)
	{
	  rtx t = l->loc;

	  if (REG_P (t) || MEM_P (t))
	    continue;
	  else if (rtx_equal_for_cselib_p (x, t))
	    return 1;
	}

      return 0;
    }

  if (GET_CODE (x) != GET_CODE (y) || GET_MODE (x) != GET_MODE (y))
    return 0;

  /* These won't be handled correctly by the code below.  */
  switch (GET_CODE (x))
    {
    case CONST_DOUBLE:
      return 0;

    case LABEL_REF:
      return XEXP (x, 0) == XEXP (y, 0);

    default:
      break;
    }

  code = GET_CODE (x);
  fmt = GET_RTX_FORMAT (code);

  for (i = GET_RTX_LENGTH (code) - 1; i >= 0; i--)
    {
      int j;

      switch (fmt[i])
	{
	case 'w':
	  if (XWINT (x, i) != XWINT (y, i))
	    return 0;
	  break;

	case 'n':
	case 'i':
	  if (XINT (x, i) != XINT (y, i))
	    return 0;
	  break;

	case 'V':
	case 'E':
	  /* Two vectors must have the same length.  */
	  if (XVECLEN (x, i) != XVECLEN (y, i))
	    return 0;

	  /* And the corresponding elements must match.  */
	  for (j = 0; j < XVECLEN (x, i); j++)
	    if (! rtx_equal_for_cselib_p (XVECEXP (x, i, j),
					  XVECEXP (y, i, j)))
	      return 0;
	  break;

	case 'e':
	  if (i == 1
	      && targetm.commutative_p (x, UNKNOWN)
	      && rtx_equal_for_cselib_p (XEXP (x, 1), XEXP (y, 0))
	      && rtx_equal_for_cselib_p (XEXP (x, 0), XEXP (y, 1)))
	    return 1;
	  if (! rtx_equal_for_cselib_p (XEXP (x, i), XEXP (y, i)))
	    return 0;
	  break;

	case 'S':
	case 's':
	  if (strcmp (XSTR (x, i), XSTR (y, i)))
	    return 0;
	  break;

	case 'u':
	  /* These are just backpointers, so they don't matter.  */
	  break;

	case '0':
	case 't':
	  break;

	  /* It is believed that rtx's at this level will never
	     contain anything but integers and other rtx's,
	     except for within LABEL_REFs and SYMBOL_REFs.  */
	default:
	  gcc_unreachable ();
	}
    }
  return 1;
}

/* We need to pass down the mode of constants through the hash table
   functions.  For that purpose, wrap them in a CONST of the appropriate
   mode.  */
static rtx
wrap_constant (enum machine_mode mode, rtx x)
{
  if (GET_CODE (x) != CONST_INT
      && (GET_CODE (x) != CONST_DOUBLE || GET_MODE (x) != VOIDmode))
    return x;
  gcc_assert (mode != VOIDmode);
  return gen_rtx_CONST (mode, x);
}

/* Hash an rtx.  Return 0 if we couldn't hash the rtx.
   For registers and memory locations, we look up their cselib_val structure
   and return its VALUE element.
   Possible reasons for return 0 are: the object is volatile, or we couldn't
   find a register or memory location in the table and CREATE is zero.  If
   CREATE is nonzero, table elts are created for regs and mem.
   N.B. this hash function returns the same hash value for RTXes that
   differ only in the order of operands, thus it is suitable for comparisons
   that take commutativity into account.
   If we wanted to also support associative rules, we'd have to use a different
   strategy to avoid returning spurious 0, e.g. return ~(~0U >> 1) .
   We used to have a MODE argument for hashing for CONST_INTs, but that
   didn't make sense, since it caused spurious hash differences between
    (set (reg:SI 1) (const_int))
    (plus:SI (reg:SI 2) (reg:SI 1))
   and
    (plus:SI (reg:SI 2) (const_int))
   If the mode is important in any context, it must be checked specifically
   in a comparison anyway, since relying on hash differences is unsafe.  */

static unsigned int
cselib_hash_rtx (rtx x, int create)
{
  cselib_val *e;
  int i, j;
  enum rtx_code code;
  const char *fmt;
  unsigned int hash = 0;

  code = GET_CODE (x);
  hash += (unsigned) code + (unsigned) GET_MODE (x);

  switch (code)
    {
    case MEM:
    case REG:
      e = cselib_lookup (x, GET_MODE (x), create);
      if (! e)
	return 0;

      return e->value;

    case CONST_INT:
      hash += ((unsigned) CONST_INT << 7) + INTVAL (x);
      return hash ? hash : (unsigned int) CONST_INT;

    case CONST_DOUBLE:
      /* This is like the general case, except that it only counts
	 the integers representing the constant.  */
      hash += (unsigned) code + (unsigned) GET_MODE (x);
      if (GET_MODE (x) != VOIDmode)
	hash += real_hash (CONST_DOUBLE_REAL_VALUE (x));
      else
	hash += ((unsigned) CONST_DOUBLE_LOW (x)
		 + (unsigned) CONST_DOUBLE_HIGH (x));
      return hash ? hash : (unsigned int) CONST_DOUBLE;

    case CONST_VECTOR:
      {
	int units;
	rtx elt;

	units = CONST_VECTOR_NUNITS (x);

	for (i = 0; i < units; ++i)
	  {
	    elt = CONST_VECTOR_ELT (x, i);
	    hash += cselib_hash_rtx (elt, 0);
	  }

	return hash;
      }

      /* Assume there is only one rtx object for any given label.  */
    case LABEL_REF:
      /* We don't hash on the address of the CODE_LABEL to avoid bootstrap
	 differences and differences between each stage's debugging dumps.  */
      hash += (((unsigned int) LABEL_REF << 7)
	       + CODE_LABEL_NUMBER (XEXP (x, 0)));
      return hash ? hash : (unsigned int) LABEL_REF;

    case SYMBOL_REF:
      {
	/* Don't hash on the symbol's address to avoid bootstrap differences.
	   Different hash values may cause expressions to be recorded in
	   different orders and thus different registers to be used in the
	   final assembler.  This also avoids differences in the dump files
	   between various stages.  */
	unsigned int h = 0;
	const unsigned char *p = (const unsigned char *) XSTR (x, 0);

	while (*p)
	  h += (h << 7) + *p++; /* ??? revisit */

	hash += ((unsigned int) SYMBOL_REF << 7) + h;
	return hash ? hash : (unsigned int) SYMBOL_REF;
      }

    case PRE_DEC:
    case PRE_INC:
    case POST_DEC:
    case POST_INC:
    case POST_MODIFY:
    case PRE_MODIFY:
    case PC:
    case CC0:
    case CALL:
    case UNSPEC_VOLATILE:
      return 0;

    case ASM_OPERANDS:
      if (MEM_VOLATILE_P (x))
	return 0;

      break;

    default:
      break;
    }

  i = GET_RTX_LENGTH (code) - 1;
  fmt = GET_RTX_FORMAT (code);
  for (; i >= 0; i--)
    {
      switch (fmt[i])
	{
	case 'e':
	  {
	    rtx tem = XEXP (x, i);
	    unsigned int tem_hash = cselib_hash_rtx (tem, create);
	    
	    if (tem_hash == 0)
	      return 0;
	    
	    hash += tem_hash;
	  }
	  break;
	case 'E':
	  for (j = 0; j < XVECLEN (x, i); j++)
	    {
	      unsigned int tem_hash
		= cselib_hash_rtx (XVECEXP (x, i, j), create);
	      
	      if (tem_hash == 0)
		return 0;
	      
	      hash += tem_hash;
	    }
	  break;

	case 's':
	  {
	    const unsigned char *p = (const unsigned char *) XSTR (x, i);
	    
	    if (p)
	      while (*p)
		hash += *p++;
	    break;
	  }
	  
	case 'i':
	  hash += XINT (x, i);
	  break;

	case '0':
	case 't':
	  /* unused */
	  break;
	  
	default:
	  gcc_unreachable ();
	}
    }

  return hash ? hash : 1 + (unsigned int) GET_CODE (x);
}

/* Create a new value structure for VALUE and initialize it.  The mode of the
   value is MODE.  */

static inline cselib_val *
new_cselib_val (unsigned int value, enum machine_mode mode)
{
  cselib_val *e = pool_alloc (cselib_val_pool);

  gcc_assert (value);

  e->value = value;
  /* We use an alloc pool to allocate this RTL construct because it
     accounts for about 8% of the overall memory usage.  We know
     precisely when we can have VALUE RTXen (when cselib is active)
     so we don't need to put them in garbage collected memory.
     ??? Why should a VALUE be an RTX in the first place?  */
  e->u.val_rtx = pool_alloc (value_pool);
  memset (e->u.val_rtx, 0, RTX_HDR_SIZE);
  PUT_CODE (e->u.val_rtx, VALUE);
  PUT_MODE (e->u.val_rtx, mode);
  CSELIB_VAL_PTR (e->u.val_rtx) = e;
  e->addr_list = 0;
  e->locs = 0;
  e->next_containing_mem = 0;
  return e;
}

/* ADDR_ELT is a value that is used as address.  MEM_ELT is the value that
   contains the data at this address.  X is a MEM that represents the
   value.  Update the two value structures to represent this situation.  */

static void
add_mem_for_addr (cselib_val *addr_elt, cselib_val *mem_elt, rtx x)
{
  struct elt_loc_list *l;

  /* Avoid duplicates.  */
  for (l = mem_elt->locs; l; l = l->next)
    if (MEM_P (l->loc)
	&& CSELIB_VAL_PTR (XEXP (l->loc, 0)) == addr_elt)
      return;

  addr_elt->addr_list = new_elt_list (addr_elt->addr_list, mem_elt);
  mem_elt->locs
    = new_elt_loc_list (mem_elt->locs,
			replace_equiv_address_nv (x, addr_elt->u.val_rtx));
  if (mem_elt->next_containing_mem == NULL)
    {
      mem_elt->next_containing_mem = first_containing_mem;
      first_containing_mem = mem_elt;
    }
}

/* Subroutine of cselib_lookup.  Return a value for X, which is a MEM rtx.
   If CREATE, make a new one if we haven't seen it before.  */

static cselib_val *
cselib_lookup_mem (rtx x, int create)
{
  enum machine_mode mode = GET_MODE (x);
  void **slot;
  cselib_val *addr;
  cselib_val *mem_elt;
  struct elt_list *l;

  if (MEM_VOLATILE_P (x) || mode == BLKmode
      || !cselib_record_memory
      || (FLOAT_MODE_P (mode) && flag_float_store))
    return 0;

  /* Look up the value for the address.  */
  addr = cselib_lookup (XEXP (x, 0), mode, create);
  if (! addr)
    return 0;

  /* Find a value that describes a value of our mode at that address.  */
  for (l = addr->addr_list; l; l = l->next)
    if (GET_MODE (l->elt->u.val_rtx) == mode)
      return l->elt;

  if (! create)
    return 0;

  mem_elt = new_cselib_val (++next_unknown_value, mode);
  add_mem_for_addr (addr, mem_elt, x);
  slot = htab_find_slot_with_hash (cselib_hash_table, wrap_constant (mode, x),
				   mem_elt->value, INSERT);
  *slot = mem_elt;
  return mem_elt;
}

/* Walk rtx X and replace all occurrences of REG and MEM subexpressions
   with VALUE expressions.  This way, it becomes independent of changes
   to registers and memory.
   X isn't actually modified; if modifications are needed, new rtl is
   allocated.  However, the return value can share rtl with X.  */

rtx
cselib_subst_to_values (rtx x)
{
  enum rtx_code code = GET_CODE (x);
  const char *fmt = GET_RTX_FORMAT (code);
  cselib_val *e;
  struct elt_list *l;
  rtx copy = x;
  int i;

  switch (code)
    {
    case REG:
      l = REG_VALUES (REGNO (x));
      if (l && l->elt == NULL)
	l = l->next;
      for (; l; l = l->next)
	if (GET_MODE (l->elt->u.val_rtx) == GET_MODE (x))
	  return l->elt->u.val_rtx;

      gcc_unreachable ();

    case MEM:
      e = cselib_lookup_mem (x, 0);
      if (! e)
	{
	  /* This happens for autoincrements.  Assign a value that doesn't
	     match any other.  */
	  e = new_cselib_val (++next_unknown_value, GET_MODE (x));
	}
      return e->u.val_rtx;

    case CONST_DOUBLE:
    case CONST_VECTOR:
    case CONST_INT:
      return x;

    case POST_INC:
    case PRE_INC:
    case POST_DEC:
    case PRE_DEC:
    case POST_MODIFY:
    case PRE_MODIFY:
      e = new_cselib_val (++next_unknown_value, GET_MODE (x));
      return e->u.val_rtx;

    default:
      break;
    }

  for (i = GET_RTX_LENGTH (code) - 1; i >= 0; i--)
    {
      if (fmt[i] == 'e')
	{
	  rtx t = cselib_subst_to_values (XEXP (x, i));

	  if (t != XEXP (x, i) && x == copy)
	    copy = shallow_copy_rtx (x);

	  XEXP (copy, i) = t;
	}
      else if (fmt[i] == 'E')
	{
	  int j, k;

	  for (j = 0; j < XVECLEN (x, i); j++)
	    {
	      rtx t = cselib_subst_to_values (XVECEXP (x, i, j));

	      if (t != XVECEXP (x, i, j) && XVEC (x, i) == XVEC (copy, i))
		{
		  if (x == copy)
		    copy = shallow_copy_rtx (x);

		  XVEC (copy, i) = rtvec_alloc (XVECLEN (x, i));
		  for (k = 0; k < j; k++)
		    XVECEXP (copy, i, k) = XVECEXP (x, i, k);
		}

	      XVECEXP (copy, i, j) = t;
	    }
	}
    }

  return copy;
}

/* Look up the rtl expression X in our tables and return the value it has.
   If CREATE is zero, we return NULL if we don't know the value.  Otherwise,
   we create a new one if possible, using mode MODE if X doesn't have a mode
   (i.e. because it's a constant).  */

cselib_val *
cselib_lookup (rtx x, enum machine_mode mode, int create)
{
  void **slot;
  cselib_val *e;
  unsigned int hashval;

  if (GET_MODE (x) != VOIDmode)
    mode = GET_MODE (x);

  if (GET_CODE (x) == VALUE)
    return CSELIB_VAL_PTR (x);

  if (REG_P (x))
    {
      struct elt_list *l;
      unsigned int i = REGNO (x);

      l = REG_VALUES (i);
      if (l && l->elt == NULL)
	l = l->next;
      for (; l; l = l->next)
	if (mode == GET_MODE (l->elt->u.val_rtx))
	  return l->elt;

      if (! create)
	return 0;

      if (i < FIRST_PSEUDO_REGISTER)
	{
	  unsigned int n = hard_regno_nregs[i][mode];

	  if (n > max_value_regs)
	    max_value_regs = n;
	}

      e = new_cselib_val (++next_unknown_value, GET_MODE (x));
      e->locs = new_elt_loc_list (e->locs, x);
      if (REG_VALUES (i) == 0)
	{
	  /* Maintain the invariant that the first entry of
	     REG_VALUES, if present, must be the value used to set the
	     register, or NULL.  */
	  used_regs[n_used_regs++] = i;
	  REG_VALUES (i) = new_elt_list (REG_VALUES (i), NULL);
	}
      REG_VALUES (i)->next = new_elt_list (REG_VALUES (i)->next, e);
      slot = htab_find_slot_with_hash (cselib_hash_table, x, e->value, INSERT);
      *slot = e;
      return e;
    }

  if (MEM_P (x))
    return cselib_lookup_mem (x, create);

  hashval = cselib_hash_rtx (x, create);
  /* Can't even create if hashing is not possible.  */
  if (! hashval)
    return 0;

  slot = htab_find_slot_with_hash (cselib_hash_table, wrap_constant (mode, x),
				   hashval, create ? INSERT : NO_INSERT);
  if (slot == 0)
    return 0;

  e = (cselib_val *) *slot;
  if (e)
    return e;

  e = new_cselib_val (hashval, mode);

  /* We have to fill the slot before calling cselib_subst_to_values:
     the hash table is inconsistent until we do so, and
     cselib_subst_to_values will need to do lookups.  */
  *slot = (void *) e;
  e->locs = new_elt_loc_list (e->locs, cselib_subst_to_values (x));
  return e;
}

/* Invalidate any entries in reg_values that overlap REGNO.  This is called
   if REGNO is changing.  MODE is the mode of the assignment to REGNO, which
   is used to determine how many hard registers are being changed.  If MODE
   is VOIDmode, then only REGNO is being changed; this is used when
   invalidating call clobbered registers across a call.  */

static void
cselib_invalidate_regno (unsigned int regno, enum machine_mode mode)
{
  unsigned int endregno;
  unsigned int i;

  /* If we see pseudos after reload, something is _wrong_.  */
  gcc_assert (!reload_completed || regno < FIRST_PSEUDO_REGISTER
	      || reg_renumber[regno] < 0);

  /* Determine the range of registers that must be invalidated.  For
     pseudos, only REGNO is affected.  For hard regs, we must take MODE
     into account, and we must also invalidate lower register numbers
     if they contain values that overlap REGNO.  */
  if (regno < FIRST_PSEUDO_REGISTER)
    {
      gcc_assert (mode != VOIDmode);

      if (regno < max_value_regs)
	i = 0;
      else
	i = regno - max_value_regs;

      endregno = regno + hard_regno_nregs[regno][mode];
    }
  else
    {
      i = regno;
      endregno = regno + 1;
    }

  for (; i < endregno; i++)
    {
      struct elt_list **l = &REG_VALUES (i);

      /* Go through all known values for this reg; if it overlaps the range
	 we're invalidating, remove the value.  */
      while (*l)
	{
	  cselib_val *v = (*l)->elt;
	  struct elt_loc_list **p;
	  unsigned int this_last = i;

	  if (i < FIRST_PSEUDO_REGISTER && v != NULL)
	    this_last += hard_regno_nregs[i][GET_MODE (v->u.val_rtx)] - 1;

	  if (this_last < regno || v == NULL)
	    {
	      l = &(*l)->next;
	      continue;
	    }

	  /* We have an overlap.  */
	  if (*l == REG_VALUES (i))
	    {
	      /* Maintain the invariant that the first entry of
		 REG_VALUES, if present, must be the value used to set
		 the register, or NULL.  This is also nice because
		 then we won't push the same regno onto user_regs
		 multiple times.  */
	      (*l)->elt = NULL;
	      l = &(*l)->next;
	    }
	  else
	    unchain_one_elt_list (l);

	  /* Now, we clear the mapping from value to reg.  It must exist, so
	     this code will crash intentionally if it doesn't.  */
	  for (p = &v->locs; ; p = &(*p)->next)
	    {
	      rtx x = (*p)->loc;

	      if (REG_P (x) && REGNO (x) == i)
		{
		  unchain_one_elt_loc_list (p);
		  break;
		}
	    }
	  if (v->locs == 0)
	    n_useless_values++;
	}
    }
}

/* Return 1 if X has a value that can vary even between two
   executions of the program.  0 means X can be compared reliably
   against certain constants or near-constants.  */

static int
cselib_rtx_varies_p (rtx x ATTRIBUTE_UNUSED, int from_alias ATTRIBUTE_UNUSED)
{
  /* We actually don't need to verify very hard.  This is because
     if X has actually changed, we invalidate the memory anyway,
     so assume that all common memory addresses are
     invariant.  */
  return 0;
}

/* Invalidate any locations in the table which are changed because of a
   store to MEM_RTX.  If this is called because of a non-const call
   instruction, MEM_RTX is (mem:BLK const0_rtx).  */

static void
cselib_invalidate_mem (rtx mem_rtx)
{
  cselib_val **vp, *v, *next;
  int num_mems = 0;
  rtx mem_addr;

  mem_addr = canon_rtx (get_addr (XEXP (mem_rtx, 0)));
  mem_rtx = canon_rtx (mem_rtx);

  vp = &first_containing_mem;
  for (v = *vp; v != &dummy_val; v = next)
    {
      bool has_mem = false;
      struct elt_loc_list **p = &v->locs;
      int had_locs = v->locs != 0;

      while (*p)
	{
	  rtx x = (*p)->loc;
	  cselib_val *addr;
	  struct elt_list **mem_chain;

	  /* MEMs may occur in locations only at the top level; below
	     that every MEM or REG is substituted by its VALUE.  */
	  if (!MEM_P (x))
	    {
	      p = &(*p)->next;
	      continue;
	    }
	  if (num_mems < PARAM_VALUE (PARAM_MAX_CSELIB_MEMORY_LOCATIONS)
	      && ! canon_true_dependence (mem_rtx, GET_MODE (mem_rtx), mem_addr,
		      			  x, cselib_rtx_varies_p))
	    {
	      has_mem = true;
	      num_mems++;
	      p = &(*p)->next;
	      continue;
	    }

	  /* This one overlaps.  */
	  /* We must have a mapping from this MEM's address to the
	     value (E).  Remove that, too.  */
	  addr = cselib_lookup (XEXP (x, 0), VOIDmode, 0);
	  mem_chain = &addr->addr_list;
	  for (;;)
	    {
	      if ((*mem_chain)->elt == v)
		{
		  unchain_one_elt_list (mem_chain);
		  break;
		}

	      mem_chain = &(*mem_chain)->next;
	    }

	  unchain_one_elt_loc_list (p);
	}

      if (had_locs && v->locs == 0)
	n_useless_values++;

      next = v->next_containing_mem;
      if (has_mem)
	{
	  *vp = v;
	  vp = &(*vp)->next_containing_mem;
	}
      else
	v->next_containing_mem = NULL;
    }
  *vp = &dummy_val;
}

/* Invalidate DEST, which is being assigned to or clobbered.  */

void
cselib_invalidate_rtx (rtx dest)
{
  while (GET_CODE (dest) == SUBREG
	 || GET_CODE (dest) == ZERO_EXTRACT
	 || GET_CODE (dest) == STRICT_LOW_PART)
    dest = XEXP (dest, 0);

  if (REG_P (dest))
    cselib_invalidate_regno (REGNO (dest), GET_MODE (dest));
  else if (MEM_P (dest))
    cselib_invalidate_mem (dest);

  /* Some machines don't define AUTO_INC_DEC, but they still use push
     instructions.  We need to catch that case here in order to
     invalidate the stack pointer correctly.  Note that invalidating
     the stack pointer is different from invalidating DEST.  */
  if (push_operand (dest, GET_MODE (dest)))
    cselib_invalidate_rtx (stack_pointer_rtx);
}

/* A wrapper for cselib_invalidate_rtx to be called via note_stores.  */

static void
cselib_invalidate_rtx_note_stores (rtx dest, rtx ignore ATTRIBUTE_UNUSED,
				   void *data ATTRIBUTE_UNUSED)
{
  cselib_invalidate_rtx (dest);
}

/* Record the result of a SET instruction.  DEST is being set; the source
   contains the value described by SRC_ELT.  If DEST is a MEM, DEST_ADDR_ELT
   describes its address.  */

static void
cselib_record_set (rtx dest, cselib_val *src_elt, cselib_val *dest_addr_elt)
{
  int dreg = REG_P (dest) ? (int) REGNO (dest) : -1;

  if (src_elt == 0 || side_effects_p (dest))
    return;

  if (dreg >= 0)
    {
      if (dreg < FIRST_PSEUDO_REGISTER)
	{
	  unsigned int n = hard_regno_nregs[dreg][GET_MODE (dest)];

	  if (n > max_value_regs)
	    max_value_regs = n;
	}

      if (REG_VALUES (dreg) == 0)
	{
	  used_regs[n_used_regs++] = dreg;
	  REG_VALUES (dreg) = new_elt_list (REG_VALUES (dreg), src_elt);
	}
      else
	{
	  /* The register should have been invalidated.  */
	  gcc_assert (REG_VALUES (dreg)->elt == 0);
	  REG_VALUES (dreg)->elt = src_elt;
	}

      if (src_elt->locs == 0)
	n_useless_values--;
      src_elt->locs = new_elt_loc_list (src_elt->locs, dest);
    }
  else if (MEM_P (dest) && dest_addr_elt != 0
	   && cselib_record_memory)
    {
      if (src_elt->locs == 0)
	n_useless_values--;
      add_mem_for_addr (dest_addr_elt, src_elt, dest);
    }
}

/* Describe a single set that is part of an insn.  */
struct set
{
  rtx src;
  rtx dest;
  cselib_val *src_elt;
  cselib_val *dest_addr_elt;
};

/* There is no good way to determine how many elements there can be
   in a PARALLEL.  Since it's fairly cheap, use a really large number.  */
#define MAX_SETS (FIRST_PSEUDO_REGISTER * 2)

/* Record the effects of any sets in INSN.  */
static void
cselib_record_sets (rtx insn)
{
  int n_sets = 0;
  int i;
  struct set sets[MAX_SETS];
  rtx body = PATTERN (insn);
  rtx cond = 0;

  body = PATTERN (insn);
  if (GET_CODE (body) == COND_EXEC)
    {
      cond = COND_EXEC_TEST (body);
      body = COND_EXEC_CODE (body);
    }

  /* Find all sets.  */
  if (GET_CODE (body) == SET)
    {
      sets[0].src = SET_SRC (body);
      sets[0].dest = SET_DEST (body);
      n_sets = 1;
    }
  else if (GET_CODE (body) == PARALLEL)
    {
      /* Look through the PARALLEL and record the values being
	 set, if possible.  Also handle any CLOBBERs.  */
      for (i = XVECLEN (body, 0) - 1; i >= 0; --i)
	{
	  rtx x = XVECEXP (body, 0, i);

	  if (GET_CODE (x) == SET)
	    {
	      sets[n_sets].src = SET_SRC (x);
	      sets[n_sets].dest = SET_DEST (x);
	      n_sets++;
	    }
	}
    }

  /* Look up the values that are read.  Do this before invalidating the
     locations that are written.  */
  for (i = 0; i < n_sets; i++)
    {
      rtx dest = sets[i].dest;

      /* A STRICT_LOW_PART can be ignored; we'll record the equivalence for
         the low part after invalidating any knowledge about larger modes.  */
      if (GET_CODE (sets[i].dest) == STRICT_LOW_PART)
	sets[i].dest = dest = XEXP (dest, 0);

      /* We don't know how to record anything but REG or MEM.  */
      if (REG_P (dest)
	  || (MEM_P (dest) && cselib_record_memory))
        {
	  rtx src = sets[i].src;
	  if (cond)
	    src = gen_rtx_IF_THEN_ELSE (GET_MODE (src), cond, src, dest);
	  sets[i].src_elt = cselib_lookup (src, GET_MODE (dest), 1);
	  if (MEM_P (dest))
	    sets[i].dest_addr_elt = cselib_lookup (XEXP (dest, 0), Pmode, 1);
	  else
	    sets[i].dest_addr_elt = 0;
	}
    }

  /* Invalidate all locations written by this insn.  Note that the elts we
     looked up in the previous loop aren't affected, just some of their
     locations may go away.  */
  note_stores (body, cselib_invalidate_rtx_note_stores, NULL);

  /* If this is an asm, look for duplicate sets.  This can happen when the
     user uses the same value as an output multiple times.  This is valid
     if the outputs are not actually used thereafter.  Treat this case as
     if the value isn't actually set.  We do this by smashing the destination
     to pc_rtx, so that we won't record the value later.  */
  if (n_sets >= 2 && asm_noperands (body) >= 0)
    {
      for (i = 0; i < n_sets; i++)
	{
	  rtx dest = sets[i].dest;
	  if (REG_P (dest) || MEM_P (dest))
	    {
	      int j;
	      for (j = i + 1; j < n_sets; j++)
		if (rtx_equal_p (dest, sets[j].dest))
		  {
		    sets[i].dest = pc_rtx;
		    sets[j].dest = pc_rtx;
		  }
	    }
	}
    }

  /* Now enter the equivalences in our tables.  */
  for (i = 0; i < n_sets; i++)
    {
      rtx dest = sets[i].dest;
      if (REG_P (dest)
	  || (MEM_P (dest) && cselib_record_memory))
	cselib_record_set (dest, sets[i].src_elt, sets[i].dest_addr_elt);
    }
}

/* Record the effects of INSN.  */

void
cselib_process_insn (rtx insn)
{
  int i;
  rtx x;

  if (find_reg_note (insn, REG_LIBCALL, NULL))
    cselib_current_insn_in_libcall = true;
  cselib_current_insn = insn;

  /* Forget everything at a CODE_LABEL, a volatile asm, or a setjmp.  */
  if (LABEL_P (insn)
      || (CALL_P (insn)
	  && find_reg_note (insn, REG_SETJMP, NULL))
      || (NONJUMP_INSN_P (insn)
	  && GET_CODE (PATTERN (insn)) == ASM_OPERANDS
	  && MEM_VOLATILE_P (PATTERN (insn))))
    {
      if (find_reg_note (insn, REG_RETVAL, NULL))
        cselib_current_insn_in_libcall = false;
      cselib_clear_table ();
      return;
    }

  if (! INSN_P (insn))
    {
      if (find_reg_note (insn, REG_RETVAL, NULL))
        cselib_current_insn_in_libcall = false;
      cselib_current_insn = 0;
      return;
    }

  /* If this is a call instruction, forget anything stored in a
     call clobbered register, or, if this is not a const call, in
     memory.  */
  if (CALL_P (insn))
    {
      for (i = 0; i < FIRST_PSEUDO_REGISTER; i++)
	if (call_used_regs[i]
	    || (REG_VALUES (i) && REG_VALUES (i)->elt
		&& HARD_REGNO_CALL_PART_CLOBBERED (i, 
		      GET_MODE (REG_VALUES (i)->elt->u.val_rtx))))
	  cselib_invalidate_regno (i, reg_raw_mode[i]);

      if (! CONST_OR_PURE_CALL_P (insn))
	cselib_invalidate_mem (callmem);
    }

  cselib_record_sets (insn);

#ifdef AUTO_INC_DEC
  /* Clobber any registers which appear in REG_INC notes.  We
     could keep track of the changes to their values, but it is
     unlikely to help.  */
  for (x = REG_NOTES (insn); x; x = XEXP (x, 1))
    if (REG_NOTE_KIND (x) == REG_INC)
      cselib_invalidate_rtx (XEXP (x, 0));
#endif

  /* Look for any CLOBBERs in CALL_INSN_FUNCTION_USAGE, but only
     after we have processed the insn.  */
  if (CALL_P (insn))
    for (x = CALL_INSN_FUNCTION_USAGE (insn); x; x = XEXP (x, 1))
      if (GET_CODE (XEXP (x, 0)) == CLOBBER)
	cselib_invalidate_rtx (XEXP (XEXP (x, 0), 0));

  if (find_reg_note (insn, REG_RETVAL, NULL))
    cselib_current_insn_in_libcall = false;
  cselib_current_insn = 0;

  if (n_useless_values > MAX_USELESS_VALUES
      /* remove_useless_values is linear in the hash table size.  Avoid
         quadratic behaviour for very large hashtables with very few
	 useless elements.  */
      && (unsigned int)n_useless_values > cselib_hash_table->n_elements / 4)
    remove_useless_values ();
}

/* Initialize cselib for one pass.  The caller must also call
   init_alias_analysis.  */

void
cselib_init (bool record_memory)
{
  elt_list_pool = create_alloc_pool ("elt_list", 
				     sizeof (struct elt_list), 10);
  elt_loc_list_pool = create_alloc_pool ("elt_loc_list", 
				         sizeof (struct elt_loc_list), 10);
  cselib_val_pool = create_alloc_pool ("cselib_val_list", 
				       sizeof (cselib_val), 10);
  value_pool = create_alloc_pool ("value", RTX_CODE_SIZE (VALUE), 100);
  cselib_record_memory = record_memory;
  /* This is only created once.  */
  if (! callmem)
    callmem = gen_rtx_MEM (BLKmode, const0_rtx);

  cselib_nregs = max_reg_num ();

  /* We preserve reg_values to allow expensive clearing of the whole thing.
     Reallocate it however if it happens to be too large.  */
  if (!reg_values || reg_values_size < cselib_nregs
      || (reg_values_size > 10 && reg_values_size > cselib_nregs * 4))
    {
      if (reg_values)
	free (reg_values);
      /* Some space for newly emit instructions so we don't end up
	 reallocating in between passes.  */
      reg_values_size = cselib_nregs + (63 + cselib_nregs) / 16;
      reg_values = XCNEWVEC (struct elt_list *, reg_values_size);
    }
  used_regs = XNEWVEC (unsigned int, cselib_nregs);
  n_used_regs = 0;
  cselib_hash_table = htab_create (31, get_value_hash,
				   entry_and_rtx_equal_p, NULL);
  cselib_current_insn_in_libcall = false;
}

/* Called when the current user is done with cselib.  */

void
cselib_finish (void)
{
  free_alloc_pool (elt_list_pool);
  free_alloc_pool (elt_loc_list_pool);
  free_alloc_pool (cselib_val_pool);
  free_alloc_pool (value_pool);
  cselib_clear_table ();
  htab_delete (cselib_hash_table);
  free (used_regs);
  used_regs = 0;
  cselib_hash_table = 0;
  n_useless_values = 0;
  next_unknown_value = 0;
}

#include "gt-cselib.h"
