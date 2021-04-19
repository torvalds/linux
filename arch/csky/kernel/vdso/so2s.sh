#!/bin/sh
# SPDX-License-Identifier: GPL-2.0+

sed 's!\([0-9a-f]*\) T \([a-z0-9_]*\)\(@@LINUX_5.10\)*!.global \2\n.set \2,0x\1!' \
| grep '^\.'
