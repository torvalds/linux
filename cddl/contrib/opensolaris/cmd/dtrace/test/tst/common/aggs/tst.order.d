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

#pragma D option quiet

BEGIN
{
	@[8] = sum(1);
	@[6] = sum(1);
	@[7] = sum(1);
	@[5] = sum(1);
	@[3] = sum(1);
	@[0] = sum(1);
	@[9] = sum(1);

	@tour["Ghent"] = sum(1);
	@tour["Berlin"] = sum(1);
	@tour["London"] = sum(1);
	@tour["Dublin"] = sum(1);
	@tour["Shanghai"] = sum(1);
	@tour["Zurich"] = sum(1);
	@tour["Regina"] = sum(1);
	@tour["Winnipeg"] = sum(1);
	@tour["Edmonton"] = sum(1);
	@tour["Calgary"] = sum(1);

	@ate[8, "Rice"] = sum(1);
	@ate[8, "Oatmeal"] = sum(1);
	@ate[8, "Barley"] = sum(1);
	@ate[8, "Carrots"] = sum(1);
	@ate[8, "Sweet potato"] = sum(1);
	@ate[8, "Asparagus"] = sum(1);
	@ate[8, "Squash"] = sum(1);

	@chars['a'] = sum(1);
	@chars['s'] = sum(1);
	@chars['d'] = sum(1);
	@chars['f'] = sum(1);

	printa("%d\n", @);
	printf("\n");

	printa("%s\n", @tour);
	printf("\n");

	printa("%d %s\n", @ate);
	printf("\n");

	printa("%c\n", @chars);
	printf("\n");

	exit(0);
}
