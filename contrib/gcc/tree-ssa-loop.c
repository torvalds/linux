/* Loop optimizations over tree-ssa.
   Copyright (C) 2003, 2005 Free Software Foundation, Inc.
   
This file is part of GCC.
   
GCC is free software; you can redistribute it and/or modify it
under the terms of the GNU General Public License as published by the
Free Software Foundation; either version 2, or (at your option) any
later version.
   
GCC is distributed in the hope that it will be useful, but WITHOUT
ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
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
#include "rtl.h"
#include "tm_p.h"
#include "hard-reg-set.h"
#include "basic-block.h"
#include "output.h"
#include "diagnostic.h"
#include "tree-flow.h"
#include "tree-dump.h"
#include "tree-pass.h"
#include "timevar.h"
#include "cfgloop.h"
#include "flags.h"
#include "tree-inline.h"
#include "tree-scalar-evolution.h"

/* The loop tree currently optimized.  */

struct loops *current_loops = NULL;

/* Initializes the loop structures.  */

static struct loops *
tree_loop_optimizer_init (void)
{
  struct loops *loops;
 
  loops = loop_optimizer_init (LOOPS_NORMAL
			       | LOOPS_HAVE_MARKED_SINGLE_EXITS);

  if (!loops)
    return NULL;

  rewrite_into_loop_closed_ssa (NULL, TODO_update_ssa);

  return loops;
}

/* The loop superpass.  */

static bool
gate_tree_loop (void)
{
  return flag_tree_loop_optimize != 0;
}

struct tree_opt_pass pass_tree_loop = 
{
  "loop",				/* name */
  gate_tree_loop,			/* gate */
  NULL,					/* execute */
  NULL,					/* sub */
  NULL,					/* next */
  0,					/* static_pass_number */
  TV_TREE_LOOP,				/* tv_id */
  PROP_cfg,				/* properties_required */
  0,					/* properties_provided */
  0,					/* properties_destroyed */
  TODO_ggc_collect,			/* todo_flags_start */
  TODO_dump_func | TODO_verify_ssa | TODO_ggc_collect,	/* todo_flags_finish */
  0					/* letter */
};

/* Loop optimizer initialization.  */

static unsigned int
tree_ssa_loop_init (void)
{
  current_loops = tree_loop_optimizer_init ();
  if (!current_loops)
    return 0;

  scev_initialize (current_loops);
  return 0;
}
  
struct tree_opt_pass pass_tree_loop_init = 
{
  "loopinit",				/* name */
  NULL,					/* gate */
  tree_ssa_loop_init,			/* execute */
  NULL,					/* sub */
  NULL,					/* next */
  0,					/* static_pass_number */
  TV_TREE_LOOP_INIT,			/* tv_id */
  PROP_cfg,				/* properties_required */
  0,					/* properties_provided */
  0,					/* properties_destroyed */
  0,					/* todo_flags_start */
  TODO_dump_func | TODO_verify_loops,	/* todo_flags_finish */
  0					/* letter */
};

/* Loop invariant motion pass.  */

static unsigned int
tree_ssa_loop_im (void)
{
  if (!current_loops)
    return 0;

  tree_ssa_lim (current_loops);
  return 0;
}

static bool
gate_tree_ssa_loop_im (void)
{
  return flag_tree_loop_im != 0;
}

struct tree_opt_pass pass_lim = 
{
  "lim",				/* name */
  gate_tree_ssa_loop_im,		/* gate */
  tree_ssa_loop_im,			/* execute */
  NULL,					/* sub */
  NULL,					/* next */
  0,					/* static_pass_number */
  TV_LIM,				/* tv_id */
  PROP_cfg,				/* properties_required */
  0,					/* properties_provided */
  0,					/* properties_destroyed */
  0,					/* todo_flags_start */
  TODO_dump_func | TODO_verify_loops,	/* todo_flags_finish */
  0					/* letter */
};

/* Loop unswitching pass.  */

static unsigned int
tree_ssa_loop_unswitch (void)
{
  if (!current_loops)
    return 0;

  return tree_ssa_unswitch_loops (current_loops);
}

static bool
gate_tree_ssa_loop_unswitch (void)
{
  return flag_unswitch_loops != 0;
}

struct tree_opt_pass pass_tree_unswitch = 
{
  "unswitch",				/* name */
  gate_tree_ssa_loop_unswitch,		/* gate */
  tree_ssa_loop_unswitch,		/* execute */
  NULL,					/* sub */
  NULL,					/* next */
  0,					/* static_pass_number */
  TV_TREE_LOOP_UNSWITCH,		/* tv_id */
  PROP_cfg,				/* properties_required */
  0,					/* properties_provided */
  0,					/* properties_destroyed */
  0,					/* todo_flags_start */
  TODO_dump_func | TODO_verify_loops,	/* todo_flags_finish */
  0					/* letter */
};

