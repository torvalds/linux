#	$NetBSD: t_md.sh,v 1.7 2011/05/14 17:42:28 jmmv Exp $
#
# Copyright (c) 2010 The NetBSD Foundation, Inc.
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

rawpart=`sysctl -n kern.rawpartition | tr '01234' 'abcde'`
rawmd=/dev/rmd0${rawpart}

atf_test_case basic cleanup
basic_head()
{

	atf_set "descr" "Check that md can be created, read and written"
}

basic_body()
{

	# Scope out raw part.  This is actually the *host* raw partition,
	# but just let it slide for now, since they *should* be the same.
	rawpart=`sysctl -n kern.rawpartition | tr '01234' 'abcde'`

	atf_check -s exit:0 $(atf_get_srcdir)/h_mdserv ${rawmd}

	export RUMP_SERVER=unix://commsock
	atf_check -s exit:0 -e ignore -x \
	    "dd if=/bin/ls count=10 | rump.dd of=${rawmd} seek=100"
	atf_check -s exit:0 -e ignore -x \
	    "rump.dd if=${rawmd} skip=100 count=10 | dd of=testfile"
	atf_check -s exit:0 -e ignore -o file:testfile dd if=/bin/ls count=10
}

basic_cleanup()
{

	env RUMP_SERVER=unix://commsock rump.halt
}

atf_init_test_cases()
{

	atf_add_test_case basic
}
