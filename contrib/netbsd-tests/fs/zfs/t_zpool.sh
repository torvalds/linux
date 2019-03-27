#	$NetBSD: t_zpool.sh,v 1.3 2011/12/06 18:18:59 njoly Exp $
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

server='rump_server -lrumpvfs -lrumpkern_solaris -lrumpfs_zfs -lrumpdev -lrumpdev_rnd -d key=/dk,hostpath=zfs.img,size=100m'

export RUMP_SERVER=unix://zsuck

atf_test_case create cleanup
create_head()
{
	atf_set "descr" "basic zpool create"
}

IFS=' '
exmount='rumpfs on / type rumpfs (local)
jippo on /jippo type zfs (local)
'

create_body()
{

	atf_check -s exit:0 -o ignore -e ignore ${server} ${RUMP_SERVER}

	export LD_PRELOAD=/usr/lib/librumphijack.so
	export RUMPHIJACK=blanket=/dev/zfs:/dk:/jippo
	atf_check -s exit:0 zpool create jippo /dk

	export RUMPHIJACK=vfs=all
	atf_check -s exit:0 -o inline:"${exmount}" mount
}

create_cleanup()
{

	rump.halt
}

atf_init_test_cases()
{

	atf_add_test_case create
}
