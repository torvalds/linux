/* Tree browser.
   Copyright (C) 2002, 2003, 2004 Free Software Foundation, Inc.
   Contributed by Sebastian Pop <s.pop@laposte.net>

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
#include "tree.h"
#include "tree-inline.h"
#include "diagnostic.h"
#include "hashtab.h"


#define TB_OUT_FILE stdout
#define TB_IN_FILE stdin
#define TB_NIY fprintf (TB_OUT_FILE, "Sorry this command is not yet implemented.\n")
#define TB_WF fprintf (TB_OUT_FILE, "Warning, this command failed.\n")


/* Structures for handling Tree Browser's commands.  */
#define DEFTBCODE(COMMAND, STRING, HELP)   COMMAND,
enum TB_Comm_code {
#include "tree-browser.def"
  TB_UNUSED_COMMAND
};
#undef DEFTBCODE
typedef enum TB_Comm_code TB_CODE;

struct tb_command {
  const char *help_msg;
  const char *comm_text;
  size_t comm_len;
  TB_CODE comm_code;
};

#define DEFTBCODE(code, str, help) { help, str, sizeof(str) - 1, code },
static const struct tb_command tb_commands[] =
{
#include "tree-browser.def"
};
#undef DEFTBCODE

#define TB_COMMAND_LEN(N) (tb_commands[N].comm_len)
#define TB_COMMAND_TEXT(N) (tb_commands[N].comm_text)
#define TB_COMMAND_CODE(N) (tb_commands[N].comm_code)
#define TB_COMMAND_HELP(N) (tb_commands[N].help_msg)


/* Next structure is for parsing TREE_CODEs.  */
struct tb_tree_code {
  enum tree_code code;
  const char *code_string;
  size_t code_string_len;
};

#define DEFTREECODE(SYM, STRING, TYPE, NARGS) { SYM, STRING, sizeof (STRING) - 1 },
static const struct tb_tree_code tb_tree_codes[] =
{
#include "tree.def"
};
#undef DEFTREECODE

#define TB_TREE_CODE(N) (tb_tree_codes[N].code)
#define TB_TREE_CODE_TEXT(N) (tb_tree_codes[N].code_string)
#define TB_TREE_CODE_LEN(N) (tb_tree_codes[N].code_string_len)


/* Function declarations.  */

static long TB_getline (char **, long *, FILE *);
static TB_CODE TB_get_command (char *);
static enum tree_code TB_get_tree_code (char *);
static tree find_node_with_code (tree *, int *, void *);
static tree store_child_info (tree *, int *, void *);
static void TB_update_up (tree);
static tree TB_current_chain_node (tree);
static tree TB_prev_expr (tree);
static tree TB_next_expr (tree);
static tree TB_up_expr (tree);
static tree TB_first_in_bind (tree);
static tree TB_last_in_bind (tree);
static int  TB_parent_eq (const void *, const void *);
static tree TB_history_prev (void);

/* FIXME: To be declared in a .h file.  */
void browse_tree (tree);

/* Static variables.  */
static htab_t TB_up_ht;
static tree TB_history_stack = NULL_TREE;
static int TB_verbose = 1;


/* Entry point in the Tree Browser.  */

