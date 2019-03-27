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

#ifndef	_DT_BUF_H
#define	_DT_BUF_H

#pragma ident	"%Z%%M%	%I%	%E% SMI"

#ifdef	__cplusplus
extern "C" {
#endif

#include <dtrace.h>

typedef struct dt_buf {
	const char *dbu_name;	/* string name for debugging */
	uchar_t *dbu_buf;	/* buffer base address */
	uchar_t *dbu_ptr;	/* current buffer location */
	size_t dbu_len;		/* buffer size in bytes */
	int dbu_err;		/* errno value if error */
	int dbu_resizes;	/* number of resizes */
} dt_buf_t;

extern void dt_buf_create(dtrace_hdl_t *, dt_buf_t *, const char *, size_t);
extern void dt_buf_destroy(dtrace_hdl_t *, dt_buf_t *);
extern void dt_buf_reset(dtrace_hdl_t *, dt_buf_t *);

extern void dt_buf_write(dtrace_hdl_t *, dt_buf_t *,
    const void *, size_t, size_t);

extern void dt_buf_concat(dtrace_hdl_t *, dt_buf_t *,
    const dt_buf_t *, size_t);

extern size_t dt_buf_offset(const dt_buf_t *, size_t);
extern size_t dt_buf_len(const dt_buf_t *);

extern int dt_buf_error(const dt_buf_t *);
extern void *dt_buf_ptr(const dt_buf_t *);

extern void *dt_buf_claim(dtrace_hdl_t *, dt_buf_t *);

#ifdef	__cplusplus
}
#endif

#endif	/* _DT_BUF_H */
