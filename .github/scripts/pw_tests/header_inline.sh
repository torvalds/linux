#!/bin/bash
# SPDX-License-Identifier: GPL-2.0
#
# Copyright (c) 2020 Facebook

inlines=$(
    git show -- '*.h' | grep -C1 -P '^\+static (?!(__always_)?inline).*\(';
    git show -- '*.h' | grep -C1 -P '^\+(static )?(?!(__always_)?inline )((unsigned|long|short) )*(char|bool|void|int|u[0-9]*) [0-9A-Za-z_]*\(.*\) *{'
       )

if [ -z "$inlines" ]; then
        exit 0
fi

msg="Detected static functions without inline keyword in header files:"
echo -e "$msg\n$inlines"
count=$( (echo "---"; echo "$inlines") | grep '^---$' | wc -l)
echo "$msg $count"
exit 1
