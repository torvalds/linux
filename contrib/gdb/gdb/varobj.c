/* Implementation of the GDB variable objects API.
   Copyright 1999, 2000, 2001 Free Software Foundation, Inc.

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
#include "value.h"
#include "expression.h"
#include "frame.h"
#include "language.h"
#include "wrapper.h"
#include "gdbcmd.h"
#include "gdb_string.h"
#include <math.h>

#include "varobj.h"

/* Non-zero if we want to see trace of varobj level stuff.  */

int varobjdebug = 0;

/* String representations of gdb's format codes */
char *varobj_format_string[] =
  { "natural", "binary", "decimal", "hexadecimal", "octal" };

/* String representations of gdb's known languages */
char *varobj_language_string[] = { "unknown", "C", "C++", "Java" };

/* Data structures */

/* Every root variable has one of these structures saved in its
   varobj. Members which must be free'd are noted. */
struct varobj_root
{

  /* Alloc'd expression for this parent. */
  struct expression *exp;

  /* Block for which this expression is valid */
  struct block *valid_block;

  /* The frame for this expression */
  struct frame_id frame;

  /* If 1, "update" always recomputes the frame & valid block
     using the currently selected frame. */
  int use_selected_frame;

  /* Language info for this variable and its children */
  struct language_specific *lang;

  /* The varobj for this root node. */
  struct varobj *rootvar;

  /* Next root variable */
  struct varobj_root *next;
};

/* Every variable in the system has a structure of this type defined
   for it. This structure holds all information necessary to manipulate
   a particular object variable. Members which must be freed are noted. */
struct varobj
{

  /* Alloc'd name of the variable for this object.. If this variable is a
     child, then this name will be the child's source name.
     (bar, not foo.bar) */
  /* NOTE: This is the "expression" */
  char *name;

  /* The alloc'd name for this variable's object. This is here for
     convenience when constructing this object's children. */
  char *obj_name;

  /* Index of this variable in its parent or -1 */
  int index;

  /* The type of this variable. This may NEVER be NULL. */
  struct type *type;

  /* The value of this expression or subexpression.  This may be NULL. */
  struct value *value;

  /* Did an error occur evaluating the expression or getting its value? */
  int error;

  /* The number of (immediate) children this variable has */
  int num_children;

  /* If this object is a child, this points to its immediate parent. */
  struct varobj *parent;

  /* A list of this object's children */
  struct varobj_child *children;

  /* Description of the root variable. Points to root variable for children. */
  struct varobj_root *root;

  /* The format of the output for this object */
  enum varobj_display_formats format;

  /* Was this variable updated via a varobj_set_value operation */
  int updated;
};

/* Every variable keeps a linked list of its children, described
   by the following structure. */
/* FIXME: Deprecated.  All should use vlist instead */

struct varobj_child
{

  /* Pointer to the child's data */
  struct varobj *child;

  /* Pointer to the next child */
  struct varobj_child *next;
};

/* A stack of varobjs */
/* FIXME: Deprecated.  All should use vlist instead */

struct vstack
{
  struct varobj *var;
  struct vstack *next;
};

struct cpstack
{
  char *name;
  struct cpstack *next;
};

/* A list of varobjs */

struct vlist
{
  struct varobj *var;
  struct vlist *next;
};

/* Private function prototypes */

/* Helper functions for the above subcommands. */

static int delete_variable (struct cpstack **, struct varobj *, int);

static void delete_variable_1 (struct cpstack **, int *,
			       struct varobj *, int, int);

static int install_variable (struct varobj *);

static void uninstall_variable (struct varobj *);

static struct varobj *child_exists (struct varobj *, char *);

static struct varobj *create_child (struct varobj *, int, char *);

static void save_child_in_parent (struct varobj *, struct varobj *);

static void remove_child_from_parent (struct varobj *, struct varobj *);

/* Utility routines */

static struct varobj *new_variable (void);

static struct varobj *new_root_variable (void);

static void free_variable (struct varobj *var);

static struct cleanup *make_cleanup_free_variable (struct varobj *var);

static struct type *get_type (struct varobj *var);

static struct type *get_type_deref (struct varobj *var);

static struct type *get_target_type (struct type *);

static enum varobj_display_formats variable_default_display (struct varobj *);

static int my_value_equal (struct value *, struct value *, int *);

static void vpush (struct vstack **pstack, struct varobj *var);

static struct varobj *vpop (struct vstack **pstack);

static void cppush (struct cpstack **pstack, char *name);

static char *cppop (struct cpstack **pstack);

/* Language-specific routines. */

static enum varobj_languages variable_language (struct varobj *var);

static int number_of_children (struct varobj *);

static char *name_of_variable (struct varobj *);

static char *name_of_child (struct varobj *, int);

static struct value *value_of_root (struct varobj **var_handle, int *);

static struct value *value_of_child (struct varobj *parent, int index);

static struct type *type_of_child (struct varobj *var);

static int variable_editable (struct varobj *var);

static char *my_value_of_variable (struct varobj *var);

static int type_changeable (struct varobj *var);

/* C implementation */

static int c_number_of_children (struct varobj *var);

static char *c_name_of_variable (struct varobj *parent);

static char *c_name_of_child (struct varobj *parent, int index);

static struct value *c_value_of_root (struct varobj **var_handle);

static struct value *c_value_of_child (struct varobj *parent, int index);

static struct type *c_type_of_child (struct varobj *parent, int index);

static int c_variable_editable (struct varobj *var);

static char *c_value_of_variable (struct varobj *var);

/* C++ implementation */

static int cplus_number_of_children (struct varobj *var);

static void cplus_class_num_children (struct type *type, int children[3]);

static char *cplus_name_of_variable (struct varobj *parent);

static char *cplus_name_of_child (struct varobj *parent, int index);

static struct value *cplus_value_of_root (struct varobj **var_handle);

static struct value *cplus_value_of_child (struct varobj *parent, int index);

static struct type *cplus_type_of_child (struct varobj *parent, int index);

static int cplus_variable_editable (struct varobj *var);

static char *cplus_value_of_variable (struct varobj *var);

/* Java implementation */

static int java_number_of_children (struct varobj *var);

static char *java_name_of_variable (struct varobj *parent);

static char *java_name_of_child (struct varobj *parent, int index);

static struct value *java_value_of_root (struct varobj **var_handle);

static struct value *java_value_of_child (struct varobj *parent, int index);

static struct type *java_type_of_child (struct varobj *parent, int index);

static int java_variable_editable (struct varobj *var);

static char *java_value_of_variable (struct varobj *var);

/* The language specific vector */

struct language_specific
{

  /* The language of this variable */
  enum varobj_languages language;

  /* The number of children of PARENT. */
  int (*number_of_children) (struct varobj * parent);

  /* The name (expression) of a root varobj. */
  char *(*name_of_variable) (struct varobj * parent);

  /* The name of the INDEX'th child of PARENT. */
  char *(*name_of_child) (struct varobj * parent, int index);

  /* The ``struct value *'' of the root variable ROOT. */
  struct value *(*value_of_root) (struct varobj ** root_handle);

  /* The ``struct value *'' of the INDEX'th child of PARENT. */
  struct value *(*value_of_child) (struct varobj * parent, int index);

  /* The type of the INDEX'th child of PARENT. */
  struct type *(*type_of_child) (struct varobj * parent, int index);

  /* Is VAR editable? */
  int (*variable_editable) (struct varobj * var);

  /* The current value of VAR. */
  char *(*value_of_variable) (struct varobj * var);
};

