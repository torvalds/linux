#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-or-later
#
# Copyright © 2008 IBM Corporation
#

# This script checks prom_init.o to see what external symbols it
# is using, if it finds symbols not in the whitelist it returns
# an error. The point of this is to discourage people from
# intentionally or accidentally adding new code to prom_init.c
# which has side effects on other parts of the kernel.

# If you really need to reference something from prom_init.o add
# it to the list below:

grep "^CONFIG_KASAN=y$" .config >/dev/null
if [ $? -eq 0 ]
then
	MEM_FUNCS="__memcpy __memset"
else
	MEM_FUNCS="memcpy memset"
fi

WHITELIST="add_reloc_offset __bss_start __bss_stop copy_and_flush
_end enter_prom $MEM_FUNCS reloc_offset __secondary_hold
__secondary_hold_acknowledge __secondary_hold_spinloop __start
logo_linux_clut224 btext_prepare_BAT
reloc_got2 kernstart_addr memstart_addr linux_banner _stext
__prom_init_toc_start __prom_init_toc_end btext_setup_display TOC."

NM="$1"
OBJ="$2"

ERROR=0

check_section()
{
    file=$1
    section=$2
    size=$(objdump -h -j $section $file 2>/dev/null | awk "\$2 == \"$section\" {print \$3}")
    size=${size:-0}
    if [ $size -ne 0 ]; then
	ERROR=1
	echo "Error: Section $section not empty in prom_init.c" >&2
    fi
}

for UNDEF in $($NM -u $OBJ | awk '{print $2}')
do
	# On 64-bit nm gives us the function descriptors, which have
	# a leading . on the name, so strip it off here.
	UNDEF="${UNDEF#.}"

	if [ $KBUILD_VERBOSE ]; then
		if [ $KBUILD_VERBOSE -ne 0 ]; then
			echo "Checking prom_init.o symbol '$UNDEF'"
		fi
	fi

	OK=0
	for WHITE in $WHITELIST
	do
		if [ "$UNDEF" = "$WHITE" ]; then
			OK=1
			break
		fi
	done

	# ignore register save/restore funcitons
	case $UNDEF in
	_restgpr_*|_restgpr0_*|_rest32gpr_*)
		OK=1
		;;
	_savegpr_*|_savegpr0_*|_save32gpr_*)
		OK=1
		;;
	esac

	if [ $OK -eq 0 ]; then
		ERROR=1
		echo "Error: External symbol '$UNDEF' referenced" \
		     "from prom_init.c" >&2
	fi
done

check_section $OBJ .data
check_section $OBJ .bss
check_section $OBJ .init.data

exit $ERROR
