#!/bin/sh

# Depmod output may be hard to diff.
# This script sorts dependencies within "xx.ko: yy.ko zz.ko" lines,
# and sorts all lines too.
# Usage:
#
# [./busybox] depmod -n | ./depmod_process.sh | sort >OUTFILE
#
# and then you can diff OUTFILEs. Useful for comparing bbox depmod
# with module-init-tools depmod and such.

while read -r word rest; do
    if ! test "${word/*:/}"; then
	echo -n "$word "
	echo "$rest" | xargs -n1 | sort | xargs
    else
	echo "$word $rest";
    fi
done
