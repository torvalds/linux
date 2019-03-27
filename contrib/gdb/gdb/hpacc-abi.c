/* Abstraction of HP aCC ABI.
   Contributed by Daniel Berlin <dberlin@redhat.com>
   Most of the real code is from HP, i've just fiddled it to fit in
   the C++ ABI abstraction framework.

   Copyright 2001 Free Software Foundation, Inc.

   This file is part of GDB.

   This program is free software; you can redistribute it and/or
   modify
   it under the terms of the GNU General Public License as published
   by
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
#include "value.h"
#include "gdb_regex.h"
#include "gdb_string.h"
#include "gdbtypes.h"
#include "gdbcore.h"
#include "cp-abi.h"

struct cp_abi_ops hpacc_abi_ops;

/* It appears the is_*_name stuff is never used when we try the hpACC
 * ABI. As such, I have no clue what the real answers are. Shouldn't
 * have any more effect than it does now.  */
static regex_t constructor_pattern;
static regex_t destructor_pattern;
static regex_t operator_pattern;

static enum dtor_kinds
hpacc_is_destructor_name (const char *name)
{
  if (regexec (&destructor_pattern, name, 0, 0, 0) == 0)
    return complete_object_dtor;
  else
    return 0;
}

static enum ctor_kinds
hpacc_is_constructor_name (const char *name)
{
  if (regexec (&constructor_pattern, name, 0, 0, 0) == 0)
    return complete_object_ctor;
  else
    return 0;
}

static int
hpacc_is_operator_name (const char *name)
{
  return regexec (&operator_pattern, name, 0, 0, 0) == 0;
}

static int
hpacc_is_vtable_name (const char *name)
{
  return strcmp (name,
		 "This will never match anything, please fill it in") == 0;
}

/* Return a virtual function as a value.
   ARG1 is the object which provides the virtual function
   table pointer.  *ARG1P is side-effected in calling this function.
   F is the list of member functions which contains the desired virtual
   function.
   J is an index into F which provides the desired virtual function.

   TYPE is the type in which F is located.  */
static struct value *
hpacc_virtual_fn_field (struct value **arg1p, struct fn_field * f, int j,
			struct type * type, int offset)
{
  struct value *arg1 = *arg1p;
  struct type *type1 = check_typedef (VALUE_TYPE (arg1));

  /* Deal with HP/Taligent runtime model for virtual functions */
  struct value *vp;
  struct value *argp;		/* arg1 cast to base */
  CORE_ADDR coreptr;		/* pointer to target address */
  int class_index;		/* which class segment pointer to use */
  struct type *ftype = TYPE_FN_FIELD_TYPE (f, j);	/* method type */

  argp = value_cast (type, *arg1p);

  if (VALUE_ADDRESS (argp) == 0)
    error ("Address of object is null; object may not have been created.");

  /* pai: FIXME -- 32x64 possible problem? */
  /* First word (4 bytes) in object layout is the vtable pointer */
  coreptr = *(CORE_ADDR *) (VALUE_CONTENTS (argp));	/* pai: (temp)  */
  /* + offset + VALUE_EMBEDDED_OFFSET (argp)); */

  if (!coreptr)
    error
      ("Virtual table pointer is null for object; object may not have been created.");

  /* pai/1997-05-09
   * FIXME: The code here currently handles only
   * the non-RRBC case of the Taligent/HP runtime spec; when RRBC
   * is introduced, the condition for the "if" below will have to
   * be changed to be a test for the RRBC case.  */

