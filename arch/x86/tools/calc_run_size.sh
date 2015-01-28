#!/bin/sh
#
# Calculate the amount of space needed to run the kernel, including room for
# the .bss and .brk sections.
#
# Usage:
# objdump -h a.out | sh calc_run_size.sh

NUM='\([0-9a-fA-F]*[ \t]*\)'
OUT=$(sed -n 's/^[ \t0-9]*.b[sr][sk][ \t]*'"$NUM$NUM$NUM$NUM"'.*/\1\4/p')
if [ -z "$OUT" ] ; then
	echo "Never found .bss or .brk file offset" >&2
	exit 1
fi

OUT=$(echo ${OUT# })
sizeA=$(printf "%d" 0x${OUT%% *})
OUT=${OUT#* }
offsetA=$(printf "%d" 0x${OUT%% *})
OUT=${OUT#* }
sizeB=$(printf "%d" 0x${OUT%% *})
OUT=${OUT#* }
offsetB=$(printf "%d" 0x${OUT%% *})

run_size=$(( $offsetA + $sizeA + $sizeB ))

# BFD linker shows the same file offset in ELF.
if [ "$offsetA" -ne "$offsetB" ] ; then
	# Gold linker shows them as consecutive.
	endB=$(( $offsetB + $sizeB ))
	if [ "$endB" != "$run_size" ] ; then
		printf "sizeA: 0x%x\n" $sizeA >&2
		printf "offsetA: 0x%x\n" $offsetA >&2
		printf "sizeB: 0x%x\n" $sizeB >&2
		printf "offsetB: 0x%x\n" $offsetB >&2
		echo ".bss and .brk are non-contiguous" >&2
		exit 1
	fi
fi

printf "%d\n" $run_size
exit 0
