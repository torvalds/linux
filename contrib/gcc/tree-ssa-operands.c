/* SSA operands management for trees.
   Copyright (C) 2003, 2004, 2005, 2006 Free Software Foundation, Inc.

This file is part of GCC.

GCC is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2, or (at your option)
any later version.

GCC is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with GCC; see the file COPYING.  If not, write to
the Free Software Foundation, 51 Franklin Street, Fifth Floor,
Boston, MA 02110-1301, USA.  */

#include "config.h"
#include "system.h"
#include "coretypes.h"
#include "tm.h"
#include "tree.h"
#include "flags.h"
#include "function.h"
#include "diagnostic.h"
#include "tree-flow.h"
#include "tree-inline.h"
#include "tree-pass.h"
#include "ggc.h"
#include "timevar.h"
#include "toplev.h"
#include "langhooks.h"
#include "ipa-reference.h"

/* This file contains the code required to manage the operands cache of the 
   SSA optimizer.  For every stmt, we maintain an operand cache in the stmt 
   annotation.  This cache contains operands that will be of interest to 
   optimizers and other passes wishing to manipulate the IL. 

   The operand type are broken up into REAL and VIRTUAL operands.  The real 
   operands are represented as pointers into the stmt's operand tree.  Thus 
   any manipulation of the real operands will be reflected in the actual tree.
   Virtual operands are represented solely in the cache, although the base 
   variable for the SSA_NAME may, or may not occur in the stmt's tree.  
   Manipulation of the virtual operands will not be reflected in the stmt tree.

   The routines in this file are concerned with creating this operand cache 
   from a stmt tree.

   The operand tree is the parsed by the various get_* routines which look 
   through the stmt tree for the occurrence of operands which may be of 
   interest, and calls are made to the append_* routines whenever one is 
   found.  There are 5 of these routines, each representing one of the 
   5 types of operands. Defs, Uses, Virtual Uses, Virtual May Defs, and 
   Virtual Must Defs.

   The append_* routines check for duplication, and simply keep a list of 
   unique objects for each operand type in the build_* extendable vectors.

   Once the stmt tree is completely parsed, the finalize_ssa_operands() 
   routine is called, which proceeds to perform the finalization routine 
   on each of the 5 operand vectors which have been built up.

   If the stmt had a previous operand cache, the finalization routines 
   attempt to match up the new operands with the old ones.  If it's a perfect 
   match, the old vector is simply reused.  If it isn't a perfect match, then 
   a new vector is created and the new operands are placed there.  For 
   virtual operands, if the previous cache had SSA_NAME version of a 
   variable, and that same variable occurs in the same operands cache, then 
   the new cache vector will also get the same SSA_NAME.

  i.e., if a stmt had a VUSE of 'a_5', and 'a' occurs in the new operand 
  vector for VUSE, then the new vector will also be modified such that 
  it contains 'a_5' rather than 'a'.  */

/* Flags to describe operand properties in helpers.  */

/* By default, operands are loaded.  */
#define opf_none	0

/* Operand is the target of an assignment expression or a 
   call-clobbered variable.  */
#define opf_is_def 	(1 << 0)

/* Operand is the target of an assignment expression.  */
#define opf_kill_def 	(1 << 1)

/* No virtual operands should be created in the expression.  This is used
   when traversing ADDR_EXPR nodes which have different semantics than
   other expressions.  Inside an ADDR_EXPR node, the only operands that we
   need to consider are indices into arrays.  For instance, &a.b[i] should
   generate a USE of 'i' but it should not generate a VUSE for 'a' nor a
   VUSE for 'b'.  */
#define opf_no_vops 	(1 << 2)

/* Operand is a "non-specific" kill for call-clobbers and such.  This
   is used to distinguish "reset the world" events from explicit
   MODIFY_EXPRs.  */
#define opf_non_specific  (1 << 3)

/* Array for building all the def operands.  */
static VEC(tree,heap) *build_defs;

/* Array for building all the use operands.  */
static VEC(tree,heap) *build_uses;

/* Array for building all the V_MAY_DEF operands.  */
static VEC(tree,heap) *build_v_may_defs;

/* Array for building all the VUSE operands.  */
static VEC(tree,heap) *build_vuses;

/* Array for building all the V_MUST_DEF operands.  */
static VEC(tree,heap) *build_v_must_defs;

/* These arrays are the cached operand vectors for call clobbered calls.  */
static bool ops_active = false;

static GTY (()) struct ssa_operand_memory_d *operand_memory = NULL;
static unsigned operand_memory_index;

static void get_expr_operands (tree, tree *, int);

static def_optype_p free_defs = NULL;
static use_optype_p free_uses = NULL;
static vuse_optype_p free_vuses = NULL;
static maydef_optype_p free_maydefs = NULL;
static mustdef_optype_p free_mustdefs = NULL;

/* Allocates operand OP of given TYPE from the appropriate free list,
   or of the new value if the list is empty.  */

#define ALLOC_OPTYPE(OP, TYPE)				\
  do							\
    {							\
      TYPE##_optype_p ret = free_##TYPE##s;		\
      if (ret)						\
	free_##TYPE##s = ret->next;			\
      else						\
	ret = ssa_operand_alloc (sizeof (*ret));	\
      (OP) = ret;					\
    } while (0) 

/* Return the DECL_UID of the base variable of T.  */

static inline unsigned
get_name_decl (tree t)
{
  if (TREE_CODE (t) != SSA_NAME)
    return DECL_UID (t);
  else
    return DECL_UID (SSA_NAME_VAR (t));
}


/* Comparison function for qsort used in operand_build_sort_virtual.  */

static int
operand_build_cmp (const void *p, const void *q)
{
  tree e1 = *((const tree *)p);
  tree e2 = *((const tree *)q);
  unsigned int u1,u2;

  u1 = get_name_decl (e1);
  u2 = get_name_decl (e2);

  /* We want to sort in ascending order.  They can never be equal.  */
#ifdef ENABLE_CHECKING
  gcc_assert (u1 != u2);
#endif
  return (u1 > u2 ? 1 : -1);
}


/* Sort the virtual operands in LIST from lowest DECL_UID to highest.  */

static inline void
operand_build_sort_virtual (VEC(tree,heap) *list)
{
  int num = VEC_length (tree, list);

  if (num < 2)
    return;

  if (num == 2)
    {
      if (get_name_decl (VEC_index (tree, list, 0)) 
	  > get_name_decl (VEC_index (tree, list, 1)))
	{  
	  /* Swap elements if in the wrong order.  */
	  tree tmp = VEC_index (tree, list, 0);
	  VEC_replace (tree, list, 0, VEC_index (tree, list, 1));
	  VEC_replace (tree, list, 1, tmp);
	}
      return;
    }

  /* There are 3 or more elements, call qsort.  */
  qsort (VEC_address (tree, list), 
	 VEC_length (tree, list), 
	 sizeof (tree),
	 operand_build_cmp);
}


/*  Return true if the SSA operands cache is active.  */

bool
ssa_operands_active (void)
{
  return ops_active;
}


/* Structure storing statistics on how many call clobbers we have, and
   how many where avoided.  */

static struct 
{
  /* Number of call-clobbered ops we attempt to add to calls in
     add_call_clobber_ops.  */
  unsigned int clobbered_vars;

  /* Number of write-clobbers (V_MAY_DEFs) avoided by using
     not_written information.  */
  unsigned int static_write_clobbers_avoided;

  /* Number of reads (VUSEs) avoided by using not_read information.  */
  unsigned int static_read_clobbers_avoided;
  
  /* Number of write-clobbers avoided because the variable can't escape to
     this call.  */
  unsigned int unescapable_clobbers_avoided;

  /* Number of read-only uses we attempt to add to calls in
     add_call_read_ops.  */
  unsigned int readonly_clobbers;

  /* Number of read-only uses we avoid using not_read information.  */
  unsigned int static_readonly_clobbers_avoided;
} clobber_stats;
  

/* Initialize the operand cache routines.  */

void
init_ssa_operands (void)
{
  build_defs = VEC_alloc (tree, heap, 5);
  build_uses = VEC_alloc (tree, heap, 10);
  build_vuses = VEC_alloc (tree, heap, 25);
  build_v_may_defs = VEC_alloc (tree, heap, 25);
  build_v_must_defs = VEC_alloc (tree, heap, 25);

  gcc_assert (operand_memory == NULL);
  operand_memory_index = SSA_OPERAND_MEMORY_SIZE;
  ops_active = true;
  memset (&clobber_stats, 0, sizeof (clobber_stats));
}


/* Dispose of anything required by the operand routines.  */

void
fini_ssa_operands (void)
{
  struct ssa_operand_memory_d *ptr;
  VEC_free (tree, heap, build_defs);
  VEC_free (tree, heap, build_uses);
  VEC_free (tree, heap, build_v_must_defs);
  VEC_free (tree, heap, build_v_may_defs);
  VEC_free (tree, heap, build_vuses);
  free_defs = NULL;
  free_uses = NULL;
  free_vuses = NULL;
  free_maydefs = NULL;
  free_mustdefs = NULL;
  while ((ptr = operand_memory) != NULL)
    {
      operand_memory = operand_memory->next;
      ggc_free (ptr);
    }

  ops_active = false;
  
  if (dump_file && (dump_flags & TDF_STATS))
    {
      fprintf (dump_file, "Original clobbered vars:%d\n",
	       clobber_stats.clobbered_vars);
      fprintf (dump_file, "Static write clobbers avoided:%d\n",
	       clobber_stats.static_write_clobbers_avoided);
      fprintf (dump_file, "Static read clobbers avoided:%d\n",
	       clobber_stats.static_read_clobbers_avoided);
      fprintf (dump_file, "Unescapable clobbers avoided:%d\n",
	       clobber_stats.unescapable_clobbers_avoided);
      fprintf (dump_file, "Original read-only clobbers:%d\n",
	       clobber_stats.readonly_clobbers);
      fprintf (dump_file, "Static read-only clobbers avoided:%d\n",
	       clobber_stats.static_readonly_clobbers_avoided);
    }
}


/* Return memory for operands of SIZE chunks.  */
                                                                              
static inline void *
ssa_operand_alloc (unsigned size)
{
  char *ptr;
  if (operand_memory_index + size >= SSA_OPERAND_MEMORY_SIZE)
    {
      struct ssa_operand_memory_d *ptr;
      ptr = GGC_NEW (struct ssa_operand_memory_d);
      ptr->next = operand_memory;
      operand_memory = ptr;
      operand_memory_index = 0;
    }
  ptr = &(operand_memory->mem[operand_memory_index]);
  operand_memory_index += size;
  return ptr;
}



