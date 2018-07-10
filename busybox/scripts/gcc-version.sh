#!/bin/sh
#
# gcc-version gcc-command
#
# Prints the gcc version of `gcc-command' in a canonical 4-digit form
# such as `0295' for gcc-2.95, `0303' for gcc-3.3, etc.
#

compiler="$*"

MAJ_MIN=$(echo __GNUC__ __GNUC_MINOR__ | $compiler -E -xc - | tail -n 1)
printf '%02d%02d\n' $MAJ_MIN
