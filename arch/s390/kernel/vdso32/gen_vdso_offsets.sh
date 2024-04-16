#!/bin/sh
# SPDX-License-Identifier: GPL-2.0

#
# Match symbols in the DSO that look like VDSO_*; produce a header file
# of constant offsets into the shared object.
#
# Doing this inside the Makefile will break the $(filter-out) function,
# causing Kbuild to rebuild the vdso-offsets header file every time.
#
# Inspired by arm64 version.
#

LC_ALL=C
sed -n 's/\([0-9a-f]*\) . __kernel_compat_\(.*\)/\#define vdso32_offset_\2\t0x\1/p'
