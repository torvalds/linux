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

if [ $# != 1 ]; then
	echo expected one argument: '<'dtrace-path'>'
	exit 2
fi

dtrace=$1
CFLAGS=

doit()
{
	file=$1
	ofile=$2
	errfile=$3
	cfile=${TMPDIR:-/tmp}/inc.$$.$file.c
	cofile=${TMPDIR:-/tmp}/inc.$$.$file
	cat > $cfile <<EOF
#include <sys/$file>
void
main()
{}
EOF
	if cc $CFLAGS -o $cofile $cfile >/dev/null 2>&1; then
		$dtrace -xerrtags -C -s /dev/stdin \
		    >/dev/null 2>$errfile <<EOF
#include <sys/$file>
BEGIN
{
	exit(0);
}
EOF
		if [ $? -ne 0 ]; then
			echo $inc failed: `cat $errfile | head -1` > $ofile
		else
			echo $inc succeeded > $ofile
		fi
		rm -f $errfile
	fi

	rm -f $cofile $cfile 2>/dev/null
}

concurrency=`psrinfo | wc -l`
let concurrency=concurrency*4
let i=0

files=/usr/include/sys/*.h

#
# There are a few files in /usr/include/sys that are known to be bad -- usually
# because they include static globals (!) or function bodies (!!) in the header
# file.  Hopefully these remain sufficiently few that the O(#files * #badfiles)
# algorithm, below, doesn't become a problem.  (And yes, writing scripts in
# something other than ksh1888 would probably be a good idea.)  If this script
# becomes a problem, kindly fix it by reducing the number of bad files!  (That
# is, fix it by fixing the broken file, not the broken script.)
#
badfiles="ctype.h eri_msg.h ser_sync.h sbpro.h neti.h hook_event.h \
    bootconf.h bootstat.h dtrace.h dumphdr.h exacct_impl.h fasttrap.h \
    kobj.h kobj_impl.h ksyms.h lockstat.h smedia.h stat.h utsname.h"

for inc in $files; do
	file=`basename $inc`
	for bad in $badfiles; do
		if [ "$file" = "$bad" ]; then
			continue 2 
		fi
	done

	ofile=${TMPDIR:-/tmp}/inc.$file.$$.out
	errfile=${TMPDIR:-/tmp}/inc.$file.$$.err
	doit $file $ofile $errfile &
	let i=i+1

	if [ $i -eq $concurrency ]; then
		#
		# This isn't optimal -- it creates a highly fluctuating load
		# as we wait for all work to complete -- but it's an easy
		# way of parallelizing work.
		#
		wait
		let i=0
	fi
done

wait

bigofile=${TMPDIR:-/tmp}/inc.$$.out

for inc in $files; do
	file=`basename $inc`
	ofile=${TMPDIR:-/tmp}/inc.$file.$$.out

	if [ -f $ofile ]; then
		cat $ofile >> $bigofile
		rm $ofile
	fi
done

status=$(grep "failed:" $bigofile | wc -l)
cat $bigofile
rm -f $bigofile
exit $status
