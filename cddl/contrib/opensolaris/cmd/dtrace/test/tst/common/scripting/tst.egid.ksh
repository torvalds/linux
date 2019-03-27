#!/bin/ksh -p
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

############################################################################
# ASSERTION:
#
#	To verify egid of current process
#
#
# SECTION: Scripting
#
############################################################################

if [ $# != 1 ]; then
	echo expected one argument: '<'dtrace-path'>'
	exit 2
fi

dtrace=$1
bname=`basename $0`
dfilename=/var/tmp/$bname.$$.d

## Create .d file
##########################################################################
cat > $dfilename <<-EOF
#!$dtrace -qs


BEGIN
/\$egid != \$1/
{
	exit(1);
}

BEGIN
/\$egid == \$1/
{
	exit(0);
}
EOF
##########################################################################


#chmod 555 the .d file

chmod 555 $dfilename >/dev/null 2>&1
if [ $? -ne 0 ]; then
	print -u2 "chmod $dfilename failed"
	exit 1
fi

#Get the groupid of the calling process using ps

groupid=`ps -x -o pid,egid | grep "$$ " | awk '{print $2}' 2>/dev/null`
if [ $? -ne 0 ]; then
	print -u2 "unable to get uid of the current process with pid = $$"
	exit 1
fi

#Pass groupid as argument to .d file
$dfilename $groupid >/dev/null 2>&1

if [ $? -ne 0 ]; then
	print -u2 "Error in executing $dfilename"
	exit 1
fi

#Cleanup leftovers

rm -f $dfilename
exit 0
