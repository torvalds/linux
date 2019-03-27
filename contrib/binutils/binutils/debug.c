/* debug.c -- Handle generic debugging information.
   Copyright 1995, 1996, 1997, 1998, 1999, 2000, 2002, 2003, 2007
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

/* This file implements a generic debugging format.  We may eventually
   have readers which convert different formats into this generic
   format, and writers which write it out.  The initial impetus for
   this was writing a converter from stabs to HP IEEE-695 debugging
   format.  */

#include "sysdep.h"
#include <assert.h>
#include "bfd.h"
#include "libiberty.h"
#include "debug.h"

/* Global information we keep for debugging.  A pointer to this
   structure is the debugging handle passed to all the routines.  */

struct debug_handle
{
  /* A linked list of compilation units.  */
  struct debug_unit *units;
  /* The current compilation unit.  */
  struct debug_unit *current_unit;
  /* The current source file.  */
  struct debug_file *current_file;
  /* The current function.  */
  struct debug_function *current_function;
  /* The current block.  */
  struct debug_block *current_block;
  /* The current line number information for the current unit.  */
  struct debug_lineno *current_lineno;
  /* Mark.  This is used by debug_write.  */
  unsigned int mark;
  /* A struct/class ID used by debug_write.  */
  unsigned int class_id;
  /* The base for class_id for this call to debug_write.  */
  unsigned int base_id;
  /* The current line number in debug_write.  */
  struct debug_lineno *current_write_lineno;
  unsigned int current_write_lineno_index;
  /* A list of classes which have assigned ID's during debug_write.
     This is linked through the next_id field of debug_class_type.  */
  struct debug_class_id *id_list;
  /* A list used to avoid recursion during debug_type_samep.  */
  struct debug_type_compare_list *compare_list;
};

/* Information we keep for a single compilation unit.  */

struct debug_unit
{
  /* The next compilation unit.  */
  struct debug_unit *next;
  /* A list of files included in this compilation unit.  The first
     file is always the main one, and that is where the main file name
     is stored.  */
  struct debug_file *files;
  /* Line number information for this compilation unit.  This is not
     stored by function, because assembler code may have line number
     information without function information.  */
  struct debug_lineno *linenos;
};

/* Information kept for a single source file.  */

struct debug_file
{
  /* The next source file in this compilation unit.  */
  struct debug_file *next;
  /* The name of the source file.  */
  const char *filename;
  /* Global functions, variables, types, etc.  */
  struct debug_namespace *globals;
};

/* A type.  */

struct debug_type
{
  /* Kind of type.  */
  enum debug_type_kind kind;
  /* Size of type (0 if not known).  */
  unsigned int size;
  /* Type which is a pointer to this type.  */
  debug_type pointer;
  /* Tagged union with additional information about the type.  */
  union
    {
      /* DEBUG_KIND_INDIRECT.  */
      struct debug_indirect_type *kindirect;
      /* DEBUG_KIND_INT.  */
      /* Whether the integer is unsigned.  */
      bfd_boolean kint;
      /* DEBUG_KIND_STRUCT, DEBUG_KIND_UNION, DEBUG_KIND_CLASS,
         DEBUG_KIND_UNION_CLASS.  */
      struct debug_class_type *kclass;
      /* DEBUG_KIND_ENUM.  */
      struct debug_enum_type *kenum;
      /* DEBUG_KIND_POINTER.  */
      struct debug_type *kpointer;
      /* DEBUG_KIND_FUNCTION.  */
      struct debug_function_type *kfunction;
      /* DEBUG_KIND_REFERENCE.  */
      struct debug_type *kreference;
      /* DEBUG_KIND_RANGE.  */
      struct debug_range_type *krange;
      /* DEBUG_KIND_ARRAY.  */
      struct debug_array_type *karray;
      /* DEBUG_KIND_SET.  */
      struct debug_set_type *kset;
      /* DEBUG_KIND_OFFSET.  */
      struct debug_offset_type *koffset;
      /* DEBUG_KIND_METHOD.  */
      struct debug_method_type *kmethod;
      /* DEBUG_KIND_CONST.  */
      struct debug_type *kconst;
      /* DEBUG_KIND_VOLATILE.  */
      struct debug_type *kvolatile;
      /* DEBUG_KIND_NAMED, DEBUG_KIND_TAGGED.  */
      struct debug_named_type *knamed;
    } u;
};

/* Information kept for an indirect type.  */

struct debug_indirect_type
{
  /* Slot where the final type will appear.  */
  debug_type *slot;
  /* Tag.  */
  const char *tag;
};

/* Information kept for a struct, union, or class.  */

struct debug_class_type
{
  /* NULL terminated array of fields.  */
  debug_field *fields;
  /* A mark field which indicates whether the struct has already been
     printed.  */
  unsigned int mark;
  /* This is used to uniquely identify unnamed structs when printing.  */
  unsigned int id;
  /* The remaining fields are only used for DEBUG_KIND_CLASS and
     DEBUG_KIND_UNION_CLASS.  */
  /* NULL terminated array of base classes.  */
  debug_baseclass *baseclasses;
  /* NULL terminated array of methods.  */
  debug_method *methods;
  /* The type of the class providing the virtual function table for
     this class.  This may point to the type itself.  */
  debug_type vptrbase;
};

/* Information kept for an enum.  */

struct debug_enum_type
{
  /* NULL terminated array of names.  */
  const char **names;
  /* Array of corresponding values.  */
  bfd_signed_vma *values;
};

/* Information kept for a function.  FIXME: We should be able to
   record the parameter types.  */

struct debug_function_type
{
  /* Return type.  */
  debug_type return_type;
  /* NULL terminated array of argument types.  */
  debug_type *arg_types;
  /* Whether the function takes a variable number of arguments.  */
  bfd_boolean varargs;
};

/* Information kept for a range.  */

struct debug_range_type
{
  /* Range base type.  */
  debug_type type;
  /* Lower bound.  */
  bfd_signed_vma lower;
  /* Upper bound.  */
  bfd_signed_vma upper;
};

/* Information kept for an array.  */

struct debug_array_type
{
  /* Element type.  */
  debug_type element_type;
  /* Range type.  */
  debug_type range_type;
  /* Lower bound.  */
  bfd_signed_vma lower;
  /* Upper bound.  */
  bfd_signed_vma upper;
  /* Whether this array is really a string.  */
  bfd_boolean stringp;
};

/* Information kept for a set.  */

struct debug_set_type
{
  /* Base type.  */
  debug_type type;
  /* Whether this set is really a bitstring.  */
  bfd_boolean bitstringp;
};

/* Information kept for an offset type (a based pointer).  */

struct debug_offset_type
{
  /* The type the pointer is an offset from.  */
  debug_type base_type;
  /* The type the pointer points to.  */
  debug_type target_type;
};

/* Information kept for a method type.  */

struct debug_method_type
{
  /* The return type.  */
  debug_type return_type;
  /* The object type which this method is for.  */
  debug_type domain_type;
  /* A NULL terminated array of argument types.  */
  debug_type *arg_types;
  /* Whether the method takes a variable number of arguments.  */
  bfd_boolean varargs;
};

/* Information kept for a named type.  */

struct debug_named_type
{
  /* Name.  */
  struct debug_name *name;
  /* Real type.  */
  debug_type type;
};

/* A field in a struct or union.  */

struct debug_field
{
  /* Name of the field.  */
  const char *name;
  /* Type of the field.  */
  struct debug_type *type;
  /* Visibility of the field.  */
  enum debug_visibility visibility;
  /* Whether this is a static member.  */
  bfd_boolean static_member;
  union
    {
      /* If static_member is false.  */
      struct
	{
	  /* Bit position of the field in the struct.  */
	  unsigned int bitpos;
	  /* Size of the field in bits.  */
	  unsigned int bitsize;
	} f;
      /* If static_member is true.  */
      struct
	{
	  const char *physname;
	} s;
    } u;
};

/* A base class for an object.  */

struct debug_baseclass
{
  /* Type of the base class.  */
  struct debug_type *type;
  /* Bit position of the base class in the object.  */
  unsigned int bitpos;
  /* Whether the base class is virtual.  */
  bfd_boolean virtual;
  /* Visibility of the base class.  */
  enum debug_visibility visibility;
};

/* A method of an object.  */

struct debug_method
{
  /* The name of the method.  */
  const char *name;
  /* A NULL terminated array of different types of variants.  */
  struct debug_method_variant **variants;
};

/* The variants of a method function of an object.  These indicate
   which method to run.  */

struct debug_method_variant
{
  /* The physical name of the function.  */
  const char *physname;
  /* The type of the function.  */
  struct debug_type *type;
  /* The visibility of the function.  */
  enum debug_visibility visibility;
  /* Whether the function is const.  */
  bfd_boolean constp;
  /* Whether the function is volatile.  */
  bfd_boolean volatilep;
  /* The offset to the function in the virtual function table.  */
  bfd_vma voffset;
  /* If voffset is VOFFSET_STATIC_METHOD, this is a static method.  */
#define VOFFSET_STATIC_METHOD ((bfd_vma) -1)
  /* Context of a virtual method function.  */
  struct debug_type *context;
};

/* A variable.  This is the information we keep for a variable object.
   This has no name; a name is associated with a variable in a
   debug_name structure.  */

struct debug_variable
{
  /* Kind of variable.  */
  enum debug_var_kind kind;
  /* Type.  */
  debug_type type;
  /* Value.  The interpretation of the value depends upon kind.  */
  bfd_vma val;
};

/* A function.  This has no name; a name is associated with a function
   in a debug_name structure.  */

struct debug_function
{
  /* Return type.  */
  debug_type return_type;
  /* Parameter information.  */
  struct debug_parameter *parameters;
  /* Block information.  The first structure on the list is the main
     block of the function, and describes function local variables.  */
  struct debug_block *blocks;
};

/* A function parameter.  */

struct debug_parameter
{
  /* Next parameter.  */
  struct debug_parameter *next;
  /* Name.  */
  const char *name;
  /* Type.  */
  debug_type type;
  /* Kind.  */
  enum debug_parm_kind kind;
  /* Value (meaning depends upon kind).  */
  bfd_vma val;
};

/* A typed constant.  */

struct debug_typed_constant
{
  /* Type.  */
  debug_type type;
  /* Value.  FIXME: We may eventually need to support non-integral
     values.  */
  bfd_vma val;
};

/* Information about a block within a function.  */

struct debug_block
{
  /* Next block with the same parent.  */
  struct debug_block *next;
  /* Parent block.  */
  struct debug_block *parent;
  /* List of child blocks.  */
  struct debug_block *children;
  /* Start address of the block.  */
  bfd_vma start;
  /* End address of the block.  */
  bfd_vma end;
  /* Local variables.  */
  struct debug_namespace *locals;
};

/* Line number information we keep for a compilation unit.  FIXME:
   This structure is easy to create, but can be very space
   inefficient.  */

struct debug_lineno
{
  /* More line number information for this block.  */
  struct debug_lineno *next;
  /* Source file.  */
  struct debug_file *file;
  /* Line numbers, terminated by a -1 or the end of the array.  */
#define DEBUG_LINENO_COUNT 10
  unsigned long linenos[DEBUG_LINENO_COUNT];
  /* Addresses for the line numbers.  */
  bfd_vma addrs[DEBUG_LINENO_COUNT];
};

/* A namespace.  This is a mapping from names to objects.  FIXME: This
   should be implemented as a hash table.  */

struct debug_namespace
{
  /* List of items in this namespace.  */
  struct debug_name *list;
  /* Pointer to where the next item in this namespace should go.  */
  struct debug_name **tail;
};

/* Kinds of objects that appear in a namespace.  */

