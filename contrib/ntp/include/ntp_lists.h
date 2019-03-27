/*
 * ntp_lists.h - linked lists common code
 *
 * SLIST: singly-linked lists
 * ==========================
 *
 * These macros implement a simple singly-linked list template.  Both
 * the listhead and per-entry next fields are declared as pointers to
 * the list entry struct type.  Initialization to NULL is typically
 * implicit (for globals and statics) or handled by zeroing of the
 * containing structure.
 *
 * The name of the next link field is passed as an argument to allow
 * membership in several lists at once using multiple next link fields.
 *
 * When possible, placing the link field first in the entry structure
 * allows slightly smaller code to be generated on some platforms.
 *
 * LINK_SLIST(listhead, pentry, nextlink)
 *	add entry at head
 *
 * LINK_TAIL_SLIST(listhead, pentry, nextlink, entrytype)
 *	add entry at tail.  This is O(n), if this is a common
 *	operation the FIFO template may be more appropriate.
 *
 * LINK_SORT_SLIST(listhead, pentry, beforecur, nextlink, entrytype)
 *	add entry in sorted order.  beforecur is an expression comparing
 *	pentry with the current list entry.  The current entry can be
 *	referenced within beforecur as L_S_S_CUR(), which is short for
 *	LINK_SORT_SLIST_CUR().  beforecur is nonzero if pentry sorts
 *	before L_S_S_CUR().
 *
 * UNLINK_HEAD_SLIST(punlinked, listhead, nextlink)
 *	unlink first entry and point punlinked to it, or set punlinked
 *	to NULL if the list is empty.
 *
 * UNLINK_SLIST(punlinked, listhead, ptounlink, nextlink, entrytype)
 *	unlink entry pointed to by ptounlink.  punlinked is set to NULL
 *	if the entry is not found on the list, otherwise it is set to
 *	ptounlink.
 *
 * UNLINK_EXPR_SLIST(punlinked, listhead, expr, nextlink, entrytype)
 *	unlink entry where expression expr is nonzero.  expr can refer
 *	to the entry being tested using UNLINK_EXPR_SLIST_CURRENT(),
 *	alias U_E_S_CUR().  See the implementation of UNLINK_SLIST()
 *	below for an example. U_E_S_CUR() is NULL iff the list is empty.
 *	punlinked is pointed to the removed entry or NULL if none
 *	satisfy expr.
 *
 * FIFO: singly-linked lists plus tail pointer
 * ===========================================
 *
 * This is the same as FreeBSD's sys/queue.h STAILQ -- a singly-linked
 * list implementation with tail-pointer maintenance, so that adding
 * at the tail for first-in, first-out access is O(1).
 *
 * DECL_FIFO_ANCHOR(entrytype)
 *	provides the type specification portion of the declaration for
 *	a variable to refer to a FIFO queue (similar to listhead).  The
 *	anchor contains the head and indirect tail pointers.  Example:
 *
 *		#include "ntp_lists.h"
 *
 *		typedef struct myentry_tag myentry;
 *		struct myentry_tag {
 *			myentry *next_link;
 *			...
 *		};
 *
 *		DECL_FIFO_ANCHOR(myentry) my_fifo;
 *
 *		void somefunc(myentry *pentry)
 *		{
 *			LINK_FIFO(my_fifo, pentry, next_link);
 *		}
 *
 *	If DECL_FIFO_ANCHOR is used with stack or heap storage, it
 *	should be initialized to NULL pointers using a = { NULL };
 *	initializer or memset.
 *
 * HEAD_FIFO(anchor)
 * TAIL_FIFO(anchor)
 *	Pointer to first/last entry, NULL if FIFO is empty.
 *
 * LINK_FIFO(anchor, pentry, nextlink)
 *	add entry at tail.
 *
 * UNLINK_FIFO(punlinked, anchor, nextlink)
 *	unlink head entry and point punlinked to it, or set punlinked
 *	to NULL if the list is empty.
 *
 * CONCAT_FIFO(q1, q2, nextlink)
 *	empty fifoq q2 moving its nodes to q1 following q1's existing
 *	nodes.
 *
 * DLIST: doubly-linked lists
 * ==========================
 *
 * Elements on DLISTs always have non-NULL forward and back links,
 * because both link chains are circular.  The beginning/end is marked
 * by the listhead, which is the same type as elements for simplicity.
 * An empty list's listhead has both links set to its own address.
 *
 *
 */
