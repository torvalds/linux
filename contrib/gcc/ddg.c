/* DDG - Data Dependence Graph implementation.
   Copyright (C) 2004, 2005, 2006
   Free Software Foundation, Inc.
   Contributed by Ayal Zaks and Mustafa Hagog <zaks,mustafa@il.ibm.com>

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
#include "toplev.h"
#include "rtl.h"
#include "tm_p.h"
#include "hard-reg-set.h"
#include "regs.h"
#include "function.h"
#include "flags.h"
#include "insn-config.h"
#include "insn-attr.h"
#include "except.h"
#include "recog.h"
#include "sched-int.h"
#include "target.h"
#include "cfglayout.h"
#include "cfgloop.h"
#include "sbitmap.h"
#include "expr.h"
#include "bitmap.h"
#include "df.h"
#include "ddg.h"

/* A flag indicating that a ddg edge belongs to an SCC or not.  */
enum edge_flag {NOT_IN_SCC = 0, IN_SCC};

/* Forward declarations.  */
static void add_backarc_to_ddg (ddg_ptr, ddg_edge_ptr);
static void add_backarc_to_scc (ddg_scc_ptr, ddg_edge_ptr);
static void add_scc_to_ddg (ddg_all_sccs_ptr, ddg_scc_ptr);
static void create_ddg_dependence (ddg_ptr, ddg_node_ptr, ddg_node_ptr, rtx);
static void create_ddg_dep_no_link (ddg_ptr, ddg_node_ptr, ddg_node_ptr,
 				    dep_type, dep_data_type, int);
static ddg_edge_ptr create_ddg_edge (ddg_node_ptr, ddg_node_ptr, dep_type,
				     dep_data_type, int, int);
static void add_edge_to_ddg (ddg_ptr g, ddg_edge_ptr);

/* Auxiliary variable for mem_read_insn_p/mem_write_insn_p.  */
static bool mem_ref_p;

/* Auxiliary function for mem_read_insn_p.  */
static int
mark_mem_use (rtx *x, void *data ATTRIBUTE_UNUSED)
{
  if (MEM_P (*x))
    mem_ref_p = true;
  return 0;
}

/* Auxiliary function for mem_read_insn_p.  */
static void
mark_mem_use_1 (rtx *x, void *data)
{
  for_each_rtx (x, mark_mem_use, data);
}

/* Returns nonzero if INSN reads from memory.  */
static bool
mem_read_insn_p (rtx insn)
{
  mem_ref_p = false;
  note_uses (&PATTERN (insn), mark_mem_use_1, NULL);
  return mem_ref_p;
}

static void
mark_mem_store (rtx loc, rtx setter ATTRIBUTE_UNUSED, void *data ATTRIBUTE_UNUSED)
{
  if (MEM_P (loc))
    mem_ref_p = true;
}

/* Returns nonzero if INSN writes to memory.  */
static bool
mem_write_insn_p (rtx insn)
{
  mem_ref_p = false;
  note_stores (PATTERN (insn), mark_mem_store, NULL);
  return mem_ref_p;
}

/* Returns nonzero if X has access to memory.  */
static bool
rtx_mem_access_p (rtx x)
{
  int i, j;
  const char *fmt;
  enum rtx_code code;

  if (x == 0)
    return false;

  if (MEM_P (x))
    return true;

  code = GET_CODE (x);
  fmt = GET_RTX_FORMAT (code);
  for (i = GET_RTX_LENGTH (code) - 1; i >= 0; i--)
    {
      if (fmt[i] == 'e')
	{
	  if (rtx_mem_access_p (XEXP (x, i)))
            return true;
        }
      else if (fmt[i] == 'E')
	for (j = 0; j < XVECLEN (x, i); j++)
	  {
	    if (rtx_mem_access_p (XVECEXP (x, i, j)))
              return true;
          }
    }
  return false;
}

/* Returns nonzero if INSN reads to or writes from memory.  */
static bool
mem_access_insn_p (rtx insn)
{
  return rtx_mem_access_p (PATTERN (insn));
}

/* Computes the dependence parameters (latency, distance etc.), creates
   a ddg_edge and adds it to the given DDG.  */
