/* Support for printing C++ values for GDB, the GNU debugger.
   Copyright 1986, 1988, 1989, 1991, 1992, 1993, 1994, 1995, 1996, 1997,
   2000, 2001, 2002, 2003
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

#include "defs.h"
#include "gdb_obstack.h"
#include "symtab.h"
#include "gdbtypes.h"
#include "expression.h"
#include "value.h"
#include "command.h"
#include "gdbcmd.h"
#include "demangle.h"
#include "annotate.h"
#include "gdb_string.h"
#include "c-lang.h"
#include "target.h"
#include "cp-abi.h"
#include "valprint.h"

/* Indication of presence of HP-compiled object files */
extern int hp_som_som_object_present;	/* defined in symtab.c */


int vtblprint;			/* Controls printing of vtbl's */
int objectprint;		/* Controls looking up an object's derived type
				   using what we find in its vtables.  */
int static_field_print;		/* Controls printing of static fields. */

static struct obstack dont_print_vb_obstack;
static struct obstack dont_print_statmem_obstack;

extern void _initialize_cp_valprint (void);

static void cp_print_static_field (struct type *, struct value *,
				   struct ui_file *, int, int,
				   enum val_prettyprint);

static void cp_print_value (struct type *, struct type *, char *, int,
			    CORE_ADDR, struct ui_file *, int, int,
			    enum val_prettyprint, struct type **);

static void cp_print_hpacc_virtual_table_entries (struct type *, int *,
						  struct value *,
						  struct ui_file *, int,
						  int,
						  enum val_prettyprint);


void
cp_print_class_method (char *valaddr,
		       struct type *type,
		       struct ui_file *stream)
{
  struct type *domain;
  struct fn_field *f = NULL;
  int j = 0;
  int len2;
  int offset;
  char *kind = "";
  CORE_ADDR addr;
  struct symbol *sym;
  unsigned len;
  unsigned int i;
  struct type *target_type = check_typedef (TYPE_TARGET_TYPE (type));

  domain = TYPE_DOMAIN_TYPE (target_type);
  if (domain == (struct type *) NULL)
    {
      fprintf_filtered (stream, "<unknown>");
      return;
    }
  addr = unpack_pointer (type, valaddr);
  if (METHOD_PTR_IS_VIRTUAL (addr))
    {
      offset = METHOD_PTR_TO_VOFFSET (addr);
      len = TYPE_NFN_FIELDS (domain);
      for (i = 0; i < len; i++)
	{
	  f = TYPE_FN_FIELDLIST1 (domain, i);
	  len2 = TYPE_FN_FIELDLIST_LENGTH (domain, i);

	  check_stub_method_group (domain, i);
	  for (j = 0; j < len2; j++)
	    {
	      if (TYPE_FN_FIELD_VOFFSET (f, j) == offset)
		{
		  kind = "virtual ";
		  goto common;
		}
	    }
	}
    }
  else
    {
      sym = find_pc_function (addr);
      if (sym == 0)
	{
	  /* 1997-08-01 Currently unsupported with HP aCC */
	  if (hp_som_som_object_present)
	    {
	      fputs_filtered ("?? <not supported with HP aCC>", stream);
	      return;
	    }
	  error ("invalid pointer to member function");
	}
      len = TYPE_NFN_FIELDS (domain);
      for (i = 0; i < len; i++)
	{
	  f = TYPE_FN_FIELDLIST1 (domain, i);
	  len2 = TYPE_FN_FIELDLIST_LENGTH (domain, i);

	  check_stub_method_group (domain, i);
	  for (j = 0; j < len2; j++)
	    {
	      if (strcmp (DEPRECATED_SYMBOL_NAME (sym), TYPE_FN_FIELD_PHYSNAME (f, j))
		  == 0)
		goto common;
	    }
	}
    }
 common:
  if (i < len)
    {
      char *demangled_name;

      fprintf_filtered (stream, "&");
      fputs_filtered (kind, stream);
      demangled_name = cplus_demangle (TYPE_FN_FIELD_PHYSNAME (f, j),
				       DMGL_ANSI | DMGL_PARAMS);
      if (demangled_name == NULL)
	fprintf_filtered (stream, "<badly mangled name %s>",
			  TYPE_FN_FIELD_PHYSNAME (f, j));
      else
	{
	  fputs_filtered (demangled_name, stream);
	  xfree (demangled_name);
	}
    }
  else
    {
      fprintf_filtered (stream, "(");
      type_print (type, "", stream, -1);
      fprintf_filtered (stream, ") %d", (int) addr >> 3);
    }
}

