/*
 * CDDL HEADER START
 *
 * This file and its contents are supplied under the terms of the
 * Common Development and Distribution License ("CDDL"), version 1.0.
 * You may only use this file in accordance with the terms of version
 * 1.0 of the CDDL.
 *
 * A full copy of the text of the CDDL should have accompanied this
 * source.  A copy of the CDDL is also available via the Internet at
 * http://www.illumos.org/license/CDDL.
 *
 * CDDL HEADER END
 */

/*
 * Copyright (c) 2012 by Delphix. All rights reserved.
 */

/*
 * This test excercises the "remnant" handling of the temporal option.
 * At the end of one pass of retrieving and printing data from all CPUs,
 * some unprocessed data will remain, because its timestamp is after the
 * time covered by all CPUs' buffers.  This unprocessed data is
 * rearranged in a more space-efficient manner.  If this is done
 * incorrectly, an alignment error may occur.  To test this, we use a
 * high-frequency probe so that data will be recorded in subsequent
 * CPU's buffers after the first CPU's buffer is obtained.  The
 * combination of data traced here (a 8-byte value and a 4-byte value)
 * is effective to cause alignment problems with an incorrect
 * implementation.
 *
 * This test needs to be run on a multi-CPU system to be effective.
 */

#pragma D option quiet
#pragma D option temporal

profile-4997
{
	printf("%u %u", 1ULL, 2);
}

tick-1
/i++ == 10/
{
	exit(0);
}