static void
create_ddg_dependence (ddg_ptr g, ddg_node_ptr src_node,
		       ddg_node_ptr dest_node, rtx link)
{
  ddg_edge_ptr e;
  int latency, distance = 0;
  int interloop = (src_node->cuid >= dest_node->cuid);
  dep_type t = TRUE_DEP;
  dep_data_type dt = (mem_access_insn_p (src_node->insn)
		      && mem_access_insn_p (dest_node->insn) ? MEM_DEP
							     : REG_DEP);

  /* For now we don't have an exact calculation of the distance,
     so assume 1 conservatively.  */
  if (interloop)
     distance = 1;

  gcc_assert (link);

  /* Note: REG_DEP_ANTI applies to MEM ANTI_DEP as well!!  */
  if (REG_NOTE_KIND (link) == REG_DEP_ANTI)
    t = ANTI_DEP;
  else if (REG_NOTE_KIND (link) == REG_DEP_OUTPUT)
    t = OUTPUT_DEP;
  latency = insn_cost (src_node->insn, link, dest_node->insn);

  e = create_ddg_edge (src_node, dest_node, t, dt, latency, distance);

  if (interloop)
    {
      /* Some interloop dependencies are relaxed:
	 1. Every insn is output dependent on itself; ignore such deps.
	 2. Every true/flow dependence is an anti dependence in the
	 opposite direction with distance 1; such register deps
	 will be removed by renaming if broken --- ignore them.  */
      if (!(t == OUTPUT_DEP && src_node == dest_node)
	  && !(t == ANTI_DEP && dt == REG_DEP))
	add_backarc_to_ddg (g, e);
      else
	free (e);
    }
  else if (t == ANTI_DEP && dt == REG_DEP)
    free (e);  /* We can fix broken anti register deps using reg-moves.  */
  else
    add_edge_to_ddg (g, e);
}

/* The same as the above function, but it doesn't require a link parameter.  */
static void
create_ddg_dep_no_link (ddg_ptr g, ddg_node_ptr from, ddg_node_ptr to,
			dep_type d_t, dep_data_type d_dt, int distance)
{
  ddg_edge_ptr e;
  int l;
  rtx link = alloc_INSN_LIST (to->insn, NULL_RTX);

  if (d_t == ANTI_DEP)
    PUT_REG_NOTE_KIND (link, REG_DEP_ANTI);
  else if (d_t == OUTPUT_DEP)
    PUT_REG_NOTE_KIND (link, REG_DEP_OUTPUT);

  l = insn_cost (from->insn, link, to->insn);
  free_INSN_LIST_node (link);

  e = create_ddg_edge (from, to, d_t, d_dt, l, distance);
  if (distance > 0)
    add_backarc_to_ddg (g, e);
  else
    add_edge_to_ddg (g, e);
}


/* Given a downwards exposed register def RD, add inter-loop true dependences
   for all its uses in the next iteration, and an output dependence to the
   first def of the next iteration.  */
static void
add_deps_for_def (ddg_ptr g, struct df *df, struct df_ref *rd)
{
  int regno = DF_REF_REGNO (rd);
  struct df_ru_bb_info *bb_info = DF_RU_BB_INFO (df, g->bb);
  struct df_link *r_use;
  int use_before_def = false;
  rtx def_insn = DF_REF_INSN (rd);
  ddg_node_ptr src_node = get_node_of_insn (g, def_insn);

  /* Create and inter-loop true dependence between RD and each of its uses
     that is upwards exposed in RD's block.  */
  for (r_use = DF_REF_CHAIN (rd); r_use != NULL; r_use = r_use->next)
    {
      if (bitmap_bit_p (bb_info->gen, r_use->ref->id))
	{
	  rtx use_insn = DF_REF_INSN (r_use->ref);
	  ddg_node_ptr dest_node = get_node_of_insn (g, use_insn);

	  gcc_assert (src_node && dest_node);

	  /* Any such upwards exposed use appears before the rd def.  */
	  use_before_def = true;
	  create_ddg_dep_no_link (g, src_node, dest_node, TRUE_DEP,
				  REG_DEP, 1);
	}
    }

  /* Create an inter-loop output dependence between RD (which is the
     last def in its block, being downwards exposed) and the first def
     in its block.  Avoid creating a self output dependence.  Avoid creating
     an output dependence if there is a dependence path between the two defs
     starting with a true dependence followed by an anti dependence (i.e. if
     there is a use between the two defs.  */
  if (! use_before_def)
    {
      struct df_ref *def = df_bb_regno_first_def_find (df, g->bb, regno);
      int i;
      ddg_node_ptr dest_node;

      if (!def || rd->id == def->id)
	return;

      /* Check if there are uses after RD.  */
      for (i = src_node->cuid + 1; i < g->num_nodes; i++)
	 if (df_find_use (df, g->nodes[i].insn, rd->reg))
	   return;

      dest_node = get_node_of_insn (g, def->insn);
      create_ddg_dep_no_link (g, src_node, dest_node, OUTPUT_DEP, REG_DEP, 1);
    }
}

