#
# This file and its contents are supplied under the terms of the
# Common Development and Distribution License ("CDDL"), version 1.0.
# You may only use this file in accordance with the terms of version
# 1.0 of the CDDL.
#
# A full copy of the text of the CDDL should have accompanied this
# source.  A copy of the CDDL is also available via the Internet at
# http://www.illumos.org/license/CDDL.
#

#
# Copyright (c) 2015, Joyent, Inc. All rights reserved.
#

err=/tmp/err.$$

ppriv -s A=basic,dtrace_user $$

#
# When we lack dtrace_kernel, we expect to not be able to get at kernel memory
# via any subroutine or other vector.
#
#	trace(func((void *)&\`utsname)); }
/usr/sbin/dtrace -wq -Cs /dev/stdin 2> $err <<EOF

#define FAIL \
	printf("able to read kernel memory via %s!\n", badsubr); \
	exit(2);

#define CANTREAD1(func) \
    BEGIN { badsubr = "func()"; func((void *)&\`utsname); FAIL }

#define CANTREAD2(func, arg1) \
    BEGIN { badsubr = "func()"; func((void *)&\`utsname, arg1); FAIL }

#define CANTREAD2ARG1(func, arg0) \
    BEGIN { badsubr = "func() (arg1)"; func(arg0, (void *)&\`utsname); FAIL }

#define CANTREAD3(func, arg1, arg2) \
    BEGIN { badsubr = "func()"; func((void *)&\`utsname, arg1, arg2); FAIL }

CANTREAD1(mutex_owned)
CANTREAD1(mutex_owner)
CANTREAD1(mutex_type_adaptive)
CANTREAD1(mutex_type_spin)
CANTREAD1(rw_read_held)
CANTREAD1(rw_write_held)
CANTREAD1(rw_iswriter)
CANTREAD3(bcopy, alloca(1), 1)
CANTREAD1(msgsize)
CANTREAD1(msgdsize)
CANTREAD1(strlen)
CANTREAD2(strchr, '!')
CANTREAD2(strrchr, '!')
CANTREAD2(strstr, "doogle")
CANTREAD2ARG1(strstr, "doogle")
CANTREAD2(index, "bagnoogle")
CANTREAD2ARG1(index, "bagnoogle")
CANTREAD2(rindex, "bagnoogle")
CANTREAD2ARG1(rindex, "bagnoogle")
CANTREAD2(strtok, "doogle")
CANTREAD2ARG1(strtok, "doogle")
CANTREAD2(json, "doogle")
CANTREAD2ARG1(json, "doogle")
CANTREAD1(toupper)
CANTREAD1(tolower)
CANTREAD2(ddi_pathname, 1)
CANTREAD2(strjoin, "doogle")
CANTREAD2ARG1(strjoin, "doogle")
CANTREAD1(strtoll)
CANTREAD1(dirname)
CANTREAD1(basename)
CANTREAD1(cleanpath)

#if defined(__amd64)
CANTREAD3(copyout, uregs[R_R9], 1)
CANTREAD3(copyoutstr, uregs[R_R9], 1)
#else
#if defined(__i386)
CANTREAD3(copyout, uregs[R_ESP], 1)
CANTREAD3(copyoutstr, uregs[R_ESP], 1)
#endif
#endif

BEGIN
{
	exit(0);
}

ERROR
/arg4 != DTRACEFLT_KPRIV/
{
	printf("bad error code via %s (expected %d, found %d)\n",
	    badsubr, DTRACEFLT_KPRIV, arg4);
	exit(3);
}

ERROR
/arg4 == DTRACEFLT_KPRIV/
{
	printf("illegal kernel access properly prevented from %s\n", badsubr);
}
EOF

status=$?

if [[ $status -eq 1 ]]; then
	cat $err
fi

exit $status
