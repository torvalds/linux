#
# CDDL HEADER START
#
# The contents of this file are subject to the terms of the
# Common Development and Distribution License (the "License").
# You may not use this file except in compliance with the License.
#
# You can obtain a copy of the license at usr/src/OPENSOLARIS.LICENSE
# or http://www.opensolaris.org/os/licensing.
# See the License for the specific language governing permissions
# and limitations under the License.
#
# When distributing Covered Code, include this CDDL HEADER in each
# file and include the License file at usr/src/OPENSOLARIS.LICENSE.
# If applicable, add the following below this CDDL HEADER, with the
# fields enclosed by brackets "[]" replaced with your own identifying
# information: Portions Copyright [yyyy] [name of copyright owner]
#
# CDDL HEADER END
#

#
# Copyright (c) 2012, Joyent, Inc. All rights reserved.
#

#
# First, make sure that we can successfully enable the io provider
#
if ! dtrace -P io -n BEGIN'{exit(0)}' > /dev/null 2>&1 ; then
	echo failed to enable io provider with full privs
	exit 1
fi

ppriv -s A=basic,dtrace_proc,dtrace_user $$

#
# Now make sure that we cannot enable the io provider with reduced privs
#
if ! dtrace -x errtags -P io -n BEGIN'{exit(1)}' 2>&1 | \
    grep D_PDESC_ZERO > /dev/null 2>&1 ; then
	echo successfully enabled the io provider with reduced privs
	exit 1
fi

#
# Keeping our reduced privs, we want to assure that we can see every provider
# that we think we should be able to see -- and that we can see curpsinfo
# state but can't otherwise see arguments.
#
/usr/sbin/dtrace -wq -Cs /dev/stdin <<EOF

int seen[string];
int err;

#define CANENABLE(provider) \
provider:::								\
/err == 0 && progenyof(\$pid) && !seen["provider"]/			\
{									\
	trace(arg0);							\
	printf("\nsuccessful trace of arg0 in %s:%s:%s:%s\n",		\
	    probeprov, probemod, probefunc, probename);			\
	exit(++err);							\
}									\
									\
provider:::								\
/progenyof(\$pid)/							\
{									\
	seen["provider"]++;						\
}									\
									\
provider:::								\
/progenyof(\$pid)/							\
{									\
	errstr = "provider";						\
	this->ignore = stringof(curpsinfo->pr_psargs);			\
	errstr = "";							\
}									\
									\
END									\
/err == 0 && !seen["provider"]/						\
{									\
	printf("no probes from provider\n");				\
	exit(++err);							\
}									\
									\
END									\
/err == 0/								\
{									\
	printf("saw %d probes from provider\n", seen["provider"]);	\
}

CANENABLE(proc)
CANENABLE(sched)
CANENABLE(vminfo)
CANENABLE(sysinfo)

BEGIN
{
	/*
	 * We'll kick off a system of a do-nothing command -- which should be
	 * enough to kick proc, sched, vminfo and sysinfo probes.
	 */
	system("echo > /dev/null");
}

ERROR
/err == 0 && errstr != ""/
{
	printf("fatal error: couldn't read curpsinfo->pr_psargs in ");
	printf("%s-provided probe\n", errstr);
	exit(++err);
}

proc:::exit
/progenyof(\$pid)/
{
	exit(0);
}

tick-10ms
/i++ > 500/
{
	printf("exit probe did not seem to fire\n");
	exit(++err);
}
EOF
