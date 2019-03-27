# $NetBSD: t_check.sh,v 1.1 2015/03/29 19:37:02 chopps Exp $
#
# Copyright (c) 2010 The NetBSD Foundation, Inc.
# All rights reserved.
#
# This code is derived from software contributed to The NetBSD Foundation
# by Christian E. Hopps
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

atf_test_case check_grow

check_grow_head() {
	atf_set "descr" "Tests check for room to grow in image"
	atf_set "require.user" "root"
}

check_grow_body() {
	echo "***resize_ffs check grow test"

	atf_check -o ignore -e ignore newfs -V1 -s 6144 -F ${IMG}
	dd if=/dev/zero count=2048 >> ${IMG}

	# test room to grow, grow then check that we did.
	atf_check -s exit:0 -o match:"newsize: 8192 oldsize: 6144" resize_ffs -v -c -y ${IMG}
	atf_check -s exit:0 -o ignore resize_ffs -y ${IMG}
	atf_check -s exit:0 -o ignore fsck_ffs -f -n -F ${IMG}
	atf_check -s exit:1 -o match:"already 8192 blocks" \
	    resize_ffs -v -c -y ${IMG}
}

atf_init_test_cases()
{
	setupvars
	atf_add_test_case check_grow
}