/* GCC versions after 2.4.5 use this.  */
const char vtbl_ptr_name[] = "__vtbl_ptr_type";

/* HP aCC uses different names.  */
const char hpacc_vtbl_ptr_name[] = "__vfp";
const char hpacc_vtbl_ptr_type_name[] = "__vftyp";

/* Return truth value for assertion that TYPE is of the type
   "pointer to virtual function".  */

int
cp_is_vtbl_ptr_type (struct type *type)
{
  char *typename = type_name_no_tag (type);

  return (typename != NULL && !strcmp (typename, vtbl_ptr_name));
}

/* Return truth value for the assertion that TYPE is of the type
   "pointer to virtual function table".  */

int
cp_is_vtbl_member (struct type *type)
{
  /* With older versions of g++, the vtbl field pointed to an array
     of structures.  Nowadays it points directly to the structure. */
  if (TYPE_CODE (type) == TYPE_CODE_PTR)
    {
      type = TYPE_TARGET_TYPE (type);
      if (TYPE_CODE (type) == TYPE_CODE_ARRAY)
	{
	  type = TYPE_TARGET_TYPE (type);
	  if (TYPE_CODE (type) == TYPE_CODE_STRUCT	/* if not using thunks */
	      || TYPE_CODE (type) == TYPE_CODE_PTR)	/* if using thunks */
	    {
	      /* Virtual functions tables are full of pointers
	         to virtual functions. */
	      return cp_is_vtbl_ptr_type (type);
	    }
	}
      else if (TYPE_CODE (type) == TYPE_CODE_STRUCT)  /* if not using thunks */
	{
	  return cp_is_vtbl_ptr_type (type);
	}
      else if (TYPE_CODE (type) == TYPE_CODE_PTR)     /* if using thunks */
	{
	  /* The type name of the thunk pointer is NULL when using dwarf2.
	     We could test for a pointer to a function, but there is
	     no type info for the virtual table either, so it wont help.  */
	  return cp_is_vtbl_ptr_type (type);
	}
    }
  return 0;
}

/* Mutually recursive subroutines of cp_print_value and c_val_print to
   print out a structure's fields: cp_print_value_fields and cp_print_value.

   TYPE, VALADDR, ADDRESS, STREAM, RECURSE, and PRETTY have the
   same meanings as in cp_print_value and c_val_print.

   2nd argument REAL_TYPE is used to carry over the type of the derived
   class across the recursion to base classes. 

   DONT_PRINT is an array of baseclass types that we
   should not print, or zero if called from top level.  */

void
cp_print_value_fields (struct type *type, struct type *real_type, char *valaddr,
		       int offset, CORE_ADDR address, struct ui_file *stream,
		       int format, int recurse, enum val_prettyprint pretty,
		       struct type **dont_print_vb, int dont_print_statmem)
{
  int i, len, n_baseclasses;
  struct obstack tmp_obstack;
  char *last_dont_print = obstack_next_free (&dont_print_statmem_obstack);
  int fields_seen = 0;

  CHECK_TYPEDEF (type);

  fprintf_filtered (stream, "{");
  len = TYPE_NFIELDS (type);
  n_baseclasses = TYPE_N_BASECLASSES (type);

  /* First, print out baseclasses such that we don't print
     duplicates of virtual baseclasses.  */

  if (n_baseclasses > 0)
    cp_print_value (type, real_type, valaddr, offset, address, stream,
		    format, recurse + 1, pretty, dont_print_vb);

