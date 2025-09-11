#!/bin/bash
# SPDX-FileCopyrightText: 2024 Rivos Inc.
#
# SPDX-License-Identifier: Apache-2.0

set -euox pipefail
d=$(dirname "${BASH_SOURCE[0]}")
. $d/series/utils.sh

logs=$(get_logs_dir)
f=${logs}/build_ubuntu_defconfig.log

date -Iseconds | tee -a ${f}
echo "Build an ubuntu kernel" | tee -a ${f}
echo "Top 16 commits" | tee -a ${f}
git log -16 --abbrev=12 --pretty="commit %h (\"%s\")" | tee -a ${f}

kernel_base_sha=$(git log -1 --pretty=%H $(git log -1 --reverse --pretty=%H .github)^)
echo "build_name $(git describe --tags ${kernel_base_sha})" | tee -a ${f}
build_name=$(git describe --tags ${kernel_base_sha})

# Build the kernel that will run LTP
export CI_TRIPLE="riscv64-unknown-linux-gnu"
# Use a CFI-enabled toolchain
export PATH=/build/INSTALL_Sept24/bin:$PATH
cp $d/series/kconfigs/ubuntu_defconfig arch/riscv/configs/
$d/series/kernel_builder.sh rv64 testsuites plain gcc | tee -a ${f}

kernel_dir="/build/$(gen_kernel_name rv64 testsuites plain gcc)"
echo $build_name > $kernel_dir/kernel_version
#tar cJvf --exclude $(basename $kernel_path) modules.tar.xz /build/$(gen_kernel_name rv64 testsuites plain gcc)/
