# $NetBSD: t_devices.sh,v 1.5 2010/11/07 17:51:18 jmmv Exp $
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

atf_test_case basic
basic_head() {
	atf_set "descr" "Tests that special devices work"
	atf_set "require.user" "root"
}
basic_body() {
	test_mount

	umask 022

	atf_check -s eq:0 -o ignore -e ignore /dev/MAKEDEV std
	atf_check -s eq:0 -o empty -e empty test -e zero
	atf_check -s eq:0 -o empty -e empty test -e null

	echo "Reading from the 'zero' character device"
	atf_check -s eq:0 -o ignore -e ignore dd if=zero of=a bs=10k count=1
	[ $(md5 a | cut -d ' ' -f 4) = 1276481102f218c981e0324180bafd9f ] || \
	    atf_fail "Data read is invalid"

	echo "Writing to the 'null' device"
	echo foo >null
	eval $(stat -s null)
	[ ${st_size} -eq 0 ] || atf_fail "Invalid size for device"

	test_unmount
}

atf_init_test_cases() {
	. $(atf_get_srcdir)/../h_funcs.subr
	. $(atf_get_srcdir)/h_funcs.subr

	atf_add_test_case basic
}
