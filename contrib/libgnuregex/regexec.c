/* Extended regular expression matching and search library.
   Copyright (C) 2002-2005,2007,2009,2010,2011 Free Software Foundation, Inc.
   This file is part of the GNU C Library.
   Contributed by Isamu Hasegawa <isamu@yamato.ibm.com>.

   The GNU C Library is free software; you can redistribute it and/or
   modify it under the terms of the GNU Lesser General Public
   License as published by the Free Software Foundation; either
   version 2.1 of the License, or (at your option) any later version.

   The GNU C Library is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
   Lesser General Public License for more details.

   You should have received a copy of the GNU Lesser General Public
   License along with the GNU C Library; if not, see
   <http://www.gnu.org/licenses/>.  */

static reg_errcode_t match_ctx_init (re_match_context_t *cache, int eflags,
				     int n) internal_function;
static void match_ctx_clean (re_match_context_t *mctx) internal_function;
static void match_ctx_free (re_match_context_t *cache) internal_function;
static reg_errcode_t match_ctx_add_entry (re_match_context_t *cache, int node,
					  int str_idx, int from, int to)
     internal_function;
static int search_cur_bkref_entry (const re_match_context_t *mctx, int str_idx)
     internal_function;
static reg_errcode_t match_ctx_add_subtop (re_match_context_t *mctx, int node,
					   int str_idx) internal_function;
static re_sub_match_last_t * match_ctx_add_sublast (re_sub_match_top_t *subtop,
						   int node, int str_idx)
     internal_function;
static void sift_ctx_init (re_sift_context_t *sctx, re_dfastate_t **sifted_sts,
			   re_dfastate_t **limited_sts, int last_node,
			   int last_str_idx)
     internal_function;
static reg_errcode_t re_search_internal (const regex_t *preg,
					 const char *string, int length,
					 int start, int range, int stop,
					 size_t nmatch, regmatch_t pmatch[],
					 int eflags) internal_function;
static int re_search_2_stub (struct re_pattern_buffer *bufp,
			     const char *string1, int length1,
			     const char *string2, int length2,
			     int start, int range, struct re_registers *regs,
			     int stop, int ret_len) internal_function;
static int re_search_stub (struct re_pattern_buffer *bufp,
			   const char *string, int length, int start,
			   int range, int stop, struct re_registers *regs,
			   int ret_len) internal_function;
static unsigned re_copy_regs (struct re_registers *regs, regmatch_t *pmatch,
			      int nregs, int regs_allocated) internal_function;
static reg_errcode_t prune_impossible_nodes (re_match_context_t *mctx)
     internal_function;
static int check_matching (re_match_context_t *mctx, int fl_longest_match,
			   int *p_match_first) internal_function;
static int check_halt_state_context (const re_match_context_t *mctx,
				     const re_dfastate_t *state, int idx)
     internal_function;
static void update_regs (const re_dfa_t *dfa, regmatch_t *pmatch,
			 regmatch_t *prev_idx_match, int cur_node,
			 int cur_idx, int nmatch) internal_function;
static reg_errcode_t push_fail_stack (struct re_fail_stack_t *fs,
				      int str_idx, int dest_node, int nregs,
				      regmatch_t *regs,
				      re_node_set *eps_via_nodes)
     internal_function;
static reg_errcode_t set_regs (const regex_t *preg,
			       const re_match_context_t *mctx,
			       size_t nmatch, regmatch_t *pmatch,
			       int fl_backtrack) internal_function;
static reg_errcode_t free_fail_stack_return (struct re_fail_stack_t *fs)
     internal_function;

#ifdef RE_ENABLE_I18N
static int sift_states_iter_mb (const re_match_context_t *mctx,
				re_sift_context_t *sctx,
				int node_idx, int str_idx, int max_str_idx)
     internal_function;
#endif /* RE_ENABLE_I18N */
static reg_errcode_t sift_states_backward (const re_match_context_t *mctx,
					   re_sift_context_t *sctx)
     internal_function;
static reg_errcode_t build_sifted_states (const re_match_context_t *mctx,
					  re_sift_context_t *sctx, int str_idx,
					  re_node_set *cur_dest)
     internal_function;
static reg_errcode_t update_cur_sifted_state (const re_match_context_t *mctx,
					      re_sift_context_t *sctx,
					      int str_idx,
					      re_node_set *dest_nodes)
     internal_function;
static reg_errcode_t add_epsilon_src_nodes (const re_dfa_t *dfa,
					    re_node_set *dest_nodes,
					    const re_node_set *candidates)
     internal_function;
static int check_dst_limits (const re_match_context_t *mctx,
			     re_node_set *limits,
			     int dst_node, int dst_idx, int src_node,
			     int src_idx) internal_function;
static int check_dst_limits_calc_pos_1 (const re_match_context_t *mctx,
					int boundaries, int subexp_idx,
					int from_node, int bkref_idx)
     internal_function;
static int check_dst_limits_calc_pos (const re_match_context_t *mctx,
				      int limit, int subexp_idx,
				      int node, int str_idx,
				      int bkref_idx) internal_function;
static reg_errcode_t check_subexp_limits (const re_dfa_t *dfa,
					  re_node_set *dest_nodes,
					  const re_node_set *candidates,
					  re_node_set *limits,
					  struct re_backref_cache_entry *bkref_ents,
					  int str_idx) internal_function;
static reg_errcode_t sift_states_bkref (const re_match_context_t *mctx,
					re_sift_context_t *sctx,
					int str_idx, const re_node_set *candidates)
     internal_function;
static reg_errcode_t merge_state_array (const re_dfa_t *dfa,
					re_dfastate_t **dst,
					re_dfastate_t **src, int num)
     internal_function;
static re_dfastate_t *find_recover_state (reg_errcode_t *err,
					 re_match_context_t *mctx) internal_function;
static re_dfastate_t *transit_state (reg_errcode_t *err,
				     re_match_context_t *mctx,
				     re_dfastate_t *state) internal_function;
static re_dfastate_t *merge_state_with_log (reg_errcode_t *err,
					    re_match_context_t *mctx,
					    re_dfastate_t *next_state)
     internal_function;
static reg_errcode_t check_subexp_matching_top (re_match_context_t *mctx,
						re_node_set *cur_nodes,
						int str_idx) internal_function;
#if 0
static re_dfastate_t *transit_state_sb (reg_errcode_t *err,
					re_match_context_t *mctx,
					re_dfastate_t *pstate)
     internal_function;
#endif
#ifdef RE_ENABLE_I18N
static reg_errcode_t transit_state_mb (re_match_context_t *mctx,
				       re_dfastate_t *pstate)
     internal_function;
#endif /* RE_ENABLE_I18N */
static reg_errcode_t transit_state_bkref (re_match_context_t *mctx,
					  const re_node_set *nodes)
     internal_function;
static reg_errcode_t get_subexp (re_match_context_t *mctx,
				 int bkref_node, int bkref_str_idx)
     internal_function;
static reg_errcode_t get_subexp_sub (re_match_context_t *mctx,
				     const re_sub_match_top_t *sub_top,
				     re_sub_match_last_t *sub_last,
				     int bkref_node, int bkref_str)
     internal_function;
static int find_subexp_node (const re_dfa_t *dfa, const re_node_set *nodes,
			     int subexp_idx, int type) internal_function;
static reg_errcode_t check_arrival (re_match_context_t *mctx,
				    state_array_t *path, int top_node,
				    int top_str, int last_node, int last_str,
				    int type) internal_function;
static reg_errcode_t check_arrival_add_next_nodes (re_match_context_t *mctx,
						   int str_idx,
						   re_node_set *cur_nodes,
						   re_node_set *next_nodes)
     internal_function;
static reg_errcode_t check_arrival_expand_ecl (const re_dfa_t *dfa,
					       re_node_set *cur_nodes,
					       int ex_subexp, int type)
     internal_function;
static reg_errcode_t check_arrival_expand_ecl_sub (const re_dfa_t *dfa,
						   re_node_set *dst_nodes,
						   int target, int ex_subexp,
						   int type) internal_function;
static reg_errcode_t expand_bkref_cache (re_match_context_t *mctx,
					 re_node_set *cur_nodes, int cur_str,
					 int subexp_num, int type)
     internal_function;
static int build_trtable (const re_dfa_t *dfa,
			  re_dfastate_t *state) internal_function;
#ifdef RE_ENABLE_I18N
static int check_node_accept_bytes (const re_dfa_t *dfa, int node_idx,
				    const re_string_t *input, int idx)
     internal_function;
# ifdef _LIBC
static unsigned int find_collation_sequence_value (const unsigned char *mbs,
						   size_t name_len)
     internal_function;
# endif /* _LIBC */
#endif /* RE_ENABLE_I18N */
static int group_nodes_into_DFAstates (const re_dfa_t *dfa,
				       const re_dfastate_t *state,
				       re_node_set *states_node,
				       bitset_t *states_ch) internal_function;
static int check_node_accept (const re_match_context_t *mctx,
			      const re_token_t *node, int idx)
     internal_function;
static reg_errcode_t extend_buffers (re_match_context_t *mctx, int min_len)
     internal_function;

/* Entry point for POSIX code.  */

/* regexec searches for a given pattern, specified by PREG, in the
   string STRING.

   If NMATCH is zero or REG_NOSUB was set in the cflags argument to
   `regcomp', we ignore PMATCH.  Otherwise, we assume PMATCH has at
   least NMATCH elements, and we set them to the offsets of the
   corresponding matched substrings.

   EFLAGS specifies `execution flags' which affect matching: if
   REG_NOTBOL is set, then ^ does not match at the beginning of the
   string; if REG_NOTEOL is set, then $ does not match at the end.

   We return 0 if we find a match and REG_NOMATCH if not.  */

int
regexec (preg, string, nmatch, pmatch, eflags)
    const regex_t *__restrict preg;
    const char *__restrict string;
    size_t nmatch;
    regmatch_t pmatch[];
    int eflags;
{
  reg_errcode_t err;
  int start, length;
  re_dfa_t *dfa = (re_dfa_t *) preg->buffer;

  if (eflags & ~(REG_NOTBOL | REG_NOTEOL | REG_STARTEND))
    return REG_BADPAT;

  if (eflags & REG_STARTEND)
    {
      start = pmatch[0].rm_so;
      length = pmatch[0].rm_eo;
    }
  else
    {
      start = 0;
      length = strlen (string);
    }

  __libc_lock_lock (dfa->lock);
  if (preg->no_sub)
    err = re_search_internal (preg, string, length, start, length - start,
			      length, 0, NULL, eflags);
  else
    err = re_search_internal (preg, string, length, start, length - start,
			      length, nmatch, pmatch, eflags);
  __libc_lock_unlock (dfa->lock);
  return err != REG_NOERROR;
}

#ifdef _LIBC
# include <shlib-compat.h>
versioned_symbol (libc, __regexec, regexec, GLIBC_2_3_4);

# if SHLIB_COMPAT (libc, GLIBC_2_0, GLIBC_2_3_4)
__typeof__ (__regexec) __compat_regexec;

int
attribute_compat_text_section
__compat_regexec (const regex_t *__restrict preg,
		  const char *__restrict string, size_t nmatch,
		  regmatch_t pmatch[], int eflags)
{
  return regexec (preg, string, nmatch, pmatch,
		  eflags & (REG_NOTBOL | REG_NOTEOL));
}
compat_symbol (libc, __compat_regexec, regexec, GLIBC_2_0);
# endif
#endif

/* Entry points for GNU code.  */

/* re_match, re_search, re_match_2, re_search_2

   The former two functions operate on STRING with length LENGTH,
   while the later two operate on concatenation of STRING1 and STRING2
   with lengths LENGTH1 and LENGTH2, respectively.

   re_match() matches the compiled pattern in BUFP against the string,
   starting at index START.

   re_search() first tries matching at index START, then it tries to match
   starting from index START + 1, and so on.  The last start position tried
   is START + RANGE.  (Thus RANGE = 0 forces re_search to operate the same
   way as re_match().)

   The parameter STOP of re_{match,search}_2 specifies that no match exceeding
   the first STOP characters of the concatenation of the strings should be
   concerned.

   If REGS is not NULL, and BUFP->no_sub is not set, the offsets of the match
   and all groups is stroed in REGS.  (For the "_2" variants, the offsets are
   computed relative to the concatenation, not relative to the individual
   strings.)

   On success, re_match* functions return the length of the match, re_search*
   return the position of the start of the match.  Return value -1 means no
   match was found and -2 indicates an internal error.  */

int
re_match (bufp, string, length, start, regs)
    struct re_pattern_buffer *bufp;
    const char *string;
    int length, start;
    struct re_registers *regs;
{
  return re_search_stub (bufp, string, length, start, 0, length, regs, 1);
}
#ifdef _LIBC
weak_alias (__re_match, re_match)
#endif

int
re_search (bufp, string, length, start, range, regs)
    struct re_pattern_buffer *bufp;
    const char *string;
    int length, start, range;
    struct re_registers *regs;
{
  return re_search_stub (bufp, string, length, start, range, length, regs, 0);
}
#ifdef _LIBC
weak_alias (__re_search, re_search)
#endif

int
re_match_2 (bufp, string1, length1, string2, length2, start, regs, stop)
    struct re_pattern_buffer *bufp;
    const char *string1, *string2;
    int length1, length2, start, stop;
    struct re_registers *regs;
{
  return re_search_2_stub (bufp, string1, length1, string2, length2,
			   start, 0, regs, stop, 1);
}
#ifdef _LIBC
weak_alias (__re_match_2, re_match_2)
#endif

int
re_search_2 (bufp, string1, length1, string2, length2, start, range, regs, stop)
    struct re_pattern_buffer *bufp;
    const char *string1, *string2;
    int length1, length2, start, range, stop;
    struct re_registers *regs;
{
  return re_search_2_stub (bufp, string1, length1, string2, length2,
			   start, range, regs, stop, 0);
}
#ifdef _LIBC
weak_alias (__re_search_2, re_search_2)
#endif

static int
re_search_2_stub (bufp, string1, length1, string2, length2, start, range, regs,
		  stop, ret_len)
    struct re_pattern_buffer *bufp;
    const char *string1, *string2;
    int length1, length2, start, range, stop, ret_len;
    struct re_registers *regs;
{
  const char *str;
  int rval;
  int len = length1 + length2;
  char *s = NULL;

  if (BE (length1 < 0 || length2 < 0 || stop < 0 || len < length1, 0))
    return -2;

  /* Concatenate the strings.  */
  if (length2 > 0)
    if (length1 > 0)
      {
	s = re_malloc (char, len);

	if (BE (s == NULL, 0))
	  return -2;
#ifdef _LIBC
	memcpy (__mempcpy (s, string1, length1), string2, length2);
#else
	memcpy (s, string1, length1);
	memcpy (s + length1, string2, length2);
#endif
	str = s;
      }
    else
      str = string2;
  else
    str = string1;

  rval = re_search_stub (bufp, str, len, start, range, stop, regs, ret_len);
  re_free (s);
  return rval;
}

/* The parameters have the same meaning as those of re_search.
   Additional parameters:
   If RET_LEN is nonzero the length of the match is returned (re_match style);
   otherwise the position of the match is returned.  */

static int
re_search_stub (bufp, string, length, start, range, stop, regs, ret_len)
    struct re_pattern_buffer *bufp;
    const char *string;
    int length, start, range, stop, ret_len;
    struct re_registers *regs;
{
  reg_errcode_t result;
  regmatch_t *pmatch;
  int nregs, rval;
  int eflags = 0;
  re_dfa_t *dfa = (re_dfa_t *) bufp->buffer;

  /* Check for out-of-range.  */
  if (BE (start < 0 || start > length, 0))
    return -1;
  if (BE (start + range > length, 0))
    range = length - start;
  else if (BE (start + range < 0, 0))
    range = -start;

  __libc_lock_lock (dfa->lock);

  eflags |= (bufp->not_bol) ? REG_NOTBOL : 0;
  eflags |= (bufp->not_eol) ? REG_NOTEOL : 0;

  /* Compile fastmap if we haven't yet.  */
  if (range > 0 && bufp->fastmap != NULL && !bufp->fastmap_accurate)
    re_compile_fastmap (bufp);

  if (BE (bufp->no_sub, 0))
    regs = NULL;

  /* We need at least 1 register.  */
  if (regs == NULL)
    nregs = 1;
  else if (BE (bufp->regs_allocated == REGS_FIXED &&
	       regs->num_regs < bufp->re_nsub + 1, 0))
    {
      nregs = regs->num_regs;
      if (BE (nregs < 1, 0))
	{
	  /* Nothing can be copied to regs.  */
	  regs = NULL;
	  nregs = 1;
	}
    }
  else
    nregs = bufp->re_nsub + 1;
  pmatch = re_malloc (regmatch_t, nregs);
  if (BE (pmatch == NULL, 0))
    {
      rval = -2;
      goto out;
    }

  result = re_search_internal (bufp, string, length, start, range, stop,
			       nregs, pmatch, eflags);

  rval = 0;

  /* I hope we needn't fill ther regs with -1's when no match was found.  */
  if (result != REG_NOERROR)
    rval = -1;
  else if (regs != NULL)
    {
      /* If caller wants register contents data back, copy them.  */
      bufp->regs_allocated = re_copy_regs (regs, pmatch, nregs,
					   bufp->regs_allocated);
      if (BE (bufp->regs_allocated == REGS_UNALLOCATED, 0))
	rval = -2;
    }

  if (BE (rval == 0, 1))
    {
      if (ret_len)
	{
	  assert (pmatch[0].rm_so == start);
	  rval = pmatch[0].rm_eo - start;
	}
      else
	rval = pmatch[0].rm_so;
    }
  re_free (pmatch);
 out:
  __libc_lock_unlock (dfa->lock);
  return rval;
}

static unsigned
re_copy_regs (regs, pmatch, nregs, regs_allocated)
    struct re_registers *regs;
    regmatch_t *pmatch;
    int nregs, regs_allocated;
{
  int rval = REGS_REALLOCATE;
  int i;
  int need_regs = nregs + 1;
  /* We need one extra element beyond `num_regs' for the `-1' marker GNU code
     uses.  */

  /* Have the register data arrays been allocated?  */
  if (regs_allocated == REGS_UNALLOCATED)
    { /* No.  So allocate them with malloc.  */
      regs->start = re_malloc (regoff_t, need_regs);
      if (BE (regs->start == NULL, 0))
	return REGS_UNALLOCATED;
      regs->end = re_malloc (regoff_t, need_regs);
      if (BE (regs->end == NULL, 0))
	{
	  re_free (regs->start);
	  return REGS_UNALLOCATED;
	}
      regs->num_regs = need_regs;
    }
  else if (regs_allocated == REGS_REALLOCATE)
    { /* Yes.  If we need more elements than were already
	 allocated, reallocate them.  If we need fewer, just
	 leave it alone.  */
      if (BE (need_regs > regs->num_regs, 0))
	{
	  regoff_t *new_start = re_realloc (regs->start, regoff_t, need_regs);
	  regoff_t *new_end;
	  if (BE (new_start == NULL, 0))
	    return REGS_UNALLOCATED;
	  new_end = re_realloc (regs->end, regoff_t, need_regs);
	  if (BE (new_end == NULL, 0))
	    {
	      re_free (new_start);
	      return REGS_UNALLOCATED;
	    }
	  regs->start = new_start;
	  regs->end = new_end;
	  regs->num_regs = need_regs;
	}
    }
  else
    {
      assert (regs_allocated == REGS_FIXED);
      /* This function may not be called with REGS_FIXED and nregs too big.  */
      assert (regs->num_regs >= nregs);
      rval = REGS_FIXED;
    }

  /* Copy the regs.  */
  for (i = 0; i < nregs; ++i)
    {
      regs->start[i] = pmatch[i].rm_so;
      regs->end[i] = pmatch[i].rm_eo;
    }
  for ( ; i < regs->num_regs; ++i)
    regs->start[i] = regs->end[i] = -1;

  return rval;
}

