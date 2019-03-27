#!/usr/bin/env ksh
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
# Test {ip,udplite}:::{send,receive} of IPv4 UDP-Lite to a local address.
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
# This test sends a UDP-Lite message using perl and checks that at least the
# following counts were traced:
#
# 1 x ip:::send (UDPLite sent to UDP-Lite port 33434)
# 1 x udplite:::send (UDPLite sent to UDP-Lite port 33434)
# 1 x ip:::receive (UDP-Lite received)
# 1 x udplite:::receive (UDP-Lite received)
# 
# A udplite:::receive event is expected even if the received UDP-Lite packet
# elicits an ICMP PORT_UNREACHABLE message since there is no UDP-Lite
# socket for receiving the packet.
#

if (( $# != 1 )); then
	print -u2 "expected one argument: <dtrace-path>"
	exit 2
fi

dtrace=$1
local=127.0.0.1
port=33434
DIR=/var/tmp/dtest.$$

mkdir $DIR
cd $DIR

cat > test.pl <<-EOPERL
	use IO::Socket;
	my \$s = IO::Socket::INET->new(
	    Type => SOCK_DGRAM,
	    Proto => "udplite",
	    PeerAddr => "$local",
	    PeerPort => $port);
	die "Could not create UDP-Lite socket $local port $port" unless \$s;
	send \$s, "Hello", 0;
	close \$s;
	sleep(2);
EOPERL

$dtrace -c 'perl test.pl' -qs /dev/stdin <<EODTRACE
BEGIN
{
	ipsend = udplitesend = ipreceive = udplitereceive = 0;
}

ip:::send
/args[2]->ip_saddr == "$local" && args[2]->ip_daddr == "$local" &&
    args[4]->ipv4_protocol == IPPROTO_UDPLITE/
{
	ipsend++;
}

udplite:::send
/args[2]->ip_saddr == "$local" && args[2]->ip_daddr == "$local"/
{
	udplitesend++;
}

ip:::receive
/args[2]->ip_saddr == "$local" && args[2]->ip_daddr == "$local" &&
    args[4]->ipv4_protocol == IPPROTO_UDPLITE/
{
	ipreceive++;
}

udplite:::receive
/args[2]->ip_saddr == "$local" && args[2]->ip_daddr == "$local"/
{
	udplitereceive++;
}

END
{
	printf("Minimum UDP-Lite events seen\n\n");
	printf("ip:::send - %s\n", ipsend >= 1 ? "yes" : "no");
	printf("ip:::receive - %s\n", ipreceive >= 1 ? "yes" : "no");
	printf("udplite:::send - %s\n", udplitesend >= 1 ? "yes" : "no");
	printf("udplite:::receive - %s\n", udplitereceive >= 1 ? "yes" : "no");
}
EODTRACE

status=$?

cd /
/bin/rm -rf $DIR

exit $status
