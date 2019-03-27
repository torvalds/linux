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

#
# This script tests that several of the the mib:::tcp* probes fire and fire
# with a valid args[0].
#

if [ $# != 1 ]; then
	echo expected one argument: '<'dtrace-path'>'
	exit 2
fi

dtrace=$1
dtraceout=/tmp/dtrace.out.$$
timeout=15
port=2000

if [ -f $dtraceout ]; then
	rm -f $dtraceout
fi

script()
{
	$dtrace -o $dtraceout -s /dev/stdin <<EOF
	mib:::tcpActiveOpens
	{
		opens = args[0];
	}

	mib:::tcpOutDataBytes
	{
		bytes = args[0];
	}

	mib:::tcpOutDataSegs
	{
		segs = args[0];
	}

	profile:::tick-10msec
	/opens && bytes && segs/
	{
		exit(0);
	}

	profile:::tick-1s
	/n++ >= 10/
	{
		exit(1);
	}
EOF
}

server()
{
	perl /dev/stdin /dev/stdout << EOF
	use strict;
	use Socket;

	socket(S, AF_INET, SOCK_STREAM, getprotobyname('tcp'))
	    or die "socket() failed: \$!";

	setsockopt(S, SOL_SOCKET, SO_REUSEADDR, 1)
	    or die "setsockopt() failed: \$!";

	my \$addr = sockaddr_in($port, INADDR_ANY);
	bind(S, \$addr) or die "bind() failed: \$!";
	listen(S, SOMAXCONN) or die "listen() failed: \$!";

	while (1) {
		next unless my \$raddr = accept(SESSION, S);

		while (<SESSION>) {
		}

		close SESSION;
	}
EOF
}

client()
{
	perl /dev/stdin /dev/stdout <<EOF
	use strict;
	use Socket;

	my \$peer = sockaddr_in($port, INADDR_ANY);

	socket(S, AF_INET, SOCK_STREAM, getprotobyname('tcp'))
	    or die "socket() failed: \$!";

	connect(S, \$peer) or die "connect failed: \$!";

	for (my \$i = 0; \$i < 10; \$i++) {
		send(S, "There!", 0) or die "send() failed: \$!";
		sleep (1);
	}
EOF
}

script &
dtrace_pid=$!

#
# Sleep while the above script fires into life. To guard against dtrace dying
# and us sleeping forever we allow 15 secs for this to happen. This should be
# enough for even the slowest systems.
#
while [ ! -f $dtraceout ]; do
	sleep 1
	timeout=$(($timeout-1))
	if [ $timeout -eq 0 ]; then
		echo "dtrace failed to start. Exiting."
		exit 1
	fi
done

server &
server_pid=$!
sleep 2
client &
client_pid=$!

wait $dtrace_pid
status=$?

kill $server_pid
kill $client_pid

exit $status