/* Set REGS to hold NUM_REGS registers, storing them in STARTS and
   ENDS.  Subsequent matches using PATTERN_BUFFER and REGS will use
   this memory for recording register information.  STARTS and ENDS
   must be allocated using the malloc library routine, and must each
   be at least NUM_REGS * sizeof (regoff_t) bytes long.

   If NUM_REGS == 0, then subsequent matches should allocate their own
   register data.

   Unless this function is called, the first search or match using
   PATTERN_BUFFER will allocate its own register data, without
   freeing the old data.  */

void
re_set_registers (bufp, regs, num_regs, starts, ends)
    struct re_pattern_buffer *bufp;
    struct re_registers *regs;
    unsigned num_regs;
    regoff_t *starts, *ends;
{
  if (num_regs)
    {
      bufp->regs_allocated = REGS_REALLOCATE;
      regs->num_regs = num_regs;
      regs->start = starts;
      regs->end = ends;
    }
  else
    {
      bufp->regs_allocated = REGS_UNALLOCATED;
      regs->num_regs = 0;
      regs->start = regs->end = (regoff_t *) 0;
    }
}
#ifdef _LIBC
weak_alias (__re_set_registers, re_set_registers)
#endif

/* Entry points compatible with 4.2 BSD regex library.  We don't define
   them unless specifically requested.  */

#if defined _REGEX_RE_COMP || defined _LIBC
int
# ifdef _LIBC
weak_function
# endif
re_exec (s)
     const char *s;
{
  return 0 == regexec (&re_comp_buf, s, 0, NULL, 0);
}
#endif /* _REGEX_RE_COMP */

/* Internal entry point.  */

/* Searches for a compiled pattern PREG in the string STRING, whose
   length is LENGTH.  NMATCH, PMATCH, and EFLAGS have the same
   mingings with regexec.  START, and RANGE have the same meanings
   with re_search.
   Return REG_NOERROR if we find a match, and REG_NOMATCH if not,
   otherwise return the error code.
   Note: We assume front end functions already check ranges.
   (START + RANGE >= 0 && START + RANGE <= LENGTH)  */

static reg_errcode_t
__attribute_warn_unused_result__
re_search_internal (preg, string, length, start, range, stop, nmatch, pmatch,
		    eflags)
    const regex_t *preg;
    const char *string;
    int length, start, range, stop, eflags;
    size_t nmatch;
    regmatch_t pmatch[];
{
  reg_errcode_t err;
  const re_dfa_t *dfa = (const re_dfa_t *) preg->buffer;
  int left_lim, right_lim, incr;
  int fl_longest_match, match_first, match_kind, match_last = -1;
  int extra_nmatch;
  int sb, ch;
#if defined _LIBC || (defined __STDC_VERSION__ && __STDC_VERSION__ >= 199901L)
  re_match_context_t mctx = { .dfa = dfa };
#else
  re_match_context_t mctx;
#endif
  char *fastmap = (preg->fastmap != NULL && preg->fastmap_accurate
		   && range && !preg->can_be_null) ? preg->fastmap : NULL;
  RE_TRANSLATE_TYPE t = preg->translate;

#if !(defined _LIBC || (defined __STDC_VERSION__ && __STDC_VERSION__ >= 199901L))
  memset (&mctx, '\0', sizeof (re_match_context_t));
  mctx.dfa = dfa;
#endif

  extra_nmatch = (nmatch > preg->re_nsub) ? nmatch - (preg->re_nsub + 1) : 0;
  nmatch -= extra_nmatch;

  /* Check if the DFA haven't been compiled.  */
  if (BE (preg->used == 0 || dfa->init_state == NULL
	  || dfa->init_state_word == NULL || dfa->init_state_nl == NULL
	  || dfa->init_state_begbuf == NULL, 0))
    return REG_NOMATCH;

#ifdef DEBUG
  /* We assume front-end functions already check them.  */
  assert (start + range >= 0 && start + range <= length);
#endif

  /* If initial states with non-begbuf contexts have no elements,
     the regex must be anchored.  If preg->newline_anchor is set,
     we'll never use init_state_nl, so do not check it.  */
  if (dfa->init_state->nodes.nelem == 0
      && dfa->init_state_word->nodes.nelem == 0
      && (dfa->init_state_nl->nodes.nelem == 0
	  || !preg->newline_anchor))
    {
      if (start != 0 && start + range != 0)
	return REG_NOMATCH;
      start = range = 0;
    }

  /* We must check the longest matching, if nmatch > 0.  */
  fl_longest_match = (nmatch != 0 || dfa->nbackref);

  err = re_string_allocate (&mctx.input, string, length, dfa->nodes_len + 1,
			    preg->translate, preg->syntax & RE_ICASE, dfa);
  if (BE (err != REG_NOERROR, 0))
    goto free_return;
  mctx.input.stop = stop;
  mctx.input.raw_stop = stop;
  mctx.input.newline_anchor = preg->newline_anchor;

  err = match_ctx_init (&mctx, eflags, dfa->nbackref * 2);
  if (BE (err != REG_NOERROR, 0))
    goto free_return;

  /* We will log all the DFA states through which the dfa pass,
     if nmatch > 1, or this dfa has "multibyte node", which is a
     back-reference or a node which can accept multibyte character or
     multi character collating element.  */
  if (nmatch > 1 || dfa->has_mb_node)
    {
      /* Avoid overflow.  */
      if (BE (SIZE_MAX / sizeof (re_dfastate_t *) <= mctx.input.bufs_len, 0))
	{
	  err = REG_ESPACE;
	  goto free_return;
	}

      mctx.state_log = re_malloc (re_dfastate_t *, mctx.input.bufs_len + 1);
      if (BE (mctx.state_log == NULL, 0))
	{
	  err = REG_ESPACE;
	  goto free_return;
	}
    }
  else
    mctx.state_log = NULL;

  match_first = start;
  mctx.input.tip_context = (eflags & REG_NOTBOL) ? CONTEXT_BEGBUF
			   : CONTEXT_NEWLINE | CONTEXT_BEGBUF;

  /* Check incrementally whether of not the input string match.  */
  incr = (range < 0) ? -1 : 1;
  left_lim = (range < 0) ? start + range : start;
  right_lim = (range < 0) ? start : start + range;
  sb = dfa->mb_cur_max == 1;
  match_kind =
    (fastmap
     ? ((sb || !(preg->syntax & RE_ICASE || t) ? 4 : 0)
	| (range >= 0 ? 2 : 0)
	| (t != NULL ? 1 : 0))
     : 8);

  for (;; match_first += incr)
    {
      err = REG_NOMATCH;
      if (match_first < left_lim || right_lim < match_first)
	goto free_return;

      /* Advance as rapidly as possible through the string, until we
	 find a plausible place to start matching.  This may be done
	 with varying efficiency, so there are various possibilities:
	 only the most common of them are specialized, in order to
	 save on code size.  We use a switch statement for speed.  */
      switch (match_kind)
	{
	case 8:
	  /* No fastmap.  */
	  break;

	case 7:
	  /* Fastmap with single-byte translation, match forward.  */
	  while (BE (match_first < right_lim, 1)
		 && !fastmap[t[(unsigned char) string[match_first]]])
	    ++match_first;
	  goto forward_match_found_start_or_reached_end;

	case 6:
	  /* Fastmap without translation, match forward.  */
	  while (BE (match_first < right_lim, 1)
		 && !fastmap[(unsigned char) string[match_first]])
	    ++match_first;

	forward_match_found_start_or_reached_end:
	  if (BE (match_first == right_lim, 0))
	    {
	      ch = match_first >= length
		       ? 0 : (unsigned char) string[match_first];
	      if (!fastmap[t ? t[ch] : ch])
		goto free_return;
	    }
	  break;

	case 4:
	case 5:
	  /* Fastmap without multi-byte translation, match backwards.  */
	  while (match_first >= left_lim)
	    {
	      ch = match_first >= length
		       ? 0 : (unsigned char) string[match_first];
	      if (fastmap[t ? t[ch] : ch])
		break;
	      --match_first;
	    }
	  if (match_first < left_lim)
	    goto free_return;
	  break;

	default:
	  /* In this case, we can't determine easily the current byte,
	     since it might be a component byte of a multibyte
	     character.  Then we use the constructed buffer instead.  */
	  for (;;)
	    {
	      /* If MATCH_FIRST is out of the valid range, reconstruct the
		 buffers.  */
	      unsigned int offset = match_first - mctx.input.raw_mbs_idx;
	      if (BE (offset >= (unsigned int) mctx.input.valid_raw_len, 0))
		{
		  err = re_string_reconstruct (&mctx.input, match_first,
					       eflags);
		  if (BE (err != REG_NOERROR, 0))
		    goto free_return;

		  offset = match_first - mctx.input.raw_mbs_idx;
		}
	      /* If MATCH_FIRST is out of the buffer, leave it as '\0'.
		 Note that MATCH_FIRST must not be smaller than 0.  */
	      ch = (match_first >= length
		    ? 0 : re_string_byte_at (&mctx.input, offset));
	      if (fastmap[ch])
		break;
	      match_first += incr;
	      if (match_first < left_lim || match_first > right_lim)
		{
		  err = REG_NOMATCH;
		  goto free_return;
		}
	    }
	  break;
	}

      /* Reconstruct the buffers so that the matcher can assume that
	 the matching starts from the beginning of the buffer.  */
      err = re_string_reconstruct (&mctx.input, match_first, eflags);
      if (BE (err != REG_NOERROR, 0))
	goto free_return;

#ifdef RE_ENABLE_I18N
     /* Don't consider this char as a possible match start if it part,
	yet isn't the head, of a multibyte character.  */
      if (!sb && !re_string_first_byte (&mctx.input, 0))
	continue;
#endif

      /* It seems to be appropriate one, then use the matcher.  */
      /* We assume that the matching starts from 0.  */
      mctx.state_log_top = mctx.nbkref_ents = mctx.max_mb_elem_len = 0;
      match_last = check_matching (&mctx, fl_longest_match,
				   range >= 0 ? &match_first : NULL);
      if (match_last != -1)
	{
	  if (BE (match_last == -2, 0))
	    {
	      err = REG_ESPACE;
	      goto free_return;
	    }
	  else
	    {
	      mctx.match_last = match_last;
	      if ((!preg->no_sub && nmatch > 1) || dfa->nbackref)
		{
		  re_dfastate_t *pstate = mctx.state_log[match_last];
		  mctx.last_node = check_halt_state_context (&mctx, pstate,
							     match_last);
		}
	      if ((!preg->no_sub && nmatch > 1 && dfa->has_plural_match)
		  || dfa->nbackref)
		{
		  err = prune_impossible_nodes (&mctx);
		  if (err == REG_NOERROR)
		    break;
		  if (BE (err != REG_NOMATCH, 0))
		    goto free_return;
		  match_last = -1;
		}
	      else
		break; /* We found a match.  */
	    }
	}

      match_ctx_clean (&mctx);
    }

#ifdef DEBUG
  assert (match_last != -1);
  assert (err == REG_NOERROR);
#endif

  /* Set pmatch[] if we need.  */
  if (nmatch > 0)
    {
      int reg_idx;

      /* Initialize registers.  */
      for (reg_idx = 1; reg_idx < nmatch; ++reg_idx)
	pmatch[reg_idx].rm_so = pmatch[reg_idx].rm_eo = -1;

      /* Set the points where matching start/end.  */
      pmatch[0].rm_so = 0;
      pmatch[0].rm_eo = mctx.match_last;

      if (!preg->no_sub && nmatch > 1)
	{
	  err = set_regs (preg, &mctx, nmatch, pmatch,
			  dfa->has_plural_match && dfa->nbackref > 0);
	  if (BE (err != REG_NOERROR, 0))
	    goto free_return;
	}

      /* At last, add the offset to the each registers, since we slided
	 the buffers so that we could assume that the matching starts
	 from 0.  */
      for (reg_idx = 0; reg_idx < nmatch; ++reg_idx)
	if (pmatch[reg_idx].rm_so != -1)
	  {
#ifdef RE_ENABLE_I18N
	    if (BE (mctx.input.offsets_needed != 0, 0))
	      {
		pmatch[reg_idx].rm_so =
		  (pmatch[reg_idx].rm_so == mctx.input.valid_len
		   ? mctx.input.valid_raw_len
		   : mctx.input.offsets[pmatch[reg_idx].rm_so]);
		pmatch[reg_idx].rm_eo =
		  (pmatch[reg_idx].rm_eo == mctx.input.valid_len
		   ? mctx.input.valid_raw_len
		   : mctx.input.offsets[pmatch[reg_idx].rm_eo]);
	      }
#else
	    assert (mctx.input.offsets_needed == 0);
#endif
	    pmatch[reg_idx].rm_so += match_first;
	    pmatch[reg_idx].rm_eo += match_first;
	  }
      for (reg_idx = 0; reg_idx < extra_nmatch; ++reg_idx)
	{
	  pmatch[nmatch + reg_idx].rm_so = -1;
	  pmatch[nmatch + reg_idx].rm_eo = -1;
	}

      if (dfa->subexp_map)
	for (reg_idx = 0; reg_idx + 1 < nmatch; reg_idx++)
	  if (dfa->subexp_map[reg_idx] != reg_idx)
	    {
	      pmatch[reg_idx + 1].rm_so
		= pmatch[dfa->subexp_map[reg_idx] + 1].rm_so;
	      pmatch[reg_idx + 1].rm_eo
		= pmatch[dfa->subexp_map[reg_idx] + 1].rm_eo;
	    }
    }

 free_return:
  re_free (mctx.state_log);
  if (dfa->nbackref)
    match_ctx_free (&mctx);
  re_string_destruct (&mctx.input);
  return err;
}

static reg_errcode_t
__attribute_warn_unused_result__
prune_impossible_nodes (mctx)
     re_match_context_t *mctx;
{
  const re_dfa_t *const dfa = mctx->dfa;
  int halt_node, match_last;
  reg_errcode_t ret;
  re_dfastate_t **sifted_states;
  re_dfastate_t **lim_states = NULL;
  re_sift_context_t sctx;
#ifdef DEBUG
  assert (mctx->state_log != NULL);
#endif
  match_last = mctx->match_last;
  halt_node = mctx->last_node;

  /* Avoid overflow.  */
  if (BE (SIZE_MAX / sizeof (re_dfastate_t *) <= match_last, 0))
    return REG_ESPACE;

  sifted_states = re_malloc (re_dfastate_t *, match_last + 1);
  if (BE (sifted_states == NULL, 0))
    {
      ret = REG_ESPACE;
      goto free_return;
    }
  if (dfa->nbackref)
    {
      lim_states = re_malloc (re_dfastate_t *, match_last + 1);
      if (BE (lim_states == NULL, 0))
	{
	  ret = REG_ESPACE;
	  goto free_return;
	}
      while (1)
	{
	  memset (lim_states, '\0',
		  sizeof (re_dfastate_t *) * (match_last + 1));
	  sift_ctx_init (&sctx, sifted_states, lim_states, halt_node,
			 match_last);
	  ret = sift_states_backward (mctx, &sctx);
	  re_node_set_free (&sctx.limits);
	  if (BE (ret != REG_NOERROR, 0))
	      goto free_return;
	  if (sifted_states[0] != NULL || lim_states[0] != NULL)
	    break;
	  do
	    {
	      --match_last;
	      if (match_last < 0)
		{
		  ret = REG_NOMATCH;
		  goto free_return;
		}
	    } while (mctx->state_log[match_last] == NULL
		     || !mctx->state_log[match_last]->halt);
	  halt_node = check_halt_state_context (mctx,
						mctx->state_log[match_last],
						match_last);
	}
      ret = merge_state_array (dfa, sifted_states, lim_states,
			       match_last + 1);
      re_free (lim_states);
      lim_states = NULL;
      if (BE (ret != REG_NOERROR, 0))
	goto free_return;
    }
  else
    {
      sift_ctx_init (&sctx, sifted_states, lim_states, halt_node, match_last);
      ret = sift_states_backward (mctx, &sctx);
      re_node_set_free (&sctx.limits);
      if (BE (ret != REG_NOERROR, 0))
	goto free_return;
      if (sifted_states[0] == NULL)
	{
	  ret = REG_NOMATCH;
	  goto free_return;
	}
    }
  re_free (mctx->state_log);
  mctx->state_log = sifted_states;
  sifted_states = NULL;
  mctx->last_node = halt_node;
  mctx->match_last = match_last;
  ret = REG_NOERROR;
 free_return:
  re_free (sifted_states);
  re_free (lim_states);
  return ret;
}

/* Acquire an initial state and return it.
   We must select appropriate initial state depending on the context,
   since initial states may have constraints like "\<", "^", etc..  */

static inline re_dfastate_t *
__attribute ((always_inline)) internal_function
acquire_init_state_context (reg_errcode_t *err, const re_match_context_t *mctx,
			    int idx)
{
  const re_dfa_t *const dfa = mctx->dfa;
  if (dfa->init_state->has_constraint)
    {
      unsigned int context;
      context = re_string_context_at (&mctx->input, idx - 1, mctx->eflags);
      if (IS_WORD_CONTEXT (context))
	return dfa->init_state_word;
      else if (IS_ORDINARY_CONTEXT (context))
	return dfa->init_state;
      else if (IS_BEGBUF_CONTEXT (context) && IS_NEWLINE_CONTEXT (context))
	return dfa->init_state_begbuf;
      else if (IS_NEWLINE_CONTEXT (context))
	return dfa->init_state_nl;
      else if (IS_BEGBUF_CONTEXT (context))
	{
	  /* It is relatively rare case, then calculate on demand.  */
	  return re_acquire_state_context (err, dfa,
					   dfa->init_state->entrance_nodes,
					   context);
	}
      else
	/* Must not happen?  */
	return dfa->init_state;
    }
  else
    return dfa->init_state;
}

/* Check whether the regular expression match input string INPUT or not,
   and return the index where the matching end, return -1 if not match,
   or return -2 in case of an error.
   FL_LONGEST_MATCH means we want the POSIX longest matching.
   If P_MATCH_FIRST is not NULL, and the match fails, it is set to the
   next place where we may want to try matching.
   Note that the matcher assume that the maching starts from the current
   index of the buffer.  */

