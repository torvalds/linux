# $NetBSD: t_gpt.sh,v 1.15 2016/03/08 08:04:48 joerg Exp $
#
# Copyright (c) 2015 The NetBSD Foundation, Inc.
# All rights reserved.
#
# This code is derived from software contributed to The NetBSD Foundation
# by Christos Zoulas
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

bootblk=/usr/mdec/gptmbr.bin
size=10240
newsize=20480
shdr=34
disk=gpt.disk
uuid="........-....-....-....-............"
zero="00000000-0000-0000-0000-000000000000"
src=$(atf_get_srcdir)

silence() {
	atf_check -s exit:0 -o empty -e empty "$@"
}

inline() {
	local inline="$1"
	shift
	atf_check -s exit:0 -e empty -o inline:"$inline" "$@"
}

match() {
	local match="$1"
	shift
	atf_check -s exit:0 -e empty -o match:"$match" "$@"
}

matcherr() {
	local match="$1"
	shift
	atf_check -s exit:0 -o empty -e match:"$match" "$@"
}

file() {
	local file="$1"
	shift
	atf_check -s exit:0 -e empty -o file:"$file" "$@"
}

save() {
	local save="$1"
	shift
	atf_check -s exit:0 -e empty -o save:"$save" "$@"
}

zerodd() {
	silence dd conv=notrunc msgfmt=quiet if=/dev/zero of="$disk" "$@"
}

prepare() {
	rm -f "$disk"
	zerodd seek="$size" count=1
}

prepare_2part() {
	prepare
	silence gpt create "$disk"
	match "$(partaddmsg 1 34 1024)" gpt add -t efi -s 1024 "$disk"
	match "$(partaddmsg 2 1058 9150)" gpt add "$disk"
}

# Calling this from tests does not work. BUG!
check_2part() {
	file "$src/gpt.2part.show.normal" gpt show "$disk"
	file "$src/gpt.2part.show.uuid" gpt show -u "$disk"
}

partaddmsg() {
	echo "^$disk: Partition $1 added: $uuid $2 $3\$"
}

partresmsg() {
	echo "^$disk: Partition $1 resized: $2 $3\$"
}

partremmsg() {
	echo "^$disk: Partition $1 removed\$"
}

partlblmsg() {
	echo "^$disk: Partition $1 label changed\$"
}

partbootmsg() {
	echo "^$disk: Partition $1 marked as bootable\$"
}

recovermsg() {
	echo "^$disk: Recovered $1 GPT [a-z]* from $2\$"
}

migratemsg() {
	echo -n "^gpt: $disk: Partition $1 unknown type MSDOS, "
	echo 'using "Microsoft Basic Data"$'
}

attrmsg() {
	echo "^$disk: Partition $1 attributes updated\$"
}

typemsg() {
	echo "^$disk: Partition $1 type changed\$"
}

atf_test_case create_empty
create_empty_head() {
	atf_set "descr" "Create empty disk"
}

create_empty_body() {
	prepare
	silence gpt create "$disk"
	file "$src/gpt.empty.show.normal" gpt show "$disk"
}

atf_test_case create_2part
create_2part_head() {
	atf_set "descr" "Create 2 partition disk"
}

create_2part_body() {
	prepare_2part
	check_2part
}

atf_test_case change_attr_2part
change_attr_2part_head() {
	atf_set "descr" "Change the attribute of 2 partition disk"
}

change_attr_2part_body() {
	prepare_2part
	match "$(attrmsg 1)" gpt set -i 1 -a biosboot,bootme "$disk"
	save attr gpt show -i 1 "$disk"
	match "^Attributes: biosboot, bootme\$" tail -1 attr
	match "$(attrmsg 1)" gpt unset -i 1 -a biosboot,bootme "$disk"
	save attr gpt show -i 1 "$disk"
	match "^Attributes: None\$" tail -1 attr
}

atf_test_case change_type_2part
change_type_2part_head() {
	atf_set "descr" "Change the partition type type of 2 partition disk"
}

change_type_2part_body() {
	prepare_2part
	match "$(typemsg 1)" gpt type -i 1 -T apple "$disk"
	save type gpt show -i 1 "$disk"
	inline "Type: apple (48465300-0000-11aa-aa11-00306543ecac)\n" \
	    grep "^Type:" type
	match "$(typemsg 1)" gpt type -i 1 -T efi "$disk"
	save type gpt show -i 1 "$disk"
	inline "Type: efi (c12a7328-f81f-11d2-ba4b-00a0c93ec93b)\n" \
	    grep "^Type:" type
}