/* This routine makes sure that PTR is in an immediate use list, and makes
   sure the stmt pointer is set to the current stmt.  */

static inline void
set_virtual_use_link (use_operand_p ptr, tree stmt)
{
  /*  fold_stmt may have changed the stmt pointers.  */
  if (ptr->stmt != stmt)
    ptr->stmt = stmt;

  /* If this use isn't in a list, add it to the correct list.  */
  if (!ptr->prev)
    link_imm_use (ptr, *(ptr->use));
}

/* Appends ELT after TO, and moves the TO pointer to ELT.  */

#define APPEND_OP_AFTER(ELT, TO)	\
  do					\
    {					\
      (TO)->next = (ELT);		\
      (TO) = (ELT);			\
    } while (0)

/* Appends head of list FROM after TO, and move both pointers
   to their successors.  */

#define MOVE_HEAD_AFTER(FROM, TO)	\
  do					\
    {					\
      APPEND_OP_AFTER (FROM, TO);	\
      (FROM) = (FROM)->next;		\
    } while (0)

/* Moves OP to appropriate freelist.  OP is set to its successor.  */

#define MOVE_HEAD_TO_FREELIST(OP, TYPE)			\
  do							\
    {							\
      TYPE##_optype_p next = (OP)->next;		\
      (OP)->next = free_##TYPE##s;			\
      free_##TYPE##s = (OP);				\
      (OP) = next;					\
    } while (0)

/* Initializes immediate use at USE_PTR to value VAL, and links it to the list
   of immediate uses.  STMT is the current statement.  */

#define INITIALIZE_USE(USE_PTR, VAL, STMT)		\
  do							\
    {							\
      (USE_PTR)->use = (VAL);				\
      link_imm_use_stmt ((USE_PTR), *(VAL), (STMT));	\
    } while (0)

/* Adds OP to the list of defs after LAST, and moves
   LAST to the new element.  */

static inline void
add_def_op (tree *op, def_optype_p *last)
{
  def_optype_p new;

  ALLOC_OPTYPE (new, def);
  DEF_OP_PTR (new) = op;
  APPEND_OP_AFTER (new, *last);  
}

/* Adds OP to the list of uses of statement STMT after LAST, and moves
   LAST to the new element.  */

static inline void
add_use_op (tree stmt, tree *op, use_optype_p *last)
{
  use_optype_p new;

  ALLOC_OPTYPE (new, use);
  INITIALIZE_USE (USE_OP_PTR (new), op, stmt);
  APPEND_OP_AFTER (new, *last);  
}

/* Adds OP to the list of vuses of statement STMT after LAST, and moves
   LAST to the new element.  */

static inline void
add_vuse_op (tree stmt, tree op, vuse_optype_p *last)
{
  vuse_optype_p new;

  ALLOC_OPTYPE (new, vuse);
  VUSE_OP (new) = op;
  INITIALIZE_USE (VUSE_OP_PTR (new), &VUSE_OP (new), stmt);
  APPEND_OP_AFTER (new, *last);  
}

/* Adds OP to the list of maydefs of statement STMT after LAST, and moves
   LAST to the new element.  */

static inline void
add_maydef_op (tree stmt, tree op, maydef_optype_p *last)
{
  maydef_optype_p new;

  ALLOC_OPTYPE (new, maydef);
  MAYDEF_RESULT (new) = op;
  MAYDEF_OP (new) = op;
  INITIALIZE_USE (MAYDEF_OP_PTR (new), &MAYDEF_OP (new), stmt);
  APPEND_OP_AFTER (new, *last);  
}

/* Adds OP to the list of mustdefs of statement STMT after LAST, and moves
   LAST to the new element.  */

static inline void
add_mustdef_op (tree stmt, tree op, mustdef_optype_p *last)
{
  mustdef_optype_p new;

  ALLOC_OPTYPE (new, mustdef);
  MUSTDEF_RESULT (new) = op;
  MUSTDEF_KILL (new) = op;
  INITIALIZE_USE (MUSTDEF_KILL_PTR (new), &MUSTDEF_KILL (new), stmt);
  APPEND_OP_AFTER (new, *last);
}

/* Takes elements from build_defs and turns them into def operands of STMT.
   TODO -- Given that def operands list is not necessarily sorted, merging
	   the operands this way does not make much sense.
	-- Make build_defs VEC of tree *.  */

static inline void
finalize_ssa_def_ops (tree stmt)
{
  unsigned new_i;
  struct def_optype_d new_list;
  def_optype_p old_ops, last;
  tree *old_base;

  new_list.next = NULL;
  last = &new_list;

  old_ops = DEF_OPS (stmt);

  new_i = 0;
  while (old_ops && new_i < VEC_length (tree, build_defs))
    {
      tree *new_base = (tree *) VEC_index (tree, build_defs, new_i);
      old_base = DEF_OP_PTR (old_ops);

      if (old_base == new_base)
        {
	  /* if variables are the same, reuse this node.  */
	  MOVE_HEAD_AFTER (old_ops, last);
	  new_i++;
	}
      else if (old_base < new_base)
	{
	  /* if old is less than new, old goes to the free list.  */
	  MOVE_HEAD_TO_FREELIST (old_ops, def);
	}
      else
	{
	  /* This is a new operand.  */
	  add_def_op (new_base, &last);
	  new_i++;
	}
    }

  /* If there is anything remaining in the build_defs list, simply emit it.  */
  for ( ; new_i < VEC_length (tree, build_defs); new_i++)
    add_def_op ((tree *) VEC_index (tree, build_defs, new_i), &last);

  last->next = NULL;

  /* If there is anything in the old list, free it.  */
  if (old_ops)
    {
      old_ops->next = free_defs;
      free_defs = old_ops;
    }

  /* Now set the stmt's operands.  */
  DEF_OPS (stmt) = new_list.next;

#ifdef ENABLE_CHECKING
  {
    def_optype_p ptr;
    unsigned x = 0;
    for (ptr = DEF_OPS (stmt); ptr; ptr = ptr->next)
      x++;

    gcc_assert (x == VEC_length (tree, build_defs));
  }
#endif
}

/* This routine will create stmt operands for STMT from the def build list.  */

static void
finalize_ssa_defs (tree stmt)
{
  unsigned int num = VEC_length (tree, build_defs);

  /* There should only be a single real definition per assignment.  */
  gcc_assert ((stmt && TREE_CODE (stmt) != MODIFY_EXPR) || num <= 1);

  /* If there is an old list, often the new list is identical, or close, so
     find the elements at the beginning that are the same as the vector.  */
  finalize_ssa_def_ops (stmt);
  VEC_truncate (tree, build_defs, 0);
}

/* Takes elements from build_uses and turns them into use operands of STMT.
   TODO -- Make build_uses VEC of tree *.  */

static inline void
finalize_ssa_use_ops (tree stmt)
{
  unsigned new_i;
  struct use_optype_d new_list;
  use_optype_p old_ops, ptr, last;

  new_list.next = NULL;
  last = &new_list;

  old_ops = USE_OPS (stmt);

  /* If there is anything in the old list, free it.  */
  if (old_ops)
    {
      for (ptr = old_ops; ptr; ptr = ptr->next)
	delink_imm_use (USE_OP_PTR (ptr));
      old_ops->next = free_uses;
      free_uses = old_ops;
    }

  /* Now create nodes for all the new nodes.  */
  for (new_i = 0; new_i < VEC_length (tree, build_uses); new_i++)
    add_use_op (stmt, (tree *) VEC_index (tree, build_uses, new_i), &last);

  last->next = NULL;

  /* Now set the stmt's operands.  */
  USE_OPS (stmt) = new_list.next;

#ifdef ENABLE_CHECKING
  {
    unsigned x = 0;
    for (ptr = USE_OPS (stmt); ptr; ptr = ptr->next)
      x++;

    gcc_assert (x == VEC_length (tree, build_uses));
  }
#endif
}

/* Return a new use operand vector for STMT, comparing to OLD_OPS_P.  */
                                                                              
static void
finalize_ssa_uses (tree stmt)
{
#ifdef ENABLE_CHECKING
  {
    unsigned x;
    unsigned num = VEC_length (tree, build_uses);

    /* If the pointer to the operand is the statement itself, something is
       wrong.  It means that we are pointing to a local variable (the 
       initial call to update_stmt_operands does not pass a pointer to a 
       statement).  */
    for (x = 0; x < num; x++)
      gcc_assert (*((tree *)VEC_index (tree, build_uses, x)) != stmt);
  }
#endif
  finalize_ssa_use_ops (stmt);
  VEC_truncate (tree, build_uses, 0);
}


/* Takes elements from build_v_may_defs and turns them into maydef operands of
   STMT.  */

static inline void
finalize_ssa_v_may_def_ops (tree stmt)
{
  unsigned new_i;
  struct maydef_optype_d new_list;
  maydef_optype_p old_ops, ptr, last;
  tree act;
  unsigned old_base, new_base;

  new_list.next = NULL;
  last = &new_list;

  old_ops = MAYDEF_OPS (stmt);

  new_i = 0;
  while (old_ops && new_i < VEC_length (tree, build_v_may_defs))
    {
      act = VEC_index (tree, build_v_may_defs, new_i);
      new_base = get_name_decl (act);
      old_base = get_name_decl (MAYDEF_OP (old_ops));

      if (old_base == new_base)
        {
	  /* if variables are the same, reuse this node.  */
	  MOVE_HEAD_AFTER (old_ops, last);
	  set_virtual_use_link (MAYDEF_OP_PTR (last), stmt);
	  new_i++;
	}
      else if (old_base < new_base)
	{
	  /* if old is less than new, old goes to the free list.  */
	  delink_imm_use (MAYDEF_OP_PTR (old_ops));
	  MOVE_HEAD_TO_FREELIST (old_ops, maydef);
	}
      else
	{
	  /* This is a new operand.  */
	  add_maydef_op (stmt, act, &last);
	  new_i++;
	}
    }

  /* If there is anything remaining in the build_v_may_defs list, simply emit it.  */
  for ( ; new_i < VEC_length (tree, build_v_may_defs); new_i++)
    add_maydef_op (stmt, VEC_index (tree, build_v_may_defs, new_i), &last);

  last->next = NULL;

  /* If there is anything in the old list, free it.  */
  if (old_ops)
    {
      for (ptr = old_ops; ptr; ptr = ptr->next)
	delink_imm_use (MAYDEF_OP_PTR (ptr));
      old_ops->next = free_maydefs;
      free_maydefs = old_ops;
    }

  /* Now set the stmt's operands.  */
  MAYDEF_OPS (stmt) = new_list.next;

#ifdef ENABLE_CHECKING
  {
    unsigned x = 0;
    for (ptr = MAYDEF_OPS (stmt); ptr; ptr = ptr->next)
      x++;

    gcc_assert (x == VEC_length (tree, build_v_may_defs));
  }
#endif
}

