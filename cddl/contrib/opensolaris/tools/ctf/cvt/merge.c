/*
 * CDDL HEADER START
 *
 * The contents of this file are subject to the terms of the
 * Common Development and Distribution License (the "License").
 * You may not use this file except in compliance with the License.
 *
 * You can obtain a copy of the license at usr/src/OPENSOLARIS.LICENSE
 * or http://www.opensolaris.org/os/licensing.
 * See the License for the specific language governing permissions
 * and limitations under the License.
 *
 * When distributing Covered Code, include this CDDL HEADER in each
 * file and include the License file at usr/src/OPENSOLARIS.LICENSE.
 * If applicable, add the following below this CDDL HEADER, with the
 * fields enclosed by brackets "[]" replaced with your own identifying
 * information: Portions Copyright [yyyy] [name of copyright owner]
 *
 * CDDL HEADER END
 */
/*
 * Copyright 2006 Sun Microsystems, Inc.  All rights reserved.
 * Use is subject to license terms.
 */

#pragma ident	"%Z%%M%	%I%	%E% SMI"

/*
 * This file contains routines that merge one tdata_t tree, called the child,
 * into another, called the parent.  Note that these names are used mainly for
 * convenience and to represent the direction of the merge.  They are not meant
 * to imply any relationship between the tdata_t graphs prior to the merge.
 *
 * tdata_t structures contain two main elements - a hash of iidesc_t nodes, and
 * a directed graph of tdesc_t nodes, pointed to by the iidesc_t nodes.  Simply
 * put, we merge the tdesc_t graphs, followed by the iidesc_t nodes, and then we
 * clean up loose ends.
 *
 * The algorithm is as follows:
 *
 * 1. Mapping iidesc_t nodes
 *
 * For each child iidesc_t node, we first try to map its tdesc_t subgraph
 * against the tdesc_t graph in the parent.  For each node in the child subgraph
 * that exists in the parent, a mapping between the two (between their type IDs)
 * is established.  For the child nodes that cannot be mapped onto existing
 * parent nodes, a mapping is established between the child node ID and a
 * newly-allocated ID that the node will use when it is re-created in the
 * parent.  These unmappable nodes are added to the md_tdtba (tdesc_t To Be
 * Added) hash, which tracks nodes that need to be created in the parent.
 *
 * If all of the nodes in the subgraph for an iidesc_t in the child can be
 * mapped to existing nodes in the parent, then we can try to map the child
 * iidesc_t onto an iidesc_t in the parent.  If we cannot find an equivalent
 * iidesc_t, or if we were not able to completely map the tdesc_t subgraph(s),
 * then we add this iidesc_t to the md_iitba (iidesc_t To Be Added) list.  This
 * list tracks iidesc_t nodes that are to be created in the parent.
 *
 * While visiting the tdesc_t nodes, we may discover a forward declaration (a
 * FORWARD tdesc_t) in the parent that is resolved in the child.  That is, there
 * may be a structure or union definition in the child with the same name as the
 * forward declaration in the parent.  If we find such a node, we record an
 * association in the md_fdida (Forward => Definition ID Association) list
 * between the parent ID of the forward declaration and the ID that the
 * definition will use when re-created in the parent.
 *
 * 2. Creating new tdesc_t nodes (the md_tdtba hash)
 *
 * We have now attempted to map all tdesc_t nodes from the child into the
 * parent, and have, in md_tdtba, a hash of all tdesc_t nodes that need to be
 * created (or, as we so wittily call it, conjured) in the parent.  We iterate
 * through this hash, creating the indicated tdesc_t nodes.  For a given tdesc_t
 * node, conjuring requires two steps - the copying of the common tdesc_t data
 * (name, type, etc) from the child node, and the creation of links from the
 * newly-created node to the parent equivalents of other tdesc_t nodes pointed
 * to by node being conjured.  Note that in some cases, the targets of these
 * links will be on the md_tdtba hash themselves, and may not have been created
 * yet.  As such, we can't establish the links from these new nodes into the
 * parent graph.  We therefore conjure them with links to nodes in the *child*
 * graph, and add pointers to the links to be created to the md_tdtbr (tdesc_t
 * To Be Remapped) hash.  For example, a POINTER tdesc_t that could not be
 * resolved would have its &tdesc_t->t_tdesc added to md_tdtbr.
 *
 * 3. Creating new iidesc_t nodes (the md_iitba list)
 *
 * When we have completed step 2, all tdesc_t nodes have been created (or
 * already existed) in the parent.  Some of them may have incorrect links (the
 * members of the md_tdtbr list), but they've all been created.  As such, we can
 * create all of the iidesc_t nodes, as we can attach the tdesc_t subgraph
 * pointers correctly.  We create each node, and attach the pointers to the
 * appropriate parts of the parent tdesc_t graph.
 *
 * 4. Resolving newly-created tdesc_t node links (the md_tdtbr list)
 *
 * As in step 3, we rely on the fact that all of the tdesc_t nodes have been
 * created.  Each entry in the md_tdtbr list is a pointer to where a link into
 * the parent will be established.  As saved in the md_tdtbr list, these
 * pointers point into the child tdesc_t subgraph.  We can thus get the target
 * type ID from the child, look at the ID mapping to determine the desired link
 * target, and redirect the link accordingly.
 *
 * 5. Parent => child forward declaration resolution
 *
 * If entries were made in the md_fdida list in step 1, we have forward
 * declarations in the parent that need to be resolved to their definitions
 * re-created in step 2 from the child.  Using the md_fdida list, we can locate
 * the definition for the forward declaration, and we can redirect all inbound
 * edges to the forward declaration node to the actual definition.
 *
 * A pox on the house of anyone who changes the algorithm without updating
 * this comment.
 */

