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
# Copyright 2006 Sun Microsystems, Inc.  All rights reserved.
# Use is subject to license terms.
#
#ident	"%Z%%M%	%I%	%E% SMI"

#
# Affirmative test of less privileged user operation.  We do so by running
# this test case as root, then as a less privileged user.  The output should
# be exactly the same.
#

script()
{
	cat <<"EOF"

	BEGIN { trace("trace\n"); }
	BEGIN { printf("%s\n", "printf"); }
	BEGIN { printf("strlen(\"strlen\") = %d\n", strlen("strlen")); }
	BEGIN { x = alloca(10);
		bcopy("alloca\n", x, 7);
		trace(stringof(x)); }

	BEGIN { printf("index(\"index\", \"x\") = %d\n",
	    index("index", "x")); }
	BEGIN { printf("strchr(\"strchr\", \'t\') = %s\n",
	    strchr("strchr", 't')); }

	BEGIN { printf("strtok(\"strtok\", \"t\") = %s\n",
	    strtok("strtok", "t")); }
	BEGIN { printf("strtok(NULL, \"t\") = %s\n",
	    strtok(NULL, "t")); }
	BEGIN { printf("strtok(NULL, \"t\") = %s\n",
	    strtok(NULL, "t")); }
	BEGIN { printf("substr(\"substr\", 2, 2) = %s\n",
	    substr("substr", 2, 2)); }
	BEGIN { trace(strjoin("str", "join\n")); }
	BEGIN { trace(basename("dirname/basename")); trace("/"); }
	BEGIN { trace(dirname("dirname/basename")); }

	BEGIN { exit(0); }
	ERROR { exit(1); }
EOF
}

privout=/tmp/$$.priv_output
unprivout=/tmp/$$.unpriv_output

script | /usr/sbin/dtrace -q -s /dev/stdin > $privout
ppriv -s A=basic,dtrace_user $$
script | /usr/sbin/dtrace -q -s /dev/stdin > $unprivout

diff $privout $unprivout
res=$?

/bin/rm -f $privout $unprivout

exit $res