static void
finalize_ssa_v_may_defs (tree stmt)
{
  finalize_ssa_v_may_def_ops (stmt);
}
                                                                               

/* Clear the in_list bits and empty the build array for V_MAY_DEFs.  */

static inline void
cleanup_v_may_defs (void)
{
  unsigned x, num;
  num = VEC_length (tree, build_v_may_defs);

  for (x = 0; x < num; x++)
    {
      tree t = VEC_index (tree, build_v_may_defs, x);
      if (TREE_CODE (t) != SSA_NAME)
	{
	  var_ann_t ann = var_ann (t);
	  ann->in_v_may_def_list = 0;
	}
    }
  VEC_truncate (tree, build_v_may_defs, 0);
}                                                                             


/* Takes elements from build_vuses and turns them into vuse operands of
   STMT.  */

static inline void
finalize_ssa_vuse_ops (tree stmt)
{
  unsigned new_i;
  struct vuse_optype_d new_list;
  vuse_optype_p old_ops, ptr, last;
  tree act;
  unsigned old_base, new_base;

  new_list.next = NULL;
  last = &new_list;

  old_ops = VUSE_OPS (stmt);

  new_i = 0;
  while (old_ops && new_i < VEC_length (tree, build_vuses))
    {
      act = VEC_index (tree, build_vuses, new_i);
      new_base = get_name_decl (act);
      old_base = get_name_decl (VUSE_OP (old_ops));

      if (old_base == new_base)
        {
	  /* if variables are the same, reuse this node.  */
	  MOVE_HEAD_AFTER (old_ops, last);
	  set_virtual_use_link (VUSE_OP_PTR (last), stmt);
	  new_i++;
	}
      else if (old_base < new_base)
	{
	  /* if old is less than new, old goes to the free list.  */
	  delink_imm_use (USE_OP_PTR (old_ops));
	  MOVE_HEAD_TO_FREELIST (old_ops, vuse);
	}
      else
	{
	  /* This is a new operand.  */
	  add_vuse_op (stmt, act, &last);
	  new_i++;
	}
    }

  /* If there is anything remaining in the build_vuses list, simply emit it.  */
  for ( ; new_i < VEC_length (tree, build_vuses); new_i++)
    add_vuse_op (stmt, VEC_index (tree, build_vuses, new_i), &last);

  last->next = NULL;

  /* If there is anything in the old list, free it.  */
  if (old_ops)
    {
      for (ptr = old_ops; ptr; ptr = ptr->next)
	delink_imm_use (VUSE_OP_PTR (ptr));
      old_ops->next = free_vuses;
      free_vuses = old_ops;
    }

  /* Now set the stmt's operands.  */
  VUSE_OPS (stmt) = new_list.next;

#ifdef ENABLE_CHECKING
  {
    unsigned x = 0;
    for (ptr = VUSE_OPS (stmt); ptr; ptr = ptr->next)
      x++;

    gcc_assert (x == VEC_length (tree, build_vuses));
  }
#endif
}
                                                                              
/* Return a new VUSE operand vector, comparing to OLD_OPS_P.  */
                                                                              
static void
finalize_ssa_vuses (tree stmt)
{
  unsigned num, num_v_may_defs;
  unsigned vuse_index;

  /* Remove superfluous VUSE operands.  If the statement already has a
     V_MAY_DEF operation for a variable 'a', then a VUSE for 'a' is
     not needed because V_MAY_DEFs imply a VUSE of the variable.  For
     instance, suppose that variable 'a' is aliased:

	      # VUSE <a_2>
	      # a_3 = V_MAY_DEF <a_2>
	      a = a + 1;

     The VUSE <a_2> is superfluous because it is implied by the
     V_MAY_DEF operation.  */
  num = VEC_length (tree, build_vuses);
  num_v_may_defs = VEC_length (tree, build_v_may_defs);

  if (num > 0 && num_v_may_defs > 0)
    {
      for (vuse_index = 0; vuse_index < VEC_length (tree, build_vuses); )
        {
	  tree vuse;
	  vuse = VEC_index (tree, build_vuses, vuse_index);
	  if (TREE_CODE (vuse) != SSA_NAME)
	    {
	      var_ann_t ann = var_ann (vuse);
	      ann->in_vuse_list = 0;
	      if (ann->in_v_may_def_list)
	        {
		  VEC_ordered_remove (tree, build_vuses, vuse_index);
		  continue;
		}
	    }
	  vuse_index++;
	}
    }
  else
    {
      /* Clear out the in_list bits.  */
      for (vuse_index = 0;
	  vuse_index < VEC_length (tree, build_vuses);
	  vuse_index++)
	{
	  tree t = VEC_index (tree, build_vuses, vuse_index);
	  if (TREE_CODE (t) != SSA_NAME)
	    {
	      var_ann_t ann = var_ann (t);
	      ann->in_vuse_list = 0;
	    }
	}
    }

  finalize_ssa_vuse_ops (stmt);

  /* The V_MAY_DEF build vector wasn't cleaned up because we needed it.  */
  cleanup_v_may_defs ();
                                                                              
  /* Free the VUSEs build vector.  */
  VEC_truncate (tree, build_vuses, 0);

}

/* Takes elements from build_v_must_defs and turns them into mustdef operands of
   STMT.  */

static inline void
finalize_ssa_v_must_def_ops (tree stmt)
{
  unsigned new_i;
  struct mustdef_optype_d new_list;
  mustdef_optype_p old_ops, ptr, last;
  tree act;
  unsigned old_base, new_base;

  new_list.next = NULL;
  last = &new_list;

  old_ops = MUSTDEF_OPS (stmt);

  new_i = 0;
  while (old_ops && new_i < VEC_length (tree, build_v_must_defs))
    {
      act = VEC_index (tree, build_v_must_defs, new_i);
      new_base = get_name_decl (act);
      old_base = get_name_decl (MUSTDEF_KILL (old_ops));

      if (old_base == new_base)
        {
	  /* If variables are the same, reuse this node.  */
	  MOVE_HEAD_AFTER (old_ops, last);
	  set_virtual_use_link (MUSTDEF_KILL_PTR (last), stmt);
	  new_i++;
	}
      else if (old_base < new_base)
	{
	  /* If old is less than new, old goes to the free list.  */
	  delink_imm_use (MUSTDEF_KILL_PTR (old_ops));
	  MOVE_HEAD_TO_FREELIST (old_ops, mustdef);
	}
      else
	{
	  /* This is a new operand.  */
	  add_mustdef_op (stmt, act, &last);
	  new_i++;
	}
    }

  /* If there is anything remaining in the build_v_must_defs list, simply emit it.  */
  for ( ; new_i < VEC_length (tree, build_v_must_defs); new_i++)
    add_mustdef_op (stmt, VEC_index (tree, build_v_must_defs, new_i), &last);

  last->next = NULL;

  /* If there is anything in the old list, free it.  */
  if (old_ops)
    {
      for (ptr = old_ops; ptr; ptr = ptr->next)
	delink_imm_use (MUSTDEF_KILL_PTR (ptr));
      old_ops->next = free_mustdefs;
      free_mustdefs = old_ops;
    }

  /* Now set the stmt's operands.  */
  MUSTDEF_OPS (stmt) = new_list.next;

#ifdef ENABLE_CHECKING
  {
    unsigned x = 0;
    for (ptr = MUSTDEF_OPS (stmt); ptr; ptr = ptr->next)
      x++;

    gcc_assert (x == VEC_length (tree, build_v_must_defs));
  }
#endif
}

static void
finalize_ssa_v_must_defs (tree stmt)
{
  /* In the presence of subvars, there may be more than one V_MUST_DEF
     per statement (one for each subvar).  It is a bit expensive to
     verify that all must-defs in a statement belong to subvars if
     there is more than one must-def, so we don't do it.  Suffice to
     say, if you reach here without having subvars, and have num >1,
     you have hit a bug.  */
  finalize_ssa_v_must_def_ops (stmt);
  VEC_truncate (tree, build_v_must_defs, 0);
}


/* Finalize all the build vectors, fill the new ones into INFO.  */
                                                                              
static inline void
finalize_ssa_stmt_operands (tree stmt)
{
  finalize_ssa_defs (stmt);
  finalize_ssa_uses (stmt);
  finalize_ssa_v_must_defs (stmt);
  finalize_ssa_v_may_defs (stmt);
  finalize_ssa_vuses (stmt);
}


/* Start the process of building up operands vectors in INFO.  */

static inline void
start_ssa_stmt_operands (void)
{
  gcc_assert (VEC_length (tree, build_defs) == 0);
  gcc_assert (VEC_length (tree, build_uses) == 0);
  gcc_assert (VEC_length (tree, build_vuses) == 0);
  gcc_assert (VEC_length (tree, build_v_may_defs) == 0);
  gcc_assert (VEC_length (tree, build_v_must_defs) == 0);
}


/* Add DEF_P to the list of pointers to operands.  */

static inline void
append_def (tree *def_p)
{
  VEC_safe_push (tree, heap, build_defs, (tree)def_p);
}


/* Add USE_P to the list of pointers to operands.  */

static inline void
append_use (tree *use_p)
{
  VEC_safe_push (tree, heap, build_uses, (tree)use_p);
}


/* Add a new virtual may def for variable VAR to the build array.  */

static inline void
append_v_may_def (tree var)
{
  if (TREE_CODE (var) != SSA_NAME)
    {
      var_ann_t ann = get_var_ann (var);

      /* Don't allow duplicate entries.  */
      if (ann->in_v_may_def_list)
	return;
      ann->in_v_may_def_list = 1;
    }

  VEC_safe_push (tree, heap, build_v_may_defs, (tree)var);
}


/* Add VAR to the list of virtual uses.  */

