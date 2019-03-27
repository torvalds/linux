/* Interprocedural analyses.
   Copyright (C) 2005 Free Software Foundation, Inc.

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

#ifndef IPA_PROP_H
#define IPA_PROP_H

#include "tree.h"

/* The following definitions and interfaces are used by
   interprocedural analyses.  */

/* A jump function for a callsite represents the values passed as actual 
   arguments of the callsite. There are three main types of values :
   Formal - the caller's formal parameter is passed as an actual argument.
   Constant - a constant is passed as a an actual argument.
   Unknown - neither of the above.
   Integer and real constants are represented as CONST_IPATYPE and Fortran 
   constants are represented as CONST_IPATYPE_REF.  */
enum jump_func_type
{
  UNKNOWN_IPATYPE,
  CONST_IPATYPE,
  CONST_IPATYPE_REF,
  FORMAL_IPATYPE
};

/* All formal parameters in the program have a cval computed by 
   the interprocedural stage of IPCP.  
   There are three main values of cval :
   TOP - unknown.
   BOTTOM - non constant.
   CONSTANT_TYPE - constant value.
   Cval of formal f will have a constant value if all callsites to this
   function have the same constant value passed to f.
   Integer and real constants are represented as CONST_IPATYPE and Fortran
   constants are represented as CONST_IPATYPE_REF.  */
enum cvalue_type
{
  BOTTOM,
  CONST_VALUE,
  CONST_VALUE_REF,
  TOP
};

/* Represents the value of either jump function or cval.
   value represents a constant.
   formal_id is used only in jump function context and represents 
   pass-through parameter (the formal of caller is passed 
   as argument).  */
union parameter_info
{
  unsigned int formal_id;
  tree value;
};

/* A jump function for a callsite represents the values passed as actual 
   arguments of the callsite. See enum jump_func_type for the various 
   types of jump functions supported.  */
struct ipa_jump_func
{
  enum jump_func_type type;
  union parameter_info info_type;
};

/* All formal parameters in the program have a cval computed by 
   the interprocedural stage of IPCP. See enum cvalue_type for 
   the various types of cvals supported */
struct ipcp_formal
{
  enum cvalue_type cval_type;
  union parameter_info cvalue;
};

/* Represent which DECL tree (or reference to such tree)
   will be replaced by another tree while versioning.  */
struct ipa_replace_map
{
  /* The tree that will be replaced.  */
  tree old_tree;
  /* The new (replacing) tree.  */ 
  tree new_tree;
  /* True when a substitution should be done, false otherwise.  */
  bool replace_p;
  /* True when we replace a reference to old_tree.  */
  bool ref_p;
};

/* Return the field in cgraph_node/cgraph_edge struct that points
   to ipa_node/ipa_edge struct.  */
#define IPA_NODE_REF(MT) ((struct ipa_node *)(MT)->aux)
#define IPA_EDGE_REF(EDGE) ((struct ipa_edge *)(EDGE)->aux)

/* ipa_node stores information related to a method and
   its formal parameters. It is pointed to by a field in the
   method's corresponding cgraph_node.

   ipa_edge stores information related to a callsite and
   its arguments. It is pointed to by a field in the
   callsite's corresponding cgraph_edge.  */
struct ipa_node
{
  /* Number of formal parameters of this method.  When set to 0,
     this method's parameters would not be analyzed by the different
     stages of IPA CP.  */
  int ipa_arg_num;
  /* Array of cvals.  */
  struct ipcp_formal *ipcp_cval;
  /* Mapping each parameter to its PARM_DECL tree.  */
  tree *ipa_param_tree;
  /* Indicating which parameter is modified in its method.  */
  bool *ipa_mod;
  /* Only for versioned nodes this field would not be NULL,
     it points to the node that IPA cp cloned from.  */
  struct cgraph_node *ipcp_orig_node;
  /* Meaningful only for original methods.  Expresses the 
     ratio between the direct calls and sum of all invocations of 
     this function (given by profiling info).  It is used to calculate 
     the profiling information of the original function and the versioned
     one.  */
  gcov_type count_scale;
};

struct ipa_edge
{
  /* Number of actual arguments in this callsite.  When set to 0,
     this callsite's parameters would not be analyzed by the different
     stages of IPA CP.  */
  int ipa_param_num;
  /* Array of the callsite's jump function of each parameter.  */
  struct ipa_jump_func *ipa_param_map;
};

/* A methodlist element (referred to also as methodlist node). It is used 
   to create a temporary worklist used in 
   the propagation stage of IPCP. (can be used for more IPA 
   optimizations)  */
struct ipa_methodlist
{
  struct cgraph_node *method_p;
  struct ipa_methodlist *next_method;
};

/* A pointer to a methodlist element.  */
typedef struct ipa_methodlist *ipa_methodlist_p;

/* ipa_methodlist interface.  */
ipa_methodlist_p ipa_methodlist_init (void);
bool ipa_methodlist_not_empty (ipa_methodlist_p);
void ipa_add_method (ipa_methodlist_p *, struct cgraph_node *);
struct cgraph_node *ipa_remove_method (ipa_methodlist_p *);

/* ipa_callsite interface.  */
int ipa_callsite_param_count (struct cgraph_edge *);
void ipa_callsite_param_count_set (struct cgraph_edge *, int);
struct ipa_jump_func *ipa_callsite_param (struct cgraph_edge *, int);
struct cgraph_node *ipa_callsite_callee (struct cgraph_edge *);
void ipa_callsite_compute_param (struct cgraph_edge *);
void ipa_callsite_compute_count (struct cgraph_edge *);

/* ipa_method interface.  */
int ipa_method_formal_count (struct cgraph_node *);
void ipa_method_formal_count_set (struct cgraph_node *, int);
tree ipa_method_get_tree (struct cgraph_node *, int);
void ipa_method_compute_tree_map (struct cgraph_node *);
void ipa_method_formal_compute_count (struct cgraph_node *);
void ipa_method_compute_modify (struct cgraph_node *);

/* jump function interface.  */
enum jump_func_type get_type (struct ipa_jump_func *);
union parameter_info *ipa_jf_get_info_type (struct ipa_jump_func *);

/* ipa_node and ipa_edge interfaces.  */
void ipa_node_create (struct cgraph_node *);
void ipa_free (void);
void ipa_nodes_create (void);
void ipa_edges_create (void);
void ipa_edges_free (void);
void ipa_nodes_free (void);


/* Debugging interface.  */
void ipa_method_tree_print (FILE *);
void ipa_method_modify_print (FILE *);

unsigned int ipcp_driver (void);

#endif /* IPA_PROP_H */
