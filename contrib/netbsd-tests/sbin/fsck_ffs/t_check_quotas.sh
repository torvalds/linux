# $NetBSD: t_check_quotas.sh,v 1.2 2011/03/06 17:08:41 bouyer Exp $ 
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
    test_case corrupt_list_${e}_${v} corrupt_list \
	"recovery of corrupted free list in" ${e} ${v}
    test_case expand1_list_${e}_${v} expand_list \
	"allocation of direct block in" 40 ${e} ${v}
    test_case expand2_list_${e}_${v} expand_list \
	"allocation of indirect block in" 1000 ${e} ${v}
  done
done

corrupt_list()
{
	create_with_quotas $*
	local blkno=$(printf "inode 3\nblks\n" | /sbin/fsdb -nF -f ${IMG} | awk '$1 == "0:" {print $2}')
	blkno=$(($blkno * 512 + 104))
	#clear the free list
	atf_check -o ignore -e ignore dd if=/dev/zero of=${IMG} bs=1 \
		count=8 seek=${blkno} conv=notrunc
	atf_check -s exit:0 \
		-o "match:QUOTA ENTRY NOT IN LIST \(FIXED\)" \
		fsck_ffs -fp -F ${IMG}
	atf_check -s exit:0 -o "match:3 files" fsck_ffs -nf -F ${IMG}
}

expand_list()
{
	local nuid=$1; shift
	local expected_files=$((nuid + 2))
	echo "/set uid=0 gid=0" > spec
	echo ".		type=dir  mode=0755" >> spec
	mkdir ${DIR}
	for i in $(seq ${nuid}); do
		touch ${DIR}/f${i}
		echo "./f$i	type=file mode=0600 uid=$i gid=$i" >> spec
	done

	atf_check -o ignore -e ignore makefs -B $1 -o version=$2 \
		-F spec -s 4000b ${IMG} ${DIR}
	atf_check -o ignore -e ignore tunefs -q user -F ${IMG}
	atf_check -s exit:0 -o 'match:NO USER QUOTA INODE \(CREATED\)' \
		-o 'match:USER QUOTA MISMATCH FOR ID 10: 0/0 SHOULD BE 0/1' \
		fsck_ffs -p -F ${IMG}
	atf_check -s exit:0 -o "match:${expected_files} files" \
		fsck_ffs -nf -F ${IMG}
}
