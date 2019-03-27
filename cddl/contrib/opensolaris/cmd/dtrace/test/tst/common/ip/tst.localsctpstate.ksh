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
# Copyright (c) 2010, Oracle and/or its affiliates. All rights reserved.
#

#
# Test sctp:::state-change and sctp:::{send,receive} by connecting to
# the local discard service.
# A number of state transition events along with SCTP send and
# receive events for the message should result.
#
# This may fail due to:
#
# 1. A change to the ip stack breaking expected probe behavior,
#    which is the reason we are testing.
# 2. The lo0 interface missing or not up.
# 3. An unlikely race causes the unlocked global send/receive
#    variables to be corrupted.
#
# This test performs a SCTP connection and checks that at least the
# following packet counts were traced:
#
# 7 x ip:::send (4 during the setup, 3 during the teardown)
# 7 x sctp:::send (4 during the setup, 3 during the teardown)
# 7 x ip:::receive (4 during the setup, 3 during the teardown)
# 7 x sctp:::receive (4 during the setup, 3 during the teardown)
#
# The actual count tested is 7 each way, since we are tracing both
# source and destination events.
#

if (( $# != 1 )); then
	print -u2 "expected one argument: <dtrace-path>"
	exit 2
fi

dtrace=$1
local=127.0.0.1
DIR=/var/tmp/dtest.$$

sctpport=1024
bound=5000
while [ $sctpport -lt $bound ]; do
	ncat --sctp -z $local $sctpport > /dev/null || break
	sctpport=$(($sctpport + 1))
done
if [ $sctpport -eq $bound ]; then
	echo "couldn't find an available SCTP port"
	exit 1
fi

mkdir $DIR
cd $DIR

# ncat will exit when the association is closed.
ncat --sctp --listen $local $sctpport &

cat > test.pl <<-EOPERL
	use IO::Socket;
	my \$s = IO::Socket::INET->new(
	    Type => SOCK_STREAM,
	    Proto => "sctp",
	    LocalAddr => "$local",
	    PeerAddr => "$local",
	    PeerPort => $sctpport,
	    Timeout => 3);
	die "Could not connect to host $local port $sctpport \$@" unless \$s;
	close \$s;
	sleep(2);
EOPERL

$dtrace -c 'perl test.pl' -qs /dev/stdin <<EODTRACE
BEGIN
{
	ipsend = sctpsend = ipreceive = sctpreceive = 0;
}

ip:::send
/args[2]->ip_saddr == "$local" && args[2]->ip_daddr == "$local" &&
    args[4]->ipv4_protocol == IPPROTO_SCTP/
{
	ipsend++;
}

sctp:::send
/args[2]->ip_saddr == "$local" && args[2]->ip_daddr == "$local" &&
 (args[4]->sctp_sport == $sctpport || args[4]->sctp_dport == $sctpport)/
{
	sctpsend++;
}

ip:::receive
/args[2]->ip_saddr == "$local" && args[2]->ip_daddr == "$local" &&
    args[4]->ipv4_protocol == IPPROTO_SCTP/
{
	ipreceive++;
}

sctp:::receive
/args[2]->ip_saddr == "$local" && args[2]->ip_daddr == "$local" &&
 (args[4]->sctp_sport == $sctpport || args[4]->sctp_dport == $sctpport)/
{
	sctpreceive++;
}

sctp:::state-change
{
	state_event[args[3]->sctps_state]++;
}

END
{
	printf("Minimum SCTP events seen\n\n");
	printf("ip:::send - %s\n", ipsend >= 7 ? "yes" : "no");
	printf("ip:::receive - %s\n", ipreceive >= 7 ? "yes" : "no");
	printf("sctp:::send - %s\n", sctpsend >= 7 ? "yes" : "no");
	printf("sctp:::receive - %s\n", sctpreceive >= 7 ? "yes" : "no");
	printf("sctp:::state-change to cookie-wait - %s\n",
	    state_event[SCTP_STATE_COOKIE_WAIT] >=1 ? "yes" : "no");
	printf("sctp:::state-change to cookie-echoed - %s\n",
	    state_event[SCTP_STATE_COOKIE_ECHOED] >=1 ? "yes" : "no");
	printf("sctp:::state-change to established - %s\n",
	    state_event[SCTP_STATE_ESTABLISHED] >= 2 ? "yes" : "no");
	printf("sctp:::state-change to shutdown-sent - %s\n",
	    state_event[SCTP_STATE_SHUTDOWN_SENT] >= 1 ? "yes" : "no");
	printf("sctp:::state-change to shutdown-received - %s\n",
	    state_event[SCTP_STATE_SHUTDOWN_RECEIVED] >= 1 ? "yes" : "no");
	printf("sctp:::state-change to shutdown-ack-sent - %s\n",
	    state_event[SCTP_STATE_SHUTDOWN_ACK_SENT] >= 1 ? "yes" : "no");
}
EODTRACE

status=$?

cd /
/bin/rm -rf $DIR

exit $status
