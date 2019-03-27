#	$NetBSD: t_cgd.sh,v 1.11 2013/02/19 21:08:24 joerg Exp $
#
# Copyright (c) 2010 The NetBSD Foundation, Inc.
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

rawpart=`sysctl -n kern.rawpartition | tr '01234' 'abcde'`
rawcgd=/dev/rcgd0${rawpart}
cgdserver=\
"rump_server -lrumpvfs -lrumpkern_crypto -lrumpdev -lrumpdev_disk -lrumpdev_cgd"

atf_test_case basic cleanup
basic_head()
{

	atf_set "descr" "Tests that encrypt/decrypt works"
	atf_set "require.progs" "rump_server"
}

basic_body()
{

	d=$(atf_get_srcdir)
	atf_check -s exit:0 \
	    ${cgdserver} -d key=/dev/dk,hostpath=dk.img,size=1m unix://csock

	export RUMP_SERVER=unix://csock
	atf_check -s exit:0 -x "echo 12345 | \
	    rump.cgdconfig -p cgd0 /dev/dk ${d}/paramsfile"
	atf_check -s exit:0 -e ignore -x \
	    "dd if=${d}/t_cgd count=2 | rump.dd of=${rawcgd}"
	atf_check -s exit:0 -e ignore dd if=${d}/t_cgd of=testfile count=2
	atf_check -s exit:0 -e ignore -o file:testfile \
	    rump.dd if=${rawcgd} count=2
}

basic_cleanup()
{

	env RUMP_SERVER=unix://csock rump.halt || true
}

atf_test_case wrongpass cleanup
wrongpass_head()
{

	atf_set "descr" "Tests that wrong password does not give original " \
	    "plaintext"
	atf_set "require.progs" "rump_server"
}

wrongpass_body()
{

	d=$(atf_get_srcdir)
	atf_check -s exit:0 \
	    ${cgdserver} -d key=/dev/dk,hostpath=dk.img,size=1m unix://csock

	export RUMP_SERVER=unix://csock
	atf_check -s exit:0 -x "echo 12345 | \
	    rump.cgdconfig -p cgd0 /dev/dk ${d}/paramsfile"
	atf_check -s exit:0 -e ignore -x \
	    "dd if=${d}/t_cgd | rump.dd of=${rawcgd} count=2"

	# unconfig and reconfig cgd
	atf_check -s exit:0 rump.cgdconfig -u cgd0
	atf_check -s exit:0 -x "echo 54321 | \
	    rump.cgdconfig -p cgd0 /dev/dk ${d}/paramsfile"

	atf_check -s exit:0 -e ignore dd if=${d}/t_cgd of=testfile count=2
	atf_check -s exit:0 -e ignore -o not-file:testfile \
	    rump.dd if=${rawcgd} count=2
}

wrongpass_cleanup()
{

	env RUMP_SERVER=unix://csock rump.halt || true
}


atf_test_case unaligned_write cleanup
unaligned_write_head()
{

	atf_set "descr" "Attempt unaligned writes to a raw cgd device"
	atf_set "require.progs" "rump_server"
}

unaligned_write_body()
{
	d=$(atf_get_srcdir)
	atf_check -s exit:0 \
	    ${cgdserver} -d key=/dev/dk,hostpath=dk.img,size=1m unix://csock

	export RUMP_SERVER=unix://csock
	atf_check -s exit:0 -x "echo 12345 | \
	    rump.cgdconfig -p cgd0 /dev/dk ${d}/paramsfile"

	# Check that cgd rejects writes of totally bogus lengths.
	atf_check -s not-exit:0 -e ignore -x \
	    "echo die hard | rump.dd of=${rawcgd} bs=123 conv=sync"

	# Check that cgd rejects non-sector-length writes even if they
	# are integral multiples of the block size.
	atf_check -s not-exit:0 -e ignore -x \
	    "echo die hard | rump.dd of=${rawcgd} bs=64 conv=sync"
	atf_check -s not-exit:0 -e ignore -x \
	    "echo die hard | rump.dd of=${rawcgd} bs=256 conv=sync"

	# Check that cgd rejects misaligned buffers, produced by
	# packetizing the input on bogus boundaries and using the
	# bizarre behaviour of `bs=N' in dd.
	atf_check -s not-exit:0 -e ignore -x \
	    "(echo -n x && sleep 1 && head -c 511 </dev/zero) \
		| rump.dd of=${rawcgd} bs=512"

	# Check that cgd rejects sector-length writes if they are not
	# on sector boundaries.  Doesn't work because dd can't be
	# persuaded to seek a non-integral multiple of the output
	# buffer size and I can't be arsed to find the another way to
	# do that.
	#atf_check -s not-exit:0 -e ignore -x \
	#    "echo die hard | rump.dd of=${rawcgd} seek=1 bs=512 conv=sync"
}

unaligned_write_cleanup()
{
	env RUMP_SERVER=unix://csock rump.halt || true
}

atf_init_test_cases()
{

	atf_add_test_case basic
	atf_add_test_case wrongpass
	atf_add_test_case unaligned_write
}
