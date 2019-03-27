#       $NetBSD: t_vfs.sh,v 1.6 2012/08/04 03:56:47 riastradh Exp $
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

img=ffs.img
rumpsrv_ffs=\
"rump_server -lrumpvfs -lrumpfs_ffs -lrumpdev_disk -d key=/img,hostpath=${img},size=host"
export RUMP_SERVER=unix://csock

domount()
{

	mntdir=$1
	[ $# -eq 0 ] && mntdir=/rump/mnt
	atf_check -s exit:0 -e ignore mount_ffs /img ${mntdir}
}

dounmount()
{

	atf_check -s exit:0 umount -R ${mntdir}
}

remount()
{

	dounmount
	domount /rump/mnt2
}

simpletest()
{
	local name="${1}"; shift

	atf_test_case "${name}" cleanup
	eval "${name}_head() {  }"
	eval "${name}_body() { \
		atf_check -s exit:0 rump_server -lrumpvfs ${RUMP_SERVER} ; \
		export LD_PRELOAD=/usr/lib/librumphijack.so ; \
		${name} " "${@}" "; \
	}"
	eval "${name}_cleanup() { \
		rump.halt
	}"
}

test_case()
{
	local name="${1}"; shift

	atf_test_case "${name}" cleanup
	eval "${name}_head() {  }"
	eval "${name}_body() { \
		atf_check -s exit:0 -o ignore newfs -F -s 20000 ${img} ; \
		atf_check -s exit:0 ${rumpsrv_ffs} ${RUMP_SERVER} ; \
		export LD_PRELOAD=/usr/lib/librumphijack.so ; \
		mkdir /rump/mnt /rump/mnt2 ; \
		domount ; \
		${name} " "${@}" "; \
		dounmount ${mntdir}
	}"
	eval "${name}_cleanup() { \
		rump.halt
	}"
}

test_case paxcopy
test_case cpcopy
test_case mv_nox
test_case ln_nox

#
# use rumphijack to cp/pax stuff onto an image, unmount it, remount it
# at a different location, and check that we have an identical copy
# (we make a local copy to avoid the minor possibility that someone
# modifies the source dir data while the test is running)
#
paxcopy()
{
	parent=$(dirname $(atf_get_srcdir))
	thedir=$(basename $(atf_get_srcdir))
	atf_check -s exit:0 pax -rw -s,${parent},, $(atf_get_srcdir) .
	atf_check -s exit:0 pax -rw ${thedir} /rump/mnt
	remount
	atf_check -s exit:0 diff -ru ${thedir} /rump/mnt2/${thedir}
}

cpcopy()
{
	thedir=$(basename $(atf_get_srcdir))
	atf_check -s exit:0 cp -Rp $(atf_get_srcdir) .
	atf_check -s exit:0 cp -Rp ${thedir} /rump/mnt
	remount
	atf_check -s exit:0 diff -ru ${thedir} /rump/mnt2/${thedir}
}

#
# non-crosskernel mv (non-simple test since this uses rename(2)
# which is not supported by rumpfs)
#

mv_nox()
{
	# stat default format sans changetime and filename
	statstr='%d %i %Sp %l %Su %Sg %r %z \"%Sa\" \"%Sm\" \"%SB\" %k %b %#Xf'

	atf_check -s exit:0 touch /rump/mnt/filename
	atf_check -s exit:0 -o save:stat.out \
	    stat -f "${statstr}" /rump/mnt/filename
	atf_check -s exit:0 mkdir /rump/mnt/dir
	atf_check -s exit:0 mv /rump/mnt/filename /rump/mnt/dir/same
	atf_check -s exit:0 -o file:stat.out \
	    stat -f "${statstr}" /rump/mnt/dir/same
}

ln_nox()
{
	# Omit st_nlink too, since it will increase.
	statstr='%d %i %Sp %Su %Sg %r %z \"%Sa\" \"%Sm\" \"%SB\" %k %b %#Xf'

	atf_check -s exit:0 touch /rump/mnt/filename
	atf_check -s exit:0 -o save:stat.out \
	    stat -f "${statstr}" /rump/mnt/filename
	atf_check -s exit:0 mkdir /rump/mnt/dir
	atf_check -s exit:0 ln /rump/mnt/filename /rump/mnt/dir/same
	atf_check -s exit:0 -o file:stat.out \
	    stat -f "${statstr}" /rump/mnt/filename
	atf_check -s exit:0 -o file:stat.out \
	    stat -f "${statstr}" /rump/mnt/dir/same
}

simpletest mv_x
simpletest ln_x
simpletest runonprefix
simpletest blanket
simpletest doubleblanket

#
# do a cross-kernel mv
#
mv_x()
{
	thedir=$(basename $(atf_get_srcdir))
	atf_check -s exit:0 cp -Rp $(atf_get_srcdir) .
	atf_check -s exit:0 cp -Rp ${thedir} ${thedir}.2
	atf_check -s exit:0 mv ${thedir} /rump
	atf_check -s exit:0 diff -ru ${thedir}.2 /rump/${thedir}
}

#
# Fail to make a cross-kernel hard link.
#
ln_x()
{
	atf_check -s exit:0 touch ./loser
	atf_check -s not-exit:0 -e ignore ln ./loser /rump/.
}

runonprefix()
{
	atf_check -s exit:0 -o ignore stat /rump/dev
	atf_check -s exit:1 -e ignore stat /rumpdev
}

blanket()
{
	export RUMPHIJACK='blanket=/dev,path=/rump'
	atf_check -s exit:0 -o save:stat.out \
	    stat -f "${statstr}" /rump/dev/null
	atf_check -s exit:0 -o file:stat.out \
	    stat -f "${statstr}" /dev/null
}

doubleblanket()
{
	atf_check -s exit:0 mkdir /rump/dir
	atf_check -s exit:0 ln -s dir /rump/dirtoo

	export RUMPHIJACK='blanket=/dirtoo:/dir'
	atf_check -s exit:0 touch /dir/file

	atf_check -s exit:0 -o save:stat.out \
	    stat -f "${statstr}" /dir/file
	atf_check -s exit:0 -o file:stat.out \
	    stat -f "${statstr}" /dirtoo/file
}

atf_init_test_cases()
{

	atf_add_test_case paxcopy
	atf_add_test_case cpcopy
	atf_add_test_case mv_x
	atf_add_test_case ln_x
	atf_add_test_case mv_nox
	atf_add_test_case ln_nox
	atf_add_test_case runonprefix
	atf_add_test_case blanket
	atf_add_test_case doubleblanket
}