/* Array of known source language routines. */
static struct language_specific
  languages[vlang_end][sizeof (struct language_specific)] = {
  /* Unknown (try treating as C */
  {
   vlang_unknown,
   c_number_of_children,
   c_name_of_variable,
   c_name_of_child,
   c_value_of_root,
   c_value_of_child,
   c_type_of_child,
   c_variable_editable,
   c_value_of_variable}
  ,
  /* C */
  {
   vlang_c,
   c_number_of_children,
   c_name_of_variable,
   c_name_of_child,
   c_value_of_root,
   c_value_of_child,
   c_type_of_child,
   c_variable_editable,
   c_value_of_variable}
  ,
  /* C++ */
  {
   vlang_cplus,
   cplus_number_of_children,
   cplus_name_of_variable,
   cplus_name_of_child,
   cplus_value_of_root,
   cplus_value_of_child,
   cplus_type_of_child,
   cplus_variable_editable,
   cplus_value_of_variable}
  ,
  /* Java */
  {
   vlang_java,
   java_number_of_children,
   java_name_of_variable,
   java_name_of_child,
   java_value_of_root,
   java_value_of_child,
   java_type_of_child,
   java_variable_editable,
   java_value_of_variable}
};

/* A little convenience enum for dealing with C++/Java */
enum vsections
{
  v_public = 0, v_private, v_protected
};

/* Private data */

/* Mappings of varobj_display_formats enums to gdb's format codes */
static int format_code[] = { 0, 't', 'd', 'x', 'o' };

/* Header of the list of root variable objects */
static struct varobj_root *rootlist;
static int rootcount = 0;	/* number of root varobjs in the list */

/* Prime number indicating the number of buckets in the hash table */
/* A prime large enough to avoid too many colisions */
#define VAROBJ_TABLE_SIZE 227

/* Pointer to the varobj hash table (built at run time) */
static struct vlist **varobj_table;

/* Is the variable X one of our "fake" children? */
#define CPLUS_FAKE_CHILD(x) \
((x) != NULL && (x)->type == NULL && (x)->value == NULL)


/* API Implementation */

/* Creates a varobj (not its children) */

/* Return the full FRAME which corresponds to the given CORE_ADDR
   or NULL if no FRAME on the chain corresponds to CORE_ADDR.  */

static struct frame_info *
find_frame_addr_in_frame_chain (CORE_ADDR frame_addr)
{
  struct frame_info *frame = NULL;

  if (frame_addr == (CORE_ADDR) 0)
    return NULL;

  while (1)
    {
      frame = get_prev_frame (frame);
      if (frame == NULL)
	return NULL;
      if (get_frame_base_address (frame) == frame_addr)
	return frame;
    }
}

struct varobj *
varobj_create (char *objname,
	       char *expression, CORE_ADDR frame, enum varobj_type type)
{
  struct varobj *var;
  struct frame_info *fi;
  struct frame_info *old_fi = NULL;
  struct block *block;
  struct cleanup *old_chain;

  /* Fill out a varobj structure for the (root) variable being constructed. */
  var = new_root_variable ();
  old_chain = make_cleanup_free_variable (var);

  if (expression != NULL)
    {
      char *p;
      enum varobj_languages lang;

      /* Parse and evaluate the expression, filling in as much
         of the variable's data as possible */

      /* Allow creator to specify context of variable */
      if ((type == USE_CURRENT_FRAME) || (type == USE_SELECTED_FRAME))
	fi = deprecated_selected_frame;
      else
	/* FIXME: cagney/2002-11-23: This code should be doing a
	   lookup using the frame ID and not just the frame's
	   ``address''.  This, of course, means an interface change.
	   However, with out that interface change ISAs, such as the
	   ia64 with its two stacks, won't work.  Similar goes for the
	   case where there is a frameless function.  */
	fi = find_frame_addr_in_frame_chain (frame);

      /* frame = -2 means always use selected frame */
      if (type == USE_SELECTED_FRAME)
	var->root->use_selected_frame = 1;

      block = NULL;
      if (fi != NULL)
	block = get_frame_block (fi, 0);

      p = expression;
      innermost_block = NULL;
      /* Wrap the call to parse expression, so we can 
         return a sensible error. */
      if (!gdb_parse_exp_1 (&p, block, 0, &var->root->exp))
	{
	  return NULL;
	}

      /* Don't allow variables to be created for types. */
      if (var->root->exp->elts[0].opcode == OP_TYPE)
	{
	  do_cleanups (old_chain);
	  fprintf_unfiltered (gdb_stderr,
			      "Attempt to use a type name as an expression.");
	  return NULL;
	}

      var->format = variable_default_display (var);
      var->root->valid_block = innermost_block;
      var->name = savestring (expression, strlen (expression));

      /* When the frame is different from the current frame, 
         we must select the appropriate frame before parsing
         the expression, otherwise the value will not be current.
         Since select_frame is so benign, just call it for all cases. */
      if (fi != NULL)
	{
	  var->root->frame = get_frame_id (fi);
	  old_fi = deprecated_selected_frame;
	  select_frame (fi);
	}

      /* We definitively need to catch errors here.
         If evaluate_expression succeeds we got the value we wanted.
         But if it fails, we still go on with a call to evaluate_type()  */
      if (gdb_evaluate_expression (var->root->exp, &var->value))
	{
	  /* no error */
	  release_value (var->value);
	  if (VALUE_LAZY (var->value))
	    gdb_value_fetch_lazy (var->value);
	}
      else
	var->value = evaluate_type (var->root->exp);

      var->type = VALUE_TYPE (var->value);

      /* Set language info */
      lang = variable_language (var);
      var->root->lang = languages[lang];

      /* Set ourselves as our root */
      var->root->rootvar = var;

      /* Reset the selected frame */
      if (fi != NULL)
	select_frame (old_fi);
    }

  /* If the variable object name is null, that means this
     is a temporary variable, so don't install it. */

  if ((var != NULL) && (objname != NULL))
    {
      var->obj_name = savestring (objname, strlen (objname));

      /* If a varobj name is duplicated, the install will fail so
         we must clenup */
      if (!install_variable (var))
	{
	  do_cleanups (old_chain);
	  return NULL;
	}
    }

  discard_cleanups (old_chain);
  return var;
}

/* Generates an unique name that can be used for a varobj */

char *
varobj_gen_name (void)
{
  static int id = 0;
  char *obj_name;

  /* generate a name for this object */
  id++;
  xasprintf (&obj_name, "var%d", id);

  return obj_name;
}

/* Given an "objname", returns the pointer to the corresponding varobj
   or NULL if not found */

struct varobj *
varobj_get_handle (char *objname)
{
  struct vlist *cv;
  const char *chp;
  unsigned int index = 0;
  unsigned int i = 1;

  for (chp = objname; *chp; chp++)
    {
      index = (index + (i++ * (unsigned int) *chp)) % VAROBJ_TABLE_SIZE;
    }

  cv = *(varobj_table + index);
  while ((cv != NULL) && (strcmp (cv->var->obj_name, objname) != 0))
    cv = cv->next;

  if (cv == NULL)
    error ("Variable object not found");

  return cv->var;
}

/* Given the handle, return the name of the object */

char *
varobj_get_objname (struct varobj *var)
{
  return var->obj_name;
}

/* Given the handle, return the expression represented by the object */

char *
varobj_get_expression (struct varobj *var)
{
  return name_of_variable (var);
}

/* Deletes a varobj and all its children if only_children == 0,
   otherwise deletes only the children; returns a malloc'ed list of all the 
   (malloc'ed) names of the variables that have been deleted (NULL terminated) */

int
varobj_delete (struct varobj *var, char ***dellist, int only_children)
{
  int delcount;
  int mycount;
  struct cpstack *result = NULL;
  char **cp;

  /* Initialize a stack for temporary results */
  cppush (&result, NULL);

  if (only_children)
    /* Delete only the variable children */
    delcount = delete_variable (&result, var, 1 /* only the children */ );
  else
    /* Delete the variable and all its children */
    delcount = delete_variable (&result, var, 0 /* parent+children */ );

  /* We may have been asked to return a list of what has been deleted */
  if (dellist != NULL)
    {
      *dellist = xmalloc ((delcount + 1) * sizeof (char *));

      cp = *dellist;
      mycount = delcount;
      *cp = cppop (&result);
      while ((*cp != NULL) && (mycount > 0))
	{
	  mycount--;
	  cp++;
	  *cp = cppop (&result);
	}

      if (mycount || (*cp != NULL))
	warning ("varobj_delete: assertion failed - mycount(=%d) <> 0",
		 mycount);
    }

  return delcount;
}

