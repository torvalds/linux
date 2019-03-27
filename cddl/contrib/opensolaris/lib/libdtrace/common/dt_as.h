/*
 * CDDL HEADER START
 *
 * The contents of this file are subject to the terms of the
 * Common Development and Distribution License, Version 1.0 only
 * (the "License").  You may not use this file except in compliance
 * with the License.
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
 * Copyright 2005 Sun Microsystems, Inc.  All rights reserved.
 * Use is subject to license terms.
 */

#ifndef	_DT_AS_H
#define	_DT_AS_H

#pragma ident	"%Z%%M%	%I%	%E% SMI"

#include <sys/types.h>
#include <sys/dtrace.h>

#ifdef	__cplusplus
extern "C" {
#endif

typedef struct dt_irnode {
	uint_t di_label;		/* label number or DT_LBL_NONE */
	dif_instr_t di_instr;		/* instruction opcode */
	void *di_extern;		/* opcode-specific external reference */
	struct dt_irnode *di_next;	/* next instruction */
} dt_irnode_t;

#define	DT_LBL_NONE	0		/* no label on this instruction */

typedef struct dt_irlist {
	dt_irnode_t *dl_list;		/* pointer to first node in list */
	dt_irnode_t *dl_last;		/* pointer to last node in list */
	uint_t dl_len;			/* number of valid instructions */
	uint_t dl_label;		/* next label number to assign */
} dt_irlist_t;

extern void dt_irlist_create(dt_irlist_t *);
extern void dt_irlist_destroy(dt_irlist_t *);
extern void dt_irlist_append(dt_irlist_t *, dt_irnode_t *);
extern uint_t dt_irlist_label(dt_irlist_t *);

#ifdef	__cplusplus
}
#endif

#endif	/* _DT_AS_H */
