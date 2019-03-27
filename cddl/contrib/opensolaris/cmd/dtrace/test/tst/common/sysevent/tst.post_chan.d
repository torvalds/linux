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
 * Copyright 2007 Sun Microsystems, Inc.  All rights reserved.
 * Use is subject to license terms.
 */

#pragma ident	"%Z%%M%	%I%	%E% SMI"

#pragma D option quiet

BEGIN
{
	$1 + 0;	/* make sure pid is referenced */

	/*
	 * Wait no more than five seconds for the sysevent to be posted
	 */
	timeout = timestamp + 5000000000;
}

sysevent:::post
/args[0]->ec_name != "channel_dtest"/
{
	printf("unexpected channel name (%s)\n", args[0]->ec_name);
	exit(1);
}

sysevent:::post
/strstr(args[1]->se_publisher, "vendor_dtest") == NULL/
{
	printf("missing vendor name from publisher (%s)\n",
	    args[1]->se_publisher);
	exit(1);
}

sysevent:::post
/strstr(args[1]->se_publisher, "publisher_dtest") == NULL/
{
	printf("missing publisher name from publisher (%s)\n",
	    args[1]->se_publisher);
	exit(1);
}

sysevent:::post
/args[1]->se_class != "class_dtest"/
{
	printf("unexpected class name (%s)\n", args[1]->se_class);
	exit(1);
}

sysevent:::post
/args[1]->se_subclass != "subclass_dtest"/
{
	printf("unexpected subclass name (%s)\n", args[1]->se_subclass);
	exit(1);
}

sysevent:::post
{
	exit(0);
}

profile:::tick-8
/timestamp > timeout/
{
	printf("timed out\n");
	exit(1);
}