/* Given a register USE, add an inter-loop anti dependence to the first
   (nearest BLOCK_BEGIN) def of the next iteration, unless USE is followed
   by a def in the block.  */
static void
add_deps_for_use (ddg_ptr g, struct df *df, struct df_ref *use)
{
  int i;
  int regno = DF_REF_REGNO (use);
  struct df_ref *first_def = df_bb_regno_first_def_find (df, g->bb, regno);
  ddg_node_ptr use_node;
  ddg_node_ptr def_node;
  struct df_rd_bb_info *bb_info;

  bb_info = DF_RD_BB_INFO (df, g->bb);

  if (!first_def)
    return;

  use_node = get_node_of_insn (g, use->insn);
  def_node = get_node_of_insn (g, first_def->insn);

  gcc_assert (use_node && def_node);

  /* Make sure there are no defs after USE.  */
  for (i = use_node->cuid + 1; i < g->num_nodes; i++)
     if (df_find_def (df, g->nodes[i].insn, use->reg))
       return;
  /* We must not add ANTI dep when there is an intra-loop TRUE dep in
     the opposite direction. If the first_def reaches the USE then there is
     such a dep.  */
  if (! bitmap_bit_p (bb_info->gen, first_def->id))
    create_ddg_dep_no_link (g, use_node, def_node, ANTI_DEP, REG_DEP, 1);
}

/* Build inter-loop dependencies, by looking at DF analysis backwards.  */
static void
build_inter_loop_deps (ddg_ptr g, struct df *df)
{
  unsigned rd_num, u_num;
  struct df_rd_bb_info *rd_bb_info;
  struct df_ru_bb_info *ru_bb_info;
  bitmap_iterator bi;

  rd_bb_info = DF_RD_BB_INFO (df, g->bb);

  /* Find inter-loop output and true deps by connecting downward exposed defs
     to the first def of the BB and to upwards exposed uses.  */
  EXECUTE_IF_SET_IN_BITMAP (rd_bb_info->gen, 0, rd_num, bi)
    {
      struct df_ref *rd = DF_DEFS_GET (df, rd_num);

      add_deps_for_def (g, df, rd);
    }

  ru_bb_info = DF_RU_BB_INFO (df, g->bb);

  /* Find inter-loop anti deps.  We are interested in uses of the block that
     appear below all defs; this implies that these uses are killed.  */
  EXECUTE_IF_SET_IN_BITMAP (ru_bb_info->kill, 0, u_num, bi)
    {
      struct df_ref *use = DF_USES_GET (df, u_num);

      /* We are interested in uses of this BB.  */
      if (BLOCK_FOR_INSN (use->insn) == g->bb)
      	add_deps_for_use (g, df, use);
    }
}

/* Given two nodes, analyze their RTL insns and add inter-loop mem deps
   to ddg G.  */
static void
add_inter_loop_mem_dep (ddg_ptr g, ddg_node_ptr from, ddg_node_ptr to)
{
  if (mem_write_insn_p (from->insn))
    {
      if (mem_read_insn_p (to->insn))
  	create_ddg_dep_no_link (g, from, to, TRUE_DEP, MEM_DEP, 1);
      else if (from->cuid != to->cuid)
  	create_ddg_dep_no_link (g, from, to, OUTPUT_DEP, MEM_DEP, 1);
    }
  else
    {
      if (mem_read_insn_p (to->insn))
	return;
      else if (from->cuid != to->cuid)
	{
  	  create_ddg_dep_no_link (g, from, to, ANTI_DEP, MEM_DEP, 1);
  	  create_ddg_dep_no_link (g, to, from, TRUE_DEP, MEM_DEP, 1);
	}
    }

}

/* Perform intra-block Data Dependency analysis and connect the nodes in
   the DDG.  We assume the loop has a single basic block.  */