enum debug_object_kind
{
  /* A type.  */
  DEBUG_OBJECT_TYPE,
  /* A tagged type (really a different sort of namespace).  */
  DEBUG_OBJECT_TAG,
  /* A variable.  */
  DEBUG_OBJECT_VARIABLE,
  /* A function.  */
  DEBUG_OBJECT_FUNCTION,
  /* An integer constant.  */
  DEBUG_OBJECT_INT_CONSTANT,
  /* A floating point constant.  */
  DEBUG_OBJECT_FLOAT_CONSTANT,
  /* A typed constant.  */
  DEBUG_OBJECT_TYPED_CONSTANT
};

/* Linkage of an object that appears in a namespace.  */

enum debug_object_linkage
{
  /* Local variable.  */
  DEBUG_LINKAGE_AUTOMATIC,
  /* Static--either file static or function static, depending upon the
     namespace is.  */
  DEBUG_LINKAGE_STATIC,
  /* Global.  */
  DEBUG_LINKAGE_GLOBAL,
  /* No linkage.  */
  DEBUG_LINKAGE_NONE
};

/* A name in a namespace.  */

struct debug_name
{
  /* Next name in this namespace.  */
  struct debug_name *next;
  /* Name.  */
  const char *name;
  /* Mark.  This is used by debug_write.  */
  unsigned int mark;
  /* Kind of object.  */
  enum debug_object_kind kind;
  /* Linkage of object.  */
  enum debug_object_linkage linkage;
  /* Tagged union with additional information about the object.  */
  union
    {
      /* DEBUG_OBJECT_TYPE.  */
      struct debug_type *type;
      /* DEBUG_OBJECT_TAG.  */
      struct debug_type *tag;
      /* DEBUG_OBJECT_VARIABLE.  */
      struct debug_variable *variable;
      /* DEBUG_OBJECT_FUNCTION.  */
      struct debug_function *function;
      /* DEBUG_OBJECT_INT_CONSTANT.  */
      bfd_vma int_constant;
      /* DEBUG_OBJECT_FLOAT_CONSTANT.  */
      double float_constant;
      /* DEBUG_OBJECT_TYPED_CONSTANT.  */
      struct debug_typed_constant *typed_constant;
    } u;
};

/* During debug_write, a linked list of these structures is used to
   keep track of ID numbers that have been assigned to classes.  */

struct debug_class_id
{
  /* Next ID number.  */
  struct debug_class_id *next;
  /* The type with the ID.  */
  struct debug_type *type;
  /* The tag; NULL if no tag.  */
  const char *tag;
};

/* During debug_type_samep, a linked list of these structures is kept
   on the stack to avoid infinite recursion.  */

struct debug_type_compare_list
{
  /* Next type on list.  */
  struct debug_type_compare_list *next;
  /* The types we are comparing.  */
  struct debug_type *t1;
  struct debug_type *t2;
};

/* During debug_get_real_type, a linked list of these structures is
   kept on the stack to avoid infinite recursion.  */

struct debug_type_real_list
{
  /* Next type on list.  */
  struct debug_type_real_list *next;
  /* The type we are checking.  */
  struct debug_type *t;
};

/* Local functions.  */

static void debug_error (const char *);
static struct debug_name *debug_add_to_namespace
  (struct debug_handle *, struct debug_namespace **, const char *,
   enum debug_object_kind, enum debug_object_linkage);
static struct debug_name *debug_add_to_current_namespace
  (struct debug_handle *, const char *, enum debug_object_kind,
   enum debug_object_linkage);
static struct debug_type *debug_make_type
  (struct debug_handle *, enum debug_type_kind, unsigned int);
static struct debug_type *debug_get_real_type
  (void *, debug_type, struct debug_type_real_list *);
static bfd_boolean debug_write_name
  (struct debug_handle *, const struct debug_write_fns *, void *,
   struct debug_name *);
static bfd_boolean debug_write_type
  (struct debug_handle *, const struct debug_write_fns *, void *,
   struct debug_type *, struct debug_name *);
static bfd_boolean debug_write_class_type
  (struct debug_handle *, const struct debug_write_fns *, void *,
   struct debug_type *, const char *);
static bfd_boolean debug_write_function
  (struct debug_handle *, const struct debug_write_fns *, void *,
   const char *, enum debug_object_linkage, struct debug_function *);
static bfd_boolean debug_write_block
  (struct debug_handle *, const struct debug_write_fns *, void *,
   struct debug_block *);
static bfd_boolean debug_write_linenos
  (struct debug_handle *, const struct debug_write_fns *, void *, bfd_vma);
static bfd_boolean debug_set_class_id
  (struct debug_handle *, const char *, struct debug_type *);
static bfd_boolean debug_type_samep
  (struct debug_handle *, struct debug_type *, struct debug_type *);
static bfd_boolean debug_class_type_samep
  (struct debug_handle *, struct debug_type *, struct debug_type *);

/* Issue an error message.  */

static void
debug_error (const char *message)
{
  fprintf (stderr, "%s\n", message);
}

/* Add an object to a namespace.  */

static struct debug_name *
debug_add_to_namespace (struct debug_handle *info ATTRIBUTE_UNUSED,
			struct debug_namespace **nsp, const char *name,
			enum debug_object_kind kind,
			enum debug_object_linkage linkage)
{
  struct debug_name *n;
  struct debug_namespace *ns;

  n = (struct debug_name *) xmalloc (sizeof *n);
  memset (n, 0, sizeof *n);

  n->name = name;
  n->kind = kind;
  n->linkage = linkage;

  ns = *nsp;
  if (ns == NULL)
    {
      ns = (struct debug_namespace *) xmalloc (sizeof *ns);
      memset (ns, 0, sizeof *ns);

      ns->tail = &ns->list;

      *nsp = ns;
    }

  *ns->tail = n;
  ns->tail = &n->next;

  return n;
}

/* Add an object to the current namespace.  */

static struct debug_name *
debug_add_to_current_namespace (struct debug_handle *info, const char *name,
				enum debug_object_kind kind,
				enum debug_object_linkage linkage)
{
  struct debug_namespace **nsp;

  if (info->current_unit == NULL
      || info->current_file == NULL)
    {
      debug_error (_("debug_add_to_current_namespace: no current file"));
      return NULL;
    }

  if (info->current_block != NULL)
    nsp = &info->current_block->locals;
  else
    nsp = &info->current_file->globals;

  return debug_add_to_namespace (info, nsp, name, kind, linkage);
}

/* Return a handle for debugging information.  */

void *
debug_init (void)
{
  struct debug_handle *ret;

  ret = (struct debug_handle *) xmalloc (sizeof *ret);
  memset (ret, 0, sizeof *ret);
  return (void *) ret;
}

/* Set the source filename.  This implicitly starts a new compilation
   unit.  */

bfd_boolean
debug_set_filename (void *handle, const char *name)
{
  struct debug_handle *info = (struct debug_handle *) handle;
  struct debug_file *nfile;
  struct debug_unit *nunit;

  if (name == NULL)
    name = "";

  nfile = (struct debug_file *) xmalloc (sizeof *nfile);
  memset (nfile, 0, sizeof *nfile);

  nfile->filename = name;

  nunit = (struct debug_unit *) xmalloc (sizeof *nunit);
  memset (nunit, 0, sizeof *nunit);

  nunit->files = nfile;
  info->current_file = nfile;

  if (info->current_unit != NULL)
    info->current_unit->next = nunit;
  else
    {
      assert (info->units == NULL);
      info->units = nunit;
    }

  info->current_unit = nunit;

  info->current_function = NULL;
  info->current_block = NULL;
  info->current_lineno = NULL;

  return TRUE;
}

/* Change source files to the given file name.  This is used for
   include files in a single compilation unit.  */

bfd_boolean
debug_start_source (void *handle, const char *name)
{
  struct debug_handle *info = (struct debug_handle *) handle;
  struct debug_file *f, **pf;

  if (name == NULL)
    name = "";

  if (info->current_unit == NULL)
    {
      debug_error (_("debug_start_source: no debug_set_filename call"));
      return FALSE;
    }

  for (f = info->current_unit->files; f != NULL; f = f->next)
    {
      if (f->filename[0] == name[0]
	  && f->filename[1] == name[1]
	  && strcmp (f->filename, name) == 0)
	{
	  info->current_file = f;
	  return TRUE;
	}
    }

  f = (struct debug_file *) xmalloc (sizeof *f);
  memset (f, 0, sizeof *f);

  f->filename = name;

  for (pf = &info->current_file->next;
       *pf != NULL;
       pf = &(*pf)->next)
    ;
  *pf = f;

  info->current_file = f;

  return TRUE;
}

/* Record a function definition.  This implicitly starts a function
   block.  The debug_type argument is the type of the return value.
   The boolean indicates whether the function is globally visible.
   The bfd_vma is the address of the start of the function.  Currently
   the parameter types are specified by calls to
   debug_record_parameter.  FIXME: There is no way to specify nested
   functions.  */

bfd_boolean
debug_record_function (void *handle, const char *name,
		       debug_type return_type, bfd_boolean global,
		       bfd_vma addr)
{
  struct debug_handle *info = (struct debug_handle *) handle;
  struct debug_function *f;
  struct debug_block *b;
  struct debug_name *n;

  if (name == NULL)
    name = "";
  if (return_type == NULL)
    return FALSE;

  if (info->current_unit == NULL)
    {
      debug_error (_("debug_record_function: no debug_set_filename call"));
      return FALSE;
    }

  f = (struct debug_function *) xmalloc (sizeof *f);
  memset (f, 0, sizeof *f);

  f->return_type = return_type;

  b = (struct debug_block *) xmalloc (sizeof *b);
  memset (b, 0, sizeof *b);

  b->start = addr;
  b->end = (bfd_vma) -1;

  f->blocks = b;

  info->current_function = f;
  info->current_block = b;

  /* FIXME: If we could handle nested functions, this would be the
     place: we would want to use a different namespace.  */
  n = debug_add_to_namespace (info,
			      &info->current_file->globals,
			      name,
			      DEBUG_OBJECT_FUNCTION,
			      (global
			       ? DEBUG_LINKAGE_GLOBAL
			       : DEBUG_LINKAGE_STATIC));
  if (n == NULL)
    return FALSE;

  n->u.function = f;

  return TRUE;
}

/* Record a parameter for the current function.  */

bfd_boolean
debug_record_parameter (void *handle, const char *name, debug_type type,
			enum debug_parm_kind kind, bfd_vma val)
{
  struct debug_handle *info = (struct debug_handle *) handle;
  struct debug_parameter *p, **pp;

  if (name == NULL || type == NULL)
    return FALSE;

  if (info->current_unit == NULL
      || info->current_function == NULL)
    {
      debug_error (_("debug_record_parameter: no current function"));
      return FALSE;
    }

  p = (struct debug_parameter *) xmalloc (sizeof *p);
  memset (p, 0, sizeof *p);

  p->name = name;
  p->type = type;
  p->kind = kind;
  p->val = val;

  for (pp = &info->current_function->parameters;
       *pp != NULL;
       pp = &(*pp)->next)
    ;
  *pp = p;

  return TRUE;
}

/* End a function.  FIXME: This should handle function nesting.  */

bfd_boolean
debug_end_function (void *handle, bfd_vma addr)
{
  struct debug_handle *info = (struct debug_handle *) handle;

  if (info->current_unit == NULL
      || info->current_block == NULL
      || info->current_function == NULL)
    {
      debug_error (_("debug_end_function: no current function"));
      return FALSE;
    }

  if (info->current_block->parent != NULL)
    {
      debug_error (_("debug_end_function: some blocks were not closed"));
      return FALSE;
    }

  info->current_block->end = addr;

  info->current_function = NULL;
  info->current_block = NULL;

  return TRUE;
}

/* Start a block in a function.  All local information will be
   recorded in this block, until the matching call to debug_end_block.
   debug_start_block and debug_end_block may be nested.  The bfd_vma
   argument is the address at which this block starts.  */

