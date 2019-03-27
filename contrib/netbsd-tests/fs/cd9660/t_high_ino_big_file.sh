# $NetBSD: t_high_ino_big_file.sh,v 1.4 2014/07/07 22:06:02 pgoyette Exp $
#
# Copyright (c) 2014 The NetBSD Foundation, Inc.
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

# The image used in these tests has been provided by Thomas Schmitt under
# the following license (see PR kern/48787 for details how to recreate it):
#
# Copyright (c) 1999 - 2008, Thomas Schmitt (scdbackup@gmx.net)
#
# Redistribution and use in source and binary forms, with or without
# modification, are permitted provided that the following conditions are met:
#
# Redistributions of source code must retain the above copyright notice,
# this list of conditions and the following disclaimer.
#
# Redistributions in binary form must reproduce the above copyright notice,
# this list of conditions and the following disclaimer in the documentation
# and/or other materials provided with the distribution.
#
# Neither the name of Thomas Schmitt nor the names of his contributors
# may be used to endorse or promote products derived from this software without
# specific prior written permission. 
#
#       THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND
#       CONTRIBUTORS ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES,
#       INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF
#       MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
#       DISCLAIMED. IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE
#       LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
#       CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
#       PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
#       PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
#       THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
#       (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE
#       USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH
#       DAMAGE. 
#
# ------------------------------------------------------------------------
# This is the BSD license as stated July 22 1999 with
#  <OWNER>="Thomas Schmitt (scdbackup@gmx.net)",
#  <ORGANIZATION>="Thomas Schmitt" and <YEAR>="1999"
# an Open Source license approved by opensource.org
#

mntpnt=""

atf_test_case pr_kern_48787 cleanup
pr_kern_48787_head() {
	atf_set "descr" "Verifies 32bit overflow isssues from PR kern/48787 are fixed"
	atf_set "require.user" "root"
	atf_set "require.progs" "rump_cd9660 bunzip2 stat"
	atf_set "timeout" 6000
}

pr_kern_48787_body() {
	avail=$( df -Pk . | awk '{if (NR==2) print $4}' )
	if [ $avail -lt 4500000 ]; then
		atf_skip "not enough free disk space, have ${avail} Kbytes, need ~ 4500000 Kbytes"
	fi
	bunzip2 < $(atf_get_srcdir)/pr_48787.image.bz2 > pr_48787.image
	mntpnt=$(pwd)/mnt
	mkdir ${mntpnt}
	rump_cd9660 -o norrip ./pr_48787.image ${mntpnt}
	if [ ! -r ${mntpnt}/small_file ]; then
		atf_fail "${mntpnt}/small_file does not exist"
	fi
	if [ ! -r ${mntpnt}/my/large_file ]; then
		atf_fail "${mntpnt}/my/large_file does not exist"
	fi
	umount ${mntpnt}
	rump_cd9660 ./pr_48787.image ${mntpnt}
	if [ ! -r ${mntpnt}/small_file ]; then
		atf_fail "${mntpnt}/small_file does not exist"
	fi
	if [ ! -r ${mntpnt}/my/large_file ]; then
		atf_fail "${mntpnt}/my/large_file does not exist"
	fi
	echo "this assumes current cd9660 inode encoding - adapt on changes"
	atf_check -o match:"^4329541966$" stat -f "%i" ${mntpnt}/small_file
	atf_check -o match:"^4329545920$" stat -f "%i" ${mntpnt}/my/large_file
	umount ${mntpnt}
	touch "done"
}

pr_kern_48787_cleanup() {
	if [ ! -f done ]; then
		if [ "x${mntpnt}" != "x" ]; then
			umount -f ${mntpnt} || true
		fi
	fi
}

atf_init_test_cases() {
	atf_add_test_case pr_kern_48787
}
