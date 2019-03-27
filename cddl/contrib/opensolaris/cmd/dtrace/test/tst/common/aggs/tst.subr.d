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
 * Copyright (c) 2012, Joyent, Inc.  All rights reserved.
 */

#pragma ident	"%Z%%M%	%I%	%E% SMI"


#define INTFUNC(x)			\
	BEGIN				\
	/*DSTYLED*/			\
	{				\
		subr++;			\
		@[(long)x] = sum(1);	\
	/*DSTYLED*/			\
	}

#define STRFUNC(x)			\
	BEGIN				\
	/*DSTYLED*/			\
	{				\
		subr++;			\
		@str[x] = sum(1);	\
	/*DSTYLED*/			\
	}

#define VOIDFUNC(x)			\
	BEGIN				\
	/*DSTYLED*/			\
	{				\
		subr++;			\
	/*DSTYLED*/			\
	}

INTFUNC(rand())
INTFUNC(mutex_owned(&`cpu_lock))
INTFUNC(mutex_owner(&`cpu_lock))
INTFUNC(mutex_type_adaptive(&`cpu_lock))
INTFUNC(mutex_type_spin(&`cpu_lock))
INTFUNC(rw_read_held(&`vfssw_lock))
INTFUNC(rw_write_held(&`vfssw_lock))
INTFUNC(rw_iswriter(&`vfssw_lock))
INTFUNC(copyin(NULL, 1))
STRFUNC(copyinstr(NULL, 1))
INTFUNC(speculation())
INTFUNC(progenyof($pid))
INTFUNC(strlen("fooey"))
VOIDFUNC(copyout)
VOIDFUNC(copyoutstr)
INTFUNC(alloca(10))
VOIDFUNC(bcopy)
VOIDFUNC(copyinto)
INTFUNC(msgdsize(NULL))
INTFUNC(msgsize(NULL))
INTFUNC(getmajor(0))
INTFUNC(getminor(0))
STRFUNC(ddi_pathname(NULL, 0))
STRFUNC(strjoin("foo", "bar"))
STRFUNC(lltostr(12373))
STRFUNC(basename("/var/crash/systemtap"))
STRFUNC(dirname("/var/crash/systemtap"))
STRFUNC(cleanpath("/var/crash/systemtap"))
STRFUNC(strchr("The SystemTap, The.", 't'))
STRFUNC(strrchr("The SystemTap, The.", 't'))
STRFUNC(strstr("The SystemTap, The.", "The"))
STRFUNC(strtok("The SystemTap, The.", "T"))
STRFUNC(substr("The SystemTap, The.", 0))
INTFUNC(index("The SystemTap, The.", "The"))
INTFUNC(rindex("The SystemTap, The.", "The"))
INTFUNC(htons(0x1234))
INTFUNC(htonl(0x12345678))
INTFUNC(htonll(0x1234567890abcdefL))
INTFUNC(ntohs(0x1234))
INTFUNC(ntohl(0x12345678))
INTFUNC(ntohll(0x1234567890abcdefL))
STRFUNC(inet_ntoa((ipaddr_t *)alloca(sizeof (ipaddr_t))))
STRFUNC(inet_ntoa6((in6_addr_t *)alloca(sizeof (in6_addr_t))))
STRFUNC(inet_ntop(AF_INET, (void *)alloca(sizeof (ipaddr_t))))
INTFUNC(getf(0))
INTFUNC(strtoll("0x12EE5D5", 16))
STRFUNC(json("{\"systemtap\": false}", "systemtap"))

BEGIN
/subr == DIF_SUBR_MAX + 1/
{
	exit(0);
}

BEGIN
{
	printf("found %d subroutines, expected %d\n", subr, DIF_SUBR_MAX + 1);
	exit(1);
}