void
browse_tree (tree begin)
{
  tree head;
  TB_CODE tbc = TB_UNUSED_COMMAND;
  ssize_t rd;
  char *input = NULL;
  long input_size = 0;

  fprintf (TB_OUT_FILE, "\nTree Browser\n");

#define TB_SET_HEAD(N) do {                                           \
  TB_history_stack = tree_cons (NULL_TREE, (N), TB_history_stack);    \
  head = N;                                                           \
  if (TB_verbose)                                                     \
    if (head)                                                         \
      {                                                               \
	print_generic_expr (TB_OUT_FILE, head, 0);                    \
	fprintf (TB_OUT_FILE, "\n");                                  \
      }                                                               \
} while (0)

  TB_SET_HEAD (begin);

  /* Store in a hashtable information about previous and upper statements.  */
  {
    TB_up_ht = htab_create (1023, htab_hash_pointer, &TB_parent_eq, NULL);
    TB_update_up (head);
  }

  while (24)
    {
      fprintf (TB_OUT_FILE, "TB> ");
      rd = TB_getline (&input, &input_size, TB_IN_FILE);

      if (rd == -1)
	/* EOF.  */
	goto ret;

      if (rd != 1)
	/* Get a new command.  Otherwise the user just pressed enter, and thus
	   she expects the last command to be reexecuted.  */
	tbc = TB_get_command (input);

      switch (tbc)
	{
	case TB_UPDATE_UP:
	  TB_update_up (head);
	  break;

	case TB_MAX:
	  if (head && (INTEGRAL_TYPE_P (head)
		       || TREE_CODE (head) == REAL_TYPE))
	    TB_SET_HEAD (TYPE_MAX_VALUE (head));
	  else
	    TB_WF;
	  break;

	case TB_MIN:
	  if (head && (INTEGRAL_TYPE_P (head)
		       || TREE_CODE (head) == REAL_TYPE))
	    TB_SET_HEAD (TYPE_MIN_VALUE (head));
	  else
	    TB_WF;
	  break;

	case TB_ELT:
	  if (head && TREE_CODE (head) == TREE_VEC)
	    {
	      /* This command takes another argument: the element number:
		 for example "elt 1".  */
	      TB_NIY;
	    }
	  else if (head && TREE_CODE (head) == VECTOR_CST)
	    {
	      /* This command takes another argument: the element number:
                 for example "elt 1".  */
              TB_NIY;
	    }
	  else
	    TB_WF;
	  break;

	case TB_VALUE:
	  if (head && TREE_CODE (head) == TREE_LIST)
	    TB_SET_HEAD (TREE_VALUE (head));
	  else
	    TB_WF;
	  break;

	case TB_PURPOSE:
	  if (head && TREE_CODE (head) == TREE_LIST)
	    TB_SET_HEAD (TREE_PURPOSE (head));
	  else
	    TB_WF;
	  break;

	case TB_IMAG:
	  if (head && TREE_CODE (head) == COMPLEX_CST)
	    TB_SET_HEAD (TREE_IMAGPART (head));
	  else
	    TB_WF;
	  break;

	case TB_REAL:
	  if (head && TREE_CODE (head) == COMPLEX_CST)
	    TB_SET_HEAD (TREE_REALPART (head));
	  else
	    TB_WF;
	  break;

	case TB_BLOCK:
	  if (head && TREE_CODE (head) == BIND_EXPR)
	    TB_SET_HEAD (TREE_OPERAND (head, 2));
	  else
	    TB_WF;
	  break;

	case TB_SUBBLOCKS:
	  if (head && TREE_CODE (head) == BLOCK)
	    TB_SET_HEAD (BLOCK_SUBBLOCKS (head));
	  else
	    TB_WF;
	  break;

	case TB_SUPERCONTEXT:
	  if (head && TREE_CODE (head) == BLOCK)
	    TB_SET_HEAD (BLOCK_SUPERCONTEXT (head));
	  else
	    TB_WF;
	  break;

	case TB_VARS:
	  if (head && TREE_CODE (head) == BLOCK)
	    TB_SET_HEAD (BLOCK_VARS (head));
	  else if (head && TREE_CODE (head) == BIND_EXPR)
	    TB_SET_HEAD (TREE_OPERAND (head, 0));
	  else
	    TB_WF;
	  break;

	case TB_REFERENCE_TO_THIS:
	  if (head && TYPE_P (head))
	    TB_SET_HEAD (TYPE_REFERENCE_TO (head));
	  else
	    TB_WF;
	  break;

	case TB_POINTER_TO_THIS:
	  if (head && TYPE_P (head))
	    TB_SET_HEAD (TYPE_POINTER_TO (head));
	  else
	    TB_WF;
	  break;

	case TB_BASETYPE:
	  if (head && TREE_CODE (head) == OFFSET_TYPE)
	    TB_SET_HEAD (TYPE_OFFSET_BASETYPE (head));
	  else
	    TB_WF;
	  break;

	case TB_ARG_TYPES:
	  if (head && (TREE_CODE (head) == FUNCTION_TYPE
		       || TREE_CODE (head) == METHOD_TYPE))
	    TB_SET_HEAD (TYPE_ARG_TYPES (head));
	  else
	    TB_WF;
	  break;

	case TB_METHOD_BASE_TYPE:
	  if (head && (TREE_CODE (head) == FUNCTION_TYPE
		       || TREE_CODE (head) == METHOD_TYPE)
	      && TYPE_METHOD_BASETYPE (head))
	    TB_SET_HEAD (TYPE_METHOD_BASETYPE (head));
	  else
	    TB_WF;
	  break;

	case TB_FIELDS:
	  if (head && (TREE_CODE (head) == RECORD_TYPE
		       || TREE_CODE (head) == UNION_TYPE
		       || TREE_CODE (head) == QUAL_UNION_TYPE))
	    TB_SET_HEAD (TYPE_FIELDS (head));
	  else
	    TB_WF;
	  break;

	case TB_DOMAIN:
	  if (head && TREE_CODE (head) == ARRAY_TYPE)
	    TB_SET_HEAD (TYPE_DOMAIN (head));
	  else
	    TB_WF;
	  break;

	case TB_VALUES:
	  if (head && TREE_CODE (head) == ENUMERAL_TYPE)
	    TB_SET_HEAD (TYPE_VALUES (head));
	  else
	    TB_WF;
	  break;

	case TB_ARG_TYPE:
	  if (head && TREE_CODE (head) == PARM_DECL)
	    TB_SET_HEAD (DECL_ARG_TYPE (head));
	  else
	    TB_WF;
	  break;

	case TB_INITIAL:
	  if (head && DECL_P (head))
	    TB_SET_HEAD (DECL_INITIAL (head));
	  else
	    TB_WF;
	  break;

	case TB_RESULT:
	  if (head && DECL_P (head))
	    TB_SET_HEAD (DECL_RESULT_FLD (head));
	  else
	    TB_WF;
	  break;

	case TB_ARGUMENTS:
	  if (head && DECL_P (head))
	    TB_SET_HEAD (DECL_ARGUMENTS (head));
	  else
	    TB_WF;
	  break;

	case TB_ABSTRACT_ORIGIN:
	  if (head && DECL_P (head))
	    TB_SET_HEAD (DECL_ABSTRACT_ORIGIN (head));
	  else if (head && TREE_CODE (head) == BLOCK)
	    TB_SET_HEAD (BLOCK_ABSTRACT_ORIGIN (head));
	  else
	    TB_WF;
	  break;

	case TB_ATTRIBUTES:
	  if (head && DECL_P (head))
	    TB_SET_HEAD (DECL_ATTRIBUTES (head));
	  else if (head && TYPE_P (head))
	    TB_SET_HEAD (TYPE_ATTRIBUTES (head));
	  else
	    TB_WF;
	  break;

	case TB_CONTEXT:
	  if (head && DECL_P (head))
	    TB_SET_HEAD (DECL_CONTEXT (head));
	  else if (head && TYPE_P (head)
		   && TYPE_CONTEXT (head))
	    TB_SET_HEAD (TYPE_CONTEXT (head));
	  else
	    TB_WF;
	  break;

	case TB_OFFSET:
	  if (head && TREE_CODE (head) == FIELD_DECL)
	    TB_SET_HEAD (DECL_FIELD_OFFSET (head));
	  else
	    TB_WF;
	  break;

	case TB_BIT_OFFSET:
	  if (head && TREE_CODE (head) == FIELD_DECL)
	    TB_SET_HEAD (DECL_FIELD_BIT_OFFSET (head));
	  else
	    TB_WF;
          break;

	case TB_UNIT_SIZE:
	  if (head && DECL_P (head))
	    TB_SET_HEAD (DECL_SIZE_UNIT (head));
	  else if (head && TYPE_P (head))
	    TB_SET_HEAD (TYPE_SIZE_UNIT (head));
	  else
	    TB_WF;
	  break;

	case TB_SIZE:
	  if (head && DECL_P (head))
	    TB_SET_HEAD (DECL_SIZE (head));
	  else if (head && TYPE_P (head))
	    TB_SET_HEAD (TYPE_SIZE (head));
	  else
	    TB_WF;
	  break;

	case TB_TYPE:
	  if (head && TREE_TYPE (head))
	    TB_SET_HEAD (TREE_TYPE (head));
	  else
	    TB_WF;
	  break;

	case TB_DECL_SAVED_TREE:
	  if (head && TREE_CODE (head) == FUNCTION_DECL
	      && DECL_SAVED_TREE (head))
	    TB_SET_HEAD (DECL_SAVED_TREE (head));
	  else
	    TB_WF;
	  break;

	case TB_BODY:
	  if (head && TREE_CODE (head) == BIND_EXPR)
	    TB_SET_HEAD (TREE_OPERAND (head, 1));
	  else
	    TB_WF;
	  break;

	case TB_CHILD_0:
	  if (head && EXPR_P (head) && TREE_OPERAND (head, 0))
	    TB_SET_HEAD (TREE_OPERAND (head, 0));
	  else
	    TB_WF;
	  break;

	case TB_CHILD_1:
          if (head && EXPR_P (head) && TREE_OPERAND (head, 1))
	    TB_SET_HEAD (TREE_OPERAND (head, 1));
	  else
	    TB_WF;
          break;

	case TB_CHILD_2:
          if (head && EXPR_P (head) && TREE_OPERAND (head, 2))
	    TB_SET_HEAD (TREE_OPERAND (head, 2));
	  else
	    TB_WF;
	  break;

	case TB_CHILD_3:
	  if (head && EXPR_P (head) && TREE_OPERAND (head, 3))
	    TB_SET_HEAD (TREE_OPERAND (head, 3));
	  else
	    TB_WF;
          break;

	case TB_PRINT:
	  if (head)
	    debug_tree (head);
	  else
	    TB_WF;
	  break;

	case TB_PRETTY_PRINT:
	  if (head)
	    {
	      print_generic_stmt (TB_OUT_FILE, head, 0);
	      fprintf (TB_OUT_FILE, "\n");
	    }
	  else
	    TB_WF;
	  break;

	case TB_SEARCH_NAME:

	  break;

	case TB_SEARCH_CODE:
	  {
	    enum tree_code code;
	    char *arg_text;

	    arg_text = strchr (input, ' ');
	    if (arg_text == NULL)
	      {
		fprintf (TB_OUT_FILE, "First argument is missing.  This isn't a valid search command.  \n");
		break;
	      }
	    code = TB_get_tree_code (arg_text + 1);

	    /* Search in the subtree a node with the given code.  */
	    {
	      tree res;

	      res = walk_tree (&head, find_node_with_code, &code, NULL);
	      if (res == NULL_TREE)
		{
		  fprintf (TB_OUT_FILE, "There's no node with this code (reachable via the walk_tree function from this node).\n");
		}
	      else
		{
		  fprintf (TB_OUT_FILE, "Achoo!  I got this node in the tree.\n");
		  TB_SET_HEAD (res);
		}
	    }
	    break;
	  }

#define TB_MOVE_HEAD(FCT) do {       \
  if (head)                          \
    {                                \
      tree t;                        \
      t = FCT (head);                \
      if (t)                         \
        TB_SET_HEAD (t);             \
      else                           \
	TB_WF;                       \
    }                                \
  else                               \
    TB_WF;                           \
} while (0)

	case TB_FIRST:
	  TB_MOVE_HEAD (TB_first_in_bind);
          break;

        case TB_LAST:
          TB_MOVE_HEAD (TB_last_in_bind);
          break;

	case TB_UP:
	  TB_MOVE_HEAD (TB_up_expr);
	  break;

	case TB_PREV:
	  TB_MOVE_HEAD (TB_prev_expr);
	  break;

	case TB_NEXT:
	  TB_MOVE_HEAD (TB_next_expr);
	  break;

	case TB_HPREV:
	  /* This command is a little bit special, since it deals with history
	     stack.  For this reason it should keep the "head = ..." statement
	     and not use TB_MOVE_HEAD.  */
	  if (head)
	    {
	      tree t;
	      t = TB_history_prev ();
	      if (t)
		{
		  head = t;
		  if (TB_verbose)
		    {
		      print_generic_expr (TB_OUT_FILE, head, 0);
		      fprintf (TB_OUT_FILE, "\n");
		    }
		}
	      else
		TB_WF;
	    }
	  else
	    TB_WF;
	  break;

	case TB_CHAIN:
	  /* Don't go further if it's the last node in this chain.  */
	  if (head && TREE_CODE (head) == BLOCK)
	    TB_SET_HEAD (BLOCK_CHAIN (head));
	  else if (head && TREE_CHAIN (head))
	    TB_SET_HEAD (TREE_CHAIN (head));
	  else
	    TB_WF;
	  break;

	case TB_FUN:
	  /* Go up to the current function declaration.  */
	  TB_SET_HEAD (current_function_decl);
	  fprintf (TB_OUT_FILE, "Current function declaration.\n");
	  break;

	case TB_HELP:
	  /* Display a help message.  */
	  {
	    int i;
	    fprintf (TB_OUT_FILE, "Possible commands are:\n\n");
	    for (i = 0; i < TB_UNUSED_COMMAND; i++)
	      {
		fprintf (TB_OUT_FILE, "%20s  -  %s\n", TB_COMMAND_TEXT (i), TB_COMMAND_HELP (i));
	      }
	  }
	  break;

	case TB_VERBOSE:
	  if (TB_verbose == 0)
	    {
	      TB_verbose = 1;
	      fprintf (TB_OUT_FILE, "Verbose on.\n");
	    }
	  else
	    {
	      TB_verbose = 0;
	      fprintf (TB_OUT_FILE, "Verbose off.\n");
	    }
	  break;

	case TB_EXIT:
	case TB_QUIT:
	  /* Just exit from this function.  */
	  goto ret;

	default:
	  TB_NIY;
	}
    }

 ret:;
  htab_delete (TB_up_ht);
  return;
}