#ifndef NTP_LISTS_H
#define NTP_LISTS_H

#include "ntp_types.h"		/* TRUE and FALSE */
#include "ntp_assert.h"

#ifdef DEBUG
# define NTP_DEBUG_LISTS_H
#endif

/*
 * If list debugging is not enabled, save a little time by not clearing
 * an entry's link pointer when it is unlinked, as the stale pointer
 * is harmless as long as it is ignored when the entry is not in a
 * list.
 */
#ifndef NTP_DEBUG_LISTS_H
#define MAYBE_Z_LISTS(p)	do { } while (FALSE)
#else
#define MAYBE_Z_LISTS(p)	(p) = NULL
#endif

#define LINK_SLIST(listhead, pentry, nextlink)			\
do {								\
	(pentry)->nextlink = (listhead);			\
	(listhead) = (pentry);					\
} while (FALSE)

#define LINK_TAIL_SLIST(listhead, pentry, nextlink, entrytype)	\
do {								\
	entrytype **pptail;					\
								\
	pptail = &(listhead);					\
	while (*pptail != NULL)					\
		pptail = &((*pptail)->nextlink);		\
								\
	(pentry)->nextlink = NULL;				\
	*pptail = (pentry);					\
} while (FALSE)

#define LINK_SORT_SLIST_CURRENT()	(*ppentry)
#define	L_S_S_CUR()			LINK_SORT_SLIST_CURRENT()

#define LINK_SORT_SLIST(listhead, pentry, beforecur, nextlink,	\
			entrytype)				\
do {								\
	entrytype **ppentry;					\
								\
	ppentry = &(listhead);					\
	while (TRUE) {						\
		if (NULL == *ppentry || (beforecur)) {		\
			(pentry)->nextlink = *ppentry;		\
			*ppentry = (pentry);			\
			break;					\
		}						\
		ppentry = &((*ppentry)->nextlink);		\
		if (NULL == *ppentry) {				\
			(pentry)->nextlink = NULL;		\
			*ppentry = (pentry);			\
			break;					\
		}						\
	}							\
} while (FALSE)

#define UNLINK_HEAD_SLIST(punlinked, listhead, nextlink)	\
do {								\
	(punlinked) = (listhead);				\
	if (NULL != (punlinked)) {				\
		(listhead) = (punlinked)->nextlink;		\
		MAYBE_Z_LISTS((punlinked)->nextlink);		\
	}							\
} while (FALSE)

#define UNLINK_EXPR_SLIST_CURRENT()	(*ppentry)
#define	U_E_S_CUR()			UNLINK_EXPR_SLIST_CURRENT()

#define UNLINK_EXPR_SLIST(punlinked, listhead, expr, nextlink,	\
			  entrytype)				\
do {								\
	entrytype **ppentry;					\
								\
	ppentry = &(listhead);					\
								\
	while (!(expr))						\
		if (*ppentry != NULL &&				\
		    (*ppentry)->nextlink != NULL) {		\
			ppentry = &((*ppentry)->nextlink);	\
		} else {					\
			ppentry = NULL;				\
			break;					\
		}						\
								\
	if (ppentry != NULL) {					\
		(punlinked) = *ppentry;				\
		*ppentry = (punlinked)->nextlink;		\
		MAYBE_Z_LISTS((punlinked)->nextlink);		\
	} else {						\
		(punlinked) = NULL;				\
	}							\
} while (FALSE)

#define UNLINK_SLIST(punlinked, listhead, ptounlink, nextlink,	\
		     entrytype)					\
	UNLINK_EXPR_SLIST(punlinked, listhead, (ptounlink) ==	\
	    U_E_S_CUR(), nextlink, entrytype)