static inline void
append_vuse (tree var)
{
  /* Don't allow duplicate entries.  */
  if (TREE_CODE (var) != SSA_NAME)
    {
      var_ann_t ann = get_var_ann (var);

      if (ann->in_vuse_list || ann->in_v_may_def_list)
        return;
      ann->in_vuse_list = 1;
    }

  VEC_safe_push (tree, heap, build_vuses, (tree)var);
}


/* Add VAR to the list of virtual must definitions for INFO.  */

static inline void
append_v_must_def (tree var)
{
  unsigned i;

  /* Don't allow duplicate entries.  */
  for (i = 0; i < VEC_length (tree, build_v_must_defs); i++)
    if (var == VEC_index (tree, build_v_must_defs, i))
      return;

  VEC_safe_push (tree, heap, build_v_must_defs, (tree)var);
}


/* REF is a tree that contains the entire pointer dereference
   expression, if available, or NULL otherwise.  ALIAS is the variable
   we are asking if REF can access.  OFFSET and SIZE come from the
   memory access expression that generated this virtual operand.  */

static bool
access_can_touch_variable (tree ref, tree alias, HOST_WIDE_INT offset,
			   HOST_WIDE_INT size)
{  
  bool offsetgtz = offset > 0;
  unsigned HOST_WIDE_INT uoffset = (unsigned HOST_WIDE_INT) offset;
  tree base = ref ? get_base_address (ref) : NULL;

  /* If ALIAS is .GLOBAL_VAR then the memory reference REF must be
     using a call-clobbered memory tag.  By definition, call-clobbered
     memory tags can always touch .GLOBAL_VAR.  */
  if (alias == global_var)
    return true;

  /* We cannot prune nonlocal aliases because they are not type
     specific.  */
  if (alias == nonlocal_all)
    return true;

  /* If ALIAS is an SFT, it can't be touched if the offset     
     and size of the access is not overlapping with the SFT offset and
     size.  This is only true if we are accessing through a pointer
     to a type that is the same as SFT_PARENT_VAR.  Otherwise, we may
     be accessing through a pointer to some substruct of the
     structure, and if we try to prune there, we will have the wrong
     offset, and get the wrong answer.
     i.e., we can't prune without more work if we have something like

     struct gcc_target
     {
       struct asm_out
       {
         const char *byte_op;
	 struct asm_int_op
	 {    
	   const char *hi;
	 } aligned_op;
       } asm_out;
     } targetm;
     
     foo = &targetm.asm_out.aligned_op;
     return foo->hi;

     SFT.1, which represents hi, will have SFT_OFFSET=32 because in
     terms of SFT_PARENT_VAR, that is where it is.
     However, the access through the foo pointer will be at offset 0.  */
  if (size != -1
      && TREE_CODE (alias) == STRUCT_FIELD_TAG
      && base
      && TREE_TYPE (base) == TREE_TYPE (SFT_PARENT_VAR (alias))
      && !overlap_subvar (offset, size, alias, NULL))
    {
#ifdef ACCESS_DEBUGGING
      fprintf (stderr, "Access to ");
      print_generic_expr (stderr, ref, 0);
      fprintf (stderr, " may not touch ");
      print_generic_expr (stderr, alias, 0);
      fprintf (stderr, " in function %s\n", get_name (current_function_decl));
#endif
      return false;
    }

  /* Without strict aliasing, it is impossible for a component access
     through a pointer to touch a random variable, unless that
     variable *is* a structure or a pointer.

     That is, given p->c, and some random global variable b,
     there is no legal way that p->c could be an access to b.
     
     Without strict aliasing on, we consider it legal to do something
     like:

     struct foos { int l; };
     int foo;
     static struct foos *getfoo(void);
     int main (void)
     {
       struct foos *f = getfoo();
       f->l = 1;
       foo = 2;
       if (f->l == 1)
         abort();
       exit(0);
     }
     static struct foos *getfoo(void)     
     { return (struct foos *)&foo; }
     
     (taken from 20000623-1.c)

     The docs also say/imply that access through union pointers
     is legal (but *not* if you take the address of the union member,
     i.e. the inverse), such that you can do

     typedef union {
       int d;
     } U;

     int rv;
     void breakme()
     {
       U *rv0;
       U *pretmp = (U*)&rv;
       rv0 = pretmp;
       rv0->d = 42;    
     }
     To implement this, we just punt on accesses through union
     pointers entirely.
  */
  else if (ref 
	   && flag_strict_aliasing
	   && TREE_CODE (ref) != INDIRECT_REF
	   && !MTAG_P (alias)
	   && (TREE_CODE (base) != INDIRECT_REF
	       || TREE_CODE (TREE_TYPE (base)) != UNION_TYPE)
	   && !AGGREGATE_TYPE_P (TREE_TYPE (alias))
	   && TREE_CODE (TREE_TYPE (alias)) != COMPLEX_TYPE
	   && !POINTER_TYPE_P (TREE_TYPE (alias))
	   /* When the struct has may_alias attached to it, we need not to
	      return true.  */
	   && get_alias_set (base))
    {
#ifdef ACCESS_DEBUGGING
      fprintf (stderr, "Access to ");
      print_generic_expr (stderr, ref, 0);
      fprintf (stderr, " may not touch ");
      print_generic_expr (stderr, alias, 0);
      fprintf (stderr, " in function %s\n", get_name (current_function_decl));
#endif
      return false;
    }

  /* If the offset of the access is greater than the size of one of
     the possible aliases, it can't be touching that alias, because it
     would be past the end of the structure.  */
  else if (ref
	   && flag_strict_aliasing
	   && TREE_CODE (ref) != INDIRECT_REF
	   && !MTAG_P (alias)
	   && !POINTER_TYPE_P (TREE_TYPE (alias))
	   && offsetgtz
	   && DECL_SIZE (alias)
	   && TREE_CODE (DECL_SIZE (alias)) == INTEGER_CST
	   && uoffset > TREE_INT_CST_LOW (DECL_SIZE (alias)))
    {
#ifdef ACCESS_DEBUGGING
      fprintf (stderr, "Access to ");
      print_generic_expr (stderr, ref, 0);
      fprintf (stderr, " may not touch ");
      print_generic_expr (stderr, alias, 0);
      fprintf (stderr, " in function %s\n", get_name (current_function_decl));
#endif
      return false;
    }	   

  return true;
}


/* Add VAR to the virtual operands array.  FLAGS is as in
   get_expr_operands.  FULL_REF is a tree that contains the entire
   pointer dereference expression, if available, or NULL otherwise.
   OFFSET and SIZE come from the memory access expression that
   generated this virtual operand.  FOR_CLOBBER is true is this is
   adding a virtual operand for a call clobber.  */

static void 
add_virtual_operand (tree var, stmt_ann_t s_ann, int flags,
		     tree full_ref, HOST_WIDE_INT offset,
		     HOST_WIDE_INT size, bool for_clobber)
{
  VEC(tree,gc) *aliases;
  tree sym;
  var_ann_t v_ann;
  
  sym = (TREE_CODE (var) == SSA_NAME ? SSA_NAME_VAR (var) : var);
  v_ann = var_ann (sym);
  
  /* Mark statements with volatile operands.  Optimizers should back
     off from statements having volatile operands.  */
  if (TREE_THIS_VOLATILE (sym) && s_ann)
    s_ann->has_volatile_ops = true;

  /* If the variable cannot be modified and this is a V_MAY_DEF change
     it into a VUSE.  This happens when read-only variables are marked
     call-clobbered and/or aliased to writable variables.  So we only
     check that this only happens on non-specific stores.

     Note that if this is a specific store, i.e. associated with a
     modify_expr, then we can't suppress the V_MAY_DEF, lest we run
     into validation problems.

     This can happen when programs cast away const, leaving us with a
     store to read-only memory.  If the statement is actually executed
     at runtime, then the program is ill formed.  If the statement is
     not executed then all is well.  At the very least, we cannot ICE.  */
  if ((flags & opf_non_specific) && unmodifiable_var_p (var))
    flags &= ~(opf_is_def | opf_kill_def);
  
  /* The variable is not a GIMPLE register.  Add it (or its aliases) to
     virtual operands, unless the caller has specifically requested
     not to add virtual operands (used when adding operands inside an
     ADDR_EXPR expression).  */
  if (flags & opf_no_vops)
    return;
  
  aliases = v_ann->may_aliases;
  if (aliases == NULL)
    {
      /* The variable is not aliased or it is an alias tag.  */
      if (flags & opf_is_def)
	{
	  if (flags & opf_kill_def)
	    {
	      /* V_MUST_DEF for non-aliased, non-GIMPLE register 
		 variable definitions.  */
	      gcc_assert (!MTAG_P (var)
			  || TREE_CODE (var) == STRUCT_FIELD_TAG);
	      append_v_must_def (var);
	    }
	  else
	    {
	      /* Add a V_MAY_DEF for call-clobbered variables and
		 memory tags.  */
	      append_v_may_def (var);
	    }
	}
      else
	append_vuse (var);
    }
  else
    {
      unsigned i;
      tree al;
      
      /* The variable is aliased.  Add its aliases to the virtual
	 operands.  */
      gcc_assert (VEC_length (tree, aliases) != 0);
      
      if (flags & opf_is_def)
	{
	  
	  bool none_added = true;

	  for (i = 0; VEC_iterate (tree, aliases, i, al); i++)
	    {
	      if (!access_can_touch_variable (full_ref, al, offset, size))
		continue;
	      
	      none_added = false;
	      append_v_may_def (al);
	    }

	  /* If the variable is also an alias tag, add a virtual
	     operand for it, otherwise we will miss representing
	     references to the members of the variable's alias set.	     
	     This fixes the bug in gcc.c-torture/execute/20020503-1.c.
	     
	     It is also necessary to add bare defs on clobbers for
	     SMT's, so that bare SMT uses caused by pruning all the
	     aliases will link up properly with calls.   In order to
	     keep the number of these bare defs we add down to the
	     minimum necessary, we keep track of which SMT's were used
	     alone in statement vdefs or VUSEs.  */
	  if (v_ann->is_aliased
	      || none_added
	      || (TREE_CODE (var) == SYMBOL_MEMORY_TAG
		  && for_clobber
		  && SMT_USED_ALONE (var)))
	    {
	      /* Every bare SMT def we add should have SMT_USED_ALONE
		 set on it, or else we will get the wrong answer on
		 clobbers.  Sadly, this assertion trips on code that
		 violates strict aliasing rules, because they *do* get
		 the clobbers wrong, since it is illegal code.  As a
		 result, we currently only enable it for aliasing
		 debugging.  Someone might wish to turn this code into
		 a nice strict-aliasing warning, since we *know* it
		 will get the wrong answer...  */
#ifdef ACCESS_DEBUGGING
	      if (none_added
		  && !updating_used_alone && aliases_computed_p
		  && TREE_CODE (var) == SYMBOL_MEMORY_TAG)
		gcc_assert (SMT_USED_ALONE (var));
#endif
	      append_v_may_def (var);
	    }
	}
      else
	{
	  bool none_added = true;
	  for (i = 0; VEC_iterate (tree, aliases, i, al); i++)
	    {
	      if (!access_can_touch_variable (full_ref, al, offset, size))
		continue;
	      none_added = false;
	      append_vuse (al);
	    }

	  /* Similarly, append a virtual uses for VAR itself, when
	     it is an alias tag.  */
	  if (v_ann->is_aliased || none_added)
	    append_vuse (var);
	}
    }
}