atf_test_case backup_2part
backup_2part_head() {
	atf_set "descr" "Backup 2 partition disk"
}

backup_2part_body() {
	prepare_2part
	save test.backup gpt backup "$disk"
	file "$src/gpt.backup" sed -e "s/$uuid/$zero/g" "test.backup"
}

atf_test_case restore_2part
restore_2part_head() {
	atf_set "descr" "Restore 2 partition disk"
}

restore_2part_body() {
	prepare_2part
	save test.backup gpt backup "$disk"
	prepare
	silence gpt restore -i test.backup "$disk"
	check_2part
}

atf_test_case recover_backup
recover_backup_head() {
	atf_set "descr" "Recover the backup GPT header and table"
}

recover_backup_body() {
	prepare_2part
	zerodd seek="$((size - shdr))" count="$shdr"
	match "$(recovermsg secondary primary)" gpt recover "$disk"
	check_2part
}

atf_test_case recover_primary
recover_primary_head() {
	atf_set "descr" "Recover the primary GPT header and table"
}

recover_primary_body() {
	prepare_2part
	zerodd seek=1 count="$shdr"
	match "$(recovermsg primary secondary)" gpt recover "$disk"
	check_2part
}

atf_test_case resize_2part
resize_2part_head() {
	atf_set "descr" "Resize a 2 partition disk and partition"
}

resize_2part_body() {
	prepare_2part
	zerodd seek="$newsize" count=1
	silence gpt resizedisk "$disk"
	file "$src/gpt.resizedisk.show.normal" gpt show "$disk"
	match "$(partresmsg 2 1058 19390)" gpt resize -i 2 "$disk"
	file "$src/gpt.resizepart.show.normal" gpt show "$disk"
}

atf_test_case remove_2part
remove_2part_head() {
	atf_set "descr" "Remove a partition from a 2 partition disk"
}

remove_2part_body() {
	prepare_2part
	match "$(partremmsg 1)" -e empty gpt remove \
	    -i 1 "$disk"
	file "$src/gpt.removepart.show.normal" \
	    gpt show "$disk"
}

atf_test_case label_2part
label_2part_head() {
	atf_set "descr" "Label partitions in a 2 partition disk"
}

label_2part_body() {
	prepare_2part
	match "$(partlblmsg 1)" gpt label -i 1 -l potato "$disk"
	match "$(partlblmsg 2)" gpt label -i 2 -l tomato "$disk"
	file "$src/gpt.2part.show.label" \
	    gpt show -l "$disk"
}

atf_test_case bootable_2part
bootable_2part_head() {
	atf_set "descr" "Make partition 2 bootable in a 2 partition disk"
	atf_set "require.files" "$bootblk"
}

bootable_2part_body() {
	prepare_2part
	match "$(partbootmsg 2)" gpt biosboot -i 2 "$disk"
	local bootsz="$(ls -l "$bootblk" | awk '{ print $5 }')"
	silence dd msgfmt=quiet if="$disk" of=bootblk bs=1 count="$bootsz"
	silence cmp "$bootblk" bootblk
	save bootattr gpt show -i 2 "$disk"
	match "^Attributes: biosboot\$" tail -1 bootattr
}

atf_test_case migrate_disklabel
migrate_disklabel_head() {
	atf_set "descr" "Migrate an MBR+disklabel disk to GPT"
}

migrate_disklabel_body() {
	prepare
	silence fdisk -fi "$disk"
	silence fdisk -fu0s "169/63/$((size / 10))" "$disk"
	silence disklabel -R "$disk" "$src/gpt.disklabel"
	matcherr "$(migratemsg 5)" gpt migrate "$disk"
	file "$src/gpt.disklabel.show.normal" gpt show "$disk"
}

atf_init_test_cases() {
	atf_add_test_case create_empty
	atf_add_test_case create_2part
	atf_add_test_case change_attr_2part
	atf_add_test_case change_type_2part
	atf_add_test_case backup_2part
	atf_add_test_case remove_2part
	atf_add_test_case restore_2part
	atf_add_test_case recover_backup
	atf_add_test_case recover_primary
	atf_add_test_case resize_2part
	atf_add_test_case label_2part
	atf_add_test_case bootable_2part
	atf_add_test_case migrate_disklabel
}
