/*
 * A Pairing Heap implementation.
 *
 * "The Pairing Heap: A New Form of Self-Adjusting Heap"
 * https://www.cs.cmu.edu/~sleator/papers/pairing-heaps.pdf
 *
 * With auxiliary twopass list, described in a follow on paper.
 *
 * "Pairing Heaps: Experiments and Analysis"
 * http://citeseerx.ist.psu.edu/viewdoc/download?doi=10.1.1.106.2988&rep=rep1&type=pdf
 *
 *******************************************************************************
 */

#ifndef PH_H_
#define PH_H_

/* Node structure. */
#define phn(a_type)							\
struct {								\
	a_type	*phn_prev;						\
	a_type	*phn_next;						\
	a_type	*phn_lchild;						\
}

/* Root structure. */
#define ph(a_type)							\
struct {								\
	a_type	*ph_root;						\
}

/* Internal utility macros. */
#define phn_lchild_get(a_type, a_field, a_phn)				\
	(a_phn->a_field.phn_lchild)
#define phn_lchild_set(a_type, a_field, a_phn, a_lchild) do {		\
	a_phn->a_field.phn_lchild = a_lchild;				\
} while (0)

#define phn_next_get(a_type, a_field, a_phn)				\
	(a_phn->a_field.phn_next)
#define phn_prev_set(a_type, a_field, a_phn, a_prev) do {		\
	a_phn->a_field.phn_prev = a_prev;				\
} while (0)

#define phn_prev_get(a_type, a_field, a_phn)				\
	(a_phn->a_field.phn_prev)
#define phn_next_set(a_type, a_field, a_phn, a_next) do {		\
	a_phn->a_field.phn_next = a_next;				\
} while (0)

#define phn_merge_ordered(a_type, a_field, a_phn0, a_phn1, a_cmp) do {	\
	a_type *phn0child;						\
									\
	assert(a_phn0 != NULL);						\
	assert(a_phn1 != NULL);						\
	assert(a_cmp(a_phn0, a_phn1) <= 0);				\
									\
	phn_prev_set(a_type, a_field, a_phn1, a_phn0);			\
	phn0child = phn_lchild_get(a_type, a_field, a_phn0);		\
	phn_next_set(a_type, a_field, a_phn1, phn0child);		\
	if (phn0child != NULL) {					\
		phn_prev_set(a_type, a_field, phn0child, a_phn1);	\
	}								\
	phn_lchild_set(a_type, a_field, a_phn0, a_phn1);		\
} while (0)

#define phn_merge(a_type, a_field, a_phn0, a_phn1, a_cmp, r_phn) do {	\
	if (a_phn0 == NULL) {						\
		r_phn = a_phn1;						\
	} else if (a_phn1 == NULL) {					\
		r_phn = a_phn0;						\
	} else if (a_cmp(a_phn0, a_phn1) < 0) {				\
		phn_merge_ordered(a_type, a_field, a_phn0, a_phn1,	\
		    a_cmp);						\
		r_phn = a_phn0;						\
	} else {							\
		phn_merge_ordered(a_type, a_field, a_phn1, a_phn0,	\
		    a_cmp);						\
		r_phn = a_phn1;						\
	}								\
} while (0)

