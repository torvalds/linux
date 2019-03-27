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
 * ASSERTION:
 *	Checks that buffer space for an END enabling is always reserved in a
 *	fill buffer.  This will fail because the size of the END enabling
 *	(64 bytes) exceeds the size of the buffer (32 bytes).
 *
 * SECTION: Buffers and Buffering/fill Policy;
 *	Buffers and Buffering/Buffer Sizes;
 *	Options and Tunables/bufpolicy;
 *	Options and Tunables/bufsize;
 *	Options and Tunables/strsize
 */

#pragma D option bufpolicy=fill
#pragma D option bufsize=32
#pragma D option strsize=64

BEGIN
{
	exit(0);
}

END
{
	trace(execname);
}