/* Loop autovectorization.  */

static unsigned int
tree_vectorize (void)
{
  vectorize_loops (current_loops);
  return 0;
}

static bool
gate_tree_vectorize (void)
{
  return flag_tree_vectorize && current_loops;
}

struct tree_opt_pass pass_vectorize =
{
  "vect",                               /* name */
  gate_tree_vectorize,                  /* gate */
  tree_vectorize,                       /* execute */
  NULL,                                 /* sub */
  NULL,                                 /* next */
  0,                                    /* static_pass_number */
  TV_TREE_VECTORIZATION,                /* tv_id */
  PROP_cfg | PROP_ssa,                  /* properties_required */
  0,                                    /* properties_provided */
  0,                                    /* properties_destroyed */
  TODO_verify_loops,			/* todo_flags_start */
  TODO_dump_func | TODO_update_ssa,	/* todo_flags_finish */
  0					/* letter */
};

/* Loop nest optimizations.  */

static unsigned int
tree_linear_transform (void)
{
  if (!current_loops)
    return 0;

  linear_transform_loops (current_loops);
  return 0;
}

static bool
gate_tree_linear_transform (void)
{
  return flag_tree_loop_linear != 0;
}

struct tree_opt_pass pass_linear_transform =
{
  "ltrans",				/* name */
  gate_tree_linear_transform,		/* gate */
  tree_linear_transform,       		/* execute */
  NULL,					/* sub */
  NULL,					/* next */
  0,					/* static_pass_number */
  TV_TREE_LINEAR_TRANSFORM,  		/* tv_id */
  PROP_cfg | PROP_ssa,			/* properties_required */
  0,					/* properties_provided */
  0,					/* properties_destroyed */
  0,					/* todo_flags_start */
  TODO_dump_func | TODO_verify_loops,	/* todo_flags_finish */
  0				        /* letter */	
};

/* Canonical induction variable creation pass.  */

static unsigned int
tree_ssa_loop_ivcanon (void)
{
  if (!current_loops)
    return 0;

  return canonicalize_induction_variables (current_loops);
}

static bool
gate_tree_ssa_loop_ivcanon (void)
{
  return flag_tree_loop_ivcanon != 0;
}

struct tree_opt_pass pass_iv_canon =
{
  "ivcanon",				/* name */
  gate_tree_ssa_loop_ivcanon,		/* gate */
  tree_ssa_loop_ivcanon,	       	/* execute */
  NULL,					/* sub */
  NULL,					/* next */
  0,					/* static_pass_number */
  TV_TREE_LOOP_IVCANON,	  		/* tv_id */
  PROP_cfg | PROP_ssa,			/* properties_required */
  0,					/* properties_provided */
  0,					/* properties_destroyed */
  0,					/* todo_flags_start */
  TODO_dump_func | TODO_verify_loops,	/* todo_flags_finish */
  0					/* letter */
};

/* Propagation of constants using scev.  */

static bool
gate_scev_const_prop (void)
{
  return true;
}

struct tree_opt_pass pass_scev_cprop =
{
  "sccp",				/* name */
  gate_scev_const_prop,			/* gate */
  scev_const_prop,	       		/* execute */
  NULL,					/* sub */
  NULL,					/* next */
  0,					/* static_pass_number */
  TV_SCEV_CONST,	  		/* tv_id */
  PROP_cfg | PROP_ssa,			/* properties_required */
  0,					/* properties_provided */
  0,					/* properties_destroyed */
  0,					/* todo_flags_start */
  TODO_dump_func | TODO_cleanup_cfg
    | TODO_update_ssa_only_virtuals,
					/* todo_flags_finish */
  0					/* letter */
};

/* Remove empty loops.  */

static unsigned int
tree_ssa_empty_loop (void)
{
  if (!current_loops)
    return 0;

  return remove_empty_loops (current_loops);
}

struct tree_opt_pass pass_empty_loop =
{
  "empty",				/* name */
  NULL,					/* gate */
  tree_ssa_empty_loop,		       	/* execute */
  NULL,					/* sub */
  NULL,					/* next */
  0,					/* static_pass_number */
  TV_COMPLETE_UNROLL,	  		/* tv_id */
  PROP_cfg | PROP_ssa,			/* properties_required */
  0,					/* properties_provided */
  0,					/* properties_destroyed */
  0,					/* todo_flags_start */
  TODO_dump_func | TODO_verify_loops,	/* todo_flags_finish */
  0					/* letter */
};

/* Record bounds on numbers of iterations of loops.  */

