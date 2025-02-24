#!/bin/bash
# SPDX-FileCopyrightText: 2024 Rivos Inc.
#
# SPDX-License-Identifier: Apache-2.0

set -x
set -euo pipefail

d=$(dirname "${BASH_SOURCE[0]}")
. $d/utils.sh

lnxroot=$(pwd)

# E.g. build_selftest.sh rv64 kselftest-bpf plain gcc
 
xlen=$1
config=$2
fragment=$3
toolchain=$4

if ! [[ "$config" =~ ^kselftest ]]; then
    echo "Not a selftest config: please try kselftest kselftest-bpf kselftest-net"
    exit 1
fi

install=${ci_root}/$(gen_kernel_name $xlen $config $fragment $toolchain)
output=${install}_build
triple=${ci_triple}

if ! [[ -d $output ]]; then
    echo "Cannot find kernel build"
    exit 1
fi

make_gcc() {
    make O=$output ARCH=riscv CROSS_COMPILE=${triple}- \
         "CC=${triple}-gcc" 'HOSTCC=gcc' $*
}

make_llvm() {
    make O=$output ARCH=riscv CROSS_COMPILE=${triple}- \
         LLVM=1 LLVM_IAS=1 'CC=clang' 'HOSTCC=clang' $*
}

make_wrap() {
    if [ $toolchain == "llvm" ]; then
        make_llvm $*
    else
        make_gcc $*
    fi
}

apply_patches
trap unapply_patches EXIT

make_wrap -j $(($(nproc)-1)) headers

make_wrap SKIP_TARGETS="bpf" -j $(($(nproc)-1)) -C tools/testing/selftests install
make_wrap TARGETS="bpf" SKIP_TARGETS="" -j $(($(nproc)-1)) -C tools/testing/selftests
make_wrap TARGETS="bpf" SKIP_TARGETS="" COLLECTION="bpf" -j $(($(nproc)-1)) \
	  -C tools/testing/selftests/bpf emit_tests | grep -e '^bpf:' \
	  >> $output/kselftest/kselftest_install/kselftest-list.txt
cp -R $output/kselftest/bpf $output/kselftest/kselftest_install
