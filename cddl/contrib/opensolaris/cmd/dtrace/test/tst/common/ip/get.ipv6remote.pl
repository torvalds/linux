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
# get.ipv6remote.pl
#
# Find an IPv6 reachable remote host using both ifconfig(1M) and ping(1M).
# Print the local address and the remote address, or print nothing if either
# no IPv6 interfaces or remote hosts were found.  (Remote IPv6 testing is
# considered optional, and so not finding another IPv6 host is not an error
# state we need to log.)  Exit status is 0 if a host was found.
#

use strict;
use IO::Socket;

my $MAXHOSTS = 32;			# max hosts to scan
my $TIMEOUT = 3;			# connection timeout
my $MULTICAST = "FF02::1";		# IPv6 multicast address

#
# Determine local IP address
#
my $local = "";
my $remote = "";
my $interf = "";
my %Local;
my %Addr;
my $up;
open IFCONFIG, '/sbin/ifconfig -a inet6 |'
    or die "Couldn't run ifconfig: $!\n";
while (<IFCONFIG>) {
	next if /^lo/;

	# "UP" is always printed first (see print_flags() in ifconfig.c):
	$up = 1 if /^[a-z].*<UP,/;
	$up = 0 if /^[a-z].*<,/;

	if (m:(\S+\d+)\: :) {
		$interf = $1;
	}

	# assume output is "inet6 ...":
	if (m:inet6 (\S+) :) {
		my $addr = $1;
                $Local{$addr} = 1;
                $Addr{$interf} = $addr;
		$up = 0;
		$interf = "";
	}
}
close IFCONFIG;

#
# Find the first remote host that responds to an icmp echo,
# which isn't a local address. Try each IPv6-enabled interface.
#
foreach $interf (split(' ', `ifconfig -l -u inet6`)) {
	next if $interf =~ /lo[0-9]+/;
	open PING, "/sbin/ping6 -n -s 56 -c $MAXHOSTS $MULTICAST\%$interf |" or next;
	while (<PING>) {
		if (/bytes from (.*), / and not defined $Local{$1}) {
			$remote = $1;
			$local = $Addr{$interf};
			last;
		}
	}
}
close PING;
exit 2 if $remote eq "";

print "$local $remote\n";
