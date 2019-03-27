#!/usr/bin/env perl
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
# get.ipv4remote.pl [port] [proto]
#
# Find an IPv4 reachable remote host using both ifconfig(1M) and ping(1M).
# If a port is specified, return a host that is also listening on this
# port. If the port is specified, the protocol can also be specified and
# defaults to tcp.  Print the local address and the remote address, or an
# error message if no suitable remote host was found.  Exit status is 0 if
# a host was found.
#

use strict;
use IO::Socket;

my $MAXHOSTS = 32;			# max hosts to port scan
my $TIMEOUT = 3;			# connection timeout
my $port = @ARGV >= 1 ? $ARGV[0] : 0;
my $proto = @ARGV == 2 ? $ARGV[1] : "tcp";

#
# Determine local IP address
#
my $local = "";
my $remote = "";
my %Broadcast;
my $up;
open IFCONFIG, '/sbin/ifconfig -a |' or die "Couldn't run ifconfig: $!\n";
while (<IFCONFIG>) {
	next if /^lo/;

	# "UP" is always printed first (see print_flags() in ifconfig.c):
	$up = 1 if /^[a-z].*<UP,/;
	$up = 0 if /^[a-z].*<,/;

	# assume output is "inet X ... broadcast Z":
	if (/inet (\S+) .* broadcast (\S+)/) {
		my ($addr, $bcast) = ($1, $2);
		$Broadcast{$addr} = $bcast;
		$local = $addr if $up and $local eq "";
		$up = 0;
	}
}
close IFCONFIG;
die "Could not determine local IP address" if $local eq "";

#
# Find the first remote host that responds to an icmp echo,
# which isn't a local address.
#
open PING, "/sbin/ping -n -s 56 -c $MAXHOSTS $Broadcast{$local} |" or
    die "Couldn't run ping: $!\n";
while (<PING>) {
	if (/bytes from (.*): / and not defined $Broadcast{$1}) {
		my $addr = $1;

		if ($port != 0) {
			#
			# Test TCP
			#
			my $socket = IO::Socket::INET->new(
				Type     => SOCK_STREAM,
				Proto    => $proto,
				PeerAddr => $addr,
				PeerPort => $port,
				Timeout  => $TIMEOUT,
			);
			next unless $socket;
			close $socket;
		}

		$remote = $addr;
		last;
	}
}
close PING;
die "Can't find a remote host for testing: No suitable response from " .
    "$Broadcast{$local}\n" if $remote eq "";

print "$local $remote\n";
