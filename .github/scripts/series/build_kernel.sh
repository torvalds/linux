#!/bin/bash
# SPDX-FileCopyrightText: 2023 Rivos Inc.
#
# SPDX-License-Identifier: Apache-2.0

set -x
set -euo pipefail

d=$(dirname "${BASH_SOURCE[0]}")
. $d/utils.sh

lnxroot=$(pwd)

# E.g. build_kernel.sh rv32 defconfig /path/to/fragment llvm
#      build_kernel.sh rv64 defconfig plain gcc
 
xlen=$1
config=$2
fragment=$3
toolchain=$4

install=${ci_root}/$(gen_kernel_name $xlen $config $fragment $toolchain)
output=${install}_build
triple=${ci_triple}

keep_build=0 # enable for kselftest

make_gcc() {
    make O=$output ARCH=riscv CROSS_COMPILE=${triple}- \
         "CC=ccache ${triple}-gcc" 'HOSTCC=ccache gcc' $*
}

make_llvm() {
    make O=$output ARCH=riscv CROSS_COMPILE=${triple}- \
         LLVM=1 LLVM_IAS=1 'CC=ccache clang' 'HOSTCC=ccache clang' $*
}

make_wrap() {
    if [[ $toolchain == "llvm" ]]; then
        make_llvm $*
    elif [[ $toolchain == "gcc-old" ]]; then
        oldpath=${PATH}
        export PATH=/opt/gcc-old/riscv64-linux/bin:${oldpath}
        make_gcc $*
        export PATH=${oldpath}
    else
        make_gcc $*
    fi
}

rm -rf ${output}
rm -rf ${install}
mkdir -p ${output}
mkdir -p ${install}

if [[ $config == "allmodconfig" || $config == "randconfig" ]]; then
    make_wrap KCONFIG_ALLCONFIG=$lnxroot/arch/riscv/configs/${xlen//rv/}-bit.config $config
    $lnxroot/scripts/kconfig/merge_config.sh -m -O $output $output/.config \
                                             <(echo "CONFIG_WERROR=n") \
                                             <(echo "CONFIG_DRM_WERROR=n") \
                                             <(echo "CONFIG_GCC_PLUGINS=n")
elif [[ $config == "kselftest" ]]; then
    apply_patches
    trap unapply_patches EXIT
    make_wrap defconfig
    make_wrap kselftest-merge
    $lnxroot/scripts/kconfig/merge_config.sh -y -m -O $output $output/.config \
					     <(echo "CONFIG_KERNEL_UNCOMPRESSED=y")
    make_wrap olddefconfig
    keep_build=1
elif [[ $config == "testsuites" ]]; then
    make_wrap ubuntu_defconfig
else
    if [[ $fragment == "plain" ]]; then
        $lnxroot/scripts/kconfig/merge_config.sh -y -m -O $output $lnxroot/arch/riscv/configs/$config \
                                                 $lnxroot/arch/riscv/configs/${xlen//rv/}-bit.config \
                                                 <(echo "CONFIG_KERNEL_UNCOMPRESSED=y")
    else
        $lnxroot/scripts/kconfig/merge_config.sh -y -m -O $output $lnxroot/arch/riscv/configs/$config \
                                                 $fragment \
                                                 $lnxroot/arch/riscv/configs/${xlen//rv/}-bit.config \
                                                 <(echo "CONFIG_KERNEL_UNCOMPRESSED=y")
    fi
    make_wrap olddefconfig
fi

make_wrap -j $(nproc) -Oline

make_wrap INSTALL_PATH=${install} install
make_wrap INSTALL_MOD_STRIP=1 INSTALL_MOD_PATH=${install} modules_install || true

if ! (( ${keep_build} )); then
    rm -rf ${output}
fi