/* Add *VAR_P to the appropriate operand array for S_ANN.  FLAGS is as in
   get_expr_operands.  If *VAR_P is a GIMPLE register, it will be added to
   the statement's real operands, otherwise it is added to virtual
   operands.  */

static void
add_stmt_operand (tree *var_p, stmt_ann_t s_ann, int flags)
{
  bool is_real_op;
  tree var, sym;
  var_ann_t v_ann;

  var = *var_p;
  gcc_assert (SSA_VAR_P (var));

  is_real_op = is_gimple_reg (var);

  /* If this is a real operand, the operand is either an SSA name or a 
     decl.  Virtual operands may only be decls.  */
  gcc_assert (is_real_op || DECL_P (var));

  sym = (TREE_CODE (var) == SSA_NAME ? SSA_NAME_VAR (var) : var);
  v_ann = var_ann (sym);

  /* Mark statements with volatile operands.  Optimizers should back
     off from statements having volatile operands.  */
  if (TREE_THIS_VOLATILE (sym) && s_ann)
    s_ann->has_volatile_ops = true;

  if (is_real_op)
    {
      /* The variable is a GIMPLE register.  Add it to real operands.  */
      if (flags & opf_is_def)
	append_def (var_p);
      else
	append_use (var_p);
    }
  else
    add_virtual_operand (var, s_ann, flags, NULL_TREE, 0, -1, false);
}


/* A subroutine of get_expr_operands to handle INDIRECT_REF,
   ALIGN_INDIRECT_REF and MISALIGNED_INDIRECT_REF.  

   STMT is the statement being processed, EXPR is the INDIRECT_REF
      that got us here.
   
   FLAGS is as in get_expr_operands.

   FULL_REF contains the full pointer dereference expression, if we
      have it, or NULL otherwise.

   OFFSET and SIZE are the location of the access inside the
      dereferenced pointer, if known.

   RECURSE_ON_BASE should be set to true if we want to continue
      calling get_expr_operands on the base pointer, and false if
      something else will do it for us.  */

static void
get_indirect_ref_operands (tree stmt, tree expr, int flags,
			   tree full_ref,
			   HOST_WIDE_INT offset, HOST_WIDE_INT size,
			   bool recurse_on_base)
{
  tree *pptr = &TREE_OPERAND (expr, 0);
  tree ptr = *pptr;
  stmt_ann_t s_ann = stmt_ann (stmt);

  /* Stores into INDIRECT_REF operands are never killing definitions.  */
  flags &= ~opf_kill_def;

  if (SSA_VAR_P (ptr))
    {
      struct ptr_info_def *pi = NULL;

      /* If PTR has flow-sensitive points-to information, use it.  */
      if (TREE_CODE (ptr) == SSA_NAME
	  && (pi = SSA_NAME_PTR_INFO (ptr)) != NULL
	  && pi->name_mem_tag)
	{
	  /* PTR has its own memory tag.  Use it.  */
	  add_virtual_operand (pi->name_mem_tag, s_ann, flags,
			       full_ref, offset, size, false);
	}
      else
	{
	  /* If PTR is not an SSA_NAME or it doesn't have a name
	     tag, use its symbol memory tag.  */
	  var_ann_t v_ann;

	  /* If we are emitting debugging dumps, display a warning if
	     PTR is an SSA_NAME with no flow-sensitive alias
	     information.  That means that we may need to compute
	     aliasing again.  */
	  if (dump_file
	      && TREE_CODE (ptr) == SSA_NAME
	      && pi == NULL)
	    {
	      fprintf (dump_file,
		  "NOTE: no flow-sensitive alias info for ");
	      print_generic_expr (dump_file, ptr, dump_flags);
	      fprintf (dump_file, " in ");
	      print_generic_stmt (dump_file, stmt, dump_flags);
	    }

	  if (TREE_CODE (ptr) == SSA_NAME)
	    ptr = SSA_NAME_VAR (ptr);
	  v_ann = var_ann (ptr);

	  if (v_ann->symbol_mem_tag)
	    add_virtual_operand (v_ann->symbol_mem_tag, s_ann, flags,
				 full_ref, offset, size, false);
	}
    }
  else if (TREE_CODE (ptr) == INTEGER_CST)
    {
      /* If a constant is used as a pointer, we can't generate a real
	 operand for it but we mark the statement volatile to prevent
	 optimizations from messing things up.  */
      if (s_ann)
	s_ann->has_volatile_ops = true;
      return;
    }
  else
    {
      /* Ok, this isn't even is_gimple_min_invariant.  Something's broke.  */
      gcc_unreachable ();
    }

  /* If requested, add a USE operand for the base pointer.  */
  if (recurse_on_base)
    get_expr_operands (stmt, pptr, opf_none);
}


/* A subroutine of get_expr_operands to handle TARGET_MEM_REF.  */

static void
get_tmr_operands (tree stmt, tree expr, int flags)
{
  tree tag = TMR_TAG (expr), ref;
  HOST_WIDE_INT offset, size, maxsize;
  subvar_t svars, sv;
  stmt_ann_t s_ann = stmt_ann (stmt);

  /* First record the real operands.  */
  get_expr_operands (stmt, &TMR_BASE (expr), opf_none);
  get_expr_operands (stmt, &TMR_INDEX (expr), opf_none);

  /* MEM_REFs should never be killing.  */
  flags &= ~opf_kill_def;

  if (TMR_SYMBOL (expr))
    {
      stmt_ann_t ann = stmt_ann (stmt);
      add_to_addressable_set (TMR_SYMBOL (expr), &ann->addresses_taken);
    }

  if (!tag)
    {
      /* Something weird, so ensure that we will be careful.  */
      stmt_ann (stmt)->has_volatile_ops = true;
      return;
    }

  if (DECL_P (tag))
    {
      get_expr_operands (stmt, &tag, flags);
      return;
    }

  ref = get_ref_base_and_extent (tag, &offset, &size, &maxsize);
  gcc_assert (ref != NULL_TREE);
  svars = get_subvars_for_var (ref);
  for (sv = svars; sv; sv = sv->next)
    {
      bool exact;		
      if (overlap_subvar (offset, maxsize, sv->var, &exact))
	{
	  int subvar_flags = flags;
	  if (!exact || size != maxsize)
	    subvar_flags &= ~opf_kill_def;
	  add_stmt_operand (&sv->var, s_ann, subvar_flags);
	}
    }
}


/* Add clobbering definitions for .GLOBAL_VAR or for each of the call
   clobbered variables in the function.  */

static void
add_call_clobber_ops (tree stmt, tree callee)
{
  unsigned u;
  bitmap_iterator bi;
  stmt_ann_t s_ann = stmt_ann (stmt);
  bitmap not_read_b, not_written_b;
  
  /* Functions that are not const, pure or never return may clobber
     call-clobbered variables.  */
  if (s_ann)
    s_ann->makes_clobbering_call = true;

  /* If we created .GLOBAL_VAR earlier, just use it.  See compute_may_aliases 
     for the heuristic used to decide whether to create .GLOBAL_VAR or not.  */
  if (global_var)
    {
      add_stmt_operand (&global_var, s_ann, opf_is_def);
      return;
    }

  /* Get info for local and module level statics.  There is a bit
     set for each static if the call being processed does not read
     or write that variable.  */
  not_read_b = callee ? ipa_reference_get_not_read_global (callee) : NULL; 
  not_written_b = callee ? ipa_reference_get_not_written_global (callee) : NULL; 
  /* Add a V_MAY_DEF operand for every call clobbered variable.  */
  EXECUTE_IF_SET_IN_BITMAP (call_clobbered_vars, 0, u, bi)
    {
      tree var = referenced_var_lookup (u);
      unsigned int escape_mask = var_ann (var)->escape_mask;
      tree real_var = var;
      bool not_read;
      bool not_written;
      
      /* Not read and not written are computed on regular vars, not
	 subvars, so look at the parent var if this is an SFT. */
      if (TREE_CODE (var) == STRUCT_FIELD_TAG)
	real_var = SFT_PARENT_VAR (var);

      not_read = not_read_b ? bitmap_bit_p (not_read_b, 
					    DECL_UID (real_var)) : false;
      not_written = not_written_b ? bitmap_bit_p (not_written_b, 
						  DECL_UID (real_var)) : false;
      gcc_assert (!unmodifiable_var_p (var));
      
      clobber_stats.clobbered_vars++;

      /* See if this variable is really clobbered by this function.  */

      /* Trivial case: Things escaping only to pure/const are not
	 clobbered by non-pure-const, and only read by pure/const. */
      if ((escape_mask & ~(ESCAPE_TO_PURE_CONST)) == 0)
	{
	  tree call = get_call_expr_in (stmt);
	  if (call_expr_flags (call) & (ECF_CONST | ECF_PURE))
	    {
	      add_stmt_operand (&var, s_ann, opf_none);
	      clobber_stats.unescapable_clobbers_avoided++;
	      continue;
	    }
	  else
	    {
	      clobber_stats.unescapable_clobbers_avoided++;
	      continue;
	    }
	}
            
      if (not_written)
	{
	  clobber_stats.static_write_clobbers_avoided++;
	  if (!not_read)
	    add_stmt_operand (&var, s_ann, opf_none);
	  else
	    clobber_stats.static_read_clobbers_avoided++;
	}
      else
	add_virtual_operand (var, s_ann, opf_is_def, NULL, 0, -1, true);
    }
}


/* Add VUSE operands for .GLOBAL_VAR or all call clobbered variables in the
   function.  */