#define ph_merge_siblings(a_type, a_field, a_phn, a_cmp, r_phn) do {	\
	a_type *head = NULL;						\
	a_type *tail = NULL;						\
	a_type *phn0 = a_phn;						\
	a_type *phn1 = phn_next_get(a_type, a_field, phn0);		\
									\
	/*								\
	 * Multipass merge, wherein the first two elements of a FIFO	\
	 * are repeatedly merged, and each result is appended to the	\
	 * singly linked FIFO, until the FIFO contains only a single	\
	 * element.  We start with a sibling list but no reference to	\
	 * its tail, so we do a single pass over the sibling list to	\
	 * populate the FIFO.						\
	 */								\
	if (phn1 != NULL) {						\
		a_type *phnrest = phn_next_get(a_type, a_field, phn1);	\
		if (phnrest != NULL) {					\
			phn_prev_set(a_type, a_field, phnrest, NULL);	\
		}							\
		phn_prev_set(a_type, a_field, phn0, NULL);		\
		phn_next_set(a_type, a_field, phn0, NULL);		\
		phn_prev_set(a_type, a_field, phn1, NULL);		\
		phn_next_set(a_type, a_field, phn1, NULL);		\
		phn_merge(a_type, a_field, phn0, phn1, a_cmp, phn0);	\
		head = tail = phn0;					\
		phn0 = phnrest;						\
		while (phn0 != NULL) {					\
			phn1 = phn_next_get(a_type, a_field, phn0);	\
			if (phn1 != NULL) {				\
				phnrest = phn_next_get(a_type, a_field,	\
				    phn1);				\
				if (phnrest != NULL) {			\
					phn_prev_set(a_type, a_field,	\
					    phnrest, NULL);		\
				}					\
				phn_prev_set(a_type, a_field, phn0,	\
				    NULL);				\
				phn_next_set(a_type, a_field, phn0,	\
				    NULL);				\
				phn_prev_set(a_type, a_field, phn1,	\
				    NULL);				\
				phn_next_set(a_type, a_field, phn1,	\
				    NULL);				\
				phn_merge(a_type, a_field, phn0, phn1,	\
				    a_cmp, phn0);			\
				phn_next_set(a_type, a_field, tail,	\
				    phn0);				\
				tail = phn0;				\
				phn0 = phnrest;				\
			} else {					\
				phn_next_set(a_type, a_field, tail,	\
				    phn0);				\
				tail = phn0;				\
				phn0 = NULL;				\
			}						\
		}							\
		phn0 = head;						\
		phn1 = phn_next_get(a_type, a_field, phn0);		\
		if (phn1 != NULL) {					\
			while (true) {					\
				head = phn_next_get(a_type, a_field,	\
				    phn1);				\
				assert(phn_prev_get(a_type, a_field,	\
				    phn0) == NULL);			\
				phn_next_set(a_type, a_field, phn0,	\
				    NULL);				\
				assert(phn_prev_get(a_type, a_field,	\
				    phn1) == NULL);			\
				phn_next_set(a_type, a_field, phn1,	\
				    NULL);				\
				phn_merge(a_type, a_field, phn0, phn1,	\
				    a_cmp, phn0);			\
				if (head == NULL) {			\
					break;				\
				}					\
				phn_next_set(a_type, a_field, tail,	\
				    phn0);				\
				tail = phn0;				\
				phn0 = head;				\
				phn1 = phn_next_get(a_type, a_field,	\
				    phn0);				\
			}						\
		}							\
	}								\
	r_phn = phn0;							\
} while (0)

#define ph_merge_aux(a_type, a_field, a_ph, a_cmp) do {			\
	a_type *phn = phn_next_get(a_type, a_field, a_ph->ph_root);	\
	if (phn != NULL) {						\
		phn_prev_set(a_type, a_field, a_ph->ph_root, NULL);	\
		phn_next_set(a_type, a_field, a_ph->ph_root, NULL);	\
		phn_prev_set(a_type, a_field, phn, NULL);		\
		ph_merge_siblings(a_type, a_field, phn, a_cmp, phn);	\
		assert(phn_next_get(a_type, a_field, phn) == NULL);	\
		phn_merge(a_type, a_field, a_ph->ph_root, phn, a_cmp,	\
		    a_ph->ph_root);					\
	}								\
} while (0)

#define ph_merge_children(a_type, a_field, a_phn, a_cmp, r_phn) do {	\
	a_type *lchild = phn_lchild_get(a_type, a_field, a_phn);	\
	if (lchild == NULL) {						\
		r_phn = NULL;						\
	} else {							\
		ph_merge_siblings(a_type, a_field, lchild, a_cmp,	\
		    r_phn);						\
	}								\
} while (0)

/*
 * The ph_proto() macro generates function prototypes that correspond to the
 * functions generated by an equivalently parameterized call to ph_gen().
 */