  if (1)
    {
      /* Non-RRBC case; the virtual function pointers are stored at fixed
       * offsets in the virtual table. */

      /* Retrieve the offset in the virtual table from the debug
       * info.  The offset of the vfunc's entry is in words from
       * the beginning of the vtable; but first we have to adjust
       * by HP_ACC_VFUNC_START to account for other entries */

      /* pai: FIXME: 32x64 problem here, a word may be 8 bytes in
       * which case the multiplier should be 8 and values should be long */
      vp = value_at (builtin_type_int,
		     coreptr + 4 * (TYPE_FN_FIELD_VOFFSET (f, j) +
				    HP_ACC_VFUNC_START), NULL);

      coreptr = *(CORE_ADDR *) (VALUE_CONTENTS (vp));
      /* coreptr now contains the address of the virtual function */
      /* (Actually, it contains the pointer to the plabel for the function. */
    }
  else
    {
      /* RRBC case; the virtual function pointers are found by double
       * indirection through the class segment tables. */

      /* Choose class segment depending on type we were passed */
      class_index = class_index_in_primary_list (type);

      /* Find class segment pointer.  These are in the vtable slots after
       * some other entries, so adjust by HP_ACC_VFUNC_START for that. */
      /* pai: FIXME 32x64 problem here, if words are 8 bytes long
       * the multiplier below has to be 8 and value should be long. */
      vp = value_at (builtin_type_int,
		     coreptr + 4 * (HP_ACC_VFUNC_START + class_index), NULL);
      /* Indirect once more, offset by function index */
      /* pai: FIXME 32x64 problem here, again multiplier could be 8 and value long */
      coreptr =
	*(CORE_ADDR *) (VALUE_CONTENTS (vp) +
			4 * TYPE_FN_FIELD_VOFFSET (f, j));
      vp = value_at (builtin_type_int, coreptr, NULL);
      coreptr = *(CORE_ADDR *) (VALUE_CONTENTS (vp));

      /* coreptr now contains the address of the virtual function */
      /* (Actually, it contains the pointer to the plabel for the function.) */

    }

  if (!coreptr)
    error ("Address of virtual function is null; error in virtual table?");

  /* Wrap this addr in a value and return pointer */
  vp = allocate_value (ftype);
  VALUE_TYPE (vp) = ftype;
  VALUE_ADDRESS (vp) = coreptr;

  /* pai: (temp) do we need the value_ind stuff in value_fn_field? */
  return vp;
}


