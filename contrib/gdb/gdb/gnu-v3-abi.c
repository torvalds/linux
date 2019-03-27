/* Abstraction of GNU v3 abi.
   Contributed by Jim Blandy <jimb@redhat.com>

   Copyright 2001, 2002, 2003 Free Software Foundation, Inc.

   This file is part of GDB.

   This program is free software; you can redistribute it and/or
   modify it under the terms of the GNU General Public License as
   published by the Free Software Foundation; either version 2 of the
   License, or (at your option) any later version.

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
#include "cp-abi.h"
#include "cp-support.h"
#include "demangle.h"
#include "gdb_assert.h"
#include "gdb_string.h"

static struct cp_abi_ops gnu_v3_abi_ops;

static int
gnuv3_is_vtable_name (const char *name)
{
  return strncmp (name, "_ZTV", 4) == 0;
}

static int
gnuv3_is_operator_name (const char *name)
{
  return strncmp (name, "operator", 8) == 0;
}


/* To help us find the components of a vtable, we build ourselves a
   GDB type object representing the vtable structure.  Following the
   V3 ABI, it goes something like this:

   struct gdb_gnu_v3_abi_vtable {

     / * An array of virtual call and virtual base offsets.  The real
         length of this array depends on the class hierarchy; we use
         negative subscripts to access the elements.  Yucky, but
         better than the alternatives.  * /
     ptrdiff_t vcall_and_vbase_offsets[0];

     / * The offset from a virtual pointer referring to this table
         to the top of the complete object.  * /
     ptrdiff_t offset_to_top;

     / * The type_info pointer for this class.  This is really a
         std::type_info *, but GDB doesn't really look at the
         type_info object itself, so we don't bother to get the type
         exactly right.  * /
     void *type_info;

     / * Virtual table pointers in objects point here.  * /

     / * Virtual function pointers.  Like the vcall/vbase array, the
         real length of this table depends on the class hierarchy.  * /
     void (*virtual_functions[0]) ();

   };

   The catch, of course, is that the exact layout of this table
   depends on the ABI --- word size, endianness, alignment, etc.  So
   the GDB type object is actually a per-architecture kind of thing.

   vtable_type_gdbarch_data is a gdbarch per-architecture data pointer
   which refers to the struct type * for this structure, laid out
   appropriately for the architecture.  */
static struct gdbarch_data *vtable_type_gdbarch_data;


/* Human-readable names for the numbers of the fields above.  */
enum {
  vtable_field_vcall_and_vbase_offsets,
  vtable_field_offset_to_top,
  vtable_field_type_info,
  vtable_field_virtual_functions
};


/* Return a GDB type representing `struct gdb_gnu_v3_abi_vtable',
   described above, laid out appropriately for ARCH.

   We use this function as the gdbarch per-architecture data
   initialization function.  We assume that the gdbarch framework
   calls the per-architecture data initialization functions after it
   sets current_gdbarch to the new architecture.  */
static void *
build_gdb_vtable_type (struct gdbarch *arch)
{
  struct type *t;
  struct field *field_list, *field;
  int offset;

  struct type *void_ptr_type
    = lookup_pointer_type (builtin_type_void);
  struct type *ptr_to_void_fn_type
    = lookup_pointer_type (lookup_function_type (builtin_type_void));

  /* ARCH can't give us the true ptrdiff_t type, so we guess.  */
  struct type *ptrdiff_type
    = init_type (TYPE_CODE_INT, TARGET_PTR_BIT / TARGET_CHAR_BIT, 0,
                 "ptrdiff_t", 0);

  /* We assume no padding is necessary, since GDB doesn't know
     anything about alignment at the moment.  If this assumption bites
     us, we should add a gdbarch method which, given a type, returns
     the alignment that type requires, and then use that here.  */

  /* Build the field list.  */
  field_list = xmalloc (sizeof (struct field [4]));
  memset (field_list, 0, sizeof (struct field [4]));
  field = &field_list[0];
  offset = 0;

  /* ptrdiff_t vcall_and_vbase_offsets[0]; */
  FIELD_NAME (*field) = "vcall_and_vbase_offsets";
  FIELD_TYPE (*field)
    = create_array_type (0, ptrdiff_type,
                         create_range_type (0, builtin_type_int, 0, -1));
  FIELD_BITPOS (*field) = offset * TARGET_CHAR_BIT;
  offset += TYPE_LENGTH (FIELD_TYPE (*field));
  field++;

  /* ptrdiff_t offset_to_top; */
  FIELD_NAME (*field) = "offset_to_top";
  FIELD_TYPE (*field) = ptrdiff_type;
  FIELD_BITPOS (*field) = offset * TARGET_CHAR_BIT;
  offset += TYPE_LENGTH (FIELD_TYPE (*field));
  field++;

  /* void *type_info; */
  FIELD_NAME (*field) = "type_info";
  FIELD_TYPE (*field) = void_ptr_type;
  FIELD_BITPOS (*field) = offset * TARGET_CHAR_BIT;
  offset += TYPE_LENGTH (FIELD_TYPE (*field));
  field++;

  /* void (*virtual_functions[0]) (); */
  FIELD_NAME (*field) = "virtual_functions";
  FIELD_TYPE (*field)
    = create_array_type (0, ptr_to_void_fn_type,
                         create_range_type (0, builtin_type_int, 0, -1));
  FIELD_BITPOS (*field) = offset * TARGET_CHAR_BIT;
  offset += TYPE_LENGTH (FIELD_TYPE (*field));
  field++;

  /* We assumed in the allocation above that there were four fields.  */
  gdb_assert (field == (field_list + 4));

  t = init_type (TYPE_CODE_STRUCT, offset, 0, 0, 0);
  TYPE_NFIELDS (t) = field - field_list;
  TYPE_FIELDS (t) = field_list;
  TYPE_TAG_NAME (t) = "gdb_gnu_v3_abi_vtable";

  return t;
}


