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
	this->dev = (struct dev_info *)alloca(sizeof (struct dev_info));
	this->minor1 =
	    (struct ddi_minor_data *)alloca(sizeof (struct ddi_minor_data));
	this->minor2 =
	    (struct ddi_minor_data *)alloca(sizeof (struct ddi_minor_data));
	this->minor3 =
	    (struct ddi_minor_data *)alloca(sizeof (struct ddi_minor_data));

	this->minor1->d_minor.dev = 0;
	this->minor1->next = this->minor2;

	this->minor2->d_minor.dev = 0;
	this->minor2->next = this->minor3;

	this->minor3->d_minor.dev = 0;
	this->minor3->next = this->minor1;

	this->dev->devi_minor = this->minor1;
	trace(ddi_pathname(this->dev, 1));
}

ERROR
/arg4 == DTRACEFLT_ILLOP/
{
	exit(0);
}

ERROR
/arg4 != DTRACEFLT_ILLOP/
{
	exit(1);
}