/* Set/Get variable object display format */

enum varobj_display_formats
varobj_set_display_format (struct varobj *var,
			   enum varobj_display_formats format)
{
  switch (format)
    {
    case FORMAT_NATURAL:
    case FORMAT_BINARY:
    case FORMAT_DECIMAL:
    case FORMAT_HEXADECIMAL:
    case FORMAT_OCTAL:
      var->format = format;
      break;

    default:
      var->format = variable_default_display (var);
    }

  return var->format;
}

enum varobj_display_formats
varobj_get_display_format (struct varobj *var)
{
  return var->format;
}

int
varobj_get_num_children (struct varobj *var)
{
  if (var->num_children == -1)
    var->num_children = number_of_children (var);

  return var->num_children;
}

/* Creates a list of the immediate children of a variable object;
   the return code is the number of such children or -1 on error */

int
varobj_list_children (struct varobj *var, struct varobj ***childlist)
{
  struct varobj *child;
  char *name;
  int i;

  /* sanity check: have we been passed a pointer? */
  if (childlist == NULL)
    return -1;

  *childlist = NULL;

  if (var->num_children == -1)
    var->num_children = number_of_children (var);

  /* List of children */
  *childlist = xmalloc ((var->num_children + 1) * sizeof (struct varobj *));

  for (i = 0; i < var->num_children; i++)
    {
      /* Mark as the end in case we bail out */
      *((*childlist) + i) = NULL;

      /* check if child exists, if not create */
      name = name_of_child (var, i);
      child = child_exists (var, name);
      if (child == NULL)
	child = create_child (var, i, name);

      *((*childlist) + i) = child;
    }

  /* End of list is marked by a NULL pointer */
  *((*childlist) + i) = NULL;

  return var->num_children;
}

/* Obtain the type of an object Variable as a string similar to the one gdb
   prints on the console */

char *
varobj_get_type (struct varobj *var)
{
  struct value *val;
  struct cleanup *old_chain;
  struct ui_file *stb;
  char *thetype;
  long length;

  /* For the "fake" variables, do not return a type. (It's type is
     NULL, too.) */
  if (CPLUS_FAKE_CHILD (var))
    return NULL;

  stb = mem_fileopen ();
  old_chain = make_cleanup_ui_file_delete (stb);

  /* To print the type, we simply create a zero ``struct value *'' and
     cast it to our type. We then typeprint this variable. */
  val = value_zero (var->type, not_lval);
  type_print (VALUE_TYPE (val), "", stb, -1);

  thetype = ui_file_xstrdup (stb, &length);
  do_cleanups (old_chain);
  return thetype;
}

enum varobj_languages
varobj_get_language (struct varobj *var)
{
  return variable_language (var);
}

int
varobj_get_attributes (struct varobj *var)
{
  int attributes = 0;

  if (variable_editable (var))
    /* FIXME: define masks for attributes */
    attributes |= 0x00000001;	/* Editable */

  return attributes;
}

char *
varobj_get_value (struct varobj *var)
{
  return my_value_of_variable (var);
}

/* Set the value of an object variable (if it is editable) to the
   value of the given expression */
/* Note: Invokes functions that can call error() */

int
varobj_set_value (struct varobj *var, char *expression)
{
  struct value *val;
  int error;
  int offset = 0;

  /* The argument "expression" contains the variable's new value.
     We need to first construct a legal expression for this -- ugh! */
  /* Does this cover all the bases? */
  struct expression *exp;
  struct value *value;
  int saved_input_radix = input_radix;

  if (var->value != NULL && variable_editable (var) && !var->error)
    {
      char *s = expression;
      int i;

      input_radix = 10;		/* ALWAYS reset to decimal temporarily */
      if (!gdb_parse_exp_1 (&s, 0, 0, &exp))
	/* We cannot proceed without a well-formed expression. */
	return 0;
      if (!gdb_evaluate_expression (exp, &value))
	{
	  /* We cannot proceed without a valid expression. */
	  xfree (exp);
	  return 0;
	}

      if (!my_value_equal (var->value, value, &error))
        var->updated = 1;
      if (!gdb_value_assign (var->value, value, &val))
	return 0;
      value_free (var->value);
      release_value (val);
      var->value = val;
      input_radix = saved_input_radix;
      return 1;
    }

  return 0;
}

/* Returns a malloc'ed list with all root variable objects */
int
varobj_list (struct varobj ***varlist)
{
  struct varobj **cv;
  struct varobj_root *croot;
  int mycount = rootcount;

  /* Alloc (rootcount + 1) entries for the result */
  *varlist = xmalloc ((rootcount + 1) * sizeof (struct varobj *));

  cv = *varlist;
  croot = rootlist;
  while ((croot != NULL) && (mycount > 0))
    {
      *cv = croot->rootvar;
      mycount--;
      cv++;
      croot = croot->next;
    }
  /* Mark the end of the list */
  *cv = NULL;

  if (mycount || (croot != NULL))
    warning
      ("varobj_list: assertion failed - wrong tally of root vars (%d:%d)",
       rootcount, mycount);

  return rootcount;
}

/* Update the values for a variable and its children.  This is a
   two-pronged attack.  First, re-parse the value for the root's
   expression to see if it's changed.  Then go all the way
   through its children, reconstructing them and noting if they've
   changed.
   Return value:
    -1 if there was an error updating the varobj
    -2 if the type changed
    Otherwise it is the number of children + parent changed

   Only root variables can be updated... 

   NOTE: This function may delete the caller's varobj. If it
   returns -2, then it has done this and VARP will be modified
   to point to the new varobj. */

