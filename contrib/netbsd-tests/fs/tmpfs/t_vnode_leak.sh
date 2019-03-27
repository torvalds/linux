# $NetBSD: t_vnode_leak.sh,v 1.6 2010/11/07 17:51:18 jmmv Exp $
#
# Copyright (c) 2005, 2006, 2007, 2008 The NetBSD Foundation, Inc.
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

atf_test_case main cleanup
main_head() {
	atf_set "descr" "Verifies that vnodes are not leaked and that" \
	                "their reclaim operation works as expected: i.e.," \
	                "when all free vnodes are exhausted, unused ones" \
	                "have to be recycled, which is what the reclaim" \
	                "operation does."
	atf_set "require.user" "root"
}
main_body() {
	echo "Lowering kern.maxvnodes to 2000"
	# Begin FreeBSD
	if true; then
		sysctl -n kern.maxvnodes > oldvnodes
	else
	# End FreeBSD
	sysctl kern.maxvnodes | awk '{ print $3; }' >oldvnodes
	# Begin FreeBSD
	fi
	# End FreeBSD
	atf_check -s eq:0 -o ignore -e empty sysctl -w kern.maxvnodes=2000

	test_mount -o -s$(((4000 + 2) * 4096))
	echo "Creating 4000 directories"
	for f in $(jot 4000); do
		mkdir ${f}
	done
	test_unmount
}
main_cleanup() {
	oldvnodes=$(cat oldvnodes)
	echo "Restoring kern.maxvnodes to ${oldvnodes}"
	sysctl -w kern.maxvnodes=${oldvnodes}
}

atf_init_test_cases() {
	. $(atf_get_srcdir)/../h_funcs.subr
	. $(atf_get_srcdir)/h_funcs.subr

	atf_add_test_case main
}
