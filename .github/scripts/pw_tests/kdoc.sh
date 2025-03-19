#!/bin/bash
# SPDX-License-Identifier: GPL-2.0
#
# Copyright (C) 2019 Netronome Systems, Inc.
# Copyright (c) 2020 Facebook

tmpfile_o=$(mktemp -p /build)
tmpfile_n=$(mktemp -p /build)
rc=0

files=$(git show --pretty="" --name-only HEAD)

HEAD=$(git rev-parse HEAD)

echo "Checking the tree before the patch"
git checkout -q HEAD~
./scripts/kernel-doc -none $files 2> >(tee $tmpfile_o)

incumbent=$(grep -v 'Error: Cannot open file ' $tmpfile_o | wc -l)

echo "Checking the tree with the patch"

git checkout -q $HEAD
./scripts/kernel-doc -none $files 2> >(tee $tmpfile_n)

current=$(grep -v 'Error: Cannot open file ' $tmpfile_n | wc -l)


if [ $current -gt $incumbent ]; then
        echo "Errors and warnings before: $incumbent this patch: $current"
        echo "New warnings added"
        diff $tmpfile_o $tmpfile_n

        echo "Per-file breakdown"
        tmpfile_fo=$(mktemp -p /build)
        tmpfile_fn=$(mktemp -p /build)

        grep -i "\(warn\|error\)" $tmpfile_o | sed -n 's@\(^\.\./[/a-zA-Z0-9_.-]*.[ch]\):.*@\1@p' | sort | uniq -c \
          >$tmpfile_fo
        grep -i "\(warn\|error\)" $tmpfile_n | sed -n 's@\(^\.\./[/a-zA-Z0-9_.-]*.[ch]\):.*@\1@p' | sort | uniq -c \
          >$tmpfile_fn

        diff $tmpfile_fo $tmpfile_fn
        rm $tmpfile_fo $tmpfile_fn
        rc=1
fi

rm $tmpfile_o $tmpfile_n

exit $rc
