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
# Test ip:::{send,receive} of IPv4 ICMP to a remote host.
#
# This may fail due to:
#
# 1. A change to the ip stack breaking expected probe behavior,
#    which is the reason we are testing.
# 2. No physical network interface is plumbed and up.
# 3. No other hosts on this subnet are reachable.
# 4. An unrelated ICMP between these hosts was traced by accident.
#

if (( $# != 1 )); then
        print -u2 "expected one argument: <dtrace-path>"
        exit 2
fi

dtrace=$1
getaddr=./get.ipv4remote.pl

if [[ ! -x $getaddr ]]; then
	print -u2 "could not find or execute sub program: $getaddr"
	exit 3
fi
$getaddr | read source dest
if (( $? != 0 )); then
	exit 4
fi

$dtrace -c "/sbin/ping $dest 3" -qs /dev/stdin <<EOF | \
    grep -v 'is alive' | sort -n
ip:::send
/args[2]->ip_saddr == "$source" && args[2]->ip_daddr == "$dest" &&
    args[4]->ipv4_protocol == IPPROTO_ICMP/
{
	printf("1 ip:::send    (");
	printf("args[2]: %d %d, ", args[2]->ip_ver, args[2]->ip_plength);
	printf("args[4]: %d %d %d %d %d)\n",
	    args[4]->ipv4_ver, args[4]->ipv4_length, args[4]->ipv4_flags,
	    args[4]->ipv4_offset, args[4]->ipv4_ttl);
}

ip:::receive
/args[2]->ip_saddr == "$dest" && args[2]->ip_daddr == "$source" &&
    args[4]->ipv4_protocol == IPPROTO_ICMP/
{
	printf("2 ip:::receive (");
	printf("args[2]: %d %d, ", args[2]->ip_ver, args[2]->ip_plength);
	printf("args[4]: %d %d %d %d %d)\n",
	    args[4]->ipv4_ver, args[4]->ipv4_length, args[4]->ipv4_flags,
	    args[4]->ipv4_offset, args[4]->ipv4_ttl);
}
EOF
