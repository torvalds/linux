#
# Copyright (c) 2017 Spectra Logic Corporation
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
# $FreeBSD$


atf_test_case max_seek
max_seek_head()
{
	atf_set "descr" "dd(1) can seek by the maximum amount"
}
max_seek_body()
{
	case `df -T . | tail -n 1 | cut -wf 2` in
		"ufs")
			atf_skip "UFS's maximum file size is too small";;
		"zfs") ;; # ZFS is fine
		"tmpfs")
			atf_skip "tmpfs can't create arbitrarily large spare files";;
		*) atf_skip "Unknown file system";;
	esac

	touch f.in
	seek=`echo "2^63 / 4096 - 1" | bc`
	atf_check -s exit:0 -e ignore dd if=f.in of=f.out bs=4096 seek=$seek
}

atf_test_case seek_overflow
seek_overflow_head()
{
	atf_set "descr" "dd(1) should reject too-large seek values"
}
seek_overflow_body()
{
	touch f.in
	seek=`echo "2^63 / 4096" | bc`
	atf_check -s not-exit:0 -e match:"seek offsets cannot be larger than" \
		dd if=f.in of=f.out bs=4096 seek=$seek
	atf_check -s not-exit:0 -e match:"seek offsets cannot be larger than" \
		dd if=f.in of=f.out bs=4096 seek=-1
}

atf_init_test_cases()
{
	atf_add_test_case max_seek
	atf_add_test_case seek_overflow
}