int
varobj_update (struct varobj **varp, struct varobj ***changelist)
{
  int changed = 0;
  int type_changed;
  int i;
  int vleft;
  int error2;
  struct varobj *v;
  struct varobj **cv;
  struct varobj **templist = NULL;
  struct value *new;
  struct vstack *stack = NULL;
  struct vstack *result = NULL;
  struct frame_id old_fid;
  struct frame_info *fi;

  /* sanity check: have we been passed a pointer? */
  if (changelist == NULL)
    return -1;

  /*  Only root variables can be updated... */
  if ((*varp)->root->rootvar != *varp)
    /* Not a root var */
    return -1;

  /* Save the selected stack frame, since we will need to change it
     in order to evaluate expressions. */
  old_fid = get_frame_id (deprecated_selected_frame);

  /* Update the root variable. value_of_root can return NULL
     if the variable is no longer around, i.e. we stepped out of
     the frame in which a local existed. We are letting the 
     value_of_root variable dispose of the varobj if the type
     has changed. */
  type_changed = 1;
  new = value_of_root (varp, &type_changed);
  if (new == NULL)
    {
      (*varp)->error = 1;
      return -1;
    }

  /* Initialize a stack for temporary results */
  vpush (&result, NULL);

  /* If this is a "use_selected_frame" varobj, and its type has changed,
     them note that it's changed. */
  if (type_changed)
    {
      vpush (&result, *varp);
      changed++;
    }
  /* If values are not equal, note that it's changed.
     There a couple of exceptions here, though.
     We don't want some types to be reported as "changed". */
  else if (type_changeable (*varp) &&
	   ((*varp)->updated || !my_value_equal ((*varp)->value, new, &error2)))
    {
      vpush (&result, *varp);
      (*varp)->updated = 0;
      changed++;
      /* error2 replaces var->error since this new value
         WILL replace the old one. */
      (*varp)->error = error2;
    }

  /* We must always keep around the new value for this root
     variable expression, or we lose the updated children! */
  value_free ((*varp)->value);
  (*varp)->value = new;

  /* Initialize a stack */
  vpush (&stack, NULL);

  /* Push the root's children */
  if ((*varp)->children != NULL)
    {
      struct varobj_child *c;
      for (c = (*varp)->children; c != NULL; c = c->next)
	vpush (&stack, c->child);
    }

  /* Walk through the children, reconstructing them all. */
  v = vpop (&stack);
  while (v != NULL)
    {
      /* Push any children */
      if (v->children != NULL)
	{
	  struct varobj_child *c;
	  for (c = v->children; c != NULL; c = c->next)
	    vpush (&stack, c->child);
	}

      /* Update this variable */
      new = value_of_child (v->parent, v->index);
      if (type_changeable (v) && 
          (v->updated || !my_value_equal (v->value, new, &error2)))
	{
	  /* Note that it's changed */
	  vpush (&result, v);
	  v->updated = 0;
	  changed++;
	}
      /* error2 replaces v->error since this new value
         WILL replace the old one. */
      v->error = error2;

      /* We must always keep new values, since children depend on it. */
      if (v->value != NULL)
	value_free (v->value);
      v->value = new;

      /* Get next child */
      v = vpop (&stack);
    }

  /* Alloc (changed + 1) list entries */
  /* FIXME: add a cleanup for the allocated list(s)
     because one day the select_frame called below can longjump */
  *changelist = xmalloc ((changed + 1) * sizeof (struct varobj *));
  if (changed > 1)
    {
      templist = xmalloc ((changed + 1) * sizeof (struct varobj *));
      cv = templist;
    }
  else
    cv = *changelist;

  /* Copy from result stack to list */
  vleft = changed;
  *cv = vpop (&result);
  while ((*cv != NULL) && (vleft > 0))
    {
      vleft--;
      cv++;
      *cv = vpop (&result);
    }
  if (vleft)
    warning ("varobj_update: assertion failed - vleft <> 0");

  if (changed > 1)
    {
      /* Now we revert the order. */
      for (i = 0; i < changed; i++)
	*(*changelist + i) = *(templist + changed - 1 - i);
      *(*changelist + changed) = NULL;
    }

  /* Restore selected frame */
  fi = frame_find_by_id (old_fid);
  if (fi)
    select_frame (fi);

  if (type_changed)
    return -2;
  else
    return changed;
}


/* Helper functions */

/*
 * Variable object construction/destruction
 */

static int
delete_variable (struct cpstack **resultp, struct varobj *var,
		 int only_children_p)
{
  int delcount = 0;

  delete_variable_1 (resultp, &delcount, var,
		     only_children_p, 1 /* remove_from_parent_p */ );

  return delcount;
}

/* Delete the variable object VAR and its children */
/* IMPORTANT NOTE: If we delete a variable which is a child
   and the parent is not removed we dump core.  It must be always
   initially called with remove_from_parent_p set */
static void
delete_variable_1 (struct cpstack **resultp, int *delcountp,
		   struct varobj *var, int only_children_p,
		   int remove_from_parent_p)
{
  struct varobj_child *vc;
  struct varobj_child *next;

  /* Delete any children of this variable, too. */
  for (vc = var->children; vc != NULL; vc = next)
    {
      if (!remove_from_parent_p)
	vc->child->parent = NULL;
      delete_variable_1 (resultp, delcountp, vc->child, 0, only_children_p);
      next = vc->next;
      xfree (vc);
    }

  /* if we were called to delete only the children we are done here */
  if (only_children_p)
    return;

  /* Otherwise, add it to the list of deleted ones and proceed to do so */
  /* If the name is null, this is a temporary variable, that has not
     yet been installed, don't report it, it belongs to the caller... */
  if (var->obj_name != NULL)
    {
      cppush (resultp, xstrdup (var->obj_name));
      *delcountp = *delcountp + 1;
    }

  /* If this variable has a parent, remove it from its parent's list */
  /* OPTIMIZATION: if the parent of this variable is also being deleted, 
     (as indicated by remove_from_parent_p) we don't bother doing an
     expensive list search to find the element to remove when we are
     discarding the list afterwards */
  if ((remove_from_parent_p) && (var->parent != NULL))
    {
      remove_child_from_parent (var->parent, var);
    }

  if (var->obj_name != NULL)
    uninstall_variable (var);

  /* Free memory associated with this variable */
  free_variable (var);
}

/* Install the given variable VAR with the object name VAR->OBJ_NAME. */
static int
install_variable (struct varobj *var)
{
  struct vlist *cv;
  struct vlist *newvl;
  const char *chp;
  unsigned int index = 0;
  unsigned int i = 1;

  for (chp = var->obj_name; *chp; chp++)
    {
      index = (index + (i++ * (unsigned int) *chp)) % VAROBJ_TABLE_SIZE;
    }

  cv = *(varobj_table + index);
  while ((cv != NULL) && (strcmp (cv->var->obj_name, var->obj_name) != 0))
    cv = cv->next;

  if (cv != NULL)
    error ("Duplicate variable object name");

  /* Add varobj to hash table */
  newvl = xmalloc (sizeof (struct vlist));
  newvl->next = *(varobj_table + index);
  newvl->var = var;
  *(varobj_table + index) = newvl;

  /* If root, add varobj to root list */
  if (var->root->rootvar == var)
    {
      /* Add to list of root variables */
      if (rootlist == NULL)
	var->root->next = NULL;
      else
	var->root->next = rootlist;
      rootlist = var->root;
      rootcount++;
    }

  return 1;			/* OK */
}

/* Unistall the object VAR. */
static void
uninstall_variable (struct varobj *var)
{
  struct vlist *cv;
  struct vlist *prev;
  struct varobj_root *cr;
  struct varobj_root *prer;
  const char *chp;
  unsigned int index = 0;
  unsigned int i = 1;

  /* Remove varobj from hash table */
  for (chp = var->obj_name; *chp; chp++)
    {
      index = (index + (i++ * (unsigned int) *chp)) % VAROBJ_TABLE_SIZE;
    }

  cv = *(varobj_table + index);
  prev = NULL;
  while ((cv != NULL) && (strcmp (cv->var->obj_name, var->obj_name) != 0))
    {
      prev = cv;
      cv = cv->next;
    }

  if (varobjdebug)
    fprintf_unfiltered (gdb_stdlog, "Deleting %s\n", var->obj_name);

  if (cv == NULL)
    {
      warning
	("Assertion failed: Could not find variable object \"%s\" to delete",
	 var->obj_name);
      return;
    }

  if (prev == NULL)
    *(varobj_table + index) = cv->next;
  else
    prev->next = cv->next;

  xfree (cv);

  /* If root, remove varobj from root list */
  if (var->root->rootvar == var)
    {
      /* Remove from list of root variables */
      if (rootlist == var->root)
	rootlist = var->root->next;
      else
	{
	  prer = NULL;
	  cr = rootlist;
	  while ((cr != NULL) && (cr->rootvar != var))
	    {
	      prer = cr;
	      cr = cr->next;
	    }
	  if (cr == NULL)
	    {
	      warning
		("Assertion failed: Could not find varobj \"%s\" in root list",
		 var->obj_name);
	      return;
	    }
	  if (prer == NULL)
	    rootlist = NULL;
	  else
	    prer->next = cr->next;
	}
      rootcount--;
    }

}

/* Does a child with the name NAME exist in VAR? If so, return its data.
   If not, return NULL. */
static struct varobj *
child_exists (struct varobj *var, char *name)
{
  struct varobj_child *vc;

  for (vc = var->children; vc != NULL; vc = vc->next)
    {
      if (strcmp (vc->child->name, name) == 0)
	return vc->child;
    }

  return NULL;
}

