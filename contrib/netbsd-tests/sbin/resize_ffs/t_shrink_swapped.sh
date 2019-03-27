# $NetBSD: t_shrink_swapped.sh,v 1.2 2015/03/29 19:37:02 chopps Exp $
#
# Copyright (c) 2010 The NetBSD Foundation, Inc.
# All rights reserved.
#
# This code is derived from software contributed to The NetBSD Foundation
# by Jeffrey C. Rizzo.
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


# resize_ffs params as follows:
# resize_ffs blocksize fragsize fssize newfssize level numdata swap
# where 'numdata' is the number of data directories to copy - this is
# determined manually based on the maximum number that will fit in the
# created fs.  'level' is the fs-level (-O 0,1,2) passed to newfs.
# If 'swap' is included, byteswap the fs
test_case shrink_24M_16M_v0_4096 resize_ffs 4096 512 49152 32768 0 41 swap
test_case shrink_24M_16M_v0_8192 resize_ffs 8192 1024 49152 32768 0 42 swap
test_case shrink_24M_16M_v0_16384 resize_ffs 16384 2048 49152 32768 0 43 swap
test_case shrink_24M_16M_v0_32768 resize_ffs 32768 4096 49152 32768 0 42 swap
test_case shrink_24M_16M_v0_65536 resize_ffs 65536 8192 49152 32768 0 38 swap
test_case shrink_32M_24M_v0_4096 resize_ffs 4096 512 65536 49152 0 55 swap
test_case shrink_32M_24M_v0_8192 resize_ffs 8192 1024 65536 49152 0 56 swap
test_case shrink_32M_24M_v0_16384 resize_ffs 16384 2048 65536 49152 0 58 swap
test_case shrink_32M_24M_v0_32768 resize_ffs 32768 4096 65536 49152 0 56 swap
test_case_xfail shrink_32M_24M_v0_65536 "PR bin/44204" resize_ffs 65536 8192 65536 49152 0 51 swap
test_case shrink_48M_16M_v0_4096 resize_ffs 4096 512 98304 32768 0 82 swap
test_case shrink_48M_16M_v0_8192 resize_ffs 8192 1024 98304 32768 0 84 swap
test_case shrink_48M_16M_v0_16384 resize_ffs 16384 2048 98304 32768 0 87 swap
test_case shrink_48M_16M_v0_32768 resize_ffs 32768 4096 98304 32768 0 83 swap
test_case shrink_48M_16M_v0_65536 resize_ffs 65536 8192 98304 32768 0 76 swap
test_case shrink_64M_48M_v0_4096 resize_ffs 4096 512 131072 98304 0 109 swap
test_case shrink_64M_48M_v0_8192 resize_ffs 8192 1024 131072 98304 0 111 swap
test_case shrink_64M_48M_v0_16384 resize_ffs 16384 2048 131072 98304 0 115 swap
test_case shrink_64M_48M_v0_32768 resize_ffs 32768 4096 131072 98304 0 111 swap
test_case shrink_64M_48M_v0_65536 resize_ffs 65536 8192 131072 98304 0 101 swap

test_case shrink_24M_16M_v1_4096 resize_ffs 4096 512 49152 32768 1 41 swap
test_case shrink_24M_16M_v1_8192 resize_ffs 8192 1024 49152 32768 1 42 swap
test_case shrink_24M_16M_v1_16384 resize_ffs 16384 2048 49152 32768 1 43 swap
test_case shrink_24M_16M_v1_32768 resize_ffs 32768 4096 49152 32768 1 42 swap
test_case shrink_24M_16M_v1_65536 resize_ffs 65536 8192 49152 32768 1 38 swap
test_case shrink_32M_24M_v1_4096 resize_ffs 4096 512 65536 49152 1 55 swap
test_case shrink_32M_24M_v1_8192 resize_ffs 8192 1024 65536 49152 1 56 swap
test_case shrink_32M_24M_v1_16384 resize_ffs 16384 2048 65536 49152 1 58 swap
test_case shrink_32M_24M_v1_32768 resize_ffs 32768 4096 65536 49152 1 56 swap
test_case_xfail shrink_32M_24M_v1_65536 "PR bin/44204" resize_ffs 65536 8192 65536 49152 1 51 swap
test_case shrink_48M_16M_v1_4096 resize_ffs 4096 512 98304 32768 1 82 swap
test_case shrink_48M_16M_v1_8192 resize_ffs 8192 1024 98304 32768 1 84 swap
test_case shrink_48M_16M_v1_16384 resize_ffs 16384 2048 98304 32768 1 87 swap
test_case shrink_48M_16M_v1_32768 resize_ffs 32768 4096 98304 32768 1 83 swap
test_case shrink_48M_16M_v1_65536 resize_ffs 65536 8192 98304 32768 1 76 swap
test_case shrink_64M_48M_v1_4096 resize_ffs 4096 512 131072 98304 1 109 swap
test_case shrink_64M_48M_v1_8192 resize_ffs 8192 1024 131072 98304 1 111 swap
test_case shrink_64M_48M_v1_16384 resize_ffs 16384 2048 131072 98304 1 115 swap
test_case shrink_64M_48M_v1_32768 resize_ffs 32768 4096 131072 98304 1 111 swap
test_case shrink_64M_48M_v1_65536 resize_ffs 65536 8192 131072 98304 1 101 swap