#include <stdio.h>
#include <strings.h>
#include <assert.h>
#include <pthread.h>

#include "ctf_headers.h"
#include "ctftools.h"
#include "list.h"
#include "alist.h"
#include "memory.h"
#include "traverse.h"

typedef struct equiv_data equiv_data_t;
typedef struct merge_cb_data merge_cb_data_t;

/*
 * There are two traversals in this file, for equivalency and for tdesc_t
 * re-creation, that do not fit into the tdtraverse() framework.  We have our
 * own traversal mechanism and ops vector here for those two cases.
 */
typedef struct tdesc_ops {
	const char *name;
	int (*equiv)(tdesc_t *, tdesc_t *, equiv_data_t *);
	tdesc_t *(*conjure)(tdesc_t *, int, merge_cb_data_t *);
} tdesc_ops_t;
extern tdesc_ops_t tdesc_ops[];

/*
 * The workhorse structure of tdata_t merging.  Holds all lists of nodes to be
 * processed during various phases of the merge algorithm.
 */
struct merge_cb_data {
	tdata_t *md_parent;
	tdata_t *md_tgt;
	alist_t *md_ta;		/* Type Association */
	alist_t *md_fdida;	/* Forward -> Definition ID Association */
	list_t	**md_iitba;	/* iidesc_t nodes To Be Added to the parent */
	hash_t	*md_tdtba;	/* tdesc_t nodes To Be Added to the parent */
	list_t	**md_tdtbr;	/* tdesc_t nodes To Be Remapped */
	int md_flags;
}; /* merge_cb_data_t */

/*
 * When we first create a tdata_t from stabs data, we will have duplicate nodes.
 * Normal merges, however, assume that the child tdata_t is already self-unique,
 * and for speed reasons do not attempt to self-uniquify.  If this flag is set,
 * the merge algorithm will self-uniquify by avoiding the insertion of
 * duplicates in the md_tdtdba list.
 */
#define	MCD_F_SELFUNIQUIFY	0x1

/*
 * When we merge the CTF data for the modules, we don't want it to contain any
 * data that can be found in the reference module (usually genunix).  If this
 * flag is set, we're doing a merge between the fully merged tdata_t for this
 * module and the tdata_t for the reference module, with the data unique to this
 * module ending up in a third tdata_t.  It is this third tdata_t that will end
 * up in the .SUNW_ctf section for the module.
 */
#define	MCD_F_REFMERGE	0x2

/*
 * Mapping of child type IDs to parent type IDs
 */

static void
add_mapping(alist_t *ta, tid_t srcid, tid_t tgtid)
{
	debug(3, "Adding mapping %u <%x> => %u <%x>\n", srcid, srcid, tgtid, tgtid);

	assert(!alist_find(ta, (void *)(uintptr_t)srcid, NULL));
	assert(srcid != 0 && tgtid != 0);

	alist_add(ta, (void *)(uintptr_t)srcid, (void *)(uintptr_t)tgtid);
}

static tid_t
get_mapping(alist_t *ta, int srcid)
{
	void *ltgtid;

	if (alist_find(ta, (void *)(uintptr_t)srcid, (void **)&ltgtid))
		return ((uintptr_t)ltgtid);
	else
		return (0);
}

/*
 * Determining equivalence of tdesc_t subgraphs
 */

struct equiv_data {
	alist_t *ed_ta;
	tdesc_t *ed_node;
	tdesc_t *ed_tgt;

	int ed_clear_mark;
	int ed_cur_mark;
	int ed_selfuniquify;
}; /* equiv_data_t */

static int equiv_node(tdesc_t *, tdesc_t *, equiv_data_t *);

/*ARGSUSED2*/
static int
equiv_intrinsic(tdesc_t *stdp, tdesc_t *ttdp, equiv_data_t *ed __unused)
{
	intr_t *si = stdp->t_intr;
	intr_t *ti = ttdp->t_intr;

	if (si->intr_type != ti->intr_type ||
	    si->intr_signed != ti->intr_signed ||
	    si->intr_offset != ti->intr_offset ||
	    si->intr_nbits != ti->intr_nbits)
		return (0);

	if (si->intr_type == INTR_INT &&
	    si->intr_iformat != ti->intr_iformat)
		return (0);
	else if (si->intr_type == INTR_REAL &&
	    si->intr_fformat != ti->intr_fformat)
		return (0);

	return (1);
}

static int
equiv_plain(tdesc_t *stdp, tdesc_t *ttdp, equiv_data_t *ed)
{
	return (equiv_node(stdp->t_tdesc, ttdp->t_tdesc, ed));
}

