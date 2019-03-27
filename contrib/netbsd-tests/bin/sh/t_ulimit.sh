# $NetBSD: t_ulimit.sh,v 1.3 2016/03/27 14:50:01 christos Exp $
#
# Copyright (c) 2012 The NetBSD Foundation, Inc.
# All rights reserved.
#
# Redistribution and use in source and binary forms, with or without
# modification, are permitted provided that the following conditions
# are met:
# 1. Redistributions of source code must retain the above copyright
#    notice, this list of conditions and the following disclaimer.
# 2. Redistributions in binary form must reproduce the above copyright
#    notice, this list of conditions and the following disclaimer in the
#    documentation and/or other materials provided with the distribution.
#
# THIS SOFTWARE IS PROVIDED BY THE NETBSD FOUNDATION, INC. AND CONTRIBUTORS
# ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED
# TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
# PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE FOUNDATION OR CONTRIBUTORS
# BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
# CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
# SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
# INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
# CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
# ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
# POSSIBILITY OF SUCH DAMAGE.
#
# the implementation of "sh" to test
: ${TEST_SH:="/bin/sh"}

# ulimit builtin test.

atf_test_case limits
limits_head() {
	atf_set "descr" "Checks for limits flags"
}

get_ulimits() {
	local limits=$(${TEST_SH} -c 'ulimit -a' |
	    sed -e 's/.*\(-[A-Za-z0-9]\)[^A-Za-z0-9].*/\1/' | sort -u)
	if [ -z "$limits" ]; then
		# grr ksh
		limits="-a -b -c -d -f -l -m -n -p -r -s -t -v"
	fi
	echo "$limits"
}

limits_body() {
	atf_check -s eq:0 -o ignore -e empty ${TEST_SH} -c "ulimit -a"
	for l in $(get_ulimits)
	do
	    atf_check -s eq:0 -o ignore -e empty ${TEST_SH} -c "ulimit $l"
	done
}

atf_init_test_cases() {
	atf_add_test_case limits
}
