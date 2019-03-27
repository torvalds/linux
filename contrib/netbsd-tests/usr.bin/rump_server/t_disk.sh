#	$NetBSD: t_disk.sh,v 1.5 2013/02/19 21:08:25 joerg Exp $
#
# Copyright (c) 2011 The NetBSD Foundation, Inc.
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

server='rump_server -lrumpvfs'
export RUMP_SERVER='unix://commsock'

startsrv()
{

	atf_check -s exit:0 ${server} $@ ${RUMP_SERVER}
}

test_case()
{
	local name="${1}"; shift

	atf_test_case "${name}" cleanup
	eval "${name}_head() {  \
		atf_set "require.progs" "rump_server" ; \
	}"
	eval "${name}_body() { \
		${name}_prefun ; \
		startsrv $@ ; \
		${name} ; \
	}"
	eval "${name}_cleanup() { \
		[ -f halted ] && return 0 ; rump.halt ;
	}"
}

test_case size -d key=/img,hostpath=the.img,size=32k
size()
{
	atf_check -s exit:0 -o inline:'32768\n' stat -f %z the.img
}

test_case offset -d key=/img,hostpath=the.img,size=32k,offset=16k
offset()
{
	atf_check -s exit:0 -o inline:'49152\n' stat -f %z the.img
}

test_case notrunc -d key=/img,hostpath=the.img,size=8k,offset=16k
notrunc_prefun()
{
	dd if=/dev/zero of=the.img bs=1 oseek=65535 count=1 
}
notrunc()
{
	atf_check -s exit:0 -o inline:'65536\n' stat -f %z the.img
}

test_case data -d key=/img,hostpath=the.img,size=8k,offset=16k
data()
{
	echo 'test string' | dd of=testfile ibs=512 count=1 conv=sync
	atf_check -s exit:0 -e ignore -x \
	    "dd if=testfile | rump.dd of=/img bs=512 count=1"

	# cheap fsync
	atf_check -s exit:0 rump.halt
	touch halted
	atf_check -s exit:0 -e ignore -o file:testfile \
	    dd if=the.img iseek=16k bs=1 count=512
}

test_case type_chr -d key=/img,hostpath=the.img,size=32k,type=chr
type_chr()
{
	atf_check -s exit:0 -o inline:'Character Device\n' \
	    env LD_PRELOAD=/usr/lib/librumphijack.so stat -f %HT /rump/img
}

test_case type_reg -d key=/img,hostpath=the.img,size=32k,type=reg
type_reg()
{
	atf_check -s exit:0 -o inline:'Regular File\n' \
	    env LD_PRELOAD=/usr/lib/librumphijack.so stat -f %HT /rump/img
}

test_case type_blk -d key=/img,hostpath=the.img,size=32k,type=blk
type_blk()
{
	atf_check -s exit:0 -o inline:'Block Device\n' \
	    env LD_PRELOAD=/usr/lib/librumphijack.so stat -f %HT /rump/img
}

test_case type_blk_default -d key=/img,hostpath=the.img,size=32k
type_blk_default()
{
	atf_check -s exit:0 -o inline:'Block Device\n' \
	    env LD_PRELOAD=/usr/lib/librumphijack.so stat -f %HT /rump/img
}

atf_init_test_cases()
{

	atf_add_test_case size
	atf_add_test_case offset
	atf_add_test_case notrunc
	atf_add_test_case data
	atf_add_test_case type_chr
	atf_add_test_case type_reg
	atf_add_test_case type_blk
	atf_add_test_case type_blk_default
}
