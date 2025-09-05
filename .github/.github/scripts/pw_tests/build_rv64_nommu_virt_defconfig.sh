#!/bin/bash
# SPDX-License-Identifier: GPL-2.0
#
# Copyright (c) 2022 by Rivos Inc.

tmpdir=$(mktemp -d -p /build)
tmpfile=$(mktemp -p /build)
rc=0

tuxmake --wrapper ccache --target-arch riscv --directory . \
        --environment=KBUILD_BUILD_TIMESTAMP=@1621270510 \
        --environment=KBUILD_BUILD_USER=tuxmake --environment=KBUILD_BUILD_HOST=tuxmake \
        -o $tmpdir --toolchain gcc -z none -k nommu_virt_defconfig \
        CROSS_COMPILE=riscv64-linux- \
        >$tmpfile 2>&1 || rc=1

if [ $rc -ne 0 ]; then
    echo "Full log:"
    cat $tmpfile
    echo "warnings/errors:"
    grep "\(warning\|error\):" $tmpfile
fi

rm -rf $tmpdir $tmpfile

exit $rc