bfd_boolean
debug_start_block (void *handle, bfd_vma addr)
{
  struct debug_handle *info = (struct debug_handle *) handle;
  struct debug_block *b, **pb;

  /* We must always have a current block: debug_record_function sets
     one up.  */
  if (info->current_unit == NULL
      || info->current_block == NULL)
    {
      debug_error (_("debug_start_block: no current block"));
      return FALSE;
    }

  b = (struct debug_block *) xmalloc (sizeof *b);
  memset (b, 0, sizeof *b);

  b->parent = info->current_block;
  b->start = addr;
  b->end = (bfd_vma) -1;

  /* This new block is a child of the current block.  */
  for (pb = &info->current_block->children;
       *pb != NULL;
       pb = &(*pb)->next)
    ;
  *pb = b;

  info->current_block = b;

  return TRUE;
}

/* Finish a block in a function.  This matches the call to
   debug_start_block.  The argument is the address at which this block
   ends.  */

bfd_boolean
debug_end_block (void *handle, bfd_vma addr)
{
  struct debug_handle *info = (struct debug_handle *) handle;
  struct debug_block *parent;

  if (info->current_unit == NULL
      || info->current_block == NULL)
    {
      debug_error (_("debug_end_block: no current block"));
      return FALSE;
    }

  parent = info->current_block->parent;
  if (parent == NULL)
    {
      debug_error (_("debug_end_block: attempt to close top level block"));
      return FALSE;
    }

  info->current_block->end = addr;

  info->current_block = parent;

  return TRUE;
}

/* Associate a line number in the current source file and function
   with a given address.  */

bfd_boolean
debug_record_line (void *handle, unsigned long lineno, bfd_vma addr)
{
  struct debug_handle *info = (struct debug_handle *) handle;
  struct debug_lineno *l;
  unsigned int i;

  if (info->current_unit == NULL)
    {
      debug_error (_("debug_record_line: no current unit"));
      return FALSE;
    }

  l = info->current_lineno;
  if (l != NULL && l->file == info->current_file)
    {
      for (i = 0; i < DEBUG_LINENO_COUNT; i++)
	{
	  if (l->linenos[i] == (unsigned long) -1)
	    {
	      l->linenos[i] = lineno;
	      l->addrs[i] = addr;
	      return TRUE;
	    }
	}
    }

  /* If we get here, then either 1) there is no current_lineno
     structure, which means this is the first line number in this
     compilation unit, 2) the current_lineno structure is for a
     different file, or 3) the current_lineno structure is full.
     Regardless, we want to allocate a new debug_lineno structure, put
     it in the right place, and make it the new current_lineno
     structure.  */

  l = (struct debug_lineno *) xmalloc (sizeof *l);
  memset (l, 0, sizeof *l);

  l->file = info->current_file;
  l->linenos[0] = lineno;
  l->addrs[0] = addr;
  for (i = 1; i < DEBUG_LINENO_COUNT; i++)
    l->linenos[i] = (unsigned long) -1;

  if (info->current_lineno != NULL)
    info->current_lineno->next = l;
  else
    info->current_unit->linenos = l;

  info->current_lineno = l;

  return TRUE;
}

/* Start a named common block.  This is a block of variables that may
   move in memory.  */

bfd_boolean
debug_start_common_block (void *handle ATTRIBUTE_UNUSED,
			  const char *name ATTRIBUTE_UNUSED)
{
  /* FIXME */
  debug_error (_("debug_start_common_block: not implemented"));
  return FALSE;
}

/* End a named common block.  */

bfd_boolean
debug_end_common_block (void *handle ATTRIBUTE_UNUSED,
			const char *name ATTRIBUTE_UNUSED)
{
  /* FIXME */
  debug_error (_("debug_end_common_block: not implemented"));
  return FALSE;
}

/* Record a named integer constant.  */

bfd_boolean
debug_record_int_const (void *handle, const char *name, bfd_vma val)
{
  struct debug_handle *info = (struct debug_handle *) handle;
  struct debug_name *n;

  if (name == NULL)
    return FALSE;

  n = debug_add_to_current_namespace (info, name, DEBUG_OBJECT_INT_CONSTANT,
				      DEBUG_LINKAGE_NONE);
  if (n == NULL)
    return FALSE;

  n->u.int_constant = val;

  return TRUE;
}

/* Record a named floating point constant.  */

bfd_boolean
debug_record_float_const (void *handle, const char *name, double val)
{
  struct debug_handle *info = (struct debug_handle *) handle;
  struct debug_name *n;

  if (name == NULL)
    return FALSE;

  n = debug_add_to_current_namespace (info, name, DEBUG_OBJECT_FLOAT_CONSTANT,
				      DEBUG_LINKAGE_NONE);
  if (n == NULL)
    return FALSE;

  n->u.float_constant = val;

  return TRUE;
}

/* Record a typed constant with an integral value.  */

bfd_boolean
debug_record_typed_const (void *handle, const char *name, debug_type type,
			  bfd_vma val)
{
  struct debug_handle *info = (struct debug_handle *) handle;
  struct debug_name *n;
  struct debug_typed_constant *tc;

  if (name == NULL || type == NULL)
    return FALSE;

  n = debug_add_to_current_namespace (info, name, DEBUG_OBJECT_TYPED_CONSTANT,
				      DEBUG_LINKAGE_NONE);
  if (n == NULL)
    return FALSE;

  tc = (struct debug_typed_constant *) xmalloc (sizeof *tc);
  memset (tc, 0, sizeof *tc);

  tc->type = type;
  tc->val = val;

  n->u.typed_constant = tc;

  return TRUE;
}

/* Record a label.  */

bfd_boolean
debug_record_label (void *handle ATTRIBUTE_UNUSED,
		    const char *name ATTRIBUTE_UNUSED,
		    debug_type type ATTRIBUTE_UNUSED,
		    bfd_vma addr ATTRIBUTE_UNUSED)
{
  /* FIXME.  */
  debug_error (_("debug_record_label: not implemented"));
  return FALSE;
}

/* Record a variable.  */

bfd_boolean
debug_record_variable (void *handle, const char *name, debug_type type,
		       enum debug_var_kind kind, bfd_vma val)
{
  struct debug_handle *info = (struct debug_handle *) handle;
  struct debug_namespace **nsp;
  enum debug_object_linkage linkage;
  struct debug_name *n;
  struct debug_variable *v;

  if (name == NULL || type == NULL)
    return FALSE;

  if (info->current_unit == NULL
      || info->current_file == NULL)
    {
      debug_error (_("debug_record_variable: no current file"));
      return FALSE;
    }

  if (kind == DEBUG_GLOBAL || kind == DEBUG_STATIC)
    {
      nsp = &info->current_file->globals;
      if (kind == DEBUG_GLOBAL)
	linkage = DEBUG_LINKAGE_GLOBAL;
      else
	linkage = DEBUG_LINKAGE_STATIC;
    }
  else
    {
      if (info->current_block == NULL)
	nsp = &info->current_file->globals;
      else
	nsp = &info->current_block->locals;
      linkage = DEBUG_LINKAGE_AUTOMATIC;
    }

  n = debug_add_to_namespace (info, nsp, name, DEBUG_OBJECT_VARIABLE, linkage);
  if (n == NULL)
    return FALSE;

  v = (struct debug_variable *) xmalloc (sizeof *v);
  memset (v, 0, sizeof *v);

  v->kind = kind;
  v->type = type;
  v->val = val;

  n->u.variable = v;

  return TRUE;
}

/* Make a type with a given kind and size.  */

static struct debug_type *
debug_make_type (struct debug_handle *info ATTRIBUTE_UNUSED,
		 enum debug_type_kind kind, unsigned int size)
{
  struct debug_type *t;

  t = (struct debug_type *) xmalloc (sizeof *t);
  memset (t, 0, sizeof *t);

  t->kind = kind;
  t->size = size;

  return t;
}

/* Make an indirect type which may be used as a placeholder for a type
   which is referenced before it is defined.  */

debug_type
debug_make_indirect_type (void *handle, debug_type *slot, const char *tag)
{
  struct debug_handle *info = (struct debug_handle *) handle;
  struct debug_type *t;
  struct debug_indirect_type *i;

  t = debug_make_type (info, DEBUG_KIND_INDIRECT, 0);
  if (t == NULL)
    return DEBUG_TYPE_NULL;

  i = (struct debug_indirect_type *) xmalloc (sizeof *i);
  memset (i, 0, sizeof *i);

  i->slot = slot;
  i->tag = tag;

  t->u.kindirect = i;

  return t;
}

/* Make a void type.  There is only one of these.  */

debug_type
debug_make_void_type (void *handle)
{
  struct debug_handle *info = (struct debug_handle *) handle;

  return debug_make_type (info, DEBUG_KIND_VOID, 0);
}

/* Make an integer type of a given size.  The boolean argument is true
   if the integer is unsigned.  */

debug_type
debug_make_int_type (void *handle, unsigned int size, bfd_boolean unsignedp)
{
  struct debug_handle *info = (struct debug_handle *) handle;
  struct debug_type *t;

  t = debug_make_type (info, DEBUG_KIND_INT, size);
  if (t == NULL)
    return DEBUG_TYPE_NULL;

  t->u.kint = unsignedp;

  return t;
}

/* Make a floating point type of a given size.  FIXME: On some
   platforms, like an Alpha, you probably need to be able to specify
   the format.  */

debug_type
debug_make_float_type (void *handle, unsigned int size)
{
  struct debug_handle *info = (struct debug_handle *) handle;

  return debug_make_type (info, DEBUG_KIND_FLOAT, size);
}

/* Make a boolean type of a given size.  */

debug_type
debug_make_bool_type (void *handle, unsigned int size)
{
  struct debug_handle *info = (struct debug_handle *) handle;

  return debug_make_type (info, DEBUG_KIND_BOOL, size);
}

/* Make a complex type of a given size.  */

debug_type
debug_make_complex_type (void *handle, unsigned int size)
{
  struct debug_handle *info = (struct debug_handle *) handle;

  return debug_make_type (info, DEBUG_KIND_COMPLEX, size);
}

/* Make a structure type.  The second argument is true for a struct,
   false for a union.  The third argument is the size of the struct.
   The fourth argument is a NULL terminated array of fields.  */

debug_type
debug_make_struct_type (void *handle, bfd_boolean structp, bfd_vma size,
			debug_field *fields)
{
  struct debug_handle *info = (struct debug_handle *) handle;
  struct debug_type *t;
  struct debug_class_type *c;

  t = debug_make_type (info,
		       structp ? DEBUG_KIND_STRUCT : DEBUG_KIND_UNION,
		       size);
  if (t == NULL)
    return DEBUG_TYPE_NULL;

  c = (struct debug_class_type *) xmalloc (sizeof *c);
  memset (c, 0, sizeof *c);

  c->fields = fields;

  t->u.kclass = c;

  return t;
}

/* Make an object type.  The first three arguments after the handle
   are the same as for debug_make_struct_type.  The next arguments are
   a NULL terminated array of base classes, a NULL terminated array of
   methods, the type of the object holding the virtual function table
   if it is not this object, and a boolean which is true if this
   object has its own virtual function table.  */

debug_type
debug_make_object_type (void *handle, bfd_boolean structp, bfd_vma size,
			debug_field *fields, debug_baseclass *baseclasses,
			debug_method *methods, debug_type vptrbase,
			bfd_boolean ownvptr)
{
  struct debug_handle *info = (struct debug_handle *) handle;
  struct debug_type *t;
  struct debug_class_type *c;

  t = debug_make_type (info,
		       structp ? DEBUG_KIND_CLASS : DEBUG_KIND_UNION_CLASS,
		       size);
  if (t == NULL)
    return DEBUG_TYPE_NULL;

  c = (struct debug_class_type *) xmalloc (sizeof *c);
  memset (c, 0, sizeof *c);

  c->fields = fields;
  c->baseclasses = baseclasses;
  c->methods = methods;
  if (ownvptr)
    c->vptrbase = t;
  else
    c->vptrbase = vptrbase;

  t->u.kclass = c;

  return t;
}

/* Make an enumeration type.  The arguments are a null terminated
   array of strings, and an array of corresponding values.  */