static int
internal_function __attribute_warn_unused_result__
check_matching (re_match_context_t *mctx, int fl_longest_match,
		int *p_match_first)
{
  const re_dfa_t *const dfa = mctx->dfa;
  reg_errcode_t err;
  int match = 0;
  int match_last = -1;
  int cur_str_idx = re_string_cur_idx (&mctx->input);
  re_dfastate_t *cur_state;
  int at_init_state = p_match_first != NULL;
  int next_start_idx = cur_str_idx;

  err = REG_NOERROR;
  cur_state = acquire_init_state_context (&err, mctx, cur_str_idx);
  /* An initial state must not be NULL (invalid).  */
  if (BE (cur_state == NULL, 0))
    {
      assert (err == REG_ESPACE);
      return -2;
    }

  if (mctx->state_log != NULL)
    {
      mctx->state_log[cur_str_idx] = cur_state;

      /* Check OP_OPEN_SUBEXP in the initial state in case that we use them
	 later.  E.g. Processing back references.  */
      if (BE (dfa->nbackref, 0))
	{
	  at_init_state = 0;
	  err = check_subexp_matching_top (mctx, &cur_state->nodes, 0);
	  if (BE (err != REG_NOERROR, 0))
	    return err;

	  if (cur_state->has_backref)
	    {
	      err = transit_state_bkref (mctx, &cur_state->nodes);
	      if (BE (err != REG_NOERROR, 0))
		return err;
	    }
	}
    }

  /* If the RE accepts NULL string.  */
  if (BE (cur_state->halt, 0))
    {
      if (!cur_state->has_constraint
	  || check_halt_state_context (mctx, cur_state, cur_str_idx))
	{
	  if (!fl_longest_match)
	    return cur_str_idx;
	  else
	    {
	      match_last = cur_str_idx;
	      match = 1;
	    }
	}
    }

  while (!re_string_eoi (&mctx->input))
    {
      re_dfastate_t *old_state = cur_state;
      int next_char_idx = re_string_cur_idx (&mctx->input) + 1;

      if ((BE (next_char_idx >= mctx->input.bufs_len, 0)
	   && mctx->input.bufs_len < mctx->input.len)
	  || (BE (next_char_idx >= mctx->input.valid_len, 0)
	      && mctx->input.valid_len < mctx->input.len))
	{
	  err = extend_buffers (mctx, next_char_idx + 1);
	  if (BE (err != REG_NOERROR, 0))
	    {
	      assert (err == REG_ESPACE);
	      return -2;
	    }
	}

      cur_state = transit_state (&err, mctx, cur_state);
      if (mctx->state_log != NULL)
	cur_state = merge_state_with_log (&err, mctx, cur_state);

      if (cur_state == NULL)
	{
	  /* Reached the invalid state or an error.  Try to recover a valid
	     state using the state log, if available and if we have not
	     already found a valid (even if not the longest) match.  */
	  if (BE (err != REG_NOERROR, 0))
	    return -2;

	  if (mctx->state_log == NULL
	      || (match && !fl_longest_match)
	      || (cur_state = find_recover_state (&err, mctx)) == NULL)
	    break;
	}

      if (BE (at_init_state, 0))
	{
	  if (old_state == cur_state)
	    next_start_idx = next_char_idx;
	  else
	    at_init_state = 0;
	}

      if (cur_state->halt)
	{
	  /* Reached a halt state.
	     Check the halt state can satisfy the current context.  */
	  if (!cur_state->has_constraint
	      || check_halt_state_context (mctx, cur_state,
					   re_string_cur_idx (&mctx->input)))
	    {
	      /* We found an appropriate halt state.  */
	      match_last = re_string_cur_idx (&mctx->input);
	      match = 1;

	      /* We found a match, do not modify match_first below.  */
	      p_match_first = NULL;
	      if (!fl_longest_match)
		break;
	    }
	}
    }

  if (p_match_first)
    *p_match_first += next_start_idx;

  return match_last;
}

/* Check NODE match the current context.  */

static int
internal_function
check_halt_node_context (const re_dfa_t *dfa, int node, unsigned int context)
{
  re_token_type_t type = dfa->nodes[node].type;
  unsigned int constraint = dfa->nodes[node].constraint;
  if (type != END_OF_RE)
    return 0;
  if (!constraint)
    return 1;
  if (NOT_SATISFY_NEXT_CONSTRAINT (constraint, context))
    return 0;
  return 1;
}

/* Check the halt state STATE match the current context.
   Return 0 if not match, if the node, STATE has, is a halt node and
   match the context, return the node.  */

static int
internal_function
check_halt_state_context (const re_match_context_t *mctx,
			  const re_dfastate_t *state, int idx)
{
  int i;
  unsigned int context;
#ifdef DEBUG
  assert (state->halt);
#endif
  context = re_string_context_at (&mctx->input, idx, mctx->eflags);
  for (i = 0; i < state->nodes.nelem; ++i)
    if (check_halt_node_context (mctx->dfa, state->nodes.elems[i], context))
      return state->nodes.elems[i];
  return 0;
}

/* Compute the next node to which "NFA" transit from NODE("NFA" is a NFA
   corresponding to the DFA).
   Return the destination node, and update EPS_VIA_NODES, return -1 in case
   of errors.  */

static int
internal_function
proceed_next_node (const re_match_context_t *mctx, int nregs, regmatch_t *regs,
		   int *pidx, int node, re_node_set *eps_via_nodes,
		   struct re_fail_stack_t *fs)
{
  const re_dfa_t *const dfa = mctx->dfa;
  int i, err;
  if (IS_EPSILON_NODE (dfa->nodes[node].type))
    {
      re_node_set *cur_nodes = &mctx->state_log[*pidx]->nodes;
      re_node_set *edests = &dfa->edests[node];
      int dest_node;
      err = re_node_set_insert (eps_via_nodes, node);
      if (BE (err < 0, 0))
	return -2;
      /* Pick up a valid destination, or return -1 if none is found.  */
      for (dest_node = -1, i = 0; i < edests->nelem; ++i)
	{
	  int candidate = edests->elems[i];
	  if (!re_node_set_contains (cur_nodes, candidate))
	    continue;
	  if (dest_node == -1)
	    dest_node = candidate;

	  else
	    {
	      /* In order to avoid infinite loop like "(a*)*", return the second
		 epsilon-transition if the first was already considered.  */
	      if (re_node_set_contains (eps_via_nodes, dest_node))
		return candidate;

	      /* Otherwise, push the second epsilon-transition on the fail stack.  */
	      else if (fs != NULL
		       && push_fail_stack (fs, *pidx, candidate, nregs, regs,
					   eps_via_nodes))
		return -2;

	      /* We know we are going to exit.  */
	      break;
	    }
	}
      return dest_node;
    }
  else
    {
      int naccepted = 0;
      re_token_type_t type = dfa->nodes[node].type;

#ifdef RE_ENABLE_I18N
      if (dfa->nodes[node].accept_mb)
	naccepted = check_node_accept_bytes (dfa, node, &mctx->input, *pidx);
      else
#endif /* RE_ENABLE_I18N */
      if (type == OP_BACK_REF)
	{
	  int subexp_idx = dfa->nodes[node].opr.idx + 1;
	  naccepted = regs[subexp_idx].rm_eo - regs[subexp_idx].rm_so;
	  if (fs != NULL)
	    {
	      if (regs[subexp_idx].rm_so == -1 || regs[subexp_idx].rm_eo == -1)
		return -1;
	      else if (naccepted)
		{
		  char *buf = (char *) re_string_get_buffer (&mctx->input);
		  if (memcmp (buf + regs[subexp_idx].rm_so, buf + *pidx,
			      naccepted) != 0)
		    return -1;
		}
	    }

	  if (naccepted == 0)
	    {
	      int dest_node;
	      err = re_node_set_insert (eps_via_nodes, node);
	      if (BE (err < 0, 0))
		return -2;
	      dest_node = dfa->edests[node].elems[0];
	      if (re_node_set_contains (&mctx->state_log[*pidx]->nodes,
					dest_node))
		return dest_node;
	    }
	}

      if (naccepted != 0
	  || check_node_accept (mctx, dfa->nodes + node, *pidx))
	{
	  int dest_node = dfa->nexts[node];
	  *pidx = (naccepted == 0) ? *pidx + 1 : *pidx + naccepted;
	  if (fs && (*pidx > mctx->match_last || mctx->state_log[*pidx] == NULL
		     || !re_node_set_contains (&mctx->state_log[*pidx]->nodes,
					       dest_node)))
	    return -1;
	  re_node_set_empty (eps_via_nodes);
	  return dest_node;
	}
    }
  return -1;
}

static reg_errcode_t
internal_function __attribute_warn_unused_result__
push_fail_stack (struct re_fail_stack_t *fs, int str_idx, int dest_node,
		 int nregs, regmatch_t *regs, re_node_set *eps_via_nodes)
{
  reg_errcode_t err;
  int num = fs->num++;
  if (fs->num == fs->alloc)
    {
      struct re_fail_stack_ent_t *new_array;
      new_array = realloc (fs->stack, (sizeof (struct re_fail_stack_ent_t)
				       * fs->alloc * 2));
      if (new_array == NULL)
	return REG_ESPACE;
      fs->alloc *= 2;
      fs->stack = new_array;
    }
  fs->stack[num].idx = str_idx;
  fs->stack[num].node = dest_node;
  fs->stack[num].regs = re_malloc (regmatch_t, nregs);
  if (fs->stack[num].regs == NULL)
    return REG_ESPACE;
  memcpy (fs->stack[num].regs, regs, sizeof (regmatch_t) * nregs);
  err = re_node_set_init_copy (&fs->stack[num].eps_via_nodes, eps_via_nodes);
  return err;
}

static int
internal_function
pop_fail_stack (struct re_fail_stack_t *fs, int *pidx, int nregs,
		regmatch_t *regs, re_node_set *eps_via_nodes)
{
  int num = --fs->num;
  assert (num >= 0);
  *pidx = fs->stack[num].idx;
  memcpy (regs, fs->stack[num].regs, sizeof (regmatch_t) * nregs);
  re_node_set_free (eps_via_nodes);
  re_free (fs->stack[num].regs);
  *eps_via_nodes = fs->stack[num].eps_via_nodes;
  return fs->stack[num].node;
}

/* Set the positions where the subexpressions are starts/ends to registers
   PMATCH.
   Note: We assume that pmatch[0] is already set, and
   pmatch[i].rm_so == pmatch[i].rm_eo == -1 for 0 < i < nmatch.  */

static reg_errcode_t
internal_function __attribute_warn_unused_result__
set_regs (const regex_t *preg, const re_match_context_t *mctx, size_t nmatch,
	  regmatch_t *pmatch, int fl_backtrack)
{
  const re_dfa_t *dfa = (const re_dfa_t *) preg->buffer;
  int idx, cur_node;
  re_node_set eps_via_nodes;
  struct re_fail_stack_t *fs;
  struct re_fail_stack_t fs_body = { 0, 2, NULL };
  regmatch_t *prev_idx_match;
  int prev_idx_match_malloced = 0;

#ifdef DEBUG
  assert (nmatch > 1);
  assert (mctx->state_log != NULL);
#endif
  if (fl_backtrack)
    {
      fs = &fs_body;
      fs->stack = re_malloc (struct re_fail_stack_ent_t, fs->alloc);
      if (fs->stack == NULL)
	return REG_ESPACE;
    }
  else
    fs = NULL;

  cur_node = dfa->init_node;
  re_node_set_init_empty (&eps_via_nodes);

  if (__libc_use_alloca (nmatch * sizeof (regmatch_t)))
    prev_idx_match = (regmatch_t *) alloca (nmatch * sizeof (regmatch_t));
  else
    {
      prev_idx_match = re_malloc (regmatch_t, nmatch);
      if (prev_idx_match == NULL)
	{
	  free_fail_stack_return (fs);
	  return REG_ESPACE;
	}
      prev_idx_match_malloced = 1;
    }
  memcpy (prev_idx_match, pmatch, sizeof (regmatch_t) * nmatch);

  for (idx = pmatch[0].rm_so; idx <= pmatch[0].rm_eo ;)
    {
      update_regs (dfa, pmatch, prev_idx_match, cur_node, idx, nmatch);

      if (idx == pmatch[0].rm_eo && cur_node == mctx->last_node)
	{
	  int reg_idx;
	  if (fs)
	    {
	      for (reg_idx = 0; reg_idx < nmatch; ++reg_idx)
		if (pmatch[reg_idx].rm_so > -1 && pmatch[reg_idx].rm_eo == -1)
		  break;
	      if (reg_idx == nmatch)
		{
		  re_node_set_free (&eps_via_nodes);
		  if (prev_idx_match_malloced)
		    re_free (prev_idx_match);
		  return free_fail_stack_return (fs);
		}
	      cur_node = pop_fail_stack (fs, &idx, nmatch, pmatch,
					 &eps_via_nodes);
	    }
	  else
	    {
	      re_node_set_free (&eps_via_nodes);
	      if (prev_idx_match_malloced)
		re_free (prev_idx_match);
	      return REG_NOERROR;
	    }
	}

      /* Proceed to next node.  */
      cur_node = proceed_next_node (mctx, nmatch, pmatch, &idx, cur_node,
				    &eps_via_nodes, fs);

      if (BE (cur_node < 0, 0))
	{
	  if (BE (cur_node == -2, 0))
	    {
	      re_node_set_free (&eps_via_nodes);
	      if (prev_idx_match_malloced)
		re_free (prev_idx_match);
	      free_fail_stack_return (fs);
	      return REG_ESPACE;
	    }
	  if (fs)
	    cur_node = pop_fail_stack (fs, &idx, nmatch, pmatch,
				       &eps_via_nodes);
	  else
	    {
	      re_node_set_free (&eps_via_nodes);
	      if (prev_idx_match_malloced)
		re_free (prev_idx_match);
	      return REG_NOMATCH;
	    }
	}
    }
  re_node_set_free (&eps_via_nodes);
  if (prev_idx_match_malloced)
    re_free (prev_idx_match);
  return free_fail_stack_return (fs);
}

static reg_errcode_t
internal_function
free_fail_stack_return (struct re_fail_stack_t *fs)
{
  if (fs)
    {
      int fs_idx;
      for (fs_idx = 0; fs_idx < fs->num; ++fs_idx)
	{
	  re_node_set_free (&fs->stack[fs_idx].eps_via_nodes);
	  re_free (fs->stack[fs_idx].regs);
	}
      re_free (fs->stack);
    }
  return REG_NOERROR;
}

static void
internal_function
update_regs (const re_dfa_t *dfa, regmatch_t *pmatch,
	     regmatch_t *prev_idx_match, int cur_node, int cur_idx, int nmatch)
{
  int type = dfa->nodes[cur_node].type;
  if (type == OP_OPEN_SUBEXP)
    {
      int reg_num = dfa->nodes[cur_node].opr.idx + 1;

      /* We are at the first node of this sub expression.  */
      if (reg_num < nmatch)
	{
	  pmatch[reg_num].rm_so = cur_idx;
	  pmatch[reg_num].rm_eo = -1;
	}
    }
  else if (type == OP_CLOSE_SUBEXP)
    {
      int reg_num = dfa->nodes[cur_node].opr.idx + 1;
      if (reg_num < nmatch)
	{
	  /* We are at the last node of this sub expression.  */
	  if (pmatch[reg_num].rm_so < cur_idx)
	    {
	      pmatch[reg_num].rm_eo = cur_idx;
	      /* This is a non-empty match or we are not inside an optional
		 subexpression.  Accept this right away.  */
	      memcpy (prev_idx_match, pmatch, sizeof (regmatch_t) * nmatch);
	    }
	  else
	    {
	      if (dfa->nodes[cur_node].opt_subexp
		  && prev_idx_match[reg_num].rm_so != -1)
		/* We transited through an empty match for an optional
		   subexpression, like (a?)*, and this is not the subexp's
		   first match.  Copy back the old content of the registers
		   so that matches of an inner subexpression are undone as
		   well, like in ((a?))*.  */
		memcpy (pmatch, prev_idx_match, sizeof (regmatch_t) * nmatch);
	      else
		/* We completed a subexpression, but it may be part of
		   an optional one, so do not update PREV_IDX_MATCH.  */
		pmatch[reg_num].rm_eo = cur_idx;
	    }
	}
    }
}

/* This function checks the STATE_LOG from the SCTX->last_str_idx to 0
   and sift the nodes in each states according to the following rules.
   Updated state_log will be wrote to STATE_LOG.

   Rules: We throw away the Node `a' in the STATE_LOG[STR_IDX] if...
     1. When STR_IDX == MATCH_LAST(the last index in the state_log):
	If `a' isn't the LAST_NODE and `a' can't epsilon transit to
	the LAST_NODE, we throw away the node `a'.
     2. When 0 <= STR_IDX < MATCH_LAST and `a' accepts
	string `s' and transit to `b':
	i. If 'b' isn't in the STATE_LOG[STR_IDX+strlen('s')], we throw
	   away the node `a'.
	ii. If 'b' is in the STATE_LOG[STR_IDX+strlen('s')] but 'b' is
	    thrown away, we throw away the node `a'.
     3. When 0 <= STR_IDX < MATCH_LAST and 'a' epsilon transit to 'b':
	i. If 'b' isn't in the STATE_LOG[STR_IDX], we throw away the
	   node `a'.
	ii. If 'b' is in the STATE_LOG[STR_IDX] but 'b' is thrown away,
	    we throw away the node `a'.  */

#define STATE_NODE_CONTAINS(state,node) \
  ((state) != NULL && re_node_set_contains (&(state)->nodes, node))

static reg_errcode_t
internal_function
sift_states_backward (const re_match_context_t *mctx, re_sift_context_t *sctx)
{
  reg_errcode_t err;
  int null_cnt = 0;
  int str_idx = sctx->last_str_idx;
  re_node_set cur_dest;

#ifdef DEBUG
  assert (mctx->state_log != NULL && mctx->state_log[str_idx] != NULL);
#endif

  /* Build sifted state_log[str_idx].  It has the nodes which can epsilon
     transit to the last_node and the last_node itself.  */
  err = re_node_set_init_1 (&cur_dest, sctx->last_node);
  if (BE (err != REG_NOERROR, 0))
    return err;
  err = update_cur_sifted_state (mctx, sctx, str_idx, &cur_dest);
  if (BE (err != REG_NOERROR, 0))
    goto free_return;

  /* Then check each states in the state_log.  */
  while (str_idx > 0)
    {
      /* Update counters.  */
      null_cnt = (sctx->sifted_states[str_idx] == NULL) ? null_cnt + 1 : 0;
      if (null_cnt > mctx->max_mb_elem_len)
	{
	  memset (sctx->sifted_states, '\0',
		  sizeof (re_dfastate_t *) * str_idx);
	  re_node_set_free (&cur_dest);
	  return REG_NOERROR;
	}
      re_node_set_empty (&cur_dest);
      --str_idx;

      if (mctx->state_log[str_idx])
	{
	  err = build_sifted_states (mctx, sctx, str_idx, &cur_dest);
	  if (BE (err != REG_NOERROR, 0))
	    goto free_return;
	}

      /* Add all the nodes which satisfy the following conditions:
	 - It can epsilon transit to a node in CUR_DEST.
	 - It is in CUR_SRC.
	 And update state_log.  */
      err = update_cur_sifted_state (mctx, sctx, str_idx, &cur_dest);
      if (BE (err != REG_NOERROR, 0))
	goto free_return;
    }
  err = REG_NOERROR;
 free_return:
  re_node_set_free (&cur_dest);
  return err;
}

