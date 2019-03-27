/*-
 * Copyright (c) 1992, 1993, 1994
 *	The Regents of the University of California.  All rights reserved.
 * Copyright (c) 1992, 1993, 1994, 1995, 1996
 *	Keith Bostic.  All rights reserved.
 *
 * See the LICENSE file for redistribution information.
 *
 *	@(#)log.h	10.2 (Berkeley) 3/6/96
 */

#define	LOG_NOTYPE		0
#define	LOG_CURSOR_INIT		1
#define	LOG_CURSOR_END		2
#define	LOG_LINE_APPEND		3
#define	LOG_LINE_DELETE		4
#define	LOG_LINE_INSERT		5
#define	LOG_LINE_RESET_F	6
#define	LOG_LINE_RESET_B	7
#define	LOG_MARK		8