debug_type
debug_make_enum_type (void *handle, const char **names,
		      bfd_signed_vma *values)
{
  struct debug_handle *info = (struct debug_handle *) handle;
  struct debug_type *t;
  struct debug_enum_type *e;

  t = debug_make_type (info, DEBUG_KIND_ENUM, 0);
  if (t == NULL)
    return DEBUG_TYPE_NULL;

  e = (struct debug_enum_type *) xmalloc (sizeof *e);
  memset (e, 0, sizeof *e);

  e->names = names;
  e->values = values;

  t->u.kenum = e;

  return t;
}

/* Make a pointer to a given type.  */

debug_type
debug_make_pointer_type (void *handle, debug_type type)
{
  struct debug_handle *info = (struct debug_handle *) handle;
  struct debug_type *t;

  if (type == NULL)
    return DEBUG_TYPE_NULL;

  if (type->pointer != DEBUG_TYPE_NULL)
    return type->pointer;

  t = debug_make_type (info, DEBUG_KIND_POINTER, 0);
  if (t == NULL)
    return DEBUG_TYPE_NULL;

  t->u.kpointer = type;

  type->pointer = t;

  return t;
}

/* Make a function returning a given type.  FIXME: We should be able
   to record the parameter types.  */

debug_type
debug_make_function_type (void *handle, debug_type type,
			  debug_type *arg_types, bfd_boolean varargs)
{
  struct debug_handle *info = (struct debug_handle *) handle;
  struct debug_type *t;
  struct debug_function_type *f;

  if (type == NULL)
    return DEBUG_TYPE_NULL;

  t = debug_make_type (info, DEBUG_KIND_FUNCTION, 0);
  if (t == NULL)
    return DEBUG_TYPE_NULL;

  f = (struct debug_function_type *) xmalloc (sizeof *f);
  memset (f, 0, sizeof *f);

  f->return_type = type;
  f->arg_types = arg_types;
  f->varargs = varargs;

  t->u.kfunction = f;

  return t;
}

/* Make a reference to a given type.  */

debug_type
debug_make_reference_type (void *handle, debug_type type)
{
  struct debug_handle *info = (struct debug_handle *) handle;
  struct debug_type *t;

  if (type == NULL)
    return DEBUG_TYPE_NULL;

  t = debug_make_type (info, DEBUG_KIND_REFERENCE, 0);
  if (t == NULL)
    return DEBUG_TYPE_NULL;

  t->u.kreference = type;

  return t;
}

/* Make a range of a given type from a lower to an upper bound.  */

debug_type
debug_make_range_type (void *handle, debug_type type, bfd_signed_vma lower,
		       bfd_signed_vma upper)
{
  struct debug_handle *info = (struct debug_handle *) handle;
  struct debug_type *t;
  struct debug_range_type *r;

  if (type == NULL)
    return DEBUG_TYPE_NULL;

  t = debug_make_type (info, DEBUG_KIND_RANGE, 0);
  if (t == NULL)
    return DEBUG_TYPE_NULL;

  r = (struct debug_range_type *) xmalloc (sizeof *r);
  memset (r, 0, sizeof *r);

  r->type = type;
  r->lower = lower;
  r->upper = upper;

  t->u.krange = r;

  return t;
}

/* Make an array type.  The second argument is the type of an element
   of the array.  The third argument is the type of a range of the
   array.  The fourth and fifth argument are the lower and upper
   bounds, respectively.  The sixth argument is true if this array is
   actually a string, as in C.  */

debug_type
debug_make_array_type (void *handle, debug_type element_type,
		       debug_type range_type, bfd_signed_vma lower,
		       bfd_signed_vma upper, bfd_boolean stringp)
{
  struct debug_handle *info = (struct debug_handle *) handle;
  struct debug_type *t;
  struct debug_array_type *a;

  if (element_type == NULL || range_type == NULL)
    return DEBUG_TYPE_NULL;

  t = debug_make_type (info, DEBUG_KIND_ARRAY, 0);
  if (t == NULL)
    return DEBUG_TYPE_NULL;

  a = (struct debug_array_type *) xmalloc (sizeof *a);
  memset (a, 0, sizeof *a);

  a->element_type = element_type;
  a->range_type = range_type;
  a->lower = lower;
  a->upper = upper;
  a->stringp = stringp;

  t->u.karray = a;

  return t;
}

/* Make a set of a given type.  For example, a Pascal set type.  The
   boolean argument is true if this set is actually a bitstring, as in
   CHILL.  */

debug_type
debug_make_set_type (void *handle, debug_type type, bfd_boolean bitstringp)
{
  struct debug_handle *info = (struct debug_handle *) handle;
  struct debug_type *t;
  struct debug_set_type *s;

  if (type == NULL)
    return DEBUG_TYPE_NULL;

  t = debug_make_type (info, DEBUG_KIND_SET, 0);
  if (t == NULL)
    return DEBUG_TYPE_NULL;

  s = (struct debug_set_type *) xmalloc (sizeof *s);
  memset (s, 0, sizeof *s);

  s->type = type;
  s->bitstringp = bitstringp;

  t->u.kset = s;

  return t;
}

/* Make a type for a pointer which is relative to an object.  The
   second argument is the type of the object to which the pointer is
   relative.  The third argument is the type that the pointer points
   to.  */

debug_type
debug_make_offset_type (void *handle, debug_type base_type,
			debug_type target_type)
{
  struct debug_handle *info = (struct debug_handle *) handle;
  struct debug_type *t;
  struct debug_offset_type *o;

  if (base_type == NULL || target_type == NULL)
    return DEBUG_TYPE_NULL;

  t = debug_make_type (info, DEBUG_KIND_OFFSET, 0);
  if (t == NULL)
    return DEBUG_TYPE_NULL;

  o = (struct debug_offset_type *) xmalloc (sizeof *o);
  memset (o, 0, sizeof *o);

  o->base_type = base_type;
  o->target_type = target_type;

  t->u.koffset = o;

  return t;
}

/* Make a type for a method function.  The second argument is the
   return type, the third argument is the domain, and the fourth
   argument is a NULL terminated array of argument types.  */

debug_type
debug_make_method_type (void *handle, debug_type return_type,
			debug_type domain_type, debug_type *arg_types,
			bfd_boolean varargs)
{
  struct debug_handle *info = (struct debug_handle *) handle;
  struct debug_type *t;
  struct debug_method_type *m;

  if (return_type == NULL)
    return DEBUG_TYPE_NULL;

  t = debug_make_type (info, DEBUG_KIND_METHOD, 0);
  if (t == NULL)
    return DEBUG_TYPE_NULL;

  m = (struct debug_method_type *) xmalloc (sizeof *m);
  memset (m, 0, sizeof *m);

  m->return_type = return_type;
  m->domain_type = domain_type;
  m->arg_types = arg_types;
  m->varargs = varargs;

  t->u.kmethod = m;

  return t;
}

/* Make a const qualified version of a given type.  */

debug_type
debug_make_const_type (void *handle, debug_type type)
{
  struct debug_handle *info = (struct debug_handle *) handle;
  struct debug_type *t;

  if (type == NULL)
    return DEBUG_TYPE_NULL;

  t = debug_make_type (info, DEBUG_KIND_CONST, 0);
  if (t == NULL)
    return DEBUG_TYPE_NULL;

  t->u.kconst = type;

  return t;
}

/* Make a volatile qualified version of a given type.  */

debug_type
debug_make_volatile_type (void *handle, debug_type type)
{
  struct debug_handle *info = (struct debug_handle *) handle;
  struct debug_type *t;

  if (type == NULL)
    return DEBUG_TYPE_NULL;

  t = debug_make_type (info, DEBUG_KIND_VOLATILE, 0);
  if (t == NULL)
    return DEBUG_TYPE_NULL;

  t->u.kvolatile = type;

  return t;
}

/* Make an undefined tagged type.  For example, a struct which has
   been mentioned, but not defined.  */

debug_type
debug_make_undefined_tagged_type (void *handle, const char *name,
				  enum debug_type_kind kind)
{
  struct debug_handle *info = (struct debug_handle *) handle;
  struct debug_type *t;

  if (name == NULL)
    return DEBUG_TYPE_NULL;

  switch (kind)
    {
    case DEBUG_KIND_STRUCT:
    case DEBUG_KIND_UNION:
    case DEBUG_KIND_CLASS:
    case DEBUG_KIND_UNION_CLASS:
    case DEBUG_KIND_ENUM:
      break;

    default:
      debug_error (_("debug_make_undefined_type: unsupported kind"));
      return DEBUG_TYPE_NULL;
    }

  t = debug_make_type (info, kind, 0);
  if (t == NULL)
    return DEBUG_TYPE_NULL;

  return debug_tag_type (handle, name, t);
}

/* Make a base class for an object.  The second argument is the base
   class type.  The third argument is the bit position of this base
   class in the object (always 0 unless doing multiple inheritance).
   The fourth argument is whether this is a virtual class.  The fifth
   argument is the visibility of the base class.  */

debug_baseclass
debug_make_baseclass (void *handle ATTRIBUTE_UNUSED, debug_type type,
		      bfd_vma bitpos, bfd_boolean virtual,
		      enum debug_visibility visibility)
{
  struct debug_baseclass *b;

  b = (struct debug_baseclass *) xmalloc (sizeof *b);
  memset (b, 0, sizeof *b);

  b->type = type;
  b->bitpos = bitpos;
  b->virtual = virtual;
  b->visibility = visibility;

  return b;
}

/* Make a field for a struct.  The second argument is the name.  The
   third argument is the type of the field.  The fourth argument is
   the bit position of the field.  The fifth argument is the size of
   the field (it may be zero).  The sixth argument is the visibility
   of the field.  */

debug_field
debug_make_field (void *handle ATTRIBUTE_UNUSED, const char *name,
		  debug_type type, bfd_vma bitpos, bfd_vma bitsize,
		  enum debug_visibility visibility)
{
  struct debug_field *f;

  f = (struct debug_field *) xmalloc (sizeof *f);
  memset (f, 0, sizeof *f);

  f->name = name;
  f->type = type;
  f->static_member = FALSE;
  f->u.f.bitpos = bitpos;
  f->u.f.bitsize = bitsize;
  f->visibility = visibility;

  return f;
}

/* Make a static member of an object.  The second argument is the
   name.  The third argument is the type of the member.  The fourth
   argument is the physical name of the member (i.e., the name as a
   global variable).  The fifth argument is the visibility of the
   member.  */

debug_field
debug_make_static_member (void *handle ATTRIBUTE_UNUSED, const char *name,
			  debug_type type, const char *physname,
			  enum debug_visibility visibility)
{
  struct debug_field *f;

  f = (struct debug_field *) xmalloc (sizeof *f);
  memset (f, 0, sizeof *f);

  f->name = name;
  f->type = type;
  f->static_member = TRUE;
  f->u.s.physname = physname;
  f->visibility = visibility;

  return f;
}

/* Make a method.  The second argument is the name, and the third
   argument is a NULL terminated array of method variants.  */

debug_method
debug_make_method (void *handle ATTRIBUTE_UNUSED, const char *name,
		   debug_method_variant *variants)
{
  struct debug_method *m;

  m = (struct debug_method *) xmalloc (sizeof *m);
  memset (m, 0, sizeof *m);

  m->name = name;
  m->variants = variants;

  return m;
}

/* Make a method argument.  The second argument is the real name of
   the function.  The third argument is the type of the function.  The
   fourth argument is the visibility.  The fifth argument is whether
   this is a const function.  The sixth argument is whether this is a
   volatile function.  The seventh argument is the offset in the
   virtual function table, if any.  The eighth argument is the virtual
   function context.  FIXME: Are the const and volatile arguments
   necessary?  Could we just use debug_make_const_type?  */

