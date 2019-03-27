/* ieee.c -- Read and write IEEE-695 debugging information.
   Copyright 1996, 1998, 1999, 2000, 2001, 2002, 2003, 2006, 2007
   Free Software Foundation, Inc.
   Written by Ian Lance Taylor <ian@cygnus.com>.

   This file is part of GNU Binutils.

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
   Foundation, Inc., 51 Franklin Street - Fifth Floor, Boston, MA
   02110-1301, USA.  */

/* This file reads and writes IEEE-695 debugging information.  */

#include "sysdep.h"
#include <assert.h>
#include "bfd.h"
#include "ieee.h"
#include "libiberty.h"
#include "debug.h"
#include "budbg.h"
#include "filenames.h"

/* This structure holds an entry on the block stack.  */

struct ieee_block
{
  /* The kind of block.  */
  int kind;
  /* The source file name, for a BB5 block.  */
  const char *filename;
  /* The index of the function type, for a BB4 or BB6 block.  */
  unsigned int fnindx;
  /* TRUE if this function is being skipped.  */
  bfd_boolean skip;
};

/* This structure is the block stack.  */

#define BLOCKSTACK_SIZE (16)

struct ieee_blockstack
{
  /* The stack pointer.  */
  struct ieee_block *bsp;
  /* The stack.  */
  struct ieee_block stack[BLOCKSTACK_SIZE];
};

/* This structure holds information for a variable.  */

struct ieee_var
{
  /* Start of name.  */
  const char *name;
  /* Length of name.  */
  unsigned long namlen;
  /* Type.  */
  debug_type type;
  /* Slot if we make an indirect type.  */
  debug_type *pslot;
  /* Kind of variable or function.  */
  enum
    {
      IEEE_UNKNOWN,
      IEEE_EXTERNAL,
      IEEE_GLOBAL,
      IEEE_STATIC,
      IEEE_LOCAL,
      IEEE_FUNCTION
    } kind;
};

/* This structure holds all the variables.  */

struct ieee_vars
{
  /* Number of slots allocated.  */
  unsigned int alloc;
  /* Variables.  */
  struct ieee_var *vars;
};

/* This structure holds information for a type.  We need this because
   we don't want to represent bitfields as real types.  */

struct ieee_type
{
  /* Type.  */
  debug_type type;
  /* Slot if this is type is referenced before it is defined.  */
  debug_type *pslot;
  /* Slots for arguments if we make indirect types for them.  */
  debug_type *arg_slots;
  /* If this is a bitfield, this is the size in bits.  If this is not
     a bitfield, this is zero.  */
  unsigned long bitsize;
};

/* This structure holds all the type information.  */

struct ieee_types
{
  /* Number of slots allocated.  */
  unsigned int alloc;
  /* Types.  */
  struct ieee_type *types;
  /* Builtin types.  */
#define BUILTIN_TYPE_COUNT (60)
  debug_type builtins[BUILTIN_TYPE_COUNT];
};

/* This structure holds a linked last of structs with their tag names,
   so that we can convert them to C++ classes if necessary.  */

struct ieee_tag
{
  /* Next tag.  */
  struct ieee_tag *next;
  /* This tag name.  */
  const char *name;
  /* The type of the tag.  */
  debug_type type;
  /* The tagged type is an indirect type pointing at this slot.  */
  debug_type slot;
  /* This is an array of slots used when a field type is converted
     into a indirect type, in case it needs to be later converted into
     a reference type.  */
  debug_type *fslots;
};

/* This structure holds the information we pass around to the parsing
   functions.  */

struct ieee_info
{
  /* The debugging handle.  */
  void *dhandle;
  /* The BFD.  */
  bfd *abfd;
  /* The start of the bytes to be parsed.  */
  const bfd_byte *bytes;
  /* The end of the bytes to be parsed.  */
  const bfd_byte *pend;
  /* The block stack.  */
  struct ieee_blockstack blockstack;
  /* Whether we have seen a BB1 or BB2.  */
  bfd_boolean saw_filename;
  /* The variables.  */
  struct ieee_vars vars;
  /* The global variables, after a global typedef block.  */
  struct ieee_vars *global_vars;
  /* The types.  */
  struct ieee_types types;
  /* The global types, after a global typedef block.  */
  struct ieee_types *global_types;
  /* The list of tagged structs.  */
  struct ieee_tag *tags;
};

/* Basic builtin types, not including the pointers.  */

enum builtin_types
{
  builtin_unknown = 0,
  builtin_void = 1,
  builtin_signed_char = 2,
  builtin_unsigned_char = 3,
  builtin_signed_short_int = 4,
  builtin_unsigned_short_int = 5,
  builtin_signed_long = 6,
  builtin_unsigned_long = 7,
  builtin_signed_long_long = 8,
  builtin_unsigned_long_long = 9,
  builtin_float = 10,
  builtin_double = 11,
  builtin_long_double = 12,
  builtin_long_long_double = 13,
  builtin_quoted_string = 14,
  builtin_instruction_address = 15,
  builtin_int = 16,
  builtin_unsigned = 17,
  builtin_unsigned_int = 18,
  builtin_char = 19,
  builtin_long = 20,
  builtin_short = 21,
  builtin_unsigned_short = 22,
  builtin_short_int = 23,
  builtin_signed_short = 24,
  builtin_bcd_float = 25
};

/* These are the values found in the derivation flags of a 'b'
   component record of a 'T' type extension record in a C++ pmisc
   record.  These are bitmasks.  */

/* Set for a private base class, clear for a public base class.
   Protected base classes are not supported.  */
#define BASEFLAGS_PRIVATE (0x1)
/* Set for a virtual base class.  */
#define BASEFLAGS_VIRTUAL (0x2)
/* Set for a friend class, clear for a base class.  */
#define BASEFLAGS_FRIEND (0x10)

/* These are the values found in the specs flags of a 'd', 'm', or 'v'
   component record of a 'T' type extension record in a C++ pmisc
   record.  The same flags are used for a 'M' record in a C++ pmisc
   record.  */

/* The lower two bits hold visibility information.  */
#define CXXFLAGS_VISIBILITY (0x3)
/* This value in the lower two bits indicates a public member.  */
#define CXXFLAGS_VISIBILITY_PUBLIC (0x0)
/* This value in the lower two bits indicates a private member.  */
#define CXXFLAGS_VISIBILITY_PRIVATE (0x1)
/* This value in the lower two bits indicates a protected member.  */
#define CXXFLAGS_VISIBILITY_PROTECTED (0x2)
/* Set for a static member.  */
#define CXXFLAGS_STATIC (0x4)
/* Set for a virtual override.  */
#define CXXFLAGS_OVERRIDE (0x8)
/* Set for a friend function.  */
#define CXXFLAGS_FRIEND (0x10)
/* Set for a const function.  */
#define CXXFLAGS_CONST (0x20)
/* Set for a volatile function.  */
#define CXXFLAGS_VOLATILE (0x40)
/* Set for an overloaded function.  */
#define CXXFLAGS_OVERLOADED (0x80)
/* Set for an operator function.  */
#define CXXFLAGS_OPERATOR (0x100)
/* Set for a constructor or destructor.  */
#define CXXFLAGS_CTORDTOR (0x400)
/* Set for a constructor.  */
#define CXXFLAGS_CTOR (0x200)
/* Set for an inline function.  */
#define CXXFLAGS_INLINE (0x800)

/* Local functions.  */

static void ieee_error (struct ieee_info *, const bfd_byte *, const char *);
static void ieee_eof (struct ieee_info *);
static char *savestring (const char *, unsigned long);
static bfd_boolean ieee_read_number
  (struct ieee_info *, const bfd_byte **, bfd_vma *);
static bfd_boolean ieee_read_optional_number
  (struct ieee_info *, const bfd_byte **, bfd_vma *, bfd_boolean *);
static bfd_boolean ieee_read_id
  (struct ieee_info *, const bfd_byte **, const char **, unsigned long *);
static bfd_boolean ieee_read_optional_id
  (struct ieee_info *, const bfd_byte **, const char **, unsigned long *,
   bfd_boolean *);
static bfd_boolean ieee_read_expression
  (struct ieee_info *, const bfd_byte **, bfd_vma *);
static debug_type ieee_builtin_type
  (struct ieee_info *, const bfd_byte *, unsigned int);
static bfd_boolean ieee_alloc_type
  (struct ieee_info *, unsigned int, bfd_boolean);
static bfd_boolean ieee_read_type_index
  (struct ieee_info *, const bfd_byte **, debug_type *);
static int ieee_regno_to_genreg (bfd *, int);
static int ieee_genreg_to_regno (bfd *, int);
static bfd_boolean parse_ieee_bb (struct ieee_info *, const bfd_byte **);
static bfd_boolean parse_ieee_be (struct ieee_info *, const bfd_byte **);
static bfd_boolean parse_ieee_nn (struct ieee_info *, const bfd_byte **);
static bfd_boolean parse_ieee_ty (struct ieee_info *, const bfd_byte **);
static bfd_boolean parse_ieee_atn (struct ieee_info *, const bfd_byte **);
static bfd_boolean ieee_read_cxx_misc
  (struct ieee_info *, const bfd_byte **, unsigned long);
static bfd_boolean ieee_read_cxx_class
  (struct ieee_info *, const bfd_byte **, unsigned long);
static bfd_boolean ieee_read_cxx_defaults
  (struct ieee_info *, const bfd_byte **, unsigned long);
static bfd_boolean ieee_read_reference
  (struct ieee_info *, const bfd_byte **);
static bfd_boolean ieee_require_asn
  (struct ieee_info *, const bfd_byte **, bfd_vma *);
static bfd_boolean ieee_require_atn65
  (struct ieee_info *, const bfd_byte **, const char **, unsigned long *);

/* Report an error in the IEEE debugging information.  */

static void
ieee_error (struct ieee_info *info, const bfd_byte *p, const char *s)
{
  if (p != NULL)
    fprintf (stderr, "%s: 0x%lx: %s (0x%x)\n", bfd_get_filename (info->abfd),
	     (unsigned long) (p - info->bytes), s, *p);
  else
    fprintf (stderr, "%s: %s\n", bfd_get_filename (info->abfd), s);
}

/* Report an unexpected EOF in the IEEE debugging information.  */

static void
ieee_eof (struct ieee_info *info)
{
  ieee_error (info, (const bfd_byte *) NULL,
	      _("unexpected end of debugging information"));
}

/* Save a string in memory.  */

static char *
savestring (const char *start, unsigned long len)
{
  char *ret;

  ret = (char *) xmalloc (len + 1);
  memcpy (ret, start, len);
  ret[len] = '\0';
  return ret;
}

/* Read a number which must be present in an IEEE file.  */

static bfd_boolean
ieee_read_number (struct ieee_info *info, const bfd_byte **pp, bfd_vma *pv)
{
  return ieee_read_optional_number (info, pp, pv, (bfd_boolean *) NULL);
}

/* Read a number in an IEEE file.  If ppresent is not NULL, the number
   need not be there.  */

static bfd_boolean
ieee_read_optional_number (struct ieee_info *info, const bfd_byte **pp,
			   bfd_vma *pv, bfd_boolean *ppresent)
{
  ieee_record_enum_type b;

  if (*pp >= info->pend)
    {
      if (ppresent != NULL)
	{
	  *ppresent = FALSE;
	  return TRUE;
	}
      ieee_eof (info);
      return FALSE;
    }

  b = (ieee_record_enum_type) **pp;
  ++*pp;

  if (b <= ieee_number_end_enum)
    {
      *pv = (bfd_vma) b;
      if (ppresent != NULL)
	*ppresent = TRUE;
      return TRUE;
    }

  if (b >= ieee_number_repeat_start_enum && b <= ieee_number_repeat_end_enum)
    {
      unsigned int i;

      i = (int) b - (int) ieee_number_repeat_start_enum;
      if (*pp + i - 1 >= info->pend)
	{
	  ieee_eof (info);
	  return FALSE;
	}

      *pv = 0;
      for (; i > 0; i--)
	{
	  *pv <<= 8;
	  *pv += **pp;
	  ++*pp;
	}

      if (ppresent != NULL)
	*ppresent = TRUE;

      return TRUE;
    }

  if (ppresent != NULL)
    {
      --*pp;
      *ppresent = FALSE;
      return TRUE;
    }

  ieee_error (info, *pp - 1, _("invalid number"));
  return FALSE;
}

/* Read a required string from an IEEE file.  */

static bfd_boolean
ieee_read_id (struct ieee_info *info, const bfd_byte **pp,
	      const char **pname, unsigned long *pnamlen)
{
  return ieee_read_optional_id (info, pp, pname, pnamlen, (bfd_boolean *) NULL);
}

/* Read a string from an IEEE file.  If ppresent is not NULL, the
   string is optional.  */

static bfd_boolean
ieee_read_optional_id (struct ieee_info *info, const bfd_byte **pp,
		       const char **pname, unsigned long *pnamlen,
		       bfd_boolean *ppresent)
{
  bfd_byte b;
  unsigned long len;

  if (*pp >= info->pend)
    {
      ieee_eof (info);
      return FALSE;
    }

  b = **pp;
  ++*pp;

  if (b <= 0x7f)
    len = b;
  else if ((ieee_record_enum_type) b == ieee_extension_length_1_enum)
    {
      len = **pp;
      ++*pp;
    }
  else if ((ieee_record_enum_type) b == ieee_extension_length_2_enum)
    {
      len = (**pp << 8) + (*pp)[1];
      *pp += 2;
    }
  else
    {
      if (ppresent != NULL)
	{
	  --*pp;
	  *ppresent = FALSE;
	  return TRUE;
	}
      ieee_error (info, *pp - 1, _("invalid string length"));
      return FALSE;
    }

  if ((unsigned long) (info->pend - *pp) < len)
    {
      ieee_eof (info);
      return FALSE;
    }

  *pname = (const char *) *pp;
  *pnamlen = len;
  *pp += len;

  if (ppresent != NULL)
    *ppresent = TRUE;

  return TRUE;
}

/* Read an expression from an IEEE file.  Since this code is only used
   to parse debugging information, I haven't bothered to write a full
   blown IEEE expression parser.  I've only thrown in the things I've
   seen in debugging information.  This can be easily extended if
   necessary.  */

static bfd_boolean
ieee_read_expression (struct ieee_info *info, const bfd_byte **pp,
		      bfd_vma *pv)
{
  const bfd_byte *expr_start;
#define EXPR_STACK_SIZE (10)
  bfd_vma expr_stack[EXPR_STACK_SIZE];
  bfd_vma *esp;

  expr_start = *pp;

  esp = expr_stack;

  while (1)
    {
      const bfd_byte *start;
      bfd_vma val;
      bfd_boolean present;
      ieee_record_enum_type c;

      start = *pp;

      if (! ieee_read_optional_number (info, pp, &val, &present))
	return FALSE;

      if (present)
	{
	  if (esp - expr_stack >= EXPR_STACK_SIZE)
	    {
	      ieee_error (info, start, _("expression stack overflow"));
	      return FALSE;
	    }
	  *esp++ = val;
	  continue;
	}

      c = (ieee_record_enum_type) **pp;

      if (c >= ieee_module_beginning_enum)
	break;

      ++*pp;

      if (c == ieee_comma)
	break;

      switch (c)
	{
	default:
	  ieee_error (info, start, _("unsupported IEEE expression operator"));
	  break;

	case ieee_variable_R_enum:
	  {
	    bfd_vma indx;
	    asection *s;

	    if (! ieee_read_number (info, pp, &indx))
	      return FALSE;
	    for (s = info->abfd->sections; s != NULL; s = s->next)
	      if ((bfd_vma) s->target_index == indx)
		break;
	    if (s == NULL)
	      {
		ieee_error (info, start, _("unknown section"));
		return FALSE;
	      }

	    if (esp - expr_stack >= EXPR_STACK_SIZE)
	      {
		ieee_error (info, start, _("expression stack overflow"));
		return FALSE;
	      }

	    *esp++ = bfd_get_section_vma (info->abfd, s);
	  }
	  break;

	case ieee_function_plus_enum:
	case ieee_function_minus_enum:
	  {
	    bfd_vma v1, v2;

	    if (esp - expr_stack < 2)
	      {
		ieee_error (info, start, _("expression stack underflow"));
		return FALSE;
	      }

	    v1 = *--esp;
	    v2 = *--esp;
	    *esp++ = v1 + v2;
	  }
	  break;
	}
    }

  if (esp - 1 != expr_stack)
    {
      ieee_error (info, expr_start, _("expression stack mismatch"));
      return FALSE;
    }

  *pv = *--esp;

  return TRUE;
}

/* Return an IEEE builtin type.  */

static debug_type
ieee_builtin_type (struct ieee_info *info, const bfd_byte *p,
		   unsigned int indx)
{
  void *dhandle;
  debug_type type;
  const char *name;

  if (indx < BUILTIN_TYPE_COUNT
      && info->types.builtins[indx] != DEBUG_TYPE_NULL)
    return info->types.builtins[indx];

  dhandle = info->dhandle;

  if (indx >= 32 && indx < 64)
    {
      type = debug_make_pointer_type (dhandle,
				      ieee_builtin_type (info, p, indx - 32));
      assert (indx < BUILTIN_TYPE_COUNT);
      info->types.builtins[indx] = type;
      return type;
    }

  switch ((enum builtin_types) indx)
    {
    default:
      ieee_error (info, p, _("unknown builtin type"));
      return NULL;

    case builtin_unknown:
      type = debug_make_void_type (dhandle);
      name = NULL;
      break;

    case builtin_void:
      type = debug_make_void_type (dhandle);
      name = "void";
      break;

    case builtin_signed_char:
      type = debug_make_int_type (dhandle, 1, FALSE);
      name = "signed char";
      break;

    case builtin_unsigned_char:
      type = debug_make_int_type (dhandle, 1, TRUE);
      name = "unsigned char";
      break;

    case builtin_signed_short_int:
      type = debug_make_int_type (dhandle, 2, FALSE);
      name = "signed short int";
      break;

    case builtin_unsigned_short_int:
      type = debug_make_int_type (dhandle, 2, TRUE);
      name = "unsigned short int";
      break;

    case builtin_signed_long:
      type = debug_make_int_type (dhandle, 4, FALSE);
      name = "signed long";
      break;

    case builtin_unsigned_long:
      type = debug_make_int_type (dhandle, 4, TRUE);
      name = "unsigned long";
      break;

    case builtin_signed_long_long:
      type = debug_make_int_type (dhandle, 8, FALSE);
      name = "signed long long";
      break;

    case builtin_unsigned_long_long:
      type = debug_make_int_type (dhandle, 8, TRUE);
      name = "unsigned long long";
      break;

    case builtin_float:
      type = debug_make_float_type (dhandle, 4);
      name = "float";
      break;

    case builtin_double:
      type = debug_make_float_type (dhandle, 8);
      name = "double";
      break;

    case builtin_long_double:
      /* FIXME: The size for this type should depend upon the
         processor.  */
      type = debug_make_float_type (dhandle, 12);
      name = "long double";
      break;

    case builtin_long_long_double:
      type = debug_make_float_type (dhandle, 16);
      name = "long long double";
      break;

    case builtin_quoted_string:
      type = debug_make_array_type (dhandle,
				    ieee_builtin_type (info, p,
						       ((unsigned int)
							builtin_char)),
				    ieee_builtin_type (info, p,
						       ((unsigned int)
							builtin_int)),
				    0, -1, TRUE);
      name = "QUOTED STRING";
      break;

    case builtin_instruction_address:
      /* FIXME: This should be a code address.  */
      type = debug_make_int_type (dhandle, 4, TRUE);
      name = "instruction address";
      break;

    case builtin_int:
      /* FIXME: The size for this type should depend upon the
         processor.  */
      type = debug_make_int_type (dhandle, 4, FALSE);
      name = "int";
      break;

    case builtin_unsigned:
      /* FIXME: The size for this type should depend upon the
         processor.  */
      type = debug_make_int_type (dhandle, 4, TRUE);
      name = "unsigned";
      break;

    case builtin_unsigned_int:
      /* FIXME: The size for this type should depend upon the
         processor.  */
      type = debug_make_int_type (dhandle, 4, TRUE);
      name = "unsigned int";
      break;

    case builtin_char:
      type = debug_make_int_type (dhandle, 1, FALSE);
      name = "char";
      break;

    case builtin_long:
      type = debug_make_int_type (dhandle, 4, FALSE);
      name = "long";
      break;

    case builtin_short:
      type = debug_make_int_type (dhandle, 2, FALSE);
      name = "short";
      break;

    case builtin_unsigned_short:
      type = debug_make_int_type (dhandle, 2, TRUE);
      name = "unsigned short";
      break;

    case builtin_short_int:
      type = debug_make_int_type (dhandle, 2, FALSE);
      name = "short int";
      break;

    case builtin_signed_short:
      type = debug_make_int_type (dhandle, 2, FALSE);
      name = "signed short";
      break;

    case builtin_bcd_float:
      ieee_error (info, p, _("BCD float type not supported"));
      return DEBUG_TYPE_NULL;
    }

  if (name != NULL)
    type = debug_name_type (dhandle, name, type);

  assert (indx < BUILTIN_TYPE_COUNT);

  info->types.builtins[indx] = type;

  return type;
}

/* Allocate more space in the type table.  If ref is TRUE, this is a
   reference to the type; if it is not already defined, we should set
   up an indirect type.  */

static bfd_boolean
ieee_alloc_type (struct ieee_info *info, unsigned int indx, bfd_boolean ref)
{
  unsigned int nalloc;
  register struct ieee_type *t;
  struct ieee_type *tend;

  if (indx >= info->types.alloc)
    {
      nalloc = info->types.alloc;
      if (nalloc == 0)
	nalloc = 4;
      while (indx >= nalloc)
	nalloc *= 2;

      info->types.types = ((struct ieee_type *)
			   xrealloc (info->types.types,
				     nalloc * sizeof *info->types.types));

      memset (info->types.types + info->types.alloc, 0,
	      (nalloc - info->types.alloc) * sizeof *info->types.types);

      tend = info->types.types + nalloc;
      for (t = info->types.types + info->types.alloc; t < tend; t++)
	t->type = DEBUG_TYPE_NULL;

      info->types.alloc = nalloc;
    }

  if (ref)
    {
      t = info->types.types + indx;
      if (t->type == NULL)
	{
	  t->pslot = (debug_type *) xmalloc (sizeof *t->pslot);
	  *t->pslot = DEBUG_TYPE_NULL;
	  t->type = debug_make_indirect_type (info->dhandle, t->pslot,
					      (const char *) NULL);
	  if (t->type == NULL)
	    return FALSE;
	}
    }

  return TRUE;
}

/* Read a type index and return the corresponding type.  */

static bfd_boolean
ieee_read_type_index (struct ieee_info *info, const bfd_byte **pp,
		      debug_type *ptype)
{
  const bfd_byte *start;
  bfd_vma indx;

  start = *pp;

  if (! ieee_read_number (info, pp, &indx))
    return FALSE;

  if (indx < 256)
    {
      *ptype = ieee_builtin_type (info, start, indx);
      if (*ptype == NULL)
	return FALSE;
      return TRUE;
    }

  indx -= 256;
  if (! ieee_alloc_type (info, indx, TRUE))
    return FALSE;

  *ptype = info->types.types[indx].type;

  return TRUE;
}

/* Parse IEEE debugging information for a file.  This is passed the
   bytes which compose the Debug Information Part of an IEEE file.  */

bfd_boolean
parse_ieee (void *dhandle, bfd *abfd, const bfd_byte *bytes, bfd_size_type len)
{
  struct ieee_info info;
  unsigned int i;
  const bfd_byte *p, *pend;

  info.dhandle = dhandle;
  info.abfd = abfd;
  info.bytes = bytes;
  info.pend = bytes + len;
  info.blockstack.bsp = info.blockstack.stack;
  info.saw_filename = FALSE;
  info.vars.alloc = 0;
  info.vars.vars = NULL;
  info.global_vars = NULL;
  info.types.alloc = 0;
  info.types.types = NULL;
  info.global_types = NULL;
  info.tags = NULL;
  for (i = 0; i < BUILTIN_TYPE_COUNT; i++)
    info.types.builtins[i] = DEBUG_TYPE_NULL;

  p = bytes;
  pend = info.pend;
  while (p < pend)
    {
      const bfd_byte *record_start;
      ieee_record_enum_type c;

      record_start = p;

      c = (ieee_record_enum_type) *p++;

      if (c == ieee_at_record_enum)
	c = (ieee_record_enum_type) (((unsigned int) c << 8) | *p++);

      if (c <= ieee_number_repeat_end_enum)
	{
	  ieee_error (&info, record_start, _("unexpected number"));
	  return FALSE;
	}

      switch (c)
	{
	default:
	  ieee_error (&info, record_start, _("unexpected record type"));
	  return FALSE;

	case ieee_bb_record_enum:
	  if (! parse_ieee_bb (&info, &p))
	    return FALSE;
	  break;

	case ieee_be_record_enum:
	  if (! parse_ieee_be (&info, &p))
	    return FALSE;
	  break;

	case ieee_nn_record:
	  if (! parse_ieee_nn (&info, &p))
	    return FALSE;
	  break;

	case ieee_ty_record_enum:
	  if (! parse_ieee_ty (&info, &p))
	    return FALSE;
	  break;

	case ieee_atn_record_enum:
	  if (! parse_ieee_atn (&info, &p))
	    return FALSE;
	  break;
	}
    }

  if (info.blockstack.bsp != info.blockstack.stack)
    {
      ieee_error (&info, (const bfd_byte *) NULL,
		  _("blocks left on stack at end"));
      return FALSE;
    }

  return TRUE;
}

/* Handle an IEEE BB record.  */

