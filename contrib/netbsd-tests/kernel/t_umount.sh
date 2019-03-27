# $NetBSD: t_umount.sh,v 1.5 2010/11/07 17:51:19 jmmv Exp $
#
# Copyright (c) 2008, 2009 The NetBSD Foundation, Inc.
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

TMPMP=umount-f_mount
TMPIM=umount-f.im

VND=vnd0
BVND=/dev/${VND}
CVND=/dev/r${VND}
MPART=a

atf_test_case umount cleanup
umount_head()
{
	atf_set "descr" "Checks forced unmounting"
	atf_set "require.user" "root"
}
umount_body()
{
	cat >disktab <<EOF
floppy288|2.88MB 3.5in Extra High Density Floppy:\
	:ty=floppy:se#512:nt#2:rm#300:ns#36:nc#80:\
	:pa#5760:oa#0:ba#4096:fa#512:ta=4.2BSD:\
	:pb#5760:ob#0:\
	:pc#5760:oc#0:
EOF

	echo "*** Creating a dummy directory tree at" \
	     "${TMPMP} mounted on ${TMPIM}"

	atf_check -o ignore -e ignore mkdir ${TMPMP}
	atf_check -o ignore -e ignore touch ${TMPMP}/under_the_mount
	atf_check -o ignore -e ignore dd if=/dev/zero of=${TMPIM} count=5860
	atf_check -o ignore -e ignore vnconfig -v ${VND} ${TMPIM}
	atf_check -o ignore -e ignore disklabel -f disktab -rw ${VND} floppy288
	atf_check -o ignore -e ignore newfs -i 500 -b 8192 -f 1024 ${CVND}${MPART}
	atf_check -o ignore -e ignore mount -o async ${BVND}${MPART} ${TMPMP}
	atf_check -o ignore -e ignore touch ${TMPMP}/in_mounted_directory

	echo "*** Testing forced unmount"
	test -e "${TMPMP}/in_mounted_directory" || \
	    atf_fail "Test file not present in mounted directory!"

	mydir="`pwd`"
	cd "${TMPMP}"
	atf_check -o ignore -e ignore umount -f "${BVND}${MPART}"

	atf_check -s ne:0 -e inline:"ls: .: No such file or directory\n" ls .
	atf_check -s ne:0 -e inline:"ls: ..: No such file or directory\n" ls ..

	atf_check -s ne:0 -e ignore -o inline:"cd: can't cd to .\n" \
	    -x "cd . 2>&1"
	atf_check -s ne:0 -e ignore -o inline:"cd: can't cd to ..\n" \
	    -x "cd .. 2>&1"

	cd "${mydir}"

	test -e "${TMPMP}/under_the_mount" || \
	    atf_fail "Original mount point dissapeared!"
}
umount_cleanup()
{
	echo "*** Cleaning up ${TMPMP}, ${TMPIM}."
	umount -f "${TMPMP}"
	vnconfig -u "${VND}"
}

atf_init_test_cases()
{
	atf_add_test_case umount
}
