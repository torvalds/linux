#!/bin/sh
# SPDX-License-Identifier: GPL-2.0+
# Copyright 2020 Palmer Dabbelt <palmerdabbelt@google.com>

sed 's!\([0-9a-f]*\) T \([a-z0-9_]*\)\(@@LINUX_4.15\)*!.global \2\n.set \2,0x\1!' \
| grep '^\.'