static void
build_intra_loop_deps (ddg_ptr g)
{
  int i;
  /* Hold the dependency analysis state during dependency calculations.  */
  struct deps tmp_deps;
  rtx head, tail, link;

  /* Build the dependence information, using the sched_analyze function.  */
  init_deps_global ();
  init_deps (&tmp_deps);

  /* Do the intra-block data dependence analysis for the given block.  */
  get_ebb_head_tail (g->bb, g->bb, &head, &tail);
  sched_analyze (&tmp_deps, head, tail);

  /* Build intra-loop data dependencies using the scheduler dependency
     analysis.  */
  for (i = 0; i < g->num_nodes; i++)
    {
      ddg_node_ptr dest_node = &g->nodes[i];

      if (! INSN_P (dest_node->insn))
	continue;

      for (link = LOG_LINKS (dest_node->insn); link; link = XEXP (link, 1))
	{
	  ddg_node_ptr src_node = get_node_of_insn (g, XEXP (link, 0));

	  if (!src_node)
	    continue;

      	  add_forw_dep (dest_node->insn, link);
	  create_ddg_dependence (g, src_node, dest_node,
				 INSN_DEPEND (src_node->insn));
	}

      /* If this insn modifies memory, add an edge to all insns that access
	 memory.  */
      if (mem_access_insn_p (dest_node->insn))
	{
	  int j;

	  for (j = 0; j <= i; j++)
	    {
	      ddg_node_ptr j_node = &g->nodes[j];
	      if (mem_access_insn_p (j_node->insn))
 		/* Don't bother calculating inter-loop dep if an intra-loop dep
		   already exists.  */
	      	  if (! TEST_BIT (dest_node->successors, j))
		    add_inter_loop_mem_dep (g, dest_node, j_node);
            }
        }
    }

  /* Free the INSN_LISTs.  */
  finish_deps_global ();
  free_deps (&tmp_deps);
}


/* Given a basic block, create its DDG and return a pointer to a variable
   of ddg type that represents it.
   Initialize the ddg structure fields to the appropriate values.  */
ddg_ptr
create_ddg (basic_block bb, struct df *df, int closing_branch_deps)
{
  ddg_ptr g;
  rtx insn, first_note;
  int i;
  int num_nodes = 0;

  g = (ddg_ptr) xcalloc (1, sizeof (struct ddg));

  g->bb = bb;
  g->closing_branch_deps = closing_branch_deps;

  /* Count the number of insns in the BB.  */
  for (insn = BB_HEAD (bb); insn != NEXT_INSN (BB_END (bb));
       insn = NEXT_INSN (insn))
    {
      if (! INSN_P (insn) || GET_CODE (PATTERN (insn)) == USE)
	continue;

      if (mem_read_insn_p (insn))
	g->num_loads++;
      if (mem_write_insn_p (insn))
	g->num_stores++;
      num_nodes++;
    }

  /* There is nothing to do for this BB.  */
  if (num_nodes <= 1)
    {
      free (g);
      return NULL;
    }

  /* Allocate the nodes array, and initialize the nodes.  */
  g->num_nodes = num_nodes;
  g->nodes = (ddg_node_ptr) xcalloc (num_nodes, sizeof (struct ddg_node));
  g->closing_branch = NULL;
  i = 0;
  first_note = NULL_RTX;
  for (insn = BB_HEAD (bb); insn != NEXT_INSN (BB_END (bb));
       insn = NEXT_INSN (insn))
    {
      if (! INSN_P (insn))
	{
	  if (! first_note && NOTE_P (insn)
	      && NOTE_LINE_NUMBER (insn) !=  NOTE_INSN_BASIC_BLOCK)
	    first_note = insn;
	  continue;
	}
      if (JUMP_P (insn))
	{
	  gcc_assert (!g->closing_branch);
	  g->closing_branch = &g->nodes[i];
	}
      else if (GET_CODE (PATTERN (insn)) == USE)
	{
	  if (! first_note)
	    first_note = insn;
	  continue;
	}

      g->nodes[i].cuid = i;
      g->nodes[i].successors = sbitmap_alloc (num_nodes);
      sbitmap_zero (g->nodes[i].successors);
      g->nodes[i].predecessors = sbitmap_alloc (num_nodes);
      sbitmap_zero (g->nodes[i].predecessors);
      g->nodes[i].first_note = (first_note ? first_note : insn);
      g->nodes[i++].insn = insn;
      first_note = NULL_RTX;
    }
  
  /* We must have found a branch in DDG.  */
  gcc_assert (g->closing_branch);
  

  /* Build the data dependency graph.  */
  build_intra_loop_deps (g);
  build_inter_loop_deps (g, df);
  return g;
}

