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
 * ASSERTION: Test network byte-ordering routines.
 */

#if defined(__amd64__) || defined(__i386__)
#define _LITTLE_ENDIAN
#endif

BEGIN
{
	before[0] = 0x1122LL;
	before[1] = 0x11223344LL;
	before[2] = 0x1122334455667788LL;

#ifdef _LITTLE_ENDIAN
	after[0] = 0x2211LL;
	after[1] = 0x44332211LL;
	after[2] = 0x8877665544332211LL;
#else
	after[0] = 0x1122LL;
	after[1] = 0x11223344LL;
	after[2] = 0x1122334455667788LL;
#endif
}

BEGIN
/after[0] != htons(before[0])/
{
	printf("%x rather than %x", htons(before[0]), after[0]);
	exit(1);
}

BEGIN
/after[0] != ntohs(before[0])/
{
	printf("%x rather than %x", ntohs(before[0]), after[0]);
	exit(1);
}

BEGIN
/after[1] != htonl(before[1])/
{
	printf("%x rather than %x", htonl(before[1]), after[1]);
	exit(1);
}

BEGIN
/after[1] != ntohl(before[1])/
{
	printf("%x rather than %x", ntohl(before[1]), after[1]);
	exit(1);
}

BEGIN
/after[2] != htonll(before[2])/
{
	printf("%x rather than %x", htonll(before[2]), after[2]);
	exit(1);
}

BEGIN
/after[2] != ntohll(before[2])/
{
	printf("%x rather than %x", ntohll(before[2]), after[2]);
	exit(1);
}

BEGIN
{
	exit(0);
}