/* Create and install a child of the parent of the given name */
static struct varobj *
create_child (struct varobj *parent, int index, char *name)
{
  struct varobj *child;
  char *childs_name;

  child = new_variable ();

  /* name is allocated by name_of_child */
  child->name = name;
  child->index = index;
  child->value = value_of_child (parent, index);
  if ((!CPLUS_FAKE_CHILD (child) && child->value == NULL) || parent->error)
    child->error = 1;
  child->parent = parent;
  child->root = parent->root;
  xasprintf (&childs_name, "%s.%s", parent->obj_name, name);
  child->obj_name = childs_name;
  install_variable (child);

  /* Save a pointer to this child in the parent */
  save_child_in_parent (parent, child);

  /* Note the type of this child */
  child->type = type_of_child (child);

  return child;
}

/* FIXME: This should be a generic add to list */
/* Save CHILD in the PARENT's data. */
static void
save_child_in_parent (struct varobj *parent, struct varobj *child)
{
  struct varobj_child *vc;

  /* Insert the child at the top */
  vc = parent->children;
  parent->children =
    (struct varobj_child *) xmalloc (sizeof (struct varobj_child));

  parent->children->next = vc;
  parent->children->child = child;
}

/* FIXME: This should be a generic remove from list */
/* Remove the CHILD from the PARENT's list of children. */
static void
remove_child_from_parent (struct varobj *parent, struct varobj *child)
{
  struct varobj_child *vc, *prev;

  /* Find the child in the parent's list */
  prev = NULL;
  for (vc = parent->children; vc != NULL;)
    {
      if (vc->child == child)
	break;
      prev = vc;
      vc = vc->next;
    }

  if (prev == NULL)
    parent->children = vc->next;
  else
    prev->next = vc->next;

}


/*
 * Miscellaneous utility functions.
 */

/* Allocate memory and initialize a new variable */
static struct varobj *
new_variable (void)
{
  struct varobj *var;

  var = (struct varobj *) xmalloc (sizeof (struct varobj));
  var->name = NULL;
  var->obj_name = NULL;
  var->index = -1;
  var->type = NULL;
  var->value = NULL;
  var->error = 0;
  var->num_children = -1;
  var->parent = NULL;
  var->children = NULL;
  var->format = 0;
  var->root = NULL;
  var->updated = 0;

  return var;
}

/* Allocate memory and initialize a new root variable */
static struct varobj *
new_root_variable (void)
{
  struct varobj *var = new_variable ();
  var->root = (struct varobj_root *) xmalloc (sizeof (struct varobj_root));;
  var->root->lang = NULL;
  var->root->exp = NULL;
  var->root->valid_block = NULL;
  var->root->frame = null_frame_id;
  var->root->use_selected_frame = 0;
  var->root->rootvar = NULL;

  return var;
}

/* Free any allocated memory associated with VAR. */
static void
free_variable (struct varobj *var)
{
  /* Free the expression if this is a root variable. */
  if (var->root->rootvar == var)
    {
      free_current_contents ((char **) &var->root->exp);
      xfree (var->root);
    }

  xfree (var->name);
  xfree (var->obj_name);
  xfree (var);
}

static void
do_free_variable_cleanup (void *var)
{
  free_variable (var);
}

static struct cleanup *
make_cleanup_free_variable (struct varobj *var)
{
  return make_cleanup (do_free_variable_cleanup, var);
}

/* This returns the type of the variable. It also skips past typedefs
   to return the real type of the variable.

   NOTE: TYPE_TARGET_TYPE should NOT be used anywhere in this file
   except within get_target_type and get_type. */
static struct type *
get_type (struct varobj *var)
{
  struct type *type;
  type = var->type;

  if (type != NULL)
    type = check_typedef (type);

  return type;
}

/* This returns the type of the variable, dereferencing pointers, too. */
static struct type *
get_type_deref (struct varobj *var)
{
  struct type *type;

  type = get_type (var);

  if (type != NULL && (TYPE_CODE (type) == TYPE_CODE_PTR
		       || TYPE_CODE (type) == TYPE_CODE_REF))
    type = get_target_type (type);

  return type;
}

/* This returns the target type (or NULL) of TYPE, also skipping
   past typedefs, just like get_type ().

   NOTE: TYPE_TARGET_TYPE should NOT be used anywhere in this file
   except within get_target_type and get_type. */
static struct type *
get_target_type (struct type *type)
{
  if (type != NULL)
    {
      type = TYPE_TARGET_TYPE (type);
      if (type != NULL)
	type = check_typedef (type);
    }

  return type;
}

/* What is the default display for this variable? We assume that
   everything is "natural". Any exceptions? */
static enum varobj_display_formats
variable_default_display (struct varobj *var)
{
  return FORMAT_NATURAL;
}

/* This function is similar to gdb's value_equal, except that this
   one is "safe" -- it NEVER longjmps. It determines if the VAR's
   value is the same as VAL2. */
static int
my_value_equal (struct value *val1, struct value *val2, int *error2)
{
  int r, err1, err2;

  *error2 = 0;
  /* Special case: NULL values. If both are null, say
     they're equal. */
  if (val1 == NULL && val2 == NULL)
    return 1;
  else if (val1 == NULL || val2 == NULL)
    return 0;

  /* This is bogus, but unfortunately necessary. We must know
     exactly what caused an error -- reading val1 or val2 --  so
     that we can really determine if we think that something has changed. */
  err1 = 0;
  err2 = 0;
  /* We do need to catch errors here because the whole purpose
     is to test if value_equal() has errored */
  if (!gdb_value_equal (val1, val1, &r))
    err1 = 1;

  if (!gdb_value_equal (val2, val2, &r))
    *error2 = err2 = 1;

  if (err1 != err2)
    return 0;

  if (!gdb_value_equal (val1, val2, &r))
    {
      /* An error occurred, this could have happened if
         either val1 or val2 errored. ERR1 and ERR2 tell
         us which of these it is. If both errored, then
         we assume nothing has changed. If one of them is
         valid, though, then something has changed. */
      if (err1 == err2)
	{
	  /* both the old and new values caused errors, so
	     we say the value did not change */
	  /* This is indeterminate, though. Perhaps we should
	     be safe and say, yes, it changed anyway?? */
	  return 1;
	}
      else
	{
	  return 0;
	}
    }

  return r;
}

/* FIXME: The following should be generic for any pointer */
static void
vpush (struct vstack **pstack, struct varobj *var)
{
  struct vstack *s;

  s = (struct vstack *) xmalloc (sizeof (struct vstack));
  s->var = var;
  s->next = *pstack;
  *pstack = s;
}

/* FIXME: The following should be generic for any pointer */
static struct varobj *
vpop (struct vstack **pstack)
{
  struct vstack *s;
  struct varobj *v;

  if ((*pstack)->var == NULL && (*pstack)->next == NULL)
    return NULL;

  s = *pstack;
  v = s->var;
  *pstack = (*pstack)->next;
  xfree (s);

  return v;
}

/* FIXME: The following should be generic for any pointer */
static void
cppush (struct cpstack **pstack, char *name)
{
  struct cpstack *s;

  s = (struct cpstack *) xmalloc (sizeof (struct cpstack));
  s->name = name;
  s->next = *pstack;
  *pstack = s;
}

/* FIXME: The following should be generic for any pointer */
static char *
cppop (struct cpstack **pstack)
{
  struct cpstack *s;
  char *v;

  if ((*pstack)->name == NULL && (*pstack)->next == NULL)
    return NULL;

  s = *pstack;
  v = s->name;
  *pstack = (*pstack)->next;
  xfree (s);

  return v;
}

/*
 * Language-dependencies
 */

/* Common entry points */

/* Get the language of variable VAR. */
static enum varobj_languages
variable_language (struct varobj *var)
{
  enum varobj_languages lang;

  switch (var->root->exp->language_defn->la_language)
    {
    default:
    case language_c:
      lang = vlang_c;
      break;
    case language_cplus:
      lang = vlang_cplus;
      break;
    case language_java:
      lang = vlang_java;
      break;
    }

  return lang;
}

/* Return the number of children for a given variable.
   The result of this function is defined by the language
   implementation. The number of children returned by this function
   is the number of children that the user will see in the variable
   display. */
