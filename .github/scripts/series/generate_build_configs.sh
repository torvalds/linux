#!/bin/bash
# SPDX-FileCopyrightText: 2023 Rivos Inc.
#
# SPDX-License-Identifier: Apache-2.0

set -euo pipefail

d=$(dirname "${BASH_SOURCE[0]}")

toolchains="gcc llvm"

# SKIP_BUILD_CONFIG_TOOLCHAIN=

do_generate_toolchain () {
    local toolchain=$1

    if [ ! -z "${SKIP_BUILD_CONFIG_TOOLCHAIN:-}" ] && echo $toolchain | egrep -wq "$SKIP_BUILD_CONFIG_TOOLCHAIN"; then
        return 1
    fi
    return 0
}

while read xlen config fragment; do
    if [ $xlen == "rv32" ] && [[ "$config" =~ "k210" ]]; then
        continue
    fi

    if do_generate_toolchain "gcc"; then
        echo $xlen $config $fragment gcc
    fi
    if do_generate_toolchain "llvm"; then
        echo $xlen $config $fragment llvm
    fi
done < <($d/generate_kconfigs.sh)

echo rv64 allmodconfig plain gcc-old