static bfd_boolean
parse_ieee_bb (struct ieee_info *info, const bfd_byte **pp)
{
  const bfd_byte *block_start;
  bfd_byte b;
  bfd_vma size;
  const char *name;
  unsigned long namlen;
  char *namcopy = NULL;
  unsigned int fnindx;
  bfd_boolean skip;

  block_start = *pp;

  b = **pp;
  ++*pp;

  if (! ieee_read_number (info, pp, &size)
      || ! ieee_read_id (info, pp, &name, &namlen))
    return FALSE;

  fnindx = (unsigned int) -1;
  skip = FALSE;

  switch (b)
    {
    case 1:
      /* BB1: Type definitions local to a module.  */
      namcopy = savestring (name, namlen);
      if (namcopy == NULL)
	return FALSE;
      if (! debug_set_filename (info->dhandle, namcopy))
	return FALSE;
      info->saw_filename = TRUE;

      /* Discard any variables or types we may have seen before.  */
      if (info->vars.vars != NULL)
	free (info->vars.vars);
      info->vars.vars = NULL;
      info->vars.alloc = 0;
      if (info->types.types != NULL)
	free (info->types.types);
      info->types.types = NULL;
      info->types.alloc = 0;

      /* Initialize the types to the global types.  */
      if (info->global_types != NULL)
	{
	  info->types.alloc = info->global_types->alloc;
	  info->types.types = ((struct ieee_type *)
			       xmalloc (info->types.alloc
					* sizeof (*info->types.types)));
	  memcpy (info->types.types, info->global_types->types,
		  info->types.alloc * sizeof (*info->types.types));
	}

      break;

    case 2:
      /* BB2: Global type definitions.  The name is supposed to be
	 empty, but we don't check.  */
      if (! debug_set_filename (info->dhandle, "*global*"))
	return FALSE;
      info->saw_filename = TRUE;
      break;

    case 3:
      /* BB3: High level module block begin.  We don't have to do
	 anything here.  The name is supposed to be the same as for
	 the BB1, but we don't check.  */
      break;

    case 4:
      /* BB4: Global function.  */
      {
	bfd_vma stackspace, typindx, offset;
	debug_type return_type;

	if (! ieee_read_number (info, pp, &stackspace)
	    || ! ieee_read_number (info, pp, &typindx)
	    || ! ieee_read_expression (info, pp, &offset))
	  return FALSE;

	/* We have no way to record the stack space.  FIXME.  */

	if (typindx < 256)
	  {
	    return_type = ieee_builtin_type (info, block_start, typindx);
	    if (return_type == DEBUG_TYPE_NULL)
	      return FALSE;
	  }
	else
	  {
	    typindx -= 256;
	    if (! ieee_alloc_type (info, typindx, TRUE))
	      return FALSE;
	    fnindx = typindx;
	    return_type = info->types.types[typindx].type;
	    if (debug_get_type_kind (info->dhandle, return_type)
		== DEBUG_KIND_FUNCTION)
	      return_type = debug_get_return_type (info->dhandle,
						   return_type);
	  }

	namcopy = savestring (name, namlen);
	if (namcopy == NULL)
	  return FALSE;
	if (! debug_record_function (info->dhandle, namcopy, return_type,
				     TRUE, offset))
	  return FALSE;
      }
      break;

    case 5:
      /* BB5: File name for source line numbers.  */
      {
	unsigned int i;

	/* We ignore the date and time.  FIXME.  */
	for (i = 0; i < 6; i++)
	  {
	    bfd_vma ignore;
	    bfd_boolean present;

	    if (! ieee_read_optional_number (info, pp, &ignore, &present))
	      return FALSE;
	    if (! present)
	      break;
	  }

	namcopy = savestring (name, namlen);
	if (namcopy == NULL)
	  return FALSE;
	if (! debug_start_source (info->dhandle, namcopy))
	  return FALSE;
      }
      break;

    case 6:
      /* BB6: Local function or block.  */
      {
	bfd_vma stackspace, typindx, offset;

	if (! ieee_read_number (info, pp, &stackspace)
	    || ! ieee_read_number (info, pp, &typindx)
	    || ! ieee_read_expression (info, pp, &offset))
	  return FALSE;

	/* We have no way to record the stack space.  FIXME.  */

	if (namlen == 0)
	  {
	    if (! debug_start_block (info->dhandle, offset))
	      return FALSE;
	    /* Change b to indicate that this is a block
	       rather than a function.  */
	    b = 0x86;
	  }
	else
	  {
	    /* The MRI C++ compiler will output a fake function named
	       __XRYCPP to hold C++ debugging information.  We skip
	       that function.  This is not crucial, but it makes
	       converting from IEEE to other debug formats work
	       better.  */
	    if (strncmp (name, "__XRYCPP", namlen) == 0)
	      skip = TRUE;
	    else
	      {
		debug_type return_type;

		if (typindx < 256)
		  {
		    return_type = ieee_builtin_type (info, block_start,
						     typindx);
		    if (return_type == NULL)
		      return FALSE;
		  }
		else
		  {
		    typindx -= 256;
		    if (! ieee_alloc_type (info, typindx, TRUE))
		      return FALSE;
		    fnindx = typindx;
		    return_type = info->types.types[typindx].type;
		    if (debug_get_type_kind (info->dhandle, return_type)
			== DEBUG_KIND_FUNCTION)
		      return_type = debug_get_return_type (info->dhandle,
							   return_type);
		  }

		namcopy = savestring (name, namlen);
		if (namcopy == NULL)
		  return FALSE;
		if (! debug_record_function (info->dhandle, namcopy,
					     return_type, FALSE, offset))
		  return FALSE;
	      }
	  }
      }
      break;

    case 10:
      /* BB10: Assembler module scope.  In the normal case, we
	 completely ignore all this information.  FIXME.  */
      {
	const char *inam, *vstr;
	unsigned long inamlen, vstrlen;
	bfd_vma tool_type;
	bfd_boolean present;
	unsigned int i;

	if (! info->saw_filename)
	  {
	    namcopy = savestring (name, namlen);
	    if (namcopy == NULL)
	      return FALSE;
	    if (! debug_set_filename (info->dhandle, namcopy))
	      return FALSE;
	    info->saw_filename = TRUE;
	  }

	if (! ieee_read_id (info, pp, &inam, &inamlen)
	    || ! ieee_read_number (info, pp, &tool_type)
	    || ! ieee_read_optional_id (info, pp, &vstr, &vstrlen, &present))
	  return FALSE;
	for (i = 0; i < 6; i++)
	  {
	    bfd_vma ignore;

	    if (! ieee_read_optional_number (info, pp, &ignore, &present))
	      return FALSE;
	    if (! present)
	      break;
	  }
      }
      break;

    case 11:
      /* BB11: Module section.  We completely ignore all this
	 information.  FIXME.  */
      {
	bfd_vma sectype, secindx, offset, map;
	bfd_boolean present;

	if (! ieee_read_number (info, pp, &sectype)
	    || ! ieee_read_number (info, pp, &secindx)
	    || ! ieee_read_expression (info, pp, &offset)
	    || ! ieee_read_optional_number (info, pp, &map, &present))
	  return FALSE;
      }
      break;

    default:
      ieee_error (info, block_start, _("unknown BB type"));
      return FALSE;
    }


  /* Push this block on the block stack.  */

  if (info->blockstack.bsp >= info->blockstack.stack + BLOCKSTACK_SIZE)
    {
      ieee_error (info, (const bfd_byte *) NULL, _("stack overflow"));
      return FALSE;
    }

  info->blockstack.bsp->kind = b;
  if (b == 5)
    info->blockstack.bsp->filename = namcopy;
  info->blockstack.bsp->fnindx = fnindx;
  info->blockstack.bsp->skip = skip;
  ++info->blockstack.bsp;

  return TRUE;
}

/* Handle an IEEE BE record.  */

static bfd_boolean
parse_ieee_be (struct ieee_info *info, const bfd_byte **pp)
{
  bfd_vma offset;

  if (info->blockstack.bsp <= info->blockstack.stack)
    {
      ieee_error (info, *pp, _("stack underflow"));
      return FALSE;
    }
  --info->blockstack.bsp;

  switch (info->blockstack.bsp->kind)
    {
    case 2:
      /* When we end the global typedefs block, we copy out the
         contents of info->vars.  This is because the variable indices
         may be reused in the local blocks.  However, we need to
         preserve them so that we can locate a function returning a
         reference variable whose type is named in the global typedef
         block.  */
      info->global_vars = ((struct ieee_vars *)
			   xmalloc (sizeof *info->global_vars));
      info->global_vars->alloc = info->vars.alloc;
      info->global_vars->vars = ((struct ieee_var *)
				 xmalloc (info->vars.alloc
					  * sizeof (*info->vars.vars)));
      memcpy (info->global_vars->vars, info->vars.vars,
	      info->vars.alloc * sizeof (*info->vars.vars));

      /* We also copy out the non builtin parts of info->types, since
         the types are discarded when we start a new block.  */
      info->global_types = ((struct ieee_types *)
			    xmalloc (sizeof *info->global_types));
      info->global_types->alloc = info->types.alloc;
      info->global_types->types = ((struct ieee_type *)
				   xmalloc (info->types.alloc
					    * sizeof (*info->types.types)));
      memcpy (info->global_types->types, info->types.types,
	      info->types.alloc * sizeof (*info->types.types));
      memset (info->global_types->builtins, 0,
	      sizeof (info->global_types->builtins));

      break;

    case 4:
    case 6:
      if (! ieee_read_expression (info, pp, &offset))
	return FALSE;
      if (! info->blockstack.bsp->skip)
	{
	  if (! debug_end_function (info->dhandle, offset + 1))
	    return FALSE;
	}
      break;

    case 0x86:
      /* This is BE6 when BB6 started a block rather than a local
	 function.  */
      if (! ieee_read_expression (info, pp, &offset))
	return FALSE;
      if (! debug_end_block (info->dhandle, offset + 1))
	return FALSE;
      break;

    case 5:
      /* When we end a BB5, we look up the stack for the last BB5, if
         there is one, so that we can call debug_start_source.  */
      if (info->blockstack.bsp > info->blockstack.stack)
	{
	  struct ieee_block *bl;

	  bl = info->blockstack.bsp;
	  do
	    {
	      --bl;
	      if (bl->kind == 5)
		{
		  if (! debug_start_source (info->dhandle, bl->filename))
		    return FALSE;
		  break;
		}
	    }
	  while (bl != info->blockstack.stack);
	}
      break;

    case 11:
      if (! ieee_read_expression (info, pp, &offset))
	return FALSE;
      /* We just ignore the module size.  FIXME.  */
      break;

    default:
      /* Other block types do not have any trailing information.  */
      break;
    }

  return TRUE;
}

/* Parse an NN record.  */

static bfd_boolean
parse_ieee_nn (struct ieee_info *info, const bfd_byte **pp)
{
  const bfd_byte *nn_start;
  bfd_vma varindx;
  const char *name;
  unsigned long namlen;

  nn_start = *pp;

  if (! ieee_read_number (info, pp, &varindx)
      || ! ieee_read_id (info, pp, &name, &namlen))
    return FALSE;

  if (varindx < 32)
    {
      ieee_error (info, nn_start, _("illegal variable index"));
      return FALSE;
    }
  varindx -= 32;

  if (varindx >= info->vars.alloc)
    {
      unsigned int alloc;

      alloc = info->vars.alloc;
      if (alloc == 0)
	alloc = 4;
      while (varindx >= alloc)
	alloc *= 2;
      info->vars.vars = ((struct ieee_var *)
			 xrealloc (info->vars.vars,
				   alloc * sizeof *info->vars.vars));
      memset (info->vars.vars + info->vars.alloc, 0,
	      (alloc - info->vars.alloc) * sizeof *info->vars.vars);
      info->vars.alloc = alloc;
    }

  info->vars.vars[varindx].name = name;
  info->vars.vars[varindx].namlen = namlen;

  return TRUE;
}

/* Parse a TY record.  */

static bfd_boolean
parse_ieee_ty (struct ieee_info *info, const bfd_byte **pp)
{
  const bfd_byte *ty_start, *ty_var_start, *ty_code_start;
  bfd_vma typeindx, varindx, tc;
  void *dhandle;
  bfd_boolean tag, typdef;
  debug_type *arg_slots;
  unsigned long type_bitsize;
  debug_type type;

  ty_start = *pp;

  if (! ieee_read_number (info, pp, &typeindx))
    return FALSE;

  if (typeindx < 256)
    {
      ieee_error (info, ty_start, _("illegal type index"));
      return FALSE;
    }

  typeindx -= 256;
  if (! ieee_alloc_type (info, typeindx, FALSE))
    return FALSE;

  if (**pp != 0xce)
    {
      ieee_error (info, *pp, _("unknown TY code"));
      return FALSE;
    }
  ++*pp;

  ty_var_start = *pp;

  if (! ieee_read_number (info, pp, &varindx))
    return FALSE;

  if (varindx < 32)
    {
      ieee_error (info, ty_var_start, _("illegal variable index"));
      return FALSE;
    }
  varindx -= 32;

  if (varindx >= info->vars.alloc || info->vars.vars[varindx].name == NULL)
    {
      ieee_error (info, ty_var_start, _("undefined variable in TY"));
      return FALSE;
    }

  ty_code_start = *pp;

  if (! ieee_read_number (info, pp, &tc))
    return FALSE;

  dhandle = info->dhandle;

  tag = FALSE;
  typdef = FALSE;
  arg_slots = NULL;
  type_bitsize = 0;
  switch (tc)
    {
    default:
      ieee_error (info, ty_code_start, _("unknown TY code"));
      return FALSE;

    case '!':
      /* Unknown type, with size.  We treat it as int.  FIXME.  */
      {
	bfd_vma size;

	if (! ieee_read_number (info, pp, &size))
	  return FALSE;
	type = debug_make_int_type (dhandle, size, FALSE);
      }
      break;

    case 'A': /* Array.  */
    case 'a': /* FORTRAN array in column/row order.  FIXME: Not
		 distinguished from normal array.  */
      {
	debug_type ele_type;
	bfd_vma lower, upper;

	if (! ieee_read_type_index (info, pp, &ele_type)
	    || ! ieee_read_number (info, pp, &lower)
	    || ! ieee_read_number (info, pp, &upper))
	  return FALSE;
	type = debug_make_array_type (dhandle, ele_type,
				      ieee_builtin_type (info, ty_code_start,
							 ((unsigned int)
							  builtin_int)),
				      (bfd_signed_vma) lower,
				      (bfd_signed_vma) upper,
				      FALSE);
      }
      break;

    case 'E':
      /* Simple enumeration.  */
      {
	bfd_vma size;
	unsigned int alloc;
	const char **names;
	unsigned int c;
	bfd_signed_vma *vals;
	unsigned int i;

	if (! ieee_read_number (info, pp, &size))
	  return FALSE;
	/* FIXME: we ignore the enumeration size.  */

	alloc = 10;
	names = (const char **) xmalloc (alloc * sizeof *names);
	memset (names, 0, alloc * sizeof *names);
	c = 0;
	while (1)
	  {
	    const char *name;
	    unsigned long namlen;
	    bfd_boolean present;

	    if (! ieee_read_optional_id (info, pp, &name, &namlen, &present))
	      return FALSE;
	    if (! present)
	      break;

	    if (c + 1 >= alloc)
	      {
		alloc += 10;
		names = ((const char **)
			 xrealloc (names, alloc * sizeof *names));
	      }

	    names[c] = savestring (name, namlen);
	    if (names[c] == NULL)
	      return FALSE;
	    ++c;
	  }

	names[c] = NULL;

	vals = (bfd_signed_vma *) xmalloc (c * sizeof *vals);
	for (i = 0; i < c; i++)
	  vals[i] = i;

	type = debug_make_enum_type (dhandle, names, vals);
	tag = TRUE;
      }
      break;

    case 'G':
      /* Struct with bit fields.  */
      {
	bfd_vma size;
	unsigned int alloc;
	debug_field *fields;
	unsigned int c;

	if (! ieee_read_number (info, pp, &size))
	  return FALSE;

	alloc = 10;
	fields = (debug_field *) xmalloc (alloc * sizeof *fields);
	c = 0;
	while (1)
	  {
	    const char *name;
	    unsigned long namlen;
	    bfd_boolean present;
	    debug_type ftype;
	    bfd_vma bitpos, bitsize;

	    if (! ieee_read_optional_id (info, pp, &name, &namlen, &present))
	      return FALSE;
	    if (! present)
	      break;
	    if (! ieee_read_type_index (info, pp, &ftype)
		|| ! ieee_read_number (info, pp, &bitpos)
		|| ! ieee_read_number (info, pp, &bitsize))
	      return FALSE;

	    if (c + 1 >= alloc)
	      {
		alloc += 10;
		fields = ((debug_field *)
			  xrealloc (fields, alloc * sizeof *fields));
	      }

	    fields[c] = debug_make_field (dhandle, savestring (name, namlen),
					  ftype, bitpos, bitsize,
					  DEBUG_VISIBILITY_PUBLIC);
	    if (fields[c] == NULL)
	      return FALSE;
	    ++c;
	  }

	fields[c] = NULL;

	type = debug_make_struct_type (dhandle, TRUE, size, fields);
	tag = TRUE;
      }
      break;

    case 'N':
      /* Enumeration.  */
      {
	unsigned int alloc;
	const char **names;
	bfd_signed_vma *vals;
	unsigned int c;

	alloc = 10;
	names = (const char **) xmalloc (alloc * sizeof *names);
	vals = (bfd_signed_vma *) xmalloc (alloc * sizeof *names);
	c = 0;
	while (1)
	  {
	    const char *name;
	    unsigned long namlen;
	    bfd_boolean present;
	    bfd_vma val;

	    if (! ieee_read_optional_id (info, pp, &name, &namlen, &present))
	      return FALSE;
	    if (! present)
	      break;
	    if (! ieee_read_number (info, pp, &val))
	      return FALSE;

	    /* If the length of the name is zero, then the value is
               actually the size of the enum.  We ignore this
               information.  FIXME.  */
	    if (namlen == 0)
	      continue;

	    if (c + 1 >= alloc)
	      {
		alloc += 10;
		names = ((const char **)
			 xrealloc (names, alloc * sizeof *names));
		vals = ((bfd_signed_vma *)
			xrealloc (vals, alloc * sizeof *vals));
	      }

	    names[c] = savestring (name, namlen);
	    if (names[c] == NULL)
	      return FALSE;
	    vals[c] = (bfd_signed_vma) val;
	    ++c;
	  }

	names[c] = NULL;

	type = debug_make_enum_type (dhandle, names, vals);
	tag = TRUE;
      }
      break;

    case 'O': /* Small pointer.  We don't distinguish small and large
		 pointers.  FIXME.  */
    case 'P': /* Large pointer.  */
      {
	debug_type t;

	if (! ieee_read_type_index (info, pp, &t))
	  return FALSE;
	type = debug_make_pointer_type (dhandle, t);
      }
      break;

    case 'R':
      /* Range.  */
      {
	bfd_vma low, high, signedp, size;

	if (! ieee_read_number (info, pp, &low)
	    || ! ieee_read_number (info, pp, &high)
	    || ! ieee_read_number (info, pp, &signedp)
	    || ! ieee_read_number (info, pp, &size))
	  return FALSE;

	type = debug_make_range_type (dhandle,
				      debug_make_int_type (dhandle, size,
							   ! signedp),
				      (bfd_signed_vma) low,
				      (bfd_signed_vma) high);
      }
      break;

    case 'S': /* Struct.  */
    case 'U': /* Union.  */
      {
	bfd_vma size;
	unsigned int alloc;
	debug_field *fields;
	unsigned int c;

	if (! ieee_read_number (info, pp, &size))
	  return FALSE;

	alloc = 10;
	fields = (debug_field *) xmalloc (alloc * sizeof *fields);
	c = 0;
	while (1)
	  {
	    const char *name;
	    unsigned long namlen;
	    bfd_boolean present;
	    bfd_vma tindx;
	    bfd_vma offset;
	    debug_type ftype;
	    bfd_vma bitsize;

	    if (! ieee_read_optional_id (info, pp, &name, &namlen, &present))
	      return FALSE;
	    if (! present)
	      break;
	    if (! ieee_read_number (info, pp, &tindx)
		|| ! ieee_read_number (info, pp, &offset))
	      return FALSE;

	    if (tindx < 256)
	      {
		ftype = ieee_builtin_type (info, ty_code_start, tindx);
		bitsize = 0;
		offset *= 8;
	      }
	    else
	      {
		struct ieee_type *t;

		tindx -= 256;
		if (! ieee_alloc_type (info, tindx, TRUE))
		  return FALSE;
		t = info->types.types + tindx;
		ftype = t->type;
		bitsize = t->bitsize;
		if (bitsize == 0)
		  offset *= 8;
	      }

	    if (c + 1 >= alloc)
	      {
		alloc += 10;
		fields = ((debug_field *)
			  xrealloc (fields, alloc * sizeof *fields));
	      }

	    fields[c] = debug_make_field (dhandle, savestring (name, namlen),
					  ftype, offset, bitsize,
					  DEBUG_VISIBILITY_PUBLIC);
	    if (fields[c] == NULL)
	      return FALSE;
	    ++c;
	  }

	fields[c] = NULL;

	type = debug_make_struct_type (dhandle, tc == 'S', size, fields);
	tag = TRUE;
      }
      break;

    case 'T':
      /* Typedef.  */
      if (! ieee_read_type_index (info, pp, &type))
	return FALSE;
      typdef = TRUE;
      break;

    case 'X':
      /* Procedure.  FIXME: This is an extern declaration, which we
         have no way of representing.  */
      {
	bfd_vma attr;
	debug_type rtype;
	bfd_vma nargs;
	bfd_boolean present;
	struct ieee_var *pv;

	/* FIXME: We ignore the attribute and the argument names.  */

	if (! ieee_read_number (info, pp, &attr)
	    || ! ieee_read_type_index (info, pp, &rtype)
	    || ! ieee_read_number (info, pp, &nargs))
	  return FALSE;
	do
	  {
	    const char *name;
	    unsigned long namlen;

	    if (! ieee_read_optional_id (info, pp, &name, &namlen, &present))
	      return FALSE;
	  }
	while (present);

	pv = info->vars.vars + varindx;
	pv->kind = IEEE_EXTERNAL;
	if (pv->namlen > 0
	    && debug_get_type_kind (dhandle, rtype) == DEBUG_KIND_POINTER)
	  {
	    /* Set up the return type as an indirect type pointing to
               the variable slot, so that we can change it to a
               reference later if appropriate.  */
	    pv->pslot = (debug_type *) xmalloc (sizeof *pv->pslot);
	    *pv->pslot = rtype;
	    rtype = debug_make_indirect_type (dhandle, pv->pslot,
					      (const char *) NULL);
	  }

	type = debug_make_function_type (dhandle, rtype, (debug_type *) NULL,
					 FALSE);
      }
      break;

    case 'V':
      /* Void.  This is not documented, but the MRI compiler emits it.  */
      type = debug_make_void_type (dhandle);
      break;

    case 'Z':
      /* Array with 0 lower bound.  */
      {
	debug_type etype;
	bfd_vma high;

	if (! ieee_read_type_index (info, pp, &etype)
	    || ! ieee_read_number (info, pp, &high))
	  return FALSE;

	type = debug_make_array_type (dhandle, etype,
				      ieee_builtin_type (info, ty_code_start,
							 ((unsigned int)
							  builtin_int)),
				      0, (bfd_signed_vma) high, FALSE);
      }
      break;

    case 'c': /* Complex.  */
    case 'd': /* Double complex.  */
      {
	const char *name;
	unsigned long namlen;

	/* FIXME: I don't know what the name means.  */

	if (! ieee_read_id (info, pp, &name, &namlen))
	  return FALSE;

	type = debug_make_complex_type (dhandle, tc == 'c' ? 4 : 8);
      }
      break;

    case 'f':
      /* Pascal file name.  FIXME.  */
      ieee_error (info, ty_code_start, _("Pascal file name not supported"));
      return FALSE;

    case 'g':
      /* Bitfield type.  */
      {
	bfd_vma signedp, bitsize, dummy;
	const bfd_byte *hold;
	bfd_boolean present;

	if (! ieee_read_number (info, pp, &signedp)
	    || ! ieee_read_number (info, pp, &bitsize))
	  return FALSE;

	/* I think the documentation says that there is a type index,
           but some actual files do not have one.  */
	hold = *pp;
	if (! ieee_read_optional_number (info, pp, &dummy, &present))
	  return FALSE;
	if (! present)
	  {
	    /* FIXME: This is just a guess.  */
	    type = debug_make_int_type (dhandle, 4,
					signedp ? FALSE : TRUE);
	  }
	else
	  {
	    *pp = hold;
	    if (! ieee_read_type_index (info, pp, &type))
	      return FALSE;
	  }
	type_bitsize = bitsize;
      }
      break;

    case 'n':
      /* Qualifier.  */
      {
	bfd_vma kind;
	debug_type t;

	if (! ieee_read_number (info, pp, &kind)
	    || ! ieee_read_type_index (info, pp, &t))
	  return FALSE;

	switch (kind)
	  {
	  default:
	    ieee_error (info, ty_start, _("unsupported qualifier"));
	    return FALSE;

	  case 1:
	    type = debug_make_const_type (dhandle, t);
	    break;

	  case 2:
	    type = debug_make_volatile_type (dhandle, t);
	    break;
	  }
      }
      break;

    case 's':
      /* Set.  */
      {
	bfd_vma size;
	debug_type etype;

	if (! ieee_read_number (info, pp, &size)
	    || ! ieee_read_type_index (info, pp, &etype))
	  return FALSE;

	/* FIXME: We ignore the size.  */

	type = debug_make_set_type (dhandle, etype, FALSE);
      }
      break;

    case 'x':
      /* Procedure with compiler dependencies.  */
      {
	struct ieee_var *pv;
	bfd_vma attr, frame_type, push_mask, nargs, level, father;
	debug_type rtype;
	debug_type *arg_types;
	bfd_boolean varargs;
	bfd_boolean present;

	/* FIXME: We ignore some of this information.  */

	pv = info->vars.vars + varindx;

	if (! ieee_read_number (info, pp, &attr)
	    || ! ieee_read_number (info, pp, &frame_type)
	    || ! ieee_read_number (info, pp, &push_mask)
	    || ! ieee_read_type_index (info, pp, &rtype)
	    || ! ieee_read_number (info, pp, &nargs))
	  return FALSE;
	if (nargs == (bfd_vma) -1)
	  {
	    arg_types = NULL;
	    varargs = FALSE;
	  }
	else
	  {
	    unsigned int i;

	    arg_types = ((debug_type *)
			 xmalloc ((nargs + 1) * sizeof *arg_types));
	    for (i = 0; i < nargs; i++)
	      if (! ieee_read_type_index (info, pp, arg_types + i))
		return FALSE;

	    /* If the last type is pointer to void, this is really a
               varargs function.  */
	    varargs = FALSE;
	    if (nargs > 0)
	      {
		debug_type last;

		last = arg_types[nargs - 1];
		if (debug_get_type_kind (dhandle, last) == DEBUG_KIND_POINTER
		    && (debug_get_type_kind (dhandle,
					     debug_get_target_type (dhandle,
								    last))
			== DEBUG_KIND_VOID))
		  {
		    --nargs;
		    varargs = TRUE;
		  }
	      }

	    /* If there are any pointer arguments, turn them into
               indirect types in case we later need to convert them to
               reference types.  */
	    for (i = 0; i < nargs; i++)
	      {
		if (debug_get_type_kind (dhandle, arg_types[i])
		    == DEBUG_KIND_POINTER)
		  {
		    if (arg_slots == NULL)
		      {
			arg_slots = ((debug_type *)
				     xmalloc (nargs * sizeof *arg_slots));
			memset (arg_slots, 0, nargs * sizeof *arg_slots);
		      }
		    arg_slots[i] = arg_types[i];
		    arg_types[i] =
		      debug_make_indirect_type (dhandle,
						arg_slots + i,
						(const char *) NULL);
		  }
	      }

	    arg_types[nargs] = DEBUG_TYPE_NULL;
	  }
	if (! ieee_read_number (info, pp, &level)
	    || ! ieee_read_optional_number (info, pp, &father, &present))
	  return FALSE;

	/* We can't distinguish between a global function and a static
           function.  */
	pv->kind = IEEE_FUNCTION;

	if (pv->namlen > 0
	    && debug_get_type_kind (dhandle, rtype) == DEBUG_KIND_POINTER)
	  {
	    /* Set up the return type as an indirect type pointing to
               the variable slot, so that we can change it to a
               reference later if appropriate.  */
	    pv->pslot = (debug_type *) xmalloc (sizeof *pv->pslot);
	    *pv->pslot = rtype;
	    rtype = debug_make_indirect_type (dhandle, pv->pslot,
					      (const char *) NULL);
	  }

	type = debug_make_function_type (dhandle, rtype, arg_types, varargs);
      }
      break;
    }

  /* Record the type in the table.  */

  if (type == DEBUG_TYPE_NULL)
    return FALSE;

  info->vars.vars[varindx].type = type;

  if ((tag || typdef)
      && info->vars.vars[varindx].namlen > 0)
    {
      const char *name;

      name = savestring (info->vars.vars[varindx].name,
			 info->vars.vars[varindx].namlen);
      if (typdef)
	type = debug_name_type (dhandle, name, type);
      else if (tc == 'E' || tc == 'N')
	type = debug_tag_type (dhandle, name, type);
      else
	{
	  struct ieee_tag *it;

	  /* We must allocate all struct tags as indirect types, so
             that if we later see a definition of the tag as a C++
             record we can update the indirect slot and automatically
             change all the existing references.  */
	  it = (struct ieee_tag *) xmalloc (sizeof *it);
	  memset (it, 0, sizeof *it);
	  it->next = info->tags;
	  info->tags = it;
	  it->name = name;
	  it->slot = type;

	  type = debug_make_indirect_type (dhandle, &it->slot, name);
	  type = debug_tag_type (dhandle, name, type);

	  it->type = type;
	}
      if (type == NULL)
	return FALSE;
    }

  info->types.types[typeindx].type = type;
  info->types.types[typeindx].arg_slots = arg_slots;
  info->types.types[typeindx].bitsize = type_bitsize;

  /* We may have already allocated type as an indirect type pointing
     to slot.  It does no harm to replace the indirect type with the
     real type.  Filling in slot as well handles the indirect types
     which are already hanging around.  */
  if (info->types.types[typeindx].pslot != NULL)
    *info->types.types[typeindx].pslot = type;

  return TRUE;
}

