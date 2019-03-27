# $NetBSD: t_read_write.sh,v 1.5 2010/11/07 17:51:18 jmmv Exp $
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

#
# Verifies that the read and write operations work.
#

atf_test_case basic
basic_head() {
	atf_set "descr" "Checks that file removal works"
	atf_set "require.user" "root"
}
basic_body() {
	test_mount

	echo "Testing write to a small file"
	echo foo >a || atf_fail "Failed to write to file"
	[ $(md5 a | cut -d ' ' -f 4) = d3b07384d113edec49eaa6238ad5ff00 ] || \
	    atf_fail "Invalid file contents"

	echo "Testing appending to a small file"
	echo bar >>a || atf_fail "Failed to append data to file"
	[ $(md5 a | cut -d ' ' -f 4) = f47c75614087a8dd938ba4acff252494 ] || \
	    atf_fail "Invalid file contents"

	echo "Testing write to a big file (bigger than a page)"
	jot 10000 >b || atf_fail "Failed to create a big file"
	[ $(md5 b | cut -d ' ' -f 4) = 72d4ff27a28afbc066d5804999d5a504 ] || \
	    atf_fail "Invalid file contents"

	test_unmount
}

atf_test_case kqueue
kqueue_head() {
	atf_set "descr" "Checks that writing to a file raises the" \
	                "appropriate kqueue events"
	atf_set "require.user" "root"
}
kqueue_body() {
	test_mount

	atf_check -s eq:0 -o ignore -e ignore \
	    dd if=/dev/zero of=c bs=1k count=10
	echo 'dd if=/dev/zero of=c seek=2 bs=1k count=1 conv=notrunc' \
	    '>/dev/null 2>&1' | kqueue_monitor 1 c
	kqueue_check c NOTE_WRITE

	echo foo >d
	echo 'echo bar >>d' | kqueue_monitor 2 d
	kqueue_check d NOTE_EXTEND
	kqueue_check d NOTE_WRITE

	test_unmount
}

atf_init_test_cases() {
	. $(atf_get_srcdir)/../h_funcs.subr
	. $(atf_get_srcdir)/h_funcs.subr

	atf_add_test_case basic
	atf_add_test_case kqueue
}