  /* Second, print out data fields */

  /* If there are no data fields, or if the only field is the
   * vtbl pointer, skip this part */
  if ((len == n_baseclasses)
      || ((len - n_baseclasses == 1)
	  && TYPE_HAS_VTABLE (type)
	  && strncmp (TYPE_FIELD_NAME (type, n_baseclasses),
		      hpacc_vtbl_ptr_name, 5) == 0)
      || !len)
    fprintf_filtered (stream, "<No data fields>");
  else
    {
      if (dont_print_statmem == 0)
	{
	  /* If we're at top level, carve out a completely fresh
	     chunk of the obstack and use that until this particular
	     invocation returns.  */
	  tmp_obstack = dont_print_statmem_obstack;
	  obstack_finish (&dont_print_statmem_obstack);
	}

      for (i = n_baseclasses; i < len; i++)
	{
	  /* If requested, skip printing of static fields.  */
	  if (!static_field_print && TYPE_FIELD_STATIC (type, i))
	    continue;

	  /* If a vtable pointer appears, we'll print it out later */
	  if (TYPE_HAS_VTABLE (type)
	      && strncmp (TYPE_FIELD_NAME (type, i), hpacc_vtbl_ptr_name,
			  5) == 0)
	    continue;

	  if (fields_seen)
	    fprintf_filtered (stream, ", ");
	  else if (n_baseclasses > 0)
	    {
	      if (pretty)
		{
		  fprintf_filtered (stream, "\n");
		  print_spaces_filtered (2 + 2 * recurse, stream);
		  fputs_filtered ("members of ", stream);
		  fputs_filtered (type_name_no_tag (type), stream);
		  fputs_filtered (": ", stream);
		}
	    }
	  fields_seen = 1;

	  if (pretty)
	    {
	      fprintf_filtered (stream, "\n");
	      print_spaces_filtered (2 + 2 * recurse, stream);
	    }
	  else
	    {
	      wrap_here (n_spaces (2 + 2 * recurse));
	    }
	  if (inspect_it)
	    {
	      if (TYPE_CODE (TYPE_FIELD_TYPE (type, i)) == TYPE_CODE_PTR)
		fputs_filtered ("\"( ptr \"", stream);
	      else
		fputs_filtered ("\"( nodef \"", stream);
	      if (TYPE_FIELD_STATIC (type, i))
		fputs_filtered ("static ", stream);
	      fprintf_symbol_filtered (stream, TYPE_FIELD_NAME (type, i),
				       language_cplus,
				       DMGL_PARAMS | DMGL_ANSI);
	      fputs_filtered ("\" \"", stream);
	      fprintf_symbol_filtered (stream, TYPE_FIELD_NAME (type, i),
				       language_cplus,
				       DMGL_PARAMS | DMGL_ANSI);
	      fputs_filtered ("\") \"", stream);
	    }
	  else
	    {
	      annotate_field_begin (TYPE_FIELD_TYPE (type, i));

	      if (TYPE_FIELD_STATIC (type, i))
		fputs_filtered ("static ", stream);
	      fprintf_symbol_filtered (stream, TYPE_FIELD_NAME (type, i),
				       language_cplus,
				       DMGL_PARAMS | DMGL_ANSI);
	      annotate_field_name_end ();
	      /* do not print leading '=' in case of anonymous unions */
	      if (strcmp (TYPE_FIELD_NAME (type, i), ""))
		fputs_filtered (" = ", stream);
	      annotate_field_value ();
	    }

	  if (!TYPE_FIELD_STATIC (type, i) && TYPE_FIELD_PACKED (type, i))
	    {
	      struct value *v;

	      /* Bitfields require special handling, especially due to byte
	         order problems.  */
	      if (TYPE_FIELD_IGNORE (type, i))
		{
		  fputs_filtered ("<optimized out or zero length>", stream);
		}
	      else
		{
		  v = value_from_longest
		    (TYPE_FIELD_TYPE (type, i), 
		     unpack_field_as_long (type, valaddr + offset, i));

		  common_val_print (v, stream, format, 0, recurse + 1, pretty);
		}
	    }
	  else
	    {
	      if (TYPE_FIELD_IGNORE (type, i))
		{
		  fputs_filtered ("<optimized out or zero length>", stream);
		}
	      else if (TYPE_FIELD_STATIC (type, i))
		{
		  struct value *v = value_static_field (type, i);
		  if (v == NULL)
		    fputs_filtered ("<optimized out>", stream);
		  else
		    cp_print_static_field (TYPE_FIELD_TYPE (type, i), v,
					   stream, format, recurse + 1,
					   pretty);
		}
	      else
		{
		  val_print (TYPE_FIELD_TYPE (type, i),
			     valaddr, offset + TYPE_FIELD_BITPOS (type, i) / 8,
			     address + TYPE_FIELD_BITPOS (type, i) / 8,
			     stream, format, 0, recurse + 1, pretty);
		}
	    }
	  annotate_field_end ();
	}

      if (dont_print_statmem == 0)
	{
	  /* Free the space used to deal with the printing
	     of the members from top level.  */
	  obstack_free (&dont_print_statmem_obstack, last_dont_print);
	  dont_print_statmem_obstack = tmp_obstack;
	}

      if (pretty)
	{
	  fprintf_filtered (stream, "\n");
	  print_spaces_filtered (2 * recurse, stream);
	}
    }				/* if there are data fields */
  /* Now print out the virtual table pointer if there is one */
  if (TYPE_HAS_VTABLE (type)
      && strncmp (TYPE_FIELD_NAME (type, n_baseclasses),
		  hpacc_vtbl_ptr_name, 5) == 0)
    {
      struct value *v;
      /* First get the virtual table pointer and print it out */

#if 0
      fputs_filtered ("__vfp = ", stream);
#endif

      fputs_filtered (", Virtual table at ", stream);

      /* pai: FIXME 32x64 problem? */
      /* Not sure what the best notation is in the case where there is no
         baseclass name.  */
      v = value_from_pointer (lookup_pointer_type (builtin_type_unsigned_long),
			      *(unsigned long *) (valaddr + offset));

      common_val_print (v, stream, format, 0, recurse + 1, pretty);
      fields_seen = 1;

      if (vtblprint)
	{
	  /* Print out function pointers in vtable. */

	  /* FIXME: then-clause is for non-RRBC layout of virtual
	   * table.  The RRBC case in the else-clause is yet to be
	   * implemented.  The if (1) below should be changed to a
	   * test for whether the executable we have was compiled
	   * with a version of HP aCC that doesn't have RRBC
	   * support. */

	  if (1)
	    {
	      /* no RRBC support; function pointers embedded directly
                 in vtable */

	      int vfuncs = count_virtual_fns (real_type);

	      fputs_filtered (" {", stream);

	      /* FIXME : doesn't work at present */
#if 0
	      fprintf_filtered (stream, "%d entr%s: ", vfuncs,
				vfuncs == 1 ? "y" : "ies");
#else
	      fputs_filtered ("not implemented", stream);


#endif

	      /* recursive function that prints all virtual function entries */
#if 0
	      cp_print_hpacc_virtual_table_entries (real_type, &vfuncs, v,
						    stream, format, recurse,
						    pretty);
#endif
	      fputs_filtered ("}", stream);
	    }			/* non-RRBC case */
	  else
	    {
	      /* FIXME -- see comments above */
	      /* RRBC support present; function pointers are found
	       * by indirection through the class segment entries. */


	    }			/* RRBC case */
	}			/* if vtblprint */

      if (pretty)
	{
	  fprintf_filtered (stream, "\n");
	  print_spaces_filtered (2 * recurse, stream);
	}

    }				/* if vtable exists */

