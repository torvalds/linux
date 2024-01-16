#!/bin/sh
# SPDX-License-Identifier: GPL-2.0

LC_ALL=C
sed -n -e 's/^[0]\+\(0[0-9a-fA-F]*\) . \(__vdso_[a-zA-Z0-9_]*\)$/\#define \2_offset\t0x\1/p'
