#ifndef JEMALLOC_INTERNAL_QL_H
#define JEMALLOC_INTERNAL_QL_H

#include "jemalloc/internal/qr.h"

/* List definitions. */
#define ql_head(a_type)							\
struct {								\
	a_type *qlh_first;						\
}

#define ql_head_initializer(a_head) {NULL}

#define ql_elm(a_type)	qr(a_type)

/* List functions. */
#define ql_new(a_head) do {						\
	(a_head)->qlh_first = NULL;					\
} while (0)

#define ql_elm_new(a_elm, a_field) qr_new((a_elm), a_field)

#define ql_first(a_head) ((a_head)->qlh_first)

#define ql_last(a_head, a_field)					\
	((ql_first(a_head) != NULL)					\
	    ? qr_prev(ql_first(a_head), a_field) : NULL)

#define ql_next(a_head, a_elm, a_field)					\
	((ql_last(a_head, a_field) != (a_elm))				\
	    ? qr_next((a_elm), a_field)	: NULL)

#define ql_prev(a_head, a_elm, a_field)					\
	((ql_first(a_head) != (a_elm)) ? qr_prev((a_elm), a_field)	\
				       : NULL)

#define ql_before_insert(a_head, a_qlelm, a_elm, a_field) do {		\
	qr_before_insert((a_qlelm), (a_elm), a_field);			\
	if (ql_first(a_head) == (a_qlelm)) {				\
		ql_first(a_head) = (a_elm);				\
	}								\
} while (0)

#define ql_after_insert(a_qlelm, a_elm, a_field)			\
	qr_after_insert((a_qlelm), (a_elm), a_field)

#define ql_head_insert(a_head, a_elm, a_field) do {			\
	if (ql_first(a_head) != NULL) {					\
		qr_before_insert(ql_first(a_head), (a_elm), a_field);	\
	}								\
	ql_first(a_head) = (a_elm);					\
} while (0)

#define ql_tail_insert(a_head, a_elm, a_field) do {			\
	if (ql_first(a_head) != NULL) {					\
		qr_before_insert(ql_first(a_head), (a_elm), a_field);	\
	}								\
	ql_first(a_head) = qr_next((a_elm), a_field);			\
} while (0)

#define ql_remove(a_head, a_elm, a_field) do {				\
	if (ql_first(a_head) == (a_elm)) {				\
		ql_first(a_head) = qr_next(ql_first(a_head), a_field);	\
	}								\
	if (ql_first(a_head) != (a_elm)) {				\
		qr_remove((a_elm), a_field);				\
	} else {							\
		ql_first(a_head) = NULL;				\
	}								\
} while (0)

#define ql_head_remove(a_head, a_type, a_field) do {			\
	a_type *t = ql_first(a_head);					\
	ql_remove((a_head), t, a_field);				\
} while (0)

#define ql_tail_remove(a_head, a_type, a_field) do {			\
	a_type *t = ql_last(a_head, a_field);				\
	ql_remove((a_head), t, a_field);				\
} while (0)

#define ql_foreach(a_var, a_head, a_field)				\
	qr_foreach((a_var), ql_first(a_head), a_field)

#define ql_reverse_foreach(a_var, a_head, a_field)			\
	qr_reverse_foreach((a_var), ql_first(a_head), a_field)

#endif /* JEMALLOC_INTERNAL_QL_H */