  fprintf_filtered (stream, "}");
}

/* Special val_print routine to avoid printing multiple copies of virtual
   baseclasses.  */

static void
cp_print_value (struct type *type, struct type *real_type, char *valaddr,
		int offset, CORE_ADDR address, struct ui_file *stream,
		int format, int recurse, enum val_prettyprint pretty,
		struct type **dont_print_vb)
{
  struct obstack tmp_obstack;
  struct type **last_dont_print
    = (struct type **) obstack_next_free (&dont_print_vb_obstack);
  int i, n_baseclasses = TYPE_N_BASECLASSES (type);
  int thisoffset;
  struct type *thistype;

  if (dont_print_vb == 0)
    {
      /* If we're at top level, carve out a completely fresh
         chunk of the obstack and use that until this particular
         invocation returns.  */
      tmp_obstack = dont_print_vb_obstack;
      /* Bump up the high-water mark.  Now alpha is omega.  */
      obstack_finish (&dont_print_vb_obstack);
    }

  for (i = 0; i < n_baseclasses; i++)
    {
      int boffset;
      int skip;
      struct type *baseclass = check_typedef (TYPE_BASECLASS (type, i));
      char *basename = TYPE_NAME (baseclass);
      char *base_valaddr;

      if (BASETYPE_VIA_VIRTUAL (type, i))
	{
	  struct type **first_dont_print
	    = (struct type **) obstack_base (&dont_print_vb_obstack);

	  int j = (struct type **) obstack_next_free (&dont_print_vb_obstack)
	    - first_dont_print;

	  while (--j >= 0)
	    if (baseclass == first_dont_print[j])
	      goto flush_it;

	  obstack_ptr_grow (&dont_print_vb_obstack, baseclass);
	}

      thisoffset = offset;
      thistype = real_type;
      if (TYPE_HAS_VTABLE (type) && BASETYPE_VIA_VIRTUAL (type, i))
	{
	  /* Assume HP/Taligent runtime convention */
	  find_rt_vbase_offset (type, TYPE_BASECLASS (type, i),
				valaddr, offset, &boffset, &skip);
	  if (skip >= 0)
	    error ("Virtual base class offset not found from vtable while"
		   " printing");
	  base_valaddr = valaddr;
	}
      else
	{
	  boffset = baseclass_offset (type, i,
				      valaddr + offset,
				      address);
	  skip = ((boffset == -1) || (boffset + offset) < 0) ? 1 : -1;

	  if (BASETYPE_VIA_VIRTUAL (type, i))
	    {
	      /* The virtual base class pointer might have been
	         clobbered by the user program. Make sure that it
	         still points to a valid memory location.  */

	      if (boffset != -1
		  && ((boffset + offset) < 0
		      || (boffset + offset) >= TYPE_LENGTH (type)))
		{
		  /* FIXME (alloca): unsafe if baseclass is really really large. */
		  base_valaddr = (char *) alloca (TYPE_LENGTH (baseclass));
		  if (target_read_memory (address + boffset, base_valaddr,
					  TYPE_LENGTH (baseclass)) != 0)
		    skip = 1;
		  address = address + boffset;
		  thisoffset = 0;
		  boffset = 0;
		  thistype = baseclass;
		}
	      else
		base_valaddr = valaddr;
	    }
	  else
	    base_valaddr = valaddr;
	}

      /* now do the printing */
      if (pretty)
	{
	  fprintf_filtered (stream, "\n");
	  print_spaces_filtered (2 * recurse, stream);
	}
      fputs_filtered ("<", stream);
      /* Not sure what the best notation is in the case where there is no
         baseclass name.  */
      fputs_filtered (basename ? basename : "", stream);
      fputs_filtered ("> = ", stream);


      if (skip >= 1)
	fprintf_filtered (stream, "<invalid address>");
      else
	cp_print_value_fields (baseclass, thistype, base_valaddr,
			       thisoffset + boffset, address + boffset,
			       stream, format,
			       recurse, pretty,
			       ((struct type **)
				obstack_base (&dont_print_vb_obstack)),
			       0);
      fputs_filtered (", ", stream);

    flush_it:
      ;
    }

