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
# Test {tcp,ip}:::{send,receive} of IPv4 TCP to a remote host.
#
# This may fail due to:
#
# 1. A change to the ip stack breaking expected probe behavior,
#    which is the reason we are testing.
# 2. No physical network interface is plumbed and up.
# 3. No other hosts on this subnet are reachable and listening on ssh.
# 4. An unlikely race causes the unlocked global send/receive
#    variables to be corrupted.
#
# This test performs a TCP connection and checks that at least the
# following packet counts were traced:
#
# 4 x ip:::send (2 during connection setup, 2 during connection teardown)
# 4 x tcp:::send (2 during connection setup, 2 during connection teardown)
# 5 x ip:::receive (1 during connection setup, the response, 1 window update,
#                   1 banner line, 2 during connection teardown)
# 5 x tcp:::receive (1 during connection setup, the response, 1 window update,
#                    1 banner line, 2 during connection teardown)

if (( $# != 1 )); then
	print -u2 "expected one argument: <dtrace-path>"
	exit 2
fi

dtrace=$1
getaddr=./get.ipv4remote.pl
tcpport=22
DIR=/var/tmp/dtest.$$

if [[ ! -x $getaddr ]]; then
        print -u2 "could not find or execute sub program: $getaddr"
        exit 3
fi
$getaddr $tcpport | read source dest
if (( $? != 0 )); then
        exit 4
fi

mkdir $DIR
cd $DIR

cat > test.pl <<-EOPERL
	use IO::Socket;
	my \$s = IO::Socket::INET->new(
	    Proto => "tcp",
	    PeerAddr => "$dest",
	    PeerPort => $tcpport,
	    Timeout => 3);
	die "Could not connect to host $dest port $tcpport" unless \$s;
	readline \$s;
	close \$s;
	sleep(2);
EOPERL

$dtrace -c 'perl test.pl' -qs /dev/stdin <<EODTRACE
BEGIN
{
	ipsend = tcpsend = ipreceive = tcpreceive = 0;
}

ip:::send
/args[2]->ip_saddr == "$source" && args[2]->ip_daddr == "$dest" &&
    args[4]->ipv4_protocol == IPPROTO_TCP/
{
	ipsend++;
}

tcp:::send
/args[2]->ip_saddr == "$source" && args[2]->ip_daddr == "$dest"/
{
	tcpsend++;
}

ip:::receive
/args[2]->ip_saddr == "$dest" && args[2]->ip_daddr == "$source" &&
    args[4]->ipv4_protocol == IPPROTO_TCP/
{
	ipreceive++;
}

tcp:::receive
/args[2]->ip_saddr == "$dest" && args[2]->ip_daddr == "$source"/
{
	tcpreceive++;
}

END
{
	printf("Minimum TCP events seen\n\n");
	printf("ip:::send - %s\n", ipsend >= 4 ? "yes" : "no");
	printf("ip:::receive - %s\n", ipreceive >= 5 ? "yes" : "no");
	printf("tcp:::send - %s\n", tcpsend >= 4 ? "yes" : "no");
	printf("tcp:::receive - %s\n", tcpreceive >= 5 ? "yes" : "no");
}
EODTRACE

status=$?

cd /
/bin/rm -rf $DIR

exit $status
