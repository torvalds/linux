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
 * Copyright (c) 1994 by Sun Microsystems, Inc.
 */

/*
 * note.h:	interface for annotating source with info for tools
 *
 * NOTE is the default interface, but if the identifier NOTE is in use for
 * some other purpose, you may prepare a similar header file using your own
 * identifier, mapping that identifier to _NOTE.  Also, exported header
 * files should *not* use NOTE, since the name may already be in use in
 * a program's namespace.  Rather, exported header files should include
 * sys/note.h directly and use _NOTE.  For consistency, all kernel source
 * should use _NOTE.
 */

#ifndef	_NOTE_H
#define	_NOTE_H

#pragma ident	"%Z%%M%	%I%	%E% SMI"

#include <sys/note.h>

#ifdef	__cplusplus
extern "C" {
#endif

#define	NOTE _NOTE

#ifdef	__cplusplus
}
#endif

#endif	/* _NOTE_H */