/* Search the first node in this BIND_EXPR.  */

static tree
TB_first_in_bind (tree node)
{
  tree t;

  if (node == NULL_TREE)
    return NULL_TREE;

  while ((t = TB_prev_expr (node)))
    node = t;

  return node;
}

/* Search the last node in this BIND_EXPR.  */

static tree
TB_last_in_bind (tree node)
{
  tree t;

  if (node == NULL_TREE)
    return NULL_TREE;

  while ((t = TB_next_expr (node)))
    node = t;

  return node;
}

/* Search the parent expression for this node.  */

static tree
TB_up_expr (tree node)
{
  tree res;
  if (node == NULL_TREE)
    return NULL_TREE;

  res = (tree) htab_find (TB_up_ht, node);
  return res;
}

/* Search the previous expression in this BIND_EXPR.  */

static tree
TB_prev_expr (tree node)
{
  node = TB_current_chain_node (node);

  if (node == NULL_TREE)
    return NULL_TREE;

  node = TB_up_expr (node);
  if (node && TREE_CODE (node) == COMPOUND_EXPR)
    return node;
  else
    return NULL_TREE;
}

/* Search the next expression in this BIND_EXPR.  */

static tree
TB_next_expr (tree node)
{
  node = TB_current_chain_node (node);

  if (node == NULL_TREE)
    return NULL_TREE;

  node = TREE_OPERAND (node, 1);
  return node;
}