#define CHECK_SLIST(listhead, nextlink, entrytype)		\
do {								\
	entrytype *pentry;					\
								\
	for (pentry = (listhead);				\
	     pentry != NULL;					\
	     pentry = pentry->nextlink) {			\
		INSIST(pentry != pentry->nextlink);		\
		INSIST((listhead) != pentry->nextlink);		\
	}							\
} while (FALSE)

/*
 * FIFO
 */

#define DECL_FIFO_ANCHOR(entrytype)				\
struct {							\
	entrytype *	phead;	/* NULL if list empty */	\
	entrytype **	pptail;	/* NULL if list empty */	\
}

#define HEAD_FIFO(anchor)	((anchor).phead)
#define TAIL_FIFO(anchor)	((NULL == (anchor).pptail)	\
					? NULL			\
					: *((anchor).pptail))

/*
 * For DEBUG builds only, verify both or neither of the anchor pointers
 * are NULL with each operation.
 */
#if !defined(NTP_DEBUG_LISTS_H)
#define	CHECK_FIFO_CONSISTENCY(anchor)	do { } while (FALSE)
#else
#define	CHECK_FIFO_CONSISTENCY(anchor)				\
	check_gen_fifo_consistency(&(anchor))
void	check_gen_fifo_consistency(void *fifo);
#endif

/*
 * generic FIFO element used to access any FIFO where each element
 * begins with the link pointer
 */
typedef struct gen_node_tag gen_node;
struct gen_node_tag {
	gen_node *	link;
};

/* generic FIFO */
typedef DECL_FIFO_ANCHOR(gen_node) gen_fifo;


#define LINK_FIFO(anchor, pentry, nextlink)			\
do {								\
	CHECK_FIFO_CONSISTENCY(anchor);				\
								\
	(pentry)->nextlink = NULL;				\
	if (NULL != (anchor).pptail) {				\
		(*((anchor).pptail))->nextlink = (pentry);	\
		(anchor).pptail =				\
		    &(*((anchor).pptail))->nextlink;		\
	} else {						\
		(anchor).phead = (pentry);			\
		(anchor).pptail = &(anchor).phead;		\
	}							\
								\
	CHECK_FIFO_CONSISTENCY(anchor);				\
} while (FALSE)

#define UNLINK_FIFO(punlinked, anchor, nextlink)		\
do {								\
	CHECK_FIFO_CONSISTENCY(anchor);				\
								\
	(punlinked) = (anchor).phead;				\
	if (NULL != (punlinked)) {				\
		(anchor).phead = (punlinked)->nextlink;		\
		if (NULL == (anchor).phead)			\
			(anchor).pptail = NULL;			\
		else if ((anchor).pptail ==			\
			 &(punlinked)->nextlink)		\
			(anchor).pptail = &(anchor).phead;	\
		MAYBE_Z_LISTS((punlinked)->nextlink);		\
		CHECK_FIFO_CONSISTENCY(anchor);			\
	}							\
} while (FALSE)

#define UNLINK_MID_FIFO(punlinked, anchor, tounlink, nextlink,	\
			entrytype)				\
do {								\
	entrytype **ppentry;					\
								\
	CHECK_FIFO_CONSISTENCY(anchor);				\
								\
	ppentry = &(anchor).phead;				\
								\
	while ((tounlink) != *ppentry)				\
		if ((*ppentry)->nextlink != NULL) {		\
			ppentry = &((*ppentry)->nextlink);	\
		} else {					\
			ppentry = NULL;				\
			break;					\
		}						\
								\
	if (ppentry != NULL) {					\
		(punlinked) = *ppentry;				\
		*ppentry = (punlinked)->nextlink;		\
		if (NULL == *ppentry)				\
			(anchor).pptail = NULL;			\
		else if ((anchor).pptail ==			\
			 &(punlinked)->nextlink)		\
			(anchor).pptail = &(anchor).phead;	\
		MAYBE_Z_LISTS((punlinked)->nextlink);		\
		CHECK_FIFO_CONSISTENCY(anchor);			\
	} else {						\
		(punlinked) = NULL;				\
	}							\
} while (FALSE)