  if (dont_print_vb == 0)
    {
      /* Free the space used to deal with the printing
         of this type from top level.  */
      obstack_free (&dont_print_vb_obstack, last_dont_print);
      /* Reset watermark so that we can continue protecting
         ourselves from whatever we were protecting ourselves.  */
      dont_print_vb_obstack = tmp_obstack;
    }
}

/* Print value of a static member.
   To avoid infinite recursion when printing a class that contains
   a static instance of the class, we keep the addresses of all printed
   static member classes in an obstack and refuse to print them more
   than once.

   VAL contains the value to print, TYPE, STREAM, RECURSE, and PRETTY
   have the same meanings as in c_val_print.  */

static void
cp_print_static_field (struct type *type,
		       struct value *val,
		       struct ui_file *stream,
		       int format,
		       int recurse,
		       enum val_prettyprint pretty)
{
  if (TYPE_CODE (type) == TYPE_CODE_STRUCT)
    {
      CORE_ADDR *first_dont_print;
      int i;

      first_dont_print
	= (CORE_ADDR *) obstack_base (&dont_print_statmem_obstack);
      i = (CORE_ADDR *) obstack_next_free (&dont_print_statmem_obstack)
	- first_dont_print;

      while (--i >= 0)
	{
	  if (VALUE_ADDRESS (val) == first_dont_print[i])
	    {
	      fputs_filtered ("<same as static member of an already"
			      " seen type>",
			      stream);
	      return;
	    }
	}

      obstack_grow (&dont_print_statmem_obstack, (char *) &VALUE_ADDRESS (val),
		    sizeof (CORE_ADDR));

      CHECK_TYPEDEF (type);
      cp_print_value_fields (type, type, VALUE_CONTENTS_ALL (val),
			     VALUE_EMBEDDED_OFFSET (val), VALUE_ADDRESS (val),
			     stream, format, recurse, pretty, NULL, 1);
      return;
    }
  val_print (type, VALUE_CONTENTS_ALL (val), 
	     VALUE_EMBEDDED_OFFSET (val), VALUE_ADDRESS (val),
	     stream, format, 0, recurse, pretty);
}

