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
#	Pass 10 arguments and try to print them.
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

dfilename=/var/tmp/$bname.$$


## Create .d file
##########################################################################
cat > $dfilename <<-EOF
#!$dtrace -qs


BEGIN
{
	printf("%d %d %d %d %d %d %d %d %d %d", \$1, \$2, \$3, \$4, \$5, \$6,
		\$7, \$8, \$9, \$10);
	exit(0);
}
EOF
##########################################################################


#Call dtrace -C -s <.d>

chmod 555 $dfilename


output=`$dfilename 1 2 3 4 5 6 7 8 9 10 2>/dev/null`

if [ $? -ne 0 ]; then
	print -u2 "Error in executing $dfilename"
	exit 1
fi

set -A outarray $output

if [[ ${outarray[0]} != 1 || ${outarray[1]} != 2 || ${outarray[2]} != 3 || \
	${outarray[3]} != 4 || ${outarray[4]} != 5 || ${outarray[5]} != 6 || \
	${outarray[6]} != 7 || ${outarray[7]} != 8 || ${outarray[8]} != 9 || \
	${outarray[9]} != 10 ]]; then
	print -u2 "Error in output by $dfilename"
	exit 1
fi

/bin/rm -f $dfilename
exit 0

