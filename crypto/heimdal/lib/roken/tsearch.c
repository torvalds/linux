/*
 * Tree search generalized from Knuth (6.2.2) Algorithm T just like
 * the AT&T man page says.
 *
 * The node_t structure is for internal use only, lint doesn't grok it.
 *
 * Written by reading the System V Interface Definition, not the code.
 *
 * Totally public domain.
 *
 * $NetBSD: tsearch.c,v 1.3 1999/09/16 11:45:37 lukem Exp $
 * $NetBSD: twalk.c,v 1.1 1999/02/22 10:33:16 christos Exp $
 * $NetBSD: tdelete.c,v 1.2 1999/09/16 11:45:37 lukem Exp $
 * $NetBSD: tfind.c,v 1.2 1999/09/16 11:45:37 lukem Exp $
 */

#include <config.h>
#include "roken.h"
#include "search.h"
#include <stdlib.h>

typedef struct node {
  char         *key;
  struct node  *llink, *rlink;
} node_t;

#ifndef __DECONST
#define __DECONST(type, var)    ((type)(uintptr_t)(const void *)(var))
#endif

/*
 * find or insert datum into search tree
 *
 * Parameters:
 *	vkey:	key to be located
 *	vrootp:	address of tree root
 */

ROKEN_LIB_FUNCTION void *
rk_tsearch(const void *vkey, void **vrootp,
	int (*compar)(const void *, const void *))
{
	node_t *q;
	node_t **rootp = (node_t **)vrootp;

	if (rootp == NULL)
		return NULL;

	while (*rootp != NULL) {	/* Knuth's T1: */
		int r;

		if ((r = (*compar)(vkey, (*rootp)->key)) == 0)	/* T2: */
			return *rootp;		/* we found it! */

		rootp = (r < 0) ?
		    &(*rootp)->llink :		/* T3: follow left branch */
		    &(*rootp)->rlink;		/* T4: follow right branch */
	}

	q = malloc(sizeof(node_t));		/* T5: key not found */
	if (q != 0) {				/* make new node */
		*rootp = q;			/* link new node to old */
		/* LINTED const castaway ok */
		q->key = __DECONST(void *, vkey); /* initialize new node */
		q->llink = q->rlink = NULL;
	}
	return q;
}

/*
 * Walk the nodes of a tree
 *
 * Parameters:
 *	root:	Root of the tree to be walked
 */
static void
trecurse(const node_t *root, void (*action)(const void *, VISIT, int),
	 int level)
{

	if (root->llink == NULL && root->rlink == NULL)
		(*action)(root, leaf, level);
	else {
		(*action)(root, preorder, level);
		if (root->llink != NULL)
			trecurse(root->llink, action, level + 1);
		(*action)(root, postorder, level);
		if (root->rlink != NULL)
			trecurse(root->rlink, action, level + 1);
		(*action)(root, endorder, level);
	}
}

/*
 * Walk the nodes of a tree
 *
 * Parameters:
 *	vroot:	Root of the tree to be walked
 */
ROKEN_LIB_FUNCTION void
rk_twalk(const void *vroot,
      void (*action)(const void *, VISIT, int))
{
	if (vroot != NULL && action != NULL)
		trecurse(vroot, action, 0);
}

/*
 * delete node with given key
 *
 * vkey:   key to be deleted
 * vrootp: address of the root of the tree
 * compar: function to carry out node comparisons
 */
ROKEN_LIB_FUNCTION void *
rk_tdelete(const void * vkey, void ** vrootp,
	int (*compar)(const void *, const void *))
{
	node_t **rootp = (node_t **)vrootp;
	node_t *p, *q, *r;
	int cmp;

	if (rootp == NULL || (p = *rootp) == NULL)
		return NULL;

	while ((cmp = (*compar)(vkey, (*rootp)->key)) != 0) {
		p = *rootp;
		rootp = (cmp < 0) ?
		    &(*rootp)->llink :		/* follow llink branch */
		    &(*rootp)->rlink;		/* follow rlink branch */
		if (*rootp == NULL)
			return NULL;		/* key not found */
	}
	r = (*rootp)->rlink;			/* D1: */
	if ((q = (*rootp)->llink) == NULL)	/* Left NULL? */
		q = r;
	else if (r != NULL) {			/* Right link is NULL? */
		if (r->llink == NULL) {		/* D2: Find successor */
			r->llink = q;
			q = r;
		} else {			/* D3: Find NULL link */
			for (q = r->llink; q->llink != NULL; q = r->llink)
				r = q;
			r->llink = q->rlink;
			q->llink = (*rootp)->llink;
			q->rlink = (*rootp)->rlink;
		}
	}
	free(*rootp);				/* D4: Free node */
	*rootp = q;				/* link parent to new node */
	return p;
}

/*
 * find a node, or return 0
 *
 * Parameters:
 *	vkey:	key to be found
 *	vrootp:	address of the tree root
 */
ROKEN_LIB_FUNCTION void *
rk_tfind(const void *vkey, void * const *vrootp,
      int (*compar)(const void *, const void *))
{
	node_t **rootp = (node_t **)vrootp;

	if (rootp == NULL)
		return NULL;

	while (*rootp != NULL) {		/* T1: */
		int r;

		if ((r = (*compar)(vkey, (*rootp)->key)) == 0)	/* T2: */
			return *rootp;		/* key found */
		rootp = (r < 0) ?
		    &(*rootp)->llink :		/* T3: follow left branch */
		    &(*rootp)->rlink;		/* T4: follow right branch */
	}
	return NULL;
}