static int
equiv_function(tdesc_t *stdp, tdesc_t *ttdp, equiv_data_t *ed)
{
	fndef_t *fn1 = stdp->t_fndef, *fn2 = ttdp->t_fndef;
	int i;

	if (fn1->fn_nargs != fn2->fn_nargs ||
	    fn1->fn_vargs != fn2->fn_vargs)
		return (0);

	if (!equiv_node(fn1->fn_ret, fn2->fn_ret, ed))
		return (0);

	for (i = 0; i < (int) fn1->fn_nargs; i++) {
		if (!equiv_node(fn1->fn_args[i], fn2->fn_args[i], ed))
			return (0);
	}

	return (1);
}

static int
equiv_array(tdesc_t *stdp, tdesc_t *ttdp, equiv_data_t *ed)
{
	ardef_t *ar1 = stdp->t_ardef, *ar2 = ttdp->t_ardef;

	if (!equiv_node(ar1->ad_contents, ar2->ad_contents, ed) ||
	    !equiv_node(ar1->ad_idxtype, ar2->ad_idxtype, ed))
		return (0);

	if (ar1->ad_nelems != ar2->ad_nelems)
		return (0);

	return (1);
}

static int
equiv_su(tdesc_t *stdp, tdesc_t *ttdp, equiv_data_t *ed)
{
	mlist_t *ml1 = stdp->t_members, *ml2 = ttdp->t_members;

	while (ml1 && ml2) {
		if (ml1->ml_offset != ml2->ml_offset ||
		    strcmp(ml1->ml_name, ml2->ml_name) != 0 ||
		    ml1->ml_size != ml2->ml_size ||
		    !equiv_node(ml1->ml_type, ml2->ml_type, ed))
			return (0);

		ml1 = ml1->ml_next;
		ml2 = ml2->ml_next;
	}

	if (ml1 || ml2)
		return (0);

	return (1);
}

/*ARGSUSED2*/
static int
equiv_enum(tdesc_t *stdp, tdesc_t *ttdp, equiv_data_t *ed __unused)
{
	elist_t *el1 = stdp->t_emem;
	elist_t *el2 = ttdp->t_emem;

	while (el1 && el2) {
		if (el1->el_number != el2->el_number ||
		    strcmp(el1->el_name, el2->el_name) != 0)
			return (0);

		el1 = el1->el_next;
		el2 = el2->el_next;
	}

	if (el1 || el2)
		return (0);

	return (1);
}

/*ARGSUSED*/
static int
equiv_assert(tdesc_t *stdp __unused, tdesc_t *ttdp __unused, equiv_data_t *ed __unused)
{
	/* foul, evil, and very bad - this is a "shouldn't happen" */
	assert(1 == 0);

	return (0);
}

static int
fwd_equiv(tdesc_t *ctdp, tdesc_t *mtdp)
{
	tdesc_t *defn = (ctdp->t_type == FORWARD ? mtdp : ctdp);

	return (defn->t_type == STRUCT || defn->t_type == UNION ||
	    defn->t_type == ENUM);
}

static int
equiv_node(tdesc_t *ctdp, tdesc_t *mtdp, equiv_data_t *ed)
{
	int (*equiv)(tdesc_t *, tdesc_t *, equiv_data_t *);
	int mapping;

	if (ctdp->t_emark > ed->ed_clear_mark &&
	    mtdp->t_emark > ed->ed_clear_mark)
		return (ctdp->t_emark == mtdp->t_emark);

	/*
	 * In normal (non-self-uniquify) mode, we don't want to do equivalency
	 * checking on a subgraph that has already been checked.  If a mapping
	 * has already been established for a given child node, we can simply
	 * compare the mapping for the child node with the ID of the parent
	 * node.  If we are in self-uniquify mode, then we're comparing two
	 * subgraphs within the child graph, and thus need to ignore any
	 * type mappings that have been created, as they are only valid into the
	 * parent.
	 */
	if ((mapping = get_mapping(ed->ed_ta, ctdp->t_id)) > 0 &&
	    mapping == mtdp->t_id && !ed->ed_selfuniquify)
		return (1);

	if (!streq(ctdp->t_name, mtdp->t_name))
		return (0);

	if (ctdp->t_type != mtdp->t_type) {
		if (ctdp->t_type == FORWARD || mtdp->t_type == FORWARD)
			return (fwd_equiv(ctdp, mtdp));
		else
			return (0);
	}

	ctdp->t_emark = ed->ed_cur_mark;
	mtdp->t_emark = ed->ed_cur_mark;
	ed->ed_cur_mark++;

	if ((equiv = tdesc_ops[ctdp->t_type].equiv) != NULL)
		return (equiv(ctdp, mtdp, ed));

	return (1);
}

/*
 * We perform an equivalency check on two subgraphs by traversing through them
 * in lockstep.  If a given node is equivalent in both the parent and the child,
 * we mark it in both subgraphs, using the t_emark field, with a monotonically
 * increasing number.  If, in the course of the traversal, we reach a node that
 * we have visited and numbered during this equivalency check, we have a cycle.
 * If the previously-visited nodes don't have the same emark, then the edges
 * that brought us to these nodes are not equivalent, and so the check ends.
 * If the emarks are the same, the edges are equivalent.  We then backtrack and
 * continue the traversal.  If we have exhausted all edges in the subgraph, and
 * have not found any inequivalent nodes, then the subgraphs are equivalent.
 */