/* Free all the memory allocated for the DDG.  */
void
free_ddg (ddg_ptr g)
{
  int i;

  if (!g)
    return;

  for (i = 0; i < g->num_nodes; i++)
    {
      ddg_edge_ptr e = g->nodes[i].out;

      while (e)
	{
	  ddg_edge_ptr next = e->next_out;

	  free (e);
	  e = next;
	}
      sbitmap_free (g->nodes[i].successors);
      sbitmap_free (g->nodes[i].predecessors);
    }
  if (g->num_backarcs > 0)
    free (g->backarcs);
  free (g->nodes);
  free (g);
}

void
print_ddg_edge (FILE *file, ddg_edge_ptr e)
{
  char dep_c;

  switch (e->type) {
    case OUTPUT_DEP :
      dep_c = 'O';
      break;
    case ANTI_DEP :
      dep_c = 'A';
      break;
    default:
      dep_c = 'T';
  }

  fprintf (file, " [%d -(%c,%d,%d)-> %d] ", INSN_UID (e->src->insn),
	   dep_c, e->latency, e->distance, INSN_UID (e->dest->insn));
}

/* Print the DDG nodes with there in/out edges to the dump file.  */
void
print_ddg (FILE *file, ddg_ptr g)
{
  int i;

  for (i = 0; i < g->num_nodes; i++)
    {
      ddg_edge_ptr e;

      print_rtl_single (file, g->nodes[i].insn);
      fprintf (file, "OUT ARCS: ");
      for (e = g->nodes[i].out; e; e = e->next_out)
	print_ddg_edge (file, e);

      fprintf (file, "\nIN ARCS: ");
      for (e = g->nodes[i].in; e; e = e->next_in)
	print_ddg_edge (file, e);

      fprintf (file, "\n");
    }
}

/* Print the given DDG in VCG format.  */
void
vcg_print_ddg (FILE *file, ddg_ptr g)
{
  int src_cuid;

  fprintf (file, "graph: {\n");
  for (src_cuid = 0; src_cuid < g->num_nodes; src_cuid++)
    {
      ddg_edge_ptr e;
      int src_uid = INSN_UID (g->nodes[src_cuid].insn);

      fprintf (file, "node: {title: \"%d_%d\" info1: \"", src_cuid, src_uid);
      print_rtl_single (file, g->nodes[src_cuid].insn);
      fprintf (file, "\"}\n");
      for (e = g->nodes[src_cuid].out; e; e = e->next_out)
	{
	  int dst_uid = INSN_UID (e->dest->insn);
	  int dst_cuid = e->dest->cuid;

	  /* Give the backarcs a different color.  */
	  if (e->distance > 0)
	    fprintf (file, "backedge: {color: red ");
	  else
	    fprintf (file, "edge: { ");

	  fprintf (file, "sourcename: \"%d_%d\" ", src_cuid, src_uid);
	  fprintf (file, "targetname: \"%d_%d\" ", dst_cuid, dst_uid);
	  fprintf (file, "label: \"%d_%d\"}\n", e->latency, e->distance);
	}
    }
  fprintf (file, "}\n");
}

/* Create an edge and initialize it with given values.  */
static ddg_edge_ptr
create_ddg_edge (ddg_node_ptr src, ddg_node_ptr dest,
		 dep_type t, dep_data_type dt, int l, int d)
{
  ddg_edge_ptr e = (ddg_edge_ptr) xmalloc (sizeof (struct ddg_edge));

  e->src = src;
  e->dest = dest;
  e->type = t;
  e->data_type = dt;
  e->latency = l;
  e->distance = d;
  e->next_in = e->next_out = NULL;
  e->aux.info = 0;
  return e;
}

/* Add the given edge to the in/out linked lists of the DDG nodes.  */
static void
add_edge_to_ddg (ddg_ptr g ATTRIBUTE_UNUSED, ddg_edge_ptr e)
{
  ddg_node_ptr src = e->src;
  ddg_node_ptr dest = e->dest;

  /* Should have allocated the sbitmaps.  */
  gcc_assert (src->successors && dest->predecessors);

  SET_BIT (src->successors, dest->cuid);
  SET_BIT (dest->predecessors, src->cuid);
  e->next_in = dest->in;
  dest->in = e;
  e->next_out = src->out;
  src->out = e;
}