static tree
TB_current_chain_node (tree node)
{
  if (node == NULL_TREE)
    return NULL_TREE;

  if (TREE_CODE (node) == COMPOUND_EXPR)
    return node;

  node = TB_up_expr (node);
  if (node)
    {
      if (TREE_CODE (node) == COMPOUND_EXPR)
	return node;

      node = TB_up_expr (node);
      if (TREE_CODE (node) == COMPOUND_EXPR)
	return node;
    }

  return NULL_TREE;
}

/* For each node store in its children nodes that the current node is their
   parent.  This function is used by walk_tree.  */

static tree
store_child_info (tree *tp, int *walk_subtrees ATTRIBUTE_UNUSED,
		  void *data ATTRIBUTE_UNUSED)
{
  tree node;
  void **slot;

  node = *tp;

  /* 'node' is the parent of 'TREE_OPERAND (node, *)'.  */
  if (EXPRESSION_CLASS_P (node))
    {

#define STORE_CHILD(N) do {                                                \
  tree op = TREE_OPERAND (node, N);                                        \
  slot = htab_find_slot (TB_up_ht, op, INSERT);                               \
  *slot = (void *) node;                                                   \
} while (0)

      switch (TREE_CODE_LENGTH (TREE_CODE (node)))
	{
	case 4:
	  STORE_CHILD (0);
	  STORE_CHILD (1);
	  STORE_CHILD (2);
	  STORE_CHILD (3);
	  break;

	case 3:
	  STORE_CHILD (0);
	  STORE_CHILD (1);
	  STORE_CHILD (2);
	  break;

	case 2:
	  STORE_CHILD (0);
	  STORE_CHILD (1);
	  break;

	case 1:
	  STORE_CHILD (0);
	  break;

	case 0:
	default:
	  /* No children: nothing to do.  */
	  break;
	}
#undef STORE_CHILD
    }

  /* Never stop walk_tree.  */
  return NULL_TREE;
}