static int
equiv_cb(void *bucket, void *arg)
{
	equiv_data_t *ed = arg;
	tdesc_t *mtdp = bucket;
	tdesc_t *ctdp = ed->ed_node;

	ed->ed_clear_mark = ed->ed_cur_mark + 1;
	ed->ed_cur_mark = ed->ed_clear_mark + 1;

	if (equiv_node(ctdp, mtdp, ed)) {
		debug(3, "equiv_node matched %d <%x> %d <%x>\n",
		    ctdp->t_id, ctdp->t_id, mtdp->t_id, mtdp->t_id);
		ed->ed_tgt = mtdp;
		/* matched.  stop looking */
		return (-1);
	}

	return (0);
}

/*ARGSUSED1*/
static int
map_td_tree_pre(tdesc_t *ctdp, tdesc_t **ctdpp __unused, void *private)
{
	merge_cb_data_t *mcd = private;

	if (get_mapping(mcd->md_ta, ctdp->t_id) > 0)
		return (0);

	return (1);
}

/*ARGSUSED1*/
static int
map_td_tree_post(tdesc_t *ctdp, tdesc_t **ctdpp __unused, void *private)
{
	merge_cb_data_t *mcd = private;
	equiv_data_t ed;

	ed.ed_ta = mcd->md_ta;
	ed.ed_clear_mark = mcd->md_parent->td_curemark;
	ed.ed_cur_mark = mcd->md_parent->td_curemark + 1;
	ed.ed_node = ctdp;
	ed.ed_selfuniquify = 0;

	debug(3, "map_td_tree_post on %d <%x> %s\n", ctdp->t_id, ctdp->t_id,tdesc_name(ctdp));

	if (hash_find_iter(mcd->md_parent->td_layouthash, ctdp,
	    equiv_cb, &ed) < 0) {
		/* We found an equivalent node */
		if (ed.ed_tgt->t_type == FORWARD && ctdp->t_type != FORWARD) {
			int id = mcd->md_tgt->td_nextid++;

			debug(3, "Creating new defn type %d <%x>\n", id, id);
			add_mapping(mcd->md_ta, ctdp->t_id, id);
			alist_add(mcd->md_fdida, (void *)(ulong_t)ed.ed_tgt,
			    (void *)(ulong_t)id);
			hash_add(mcd->md_tdtba, ctdp);
		} else
			add_mapping(mcd->md_ta, ctdp->t_id, ed.ed_tgt->t_id);

	} else if (debug_level > 1 && hash_iter(mcd->md_parent->td_idhash,
	    equiv_cb, &ed) < 0) {
		/*
		 * We didn't find an equivalent node by looking through the
		 * layout hash, but we somehow found it by performing an
		 * exhaustive search through the entire graph.  This usually
		 * means that the "name" hash function is broken.
		 */
		aborterr("Second pass for %d (%s) == %d\n", ctdp->t_id,
		    tdesc_name(ctdp), ed.ed_tgt->t_id);
	} else {
		int id = mcd->md_tgt->td_nextid++;

		debug(3, "Creating new type %d <%x>\n", id, id);
		add_mapping(mcd->md_ta, ctdp->t_id, id);
		hash_add(mcd->md_tdtba, ctdp);
	}

	mcd->md_parent->td_curemark = ed.ed_cur_mark + 1;

	return (1);
}

/*ARGSUSED1*/
static int
map_td_tree_self_post(tdesc_t *ctdp, tdesc_t **ctdpp __unused, void *private)
{
	merge_cb_data_t *mcd = private;
	equiv_data_t ed;

	ed.ed_ta = mcd->md_ta;
	ed.ed_clear_mark = mcd->md_parent->td_curemark;
	ed.ed_cur_mark = mcd->md_parent->td_curemark + 1;
	ed.ed_node = ctdp;
	ed.ed_selfuniquify = 1;
	ed.ed_tgt = NULL;

	if (hash_find_iter(mcd->md_tdtba, ctdp, equiv_cb, &ed) < 0) {
		debug(3, "Self check found %d <%x> in %d <%x>\n", ctdp->t_id,
		    ctdp->t_id, ed.ed_tgt->t_id, ed.ed_tgt->t_id);
		add_mapping(mcd->md_ta, ctdp->t_id,
		    get_mapping(mcd->md_ta, ed.ed_tgt->t_id));
	} else if (debug_level > 1 && hash_iter(mcd->md_tdtba,
	    equiv_cb, &ed) < 0) {
		/*
		 * We didn't find an equivalent node using the quick way (going
		 * through the hash normally), but we did find it by iterating
		 * through the entire hash.  This usually means that the hash
		 * function is broken.
		 */
		aborterr("Self-unique second pass for %d <%x> (%s) == %d <%x>\n",
		    ctdp->t_id, ctdp->t_id, tdesc_name(ctdp), ed.ed_tgt->t_id,
		    ed.ed_tgt->t_id);
	} else {
		int id = mcd->md_tgt->td_nextid++;

		debug(3, "Creating new type %d <%x>\n", id, id);
		add_mapping(mcd->md_ta, ctdp->t_id, id);
		hash_add(mcd->md_tdtba, ctdp);
	}

	mcd->md_parent->td_curemark = ed.ed_cur_mark + 1;

	return (1);
}