static void
add_call_read_ops (tree stmt, tree callee)
{
  unsigned u;
  bitmap_iterator bi;
  stmt_ann_t s_ann = stmt_ann (stmt);
  bitmap not_read_b;

  /* if the function is not pure, it may reference memory.  Add
     a VUSE for .GLOBAL_VAR if it has been created.  See add_referenced_var
     for the heuristic used to decide whether to create .GLOBAL_VAR.  */
  if (global_var)
    {
      add_stmt_operand (&global_var, s_ann, opf_none);
      return;
    }
  
  not_read_b = callee ? ipa_reference_get_not_read_global (callee) : NULL; 

  /* Add a VUSE for each call-clobbered variable.  */
  EXECUTE_IF_SET_IN_BITMAP (call_clobbered_vars, 0, u, bi)
    {
      tree var = referenced_var (u);
      tree real_var = var;
      bool not_read;
      
      clobber_stats.readonly_clobbers++;

      /* Not read and not written are computed on regular vars, not
	 subvars, so look at the parent var if this is an SFT. */

      if (TREE_CODE (var) == STRUCT_FIELD_TAG)
	real_var = SFT_PARENT_VAR (var);

      not_read = not_read_b ? bitmap_bit_p (not_read_b, DECL_UID (real_var))
	                    : false;
      
      if (not_read)
	{
	  clobber_stats.static_readonly_clobbers_avoided++;
	  continue;
	}
            
      add_stmt_operand (&var, s_ann, opf_none | opf_non_specific);
    }
}


/* A subroutine of get_expr_operands to handle CALL_EXPR.  */

static void
get_call_expr_operands (tree stmt, tree expr)
{
  tree op;
  int call_flags = call_expr_flags (expr);

  /* If aliases have been computed already, add V_MAY_DEF or V_USE
     operands for all the symbols that have been found to be
     call-clobbered.
     
     Note that if aliases have not been computed, the global effects
     of calls will not be included in the SSA web. This is fine
     because no optimizer should run before aliases have been
     computed.  By not bothering with virtual operands for CALL_EXPRs
     we avoid adding superfluous virtual operands, which can be a
     significant compile time sink (See PR 15855).  */
  if (aliases_computed_p
      && !bitmap_empty_p (call_clobbered_vars)
      && !(call_flags & ECF_NOVOPS))
    {
      /* A 'pure' or a 'const' function never call-clobbers anything. 
	 A 'noreturn' function might, but since we don't return anyway 
	 there is no point in recording that.  */ 
      if (TREE_SIDE_EFFECTS (expr)
	  && !(call_flags & (ECF_PURE | ECF_CONST | ECF_NORETURN)))
	add_call_clobber_ops (stmt, get_callee_fndecl (expr));
      else if (!(call_flags & ECF_CONST))
	add_call_read_ops (stmt, get_callee_fndecl (expr));
    }

  /* Find uses in the called function.  */
  get_expr_operands (stmt, &TREE_OPERAND (expr, 0), opf_none);

  for (op = TREE_OPERAND (expr, 1); op; op = TREE_CHAIN (op))
    get_expr_operands (stmt, &TREE_VALUE (op), opf_none);

  get_expr_operands (stmt, &TREE_OPERAND (expr, 2), opf_none);
}


/* Scan operands in the ASM_EXPR stmt referred to in INFO.  */

static void
get_asm_expr_operands (tree stmt)
{
  stmt_ann_t s_ann = stmt_ann (stmt);
  int noutputs = list_length (ASM_OUTPUTS (stmt));
  const char **oconstraints
    = (const char **) alloca ((noutputs) * sizeof (const char *));
  int i;
  tree link;
  const char *constraint;
  bool allows_mem, allows_reg, is_inout;

  for (i=0, link = ASM_OUTPUTS (stmt); link; ++i, link = TREE_CHAIN (link))
    {
      constraint = TREE_STRING_POINTER (TREE_VALUE (TREE_PURPOSE (link)));
      oconstraints[i] = constraint;
      parse_output_constraint (&constraint, i, 0, 0, &allows_mem,
	                       &allows_reg, &is_inout);

      /* This should have been split in gimplify_asm_expr.  */
      gcc_assert (!allows_reg || !is_inout);

      /* Memory operands are addressable.  Note that STMT needs the
	 address of this operand.  */
      if (!allows_reg && allows_mem)
	{
	  tree t = get_base_address (TREE_VALUE (link));
	  if (t && DECL_P (t) && s_ann)
	    add_to_addressable_set (t, &s_ann->addresses_taken);
	}

      get_expr_operands (stmt, &TREE_VALUE (link), opf_is_def);
    }

  for (link = ASM_INPUTS (stmt); link; link = TREE_CHAIN (link))
    {
      constraint = TREE_STRING_POINTER (TREE_VALUE (TREE_PURPOSE (link)));
      parse_input_constraint (&constraint, 0, 0, noutputs, 0,
			      oconstraints, &allows_mem, &allows_reg);

      /* Memory operands are addressable.  Note that STMT needs the
	 address of this operand.  */
      if (!allows_reg && allows_mem)
	{
	  tree t = get_base_address (TREE_VALUE (link));
	  if (t && DECL_P (t) && s_ann)
	    add_to_addressable_set (t, &s_ann->addresses_taken);
	}

      get_expr_operands (stmt, &TREE_VALUE (link), 0);
    }


  /* Clobber memory for asm ("" : : : "memory");  */
  for (link = ASM_CLOBBERS (stmt); link; link = TREE_CHAIN (link))
    if (strcmp (TREE_STRING_POINTER (TREE_VALUE (link)), "memory") == 0)
      {
	unsigned i;
	bitmap_iterator bi;

	/* Clobber all call-clobbered variables (or .GLOBAL_VAR if we
	   decided to group them).  */
	if (global_var)
	  add_stmt_operand (&global_var, s_ann, opf_is_def);
	else
	  EXECUTE_IF_SET_IN_BITMAP (call_clobbered_vars, 0, i, bi)
	    {
	      tree var = referenced_var (i);
	      add_stmt_operand (&var, s_ann, opf_is_def | opf_non_specific);
	    }

	/* Now clobber all addressables.  */
	EXECUTE_IF_SET_IN_BITMAP (addressable_vars, 0, i, bi)
	    {
	      tree var = referenced_var (i);

	      /* Subvars are explicitly represented in this list, so
		 we don't need the original to be added to the clobber
		 ops, but the original *will* be in this list because 
		 we keep the addressability of the original
		 variable up-to-date so we don't screw up the rest of
		 the backend.  */
	      if (var_can_have_subvars (var)
		  && get_subvars_for_var (var) != NULL)
		continue;		

	      add_stmt_operand (&var, s_ann, opf_is_def | opf_non_specific);
	    }

	break;
      }
}


/* Scan operands for the assignment expression EXPR in statement STMT.  */

static void
get_modify_expr_operands (tree stmt, tree expr)
{
  /* First get operands from the RHS.  */
  get_expr_operands (stmt, &TREE_OPERAND (expr, 1), opf_none);

  /* For the LHS, use a regular definition (OPF_IS_DEF) for GIMPLE
     registers.  If the LHS is a store to memory, we will either need
     a preserving definition (V_MAY_DEF) or a killing definition
     (V_MUST_DEF).

     Preserving definitions are those that modify a part of an
     aggregate object for which no subvars have been computed (or the
     reference does not correspond exactly to one of them). Stores
     through a pointer are also represented with V_MAY_DEF operators.

     The determination of whether to use a preserving or a killing
     definition is done while scanning the LHS of the assignment.  By
     default, assume that we will emit a V_MUST_DEF.  */
  get_expr_operands (stmt, &TREE_OPERAND (expr, 0), opf_is_def|opf_kill_def);
}


/* Recursively scan the expression pointed to by EXPR_P in statement
   STMT.  FLAGS is one of the OPF_* constants modifying how to
   interpret the operands found.  */