static int
number_of_children (struct varobj *var)
{
  return (*var->root->lang->number_of_children) (var);;
}

/* What is the expression for the root varobj VAR? Returns a malloc'd string. */
static char *
name_of_variable (struct varobj *var)
{
  return (*var->root->lang->name_of_variable) (var);
}

/* What is the name of the INDEX'th child of VAR? Returns a malloc'd string. */
static char *
name_of_child (struct varobj *var, int index)
{
  return (*var->root->lang->name_of_child) (var, index);
}

/* What is the ``struct value *'' of the root variable VAR? 
   TYPE_CHANGED controls what to do if the type of a
   use_selected_frame = 1 variable changes.  On input,
   TYPE_CHANGED = 1 means discard the old varobj, and replace
   it with this one.  TYPE_CHANGED = 0 means leave it around.
   NB: In both cases, var_handle will point to the new varobj,
   so if you use TYPE_CHANGED = 0, you will have to stash the
   old varobj pointer away somewhere before calling this.
   On return, TYPE_CHANGED will be 1 if the type has changed, and 
   0 otherwise. */
static struct value *
value_of_root (struct varobj **var_handle, int *type_changed)
{
  struct varobj *var;

  if (var_handle == NULL)
    return NULL;

  var = *var_handle;

  /* This should really be an exception, since this should
     only get called with a root variable. */

  if (var->root->rootvar != var)
    return NULL;

  if (var->root->use_selected_frame)
    {
      struct varobj *tmp_var;
      char *old_type, *new_type;
      old_type = varobj_get_type (var);
      tmp_var = varobj_create (NULL, var->name, (CORE_ADDR) 0,
			       USE_SELECTED_FRAME);
      if (tmp_var == NULL)
	{
	  return NULL;
	}
      new_type = varobj_get_type (tmp_var);
      if (strcmp (old_type, new_type) == 0)
	{
	  varobj_delete (tmp_var, NULL, 0);
	  *type_changed = 0;
	}
      else
	{
	  if (*type_changed)
	    {
	      tmp_var->obj_name =
		savestring (var->obj_name, strlen (var->obj_name));
	      varobj_delete (var, NULL, 0);
	    }
	  else
	    {
	      tmp_var->obj_name = varobj_gen_name ();
	    }
	  install_variable (tmp_var);
	  *var_handle = tmp_var;
	  var = *var_handle;
	  *type_changed = 1;
	}
    }
  else
    {
      *type_changed = 0;
    }

  return (*var->root->lang->value_of_root) (var_handle);
}

/* What is the ``struct value *'' for the INDEX'th child of PARENT? */
static struct value *
value_of_child (struct varobj *parent, int index)
{
  struct value *value;

  value = (*parent->root->lang->value_of_child) (parent, index);

  /* If we're being lazy, fetch the real value of the variable. */
  if (value != NULL && VALUE_LAZY (value))
    {
      /* If we fail to fetch the value of the child, return
         NULL so that callers notice that we're leaving an
         error message. */
      if (!gdb_value_fetch_lazy (value))
	value = NULL;
    }

  return value;
}

/* What is the type of VAR? */
static struct type *
type_of_child (struct varobj *var)
{

  /* If the child had no evaluation errors, var->value
     will be non-NULL and contain a valid type. */
  if (var->value != NULL)
    return VALUE_TYPE (var->value);

  /* Otherwise, we must compute the type. */
  return (*var->root->lang->type_of_child) (var->parent, var->index);
}

/* Is this variable editable? Use the variable's type to make
   this determination. */
static int
variable_editable (struct varobj *var)
{
  return (*var->root->lang->variable_editable) (var);
}

/* GDB already has a command called "value_of_variable". Sigh. */
static char *
my_value_of_variable (struct varobj *var)
{
  return (*var->root->lang->value_of_variable) (var);
}

/* Is VAR something that can change? Depending on language,
   some variable's values never change. For example,
   struct and unions never change values. */
static int
type_changeable (struct varobj *var)
{
  int r;
  struct type *type;

  if (CPLUS_FAKE_CHILD (var))
    return 0;

  type = get_type (var);

  switch (TYPE_CODE (type))
    {
    case TYPE_CODE_STRUCT:
    case TYPE_CODE_UNION:
    case TYPE_CODE_ARRAY:
      r = 0;
      break;

    default:
      r = 1;
    }

  return r;
}

/* C */
static int
c_number_of_children (struct varobj *var)
{
  struct type *type;
  struct type *target;
  int children;

  type = get_type (var);
  target = get_target_type (type);
  children = 0;

  switch (TYPE_CODE (type))
    {
    case TYPE_CODE_ARRAY:
      if (TYPE_LENGTH (type) > 0 && TYPE_LENGTH (target) > 0
	  && TYPE_ARRAY_UPPER_BOUND_TYPE (type) != BOUND_CANNOT_BE_DETERMINED)
	children = TYPE_LENGTH (type) / TYPE_LENGTH (target);
      else
	children = -1;
      break;

    case TYPE_CODE_STRUCT:
    case TYPE_CODE_UNION:
      children = TYPE_NFIELDS (type);
      break;

    case TYPE_CODE_PTR:
      /* This is where things get compilcated. All pointers have one child.
         Except, of course, for struct and union ptr, which we automagically
         dereference for the user and function ptrs, which have no children.
         We also don't dereference void* as we don't know what to show.
         We can show char* so we allow it to be dereferenced.  If you decide
         to test for it, please mind that a little magic is necessary to
         properly identify it: char* has TYPE_CODE == TYPE_CODE_INT and 
         TYPE_NAME == "char" */

      switch (TYPE_CODE (target))
	{
	case TYPE_CODE_STRUCT:
	case TYPE_CODE_UNION:
	  children = TYPE_NFIELDS (target);
	  break;

	case TYPE_CODE_FUNC:
	case TYPE_CODE_VOID:
	  children = 0;
	  break;

	default:
	  children = 1;
	}
      break;

    default:
      /* Other types have no children */
      break;
    }

  return children;
}

static char *
c_name_of_variable (struct varobj *parent)
{
  return savestring (parent->name, strlen (parent->name));
}

static char *
c_name_of_child (struct varobj *parent, int index)
{
  struct type *type;
  struct type *target;
  char *name;
  char *string;

  type = get_type (parent);
  target = get_target_type (type);

  switch (TYPE_CODE (type))
    {
    case TYPE_CODE_ARRAY:
      xasprintf (&name, "%d", index);
      break;

    case TYPE_CODE_STRUCT:
    case TYPE_CODE_UNION:
      string = TYPE_FIELD_NAME (type, index);
      name = savestring (string, strlen (string));
      break;

    case TYPE_CODE_PTR:
      switch (TYPE_CODE (target))
	{
	case TYPE_CODE_STRUCT:
	case TYPE_CODE_UNION:
	  string = TYPE_FIELD_NAME (target, index);
	  name = savestring (string, strlen (string));
	  break;

	default:
	  xasprintf (&name, "*%s", parent->name);
	  break;
	}
      break;

    default:
      /* This should not happen */
      name = xstrdup ("???");
    }

  return name;
}

static struct value *
c_value_of_root (struct varobj **var_handle)
{
  struct value *new_val;
  struct varobj *var = *var_handle;
  struct frame_info *fi;
  int within_scope;

  /*  Only root variables can be updated... */
  if (var->root->rootvar != var)
    /* Not a root var */
    return NULL;


  /* Determine whether the variable is still around. */
  if (var->root->valid_block == NULL)
    within_scope = 1;
  else
    {
      reinit_frame_cache ();
      fi = frame_find_by_id (var->root->frame);
      within_scope = fi != NULL;
      /* FIXME: select_frame could fail */
      if (within_scope)
	select_frame (fi);
    }

  if (within_scope)
    {
      /* We need to catch errors here, because if evaluate
         expression fails we just want to make val->error = 1 and
         go on */
      if (gdb_evaluate_expression (var->root->exp, &new_val))
	{
	  if (VALUE_LAZY (new_val))
	    {
	      /* We need to catch errors because if
	         value_fetch_lazy fails we still want to continue
	         (after making val->error = 1) */
	      /* FIXME: Shouldn't be using VALUE_CONTENTS?  The
	         comment on value_fetch_lazy() says it is only
	         called from the macro... */
	      if (!gdb_value_fetch_lazy (new_val))
		var->error = 1;
	      else
		var->error = 0;
	    }
	}
      else
	var->error = 1;

      release_value (new_val);
      return new_val;
    }

  return NULL;
}