static tdtrav_cb_f map_pre[] = {
	NULL,
	map_td_tree_pre,	/* intrinsic */
	map_td_tree_pre,	/* pointer */
	map_td_tree_pre,	/* array */
	map_td_tree_pre,	/* function */
	map_td_tree_pre,	/* struct */
	map_td_tree_pre,	/* union */
	map_td_tree_pre,	/* enum */
	map_td_tree_pre,	/* forward */
	map_td_tree_pre,	/* typedef */
	tdtrav_assert,		/* typedef_unres */
	map_td_tree_pre,	/* volatile */
	map_td_tree_pre,	/* const */
	map_td_tree_pre		/* restrict */
};

static tdtrav_cb_f map_post[] = {
	NULL,
	map_td_tree_post,	/* intrinsic */
	map_td_tree_post,	/* pointer */
	map_td_tree_post,	/* array */
	map_td_tree_post,	/* function */
	map_td_tree_post,	/* struct */
	map_td_tree_post,	/* union */
	map_td_tree_post,	/* enum */
	map_td_tree_post,	/* forward */
	map_td_tree_post,	/* typedef */
	tdtrav_assert,		/* typedef_unres */
	map_td_tree_post,	/* volatile */
	map_td_tree_post,	/* const */
	map_td_tree_post	/* restrict */
};

static tdtrav_cb_f map_self_post[] = {
	NULL,
	map_td_tree_self_post,	/* intrinsic */
	map_td_tree_self_post,	/* pointer */
	map_td_tree_self_post,	/* array */
	map_td_tree_self_post,	/* function */
	map_td_tree_self_post,	/* struct */
	map_td_tree_self_post,	/* union */
	map_td_tree_self_post,	/* enum */
	map_td_tree_self_post,	/* forward */
	map_td_tree_self_post,	/* typedef */
	tdtrav_assert,		/* typedef_unres */
	map_td_tree_self_post,	/* volatile */
	map_td_tree_self_post,	/* const */
	map_td_tree_self_post	/* restrict */
};

/*
 * Determining equivalence of iidesc_t nodes
 */

typedef struct iifind_data {
	iidesc_t *iif_template;
	alist_t *iif_ta;
	int iif_newidx;
	int iif_refmerge;
} iifind_data_t;

/*
 * Check to see if this iidesc_t (node) - the current one on the list we're
 * iterating through - matches the target one (iif->iif_template).  Return -1
 * if it matches, to stop the iteration.
 */
static int
iidesc_match(void *data, void *arg)
{
	iidesc_t *node = data;
	iifind_data_t *iif = arg;
	int i;

	if (node->ii_type != iif->iif_template->ii_type ||
	    !streq(node->ii_name, iif->iif_template->ii_name) ||
	    node->ii_dtype->t_id != iif->iif_newidx)
		return (0);

	if ((node->ii_type == II_SVAR || node->ii_type == II_SFUN) &&
	    !streq(node->ii_owner, iif->iif_template->ii_owner))
		return (0);

	if (node->ii_nargs != iif->iif_template->ii_nargs)
		return (0);

	for (i = 0; i < node->ii_nargs; i++) {
		if (get_mapping(iif->iif_ta,
		    iif->iif_template->ii_args[i]->t_id) !=
		    node->ii_args[i]->t_id)
			return (0);
	}

	if (iif->iif_refmerge) {
		switch (iif->iif_template->ii_type) {
		case II_GFUN:
		case II_SFUN:
		case II_GVAR:
		case II_SVAR:
			debug(3, "suppressing duping of %d %s from %s\n",
			    iif->iif_template->ii_type,
			    iif->iif_template->ii_name,
			    (iif->iif_template->ii_owner ?
			    iif->iif_template->ii_owner : "NULL"));
			return (0);
		case II_NOT:
		case II_PSYM:
		case II_SOU:
		case II_TYPE:
			break;
		}
	}

	return (-1);
}

static int
merge_type_cb(void *data, void *arg)
{
	iidesc_t *sii = data;
	merge_cb_data_t *mcd = arg;
	iifind_data_t iif;
	tdtrav_cb_f *post;

	post = (mcd->md_flags & MCD_F_SELFUNIQUIFY ? map_self_post : map_post);

	/* Map the tdesc nodes */
	(void) iitraverse(sii, &mcd->md_parent->td_curvgen, NULL, map_pre, post,
	    mcd);

	/* Map the iidesc nodes */
	iif.iif_template = sii;
	iif.iif_ta = mcd->md_ta;
	iif.iif_newidx = get_mapping(mcd->md_ta, sii->ii_dtype->t_id);
	iif.iif_refmerge = (mcd->md_flags & MCD_F_REFMERGE);

	if (hash_match(mcd->md_parent->td_iihash, sii, iidesc_match,
	    &iif) == 1)
		/* successfully mapped */
		return (1);

	debug(3, "tba %s (%d)\n", (sii->ii_name ? sii->ii_name : "(anon)"),
	    sii->ii_type);

	list_add(mcd->md_iitba, sii);

	return (0);
}