static void
get_expr_operands (tree stmt, tree *expr_p, int flags)
{
  enum tree_code code;
  enum tree_code_class class;
  tree expr = *expr_p;
  stmt_ann_t s_ann = stmt_ann (stmt);

  if (expr == NULL)
    return;

  code = TREE_CODE (expr);
  class = TREE_CODE_CLASS (code);

  switch (code)
    {
    case ADDR_EXPR:
      /* Taking the address of a variable does not represent a
	 reference to it, but the fact that the statement takes its
	 address will be of interest to some passes (e.g. alias
	 resolution).  */
      add_to_addressable_set (TREE_OPERAND (expr, 0), &s_ann->addresses_taken);

      /* If the address is invariant, there may be no interesting
	 variable references inside.  */
      if (is_gimple_min_invariant (expr))
	return;

      /* Otherwise, there may be variables referenced inside but there
	 should be no VUSEs created, since the referenced objects are
	 not really accessed.  The only operands that we should find
	 here are ARRAY_REF indices which will always be real operands
	 (GIMPLE does not allow non-registers as array indices).  */
      flags |= opf_no_vops;
      get_expr_operands (stmt, &TREE_OPERAND (expr, 0), flags);
      return;

    case SSA_NAME:
    case STRUCT_FIELD_TAG:
    case SYMBOL_MEMORY_TAG:
    case NAME_MEMORY_TAG:
     add_stmt_operand (expr_p, s_ann, flags);
     return;

    case VAR_DECL:
    case PARM_DECL:
    case RESULT_DECL:
      {
	subvar_t svars;
	
	/* Add the subvars for a variable, if it has subvars, to DEFS
	   or USES.  Otherwise, add the variable itself.  Whether it
	   goes to USES or DEFS depends on the operand flags.  */
	if (var_can_have_subvars (expr)
	    && (svars = get_subvars_for_var (expr)))
	  {
	    subvar_t sv;
	    for (sv = svars; sv; sv = sv->next)
	      add_stmt_operand (&sv->var, s_ann, flags);
	  }
	else
	  add_stmt_operand (expr_p, s_ann, flags);

	return;
      }

    case MISALIGNED_INDIRECT_REF:
      get_expr_operands (stmt, &TREE_OPERAND (expr, 1), flags);
      /* fall through */

    case ALIGN_INDIRECT_REF:
    case INDIRECT_REF:
      get_indirect_ref_operands (stmt, expr, flags, NULL_TREE, 0, -1, true);
      return;

    case TARGET_MEM_REF:
      get_tmr_operands (stmt, expr, flags);
      return;

    case ARRAY_REF:
    case ARRAY_RANGE_REF:
    case COMPONENT_REF:
    case REALPART_EXPR:
    case IMAGPART_EXPR:
      {
	tree ref;
	HOST_WIDE_INT offset, size, maxsize;
	bool none = true;

	/* This component reference becomes an access to all of the
	   subvariables it can touch, if we can determine that, but
	   *NOT* the real one.  If we can't determine which fields we
	   could touch, the recursion will eventually get to a
	   variable and add *all* of its subvars, or whatever is the
	   minimum correct subset.  */
	ref = get_ref_base_and_extent (expr, &offset, &size, &maxsize);
	if (SSA_VAR_P (ref) && get_subvars_for_var (ref))
	  {
	    subvar_t sv;
	    subvar_t svars = get_subvars_for_var (ref);

	    for (sv = svars; sv; sv = sv->next)
	      {
		bool exact;		

		if (overlap_subvar (offset, maxsize, sv->var, &exact))
		  {
	            int subvar_flags = flags;
		    none = false;
		    if (!exact || size != maxsize)
		      subvar_flags &= ~opf_kill_def;
		    add_stmt_operand (&sv->var, s_ann, subvar_flags);
		  }
	      }

	    if (!none)
	      flags |= opf_no_vops;
	  }
	else if (TREE_CODE (ref) == INDIRECT_REF)
	  {
	    get_indirect_ref_operands (stmt, ref, flags, expr, offset,
		                       maxsize, false);
	    flags |= opf_no_vops;
	  }

	/* Even if we found subvars above we need to ensure to see
	   immediate uses for d in s.a[d].  In case of s.a having
	   a subvar or we would miss it otherwise.  */
	get_expr_operands (stmt, &TREE_OPERAND (expr, 0),
			   flags & ~opf_kill_def);
	
	if (code == COMPONENT_REF)
	  {
	    if (s_ann && TREE_THIS_VOLATILE (TREE_OPERAND (expr, 1)))
	      s_ann->has_volatile_ops = true; 
	    get_expr_operands (stmt, &TREE_OPERAND (expr, 2), opf_none);
	  }
	else if (code == ARRAY_REF || code == ARRAY_RANGE_REF)
	  {
            get_expr_operands (stmt, &TREE_OPERAND (expr, 1), opf_none);
            get_expr_operands (stmt, &TREE_OPERAND (expr, 2), opf_none);
            get_expr_operands (stmt, &TREE_OPERAND (expr, 3), opf_none);
	  }

	return;
      }

    case WITH_SIZE_EXPR:
      /* WITH_SIZE_EXPR is a pass-through reference to its first argument,
	 and an rvalue reference to its second argument.  */
      get_expr_operands (stmt, &TREE_OPERAND (expr, 1), opf_none);
      get_expr_operands (stmt, &TREE_OPERAND (expr, 0), flags);
      return;

    case CALL_EXPR:
      get_call_expr_operands (stmt, expr);
      return;

    case COND_EXPR:
    case VEC_COND_EXPR:
      get_expr_operands (stmt, &TREE_OPERAND (expr, 0), opf_none);
      get_expr_operands (stmt, &TREE_OPERAND (expr, 1), opf_none);
      get_expr_operands (stmt, &TREE_OPERAND (expr, 2), opf_none);
      return;

    case MODIFY_EXPR:
      get_modify_expr_operands (stmt, expr);
      return;

    case CONSTRUCTOR:
      {
	/* General aggregate CONSTRUCTORs have been decomposed, but they
	   are still in use as the COMPLEX_EXPR equivalent for vectors.  */
	constructor_elt *ce;
	unsigned HOST_WIDE_INT idx;

	for (idx = 0;
	     VEC_iterate (constructor_elt, CONSTRUCTOR_ELTS (expr), idx, ce);
	     idx++)
	  get_expr_operands (stmt, &ce->value, opf_none);

	return;
      }

    case BIT_FIELD_REF:
      /* Stores using BIT_FIELD_REF are always preserving definitions.  */
      flags &= ~opf_kill_def;

      /* Fallthru  */

    case TRUTH_NOT_EXPR:
    case VIEW_CONVERT_EXPR:
    do_unary:
      get_expr_operands (stmt, &TREE_OPERAND (expr, 0), flags);
      return;

    case TRUTH_AND_EXPR:
    case TRUTH_OR_EXPR:
    case TRUTH_XOR_EXPR:
    case COMPOUND_EXPR:
    case OBJ_TYPE_REF:
    case ASSERT_EXPR:
    do_binary:
      {
	get_expr_operands (stmt, &TREE_OPERAND (expr, 0), flags);
	get_expr_operands (stmt, &TREE_OPERAND (expr, 1), flags);
	return;
      }

    case DOT_PROD_EXPR:
    case REALIGN_LOAD_EXPR:
      {
	get_expr_operands (stmt, &TREE_OPERAND (expr, 0), flags);
        get_expr_operands (stmt, &TREE_OPERAND (expr, 1), flags);
        get_expr_operands (stmt, &TREE_OPERAND (expr, 2), flags);
        return;
      }

    case BLOCK:
    case FUNCTION_DECL:
    case EXC_PTR_EXPR:
    case FILTER_EXPR:
    case LABEL_DECL:
    case CONST_DECL:
    case OMP_PARALLEL:
    case OMP_SECTIONS:
    case OMP_FOR:
    case OMP_SINGLE:
    case OMP_MASTER:
    case OMP_ORDERED:
    case OMP_CRITICAL:
    case OMP_RETURN:
    case OMP_CONTINUE:
      /* Expressions that make no memory references.  */
      return;

    default:
      if (class == tcc_unary)
	goto do_unary;
      if (class == tcc_binary || class == tcc_comparison)
	goto do_binary;
      if (class == tcc_constant || class == tcc_type)
	return;
    }

  /* If we get here, something has gone wrong.  */
#ifdef ENABLE_CHECKING
  fprintf (stderr, "unhandled expression in get_expr_operands():\n");
  debug_tree (expr);
  fputs ("\n", stderr);
#endif
  gcc_unreachable ();
}


/* Parse STMT looking for operands.  When finished, the various
   build_* operand vectors will have potential operands in them.  */

static void
parse_ssa_operands (tree stmt)
{
  enum tree_code code;

  code = TREE_CODE (stmt);
  switch (code)
    {
    case MODIFY_EXPR:
      get_modify_expr_operands (stmt, stmt);
      break;

    case COND_EXPR:
      get_expr_operands (stmt, &COND_EXPR_COND (stmt), opf_none);
      break;

    case SWITCH_EXPR:
      get_expr_operands (stmt, &SWITCH_COND (stmt), opf_none);
      break;

    case ASM_EXPR:
      get_asm_expr_operands (stmt);
      break;

    case RETURN_EXPR:
      get_expr_operands (stmt, &TREE_OPERAND (stmt, 0), opf_none);
      break;

    case GOTO_EXPR:
      get_expr_operands (stmt, &GOTO_DESTINATION (stmt), opf_none);
      break;

    case LABEL_EXPR:
      get_expr_operands (stmt, &LABEL_EXPR_LABEL (stmt), opf_none);
      break;

    case BIND_EXPR:
    case CASE_LABEL_EXPR:
    case TRY_CATCH_EXPR:
    case TRY_FINALLY_EXPR:
    case EH_FILTER_EXPR:
    case CATCH_EXPR:
    case RESX_EXPR:
      /* These nodes contain no variable references.  */
      break;

    default:
      /* Notice that if get_expr_operands tries to use &STMT as the
	 operand pointer (which may only happen for USE operands), we
	 will fail in add_stmt_operand.  This default will handle
	 statements like empty statements, or CALL_EXPRs that may
	 appear on the RHS of a statement or as statements themselves.  */
      get_expr_operands (stmt, &stmt, opf_none);
      break;
    }
}


/* Create an operands cache for STMT.  */

static void
build_ssa_operands (tree stmt)
{
  stmt_ann_t ann = get_stmt_ann (stmt);
  
  /* Initially assume that the statement has no volatile operands and
     does not take the address of any symbols.  */
  if (ann)
    {
      ann->has_volatile_ops = false;
      if (ann->addresses_taken)
	ann->addresses_taken = NULL;
    }

  start_ssa_stmt_operands ();

  parse_ssa_operands (stmt);
  operand_build_sort_virtual (build_vuses);
  operand_build_sort_virtual (build_v_may_defs);
  operand_build_sort_virtual (build_v_must_defs);

  finalize_ssa_stmt_operands (stmt);
}


/* Free any operands vectors in OPS.  */

void 
free_ssa_operands (stmt_operands_p ops)
{
  ops->def_ops = NULL;
  ops->use_ops = NULL;
  ops->maydef_ops = NULL;
  ops->mustdef_ops = NULL;
  ops->vuse_ops = NULL;
}


/* Get the operands of statement STMT.  */

void
update_stmt_operands (tree stmt)
{
  stmt_ann_t ann = get_stmt_ann (stmt);

  /* If update_stmt_operands is called before SSA is initialized, do
     nothing.  */
  if (!ssa_operands_active ())
    return;

  /* The optimizers cannot handle statements that are nothing but a
     _DECL.  This indicates a bug in the gimplifier.  */
  gcc_assert (!SSA_VAR_P (stmt));

  gcc_assert (ann->modified);

  timevar_push (TV_TREE_OPS);

  build_ssa_operands (stmt);

  /* Clear the modified bit for STMT.  */
  ann->modified = 0;

  timevar_pop (TV_TREE_OPS);
}


/* Copies virtual operands from SRC to DST.  */

