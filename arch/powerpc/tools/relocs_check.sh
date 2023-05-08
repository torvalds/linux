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

bad_relocs=$(
${srctree}/scripts/relocs_check.sh "$@" |
	# These relocations are okay
	# On PPC64:
	#	R_PPC64_RELATIVE, R_PPC64_NONE
	# On PPC:
	#	R_PPC_RELATIVE, R_PPC_ADDR16_HI,
	#	R_PPC_ADDR16_HA,R_PPC_ADDR16_LO,
	#	R_PPC_NONE
	grep -F -w -v 'R_PPC64_RELATIVE
R_PPC64_NONE
R_PPC64_UADDR64
R_PPC_ADDR16_LO
R_PPC_ADDR16_HI
R_PPC_ADDR16_HA
R_PPC_RELATIVE
R_PPC_NONE'
)

if [ -z "$bad_relocs" ]; then
	exit 0
fi

num_bad=$(echo "$bad_relocs" | wc -l)
echo "WARNING: $num_bad bad relocations"
echo "$bad_relocs"
