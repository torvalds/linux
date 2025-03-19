#!/bin/bash
# SPDX-FileCopyrightText: 2023 Rivos Inc.
#
# SPDX-License-Identifier: Apache-2.0

set -euo pipefail

d=$(dirname "${BASH_SOURCE[0]}")
lnxroot=$(pwd)
kconfigs=$d/kconfigs

builtin_skip="rv32_defconfig"
builtin_allow=""

print() {
    if [ ! -z "${SKIP_KCONFIG:-}" ]; then
        if echo $* | egrep -q "$SKIP_KCONFIG"; then
            return
        fi
    fi
    if [ ! -z "$builtin_skip" ]; then
        if echo $* | egrep -q "$builtin_skip"; then
            return
        fi
    fi
    if [ ! -z "${ALLOW_KCONFIG:-}" ]; then
        if echo $* | egrep -q "$ALLOW_KCONFIG"; then
            echo $*
        fi
        return
    fi
    if [ ! -z "$builtin_allow" ]; then
        if echo $* | egrep -q "$builtin_allow"; then
            echo $*
        fi
        return
    fi

    echo $*
}

# Too much? Override by uncommenting below:
# print rv64 defconfig "plain" && exit 0

defconfigs=$(find $lnxroot/arch/riscv/configs/ -type f -name '*defconfig' -printf '%f\n')
for i in $defconfigs; do
    for xlen in 32 64; do
        frags=$(echo $i && find $kconfigs/$i -type f -printf '%f\n' 2>/dev/null || :)
        for frag in $frags; do
            if [ $frag == $i ]; then
                fn=${xlen}__${i}
                fr="plain"
            else
                fn=${xlen}_${frag}__$i
                fr=$(readlink -f $kconfigs/$i/$frag)
            fi

            print rv$xlen $i $fr
        done
    done
done

#special case set KCONFIG_ALLCONFIG
print rv32 allmodconfig "plain"
print rv64 allmodconfig "plain"
