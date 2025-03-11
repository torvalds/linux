#!/bin/bash
# SPDX-License-Identifier: GPL-2.0-or-later
#
# This script checks vmlinux to ensure that all functions can call ftrace_caller() either directly,
# or through the stub, ftrace_tramp_text, at the end of kernel text.

# Error out if any command fails
set -e

# Allow for verbose output
if [ "$V" = "1" ]; then
	set -x
fi

if [ $# -lt 2 ]; then
	echo "$0 [path to nm] [path to vmlinux]" 1>&2
	exit 1
fi

# Have Kbuild supply the path to nm so we handle cross compilation.
nm="$1"
vmlinux="$2"

stext_addr=$($nm "$vmlinux" | grep -e " [TA] _stext$" | \
	cut -d' ' -f1 | tr '[:lower:]' '[:upper:]')
ftrace_caller_addr=$($nm "$vmlinux" | grep -e " T ftrace_caller$" | \
	cut -d' ' -f1 | tr '[:lower:]' '[:upper:]')
ftrace_tramp_addr=$($nm "$vmlinux" | grep -e " T ftrace_tramp_text$" | \
	cut -d' ' -f1 | tr '[:lower:]' '[:upper:]')

ftrace_caller_offset=$(echo "ibase=16;$ftrace_caller_addr - $stext_addr" | bc)
ftrace_tramp_offset=$(echo "ibase=16;$ftrace_tramp_addr - $ftrace_caller_addr" | bc)
sz_32m=$(printf "%d" 0x2000000)
sz_64m=$(printf "%d" 0x4000000)

# ftrace_caller - _stext < 32M
if [ "$ftrace_caller_offset" -ge "$sz_32m" ]; then
	echo "ERROR: ftrace_caller (0x$ftrace_caller_addr) is beyond 32MiB of _stext" 1>&2
	echo "ERROR: consider disabling CONFIG_FUNCTION_TRACER, or reducing the size \
		of kernel text" 1>&2
	exit 1
fi

# ftrace_tramp_text - ftrace_caller < 64M
if [ "$ftrace_tramp_offset" -ge "$sz_64m" ]; then
	echo "ERROR: kernel text extends beyond 64MiB from ftrace_caller" 1>&2
	echo "ERROR: consider disabling CONFIG_FUNCTION_TRACER, or reducing the size \
		of kernel text" 1>&2
	exit 1
fi