/* Return the offset from the start of the imaginary `struct
   gdb_gnu_v3_abi_vtable' object to the vtable's "address point"
   (i.e., where objects' virtual table pointers point).  */
static int
vtable_address_point_offset (void)
{
  struct type *vtable_type = gdbarch_data (current_gdbarch,
					   vtable_type_gdbarch_data);

  return (TYPE_FIELD_BITPOS (vtable_type, vtable_field_virtual_functions)
          / TARGET_CHAR_BIT);
}


static struct type *
gnuv3_rtti_type (struct value *value,
                 int *full_p, int *top_p, int *using_enc_p)
{
  struct type *vtable_type = gdbarch_data (current_gdbarch,
					   vtable_type_gdbarch_data);
  struct type *value_type = check_typedef (VALUE_TYPE (value));
  CORE_ADDR vtable_address;
  struct value *vtable;
  struct minimal_symbol *vtable_symbol;
  const char *vtable_symbol_name;
  const char *class_name;
  struct type *run_time_type;
  struct type *base_type;
  LONGEST offset_to_top;

  /* We only have RTTI for class objects.  */
  if (TYPE_CODE (value_type) != TYPE_CODE_CLASS)
    return NULL;

  /* If we can't find the virtual table pointer for value_type, we
     can't find the RTTI.  */
  fill_in_vptr_fieldno (value_type);
  if (TYPE_VPTR_FIELDNO (value_type) == -1)
    return NULL;

  if (using_enc_p)
    *using_enc_p = 0;

  /* Fetch VALUE's virtual table pointer, and tweak it to point at
     an instance of our imaginary gdb_gnu_v3_abi_vtable structure.  */
  base_type = check_typedef (TYPE_VPTR_BASETYPE (value_type));
  if (value_type != base_type)
    {
      value = value_cast (base_type, value);
      if (using_enc_p)
	*using_enc_p = 1;
    }
  vtable_address
    = value_as_address (value_field (value, TYPE_VPTR_FIELDNO (value_type)));
  vtable = value_at_lazy (vtable_type,
                          vtable_address - vtable_address_point_offset (),
                          VALUE_BFD_SECTION (value));
  
  /* Find the linker symbol for this vtable.  */
  vtable_symbol
    = lookup_minimal_symbol_by_pc (VALUE_ADDRESS (vtable)
                                   + VALUE_OFFSET (vtable)
                                   + VALUE_EMBEDDED_OFFSET (vtable));
  if (! vtable_symbol)
    return NULL;
  
  /* The symbol's demangled name should be something like "vtable for
     CLASS", where CLASS is the name of the run-time type of VALUE.
     If we didn't like this approach, we could instead look in the
     type_info object itself to get the class name.  But this way
     should work just as well, and doesn't read target memory.  */
  vtable_symbol_name = SYMBOL_DEMANGLED_NAME (vtable_symbol);
  if (vtable_symbol_name == NULL
      || strncmp (vtable_symbol_name, "vtable for ", 11))
    {
      warning ("can't find linker symbol for virtual table for `%s' value",
	       TYPE_NAME (value_type));
      if (vtable_symbol_name)
	warning ("  found `%s' instead", vtable_symbol_name);
      return NULL;
    }
  class_name = vtable_symbol_name + 11;

  /* Try to look up the class name as a type name.  */
  /* FIXME: chastain/2003-11-26: block=NULL is bogus.  See pr gdb/1465. */
  run_time_type = cp_lookup_rtti_type (class_name, NULL);
  if (run_time_type == NULL)
    return NULL;

  /* Get the offset from VALUE to the top of the complete object.
     NOTE: this is the reverse of the meaning of *TOP_P.  */
  offset_to_top
    = value_as_long (value_field (vtable, vtable_field_offset_to_top));

  if (full_p)
    *full_p = (- offset_to_top == VALUE_EMBEDDED_OFFSET (value)
               && (TYPE_LENGTH (VALUE_ENCLOSING_TYPE (value))
                   >= TYPE_LENGTH (run_time_type)));
  if (top_p)
    *top_p = - offset_to_top;

  return run_time_type;
}


