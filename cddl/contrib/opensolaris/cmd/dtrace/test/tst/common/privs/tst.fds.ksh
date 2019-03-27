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

tmpin=/tmp/tst.fds.$$.d
tmpout1=/tmp/tst.fds.$$.out1
tmpout2=/tmp/tst.fds.$$.out2

cat > $tmpin <<EOF
#define DUMPFIELD(fd, fmt, field) \
	errmsg = "could not dump field"; \
	printf("%d: field =fmt\n", fd, fds[fd].field);

/*
 * Note that we are explicitly not looking at fi_mount -- it (by design) does
 * not work if not running with kernel permissions.
 */
#define DUMP(fd)	\
	DUMPFIELD(fd, %s, fi_name); \
	DUMPFIELD(fd, %s, fi_dirname); \
	DUMPFIELD(fd, %s, fi_pathname); \
	DUMPFIELD(fd, %d, fi_offset); \
	DUMPFIELD(fd, %s, fi_fs); \
	DUMPFIELD(fd, %o, fi_oflags);

BEGIN
{
	DUMP(0);
	DUMP(1);
	DUMP(2);
	DUMP(3);
	DUMP(4);
	exit(0);
}

ERROR
{
	printf("error: %s\n", errmsg);
	exit(1);
}
EOF

#
# First, with all privs
#
/usr/sbin/dtrace -q -Cs /dev/stdin < $tmpin > $tmpout2
mv $tmpout2 $tmpout1

#
# And now with only dtrace_proc and dtrace_user -- the output should be
# identical.
#
ppriv -s A=basic,dtrace_proc,dtrace_user $$

/usr/sbin/dtrace -q -Cs /dev/stdin < $tmpin > $tmpout2

echo ">>> $tmpout1"
cat $tmpout1

echo ">>> $tmpout2"
cat $tmpout2

rval=0

if ! cmp $tmpout1 $tmpout2 ; then
	rval=1
fi

rm $tmpout1 $tmpout2 $tmpin
exit $rval