static int
remap_node(tdesc_t **tgtp, tdesc_t *oldtgt, int selftid, tdesc_t *newself,
    merge_cb_data_t *mcd)
{
	tdesc_t *tgt = NULL;
	tdesc_t template;
	int oldid = oldtgt->t_id;

	if (oldid == selftid) {
		*tgtp = newself;
		return (1);
	}

	if ((template.t_id = get_mapping(mcd->md_ta, oldid)) == 0)
		aborterr("failed to get mapping for tid %d <%x>\n", oldid, oldid);

	if (!hash_find(mcd->md_parent->td_idhash, (void *)&template,
	    (void *)&tgt) && (!(mcd->md_flags & MCD_F_REFMERGE) ||
	    !hash_find(mcd->md_tgt->td_idhash, (void *)&template,
	    (void *)&tgt))) {
		debug(3, "Remap couldn't find %d <%x> (from %d <%x>)\n", template.t_id,
		    template.t_id, oldid, oldid);
		*tgtp = oldtgt;
		list_add(mcd->md_tdtbr, tgtp);
		return (0);
	}

	*tgtp = tgt;
	return (1);
}

static tdesc_t *
conjure_template(tdesc_t *old, int newselfid)
{
	tdesc_t *new = xcalloc(sizeof (tdesc_t));

	new->t_name = old->t_name ? xstrdup(old->t_name) : NULL;
	new->t_type = old->t_type;
	new->t_size = old->t_size;
	new->t_id = newselfid;
	new->t_flags = old->t_flags;

	return (new);
}

/*ARGSUSED2*/
static tdesc_t *
conjure_intrinsic(tdesc_t *old, int newselfid, merge_cb_data_t *mcd __unused)
{
	tdesc_t *new = conjure_template(old, newselfid);

	new->t_intr = xmalloc(sizeof (intr_t));
	bcopy(old->t_intr, new->t_intr, sizeof (intr_t));

	return (new);
}

static tdesc_t *
conjure_plain(tdesc_t *old, int newselfid, merge_cb_data_t *mcd)
{
	tdesc_t *new = conjure_template(old, newselfid);

	(void) remap_node(&new->t_tdesc, old->t_tdesc, old->t_id, new, mcd);

	return (new);
}

static tdesc_t *
conjure_function(tdesc_t *old, int newselfid, merge_cb_data_t *mcd)
{
	tdesc_t *new = conjure_template(old, newselfid);
	fndef_t *nfn = xmalloc(sizeof (fndef_t));
	fndef_t *ofn = old->t_fndef;
	int i;

	(void) remap_node(&nfn->fn_ret, ofn->fn_ret, old->t_id, new, mcd);

	nfn->fn_nargs = ofn->fn_nargs;
	nfn->fn_vargs = ofn->fn_vargs;

	if (nfn->fn_nargs > 0)
		nfn->fn_args = xcalloc(sizeof (tdesc_t *) * ofn->fn_nargs);

	for (i = 0; i < (int) ofn->fn_nargs; i++) {
		(void) remap_node(&nfn->fn_args[i], ofn->fn_args[i], old->t_id,
		    new, mcd);
	}

	new->t_fndef = nfn;

	return (new);
}

static tdesc_t *
conjure_array(tdesc_t *old, int newselfid, merge_cb_data_t *mcd)
{
	tdesc_t *new = conjure_template(old, newselfid);
	ardef_t *nar = xmalloc(sizeof (ardef_t));
	ardef_t *oar = old->t_ardef;

	(void) remap_node(&nar->ad_contents, oar->ad_contents, old->t_id, new,
	    mcd);
	(void) remap_node(&nar->ad_idxtype, oar->ad_idxtype, old->t_id, new,
	    mcd);

	nar->ad_nelems = oar->ad_nelems;

	new->t_ardef = nar;

	return (new);
}

static tdesc_t *
conjure_su(tdesc_t *old, int newselfid, merge_cb_data_t *mcd)
{
	tdesc_t *new = conjure_template(old, newselfid);
	mlist_t *omem, **nmemp;

	for (omem = old->t_members, nmemp = &new->t_members;
	    omem; omem = omem->ml_next, nmemp = &((*nmemp)->ml_next)) {
		*nmemp = xmalloc(sizeof (mlist_t));
		(*nmemp)->ml_offset = omem->ml_offset;
		(*nmemp)->ml_size = omem->ml_size;
		(*nmemp)->ml_name = xstrdup(omem->ml_name ? omem->ml_name : "empty omem->ml_name");
		(void) remap_node(&((*nmemp)->ml_type), omem->ml_type,
		    old->t_id, new, mcd);
	}
	*nmemp = NULL;

	return (new);
}

/*ARGSUSED2*/
static tdesc_t *
conjure_enum(tdesc_t *old, int newselfid, merge_cb_data_t *mcd __unused)
{
	tdesc_t *new = conjure_template(old, newselfid);
	elist_t *oel, **nelp;

	for (oel = old->t_emem, nelp = &new->t_emem;
	    oel; oel = oel->el_next, nelp = &((*nelp)->el_next)) {
		*nelp = xmalloc(sizeof (elist_t));
		(*nelp)->el_name = xstrdup(oel->el_name);
		(*nelp)->el_number = oel->el_number;
	}
	*nelp = NULL;

	return (new);
}

