#!/bin/sh

# Copyright © 2015 IBM Corporation

# This program is free software; you can redistribute it and/or
# modify it under the terms of the GNU General Public License
# as published by the Free Software Foundation; either version
# 2 of the License, or (at your option) any later version.

# This script checks the relocations of a vmlinux for "suspicious"
# relocations.

# based on relocs_check.pl
# Copyright © 2009 IBM Corporation

if [ $# -lt 2 ]; then
	echo "$0 [path to objdump] [path to vmlinux]" 1>&2
	exit 1
fi

# Have Kbuild supply the path to objdump so we handle cross compilation.
objdump="$1"
vmlinux="$2"

bad_relocs=$(
"$objdump" -R "$vmlinux" |
	# Only look at relocation lines.
	grep -E '\<R_' |
	# These relocations are okay
	# On PPC64:
	#	R_PPC64_RELATIVE, R_PPC64_NONE
	#	R_PPC64_ADDR64 mach_<name>
	#	R_PPC64_ADDR64 __crc_<name>
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
	grep -E -v '\<R_PPC64_ADDR64[[:space:]]+mach_' |
	grep -E -v '\<R_PPC64_ADDR64[[:space:]]+__crc_'
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