test_case_xfail shrink_24M_16M_v2_4096 "PR bin/44205" resize_ffs 4096 512 49152 32768 2 41 swap
test_case_xfail shrink_24M_16M_v2_8192 "PR bin/44205" resize_ffs 8192 1024 49152 32768 2 42 swap
test_case_xfail shrink_24M_16M_v2_16384 "PR bin/44205" resize_ffs 16384 2048 49152 32768 2 43 swap
test_case_xfail shrink_24M_16M_v2_32768 "PR bin/44205" resize_ffs 32768 4096 49152 32768 2 42 swap
test_case_xfail shrink_24M_16M_v2_65536 "PR bin/44205" resize_ffs 65536 8192 49152 32768 2 38 swap
test_case_xfail shrink_32M_24M_v2_4096 "PR bin/44205" resize_ffs 4096 512 65536 49152 2 55 swap
test_case_xfail shrink_32M_24M_v2_8192 "PR bin/44205" resize_ffs 8192 1024 65536 49152 2 56 swap
test_case_xfail shrink_32M_24M_v2_16384 "PR bin/44205" resize_ffs 16384 2048 65536 49152 2 58 swap
test_case_xfail shrink_32M_24M_v2_32768 "PR bin/44205" resize_ffs 32768 4096 65536 49152 2 56 swap
test_case_xfail shrink_32M_24M_v2_65536 "PR bin/44204" resize_ffs 65536 8192 65536 49152 2 51 swap
test_case_xfail shrink_48M_16M_v2_4096 "PR bin/44205" resize_ffs 4096 512 98304 32768 2 82 swap
test_case_xfail shrink_48M_16M_v2_8192 "PR bin/44205" resize_ffs 8192 1024 98304 32768 2 84 swap
test_case_xfail shrink_48M_16M_v2_16384 "PR bin/44205" resize_ffs 16384 2048 98304 32768 2 87 swap
test_case_xfail shrink_48M_16M_v2_32768 "PR bin/44205" resize_ffs 32768 4096 98304 32768 2 83 swap
test_case_xfail shrink_48M_16M_v2_65536 "PR bin/44205" resize_ffs 65536 8192 98304 32768 2 76 swap
test_case_xfail shrink_64M_48M_v2_4096 "PR bin/44205" resize_ffs 4096 512 131072 98304 2 109 swap
test_case_xfail shrink_64M_48M_v2_8192 "PR bin/44205" resize_ffs 8192 1024 131072 98304 2 111 swap
test_case_xfail shrink_64M_48M_v2_16384 "PR bin/44205" resize_ffs 16384 2048 131072 98304 2 115 swap
test_case_xfail shrink_64M_48M_v2_32768 "PR bin/44205" resize_ffs 32768 4096 131072 98304 2 111 swap
test_case_xfail shrink_64M_48M_v2_65536 "PR bin/44205" resize_ffs 65536 8192 131072 98304 2 101 swap


atf_test_case shrink_ffsv1_partial_cg
shrink_ffsv1_partial_cg_head()
{
	atf_set "descr" "Checks successful shrinkage of ffsv1 by" \
			"less than a cylinder group"
}
shrink_ffsv1_partial_cg_body()
{
	echo "*** resize_ffs shrinkage partial cg test"

	atf_check -o ignore -e ignore newfs -V1 -F -s 5760 ${IMG}

	# shrink so there's a partial cg at the end
	atf_check -s exit:0 resize_ffs -c -s 4000 -y ${IMG}
	atf_check -s exit:0 resize_ffs -s 4000 -y ${IMG}
	atf_check -s exit:0 -o ignore fsck_ffs -f -n -F ${IMG}
	atf_check -s exit:1 resize_ffs -c -s 4000 -y ${IMG}
}

