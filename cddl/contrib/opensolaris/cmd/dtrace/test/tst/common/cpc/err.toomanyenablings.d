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
 * Test to check that attempting to enable too many probes will fail.
 *
 * This test will fail if:
 *	1) We ever execute on a platform which is capable of programming 10
 *	'PAPI_tot_ins' events simultaneously (which no current platforms are
 *	capable of doing).
 *	2) The system under test does not define the 'PAPI_tot_ins' event.
 */

#pragma D option quiet

BEGIN
{
	exit(0);
}

cpc:::PAPI_tot_ins-all-10000,
cpc:::PAPI_tot_ins-all-10001,
cpc:::PAPI_tot_ins-all-10002,
cpc:::PAPI_tot_ins-all-10003,
cpc:::PAPI_tot_ins-all-10004,
cpc:::PAPI_tot_ins-all-10005,
cpc:::PAPI_tot_ins-all-10006,
cpc:::PAPI_tot_ins-all-10007,
cpc:::PAPI_tot_ins-all-10008,
cpc:::PAPI_tot_ins-all-10009
{
}
