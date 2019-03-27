# $NetBSD: t_setattr.sh,v 1.5 2010/11/07 17:51:18 jmmv Exp $
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
# Verifies that the setattr vnode operation works, using several commands
# that require this function.
#

atf_test_case chown
chown_head() {
	atf_set "descr" "Tests that the file owner can be changed"
	atf_set "require.user" "root"
}
chown_body() {
	test_mount

	atf_check -s eq:0 -o empty -e empty mkdir own
	eval $(stat -s own | sed -e 's|st_|ost_|g')
	atf_check -s eq:0 -o empty -e empty chown 1234 own
	eval $(stat -s own)
	[ ${st_uid} -eq 1234 ] || atf_fail "uid was not set"
	[ ${st_gid} -eq ${ost_gid} ] || atf_fail "gid was modified"

	test_unmount
}

atf_test_case chown_kqueue
chown_kqueue_head() {
	atf_set "descr" "Tests that changing the file owner raises" \
	                "NOTE_ATTRIB on it"
	atf_set "require.user" "root"
}
chown_kqueue_body() {
	test_mount

	atf_check -s eq:0 -o empty -e empty mkdir ownq
	echo 'chown 1234 ownq' | kqueue_monitor 1 ownq
	kqueue_check ownq NOTE_ATTRIB

	test_unmount
}

atf_test_case chgrp
chgrp_head() {
	atf_set "descr" "Tests that the file group can be changed"
	atf_set "require.user" "root"
}
chgrp_body() {
	test_mount

	atf_check -s eq:0 -o empty -e empty mkdir grp
	eval $(stat -s grp | sed -e 's|st_|ost_|g')
	atf_check -s eq:0 -o empty -e empty chgrp 5678 grp
	eval $(stat -s grp)
	[ ${st_uid} -eq ${ost_uid} ] || atf_fail "uid was modified"
	[ ${st_gid} -eq 5678 ] || atf_fail "gid was not set"

	test_unmount
}

atf_test_case chgrp_kqueue
chgrp_kqueue_head() {
	atf_set "descr" "Tests that changing the file group raises" \
	                "NOTE_ATTRIB on it"
	atf_set "require.user" "root"
}
chgrp_kqueue_body() {
	test_mount

	atf_check -s eq:0 -o empty -e empty mkdir grpq
	echo 'chgrp 1234 grpq' | kqueue_monitor 1 grpq
	kqueue_check grpq NOTE_ATTRIB

	test_unmount
}

atf_test_case chowngrp
chowngrp_head() {
	atf_set "descr" "Tests that the file owner and group can be" \
	                "changed at once"
	atf_set "require.user" "root"
}
chowngrp_body() {
	test_mount

	atf_check -s eq:0 -o empty -e empty mkdir owngrp
	atf_check -s eq:0 -o empty -e empty chown 1234:5678 owngrp
	eval $(stat -s owngrp)
	[ ${st_uid} -eq 1234 ] || atf_fail "uid was not modified"
	[ ${st_gid} -eq 5678 ] || atf_fail "gid was not modified"

	test_unmount
}

atf_test_case chowngrp_kqueue
chowngrp_kqueue_head() {
	atf_set "descr" "Tests that changing the file owner and group" \
	                "raises NOTE_ATTRIB on it"
	atf_set "require.user" "root"
}
chowngrp_kqueue_body() {
	test_mount

	atf_check -s eq:0 -o empty -e empty mkdir owngrpp
	echo 'chown 1234:5678 owngrpp' | kqueue_monitor 1 owngrpp
	kqueue_check owngrpp NOTE_ATTRIB

	test_unmount
}

atf_test_case chmod
chmod_head() {
	atf_set "descr" "Tests that the file mode can be changed"
	atf_set "require.user" "root"
}
chmod_body() {
	test_mount

	atf_check -s eq:0 -o empty -e empty mkdir mode
	atf_check -s eq:0 -o empty -e empty chmod 0000 mode
	eval $(stat -s mode)
	[ ${st_mode} -eq 40000 ] || af_fail "mode was not set"

	test_unmount
}

atf_test_case chmod_kqueue
chmod_kqueue_head() {
	atf_set "descr" "Tests that changing the file mode raises" \
	                "NOTE_ATTRIB on it"
	atf_set "require.user" "root"
}
chmod_kqueue_body() {
	test_mount

	atf_check -s eq:0 -o empty -e empty mkdir modeq
	echo 'chmod 0000 modeq' | kqueue_monitor 1 modeq
	kqueue_check modeq NOTE_ATTRIB

	test_unmount
}

atf_test_case chtimes
chtimes_head() {
	atf_set "descr" "Tests that file times can be changed"
	atf_set "require.user" "root"
}
chtimes_body() {
	test_mount

	atf_check -s eq:0 -o empty -e empty mkdir times
	atf_check -s eq:0 -o empty -e empty \
	    -x 'TZ=GMT touch -t 200501010101 times'
	eval $(stat -s times)
	[ ${st_atime} = ${st_mtime} ] || \
	    atf_fail "atime does not match mtime"
	[ ${st_atime} = 1104541260 ] || atf_fail "atime does not match"

	test_unmount
}

atf_test_case chtimes_kqueue
chtimes_kqueue_head() {
	atf_set "descr" "Tests that changing the file times raises" \
	                "NOTE_ATTRIB on it"
	atf_set "require.user" "root"
}
chtimes_kqueue_body() {
	test_mount

	atf_check -s eq:0 -o empty -e empty mkdir timesq
	echo 'touch timesq' | kqueue_monitor 1 timesq
	kqueue_check timesq NOTE_ATTRIB

	test_unmount
}

atf_init_test_cases() {
	. $(atf_get_srcdir)/../h_funcs.subr
	. $(atf_get_srcdir)/h_funcs.subr

	atf_add_test_case chown
	atf_add_test_case chown_kqueue
	atf_add_test_case chgrp
	atf_add_test_case chgrp_kqueue
	atf_add_test_case chowngrp
	atf_add_test_case chowngrp_kqueue
	atf_add_test_case chmod
	atf_add_test_case chmod_kqueue
	atf_add_test_case chtimes
	atf_add_test_case chtimes_kqueue
}