static reg_errcode_t
internal_function __attribute_warn_unused_result__
build_sifted_states (const re_match_context_t *mctx, re_sift_context_t *sctx,
		     int str_idx, re_node_set *cur_dest)
{
  const re_dfa_t *const dfa = mctx->dfa;
  const re_node_set *cur_src = &mctx->state_log[str_idx]->non_eps_nodes;
  int i;

  /* Then build the next sifted state.
     We build the next sifted state on `cur_dest', and update
     `sifted_states[str_idx]' with `cur_dest'.
     Note:
     `cur_dest' is the sifted state from `state_log[str_idx + 1]'.
     `cur_src' points the node_set of the old `state_log[str_idx]'
     (with the epsilon nodes pre-filtered out).  */
  for (i = 0; i < cur_src->nelem; i++)
    {
      int prev_node = cur_src->elems[i];
      int naccepted = 0;
      int ret;

#ifdef DEBUG
      re_token_type_t type = dfa->nodes[prev_node].type;
      assert (!IS_EPSILON_NODE (type));
#endif
#ifdef RE_ENABLE_I18N
      /* If the node may accept `multi byte'.  */
      if (dfa->nodes[prev_node].accept_mb)
	naccepted = sift_states_iter_mb (mctx, sctx, prev_node,
					 str_idx, sctx->last_str_idx);
#endif /* RE_ENABLE_I18N */

      /* We don't check backreferences here.
	 See update_cur_sifted_state().  */
      if (!naccepted
	  && check_node_accept (mctx, dfa->nodes + prev_node, str_idx)
	  && STATE_NODE_CONTAINS (sctx->sifted_states[str_idx + 1],
				  dfa->nexts[prev_node]))
	naccepted = 1;

      if (naccepted == 0)
	continue;

      if (sctx->limits.nelem)
	{
	  int to_idx = str_idx + naccepted;
	  if (check_dst_limits (mctx, &sctx->limits,
				dfa->nexts[prev_node], to_idx,
				prev_node, str_idx))
	    continue;
	}
      ret = re_node_set_insert (cur_dest, prev_node);
      if (BE (ret == -1, 0))
	return REG_ESPACE;
    }

  return REG_NOERROR;
}

/* Helper functions.  */

static reg_errcode_t
internal_function
clean_state_log_if_needed (re_match_context_t *mctx, int next_state_log_idx)
{
  int top = mctx->state_log_top;

  if ((next_state_log_idx >= mctx->input.bufs_len
       && mctx->input.bufs_len < mctx->input.len)
      || (next_state_log_idx >= mctx->input.valid_len
	  && mctx->input.valid_len < mctx->input.len))
    {
      reg_errcode_t err;
      err = extend_buffers (mctx, next_state_log_idx + 1);
      if (BE (err != REG_NOERROR, 0))
	return err;
    }

  if (top < next_state_log_idx)
    {
      memset (mctx->state_log + top + 1, '\0',
	      sizeof (re_dfastate_t *) * (next_state_log_idx - top));
      mctx->state_log_top = next_state_log_idx;
    }
  return REG_NOERROR;
}

static reg_errcode_t
internal_function
merge_state_array (const re_dfa_t *dfa, re_dfastate_t **dst,
		   re_dfastate_t **src, int num)
{
  int st_idx;
  reg_errcode_t err;
  for (st_idx = 0; st_idx < num; ++st_idx)
    {
      if (dst[st_idx] == NULL)
	dst[st_idx] = src[st_idx];
      else if (src[st_idx] != NULL)
	{
	  re_node_set merged_set;
	  err = re_node_set_init_union (&merged_set, &dst[st_idx]->nodes,
					&src[st_idx]->nodes);
	  if (BE (err != REG_NOERROR, 0))
	    return err;
	  dst[st_idx] = re_acquire_state (&err, dfa, &merged_set);
	  re_node_set_free (&merged_set);
	  if (BE (err != REG_NOERROR, 0))
	    return err;
	}
    }
  return REG_NOERROR;
}

static reg_errcode_t
internal_function
update_cur_sifted_state (const re_match_context_t *mctx,
			 re_sift_context_t *sctx, int str_idx,
			 re_node_set *dest_nodes)
{
  const re_dfa_t *const dfa = mctx->dfa;
  reg_errcode_t err = REG_NOERROR;
  const re_node_set *candidates;
  candidates = ((mctx->state_log[str_idx] == NULL) ? NULL
		: &mctx->state_log[str_idx]->nodes);

  if (dest_nodes->nelem == 0)
    sctx->sifted_states[str_idx] = NULL;
  else
    {
      if (candidates)
	{
	  /* At first, add the nodes which can epsilon transit to a node in
	     DEST_NODE.  */
	  err = add_epsilon_src_nodes (dfa, dest_nodes, candidates);
	  if (BE (err != REG_NOERROR, 0))
	    return err;

	  /* Then, check the limitations in the current sift_context.  */
	  if (sctx->limits.nelem)
	    {
	      err = check_subexp_limits (dfa, dest_nodes, candidates, &sctx->limits,
					 mctx->bkref_ents, str_idx);
	      if (BE (err != REG_NOERROR, 0))
		return err;
	    }
	}

      sctx->sifted_states[str_idx] = re_acquire_state (&err, dfa, dest_nodes);
      if (BE (err != REG_NOERROR, 0))
	return err;
    }

  if (candidates && mctx->state_log[str_idx]->has_backref)
    {
      err = sift_states_bkref (mctx, sctx, str_idx, candidates);
      if (BE (err != REG_NOERROR, 0))
	return err;
    }
  return REG_NOERROR;
}

static reg_errcode_t
internal_function __attribute_warn_unused_result__
add_epsilon_src_nodes (const re_dfa_t *dfa, re_node_set *dest_nodes,
		       const re_node_set *candidates)
{
  reg_errcode_t err = REG_NOERROR;
  int i;

  re_dfastate_t *state = re_acquire_state (&err, dfa, dest_nodes);
  if (BE (err != REG_NOERROR, 0))
    return err;

  if (!state->inveclosure.alloc)
    {
      err = re_node_set_alloc (&state->inveclosure, dest_nodes->nelem);
      if (BE (err != REG_NOERROR, 0))
	return REG_ESPACE;
      for (i = 0; i < dest_nodes->nelem; i++)
	{
	  err = re_node_set_merge (&state->inveclosure,
				   dfa->inveclosures + dest_nodes->elems[i]);
	  if (BE (err != REG_NOERROR, 0))
	    return REG_ESPACE;
	}
    }
  return re_node_set_add_intersect (dest_nodes, candidates,
				    &state->inveclosure);
}

static reg_errcode_t
internal_function
sub_epsilon_src_nodes (const re_dfa_t *dfa, int node, re_node_set *dest_nodes,
		       const re_node_set *candidates)
{
    int ecl_idx;
    reg_errcode_t err;
    re_node_set *inv_eclosure = dfa->inveclosures + node;
    re_node_set except_nodes;
    re_node_set_init_empty (&except_nodes);
    for (ecl_idx = 0; ecl_idx < inv_eclosure->nelem; ++ecl_idx)
      {
	int cur_node = inv_eclosure->elems[ecl_idx];
	if (cur_node == node)
	  continue;
	if (IS_EPSILON_NODE (dfa->nodes[cur_node].type))
	  {
	    int edst1 = dfa->edests[cur_node].elems[0];
	    int edst2 = ((dfa->edests[cur_node].nelem > 1)
			 ? dfa->edests[cur_node].elems[1] : -1);
	    if ((!re_node_set_contains (inv_eclosure, edst1)
		 && re_node_set_contains (dest_nodes, edst1))
		|| (edst2 > 0
		    && !re_node_set_contains (inv_eclosure, edst2)
		    && re_node_set_contains (dest_nodes, edst2)))
	      {
		err = re_node_set_add_intersect (&except_nodes, candidates,
						 dfa->inveclosures + cur_node);
		if (BE (err != REG_NOERROR, 0))
		  {
		    re_node_set_free (&except_nodes);
		    return err;
		  }
	      }
	  }
      }
    for (ecl_idx = 0; ecl_idx < inv_eclosure->nelem; ++ecl_idx)
      {
	int cur_node = inv_eclosure->elems[ecl_idx];
	if (!re_node_set_contains (&except_nodes, cur_node))
	  {
	    int idx = re_node_set_contains (dest_nodes, cur_node) - 1;
	    re_node_set_remove_at (dest_nodes, idx);
	  }
      }
    re_node_set_free (&except_nodes);
    return REG_NOERROR;
}

static int
internal_function
check_dst_limits (const re_match_context_t *mctx, re_node_set *limits,
		  int dst_node, int dst_idx, int src_node, int src_idx)
{
  const re_dfa_t *const dfa = mctx->dfa;
  int lim_idx, src_pos, dst_pos;

  int dst_bkref_idx = search_cur_bkref_entry (mctx, dst_idx);
  int src_bkref_idx = search_cur_bkref_entry (mctx, src_idx);
  for (lim_idx = 0; lim_idx < limits->nelem; ++lim_idx)
    {
      int subexp_idx;
      struct re_backref_cache_entry *ent;
      ent = mctx->bkref_ents + limits->elems[lim_idx];
      subexp_idx = dfa->nodes[ent->node].opr.idx;

      dst_pos = check_dst_limits_calc_pos (mctx, limits->elems[lim_idx],
					   subexp_idx, dst_node, dst_idx,
					   dst_bkref_idx);
      src_pos = check_dst_limits_calc_pos (mctx, limits->elems[lim_idx],
					   subexp_idx, src_node, src_idx,
					   src_bkref_idx);

      /* In case of:
	 <src> <dst> ( <subexp> )
	 ( <subexp> ) <src> <dst>
	 ( <subexp1> <src> <subexp2> <dst> <subexp3> )  */
      if (src_pos == dst_pos)
	continue; /* This is unrelated limitation.  */
      else
	return 1;
    }
  return 0;
}

static int
internal_function
check_dst_limits_calc_pos_1 (const re_match_context_t *mctx, int boundaries,
			     int subexp_idx, int from_node, int bkref_idx)
{
  const re_dfa_t *const dfa = mctx->dfa;
  const re_node_set *eclosures = dfa->eclosures + from_node;
  int node_idx;

  /* Else, we are on the boundary: examine the nodes on the epsilon
     closure.  */
  for (node_idx = 0; node_idx < eclosures->nelem; ++node_idx)
    {
      int node = eclosures->elems[node_idx];
      switch (dfa->nodes[node].type)
	{
	case OP_BACK_REF:
	  if (bkref_idx != -1)
	    {
	      struct re_backref_cache_entry *ent = mctx->bkref_ents + bkref_idx;
	      do
		{
		  int dst, cpos;

		  if (ent->node != node)
		    continue;

		  if (subexp_idx < BITSET_WORD_BITS
		      && !(ent->eps_reachable_subexps_map
			   & ((bitset_word_t) 1 << subexp_idx)))
		    continue;

		  /* Recurse trying to reach the OP_OPEN_SUBEXP and
		     OP_CLOSE_SUBEXP cases below.  But, if the
		     destination node is the same node as the source
		     node, don't recurse because it would cause an
		     infinite loop: a regex that exhibits this behavior
		     is ()\1*\1*  */
		  dst = dfa->edests[node].elems[0];
		  if (dst == from_node)
		    {
		      if (boundaries & 1)
			return -1;
		      else /* if (boundaries & 2) */
			return 0;
		    }

		  cpos =
		    check_dst_limits_calc_pos_1 (mctx, boundaries, subexp_idx,
						 dst, bkref_idx);
		  if (cpos == -1 /* && (boundaries & 1) */)
		    return -1;
		  if (cpos == 0 && (boundaries & 2))
		    return 0;

		  if (subexp_idx < BITSET_WORD_BITS)
		    ent->eps_reachable_subexps_map
		      &= ~((bitset_word_t) 1 << subexp_idx);
		}
	      while (ent++->more);
	    }
	  break;

	case OP_OPEN_SUBEXP:
	  if ((boundaries & 1) && subexp_idx == dfa->nodes[node].opr.idx)
	    return -1;
	  break;

	case OP_CLOSE_SUBEXP:
	  if ((boundaries & 2) && subexp_idx == dfa->nodes[node].opr.idx)
	    return 0;
	  break;

	default:
	    break;
	}
    }

  return (boundaries & 2) ? 1 : 0;
}

static int
internal_function
check_dst_limits_calc_pos (const re_match_context_t *mctx, int limit,
			   int subexp_idx, int from_node, int str_idx,
			   int bkref_idx)
{
  struct re_backref_cache_entry *lim = mctx->bkref_ents + limit;
  int boundaries;

  /* If we are outside the range of the subexpression, return -1 or 1.  */
  if (str_idx < lim->subexp_from)
    return -1;

  if (lim->subexp_to < str_idx)
    return 1;

  /* If we are within the subexpression, return 0.  */
  boundaries = (str_idx == lim->subexp_from);
  boundaries |= (str_idx == lim->subexp_to) << 1;
  if (boundaries == 0)
    return 0;

  /* Else, examine epsilon closure.  */
  return check_dst_limits_calc_pos_1 (mctx, boundaries, subexp_idx,
				      from_node, bkref_idx);
}

/* Check the limitations of sub expressions LIMITS, and remove the nodes
   which are against limitations from DEST_NODES. */

static reg_errcode_t
internal_function
check_subexp_limits (const re_dfa_t *dfa, re_node_set *dest_nodes,
		     const re_node_set *candidates, re_node_set *limits,
		     struct re_backref_cache_entry *bkref_ents, int str_idx)
{
  reg_errcode_t err;
  int node_idx, lim_idx;

  for (lim_idx = 0; lim_idx < limits->nelem; ++lim_idx)
    {
      int subexp_idx;
      struct re_backref_cache_entry *ent;
      ent = bkref_ents + limits->elems[lim_idx];

      if (str_idx <= ent->subexp_from || ent->str_idx < str_idx)
	continue; /* This is unrelated limitation.  */

      subexp_idx = dfa->nodes[ent->node].opr.idx;
      if (ent->subexp_to == str_idx)
	{
	  int ops_node = -1;
	  int cls_node = -1;
	  for (node_idx = 0; node_idx < dest_nodes->nelem; ++node_idx)
	    {
	      int node = dest_nodes->elems[node_idx];
	      re_token_type_t type = dfa->nodes[node].type;
	      if (type == OP_OPEN_SUBEXP
		  && subexp_idx == dfa->nodes[node].opr.idx)
		ops_node = node;
	      else if (type == OP_CLOSE_SUBEXP
		       && subexp_idx == dfa->nodes[node].opr.idx)
		cls_node = node;
	    }

	  /* Check the limitation of the open subexpression.  */
	  /* Note that (ent->subexp_to = str_idx != ent->subexp_from).  */
	  if (ops_node >= 0)
	    {
	      err = sub_epsilon_src_nodes (dfa, ops_node, dest_nodes,
					   candidates);
	      if (BE (err != REG_NOERROR, 0))
		return err;
	    }

	  /* Check the limitation of the close subexpression.  */
	  if (cls_node >= 0)
	    for (node_idx = 0; node_idx < dest_nodes->nelem; ++node_idx)
	      {
		int node = dest_nodes->elems[node_idx];
		if (!re_node_set_contains (dfa->inveclosures + node,
					   cls_node)
		    && !re_node_set_contains (dfa->eclosures + node,
					      cls_node))
		  {
		    /* It is against this limitation.
		       Remove it form the current sifted state.  */
		    err = sub_epsilon_src_nodes (dfa, node, dest_nodes,
						 candidates);
		    if (BE (err != REG_NOERROR, 0))
		      return err;
		    --node_idx;
		  }
	      }
	}
      else /* (ent->subexp_to != str_idx)  */
	{
	  for (node_idx = 0; node_idx < dest_nodes->nelem; ++node_idx)
	    {
	      int node = dest_nodes->elems[node_idx];
	      re_token_type_t type = dfa->nodes[node].type;
	      if (type == OP_CLOSE_SUBEXP || type == OP_OPEN_SUBEXP)
		{
		  if (subexp_idx != dfa->nodes[node].opr.idx)
		    continue;
		  /* It is against this limitation.
		     Remove it form the current sifted state.  */
		  err = sub_epsilon_src_nodes (dfa, node, dest_nodes,
					       candidates);
		  if (BE (err != REG_NOERROR, 0))
		    return err;
		}
	    }
	}
    }
  return REG_NOERROR;
}

static reg_errcode_t
internal_function __attribute_warn_unused_result__
sift_states_bkref (const re_match_context_t *mctx, re_sift_context_t *sctx,
		   int str_idx, const re_node_set *candidates)
{
  const re_dfa_t *const dfa = mctx->dfa;
  reg_errcode_t err;
  int node_idx, node;
  re_sift_context_t local_sctx;
  int first_idx = search_cur_bkref_entry (mctx, str_idx);

  if (first_idx == -1)
    return REG_NOERROR;

  local_sctx.sifted_states = NULL; /* Mark that it hasn't been initialized.  */

  for (node_idx = 0; node_idx < candidates->nelem; ++node_idx)
    {
      int enabled_idx;
      re_token_type_t type;
      struct re_backref_cache_entry *entry;
      node = candidates->elems[node_idx];
      type = dfa->nodes[node].type;
      /* Avoid infinite loop for the REs like "()\1+".  */
      if (node == sctx->last_node && str_idx == sctx->last_str_idx)
	continue;
      if (type != OP_BACK_REF)
	continue;

      entry = mctx->bkref_ents + first_idx;
      enabled_idx = first_idx;
      do
	{
	  int subexp_len;
	  int to_idx;
	  int dst_node;
	  int ret;
	  re_dfastate_t *cur_state;

	  if (entry->node != node)
	    continue;
	  subexp_len = entry->subexp_to - entry->subexp_from;
	  to_idx = str_idx + subexp_len;
	  dst_node = (subexp_len ? dfa->nexts[node]
		      : dfa->edests[node].elems[0]);

	  if (to_idx > sctx->last_str_idx
	      || sctx->sifted_states[to_idx] == NULL
	      || !STATE_NODE_CONTAINS (sctx->sifted_states[to_idx], dst_node)
	      || check_dst_limits (mctx, &sctx->limits, node,
				   str_idx, dst_node, to_idx))
	    continue;

	  if (local_sctx.sifted_states == NULL)
	    {
	      local_sctx = *sctx;
	      err = re_node_set_init_copy (&local_sctx.limits, &sctx->limits);
	      if (BE (err != REG_NOERROR, 0))
		goto free_return;
	    }
	  local_sctx.last_node = node;
	  local_sctx.last_str_idx = str_idx;
	  ret = re_node_set_insert (&local_sctx.limits, enabled_idx);
	  if (BE (ret < 0, 0))
	    {
	      err = REG_ESPACE;
	      goto free_return;
	    }
	  cur_state = local_sctx.sifted_states[str_idx];
	  err = sift_states_backward (mctx, &local_sctx);
	  if (BE (err != REG_NOERROR, 0))
	    goto free_return;
	  if (sctx->limited_states != NULL)
	    {
	      err = merge_state_array (dfa, sctx->limited_states,
				       local_sctx.sifted_states,
				       str_idx + 1);
	      if (BE (err != REG_NOERROR, 0))
		goto free_return;
	    }
	  local_sctx.sifted_states[str_idx] = cur_state;
	  re_node_set_remove (&local_sctx.limits, enabled_idx);

	  /* mctx->bkref_ents may have changed, reload the pointer.  */
	  entry = mctx->bkref_ents + enabled_idx;
	}
      while (enabled_idx++, entry++->more);
    }
  err = REG_NOERROR;
 free_return:
  if (local_sctx.sifted_states != NULL)
    {
      re_node_set_free (&local_sctx.limits);
    }

  return err;
}