/* Parse an ATN record.  */

static bfd_boolean
parse_ieee_atn (struct ieee_info *info, const bfd_byte **pp)
{
  const bfd_byte *atn_start, *atn_code_start;
  bfd_vma varindx;
  struct ieee_var *pvar;
  debug_type type;
  bfd_vma atn_code;
  void *dhandle;
  bfd_vma v, v2, v3, v4, v5;
  const char *name;
  unsigned long namlen;
  char *namcopy;
  bfd_boolean present;
  int blocktype;

  atn_start = *pp;

  if (! ieee_read_number (info, pp, &varindx)
      || ! ieee_read_type_index (info, pp, &type))
    return FALSE;

  atn_code_start = *pp;

  if (! ieee_read_number (info, pp, &atn_code))
    return FALSE;

  if (varindx == 0)
    {
      pvar = NULL;
      name = "";
      namlen = 0;
    }
  else if (varindx < 32)
    {
      /* The MRI compiler reportedly sometimes emits variable lifetime
         information for a register.  We just ignore it.  */
      if (atn_code == 9)
	return ieee_read_number (info, pp, &v);

      ieee_error (info, atn_start, _("illegal variable index"));
      return FALSE;
    }
  else
    {
      varindx -= 32;
      if (varindx >= info->vars.alloc
	  || info->vars.vars[varindx].name == NULL)
	{
	  /* The MRI compiler or linker sometimes omits the NN record
             for a pmisc record.  */
	  if (atn_code == 62)
	    {
	      if (varindx >= info->vars.alloc)
		{
		  unsigned int alloc;

		  alloc = info->vars.alloc;
		  if (alloc == 0)
		    alloc = 4;
		  while (varindx >= alloc)
		    alloc *= 2;
		  info->vars.vars = ((struct ieee_var *)
				     xrealloc (info->vars.vars,
					       (alloc
						* sizeof *info->vars.vars)));
		  memset (info->vars.vars + info->vars.alloc, 0,
			  ((alloc - info->vars.alloc)
			   * sizeof *info->vars.vars));
		  info->vars.alloc = alloc;
		}

	      pvar = info->vars.vars + varindx;
	      pvar->name = "";
	      pvar->namlen = 0;
	    }
	  else
	    {
	      ieee_error (info, atn_start, _("undefined variable in ATN"));
	      return FALSE;
	    }
	}

      pvar = info->vars.vars + varindx;

      pvar->type = type;

      name = pvar->name;
      namlen = pvar->namlen;
    }

  dhandle = info->dhandle;

  /* If we are going to call debug_record_variable with a pointer
     type, change the type to an indirect type so that we can later
     change it to a reference type if we encounter a C++ pmisc 'R'
     record.  */
  if (pvar != NULL
      && type != DEBUG_TYPE_NULL
      && debug_get_type_kind (dhandle, type) == DEBUG_KIND_POINTER)
    {
      switch (atn_code)
	{
	case 1:
	case 2:
	case 3:
	case 5:
	case 8:
	case 10:
	  pvar->pslot = (debug_type *) xmalloc (sizeof *pvar->pslot);
	  *pvar->pslot = type;
	  type = debug_make_indirect_type (dhandle, pvar->pslot,
					   (const char *) NULL);
	  pvar->type = type;
	  break;
	}
    }

  switch (atn_code)
    {
    default:
      ieee_error (info, atn_code_start, _("unknown ATN type"));
      return FALSE;

    case 1:
      /* Automatic variable.  */
      if (! ieee_read_number (info, pp, &v))
	return FALSE;
      namcopy = savestring (name, namlen);
      if (type == NULL)
	type = debug_make_void_type (dhandle);
      if (pvar != NULL)
	pvar->kind = IEEE_LOCAL;
      return debug_record_variable (dhandle, namcopy, type, DEBUG_LOCAL, v);

    case 2:
      /* Register variable.  */
      if (! ieee_read_number (info, pp, &v))
	return FALSE;
      namcopy = savestring (name, namlen);
      if (type == NULL)
	type = debug_make_void_type (dhandle);
      if (pvar != NULL)
	pvar->kind = IEEE_LOCAL;
      return debug_record_variable (dhandle, namcopy, type, DEBUG_REGISTER,
				    ieee_regno_to_genreg (info->abfd, v));

    case 3:
      /* Static variable.  */
      if (! ieee_require_asn (info, pp, &v))
	return FALSE;
      namcopy = savestring (name, namlen);
      if (type == NULL)
	type = debug_make_void_type (dhandle);
      if (info->blockstack.bsp <= info->blockstack.stack)
	blocktype = 0;
      else
	blocktype = info->blockstack.bsp[-1].kind;
      if (pvar != NULL)
	{
	  if (blocktype == 4 || blocktype == 6)
	    pvar->kind = IEEE_LOCAL;
	  else
	    pvar->kind = IEEE_STATIC;
	}
      return debug_record_variable (dhandle, namcopy, type,
				    (blocktype == 4 || blocktype == 6
				     ? DEBUG_LOCAL_STATIC
				     : DEBUG_STATIC),
				    v);

    case 4:
      /* External function.  We don't currently record these.  FIXME.  */
      if (pvar != NULL)
	pvar->kind = IEEE_EXTERNAL;
      return TRUE;

    case 5:
      /* External variable.  We don't currently record these.  FIXME.  */
      if (pvar != NULL)
	pvar->kind = IEEE_EXTERNAL;
      return TRUE;

    case 7:
      if (! ieee_read_number (info, pp, &v)
	  || ! ieee_read_number (info, pp, &v2)
	  || ! ieee_read_optional_number (info, pp, &v3, &present))
	return FALSE;
      if (present)
	{
	  if (! ieee_read_optional_number (info, pp, &v4, &present))
	    return FALSE;
	}

      /* We just ignore the two optional fields in v3 and v4, since
         they are not defined.  */

      if (! ieee_require_asn (info, pp, &v3))
	return FALSE;

      /* We have no way to record the column number.  FIXME.  */

      return debug_record_line (dhandle, v, v3);

    case 8:
      /* Global variable.  */
      if (! ieee_require_asn (info, pp, &v))
	return FALSE;
      namcopy = savestring (name, namlen);
      if (type == NULL)
	type = debug_make_void_type (dhandle);
      if (pvar != NULL)
	pvar->kind = IEEE_GLOBAL;
      return debug_record_variable (dhandle, namcopy, type, DEBUG_GLOBAL, v);

    case 9:
      /* Variable lifetime information.  */
      if (! ieee_read_number (info, pp, &v))
	return FALSE;

      /* We have no way to record this information.  FIXME.  */
      return TRUE;

    case 10:
      /* Locked register.  The spec says that there are two required
         fields, but at least on occasion the MRI compiler only emits
         one.  */
      if (! ieee_read_number (info, pp, &v)
	  || ! ieee_read_optional_number (info, pp, &v2, &present))
	return FALSE;

      /* I think this means a variable that is both in a register and
         a frame slot.  We ignore the frame slot.  FIXME.  */

      namcopy = savestring (name, namlen);
      if (type == NULL)
	type = debug_make_void_type (dhandle);
      if (pvar != NULL)
	pvar->kind = IEEE_LOCAL;
      return debug_record_variable (dhandle, namcopy, type, DEBUG_REGISTER, v);

    case 11:
      /* Reserved for FORTRAN common.  */
      ieee_error (info, atn_code_start, _("unsupported ATN11"));

      /* Return TRUE to keep going.  */
      return TRUE;

    case 12:
      /* Based variable.  */
      v3 = 0;
      v4 = 0x80;
      v5 = 0;
      if (! ieee_read_number (info, pp, &v)
	  || ! ieee_read_number (info, pp, &v2)
	  || ! ieee_read_optional_number (info, pp, &v3, &present))
	return FALSE;
      if (present)
	{
	  if (! ieee_read_optional_number (info, pp, &v4, &present))
	    return FALSE;
	  if (present)
	    {
	      if (! ieee_read_optional_number (info, pp, &v5, &present))
		return FALSE;
	    }
	}

      /* We have no way to record this information.  FIXME.  */

      ieee_error (info, atn_code_start, _("unsupported ATN12"));

      /* Return TRUE to keep going.  */
      return TRUE;

    case 16:
      /* Constant.  The description of this that I have is ambiguous,
         so I'm not going to try to implement it.  */
      if (! ieee_read_number (info, pp, &v)
	  || ! ieee_read_optional_number (info, pp, &v2, &present))
	return FALSE;
      if (present)
	{
	  if (! ieee_read_optional_number (info, pp, &v2, &present))
	    return FALSE;
	  if (present)
	    {
	      if (! ieee_read_optional_id (info, pp, &name, &namlen, &present))
		return FALSE;
	    }
	}

      if ((ieee_record_enum_type) **pp == ieee_e2_first_byte_enum)
	{
	  if (! ieee_require_asn (info, pp, &v3))
	    return FALSE;
	}

      return TRUE;

    case 19:
      /* Static variable from assembler.  */
      v2 = 0;
      if (! ieee_read_number (info, pp, &v)
	  || ! ieee_read_optional_number (info, pp, &v2, &present)
	  || ! ieee_require_asn (info, pp, &v3))
	return FALSE;
      namcopy = savestring (name, namlen);
      /* We don't really handle this correctly.  FIXME.  */
      return debug_record_variable (dhandle, namcopy,
				    debug_make_void_type (dhandle),
				    v2 != 0 ? DEBUG_GLOBAL : DEBUG_STATIC,
				    v3);

    case 62:
      /* Procedure miscellaneous information.  */
    case 63:
      /* Variable miscellaneous information.  */
    case 64:
      /* Module miscellaneous information.  */
      if (! ieee_read_number (info, pp, &v)
	  || ! ieee_read_number (info, pp, &v2)
	  || ! ieee_read_optional_id (info, pp, &name, &namlen, &present))
	return FALSE;

      if (atn_code == 62 && v == 80)
	{
	  if (present)
	    {
	      ieee_error (info, atn_code_start,
			  _("unexpected string in C++ misc"));
	      return FALSE;
	    }
	  return ieee_read_cxx_misc (info, pp, v2);
	}

      /* We just ignore all of this stuff.  FIXME.  */

      for (; v2 > 0; --v2)
	{
	  switch ((ieee_record_enum_type) **pp)
	    {
	    default:
	      ieee_error (info, *pp, _("bad misc record"));
	      return FALSE;

	    case ieee_at_record_enum:
	      if (! ieee_require_atn65 (info, pp, &name, &namlen))
		return FALSE;
	      break;

	    case ieee_e2_first_byte_enum:
	      if (! ieee_require_asn (info, pp, &v3))
		return FALSE;
	      break;
	    }
	}

      return TRUE;
    }

  /*NOTREACHED*/
}

/* Handle C++ debugging miscellaneous records.  This is called for
   procedure miscellaneous records of type 80.  */

static bfd_boolean
ieee_read_cxx_misc (struct ieee_info *info, const bfd_byte **pp,
		    unsigned long count)
{
  const bfd_byte *start;
  bfd_vma category;

  start = *pp;

  /* Get the category of C++ misc record.  */
  if (! ieee_require_asn (info, pp, &category))
    return FALSE;
  --count;

  switch (category)
    {
    default:
      ieee_error (info, start, _("unrecognized C++ misc record"));
      return FALSE;

    case 'T':
      if (! ieee_read_cxx_class (info, pp, count))
	return FALSE;
      break;

    case 'M':
      {
	bfd_vma flags;
	const char *name;
	unsigned long namlen;

	/* The IEEE spec indicates that the 'M' record only has a
           flags field.  The MRI compiler also emits the name of the
           function.  */

	if (! ieee_require_asn (info, pp, &flags))
	  return FALSE;
	if (*pp < info->pend
	    && (ieee_record_enum_type) **pp == ieee_at_record_enum)
	  {
	    if (! ieee_require_atn65 (info, pp, &name, &namlen))
	      return FALSE;
	  }

	/* This is emitted for method functions, but I don't think we
           care very much.  It might help if it told us useful
           information like the class with which this function is
           associated, but it doesn't, so it isn't helpful.  */
      }
      break;

    case 'B':
      if (! ieee_read_cxx_defaults (info, pp, count))
	return FALSE;
      break;

    case 'z':
      {
	const char *name, *mangled, *class;
	unsigned long namlen, mangledlen, classlen;
	bfd_vma control;

	/* Pointer to member.  */

	if (! ieee_require_atn65 (info, pp, &name, &namlen)
	    || ! ieee_require_atn65 (info, pp, &mangled, &mangledlen)
	    || ! ieee_require_atn65 (info, pp, &class, &classlen)
	    || ! ieee_require_asn (info, pp, &control))
	  return FALSE;

	/* FIXME: We should now track down name and change its type.  */
      }
      break;

    case 'R':
      if (! ieee_read_reference (info, pp))
	return FALSE;
      break;
    }

  return TRUE;
}

/* Read a C++ class definition.  This is a pmisc type 80 record of
   category 'T'.  */

static bfd_boolean
ieee_read_cxx_class (struct ieee_info *info, const bfd_byte **pp,
		     unsigned long count)
{
  const bfd_byte *start;
  bfd_vma class;
  const char *tag;
  unsigned long taglen;
  struct ieee_tag *it;
  void *dhandle;
  debug_field *fields;
  unsigned int field_count, field_alloc;
  debug_baseclass *baseclasses;
  unsigned int baseclasses_count, baseclasses_alloc;
  const debug_field *structfields;
  struct ieee_method
    {
      const char *name;
      unsigned long namlen;
      debug_method_variant *variants;
      unsigned count;
      unsigned int alloc;
    } *methods;
  unsigned int methods_count, methods_alloc;
  debug_type vptrbase;
  bfd_boolean ownvptr;
  debug_method *dmethods;

  start = *pp;

  if (! ieee_require_asn (info, pp, &class))
    return FALSE;
  --count;

  if (! ieee_require_atn65 (info, pp, &tag, &taglen))
    return FALSE;
  --count;

  /* Find the C struct with this name.  */
  for (it = info->tags; it != NULL; it = it->next)
    if (it->name[0] == tag[0]
	&& strncmp (it->name, tag, taglen) == 0
	&& strlen (it->name) == taglen)
      break;
  if (it == NULL)
    {
      ieee_error (info, start, _("undefined C++ object"));
      return FALSE;
    }

  dhandle = info->dhandle;

  fields = NULL;
  field_count = 0;
  field_alloc = 0;
  baseclasses = NULL;
  baseclasses_count = 0;
  baseclasses_alloc = 0;
  methods = NULL;
  methods_count = 0;
  methods_alloc = 0;
  vptrbase = DEBUG_TYPE_NULL;
  ownvptr = FALSE;

  structfields = debug_get_fields (dhandle, it->type);

  while (count > 0)
    {
      bfd_vma id;
      const bfd_byte *spec_start;

      spec_start = *pp;

      if (! ieee_require_asn (info, pp, &id))
	return FALSE;
      --count;

      switch (id)
	{
	default:
	  ieee_error (info, spec_start, _("unrecognized C++ object spec"));
	  return FALSE;

	case 'b':
	  {
	    bfd_vma flags, cinline;
	    const char *basename, *fieldname;
	    unsigned long baselen, fieldlen;
	    char *basecopy;
	    debug_type basetype;
	    bfd_vma bitpos;
	    bfd_boolean virtualp;
	    enum debug_visibility visibility;
	    debug_baseclass baseclass;

	    /* This represents a base or friend class.  */

	    if (! ieee_require_asn (info, pp, &flags)
		|| ! ieee_require_atn65 (info, pp, &basename, &baselen)
		|| ! ieee_require_asn (info, pp, &cinline)
		|| ! ieee_require_atn65 (info, pp, &fieldname, &fieldlen))
	      return FALSE;
	    count -= 4;

	    /* We have no way of recording friend information, so we
               just ignore it.  */
	    if ((flags & BASEFLAGS_FRIEND) != 0)
	      break;

	    /* I assume that either all of the members of the
               baseclass are included in the object, starting at the
               beginning of the object, or that none of them are
               included.  */

	    if ((fieldlen == 0) == (cinline == 0))
	      {
		ieee_error (info, start, _("unsupported C++ object type"));
		return FALSE;
	      }

	    basecopy = savestring (basename, baselen);
	    basetype = debug_find_tagged_type (dhandle, basecopy,
					       DEBUG_KIND_ILLEGAL);
	    free (basecopy);
	    if (basetype == DEBUG_TYPE_NULL)
	      {
		ieee_error (info, start, _("C++ base class not defined"));
		return FALSE;
	      }

	    if (fieldlen == 0)
	      bitpos = 0;
	    else
	      {
		const debug_field *pf;

		if (structfields == NULL)
		  {
		    ieee_error (info, start, _("C++ object has no fields"));
		    return FALSE;
		  }

		for (pf = structfields; *pf != DEBUG_FIELD_NULL; pf++)
		  {
		    const char *fname;

		    fname = debug_get_field_name (dhandle, *pf);
		    if (fname == NULL)
		      return FALSE;
		    if (fname[0] == fieldname[0]
			&& strncmp (fname, fieldname, fieldlen) == 0
			&& strlen (fname) == fieldlen)
		      break;
		  }
		if (*pf == DEBUG_FIELD_NULL)
		  {
		    ieee_error (info, start,
				_("C++ base class not found in container"));
		    return FALSE;
		  }

		bitpos = debug_get_field_bitpos (dhandle, *pf);
	      }

	    if ((flags & BASEFLAGS_VIRTUAL) != 0)
	      virtualp = TRUE;
	    else
	      virtualp = FALSE;
	    if ((flags & BASEFLAGS_PRIVATE) != 0)
	      visibility = DEBUG_VISIBILITY_PRIVATE;
	    else
	      visibility = DEBUG_VISIBILITY_PUBLIC;

	    baseclass = debug_make_baseclass (dhandle, basetype, bitpos,
					      virtualp, visibility);
	    if (baseclass == DEBUG_BASECLASS_NULL)
	      return FALSE;

	    if (baseclasses_count + 1 >= baseclasses_alloc)
	      {
		baseclasses_alloc += 10;
		baseclasses = ((debug_baseclass *)
			       xrealloc (baseclasses,
					 (baseclasses_alloc
					  * sizeof *baseclasses)));
	      }

	    baseclasses[baseclasses_count] = baseclass;
	    ++baseclasses_count;
	    baseclasses[baseclasses_count] = DEBUG_BASECLASS_NULL;
	  }
	  break;

	case 'd':
	  {
	    bfd_vma flags;
	    const char *fieldname, *mangledname;
	    unsigned long fieldlen, mangledlen;
	    char *fieldcopy;
	    bfd_boolean staticp;
	    debug_type ftype;
	    const debug_field *pf = NULL;
	    enum debug_visibility visibility;
	    debug_field field;

	    /* This represents a data member.  */

	    if (! ieee_require_asn (info, pp, &flags)
		|| ! ieee_require_atn65 (info, pp, &fieldname, &fieldlen)
		|| ! ieee_require_atn65 (info, pp, &mangledname, &mangledlen))
	      return FALSE;
	    count -= 3;

	    fieldcopy = savestring (fieldname, fieldlen);

	    staticp = (flags & CXXFLAGS_STATIC) != 0 ? TRUE : FALSE;

	    if (staticp)
	      {
		struct ieee_var *pv, *pvend;

		/* See if we can find a definition for this variable.  */
		pv = info->vars.vars;
		pvend = pv + info->vars.alloc;
		for (; pv < pvend; pv++)
		  if (pv->namlen == mangledlen
		      && strncmp (pv->name, mangledname, mangledlen) == 0)
		    break;
		if (pv < pvend)
		  ftype = pv->type;
		else
		  {
		    /* This can happen if the variable is never used.  */
		    ftype = ieee_builtin_type (info, start,
					       (unsigned int) builtin_void);
		  }
	      }
	    else
	      {
		unsigned int findx;

		if (structfields == NULL)
		  {
		    ieee_error (info, start, _("C++ object has no fields"));
		    return FALSE;
		  }

		for (pf = structfields, findx = 0;
		     *pf != DEBUG_FIELD_NULL;
		     pf++, findx++)
		  {
		    const char *fname;

		    fname = debug_get_field_name (dhandle, *pf);
		    if (fname == NULL)
		      return FALSE;
		    if (fname[0] == mangledname[0]
			&& strncmp (fname, mangledname, mangledlen) == 0
			&& strlen (fname) == mangledlen)
		      break;
		  }
		if (*pf == DEBUG_FIELD_NULL)
		  {
		    ieee_error (info, start,
				_("C++ data member not found in container"));
		    return FALSE;
		  }

		ftype = debug_get_field_type (dhandle, *pf);

		if (debug_get_type_kind (dhandle, ftype) == DEBUG_KIND_POINTER)
		  {
		    /* We might need to convert this field into a
                       reference type later on, so make it an indirect
                       type.  */
		    if (it->fslots == NULL)
		      {
			unsigned int fcnt;
			const debug_field *pfcnt;

			fcnt = 0;
			for (pfcnt = structfields;
			     *pfcnt != DEBUG_FIELD_NULL;
			     pfcnt++)
			  ++fcnt;
			it->fslots = ((debug_type *)
				      xmalloc (fcnt * sizeof *it->fslots));
			memset (it->fslots, 0,
				fcnt * sizeof *it->fslots);
		      }

		    if (ftype == DEBUG_TYPE_NULL)
		      return FALSE;
		    it->fslots[findx] = ftype;
		    ftype = debug_make_indirect_type (dhandle,
						      it->fslots + findx,
						      (const char *) NULL);
		  }
	      }
	    if (ftype == DEBUG_TYPE_NULL)
	      return FALSE;

	    switch (flags & CXXFLAGS_VISIBILITY)
	      {
	      default:
		ieee_error (info, start, _("unknown C++ visibility"));
		return FALSE;

	      case CXXFLAGS_VISIBILITY_PUBLIC:
		visibility = DEBUG_VISIBILITY_PUBLIC;
		break;

	      case CXXFLAGS_VISIBILITY_PRIVATE:
		visibility = DEBUG_VISIBILITY_PRIVATE;
		break;

	      case CXXFLAGS_VISIBILITY_PROTECTED:
		visibility = DEBUG_VISIBILITY_PROTECTED;
		break;
	      }

	    if (staticp)
	      {
		char *mangledcopy;

		mangledcopy = savestring (mangledname, mangledlen);

		field = debug_make_static_member (dhandle, fieldcopy,
						  ftype, mangledcopy,
						  visibility);
	      }
	    else
	      {
		bfd_vma bitpos, bitsize;

		bitpos = debug_get_field_bitpos (dhandle, *pf);
		bitsize = debug_get_field_bitsize (dhandle, *pf);
		if (bitpos == (bfd_vma) -1 || bitsize == (bfd_vma) -1)
		  {
		    ieee_error (info, start, _("bad C++ field bit pos or size"));
		    return FALSE;
		  }
		field = debug_make_field (dhandle, fieldcopy, ftype, bitpos,
					  bitsize, visibility);
	      }

	    if (field == DEBUG_FIELD_NULL)
	      return FALSE;

	    if (field_count + 1 >= field_alloc)
	      {
		field_alloc += 10;
		fields = ((debug_field *)
			  xrealloc (fields, field_alloc * sizeof *fields));
	      }

	    fields[field_count] = field;
	    ++field_count;
	    fields[field_count] = DEBUG_FIELD_NULL;
	  }
	  break;

	case 'm':
	case 'v':
	  {
	    bfd_vma flags, voffset, control;
	    const char *name, *mangled;
	    unsigned long namlen, mangledlen;
	    struct ieee_var *pv, *pvend;
	    debug_type type;
	    enum debug_visibility visibility;
	    bfd_boolean constp, volatilep;
	    char *mangledcopy;
	    debug_method_variant mv;
	    struct ieee_method *meth;
	    unsigned int im;

	    if (! ieee_require_asn (info, pp, &flags)
		|| ! ieee_require_atn65 (info, pp, &name, &namlen)
		|| ! ieee_require_atn65 (info, pp, &mangled, &mangledlen))
	      return FALSE;
	    count -= 3;
	    if (id != 'v')
	      voffset = 0;
	    else
	      {
		if (! ieee_require_asn (info, pp, &voffset))
		  return FALSE;
		--count;
	      }
	    if (! ieee_require_asn (info, pp, &control))
	      return FALSE;
	    --count;

	    /* We just ignore the control information.  */

	    /* We have no way to represent friend information, so we
               just ignore it.  */
	    if ((flags & CXXFLAGS_FRIEND) != 0)
	      break;

	    /* We should already have seen a type for the function.  */
	    pv = info->vars.vars;
	    pvend = pv + info->vars.alloc;
	    for (; pv < pvend; pv++)
	      if (pv->namlen == mangledlen
		  && strncmp (pv->name, mangled, mangledlen) == 0)
		break;

	    if (pv >= pvend)
	      {
		/* We won't have type information for this function if
		   it is not included in this file.  We don't try to
		   handle this case.  FIXME.  */
		type = (debug_make_function_type
			(dhandle,
			 ieee_builtin_type (info, start,
					    (unsigned int) builtin_void),
			 (debug_type *) NULL,
			 FALSE));
	      }
	    else
	      {
		debug_type return_type;
		const debug_type *arg_types;
		bfd_boolean varargs;

		if (debug_get_type_kind (dhandle, pv->type)
		    != DEBUG_KIND_FUNCTION)
		  {
		    ieee_error (info, start,
				_("bad type for C++ method function"));
		    return FALSE;
		  }

		return_type = debug_get_return_type (dhandle, pv->type);
		arg_types = debug_get_parameter_types (dhandle, pv->type,
						       &varargs);
		if (return_type == DEBUG_TYPE_NULL || arg_types == NULL)
		  {
		    ieee_error (info, start,
				_("no type information for C++ method function"));
		    return FALSE;
		  }

		type = debug_make_method_type (dhandle, return_type, it->type,
					       (debug_type *) arg_types,
					       varargs);
	      }
	    if (type == DEBUG_TYPE_NULL)
	      return FALSE;

	    switch (flags & CXXFLAGS_VISIBILITY)
	      {
	      default:
		ieee_error (info, start, _("unknown C++ visibility"));
		return FALSE;

	      case CXXFLAGS_VISIBILITY_PUBLIC:
		visibility = DEBUG_VISIBILITY_PUBLIC;
		break;

	      case CXXFLAGS_VISIBILITY_PRIVATE:
		visibility = DEBUG_VISIBILITY_PRIVATE;
		break;

	      case CXXFLAGS_VISIBILITY_PROTECTED:
		visibility = DEBUG_VISIBILITY_PROTECTED;
		break;
	      }

	    constp = (flags & CXXFLAGS_CONST) != 0 ? TRUE : FALSE;
	    volatilep = (flags & CXXFLAGS_VOLATILE) != 0 ? TRUE : FALSE;

	    mangledcopy = savestring (mangled, mangledlen);

	    if ((flags & CXXFLAGS_STATIC) != 0)
	      {
		if (id == 'v')
		  {
		    ieee_error (info, start, _("C++ static virtual method"));
		    return FALSE;
		  }
		mv = debug_make_static_method_variant (dhandle, mangledcopy,
						       type, visibility,
						       constp, volatilep);
	      }
	    else
	      {
		debug_type vcontext;

		if (id != 'v')
		  vcontext = DEBUG_TYPE_NULL;
		else
		  {
		    /* FIXME: How can we calculate this correctly?  */
		    vcontext = it->type;
		  }
		mv = debug_make_method_variant (dhandle, mangledcopy, type,
						visibility, constp,
						volatilep, voffset,
						vcontext);
	      }
	    if (mv == DEBUG_METHOD_VARIANT_NULL)
	      return FALSE;

	    for (meth = methods, im = 0; im < methods_count; meth++, im++)
	      if (meth->namlen == namlen
		  && strncmp (meth->name, name, namlen) == 0)
		break;
	    if (im >= methods_count)
	      {
		if (methods_count >= methods_alloc)
		  {
		    methods_alloc += 10;
		    methods = ((struct ieee_method *)
			       xrealloc (methods,
					 methods_alloc * sizeof *methods));
		  }
		methods[methods_count].name = name;
		methods[methods_count].namlen = namlen;
		methods[methods_count].variants = NULL;
		methods[methods_count].count = 0;
		methods[methods_count].alloc = 0;
		meth = methods + methods_count;
		++methods_count;
	      }

	    if (meth->count + 1 >= meth->alloc)
	      {
		meth->alloc += 10;
		meth->variants = ((debug_method_variant *)
				  xrealloc (meth->variants,
					    (meth->alloc
					     * sizeof *meth->variants)));
	      }

	    meth->variants[meth->count] = mv;
	    ++meth->count;
	    meth->variants[meth->count] = DEBUG_METHOD_VARIANT_NULL;
	  }
	  break;

	case 'o':
	  {
	    bfd_vma spec;

	    /* We have no way to store this information, so we just
	       ignore it.  */
	    if (! ieee_require_asn (info, pp, &spec))
	      return FALSE;
	    --count;
	    if ((spec & 4) != 0)
	      {
		const char *filename;
		unsigned long filenamlen;
		bfd_vma lineno;

		if (! ieee_require_atn65 (info, pp, &filename, &filenamlen)
		    || ! ieee_require_asn (info, pp, &lineno))
		  return FALSE;
		count -= 2;
	      }
	    else if ((spec & 8) != 0)
	      {
		const char *mangled;
		unsigned long mangledlen;

		if (! ieee_require_atn65 (info, pp, &mangled, &mangledlen))
		  return FALSE;
		--count;
	      }
	    else
	      {
		ieee_error (info, start,
			    _("unrecognized C++ object overhead spec"));
		return FALSE;
	      }
	  }
	  break;

	case 'z':
	  {
	    const char *vname, *basename;
	    unsigned long vnamelen, baselen;
	    bfd_vma vsize, control;

	    /* A virtual table pointer.  */

	    if (! ieee_require_atn65 (info, pp, &vname, &vnamelen)
		|| ! ieee_require_asn (info, pp, &vsize)
		|| ! ieee_require_atn65 (info, pp, &basename, &baselen)
		|| ! ieee_require_asn (info, pp, &control))
	      return FALSE;
	    count -= 4;

	    /* We just ignore the control number.  We don't care what
	       the virtual table name is.  We have no way to store the
	       virtual table size, and I don't think we care anyhow.  */

	    /* FIXME: We can't handle multiple virtual table pointers.  */

	    if (baselen == 0)
	      ownvptr = TRUE;
	    else
	      {
		char *basecopy;

		basecopy = savestring (basename, baselen);
		vptrbase = debug_find_tagged_type (dhandle, basecopy,
						   DEBUG_KIND_ILLEGAL);
		free (basecopy);
		if (vptrbase == DEBUG_TYPE_NULL)
		  {
		    ieee_error (info, start, _("undefined C++ vtable"));
		    return FALSE;
		  }
	      }
	  }
	  break;
	}
    }

  /* Now that we have seen all the method variants, we can call
     debug_make_method for each one.  */

  if (methods_count == 0)
    dmethods = NULL;
  else
    {
      unsigned int i;

      dmethods = ((debug_method *)
		  xmalloc ((methods_count + 1) * sizeof *dmethods));
      for (i = 0; i < methods_count; i++)
	{
	  char *namcopy;

	  namcopy = savestring (methods[i].name, methods[i].namlen);
	  dmethods[i] = debug_make_method (dhandle, namcopy,
					   methods[i].variants);
	  if (dmethods[i] == DEBUG_METHOD_NULL)
	    return FALSE;
	}
      dmethods[i] = DEBUG_METHOD_NULL;
      free (methods);
    }

  /* The struct type was created as an indirect type pointing at
     it->slot.  We update it->slot to automatically update all
     references to this struct.  */
  it->slot = debug_make_object_type (dhandle,
				     class != 'u',
				     debug_get_type_size (dhandle,
							  it->slot),
				     fields, baseclasses, dmethods,
				     vptrbase, ownvptr);
  if (it->slot == DEBUG_TYPE_NULL)
    return FALSE;

  return TRUE;
}