static struct value *
c_value_of_child (struct varobj *parent, int index)
{
  struct value *value;
  struct value *temp;
  struct value *indval;
  struct type *type, *target;
  char *name;

  type = get_type (parent);
  target = get_target_type (type);
  name = name_of_child (parent, index);
  temp = parent->value;
  value = NULL;

  if (temp != NULL)
    {
      switch (TYPE_CODE (type))
	{
	case TYPE_CODE_ARRAY:
#if 0
	  /* This breaks if the array lives in a (vector) register. */
	  value = value_slice (temp, index, 1);
	  temp = value_coerce_array (value);
	  gdb_value_ind (temp, &value);
#else
	  indval = value_from_longest (builtin_type_int, (LONGEST) index);
	  gdb_value_subscript (temp, indval, &value);
#endif
	  break;

	case TYPE_CODE_STRUCT:
	case TYPE_CODE_UNION:
	  gdb_value_struct_elt (NULL, &value, &temp, NULL, name, NULL,
				"vstructure");
	  break;

	case TYPE_CODE_PTR:
	  switch (TYPE_CODE (target))
	    {
	    case TYPE_CODE_STRUCT:
	    case TYPE_CODE_UNION:
	      gdb_value_struct_elt (NULL, &value, &temp, NULL, name, NULL,
				    "vstructure");
	      break;

	    default:
	      gdb_value_ind (temp, &value);
	      break;
	    }
	  break;

	default:
	  break;
	}
    }

  if (value != NULL)
    release_value (value);

  xfree (name);
  return value;
}

static struct type *
c_type_of_child (struct varobj *parent, int index)
{
  struct type *type;
  char *name = name_of_child (parent, index);

  switch (TYPE_CODE (parent->type))
    {
    case TYPE_CODE_ARRAY:
      type = get_target_type (parent->type);
      break;

    case TYPE_CODE_STRUCT:
    case TYPE_CODE_UNION:
      type = lookup_struct_elt_type (parent->type, name, 0);
      break;

    case TYPE_CODE_PTR:
      switch (TYPE_CODE (get_target_type (parent->type)))
	{
	case TYPE_CODE_STRUCT:
	case TYPE_CODE_UNION:
	  type = lookup_struct_elt_type (parent->type, name, 0);
	  break;

	default:
	  type = get_target_type (parent->type);
	  break;
	}
      break;

    default:
      /* This should not happen as only the above types have children */
      warning ("Child of parent whose type does not allow children");
      /* FIXME: Can we still go on? */
      type = NULL;
      break;
    }

  xfree (name);
  return type;
}

static int
c_variable_editable (struct varobj *var)
{
  switch (TYPE_CODE (get_type (var)))
    {
    case TYPE_CODE_STRUCT:
    case TYPE_CODE_UNION:
    case TYPE_CODE_ARRAY:
    case TYPE_CODE_FUNC:
    case TYPE_CODE_MEMBER:
    case TYPE_CODE_METHOD:
      return 0;
      break;

    default:
      return 1;
      break;
    }
}

static char *
c_value_of_variable (struct varobj *var)
{
  /* BOGUS: if val_print sees a struct/class, it will print out its
     children instead of "{...}" */

  switch (TYPE_CODE (get_type (var)))
    {
    case TYPE_CODE_STRUCT:
    case TYPE_CODE_UNION:
      return xstrdup ("{...}");
      /* break; */

    case TYPE_CODE_ARRAY:
      {
	char *number;
	xasprintf (&number, "[%d]", var->num_children);
	return (number);
      }
      /* break; */

    default:
      {
	if (var->value == NULL)
	  {
	    /* This can happen if we attempt to get the value of a struct
	       member when the parent is an invalid pointer. This is an
	       error condition, so we should tell the caller. */
	    return NULL;
	  }
	else
	  {
	    long dummy;
	    struct ui_file *stb = mem_fileopen ();
	    struct cleanup *old_chain = make_cleanup_ui_file_delete (stb);
	    char *thevalue;

	    if (VALUE_LAZY (var->value))
	      gdb_value_fetch_lazy (var->value);
	    common_val_print (var->value, stb,
			      format_code[(int) var->format], 1, 0, 0);
	    thevalue = ui_file_xstrdup (stb, &dummy);
	    do_cleanups (old_chain);
	return thevalue;
      }
      }
    }
}


/* C++ */

static int
cplus_number_of_children (struct varobj *var)
{
  struct type *type;
  int children, dont_know;

  dont_know = 1;
  children = 0;

  if (!CPLUS_FAKE_CHILD (var))
    {
      type = get_type_deref (var);

      if (((TYPE_CODE (type)) == TYPE_CODE_STRUCT) ||
	  ((TYPE_CODE (type)) == TYPE_CODE_UNION))
	{
	  int kids[3];

	  cplus_class_num_children (type, kids);
	  if (kids[v_public] != 0)
	    children++;
	  if (kids[v_private] != 0)
	    children++;
	  if (kids[v_protected] != 0)
	    children++;

	  /* Add any baseclasses */
	  children += TYPE_N_BASECLASSES (type);
	  dont_know = 0;

	  /* FIXME: save children in var */
	}
    }
  else
    {
      int kids[3];

      type = get_type_deref (var->parent);

      cplus_class_num_children (type, kids);
      if (strcmp (var->name, "public") == 0)
	children = kids[v_public];
      else if (strcmp (var->name, "private") == 0)
	children = kids[v_private];
      else
	children = kids[v_protected];
      dont_know = 0;
    }

  if (dont_know)
    children = c_number_of_children (var);

  return children;
}

/* Compute # of public, private, and protected variables in this class.
   That means we need to descend into all baseclasses and find out
   how many are there, too. */
static void
cplus_class_num_children (struct type *type, int children[3])
{
  int i;

  children[v_public] = 0;
  children[v_private] = 0;
  children[v_protected] = 0;

  for (i = TYPE_N_BASECLASSES (type); i < TYPE_NFIELDS (type); i++)
    {
      /* If we have a virtual table pointer, omit it. */
      if (TYPE_VPTR_BASETYPE (type) == type && TYPE_VPTR_FIELDNO (type) == i)
	continue;

      if (TYPE_FIELD_PROTECTED (type, i))
	children[v_protected]++;
      else if (TYPE_FIELD_PRIVATE (type, i))
	children[v_private]++;
      else
	children[v_public]++;
    }
}

static char *
cplus_name_of_variable (struct varobj *parent)
{
  return c_name_of_variable (parent);
}