/* Function used in TB_up_ht.  */

static int
TB_parent_eq (const void *p1, const void *p2)
{
  tree node, parent;
  node = (tree) p2;
  parent = (tree) p1;

  if (p1 == NULL || p2 == NULL)
    return 0;

  if (EXPRESSION_CLASS_P (parent))
    {

#define TEST_CHILD(N) do {               \
  if (node == TREE_OPERAND (parent, N))  \
    return 1;                            \
} while (0)

    switch (TREE_CODE_LENGTH (TREE_CODE (parent)))
      {
      case 4:
	TEST_CHILD (0);
	TEST_CHILD (1);
	TEST_CHILD (2);
	TEST_CHILD (3);
	break;

      case 3:
	TEST_CHILD (0);
	TEST_CHILD (1);
	TEST_CHILD (2);
	break;

      case 2:
	TEST_CHILD (0);
	TEST_CHILD (1);
	break;

      case 1:
	TEST_CHILD (0);
	break;

      case 0:
      default:
	/* No children: nothing to do.  */
	break;
      }
#undef TEST_CHILD
    }

  return 0;
}

/* Update information about upper expressions in the hash table.  */

static void
TB_update_up (tree node)
{
  while (node)
    {
      walk_tree (&node, store_child_info, NULL, NULL);

      /* Walk function's body.  */
      if (TREE_CODE (node) == FUNCTION_DECL)
        if (DECL_SAVED_TREE (node))
          walk_tree (&DECL_SAVED_TREE (node), store_child_info, NULL, NULL);

      /* Walk rest of the chain.  */
      node = TREE_CHAIN (node);
    }
  fprintf (TB_OUT_FILE, "Up/prev expressions updated.\n");
}

