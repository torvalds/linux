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
 * Copyright (c) 2013 by Delphix. All rights reserved.
 */

provider zfs {
	probe probe0(char *probename);
	probe probe1(char *probename, unsigned long arg1);
	probe probe2(char *probename, unsigned long arg1, unsigned long arg2);
	probe probe3(char *probename, unsigned long arg1, unsigned long arg2,
	    unsigned long arg3);
	probe probe4(char *probename, unsigned long arg1, unsigned long arg2,
	    unsigned long arg3, unsigned long arg4);

	probe set__error(int err);
};

#pragma D attributes Evolving/Evolving/ISA provider zfs provider
#pragma D attributes Private/Private/Unknown provider zfs module
#pragma D attributes Private/Private/Unknown provider zfs function
#pragma D attributes Evolving/Evolving/ISA provider zfs name
#pragma D attributes Evolving/Evolving/ISA provider zfs args