/*ARGSUSED2*/
static tdesc_t *
conjure_forward(tdesc_t *old, int newselfid, merge_cb_data_t *mcd)
{
	tdesc_t *new = conjure_template(old, newselfid);

	list_add(&mcd->md_tgt->td_fwdlist, new);

	return (new);
}

/*ARGSUSED*/
static tdesc_t *
conjure_assert(tdesc_t *old __unused, int newselfid __unused, merge_cb_data_t *mcd __unused)
{
	assert(1 == 0);
	return (NULL);
}

static iidesc_t *
conjure_iidesc(iidesc_t *old, merge_cb_data_t *mcd)
{
	iidesc_t *new = iidesc_dup(old);
	int i;

	(void) remap_node(&new->ii_dtype, old->ii_dtype, -1, NULL, mcd);
	for (i = 0; i < new->ii_nargs; i++) {
		(void) remap_node(&new->ii_args[i], old->ii_args[i], -1, NULL,
		    mcd);
	}

	return (new);
}

static int
fwd_redir(tdesc_t *fwd, tdesc_t **fwdp, void *private)
{
	alist_t *map = private;
	void *defn;

	if (!alist_find(map, (void *)fwd, (void **)&defn))
		return (0);

	debug(3, "Redirecting an edge to %s\n", tdesc_name(defn));

	*fwdp = defn;

	return (1);
}

static tdtrav_cb_f fwd_redir_cbs[] = {
	NULL,
	NULL,			/* intrinsic */
	NULL,			/* pointer */
	NULL,			/* array */
	NULL,			/* function */
	NULL,			/* struct */
	NULL,			/* union */
	NULL,			/* enum */
	fwd_redir,		/* forward */
	NULL,			/* typedef */
	tdtrav_assert,		/* typedef_unres */
	NULL,			/* volatile */
	NULL,			/* const */
	NULL			/* restrict */
};

typedef struct redir_mstr_data {
	tdata_t *rmd_tgt;
	alist_t *rmd_map;
} redir_mstr_data_t;

static int
redir_mstr_fwd_cb(void *name, void *value, void *arg)
{
	tdesc_t *fwd = name;
	int defnid = (uintptr_t)value;
	redir_mstr_data_t *rmd = arg;
	tdesc_t template;
	tdesc_t *defn;

	template.t_id = defnid;

	if (!hash_find(rmd->rmd_tgt->td_idhash, (void *)&template,
	    (void *)&defn)) {
		aborterr("Couldn't unforward %d (%s)\n", defnid,
		    tdesc_name(defn));
	}

	debug(3, "Forward map: resolved %d to %s\n", defnid, tdesc_name(defn));

	alist_add(rmd->rmd_map, (void *)fwd, (void *)defn);

	return (1);
}

static void
redir_mstr_fwds(merge_cb_data_t *mcd)
{
	redir_mstr_data_t rmd;
	alist_t *map = alist_new(NULL, NULL);

	rmd.rmd_tgt = mcd->md_tgt;
	rmd.rmd_map = map;

	if (alist_iter(mcd->md_fdida, redir_mstr_fwd_cb, &rmd)) {
		(void) iitraverse_hash(mcd->md_tgt->td_iihash,
		    &mcd->md_tgt->td_curvgen, fwd_redir_cbs, NULL, NULL, map);
	}

	alist_free(map);
}

static int
add_iitba_cb(void *data, void *private)
{
	merge_cb_data_t *mcd = private;
	iidesc_t *tba = data;
	iidesc_t *new;
	iifind_data_t iif;
	int newidx;

	newidx = get_mapping(mcd->md_ta, tba->ii_dtype->t_id);
	assert(newidx != -1);

	(void) list_remove(mcd->md_iitba, data, NULL, NULL);

	iif.iif_template = tba;
	iif.iif_ta = mcd->md_ta;
	iif.iif_newidx = newidx;
	iif.iif_refmerge = (mcd->md_flags & MCD_F_REFMERGE);

	if (hash_match(mcd->md_parent->td_iihash, tba, iidesc_match,
	    &iif) == 1) {
		debug(3, "iidesc_t %s already exists\n",
		    (tba->ii_name ? tba->ii_name : "(anon)"));
		return (1);
	}

	new = conjure_iidesc(tba, mcd);
	hash_add(mcd->md_tgt->td_iihash, new);

	return (1);
}

static int
add_tdesc(tdesc_t *oldtdp, int newid, merge_cb_data_t *mcd)
{
	tdesc_t *newtdp;
	tdesc_t template;

	template.t_id = newid;
	assert(hash_find(mcd->md_parent->td_idhash,
	    (void *)&template, NULL) == 0);

	debug(3, "trying to conjure %d %s (%d, <%x>) as %d, <%x>\n",
	    oldtdp->t_type, tdesc_name(oldtdp), oldtdp->t_id,
	    oldtdp->t_id, newid, newid);

	if ((newtdp = tdesc_ops[oldtdp->t_type].conjure(oldtdp, newid,
	    mcd)) == NULL)
		/* couldn't map everything */
		return (0);

	debug(3, "succeeded\n");

	hash_add(mcd->md_tgt->td_idhash, newtdp);
	hash_add(mcd->md_tgt->td_layouthash, newtdp);

	return (1);
}