#ifdef RE_ENABLE_I18N
static int
internal_function
sift_states_iter_mb (const re_match_context_t *mctx, re_sift_context_t *sctx,
		     int node_idx, int str_idx, int max_str_idx)
{
  const re_dfa_t *const dfa = mctx->dfa;
  int naccepted;
  /* Check the node can accept `multi byte'.  */
  naccepted = check_node_accept_bytes (dfa, node_idx, &mctx->input, str_idx);
  if (naccepted > 0 && str_idx + naccepted <= max_str_idx &&
      !STATE_NODE_CONTAINS (sctx->sifted_states[str_idx + naccepted],
			    dfa->nexts[node_idx]))
    /* The node can't accept the `multi byte', or the
       destination was already thrown away, then the node
       could't accept the current input `multi byte'.   */
    naccepted = 0;
  /* Otherwise, it is sure that the node could accept
     `naccepted' bytes input.  */
  return naccepted;
}
#endif /* RE_ENABLE_I18N */


/* Functions for state transition.  */

/* Return the next state to which the current state STATE will transit by
   accepting the current input byte, and update STATE_LOG if necessary.
   If STATE can accept a multibyte char/collating element/back reference
   update the destination of STATE_LOG.  */

static re_dfastate_t *
internal_function __attribute_warn_unused_result__
transit_state (reg_errcode_t *err, re_match_context_t *mctx,
	       re_dfastate_t *state)
{
  re_dfastate_t **trtable;
  unsigned char ch;

#ifdef RE_ENABLE_I18N
  /* If the current state can accept multibyte.  */
  if (BE (state->accept_mb, 0))
    {
      *err = transit_state_mb (mctx, state);
      if (BE (*err != REG_NOERROR, 0))
	return NULL;
    }
#endif /* RE_ENABLE_I18N */

  /* Then decide the next state with the single byte.  */
#if 0
  if (0)
    /* don't use transition table  */
    return transit_state_sb (err, mctx, state);
#endif

  /* Use transition table  */
  ch = re_string_fetch_byte (&mctx->input);
  for (;;)
    {
      trtable = state->trtable;
      if (BE (trtable != NULL, 1))
	return trtable[ch];

      trtable = state->word_trtable;
      if (BE (trtable != NULL, 1))
	{
	  unsigned int context;
	  context
	    = re_string_context_at (&mctx->input,
				    re_string_cur_idx (&mctx->input) - 1,
				    mctx->eflags);
	  if (IS_WORD_CONTEXT (context))
	    return trtable[ch + SBC_MAX];
	  else
	    return trtable[ch];
	}

      if (!build_trtable (mctx->dfa, state))
	{
	  *err = REG_ESPACE;
	  return NULL;
	}

      /* Retry, we now have a transition table.  */
    }
}

/* Update the state_log if we need */
re_dfastate_t *
internal_function
merge_state_with_log (reg_errcode_t *err, re_match_context_t *mctx,
		      re_dfastate_t *next_state)
{
  const re_dfa_t *const dfa = mctx->dfa;
  int cur_idx = re_string_cur_idx (&mctx->input);

  if (cur_idx > mctx->state_log_top)
    {
      mctx->state_log[cur_idx] = next_state;
      mctx->state_log_top = cur_idx;
    }
  else if (mctx->state_log[cur_idx] == 0)
    {
      mctx->state_log[cur_idx] = next_state;
    }
  else
    {
      re_dfastate_t *pstate;
      unsigned int context;
      re_node_set next_nodes, *log_nodes, *table_nodes = NULL;
      /* If (state_log[cur_idx] != 0), it implies that cur_idx is
	 the destination of a multibyte char/collating element/
	 back reference.  Then the next state is the union set of
	 these destinations and the results of the transition table.  */
      pstate = mctx->state_log[cur_idx];
      log_nodes = pstate->entrance_nodes;
      if (next_state != NULL)
	{
	  table_nodes = next_state->entrance_nodes;
	  *err = re_node_set_init_union (&next_nodes, table_nodes,
					     log_nodes);
	  if (BE (*err != REG_NOERROR, 0))
	    return NULL;
	}
      else
	next_nodes = *log_nodes;
      /* Note: We already add the nodes of the initial state,
	 then we don't need to add them here.  */

      context = re_string_context_at (&mctx->input,
				      re_string_cur_idx (&mctx->input) - 1,
				      mctx->eflags);
      next_state = mctx->state_log[cur_idx]
	= re_acquire_state_context (err, dfa, &next_nodes, context);
      /* We don't need to check errors here, since the return value of
	 this function is next_state and ERR is already set.  */

      if (table_nodes != NULL)
	re_node_set_free (&next_nodes);
    }

  if (BE (dfa->nbackref, 0) && next_state != NULL)
    {
      /* Check OP_OPEN_SUBEXP in the current state in case that we use them
	 later.  We must check them here, since the back references in the
	 next state might use them.  */
      *err = check_subexp_matching_top (mctx, &next_state->nodes,
					cur_idx);
      if (BE (*err != REG_NOERROR, 0))
	return NULL;

      /* If the next state has back references.  */
      if (next_state->has_backref)
	{
	  *err = transit_state_bkref (mctx, &next_state->nodes);
	  if (BE (*err != REG_NOERROR, 0))
	    return NULL;
	  next_state = mctx->state_log[cur_idx];
	}
    }

  return next_state;
}

/* Skip bytes in the input that correspond to part of a
   multi-byte match, then look in the log for a state
   from which to restart matching.  */
re_dfastate_t *
internal_function
find_recover_state (reg_errcode_t *err, re_match_context_t *mctx)
{
  re_dfastate_t *cur_state;
  do
    {
      int max = mctx->state_log_top;
      int cur_str_idx = re_string_cur_idx (&mctx->input);

      do
	{
	  if (++cur_str_idx > max)
	    return NULL;
	  re_string_skip_bytes (&mctx->input, 1);
	}
      while (mctx->state_log[cur_str_idx] == NULL);

      cur_state = merge_state_with_log (err, mctx, NULL);
    }
  while (*err == REG_NOERROR && cur_state == NULL);
  return cur_state;
}

/* Helper functions for transit_state.  */

/* From the node set CUR_NODES, pick up the nodes whose types are
   OP_OPEN_SUBEXP and which have corresponding back references in the regular
   expression. And register them to use them later for evaluating the
   correspoding back references.  */

static reg_errcode_t
internal_function
check_subexp_matching_top (re_match_context_t *mctx, re_node_set *cur_nodes,
			   int str_idx)
{
  const re_dfa_t *const dfa = mctx->dfa;
  int node_idx;
  reg_errcode_t err;

  /* TODO: This isn't efficient.
	   Because there might be more than one nodes whose types are
	   OP_OPEN_SUBEXP and whose index is SUBEXP_IDX, we must check all
	   nodes.
	   E.g. RE: (a){2}  */
  for (node_idx = 0; node_idx < cur_nodes->nelem; ++node_idx)
    {
      int node = cur_nodes->elems[node_idx];
      if (dfa->nodes[node].type == OP_OPEN_SUBEXP
	  && dfa->nodes[node].opr.idx < BITSET_WORD_BITS
	  && (dfa->used_bkref_map
	      & ((bitset_word_t) 1 << dfa->nodes[node].opr.idx)))
	{
	  err = match_ctx_add_subtop (mctx, node, str_idx);
	  if (BE (err != REG_NOERROR, 0))
	    return err;
	}
    }
  return REG_NOERROR;
}

#if 0
/* Return the next state to which the current state STATE will transit by
   accepting the current input byte.  */

static re_dfastate_t *
transit_state_sb (reg_errcode_t *err, re_match_context_t *mctx,
		  re_dfastate_t *state)
{
  const re_dfa_t *const dfa = mctx->dfa;
  re_node_set next_nodes;
  re_dfastate_t *next_state;
  int node_cnt, cur_str_idx = re_string_cur_idx (&mctx->input);
  unsigned int context;

  *err = re_node_set_alloc (&next_nodes, state->nodes.nelem + 1);
  if (BE (*err != REG_NOERROR, 0))
    return NULL;
  for (node_cnt = 0; node_cnt < state->nodes.nelem; ++node_cnt)
    {
      int cur_node = state->nodes.elems[node_cnt];
      if (check_node_accept (mctx, dfa->nodes + cur_node, cur_str_idx))
	{
	  *err = re_node_set_merge (&next_nodes,
				    dfa->eclosures + dfa->nexts[cur_node]);
	  if (BE (*err != REG_NOERROR, 0))
	    {
	      re_node_set_free (&next_nodes);
	      return NULL;
	    }
	}
    }
  context = re_string_context_at (&mctx->input, cur_str_idx, mctx->eflags);
  next_state = re_acquire_state_context (err, dfa, &next_nodes, context);
  /* We don't need to check errors here, since the return value of
     this function is next_state and ERR is already set.  */

  re_node_set_free (&next_nodes);
  re_string_skip_bytes (&mctx->input, 1);
  return next_state;
}
#endif

#ifdef RE_ENABLE_I18N
static reg_errcode_t
internal_function
transit_state_mb (re_match_context_t *mctx, re_dfastate_t *pstate)
{
  const re_dfa_t *const dfa = mctx->dfa;
  reg_errcode_t err;
  int i;

  for (i = 0; i < pstate->nodes.nelem; ++i)
    {
      re_node_set dest_nodes, *new_nodes;
      int cur_node_idx = pstate->nodes.elems[i];
      int naccepted, dest_idx;
      unsigned int context;
      re_dfastate_t *dest_state;

      if (!dfa->nodes[cur_node_idx].accept_mb)
	continue;

      if (dfa->nodes[cur_node_idx].constraint)
	{
	  context = re_string_context_at (&mctx->input,
					  re_string_cur_idx (&mctx->input),
					  mctx->eflags);
	  if (NOT_SATISFY_NEXT_CONSTRAINT (dfa->nodes[cur_node_idx].constraint,
					   context))
	    continue;
	}

      /* How many bytes the node can accept?  */
      naccepted = check_node_accept_bytes (dfa, cur_node_idx, &mctx->input,
					   re_string_cur_idx (&mctx->input));
      if (naccepted == 0)
	continue;

      /* The node can accepts `naccepted' bytes.  */
      dest_idx = re_string_cur_idx (&mctx->input) + naccepted;
      mctx->max_mb_elem_len = ((mctx->max_mb_elem_len < naccepted) ? naccepted
			       : mctx->max_mb_elem_len);
      err = clean_state_log_if_needed (mctx, dest_idx);
      if (BE (err != REG_NOERROR, 0))
	return err;
#ifdef DEBUG
      assert (dfa->nexts[cur_node_idx] != -1);
#endif
      new_nodes = dfa->eclosures + dfa->nexts[cur_node_idx];

      dest_state = mctx->state_log[dest_idx];
      if (dest_state == NULL)
	dest_nodes = *new_nodes;
      else
	{
	  err = re_node_set_init_union (&dest_nodes,
					dest_state->entrance_nodes, new_nodes);
	  if (BE (err != REG_NOERROR, 0))
	    return err;
	}
      context = re_string_context_at (&mctx->input, dest_idx - 1,
				      mctx->eflags);
      mctx->state_log[dest_idx]
	= re_acquire_state_context (&err, dfa, &dest_nodes, context);
      if (dest_state != NULL)
	re_node_set_free (&dest_nodes);
      if (BE (mctx->state_log[dest_idx] == NULL && err != REG_NOERROR, 0))
	return err;
    }
  return REG_NOERROR;
}
#endif /* RE_ENABLE_I18N */

static reg_errcode_t
internal_function
transit_state_bkref (re_match_context_t *mctx, const re_node_set *nodes)
{
  const re_dfa_t *const dfa = mctx->dfa;
  reg_errcode_t err;
  int i;
  int cur_str_idx = re_string_cur_idx (&mctx->input);

  for (i = 0; i < nodes->nelem; ++i)
    {
      int dest_str_idx, prev_nelem, bkc_idx;
      int node_idx = nodes->elems[i];
      unsigned int context;
      const re_token_t *node = dfa->nodes + node_idx;
      re_node_set *new_dest_nodes;

      /* Check whether `node' is a backreference or not.  */
      if (node->type != OP_BACK_REF)
	continue;

      if (node->constraint)
	{
	  context = re_string_context_at (&mctx->input, cur_str_idx,
					  mctx->eflags);
	  if (NOT_SATISFY_NEXT_CONSTRAINT (node->constraint, context))
	    continue;
	}

      /* `node' is a backreference.
	 Check the substring which the substring matched.  */
      bkc_idx = mctx->nbkref_ents;
      err = get_subexp (mctx, node_idx, cur_str_idx);
      if (BE (err != REG_NOERROR, 0))
	goto free_return;

      /* And add the epsilon closures (which is `new_dest_nodes') of
	 the backreference to appropriate state_log.  */
#ifdef DEBUG
      assert (dfa->nexts[node_idx] != -1);
#endif
      for (; bkc_idx < mctx->nbkref_ents; ++bkc_idx)
	{
	  int subexp_len;
	  re_dfastate_t *dest_state;
	  struct re_backref_cache_entry *bkref_ent;
	  bkref_ent = mctx->bkref_ents + bkc_idx;
	  if (bkref_ent->node != node_idx || bkref_ent->str_idx != cur_str_idx)
	    continue;
	  subexp_len = bkref_ent->subexp_to - bkref_ent->subexp_from;
	  new_dest_nodes = (subexp_len == 0
			    ? dfa->eclosures + dfa->edests[node_idx].elems[0]
			    : dfa->eclosures + dfa->nexts[node_idx]);
	  dest_str_idx = (cur_str_idx + bkref_ent->subexp_to
			  - bkref_ent->subexp_from);
	  context = re_string_context_at (&mctx->input, dest_str_idx - 1,
					  mctx->eflags);
	  dest_state = mctx->state_log[dest_str_idx];
	  prev_nelem = ((mctx->state_log[cur_str_idx] == NULL) ? 0
			: mctx->state_log[cur_str_idx]->nodes.nelem);
	  /* Add `new_dest_node' to state_log.  */
	  if (dest_state == NULL)
	    {
	      mctx->state_log[dest_str_idx]
		= re_acquire_state_context (&err, dfa, new_dest_nodes,
					    context);
	      if (BE (mctx->state_log[dest_str_idx] == NULL
		      && err != REG_NOERROR, 0))
		goto free_return;
	    }
	  else
	    {
	      re_node_set dest_nodes;
	      err = re_node_set_init_union (&dest_nodes,
					    dest_state->entrance_nodes,
					    new_dest_nodes);
	      if (BE (err != REG_NOERROR, 0))
		{
		  re_node_set_free (&dest_nodes);
		  goto free_return;
		}
	      mctx->state_log[dest_str_idx]
		= re_acquire_state_context (&err, dfa, &dest_nodes, context);
	      re_node_set_free (&dest_nodes);
	      if (BE (mctx->state_log[dest_str_idx] == NULL
		      && err != REG_NOERROR, 0))
		goto free_return;
	    }
	  /* We need to check recursively if the backreference can epsilon
	     transit.  */
	  if (subexp_len == 0
	      && mctx->state_log[cur_str_idx]->nodes.nelem > prev_nelem)
	    {
	      err = check_subexp_matching_top (mctx, new_dest_nodes,
					       cur_str_idx);
	      if (BE (err != REG_NOERROR, 0))
		goto free_return;
	      err = transit_state_bkref (mctx, new_dest_nodes);
	      if (BE (err != REG_NOERROR, 0))
		goto free_return;
	    }
	}
    }
  err = REG_NOERROR;
 free_return:
  return err;
}

/* Enumerate all the candidates which the backreference BKREF_NODE can match
   at BKREF_STR_IDX, and register them by match_ctx_add_entry().
   Note that we might collect inappropriate candidates here.
   However, the cost of checking them strictly here is too high, then we
   delay these checking for prune_impossible_nodes().  */