#define ph_proto(a_attr, a_prefix, a_ph_type, a_type)			\
a_attr void	a_prefix##new(a_ph_type *ph);				\
a_attr bool	a_prefix##empty(a_ph_type *ph);				\
a_attr a_type	*a_prefix##first(a_ph_type *ph);			\
a_attr a_type	*a_prefix##any(a_ph_type *ph);				\
a_attr void	a_prefix##insert(a_ph_type *ph, a_type *phn);		\
a_attr a_type	*a_prefix##remove_first(a_ph_type *ph);			\
a_attr a_type	*a_prefix##remove_any(a_ph_type *ph);			\
a_attr void	a_prefix##remove(a_ph_type *ph, a_type *phn);

/*
 * The ph_gen() macro generates a type-specific pairing heap implementation,
 * based on the above cpp macros.
 */
#define ph_gen(a_attr, a_prefix, a_ph_type, a_type, a_field, a_cmp)	\
a_attr void								\
a_prefix##new(a_ph_type *ph) {						\
	memset(ph, 0, sizeof(ph(a_type)));				\
}									\
a_attr bool								\
a_prefix##empty(a_ph_type *ph) {					\
	return (ph->ph_root == NULL);					\
}									\
a_attr a_type *								\
a_prefix##first(a_ph_type *ph) {					\
	if (ph->ph_root == NULL) {					\
		return NULL;						\
	}								\
	ph_merge_aux(a_type, a_field, ph, a_cmp);			\
	return ph->ph_root;						\
}									\
a_attr a_type *								\
a_prefix##any(a_ph_type *ph) {						\
	if (ph->ph_root == NULL) {					\
		return NULL;						\
	}								\
	a_type *aux = phn_next_get(a_type, a_field, ph->ph_root);	\
	if (aux != NULL) {						\
		return aux;						\
	}								\
	return ph->ph_root;						\
}									\
a_attr void								\
a_prefix##insert(a_ph_type *ph, a_type *phn) {				\
	memset(&phn->a_field, 0, sizeof(phn(a_type)));			\
									\
	/*								\
	 * Treat the root as an aux list during insertion, and lazily	\
	 * merge during a_prefix##remove_first().  For elements that	\
	 * are inserted, then removed via a_prefix##remove() before the	\
	 * aux list is ever processed, this makes insert/remove		\
	 * constant-time, whereas eager merging would make insert	\
	 * O(log n).							\
	 */								\
	if (ph->ph_root == NULL) {					\
		ph->ph_root = phn;					\
	} else {							\
		phn_next_set(a_type, a_field, phn, phn_next_get(a_type,	\
		    a_field, ph->ph_root));				\
		if (phn_next_get(a_type, a_field, ph->ph_root) !=	\
		    NULL) {						\
			phn_prev_set(a_type, a_field,			\
			    phn_next_get(a_type, a_field, ph->ph_root),	\
			    phn);					\
		}							\
		phn_prev_set(a_type, a_field, phn, ph->ph_root);	\
		phn_next_set(a_type, a_field, ph->ph_root, phn);	\
	}								\
}									\
a_attr a_type *								\
a_prefix##remove_first(a_ph_type *ph) {					\
	a_type *ret;							\
									\
	if (ph->ph_root == NULL) {					\
		return NULL;						\
	}								\
	ph_merge_aux(a_type, a_field, ph, a_cmp);			\
									\
	ret = ph->ph_root;						\
									\
	ph_merge_children(a_type, a_field, ph->ph_root, a_cmp,		\
	    ph->ph_root);						\
									\
	return ret;							\
}									\
a_attr a_type *								\
a_prefix##remove_any(a_ph_type *ph) {					\
	/*								\
	 * Remove the most recently inserted aux list element, or the	\
	 * root if the aux list is empty.  This has the effect of	\
	 * behaving as a LIFO (and insertion/removal is therefore	\
	 * constant-time) if a_prefix##[remove_]first() are never	\
	 * called.							\
	 */								\
	if (ph->ph_root == NULL) {					\
		return NULL;						\
	}								\
	a_type *ret = phn_next_get(a_type, a_field, ph->ph_root);	\
	if (ret != NULL) {						\
		a_type *aux = phn_next_get(a_type, a_field, ret);	\
		phn_next_set(a_type, a_field, ph->ph_root, aux);	\
		if (aux != NULL) {					\
			phn_prev_set(a_type, a_field, aux,		\
			    ph->ph_root);				\
		}							\
		return ret;						\
	}								\
	ret = ph->ph_root;						\
	ph_merge_children(a_type, a_field, ph->ph_root, a_cmp,		\
	    ph->ph_root);						\
	return ret;							\
}									\
a_attr void								\
a_prefix##remove(a_ph_type *ph, a_type *phn) {				\
	a_type *replace, *parent;					\
									\
	if (ph->ph_root == phn) {					\
		/*							\
		 * We can delete from aux list without merging it, but	\
		 * we need to merge if we are dealing with the root	\
		 * node and it has children.				\
		 */							\
		if (phn_lchild_get(a_type, a_field, phn) == NULL) {	\
			ph->ph_root = phn_next_get(a_type, a_field,	\
			    phn);					\
			if (ph->ph_root != NULL) {			\
				phn_prev_set(a_type, a_field,		\
				    ph->ph_root, NULL);			\
			}						\
			return;						\
		}							\
		ph_merge_aux(a_type, a_field, ph, a_cmp);		\
		if (ph->ph_root == phn) {				\
			ph_merge_children(a_type, a_field, ph->ph_root,	\
			    a_cmp, ph->ph_root);			\
			return;						\
		}							\
	}								\
									\
	/* Get parent (if phn is leftmost child) before mutating. */	\
	if ((parent = phn_prev_get(a_type, a_field, phn)) != NULL) {	\
		if (phn_lchild_get(a_type, a_field, parent) != phn) {	\
			parent = NULL;					\
		}							\
	}								\
	/* Find a possible replacement node, and link to parent. */	\
	ph_merge_children(a_type, a_field, phn, a_cmp, replace);	\
	/* Set next/prev for sibling linked list. */			\
	if (replace != NULL) {						\
		if (parent != NULL) {					\
			phn_prev_set(a_type, a_field, replace, parent);	\
			phn_lchild_set(a_type, a_field, parent,		\
			    replace);					\
		} else {						\
			phn_prev_set(a_type, a_field, replace,		\
			    phn_prev_get(a_type, a_field, phn));	\
			if (phn_prev_get(a_type, a_field, phn) !=	\
			    NULL) {					\
				phn_next_set(a_type, a_field,		\
				    phn_prev_get(a_type, a_field, phn),	\
				    replace);				\
			}						\
		}							\
		phn_next_set(a_type, a_field, replace,			\
		    phn_next_get(a_type, a_field, phn));		\
		if (phn_next_get(a_type, a_field, phn) != NULL) {	\
			phn_prev_set(a_type, a_field,			\
			    phn_next_get(a_type, a_field, phn),		\
			    replace);					\
		}							\
	} else {							\
		if (parent != NULL) {					\
			a_type *next = phn_next_get(a_type, a_field,	\
			    phn);					\
			phn_lchild_set(a_type, a_field, parent, next);	\
			if (next != NULL) {				\
				phn_prev_set(a_type, a_field, next,	\
				    parent);				\
			}						\
		} else {						\
			assert(phn_prev_get(a_type, a_field, phn) !=	\
			    NULL);					\
			phn_next_set(a_type, a_field,			\
			    phn_prev_get(a_type, a_field, phn),		\
			    phn_next_get(a_type, a_field, phn));	\
		}							\
		if (phn_next_get(a_type, a_field, phn) != NULL) {	\
			phn_prev_set(a_type, a_field,			\
			    phn_next_get(a_type, a_field, phn),		\
			    phn_prev_get(a_type, a_field, phn));	\
		}							\
	}								\
}

#endif /* PH_H_ */
