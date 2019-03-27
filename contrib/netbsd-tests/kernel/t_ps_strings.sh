# $NetBSD: t_ps_strings.sh,v 1.1 2011/03/05 18:14:33 pgoyette Exp $
#
# Copyright (c) 2008 The NetBSD Foundation, Inc.
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

atf_test_case validate
validate_head()
{
	atf_set "descr" "Validates ps_strings passed to program"
}
validate_body()
{
	atf_check -s exit:0 -o ignore -e ignore \
		$(atf_get_srcdir)/h_ps_strings1
}

# Function to parse and validate the output from ps

parse_ps() {
	local pid seq arg

	pid="$1" ; shift

	while [ "$1" != "$pid" ] ; do
		echo $1
		shift
	done
	if [ $# -eq 0 ] ; then
		echo "NO_PID"
		return
	fi
	shift

	seq=0
	while [ $# -gt 1 ] ; do
		arg=$(printf "arg%04x" $seq)
		if [ "$arg" != "$1" ] ; then
			echo BAD_$seq
			return
		fi
		shift
	done
	echo "OK"
}

atf_test_case update
update_head()
{
	atf_set "descr" "Check updating of ps_strings"
}
update_body()
{
	$(atf_get_srcdir)/h_ps_strings2 > /dev/null 2>&1 &
	h_pid=$!
	parse=$(parse_ps $h_pid $(ps -wwo pid,args -p $h_pid) )
	kill $h_pid
}

atf_init_test_cases()
{
	atf_add_test_case validate
	atf_add_test_case update
}
