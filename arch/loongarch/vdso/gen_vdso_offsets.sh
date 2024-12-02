#!/bin/sh
# SPDX-License-Identifier: GPL-2.0

#
# Derived from RISC-V and ARM64:
# Author: Will Deacon <will.deacon@arm.com>
#
# Match symbols in the DSO that look like VDSO_*; produce a header file
# of constant offsets into the shared object.
#

LC_ALL=C sed -n -e 's/^00*/0/' -e \
's/^\([0-9a-fA-F]*\) . VDSO_\([a-zA-Z0-9_]*\)$/\#define vdso_offset_\2\t0x\1/p'