void
cp_print_class_member (char *valaddr, struct type *domain,
		       struct ui_file *stream, char *prefix)
{

  /* VAL is a byte offset into the structure type DOMAIN.
     Find the name of the field for that offset and
     print it.  */
  int extra = 0;
  int bits = 0;
  unsigned int i;
  unsigned len = TYPE_NFIELDS (domain);

  /* @@ Make VAL into bit offset */

  /* Note: HP aCC generates offsets that are the real byte offsets added
     to a constant bias 0x20000000 (1 << 29).  This constant bias gets
     shifted out in the code below -- joyous happenstance! */

  /* Note: HP cfront uses a constant bias of 1; if we support this
     compiler ever, we will have to adjust the computation below */

  LONGEST val = unpack_long (builtin_type_int, valaddr) << 3;
  for (i = TYPE_N_BASECLASSES (domain); i < len; i++)
    {
      int bitpos = TYPE_FIELD_BITPOS (domain, i);
      QUIT;
      if (val == bitpos)
	break;
      if (val < bitpos && i != 0)
	{
	  /* Somehow pointing into a field.  */
	  i -= 1;
	  extra = (val - TYPE_FIELD_BITPOS (domain, i));
	  if (extra & 0x7)
	    bits = 1;
	  else
	    extra >>= 3;
	  break;
	}
    }
  if (i < len)
    {
      char *name;
      fputs_filtered (prefix, stream);
      name = type_name_no_tag (domain);
      if (name)
	fputs_filtered (name, stream);
      else
	c_type_print_base (domain, stream, 0, 0);
      fprintf_filtered (stream, "::");
      fputs_filtered (TYPE_FIELD_NAME (domain, i), stream);
      if (extra)
	fprintf_filtered (stream, " + %d bytes", extra);
      if (bits)
	fprintf_filtered (stream, " (offset in bits)");
    }
  else
    fprintf_filtered (stream, "%ld", (long) (val >> 3));
}