static reg_errcode_t
internal_function __attribute_warn_unused_result__
get_subexp (re_match_context_t *mctx, int bkref_node, int bkref_str_idx)
{
  const re_dfa_t *const dfa = mctx->dfa;
  int subexp_num, sub_top_idx;
  const char *buf = (const char *) re_string_get_buffer (&mctx->input);
  /* Return if we have already checked BKREF_NODE at BKREF_STR_IDX.  */
  int cache_idx = search_cur_bkref_entry (mctx, bkref_str_idx);
  if (cache_idx != -1)
    {
      const struct re_backref_cache_entry *entry
	= mctx->bkref_ents + cache_idx;
      do
	if (entry->node == bkref_node)
	  return REG_NOERROR; /* We already checked it.  */
      while (entry++->more);
    }

  subexp_num = dfa->nodes[bkref_node].opr.idx;

  /* For each sub expression  */
  for (sub_top_idx = 0; sub_top_idx < mctx->nsub_tops; ++sub_top_idx)
    {
      reg_errcode_t err;
      re_sub_match_top_t *sub_top = mctx->sub_tops[sub_top_idx];
      re_sub_match_last_t *sub_last;
      int sub_last_idx, sl_str, bkref_str_off;

      if (dfa->nodes[sub_top->node].opr.idx != subexp_num)
	continue; /* It isn't related.  */

      sl_str = sub_top->str_idx;
      bkref_str_off = bkref_str_idx;
      /* At first, check the last node of sub expressions we already
	 evaluated.  */
      for (sub_last_idx = 0; sub_last_idx < sub_top->nlasts; ++sub_last_idx)
	{
	  int sl_str_diff;
	  sub_last = sub_top->lasts[sub_last_idx];
	  sl_str_diff = sub_last->str_idx - sl_str;
	  /* The matched string by the sub expression match with the substring
	     at the back reference?  */
	  if (sl_str_diff > 0)
	    {
	      if (BE (bkref_str_off + sl_str_diff > mctx->input.valid_len, 0))
		{
		  /* Not enough chars for a successful match.  */
		  if (bkref_str_off + sl_str_diff > mctx->input.len)
		    break;

		  err = clean_state_log_if_needed (mctx,
						   bkref_str_off
						   + sl_str_diff);
		  if (BE (err != REG_NOERROR, 0))
		    return err;
		  buf = (const char *) re_string_get_buffer (&mctx->input);
		}
	      if (memcmp (buf + bkref_str_off, buf + sl_str, sl_str_diff) != 0)
		/* We don't need to search this sub expression any more.  */
		break;
	    }
	  bkref_str_off += sl_str_diff;
	  sl_str += sl_str_diff;
	  err = get_subexp_sub (mctx, sub_top, sub_last, bkref_node,
				bkref_str_idx);

	  /* Reload buf, since the preceding call might have reallocated
	     the buffer.  */
	  buf = (const char *) re_string_get_buffer (&mctx->input);

	  if (err == REG_NOMATCH)
	    continue;
	  if (BE (err != REG_NOERROR, 0))
	    return err;
	}

      if (sub_last_idx < sub_top->nlasts)
	continue;
      if (sub_last_idx > 0)
	++sl_str;
      /* Then, search for the other last nodes of the sub expression.  */
      for (; sl_str <= bkref_str_idx; ++sl_str)
	{
	  int cls_node, sl_str_off;
	  const re_node_set *nodes;
	  sl_str_off = sl_str - sub_top->str_idx;
	  /* The matched string by the sub expression match with the substring
	     at the back reference?  */
	  if (sl_str_off > 0)
	    {
	      if (BE (bkref_str_off >= mctx->input.valid_len, 0))
		{
		  /* If we are at the end of the input, we cannot match.  */
		  if (bkref_str_off >= mctx->input.len)
		    break;

		  err = extend_buffers (mctx, bkref_str_off + 1);
		  if (BE (err != REG_NOERROR, 0))
		    return err;

		  buf = (const char *) re_string_get_buffer (&mctx->input);
		}
	      if (buf [bkref_str_off++] != buf[sl_str - 1])
		break; /* We don't need to search this sub expression
			  any more.  */
	    }
	  if (mctx->state_log[sl_str] == NULL)
	    continue;
	  /* Does this state have a ')' of the sub expression?  */
	  nodes = &mctx->state_log[sl_str]->nodes;
	  cls_node = find_subexp_node (dfa, nodes, subexp_num,
				       OP_CLOSE_SUBEXP);
	  if (cls_node == -1)
	    continue; /* No.  */
	  if (sub_top->path == NULL)
	    {
	      sub_top->path = calloc (sizeof (state_array_t),
				      sl_str - sub_top->str_idx + 1);
	      if (sub_top->path == NULL)
		return REG_ESPACE;
	    }
	  /* Can the OP_OPEN_SUBEXP node arrive the OP_CLOSE_SUBEXP node
	     in the current context?  */
	  err = check_arrival (mctx, sub_top->path, sub_top->node,
			       sub_top->str_idx, cls_node, sl_str,
			       OP_CLOSE_SUBEXP);
	  if (err == REG_NOMATCH)
	      continue;
	  if (BE (err != REG_NOERROR, 0))
	      return err;
	  sub_last = match_ctx_add_sublast (sub_top, cls_node, sl_str);
	  if (BE (sub_last == NULL, 0))
	    return REG_ESPACE;
	  err = get_subexp_sub (mctx, sub_top, sub_last, bkref_node,
				bkref_str_idx);
	  if (err == REG_NOMATCH)
	    continue;
	}
    }
  return REG_NOERROR;
}

/* Helper functions for get_subexp().  */

/* Check SUB_LAST can arrive to the back reference BKREF_NODE at BKREF_STR.
   If it can arrive, register the sub expression expressed with SUB_TOP
   and SUB_LAST.  */

static reg_errcode_t
internal_function
get_subexp_sub (re_match_context_t *mctx, const re_sub_match_top_t *sub_top,
		re_sub_match_last_t *sub_last, int bkref_node, int bkref_str)
{
  reg_errcode_t err;
  int to_idx;
  /* Can the subexpression arrive the back reference?  */
  err = check_arrival (mctx, &sub_last->path, sub_last->node,
		       sub_last->str_idx, bkref_node, bkref_str,
		       OP_OPEN_SUBEXP);
  if (err != REG_NOERROR)
    return err;
  err = match_ctx_add_entry (mctx, bkref_node, bkref_str, sub_top->str_idx,
			     sub_last->str_idx);
  if (BE (err != REG_NOERROR, 0))
    return err;
  to_idx = bkref_str + sub_last->str_idx - sub_top->str_idx;
  return clean_state_log_if_needed (mctx, to_idx);
}

/* Find the first node which is '(' or ')' and whose index is SUBEXP_IDX.
   Search '(' if FL_OPEN, or search ')' otherwise.
   TODO: This function isn't efficient...
	 Because there might be more than one nodes whose types are
	 OP_OPEN_SUBEXP and whose index is SUBEXP_IDX, we must check all
	 nodes.
	 E.g. RE: (a){2}  */

static int
internal_function
find_subexp_node (const re_dfa_t *dfa, const re_node_set *nodes,
		  int subexp_idx, int type)
{
  int cls_idx;
  for (cls_idx = 0; cls_idx < nodes->nelem; ++cls_idx)
    {
      int cls_node = nodes->elems[cls_idx];
      const re_token_t *node = dfa->nodes + cls_node;
      if (node->type == type
	  && node->opr.idx == subexp_idx)
	return cls_node;
    }
  return -1;
}

/* Check whether the node TOP_NODE at TOP_STR can arrive to the node
   LAST_NODE at LAST_STR.  We record the path onto PATH since it will be
   heavily reused.
   Return REG_NOERROR if it can arrive, or REG_NOMATCH otherwise.  */

static reg_errcode_t
internal_function __attribute_warn_unused_result__
check_arrival (re_match_context_t *mctx, state_array_t *path, int top_node,
	       int top_str, int last_node, int last_str, int type)
{
  const re_dfa_t *const dfa = mctx->dfa;
  reg_errcode_t err = REG_NOERROR;
  int subexp_num, backup_cur_idx, str_idx, null_cnt;
  re_dfastate_t *cur_state = NULL;
  re_node_set *cur_nodes, next_nodes;
  re_dfastate_t **backup_state_log;
  unsigned int context;

  subexp_num = dfa->nodes[top_node].opr.idx;
  /* Extend the buffer if we need.  */
  if (BE (path->alloc < last_str + mctx->max_mb_elem_len + 1, 0))
    {
      re_dfastate_t **new_array;
      int old_alloc = path->alloc;
      path->alloc += last_str + mctx->max_mb_elem_len + 1;
      new_array = re_realloc (path->array, re_dfastate_t *, path->alloc);
      if (BE (new_array == NULL, 0))
	{
	  path->alloc = old_alloc;
	  return REG_ESPACE;
	}
      path->array = new_array;
      memset (new_array + old_alloc, '\0',
	      sizeof (re_dfastate_t *) * (path->alloc - old_alloc));
    }

  str_idx = path->next_idx ?: top_str;

  /* Temporary modify MCTX.  */
  backup_state_log = mctx->state_log;
  backup_cur_idx = mctx->input.cur_idx;
  mctx->state_log = path->array;
  mctx->input.cur_idx = str_idx;

  /* Setup initial node set.  */
  context = re_string_context_at (&mctx->input, str_idx - 1, mctx->eflags);
  if (str_idx == top_str)
    {
      err = re_node_set_init_1 (&next_nodes, top_node);
      if (BE (err != REG_NOERROR, 0))
	return err;
      err = check_arrival_expand_ecl (dfa, &next_nodes, subexp_num, type);
      if (BE (err != REG_NOERROR, 0))
	{
	  re_node_set_free (&next_nodes);
	  return err;
	}
    }
  else
    {
      cur_state = mctx->state_log[str_idx];
      if (cur_state && cur_state->has_backref)
	{
	  err = re_node_set_init_copy (&next_nodes, &cur_state->nodes);
	  if (BE (err != REG_NOERROR, 0))
	    return err;
	}
      else
	re_node_set_init_empty (&next_nodes);
    }
  if (str_idx == top_str || (cur_state && cur_state->has_backref))
    {
      if (next_nodes.nelem)
	{
	  err = expand_bkref_cache (mctx, &next_nodes, str_idx,
				    subexp_num, type);
	  if (BE (err != REG_NOERROR, 0))
	    {
	      re_node_set_free (&next_nodes);
	      return err;
	    }
	}
      cur_state = re_acquire_state_context (&err, dfa, &next_nodes, context);
      if (BE (cur_state == NULL && err != REG_NOERROR, 0))
	{
	  re_node_set_free (&next_nodes);
	  return err;
	}
      mctx->state_log[str_idx] = cur_state;
    }

  for (null_cnt = 0; str_idx < last_str && null_cnt <= mctx->max_mb_elem_len;)
    {
      re_node_set_empty (&next_nodes);
      if (mctx->state_log[str_idx + 1])
	{
	  err = re_node_set_merge (&next_nodes,
				   &mctx->state_log[str_idx + 1]->nodes);
	  if (BE (err != REG_NOERROR, 0))
	    {
	      re_node_set_free (&next_nodes);
	      return err;
	    }
	}
      if (cur_state)
	{
	  err = check_arrival_add_next_nodes (mctx, str_idx,
					      &cur_state->non_eps_nodes,
					      &next_nodes);
	  if (BE (err != REG_NOERROR, 0))
	    {
	      re_node_set_free (&next_nodes);
	      return err;
	    }
	}
      ++str_idx;
      if (next_nodes.nelem)
	{
	  err = check_arrival_expand_ecl (dfa, &next_nodes, subexp_num, type);
	  if (BE (err != REG_NOERROR, 0))
	    {
	      re_node_set_free (&next_nodes);
	      return err;
	    }
	  err = expand_bkref_cache (mctx, &next_nodes, str_idx,
				    subexp_num, type);
	  if (BE (err != REG_NOERROR, 0))
	    {
	      re_node_set_free (&next_nodes);
	      return err;
	    }
	}
      context = re_string_context_at (&mctx->input, str_idx - 1, mctx->eflags);
      cur_state = re_acquire_state_context (&err, dfa, &next_nodes, context);
      if (BE (cur_state == NULL && err != REG_NOERROR, 0))
	{
	  re_node_set_free (&next_nodes);
	  return err;
	}
      mctx->state_log[str_idx] = cur_state;
      null_cnt = cur_state == NULL ? null_cnt + 1 : 0;
    }
  re_node_set_free (&next_nodes);
  cur_nodes = (mctx->state_log[last_str] == NULL ? NULL
	       : &mctx->state_log[last_str]->nodes);
  path->next_idx = str_idx;

  /* Fix MCTX.  */
  mctx->state_log = backup_state_log;
  mctx->input.cur_idx = backup_cur_idx;

  /* Then check the current node set has the node LAST_NODE.  */
  if (cur_nodes != NULL && re_node_set_contains (cur_nodes, last_node))
    return REG_NOERROR;

  return REG_NOMATCH;
}

/* Helper functions for check_arrival.  */

/* Calculate the destination nodes of CUR_NODES at STR_IDX, and append them
   to NEXT_NODES.
   TODO: This function is similar to the functions transit_state*(),
	 however this function has many additional works.
	 Can't we unify them?  */

static reg_errcode_t
internal_function __attribute_warn_unused_result__
check_arrival_add_next_nodes (re_match_context_t *mctx, int str_idx,
			      re_node_set *cur_nodes, re_node_set *next_nodes)
{
  const re_dfa_t *const dfa = mctx->dfa;
  int result;
  int cur_idx;
  reg_errcode_t err = REG_NOERROR;
  re_node_set union_set;
  re_node_set_init_empty (&union_set);
  for (cur_idx = 0; cur_idx < cur_nodes->nelem; ++cur_idx)
    {
      int naccepted = 0;
      int cur_node = cur_nodes->elems[cur_idx];
#ifdef DEBUG
      re_token_type_t type = dfa->nodes[cur_node].type;
      assert (!IS_EPSILON_NODE (type));
#endif
#ifdef RE_ENABLE_I18N
      /* If the node may accept `multi byte'.  */
      if (dfa->nodes[cur_node].accept_mb)
	{
	  naccepted = check_node_accept_bytes (dfa, cur_node, &mctx->input,
					       str_idx);
	  if (naccepted > 1)
	    {
	      re_dfastate_t *dest_state;
	      int next_node = dfa->nexts[cur_node];
	      int next_idx = str_idx + naccepted;
	      dest_state = mctx->state_log[next_idx];
	      re_node_set_empty (&union_set);
	      if (dest_state)
		{
		  err = re_node_set_merge (&union_set, &dest_state->nodes);
		  if (BE (err != REG_NOERROR, 0))
		    {
		      re_node_set_free (&union_set);
		      return err;
		    }
		}
	      result = re_node_set_insert (&union_set, next_node);
	      if (BE (result < 0, 0))
		{
		  re_node_set_free (&union_set);
		  return REG_ESPACE;
		}
	      mctx->state_log[next_idx] = re_acquire_state (&err, dfa,
							    &union_set);
	      if (BE (mctx->state_log[next_idx] == NULL
		      && err != REG_NOERROR, 0))
		{
		  re_node_set_free (&union_set);
		  return err;
		}
	    }
	}
#endif /* RE_ENABLE_I18N */
      if (naccepted
	  || check_node_accept (mctx, dfa->nodes + cur_node, str_idx))
	{
	  result = re_node_set_insert (next_nodes, dfa->nexts[cur_node]);
	  if (BE (result < 0, 0))
	    {
	      re_node_set_free (&union_set);
	      return REG_ESPACE;
	    }
	}
    }
  re_node_set_free (&union_set);
  return REG_NOERROR;
}

/* For all the nodes in CUR_NODES, add the epsilon closures of them to
   CUR_NODES, however exclude the nodes which are:
    - inside the sub expression whose number is EX_SUBEXP, if FL_OPEN.
    - out of the sub expression whose number is EX_SUBEXP, if !FL_OPEN.
*/

static reg_errcode_t
internal_function
check_arrival_expand_ecl (const re_dfa_t *dfa, re_node_set *cur_nodes,
			  int ex_subexp, int type)
{
  reg_errcode_t err;
  int idx, outside_node;
  re_node_set new_nodes;
#ifdef DEBUG
  assert (cur_nodes->nelem);
#endif
  err = re_node_set_alloc (&new_nodes, cur_nodes->nelem);
  if (BE (err != REG_NOERROR, 0))
    return err;
  /* Create a new node set NEW_NODES with the nodes which are epsilon
     closures of the node in CUR_NODES.  */

  for (idx = 0; idx < cur_nodes->nelem; ++idx)
    {
      int cur_node = cur_nodes->elems[idx];
      const re_node_set *eclosure = dfa->eclosures + cur_node;
      outside_node = find_subexp_node (dfa, eclosure, ex_subexp, type);
      if (outside_node == -1)
	{
	  /* There are no problematic nodes, just merge them.  */
	  err = re_node_set_merge (&new_nodes, eclosure);
	  if (BE (err != REG_NOERROR, 0))
	    {
	      re_node_set_free (&new_nodes);
	      return err;
	    }
	}
      else
	{
	  /* There are problematic nodes, re-calculate incrementally.  */
	  err = check_arrival_expand_ecl_sub (dfa, &new_nodes, cur_node,
					      ex_subexp, type);
	  if (BE (err != REG_NOERROR, 0))
	    {
	      re_node_set_free (&new_nodes);
	      return err;
	    }
	}
    }
  re_node_set_free (cur_nodes);
  *cur_nodes = new_nodes;
  return REG_NOERROR;
}

/* Helper function for check_arrival_expand_ecl.
   Check incrementally the epsilon closure of TARGET, and if it isn't
   problematic append it to DST_NODES.  */

static reg_errcode_t
internal_function __attribute_warn_unused_result__
check_arrival_expand_ecl_sub (const re_dfa_t *dfa, re_node_set *dst_nodes,
			      int target, int ex_subexp, int type)
{
  int cur_node;
  for (cur_node = target; !re_node_set_contains (dst_nodes, cur_node);)
    {
      int err;

      if (dfa->nodes[cur_node].type == type
	  && dfa->nodes[cur_node].opr.idx == ex_subexp)
	{
	  if (type == OP_CLOSE_SUBEXP)
	    {
	      err = re_node_set_insert (dst_nodes, cur_node);
	      if (BE (err == -1, 0))
		return REG_ESPACE;
	    }
	  break;
	}
      err = re_node_set_insert (dst_nodes, cur_node);
      if (BE (err == -1, 0))
	return REG_ESPACE;
      if (dfa->edests[cur_node].nelem == 0)
	break;
      if (dfa->edests[cur_node].nelem == 2)
	{
	  err = check_arrival_expand_ecl_sub (dfa, dst_nodes,
					      dfa->edests[cur_node].elems[1],
					      ex_subexp, type);
	  if (BE (err != REG_NOERROR, 0))
	    return err;
	}
      cur_node = dfa->edests[cur_node].elems[0];
    }
  return REG_NOERROR;
}


/* For all the back references in the current state, calculate the
   destination of the back references by the appropriate entry
   in MCTX->BKREF_ENTS.  */