/* Parse the input string for determining the command the user asked for.  */

static TB_CODE
TB_get_command (char *input)
{
  unsigned int mn, size_tok;
  int comp;
  char *space;

  space = strchr (input, ' ');
  if (space != NULL)
    size_tok = strlen (input) - strlen (space);
  else
    size_tok = strlen (input) - 1;

  for (mn = 0; mn < TB_UNUSED_COMMAND; mn++)
    {
      if (size_tok != TB_COMMAND_LEN (mn))
	continue;

      comp = memcmp (input, TB_COMMAND_TEXT (mn), TB_COMMAND_LEN (mn));
      if (comp == 0)
	/* Here we just determined the command.  If this command takes
	   an argument, then the argument is determined later.  */
	return TB_COMMAND_CODE (mn);
    }

  /* Not a valid command.  */
  return TB_UNUSED_COMMAND;
}

/* Parse the input string for determining the tree code.  */

static enum tree_code
TB_get_tree_code (char *input)
{
  unsigned int mn, size_tok;
  int comp;
  char *space;

  space = strchr (input, ' ');
  if (space != NULL)
    size_tok = strlen (input) - strlen (space);
  else
    size_tok = strlen (input) - 1;

  for (mn = 0; mn < LAST_AND_UNUSED_TREE_CODE; mn++)
    {
      if (size_tok != TB_TREE_CODE_LEN (mn))
	continue;

      comp = memcmp (input, TB_TREE_CODE_TEXT (mn), TB_TREE_CODE_LEN (mn));
      if (comp == 0)
	{
	  fprintf (TB_OUT_FILE, "%s\n", TB_TREE_CODE_TEXT (mn));
	  return TB_TREE_CODE (mn);
	}
    }

  /* This isn't a valid code.  */
  return LAST_AND_UNUSED_TREE_CODE;
}

