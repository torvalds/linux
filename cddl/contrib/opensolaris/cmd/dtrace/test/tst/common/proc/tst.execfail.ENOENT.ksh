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
# Copyright 2007 Sun Microsystems, Inc.  All rights reserved.
# Use is subject to license terms.
#
# ident	"%Z%%M%	%I%	%E% SMI"

#
# This script is identical to tst.execfail.ksh -- but it additionally checks
# that errno is set to ENOENT in the case that an interpreter can't be
# found.
#
# If this fails, the script will run indefinitely; it relies on the harness
# to time it out.
#
script()
{
	$dtrace -s /dev/stdin <<EOF
	proc:::exec
	/curpsinfo->pr_ppid == $child && args[0] == "$badexec"/
	{
		self->exec = 1;
	}

	proc:::exec-failure
	/self->exec && args[0] == ENOENT/
	{
		exit(0);
	}
EOF
}

sleeper()
{
	while true; do
		/bin/sleep 1
		$badexec
	done
}

if [ $# != 1 ]; then
	echo expected one argument: '<'dtrace-path'>'
	exit 2
fi

badexec=/tmp/execfail.ENOENT.ksh.$$
dtrace=$1

cat > $badexec <<EOF
#!/this_is_a_bogus_interpreter
EOF

chmod +x $badexec

sleeper &
child=$!

script
status=$?

kill $child
rm $badexec

exit $status