static struct value *
gnuv3_virtual_fn_field (struct value **value_p,
                        struct fn_field *f, int j,
			struct type *type, int offset)
{
  struct type *vtable_type = gdbarch_data (current_gdbarch,
					   vtable_type_gdbarch_data);
  struct value *value = *value_p;
  struct type *value_type = check_typedef (VALUE_TYPE (value));
  struct type *vfn_base;
  CORE_ADDR vtable_address;
  struct value *vtable;
  struct value *vfn;

  /* Some simple sanity checks.  */
  if (TYPE_CODE (value_type) != TYPE_CODE_CLASS)
    error ("Only classes can have virtual functions.");

  /* Find the base class that defines this virtual function.  */
  vfn_base = TYPE_FN_FIELD_FCONTEXT (f, j);
  if (! vfn_base)
    /* In programs compiled with G++ version 1, the debug info doesn't
       say which base class defined the virtual function.  We'll guess
       it's the same base class that has our vtable; this is wrong for
       multiple inheritance, but it's better than nothing.  */
    vfn_base = TYPE_VPTR_BASETYPE (type);

  /* This type may have been defined before its virtual function table
     was.  If so, fill in the virtual function table entry for the
     type now.  */
  if (TYPE_VPTR_FIELDNO (vfn_base) < 0)
    fill_in_vptr_fieldno (vfn_base);
  if (TYPE_VPTR_FIELDNO (vfn_base) < 0)
    error ("Could not find virtual table pointer for class \"%s\".",
	   TYPE_TAG_NAME (vfn_base) ? TYPE_TAG_NAME (vfn_base) : "<unknown>");

  /* Now that we know which base class is defining our virtual
     function, cast our value to that baseclass.  This takes care of
     any necessary `this' adjustments.  */
  if (vfn_base != value_type)
    value = value_cast (vfn_base, value);

  /* Now value is an object of the appropriate base type.  Fetch its
     virtual table.  */
  /* It might be possible to do this cast at the same time as the above.
     Does multiple inheritance affect this?
     Can this even trigger, or is TYPE_VPTR_BASETYPE idempotent?
  */
  if (TYPE_VPTR_BASETYPE (vfn_base) != vfn_base)
    value = value_cast (TYPE_VPTR_BASETYPE (vfn_base), value);
  vtable_address
    = value_as_address (value_field (value, TYPE_VPTR_FIELDNO (vfn_base)));

  vtable = value_at_lazy (vtable_type,
                          vtable_address - vtable_address_point_offset (),
                          VALUE_BFD_SECTION (value));

  /* Fetch the appropriate function pointer from the vtable.  */
  vfn = value_subscript (value_field (vtable, vtable_field_virtual_functions),
                         value_from_longest (builtin_type_int,
                                             TYPE_FN_FIELD_VOFFSET (f, j)));

  /* Cast the function pointer to the appropriate type.  */
  vfn = value_cast (lookup_pointer_type (TYPE_FN_FIELD_TYPE (f, j)),
                    vfn);

  /* Is (type)value always numerically the same as (vfn_base)value?
     If so we can spare this cast and use one of the ones above.  */
  *value_p = value_addr (value_cast (type, *value_p));

  return vfn;
}

/* Compute the offset of the baseclass which is
   the INDEXth baseclass of class TYPE,
   for value at VALADDR (in host) at ADDRESS (in target).
   The result is the offset of the baseclass value relative
   to (the address of)(ARG) + OFFSET.

   -1 is returned on error. */
