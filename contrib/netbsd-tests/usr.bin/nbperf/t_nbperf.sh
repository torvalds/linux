# $NetBSD: t_nbperf.sh,v 1.3 2014/04/30 21:04:21 joerg Exp $
#
# Copyright (c) 2012 The NetBSD Foundation, Inc.
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

cleanup()
{
	rm -f reference.txt hash.c hash.map testprog
}

atf_test_case chm
chm_head()
{
	atf_set "descr" "Checks chm algorithm"
	atf_set "require.files" "/usr/share/dict/web2"
	atf_set "require.progs" "cc"
}
chm_body()
{ 
	for n in 4 32 128 1024 65536; do
		seq 0 $(($n - 1)) > reference.txt
		atf_check -o file:reference.txt \
		    $(atf_get_srcdir)/h_nbperf /usr/share/dict/web2 chm cat \
		    $n $(atf_get_srcdir)/hash_driver.c
		atf_check -o file:hash.map \
		    $(atf_get_srcdir)/h_nbperf /usr/share/dict/web2 chm cat \
		    $n $(atf_get_srcdir)/hash_driver.c
	done
}
chm_clean()
{
	cleanup
}

atf_test_case chm3
chm3_head()
{
	atf_set "descr" "Checks chm3 algorithm"
	atf_set "require.files" "/usr/share/dict/web2"
	atf_set "require.progs" "cc"
}
chm3_body()
{ 
	for n in 4 32 128 1024 65536; do
		seq 0 $(($n - 1)) > reference.txt
		atf_check -o file:reference.txt \
		    $(atf_get_srcdir)/h_nbperf /usr/share/dict/web2 chm3 cat \
		    $n $(atf_get_srcdir)/hash_driver.c
		atf_check -o file:hash.map \
		    $(atf_get_srcdir)/h_nbperf /usr/share/dict/web2 chm3 cat \
		    $n $(atf_get_srcdir)/hash_driver.c
	done
}
chm3_clean()
{
	cleanup
}

atf_test_case bdz
bdz_head()
{
	atf_set "descr" "Checks bdz algorithm"
	atf_set "require.files" "/usr/share/dict/web2"
	atf_set "require.progs" "cc"
}
bdz_body()
{ 
	for n in 4 32 128 1024 65536 131072; do
		seq 0 $(($n - 1)) > reference.txt
		atf_check -o file:reference.txt \
		    $(atf_get_srcdir)/h_nbperf /usr/share/dict/web2 bdz "sort -n" \
		    $n $(atf_get_srcdir)/hash_driver.c
		atf_check -o file:hash.map \
		    $(atf_get_srcdir)/h_nbperf /usr/share/dict/web2 bdz cat \
		    $n $(atf_get_srcdir)/hash_driver.c
	done
}
bdz_clean()
{
	cleanup
}

atf_init_test_cases()
{
	atf_add_test_case chm
	atf_add_test_case chm3
	atf_add_test_case bdz
}
