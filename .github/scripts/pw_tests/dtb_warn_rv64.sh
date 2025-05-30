#!/bin/bash
# SPDX-License-Identifier: GPL-2.0
#
# Copyright (C) 2019 Netronome Systems, Inc.

# Modified tests/patch/build_defconfig_warn.sh for RISC-V builds

tmpfile_o=$(mktemp -p /build)
tmpfile_n=$(mktemp -p /build)

tmpdir_o=$(mktemp -d -p /build)
tmpdir_n=$(mktemp -d -p /build)

rc=0

echo "Redirect to $tmpfile_o and $tmpfile_n"

HEAD=$(git rev-parse HEAD)

echo "Tree base:"
git log -1 --pretty='%h ("%s")' HEAD~

git checkout -q HEAD~

echo "Building the tree before the patch"

make -C . O=$tmpdir_o ARCH=riscv CROSS_COMPILE=riscv64-linux- \
        defconfig

make -C . O=$tmpdir_o ARCH=riscv CROSS_COMPILE=riscv64-linux- \
        dtbs_check W=1 -j$(nproc) \
        2> >(tee $tmpfile_o)

incumbent=$(cat $tmpfile_o | grep -v "From schema" | wc -l)

echo "Building the tree with the patch"

git checkout -q $HEAD

make -C . O=$tmpdir_n ARCH=riscv CROSS_COMPILE=riscv64-linux- \
        defconfig

make -C . O=$tmpdir_n ARCH=riscv CROSS_COMPILE=riscv64-linux- \
        dtbs_check W=1 -j$(nproc) \
        2> >(tee $tmpfile_n) || rc=1

current=$(cat $tmpfile_n | grep -v "From schema" | wc -l)

if [ $current -gt $incumbent ]; then
        echo "Errors and warnings before: $incumbent this patch: $current"
        echo "New errors added"
        sed -i 's|^.*arch|arch|g' $tmpfile_o
        sed -i 's|^.*arch|arch|g' $tmpfile_n
        diff -U 0 $tmpfile_o $tmpfile_n

        rc=1
fi

rm -rf $tmpdir_o $tmpdir_n $tmpfile_o $tmpfile_n

exit $rc