/* Algorithm for computing the recurrence_length of an scc.  We assume at
   for now that cycles in the data dependence graph contain a single backarc.
   This simplifies the algorithm, and can be generalized later.  */
static void
set_recurrence_length (ddg_scc_ptr scc, ddg_ptr g)
{
  int j;
  int result = -1;

  for (j = 0; j < scc->num_backarcs; j++)
    {
      ddg_edge_ptr backarc = scc->backarcs[j];
      int length;
      int distance = backarc->distance;
      ddg_node_ptr src = backarc->dest;
      ddg_node_ptr dest = backarc->src;

      length = longest_simple_path (g, src->cuid, dest->cuid, scc->nodes);
      if (length < 0 )
	{
	  /* fprintf (stderr, "Backarc not on simple cycle in SCC.\n"); */
	  continue;
	}
      length += backarc->latency;
      result = MAX (result, (length / distance));
    }
  scc->recurrence_length = result;
}

/* Create a new SCC given the set of its nodes.  Compute its recurrence_length
   and mark edges that belong to this scc as IN_SCC.  */
static ddg_scc_ptr
create_scc (ddg_ptr g, sbitmap nodes)
{
  ddg_scc_ptr scc;
  unsigned int u = 0;
  sbitmap_iterator sbi;

  scc = (ddg_scc_ptr) xmalloc (sizeof (struct ddg_scc));
  scc->backarcs = NULL;
  scc->num_backarcs = 0;
  scc->nodes = sbitmap_alloc (g->num_nodes);
  sbitmap_copy (scc->nodes, nodes);

  /* Mark the backarcs that belong to this SCC.  */
  EXECUTE_IF_SET_IN_SBITMAP (nodes, 0, u, sbi)
    {
      ddg_edge_ptr e;
      ddg_node_ptr n = &g->nodes[u];

      for (e = n->out; e; e = e->next_out)
	if (TEST_BIT (nodes, e->dest->cuid))
	  {
	    e->aux.count = IN_SCC;
	    if (e->distance > 0)
	      add_backarc_to_scc (scc, e);
	  }
    }

  set_recurrence_length (scc, g);
  return scc;
}

/* Cleans the memory allocation of a given SCC.  */
static void
free_scc (ddg_scc_ptr scc)
{
  if (!scc)
    return;

  sbitmap_free (scc->nodes);
  if (scc->num_backarcs > 0)
    free (scc->backarcs);
  free (scc);
}


/* Add a given edge known to be a backarc to the given DDG.  */
static void
add_backarc_to_ddg (ddg_ptr g, ddg_edge_ptr e)
{
  int size = (g->num_backarcs + 1) * sizeof (ddg_edge_ptr);

  add_edge_to_ddg (g, e);
  g->backarcs = (ddg_edge_ptr *) xrealloc (g->backarcs, size);
  g->backarcs[g->num_backarcs++] = e;
}

/* Add backarc to an SCC.  */
static void
add_backarc_to_scc (ddg_scc_ptr scc, ddg_edge_ptr e)
{
  int size = (scc->num_backarcs + 1) * sizeof (ddg_edge_ptr);

  scc->backarcs = (ddg_edge_ptr *) xrealloc (scc->backarcs, size);
  scc->backarcs[scc->num_backarcs++] = e;
}

/* Add the given SCC to the DDG.  */
static void
add_scc_to_ddg (ddg_all_sccs_ptr g, ddg_scc_ptr scc)
{
  int size = (g->num_sccs + 1) * sizeof (ddg_scc_ptr);

  g->sccs = (ddg_scc_ptr *) xrealloc (g->sccs, size);
  g->sccs[g->num_sccs++] = scc;
}

/* Given the instruction INSN return the node that represents it.  */
ddg_node_ptr
get_node_of_insn (ddg_ptr g, rtx insn)
{
  int i;

  for (i = 0; i < g->num_nodes; i++)
    if (insn == g->nodes[i].insn)
      return &g->nodes[i];
  return NULL;
}

/* Given a set OPS of nodes in the DDG, find the set of their successors
   which are not in OPS, and set their bits in SUCC.  Bits corresponding to
   OPS are cleared from SUCC.  Leaves the other bits in SUCC unchanged.  */