debug_method_variant
debug_make_method_variant (void *handle ATTRIBUTE_UNUSED,
			   const char *physname, debug_type type,
			   enum debug_visibility visibility,
			   bfd_boolean constp, bfd_boolean volatilep,
			   bfd_vma voffset, debug_type context)
{
  struct debug_method_variant *m;

  m = (struct debug_method_variant *) xmalloc (sizeof *m);
  memset (m, 0, sizeof *m);

  m->physname = physname;
  m->type = type;
  m->visibility = visibility;
  m->constp = constp;
  m->volatilep = volatilep;
  m->voffset = voffset;
  m->context = context;

  return m;
}

/* Make a static method argument.  The arguments are the same as for
   debug_make_method_variant, except that the last two are omitted
   since a static method can not also be virtual.  */

debug_method_variant
debug_make_static_method_variant (void *handle ATTRIBUTE_UNUSED,
				  const char *physname, debug_type type,
				  enum debug_visibility visibility,
				  bfd_boolean constp, bfd_boolean volatilep)
{
  struct debug_method_variant *m;

  m = (struct debug_method_variant *) xmalloc (sizeof *m);
  memset (m, 0, sizeof *m);

  m->physname = physname;
  m->type = type;
  m->visibility = visibility;
  m->constp = constp;
  m->volatilep = volatilep;
  m->voffset = VOFFSET_STATIC_METHOD;

  return m;
}

/* Name a type.  */

debug_type
debug_name_type (void *handle, const char *name, debug_type type)
{
  struct debug_handle *info = (struct debug_handle *) handle;
  struct debug_type *t;
  struct debug_named_type *n;
  struct debug_name *nm;

  if (name == NULL || type == NULL)
    return DEBUG_TYPE_NULL;

  if (info->current_unit == NULL
      || info->current_file == NULL)
    {
      debug_error (_("debug_name_type: no current file"));
      return DEBUG_TYPE_NULL;
    }

  t = debug_make_type (info, DEBUG_KIND_NAMED, 0);
  if (t == NULL)
    return DEBUG_TYPE_NULL;

  n = (struct debug_named_type *) xmalloc (sizeof *n);
  memset (n, 0, sizeof *n);

  n->type = type;

  t->u.knamed = n;

  /* We always add the name to the global namespace.  This is probably
     wrong in some cases, but it seems to be right for stabs.  FIXME.  */

  nm = debug_add_to_namespace (info, &info->current_file->globals, name,
			       DEBUG_OBJECT_TYPE, DEBUG_LINKAGE_NONE);
  if (nm == NULL)
    return DEBUG_TYPE_NULL;

  nm->u.type = t;

  n->name = nm;

  return t;
}

/* Tag a type.  */

debug_type
debug_tag_type (void *handle, const char *name, debug_type type)
{
  struct debug_handle *info = (struct debug_handle *) handle;
  struct debug_type *t;
  struct debug_named_type *n;
  struct debug_name *nm;

  if (name == NULL || type == NULL)
    return DEBUG_TYPE_NULL;

  if (info->current_file == NULL)
    {
      debug_error (_("debug_tag_type: no current file"));
      return DEBUG_TYPE_NULL;
    }

  if (type->kind == DEBUG_KIND_TAGGED)
    {
      if (strcmp (type->u.knamed->name->name, name) == 0)
	return type;
      debug_error (_("debug_tag_type: extra tag attempted"));
      return DEBUG_TYPE_NULL;
    }

  t = debug_make_type (info, DEBUG_KIND_TAGGED, 0);
  if (t == NULL)
    return DEBUG_TYPE_NULL;

  n = (struct debug_named_type *) xmalloc (sizeof *n);
  memset (n, 0, sizeof *n);

  n->type = type;

  t->u.knamed = n;

  /* We keep a global namespace of tags for each compilation unit.  I
     don't know if that is the right thing to do.  */

  nm = debug_add_to_namespace (info, &info->current_file->globals, name,
			       DEBUG_OBJECT_TAG, DEBUG_LINKAGE_NONE);
  if (nm == NULL)
    return DEBUG_TYPE_NULL;

  nm->u.tag = t;

  n->name = nm;

  return t;
}

/* Record the size of a given type.  */

bfd_boolean
debug_record_type_size (void *handle ATTRIBUTE_UNUSED, debug_type type,
			unsigned int size)
{
  if (type->size != 0 && type->size != size)
    fprintf (stderr, _("Warning: changing type size from %d to %d\n"),
	     type->size, size);

  type->size = size;

  return TRUE;
}

/* Find a named type.  */

debug_type
debug_find_named_type (void *handle, const char *name)
{
  struct debug_handle *info = (struct debug_handle *) handle;
  struct debug_block *b;
  struct debug_file *f;

  /* We only search the current compilation unit.  I don't know if
     this is right or not.  */

  if (info->current_unit == NULL)
    {
      debug_error (_("debug_find_named_type: no current compilation unit"));
      return DEBUG_TYPE_NULL;
    }

  for (b = info->current_block; b != NULL; b = b->parent)
    {
      if (b->locals != NULL)
	{
	  struct debug_name *n;

	  for (n = b->locals->list; n != NULL; n = n->next)
	    {
	      if (n->kind == DEBUG_OBJECT_TYPE
		  && n->name[0] == name[0]
		  && strcmp (n->name, name) == 0)
		return n->u.type;
	    }
	}
    }

  for (f = info->current_unit->files; f != NULL; f = f->next)
    {
      if (f->globals != NULL)
	{
	  struct debug_name *n;

	  for (n = f->globals->list; n != NULL; n = n->next)
	    {
	      if (n->kind == DEBUG_OBJECT_TYPE
		  && n->name[0] == name[0]
		  && strcmp (n->name, name) == 0)
		return n->u.type;
	    }
	}
    }

  return DEBUG_TYPE_NULL;
}

/* Find a tagged type.  */

debug_type
debug_find_tagged_type (void *handle, const char *name,
			enum debug_type_kind kind)
{
  struct debug_handle *info = (struct debug_handle *) handle;
  struct debug_unit *u;

  /* We search the globals of all the compilation units.  I don't know
     if this is correct or not.  It would be easy to change.  */

  for (u = info->units; u != NULL; u = u->next)
    {
      struct debug_file *f;

      for (f = u->files; f != NULL; f = f->next)
	{
	  struct debug_name *n;

	  if (f->globals != NULL)
	    {
	      for (n = f->globals->list; n != NULL; n = n->next)
		{
		  if (n->kind == DEBUG_OBJECT_TAG
		      && (kind == DEBUG_KIND_ILLEGAL
			  || n->u.tag->kind == kind)
		      && n->name[0] == name[0]
		      && strcmp (n->name, name) == 0)
		    return n->u.tag;
		}
	    }
	}
    }

  return DEBUG_TYPE_NULL;
}

/* Get a base type.  We build a linked list on the stack to avoid
   crashing if the type is defined circularly.  */

static struct debug_type *
debug_get_real_type (void *handle, debug_type type,
		     struct debug_type_real_list *list)
{
  struct debug_type_real_list *l;
  struct debug_type_real_list rl;

  switch (type->kind)
    {
    default:
      return type;

    case DEBUG_KIND_INDIRECT:
    case DEBUG_KIND_NAMED:
    case DEBUG_KIND_TAGGED:
      break;
    }

  for (l = list; l != NULL; l = l->next)
    {
      if (l->t == type || l == l->next)
	{
	  fprintf (stderr,
		   _("debug_get_real_type: circular debug information for %s\n"),
		   debug_get_type_name (handle, type));
	  return NULL;
	}
    }

  rl.next = list;
  rl.t = type;

  switch (type->kind)
    {
      /* The default case is just here to avoid warnings.  */
    default:
    case DEBUG_KIND_INDIRECT:
      if (*type->u.kindirect->slot != NULL)
	return debug_get_real_type (handle, *type->u.kindirect->slot, &rl);
      return type;
    case DEBUG_KIND_NAMED:
    case DEBUG_KIND_TAGGED:
      return debug_get_real_type (handle, type->u.knamed->type, &rl);
    }
  /*NOTREACHED*/
}

/* Get the kind of a type.  */

enum debug_type_kind
debug_get_type_kind (void *handle, debug_type type)
{
  if (type == NULL)
    return DEBUG_KIND_ILLEGAL;
  type = debug_get_real_type (handle, type, NULL);
  if (type == NULL)
    return DEBUG_KIND_ILLEGAL;
  return type->kind;
}

/* Get the name of a type.  */

const char *
debug_get_type_name (void *handle, debug_type type)
{
  if (type->kind == DEBUG_KIND_INDIRECT)
    {
      if (*type->u.kindirect->slot != NULL)
	return debug_get_type_name (handle, *type->u.kindirect->slot);
      return type->u.kindirect->tag;
    }
  if (type->kind == DEBUG_KIND_NAMED
      || type->kind == DEBUG_KIND_TAGGED)
    return type->u.knamed->name->name;
  return NULL;
}

/* Get the size of a type.  */

bfd_vma
debug_get_type_size (void *handle, debug_type type)
{
  if (type == NULL)
    return 0;

  /* We don't call debug_get_real_type, because somebody might have
     called debug_record_type_size on a named or indirect type.  */

  if (type->size != 0)
    return type->size;

  switch (type->kind)
    {
    default:
      return 0;
    case DEBUG_KIND_INDIRECT:
      if (*type->u.kindirect->slot != NULL)
	return debug_get_type_size (handle, *type->u.kindirect->slot);
      return 0;
    case DEBUG_KIND_NAMED:
    case DEBUG_KIND_TAGGED:
      return debug_get_type_size (handle, type->u.knamed->type);
    }
  /*NOTREACHED*/
}

/* Get the return type of a function or method type.  */

debug_type
debug_get_return_type (void *handle, debug_type type)
{
  if (type == NULL)
    return DEBUG_TYPE_NULL;

  type = debug_get_real_type (handle, type, NULL);
  if (type == NULL)
    return DEBUG_TYPE_NULL;

  switch (type->kind)
    {
    default:
      return DEBUG_TYPE_NULL;
    case DEBUG_KIND_FUNCTION:
      return type->u.kfunction->return_type;
    case DEBUG_KIND_METHOD:
      return type->u.kmethod->return_type;
    }
  /*NOTREACHED*/
}

/* Get the parameter types of a function or method type (except that
   we don't currently store the parameter types of a function).  */

const debug_type *
debug_get_parameter_types (void *handle, debug_type type,
			   bfd_boolean *pvarargs)
{
  if (type == NULL)
    return NULL;

  type = debug_get_real_type (handle, type, NULL);
  if (type == NULL)
    return NULL;

  switch (type->kind)
    {
    default:
      return NULL;
    case DEBUG_KIND_FUNCTION:
      *pvarargs = type->u.kfunction->varargs;
      return type->u.kfunction->arg_types;
    case DEBUG_KIND_METHOD:
      *pvarargs = type->u.kmethod->varargs;
      return type->u.kmethod->arg_types;
    }
  /*NOTREACHED*/
}

/* Get the target type of a type.  */

debug_type
debug_get_target_type (void *handle, debug_type type)
{
  if (type == NULL)
    return NULL;

  type = debug_get_real_type (handle, type, NULL);
  if (type == NULL)
    return NULL;

  switch (type->kind)
    {
    default:
      return NULL;
    case DEBUG_KIND_POINTER:
      return type->u.kpointer;
    case DEBUG_KIND_REFERENCE:
      return type->u.kreference;
    case DEBUG_KIND_CONST:
      return type->u.kconst;
    case DEBUG_KIND_VOLATILE:
      return type->u.kvolatile;
    }
  /*NOTREACHED*/
}

/* Get the NULL terminated array of fields for a struct, union, or
   class.  */

const debug_field *
debug_get_fields (void *handle, debug_type type)
{
  if (type == NULL)
    return NULL;

  type = debug_get_real_type (handle, type, NULL);
  if (type == NULL)
    return NULL;

  switch (type->kind)
    {
    default:
      return NULL;
    case DEBUG_KIND_STRUCT:
    case DEBUG_KIND_UNION:
    case DEBUG_KIND_CLASS:
    case DEBUG_KIND_UNION_CLASS:
      return type->u.kclass->fields;
    }
  /*NOTREACHED*/
}