/* Read C++ default argument value and reference type information.  */

static bfd_boolean
ieee_read_cxx_defaults (struct ieee_info *info, const bfd_byte **pp,
			unsigned long count)
{
  const bfd_byte *start;
  const char *fnname;
  unsigned long fnlen;
  bfd_vma defcount;

  start = *pp;

  /* Giving the function name before the argument count is an addendum
     to the spec.  The function name is demangled, though, so this
     record must always refer to the current function.  */

  if (info->blockstack.bsp <= info->blockstack.stack
      || info->blockstack.bsp[-1].fnindx == (unsigned int) -1)
    {
      ieee_error (info, start, _("C++ default values not in a function"));
      return FALSE;
    }

  if (! ieee_require_atn65 (info, pp, &fnname, &fnlen)
      || ! ieee_require_asn (info, pp, &defcount))
    return FALSE;
  count -= 2;

  while (defcount-- > 0)
    {
      bfd_vma type, val;
      const char *strval;
      unsigned long strvallen;

      if (! ieee_require_asn (info, pp, &type))
	return FALSE;
      --count;

      switch (type)
	{
	case 0:
	case 4:
	  break;

	case 1:
	case 2:
	  if (! ieee_require_asn (info, pp, &val))
	    return FALSE;
	  --count;
	  break;

	case 3:
	case 7:
	  if (! ieee_require_atn65 (info, pp, &strval, &strvallen))
	    return FALSE;
	  --count;
	  break;

	default:
	  ieee_error (info, start, _("unrecognized C++ default type"));
	  return FALSE;
	}

      /* We have no way to record the default argument values, so we
         just ignore them.  FIXME.  */
    }

  /* Any remaining arguments are indices of parameters that are really
     reference type.  */
  if (count > 0)
    {
      void *dhandle;
      debug_type *arg_slots;

      dhandle = info->dhandle;
      arg_slots = info->types.types[info->blockstack.bsp[-1].fnindx].arg_slots;
      while (count-- > 0)
	{
	  bfd_vma indx;
	  debug_type target;

	  if (! ieee_require_asn (info, pp, &indx))
	    return FALSE;
	  /* The index is 1 based.  */
	  --indx;
	  if (arg_slots == NULL
	      || arg_slots[indx] == DEBUG_TYPE_NULL
	      || (debug_get_type_kind (dhandle, arg_slots[indx])
		  != DEBUG_KIND_POINTER))
	    {
	      ieee_error (info, start, _("reference parameter is not a pointer"));
	      return FALSE;
	    }

	  target = debug_get_target_type (dhandle, arg_slots[indx]);
	  arg_slots[indx] = debug_make_reference_type (dhandle, target);
	  if (arg_slots[indx] == DEBUG_TYPE_NULL)
	    return FALSE;
	}
    }

  return TRUE;
}

/* Read a C++ reference definition.  */

static bfd_boolean
ieee_read_reference (struct ieee_info *info, const bfd_byte **pp)
{
  const bfd_byte *start;
  bfd_vma flags;
  const char *class, *name;
  unsigned long classlen, namlen;
  debug_type *pslot;
  debug_type target;

  start = *pp;

  if (! ieee_require_asn (info, pp, &flags))
    return FALSE;

  /* Giving the class name before the member name is in an addendum to
     the spec.  */
  if (flags == 3)
    {
      if (! ieee_require_atn65 (info, pp, &class, &classlen))
	return FALSE;
    }

  if (! ieee_require_atn65 (info, pp, &name, &namlen))
    return FALSE;

  pslot = NULL;
  if (flags != 3)
    {
      int pass;

      /* We search from the last variable indices to the first in
	 hopes of finding local variables correctly.  We search the
	 local variables on the first pass, and the global variables
	 on the second.  FIXME: This probably won't work in all cases.
	 On the other hand, I don't know what will.  */
      for (pass = 0; pass < 2; pass++)
	{
	  struct ieee_vars *vars;
	  int i;
	  struct ieee_var *pv = NULL;

	  if (pass == 0)
	    vars = &info->vars;
	  else
	    {
	      vars = info->global_vars;
	      if (vars == NULL)
		break;
	    }

	  for (i = (int) vars->alloc - 1; i >= 0; i--)
	    {
	      bfd_boolean found;

	      pv = vars->vars + i;

	      if (pv->pslot == NULL
		  || pv->namlen != namlen
		  || strncmp (pv->name, name, namlen) != 0)
		continue;

	      found = FALSE;
	      switch (flags)
		{
		default:
		  ieee_error (info, start,
			      _("unrecognized C++ reference type"));
		  return FALSE;

		case 0:
		  /* Global variable or function.  */
		  if (pv->kind == IEEE_GLOBAL
		      || pv->kind == IEEE_EXTERNAL
		      || pv->kind == IEEE_FUNCTION)
		    found = TRUE;
		  break;

		case 1:
		  /* Global static variable or function.  */
		  if (pv->kind == IEEE_STATIC
		      || pv->kind == IEEE_FUNCTION)
		    found = TRUE;
		  break;

		case 2:
		  /* Local variable.  */
		  if (pv->kind == IEEE_LOCAL)
		    found = TRUE;
		  break;
		}

	      if (found)
		break;
	    }

	  if (i >= 0)
	    {
	      pslot = pv->pslot;
	      break;
	    }
	}
    }
  else
    {
      struct ieee_tag *it;

      for (it = info->tags; it != NULL; it = it->next)
	{
	  if (it->name[0] == class[0]
	      && strncmp (it->name, class, classlen) == 0
	      && strlen (it->name) == classlen)
	    {
	      if (it->fslots != NULL)
		{
		  const debug_field *pf;
		  unsigned int findx;

		  pf = debug_get_fields (info->dhandle, it->type);
		  if (pf == NULL)
		    {
		      ieee_error (info, start,
				  "C++ reference in class with no fields");
		      return FALSE;
		    }

		  for (findx = 0; *pf != DEBUG_FIELD_NULL; pf++, findx++)
		    {
		      const char *fname;

		      fname = debug_get_field_name (info->dhandle, *pf);
		      if (fname == NULL)
			return FALSE;
		      if (strncmp (fname, name, namlen) == 0
			  && strlen (fname) == namlen)
			{
			  pslot = it->fslots + findx;
			  break;
			}
		    }
		}

	      break;
	    }
	}
    }

  if (pslot == NULL)
    {
      ieee_error (info, start, _("C++ reference not found"));
      return FALSE;
    }

  /* We allocated the type of the object as an indirect type pointing
     to *pslot, which we can now update to be a reference type.  */
  if (debug_get_type_kind (info->dhandle, *pslot) != DEBUG_KIND_POINTER)
    {
      ieee_error (info, start, _("C++ reference is not pointer"));
      return FALSE;
    }

  target = debug_get_target_type (info->dhandle, *pslot);
  *pslot = debug_make_reference_type (info->dhandle, target);
  if (*pslot == DEBUG_TYPE_NULL)
    return FALSE;

  return TRUE;
}

/* Require an ASN record.  */

static bfd_boolean
ieee_require_asn (struct ieee_info *info, const bfd_byte **pp, bfd_vma *pv)
{
  const bfd_byte *start;
  ieee_record_enum_type c;
  bfd_vma varindx;

  start = *pp;

  c = (ieee_record_enum_type) **pp;
  if (c != ieee_e2_first_byte_enum)
    {
      ieee_error (info, start, _("missing required ASN"));
      return FALSE;
    }
  ++*pp;

  c = (ieee_record_enum_type) (((unsigned int) c << 8) | **pp);
  if (c != ieee_asn_record_enum)
    {
      ieee_error (info, start, _("missing required ASN"));
      return FALSE;
    }
  ++*pp;

  /* Just ignore the variable index.  */
  if (! ieee_read_number (info, pp, &varindx))
    return FALSE;

  return ieee_read_expression (info, pp, pv);
}

/* Require an ATN65 record.  */

static bfd_boolean
ieee_require_atn65 (struct ieee_info *info, const bfd_byte **pp,
		    const char **pname, unsigned long *pnamlen)
{
  const bfd_byte *start;
  ieee_record_enum_type c;
  bfd_vma name_indx, type_indx, atn_code;

  start = *pp;

  c = (ieee_record_enum_type) **pp;
  if (c != ieee_at_record_enum)
    {
      ieee_error (info, start, _("missing required ATN65"));
      return FALSE;
    }
  ++*pp;

  c = (ieee_record_enum_type) (((unsigned int) c << 8) | **pp);
  if (c != ieee_atn_record_enum)
    {
      ieee_error (info, start, _("missing required ATN65"));
      return FALSE;
    }
  ++*pp;

  if (! ieee_read_number (info, pp, &name_indx)
      || ! ieee_read_number (info, pp, &type_indx)
      || ! ieee_read_number (info, pp, &atn_code))
    return FALSE;

  /* Just ignore name_indx.  */

  if (type_indx != 0 || atn_code != 65)
    {
      ieee_error (info, start, _("bad ATN65 record"));
      return FALSE;
    }

  return ieee_read_id (info, pp, pname, pnamlen);
}

/* Convert a register number in IEEE debugging information into a
   generic register number.  */

static int
ieee_regno_to_genreg (bfd *abfd, int r)
{
  switch (bfd_get_arch (abfd))
    {
    case bfd_arch_m68k:
      /* For some reasons stabs adds 2 to the floating point register
         numbers.  */
      if (r >= 16)
	r += 2;
      break;

    case bfd_arch_i960:
      /* Stabs uses 0 to 15 for r0 to r15, 16 to 31 for g0 to g15, and
         32 to 35 for fp0 to fp3.  */
      --r;
      break;

    default:
      break;
    }

  return r;
}

/* Convert a generic register number to an IEEE specific one.  */

static int
ieee_genreg_to_regno (bfd *abfd, int r)
{
  switch (bfd_get_arch (abfd))
    {
    case bfd_arch_m68k:
      /* For some reason stabs add 2 to the floating point register
         numbers.  */
      if (r >= 18)
	r -= 2;
      break;

    case bfd_arch_i960:
      /* Stabs uses 0 to 15 for r0 to r15, 16 to 31 for g0 to g15, and
         32 to 35 for fp0 to fp3.  */
      ++r;
      break;

    default:
      break;
    }

  return r;
}

/* These routines build IEEE debugging information out of the generic
   debugging information.  */

/* We build the IEEE debugging information byte by byte.  Rather than
   waste time copying data around, we use a linked list of buffers to
   hold the data.  */

#define IEEE_BUFSIZE (490)

struct ieee_buf
{
  /* Next buffer.  */
  struct ieee_buf *next;
  /* Number of data bytes in this buffer.  */
  unsigned int c;
  /* Bytes.  */
  bfd_byte buf[IEEE_BUFSIZE];
};

/* A list of buffers.  */

struct ieee_buflist
{
  /* Head of list.  */
  struct ieee_buf *head;
  /* Tail--last buffer on list.  */
  struct ieee_buf *tail;
};

/* In order to generate the BB11 blocks required by the HP emulator,
   we keep track of ranges of addresses which correspond to a given
   compilation unit.  */

struct ieee_range
{
  /* Next range.  */
  struct ieee_range *next;
  /* Low address.  */
  bfd_vma low;
  /* High address.  */
  bfd_vma high;
};

/* This structure holds information for a class on the type stack.  */

struct ieee_type_class
{
  /* The name index in the debugging information.  */
  unsigned int indx;
  /* The pmisc records for the class.  */
  struct ieee_buflist pmiscbuf;
  /* The number of pmisc records.  */
  unsigned int pmisccount;
  /* The name of the class holding the virtual table, if not this
     class.  */
  const char *vclass;
  /* Whether this class holds its own virtual table.  */
  bfd_boolean ownvptr;
  /* The largest virtual table offset seen so far.  */
  bfd_vma voffset;
  /* The current method.  */
  const char *method;
  /* Additional pmisc records used to record fields of reference type.  */
  struct ieee_buflist refs;
};

/* This is how we store types for the writing routines.  Most types
   are simply represented by a type index.  */

struct ieee_write_type
{
  /* Type index.  */
  unsigned int indx;
  /* The size of the type, if known.  */
  unsigned int size;
  /* The name of the type, if any.  */
  const char *name;
  /* If this is a function or method type, we build the type here, and
     only add it to the output buffers if we need it.  */
  struct ieee_buflist fndef;
  /* If this is a struct, this is where the struct definition is
     built.  */
  struct ieee_buflist strdef;
  /* If this is a class, this is where the class information is built.  */
  struct ieee_type_class *classdef;
  /* Whether the type is unsigned.  */
  unsigned int unsignedp : 1;
  /* Whether this is a reference type.  */
  unsigned int referencep : 1;
  /* Whether this is in the local type block.  */
  unsigned int localp : 1;
  /* Whether this is a duplicate struct definition which we are
     ignoring.  */
  unsigned int ignorep : 1;
};

/* This is the type stack used by the debug writing routines.  FIXME:
   We could generate more efficient output if we remembered when we
   have output a particular type before.  */

struct ieee_type_stack
{
  /* Next entry on stack.  */
  struct ieee_type_stack *next;
  /* Type information.  */
  struct ieee_write_type type;
};

/* This is a list of associations between a name and some types.
   These are used for typedefs and tags.  */

struct ieee_name_type
{
  /* Next type for this name.  */
  struct ieee_name_type *next;
  /* ID number.  For a typedef, this is the index of the type to which
     this name is typedefed.  */
  unsigned int id;
  /* Type.  */
  struct ieee_write_type type;
  /* If this is a tag which has not yet been defined, this is the
     kind.  If the tag has been defined, this is DEBUG_KIND_ILLEGAL.  */
  enum debug_type_kind kind;
};

/* We use a hash table to associate names and types.  */

struct ieee_name_type_hash_table
{
  struct bfd_hash_table root;
};

struct ieee_name_type_hash_entry
{
  struct bfd_hash_entry root;
  /* Information for this name.  */
  struct ieee_name_type *types;
};

/* This is a list of enums.  */

struct ieee_defined_enum
{
  /* Next enum.  */
  struct ieee_defined_enum *next;
  /* Type index.  */
  unsigned int indx;
  /* Whether this enum has been defined.  */
  bfd_boolean defined;
  /* Tag.  */
  const char *tag;
  /* Names.  */
  const char **names;
  /* Values.  */
  bfd_signed_vma *vals;
};

/* We keep a list of modified versions of types, so that we don't
   output them more than once.  */

struct ieee_modified_type
{
  /* Pointer to this type.  */
  unsigned int pointer;
  /* Function with unknown arguments returning this type.  */
  unsigned int function;
  /* Const version of this type.  */
  unsigned int const_qualified;
  /* Volatile version of this type.  */
  unsigned int volatile_qualified;
  /* List of arrays of this type of various bounds.  */
  struct ieee_modified_array_type *arrays;
};

/* A list of arrays bounds.  */

struct ieee_modified_array_type
{
  /* Next array bounds.  */
  struct ieee_modified_array_type *next;
  /* Type index with these bounds.  */
  unsigned int indx;
  /* Low bound.  */
  bfd_signed_vma low;
  /* High bound.  */
  bfd_signed_vma high;
};

/* This is a list of pending function parameter information.  We don't
   output them until we see the first block.  */

struct ieee_pending_parm
{
  /* Next pending parameter.  */
  struct ieee_pending_parm *next;
  /* Name.  */
  const char *name;
  /* Type index.  */
  unsigned int type;
  /* Whether the type is a reference.  */
  bfd_boolean referencep;
  /* Kind.  */
  enum debug_parm_kind kind;
  /* Value.  */
  bfd_vma val;
};

/* This is the handle passed down by debug_write.  */

struct ieee_handle
{
  /* BFD we are writing to.  */
  bfd *abfd;
  /* Whether we got an error in a subroutine called via traverse or
     map_over_sections.  */
  bfd_boolean error;
  /* Current data buffer list.  */
  struct ieee_buflist *current;
  /* Current data buffer.  */
  struct ieee_buf *curbuf;
  /* Filename of current compilation unit.  */
  const char *filename;
  /* Module name of current compilation unit.  */
  const char *modname;
  /* List of buffer for global types.  */
  struct ieee_buflist global_types;
  /* List of finished data buffers.  */
  struct ieee_buflist data;
  /* List of buffers for typedefs in the current compilation unit.  */
  struct ieee_buflist types;
  /* List of buffers for variables and functions in the current
     compilation unit.  */
  struct ieee_buflist vars;
  /* List of buffers for C++ class definitions in the current
     compilation unit.  */
  struct ieee_buflist cxx;
  /* List of buffers for line numbers in the current compilation unit.  */
  struct ieee_buflist linenos;
  /* Ranges for the current compilation unit.  */
  struct ieee_range *ranges;
  /* Ranges for all debugging information.  */
  struct ieee_range *global_ranges;
  /* Nested pending ranges.  */
  struct ieee_range *pending_ranges;
  /* Type stack.  */
  struct ieee_type_stack *type_stack;
  /* Next unallocated type index.  */
  unsigned int type_indx;
  /* Next unallocated name index.  */
  unsigned int name_indx;
  /* Typedefs.  */
  struct ieee_name_type_hash_table typedefs;
  /* Tags.  */
  struct ieee_name_type_hash_table tags;
  /* Enums.  */
  struct ieee_defined_enum *enums;
  /* Modified versions of types.  */
  struct ieee_modified_type *modified;
  /* Number of entries allocated in modified.  */
  unsigned int modified_alloc;
  /* 4 byte complex type.  */
  unsigned int complex_float_index;
  /* 8 byte complex type.  */
  unsigned int complex_double_index;
  /* The depth of block nesting.  This is 0 outside a function, and 1
     just after start_function is called.  */
  unsigned int block_depth;
  /* The name of the current function.  */
  const char *fnname;
  /* List of buffers for the type of the function we are currently
     writing out.  */
  struct ieee_buflist fntype;
  /* List of buffers for the parameters of the function we are
     currently writing out.  */
  struct ieee_buflist fnargs;
  /* Number of arguments written to fnargs.  */
  unsigned int fnargcount;
  /* Pending function parameters.  */
  struct ieee_pending_parm *pending_parms;
  /* Current line number filename.  */
  const char *lineno_filename;
  /* Line number name index.  */
  unsigned int lineno_name_indx;
  /* Filename of pending line number.  */
  const char *pending_lineno_filename;
  /* Pending line number.  */
  unsigned long pending_lineno;
  /* Address of pending line number.  */
  bfd_vma pending_lineno_addr;
  /* Highest address seen at end of procedure.  */
  bfd_vma highaddr;
};

static bfd_boolean ieee_init_buffer
  (struct ieee_handle *, struct ieee_buflist *);
