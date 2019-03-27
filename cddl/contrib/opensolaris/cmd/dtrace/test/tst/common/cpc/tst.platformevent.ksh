#!/bin/ksh
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
# This script ensures that we can enable a probe which specifies a platform
# specific event.
#

if [ $# != 1 ]; then
        print -u2 "expected one argument: <dtrace-path>"
        exit 2
fi

dtrace=$1

get_event()
{
        perl /dev/stdin /dev/stdout << EOF
        open CPUSTAT, '/usr/sbin/cpustat -h |'
            or die  "Couldn't run cpustat: \$!\n";
        while (<CPUSTAT>) {
                if (/(\s+)event\[*[0-9]-*[0-9]*\]*:/ && !/PAPI/) {
                        @a = split(/ /, \$_);
                        \$event = \$a[\$#a-1];
                }
        }

        close CPUSTAT;
        print "\$event\n";
EOF
}

script()
{
        $dtrace -s /dev/stdin << EOD
        #pragma D option quiet
        #pragma D option bufsize=128k

        cpc:::$1-all-10000
        {
                @[probename] = count();
        }

        tick-1s
        /n++ > 5/
        {
                exit(0);
        }
EOD
}

event=$(get_event)
script $event

status=$?

exit $status