/* This function prints out virtual table entries for a class; it
 * recurses on the base classes to find all virtual functions
 * available in a class.
 *
 * pai/1997-05-21 Note: As the name suggests, it's currently
 * implemented for HP aCC runtime only. g++ objects are handled
 * differently and I have made no attempt to fold that logic in
 * here. The runtime layout is different for the two cases.  Also,
 * this currently has only the code for non-RRBC layouts generated by
 * the HP aCC compiler; RRBC code is stubbed out and will have to be
 * added later. */


static void
cp_print_hpacc_virtual_table_entries (struct type *type, int *vfuncs,
				      struct value *v, struct ui_file *stream,
				      int format, int recurse,
				      enum val_prettyprint pretty)
{
  int fn, oi;

  /* pai: FIXME this function doesn't work. It should handle a given
   * virtual function only once (latest redefinition in class hierarchy)
   */

  /* Recursion on other classes that can share the same vtable */
  struct type *pbc = primary_base_class (type);
  if (pbc)
    cp_print_hpacc_virtual_table_entries (pbc, vfuncs, v, stream, format,
					  recurse, pretty);

  /* Now deal with vfuncs declared in this class */
  for (fn = 0; fn < TYPE_NFN_FIELDS (type); fn++)
    for (oi = 0; oi < TYPE_FN_FIELDLIST_LENGTH (type, fn); oi++)
      if (TYPE_FN_FIELD_VIRTUAL_P (TYPE_FN_FIELDLIST1 (type, fn), oi))
	{
	  char *vf_name;
	  const char *field_physname;

	  /* virtual function offset */
	  int vx = (TYPE_FN_FIELD_VOFFSET (TYPE_FN_FIELDLIST1 (type, fn), oi)
		    - 1);

	  /* Get the address of the vfunction entry */
	  struct value *vf = value_copy (v);
	  if (VALUE_LAZY (vf))
	    (void) value_fetch_lazy (vf);
	  /* adjust by offset */
	  vf->aligner.contents[0] += 4 * (HP_ACC_VFUNC_START + vx);
	  vf = value_ind (vf);	/* get the entry */
	  VALUE_TYPE (vf) = VALUE_TYPE (v);	/* make it a pointer */

	  /* print out the entry */
	  common_val_print (vf, stream, format, 0, recurse + 1, pretty);
	  field_physname
	    = TYPE_FN_FIELD_PHYSNAME (TYPE_FN_FIELDLIST1 (type, fn), oi);
	  /* pai: (temp) FIXME Maybe this should be DMGL_ANSI */
	  vf_name = cplus_demangle (field_physname, DMGL_ARM);
	  fprintf_filtered (stream, " %s", vf_name);
	  if (--(*vfuncs) > 0)
	    fputs_filtered (", ", stream);
	}
}



void
_initialize_cp_valprint (void)
{
  add_show_from_set
    (add_set_cmd ("static-members", class_support, var_boolean,
		  (char *) &static_field_print,
		  "Set printing of C++ static members.",
		  &setprintlist),
     &showprintlist);
  /* Turn on printing of static fields.  */
  static_field_print = 1;

  add_show_from_set
    (add_set_cmd ("vtbl", class_support, var_boolean, (char *) &vtblprint,
		  "Set printing of C++ virtual function tables.",
		  &setprintlist),
     &showprintlist);

  add_show_from_set
    (add_set_cmd ("object", class_support, var_boolean, (char *) &objectprint,
	      "Set printing of object's derived type based on vtable info.",
		  &setprintlist),
     &showprintlist);

  /* Give people the defaults which they are used to.  */
  objectprint = 0;
  vtblprint = 0;
  obstack_begin (&dont_print_vb_obstack, 32 * sizeof (struct type *));
  obstack_specify_allocation (&dont_print_statmem_obstack,
			      32 * sizeof (CORE_ADDR), sizeof (CORE_ADDR),
			      xmalloc, xfree);
}
