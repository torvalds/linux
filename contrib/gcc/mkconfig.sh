#! /bin/sh

# Copyright (C) 2001, 2002, 2006 Free Software Foundation, Inc.
# This file is part of GCC.

# GCC is free software; you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation; either version 2, or (at your option)
# any later version.

# GCC is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.

# You should have received a copy of the GNU General Public License
# along with GCC; see the file COPYING.  If not, write to
# the Free Software Foundation, 51 Franklin Street, Fifth Floor,
# Boston MA 02110-1301, USA.


# Generate gcc's various configuration headers:
# config.h, tconfig.h, bconfig.h, tm.h, and tm_p.h.
# $1 is the file to generate.  DEFINES, HEADERS, and possibly
# TARGET_CPU_DEFAULT are expected to be set in the environment.

if [ -z "$1" ]; then
    echo "Usage: DEFINES='list' HEADERS='list' \\" >&2
    echo "  [TARGET_CPU_DEFAULT='default'] mkconfig.sh FILE" >&2
    exit 1
fi

output=$1
rm -f ${output}T

# This converts a file name into header guard macro format.
hg_sed_expr='y,abcdefghijklmnopqrstuvwxyz./,ABCDEFGHIJKLMNOPQRSTUVWXYZ__,'
header_guard=GCC_`echo ${output} | sed -e ${hg_sed_expr}`

# Add multiple inclusion protection guard, part one.
echo "#ifndef ${header_guard}" >> ${output}T
echo "#define ${header_guard}" >> ${output}T

# A special test to ensure that build-time files don't blindly use
# config.h.
if test x"$output" = x"config.h"; then
  echo "#ifdef GENERATOR_FILE" >> ${output}T
  echo "#error config.h is for the host, not build, machine." >> ${output}T
  echo "#endif" >> ${output}T
fi

# Define TARGET_CPU_DEFAULT if the system wants one.
# This substitutes for lots of *.h files.
if [ "$TARGET_CPU_DEFAULT" != "" ]; then
    echo "#define TARGET_CPU_DEFAULT ($TARGET_CPU_DEFAULT)" >> ${output}T
fi

# Provide defines for other macros set in config.gcc for this file.
for def in $DEFINES; do
    echo "#ifndef $def" | sed 's/=.*//' >> ${output}T
    echo "# define $def" | sed 's/=/ /' >> ${output}T
    echo "#endif" >> ${output}T
done

# The first entry in HEADERS may be auto-FOO.h ;
# it wants to be included even when not -DIN_GCC.
if [ -n "$HEADERS" ]; then
    set $HEADERS
    case "$1" in auto-* )
	echo "#include \"$1\"" >> ${output}T
	shift
	;;
    esac
    if [ $# -ge 1 ]; then
	echo '#ifdef IN_GCC' >> ${output}T
	for file in "$@"; do
	    echo "# include \"$file\"" >> ${output}T
	done
	echo '#endif' >> ${output}T
    fi
fi

# If this is tm.h, now include insn-constants.h and insn-flags.h only
# if IN_GCC is defined but neither GENERATOR_FILE nor USED_FOR_TARGET
# is defined.  (Much of this is temporary.)

case $output in
    tm.h )
        cat >> ${output}T <<EOF
#if defined IN_GCC && !defined GENERATOR_FILE && !defined USED_FOR_TARGET
# include "insn-constants.h"
# include "insn-flags.h"
#endif
EOF
    ;;
esac

# Add multiple inclusion protection guard, part two.
echo "#endif /* ${header_guard} */" >> ${output}T

# Avoid changing the actual file if possible.
if [ -f $output ] && cmp ${output}T $output >/dev/null 2>&1; then
    echo $output is unchanged >&2
    rm -f ${output}T
else
    mv -f ${output}T $output
fi

# Touch a stamp file for Make's benefit.
rm -f cs-$output
echo timestamp > cs-$output
