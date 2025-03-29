#!/bin/bash
# SPDX-FileCopyrightText: 2023 Rivos Inc.
#
# SPDX-License-Identifier: Apache-2.0

set -euo pipefail

d=$(dirname "${BASH_SOURCE[0]}")

rv64_rootfs="alpine ubuntu"
rv32_rootfs="buildroot_glibc"

# SKIP_TEST_RUN_ROOTFS=

print() {
    if [ ! -z "${SKIP_TEST_RUN_ROOTFS:-}" ]; then
        if echo $* | egrep -wq "$SKIP_TEST_RUN_ROOTFS"; then
            return
        fi
    fi

    echo $*
}

while read xlen config fragment image toolchain; do
    if [[ "$config" =~ "nommu" ]]; then
        continue
    fi
    if [[ "$config" =~ "allmodconfig" ]]; then
        continue
    fi
    if [[ "$config" =~ "randconfig" ]]; then
        continue
    fi
    if [[ "$config" =~ "kselftest" ]]; then
        print $xlen $config $fragment $image $toolchain ubuntu
        continue
    fi

    if [[ $xlen == "rv64" ]]; then
        print $xlen $config $fragment $image $toolchain alpine
        print $xlen $config $fragment $image $toolchain ubuntu
    else
        print $xlen $config $fragment $image $toolchain buildroot_glibc
        print $xlen $config $fragment $image $toolchain buildroot_musl
    fi
done < <($d/generate_build_configs.sh)