static reg_errcode_t
internal_function __attribute_warn_unused_result__
expand_bkref_cache (re_match_context_t *mctx, re_node_set *cur_nodes,
		    int cur_str, int subexp_num, int type)
{
  const re_dfa_t *const dfa = mctx->dfa;
  reg_errcode_t err;
  int cache_idx_start = search_cur_bkref_entry (mctx, cur_str);
  struct re_backref_cache_entry *ent;

  if (cache_idx_start == -1)
    return REG_NOERROR;

 restart:
  ent = mctx->bkref_ents + cache_idx_start;
  do
    {
      int to_idx, next_node;

      /* Is this entry ENT is appropriate?  */
      if (!re_node_set_contains (cur_nodes, ent->node))
	continue; /* No.  */

      to_idx = cur_str + ent->subexp_to - ent->subexp_from;
      /* Calculate the destination of the back reference, and append it
	 to MCTX->STATE_LOG.  */
      if (to_idx == cur_str)
	{
	  /* The backreference did epsilon transit, we must re-check all the
	     node in the current state.  */
	  re_node_set new_dests;
	  reg_errcode_t err2, err3;
	  next_node = dfa->edests[ent->node].elems[0];
	  if (re_node_set_contains (cur_nodes, next_node))
	    continue;
	  err = re_node_set_init_1 (&new_dests, next_node);
	  err2 = check_arrival_expand_ecl (dfa, &new_dests, subexp_num, type);
	  err3 = re_node_set_merge (cur_nodes, &new_dests);
	  re_node_set_free (&new_dests);
	  if (BE (err != REG_NOERROR || err2 != REG_NOERROR
		  || err3 != REG_NOERROR, 0))
	    {
	      err = (err != REG_NOERROR ? err
		     : (err2 != REG_NOERROR ? err2 : err3));
	      return err;
	    }
	  /* TODO: It is still inefficient...  */
	  goto restart;
	}
      else
	{
	  re_node_set union_set;
	  next_node = dfa->nexts[ent->node];
	  if (mctx->state_log[to_idx])
	    {
	      int ret;
	      if (re_node_set_contains (&mctx->state_log[to_idx]->nodes,
					next_node))
		continue;
	      err = re_node_set_init_copy (&union_set,
					   &mctx->state_log[to_idx]->nodes);
	      ret = re_node_set_insert (&union_set, next_node);
	      if (BE (err != REG_NOERROR || ret < 0, 0))
		{
		  re_node_set_free (&union_set);
		  err = err != REG_NOERROR ? err : REG_ESPACE;
		  return err;
		}
	    }
	  else
	    {
	      err = re_node_set_init_1 (&union_set, next_node);
	      if (BE (err != REG_NOERROR, 0))
		return err;
	    }
	  mctx->state_log[to_idx] = re_acquire_state (&err, dfa, &union_set);
	  re_node_set_free (&union_set);
	  if (BE (mctx->state_log[to_idx] == NULL
		  && err != REG_NOERROR, 0))
	    return err;
	}
    }
  while (ent++->more);
  return REG_NOERROR;
}

/* Build transition table for the state.
   Return 1 if succeeded, otherwise return NULL.  */

static int
internal_function
build_trtable (const re_dfa_t *dfa, re_dfastate_t *state)
{
  reg_errcode_t err;
  int i, j, ch, need_word_trtable = 0;
  bitset_word_t elem, mask;
  bool dests_node_malloced = false;
  bool dest_states_malloced = false;
  int ndests; /* Number of the destination states from `state'.  */
  re_dfastate_t **trtable;
  re_dfastate_t **dest_states = NULL, **dest_states_word, **dest_states_nl;
  re_node_set follows, *dests_node;
  bitset_t *dests_ch;
  bitset_t acceptable;

  struct dests_alloc
  {
    re_node_set dests_node[SBC_MAX];
    bitset_t dests_ch[SBC_MAX];
  } *dests_alloc;

  /* We build DFA states which corresponds to the destination nodes
     from `state'.  `dests_node[i]' represents the nodes which i-th
     destination state contains, and `dests_ch[i]' represents the
     characters which i-th destination state accepts.  */
  if (__libc_use_alloca (sizeof (struct dests_alloc)))
    dests_alloc = (struct dests_alloc *) alloca (sizeof (struct dests_alloc));
  else
    {
      dests_alloc = re_malloc (struct dests_alloc, 1);
      if (BE (dests_alloc == NULL, 0))
	return 0;
      dests_node_malloced = true;
    }
  dests_node = dests_alloc->dests_node;
  dests_ch = dests_alloc->dests_ch;

  /* Initialize transiton table.  */
  state->word_trtable = state->trtable = NULL;

  /* At first, group all nodes belonging to `state' into several
     destinations.  */
  ndests = group_nodes_into_DFAstates (dfa, state, dests_node, dests_ch);
  if (BE (ndests <= 0, 0))
    {
      if (dests_node_malloced)
	free (dests_alloc);
      /* Return 0 in case of an error, 1 otherwise.  */
      if (ndests == 0)
	{
	  state->trtable = (re_dfastate_t **)
	    calloc (sizeof (re_dfastate_t *), SBC_MAX);
	  if (BE (state->trtable == NULL, 0))
	    return 0;
	  return 1;
	}
      return 0;
    }

  err = re_node_set_alloc (&follows, ndests + 1);
  if (BE (err != REG_NOERROR, 0))
    goto out_free;

  /* Avoid arithmetic overflow in size calculation.  */
  if (BE ((((SIZE_MAX - (sizeof (re_node_set) + sizeof (bitset_t)) * SBC_MAX)
	    / (3 * sizeof (re_dfastate_t *)))
	   < ndests),
	  0))
    goto out_free;

  if (__libc_use_alloca ((sizeof (re_node_set) + sizeof (bitset_t)) * SBC_MAX
			 + ndests * 3 * sizeof (re_dfastate_t *)))
    dest_states = (re_dfastate_t **)
      alloca (ndests * 3 * sizeof (re_dfastate_t *));
  else
    {
      dest_states = (re_dfastate_t **)
	malloc (ndests * 3 * sizeof (re_dfastate_t *));
      if (BE (dest_states == NULL, 0))
	{
out_free:
	  if (dest_states_malloced)
	    free (dest_states);
	  re_node_set_free (&follows);
	  for (i = 0; i < ndests; ++i)
	    re_node_set_free (dests_node + i);
	  if (dests_node_malloced)
	    free (dests_alloc);
	  return 0;
	}
      dest_states_malloced = true;
    }
  dest_states_word = dest_states + ndests;
  dest_states_nl = dest_states_word + ndests;
  bitset_empty (acceptable);

  /* Then build the states for all destinations.  */
  for (i = 0; i < ndests; ++i)
    {
      int next_node;
      re_node_set_empty (&follows);
      /* Merge the follows of this destination states.  */
      for (j = 0; j < dests_node[i].nelem; ++j)
	{
	  next_node = dfa->nexts[dests_node[i].elems[j]];
	  if (next_node != -1)
	    {
	      err = re_node_set_merge (&follows, dfa->eclosures + next_node);
	      if (BE (err != REG_NOERROR, 0))
		goto out_free;
	    }
	}
      dest_states[i] = re_acquire_state_context (&err, dfa, &follows, 0);
      if (BE (dest_states[i] == NULL && err != REG_NOERROR, 0))
	goto out_free;
      /* If the new state has context constraint,
	 build appropriate states for these contexts.  */
      if (dest_states[i]->has_constraint)
	{
	  dest_states_word[i] = re_acquire_state_context (&err, dfa, &follows,
							  CONTEXT_WORD);
	  if (BE (dest_states_word[i] == NULL && err != REG_NOERROR, 0))
	    goto out_free;

	  if (dest_states[i] != dest_states_word[i] && dfa->mb_cur_max > 1)
	    need_word_trtable = 1;

	  dest_states_nl[i] = re_acquire_state_context (&err, dfa, &follows,
							CONTEXT_NEWLINE);
	  if (BE (dest_states_nl[i] == NULL && err != REG_NOERROR, 0))
	    goto out_free;
 	}
      else
	{
	  dest_states_word[i] = dest_states[i];
	  dest_states_nl[i] = dest_states[i];
	}
      bitset_merge (acceptable, dests_ch[i]);
    }

  if (!BE (need_word_trtable, 0))
    {
      /* We don't care about whether the following character is a word
	 character, or we are in a single-byte character set so we can
	 discern by looking at the character code: allocate a
	 256-entry transition table.  */
      trtable = state->trtable =
	(re_dfastate_t **) calloc (sizeof (re_dfastate_t *), SBC_MAX);
      if (BE (trtable == NULL, 0))
	goto out_free;

      /* For all characters ch...:  */
      for (i = 0; i < BITSET_WORDS; ++i)
	for (ch = i * BITSET_WORD_BITS, elem = acceptable[i], mask = 1;
	     elem;
	     mask <<= 1, elem >>= 1, ++ch)
	  if (BE (elem & 1, 0))
	    {
	      /* There must be exactly one destination which accepts
		 character ch.  See group_nodes_into_DFAstates.  */
	      for (j = 0; (dests_ch[j][i] & mask) == 0; ++j)
		;

	      /* j-th destination accepts the word character ch.  */
	      if (dfa->word_char[i] & mask)
		trtable[ch] = dest_states_word[j];
	      else
		trtable[ch] = dest_states[j];
	    }
    }
  else
    {
      /* We care about whether the following character is a word
	 character, and we are in a multi-byte character set: discern
	 by looking at the character code: build two 256-entry
	 transition tables, one starting at trtable[0] and one
	 starting at trtable[SBC_MAX].  */
      trtable = state->word_trtable =
	(re_dfastate_t **) calloc (sizeof (re_dfastate_t *), 2 * SBC_MAX);
      if (BE (trtable == NULL, 0))
	goto out_free;

      /* For all characters ch...:  */
      for (i = 0; i < BITSET_WORDS; ++i)
	for (ch = i * BITSET_WORD_BITS, elem = acceptable[i], mask = 1;
	     elem;
	     mask <<= 1, elem >>= 1, ++ch)
	  if (BE (elem & 1, 0))
	    {
	      /* There must be exactly one destination which accepts
		 character ch.  See group_nodes_into_DFAstates.  */
	      for (j = 0; (dests_ch[j][i] & mask) == 0; ++j)
		;

	      /* j-th destination accepts the word character ch.  */
	      trtable[ch] = dest_states[j];
	      trtable[ch + SBC_MAX] = dest_states_word[j];
	    }
    }

  /* new line */
  if (bitset_contain (acceptable, NEWLINE_CHAR))
    {
      /* The current state accepts newline character.  */
      for (j = 0; j < ndests; ++j)
	if (bitset_contain (dests_ch[j], NEWLINE_CHAR))
	  {
	    /* k-th destination accepts newline character.  */
	    trtable[NEWLINE_CHAR] = dest_states_nl[j];
	    if (need_word_trtable)
	      trtable[NEWLINE_CHAR + SBC_MAX] = dest_states_nl[j];
	    /* There must be only one destination which accepts
	       newline.  See group_nodes_into_DFAstates.  */
	    break;
	  }
    }

  if (dest_states_malloced)
    free (dest_states);

  re_node_set_free (&follows);
  for (i = 0; i < ndests; ++i)
    re_node_set_free (dests_node + i);

  if (dests_node_malloced)
    free (dests_alloc);

  return 1;
}

/* Group all nodes belonging to STATE into several destinations.
   Then for all destinations, set the nodes belonging to the destination
   to DESTS_NODE[i] and set the characters accepted by the destination
   to DEST_CH[i].  This function return the number of destinations.  */

static int
internal_function
group_nodes_into_DFAstates (const re_dfa_t *dfa, const re_dfastate_t *state,
			    re_node_set *dests_node, bitset_t *dests_ch)
{
  reg_errcode_t err;
  int result;
  int i, j, k;
  int ndests; /* Number of the destinations from `state'.  */
  bitset_t accepts; /* Characters a node can accept.  */
  const re_node_set *cur_nodes = &state->nodes;
  bitset_empty (accepts);
  ndests = 0;

  /* For all the nodes belonging to `state',  */
  for (i = 0; i < cur_nodes->nelem; ++i)
    {
      re_token_t *node = &dfa->nodes[cur_nodes->elems[i]];
      re_token_type_t type = node->type;
      unsigned int constraint = node->constraint;

      /* Enumerate all single byte character this node can accept.  */
      if (type == CHARACTER)
	bitset_set (accepts, node->opr.c);
      else if (type == SIMPLE_BRACKET)
	{
	  bitset_merge (accepts, node->opr.sbcset);
	}
      else if (type == OP_PERIOD)
	{
#ifdef RE_ENABLE_I18N
	  if (dfa->mb_cur_max > 1)
	    bitset_merge (accepts, dfa->sb_char);
	  else
#endif
	    bitset_set_all (accepts);
	  if (!(dfa->syntax & RE_DOT_NEWLINE))
	    bitset_clear (accepts, '\n');
	  if (dfa->syntax & RE_DOT_NOT_NULL)
	    bitset_clear (accepts, '\0');
	}
#ifdef RE_ENABLE_I18N
      else if (type == OP_UTF8_PERIOD)
	{
	  memset (accepts, '\xff', sizeof (bitset_t) / 2);
	  if (!(dfa->syntax & RE_DOT_NEWLINE))
	    bitset_clear (accepts, '\n');
	  if (dfa->syntax & RE_DOT_NOT_NULL)
	    bitset_clear (accepts, '\0');
	}
#endif
      else
	continue;

      /* Check the `accepts' and sift the characters which are not
	 match it the context.  */
      if (constraint)
	{
	  if (constraint & NEXT_NEWLINE_CONSTRAINT)
	    {
	      bool accepts_newline = bitset_contain (accepts, NEWLINE_CHAR);
	      bitset_empty (accepts);
	      if (accepts_newline)
		bitset_set (accepts, NEWLINE_CHAR);
	      else
		continue;
	    }
	  if (constraint & NEXT_ENDBUF_CONSTRAINT)
	    {
	      bitset_empty (accepts);
	      continue;
	    }

	  if (constraint & NEXT_WORD_CONSTRAINT)
	    {
	      bitset_word_t any_set = 0;
	      if (type == CHARACTER && !node->word_char)
		{
		  bitset_empty (accepts);
		  continue;
		}
#ifdef RE_ENABLE_I18N
	      if (dfa->mb_cur_max > 1)
		for (j = 0; j < BITSET_WORDS; ++j)
		  any_set |= (accepts[j] &= (dfa->word_char[j] | ~dfa->sb_char[j]));
	      else
#endif
		for (j = 0; j < BITSET_WORDS; ++j)
		  any_set |= (accepts[j] &= dfa->word_char[j]);
	      if (!any_set)
		continue;
	    }
	  if (constraint & NEXT_NOTWORD_CONSTRAINT)
	    {
	      bitset_word_t any_set = 0;
	      if (type == CHARACTER && node->word_char)
		{
		  bitset_empty (accepts);
		  continue;
		}
#ifdef RE_ENABLE_I18N
	      if (dfa->mb_cur_max > 1)
		for (j = 0; j < BITSET_WORDS; ++j)
		  any_set |= (accepts[j] &= ~(dfa->word_char[j] & dfa->sb_char[j]));
	      else
#endif
		for (j = 0; j < BITSET_WORDS; ++j)
		  any_set |= (accepts[j] &= ~dfa->word_char[j]);
	      if (!any_set)
		continue;
	    }
	}

      /* Then divide `accepts' into DFA states, or create a new
	 state.  Above, we make sure that accepts is not empty.  */
      for (j = 0; j < ndests; ++j)
	{
	  bitset_t intersec; /* Intersection sets, see below.  */
	  bitset_t remains;
	  /* Flags, see below.  */
	  bitset_word_t has_intersec, not_subset, not_consumed;

	  /* Optimization, skip if this state doesn't accept the character.  */
	  if (type == CHARACTER && !bitset_contain (dests_ch[j], node->opr.c))
	    continue;

	  /* Enumerate the intersection set of this state and `accepts'.  */
	  has_intersec = 0;
	  for (k = 0; k < BITSET_WORDS; ++k)
	    has_intersec |= intersec[k] = accepts[k] & dests_ch[j][k];
	  /* And skip if the intersection set is empty.  */
	  if (!has_intersec)
	    continue;

	  /* Then check if this state is a subset of `accepts'.  */
	  not_subset = not_consumed = 0;
	  for (k = 0; k < BITSET_WORDS; ++k)
	    {
	      not_subset |= remains[k] = ~accepts[k] & dests_ch[j][k];
	      not_consumed |= accepts[k] = accepts[k] & ~dests_ch[j][k];
	    }

	  /* If this state isn't a subset of `accepts', create a
	     new group state, which has the `remains'. */
	  if (not_subset)
	    {
	      bitset_copy (dests_ch[ndests], remains);
	      bitset_copy (dests_ch[j], intersec);
	      err = re_node_set_init_copy (dests_node + ndests, &dests_node[j]);
	      if (BE (err != REG_NOERROR, 0))
		goto error_return;
	      ++ndests;
	    }

	  /* Put the position in the current group. */
	  result = re_node_set_insert (&dests_node[j], cur_nodes->elems[i]);
	  if (BE (result < 0, 0))
	    goto error_return;

	  /* If all characters are consumed, go to next node. */
	  if (!not_consumed)
	    break;
	}
      /* Some characters remain, create a new group. */
      if (j == ndests)
	{
	  bitset_copy (dests_ch[ndests], accepts);
	  err = re_node_set_init_1 (dests_node + ndests, cur_nodes->elems[i]);
	  if (BE (err != REG_NOERROR, 0))
	    goto error_return;
	  ++ndests;
	  bitset_empty (accepts);
	}
    }
  return ndests;
 error_return:
  for (j = 0; j < ndests; ++j)
    re_node_set_free (dests_node + j);
  return -1;
}

#ifdef RE_ENABLE_I18N
/* Check how many bytes the node `dfa->nodes[node_idx]' accepts.
   Return the number of the bytes the node accepts.
   STR_IDX is the current index of the input string.

   This function handles the nodes which can accept one character, or
   one collating element like '.', '[a-z]', opposite to the other nodes
   can only accept one byte.  */