/* Get the type of a field.  */

debug_type
debug_get_field_type (void *handle ATTRIBUTE_UNUSED, debug_field field)
{
  if (field == NULL)
    return NULL;
  return field->type;
}

/* Get the name of a field.  */

const char *
debug_get_field_name (void *handle ATTRIBUTE_UNUSED, debug_field field)
{
  if (field == NULL)
    return NULL;
  return field->name;
}

/* Get the bit position of a field.  */

bfd_vma
debug_get_field_bitpos (void *handle ATTRIBUTE_UNUSED, debug_field field)
{
  if (field == NULL || field->static_member)
    return (bfd_vma) -1;
  return field->u.f.bitpos;
}

/* Get the bit size of a field.  */

bfd_vma
debug_get_field_bitsize (void *handle ATTRIBUTE_UNUSED, debug_field field)
{
  if (field == NULL || field->static_member)
    return (bfd_vma) -1;
  return field->u.f.bitsize;
}

/* Get the visibility of a field.  */

enum debug_visibility
debug_get_field_visibility (void *handle ATTRIBUTE_UNUSED, debug_field field)
{
  if (field == NULL)
    return DEBUG_VISIBILITY_IGNORE;
  return field->visibility;
}

/* Get the physical name of a field.  */

const char *
debug_get_field_physname (void *handle ATTRIBUTE_UNUSED, debug_field field)
{
  if (field == NULL || ! field->static_member)
    return NULL;
  return field->u.s.physname;
}

/* Write out the debugging information.  This is given a handle to
   debugging information, and a set of function pointers to call.  */

bfd_boolean
debug_write (void *handle, const struct debug_write_fns *fns, void *fhandle)
{
  struct debug_handle *info = (struct debug_handle *) handle;
  struct debug_unit *u;

  /* We use a mark to tell whether we have already written out a
     particular name.  We use an integer, so that we don't have to
     clear the mark fields if we happen to write out the same
     information more than once.  */
  ++info->mark;

  /* The base_id field holds an ID value which will never be used, so
     that we can tell whether we have assigned an ID during this call
     to debug_write.  */
  info->base_id = info->class_id;

  /* We keep a linked list of classes for which was have assigned ID's
     during this call to debug_write.  */
  info->id_list = NULL;

  for (u = info->units; u != NULL; u = u->next)
    {
      struct debug_file *f;
      bfd_boolean first_file;

      info->current_write_lineno = u->linenos;
      info->current_write_lineno_index = 0;

      if (! (*fns->start_compilation_unit) (fhandle, u->files->filename))
	return FALSE;

      first_file = TRUE;
      for (f = u->files; f != NULL; f = f->next)
	{
	  struct debug_name *n;

	  if (first_file)
	    first_file = FALSE;
	  else if (! (*fns->start_source) (fhandle, f->filename))
	    return FALSE;

	  if (f->globals != NULL)
	    for (n = f->globals->list; n != NULL; n = n->next)
	      if (! debug_write_name (info, fns, fhandle, n))
		return FALSE;
	}

      /* Output any line number information which hasn't already been
         handled.  */
      if (! debug_write_linenos (info, fns, fhandle, (bfd_vma) -1))
	return FALSE;
    }

  return TRUE;
}

/* Write out an element in a namespace.  */

static bfd_boolean
debug_write_name (struct debug_handle *info,
		  const struct debug_write_fns *fns, void *fhandle,
		  struct debug_name *n)
{
  switch (n->kind)
    {
    case DEBUG_OBJECT_TYPE:
      if (! debug_write_type (info, fns, fhandle, n->u.type, n)
	  || ! (*fns->typdef) (fhandle, n->name))
	return FALSE;
      return TRUE;
    case DEBUG_OBJECT_TAG:
      if (! debug_write_type (info, fns, fhandle, n->u.tag, n))
	return FALSE;
      return (*fns->tag) (fhandle, n->name);
    case DEBUG_OBJECT_VARIABLE:
      if (! debug_write_type (info, fns, fhandle, n->u.variable->type,
			      (struct debug_name *) NULL))
	return FALSE;
      return (*fns->variable) (fhandle, n->name, n->u.variable->kind,
			       n->u.variable->val);
    case DEBUG_OBJECT_FUNCTION:
      return debug_write_function (info, fns, fhandle, n->name,
				   n->linkage, n->u.function);
    case DEBUG_OBJECT_INT_CONSTANT:
      return (*fns->int_constant) (fhandle, n->name, n->u.int_constant);
    case DEBUG_OBJECT_FLOAT_CONSTANT:
      return (*fns->float_constant) (fhandle, n->name, n->u.float_constant);
    case DEBUG_OBJECT_TYPED_CONSTANT:
      if (! debug_write_type (info, fns, fhandle, n->u.typed_constant->type,
			      (struct debug_name *) NULL))
	return FALSE;
      return (*fns->typed_constant) (fhandle, n->name,
				     n->u.typed_constant->val);
    default:
      abort ();
      return FALSE;
    }
  /*NOTREACHED*/
}

/* Write out a type.  If the type is DEBUG_KIND_NAMED or
   DEBUG_KIND_TAGGED, then the name argument is the name for which we
   are about to call typedef or tag.  If the type is anything else,
   then the name argument is a tag from a DEBUG_KIND_TAGGED type which
   points to this one.  */

static bfd_boolean
debug_write_type (struct debug_handle *info,
		  const struct debug_write_fns *fns, void *fhandle,
		  struct debug_type *type, struct debug_name *name)
{
  unsigned int i;
  int is;
  const char *tag = NULL;

  /* If we have a name for this type, just output it.  We only output
     typedef names after they have been defined.  We output type tags
     whenever we are not actually defining them.  */
  if ((type->kind == DEBUG_KIND_NAMED
       || type->kind == DEBUG_KIND_TAGGED)
      && (type->u.knamed->name->mark == info->mark
	  || (type->kind == DEBUG_KIND_TAGGED
	      && type->u.knamed->name != name)))
    {
      if (type->kind == DEBUG_KIND_NAMED)
	return (*fns->typedef_type) (fhandle, type->u.knamed->name->name);
      else
	{
	  struct debug_type *real;
	  unsigned int id;

	  real = debug_get_real_type ((void *) info, type, NULL);
	  if (real == NULL)
	    return (*fns->empty_type) (fhandle);
	  id = 0;
	  if ((real->kind == DEBUG_KIND_STRUCT
	       || real->kind == DEBUG_KIND_UNION
	       || real->kind == DEBUG_KIND_CLASS
	       || real->kind == DEBUG_KIND_UNION_CLASS)
	      && real->u.kclass != NULL)
	    {
	      if (real->u.kclass->id <= info->base_id)
		{
		  if (! debug_set_class_id (info,
					    type->u.knamed->name->name,
					    real))
		    return FALSE;
		}
	      id = real->u.kclass->id;
	    }

	  return (*fns->tag_type) (fhandle, type->u.knamed->name->name, id,
				   real->kind);
	}
    }

  /* Mark the name after we have already looked for a known name, so
     that we don't just define a type in terms of itself.  We need to
     mark the name here so that a struct containing a pointer to
     itself will work.  */
  if (name != NULL)
    name->mark = info->mark;

  if (name != NULL
      && type->kind != DEBUG_KIND_NAMED
      && type->kind != DEBUG_KIND_TAGGED)
    {
      assert (name->kind == DEBUG_OBJECT_TAG);
      tag = name->name;
    }

  switch (type->kind)
    {
    case DEBUG_KIND_ILLEGAL:
      debug_error (_("debug_write_type: illegal type encountered"));
      return FALSE;
    case DEBUG_KIND_INDIRECT:
      if (*type->u.kindirect->slot == DEBUG_TYPE_NULL)
	return (*fns->empty_type) (fhandle);
      return debug_write_type (info, fns, fhandle, *type->u.kindirect->slot,
			       name);
    case DEBUG_KIND_VOID:
      return (*fns->void_type) (fhandle);
    case DEBUG_KIND_INT:
      return (*fns->int_type) (fhandle, type->size, type->u.kint);
    case DEBUG_KIND_FLOAT:
      return (*fns->float_type) (fhandle, type->size);
    case DEBUG_KIND_COMPLEX:
      return (*fns->complex_type) (fhandle, type->size);
    case DEBUG_KIND_BOOL:
      return (*fns->bool_type) (fhandle, type->size);
    case DEBUG_KIND_STRUCT:
    case DEBUG_KIND_UNION:
      if (type->u.kclass != NULL)
	{
	  if (type->u.kclass->id <= info->base_id)
	    {
	      if (! debug_set_class_id (info, tag, type))
		return FALSE;
	    }

	  if (info->mark == type->u.kclass->mark)
	    {
	      /* We are currently outputting this struct, or we have
		 already output it.  I don't know if this can happen,
		 but it can happen for a class.  */
	      assert (type->u.kclass->id > info->base_id);
	      return (*fns->tag_type) (fhandle, tag, type->u.kclass->id,
				       type->kind);
	    }
	  type->u.kclass->mark = info->mark;
	}

      if (! (*fns->start_struct_type) (fhandle, tag,
				       (type->u.kclass != NULL
					? type->u.kclass->id
					: 0),
				       type->kind == DEBUG_KIND_STRUCT,
				       type->size))
	return FALSE;
      if (type->u.kclass != NULL
	  && type->u.kclass->fields != NULL)
	{
	  for (i = 0; type->u.kclass->fields[i] != NULL; i++)
	    {
	      struct debug_field *f;

	      f = type->u.kclass->fields[i];
	      if (! debug_write_type (info, fns, fhandle, f->type,
				      (struct debug_name *) NULL)
		  || ! (*fns->struct_field) (fhandle, f->name, f->u.f.bitpos,
					     f->u.f.bitsize, f->visibility))
		return FALSE;
	    }
	}
      return (*fns->end_struct_type) (fhandle);
    case DEBUG_KIND_CLASS:
    case DEBUG_KIND_UNION_CLASS:
      return debug_write_class_type (info, fns, fhandle, type, tag);
    case DEBUG_KIND_ENUM:
      if (type->u.kenum == NULL)
	return (*fns->enum_type) (fhandle, tag, (const char **) NULL,
				  (bfd_signed_vma *) NULL);
      return (*fns->enum_type) (fhandle, tag, type->u.kenum->names,
				type->u.kenum->values);
    case DEBUG_KIND_POINTER:
      if (! debug_write_type (info, fns, fhandle, type->u.kpointer,
			      (struct debug_name *) NULL))
	return FALSE;
      return (*fns->pointer_type) (fhandle);
    case DEBUG_KIND_FUNCTION:
      if (! debug_write_type (info, fns, fhandle,
			      type->u.kfunction->return_type,
			      (struct debug_name *) NULL))
	return FALSE;
      if (type->u.kfunction->arg_types == NULL)
	is = -1;
      else
	{
	  for (is = 0; type->u.kfunction->arg_types[is] != NULL; is++)
	    if (! debug_write_type (info, fns, fhandle,
				    type->u.kfunction->arg_types[is],
				    (struct debug_name *) NULL))
	      return FALSE;
	}
      return (*fns->function_type) (fhandle, is,
				    type->u.kfunction->varargs);
    case DEBUG_KIND_REFERENCE:
      if (! debug_write_type (info, fns, fhandle, type->u.kreference,
			      (struct debug_name *) NULL))
	return FALSE;
      return (*fns->reference_type) (fhandle);
    case DEBUG_KIND_RANGE:
      if (! debug_write_type (info, fns, fhandle, type->u.krange->type,
			      (struct debug_name *) NULL))
	return FALSE;
      return (*fns->range_type) (fhandle, type->u.krange->lower,
				 type->u.krange->upper);
    case DEBUG_KIND_ARRAY:
      if (! debug_write_type (info, fns, fhandle, type->u.karray->element_type,
			      (struct debug_name *) NULL)
	  || ! debug_write_type (info, fns, fhandle,
				 type->u.karray->range_type,
				 (struct debug_name *) NULL))
	return FALSE;
      return (*fns->array_type) (fhandle, type->u.karray->lower,
				 type->u.karray->upper,
				 type->u.karray->stringp);
    case DEBUG_KIND_SET:
      if (! debug_write_type (info, fns, fhandle, type->u.kset->type,
			      (struct debug_name *) NULL))
	return FALSE;
      return (*fns->set_type) (fhandle, type->u.kset->bitstringp);
    case DEBUG_KIND_OFFSET:
      if (! debug_write_type (info, fns, fhandle, type->u.koffset->base_type,
			      (struct debug_name *) NULL)
	  || ! debug_write_type (info, fns, fhandle,
				 type->u.koffset->target_type,
				 (struct debug_name *) NULL))
	return FALSE;
      return (*fns->offset_type) (fhandle);
    case DEBUG_KIND_METHOD:
      if (! debug_write_type (info, fns, fhandle,
			      type->u.kmethod->return_type,
			      (struct debug_name *) NULL))
	return FALSE;
      if (type->u.kmethod->arg_types == NULL)
	is = -1;
      else
	{
	  for (is = 0; type->u.kmethod->arg_types[is] != NULL; is++)
	    if (! debug_write_type (info, fns, fhandle,
				    type->u.kmethod->arg_types[is],
				    (struct debug_name *) NULL))
	      return FALSE;
	}
      if (type->u.kmethod->domain_type != NULL)
	{
	  if (! debug_write_type (info, fns, fhandle,
				  type->u.kmethod->domain_type,
				  (struct debug_name *) NULL))
	    return FALSE;
	}
      return (*fns->method_type) (fhandle,
				  type->u.kmethod->domain_type != NULL,
				  is,
				  type->u.kmethod->varargs);
    case DEBUG_KIND_CONST:
      if (! debug_write_type (info, fns, fhandle, type->u.kconst,
			      (struct debug_name *) NULL))
	return FALSE;
      return (*fns->const_type) (fhandle);
    case DEBUG_KIND_VOLATILE:
      if (! debug_write_type (info, fns, fhandle, type->u.kvolatile,
			      (struct debug_name *) NULL))
	return FALSE;
      return (*fns->volatile_type) (fhandle);
    case DEBUG_KIND_NAMED:
      return debug_write_type (info, fns, fhandle, type->u.knamed->type,
			       (struct debug_name *) NULL);
    case DEBUG_KIND_TAGGED:
      return debug_write_type (info, fns, fhandle, type->u.knamed->type,
			       type->u.knamed->name);
    default:
      abort ();
      return FALSE;
    }
}

