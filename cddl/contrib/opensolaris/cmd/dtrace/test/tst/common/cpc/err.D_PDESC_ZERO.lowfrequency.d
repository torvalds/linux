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
 * Test to check that attempting to enable a valid event with a frequency
 * lower than the default platform limit will fail.
 *
 * This test will fail if:
 *	1) The system under test does not define the 'PAPI_tot_ins' event.
 *	2) The 'dcpc-min-overflow' variable in dcpc.conf has been modified.
 */

#pragma D option quiet

BEGIN
{
	exit(0);
}

cpc:::PAPI_tot_ins-all-100
{
}
