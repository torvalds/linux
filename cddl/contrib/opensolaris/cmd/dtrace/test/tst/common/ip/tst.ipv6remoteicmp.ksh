#!/usr/bin/env ksh93
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
# Copyright 2008 Sun Microsystems, Inc.  All rights reserved.
# Use is subject to license terms.
#
#pragma ident	"%Z%%M%	%I%	%E% SMI"

#
# Test ip:::{send,receive} of IPv6 ICMP to a remote host.  This test is
# skipped if there are no physical interfaces configured with IPv6, or no
# other IPv6 hosts are reachable.
#
# This may fail due to:
#
# 1. A change to the ip stack breaking expected probe behavior,
#    which is the reason we are testing.
# 2. An unrelated ICMPv6 between these hosts was traced by accident.
#

if (( $# != 1 )); then
	print -u2 "expected one argument: <dtrace-path>"
	exit 2
fi

dtrace=$1
getaddr=./get.ipv6remote.pl

if [[ ! -x $getaddr ]]; then
	print -u2 "could not find or execute sub program: $getaddr"
	exit 3
fi
$getaddr | read source dest
if (( $? != 0 )); then
	print -nu2 "Could not find a local IPv6 interface and a remote IPv6 "
	print -u2 "host.  Aborting test.\n"
	print -nu2 "For this test to continue, a \"ping -ns -A inet6 FF02::1\" "
	print -u2 "must respond with a\nremote IPv6 host."
	exit 3
fi

#
# Shake loose any ICMPv6 Neighbor advertisement messages before tracing.
#
/sbin/ping $dest 3 > /dev/null 2>&1

$dtrace -c "/sbin/ping $dest 3" -qs /dev/stdin <<EOF | \
    grep -v 'is alive' | sort -n
ip:::send
/args[2]->ip_saddr == "$source" && args[2]->ip_daddr == "$dest" &&
    args[5]->ipv6_nexthdr == IPPROTO_ICMPV6/
{
	printf("1 ip:::send    (");
	printf("args[2]: %d %d, ", args[2]->ip_ver, args[2]->ip_plength);
	printf("args[5]: %d %d %d)\n",
	    args[5]->ipv6_ver, args[5]->ipv6_tclass, args[5]->ipv6_plen);
}

ip:::receive
/args[2]->ip_saddr == "$dest" && args[2]->ip_daddr == "$source" &&
    args[5]->ipv6_nexthdr == IPPROTO_ICMPV6/
{
	printf("2 ip:::receive (");
	printf("args[2]: %d %d, ", args[2]->ip_ver, args[2]->ip_plength);
	printf("args[5]: %d %d %d)\n",
	    args[5]->ipv6_ver, args[5]->ipv6_tclass, args[5]->ipv6_plen);
}
EOF