/* Write out a class type.  */

static bfd_boolean
debug_write_class_type (struct debug_handle *info,
			const struct debug_write_fns *fns, void *fhandle,
			struct debug_type *type, const char *tag)
{
  unsigned int i;
  unsigned int id;
  struct debug_type *vptrbase;

  if (type->u.kclass == NULL)
    {
      id = 0;
      vptrbase = NULL;
    }
  else
    {
      if (type->u.kclass->id <= info->base_id)
	{
	  if (! debug_set_class_id (info, tag, type))
	    return FALSE;
	}

      if (info->mark == type->u.kclass->mark)
	{
	  /* We are currently outputting this class, or we have
	     already output it.  This can happen when there are
	     methods for an anonymous class.  */
	  assert (type->u.kclass->id > info->base_id);
	  return (*fns->tag_type) (fhandle, tag, type->u.kclass->id,
				   type->kind);
	}
      type->u.kclass->mark = info->mark;
      id = type->u.kclass->id;

      vptrbase = type->u.kclass->vptrbase;
      if (vptrbase != NULL && vptrbase != type)
	{
	  if (! debug_write_type (info, fns, fhandle, vptrbase,
				  (struct debug_name *) NULL))
	    return FALSE;
	}
    }

  if (! (*fns->start_class_type) (fhandle, tag, id,
				  type->kind == DEBUG_KIND_CLASS,
				  type->size,
				  vptrbase != NULL,
				  vptrbase == type))
    return FALSE;

  if (type->u.kclass != NULL)
    {
      if (type->u.kclass->fields != NULL)
	{
	  for (i = 0; type->u.kclass->fields[i] != NULL; i++)
	    {
	      struct debug_field *f;

	      f = type->u.kclass->fields[i];
	      if (! debug_write_type (info, fns, fhandle, f->type,
				      (struct debug_name *) NULL))
		return FALSE;
	      if (f->static_member)
		{
		  if (! (*fns->class_static_member) (fhandle, f->name,
						     f->u.s.physname,
						     f->visibility))
		    return FALSE;
		}
	      else
		{
		  if (! (*fns->struct_field) (fhandle, f->name, f->u.f.bitpos,
					      f->u.f.bitsize, f->visibility))
		    return FALSE;
		}
	    }
	}

      if (type->u.kclass->baseclasses != NULL)
	{
	  for (i = 0; type->u.kclass->baseclasses[i] != NULL; i++)
	    {
	      struct debug_baseclass *b;

	      b = type->u.kclass->baseclasses[i];
	      if (! debug_write_type (info, fns, fhandle, b->type,
				      (struct debug_name *) NULL))
		return FALSE;
	      if (! (*fns->class_baseclass) (fhandle, b->bitpos, b->virtual,
					     b->visibility))
		return FALSE;
	    }
	}

      if (type->u.kclass->methods != NULL)
	{
	  for (i = 0; type->u.kclass->methods[i] != NULL; i++)
	    {
	      struct debug_method *m;
	      unsigned int j;

	      m = type->u.kclass->methods[i];
	      if (! (*fns->class_start_method) (fhandle, m->name))
		return FALSE;
	      for (j = 0; m->variants[j] != NULL; j++)
		{
		  struct debug_method_variant *v;

		  v = m->variants[j];
		  if (v->context != NULL)
		    {
		      if (! debug_write_type (info, fns, fhandle, v->context,
					      (struct debug_name *) NULL))
			return FALSE;
		    }
		  if (! debug_write_type (info, fns, fhandle, v->type,
					  (struct debug_name *) NULL))
		    return FALSE;
		  if (v->voffset != VOFFSET_STATIC_METHOD)
		    {
		      if (! (*fns->class_method_variant) (fhandle, v->physname,
							  v->visibility,
							  v->constp,
							  v->volatilep,
							  v->voffset,
							  v->context != NULL))
			return FALSE;
		    }
		  else
		    {
		      if (! (*fns->class_static_method_variant) (fhandle,
								 v->physname,
								 v->visibility,
								 v->constp,
								 v->volatilep))
			return FALSE;
		    }
		}
	      if (! (*fns->class_end_method) (fhandle))
		return FALSE;
	    }
	}
    }

  return (*fns->end_class_type) (fhandle);
}

/* Write out information for a function.  */

static bfd_boolean
debug_write_function (struct debug_handle *info,
		      const struct debug_write_fns *fns, void *fhandle,
		      const char *name, enum debug_object_linkage linkage,
		      struct debug_function *function)
{
  struct debug_parameter *p;
  struct debug_block *b;

  if (! debug_write_linenos (info, fns, fhandle, function->blocks->start))
    return FALSE;

  if (! debug_write_type (info, fns, fhandle, function->return_type,
			  (struct debug_name *) NULL))
    return FALSE;

  if (! (*fns->start_function) (fhandle, name,
				linkage == DEBUG_LINKAGE_GLOBAL))
    return FALSE;

  for (p = function->parameters; p != NULL; p = p->next)
    {
      if (! debug_write_type (info, fns, fhandle, p->type,
			      (struct debug_name *) NULL)
	  || ! (*fns->function_parameter) (fhandle, p->name, p->kind, p->val))
	return FALSE;
    }

  for (b = function->blocks; b != NULL; b = b->next)
    {
      if (! debug_write_block (info, fns, fhandle, b))
	return FALSE;
    }

  return (*fns->end_function) (fhandle);
}

/* Write out information for a block.  */

static bfd_boolean
debug_write_block (struct debug_handle *info,
		   const struct debug_write_fns *fns, void *fhandle,
		   struct debug_block *block)
{
  struct debug_name *n;
  struct debug_block *b;

  if (! debug_write_linenos (info, fns, fhandle, block->start))
    return FALSE;

  /* I can't see any point to writing out a block with no local
     variables, so we don't bother, except for the top level block.  */
  if (block->locals != NULL || block->parent == NULL)
    {
      if (! (*fns->start_block) (fhandle, block->start))
	return FALSE;
    }

  if (block->locals != NULL)
    {
      for (n = block->locals->list; n != NULL; n = n->next)
	{
	  if (! debug_write_name (info, fns, fhandle, n))
	    return FALSE;
	}
    }

  for (b = block->children; b != NULL; b = b->next)
    {
      if (! debug_write_block (info, fns, fhandle, b))
	return FALSE;
    }

  if (! debug_write_linenos (info, fns, fhandle, block->end))
    return FALSE;

  if (block->locals != NULL || block->parent == NULL)
    {
      if (! (*fns->end_block) (fhandle, block->end))
	return FALSE;
    }

  return TRUE;
}

/* Write out line number information up to ADDRESS.  */

static bfd_boolean
debug_write_linenos (struct debug_handle *info,
		     const struct debug_write_fns *fns, void *fhandle,
		     bfd_vma address)
{
  while (info->current_write_lineno != NULL)
    {
      struct debug_lineno *l;

      l = info->current_write_lineno;

      while (info->current_write_lineno_index < DEBUG_LINENO_COUNT)
	{
	  if (l->linenos[info->current_write_lineno_index]
	      == (unsigned long) -1)
	    break;

	  if (l->addrs[info->current_write_lineno_index] >= address)
	    return TRUE;

	  if (! (*fns->lineno) (fhandle, l->file->filename,
				l->linenos[info->current_write_lineno_index],
				l->addrs[info->current_write_lineno_index]))
	    return FALSE;

	  ++info->current_write_lineno_index;
	}

      info->current_write_lineno = l->next;
      info->current_write_lineno_index = 0;
    }

  return TRUE;
}

/* Get the ID number for a class.  If during the same call to
   debug_write we find a struct with the same definition with the same
   name, we use the same ID.  This type of things happens because the
   same struct will be defined by multiple compilation units.  */

static bfd_boolean
debug_set_class_id (struct debug_handle *info, const char *tag,
		    struct debug_type *type)
{
  struct debug_class_type *c;
  struct debug_class_id *l;

  assert (type->kind == DEBUG_KIND_STRUCT
	  || type->kind == DEBUG_KIND_UNION
	  || type->kind == DEBUG_KIND_CLASS
	  || type->kind == DEBUG_KIND_UNION_CLASS);

  c = type->u.kclass;

  if (c->id > info->base_id)
    return TRUE;

  for (l = info->id_list; l != NULL; l = l->next)
    {
      if (l->type->kind != type->kind)
	continue;

      if (tag == NULL)
	{
	  if (l->tag != NULL)
	    continue;
	}
      else
	{
	  if (l->tag == NULL
	      || l->tag[0] != tag[0]
	      || strcmp (l->tag, tag) != 0)
	    continue;
	}

      if (debug_type_samep (info, l->type, type))
	{
	  c->id = l->type->u.kclass->id;
	  return TRUE;
	}
    }

  /* There are no identical types.  Use a new ID, and add it to the
     list.  */
  ++info->class_id;
  c->id = info->class_id;

  l = (struct debug_class_id *) xmalloc (sizeof *l);
  memset (l, 0, sizeof *l);

  l->type = type;
  l->tag = tag;

  l->next = info->id_list;
  info->id_list = l;

  return TRUE;
}

/* See if two types are the same.  At this point, we don't care about
   tags and the like.  */

static bfd_boolean
debug_type_samep (struct debug_handle *info, struct debug_type *t1,
		  struct debug_type *t2)
{
  struct debug_type_compare_list *l;
  struct debug_type_compare_list top;
  bfd_boolean ret;

  if (t1 == NULL)
    return t2 == NULL;
  if (t2 == NULL)
    return FALSE;