void
find_successors (sbitmap succ, ddg_ptr g, sbitmap ops)
{
  unsigned int i = 0;
  sbitmap_iterator sbi;

  EXECUTE_IF_SET_IN_SBITMAP (ops, 0, i, sbi)
    {
      const sbitmap node_succ = NODE_SUCCESSORS (&g->nodes[i]);
      sbitmap_a_or_b (succ, succ, node_succ);
    };

  /* We want those that are not in ops.  */
  sbitmap_difference (succ, succ, ops);
}

/* Given a set OPS of nodes in the DDG, find the set of their predecessors
   which are not in OPS, and set their bits in PREDS.  Bits corresponding to
   OPS are cleared from PREDS.  Leaves the other bits in PREDS unchanged.  */
void
find_predecessors (sbitmap preds, ddg_ptr g, sbitmap ops)
{
  unsigned int i = 0;
  sbitmap_iterator sbi;

  EXECUTE_IF_SET_IN_SBITMAP (ops, 0, i, sbi)
    {
      const sbitmap node_preds = NODE_PREDECESSORS (&g->nodes[i]);
      sbitmap_a_or_b (preds, preds, node_preds);
    };

  /* We want those that are not in ops.  */
  sbitmap_difference (preds, preds, ops);
}


/* Compare function to be passed to qsort to order the backarcs in descending
   recMII order.  */
static int
compare_sccs (const void *s1, const void *s2)
{
  int rec_l1 = (*(ddg_scc_ptr *)s1)->recurrence_length;
  int rec_l2 = (*(ddg_scc_ptr *)s2)->recurrence_length; 
  return ((rec_l2 > rec_l1) - (rec_l2 < rec_l1));
	  
}

/* Order the backarcs in descending recMII order using compare_sccs.  */
static void
order_sccs (ddg_all_sccs_ptr g)
{
  qsort (g->sccs, g->num_sccs, sizeof (ddg_scc_ptr),
	 (int (*) (const void *, const void *)) compare_sccs);
}

/* Perform the Strongly Connected Components decomposing algorithm on the
   DDG and return DDG_ALL_SCCS structure that contains them.  */
ddg_all_sccs_ptr
create_ddg_all_sccs (ddg_ptr g)
{
  int i;
  int num_nodes = g->num_nodes;
  sbitmap from = sbitmap_alloc (num_nodes);
  sbitmap to = sbitmap_alloc (num_nodes);
  sbitmap scc_nodes = sbitmap_alloc (num_nodes);
  ddg_all_sccs_ptr sccs = (ddg_all_sccs_ptr)
			  xmalloc (sizeof (struct ddg_all_sccs));

  sccs->ddg = g;
  sccs->sccs = NULL;
  sccs->num_sccs = 0;

  for (i = 0; i < g->num_backarcs; i++)
    {
      ddg_scc_ptr  scc;
      ddg_edge_ptr backarc = g->backarcs[i];
      ddg_node_ptr src = backarc->src;
      ddg_node_ptr dest = backarc->dest;

      /* If the backarc already belongs to an SCC, continue.  */
      if (backarc->aux.count == IN_SCC)
	continue;

      sbitmap_zero (from);
      sbitmap_zero (to);
      SET_BIT (from, dest->cuid);
      SET_BIT (to, src->cuid);

      if (find_nodes_on_paths (scc_nodes, g, from, to))
	{
	  scc = create_scc (g, scc_nodes);
	  add_scc_to_ddg (sccs, scc);
	}
    }
  order_sccs (sccs);
  sbitmap_free (from);
  sbitmap_free (to);
  sbitmap_free (scc_nodes);
  return sccs;
}

/* Frees the memory allocated for all SCCs of the DDG, but keeps the DDG.  */
void
free_ddg_all_sccs (ddg_all_sccs_ptr all_sccs)
{
  int i;

  if (!all_sccs)
    return;

  for (i = 0; i < all_sccs->num_sccs; i++)
    free_scc (all_sccs->sccs[i]);

  free (all_sccs);
}


/* Given FROM - a bitmap of source nodes - and TO - a bitmap of destination
   nodes - find all nodes that lie on paths from FROM to TO (not excluding
   nodes from FROM and TO).  Return nonzero if nodes exist.  */