/* Find a node with a given code.  This function is used as an argument to
   walk_tree.  */

static tree
find_node_with_code (tree *tp, int *walk_subtrees ATTRIBUTE_UNUSED,
		     void *data)
{
  enum tree_code *code;
  code = (enum tree_code *) data;
  if (*code == TREE_CODE (*tp))
    return *tp;

  return NULL_TREE;
}

/* Returns a pointer to the last visited node.  */

static tree
TB_history_prev (void)
{
  if (TB_history_stack)
    {
      TB_history_stack = TREE_CHAIN (TB_history_stack);
      if (TB_history_stack)
	return TREE_VALUE (TB_history_stack);
    }
  return NULL_TREE;
}

/* Read up to (and including) a '\n' from STREAM into *LINEPTR
   (and null-terminate it). *LINEPTR is a pointer returned from malloc
   (or NULL), pointing to *N characters of space.  It is realloc'd as
   necessary.  Returns the number of characters read (not including the
   null terminator), or -1 on error or EOF.
   This function comes from sed (and is supposed to be a portable version
   of getline).  */

static long
TB_getline (char **lineptr, long *n, FILE *stream)
{
  char *line, *p;
  long size, copy;

  if (lineptr == NULL || n == NULL)
    {
      errno = EINVAL;
      return -1;
    }

  if (ferror (stream))
    return -1;

  /* Make sure we have a line buffer to start with.  */
  if (*lineptr == NULL || *n < 2) /* !seen and no buf yet need 2 chars.  */
    {
#ifndef MAX_CANON
#define MAX_CANON       256
#endif
      line = (char *) xrealloc (*lineptr, MAX_CANON);
      if (line == NULL)
        return -1;
      *lineptr = line;
      *n = MAX_CANON;
    }

  line = *lineptr;
  size = *n;

  copy = size;
  p = line;

  while (1)
    {
      long len;

      while (--copy > 0)
        {
          register int c = getc (stream);
          if (c == EOF)
            goto lose;
          else if ((*p++ = c) == '\n')
            goto win;
        }

      /* Need to enlarge the line buffer.  */
      len = p - line;
      size *= 2;
      line = (char *) xrealloc (line, size);
      if (line == NULL)
        goto lose;
      *lineptr = line;
      *n = size;
      p = line + len;
      copy = size - len;
    }

 lose:
  if (p == *lineptr)
    return -1;

  /* Return a partial line since we got an error in the middle.  */
 win:
#if defined(WIN32) || defined(_WIN32) || defined(__CYGWIN__) || defined(MSDOS)
  if (p - 2 >= *lineptr && p[-2] == '\r')
    p[-2] = p[-1], --p;
#endif
  *p = '\0';
  return p - *lineptr;
}
