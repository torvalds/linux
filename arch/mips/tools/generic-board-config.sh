#!/bin/sh
#
# Copyright (C) 2017 Imagination Technologies
# Author: Paul Burton <paul.burton@imgtec.com>
#
# This program is free software; you can redistribute it and/or modify it
# under the terms of the GNU General Public License as published by the
# Free Software Foundation;  either version 2 of the  License, or (at your
# option) any later version.
#
# This script merges configuration fragments for boards supported by the
# generic MIPS kernel. It checks each for requirements specified using
# formatted comments, and then calls merge_config.sh to merge those
# fragments which have no unmet requirements.
#
# An example of requirements in your board config fragment might be:
#
# # require CONFIG_CPU_MIPS32_R2=y
# # require CONFIG_CPU_LITTLE_ENDIAN=y
#
# This would mean that your board is only included in kernels which are
# configured for little endian MIPS32r2 CPUs, and not for example in kernels
# configured for 64 bit or big endian systems.
#

srctree="$1"
objtree="$2"
ref_cfg="$3"
cfg="$4"
boards_origin="$5"
shift 5

cd "${srctree}"

# Only print Skipping... lines if the user explicitly specified BOARDS=. In the
# general case it only serves to obscure the useful output about what actually
# was included.
case ${boards_origin} in
"command line")
	print_skipped=1
	;;
environment*)
	print_skipped=1
	;;
*)
	print_skipped=0
	;;
esac

for board in $@; do
	board_cfg="arch/mips/configs/generic/board-${board}.config"
	if [ ! -f "${board_cfg}" ]; then
		echo "WARNING: Board config '${board_cfg}' not found"
		continue
	fi

	# For each line beginning with # require, cut out the field following
	# it & search for that in the reference config file. If the requirement
	# is not found then the subshell will exit with code 1, and we'll
	# continue on to the next board.
	grep -E '^# require ' "${board_cfg}" | \
	    cut -d' ' -f 3- | \
	    while read req; do
		case ${req} in
		*=y)
			# If we require something =y then we check that a line
			# containing it is present in the reference config.
			grep -Eq "^${req}\$" "${ref_cfg}" && continue
			;;
		*=n)
			# If we require something =n then we just invert that
			# check, considering the requirement met if there isn't
			# a line containing the value =y in the reference
			# config.
			grep -Eq "^${req/%=n/=y}\$" "${ref_cfg}" || continue
			;;
		*)
			echo "WARNING: Unhandled requirement '${req}'"
			;;
		esac

		[ ${print_skipped} -eq 1 ] && echo "Skipping ${board_cfg}"
		exit 1
	done || continue

	# Merge this board config fragment into our final config file
	./scripts/kconfig/merge_config.sh \
		-m -O ${objtree} ${cfg} ${board_cfg} \
		| grep -Ev '^(#|Using)'
done
