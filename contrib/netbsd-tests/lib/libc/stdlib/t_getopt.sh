# $NetBSD: t_getopt.sh,v 1.1 2011/01/01 23:56:49 pgoyette Exp $
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

h_getopt()
{
	atf_check -e save:stderr -x "$(atf_get_srcdir)/h_getopt" <<EOF
load:	$1
args:	$2
result:	$3
EOF
	cat stderr
	rm -f stderr
}

h_getopt_long()
{
	atf_check -e save:stderr -x "$(atf_get_srcdir)/h_getopt_long" <<EOF
$1
args:	$2
result:	$3
EOF
	cat stderr
	rm -f stderr
}

atf_test_case getopt
getopt_head()
{
	atf_set "descr" "Checks getopt(3)"
}
getopt_body()
{
	load="c:d"

	h_getopt "${load}" "foo -c 1 -d foo" "c=1,d|1"
	h_getopt "${load}" "foo -d foo bar" "d|2"
	h_getopt "${load}" "foo -c 2 foo bar" "c=2|2"
	h_getopt "${load}" "foo -e 1 foo bar" "!?|3"
	h_getopt "${load}" "foo -d -- -c 1" "d|2"
	h_getopt "${load}" "foo -c- 1" "c=-|1"
	h_getopt "${load}" "foo -d - 1" "d|2"
}

atf_test_case getopt_long
getopt_long_head()
{
	atf_set "descr" "Checks getopt_long(3)"
}
getopt_long_body()
{
	# GNU libc tests with these
	load="optstring:	abc:
longopts:	5
longopt:	required, required_argument, , 'r'
longopt:	optional, optional_argument, , 'o'
longopt:	none, no_argument, , 'n'
longopt:	color, no_argument, , 'C'
longopt:	colour, no_argument, , 'C'"

	h_getopt_long "${load}" "foo --req foobar" "-required=foobar|0"
	h_getopt_long "${load}" "foo --opt=bazbug" "-optional=bazbug|0"
	
	# This is problematic
	#
	# GNU libc 2.1.3 this fails with ambiguous result
	# h_getopt_long "${load}" "foo --col" "!?|0"
	#
	# GNU libc 2.2 >= this works
	h_getopt_long "${load}" "foo --col" "-color|0"

	h_getopt_long "${load}" "foo --colour" "-colour|0"

	# This is the real GNU libc test!
	args="foo -a -b -cfoobar --required foobar --optional=bazbug --none random --col --color --colour"
	# GNU libc 2.1.3 this fails with ambiguous
	#result="a,b,c=foobar,-required=foobar,-optional=bazbug,-none,!?,-color,-colour|1"
	#
	# GNU libc 2.2 >= this works
	result="a,b,c=foobar,-required=foobar,-optional=bazbug,-none,-color,-color,-colour|1"
	h_getopt_long "${load}" "${args}" "${result}"

	#
	# The order of long options in the long option array should not matter.
	# An exact match should never be treated as ambiguous.
	#
	load="optstring:	fgl
longopts:	3
longopt:	list-foobar, no_argument, lopt, 'f'
longopt:	list-goobar, no_argument, lopt, 'g'
longopt:	list, no_argument, lopt, 'l'"
	h_getopt_long "${load}" "foo --list" "-list|0"
}


atf_init_test_cases()
{
	atf_add_test_case getopt
	atf_add_test_case getopt_long
}