static struct type *
hpacc_value_rtti_type (struct value *v, int *full, int *top, int *using_enc)
{
  struct type *known_type;
  struct type *rtti_type;
  CORE_ADDR coreptr;
  struct value *vp;
  int using_enclosing = 0;
  long top_offset = 0;
  char rtti_type_name[256];

  if (full)
    *full = 0;
  if (top)
    *top = -1;
  if (using_enc)
    *using_enc = 0;

  /* Get declared type */
  known_type = VALUE_TYPE (v);
  CHECK_TYPEDEF (known_type);
  /* RTTI works only or class objects */
  if (TYPE_CODE (known_type) != TYPE_CODE_CLASS)
    return NULL;

  /* If neither the declared type nor the enclosing type of the
   * value structure has a HP ANSI C++ style virtual table,
   * we can't do anything. */
  if (!TYPE_HAS_VTABLE (known_type))
    {
      known_type = VALUE_ENCLOSING_TYPE (v);
      CHECK_TYPEDEF (known_type);
      if ((TYPE_CODE (known_type) != TYPE_CODE_CLASS) ||
          !TYPE_HAS_VTABLE (known_type))
        return NULL;		/* No RTTI, or not HP-compiled types */
      CHECK_TYPEDEF (known_type);
      using_enclosing = 1;
    }

  if (using_enclosing && using_enc)
    *using_enc = 1;

  /* First get the virtual table address */
  coreptr = *(CORE_ADDR *) ((VALUE_CONTENTS_ALL (v))
                            + VALUE_OFFSET (v)
                            + (using_enclosing
                               ? 0
                               : VALUE_EMBEDDED_OFFSET (v)));
  if (coreptr == 0)
    /* return silently -- maybe called on gdb-generated value */
    return NULL;
  
  /* Fetch the top offset of the object */
  /* FIXME possible 32x64 problem with pointer size & arithmetic */
  vp = value_at (builtin_type_int,
                 coreptr + 4 * HP_ACC_TOP_OFFSET_OFFSET,
                 VALUE_BFD_SECTION (v));
  top_offset = value_as_long (vp);
  if (top)
    *top = top_offset;

  /* Fetch the typeinfo pointer */
  /* FIXME possible 32x64 problem with pointer size & arithmetic */
  vp = value_at (builtin_type_int, coreptr + 4 * HP_ACC_TYPEINFO_OFFSET,
                 VALUE_BFD_SECTION (v));
  /* Indirect through the typeinfo pointer and retrieve the pointer
   * to the string name */
  coreptr = *(CORE_ADDR *) (VALUE_CONTENTS (vp));
  if (!coreptr)
    error ("Retrieved null typeinfo pointer in trying to determine "
           "run-time type");
  /* 4 -> offset of name field */
  vp = value_at (builtin_type_int, coreptr + 4, VALUE_BFD_SECTION (v));
  /* FIXME possible 32x64 problem */

  coreptr = *(CORE_ADDR *) (VALUE_CONTENTS (vp));

  read_memory_string (coreptr, rtti_type_name, 256);

  if (strlen (rtti_type_name) == 0)
    error ("Retrieved null type name from typeinfo");

  /* search for type */
  rtti_type = lookup_typename (rtti_type_name, (struct block *) 0, 1);

  if (!rtti_type)
    error ("Could not find run-time type: invalid type name %s in typeinfo??",
           rtti_type_name);
  CHECK_TYPEDEF (rtti_type);
#if 0
  printf ("RTTI type name %s, tag %s, full? %d\n", TYPE_NAME (rtti_type),
          TYPE_TAG_NAME (rtti_type), full ? *full : -1);
#endif
  /* Check whether we have the entire object */
  if (full			/* Non-null pointer passed */
      &&
      /* Either we checked on the whole object in hand and found the
         top offset to be zero */
      (((top_offset == 0) &&
        using_enclosing &&
        TYPE_LENGTH (known_type) == TYPE_LENGTH (rtti_type))
       ||
       /* Or we checked on the embedded object and top offset was the
          same as the embedded offset */
       ((top_offset == VALUE_EMBEDDED_OFFSET (v)) &&
        !using_enclosing &&
        TYPE_LENGTH (VALUE_ENCLOSING_TYPE (v)) == TYPE_LENGTH (rtti_type))))

    *full = 1;

  return rtti_type;
}

extern int gnuv2_baseclass_offset (struct type *type, int index,
				   char *valaddr, CORE_ADDR address);

static void
init_hpacc_ops (void)
{
  hpacc_abi_ops.shortname = "hpaCC";
  hpacc_abi_ops.longname = "HP aCC ABI";
  hpacc_abi_ops.doc = "HP aCC ABI";
  hpacc_abi_ops.is_destructor_name = hpacc_is_destructor_name;
  hpacc_abi_ops.is_constructor_name = hpacc_is_constructor_name;
  hpacc_abi_ops.is_vtable_name = hpacc_is_vtable_name;
  hpacc_abi_ops.is_operator_name = hpacc_is_operator_name;
  hpacc_abi_ops.virtual_fn_field = hpacc_virtual_fn_field;
  hpacc_abi_ops.rtti_type = hpacc_value_rtti_type;
  /* It seems that this function is specific to GNU G++ < 3.0.
     However, it is called for data members even in the HP
     case (although not for member functions).
     FIXME: Is that correct?  */
  hpacc_abi_ops.baseclass_offset = gnuv2_baseclass_offset;
}

extern initialize_file_ftype _initialize_hpacc_abi; /* -Wmissing-prototypes */

void
_initialize_hpacc_abi (void)
{
  init_hpacc_ops ();

  regcomp (&constructor_pattern,
	   "^This will never match anything, please fill it in$", REG_NOSUB);

  regcomp (&destructor_pattern,
	   "^This will never match anything, please fill it in$", REG_NOSUB);

  regcomp (&operator_pattern,
	   "^This will never match anything, please fill it in$", REG_NOSUB);

  register_cp_abi (&hpacc_abi_ops);
}