#define CONCAT_FIFO(f1, f2, nextlink)				\
do {								\
	CHECK_FIFO_CONSISTENCY(f1);				\
	CHECK_FIFO_CONSISTENCY(f2);				\
								\
	if ((f2).pptail != NULL) {				\
		if ((f1).pptail != NULL) {			\
			(*(f1).pptail)->nextlink = (f2).phead;	\
			if ((f2).pptail == &(f2).phead)		\
				(f1).pptail =			\
				    &(*(f1).pptail)->nextlink;	\
			else					\
				(f1).pptail = (f2).pptail;	\
			CHECK_FIFO_CONSISTENCY(f1);		\
		} else	{					\
			(f1) = (f2);				\
		}						\
		MAYBE_Z_LISTS((f2).phead);			\
		MAYBE_Z_LISTS((f2).pptail);			\
	}							\
} while (FALSE)

/*
 * DLIST
 */
#define DECL_DLIST_LINK(entrytype, link)			\
struct {							\
	entrytype *	b;					\
	entrytype *	f;					\
} link

#define INIT_DLIST(listhead, link)				\
do {								\
	(listhead).link.f = &(listhead);			\
	(listhead).link.b = &(listhead);			\
} while (FALSE)

#define HEAD_DLIST(listhead, link)				\
	(							\
		(&(listhead) != (listhead).link.f)		\
		    ? (listhead).link.f				\
		    : NULL					\
	)

#define TAIL_DLIST(listhead, link)				\
	(							\
		(&(listhead) != (listhead).link.b)		\
		    ? (listhead).link.b				\
		    : NULL					\
	)

#define NEXT_DLIST(listhead, entry, link)			\
	(							\
		(&(listhead) != (entry)->link.f)		\
		    ? (entry)->link.f				\
		    : NULL					\
	)

#define PREV_DLIST(listhead, entry, link)			\
	(							\
		(&(listhead) != (entry)->link.b)		\
		    ? (entry)->link.b				\
		    : NULL					\
	)

#define LINK_DLIST(listhead, pentry, link)			\
do {								\
	(pentry)->link.f = (listhead).link.f;			\
	(pentry)->link.b = &(listhead);				\
	(listhead).link.f->link.b = (pentry);			\
	(listhead).link.f = (pentry);				\
} while (FALSE)

#define LINK_TAIL_DLIST(listhead, pentry, link)			\
do {								\
	(pentry)->link.b = (listhead).link.b;			\
	(pentry)->link.f = &(listhead);				\
	(listhead).link.b->link.f = (pentry);			\
	(listhead).link.b = (pentry);				\
} while (FALSE)

#define UNLINK_DLIST(ptounlink, link)				\
do {								\
	(ptounlink)->link.b->link.f = (ptounlink)->link.f;	\
	(ptounlink)->link.f->link.b = (ptounlink)->link.b;	\
	MAYBE_Z_LISTS((ptounlink)->link.b);			\
	MAYBE_Z_LISTS((ptounlink)->link.f);			\
} while (FALSE)

#define ITER_DLIST_BEGIN(listhead, iter, link, entrytype)	\
{								\
	entrytype *i_dl_nextiter;				\
								\
	for ((iter) = (listhead).link.f;			\
	     (iter) != &(listhead)				\
	     && ((i_dl_nextiter = (iter)->link.f), TRUE);	\
	     (iter) = i_dl_nextiter) {
#define ITER_DLIST_END()					\
	}							\
}

#define REV_ITER_DLIST_BEGIN(listhead, iter, link, entrytype)	\
{								\
	entrytype *i_dl_nextiter;				\
								\
	for ((iter) = (listhead).link.b;			\
	     (iter) != &(listhead)				\
	     && ((i_dl_nextiter = (iter)->link.b), TRUE);	\
	     (iter) = i_dl_nextiter) {
#define REV_ITER_DLIST_END()					\
	}							\
}

#endif	/* NTP_LISTS_H */
