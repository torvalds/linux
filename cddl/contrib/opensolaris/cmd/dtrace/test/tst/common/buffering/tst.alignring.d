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
 *   Positive test for ring buffer policy.
 *
 * SECTION: Buffers and Buffering/ring Policy;
 *	Buffers and Buffering/Buffer Sizes;
 *	Options and Tunables/bufsize;
 *	Options and Tunables/bufpolicy
 */

#pragma D option bufpolicy=ring
#pragma D option bufsize=4k

profile:::profile-1009hz
{
	printf("%x %x\n", (int)0xaaaa, (int)0xbbbb);
}

profile:::profile-1237hz
{
	printf("%x %x %x %x %x %x\n",
	    (int)0xcccc,
	    (int)0xdddd,
	    (int)0xeeee,
	    (int)0xffff,
	    (int)0xabab,
	    (int)0xacac);
	printf("%x %x\n",
	    (uint64_t)0xaabbaabbaabbaabb,
	    (int)0xadad);
}

profile:::profile-1789hz
{
	printf("%x %x %x %x %x\n",
	    (int)0xaeae,
	    (int)0xafaf,
	    (unsigned char)0xaa,
	    (int)0xbcbc,
	    (int)0xbdbd);
}

profile-1543hz
{}

profile-1361hz
{}

tick-1sec
/i++ >= 10/
{
	exit(0);
}