void
copy_virtual_operands (tree dest, tree src)
{
  tree t;
  ssa_op_iter iter, old_iter;
  use_operand_p use_p, u2;
  def_operand_p def_p, d2;

  build_ssa_operands (dest);

  /* Copy all the virtual fields.  */
  FOR_EACH_SSA_TREE_OPERAND (t, src, iter, SSA_OP_VUSE)
    append_vuse (t);
  FOR_EACH_SSA_TREE_OPERAND (t, src, iter, SSA_OP_VMAYDEF)
    append_v_may_def (t);
  FOR_EACH_SSA_TREE_OPERAND (t, src, iter, SSA_OP_VMUSTDEF)
    append_v_must_def (t);

  if (VEC_length (tree, build_vuses) == 0
      && VEC_length (tree, build_v_may_defs) == 0
      && VEC_length (tree, build_v_must_defs) == 0)
    return;

  /* Now commit the virtual operands to this stmt.  */
  finalize_ssa_v_must_defs (dest);
  finalize_ssa_v_may_defs (dest);
  finalize_ssa_vuses (dest);

  /* Finally, set the field to the same values as then originals.  */
  t = op_iter_init_tree (&old_iter, src, SSA_OP_VUSE);
  FOR_EACH_SSA_USE_OPERAND (use_p, dest, iter, SSA_OP_VUSE)
    {
      gcc_assert (!op_iter_done (&old_iter));
      SET_USE (use_p, t);
      t = op_iter_next_tree (&old_iter);
    }
  gcc_assert (op_iter_done (&old_iter));

  op_iter_init_maydef (&old_iter, src, &u2, &d2);
  FOR_EACH_SSA_MAYDEF_OPERAND (def_p, use_p, dest, iter)
    {
      gcc_assert (!op_iter_done (&old_iter));
      SET_USE (use_p, USE_FROM_PTR (u2));
      SET_DEF (def_p, DEF_FROM_PTR (d2));
      op_iter_next_maymustdef (&u2, &d2, &old_iter);
    }
  gcc_assert (op_iter_done (&old_iter));

  op_iter_init_mustdef (&old_iter, src, &u2, &d2);
  FOR_EACH_SSA_MUSTDEF_OPERAND (def_p, use_p, dest, iter)
    {
      gcc_assert (!op_iter_done (&old_iter));
      SET_USE (use_p, USE_FROM_PTR (u2));
      SET_DEF (def_p, DEF_FROM_PTR (d2));
      op_iter_next_maymustdef (&u2, &d2, &old_iter);
    }
  gcc_assert (op_iter_done (&old_iter));

}


/* Specifically for use in DOM's expression analysis.  Given a store, we
   create an artificial stmt which looks like a load from the store, this can
   be used to eliminate redundant loads.  OLD_OPS are the operands from the 
   store stmt, and NEW_STMT is the new load which represents a load of the
   values stored.  */

void
create_ssa_artficial_load_stmt (tree new_stmt, tree old_stmt)
{
  stmt_ann_t ann;
  tree op;
  ssa_op_iter iter;
  use_operand_p use_p;
  unsigned x;

  ann = get_stmt_ann (new_stmt);

  /* Process the stmt looking for operands.  */
  start_ssa_stmt_operands ();
  parse_ssa_operands (new_stmt);

  for (x = 0; x < VEC_length (tree, build_vuses); x++)
    {
      tree t = VEC_index (tree, build_vuses, x);
      if (TREE_CODE (t) != SSA_NAME)
	{
	  var_ann_t ann = var_ann (t);
	  ann->in_vuse_list = 0;
	}
    }
   
  for (x = 0; x < VEC_length (tree, build_v_may_defs); x++)
    {
      tree t = VEC_index (tree, build_v_may_defs, x);
      if (TREE_CODE (t) != SSA_NAME)
	{
	  var_ann_t ann = var_ann (t);
	  ann->in_v_may_def_list = 0;
	}
    }

  /* Remove any virtual operands that were found.  */
  VEC_truncate (tree, build_v_may_defs, 0);
  VEC_truncate (tree, build_v_must_defs, 0);
  VEC_truncate (tree, build_vuses, 0);

  /* For each VDEF on the original statement, we want to create a
     VUSE of the V_MAY_DEF result or V_MUST_DEF op on the new 
     statement.  */
  FOR_EACH_SSA_TREE_OPERAND (op, old_stmt, iter, 
			     (SSA_OP_VMAYDEF | SSA_OP_VMUSTDEF))
    append_vuse (op);
    
  /* Now build the operands for this new stmt.  */
  finalize_ssa_stmt_operands (new_stmt);

  /* All uses in this fake stmt must not be in the immediate use lists.  */
  FOR_EACH_SSA_USE_OPERAND (use_p, new_stmt, iter, SSA_OP_ALL_USES)
    delink_imm_use (use_p);
}


/* Swap operands EXP0 and EXP1 in statement STMT.  No attempt is done
   to test the validity of the swap operation.  */

void
swap_tree_operands (tree stmt, tree *exp0, tree *exp1)
{
  tree op0, op1;
  op0 = *exp0;
  op1 = *exp1;

  /* If the operand cache is active, attempt to preserve the relative
     positions of these two operands in their respective immediate use
     lists.  */
  if (ssa_operands_active () && op0 != op1)
    {
      use_optype_p use0, use1, ptr;
      use0 = use1 = NULL;

      /* Find the 2 operands in the cache, if they are there.  */
      for (ptr = USE_OPS (stmt); ptr; ptr = ptr->next)
	if (USE_OP_PTR (ptr)->use == exp0)
	  {
	    use0 = ptr;
	    break;
	  }

      for (ptr = USE_OPS (stmt); ptr; ptr = ptr->next)
	if (USE_OP_PTR (ptr)->use == exp1)
	  {
	    use1 = ptr;
	    break;
	  }

      /* If both uses don't have operand entries, there isn't much we can do
         at this point.  Presumably we don't need to worry about it.  */
      if (use0 && use1)
        {
	  tree *tmp = USE_OP_PTR (use1)->use;
	  USE_OP_PTR (use1)->use = USE_OP_PTR (use0)->use;
	  USE_OP_PTR (use0)->use = tmp;
	}
    }

  /* Now swap the data.  */
  *exp0 = op1;
  *exp1 = op0;
}


/* Add the base address of REF to the set *ADDRESSES_TAKEN.  If
   *ADDRESSES_TAKEN is NULL, a new set is created.  REF may be
   a single variable whose address has been taken or any other valid
   GIMPLE memory reference (structure reference, array, etc).  If the
   base address of REF is a decl that has sub-variables, also add all
   of its sub-variables.  */

void
add_to_addressable_set (tree ref, bitmap *addresses_taken)
{
  tree var;
  subvar_t svars;

  gcc_assert (addresses_taken);

  /* Note that it is *NOT OKAY* to use the target of a COMPONENT_REF
     as the only thing we take the address of.  If VAR is a structure,
     taking the address of a field means that the whole structure may
     be referenced using pointer arithmetic.  See PR 21407 and the
     ensuing mailing list discussion.  */
  var = get_base_address (ref);
  if (var && SSA_VAR_P (var))
    {
      if (*addresses_taken == NULL)
	*addresses_taken = BITMAP_GGC_ALLOC ();      
      
      if (var_can_have_subvars (var)
	  && (svars = get_subvars_for_var (var)))
	{
	  subvar_t sv;
	  for (sv = svars; sv; sv = sv->next)
	    {
	      bitmap_set_bit (*addresses_taken, DECL_UID (sv->var));
	      TREE_ADDRESSABLE (sv->var) = 1;
	    }
	}
      else
	{
	  bitmap_set_bit (*addresses_taken, DECL_UID (var));
	  TREE_ADDRESSABLE (var) = 1;
	}
    }
}


/* Scan the immediate_use list for VAR making sure its linked properly.
   Return TRUE if there is a problem and emit an error message to F.  */

bool
verify_imm_links (FILE *f, tree var)
{
  use_operand_p ptr, prev, list;
  int count;

  gcc_assert (TREE_CODE (var) == SSA_NAME);

  list = &(SSA_NAME_IMM_USE_NODE (var));
  gcc_assert (list->use == NULL);

  if (list->prev == NULL)
    {
      gcc_assert (list->next == NULL);
      return false;
    }

  prev = list;
  count = 0;
  for (ptr = list->next; ptr != list; )
    {
      if (prev != ptr->prev)
	goto error;
      
      if (ptr->use == NULL)
	goto error; /* 2 roots, or SAFE guard node.  */
      else if (*(ptr->use) != var)
	goto error;

      prev = ptr;
      ptr = ptr->next;

      /* Avoid infinite loops.  50,000,000 uses probably indicates a
	 problem.  */
      if (count++ > 50000000)
	goto error;
    }

  /* Verify list in the other direction.  */
  prev = list;
  for (ptr = list->prev; ptr != list; )
    {
      if (prev != ptr->next)
	goto error;
      prev = ptr;
      ptr = ptr->prev;
      if (count-- < 0)
	goto error;
    }

  if (count != 0)
    goto error;

  return false;

 error:
  if (ptr->stmt && stmt_modified_p (ptr->stmt))
    {
      fprintf (f, " STMT MODIFIED. - <%p> ", (void *)ptr->stmt);
      print_generic_stmt (f, ptr->stmt, TDF_SLIM);
    }
  fprintf (f, " IMM ERROR : (use_p : tree - %p:%p)", (void *)ptr, 
	   (void *)ptr->use);
  print_generic_expr (f, USE_FROM_PTR (ptr), TDF_SLIM);
  fprintf(f, "\n");
  return true;
}


/* Dump all the immediate uses to FILE.  */

void
dump_immediate_uses_for (FILE *file, tree var)
{
  imm_use_iterator iter;
  use_operand_p use_p;

  gcc_assert (var && TREE_CODE (var) == SSA_NAME);

  print_generic_expr (file, var, TDF_SLIM);
  fprintf (file, " : -->");
  if (has_zero_uses (var))
    fprintf (file, " no uses.\n");
  else
    if (has_single_use (var))
      fprintf (file, " single use.\n");
    else
      fprintf (file, "%d uses.\n", num_imm_uses (var));

  FOR_EACH_IMM_USE_FAST (use_p, iter, var)
    {
      if (use_p->stmt == NULL && use_p->use == NULL)
        fprintf (file, "***end of stmt iterator marker***\n");
      else
	if (!is_gimple_reg (USE_FROM_PTR (use_p)))
	  print_generic_stmt (file, USE_STMT (use_p), TDF_VOPS);
	else
	  print_generic_stmt (file, USE_STMT (use_p), TDF_SLIM);
    }
  fprintf(file, "\n");
}


/* Dump all the immediate uses to FILE.  */

void
dump_immediate_uses (FILE *file)
{
  tree var;
  unsigned int x;

  fprintf (file, "Immediate_uses: \n\n");
  for (x = 1; x < num_ssa_names; x++)
    {
      var = ssa_name(x);
      if (!var)
        continue;
      dump_immediate_uses_for (file, var);
    }
}


/* Dump def-use edges on stderr.  */

void
debug_immediate_uses (void)
{
  dump_immediate_uses (stderr);
}


/* Dump def-use edges on stderr.  */

void
debug_immediate_uses_for (tree var)
{
  dump_immediate_uses_for (stderr, var);
}

#include "gt-tree-ssa-operands.h"