static unsigned int
tree_ssa_loop_bounds (void)
{
  if (!current_loops)
    return 0;

  estimate_numbers_of_iterations (current_loops);
  scev_reset ();
  return 0;
}

struct tree_opt_pass pass_record_bounds =
{
  NULL,					/* name */
  NULL,					/* gate */
  tree_ssa_loop_bounds,		       	/* execute */
  NULL,					/* sub */
  NULL,					/* next */
  0,					/* static_pass_number */
  TV_TREE_LOOP_BOUNDS,	  		/* tv_id */
  PROP_cfg | PROP_ssa,			/* properties_required */
  0,					/* properties_provided */
  0,					/* properties_destroyed */
  0,					/* todo_flags_start */
  0,			              	/* todo_flags_finish */
  0					/* letter */
};

/* Complete unrolling of loops.  */

static unsigned int
tree_complete_unroll (void)
{
  if (!current_loops)
    return 0;

  return tree_unroll_loops_completely (current_loops,
				       flag_unroll_loops
					|| flag_peel_loops
					|| optimize >= 3);
}

static bool
gate_tree_complete_unroll (void)
{
  return true;
}

struct tree_opt_pass pass_complete_unroll =
{
  "cunroll",				/* name */
  gate_tree_complete_unroll,		/* gate */
  tree_complete_unroll,		       	/* execute */
  NULL,					/* sub */
  NULL,					/* next */
  0,					/* static_pass_number */
  TV_COMPLETE_UNROLL,	  		/* tv_id */
  PROP_cfg | PROP_ssa,			/* properties_required */
  0,					/* properties_provided */
  0,					/* properties_destroyed */
  0,					/* todo_flags_start */
  TODO_dump_func | TODO_verify_loops,	/* todo_flags_finish */
  0					/* letter */
};

/* Prefetching.  */

static unsigned int
tree_ssa_loop_prefetch (void)
{
  if (!current_loops)
    return 0;

  return tree_ssa_prefetch_arrays (current_loops);
}

static bool
gate_tree_ssa_loop_prefetch (void)
{
  return flag_prefetch_loop_arrays != 0;
}

struct tree_opt_pass pass_loop_prefetch =
{
  "prefetch",				/* name */
  gate_tree_ssa_loop_prefetch,		/* gate */
  tree_ssa_loop_prefetch,	       	/* execute */
  NULL,					/* sub */
  NULL,					/* next */
  0,					/* static_pass_number */
  TV_TREE_PREFETCH,	  		/* tv_id */
  PROP_cfg | PROP_ssa,			/* properties_required */
  0,					/* properties_provided */
  0,					/* properties_destroyed */
  0,					/* todo_flags_start */
  TODO_dump_func | TODO_verify_loops,	/* todo_flags_finish */
  0					/* letter */
};

/* Induction variable optimizations.  */

static unsigned int
tree_ssa_loop_ivopts (void)
{
  if (!current_loops)
    return 0;

  tree_ssa_iv_optimize (current_loops);
  return 0;
}

static bool
gate_tree_ssa_loop_ivopts (void)
{
  return flag_ivopts != 0;
}

struct tree_opt_pass pass_iv_optimize =
{
  "ivopts",				/* name */
  gate_tree_ssa_loop_ivopts,		/* gate */
  tree_ssa_loop_ivopts,		       	/* execute */
  NULL,					/* sub */
  NULL,					/* next */
  0,					/* static_pass_number */
  TV_TREE_LOOP_IVOPTS,	  		/* tv_id */
  PROP_cfg | PROP_ssa,			/* properties_required */
  0,					/* properties_provided */
  0,					/* properties_destroyed */
  0,					/* todo_flags_start */
  TODO_dump_func
  | TODO_verify_loops
  | TODO_update_ssa,			/* todo_flags_finish */
  0					/* letter */
};

/* Loop optimizer finalization.  */

static unsigned int
tree_ssa_loop_done (void)
{
  if (!current_loops)
    return 0;

  free_numbers_of_iterations_estimates (current_loops);
  scev_finalize ();
  loop_optimizer_finalize (current_loops);
  current_loops = NULL;
  return 0;
}
  
struct tree_opt_pass pass_tree_loop_done = 
{
  "loopdone",				/* name */
  NULL,					/* gate */
  tree_ssa_loop_done,			/* execute */
  NULL,					/* sub */
  NULL,					/* next */
  0,					/* static_pass_number */
  TV_TREE_LOOP_FINI,			/* tv_id */
  PROP_cfg,				/* properties_required */
  0,					/* properties_provided */
  0,					/* properties_destroyed */
  0,					/* todo_flags_start */
  TODO_cleanup_cfg | TODO_dump_func,	/* todo_flags_finish */
  0					/* letter */
};
