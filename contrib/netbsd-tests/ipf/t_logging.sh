# $NetBSD: t_logging.sh,v 1.2 2010/10/19 16:36:36 jmmv Exp $
#
# Copyright (c) 2008, 2010 The NetBSD Foundation, Inc.
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
#
# (C)opyright 1993-1996 by Darren Reed.
#
# See the IPFILTER.LICENCE file for details on licencing.
#

logtest()
{
	h_copydata $1
	cp $(atf_get_srcdir)/expected/$1.b exp.b

	case `uname -s` in
	OSF1)
		GMT=:
		;;
	*)
		GMT=GMT
		;;
	esac

	{ while read rule; do
		echo $rule >>out
		atf_check -o ignore -x \
		    "echo $rule | ipftest -br - -F $2 -i in -l logout"

		atf_check -o save:save -x "TZ=$GMT ipmon -P /dev/null -f logout"
		cat save >>out
		echo "--------" >>out

		atf_check -o save:save -x "TZ=$GMT ipmon -P /dev/null -bf logout"
		cat save >>out.b
		echo "--------" >>out.b
	done; } <reg

	atf_check -o ignore ipftest -br reg -F $2 -i in -l logout

	atf_check -o save:save -x "TZ=$GMT ipmon -P /dev/null -f logout"
	cat save >>out
	echo "--------" >>out

	atf_check -o save:save -x "TZ=$GMT ipmon -P /dev/null -bf logout"
	cat save >>out.b
	echo "--------" >>out.b

	diff -u exp out || atf_fail "results differ"
	diff -u exp.b out.b || atf_fail "results differ"
}

test_case l1 logtest hex hex

atf_init_test_cases()
{
	atf_add_test_case l1
}
