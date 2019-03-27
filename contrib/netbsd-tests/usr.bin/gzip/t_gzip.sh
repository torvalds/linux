# $NetBSD: t_gzip.sh,v 1.1 2012/03/17 16:33:13 jruoho Exp $
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

atf_test_case concatenated
concatenated_head()
{
	atf_set "descr" "Checks concatenated gzipped data"
}
concatenated_body()
{
	echo -n "aaaa" | gzip > tmp.gz
	echo -n "bbbb" | gzip >> tmp.gz

	atf_check -o inline:"aaaabbbb" gzip -d tmp.gz -c
}

atf_test_case pipe
pipe_head()
{
	atf_set "descr" "Checks input from pipe"
}
pipe_body()
{
	atf_check -x "dd if=/dev/zero count=102400 2>/dev/null \
| gzip -c | dd bs=1 2>/dev/null | gzip -tc"
}

atf_test_case truncated
truncated_head()
{
	atf_set "descr" "Checks that gzip fails on truncated data"
}
truncated_body()
{
	cat >truncated.gz.uue <<EOF
begin-base64 644 truncated.gz
H4sIAAAAAAAAA0tMSk7hAgCspIpYCg==
====
EOF
	uudecode -m truncated.gz.uue

	atf_check -s ne:0 -e ignore gzip -d truncated.gz
}

atf_test_case crcerror
crcerror_head()
{
	atf_set "descr" "Checks that gzip fails on crc error"
}
crcerror_body()
{
	cat >crcerror.gz.uue <<EOF
begin-base64 644 crcerror.gz
H4sIAAAAAAAAA0tMSk7hAgCspFhYBQAAAA==
====
EOF
	uudecode -m crcerror.gz.uue

	atf_check -s ne:0 -e ignore gzip -d crcerror.gz
}

atf_test_case good
good_head()
{
	atf_set "descr" "Checks decompressing correct file"
}
good_body()
{
	cat >good.gz.uue <<EOF
begin-base64 644 good.gz
H4sICC8G8UAAA2FiY2QAS0xKTuECAKykilgFAAAA
====
EOF
	uudecode -m good.gz.uue

	atf_check gzip -d good.gz
}

atf_init_test_cases()
{
	atf_add_test_case concatenated
	atf_add_test_case pipe
	atf_add_test_case truncated
	atf_add_test_case crcerror
	atf_add_test_case good
}
