# $NetBSD: t_enable_quotas.sh,v 1.2 2011/03/06 17:08:41 bouyer Exp $ 
#
#  Copyright (c) 2011 Manuel Bouyer
#  All rights reserved.
# 
#  Redistribution and use in source and binary forms, with or without
#  modification, are permitted provided that the following conditions
#  are met:
#  1. Redistributions of source code must retain the above copyright
#     notice, this list of conditions and the following disclaimer.
#  2. Redistributions in binary form must reproduce the above copyright
#     notice, this list of conditions and the following disclaimer in the
#     documentation and/or other materials provided with the distribution.
# 
#  THIS SOFTWARE IS PROVIDED BY THE NETBSD FOUNDATION, INC. AND CONTRIBUTORS
#  ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED
#  TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
#  PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE FOUNDATION OR CONTRIBUTORS
#  BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
#  CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
#  SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
#  INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
#  CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
#  ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
#  POSSIBILITY OF SUCH DAMAGE.
#

for e in le be; do
  for v in 1 2; do
    test_case disable_${e}_${v} disable_quotas "creation/removal of" ${e} ${v}
    test_case corrupt_${e}_${v} corrupt_quotas "repair of corrupted" ${e} ${v}
    test_case unallocated_${e}_${v} unallocated_quotas \
		"recovery of unallocated" ${e} ${v}
    test_case dir1_${e}_${v} dir1_quotas \
		"successfull clear of wrong type of" ${e} ${v}
    test_case notreg_${e}_${v} notreg_quotas \
		"successfull clear of wrong type of" ${e} ${v}
  done
done

disable_quotas()
{
	create_with_quotas $*
	
# check that the quota inode creation didn't corrupt the filesystem
	atf_check -s exit:0 -o "match:already clean" -o  "match:3 files" \
		fsck_ffs -nf -F ${IMG}
#now check fsck can properly clear the quota inode when quota flags are
# cleared
	atf_check -o ignore -e ignore tunefs -q nouser -q nogroup -F ${IMG}
	atf_check -s exit:0 -o "match:SUPERBLOCK QUOTA FLAG CLEARED" \
		fsck_ffs -fp -F ${IMG}
	atf_check -s exit:0 -o "match:1 files, 1 used" fsck_ffs -nf -F ${IMG}
}

corrupt_quotas()
{
	create_with_quotas $*

	local blkno=$(printf "inode 3\nblks\n" | /sbin/fsdb -nF -f ${IMG} | awk '$1 == "0:" {print $2}')
	atf_check -o ignore -e ignore dd if=/dev/zero of=${IMG} bs=512 \
		count=1 seek=${blkno} conv=notrunc
	atf_check -s exit:0 \
		-o "match:CORRUPTED USER QUOTA INODE 3 \(CLEARED\)" \
		-o "match:NO USER QUOTA INODE \(CREATED\)" \
		fsck_ffs -fp -F ${IMG}
	atf_check -s exit:0 -o "match:3 files" fsck_ffs -nf -F ${IMG}
}

unallocated_quotas()
{
	create_with_quotas $*

	atf_check -o ignore -e ignore clri ${IMG} 3
	atf_check -s exit:0 \
		-o "match:UNALLOCATED USER QUOTA INODE 3 \(CLEARED\)" \
		-o "match:NO USER QUOTA INODE \(CREATED\)" \
		fsck_ffs -fp -F ${IMG}
	atf_check -s exit:0 -o "match:3 files" fsck_ffs -nf -F ${IMG}
}

dir1_quotas()
{
	create_with_quotas $*

	atf_check -s exit:255 -o ignore -e ignore -x \
		"printf 'inode 3\nchtype dir\nexit\n' | fsdb -F -f ${IMG}"
	atf_check -s exit:0 \
		-o "match:DIR I=3 CONNECTED. PARENT WAS I=0" \
		-o "match:USER QUOTA INODE 3 IS A DIRECTORY" \
		fsck_ffs -y -F ${IMG}
}

notreg_quotas()
{
	create_with_quotas $*

	atf_check -s exit:255 -o ignore -e ignore -x \
		"printf 'inode 3\nchtype fifo\nexit\n' | fsdb -F -f ${IMG}"
	atf_check -s exit:0 \
		-o "match:WRONG TYPE 4096 for USER QUOTA INODE 3 \(CLEARED\)" \
		-o "match:NO USER QUOTA INODE \(CREATED\)" \
		fsck_ffs -p -F ${IMG}
	atf_check -s exit:0 -o "match:3 files" fsck_ffs -nf -F ${IMG}
}