atf_init_test_cases()
{
	setupvars
	atf_add_test_case shrink_24M_16M_v0_4096
	atf_add_test_case shrink_24M_16M_v1_8192
	atf_add_test_case shrink_24M_16M_v2_16384
if [ "${RESIZE_FFS_ALL_TESTS-X}" != "X" ]; then
	atf_add_test_case shrink_24M_16M_v0_8192
	atf_add_test_case shrink_24M_16M_v0_16384
	atf_add_test_case shrink_24M_16M_v0_32768
	atf_add_test_case shrink_24M_16M_v0_65536
	atf_add_test_case shrink_24M_16M_v1_4096
	atf_add_test_case shrink_24M_16M_v1_16384
	atf_add_test_case shrink_24M_16M_v1_32768
	atf_add_test_case shrink_24M_16M_v1_65536
	atf_add_test_case shrink_24M_16M_v2_4096
	atf_add_test_case shrink_24M_16M_v2_8192
	atf_add_test_case shrink_24M_16M_v2_32768
	atf_add_test_case shrink_24M_16M_v2_65536
	atf_add_test_case shrink_32M_24M_v0_4096
	atf_add_test_case shrink_32M_24M_v0_8192
	atf_add_test_case shrink_32M_24M_v0_16384
	atf_add_test_case shrink_32M_24M_v0_32768
	atf_add_test_case shrink_32M_24M_v0_65536
	atf_add_test_case shrink_48M_16M_v0_4096
	atf_add_test_case shrink_48M_16M_v0_8192
	atf_add_test_case shrink_48M_16M_v0_16384
	atf_add_test_case shrink_48M_16M_v0_32768
	atf_add_test_case shrink_48M_16M_v0_65536
	atf_add_test_case shrink_64M_48M_v0_4096
	atf_add_test_case shrink_64M_48M_v0_8192
	atf_add_test_case shrink_64M_48M_v0_16384
	atf_add_test_case shrink_64M_48M_v0_32768
	atf_add_test_case shrink_64M_48M_v0_65536
	atf_add_test_case shrink_32M_24M_v1_4096
	atf_add_test_case shrink_32M_24M_v1_8192
	atf_add_test_case shrink_32M_24M_v1_16384
	atf_add_test_case shrink_32M_24M_v1_32768
	atf_add_test_case shrink_32M_24M_v1_65536
	atf_add_test_case shrink_48M_16M_v1_4096
	atf_add_test_case shrink_48M_16M_v1_8192
	atf_add_test_case shrink_48M_16M_v1_16384
	atf_add_test_case shrink_48M_16M_v1_32768
	atf_add_test_case shrink_48M_16M_v1_65536
	atf_add_test_case shrink_64M_48M_v1_4096
	atf_add_test_case shrink_64M_48M_v1_8192
	atf_add_test_case shrink_64M_48M_v1_16384
	atf_add_test_case shrink_64M_48M_v1_32768
	atf_add_test_case shrink_64M_48M_v1_65536
	atf_add_test_case shrink_32M_24M_v2_4096
	atf_add_test_case shrink_32M_24M_v2_8192
	atf_add_test_case shrink_32M_24M_v2_16384
	atf_add_test_case shrink_32M_24M_v2_32768
	atf_add_test_case shrink_32M_24M_v2_65536
	atf_add_test_case shrink_48M_16M_v2_4096
	atf_add_test_case shrink_48M_16M_v2_8192
	atf_add_test_case shrink_48M_16M_v2_16384
	atf_add_test_case shrink_48M_16M_v2_32768
	atf_add_test_case shrink_48M_16M_v2_65536
	atf_add_test_case shrink_64M_48M_v2_4096
	atf_add_test_case shrink_64M_48M_v2_8192
	atf_add_test_case shrink_64M_48M_v2_16384
	atf_add_test_case shrink_64M_48M_v2_32768
	atf_add_test_case shrink_64M_48M_v2_65536
fi
	atf_add_test_case shrink_ffsv1_partial_cg
}
