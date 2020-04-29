#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-or-later

# Copyright © 2015 IBM Corporation


# This script checks the relocations of a vmlinux for "suspicious"
# relocations.

# based on relocs_check.pl
# Copyright © 2009 IBM Corporation

if [ $# -lt 3 ]; then
	echo "$0 [path to objdump] [path to nm] [path to vmlinux]" 1>&2
	exit 1
fi

# Have Kbuild supply the path to objdump and nm so we handle cross compilation.
objdump="$1"
nm="$2"
vmlinux="$3"

# Remove from the bad relocations those that match an undefined weak symbol
# which will result in an absolute relocation to 0.
# Weak unresolved symbols are of that form in nm output:
# "                  w _binary__btf_vmlinux_bin_end"
undef_weak_symbols=$($nm "$vmlinux" | awk '$1 ~ /w/ { print $2 }')

bad_relocs=$(
$objdump -R "$vmlinux" |
	# Only look at relocation lines.
	grep -E '\<R_' |
	# These relocations are okay
	# On PPC64:
	#	R_PPC64_RELATIVE, R_PPC64_NONE
	# On PPC:
	#	R_PPC_RELATIVE, R_PPC_ADDR16_HI,
	#	R_PPC_ADDR16_HA,R_PPC_ADDR16_LO,
	#	R_PPC_NONE
	grep -F -w -v 'R_PPC64_RELATIVE
R_PPC64_NONE
R_PPC_ADDR16_LO
R_PPC_ADDR16_HI
R_PPC_ADDR16_HA
R_PPC_RELATIVE
R_PPC_NONE' |
	([ "$undef_weak_symbols" ] && grep -F -w -v "$undef_weak_symbols" || cat)
)

if [ -z "$bad_relocs" ]; then
	exit 0
fi

num_bad=$(echo "$bad_relocs" | wc -l)
echo "WARNING: $num_bad bad relocations"
echo "$bad_relocs"

# If we see this type of relocation it's an idication that
# we /may/ be using an old version of binutils.
if echo "$bad_relocs" | grep -q -F -w R_PPC64_UADDR64; then
	echo "WARNING: You need at least binutils >= 2.19 to build a CONFIG_RELOCATABLE kernel"
fi