int
find_nodes_on_paths (sbitmap result, ddg_ptr g, sbitmap from, sbitmap to)
{
  int answer;
  int change;
  unsigned int u = 0;
  int num_nodes = g->num_nodes;
  sbitmap_iterator sbi;

  sbitmap workset = sbitmap_alloc (num_nodes);
  sbitmap reachable_from = sbitmap_alloc (num_nodes);
  sbitmap reach_to = sbitmap_alloc (num_nodes);
  sbitmap tmp = sbitmap_alloc (num_nodes);

  sbitmap_copy (reachable_from, from);
  sbitmap_copy (tmp, from);

  change = 1;
  while (change)
    {
      change = 0;
      sbitmap_copy (workset, tmp);
      sbitmap_zero (tmp);
      EXECUTE_IF_SET_IN_SBITMAP (workset, 0, u, sbi)
	{
	  ddg_edge_ptr e;
	  ddg_node_ptr u_node = &g->nodes[u];

	  for (e = u_node->out; e != (ddg_edge_ptr) 0; e = e->next_out)
	    {
	      ddg_node_ptr v_node = e->dest;
	      int v = v_node->cuid;

	      if (!TEST_BIT (reachable_from, v))
		{
		  SET_BIT (reachable_from, v);
		  SET_BIT (tmp, v);
		  change = 1;
		}
	    }
	}
    }

  sbitmap_copy (reach_to, to);
  sbitmap_copy (tmp, to);

  change = 1;
  while (change)
    {
      change = 0;
      sbitmap_copy (workset, tmp);
      sbitmap_zero (tmp);
      EXECUTE_IF_SET_IN_SBITMAP (workset, 0, u, sbi)
	{
	  ddg_edge_ptr e;
	  ddg_node_ptr u_node = &g->nodes[u];

	  for (e = u_node->in; e != (ddg_edge_ptr) 0; e = e->next_in)
	    {
	      ddg_node_ptr v_node = e->src;
	      int v = v_node->cuid;

	      if (!TEST_BIT (reach_to, v))
		{
		  SET_BIT (reach_to, v);
		  SET_BIT (tmp, v);
		  change = 1;
		}
	    }
	}
    }

  answer = sbitmap_a_and_b_cg (result, reachable_from, reach_to);
  sbitmap_free (workset);
  sbitmap_free (reachable_from);
  sbitmap_free (reach_to);
  sbitmap_free (tmp);
  return answer;
}


/* Updates the counts of U_NODE's successors (that belong to NODES) to be
   at-least as large as the count of U_NODE plus the latency between them.
   Sets a bit in TMP for each successor whose count was changed (increased).
   Returns nonzero if any count was changed.  */
static int
update_dist_to_successors (ddg_node_ptr u_node, sbitmap nodes, sbitmap tmp)
{
  ddg_edge_ptr e;
  int result = 0;

  for (e = u_node->out; e; e = e->next_out)
    {
      ddg_node_ptr v_node = e->dest;
      int v = v_node->cuid;

      if (TEST_BIT (nodes, v)
	  && (e->distance == 0)
	  && (v_node->aux.count < u_node->aux.count + e->latency))
	{
	  v_node->aux.count = u_node->aux.count + e->latency;
	  SET_BIT (tmp, v);
	  result = 1;
	}
    }
  return result;
}


/* Find the length of a longest path from SRC to DEST in G,
   going only through NODES, and disregarding backarcs.  */
int
longest_simple_path (struct ddg * g, int src, int dest, sbitmap nodes)
{
  int i;
  unsigned int u = 0;
  int change = 1;
  int result;
  int num_nodes = g->num_nodes;
  sbitmap workset = sbitmap_alloc (num_nodes);
  sbitmap tmp = sbitmap_alloc (num_nodes);


  /* Data will hold the distance of the longest path found so far from
     src to each node.  Initialize to -1 = less than minimum.  */
  for (i = 0; i < g->num_nodes; i++)
    g->nodes[i].aux.count = -1;
  g->nodes[src].aux.count = 0;

  sbitmap_zero (tmp);
  SET_BIT (tmp, src);

  while (change)
    {
      sbitmap_iterator sbi;

      change = 0;
      sbitmap_copy (workset, tmp);
      sbitmap_zero (tmp);
      EXECUTE_IF_SET_IN_SBITMAP (workset, 0, u, sbi)
	{
	  ddg_node_ptr u_node = &g->nodes[u];

	  change |= update_dist_to_successors (u_node, nodes, tmp);
	}
    }
  result = g->nodes[dest].aux.count;
  sbitmap_free (workset);
  sbitmap_free (tmp);
  return result;
}
