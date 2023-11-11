#!/bin/sh
# SPDX-License-Identifier: GPL-2.0
nm="$1"
file="$2"
$nm "$file" | grep '^ *U' > /dev/null 2>&1
if [ $? -eq 1 ]; then
    exit 0
else
    echo "$file: undefined symbols found" >&2
    exit 1
fi