static char *
cplus_name_of_child (struct varobj *parent, int index)
{
  char *name;
  struct type *type;

  if (CPLUS_FAKE_CHILD (parent))
    {
      /* Looking for children of public, private, or protected. */
      type = get_type_deref (parent->parent);
    }
  else
    type = get_type_deref (parent);

  name = NULL;
  switch (TYPE_CODE (type))
    {
    case TYPE_CODE_STRUCT:
    case TYPE_CODE_UNION:
      if (CPLUS_FAKE_CHILD (parent))
	{
	  /* The fields of the class type are ordered as they
	     appear in the class.  We are given an index for a
	     particular access control type ("public","protected",
	     or "private").  We must skip over fields that don't
	     have the access control we are looking for to properly
	     find the indexed field. */
	  int type_index = TYPE_N_BASECLASSES (type);
	  if (strcmp (parent->name, "private") == 0)
	    {
	      while (index >= 0)
		{
	  	  if (TYPE_VPTR_BASETYPE (type) == type
	      	      && type_index == TYPE_VPTR_FIELDNO (type))
		    ; /* ignore vptr */
		  else if (TYPE_FIELD_PRIVATE (type, type_index))
		    --index;
		  ++type_index;
		}
	      --type_index;
	    }
	  else if (strcmp (parent->name, "protected") == 0)
	    {
	      while (index >= 0)
		{
	  	  if (TYPE_VPTR_BASETYPE (type) == type
	      	      && type_index == TYPE_VPTR_FIELDNO (type))
		    ; /* ignore vptr */
		  else if (TYPE_FIELD_PROTECTED (type, type_index))
		    --index;
		  ++type_index;
		}
	      --type_index;
	    }
	  else
	    {
	      while (index >= 0)
		{
	  	  if (TYPE_VPTR_BASETYPE (type) == type
	      	      && type_index == TYPE_VPTR_FIELDNO (type))
		    ; /* ignore vptr */
		  else if (!TYPE_FIELD_PRIVATE (type, type_index) &&
		      !TYPE_FIELD_PROTECTED (type, type_index))
		    --index;
		  ++type_index;
		}
	      --type_index;
	    }

	  name = TYPE_FIELD_NAME (type, type_index);
	}
      else if (index < TYPE_N_BASECLASSES (type))
	/* We are looking up the name of a base class */
	name = TYPE_FIELD_NAME (type, index);
      else
	{
	  int children[3];
	  cplus_class_num_children(type, children);

	  /* Everything beyond the baseclasses can
	     only be "public", "private", or "protected"

	     The special "fake" children are always output by varobj in
	     this order. So if INDEX == 2, it MUST be "protected". */
	  index -= TYPE_N_BASECLASSES (type);
	  switch (index)
	    {
	    case 0:
	      if (children[v_public] > 0)
	 	name = "public";
	      else if (children[v_private] > 0)
	 	name = "private";
	      else 
	 	name = "protected";
	      break;
	    case 1:
	      if (children[v_public] > 0)
		{
		  if (children[v_private] > 0)
		    name = "private";
		  else
		    name = "protected";
		}
	      else if (children[v_private] > 0)
	 	name = "protected";
	      break;
	    case 2:
	      /* Must be protected */
	      name = "protected";
	      break;
	    default:
	      /* error! */
	      break;
	    }
	}
      break;

    default:
      break;
    }

  if (name == NULL)
    return c_name_of_child (parent, index);
  else
    {
      if (name != NULL)
	name = savestring (name, strlen (name));
    }

  return name;
}

static struct value *
cplus_value_of_root (struct varobj **var_handle)
{
  return c_value_of_root (var_handle);
}

static struct value *
cplus_value_of_child (struct varobj *parent, int index)
{
  struct type *type;
  struct value *value;

  if (CPLUS_FAKE_CHILD (parent))
    type = get_type_deref (parent->parent);
  else
    type = get_type_deref (parent);

  value = NULL;

  if (((TYPE_CODE (type)) == TYPE_CODE_STRUCT) ||
      ((TYPE_CODE (type)) == TYPE_CODE_UNION))
    {
      if (CPLUS_FAKE_CHILD (parent))
	{
	  char *name;
	  struct value *temp = parent->parent->value;

	  if (temp == NULL)
	    return NULL;

	  name = name_of_child (parent, index);
	  gdb_value_struct_elt (NULL, &value, &temp, NULL, name, NULL,
				"cplus_structure");
	  if (value != NULL)
	    release_value (value);

	  xfree (name);
	}
      else if (index >= TYPE_N_BASECLASSES (type))
	{
	  /* public, private, or protected */
	  return NULL;
	}
      else
	{
	  /* Baseclass */
	  if (parent->value != NULL)
	    {
	      struct value *temp = NULL;

	      if (TYPE_CODE (VALUE_TYPE (parent->value)) == TYPE_CODE_PTR
		  || TYPE_CODE (VALUE_TYPE (parent->value)) == TYPE_CODE_REF)
		{
		  if (!gdb_value_ind (parent->value, &temp))
		    return NULL;
		}
	      else
		temp = parent->value;

	      if (temp != NULL)
		{
		  value = value_cast (TYPE_FIELD_TYPE (type, index), temp);
		  release_value (value);
		}
	      else
		{
		  /* We failed to evaluate the parent's value, so don't even
		     bother trying to evaluate this child. */
		  return NULL;
		}
	    }
	}
    }

  if (value == NULL)
    return c_value_of_child (parent, index);

  return value;
}

static struct type *
cplus_type_of_child (struct varobj *parent, int index)
{
  struct type *type, *t;

  if (CPLUS_FAKE_CHILD (parent))
    {
      /* Looking for the type of a child of public, private, or protected. */
      t = get_type_deref (parent->parent);
    }
  else
    t = get_type_deref (parent);

  type = NULL;
  switch (TYPE_CODE (t))
    {
    case TYPE_CODE_STRUCT:
    case TYPE_CODE_UNION:
      if (CPLUS_FAKE_CHILD (parent))
	{
	  char *name = cplus_name_of_child (parent, index);
	  type = lookup_struct_elt_type (t, name, 0);
	  xfree (name);
	}
      else if (index < TYPE_N_BASECLASSES (t))
	type = TYPE_FIELD_TYPE (t, index);
      else
	{
	  /* special */
	  return NULL;
	}
      break;

    default:
      break;
    }

  if (type == NULL)
    return c_type_of_child (parent, index);

  return type;
}

static int
cplus_variable_editable (struct varobj *var)
{
  if (CPLUS_FAKE_CHILD (var))
    return 0;

  return c_variable_editable (var);
}

static char *
cplus_value_of_variable (struct varobj *var)
{

  /* If we have one of our special types, don't print out
     any value. */
  if (CPLUS_FAKE_CHILD (var))
    return xstrdup ("");

  return c_value_of_variable (var);
}

/* Java */

static int
java_number_of_children (struct varobj *var)
{
  return cplus_number_of_children (var);
}

static char *
java_name_of_variable (struct varobj *parent)
{
  char *p, *name;

  name = cplus_name_of_variable (parent);
  /* If  the name has "-" in it, it is because we
     needed to escape periods in the name... */
  p = name;

  while (*p != '\000')
    {
      if (*p == '-')
	*p = '.';
      p++;
    }

  return name;
}

static char *
java_name_of_child (struct varobj *parent, int index)
{
  char *name, *p;

  name = cplus_name_of_child (parent, index);
  /* Escape any periods in the name... */
  p = name;

  while (*p != '\000')
    {
      if (*p == '.')
	*p = '-';
      p++;
    }

  return name;
}

static struct value *
java_value_of_root (struct varobj **var_handle)
{
  return cplus_value_of_root (var_handle);
}

static struct value *
java_value_of_child (struct varobj *parent, int index)
{
  return cplus_value_of_child (parent, index);
}

static struct type *
java_type_of_child (struct varobj *parent, int index)
{
  return cplus_type_of_child (parent, index);
}

static int
java_variable_editable (struct varobj *var)
{
  return cplus_variable_editable (var);
}

static char *
java_value_of_variable (struct varobj *var)
{
  return cplus_value_of_variable (var);
}

extern void _initialize_varobj (void);
void
_initialize_varobj (void)
{
  int sizeof_table = sizeof (struct vlist *) * VAROBJ_TABLE_SIZE;

  varobj_table = xmalloc (sizeof_table);
  memset (varobj_table, 0, sizeof_table);

  add_show_from_set (add_set_cmd ("debugvarobj", class_maintenance, var_zinteger, (char *) &varobjdebug, "Set varobj debugging.\n\
When non-zero, varobj debugging is enabled.", &setlist),
		     &showlist);
}