  while (t1->kind == DEBUG_KIND_INDIRECT)
    {
      t1 = *t1->u.kindirect->slot;
      if (t1 == NULL)
	return FALSE;
    }
  while (t2->kind == DEBUG_KIND_INDIRECT)
    {
      t2 = *t2->u.kindirect->slot;
      if (t2 == NULL)
	return FALSE;
    }

  if (t1 == t2)
    return TRUE;

  /* As a special case, permit a typedef to match a tag, since C++
     debugging output will sometimes add a typedef where C debugging
     output will not.  */
  if (t1->kind == DEBUG_KIND_NAMED
      && t2->kind == DEBUG_KIND_TAGGED)
    return debug_type_samep (info, t1->u.knamed->type, t2);
  else if (t1->kind == DEBUG_KIND_TAGGED
	   && t2->kind == DEBUG_KIND_NAMED)
    return debug_type_samep (info, t1, t2->u.knamed->type);

  if (t1->kind != t2->kind
      || t1->size != t2->size)
    return FALSE;

  /* Get rid of the trivial cases first.  */
  switch (t1->kind)
    {
    default:
      break;
    case DEBUG_KIND_VOID:
    case DEBUG_KIND_FLOAT:
    case DEBUG_KIND_COMPLEX:
    case DEBUG_KIND_BOOL:
      return TRUE;
    case DEBUG_KIND_INT:
      return t1->u.kint == t2->u.kint;
    }

  /* We have to avoid an infinite recursion.  We do this by keeping a
     list of types which we are comparing.  We just keep the list on
     the stack.  If we encounter a pair of types we are currently
     comparing, we just assume that they are equal.  */
  for (l = info->compare_list; l != NULL; l = l->next)
    {
      if (l->t1 == t1 && l->t2 == t2)
	return TRUE;
    }

  top.t1 = t1;
  top.t2 = t2;
  top.next = info->compare_list;
  info->compare_list = &top;

  switch (t1->kind)
    {
    default:
      abort ();
      ret = FALSE;
      break;

    case DEBUG_KIND_STRUCT:
    case DEBUG_KIND_UNION:
    case DEBUG_KIND_CLASS:
    case DEBUG_KIND_UNION_CLASS:
      if (t1->u.kclass == NULL)
	ret = t2->u.kclass == NULL;
      else if (t2->u.kclass == NULL)
	ret = FALSE;
      else if (t1->u.kclass->id > info->base_id
	       && t1->u.kclass->id == t2->u.kclass->id)
	ret = TRUE;
      else
	ret = debug_class_type_samep (info, t1, t2);
      break;

    case DEBUG_KIND_ENUM:
      if (t1->u.kenum == NULL)
	ret = t2->u.kenum == NULL;
      else if (t2->u.kenum == NULL)
	ret = FALSE;
      else
	{
	  const char **pn1, **pn2;
	  bfd_signed_vma *pv1, *pv2;

	  pn1 = t1->u.kenum->names;
	  pn2 = t2->u.kenum->names;
	  pv1 = t1->u.kenum->values;
	  pv2 = t2->u.kenum->values;
	  while (*pn1 != NULL && *pn2 != NULL)
	    {
	      if (**pn1 != **pn2
		  || *pv1 != *pv2
		  || strcmp (*pn1, *pn2) != 0)
		break;
	      ++pn1;
	      ++pn2;
	      ++pv1;
	      ++pv2;
	    }
	  ret = *pn1 == NULL && *pn2 == NULL;
	}
      break;

    case DEBUG_KIND_POINTER:
      ret = debug_type_samep (info, t1->u.kpointer, t2->u.kpointer);
      break;

    case DEBUG_KIND_FUNCTION:
      if (t1->u.kfunction->varargs != t2->u.kfunction->varargs
	  || ! debug_type_samep (info, t1->u.kfunction->return_type,
				 t2->u.kfunction->return_type)
	  || ((t1->u.kfunction->arg_types == NULL)
	      != (t2->u.kfunction->arg_types == NULL)))
	ret = FALSE;
      else if (t1->u.kfunction->arg_types == NULL)
	ret = TRUE;
      else
	{
	  struct debug_type **a1, **a2;

	  a1 = t1->u.kfunction->arg_types;
	  a2 = t2->u.kfunction->arg_types;
	  while (*a1 != NULL && *a2 != NULL)
	    {
	      if (! debug_type_samep (info, *a1, *a2))
		break;
	      ++a1;
	      ++a2;
	    }
	  ret = *a1 == NULL && *a2 == NULL;
	}
      break;

    case DEBUG_KIND_REFERENCE:
      ret = debug_type_samep (info, t1->u.kreference, t2->u.kreference);
      break;

    case DEBUG_KIND_RANGE:
      ret = (t1->u.krange->lower == t2->u.krange->lower
	     && t1->u.krange->upper == t2->u.krange->upper
	     && debug_type_samep (info, t1->u.krange->type,
				  t2->u.krange->type));

    case DEBUG_KIND_ARRAY:
      ret = (t1->u.karray->lower == t2->u.karray->lower
	     && t1->u.karray->upper == t2->u.karray->upper
	     && t1->u.karray->stringp == t2->u.karray->stringp
	     && debug_type_samep (info, t1->u.karray->element_type,
				  t2->u.karray->element_type));
      break;

    case DEBUG_KIND_SET:
      ret = (t1->u.kset->bitstringp == t2->u.kset->bitstringp
	     && debug_type_samep (info, t1->u.kset->type, t2->u.kset->type));
      break;

    case DEBUG_KIND_OFFSET:
      ret = (debug_type_samep (info, t1->u.koffset->base_type,
			       t2->u.koffset->base_type)
	     && debug_type_samep (info, t1->u.koffset->target_type,
				  t2->u.koffset->target_type));
      break;

    case DEBUG_KIND_METHOD:
      if (t1->u.kmethod->varargs != t2->u.kmethod->varargs
	  || ! debug_type_samep (info, t1->u.kmethod->return_type,
				 t2->u.kmethod->return_type)
	  || ! debug_type_samep (info, t1->u.kmethod->domain_type,
				 t2->u.kmethod->domain_type)
	  || ((t1->u.kmethod->arg_types == NULL)
	      != (t2->u.kmethod->arg_types == NULL)))
	ret = FALSE;
      else if (t1->u.kmethod->arg_types == NULL)
	ret = TRUE;
      else
	{
	  struct debug_type **a1, **a2;

	  a1 = t1->u.kmethod->arg_types;
	  a2 = t2->u.kmethod->arg_types;
	  while (*a1 != NULL && *a2 != NULL)
	    {
	      if (! debug_type_samep (info, *a1, *a2))
		break;
	      ++a1;
	      ++a2;
	    }
	  ret = *a1 == NULL && *a2 == NULL;
	}
      break;

    case DEBUG_KIND_CONST:
      ret = debug_type_samep (info, t1->u.kconst, t2->u.kconst);
      break;

    case DEBUG_KIND_VOLATILE:
      ret = debug_type_samep (info, t1->u.kvolatile, t2->u.kvolatile);
      break;

    case DEBUG_KIND_NAMED:
    case DEBUG_KIND_TAGGED:
      ret = (strcmp (t1->u.knamed->name->name, t2->u.knamed->name->name) == 0
	     && debug_type_samep (info, t1->u.knamed->type,
				  t2->u.knamed->type));
      break;
    }

  info->compare_list = top.next;

  return ret;
}

/* See if two classes are the same.  This is a subroutine of
   debug_type_samep.  */

static bfd_boolean
debug_class_type_samep (struct debug_handle *info, struct debug_type *t1,
			struct debug_type *t2)
{
  struct debug_class_type *c1, *c2;

  c1 = t1->u.kclass;
  c2 = t2->u.kclass;

  if ((c1->fields == NULL) != (c2->fields == NULL)
      || (c1->baseclasses == NULL) != (c2->baseclasses == NULL)
      || (c1->methods == NULL) != (c2->methods == NULL)
      || (c1->vptrbase == NULL) != (c2->vptrbase == NULL))
    return FALSE;

  if (c1->fields != NULL)
    {
      struct debug_field **pf1, **pf2;

      for (pf1 = c1->fields, pf2 = c2->fields;
	   *pf1 != NULL && *pf2 != NULL;
	   pf1++, pf2++)
	{
	  struct debug_field *f1, *f2;

	  f1 = *pf1;
	  f2 = *pf2;
	  if (f1->name[0] != f2->name[0]
	      || f1->visibility != f2->visibility
	      || f1->static_member != f2->static_member)
	    return FALSE;
	  if (f1->static_member)
	    {
	      if (strcmp (f1->u.s.physname, f2->u.s.physname) != 0)
		return FALSE;
	    }
	  else
	    {
	      if (f1->u.f.bitpos != f2->u.f.bitpos
		  || f1->u.f.bitsize != f2->u.f.bitsize)
		return FALSE;
	    }
	  /* We do the checks which require function calls last.  We
             don't require that the types of fields have the same
             names, since that sometimes fails in the presence of
             typedefs and we really don't care.  */
	  if (strcmp (f1->name, f2->name) != 0
	      || ! debug_type_samep (info,
				     debug_get_real_type ((void *) info,
							  f1->type, NULL),
				     debug_get_real_type ((void *) info,
							  f2->type, NULL)))
	    return FALSE;
	}
      if (*pf1 != NULL || *pf2 != NULL)
	return FALSE;
    }

  if (c1->vptrbase != NULL)
    {
      if (! debug_type_samep (info, c1->vptrbase, c2->vptrbase))
	return FALSE;
    }

  if (c1->baseclasses != NULL)
    {
      struct debug_baseclass **pb1, **pb2;

      for (pb1 = c1->baseclasses, pb2 = c2->baseclasses;
	   *pb1 != NULL && *pb2 != NULL;
	   ++pb1, ++pb2)
	{
	  struct debug_baseclass *b1, *b2;

	  b1 = *pb1;
	  b2 = *pb2;
	  if (b1->bitpos != b2->bitpos
	      || b1->virtual != b2->virtual
	      || b1->visibility != b2->visibility
	      || ! debug_type_samep (info, b1->type, b2->type))
	    return FALSE;
	}
      if (*pb1 != NULL || *pb2 != NULL)
	return FALSE;
    }

  if (c1->methods != NULL)
    {
      struct debug_method **pm1, **pm2;

      for (pm1 = c1->methods, pm2 = c2->methods;
	   *pm1 != NULL && *pm2 != NULL;
	   ++pm1, ++pm2)
	{
	  struct debug_method *m1, *m2;

	  m1 = *pm1;
	  m2 = *pm2;
	  if (m1->name[0] != m2->name[0]
	      || strcmp (m1->name, m2->name) != 0
	      || (m1->variants == NULL) != (m2->variants == NULL))
	    return FALSE;
	  if (m1->variants == NULL)
	    {
	      struct debug_method_variant **pv1, **pv2;

	      for (pv1 = m1->variants, pv2 = m2->variants;
		   *pv1 != NULL && *pv2 != NULL;
		   ++pv1, ++pv2)
		{
		  struct debug_method_variant *v1, *v2;

		  v1 = *pv1;
		  v2 = *pv2;
		  if (v1->physname[0] != v2->physname[0]
		      || v1->visibility != v2->visibility
		      || v1->constp != v2->constp
		      || v1->volatilep != v2->volatilep
		      || v1->voffset != v2->voffset
		      || (v1->context == NULL) != (v2->context == NULL)
		      || strcmp (v1->physname, v2->physname) != 0
		      || ! debug_type_samep (info, v1->type, v2->type))
		    return FALSE;
		  if (v1->context != NULL)
		    {
		      if (! debug_type_samep (info, v1->context,
					      v2->context))
			return FALSE;
		    }
		}
	      if (*pv1 != NULL || *pv2 != NULL)
		return FALSE;
	    }
	}
      if (*pm1 != NULL || *pm2 != NULL)
	return FALSE;
    }

  return TRUE;
}
