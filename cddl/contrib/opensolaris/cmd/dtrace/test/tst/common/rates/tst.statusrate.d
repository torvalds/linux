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
 *	Positive test of statusrate option.
 *
 * SECTION: Options and Tunables/statusrate
 */

/*
 * Tests the statusrate option, by checking that the time delta between
 * exit() and END is at least as long as mandated by the statusrate.
 */

#pragma D option statusrate=10sec

inline uint64_t NANOSEC = 1000000000;

tick-1sec
/n++ > 5/
{
	exit(2);
	ts = timestamp;
}

END
/(this->delta = timestamp - ts) > 2 * NANOSEC/
{
	exit(0);
}

END
/this->delta <= 2 * NANOSEC/
{
	printf("delta between exit() and END (%u nanos) too small",
	    this->delta);
	exit(1);
}

END
/this->delta > 20 * NANOSEC/
{
	printf("delta between exit() and END (%u nanos) too large",
	    this->delta);
	exit(1);
}