static int
add_tdtba_cb(void *data, void *arg)
{
	tdesc_t *tdp = data;
	merge_cb_data_t *mcd = arg;
	int newid;
	int rc;

	newid = get_mapping(mcd->md_ta, tdp->t_id);
	assert(newid != -1);

	if ((rc = add_tdesc(tdp, newid, mcd)))
		hash_remove(mcd->md_tdtba, (void *)tdp);

	return (rc);
}

static int
add_tdtbr_cb(void *data, void *arg)
{
	tdesc_t **tdpp = data;
	merge_cb_data_t *mcd = arg;

	debug(3, "Remapping %s (%d)\n", tdesc_name(*tdpp), (*tdpp)->t_id);

	if (!remap_node(tdpp, *tdpp, -1, NULL, mcd))
		return (0);

	(void) list_remove(mcd->md_tdtbr, (void *)tdpp, NULL, NULL);
	return (1);
}

static void
merge_types(hash_t *src, merge_cb_data_t *mcd)
{
	list_t *iitba = NULL;
	list_t *tdtbr = NULL;
	int iirc, tdrc;

	mcd->md_iitba = &iitba;
	mcd->md_tdtba = hash_new(TDATA_LAYOUT_HASH_SIZE, tdesc_layouthash,
	    tdesc_layoutcmp);
	mcd->md_tdtbr = &tdtbr;

	(void) hash_iter(src, merge_type_cb, mcd);

	tdrc = hash_iter(mcd->md_tdtba, add_tdtba_cb, mcd);
	debug(3, "add_tdtba_cb added %d items\n", tdrc);

	iirc = list_iter(*mcd->md_iitba, add_iitba_cb, mcd);
	debug(3, "add_iitba_cb added %d items\n", iirc);

	assert(list_count(*mcd->md_iitba) == 0 &&
	    hash_count(mcd->md_tdtba) == 0);

	tdrc = list_iter(*mcd->md_tdtbr, add_tdtbr_cb, mcd);
	debug(3, "add_tdtbr_cb added %d items\n", tdrc);

	if (list_count(*mcd->md_tdtbr) != 0)
		aborterr("Couldn't remap all nodes\n");

	/*
	 * We now have an alist of master forwards and the ids of the new master
	 * definitions for those forwards in mcd->md_fdida.  By this point,
	 * we're guaranteed that all of the master definitions referenced in
	 * fdida have been added to the master tree.  We now traverse through
	 * the master tree, redirecting all edges inbound to forwards that have
	 * definitions to those definitions.
	 */
	if (mcd->md_parent == mcd->md_tgt) {
		redir_mstr_fwds(mcd);
	}
}

void
merge_into_master(tdata_t *cur, tdata_t *mstr, tdata_t *tgt, int selfuniquify)
{
	merge_cb_data_t mcd;

	cur->td_ref++;
	mstr->td_ref++;
	if (tgt)
		tgt->td_ref++;

	assert(cur->td_ref == 1 && mstr->td_ref == 1 &&
	    (tgt == NULL || tgt->td_ref == 1));

	mcd.md_parent = mstr;
	mcd.md_tgt = (tgt ? tgt : mstr);
	mcd.md_ta = alist_new(NULL, NULL);
	mcd.md_fdida = alist_new(NULL, NULL);
	mcd.md_flags = 0;

	if (selfuniquify)
		mcd.md_flags |= MCD_F_SELFUNIQUIFY;
	if (tgt)
		mcd.md_flags |= MCD_F_REFMERGE;

	mstr->td_curvgen = MAX(mstr->td_curvgen, cur->td_curvgen);
	mstr->td_curemark = MAX(mstr->td_curemark, cur->td_curemark);

	merge_types(cur->td_iihash, &mcd);

	if (debug_level >= 3) {
		debug(3, "Type association stats\n");
		alist_stats(mcd.md_ta, 0);
		debug(3, "Layout hash stats\n");
		hash_stats(mcd.md_tgt->td_layouthash, 1);
	}

	alist_free(mcd.md_fdida);
	alist_free(mcd.md_ta);

	cur->td_ref--;
	mstr->td_ref--;
	if (tgt)
		tgt->td_ref--;
}

tdesc_ops_t tdesc_ops[] = {
	{ "ERROR! BAD tdesc TYPE", NULL, NULL },
	{ "intrinsic",		equiv_intrinsic,	conjure_intrinsic },
	{ "pointer", 		equiv_plain,		conjure_plain },
	{ "array", 		equiv_array,		conjure_array },
	{ "function", 		equiv_function,		conjure_function },
	{ "struct",		equiv_su,		conjure_su },
	{ "union",		equiv_su,		conjure_su },
	{ "enum",		equiv_enum,		conjure_enum },
	{ "forward",		NULL,			conjure_forward },
	{ "typedef",		equiv_plain,		conjure_plain },
	{ "typedef_unres",	equiv_assert,		conjure_assert },
	{ "volatile",		equiv_plain,		conjure_plain },
	{ "const", 		equiv_plain,		conjure_plain },
	{ "restrict",		equiv_plain,		conjure_plain }
};
