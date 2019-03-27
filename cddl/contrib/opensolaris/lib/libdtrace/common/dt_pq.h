/*
 * CDDL HEADER START
 *
 * This file and its contents are supplied under the terms of the
 * Common Development and Distribution License ("CDDL"), version 1.0.
 * You may only use this file in accordance with the terms of version
 * 1.0 of the CDDL.
 *
 * A full copy of the text of the CDDL should have accompanied this
 * source.  A copy of the CDDL is also available via the Internet at
 * http://www.illumos.org/license/CDDL.
 *
 * CDDL HEADER END
 */

/*
 * Copyright (c) 2012 by Delphix. All rights reserved.
 */

#ifndef	_DT_PQ_H
#define	_DT_PQ_H

#include <dtrace.h>

#ifdef	__cplusplus
extern "C" {
#endif

typedef uint64_t (*dt_pq_value_f)(void *, void *);

typedef struct dt_pq {
	dtrace_hdl_t *dtpq_hdl;		/* dtrace handle */
	void **dtpq_items;		/* array of elements */
	uint_t dtpq_size;		/* count of allocated elements */
	uint_t dtpq_last;		/* next free slot */
	dt_pq_value_f dtpq_value;	/* callback to get the value */
	void *dtpq_arg;			/* callback argument */
} dt_pq_t;

extern dt_pq_t *dt_pq_init(dtrace_hdl_t *, uint_t size, dt_pq_value_f, void *);
extern void dt_pq_fini(dt_pq_t *);

extern void dt_pq_insert(dt_pq_t *, void *);
extern void *dt_pq_pop(dt_pq_t *);
extern void *dt_pq_walk(dt_pq_t *, uint_t *);

#ifdef	__cplusplus
}
#endif

#endif	/* _DT_PQ_H */