static int
internal_function
check_node_accept_bytes (const re_dfa_t *dfa, int node_idx,
			 const re_string_t *input, int str_idx)
{
  const re_token_t *node = dfa->nodes + node_idx;
  int char_len, elem_len;
  int i;

  if (BE (node->type == OP_UTF8_PERIOD, 0))
    {
      unsigned char c = re_string_byte_at (input, str_idx), d;
      if (BE (c < 0xc2, 1))
	return 0;

      if (str_idx + 2 > input->len)
	return 0;

      d = re_string_byte_at (input, str_idx + 1);
      if (c < 0xe0)
	return (d < 0x80 || d > 0xbf) ? 0 : 2;
      else if (c < 0xf0)
	{
	  char_len = 3;
	  if (c == 0xe0 && d < 0xa0)
	    return 0;
	}
      else if (c < 0xf8)
	{
	  char_len = 4;
	  if (c == 0xf0 && d < 0x90)
	    return 0;
	}
      else if (c < 0xfc)
	{
	  char_len = 5;
	  if (c == 0xf8 && d < 0x88)
	    return 0;
	}
      else if (c < 0xfe)
	{
	  char_len = 6;
	  if (c == 0xfc && d < 0x84)
	    return 0;
	}
      else
	return 0;

      if (str_idx + char_len > input->len)
	return 0;

      for (i = 1; i < char_len; ++i)
	{
	  d = re_string_byte_at (input, str_idx + i);
	  if (d < 0x80 || d > 0xbf)
	    return 0;
	}
      return char_len;
    }

  char_len = re_string_char_size_at (input, str_idx);
  if (node->type == OP_PERIOD)
    {
      if (char_len <= 1)
	return 0;
      /* FIXME: I don't think this if is needed, as both '\n'
	 and '\0' are char_len == 1.  */
      /* '.' accepts any one character except the following two cases.  */
      if ((!(dfa->syntax & RE_DOT_NEWLINE) &&
	   re_string_byte_at (input, str_idx) == '\n') ||
	  ((dfa->syntax & RE_DOT_NOT_NULL) &&
	   re_string_byte_at (input, str_idx) == '\0'))
	return 0;
      return char_len;
    }

  elem_len = re_string_elem_size_at (input, str_idx);
  if ((elem_len <= 1 && char_len <= 1) || char_len == 0)
    return 0;

  if (node->type == COMPLEX_BRACKET)
    {
      const re_charset_t *cset = node->opr.mbcset;
# ifdef _LIBC
      const unsigned char *pin
	= ((const unsigned char *) re_string_get_buffer (input) + str_idx);
      int j;
      uint32_t nrules;
# endif /* _LIBC */
      int match_len = 0;
      wchar_t wc = ((cset->nranges || cset->nchar_classes || cset->nmbchars)
		    ? re_string_wchar_at (input, str_idx) : 0);

      /* match with multibyte character?  */
      for (i = 0; i < cset->nmbchars; ++i)
	if (wc == cset->mbchars[i])
	  {
	    match_len = char_len;
	    goto check_node_accept_bytes_match;
	  }
      /* match with character_class?  */
      for (i = 0; i < cset->nchar_classes; ++i)
	{
	  wctype_t wt = cset->char_classes[i];
	  if (__iswctype (wc, wt))
	    {
	      match_len = char_len;
	      goto check_node_accept_bytes_match;
	    }
	}

# ifdef _LIBC
      nrules = _NL_CURRENT_WORD (LC_COLLATE, _NL_COLLATE_NRULES);
      if (nrules != 0)
	{
	  unsigned int in_collseq = 0;
	  const int32_t *table, *indirect;
	  const unsigned char *weights, *extra;
	  const char *collseqwc;
	  /* This #include defines a local function!  */
#  include <locale/weight.h>

	  /* match with collating_symbol?  */
	  if (cset->ncoll_syms)
	    extra = (const unsigned char *)
	      _NL_CURRENT (LC_COLLATE, _NL_COLLATE_SYMB_EXTRAMB);
	  for (i = 0; i < cset->ncoll_syms; ++i)
	    {
	      const unsigned char *coll_sym = extra + cset->coll_syms[i];
	      /* Compare the length of input collating element and
		 the length of current collating element.  */
	      if (*coll_sym != elem_len)
		continue;
	      /* Compare each bytes.  */
	      for (j = 0; j < *coll_sym; j++)
		if (pin[j] != coll_sym[1 + j])
		  break;
	      if (j == *coll_sym)
		{
		  /* Match if every bytes is equal.  */
		  match_len = j;
		  goto check_node_accept_bytes_match;
		}
	    }

	  if (cset->nranges)
	    {
	      if (elem_len <= char_len)
		{
		  collseqwc = _NL_CURRENT (LC_COLLATE, _NL_COLLATE_COLLSEQWC);
		  in_collseq = __collseq_table_lookup (collseqwc, wc);
		}
	      else
		in_collseq = find_collation_sequence_value (pin, elem_len);
	    }
	  /* match with range expression?  */
	  for (i = 0; i < cset->nranges; ++i)
	    if (cset->range_starts[i] <= in_collseq
		&& in_collseq <= cset->range_ends[i])
	      {
		match_len = elem_len;
		goto check_node_accept_bytes_match;
	      }

	  /* match with equivalence_class?  */
	  if (cset->nequiv_classes)
	    {
	      const unsigned char *cp = pin;
	      table = (const int32_t *)
		_NL_CURRENT (LC_COLLATE, _NL_COLLATE_TABLEMB);
	      weights = (const unsigned char *)
		_NL_CURRENT (LC_COLLATE, _NL_COLLATE_WEIGHTMB);
	      extra = (const unsigned char *)
		_NL_CURRENT (LC_COLLATE, _NL_COLLATE_EXTRAMB);
	      indirect = (const int32_t *)
		_NL_CURRENT (LC_COLLATE, _NL_COLLATE_INDIRECTMB);
	      int32_t idx = findidx (&cp, elem_len);
	      if (idx > 0)
		for (i = 0; i < cset->nequiv_classes; ++i)
		  {
		    int32_t equiv_class_idx = cset->equiv_classes[i];
		    size_t weight_len = weights[idx & 0xffffff];
		    if (weight_len == weights[equiv_class_idx & 0xffffff]
			&& (idx >> 24) == (equiv_class_idx >> 24))
		      {
			int cnt = 0;

			idx &= 0xffffff;
			equiv_class_idx &= 0xffffff;

			while (cnt <= weight_len
			       && (weights[equiv_class_idx + 1 + cnt]
				   == weights[idx + 1 + cnt]))
			  ++cnt;
			if (cnt > weight_len)
			  {
			    match_len = elem_len;
			    goto check_node_accept_bytes_match;
			  }
		      }
		  }
	    }
	}
      else
# endif /* _LIBC */
	{
	  /* match with range expression?  */
#if __GNUC__ >= 2
	  wchar_t cmp_buf[] = {L'\0', L'\0', wc, L'\0', L'\0', L'\0'};
#else
	  wchar_t cmp_buf[] = {L'\0', L'\0', L'\0', L'\0', L'\0', L'\0'};
	  cmp_buf[2] = wc;
#endif
	  for (i = 0; i < cset->nranges; ++i)
	    {
	      cmp_buf[0] = cset->range_starts[i];
	      cmp_buf[4] = cset->range_ends[i];
	      if (wcscoll (cmp_buf, cmp_buf + 2) <= 0
		  && wcscoll (cmp_buf + 2, cmp_buf + 4) <= 0)
		{
		  match_len = char_len;
		  goto check_node_accept_bytes_match;
		}
	    }
	}
    check_node_accept_bytes_match:
      if (!cset->non_match)
	return match_len;
      else
	{
	  if (match_len > 0)
	    return 0;
	  else
	    return (elem_len > char_len) ? elem_len : char_len;
	}
    }
  return 0;
}

# ifdef _LIBC
static unsigned int
internal_function
find_collation_sequence_value (const unsigned char *mbs, size_t mbs_len)
{
  uint32_t nrules = _NL_CURRENT_WORD (LC_COLLATE, _NL_COLLATE_NRULES);
  if (nrules == 0)
    {
      if (mbs_len == 1)
	{
	  /* No valid character.  Match it as a single byte character.  */
	  const unsigned char *collseq = (const unsigned char *)
	    _NL_CURRENT (LC_COLLATE, _NL_COLLATE_COLLSEQMB);
	  return collseq[mbs[0]];
	}
      return UINT_MAX;
    }
  else
    {
      int32_t idx;
      const unsigned char *extra = (const unsigned char *)
	_NL_CURRENT (LC_COLLATE, _NL_COLLATE_SYMB_EXTRAMB);
      int32_t extrasize = (const unsigned char *)
	_NL_CURRENT (LC_COLLATE, _NL_COLLATE_SYMB_EXTRAMB + 1) - extra;

      for (idx = 0; idx < extrasize;)
	{
	  int mbs_cnt, found = 0;
	  int32_t elem_mbs_len;
	  /* Skip the name of collating element name.  */
	  idx = idx + extra[idx] + 1;
	  elem_mbs_len = extra[idx++];
	  if (mbs_len == elem_mbs_len)
	    {
	      for (mbs_cnt = 0; mbs_cnt < elem_mbs_len; ++mbs_cnt)
		if (extra[idx + mbs_cnt] != mbs[mbs_cnt])
		  break;
	      if (mbs_cnt == elem_mbs_len)
		/* Found the entry.  */
		found = 1;
	    }
	  /* Skip the byte sequence of the collating element.  */
	  idx += elem_mbs_len;
	  /* Adjust for the alignment.  */
	  idx = (idx + 3) & ~3;
	  /* Skip the collation sequence value.  */
	  idx += sizeof (uint32_t);
	  /* Skip the wide char sequence of the collating element.  */
	  idx = idx + sizeof (uint32_t) * (*(int32_t *) (extra + idx) + 1);
	  /* If we found the entry, return the sequence value.  */
	  if (found)
	    return *(uint32_t *) (extra + idx);
	  /* Skip the collation sequence value.  */
	  idx += sizeof (uint32_t);
	}
      return UINT_MAX;
    }
}
# endif /* _LIBC */
#endif /* RE_ENABLE_I18N */

/* Check whether the node accepts the byte which is IDX-th
   byte of the INPUT.  */

static int
internal_function
check_node_accept (const re_match_context_t *mctx, const re_token_t *node,
		   int idx)
{
  unsigned char ch;
  ch = re_string_byte_at (&mctx->input, idx);
  switch (node->type)
    {
    case CHARACTER:
      if (node->opr.c != ch)
	return 0;
      break;

    case SIMPLE_BRACKET:
      if (!bitset_contain (node->opr.sbcset, ch))
	return 0;
      break;

#ifdef RE_ENABLE_I18N
    case OP_UTF8_PERIOD:
      if (ch >= 0x80)
	return 0;
      /* FALLTHROUGH */
#endif
    case OP_PERIOD:
      if ((ch == '\n' && !(mctx->dfa->syntax & RE_DOT_NEWLINE))
	  || (ch == '\0' && (mctx->dfa->syntax & RE_DOT_NOT_NULL)))
	return 0;
      break;

    default:
      return 0;
    }

  if (node->constraint)
    {
      /* The node has constraints.  Check whether the current context
	 satisfies the constraints.  */
      unsigned int context = re_string_context_at (&mctx->input, idx,
						   mctx->eflags);
      if (NOT_SATISFY_NEXT_CONSTRAINT (node->constraint, context))
	return 0;
    }

  return 1;
}

/* Extend the buffers, if the buffers have run out.  */

static reg_errcode_t
internal_function __attribute_warn_unused_result__
extend_buffers (re_match_context_t *mctx, int min_len)
{
  reg_errcode_t ret;
  re_string_t *pstr = &mctx->input;

  /* Avoid overflow.  */
  if (BE (INT_MAX / 2 / sizeof (re_dfastate_t *) <= pstr->bufs_len, 0))
    return REG_ESPACE;

  /* Double the lengthes of the buffers, but allocate at least MIN_LEN.  */
  ret = re_string_realloc_buffers (pstr,
				   MAX (min_len,
					MIN (pstr->len, pstr->bufs_len * 2)));
  if (BE (ret != REG_NOERROR, 0))
    return ret;

  if (mctx->state_log != NULL)
    {
      /* And double the length of state_log.  */
      /* XXX We have no indication of the size of this buffer.  If this
	 allocation fail we have no indication that the state_log array
	 does not have the right size.  */
      re_dfastate_t **new_array = re_realloc (mctx->state_log, re_dfastate_t *,
					      pstr->bufs_len + 1);
      if (BE (new_array == NULL, 0))
	return REG_ESPACE;
      mctx->state_log = new_array;
    }

  /* Then reconstruct the buffers.  */
  if (pstr->icase)
    {
#ifdef RE_ENABLE_I18N
      if (pstr->mb_cur_max > 1)
	{
	  ret = build_wcs_upper_buffer (pstr);
	  if (BE (ret != REG_NOERROR, 0))
	    return ret;
	}
      else
#endif /* RE_ENABLE_I18N  */
	build_upper_buffer (pstr);
    }
  else
    {
#ifdef RE_ENABLE_I18N
      if (pstr->mb_cur_max > 1)
	build_wcs_buffer (pstr);
      else
#endif /* RE_ENABLE_I18N  */
	{
	  if (pstr->trans != NULL)
	    re_string_translate_buffer (pstr);
	}
    }
  return REG_NOERROR;
}


/* Functions for matching context.  */

/* Initialize MCTX.  */

static reg_errcode_t
internal_function __attribute_warn_unused_result__
match_ctx_init (re_match_context_t *mctx, int eflags, int n)
{
  mctx->eflags = eflags;
  mctx->match_last = -1;
  if (n > 0)
    {
      mctx->bkref_ents = re_malloc (struct re_backref_cache_entry, n);
      mctx->sub_tops = re_malloc (re_sub_match_top_t *, n);
      if (BE (mctx->bkref_ents == NULL || mctx->sub_tops == NULL, 0))
	return REG_ESPACE;
    }
  /* Already zero-ed by the caller.
     else
       mctx->bkref_ents = NULL;
     mctx->nbkref_ents = 0;
     mctx->nsub_tops = 0;  */
  mctx->abkref_ents = n;
  mctx->max_mb_elem_len = 1;
  mctx->asub_tops = n;
  return REG_NOERROR;
}

/* Clean the entries which depend on the current input in MCTX.
   This function must be invoked when the matcher changes the start index
   of the input, or changes the input string.  */

static void
internal_function
match_ctx_clean (re_match_context_t *mctx)
{
  int st_idx;
  for (st_idx = 0; st_idx < mctx->nsub_tops; ++st_idx)
    {
      int sl_idx;
      re_sub_match_top_t *top = mctx->sub_tops[st_idx];
      for (sl_idx = 0; sl_idx < top->nlasts; ++sl_idx)
	{
	  re_sub_match_last_t *last = top->lasts[sl_idx];
	  re_free (last->path.array);
	  re_free (last);
	}
      re_free (top->lasts);
      if (top->path)
	{
	  re_free (top->path->array);
	  re_free (top->path);
	}
      free (top);
    }

  mctx->nsub_tops = 0;
  mctx->nbkref_ents = 0;
}

/* Free all the memory associated with MCTX.  */

static void
internal_function
match_ctx_free (re_match_context_t *mctx)
{
  /* First, free all the memory associated with MCTX->SUB_TOPS.  */
  match_ctx_clean (mctx);
  re_free (mctx->sub_tops);
  re_free (mctx->bkref_ents);
}

/* Add a new backreference entry to MCTX.
   Note that we assume that caller never call this function with duplicate
   entry, and call with STR_IDX which isn't smaller than any existing entry.
*/

static reg_errcode_t
internal_function __attribute_warn_unused_result__
match_ctx_add_entry (re_match_context_t *mctx, int node, int str_idx, int from,
		     int to)
{
  if (mctx->nbkref_ents >= mctx->abkref_ents)
    {
      struct re_backref_cache_entry* new_entry;
      new_entry = re_realloc (mctx->bkref_ents, struct re_backref_cache_entry,
			      mctx->abkref_ents * 2);
      if (BE (new_entry == NULL, 0))
	{
	  re_free (mctx->bkref_ents);
	  return REG_ESPACE;
	}
      mctx->bkref_ents = new_entry;
      memset (mctx->bkref_ents + mctx->nbkref_ents, '\0',
	      sizeof (struct re_backref_cache_entry) * mctx->abkref_ents);
      mctx->abkref_ents *= 2;
    }
  if (mctx->nbkref_ents > 0
      && mctx->bkref_ents[mctx->nbkref_ents - 1].str_idx == str_idx)
    mctx->bkref_ents[mctx->nbkref_ents - 1].more = 1;

  mctx->bkref_ents[mctx->nbkref_ents].node = node;
  mctx->bkref_ents[mctx->nbkref_ents].str_idx = str_idx;
  mctx->bkref_ents[mctx->nbkref_ents].subexp_from = from;
  mctx->bkref_ents[mctx->nbkref_ents].subexp_to = to;

  /* This is a cache that saves negative results of check_dst_limits_calc_pos.
     If bit N is clear, means that this entry won't epsilon-transition to
     an OP_OPEN_SUBEXP or OP_CLOSE_SUBEXP for the N+1-th subexpression.  If
     it is set, check_dst_limits_calc_pos_1 will recurse and try to find one
     such node.

     A backreference does not epsilon-transition unless it is empty, so set
     to all zeros if FROM != TO.  */
  mctx->bkref_ents[mctx->nbkref_ents].eps_reachable_subexps_map
    = (from == to ? ~0 : 0);

  mctx->bkref_ents[mctx->nbkref_ents++].more = 0;
  if (mctx->max_mb_elem_len < to - from)
    mctx->max_mb_elem_len = to - from;
  return REG_NOERROR;
}

/* Search for the first entry which has the same str_idx, or -1 if none is
   found.  Note that MCTX->BKREF_ENTS is already sorted by MCTX->STR_IDX.  */

static int
internal_function
search_cur_bkref_entry (const re_match_context_t *mctx, int str_idx)
{
  int left, right, mid, last;
  last = right = mctx->nbkref_ents;
  for (left = 0; left < right;)
    {
      mid = (left + right) / 2;
      if (mctx->bkref_ents[mid].str_idx < str_idx)
	left = mid + 1;
      else
	right = mid;
    }
  if (left < last && mctx->bkref_ents[left].str_idx == str_idx)
    return left;
  else
    return -1;
}

/* Register the node NODE, whose type is OP_OPEN_SUBEXP, and which matches
   at STR_IDX.  */

static reg_errcode_t
internal_function __attribute_warn_unused_result__
match_ctx_add_subtop (re_match_context_t *mctx, int node, int str_idx)
{
#ifdef DEBUG
  assert (mctx->sub_tops != NULL);
  assert (mctx->asub_tops > 0);
#endif
  if (BE (mctx->nsub_tops == mctx->asub_tops, 0))
    {
      int new_asub_tops = mctx->asub_tops * 2;
      re_sub_match_top_t **new_array = re_realloc (mctx->sub_tops,
						   re_sub_match_top_t *,
						   new_asub_tops);
      if (BE (new_array == NULL, 0))
	return REG_ESPACE;
      mctx->sub_tops = new_array;
      mctx->asub_tops = new_asub_tops;
    }
  mctx->sub_tops[mctx->nsub_tops] = calloc (1, sizeof (re_sub_match_top_t));
  if (BE (mctx->sub_tops[mctx->nsub_tops] == NULL, 0))
    return REG_ESPACE;
  mctx->sub_tops[mctx->nsub_tops]->node = node;
  mctx->sub_tops[mctx->nsub_tops++]->str_idx = str_idx;
  return REG_NOERROR;
}

/* Register the node NODE, whose type is OP_CLOSE_SUBEXP, and which matches
   at STR_IDX, whose corresponding OP_OPEN_SUBEXP is SUB_TOP.  */

static re_sub_match_last_t *
internal_function
match_ctx_add_sublast (re_sub_match_top_t *subtop, int node, int str_idx)
{
  re_sub_match_last_t *new_entry;
  if (BE (subtop->nlasts == subtop->alasts, 0))
    {
      int new_alasts = 2 * subtop->alasts + 1;
      re_sub_match_last_t **new_array = re_realloc (subtop->lasts,
						    re_sub_match_last_t *,
						    new_alasts);
      if (BE (new_array == NULL, 0))
	return NULL;
      subtop->lasts = new_array;
      subtop->alasts = new_alasts;
    }
  new_entry = calloc (1, sizeof (re_sub_match_last_t));
  if (BE (new_entry != NULL, 1))
    {
      subtop->lasts[subtop->nlasts] = new_entry;
      new_entry->node = node;
      new_entry->str_idx = str_idx;
      ++subtop->nlasts;
    }
  return new_entry;
}

static void
internal_function
sift_ctx_init (re_sift_context_t *sctx, re_dfastate_t **sifted_sts,
	       re_dfastate_t **limited_sts, int last_node, int last_str_idx)
{
  sctx->sifted_states = sifted_sts;
  sctx->limited_states = limited_sts;
  sctx->last_node = last_node;
  sctx->last_str_idx = last_str_idx;
  re_node_set_init_empty (&sctx->limits);
}
