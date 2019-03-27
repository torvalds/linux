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
 * Copyright 2009 Sun Microsystems, Inc.  All rights reserved.
 * Use is subject to license terms.
 */

/*
 * Test that we can successfully enable a probe using a generic event.
 * Currently, all platforms implement 'PAPI_tot_ins' so we'll use that.
 * Note that this test will fail if the system under test does not
 * implement that event.
 *
 * This test will fail if:
 *	1) The system under test does not define the 'PAPI_tot_ins' event.
 */

#pragma D option quiet
#pragma D option bufsize=128k

cpc:::PAPI_tot_ins-all-10000
{
	@[probename] = count();
}

tick-1s
/n++ > 10/
{
	exit(0);
}
