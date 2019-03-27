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
# Copyright 2009 Sun Microsystems, Inc.  All rights reserved.
# Use is subject to license terms.
#

#
# ASSERTION: Sched probe arguments should be valid. 
#

if [ $# != 1 ]; then
	echo expected one argument: '<'dtrace-path'>'
	exit 2
fi

#
# do not fail test in a domU
#
if [ ! -c /dev/xen/privcmd ]; then
	exit 0
fi

dtrace=$1
outf=/tmp/sched.args.$$

script()
{
	$dtrace -c '/usr/bin/sleep 10' -o $outf -qs /dev/stdin <<EOF
	xdt:sched::off-cpu,
	xdt:sched::on-cpu,
	xdt:sched::block,
	xdt:sched::sleep,
	xdt:sched::wake,
	xdt:sched::yield
	{
		/* print domid vcpu pcpu probename */
		printf("%d %d %d %s\n", arg0, arg1, \`xdt_curpcpu, probename);
	}
EOF
}

validate()
{
	/usr/bin/nawk '
	BEGIN {
		while (("/usr/sbin/xm vcpu-list" | getline)) {
			if ($1 != "Name") {
				domid = $2
				vcpu = $3

				vcpumap[domid, vcpu] = 1

				split($7, affinity, ",")
				for (i in affinity) {
					if (split(affinity[i], p, "-") > 1) {
						for (pcpu = p[1]; pcpu <= p[2];\
						    pcpu++) {
							cpumap[domid, vcpu,
							    pcpu] = 1
						}
					} else {
						cpumap[domid, vcpu,
						    affinity[i]] = 1
					}
				}
			}
		}
	}

	/^$/ { next }

	/wake/ {
		if (vcpumap[$1, $2]) {
			next
		} else {
			print "error: " $0
			exit 1
		}
	}

	{
		if (cpumap[$1, $2, "any"] || cpumap[$1, $2, $3]) {
			next
		} else {
			print "error: " $0
			exit 1
		}
	}
	' $outf
}

script
status=$?

if [ $status == 0 ]; then
	validate
	status=$?
fi

rm $outf
exit $status