static bfd_boolean ieee_change_buffer
  (struct ieee_handle *, struct ieee_buflist *);
static bfd_boolean ieee_append_buffer
  (struct ieee_handle *, struct ieee_buflist *, struct ieee_buflist *);
static bfd_boolean ieee_real_write_byte (struct ieee_handle *, int);
static bfd_boolean ieee_write_2bytes (struct ieee_handle *, int);
static bfd_boolean ieee_write_number (struct ieee_handle *, bfd_vma);
static bfd_boolean ieee_write_id (struct ieee_handle *, const char *);
static bfd_boolean ieee_write_asn
  (struct ieee_handle *, unsigned int, bfd_vma);
static bfd_boolean ieee_write_atn65
  (struct ieee_handle *, unsigned int, const char *);
static bfd_boolean ieee_push_type
  (struct ieee_handle *, unsigned int, unsigned int, bfd_boolean,
   bfd_boolean);
static unsigned int ieee_pop_type (struct ieee_handle *);
static void ieee_pop_unused_type (struct ieee_handle *);
static unsigned int ieee_pop_type_used (struct ieee_handle *, bfd_boolean);
static bfd_boolean ieee_add_range
  (struct ieee_handle *, bfd_boolean, bfd_vma, bfd_vma);
static bfd_boolean ieee_start_range (struct ieee_handle *, bfd_vma);
static bfd_boolean ieee_end_range (struct ieee_handle *, bfd_vma);
static bfd_boolean ieee_define_type
  (struct ieee_handle *, unsigned int, bfd_boolean, bfd_boolean);
static bfd_boolean ieee_define_named_type
  (struct ieee_handle *, const char *, unsigned int, unsigned int,
   bfd_boolean, bfd_boolean, struct ieee_buflist *);
static struct ieee_modified_type *ieee_get_modified_info
  (struct ieee_handle *, unsigned int);
static struct bfd_hash_entry *ieee_name_type_newfunc
  (struct bfd_hash_entry *, struct bfd_hash_table *, const char *);
static bfd_boolean ieee_write_undefined_tag
  (struct ieee_name_type_hash_entry *, void *);
static bfd_boolean ieee_finish_compilation_unit (struct ieee_handle *);
static void ieee_add_bb11_blocks (bfd *, asection *, void *);
static bfd_boolean ieee_add_bb11
  (struct ieee_handle *, asection *, bfd_vma, bfd_vma);
static bfd_boolean ieee_output_pending_parms (struct ieee_handle *);
static unsigned int ieee_vis_to_flags (enum debug_visibility);
static bfd_boolean ieee_class_method_var
  (struct ieee_handle *, const char *, enum debug_visibility, bfd_boolean,
   bfd_boolean, bfd_boolean, bfd_vma, bfd_boolean);

static bfd_boolean ieee_start_compilation_unit (void *, const char *);
static bfd_boolean ieee_start_source (void *, const char *);
static bfd_boolean ieee_empty_type (void *);
static bfd_boolean ieee_void_type (void *);
static bfd_boolean ieee_int_type (void *, unsigned int, bfd_boolean);
static bfd_boolean ieee_float_type (void *, unsigned int);
static bfd_boolean ieee_complex_type (void *, unsigned int);
static bfd_boolean ieee_bool_type (void *, unsigned int);
static bfd_boolean ieee_enum_type
  (void *, const char *, const char **, bfd_signed_vma *);
static bfd_boolean ieee_pointer_type (void *);
static bfd_boolean ieee_function_type (void *, int, bfd_boolean);
static bfd_boolean ieee_reference_type (void *);
static bfd_boolean ieee_range_type (void *, bfd_signed_vma, bfd_signed_vma);
static bfd_boolean ieee_array_type
  (void *, bfd_signed_vma, bfd_signed_vma, bfd_boolean);
static bfd_boolean ieee_set_type (void *, bfd_boolean);
static bfd_boolean ieee_offset_type (void *);
static bfd_boolean ieee_method_type (void *, bfd_boolean, int, bfd_boolean);
static bfd_boolean ieee_const_type (void *);
static bfd_boolean ieee_volatile_type (void *);
static bfd_boolean ieee_start_struct_type
  (void *, const char *, unsigned int, bfd_boolean, unsigned int);
static bfd_boolean ieee_struct_field
  (void *, const char *, bfd_vma, bfd_vma, enum debug_visibility);
static bfd_boolean ieee_end_struct_type (void *);
static bfd_boolean ieee_start_class_type
  (void *, const char *, unsigned int, bfd_boolean, unsigned int, bfd_boolean,
   bfd_boolean);
static bfd_boolean ieee_class_static_member
  (void *, const char *, const char *, enum debug_visibility);
static bfd_boolean ieee_class_baseclass
  (void *, bfd_vma, bfd_boolean, enum debug_visibility);
static bfd_boolean ieee_class_start_method (void *, const char *);
static bfd_boolean ieee_class_method_variant
  (void *, const char *, enum debug_visibility, bfd_boolean, bfd_boolean,
   bfd_vma, bfd_boolean);
static bfd_boolean ieee_class_static_method_variant
  (void *, const char *, enum debug_visibility, bfd_boolean, bfd_boolean);
static bfd_boolean ieee_class_end_method (void *);
static bfd_boolean ieee_end_class_type (void *);
static bfd_boolean ieee_typedef_type (void *, const char *);
static bfd_boolean ieee_tag_type
  (void *, const char *, unsigned int, enum debug_type_kind);
static bfd_boolean ieee_typdef (void *, const char *);
static bfd_boolean ieee_tag (void *, const char *);
static bfd_boolean ieee_int_constant (void *, const char *, bfd_vma);
static bfd_boolean ieee_float_constant (void *, const char *, double);
static bfd_boolean ieee_typed_constant (void *, const char *, bfd_vma);
static bfd_boolean ieee_variable
  (void *, const char *, enum debug_var_kind, bfd_vma);
static bfd_boolean ieee_start_function (void *, const char *, bfd_boolean);
static bfd_boolean ieee_function_parameter
  (void *, const char *, enum debug_parm_kind, bfd_vma);
static bfd_boolean ieee_start_block (void *, bfd_vma);
static bfd_boolean ieee_end_block (void *, bfd_vma);
static bfd_boolean ieee_end_function (void *);
static bfd_boolean ieee_lineno (void *, const char *, unsigned long, bfd_vma);

static const struct debug_write_fns ieee_fns =
{
  ieee_start_compilation_unit,
  ieee_start_source,
  ieee_empty_type,
  ieee_void_type,
  ieee_int_type,
  ieee_float_type,
  ieee_complex_type,
  ieee_bool_type,
  ieee_enum_type,
  ieee_pointer_type,
  ieee_function_type,
  ieee_reference_type,
  ieee_range_type,
  ieee_array_type,
  ieee_set_type,
  ieee_offset_type,
  ieee_method_type,
  ieee_const_type,
  ieee_volatile_type,
  ieee_start_struct_type,
  ieee_struct_field,
  ieee_end_struct_type,
  ieee_start_class_type,
  ieee_class_static_member,
  ieee_class_baseclass,
  ieee_class_start_method,
  ieee_class_method_variant,
  ieee_class_static_method_variant,
  ieee_class_end_method,
  ieee_end_class_type,
  ieee_typedef_type,
  ieee_tag_type,
  ieee_typdef,
  ieee_tag,
  ieee_int_constant,
  ieee_float_constant,
  ieee_typed_constant,
  ieee_variable,
  ieee_start_function,
  ieee_function_parameter,
  ieee_start_block,
  ieee_end_block,
  ieee_end_function,
  ieee_lineno
};

/* Initialize a buffer to be empty.  */

static bfd_boolean
ieee_init_buffer (struct ieee_handle *info ATTRIBUTE_UNUSED,
		  struct ieee_buflist *buflist)
{
  buflist->head = NULL;
  buflist->tail = NULL;
  return TRUE;
}

/* See whether a buffer list has any data.  */

#define ieee_buffer_emptyp(buflist) ((buflist)->head == NULL)

/* Change the current buffer to a specified buffer chain.  */

static bfd_boolean
ieee_change_buffer (struct ieee_handle *info, struct ieee_buflist *buflist)
{
  if (buflist->head == NULL)
    {
      struct ieee_buf *buf;

      buf = (struct ieee_buf *) xmalloc (sizeof *buf);
      buf->next = NULL;
      buf->c = 0;
      buflist->head = buf;
      buflist->tail = buf;
    }

  info->current = buflist;
  info->curbuf = buflist->tail;

  return TRUE;
}

/* Append a buffer chain.  */

static bfd_boolean
ieee_append_buffer (struct ieee_handle *info ATTRIBUTE_UNUSED,
		    struct ieee_buflist *mainbuf,
		    struct ieee_buflist *newbuf)
{
  if (newbuf->head != NULL)
    {
      if (mainbuf->head == NULL)
	mainbuf->head = newbuf->head;
      else
	mainbuf->tail->next = newbuf->head;
      mainbuf->tail = newbuf->tail;
    }
  return TRUE;
}

/* Write a byte into the buffer.  We use a macro for speed and a
   function for the complex cases.  */

#define ieee_write_byte(info, b)				\
  ((info)->curbuf->c < IEEE_BUFSIZE				\
   ? ((info)->curbuf->buf[(info)->curbuf->c++] = (b), TRUE)	\
   : ieee_real_write_byte ((info), (b)))

static bfd_boolean
ieee_real_write_byte (struct ieee_handle *info, int b)
{
  if (info->curbuf->c >= IEEE_BUFSIZE)
    {
      struct ieee_buf *n;

      n = (struct ieee_buf *) xmalloc (sizeof *n);
      n->next = NULL;
      n->c = 0;
      if (info->current->head == NULL)
	info->current->head = n;
      else
	info->current->tail->next = n;
      info->current->tail = n;
      info->curbuf = n;
    }

  info->curbuf->buf[info->curbuf->c] = b;
  ++info->curbuf->c;

  return TRUE;
}

/* Write out two bytes.  */

static bfd_boolean
ieee_write_2bytes (struct ieee_handle *info, int i)
{
  return (ieee_write_byte (info, i >> 8)
	  && ieee_write_byte (info, i & 0xff));
}

/* Write out an integer.  */

static bfd_boolean
ieee_write_number (struct ieee_handle *info, bfd_vma v)
{
  bfd_vma t;
  bfd_byte ab[20];
  bfd_byte *p;
  unsigned int c;

  if (v <= (bfd_vma) ieee_number_end_enum)
    return ieee_write_byte (info, (int) v);

  t = v;
  p = ab + sizeof ab;
  while (t != 0)
    {
      *--p = t & 0xff;
      t >>= 8;
    }
  c = (ab + 20) - p;

  if (c > (unsigned int) (ieee_number_repeat_end_enum
			  - ieee_number_repeat_start_enum))
    {
      fprintf (stderr, _("IEEE numeric overflow: 0x"));
      fprintf_vma (stderr, v);
      fprintf (stderr, "\n");
      return FALSE;
    }

  if (! ieee_write_byte (info, (int) ieee_number_repeat_start_enum + c))
    return FALSE;
  for (; c > 0; --c, ++p)
    {
      if (! ieee_write_byte (info, *p))
	return FALSE;
    }

  return TRUE;
}

/* Write out a string.  */

static bfd_boolean
ieee_write_id (struct ieee_handle *info, const char *s)
{
  unsigned int len;

  len = strlen (s);
  if (len <= 0x7f)
    {
      if (! ieee_write_byte (info, len))
	return FALSE;
    }
  else if (len <= 0xff)
    {
      if (! ieee_write_byte (info, (int) ieee_extension_length_1_enum)
	  || ! ieee_write_byte (info, len))
	return FALSE;
    }
  else if (len <= 0xffff)
    {
      if (! ieee_write_byte (info, (int) ieee_extension_length_2_enum)
	  || ! ieee_write_2bytes (info, len))
	return FALSE;
    }
  else
    {
      fprintf (stderr, _("IEEE string length overflow: %u\n"), len);
      return FALSE;
    }

  for (; *s != '\0'; s++)
    if (! ieee_write_byte (info, *s))
      return FALSE;

  return TRUE;
}

/* Write out an ASN record.  */

static bfd_boolean
ieee_write_asn (struct ieee_handle *info, unsigned int indx, bfd_vma val)
{
  return (ieee_write_2bytes (info, (int) ieee_asn_record_enum)
	  && ieee_write_number (info, indx)
	  && ieee_write_number (info, val));
}

/* Write out an ATN65 record.  */

static bfd_boolean
ieee_write_atn65 (struct ieee_handle *info, unsigned int indx, const char *s)
{
  return (ieee_write_2bytes (info, (int) ieee_atn_record_enum)
	  && ieee_write_number (info, indx)
	  && ieee_write_number (info, 0)
	  && ieee_write_number (info, 65)
	  && ieee_write_id (info, s));
}

/* Push a type index onto the type stack.  */

static bfd_boolean
ieee_push_type (struct ieee_handle *info, unsigned int indx,
		unsigned int size, bfd_boolean unsignedp, bfd_boolean localp)
{
  struct ieee_type_stack *ts;

  ts = (struct ieee_type_stack *) xmalloc (sizeof *ts);
  memset (ts, 0, sizeof *ts);

  ts->type.indx = indx;
  ts->type.size = size;
  ts->type.unsignedp = unsignedp;
  ts->type.localp = localp;

  ts->next = info->type_stack;
  info->type_stack = ts;

  return TRUE;
}

/* Pop a type index off the type stack.  */

static unsigned int
ieee_pop_type (struct ieee_handle *info)
{
  return ieee_pop_type_used (info, TRUE);
}

/* Pop an unused type index off the type stack.  */

static void
ieee_pop_unused_type (struct ieee_handle *info)
{
  (void) ieee_pop_type_used (info, FALSE);
}

/* Pop a used or unused type index off the type stack.  */

static unsigned int
ieee_pop_type_used (struct ieee_handle *info, bfd_boolean used)
{
  struct ieee_type_stack *ts;
  unsigned int ret;

  ts = info->type_stack;
  assert (ts != NULL);

  /* If this is a function type, and we need it, we need to append the
     actual definition to the typedef block now.  */
  if (used && ! ieee_buffer_emptyp (&ts->type.fndef))
    {
      struct ieee_buflist *buflist;

      if (ts->type.localp)
	{
	  /* Make sure we have started the types block.  */
	  if (ieee_buffer_emptyp (&info->types))
	    {
	      if (! ieee_change_buffer (info, &info->types)
		  || ! ieee_write_byte (info, (int) ieee_bb_record_enum)
		  || ! ieee_write_byte (info, 1)
		  || ! ieee_write_number (info, 0)
		  || ! ieee_write_id (info, info->modname))
		return FALSE;
	    }
	  buflist = &info->types;
	}
      else
	{
	  /* Make sure we started the global type block.  */
	  if (ieee_buffer_emptyp (&info->global_types))
	    {
	      if (! ieee_change_buffer (info, &info->global_types)
		  || ! ieee_write_byte (info, (int) ieee_bb_record_enum)
		  || ! ieee_write_byte (info, 2)
		  || ! ieee_write_number (info, 0)
		  || ! ieee_write_id (info, ""))
		return FALSE;
	    }
	  buflist = &info->global_types;
	}

      if (! ieee_append_buffer (info, buflist, &ts->type.fndef))
	return FALSE;
    }

  ret = ts->type.indx;
  info->type_stack = ts->next;
  free (ts);
  return ret;
}

/* Add a range of bytes included in the current compilation unit.  */

static bfd_boolean
ieee_add_range (struct ieee_handle *info, bfd_boolean global, bfd_vma low,
		bfd_vma high)
{
  struct ieee_range **plist, *r, **pr;

  if (low == (bfd_vma) -1 || high == (bfd_vma) -1 || low == high)
    return TRUE;

  if (global)
    plist = &info->global_ranges;
  else
    plist = &info->ranges;

  for (r = *plist; r != NULL; r = r->next)
    {
      if (high >= r->low && low <= r->high)
	{
	  /* The new range overlaps r.  */
	  if (low < r->low)
	    r->low = low;
	  if (high > r->high)
	    r->high = high;
	  pr = &r->next;
	  while (*pr != NULL && (*pr)->low <= r->high)
	    {
	      struct ieee_range *n;

	      if ((*pr)->high > r->high)
		r->high = (*pr)->high;
	      n = (*pr)->next;
	      free (*pr);
	      *pr = n;
	    }
	  return TRUE;
	}
    }

  r = (struct ieee_range *) xmalloc (sizeof *r);
  memset (r, 0, sizeof *r);

  r->low = low;
  r->high = high;

  /* Store the ranges sorted by address.  */
  for (pr = plist; *pr != NULL; pr = &(*pr)->next)
    if ((*pr)->low > high)
      break;
  r->next = *pr;
  *pr = r;

  return TRUE;
}

/* Start a new range for which we only have the low address.  */

static bfd_boolean
ieee_start_range (struct ieee_handle *info, bfd_vma low)
{
  struct ieee_range *r;

  r = (struct ieee_range *) xmalloc (sizeof *r);
  memset (r, 0, sizeof *r);
  r->low = low;
  r->next = info->pending_ranges;
  info->pending_ranges = r;
  return TRUE;
}

/* Finish a range started by ieee_start_range.  */

static bfd_boolean
ieee_end_range (struct ieee_handle *info, bfd_vma high)
{
  struct ieee_range *r;
  bfd_vma low;

  assert (info->pending_ranges != NULL);
  r = info->pending_ranges;
  low = r->low;
  info->pending_ranges = r->next;
  free (r);
  return ieee_add_range (info, FALSE, low, high);
}

/* Start defining a type.  */

static bfd_boolean
ieee_define_type (struct ieee_handle *info, unsigned int size,
		  bfd_boolean unsignedp, bfd_boolean localp)
{
  return ieee_define_named_type (info, (const char *) NULL,
				 (unsigned int) -1, size, unsignedp,
				 localp, (struct ieee_buflist *) NULL);
}

/* Start defining a named type.  */

static bfd_boolean
ieee_define_named_type (struct ieee_handle *info, const char *name,
			unsigned int indx, unsigned int size,
			bfd_boolean unsignedp, bfd_boolean localp,
			struct ieee_buflist *buflist)
{
  unsigned int type_indx;
  unsigned int name_indx;

  if (indx != (unsigned int) -1)
    type_indx = indx;
  else
    {
      type_indx = info->type_indx;
      ++info->type_indx;
    }

  name_indx = info->name_indx;
  ++info->name_indx;

  if (name == NULL)
    name = "";

  /* If we were given a buffer, use it; otherwise, use either the
     local or the global type information, and make sure that the type
     block is started.  */
  if (buflist != NULL)
    {
      if (! ieee_change_buffer (info, buflist))
	return FALSE;
    }
  else if (localp)
    {
      if (! ieee_buffer_emptyp (&info->types))
	{
	  if (! ieee_change_buffer (info, &info->types))
	    return FALSE;
	}
      else
	{
	  if (! ieee_change_buffer (info, &info->types)
	      || ! ieee_write_byte (info, (int) ieee_bb_record_enum)
	      || ! ieee_write_byte (info, 1)
	      || ! ieee_write_number (info, 0)
	      || ! ieee_write_id (info, info->modname))
	    return FALSE;
	}
    }
  else
    {
      if (! ieee_buffer_emptyp (&info->global_types))
	{
	  if (! ieee_change_buffer (info, &info->global_types))
	    return FALSE;
	}
      else
	{
	  if (! ieee_change_buffer (info, &info->global_types)
	      || ! ieee_write_byte (info, (int) ieee_bb_record_enum)
	      || ! ieee_write_byte (info, 2)
	      || ! ieee_write_number (info, 0)
	      || ! ieee_write_id (info, ""))
	    return FALSE;
	}
    }

  /* Push the new type on the type stack, write out an NN record, and
     write out the start of a TY record.  The caller will then finish
     the TY record.  */
  if (! ieee_push_type (info, type_indx, size, unsignedp, localp))
    return FALSE;

  return (ieee_write_byte (info, (int) ieee_nn_record)
	  && ieee_write_number (info, name_indx)
	  && ieee_write_id (info, name)
	  && ieee_write_byte (info, (int) ieee_ty_record_enum)
	  && ieee_write_number (info, type_indx)
	  && ieee_write_byte (info, 0xce)
	  && ieee_write_number (info, name_indx));
}

/* Get an entry to the list of modified versions of a type.  */

static struct ieee_modified_type *
ieee_get_modified_info (struct ieee_handle *info, unsigned int indx)
{
  if (indx >= info->modified_alloc)
    {
      unsigned int nalloc;

      nalloc = info->modified_alloc;
      if (nalloc == 0)
	nalloc = 16;
      while (indx >= nalloc)
	nalloc *= 2;
      info->modified = ((struct ieee_modified_type *)
			xrealloc (info->modified,
				  nalloc * sizeof *info->modified));
      memset (info->modified + info->modified_alloc, 0,
	      (nalloc - info->modified_alloc) * sizeof *info->modified);
      info->modified_alloc = nalloc;
    }

  return info->modified + indx;
}

/* Routines for the hash table mapping names to types.  */

/* Initialize an entry in the hash table.  */

static struct bfd_hash_entry *
ieee_name_type_newfunc (struct bfd_hash_entry *entry,
			struct bfd_hash_table *table, const char *string)
{
  struct ieee_name_type_hash_entry *ret =
    (struct ieee_name_type_hash_entry *) entry;

  /* Allocate the structure if it has not already been allocated by a
     subclass.  */
  if (ret == NULL)
    ret = ((struct ieee_name_type_hash_entry *)
	   bfd_hash_allocate (table, sizeof *ret));
  if (ret == NULL)
    return NULL;

  /* Call the allocation method of the superclass.  */
  ret = ((struct ieee_name_type_hash_entry *)
	 bfd_hash_newfunc ((struct bfd_hash_entry *) ret, table, string));
  if (ret)
    {
      /* Set local fields.  */
      ret->types = NULL;
    }

  return (struct bfd_hash_entry *) ret;
}

/* Look up an entry in the hash table.  */

#define ieee_name_type_hash_lookup(table, string, create, copy) \
  ((struct ieee_name_type_hash_entry *) \
   bfd_hash_lookup (&(table)->root, (string), (create), (copy)))

/* Traverse the hash table.  */

#define ieee_name_type_hash_traverse(table, func, info)			\
  (bfd_hash_traverse							\
   (&(table)->root,							\
    (bfd_boolean (*) (struct bfd_hash_entry *, void *)) (func),		\
    (info)))

/* The general routine to write out IEEE debugging information.  */

bfd_boolean
write_ieee_debugging_info (bfd *abfd, void *dhandle)
{
  struct ieee_handle info;
  asection *s;
  const char *err;
  struct ieee_buf *b;

  memset (&info, 0, sizeof info);
  info.abfd = abfd;
  info.type_indx = 256;
  info.name_indx = 32;

  if (!bfd_hash_table_init (&info.typedefs.root, ieee_name_type_newfunc,
			    sizeof (struct ieee_name_type_hash_entry))
      || !bfd_hash_table_init (&info.tags.root, ieee_name_type_newfunc,
			       sizeof (struct ieee_name_type_hash_entry)))
    return FALSE;

  if (! ieee_init_buffer (&info, &info.global_types)
      || ! ieee_init_buffer (&info, &info.data)
      || ! ieee_init_buffer (&info, &info.types)
      || ! ieee_init_buffer (&info, &info.vars)
      || ! ieee_init_buffer (&info, &info.cxx)
      || ! ieee_init_buffer (&info, &info.linenos)
      || ! ieee_init_buffer (&info, &info.fntype)
      || ! ieee_init_buffer (&info, &info.fnargs))
    return FALSE;

  if (! debug_write (dhandle, &ieee_fns, (void *) &info))
    return FALSE;

  if (info.filename != NULL)
    {
      if (! ieee_finish_compilation_unit (&info))
	return FALSE;
    }

  /* Put any undefined tags in the global typedef information.  */
  info.error = FALSE;
  ieee_name_type_hash_traverse (&info.tags,
				ieee_write_undefined_tag,
				(void *) &info);
  if (info.error)
    return FALSE;

  /* Prepend the global typedef information to the other data.  */
  if (! ieee_buffer_emptyp (&info.global_types))
    {
      /* The HP debugger seems to have a bug in which it ignores the
         last entry in the global types, so we add a dummy entry.  */
      if (! ieee_change_buffer (&info, &info.global_types)
	  || ! ieee_write_byte (&info, (int) ieee_nn_record)
	  || ! ieee_write_number (&info, info.name_indx)
	  || ! ieee_write_id (&info, "")
	  || ! ieee_write_byte (&info, (int) ieee_ty_record_enum)
	  || ! ieee_write_number (&info, info.type_indx)
	  || ! ieee_write_byte (&info, 0xce)
	  || ! ieee_write_number (&info, info.name_indx)
	  || ! ieee_write_number (&info, 'P')
	  || ! ieee_write_number (&info, (int) builtin_void + 32)
	  || ! ieee_write_byte (&info, (int) ieee_be_record_enum))
	return FALSE;

      if (! ieee_append_buffer (&info, &info.global_types, &info.data))
	return FALSE;
      info.data = info.global_types;
    }

  /* Make sure that we have declare BB11 blocks for each range in the
     file.  They are added to info->vars.  */
  info.error = FALSE;
  if (! ieee_init_buffer (&info, &info.vars))
    return FALSE;
  bfd_map_over_sections (abfd, ieee_add_bb11_blocks, (void *) &info);
  if (info.error)
    return FALSE;
  if (! ieee_buffer_emptyp (&info.vars))
    {
      if (! ieee_change_buffer (&info, &info.vars)
	  || ! ieee_write_byte (&info, (int) ieee_be_record_enum))
	return FALSE;

      if (! ieee_append_buffer (&info, &info.data, &info.vars))
	return FALSE;
    }

  /* Now all the data is in info.data.  Write it out to the BFD.  We
     normally would need to worry about whether all the other sections
     are set up yet, but the IEEE backend will handle this particular
     case correctly regardless.  */
  if (ieee_buffer_emptyp (&info.data))
    {
      /* There is no debugging information.  */
      return TRUE;
    }
  err = NULL;
  s = bfd_make_section (abfd, ".debug");
  if (s == NULL)
    err = "bfd_make_section";
  if (err == NULL)
    {
      if (! bfd_set_section_flags (abfd, s, SEC_DEBUGGING | SEC_HAS_CONTENTS))
	err = "bfd_set_section_flags";
    }
  if (err == NULL)
    {
      bfd_size_type size;

      size = 0;
      for (b = info.data.head; b != NULL; b = b->next)
	size += b->c;
      if (! bfd_set_section_size (abfd, s, size))
	err = "bfd_set_section_size";
    }
  if (err == NULL)
    {
      file_ptr offset;

      offset = 0;
      for (b = info.data.head; b != NULL; b = b->next)
	{
	  if (! bfd_set_section_contents (abfd, s, b->buf, offset, b->c))
	    {
	      err = "bfd_set_section_contents";
	      break;
	    }
	  offset += b->c;
	}
    }

  if (err != NULL)
    {
      fprintf (stderr, "%s: %s: %s\n", bfd_get_filename (abfd), err,
	       bfd_errmsg (bfd_get_error ()));
      return FALSE;
    }

  bfd_hash_table_free (&info.typedefs.root);
  bfd_hash_table_free (&info.tags.root);

  return TRUE;
}

