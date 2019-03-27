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
# Copyright (c) 2008, 2010, Oracle and/or its affiliates. All rights reserved.
#

#
# Test {udp,ip}:::{send,receive} of IPv4 UDP to a remote host.
#
# This may fail due to:
#
# 1. A change to the ip stack breaking expected probe behavior,
#    which is the reason we are testing.
# 2. No physical network interface is plumbed and up.
# 3. No other hosts on this subnet are reachable and listening on rpcbind.
# 4. An unlikely race causes the unlocked global send/receive
#    variables to be corrupted.
#
# This test sends a UDP message using perl and checks that at least the
# following counts were traced:
#
# 1 x ip:::send (UDP sent to ping's base UDP port)
# 1 x udp:::send (UDP sent to ping's base UDP port)
# 

if (( $# != 1 )); then
	print -u2 "expected one argument: <dtrace-path>"
	exit 2
fi

dtrace=$1
getaddr=./get.ipv4remote.pl
port=33434
DIR=/var/tmp/dtest.$$

if [[ ! -x $getaddr ]]; then
	print -u2 "could not find or execute sub program: $getaddr"
	exit 3
fi
$getaddr | read source dest
if (( $? != 0 )); then
	exit 4
fi

mkdir $DIR
cd $DIR

cat > test.pl <<-EOPERL
	use IO::Socket;
	my \$s = IO::Socket::INET->new(
	    Proto => "udp",
	    PeerAddr => "$dest",
	    PeerPort => $port);
	die "Could not create UDP socket $dest port $port" unless \$s;
	send \$s, "Hello", 0;
	close \$s;
	sleep(2);
EOPERL

$dtrace -c 'perl test.pl' -qs /dev/stdin <<EODTRACE
BEGIN
{
	ipsend = udpsend = 0;
}

ip:::send
/args[2]->ip_saddr == "$source" && args[2]->ip_daddr == "$dest" &&
    args[4]->ipv4_protocol == IPPROTO_UDP/
{
	ipsend++;
}

udp:::send
/args[2]->ip_saddr == "$source" && args[2]->ip_daddr == "$dest"/
{
	udpsend++;
}

END
{
	printf("Minimum UDP events seen\n\n");
	printf("ip:::send - %s\n", ipsend >= 1 ? "yes" : "no");
	printf("udp:::send - %s\n", udpsend >= 1 ? "yes" : "no");
}
EODTRACE

status=$?

cd /
/bin/rm -rf $DIR

exit $status