static int
gnuv3_baseclass_offset (struct type *type, int index, char *valaddr,
			CORE_ADDR address)
{
  struct type *vtable_type = gdbarch_data (current_gdbarch,
					   vtable_type_gdbarch_data);
  struct value *vtable;
  struct type *vbasetype;
  struct value *offset_val, *vbase_array;
  CORE_ADDR vtable_address;
  long int cur_base_offset, base_offset;

  /* If it isn't a virtual base, this is easy.  The offset is in the
     type definition.  */
  if (!BASETYPE_VIA_VIRTUAL (type, index))
    return TYPE_BASECLASS_BITPOS (type, index) / 8;

  /* To access a virtual base, we need to use the vbase offset stored in
     our vtable.  Recent GCC versions provide this information.  If it isn't
     available, we could get what we needed from RTTI, or from drawing the
     complete inheritance graph based on the debug info.  Neither is
     worthwhile.  */
  cur_base_offset = TYPE_BASECLASS_BITPOS (type, index) / 8;
  if (cur_base_offset >= - vtable_address_point_offset ())
    error ("Expected a negative vbase offset (old compiler?)");

  cur_base_offset = cur_base_offset + vtable_address_point_offset ();
  if ((- cur_base_offset) % TYPE_LENGTH (builtin_type_void_data_ptr) != 0)
    error ("Misaligned vbase offset.");
  cur_base_offset = cur_base_offset
    / ((int) TYPE_LENGTH (builtin_type_void_data_ptr));

  /* We're now looking for the cur_base_offset'th entry (negative index)
     in the vcall_and_vbase_offsets array.  We used to cast the object to
     its TYPE_VPTR_BASETYPE, and reference the vtable as TYPE_VPTR_FIELDNO;
     however, that cast can not be done without calling baseclass_offset again
     if the TYPE_VPTR_BASETYPE is a virtual base class, as described in the
     v3 C++ ABI Section 2.4.I.2.b.  Fortunately the ABI guarantees that the
     vtable pointer will be located at the beginning of the object, so we can
     bypass the casting.  Verify that the TYPE_VPTR_FIELDNO is in fact at the
     start of whichever baseclass it resides in, as a sanity measure - iff
     we have debugging information for that baseclass.  */

  vbasetype = TYPE_VPTR_BASETYPE (type);
  if (TYPE_VPTR_FIELDNO (vbasetype) < 0)
    fill_in_vptr_fieldno (vbasetype);

  if (TYPE_VPTR_FIELDNO (vbasetype) >= 0
      && TYPE_FIELD_BITPOS (vbasetype, TYPE_VPTR_FIELDNO (vbasetype)) != 0)
    error ("Illegal vptr offset in class %s",
	   TYPE_NAME (vbasetype) ? TYPE_NAME (vbasetype) : "<unknown>");

  vtable_address = value_as_address (value_at_lazy (builtin_type_void_data_ptr,
						    address, NULL));
  vtable = value_at_lazy (vtable_type,
                          vtable_address - vtable_address_point_offset (),
                          NULL);
  offset_val = value_from_longest(builtin_type_int, cur_base_offset);
  vbase_array = value_field (vtable, vtable_field_vcall_and_vbase_offsets);
  base_offset = value_as_long (value_subscript (vbase_array, offset_val));
  return base_offset;
}

static void
init_gnuv3_ops (void)
{
  vtable_type_gdbarch_data = register_gdbarch_data (build_gdb_vtable_type);

  gnu_v3_abi_ops.shortname = "gnu-v3";
  gnu_v3_abi_ops.longname = "GNU G++ Version 3 ABI";
  gnu_v3_abi_ops.doc = "G++ Version 3 ABI";
  gnu_v3_abi_ops.is_destructor_name = is_gnu_v3_mangled_dtor;
  gnu_v3_abi_ops.is_constructor_name = is_gnu_v3_mangled_ctor;
  gnu_v3_abi_ops.is_vtable_name = gnuv3_is_vtable_name;
  gnu_v3_abi_ops.is_operator_name = gnuv3_is_operator_name;
  gnu_v3_abi_ops.rtti_type = gnuv3_rtti_type;
  gnu_v3_abi_ops.virtual_fn_field = gnuv3_virtual_fn_field;
  gnu_v3_abi_ops.baseclass_offset = gnuv3_baseclass_offset;
}

extern initialize_file_ftype _initialize_gnu_v3_abi; /* -Wmissing-prototypes */

void
_initialize_gnu_v3_abi (void)
{
  init_gnuv3_ops ();

  register_cp_abi (&gnu_v3_abi_ops);
}