/* Write out information for an undefined tag.  This is called via
   ieee_name_type_hash_traverse.  */

static bfd_boolean
ieee_write_undefined_tag (struct ieee_name_type_hash_entry *h, void *p)
{
  struct ieee_handle *info = (struct ieee_handle *) p;
  struct ieee_name_type *nt;

  for (nt = h->types; nt != NULL; nt = nt->next)
    {
      unsigned int name_indx;
      char code;

      if (nt->kind == DEBUG_KIND_ILLEGAL)
	continue;

      if (ieee_buffer_emptyp (&info->global_types))
	{
	  if (! ieee_change_buffer (info, &info->global_types)
	      || ! ieee_write_byte (info, (int) ieee_bb_record_enum)
	      || ! ieee_write_byte (info, 2)
	      || ! ieee_write_number (info, 0)
	      || ! ieee_write_id (info, ""))
	    {
	      info->error = TRUE;
	      return FALSE;
	    }
	}
      else
	{
	  if (! ieee_change_buffer (info, &info->global_types))
	    {
	      info->error = TRUE;
	      return FALSE;
	    }
	}

      name_indx = info->name_indx;
      ++info->name_indx;
      if (! ieee_write_byte (info, (int) ieee_nn_record)
	  || ! ieee_write_number (info, name_indx)
	  || ! ieee_write_id (info, nt->type.name)
	  || ! ieee_write_byte (info, (int) ieee_ty_record_enum)
	  || ! ieee_write_number (info, nt->type.indx)
	  || ! ieee_write_byte (info, 0xce)
	  || ! ieee_write_number (info, name_indx))
	{
	  info->error = TRUE;
	  return FALSE;
	}

      switch (nt->kind)
	{
	default:
	  abort ();
	  info->error = TRUE;
	  return FALSE;
	case DEBUG_KIND_STRUCT:
	case DEBUG_KIND_CLASS:
	  code = 'S';
	  break;
	case DEBUG_KIND_UNION:
	case DEBUG_KIND_UNION_CLASS:
	  code = 'U';
	  break;
	case DEBUG_KIND_ENUM:
	  code = 'E';
	  break;
	}
      if (! ieee_write_number (info, code)
	  || ! ieee_write_number (info, 0))
	{
	  info->error = TRUE;
	  return FALSE;
	}
    }

  return TRUE;
}

/* Start writing out information for a compilation unit.  */

static bfd_boolean
ieee_start_compilation_unit (void *p, const char *filename)
{
  struct ieee_handle *info = (struct ieee_handle *) p;
  const char *modname;
#ifdef HAVE_DOS_BASED_FILE_SYSTEM
  const char *backslash;
#endif
  char *c, *s;
  unsigned int nindx;

  if (info->filename != NULL)
    {
      if (! ieee_finish_compilation_unit (info))
	return FALSE;
    }

  info->filename = filename;
  modname = strrchr (filename, '/');
#ifdef HAVE_DOS_BASED_FILE_SYSTEM
  /* We could have a mixed forward/back slash case.  */
  backslash = strrchr (filename, '\\');
  if (modname == NULL || (backslash != NULL && backslash > modname))
    modname = backslash;
#endif

  if (modname != NULL)
    ++modname;
#ifdef HAVE_DOS_BASED_FILE_SYSTEM
  else if (filename[0] && filename[1] == ':')
    modname = filename + 2;
#endif
  else
    modname = filename;

  c = xstrdup (modname);
  s = strrchr (c, '.');
  if (s != NULL)
    *s = '\0';
  info->modname = c;

  if (! ieee_init_buffer (info, &info->types)
      || ! ieee_init_buffer (info, &info->vars)
      || ! ieee_init_buffer (info, &info->cxx)
      || ! ieee_init_buffer (info, &info->linenos))
    return FALSE;
  info->ranges = NULL;

  /* Always include a BB1 and a BB3 block.  That is what the output of
     the MRI linker seems to look like.  */
  if (! ieee_change_buffer (info, &info->types)
      || ! ieee_write_byte (info, (int) ieee_bb_record_enum)
      || ! ieee_write_byte (info, 1)
      || ! ieee_write_number (info, 0)
      || ! ieee_write_id (info, info->modname))
    return FALSE;

  nindx = info->name_indx;
  ++info->name_indx;
  if (! ieee_change_buffer (info, &info->vars)
      || ! ieee_write_byte (info, (int) ieee_bb_record_enum)
      || ! ieee_write_byte (info, 3)
      || ! ieee_write_number (info, 0)
      || ! ieee_write_id (info, info->modname))
    return FALSE;

  return TRUE;
}

/* Finish up a compilation unit.  */

static bfd_boolean
ieee_finish_compilation_unit (struct ieee_handle *info)
{
  struct ieee_range *r;

  if (! ieee_buffer_emptyp (&info->types))
    {
      if (! ieee_change_buffer (info, &info->types)
	  || ! ieee_write_byte (info, (int) ieee_be_record_enum))
	return FALSE;
    }

  if (! ieee_buffer_emptyp (&info->cxx))
    {
      /* Append any C++ information to the global function and
         variable information.  */
      assert (! ieee_buffer_emptyp (&info->vars));
      if (! ieee_change_buffer (info, &info->vars))
	return FALSE;

      /* We put the pmisc records in a dummy procedure, just as the
         MRI compiler does.  */
      if (! ieee_write_byte (info, (int) ieee_bb_record_enum)
	  || ! ieee_write_byte (info, 6)
	  || ! ieee_write_number (info, 0)
	  || ! ieee_write_id (info, "__XRYCPP")
	  || ! ieee_write_number (info, 0)
	  || ! ieee_write_number (info, 0)
	  || ! ieee_write_number (info, info->highaddr - 1)
	  || ! ieee_append_buffer (info, &info->vars, &info->cxx)
	  || ! ieee_change_buffer (info, &info->vars)
	  || ! ieee_write_byte (info, (int) ieee_be_record_enum)
	  || ! ieee_write_number (info, info->highaddr - 1))
	return FALSE;
    }

  if (! ieee_buffer_emptyp (&info->vars))
    {
      if (! ieee_change_buffer (info, &info->vars)
	  || ! ieee_write_byte (info, (int) ieee_be_record_enum))
	return FALSE;
    }

  if (info->pending_lineno_filename != NULL)
    {
      /* Force out the pending line number.  */
      if (! ieee_lineno ((void *) info, (const char *) NULL, 0, (bfd_vma) -1))
	return FALSE;
    }
  if (! ieee_buffer_emptyp (&info->linenos))
    {
      if (! ieee_change_buffer (info, &info->linenos)
	  || ! ieee_write_byte (info, (int) ieee_be_record_enum))
	return FALSE;
      if (strcmp (info->filename, info->lineno_filename) != 0)
	{
	  /* We were not in the main file.  We just closed the
             included line number block, and now we must close the
             main line number block.  */
	  if (! ieee_write_byte (info, (int) ieee_be_record_enum))
	    return FALSE;
	}
    }

  if (! ieee_append_buffer (info, &info->data, &info->types)
      || ! ieee_append_buffer (info, &info->data, &info->vars)
      || ! ieee_append_buffer (info, &info->data, &info->linenos))
    return FALSE;

  /* Build BB10/BB11 blocks based on the ranges we recorded.  */
  if (! ieee_change_buffer (info, &info->data))
    return FALSE;

  if (! ieee_write_byte (info, (int) ieee_bb_record_enum)
      || ! ieee_write_byte (info, 10)
      || ! ieee_write_number (info, 0)
      || ! ieee_write_id (info, info->modname)
      || ! ieee_write_id (info, "")
      || ! ieee_write_number (info, 0)
      || ! ieee_write_id (info, "GNU objcopy"))
    return FALSE;

  for (r = info->ranges; r != NULL; r = r->next)
    {
      bfd_vma low, high;
      asection *s;
      int kind;

      low = r->low;
      high = r->high;

      /* Find the section corresponding to this range.  */
      for (s = info->abfd->sections; s != NULL; s = s->next)
	{
	  if (bfd_get_section_vma (info->abfd, s) <= low
	      && high <= (bfd_get_section_vma (info->abfd, s)
			  + bfd_section_size (info->abfd, s)))
	    break;
	}

      if (s == NULL)
	{
	  /* Just ignore this range.  */
	  continue;
	}

      /* Coalesce ranges if it seems reasonable.  */
      while (r->next != NULL
	     && high + 0x1000 >= r->next->low
	     && (r->next->high
		 <= (bfd_get_section_vma (info->abfd, s)
		     + bfd_section_size (info->abfd, s))))
	{
	  r = r->next;
	  high = r->high;
	}

      if ((s->flags & SEC_CODE) != 0)
	kind = 1;
      else if ((s->flags & SEC_READONLY) != 0)
	kind = 3;
      else
	kind = 2;

      if (! ieee_write_byte (info, (int) ieee_bb_record_enum)
	  || ! ieee_write_byte (info, 11)
	  || ! ieee_write_number (info, 0)
	  || ! ieee_write_id (info, "")
	  || ! ieee_write_number (info, kind)
	  || ! ieee_write_number (info, s->index + IEEE_SECTION_NUMBER_BASE)
	  || ! ieee_write_number (info, low)
	  || ! ieee_write_byte (info, (int) ieee_be_record_enum)
	  || ! ieee_write_number (info, high - low))
	return FALSE;

      /* Add this range to the list of global ranges.  */
      if (! ieee_add_range (info, TRUE, low, high))
	return FALSE;
    }

  if (! ieee_write_byte (info, (int) ieee_be_record_enum))
    return FALSE;

  return TRUE;
}

/* Add BB11 blocks describing each range that we have not already
   described.  */

static void
ieee_add_bb11_blocks (bfd *abfd ATTRIBUTE_UNUSED, asection *sec, void *data)
{
  struct ieee_handle *info = (struct ieee_handle *) data;
  bfd_vma low, high;
  struct ieee_range *r;

  low = bfd_get_section_vma (abfd, sec);
  high = low + bfd_section_size (abfd, sec);

  /* Find the first range at or after this section.  The ranges are
     sorted by address.  */
  for (r = info->global_ranges; r != NULL; r = r->next)
    if (r->high > low)
      break;

  while (low < high)
    {
      if (r == NULL || r->low >= high)
	{
	  if (! ieee_add_bb11 (info, sec, low, high))
	    info->error = TRUE;
	  return;
	}

      if (low < r->low
	  && r->low - low > 0x100)
	{
	  if (! ieee_add_bb11 (info, sec, low, r->low))
	    {
	      info->error = TRUE;
	      return;
	    }
	}
      low = r->high;

      r = r->next;
    }
}

/* Add a single BB11 block for a range.  We add it to info->vars.  */

static bfd_boolean
ieee_add_bb11 (struct ieee_handle *info, asection *sec, bfd_vma low,
	       bfd_vma high)
{
  int kind;

  if (! ieee_buffer_emptyp (&info->vars))
    {
      if (! ieee_change_buffer (info, &info->vars))
	return FALSE;
    }
  else
    {
      const char *filename, *modname;
#ifdef HAVE_DOS_BASED_FILE_SYSTEM
      const char *backslash;
#endif
      char *c, *s;

      /* Start the enclosing BB10 block.  */
      filename = bfd_get_filename (info->abfd);
      modname = strrchr (filename, '/');
#ifdef HAVE_DOS_BASED_FILE_SYSTEM
      backslash = strrchr (filename, '\\');
      if (modname == NULL || (backslash != NULL && backslash > modname))
	modname = backslash;
#endif

      if (modname != NULL)
	++modname;
#ifdef HAVE_DOS_BASED_FILE_SYSTEM
      else if (filename[0] && filename[1] == ':')
	modname = filename + 2;
#endif
      else
	modname = filename;

      c = xstrdup (modname);
      s = strrchr (c, '.');
      if (s != NULL)
	*s = '\0';

      if (! ieee_change_buffer (info, &info->vars)
	  || ! ieee_write_byte (info, (int) ieee_bb_record_enum)
	  || ! ieee_write_byte (info, 10)
	  || ! ieee_write_number (info, 0)
	  || ! ieee_write_id (info, c)
	  || ! ieee_write_id (info, "")
	  || ! ieee_write_number (info, 0)
	  || ! ieee_write_id (info, "GNU objcopy"))
	return FALSE;

      free (c);
    }

  if ((sec->flags & SEC_CODE) != 0)
    kind = 1;
  else if ((sec->flags & SEC_READONLY) != 0)
    kind = 3;
  else
    kind = 2;

  if (! ieee_write_byte (info, (int) ieee_bb_record_enum)
      || ! ieee_write_byte (info, 11)
      || ! ieee_write_number (info, 0)
      || ! ieee_write_id (info, "")
      || ! ieee_write_number (info, kind)
      || ! ieee_write_number (info, sec->index + IEEE_SECTION_NUMBER_BASE)
      || ! ieee_write_number (info, low)
      || ! ieee_write_byte (info, (int) ieee_be_record_enum)
      || ! ieee_write_number (info, high - low))
    return FALSE;

  return TRUE;
}

/* Start recording information from a particular source file.  This is
   used to record which file defined which types, variables, etc.  It
   is not used for line numbers, since the lineno entry point passes
   down the file name anyhow.  IEEE debugging information doesn't seem
   to store this information anywhere.  */

static bfd_boolean
ieee_start_source (void *p ATTRIBUTE_UNUSED,
		   const char *filename ATTRIBUTE_UNUSED)
{
  return TRUE;
}

/* Make an empty type.  */

static bfd_boolean
ieee_empty_type (void *p)
{
  struct ieee_handle *info = (struct ieee_handle *) p;

  return ieee_push_type (info, (int) builtin_unknown, 0, FALSE, FALSE);
}

/* Make a void type.  */

static bfd_boolean
ieee_void_type (void *p)
{
  struct ieee_handle *info = (struct ieee_handle *) p;

  return ieee_push_type (info, (int) builtin_void, 0, FALSE, FALSE);
}

/* Make an integer type.  */

static bfd_boolean
ieee_int_type (void *p, unsigned int size, bfd_boolean unsignedp)
{
  struct ieee_handle *info = (struct ieee_handle *) p;
  unsigned int indx;

  switch (size)
    {
    case 1:
      indx = (int) builtin_signed_char;
      break;
    case 2:
      indx = (int) builtin_signed_short_int;
      break;
    case 4:
      indx = (int) builtin_signed_long;
      break;
    case 8:
      indx = (int) builtin_signed_long_long;
      break;
    default:
      fprintf (stderr, _("IEEE unsupported integer type size %u\n"), size);
      return FALSE;
    }

  if (unsignedp)
    ++indx;

  return ieee_push_type (info, indx, size, unsignedp, FALSE);
}

/* Make a floating point type.  */

static bfd_boolean
ieee_float_type (void *p, unsigned int size)
{
  struct ieee_handle *info = (struct ieee_handle *) p;
  unsigned int indx;

  switch (size)
    {
    case 4:
      indx = (int) builtin_float;
      break;
    case 8:
      indx = (int) builtin_double;
      break;
    case 12:
      /* FIXME: This size really depends upon the processor.  */
      indx = (int) builtin_long_double;
      break;
    case 16:
      indx = (int) builtin_long_long_double;
      break;
    default:
      fprintf (stderr, _("IEEE unsupported float type size %u\n"), size);
      return FALSE;
    }

  return ieee_push_type (info, indx, size, FALSE, FALSE);
}

/* Make a complex type.  */

static bfd_boolean
ieee_complex_type (void *p, unsigned int size)
{
  struct ieee_handle *info = (struct ieee_handle *) p;
  char code;

  switch (size)
    {
    case 4:
      if (info->complex_float_index != 0)
	return ieee_push_type (info, info->complex_float_index, size * 2,
			       FALSE, FALSE);
      code = 'c';
      break;
    case 12:
    case 16:
      /* These cases can be output by gcc -gstabs.  Outputting the
         wrong type is better than crashing.  */
    case 8:
      if (info->complex_double_index != 0)
	return ieee_push_type (info, info->complex_double_index, size * 2,
			       FALSE, FALSE);
      code = 'd';
      break;
    default:
      fprintf (stderr, _("IEEE unsupported complex type size %u\n"), size);
      return FALSE;
    }

  /* FIXME: I don't know what the string is for.  */
  if (! ieee_define_type (info, size * 2, FALSE, FALSE)
      || ! ieee_write_number (info, code)
      || ! ieee_write_id (info, ""))
    return FALSE;

  if (size == 4)
    info->complex_float_index = info->type_stack->type.indx;
  else
    info->complex_double_index = info->type_stack->type.indx;

  return TRUE;
}

/* Make a boolean type.  IEEE doesn't support these, so we just make
   an integer type instead.  */

static bfd_boolean
ieee_bool_type (void *p, unsigned int size)
{
  return ieee_int_type (p, size, TRUE);
}

/* Make an enumeration.  */

static bfd_boolean
ieee_enum_type (void *p, const char *tag, const char **names,
		bfd_signed_vma *vals)
{
  struct ieee_handle *info = (struct ieee_handle *) p;
  struct ieee_defined_enum *e;
  bfd_boolean localp, simple;
  unsigned int indx;
  int i = 0;

  localp = FALSE;
  indx = (unsigned int) -1;
  for (e = info->enums; e != NULL; e = e->next)
    {
      if (tag == NULL)
	{
	  if (e->tag != NULL)
	    continue;
	}
      else
	{
	  if (e->tag == NULL
	      || tag[0] != e->tag[0]
	      || strcmp (tag, e->tag) != 0)
	    continue;
	}

      if (! e->defined)
	{
	  /* This enum tag has been seen but not defined.  */
	  indx = e->indx;
	  break;
	}

      if (names != NULL && e->names != NULL)
	{
	  for (i = 0; names[i] != NULL && e->names[i] != NULL; i++)
	    {
	      if (names[i][0] != e->names[i][0]
		  || vals[i] != e->vals[i]
		  || strcmp (names[i], e->names[i]) != 0)
		break;
	    }
	}

      if ((names == NULL && e->names == NULL)
	  || (names != NULL
	      && e->names != NULL
	      && names[i] == NULL
	      && e->names[i] == NULL))
	{
	  /* We've seen this enum before.  */
	  return ieee_push_type (info, e->indx, 0, TRUE, FALSE);
	}

      if (tag != NULL)
	{
	  /* We've already seen an enum of the same name, so we must make
	     sure to output this one locally.  */
	  localp = TRUE;
	  break;
	}
    }

  /* If this is a simple enumeration, in which the values start at 0
     and always increment by 1, we can use type E.  Otherwise we must
     use type N.  */

  simple = TRUE;
  if (names != NULL)
    {
      for (i = 0; names[i] != NULL; i++)
	{
	  if (vals[i] != i)
	    {
	      simple = FALSE;
	      break;
	    }
	}
    }

  if (! ieee_define_named_type (info, tag, indx, 0, TRUE, localp,
				(struct ieee_buflist *) NULL)
      || ! ieee_write_number (info, simple ? 'E' : 'N'))
    return FALSE;
  if (simple)
    {
      /* FIXME: This is supposed to be the enumeration size, but we
         don't store that.  */
      if (! ieee_write_number (info, 4))
	return FALSE;
    }
  if (names != NULL)
    {
      for (i = 0; names[i] != NULL; i++)
	{
	  if (! ieee_write_id (info, names[i]))
	    return FALSE;
	  if (! simple)
	    {
	      if (! ieee_write_number (info, vals[i]))
		return FALSE;
	    }
	}
    }

  if (! localp)
    {
      if (indx == (unsigned int) -1)
	{
	  e = (struct ieee_defined_enum *) xmalloc (sizeof *e);
	  memset (e, 0, sizeof *e);
	  e->indx = info->type_stack->type.indx;
	  e->tag = tag;

	  e->next = info->enums;
	  info->enums = e;
	}

      e->names = names;
      e->vals = vals;
      e->defined = TRUE;
    }

  return TRUE;
}

/* Make a pointer type.  */

static bfd_boolean
ieee_pointer_type (void *p)
{
  struct ieee_handle *info = (struct ieee_handle *) p;
  bfd_boolean localp;
  unsigned int indx;
  struct ieee_modified_type *m = NULL;

  localp = info->type_stack->type.localp;
  indx = ieee_pop_type (info);

  /* A pointer to a simple builtin type can be obtained by adding 32.
     FIXME: Will this be a short pointer, and will that matter?  */
  if (indx < 32)
    return ieee_push_type (info, indx + 32, 0, TRUE, FALSE);

  if (! localp)
    {
      m = ieee_get_modified_info (p, indx);
      if (m == NULL)
	return FALSE;

      /* FIXME: The size should depend upon the architecture.  */
      if (m->pointer > 0)
	return ieee_push_type (info, m->pointer, 4, TRUE, FALSE);
    }

  if (! ieee_define_type (info, 4, TRUE, localp)
      || ! ieee_write_number (info, 'P')
      || ! ieee_write_number (info, indx))
    return FALSE;

  if (! localp)
    m->pointer = info->type_stack->type.indx;

  return TRUE;
}

/* Make a function type.  This will be called for a method, but we
   don't want to actually add it to the type table in that case.  We
   handle this by defining the type in a private buffer, and only
   adding that buffer to the typedef block if we are going to use it.  */

static bfd_boolean
ieee_function_type (void *p, int argcount, bfd_boolean varargs)
{
  struct ieee_handle *info = (struct ieee_handle *) p;
  bfd_boolean localp;
  unsigned int *args = NULL;
  int i;
  unsigned int retindx;
  struct ieee_buflist fndef;
  struct ieee_modified_type *m;

  localp = FALSE;

  if (argcount > 0)
    {
      args = (unsigned int *) xmalloc (argcount * sizeof *args);
      for (i = argcount - 1; i >= 0; i--)
	{
	  if (info->type_stack->type.localp)
	    localp = TRUE;
	  args[i] = ieee_pop_type (info);
	}
    }
  else if (argcount < 0)
    varargs = FALSE;

  if (info->type_stack->type.localp)
    localp = TRUE;
  retindx = ieee_pop_type (info);

  m = NULL;
  if (argcount < 0 && ! localp)
    {
      m = ieee_get_modified_info (p, retindx);
      if (m == NULL)
	return FALSE;

      if (m->function > 0)
	return ieee_push_type (info, m->function, 0, TRUE, FALSE);
    }

  /* An attribute of 0x41 means that the frame and push mask are
     unknown.  */
  if (! ieee_init_buffer (info, &fndef)
      || ! ieee_define_named_type (info, (const char *) NULL,
				   (unsigned int) -1, 0, TRUE, localp,
				   &fndef)
      || ! ieee_write_number (info, 'x')
      || ! ieee_write_number (info, 0x41)
      || ! ieee_write_number (info, 0)
      || ! ieee_write_number (info, 0)
      || ! ieee_write_number (info, retindx)
      || ! ieee_write_number (info, (bfd_vma) argcount + (varargs ? 1 : 0)))
    return FALSE;
  if (argcount > 0)
    {
      for (i = 0; i < argcount; i++)
	if (! ieee_write_number (info, args[i]))
	  return FALSE;
      free (args);
    }
  if (varargs)
    {
      /* A varargs function is represented by writing out the last
         argument as type void *, although this makes little sense.  */
      if (! ieee_write_number (info, (bfd_vma) builtin_void + 32))
	return FALSE;
    }

  if (! ieee_write_number (info, 0))
    return FALSE;

  /* We wrote the information into fndef, in case we don't need it.
     It will be appended to info->types by ieee_pop_type.  */
  info->type_stack->type.fndef = fndef;

  if (m != NULL)
    m->function = info->type_stack->type.indx;

  return TRUE;
}

/* Make a reference type.  */

static bfd_boolean
ieee_reference_type (void *p)
{
  struct ieee_handle *info = (struct ieee_handle *) p;

  /* IEEE appears to record a normal pointer type, and then use a
     pmisc record to indicate that it is really a reference.  */

  if (! ieee_pointer_type (p))
    return FALSE;
  info->type_stack->type.referencep = TRUE;
  return TRUE;
}

/* Make a range type.  */

static bfd_boolean
ieee_range_type (void *p, bfd_signed_vma low, bfd_signed_vma high)
{
  struct ieee_handle *info = (struct ieee_handle *) p;
  unsigned int size;
  bfd_boolean unsignedp, localp;

  size = info->type_stack->type.size;
  unsignedp = info->type_stack->type.unsignedp;
  localp = info->type_stack->type.localp;
  ieee_pop_unused_type (info);
  return (ieee_define_type (info, size, unsignedp, localp)
	  && ieee_write_number (info, 'R')
	  && ieee_write_number (info, (bfd_vma) low)
	  && ieee_write_number (info, (bfd_vma) high)
	  && ieee_write_number (info, unsignedp ? 0 : 1)
	  && ieee_write_number (info, size));
}

/* Make an array type.  */

static bfd_boolean
ieee_array_type (void *p, bfd_signed_vma low, bfd_signed_vma high,
		 bfd_boolean stringp ATTRIBUTE_UNUSED)
{
  struct ieee_handle *info = (struct ieee_handle *) p;
  unsigned int eleindx;
  bfd_boolean localp;
  unsigned int size;
  struct ieee_modified_type *m = NULL;
  struct ieee_modified_array_type *a;

  /* IEEE does not store the range, so we just ignore it.  */
  ieee_pop_unused_type (info);
  localp = info->type_stack->type.localp;
  size = info->type_stack->type.size;
  eleindx = ieee_pop_type (info);

  /* If we don't know the range, treat the size as exactly one
     element.  */
  if (low < high)
    size *= (high - low) + 1;

  if (! localp)
    {
      m = ieee_get_modified_info (info, eleindx);
      if (m == NULL)
	return FALSE;

      for (a = m->arrays; a != NULL; a = a->next)
	{
	  if (a->low == low && a->high == high)
	    return ieee_push_type (info, a->indx, size, FALSE, FALSE);
	}
    }

  if (! ieee_define_type (info, size, FALSE, localp)
      || ! ieee_write_number (info, low == 0 ? 'Z' : 'C')
      || ! ieee_write_number (info, eleindx))
    return FALSE;
  if (low != 0)
    {
      if (! ieee_write_number (info, low))
	return FALSE;
    }

  if (! ieee_write_number (info, high + 1))
    return FALSE;

  if (! localp)
    {
      a = (struct ieee_modified_array_type *) xmalloc (sizeof *a);
      memset (a, 0, sizeof *a);

      a->indx = info->type_stack->type.indx;
      a->low = low;
      a->high = high;

      a->next = m->arrays;
      m->arrays = a;
    }

  return TRUE;
}

