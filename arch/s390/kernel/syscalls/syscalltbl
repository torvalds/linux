#!/bin/sh
# SPDX-License-Identifier: GPL-2.0
#
# Generate system call table and header files
#
# Copyright IBM Corp. 2018
# Author(s):  Hendrik Brueckner <brueckner@linux.vnet.ibm.com>

#
# File path to the system call table definition.
# You can set the path with the -i option.  If omitted,
# system call table definitions are read from standard input.
#
SYSCALL_TBL=""


create_syscall_table_entries()
{
	local nr abi name entry64 entry32 _ignore
	local temp=$(mktemp ${TMPDIR:-/tmp}/syscalltbl-common.XXXXXXXXX)

	(
	#
	# Initialize with 0 to create an NI_SYSCALL for 0
	#
	local prev_nr=0 prev_32=sys_ni_syscall prev_64=sys_ni_syscall
	while read nr abi name entry64 entry32 _ignore; do
		test x$entry32 = x- && entry32=sys_ni_syscall
		test x$entry64 = x- && entry64=sys_ni_syscall

		if test $prev_nr -eq $nr; then
			#
			# Same syscall but different ABI, just update
			# the respective entry point
			#
			case $abi in
			32)
				prev_32=$entry32
			;;
			64)
				prev_64=$entry64
			;;
			esac
			continue;
		else
			printf "%d\t%s\t%s\n" $prev_nr $prev_64 $prev_32
		fi

		prev_nr=$nr
		prev_64=$entry64
		prev_32=$entry32
	done
	printf "%d\t%s\t%s\n" $prev_nr $prev_64 $prev_32
	) >> $temp

	#
	# Check for duplicate syscall numbers
	#
	if ! cat $temp |cut -f1 |uniq -d 2>&1; then
		echo "Error: generated system call table contains duplicate entries: $temp" >&2
		exit 1
	fi

	#
	# Generate syscall table
	#
	prev_nr=0
	while read nr entry64 entry32; do
		while test $prev_nr -lt $((nr - 1)); do
			printf "NI_SYSCALL\n"
			prev_nr=$((prev_nr + 1))
		done
		if test x$entry64 = xsys_ni_syscall &&
		   test x$entry32 = xsys_ni_syscall; then
			printf "NI_SYSCALL\n"
		else
			printf "SYSCALL(%s,%s)\n" $entry64 $entry32
		fi
		prev_nr=$nr
	done < $temp
	rm $temp
}

generate_syscall_table()
{
	cat <<-EoHEADER
	/* SPDX-License-Identifier: GPL-2.0 */
	/*
	 * Definitions for sys_call_table, each line represents an
	 * entry in the table in the form
	 * SYSCALL(64 bit syscall, 31 bit emulated syscall)
	 *
	 * This file is meant to be included from entry.S.
	 */

	#define NI_SYSCALL SYSCALL(sys_ni_syscall,sys_ni_syscall)

EoHEADER
	grep -Ev '^(#|[[:blank:]]*$)' $SYSCALL_TBL	\
		|sort -k1 -n				\
		|create_syscall_table_entries
}

create_header_defines()
{
	local nr abi name _ignore

	while read nr abi name _ignore; do
		printf "#define __NR_%s %d\n" $name $nr
	done
}

normalize_fileguard()
{
	local fileguard="$1"

	echo "$1" |tr '[[:lower:]]' '[[:upper:]]' \
		  |sed -e 's/[^A-Z0-9_]/_/g' -e 's/__/_/g'
}

generate_syscall_header()
{
	local abis=$(echo "($1)" | tr ',' '|')
	local filename="$2"
	local fileguard suffix

	if test "$filename"; then
		fileguard=$(normalize_fileguard "__UAPI_ASM_S390_$2")
	else
		case "$abis" in
		*64*) suffix=64 ;;
		*32*) suffix=32 ;;
		esac
		fileguard=$(normalize_fileguard "__UAPI_ASM_S390_SYSCALLS_$suffix")
	fi

	cat <<-EoHEADER
	/* SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note */
	#ifndef ${fileguard}
	#define ${fileguard}

EoHEADER

	grep -E "^[[:digit:]]+[[:space:]]+${abis}" $SYSCALL_TBL	\
		|sort -k1 -n					\
		|create_header_defines

	cat <<-EoFOOTER

	#endif /* ${fileguard} */
EoFOOTER
}

__max_syscall_nr()
{
	local abis=$(echo "($1)" | tr ',' '|')

	grep -E "^[[:digit:]]+[[:space:]]+${abis}" $SYSCALL_TBL	 \
		|sed -ne 's/^\([[:digit:]]*\)[[:space:]].*/\1/p' \
		|sort -n					 \
		|tail -1
}


generate_syscall_nr()
{
	local abis="$1"
	local max_syscall_nr num_syscalls

	max_syscall_nr=$(__max_syscall_nr "$abis")
	num_syscalls=$((max_syscall_nr + 1))

	cat <<-EoHEADER
	/* SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note */
	#ifndef __ASM_S390_SYSCALLS_NR
	#define __ASM_S390_SYSCALLS_NR

	#define NR_syscalls ${num_syscalls}

	#endif /* __ASM_S390_SYSCALLS_NR */
EoHEADER
}


#
# Parse command line arguments
#
do_syscall_header=""
do_syscall_table=""
do_syscall_nr=""
output_file=""
abi_list="common,64"
filename=""
while getopts ":HNSXi:a:f:" arg; do
	case $arg in
	a)
		abi_list="$OPTARG"
		;;
	i)
		SYSCALL_TBL="$OPTARG"
		;;
	f)
		filename=${OPTARG##*/}
		;;
	H)
		do_syscall_header=1
		;;
	N)
		do_syscall_nr=1
		;;
	S)
		do_syscall_table=1
		;;
	X)
		set -x
		;;
	:)
		echo "Missing argument for -$OPTARG" >&2
		exit 1
	;;
	\?)
		echo "Invalid option specified" >&2
		exit 1
	;;
	esac
done

test "$do_syscall_header" && generate_syscall_header "$abi_list" "$filename"
test "$do_syscall_table" && generate_syscall_table
test "$do_syscall_nr" && generate_syscall_nr "$abi_list"

exit 0