/* Make a set type.  */

static bfd_boolean
ieee_set_type (void *p, bfd_boolean bitstringp ATTRIBUTE_UNUSED)
{
  struct ieee_handle *info = (struct ieee_handle *) p;
  bfd_boolean localp;
  unsigned int eleindx;

  localp = info->type_stack->type.localp;
  eleindx = ieee_pop_type (info);

  /* FIXME: We don't know the size, so we just use 4.  */

  return (ieee_define_type (info, 0, TRUE, localp)
	  && ieee_write_number (info, 's')
	  && ieee_write_number (info, 4)
	  && ieee_write_number (info, eleindx));
}

/* Make an offset type.  */

static bfd_boolean
ieee_offset_type (void *p)
{
  struct ieee_handle *info = (struct ieee_handle *) p;
  unsigned int targetindx, baseindx;

  targetindx = ieee_pop_type (info);
  baseindx = ieee_pop_type (info);

  /* FIXME: The MRI C++ compiler does not appear to generate any
     useful type information about an offset type.  It just records a
     pointer to member as an integer.  The MRI/HP IEEE spec does
     describe a pmisc record which can be used for a pointer to
     member.  Unfortunately, it does not describe the target type,
     which seems pretty important.  I'm going to punt this for now.  */

  return ieee_int_type (p, 4, TRUE);
}

/* Make a method type.  */

static bfd_boolean
ieee_method_type (void *p, bfd_boolean domain, int argcount,
		  bfd_boolean varargs)
{
  struct ieee_handle *info = (struct ieee_handle *) p;

  /* FIXME: The MRI/HP IEEE spec defines a pmisc record to use for a
     method, but the definition is incomplete.  We just output an 'x'
     type.  */

  if (domain)
    ieee_pop_unused_type (info);

  return ieee_function_type (p, argcount, varargs);
}

/* Make a const qualified type.  */

static bfd_boolean
ieee_const_type (void *p)
{
  struct ieee_handle *info = (struct ieee_handle *) p;
  unsigned int size;
  bfd_boolean unsignedp, localp;
  unsigned int indx;
  struct ieee_modified_type *m = NULL;

  size = info->type_stack->type.size;
  unsignedp = info->type_stack->type.unsignedp;
  localp = info->type_stack->type.localp;
  indx = ieee_pop_type (info);

  if (! localp)
    {
      m = ieee_get_modified_info (info, indx);
      if (m == NULL)
	return FALSE;

      if (m->const_qualified > 0)
	return ieee_push_type (info, m->const_qualified, size, unsignedp,
			       FALSE);
    }

  if (! ieee_define_type (info, size, unsignedp, localp)
      || ! ieee_write_number (info, 'n')
      || ! ieee_write_number (info, 1)
      || ! ieee_write_number (info, indx))
    return FALSE;

  if (! localp)
    m->const_qualified = info->type_stack->type.indx;

  return TRUE;
}

/* Make a volatile qualified type.  */

static bfd_boolean
ieee_volatile_type (void *p)
{
  struct ieee_handle *info = (struct ieee_handle *) p;
  unsigned int size;
  bfd_boolean unsignedp, localp;
  unsigned int indx;
  struct ieee_modified_type *m = NULL;

  size = info->type_stack->type.size;
  unsignedp = info->type_stack->type.unsignedp;
  localp = info->type_stack->type.localp;
  indx = ieee_pop_type (info);

  if (! localp)
    {
      m = ieee_get_modified_info (info, indx);
      if (m == NULL)
	return FALSE;

      if (m->volatile_qualified > 0)
	return ieee_push_type (info, m->volatile_qualified, size, unsignedp,
			       FALSE);
    }

  if (! ieee_define_type (info, size, unsignedp, localp)
      || ! ieee_write_number (info, 'n')
      || ! ieee_write_number (info, 2)
      || ! ieee_write_number (info, indx))
    return FALSE;

  if (! localp)
    m->volatile_qualified = info->type_stack->type.indx;

  return TRUE;
}

/* Convert an enum debug_visibility into a CXXFLAGS value.  */

static unsigned int
ieee_vis_to_flags (enum debug_visibility visibility)
{
  switch (visibility)
    {
    default:
      abort ();
    case DEBUG_VISIBILITY_PUBLIC:
      return CXXFLAGS_VISIBILITY_PUBLIC;
    case DEBUG_VISIBILITY_PRIVATE:
      return CXXFLAGS_VISIBILITY_PRIVATE;
    case DEBUG_VISIBILITY_PROTECTED:
      return CXXFLAGS_VISIBILITY_PROTECTED;
    }
  /*NOTREACHED*/
}

/* Start defining a struct type.  We build it in the strdef field on
   the stack, to avoid confusing type definitions required by the
   fields with the struct type itself.  */

static bfd_boolean
ieee_start_struct_type (void *p, const char *tag, unsigned int id,
			bfd_boolean structp, unsigned int size)
{
  struct ieee_handle *info = (struct ieee_handle *) p;
  bfd_boolean localp, ignorep;
  bfd_boolean copy;
  char ab[20];
  const char *look;
  struct ieee_name_type_hash_entry *h;
  struct ieee_name_type *nt, *ntlook;
  struct ieee_buflist strdef;

  localp = FALSE;
  ignorep = FALSE;

  /* We need to create a tag for internal use even if we don't want
     one for external use.  This will let us refer to an anonymous
     struct.  */
  if (tag != NULL)
    {
      look = tag;
      copy = FALSE;
    }
  else
    {
      sprintf (ab, "__anon%u", id);
      look = ab;
      copy = TRUE;
    }

  /* If we already have references to the tag, we must use the
     existing type index.  */
  h = ieee_name_type_hash_lookup (&info->tags, look, TRUE, copy);
  if (h == NULL)
    return FALSE;

  nt = NULL;
  for (ntlook = h->types; ntlook != NULL; ntlook = ntlook->next)
    {
      if (ntlook->id == id)
	nt = ntlook;
      else if (! ntlook->type.localp)
	{
	  /* We are creating a duplicate definition of a globally
	     defined tag.  Force it to be local to avoid
	     confusion.  */
	  localp = TRUE;
	}
    }

  if (nt != NULL)
    {
      assert (localp == nt->type.localp);
      if (nt->kind == DEBUG_KIND_ILLEGAL && ! localp)
	{
	  /* We've already seen a global definition of the type.
             Ignore this new definition.  */
	  ignorep = TRUE;
	}
    }
  else
    {
      nt = (struct ieee_name_type *) xmalloc (sizeof *nt);
      memset (nt, 0, sizeof *nt);
      nt->id = id;
      nt->type.name = h->root.string;
      nt->next = h->types;
      h->types = nt;
      nt->type.indx = info->type_indx;
      ++info->type_indx;
    }

  nt->kind = DEBUG_KIND_ILLEGAL;

  if (! ieee_init_buffer (info, &strdef)
      || ! ieee_define_named_type (info, tag, nt->type.indx, size, TRUE,
				   localp, &strdef)
      || ! ieee_write_number (info, structp ? 'S' : 'U')
      || ! ieee_write_number (info, size))
    return FALSE;

  if (! ignorep)
    {
      const char *hold;

      /* We never want nt->type.name to be NULL.  We want the rest of
	 the type to be the object set up on the type stack; it will
	 have a NULL name if tag is NULL.  */
      hold = nt->type.name;
      nt->type = info->type_stack->type;
      nt->type.name = hold;
    }

  info->type_stack->type.name = tag;
  info->type_stack->type.strdef = strdef;
  info->type_stack->type.ignorep = ignorep;

  return TRUE;
}

/* Add a field to a struct.  */

static bfd_boolean
ieee_struct_field (void *p, const char *name, bfd_vma bitpos, bfd_vma bitsize,
		   enum debug_visibility visibility)
{
  struct ieee_handle *info = (struct ieee_handle *) p;
  unsigned int size;
  bfd_boolean unsignedp;
  bfd_boolean referencep;
  bfd_boolean localp;
  unsigned int indx;
  bfd_vma offset;

  assert (info->type_stack != NULL
	  && info->type_stack->next != NULL
	  && ! ieee_buffer_emptyp (&info->type_stack->next->type.strdef));

  /* If we are ignoring this struct definition, just pop and ignore
     the type.  */
  if (info->type_stack->next->type.ignorep)
    {
      ieee_pop_unused_type (info);
      return TRUE;
    }

  size = info->type_stack->type.size;
  unsignedp = info->type_stack->type.unsignedp;
  referencep = info->type_stack->type.referencep;
  localp = info->type_stack->type.localp;
  indx = ieee_pop_type (info);

  if (localp)
    info->type_stack->type.localp = TRUE;

  if (info->type_stack->type.classdef != NULL)
    {
      unsigned int flags;
      unsigned int nindx;

      /* This is a class.  We must add a description of this field to
         the class records we are building.  */

      flags = ieee_vis_to_flags (visibility);
      nindx = info->type_stack->type.classdef->indx;
      if (! ieee_change_buffer (info,
				&info->type_stack->type.classdef->pmiscbuf)
	  || ! ieee_write_asn (info, nindx, 'd')
	  || ! ieee_write_asn (info, nindx, flags)
	  || ! ieee_write_atn65 (info, nindx, name)
	  || ! ieee_write_atn65 (info, nindx, name))
	return FALSE;
      info->type_stack->type.classdef->pmisccount += 4;

      if (referencep)
	{
	  unsigned int nindx;

	  /* We need to output a record recording that this field is
             really of reference type.  We put this on the refs field
             of classdef, so that it can be appended to the C++
             records after the class is defined.  */

	  nindx = info->name_indx;
	  ++info->name_indx;

	  if (! ieee_change_buffer (info,
				    &info->type_stack->type.classdef->refs)
	      || ! ieee_write_byte (info, (int) ieee_nn_record)
	      || ! ieee_write_number (info, nindx)
	      || ! ieee_write_id (info, "")
	      || ! ieee_write_2bytes (info, (int) ieee_atn_record_enum)
	      || ! ieee_write_number (info, nindx)
	      || ! ieee_write_number (info, 0)
	      || ! ieee_write_number (info, 62)
	      || ! ieee_write_number (info, 80)
	      || ! ieee_write_number (info, 4)
	      || ! ieee_write_asn (info, nindx, 'R')
	      || ! ieee_write_asn (info, nindx, 3)
	      || ! ieee_write_atn65 (info, nindx, info->type_stack->type.name)
	      || ! ieee_write_atn65 (info, nindx, name))
	    return FALSE;
	}
    }

  /* If the bitsize doesn't match the expected size, we need to output
     a bitfield type.  */
  if (size == 0 || bitsize == 0 || bitsize == size * 8)
    offset = bitpos / 8;
  else
    {
      if (! ieee_define_type (info, 0, unsignedp,
			      info->type_stack->type.localp)
	  || ! ieee_write_number (info, 'g')
	  || ! ieee_write_number (info, unsignedp ? 0 : 1)
	  || ! ieee_write_number (info, bitsize)
	  || ! ieee_write_number (info, indx))
	return FALSE;
      indx = ieee_pop_type (info);
      offset = bitpos;
    }

  /* Switch to the struct we are building in order to output this
     field definition.  */
  return (ieee_change_buffer (info, &info->type_stack->type.strdef)
	  && ieee_write_id (info, name)
	  && ieee_write_number (info, indx)
	  && ieee_write_number (info, offset));
}

/* Finish up a struct type.  */

static bfd_boolean
ieee_end_struct_type (void *p)
{
  struct ieee_handle *info = (struct ieee_handle *) p;
  struct ieee_buflist *pb;

  assert (info->type_stack != NULL
	  && ! ieee_buffer_emptyp (&info->type_stack->type.strdef));

  /* If we were ignoring this struct definition because it was a
     duplicate definition, just through away whatever bytes we have
     accumulated.  Leave the type on the stack.  */
  if (info->type_stack->type.ignorep)
    return TRUE;

  /* If this is not a duplicate definition of this tag, then localp
     will be FALSE, and we can put it in the global type block.
     FIXME: We should avoid outputting duplicate definitions which are
     the same.  */
  if (! info->type_stack->type.localp)
    {
      /* Make sure we have started the global type block.  */
      if (ieee_buffer_emptyp (&info->global_types))
	{
	  if (! ieee_change_buffer (info, &info->global_types)
	      || ! ieee_write_byte (info, (int) ieee_bb_record_enum)
	      || ! ieee_write_byte (info, 2)
	      || ! ieee_write_number (info, 0)
	      || ! ieee_write_id (info, ""))
	    return FALSE;
	}
      pb = &info->global_types;
    }
  else
    {
      /* Make sure we have started the types block.  */
      if (ieee_buffer_emptyp (&info->types))
	{
	  if (! ieee_change_buffer (info, &info->types)
	      || ! ieee_write_byte (info, (int) ieee_bb_record_enum)
	      || ! ieee_write_byte (info, 1)
	      || ! ieee_write_number (info, 0)
	      || ! ieee_write_id (info, info->modname))
	    return FALSE;
	}
      pb = &info->types;
    }

  /* Append the struct definition to the types.  */
  if (! ieee_append_buffer (info, pb, &info->type_stack->type.strdef)
      || ! ieee_init_buffer (info, &info->type_stack->type.strdef))
    return FALSE;

  /* Leave the struct on the type stack.  */

  return TRUE;
}

/* Start a class type.  */

static bfd_boolean
ieee_start_class_type (void *p, const char *tag, unsigned int id,
		       bfd_boolean structp, unsigned int size,
		       bfd_boolean vptr, bfd_boolean ownvptr)
{
  struct ieee_handle *info = (struct ieee_handle *) p;
  const char *vclass;
  struct ieee_buflist pmiscbuf;
  unsigned int indx;
  struct ieee_type_class *classdef;

  /* A C++ class is output as a C++ struct along with a set of pmisc
     records describing the class.  */

  /* We need to have a name so that we can associate the struct and
     the class.  */
  if (tag == NULL)
    {
      char *t;

      t = (char *) xmalloc (20);
      sprintf (t, "__anon%u", id);
      tag = t;
    }

  /* We can't write out the virtual table information until we have
     finished the class, because we don't know the virtual table size.
     We get the size from the largest voffset we see.  */
  vclass = NULL;
  if (vptr && ! ownvptr)
    {
      vclass = info->type_stack->type.name;
      assert (vclass != NULL);
      /* We don't call ieee_pop_unused_type, since the class should
         get defined.  */
      (void) ieee_pop_type (info);
    }

  if (! ieee_start_struct_type (p, tag, id, structp, size))
    return FALSE;

  indx = info->name_indx;
  ++info->name_indx;

  /* We write out pmisc records into the classdef field.  We will
     write out the pmisc start after we know the number of records we
     need.  */
  if (! ieee_init_buffer (info, &pmiscbuf)
      || ! ieee_change_buffer (info, &pmiscbuf)
      || ! ieee_write_asn (info, indx, 'T')
      || ! ieee_write_asn (info, indx, structp ? 'o' : 'u')
      || ! ieee_write_atn65 (info, indx, tag))
    return FALSE;

  classdef = (struct ieee_type_class *) xmalloc (sizeof *classdef);
  memset (classdef, 0, sizeof *classdef);

  classdef->indx = indx;
  classdef->pmiscbuf = pmiscbuf;
  classdef->pmisccount = 3;
  classdef->vclass = vclass;
  classdef->ownvptr = ownvptr;

  info->type_stack->type.classdef = classdef;

  return TRUE;
}

/* Add a static member to a class.  */

static bfd_boolean
ieee_class_static_member (void *p, const char *name, const char *physname,
			  enum debug_visibility visibility)
{
  struct ieee_handle *info = (struct ieee_handle *) p;
  unsigned int flags;
  unsigned int nindx;

  /* We don't care about the type.  Hopefully there will be a call to
     ieee_variable declaring the physical name and the type, since
     that is where an IEEE consumer must get the type.  */
  ieee_pop_unused_type (info);

  assert (info->type_stack != NULL
	  && info->type_stack->type.classdef != NULL);

  flags = ieee_vis_to_flags (visibility);
  flags |= CXXFLAGS_STATIC;

  nindx = info->type_stack->type.classdef->indx;

  if (! ieee_change_buffer (info, &info->type_stack->type.classdef->pmiscbuf)
      || ! ieee_write_asn (info, nindx, 'd')
      || ! ieee_write_asn (info, nindx, flags)
      || ! ieee_write_atn65 (info, nindx, name)
      || ! ieee_write_atn65 (info, nindx, physname))
    return FALSE;
  info->type_stack->type.classdef->pmisccount += 4;

  return TRUE;
}

/* Add a base class to a class.  */

static bfd_boolean
ieee_class_baseclass (void *p, bfd_vma bitpos, bfd_boolean virtual,
		      enum debug_visibility visibility)
{
  struct ieee_handle *info = (struct ieee_handle *) p;
  const char *bname;
  bfd_boolean localp;
  unsigned int bindx;
  char *fname;
  unsigned int flags;
  unsigned int nindx;

  assert (info->type_stack != NULL
	  && info->type_stack->type.name != NULL
	  && info->type_stack->next != NULL
	  && info->type_stack->next->type.classdef != NULL
	  && ! ieee_buffer_emptyp (&info->type_stack->next->type.strdef));

  bname = info->type_stack->type.name;
  localp = info->type_stack->type.localp;
  bindx = ieee_pop_type (info);

  /* We are currently defining both a struct and a class.  We must
     write out a field definition in the struct which holds the base
     class.  The stabs debugging reader will create a field named
     _vb$CLASS for a virtual base class, so we just use that.  FIXME:
     we should not depend upon a detail of stabs debugging.  */
  if (virtual)
    {
      fname = (char *) xmalloc (strlen (bname) + sizeof "_vb$");
      sprintf (fname, "_vb$%s", bname);
      flags = BASEFLAGS_VIRTUAL;
    }
  else
    {
      if (localp)
	info->type_stack->type.localp = TRUE;

      fname = (char *) xmalloc (strlen (bname) + sizeof "_b$");
      sprintf (fname, "_b$%s", bname);

      if (! ieee_change_buffer (info, &info->type_stack->type.strdef)
	  || ! ieee_write_id (info, fname)
	  || ! ieee_write_number (info, bindx)
	  || ! ieee_write_number (info, bitpos / 8))
	return FALSE;
      flags = 0;
    }

  if (visibility == DEBUG_VISIBILITY_PRIVATE)
    flags |= BASEFLAGS_PRIVATE;

  nindx = info->type_stack->type.classdef->indx;

  if (! ieee_change_buffer (info, &info->type_stack->type.classdef->pmiscbuf)
      || ! ieee_write_asn (info, nindx, 'b')
      || ! ieee_write_asn (info, nindx, flags)
      || ! ieee_write_atn65 (info, nindx, bname)
      || ! ieee_write_asn (info, nindx, 0)
      || ! ieee_write_atn65 (info, nindx, fname))
    return FALSE;
  info->type_stack->type.classdef->pmisccount += 5;

  free (fname);

  return TRUE;
}

/* Start building a method for a class.  */

static bfd_boolean
ieee_class_start_method (void *p, const char *name)
{
  struct ieee_handle *info = (struct ieee_handle *) p;

  assert (info->type_stack != NULL
	  && info->type_stack->type.classdef != NULL
	  && info->type_stack->type.classdef->method == NULL);

  info->type_stack->type.classdef->method = name;

  return TRUE;
}

/* Define a new method variant, either static or not.  */

static bfd_boolean
ieee_class_method_var (struct ieee_handle *info, const char *physname,
		       enum debug_visibility visibility,
		       bfd_boolean staticp, bfd_boolean constp,
		       bfd_boolean volatilep, bfd_vma voffset,
		       bfd_boolean context)
{
  unsigned int flags;
  unsigned int nindx;
  bfd_boolean virtual;

  /* We don't need the type of the method.  An IEEE consumer which
     wants the type must track down the function by the physical name
     and get the type from that.  */
  ieee_pop_unused_type (info);

  /* We don't use the context.  FIXME: We probably ought to use it to
     adjust the voffset somehow, but I don't really know how.  */
  if (context)
    ieee_pop_unused_type (info);

  assert (info->type_stack != NULL
	  && info->type_stack->type.classdef != NULL
	  && info->type_stack->type.classdef->method != NULL);

  flags = ieee_vis_to_flags (visibility);

  /* FIXME: We never set CXXFLAGS_OVERRIDE, CXXFLAGS_OPERATOR,
     CXXFLAGS_CTORDTOR, CXXFLAGS_CTOR, or CXXFLAGS_INLINE.  */

  if (staticp)
    flags |= CXXFLAGS_STATIC;
  if (constp)
    flags |= CXXFLAGS_CONST;
  if (volatilep)
    flags |= CXXFLAGS_VOLATILE;

  nindx = info->type_stack->type.classdef->indx;

  virtual = context || voffset > 0;

  if (! ieee_change_buffer (info,
			    &info->type_stack->type.classdef->pmiscbuf)
      || ! ieee_write_asn (info, nindx, virtual ? 'v' : 'm')
      || ! ieee_write_asn (info, nindx, flags)
      || ! ieee_write_atn65 (info, nindx,
			     info->type_stack->type.classdef->method)
      || ! ieee_write_atn65 (info, nindx, physname))
    return FALSE;

  if (virtual)
    {
      if (voffset > info->type_stack->type.classdef->voffset)
	info->type_stack->type.classdef->voffset = voffset;
      if (! ieee_write_asn (info, nindx, voffset))
	return FALSE;
      ++info->type_stack->type.classdef->pmisccount;
    }

  if (! ieee_write_asn (info, nindx, 0))
    return FALSE;

  info->type_stack->type.classdef->pmisccount += 5;

  return TRUE;
}

/* Define a new method variant.  */

static bfd_boolean
ieee_class_method_variant (void *p, const char *physname,
			   enum debug_visibility visibility,
			   bfd_boolean constp, bfd_boolean volatilep,
			   bfd_vma voffset, bfd_boolean context)
{
  struct ieee_handle *info = (struct ieee_handle *) p;

  return ieee_class_method_var (info, physname, visibility, FALSE, constp,
				volatilep, voffset, context);
}

/* Define a new static method variant.  */

static bfd_boolean
ieee_class_static_method_variant (void *p, const char *physname,
				  enum debug_visibility visibility,
				  bfd_boolean constp, bfd_boolean volatilep)
{
  struct ieee_handle *info = (struct ieee_handle *) p;

  return ieee_class_method_var (info, physname, visibility, TRUE, constp,
				volatilep, 0, FALSE);
}

/* Finish up a method.  */

static bfd_boolean
ieee_class_end_method (void *p)
{
  struct ieee_handle *info = (struct ieee_handle *) p;

  assert (info->type_stack != NULL
	  && info->type_stack->type.classdef != NULL
	  && info->type_stack->type.classdef->method != NULL);

  info->type_stack->type.classdef->method = NULL;

  return TRUE;
}

/* Finish up a class.  */

static bfd_boolean
ieee_end_class_type (void *p)
{
  struct ieee_handle *info = (struct ieee_handle *) p;
  unsigned int nindx;

  assert (info->type_stack != NULL
	  && info->type_stack->type.classdef != NULL);

  /* If we were ignoring this class definition because it was a
     duplicate definition, just through away whatever bytes we have
     accumulated.  Leave the type on the stack.  */
  if (info->type_stack->type.ignorep)
    return TRUE;

  nindx = info->type_stack->type.classdef->indx;

  /* If we have a virtual table, we can write out the information now.  */
  if (info->type_stack->type.classdef->vclass != NULL
      || info->type_stack->type.classdef->ownvptr)
    {
      if (! ieee_change_buffer (info,
				&info->type_stack->type.classdef->pmiscbuf)
	  || ! ieee_write_asn (info, nindx, 'z')
	  || ! ieee_write_atn65 (info, nindx, "")
	  || ! ieee_write_asn (info, nindx,
			       info->type_stack->type.classdef->voffset))
	return FALSE;
      if (info->type_stack->type.classdef->ownvptr)
	{
	  if (! ieee_write_atn65 (info, nindx, ""))
	    return FALSE;
	}
      else
	{
	  if (! ieee_write_atn65 (info, nindx,
				  info->type_stack->type.classdef->vclass))
	    return FALSE;
	}
      if (! ieee_write_asn (info, nindx, 0))
	return FALSE;
      info->type_stack->type.classdef->pmisccount += 5;
    }

  /* Now that we know the number of pmisc records, we can write out
     the atn62 which starts the pmisc records, and append them to the
     C++ buffers.  */

  if (! ieee_change_buffer (info, &info->cxx)
      || ! ieee_write_byte (info, (int) ieee_nn_record)
      || ! ieee_write_number (info, nindx)
      || ! ieee_write_id (info, "")
      || ! ieee_write_2bytes (info, (int) ieee_atn_record_enum)
      || ! ieee_write_number (info, nindx)
      || ! ieee_write_number (info, 0)
      || ! ieee_write_number (info, 62)
      || ! ieee_write_number (info, 80)
      || ! ieee_write_number (info,
			      info->type_stack->type.classdef->pmisccount))
    return FALSE;

  if (! ieee_append_buffer (info, &info->cxx,
			    &info->type_stack->type.classdef->pmiscbuf))
    return FALSE;
  if (! ieee_buffer_emptyp (&info->type_stack->type.classdef->refs))
    {
      if (! ieee_append_buffer (info, &info->cxx,
				&info->type_stack->type.classdef->refs))
	return FALSE;
    }

  return ieee_end_struct_type (p);
}

/* Push a previously seen typedef onto the type stack.  */

static bfd_boolean
ieee_typedef_type (void *p, const char *name)
{
  struct ieee_handle *info = (struct ieee_handle *) p;
  struct ieee_name_type_hash_entry *h;
  struct ieee_name_type *nt;

  h = ieee_name_type_hash_lookup (&info->typedefs, name, FALSE, FALSE);

  /* h should never be NULL, since that would imply that the generic
     debugging code has asked for a typedef which it has not yet
     defined.  */
  assert (h != NULL);

  /* We always use the most recently defined type for this name, which
     will be the first one on the list.  */

  nt = h->types;
  if (! ieee_push_type (info, nt->type.indx, nt->type.size,
			nt->type.unsignedp, nt->type.localp))
    return FALSE;

  /* Copy over any other type information we may have.  */
  info->type_stack->type = nt->type;

  return TRUE;
}

/* Push a tagged type onto the type stack.  */

static bfd_boolean
ieee_tag_type (void *p, const char *name, unsigned int id,
	       enum debug_type_kind kind)
{
  struct ieee_handle *info = (struct ieee_handle *) p;
  bfd_boolean localp;
  bfd_boolean copy;
  char ab[20];
  struct ieee_name_type_hash_entry *h;
  struct ieee_name_type *nt;

  if (kind == DEBUG_KIND_ENUM)
    {
      struct ieee_defined_enum *e;

      if (name == NULL)
	abort ();
      for (e = info->enums; e != NULL; e = e->next)
	if (e->tag != NULL && strcmp (e->tag, name) == 0)
	  return ieee_push_type (info, e->indx, 0, TRUE, FALSE);

      e = (struct ieee_defined_enum *) xmalloc (sizeof *e);
      memset (e, 0, sizeof *e);

      e->indx = info->type_indx;
      ++info->type_indx;
      e->tag = name;
      e->defined = FALSE;

      e->next = info->enums;
      info->enums = e;

      return ieee_push_type (info, e->indx, 0, TRUE, FALSE);
    }

  localp = FALSE;

  copy = FALSE;
  if (name == NULL)
    {
      sprintf (ab, "__anon%u", id);
      name = ab;
      copy = TRUE;
    }

  h = ieee_name_type_hash_lookup (&info->tags, name, TRUE, copy);
  if (h == NULL)
    return FALSE;

  for (nt = h->types; nt != NULL; nt = nt->next)
    {
      if (nt->id == id)
	{
	  if (! ieee_push_type (info, nt->type.indx, nt->type.size,
				nt->type.unsignedp, nt->type.localp))
	    return FALSE;
	  /* Copy over any other type information we may have.  */
	  info->type_stack->type = nt->type;
	  return TRUE;
	}

      if (! nt->type.localp)
	{
	  /* This is a duplicate of a global type, so it must be
             local.  */
	  localp = TRUE;
	}
    }

  nt = (struct ieee_name_type *) xmalloc (sizeof *nt);
  memset (nt, 0, sizeof *nt);

  nt->id = id;
  nt->type.name = h->root.string;
  nt->type.indx = info->type_indx;
  nt->type.localp = localp;
  ++info->type_indx;
  nt->kind = kind;

  nt->next = h->types;
  h->types = nt;

  if (! ieee_push_type (info, nt->type.indx, 0, FALSE, localp))
    return FALSE;

  info->type_stack->type.name = h->root.string;

  return TRUE;
}

/* Output a typedef.  */

static bfd_boolean
ieee_typdef (void *p, const char *name)
{
  struct ieee_handle *info = (struct ieee_handle *) p;
  struct ieee_write_type type;
  unsigned int indx;
  bfd_boolean found;
  bfd_boolean localp;
  struct ieee_name_type_hash_entry *h;
  struct ieee_name_type *nt;

  type = info->type_stack->type;
  indx = type.indx;

  /* If this is a simple builtin type using a builtin name, we don't
     want to output the typedef itself.  We also want to change the
     type index to correspond to the name being used.  We recognize
     names used in stabs debugging output even if they don't exactly
     correspond to the names used for the IEEE builtin types.  */
  found = FALSE;
  if (indx <= (unsigned int) builtin_bcd_float)
    {
      switch ((enum builtin_types) indx)
	{
	default:
	  break;

	case builtin_void:
	  if (strcmp (name, "void") == 0)
	    found = TRUE;
	  break;

	case builtin_signed_char:
	case builtin_char:
	  if (strcmp (name, "signed char") == 0)
	    {
	      indx = (unsigned int) builtin_signed_char;
	      found = TRUE;
	    }
	  else if (strcmp (name, "char") == 0)
	    {
	      indx = (unsigned int) builtin_char;
	      found = TRUE;
	    }
	  break;

	case builtin_unsigned_char:
	  if (strcmp (name, "unsigned char") == 0)
	    found = TRUE;
	  break;

	case builtin_signed_short_int:
	case builtin_short:
	case builtin_short_int:
	case builtin_signed_short:
	  if (strcmp (name, "signed short int") == 0)
	    {
	      indx = (unsigned int) builtin_signed_short_int;
	      found = TRUE;
	    }
	  else if (strcmp (name, "short") == 0)
	    {
	      indx = (unsigned int) builtin_short;
	      found = TRUE;
	    }
	  else if (strcmp (name, "short int") == 0)
	    {
	      indx = (unsigned int) builtin_short_int;
	      found = TRUE;
	    }
	  else if (strcmp (name, "signed short") == 0)
	    {
	      indx = (unsigned int) builtin_signed_short;
	      found = TRUE;
	    }
	  break;

	case builtin_unsigned_short_int:
	case builtin_unsigned_short:
	  if (strcmp (name, "unsigned short int") == 0
	      || strcmp (name, "short unsigned int") == 0)
	    {
	      indx = builtin_unsigned_short_int;
	      found = TRUE;
	    }
	  else if (strcmp (name, "unsigned short") == 0)
	    {
	      indx = builtin_unsigned_short;
	      found = TRUE;
	    }
	  break;

	case builtin_signed_long:
	case builtin_int: /* FIXME: Size depends upon architecture.  */
	case builtin_long:
	  if (strcmp (name, "signed long") == 0)
	    {
	      indx = builtin_signed_long;
	      found = TRUE;
	    }
	  else if (strcmp (name, "int") == 0)
	    {
	      indx = builtin_int;
	      found = TRUE;
	    }
	  else if (strcmp (name, "long") == 0
		   || strcmp (name, "long int") == 0)
	    {
	      indx = builtin_long;
	      found = TRUE;
	    }
	  break;

	case builtin_unsigned_long:
	case builtin_unsigned: /* FIXME: Size depends upon architecture.  */
	case builtin_unsigned_int: /* FIXME: Like builtin_unsigned.  */
	  if (strcmp (name, "unsigned long") == 0
	      || strcmp (name, "long unsigned int") == 0)
	    {
	      indx = builtin_unsigned_long;
	      found = TRUE;
	    }
	  else if (strcmp (name, "unsigned") == 0)
	    {
	      indx = builtin_unsigned;
	      found = TRUE;
	    }
	  else if (strcmp (name, "unsigned int") == 0)
	    {
	      indx = builtin_unsigned_int;
	      found = TRUE;
	    }
	  break;

	case builtin_signed_long_long:
	  if (strcmp (name, "signed long long") == 0
	      || strcmp (name, "long long int") == 0)
	    found = TRUE;
	  break;

	case builtin_unsigned_long_long:
	  if (strcmp (name, "unsigned long long") == 0
	      || strcmp (name, "long long unsigned int") == 0)
	    found = TRUE;
	  break;

	case builtin_float:
	  if (strcmp (name, "float") == 0)
	    found = TRUE;
	  break;

	case builtin_double:
	  if (strcmp (name, "double") == 0)
	    found = TRUE;
	  break;

	case builtin_long_double:
	  if (strcmp (name, "long double") == 0)
	    found = TRUE;
	  break;

	case builtin_long_long_double:
	  if (strcmp (name, "long long double") == 0)
	    found = TRUE;
	  break;
	}

      if (found)
	type.indx = indx;
    }

  h = ieee_name_type_hash_lookup (&info->typedefs, name, TRUE, FALSE);
  if (h == NULL)
    return FALSE;

  /* See if we have already defined this type with this name.  */
  localp = type.localp;
  for (nt = h->types; nt != NULL; nt = nt->next)
    {
      if (nt->id == indx)
	{
	  /* If this is a global definition, then we don't need to
	     do anything here.  */
	  if (! nt->type.localp)
	    {
	      ieee_pop_unused_type (info);
	      return TRUE;
	    }
	}
      else
	{
	  /* This is a duplicate definition, so make this one local.  */
	  localp = TRUE;
	}
    }

  /* We need to add a new typedef for this type.  */

  nt = (struct ieee_name_type *) xmalloc (sizeof *nt);
  memset (nt, 0, sizeof *nt);
  nt->id = indx;
  nt->type = type;
  nt->type.name = name;
  nt->type.localp = localp;
  nt->kind = DEBUG_KIND_ILLEGAL;

  nt->next = h->types;
  h->types = nt;

  if (found)
    {
      /* This is one of the builtin typedefs, so we don't need to
         actually define it.  */
      ieee_pop_unused_type (info);
      return TRUE;
    }

  indx = ieee_pop_type (info);

  if (! ieee_define_named_type (info, name, (unsigned int) -1, type.size,
				type.unsignedp,	localp,
				(struct ieee_buflist *) NULL)
      || ! ieee_write_number (info, 'T')
      || ! ieee_write_number (info, indx))
    return FALSE;

  /* Remove the type we just added to the type stack.  This should not
     be ieee_pop_unused_type, since the type is used, we just don't
     need it now.  */
  (void) ieee_pop_type (info);

  return TRUE;
}

/* Output a tag for a type.  We don't have to do anything here.  */

static bfd_boolean
ieee_tag (void *p, const char *name ATTRIBUTE_UNUSED)
{
  struct ieee_handle *info = (struct ieee_handle *) p;

  /* This should not be ieee_pop_unused_type, since we want the type
     to be defined.  */
  (void) ieee_pop_type (info);
  return TRUE;
}

/* Output an integer constant.  */

static bfd_boolean
ieee_int_constant (void *p ATTRIBUTE_UNUSED, const char *name ATTRIBUTE_UNUSED,
		   bfd_vma val ATTRIBUTE_UNUSED)
{
  /* FIXME.  */
  return TRUE;
}

/* Output a floating point constant.  */

static bfd_boolean
ieee_float_constant (void *p ATTRIBUTE_UNUSED,
		     const char *name ATTRIBUTE_UNUSED,
		     double val ATTRIBUTE_UNUSED)
{
  /* FIXME.  */
  return TRUE;
}

/* Output a typed constant.  */

static bfd_boolean
ieee_typed_constant (void *p, const char *name ATTRIBUTE_UNUSED,
		     bfd_vma val ATTRIBUTE_UNUSED)
{
  struct ieee_handle *info = (struct ieee_handle *) p;

  /* FIXME.  */
  ieee_pop_unused_type (info);
  return TRUE;
}

/* Output a variable.  */

static bfd_boolean
ieee_variable (void *p, const char *name, enum debug_var_kind kind,
	       bfd_vma val)
{
  struct ieee_handle *info = (struct ieee_handle *) p;
  unsigned int name_indx;
  unsigned int size;
  bfd_boolean referencep;
  unsigned int type_indx;
  bfd_boolean asn;
  int refflag;

  size = info->type_stack->type.size;
  referencep = info->type_stack->type.referencep;
  type_indx = ieee_pop_type (info);

  assert (! ieee_buffer_emptyp (&info->vars));
  if (! ieee_change_buffer (info, &info->vars))
    return FALSE;

  name_indx = info->name_indx;
  ++info->name_indx;

  /* Write out an NN and an ATN record for this variable.  */
  if (! ieee_write_byte (info, (int) ieee_nn_record)
      || ! ieee_write_number (info, name_indx)
      || ! ieee_write_id (info, name)
      || ! ieee_write_2bytes (info, (int) ieee_atn_record_enum)
      || ! ieee_write_number (info, name_indx)
      || ! ieee_write_number (info, type_indx))
    return FALSE;
  switch (kind)
    {
    default:
      abort ();
      return FALSE;
    case DEBUG_GLOBAL:
      if (! ieee_write_number (info, 8)
	  || ! ieee_add_range (info, FALSE, val, val + size))
	return FALSE;
      refflag = 0;
      asn = TRUE;
      break;
    case DEBUG_STATIC:
      if (! ieee_write_number (info, 3)
	  || ! ieee_add_range (info, FALSE, val, val + size))
	return FALSE;
      refflag = 1;
      asn = TRUE;
      break;
    case DEBUG_LOCAL_STATIC:
      if (! ieee_write_number (info, 3)
	  || ! ieee_add_range (info, FALSE, val, val + size))
	return FALSE;
      refflag = 2;
      asn = TRUE;
      break;
    case DEBUG_LOCAL:
      if (! ieee_write_number (info, 1)
	  || ! ieee_write_number (info, val))
	return FALSE;
      refflag = 2;
      asn = FALSE;
      break;
    case DEBUG_REGISTER:
      if (! ieee_write_number (info, 2)
	  || ! ieee_write_number (info,
				  ieee_genreg_to_regno (info->abfd, val)))
	return FALSE;
      refflag = 2;
      asn = FALSE;
      break;
    }

  if (asn)
    {
      if (! ieee_write_asn (info, name_indx, val))
	return FALSE;
    }

  /* If this is really a reference type, then we just output it with
     pointer type, and must now output a C++ record indicating that it
     is really reference type.  */
  if (referencep)
    {
      unsigned int nindx;

      nindx = info->name_indx;
      ++info->name_indx;

      /* If this is a global variable, we want to output the misc
         record in the C++ misc record block.  Otherwise, we want to
         output it just after the variable definition, which is where
         the current buffer is.  */
      if (refflag != 2)
	{
	  if (! ieee_change_buffer (info, &info->cxx))
	    return FALSE;
	}

      if (! ieee_write_byte (info, (int) ieee_nn_record)
	  || ! ieee_write_number (info, nindx)
	  || ! ieee_write_id (info, "")
	  || ! ieee_write_2bytes (info, (int) ieee_atn_record_enum)
	  || ! ieee_write_number (info, nindx)
	  || ! ieee_write_number (info, 0)
	  || ! ieee_write_number (info, 62)
	  || ! ieee_write_number (info, 80)
	  || ! ieee_write_number (info, 3)
	  || ! ieee_write_asn (info, nindx, 'R')
	  || ! ieee_write_asn (info, nindx, refflag)
	  || ! ieee_write_atn65 (info, nindx, name))
	return FALSE;
    }

  return TRUE;
}

/* Start outputting information for a function.  */

static bfd_boolean
ieee_start_function (void *p, const char *name, bfd_boolean global)
{
  struct ieee_handle *info = (struct ieee_handle *) p;
  bfd_boolean referencep;
  unsigned int retindx, typeindx;

  referencep = info->type_stack->type.referencep;
  retindx = ieee_pop_type (info);

  /* Besides recording a BB4 or BB6 block, we record the type of the
     function in the BB1 typedef block.  We can't write out the full
     type until we have seen all the parameters, so we accumulate it
     in info->fntype and info->fnargs.  */
  if (! ieee_buffer_emptyp (&info->fntype))
    {
      /* FIXME: This might happen someday if we support nested
         functions.  */
      abort ();
    }

  info->fnname = name;

  /* An attribute of 0x40 means that the push mask is unknown.  */
  if (! ieee_define_named_type (info, name, (unsigned int) -1, 0, FALSE, TRUE,
				&info->fntype)
      || ! ieee_write_number (info, 'x')
      || ! ieee_write_number (info, 0x40)
      || ! ieee_write_number (info, 0)
      || ! ieee_write_number (info, 0)
      || ! ieee_write_number (info, retindx))
    return FALSE;

  typeindx = ieee_pop_type (info);

  if (! ieee_init_buffer (info, &info->fnargs))
    return FALSE;
  info->fnargcount = 0;

  /* If the function return value is actually a reference type, we
     must add a record indicating that.  */
  if (referencep)
    {
      unsigned int nindx;

      nindx = info->name_indx;
      ++info->name_indx;
      if (! ieee_change_buffer (info, &info->cxx)
	  || ! ieee_write_byte (info, (int) ieee_nn_record)
	  || ! ieee_write_number (info, nindx)
	  || ! ieee_write_id (info, "")
	  || ! ieee_write_2bytes (info, (int) ieee_atn_record_enum)
	  || ! ieee_write_number (info, nindx)
	  || ! ieee_write_number (info, 0)
	  || ! ieee_write_number (info, 62)
	  || ! ieee_write_number (info, 80)
	  || ! ieee_write_number (info, 3)
	  || ! ieee_write_asn (info, nindx, 'R')
	  || ! ieee_write_asn (info, nindx, global ? 0 : 1)
	  || ! ieee_write_atn65 (info, nindx, name))
	return FALSE;
    }

  assert (! ieee_buffer_emptyp (&info->vars));
  if (! ieee_change_buffer (info, &info->vars))
    return FALSE;

  /* The address is written out as the first block.  */

  ++info->block_depth;

  return (ieee_write_byte (info, (int) ieee_bb_record_enum)
	  && ieee_write_byte (info, global ? 4 : 6)
	  && ieee_write_number (info, 0)
	  && ieee_write_id (info, name)
	  && ieee_write_number (info, 0)
	  && ieee_write_number (info, typeindx));
}

/* Add a function parameter.  This will normally be called before the
   first block, so we postpone them until we see the block.  */

static bfd_boolean
ieee_function_parameter (void *p, const char *name, enum debug_parm_kind kind,
			 bfd_vma val)
{
  struct ieee_handle *info = (struct ieee_handle *) p;
  struct ieee_pending_parm *m, **pm;

  assert (info->block_depth == 1);

  m = (struct ieee_pending_parm *) xmalloc (sizeof *m);
  memset (m, 0, sizeof *m);

  m->next = NULL;
  m->name = name;
  m->referencep = info->type_stack->type.referencep;
  m->type = ieee_pop_type (info);
  m->kind = kind;
  m->val = val;

  for (pm = &info->pending_parms; *pm != NULL; pm = &(*pm)->next)
    ;
  *pm = m;

  /* Add the type to the fnargs list.  */
  if (! ieee_change_buffer (info, &info->fnargs)
      || ! ieee_write_number (info, m->type))
    return FALSE;
  ++info->fnargcount;

  return TRUE;
}

/* Output pending function parameters.  */

static bfd_boolean
ieee_output_pending_parms (struct ieee_handle *info)
{
  struct ieee_pending_parm *m;
  unsigned int refcount;

  refcount = 0;
  for (m = info->pending_parms; m != NULL; m = m->next)
    {
      enum debug_var_kind vkind;

      switch (m->kind)
	{
	default:
	  abort ();
	  return FALSE;
	case DEBUG_PARM_STACK:
	case DEBUG_PARM_REFERENCE:
	  vkind = DEBUG_LOCAL;
	  break;
	case DEBUG_PARM_REG:
	case DEBUG_PARM_REF_REG:
	  vkind = DEBUG_REGISTER;
	  break;
	}

      if (! ieee_push_type (info, m->type, 0, FALSE, FALSE))
	return FALSE;
      info->type_stack->type.referencep = m->referencep;
      if (m->referencep)
	++refcount;
      if (! ieee_variable ((void *) info, m->name, vkind, m->val))
	return FALSE;
    }

  /* If there are any reference parameters, we need to output a
     miscellaneous record indicating them.  */
  if (refcount > 0)
    {
      unsigned int nindx, varindx;

      /* FIXME: The MRI compiler outputs the demangled function name
         here, but we are outputting the mangled name.  */
      nindx = info->name_indx;
      ++info->name_indx;
      if (! ieee_change_buffer (info, &info->vars)
	  || ! ieee_write_byte (info, (int) ieee_nn_record)
	  || ! ieee_write_number (info, nindx)
	  || ! ieee_write_id (info, "")
	  || ! ieee_write_2bytes (info, (int) ieee_atn_record_enum)
	  || ! ieee_write_number (info, nindx)
	  || ! ieee_write_number (info, 0)
	  || ! ieee_write_number (info, 62)
	  || ! ieee_write_number (info, 80)
	  || ! ieee_write_number (info, refcount + 3)
	  || ! ieee_write_asn (info, nindx, 'B')
	  || ! ieee_write_atn65 (info, nindx, info->fnname)
	  || ! ieee_write_asn (info, nindx, 0))
	return FALSE;
      for (m = info->pending_parms, varindx = 1;
	   m != NULL;
	   m = m->next, varindx++)
	{
	  if (m->referencep)
	    {
	      if (! ieee_write_asn (info, nindx, varindx))
		return FALSE;
	    }
	}
    }

  m = info->pending_parms;
  while (m != NULL)
    {
      struct ieee_pending_parm *next;

      next = m->next;
      free (m);
      m = next;
    }

  info->pending_parms = NULL;

  return TRUE;
}

/* Start a block.  If this is the first block, we output the address
   to finish the BB4 or BB6, and then output the function parameters.  */

static bfd_boolean
ieee_start_block (void *p, bfd_vma addr)
{
  struct ieee_handle *info = (struct ieee_handle *) p;

  if (! ieee_change_buffer (info, &info->vars))
    return FALSE;

  if (info->block_depth == 1)
    {
      if (! ieee_write_number (info, addr)
	  || ! ieee_output_pending_parms (info))
	return FALSE;
    }
  else
    {
      if (! ieee_write_byte (info, (int) ieee_bb_record_enum)
	  || ! ieee_write_byte (info, 6)
	  || ! ieee_write_number (info, 0)
	  || ! ieee_write_id (info, "")
	  || ! ieee_write_number (info, 0)
	  || ! ieee_write_number (info, 0)
	  || ! ieee_write_number (info, addr))
	return FALSE;
    }

  if (! ieee_start_range (info, addr))
    return FALSE;

  ++info->block_depth;

  return TRUE;
}

/* End a block.  */

static bfd_boolean
ieee_end_block (void *p, bfd_vma addr)
{
  struct ieee_handle *info = (struct ieee_handle *) p;

  /* The address we are given is the end of the block, but IEEE seems
     to want to the address of the last byte in the block, so we
     subtract one.  */
  if (! ieee_change_buffer (info, &info->vars)
      || ! ieee_write_byte (info, (int) ieee_be_record_enum)
      || ! ieee_write_number (info, addr - 1))
    return FALSE;

  if (! ieee_end_range (info, addr))
    return FALSE;

  --info->block_depth;

  if (addr > info->highaddr)
    info->highaddr = addr;

  return TRUE;
}

/* End a function.  */

static bfd_boolean
ieee_end_function (void *p)
{
  struct ieee_handle *info = (struct ieee_handle *) p;

  assert (info->block_depth == 1);

  --info->block_depth;

  /* Now we can finish up fntype, and add it to the typdef section.
     At this point, fntype is the 'x' type up to the argument count,
     and fnargs is the argument types.  We must add the argument
     count, and we must add the level.  FIXME: We don't record varargs
     functions correctly.  In fact, stabs debugging does not give us
     enough information to do so.  */
  if (! ieee_change_buffer (info, &info->fntype)
      || ! ieee_write_number (info, info->fnargcount)
      || ! ieee_change_buffer (info, &info->fnargs)
      || ! ieee_write_number (info, 0))
    return FALSE;

  /* Make sure the typdef block has been started.  */
  if (ieee_buffer_emptyp (&info->types))
    {
      if (! ieee_change_buffer (info, &info->types)
	  || ! ieee_write_byte (info, (int) ieee_bb_record_enum)
	  || ! ieee_write_byte (info, 1)
	  || ! ieee_write_number (info, 0)
	  || ! ieee_write_id (info, info->modname))
	return FALSE;
    }

  if (! ieee_append_buffer (info, &info->types, &info->fntype)
      || ! ieee_append_buffer (info, &info->types, &info->fnargs))
    return FALSE;

  info->fnname = NULL;
  if (! ieee_init_buffer (info, &info->fntype)
      || ! ieee_init_buffer (info, &info->fnargs))
    return FALSE;
  info->fnargcount = 0;

  return TRUE;
}

/* Record line number information.  */

static bfd_boolean
ieee_lineno (void *p, const char *filename, unsigned long lineno, bfd_vma addr)
{
  struct ieee_handle *info = (struct ieee_handle *) p;

  assert (info->filename != NULL);

  /* The HP simulator seems to get confused when more than one line is
     listed for the same address, at least if they are in different
     files.  We handle this by always listing the last line for a
     given address, since that seems to be the one that gdb uses.  */
  if (info->pending_lineno_filename != NULL
      && addr != info->pending_lineno_addr)
    {
      /* Make sure we have a line number block.  */
      if (! ieee_buffer_emptyp (&info->linenos))
	{
	  if (! ieee_change_buffer (info, &info->linenos))
	    return FALSE;
	}
      else
	{
	  info->lineno_name_indx = info->name_indx;
	  ++info->name_indx;
	  if (! ieee_change_buffer (info, &info->linenos)
	      || ! ieee_write_byte (info, (int) ieee_bb_record_enum)
	      || ! ieee_write_byte (info, 5)
	      || ! ieee_write_number (info, 0)
	      || ! ieee_write_id (info, info->filename)
	      || ! ieee_write_byte (info, (int) ieee_nn_record)
	      || ! ieee_write_number (info, info->lineno_name_indx)
	      || ! ieee_write_id (info, ""))
	    return FALSE;
	  info->lineno_filename = info->filename;
	}

      if (strcmp (info->pending_lineno_filename, info->lineno_filename) != 0)
	{
	  if (strcmp (info->filename, info->lineno_filename) != 0)
	    {
	      /* We were not in the main file.  Close the block for the
		 included file.  */
	      if (! ieee_write_byte (info, (int) ieee_be_record_enum))
		return FALSE;
	      if (strcmp (info->filename, info->pending_lineno_filename) == 0)
		{
		  /* We need a new NN record, and we aren't about to
		     output one.  */
		  info->lineno_name_indx = info->name_indx;
		  ++info->name_indx;
		  if (! ieee_write_byte (info, (int) ieee_nn_record)
		      || ! ieee_write_number (info, info->lineno_name_indx)
		      || ! ieee_write_id (info, ""))
		    return FALSE;
		}
	    }
	  if (strcmp (info->filename, info->pending_lineno_filename) != 0)
	    {
	      /* We are not changing to the main file.  Open a block for
		 the new included file.  */
	      info->lineno_name_indx = info->name_indx;
	      ++info->name_indx;
	      if (! ieee_write_byte (info, (int) ieee_bb_record_enum)
		  || ! ieee_write_byte (info, 5)
		  || ! ieee_write_number (info, 0)
		  || ! ieee_write_id (info, info->pending_lineno_filename)
		  || ! ieee_write_byte (info, (int) ieee_nn_record)
		  || ! ieee_write_number (info, info->lineno_name_indx)
		  || ! ieee_write_id (info, ""))
		return FALSE;
	    }
	  info->lineno_filename = info->pending_lineno_filename;
	}

      if (! ieee_write_2bytes (info, (int) ieee_atn_record_enum)
	  || ! ieee_write_number (info, info->lineno_name_indx)
	  || ! ieee_write_number (info, 0)
	  || ! ieee_write_number (info, 7)
	  || ! ieee_write_number (info, info->pending_lineno)
	  || ! ieee_write_number (info, 0)
	  || ! ieee_write_asn (info, info->lineno_name_indx,
			       info->pending_lineno_addr))
	return FALSE;
    }

  info->pending_lineno_filename = filename;
  info->pending_lineno = lineno;
  info->pending_lineno_addr = addr;

  return TRUE;
}
